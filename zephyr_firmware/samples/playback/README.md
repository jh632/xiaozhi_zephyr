# 音频播放示例

把一段原始 PCM 经 I2S 送给 ES8311，由板载扬声器放出来。这是在本工程里播放音频
需要的最小内容：配置编解码器与 I2S、打开播放通路、按块送数据、等发送结束再收尾。

| 文件 | 作用 |
|---|---|
| [src/main.c](src/main.c) | 播放流程 |
| [audio/clip.pcm](audio/clip.pcm) | 待播放的音频内容，构建时转成字节数组嵌入固件 |
| [boards/esp32c3_lckfb.overlay](boards/esp32c3_lckfb.overlay) | 板级接线（编解码器、功放、I2S 引脚） |
| [CMakeLists.txt](CMakeLists.txt) | 复用应用目录下的 ES8311 驱动与设备树绑定 |

## 构建与烧录

在 `zephyr_firmware` 目录下执行：

```bash
west build -p always -b esp32c3_lckfb -d samples/playback/build samples/playback
west flash -d samples/playback/build
```

程序播放一遍，串口最后打印一行结果：

```
播放结束: 0
```

## 换成自己的音频

`audio/clip.pcm` 是 16 位小端、双声道、不带文件头的原始 PCM，采样率要与
`src/main.c` 里的 `SAMPLE_RATE` 相同（当前 16 kHz）。任意音频文件都可以
用 ffmpeg 或 sox 转成这个格式：

```bash
ffmpeg -i 歌曲.mp3 -ac 2 -ar 16000 -f s16le audio/clip.pcm
sox 歌曲.mp3 -c 2 -r 16000 -t raw audio/clip.pcm
```

示例自带的内容是 11.2 秒的合成音频，旋律为欢乐颂主题（公有领域）。
文件内容会完整编译进固件：16 kHz 双声道每秒占用 64 KB，这段内容用掉 716 KB，
整个固件（含驱动与内核）占用 854 KB。应用分区有 3.75 MB，固件本身占 137 KB，
剩下的空间最长可以放一分钟左右的音频；整首歌需要等联网获取音频的功能做好之后才能播放，
届时音频经网络流式传输，不再占用 Flash。

## 实现要点

- 播放通路用 `audio_codec_start(codec_dev, AUDIO_DAI_DIR_TX)` 打开，驱动在这里
  使能功放 NS4150B 并解除 DAC 静音，收尾的 `audio_codec_stop` 反过来关闭。
- 数据按 20 毫秒一块从音频数组拷进缓冲，经 `i2s_buf_write` 送入，第一块在
  `I2S_TRIGGER_START` 之前排入队列，发送队列空转会被 ESP32 的 I2S 驱动判为错误。
- I2S 配置为两个槽位（ESP32-C3 的 I2S 驱动只支持两个槽位），音频按双声道存放。
- 送完全部数据后先 `I2S_TRIGGER_DRAIN`，等队列排空再关闭通路，结尾的几块数据
  不会被切掉。
