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

/* ################# BIG FAT WARNING #####################
   #  This driver will start malfunctioning if you       #
   # enable debugging messages. The reason is that the   #
   # ISR has critical timing requirements, due to a bug  #
   # in the Tegra2 Slave mode i2c hardware.The dev_dbg() #
   # calls create additional delays that cause the i2c   #
   # reception to hang (hardware bug). So, enabling      #
   # debugging messages is not recommended, unless       #
   # specifically needed - But, if enabled, results won't#
   # accurately reflect what is happening in reality     #
   ####################################################### */

/* #define DEBUG */

#include <linux/io.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/clk.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <asm/irq.h>
#include <asm/atomic.h>
#include <mach/iomap.h>
#include <mach/clk.h>
#include <linux/mfd/nvec.h>

/*
 * interface for communicating with  an Embedded Controller (EC).
 *
 * The EC Interface (ECI) handles communication of packets the AP and EC.
 *
 * Three types of packets are supported --
 *
 * * Request Packets -- sent from AP to EC
 * * Response Packets -- sent from EC to AP
 * * Event Packets -- sent from EC to AP
 *
 * There is a one-to-one correspondence between Request Packets and Response
 * Packets.  For every Request Packet sent from the AP to the EC, there will be
 * one and only one corresponding Response Packet sent from the EC back to the
 * AP.
 *
 * Event Packets, on the other hand, are unsolicited and can be sent by the 
 * EC at any time.
 *
 * See below for detailed information about the format and content of the
 * packet types.
 * 
 *   The first element of any packet is the packet type, so given any
 * unknown packet it is possible to determine its type (request, response, or
 * event).  From there, the remainder of the packet can be decoded using the
 * structure definition
 *
 * For example, a keyboard request would have a packet type of request/response 
 * (NVEC_CMD) type of Keyboard.  The response to a keyboard request
 * would have a packet type of Response and a request/response type of Keyboard.
 * Finally, a keyboard event would have a packet type of Event and an event type
 * of Keyboard.
 *
 * Request operations are specified as a combination of a request type and a
 * request sub-type.  Since every request has a corresponding response, requests
 * and responses have a common set of types and sub-types.
 *
 * There is a separate set of types for event packets, and events do not have a
 * sub-type.  
 */ 


/**
 * Command field definition
 *
 * PACKET_TYPE identifies the packet as either a request/response or
 * as an event.  Requests and responses are distinguished by context.
 *
 * If the PACKET_TYPE is EVENT, then the type of event can be determined
 * from the EVENT_TYPE field.
 *
 * If the PACKET_TYPE is CMD, then the request/response
 * type can be determined from the CMD_TYPE field.
 */
#define NVEC_COMMAND_0_PACKET_TYPE_MASK               0x80
#define NVEC_COMMAND_0_PACKET_TYPE_CMD                0x00
#define NVEC_COMMAND_0_PACKET_TYPE_EVENT              0x80

#define NVEC_COMMAND_0_EVENT_LENGTH_MASK              0x60

#define NVEC_COMMAND_0_EVENT_LENGTH_FIXED_2BYTE       0x00
#define NVEC_COMMAND_0_EVENT_LENGTH_FIXED_3BYTE       0x20
#define NVEC_COMMAND_0_EVENT_LENGTH_VARIABLE          0x40
#define NVEC_COMMAND_0_EVENT_LENGTH_RESERVED          0x60

#define NVEC_COMMAND_0_ERROR_FLAG_MASK                0x10

#define NVEC_COMMAND_0_EVENT_TYPE_MASK                0x0F

#define NVEC_COMMAND_0_CMD_TAG_MASK                   0x70
#define NVEC_COMMAND_0_CMD_TAG_SHIFT                  4
#define NVEC_COMMAND_0_CMD_TYPE_MASK                  0x0F


/**
 * SMBus transaction format for Request Packet
 *
 * Request Packets are always sent using the SMBus Block Read operation
 *
 * SMBus byte field   Packet Content
 * ----------------   ----------------------------------------------------------
 * Command Code       Must be 0x1.  This value indicates to that the Block Read
 *                    is directed to the EC interface.
 *
 *                    Note: the Command Code is checked and discarded by the
 *                          lowest-level SMBus transport code (for Block Reads)
 *
 * Byte Count         number of remaining bytes in transfer
 *                    SMBus spec requires Byte Count be nonzero, which is
 *                    guaranteed by the required presence of the Command and
 *                    SubType data in Data Byte 1 - 2
 *
 * Data Byte 1        PacketType, RequestType, and RequestorTag; see details in
 *                    Command Field Definition section above
 *
 * Data Byte 2        SubType
 *
 * Data Byte 3 - N    Payload
 */


/**
 * SMBus transaction format for Response Packet
 *
 * Response Packets are always sent using the SMBus Block Write operation
 *
 * SMBus byte field   Packet Content
 * ----------------   ----------------------------------------------------------
 * Command Code       PacketType, RequestType, and RequestorTag; see details in
 *                    Command Field Definition section above
 *
 * Byte Count         number of remaining bytes in transfer 
 *                    SMBus spec requires Byte Count be nonzero, which is
 *                    guaranteed by the required presence of the SubType amd
 *                    Status data in Data Byte 1 - 2
 *
 * Data Byte 1        SubType
 *
 * Data Byte 2        Status
 *
 * Data Byte 3 - N    Payload
 */


/**
 * SMBus transaction format for Event Packet
 *
 * Event Packets can be sent using the SMBus Block Write, SMBus Write Byte or
 * SMBus Write Word operation
 *
 * Event Packet sent using the SMBus Block Write operation --
 *
 * SMBus byte field   Packet Content
 * ----------------   ----------------------------------------------------------
 * Command Code       PacketType and EventType; see details in Command Field
 *                    Definition section above
 *
 * Byte Count         number of remaining bytes in transfer
 *                    SMBus spec requires Byte Count be nonzero
 *
 * Data Byte 1 - N    Payload; Note that if the ERROR_FLAG is set in the Command
 *                    Field, then the first byte of the Payload is interpreted
 *                    as a Status value
 *
 *
 * Event Packet sent using the SMBus Write Byte operation
 *
 * SMBus byte field   Packet Content
 * ----------------   ----------------------------------------------------------
 * Command Code       PacketType, NumPayloadBytes, and EventType; see details in
 *                    Command Field Definition section above
 *
 * Data Byte 1        Payload; Note that if the ERROR_FLAG is set in the Command
 *                    Field, then the Payload is interpreted as a Status value
 *
 *
 * Event Packet sent using the SMBus Write Word operation --
 *
 * SMBus byte field   Packet Content
 * ----------------   ----------------------------------------------------------
 * Command Code       PacketType, NumPayloadBytes, and EventType; see details in
 *                    Command Field Definition section above
 *
 * Data Byte 1 - 2    Payload; Note that if the ERROR_FLAG is set in the Command
 *                    Field, then the first byte of the Payload is interpreted
 *                    as a Status value
 */

