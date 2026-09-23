/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ES8311 驱动测试, 运行在 native_sim 上, 由 I2C 模拟器代替真实器件,
 * 逐条核对驱动写入的寄存器与功放开关的时序。
 */

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "es8311_emul.h"

#define CODEC_NODE  DT_NODELABEL(es8311)
#define SAMPLE_RATE 16000U
#define MCLK_RATE   (SAMPLE_RATE * 256U)

/* 寄存器地址, 取自 ES8311 Datasheet Rev 8.0 §8 */
#define REG_RESET     0x00
#define REG_CLKMGR_01 0x01
#define REG_CLKMGR_03 0x03
#define REG_CLKMGR_04 0x04
#define REG_SDP_IN    0x09
#define REG_SDP_OUT   0x0A
#define REG_SYS_0D    0x0D
#define REG_SYS_0E    0x0E
#define REG_SYS_12    0x12
#define REG_SYS_14    0x14
#define REG_ADC_15    0x15
#define REG_ADC_17    0x17
#define REG_DAC_31    0x31
#define REG_DAC_32    0x32
#define REG_DAC_37    0x37
#define REG_CHIP_ID1  0xFD
#define REG_CHIP_ID2  0xFE

#define SDP_MUTE              BIT(6)
#define SYS_0D_ANALOG_ON      0x01
#define SYS_0D_ANALOG_OFF     0xFC
#define SYS_0E_ADC_ON         0x02
#define SYS_0E_ADC_OFF        0x6A
#define SYS_12_DAC_ON         0x00
#define SYS_12_DAC_OFF        0x02
#define SYS_14_MIC1_PGA_30DB    0x1A
#define DAC_31_MUTE             0x60
#define VOLUME_ZERO_DB_REG      0xBF

static const struct device *codec_dev = DEVICE_DT_GET(CODEC_NODE);
static const struct emul *codec_emul = EMUL_DT_GET(CODEC_NODE);
static const struct gpio_dt_spec amp_gpio =
	GPIO_DT_SPEC_GET(DT_PATH(speaker_amp), enable_gpios);

/* 与 xiaozhi 语音链路一致的基准配置: 16 kHz, 16 位, 两个槽位, 编解码器作从机 */
static struct audio_codec_cfg default_cfg(void)
{
	return (struct audio_codec_cfg){
		.mclk_freq = MCLK_RATE,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_route = AUDIO_ROUTE_PLAYBACK_CAPTURE,
		.dai_cfg.i2s = {
			.word_size = 16,
			.channels = 2,
			.format = I2S_FMT_DATA_FORMAT_I2S,
			.options = I2S_OPT_BIT_CLK_TARGET | I2S_OPT_FRAME_CLK_TARGET,
			.frame_clk_freq = SAMPLE_RATE,
		},
	};
}

static void es8311_before_each(void *fixture)
{
	struct audio_codec_cfg cfg = default_cfg();

	ARG_UNUSED(fixture);

	zassert_ok(audio_codec_configure(codec_dev, &cfg), "基准配置应当成功");
}

ZTEST(es8311_codec, test_chip_id_read_from_device)
{
	zassert_true(device_is_ready(codec_dev), "编解码器应当就绪");
	zassert_equal(es8311_emul_reg(codec_emul, REG_CHIP_ID1), 0x83, "芯片 ID1 不匹配");
	zassert_equal(es8311_emul_reg(codec_emul, REG_CHIP_ID2), 0x11, "芯片 ID2 不匹配");
}

ZTEST(es8311_codec, test_configure_writes_clock_and_format)
{
	zassert_equal(es8311_emul_reg(codec_emul, REG_RESET) & BIT(7), BIT(7),
		      "芯片状态机应当上电");
	zassert_equal(es8311_emul_reg(codec_emul, REG_RESET) & BIT(6), 0,
		      "应当工作在从机模式");
	zassert_equal(es8311_emul_reg(codec_emul, REG_CLKMGR_01), 0x3F,
		      "MCLK/BCLK/ADC/DAC 时钟应当全部打开");
	zassert_equal(es8311_emul_reg(codec_emul, REG_CLKMGR_03), 0x10, "16 kHz 的 ADC 过采样率");
	zassert_equal(es8311_emul_reg(codec_emul, REG_CLKMGR_04), 0x20, "16 kHz 的 DAC 过采样率");

	/* 16 位字长编码为 3, I2S 格式编码为 0, 配置期间保持静音 */
	zassert_equal(es8311_emul_reg(codec_emul, REG_SDP_IN), 0x4C, "DAC 串行格式错误");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SDP_OUT), 0x4C, "ADC 串行格式错误");

	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_32), VOLUME_ZERO_DB_REG,
		      "默认输出音量为 0 dB");
	zassert_equal(es8311_emul_reg(codec_emul, REG_ADC_17), VOLUME_ZERO_DB_REG,
		      "默认输入音量为 0 dB");
	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_31) & DAC_31_MUTE, DAC_31_MUTE,
		      "配置后应当保持静音");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_0D), SYS_0D_ANALOG_OFF,
		      "配置后模拟部分应当断电");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_12), SYS_12_DAC_OFF,
		      "配置后 DAC 应当断电");
}

