/*
 * NVEC: NVIDIA compliant embedded controller interface
 *
 * Copyright (C) 2011 Marc Dietrich <marvin24@gmx.de>
 *
 * Authors:  Pierre-Hugues Husson <phhusson@free.fr>
 *           Ilya Petrov <ilya.muromec@gmail.com>
 *           Marc Dietrich <marvin24@gmx.de>
 *           Eduardo José Tagle <ejtagle@hotmail.com> 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#ifndef __LINUX_MFD_NVEC
#define __LINUX_MFD_NVEC


struct nvec_subdev_info {
	char* name;				/* subdevice name */
	int	  id;				/* subdevice id */
	void* platform_data;	/* Associated platform data */
};
	
struct nvec_platform_data {
	int i2c_addr;			/* i2c address for the Slave */
	int gpio;				/* GPIO used to call attention of the NvEC */
	int irq;				/* i2c IRQ */
	int base;				/* i2c registers base address */
	int size;				/* i2c interrupt source */
	char* clock;			/* i2c clock */
	
	struct nvec_subdev_info *subdevs;		/* subdevices to register */
	int 					 num_subdevs;	/* Number of subdevices */
	
	int (*oem_init) (struct device* dev);	/* Callback called by the init 
											   procedure to send commands for specific 
											   initialization of boards */
};
	

	
#define NVEC_MAX_MSG_SZ 32

struct nvec_event {
	struct list_head 	node; 	/* linked list node to queue packets to send */
	int					id;		/* Message id, used to release events */
	
	/* Decoded event */
	u8					ev;		/* Event */
	u8					status;	/* status */
	u8					size;	/* payload size */
	u8					data[NVEC_MAX_MSG_SZ];
};


extern int nvec_cmd_xfer(struct device* dev, int cmd,int subcmd,
				 const void* txpayld,int txpayldsize,
				 void* rxpayld,int rxpayloadmaxsize);

extern int nvec_add_eventhandler(struct device* dev, 
			struct notifier_block *nb);

extern int nvec_remove_eventhandler(struct device* dev, 
			struct notifier_block *nb);

extern void nvec_poweroff(void);

/* Request/Response types */
#define NVEC_CMD_SYSTEM					0x01
#define NVEC_CMD_BATTERY				0x02
#define NVEC_CMD_GPIO					0x03
#define NVEC_CMD_SLEEP 					0x04
#define NVEC_CMD_KEYBOARD				0x05
#define NVEC_CMD_AUXDEVICE				0x06
#define NVEC_CMD_CONTROL				0x07
#define NVEC_CMD_OEM0					0x0D
#define NVEC_CMD_OEM1					0x0E

/* Event types */
#define NVEC_EV_KEYBOARD				0x00
#define NVEC_EV_AUXDEVICE0				0x01
#define NVEC_EV_AUXDEVICE1				0x02
#define NVEC_EV_AUXDEVICE2				0x03
#define NVEC_EV_AUXDEVICE3				0x04
#define NVEC_EV_SYSTEM					0x05
#define NVEC_EV_GPIOSCALAR				0x06
#define NVEC_EV_GPIOVECTOR				0x07
#define NVEC_EV_BATTERY					0x08
#define NVEC_EV_OEM0					0x0D
#define NVEC_EV_OEM1					0x0E

/* Possible Status responses */
#define NVEC_STATUS_SUCCESS				0x00
#define NVEC_STATUS_TIMEOUT				0x01
#define NVEC_STATUS_PARITY				0x02
#define NVEC_STATUS_UNAVAILABLE			0x03
#define NVEC_STATUS_INVALIDCOMMAND		0x04
#define NVEC_STATUS_INVALIDSIZE			0x05
#define NVEC_STATUS_INVALIDPARAMETER	0x06
#define NVEC_STATUS_UNSUPPORTEDCONFIG	0x07
#define NVEC_STATUS_CHECKSUMFAILURE		0x08
#define NVEC_STATUS_WRITEFAILURE		0x09
#define NVEC_STATUS_READFAILURE			0x0A
#define NVEC_STATUS_OVERFLOW			0x0B
#define NVEC_STATUS_UNDERFLOW			0x0C
#define NVEC_STATUS_INVALIDSTATE		0x0D
#define NVEC_STATUS_OEM0 				0xD0
#define NVEC_STATUS_OEM1				0xD1
#define NVEC_STATUS_OEM2				0xD2
#define NVEC_STATUS_OEM3				0xD3
#define NVEC_STATUS_OEM4				0xD4
#define NVEC_STATUS_OEM5				0xD5
#define NVEC_STATUS_OEM6				0xD6
#define NVEC_STATUS_OEM7				0xD7
#define NVEC_STATUS_OEM8				0xD8
#define NVEC_STATUS_OEM9				0xD9
#define NVEC_STATUS_OEM10				0xDA
#define NVEC_STATUS_OEM11				0xDB
#define NVEC_STATUS_OEM12				0xDC
#define NVEC_STATUS_OEM13				0xDD
#define NVEC_STATUS_OEM14				0xDE
#define NVEC_STATUS_OEM15				0xDF
#define NVEC_STATUS_OEM16				0xE0
#define NVEC_STATUS_OEM17				0xE1
#define NVEC_STATUS_OEM18				0xE2
#define NVEC_STATUS_OEM19				0xE3
#define NVEC_STATUS_OEM20				0xE4
#define NVEC_STATUS_OEM21				0xE5
#define NVEC_STATUS_OEM22				0xE6
#define NVEC_STATUS_OEM23				0xE7
#define NVEC_STATUS_OEM24				0xE8
#define NVEC_STATUS_OEM25				0xE9
#define NVEC_STATUS_OEM26				0xEA
#define NVEC_STATUS_OEM27				0xEB
#define NVEC_STATUS_OEM28				0xEC
#define NVEC_STATUS_OEM29				0xED
#define NVEC_STATUS_OEM30				0xEE
#define NVEC_STATUS_OEM31				0xEF


