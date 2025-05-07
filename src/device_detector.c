/*
 * Copyright (c) 2023 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_device_detector

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zmk/keymap.h>
#include <zmk/drivers/device_detector.h>

// Shell命令支持
#if defined(CONFIG_DEVICE_DETECTOR_SHELL)
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#endif

LOG_MODULE_REGISTER(device_detector, CONFIG_DEVICE_DETECTOR_LOG_LEVEL);

struct device_detector_callback {
    device_type_changed_callback_t callback;
    void *user_data;
};

#define MAX_CALLBACKS 5
#define MAX_DEVICE_TYPES 5
#define ADC_RESOLUTION 12
#define ADC_OVERSAMPLING 4
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40)

// 定义稳定性检测所需的样本数量和一致性阈值
#define STABILITY_SAMPLE_COUNT 3
#define STABILITY_DEFAULT_THRESHOLD 2

// 自适应稳定性相关参数
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
#define ADAPTIVE_STABILITY_WINDOW CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY_WINDOW
#define ADAPTIVE_STABILITY_MIN_QUALITY CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY_MIN_QUALITY
#define INITIAL_ADAPTIVE_THRESHOLD 2
#define MAX_ADAPTIVE_THRESHOLD 5
#define MIN_ADAPTIVE_THRESHOLD 1
#define SIGNAL_QUALITY_EXCELLENT 90
#define SIGNAL_QUALITY_GOOD 70
#define SIGNAL_QUALITY_FAIR 50
#define SIGNAL_QUALITY_POOR 30
#define VOLTAGE_VARIANCE_THRESHOLD 500  // 电压波动阈值（毫伏平方）
#else
#define ADAPTIVE_STABILITY_WINDOW 1
#endif

struct device_detector_data {
    const struct device *dev;
    struct adc_sequence as;
    uint16_t *as_buff;
    struct k_work_delayable init_work;
    struct k_work sampling_work;
    struct k_timer sampling_timer;
    enum device_type current_device_type;
    enum device_type last_reported_type;
    int32_t last_mv;
    int32_t min_mv;
    int32_t max_mv;
    int32_t avg_mv;
    bool ready;
    bool enabled;
    bool adc_initialized;
    uint32_t total_samples;
    uint32_t type_changes;
    uint32_t adc_errors;
    uint32_t unstable_readings;
    uint32_t consecutive_matches;
    
    // 设备类型历史记录
    struct device_type_history history;
    
    // 设备类型计数
    uint32_t type_counts[6];

    // 回调相关
    struct device_detector_callback callbacks[MAX_CALLBACKS];
    uint8_t callback_count;
    
    // 去抖动相关
    struct k_work_delayable debounce_work;
    enum device_type debounce_type;
    int64_t last_change_time;
    
    // 稳定性检测
    enum device_type stability_history[STABILITY_SAMPLE_COUNT];
    uint8_t stability_index;
    
    // 自适应稳定性相关
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
    bool adaptive_stability_enabled;
    struct stability_stats stability;
    int32_t voltage_history[ADAPTIVE_STABILITY_WINDOW];
    uint8_t voltage_history_index;
    uint32_t voltage_sum;
    uint32_t voltage_sum_squares;
    uint32_t rejected_changes;
#endif
};

struct device_detector_config {
    uint32_t sampling_hz;
    struct adc_dt_spec adc_channel;
    uint32_t detection_delay_ms;
    uint32_t voltage_ranges[MAX_DEVICE_TYPES * 2]; // 最多支持5种设备类型，每种类型需要min和max
};

// 设备类型字符串描述
static const char *device_type_strings[] = {
    "None",
    "Knob",
    "Joystick",
    "Trackball",
    "Slider",
    "Other"
};

// 获取设备类型字符串描述
const char *device_type_to_str(enum device_type type) {
    if (type >= 0 && type < ARRAY_SIZE(device_type_strings)) {
        return device_type_strings[type];
    }
    return "Unknown";
}

// 根据电压值检测设备类型
static enum device_type detect_device_type(const struct device_detector_config *config, int32_t mv) {
    // 电压范围定义： [device1_min, device1_max, device2_min, device2_max, ...]
    // 检查各个设备的电压范围
    if (mv < config->voltage_ranges[0]) {
        return DEVICE_TYPE_NONE;
    }
    
    for (int i = 0; i < MAX_DEVICE_TYPES; i++) { // 最多5种设备类型
        uint32_t min_mv = config->voltage_ranges[i*2];
        uint32_t max_mv = config->voltage_ranges[i*2+1];
        
        // 确保最小值和最大值都有效，且最大值大于最小值
        if (min_mv == 0 && max_mv == 0) {
            break; // 没有更多定义的设备类型
        }
        
        if (min_mv >= max_mv) {
            LOG_WRN("Invalid voltage range for device type %d: min=%u, max=%u", 
                    i+1, min_mv, max_mv);
            continue;
        }
        
        if (mv >= min_mv && mv < max_mv) {
            return i + 1; // 设备类型从1开始
        }
    }
    
    return DEVICE_TYPE_OTHER;
}

#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
// 计算电压方差和信号质量
static void update_adaptive_stability_metrics(struct device_detector_data *data, int32_t mv) {
    // 更新电压历史记录
    int32_t old_mv = data->voltage_history[data->voltage_history_index];
    data->voltage_history[data->voltage_history_index] = mv;
    data->voltage_history_index = (data->voltage_history_index + 1) % ADAPTIVE_STABILITY_WINDOW;
    
    // 更新电压总和和平方和，用于计算方差
    if (data->total_samples >= ADAPTIVE_STABILITY_WINDOW) {
        data->voltage_sum -= old_mv;
        data->voltage_sum_squares -= (uint32_t)(old_mv * old_mv);
    }
    data->voltage_sum += mv;
    data->voltage_sum_squares += (uint32_t)(mv * mv);
    
    // 如果采样数量足够，计算方差和信号质量
    if (data->total_samples >= ADAPTIVE_STABILITY_WINDOW) {
        uint32_t n = ADAPTIVE_STABILITY_WINDOW;
        uint32_t mean = data->voltage_sum / n;
        uint32_t mean_squares = data->voltage_sum_squares / n;
        uint32_t variance = (mean_squares > (mean * mean)) ? 
                          (mean_squares - (mean * mean)) : 0;
        
        // 更新稳定性统计
        data->stability.voltage_variance = variance;
        
        // 计算信号质量 (0-100)
        // 方差越大，信号质量越低
        uint32_t quality = 100;
        if (variance > VOLTAGE_VARIANCE_THRESHOLD) {
            quality = 100 - MIN(100, (variance - VOLTAGE_VARIANCE_THRESHOLD) / 100);
        }
        data->stability.signal_quality = quality;
        
        // 估计噪声水平
        data->stability.noise_level = (uint32_t)sqrtf(variance);
        
        // 根据信号质量动态调整稳定性阈值
        if (quality >= SIGNAL_QUALITY_EXCELLENT) {
            data->stability.adaptive_threshold = MIN_ADAPTIVE_THRESHOLD;
        } else if (quality >= SIGNAL_QUALITY_GOOD) {
            data->stability.adaptive_threshold = MIN_ADAPTIVE_THRESHOLD + 1;
        } else if (quality >= SIGNAL_QUALITY_FAIR) {
            data->stability.adaptive_threshold = MIN_ADAPTIVE_THRESHOLD + 2;
        } else if (quality >= SIGNAL_QUALITY_POOR) {
            data->stability.adaptive_threshold = MIN_ADAPTIVE_THRESHOLD + 3;
        } else {
            data->stability.adaptive_threshold = MAX_ADAPTIVE_THRESHOLD;
        }
        
        LOG_DBG("Signal quality: %u%%, adaptive threshold: %u, variance: %u", 
                quality, data->stability.adaptive_threshold, variance);
    }
}

// 根据信号质量判断是否需要增加稳定性要求
static uint8_t get_effective_stability_threshold(struct device_detector_data *data) {
    if (!data->adaptive_stability_enabled) {
        return DT_INST_PROP_OR(0, stability_threshold, STABILITY_DEFAULT_THRESHOLD);
    }
    
    return data->stability.adaptive_threshold;
}
#endif

// 修改稳定性检测函数，增加自适应阈值支持
static bool is_device_type_stable(struct device_detector_data *data, enum device_type type) {
    const struct device *dev = data->dev;
    const struct device_detector_config *config = dev->config;
    
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
    uint8_t stability_threshold = get_effective_stability_threshold(data);
#else
    uint8_t stability_threshold = DT_INST_PROP_OR(0, stability_threshold, STABILITY_DEFAULT_THRESHOLD);
#endif
    
    // 将新检测到的类型保存到历史记录
    data->stability_history[data->stability_index] = type;
    data->stability_index = (data->stability_index + 1) % STABILITY_SAMPLE_COUNT;
    
    // 检查最近几次的类型是否一致
    bool all_match = true;
    enum device_type first_type = data->stability_history[0];
    
    for (int i = 1; i < STABILITY_SAMPLE_COUNT; i++) {
        if (data->stability_history[i] != first_type) {
            all_match = false;
            data->unstable_readings++; // 记录不稳定读数
            break;
        }
    }
    
    // 如果所有样本匹配，增加连续匹配计数
    if (all_match) {
        data->consecutive_matches++;
    } else {
        data->consecutive_matches = 0;
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
        if (data->adaptive_stability_enabled && 
            data->stability.signal_quality < ADAPTIVE_STABILITY_MIN_QUALITY) {
            data->stability.rejected_changes++;
            data->rejected_changes++;
            LOG_DBG("Rejected device type change due to poor signal quality: %u%%", 
                   data->stability.signal_quality);
        }
#endif
    }
    
    // 当连续匹配次数达到阈值，认为类型稳定
    return data->consecutive_matches >= stability_threshold;
}

// 触发所有回调函数
static void trigger_callbacks(struct device_detector_data *data, enum device_type type) {
    for (int i = 0; i < data->callback_count; i++) {
        if (data->callbacks[i].callback) {
            data->callbacks[i].callback(type, data->callbacks[i].user_data);
        }
    }
}

// 当检测到新的设备类型时，更新历史记录
static void update_device_type_history(struct device_detector_data *data, enum device_type type) {
    // 更新历史记录
    data->history.types[data->history.index] = type;
    data->history.timestamps[data->history.index] = k_uptime_get();
    data->history.index = (data->history.index + 1) % 5;  // 循环使用5个历史记录槽
    
    // 更新设备类型计数
    if (type >= 0 && type < 6) {  // 确保索引有效
        data->type_counts[type]++;
    }
}

// 去抖动完成，确认设备类型变化
static void debounce_work_handler(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct device_detector_data *data = CONTAINER_OF(work_delayable, 
                                                    struct device_detector_data, debounce_work);
    
    // 确认设备类型变化
    if (data->current_device_type != data->last_reported_type) {
        enum device_type new_type = data->current_device_type;
        enum device_type old_type = data->last_reported_type;
        data->last_reported_type = new_type;
        data->type_changes++;
        
        // 更新设备历史记录
        update_device_type_history(data, new_type);
        
        LOG_INF("Device type changed to %s (voltage: %d mV)", 
                device_type_to_str(new_type), data->last_mv);
        
        // 触发回调
        trigger_callbacks(data, new_type);
        
        // 如果配置了自动层切换，则切换到对应层
#if defined(CONFIG_DEVICE_DETECTOR_CALLBACK_LAYER) && defined(CONFIG_ZMK_KEYMAP)
        int new_layer = -1;
        // 存储所有可能的设备层
        int all_device_layers[] = {
            CONFIG_DEVICE_DETECTOR_LAYER_KNOB,
            CONFIG_DEVICE_DETECTOR_LAYER_JOYSTICK,
            CONFIG_DEVICE_DETECTOR_LAYER_TRACKBALL,
            CONFIG_DEVICE_DETECTOR_LAYER_SLIDER,
            CONFIG_DEVICE_DETECTOR_LAYER_OTHER
        };
        
        // 获取新设备类型的层
        switch (new_type) {
            case DEVICE_TYPE_KNOB:
                new_layer = CONFIG_DEVICE_DETECTOR_LAYER_KNOB;
                break;
            case DEVICE_TYPE_JOYSTICK:
                new_layer = CONFIG_DEVICE_DETECTOR_LAYER_JOYSTICK;
                break;
            case DEVICE_TYPE_TRACKBALL:
                new_layer = CONFIG_DEVICE_DETECTOR_LAYER_TRACKBALL;
                break;
            case DEVICE_TYPE_SLIDER:
                new_layer = CONFIG_DEVICE_DETECTOR_LAYER_SLIDER;
                break;
            case DEVICE_TYPE_OTHER:
                new_layer = CONFIG_DEVICE_DETECTOR_LAYER_OTHER;
                break;
            default:
                // 无设备或未知类型，不切换层
                new_layer = -1;
                break;
        }
        
        // 优化的层切换策略:
        // 1. 首先停用所有设备相关的层（确保互斥）
        // 2. 然后激活新设备对应的层
       
        // 停用所有设备相关的层
        for (int i = 0; i < sizeof(all_device_layers) / sizeof(all_device_layers[0]); i++) {
            int layer = all_device_layers[i];
            if (layer >= 0 && zmk_keymap_layer_active(layer)) {
                // 如果这不是我们要激活的新层，则停用它
                if (layer != new_layer || new_type == DEVICE_TYPE_NONE) {
                    LOG_DBG("Deactivating layer %d for device type cleanup", layer);
                    zmk_keymap_layer_deactivate(layer);
                   
                    // 添加短暂延迟，确保层状态更新
                    k_sleep(K_MSEC(10));
                }
            }
        }
        
        // 激活新设备对应的层
        if (new_layer >= 0 && new_type != DEVICE_TYPE_NONE) {
            // 检查层是否已经激活
            if (!zmk_keymap_layer_active(new_layer)) {
                LOG_INF("Activating layer %d for device type %s", new_layer, device_type_to_str(new_type));
                zmk_keymap_layer_activate(new_layer);
            } else {
                LOG_DBG("Layer %d already active for device type %s", new_layer, device_type_to_str(new_type));
            }
        }
#endif

        // 如果配置了事件回调，发送事件
#if defined(CONFIG_DEVICE_DETECTOR_CALLBACK_EVENT)
        // 发送设备类型变化事件
        input_report_abs(data->dev, ABS_MISC, (int32_t)new_type, false, K_NO_WAIT);
        
        // 发送电压值事件
        input_report_abs(data->dev, ABS_X, data->last_mv, false, K_NO_WAIT);
        
        // 同步事件
        input_sync(data->dev, K_NO_WAIT);
#endif
    }
}

// 读取ADC数据并转换为毫伏
static int read_adc_mv(struct device_detector_data *data, int32_t *mv_out) {
    const struct device *dev = data->dev;
    const struct device_detector_config *config = dev->config;
    
    if (!data->ready || !data->adc_initialized) {
        return -ENODEV;
    }
    
    // 读取ADC原始值
    int err = adc_read(config->adc_channel.dev, &data->as);
    if (err < 0) {
        data->adc_errors++;
        LOG_ERR("Failed to read ADC: %d", err);
        return err;
    }
    
    // 转换为毫伏
    int32_t raw = data->as_buff[0];
    int32_t mv = raw;
    err = adc_raw_to_millivolts(adc_ref_internal(config->adc_channel.dev), 
                               ADC_GAIN_1_6, data->as.resolution, &mv);
    if (err < 0) {
        data->adc_errors++;
        LOG_ERR("Failed to convert ADC raw value to millivolts: %d", err);
        return err;
    }
    
    // 更新诊断信息
    data->total_samples++;
    if (mv < data->min_mv || data->min_mv == 0) {
        data->min_mv = mv;
    }
    if (mv > data->max_mv) {
        data->max_mv = mv;
    }
    
    // 输出转换结果
    *mv_out = mv;
    
    return 0;
}

#ifdef CONFIG_DEVICE_DETECTOR_VOLTAGE_LEARNING

// 用于自动学习的电压范围结构
struct voltage_range_learning {
    bool active;                    // 是否处于学习模式
    uint8_t current_learning_type;  // 当前正在学习的设备类型
    int32_t min_mv;                 // 学习到的最小电压
    int32_t max_mv;                 // 学习到的最大电压
    uint32_t sample_count;          // 学习样本数量
    bool learning_completed;        // 是否已完成学习
};

// 全局学习状态
static struct voltage_range_learning g_learning = {
    .active = false,
    .current_learning_type = 0,
    .min_mv = INT32_MAX,
    .max_mv = INT32_MIN,
    .sample_count = 0,
    .learning_completed = false
};

/**
 * @brief 启动电压范围学习模式
 * 
 * @param dev 设备检测器设备
 * @param device_type 需要学习的设备类型 (1-5)
 * @return int 0表示成功，负值表示错误
 */
