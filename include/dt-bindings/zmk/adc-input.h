/*
 * Copyright (c) 2023 ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* 设备类型定义 */
#define ZMK_DEVICE_NONE      0
#define ZMK_DEVICE_TRACKBALL 1
#define ZMK_DEVICE_JOYSTICK  2
#define ZMK_DEVICE_ENCODER   3

/* 输入事件类型 */
#define INPUT_EV_SYN  0x00
#define INPUT_EV_KEY  0x01
#define INPUT_EV_REL  0x02
#define INPUT_EV_ABS  0x03

/* 相对轴事件代码 */
#define INPUT_REL_X    0x00
#define INPUT_REL_Y    0x01
#define INPUT_REL_Z    0x02
#define INPUT_REL_WHEEL 0x08

/* 绝对轴事件代码 */
#define INPUT_ABS_X    0x00
#define INPUT_ABS_Y    0x01
#define INPUT_ABS_Z    0x02 