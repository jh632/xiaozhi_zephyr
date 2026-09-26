# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# 工程自带的板级定义放在 boards/ 下, 加入搜索路径后可以直接用 west build -b <板名> 构建
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiaozhi_zephyr)

if(CONFIG_AUDIO_CODEC_ES8311)
	target_sources(app PRIVATE drivers/audio/es8311.c)
endif()

target_sources(app PRIVATE src/main.c)
