/*
 * Battery charger driver for tps6586x
 *
 * Copyright (C) 2011 Eduardo José Tagle <ejtagle@tutopia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/tps6586x.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <linux/mfd/tps6586x.h>
#include <linux/power/tps6586x_battery.h>

/* Charger Setup */
#define TPS6586x_CHG1      0x49
#define TPS6586x_CHG2      0x4A
#define TPS6586x_CHG3      0x4B 

/* Power Path Setup */
#define TPS6586x_PPATH2    0x4C 

/* ADC0 Engine Setup */
#define TPS6586x_ADCANLG   0x60

/* ADC0 Engine Data */
#define TPS6586x_ADC0_SET  0x61
#define TPS6586x_ADC0_WAIT 0x62
#define TPS6586x_ADC0_SUM2 0x94
#define TPS6586x_ADC0_SUM1 0x95
#define TPS6586x_ADC0_INT  0x9A  

/* System Status */
#define TPS6586x_STAT1     0xB9
#define TPS6586x_STAT2     0xBA
#define TPS6586x_STAT3     0xBB 


#define TPS6586X_POLLING_INTERVAL 30000  /* 30 seconds */

struct tps6586x_ec_ctx {

	struct power_supply battery_psy;
	struct power_supply ac_psy;
	struct power_supply usb_psy;
	
	struct device *master;
	struct power_supply_info *battery_info;
	struct dentry *debug_file;
	struct delayed_work work;
	struct workqueue_struct *work_queue; 
	
	struct work_struct isr_work;
	struct workqueue_struct *isr_wq; 

	struct mutex 		lock;				/* mutex protect */
	
	char*  supplied_to[1];
	
	// Battery and charger status  
	int ac_charger_is_on;
	int usb_charger_is_on;
	int battery_present;
	int battery_is_full;
	int battery_charging_current;
	int battery_voltage;
	int battery_temperature;
	
	int last_update;
	
	int low_batt_irq;						/* If there is a low battery IRQ */
	int use_irq;							/* if using a low battery irq */
	int in_s3_state_gpio;					/* Gpio used to indicate system is in S3 */
};

/* check CBC main batt presence */
static int tps6586x_battery_is_present(struct tps6586x_ec_ctx *charger,int* battispresent)
{
	int ret;
	char data;
	
	// Assume no battery by default
	*battispresent = 0;
	
   	ret = tps6586x_read(charger->master, TPS6586x_STAT1, &data);
	if (ret)
		return ret;
    
    // bit 0 show if battery exists or not
    data = data & 0x01;
    
    *battispresent = (data == 0 ? 0 : 1 ); 

    return 0;
}

/* check batt_ful status */
static int tps6586x_battery_is_full(struct tps6586x_ec_ctx *charger,int* battisfull)
{
	int ret;
	char data;

   	ret = tps6586x_read(charger->master, TPS6586x_STAT2, &data);
	if (ret)
		return ret;

    data = data & 0x2;
    *battisfull = (data == 0 ? 0 : 1 ); 

    return 0;
}

/* check main charger status */
static int tps6586x_ac_charger_is_present(struct tps6586x_ec_ctx *charger,int* ispresent)
{
	int ret;
	char data;

   	ret = tps6586x_read(charger->master, TPS6586x_STAT3, &data);
	if (ret)
		return ret;

    data = data & 0x8;
    *ispresent = (data == 0 ? 0 : 1 ); 

    return 0;
} 

/* check usb charger status */
static int tps6586x_usb_charger_is_present(struct tps6586x_ec_ctx *charger,int* ispresent)
{
	int ret;
	char data;

   	ret = tps6586x_read(charger->master, TPS6586x_STAT3, &data);
	if (ret)
		return ret;

    data = data & 0x4;
    *ispresent = (data == 0 ? 0 : 1 ); 

    return 0;
} 

