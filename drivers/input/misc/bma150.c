/*
 * BMA150/SMB380 linux driver
 *  Copyright (C) 2011 Eduardo José Tagle <ejtagle@tutopia.com> 
 *  Copyright (C) 2009 Bosch Sensortec GmbH
 *  Authors:	Eduardo José Tagle   <ejtagle@tutopia.com>
 *		Rene Bensch "rebel1" <rene.bensch@googlemail,com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

/*! \file bma150.c
    \brief This file contains all function implementations for the BMA150 in linux
    
    Details.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <asm/uaccess.h>
#include <linux/unistd.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/input.h>
#include <linux/gpio.h>

#define TEGRA_GPIO_GSENSOR_TOGGLE       57
#define xy_flip
static int temp1;

/** BMA150 acceleration data 
	\brief Structure containing acceleration values for x,y and z-axis in signed short
*/
typedef struct {
	short x,	 /**< holds x-axis acceleration data sign extended. Range -512 to 511. */
	 y,		     /**< holds y-axis acceleration data sign extended. Range -512 to 511. */
	 z;		     /**< holds z-axis acceleration data sign extended. Range -512 to 511. */
} bma150acc_t;

/** bma150 typedef structure
	\brief This structure holds all relevant information about BMA150 and links communication to the 
*/
struct bma150ctx {
	struct i2c_client *client;
	struct input_dev *input;
	char phys[32];
	struct mutex lock;
	struct delayed_work input_work;
	struct workqueue_struct *input_work_queue;

	atomic_t open_cnt;		/** if accelerometer is enabled or not */
	int enabled_b4_suspend;
	bma150acc_t acc;		/** last read acceleration */

	unsigned char mode;		/**< save current BMA150 operation mode */
	unsigned char chip_id,	/**< save BMA150's chip id which has to be 0x02 after calling bma150_init() */
	 ml_version,			      /**< holds the BMA150 ML_version number */
	 al_version;			      /**< holds the BMA150 AL_version number */
};

/* BMA150 Macro for read and write commincation */

/** define for used read and write macros */
#define BMA150_SPI_RD_MASK 0x80	/* for spi read transactions on SPI the MSB has to be set */

/** BMA150 I2C Address */
#define BMA150_I2C_ADDR		0x38

/* register definitions */
#define BMA150_EEP_OFFSET   0x20

#define BMA150_CHIP_ID_REG			0x00
#define BMA150_VERSION_REG			0x01
#define BMA150_X_AXIS_LSB_REG		0x02
#define BMA150_X_AXIS_MSB_REG		0x03
#define BMA150_Y_AXIS_LSB_REG		0x04
#define BMA150_Y_AXIS_MSB_REG		0x05
#define BMA150_Z_AXIS_LSB_REG		0x06
#define BMA150_Z_AXIS_MSB_REG		0x07
#define BMA150_TEMP_RD_REG			0x08
#define BMA150_STATUS_REG	0x09
#define BMA150_CTRL_REG		0x0a
#define BMA150_CONF1_REG	0x0b
#define BMA150_LG_THRESHOLD_REG	0x0c
#define BMA150_LG_DURATION_REG		0x0d
#define BMA150_HG_THRESHOLD_REG	0x0e
#define BMA150_HG_DURATION_REG		0x0f
#define BMA150_MOTION_THRS_REG		0x10
#define BMA150_HYSTERESIS_REG		0x11
#define BMA150_CUSTOMER1_REG		0x12
#define BMA150_CUSTOMER2_REG		0x13
#define BMA150_RANGE_BWIDTH_REG	0x14
#define BMA150_CONF2_REG	0x15

#define BMA150_OFFS_GAIN_X_REG		0x16
#define BMA150_OFFS_GAIN_Y_REG		0x17
#define BMA150_OFFS_GAIN_Z_REG		0x18
#define BMA150_OFFS_GAIN_T_REG		0x19
#define BMA150_OFFSET_X_REG		0x1a
#define BMA150_OFFSET_Y_REG		0x1b
#define BMA150_OFFSET_Z_REG		0x1c
#define BMA150_OFFSET_T_REG		0x1d

/* bit slice positions in registers*/
/** \cond BITSLICE */
#define BMA150_CHIP_ID__POS		0
#define BMA150_CHIP_ID__MSK		0x07
#define BMA150_CHIP_ID__LEN		3
#define BMA150_CHIP_ID__REG		BMA150_CHIP_ID_REG

#define BMA150_ML_VERSION__POS		0
#define BMA150_ML_VERSION__LEN		4
#define BMA150_ML_VERSION__MSK		0x0F
#define BMA150_ML_VERSION__REG		BMA150_VERSION_REG

#define BMA150_AL_VERSION__POS  	4
#define BMA150_AL_VERSION__LEN  	4
#define BMA150_AL_VERSION__MSK		0xF0
#define BMA150_AL_VERSION__REG		BMA150_VERSION_REG

/* DATA REGISTERS */
#define BMA150_NEW_DATA_X__POS  	0
#define BMA150_NEW_DATA_X__LEN  	1
#define BMA150_NEW_DATA_X__MSK  	0x01
#define BMA150_NEW_DATA_X__REG		BMA150_X_AXIS_LSB_REG

#define BMA150_ACC_X_LSB__POS   	6
#define BMA150_ACC_X_LSB__LEN   	2
#define BMA150_ACC_X_LSB__MSK		0xC0
#define BMA150_ACC_X_LSB__REG		BMA150_X_AXIS_LSB_REG

#define BMA150_ACC_X_MSB__POS   	0
#define BMA150_ACC_X_MSB__LEN   	8
#define BMA150_ACC_X_MSB__MSK		0xFF
#define BMA150_ACC_X_MSB__REG		BMA150_X_AXIS_MSB_REG

#define BMA150_NEW_DATA_Y__POS  	0
#define BMA150_NEW_DATA_Y__LEN  	1
#define BMA150_NEW_DATA_Y__MSK  	0x01
#define BMA150_NEW_DATA_Y__REG		BMA150_Y_AXIS_LSB_REG

#define BMA150_ACC_Y_LSB__POS   	6
#define BMA150_ACC_Y_LSB__LEN   	2
#define BMA150_ACC_Y_LSB__MSK   	0xC0
#define BMA150_ACC_Y_LSB__REG		BMA150_Y_AXIS_LSB_REG

#define BMA150_ACC_Y_MSB__POS   	0
#define BMA150_ACC_Y_MSB__LEN   	8
#define BMA150_ACC_Y_MSB__MSK   	0xFF
#define BMA150_ACC_Y_MSB__REG		BMA150_Y_AXIS_MSB_REG

#define BMA150_NEW_DATA_Z__POS  	0
#define BMA150_NEW_DATA_Z__LEN  	1
#define BMA150_NEW_DATA_Z__MSK		0x01
#define BMA150_NEW_DATA_Z__REG		BMA150_Z_AXIS_LSB_REG

#define BMA150_ACC_Z_LSB__POS   	6
#define BMA150_ACC_Z_LSB__LEN   	2
#define BMA150_ACC_Z_LSB__MSK		0xC0
#define BMA150_ACC_Z_LSB__REG		BMA150_Z_AXIS_LSB_REG

#define BMA150_ACC_Z_MSB__POS   	0
#define BMA150_ACC_Z_MSB__LEN   	8
#define BMA150_ACC_Z_MSB__MSK		0xFF
#define BMA150_ACC_Z_MSB__REG		BMA150_Z_AXIS_MSB_REG

#define BMA150_TEMPERATURE__POS 	0
#define BMA150_TEMPERATURE__LEN 	8
#define BMA150_TEMPERATURE__MSK 	0xFF
#define BMA150_TEMPERATURE__REG		BMA150_TEMP_RD_REG

/* STATUS BITS */
#define BMA150_STATUS_HG__POS		0
#define BMA150_STATUS_HG__LEN		1
#define BMA150_STATUS_HG__MSK		0x01
#define BMA150_STATUS_HG__REG		BMA150_STATUS_REG

#define BMA150_STATUS_LG__POS		1
#define BMA150_STATUS_LG__LEN		1
#define BMA150_STATUS_LG__MSK		0x02
#define BMA150_STATUS_LG__REG		BMA150_STATUS_REG

#define BMA150_HG_LATCHED__POS  	2
#define BMA150_HG_LATCHED__LEN  	1
#define BMA150_HG_LATCHED__MSK		0x04
#define BMA150_HG_LATCHED__REG		BMA150_STATUS_REG

#define BMA150_LG_LATCHED__POS		3
#define BMA150_LG_LATCHED__LEN		1
#define BMA150_LG_LATCHED__MSK		8
#define BMA150_LG_LATCHED__REG		BMA150_STATUS_REG

#define BMA150_ALERT_PHASE__POS		4
#define BMA150_ALERT_PHASE__LEN		1
#define BMA150_ALERT_PHASE__MSK		0x10
#define BMA150_ALERT_PHASE__REG		BMA150_STATUS_REG

#define BMA150_ST_RESULT__POS		7
#define BMA150_ST_RESULT__LEN		1
#define BMA150_ST_RESULT__MSK		0x80
#define BMA150_ST_RESULT__REG		BMA150_STATUS_REG

/* CONTROL BITS */
#define BMA150_SLEEP__POS			0
#define BMA150_SLEEP__LEN			1
#define BMA150_SLEEP__MSK			0x01
#define BMA150_SLEEP__REG			BMA150_CTRL_REG

#define BMA150_SOFT_RESET__POS		1
#define BMA150_SOFT_RESET__LEN		1
#define BMA150_SOFT_RESET__MSK		0x02
#define BMA150_SOFT_RESET__REG		BMA150_CTRL_REG

#define BMA150_SELF_TEST__POS		2
#define BMA150_SELF_TEST__LEN		2
#define BMA150_SELF_TEST__MSK		0x0C
#define BMA150_SELF_TEST__REG		BMA150_CTRL_REG

#define BMA150_SELF_TEST0__POS		2
#define BMA150_SELF_TEST0__LEN		1
#define BMA150_SELF_TEST0__MSK		0x04
#define BMA150_SELF_TEST0__REG		BMA150_CTRL_REG

#define BMA150_SELF_TEST1__POS		3
#define BMA150_SELF_TEST1__LEN		1
#define BMA150_SELF_TEST1__MSK		0x08
#define BMA150_SELF_TEST1__REG		BMA150_CTRL_REG

#define BMA150_EE_W__POS			4
#define BMA150_EE_W__LEN			1
#define BMA150_EE_W__MSK			0x10
#define BMA150_EE_W__REG			BMA150_CTRL_REG

#define BMA150_UPDATE_IMAGE__POS	5
#define BMA150_UPDATE_IMAGE__LEN	1
#define BMA150_UPDATE_IMAGE__MSK	0x20
#define BMA150_UPDATE_IMAGE__REG	BMA150_CTRL_REG

#define BMA150_RESET_INT__POS		6
#define BMA150_RESET_INT__LEN		1
#define BMA150_RESET_INT__MSK		0x40
#define BMA150_RESET_INT__REG		BMA150_CTRL_REG

/* LOW-G, HIGH-G settings */
#define BMA150_ENABLE_LG__POS		0
#define BMA150_ENABLE_LG__LEN		1
#define BMA150_ENABLE_LG__MSK		0x01
#define BMA150_ENABLE_LG__REG		BMA150_CONF1_REG

