/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>

/* EC11编码器配置 */
struct ec11_config {
    const struct gpio_dt_spec a;
    const struct gpio_dt_spec b;

    const uint16_t steps;
    const uint8_t resolution;
};

/* EC11编码器数据 */
struct ec11_data {
    uint8_t ab_state;
    int8_t pulses;
    int8_t ticks;
    int8_t delta;

#ifdef CONFIG_EC11_TRIGGER
    struct gpio_callback a_gpio_cb;
    struct gpio_callback b_gpio_cb;
    const struct device *dev;

    sensor_trigger_handler_t handler;
    const struct sensor_trigger *trigger;

#if defined(CONFIG_EC11_TRIGGER_OWN_THREAD)
    K_THREAD_STACK_MEMBER(thread_stack, CONFIG_EC11_THREAD_STACK_SIZE);
    struct k_sem gpio_sem;
    struct k_thread thread;
#elif defined(CONFIG_EC11_TRIGGER_GLOBAL_THREAD)
    struct k_work work;
#endif

#endif /* CONFIG_EC11_TRIGGER */
};

/* EC11 API */
int ec11_init(const struct device *dev);
int ec11_sample_fetch(const struct device *dev, enum sensor_channel chan);
int ec11_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val);

#ifdef CONFIG_EC11_TRIGGER
int ec11_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
                     sensor_trigger_handler_t handler);
int ec11_init_interrupt(const struct device *dev);
#endif 