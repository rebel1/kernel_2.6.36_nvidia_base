/* drivers/input/touchscreen/it7260.c
 *
 * Copyright (C) 2011 Eduardo José Tagle
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

struct it7260_platform_data {
	void (*disable_tp)(void);	/* function to disable the touchpad */
	void (*enable_tp)(void);	/* function to enable the touchpad */
};
