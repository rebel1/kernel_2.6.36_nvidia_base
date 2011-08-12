/*
 * power supply driver for a NVIDIA compliant embedded controller
 *
 * Copyright (C) 2011 Marc Dietrich <marvin24@gmx.de>
 *
 * Authors:  Ilya Petrov <ilya.muromec@gmail.com>
 *           Marc Dietrich <marvin24@gmx.de>
 *           Eduardo José Tagle <ejtagle@tutopia.com>  
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/module.h>
#include <linux/power/nvec_power.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/mfd/nvec.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/reboot.h>
#include <linux/jiffies.h>

#include <asm/irq.h>
 
#define NVEC_POWER_POLLING_INTERVAL 10000

struct nvec_power {
	struct notifier_block 	 notifier;
	
	struct device *			 master;
	struct device *			 dev;
		
	struct dentry *			 debug_file;
	struct delayed_work 	 work;			/* battery status polling */
	struct workqueue_struct *work_queue; 
	
	struct work_struct 		 isr_work;		/* Low batt notification ISR */
	struct workqueue_struct *isr_wq; 

	struct mutex 			 lock;			/* mutex protect battery update */
	
	int    next_update;						/* Time to next update */
	
	int    low_batt_irq;					/* If there is a low battery IRQ */
	int	   low_batt_alarm_percent;			/* Percentage of charge to fire the low batt alarm */
	int    use_irq;							/* if using a low battery irq */
	int    in_s3_state_gpio;				/* Gpio used to indicate system is in S3 */ 
	
	/* Battery status and information */
	int on;
	int bat_present;
	int bat_status;
	int bat_voltage_now;
	int bat_current_now;
	int bat_current_avg;
	int time_remain;
	int charge_full_design;
	int charge_last_full;
	int critical_capacity;
	int capacity_remain;
	int bat_temperature;
	int bat_cap;	/* as percent */
	int bat_type_enum;
	char bat_manu[32];
	char bat_model[32];
	char bat_type[32];
};

static enum power_supply_property nvec_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property nvec_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TECHNOLOGY,
};

