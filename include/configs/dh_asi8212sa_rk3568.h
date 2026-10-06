/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Dahua DH-ASI8212SA (RK3568 door-station board)
 *
 * Serial console only (UART2, 1.5MBaud - factory debug UART).
 *
 * Independent of the Z96A RK3568 / LB2004 RK3566 board configs: this board
 * has its PMIC on a private I2C bus (no in-DT PMIC), eMMC on sdhci@fe310000
 * and the Recovery key on SARADC ch0 pressing to < 9 uV.
 */

#ifndef __DH_ASI8212SA_RK3568_H
#define __DH_ASI8212SA_RK3568_H

#define ROCKCHIP_DEVICE_SETTINGS \
			"stdout=serial\0" \
			"stderr=serial\0"

#include <configs/rk3568_common.h>

#endif