int device_detector_start_learning(const struct device *dev, uint8_t device_type) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    if (device_type < 1 || device_type > 5) {
        return -EINVAL;
    }
    
    if (g_learning.active) {
        return -EBUSY;  // 已经在学习模式中
    }
    
    // 初始化学习状态
    g_learning.active = true;
    g_learning.current_learning_type = device_type;
    g_learning.min_mv = INT32_MAX;
    g_learning.max_mv = INT32_MIN;
    g_learning.sample_count = 0;
    g_learning.learning_completed = false;
    
    LOG_INF("Started voltage range learning for device type %s", 
           device_type_to_str(device_type));
    
    return 0;
}

/**
 * @brief 停止电压范围学习模式并保存结果
 * 
 * @param dev 设备检测器设备
 * @return int 0表示成功，负值表示错误
 */
int device_detector_finish_learning(const struct device *dev) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    if (!g_learning.active) {
        return -EINVAL;  // 没有处于学习模式
    }
    
    struct device_detector_data *data = dev->data;
    struct device_detector_config *config = (struct device_detector_config *)dev->config;
    
    // 确保有足够的样本
    if (g_learning.sample_count < 10) {
        g_learning.active = false;
        LOG_WRN("Too few samples for learning (%u), learning aborted", 
               g_learning.sample_count);
        return -EINVAL;
    }
    
    // 计算范围，添加10%的余量
    int32_t range = g_learning.max_mv - g_learning.min_mv;
    int32_t margin = range / 10;
    int32_t min_mv = MAX(0, g_learning.min_mv - margin);
    int32_t max_mv = g_learning.max_mv + margin;
    
    // 保存学习到的电压范围
    uint8_t idx = g_learning.current_learning_type - 1;
    config->voltage_ranges[idx*2] = min_mv;
    config->voltage_ranges[idx*2+1] = max_mv;
    
    LOG_INF("Completed voltage range learning for device type %s: [%d, %d] mV", 
           device_type_to_str(g_learning.current_learning_type), min_mv, max_mv);
    
    // 重置学习状态
    g_learning.active = false;
    g_learning.learning_completed = true;
    
    return 0;
}