static int nvec_power_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct nvec_power *power = dev_get_drvdata(psy->dev->parent);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = power->on;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int nvec_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct nvec_power *power = dev_get_drvdata(psy->dev->parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = power->bat_status;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = power->bat_cap;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = power->bat_present;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = power->bat_voltage_now;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = power->bat_current_now;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = power->bat_current_avg;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		val->intval = power->time_remain;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = power->charge_full_design;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = power->charge_last_full;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = power->critical_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = power->capacity_remain;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = power->bat_temperature;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = power->bat_manu;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = power->bat_model;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = power->bat_type_enum;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static char *nvec_power_supplied_to[] = {
	"battery",
};

static struct power_supply nvec_bat_psy = {
	.name			= "battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= nvec_battery_props,
	.num_properties	= ARRAY_SIZE(nvec_battery_props),
	.get_property	= nvec_battery_get_property,
};

static struct power_supply nvec_ac_psy = {
	.name 			= "ac",
	.type 			= POWER_SUPPLY_TYPE_MAINS,
	.supplied_to 	= nvec_power_supplied_to,
	.num_supplicants= ARRAY_SIZE(nvec_power_supplied_to),
	.properties 	= nvec_power_props,
	.num_properties = ARRAY_SIZE(nvec_power_props),
	.get_property 	= nvec_power_get_property,
};


/* Get battery manufacturer data */
static void get_bat_mfg_data(struct nvec_power *power)
{	
	int res;
	struct device* master = power->master;
	
	{
		struct NVEC_ANS_BATTERY_GETLASTFULLCHARGECAPACITY_PAYLOAD getLastFullChargeCap;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETLASTFULLCHARGECAPACITY, 
							NULL,0,&getLastFullChargeCap,sizeof(getLastFullChargeCap)) == sizeof(getLastFullChargeCap)) {
			power->charge_last_full = NVEC_GETU16(getLastFullChargeCap.LastFullChargeCapacity) * 1000;
		} else {
			dev_err(power->dev,"unable to get last full charge capacity\n");
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETDESIGNCAPACITY_PAYLOAD getDesignCap;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETDESIGNCAPACITY, 
							NULL,0,&getDesignCap,sizeof(getDesignCap)) == sizeof(getDesignCap)) {
			power->charge_full_design = NVEC_GETU16(getDesignCap.DesignCapacity) * 1000;
		} else {
			dev_err(power->dev,"unable to get design capacity\n");
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETCRITICALCAPACITY_PAYLOAD getCriticalCap;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETCRITICALCAPACITY,
							NULL,0,&getCriticalCap,sizeof(getCriticalCap)) == sizeof(getCriticalCap)) {
			power->critical_capacity = NVEC_GETU16(getCriticalCap.CriticalCapacity) * 1000;
		} else {
			dev_err(power->dev,"unable to get critical capacity\n");
		}
		
		/* Validate critical capacity. If it does make not sense, estimate it
		   as 10% of full capacity */
		if (power->critical_capacity == 0 || 
			power->critical_capacity >= (power->charge_full_design>>1)) {
			/* Seems to be incorrect. Estimate it as 10% of full design */
			dev_dbg(power->dev,"critical capacity seems wrong. Estimate it as 10%% of total\n");
			power->critical_capacity = power->charge_full_design / 10;
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETMANUFACTURER_PAYLOAD getManuf;
		if ((res = nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETMANUFACTURER, 
							NULL,0,&getManuf,sizeof(getManuf))) >= 0) {
			if (res > (ARRAY_SIZE(power->bat_manu) -1)) {
				res = ARRAY_SIZE(power->bat_manu) - 1;
			}
			memcpy(power->bat_manu, &getManuf.Manufacturer, res);
			power->bat_manu[res] = '\0';
			if (power->bat_manu[0] == 0) {
				dev_dbg(power->dev,"unknown manufacturer\n");
				strcpy(power->bat_manu,"Unknown");
			}
		} else {
			dev_err(power->dev,"unable to get battery manufacturer\n");
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETMODEL_PAYLOAD getModel;
		if ((res = nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETMODEL, 
							NULL,0,&getModel,sizeof(getModel))) >= 0) {
			if (res > (ARRAY_SIZE(power->bat_model) - 1)) {
				res = ARRAY_SIZE(power->bat_model) - 1;
			}
			memcpy(power->bat_model, &getModel.Model, res);
			power->bat_model[res] = '\0';
			if (power->bat_model[0] == 0) {
				dev_dbg(power->dev,"unknown model\n");
				strcpy(power->bat_model,"Unknown");
			}
			
		} else {
			dev_err(power->dev,"unable to get battery model\n");
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETTYPE_PAYLOAD getType;
		if ((res = nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETTYPE,
							NULL,0,&getType,sizeof(getType))) >= 0) {
			if (res > (ARRAY_SIZE(power->bat_type) - 1)) {
				res = ARRAY_SIZE(power->bat_type) - 1;
			}
			memcpy(power->bat_type, &getType.Type, res);
			power->bat_type[res] = '\0';

			if (power->bat_type[0] == 0) {
				dev_dbg(power->dev,"unknown type - Assuming LiPo\n");
				strcpy(power->bat_type,"LIPOLY");
			}
			
			/* this differs a little from the spec - fill in more if you find some */
			if (!strcmp(power->bat_type, "Li"))
				power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_LION;
			else
			if (!strcmp(power->bat_type, "LION"))
				power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_LION;
			else 
			if (!strcmp(power->bat_type, "Alkaline"))
				power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
			else 
			if (!strcmp(power->bat_type, "NICD"))
				power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_NiCd;
			else 
			if (!strcmp(power->bat_type, "NIMH"))
				power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_NiMH;
			else 
			if (!strcmp(power->bat_type, "LIPOLY"))
				power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_LIPO;
			else 
			if (!strcmp(power->bat_type, "XINCAIR"))
				power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
			else 
				power->bat_type_enum = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
			
		} else {
			dev_err(power->dev,"unable to get battery type\n");
		}
	}
}