#define BMA150_ENABLE_HG__POS		1
#define BMA150_ENABLE_HG__LEN		1
#define BMA150_ENABLE_HG__MSK		0x02
#define BMA150_ENABLE_HG__REG		BMA150_CONF1_REG

/* LG/HG counter */
#define BMA150_COUNTER_LG__POS			2
#define BMA150_COUNTER_LG__LEN			2
#define BMA150_COUNTER_LG__MSK			0x0C
#define BMA150_COUNTER_LG__REG			BMA150_CONF1_REG

#define BMA150_COUNTER_HG__POS			4
#define BMA150_COUNTER_HG__LEN			2
#define BMA150_COUNTER_HG__MSK			0x30
#define BMA150_COUNTER_HG__REG			BMA150_CONF1_REG

/* LG/HG duration is in ms */
#define BMA150_LG_DUR__POS			0
#define BMA150_LG_DUR__LEN			8
#define BMA150_LG_DUR__MSK			0xFF
#define BMA150_LG_DUR__REG			BMA150_LG_DURATION_REG

#define BMA150_HG_DUR__POS			0
#define BMA150_HG_DUR__LEN			8
#define BMA150_HG_DUR__MSK			0xFF
#define BMA150_HG_DUR__REG			BMA150_HG_DURATION_REG

#define BMA150_LG_THRES__POS		0
#define BMA150_LG_THRES__LEN		8
#define BMA150_LG_THRES__MSK		0xFF
#define BMA150_LG_THRES__REG		BMA150_LG_THRESHOLD_REG

#define BMA150_HG_THRES__POS		0
#define BMA150_HG_THRES__LEN		8
#define BMA150_HG_THRES__MSK		0xFF
#define BMA150_HG_THRES__REG		BMA150_HG_THRESHOLD_REG

#define BMA150_LG_HYST__POS			0
#define BMA150_LG_HYST__LEN			3
#define BMA150_LG_HYST__MSK			0x07
#define BMA150_LG_HYST__REG			BMA150_HYSTERESIS_REG

#define BMA150_HG_HYST__POS			3
#define BMA150_HG_HYST__LEN			3
#define BMA150_HG_HYST__MSK			0x38
#define BMA150_HG_HYST__REG			BMA150_HYSTERESIS_REG

/* ANY MOTION and ALERT settings */
#define BMA150_EN_ANY_MOTION__POS		6
#define BMA150_EN_ANY_MOTION__LEN		1
#define BMA150_EN_ANY_MOTION__MSK		0x40
#define BMA150_EN_ANY_MOTION__REG		BMA150_CONF1_REG

/* ALERT settings */
#define BMA150_ALERT__POS			7
#define BMA150_ALERT__LEN			1
#define BMA150_ALERT__MSK			0x80
#define BMA150_ALERT__REG			BMA150_CONF1_REG

/* ANY MOTION Duration */
#define BMA150_ANY_MOTION_THRES__POS	0
#define BMA150_ANY_MOTION_THRES__LEN	8
#define BMA150_ANY_MOTION_THRES__MSK	0xFF
#define BMA150_ANY_MOTION_THRES__REG	BMA150_MOTION_THRS_REG

#define BMA150_ANY_MOTION_DUR__POS		6
#define BMA150_ANY_MOTION_DUR__LEN		2
#define BMA150_ANY_MOTION_DUR__MSK		0xC0
#define BMA150_ANY_MOTION_DUR__REG		BMA150_HYSTERESIS_REG

#define BMA150_CUSTOMER_RESERVED1__POS		0
#define BMA150_CUSTOMER_RESERVED1__LEN	 	8
#define BMA150_CUSTOMER_RESERVED1__MSK		0xFF
#define BMA150_CUSTOMER_RESERVED1__REG		BMA150_CUSTOMER1_REG

#define BMA150_CUSTOMER_RESERVED2__POS		0
#define BMA150_CUSTOMER_RESERVED2__LEN	 	8
#define BMA150_CUSTOMER_RESERVED2__MSK		0xFF
#define BMA150_CUSTOMER_RESERVED2__REG		BMA150_CUSTOMER2_REG

/* BANDWIDTH dependend definitions */
#define BMA150_BANDWIDTH__POS				0
#define BMA150_BANDWIDTH__LEN			 	3
#define BMA150_BANDWIDTH__MSK			 	0x07
#define BMA150_BANDWIDTH__REG				BMA150_RANGE_BWIDTH_REG

/* RANGE */
#define BMA150_RANGE__POS				3
#define BMA150_RANGE__LEN				2
#define BMA150_RANGE__MSK				0x18
#define BMA150_RANGE__REG				BMA150_RANGE_BWIDTH_REG

/* WAKE UP */
#define BMA150_WAKE_UP__POS			0
#define BMA150_WAKE_UP__LEN			1
#define BMA150_WAKE_UP__MSK			0x01
#define BMA150_WAKE_UP__REG			BMA150_CONF2_REG

#define BMA150_WAKE_UP_PAUSE__POS		1
#define BMA150_WAKE_UP_PAUSE__LEN		2
#define BMA150_WAKE_UP_PAUSE__MSK		0x06
#define BMA150_WAKE_UP_PAUSE__REG		BMA150_CONF2_REG

/* ACCELERATION DATA SHADOW */
#define BMA150_SHADOW_DIS__POS			3
#define BMA150_SHADOW_DIS__LEN			1
#define BMA150_SHADOW_DIS__MSK			0x08
#define BMA150_SHADOW_DIS__REG			BMA150_CONF2_REG

/* LATCH Interrupt */
#define BMA150_LATCH_INT__POS			4
#define BMA150_LATCH_INT__LEN			1
#define BMA150_LATCH_INT__MSK			0x10
#define BMA150_LATCH_INT__REG			BMA150_CONF2_REG

/* new data interrupt */
#define BMA150_NEW_DATA_INT__POS		5
#define BMA150_NEW_DATA_INT__LEN		1
#define BMA150_NEW_DATA_INT__MSK		0x20
#define BMA150_NEW_DATA_INT__REG		BMA150_CONF2_REG

#define BMA150_ENABLE_ADV_INT__POS		6
#define BMA150_ENABLE_ADV_INT__LEN		1
#define BMA150_ENABLE_ADV_INT__MSK		0x40
#define BMA150_ENABLE_ADV_INT__REG		BMA150_CONF2_REG

#define BMA150_BMA150_SPI4_OFF	0
#define BMA150_BMA150_SPI4_ON	1

#define BMA150_SPI4__POS				7
#define BMA150_SPI4__LEN				1
#define BMA150_SPI4__MSK				0x80
#define BMA150_SPI4__REG				BMA150_CONF2_REG

#define BMA150_OFFSET_X_LSB__POS	6
#define BMA150_OFFSET_X_LSB__LEN	2
#define BMA150_OFFSET_X_LSB__MSK	0xC0
#define BMA150_OFFSET_X_LSB__REG	BMA150_OFFS_GAIN_X_REG

#define BMA150_GAIN_X__POS			0
#define BMA150_GAIN_X__LEN			6
#define BMA150_GAIN_X__MSK			0x3f
#define BMA150_GAIN_X__REG			BMA150_OFFS_GAIN_X_REG

#define BMA150_OFFSET_Y_LSB__POS	6
#define BMA150_OFFSET_Y_LSB__LEN	2
#define BMA150_OFFSET_Y_LSB__MSK	0xC0
#define BMA150_OFFSET_Y_LSB__REG	BMA150_OFFS_GAIN_Y_REG

#define BMA150_GAIN_Y__POS			0
#define BMA150_GAIN_Y__LEN			6
#define BMA150_GAIN_Y__MSK			0x3f
#define BMA150_GAIN_Y__REG			BMA150_OFFS_GAIN_Y_REG

#define BMA150_OFFSET_Z_LSB__POS	6
#define BMA150_OFFSET_Z_LSB__LEN	2
#define BMA150_OFFSET_Z_LSB__MSK	0xC0
#define BMA150_OFFSET_Z_LSB__REG	BMA150_OFFS_GAIN_Z_REG

#define BMA150_GAIN_Z__POS			0
#define BMA150_GAIN_Z__LEN			6
#define BMA150_GAIN_Z__MSK			0x3f
#define BMA150_GAIN_Z__REG			BMA150_OFFS_GAIN_Z_REG

#define BMA150_OFFSET_T_LSB__POS	6
#define BMA150_OFFSET_T_LSB__LEN	2
#define BMA150_OFFSET_T_LSB__MSK	0xC0
#define BMA150_OFFSET_T_LSB__REG	BMA150_OFFS_GAIN_T_REG

#define BMA150_GAIN_T__POS			0
#define BMA150_GAIN_T__LEN			6
#define BMA150_GAIN_T__MSK			0x3f
#define BMA150_GAIN_T__REG			BMA150_OFFS_GAIN_T_REG

#define BMA150_OFFSET_X_MSB__POS	0
#define BMA150_OFFSET_X_MSB__LEN	8
#define BMA150_OFFSET_X_MSB__MSK	0xFF
#define BMA150_OFFSET_X_MSB__REG	BMA150_OFFSET_X_REG

#define BMA150_OFFSET_Y_MSB__POS	0
#define BMA150_OFFSET_Y_MSB__LEN	8
#define BMA150_OFFSET_Y_MSB__MSK	0xFF
#define BMA150_OFFSET_Y_MSB__REG	BMA150_OFFSET_Y_REG

#define BMA150_OFFSET_Z_MSB__POS	0
#define BMA150_OFFSET_Z_MSB__LEN	8
#define BMA150_OFFSET_Z_MSB__MSK	0xFF
#define BMA150_OFFSET_Z_MSB__REG	BMA150_OFFSET_Z_REG

#define BMA150_OFFSET_T_MSB__POS	0
#define BMA150_OFFSET_T_MSB__LEN	8
#define BMA150_OFFSET_T_MSB__MSK	0xFF
#define BMA150_OFFSET_T_MSB__REG	BMA150_OFFSET_T_REG