/* Full scale mv for each channel maximum value */ 
static const unsigned short tps6586x_fullscale_mv[] = {
	2600,2600,2600,2600,
	2600,2600,4622,5547,
	5547,4622,2600,2600,
	2600,2600,2600,2600
};

/* get an ADC result from a specified channel */
static int tps6586x_get_adc_value(struct tps6586x_ec_ctx *charger,int channel, int* voltinmv)
{
	int ret;
    int timeout  = 0;
    uint8_t  dataH   = 0;
    uint8_t  dataL   = 0;

    *voltinmv = 0;    // Default is 0mV. 
	
    // Configuring the adc conversion cycle
    // ADC0_WAIT register(0x62)
    // Reset all ADC engines and return them to the idle state; ADC0_RESET: 1
	ret = tps6586x_write(charger->master, TPS6586x_ADC0_WAIT, 0x80);
	if (ret)
		return ret;

    // ADC0_SET register(0x61)
    // ADC0_EN: 0(Don't start conversion); Number of Readings: 16; CHANNEL: 
	ret = tps6586x_write(charger->master, TPS6586x_ADC0_SET, 0x10 | (channel & 0xF) );
	if (ret)
		return ret;
        
    // ADC0_WAIT register(0x62)
    // REF_EN: 0; AUTO_REF: 1; Wait time: 0.062ms
	ret = tps6586x_write(charger->master, TPS6586x_ADC0_WAIT, 0x21 );
	if (ret)
		return ret;
    	
    // Start conversion!!
	ret = tps6586x_write(charger->master, TPS6586x_ADC0_SET, 0x90 | (channel & 0xF) );
	if (ret)
		return ret;
	
    // Wait for conversion
	mdelay(26);

    // Make sure the conversion is completed - check for ADC error.
    while (1)
    {
		uint8_t dataS1 = 0;
		
        // Read ADC status register
		ret = tps6586x_read(charger->master, TPS6586x_ADC0_INT, &dataS1 );
		if (ret)
			return ret;
		
        // Conversion is done!
        if (dataS1 & 0x80)
            break;
        
        // ADC error!
        if (dataS1 & 0x40)
        {
            return -ENXIO;
        }

		udelay(70);
		
        timeout ++;
		if (timeout >= 10)
            return -ETIMEDOUT;
    }

    // Read the ADC conversion Average (SUM).
	ret = tps6586x_read(charger->master, TPS6586x_ADC0_SUM2, &dataH );
	if (ret)
		return ret;

	ret = tps6586x_read(charger->master, TPS6586x_ADC0_SUM1, &dataL );
	if (ret)
		return ret;

    // Get a result value in mV.
    *voltinmv = ((dataH << 8) | dataL) * tps6586x_fullscale_mv[channel & 0xF] / (1023 * 16);

    return 0;
} 

/* get charging current in mA */
static int tps6586x_get_charging_current(struct tps6586x_ec_ctx *charger,int* currentinma)
{
	int ispresent = 0;
	int battisfull = 0;
	int battispresent = 0;
	int res;
	int voltinmv;
	
	// Assume no charging 
	*currentinma = 0;
	
	// If either battery or charger not present, or battery full, charging current will be 0.
	res = tps6586x_ac_charger_is_present(charger,&ispresent);
	if (res)
		return res;
	if (!ispresent)
		return 0;

	res = tps6586x_battery_is_full(charger,&battisfull);
	if (res)
		return res;
	if (battisfull)
		return 0;
	
	res = tps6586x_battery_is_present(charger,&battispresent);
	if (res)
		return res;
	if (!battispresent)
		return 0;

	// Get the charging current
	res = tps6586x_get_adc_value(charger,3, &voltinmv);
	if (res)
		return res;
		
	// We are guessing here... We assume 2.5v->1A
	*currentinma = (voltinmv * 2) / 5;
	return 0;
}

