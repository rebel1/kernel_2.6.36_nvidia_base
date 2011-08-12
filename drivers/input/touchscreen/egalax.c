/* drivers/input/touchscreen/egalax.c
 *
 * Copyright (C) 2007 Google, Inc.
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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <asm/uaccess.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/earlysuspend.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/input/egalax.h>

#define MAX_I2C_LEN		10
#define MAX_SUPPORT_POINT	4
#define REPORTID_VENDOR		0x03
#define REPORTID_MTOUCH		0x04

struct ts_point {
	int x,y;		/* coordinates of the touch */
	int p;			/* Touch pressure */
	int valid;		/* if point is valid or not */
};

struct eGalax_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	char phys[32];
	
	struct work_struct  work;
#ifdef CONFIG_HAS_EARLYSUSPEND	
	struct early_suspend early_suspend;
#endif

	struct workqueue_struct *eGalax_wq;	
	void (*disable_tp)(void);	/* function to disable the touchpad */
	void (*enable_tp)(void);	/* function to enable the touchpad */
	
	struct ts_point pt[MAX_SUPPORT_POINT]; 		/* The list of points used right now */
	int	   LastUpdateID;						/* last updated point */
	   
};

static int eGalax_init(struct eGalax_ts_data *ts)
{
	int ret,gpio;
	unsigned char buf[MAX_I2C_LEN]={0};

	// First, wake up the device
	gpio = irq_to_gpio(ts->client->irq);
	gpio_free(gpio);
	if( (ret=gpio_request(gpio, "Touch Wakeup GPIO"))!=0 )
	{
		return -1;
	}
	
	// Push the GPIO down and release it
	gpio_direction_output(gpio, 0);
	mdelay(10);
	mdelay(10);
	gpio_direction_input(gpio);

	// Clean data
	ret = 10000;
	while( !gpio_get_value(gpio) && --ret != 0)
		if (i2c_master_recv(ts->client, buf, MAX_I2C_LEN) != MAX_I2C_LEN)
			break;
	if (ret == 0) {
		dev_err(&ts->client->dev,"The egalax_i2c device does not exist\n");
		return -1;
	}
		
	// Try to get acknowledge from device
	memset(buf,0,sizeof(buf));
	ret = i2c_master_send(ts->client, buf, MAX_I2C_LEN);
	if (ret != MAX_I2C_LEN)
	{
		dev_err(&ts->client->dev,"The egalax_i2c device does not exist\n");
		return -1;
	}
	
	// Wait until device is idle
	ret = 100000;
	while( !gpio_get_value(gpio) && --ret != 0)
		if (i2c_master_recv(ts->client, buf, MAX_I2C_LEN) != MAX_I2C_LEN)
			break;

	// Device present
	return ret != 0 ? 0 : -1;
}

static void eGalax_readpoints(struct eGalax_ts_data *ts)
{
	unsigned char buf[MAX_I2C_LEN];
	int count, loop = 3;
	
	do {
		count = i2c_master_recv(ts->client, buf, MAX_I2C_LEN);
	} while ( count != MAX_I2C_LEN && --loop);

	if( count != MAX_I2C_LEN || 
		(buf[0] != REPORTID_VENDOR && buf[0] != REPORTID_MTOUCH) )
	{
		dev_err(&ts->client->dev,"I2C read error data with Len=%d header=%d\n",count, buf[0]);
		return;
	}

	dev_info(&ts->client->dev,"read data with Len=%d\n", count);
	if(buf[0] == REPORTID_VENDOR) {
		dev_info(&ts->client->dev,"got command packet\n");
	} else

	// If dealing with a multitouch event...
	if(buf[0] == REPORTID_MTOUCH )
	{
		int i ,cnt = 0;
		int X=0, Y=0, ContactID=0, Status=0;

		Status 	  = buf[1]&0x01;
		ContactID = (buf[1]&0x7C)>>2;
		X = ((buf[3]<<8) | buf[2])>>4;
		Y = ((buf[5]<<8) | buf[4])>>4;
	
		ts->pt[ContactID].p = Status;
		ts->pt[ContactID].x = X;
		ts->pt[ContactID].y = Y;

		dev_info(&ts->client->dev,"Get Point[%d] Update: Status=%d X=%d Y=%d\n", ContactID, Status, X, Y);

		// Send point report to Android if we started from a new point scan, or any point was released
		if(!Status || ContactID <= ts->LastUpdateID)
		{
			for(i=0; i<MAX_SUPPORT_POINT;i++)
			{
				if(ts->pt[i].p != 0)
				{
					input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
					input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, ts->pt[i].p);
					input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, ts->pt[i].p);
					input_report_abs(ts->input_dev, ABS_MT_POSITION_X , ts->pt[i].x);
					input_report_abs(ts->input_dev, ABS_MT_POSITION_Y , ts->pt[i].y);
					input_mt_sync(ts->input_dev);\
					cnt++;
				}
			}

			// If no points returned... Inform android of that
			if (!cnt) 
			{
				input_report_key(ts->input_dev, BTN_TOUCH, 0);
				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
				input_mt_sync(ts->input_dev);
			}	

			input_sync(ts->input_dev);	
		}

		// Remember the last processed point
		ts->LastUpdateID = ContactID;
	}
}