/**
 * @brief 获取学习模式的电压范围状态
 * 
 * @param dev 设备检测器设备
 * @param min_mv 学习到的最小电压
 * @param max_mv 学习到的最大电压
 * @param samples 收集的样本数量
 * @return int 0表示成功，负值表示错误
 */
int device_detector_get_learning_status(const struct device *dev, 
                                      int32_t *min_mv, int32_t *max_mv, 
                                      uint32_t *samples) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    *min_mv = g_learning.min_mv;
    *max_mv = g_learning.max_mv;
    *samples = g_learning.sample_count;
    
    return 0;
}

// 在采样过程中更新学习数据
static void update_learning_data(int32_t mv) {
    if (!g_learning.active) {
        return;
    }
    
    // 更新电压范围
    if (mv < g_learning.min_mv) {
        g_learning.min_mv = mv;
    }
    if (mv > g_learning.max_mv) {
        g_learning.max_mv = mv;
    }
    
    // 增加样本计数
    g_learning.sample_count++;
    
    if (g_learning.sample_count % 50 == 0) {
        LOG_DBG("Learning progress: %u samples, range [%d, %d] mV", 
               g_learning.sample_count, g_learning.min_mv, g_learning.max_mv);
    }
}

#else
// 如果未启用电压学习功能，提供空的实现
int device_detector_start_learning(const struct device *dev, uint8_t device_type) {
    return -ENOTSUP;
}