/* get battery pack temperature in degrees * 10 */
static int tps6586x_get_battery_temperature(struct tps6586x_ec_ctx *charger,int* temp)
{
	int battispresent = 0;
	int res;
	int voltinmv;
	
	// Assume not present
	*temp = 0;
	
	// If battery not present, temp will be 0
	res = tps6586x_battery_is_present(charger,&battispresent);
	if (res)
		return res;
	if (!battispresent)
		return 0;

	// Enable the thermistor bias
	res = tps6586x_set_bits(charger->master, TPS6586x_CHG1, 0x10);
	if (res)
		return res;
		
	// Get the temperature
	res = tps6586x_get_adc_value(charger,4, &voltinmv);
	if (res)
		return res;

	// Disable the thermistor bias
	res = tps6586x_clr_bits(charger->master, TPS6586x_CHG1, 0x10);
	if (res)
		return res;

	// We are guessing here... We assume 2.5v->80g
	*temp = (voltinmv * 8000) / 25;
	return 0;
}

/* get battery pack voltage in mv */
static int tps6586x_get_battery_voltage(struct tps6586x_ec_ctx *charger,int* voltinmv)
{
	int battispresent = 0;
	int res;
	
	// Assume not present
	*voltinmv = 0;
	
	// If battery not present, voltage will be 0
	res = tps6586x_battery_is_present(charger,&battispresent);
	if (res)
		return res;
	if (!battispresent)
		return 0;

	// Get the battery voltage
	return tps6586x_get_adc_value(charger,9, voltinmv);
}

/* This fn is the only one that should be called by the driver to update
   the battery status */
static int tps6586x_update_status(struct tps6586x_ec_ctx *charger,bool force_update)
{
	int ret;
	
	/* Do not accept to update too often */
	if (!force_update && (charger->last_update - jiffies) >= 0)
		return 0;

	/* get exclusive access to the accelerometer */
	mutex_lock(&charger->lock);	
		
	ret = tps6586x_ac_charger_is_present(charger,&charger->ac_charger_is_on);

	if (!ret)
		ret = tps6586x_usb_charger_is_present(charger,&charger->usb_charger_is_on);
	if (!ret)
		ret = tps6586x_battery_is_present(charger,&charger->battery_present);
	if (!ret)
		ret = tps6586x_battery_is_full(charger,&charger->battery_is_full);
	if (!ret)
		ret = tps6586x_get_charging_current(charger,&charger->battery_charging_current);
	if (!ret)
		ret = tps6586x_get_battery_voltage(charger,&charger->battery_voltage);
	if (!ret)
		ret = tps6586x_get_battery_temperature(charger,&charger->battery_temperature);
	if (!ret)
		/* Allow next update 10 seconds from now */
		charger->last_update = jiffies + (TPS6586X_POLLING_INTERVAL/(1000*HZ*2));
		
	mutex_unlock(&charger->lock);		
	
	return ret;
}

