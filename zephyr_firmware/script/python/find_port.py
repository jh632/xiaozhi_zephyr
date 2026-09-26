import sys
import time

import serial
from serial import SerialException
from serial.tools import list_ports

BAUD_RATE = 115200
PROBE_S = 1.0


def reset_target(ser):
	"""按 USB 串口自动复位电路的约定触发一次复位。

	这类电路用 RTS 控制复位引脚、DTR 控制启动模式引脚, 这里只动 RTS,
	DTR 保持不拉低, 芯片复位后从 Flash 正常启动。
	"""
	ser.setDTR(False)
	ser.setRTS(True)
	time.sleep(0.2)
	ser.setRTS(False)


def find_port():
	"""返回第一个复位后有控制台输出的 USB 串口名, 全部没有输出时返回 None"""
	candidates = [info for info in list_ports.comports() if info.vid is not None]

	if not candidates:
		print("错误: 没有发现 USB 串口设备", file=sys.stderr)
		return None

	if len(candidates) == 1:
		print(f"只有一个候选串口 {candidates[0].device}", file=sys.stderr)
		return candidates[0].device

	for info in candidates:
		print(f"探测 {info.device} ({info.description})", file=sys.stderr, end=" ... ", flush=True)

		try:
			with serial.Serial(info.device, BAUD_RATE, timeout=0.2) as ser:
				reset_target(ser)

				deadline = time.monotonic() + PROBE_S
				while time.monotonic() < deadline:
					if ser.read(4096):
						break
				else:
					print("没有输出", file=sys.stderr)
					continue
		except (SerialException, OSError) as err:
			print(f"打开失败 ({type(err).__name__})", file=sys.stderr)
			continue

		print("有输出", file=sys.stderr)
		return info.device

	print("错误: 所有 USB 串口复位后都没有输出, 请把串口设备作为参数传入", file=sys.stderr)
	return None


def main():
	port = find_port()

	if port is None:
		return 1

	print(port)
	return 0


if __name__ == "__main__":
	sys.exit(main())
