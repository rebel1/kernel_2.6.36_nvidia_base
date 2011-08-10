/*
 * drivers/input/touchscreen/it7260.c
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
#include <linux/input/it7260.h>

struct ts_rawpt {
	int x,y;		/* coordinates of the touch */
	int p;			/* Touch pressure */
};

struct ts_point {
	struct ts_rawpt data;	/* processed point data */
	int valid;				/* if point is valid or not */
};

struct it7260_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	char phys[32];
	
	int use_irq;
	struct hrtimer hr_timer;
	struct work_struct  work;
#ifdef CONFIG_HAS_EARLYSUSPEND	
	struct early_suspend early_suspend;
#endif

	struct workqueue_struct *it7260_wq;	
	void (*disable_tp)(void);	/* function to disable the touchpad */
	void (*enable_tp)(void);	/* function to enable the touchpad */
	
	int xres,yres;				/* touchpad resolution */
	
	struct ts_point pt[3]; 		/* The list of points used right now */
	int	   proximity_thresh;	/* Proximity threshold */
	int	   proximity_thresh2;	/* Proximity threshold squared */
	   
};
static struct it7260_ts_data *gl_ts;

// --- Low level touchscreen Functions
#define COMMAND_BUFFER_INDEX 			0x20
#define QUERY_BUFFER_INDEX 				0x80
#define COMMAND_RESPONSE_BUFFER_INDEX 	0xA0
#define POINT_BUFFER_INDEX2 			0xC0
#define POINT_BUFFER_INDEX 				0xE0

#define QUERY_SUCCESS 0x00
#define QUERY_BUSY 0x01
#define QUERY_ERROR 0x02
#define QUERY_POINT 0x80

static int it7260_read_query_buffer(struct it7260_ts_data *ts,unsigned char * pucData)
{
	int ret;
	char addr = QUERY_BUFFER_INDEX;
	struct i2c_msg msgs[] = {
		{
		 .addr = ts->client->addr,
		 .flags = 0,
		 .len = 1,
		 .buf = &addr,
		 },
		{
		 .addr = ts->client->addr,
		 .flags = I2C_M_RD,
		 .len = 1,
		 .buf = pucData,
		 },
	};
	ret = i2c_transfer(ts->client->adapter, msgs, 2);
	if (ret != 2) {
		return -1;
	}
	return 0;
}

static int it7260_read_command_response_buffer(struct it7260_ts_data *ts,unsigned char * pucData, unsigned int unDataLength)
{
	int ret;
	char addr = COMMAND_RESPONSE_BUFFER_INDEX;
	struct i2c_msg msgs[] = {
		{
		 .addr = ts->client->addr,
		 .flags = 0,
		 .len = 1,
		 .buf = &addr,
		 },
		{
		 .addr = ts->client->addr,
		 .flags = I2C_M_RD,
		 .len = unDataLength,
		 .buf = pucData,
		 },
	};
	ret = i2c_transfer(ts->client->adapter, msgs, 2);
	if (ret != 2) {
		dev_err(&ts->client->dev,"read command response buffer failed\n");
		return -1;
	}
	return 0;
}

static int it7260_read_point_buffer(struct it7260_ts_data *ts,unsigned char * pucData)
{
	int ret;
	char addr = POINT_BUFFER_INDEX;
	struct i2c_msg msgs[] = {
		{
		 .addr = ts->client->addr,
		 .flags = 0,
		 .len = 1,
		 .buf = &addr,
		 },
		{
		 .addr = ts->client->addr,
		 .flags = I2C_M_RD,
		 .len = 14,
		 .buf = pucData,
		 },
	};
	ret = i2c_transfer(ts->client->adapter, msgs, 2);
	if (ret != 2) {
		dev_err(&ts->client->dev,"read point buffer failed\n");
		return -1;
	}
	return 0;
}

static int it7260_write_command_buffer(struct it7260_ts_data *ts,unsigned char * pucData, unsigned int unDataLength)
{
	int ret;
	char buf[16];
	struct i2c_msg msgs[] = {
		{
		 .addr = ts->client->addr,
		 .flags = 0,
		 .len = 1+unDataLength,
		 .buf = &buf[0],
		 },
	};
	
	buf[0] = POINT_BUFFER_INDEX;
	memcpy(buf+1,pucData,unDataLength);
	
	ret = i2c_transfer(ts->client->adapter, msgs, 1);
	if (ret != 1) {
		unsigned int i;
		dev_err(&ts->client->dev,"write command buffer failed:\n");
		for (i=0;i<unDataLength;i++) {
			dev_err(&ts->client->dev,"[%d] = 0x%02x\n", i, pucData[i]);
		}
		return -1;
	}
	return 0;
}

// it7260_wait_for_idle: -1 on failure
static int it7260_wait_for_idle(struct it7260_ts_data *ts)
{
	unsigned char ucQuery;
	int test_read_count=0;
	
	// If failed to read, let the controller end the processing...
	if(it7260_read_query_buffer(ts,&ucQuery)<0)
	{
		msleep(10);
		ucQuery = QUERY_BUSY;
	}
	
	test_read_count=0;
	while((ucQuery & QUERY_BUSY) && (test_read_count<50000) )
	{
		test_read_count++;
		if(it7260_read_query_buffer(ts,&ucQuery)<0)
		{
			ucQuery = QUERY_BUSY;
		}
	}
	if (test_read_count>=50000) {
		return -1;
	}
	return 0;
}