/* I2C register definitions for Slave mode in Tegra */
#define I2C_CNFG			0x00
#define I2C_CNFG_PACKET_MODE_EN		(1<<10)
#define I2C_CNFG_NEW_MASTER_SFM		(1<<11)
#define I2C_CNFG_DEBOUNCE_CNT_SHIFT	12

#define I2C_SL_CNFG		0x20
#	define I2C_SL_NEWL		(1<<2) 	/* New slave */
#	define I2C_SL_NACK		(1<<1)	/* Disable slave Ack */
#	define I2C_SL_RESP		(1<<0)	/* Slave response to general call address */

#define I2C_SL_RCVD			0x24

#define I2C_SL_STATUS		0x28
#	define END_TRANS		(1<<4)	/* Transaction completed, as indicated by Stop/repeat start condition */
#	define I2C_SL_IRQ		(1<<3)	/* Interrupt was generated by slave */
#	define RCVD				(1<<2)	/* New transaction received */
#	define RNW				(1<<1)	/* Slave transaction status: 0=Write, 1=Read */

#define I2C_SL_ADDR1		0x2c
#define I2C_SL_ADDR2		0x30
#define I2C_SL_DELAY_COUNT	0x3c

#define NVEC_CMD_TIMEOUT 				700		/* Timeout for NvEC commands in ms - NVEC usually takes 70ms to answer - but up to 700 can be */
#define NVEC_MINIMUM_TIME_BETWEEN_CMDS 	 10		/* Minimum time between commands to NvEC to let it stabilize in ms */

struct nvec_cmd {
	struct list_head 	node; 				/* linked list node to queue evemt packets */

	/* TX payload */
	union {
		struct {
			u8 			size;				/* count of bytes to send (2+ size(data) */
			u8			cmd;				/* Command + tag */
			u8			subcmd;				/* Subcommand */
			u8 			data[NVEC_MAX_MSG_SZ];	/* data */
		};
		u8				raw[NVEC_MAX_MSG_SZ+3];
	} tx;

	/* RX payload */
	union {
		/* Command response */
		struct {
			u8			cmd;				/* Command + tag */					
			u8 			size;				/* count of bytes received (2+size(data) */
			u8			subcmd;				/* Subcommand */
			u8			status;				/* Status */
			u8 			data[NVEC_MAX_MSG_SZ];	/* data */
		};
		u8				raw[NVEC_MAX_MSG_SZ+4];
	} rx;
};


struct nvec_chip {
	struct device *		dev;
	int 				gpio;
	int 				irq;
	bool				suspended;			/* If the nvec is suspended or not */	
	
	int 				i2c_addr;
	void __iomem *		i2c_regs;
	struct clk *		i2c_clk;
	bool 				i2c_enabled;		/* If i2c slave is enabled or not: 0=disabled*/

	struct mutex		cmdmtx;				/* The mutex used to serialize commands */
	unsigned long		nextcmdtime;		/* The time the next command can be sent */
	
	spinlock_t 			lock;				/* Spinlock used to protect the lists */
	
	volatile struct nvec_cmd * tx;			/* Message to transmit: Autoset to NULL once it is transmitted */
	volatile struct nvec_cmd * rx;			/* Message to receive: Autoset to NULL once it is transmitted */
	
	struct nvec_cmd *	msg;				/* Message being transmitted */
	int					tx_pos;				/* Next byte to transmit */
	int 				tx_size;			/* Count of bytes to tx */
	int					rx_pos;				/* Next byte to receive */
	struct completion 	rx_done;			/* Signaled when message RX is done */
	
	struct nvec_cmd		scratch;			/* Scratch message to send when no other message is ready or teporary storage while receiving */	 
	
	struct list_head 	ev_toprocess;		/* List of messages to process */
	struct nvec_event	ev_pool[8];			/* Pool of messages used to receive events */
	volatile long unsigned int ev_allocd;	/* bitmap of allocated event messages */
	struct work_struct 	ev_work;			/* Call of list of event handlers */
	
	int					smbus_state;		/* Step into the state machine to handle SMBus slave communications */
	
	struct blocking_notifier_head 	ev_notifier_list;
	
};

/* Allocate an event message - callable from isr. It can return NULL
   if no message is available */
static struct nvec_event* nvec_alloc_ev_msg(struct nvec_chip *nvec)
{
	int i;
	
	/* Look for a free event message */
	for (i = 0; i < ARRAY_SIZE(nvec->ev_pool); i++) {
	
		/* Try to reserve it... did we succeed ? */
		if (!test_and_set_bit(i, &nvec->ev_allocd)) {
		
			/* Got it... It is already reserved ! - Return the message */
			struct nvec_event* ev = &nvec->ev_pool[i];
			
			/* Fill in the message id (required to free it) */
			ev->id = i;
			
			/* initialize the head node */
			INIT_LIST_HEAD(&ev->node);
			
			/* Return it */
			return ev;
		}
	}
	
	/* No message available */
	return NULL;
}

/* Release an allocated tag - It can be called from an ISR context safely */
static void nvec_free_ev_msg(struct nvec_chip *nvec,struct nvec_event* ev)
{
	/* Free event */
	clear_bit(ev->id, &nvec->ev_allocd);
}

/* Event worker */
static void nvec_ev_dispatch(struct work_struct *work)
{
	unsigned long flags;
	struct nvec_chip *nvec = container_of(work, struct nvec_chip, ev_work);
	
	while (1) {
		struct nvec_event *ev = NULL;
	
		/* Take spinlock to avoid concurrent modifications */
		spin_lock_irqsave(&nvec->lock,flags);

		/* If list is not empty ... */
		if (!list_empty(&nvec->ev_toprocess)) {

			/* Get a message from the list */
			ev = list_first_entry(&nvec->ev_toprocess, struct nvec_event, node);
			
			/* Remove the message from the list of events to process */
			list_del_init(&ev->node);
			
		}

		/* release lock */
		spin_unlock_irqrestore(&nvec->lock,flags);

		/* If no more events, we are done */
		if (ev == NULL) {
			break;
		}
		
		/* We got an event to process - Call the event handler */
		blocking_notifier_call_chain(&nvec->ev_notifier_list,ev->ev & NVEC_COMMAND_0_EVENT_TYPE_MASK,ev);
		
		/* Finally, release the event */
		nvec_free_ev_msg(nvec,ev);
		
	};
} 