/**
 * Variable-length strings
 *
 * Variable-length strings in Response Packets may not be null-terminated.
 * Maximum length for variable-length strings is defined below.
 */

#define NVEC_MAX_RESPONSE_STRING_SIZE  30

/**
 * Byte ordering
 * 
 * Multi-byte integers in the payload section of Request, Response, and Event
 * Packets are treated as byte arrays.  The bytes are stored in little-endian
 * order (least significant byte first, most significant byte last).
 *
 * Multi-byte integers in the payload section of Request, Response, and Event
 * Packets are treated as byte arrays.  The bytes are stored in little-endian
 * order (least significant byte first, most significant byte last).
 */ 
#define NVEC_GETU16(x) (x[0] | ((u16)x[1] << 8))
 

/* System subrequests */ 
#define NVEC_CMD_SYSTEM_GETSTATUS							0x00
#define NVEC_CMD_SYSTEM_CONFIGEVENTREPORTING				0x01
#define NVEC_CMD_SYSTEM_ACKSYSTEMSTATUS						0x02
#define NVEC_CMD_SYSTEM_CONFIGWAKE							0xFD

/* System payload data structures */
struct NVEC_ANS_SYSTEM_GETSTATUS_PAYLOAD {
    u8 State[2];     			// see NVEC_SYSTEM_STATE* #define's
    u8 OemState[2];
};

#define NVEC_SYSTEM_STATE0_0_EC_RESET_MASK              (1<<4)
#define NVEC_SYSTEM_STATE0_0_EC_RESET                   (1<<4)
#define NVEC_SYSTEM_STATE0_0_AP_POWERDOWN_NOW_MASK      (1<<3)
#define NVEC_SYSTEM_STATE0_0_AP_POWERDOWN_NOW           (1<<3)
#define NVEC_SYSTEM_STATE0_0_AP_SUSPEND_NOW_MASK        (1<<2)
#define NVEC_SYSTEM_STATE0_0_AP_SUSPEND_NOW             (1<<2)
#define NVEC_SYSTEM_STATE0_0_AP_RESTART_NOW_MASK        (1<<1)
#define NVEC_SYSTEM_STATE0_0_AP_RESTART_NOW             (1<<1)

#define NVEC_SYSTEM_STATE1_0_AC_MASK                    0x1
#define NVEC_SYSTEM_STATE1_0_AC_NOT_PRESENT             0x0
#define NVEC_SYSTEM_STATE1_0_AC_PRESENT                 0x1

struct NVEC_REQ_SYSTEM_CONFIGEVENTREPORTING_PAYLOAD
{
    u8 					ReportEnable;     		// see NVEC_REQ_SYSTEM_REPORT_ENABLE* #define's
    u8 					SystemStateMask[2];     	// see NVEC_SYSTEM_STATE* #define's
    u8 					OemStateMask[2];
};

#define NVEC_REQ_SYSTEM_REPORT_ENABLE_0_ACTION_MASK         0xFF
#define NVEC_REQ_SYSTEM_REPORT_ENABLE_0_ACTION_DISABLE      0x00
#define NVEC_REQ_SYSTEM_REPORT_ENABLE_0_ACTION_ENABLE       0x01

struct NVEC_REQ_SYSTEM_ACKSYSTEMSTATUS_PAYLOAD
{
    u8 					SystemStateMask[2];     	// see NVEC_SYSTEM_STATE* #define's
    u8 					OemStateMask[2];
};

struct NVEC_REQ_SYSTEM_CONFIGWAKE_PAYLOAD
{
    u8 					WakeEnable;     			// see NVEC_REQ_SYSTEM_WAKE_ENABLE* #define's
    u8 					SystemStateMask[2];     	// see NVEC_SYSTEM_STATE* #define's
    u8 					OemStateMask[2];
};

#define NVEC_REQ_SYSTEM_WAKE_ENABLE_0_ACTION_MASK           0xFF
#define NVEC_REQ_SYSTEM_WAKE_ENABLE_0_ACTION_DISABLE        0x00
#define NVEC_REQ_SYSTEM_WAKE_ENABLE_0_ACTION_ENABLE         0x01


/* Battery subrequests */
#define NVEC_CMD_BATTERY_GETSLOTSTATUS						0x00
#define NVEC_CMD_BATTERY_GETVOLTAGE							0x01
#define NVEC_CMD_BATTERY_GETTIMEREMAINING					0x02
#define NVEC_CMD_BATTERY_GETCURRENT							0x03
#define NVEC_CMD_BATTERY_GETAVERAGECURRENT					0x04
#define NVEC_CMD_BATTERY_GETAVERAGINGTIMEINTERVAL			0x05
#define NVEC_CMD_BATTERY_GETCAPACITYREMAINING				0x06
#define NVEC_CMD_BATTERY_GETLASTFULLCHARGECAPACITY			0x07
#define NVEC_CMD_BATTERY_GETDESIGNCAPACITY					0x08
#define NVEC_CMD_BATTERY_GETCRITICALCAPACITY				0x09
#define NVEC_CMD_BATTERY_GETTEMPERATURE						0x0A
#define NVEC_CMD_BATTERY_GETMANUFACTURER					0x0B
#define NVEC_CMD_BATTERY_GETMODEL							0x0C
#define NVEC_CMD_BATTERY_GETTYPE							0x0D
#define NVEC_CMD_BATTERY_SETREMAININGCAPACITYALARM			0x0E
#define NVEC_CMD_BATTERY_GETREMAININGCAPACITYALARM			0x0F
#define NVEC_CMD_BATTERY_SETCONFIGURATION					0x10
#define NVEC_CMD_BATTERY_GETCONFIGURATION					0x11
#define NVEC_CMD_BATTERY_CONFIGUREEVENTREPORTING			0x12
#define NVEC_CMD_BATTERY_CONFIGUREWAKE 						0x1D


