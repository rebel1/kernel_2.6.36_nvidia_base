/*
 * arch/arm/mach-tegra/board-whistler.c
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/synaptics_i2c_rmi.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/i2c-tegra.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/usb/android_composite.h>
#include <linux/memblock.h>

#include <mach/clk.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/iomap.h>
#include <mach/io.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "board.h"
#include "clock.h"
#include "board-whistler.h"
#include "devices.h"
#include "gpio-names.h"
#include "fuse.h"

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTA_BASE),
		.mapbase	= TEGRA_UARTA_BASE,
		.irq		= INT_UARTA,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0,
	}
};

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = debug_uart_platform_data,
	},
};

static __initdata struct tegra_clk_init_table whistler_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uarta",	"pll_p",	216000000,	true},
	{ "pll_m",	"clk_m",	600000000,	true},
	{ "pwm",	"clk_32k",	32768,		false},
	{ "kbc",	"clk_32k",	32768,		true},
	{ NULL,		NULL,		0,		0},
};

static char *usb_functions[] = { "mtp" };
static char *usb_functions_adb[] = { "mtp", "adb" };

static struct android_usb_product usb_products[] = {
	{
		.product_id     = 0x7102,
		.num_functions  = ARRAY_SIZE(usb_functions),
		.functions      = usb_functions,
	},
	{
		.product_id     = 0x7100,
		.num_functions  = ARRAY_SIZE(usb_functions_adb),
		.functions      = usb_functions_adb,
	},
};

/* standard android USB platform data */
static struct android_usb_platform_data andusb_plat = {
	.vendor_id              = 0x0955,
	.product_id             = 0x7100,
	.manufacturer_name      = "NVIDIA",
	.product_name           = "Whistler",
	.serial_number          = NULL,
	.num_products = ARRAY_SIZE(usb_products),
	.products = usb_products,
	.num_functions = ARRAY_SIZE(usb_functions_adb),
	.functions = usb_functions_adb,
};

static struct platform_device androidusb_device = {
	.name   = "android_usb",
	.id     = -1,
	.dev    = {
		.platform_data  = &andusb_plat,
	},
};

static struct tegra_i2c_platform_data whistler_i2c1_platform_data = {
	.adapter_nr	= 0,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
};

static struct tegra_i2c_platform_data whistler_i2c2_platform_data = {
	.adapter_nr	= 1,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
};

static struct tegra_i2c_platform_data whistler_i2c3_platform_data = {
	.adapter_nr	= 3,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
};

static struct tegra_i2c_platform_data whistler_dvc_platform_data = {
	.adapter_nr	= 4,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.is_dvc		= true,
};

static void whistler_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &whistler_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &whistler_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &whistler_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &whistler_dvc_platform_data;

	platform_device_register(&tegra_i2c_device4);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device1);
}

static struct platform_device *whistler_devices[] __initdata = {
	&tegra_otg_device,
	&androidusb_device,
	&debug_uart,
	&pmu_device,
	&tegra_udc_device,
	&tegra_gart_device,
	&tegra_wdt_device,
	&tegra_avp_device,
};

static struct synaptics_i2c_rmi_platform_data synaptics_pdata= {
	.flags			= SYNAPTICS_FLIP_X | SYNAPTICS_FLIP_Y | SYNAPTICS_SWAP_XY,
	.irqflags		= IRQF_TRIGGER_LOW,
};

static const struct i2c_board_info whistler_i2c_touch_info[] = {
	{
		I2C_BOARD_INFO("synaptics-rmi-ts", 0x20),
		.irq		= TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PC6),
		.platform_data	= &synaptics_pdata,
	},
};

static int __init whistler_touch_init(void)
{
	i2c_register_board_info(0, whistler_i2c_touch_info, 1);

	return 0;
}

static void __init tegra_whistler_init(void)
{
	char serial[20];

	tegra_common_init();
	tegra_clk_init_from_table(whistler_clk_init_table);
	whistler_pinmux_init();

	snprintf(serial, sizeof(serial), "%llx", tegra_chip_uid());
	andusb_plat.serial_number = kstrdup(serial, GFP_KERNEL);

	platform_add_devices(whistler_devices, ARRAY_SIZE(whistler_devices));

	whistler_sdhci_init();
	whistler_i2c_init();
	whistler_regulator_init();
	whistler_panel_init();
	whistler_touch_init();
	whistler_kbc_init();
}

int __init tegra_whistler_protected_aperture_init(void)
{
	tegra_protected_aperture_init(tegra_grhost_aperture);
	return 0;
}

void __init tegra_whistler_reserve(void)
{
	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4K of memory for safety\n");

	tegra_reserve(SZ_128M, SZ_8M, SZ_16M);
}

MACHINE_START(WHISTLER, "whistler")
	.boot_params    = 0x00000100,
	.phys_io        = IO_APB_PHYS,
	.io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
	.init_irq       = tegra_init_irq,
	.init_machine   = tegra_whistler_init,
	.map_io         = tegra_map_common_io,
	.reserve        = tegra_whistler_reserve,
	.timer          = &tegra_timer,
MACHINE_END
