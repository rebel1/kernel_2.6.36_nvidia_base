/*
 * arch/arm/mach-tegra/board-shuttle.c
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

#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/dma-mapping.h>
#include <linux/fsl_devices.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/pda_power.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/i2c-tegra.h>
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
#include <mach/sdhci.h>
#include <mach/gpio.h>
#include <mach/clk.h>
#include <mach/usb_phy.h>
#include <mach/i2s.h>
#include <mach/system.h>
#include <mach/nvmap.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,38)	
#include <mach/suspend.h>
#else
#include "pm.h"
#endif

#include <linux/usb/android_composite.h>
#include <linux/usb/f_accessory.h>

#include "board.h"
#include "board-shuttle.h"
#include "clock.h"
#include "gpio-names.h"
#include "devices.h"
#include "wakeups-t2.h"


/* NVidia bootloader tags */
#define ATAG_NVIDIA		0x41000801

#define ATAG_NVIDIA_RM			0x1
#define ATAG_NVIDIA_DISPLAY		0x2
#define ATAG_NVIDIA_FRAMEBUFFER		0x3
#define ATAG_NVIDIA_CHIPSHMOO		0x4
#define ATAG_NVIDIA_CHIPSHMOOPHYS	0x5
#define ATAG_NVIDIA_PRESERVED_MEM_0	0x10000
#define ATAG_NVIDIA_PRESERVED_MEM_N	2
#define ATAG_NVIDIA_FORCE_32		0x7fffffff

struct tag_tegra {
	__u32 bootarg_key;
	__u32 bootarg_len;
	char bootarg[1];
};

static int __init parse_tag_nvidia(const struct tag *tag)
{
	return 0;
}
__tagtable(ATAG_NVIDIA, parse_tag_nvidia);

static atomic_t shuttle_3g_gps_powered = ATOMIC_INIT(0);
void shuttle_3g_gps_poweron(void)
{
	if (atomic_inc_return(&shuttle_3g_gps_powered) == 1) {
		pr_info("Enabling 3G/GPS module\n");
		/* 3G/GPS power on sequence */
		gpio_set_value(SHUTTLE_3GGPS_DISABLE, 0); /* Enable power */
		msleep(2);
	}
}
EXPORT_SYMBOL_GPL(shuttle_3g_gps_poweron);

void shuttle_3g_gps_poweroff(void)
{
	if (atomic_dec_return(&shuttle_3g_gps_powered) == 0) {
		pr_info("Disabling 3G/GPS module\n");
		/* 3G/GPS power on sequence */
		gpio_set_value(SHUTTLE_3GGPS_DISABLE, 1); /* Disable power */
		msleep(2);
	}
}
EXPORT_SYMBOL_GPL(shuttle_3g_gps_poweroff);

static atomic_t shuttle_3g_gps_inited = ATOMIC_INIT(0);
void shuttle_3g_gps_init(void)
{
	if (atomic_inc_return(&shuttle_3g_gps_inited) == 1) {
		gpio_request(SHUTTLE_3GGPS_DISABLE, "gps_disable");
		gpio_direction_output(SHUTTLE_3GGPS_DISABLE, 1);
	}
}
EXPORT_SYMBOL_GPL(shuttle_3g_gps_init);

void shuttle_3g_gps_deinit(void)
{
	atomic_dec(&shuttle_3g_gps_inited);
}
EXPORT_SYMBOL_GPL(shuttle_3g_gps_deinit);

static struct tegra_suspend_platform_data shuttle_suspend = {
	.cpu_timer = 5000,
	.cpu_off_timer = 5000,
	.core_timer = 0x7e7e,
	.core_off_timer = 0x7f,
    .corereq_high = false,
	.sysclkreq_high = true,
	.suspend_mode = TEGRA_SUSPEND_LP0,
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,38) /* NB: 2.6.39+ handles this automatically */
	.separate_req = true,	
	.wake_enb = SHUTTLE_WAKE_KEY_POWER | 
				SHUTTLE_WAKE_KEY_RESUME | 
				TEGRA_WAKE_RTC_ALARM,
	.wake_high = TEGRA_WAKE_RTC_ALARM,
	.wake_low = SHUTTLE_WAKE_KEY_POWER | 
				SHUTTLE_WAKE_KEY_RESUME,
	.wake_any = 0,
#endif
};

static void __init tegra_shuttle_init(void)
{
	struct clk *clk;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)	
	tegra_common_init();
