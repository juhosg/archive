/*
 *  MikroTik RouterBOARD 911 Lite2/Lite5 support
 *
 *  Copyright (C) 2017 Gabor Juhos <juhosg@freemail.hu>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/phy.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/ath9k_platform.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/routerboot.h>
#include <linux/gpio.h>

#include <asm/prom.h>
#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>

#include "common.h"
#include "dev-gpio-buttons.h"
#include "dev-eth.h"
#include "dev-leds-gpio.h"
#include "dev-m25p80.h"
#include "dev-wmac.h"
#include "machtypes.h"
#include "routerboot.h"

#define RB911L_KEYS_POLL_INTERVAL	20	/* msecs */
#define RB911L_KEYS_DEBOUNCE_INTERVAL	(3 * RB911L_KEYS_POLL_INTERVAL)

#define RB_ROUTERBOOT_OFFSET	0x0000
#define RB_ROUTERBOOT_MIN_SIZE	0xb000
#define RB_HARD_CFG_SIZE	0x1000
#define RB_BIOS_OFFSET		0xd000
#define RB_BIOS_SIZE		0x1000
#define RB_SOFT_CFG_OFFSET	0xf000
#define RB_SOFT_CFG_SIZE	0x1000

static struct mtd_partition rb911l_spi_partitions[] = {
	{
		.name		= "routerboot",
		.offset		= RB_ROUTERBOOT_OFFSET,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "hard_config",
		.size		= RB_HARD_CFG_SIZE,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "bios",
		.size		= RB_BIOS_SIZE,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "booter",
		.size		= RB_SOFT_CFG_SIZE,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "soft_config",
		.size		= RB_SOFT_CFG_SIZE,
	},
	{
		.name		= "firmware",
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct flash_platform_data rb911l_spi_flash_data = {
	.parts		= rb911l_spi_partitions,
	.nr_parts	= ARRAY_SIZE(rb911l_spi_partitions),
};

/* RB911L GPIOs */
#define RB911L_GPIO_LED_1	13
#define RB911L_GPIO_LED_2	12
#define RB911L_GPIO_LED_3	4
#define RB911L_GPIO_LED_4	21
#define RB911L_GPIO_LED_5	18
#define RB911L_GPIO_LED_POWER	11
#define RB911L_GPIO_LED_USER	3
#define RB911L_GPIO_LED_ETH	20
#define RB911L_GPIO_BTN_RESET	15
#define RB911L_GPIO_PIN_HOLE	14

static struct gpio_led rb911l_leds[] __initdata = {
	{
		.name = "rb:green:led1",
		.gpio = RB911L_GPIO_LED_1,
		.active_low = 1,
	}, {
		.name = "rb:green:led2",
		.gpio = RB911L_GPIO_LED_2,
		.active_low = 1,
	}, {
		.name = "rb:green:led3",
		.gpio = RB911L_GPIO_LED_3,
		.active_low = 1,
	}, {
		.name = "rb:green:led4",
		.gpio = RB911L_GPIO_LED_4,
		.active_low = 1,
	}, {
		.name = "rb:green:led5",
		.gpio = RB911L_GPIO_LED_5,
		.active_low = 1,
	}, {
		.name = "rb:green:user",
		.gpio = RB911L_GPIO_LED_USER,
		.active_low = 1,
		.direction = LEDS_GPIO_DIR_INPUT_OFF,
	}, {
		.name = "rb:green:power",
		.gpio = RB911L_GPIO_LED_POWER,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
};

static struct gpio_keys_button rb911l_gpio_keys[] __initdata = {
	{
		.desc = "Reset button",
		.type = EV_KEY,
		.code = KEY_RESTART,
		.debounce_interval = RB911L_KEYS_DEBOUNCE_INTERVAL,
		.gpio = RB911L_GPIO_BTN_RESET,
		.active_low = 1,
	},
};

static void __init rb911l_init_partitions(const struct rb_info *info)
{
	rb911l_spi_partitions[0].size = info->hard_cfg_offs;

	rb911l_spi_partitions[1].offset = info->hard_cfg_offs;
	rb911l_spi_partitions[1].size = info->hard_cfg_size;

	rb911l_spi_partitions[2].offset = rb911l_spi_partitions[1].offset +
					  rb911l_spi_partitions[1].size;

	rb911l_spi_partitions[3].offset = rb911l_spi_partitions[2].offset +
					  rb911l_spi_partitions[2].size;
	rb911l_spi_partitions[3].size = info->soft_cfg_offs -
					rb911l_spi_partitions[3].offset;

	rb911l_spi_partitions[4].offset = info->soft_cfg_offs;

	rb911l_spi_partitions[5].offset = rb911l_spi_partitions[4].offset +
				          rb911l_spi_partitions[4].size;
}

static void __init rb911l_wlan_init(void)
{
	char *art_buf;
	u8 wlan_mac[ETH_ALEN];

	art_buf = rb_get_wlan_data();
	if (art_buf == NULL)
		return;

	ath79_init_mac(wlan_mac, ath79_mac_base, 1);
	ath79_register_wmac(art_buf + 0x1000, wlan_mac);

	kfree(art_buf);
}

static void __init rb911l_setup(void)
{
	const struct rb_info *info;
	char buf[64];

	info = rb_init_info((void *) KSEG1ADDR(AR71XX_SPI_BASE), 0x20000);
	if (!info)
		return;

	scnprintf(buf, sizeof(buf), "MikroTik RouterBOARD %s",
		  (info->board_name) ? info->board_name : "");
	mips_set_machine_name(buf);

	if ((info->hw_options & RB_HW_OPT_NO_NAND)) {
		rb911l_init_partitions(info);
		ath79_register_m25p80(&rb911l_spi_flash_data);
	} else {
		WARN(1, "The %s with NAND flash is not supported yet\n",
			(info->board_name) ? info->board_name : "board");
	}

	ath79_register_mdio(1, 0x0);

	ath79_init_mac(ath79_eth1_data.mac_addr, ath79_mac_base, 0);

	ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	ath79_eth1_data.speed = SPEED_1000;
	ath79_eth1_data.duplex = DUPLEX_FULL;

	ath79_register_eth(1);

	rb911l_wlan_init();

	ath79_register_leds_gpio(-1, ARRAY_SIZE(rb911l_leds), rb911l_leds);
	ath79_register_gpio_keys_polled(-1, RB911L_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(rb911l_gpio_keys),
					rb911l_gpio_keys);
}

MIPS_MACHINE_NONAME(ATH79_MACH_RB_911L, "911L", rb911l_setup);
