/*
 * Copyright (c) 2023 ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_analog_input

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>

#include <zmk-device-detector/analog-input.h>

LOG_MODULE_REGISTER(analog_input, CONFIG_ANALOG_INPUT_LOG_LEVEL);

/* 工作处理函数的前向声明 */
static void analog_input_work_handler(struct k_work *work);

/* 处理轴的ADC输入 */
static int process_axis_input(const struct device *dev, uint8_t axis_idx) {
    struct analog_input_data *data = dev->data;
    const struct analog_input_config *config = dev->config;
    const struct analog_input_axis_config *axis_cfg = &config->axes[axis_idx];
    struct analog_input_axis_state *axis_state = &data->axis_states[axis_idx];
    
    int err;
    int16_t adc_raw;
    int32_t mv_value = 0;
    int32_t out_value = 0;
    
    struct adc_sequence sequence = {
        .buffer = &adc_raw,
        .buffer_size = sizeof(adc_raw),
        .channels = BIT(axis_cfg->adc.channel_id),
        .resolution = 12, // 大多数ADC使用12位分辨率
    };

    /* 读取ADC值 */
    err = adc_read_dt(&axis_cfg->adc, &sequence);
    if (err) {
        LOG_ERR("无法读取ADC (轴 %d): %d", axis_idx, err);
        return err;
    }

    /* 转换为毫伏 */
    err = adc_raw_to_millivolts_dt(&axis_cfg->adc, &mv_value);
    if (err) {
        LOG_ERR("无法将ADC值转换为毫伏 (轴 %d): %d", axis_idx, err);
        return err;
    }

    if (IS_ENABLED(CONFIG_ANALOG_INPUT_LOG_DBG_RAW)) {
        LOG_DBG("轴 %d ADC 原始值: %d 毫伏", axis_idx, mv_value);
    }

    /* 处理中点偏移 */
    int32_t offset = mv_value - axis_cfg->mv_mid;
    
    /* 应用死区 */
    if (abs(offset) <= axis_cfg->mv_deadzone) {
        out_value = 0;
    } else {
        /* 应用缩放 */
        if (offset > 0) {
            offset = offset - axis_cfg->mv_deadzone;
        } else {
            offset = offset + axis_cfg->mv_deadzone;
        }
        
        /* 限制最大值 */
        if (abs(offset) > axis_cfg->mv_min_max) {
            offset = (offset > 0) ? axis_cfg->mv_min_max : -axis_cfg->mv_min_max;
        }
        
        /* 缩放到输出范围 */
        out_value = (offset * axis_cfg->scale_multiplier) / axis_cfg->scale_divisor;
        
        /* 反转如果需要 */
        if (axis_cfg->invert) {
            out_value = -out_value;
        }
    }
    
    /* 如果仅在变化时报告且值未变化，不报告 */
    if (axis_cfg->report_on_change_only && out_value == axis_state->last_value) {
        return 0;
    }
    
    /* 缓存新值 */
    axis_state->last_raw_value = mv_value;
    axis_state->last_value = out_value;
    
    if (IS_ENABLED(CONFIG_ANALOG_INPUT_LOG_DBG_REPORT)) {
        LOG_DBG("轴 %d 报告值: %d", axis_idx, out_value);
    }
    
    /* 报告输入事件 */
    input_report(dev, axis_cfg->evt_type, axis_cfg->input_code, out_value);
    
    return 0;
}

/* 工作处理函数 */
static void analog_input_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct analog_input_data *data = CONTAINER_OF(dwork, struct analog_input_data, work);
    const struct device *dev = CONTAINER_OF(data, struct analog_input_data, work.work)->work.dev;
    const struct analog_input_config *config = dev->config;
    
    uint64_t now = k_uptime_get();
    uint64_t min_interval_us = CONFIG_ANALOG_INPUT_REPORT_INTERVAL_MIN * 1000;  // 转为微秒
    
    /* 检查最小报告间隔 */
    if ((now - data->last_sample_time) < min_interval_us / 1000) {
        /* 重新调度工作 */
        k_work_schedule(dwork, K_MSEC(CONFIG_ANALOG_INPUT_REPORT_INTERVAL_MIN / 2));
        return;
    }
    
    /* 处理每个轴 */
    for (int i = 0; i < config->num_axes; i++) {
        process_axis_input(dev, i);
    }
    
    /* 同步输入事件 */
    input_sync(dev);
    
    /* 更新采样时间 */
    data->last_sample_time = now;
    
    /* 调度下一次采样 */
    int sample_interval_ms = 1000 / config->sampling_hz;
    k_work_schedule(dwork, K_MSEC(sample_interval_ms));
}