#define NVEC_SUBTYPE_0_BATTERY_SLOT_MASK                    0xF0
#define NVEC_SUBTYPE_0_BATTERY_INFO_MASK                    0x0F

/**
 * Battery payload data structures
 */

struct NVEC_ANS_BATTERY_GETSLOTSTATUS_PAYLOAD 
{
    u8 SlotStatus;     			// see NVEC_ANS_BATTERY_SLOT_STATUS* #define's
    u8 CapacityGauge;
};

#define NVEC_ANS_BATTERY_SLOT_STATUS_0_CRITICAL_CAPACITY_ALARM_MASK     (1<<3)
#define NVEC_ANS_BATTERY_SLOT_STATUS_0_CRITICAL_CAPACITY_ALARM_UNSET    (0<<3)
#define NVEC_ANS_BATTERY_SLOT_STATUS_0_CRITICAL_CAPACITY_ALARM_SET      (1<<3)

#define NVEC_ANS_BATTERY_SLOT_STATUS_0_CHARGING_STATE_MASK                  (0x3<<1)
#define NVEC_ANS_BATTERY_SLOT_STATUS_0_CHARGING_STATE_IDLE                  (0x0<<1)
#define NVEC_ANS_BATTERY_SLOT_STATUS_0_CHARGING_STATE_CHARGING              (0x1<<1)
#define NVEC_ANS_BATTERY_SLOT_STATUS_0_CHARGING_STATE_DISCHARGING           (0x2<<1)

#define NVEC_ANS_BATTERY_SLOT_STATUS_0_PRESENT_STATE_MASK                   0x1
#define NVEC_ANS_BATTERY_SLOT_STATUS_0_PRESENT_STATE_NOT_PRESENT            0x0
#define NVEC_ANS_BATTERY_SLOT_STATUS_0_PRESENT_STATE_PRESENT                0x1

struct NVEC_ANS_BATTERY_GETVOLTAGE_PAYLOAD
{
    u8 PresentVoltage[2];     	// 16-bit unsigned value, in mV
};

struct NVEC_ANS_BATTERY_GETTIMEREMAINING_PAYLOAD
{
    u8 TimeRemaining[2];     	// 16-bit unsigned value, in minutes
};

struct NVEC_ANS_BATTERY_GETCURRENT_PAYLOAD
{
    u8 PresentCurrent[2];     // 16-bit signed value, in mA
};

struct NVEC_ANS_BATTERY_GETAVERAGECURRENT_PAYLOAD
{
    u8 AverageCurrent[2];     // 16-bit signed value, in mA
};

struct NVEC_ANS_BATTERY_GETAVERAGINGTIMEINTERVAL_PAYLOAD
{
    u8 TimeInterval[2];     // 16-bit unsigned value, in msec
};

struct NVEC_ANS_BATTERY_GETCAPACITYREMAINING_PAYLOAD
{
    u8 CapacityRemaining[2];     // 16-bit unsigned value, in mAh or 10mWh
};

struct NVEC_ANS_BATTERY_GETLASTFULLCHARGECAPACITY_PAYLOAD
{
    u8 LastFullChargeCapacity[2];     // 16-bit unsigned value, in mAh or 10mWh
};

struct NVEC_ANS_BATTERY_GETDESIGNCAPACITY_PAYLOAD
{
    u8 DesignCapacity[2];     // 16-bit unsigned value, in mAh or 10mWh
};

struct NVEC_ANS_BATTERY_GETCRITICALCAPACITY_PAYLOAD
{
    u8 CriticalCapacity[2];     // 16-bit unsigned value, in mAh or 10mWh
};

struct NVEC_ANS_BATTERY_GETTEMPERATURE_PAYLOAD
{
    u8 Temperature[2];     // 16-bit unsigned value, in 0.1 degrees Kelvin
};

struct NVEC_ANS_BATTERY_GETMANUFACTURER_PAYLOAD
{
    char Manufacturer[NVEC_MAX_RESPONSE_STRING_SIZE];
};

struct NVEC_ANS_BATTERY_GETMODEL_PAYLOAD
{
    char Model[NVEC_MAX_RESPONSE_STRING_SIZE];
};

struct NVEC_ANS_BATTERY_GETTYPE_PAYLOAD
{
    char Type[NVEC_MAX_RESPONSE_STRING_SIZE];
};

struct NVEC_ANS_BATTERY_SETREMAININGCAPACITYALARM_PAYLOAD
{
    u8 CapacityThreshold[2];     // 16-bit unsigned value, in mAh or 10mWh
};

struct NVEC_ANS_BATTERY_GETREMAININGCAPACITYALARM_PAYLOAD
{
    u8 CapacityThreshold[2];     // 16-bit unsigned value, in mAh or 10mWh
};

struct NVEC_REQ_BATTERY_SETCONFIGURATION_PAYLOAD
{
    u8 					Configuration;     		// see NVEC_BATTERY_CONFIGURATION* #define's
};

#define NVEC_BATTERY_CONFIGURATION_0_CAPACITY_UNITS_MASK                0x1
#define NVEC_BATTERY_CONFIGURATION_0_CAPACITY_UNITS_MAH                 0x0
#define NVEC_BATTERY_CONFIGURATION_0_CAPACITY_UNITS_10MWH               0x1

struct NVEC_ANS_BATTERY_GETCONFIGURATION_PAYLOAD
{
    u8 Configuration;     		// see NVEC_BATTERY_CONFIGURATION* #define's
};