#define BMA150_GET_BITSLICE(regvar, bitname)\
			(regvar & bitname##__MSK) >> bitname##__POS

#define BMA150_SET_BITSLICE(regvar, bitname, val)\
		  (regvar & ~bitname##__MSK) | ((val<<bitname##__POS)&bitname##__MSK)

/** \endcond */

/* CONSTANTS */

/* range and bandwidth */
#define BMA150_RANGE_2G			0 /**< sets range to 2G mode \see bma150_set_range() */
#define BMA150_RANGE_4G			1 /**< sets range to 4G mode \see bma150_set_range() */
#define BMA150_RANGE_8G			2 /**< sets range to 8G mode \see bma150_set_range() */

#define BMA150_BW_25HZ		0	/**< sets bandwidth to 25HZ \see bma150_set_bandwidth() */
#define BMA150_BW_50HZ		1	/**< sets bandwidth to 50HZ \see bma150_set_bandwidth() */
#define BMA150_BW_100HZ		2	/**< sets bandwidth to 100HZ \see bma150_set_bandwidth() */
#define BMA150_BW_190HZ		3	/**< sets bandwidth to 190HZ \see bma150_set_bandwidth() */
#define BMA150_BW_375HZ		4	/**< sets bandwidth to 375HZ \see bma150_set_bandwidth() */
#define BMA150_BW_750HZ		5	/**< sets bandwidth to 750HZ \see bma150_set_bandwidth() */
#define BMA150_BW_1500HZ	6	/**< sets bandwidth to 1500HZ \see bma150_set_bandwidth() */

/* mode settings */
#define BMA150_MODE_NORMAL      0
#define BMA150_MODE_SLEEP       2
#define BMA150_MODE_WAKE_UP     3

/* wake up */
#define BMA150_WAKE_UP_PAUSE_20MS		0
#define BMA150_WAKE_UP_PAUSE_80MS		1
#define BMA150_WAKE_UP_PAUSE_320MS		2
#define BMA150_WAKE_UP_PAUSE_2560MS		3

/* LG/HG thresholds are in LSB and depend on RANGE setting */
/* no range check on threshold calculation */

#define BMA150_SELF_TEST0_ON		1
#define BMA150_SELF_TEST1_ON		2

#define BMA150_EE_W_OFF			0
#define BMA150_EE_W_ON			1

/* low-g, high-g, any_motion */
#define BMA150_COUNTER_LG_RST		0
#define BMA150_COUNTER_LG_0LSB		BMA150_COUNTER_LG_RST
#define BMA150_COUNTER_LG_1LSB		1
#define BMA150_COUNTER_LG_2LSB		2
#define BMA150_COUNTER_LG_3LSB		3

#define BMA150_COUNTER_HG_RST		0
#define BMA150_COUNTER_HG_0LSB		BMA150_COUNTER_HG_RST
#define BMA150_COUNTER_HG_1LSB		1
#define BMA150_COUNTER_HG_2LSB		2
#define BMA150_COUNTER_HG_3LSB		3

#define BMA150_COUNTER_RST			0
#define BMA150_COUNTER_0LSB			BMA150_COUNTER_RST
#define BMA150_COUNTER_1LSB			1
#define BMA150_COUNTER_2LSB			2
#define BMA150_COUNTER_3LSB			3

#define BMA150_ANY_MOTION_DUR_1		0
#define BMA150_ANY_MOTION_DUR_3		1
#define BMA150_ANY_MOTION_DUR_5		2
#define BMA150_ANY_MOTION_DUR_7		3

#define BMA150_SHADOW_DIS_OFF	0
#define BMA150_SHADOW_DIS_ON	1

#define BMA150_LATCH_INT_OFF	0
#define BMA150_LATCH_INT_ON		1

#define BMA150_NEW_DATA_INT_OFF	0
#define BMA150_NEW_DATA_INT_ON	1

#define BMA150_ENABLE_ADV_INT_OFF	0
#define BMA150_ENABLE_ADV_INT_ON	1

#define BMA150_EN_ANY_MOTION_OFF 	0
#define BMA150_EN_ANY_MOTION_ON 	1

#define BMA150_ALERT_OFF	0
#define BMA150_ALERT_ON		1

#define BMA150_ENABLE_LG_OFF	0
#define BMA150_ENABLE_LG_ON		1

#define BMA150_ENABLE_HG_OFF	0
#define BMA150_ENABLE_HG_ON		1

#define BMA150_INT_ALERT		(1<<7)
#define BMA150_INT_ANY_MOTION	(1<<6)
#define BMA150_INT_EN_ADV_INT	(1<<5)
#define BMA150_INT_NEW_DATA		(1<<4)
#define BMA150_INT_LATCH		(1<<3)
#define BMA150_INT_HG			(1<<1)
#define BMA150_INT_LG			(1<<0)

#define BMA150_INT_STATUS_HG			(1<<0)
#define BMA150_INT_STATUS_LG			(1<<1)
#define BMA150_INT_STATUS_HG_LATCHED	(1<<2)
#define BMA150_INT_STATUS_LG_LATCHED	(1<<3)
#define BMA150_INT_STATUS_ALERT			(1<<4)
#define BMA150_INT_STATUS_ST_RESULT		(1<<7)

#define BMA150_CONF1_INT_MSK	((1<<BMA150_ALERT__POS) | (1<<BMA150_EN_ANY_MOTION__POS) | (1<<BMA150_ENABLE_HG__POS) | (1<<BMA150_ENABLE_LG__POS))
#define BMA150_CONF2_INT_MSK	((1<<BMA150_ENABLE_ADV_INT__POS) | (1<<BMA150_NEW_DATA_INT__POS) | (1<<BMA150_LATCH_INT__POS))

/*	i2c write routine for bma150	*/
static int bma150_i2c_write(struct bma150ctx *ctx, unsigned char reg_addr, unsigned char *data, unsigned char len)
{
	s32 dummy;
	unsigned char buffer[2];

	while (len--) {
		buffer[0] = reg_addr;
		buffer[1] = *data;
		dummy = i2c_master_send(ctx->client, (char *)buffer, 2);
		reg_addr++;
		data++;
		if (dummy < 0)
			return dummy;
	}
	return 0;
}

/*	i2c read routine for bma150	*/
static int bma150_i2c_read(struct bma150ctx *ctx, unsigned char reg_addr, unsigned char *data, unsigned char len)
{
	s32 dummy;

	reg_addr |= BMA150_SPI_RD_MASK;

	while (len--) {
		dummy = i2c_master_send(ctx->client, (char *)&reg_addr, 1);
		if (dummy < 0)
			return dummy;
		dummy = i2c_master_recv(ctx->client, (char *)data, 1);
		if (dummy < 0)
			return dummy;
		reg_addr++;
		data++;
	}
	return 0;
}

/** API Initialization routine
 \param *bma150 pointer to BMA150 structured type
 \return result of communication routines 
 */

static int bma150_init(struct bma150ctx *ctx)
{
	int comres = 0;
	unsigned char data;

	if ((comres = bma150_i2c_read(ctx, BMA150_CHIP_ID__REG, &data, 1)))	/* read Chip Id */
		return comres;

	ctx->chip_id = BMA150_GET_BITSLICE(data, BMA150_CHIP_ID);	/* get bitslice */

	if ((comres = bma150_i2c_read(ctx, BMA150_ML_VERSION__REG, &data, 1)))	/* read Version reg */
		return comres;

	ctx->ml_version = BMA150_GET_BITSLICE(data, BMA150_ML_VERSION);	/* get ML Version */
	ctx->al_version = BMA150_GET_BITSLICE(data, BMA150_AL_VERSION);	/* get AL Version */

	/* If device not found, say so now */
	return (ctx->chip_id == 0x02) ? 0 : -ENODEV;

}

/** Perform soft reset of BMA150 via bus command
*/
static int bma150_soft_reset(struct bma150ctx *ctx)
{
	unsigned char data = 0;
	data = BMA150_SET_BITSLICE(data, BMA150_SOFT_RESET, 1);
	return bma150_i2c_write(ctx, BMA150_SOFT_RESET__REG, &data, 1);
}

/** read out offset data from 
   \param xyz select axis x=0, y=1, z=2
   \param *offset pointer to offset value (offset is in offset binary representation
   \return result of bus communication function
   \note use bma150_set_ee_w() function to enable access to offset registers 
*/
static int bma150_get_offset(struct bma150ctx *ctx, unsigned char xyz, unsigned short *offset)
{

	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, (BMA150_OFFSET_X_LSB__REG + xyz), &data, 1)))
		return comres;
	data = BMA150_GET_BITSLICE(data, BMA150_OFFSET_X_LSB);
	*offset = data;
	if ((comres = bma150_i2c_read(ctx, (BMA150_OFFSET_X_MSB__REG + xyz), &data, 1)))
		return comres;
	*offset |= (data << 2);
	return comres;
}

/** write offset data to BMA150 image
   \param xyz select axis x=0, y=1, z=2
   \param offset value to write (offset is in offset binary representation
   \return result of bus communication function
   \note use bma150_set_ee_w() function to enable access to offset registers 
*/
static int bma150_set_offset(struct bma150ctx *ctx, unsigned char xyz, unsigned short offset)
{

	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, (BMA150_OFFSET_X_LSB__REG + xyz), &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_OFFSET_X_LSB, offset);
	if ((comres = bma150_i2c_write(ctx, (BMA150_OFFSET_X_LSB__REG + xyz), &data, 1)))
		return comres;
	data = (offset & 0x3ff) >> 2;
	return bma150_i2c_write(ctx, (BMA150_OFFSET_X_MSB__REG + xyz), &data, 1);
}

/** write offset data to BMA150 image
   \param xyz select axis x=0, y=1, z=2
   \param offset value to write to eeprom(offset is in offset binary representation
   \return result of bus communication function
   \note use bma150_set_ee_w() function to enable access to offset registers in EEPROM space
*/
static int bma150_set_offset_eeprom(struct bma150ctx *ctx, unsigned char xyz, unsigned short offset)
{

	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, (BMA150_OFFSET_X_LSB__REG + xyz), &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_OFFSET_X_LSB, offset);
	if ((comres = bma150_i2c_write(ctx, (BMA150_EEP_OFFSET + BMA150_OFFSET_X_LSB__REG + xyz), &data, 1)))
		return comres;
	msleep(34);
	data = (offset & 0x3ff) >> 2;
	if ((comres = bma150_i2c_write(ctx, (BMA150_EEP_OFFSET + BMA150_OFFSET_X_MSB__REG + xyz), &data, 1)))
		return comres;
	msleep(34);
	return 0;
}

