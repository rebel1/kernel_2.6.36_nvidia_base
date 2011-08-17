/*
 * arch/arm/mach-tegra/board-shuttle-audio.c
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

/* All configurations related to audio */
 
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/i2c-tegra.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <sound/alc5624.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>
#include <asm/io.h>

#include <mach/io.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/gpio.h>
#include <mach/i2s.h>
#include <mach/spdif.h>
#include <mach/audio.h>  
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,36)
#include <mach/tegra_das.h>
#endif

#include <mach/system.h>
#include <mach/shuttle_audio.h>

#include "board.h"
#include "board-shuttle.h"
#include "gpio-names.h"
#include "devices.h"

/* Default music path: I2S1(DAC1)<->Dap1<->HifiCodec
   Bluetooth to codec: I2S2(DAC2)<->Dap4<->Bluetooth
*/
/* For Shuttle, 
	Codec is ALC5624
	Codec I2C Address = 0x30(includes R/W bit), i2c #0
	Codec MCLK = APxx DAP_MCLK1
*/

#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,36)
static struct tegra_das_platform_data tegra_das_pdata = {
	.dap_clk = "clk_dev1",
	.tegra_dap_port_info_table = {
		/* I2S1 <--> DAC1 <--> DAP1 <--> Hifi Codec */
		[0] = {
			.dac_port = tegra_das_port_i2s1,
			.dap_port = tegra_das_port_dap1,
			.codec_type = tegra_audio_codec_type_hifi,
			.device_property = {
				.num_channels = 2,
				.bits_per_sample = 16,
#ifdef SHUTTLE_48KHZ_AUDIO				
				.rate = 48000,
#else
				.rate = 44100,
#endif
				.dac_dap_data_comm_format =
						dac_dap_data_format_all,
			},
		},
		/* I2S2 <--> DAC2 <--> DAP2 <--> Voice Codec */
		[1] = {
			.dac_port = tegra_das_port_i2s2,
			.dap_port = tegra_das_port_dap2,
			.codec_type = tegra_audio_codec_type_voice,
			.device_property = {
				.num_channels = 1,
				.bits_per_sample = 16,
				.rate = 8000,
				.dac_dap_data_comm_format =
						dac_dap_data_format_all,
			},
		},
		/* I2S2 <--> DAC2 <--> DAP3 <--> Baseband Codec */
		[2] = {
			.dac_port = tegra_das_port_i2s2,
			.dap_port = tegra_das_port_dap3,
			.codec_type = tegra_audio_codec_type_baseband,
			.device_property = {
				.num_channels = 1,
				.bits_per_sample = 16,
				.rate = 8000,
				.dac_dap_data_comm_format =
					dac_dap_data_format_dsp,
			},
		},
		/* I2S2 <--> DAC2 <--> DAP4 <--> BT SCO Codec */
		[3] = {
			.dac_port = tegra_das_port_i2s2,
			.dap_port = tegra_das_port_dap4,
			.codec_type = tegra_audio_codec_type_bluetooth,
			.device_property = {
				.num_channels = 1,
				.bits_per_sample = 16,
				.rate = 8000,
				.dac_dap_data_comm_format =
					dac_dap_data_format_dsp,
			},
		},
		[4] = {
			.dac_port = tegra_das_port_none,
			.dap_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
	},

	.tegra_das_con_table = {
		[0] = {
			.con_id = tegra_das_port_con_id_hifi,
			.num_entries = 2,
			.con_line = { /*src*/            /*dst*/             /* src master */
#ifdef ALC5624_IS_MASTER			
				[0] = {tegra_das_port_i2s1, tegra_das_port_dap1, true}, 
				[1] = {tegra_das_port_dap1, tegra_das_port_i2s1, false},
#else
				[0] = {tegra_das_port_i2s1, tegra_das_port_dap1, false}, 
				[1] = {tegra_das_port_dap1, tegra_das_port_i2s1, true},
#endif
			},
		},
		[1] = {
			.con_id = tegra_das_port_con_id_bt_codec,
			.num_entries = 4,
			.con_line = {
				[0] = {tegra_das_port_i2s2, tegra_das_port_dap4, true}, /* src is master */
				[1] = {tegra_das_port_dap4, tegra_das_port_i2s2, false},
#ifdef ALC5624_IS_MASTER
				[2] = {tegra_das_port_i2s1, tegra_das_port_dap1, true},
				[3] = {tegra_das_port_dap1, tegra_das_port_i2s1, false},
#else				
				[2] = {tegra_das_port_i2s1, tegra_das_port_dap1, false},
				[3] = {tegra_das_port_dap1, tegra_das_port_i2s1, true},
#endif				
			},
		},
		[2] = {
			.con_id = tegra_das_port_con_id_voicecall_no_bt,
			.num_entries = 4,
			.con_line = {
				[0] = {tegra_das_port_dap2, tegra_das_port_dap3, true},
				[1] = {tegra_das_port_dap3, tegra_das_port_dap2, false},
#ifdef ALC5624_IS_MASTER
				[2] = {tegra_das_port_i2s1, tegra_das_port_dap1, true},
				[3] = {tegra_das_port_dap1, tegra_das_port_i2s1, false},
#else
				[2] = {tegra_das_port_i2s1, tegra_das_port_dap1, false},
				[3] = {tegra_das_port_dap1, tegra_das_port_i2s1, true},
#endif
			},
		},
	}
}; 
#endif

static struct tegra_audio_platform_data tegra_spdif_pdata = {
	.dma_on = true,  /* use dma by default */
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,36)
#	ifdef SHUTTLE_48KHZ_AUDIO
	.spdif_clk_rate = 6144000,
#	else
	.spdif_clk_rate = 5644800,
#	endif
#endif
};

