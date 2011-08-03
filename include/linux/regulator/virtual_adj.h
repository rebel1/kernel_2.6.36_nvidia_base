/*
 * virtual_adj.h
 *
 * Copyright 2011 Eduardo José Tagle <ejtagle@tutopia.com>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef __REGULATOR_VIRTUAL_ADJ_H
#define __REGULATOR_VIRTUAL_ADJ_H

struct regulator_init_data;

/**
 * struct virtual_adj_voltage_config - virtual_adj_voltage_config structure
 * @supply_name:		Name of the regulator supply
 * @min_microvolts:		Minimum Output voltage of regulator
 * @max_microvolts:		Maximum Output voltage of regulator (inclusive)
 * @step_microvolts:	Output voltage granularity of regulator
 * @microvolts:			Output voltage of regulator
 * @init_data:			regulator_init_data
 *
 *   This structure contains virtual adjustable (faked) voltage regulator configuration
 * information that must be passed by platform code to the virtual adjustable voltage 
 * regulator driver.
 */
struct virtual_adj_voltage_config {
	const char *supply_name;
	int id;
	int min_mV;
	int max_mV;
	int step_mV;
	int mV;
	
	struct regulator_init_data *init_data;
};

#endif