ZTEST(es8311_codec, test_configure_rate_table)
{
	struct audio_codec_cfg cfg = default_cfg();

	cfg.dai_cfg.i2s.frame_clk_freq = 48000;
	cfg.mclk_freq = 48000 * 256U;
	zassert_ok(audio_codec_configure(codec_dev, &cfg), "48 kHz 应当被接受");
	zassert_equal(es8311_emul_reg(codec_emul, REG_CLKMGR_04), 0x10, "48 kHz 的 DAC 过采样率");

	cfg.dai_cfg.i2s.frame_clk_freq = 8000;
	cfg.mclk_freq = 8000 * 256U;
	zassert_ok(audio_codec_configure(codec_dev, &cfg), "8 kHz 应当被接受");
	zassert_equal(es8311_emul_reg(codec_emul, REG_CLKMGR_04), 0x20, "8 kHz 的 DAC 过采样率");
}

ZTEST(es8311_codec, test_configure_rejects_invalid_parameters)
{
	struct audio_codec_cfg cfg;

	cfg = default_cfg();
	cfg.mclk_freq = 12288000;
	zassert_equal(audio_codec_configure(codec_dev, &cfg), -EINVAL,
		      "MCLK 与采样率不匹配时应当报错");

	cfg = default_cfg();
	cfg.dai_cfg.i2s.frame_clk_freq = 96000;
	cfg.mclk_freq = 96000 * 256U;
	zassert_equal(audio_codec_configure(codec_dev, &cfg), -EINVAL, "非标准采样率应当报错");

	cfg = default_cfg();
	cfg.dai_cfg.i2s.word_size = 8;
	zassert_equal(audio_codec_configure(codec_dev, &cfg), -EINVAL, "不支持的字长应当报错");

	cfg = default_cfg();
	cfg.dai_cfg.i2s.channels = 3;
	zassert_equal(audio_codec_configure(codec_dev, &cfg), -EINVAL, "不支持的声道数应当报错");

	cfg = default_cfg();
	cfg.dai_type = AUDIO_DAI_TYPE_PCM;
	zassert_equal(audio_codec_configure(codec_dev, &cfg), -ENOTSUP, "不支持的 DAI 类型");

	cfg = default_cfg();
	cfg.dai_route = AUDIO_ROUTE_BYPASS;
	zassert_equal(audio_codec_configure(codec_dev, &cfg), -ENOTSUP, "不支持的音频通路");

	cfg = default_cfg();
	cfg.dai_cfg.i2s.options = I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER;
	zassert_equal(audio_codec_configure(codec_dev, &cfg), -ENOTSUP,
		      "编解码器只能作从机, 缺少从机选项应当报错");
}

ZTEST(es8311_codec, test_output_volume_property)
{
	audio_property_value_t value = {.vol = 0};

	zassert_ok(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME,
					    AUDIO_CHANNEL_ALL, value));
	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_32), VOLUME_ZERO_DB_REG, "0 dB 的寄存器值");

	value.vol = -20;
	zassert_ok(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME,
					    AUDIO_CHANNEL_ALL, value));
	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_32), VOLUME_ZERO_DB_REG - 40,
		      "-20 dB 的寄存器值");

	value.vol = 32;
	zassert_ok(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME,
					    AUDIO_CHANNEL_ALL, value));
	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_32), 0xFF, "+32 dB 的寄存器值");

	value.vol = 33;
	zassert_equal(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME,
					       AUDIO_CHANNEL_ALL, value),
		      -EINVAL, "超出范围的音量应当报错");

	value.vol = -96;
	zassert_equal(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME,
					       AUDIO_CHANNEL_ALL, value),
		      -EINVAL, "低于下限的音量应当报错");

	value.vol = 6;
	zassert_ok(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_INPUT_VOLUME,
					    AUDIO_CHANNEL_ALL, value));
	zassert_equal(es8311_emul_reg(codec_emul, REG_ADC_17), VOLUME_ZERO_DB_REG + 12,
		      "输入音量寄存器值");
}