static struct tegra_audio_platform_data tegra_audio_pdata[] = {
	/* For I2S1 - Hifi */
	[0] = {
#ifdef ALC5624_IS_MASTER
		.i2s_master		= false,	/* CODEC is master for audio */
		.dma_on			= true,  	/* use dma by default */
		.i2s_clk_rate 	= 2822400,
		.dap_clk	  	= "clk_dev1",
		.audio_sync_clk = "audio_2x",
		.mode			= I2S_BIT_FORMAT_I2S,
		.fifo_fmt		= I2S_FIFO_16_LSB,
		.bit_size		= I2S_BIT_SIZE_16,
#else
		.i2s_master		= true,		/* CODEC is slave for audio */
		.dma_on			= true,  	/* use dma by default */
#ifdef SHUTTLE_48KHZ_AUDIO						
		.i2s_master_clk = 48000,
		.i2s_clk_rate 	= 12288000,
#else
		.i2s_master_clk = 44100,
		.i2s_clk_rate 	= 11289600,
#endif
		.dap_clk	  	= "clk_dev1",
		.audio_sync_clk = "audio_2x",
		.mode			= I2S_BIT_FORMAT_I2S,
		.fifo_fmt		= I2S_FIFO_PACKED,
		.bit_size		= I2S_BIT_SIZE_16,
		.i2s_bus_width	= 32,
#endif
	},
	/* For I2S2 - Bluetooth */
	[1] = {
		.i2s_master		= true,
		.dma_on			= true,  /* use dma by default */
		.i2s_master_clk = 8000,
		.dsp_master_clk = 8000,
		.i2s_clk_rate	= 2000000,
		.dap_clk		= "clk_dev1",
		.audio_sync_clk = "audio_2x",
		.mode			= I2S_BIT_FORMAT_DSP,
		.fifo_fmt		= I2S_FIFO_16_LSB,
		.bit_size		= I2S_BIT_SIZE_16,
		.i2s_bus_width 	= 32,
		.dsp_bus_width 	= 16,
	}
}; 

static struct alc5624_platform_data alc5624_pdata = {
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,36)
	.mclk 			= "clk_dev1",
#else	
	.mclk 			= "cdev1",
#endif
	.spkvdd_mv 		= 5000,	/* Speaker Vdd in millivolts */
	.hpvdd_mv 		= 3300,	/* Headphone Vdd in millivolts */
	.spkvol_scale 	= 75,	/* Scale speaker volume to the percent of maximum range -Be careful: range is logarithmic! */
};

static struct i2c_board_info __initdata shuttle_i2c_bus0_board_info[] = {
	{
		I2C_BOARD_INFO("alc5624", 0x18),
		.platform_data = &alc5624_pdata,
	},
};

static struct shuttle_audio_platform_data shuttle_audio_pdata = {
	.gpio_hp_det = SHUTTLE_HP_DETECT,
}; 

static struct platform_device shuttle_audio_device = {
	.name = "tegra-snd-shuttle",
	.id   = 0,
	.dev = {
		.platform_data = &shuttle_audio_pdata,
	}, 
};

static struct platform_device spdif_dit_device = {
	.name   = "spdif-dit",
	.id     = -1,
}; 

static struct platform_device *shuttle_i2s_devices[] __initdata = {
	&tegra_i2s_device1,
	&tegra_i2s_device2,
	&spdif_dit_device,
	&tegra_spdif_device,
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,36)
	&tegra_das_device,
#else
	&tegra_pcm_device,
#endif
	&shuttle_audio_device, /* this must come last, as we need the DAS to be initialized to access the codec registers ! */
};

int __init shuttle_audio_register_devices(void)
{
	int ret;
	
	/* Patch in the platform data */
	tegra_i2s_device1.dev.platform_data = &tegra_audio_pdata[0];
	tegra_i2s_device2.dev.platform_data = &tegra_audio_pdata[1];
	tegra_spdif_device.dev.platform_data = &tegra_spdif_pdata;
	
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,36)
	tegra_das_device.dev.platform_data = &tegra_das_pdata;
#endif
 
	ret = i2c_register_board_info(0, shuttle_i2c_bus0_board_info, 
		ARRAY_SIZE(shuttle_i2c_bus0_board_info)); 
	if (ret)
		return ret;
	return platform_add_devices(shuttle_i2s_devices, ARRAY_SIZE(shuttle_i2s_devices));
}
	
#if 0
static inline void das_writel(unsigned long value, unsigned long offset)
{
	writel(value, IO_ADDRESS(TEGRA_APB_MISC_BASE) + offset);
}

#define APB_MISC_DAS_DAP_CTRL_SEL_0             0xc00
#define APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0   0xc40

static void init_dac1(void)
{
	bool master = tegra_audio_pdata.i2s_master;
	/* DAC1 -> DAP1 */
	das_writel((!master)<<31, APB_MISC_DAS_DAP_CTRL_SEL_0);
	das_writel(0, APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0);
}

static void init_dac2(void)
{
	/* DAC2 -> DAP4 for Bluetooth Voice */
	bool master = tegra_audio2_pdata.dsp_master;
	das_writel((!master)<<31 | 1, APB_MISC_DAS_DAP_CTRL_SEL_0 + 12);
	das_writel(3<<28 | 3<<24 | 3,
			APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0 + 4);
}
#endif