int device_detector_finish_learning(const struct device *dev) {
    return -ENOTSUP;
}

int device_detector_get_learning_status(const struct device *dev, 
                                      int32_t *min_mv, int32_t *max_mv, 
                                      uint32_t *samples) {
    return -ENOTSUP;
}

static inline void update_learning_data(int32_t mv) {
    // 不支持学习功能，什么也不做
}
#endif

// 修改采样工作处理函数，添加电压范围学习支持
static void sampling_work_handler(struct k_work *work) {
    struct device_detector_data *data = CONTAINER_OF(work, struct device_detector_data, sampling_work);
    const struct device *dev = data->dev;
    const struct device_detector_config *config = dev->config;
    
    if (!data->ready || !data->enabled) {
        return;
    }
    
    // 读取电压值
    int32_t mv;
    int err = read_adc_mv(data, &mv);
    if (err < 0) {
        return;
    }
    
    // 更新电压值
    data->last_mv = mv;
    
    // 更新电压统计数据
    if (mv < data->min_mv || data->min_mv == INT32_MAX) {
        data->min_mv = mv;
    }
    if (mv > data->max_mv) {
        data->max_mv = mv;
    }
    
    // 更新平均电压值（使用简单移动平均算法）
    if (data->avg_mv == 0) {
        data->avg_mv = mv;
    } else {
        // 使用指数移动平均，赋予新值权重0.1
        data->avg_mv = (data->avg_mv * 9 + mv) / 10;
    }
    
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
    // 更新自适应稳定性指标
    if (data->adaptive_stability_enabled) {
        update_adaptive_stability_metrics(data, mv);
    }
#endif

#ifdef CONFIG_DEVICE_DETECTOR_VOLTAGE_LEARNING
    // 如果处于学习模式，更新学习数据
    update_learning_data(mv);
#endif
    
    // 检测设备类型
    enum device_type device_type = detect_device_type(config, mv);
    
    // 检查设备类型的稳定性
    bool is_stable = is_device_type_stable(data, device_type);
    
    // 如果设备类型稳定且与当前类型不同，启动去抖动
    if (is_stable && device_type != data->current_device_type) {
        data->current_device_type = device_type;
        
        // 记录变化时间
        data->last_change_time = k_uptime_get();
        
        // 用配置的延迟启动去抖动工作
        k_work_schedule(&data->debounce_work, 
                      K_MSEC(CONFIG_DEVICE_DETECTOR_DETECTION_DELAY_MS));
        
        LOG_DBG("Device type potentially changed to %s (stable), starting debounce", 
               device_type_to_str(device_type));
    }
}
#endif

#ifdef CONFIG_DEVICE_DETECTOR_VOLTAGE_LEARNING

// 用于自动学习的电压范围结构
struct voltage_range_learning {
    bool active;                    // 是否处于学习模式
    uint8_t current_learning_type;  // 当前正在学习的设备类型
    int32_t min_mv;                 // 学习到的最小电压
    int32_t max_mv;                 // 学习到的最大电压
    uint32_t sample_count;          // 学习样本数量
    bool learning_completed;        // 是否已完成学习
};

// 全局学习状态
static struct voltage_range_learning g_learning = {
    .active = false,
    .current_learning_type = 0,
    .min_mv = INT32_MAX,
    .max_mv = INT32_MIN,
    .sample_count = 0,
    .learning_completed = false
};

/**
 * @brief 启动电压范围学习模式
 * 
 * @param dev 设备检测器设备
 * @param device_type 需要学习的设备类型 (1-5)
 * @return int 0表示成功，负值表示错误
 */
