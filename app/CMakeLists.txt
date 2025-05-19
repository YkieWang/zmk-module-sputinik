# Copyright (c) 2023 ZMK Contributors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

# 设置应用程序名称
set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(ZMK_APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zmk_device_detector_test)

# 包含头文件目录
target_include_directories(app PRIVATE ${CMAKE_SOURCE_DIR}/../include)

# 添加源文件
target_sources(app PRIVATE 
  src/main.c
) 