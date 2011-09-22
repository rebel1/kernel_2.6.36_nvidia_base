/*
 * arch/arm/mach-tegra/shuttle-pm-gsm.c
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

/* GSM/UMTS power control via GPIO */
  
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/jiffies.h>
#include <linux/rfkill.h>

#include "board-shuttle.h"
#include "gpio-names.h"

#include <mach/hardware.h>
#include <asm/mach-types.h>

struct shuttle_pm_gsm_data {
	struct rfkill *rfkill;
#ifdef CONFIG_PM
	int pre_resume_state;
	int keep_on_in_suspend;
#endif
	int powered_up;
};

/* Power control */
static void __shuttle_pm_gsm_toggle_radio(struct device *dev, unsigned int on)
{
	struct shuttle_pm_gsm_data *gsm_data = dev_get_drvdata(dev);

	/* Avoid turning it on or off if already in that state */
	if (gsm_data->powered_up == on)
		return;
	
	if (on) {
	
		/* 3G/GPS power on sequence */
		shuttle_3g_gps_poweron();

	} else {
	
		shuttle_3g_gps_poweroff();
				
	}
	
	/* store new state */
	gsm_data->powered_up = on;
}

/* rfkill */
static int gsm_rfkill_set_block(void *data, bool blocked)
{
	struct device *dev = data;
	dev_dbg(dev, "blocked %d\n", blocked);

	__shuttle_pm_gsm_toggle_radio(dev, !blocked);

	return 0;
}

static const struct rfkill_ops shuttle_gsm_rfkill_ops = {
    .set_block = gsm_rfkill_set_block,
};

static ssize_t gsm_read(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	int ret = 0;
	struct shuttle_pm_gsm_data *gsm_data = dev_get_drvdata(dev);
	
	if (!strcmp(attr->attr.name, "power_on")) {
		ret = gsm_data->powered_up;
	} else if (!strcmp(attr->attr.name, "reset")) {
		ret = !gsm_data->powered_up;
	}
#ifdef CONFIG_PM
	else if (!strcmp(attr->attr.name, "keep_on_in_suspend")) {
		ret = gsm_data->keep_on_in_suspend;
	}
#endif 	

	return strlcpy(buf, (!ret) ? "0\n" : "1\n", 3);
}

static ssize_t gsm_write(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	unsigned long on = simple_strtoul(buf, NULL, 10);
	struct shuttle_pm_gsm_data *gsm_data = dev_get_drvdata(dev);

	if (!strcmp(attr->attr.name, "power_on")) {
	
		rfkill_set_sw_state(gsm_data->rfkill, !on); /* here it receives the blocked state */
		__shuttle_pm_gsm_toggle_radio(dev, !!on);
		
	} else if (!strcmp(attr->attr.name, "reset")) {
	
		/* reset is low-active, so we need to invert */
		rfkill_set_sw_state(gsm_data->rfkill, !!on); /* here it receives the blocked state */
		__shuttle_pm_gsm_toggle_radio(dev, !on);
		
	}
#ifdef CONFIG_PM
	else if (!strcmp(attr->attr.name, "keep_on_in_suspend")) {
		gsm_data->keep_on_in_suspend = on;
	}
#endif 

	return count;
}

static DEVICE_ATTR(power_on, 0666, gsm_read, gsm_write);
static DEVICE_ATTR(reset, 0666, gsm_read, gsm_write);
#ifdef CONFIG_PM
static DEVICE_ATTR(keep_on_in_suspend, 0666, gsm_read, gsm_write);
#endif


#ifdef CONFIG_PM
static int shuttle_gsm_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct shuttle_pm_gsm_data *gsm_data = dev_get_drvdata(&pdev->dev);

	dev_dbg(&pdev->dev, "suspending\n");

	gsm_data->pre_resume_state = gsm_data->powered_up;
	if (!gsm_data->keep_on_in_suspend)
		__shuttle_pm_gsm_toggle_radio(&pdev->dev, 0);
	else
		dev_warn(&pdev->dev, "keeping GSM ON during suspend\n");
		
	return 0;
}

