# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# 复用应用目录 dts/bindings 下的 ES8311 设备树绑定
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(es8311_test)

target_sources(app PRIVATE
	src/test_es8311.c
	src/es8311_emul.c
	../../drivers/audio/es8311.c
)

target_include_directories(app PRIVATE src)
