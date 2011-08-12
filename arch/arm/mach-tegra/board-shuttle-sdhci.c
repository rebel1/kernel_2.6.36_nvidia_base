/*
 * arch/arm/mach-tegra/board-shuttle-sdhci.c
 *
 * Copyright (C) 2011 Eduardo José Tagle <ejtagle@tutopia.com> 
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/resource.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/version.h>

#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/sdhci.h>
#include <mach/pinmux.h>

#include "gpio-names.h"
#include "devices.h"
#include "board-shuttle.h"

/*
  For Shuttle, 
    SDIO0: WLan
	SDIO1: Missing SD MMC
	sDIO2: Unused
	SDIO3: SD MMC
 */

/* Make sure they are NOT trying to compile with a nonworking config */
#ifdef CONFIG_MMC_EMBEDDED_SDIO
#error  DISABLE MMC EMBEDDED SDIO, or WLAN wont work amd SD Cards could stop responding...
#endif

static void (*wlan_status_cb)(int card_present, void *dev_id) = NULL;
static void *wlan_status_cb_devid = NULL;
static int shuttle_wlan_cd = 0; /* WIFI virtual 'card detect' status */

/* Used to set the virtual CD of wifi adapter */
void shuttle_wifi_set_cd(int val)
{
	/* Only if a change is detected */
	if (shuttle_wlan_cd != val) {
	
		/* Store new card 'detect' */
		shuttle_wlan_cd = val;
		
		/* Let the SDIO infrastructure know about the change */
		if (wlan_status_cb) {
			wlan_status_cb(val, wlan_status_cb_devid);
		} else
			pr_info("%s: Nobody to notify\n", __func__);
	}
}
EXPORT_SYMBOL_GPL(shuttle_wifi_set_cd);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
/* 2.6.36 version has a hook to check card status. Use it */
static int shuttle_wlan_status_register(
		void (*callback)(int card_present, void *dev_id),
		void *dev_id)
{
	if (wlan_status_cb)
		return -EAGAIN;
	wlan_status_cb = callback;
	wlan_status_cb_devid = dev_id;
	return 0;
} 
#endif

struct tegra_sdhci_platform_data shuttle_wlan_data = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
	/* 2.6.36 version has a hook to check card status. Use it */
	.register_status_notify	= shuttle_wlan_status_register, 
#endif
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data2 = {
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data3 = {
	.cd_gpio = SHUTTLE_SDIO2_CD,
	.wp_gpio = -1,
	.power_gpio = SHUTTLE_SDIO2_POWER,
};


static struct tegra_sdhci_platform_data tegra_sdhci_platform_data4 = {
	.cd_gpio = SHUTTLE_SDHC_CD,
	.cd_gpio_polarity = 1,
	.wp_gpio = SHUTTLE_SDHC_WP,
	.power_gpio = SHUTTLE_SDHC_POWER,
};

static struct platform_device *shuttle_sdhci_devices[] __initdata = {
	&tegra_sdhci_device1,
	&tegra_sdhci_device2,
	&tegra_sdhci_device3,
	&tegra_sdhci_device4,
};

/* Register sdhci devices */
int __init shuttle_sdhci_register_devices(void)
{
	/* Plug in platform data */
	tegra_sdhci_device1.dev.platform_data = &shuttle_wlan_data;
	tegra_sdhci_device2.dev.platform_data = &tegra_sdhci_platform_data2;
	tegra_sdhci_device3.dev.platform_data = &tegra_sdhci_platform_data3;
	tegra_sdhci_device4.dev.platform_data = &tegra_sdhci_platform_data4;

	gpio_request(tegra_sdhci_platform_data2.power_gpio, "sdhci2_power");
	gpio_request(tegra_sdhci_platform_data2.cd_gpio, "sdhci2_cd");

	gpio_request(tegra_sdhci_platform_data4.power_gpio, "sdhci4_power");
	gpio_request(tegra_sdhci_platform_data4.cd_gpio, "sdhci4_cd");
	gpio_request(tegra_sdhci_platform_data4.wp_gpio, "sdhci4_wp");

	gpio_direction_output(tegra_sdhci_platform_data2.power_gpio, 1);
	gpio_direction_output(tegra_sdhci_platform_data4.power_gpio, 1);
	gpio_direction_input(tegra_sdhci_platform_data2.cd_gpio);
	gpio_direction_input(tegra_sdhci_platform_data4.cd_gpio);
	gpio_direction_input(tegra_sdhci_platform_data4.wp_gpio);
	
	return platform_add_devices(shuttle_sdhci_devices, ARRAY_SIZE(shuttle_sdhci_devices));

}
