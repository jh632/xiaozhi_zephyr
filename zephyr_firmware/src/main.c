/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 立创·实战派 ESP32-C3 开发板 V1.3 启动自检:
 *   1. 打印板级信息, 扫描 I2C 总线核对原理图整理出的引脚分配
 *   2. 音频通路自检, 经 ES8311 录音与放音
 *
 * 引脚与网络对照见 doc/SCH_ESP32-C3-V1_3.md。
 */

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>

#include <math.h>
#include <stdint.h>

/* I2C 扫描范围, 0x00-0x07 与 0x78-0x7F 为保留地址 */
#define I2C_SCAN_ADDR_FIRST 0x08U
#define I2C_SCAN_ADDR_LAST  0x77U

/* 探测时读取的寄存器地址, 各传感器均为只读的器件识别寄存器 */
#define I2C_PROBE_REG 0x00U

/* 音频参数与 xiaozhi 语音链路一致: 16 kHz, 16 位, 两个槽位 */
#define AUDIO_SAMPLE_RATE 16000U
#define AUDIO_BITS        16U
#define AUDIO_CHANNELS    2U

/* I2S 控制器输出的 MCLK 固定为 256 倍采样率 */
#define AUDIO_MCLK_FREQ (AUDIO_SAMPLE_RATE * 256U)

#define AUDIO_BLOCK_MS     20U
#define AUDIO_BLOCK_FRAMES (AUDIO_SAMPLE_RATE / 1000U * AUDIO_BLOCK_MS)
#define AUDIO_BLOCK_SIZE   (AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS * (AUDIO_BITS / 8U))
#define AUDIO_BLOCK_COUNT  8U
#define AUDIO_TIMEOUT_MS   1000

/* 测试音: 440 Hz 正弦, 幅度留出约 10 dB 余量 */
#define AUDIO_TONE_HZ   440
#define AUDIO_TONE_PEAK 10000
/* 放音时回采到的测试音分量至少要比环境噪声时高出的倍数 */
#define AUDIO_TONE_RATIO 4

/* 2π, picolibc 默认不暴露 M_PI */
#define AUDIO_TWO_PI 6.283185307179586

K_MEM_SLAB_DEFINE(audio_tx_slab, AUDIO_BLOCK_SIZE, AUDIO_BLOCK_COUNT, 4);
K_MEM_SLAB_DEFINE(audio_rx_slab, AUDIO_BLOCK_SIZE, AUDIO_BLOCK_COUNT, 4);

/* i2s_buf_write / i2s_buf_read 会在这两个缓冲与驱动内部块之间拷贝 */
static uint8_t s_tx_buffer[AUDIO_BLOCK_SIZE];
static uint8_t s_rx_buffer[AUDIO_BLOCK_SIZE];

struct audio_stats {
	uint32_t samples;
	uint32_t blocks;
	int32_t peak;
	int64_t sum_squares;
	double tone_acc1;
	double tone_acc2;
};

/* 自检各阶段: 每块 20 毫秒, 播放期间用板载麦克风回采 */
static const struct audio_stage {
	const char *name;
	bool tone;
	bool playback;
	uint32_t blocks;
	int volume_db;
} s_audio_stages[] = {
	{"环境噪声(功放关闭)", false, false, 50U, 0},
	{"440Hz 放音(0 dB)", true, true, 100U, 0},
	{"440Hz 放音(-20 dB)", true, true, 100U, -20},
};

static struct audio_stats s_stage_stats[ARRAY_SIZE(s_audio_stages)];

/* 单频点检测的系数 2·cos(2π·f/fs), 按实际采样率算好后全程使用 */
static double s_tone_coeff;

/* 从设备读取寄存器判断从设备是否应答, 返回 0 表示存在 */
static int board_i2c_probe(const struct device *i2c_dev, uint16_t addr)
{
	uint8_t reg = I2C_PROBE_REG;
	uint8_t value;

	return i2c_write_read(i2c_dev, addr, &reg, sizeof(reg), &value, sizeof(value));
}