/*
 * Slave always acks master, if it is ready to receive data.
 * If Slave is not ready to receive/send data, it nacks.
 * This driver supports
 *      1.Write Block(To receive Response/Event from Master).
 *      2.Write Byte-Read Block(To Send Request to Master).
 *      3.Write Byte(To receive 1 byte Event from Master).
 *      4.Write Word(To receive word Event from Master).
 *
 * The isr 
 */
 
/*
 This is the expected flow of communication 
 
 !RNW[ST]:m0x84 !RNW:m0x01 RNW[ST]:sCOUNT .... RNW[SP]  | SMBus block read
	->Used by NvEC to request commands
 !RNW[ST]:m0x84 !RNW:mCMD  !RNW:mCOUNT .... !RNW[SP]    | SMBus block write
	->Used by NvEC to send responses or events
 !RNW[ST]:m0x84 !RNW:mCMD  !RNW:mData !RNW[SP] 		 	| SMBus Write Byte
	->Used by NvEC to send events
 !RNW[ST]:m0x84 !RNW:mCMD  !RNW:mDLO  !RNW:mDHI !RNW[SP]| SMBus Write Word
	->Used by NvEC to send events
 
 
 RNW signals Master is requesting data from Slave. i.e Read from Slave.
 ST = RCVD signals a start bit was detected
 SP =END_TRANS signals a stop bit was detected
 
 We will use a finite state machine to ID and follow the protocol
 
 */

