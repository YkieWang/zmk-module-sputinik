zephyr_library()

zephyr_library_sources_ifdef(CONFIG_DEVICE_DETECTOR src/device_detector.c)
zephyr_include_directories(include)

zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
