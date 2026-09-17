# 立创·实战派 ESP32-C3 开发板 V1.3 原理图说明

来源: `doc/SCH_ESP32-C3-V1_3_2026-09-17.pdf`
图纸共 3 页:

| 图页 | 内容 |
|---|---|
| P1 | ESP32-C3 主控、8MB SPI Flash、天线、外部接口 J1/J2、复位/BOOT 按键、I2C 上拉 |
| P2 | USB Type-C、CH343P USB 转串口、自动下载电路、3.3V LDO、QMI8658C、QMC5883L |
| P3 | ES8311 音频编解码、NS4150B 功放、麦克风、喇叭、FT6X36 触摸、GXHTC3 温湿度、TFT 接口 |

## 1. ESP32-C3 主控（P1，U3）

芯片为裸片 ESP32-C3（QFN32，33 号焊盘为 GND 散热焊盘），外部 8MB SPI NOR Flash，
40MHz 无源晶振。引脚定义与 Espressif 数据手册 QFN32 一致。

### 1.1 引脚与网络对照

| 引脚 | 引脚名 | 网络 | 说明 |
|---|---|---|---|
| 1 | LNA_IN | — | 经天线匹配网络接板载陶瓷天线 ANT3216LL00R2400A |
| 2 | VDD3P3 | 3V3（经 L1） | 射频模拟电源，经 L1(2nH) 从 3V3 引入 |
| 3 | VDD3P3 | 3V3（经 L1） | 与 2 脚在芯片左侧短接后同接 L1 |
| 4 | XTAL_32K_P(GPIO0) | IO0_I2C_SDA | I2C 数据线，R4 4.7kΩ 上拉到 3V3 |
| 5 | XTAL_32K_N(GPIO1) | IO1_I2C_SCL | I2C 时钟线，R5 4.7kΩ 上拉到 3V3 |
| 6 | GPIO2 | IO2_LCD_BL | TFT 背光控制（P3 经 Q2 驱动背光） |
| 7 | CHIP_EN | RESET | 复位网络，R7 10kΩ 上拉到 3V3、C12 100nF 到 GND、SW2 按键到 GND |
| 8 | GPIO3 | IO3_LCD_SCK | TFT SPI 时钟 |
| 9 | MTMS(GPIO4) | IO4_LCD_CS | TFT 片选 |
| 10 | MTDI(GPIO5) | IO5_LCD_MOSI | TFT SPI 数据 |
| 11 | VDD3P3_RTC | 3V3 | C11 100nF 去耦 |
| 12 | MTCK(GPIO6) | IO6_LCD_DC | TFT 数据/命令选择 |
| 13 | MTDO(GPIO7) | IO7_I2S_DI | I2S 数据输入（麦克风/编解码器输出） |
| 14 | GPIO8 | IO8_I2S_BCK | I2S 位时钟，R35 10kΩ 上拉到 3V3 |
| 15 | GPIO9 | IO9_BOOT | BOOT 按键（SW1 到 GND）、R2 10kΩ 上拉到 3V3，同时参与下载模式选择 |
| 16 | GPIO10 | IO10_I2S_MCK | I2S 主时钟 |
| 17 | VDD3P3_CPU | 3V3 | C13 100nF 去耦 |
| 18 | VDD_SPI(GPIO11) | IO11_I2S_DO | 外部 Flash 由 3V3 供电，故该脚改作 GPIO11 使用 |
| 19 | SPIHD(GPIO12) | IO12_I2S_WS | 未作 Flash HOLD，改作 I2S 帧同步 |
| 20 | SPIWP(GPIO13) | IO13_PA_EN | 未作 Flash WP，改作功放使能 |
| 21 | SPICS0(GPIO14) | SPI_CS0 | Flash 片选 |
| 22 | SPICLK(GPIO15) | SPI_CLK | Flash 时钟 |
| 23 | SPID(GPIO16) | SPI_ID | Flash 数据输入 |
| 24 | SPIQ(GPIO17) | SPI_IQ | Flash 数据输出 |
| 25 | GPIO18 | IO18 | 外部接口 J2 |
| 26 | GPIO19 | IO19 | 外部接口 J2 |
| 27 | U0RXD(GPIO20) | U0RXD | 经 499Ω 串联电阻接 CH343P TXD |
| 28 | U0TXD(GPIO21) | U0TXD | 经 499Ω 串联电阻接 CH343P RXD |
| 29 | XTAL_N | — | 40MHz 晶振 X1 一端，C8 13pF 到 GND |
| 30 | XTAL_P | — | 40MHz 晶振 X1 另一端，C7 13pF 到 GND |
| 31 | VDDA | 3V3 | C4 1uF、C9 100nF 去耦 |
| 32 | VDDA | 3V3 | 与 31 脚同网络 |
| 33 | GND(散热焊盘) | GND | |

