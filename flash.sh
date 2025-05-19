#!/bin/bash
# ZMK设备检测器固件刷写脚本

set -e

# 设置参数
BOARD=${1:-nrf52840dk_nrf52840}
DOCKER_IMAGE="zmkfirmware/zmk-build-arm:3.5"

if [ ! -f "build/zephyr/zephyr.hex" ]; then
  echo "错误: 找不到固件文件 build/zephyr/zephyr.hex"
  echo "请先运行构建脚本 ./build_debug.sh 或 ./zmk-build.sh"
  exit 1
fi

echo "===== 刷写ZMK设备检测器固件 ====="
echo "开发板: $BOARD"

# 检查开发板类型并选择合适的刷写方法
if [[ "$BOARD" == *"nrf"* ]]; then
  echo "检测到nRF开发板，使用nrfjprog刷写..."
  
  docker run --rm -it --privileged \
    -v "$(pwd):/workdir" \
    -v /dev:/dev \
    -w /workdir \
    $DOCKER_IMAGE \
    /bin/bash -c "nrfjprog --program build/zephyr/zephyr.hex --chiperase --reset"
    
elif [[ "$BOARD" == *"stm32"* ]]; then
  echo "检测到STM32开发板，使用STM32_Programmer_CLI刷写..."
  
  # 注意: 需要确认Docker镜像中有STM32 Programmer
  docker run --rm -it --privileged \
    -v "$(pwd):/workdir" \
    -v /dev:/dev \
    -w /workdir \
    $DOCKER_IMAGE \
    /bin/bash -c "STM32_Programmer_CLI --connect port=SWD --download build/zephyr/zephyr.hex --verify --reset"

elif [[ "$BOARD" == *"native_posix"* ]]; then
  echo "Native POSIX目标不需要刷写，直接运行..."
  
  docker run --rm -it \
    -v "$(pwd):/workdir" \
    -w /workdir \
    $DOCKER_IMAGE \
    /bin/bash -c "cd /workdir && ./build/zephyr/zephyr.exe"
  
  exit 0
else
  echo "使用west flash刷写通用目标..."
  
  docker run --rm -it --privileged \
    -v "$(pwd):/workdir" \
    -v /dev:/dev \
    -w /workdir \
    $DOCKER_IMAGE \
    /bin/bash -c "cd /workdir && west flash"
fi

if [ $? -eq 0 ]; then
  echo "===== 固件刷写成功 ====="
  
  if [[ "$BOARD" == *"nrf"* ]]; then
    echo ""
    echo "提示: 可使用J-Link RTT Viewer查看调试日志"
  fi
else
  echo "===== 固件刷写失败 ====="
  exit 1
fi 