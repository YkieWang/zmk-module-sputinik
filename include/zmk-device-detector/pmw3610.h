/*
 * Copyright (c) 2023 ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/sensor.h>

/* PMW3610寄存器定义 */
#define PMW3610_REG_PRODUCT_ID        0x00
#define PMW3610_REG_REVISION_ID       0x01
#define PMW3610_REG_MOTION            0x02
#define PMW3610_REG_DELTA_X_L         0x03
#define PMW3610_REG_DELTA_Y_L         0x04
#define PMW3610_REG_DELTA_XY_H        0x05
#define PMW3610_REG_SQUAL             0x06
#define PMW3610_REG_SHUTTER_UPPER     0x07
#define PMW3610_REG_SHUTTER_LOWER     0x08
#define PMW3610_REG_MAXIMUM_PIXEL     0x09
#define PMW3610_REG_SUM_PIXEL         0x0A
#define PMW3610_REG_MINIMUM_PIXEL     0x0B
#define PMW3610_REG_PIXEL_SUM         0x0C
#define PMW3610_REG_CRC0              0x0D
#define PMW3610_REG_CRC1              0x0E
#define PMW3610_REG_CRC2              0x0F
#define PMW3610_REG_CRC3              0x10
#define PMW3610_REG_SELF_TEST         0x11
#define PMW3610_REG_POWER_MANAGEMENT  0x12
#define PMW3610_REG_RUN_DOWNSHIFT     0x13
#define PMW3610_REG_REST1_PERIOD      0x14
#define PMW3610_REG_REST1_DOWNSHIFT   0x15
#define PMW3610_REG_REST2_PERIOD      0x16
#define PMW3610_REG_REST2_DOWNSHIFT   0x17
#define PMW3610_REG_REST3_PERIOD      0x18
#define PMW3610_REG_PERFORMANCE       0x19
#define PMW3610_REG_RESOLUTION        0x1A
#define PMW3610_REG_ORIENTATION       0x1F

/* PMW3610配置值 */
#define PMW3610_PRODUCT_ID            0x3E
#define PMW3610_DEFAULT_CPI           400
#define PMW3610_DEFAULT_RUN_DOWNSHIFT 0x05 /* ~10ms */
#define PMW3610_DEFAULT_REST1_PERIOD  0x01 /* ~10ms */
#define PMW3610_DEFAULT_REST1_DOWNSHIFT 0x03 /* ~1ms */
#define PMW3610_DEFAULT_REST2_PERIOD  0x09 /* ~30ms */

/* PMW3610设备配置 */
struct pmw3610_config {
    struct spi_dt_spec bus;
    struct gpio_dt_spec irq_gpio;
    uint16_t cpi;
    uint8_t evt_type;
    uint16_t x_input_code;
    uint16_t y_input_code;
    bool force_awake;
    bool swap_xy;
    bool invert_x;
    bool invert_y;
};

/* PMW3610设备数据 */
struct pmw3610_data {
    const struct device *dev;
    struct gpio_callback irq_cb;
    struct k_work_delayable report_timer;
    struct k_work irq_work;
    uint8_t op_mode;
    int32_t delta_x;
    int32_t delta_y;
    uint64_t timestamp_reporting;
};

/* PMW3610 API */
int pmw3610_init(const struct device *dev);
int pmw3610_sample_fetch(const struct device *dev, enum sensor_channel chan);
int pmw3610_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val); 