int device_detector_start_learning(const struct device *dev, uint8_t device_type) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    if (device_type < 1 || device_type > 5) {
        return -EINVAL;
    }
    
    if (g_learning.active) {
        return -EBUSY;  // 已经在学习模式中
    }
    
    // 初始化学习状态
    g_learning.active = true;
    g_learning.current_learning_type = device_type;
    g_learning.min_mv = INT32_MAX;
    g_learning.max_mv = INT32_MIN;
    g_learning.sample_count = 0;
    g_learning.learning_completed = false;
    
    LOG_INF("Started voltage range learning for device type %s", 
           device_type_to_str(device_type));
    
    return 0;
}

/**
 * @brief 停止电压范围学习模式并保存结果
 * 
 * @param dev 设备检测器设备
 * @return int 0表示成功，负值表示错误
 */
int device_detector_finish_learning(const struct device *dev) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    if (!g_learning.active) {
        return -EINVAL;  // 没有处于学习模式
    }
    
    struct device_detector_data *data = dev->data;
    struct device_detector_config *config = (struct device_detector_config *)dev->config;
    
    // 确保有足够的样本
    if (g_learning.sample_count < 10) {
        g_learning.active = false;
        LOG_WRN("Too few samples for learning (%u), learning aborted", 
               g_learning.sample_count);
        return -EINVAL;
    }
    
    // 计算范围，添加10%的余量
    int32_t range = g_learning.max_mv - g_learning.min_mv;
    int32_t margin = range / 10;
    int32_t min_mv = MAX(0, g_learning.min_mv - margin);
    int32_t max_mv = g_learning.max_mv + margin;
    
    // 保存学习到的电压范围
    uint8_t idx = g_learning.current_learning_type - 1;
    config->voltage_ranges[idx*2] = min_mv;
    config->voltage_ranges[idx*2+1] = max_mv;
    
    LOG_INF("Completed voltage range learning for device type %s: [%d, %d] mV", 
           device_type_to_str(g_learning.current_learning_type), min_mv, max_mv);
    
    // 重置学习状态
    g_learning.active = false;
    g_learning.learning_completed = true;
    
    return 0;
}

/**
 * @brief 获取学习模式的电压范围状态
 * 
 * @param dev 设备检测器设备
 * @param min_mv 学习到的最小电压
 * @param max_mv 学习到的最大电压
 * @param samples 收集的样本数量
 * @return int 0表示成功，负值表示错误
 */
int device_detector_get_learning_status(const struct device *dev, 
                                      int32_t *min_mv, int32_t *max_mv, 
                                      uint32_t *samples) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    *min_mv = g_learning.min_mv;
    *max_mv = g_learning.max_mv;
    *samples = g_learning.sample_count;
    
    return 0;
}

// 在采样过程中更新学习数据
static void update_learning_data(int32_t mv) {
    if (!g_learning.active) {
        return;
    }
    
    // 更新电压范围
    if (mv < g_learning.min_mv) {
        g_learning.min_mv = mv;
    }
    if (mv > g_learning.max_mv) {
        g_learning.max_mv = mv;
    }
    
    // 增加样本计数
    g_learning.sample_count++;
    
    if (g_learning.sample_count % 50 == 0) {
        LOG_DBG("Learning progress: %u samples, range [%d, %d] mV", 
               g_learning.sample_count, g_learning.min_mv, g_learning.max_mv);
    }
}

#else
// 如果未启用电压学习功能，提供空的实现
int device_detector_start_learning(const struct device *dev, uint8_t device_type) {
    return -ENOTSUP;
}

int device_detector_finish_learning(const struct device *dev) {
    return -ENOTSUP;
}

int device_detector_get_learning_status(const struct device *dev, 
                                      int32_t *min_mv, int32_t *max_mv, 
                                      uint32_t *samples) {
    return -ENOTSUP;
}

static inline void update_learning_data(int32_t mv) {
    // 不支持学习功能，什么也不做
}
#endif

// 修改采样工作处理函数，添加电压范围学习支持
static void sampling_work_handler(struct k_work *work) {
    struct device_detector_data *data = CONTAINER_OF(work, struct device_detector_data, sampling_work);
    const struct device *dev = data->dev;
    const struct device_detector_config *config = dev->config;
    
    if (!data->ready || !data->enabled) {
        return;
    }
    
    // 读取电压值
    int32_t mv;
    int err = read_adc_mv(data, &mv);
    if (err < 0) {
        return;
    }
    
    // 更新电压值
    data->last_mv = mv;
    
    // 更新电压统计数据
    if (mv < data->min_mv || data->min_mv == INT32_MAX) {
        data->min_mv = mv;
    }
    if (mv > data->max_mv) {
        data->max_mv = mv;
    }
    
    // 更新平均电压值（使用简单移动平均算法）
    if (data->avg_mv == 0) {
        data->avg_mv = mv;
    } else {
        // 使用指数移动平均，赋予新值权重0.1
        data->avg_mv = (data->avg_mv * 9 + mv) / 10;
    }
    
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
    // 更新自适应稳定性指标
    if (data->adaptive_stability_enabled) {
        update_adaptive_stability_metrics(data, mv);
    }
#endif

#ifdef CONFIG_DEVICE_DETECTOR_VOLTAGE_LEARNING
    // 如果处于学习模式，更新学习数据
    update_learning_data(mv);
#endif
    
    // 检测设备类型
    enum device_type device_type = detect_device_type(config, mv);
    
    // 检查设备类型的稳定性
    bool is_stable = is_device_type_stable(data, device_type);
    
    // 如果设备类型稳定且与当前类型不同，启动去抖动
    if (is_stable && device_type != data->current_device_type) {
        data->current_device_type = device_type;
        
        // 记录变化时间
        data->last_change_time = k_uptime_get();
        
        // 用配置的延迟启动去抖动工作
        k_work_schedule(&data->debounce_work, 
                      K_MSEC(CONFIG_DEVICE_DETECTOR_DETECTION_DELAY_MS));
        
        LOG_DBG("Device type potentially changed to %s (stable), starting debounce", 
               device_type_to_str(device_type));
    }
}