ZTEST(es8311_codec, test_mute_properties)
{
	audio_property_value_t value = {.mute = true};

	zassert_ok(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_MUTE,
					    AUDIO_CHANNEL_ALL, value));
	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_31) & DAC_31_MUTE, DAC_31_MUTE,
		      "输出静音位应当置位");

	value.mute = false;
	zassert_ok(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_MUTE,
					    AUDIO_CHANNEL_ALL, value));
	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_31) & DAC_31_MUTE, 0,
		      "输出静音位应当清除");

	value.mute = true;
	zassert_ok(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_INPUT_MUTE,
					    AUDIO_CHANNEL_ALL, value));
	zassert_equal(es8311_emul_reg(codec_emul, REG_SDP_OUT) & SDP_MUTE, SDP_MUTE,
		      "输入静音位应当置位");

	value.mute = false;
	zassert_ok(audio_codec_set_property(codec_dev, AUDIO_PROPERTY_INPUT_MUTE,
					    AUDIO_CHANNEL_ALL, value));
	zassert_equal(es8311_emul_reg(codec_emul, REG_SDP_OUT) & SDP_MUTE, 0,
		      "输入静音位应当清除");
}

ZTEST(es8311_codec, test_playback_controls_amplifier)
{
	audio_codec_start_output(codec_dev);

	zassert_equal(gpio_emul_output_get_dt(&amp_gpio), 1,
		      "播放开始时功放应当使能");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_0D), SYS_0D_ANALOG_ON,
		      "模拟部分应当上电");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_12), SYS_12_DAC_ON, "DAC 应当上电");
	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_31) & DAC_31_MUTE, 0,
		      "播放开始时应当解除静音");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SDP_IN) & SDP_MUTE, 0,
		      "播放开始时应当解除数字静音");
	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_37), 0x48,
		      "音量变化应当按 0.25dB/32LRCK 渐变");

	audio_codec_stop_output(codec_dev);

	zassert_equal(gpio_emul_output_get_dt(&amp_gpio), 0,
		      "播放结束时功放应当关闭");
	zassert_equal(es8311_emul_reg(codec_emul, REG_DAC_31) & DAC_31_MUTE, DAC_31_MUTE,
		      "播放结束时应当静音");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_12), SYS_12_DAC_OFF, "DAC 应当断电");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_0D), SYS_0D_ANALOG_OFF,
		      "无方向在用时时模拟部分应当断电");
}

ZTEST(es8311_codec, test_capture_path_and_shared_analog_power)
{
	zassert_ok(audio_codec_start(codec_dev, AUDIO_DAI_DIR_RX), "开始采集应当成功");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_0E), SYS_0E_ADC_ON,
		      "PGA 与 ADC 调制器应当上电");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_14), SYS_14_MIC1_PGA_30DB,
		      "应当选中差分 MIC1 输入");
	zassert_equal(es8311_emul_reg(codec_emul, REG_ADC_15), 0x40, "ADC 音量应当带软渐变");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SDP_OUT) & SDP_MUTE, 0,
		      "采集开始时应当解除数字静音");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_0D), SYS_0D_ANALOG_ON,
		      "采集开始时模拟部分应当上电");

	/* 采集进行中添加播放, 播放结束后模拟部分必须保持上电 */
	audio_codec_start_output(codec_dev);
	zassert_equal(gpio_emul_output_get_dt(&amp_gpio), 1,
		      "播放开始时功放应当使能");

	audio_codec_stop_output(codec_dev);
	zassert_equal(gpio_emul_output_get_dt(&amp_gpio), 0,
		      "播放结束时功放应当关闭");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_0D), SYS_0D_ANALOG_ON,
		      "采集仍在进行, 模拟部分不得断电");

	zassert_ok(audio_codec_stop(codec_dev, AUDIO_DAI_DIR_RX), "停止采集应当成功");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SDP_OUT) & SDP_MUTE, SDP_MUTE,
		      "停止采集后应当静音");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_0E), SYS_0E_ADC_OFF,
		      "停止采集后 PGA 与调制器应当断电");
	zassert_equal(es8311_emul_reg(codec_emul, REG_SYS_0D), SYS_0D_ANALOG_OFF,
		      "所有方向停止后模拟部分应当断电");
}

ZTEST_SUITE(es8311_codec, NULL, NULL, es8311_before_each, NULL, NULL);