static void board_i2c_scan(const struct device *i2c_dev)
{
	unsigned int found = 0U;

	for (uint16_t addr = I2C_SCAN_ADDR_FIRST; addr <= I2C_SCAN_ADDR_LAST; addr++) {
		if (board_i2c_probe(i2c_dev, addr) != 0) {
			continue;
		}

		printk("I2C 从设备应答: 0x%02X\n", addr);
		found++;
	}

	printk("I2C 扫描结束: 共 %u 个从设备\n", found);
}

static void audio_stats_add(struct audio_stats *stats, const int16_t *samples, size_t count)
{
	for (size_t idx = 0; idx < count; idx++) {
		int32_t value = samples[idx];
		int32_t magnitude = (value < 0) ? -value : value;

		if (magnitude > stats->peak) {
			stats->peak = magnitude;
		}

		stats->sum_squares += (int64_t)value * value;

		/*
		 * 两个声道承载同一路麦克风信号, 单频点检测只取左声道。
		 * 数据块按帧对齐, 用块内下标就能取到同一个声道。
		 */
		if ((idx % AUDIO_CHANNELS) == 0U) {
			double acc = (double)value + (s_tone_coeff * stats->tone_acc1) -
				     stats->tone_acc2;

			stats->tone_acc2 = stats->tone_acc1;
			stats->tone_acc1 = acc;
		}

		stats->samples++;
	}
}

static int32_t audio_stats_rms(const struct audio_stats *stats)
{
	if (stats->samples == 0U) {
		return 0;
	}

	return (int32_t)sqrt((double)stats->sum_squares / stats->samples);
}

/*
 * 用 Goertzel 算法取出测试音频率上的分量幅度。回采信号里既有测试音也有环境噪声
 * 与麦克风自身的噪声, 单频点幅度比总电平更能说明测试音是否送达。
 */
static int32_t audio_stats_tone_amplitude(const struct audio_stats *stats)
{
	double frames = (double)stats->samples / AUDIO_CHANNELS;
	double power = (stats->tone_acc1 * stats->tone_acc1) +
		       (stats->tone_acc2 * stats->tone_acc2) -
		       (s_tone_coeff * stats->tone_acc1 * stats->tone_acc2);

	if (frames < 1.0 || power <= 0.0) {
		return 0;
	}

	return (int32_t)(2.0 * sqrt(power) / frames);
}

static void audio_stats_print(const struct audio_stage *stage, const struct audio_stats *stats)
{
	printk("%s: %u 块 %u 采样点, RMS %d, 峰值 %d, %d Hz 分量幅度 %d\n", stage->name,
	       stats->blocks, stats->samples, audio_stats_rms(stats), stats->peak, AUDIO_TONE_HZ,
	       audio_stats_tone_amplitude(stats));
}

/* 按当前阶段的要求填充待播放的数据: 静音或正弦测试音 */
static void audio_fill_tx_block(int16_t *frames, size_t frame_count, bool tone,
				uint32_t *tone_index)
{
	for (size_t idx = 0; idx < frame_count; idx++) {
		int16_t value = 0;

		if (tone) {
			double phase = AUDIO_TWO_PI * (double)AUDIO_TONE_HZ * (double)(*tone_index) /
				       (double)AUDIO_SAMPLE_RATE;

			value = (int16_t)(AUDIO_TONE_PEAK * sin(phase));
			(*tone_index)++;
		}

		frames[AUDIO_CHANNELS * idx] = value;
		frames[AUDIO_CHANNELS * idx + 1] = value;
	}
}

static int audio_set_output_volume(const struct device *codec_dev, int volume_db)
{
	audio_property_value_t value = {.vol = volume_db};

	return audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL,
					value);
}

/*
 * 收发各推进一块: 播放方向写入一块数据, 采集方向取回一块数据。
 * 两个方向同为 20 毫秒一块, 交替执行即可保持同步推进。
 */
