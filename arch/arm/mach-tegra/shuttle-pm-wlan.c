/*
 * arch/arm/mach-tegra/shuttle-pm-wlan.c
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

/* Wlan is on SDIO bus card attached to sdhci.0 and it is a AR6002 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <linux/if.h>
#include <linux/skbuff.h>
#include <linux/random.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/rfkill.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>

#include "board-shuttle.h"
#include "gpio-names.h"

struct shuttle_pm_wlan_data {
	struct rfkill *rfkill;
#ifdef CONFIG_PM
	int pre_resume_state;
	int keep_on_in_suspend;
#endif 
	int powered_up;
};


/* Power control */
static void __shuttle_pm_wlan_toggle_radio(struct device *dev, unsigned int on)
{
	struct shuttle_pm_wlan_data *wlan_data = dev_get_drvdata(dev);

	/* Avoid turning it on if already on */
	if (wlan_data->powered_up == on)
		return;
	
	if (on) {

		shuttle_wlan_bt_poweron();
		dev_info(dev, "WLAN adapter enabled\n");
		
	} else {
	
		shuttle_wlan_bt_poweroff();
		dev_info(dev, "WLAN adapter disabled\n");
		
	}
	
	/* store new state */
	wlan_data->powered_up = on;
	
}

static void shuttle_wlan_set_carddetect(struct device *dev,int cd)
{
	dev_dbg(dev,"%s: %d\n", __func__, cd);

	/* Sequence varies if powering up or down */
	if (cd) {
	
		/* power module up */
		__shuttle_pm_wlan_toggle_radio(dev,cd);
	
		/* notify the SDIO layer of the CD change */
		shuttle_wifi_set_cd(cd);
		
	} else {
	
		/* notify the SDIO layer of the CD change */
		shuttle_wifi_set_cd(cd);

		/* power module down */
		__shuttle_pm_wlan_toggle_radio(dev,cd);

	}
} 

/* rfkill */
static int shuttle_wlan_rfkill_set_block(void *data, bool blocked)
{
	struct device *dev = data;
	dev_dbg(dev, "blocked %d\n", blocked);

	/* manage rfkill by 'inserting' or 'removing' the virtual adapter */
	shuttle_wlan_set_carddetect(dev,!blocked);
	
	return 0;
}

static const struct rfkill_ops shuttle_wlan_rfkill_ops = {
    .set_block = shuttle_wlan_rfkill_set_block,
};

static ssize_t wlan_read(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	int ret = 0;
	struct shuttle_pm_wlan_data *wlan_data = dev_get_drvdata(dev);
	
	if (!strcmp(attr->attr.name, "power_on")) {
		ret = wlan_data->powered_up;
	} else if (!strcmp(attr->attr.name, "reset")) {
		ret = !wlan_data->powered_up;
	}
#ifdef CONFIG_PM
	else if (!strcmp(attr->attr.name, "keep_on_in_suspend")) {
		ret = wlan_data->keep_on_in_suspend;
	}
#endif 	 
	
	return strlcpy(buf, (!ret) ? "0\n" : "1\n", 3);
}

static ssize_t wlan_write(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	unsigned long on = simple_strtoul(buf, NULL, 10);
	struct shuttle_pm_wlan_data *wlan_data = dev_get_drvdata(dev);

	if (!strcmp(attr->attr.name, "power_on")) {
	
		rfkill_set_sw_state(wlan_data->rfkill, !on); /* here it receives the blocked state */
		shuttle_wlan_set_carddetect(dev, !!on);
		
	} else if (!strcmp(attr->attr.name, "reset")) {
	
		/* reset is low-active, so we need to invert */
		rfkill_set_sw_state(wlan_data->rfkill, !!on); /* here it receives the blocked state */
		shuttle_wlan_set_carddetect(dev, !on);

	}
#ifdef CONFIG_PM
	else if (!strcmp(attr->attr.name, "keep_on_in_suspend")) {
		wlan_data->keep_on_in_suspend = on;
	}
#endif  	

	return count;
}

static DEVICE_ATTR(power_on, 0666, wlan_read, wlan_write);
static DEVICE_ATTR(reset, 0666, wlan_read, wlan_write);
#ifdef CONFIG_PM
static DEVICE_ATTR(keep_on_in_suspend, 0666, wlan_read, wlan_write);
#endif

#ifdef CONFIG_PM
static int shuttle_wlan_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct shuttle_pm_wlan_data *wlan_data = dev_get_drvdata(&pdev->dev);

	dev_dbg(&pdev->dev, "suspending\n");

	wlan_data->pre_resume_state = wlan_data->powered_up;
	
	if (!wlan_data->keep_on_in_suspend)
		shuttle_wlan_set_carddetect(&pdev->dev, 0);
	else
		dev_warn(&pdev->dev, "keeping WLAN ON during suspend\n");
		
	return 0;
}

