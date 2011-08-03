/*
 * arch/arm/mach-tegra/board-shuttle-nand.c
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

#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/memblock.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/io.h>
#include <mach/w1.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/nand.h>
#include <mach/iomap.h>

#include "board.h"
#include "board-shuttle.h"
#include "clock.h"
#include "gpio-names.h"
#include "devices.h"

static struct tegra_nand_chip_parms shuttle_nand_chip_parms[] = {
	/* Samsung K5E2G1GACM */
	[0] = {
		.vendor_id   = 0xEC,
		.device_id   = 0xAA,
		.capacity    = 256,
		.timing      = {
			.trp		= 21,
			.trh		= 15,
			.twp		= 21,
			.twh		= 15,
			.tcs		= 31,
			.twhr		= 60,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 30,
			.tadl		= 100,
		},
	},
	/* Hynix H5PS1GB3EFR */
	[1] = {
		.vendor_id   = 0xAD,
		.device_id   = 0xDC,
		.capacity    = 512,
		.timing      = {
			.trp		= 12,
			.trh		= 10,
			.twp		= 12,
			.twh		= 10,
			.tcs		= 20,
			.twhr		= 80,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 20,
			.tadl		= 70,
		},
	},
	/* Samsung K9K8G08U0M */
    [2] = {
		.vendor_id	= 0xEC, 
		.device_id  = 0xD3,
		.capacity   = 1024,
		.timing		= {
			.trp		= 15,
			.trh		= 10,
			.twp		= 15,
			.twh		= 10, 
			.tcs		= 20, 
			.twhr		= 60, 
			.tcr_tar_trr	= 20,
			.twb		= 100, 
			.trp_resp	= 26,
			.tadl		= 70,
		},
    },
    /* Samsung K9GAG08U0D */
    [3] = {
        .vendor_id	= 0xEC,
		.device_id	= 0xD5,
		.capacity	= 2048,
		.timing		= {
			.trp		= 15,
			.trh		= 10,
			.twp		= 15,
			.twh		= 10,
			.tcs		= 20,
			.twhr		= 60, 
			.tcr_tar_trr	= 20,
			.twb		= 100, 
			.trp_resp	= 20,
			.tadl		= 100,
        },
    },
	/* Kenji+NAND (Samsung ???) */
	[4] = {
		.vendor_id	= 0xEC, 
		.device_id	= 0xDC,
		.capacity	= 512,
		.timing		= {
			.trp		= 15, 
			.trh		= 10, 
			.twp		= 15, 
			.twh		= 10, 
			.tcs		= 20, 
			.twhr		= 60, 
			.tcr_tar_trr	= 20,
			.twb		= 100, 
			.trp_resp	= 18,
			.tadl		= 100,
		},
	},
	/* Samsung K9W8G08U1M */
    [5] = {
		.vendor_id	= 0xEC, 
		.device_id	= 0xDC,
		.capacity	= 1024,
		.timing		= {
			.trp		= 15, 
			.trh		= 10,
			.twp		= 15,
			.twh		= 10,
			.tcs		= 15,
			.twhr		= 60,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 18,
			.tadl		= 100,
        },
    },
    /* Samsung K9F1G08Q0M */
    [6] = {
        .vendor_id	= 0xEC,
		.device_id	= 0xA1, 
		.capacity	= 128,
		.timing		= {
			.trp		= 60,
			.trh		= 20,
			.twp		= 60,
			.twh		= 20,
			.tcs		= 0,
			.twhr		= 60,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 60,
			.tadl		= 70,
        },
    },
    /* Samsung K9F1G08U0M */
    [7] = {
        .vendor_id 	= 0xEC, 
		.device_id 	= 0xF1,
		.capacity 	= 128,
		.timing 	= {
			.trp		= 25,
			.trh		= 15,
			.twp		= 25,
			.twh		= 15,
			.tcs		= 0, 
			.twhr		= 60,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 30,
			.tadl		= 35,
        },
    },
    /* Samsung K9L8G08U0M */
    [8] = {
		.vendor_id 	= 0xEC, 
		.device_id 	= 0xD3,
		.capacity 	= 1024,
		.timing		= {
			.trp		= 15,
			.trh		= 10,
			.twp		= 15,
			.twh		= 10,
			.tcs		= 20, 
			.twhr		= 60,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 20,
			.tadl		= 35,
        },
    },
    /* Samsung K9G4G08U0M */
    [9] = {
        .vendor_id	= 0xEC,
		.device_id	= 0xDC,
		.capacity 	= 512,
		.timing		= {
			.trp		= 15,
			.trh		= 10,
			.twp		= 15,
			.twh		= 15,
			.tcs		= 15,
			.twhr		= 60,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 18,
			.tadl		= 50,
        },
    },
	/* Toshiba TH58NVG4D4CTG00 */
	[10] = {
		.vendor_id	= 0x98,
		.device_id	= 0xD5,
		.capacity	= 2048,
		.timing		= {
			.trp		= 41,
			.trh		= 15,
			.twp		= 15,
			.twh		= 10,
			.tcs		= 0,
			.twhr		= 30,
			.tcr_tar_trr	= 20,
			.twb		= 200,
			.trp_resp	= 41,
			.tadl		= 21,
        },
    },
    /* Toshiba TH58NVG3D4BTG00 */
    [11] = {
        .vendor_id	= 0x98,
		.device_id	= 0xD3,
		.capacity	= 1024,
		.timing		= {
			.trp		= 41,
			.trh		= 15,
			.twp		= 15,
			.twh		= 10,
			.tcs		= 0,
			.twhr		= 30,
			.tcr_tar_trr	= 10,
			.twb		= 20, 
			.trp_resp	= 200,
			.tadl		= 41,
		},
    },
    /* Toshiba TH58NVG2S3BFT00 */
    [12] = {
        .vendor_id	= 0x98,
		.device_id	= 0xDC,
		.capacity	= 512,
		.timing		= {
			.trp 		= 41,
			.trh		= 15,
			.twp		= 15,
			.twh		= 10,
			.tcs		= 0,
			.twhr		= 30,
			.tcr_tar_trr 	= 10,
			.twb		= 20,
			.trp_resp	= 200,
			.tadl		= 41,
		}
    },
	/* Samsung K9LBG08U0M */
    [13] = {
		.vendor_id	= 0xEC, 
		.device_id	= 0xD7,
		.capacity	= 4096,
		.timing		= {
			.trp		= 12,
			.trh		= 10,
			.twp		= 12,
			.twh		= 10,
			.tcs		= 20,
			.twhr		= 60,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 20,
			.tadl		= 100,
        },
    },
    /* Hynix H8BES0UQ0MCP */
	[14] = {
		.vendor_id	= 0xAD,
		.device_id	= 0xBC,
		.capacity	= 512,
		.timing		= { 
			.trp		= 25,
			.trh		= 10,
			.twp		= 25,
			.twh		= 15,
			.tcs		= 35,
			.twhr		= 60,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 30,
			.tadl		= 100,
        },
    },
	/* Hynix H8BCS0SJ0MCP */
    [15] = {
		.vendor_id 	= 0xAD,
		.device_id	= 0xBA,
		.capacity	= 256,
		.timing		= {
			.trp		= 25,
			.trh		= 15,
			.twp		= 25,
			.twh		= 15,
			.tcs		= 35,
			.twhr		= 60,
			.tcr_tar_trr	= 25,
			.twb		= 100,
			.trp_resp	= 30,
			.tadl		= 100,
        },
	},
	/* Hynix H8BCS0RJ0MCP */
	[16] = {
		.vendor_id 	= 0xAD,
		.device_id	= 0xAA,
		.capacity	= 256,
		.timing		= {
			.trp		= 25,
			.trh		= 10,
			.twp		= 25,
			.twh		= 15,
			.tcs		= 35,
			.twhr		= 60,
			.tcr_tar_trr	= 25,
			.twb		= 100,
			.trp_resp	= 30,
			.tadl		= 100,
        },
    },
    /* Numonyx MCP - NAND02GR3B2D*/ 
    [17] = {
		.vendor_id	= 0x20, 
		.device_id	= 0xAA,
		.capacity	= 256,
		.timing		= { 
			.trp		= 25,
			.trh		= 15,
			.twp		= 25,
			.twh		= 15,
			.tcs		= 35,
			.twhr		= 60, 
			.tcr_tar_trr	= 25,
			.twb		= 100,
			.trp_resp	= 30,
			.tadl		= 100,
        },
	},
	/* Hynix HY27UF084G2B (readid 4th byte 0x95) */
	[18] = {
		.vendor_id	= 0xAD,
		.device_id	= 0xDC,
		.capacity	= 512,
		.timing		= {
			.trp		= 12,
			.trh		= 10,
			.twp		= 12,
			.twh		= 10,
			.tcs		= 20,
			.twhr		= 80,
			.tcr_tar_trr	= 20,
			.twb		= 100,
			.trp_resp	= 20,
			.tadl		= 70,
        },
     },
};

