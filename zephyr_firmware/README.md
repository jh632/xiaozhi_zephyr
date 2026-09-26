# xiaozhi_zephyr 固件工程

## 最小外设需求

运行本固件对硬件的最低要求:

- [x] 主控：能运行 Zephyr，最小400kbSram。
- [x] 联网：2.4 GHz Wi-Fi。
- [x] 语音采集：麦克风，16 kHz 单声道，经 I2S 输入。
- [x] 音频输出：功放与扬声器，16 kHz 单声道，经 I2S 输出。
- [x] 控制通道：编解码器走 I2C。
- [x] 交互按键。
- [x] Flash：4 MB 以上，分应用、资源、设置三区，双分区升级时应用分区占两份。
- [x] 串口。
- [ ] 显示屏（REQ_005_Service_display）。
- [ ] 状态指示灯。
- [ ] 触摸输入。
- [ ] 电池与充电管理。
- [ ] 其他传感器（注册MCP 工具）。

## 编译与烧录

开发环境：

| 项目 | 版本 |
|---|---|
| Zephyr | 主线开发版本 `v4.4.0-13771-gb9df9f46ae4` |
| Zephyr SDK | 1.0.1，交叉编译器按目标芯片选择 |
| west | 1.5.0 |
| esptool | 5.3.1，ESP 目标的烧录由 west 的 esp32 runner 调用 |
| pyserial | 3.5 |
| Python | 3.14.4 |

编译：

```bash
west build -b target_board .
```

烧录并抓取串口输出：

```bash
script/flash_and_log.sh                     # 抓 20 秒, 串口自动探测
script/flash_and_log.sh 30                  # 抓 30 秒
script/flash_and_log.sh 30 /dev/ttyACM0     # 指定串口
```

脚本先执行 `west flash`（烧录方式由目标板决定，串口之后的参数原样转给它，用于指定烧录器或调试器），再复位抓取串口输出，日志保存在 `log/console_<时间戳>.log`，同时打印到终端。

串口缺省时依次探测各 USB 串口：只有一个候选就用它，有多个候选时逐个复位并监听，取第一个有输出的端口。可以单独运行查看：

```bash
python3 script/python/find_port.py
```

只抓取不烧录时，单独运行 `python3 script/python/serial_capture.py [秒数] [串口]`。