static int it7260_flush(struct it7260_ts_data *ts)
{
	int ret = 0;
	dev_info(&ts->client->dev,"flushing buffers\n");	
	if (ts->client->irq) {
		// Interrupt assigned, use it to wait
		unsigned char ucQuery = 0;
		unsigned char pucPoint[14];
		int gpio = irq_to_gpio(ts->client->irq);
		int pollend = jiffies + HZ;	// 1 second of polling, maximum...
		while( !gpio_get_value(gpio) && jiffies < pollend) {
			it7260_read_query_buffer(ts,&ucQuery);
			it7260_read_point_buffer(ts,pucPoint);
			schedule();
		};
		ret = gpio_get_value(gpio) ? 0 : -1;
	} else {
		// No interrupt. Use a polling method
		unsigned char ucQuery = QUERY_BUSY;
		unsigned char pucPoint[14];
		int pollend = jiffies + HZ;	// 1 second of polling, maximum...
		while( (ucQuery & QUERY_BUSY) && jiffies < pollend) {
			if (it7260_read_query_buffer(ts,&ucQuery) >= 0) {
				it7260_read_point_buffer(ts,pucPoint);
			} else {
				ucQuery = QUERY_BUSY;
			}
			schedule();
		};
		ret = (ucQuery & QUERY_BUSY) ? -1 : 0;
		
	}
	dev_info(&ts->client->dev,"flushing ended %s\n",(ret < 0) ? "timedout" : "ok");
	return ret;
}

// Power on touchscreen
static int it7260_powerup(struct it7260_ts_data *ts)
{
	// Power on device
	static unsigned char powerOnCmd[] = {0x04, 0x00, 0x00 };
	
	// Flush device first
	it7260_flush(ts);
	
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on powerup timed out\n");
		return -1;
	}
		
	if (it7260_write_command_buffer(ts,&powerOnCmd[0],3)<0) {
		dev_err(&ts->client->dev,"failed to powerup\n");
		return -1;
	}
	
	return it7260_wait_for_idle(ts);
}

// Power down the touchscreen
static int it7260_powerdown(struct it7260_ts_data *ts)
{
	// Power off device
	static unsigned char powerOffCmd[] = {0x04, 0x00, 0x02 };
	
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on powerdown timed out\n");
		return -1;
	}

	if (it7260_write_command_buffer(ts,powerOffCmd,3)<0) {
		dev_err(&ts->client->dev,"failed to powerdown\n");
		return -1;
	}
	
	return it7260_wait_for_idle(ts);
}

// Enable interrupts
static int it7260_enable_interrupts(struct it7260_ts_data *ts)
{
	static unsigned char enableIntCmd[] = {0x02, 0x04, 0x01, 0x00 }; /* enable int, low level trigger */
	unsigned char ans[2] = {0,0};
	
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on enable interrupts timed out\n");
		return -1;
	}
		
	if (it7260_write_command_buffer(ts,enableIntCmd,4)<0) {
		dev_err(&ts->client->dev,"failed to enable interrupts\n");
		return -1;
	}

	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on enable interrupts timed out [2]\n");
		return -1;
	}

	if (it7260_read_command_response_buffer(ts,&ans[0],2) < 0) {
		dev_err(&ts->client->dev,"failed to read answer on enable interrupts\n");
		return -1;
	}
	
	if ((ans[0] | ans[1]) != 0) {
		dev_err(&ts->client->dev,"failed to enable interrupts [0x%02x%02x]\n",ans[1],ans[0]);
		return -1;
	}
	return 0;
}

// Disable ints
static int it7260_disable_interrupts(struct it7260_ts_data *ts)
{
	static unsigned char disableIntCmd[] = {0x02, 0x04, 0x00, 0x00 };
	unsigned char ans[2] = {0,0};
	
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on disable interrupts timed out\n");
		return -1;
	}

	if (it7260_write_command_buffer(ts,disableIntCmd,4)<0){
		dev_err(&ts->client->dev,"failed to disable interrupts\n");
		return -1;
	}

	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on disable interrupts timed out [2]\n");
		return -1;
	}

	if (it7260_read_command_response_buffer(ts,&ans[0],2) < 0) {
		dev_err(&ts->client->dev,"failed to read answer on disable interrupts\n");
		return -1;
	}
	
	if ((ans[0] | ans[1]) != 0) {
		dev_err(&ts->client->dev,"failed to disable interrupts [0x%02x%02x]\n",ans[1],ans[0]);
		return -1;
	}
	return 0;
}

// Calibration process
static int it7260_calibrate_cap_sensor(struct it7260_ts_data *ts)
{
	unsigned char pucCalibrate[] = { 
		0x13, 
		0x01, 		 /* autotune */
		0x01, 0x00,  /* threshold value = 0x0001 */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	unsigned char pucAns;
	int ret = 0;
	
	// Wait until idle
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on recalibrate timed out\n");
		return -1;
	}
	
	// Send command
	ret = it7260_write_command_buffer(ts,&pucCalibrate[0],12);
	if (ret < 0) {
		dev_err(&ts->client->dev,"failed to send calibration command\n");
		return -1;
	}
	
	// Wait until idle
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on recalibrate timed out [2]\n");
		return -1;
	}

	// Read status
	ret = it7260_read_command_response_buffer(ts,&pucAns,1);
	return ret < 0 ? -1 : 0;
}