static irqreturn_t i2c_interrupt(int irq, void *dev)
{
	struct nvec_chip *nvec = (struct nvec_chip *)dev;
	
	unsigned int status;
	unsigned int received = 0;
	unsigned char to_send = 0xff; /* release line by default */
	
	/* Expected interrupt mask */
	unsigned int irq_mask = I2C_SL_IRQ | END_TRANS | RCVD | RNW;
	
	unsigned char *i2c_regs = nvec->i2c_regs;
	unsigned long flags;

	struct timespec start_time, end_time, diff_time;
	s64 dtime;

	/* Get the time of the processing */
    getnstimeofday(&start_time);

	/* Read the slave status register. */
	status = readl(i2c_regs + I2C_SL_STATUS);
	dev_dbg(nvec->dev, "irq status: 0x%08x\n", status);

	/* Validate interrupts */
	if (!(status & irq_mask) && !((status & ~irq_mask) == 0)) {
		dev_err(nvec->dev, "unexpected irq mask 0x%08x\n", status);
		return IRQ_HANDLED;
	}
	
	/* Filter out spurious IRQs */
	if ((status & I2C_SL_IRQ) == 0) {
		dev_err(nvec->dev, "Spurious IRQ\n");
		return IRQ_HANDLED;
	}

	/* Read received data if required */
	if ((status & (RNW|END_TRANS)) == 0 ||
		(status & (RNW|END_TRANS|RCVD)) == (END_TRANS|RCVD) /* back to back transfers */
		) {
		
		/* Master wrote to us, and it is not the end of the write */
		if (status & RCVD) { 
			/* Received the first byte from the master of a new transmission */
		
			local_irq_save(flags);
			/* Read data byte to release the bus: In this case, the address */
			received = readl(i2c_regs + I2C_SL_RCVD);
			/* Workaround for AP20 New I2C Slave Controller bug #626607. */
			writel(0, i2c_regs + I2C_SL_RCVD);
			local_irq_restore(flags);
			
		} else {
		
			/* Read data byte to release the bus */
			received = readl(i2c_regs + I2C_SL_RCVD);
		}
		dev_dbg(nvec->dev, "receiving 0x%02x\n", received);		
	}

	/* If we detected the start of a transaction, restart the 
	   reception state to 0 */
	if (status == (I2C_SL_IRQ|RCVD)) { /* Start of transaction */
		dev_dbg(nvec->dev, "Start of transaction: going to state 0\n");
		nvec->smbus_state = 0;
	}
	
	/* Execute the state machine required to handle slave communications 
	   using SMBus protocol */
	switch (nvec->smbus_state) {
	case 0: 
	
		/* Waiting for the start of the SMBus protocol */
		dev_dbg(nvec->dev, "State 0: Waiting for SMBus start\n");
		
		/* Did we get a write from the master with an start bit ? */
		if (status == (I2C_SL_IRQ|RCVD) 			/* Start of transaction */
			) {
			
			/* Check received SMBus addr */
			if (received != nvec->i2c_addr) {
				dev_err(nvec->dev, 
					"unexpected SMBus address: Got 0x%02x, expected 0x%02x\n",received,nvec->i2c_addr);
			} else {
				/* Address was OK, jump to state 1 */
				dev_dbg(nvec->dev, "SMBus address matches, jump to state 1\n");
				nvec->smbus_state = 1;
			
			}
			
		} else {
			/* Unexpected flags - clean up mess */
			dev_err(nvec->dev, "unexpected flags 0x%02x: Keeping in state 0 [1]\n",status);
		}
		break;
		
	case 1: /* Waiting for command */
	
		dev_dbg(nvec->dev, "State 1: Waiting for SMBus command\n");

		/* Did we get a write from the master without an start bit ? */
		if (status != (I2C_SL_IRQ)) {
		
			/* No, sync error. Restart parsing */
			dev_err(nvec->dev, "unexpected flags 0x%02x: Jump to state 0 [2]\n",status);
			nvec->smbus_state = 0;

		} else {
		
			/* Flags OK - store command for later verification */
			nvec->scratch.rx.cmd = received;
			
			/* Jump to state 2 */
			dev_dbg(nvec->dev, "Flags matching, jump to state 2\n");
			nvec->smbus_state = 2;
			
		}
		break;
		
	case 2: /* Try to differenciate between SMBus transactions */
		
		dev_dbg(nvec->dev, "State 2: Try to differenciate between transactions\n");

		if (status == (I2C_SL_IRQ|RNW|RCVD) /* Read from slave */
			) {
			/* SMBus block read */
			
			/* Work around for AP20 New Slave Hw Bug.
               ((1000 / 80) / 2) + 1 = 33uS - This time is CRITICAL,
			   otherwise, Hw stops working! */
            getnstimeofday(&end_time);
            diff_time = timespec_sub(end_time, start_time);
            dtime = timespec_to_ns(&diff_time);
            if (dtime < 33000)
                ndelay(33000 - dtime);
            else
                dev_warn(nvec->dev, "isr time: %llu nsec\n", timespec_to_ns(&diff_time));			
			
			/* Verify that command was 0x01, or else, it is an invalid request */
			if (nvec->scratch.rx.cmd != 0x01) {
			
				dev_err(nvec->dev, "Invalid command for a SMBus block read. Jumping to state 0\n");
				nvec->smbus_state = 0;
				
			} else {
				struct nvec_cmd *msg;
				
				dev_dbg(nvec->dev, "Detected an SMBus block read: Jumping to state 3\n");
				nvec->smbus_state = 3;

				/* Get the message to send */
				msg = (struct nvec_cmd *) nvec->tx;

				/* If no message to send... */
				if (msg == NULL) {
					/* Use the scratch message to send a no-op message */

					dev_dbg(nvec->dev, "empty tx - sending no-op to resync\n");

					msg = &nvec->scratch;
#if 0
					msg->tx.size   = 5;
					msg->tx.cmd    = NVEC_CMD_SLEEP;
					msg->tx.subcmd = 0x8a;
					msg->tx.data[0] = 0x02;
					msg->tx.data[1] = 0x07;
					msg->tx.data[2] = 0x02;
#else
					msg->tx.size   = 2;
					msg->tx.cmd    = NVEC_CMD_CONTROL;
					msg->tx.subcmd = NVEC_CMD_CONTROL_NOOPERATION;
#endif
				}
				
				/* Store the new message to send */
				nvec->msg = msg;

				/* Start from the 1st byte and compute total size */
				nvec->tx_pos = 0;
				nvec->tx_size = msg->tx.size + 1; // +1 to send all the packet including the size field
				
				/* Send the next byte - if no request is pending, just
				   send 0xFF's to release bus and clear intr. */
				if (nvec->tx_pos < nvec->tx_size) {
					to_send = msg->tx.raw[nvec->tx_pos++];
				} else {
					dev_err(nvec->dev, "tx buffer underflow, sending 0xFF\n");
				}
			}

		} else 
		if (status == (I2C_SL_IRQ)) {

			dev_dbg(nvec->dev, "Could be SMBus block write,SMBus word write or SMBus byte write\n");

			/* Store it for later discrimination - saving the command already received */
			nvec->scratch.rx.raw[1] = received;
			nvec->rx_pos = 2;
				
			dev_dbg(nvec->dev, "stored as 1st byte: Jump to state 4\n");
			
			/* Jump to the state 4 */
			nvec->smbus_state = 4;

		} else {
		
			/* No, sync error. Restart parsing */
			dev_err(nvec->dev, "unexpected flags 0x%02x: Jump to state 0 [3]\n",status);
			nvec->smbus_state = 0;
			
		}
		
		/* De-assert the Gpio Line here - NvEC started to query us */
		gpio_set_value(nvec->gpio, 1);
		dev_dbg(nvec->dev, "gpio -> high\n");
		
		break;
		
	case 3: /* On a SMBus block read */

		dev_dbg(nvec->dev, "State 3: SMBus block read\n");
		if (status == (I2C_SL_IRQ|RNW)) {
		
			/* Send the next byte - if no request is pending, just
			   send 0xFF's to release bus and clear intr. */
			if (nvec->msg != NULL && nvec->tx_pos < nvec->tx_size) {
				to_send = nvec->msg->tx.raw[nvec->tx_pos++];
			} else {
				dev_err(nvec->dev, "tx buffer underflow, sending 0xFF\n");				
			}
			
		} else 
		if (status == (I2C_SL_IRQ|RNW|END_TRANS) ||
			status == (I2C_SL_IRQ|RCVD|END_TRANS) /* back to back transfers */
			) {
		
			dev_dbg(nvec->dev, "SMBus block read end\n");
			
			/* Send the next byte - if no request is pending, just
			   send 0xFF's to release bus and clear intr. */
			if (nvec->msg != NULL) {
			
				/* If everything was transferred ... */
				if (nvec->tx_pos >= nvec->tx_size) { 
				
					dev_dbg(nvec->dev, "everything transfered - msg unqueued (sent:%d,req:%d)\n",nvec->tx_pos, nvec->tx_size);

					/* Make sure once the user command is sent, it is not sent anymore */
					nvec->tx = NULL;
									
				} else {
				
					dev_err(nvec->dev, "received premature END_TRANS - Retransmitting command\n");
					nvec->tx_pos = 0;
					nvec->smbus_state = 0;
				}
			}

			/* If dealing with a back-to-back transfer, handle it */
			if (status == (I2C_SL_IRQ|RCVD|END_TRANS)) { /* back to back transfers */

				/* Check received SMBus addr */
				if (received != nvec->i2c_addr) {
					dev_err(nvec->dev, 
						"Back to Back Xfer: unexpected SMBus address: Got 0x%02x, expected 0x%02x, going to state 0\n",received,nvec->i2c_addr);
					nvec->smbus_state = 0;
					
				} else {
					/* Address was OK, jump to state 1 */
					dev_dbg(nvec->dev, "Back to Back Xfer: SMBus address matches, jump to state 1\n");
					nvec->smbus_state = 1;
				
				}
			
			} else {
			
				/* Jump to state 0, preparing to get new commands */
				nvec->smbus_state = 0;
			}
			
		} else {
		
			dev_err(nvec->dev, "unexpected flags 0x%02x: Jump to state 0 [4]\n",status);
			nvec->smbus_state = 0;
			
		}
		break;
		
	case 4: /* Receiving SMBus writes */
	
		dev_dbg(nvec->dev, "State 4: SMBus block write,SMBus word write or SMBus byte write\n");
		
		if (status == (I2C_SL_IRQ)) {

			/* Store received byte */
			if ( nvec->rx_pos >= ARRAY_SIZE(nvec->scratch.rx.raw) || /* out of bounds */
				(nvec->rx_pos > 2 && nvec->rx_pos >= (nvec->scratch.rx.size + 2)) /* out of assumed size */
			) {
				dev_err(nvec->dev, "too much bytes received: max:%d\n", nvec->scratch.rx.size+2 );
			} else {
				dev_dbg(nvec->dev, "storing 0x%02x at %d of %d total\n", received,nvec->rx_pos,(nvec->scratch.rx.size + 2));				
				nvec->scratch.rx.raw[nvec->rx_pos] = received;
				nvec->rx_pos++;
			}
			
		} else
		if (status == (I2C_SL_IRQ|END_TRANS)) {

			/* End of write. Decide, based on size, the kind of SMBus transaction */
			if (nvec->rx_pos == 2) {
				dev_dbg(nvec->dev, "completed a SMBus write byte\n");
			} else
			if (nvec->rx_pos == 3) {
				dev_dbg(nvec->dev, "completed a SMBus write word\n");
			} else {
				dev_dbg(nvec->dev, "completed a SMBus write block\n");
				/* The expected length must match the received len */
				if (nvec->rx_pos != (nvec->scratch.rx.size + 2)) {
					dev_err(nvec->dev, "incorrect count of bytes: Expected: %d, Got: %d\n",(nvec->scratch.rx.size + 2),nvec->rx_pos);
				}
			}
			
			/* Process the received packet */
			
			/* Did we receive an event ? */
			if (
				(nvec->scratch.rx.cmd & 
					NVEC_COMMAND_0_PACKET_TYPE_MASK) == 
						NVEC_COMMAND_0_PACKET_TYPE_EVENT) {
				struct nvec_event *rxev = NULL;							
						
				dev_dbg(nvec->dev, "got an event 0x%02x\n", nvec->scratch.rx.raw[0] & NVEC_COMMAND_0_EVENT_TYPE_MASK);
						
				/* Yes, allocate an event message */
				rxev = nvec_alloc_ev_msg(nvec);
				if (rxev == NULL) {
				
					dev_err(nvec->dev,"no slot for event available - dropping it\n");
					
				} else {
					int payldLen = 0, payldPos = 1;
					/* Interpret the event: */
					
					/* Event type */
					rxev->ev = nvec->scratch.rx.raw[0] & NVEC_COMMAND_0_EVENT_TYPE_MASK;
					
					/* Decode event length and payload position */
					switch (nvec->scratch.rx.raw[0] & NVEC_COMMAND_0_EVENT_LENGTH_MASK) {
					case NVEC_COMMAND_0_EVENT_LENGTH_FIXED_2BYTE:
						payldPos = 1;
						payldLen = 1;
						break;
					case NVEC_COMMAND_0_EVENT_LENGTH_FIXED_3BYTE:
						payldPos = 1;
						payldLen = 2;
						break;
					case NVEC_COMMAND_0_EVENT_LENGTH_VARIABLE:
						payldPos = 2;
						payldLen = nvec->scratch.rx.raw[1];
						break;
					}

					/* Assume no failure */
					rxev->status = NVEC_STATUS_SUCCESS;
					
					/* If it is an error event, get status */
					if (nvec->scratch.rx.raw[0] & NVEC_COMMAND_0_ERROR_FLAG_MASK) {
						payldLen--;
						rxev->status = nvec->scratch.rx.raw[payldPos++];
					}

					/* Limit payload length to the size of the buffer */
					if (payldLen > ARRAY_SIZE(rxev->data)) {
						payldLen = ARRAY_SIZE(rxev->data);
					}
					
					/* Copy the payload data */
					rxev->size = payldLen;
					memcpy(&rxev->data[0],&nvec->scratch.rx.raw[payldPos],payldLen);

					/* Take spinlock to avoid concurrent modifications */
					spin_lock_irqsave(&nvec->lock,flags);
					
					/* And add it to the to process list */
					list_add_tail(&rxev->node, &nvec->ev_toprocess);

					/* release lock */
					spin_unlock_irqrestore(&nvec->lock,flags);
					
					/* Schedule processing of events */
					schedule_work(&nvec->ev_work);
					
				}

			} else {
				struct nvec_cmd *rxmsg = (struct nvec_cmd *)nvec->rx;
				
				dev_dbg(nvec->dev, "got an command 0x%02x\n", nvec->scratch.rx.cmd & NVEC_COMMAND_0_CMD_TYPE_MASK);
				
				/* Not an event. Just look for the associated RX entry and
					copy the info to that slot */
					
				if (rxmsg == NULL) {
				
					dev_err(nvec->dev,"sent command without associated answer\n");
					
				} else {
					
					/* Copy the received answer there */
					memcpy(&rxmsg->rx.raw[0],&nvec->scratch.rx.raw[0],2+nvec->scratch.rx.size); // +2 to copy all the packet including size and command fields

					/* And signal the reception is complete */
					complete(&nvec->rx_done);
					
					/* Packet received - Make sure you don't receive it again */
					nvec->rx = NULL;
				} 
			}
			
			/* Calculate the time the next command can be sent, in order to enforce time
			   between commands and let the NVEC calm down */
			nvec->nextcmdtime = jiffies + msecs_to_jiffies(NVEC_MINIMUM_TIME_BETWEEN_CMDS);
			
			/* Jump to receive new commands */
			nvec->smbus_state = 0;	
			
		} else {
			dev_err(nvec->dev, "unexpected flags 0x%02x: Jump to state 0 [5]\n",status);
			nvec->smbus_state = 0;	
			
		}
		break;
		
	default:
		nvec->smbus_state = 0;
	}

	/* Write data if required to complete transaction */
	if ((status & (RNW|END_TRANS)) == RNW) {
		dev_dbg(nvec->dev, "sending 0x%02x\n", to_send);
		writel(to_send, i2c_regs + I2C_SL_RCVD);
	}

	return IRQ_HANDLED;
}

