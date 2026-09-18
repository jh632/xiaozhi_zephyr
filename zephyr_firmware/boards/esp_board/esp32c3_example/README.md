# 语音助手板最小板级定义示例

这份目录是一块语音助手类板子的完整板级定义，基于 `esp32c3_devkitc` 改写，
保留在这里作为示例：以后遇到 Zephyr 官方不支持的板子、或者自己设计的板子时，
可以复制这份目录作为起点。

板上的最小硬件集合只有四项，全部在 [esp32c3_example.dts](esp32c3_example.dts) 里：

| 需求 | 用到的硬件 | 设备树对应 |
|---|---|---|
| MCU + Wi-Fi，运行 Zephyr、联网 | ESP32-C3 + Wi-Fi | SoC 自带，`&wifi` 置 okay |
| 语音采集 | 麦克风经 ES8311 编解码器数字化 | `&i2s` 输入通道（DI=GPIO7） |
| 音频输出 | ES8311 的 DAC + NS4150B 功放 | `&i2s` 输出通道（DO=GPIO11）、功放使能 GPIO13 |
| 交互按键 | BOOT 按键 | `gpio_keys`（GPIO9） |

具体引脚按照立创·实战派 ESP32-C3 开发板 V1.3 的连接编写，可以直接在真板上构建、
烧录、验证。编解码器 ES8311 的控制通道是 I2C0（地址 0x18），Zephyr 主线还没有它的
驱动，实现驱动时才需要在设备树里补充节点。

## 文件说明

| 文件 | 作用 |
|---|---|
| `board.yml` | 板名、厂商、使用的 SoC，hardware model v2 用它识别一块板 |
| `esp32c3_example.dts` | 设备树主文件：外设状态、flash 容量、分区表 |
| `esp32c3_example-pinctrl.dtsi` | 引脚复用定义（UART、I2C、I2S） |
| `esp32c3_example.yaml` | 板卡描述：架构、工具链、支持的驱动 |
| `esp32c3_example_defconfig` | 板级默认 Kconfig |
| `Kconfig.esp32c3_example` | 板级 Kconfig，选择 SoC 与 SoC 变体 |
| `Kconfig` | 板级内存池等默认值 |
| `Kconfig.sysbuild` | sysbuild 构建时的 bootloader 默认值 |
| `board.cmake` | 烧录与调试参数（esptool、openocd） |

## 新板子的写法

1. 在 `boards/<厂商>/<板名>/` 建目录，厂商目录名与 `board.yml` 里的 `vendor` 保持一致；
2. 复制本目录的全部文件，把文件名和内容里的 `esp32c3_example` 改成新板名，
   `board.yml` 的 `name` 就是构建时 `-b` 后面跟的名字；
3. 按原理图修改 `-pinctrl.dtsi` 里的引脚复用和 `.dts` 里的外设配置；
4. 在 `CMakeLists.txt` 里 `find_package(Zephyr ...)` 之前加入
   `list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})`，之后 `west build -b <板名>` 即可。

注意 `Kconfig.<板名>` 的文件名和其中的 `config BOARD_<大写板名>` 符号要与板名一致，
`board.yml`、`<板名>.dts`、`<板名>_defconfig` 等文件名同理。
