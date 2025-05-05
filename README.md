# ZMK 热插拔设备检测器

这是一个ZMK模块，用于检测热插拔设备。通过读取ADC输入的电压值，可以识别不同类型的热插拔设备，如旋钮、摇杆、轨迹球等。

## 功能特点

- 通过ADC读取设备的电压值
- 支持多种设备类型的检测
- 支持自动层切换
- 支持自定义回调
- 支持去抖动和稳定性处理
- 可配置的电压范围和采样频率

## 安装方法

要使用此模块，您需要将其添加到您的ZMK配置中。

1. 在您的 `config/west.yml` 文件中添加此模块：

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: your-username
      url-base: https://github.com/your-username
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-device-detector
      remote: your-username
      revision: main
  self:
    path: config
```

2. 在您的 `board.overlay` 文件中添加设备树配置：

```dts
#include <dt-bindings/zmk/matrix_transform.h>

&adc {
    status = "okay";
};

/ {
    device_detector {
        compatible = "zmk,device-detector";
        status = "okay";
        io-channels = <&adc 2>;  // 使用ADC通道2
        sampling-hz = <2>;       // 每秒采样2次
        detection-delay-ms = <50>; // 识别延迟（去抖动）
        voltage-ranges = <500 1000 1000 1500 1500 2000 2000 2500 2500 3000>; // 电压范围定义
    };
};
```

3. 在您的 `.conf` 文件中启用模块：

```conf
# 启用ADC
CONFIG_ADC=y

# 启用设备检测器
CONFIG_DEVICE_DETECTOR=y

# 可选日志级别 (0=OFF, 1=ERROR, 2=WARNING, 3=INFO, 4=DEBUG)
CONFIG_DEVICE_DETECTOR_LOG_LEVEL=3

# 回调模式选择
# 层切换回调
CONFIG_DEVICE_DETECTOR_CALLBACK_LAYER=y
# 事件回调
# CONFIG_DEVICE_DETECTOR_CALLBACK_EVENT=y
# 无回调
# CONFIG_DEVICE_DETECTOR_CALLBACK_NONE=y

# 层配置（如果启用了层切换回调）
CONFIG_DEVICE_DETECTOR_LAYER_KNOB=1
CONFIG_DEVICE_DETECTOR_LAYER_JOYSTICK=2
CONFIG_DEVICE_DETECTOR_LAYER_TRACKBALL=3
CONFIG_DEVICE_DETECTOR_LAYER_SLIDER=4
CONFIG_DEVICE_DETECTOR_LAYER_OTHER=5
```

## 工作原理

1. 该模块使用ADC读取连接到MCU的模拟信号
2. 根据读取的电压值判断连接的设备类型
3. 当检测到设备类型变化时，根据配置执行相应操作：
   - 切换到指定层
   - 发送事件
   - 调用自定义回调函数

## 电压范围

默认电压范围配置如下（单位：毫伏）：

| 设备类型   | 最小电压 | 最大电压 |
|-----------|---------|---------|
| 无设备     | 0       | 500     |
| 旋钮       | 500     | 1000    |
| 摇杆       | 1000    | 1500    |
| 轨迹球     | 1500    | 2000    |
| 滑块       | 2000    | 2500    |
| 其他       | 2500    | 3000    |

您可以通过设备树中的 `voltage-ranges` 属性自定义这些范围。

## 编程API

该模块提供了以下函数用于编程交互：

```c
// 获取当前检测到的设备类型
enum device_type device_detector_get_type(const struct device *dev);

// 注册设备类型变化回调函数
int device_detector_register_callback(const struct device *dev, 
                                      device_type_changed_callback_t callback,
                                      void *user_data);

// 取消注册设备类型变化回调函数
int device_detector_unregister_callback(const struct device *dev, 
                                        device_type_changed_callback_t callback);

// 获取设备类型的字符串描述
const char *device_type_to_str(enum device_type type);
```

## 故障排除

如果设备检测器不工作，请检查以下几点：

1. 确保ADC设备在设备树中配置正确且状态为 "okay"
2. 确保您使用了正确的ADC通道
3. 检查电压范围是否与您的设备匹配
4. 启用调试日志查看ADC读数（CONFIG_DEVICE_DETECTOR_LOG_LEVEL=4）

## 许可证

此模块遵循MIT许可证。