Flash 通信只用到 SPICS0/SPICLK/SPID/SPIQ，Flash 的 WP#/HOLD# 不接芯片引脚，
所以 Flash 只能工作在单线或双线模式，不能使用 QIO/QPI。

### 1.2 8MB SPI Flash（P1，U2，MX25L6433FM2I-08G）

| U2 引脚 | 名称 | 连接 |
|---|---|---|
| 1 | /CS | SPI_CS0（ESP32-C3 GPIO14） |
| 2 | DO(IO1) | SPI_IQ（GPIO17） |
| 3 | /WP(IO2) | 经 R1 100kΩ 上拉到 3V3 |
| 4 | GND | GND |
| 5 | DI(IO0) | SPI_ID（GPIO16） |
| 6 | CLK | SPI_CLK（GPIO15） |
| 7 | /HOLD(IO3) | 经 R6 100kΩ 上拉到 3V3 |
| 8 | VCC | 3V3，C10 100nF 去耦 |

### 1.3 天线与时钟（P1）

- 板载陶瓷天线 ANT3216LL00R2400A（U1）。LNA_IN 经匹配网络接天线，其中 R3 为 0Ω
  串联电阻，C3、C6 为并联到地的预留电容（均为 NC，未贴装）。
- VDD3P3（2、3 脚）经 L1（2nH）从 3V3 供电，C1 10uF、C2 100nF、C5 100nF 为去耦电容。
- 40MHz 晶振 X1 接 XTAL_P/XTAL_N，负载电容 C7/C8 均为 13pF。
- XTAL_32K_P/XTAL_32K_N（GPIO0/GPIO1）没有接 32.768kHz 晶振，在板上作普通 GPIO
  使用，即 I2C 的 SDA/SCL。

## 2. 按键与外部接口（P1）

### 2.1 按键

| 位号 | 功能 | 连接 |
|---|---|---|
| SW1 | BOOT/用户按键 | 一端 IO9_BOOT（GPIO9），一端 GND；R2 10kΩ 上拉到 3V3 |
| SW2 | 复位按键 | 一端 RESET（CHIP_EN），一端 GND；R7 10kΩ 上拉到 3V3，C12 100nF 到 GND |

### 2.2 外部接口 J1「外部接口（I2C）」

连接器 HC-GH-5PWT，1.25mm 5 针。引脚号在图中自上而下为 5、4、3、2、1。

| 引脚 | 网络 | 说明 |
|---|---|---|
| 1 | IO1_I2C_SCL | 与板上 I2C SCL 同一网络 |
| 2 | IO0_I2C_SDA | 与板上 I2C SDA 同一网络 |
| 3 | GND | — |
| 4 | 3V3 | 经 NFM18PC104R1C3D 三端滤波器滤波后引出，C15/C19 为滤波电容 |
| 5 | VBUS | 经另一路 NFM18PC104R1C3D 滤波后引出，C14/C18 为滤波电容 |

信号线各有一只 ESD 保护二极管到 GND：D1 在 IO0_I2C_SDA 上，D3 在 IO1_I2C_SCL 上
（型号 RCLAMP0521T-ES）。连接器符号左侧还有两条接 GND 的连线，对应连接器的固定焊盘。

### 2.3 外部接口 J2「外部接口（多用）」

连接器 HC-GH-5PWT，引脚号自上而下为 5、4、3、2、1。

| 引脚 | 网络 | 说明 |
|---|---|---|
| 1 | IO19 | ESP32-C3 GPIO19 |
| 2 | IO18 | ESP32-C3 GPIO18 |
| 3 | GND | — |
| 4 | 3V3 | 经 NFM18PC104R1C3D 滤波后引出，C17/C21 为滤波电容 |
| 5 | VBUS | 经另一路 NFM18PC104R1C3D 滤波后引出，C16/C20 为滤波电容 |