static void eGalax_ts_work_func(struct work_struct *work)
{
	struct eGalax_ts_data *ts = container_of(work, struct eGalax_ts_data, work);

	// Process all the available data
	int gpio = irq_to_gpio(ts->client->irq);
	while( !gpio_get_value(gpio) ) {
		eGalax_readpoints(ts);
		schedule();
	}
	
	// Reenable interrupts
	enable_irq(ts->client->irq);
}

static irqreturn_t eGalax_ts_irq_handler(int irq, void *dev_id)
{
	struct eGalax_ts_data *ts = dev_id;

	disable_irq_nosync(ts->client->irq);
	queue_work(ts->eGalax_wq, &ts->work);
	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void eGalax_ts_early_suspend(struct early_suspend *h);
static void eGalax_ts_late_resume(struct early_suspend *h);
#endif

static int eGalax_ts_probe(
	struct i2c_client *client, const struct i2c_device_id *id)
{
	struct eGalax_ts_data *ts;
	struct egalax_platform_data *pdata = client->dev.platform_data;
	int ret = 0;

	dev_info(&client->dev,"eGalax touchscreen Driver\n");
	
	if (!client->irq) {
		dev_err(&client->dev,"no IRQ specified for device\n");
		return -EIO;
	}
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,"need I2C_FUNC_I2C\n");
		return -EIO;
	}

	ts = kzalloc(sizeof(struct eGalax_ts_data), GFP_KERNEL);
	if (!ts) {
		dev_err(&client->dev,"failed memory allocation\n");
		return -ENOMEM;
	}
	
	i2c_set_clientdata(client, ts);
	ts->client = client;
	
	// Fill in default values
	ts->disable_tp = pdata->disable_tp;	/* function to disable the touchpad */
	ts->enable_tp = pdata->enable_tp;	/* function to enable the touchpad */

	// Enable the touchpad
	if (ts->enable_tp)
		ts->enable_tp();

	// Try to init the capacitive sensor
	if(eGalax_init(ts)) {
		dev_err(&client->dev,"not detected or in firmware upgrade mode.\n");
		ret = -ENODEV;
		goto error_not_found;
	}

	// Prepare the input context
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_err(&client->dev,"failed to allocate input device\n");
		goto err_input_alloc;
	}
	
	// Fill in information
	input_set_drvdata(ts->input_dev, ts);
	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", dev_name(&client->dev));
	ts->input_dev->name = "egalax";
	ts->input_dev->phys = ts->phys;
	ts->input_dev->dev.parent = &client->dev;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0x0EEF;
	ts->input_dev->id.product = 0x0020;
	ts->input_dev->id.version = 0x0100;
	
	// And capabilities
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);

	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 15, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, 2047, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, 2047, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 3, 0, 0);

	input_set_abs_params(ts->input_dev, ABS_X, 0, 2047, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, 2047, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 15, 0, 0);

	ret = input_register_device(ts->input_dev);
	if (ret) {
		ret = -ENOMEM;
		dev_err(&client->dev,"unable to register %s input device\n", ts->input_dev->name);
		goto err_could_not_register;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING - 1;
	ts->early_suspend.suspend = eGalax_ts_early_suspend;
	ts->early_suspend.resume = eGalax_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
	
	ts->eGalax_wq = create_singlethread_workqueue("eGalax_wq");
	if (!ts->eGalax_wq) {
		ret = -ENOMEM;
		dev_err(&client->dev,"unable to allocate workqueue\n");	
		goto err_alloc_wq;
	}
	
	INIT_WORK(&ts->work, eGalax_ts_work_func);

	ret = request_irq(client->irq, eGalax_ts_irq_handler, IRQF_TRIGGER_FALLING, client->name, ts);
	if (ret) {
		ret = -ENOMEM;
		dev_err(&client->dev, "request_irq failed\n");
		goto err_alloc_irq;
	}

	dev_info(&client->dev,"eGalax touchscreen driver loaded\n");
	return 0;

err_alloc_irq:	
	destroy_workqueue(ts->eGalax_wq);
	
err_alloc_wq:
err_could_not_register:
	input_free_device(ts->input_dev);
	
err_input_alloc:
error_not_found:

	// Disable the touchpad
	if (ts && ts->disable_tp)
		ts->disable_tp();

	i2c_set_clientdata(client, NULL);
	kfree(ts);

	return ret;

}

