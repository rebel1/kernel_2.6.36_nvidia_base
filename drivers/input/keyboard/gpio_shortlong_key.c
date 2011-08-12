/*
 * gpio_shortlong_key.c
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

/* Special driver to handle two keycodes with one GPIO using time as differenciator */


#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h> 
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h> 
#include <linux/gpio.h>

#include <linux/timer.h>
#include <linux/input.h>
#include <linux/delay.h>

#include <linux/interrupt.h>

#include <linux/gpio_shortlong_key.h>

#define SHUTTLE_KBD_TICK (HZ/100*2)  /* 20ms */

struct gpio_shortlong_key_ctx {
	
	struct device *		master;
	struct input_dev *	button_dev;	
	
	int 				gpio;					/* gpio to use to detect long presses */
	int					active_low;				/* if key is active low */
	int					short_press_keycode;	/* short press key code */
	int					long_press_keycode;		/* long press key code */
	int					debounce_time;			/* time to recognize at least a short press in ticks*/
	int					long_press_time;		/* time to recognize a long press in ticks */
	
	struct timer_list 	timer;  
	int 				running;
	int 				range;
	int 				sent_as_long;
}; 

static void gpio_shortlong_key_emit_keycode(struct gpio_shortlong_key_ctx *ctx, int key_code)
{
	input_report_key(ctx->button_dev, key_code, 1) ;
	input_sync(ctx->button_dev) ;
	input_report_key(ctx->button_dev, key_code, 0) ;
	input_sync(ctx->button_dev) ;
}

static void gpio_shortlong_key_timerhandler(unsigned long data)
{
	struct gpio_shortlong_key_ctx *ctx = (struct gpio_shortlong_key_ctx *) data;
    if (ctx->active_low != gpio_get_value(ctx->gpio))
    {
        ctx->range += SHUTTLE_KBD_TICK;
		
        // long press
        if ((ctx->range > ctx->long_press_time) && (!ctx->sent_as_long) ) 
        {
			/* send long command */
            gpio_shortlong_key_emit_keycode(ctx,ctx->long_press_keycode) ;
            ctx->sent_as_long = true; 
        }
        mod_timer(&ctx->timer, jiffies + SHUTTLE_KBD_TICK) ;
    }
}

static irqreturn_t gpio_shortlong_key_isr(int irq, void *data)
{
	struct gpio_shortlong_key_ctx *ctx = data;
	
    if (ctx->active_low != gpio_get_value(ctx->gpio))
    {
		if (!ctx->running)
		{
			ctx->running = true;
			mod_timer(&ctx->timer, jiffies + SHUTTLE_KBD_TICK) ;
		}
    }
    else
    {
		mod_timer(&ctx->timer, jiffies - 1) ;
		ctx->running = false;

        if ((ctx->range > ctx->debounce_time) && ( ctx->range < ctx->long_press_time))
        { 
			/* send short press key code */
	        gpio_shortlong_key_emit_keycode(ctx,ctx->short_press_keycode) ;
        }

        ctx->range = 0;
		ctx->sent_as_long = 0;
    }

    return IRQ_HANDLED;
}


static int gpio_shortlong_key_probe(struct platform_device *pdev)
{
	struct gpio_shortlong_key_ctx *ctx;
	struct gpio_shortlong_key_platform_data *pdata = pdev->dev.platform_data;

	if (pdata == NULL) {
		dev_err(&pdev->dev,"no platform data\n");
		return -EINVAL;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL) {
		dev_err(&pdev->dev, "can't allocate context\n");
		return -ENOMEM;
	}

	ctx->master = pdev->dev.parent;
	ctx->gpio = pdata->gpio;								/* gpio to use to detect long presses */
	ctx->active_low = pdata->active_low;					/* if key is active low */
	ctx->short_press_keycode = pdata->short_press_keycode;	/* short press key code */
	ctx->long_press_keycode = pdata->long_press_keycode;	/* long press key code */
	ctx->debounce_time = (pdata->debounce_time < 20) ? 1 : (pdata->debounce_time / 20);			/* time to recognize at least a short press in ticks*/
	ctx->long_press_time = (pdata->long_press_time < 20) ? 1 : (pdata->long_press_time / 20);	/* time to recognize a long press in ticks */
	
	ctx->button_dev = input_allocate_device();
	if (!ctx->button_dev) {
		kfree(ctx);
		dev_err(&pdev->dev, "can't allocate input device\n");
		return -ENOMEM;
	}

	set_bit(EV_KEY,ctx->button_dev->evbit) ;
	set_bit(ctx->short_press_keycode,ctx->button_dev->keybit) ;
	set_bit(ctx->long_press_keycode,ctx->button_dev->keybit) ;

	input_set_drvdata(ctx->button_dev, ctx);
	ctx->button_dev->name = pdev->name;
	ctx->button_dev->phys = "gpio-shortlong-keys/input0";
	ctx->button_dev->dev.parent = &pdev->dev;
	ctx->button_dev->id.bustype = BUS_HOST;
	ctx->button_dev->id.vendor = 0x0001;
	ctx->button_dev->id.product = 0x0001;
	ctx->button_dev->id.version = 0x0100;
	
	if (input_register_device(ctx->button_dev)) {
		dev_err(&pdev->dev, "Failed to register input device\n");
		input_free_device(ctx->button_dev);
		kfree(ctx);
		return -ENOMEM;
	}

	gpio_request(ctx->gpio, "short-long-press key") ;
	gpio_direction_input(ctx->gpio) ;

    init_timer(&ctx->timer) ;
    ctx->timer.function = &gpio_shortlong_key_timerhandler;
    ctx->timer.expires = jiffies - 1;
	ctx->timer.data = (unsigned long) ctx;
    add_timer(&ctx->timer) ;
    
    if (request_irq( gpio_to_irq(ctx->gpio),
                          gpio_shortlong_key_isr,
                          IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                          "short-long-press key",
                          ctx))
    {
		dev_err(&pdev->dev, "Failed to get interrupt\n");
		gpio_free(ctx->gpio);
		input_unregister_device(ctx->button_dev) ;
		free_irq(gpio_to_irq(ctx->gpio), NULL);
		kfree(ctx);
		return -ENXIO;
    }

	platform_set_drvdata(pdev, ctx);
	
	
	dev_info(&pdev->dev, "Started\n");
	
	return 0;
}

static int gpio_shortlong_key_remove(struct platform_device *dev)
{
	struct gpio_shortlong_key_ctx *ctx = platform_get_drvdata(dev);
	input_unregister_device(ctx->button_dev) ;
	free_irq(gpio_to_irq(ctx->gpio), ctx);
	kfree(ctx);

	return 0;
}

static struct platform_driver gpio_shortlong_key_driver = {
	.driver	= {
		.name	= "gpio-shortlong-kbd",
		.owner	= THIS_MODULE,
	},
	.probe  = gpio_shortlong_key_probe,
	.remove = gpio_shortlong_key_remove,
};

static int gpio_shortlong_key_init(void)
{
	return platform_driver_register(&gpio_shortlong_key_driver);
}

static void gpio_shortlong_key_exit(void)
{
	platform_driver_unregister(&gpio_shortlong_key_driver);
}

module_init(gpio_shortlong_key_init);
module_exit(gpio_shortlong_key_exit);

MODULE_DESCRIPTION("gpio shortlong keyboard driver");
MODULE_AUTHOR("Eduardo José Tagle");
MODULE_LICENSE("GPL"); 