static int it7260_init(struct it7260_ts_data *ts)
{
	unsigned char pucCmd[10];
	int ret = 0,i;
	
	// Reinitialize Firmware
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle while reinitializing fw\n");
		return -1;
	}
		
	pucCmd[0] = 0x6F;
	ret = it7260_write_command_buffer(ts,pucCmd,1);
	if (ret < 0) {
		dev_err(&ts->client->dev,"failed to reset touchpad\n");
		return -1;
	}
	
	// Let the IT reboot and init ... Takes some time ...
	mdelay(200);
	
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on reset touchpad timed out [2]\n");
		return -1;
	}

	if (it7260_read_command_response_buffer(ts,&pucCmd[0],2) < 0) {
		dev_err(&ts->client->dev,"failed to read answer to reset touchpad\n");
		return -1;
	}
	
	if ((pucCmd[0] | pucCmd[1]) != 0) {
		dev_err(&ts->client->dev,"failed to reset touchpad [0x%02x%02x]\n",pucCmd[1],pucCmd[0]);
		return -1;
	}

	// Wait until idle
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on identify cap sensor timed out\n");
		return -1;
	}

	// Now, we must poll the device waiting for it to settle... Otherwise, seems it does not respond...	
	it7260_flush(ts);
	

	// Don't know why, but firmware tends not to answer .... But, nevertheless, the touchscreen works.
	//  So, just ignore failures here
			
	// Identify
    pucCmd[0] = 0x00; 
	ret = it7260_write_command_buffer(ts,pucCmd,1);
	if (ret < 0) {
		dev_err(&ts->client->dev,"unable to send identify command\n");
		return -1;
	}

	// Wait until idle
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on identify cap sensor timed out [2]\n");
		return -1;
	}
		
	ret = it7260_read_command_response_buffer(ts,pucCmd,10);
	if (ret < 0) {
		dev_err(&ts->client->dev,"failed to get id from cap sensor\n");
		return -1;
	}
	dev_info(&ts->client->dev,"ID: [%d] %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x '%c%c%c%c%c%c%c%c%c'\n",
		pucCmd[0],pucCmd[1],
		pucCmd[2],pucCmd[3],pucCmd[4],pucCmd[5],
		pucCmd[6],pucCmd[7],pucCmd[8],pucCmd[9],
		pucCmd[0],pucCmd[1],
		pucCmd[2],pucCmd[3],pucCmd[4],pucCmd[5],
		pucCmd[6],pucCmd[7],pucCmd[8],pucCmd[9]
	);

	if (memcmp(&pucCmd[1],"ITE7260",7) != 0) {
		dev_err(&ts->client->dev,"signature not found. Perhaps IT7260 does not want to ID itself...\n");
	}
	
	// Get firmware information
	
	// Wait until idle
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on firmware version timed out\n");
	}
		
	pucCmd[0] = 0x01;
	pucCmd[1] = 0x00;
	ret = it7260_write_command_buffer(ts,pucCmd,2);
	if (ret < 0) {
		dev_err(&ts->client->dev,"unable to get firmware version\n");
	}

	// Wait until idle
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle on firmware version timed out [2]\n");
	}

	memset(&pucCmd, 0, sizeof(pucCmd));
	ret = it7260_read_command_response_buffer(ts,pucCmd,9);
	if (ret < 0) {
		dev_err(&ts->client->dev,"failed to read firmware version");
	}
	
	ret = 0;
	for (i = 5; i <= 8; i++) {
		ret += pucCmd[i];
	}
	if (ret == 0) {
		dev_info(&ts->client->dev,"no flash code");
	} else {
		dev_info(&ts->client->dev,"Flash code: %d.%d.%d.%d\n",
			pucCmd[5],pucCmd[6],pucCmd[7],pucCmd[8]);
	}

	// Get 2D Resolution
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle while getting 2D resolutions\n");
	}
		
	pucCmd[0] = 0x01;
	pucCmd[1] = 0x02;
	pucCmd[2] = 0x00;
	ret = it7260_write_command_buffer(ts,pucCmd,3);
	if (ret < 0) {
		dev_err(&ts->client->dev,"unable to query for 2D resolutions\n");
	}
	
	if (it7260_wait_for_idle(ts)) {
		dev_err(&ts->client->dev,"wait for idle while getting 2D resolutions [2]\n");
	}
		
	memset(&pucCmd, 0, sizeof(pucCmd));
	ret = it7260_read_command_response_buffer(ts,pucCmd,7);
	if (ret < 0) {
		dev_err(&ts->client->dev,"unable to read 2D resolution\n");
	}
		
	ts->xres = (int)(pucCmd[2] + (pucCmd[3] << 8));
	ts->yres = (int)(pucCmd[4] + (pucCmd[5] << 8));

	/* make sure to provide defaults, if touchscreen decided not to answer us */
	if (ts->xres == 0)
		ts->xres = 1024;
	if (ts->yres == 0)
		ts->yres = 600;

	dev_info(&ts->client->dev,"Resolution: X:%d , Y:%d\n", ts->xres, ts->yres);

	// Recalibrate it
	it7260_calibrate_cap_sensor(ts);
	
	return 0;
}