/** write offset data to BMA150 image
   \param eew 0 = lock EEPROM 1 = unlock EEPROM 
   \return result of bus communication function
*/
static int bma150_set_ee_w(struct bma150ctx *ctx, unsigned char eew)
{
	unsigned char data;
	int comres;
	if ((comres = bma150_i2c_read(ctx, BMA150_EE_W__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_EE_W, eew);
	return bma150_i2c_write(ctx, BMA150_EE_W__REG, &data, 1);
}

/**	set bma150s range 
 \param range 
 \return  result of bus communication function
 
 \see BMA150_RANGE_2G		
 \see BMA150_RANGE_4G			
 \see BMA150_RANGE_8G			
*/
static int bma150_set_range(struct bma150ctx *ctx, char range)
{
	int comres = 0;
	unsigned char data;

	if (range < 3) {
		if ((comres = bma150_i2c_read(ctx, BMA150_RANGE__REG, &data, 1)))
			return comres;
		data = BMA150_SET_BITSLICE(data, BMA150_RANGE, range);
		if ((comres = bma150_i2c_write(ctx, BMA150_RANGE__REG, &data, 1)))
			return comres;
	}
	return 0;
}

/* readout select range from BMA150 
   \param *range pointer to range setting
   \return result of bus communication function
   \see BMA150_RANGE_2G, BMA150_RANGE_4G, BMA150_RANGE_8G		
   \see bma150_set_range()
*/
static int bma150_get_range(struct bma150ctx *ctx, unsigned char *range)
{
	int comres = 0;
	if ((comres = bma150_i2c_read(ctx, BMA150_RANGE__REG, range, 1)))
		return comres;
	*range = BMA150_GET_BITSLICE(*range, BMA150_RANGE);
	return 0;
}

/** set BMA150s operation mode
   \param mode 0 = normal, 2 = sleep, 3 = auto wake up
   \return result of bus communication function
   \note Available constants see below
   \see BMA150_MODE_NORMAL, BMA150_MODE_SLEEP, BMA150_MODE_WAKE_UP     
	 \see bma150_get_mode()
*/
static int bma150_set_mode(struct bma150ctx *ctx, unsigned char mode)
{

	int comres = 0;
	unsigned char data1, data2;

	if (mode < 4 && mode != 1) {
		if ((comres = bma150_i2c_read(ctx, BMA150_WAKE_UP__REG, &data1, 1)))
			return comres;
		data1 = BMA150_SET_BITSLICE(data1, BMA150_WAKE_UP, mode);
		if ((comres = bma150_i2c_read(ctx, BMA150_SLEEP__REG, &data2, 1)))
			return comres;
		data2 = BMA150_SET_BITSLICE(data2, BMA150_SLEEP, (mode >> 1));
		if ((comres = bma150_i2c_write(ctx, BMA150_WAKE_UP__REG, &data1, 1)))
			return comres;
		if ((comres = bma150_i2c_write(ctx, BMA150_SLEEP__REG, &data2, 1)))
			return comres;
		ctx->mode = mode;
	}
	return 0;
}

/** get selected mode
   \return used mode
   \note this function returns the mode stored in \ref bma150_t structure
   \see BMA150_MODE_NORMAL, BMA150_MODE_SLEEP, BMA150_MODE_WAKE_UP
   \see bma150_set_mode()
*/
static int bma150_get_mode(struct bma150ctx *ctx, unsigned char *mode)
{
	*mode = ctx->mode;
	return 0;
}

/** set BMA150 internal filter bandwidth
   \param bw bandwidth (see bandwidth constants)
   \return result of bus communication function
   \see #define BMA150_BW_25HZ, BMA150_BW_50HZ, BMA150_BW_100HZ, BMA150_BW_190HZ, BMA150_BW_375HZ, BMA150_BW_750HZ, BMA150_BW_1500HZ
   \see bma150_get_bandwidth()
*/
static int bma150_set_bandwidth(struct bma150ctx *ctx, char bw)
{
	int comres = 0;
	unsigned char data;

	if (bw < 8) {
		if ((comres = bma150_i2c_read(ctx, BMA150_BANDWIDTH__REG, &data, 1)))
			return comres;
		data = BMA150_SET_BITSLICE(data, BMA150_BANDWIDTH, bw);
		if ((comres = bma150_i2c_write(ctx, BMA150_BANDWIDTH__REG, &data, 1)))
			return comres;
	}
	return 0;
}

/** read selected bandwidth from BMA150 
 \param *bw pointer to bandwidth return value
 \return result of bus communication function
 \see #define BMA150_BW_25HZ, BMA150_BW_50HZ, BMA150_BW_100HZ, BMA150_BW_190HZ, BMA150_BW_375HZ, BMA150_BW_750HZ, BMA150_BW_1500HZ
 \see bma150_set_bandwidth()
*/
static int bma150_get_bandwidth(struct bma150ctx *ctx, unsigned char *bw)
{
	int comres = 1;
	if ((comres = bma150_i2c_read(ctx, BMA150_BANDWIDTH__REG, bw, 1)))
		return comres;

	*bw = BMA150_GET_BITSLICE(*bw, BMA150_BANDWIDTH);
	return 0;
}

/** set BMA150 auto wake up pause
  \param wup wake_up_pause parameters
	\return result of bus communication function
	\see BMA150_WAKE_UP_PAUSE_20MS, BMA150_WAKE_UP_PAUSE_80MS, BMA150_WAKE_UP_PAUSE_320MS, BMA150_WAKE_UP_PAUSE_2560MS
	\see bma150_get_wake_up_pause()
*/
static int bma150_set_wake_up_pause(struct bma150ctx *ctx, unsigned char wup)
{
	int comres = 0;
	unsigned char data;

	if ((comres = bma150_i2c_read(ctx, BMA150_WAKE_UP_PAUSE__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_WAKE_UP_PAUSE, wup);
	return bma150_i2c_write(ctx, BMA150_WAKE_UP_PAUSE__REG, &data, 1);
}

/** read BMA150 auto wake up pause from image
  \param *wup wake up pause read back pointer
	\see BMA150_WAKE_UP_PAUSE_20MS, BMA150_WAKE_UP_PAUSE_80MS, BMA150_WAKE_UP_PAUSE_320MS, BMA150_WAKE_UP_PAUSE_2560MS
	\see bma150_set_wake_up_pause()
*/
static int bma150_get_wake_up_pause(struct bma150ctx *ctx, unsigned char *wup)
{
	int comres = 1;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_WAKE_UP_PAUSE__REG, &data, 1)))
		return comres;

	*wup = BMA150_GET_BITSLICE(data, BMA150_WAKE_UP_PAUSE);
	return 0;
}

/* Thresholds and Interrupt Configuration */

/** set low-g interrupt threshold
   \param th set the threshold
   \note the threshold depends on configured range.
   \see bma150_get_low_g_threshold()
*/
static int bma150_set_low_g_threshold(struct bma150ctx *ctx, unsigned char th)
{
	return bma150_i2c_write(ctx, BMA150_LG_THRES__REG, &th, 1);
}

/** get low-g interrupt threshold
   \param *th get the threshold  value from sensor image
   \see bma150_set_low_g_threshold()
*/
static int bma150_get_low_g_threshold(struct bma150ctx *ctx, unsigned char *th)
{
	return bma150_i2c_read(ctx, BMA150_LG_THRES__REG, th, 1);
}

/** set low-g interrupt countdown
   \param cnt get the countdown value from sensor image
   \see bma150_get_low_g_countdown()
*/
static int bma150_set_low_g_countdown(struct bma150ctx *ctx, unsigned char cnt)
{
	int comres = 0;
	unsigned char data;

	if ((comres = bma150_i2c_read(ctx, BMA150_COUNTER_LG__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_COUNTER_LG, cnt);
	return bma150_i2c_write(ctx, BMA150_COUNTER_LG__REG, &data, 1);
}

/** set low-g hysteresis 
   \param hyst sets the hysteresis value    
*/
static int bma150_set_low_g_hysteresis(struct bma150ctx *ctx, unsigned char hyst)
{
	int comres = 0;
	unsigned char data;

	if ((comres = bma150_i2c_read(ctx, BMA150_LG_HYST__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_LG_HYST, hyst);
	return bma150_i2c_write(ctx, BMA150_LG_HYST__REG, &data, 1);
}

/** get low-g hysteresis value
   \param hyst gets the hysteresis value from sensor
   \see bma150_set_low_g_hysteresis()
*/
static int bma150_get_low_g_hysteresis(struct bma150ctx *ctx, unsigned char *hyst)
{
	int comres = 0;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_LG_HYST__REG, &data, 1)))
		return comres;

	*hyst = BMA150_GET_BITSLICE(data, BMA150_LG_HYST);

	return 0;
}

/** get low-g interrupt countdown
   \param cnt get the countdown  value from sensor image
   \see bma150_set_low_g_countdown()
*/
static int bma150_get_low_g_countdown(struct bma150ctx *ctx, unsigned char *cnt)
{
	int comres = 1;
	unsigned char data;

	if ((comres = bma150_i2c_read(ctx, BMA150_COUNTER_LG__REG, &data, 1)))
		return comres;

	*cnt = BMA150_GET_BITSLICE(data, BMA150_COUNTER_LG);
	return 0;
}

/** set high-g interrupt countdown
   \param cnt get the countdown value from sensor image
   \see bma150_get_high_g_countdown()
*/
static int bma150_set_high_g_countdown(struct bma150ctx *ctx, unsigned char cnt)
{
	int comres = 1;
	unsigned char data;

	if ((comres = bma150_i2c_read(ctx, BMA150_COUNTER_HG__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_COUNTER_HG, cnt);
	return bma150_i2c_write(ctx, BMA150_COUNTER_HG__REG, &data, 1);
}

/** get high-g interrupt countdown
   \param cnt get the countdown  value from sensor image
   \see bma150_set_high_g_countdown()
*/
static int bma150_get_high_g_countdown(struct bma150ctx *ctx, unsigned char *cnt)
{
	int comres = 0;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_COUNTER_HG__REG, &data, 1)))
		return comres;

	*cnt = BMA150_GET_BITSLICE(data, BMA150_COUNTER_HG);

	return 0;
}

/** configure low-g duration value
	\param dur low-g duration in miliseconds
	\see bma150_get_low_g_duration(), bma150_get_high_g_duration(), bma150_set_high_g_duration()
*/
static int bma150_set_low_g_duration(struct bma150ctx *ctx, unsigned char dur)
{
	return bma150_i2c_write(ctx, BMA150_LG_DUR__REG, &dur, 1);
}

/** set high-g hysteresis 
   \param hyst sets the hysteresis value    
*/
static int bma150_set_high_g_hysteresis(struct bma150ctx *ctx, unsigned char hyst)
{
	int comres = 0;
	unsigned char data;

	if ((comres = bma150_i2c_read(ctx, BMA150_HG_HYST__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_HG_HYST, hyst);
	return bma150_i2c_write(ctx, BMA150_HG_HYST__REG, &data, 1);
}

/** get high-g hysteresis value
   \param hyst gets the hysteresis value from sensor
   \see bma150_set_high_g_hysteresis()
*/
static int bma150_get_high_g_hysteresis(struct bma150ctx *ctx, unsigned char *hyst)
{
	int comres = 0;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_HG_HYST__REG, &data, 1)))
		return comres;

	*hyst = BMA150_GET_BITSLICE(data, BMA150_HG_HYST);
	return 0;
}

/** read out low-g duration value from sensor image
	\param dur low-g duration in miliseconds
	\see bma150_set_low_g_duration(), bma150_get_high_g_duration(), bma150_set_high_g_duration()
*/
static int bma150_get_low_g_duration(struct bma150ctx *ctx, unsigned char *dur)
{
	return bma150_i2c_read(ctx, BMA150_LG_DUR__REG, dur, 1);
}

/** set low-g interrupt threshold
   \param th set the threshold
   \note the threshold depends on configured range.
   \see bma150_get_high_g_threshold()
*/
static int bma150_set_high_g_threshold(struct bma150ctx *ctx, unsigned char th)
{
	return bma150_i2c_write(ctx, BMA150_HG_THRES__REG, &th, 1);
}

/** get high-g interrupt threshold
   \param *th get the threshold  value from sensor image
   \see bma150_set_high_g_threshold()
*/
static int bma150_get_high_g_threshold(struct bma150ctx *ctx, unsigned char *th)
{
	return bma150_i2c_read(ctx, BMA150_HG_THRES__REG, th, 1);
}

/** configure high-g duration value
	\param dur high-g duration in miliseconds
	\see  bma150_get_high_g_duration(), bma150_set_low_g_duration(), bma150_get_low_g_duration()
*/
static int bma150_set_high_g_duration(struct bma150ctx *ctx, unsigned char dur)
{
	return bma150_i2c_write(ctx, BMA150_HG_DUR__REG, &dur, 1);
}

/** read out high-g duration value from sensor image
	\param dur high-g duration in miliseconds
	\see  bma150_set_high_g_duration(), bma150_get_low_g_duration(), bma150_set_low_g_duration(),
*/
static int bma150_get_high_g_duration(struct bma150ctx *ctx, unsigned char *dur)
{
	return bma150_i2c_read(ctx, BMA150_HG_DUR__REG, dur, 1);
}

/**  set threshold value for any_motion feature
		\param th set the threshold
*/
static int bma150_set_any_motion_threshold(struct bma150ctx *ctx, unsigned char th)
{
	return bma150_i2c_write(ctx, BMA150_ANY_MOTION_THRES__REG, &th, 1);
}

/**  get threshold value for any_motion feature
		\param *th read back any_motion threshold from image register 
*/
static int bma150_get_any_motion_threshold(struct bma150ctx *ctx, unsigned char *th)
{
	return bma150_i2c_read(ctx, BMA150_ANY_MOTION_THRES__REG, th, 1);
}