/* This fn is the only one that should be called by the driver to update
   the battery status */
static int nvec_power_update_status(struct nvec_power *power,bool force_update)
{
	int bat_status_changed = 0;
	struct device* master = power->master;
	
	/* Do not accept to update too often */
	if (!force_update && (power->next_update - jiffies) > 0)
		return 0;

	/* get exclusive access to the accelerometer */
	mutex_lock(&power->lock);	

	{
		struct NVEC_ANS_BATTERY_GETCAPACITYREMAINING_PAYLOAD getRemCapacity;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETCAPACITYREMAINING,
					NULL,0,&getRemCapacity,sizeof(getRemCapacity)) == sizeof(getRemCapacity)) {
			power->capacity_remain = NVEC_GETU16(getRemCapacity.CapacityRemaining) * 1000;
		} else {
			dev_err(power->dev,"unable to get Battery remaining capacity\n");
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETSLOTSTATUS_PAYLOAD getSlotStatus;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETSLOTSTATUS, 
					NULL,0,&getSlotStatus,sizeof(getSlotStatus)) == sizeof(getSlotStatus)) {
					
			/* If battery is present */
			if (getSlotStatus.SlotStatus & NVEC_ANS_BATTERY_SLOT_STATUS_0_PRESENT_STATE_PRESENT) {
				if (power->bat_present == 0) {
					bat_status_changed = 1;
					get_bat_mfg_data(power);
				}

				power->bat_present = 1;

				switch (getSlotStatus.SlotStatus & NVEC_ANS_BATTERY_SLOT_STATUS_0_CHARGING_STATE_MASK) {
				case NVEC_ANS_BATTERY_SLOT_STATUS_0_CHARGING_STATE_IDLE:
					power->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
					break;
				case NVEC_ANS_BATTERY_SLOT_STATUS_0_CHARGING_STATE_CHARGING:
					power->bat_status = POWER_SUPPLY_STATUS_CHARGING;
					break;
				case NVEC_ANS_BATTERY_SLOT_STATUS_0_CHARGING_STATE_DISCHARGING:
					power->bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
					break;
				default:
					power->bat_status = POWER_SUPPLY_STATUS_UNKNOWN;
				}
			} else {
				if (power->bat_present == 1)
					bat_status_changed = 1;

				power->bat_present = 0;
				power->bat_status = POWER_SUPPLY_STATUS_UNKNOWN;
			}
			power->bat_cap = getSlotStatus.CapacityGauge;
			
			/* Validate bat_cap. If out of range, calculate it if possible */
			if (power->bat_cap == 0 || power->bat_cap > 100) {
				int range;
				
				dev_dbg(power->dev,"available batt capacity invalid. Estimating it\n");

				range = (power->charge_last_full - power->critical_capacity);
				if (range <= 0) {
				
					/* Estimation is impossible. Assume 50% charge ... */
					power->bat_cap = 50;
				} else {
				
					/* Estimate remaining capacity */
					int perc = ((power->capacity_remain - power->critical_capacity) * 100) / range;
					if (perc < 0)
						perc = 0;
					if (perc > 100)
						perc = 100;
					power->bat_cap = perc;
				}
			}
			
		} else {
			dev_err(power->dev,"unable to get Battery status\n");
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETVOLTAGE_PAYLOAD getVoltage;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETVOLTAGE, 
					NULL,0,&getVoltage,sizeof(getVoltage)) == sizeof(getVoltage)) {
			power->bat_voltage_now = NVEC_GETU16(getVoltage.PresentVoltage) * 1000;
		} else {
			dev_err(power->dev,"unable to get Battery voltage\n");
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETCURRENT_PAYLOAD getCurrent;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETCURRENT, 
					NULL,0,&getCurrent,sizeof(getCurrent)) == sizeof(getCurrent)) {
			/* The value is signed, so convert it */
			s16 val = (s16) NVEC_GETU16(getCurrent.PresentCurrent);
			power->bat_current_now = val * 1000;
			
			/* Validate battery status. If current is negative, battery is
			   discharging ... otherwise is charging, unless charge is 100%.
			   We validate data, as some firmware do not provide proper information */
			if (power->bat_present) {
				if (val < 0) {
					power->bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
				} else if (val > 0) {
					if (power->bat_cap >= 100) {
						power->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
					} else {
						power->bat_status = POWER_SUPPLY_STATUS_CHARGING;
					}
				}
			}
		} else {
			dev_err(power->dev,"unable to get Battery current\n");
		}
	}

	/* AC status via sys req */
	{
		int ac;
		struct NVEC_ANS_SYSTEM_GETSTATUS_PAYLOAD sysState;
		if (nvec_cmd_xfer(master, NVEC_CMD_SYSTEM,NVEC_CMD_SYSTEM_GETSTATUS,
					NULL,0,&sysState,sizeof(sysState)) == sizeof(sysState)) {
			ac = ((sysState.State[1] & NVEC_SYSTEM_STATE1_0_AC_MASK) 
					== NVEC_SYSTEM_STATE1_0_AC_PRESENT) ? 1 : 0;
					
			/* Validate results... If battery is charging, then the AC adapter 
			   MUST be present, and if battery is not charging, it must not be*/
			if (power->bat_present) {
				if (power->bat_status == POWER_SUPPLY_STATUS_CHARGING) {
					ac = 1;
				} else
				if (power->bat_status == POWER_SUPPLY_STATUS_DISCHARGING) {
					ac = 0;
				}
			}
			
			if (power->on != ac) {
				power->on = ac;
				power_supply_changed(&nvec_ac_psy);
			}
		} else {
			dev_err(power->dev,"unable to get AC line status\n");
		}
	}

	{
		struct NVEC_ANS_BATTERY_GETAVERAGECURRENT_PAYLOAD getAvgCurrent;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETAVERAGECURRENT, 
					NULL,0,&getAvgCurrent,sizeof(getAvgCurrent)) == sizeof(getAvgCurrent)) {
			/* The value is signed, so convert it */
			s16 val = (s16) NVEC_GETU16(getAvgCurrent.AverageCurrent);
			power->bat_current_avg = val * 1000;
		} else {
			dev_err(power->dev,"unable to get Battery average current\n");
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETTEMPERATURE_PAYLOAD getTemp;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETTEMPERATURE, 
					NULL,0,&getTemp,sizeof(getTemp)) == sizeof(getTemp)) {
			power->bat_temperature = NVEC_GETU16(getTemp.Temperature) - 2732;
		} else {
			dev_err(power->dev,"unable to get Battery temperature\n");
		}
	}
	
	{
		struct NVEC_ANS_BATTERY_GETTIMEREMAINING_PAYLOAD getRemTime;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY,NVEC_CMD_BATTERY_GETTIMEREMAINING,
					NULL,0,&getRemTime,sizeof(getRemTime)) == sizeof(getRemTime)) {
			power->time_remain = NVEC_GETU16(getRemTime.TimeRemaining) * 60; /* in seconds */
		} else {
			dev_err(power->dev,"unable to get Battery remaining time\n");
		}
	}
	
	if (bat_status_changed)
		power_supply_changed(&nvec_bat_psy);
	
	/* Allow next update 10 seconds from now */
	power->next_update = jiffies + msecs_to_jiffies(NVEC_POWER_POLLING_INTERVAL/2);
		
	mutex_unlock(&power->lock);		
	
	return 0;
} 

