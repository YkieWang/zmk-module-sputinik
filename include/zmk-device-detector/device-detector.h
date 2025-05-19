/*
 * Copyright (c) 2023 ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

/* 设备类型定义 */
enum zmk_device_type {
    ZMK_DEVICE_NONE = 0,     /* 无设备 */
    ZMK_DEVICE_TRACKBALL,    /* 轨迹球 */
    ZMK_DEVICE_JOYSTICK,     /* 摇杆 */
    ZMK_DEVICE_ENCODER,      /* 旋钮编码器 */
    ZMK_DEVICE_TYPE_COUNT,   /* 设备类型计数 */
};

/* 设备状态回调函数类型 */
typedef void (*zmk_device_status_callback_t)(enum zmk_device_type type, bool connected, void *user_data);

/* 设备检测器配置 */
struct zmk_device_detector_config {
    struct adc_dt_spec adc;              /* ADC 设备规格 */
    uint16_t adc_thresholds[ZMK_DEVICE_TYPE_COUNT]; /* 各设备类型的ADC阈值 */
    uint16_t adc_threshold_tolerance;    /* ADC阈值容差 */
    uint32_t poll_interval_ms;           /* 轮询间隔时间(毫秒) */
};

/* 设备检测器数据 */
struct zmk_device_detector_data {
    enum zmk_device_type detected_device;  /* 当前检测到的设备类型 */
    bool device_initialized;               /* 设备是否已初始化 */
    struct k_work_delayable work;          /* 延时工作结构 */
    zmk_device_status_callback_t status_callback; /* 状态回调函数 */
    void *callback_user_data;              /* 回调函数用户数据 */
};

/**
 * @brief 注册设备状态回调函数
 *
 * @param dev 设备检测器设备
 * @param callback 状态回调函数
 * @param user_data 用户数据
 * @return int 0表示成功，负值表示错误
 */
int zmk_device_detector_register_callback(const struct device *dev, 
                                        zmk_device_status_callback_t callback,
                                        void *user_data);

/**
 * @brief 取消注册设备状态回调函数
 *
 * @param dev 设备检测器设备
 * @return int 0表示成功，负值表示错误
 */
int zmk_device_detector_unregister_callback(const struct device *dev);

/**
 * @brief 获取当前检测到的设备类型
 *
 * @param dev 设备检测器设备
 * @return enum zmk_device_type 当前检测到的设备类型
 */
enum zmk_device_type zmk_device_detector_get_device_type(const struct device *dev);

/**
 * @brief 启动设备检测
 *
 * @param dev 设备检测器设备
 * @return int 0表示成功，负值表示错误
 */
int zmk_device_detector_start(const struct device *dev);

/**
 * @brief 停止设备检测
 *
 * @param dev 设备检测器设备
 * @return int 0表示成功，负值表示错误
 */
int zmk_device_detector_stop(const struct device *dev); 