struct NVEC_REQ_BATTERY_CONFIGUREEVENTREPORTING_PAYLOAD
{
    u8 					ReportEnable;     		// see NVEC_REQ_BATTERY_REPORT_ENABLE* #define's
    u8 					EventTypes;     		// see NVEC_BATTERY_EVENT_TYPE* #define's
};

#define NVEC_REQ_BATTERY_REPORT_ENABLE_0_ACTION_MASK                        0xFF
#define NVEC_REQ_BATTERY_REPORT_ENABLE_0_ACTION_DISABLE                     0x0
#define NVEC_REQ_BATTERY_REPORT_ENABLE_0_ACTION_ENABLE                      0x1

#define NVEC_BATTERY_EVENT_TYPE_0_REMAINING_CAPACITY_ALARM_MASK         	0x4
#define NVEC_BATTERY_EVENT_TYPE_0_REMAINING_CAPACITY_ALARM_ENABLE       	0x0
#define NVEC_BATTERY_EVENT_TYPE_0_REMAINING_CAPACITY_ALARM_DISABLE      	0x4

#define NVEC_BATTERY_EVENT_TYPE_0_CHARGING_STATE_MASK                   	0x2
#define NVEC_BATTERY_EVENT_TYPE_0_CHARGING_STATE_ENABLE                 	0x0
#define NVEC_BATTERY_EVENT_TYPE_0_CHARGING_STATE_DISABLE                	0x2

#define NVEC_BATTERY_EVENT_TYPE_0_PRESENT_STATE_MASK                    	0x1
#define NVEC_BATTERY_EVENT_TYPE_0_PRESENT_STATE_ENABLE                  	0x0
#define NVEC_BATTERY_EVENT_TYPE_0_PRESENT_STATE_DISABLE                 	0x1

struct NVEC_REQ_BATTERY_CONFIGUREWAKE_PAYLOAD
{
    u8 					WakeEnable;     // see NVEC_REQ_BATTERY_WAKE_ENABLE* #define's
    u8 					EventTypes;     // see NVEC_BATTERY_EVENT_TYPE* #define's
};

#define NVEC_REQ_BATTERY_WAKE_ENABLE_ACTION_MASK                            0xFF
#define NVEC_REQ_BATTERY_WAKE_ENABLE_ACTION_DISABLE                         0x0
#define NVEC_REQ_BATTERY_WAKE_ENABLE_ACTION_ENABLE                          0x1

 
/* Gpio subrequests */
#define NVEC_CMD_GPIO_CONFIGUREPIN							0x00
#define NVEC_CMD_GPIO_SETPINSCALAR							0x01
#define NVEC_CMD_GPIO_GETPINSCALAR							0x02
#define NVEC_CMD_GPIO_CONFIGUREEVENTREPORTINGSCALAR			0x03
#define NVEC_CMD_GPIO_ACKNOWLEDGEEVENTREPORTSCALAR			0x04
#define NVEC_CMD_GPIO_GETEVENTREPORTSCALAR 					0x06
#define NVEC_CMD_GPIO_CONFIGUREWAKESCALAR 					0x1D
#define NVEC_CMD_GPIO_SETPINVECTOR 							0x21
#define NVEC_CMD_GPIO_GETPINVECTOR							0x22
#define NVEC_CMD_GPIO_CONFIGUREEVENTREPORTINGVECTOR			0x23
#define NVEC_CMD_GPIO_ACKNOWLEDGEEVENTREPORTVECTOR			0x24
#define NVEC_CMD_GPIO_GETEVENTREPORTVECTOR 					0x26
#define NVEC_CMD_GPIO_CONFIGUREWAKEVECTOR 					0x3D

/**
 * Gpio payload data structures
 */

struct NVEC_REQ_GPIO_CONFIGUREPIN_PAYLOAD
{
    u8 					Configuration[2];     	// see NVEC_REQ_GPIO_CONFIGURATION* #define's
    u8 					LogicalPinNumber;
};

#define NVEC_REQ_GPIO_CONFIGURATION0_0_MODE_MASK                            (7<<5)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_MODE_INPUT                           (0<<5)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_MODE_OUTPUT                          (1<<5)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_MODE_TRISTATE                        (2<<5)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_MODE_UNUSED                          (3<<5)

#define NVEC_REQ_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_MASK              (7<<2)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_NONE              (0<<2)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_RISING_EDGE       (1<<2)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_FALLING_EDGE      (2<<2)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_ANY_EDGE          (3<<2)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_LO_LEVEL          (4<<2)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_HI_LEVEL          (5<<2)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_LEVEL_CHANGE      (6<<2)

#define NVEC_REQ_GPIO_CONFIGURATION0_0_PULL_MASK                            3
#define NVEC_REQ_GPIO_CONFIGURATION0_0_PULL_NONE                            0
#define NVEC_REQ_GPIO_CONFIGURATION0_0_PULL_DOWN                            1
#define NVEC_REQ_GPIO_CONFIGURATION0_0_PULL_UP                              2

#define NVEC_REQ_GPIO_CONFIGURATION0_0_OUTPUT_DRIVE_TYPE_MASK               (3<<6)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_OUTPUT_DRIVE_TYPE_PUSH_PULL          (0<<6)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_OUTPUT_DRIVE_TYPE_OPEN_DRAIN         (1<<6)

#define NVEC_REQ_GPIO_CONFIGURATION0_0_SCHMITT_TRIGGER_MASK                 (1<<5)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_SCHMITT_TRIGGER_DISABLE              (0<<5)
#define NVEC_REQ_GPIO_CONFIGURATION0_0_SCHMITT_TRIGGER_ENABLE               (1<<5)

/**
 * GPIO scalar payload data structures
 */

