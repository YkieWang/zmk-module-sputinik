/*
 * Copyright (c) 2023 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

// 兼容性头文件，重定向到device_detector.h
#include <zmk/drivers/device_detector.h>

// 定义别名以保持向后兼容
typedef enum device_type module_type;
#define MODULE_TYPE_NONE DEVICE_TYPE_NONE
#define MODULE_TYPE_KNOB DEVICE_TYPE_KNOB
#define MODULE_TYPE_JOYSTICK DEVICE_TYPE_JOYSTICK
#define MODULE_TYPE_TRACKBALL DEVICE_TYPE_TRACKBALL
#define MODULE_TYPE_SLIDER DEVICE_TYPE_SLIDER
#define MODULE_TYPE_OTHER DEVICE_TYPE_OTHER

// 结构体别名
typedef struct device_detector_diagnostics module_detector_diagnostics;

// 函数别名
#define module_detector_get_type device_detector_get_type
#define module_detector_get_voltage device_detector_get_voltage
#define module_detector_get_diagnostics device_detector_get_diagnostics
typedef device_type_changed_callback_t module_type_changed_callback_t;
#define module_detector_register_callback device_detector_register_callback
#define module_detector_unregister_callback device_detector_unregister_callback
#define module_type_to_str device_type_to_str 