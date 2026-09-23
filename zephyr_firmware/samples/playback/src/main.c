/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 音频播放示例: 把 audio/clip.pcm 里的原始 PCM 经 I2S 送给 ES8311, 由扬声器放出来。
 * 音频为 16 kHz / 16 位 / 双声道, 换成自己的音频的办法见 README.md。
 */

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>

#include <string.h>

#define SAMPLE_RATE  16000U
#define CHANNELS     2U
#define BLOCK_MS     20U
#define BLOCK_FRAMES (SAMPLE_RATE / 1000U * BLOCK_MS)
#define BLOCK_SIZE   (BLOCK_FRAMES * CHANNELS * sizeof(int16_t))
#define BLOCK_COUNT  4U

K_MEM_SLAB_DEFINE(playback_tx_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

/* 构建时由 audio/clip.pcm 生成的字节数组 */
static const uint8_t s_clip_pcm[] = {
#include <clip.pcm.inc>
};

/* 待送出的整块数据, i2s_buf_write 会把它拷进驱动内部块 */
static uint8_t s_block[BLOCK_SIZE];

/* 把音频里从第 offset 字节开始的一块送入 I2S, 末尾不足一块的部分补静音 */
static int audio_send_block(const struct device *i2s_dev, size_t offset)
{
	size_t chunk = MIN(sizeof(s_clip_pcm) - offset, BLOCK_SIZE);

	memcpy(s_block, &s_clip_pcm[offset], chunk);
	memset(s_block + chunk, 0, BLOCK_SIZE - chunk);

	return i2s_buf_write(i2s_dev, s_block, BLOCK_SIZE);
}

int main(void)
{
	const struct device *i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s));
	const struct device *codec_dev = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
	struct audio_codec_cfg codec_cfg = {
		/* I2S 控制器输出的 MCLK 固定为 256 倍采样率 */
		.mclk_freq = SAMPLE_RATE * 256U,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_route = AUDIO_ROUTE_PLAYBACK,
		.dai_cfg.i2s = {
			.word_size = 16U,
			.channels = CHANNELS,
			.format = I2S_FMT_DATA_FORMAT_I2S,
			/* 时钟由 MCU 产生, 编解码器作为从机 */
			.options = I2S_OPT_BIT_CLK_TARGET | I2S_OPT_FRAME_CLK_TARGET,
			.frame_clk_freq = SAMPLE_RATE,
			.mem_slab = &playback_tx_slab,
			.block_size = BLOCK_SIZE,
		},
	};
	struct i2s_config i2s_cfg = {
		.word_size = 16U,
		.channels = CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER,
		.frame_clk_freq = SAMPLE_RATE,
		.block_size = BLOCK_SIZE,
		.mem_slab = &playback_tx_slab,
		.timeout = 1000U,
	};
	int ret;

	ret = audio_codec_configure(codec_dev, &codec_cfg);
	if (ret < 0) {
		return ret;
	}

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &i2s_cfg);
	if (ret < 0) {
		return ret;
	}

	/* 打开播放通路, 驱动在这里使能功放并解除 DAC 静音 */
	ret = audio_codec_start(codec_dev, AUDIO_DAI_DIR_TX);
	if (ret < 0) {
		return ret;
	}

	/* 第一块要在启动前送入, 发送队列空转会被 ESP32 的 I2S 驱动判为错误 */
	ret = audio_send_block(i2s_dev, 0U);
	if (ret < 0) {
		goto err_stop;
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) {
		goto err_stop;
	}

	for (size_t offset = BLOCK_SIZE; offset < sizeof(s_clip_pcm); offset += BLOCK_SIZE) {
		ret = audio_send_block(i2s_dev, offset);
		if (ret < 0) {
			goto err_stop;
		}
	}

	/* 送完后等发送队列排空再关闭通路, 结尾的几块不会被切掉 */
	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	k_sleep(K_MSEC((BLOCK_COUNT + 1U) * BLOCK_MS));

err_stop:
	(void)audio_codec_stop(codec_dev, AUDIO_DAI_DIR_TX);

	printk("播放结束: %d\n", ret);

	return ret;
}