/* 设备初始化 */
static int analog_input_init(const struct device *dev) {
    struct analog_input_data *data = dev->data;
    const struct analog_input_config *config = dev->config;
    
    LOG_DBG("初始化模拟输入设备，轴数: %d", config->num_axes);
    
    /* 初始化轴状态 */
    data->axis_states = k_calloc(config->num_axes, sizeof(struct analog_input_axis_state));
    if (!data->axis_states) {
        LOG_ERR("无法分配轴状态内存");
        return -ENOMEM;
    }
    
    /* 初始化延时工作 */
    k_work_init_delayable(&data->work, analog_input_work_handler);
    
    /* 验证每个轴的ADC通道 */
    for (int i = 0; i < config->num_axes; i++) {
        if (!adc_is_ready_dt(&config->axes[i].adc)) {
            LOG_ERR("轴 %d 的ADC未就绪", i);
            k_free(data->axis_states);
            return -ENODEV;
        }
        
        /* 配置ADC通道 */
        int err = adc_channel_setup_dt(&config->axes[i].adc);
        if (err) {
            LOG_ERR("轴 %d 的ADC通道设置失败: %d", i, err);
            k_free(data->axis_states);
            return err;
        }
    }
    
    /* 启动采样 */
    int sample_interval_ms = 1000 / config->sampling_hz;
    k_work_schedule(&data->work, K_MSEC(sample_interval_ms));
    
    LOG_INF("模拟输入设备初始化完成，采样频率: %d Hz", config->sampling_hz);
    return 0;
}

/* 获取轴的当前值 */
int analog_input_get_value(const struct device *dev, uint8_t axis_idx, int32_t *value) {
    struct analog_input_data *data = dev->data;
    const struct analog_input_config *config = dev->config;
    
    if (axis_idx >= config->num_axes) {
        return -EINVAL;
    }
    
    *value = data->axis_states[axis_idx].last_value;
    return 0;
}

/* 轴配置宏 */
#define ANALOG_INPUT_AXIS_CHILD_INIT(node_id)                                           \
    {                                                                                   \
        .adc = ADC_DT_SPEC_GET(node_id),                                               \
        .mv_mid = DT_PROP_OR(node_id, mv_mid, 1650),                                   \
        .mv_min_max = DT_PROP_OR(node_id, mv_min_max, 1600),                          \
        .mv_deadzone = DT_PROP_OR(node_id, mv_deadzone, 10),                          \
        .scale_multiplier = DT_PROP_OR(node_id, scale_multiplier, 1),                 \
        .scale_divisor = DT_PROP_OR(node_id, scale_divisor, 1),                       \
        .invert = DT_PROP_OR(node_id, invert, false),                                 \
        .report_on_change_only = DT_PROP_OR(node_id, report_on_change_only, false),  \
        .evt_type = DT_PROP(node_id, evt_type),                                       \
        .input_code = DT_PROP(node_id, input_code),                                   \
    },

/* 设备定义宏 */
#define ANALOG_INPUT_DEFINE(n)                                                        \
    static struct analog_input_data analog_input_data_##n = {                        \
        .axis_states = NULL,                                                         \
        .last_sample_time = 0,                                                       \
    };                                                                               \
                                                                                     \
    static const struct analog_input_axis_config                                     \
        analog_input_axis_cfg_##n[] = {                                             \
            DT_INST_FOREACH_CHILD(n, ANALOG_INPUT_AXIS_CHILD_INIT)                  \
    };                                                                               \
                                                                                     \
    static const struct analog_input_config analog_input_config_##n = {             \
        .axes = analog_input_axis_cfg_##n,                                          \
        .num_axes = ARRAY_SIZE(analog_input_axis_cfg_##n),                          \
        .sampling_hz = DT_INST_PROP_OR(n, sampling_hz, 100),                        \
    };                                                                               \
                                                                                     \
    DEVICE_DT_INST_DEFINE(n, analog_input_init, NULL, &analog_input_data_##n,       \
                         &analog_input_config_##n, POST_KERNEL,                     \
                         CONFIG_SENSOR_INIT_PRIORITY, NULL);                         \

/* 应用设备定义宏到所有设备树中匹配的节点 */
DT_INST_FOREACH_STATUS_OKAY(ANALOG_INPUT_DEFINE) 