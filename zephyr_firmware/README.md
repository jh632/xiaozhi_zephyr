# xiaozhi_zephyr 固件工程

立创·实战派 ESP32-C3 开发板 V1.3 的 Zephyr 工程。

- 主控: ESP32-C3（QFN32 裸片 + 40MHz 晶振）
- 外部 Flash: MX25L6433FM2I-08G，8MB SPI NOR
- 控制台: UART0（GPIO21 TX / GPIO20 RX）经 CH343P 转 USB Type-C
- 原理图整理结果: [doc/SCH_ESP32-C3-V1_3.md](doc/SCH_ESP32-C3-V1_3.md)

## 板级配置

基础板为 `esp32c3_devkitc`，本板差异全部放在 [boards/lichuangc3.overlay](boards/lichuangc3.overlay)：

| 项目 | 本板 | devkitc 默认值 | 覆盖动作 |
|---|---|---|---|
| I2C0 | SDA=GPIO0, SCL=GPIO1 | SDA=GPIO1, SCL=GPIO3 | 覆盖 pinctrl |
| USB Serial/JTAG | 未接（GPIO18/19 作 IO18/IO19） | 打开 | 关闭 |
| SPI2 | TFT 用 GPIO3/4/5/6 | GPIO2/6/7/10 | 关闭总线 |
| 外部 Flash | 8MB | 4MB | 覆盖 flash0 容量 |
| BOOT 按键 | GPIO9 | GPIO9（sw0） | 无需修改 |

分区表沿用 Zephyr 的 4MB 默认布局（`partitions_0x0_default_4M.dtsi`），
若要用满 8MB，需要另外提供分区表。

## 编译与烧录

先激活 west 环境（提供 `west`、`esptool`、`pyserial`）：

```bash
cd /home/jasper/zephyrproject
source .venv/bin/activate
```

编译：

```bash
cd userproject/xiaozhi_zephyr/zephyr_firmware
west build -b esp32c3_devkitc .
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

`src/main.c` 打印板级信息并扫描 I2C 总线，用于核对引脚分配与硬件是否一致。
I2C 总线上应能看到：

| 地址 | 器件 |
|---|---|
| 0x0D | QMC5883L 地磁传感器 |
| 0x18 | ES8311 音频编解码器 |
| 0x38 | FT6X36 触摸屏控制器（接上触摸屏排线时） |
| 0x6A | QMI8658C 六轴姿态传感器 |
| 0x70 | GXHTC3 温湿度传感器 |

Zephyr 主线目前只有 QMI8658A 的驱动，QMC5883L、GXHTC3、FT6X36、ES8311
需要自行实现驱动后才能作为设备接入设备树。

## 硬件约束

- 外部 Flash 由 3V3 供电，GPIO11/12/13 被复用为 I2S 与功放使能，
  因此 Flash 只能工作在单线或双线模式，烧录参数为 `--esp-flash-mode dio`（默认值）。
- GPIO9 是启动模式选择脚，BOOT 按键按下时拉低，复位后进入下载模式。
- 芯片的 USB（GPIO18/19）未接到 Type-C，串口日志只能从 CH343P 侧读取。
