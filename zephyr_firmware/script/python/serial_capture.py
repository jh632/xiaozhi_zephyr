import sys
import time

import serial

from find_port import find_port, reset_target

BAUD_RATE = 115200
READ_TIMEOUT_S = 0.2
CHUNK_SIZE = 4096


def main():
	# 不给串口参数时同样逐个探测, 与 find_port.py 行为一致
	port = sys.argv[1] if len(sys.argv) > 1 else find_port()
	duration_s = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
	deadline = time.monotonic() + duration_s

	if port is None:
		print("错误: 没有探测到串口, 请把串口设备作为第一个参数传入", file=sys.stderr)
		return 1

	with serial.Serial(port, BAUD_RATE, timeout=READ_TIMEOUT_S) as ser:
		# 复位一次, 让日志从启动开始
		reset_target(ser)

		while time.monotonic() < deadline:
			chunk = ser.read(CHUNK_SIZE)

			if chunk:
				sys.stdout.buffer.write(chunk)
				sys.stdout.buffer.flush()


if __name__ == "__main__":
	sys.exit(main())
