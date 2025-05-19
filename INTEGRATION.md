# ZMK设备检测器集成指南

本文档介绍如何将ZMK设备检测器模块集成到ZMK主项目中。

## 作为独立模块使用

最简单的方法是将此项目作为西部模块添加到您的ZMK设置中。

### 1. 编辑west.yml文件

编辑您的`west.yml`文件，在`manifest/projects`部分添加以下内容：

```yaml
- name: zmk-device-detector
  url: https://github.com/yourusername/zmk-device-detector
  revision: main
```

### 2. 更新西部工作区

```bash
west update
```

### 3. 在键盘配置中启用设备检测器

编辑您的`<keyboard>.conf`文件，添加：

```
# 启用设备检测器
CONFIG_ZMK_DEVICE_DETECTOR=y
```

### 4. 添加设备树配置

在您的键盘的`.overlay`文件中，添加设备检测器节点：

```dts
/ {
    device_detector: device_detector {
        compatible = "zmk,device-detector";
        status = "okay";
        io-channels = <&adc 0>; // 使用ADC通道0检测设备
        adc-threshold-tolerance = <50>; // 阈值容差(毫伏)
        adc-thresholds = <0 500 1000 1500>; // 各设备的ADC阈值(毫伏)
        poll-interval-ms = <1000>; // 轮询间隔(毫秒)
    };
};
```

## 集成到ZMK主项目

如果您想将此模块作为ZMK主项目的一部分集成，请按照以下步骤操作：

### 1. 复制源文件

将以下目录复制到ZMK项目中的相应位置：

- `include/zmk-device-detector` → `zmk/app/include/zmk-device-detector`
- `src/` → `zmk/app/drivers/input/`
- `dts/bindings/` → `zmk/app/dts/bindings/`

### 2. 更新CMakeLists.txt

编辑`zmk/app/drivers/input/CMakeLists.txt`，添加：

```cmake
# 设备检测器
zephyr_library_sources_ifdef(CONFIG_ZMK_DEVICE_DETECTOR device-detector.c)
# EC11编码器
zephyr_library_sources_ifdef(CONFIG_EC11 ec11/ec11.c)
zephyr_library_sources_ifdef(CONFIG_EC11_TRIGGER ec11/ec11_trigger.c)
# PMW3610轨迹球
zephyr_library_sources_ifdef(CONFIG_PMW3610 pmw3610/pmw3610.c)
# 模拟输入(摇杆)
zephyr_library_sources_ifdef(CONFIG_ANALOG_INPUT analog-input/analog_input.c)
```

### 3. 添加Kconfig

将`zephyr/Kconfig`文件的内容添加到`zmk/app/drivers/input/Kconfig`中。

### 4. 更新ZMK核心以支持设备检测器

编辑相关的ZMK文件以添加对设备检测器的支持，例如在初始化过程中添加设备检测和处理。

## 模块化设计的优势

保持此项目作为独立模块有以下优势：

1. **独立开发**：可以独立于ZMK主项目进行开发和测试
2. **易于集成**：用户只需添加west模块即可使用
3. **灵活性**：可以轻松更新或替换，而无需修改ZMK核心代码
4. **可扩展性**：可以轻松添加对新设备类型的支持

## 硬件设计注意事项

为了正确使用设备检测器，您需要在键盘PCB上设计一个电阻分压网络：

1. 设计一个具有多个电阻值的网络，使每种设备类型产生唯一的电压值
2. 在设备连接器上添加电阻，使其连接时能被ADC读取
3. 确保电压范围在ADC能够检测的范围内（通常是0-3.3V）

## 调试提示

在集成过程中遇到问题时：

1. 增加日志级别：`CONFIG_ZMK_DEVICE_DETECTOR_LOG_LEVEL=4`
2. 使用万用表测量各设备的实际电压值，确保它们与配置的阈值匹配
3. 在设备连接/断开时使用逻辑分析仪监视I2C/SPI通信 