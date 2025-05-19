FROM zmkfirmware/zmk-dev-arm:3.5

# 设置工作目录
WORKDIR /workdir

# 安装额外的工具
RUN apt-get update && apt-get install -y \
    vim \
    minicom \
    usbutils \
    && rm -rf /var/lib/apt/lists/*

# 预设环境变量
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk
ENV ZEPHYR_BASE=/workdir/zephyr

# 设置入口点为bash
ENTRYPOINT ["/bin/bash"] 