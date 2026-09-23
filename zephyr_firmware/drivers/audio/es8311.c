/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Everest Semiconductor ES8311 低功耗单声道音频编解码器驱动。
 *
 * 数据面的 PCM 由 I2S 控制器承载, 本驱动负责控制面: 时钟链、串行数据格式、
 * 音量、静音、模拟通路上下电以及扬声器功放的开关。
 *
 * 寄存器定义与取值依据:
 *   - ES8311 Datasheet Rev 8.0 §8 CONFIGURATION REGISTER DEFINITION
 *   - ES8311 User Guide Rev 1.11 §8 时钟、§9 上下电、§10 ADC、§11 DAC
 *   - ESP-ADF components/audio_hal/driver/es8311/es8311.c
 */

#define DT_DRV_COMPAT everest_es8311

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <stdint.h>

LOG_MODULE_REGISTER(everest_es8311, CONFIG_AUDIO_CODEC_LOG_LEVEL);

/* 复位与状态机 */
#define REG_RESET                    0x00
#define RESET_CSM_ON                 BIT(7)

/* 时钟管理 */
#define REG_CLKMGR_01                0x01
#define CLKMGR_MCLK_SEL_BCLK         BIT(7)
#define CLKMGR_MCLK_ON               BIT(5)
#define CLKMGR_BCLK_ON               BIT(4)
#define CLKMGR_CLKADC_ON             BIT(3)
#define CLKMGR_CLKDAC_ON             BIT(2)
#define CLKMGR_ANACLKADC_ON          BIT(1)
#define CLKMGR_ANACLKDAC_ON          BIT(0)
#define CLKMGR_ALL_ON                (CLKMGR_MCLK_ON | CLKMGR_BCLK_ON | CLKMGR_CLKADC_ON | \
				      CLKMGR_CLKDAC_ON | CLKMGR_ANACLKADC_ON | CLKMGR_ANACLKDAC_ON)

#define REG_CLKMGR_02                0x02
#define REG_CLKMGR_03                0x03
#define REG_CLKMGR_04                0x04
#define REG_CLKMGR_05                0x05

/* 串行数据端口 */
#define REG_SDP_IN                   0x09
#define REG_SDP_OUT                  0x0A
#define SDP_MUTE                     BIT(6)
#define SDP_WL_SHIFT                 2
#define SDP_FMT_I2S                  0
#define SDP_FMT_LEFT_JUSTIFIED       1

/* 系统控制 */
#define REG_SYS_0B                   0x0B
#define REG_SYS_0C                   0x0C
#define REG_SYS_0D                   0x0D
#define SYS_0D_ANALOG_ON             0x01
#define SYS_0D_ANALOG_OFF            0xFC
#define REG_SYS_0E                   0x0E
#define SYS_0E_ADC_ON                0x02
#define SYS_0E_ADC_OFF               0x6A
#define REG_SYS_10                   0x10
#define REG_SYS_11                   0x11
#define REG_SYS_12                   0x12
#define SYS_12_DAC_ON                0x00
#define SYS_12_DAC_OFF               0x02
#define REG_SYS_13                   0x13
#define SYS_13_HP_OUT_DRIVE          BIT(4)
#define REG_SYS_14                   0x14
#define SYS_14_LINSEL_MIC1           BIT(4)
#define SYS_14_PGA_GAIN_30DB         0x0A

/* ADC */
#define REG_ADC_15                   0x15
/* ADC_RAMPRATE = 0.25dB/32LRCK, 音量变化按渐变推进, 避免阶跃产生爆音 */
#define ADC_15_RAMP_0P25DB_32LRCK    0x40
#define REG_ADC_16                   0x16
#define ADC_16_SYNC_STANDARD         BIT(5)
#define ADC_16_SCALE_24DB            0x04
#define REG_ADC_17                   0x17
#define ADC_17_DEFAULT_0DB           0xBF
#define REG_ADC_1B                   0x1B
#define REG_ADC_1C                   0x1C

/* DAC */
#define REG_DAC_31                   0x31
#define DAC_31_MUTE                  0x60
#define REG_DAC_32                   0x32
#define REG_DAC_37                   0x37
/* DAC_RAMPRATE 位 7:4 = 0.25dB/32LRCK, 位 3 保持 DAC 均衡器旁路, 音量变化按渐变推进 */
#define DAC_37_RAMP_0P25DB_32LRCK    (0x40 | BIT(3))

