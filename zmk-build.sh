#!/bin/bash
# 使用官方ZMK镜像编译设备检测器

set -e

# 设置参数
BOARD=${1:-nrf52840dk_nrf52840}
BUILD_DIR=${2:-build}
DOCKER_IMAGE="zmkfirmware/zmk-build-arm:3.5"
TMPDIR="$(mktemp -d)"

# 清理函数
cleanup() {
  echo "清理临时目录..."
  rm -rf "$TMPDIR"
}

# 设置退出时自动清理
trap cleanup EXIT

echo "===== 使用Docker编译ZMK设备检测器 ====="
echo "开发板: $BOARD"
echo "构建目录: $BUILD_DIR"
echo "Docker镜像: $DOCKER_IMAGE"
echo "临时目录: $TMPDIR"

# 检查Docker是否可用
if ! docker info &> /dev/null; then
  echo "错误: Docker未运行。请确保Docker服务已启动。"
  exit 1
fi

# 准备构建环境
echo "准备构建环境..."
mkdir -p "$TMPDIR/app"

# 复制源文件到临时目录
echo "复制项目文件到临时目录..."
cp -r CMakeLists.txt Kconfig Kconfig.module README.md app dts include src west.yml "$TMPDIR/"

# 使用Docker构建项目
echo "启动Docker构建..."
docker run --rm -it \
  -v "$TMPDIR:/workdir" \
  -v "$(pwd)/$BUILD_DIR:/builddir" \
  -w /workdir \
  $DOCKER_IMAGE \
  /bin/bash -c "
    # 初始化west工作区
    echo '初始化临时west工作区...'
    west init --manifest-url https://github.com/zmkfirmware/zmk --manifest-rev main .
    
    # 更新必要的模块
    echo '更新west模块...'
    west update --narrow -o=--depth=1 zephyr

    # 导出Zephyr环境
    echo '导出Zephyr环境...'
    eval \"\$(west zephyr-export)\"
    source zephyr/zephyr-env.sh

    # 构建应用程序
    echo '构建应用程序...'
    west build -p -b $BOARD -d /builddir app
  "

# 检查构建结果
if [ -f "$(pwd)/$BUILD_DIR/zephyr/zephyr.hex" ]; then
  echo "===== 构建成功 ====="
  echo "生成的固件位于: $BUILD_DIR/zephyr/zephyr.hex"
  echo "生成的ELF文件位于: $BUILD_DIR/zephyr/zephyr.elf"
else
  echo "===== 构建失败 ====="
  exit 1
fi 