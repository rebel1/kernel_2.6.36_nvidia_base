/*
 * Copyright (C) 2011 Eduardo José Tagle <ejtagle@tutopia.com> 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include "board-shuttle.h"
#include "gpio-names.h"

static struct i2c_board_info __initdata shuttle_i2c_bma150_sensor_info[] = {
	 {
		I2C_BOARD_INFO("bma150", 0x38),
	 },
};

int __init shuttle_sensors_register_devices(void)
{
	return i2c_register_board_info(0, shuttle_i2c_bma150_sensor_info,
		ARRAY_SIZE(shuttle_i2c_bma150_sensor_info));
}