/* GPIO 与通用控制 */
#define REG_GPIO_44                  0x44
#define GPIO_44_I2C_WL               BIT(3)
#define REG_GP_45                    0x45
#define GP_45_PULLUP_OFF             BIT(0)

/* 芯片识别 */
#define REG_CHIP_ID1                 0xFD
#define REG_CHIP_ID2                 0xFE
#define REG_CHIP_VER                 0xFF
#define CHIP_ID1_VALUE               0x83
#define CHIP_ID2_VALUE               0x11

/* I2S 控制器在 16/32 位字长下输出的 MCLK 固定为 256 倍采样率 */
#define MCLK_FS_RATIO                256

/* 音量寄存器标度: 0xBF 为 0dB, 步进 0.5dB, 0x00 为 -95.5dB, 0xFF 为 +32dB */
#define VOLUME_ZERO_DB_REG           0xBF
#define VOLUME_MIN_DB                (-95)
#define VOLUME_MAX_DB                32

struct es8311_rate_cfg {
	uint32_t rate;
	uint8_t adc_osr;
	uint8_t dac_osr;
};

/* 256 倍采样率下各标准采样率的 ADC/DAC 过采样率, 取自 ESP-ADF 参考驱动的系数表 */
static const struct es8311_rate_cfg es8311_rate_table[] = {
	{  8000, 0x10, 0x20},
	{ 11025, 0x10, 0x10},
	{ 12000, 0x10, 0x10},
	{ 16000, 0x10, 0x20},
	{ 22050, 0x10, 0x10},
	{ 24000, 0x10, 0x10},
	{ 32000, 0x10, 0x10},
	{ 44100, 0x10, 0x10},
	{ 48000, 0x10, 0x10},
};

struct es8311_reg_val {
	uint8_t reg;
	uint8_t val;
};

/*
 * 固定初始化序列。模拟偏置(0x10/0x11)、ADC 高通系数(0x1B/0x1C)、输出驱动(0x13)
 * 取参考驱动的取值, 手册中标注为内部使用, 未公开字段含义。
 *
 * 0x44 连写两次: 上电后第一条 I2C 写入可能丢失, 参考驱动据此把首条写入重复一次。
 */
static const struct es8311_reg_val es8311_init_table[] = {
	{REG_GPIO_44, GPIO_44_I2C_WL},
	{REG_GPIO_44, GPIO_44_I2C_WL},
	{REG_CLKMGR_01, CLKMGR_MCLK_ON | CLKMGR_BCLK_ON},
	{REG_CLKMGR_02, 0x00},
	{REG_CLKMGR_05, 0x00},
	{REG_ADC_16, ADC_16_SYNC_STANDARD | ADC_16_SCALE_24DB},
	{REG_SYS_0B, 0x00},
	{REG_SYS_0C, 0x00},
	{REG_SYS_10, 0x1F},
	{REG_SYS_11, 0x7F},
	{REG_RESET, RESET_CSM_ON},
	{REG_CLKMGR_01, CLKMGR_ALL_ON},
	{REG_SYS_13, SYS_13_HP_OUT_DRIVE},
	{REG_ADC_1B, 0x0A},
	{REG_ADC_1C, 0x6A},
	{REG_SYS_0D, SYS_0D_ANALOG_OFF},
	{REG_SYS_0E, SYS_0E_ADC_OFF},
	{REG_SYS_12, SYS_12_DAC_OFF},
	{REG_DAC_31, DAC_31_MUTE},
};

struct es8311_config {
	struct i2c_dt_spec i2c;
	const struct device *pa;
};

struct es8311_data {
	audio_route_t route;
	audio_dai_dir_t active_dirs;
	int dac_volume_db;
	int adc_volume_db;
	bool configured;
};

static int es8311_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct es8311_config *cfg = dev->config;

	return i2c_reg_write_byte_dt(&cfg->i2c, reg, val);
}

static int es8311_read_reg(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct es8311_config *cfg = dev->config;

	return i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
}

static uint8_t es8311_volume_to_reg(int volume_db)
{
	return (uint8_t)(VOLUME_ZERO_DB_REG + (volume_db * 2));
}

static int es8311_pa_enable(const struct device *dev, bool enable)
{
	const struct es8311_config *cfg = dev->config;
	int ret;

	if (cfg->pa == NULL) {
		return 0;
	}

	if (!device_is_ready(cfg->pa)) {
		LOG_ERR("功放设备 %s 未就绪", cfg->pa->name);
		return -ENODEV;
	}

	ret = enable ? regulator_enable(cfg->pa) : regulator_disable(cfg->pa);
	if (ret < 0) {
		LOG_ERR("功放%s失败: %d", enable ? "使能" : "关闭", ret);
	}

	return ret;
}

