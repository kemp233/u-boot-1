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
	 * Factory U-Boot DTB (z96a-extract/factory.dts):
	 *   adc-keys on SARADC ch0, label "volume up", press thr = 9 uV
	 * Case "Recovery" == this key -> BOOT_BROM_DOWNLOAD (Maskrom/rockusb),
	 * not Android recovery partition (that is reboot-mode 0x5242c303).
	 * Never use ch1 raw 0..30 (idle false reboot after vref).
	 */
	unsigned int raw = ~0U;
	int ret, pressed = 0;

#if CONFIG_IS_ENABLED(BUTTON)
	{
		struct udevice *btn;

		if (!button_get_by_label("volume up", &btn)) {
			if (button_get_state(btn) == BUTTON_ON) {
				printf("dnl-key: button 'volume up' ON\n");
				pressed = 1;
			}
		}
	}
#endif
#if CONFIG_IS_ENABLED(ADC)
	ret = adc_channel_single_shot("saradc", 0, &raw);
	if (ret)
		ret = adc_channel_single_shot("saradc@fe720000", 0, &raw);
	if (ret) {
		printf("dnl-key: saradc ch0 read fail %d\n", ret);
	} else {
		printf("dnl-key: saradc ch0 raw=%u (press if raw<=40, factory thr~0)\n",
		       raw);
		if (raw <= 40)
			pressed = 1;
	}
#endif
	return pressed;
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
