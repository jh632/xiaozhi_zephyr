# xiaozhi_zephyr

在 Zephyr 上实现小智 AI 聊天机器人的固件工程。

上游参考项目是 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)

本工程基于 Zephyr 重新实现，希望增加项目的兼容性

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

## 当前进度

- [x] REQ_001_Driver_ES8311
- [ ] REQ_004_Service_audio
- [ ] REQ_005_Service_display
- [ ] REQ_006_Service_wifi

## 许可

工程采用 Apache-2.0 许可，正文见 [LICENSE](LICENSE)，与 Zephyr 保持一致，源文件头部带有 SPDX 标识。
`zephyr_firmware/doc/` 下的原理图 PDF 及其整理结果来自立创开发板的公开资料，版权归原作者所有，
不适用上述许可。