static int nvec_power_initialize(struct nvec_power *power)
{
	struct device* master = power->master;

	/* Make sure to configure NvEC units in mAh */
	{
		struct NVEC_REQ_BATTERY_SETCONFIGURATION_PAYLOAD setCfg = {
			.Configuration = NVEC_BATTERY_CONFIGURATION_0_CAPACITY_UNITS_MAH,
		};
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY, NVEC_CMD_BATTERY_SETCONFIGURATION,
					&setCfg,sizeof(setCfg),NULL,0) < 0) {
			dev_err(power->dev,"unable to set NvEC capacity units\n");
		}
	}
	
	/* Get battery manufacturer data */
	get_bat_mfg_data(power);

	/* Now, configure the alarms and events we are interested in */
	
	/* Configure the Battery present and low capacity as a wakeup */
	{
		struct NVEC_REQ_BATTERY_CONFIGUREWAKE_PAYLOAD cfgWake = {
			.WakeEnable = NVEC_REQ_BATTERY_WAKE_ENABLE_ACTION_ENABLE,
			.EventTypes = NVODM_BATTERY_SET_PRESENT_EVENT | 
						NVODM_BATTERY_SET_REM_CAP_ALARM_EVENT,
		};
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY, NVEC_CMD_BATTERY_CONFIGUREWAKE, 
					&cfgWake,sizeof(cfgWake),NULL,0) < 0) {
			dev_err(power->dev,"unable to set battery wakeups\n");
		}
	}
	
	/* Configure the AC present event  as a wakeup */
	{
		/* NB: Don't know why, but ODM uses this struct to config this system event... */
		struct NVEC_REQ_BATTERY_CONFIGUREWAKE_PAYLOAD cfgWake = {
			.WakeEnable = NVEC_REQ_SYSTEM_REPORT_ENABLE_0_ACTION_ENABLE,
			.EventTypes = NVEC_SYSTEM_STATE1_0_AC_PRESENT,
		};
		if (nvec_cmd_xfer(master,NVEC_CMD_SYSTEM, NVEC_CMD_SYSTEM_CONFIGWAKE,
					&cfgWake,sizeof(cfgWake),NULL,0) < 0) {
			dev_err(power->dev,"unable to set system wakeups\n");
		}
	}
	
    /* Configure the Battery events */
	{
		struct NVEC_REQ_BATTERY_CONFIGUREEVENTREPORTING_PAYLOAD cfgEvRep = {
			.ReportEnable = NVEC_REQ_BATTERY_REPORT_ENABLE_0_ACTION_ENABLE,
			/* Bit 0 = Present State event */
			/* Bit 1 = Charging State event */
			/* Bit 2 = Remaining Capacity Alaram event */
			.EventTypes =  NVODM_BATTERY_SET_PRESENT_EVENT |
						NVODM_BATTERY_SET_CHARGING_EVENT|
						NVODM_BATTERY_SET_REM_CAP_ALARM_EVENT,
		};
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY, NVEC_CMD_BATTERY_CONFIGUREEVENTREPORTING, 
					&cfgEvRep,sizeof(cfgEvRep),NULL,0) < 0) {
			dev_err(power->dev,"unable to set battery events\n");
		}
	}
		
	/* We have the battery design capacity. Set the alarm to the specified percent */
	{
		struct NVEC_ANS_BATTERY_SETREMAININGCAPACITYALARM_PAYLOAD alarmCfg;
		int alarm_perc = 10, rem_cap_alarm;
		if (power->low_batt_alarm_percent && power->low_batt_alarm_percent < 100) {
			alarm_perc = power->low_batt_alarm_percent;
		}

		/* Set the remaining capacity alarm to the specified percent of the design capacity */
        rem_cap_alarm = (power->charge_full_design * alarm_perc)/100;
		alarmCfg.CapacityThreshold[0] = rem_cap_alarm & 0xFF;
		alarmCfg.CapacityThreshold[1] = rem_cap_alarm >> 8;
		if (nvec_cmd_xfer(master,NVEC_CMD_BATTERY, NVEC_CMD_BATTERY_SETREMAININGCAPACITYALARM, 
					&alarmCfg,sizeof(alarmCfg),NULL,0) < 0) {
			dev_err(power->dev,"unable to set low battery alarm threshold\n");
		}
	}
		
	return 0;
}