/* Enable the i2c slave mode in Tegra */
static void tegra_enable_i2c_slave(struct nvec_chip* nvec)
{
	unsigned char *i2c_regs = nvec->i2c_regs;
	struct clk* i2c_clk = nvec->i2c_clk;

	/* If already enabled, avoid reenabling it */
	if (nvec->i2c_enabled)
		return;
	
	clk_enable(i2c_clk);
	
	tegra_periph_reset_assert(i2c_clk);
	udelay(2);
	tegra_periph_reset_deassert(i2c_clk);

	/* It seems like the I2C Controller has an hidden clock divider,
       whose value is 8. So, request for clock value multipled by 8. 
	   We use 80khz */
	clk_set_rate(i2c_clk, 8*80000);
	
	/* Set the slave address and 7-bit address mode. */
	writel(nvec->i2c_addr>>1, i2c_regs + I2C_SL_ADDR1);
	writel(0, i2c_regs + I2C_SL_ADDR2);

	/* Set Delay count register */
    writel(0x1E, i2c_regs + I2C_SL_DELAY_COUNT);
	
	/* Enable NEW_MASTER_FSM in slave for T20
       It is found that in some corner case, it appears that the slave is
       driving '0' on the bus and the HW team suggested to enable new master
       even if it is not used as old master is known to go into
       bad state */
	writel(I2C_CNFG_NEW_MASTER_SFM, i2c_regs + I2C_CNFG);
	
	/* Enable Ack and disable response to general call. Enable new Slave */
	writel(I2C_SL_NEWL, i2c_regs + I2C_SL_CNFG);

	/* Enable IRQ */
	enable_irq(nvec->irq);

	/* I2C slave is enabled right now */
	nvec->i2c_enabled = true;
}