static int tps6586x_ec_init(struct tps6586x_ec_ctx *charger)
{
	int ret;
	char data;
	
    // Configure CHARGER RAM registers
    // CHG1: Charge safety timer value is 4 Hrs; Charge current scaling facotr: 1.0; 
	
	// 76543210
	// cc       Maximum charge time
	//            00 = 4 hours
	//            01 = 5 hours
	//            10 = 6 hours
	//            11 = 8 hours
	//   d      Battery discharge switch (1=enabled)
	//    t     Thermistor bias control  (1=enabled)
	//     ss   Charge current scaling	 
	//            00 = 0.25
	//            01 = 0.50
	//            10 = 0.75
	//            11 = 1.00
	//       u  Charge termination state (0=allow termination)
	//        v Suspend charge (1=suspended)
    data = 0x0c; 
	ret = tps6586x_write(charger->master, TPS6586x_CHG1, data);
	if (ret)
		return ret;
			
    // CHG2: CHARGE SAFETY TIMER: ON; CHARGE VOLTAGE: 4.2V; CHARGER: ON;
	// 76543210
	// d        Dynamic timer function (1=on)
	//  p       Precharge voltage
	//            0 = 2.5v
	//            1 = 2.9v
	//   l      Enable charge LDO mode (1=On)
	//    s     Charge safety timer (1=On)
	//     vv   Charge voltage selection 
	//            00 =
	//       o  Charger OnOff 
	//        b Charger operation during boot (1=On)
	
    data = 0x1a;
	ret = tps6586x_write(charger->master, TPS6586x_CHG2, data);
	if (ret)
		return ret;

    // CHG3:
	// 76543210
	// c        Charger On/Off
	//  pp      System power path DPPM selection threshold
	//            00 = 3.5 v
	//            01 = 3.75v
	//            10 = 4.0 v
	//            11 = 4.25v
	//    s     Precharge timer scaling
	//            0 = 30min
	//            1 = 60min
	//     tt   Termination current factor
	//            00 = 0.04
	//            01 = 0.10
	//            10 = 0.15
	//            11 = 0.20
	//       pp Precharge current factor
	//            00 = 0.04
	//            01 = 0.1
	//            10 = 0.15
	//            11 = 0.2
    data = 0x0;
	ret = tps6586x_write(charger->master, TPS6586x_CHG3, data);
	if (ret)
		return ret;
	
    // RAM Control BITS: CHARGE VOLTAGE RANGE: 3.95 - 4.2; USB Input current limit: 500mA;
    // Auto mode enabled; AC input current limit: 2A
    data = 0x05;
	return tps6586x_write(charger->master, TPS6586x_PPATH2, data);
}


#ifdef CONFIG_DEBUG_FS
static int bat_debug_show(struct seq_file *s, void *data)
{
	struct tps6586x_ec_ctx *charger = s->private;
	
	int ret = tps6586x_update_status(charger,true);
	if (ret)
		return ret;

	seq_printf(s, "AC charger is %s\n", charger->ac_charger_is_on ? "on" : "off");
	seq_printf(s, "USB charger is %s\n", charger->usb_charger_is_on ? "on" : "off");
	seq_printf(s, "battery is %s\n", charger->battery_present ? "present" : "absent");
	seq_printf(s, "battery is %s\n", charger->battery_is_full ? "fully charged" : "not fully charged");
	seq_printf(s, "charging current is %d mA\n", charger->battery_charging_current);
	seq_printf(s, "battery voltage is %d mV\n", charger->battery_voltage);	
	seq_printf(s, "battery temperature is %d degrees C\n", charger->battery_temperature);
	
	return 0;
}

static int debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, bat_debug_show, inode->i_private);
}

