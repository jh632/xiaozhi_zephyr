# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# boards/ 下保留了一份示例板级定义, 加入搜索路径才能用 west build -b esp32c3_example 构建
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lc_esp32c3)

if(CONFIG_AUDIO_CODEC_ES8311)
	target_sources(app PRIVATE drivers/audio/es8311.c)
endif()

target_sources(app PRIVATE src/main.c)
