/*
 * GPS low power control via GPIO
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

#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include "board-shuttle.h"
#include "gpio-names.h"


struct shuttle_pm_gps_data {
#ifdef CONFIG_PM
	int pre_resume_state;
	int keep_on_in_suspend;
#endif
	int powered_up;
};

/* Power control */
static void __shuttle_pm_gps_toggle_radio(struct device *dev, unsigned int on)
{
	struct shuttle_pm_gps_data *gps_data = dev_get_drvdata(dev);

	/* Avoid turning it on or off if already in that state */
	if (gps_data->powered_up == on)
		return;
	
	if (on) {
	
		/* 3G/GPS power on sequence */
		shuttle_3g_gps_poweron();

	} else {
	
		shuttle_3g_gps_poweroff();
				
	}
	
	/* store new state */
	gps_data->powered_up = on;
}


static ssize_t shuttle_gps_read(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct shuttle_pm_gps_data *gps_data = dev_get_drvdata(dev);
	int ret = 0;

	if (!strcmp(attr->attr.name, "power_on") ||
	    !strcmp(attr->attr.name, "pwron")) {
		ret = gps_data->powered_up;
	}
#ifdef CONFIG_PM
	else if (!strcmp(attr->attr.name, "keep_on_in_suspend")) {
		ret = gps_data->keep_on_in_suspend;
	}
#endif

	return strlcpy(buf, (!ret) ? "0\n" : "1\n", 3);
}

static ssize_t shuttle_gps_write(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct shuttle_pm_gps_data *gps_data = dev_get_drvdata(dev);
	unsigned long on = simple_strtoul(buf, NULL, 10);

	if (!strcmp(attr->attr.name, "power_on") ||
	    !strcmp(attr->attr.name, "pwron")) {
		__shuttle_pm_gps_toggle_radio(dev,on);
	}
#ifdef CONFIG_PM
	else if (!strcmp(attr->attr.name, "keep_on_in_suspend")) {
		gps_data->keep_on_in_suspend = on;
	}
#endif

	return count;
}

static DEVICE_ATTR(power_on, 0666, shuttle_gps_read, shuttle_gps_write);
static DEVICE_ATTR(pwron, 0666, shuttle_gps_read, shuttle_gps_write);
#ifdef CONFIG_PM
static DEVICE_ATTR(keep_on_in_suspend, 0666, shuttle_gps_read, shuttle_gps_write);
#endif


#ifdef CONFIG_PM
static int shuttle_pm_gps_suspend(struct platform_device *pdev,
				pm_message_t state)
{
	struct shuttle_pm_gps_data *gps_data = dev_get_drvdata(&pdev->dev);
	
	gps_data->pre_resume_state = gps_data->powered_up;
	if (!gps_data->keep_on_in_suspend)
		__shuttle_pm_gps_toggle_radio(&pdev->dev,0);
	else
		dev_warn(&pdev->dev, "keeping gps ON during suspend\n");
	return 0;
}

static int shuttle_pm_gps_resume(struct platform_device *pdev)
{
	struct shuttle_pm_gps_data *gps_data = dev_get_drvdata(&pdev->dev);
	__shuttle_pm_gps_toggle_radio(&pdev->dev,gps_data->pre_resume_state);
	return 0;
}

#else
#define shuttle_pm_gps_suspend	NULL
#define shuttle_pm_gps_resume	NULL
#endif


static struct attribute *shuttle_gps_sysfs_entries[] = {
	&dev_attr_power_on.attr,
	&dev_attr_pwron.attr,
#ifdef CONFIG_PM
	&dev_attr_keep_on_in_suspend.attr,
#endif
	NULL
};

static struct attribute_group shuttle_gps_attr_group = {
	.name	= NULL,
	.attrs	= shuttle_gps_sysfs_entries,
};

static int __init shuttle_pm_gps_probe(struct platform_device *pdev)
{
	/* start with gps enabled */
	int default_state = 1;
	int ret;
	
	struct shuttle_pm_gps_data *gps_data;
	
	gps_data = kzalloc(sizeof(*gps_data), GFP_KERNEL);
	if (!gps_data) {
		dev_err(&pdev->dev, "no memory for context\n");
		return -ENOMEM;
	}
	dev_set_drvdata(&pdev->dev, gps_data);

	ret = shuttle_3g_gps_init();
	if (ret) {
		dev_err(&pdev->dev, "unable to init gps/gsm module\n");
		kfree(gps_data);
		dev_set_drvdata(&pdev->dev, NULL);
		return ret;
	}

	/* Set the default state */
	__shuttle_pm_gps_toggle_radio(&pdev->dev, default_state);
	
	dev_info(&pdev->dev, "GPS power management driver loaded\n");
	
	return sysfs_create_group(&pdev->dev.kobj,
				  &shuttle_gps_attr_group);
}

static int shuttle_pm_gps_remove(struct platform_device *pdev)
{
	struct shuttle_pm_gps_data *gps_data = dev_get_drvdata(&pdev->dev);
	if (!gps_data)
		return 0;
	
	sysfs_remove_group(&pdev->dev.kobj, &shuttle_gps_attr_group);
	
	__shuttle_pm_gps_toggle_radio(&pdev->dev, 0);

	shuttle_3g_gps_deinit();

	kfree(gps_data);
	dev_set_drvdata(&pdev->dev, NULL);
	
	return 0;
}

static struct platform_driver shuttle_pm_gps_driver = {
	.probe		= shuttle_pm_gps_probe,
	.remove		= shuttle_pm_gps_remove,
	.suspend	= shuttle_pm_gps_suspend,
	.resume		= shuttle_pm_gps_resume,
	.driver		= {
		.name		= "shuttle-pm-gps",
	},
};

static int __devinit shuttle_pm_gps_init(void)
{
	return platform_driver_register(&shuttle_pm_gps_driver);
}

static void shuttle_pm_gps_exit(void)
{
	platform_driver_unregister(&shuttle_pm_gps_driver);
}

module_init(shuttle_pm_gps_init);
module_exit(shuttle_pm_gps_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_DESCRIPTION("Shuttle 3G / GPS power management");