static const struct file_operations bat_debug_fops = {
	.open		= debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct dentry *tps6586x_bat_create_debugfs(struct tps6586x_ec_ctx *charger)
{
	charger->debug_file = debugfs_create_file("charger", 0666, 0, charger,
						 &bat_debug_fops);
	return charger->debug_file;
}

static void tps6586x_bat_remove_debugfs(struct tps6586x_ec_ctx *charger)
{
	debugfs_remove(charger->debug_file);
}
#else
static inline struct dentry *tps6586x_bat_create_debugfs(struct tps6586x_ec_ctx *charger)
{
	return NULL;
}
static inline void tps6586x_bat_remove_debugfs(struct tps6586x_ec_ctx *charger)
{
}
#endif

static void tps6586x_battery_check_status(struct tps6586x_ec_ctx *charger,
				    union power_supply_propval *val)
{
	
	/* Assume not charging */
	val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;

	/* No battery means not charging */
	if (!charger->battery_present)
		return;

	/* Battery full means no charging */
	if (charger->battery_is_full) {
		val->intval = POWER_SUPPLY_STATUS_FULL;
		return;
	}
	
	val->intval = (charger->ac_charger_is_on || charger->usb_charger_is_on) 
		? POWER_SUPPLY_STATUS_CHARGING 
		: POWER_SUPPLY_STATUS_DISCHARGING;
}

static void tps6586x_battery_check_health(struct tps6586x_ec_ctx *charger,
				    union power_supply_propval *val)
{
	if (charger->battery_temperature > 600)
		val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
	else
	if (charger->battery_voltage > charger->battery_info->voltage_max_design)
		val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
}

static int tps6586x_battery_get_property(struct power_supply *battery_psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	int ret;
	struct tps6586x_ec_ctx *charger = container_of(battery_psy, struct tps6586x_ec_ctx, battery_psy);
	
	/* Update battery status */
	ret = tps6586x_update_status(charger,false);
	if (ret)
		return ret;
	
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->battery_present;
		break;
		
	case POWER_SUPPLY_PROP_STATUS:
		tps6586x_battery_check_status(charger, val);
		break;
		
	case POWER_SUPPLY_PROP_HEALTH:
		tps6586x_battery_check_health(charger, val);
		break;
		
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = charger->battery_info->technology;
		break;
		
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = charger->battery_info->voltage_max_design;
		break;
		
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = charger->battery_info->voltage_min_design;
		break;
		
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = charger->battery_voltage * 1000; /* in uVolts */
		break;
		
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_POWER_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = charger->battery_charging_current * 1000; /* in uAmps */
		break;
		
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = charger->battery_info->name;
		break;
		
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = charger->battery_temperature / 10; /* in degrees */
		break;
		
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = ((charger->battery_voltage * 1000) - charger->battery_info->voltage_min_design) / 
						((charger->battery_info->voltage_max_design - charger->battery_info->voltage_min_design) / 100);
		break;
		
	default:
		break;
	}

	return 0;
}

static enum power_supply_property tps6586x_battery_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CAPACITY,
};

static void tps6586x_battery_setup_psy(struct tps6586x_ec_ctx *charger)
{
	struct power_supply *battery_psy = &charger->battery_psy;
	struct power_supply_info *info = charger->battery_info;

	battery_psy->name = info->name;
	battery_psy->use_for_apm = info->use_for_apm;
	battery_psy->type = POWER_SUPPLY_TYPE_BATTERY;
	battery_psy->get_property = tps6586x_battery_get_property;

	battery_psy->properties = tps6586x_battery_props;
	battery_psy->num_properties = ARRAY_SIZE(tps6586x_battery_props);
};


static int tps6586x_ac_get_property(struct power_supply *ac_psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	int ret;
	struct tps6586x_ec_ctx *charger = container_of(ac_psy, struct tps6586x_ec_ctx, ac_psy);
		
	/* Update battery status */
	ret = tps6586x_update_status(charger,false);
	if (ret)
		return ret;
	
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->ac_charger_is_on;
		break;
	
	default:
		break;
	}

	return 0;
}

static enum power_supply_property tps6586x_ac_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static void tps6586x_ac_setup_psy(struct tps6586x_ec_ctx *charger)
{
	struct power_supply *ac_psy = &charger->ac_psy;

	ac_psy->name = "ac";
	ac_psy->use_for_apm = 1;
	ac_psy->supplied_to = charger->supplied_to;
	ac_psy->num_supplicants = 1;
	ac_psy->type = POWER_SUPPLY_TYPE_MAINS;
	ac_psy->get_property = tps6586x_ac_get_property;

	ac_psy->properties = tps6586x_ac_props;
	ac_psy->num_properties = ARRAY_SIZE(tps6586x_ac_props);
};

static int tps6586x_usb_get_property(struct power_supply *usb_psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	int ret;
	struct tps6586x_ec_ctx *charger = container_of(usb_psy, struct tps6586x_ec_ctx, usb_psy);
		
	/* Update battery status */
	ret = tps6586x_update_status(charger,false);
	if (ret)
		return ret;
	
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->usb_charger_is_on;
		break;
	
	default:
		break;
	}

	return 0;
}

static enum power_supply_property tps6586x_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static void tps6586x_usb_setup_psy(struct tps6586x_ec_ctx *charger)
{
	struct power_supply *usb_psy = &charger->usb_psy;

