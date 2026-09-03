/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Sunniwell Z96A RK3568 Laptop V2
 *
 * Serial console only (no video pipeline in U-Boot): keep stdout on
 * UART so the FIQ/kernel console handoff stays predictable.
 */

#ifndef __Z96A_RK3568_H
#define __Z96A_RK3568_H

#define ROCKCHIP_DEVICE_SETTINGS \
			"stdout=serial\0" \
			"stderr=serial\0"

#include <configs/rk3568_common.h>

#endif
