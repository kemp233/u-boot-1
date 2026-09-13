// SPDX-License-Identifier: GPL-2.0+
/*
 * Sunniwell Z96A RK3568 Laptop V2 — own board directory.
 *
 * This file provides the weak rk_board_late_init() hook so the
 * z96a-specific SC8886 charger / power-source selection runs in
 * board_late_init() (after all DM devices are probed and the I2C bus
 * is fully up), instead of compiling the generic evb_rk3568 board
 * file which shared unrelated state with other RK3568 targets.
 *
 * All I2C operations are wrapped so a missing chip never blocks the
 * rest of U-Boot: errors print a diagnostic and return 0.
 */

#include <adc.h>
#include <asm/io.h>
#include <dm.h>
#include <dm/device.h>
#include <dm/uclass.h>
#include <env.h>
#include <linux/delay.h>
#include <time.h>
#include <i2c.h>
#include <log.h>

#define SC8886_I2C_BUS		"i2c@fdd40000"
#define SC8886_I2C_ADDR		0x6b

#define SC8886_CHARGER_STATUS	0x20	/* bit15 AC_STAT, bit8 IN_OTG */
#define SC8886_PROCHOT_STATUS	0x21	/* bit1 STAT_BAT_REMOV */
#define SC8886_ADCIBAT		0x26	/* bit[6:0] OUTPUT_BAT_VOL */
#define SC8886_CHARGE_OPTION0	0x12	/* bit15 EN_LWPWR */
#define SC8886_CHARGE_OPTION2	0x32	/* bit12 EN_OTG */
#define SC8886_CHARGE_CURRENT	0x14	/* bits[12:6], 64 mA LSB */
#define SC8886_PROCHOT_OPT0	0x36	/* VSYS_VTH/ICRIT_DEG/PROCHOT_WIDTH */
#define SC8886_PROCHOT_OPT1	0x37	/* IBAT short / VSYS_REG thresholds */

/* factory values from SC8886 register dump (before any host writes) */
#define SC8886_PROCHOT_OPT0_VAL	0x65
#define SC8886_PROCHOT_OPT1_VAL	0x4a

#define SC8886_AC_STAT		BIT(15)
#define SC8886_BAT_REMOV	BIT(1)
#define SC8886_EN_LWPWR		BIT(15)
#define SC8886_EN_OTG		BIT(12)

/* battery voltage = 2880 mV + OUTPUT_BAT_VOL * 64 mV */
#define SC8886_BATVOL_BASE_MV	2880
#define SC8886_BATVOL_STEP_MV	64
#define SC8886_BATVOL_MASK	0x7f

/* treat battery as usable above this voltage */
#define SC8886_BAT_OK_MV	3000

static int sc8886_read16(struct udevice *chip, u8 reg, u16 *val)
{
	u8 buf[2];
	int ret;

	ret = dm_i2c_read(chip, reg, buf, 2);
	if (ret)
		return ret;

	*val = buf[0] | (buf[1] << 8);
	return 0;
}

static int sc8886_write8(struct udevice *chip, u8 reg, u8 val)
{
	return dm_i2c_write(chip, reg, &val, 1);
}

static int sc8886_update16(struct udevice *chip, u8 reg, u16 mask, u16 val)
{
	u16 cur;
	u8 buf[2];
	int ret;

	ret = sc8886_read16(chip, reg, &cur);
	if (ret)
		return ret;

	cur = (cur & ~mask) | (val & mask);
	buf[0] = cur & 0xff;
	buf[1] = (cur >> 8) & 0xff;

	return dm_i2c_write(chip, reg, buf, 2);
}