/* 模拟公共块被收发两个方向共用, 任一方在用就必须保持上电 */
static bool es8311_any_dir_active(const struct device *dev)
{
	const struct es8311_data *data = dev->data;

	return data->active_dirs != 0;
}

static int es8311_tx_enable(const struct device *dev, bool enable)
{
	const struct es8311_config *cfg = dev->config;
	struct es8311_data *data = dev->data;
	int ret;

	if (enable) {
		if (!es8311_any_dir_active(dev)) {
			ret = es8311_write_reg(dev, REG_SYS_0D, SYS_0D_ANALOG_ON);
			if (ret < 0) {
				return ret;
			}
		}

		ret = es8311_write_reg(dev, REG_SYS_12, SYS_12_DAC_ON);
		if (ret < 0) {
			return ret;
		}

		ret = es8311_write_reg(dev, REG_DAC_37, DAC_37_RAMP_0P25DB_32LRCK);
		if (ret < 0) {
			return ret;
		}

		/* 功放先使能, 此时 DAC 仍处于静音, 功放不会放大上电瞬态 */
		ret = es8311_pa_enable(dev, true);
		if (ret < 0) {
			return ret;
		}

		ret = i2c_reg_update_byte_dt(&cfg->i2c, REG_DAC_31, DAC_31_MUTE, 0x00);
		if (ret < 0) {
			return ret;
		}

		ret = i2c_reg_update_byte_dt(&cfg->i2c, REG_SDP_IN, SDP_MUTE, 0x00);
		if (ret < 0) {
			return ret;
		}

		data->active_dirs |= AUDIO_DAI_DIR_TX;

		return 0;
	}

	ret = i2c_reg_update_byte_dt(&cfg->i2c, REG_DAC_31, DAC_31_MUTE, DAC_31_MUTE);
	if (ret < 0) {
		return ret;
	}

	ret = i2c_reg_update_byte_dt(&cfg->i2c, REG_SDP_IN, SDP_MUTE, SDP_MUTE);
	if (ret < 0) {
		return ret;
	}

	ret = es8311_pa_enable(dev, false);
	if (ret < 0) {
		return ret;
	}

	ret = es8311_write_reg(dev, REG_SYS_12, SYS_12_DAC_OFF);
	if (ret < 0) {
		return ret;
	}

	data->active_dirs &= ~AUDIO_DAI_DIR_TX;

	if (!es8311_any_dir_active(dev)) {
		ret = es8311_write_reg(dev, REG_SYS_0D, SYS_0D_ANALOG_OFF);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int es8311_rx_enable(const struct device *dev, bool enable)
{
	const struct es8311_config *cfg = dev->config;
	struct es8311_data *data = dev->data;
	int ret;

	if (enable) {
		if (!es8311_any_dir_active(dev)) {
			ret = es8311_write_reg(dev, REG_SYS_0D, SYS_0D_ANALOG_ON);
			if (ret < 0) {
				return ret;
			}
		}

		ret = es8311_write_reg(dev, REG_SYS_0E, SYS_0E_ADC_ON);
		if (ret < 0) {
			return ret;
		}

		ret = es8311_write_reg(dev, REG_SYS_14, SYS_14_LINSEL_MIC1 | SYS_14_PGA_GAIN_30DB);
		if (ret < 0) {
			return ret;
		}

		ret = es8311_write_reg(dev, REG_ADC_15, ADC_15_RAMP_0P25DB_32LRCK);
		if (ret < 0) {
			return ret;
		}

		ret = es8311_write_reg(dev, REG_ADC_17, es8311_volume_to_reg(data->adc_volume_db));
		if (ret < 0) {
			return ret;
		}

		ret = i2c_reg_update_byte_dt(&cfg->i2c, REG_SDP_OUT, SDP_MUTE, 0x00);
		if (ret < 0) {
			return ret;
		}

		data->active_dirs |= AUDIO_DAI_DIR_RX;

		return 0;
	}

	ret = i2c_reg_update_byte_dt(&cfg->i2c, REG_SDP_OUT, SDP_MUTE, SDP_MUTE);
	if (ret < 0) {
		return ret;
	}

	ret = es8311_write_reg(dev, REG_SYS_0E, SYS_0E_ADC_OFF);
	if (ret < 0) {
		return ret;
	}

	data->active_dirs &= ~AUDIO_DAI_DIR_RX;

	if (!es8311_any_dir_active(dev)) {
		ret = es8311_write_reg(dev, REG_SYS_0D, SYS_0D_ANALOG_OFF);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int es8311_word_length_code(uint16_t word_size)
{
	switch (word_size) {
	case 16:
		return 3;
	case 18:
		return 2;
	case 20:
		return 1;
	case 24:
		return 0;
	case 32:
		return 4;
	default:
		return -EINVAL;
	}
}

static const struct es8311_rate_cfg *es8311_rate_lookup(uint32_t rate)
{
	for (size_t idx = 0; idx < ARRAY_SIZE(es8311_rate_table); idx++) {
		if (es8311_rate_table[idx].rate == rate) {
			return &es8311_rate_table[idx];
		}
	}

	return NULL;
}

static int es8311_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	struct es8311_data *data = dev->data;
	const struct es8311_rate_cfg *rate_cfg;
	uint32_t format;
	int word_length;
	int ret;

	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->dai_type != AUDIO_DAI_TYPE_I2S &&
	    cfg->dai_type != AUDIO_DAI_TYPE_LEFT_JUSTIFIED) {
		LOG_ERR("不支持的 DAI 类型 %d", cfg->dai_type);
		return -ENOTSUP;
	}

	if (cfg->dai_route != AUDIO_ROUTE_PLAYBACK && cfg->dai_route != AUDIO_ROUTE_CAPTURE &&
	    cfg->dai_route != AUDIO_ROUTE_PLAYBACK_CAPTURE) {
		LOG_ERR("不支持的音频通路 %d", cfg->dai_route);
		return -ENOTSUP;
	}

	/* 本板 codec 只能作从机, 时钟由 I2S 控制器提供 */
	if ((cfg->dai_cfg.i2s.options & I2S_OPT_BIT_CLK_TARGET) == 0 ||
	    (cfg->dai_cfg.i2s.options & I2S_OPT_FRAME_CLK_TARGET) == 0) {
		LOG_ERR("ES8311 需要以从机模式工作, 请设置 I2S_OPT_BIT_CLK_TARGET 与 "
			"I2S_OPT_FRAME_CLK_TARGET");
		return -ENOTSUP;
	}

	/* 时钟链按 256 倍采样率配置, 与 I2S 控制器输出的 MCLK 一致 */
	if (cfg->mclk_freq != cfg->dai_cfg.i2s.frame_clk_freq * MCLK_FS_RATIO) {
		LOG_ERR("MCLK 应为 %u 倍采样率, 即 %u Hz, 实际 %u Hz", MCLK_FS_RATIO,
			cfg->dai_cfg.i2s.frame_clk_freq * MCLK_FS_RATIO, cfg->mclk_freq);
		return -EINVAL;
	}

	rate_cfg = es8311_rate_lookup(cfg->dai_cfg.i2s.frame_clk_freq);
	if (rate_cfg == NULL) {
		LOG_ERR("不支持的采样率 %u Hz", cfg->dai_cfg.i2s.frame_clk_freq);
		return -EINVAL;
	}

	word_length = es8311_word_length_code(cfg->dai_cfg.i2s.word_size);
	if (word_length < 0) {
		LOG_ERR("不支持的字长 %u", cfg->dai_cfg.i2s.word_size);
		return -EINVAL;
	}

	if (cfg->dai_cfg.i2s.channels != 1 && cfg->dai_cfg.i2s.channels != 2) {
		LOG_ERR("不支持的声道数 %u", cfg->dai_cfg.i2s.channels);
		return -EINVAL;
	}

	data->route = cfg->dai_route;

	/* 先复位数字与时钟管理模块, 保证寄存器处于确定的初值 */
	ret = es8311_write_reg(dev, REG_RESET, 0x1F);
	if (ret < 0) {
		return ret;
	}

	for (size_t idx = 0; idx < ARRAY_SIZE(es8311_init_table); idx++) {
		ret = es8311_write_reg(dev, es8311_init_table[idx].reg, es8311_init_table[idx].val);
		if (ret < 0) {
			LOG_ERR("初始化寄存器 0x%02X 失败: %d", es8311_init_table[idx].reg, ret);
			return ret;
		}
	}

	/*
	 * 内部主时钟 = MCLK, ADC/DAC 时钟 = 内部主时钟 = 256 倍采样率,
	 * 满足手册对 ADC(单速 240/256 倍) 与 DAC(256 倍) 时钟比例的要求。
	 * 从机模式下 BCLK 与 LRCK 分频器不生效, 由芯片自动检测比例, 无需配置。
	 */
	ret = es8311_write_reg(dev, REG_CLKMGR_03, rate_cfg->adc_osr);
	if (ret < 0) {
		return ret;
	}

	ret = es8311_write_reg(dev, REG_CLKMGR_04, rate_cfg->dac_osr);
	if (ret < 0) {
		return ret;
	}

	format = (cfg->dai_type == AUDIO_DAI_TYPE_I2S) ? SDP_FMT_I2S : SDP_FMT_LEFT_JUSTIFIED;

	/* 配置期间两个方向都保持静音, 等各自 start 时再解除 */
	ret = es8311_write_reg(dev, REG_SDP_IN,
			       (word_length << SDP_WL_SHIFT) | format | SDP_MUTE);
	if (ret < 0) {
		return ret;
	}

	ret = es8311_write_reg(dev, REG_SDP_OUT,
			       (word_length << SDP_WL_SHIFT) | format | SDP_MUTE);
	if (ret < 0) {
		return ret;
	}

	ret = es8311_write_reg(dev, REG_DAC_32, es8311_volume_to_reg(data->dac_volume_db));
	if (ret < 0) {
		return ret;
	}

	ret = es8311_write_reg(dev, REG_ADC_17, es8311_volume_to_reg(data->adc_volume_db));
	if (ret < 0) {
		return ret;
	}

	data->configured = true;
	LOG_DBG("配置完成: %u Hz, %u bit, %u 声道, MCLK %u Hz",
		cfg->dai_cfg.i2s.frame_clk_freq, cfg->dai_cfg.i2s.word_size,
		cfg->dai_cfg.i2s.channels, cfg->mclk_freq);

	return 0;
}

static int es8311_start(const struct device *dev, audio_dai_dir_t dir)
{
	struct es8311_data *data = dev->data;
	int ret;

	if (!data->configured) {
		return -EIO;
	}

	if ((dir & AUDIO_DAI_DIR_TX) != 0) {
		if (data->route == AUDIO_ROUTE_CAPTURE) {
			LOG_ERR("配置的通路不含播放方向");
			return -ENOTSUP;
		}

		if ((data->active_dirs & AUDIO_DAI_DIR_TX) == 0) {
			ret = es8311_tx_enable(dev, true);
			if (ret < 0) {
				return ret;
			}
		}
	}

	if ((dir & AUDIO_DAI_DIR_RX) != 0) {
		if (data->route == AUDIO_ROUTE_PLAYBACK) {
			LOG_ERR("配置的通路不含采集方向");
			return -ENOTSUP;
		}

		if ((data->active_dirs & AUDIO_DAI_DIR_RX) == 0) {
			ret = es8311_rx_enable(dev, true);
			if (ret < 0) {
				return ret;
			}
		}
	}

	return 0;
}

static int es8311_stop(const struct device *dev, audio_dai_dir_t dir)
{
	struct es8311_data *data = dev->data;
	int ret;

	if ((dir & AUDIO_DAI_DIR_TX) != 0 && (data->active_dirs & AUDIO_DAI_DIR_TX) != 0) {
		ret = es8311_tx_enable(dev, false);
		if (ret < 0) {
			return ret;
		}
	}

	if ((dir & AUDIO_DAI_DIR_RX) != 0 && (data->active_dirs & AUDIO_DAI_DIR_RX) != 0) {
		ret = es8311_rx_enable(dev, false);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

/* 播放方向的便捷入口, 与 audio_codec_start(dev, AUDIO_DAI_DIR_TX) 等价 */
static void es8311_start_output(const struct device *dev)
{
	int ret = es8311_start(dev, AUDIO_DAI_DIR_TX);

	if (ret < 0) {
		LOG_ERR("开启播放通路失败: %d", ret);
	}
}

static void es8311_stop_output(const struct device *dev)
{
	int ret = es8311_stop(dev, AUDIO_DAI_DIR_TX);

	if (ret < 0) {
		LOG_ERR("关闭播放通路失败: %d", ret);
	}
}

static int es8311_set_property(const struct device *dev, audio_property_t property,
			       audio_channel_t channel, audio_property_value_t val)
{
	const struct es8311_config *cfg = dev->config;
	struct es8311_data *data = dev->data;

	if (channel != AUDIO_CHANNEL_ALL) {
		return -ENOTSUP;
	}

	switch (property) {
	case AUDIO_PROPERTY_OUTPUT_VOLUME:
		if (val.vol < VOLUME_MIN_DB || val.vol > VOLUME_MAX_DB) {
			LOG_ERR("输出音量 %d dB 超出范围 [%d, %d]", val.vol, VOLUME_MIN_DB,
				VOLUME_MAX_DB);
			return -EINVAL;
		}

		data->dac_volume_db = val.vol;
		return es8311_write_reg(dev, REG_DAC_32, es8311_volume_to_reg(val.vol));

	case AUDIO_PROPERTY_OUTPUT_MUTE:
		return i2c_reg_update_byte_dt(&cfg->i2c, REG_DAC_31, DAC_31_MUTE,
					      val.mute ? DAC_31_MUTE : 0x00);

	case AUDIO_PROPERTY_INPUT_VOLUME:
		if (val.vol < VOLUME_MIN_DB || val.vol > VOLUME_MAX_DB) {
			LOG_ERR("输入音量 %d dB 超出范围 [%d, %d]", val.vol, VOLUME_MIN_DB,
				VOLUME_MAX_DB);
			return -EINVAL;
		}

		data->adc_volume_db = val.vol;
		return es8311_write_reg(dev, REG_ADC_17, es8311_volume_to_reg(val.vol));

	case AUDIO_PROPERTY_INPUT_MUTE:
		return i2c_reg_update_byte_dt(&cfg->i2c, REG_SDP_OUT, SDP_MUTE,
					      val.mute ? SDP_MUTE : 0x00);

	default:
		return -ENOTSUP;
	}
}

static int es8311_apply_properties(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* 属性在 set_property 时已写入器件, 无需二次提交 */
	return 0;
}

static DEVICE_API(audio_codec, es8311_api) = {
	.configure = es8311_configure,
	.start_output = es8311_start_output,
	.stop_output = es8311_stop_output,
	.start = es8311_start,
	.stop = es8311_stop,
	.set_property = es8311_set_property,
	.apply_properties = es8311_apply_properties,
};

static int es8311_init(const struct device *dev)
{
	const struct es8311_config *cfg = dev->config;
	struct es8311_data *data = dev->data;
	uint8_t chip_id1;
	uint8_t chip_id2;
	uint8_t chip_ver;
	int ret;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C 总线未就绪");
		return -ENODEV;
	}

	ret = es8311_read_reg(dev, REG_CHIP_ID1, &chip_id1);
	if (ret < 0) {
		LOG_ERR("读取芯片 ID 失败: %d", ret);
		return -ENODEV;
	}

	ret = es8311_read_reg(dev, REG_CHIP_ID2, &chip_id2);
	if (ret < 0) {
		return -ENODEV;
	}

	if (chip_id1 != CHIP_ID1_VALUE || chip_id2 != CHIP_ID2_VALUE) {
		LOG_ERR("芯片 ID 不符: 期望 0x%02X 0x%02X, 实际 0x%02X 0x%02X", CHIP_ID1_VALUE,
			CHIP_ID2_VALUE, chip_id1, chip_id2);
		return -ENODEV;
	}

	ret = es8311_read_reg(dev, REG_CHIP_VER, &chip_ver);
	if (ret < 0) {
		return -ENODEV;
	}

	data->active_dirs = 0;
	data->dac_volume_db = 0;
	data->adc_volume_db = 0;
	data->configured = false;

	LOG_INF("ES8311 已识别, ID 0x%02X%02X, 版本 0x%02X", chip_id1, chip_id2, chip_ver);

	return 0;
}

#define ES8311_PA_DEVICE(inst)								\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, pa_supply),				\
		    (DEVICE_DT_GET(DT_INST_PHANDLE(inst, pa_supply))), (NULL))

#define ES8311_INIT(inst)								\
	static const struct es8311_config es8311_config_##inst = {			\
		.i2c = I2C_DT_SPEC_INST_GET(inst),					\
		.pa = ES8311_PA_DEVICE(inst),						\
	};										\
	static struct es8311_data es8311_data_##inst;					\
	DEVICE_DT_INST_DEFINE(inst, es8311_init, NULL, &es8311_data_##inst,		\
			      &es8311_config_##inst, POST_KERNEL,			\
			      CONFIG_AUDIO_CODEC_INIT_PRIORITY, &es8311_api);

DT_INST_FOREACH_STATUS_OKAY(ES8311_INIT)