/*
	Default NAND layout is:
	2048K@6784K(misc)
	5120K@9344K(recovery),
	8192K@14976K(boot),
	125440K@23680K(system),
	32768K@149632K(cache),
	4096K@182912K(staging),
	336256K@187520K(userdata)
	
	Can be overriden from the command line
*/
	
static struct mtd_partition shuttle_nand_partitions[] = {
	[0] = {
		.name		= "misc",
		.offset		=  6784*1024,
		.size		=  2048*1024,
		.mask_flags	= MTD_WRITEABLE, /* r/o */
	},
	[1] = {
		.name		= "recovery",
		.offset		=  9344*1024,
		.size		=  5120*1024,
		.mask_flags	= MTD_WRITEABLE, /* r/o */
	},
	[2] = {
		.name		= "boot",
		.offset		= 14976*1024,
		.size		=  8192*1024,
	},
	[3] = {
		.name		= "system",
		.offset		=  23680*1024,
		.size		= 125440*1024,
	},
	[4] = {
		.name		= "cache",
		.offset		= 149632*1024,
		.size		=  32768*1024,
	},
	[5] = {
		.name		= "staging",
		.offset		= 182912*1024,
		.size		=   4096*1024,
	},
	[6] = {
		.name		= "userdata",
		.offset		= 187520*1024,
		.size		= 336256*1024,
	},
};

static struct tegra_nand_platform shuttle_nand_data = {
	.max_chips	= 8,
	.chip_parms	= shuttle_nand_chip_parms,
	.nr_chip_parms  = ARRAY_SIZE(shuttle_nand_chip_parms),
	.parts		= shuttle_nand_partitions,
	.nr_parts	= ARRAY_SIZE(shuttle_nand_partitions),
};

static struct resource resources_nand[] = {
	[0] = {
		.start  = INT_NANDFLASH,
		.end    = INT_NANDFLASH,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device tegra_nand_device = {
	.name           = "tegra_nand",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(resources_nand),
	.resource       = resources_nand,
	.dev            = {
		.platform_data = &shuttle_nand_data,
	},
};

int __init shuttle_nand_register_devices(void)
{

	/* Enable writes on NAND */
	gpio_request(SHUTTLE_NAND_WPN, "nand_wp#");
	gpio_direction_output(SHUTTLE_NAND_WPN, 1);

	platform_device_register(&tegra_nand_device);
	return 0;
} 