static int eGalax_ts_remove(struct i2c_client *client)
{
	struct eGalax_ts_data *ts = i2c_get_clientdata(client);

	free_irq(client->irq,ts);
	destroy_workqueue(ts->eGalax_wq);
	input_free_device(ts->input_dev);
	
	// Disable the touchpad
	if (ts->disable_tp)
		ts->disable_tp();
	
	i2c_set_clientdata(client, NULL);
	kfree(ts);
	
	return 0;
}

static int eGalax_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	// Power down command
	unsigned char cmdbuf[MAX_I2C_LEN]={0x03, 0x05, 0x0A, 0x03, 0x36, 0x3F, 0x02, 0, 0, 0};
	
	int ret;
	struct eGalax_ts_data *ts = i2c_get_clientdata(client);
	disable_irq(client->irq);
		
	ret = cancel_work_sync(&ts->work);
	if (ret) /* if work was pending disable-count is now 2 */
		enable_irq(client->irq);
		
	// Power down the touchscreen
	i2c_master_send(ts->client, cmdbuf, MAX_I2C_LEN);
	
	// Disable the touchpad
	if (ts->disable_tp)
		ts->disable_tp();
	
	return 0;
}


static int eGalax_ts_resume(struct i2c_client *client)
{
	unsigned char buf[MAX_I2C_LEN]={0};
	int ret=0, i, gpio;	
	struct eGalax_ts_data *ts = i2c_get_clientdata(client);

	// Enable the touchpad
	if (ts->enable_tp)
		ts->enable_tp();

	// Power up the touchscreen
	gpio = irq_to_gpio(ts->client->irq);
	gpio_free(gpio);
	if( (ret = gpio_request(gpio, "Touch Wakeup GPIO"))!=0 )
	{
		dev_err(&client->dev,"Failed to request GPIO for Touch Wakeup GPIO. Err:%d\n", ret);
	}
	else
	{
		gpio_direction_output(gpio, 0);
		for(i=0; i<100; i++);
		gpio_direction_input(gpio);
		dev_info(&client->dev, "INT wakeup touch controller done\n");
	}

	ret = 100000;
	while( !gpio_get_value(gpio) && --ret != 0)
		if (i2c_master_recv(ts->client, buf, MAX_I2C_LEN) != MAX_I2C_LEN)
			break;

	enable_irq(client->irq);
	
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void eGalax_ts_early_suspend(struct early_suspend *h)
{
	struct eGalax_ts_data *ts;
	ts = container_of(h, struct eGalax_ts_data, early_suspend);
	eGalax_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void eGalax_ts_late_resume(struct early_suspend *h)
{
	struct eGalax_ts_data *ts;
	ts = container_of(h, struct eGalax_ts_data, early_suspend);
	eGalax_ts_resume(ts->client);
}
#endif

static const struct i2c_device_id eGalax_ts_id[] = {
	{ "egalax", 0 },
	{}
};

static struct i2c_driver eGalax_ts_driver = {
	.driver = {
		.name	= "egalax",
		.owner  = THIS_MODULE,
	},
	.probe		= eGalax_ts_probe,
	.remove		= eGalax_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND	
	.suspend	= eGalax_ts_suspend,
	.resume		= eGalax_ts_resume,
#endif
	.id_table	= eGalax_ts_id,
};

static int __devinit eGalax_ts_init(void)
{
	pr_info("eGalax touchscreen driver\n");
	return i2c_add_driver(&eGalax_ts_driver);
}

static void __exit eGalax_ts_exit(void)
{
	i2c_del_driver(&eGalax_ts_driver);
}

module_init(eGalax_ts_init);
module_exit(eGalax_ts_exit);

MODULE_AUTHOR("Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_DESCRIPTION("eGalax Touchscreen Driver");
MODULE_LICENSE("GPL");
