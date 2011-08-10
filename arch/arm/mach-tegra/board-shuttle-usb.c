/*
 * arch/arm/mach-tegra/board-shuttle-usb.c
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

/* All configurations related to USB */
 
#include <linux/console.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/i2c-tegra.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/io.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/gpio.h>
#include <mach/clk.h>
#include <mach/usb_phy.h>
#include <mach/system.h>

#include <linux/usb/android_composite.h>
#include <linux/usb/f_accessory.h>

#include "board.h"
#include "board-shuttle.h"
#include "clock.h"
#include "gpio-names.h"
#include "devices.h"

static char *usb_functions_acm_mtp_ums[] = { "acm", "mtp", "usb_mass_storage" };
static char *usb_functions_acm_mtp_adb_ums[] = { "acm", "mtp", "adb", "usb_mass_storage" };

static char *tegra_android_functions_all[] = {
#ifdef CONFIG_USB_ANDROID_MTP
	"mtp",
#endif
#ifdef CONFIG_USB_ANDROID_ACM	
	"acm",
#endif
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	"usb_mass_storage",
#endif
};

static struct android_usb_product usb_products[] = {
	{
		.product_id     = 0x7102,
		.num_functions  = ARRAY_SIZE(usb_functions_acm_mtp_ums),
		.functions      = usb_functions_acm_mtp_ums,
	},
	{
		.product_id     = 0x7100,
		.num_functions  = ARRAY_SIZE(usb_functions_acm_mtp_adb_ums),
		.functions      = usb_functions_acm_mtp_adb_ums,
	},
};

/* standard android USB platform data */
static struct android_usb_platform_data andusb_plat = {
	.vendor_id 			= 0x0955,
	.product_id 		= 0x7100,
	.manufacturer_name 	= "NVIDIA",
	.product_name      	= "Shuttle",
	.serial_number     	= "0000",
	.num_products 		= ARRAY_SIZE(usb_products),
	.products 			= usb_products,
	.num_functions 		= ARRAY_SIZE(tegra_android_functions_all),
	.functions 			= tegra_android_functions_all,
};

#ifdef CONFIG_USB_ANDROID_ACM	
static struct acm_platform_data tegra_acm_platform_data = {
	.num_inst = 1,
};
static struct platform_device tegra_usb_acm_device = {
	.name 	 = "acm",
	.id 	 = -1,
	.dev = {
		.platform_data = &tegra_acm_platform_data,
	},
};
#endif

#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
static struct usb_mass_storage_platform_data tegra_usb_ums_platform = {
	.vendor  = "NVIDIA",
	.product = "Tegra 2",
	.nluns 	 = 1,
};
static struct platform_device tegra_usb_ums_device = {
	.name 	 = "usb_mass_storage",
	.id 	 = -1,
	.dev = {
		.platform_data = &tegra_usb_ums_platform,
	},
};
#endif

static struct platform_device androidusb_device = {
	.name   = "android_usb",
	.id     = -1,
	.dev    = {
		.platform_data  = &andusb_plat,
	},
};

static struct tegra_utmip_config utmi_phy_config[] = {
	[0] = {
		.hssync_start_delay = 0,
		.idle_wait_delay 	= 17,
		.elastic_limit 		= 16,
		.term_range_adj 	= 6, 	/*  xcvr_setup = 9 with term_range_adj = 6 gives the maximum guard around */
		.xcvr_setup 		= 9, 	/*  the USB electrical spec. This is true across fast and slow chips, high */
									/*  and low voltage and hot and cold temperatures */
		.xcvr_lsfslew 		= 2,	/*  -> To slow rise and fall times in low speed eye diagrams in host mode */
		.xcvr_lsrslew 		= 2,	/*                                                                        */
	},
	[1] = {
		.hssync_start_delay = 0,
		.idle_wait_delay 	= 17,
		.elastic_limit 		= 16,
		.term_range_adj 	= 6,	/*  -> xcvr_setup = 9 with term_range_adj = 6 gives the maximum guard around */
		.xcvr_setup 		= 9,	/*     the USB electrical spec. This is true across fast and slow chips, high */
									/*     and low voltage and hot and cold temperatures */
		.xcvr_lsfslew 		= 2,	/*  -> To slow rise and fall times in low speed eye diagrams in host mode */
		.xcvr_lsrslew 		= 2,	/*                                                                        */
	},
};

/* ULPI is managed by an SMSC3317 on the Harmony board */
static struct tegra_ulpi_config ulpi_phy_config = {
	.reset_gpio = SHUTTLE_USB1_RESET,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
	.clk = "cdev2",
#else
	.clk = "clk_dev2",
#endif
#if 0
	.inf_type = TEGRA_USB_LINK_ULPI,
#endif
};

static struct tegra_ehci_platform_data tegra_ehci_pdata[] = {
	[0] = {
		.phy_config = &utmi_phy_config[0],
		.operating_mode = TEGRA_USB_HOST, /* DEVICE is slave here */
		.power_down_on_bus_suspend = 1,
	},
	[1] = {
		.phy_config = &ulpi_phy_config,
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
	},
	[2] = {
		.phy_config = &utmi_phy_config[1],
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
	},
};


