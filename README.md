# xiaozhi_zephyr

在 Zephyr 上实现小智 AI 聊天机器人的固件工程，目标硬件是立创·实战派 ESP32-C3 开发板 V1.3。

上游参考项目是 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)，那个工程基于 ESP-IDF 实现。
本工程把同样的功能放到 Zephyr 上重新实现，板级差异集中在设备树 overlay 里，不修改 Zephyr 主线代码，
因此可以直接跟随 Zephyr 主线升级。

## 当前进度

固件目前是板级自检程序（`zephyr_firmware/src/main.c`）：打印板级信息、扫描 I2C 总线并列出所有应答的
从设备地址、每 10 秒输出一次运行时间。它的用途是核对原理图整理出的引脚分配与真实硬件是否一致。

小智的语音交互功能还没有开始实现，音频采集与播放、唤醒词、通信协议对接、MCP 协议都处于未开发状态。

## 硬件概况

| 项目 | 参数 |
|---|---|
| 主控 | ESP32-C3，QFN32 封装，外部 40MHz 无源晶振 |
| 外部 Flash | MX25L6433FM2I-08G，8MB SPI NOR |
| 控制台 | UART0，GPIO21 TX / GPIO20 RX，经 CH343P 转 USB Type-C |
| 音频 | ES8311 编解码器（I2C 地址 0x18）、NS4150B 功放（GPIO13 使能）、ZTS6216 模拟麦克风 |
| 显示 | TFT 走 SPI2，SCK=GPIO3、CS=GPIO4、MOSI=GPIO5、DC=GPIO6，背光 GPIO2 |
| 触摸 | FT6X36，I2C 地址 0x38 |
| 传感器 | QMI8658C 六轴 0x6A、QMC5883L 地磁 0x0D、GXHTC3 温湿度 0x70 |
| 按键 | BOOT=GPIO9（低有效，复位后拉低进入下载模式）、RESET |

I2C0 使用 GPIO0 作 SDA、GPIO1 作 SCL，板上 R4/R5 已经有 4.7kΩ 上拉到 3V3。
I2S 使用 GPIO7/8/10/11/12，其中 GPIO11/12/13 原本是外部 Flash 的 VDD_SPI、SPIHD、SPIWP，
因为 Flash 由 3V3 供电而被复用，所以 Flash 只能工作在单线或双线模式，不能使用 QIO/QPI。

完整的引脚与网络对照见 [zephyr_firmware/doc/SCH_ESP32-C3-V1_3.md](zephyr_firmware/doc/SCH_ESP32-C3-V1_3.md)，
原始原理图 PDF 是 [zephyr_firmware/doc/SCH_ESP32-C3-V1_3_2026-09-17.pdf](zephyr_firmware/doc/SCH_ESP32-C3-V1_3_2026-09-17.pdf)。

## 目录结构

| 路径 | 说明 |
|---|---|
| `zephyr_firmware/src/main.c` | 板级自检固件，I2C 扫描与心跳输出 |
| `zephyr_firmware/boards/lichuangc3.overlay` | 板级覆盖配置，本板与 devkitc 的全部差异 |
| `zephyr_firmware/prj.conf` | Kconfig 配置，打开日志、控制台、GPIO、INPUT、I2C |
| `zephyr_firmware/CMakeLists.txt` | Zephyr 应用构建入口 |
| `zephyr_firmware/doc/` | 原理图整理结果与原始 PDF |
| `zephyr_firmware/script/flash_and_log.sh` | 烧录并抓取串口输出 |
| `zephyr_firmware/script/python/` | 串口自动探测与采集脚本 |
| `zephyr_firmware/README.md` | 固件侧的详细说明，包含板级差异对照表 |

## 开发环境

| 项目 | 版本 |
|---|---|
| Zephyr | 主线开发版本 `v4.4.0-13771-gb9df9f46ae4` |
| Zephyr SDK | 1.0.1，使用 `riscv64-zephyr-elf` 交叉编译器 |
| west | 1.5.0 |
| esptool | 5.3.1 |
| pyserial | 3.5 |
| Python | 3.14.4 |

`west`、`esptool`、`pyserial` 装在 west 工作区根目录的 `.venv` 虚拟环境里，编译和烧录之前需要先激活它。
工程以 Zephyr application 的形式放在 west 工作区内，本身不携带 Zephyr 源码，
编译时通过 `ZEPHYR_BASE` 指向工作区中的 Zephyr。

## 编译

```bash
# 在 west 工作区根目录激活环境
cd /path/to/zephyrproject
source .venv/bin/activate

# 进入固件目录编译
cd userproject/xiaozhi_zephyr/zephyr_firmware
west build -b esp32c3_devkitc .
```

基础板是 `esp32c3_devkitc`，本板的差异由 `zephyr_firmware/CMakeLists.txt` 里的 `EXTRA_DTC_OVERLAY_FILE`
引入 `zephyr_firmware/boards/lichuangc3.overlay`，不需要自定义 board 目录。

## 烧录与串口日志

```bash
script/flash_and_log.sh                     # 抓取 20 秒，串口自动探测
script/flash_and_log.sh 30                  # 抓取 30 秒
script/flash_and_log.sh 30 /dev/ttyACM0     # 指定串口设备
```

脚本先调用 `west flash` 烧录，然后复位芯片并抓取串口输出，日志写入 `log/console_<时间戳>.log`，
同时打印到终端。不指定串口时脚本会依次探测各 USB 串口，取第一个能识别出 ESP 芯片的端口。

板载 CH343P 的节点名取决于内核把设备绑定到哪个驱动，可能是 `/dev/ttyACM0`，也可能是 `/dev/ttyUSB0`，
所以脚本不猜测名称。运行脚本的用户需要在 `dialout` 组内才能访问串口设备。

## 板级适配要点

本板与 `esp32c3_devkitc` 的差异全部写在 `zephyr_firmware/boards/lichuangc3.overlay` 里：

| 项目 | 本板 | devkitc 默认值 | 处理方式 |
|---|---|---|---|
| I2C0 | SDA=GPIO0，SCL=GPIO1 | SDA=GPIO1，SCL=GPIO3 | 覆盖 pinctrl |
| USB Serial/JTAG | 未接，GPIO18/19 用作外部接口 J2 | 打开 | 关闭外设 |
| SPI2 | TFT 使用 GPIO3/4/5/6 | GPIO2/6/7/10 | 关闭总线 |
| 外部 Flash | 8MB | 4MB | 覆盖 flash0 容量 |
| BOOT 按键 | GPIO9 | GPIO9（sw0） | 沿用板级定义 |

分区表沿用 Zephyr 的 4MB 默认布局（`partitions_0x0_default_4M.dtsi`），要用满 8MB 需要另行提供分区表。

## 许可

工程采用 Apache-2.0 许可，正文见 [LICENSE](LICENSE)，与 Zephyr 保持一致，源文件头部带有 SPDX 标识。
`zephyr_firmware/doc/` 下的原理图 PDF 及其整理结果来自立创开发板的公开资料，版权归原作者所有，
不适用上述许可。
