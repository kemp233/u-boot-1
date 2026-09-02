// SPDX-License-Identifier: GPL-2.0+
/*
 * Z96A RK3568 Laptop V2 — power-source selection at U-Boot boot.
 *
 * SC8886 (I2C 0x6b, bq25700-compatible) is an NVDC buck-boost charger:
 *   - VBUS (Type-C, via HUSB311 PD) present -> VBUS powers VSYS + charges BAT
 *   - VBUS absent                          -> BAT feeds VSYS via BATFET (auto)
 * So "C口 vs 电池" system-power switching is done by hardware. What U-Boot
 * must do is:
 *   1. detect the real state (AC_STAT, battery-removed, battery voltage)
 *   2. keep the charger in a sane mode:
 *        - C口 present: clear EN_OTG (never reverse-boost BAT->VBUS here)
 *        - C口 absent + battery ok: clear EN_LWPWR (full-power on battery)
 *        - neither: warn, enter low-power (nothing else we can do)
 *
 * TCS4525 (I2C 0x1c, vdd_cpu) is always-on CPU VRM and is NOT part of the
 * source choice; its input is VSYS and it stays enabled regardless.
 */

#include <dm.h>
#include <dm/device.h>
#include <dm/uclass.h>
#include <i2c.h>
#include <env.h>
#include <linux/bitops.h>
#include <log.h>

#define SC8886_I2C_BUS		"i2c@fdd40000"
#define SC8886_I2C_ADDR		0x6b

/*
 * bq25700/sc8886 register map (8-bit address, 16-bit little-endian value),
 * cross-checked with drivers/power/supply/bq25700_charger.c reg_field defs.
 */
#define SC8886_CHARGER_STATUS	0x20	/* bit15 AC_STAT, bit8 IN_OTG */
#define SC8886_PROCHOT_STATUS	0x21	/* bit1 STAT_BAT_REMOV */
#define SC8886_ADCIBAT		0x26	/* bit[6:0] OUTPUT_BAT_VOL */
#define SC8886_CHARGE_OPTION0	0x12	/* bit15 EN_LWPWR */
#define SC8886_CHARGE_OPTION2	0x32	/* bit12 EN_OTG */

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

	sc8886_read16(chip, SC8886_CHARGER_STATUS, &status);
	sc8886_read16(chip, SC8886_PROCHOT_STATUS, &prochot);
	sc8886_read16(chip, SC8886_ADCIBAT, &adcibat);

	ac_present = !!(status & SC8886_AC_STAT);
	bat_removed = !!(prochot & SC8886_BAT_REMOV);
	bat_mv = SC8886_BATVOL_BASE_MV +
		 (adcibat & SC8886_BATVOL_MASK) * SC8886_BATVOL_STEP_MV;

	printf("z96a-pwr: SC8886 AC=%d BAT_REMOV=%d BAT=%dmV\n",
	       ac_present, bat_removed, bat_mv);

	if (ac_present) {
		/* C口有电 -> C口优先，绝不反向升压到 VBUS */
		sc8886_update16(chip, SC8886_CHARGE_OPTION2,
				SC8886_EN_OTG, 0);
		sc8886_update16(chip, SC8886_CHARGE_OPTION0,
				SC8886_EN_LWPWR, 0);
		env_set("z96a_power_src", "usb-c");
		printf("z96a-pwr: source=USB-C (C口优先)\n");
	} else if (!bat_removed && bat_mv >= SC8886_BAT_OK_MV) {
		/* C口没电 + 电池可用 -> 电池供电，退出低功耗，不开 OTG */
		sc8886_update16(chip, SC8886_CHARGE_OPTION2,
				SC8886_EN_OTG, 0);
		sc8886_update16(chip, SC8886_CHARGE_OPTION0,
				SC8886_EN_LWPWR, 0);
		env_set("z96a_power_src", "battery");
		printf("z96a-pwr: source=battery (%dmV)\n", bat_mv);
	} else {
		/* 无 C口且电池缺失/低电 -> 低功耗，无法可靠运行 */
		sc8886_update16(chip, SC8886_CHARGE_OPTION0,
				SC8886_EN_LWPWR, SC8886_EN_LWPWR);
		env_set("z96a_power_src", "none");
		printf("z96a-pwr: NO usable power (AC=0 BAT_REMOV=%d BAT=%dmV)\n",
		       bat_removed, bat_mv);
	}

	return 0;
}
