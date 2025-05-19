/*
 * Copyright (c) 2023 ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_device_detector

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk-device-detector/device-detector.h>

LOG_MODULE_REGISTER(zmk_device_detector, CONFIG_ZMK_DEVICE_DETECTOR_LOG_LEVEL);

/* 设备类型名称，方便日志输出 */
static const char *device_type_names[] = {
    "None",
    "Trackball",
    "Joystick",
    "Encoder",
};

/* ADC序列的缓冲区 */
static int16_t adc_buffer;

/* 设备检测工作处理函数的前向声明 */
static void device_detector_work_handler(struct k_work *work);

/* 初始化设备类型对应的驱动 */
static int initialize_device(const struct device *dev, enum zmk_device_type type) {
    struct zmk_device_detector_data *data = dev->data;
    int ret = 0;

    /* 如果检测到的设备类型没有改变且已经初始化，则无需重新初始化 */
    if (data->detected_device == type && data->device_initialized) {
        return 0;
    }

    /* 如果之前已经初始化了其他设备，首先取消初始化 */
    if (data->device_initialized && data->detected_device != ZMK_DEVICE_NONE) {
        /* 这里可以添加设备特定的取消初始化代码 */
        data->device_initialized = false;
        
        /* 通知设备已断开连接 */
        if (data->status_callback) {
            data->status_callback(data->detected_device, false, data->callback_user_data);
        }
    }

    /* 记录新的设备类型 */
    data->detected_device = type;

    /* 如果没有设备，直接返回 */
    if (type == ZMK_DEVICE_NONE) {
        return 0;
    }

    LOG_INF("初始化设备类型: %s", device_type_names[type]);

    /* 根据设备类型进行初始化 */
    switch (type) {
        case ZMK_DEVICE_TRACKBALL: {
            /* 查找PMW3610设备 */
            const struct device *trackball_dev = device_get_binding("pmw3610");
            if (trackball_dev == NULL) {
                LOG_ERR("未找到PMW3610轨迹球设备");
                ret = -ENODEV;
            } else {
                LOG_INF("找到并初始化PMW3610轨迹球");
            }
            break;
        }
        case ZMK_DEVICE_JOYSTICK: {
            /* 查找模拟输入设备 */
            const struct device *joystick_dev = device_get_binding("analog_input");
            if (joystick_dev == NULL) {
                LOG_ERR("未找到模拟输入(摇杆)设备");
                ret = -ENODEV;
            } else {
                LOG_INF("找到并初始化模拟输入(摇杆)");
            }
            break;
        }
        case ZMK_DEVICE_ENCODER: {
            /* 查找EC11编码器设备 */
            const struct device *encoder_dev = device_get_binding("ec11");
            if (encoder_dev == NULL) {
                LOG_ERR("未找到EC11编码器设备");
                ret = -ENODEV;
            } else {
                LOG_INF("找到并初始化EC11编码器");
            }
            break;
        }
        default:
            LOG_WRN("未知设备类型: %d", type);
            ret = -EINVAL;
            break;
    }

    /* 如果初始化成功，标记为已初始化 */
    if (ret == 0) {
        data->device_initialized = true;

        /* 通知设备已连接 */
        if (data->status_callback) {
            data->status_callback(type, true, data->callback_user_data);
        }
    }

    return ret;
}

/* 设备检测的工作处理函数，执行ADC采样并识别设备类型 */
static void device_detector_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct zmk_device_detector_data *data = 
        CONTAINER_OF(dwork, struct zmk_device_detector_data, work);
    const struct device *dev = data->callback_user_data;
    const struct zmk_device_detector_config *config = dev->config;
    
    int ret;
    enum zmk_device_type detected_type = ZMK_DEVICE_NONE;
    struct adc_sequence sequence = {
        .buffer = &adc_buffer,
        .buffer_size = sizeof(adc_buffer),
        .channels = BIT(config->adc.channel_id),
        .resolution = 12, // 大多数ADC使用12位分辨率
    };

    if (!adc_is_ready_dt(&config->adc)) {
        LOG_ERR("ADC设备未就绪");
        goto reschedule;
    }

    /* 执行ADC采样 */
    ret = adc_read_dt(&config->adc, &sequence);
    if (ret < 0) {
        LOG_ERR("无法读取ADC: %d", ret);
        goto reschedule;
    }

    /* 将ADC值转换为毫伏 */
    int32_t mv_value = 0;
    ret = adc_raw_to_millivolts_dt(&config->adc, &mv_value);
    if (ret < 0) {
        LOG_ERR("无法将ADC值转换为毫伏: %d", ret);
        goto reschedule;
    }

    LOG_DBG("ADC读数: %d 毫伏", mv_value);

    /* 基于ADC值识别设备类型 */
    for (int i = 1; i < ZMK_DEVICE_TYPE_COUNT; i++) {
        if (abs(mv_value - config->adc_thresholds[i]) <= config->adc_threshold_tolerance) {
            detected_type = i;
            break;
        }
    }

    LOG_DBG("检测到设备类型: %s", device_type_names[detected_type]);

    /* 初始化检测到的设备 */
    ret = initialize_device(dev, detected_type);
    if (ret < 0) {
        LOG_ERR("无法初始化设备: %d", ret);
    }