/**  set counter value for any_motion feature 
		\param amc set the counter value, constants are available for that
		\see BMA150_ANY_MOTION_DUR_1, BMA150_ANY_MOTION_DUR_3, BMA150_ANY_MOTION_DUR_5, BMA150_ANY_MOTION_DUR_7
*/
static int bma150_set_any_motion_count(struct bma150ctx *ctx, unsigned char amc)
{
	int comres = 0;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_ANY_MOTION_DUR__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_ANY_MOTION_DUR, amc);
	return bma150_i2c_write(ctx, BMA150_ANY_MOTION_DUR__REG, &data, 1);
}

/**  get counter value for any_motion feature from image register
		\param *amc readback pointer for counter value
		\see BMA150_ANY_MOTION_DUR_1, BMA150_ANY_MOTION_DUR_3, BMA150_ANY_MOTION_DUR_5, BMA150_ANY_MOTION_DUR_7
*/
static int bma150_get_any_motion_count(struct bma150ctx *ctx, unsigned char *amc)
{
	int comres = 0;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_ANY_MOTION_DUR__REG, &data, 1)))
		return comres;
	*amc = BMA150_GET_BITSLICE(data, BMA150_ANY_MOTION_DUR);
	return 0;
}

/** set the interrupt mask for BMA150's interrupt features in one mask
	\param mask input for interrupt mask
	\see BMA150_INT_ALERT, BMA150_INT_ANY_MOTION, BMA150_INT_EN_ADV_INT, BMA150_INT_NEW_DATA, BMA150_INT_LATCH, BMA150_INT_HG, BMA150_INT_LG
*/
static int bma150_set_interrupt_mask(struct bma150ctx *ctx, unsigned char mask)
{
	int comres = 0;
	unsigned char data[4];

	data[0] = mask & BMA150_CONF1_INT_MSK;
	data[2] = ((mask << 1) & BMA150_CONF2_INT_MSK);

	if ((comres = bma150_i2c_read(ctx, BMA150_CONF1_REG, &data[1], 1)))
		return comres;
	if ((comres = bma150_i2c_read(ctx, BMA150_CONF2_REG, &data[3], 1)))
		return comres;

	data[1] &= (~BMA150_CONF1_INT_MSK);
	data[1] |= data[0];
	data[3] &= (~(BMA150_CONF2_INT_MSK));
	data[3] |= data[2];

	if ((comres = bma150_i2c_write(ctx, BMA150_CONF1_REG, &data[1], 1)))
		return comres;
	return bma150_i2c_write(ctx, BMA150_CONF2_REG, &data[3], 1);
}

/** get the current interrupt mask settings from BMA150 image registers
	\param *mask return variable pointer for interrupt mask
	\see BMA150_INT_ALERT, BMA150_INT_ANY_MOTION, BMA150_INT_EN_ADV_INT, BMA150_INT_NEW_DATA, BMA150_INT_LATCH, BMA150_INT_HG, BMA150_INT_LG
*/
static int bma150_get_interrupt_mask(struct bma150ctx *ctx, unsigned char *mask)
{
	int comres = 0;
	unsigned char data;

	if ((comres = bma150_i2c_read(ctx, BMA150_CONF1_REG, &data, 1)))
		return comres;
	*mask = data & BMA150_CONF1_INT_MSK;
	if ((comres = bma150_i2c_read(ctx, BMA150_CONF2_REG, &data, 1)))
		return comres;
	*mask = *mask | ((data & BMA150_CONF2_INT_MSK) >> 1);
	return 0;
}

/** resets the BMA150 interrupt status 
		\note this feature can be used to reset a latched interrupt
*/
static int bma150_reset_interrupt(struct bma150ctx *ctx)
{
	unsigned char data = (1 << BMA150_RESET_INT__POS);
	return bma150_i2c_write(ctx, BMA150_RESET_INT__REG, &data, 1);
}

/* Data Readout */

/** X-axis acceleration data readout 
	\param *a_x pointer for 16 bit 2's complement data output (LSB aligned)
*/
static int bma150_read_accel_x(struct bma150ctx *ctx, short *a_x)
{
	int comres;
	unsigned char data[2];
	if ((comres = bma150_i2c_read(ctx, BMA150_ACC_X_LSB__REG, data, 2)))
		return comres;
	*a_x =
	    BMA150_GET_BITSLICE(data[0], BMA150_ACC_X_LSB) | BMA150_GET_BITSLICE(data[1],
										 BMA150_ACC_X_MSB) <<
	    BMA150_ACC_X_LSB__LEN;
	*a_x = *a_x << (sizeof(short) * 8 - (BMA150_ACC_X_LSB__LEN + BMA150_ACC_X_MSB__LEN));
	*a_x = *a_x >> (sizeof(short) * 8 - (BMA150_ACC_X_LSB__LEN + BMA150_ACC_X_MSB__LEN));
	return 0;
}

/** Y-axis acceleration data readout 
	\param *a_y pointer for 16 bit 2's complement data output (LSB aligned)
*/
static int bma150_read_accel_y(struct bma150ctx *ctx, short *a_y)
{
	int comres;
	unsigned char data[2];
	if ((comres = bma150_i2c_read(ctx, BMA150_ACC_Y_LSB__REG, data, 2)))
		return comres;
	*a_y =
	    BMA150_GET_BITSLICE(data[0], BMA150_ACC_Y_LSB) | BMA150_GET_BITSLICE(data[1],
										 BMA150_ACC_Y_MSB) <<
	    BMA150_ACC_Y_LSB__LEN;
	*a_y = *a_y << (sizeof(short) * 8 - (BMA150_ACC_Y_LSB__LEN + BMA150_ACC_Y_MSB__LEN));
	*a_y = *a_y >> (sizeof(short) * 8 - (BMA150_ACC_Y_LSB__LEN + BMA150_ACC_Y_MSB__LEN));
	return 0;
}

/** Z-axis acceleration data readout 
	\param *a_z pointer for 16 bit 2's complement data output (LSB aligned)
*/
static int bma150_read_accel_z(struct bma150ctx *ctx, short *a_z)
{
	int comres;
	unsigned char data[2];
	if ((comres = bma150_i2c_read(ctx, BMA150_ACC_Z_LSB__REG, data, 2)))
		return comres;
	*a_z =
	    BMA150_GET_BITSLICE(data[0], BMA150_ACC_Z_LSB) | BMA150_GET_BITSLICE(data[1],
										 BMA150_ACC_Z_MSB) <<
	    BMA150_ACC_Z_LSB__LEN;
	*a_z = *a_z << (sizeof(short) * 8 - (BMA150_ACC_Z_LSB__LEN + BMA150_ACC_Z_MSB__LEN));
	*a_z = *a_z >> (sizeof(short) * 8 - (BMA150_ACC_Z_LSB__LEN + BMA150_ACC_Z_MSB__LEN));
	return 0;
}

/** 8 bit temperature data readout 
	\param *temp pointer for 8 bit temperature output (offset binary)
	\note: an output of 0 equals -30°C, 1 LSB equals 0.5°C
*/
static int bma150_read_temperature(struct bma150ctx *ctx, unsigned char *temp)
{
	return bma150_i2c_read(ctx, BMA150_TEMPERATURE__REG, temp, 1);
}

/** X,Y and Z-axis acceleration data readout 
	\param *acc pointer to \ref bma150acc_t structure for x,y,z data readout
	\note data will be read by multi-byte protocol into a 6 byte structure 
*/
static int bma150_read_accel_xyz(struct bma150ctx *ctx, bma150acc_t * acc)
{
	static int counter = 0;
	static int toggle = 1;
	int new_toggle;
	// start GSENSOR Toggle with GPIO
		if( counter % 25 == 0 ) {
		 new_toggle = gpio_get_value( TEGRA_GPIO_GSENSOR_TOGGLE );
		 if(new_toggle == 0)
			{
			new_toggle = 1 ;
			} else {
			new_toggle = 0 ;
			}
			if( toggle != new_toggle ) {
			toggle = new_toggle;
			printk(KERN_INFO "[SHUTTLE] GPIO:%d = %d Set GSENSOR MODE: [%s]\n",
			TEGRA_GPIO_GSENSOR_TOGGLE, toggle, toggle == 0 ? "sleep" : "wake-->normal");
			bma150_set_mode( ctx, BMA150_MODE_SLEEP );
			if( toggle == 1 ) {
			bma150_set_mode( ctx, BMA150_MODE_NORMAL );
			}
			    }
			counter = 0;
				}
	int comres;
	unsigned char data[6];

	if ((comres = bma150_i2c_read(ctx, BMA150_ACC_X_LSB__REG, &data[0], 6)))
		return comres;

	acc->x =
	    BMA150_GET_BITSLICE(data[0],
				BMA150_ACC_X_LSB) | (BMA150_GET_BITSLICE(data[1],
									 BMA150_ACC_X_MSB) << BMA150_ACC_X_LSB__LEN);
	acc->x = acc->x << (sizeof(short) * 8 - (BMA150_ACC_X_LSB__LEN + BMA150_ACC_X_MSB__LEN));
	acc->x = acc->x >> (sizeof(short) * 8 - (BMA150_ACC_X_LSB__LEN + BMA150_ACC_X_MSB__LEN));

	acc->y =
	    BMA150_GET_BITSLICE(data[2],
				BMA150_ACC_Y_LSB) | (BMA150_GET_BITSLICE(data[3],
									 BMA150_ACC_Y_MSB) << BMA150_ACC_Y_LSB__LEN);
	acc->y = acc->y << (sizeof(short) * 8 - (BMA150_ACC_Y_LSB__LEN + BMA150_ACC_Y_MSB__LEN));
	acc->y = acc->y >> (sizeof(short) * 8 - (BMA150_ACC_Y_LSB__LEN + BMA150_ACC_Y_MSB__LEN));

	acc->z = BMA150_GET_BITSLICE(data[4], BMA150_ACC_Z_LSB);
	acc->z |= (BMA150_GET_BITSLICE(data[5], BMA150_ACC_Z_MSB) << BMA150_ACC_Z_LSB__LEN);
	acc->z = acc->z << (sizeof(short) * 8 - (BMA150_ACC_Z_LSB__LEN + BMA150_ACC_Z_MSB__LEN));
	acc->z = acc->z >> (sizeof(short) * 8 - (BMA150_ACC_Z_LSB__LEN + BMA150_ACC_Z_MSB__LEN));

#ifdef xy_flip
	temp1=(acc->x * -1);
	acc->x=acc->y;
	acc->y=temp1; 
#endif
	return 0;
}

/** check current interrupt status from interrupt status register in BMA150 image register
	\param *ist pointer to interrupt status byte
	\see BMA150_INT_STATUS_HG, BMA150_INT_STATUS_LG, BMA150_INT_STATUS_HG_LATCHED, BMA150_INT_STATUS_LG_LATCHED, BMA150_INT_STATUS_ALERT, BMA150_INT_STATUS_ST_RESULT
*/
static int bma150_get_interrupt_status(struct bma150ctx *ctx, unsigned char *ist)
{
	return bma150_i2c_read(ctx, BMA150_STATUS_REG, ist, 1);
}

