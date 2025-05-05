/*
 * Copyright (c) 2023 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_module_detector

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zmk/keymap.h>
#include <zmk/drivers/module_detector.h>

LOG_MODULE_REGISTER(module_detector, CONFIG_MODULE_DETECTOR_LOG_LEVEL);

struct module_detector_callback {
    module_type_changed_callback_t callback;
    void *user_data;
};

#define MAX_CALLBACKS 5

struct module_detector_data {
    const struct device *dev;
    struct adc_sequence as;
    uint16_t *as_buff;
    struct k_work_delayable init_work;
    struct k_work sampling_work;
    struct k_timer sampling_timer;
    enum module_type current_module_type;
    enum module_type last_reported_type;
    int32_t last_mv;
    bool ready;
    bool enabled;

    // 回调相关
    struct module_detector_callback callbacks[MAX_CALLBACKS];
    uint8_t callback_count;
    
    // 去抖动相关
    struct k_work_delayable debounce_work;
    enum module_type debounce_type;
    int64_t last_change_time;
};

struct module_detector_config {
    uint32_t sampling_hz;
    struct adc_dt_spec adc_channel;
    uint32_t detection_delay_ms;
    uint32_t voltage_ranges[10]; // 最多支持5种模块类型，每种类型需要min和max
};

// 模块类型字符串描述
static const char *module_type_strings[] = {
    "None",
    "Knob",
    "Joystick",
    "Trackball",
    "Slider",
    "Other"
};

// 获取模块类型字符串描述
const char *module_type_to_str(enum module_type type) {
    if (type >= 0 && type < ARRAY_SIZE(module_type_strings)) {
        return module_type_strings[type];
    }
    return "Unknown";
}

// 根据电压值检测模块类型
static enum module_type detect_module_type(const struct module_detector_config *config, int32_t mv) {
    // 电压范围定义： [module1_min, module1_max, module2_min, module2_max, ...]
    // 检查各个模块的电压范围
    if (mv < config->voltage_ranges[0]) {
        return MODULE_TYPE_NONE;
    }
    
    for (int i = 0; i < 5; i++) { // 最多5种模块类型
        uint32_t min_mv = config->voltage_ranges[i*2];
        uint32_t max_mv = config->voltage_ranges[i*2+1];
        if (mv >= min_mv && mv < max_mv) {
            return i + 1; // 模块类型从1开始
        }
    }
    
    return MODULE_TYPE_OTHER;
}

// 触发所有回调函数
static void trigger_callbacks(struct module_detector_data *data, enum module_type type) {
    for (int i = 0; i < data->callback_count; i++) {
        if (data->callbacks[i].callback) {
            data->callbacks[i].callback(type, data->callbacks[i].user_data);
        }
    }
}

// 去抖动完成，确认模块类型变化
static void debounce_work_handler(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct module_detector_data *data = CONTAINER_OF(work_delayable, 
                                                    struct module_detector_data, debounce_work);
    
    // 确认模块类型变化
    if (data->current_module_type != data->last_reported_type) {
        enum module_type new_type = data->current_module_type;
        data->last_reported_type = new_type;
        
        LOG_INF("Module type changed to %s", module_type_to_str(new_type));
        
        // 触发回调
        trigger_callbacks(data, new_type);
        
        // 如果配置了自动层切换，则切换到对应层
#if defined(CONFIG_MODULE_DETECTOR_CALLBACK_LAYER) && defined(CONFIG_ZMK_KEYMAP)
        int layer = -1;
        switch (new_type) {
            case MODULE_TYPE_KNOB:
                layer = CONFIG_MODULE_DETECTOR_LAYER_KNOB;
                break;
            case MODULE_TYPE_JOYSTICK:
                layer = CONFIG_MODULE_DETECTOR_LAYER_JOYSTICK;
                break;
            case MODULE_TYPE_TRACKBALL:
                layer = CONFIG_MODULE_DETECTOR_LAYER_TRACKBALL;
                break;
            case MODULE_TYPE_SLIDER:
                layer = CONFIG_MODULE_DETECTOR_LAYER_SLIDER;
                break;
            case MODULE_TYPE_OTHER:
                layer = CONFIG_MODULE_DETECTOR_LAYER_OTHER;
                break;
            default:
                // 无模块或未知类型，不切换层
                break;
        }
        
        if (layer >= 0) {
            LOG_INF("Activating layer %d for module type %s", layer, module_type_to_str(new_type));
            zmk_keymap_layer_activate(layer);
        } else if (new_type == MODULE_TYPE_NONE) {
            // 如果是无模块，则尝试取消激活所有模块相关的层
            for (int l = 0; l < 5; l++) {
                int layer_to_deactivate = -1;
                switch (l) {
                    case 0: layer_to_deactivate = CONFIG_MODULE_DETECTOR_LAYER_KNOB; break;
                    case 1: layer_to_deactivate = CONFIG_MODULE_DETECTOR_LAYER_JOYSTICK; break;
                    case 2: layer_to_deactivate = CONFIG_MODULE_DETECTOR_LAYER_TRACKBALL; break;
                    case 3: layer_to_deactivate = CONFIG_MODULE_DETECTOR_LAYER_SLIDER; break;
                    case 4: layer_to_deactivate = CONFIG_MODULE_DETECTOR_LAYER_OTHER; break;
                }
                if (layer_to_deactivate >= 0) {
                    zmk_keymap_layer_deactivate(layer_to_deactivate);
                }
            }
        }
#endif

        // 如果配置了事件回调，发送事件
#if defined(CONFIG_MODULE_DETECTOR_CALLBACK_EVENT)
        input_report(data->dev, EV_MSC, MSC_SCAN, new_type, true, K_NO_WAIT);
#endif
    }
}

// 采样工作处理函数
static void sampling_work_handler(struct k_work *work) {
    struct module_detector_data *data = CONTAINER_OF(work, struct module_detector_data, sampling_work);
    const struct device *dev = data->dev;
    const struct module_detector_config *config = dev->config;
    
    if (!data->ready) {
        return;
    }
    
    int err = adc_read(config->adc_channel.dev, &data->as);
    if (err < 0) {
        LOG_ERR("Failed to read ADC: %d", err);
        return;
    }
    
    int32_t raw = data->as_buff[0];
    int32_t mv = raw;
    adc_raw_to_millivolts(adc_ref_internal(config->adc_channel.dev), 
                          ADC_GAIN_1_6, data->as.resolution, &mv);
    
    data->last_mv = mv;
    
    enum module_type module_type = detect_module_type(config, mv);
    
    // 只有当模块类型变化时才进行去抖动处理
    if (module_type != data->current_module_type) {
        data->current_module_type = module_type;
        data->last_change_time = k_uptime_get();
        
        // 取消之前的去抖动工作
        k_work_cancel_delayable(&data->debounce_work);
        // 启动去抖动工作
        k_work_schedule(&data->debounce_work, K_MSEC(config->detection_delay_ms));
        
        LOG_DBG("Detected potential module change to %s, debouncing...", 
                module_type_to_str(module_type));
    }
}

// 采样定时器回调
static void sampling_timer_handler(struct k_timer *timer) {
    struct module_detector_data *data = CONTAINER_OF(timer, struct module_detector_data, sampling_timer);
    k_work_submit(&data->sampling_work);
}

// 异步初始化工作
static void module_detector_async_init(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct module_detector_data *data = CONTAINER_OF(work_delayable, 
                                                  struct module_detector_data, init_work);
    const struct device *dev = data->dev;
    const struct module_detector_config *config = dev->config;
    
    const struct device* adc = config->adc_channel.dev;
    uint8_t channel_id = config->adc_channel.channel_id;
    
    if (!device_is_ready(adc)) {
        LOG_ERR("ADC device not ready");
        return;
    }
    
    struct adc_channel_cfg channel_cfg = {
        .gain = ADC_GAIN_1_6,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id = channel_id,
        #ifdef CONFIG_ADC_CONFIGURABLE_INPUTS
            #ifdef CONFIG_ADC_NRFX_SAADC
                .input_positive = SAADC_CH_PSELP_PSELP_AnalogInput0 + channel_id,
            #else
                .input_positive = channel_id,
            #endif
        #endif
    };
    
    int err = adc_channel_setup(adc, &channel_cfg);
    if (err < 0) {
        LOG_ERR("Failed to setup ADC channel: %d", err);
        return;
    }
    
    data->as_buff = k_malloc(sizeof(uint16_t));
    if (!data->as_buff) {
        LOG_ERR("Failed to allocate buffer for ADC");
        return;
    }
    
    data->as = (struct adc_sequence){
        .channels = BIT(channel_id),
        .buffer = data->as_buff,
        .buffer_size = sizeof(uint16_t),
        .resolution = 12,
    };
    
    data->ready = true;
    data->current_module_type = MODULE_TYPE_NONE;
    data->last_reported_type = MODULE_TYPE_NONE;
    data->last_mv = 0;
    data->callback_count = 0;
    
    // 初始化工作和定时器
    k_work_init(&data->sampling_work, sampling_work_handler);
    k_work_init_delayable(&data->debounce_work, debounce_work_handler);
    k_timer_init(&data->sampling_timer, sampling_timer_handler, NULL);
    
    // 启动定时器，按照配置的频率采样
    uint32_t period_ms = 1000 / config->sampling_hz;
    k_timer_start(&data->sampling_timer, K_MSEC(period_ms), K_MSEC(period_ms));
    data->enabled = true;
    
    LOG_INF("Module detector initialized, sampling at %d Hz", config->sampling_hz);
}

// 设备初始化
static int module_detector_init(const struct device *dev) {
    struct module_detector_data *data = dev->data;
    
    data->dev = dev;
    k_work_init_delayable(&data->init_work, module_detector_async_init);
    k_work_schedule(&data->init_work, K_MSEC(100));
    
    LOG_INF("Module detector driver initialized");
    return 0;
}

// 输入设备API
static const struct input_device_api module_detector_api = {
    // 空实现，我们只使用input_report上报事件
};

// 获取当前检测到的模块类型
enum module_type module_detector_get_type(const struct device *dev) {
    struct module_detector_data *data = dev->data;
    return data->current_module_type;
}

// 注册模块类型变化回调函数
int module_detector_register_callback(const struct device *dev, 
                                      module_type_changed_callback_t callback,
                                      void *user_data) {
    if (!dev || !callback) {
        return -EINVAL;
    }
    
    struct module_detector_data *data = dev->data;
    
    if (data->callback_count >= MAX_CALLBACKS) {
        return -ENOMEM;
    }
    
    // 检查是否已注册
    for (int i = 0; i < data->callback_count; i++) {
        if (data->callbacks[i].callback == callback) {
            return -EALREADY;
        }
    }
    
    // 添加回调
    data->callbacks[data->callback_count].callback = callback;
    data->callbacks[data->callback_count].user_data = user_data;
    data->callback_count++;
    
    // 如果已经有模块类型，立即触发回调
    if (data->current_module_type != MODULE_TYPE_NONE) {
        callback(data->current_module_type, user_data);
    }
    
    return 0;
}

// 取消注册模块类型变化回调函数
int module_detector_unregister_callback(const struct device *dev, 
                                        module_type_changed_callback_t callback) {
    if (!dev || !callback) {
        return -EINVAL;
    }
    
    struct module_detector_data *data = dev->data;
    
    // 查找并移除回调
    for (int i = 0; i < data->callback_count; i++) {
        if (data->callbacks[i].callback == callback) {
            // 移动后面的回调前移
            for (int j = i; j < data->callback_count - 1; j++) {
                data->callbacks[j] = data->callbacks[j + 1];
            }
            data->callback_count--;
            return 0;
        }
    }
    
    return -ENOENT;
}

// 设备定义宏
#define MODULE_DETECTOR_DEFINE(n)                                                                   \
    static struct module_detector_data module_detector_data_##n = {                                 \
    };                                                                                              \
                                                                                                    \
    static const struct module_detector_config module_detector_config_##n = {                        \
        .sampling_hz = DT_PROP(DT_DRV_INST(n), sampling_hz),                                        \
        .adc_channel = ADC_DT_SPEC_GET_BY_IDX(DT_DRV_INST(n), io_channels, 0),                      \
        .detection_delay_ms = DT_PROP_OR(DT_DRV_INST(n), detection_delay_ms, 50),                   \
        .voltage_ranges = {                                                                         \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 500),  /* MODULE_TYPE_KNOB min */           \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 1000), /* MODULE_TYPE_KNOB max */           \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 1000), /* MODULE_TYPE_JOYSTICK min */       \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 1500), /* MODULE_TYPE_JOYSTICK max */       \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 1500), /* MODULE_TYPE_TRACKBALL min */      \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 2000), /* MODULE_TYPE_TRACKBALL max */      \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 2000), /* MODULE_TYPE_SLIDER min */         \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 2500), /* MODULE_TYPE_SLIDER max */         \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 2500), /* MODULE_TYPE_OTHER min */          \
            DT_PROP_OR(DT_DRV_INST(n), voltage_ranges, 3000), /* MODULE_TYPE_OTHER max */          \
        },                                                                                          \
    };                                                                                              \
                                                                                                    \
    DEVICE_DT_INST_DEFINE(n, module_detector_init, NULL, &module_detector_data_##n,                 \
                          &module_detector_config_##n, POST_KERNEL,                                 \
                          CONFIG_MODULE_DETECTOR_INIT_PRIORITY, &module_detector_api);

DT_INST_FOREACH_STATUS_OKAY(MODULE_DETECTOR_DEFINE)
