# xiaozhi_zephyr

在 Zephyr 上实现小智 AI 聊天机器人的固件工程，目标硬件是立创·实战派 ESP32-C3 开发板 V1.3。

上游参考项目是 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)，那个工程基于 ESP-IDF 实现。
本工程把同样的功能放到 Zephyr 上重新实现，板级差异集中在设备树 overlay 里，不修改 Zephyr 主线代码，
因此可以直接跟随 Zephyr 主线升级。工程以 Zephyr application 的形式放在 west 工作区内，本身不携带
Zephyr 源码，编译时通过 `ZEPHYR_BASE` 指向工作区中的 Zephyr，编译与烧录的具体步骤见
[zephyr_firmware/README.md](zephyr_firmware/README.md)。

## 当前进度

ES8311 音频编解码器驱动（[zephyr_firmware/drivers/audio/es8311.c](zephyr_firmware/drivers/audio/es8311.c)）
已经实现，应用侧通过 Zephyr 的 audio codec 接口配置时钟、格式、音量与静音，播放期间由驱动开关功放。
真板上已经用示例音频走通整条音频通路，驱动写出的寄存器行为另有 native_sim 单元测试覆盖。
把音频文件转成原始 PCM 后用扬声器播放的独立示例在
[zephyr_firmware/samples/playback](zephyr_firmware/samples/playback)。
唤醒词、通信协议对接、MCP 协议、小智的语音交互流程还未开始实现。

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

完整的引脚与网络对照见 [zephyr_firmware/doc/05-reference/hardware/SCH_ESP32-C3-V1_3.md](zephyr_firmware/doc/05-reference/hardware/SCH_ESP32-C3-V1_3.md)，
原始原理图 PDF 是 [zephyr_firmware/doc/05-reference/hardware/SCH_ESP32-C3-V1_3_2026-09-17.pdf](zephyr_firmware/doc/05-reference/hardware/SCH_ESP32-C3-V1_3_2026-09-17.pdf)。

## 板级配置

Zephyr 主线已经有这块板子的板定义 `esp32c3_lckfb`（`zephyr/boards/others/esp32c3_lckfb`），
工程直接使用它。本板与它的差异写在 `zephyr_firmware/boards/esp32c3_lckfb.overlay` 里，
构建时按板名自动应用：关闭官方定义里默认打开的 USB Serial/JTAG（本板 GPIO18/19 未接原生 USB、
用作外部接口 J2），补上 I2S 全双工缺的帧同步与播放数据脚，并增加 ES8311 与功放节点。

另外 `zephyr_firmware/boards/esp_board/esp32c3_example/` 保留了一份语音助手板的最小板级定义作为示例，
只描述项目需要的硬件（MCU + Wi-Fi、语音采集、音频输出、交互按键），
供以后为官方不支持的板子或自制板子编写定义时参考。

## 许可

工程采用 Apache-2.0 许可，正文见 [LICENSE](LICENSE)，与 Zephyr 保持一致，源文件头部带有 SPDX 标识。
`zephyr_firmware/doc/` 下的原理图 PDF 及其整理结果来自立创开发板的公开资料，版权归原作者所有，
不适用上述许可。
