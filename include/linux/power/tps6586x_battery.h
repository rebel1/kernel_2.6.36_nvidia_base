/*
 * Battery charger driver for tps6586x
 *
 * Copyright (C) 2011 Eduardo José Tagle <ejtagle@tutopia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


struct tps6586x_ec_platform_data {
	struct power_supply_info battery_info;	/* Battery information */
	int low_batt_irq;						/* If there is a low battery IRQ */
	int in_s3_state_gpio;					/* Gpio pin used to flag that system is suspended */
};