// 采样定时器回调
static void sampling_timer_handler(struct k_timer *timer) {
    struct device_detector_data *data = CONTAINER_OF(timer, struct device_detector_data, sampling_timer);
    
    // 只有在设备使能的情况下才提交工作
    if (data->enabled) {
        k_work_submit(&data->sampling_work);
    }
}

// 重置设备检测器稳定性历史
static void reset_stability_history(struct device_detector_data *data) {
    for (int i = 0; i < STABILITY_SAMPLE_COUNT; i++) {
        data->stability_history[i] = DEVICE_TYPE_NONE;
    }
    data->stability_index = 0;
    data->consecutive_matches = 0;
}

// 异步初始化ADC和设备检测器
static void device_detector_async_init(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct device_detector_data *data = CONTAINER_OF(work_delayable, 
                                                  struct device_detector_data, init_work);
    const struct device *dev = data->dev;
    const struct device_detector_config *config = dev->config;
    
    const struct device* adc = config->adc_channel.dev;
    uint8_t channel_id = config->adc_channel.channel_id;
    
    // 检查ADC设备是否就绪
    if (!device_is_ready(adc)) {
        LOG_ERR("ADC device %s not ready", adc->name);
        k_work_schedule(&data->init_work, K_MSEC(1000)); // 1秒后重试
        return;
    }
    
    // 配置ADC通道
    struct adc_channel_cfg channel_cfg = {
        .gain = ADC_GAIN_1_6,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQUISITION_TIME,
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
        LOG_ERR("Failed to setup ADC channel %d: %d", channel_id, err);
        k_work_schedule(&data->init_work, K_MSEC(1000)); // 1秒后重试
        return;
    }
    
    // 设置ADC序列
    data->as = (struct adc_sequence){
        .channels = BIT(channel_id),
        .buffer = data->as_buff,
        .buffer_size = sizeof(uint16_t),
        .resolution = ADC_RESOLUTION,
        .oversampling = ADC_OVERSAMPLING,
        .calibrate = true,  // 第一次读取时校准
    };
    
    data->adc_initialized = true;
    data->ready = true;
    
    // 启动采样定时器
    uint32_t sampling_period_ms = 1000 / config->sampling_hz;
    k_timer_start(&data->sampling_timer, K_MSEC(sampling_period_ms), K_MSEC(sampling_period_ms));
    
    LOG_INF("Device detector initialized, sampling at %d Hz on ADC channel %d", 
            config->sampling_hz, channel_id);
    
    // 立即进行一次采样以获取初始状态
    k_work_submit(&data->sampling_work);
}

static int device_detector_init(const struct device *dev) {
    struct device_detector_data *data = dev->data;
    const struct device_detector_config *config = dev->config;
    
    // 初始化设备检测器
    data->dev = dev;
    data->ready = false;
    data->adc_initialized = false;
    data->enabled = true;
    data->callback_count = 0;
    data->current_device_type = DEVICE_TYPE_NONE;
    data->last_reported_type = DEVICE_TYPE_NONE;
    data->last_mv = 0;
    data->min_mv = INT32_MAX;
    data->max_mv = INT32_MIN;
    data->avg_mv = 0;
    data->stability_index = 0;
    data->consecutive_matches = 0;
    
    // 初始化诊断数据
    data->total_samples = 0;
    data->type_changes = 0;
    data->adc_errors = 0;
    data->unstable_readings = 0;
    
    // 初始化设备类型计数
    for (int i = 0; i < 6; i++) {
        data->type_counts[i] = 0;
    }
    
    // 初始化历史记录
    for (int i = 0; i < 5; i++) {
        data->history.types[i] = DEVICE_TYPE_NONE;
        data->history.timestamps[i] = 0;
    }
    data->history.index = 0;
    
    // 初始化稳定性历史数组
    reset_stability_history(data);
    
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
    // 初始化自适应稳定性相关数据
    data->adaptive_stability_enabled = true;
    data->voltage_history_index = 0;
    data->voltage_sum = 0;
    data->voltage_sum_squares = 0;
    data->rejected_changes = 0;
    data->stability.voltage_variance = 0;
    data->stability.adaptive_threshold = INITIAL_ADAPTIVE_THRESHOLD;
    data->stability.noise_level = 0;
    data->stability.signal_quality = 100;
    data->stability.false_triggers = 0;
    data->stability.rejected_changes = 0;
    
    // 初始化电压历史数组
    for (int i = 0; i < ADAPTIVE_STABILITY_WINDOW; i++) {
        data->voltage_history[i] = 0;
    }
#endif
    
    // 初始化延迟工作队列
    k_work_init_delayable(&data->init_work, device_detector_async_init);
    k_work_init(&data->sampling_work, sampling_work_handler);
    k_work_init_delayable(&data->debounce_work, debounce_work_handler);
    
    // 初始化采样定时器
    k_timer_init(&data->sampling_timer, sampling_timer_handler, NULL);
    
#if defined(CONFIG_DEVICE_DETECTOR_CALLBACK_EVENT)
    // 注册输入事件处理
    input_register_abs(dev, ABS_MISC, 0, DEVICE_TYPE_OTHER, 0);  // 设备类型事件
    input_register_abs(dev, ABS_X, 0, 3300, 0);                 // 电压值事件 (0-3300mV)
#endif
    
    // 分配ADC缓冲区
    data->as_buff = k_malloc(sizeof(uint16_t));
    if (data->as_buff == NULL) {
        LOG_ERR("Failed to allocate ADC buffer");
        return -ENOMEM;
    }
    
    // 启动异步初始化
    k_work_schedule(&data->init_work, K_NO_WAIT);
    
    return 0;
}

// 电源管理回调
#ifdef CONFIG_PM_DEVICE
static int device_detector_pm_action(const struct device *dev,
                                    enum pm_device_action action)
{
    int ret = 0;
    struct device_detector_data *data = dev->data;
    
