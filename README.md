# ZMK 热插拔设备检测器

这是一个ZMK模块，用于检测热插拔设备。通过读取ADC输入的电压值，可以识别不同类型的热插拔设备，如旋钮、摇杆、轨迹球等。

## 功能特点

- 通过ADC读取设备的电压值
- 支持多种设备类型的检测
- 自适应稳定性检测，根据信号质量动态调整稳定性阈值
- 电压范围自学习功能，便于用户校准不同设备类型
- 支持自动层切换
- 支持事件回调机制
- 支持自定义回调函数
- 支持去抖动和稳定性处理
- 可配置的电压范围和采样频率
- 低功耗管理
- 诊断和调试功能
- 支持Shell命令行交互

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
CONFIG_DEVICE_DETECTOR_CALLBACK_EVENT=y
# 无回调
# CONFIG_DEVICE_DETECTOR_CALLBACK_NONE=y

# 层配置（如果启用了层切换回调）
CONFIG_DEVICE_DETECTOR_LAYER_KNOB=1
CONFIG_DEVICE_DETECTOR_LAYER_JOYSTICK=2
CONFIG_DEVICE_DETECTOR_LAYER_TRACKBALL=3
CONFIG_DEVICE_DETECTOR_LAYER_SLIDER=4
CONFIG_DEVICE_DETECTOR_LAYER_OTHER=5

# 启用自适应稳定性检测(推荐)
CONFIG_DEVICE_DETECTOR_ADAPTIVE_STABILITY=y

# 启用电压范围学习功能(可选)
CONFIG_DEVICE_DETECTOR_VOLTAGE_LEARNING=y

# 启用Shell命令支持(可选)
CONFIG_DEVICE_DETECTOR_SHELL=y
CONFIG_SHELL=y
```

## 工作原理

1. 该模块使用ADC读取连接到MCU的模拟信号
2. 根据读取的电压值判断连接的设备类型
3. 使用自适应稳定性检测算法确保检测结果可靠
4. 当检测到设备类型变化时，根据配置执行相应操作：
   - 切换到指定层
   - 发送事件
   - 调用自定义回调函数

### 自适应稳定性检测

自适应稳定性算法可以动态调整稳定性要求，根据信号质量自动优化检测灵敏度：

1. 当信号质量高（低噪声）时，降低稳定性阈值，提高响应速度
2. 当信号质量低（高噪声）时，提高稳定性要求，避免误触发
3. 自动计算信号方差和噪声水平，以判断信号质量
4. 提供诊断数据，帮助调试和优化

### 电压范围自学习

电压范围自学习功能允许用户校准不同设备类型的电压范围：

1. 启动学习模式，指定要学习的设备类型
2. 系统收集设备运行时的电压样本
3. 完成学习后，自动计算最佳电压范围（包含安全余量）
4. 保存学习结果，用于后续的设备检测

Shell命令示例：
```
device_detector start_learning knob
device_detector finish_learning
```

### 层切换机制

当启用层切换回调（`CONFIG_DEVICE_DETECTOR_CALLBACK_LAYER=y`）时，模块会自动管理与不同设备相关的层：

1. 当检测到新设备时，会自动激活与该设备对应的层
2. 当设备被移除或更换时，会自动停用旧设备的层，并激活新设备的层
3. 当没有设备连接时，会停用所有设备相关的层

层编号可以通过以下配置选项设置：
- `CONFIG_DEVICE_DETECTOR_LAYER_KNOB` - 旋钮设备的层
- `CONFIG_DEVICE_DETECTOR_LAYER_JOYSTICK` - 摇杆设备的层
- `CONFIG_DEVICE_DETECTOR_LAYER_TRACKBALL` - 轨迹球设备的层
- `CONFIG_DEVICE_DETECTOR_LAYER_SLIDER` - 滑块设备的层
- `CONFIG_DEVICE_DETECTOR_LAYER_OTHER` - 其他设备的层

### 事件回调机制

当启用事件回调（`CONFIG_DEVICE_DETECTOR_CALLBACK_EVENT=y`）时，模块会通过ZMK的输入事件系统发送事件：

1. 当设备类型变化时，会发送设备类型变化事件
2. 同时会发送当前的电压值信息

可以通过监听以下事件来获取设备变化信息：
- `ABS_MISC` - 表示设备类型（值为 `enum device_type` 的整数表示）
- `ABS_X` - 表示当前电压值（单位：毫伏）

### 自定义回调函数

除了层切换和事件回调外，您还可以注册自定义回调函数来响应设备类型变化：

```c
// 回调函数类型定义
typedef void (*device_type_changed_callback_t)(enum device_type type, void *user_data);