#define arr_nels(x) (sizeof(x)/sizeof(x[0]))

static int myabs(int x) 
{
	return (x < 0) ? -x : x;
}

static int dist2(int x1,int x2,int y1,int y2)
{
	int difx = x1 - x2;
	int dify = y1 - y2;
	return (difx * difx) + (dify * dify);
}

/* find closest point, or -1 if no points are near enough */
static int find_closest(struct ts_point* pts, int count, int x,int y,int maxdist2)
{
	int j;
	int dist = maxdist2;
	int pos = -1;
	for (j=0; j<count; j++) {
		if (pts[j].valid) {
			int tdist = dist2(pts[j].data.x,x,pts[j].data.y,y);
			if (tdist < dist) {
				dist = tdist;
				pos = j;
			}
		}
	}
	return pos;
}

/* find closest point by x, or -1 if no points are near enough */
static int find_closestx(struct ts_point* pts, int count, int x,int maxdist)
{
	int j;
	int dist = maxdist;
	int pos = -1;
	for (j=0; j<count; j++) {
		if (pts[j].valid) {
			int tdist = myabs(pts[j].data.x-x);
			if (tdist < dist) {
				dist = tdist;
				pos = j;
			}
		}
	}
	return pos;
}

/* find closest point by y, or -1 if no points are near enough */
static int find_closesty(struct ts_point* pts, int count, int y,int maxdist)
{
	int j;
	int dist = maxdist;
	int pos = -1;
	for (j=0; j<count; j++) {
		if (pts[j].valid) {
			int tdist = myabs(pts[j].data.y-y);
			if (tdist < dist) {
				dist = tdist;
				pos = j;
			}
		}
	}
	return pos;
}

/* Mark all points as invalid */
static void mark_allpts_asinvalid(struct ts_point* pts, int count)
{
	int i;
	for (i=0; i < count; i++) {
		pts[i].valid = 0;
	}
}

/* Find invalid pt */
static int find_invalid_pt(struct ts_point* pts, int count)
{
	int i;
	for (i=0; i < count; i++) {
		if (!pts[i].valid)
			return i;
	}
	return 0;
}

/* Init a point */
static void init_pt(struct ts_point* pt,struct ts_rawpt* p)
{
	pt->data.x = p->x;
	pt->data.y = p->y;
	pt->data.p = p->p;
	pt->valid = 1;
}

/* update point */
static void update_pt(struct ts_point* pt,struct ts_rawpt* p)
{
	pt->data.x = p->x;
	pt->data.y = p->y;
	pt->data.p = p->p;
	pt->valid = 1;
}

/* update point X */
static void update_ptx(struct ts_point* pt,struct ts_rawpt* p)
{
	pt->data.x = p->x;
	pt->data.p = p->p;
	pt->valid = 1;
}

/* update point Y */
static void update_pty(struct ts_point* pt,struct ts_rawpt* p)
{
	pt->data.y = p->y;
	pt->data.p = p->p;
	pt->valid = 1;
}

/* Called when no fingers are detected */
static void update_no_fingers(struct it7260_ts_data *ts)
{
	// Invalidate all points 
	mark_allpts_asinvalid(&ts->pt[0],arr_nels(ts->pt));
}

/* Called when just one finger is detected */
static void update_1_finger(struct it7260_ts_data *ts,struct ts_rawpt* p)
{
	// Look for a point close enough
	int pos = find_closest(&ts->pt[0],arr_nels(ts->pt),p->x,p->y,ts->proximity_thresh2);

	// Mark all points as invalid
	mark_allpts_asinvalid(&ts->pt[0],arr_nels(ts->pt));

	// If not found, store this new one as 0.
	if (pos == -1) {
		init_pt(&ts->pt[0],p);
		return;
	} 
		
	// And update the found one
	update_pt(&ts->pt[pos],p);
}

/* Called when just two fingers are detected */
static void update_2_fingers(struct it7260_ts_data *ts,struct ts_rawpt* p /*[2]*/)
{
	// Look for a point close enough trying all approachs
	int posd1 = find_closest(&ts->pt[0],arr_nels(ts->pt),p[0].x,p[0].y,ts->proximity_thresh2);
	int posd2 = find_closest(&ts->pt[0],arr_nels(ts->pt),p[1].x,p[1].y,ts->proximity_thresh2);
	
	// Mark all points as invalid
	mark_allpts_asinvalid(&ts->pt[0],arr_nels(ts->pt));

	// Except the found ones
	if (posd1 >= 0)
		ts->pt[posd1].valid = 1;
	if (posd2 >= 0)
		ts->pt[posd2].valid = 1;
	
	// If point1 was not found, find an empty slot and add it
	if (posd1 < 0) {
		int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
		init_pt(&ts->pt[pos],&p[0]);
	} else {
		// Found it, just update the point info
		update_pt(&ts->pt[posd1],&p[0]);
	}

	// If point2 was not found, find an empty slot and add it
	if (posd2 < 0) {
		int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
		init_pt(&ts->pt[pos],&p[1]);
	} else {
		// Found it, just update the point info
		update_pt(&ts->pt[posd2],&p[1]);
	}
}

