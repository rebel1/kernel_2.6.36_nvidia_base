/*
 * virtual_adj.c
 *
 * Copyright 2011 Eduardo José Tagle <ejtagle@tutopia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This is useful for systems or drivers that ask for a controllable 
 * regulator, when no regulator is needed.
 */

#include <linux/kernel.h> 
#include <linux/version.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h> 
#include <linux/regulator/virtual_adj.h>


struct virtual_adj_data {
	struct regulator_desc 	desc;
	struct regulator_dev   *dev;
	int 					min_mV;
	int 					max_mV;
	int 					step_mV;
	int 					mV;			/* current microvolts */
	bool 					is_enabled;	/* if it is enabled or not */
};

static int virtual_adj_is_enabled(struct regulator_dev *dev)
{
	struct virtual_adj_data *data = rdev_get_drvdata(dev);
	return data->is_enabled;
}

static int virtual_adj_enable(struct regulator_dev *dev)
{
	struct virtual_adj_data *data = rdev_get_drvdata(dev);
	data->is_enabled = true;
	return 0;
}

static int virtual_adj_disable(struct regulator_dev *dev)
{
	struct virtual_adj_data *data = rdev_get_drvdata(dev);
	data->is_enabled = false;
	return 0;
}

static int virtual_adj_list_voltage(struct regulator_dev *dev,
				     unsigned selector)
{
	struct virtual_adj_data *data = rdev_get_drvdata(dev);
	
	/* Given the step, return the associated voltage value */
	return (data->min_mV + (selector * data->step_mV)) * 1000;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
static int virtual_adj_set_voltage(struct regulator_dev *dev,
				    int min_uV, int max_uV, unsigned *selector)
#else
static int virtual_adj_set_voltage(struct regulator_dev *dev,
				    int min_uV, int max_uV)
#endif
{
	struct virtual_adj_data *data = rdev_get_drvdata(dev);
	int sel;
	
	/* Calculate the step to use */
	sel = ((min_uV/1000) - data->min_mV) / data->step_mV;
	if (sel < 0)
		sel = 0;
	
	
	/* And the voltage to set */
	data->mV = data->min_mV + (sel * data->step_mV);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
	/* Store the step being used */
	*selector = sel;
#endif
	
	return 0;
}

static int virtual_adj_get_voltage(struct regulator_dev *dev)
{
	struct virtual_adj_data *data = rdev_get_drvdata(dev);

	return data->mV * 1000;
}


static struct regulator_ops virtual_adj_ops = {
	.is_enabled   = virtual_adj_is_enabled,
	.enable 	  = virtual_adj_enable,
	.disable 	  = virtual_adj_disable,
	.get_voltage  = virtual_adj_get_voltage,
	.set_voltage  = virtual_adj_set_voltage,
	.list_voltage = virtual_adj_list_voltage,
};

static int __devinit reg_virtual_adj_probe(struct platform_device *pdev)
{
	struct virtual_adj_voltage_config *config = pdev->dev.platform_data;
	struct virtual_adj_data *drvdata;
	int ret;

	drvdata = kzalloc(sizeof(struct virtual_adj_data), GFP_KERNEL);
	if (drvdata == NULL) {
		dev_err(&pdev->dev, "Failed to allocate device data\n");
		ret = -ENOMEM;
		goto err;
	}

	drvdata->desc.name = kstrdup(config->supply_name, GFP_KERNEL);
	if (drvdata->desc.name == NULL) {
		dev_err(&pdev->dev, "Failed to allocate supply name\n");
		ret = -ENOMEM;
		goto err;
	}
	drvdata->desc.id = config->id;
	drvdata->desc.type = REGULATOR_VOLTAGE;
	drvdata->desc.owner = THIS_MODULE;
	drvdata->desc.ops = &virtual_adj_ops;
	drvdata->desc.n_voltages = 1 + ((config->max_mV - config->min_mV) / config->step_mV);

	drvdata->min_mV = config->min_mV;
	drvdata->max_mV = config->max_mV;
	drvdata->step_mV = config->step_mV;
	drvdata->mV	= config->mV;
	drvdata->is_enabled = true;

	drvdata->dev = regulator_register(&drvdata->desc, &pdev->dev,
					  config->init_data, drvdata);
	if (IS_ERR(drvdata->dev)) {
		ret = PTR_ERR(drvdata->dev);
		dev_err(&pdev->dev, "Failed to register regulator: %d\n", ret);
		goto err_name;
	}

	platform_set_drvdata(pdev, drvdata);

	dev_dbg(&pdev->dev, "%s supplying %dmV, min: %dmV, max: %dmV, step: %dmV [%d]\n", drvdata->desc.name,
		drvdata->mV,drvdata->min_mV,drvdata->max_mV,drvdata->step_mV,drvdata->desc.n_voltages);

	return 0;

err_name:
	kfree(drvdata->desc.name);
err:
	kfree(drvdata);
	return ret;
}

static int __devexit reg_virtual_adj_remove(struct platform_device *pdev)
{
	struct virtual_adj_data *drvdata = platform_get_drvdata(pdev);

	regulator_unregister(drvdata->dev);
	kfree(drvdata->desc.name);
	kfree(drvdata);

	return 0;
}

static struct platform_driver regulator_virtual_adj_driver = {
	.probe		= reg_virtual_adj_probe,
	.remove		= __devexit_p(reg_virtual_adj_remove),
	.driver		= {
		.name		= "reg-virtual-adj-voltage",
		.owner		= THIS_MODULE,
	},
};

static int __init regulator_virtual_adj_init(void)
{
	return platform_driver_register(&regulator_virtual_adj_driver);
}
subsys_initcall(regulator_virtual_adj_init);

static void __exit regulator_virtual_adj_exit(void)
{
	platform_driver_unregister(&regulator_virtual_adj_driver);
}
module_exit(regulator_virtual_adj_exit);

MODULE_AUTHOR("Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_DESCRIPTION("Virtual adjustable voltage regulator");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:reg-virtual-adj-voltage");
