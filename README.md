# ZMK设备检测器

通过ADC电阻分压网络识别不同类型的输入设备（轨迹球、摇杆、旋钮），并自动初始化相应的驱动。

## 功能特性

- 设备检测器：通过ADC读取电阻分压网络值识别设备类型
- 设备状态变更通知机制
- 支持的设备驱动：
  - PMW3610轨迹球驱动
  - 模拟输入(摇杆)驱动
  - EC11旋钮编码器驱动

## 编译与测试

### 作为Zephyr模块安装

1. 在您的Zephyr项目中，编辑`west.yml`文件，添加以下内容：

```yaml
manifest:
  projects:
    - name: zmk-device-detector
      url: https://github.com/yourusername/zmk-device-detector
      revision: main
```

2. 更新西部工作区：

```bash
west update
```

### 编译测试应用程序

1. 进入项目目录：

```bash
cd zmk-device-detector
```

2. 编译测试应用程序：

```bash
# 使用nRF52840 DK开发板
west build -p -b nrf52840dk_nrf52840 app

# 闪存到开发板
west flash
```

### 调试

1. 使用调试器连接：

```bash
# 启动GDB服务器
west debug --tui
```

2. 日志监控：

```bash
# 使用minicom或GNU Screen查看串口输出
minicom -D /dev/ttyACM0
# 或
screen /dev/ttyACM0 115200
```

## 开发和调试提示

1. 增加日志级别以获取更多调试信息：

```
CONFIG_LOG_DEFAULT_LEVEL=4
CONFIG_ZMK_DEVICE_DETECTOR_LOG_LEVEL=4
```

2. 使用RTT查看器获取更快的日志输出：

```
CONFIG_LOG_BACKEND_RTT=y
CONFIG_LOG_BACKEND_UART=n
```

3. 使用分析器测量ADC值：

```bash
# 在测试时打印原始ADC值
CONFIG_ANALOG_INPUT_LOG_DBG_RAW=y
```

## 许可

MIT
