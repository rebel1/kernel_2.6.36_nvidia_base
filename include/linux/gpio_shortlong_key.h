/*
 * gpio_shortlong_key.h
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

struct gpio_shortlong_key_platform_data {
	int 				gpio;					/* gpio to use to detect long presses */
	int					active_low;				/* 1 if key is active low */
	int					debounce_time;			/* time to recognize at least a short press in ms */
	int					long_press_time;		/* time to recognize a long press in ms */
	int					short_press_keycode;	/* short press key code */
	int					long_press_keycode;		/* long press key code */
};

