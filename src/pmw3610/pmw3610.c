/*
 * Copyright (c) 2023 ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT pixart_pmw3610

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/input/input.h>

#include <zmk-device-detector/pmw3610.h>

LOG_MODULE_REGISTER(pmw3610, CONFIG_PMW3610_LOG_LEVEL);

/* SPI操作 */
static int pmw3610_reg_read(const struct device *dev, uint8_t reg, uint8_t *value) {
    struct pmw3610_data *data = dev->data;
    const struct pmw3610_config *config = dev->config;
    const struct spi_dt_spec *bus = &config->bus;

    uint8_t tx_buffer = 0x80 | reg;
    uint8_t rx_buffer[2] = {0};

    const struct spi_buf tx_buf = {
        .buf = &tx_buffer,
        .len = 1,
    };
    const struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1,
    };

    const struct spi_buf rx_buf[2] = {
        {
            .buf = NULL,
            .len = 1, /* skip */
        },
        {
            .buf = &rx_buffer[1],
            .len = 1,
        }
    };
    const struct spi_buf_set rx = {
        .buffers = rx_buf,
        .count = 2,
    };

    int err = spi_transceive_dt(bus, &tx, &rx);
    if (err) {
        LOG_ERR("SPI读取失败: %d", err);
        return err;
    }

    *value = rx_buffer[1];
    return 0;
}

static int pmw3610_reg_write(const struct device *dev, uint8_t reg, uint8_t value) {
    struct pmw3610_data *data = dev->data;
    const struct pmw3610_config *config = dev->config;
    const struct spi_dt_spec *bus = &config->bus;

    uint8_t tx_buffer[2] = {reg, value};

    const struct spi_buf tx_buf = {
        .buf = tx_buffer,
        .len = 2,
    };
    const struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1,
    };

    int err = spi_write_dt(bus, &tx);
    if (err) {
        LOG_ERR("SPI写入失败: %d", err);
        return err;
    }

    return 0;
}

/* 初始化PMW3610传感器 */
static int pmw3610_init_sensor(const struct device *dev) {
    struct pmw3610_data *data = dev->data;
    const struct pmw3610_config *config = dev->config;
    int err;
    uint8_t product_id;

    /* 等待额外的初始化延迟(用于等待外部电源稳定) */
    k_msleep(CONFIG_PMW3610_INIT_POWER_UP_EXTRA_DELAY_MS);

    /* 读取产品ID确认传感器存在 */
    err = pmw3610_reg_read(dev, PMW3610_REG_PRODUCT_ID, &product_id);
    if (err) {
        LOG_ERR("无法读取产品ID: %d", err);
        return err;
    }

    if (product_id != PMW3610_PRODUCT_ID) {
        LOG_ERR("产品ID不正确 0x%x (应为 0x%x)!", product_id, PMW3610_PRODUCT_ID);
        return -ENODEV;
    }

    LOG_DBG("找到PMW3610传感器，产品ID: 0x%x", product_id);

    /* 应用设置 */
    uint8_t resolution = (config->cpi / 50) - 1;
    if (resolution > 0x77) {
        resolution = 0x77;
    }

    /* 设置分辨率 */
    err = pmw3610_reg_write(dev, PMW3610_REG_RESOLUTION, resolution);
    if (err) {
        LOG_ERR("设置分辨率失败: %d", err);
        return err;
    }

    /* 配置省电设置 */
    err = pmw3610_reg_write(dev, PMW3610_REG_RUN_DOWNSHIFT, PMW3610_DEFAULT_RUN_DOWNSHIFT);
    if (err) {
        return err;
    }

    err = pmw3610_reg_write(dev, PMW3610_REG_REST1_PERIOD, PMW3610_DEFAULT_REST1_PERIOD);
    if (err) {
        return err;
    }

    err = pmw3610_reg_write(dev, PMW3610_REG_REST1_DOWNSHIFT, PMW3610_DEFAULT_REST1_DOWNSHIFT);
    if (err) {
        return err;
    }

    err = pmw3610_reg_write(dev, PMW3610_REG_REST2_PERIOD, PMW3610_DEFAULT_REST2_PERIOD);
    if (err) {
        return err;
    }

    LOG_INF("PMW3610传感器初始化完成，CPI: %d", config->cpi);
    return 0;
}

/* 中断处理函数 */
static void pmw3610_gpio_callback(const struct device *port, struct gpio_callback *cb,
                                  uint32_t pins) {
    struct pmw3610_data *data = CONTAINER_OF(cb, struct pmw3610_data, irq_cb);
    k_work_submit(&data->irq_work);
}

