#!/bin/bash
# ZMK设备检测器调试构建脚本

set -e

# 设置参数
BOARD=${1:-nrf52840dk_nrf52840}
DOCKER_IMAGE="zmkfirmware/zmk-build-arm:3.5"

echo "===== 使用Docker构建ZMK设备检测器(调试版) ====="
echo "开发板: $BOARD"
echo "Docker镜像: $DOCKER_IMAGE"

# 创建调试版本配置文件
mkdir -p app/debug_conf
cat > app/debug_conf/prj_debug.conf << EOF
# 基本配置
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=4
CONFIG_ZMK_DEVICE_DETECTOR_LOG_LEVEL=4
CONFIG_EC11_LOG_LEVEL=4
CONFIG_ANALOG_INPUT_LOG_LEVEL=4
CONFIG_PMW3610_LOG_LEVEL=4

# ADC配置
CONFIG_ADC=y

# 启用设备检测器
CONFIG_ZMK_DEVICE_DETECTOR=y

# 启用编码器
CONFIG_EC11=y
CONFIG_EC11_TRIGGER_GLOBAL_THREAD=y

# 启用模拟输入(摇杆)
CONFIG_ANALOG_INPUT=y
CONFIG_ANALOG_INPUT_LOG_DBG_RAW=y
CONFIG_ANALOG_INPUT_LOG_DBG_REPORT=y

# 启用PMW3610轨迹球
CONFIG_PMW3610=y
CONFIG_PMW3610_LOG_DBG_RAW=y

# 启用输入子系统
CONFIG_INPUT=y

# SEGGER RTT日志输出 (仅针对nRF开发板)
CONFIG_USE_SEGGER_RTT=y
CONFIG_UART_CONSOLE=n
CONFIG_RTT_CONSOLE=y
EOF

# 使用Docker构建项目
echo "启动Docker调试构建..."
docker run --rm -it \
  -v "$(pwd):/workdir" \
  -w /workdir \
  $DOCKER_IMAGE \
  /bin/bash -c "cd /workdir && \
  west init -l . && \
  west update --fetch-opt=--depth=1 && \
  west zephyr-export && \
  west build -p -b $BOARD app -- -DCONF_FILE=debug_conf/prj_debug.conf"

if [ $? -eq 0 ]; then
  echo "===== 调试构建成功 ====="
  echo "生成的固件位于: build/zephyr/zephyr.hex"
  echo "生成的ELF文件位于: build/zephyr/zephyr.elf"
  
  if [[ "$BOARD" == *"nrf"* ]]; then
    echo ""
    echo "可使用J-Link RTT Viewer连接开发板查看调试日志"
    echo "J-Link RTT Viewer 配置:"
    echo "  - Target Device: nRF52840_xxAA (或对应芯片)"
    echo "  - Target Interface: SWD"
    echo "  - Speed: 4000 kHz"
    echo "  - RTT Control Block: Auto Detection"
  fi
else
  echo "===== 构建失败 ====="
  exit 1
fi 