# Copyright (c) 2023 ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library()

if(CONFIG_ZMK_DEVICE_DETECTOR)
  zephyr_library_sources(src/device-detector.c)
  zephyr_library_include_directories(include)
endif()

# EC11旋钮编码器驱动
if(CONFIG_EC11)
  zephyr_library_sources_ifdef(CONFIG_EC11 src/ec11/ec11.c)
  zephyr_library_sources_ifdef(CONFIG_EC11_TRIGGER src/ec11/ec11_trigger.c)
  zephyr_library_include_directories(include)
endif()

# PMW3610轨迹球驱动
if(CONFIG_PMW3610)
  zephyr_library_sources(
    src/pmw3610/pmw3610.c
  )
  zephyr_library_include_directories(include)
endif()

# 模拟输入(摇杆)驱动
if(CONFIG_ANALOG_INPUT)
  zephyr_library_sources(
    src/analog-input/analog_input.c
  )
  zephyr_library_include_directories(include)
endif()
