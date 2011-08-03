/*
 * alc5624.h  --  ALC5624 ALSA Soc Audio driver
 *
 * Copyright 2011 Eduardo José Tagle <ejtagle@tutopia.com>
 * Based on rt5624.c , Copyright 2008 Realtek Microelectronics flove <flove@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/* Platform data required for the codec */
struct alc5624_platform_data {
	char* 			mclk;			/* The MCLK, that is required to make the codec work */
	unsigned int	spkvdd_mv;		/* Speaker Vdd in millivolts */
	unsigned int	hpvdd_mv;		/* Headphone Vdd in millivolts */
};