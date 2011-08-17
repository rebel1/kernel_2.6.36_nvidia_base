/* OK */
/*
 * arch/arm/mach-tegra/board-shuttle-keyboard.c
 *
 * Copyright (C) 2011 Eduardo José Tagle <ejtagle@tutopia.com>
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

#include <linux/platform_device.h>
#include <linux/input.h>

#include <linux/gpio_keys.h>
#include <linux/gpio_shortlong_key.h>

#include <linux/gpio.h>
#include <asm/mach-types.h>

#include "board-shuttle.h"
#include "gpio-names.h"

static struct gpio_keys_button shuttle_keys[] = {
	[0] = {
		.gpio = SHUTTLE_KEY_VOLUMEUP,
		.active_low = true,
		.debounce_interval = 10,
		.wakeup = false,		
		.code = KEY_VOLUMEUP,
		.type = EV_KEY,		
		.desc = "volume up",
	},
	[1] = {
		.gpio = SHUTTLE_KEY_VOLUMEDOWN,
		.active_low = true,
		.debounce_interval = 10,
		.wakeup = false,		
		.code = KEY_VOLUMEDOWN,
		.type = EV_KEY,		
		.desc = "volume down",
	},
	[2] = {
		.gpio = SHUTTLE_KEY_POWER,
		.active_low = true,
		.debounce_interval = 100,
		.wakeup = true,		
		.code = KEY_POWER,
		.type = EV_KEY,		
		.desc = "power",
	},
	[3] = {
		.gpio = SHUTTLE_FB_NONROTATE,
		.active_low = false,
		.debounce_interval = 10,
		.wakeup = false,		
		.code = SW_ROTATION_LOCK,
		.type = EV_SW,		
		.desc = "rotation lock",
	},
	
#if 0 /* Not populated on Shuttle board */
	[4] = {
		.gpio = SHUTTLE_KEY_RESUME,
		.active_low = true,
		.debounce_interval = 10,
		.wakeup = true,		
		.code = KEY_F3,
		.type = EV_KEY,		
		.desc = "resume",
	},
	[5] = {
		.gpio = SHUTTLE_KEY_SUSPEND,
		.active_low = true,
		.debounce_interval = 10,
		.wakeup = false,		
		.code = KEY_F4,
		.type = EV_KEY,		
		.desc = "suspend",
	},
#endif
};


static struct gpio_keys_platform_data shuttle_keys_platform_data = {
	.buttons 	= shuttle_keys,
	.nbuttons 	= ARRAY_SIZE(shuttle_keys),
	.rep		= false, /* auto repeat enabled */
};

static struct platform_device shuttle_keys_device = {
	.name 		= "gpio-keys",
	.id 		= 0,
	.dev		= {
		.platform_data = &shuttle_keys_platform_data,
	},
};

struct gpio_shortlong_key_platform_data shuttle_longshortkey_pdata = {
	.gpio = SHUTTLE_KEY_BACK,			/* gpio to use to detect long presses */
	.active_low = 1,					/* active low */
	.debounce_time = 50,				/* time to recognize at least a short press in ms */
	.long_press_time = 1000,			/* time to recognize a long press in ms */
	.short_press_keycode = KEY_BACK,	/* short press key code */
	.long_press_keycode = KEY_HOME,		/* long press key code */
};

struct platform_device shuttle_longshortkey_device = {
	.name	= "gpio-shortlong-kbd",
	.dev	= {
		.platform_data = &shuttle_longshortkey_pdata,
	},
};

static struct platform_device *shuttle_pmu_devices[] __initdata = {
	&shuttle_longshortkey_device,
	&shuttle_keys_device,
};

/* Register all keyboard devices */
int __init shuttle_keyboard_register_devices(void)
{
	return platform_add_devices(shuttle_pmu_devices, ARRAY_SIZE(shuttle_pmu_devices));
}