#endif

	/* force consoles to stay enabled across suspend/resume */
	// console_suspend_enabled = 0;

	/* Init the suspend information */
	tegra_init_suspend(&shuttle_suspend);

	/* Set the SDMMC2 (wifi) tap delay to 6.  This value is determined
	 * based on propagation delay on the PCB traces. */
	clk = clk_get_sys("sdhci-tegra.1", NULL);
	if (!IS_ERR(clk)) {
		tegra_sdmmc_tap_delay(clk, 6);
		clk_put(clk);
	} else {
		pr_err("Failed to set wifi sdmmc tap delay\n");
	}

	/* Initialize the clocks */
	shuttle_clks_init();

	/* Initialize the pinmux */
	shuttle_pinmux_init();

	/* Register i2c devices - required for Power management and MUST be done before the power register */
	shuttle_i2c_register_devices();

	/* Register the power subsystem - Including the poweroff handler - Required by all the others */
	shuttle_power_register_devices();
	
	/* Register the USB device */
	shuttle_usb_register_devices();

	/* Register UART devices */
	shuttle_uart_register_devices();
	
	/* Register SPI devices */
	shuttle_spi_register_devices();

	/* Register GPU devices */
	shuttle_gpu_register_devices();

	/* Register Audio devices */
	shuttle_audio_register_devices();

	/* Register AES encryption devices */
	shuttle_aes_register_devices();

	/* Register Watchdog devices */
	shuttle_wdt_register_devices();

	/* Register all the keyboard devices */
	shuttle_keyboard_register_devices();
	
	/* Register touchscreen devices */
	shuttle_touch_register_devices();
	
	/* Register SDHCI devices */
	shuttle_sdhci_register_devices();

	/* Register accelerometer device */
	shuttle_sensors_register_devices();
	
	/* Register wlan powermanagement devices */
	shuttle_wlan_pm_register_devices();
	
	/* Register gps powermanagement devices */
	shuttle_gps_pm_register_devices();

	/* Register gsm powermanagement devices */
	shuttle_gsm_pm_register_devices();
	
	/* Register Bluetooth powermanagement devices */
	shuttle_bt_pm_register_devices();

	/* Register Camera powermanagement devices */
	shuttle_camera_pm_register_devices();

	/* Register NAND flash devices */
	shuttle_nand_register_devices();
	
#if 0
	/* Finally, init the external memory controller and memory frequency scaling
   	   NB: This is not working on P10AN01. And seems there is no point in fixing it,
	   as the EMC clock is forced to the maximum speed as soon as the 2D/3D engine
	   starts.*/
	shuttle_init_emc();
#endif
	
}

static void __init tegra_shuttle_reserve(void)
{
	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4K of memory for safety\n");

#if defined(DYNAMIC_GPU_MEM)
	/* Reserve the graphics memory */
	tegra_reserve(SHUTTLE_GPU_MEM_SIZE, SHUTTLE_FB1_MEM_SIZE, SHUTTLE_FB2_MEM_SIZE);
#endif
}

static void __init tegra_shuttle_fixup(struct machine_desc *desc,
	struct tag *tags, char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PHYS_OFFSET;
#if defined(DYNAMIC_GPU_MEM)
	mi->bank[0].size  = SHUTTLE_MEM_SIZE;
#else
	mi->bank[0].size  = SHUTTLE_MEM_SIZE - SHUTTLE_GPU_MEM_SIZE;
#endif
} 

/* the Shuttle bootloader identifies itself as MACH_TYPE_HARMONY [=2731]
   or as MACH_TYPE_LEGACY[=3333]. We MUST handle both cases in order
   to make the kernel bootable */
MACHINE_START(HARMONY, "harmony")
	.boot_params	= 0x00000100,
	.map_io         = tegra_map_common_io,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)		
	.init_early     = tegra_init_early,
#else
	.phys_io		= IO_APB_PHYS,
	.io_pg_offst	= ((IO_APB_VIRT) >> 18) & 0xfffc,
#endif
	.init_irq       = tegra_init_irq,
	.timer          = &tegra_timer, 	
	.init_machine	= tegra_shuttle_init,
	.reserve		= tegra_shuttle_reserve,
	.fixup			= tegra_shuttle_fixup,
MACHINE_END

//#ifdef MACH_TYPE_TEGRA_LEGACY
//MACHINE_START(TEGRA_LEGACY, "tegra_legacy")
//#else
//MACHINE_START(LEGACY, "legacy")
//#endif
//	.boot_params	= 0x00000100,
//	.map_io         = tegra_map_common_io,
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)		
//	.init_early     = tegra_init_early,
//#else
//	.phys_io		= IO_APB_PHYS,
//	.io_pg_offst	= ((IO_APB_VIRT) >> 18) & 0xfffc,
//#endif
//	.init_irq       = tegra_init_irq,
//	.timer          = &tegra_timer, 	
//	.init_machine	= tegra_shuttle_init,
//	.reserve		= tegra_shuttle_reserve,
//	.fixup			= tegra_shuttle_fixup,
//MACHINE_END

#if 0
#define PMC_WAKE_STATUS 0x14

static int shuttle_wakeup_key(void)
{
	unsigned long status = 
		readl(IO_ADDRESS(TEGRA_PMC_BASE) + PMC_WAKE_STATUS);
	return status & TEGRA_WAKE_GPIO_PV2 ? KEY_POWER : KEY_RESERVED;
}
#endif


