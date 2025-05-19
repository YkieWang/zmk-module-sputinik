/*
 * Copyright (c) 2023 ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/input/input.h>

/* 轴通道配置 */
struct analog_input_axis_config {
    struct adc_dt_spec adc; /* ADC通道 */
    uint16_t mv_mid;        /* 中点电压(毫伏) */
    uint16_t mv_min_max;    /* 最小/最大偏移电压(毫伏) */
    uint16_t mv_deadzone;   /* 中点死区宽度(毫伏) */
    uint8_t scale_multiplier; /* 缩放乘数 */
    uint8_t scale_divisor;  /* 缩放除数 */
    bool invert;            /* 是否反转输出 */
    bool report_on_change_only; /* 仅在值变化时报告 */
    uint8_t evt_type;       /* 输入事件类型 */
    uint16_t input_code;    /* 输入事件代码 */
};

/* 模拟输入设备配置 */
struct analog_input_config {
    const struct analog_input_axis_config *axes; /* 轴配置数组 */
    uint8_t num_axes;       /* 轴数量 */
    uint16_t sampling_hz;   /* 采样频率(Hz) */
};

/* 轴状态 */
struct analog_input_axis_state {
    int32_t last_value;     /* 上次报告的值 */
    int32_t last_raw_value; /* 上次的原始值 */
};

/* 模拟输入设备数据 */
struct analog_input_data {
    struct analog_input_axis_state *axis_states; /* 轴状态数组 */
    struct k_work_delayable work; /* 延迟工作 */
    uint64_t last_sample_time; /* 上次采样时间 */
};

/* 功能API */
int analog_input_init(const struct device *dev);
int analog_input_get_value(const struct device *dev, uint8_t axis_idx, int32_t *value); 