/* IRQ工作函数 */
static void pmw3610_irq_worker(struct k_work *work) {
    struct pmw3610_data *data = CONTAINER_OF(work, struct pmw3610_data, irq_work);
    const struct device *dev = data->dev;
    const struct pmw3610_config *config = dev->config;
    uint8_t motion, delta_xy_high, delta_x_low, delta_y_low;
    int16_t delta_x, delta_y;
    int err;

    /* 读取运动状态 */
    err = pmw3610_reg_read(dev, PMW3610_REG_MOTION, &motion);
    if (err) {
        LOG_ERR("读取运动状态失败: %d", err);
        return;
    }

    /* 如果没有运动，直接返回 */
    if (!(motion & 0x80)) {
        return;
    }

    /* 读取增量值 */
    err = pmw3610_reg_read(dev, PMW3610_REG_DELTA_X_L, &delta_x_low);
    if (err) {
        LOG_ERR("读取X增量低位失败: %d", err);
        return;
    }

    err = pmw3610_reg_read(dev, PMW3610_REG_DELTA_Y_L, &delta_y_low);
    if (err) {
        LOG_ERR("读取Y增量低位失败: %d", err);
        return;
    }

    err = pmw3610_reg_read(dev, PMW3610_REG_DELTA_XY_H, &delta_xy_high);
    if (err) {
        LOG_ERR("读取XY增量高位失败: %d", err);
        return;
    }

    /* 构建完整的增量值 */
    delta_x = ((int16_t)((delta_xy_high & 0xF0) << 4) | delta_x_low);
    delta_y = ((int16_t)((delta_xy_high & 0x0F) << 8) | delta_y_low);

    /* 根据配置调整X/Y轴 */
    if (config->swap_xy) {
        int16_t temp = delta_x;
        delta_x = delta_y;
        delta_y = temp;
    }

    if (config->invert_x) {
        delta_x = -delta_x;
    }

    if (config->invert_y) {
        delta_y = -delta_y;
    }

    /* 累加增量 */
    data->delta_x += delta_x;
    data->delta_y += delta_y;

    /* 检查是否到达报告时间 */
    uint64_t now = k_uptime_get();
    uint64_t diff = now - data->timestamp_reporting;
    if (diff < CONFIG_PMW3610_REPORT_INTERVAL_MIN) {
        return;
    }

    /* 报告输入事件 */
    if (data->delta_x != 0) {
        input_report(dev, config->evt_type, config->x_input_code, data->delta_x);
        data->delta_x = 0;
    }

    if (data->delta_y != 0) {
        input_report(dev, config->evt_type, config->y_input_code, data->delta_y);
        data->delta_y = 0;
    }

    input_sync(dev);
    data->timestamp_reporting = now;
}

/* 设备初始化 */
static int pmw3610_init(const struct device *dev) {
    struct pmw3610_data *data = dev->data;
    const struct pmw3610_config *config = dev->config;
    int err;

    data->dev = dev;
    data->delta_x = 0;
    data->delta_y = 0;
    data->timestamp_reporting = 0;

    /* 初始化SPI总线 */
    if (!spi_is_ready_dt(&config->bus)) {
        LOG_ERR("SPI总线未就绪");
        return -ENODEV;
    }

    /* 初始化中断GPIO */
    if (!gpio_is_ready_dt(&config->irq_gpio)) {
        LOG_ERR("中断GPIO未就绪");
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT);
    if (err) {
        LOG_ERR("配置中断GPIO失败: %d", err);
        return err;
    }

    /* 初始化传感器 */
    err = pmw3610_init_sensor(dev);
    if (err) {
        LOG_ERR("初始化传感器失败: %d", err);
        return err;
    }

    /* 设置中断 */
    k_work_init(&data->irq_work, pmw3610_irq_worker);

    gpio_init_callback(&data->irq_cb, pmw3610_gpio_callback, BIT(config->irq_gpio.pin));
    err = gpio_add_callback(config->irq_gpio.port, &data->irq_cb);
    if (err) {
        LOG_ERR("添加GPIO回调失败: %d", err);
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&config->irq_gpio, GPIO_INT_EDGE_FALLING);
    if (err) {
        LOG_ERR("配置GPIO中断失败: %d", err);
        return err;
    }

    return 0;
}

/* 传感器API实现 */
static int pmw3610_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    /* 数据已经在中断中获取，无需在此处操作 */
    return 0;
}

static int pmw3610_channel_get(const struct device *dev, enum sensor_channel chan,
                               struct sensor_value *val) {
    struct pmw3610_data *data = dev->data;

    if (chan == SENSOR_CHAN_POS_DX) {
        val->val1 = data->delta_x;
        val->val2 = 0;
        return 0;
    }

    if (chan == SENSOR_CHAN_POS_DY) {
        val->val1 = data->delta_y;
        val->val2 = 0;
        return 0;
    }

    return -ENOTSUP;
}

/* 传感器API结构体 */
static const struct sensor_driver_api pmw3610_api = {
    .sample_fetch = pmw3610_sample_fetch,
    .channel_get = pmw3610_channel_get,
};

/* 设备实例化宏 */
#define PMW3610_DEFINE(n)                                                                        \
    static struct pmw3610_data pmw3610_data_##n = {                                              \
        .delta_x = 0,                                                                            \
        .delta_y = 0,                                                                            \
        .timestamp_reporting = 0,                                                                \
    };                                                                                           \
                                                                                                 \
    static const struct pmw3610_config pmw3610_config_##n = {                                    \
        .bus = SPI_DT_SPEC_INST_GET(n, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0),                \
        .irq_gpio = GPIO_DT_SPEC_INST_GET(n, irq_gpios),                                        \
        .cpi = DT_INST_PROP_OR(n, cpi, PMW3610_DEFAULT_CPI),                                     \
        .evt_type = DT_INST_PROP_OR(n, evt_type, INPUT_EV_REL),                                 \
        .x_input_code = DT_INST_PROP_OR(n, x_input_code, INPUT_REL_X),                          \
        .y_input_code = DT_INST_PROP_OR(n, y_input_code, INPUT_REL_Y),                          \
        .force_awake = DT_INST_PROP_OR(n, force_awake, false),                                  \
        .swap_xy = IS_ENABLED(CONFIG_PMW3610_SWAP_XY),                                          \
        .invert_x = IS_ENABLED(CONFIG_PMW3610_INVERT_X),                                        \
        .invert_y = IS_ENABLED(CONFIG_PMW3610_INVERT_Y),                                        \
    };                                                                                           \
                                                                                                 \
    DEVICE_DT_INST_DEFINE(n, pmw3610_init, NULL, &pmw3610_data_##n, &pmw3610_config_##n,       \
                         POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &pmw3610_api);               \

/* 应用设备实例化宏到所有设备树中匹配的节点 */
DT_INST_FOREACH_STATUS_OKAY(PMW3610_DEFINE) 