/* Disable the i2c slave mode in Tegra */
static void tegra_disable_i2c_slave(struct nvec_chip* nvec)
{
	unsigned char *i2c_regs = nvec->i2c_regs;
	struct clk* i2c_clk = nvec->i2c_clk;

	/* If already disabled, avoid redisabling it */
	if (!nvec->i2c_enabled) 
		return;
	
	/* Disable IRQ */
	disable_irq(nvec->irq);
	
    /* Disable Ack and disable response to general call. */
	writel(I2C_SL_NEWL | I2C_SL_NACK, i2c_regs + I2C_SL_CNFG);
	
	clk_disable(i2c_clk);
	
	/* I2C is disabled right now */
	nvec->i2c_enabled = false;
}


/* write a command, and wait for the answer - The function may sleep, so
   it must not be called from a ISR context */
static int nvec_msg_xfer(struct nvec_chip *nvec,struct nvec_cmd* msg)
{
	unsigned long flags;
	int res,tries,diff;
	
	/* Get the command mutex, to make sure just one command is pending */
	mutex_lock(&nvec->cmdmtx);

	/* Prepare the header to queue the request and wait for the response */
	INIT_LIST_HEAD(&msg->node);
	init_completion(&nvec->rx_done);

	/* Take spinlock to protect against concurrent access */
	spin_lock_irqsave(&nvec->lock,flags);

	/* Set the packet to TX and to RX - We are sure noone is using it */
	nvec->tx = msg;
	nvec->rx = msg;

	/* Release spinlock */
	spin_unlock_irqrestore(&nvec->lock,flags);	

	/* Check if we can kick the command right now */
	diff = (int)(nvec->nextcmdtime - jiffies);
	if (diff > 0) {
		/* We cant. Delay command execution until we can */
		msleep( jiffies_to_msecs( diff ) );
	}
		
	/* Now, signal NVEC to process it */
	gpio_set_value(nvec->gpio, 0);
	dev_dbg(nvec->dev, "gpio -> low\n");

	/* Estimate the timeout for the current command assuming each command is composed
	   by 32 bytes TX + 32 bytes RX. Usually it will be much less than this, but we
	   prefer to play safe than to timeout prematurely. The speed is 10Kbytes per byte,
	   meaning 6.4ms per queued command. Add to that the time NVEC takes to answer (about
	   60ms. We round it to 70 ms */
	
	/* Wait for command execution */
	tries = 3;
	do {
	
		/* Now wait until the answer was received with timeout */
		res = wait_for_completion_timeout(&nvec->rx_done, msecs_to_jiffies(NVEC_CMD_TIMEOUT));
			
		/* If succeeded, just break loop */
		if (res)
			break;
		
		/* If the slight call of attention to NVEC didn't work, proceed more
		   aggresively, resetting the i2c slave and kicking the process again */
		if (tries < 2) {
			/* Reset the i2c slave of tegra */
			dev_dbg(nvec->dev, "Resetting i2c slave\n");
			
			/* Take spinlock to protect against concurrent modifications to the lists */
			spin_lock_irqsave(&nvec->lock,flags);
			
			tegra_disable_i2c_slave(nvec);
			tegra_enable_i2c_slave(nvec);
		
			/* Restart finite machine status */
			nvec->smbus_state = 0;		
			nvec->msg = NULL;
		
			/* Prepare the header to queue the request and wait for the response */
			INIT_LIST_HEAD(&msg->node);
			init_completion(&nvec->rx_done);
	
			/* Set the packet to TX and to RX */
			nvec->tx = msg;
			nvec->rx = msg;

			/* Release spinlock */
			spin_unlock_irqrestore(&nvec->lock,flags);	
		}
		
		/* Try to wake the NvEC by toggling the GPIO line */
		gpio_set_value(nvec->gpio, 1);
		dev_dbg(nvec->dev, "gpio -> high\n");
		msleep(10);
		gpio_set_value(nvec->gpio, 0);
		dev_dbg(nvec->dev, "gpio -> low\n");

	} while (--tries);
	
	/* Check results ... */
	if (!res) {

		/* Assume a Timed out error */
		res = -ETIMEDOUT;
		
		/* Take spinlock to protect against concurrent modifications to the lists */
		spin_lock_irqsave(&nvec->lock,flags);

		/* Message was not sent? */
		if (!nvec->tx && !nvec->rx) {

			/* Everything was ok - Command completed successfully */
			res = 0;
			
		} else {
		
			dev_err(nvec->dev,"Command timed out\n");
		}
		
		/* Abort command */
		nvec->tx = NULL;
		nvec->rx = NULL;
		if (nvec->msg == msg) {
			nvec->msg = NULL;
		}
		
		/* Release spinlock */
		spin_unlock_irqrestore(&nvec->lock,flags);	

	} else {
	
		/* Everything is ok */
		res = 0;
	}
	
	/* Release the command mutex */
	mutex_unlock(&nvec->cmdmtx);
	
	return res;
}


/* Enable event reporting */
static int nvec_enable_eventreporting(struct nvec_chip *nvec)
{
	int ret;
	static const struct NVEC_REQ_SLEEP_GLOBALCONFIGEVENTREPORT_PAYLOAD cfg = {
		.GlobalReportEnable = NVEC_REQ_SLEEP_GLOBAL_REPORT_ENABLE_0_ACTION_ENABLE,
	};
	ret = nvec_cmd_xfer(nvec->dev,
			NVEC_CMD_SLEEP,NVEC_CMD_SLEEP_GLOBALCONFIGEVENTREPORT,
			&cfg,sizeof(cfg),NULL,0);
	if (ret < 0) {
		dev_err(nvec->dev, "Unable to enable event reporting\n");
	}
	return ret;
}

/* Disable event reporting */
static int nvec_disable_eventreporting(struct nvec_chip *nvec)
{
	int ret;
	static const struct NVEC_REQ_SLEEP_GLOBALCONFIGEVENTREPORT_PAYLOAD cfg = {
		.GlobalReportEnable = NVEC_REQ_SLEEP_GLOBAL_REPORT_ENABLE_0_ACTION_DISABLE,
	};
	ret = nvec_cmd_xfer(nvec->dev,
			NVEC_CMD_SLEEP,NVEC_CMD_SLEEP_GLOBALCONFIGEVENTREPORT,
			&cfg,sizeof(cfg),NULL,0);
	if (ret < 0) {
		dev_err(nvec->dev, "Unable to disable event reporting\n");
	}
	return ret;
}