/** enable/ disable low-g interrupt feature
		\param onoff enable=1, disable=0
*/
static int bma150_set_low_g_int(struct bma150ctx *ctx, unsigned char onoff)
{
	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_ENABLE_LG__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_ENABLE_LG, onoff);
	return bma150_i2c_write(ctx, BMA150_ENABLE_LG__REG, &data, 1);
}

/** enable/ disable high-g interrupt feature
		\param onoff enable=1, disable=0
*/
static int bma150_set_high_g_int(struct bma150ctx *ctx, unsigned char onoff)
{
	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_ENABLE_HG__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_ENABLE_HG, onoff);
	return bma150_i2c_write(ctx, BMA150_ENABLE_HG__REG, &data, 1);
}

/** enable/ disable any_motion interrupt feature
		\param onoff enable=1, disable=0
		\note for any_motion interrupt feature usage see also \ref bma150_set_advanced_int()
*/
static int bma150_set_any_motion_int(struct bma150ctx *ctx, unsigned char onoff)
{
	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_EN_ANY_MOTION__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_EN_ANY_MOTION, onoff);
	return bma150_i2c_write(ctx, BMA150_EN_ANY_MOTION__REG, &data, 1);
}

/** enable/ disable alert-int interrupt feature
		\param onoff enable=1, disable=0
		\note for any_motion interrupt feature usage see also \ref bma150_set_advanced_int()
*/
static int bma150_set_alert_int(struct bma150ctx *ctx, unsigned char onoff)
{
	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_ALERT__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_ALERT, onoff);
	return bma150_i2c_write(ctx, BMA150_ALERT__REG, &data, 1);
}

/** enable/ disable advanced interrupt feature
		\param onoff enable=1, disable=0
		\see bma150_set_any_motion_int()
		\see bma150_set_alert_int()
*/
static int bma150_set_advanced_int(struct bma150ctx *ctx, unsigned char onoff)
{
	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_ENABLE_ADV_INT__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_ENABLE_ADV_INT, onoff);
	return bma150_i2c_write(ctx, BMA150_ENABLE_ADV_INT__REG, &data, 1);
}

/** enable/disable latched interrupt for all interrupt feature (global option)
	\param latched (=1 for latched interrupts), (=0 for unlatched interrupts) 
*/
static int bma150_latch_int(struct bma150ctx *ctx, unsigned char latched)
{
	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_LATCH_INT__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_LATCH_INT, latched);
	return bma150_i2c_write(ctx, BMA150_LATCH_INT__REG, &data, 1);
}

/** enable/ disable new data interrupt feature
		\param onoff enable=1, disable=0
*/
static int bma150_set_new_data_int(struct bma150ctx *ctx, unsigned char onoff)
{
	int comres;
	unsigned char data;
	if ((comres = bma150_i2c_read(ctx, BMA150_NEW_DATA_INT__REG, &data, 1)))
		return comres;
	data = BMA150_SET_BITSLICE(data, BMA150_NEW_DATA_INT, onoff);
	return bma150_i2c_write(ctx, BMA150_NEW_DATA_INT__REG, &data, 1);
}

/* MISC functions */

/** calculates new offset in respect to acceleration data and old offset register values
  \param orientation pass orientation one axis needs to be absolute 1 the others need to be 0
  \param *offset_x takes the old offset value and modifies it to the new calculated one
  \param *offset_y takes the old offset value and modifies it to the new calculated one
  \param *offset_z takes the old offset value and modifies it to the new calculated one
 */
static int bma150_calc_new_offset(bma150acc_t orientation, bma150acc_t accel,
				  unsigned short *offset_x, unsigned short *offset_y, unsigned short *offset_z)
{
	short old_offset_x, old_offset_y, old_offset_z;
	short new_offset_x, new_offset_y, new_offset_z;

	unsigned char calibrated = 0;

	old_offset_x = *offset_x;
	old_offset_y = *offset_y;
	old_offset_z = *offset_z;

	accel.x = accel.x - (orientation.x * 256);
	accel.y = accel.y - (orientation.y * 256);
	accel.z = accel.z - (orientation.z * 256);

	if ((accel.x > 7) | (accel.x < -7)) {	/* does x axis need calibration? */

		if ((accel.x < 8) && accel.x > 0)	/* check for values less than quantisation of offset register */
			new_offset_x = old_offset_x - 1;
		else if ((accel.x > -8) && (accel.x < 0))
			new_offset_x = old_offset_x + 1;
		else
			new_offset_x = old_offset_x - (accel.x / 8);	/* calculate new offset due to formula */
		if (new_offset_x < 0)	/* check for register boundary */
			new_offset_x = 0;	/* <0 ? */
		else if (new_offset_x > 1023)
			new_offset_x = 1023;	/* >1023 ? */
		*offset_x = new_offset_x;
		calibrated = 1;
	}

	if ((accel.y > 7) | (accel.y < -7)) {	/* does y axis need calibration? */
		if ((accel.y < 8) && accel.y > 0)	/* check for values less than quantisation of offset register */
			new_offset_y = old_offset_y - 1;
		else if ((accel.y > -8) && accel.y < 0)
			new_offset_y = old_offset_y + 1;
		else
			new_offset_y = old_offset_y - accel.y / 8;	/* calculate new offset due to formula */

		if (new_offset_y < 0)	/* check for register boundary */
			new_offset_y = 0;	/* <0 ? */
		else if (new_offset_y > 1023)
			new_offset_y = 1023;	/* >1023 ? */

		*offset_y = new_offset_y;
		calibrated = 1;
	}

	if ((accel.z > 7) | (accel.z < -7)) {	/* does z axis need calibration? */
		if ((accel.z < 8) && accel.z > 0)	/* check for values less than quantisation of offset register */
			new_offset_z = old_offset_z - 1;
		else if ((accel.z > -8) && accel.z < 0)
			new_offset_z = old_offset_z + 1;
		else
			new_offset_z = old_offset_z - (accel.z / 8);	/* calculate new offset due to formula */

		if (new_offset_z < 0)	/* check for register boundary */
			new_offset_z = 0;	/* <0 ? */
		else if (new_offset_z > 1023)
			new_offset_z = 1023;

		*offset_z = new_offset_z;
		calibrated = 1;
	}
	return calibrated;
}

/** reads out acceleration data and averages them, measures min and max
  \param orientation pass orientation one axis needs to be absolute 1 the others need to be 0
  \param num_avg numer of samples for averaging
  \param *min returns the minimum measured value
  \param *max returns the maximum measured value
  \param *average returns the average value
 */
static int bma150_read_accel_avg(struct bma150ctx *ctx, int num_avg, bma150acc_t * min, bma150acc_t * max,
				 bma150acc_t * avg)
{
	long x_avg = 0, y_avg = 0, z_avg = 0;
	int comres = 0;
	int i;
	bma150acc_t accel;	/* read accel data */

	x_avg = 0;
	y_avg = 0;
	z_avg = 0;
	max->x = -512;
	max->y = -512;
	max->z = -512;
	min->x = 512;
	min->y = 512;
	min->z = 512;

	for (i = 0; i < num_avg; i++) {
		if ((comres = bma150_read_accel_xyz(ctx, &accel)))	/* read 10 acceleration data triples */
			return comres;

		if (accel.x > max->x)
			max->x = accel.x;
		if (accel.x < min->x)
			min->x = accel.x;

		if (accel.y > max->y)
			max->y = accel.y;
		if (accel.y < min->y)
			min->y = accel.y;

		if (accel.z > max->z)
			max->z = accel.z;
		if (accel.z < min->z)
			min->z = accel.z;

		x_avg += accel.x;
		y_avg += accel.y;
		z_avg += accel.z;

		msleep(10);
	}
	avg->x = x_avg /= num_avg;	/* calculate averages, min and max values */
	avg->y = y_avg /= num_avg;
	avg->z = z_avg /= num_avg;
	return 0;
}

/** verifies the accerleration values to be good enough for calibration calculations
 \param min takes the minimum measured value
  \param max takes the maximum measured value
  \param takes returns the average value
  \return 1: min,max values are in range, 0: not in range
*/
static int bma150_verify_min_max(bma150acc_t min, bma150acc_t max, bma150acc_t avg)
{
	short dx, dy, dz;
	int ver_ok = 1;

	dx = max.x - min.x;	/* calc delta max-min */
	dy = max.y - min.y;
	dz = max.z - min.z;

	if (dx > 10 || dx < -10)
		ver_ok = 0;
	if (dy > 10 || dy < -10)
		ver_ok = 0;
	if (dz > 10 || dz < -10)
		ver_ok = 0;

	return ver_ok;
}

/** overall calibration process. This function takes care about all other functions 
  \param orientation input for orientation [0;0;1] for measuring the device in horizontal surface up
  \param *tries takes the number of wanted iteration steps, this pointer returns the number of calculated steps after this routine has finished
  \return 1: calibration passed 2: did not pass within N steps 
*/
static int bma150_calibrate(struct bma150ctx *ctx, bma150acc_t orientation, int *tries)
{
	int comres = 0;
	unsigned short offset_x, offset_y, offset_z;
	unsigned short old_offset_x, old_offset_y, old_offset_z;
	int need_calibration = 0, min_max_ok = 0;
	int ltries;

	bma150acc_t min, max, avg;

	if ((comres = bma150_set_range(ctx, BMA150_RANGE_2G)))
		return comres;
	if ((comres = bma150_set_bandwidth(ctx, BMA150_BW_25HZ)))
		return comres;

	if ((comres = bma150_set_ee_w(ctx, 1)))
		return comres;

	if ((comres = bma150_get_offset(ctx, 0, &offset_x)))
		return comres;
	if ((comres = bma150_get_offset(ctx, 1, &offset_y)))
		return comres;
	if ((comres = bma150_get_offset(ctx, 2, &offset_z)))
		return comres;

	old_offset_x = offset_x;
	old_offset_y = offset_y;
	old_offset_z = offset_z;
	ltries = *tries;

	do {

		if ((comres = bma150_read_accel_avg(ctx, 10, &min, &max, &avg)))	/* read acceleration data min, max, avg */
			return comres;

		min_max_ok = bma150_verify_min_max(min, max, avg);

		/* check if calibration is needed */
		if (min_max_ok)
			need_calibration = bma150_calc_new_offset(orientation, avg, &offset_x, &offset_y, &offset_z);

		if (*tries == 0)	/*number of maximum tries reached? */
			break;

		if (need_calibration) {
			/* when needed calibration is updated in image */
			(*tries)--;
			if ((comres = bma150_set_offset(ctx, 0, offset_x)))
				return comres;
			if ((comres = bma150_set_offset(ctx, 1, offset_y)))
				return comres;
			if ((comres = bma150_set_offset(ctx, 2, offset_z)))
				return comres;
			msleep(20);
		}
	} while (need_calibration || !min_max_ok);

	if (*tries > 0 && *tries < ltries) {

		if (old_offset_x != offset_x)
			if ((comres = bma150_set_offset_eeprom(ctx, 0, offset_x)))
				return comres;

		if (old_offset_y != offset_y)
			if ((comres = bma150_set_offset_eeprom(ctx, 1, offset_y)))
				return comres;

		if (old_offset_z != offset_z)
			if ((comres = bma150_set_offset_eeprom(ctx, 2, offset_z)))
				return comres;
	}

	if ((comres = bma150_set_ee_w(ctx, 0)))
		return comres;

	msleep(20);
	*tries = ltries - *tries;

	return !need_calibration;
}

