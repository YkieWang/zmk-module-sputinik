/*
 * Copyright (c) 2023 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zmk/drivers/device_detector.h>

LOG_MODULE_REGISTER(detector_test, CONFIG_LOG_DEFAULT_LEVEL);

// 设备类型改变回调
static void device_type_changed_cb(enum device_type type, void *user_data) {
    LOG_INF("Device type changed to: %s", device_type_to_str(type));
}

int main(void) {
    LOG_INF("Device Detector Test Application");
    
    // 获取设备检测器设备
    const struct device *detector = DEVICE_DT_GET(DT_INST(0, zmk_device_detector));
    
    if (!device_is_ready(detector)) {
        LOG_ERR("Device detector not ready");
        return -1;
    }
    
    LOG_INF("Device detector ready");
    
    // 注册回调
    int err = device_detector_register_callback(detector, device_type_changed_cb, NULL);
    if (err) {
        LOG_ERR("Failed to register callback: %d", err);
        return -1;
    }
    
    // 启用自适应稳定性检测（如果支持）
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
    device_detector_set_adaptive_stability(detector, true);
    LOG_INF("Adaptive stability enabled");
#endif
    
    // 主循环：每秒读取设备类型和电压信息
    while (1) {
        // 获取设备类型
        enum device_type type = device_detector_get_type(detector);
        
        // 获取电压值
        int32_t mv;
        if (device_detector_get_voltage(detector, &mv) == 0) {
            LOG_INF("Current device: %s, Voltage: %d mV", device_type_to_str(type), mv);
        } else {
            LOG_ERR("Failed to read voltage");
        }
        
        // 每隔一段时间获取诊断信息
        static int diag_counter = 0;
        if (++diag_counter >= 10) {
            diag_counter = 0;
            
            // 获取诊断信息
            struct device_detector_diagnostics diag;
            if (device_detector_get_diagnostics(detector, &diag) == 0) {
                LOG_INF("Diagnostics:");
                LOG_INF("  Samples: %u, Changes: %u, Errors: %u", 
                        diag.total_samples, diag.type_changes, diag.adc_errors);
                LOG_INF("  Voltage: min=%d, max=%d, avg=%d mV", 
                        diag.min_mv, diag.max_mv, diag.avg_mv);
                LOG_INF("  Unstable readings: %u", diag.unstable_readings);
                
#ifdef CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY
                LOG_INF("  Signal quality: %u%%", diag.stability.signal_quality);
                LOG_INF("  Adaptive threshold: %u", diag.stability.adaptive_threshold);
#endif
            }
        }
        
        k_sleep(K_MSEC(1000));
    }
    
    return 0;
} 