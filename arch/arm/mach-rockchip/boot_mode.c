// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Rockchip Electronics Co., Ltd
 */

#include <adc.h>
#include <button.h>
#include <command.h>
#include <env.h>
#include <log.h>
#include <asm/io.h>
#include <asm/arch-rockchip/boot_mode.h>
#include <dm/device.h>
#include <dm/uclass.h>
#include <linux/printk.h>

#if (CONFIG_ROCKCHIP_BOOT_MODE_REG == 0)

int setup_boot_mode(void)
{
	return 0;
}

#else

void set_back_to_bootrom_dnl_flag(void)
{
	writel(BOOT_BROM_DOWNLOAD, CONFIG_ROCKCHIP_BOOT_MODE_REG);
}

/*
 * detect download key status by adc, most rockchip
 * based boards use adc sample the download key status,
 * but there are also some use gpio. So it's better to
 * make this a weak function that can be override by
 * some special boards.
 */
#define KEY_DOWN_MIN_VAL	0
#define KEY_DOWN_MAX_VAL	30

__weak int rockchip_dnl_key_pressed(void)
{
	/*
	 * Z96A: rely ONLY on the adc-keys BUTTON driver, whose thresholds
	 * come from the DT (press 0.14-0.17V, keyup 1.2-1.4V, SARADC ch2).
	 *
	 * Do NOT use a raw-ADC fallback here. On Z96A the idle ch2 line
	 * reads raw ~13 (~0V, floating), so any "raw <= N means pressed"
	 * heuristic false-triggers on every normal boot and loops in
	 * "download key pressed ... resetting". The press band on the
	 * divider ladder is raw ~80-97, NOT near zero.
	 */
#if CONFIG_IS_ENABLED(BUTTON)
	{
		struct udevice *btn;
		static const char *const labels[] = { "volume up", "Recovery" };
		int i;

		for (i = 0; i < 2; i++) {
			if (button_get_by_label(labels[i], &btn))
				continue;
			if (button_get_state(btn) == BUTTON_ON) {
				printf("dnl-key: '%s' pressed (adc-keys)\n", labels[i]);
				return true;
			}
		}
	}
#endif
	return false;
}



void rockchip_dnl_mode_check(void)
{
	if (rockchip_dnl_key_pressed()) {
		printf("download key pressed, entering download mode...");
		set_back_to_bootrom_dnl_flag();
		do_reset(NULL, 0, 0, NULL);
	}
}

int setup_boot_mode(void)
{
	void *reg = (void *)CONFIG_ROCKCHIP_BOOT_MODE_REG;
	int boot_mode = readl(reg);

	rockchip_dnl_mode_check();

	boot_mode = readl(reg);
	debug("%s: boot mode 0x%08x\n", __func__, boot_mode);

	/* Clear boot mode */
	writel(BOOT_NORMAL, reg);

	switch (boot_mode) {
	case BOOT_FASTBOOT:
		debug("%s: enter fastboot!\n", __func__);
		env_set("preboot", "setenv preboot; fastboot usb 0");
		break;
	case BOOT_UMS:
		debug("%s: enter UMS!\n", __func__);
		env_set("preboot", "setenv preboot; ums mmc 0");
		break;
	}

	return 0;
}

#endif
