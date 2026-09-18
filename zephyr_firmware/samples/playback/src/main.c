/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 音频播放示例: 把一段原始 PCM 经 I2S 送给 ES8311, 由板载扬声器放出来。
 *
 * 音频内容放在 audio/clip.pcm, 构建时转成字节数组嵌入固件,
 * 换成自己的音频文件的办法见 README.md。
 */

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <stdint.h>

/* 与 audio/clip.pcm 的属性一致, 也是 xiaozhi 语音链路使用的参数 */
#define AUDIO_SAMPLE_RATE 16000U
#define AUDIO_BITS        16U
#define AUDIO_CHANNELS    2U

/* I2S 控制器在 16 位字长下输出的 MCLK 固定为 256 倍采样率, 编解码器按同样倍数配置时钟链 */
#define AUDIO_MCLK_FREQ (AUDIO_SAMPLE_RATE * 256U)

#define AUDIO_BLOCK_MS     20U
#define AUDIO_BLOCK_FRAMES (AUDIO_SAMPLE_RATE / 1000U * AUDIO_BLOCK_MS)
#define AUDIO_BLOCK_SIZE   (AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS * (AUDIO_BITS / 8U))
#define AUDIO_BLOCK_COUNT  4U
#define AUDIO_TIMEOUT_MS   1000U

/* 每送入这么多毫秒的数据打印一次进度 */
#define AUDIO_PROGRESS_MS 2000U

K_MEM_SLAB_DEFINE(playback_tx_slab, AUDIO_BLOCK_SIZE, AUDIO_BLOCK_COUNT, 4);

/* 构建时由 audio/clip.pcm 生成的字节数组: 16 位小端, 单声道 */
static const uint8_t s_clip_pcm[] = {
#include <clip.pcm.inc>
};

/* i2s_buf_write 会在这块缓冲与驱动内部块之间拷贝 */
static int16_t s_tx_block[AUDIO_BLOCK_SIZE / sizeof(int16_t)];

/* 取单声道 PCM 的一个样点, 越过末尾时按静音处理, 供最后一块补零使用 */
static int16_t audio_clip_sample(size_t frame)
{
	size_t offset = frame * sizeof(int16_t);

	if (offset + sizeof(int16_t) > sizeof(s_clip_pcm)) {
		return 0;
	}

	return (int16_t)sys_get_le16(&s_clip_pcm[offset]);
}

/*
 * ES8311 是单声道器件, 两个槽位放同一路信号。
 * first_frame 是这一块数据在整段音频里的起始帧序号。
 */
static void audio_fill_block(int16_t *block, size_t first_frame)
{
	for (size_t idx = 0; idx < AUDIO_BLOCK_FRAMES; idx++) {
		int16_t value = audio_clip_sample(first_frame + idx);

		block[AUDIO_CHANNELS * idx] = value;
		block[AUDIO_CHANNELS * idx + 1U] = value;
	}
}

static int audio_open(const struct device *i2s_dev, const struct device *codec_dev)
{
	struct audio_codec_cfg codec_cfg = {
		.mclk_freq = AUDIO_MCLK_FREQ,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_route = AUDIO_ROUTE_PLAYBACK,
		.dai_cfg.i2s = {
			.word_size = AUDIO_BITS,
			.channels = AUDIO_CHANNELS,
			.format = I2S_FMT_DATA_FORMAT_I2S,
			/* 时钟由 MCU 产生, 编解码器作为从机 */
			.options = I2S_OPT_BIT_CLK_TARGET | I2S_OPT_FRAME_CLK_TARGET,
			.frame_clk_freq = AUDIO_SAMPLE_RATE,
			.mem_slab = &playback_tx_slab,
			.block_size = AUDIO_BLOCK_SIZE,
		},
	};
	struct i2s_config stream_cfg = {
		.word_size = AUDIO_BITS,
		.channels = AUDIO_CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER,
		.frame_clk_freq = AUDIO_SAMPLE_RATE,
		.block_size = AUDIO_BLOCK_SIZE,
		.mem_slab = &playback_tx_slab,
		.timeout = AUDIO_TIMEOUT_MS,
	};
	int ret;

	ret = audio_codec_configure(codec_dev, &codec_cfg);
	if (ret < 0) {
		printk("[FAIL] 配置编解码器失败: %d\n", ret);
		return ret;
	}

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &stream_cfg);
	if (ret < 0) {
		printk("[FAIL] 配置 I2S 播放失败: %d\n", ret);
		return ret;
	}

	/* 打开播放通路: 驱动在这里使能功放并解除 DAC 静音 */
	ret = audio_codec_start(codec_dev, AUDIO_DAI_DIR_TX);
	if (ret < 0) {
		printk("[FAIL] 打开播放通路失败: %d\n", ret);
	}

	return ret;
}