    switch (action) {
        case PM_DEVICE_ACTION_RESUME:
            LOG_DBG("Resuming device detector");
            if (data->enabled) {
                uint32_t sampling_period_ms = 1000 / ((struct device_detector_config *)dev->config)->sampling_hz;
                k_timer_start(&data->sampling_timer, K_MSEC(sampling_period_ms), K_MSEC(sampling_period_ms));
                data->ready = true;
            }
            break;
        case PM_DEVICE_ACTION_SUSPEND:
            LOG_DBG("Suspending device detector");
            k_timer_stop(&data->sampling_timer);
            k_work_cancel(&data->sampling_work);
            k_work_cancel_delayable(&data->debounce_work);
            data->ready = false;
            break;
        case PM_DEVICE_ACTION_TURN_OFF:
            LOG_DBG("Turning off device detector");
            k_timer_stop(&data->sampling_timer);
            k_work_cancel(&data->sampling_work);
            k_work_cancel_delayable(&data->debounce_work);
            k_work_cancel_delayable(&data->init_work);
            data->ready = false;
            data->adc_initialized = false;
            break;
        default:
            ret = -ENOTSUP;
    }
    
    return ret;
}
#endif

#if defined(CONFIG_DEVICE_DETECTOR_CALLBACK_EVENT)
static const struct input_device_api device_detector_api = {
    .input_name = "ZMK Device Detector",
};
#endif

// 获取当前检测到的设备类型
enum device_type device_detector_get_type(const struct device *dev) {
    struct device_detector_data *data = dev->data;
    return data->current_device_type;
}

// 获取当前电压值
int device_detector_get_voltage(const struct device *dev, int32_t *mv) {
    struct device_detector_data *data = dev->data;
    
    if (!data->ready) {
        return -ENODEV;
    }
    
    if (mv != NULL) {
        *mv = data->last_mv;
    }
    
    return 0;
}

/**
 * @brief 获取设备检测器诊断信息
 * 
 * @param dev 设备检测器设备
 * @param diag 诊断信息结构指针
 * @return int 0表示成功，负值表示错误
 */
int device_detector_get_diagnostics(const struct device *dev, 
                               struct device_detector_diagnostics *diag) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    struct device_detector_data *data = dev->data;
    
    // 复制诊断数据
    diag->total_samples = data->total_samples;
    diag->type_changes = data->type_changes;
    diag->adc_errors = data->adc_errors;
    diag->min_mv = data->min_mv;
    diag->max_mv = data->max_mv;
    diag->last_mv = data->last_mv;
    diag->avg_mv = data->avg_mv;
    diag->current_type = data->current_device_type;
    diag->unstable_readings = data->unstable_readings;
    diag->consecutive_matches = data->consecutive_matches;
    
    // 复制设备类型计数
    memcpy(diag->type_counts, data->type_counts, sizeof(data->type_counts));
    
    // 复制历史记录
    memcpy(&diag->history, &data->history, sizeof(struct device_type_history));
    
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
    // 复制自适应稳定性数据
    diag->stability.voltage_variance = data->stability.voltage_variance;
    diag->stability.adaptive_threshold = data->stability.adaptive_threshold;
    diag->stability.noise_level = data->stability.noise_level;
    diag->stability.signal_quality = data->stability.signal_quality;
    diag->stability.false_triggers = data->stability.false_triggers;
    diag->stability.rejected_changes = data->stability.rejected_changes;
#else
    // 清空自适应稳定性数据
    memset(&diag->stability, 0, sizeof(struct stability_stats));
#endif
    
    return 0;
}

// 注册设备类型变化回调函数
int device_detector_register_callback(const struct device *dev, 
                                      device_type_changed_callback_t callback,
                                      void *user_data) {
    if (!dev || !callback) {
        return -EINVAL;
    }
    
    struct device_detector_data *data = dev->data;
    
    // 检查回调是否已注册
    for (int i = 0; i < data->callback_count; i++) {
        if (data->callbacks[i].callback == callback) {
            return -EALREADY;
        }
    }
    
    // 检查回调数量是否达到上限
    if (data->callback_count >= MAX_CALLBACKS) {
        return -ENOMEM;
    }
    
    // 注册新回调
    data->callbacks[data->callback_count].callback = callback;
    data->callbacks[data->callback_count].user_data = user_data;
    data->callback_count++;
    
    LOG_DBG("Registered device type change callback, total: %d", data->callback_count);
    
    return 0;
}

// 取消注册设备类型变化回调函数
int device_detector_unregister_callback(const struct device *dev, 
                                        device_type_changed_callback_t callback) {
    if (!dev || !callback) {
        return -EINVAL;
    }
    
    struct device_detector_data *data = dev->data;
    
    // 查找并移除回调
    for (int i = 0; i < data->callback_count; i++) {
        if (data->callbacks[i].callback == callback) {
            // 移动后面的回调前移一位
            for (int j = i; j < data->callback_count - 1; j++) {
                data->callbacks[j] = data->callbacks[j + 1];
            }
            data->callback_count--;
            
            LOG_DBG("Unregistered device type change callback, remaining: %d", data->callback_count);
            
            return 0;
        }
    }
    
    return -ENOENT;
}

/**
 * @brief 重置设备检测器诊断统计数据
 * 
 * @param dev 设备检测器设备
 * @return int 0表示成功，负值表示错误
 */
int device_detector_reset_diagnostics(const struct device *dev) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    struct device_detector_data *data = dev->data;
    
    // 重置诊断数据
    data->total_samples = 0;
    data->type_changes = 0;
    data->adc_errors = 0;
    data->unstable_readings = 0;
    data->consecutive_matches = 0;
    
    // 重置电压统计
    data->min_mv = INT32_MAX;
    data->max_mv = INT32_MIN;
    data->avg_mv = 0;
    
    // 重置设备类型计数
    for (int i = 0; i < 6; i++) {
        data->type_counts[i] = 0;
    }
    
    // 重置历史记录
    for (int i = 0; i < 5; i++) {
        data->history.types[i] = DEVICE_TYPE_NONE;
        data->history.timestamps[i] = 0;
    }
    data->history.index = 0;
    
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
    // 重置自适应稳定性数据
    data->voltage_history_index = 0;
    data->voltage_sum = 0;
    data->voltage_sum_squares = 0;
    data->rejected_changes = 0;
    data->stability.voltage_variance = 0;
    data->stability.adaptive_threshold = INITIAL_ADAPTIVE_THRESHOLD;
    data->stability.noise_level = 0;
    data->stability.signal_quality = 100;
    data->stability.false_triggers = 0;
    data->stability.rejected_changes = 0;
    
    // 重置电压历史数组
    for (int i = 0; i < ADAPTIVE_STABILITY_WINDOW; i++) {
        data->voltage_history[i] = 0;
    }
