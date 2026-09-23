/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 立创·实战派 ESP32-C3 开发板 V1.3 的主固件: 启动后向控制台打印一行板名。
 * 引脚与网络对照见 doc/05-reference/hardware/SCH_ESP32-C3-V1_3.md。
 */

#include <stdio.h>

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	return 0;
}