/* Called when just three fingers are detected -- ITE7260 has lots
   of trouble with three fingers. Usually, it is unable to detect
   coordinates of each separate finger, it gives the same X coordinate
   or Y coordinate for all the 3 fingers. But that coordinate is just
   for one of the points, not the three... Try to deduce the missing 
   information and do the best we can. */
static void update_3_fingers(struct it7260_ts_data *ts,struct ts_rawpt* p /*[3]*/)
{
	// Discriminate orientation, if possible
	if (p[0].x == p[1].x && p[1].x == p[2].x) {
		int posd;
		// Horizontal reporting. Assume we can only use the Y coordinates to update. And
		//  the X coordinate can be used for just ONE point.
		
		// Look for a point close enough
		int posd1 = find_closesty(&ts->pt[0],arr_nels(ts->pt),p[0].y,ts->proximity_thresh);
		int posd2 = find_closesty(&ts->pt[0],arr_nels(ts->pt),p[1].y,ts->proximity_thresh);
		int posd3 = find_closesty(&ts->pt[0],arr_nels(ts->pt),p[2].y,ts->proximity_thresh);		
		
		// Mark all points as invalid
		mark_allpts_asinvalid(&ts->pt[0],arr_nels(ts->pt));

		// Except the found ones
		if (posd1 >= 0)
			ts->pt[posd1].valid = 1;
		if (posd2 >= 0)
			ts->pt[posd2].valid = 1;
		if (posd3 >= 0)
			ts->pt[posd3].valid = 1;
		
		// If point1 was not found, find an empty slot and add it
		if (posd1 < 0) {
			int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
			init_pt(&ts->pt[pos],&p[0]);
		} else {
			// Found it, just update the point info
			update_pty(&ts->pt[posd1],&p[0]);
		}

		// If point2 was not found, find an empty slot and add it
		if (posd2 < 0) {
			int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
			init_pt(&ts->pt[pos],&p[1]);
		} else {
			// Found it, just update the point info
			update_pty(&ts->pt[posd2],&p[1]);
		}

		// If point2 was not found, find an empty slot and add it
		if (posd3 < 0) {
			int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
			init_pt(&ts->pt[pos],&p[2]);
		} else {
			// Found it, just update the point info
			update_pty(&ts->pt[posd3],&p[2]);
		}
		
		// Finally, check for the point to update the X coordinates, if any
		// Look for a point close enough
		posd = find_closestx(&ts->pt[0],arr_nels(ts->pt),p[0].x,ts->proximity_thresh);
		
		// If found it, just update the point info
		if (posd >= 0) {
			update_ptx(&ts->pt[posd],&p[0]);
		}
				
	} else
	if (p[0].y == p[1].y && p[1].y == p[2].y) {	
		int posd;
		// Vertical reporting. Assume we can only use the X coordinates to update. And
		//  the Y coordinate can be used for just ONE point.

		// Look for a point close enough
		int posd1 = find_closestx(&ts->pt[0],arr_nels(ts->pt),p[0].x,ts->proximity_thresh);
		int posd2 = find_closestx(&ts->pt[0],arr_nels(ts->pt),p[1].x,ts->proximity_thresh);
		int posd3 = find_closestx(&ts->pt[0],arr_nels(ts->pt),p[2].x,ts->proximity_thresh);		
		
		// Mark all points as invalid
		mark_allpts_asinvalid(&ts->pt[0],arr_nels(ts->pt));

		// Except the found ones
		if (posd1 >= 0)
			ts->pt[posd1].valid = 1;
		if (posd2 >= 0)
			ts->pt[posd2].valid = 1;
		if (posd3 >= 0)
			ts->pt[posd3].valid = 1;
		
		// If point1 was not found, find an empty slot and add it
		if (posd1 < 0) {
			int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
			init_pt(&ts->pt[pos],&p[0]);
		} else {
			// Found it, just update the point info
			update_ptx(&ts->pt[posd1],&p[0]);
		}

		// If point2 was not found, find an empty slot and add it
		if (posd2 < 0) {
			int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
			init_pt(&ts->pt[pos],&p[1]);
		} else {
			// Found it, just update the point info
			update_ptx(&ts->pt[posd2],&p[1]);
		}

		// If point2 was not found, find an empty slot and add it
		if (posd3 < 0) {
			int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
			init_pt(&ts->pt[pos],&p[2]);
		} else {
			// Found it, just update the point info
			update_ptx(&ts->pt[posd3],&p[2]);
		}
		
		// Finally, check for the point to update the Y coordinates, if any
		// Look for a point close enough
		posd = find_closesty(&ts->pt[0],arr_nels(ts->pt),p[0].y,ts->proximity_thresh);
		
		// If found it, just update the point info
		if (posd >= 0) {
			update_pty(&ts->pt[posd],&p[0]);
		}

	} else {
		// Unable to find out orientation. Will use points as they are
		// Look for a point close enough
		int posd1 = find_closest(&ts->pt[0],arr_nels(ts->pt),p[0].x,p[0].y,ts->proximity_thresh2);
		int posd2 = find_closest(&ts->pt[0],arr_nels(ts->pt),p[1].x,p[1].y,ts->proximity_thresh2);
		int posd3 = find_closest(&ts->pt[0],arr_nels(ts->pt),p[2].x,p[2].y,ts->proximity_thresh2);		
		
		// Mark all points as invalid
		mark_allpts_asinvalid(&ts->pt[0],arr_nels(ts->pt));

		// Except the found ones
		if (posd1 >= 0)
			ts->pt[posd1].valid = 1;
		if (posd2 >= 0)
			ts->pt[posd2].valid = 1;
		if (posd3 >= 0)
			ts->pt[posd3].valid = 1;
		
		// If point1 was not found, find an empty slot and add it
		if (posd1 < 0) {
			int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
			init_pt(&ts->pt[pos],&p[0]);
		} else {
			// Found it, just update the point info
			update_pt(&ts->pt[posd1],&p[0]);
		}

		// If point2 was not found, find an empty slot and add it
		if (posd2 < 0) {
			int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
			init_pt(&ts->pt[pos],&p[1]);
		} else {
			// Found it, just update the point info
			update_pt(&ts->pt[posd2],&p[1]);
		}

		// If point2 was not found, find an empty slot and add it
		if (posd3 < 0) {
			int pos = find_invalid_pt(&ts->pt[0],arr_nels(ts->pt));
			init_pt(&ts->pt[pos],&p[2]);
		} else {
			// Found it, just update the point info
			update_pt(&ts->pt[posd3],&p[2]);
		}
	}
}