static int bma150_device_power_on(struct bma150ctx *ctx)
{
	int comres;
	if ((comres = bma150_set_mode(ctx, BMA150_MODE_NORMAL)))
		return comres;
	if ((comres = bma150_set_range(ctx, BMA150_RANGE_2G)))
		return comres;
	if ((comres = bma150_set_bandwidth(ctx, BMA150_BW_25HZ)))
		return comres;
	msleep(20);
	return 0;
}

static int bma150_device_power_off(struct bma150ctx *ctx)
{
	return bma150_set_mode(ctx, BMA150_MODE_SLEEP);
}

#define BMA150_IOC_MAGIC 'B'

#define BMA150_SOFT_RESET				_IO(BMA150_IOC_MAGIC,0)
#define BMA150_SET_RANGE				_IOWR(BMA150_IOC_MAGIC,4, unsigned char)
#define BMA150_GET_RANGE				_IOWR(BMA150_IOC_MAGIC,5, unsigned char)
#define BMA150_SET_MODE					_IOWR(BMA150_IOC_MAGIC,6, unsigned char)
#define BMA150_GET_MODE					_IOWR(BMA150_IOC_MAGIC,7, unsigned char)
#define BMA150_SET_BANDWIDTH			_IOWR(BMA150_IOC_MAGIC,8, unsigned char)
#define BMA150_GET_BANDWIDTH			_IOWR(BMA150_IOC_MAGIC,9, unsigned char)
#define BMA150_SET_WAKE_UP_PAUSE		_IOWR(BMA150_IOC_MAGIC,10,unsigned char)
#define BMA150_GET_WAKE_UP_PAUSE		_IOWR(BMA150_IOC_MAGIC,11,unsigned char)
#define BMA150_SET_LOW_G_THRESHOLD		_IOWR(BMA150_IOC_MAGIC,12,unsigned char)
#define BMA150_GET_LOW_G_THRESHOLD		_IOWR(BMA150_IOC_MAGIC,13,unsigned char)
#define BMA150_SET_LOW_G_COUNTDOWN		_IOWR(BMA150_IOC_MAGIC,14,unsigned char)
#define BMA150_GET_LOW_G_COUNTDOWN		_IOWR(BMA150_IOC_MAGIC,15,unsigned char)
#define BMA150_SET_HIGH_G_COUNTDOWN		_IOWR(BMA150_IOC_MAGIC,16,unsigned char)
#define BMA150_GET_HIGH_G_COUNTDOWN		_IOWR(BMA150_IOC_MAGIC,17,unsigned char)
#define BMA150_SET_LOW_G_DURATION		_IOWR(BMA150_IOC_MAGIC,18,unsigned char)
#define BMA150_GET_LOW_G_DURATION		_IOWR(BMA150_IOC_MAGIC,19,unsigned char)
#define BMA150_SET_HIGH_G_THRESHOLD		_IOWR(BMA150_IOC_MAGIC,20,unsigned char)
#define BMA150_GET_HIGH_G_THRESHOLD		_IOWR(BMA150_IOC_MAGIC,21,unsigned char)
#define BMA150_SET_HIGH_G_DURATION		_IOWR(BMA150_IOC_MAGIC,22,unsigned char)
#define BMA150_GET_HIGH_G_DURATION		_IOWR(BMA150_IOC_MAGIC,23,unsigned char)
#define BMA150_SET_ANY_MOTION_THRESHOLD	_IOWR(BMA150_IOC_MAGIC,24,unsigned char)
#define BMA150_GET_ANY_MOTION_THRESHOLD	_IOWR(BMA150_IOC_MAGIC,25,unsigned char)
#define BMA150_SET_ANY_MOTION_COUNT		_IOWR(BMA150_IOC_MAGIC,26,unsigned char)
#define BMA150_GET_ANY_MOTION_COUNT		_IOWR(BMA150_IOC_MAGIC,27,unsigned char)
#define BMA150_SET_INTERRUPT_MASK		_IOWR(BMA150_IOC_MAGIC,28,unsigned char)
#define BMA150_GET_INTERRUPT_MASK		_IOWR(BMA150_IOC_MAGIC,29,unsigned char)
#define BMA150_RESET_INTERRUPT			_IO(BMA150_IOC_MAGIC,30)
#define BMA150_READ_ACCEL_X				_IOWR(BMA150_IOC_MAGIC,31,short)
#define BMA150_READ_ACCEL_Y				_IOWR(BMA150_IOC_MAGIC,32,short)
#define BMA150_READ_ACCEL_Z				_IOWR(BMA150_IOC_MAGIC,33,short)
#define BMA150_GET_INTERRUPT_STATUS		_IOWR(BMA150_IOC_MAGIC,34,unsigned char)
#define BMA150_SET_LOW_G_INT			_IOWR(BMA150_IOC_MAGIC,35,unsigned char)
#define BMA150_SET_HIGH_G_INT			_IOWR(BMA150_IOC_MAGIC,36,unsigned char)
#define BMA150_SET_ANY_MOTION_INT		_IOWR(BMA150_IOC_MAGIC,37,unsigned char)
#define BMA150_SET_ALERT_INT			_IOWR(BMA150_IOC_MAGIC,38,unsigned char)
#define BMA150_SET_ADVANCED_INT			_IOWR(BMA150_IOC_MAGIC,39,unsigned char)
#define BMA150_LATCH_INT				_IOWR(BMA150_IOC_MAGIC,40,unsigned char)
#define BMA150_SET_NEW_DATA_INT			_IOWR(BMA150_IOC_MAGIC,41,unsigned char)
#define BMA150_GET_LOW_G_HYST			_IOWR(BMA150_IOC_MAGIC,42,unsigned char)
#define BMA150_SET_LOW_G_HYST			_IOWR(BMA150_IOC_MAGIC,43,unsigned char)
#define BMA150_GET_HIGH_G_HYST			_IOWR(BMA150_IOC_MAGIC,44,unsigned char)
#define BMA150_SET_HIGH_G_HYST			_IOWR(BMA150_IOC_MAGIC,45,unsigned char)
#define BMA150_READ_ACCEL_XYZ			_IOWR(BMA150_IOC_MAGIC,46,short)
#define BMA150_READ_TEMPERATURE			_IOWR(BMA150_IOC_MAGIC,47,unsigned char)
#define BMA150_CALIBRATION				_IOWR(BMA150_IOC_MAGIC,48,short)
#define BMA150_IOC_MAXNR				50

/* read command for BMA150 device file	*/
static ssize_t bma150_read(struct file *file, char __user * buf, size_t count, loff_t * offset)
{
	struct bma150ctx *ctx = file->private_data;

	int ret;

	dev_dbg(&ctx->client->dev, "X/Y/Z axis: %-8d %-8d %-8d\n", (int)ctx->acc.x, (int)ctx->acc.y, (int)ctx->acc.z);

	if (count != sizeof(ctx->acc)) {
		return -1;
	}

	ret = copy_to_user(buf, &ctx->acc, sizeof(ctx->acc));

	if (ret != 0) {
		dev_err(&ctx->client->dev, "copy_to_user result: %d\n", ret);
	}
	return sizeof(ctx->acc);
}

/*	write command for BMA150 device file	*/
static ssize_t bma150_write(struct file *file, const char __user * buf, size_t count, loff_t * offset)
{
	struct bma150ctx *ctx = file->private_data;
	dev_dbg(&ctx->client->dev, "should be accessed with ioctl command\n");
	return 0;
}

static int bma150_enable(struct bma150ctx *ctx)
{
	int err;

	err = bma150_device_power_on(ctx);
	if (err < 0) {
		return err;
	}

	queue_delayed_work(ctx->input_work_queue, &ctx->input_work, msecs_to_jiffies(40));
	return 0;
}

static int bma150_disable(struct bma150ctx *ctx)
{
	cancel_delayed_work_sync(&ctx->input_work);
	bma150_device_power_off(ctx);
	return 0;
}

/*	open command for BMA150 device file	*/
static struct bma150ctx *g_ctx = NULL;
static int bma150_open(struct inode *inode, struct file *file)
{
	struct bma150ctx *ctx = g_ctx;
	int comres = 0;

	/* Store a pointer to our private data */
	file->private_data = ctx;

	if (atomic_inc_return(&ctx->open_cnt) == 1) {
		/* opened for the fisrt time - Enable accelerometer */
		if ((comres = bma150_enable(ctx))) {
			atomic_dec(&ctx->open_cnt);
			return comres;
		}
	}

	dev_dbg(&ctx->client->dev, "has been opened\n");
	return 0;
}

/*	release command for BMA150 device file	*/
static int bma150_close(struct inode *inode, struct file *file)
{
	int comres = 0;
	struct bma150ctx *ctx = file->private_data;

	if (atomic_dec_return(&ctx->open_cnt) == 0) {
		/* Last user, turn off accelerometer */
		if ((comres = bma150_disable(ctx))) {
			atomic_inc(&ctx->open_cnt);
			return comres;
		}
	}

	dev_dbg(&ctx->client->dev, "has been closed\n");
	return 0;
}

