/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ES8311 的 I2C 模拟器, 在 native_sim 上代替真实器件。
 *
 * 上电默认值取自 ES8311 Datasheet Rev 8.0 §8 的寄存器清单, 驱动写入的
 * 寄存器地址与取值由测试用例逐条核对。
 */

#define DT_DRV_COMPAT everest_es8311

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include "es8311_emul.h"

LOG_MODULE_REGISTER(es8311_emul, LOG_LEVEL_INF);

/* 数据手册中上电默认值非零的寄存器 */
static const struct {
	uint8_t reg;
	uint8_t val;
} es8311_emul_defaults[] = {
	{0x00, 0x1F},
	{0x03, 0x10},
	{0x04, 0x10},
	{0x06, 0x03},
	{0x08, 0xFF},
	{0x0C, 0x20},
	{0x0D, 0xFC},
	{0x0E, 0x6A},
	{0x10, 0x13},
	{0x11, 0x7C},
	{0x12, 0x02},
	{0x13, 0x40},
	{0x14, 0x10},
	{0x16, 0x04},
	{0x1B, 0x0C},
	{0x1C, 0x4C},
	{0x37, 0x08},
	{0xFD, 0x83},
	{0xFE, 0x11},
};

struct es8311_emul_data {
	uint8_t regs[ES8311_EMUL_REG_COUNT];
};

uint8_t es8311_emul_reg(const struct emul *target, uint8_t reg)
{
	const struct es8311_emul_data *data = target->data;

	return data->regs[reg];
}

static int es8311_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
				int addr)
{
	struct es8311_emul_data *data = target->data;
	uint8_t reg_addr;

	ARG_UNUSED(addr);

	if (num_msgs == 1 && (msgs[0].flags & I2C_MSG_READ) == 0) {
		/* 写: 首字节为寄存器地址, 次字节为数据 */
		if (msgs[0].len != 2U) {
			return -EINVAL;
		}

		reg_addr = msgs[0].buf[0];
		data->regs[reg_addr] = msgs[0].buf[1];

		return 0;
	}

	if (num_msgs == 2 && (msgs[0].flags & I2C_MSG_READ) == 0 &&
	    (msgs[1].flags & I2C_MSG_READ) != 0) {
		/* 读: 先写寄存器地址, 再读出数据 */
		if (msgs[0].len != 1U) {
			return -EINVAL;
		}

		reg_addr = msgs[0].buf[0];

		for (uint16_t idx = 0; idx < msgs[1].len; idx++) {
			msgs[1].buf[idx] = data->regs[(reg_addr + idx) % ES8311_EMUL_REG_COUNT];
		}

		return 0;
	}

	LOG_ERR("不支持的 I2C 传输: %d 段", num_msgs);

	return -EINVAL;
}

static const struct i2c_emul_api es8311_emul_api = {
	.transfer = es8311_emul_transfer,
};

static int es8311_emul_init(const struct emul *target, const struct device *parent)
{
	struct es8311_emul_data *data = target->data;

	memset(data->regs, 0, sizeof(data->regs));

	for (size_t idx = 0; idx < ARRAY_SIZE(es8311_emul_defaults); idx++) {
		data->regs[es8311_emul_defaults[idx].reg] = es8311_emul_defaults[idx].val;
	}

	LOG_INF("%s 上的 ES8311 模拟器就绪", parent->name);

	return 0;
}

#define ES8311_EMUL_DEFINE(inst)							\
	static struct es8311_emul_data es8311_emul_data_##inst;				\
	EMUL_DT_INST_DEFINE(inst, es8311_emul_init, &es8311_emul_data_##inst, NULL,	\
			    (struct i2c_emul_api *)&es8311_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(ES8311_EMUL_DEFINE)