#if 0
/* OTG gadget device */
static u64 tegra_otg_dmamask = DMA_BIT_MASK(32);

static struct resource tegra_otg_resources[] = {
	[0] = {
		.start  = TEGRA_USB_BASE,
		.end    = TEGRA_USB_BASE + TEGRA_USB_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = INT_USB,
		.end    = INT_USB,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct fsl_usb2_platform_data tegra_otg_pdata = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode		= FSL_USB2_PHY_UTMI,
};

static struct platform_device tegra_otg = {
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,36)	
	.name = "fsl-tegra-udc",
#else
	.name = "fsl-usb2-udc",
#endif
	.id   = -1,
	.dev  = {
		.dma_mask			= &tegra_otg_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data 		= &tegra_otg_pdata,
	},
	.resource = tegra_otg_resources,
	.num_resources = ARRAY_SIZE(tegra_otg_resources),
}; 


/////


#endif

struct platform_device *usb_host_pdev = NULL;

static void tegra_usb_otg_host_register(void)
{
	int val;
	if (usb_host_pdev != NULL)
		return;

	usb_host_pdev = platform_device_alloc(tegra_ehci1_device.name,
			tegra_ehci1_device.id);
	if (!usb_host_pdev)
		return;

	val = platform_device_add_resources(usb_host_pdev, tegra_ehci1_device.resource,
		tegra_ehci1_device.num_resources);
	if (val)
		goto error;

	usb_host_pdev->dev.dma_mask =  tegra_ehci1_device.dev.dma_mask;
	usb_host_pdev->dev.coherent_dma_mask = tegra_ehci1_device.dev.coherent_dma_mask;
	usb_host_pdev->dev.platform_data = tegra_ehci1_device.dev.platform_data;

	val = platform_device_add(usb_host_pdev);
	if (val)
		goto error_add;

	/* Place interface in host mode */
	gpio_direction_input(SHUTTLE_USB0_VBUS );
		
	return;

error_add:
error:
	pr_err("%s: failed to add the host controller device\n", __func__);
	platform_device_put(usb_host_pdev);
	usb_host_pdev = NULL;
	return;
}

static void tegra_usb_otg_host_unregister(void)
{
	if (usb_host_pdev == NULL)
		return;

	/* Place interfase in gadget mode */
	gpio_direction_output(SHUTTLE_USB0_VBUS, 0 ); /* Gadget */

	platform_device_unregister(usb_host_pdev);
	usb_host_pdev = NULL;
}


static struct platform_device *shuttle_usb_devices[] __initdata = {
#ifdef CONFIG_USB_ANDROID_ACM	
	&tegra_usb_acm_device,
#endif
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	&tegra_usb_ums_device,
#endif
	&androidusb_device,		/* should come AFTER ums and acm */
	&tegra_udc_device, 		/* USB gadget */
	&tegra_ehci2_device,
	&tegra_ehci3_device,
};

static ssize_t usb_read(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	int ret = 0;
	
	if (!strcmp(attr->attr.name, "host_mode")) {
		if (usb_host_pdev != NULL)
			ret = 1;
	}

	if (!ret) {
		return strlcpy(buf, "0\n", 3);
	} else {
		return strlcpy(buf, "1\n", 3);
	}
}

static ssize_t usb_write(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	unsigned long on = simple_strtoul(buf, NULL, 10);

	if (!strcmp(attr->attr.name, "host_mode")) {
		if (on)
			tegra_usb_otg_host_register();
		else
			tegra_usb_otg_host_unregister();
	} 

	return count;
}

static DEVICE_ATTR(host_mode, 0644, usb_read, usb_write);

static struct attribute *usb_sysfs_entries[] = {
	&dev_attr_host_mode.attr,
	NULL
};

static struct attribute_group usb_attr_group = {
	.name	= NULL,
	.attrs	= usb_sysfs_entries,
}; 


int __init shuttle_usb_register_devices(void)
{
	int ret;
	
	tegra_ehci1_device.dev.platform_data = &tegra_ehci_pdata[0];
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	tegra_ehci3_device.dev.platform_data = &tegra_ehci_pdata[2];
	
	/* If in host mode, set VBUS to 1 */
	gpio_request(SHUTTLE_USB0_VBUS, "USB0 VBUS"); /* VBUS switch, perhaps ? -- Tied to what? -- should require +5v ... */
	
	/* 0 = Gadget */
	gpio_direction_output(SHUTTLE_USB0_VBUS, 0 ); /* Gadget */
	
	ret = platform_add_devices(shuttle_usb_devices, ARRAY_SIZE(shuttle_usb_devices));
	if (ret)
		return ret;

	/* Attach an attribute to the already registered udc device to switch it to host mode */
	return sysfs_create_group(&tegra_udc_device.dev.kobj, &usb_attr_group); 
}