/*	ioctl command for BMA150 device file	*/
static long bma150_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bma150ctx *ctx = file->private_data;
	int err = 0;
	unsigned char data[6];
	int temp;

	dev_dbg(&ctx->client->dev, "%s\n", __FUNCTION__);

	/* check cmd */
	if (_IOC_TYPE(cmd) != BMA150_IOC_MAGIC) {
		dev_err(&ctx->client->dev, "cmd magic type error\n");
		return -ENOTTY;
	}
	if (_IOC_NR(cmd) > BMA150_IOC_MAXNR) {
		dev_err(&ctx->client->dev, "cmd number error\n");
		return -ENOTTY;
	}

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) {
		dev_err(&ctx->client->dev, "cmd access_ok error\n");
		return -EFAULT;
	}

	/* get exclusive access to the accelerometer */
	mutex_lock(&ctx->lock);

	/* cmd mapping */
	switch (cmd) {
	case BMA150_SOFT_RESET:
		err = bma150_soft_reset(ctx);
		break;

	case BMA150_SET_RANGE:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_range(ctx,*data);
		break;

	case BMA150_GET_RANGE:
		err = bma150_get_range(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_MODE:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_mode(ctx,*data);
		break;

	case BMA150_GET_MODE:
		err = bma150_get_mode(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_BANDWIDTH:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_bandwidth(ctx,*data);
		break;

	case BMA150_GET_BANDWIDTH:
		err = bma150_get_bandwidth(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_WAKE_UP_PAUSE:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_wake_up_pause(ctx,*data);
		break;

	case BMA150_GET_WAKE_UP_PAUSE:
		err = bma150_get_wake_up_pause(ctx,data);
		if (copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_LOW_G_THRESHOLD:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_low_g_threshold(ctx,*data);
		break;

	case BMA150_GET_LOW_G_THRESHOLD:
		err = bma150_get_low_g_threshold(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_LOW_G_COUNTDOWN:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_low_g_countdown(ctx,*data);
		break;

	case BMA150_GET_LOW_G_COUNTDOWN:
		err = bma150_get_low_g_countdown(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_HIGH_G_COUNTDOWN:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_high_g_countdown(ctx,*data);
		break;

	case BMA150_GET_HIGH_G_COUNTDOWN:
		err = bma150_get_high_g_countdown(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_LOW_G_DURATION:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_low_g_duration(ctx,*data);
		break;

	case BMA150_GET_LOW_G_DURATION:
		err = bma150_get_low_g_duration(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_HIGH_G_THRESHOLD:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_high_g_threshold(ctx,*data);
		break;

	case BMA150_GET_HIGH_G_THRESHOLD:
		err = bma150_get_high_g_threshold(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_HIGH_G_DURATION:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_high_g_duration(ctx,*data);
		break;

	case BMA150_GET_HIGH_G_DURATION:
		err = bma150_get_high_g_duration(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_ANY_MOTION_THRESHOLD:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_any_motion_threshold(ctx,*data);
		break;

	case BMA150_GET_ANY_MOTION_THRESHOLD:
		err = bma150_get_any_motion_threshold(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_ANY_MOTION_COUNT:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_any_motion_count(ctx,*data);
		break;

	case BMA150_GET_ANY_MOTION_COUNT:
		err = bma150_get_any_motion_count(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_INTERRUPT_MASK:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_interrupt_mask(ctx,*data);
		break;

	case BMA150_GET_INTERRUPT_MASK:
		err = bma150_get_interrupt_mask(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_RESET_INTERRUPT:
		err = bma150_reset_interrupt(ctx);
		break;

	case BMA150_READ_ACCEL_X:
		err = bma150_read_accel_x(ctx,(short *)data);
		if (err == 0 && copy_to_user((short *)arg, (short *)data, 2) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_READ_ACCEL_Y:
		err = bma150_read_accel_y(ctx,(short *)data);
		if (err == 0 && copy_to_user((short *)arg, (short *)data, 2) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_READ_ACCEL_Z:
		err = bma150_read_accel_z(ctx,(short *)data);
		if (err == 0 && copy_to_user((short *)arg, (short *)data, 2) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_GET_INTERRUPT_STATUS:
		err = bma150_get_interrupt_status(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_LOW_G_INT:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_low_g_int(ctx,*data);
		break;

	case BMA150_SET_HIGH_G_INT:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_high_g_int(ctx,*data);
		break;

	case BMA150_SET_ANY_MOTION_INT:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_any_motion_int(ctx,*data);
		break;

	case BMA150_SET_ALERT_INT:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_alert_int(ctx,*data);
		break;

	case BMA150_SET_ADVANCED_INT:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_advanced_int(ctx,*data);
		break;

	case BMA150_LATCH_INT:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_latch_int(ctx,*data);
		break;

	case BMA150_SET_NEW_DATA_INT:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_new_data_int(ctx,*data);
		break;

	case BMA150_GET_LOW_G_HYST:
		err = bma150_get_low_g_hysteresis(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_LOW_G_HYST:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_low_g_hysteresis(ctx,*data);
		break;

	case BMA150_GET_HIGH_G_HYST:
		err = bma150_get_high_g_hysteresis(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_SET_HIGH_G_HYST:
		if (copy_from_user(data, (unsigned char *)arg, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else
			err = bma150_set_high_g_hysteresis(ctx,*data);
		break;

	case BMA150_READ_ACCEL_XYZ:
		err = bma150_read_accel_xyz(ctx,(bma150acc_t *) data);
		if (err == 0 && copy_to_user((bma150acc_t *) arg, (bma150acc_t *) data, 6) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to error\n");
			err = -EFAULT;
		}
		break;

	case BMA150_READ_TEMPERATURE:
		err = bma150_read_temperature(ctx,data);
		if (err == 0 && copy_to_user((unsigned char *)arg, data, 1) != 0) {
			dev_dbg(&ctx->client->dev, "copy_to_user error\n");
			err = -EFAULT;
		}
		break;

		/* offset calibration routine */
	case BMA150_CALIBRATION:
		if (copy_from_user((bma150acc_t *) data, (bma150acc_t *) arg, 6) != 0) {
			dev_dbg(&ctx->client->dev, "copy_from_user error\n");
			err = -EFAULT;
		} else {
			/* iteration time 10 */
			temp = 10;
			err = bma150_calibrate(ctx,*(bma150acc_t *) data, &temp);
		}
		break;

	default:
		err = 0;
		break;
	}

	/* Unlock accelerometer */
	mutex_unlock(&ctx->lock);

	/* Return if success or not */
	return err;
}

static const struct file_operations bma150_fops = {
	.owner = THIS_MODULE,
	.read = bma150_read,
	.write = bma150_write,
	.open = bma150_open,
	.release = bma150_close,
	.unlocked_ioctl = bma150_ioctl,
};

static struct miscdevice bma_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "bma150",
	.fops = &bma150_fops,
};

int bma150_input_open(struct input_dev *input)
{
	struct bma150ctx *ctx = input_get_drvdata(input);
	int comres = 0;

	if (atomic_inc_return(&ctx->open_cnt) == 1) {

		/* opened for the first time - Enable accelerometer */
		if ((comres = bma150_enable(ctx))) {
			atomic_dec(&ctx->open_cnt);
			return comres;
		}
	}
	return 0;
}

void bma150_input_close(struct input_dev *dev)
{
	struct bma150ctx *ctx = input_get_drvdata(dev);
	if (atomic_dec_return(&ctx->open_cnt) == 0) {
		/* Last user, turn off accelerometer */
		bma150_disable(ctx);
	}
}

/* INPUT_ABS CONSTANTS */
#define G_MAX			512
#define FUZZ			32
#define FLAT			32

static void bma150_report_values(struct bma150ctx *ctx, bma150acc_t* accel)
{
	input_report_abs(ctx->input, ABS_X, accel->x);
	input_report_abs(ctx->input, ABS_Y, accel->y);
	input_report_abs(ctx->input, ABS_Z, accel->z);
	input_report_rel(ctx->input, REL_X, accel->x);
	input_report_rel(ctx->input, REL_Y, accel->y);
	input_report_rel(ctx->input, REL_Z, accel->z);

	input_sync(ctx->input);
}

static void bma150_input_work_func(struct work_struct *work)
{
	struct bma150ctx *ctx = container_of((struct delayed_work *)work,
					     struct bma150ctx, input_work);
	mutex_lock(&ctx->lock);

	if (bma150_read_accel_xyz(ctx, &ctx->acc) == 0) {
		bma150_report_values(ctx, &ctx->acc);
	}
	queue_delayed_work(ctx->input_work_queue, &ctx->input_work, msecs_to_jiffies(40));
	mutex_unlock(&ctx->lock);
}

static int bma150_input_init(struct i2c_client *client)
{
	struct bma150ctx *ctx = i2c_get_clientdata(client);
	int err;

	ctx->input_work_queue = create_singlethread_workqueue("bma150_input_wq");
	if (!ctx->input_work_queue) {
		err = -ENOMEM;
		dev_err(&ctx->client->dev, "could not create workqueue\n");
		goto err0;
	}
	INIT_DELAYED_WORK(&ctx->input_work, bma150_input_work_func);

	ctx->input = input_allocate_device();
	if (!ctx->input) {
		err = -ENOMEM;
		dev_err(&ctx->client->dev, "input device allocate failed: %d\n", err);
		goto err1;
	}

	input_set_drvdata(ctx->input, ctx);	
	
	ctx->input->open = bma150_input_open;
	ctx->input->close = bma150_input_close;

	set_bit(EV_ABS, ctx->input->evbit);
	set_bit(ABS_MISC, ctx->input->absbit);

	input_set_abs_params(ctx->input, ABS_X, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(ctx->input, ABS_Y, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(ctx->input, ABS_Z, -G_MAX, G_MAX, FUZZ, FLAT);

	input_set_capability(ctx->input, EV_REL, REL_X);
	input_set_capability(ctx->input, EV_REL, REL_Y);
	input_set_capability(ctx->input, EV_REL, REL_Z);

	snprintf(ctx->phys, sizeof(ctx->phys), "%s/input0", dev_name(&client->dev));
	ctx->input->name = "bma150";
	ctx->input->phys = ctx->phys;
	ctx->input->dev.parent = &client->dev;
	ctx->input->id.bustype = BUS_I2C;
	ctx->input->id.vendor = 0x0001;
	ctx->input->id.product = 0x0001;
	ctx->input->id.version = 0x0100;

	err = input_register_device(ctx->input);
	if (err) {
		dev_err(&ctx->client->dev, "unable to register input polled device %s: %d\n", ctx->input->name, err);
		goto err2;
	}

	return 0;
      err2:
	input_free_device(ctx->input);
      err1:
	destroy_workqueue(ctx->input_work_queue);
      err0:

	return err;
}

static void bma150_input_cleanup(struct bma150ctx *ctx)
{
	input_unregister_device(ctx->input);
	input_free_device(ctx->input);
	destroy_workqueue(ctx->input_work_queue);
}

static int bma150_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct bma150ctx *ctx;
	int err = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "client not i2c capable\n");
		err = -ENODEV;
		goto exit;
	}

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet. */
	if (!(ctx = kzalloc(sizeof(*ctx), GFP_KERNEL))) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate memory for module data: %d\n", err);
		goto exit;
	}
	g_ctx = ctx;

	ctx->client = client;
	i2c_set_clientdata(client, ctx);

	/* Initialize concurrent access mutex */
	mutex_init(&ctx->lock);

	/* initialize chip id */
	if (bma150_init(ctx)) {
		dev_err(&client->dev, "Bosch Sensortec Device not found\n");
		err = -ENODEV;
		goto exit_kfree2;
	}

	if ((err = bma150_input_init(client))) {
		dev_err(&client->dev, "Unable to register input device\n");
		err = -ENODEV;
		goto exit_kfree2;
	}

	/* create misc device */
	err = misc_register(&bma_device);
	if (err) {
		dev_err(&client->dev, "bma150 device register failed\n");
		goto exit_kfree;
	}

	dev_info(&client->dev, "Bosch Sensortec BMA150/BMA380 accelerometer driver registered\n");
	return 0;

exit_kfree:
	bma150_input_cleanup(ctx);

exit_kfree2:
	mutex_destroy(&ctx->lock);
	kfree(ctx);

exit:
	return err;
}

static int __devexit bma150_remove(struct i2c_client *client)
{
	struct bma150ctx *ctx = i2c_get_clientdata(client);
	misc_deregister(&bma_device);
	bma150_input_cleanup(ctx);
	mutex_destroy(&ctx->lock);
	ctx->client = NULL;
	kfree(ctx);
	return 0;
}

static int bma150_resume(struct i2c_client *client)
{
	struct bma150ctx *ctx = i2c_get_clientdata(client);
	if (!ctx->enabled_b4_suspend)
		return 0;
	return bma150_enable(ctx);
}

static int bma150_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bma150ctx *ctx = i2c_get_clientdata(client);
	ctx->enabled_b4_suspend = atomic_read(&ctx->open_cnt);
	if (!ctx->enabled_b4_suspend)
		return 0;
	return bma150_disable(ctx);
}

static const struct i2c_device_id bma150_id[] = {
	{"bma150", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, bma150_id);

static struct i2c_driver bma150_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "bma150",
		   },
	.probe = bma150_probe,
	.remove = __devexit_p(bma150_remove),
	.resume = bma150_resume,
	.suspend = bma150_suspend,
	.id_table = bma150_id,
};

static int __init BMA150_init(void)
{
	pr_info("bma150 accelerometer driver\n");
	return i2c_add_driver(&bma150_driver);
}

static void __exit BMA150_exit(void)
{
	i2c_del_driver(&bma150_driver);
}

module_init(BMA150_init);
module_exit(BMA150_exit);

MODULE_DESCRIPTION("BMA150 accelerometer driver");
MODULE_AUTHOR("Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_LICENSE("GPL");