static void it7260_readpoints(struct it7260_ts_data *ts)
{
	unsigned char ucQuery = 0;
	unsigned char pucPoint[14];
	int ret = 0;
	
	struct ts_rawpt p[3];
	int idx = 0;

	// If error
	if(it7260_read_query_buffer(ts,&ucQuery)<0) {
		dev_err(&ts->client->dev,"failed to read points [1]\n");
		if (ts->use_irq)
		   enable_irq(ts->client->irq);
		return;
	}
	
	// If no query point
	if(!(ucQuery & QUERY_POINT))
	{
		if (ts->use_irq)
		   enable_irq(ts->client->irq);
		return ;
	}
	
	// Query point data
	ret = it7260_read_point_buffer(ts,pucPoint);

	// If error...
	if(ret < 0)
	{
		dev_err(&ts->client->dev,"failed to read points [2]\n");
		if (ts->use_irq)
		   enable_irq(ts->client->irq);
		return;
	}
	
#if 0
	dev_info(&ts->client->dev,"pucPt: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		pucPoint[0],pucPoint[1],pucPoint[2],pucPoint[3],pucPoint[4],
		pucPoint[5],pucPoint[6],pucPoint[7],pucPoint[8],pucPoint[9],
		pucPoint[10],pucPoint[11],pucPoint[12],pucPoint[13]
	);
#endif
	
	// gesture -- ignore it
	if(pucPoint[0] & 0xF0)
	{
#if 0
		dev_info(&ts->client->dev,"gesture\n");
#endif
		if (ts->use_irq)
		   enable_irq(ts->client->irq);
		return;
	} 			
	
#if 0
	// palm -- 
	if(pucPoint[1] & 0x01)
	{
		if (ts->use_irq)
		   enable_irq(ts->client->irq);
		return ;
	}
	
	// no more data
	if(!(pucPoint[0] & 0x08))
	{
		input_report_key(ts->input_dev, BTN_TOUCH, 0);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_mt_sync(ts->input_dev);
		input_sync(ts->input_dev);
		if (ts->use_irq)
			enable_irq(ts->client->irq);
		it7260_read_query_buffer(ts,&ucQuery);
		return;
	}
#endif

	// Collect all finger data
	if(pucPoint[0] & 0x01)
	{
		p[idx].x = ((pucPoint[3] & 0x0F) << 8) + pucPoint[2];
		p[idx].y = ((pucPoint[3] & 0xF0) << 4) + pucPoint[4];
		p[idx].p = pucPoint[5] & 0x0f;
		idx++;
	}

	if(pucPoint[0] & 0x02)
	{
		p[idx].x = ((pucPoint[7] & 0x0F) << 8) + pucPoint[6];
		p[idx].y = ((pucPoint[7] & 0xF0) << 4) + pucPoint[8];
		p[idx].p = pucPoint[9]&0x0f;
		idx++;
	}
	
	if(pucPoint[0] & 0x04) 
	{
		p[idx].x = ((pucPoint[11] & 0x0F) << 8) + pucPoint[10];
		p[idx].y = ((pucPoint[11] & 0xF0) << 4) + pucPoint[12];
		p[idx].p = pucPoint[13]&0x0f;
		idx++;
	}

#if 0
	dev_info(&ts->client->dev,"got points: %d\n",idx);
	for (ret = 0; ret < idx; ret ++) {
		dev_info(&ts->client->dev,"[%d] - X:%d, Y:%d, P:%d\n", ret,p[ret].x,p[ret].y,p[ret].p);
	}
#endif
	
	//  Now, based on the number of detected fingers, process them, 
	// trying to handle hw quirks.
	switch (idx) {
	default:
	case 0:
		update_no_fingers(ts);
		break;
	case 1:
		update_1_finger(ts,&p[0]);
		break;
	case 2:
		update_2_fingers(ts,&p[0]);
		break;
	case 3:
		update_3_fingers(ts,&p[0]);
		break;
	}
	
	//  Finally, translate the processed points into linux events.
	idx = 0;
	for (ret = 0; ret < 3; ret++) {
		if (ts->pt[ret].valid) {
		
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, ret);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, ts->pt[ret].data.p);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, ts->pt[ret].data.p);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X , ts->pt[ret].data.x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y , ts->pt[ret].data.y);
			input_mt_sync(ts->input_dev);
			idx++;
		}
	}
	
	input_sync(ts->input_dev);	