	usb_psy->name = "usb";
	usb_psy->use_for_apm = 1;
	usb_psy->supplied_to = charger->supplied_to;
	usb_psy->num_supplicants = 1;
	usb_psy->type = POWER_SUPPLY_TYPE_USB;
	usb_psy->get_property = tps6586x_usb_get_property;

	usb_psy->properties = tps6586x_usb_props;
	usb_psy->num_properties = ARRAY_SIZE(tps6586x_usb_props);
};

static void tps6586x_work_func(struct work_struct *work)
{
	struct tps6586x_ec_ctx *charger = container_of((struct delayed_work*)work,
		struct tps6586x_ec_ctx, work);

	/* Update power supply status */
	if (!tps6586x_update_status(charger,false)) {
		power_supply_changed(&charger->battery_psy);
		power_supply_changed(&charger->ac_psy);
		power_supply_changed(&charger->usb_psy);
	}
	
	queue_delayed_work(charger->work_queue, &charger->work,
				 msecs_to_jiffies(TPS6586X_POLLING_INTERVAL));
}


static void tps6586x_isr_work_func(struct work_struct *isr_work)
{
	struct tps6586x_ec_ctx *charger = container_of(isr_work,
		struct tps6586x_ec_ctx, isr_work);

	/* Update power supply status */
	if (!tps6586x_update_status(charger,true)) {
		power_supply_changed(&charger->battery_psy);
		power_supply_changed(&charger->ac_psy);
		power_supply_changed(&charger->usb_psy);
		
		/* If battery is below minimun, force a kernel shutdown */
		if (charger->battery_voltage < charger->battery_info->voltage_min_design) {
			pr_info("Battery critically low. Calling kernel_power_off()!\n");
			kernel_power_off();
		}
	}
		
	enable_irq(charger->low_batt_irq);
}

static irqreturn_t tps6586x_irq_handler(int irq, void *dev_id)
{
	struct tps6586x_ec_ctx *charger = dev_id;

	disable_irq_nosync(charger->low_batt_irq);
	
	queue_work(charger->isr_wq, &charger->isr_work);
	return IRQ_HANDLED;
}

static int tps6586x_ec_probe(struct platform_device *pdev)
{
	struct tps6586x_ec_ctx *charger;
	struct tps6586x_ec_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	if (pdata == NULL)
		return -EINVAL;

	charger = kzalloc(sizeof(*charger), GFP_KERNEL);
	if (charger == NULL)
		return -ENOMEM;

	charger->master = pdev->dev.parent;
	charger->battery_info = &pdata->battery_info;
	charger->low_batt_irq = pdata->low_batt_irq;
	charger->in_s3_state_gpio = pdata->in_s3_state_gpio;

	/* Init the protection mutex */
	mutex_init(&charger->lock); 
	
	ret = tps6586x_ec_init(charger);
	if (ret)
		goto err_charger_init;

	/* Fill in the supplied to list */
	charger->supplied_to[0] = (char*) pdata->battery_info.name;
	
	tps6586x_battery_setup_psy(charger);
	tps6586x_ac_setup_psy(charger);
	tps6586x_usb_setup_psy(charger); 
	
	ret = power_supply_register(&pdev->dev, &charger->battery_psy);
	if (ret)
		goto err_ps_register;

	ret = power_supply_register(&pdev->dev, &charger->ac_psy);
	if (ret)
		goto err_ps_register2;

	ret = power_supply_register(&pdev->dev, &charger->usb_psy);
	if (ret)
		goto err_ps_register3;

	charger->debug_file = tps6586x_bat_create_debugfs(charger);
	platform_set_drvdata(pdev, charger);
	
	charger->work_queue = create_singlethread_workqueue("tps6586x_wq");
	if (!charger->work_queue) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "could not create workqueue\n");
		goto err_ps_register4;
	}
	INIT_DELAYED_WORK(&charger->work, tps6586x_work_func);
	
	queue_delayed_work(charger->work_queue, &charger->work,
		 msecs_to_jiffies(TPS6586X_POLLING_INTERVAL));
	
	charger->isr_wq = create_singlethread_workqueue("tps6586x_isr_wq");
	if (!charger->isr_wq) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "could not create isr workqueue\n");
		goto err_ps_register5;
	}
	
	INIT_WORK(&charger->isr_work, tps6586x_isr_work_func);

	if (charger->low_batt_irq) {
		ret = request_irq(charger->low_batt_irq, tps6586x_irq_handler,
			IRQF_TRIGGER_FALLING, "tps6586x_lowbatt_isr", charger);
		if (!ret) {
			charger->use_irq = 1;
		} else {
			dev_err(&pdev->dev, "request_irq failed\n");
		}
	}
	
	/* Finally, get the S3 gpio, if specified */
	if (charger->in_s3_state_gpio) {
		gpio_request(charger->in_s3_state_gpio,"S3 flag");
		gpio_direction_output(charger->in_s3_state_gpio,0);
	}
	
	return 0;
	
