// SPDX-License-Identifier: GPL-2.0+
/*
 * LB2004 (RK3566) board code - independent of the Z96A RK3568 board.
 *
 * Recovery / Loader key: measured on a working Debian LB2004 board
 * (2026-09-16).  The key is on SARADC channel 0 (vref 1.8 V, 10-bit).
 *   idle       : raw = 1023 (1.800 V, full scale)
 *   pressed    : raw = 19..23 (~0.035 V) - very close to ground
 * So the press band is far below any other channel: any raw < 100 is
 * an unambiguous press, and > 900 is an unambiguous release.  The
 * Z96A board.c used 90..200 which never matched this board's ~20.
 */

#include <adc.h>
#include <dm.h>
#include <env.h>
#include <stdio.h>

int rockchip_dnl_key_pressed(void)
{
	unsigned int raw = ~0U;
	int ret;

	ret = adc_channel_single_shot("saradc", 0, &raw);
	if (ret)
		ret = adc_channel_single_shot("saradc@fe720000", 0, &raw);

	printf("lb2004-dnl-key: adc_ret=%d raw=%u\n", ret, raw);

	if (ret)
		return false;

	/* measured press = 19..23; use a wide band with big margins on
	 * both sides (idle 1023, unused channels ~540).  Anything under
	 * 100 is a press. */
	if (raw < 100) {
		printf("lb2004-dnl-key: Recovery pressed (raw=%u)\n", raw);
		return true;
	}

	return false;
}

int rk_board_late_init(void)
{
	return 0;
}