static int nvec_power_notifier(struct notifier_block *nb,
				 unsigned long event_type, void *data)
{
	struct nvec_power *power = container_of(nb, struct nvec_power, notifier);
	struct nvec_event *ev = (struct nvec_event *)data;
	int bat_event;
	
	/* If not targeting battery, do not process it */
	if (event_type != NVEC_EV_BATTERY)
		return NOTIFY_DONE;
	
	if (ev->size == 0) {
		dev_err(power->dev,"Received Battery event with no data\n");
		return NOTIFY_DONE;
	}

	dev_dbg(power->dev,"received battery notification: 0x%02x,0x%02x\n", ev->data[0], ev->data[1]);
	
	/* ev->data[0] is Slot number */
	/* ev->data[1] has 4 lsb bits for battery events */
	bat_event = ev->data[1] & NVODM_BATTERY_EVENT_MASK;

	/* Read the Battery Slot status to set the proper event */
	if (bat_event & NVODM_BATTERY_PRESENT_IN_SLOT) {
		int charging_state;
		
		/* Battery is present */
		power->bat_present = 1;

		/* Find out the battery charging state and store it */
		charging_state = bat_event >> NVODM_BATTERY_CHARGING_STATE_SHIFT;
		charging_state &= NVODM_BATTERY_CHARGING_STATE_MASK;
		if (charging_state == NVODM_BATTERY_CHARGING_STATE_IDLE)
			power->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (charging_state == NVODM_BATTERY_CHARGING_STATE_CHARGING)
			power->bat_status = POWER_SUPPLY_STATUS_CHARGING;
		else if (charging_state == NVODM_BATTERY_CHARGING_STATE_DISCHARGING)
			power->bat_status = POWER_SUPPLY_STATUS_DISCHARGING;

		/* Find out if it is a low battery alarm */
		charging_state = bat_event >> NVODM_BATTERY_REM_CAP_ALARM_SHIFT;
		if (charging_state == NVODM_BATTERY_REM_CAP_ALARM_IS_SET) {
			/* It is... just shutdown kernel */
			pr_info("Battery critically low. Calling kernel_power_off()!\n");
			kernel_power_off();
        } 	
		
		/* Propagate changes */
		power_supply_changed(&nvec_bat_psy);
	}
	
	return NOTIFY_STOP;
}




