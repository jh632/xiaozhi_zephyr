# xiaozhi_zephyr 固件工程

## 板级配置

使用 Zephyr 主线的 `esp32c3_lckfb` 板定义（位于 `zephyr/boards/others/esp32c3_lckfb`），
本板与它的差异写在 [boards/esp32c3_lckfb.overlay](boards/esp32c3_lckfb.overlay) 里，
构建时按板名自动应用：

| 项目 | 本板 | 官方 esp32c3_lckfb | 处理方式 |
|---|---|---|---|
| USB Serial/JTAG | 未接（GPIO18/19 作 IO18/IO19） | 打开 | overlay 关闭 |
| I2C0 | SDA=GPIO0, SCL=GPIO1 | 相同 | 沿用 |
| SPI2 | TFT 用 GPIO3/4/5/6 | 相同 | 沿用 |
| 外部 Flash | 8MB | 8MB | 沿用 |
| BOOT 按键 | GPIO9 | GPIO9（sw0） | 沿用 |
| I2S 全双工 | WS=GPIO12, DO=GPIO11 | 只接了 MCLK/BCK/DI | overlay 补上缺的两个脚 |
| ES8311 与功放 | I2C 0x18, 功放使能 GPIO13 | 未描述 | overlay 增加 codec 与功放节点 |

I2S 与 DMA 在官方板定义里没有打开，overlay 里一并打开。

分区表是 Zephyr 的 8MB 默认布局（`partitions_0x0_default_8M.dtsi`）。

[boards/esp_board/esp32c3_example](boards/esp_board/esp32c3_example) 是一份语音助手板的最小板级定义示例，
从 `esp32c3_devkitc` 改写而来，只描述项目需要的硬件（MCU + Wi-Fi、语音采集、音频输出、交互按键），
引脚按本板连接，写新板子时可以照 [它的说明](boards/esp_board/esp32c3_example/README.md) 作为起点，
也可以用 `west build -b esp32c3_example .` 构建。

## 编译与烧录

开发环境：

| 项目 | 版本 |
|---|---|
| Zephyr | 主线开发版本 `v4.4.0-13771-gb9df9f46ae4` |
| Zephyr SDK | 1.0.1，使用 `riscv64-zephyr-elf` 交叉编译器 |
| west | 1.5.0 |
| esptool | 5.3.1 |
| pyserial | 3.5 |
| Python | 3.14.4 |

`west`、`esptool`、`pyserial` 装在 west 工作区根目录的 `.venv` 虚拟环境里，编译和烧录之前先激活它：

```bash
cd /home/jasper/zephyrproject
source .venv/bin/activate
```

编译：

```bash
cd userproject/xiaozhi_zephyr/zephyr_firmware
west build -b esp32c3_lckfb .
```

烧录并抓取串口输出：

```bash
script/flash_and_log.sh                     # 抓 20 秒, 串口自动探测
script/flash_and_log.sh 30                  # 抓 30 秒
script/flash_and_log.sh 30 /dev/ttyACM0     # 指定串口
```

脚本先烧录，再复位抓取串口输出，日志保存在 `log/console_<时间戳>.log`，同时打印到终端。

串口缺省时会依次探测各 USB 串口，取第一个能识别出 ESP 芯片的端口，可以单独运行查看：

```bash
python3 script/python/find_port.py
```

板载 CH343P 的节点名取决于内核绑定到哪个驱动（本机是 `cdc_acm`，节点为 `/dev/ttyACM0`；
也可能由 `ch343` 驱动绑定成 `/dev/ttyUSB*`），所以不做名称猜测。当前用户需要在
`dialout` 组内才能访问该设备。

## 当前固件功能

`src/main.c` 是一个最小的 Hello World，启动后向控制台打印一行板名，用来确认构建、
烧录与串口链路正常。音频的播放演示在 [samples/playback](samples/playback)，
驱动的自动化测试在 [tests/es8311](tests/es8311)。

板上挂在 I2C0 的器件：

| 地址 | 器件 |
|---|---|
| 0x0D | QMC5883L 地磁传感器 |
| 0x18 | ES8311 音频编解码器 |
| 0x38 | FT6X36 触摸屏控制器（接上触摸屏排线时） |
| 0x6A | QMI8658C 六轴姿态传感器 |
| 0x70 | GXHTC3 温湿度传感器 |

Zephyr 主线目前只有 QMI8658A 的驱动，QMC5883L、GXHTC3、FT6X36
需要自行实现驱动后才能作为设备接入设备树。

## 音频通路

采集与播放都在 I2S 上，编解码器 ES8311 夹在中间：模拟麦克风 ZTS6216 的
信号进 ES8311 的 ADC，转成 PCM 后从 ASDOUT 送到 MCU；播放方向相反，
MCU 送出的 PCM 经 ES8311 的 DAC 变成模拟信号，由 NS4150B 功放驱动扬声器。

| 环节 | 实现 |
|---|---|
| 数据面 | Zephyr I2S 接口（`i2s_read`/`i2s_write`/`i2s_trigger`），16 kHz / 16 位 / 两个槽位 |
| 控制面 | Zephyr audio codec 接口（`audio_codec_configure`/`set_property`/`start`/`stop`） |
| 驱动 | [drivers/audio/es8311.c](drivers/audio/es8311.c)，设备树绑定 [dts/bindings/audio/everest,es8311.yaml](dts/bindings/audio/everest,es8311.yaml) |
| 功放 | 设备树里的 `regulator-fixed` 节点，播放开始时由驱动使能、结束后关闭 |

驱动按 256 倍采样率配置时钟链，与 I2S 控制器输出的 MCLK 一致；音量以 dB 为单位，
`set_property` 直接映射到 DAC/ADC 的数字音量寄存器（0.5 dB 步进）。板级与应用侧的接线
见 [boards/esp32c3_lckfb.overlay](boards/esp32c3_lckfb.overlay)。

配置 I2S 时必须先配播放方向，再配采集方向。ESP32-C3 的收发共用同一组 WS/BCK，
驱动在播放方向已经配置的情况下会把采集方向当作从机，两个方向共用同一套帧同步；顺序反过来时
采集方向自行计数时钟，收回的每个样点都偏移 2 个时钟位，幅度降到四分之一且高 2 位取自
上一个样点，回采数据会变成幅度很大的杂波。

播放音频的独立示例在 [samples/playback](samples/playback)：把任意音频文件转成
16 kHz 单声道原始 PCM 再重新构建烧录，扬声器就会放出来，转换命令与音频长度的
上限见该示例的 README。

驱动逻辑的自动化测试在 [tests/es8311](tests/es8311)，用 I2C 模拟器代替真实器件，
在 native_sim 上运行：

```bash
west build -p always -b native_sim -d build-test tests/es8311
west build -d build-test -t run
```

## 硬件约束

- 外部 Flash 由 3V3 供电，GPIO11/12/13 被复用为 I2S 与功放使能，
  因此 Flash 只能工作在单线或双线模式，烧录参数为 `--esp-flash-mode dio`（默认值）。
- GPIO9 是启动模式选择脚，BOOT 按键按下时拉低，复位后进入下载模式。
- 芯片的 USB（GPIO18/19）未接到 Type-C，串口日志只能从 CH343P 侧读取。
- 功放使能脚 GPIO13 上没有下拉电阻，上电后由 `regulator-fixed` 驱动配置成输出低电平，
  在播放开始之前扬声器保持静默。