#if 0
	dev_info(&ts->client->dev,"processed points:\n");
	for (ret = 0; ret < 3; ret ++) {
		dev_info(&ts->client->dev,"[%d] - X:%d, Y:%d, P:%d, V:%d\n", ret,ts->pt[ret].data.x,ts->pt[ret].data.y,ts->pt[ret].data.p,ts->pt[ret].valid);
	}
#endif
	
	// If nothing being touched...
	if(idx == 0)
	{
		input_report_key(ts->input_dev, BTN_TOUCH, 0);
		input_report_key(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_mt_sync(ts->input_dev);
		input_sync(ts->input_dev);	
	} 

	
	if (ts->use_irq)
		enable_irq(ts->client->irq);
}

static void it7260_ts_work_func(struct work_struct *work)
{
	struct it7260_ts_data *ts = container_of(work, struct it7260_ts_data, work);
	it7260_readpoints(ts);
}

static enum hrtimer_restart it7260_ts_timer_func(struct hrtimer *timer)
{
	struct it7260_ts_data *ts = container_of(timer, struct it7260_ts_data, hr_timer);

	queue_work(ts->it7260_wq, &ts->work);

	hrtimer_start(&ts->hr_timer, ktime_set(0, 1250), HRTIMER_MODE_REL);//12500000
	return HRTIMER_NORESTART;
}

static irqreturn_t it7260_ts_irq_handler(int irq, void *dev_id)
{
	struct it7260_ts_data *ts = dev_id;

	disable_irq_nosync(ts->client->irq);
	queue_work(ts->it7260_wq, &ts->work);
	return IRQ_HANDLED;
}

///////////////////////////////////////////////////////////////////////////////////////

static ssize_t threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct it7260_ts_data *ts = gl_ts;

	return sprintf(buf, "%d\n", ts->proximity_thresh);
}

static ssize_t threshold_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct it7260_ts_data *ts = gl_ts;
	long val = 0;
	int error = strict_strtol(buf,10,&val);
	if (error) 
		return error;
	
	if (val > 1024) 
		val = 1024;
	
	ts->proximity_thresh = buf[0] - '0';
	ts->proximity_thresh2 = ts->proximity_thresh * ts->proximity_thresh;

	return count;
}

static DEVICE_ATTR(threshold, 0664, threshold_show, threshold_store);

static ssize_t it7260_calibration_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "[%d][%d]\n", 0,0);
}

static ssize_t it7260_calibration_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	it7260_calibrate_cap_sensor(gl_ts);
	return 0;
}
static DEVICE_ATTR(calibration, 0666, it7260_calibration_show, it7260_calibration_store);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void it7260_ts_early_suspend(struct early_suspend *h);
static void it7260_ts_late_resume(struct early_suspend *h);
#endif

///////////////////////////////////////////////////////////////////////////////////////
static int it7260_ts_probe(
	struct i2c_client *client, const struct i2c_device_id *id)
{
	struct it7260_ts_data *ts;
	struct it7260_platform_data *pdata = client->dev.platform_data;
	int ret = 0;
	
	dev_info(&client->dev,"IT7260 touchscreen Driver\n");