static int shuttle_gsm_resume(struct platform_device *pdev)
{
	struct shuttle_pm_gsm_data *gsm_data = dev_get_drvdata(&pdev->dev);
	dev_dbg(&pdev->dev, "resuming\n");

	__shuttle_pm_gsm_toggle_radio(&pdev->dev, gsm_data->pre_resume_state);
	return 0;
}
#else
#define shuttle_gsm_suspend	NULL
#define shuttle_gsm_resume		NULL
#endif

static struct attribute *shuttle_gsm_sysfs_entries[] = {
	&dev_attr_power_on.attr,
	&dev_attr_reset.attr,
#ifdef CONFIG_PM	
	&dev_attr_keep_on_in_suspend.attr,
#endif
	NULL
};

static struct attribute_group shuttle_gsm_attr_group = {
	.name	= NULL,
	.attrs	= shuttle_gsm_sysfs_entries,
};

static int __init shuttle_gsm_probe(struct platform_device *pdev)
{
	/* default-on */
	const int default_blocked_state = 0;

	struct rfkill *rfkill;
	struct shuttle_pm_gsm_data *gsm_data;
	int ret;

	gsm_data = kzalloc(sizeof(*gsm_data), GFP_KERNEL);
	if (!gsm_data) {
		dev_err(&pdev->dev, "no memory for context\n");
		return -ENOMEM;
	}
	dev_set_drvdata(&pdev->dev, gsm_data);

	ret = shuttle_3g_gps_init();
	if (ret) {
		dev_err(&pdev->dev, "unable to init gps/gsm module\n");
		kfree(gsm_data);
		dev_set_drvdata(&pdev->dev, NULL);
		return ret;
	}

	/* register rfkill interface */
	rfkill = rfkill_alloc(pdev->name, &pdev->dev, RFKILL_TYPE_WWAN,
                            &shuttle_gsm_rfkill_ops, &pdev->dev);

	if (!rfkill) {
		dev_err(&pdev->dev, "Failed to allocate rfkill\n");
		shuttle_3g_gps_deinit();
		kfree(gsm_data);
		dev_set_drvdata(&pdev->dev, NULL);
		return -ENOMEM;
	}
	gsm_data->rfkill = rfkill;

	/* Set the default state */
	rfkill_init_sw_state(rfkill, default_blocked_state);
	__shuttle_pm_gsm_toggle_radio(&pdev->dev, !default_blocked_state);

	ret = rfkill_register(rfkill);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register rfkill\n");
		rfkill_destroy(rfkill);		
		shuttle_3g_gps_deinit();
		kfree(gsm_data);
		dev_set_drvdata(&pdev->dev, NULL);
		return ret;
	}

	dev_info(&pdev->dev, "GSM/UMTS RFKill driver loaded\n");
	
	return sysfs_create_group(&pdev->dev.kobj, &shuttle_gsm_attr_group);
}

static int shuttle_gsm_remove(struct platform_device *pdev)
{
	struct shuttle_pm_gsm_data *gsm_data = dev_get_drvdata(&pdev->dev);
	if (!gsm_data)
		return 0;
	
	sysfs_remove_group(&pdev->dev.kobj, &shuttle_gsm_attr_group);

	rfkill_unregister(gsm_data->rfkill);
	rfkill_destroy(gsm_data->rfkill);

	__shuttle_pm_gsm_toggle_radio(&pdev->dev, 0);

	shuttle_3g_gps_deinit();

	kfree(gsm_data);
	dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}
static struct platform_driver shuttle_gsm_driver = {
	.probe		= shuttle_gsm_probe,
	.remove		= shuttle_gsm_remove,
	.suspend	= shuttle_gsm_suspend,
	.resume		= shuttle_gsm_resume,
	.driver		= {
		.name		= "shuttle-pm-gsm",
	},
};

static int __devinit shuttle_gsm_init(void)
{
	return platform_driver_register(&shuttle_gsm_driver);
}

static void shuttle_gsm_exit(void)
{
	platform_driver_unregister(&shuttle_gsm_driver);
}

module_init(shuttle_gsm_init);
module_exit(shuttle_gsm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_DESCRIPTION("Shuttle GSM power management");