/* Register an event handler */
int nvec_add_eventhandler(struct device* dev, struct notifier_block *nb)
{
	struct nvec_chip* nvec = dev_get_drvdata(dev);
	return blocking_notifier_chain_register(&nvec->ev_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(nvec_add_eventhandler);

/* Unregister an event handler */
int nvec_remove_eventhandler(struct device* dev, struct notifier_block *nb)
{
	struct nvec_chip* nvec = dev_get_drvdata(dev);
	return blocking_notifier_chain_unregister(&nvec->ev_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(nvec_remove_eventhandler);


/* write a command, and wait for the answer - The function may sleep, so
   it must not be called from a ISR context. Returns the size of the rx
   payload, or negative numbers if errors detected */
int nvec_cmd_xfer(struct device* dev, int cmd,int subcmd,
				 const void* txpayld,int txpayldsize,
				 void* rxpayld,int rxpayloadmaxsize)
{
	int ret;
	struct nvec_chip* nvec = dev_get_drvdata(dev);
	
	/* Fill in the message */
	struct nvec_cmd msg = {
		.tx = {
			.raw = { 0 },
		}
	};

	/* If the device is suspended, do not try this */
	if (nvec->suspended) {
		dev_err(nvec->dev,"tried to send a command while NVEC is suspended!\n");
		return -EIO;
	}
	
	msg.tx.size   = txpayldsize + 2;/* Size is computed as payload size + 2 bytes */	
	msg.tx.cmd 	  = cmd;			/* Command */
	msg.tx.subcmd = subcmd;			/* Subcommand */
	
	if (txpayldsize > 0 && txpayld != NULL) {
		memcpy(&msg.tx.data[0],txpayld,txpayldsize);
	}
	
	dev_dbg(nvec->dev, "Sendind cmd:0x%02x, subcmd:0x%02x, size:0x%02x\n",msg.tx.cmd,msg.tx.subcmd,msg.tx.size);
#ifdef DEBUG
	for (ret = 0; ret < txpayldsize; ret++) {
		dev_dbg(nvec->dev, "payload #[%d]: 0x%02x\n",ret,msg.tx.data[ret]);
	}
#endif
	
	/* Transfer it */
	ret = nvec_msg_xfer(nvec,&msg);
	
	/* If failed to transfer, say so */
	if (ret < 0)
		return ret;
		
	/* If an error was returned, say so */
	if (msg.rx.status != NVEC_STATUS_SUCCESS) {
		dev_err(nvec->dev, "NvEC returned an error: 0x%02x\n",msg.rx.status);
		return -EIO;
	}

	/* Everything went fine. Copy answer back if possible 
	   and user wants it and return the count of bytes copied */
	dev_dbg(nvec->dev, "Received cmd:0x%02x, subcmd:0x%02x, size:0x%02x, status:0x%02x\n",msg.rx.cmd,msg.rx.subcmd,msg.rx.size,msg.rx.status);	   
	
	ret = msg.rx.size - 2;
	if (rxpayld != NULL) {
		if (ret > rxpayloadmaxsize) {
			dev_err(nvec->dev, "Buffer too small to copy answer: Required %d, Supplied %d bytes\n",ret,rxpayloadmaxsize);
			ret = rxpayloadmaxsize;
		}
		if (ret) {
#ifdef DEBUG		
			int i;
#endif
			memcpy(rxpayld,&msg.rx.data[0],ret);
#ifdef DEBUG
			for (i = 0; i < ret; i++) {
				dev_dbg(nvec->dev, "payload #[%d]: 0x%02x\n",i,((u8*)rxpayld)[i]);
			}
#endif
		} else {
			dev_dbg(nvec->dev, "No payload\n");
		}
	} else {
		/* User was not interested in the count of bytes */
		dev_dbg(nvec->dev, "User has no interest in payload\n");		
		ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL(nvec_cmd_xfer);

struct nvec_chip* g_nvec = NULL;

/* Power down by using NvEC */
void nvec_poweroff(void)
{
	if (g_nvec != NULL) {
	
		/* Disable event reporting */
		nvec_disable_eventreporting(g_nvec);
		
		/* Send the command to power down AP */
		nvec_cmd_xfer(g_nvec->dev,
			NVEC_CMD_SLEEP, NVEC_CMD_SLEEP_APPOWERDOWN,
			NULL,0,NULL,0);
	} else {
		pr_emerg("NvEC not initialized. Unable to shutdown\n");
	}
}
EXPORT_SYMBOL(nvec_poweroff);

/* Restart by using NvEC */
void nvec_restart(void)
{
	if (g_nvec != NULL) {
	
		/* Disable event reporting */
		nvec_disable_eventreporting(g_nvec);
		
		/* Send the command to restart AP */
		nvec_cmd_xfer(g_nvec->dev,
			NVEC_CMD_SLEEP, NVEC_CMD_SLEEP_APRESTART,
			NULL,0,NULL,0);
	} else {
		pr_emerg("NvEC not initialized. Unable to restart\n");
	}
}
EXPORT_SYMBOL(nvec_restart);

static int __remove_subdev(struct device *dev, void *unused)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int nvec_remove_subdevs(struct nvec_chip *nvec)
{
	return device_for_each_child(nvec->dev, NULL, __remove_subdev);
}
 
static int __devinit nvec_add_subdevs(struct nvec_chip *nvec,
					  struct nvec_platform_data* pdata)
{
	struct nvec_subdev_info *subdev;
	struct platform_device *pdev;
	int i, ret = 0;

	for (i = 0; i < pdata->num_subdevs; i++) {
		subdev = &pdata->subdevs[i];

		pdev = platform_device_alloc(subdev->name, subdev->id);

		pdev->dev.parent = nvec->dev;
		pdev->dev.platform_data = subdev->platform_data;

		ret = platform_device_add(pdev);
		if (ret)
			goto failed;
	}
	return 0;

failed:
	nvec_remove_subdevs(nvec);
	return ret;
} 

static int __devinit tegra_nvec_probe(struct platform_device *pdev)
{
	int err, ret;
	struct clk *i2c_clk;
	struct nvec_platform_data *pdata = pdev->dev.platform_data;
	struct nvec_chip *nvec;
	void __iomem *i2c_regs;

	/* Check that platform data is present */
	if (pdata == NULL) {
		dev_err(&pdev->dev, "no platform data\n");
		return -ENODEV;
	}
	
	/* Allocate platform data */
	nvec = kzalloc(sizeof(struct nvec_chip), GFP_KERNEL);
	if (nvec == NULL) {
		dev_err(&pdev->dev, "failed to reserve memory\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, nvec);
	dev_set_drvdata(&pdev->dev, nvec);
		
	/* Copy the configuration info to it */
	nvec->dev = &pdev->dev;
	nvec->gpio = pdata->gpio;
	nvec->irq = pdata->irq;
	nvec->i2c_addr = pdata->i2c_addr;
	
	i2c_regs = ioremap(pdata->base, pdata->size);
	if (!i2c_regs) {
		dev_err(nvec->dev, "failed to ioremap registers\n");
		goto failed;
	}

	nvec->i2c_regs = i2c_regs;

	i2c_clk = clk_get_sys(pdata->clock, NULL);
	if (IS_ERR_OR_NULL(i2c_clk)) {
		dev_err(nvec->dev, "failed to get clock tegra-i2c.2\n");
		goto failed;
	}
	
	nvec->i2c_clk = i2c_clk;

	/* Set the gpio to low when we've got something to say */
	err = gpio_request(nvec->gpio, "nvec gpio");
	if (err < 0) {
		dev_err(nvec->dev, "couldn't request gpio\n");
		goto failed;
	}

	/* Deassert the GPIO line */
	gpio_direction_output(nvec->gpio, 1);
	gpio_set_value(nvec->gpio, 1);

	/* Init the context */
	mutex_init(&nvec->cmdmtx);
	spin_lock_init(&nvec->lock);
	INIT_LIST_HEAD(&nvec->ev_toprocess);
	INIT_WORK(&nvec->ev_work, nvec_ev_dispatch);
	BLOCKING_INIT_NOTIFIER_HEAD(&nvec->ev_notifier_list);
	
	/* Set as the next available time current time */
	nvec->nextcmdtime = jiffies;
	
	/* Ask for an ISR handler with ints disabled while executing it */
	err = request_irq(nvec->irq, i2c_interrupt, IRQF_DISABLED, pdev->name, nvec);
	if (err) {
		dev_err(nvec->dev, "couldn't request irq");
		goto failed2;
	}
	/* Initially, disable the Irq */
	disable_irq(nvec->irq);

	/* Enable the i2c slave */
	tegra_enable_i2c_slave(nvec);	

	/* Try to query for fw version. Not all NVEC firmwares support it, so
	   do not use it as a proof of nvec existance */
	{
		struct NVEC_ANS_CONTROL_GETFIRMWAREVERSION_PAYLOAD fwVer = {
			.VersionMajor = {0,0},
			.VersionMinor = {0,0},
		};
		
		ret = nvec_cmd_xfer(nvec->dev,
				NVEC_CMD_CONTROL,NVEC_CMD_CONTROL_GETFIRMWAREVERSION,
				NULL,0,
				&fwVer,sizeof(fwVer));
				
		/* We verify success, not size, as there are firmwares out there that
		   respond with less bytes than expected */
		if (ret >= 0) {
			dev_info(nvec->dev, "Firmware version %02x.%02x.%02x / %02x\n",
					fwVer.VersionMajor[1],fwVer.VersionMajor[0],
					fwVer.VersionMinor[1],fwVer.VersionMinor[0]);
		}
	}

	/* Enable event reporting - If this is not supported, then NvEC is not there*/
	ret = nvec_enable_eventreporting(nvec);
	if (ret < 0) {
		dev_err(nvec->dev, "NvEC not found\n");
		goto failed3;
	}

	/* Call the oem initialization callback, if provided. For example, Folio100
       can use it to initialize Lid detection/Power button detection or enable
	   speakers ...*/
	if (pdata->oem_init) {
		ret = pdata->oem_init(nvec->dev);
		if (ret < 0) {
			dev_err(nvec->dev, "OEM initialization failed\n");
		}
	}
	
	/* Register subdevices */
	ret = nvec_add_subdevs(nvec, pdata);
	if(ret) {
		dev_err(nvec->dev, "error adding subdevices\n");
	}
	
	/* Keep a pointer to the nvec chip structure */
	g_nvec = nvec;

	dev_info(nvec->dev, "Nvidia Embedded controller driver loaded\n");	
	
	return 0;

failed3: 
	tegra_disable_i2c_slave(nvec);
	free_irq(nvec->irq,nvec);
	
failed2:
	gpio_free(nvec->gpio);
	
failed:
	kfree(nvec);
	return -ENOMEM;
}

static int __devexit tegra_nvec_remove(struct platform_device *pdev)
{
	struct nvec_chip *nvec = platform_get_drvdata(pdev);

	/* No more pointer to the nvec chip structure */
	g_nvec = NULL;

	/* Remove subdevices */
	nvec_remove_subdevs(nvec);

	/* Disable event reporting */
	nvec_disable_eventreporting(nvec);

	/* Disable I2C Slave */
	tegra_disable_i2c_slave(nvec);	

	/* Release interrupt */
	free_irq(nvec->irq,nvec);
	
	/* Release gpio */
	gpio_free(nvec->gpio);
	kfree(nvec);
	
	return 0;
}

#ifdef CONFIG_PM

static int tegra_nvec_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct nvec_chip *nvec = platform_get_drvdata(pdev);

	dev_dbg(nvec->dev, "suspending\n");
	
	/* Disable event reporting */
	nvec_disable_eventreporting(nvec);
	
	/* Suspend AP */
	if (nvec_cmd_xfer(nvec->dev,NVEC_CMD_SLEEP,NVEC_CMD_SLEEP_APSUSPEND,NULL,0,NULL,0) < 0) {
		dev_err(nvec->dev, "error suspending AP\n");
	}

	/* From now on, the device is suspended. All commands that any thread
	   tries to send, will fail */
	nvec->suspended = true;
	
	/* Wait until no command is pending */
	while ( nvec->rx != NULL || nvec->tx != NULL) {
		msleep(10);
	};
	
	/* Disable I2C Slave */
	tegra_disable_i2c_slave(nvec);	
	
	return 0;
}

static int tegra_nvec_resume(struct platform_device *pdev)
{
	struct nvec_chip *nvec = platform_get_drvdata(pdev);

	dev_dbg(nvec->dev, "resuming\n");
	
	/* Enable I2C Slave */
	tegra_enable_i2c_slave(nvec);	

	/* From now on, the device is not suspended anymore */
	nvec->suspended = false;

	/* Enable event reporting */
	nvec_enable_eventreporting(nvec);

	return 0;
}

#else
#define tegra_nvec_suspend NULL
#define tegra_nvec_resume NULL
#endif

static struct platform_driver nvec_device_driver = {
	.probe = tegra_nvec_probe,
	.remove = __devexit_p(tegra_nvec_remove),
	.suspend = tegra_nvec_suspend,
	.resume = tegra_nvec_resume,
	.driver = {
		.name = "nvec",
		.owner = THIS_MODULE,
	},
};

static int __init tegra_nvec_init(void)
{
	return platform_driver_register(&nvec_device_driver);
}

static void __exit tegra_nvec_exit(void)
{
	platform_driver_unregister(&nvec_device_driver);
}


module_init(tegra_nvec_init);
module_exit(tegra_nvec_exit);

MODULE_ALIAS("platform:nvec");
MODULE_DESCRIPTION("NVIDIA compliant embedded controller interface");
MODULE_AUTHOR("Marc Dietrich <marvin24@gmx.de>");
MODULE_LICENSE("GPL");