static int audio_exchange(const struct device *i2s_dev, bool tone, uint32_t *tone_index,
			  struct audio_stats *stats)
{
	size_t received = sizeof(s_rx_buffer);
	int ret;

	audio_fill_tx_block((int16_t *)s_tx_buffer, AUDIO_BLOCK_FRAMES, tone, tone_index);

	ret = i2s_buf_write(i2s_dev, s_tx_buffer, sizeof(s_tx_buffer));
	if (ret < 0) {
		printk("[FAIL] 写入播放数据失败: %d\n", ret);
		return ret;
	}

	ret = i2s_buf_read(i2s_dev, s_rx_buffer, &received);
	if (ret < 0) {
		printk("[FAIL] 读取采集数据失败: %d\n", ret);
		return ret;
	}

	audio_stats_add(stats, (const int16_t *)s_rx_buffer, received / sizeof(int16_t));
	stats->blocks++;

	return 0;
}

static int audio_run_stage(const struct device *i2s_dev, const struct device *codec_dev,
			   const struct audio_stage *stage, uint32_t *tone_index,
			   struct audio_stats *stats)
{
	int ret;

	ret = audio_set_output_volume(codec_dev, stage->volume_db);
	if (ret < 0) {
		printk("[FAIL] 设置输出音量 %d dB 失败: %d\n", stage->volume_db, ret);
		return ret;
	}

	/* 采集环境噪声时关掉功放, 避免扬声器与麦克风的耦合影响噪声底 */
	if (stage->playback) {
		audio_codec_start_output(codec_dev);
	} else {
		audio_codec_stop_output(codec_dev);
	}

	for (uint32_t idx = 0; idx < stage->blocks; idx++) {
		ret = audio_exchange(i2s_dev, stage->tone, tone_index, stats);
		if (ret < 0) {
			return ret;
		}
	}

	audio_stats_print(stage, stats);

	return 0;
}

static int audio_verify(void)
{
	int32_t quiet_tone = audio_stats_tone_amplitude(&s_stage_stats[0]);
	int32_t tone_here = audio_stats_tone_amplitude(&s_stage_stats[1]);
	int32_t faded_tone = audio_stats_tone_amplitude(&s_stage_stats[2]);

	if (tone_here < quiet_tone * AUDIO_TONE_RATIO) {
		printk("[FAIL] 放音期间 %d Hz 分量幅度 %d 未明显高于环境噪声时的 %d\n",
		       AUDIO_TONE_HZ, tone_here, quiet_tone);
		return -EIO;
	}

	if (faded_tone >= tone_here / 2) {
		printk("[FAIL] 降低输出音量后 %d Hz 分量幅度 %d 未下降(0 dB 时为 %d)\n",
		       AUDIO_TONE_HZ, faded_tone, tone_here);
		return -EIO;
	}

	printk("音频自检通过\n");

	return 0;
}

