/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ES8311 的 I2C 模拟器接口, 供驱动测试读取模拟寄存器。
 */

#ifndef ES8311_EMUL_H_
#define ES8311_EMUL_H_

#include <zephyr/drivers/emul.h>

#include <stdint.h>

/* ES8311 寄存器地址范围 0x00-0xFF */
#define ES8311_EMUL_REG_COUNT 256U

/* 读取模拟寄存器值 */
uint8_t es8311_emul_reg(const struct emul *target, uint8_t reg);

#endif /* ES8311_EMUL_H_ */