ESD 保护：D2 在 IO19 上，D4 在 IO18 上（RCLAMP0521T-ES）。

### 2.4 I2C 上拉

| 位号 | 阻值 | 连接 |
|---|---|---|
| R4 | 4.7kΩ | 3V3 — IO0_I2C_SDA |
| R5 | 4.7kΩ | 3V3 — IO1_I2C_SCL |

## 3. USB、串口与电源（P2）

### 3.1 USB Type-C

- USB1 = TYPEC-304-ACP16 母座，CC1/CC2 各经 R14/R13 5.1kΩ 下拉到 GND（受电端）。
- D+/D- 各接一只 ESD 保护二极管（D5/D6，RCLAMP0521T-ES），D7 保护 VBUS。
- VBUS 经 F1（BSMD0603L-100，自恢复保险丝）后成为板上 VBUS 网络，L4
  （UPZ1608U221-2R2TF，磁珠）用于电源滤波。
- Type-C 只接 CH343P，ESP32-C3 的 GPIO18/GPIO19 未接 USB，板上没有原生 USB。

### 3.2 USB 转串口 CH343P（U4，I2C 无关）

- CH343P 的 UD+/UD- 接 Type-C 的 D+/D-；TXD/RXD 接 ESP32-C3 的 U0RXD/U0TXD
  （中间串联 499Ω 电阻 R8/R9）。
- DTR/RTS 经 Q1（LMBT3904DW1T1G，双 NPN）+ R15/R16 10kΩ + C22/C23/C24 100nF
  构成自动下载电路：R16 一路的三极管集电极接 RESET（CHIP_EN），R15 一路的接
  IO9_BOOT，两个三极管交叉耦合（发射极接对方的信号线）。该接法与 esptool 的经典
  复位时序一致，因此烧录工具可以通过串口自动复位并进入下载模式：
  RTS 拉低 EN 复位、DTR 拉低 GPIO9 进入下载模式。
- CH343P 供电 VBUS，其 V3 脚接 3V3。

### 3.3 3.3V 电源

- U5 = ME6217C33M5G LDO：VIN 接 VBUS，VOUT 输出 3V3，输入/输出各接 10uF
  （C25/C26），CE 使能脚接 VBUS 一侧。
- 3V3 为全板数字电源；AU_3V3 为音频部分电源（P3）。
- R17 0Ω 用于 VBUS 与 3V3 之间的选择/预留。

## 4. 板载 I2C 器件

I2C 总线为 IO0_I2C_SDA（GPIO0，SDA）与 IO1_I2C_SCL（GPIO1，SCL），
板上 R4/R5 4.7kΩ 上拉到 3V3。

| 位号 | 器件 | 7 位地址 | 说明（P2/P3） |
|---|---|---|---|
| U9 | QMI8658C | 0x6A | 6 轴姿态传感器；C57 100nF 去耦，3V3 供电 |
| U10 | QMC5883L | 0x0D | 三轴地磁传感器；C59 100nF 去耦，3V3 供电，C60 220nF、C61 4.7uF 为电源滤波 |
| U6 | GXHTC3 | 0x70 | 温湿度传感器；C28 100nF 去耦 |
| 屏模组内 | FT6X36 | 0x38 | 电容触摸屏控制器，经连接器 J5（X9821WRS-02-9TSN）接触到触摸屏，RESET 与 I2C 接同一总线 |

## 5. 音频（P3）

- U7 = ES8311 音频编解码器，I2C 地址 0x18，I2S 接口，AU_3V3 供电。
  与 ESP32-C3 的 I2S 连接：IO10_I2S_MCK、IO8_I2S_BCK、IO12_I2S_WS、
  IO7_I2S_DI、IO11_I2S_DO。
- MIC1 = ZTS6216 模拟麦克风，经 C40/C41 1uF、R27/R28 0Ω 及 R34 220Ω、C27/C44/C45
  等偏置网络接编解码器输入（VMIC 由编解码器提供）。