#ifdef CONFIG_DEBUG_FS
static int bat_debug_show(struct seq_file *s, void *data)
{
	struct nvec_power *power = s->private;
	const char* bat_status = "unknown";
	
	int ret = nvec_power_update_status(power,true);
	if (ret)
		return ret; 
		
	/* Convert battery status */
	switch (power->bat_status) {
	case POWER_SUPPLY_STATUS_NOT_CHARGING:
		bat_status = "not charging";
		break;
	case POWER_SUPPLY_STATUS_CHARGING:
		bat_status = "charging";
		break;
	case POWER_SUPPLY_STATUS_DISCHARGING:
		bat_status = "discharging";
		break;
	default:
		break;
	}
	
	seq_printf(s, "AC charger is %s\n", power->on ? "on" : "off");
	seq_printf(s, "Battery is %s\n", power->bat_present ? "present" : "absent");
	seq_printf(s, "Battery is %s\n", bat_status);
	seq_printf(s, "Battery voltage is %d mV\n", power->bat_voltage_now / 1000);	
	seq_printf(s, "Battery current is %d mA\n", power->bat_current_now / 1000);	
	seq_printf(s, "Battery average current is %d mA\n", power->bat_current_avg / 1000);	
	seq_printf(s, "Battery remaining time is %d minutes\n", power->time_remain/60);	
	seq_printf(s, "Battery full charge by design is %d mAh\n", power->charge_full_design/1000);	
	seq_printf(s, "Battery last full charge was %d mAh\n", power->charge_last_full/1000);		
	seq_printf(s, "Battery critical capacity is %d mAh\n", power->critical_capacity/1000);		
	seq_printf(s, "Battery capacity remaining is %d mAh\n", power->capacity_remain/1000);		
	seq_printf(s, "Battery temperature is %d degrees C\n", power->bat_temperature/10);		
	seq_printf(s, "Battery current capacity is %d %%\n", power->bat_cap);		
	seq_printf(s, "Battery manufacturer is %s\n", power->bat_manu);			
	seq_printf(s, "Battery model is %s\n", power->bat_model);			
	seq_printf(s, "Battery chemistry is %s\n", power->bat_type);			
	
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

static struct dentry *nvec_power_create_debugfs(struct nvec_power *power)
{
	power->debug_file = debugfs_create_file("charger", 0666, 0, power,
						 &bat_debug_fops);
	return power->debug_file;
}

static void nvec_power_remove_debugfs(struct nvec_power *power)
{
	debugfs_remove(power->debug_file);
}
#else
static inline struct dentry *nvec_power_create_debugfs(struct nvec_power *power)
{
	return NULL;
}
static inline void nvec_power_remove_debugfs(struct nvec_power *power)
{
}
#endif 

static void nvec_power_work_func(struct work_struct *work)
{
	struct nvec_power *power = container_of((struct delayed_work*)work,
		struct nvec_power, work);

	/* Update power supply status */
	if (!nvec_power_update_status(power,false)) {
		power_supply_changed(&nvec_bat_psy);
		power_supply_changed(&nvec_ac_psy);
	}
	
	queue_delayed_work(power->work_queue, &power->work,
				 msecs_to_jiffies(NVEC_POWER_POLLING_INTERVAL));
} 

static void nvec_power_isr_work_func(struct work_struct *isr_work)
{
	struct nvec_power *power = container_of(isr_work,
		struct nvec_power, isr_work);

	/* Update power supply status */
	if (!nvec_power_update_status(power,true)) {
		power_supply_changed(&nvec_bat_psy);
		power_supply_changed(&nvec_ac_psy);
		
		/* If battery is below minimun, force a kernel shutdown */
		if (power->capacity_remain < power->critical_capacity) {
			pr_info("Battery critically low. Calling kernel_power_off()!\n");
			kernel_power_off();
		}
	}
		
	enable_irq(power->low_batt_irq);
} 

static irqreturn_t nvec_power_irq_handler(int irq, void *dev_id)
{
	struct nvec_power *power = dev_id;

	disable_irq_nosync(power->low_batt_irq);
	
	queue_work(power->isr_wq, &power->isr_work);
	return IRQ_HANDLED;
}

static int __devinit nvec_power_probe(struct platform_device *pdev)
{
	struct nvec_power *power;
	struct nvec_power_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL; 
	}			


	power = kzalloc(sizeof(*power), GFP_KERNEL);
	if (power == NULL) {
		dev_err(&pdev->dev, "no memory for context\n");
		return -ENOMEM;
	}
	dev_set_drvdata(&pdev->dev, power);
	platform_set_drvdata(pdev, power);
	
	power->master = pdev->dev.parent;
	power->dev = &pdev->dev;
	power->low_batt_irq = pdata->low_batt_irq;
	power->low_batt_alarm_percent = pdata->low_batt_alarm_percent;
	power->in_s3_state_gpio = pdata->in_s3_state_gpio; 	
 
	/* Init the protection mutex */
	mutex_init(&power->lock);   
	
	ret = nvec_power_initialize(power);
	if (ret)
		goto err_charger_init;
		
	/* Register power supplies */
	ret = power_supply_register(&pdev->dev, &nvec_bat_psy);
	if (ret)
		goto err_ps_register;

	ret = power_supply_register(&pdev->dev, &nvec_ac_psy);
	if (ret)
		goto err_ps_register2; 
		
		
	power->debug_file = nvec_power_create_debugfs(power);
		
	power->work_queue = create_singlethread_workqueue("nvec_power_wq");
	if (!power->work_queue) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "could not create workqueue\n");
		goto err_ps_register4;
	}
	INIT_DELAYED_WORK(&power->work, nvec_power_work_func);
	
	power->isr_wq = create_singlethread_workqueue("nvec_power_isr_wq");
	if (!power->isr_wq) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "could not create isr workqueue\n");
		goto err_ps_register5;
	}
	
	INIT_WORK(&power->isr_work, nvec_power_isr_work_func);

	if (power->low_batt_irq) {
		ret = request_irq(power->low_batt_irq, nvec_power_irq_handler,
			IRQF_TRIGGER_FALLING, "nvec_power_lowbatt_isr", power);
		if (!ret) {
			power->use_irq = 1;
		} else {
			dev_err(&pdev->dev, "request_irq failed\n");
		}
	}
	
	/* Finally, get the S3 gpio, if specified */
	if (power->in_s3_state_gpio) {
		gpio_request(power->in_s3_state_gpio,"S3 flag");
		gpio_direction_output(power->in_s3_state_gpio,0);
	}

	/* Register notifier */
	power->notifier.notifier_call = nvec_power_notifier;
	nvec_add_eventhandler(power->master, &power->notifier);

	queue_delayed_work(power->work_queue, &power->work,
		 msecs_to_jiffies(NVEC_POWER_POLLING_INTERVAL));

	dev_info(&pdev->dev, "NvEC power controller driver registered\n");
	
	return 0;
	