reschedule:
    /* 重新调度工作以继续轮询 */
    k_work_schedule(dwork, K_MSEC(config->poll_interval_ms));
}

/* API函数实现 */

int zmk_device_detector_register_callback(const struct device *dev, 
                                        zmk_device_status_callback_t callback,
                                        void *user_data) {
    struct zmk_device_detector_data *data = dev->data;

    if (callback == NULL) {
        return -EINVAL;
    }

    data->status_callback = callback;
    data->callback_user_data = user_data;

    return 0;
}

int zmk_device_detector_unregister_callback(const struct device *dev) {
    struct zmk_device_detector_data *data = dev->data;

    data->status_callback = NULL;
    data->callback_user_data = NULL;

    return 0;
}

enum zmk_device_type zmk_device_detector_get_device_type(const struct device *dev) {
    struct zmk_device_detector_data *data = dev->data;
    return data->detected_device;
}

int zmk_device_detector_start(const struct device *dev) {
    struct zmk_device_detector_data *data = dev->data;
    const struct zmk_device_detector_config *config = dev->config;

    /* 初始化ADC设备 */
    if (!adc_is_ready_dt(&config->adc)) {
        LOG_ERR("ADC设备未就绪");
        return -ENODEV;
    }

    /* 设置回调数据 */
    data->callback_user_data = (void *)dev;

    /* 启动轮询工作 */
    return k_work_schedule(&data->work, K_MSEC(config->poll_interval_ms));
}

int zmk_device_detector_stop(const struct device *dev) {
    struct zmk_device_detector_data *data = dev->data;

    /* 取消轮询工作 */
    return k_work_cancel_delayable(&data->work);
}

/* 设备初始化函数 */
static int device_detector_init(const struct device *dev) {
    struct zmk_device_detector_data *data = dev->data;
    const struct zmk_device_detector_config *config = dev->config;

    /* 验证ADC设备 */
    if (!adc_is_ready_dt(&config->adc)) {
        LOG_ERR("ADC设备未就绪");
        return -ENODEV;
    }

    /* 初始化延时工作 */
    k_work_init_delayable(&data->work, device_detector_work_handler);

    /* 初始化数据 */
    data->detected_device = ZMK_DEVICE_NONE;
    data->device_initialized = false;
    data->status_callback = NULL;
    data->callback_user_data = NULL;

    LOG_INF("设备检测器初始化完成");
    return 0;
}

/* 设备实例化宏 */
#define ZMK_DEVICE_DETECTOR_INIT(n)                                                 \
    static struct zmk_device_detector_data device_detector_data_##n = {             \
        .detected_device = ZMK_DEVICE_NONE,                                         \
        .device_initialized = false,                                                 \
    };                                                                               \
                                                                                     \
    static const struct zmk_device_detector_config device_detector_config_##n = {    \
        .adc = ADC_DT_SPEC_INST_GET(n),                                             \
        .adc_thresholds = DT_INST_PROP_OR(n, adc_thresholds, {0}),                  \
        .adc_threshold_tolerance = DT_INST_PROP_OR(n, adc_threshold_tolerance, 50), \
        .poll_interval_ms = DT_INST_PROP_OR(n, poll_interval_ms, 1000),             \
    };                                                                               \
                                                                                     \
    DEVICE_DT_INST_DEFINE(n,                                                         \
                         device_detector_init,                                       \
                         NULL,                                                       \
                         &device_detector_data_##n,                                  \
                         &device_detector_config_##n,                                \
                         POST_KERNEL,                                                \
                         CONFIG_ZMK_DEVICE_DETECTOR_INIT_PRIORITY,                   \
                         NULL);

/* 应用设备实例化宏到所有设备树中匹配的节点 */
DT_INST_FOREACH_STATUS_OKAY(ZMK_DEVICE_DETECTOR_INIT) 