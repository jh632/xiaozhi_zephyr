/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 立创·实战派 ESP32-C3 开发板 V1.3 启动自检: 打印板级信息并扫描 I2C 总线。
 * 扫描结果用于核对原理图整理出的引脚分配是否与实际硬件一致。
 * 引脚与网络对照见 doc/SCH_ESP32-C3-V1_3.md。
 */

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include <stdint.h>

/* I2C 7 位地址可用范围, 0x00-0x07 与 0x78-0x7F 为保留地址 */
#define I2C_SCAN_ADDR_FIRST 0x08U
#define I2C_SCAN_ADDR_LAST  0x77U

/* 探测时读取的寄存器地址, 各传感器均为只读的器件识别寄存器 */
#define I2C_PROBE_REG 0x00U

#define HEARTBEAT_PERIOD_S 10

/* 读一个字节判断从设备是否应答, 返回 0 表示存在 */
static int board_i2c_probe(const struct device *i2c_dev, uint16_t addr)
{
	uint8_t reg = I2C_PROBE_REG;
	uint8_t value;

	return i2c_write_read(i2c_dev, addr, &reg, sizeof(reg), &value, sizeof(value));
}

int main(void)
{
	const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	unsigned int found = 0U;
	unsigned int uptime_s = 0U;

	printk("\n===== 立创·实战派 ESP32-C3 开发板 V1.3 =====\n");
	printk("控制台 UART0: GPIO21 TX / GPIO20 RX, 经 CH343P 转 USB\n");
	printk("I2C: SDA=GPIO0, SCL=GPIO1, 板上 R4/R5 4.7kΩ 上拉到 3V3\n");

	if (!device_is_ready(i2c_dev)) {
		printk("[FAIL] I2C0 未就绪\n");
		return 0;
	}

	for (uint16_t addr = I2C_SCAN_ADDR_FIRST; addr <= I2C_SCAN_ADDR_LAST; addr++) {
		if (board_i2c_probe(i2c_dev, addr) != 0) {
			continue;
		}

		printk("I2C 从设备应答: 0x%02X\n", addr);
		found++;
	}

	printk("I2C 扫描结束: 共 %u 个从设备\n", found);

	while (true) {
		k_msleep(HEARTBEAT_PERIOD_S * 1000);
		uptime_s += HEARTBEAT_PERIOD_S;
		printk("运行中: %u 秒\n", uptime_s);
	}

	return 0;
}