struct NVEC_REQ_GPIO_SETPINSCALAR_PAYLOAD
{
    u8 					DriveLevel;     	// see NVEC_GPIO_DRIVE_LEVEL* #define's
    u8 					LogicalPinNumber;
};

#define NVEC_GPIO_DRIVE_LEVEL_0_DRIVE_LEVEL_MASK                        1
#define NVEC_GPIO_DRIVE_LEVEL_0_DRIVE_LEVEL_LOGICAL_LO                  0
#define NVEC_GPIO_DRIVE_LEVEL_0_DRIVE_LEVEL_LOGICAL_HI                  1

struct NVEC_REQ_GPIO_GETPINSCALAR_PAYLOAD
{
    u8 					LogicalPinNumber;
};

struct NVEC_ANS_GPIO_GETPINSCALAR_PAYLOAD
{
    u8 DriveLevel;         // see NVEC_GPIO_DRIVE_LEVEL* #define's
};

struct NVEC_REQ_GPIO_CONFIGUREEVENTREPORTINGSCALAR_PAYLOAD
{
    u8 					ReportEnable;       // 0x0 to disable, 0x1 to enable
    u8 					LogicalPinNumber;
};

struct NVEC_REQ_GPIO_ACKNOWLEDGEEVENTREPORTSCALAR_PAYLOAD
{
    u8 					LogicalPinNumber;
};

struct NVEC_REQ_GPIO_GETEVENTREPORTSCALAR_PAYLOAD
{
    u8 					LogicalPinNumber;
};

struct NVEC_ANS_GPIO_GETEVENTREPORTSCALAR_PAYLOAD
{
    u8 TriggerStatus;     // see NVEC_ANS_GPIO_TRIGGER_STATUS* #define's
};

#define NVEC_ANS_GPIO_TRIGGER_STATUS_0_TRIGGER_STATUS_MASK                  1
#define NVEC_ANS_GPIO_TRIGGER_STATUS_0_TRIGGER_STATUS_NO_EVENT_DETECTED     0
#define NVEC_ANS_GPIO_TRIGGER_STATUS_0_TRIGGER_STATUS_EVENT_DETECTED        1

struct NVEC_REQ_GPIO_CONFIGUREWAKESCALAR_PAYLOAD
{
    u8 					WakeEnable;     // 0x0 to disable, 0x1 to enable
    u8 					LogicalPinNumber;
};

/**
 * GPIO vector payload data structures
 */

#define NVEC_GPIO_MAX_BIT_VECTOR_BYTES   24

struct NVEC_REQ_GPIO_SETPINVECTOR_PAYLOAD
{
    u8 					DriveLevel;     // see NVEC_GPIO_DRIVE_LEVEL* #define's
    u8 					PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
};

struct NVEC_REQ_GPIO_GETPINVECTOR_PAYLOAD
{
    u8 					PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
};

struct NVEC_ANS_GPIO_GETPINVECTOR_PAYLOAD
{
    u8 DriveLevelBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
};

struct NVEC_REQ_GPIO_CONFIGUREEVENTREPORTINGVECTOR_PAYLOAD
{
    u8 					ReportEnable;     // see NVEC_REQ_GPIO_REPORT_ENABLE* #define's
    u8 					PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
};

#define NVEC_REQ_GPIO_REPORT_ENABLE_0_ACTION_MASK                           0xFF
#define NVEC_REQ_GPIO_REPORT_ENABLE_0_ACTION_DISABLE                          0
#define NVEC_REQ_GPIO_REPORT_ENABLE_0_ACTION_ENABLE                           1

struct NVEC_REQ_GPIO_ACKNOWLEDGEEVENTREPORTVECTOR_PAYLOAD
{
    u8 					PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
};

struct NVEC_REQ_GPIO_GETEVENTREPORTVECTOR_PAYLOAD
{
    u8 					PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
};

struct NVEC_ANS_GPIO_GETEVENTREPORTVECTOR_PAYLOAD
{
    u8 TriggerStatusBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
};

struct NVEC_REQ_GPIO_CONFIGUREWAKEVECTOR_PAYLOAD
{
    u8 					WakeEnable;     	// see NVEC_REQ_GPIO_WAKE_ENABLE* #define's
    u8 					PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
};

#define NVEC_REQ_GPIO_WAKE_ENABLE_0_ACTION_MASK                            0xFF
#define NVEC_REQ_GPIO_WAKE_ENABLE_0_ACTION_DISABLE                         0x0
#define NVEC_REQ_GPIO_WAKE_ENABLE_0_ACTION_ENABLE                          0x1
 

/* Sleep subrequests */
#define NVEC_CMD_SLEEP_GLOBALCONFIGEVENTREPORT				0x00
#define NVEC_CMD_SLEEP_APPOWERDOWN 							0x01
#define NVEC_CMD_SLEEP_APSUSPEND 							0x02
#define NVEC_CMD_SLEEP_APRESTART							0x03
 
/**
 * Sleep payload data structures
 */

struct NVEC_REQ_SLEEP_GLOBALCONFIGEVENTREPORT_PAYLOAD
{
    u8 					GlobalReportEnable;     // see NVEC_REQ_SLEEP_GLOBAL_REPORT_ENABLE* #define's
};

