/*
 * Copyright (c) 2023 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>

/**
 * @brief 设备类型定义
 */
enum device_type {
    DEVICE_TYPE_NONE = 0,     // 无设备
    DEVICE_TYPE_KNOB,         // 旋钮设备
    DEVICE_TYPE_JOYSTICK,     // 摇杆设备
    DEVICE_TYPE_TRACKBALL,    // 轨迹球设备
    DEVICE_TYPE_SLIDER,       // 滑块设备
    DEVICE_TYPE_OTHER,        // 其他类型设备
};

/**
 * @brief 获取当前检测到的设备类型
 * 
 * @param dev 设备检测器设备
 * @return enum device_type 当前检测到的设备类型
 */
enum device_type device_detector_get_type(const struct device *dev);

/**
 * @brief 注册设备类型变化回调函数
 * 
 * @param dev 设备检测器设备
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return int 0表示成功，负值表示错误
 */
typedef void (*device_type_changed_callback_t)(enum device_type type, void *user_data);
int device_detector_register_callback(const struct device *dev, 
                                      device_type_changed_callback_t callback,
                                      void *user_data);

/**
 * @brief 取消注册设备类型变化回调函数
 * 
 * @param dev 设备检测器设备
 * @param callback 回调函数
 * @return int 0表示成功，负值表示错误
 */
int device_detector_unregister_callback(const struct device *dev, 
                                        device_type_changed_callback_t callback);

/**
 * @brief 获取设备类型的字符串描述
 * 
 * @param type 设备类型
 * @return const char* 设备类型描述
 */
const char *device_type_to_str(enum device_type type); 