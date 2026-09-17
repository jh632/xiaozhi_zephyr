#!/usr/bin/env bash
#
# 烧录固件, 然后抓取串口控制台输出, 同时显示在终端并保存到 log/ 目录。
#
# 使用前请自行激活 west 环境(提供 west、esptool 与 pyserial), 脚本不会修改环境变量。
#
# 用法: script/flash_and_log.sh [抓取秒数] [串口设备]
#   抓取秒数  默认 20 秒
#   串口设备  默认依次探测各 USB 串口, 取第一个能识别出 ESP 芯片的端口
#
# 例: script/flash_and_log.sh 30 /dev/ttyACM0

set -euo pipefail

CAPTURE_S="${1:-20}"
PORT="${2:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${PROJECT_DIR}/log"
LOG_FILE="${LOG_DIR}/console_$(date +%Y%m%d_%H%M%S).log"

if ! command -v west >/dev/null 2>&1; then
	echo "错误: 找不到 west 命令, 请先激活 west 环境" >&2
	exit 1
fi

if [[ -z "${PORT}" ]]; then
	echo "== 未指定串口, 依次探测 =="
	PORT="$(python3 "${SCRIPT_DIR}/python/find_port.py")" || {
		echo "错误: 没有探测到 ESP 芯片, 请把串口设备作为第二个参数传入" >&2
		exit 1
	}
fi

mkdir -p "${LOG_DIR}"

cd "${PROJECT_DIR}"

echo "== 烧录 ${PORT} =="
west flash --esp-device "${PORT}"

echo "== 抓取 ${PORT} 输出 ${CAPTURE_S} 秒, 保存到 ${LOG_FILE} =="
python3 "${SCRIPT_DIR}/python/serial_capture.py" "${PORT}" "${CAPTURE_S}" | tee "${LOG_FILE}"