#define NVEC_REQ_SLEEP_GLOBAL_REPORT_ENABLE_0_ACTION_MASK                   0xFF
#define NVEC_REQ_SLEEP_GLOBAL_REPORT_ENABLE_0_ACTION_DISABLE                0x0
#define NVEC_REQ_SLEEP_GLOBAL_REPORT_ENABLE_0_ACTION_ENABLE                 0x1
  
 
/* Keyboard subrequests */
#define NVEC_CMD_KEYBOARD_CONFIGUREWAKE 					0x03
#define NVEC_CMD_KEYBOARD_CONFIGUREWAKEKEYREPORT			0x04
#define NVEC_CMD_KEYBOARD_RESET								0xff
#define NVEC_CMD_KEYBOARD_ENABLE							0xf4
#define NVEC_CMD_KEYBOARD_DISABLE							0xf5
#define NVEC_CMD_KEYBOARD_SETSCANCODESET					0xf1
#define NVEC_CMD_KEYBOARD_GETSCANCODESET					0xf0
#define NVEC_CMD_KEYBOARD_SETLEDS							0xed
 
 
/* Keyboard payload data structures */
struct NVEC_REQ_KEYBOARD_CONFIGUREWAKE_PAYLOAD
{
    u8 					WakeEnable;     // see NVEC_REQ_KEYBOARD_WAKE_ENABLE* #define's
    u8 					EventTypes;     // see NVEC_REQ_KEYBOARD_EVENT_TYPE* #define's
};

#define NVEC_REQ_KEYBOARD_WAKE_ENABLE_0_ACTION_MASK                         0xFF
#define NVEC_REQ_KEYBOARD_WAKE_ENABLE_0_ACTION_DISABLE                      0x0
#define NVEC_REQ_KEYBOARD_WAKE_ENABLE_0_ACTION_ENABLE                       0x1

#define NVEC_REQ_KEYBOARD_EVENT_TYPE_0_SPECIAL_KEY_PRESS_MASK               (1<<1)
#define NVEC_REQ_KEYBOARD_EVENT_TYPE_0_SPECIAL_KEY_PRESS_DISABLE            (0<<1)
#define NVEC_REQ_KEYBOARD_EVENT_TYPE_0_SPECIAL_KEY_PRESS_ENABLE             (1<<1)

#define NVEC_REQ_KEYBOARD_EVENT_TYPE_0_ANY_KEY_PRESS_MASK                   0x1
#define NVEC_REQ_KEYBOARD_EVENT_TYPE_0_ANY_KEY_PRESS_DISABLE                0x0
#define NVEC_REQ_KEYBOARD_EVENT_TYPE_0_ANY_KEY_PRESS_ENABLE                 0x1

struct NVEC_REQ_KEYBOARD_CONFIGUREWAKEKEYREPORT_PAYLOAD
{
    u8 					ReportWakeKey;     	// see NVEC_REQ_KEYBOARD_REPORT_WAKE_KEY* #define's
};

#define NVEC_REQ_KEYBOARD_REPORT_WAKE_KEY_0_ACTION_MASK                     0xFF
#define NVEC_REQ_KEYBOARD_REPORT_WAKE_KEY_0_ACTION_DISABLE                  0x00
#define NVEC_REQ_KEYBOARD_REPORT_WAKE_KEY_0_ACTION_ENABLE                   0x01

struct NVEC_REQ_KEYBOARD_SETSCANCODESET_PAYLOAD
{
    u8 					ScanSet;
};

struct NVEC_AMS_KEYBOARD_GETSCANCODESET_PAYLOAD
{
    u8 ScanSet;
};

struct NVEC_REQ_KEYBOARD_SETLEDS_PAYLOAD
{
    u8 					LedFlag;     // see NVEC_REQ_KEYBOARD_SET_LEDS* #define's
};

#define NVEC_REQ_KEYBOARD_SET_LEDS_0_SCROLL_LOCK_LED_MASK     0x4
#define NVEC_REQ_KEYBOARD_SET_LEDS_0_SCROLL_LOCK_LED_ON       0x4
#define NVEC_REQ_KEYBOARD_SET_LEDS_0_SCROLL_LOCK_LED_OFF      0x0

#define NVEC_REQ_KEYBOARD_SET_LEDS_0_NUM_LOCK_LED_MASK        0x2
#define NVEC_REQ_KEYBOARD_SET_LEDS_0_NUM_LOCK_LED_ON          0x2
#define NVEC_REQ_KEYBOARD_SET_LEDS_0_NUM_LOCK_LED_OFF         0x0

#define NVEC_REQ_KEYBOARD_SET_LEDS_0_CAPS_LOCK_LED_MASK       0x1
#define NVEC_REQ_KEYBOARD_SET_LEDS_0_CAPS_LOCK_LED_ON         0x1
#define NVEC_REQ_KEYBOARD_SET_LEDS_0_CAPS_LOCK_LED_OFF        0x0

/* AuxDevice subtypes
 *
 * Note that for AuxDevice's the subtype setting contains two bit-fields which
 * encode the following information --
 * * port id on which operation is to be performed
 * * operation subtype to perform
 */

#define NVEC_CMD_AUXDEVICE_RESET						0x00
#define NVEC_CMD_AUXDEVICE_SENDCOMMAND					0x01
#define NVEC_CMD_AUXDEVICE_RECEIVEBYTES					0x02
#define NVEC_CMD_AUXDEVICE_AUTORECEIVEBYTES				0x03
#define NVEC_CMD_AUXDEVICE_CANCELAUTORECEIVE			0x04
#define NVEC_CMD_AUXDEVICE_SETCOMPRESSION				0x05
#define NVEC_CMD_AUXDEVICE_CONFIGUREWAKE				0x3d

#define NVEC_SUBTYPE_0_AUX_PORT_ID_MASK            0xC0

#define NVEC_SUBTYPE_0_AUX_PORT_ID_0               (0x0<<6)
#define NVEC_SUBTYPE_0_AUX_PORT_ID_1               (0x1<<6)
#define NVEC_SUBTYPE_0_AUX_PORT_ID_2               (0x2<<6)
#define NVEC_SUBTYPE_0_AUX_PORT_ID_3               (0x3<<6)

#define NVEC_SUBTYPE_0_AUX_PORT_SUBTYPE_MASK      0x3F