static int shuttle_wlan_resume(struct platform_device *pdev)
{
	struct shuttle_pm_wlan_data *wlan_data = dev_get_drvdata(&pdev->dev);
	dev_dbg(&pdev->dev, "resuming\n");

	shuttle_wlan_set_carddetect(&pdev->dev, wlan_data->pre_resume_state);
	return 0;
}

#else
#define shuttle_wlan_suspend	NULL
#define shuttle_wlan_resume		NULL
#endif

static struct attribute *shuttle_wlan_sysfs_entries[] = {
	&dev_attr_power_on.attr,
	&dev_attr_reset.attr,
#ifdef CONFIG_PM	
	&dev_attr_keep_on_in_suspend.attr,
#endif
	NULL
};

static struct attribute_group shuttle_wlan_attr_group = {
	.name	= NULL,
	.attrs	= shuttle_wlan_sysfs_entries,
};

/* ----- Initialization/removal -------------------------------------------- */
static int __init shuttle_wlan_probe(struct platform_device *pdev)
{
	/* default-on */
	const int default_blocked_state = 0;
	
	struct rfkill *rfkill;
	struct shuttle_pm_wlan_data *wlan_data;
	int ret;

	wlan_data = kzalloc(sizeof(*wlan_data), GFP_KERNEL);
	if (!wlan_data) {
		dev_err(&pdev->dev, "no memory for context\n");
		return -ENOMEM;
	}
	dev_set_drvdata(&pdev->dev, wlan_data);

	ret = shuttle_wlan_bt_init();
	if (ret) {
		dev_err(&pdev->dev, "unable to init wlan/bt module\n");
		kfree(wlan_data);
		dev_set_drvdata(&pdev->dev, NULL);
		return ret;
	}

	rfkill = rfkill_alloc(pdev->name, &pdev->dev, RFKILL_TYPE_WLAN,
							&shuttle_wlan_rfkill_ops, &pdev->dev);


	if (!rfkill) {
		dev_err(&pdev->dev, "Failed to allocate rfkill\n");
		shuttle_wlan_bt_deinit();
		kfree(wlan_data);
		dev_set_drvdata(&pdev->dev, NULL);
		return -ENOMEM;
	}
	wlan_data->rfkill = rfkill;

	/* Tell the SDIO stack the card is not there... otherwise, it could 
	   be using a non powered card not properly initialized */
	shuttle_wlan_set_carddetect(&pdev->dev,0);
		
	/* Set the default state */
	rfkill_init_sw_state(rfkill, default_blocked_state);
	shuttle_wlan_set_carddetect(&pdev->dev, !default_blocked_state);

	ret = rfkill_register(rfkill);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register rfkill\n");
		rfkill_destroy(rfkill);		
		shuttle_wlan_bt_deinit();
		kfree(wlan_data);
		dev_set_drvdata(&pdev->dev, NULL);
		return ret;
	}

	dev_info(&pdev->dev, "WLAN RFKill driver loaded\n");
	
	return sysfs_create_group(&pdev->dev.kobj, &shuttle_wlan_attr_group);
}

static int shuttle_wlan_remove(struct platform_device *pdev)
{
	struct shuttle_pm_wlan_data *wlan_data = dev_get_drvdata(&pdev->dev);
	if (!wlan_data)
		return 0;
	
	sysfs_remove_group(&pdev->dev.kobj, &shuttle_wlan_attr_group);

	rfkill_unregister(wlan_data->rfkill);
	rfkill_destroy(wlan_data->rfkill);

	shuttle_wlan_set_carddetect(&pdev->dev, 0);
	
	shuttle_wlan_bt_deinit();

	kfree(wlan_data);
	dev_set_drvdata(&pdev->dev, NULL);
	
	return 0;
}

static struct platform_driver shuttle_wlan_driver = {
	.probe		= shuttle_wlan_probe,
	.remove		= shuttle_wlan_remove,
	.suspend	= shuttle_wlan_suspend,
	.resume		= shuttle_wlan_resume,
	.driver		= {
		.name		= "shuttle-pm-wlan",
	},
};

static int __devinit shuttle_wlan_init(void)
{
	return platform_driver_register(&shuttle_wlan_driver);
}

static void shuttle_wlan_exit(void)
{
	platform_driver_unregister(&shuttle_wlan_driver);
}

module_init(shuttle_wlan_init);
module_exit(shuttle_wlan_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_DESCRIPTION("Shuttle WLAN power management");