/*
 * Z96A volume-up / Recovery key: SARADC ch0, VERIFIED on hardware
 * 2026-09.  Idle = 1023 raw (1.8 V, full scale); pressing the key
 * pulls ch0 down to ~127 raw (~0.223 V).  ch2 stays at 1023 the whole
 * time (the factory Android DTB's ch2 claim does not match the real
 * board).  Band 90..200 covers the measured press point with margin
 * and is far from both the idle 1023 and the unused-channel 0.
 *
 * 2017-vendor-style RAW REGISTER read: no DM, no clk/regulator subsystem.
 * The SARADC block works on reset-default gate clocks (the vendor 2017
 * U-Boot relied on exactly this), so a direct register poke is immune to
 * any DM probe failure that silently killed the adc_channel_single_shot()
 * path. Register layout mirrors drivers/adc/rockchip-saradc.c v1.
 */
#define Z96A_SARADC_BASE		0xfe720000
#define Z96A_SARADC_DATA		0x00
#define Z96A_SARADC_CTRL		0x08
#define Z96A_SARADC_DLY_PU_SOC		0x0c
#define Z96A_SARADC_CTRL_POWER		BIT(3)
#define Z96A_SARADC_CTRL_IRQ_ENABLE	BIT(5)
#define Z96A_SARADC_CTRL_IRQ_STATUS	BIT(6)
#define Z96A_SARADC_CTRL_CHN_MASK	GENMASK(2, 0)

static int z96a_saradc_raw(int channel, unsigned int *raw)
{
	ulong start;

	/* 8 clock periods between power-up and start command */
	writel(8, (void *)(Z96A_SARADC_BASE + Z96A_SARADC_DLY_PU_SOC));
	writel(Z96A_SARADC_CTRL_POWER | (channel & Z96A_SARADC_CTRL_CHN_MASK) |
	       Z96A_SARADC_CTRL_IRQ_ENABLE,
	       (void *)(Z96A_SARADC_BASE + Z96A_SARADC_CTRL));

	start = get_timer(0);
	while (!(readl((void *)(Z96A_SARADC_BASE + Z96A_SARADC_CTRL)) &
		 Z96A_SARADC_CTRL_IRQ_STATUS)) {
		if (get_timer(start) > 20) {
			writel(0, (void *)(Z96A_SARADC_BASE + Z96A_SARADC_CTRL));
			return -ETIMEDOUT;
		}
		udelay(10);
	}

	*raw = readl((void *)(Z96A_SARADC_BASE + Z96A_SARADC_DATA)) & 0x3ff;

	/* power the block back down */
	writel(0, (void *)(Z96A_SARADC_BASE + Z96A_SARADC_CTRL));
	return 0;
}

int rockchip_dnl_key_pressed(void)
{
	unsigned int raw = ~0U;
	int ret;

	/* Instrumented: every step prints, so one boot log localizes a
	 * silent failure. */
	ret = z96a_saradc_raw(0, &raw);
	printf("dnl-key: raw_adc=%d raw=%u\n", ret, raw);
	if (ret)
		return false;

	if (raw >= 90 && raw <= 200) {
		printf("dnl-key: volume-up/Recovery pressed (raw=%u)\n", raw);
		/* BootROM download via PMU_GRF OS_REG2 + WARM reset (see the
		 * LB2004 note in evb_rk3568.c: a PSCI cold reset loses the
		 * flag, so the board just rebooted normally). */
		writel(0xEF08A53C, 0xFDC20200);
		writel(0xeca8, 0xFDD200D8);
		return true;	/* not reached */
	}

	return false;
}

