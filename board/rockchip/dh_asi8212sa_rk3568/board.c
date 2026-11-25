// SPDX-License-Identifier: GPL-2.0+
/*
 * DH-ASI8212SA (Dahua RK3568 door-station board) - U-Boot board code.
 *
 * Independent of the Z96A RK3568 and LB2004 RK3566 board code.
 *
 * Recovery/MASKROM key: the factory U-Boot DTB (recovered from the uboot
 * partition, see dh/flash_kit/uboot_config.dts) carries an adc-keys node
 * with a single volume-up key pressing SARADC ch0 down to < 9 uV - i.e.
 * pressed reads raw ~0..5, idle reads 1023 (1.8 V full scale).  Any raw
 * < 100 is an unambiguous press (same wide band as LB2004), which triggers
 * rockchip_dnl_mode_check() -> BOOT_BROM_DOWNLOAD (maskrom/rockusb).
 */

#include <adc.h>
#include <dm.h>
#include <env.h>
#include <linux/delay.h>


/* GPIO0_A0 / GPIO0_A8 输出低 — loader 阶段立即保持 (外部看门狗/使能脚) */
#define DH_GPIO0_BASE 0xfdd60000

static void dh_gpio_hold_early(void)
{
	*(volatile unsigned int *)(DH_GPIO0_BASE + 0x00) = 0x00020002; /* SWPORT_DR_L  : A0/A8 = 0 */
	*(volatile unsigned int *)(DH_GPIO0_BASE + 0x08) = 0x00020002; /* SWPORT_DDR_L : A0/A8 output */
}

void spl_board_init(void)
{
	dh_gpio_hold_early();
}


/* GPIO0_A0/A8 输出低 — U-Boot 启动即闭狗 (此阶段 gpio 设备已 probe, 时钟已开) */
static void dh_gpio_hold_uboot(void)
{
	*(volatile unsigned int *)(DH_GPIO0_BASE + 0x00) = 0x00020002; /* SWPORT_DR_L  */
	*(volatile unsigned int *)(DH_GPIO0_BASE + 0x08) = 0x00020002; /* SWPORT_DDR_L */
}

int rockchip_dnl_key_pressed(void)
{
	unsigned int raw = ~0U;
	int ret;

	dh_gpio_hold_uboot();

	ret = adc_channel_single_shot("saradc", 0, &raw);
	if (ret)
		ret = adc_channel_single_shot("saradc@fe720000", 0, &raw);

	printf("dh-dnl-key: adc_ret=%d raw=%u\n", ret, raw);

	if (ret)
		return false;

	/* factory press band is < 9uV (raw ~0..5); < 100 leaves wide
	 * margins on both sides (idle = 1023) */
	if (raw < 100) {
		printf("dh-dnl-key: Recovery pressed (raw=%u)\n", raw);
		return true;
	}

	return false;
}

int rk_board_late_init(void)
{
	return 0;
}