/* 数据送完后等发送队列排空再关闭通路, 结尾的几块才不会被切掉 */
static int audio_close(const struct device *i2s_dev, const struct device *codec_dev)
{
	int ret;

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	if (ret < 0) {
		printk("[FAIL] 等待播放结束失败: %d\n", ret);
		(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
		(void)audio_codec_stop(codec_dev, AUDIO_DAI_DIR_TX);

		return ret;
	}

	/*
	 * 发送队列里最多还压着 AUDIO_BLOCK_COUNT 块数据, 等它们播完再关功放。
	 * 队列排空后驱动会把状态置回 READY, 这里不需要再做别的收尾。
	 */
	k_sleep(K_MSEC((AUDIO_BLOCK_COUNT + 1U) * AUDIO_BLOCK_MS));

	ret = audio_codec_stop(codec_dev, AUDIO_DAI_DIR_TX);
	if (ret < 0) {
		printk("[FAIL] 关闭播放通路失败: %d\n", ret);
	}

	return ret;
}

int main(void)
{
	const struct device *i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s));
	const struct device *codec_dev = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
	const uint32_t total_frames = sizeof(s_clip_pcm) / sizeof(int16_t);
	const uint32_t blocks = (total_frames + AUDIO_BLOCK_FRAMES - 1U) / AUDIO_BLOCK_FRAMES;
	int ret;

	printk("\n===== 音频播放示例: %u Hz, %u 位, 单声道 =====\n", AUDIO_SAMPLE_RATE, AUDIO_BITS);
	printk("音频内容 %u 帧, 约 %u 毫秒\n", total_frames,
	       total_frames * 1000U / AUDIO_SAMPLE_RATE);

	if (!device_is_ready(i2s_dev)) {
		printk("[FAIL] I2S 控制器未就绪\n");
		return -ENODEV;
	}

	if (!device_is_ready(codec_dev)) {
		printk("[FAIL] 编解码器未就绪\n");
		return -ENODEV;
	}

	ret = audio_open(i2s_dev, codec_dev);
	if (ret < 0) {
		return ret;
	}

	/* 先排入一块数据再启动, 否则发送队列空转会被驱动判为错误 */
	audio_fill_block(s_tx_block, 0U);
	ret = i2s_buf_write(i2s_dev, (uint8_t *)s_tx_block, AUDIO_BLOCK_SIZE);
	if (ret < 0) {
		printk("[FAIL] 写入播放数据失败: %d\n", ret);
		goto err_close;
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) {
		printk("[FAIL] 启动 I2S 失败: %d\n", ret);
		goto err_close;
	}

	for (uint32_t index = 1U; index < blocks; index++) {
		audio_fill_block(s_tx_block, (size_t)index * AUDIO_BLOCK_FRAMES);

		ret = i2s_buf_write(i2s_dev, (uint8_t *)s_tx_block, AUDIO_BLOCK_SIZE);
		if (ret < 0) {
			printk("[FAIL] 写入播放数据失败: %d\n", ret);
			goto err_close;
		}

		if ((index % (AUDIO_PROGRESS_MS / AUDIO_BLOCK_MS)) == 0U) {
			printk("已送入 %u 毫秒\n", index * AUDIO_BLOCK_MS);
		}
	}

	ret = audio_close(i2s_dev, codec_dev);
	if (ret < 0) {
		return ret;
	}

	printk("播放结束\n");

	return 0;

err_close:
	(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	(void)audio_codec_stop(codec_dev, AUDIO_DAI_DIR_TX);

	return ret;
}
