# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# 复用应用目录下的板级定义 boards/ 与编解码器的设备树绑定 dts/bindings
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# 板级接线由所用板子的设备树提供, 示例只依赖 i2s 与 audio_codec 两个节点
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
