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
#include <asm/io.h>
#include <linux/delay.h>

/* GMAC1 MDIO registers (dwmac 4.20a) - used to wake the PHY up before
 * the kernel probes it. */
#define GMAC1_BASE		0xfe010000
#define GMAC_MDIO_ADDR		(GMAC1_BASE + 0x200)
#define GMAC_MDIO_DATA		(GMAC1_BASE + 0x204)

#define MII_BUSY		BIT(0)
#define MII_GMAC4_READ		(3 << 2)	/* bits 3:2 = 11 */
#define MII_PHY_ADDR_SHIFT	21
#define MII_PHY_REG_SHIFT	16

/* GPIO4 PC2 - PHY reset, ACTIVE_LOW.  Linux dwmac-rk only pulses this
 * once during mdio register and then releases it; some boards need the
 * line held deasserted while the PHY settles. */
#define GPIO4_BASE		0xfe770000
#define GPIO_SWPORTA_DR		0x00
#define GPIO_SWPORTA_DDR	0x04
#define GPIO4_C2_BIT		BIT(18)

static void lb2004_phy_wakeup(void)
{
	u32 val;
	int i;

	/* 1. deassert PHY reset (GPIO4 PC2, active low => set bit = high) */
	writel(readl(GPIO4_BASE + GPIO_SWPORTA_DDR) | GPIO4_C2_BIT,
	       GPIO4_BASE + GPIO_SWPORTA_DDR);
	writel(readl(GPIO4_BASE + GPIO_SWPORTA_DR) | GPIO4_C2_BIT,
	       GPIO4_BASE + GPIO_SWPORTA_DR);

	/* 2. let the PHY settle after reset release */
	mdelay(50);

	/* 3. poke the MDIO bus: read PHY id1/id2 (reg 2/3, phy addr 0).
	 *    A dummy read is exactly what the PHY needs to start
	 *    responding - the STE101P workaround in the Linux driver does
	 *    the same thing. */
	for (i = 0; i < 3; i++) {
		/* wait for the MDIO bus to be idle */
		if (readl(GMAC_MDIO_ADDR) & MII_BUSY)
			mdelay(2);
	}

	/* read PHY id1 (reg 2) */
	writel((0 << MII_PHY_ADDR_SHIFT) | (2 << MII_PHY_REG_SHIFT) |
	       MII_BUSY | MII_GMAC4_READ, GMAC_MDIO_ADDR);
	for (i = 0; i < 100; i++) {
		if (!(readl(GMAC_MDIO_ADDR) & MII_BUSY))
			break;
		udelay(10);
	}
	val = readl(GMAC_MDIO_DATA);
	printf("lb2004-phy: id1=0x%04x\n", val & 0xffff);

	/* read PHY id2 (reg 3) */
	writel((0 << MII_PHY_ADDR_SHIFT) | (3 << MII_PHY_REG_SHIFT) |
	       MII_BUSY | MII_GMAC4_READ, GMAC_MDIO_ADDR);
	for (i = 0; i < 100; i++) {
		if (!(readl(GMAC_MDIO_ADDR) & MII_BUSY))
			break;
		udelay(10);
	}
	val = readl(GMAC_MDIO_DATA);
	printf("lb2004-phy: id2=0x%04x\n", val & 0xffff);
}


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
	lb2004_phy_wakeup();
	return 0;
}