int rk_board_late_init(void)
{
	struct udevice *bus, *chip;
	u16 status = 0, prochot = 0, adcibat = 0;
	int ac_present, bat_removed, bat_mv;
	int ret;

	if (!of_machine_is_compatible("sunniwell,z96a-rk3568-laptop-v2"))
		return 0;

	ret = uclass_get_device_by_name(UCLASS_I2C, SC8886_I2C_BUS, &bus);
	if (ret) {
		printf("z96a-pwr: i2c bus '%s' unavailable (%d)\n",
		       SC8886_I2C_BUS, ret);
		return 0;
	}

	ret = i2c_get_chip(bus, SC8886_I2C_ADDR, 1, &chip);
	if (ret) {
		printf("z96a-pwr: SC8886@0x6b not found (%d)\n", ret);
		return 0;
	}

	/*
	 * Each read failure is non-fatal: if the chip is missing or
	 * the I2C bus is in a bad state, fall through to the "no
	 * usable power" path. This guarantees rk_board_late_init()
	 * always returns and never blocks the rest of U-Boot.
	 */
	if (sc8886_read16(chip, SC8886_CHARGER_STATUS, &status) ||
	    sc8886_read16(chip, SC8886_PROCHOT_STATUS, &prochot) ||
	    sc8886_read16(chip, SC8886_ADCIBAT, &adcibat)) {
		printf("z96a-pwr: SC8886 read failed; "
		       "skipping power-source selection\n");
		return 0;
	}

	/*
	 * Restore PROCHOT thresholds to factory values.  Without this the
	 * VSYS_VTH field (0x36 bits[7:5]) may power up at its lowest setting,
	 * making PROCHOT fire on every I2C bus transient and blocking charge.
	 * Factory values measured on a healthy Z96A v2: 0x36=0x65, 0x37=0x4A.
	 */
	sc8886_write8(chip, SC8886_PROCHOT_OPT0, SC8886_PROCHOT_OPT0_VAL);
	sc8886_write8(chip, SC8886_PROCHOT_OPT1, SC8886_PROCHOT_OPT1_VAL);

	ac_present = !!(status & SC8886_AC_STAT);
	bat_removed = !!(prochot & SC8886_BAT_REMOV);
	bat_mv = SC8886_BATVOL_BASE_MV +
		 (adcibat & SC8886_BATVOL_MASK) * SC8886_BATVOL_STEP_MV;

	printf("z96a-pwr: SC8886 AC=%d BAT_REMOV=%d BAT=%dmV\n",
	       ac_present, bat_removed, bat_mv);

	if (ac_present) {
		/* C port has power: use it, never reverse-boost to VBUS */
		sc8886_update16(chip, SC8886_CHARGE_OPTION2,
				SC8886_EN_OTG, 0);
		sc8886_update16(chip, SC8886_CHARGE_OPTION0,
				SC8886_EN_LWPWR, 0);
		/*
		 * Factory DT charges at 1 A (bq25700/SC8886 reg 0x14,
		 * bits[12:6], 64 mA LSB). The kernel charger driver is
		 * disabled in the z96a DTB, so the chip would charge at
		 * its power-on default and sag VSYS with the adapter
		 * plugged, which killed the NIC behind the OTG controller.
		 */
		sc8886_update16(chip, SC8886_CHARGE_CURRENT, 0x7f00, 0x0400);
		env_set("z96a_power_src", "usb-c");
		printf("z96a-pwr: source=USB-C, charge current 1024 mA\n");
	} else if (!bat_removed && bat_mv >= SC8886_BAT_OK_MV) {
		/* No C port but battery usable: full power on battery */
		sc8886_update16(chip, SC8886_CHARGE_OPTION2,
				SC8886_EN_OTG, 0);
		sc8886_update16(chip, SC8886_CHARGE_OPTION0,
				SC8886_EN_LWPWR, 0);
		env_set("z96a_power_src", "battery");
		printf("z96a-pwr: source=battery (%dmV)\n", bat_mv);
	} else {
		/* No C port and battery missing/dead: low power */
		sc8886_update16(chip, SC8886_CHARGE_OPTION0,
				SC8886_EN_LWPWR, SC8886_EN_LWPWR);
		env_set("z96a_power_src", "none");
		printf("z96a-pwr: NO usable power (AC=0 BAT_REMOV=%d BAT=%dmV)\n",
		       bat_removed, bat_mv);
	}

	return 0;
}
