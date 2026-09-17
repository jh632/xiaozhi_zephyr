import contextlib
import sys

from esptool import FatalError, detect_chip
from serial import SerialException
from serial.tools import list_ports

BAUD_RATE = 115200
CONNECT_ATTEMPTS = 2


def find_esp_port():
	"""依次探测各 USB 串口, 返回第一个能识别出 ESP 芯片的端口名, 全部失败返回 None"""
	candidates = [info for info in list_ports.comports() if info.vid is not None]

	if not candidates:
		print("错误: 没有发现 USB 串口设备", file=sys.stderr)
		return None

	for info in candidates:
		print(f"探测 {info.device} ({info.description})", file=sys.stderr, end=" ... ", flush=True)

		try:
			# esptool 的进度信息走 stdout, 转到 stderr 以免混进本脚本的返回值
			with contextlib.redirect_stdout(sys.stderr):
				esp = detect_chip(info.device, BAUD_RATE, connect_attempts=CONNECT_ATTEMPTS)
		except (FatalError, SerialException, OSError) as err:
			print(f"未识别到 ESP 芯片 ({type(err).__name__})", file=sys.stderr)
			continue

		print(f"识别到 {esp.CHIP_NAME}", file=sys.stderr)

		# 探测会把芯片停在下载模式, 复位让它回到正常运行状态
		with contextlib.redirect_stdout(sys.stderr):
			esp.hard_reset()

		return info.device

	print("错误: 所有 USB 串口都没有识别到 ESP 芯片", file=sys.stderr)
	return None


def main():
	port = find_esp_port()

	if port is None:
		return 1

	print(port)
	return 0


if __name__ == "__main__":
	sys.exit(main())