// 注册回调函数
int device_detector_register_callback(const struct device *dev, 
                                      device_type_changed_callback_t callback,
                                      void *user_data);
```

这使您可以实现更复杂的行为，如改变背光、修改键码映射等。

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

您可以通过设备树中的 `voltage-ranges` 属性自定义这些范围，或使用电压范围自学习功能动态校准。

## Shell命令

如果启用了Shell支持（`CONFIG_DEVICE_DETECTOR_SHELL=y`），您可以使用以下命令与设备检测器交互：

```
device_detector get_type         # 获取当前设备类型
device_detector get_voltage      # 获取当前电压
device_detector diagnostics      # 显示诊断信息
device_detector reset_diagnostics # 重置诊断计数器
device_detector force_type <type> # 强制设定设备类型
```

自适应稳定性相关命令：
```
device_detector set_adaptive_stability <on|off> # 启用/禁用自适应稳定性
device_detector get_stability_stats            # 获取稳定性统计
```

电压范围学习相关命令：
```
device_detector start_learning <type>  # 开始学习某种设备类型的电压范围
device_detector finish_learning        # 完成学习并保存结果
device_detector learning_status        # 查看学习进度
```

## 编程API

该模块提供了以下函数用于编程交互：

```c
// 获取当前检测到的设备类型
enum device_type device_detector_get_type(const struct device *dev);

// 获取当前电压值
int device_detector_get_voltage(const struct device *dev, int32_t *mv);

// 获取诊断信息
int device_detector_get_diagnostics(const struct device *dev, 
                                   struct device_detector_diagnostics *diag);

// 注册设备类型变化回调函数
int device_detector_register_callback(const struct device *dev, 
                                      device_type_changed_callback_t callback,
                                      void *user_data);

// 取消注册设备类型变化回调函数
int device_detector_unregister_callback(const struct device *dev, 
                                        device_type_changed_callback_t callback);

// 获取设备类型的字符串描述
const char *device_type_to_str(enum device_type type);

// 设置/获取自适应稳定性状态
int device_detector_set_adaptive_stability(const struct device *dev, bool enabled);
int device_detector_get_stability_stats(const struct device *dev, 
                                       struct stability_stats *stats);

// 电压范围学习功能
int device_detector_start_learning(const struct device *dev, uint8_t device_type);
int device_detector_finish_learning(const struct device *dev);
int device_detector_get_learning_status(const struct device *dev, 
                                     int32_t *min_mv, int32_t *max_mv, 
                                     uint32_t *samples);
```

## 示例应用

仓库提供了一个演示应用，位于`samples/detector_test/`目录，用于测试和演示设备检测器的功能。参见该目录中的README文件了解更多信息。

## 故障排除

如果设备检测器不工作，请检查以下几点：

1. 确保ADC设备在设备树中配置正确且状态为 "okay"
2. 确保您使用了正确的ADC通道
3. 检查电压范围是否与您的设备匹配
4. 使用Shell命令获取实时诊断信息
5. 启用调试日志查看ADC读数（CONFIG_DEVICE_DETECTOR_LOG_LEVEL=4）
6. 使用电压范围学习功能校准设备类型的电压范围

## 使用示例

### 自定义回调示例

以下是一个自定义回调函数的示例，当设备类型变化时修改RGB灯效：

```c
#include <zmk/drivers/device_detector.h>
#include <zmk/rgb_underglow.h>

// 设备变化回调函数
static void on_device_type_changed(enum device_type type, void *user_data) {
    // 根据设备类型修改RGB灯效
    switch (type) {
        case DEVICE_TYPE_KNOB:
            // 旋钮设备 - 蓝色
            zmk_rgb_underglow_set_hsb(240, 100, 100);
            break;
        case DEVICE_TYPE_JOYSTICK:
            // 摇杆设备 - 绿色
            zmk_rgb_underglow_set_hsb(120, 100, 100);
            break;
        case DEVICE_TYPE_TRACKBALL:
            // 轨迹球设备 - 红色
            zmk_rgb_underglow_set_hsb(0, 100, 100);
            break;
        case DEVICE_TYPE_SLIDER:
            // 滑块设备 - 紫色
            zmk_rgb_underglow_set_hsb(300, 100, 100);
            break;
        case DEVICE_TYPE_OTHER:
            // 其他设备 - 黄色
            zmk_rgb_underglow_set_hsb(60, 100, 100);
            break;
        case DEVICE_TYPE_NONE:
            // 无设备 - 白色
            zmk_rgb_underglow_set_hsb(0, 0, 80);
            break;
    }
}

// 在初始化代码中注册回调
const struct device *dev = DEVICE_DT_GET(DT_INST(0, zmk_device_detector));
device_detector_register_callback(dev, on_device_type_changed, NULL);
```