#endif
    
    LOG_INF("Device detector diagnostics reset");
    
    return 0;
}

// 设置设备检测器日志级别
int device_detector_set_log_level(const struct device *dev, uint8_t level) {
    if (dev == NULL) {
        return -EINVAL;
    }
    
    if (level > 4) {  // 日志级别范围 0-4
        return -EINVAL;
    }
    
#if defined(CONFIG_DEVICE_DETECTOR_RUNTIME_LOG_LEVEL)
    LOG_LEVEL_SET(device_detector, level);
    LOG_INF("Device detector log level set to %d", level);
    return 0;
#else
    LOG_WRN("Runtime log level control not enabled (CONFIG_DEVICE_DETECTOR_RUNTIME_LOG_LEVEL not set)");
    return -ENOTSUP;
#endif
}

// 强制设置设备类型（用于测试）
int device_detector_force_type(const struct device *dev, enum device_type type) {
    if (dev == NULL) {
        return -EINVAL;
    }
    
    if (type < DEVICE_TYPE_NONE || type > DEVICE_TYPE_OTHER) {
        return -EINVAL;
    }
    
    struct device_detector_data *data = dev->data;
    
    LOG_INF("Forcing device type to %s", device_type_to_str(type));
    
    // 保存当前类型
    enum device_type old_type = data->current_device_type;
    
    // 设置新类型
    data->current_device_type = type;
    
    // 如果类型变化，启动去抖动
    if (old_type != type) {
        // 记录变化时间
        data->last_change_time = k_uptime_get();
        
        // 直接触发去抖动工作，立即应用类型变化
        k_work_schedule(&data->debounce_work, K_NO_WAIT);
    }
    
    return 0;
}

#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
/**
 * @brief 启用或禁用自适应稳定性检测
 * 
 * @param dev 设备检测器设备
 * @param enabled 是否启用自适应稳定性检测
 * @return int 0表示成功，负值表示错误
 */
int device_detector_set_adaptive_stability(const struct device *dev, bool enabled) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    struct device_detector_data *data = dev->data;
    
    if (data->adaptive_stability_enabled == enabled) {
        return 0;  // 状态未变，无需操作
    }
    
    data->adaptive_stability_enabled = enabled;
    LOG_INF("Adaptive stability detection %s", enabled ? "enabled" : "disabled");
    
    // 重置稳定性历史，以避免旧的稳定性检测影响新设置
    reset_stability_history(data);
    
    // 如果启用，重置电压历史和统计数据
    if (enabled) {
        data->voltage_history_index = 0;
        data->voltage_sum = 0;
        data->voltage_sum_squares = 0;
        data->stability.voltage_variance = 0;
        data->stability.adaptive_threshold = INITIAL_ADAPTIVE_THRESHOLD;
        data->stability.noise_level = 0;
        data->stability.signal_quality = 100;
        
        // 重新初始化电压历史数组
        for (int i = 0; i < ADAPTIVE_STABILITY_WINDOW; i++) {
            data->voltage_history[i] = 0;
        }
    }
    
    return 0;
}

/**
 * @brief 获取稳定性统计信息
 * 
 * @param dev 设备检测器设备
 * @param stats 稳定性统计信息结构指针
 * @return int 0表示成功，负值表示错误
 */
int device_detector_get_stability_stats(const struct device *dev, 
                                       struct stability_stats *stats) {
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }
    
    struct device_detector_data *data = dev->data;
    
    // 复制稳定性统计数据
    memcpy(stats, &data->stability, sizeof(struct stability_stats));
    
    return 0;
}
#else
int device_detector_set_adaptive_stability(const struct device *dev, bool enabled) {
    ARG_UNUSED(dev);
    ARG_UNUSED(enabled);
    return -ENOTSUP;
}

int device_detector_get_stability_stats(const struct device *dev, 
                                       struct stability_stats *stats) {
    ARG_UNUSED(dev);
    ARG_UNUSED(stats);
    return -ENOTSUP;
}
#endif

#define DEVICE_DETECTOR_DEFINE(inst)                                                             \
    static struct device_detector_data device_detector_data_##inst = {0};                        \
    static uint16_t device_detector_value_##inst;                                                \
                                                                                                 \
    static const struct device_detector_config device_detector_config_##inst = {                 \
        .sampling_hz = DT_INST_PROP(inst, sampling_hz),                                          \
        .adc_channel = ADC_DT_SPEC_INST_GET(inst, 0),                                                  \
        .detection_delay_ms = DT_INST_PROP(inst, detection_delay_ms),                            \
        .voltage_ranges = DT_INST_PROP(inst, voltage_ranges),                                    \
    };                                                                                           \
                                                                                                 \
    PM_DEVICE_DT_INST_DEFINE(inst, device_detector_pm_action);                                   \
                                                                                                 \
    DEVICE_DT_INST_DEFINE(inst,                                                                  \
                         device_detector_init,                                                   \
                         PM_DEVICE_DT_INST_GET(inst),                                            \
                         &device_detector_data_##inst,                                           \
                         &device_detector_config_##inst,                                         \
                         POST_KERNEL,                                                            \
                         CONFIG_DEVICE_DETECTOR_INIT_PRIORITY,                                   \
                         &device_detector_api);

DT_INST_FOREACH_STATUS_OKAY(DEVICE_DETECTOR_DEFINE)