err_ps_register5:	
	cancel_delayed_work_sync(&power->work);
	destroy_workqueue(power->work_queue);
	
err_ps_register4:
	power_supply_unregister(&nvec_ac_psy);
	
err_ps_register2:	
	power_supply_unregister(&nvec_bat_psy);
	
err_ps_register:
	
err_charger_init:
	mutex_destroy(&power->lock); 
	kfree(power);

	return ret; 	
}

static int __devexit nvec_power_remove(struct platform_device *dev)
{
	struct nvec_power *power = platform_get_drvdata(dev);
	
	nvec_remove_eventhandler(power->master, &power->notifier);
	
	if (power->use_irq)
		free_irq(power->low_batt_irq,power);
		
	if (power->in_s3_state_gpio)
		gpio_free(power->in_s3_state_gpio);
		
	destroy_workqueue(power->isr_wq);
	cancel_delayed_work_sync(&power->work);
	destroy_workqueue(power->work_queue);
	nvec_power_remove_debugfs(power);
	
	power_supply_unregister(&nvec_bat_psy);
	power_supply_unregister(&nvec_ac_psy);
	mutex_destroy(&power->lock); 
	kfree(power);

	return 0;
}

static int nvec_power_resume(struct platform_device *dev)
{
	struct nvec_power *power = platform_get_drvdata(dev);
	if (power->in_s3_state_gpio) 
		gpio_set_value(power->in_s3_state_gpio,0);
	queue_delayed_work(power->work_queue, &power->work,
		msecs_to_jiffies(NVEC_POWER_POLLING_INTERVAL));
	return 0;
}

static int nvec_power_suspend(struct platform_device *dev, pm_message_t mesg)
{
	struct nvec_power *power = platform_get_drvdata(dev);
	cancel_delayed_work_sync(&power->work);
	if (power->in_s3_state_gpio) 
		gpio_set_value(power->in_s3_state_gpio,1);
	return 0;
} 

static struct platform_driver nvec_power_driver = {
	.probe = nvec_power_probe,
	.remove = __devexit_p(nvec_power_remove), 
	.suspend = nvec_power_suspend,
	.resume = nvec_power_resume, 	
	.driver = {
		.name = "nvec-power",
		.owner = THIS_MODULE,
	}
};

static int __init nvec_power_init(void)
{
	return platform_driver_register(&nvec_power_driver);
}

static void __exit nvec_power_exit(void)
{
	platform_driver_unregister(&nvec_power_driver);
} 

module_init(nvec_power_init);
module_exit(nvec_power_exit);

MODULE_AUTHOR("Ilya Petrov <ilya.muromec@gmail.com>/Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NVEC battery and AC driver");
MODULE_ALIAS("platform:nvec-power");
