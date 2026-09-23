# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# 复用应用目录下的板级定义 boards/esp_board 与 ES8311 的设备树绑定 dts/bindings
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# 板级接线与应用共用一份 overlay, 引脚配置只保留一处
set(EXTRA_DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../boards/esp32c3_lckfb.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(playback)

target_sources(app PRIVATE
	src/main.c
	../../drivers/audio/es8311.c
)

# 待播放的原始 PCM 在构建时转成字节数组, 由 main.c 直接 include
generate_inc_file_for_target(
	app
	audio/clip.pcm
	${ZEPHYR_BINARY_DIR}/include/generated/clip.pcm.inc
)
