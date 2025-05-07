# 设备检测器测试应用

这是一个简单的测试应用，用于演示和测试ZMK设备检测器的功能。

## 功能

- 实时显示检测到的设备类型和电压值
- 定期输出诊断信息
- 包含设备类型变化回调示例
- 提供shell命令界面，用于交互式控制和诊断

## 使用方法

### 构建和运行

1. 确保您已经设置好ZMK开发环境
2. 使用以下命令构建并烧录：

```bash
west build -b <your_board> zmk-device-detector/samples/detector_test
west flash
```

### 查看输出

连接到设备的串口控制台以查看输出信息：

```bash
minicom -D /dev/ttyACM0 -b 115200
```

或使用其他串口工具，如PuTTY、screen等。

### Shell命令

该应用支持以下shell命令：

```
device_detector get_type         # 获取当前设备类型
device_detector get_voltage      # 获取当前电压
device_detector diagnostics      # 显示诊断信息
device_detector reset_diagnostics # 重置诊断计数器
device_detector force_type <type> # 强制设定设备类型
```

如果启用了自适应稳定性检测：
```
device_detector set_adaptive_stability <on|off> # 启用/禁用自适应稳定性
device_detector get_stability_stats            # 获取稳定性统计
```

如果启用了电压范围学习功能：
```
device_detector start_learning <type>  # 开始学习某种设备类型的电压范围
device_detector finish_learning        # 完成学习并保存结果
device_detector learning_status        # 查看学习进度
```

## 自定义

修改`prj.conf`可以启用或禁用不同功能：

```
# 启用自适应稳定性检测
CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY=y

# 启用电压范围学习功能
CONFIG_DEVICE_DETECTOR_VOLTAGE_LEARNING=y

# 启用shell命令
CONFIG_DEVICE_DETECTOR_SHELL=y
CONFIG_SHELL=y
```

## 故障排除

如果测试不正常工作，请检查以下问题：

1. 确认ADC引脚连接正确
2. 验证设备树中配置的ADC通道正确
3. 检查电压范围配置是否匹配您的硬件
4. 增加日志级别以获取更详细的调试信息：

```
CONFIG_DEVICE_DETECTOR_LOG_LEVEL=4
CONFIG_LOG_DEFAULT_LEVEL=4
``` 