- U8 = NS4150B 音频功放：INP/INN 经 C32/C33 100nF 与 R22/R23 100kΩ 接编解码器输出，
  R31/R32 0Ω 接 PA_IN+/PA_IN-；输出接喇叭接口驱动扬声器，
  CTRL 为 IO13_PA_EN（GPIO13）使能，VCC 由 VBUS 经 C46 1uF、C48/C53 22uF 供电。
- 音频地 AU_GND、PA_GND 与数字地 GND 通过 R25/R26、R29/R30 等 0Ω 电阻连接。

## 6. 显示与触摸（P3）

- TFT 接口：J3 = AFC34-S06FIA-00（6 针），J4 = AFC34-S10FIA-00（10 针）排线座。
  控制信号 IO3_LCD_SCK（GPIO3）、IO4_LCD_CS（GPIO4）、IO5_LCD_MOSI（GPIO5）、
  IO6_LCD_DC（GPIO6），背光由 IO2_LCD_BL（GPIO2）经 Q2（SI2301CDS-T1-GE3-ES，
  P 沟道 MOS）开关，R18 10kΩ 上拉、R19 10Ω、R20 1kΩ 为背光驱动相关电阻。
- 触摸屏经 J5（X9821WRS-02-9TSN）接 FT6X36，I2C 地址 0x38，RESET 网络与
  复位按键同网络。

## 7. GPIO 分配总表

| GPIO | 网络 | 方向/用途 |
|---|---|---|
| GPIO0 | IO0_I2C_SDA | I2C SDA（传感器、触摸、外部接口 J1-2） |
| GPIO1 | IO1_I2C_SCL | I2C SCL（传感器、触摸、外部接口 J1-1） |
| GPIO2 | IO2_LCD_BL | TFT 背光使能（经 Q2） |
| GPIO3 | IO3_LCD_SCK | TFT SPI 时钟 |
| GPIO4 | IO4_LCD_CS | TFT 片选 |
| GPIO5 | IO5_LCD_MOSI | TFT SPI 数据 |
| GPIO6 | IO6_LCD_DC | TFT 数据/命令 |
| GPIO7 | IO7_I2S_DI | I2S 数据输入 |
| GPIO8 | IO8_I2S_BCK | I2S 位时钟（R35 10kΩ 上拉） |
| GPIO9 | IO9_BOOT | BOOT 按键（上拉 10kΩ，参与下载模式） |
| GPIO10 | IO10_I2S_MCK | I2S 主时钟 |
| GPIO11 | IO11_I2S_DO | I2S 数据输出（VDD_SPI 脚复用为 GPIO） |
| GPIO12 | IO12_I2S_WS | I2S 帧同步（SPIHD 脚复用） |
| GPIO13 | IO13_PA_EN | 功放使能（SPIWP 脚复用） |
| GPIO14 | SPI_CS0 | 外部 Flash 片选 |
| GPIO15 | SPI_CLK | 外部 Flash 时钟 |
| GPIO16 | SPI_ID | 外部 Flash 数据输入 |
| GPIO17 | SPI_IQ | 外部 Flash 数据输出 |
| GPIO18 | IO18 | 外部接口 J2-2 |
| GPIO19 | IO19 | 外部接口 J2-1 |
| GPIO20 | U0RXD | 串口接收（接 CH343P TXD） |
| GPIO21 | U0TXD | 串口发送（接 CH343P RXD） |

## 8. 硬件约束

- 板上没有原生 USB，串口控制台在 UART0（GPIO20/21）上，USB 侧由 CH343P 转接成
  一个 USB 串口设备（节点名取决于内核绑定的驱动，见 README）。
- GPIO11/12/13 原本是 Flash 的 VDD_SPI/SPIHD/SPIWP。外部 Flash 由 3V3 供电，
  因此这三个脚被复用为 I2S 与功放使能，Flash 必须工作在单线或双线模式，
  不能使用 QIO/QPI。
- GPIO9 是启动模式选择脚，BOOT 按键按下时会拉低该脚，复位后进入下载模式。
- GPIO18/GPIO19 在芯片上复用为 USB D-/D+，本板把这两脚用作外部接口，
  使用 UART0 控制台时必须关闭 USB Serial/JTAG 外设。
- GPIO0/GPIO1（XTAL_32K_P/N）没有接 32.768kHz 晶振，本板作普通 IO 使用，即 I2C 的
  SDA/SCL。