static int audio_selftest(void)
{
	const struct device *i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s));
	const struct device *codec_dev = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
	struct audio_codec_cfg codec_cfg = {
		.mclk_freq = AUDIO_MCLK_FREQ,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_route = AUDIO_ROUTE_PLAYBACK_CAPTURE,
		.dai_cfg.i2s = {
			.word_size = AUDIO_BITS,
			.channels = AUDIO_CHANNELS,
			.format = I2S_FMT_DATA_FORMAT_I2S,
			/* 时钟由 MCU 产生, 编解码器作为从机 */
			.options = I2S_OPT_BIT_CLK_TARGET | I2S_OPT_FRAME_CLK_TARGET,
			.frame_clk_freq = AUDIO_SAMPLE_RATE,
			.mem_slab = &audio_tx_slab,
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
		.timeout = AUDIO_TIMEOUT_MS,
	};
	uint32_t tone_index = 0U;
	int ret;

	if (!device_is_ready(i2s_dev)) {
		printk("[FAIL] I2S 控制器未就绪\n");
		return -ENODEV;
	}

	if (!device_is_ready(codec_dev)) {
		printk("[FAIL] 编解码器未就绪\n");
		return -ENODEV;
	}

	ret = audio_codec_configure(codec_dev, &codec_cfg);
	if (ret < 0) {
		printk("[FAIL] 配置编解码器失败: %d\n", ret);
		return ret;
	}

	/*
	 * 先配置播放方向再配置采集方向: ESP32-C3 的收发共用同一组 WS/BCK,
	 * 驱动在发送方向已经配置的情况下会把采集方向当作从机, 两者按同一帧对齐;
	 * 顺序反过来时采集方向会自行计数时钟, 收回的每个样点都偏离 2 个时钟位。
	 */
	stream_cfg.mem_slab = &audio_tx_slab;
	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &stream_cfg);
	if (ret < 0) {
		printk("[FAIL] 配置 I2S 播放失败: %d\n", ret);
		return ret;
	}

	stream_cfg.mem_slab = &audio_rx_slab;
	ret = i2s_configure(i2s_dev, I2S_DIR_RX, &stream_cfg);
	if (ret < 0) {
		printk("[FAIL] 配置 I2S 采集失败: %d\n", ret);
		return ret;
	}

	ret = audio_codec_start(codec_dev, AUDIO_DAI_DIR_TXRX);
	if (ret < 0) {
		printk("[FAIL] 打开编解码器通路失败: %d\n", ret);
		return ret;
	}

	s_tone_coeff = 2.0 * cos(AUDIO_TWO_PI * (double)AUDIO_TONE_HZ / (double)AUDIO_SAMPLE_RATE);

	/* 播放方向先排入一块数据, START 时才不会立即欠载 */
	audio_fill_tx_block((int16_t *)s_tx_buffer, AUDIO_BLOCK_FRAMES, false, &tone_index);

	ret = i2s_buf_write(i2s_dev, s_tx_buffer, sizeof(s_tx_buffer));
	if (ret < 0) {
		printk("[FAIL] 预填播放数据失败: %d\n", ret);
		return ret;
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_BOTH, I2S_TRIGGER_START);
	if (ret < 0) {
		printk("[FAIL] 启动 I2S 失败: %d\n", ret);
		return ret;
	}

	for (size_t idx = 0; idx < ARRAY_SIZE(s_audio_stages); idx++) {
		ret = audio_run_stage(i2s_dev, codec_dev, &s_audio_stages[idx], &tone_index,
				      &s_stage_stats[idx]);
		if (ret < 0) {
			return ret;
		}
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_BOTH, I2S_TRIGGER_DROP);
	if (ret < 0) {
		printk("[FAIL] 停止 I2S 失败: %d\n", ret);
		return ret;
	}

	ret = audio_codec_stop(codec_dev, AUDIO_DAI_DIR_TXRX);
	if (ret < 0) {
		printk("[FAIL] 关闭编解码器通路失败: %d\n", ret);
		return ret;
	}

	return audio_verify();
}

int main(void)
{
	const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	int ret;

	printk("\n===== 立创·实战派 ESP32-C3 开发板 V1.3 =====\n");
	printk("控制台 UART0: GPIO21 TX / GPIO20 RX, 经 CH343P 转 USB\n");
	printk("I2C: SDA=GPIO0, SCL=GPIO1, 板上 R4/R5 4.7kΩ 上拉到 3V3\n");

	if (!device_is_ready(i2c_dev)) {
		printk("[FAIL] I2C0 未就绪\n");
		return 0;
	}

	board_i2c_scan(i2c_dev);

	printk("\n----- 音频通路自检 -----\n");
	printk("I2S: MCLK=GPIO10, BCK=GPIO8, WS=GPIO12, DI=GPIO7, DO=GPIO11\n");
	printk("ES8311: I2C 0x18, DAC 输出经 NS4150B 功放(GPIO13 使能)\n");

	ret = audio_selftest();
	if (ret < 0) {
		printk("音频通路自检未通过\n");
	}

	while (true) {
		k_msleep(10000);
		printk("运行中\n");
	}

	return 0;
}
