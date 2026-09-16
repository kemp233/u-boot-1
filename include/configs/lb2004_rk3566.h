/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * LB2004 (RK3566)
 *
 * Serial console only (no video pipeline in U-Boot): keep stdout on
 * UART so the FIQ/kernel console handoff stays predictable.
 *
 * Independent of the Z96A RK3568 board config: different SoC variant
 * and different Recovery-key thresholds (see
 * board/rockchip/lb2004_rk3566/board.c).
 */

#ifndef __LB2004_RK3566_H
#define __LB2004_RK3566_H

#define ROCKCHIP_DEVICE_SETTINGS \
			"stdout=serial\0" \
			"stderr=serial\0"

#include <configs/rk3568_common.h>

#endif