	if (!pdata) {
		dev_err(&client->dev,"no platform data\n");
		return -EIO;
	}
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,"need I2C_FUNC_I2C\n");
		return -EIO;
	}

	ts = kzalloc(sizeof(struct it7260_ts_data), GFP_KERNEL);
	if (!ts) {
		dev_err(&client->dev,"failed memory allocation\n");
		return -ENOMEM;
	}
	
	gl_ts = ts;
	i2c_set_clientdata(client, ts);
	ts->client = client;
	
	// Fill in default values
	ts->disable_tp = pdata->disable_tp;	/* function to disable the touchpad */
	ts->enable_tp = pdata->enable_tp;	/* function to enable the touchpad */
	ts->proximity_thresh = 50;
	ts->proximity_thresh2 = ts->proximity_thresh * ts->proximity_thresh;

	// Enable the touchpad
	if (ts->enable_tp)
		ts->enable_tp();
	
	// Try to init the capacitive sensor
	if(it7260_init(ts)) {
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
	ts->input_dev->name = "it7260";
	ts->input_dev->phys = ts->phys;
	ts->input_dev->dev.parent = &client->dev;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0x0001;
	ts->input_dev->id.product = 0x0001;
	ts->input_dev->id.version = 0x0100;
	
	// And capabilities
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);

	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 15, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->xres, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->yres, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 2, 0, 0);
	
	input_set_abs_params(ts->input_dev, ABS_X, 0, ts->xres, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, ts->yres, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 15, 0, 0);

	ret = input_register_device(ts->input_dev);
	if (ret) {
		ret = -ENOMEM;
		dev_err(&client->dev,"unable to register %s input device\n", ts->input_dev->name);
		goto err_could_not_register;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING - 1;
	ts->early_suspend.suspend = it7260_ts_early_suspend;
	ts->early_suspend.resume = it7260_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
	
	ts->it7260_wq = create_singlethread_workqueue("it7260_wq");
	if (!ts->it7260_wq) {
		ret = -ENOMEM;
		dev_err(&client->dev,"unable to allocate workqueue\n");	
		goto err_alloc_wq;
	}
	
	INIT_WORK(&ts->work, it7260_ts_work_func);

	if (client->irq) {
		ret = request_irq(client->irq, it7260_ts_irq_handler, IRQF_TRIGGER_LOW, client->name, ts);
		if (!ret) {
			ts->use_irq = 1;
		} else {
			dev_err(&client->dev, "request_irq failed\n");
		}
	}
	if (!ts->use_irq) {

		hrtimer_init(&ts->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->hr_timer.function = it7260_ts_timer_func;
		hrtimer_start(&ts->hr_timer, ktime_set(1, 0), HRTIMER_MODE_REL);
		
		// Disable ints
		it7260_disable_interrupts(ts);
		
	} else {
	
		// Enable interrupts
		it7260_enable_interrupts(ts);
	}

    // Create attributes
    gl_ts=ts;
    ret = device_create_file(&ts->input_dev->dev, &dev_attr_calibration);
	if (ret) {
		ret = -ENOMEM;
		dev_err(&client->dev, "error creating calibration attribute\n");
		goto err_attr_create;
	}

    ret = device_create_file(&ts->input_dev->dev, &dev_attr_threshold);
	if (ret) {
		ret = -ENOMEM;
		dev_err(&client->dev, "error creating threshold attribute\n");
		goto err_attr_create;
	}

	dev_info(&client->dev,"touchscreen driver loaded (using ints:%c)\n",ts->use_irq?'Y':'N');
	return 0;
	
err_attr_create:
	it7260_disable_interrupts(ts);
	
	if (ts->use_irq)
		free_irq(client->irq,ts);
	
	destroy_workqueue(ts->it7260_wq);
	
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

static int it7260_ts_remove(struct i2c_client *client)
{
	struct it7260_ts_data *ts = i2c_get_clientdata(client);
	
	it7260_disable_interrupts(ts);
	if (ts->use_irq)
		free_irq(client->irq,ts);
	destroy_workqueue(ts->it7260_wq);
	input_free_device(ts->input_dev);
	
	// Disable the touchpad
	if (ts->disable_tp)
		ts->disable_tp();

	i2c_set_clientdata(client, NULL);
	kfree(ts);
	
	return 0;
}

static int it7260_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct it7260_ts_data *ts = i2c_get_clientdata(client);
	if (ts->use_irq)
		disable_irq(client->irq);
	else
		hrtimer_cancel(&ts->hr_timer);
		
	ret = cancel_work_sync(&ts->work);
	if (ret && ts->use_irq) /* if work was pending disable-count is now 2 */
		enable_irq(client->irq);
		
	// Disable interrupts, if being used
	if (ts->use_irq) {
		it7260_disable_interrupts(ts);
	}
	
	// Power down the touchscreen
	it7260_powerdown(ts);

	// Disable the touchpad
	if (ts->disable_tp)
		ts->disable_tp();

	return 0;
}


static int it7260_ts_resume(struct i2c_client *client)
{
	struct it7260_ts_data *ts = i2c_get_clientdata(client);

	// Enable the touchpad
	if (ts->enable_tp)
		ts->enable_tp();
	
	if (ts->use_irq)
		enable_irq(client->irq);
	else
		hrtimer_start(&ts->hr_timer, ktime_set(1, 0), HRTIMER_MODE_REL);

	// Power up the touchscreen
	it7260_powerup(ts);
		
	// Enable interrupts, if being used
	if (ts->use_irq) {
		it7260_enable_interrupts(ts);
	}

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void it7260_ts_early_suspend(struct early_suspend *h)
{
	struct it7260_ts_data *ts;
	ts = container_of(h, struct it7260_ts_data, early_suspend);
	it7260_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void it7260_ts_late_resume(struct early_suspend *h)
{
	struct it7260_ts_data *ts;
	ts = container_of(h, struct it7260_ts_data, early_suspend);
	it7260_ts_resume(ts->client);
}
#endif


static const struct i2c_device_id it7260_ts_id[] = {
	{ "it7260", 0 },
	{}
};

static struct i2c_driver it7260_ts_driver = {
	.driver = {
		.name	= "it7260",
		.owner  = THIS_MODULE,
	},
	.probe		= it7260_ts_probe,
	.remove		= it7260_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= it7260_ts_suspend,
	.resume		= it7260_ts_resume,
#endif
	.id_table	= it7260_ts_id,
};

static int __devinit it7260_ts_init(void)
{
	pr_info("it7260 touchscreen driver\n");
	return i2c_add_driver(&it7260_ts_driver);
}

static void __exit it7260_ts_exit(void)
{
	i2c_del_driver(&it7260_ts_driver);
}

module_init(it7260_ts_init);
module_exit(it7260_ts_exit);

MODULE_AUTHOR("Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_DESCRIPTION("IT7260 Touchscreen Driver");
MODULE_LICENSE("GPL");
