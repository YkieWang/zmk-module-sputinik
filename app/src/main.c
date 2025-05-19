/*
 * Copyright (c) 2023 ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zmk-device-detector/device-detector.h>

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

/* 设备状态回调函数 */
static void device_status_callback(enum zmk_device_type type, bool connected, void *user_data) {
    const char *device_names[] = {
        "None",
        "Trackball",
        "Joystick",
        "Encoder"
    };

    LOG_INF("设备 %s 状态变更: %s", device_names[type], connected ? "已连接" : "已断开");
}

void main(void) {
    LOG_INF("ZMK设备检测器测试程序启动");

    /* 获取设备检测器设备 */
    const struct device *dev = DEVICE_DT_GET_ONE(zmk_device_detector);
    if (!device_is_ready(dev)) {
        LOG_ERR("设备检测器未就绪");
        return;
    }

    /* 注册设备状态回调 */
    int ret = zmk_device_detector_register_callback(dev, device_status_callback, NULL);
    if (ret) {
        LOG_ERR("注册设备状态回调失败: %d", ret);
        return;
    }

    /* 启动设备检测器 */
    ret = zmk_device_detector_start(dev);
    if (ret) {
        LOG_ERR("启动设备检测器失败: %d", ret);
        return;
    }

    LOG_INF("设备检测器已启动，等待设备检测...");

    /* 主循环 */
    while (1) {
        k_sleep(K_SECONDS(1));
        
        enum zmk_device_type type = zmk_device_detector_get_device_type(dev);
        LOG_DBG("当前检测到的设备类型: %d", type);
    }
} 