err_ps_register5:	
	cancel_delayed_work_sync(&charger->work);
	destroy_workqueue(charger->work_queue);
	
err_ps_register4:
	power_supply_unregister(&charger->usb_psy);
	
err_ps_register3:	
	power_supply_unregister(&charger->ac_psy);
	
err_ps_register2:	
	power_supply_unregister(&charger->battery_psy);
	
err_ps_register:
err_charger_init:
	mutex_destroy(&charger->lock); 
	kfree(charger);

	return ret;
}

static int tps6586x_ec_remove(struct platform_device *dev)
{
	struct tps6586x_ec_ctx *charger = platform_get_drvdata(dev);
	
	if (charger->use_irq)
		free_irq(charger->low_batt_irq,charger);
	if (charger->in_s3_state_gpio)
		gpio_free(charger->in_s3_state_gpio);
	destroy_workqueue(charger->isr_wq);
	cancel_delayed_work_sync(&charger->work);
	destroy_workqueue(charger->work_queue);
	tps6586x_bat_remove_debugfs(charger);
	power_supply_unregister(&charger->battery_psy);
	power_supply_unregister(&charger->ac_psy);
	power_supply_unregister(&charger->usb_psy);
	mutex_destroy(&charger->lock); 
	kfree(charger);

	return 0;
}

static int tps6586x_ec_resume(struct platform_device *dev)
{
	struct tps6586x_ec_ctx *charger = platform_get_drvdata(dev);
	if (charger->in_s3_state_gpio) 
		gpio_set_value(charger->in_s3_state_gpio,0);
	queue_delayed_work(charger->work_queue, &charger->work,
		msecs_to_jiffies(TPS6586X_POLLING_INTERVAL));
	return 0;
}

static int tps6586x_ec_suspend(struct platform_device *dev, pm_message_t mesg)
{
	struct tps6586x_ec_ctx *charger = platform_get_drvdata(dev);
	cancel_delayed_work_sync(&charger->work);
	if (charger->in_s3_state_gpio) 
		gpio_set_value(charger->in_s3_state_gpio,1);
	return 0;
}


static struct platform_driver tps6586x_driver = {
	.driver	= {
		.name	= "tps6586x-ec",
		.owner	= THIS_MODULE,
	},
	.probe = tps6586x_ec_probe,
	.remove = tps6586x_ec_remove,
	.suspend = tps6586x_ec_suspend,
	.resume = tps6586x_ec_resume,
};

static int tps6586x_init(void)
{
	return platform_driver_register(&tps6586x_driver);
}

static void tps6586x_exit(void)
{
	platform_driver_unregister(&tps6586x_driver);
}

module_init(tps6586x_init);
module_exit(tps6586x_exit);

MODULE_DESCRIPTION("TPS6586x battery charger driver");
MODULE_AUTHOR("Eduardo José Tagle");
MODULE_LICENSE("GPL");