/* AuxDevice payload data structures */
struct NVEC_REQ_AUXDEVICE_SENDCOMMAND_PAYLOAD
{
    u8 					Operation;
    u8 					NumBytesToReceive;
};

struct NVEC_REQ_AUXDEVICE_RECEIVEBYTES_PAYLOAD
{
    u8 					NumBytesToReceive;
};

struct NVEC_REQ_AUXDEVICE_AUTORECEIVEBYTES_PAYLOAD
{
    u8 					NumBytesToReceive;
};

struct NVEC_REQ_AUXDEVICE_SETCOMPRESSION_PAYLOAD
{
    u8 					CompressionEnable;  // see NVEC_REQ_AUX_DEVICE_SET_COMPRESSION* #define's
};

#define NVEC_REQ_AUX_DEVICE_COMPRESSION_ENABLE_0_ACTION_MASK                0x1
#define NVEC_REQ_AUX_DEVICE_COMPRESSION_ENABLE_0_ACTION_DISABLE             0x0
#define NVEC_REQ_AUX_DEVICE_COMPRESSION_ENABLE_0_ACTION_ENABLE              0x1

struct NVEC_REQ_AUXDEVICE_CONFIGUREWAKE_PAYLOAD
{
    u8 					WakeEnable;     // see NVEC_REQ_AUX_DEVICE_WAKE_ENABLE* #define's
    u8 					EventTypes;     // see NVEC_REQ_AUX_DEVICE_EVENT_TYPE* #define's
};

#define NVEC_REQ_AUX_DEVICE_WAKE_ENABLE_0_ACTION_MASK                       0xFF
#define NVEC_REQ_AUX_DEVICE_WAKE_ENABLE_0_ACTION_DISABLE                    0x0
#define NVEC_REQ_AUX_DEVICE_WAKE_ENABLE_0_ACTION_ENABLE                     0x1

#define NVEC_REQ_AUX_DEVICE_EVENT_TYPE_0_ANY_EVENT_MASK                     0x1
#define NVEC_REQ_AUX_DEVICE_EVENT_TYPE_0_ANY_EVENT_DISABLE                  0x0
#define NVEC_REQ_AUX_DEVICE_EVENT_TYPE_0_ANY_EVENT_ENABLE                   0x1


/* Control subtypes */
#define NVEC_CMD_CONTROL_RESET								0x00
#define NVEC_CMD_CONTROL_SELFTEST							0x01
#define NVEC_CMD_CONTROL_NOOPERATION						0x02

#define NVEC_CMD_CONTROL_GETSPECVERSION						0x10
#define NVEC_CMD_CONTROL_GETCAPABILITIES					0x11
#define NVEC_CMD_CONTROL_GETCONFIGURATION					0x12
#define NVEC_CMD_CONTROL_GETPRODUCTNAME						0x14
#define NVEC_CMD_CONTROL_GETFIRMWAREVERSION					0x15

#define NVEC_CMD_CONTROL_INITIALIZEGENERICCONFIGURATION		0x20
#define NVEC_CMD_CONTROL_SENDGENERICCONFIGURATIONBYTES		0x21
#define NVEC_CMD_CONTROL_FINALIZEGENERICCONFIGURATION		0x22

#define NVEC_CMD_CONTROL_INITIALIZEFIRMWAREUPDATE			0x30
#define NVEC_CMD_CONTROL_SENDFIRMWAREBYTES					0x31
#define NVEC_CMD_CONTROL_FINALIZEFIRMWAREUPDATE				0x32
#define NVEC_CMD_CONTROL_POLLFIRMWAREUPDATE					0x33

#define NVEC_CMD_CONTROL_GETFIRMWARESIZE					0x40
#define NVEC_CMD_CONTROL_READFIRMWAREBYTES					0x41


/**
 * Control payload data structures
 */

struct NVEC_ANS_CONTROL_GETSPECVERSION_PAYLOAD
{
    u8 Version;
};

// extract 4-bit major version number from 8-bit version number
#define NVEC_ANS_SPEC_VERSION_MAJOR(x)  (((x)>>4) & 0xf)

// extract 4-bit minor version number from 8-bit version number
#define NVEC_ANS_SPEC_VERSION_MINOR(x)  ((x) & 0xf)

// assemble 8-bit version number from 4-bit major version number
// and 4-bit minor version number
#define NVEC_SPEC_VERSION(major, minor)  ((((major)&0xf) << 4) | ((minor)&0xf))

#define NVEC_SPEC_VERSION_1_0  NVEC_SPEC_VERSION(1,0)

struct NVEC_ANS_CONTROL_GETCAPABILITIES_PAYLOAD
{
    u8 Capabilities[2];     // see NVEC_ANS_CONTROL_CAPABILITIES* #define's
    u8 OEMCapabilities[2];
};

#define NVEC_ANS_CONTROL_CAPABILITIES0_0_FIXED_SIZE_EVENT_PACKET_MASK           0x10
#define NVEC_ANS_CONTROL_CAPABILITIES0_0_FIXED_SIZE_EVENT_PACKET_NOT_SUPPORTED  0x00
#define NVEC_ANS_CONTROL_CAPABILITIES0_0_FIXED_SIZE_EVENT_PACKET_SUPPORTED      0x10

#define NVEC_ANS_CONTROL_CAPABILITIES0_0_NON_EC_WAKE_MASK                       0x08
#define NVEC_ANS_CONTROL_CAPABILITIES0_0_NON_EC_WAKE_NOT_SUPPORTED              0x00
#define NVEC_ANS_CONTROL_CAPABILITIES0_0_NON_EC_WAKE_SUPPORTED                  0x08

