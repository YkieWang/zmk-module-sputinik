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
 * @brief 设备类型历史记录结构
 */
struct device_type_history {
    enum device_type types[5];        // 最近5次检测到的设备类型
    int64_t timestamps[5];            // 对应的时间戳 (ms)
    uint8_t index;                    // 当前索引位置
};

/**
 * @brief 稳定性统计信息结构
 */
struct stability_stats {
    uint32_t voltage_variance;         // 电压波动度量
    uint16_t adaptive_threshold;       // 自适应稳定性阈值
    uint32_t noise_level;              // 估计的噪声水平
    uint32_t signal_quality;           // 信号质量评分 (0-100)
    uint32_t false_triggers;           // 误触发次数
    uint32_t rejected_changes;         // 被拒绝的变化次数
};

/**
 * @brief 设备检测器诊断信息结构
 */
struct device_detector_diagnostics {
    uint32_t total_samples;    // 总采样次数
    uint32_t type_changes;     // 设备类型变化次数
    uint32_t adc_errors;       // ADC错误次数
    int32_t min_mv;            // 检测到的最小电压 (mV)
    int32_t max_mv;            // 检测到的最大电压 (mV)
    int32_t last_mv;           // 最近一次电压读数 (mV)
    int32_t avg_mv;            // 平均电压值 (mV)
    enum device_type current_type; // 当前设备类型
    
    // 每种设备类型的检测次数
    uint32_t type_counts[6];   // 对应每种设备类型的检测次数 (包括NONE)
    
    // 设备类型历史记录
    struct device_type_history history;
    
    // 稳定性指标
    uint32_t unstable_readings;     // 不稳定读数次数
    uint32_t consecutive_matches;   // 当前连续匹配次数
    
    // 自适应稳定性统计
    struct stability_stats stability;
};

/**
 * @brief 获取当前检测到的设备类型
 * 
 * @param dev 设备检测器设备
 * @return enum device_type 当前检测到的设备类型
 */
enum device_type device_detector_get_type(const struct device *dev);

/**
 * @brief 获取当前ADC电压值
 * 
 * @param dev 设备检测器设备
 * @param mv 用于存储电压值的指针 (mV)
 * @return int 0表示成功，负值表示错误
 */
int device_detector_get_voltage(const struct device *dev, int32_t *mv);

/**
 * @brief 获取设备检测器诊断信息
 * 
 * @param dev 设备检测器设备
 * @param diag 诊断信息结构指针
 * @return int 0表示成功，负值表示错误
 */
int device_detector_get_diagnostics(const struct device *dev, 
                                   struct device_detector_diagnostics *diag);

/**
 * @brief 重置设备检测器诊断统计数据
 * 
 * @param dev 设备检测器设备
 * @return int 0表示成功，负值表示错误
 */
int device_detector_reset_diagnostics(const struct device *dev);

/**
 * @brief 设置设备检测器日志级别
 * 
 * @param dev 设备检测器设备
 * @param level 日志级别 (0-4, 与CONFIG_DEVICE_DETECTOR_LOG_LEVEL对应)
 * @return int 0表示成功，负值表示错误
 */
int device_detector_set_log_level(const struct device *dev, uint8_t level);

/**
 * @brief 强制设置设备类型（用于测试）
 * 
 * @param dev 设备检测器设备
 * @param type 要设置的设备类型
 * @return int 0表示成功，负值表示错误
 */
int device_detector_force_type(const struct device *dev, enum device_type type);

/**
 * @brief 启用或禁用自适应稳定性检测
 * 
 * @param dev 设备检测器设备
 * @param enabled 是否启用自适应稳定性检测
 * @return int 0表示成功，负值表示错误
 */
int device_detector_set_adaptive_stability(const struct device *dev, bool enabled);

/**
 * @brief 获取稳定性统计信息
 * 
 * @param dev 设备检测器设备
 * @param stats 稳定性统计信息结构指针
 * @return int 0表示成功，负值表示错误
 */
int device_detector_get_stability_stats(const struct device *dev, 
                                        struct stability_stats *stats);

/**
 * @brief 启动电压范围学习模式
 * 
 * @param dev 设备检测器设备
 * @param device_type 需要学习的设备类型 (1-5)
 * @return int 0表示成功，负值表示错误
 */
int device_detector_start_learning(const struct device *dev, uint8_t device_type);

/**
 * @brief 停止电压范围学习模式并保存结果
 * 
 * @param dev 设备检测器设备
 * @return int 0表示成功，负值表示错误
 */
int device_detector_finish_learning(const struct device *dev);

/**
 * @brief 获取学习模式的电压范围状态
 * 
 * @param dev 设备检测器设备
 * @param min_mv 学习到的最小电压
 * @param max_mv 学习到的最大电压
 * @param samples 收集的样本数量
 * @return int 0表示成功，负值表示错误
 */
int device_detector_get_learning_status(const struct device *dev, 
                                      int32_t *min_mv, int32_t *max_mv, 
                                      uint32_t *samples);

/**
 * @brief 注册设备类型变化回调函数
 * 
 * @param dev 设备检测器设备
 * @param callback 回调函数
 * @param user_data
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