import sys
import time

import serial

from find_port import find_esp_port

BAUD_RATE = 115200
READ_TIMEOUT_S = 0.2
CHUNK_SIZE = 4096


def main():
	# 不给串口参数时同样逐个探测, 与 find_port.py 行为一致
	port = sys.argv[1] if len(sys.argv) > 1 else find_esp_port()
	duration_s = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
	deadline = time.monotonic() + duration_s

	if port is None:
		print("错误: 没有探测到 ESP 芯片, 请把串口设备作为第一个参数传入", file=sys.stderr)
		return 1

	with serial.Serial(port, BAUD_RATE, timeout=READ_TIMEOUT_S) as ser:
		# 板上 Q1 为交叉耦合自动下载电路: RTS 经三极管拉低 EN(RESET),
		# DTR 经三极管拉低 GPIO9(下载模式)。这里只复位复位引脚:
		# DTR 保持不拉低(GPIO9 由 R2 上拉到高), RTS 拉低再释放触发复位,
		# 与 esptool 的经典复位时序一致, 芯片复位后从 Flash 正常启动。
		ser.setDTR(False)
		ser.setRTS(True)
		time.sleep(0.2)
		ser.setRTS(False)

		while time.monotonic() < deadline:
			chunk = ser.read(CHUNK_SIZE)

			if chunk:
				sys.stdout.buffer.write(chunk)
				sys.stdout.buffer.flush()


if __name__ == "__main__":
	sys.exit(main())