#define NVEC_ANS_CONTROL_CAPABILITIES0_0_GENERIC_CONFIGURATION_MASK             0x01
#define NVEC_ANS_CONTROL_CAPABILITIES0_0_GENERIC_CONFIGURATION_NOT_SUPPORTED    0x00
#define NVEC_ANS_CONTROL_CAPABILITIES0_0_GENERIC_CONFIGURATION_SUPPORTED        0x01

struct NVEC_ANS_CONTROL_GETCONFIGURATION_PAYLOAD
{
    u8 Configuration[2];     // see NVEC_ANS_CONTROL_CONFIGURATION* #define's
    u8 OEMConfiguration[2];
};

#define NVEC_ANS_CONTROL_CONFIGURATION0_0_NUM_AUX_DEVICE_PORTS_MASK         0x30
#define NVEC_ANS_CONTROL_CONFIGURATION0_0_NUM_BATTERY_SLOTS_MASK            0x0F

struct NVEC_ANS_CONTROL_GETPRODUCTNAME_PAYLOAD
{
    char ProductName[NVEC_MAX_RESPONSE_STRING_SIZE];
};

struct NVEC_ANS_CONTROL_GETFIRMWAREVERSION_PAYLOAD
{
    u8 VersionMinor[2];
    u8 VersionMajor[2];
};

struct NVEC_REQ_CONTROL_INITIALIZEGENERICCONFIGURATION_PAYLOAD
{
    u8 					ConfigurationId[4];
};

struct NVEC_ANS_CONTROL_SENDGENERICCONFIGURATIONBYTES_PAYLOAD
{
    u8 NumBytes[4];
};

struct NVEC_ANS_CONTROL_SENDFIRMWAREBYTES_PAYLOAD
{
    u8 NumBytes[4];
};

struct NVEC_ANS_CONTROL_POLLFIRMWAREUPDATE_PAYLOAD
{
    u8 Flag;     // see NVEC_ANS_CONTROL_POLL_FIRMWARE_UPDATE* #define's
};

#define NVEC_ANS_CONTROL_POLL_FIRMWARE_UPDATE_0_FLAG_MASK                       0xFF
#define NVEC_ANS_CONTROL_POLL_FIRMWARE_UPDATE_0_FLAG_BUSY                       0x0
#define NVEC_ANS_CONTROL_POLL_FIRMWARE_UPDATE_0_FLAG_READY                      0x1

struct NVEC_ANS_CONTROL_GETFIRMWARESIZE_PAYLOAD
{
    u8 NumBytes[4];
};


/* Besides the default commands, there are some ODM defined ones */
/****************************************************************************/

#define NVODM_BATTERY_NUM_BATTERY_SLOTS_MASK 0x0F

/* Battery Slot Status and Capacity Gauge Report */
/* Data Byte 3 : Battery Slot Status */
#define NVODM_BATTERY_SLOT_STATUS_DATA  0
/*
 * Data Byte 4 : Battery Capacity Gauge :
 * Battery's relative remaining capacity in %
 */
#define NVODM_BATTERY_CAPACITY_GAUGE_DATA  1

/*
 * Battery Slot Status :
 * Bit 0 = Present State:
 * 1 = Battery is present in the respective slot
 */
#define NVODM_BATTERY_PRESENT_IN_SLOT  0x01

#define NVODM_BATTERY_CHARGING_STATE_SHIFT  1
#define NVODM_BATTERY_CHARGING_STATE_MASK   0x03

/* Battery Slot Status : Bits 1-2 = Charging state */
#define NVODM_BATTERY_CHARGING_STATE_IDLE         0x00
#define NVODM_BATTERY_CHARGING_STATE_CHARGING     0x01
#define NVODM_BATTERY_CHARGING_STATE_DISCHARGING  0x02
#define NVODM_BATTERY_CHARGING_STATE_RESERVED     0x03

/* Remaining capacity alarm bit is 3rd in slot status */
#define NVODM_BATTERY_REM_CAP_ALARM_SHIFT 3
#define NVODM_BATTERY_REM_CAP_ALARM_IS_SET 1

/* Response System Status : Data Byte 3 System State Bits 7-0 */
#define NVODM_BATTERY_SYSTEM_STATE_DATA1 0
/* Response System Status : Data Byte 4 System State Bits 15-8 */
#define NVODM_BATTERY_SYSTEM_STATE_DATA2 1
/* System State Flags : AC Present : System State Bit 0 */
#define NVODM_BATTERY_SYSTEM_STATE_AC_PRESENT 0x01

#define NVODM_BATTERY_CHARGING_RATE_DATA_BYTES 3
#define NVODM_BATTERY_CHARGING_RATE_UNIT 3

/* Threshold for battery status.*/
#define NVODM_BATTERY_FULL_VOLTAGE_MV      12600
#define NVODM_BATTERY_HIGH_VOLTAGE_MV      10200
#define NVODM_BATTERY_LOW_VOLTAGE_MV       10000
#define NVODM_BATTERY_CRITICAL_VOLTAGE_MV   9500

#define NVODM_BATTERY_EC_FIRMWARE_VER_R01 2
#define NVODM_BATTERY_EC_FIRMWARE_VER_R04 8

/* Bit 0 = Present State event */
/* Bit 1 = Charging State event */
/* Bit 2 = Remaining Capacity Alarm event */
#define NVODM_BATTERY_SET_PRESENT_EVENT       0x01
#define NVODM_BATTERY_SET_CHARGING_EVENT      0x02
#define NVODM_BATTERY_SET_REM_CAP_ALARM_EVENT 0x04

/*
 * Bit 0   => 0=Not Present, 1=Present
 * Bit 1:2 => 00=Idle, 01=Charging,10=Discharging, 11=Reserved
 * Bit 3   => 1=Remaining Capacity Alarm set
 */
#define NVODM_BATTERY_EVENT_MASK 0x0F


#endif
