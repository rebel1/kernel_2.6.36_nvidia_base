/*
 * keyboard driver for a NVIDIA compliant embedded controller
 *
 * Copyright (C) 2011 Marc Dietrich <marvin24@gmx.de>
 *
 * Authors:  Pierre-Hugues Husson <phhusson@free.fr>
 *           Marc Dietrich <marvin24@gmx.de>
 *           Eduardo José Tagle <ejtagle@tutopia.com>  
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/slab.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mfd/nvec.h>
#include "nvec_keytable.h"

/* Compute the range of scan codes covered by the tables */
#define EC_FIRST_CODE 0x00
#define EC_LAST_CODE (ARRAY_SIZE(code_tab_102us)-1)
#define EC_TOTAL_CODES (EC_LAST_CODE - EC_FIRST_CODE + 1)

#define EC_EXT_CODE_FIRST 0xE000
#define EC_EXT_CODE_LAST (0xE000+ARRAY_SIZE(extcode_tab_us102)-1)
#define EC_EXT_TOTAL_CODES (EC_EXT_CODE_LAST - EC_EXT_CODE_FIRST + 1)


/* Keyboard driver context */
struct nvec_keys {
	struct notifier_block notifier;
	
	struct device *			 master;
	struct device *			 dev;

	struct input_dev *input;	
	
	int leds;				/* LED state */
	
	/* All the keycodes supported */
	u8 keycodes[ARRAY_SIZE(code_tab_102us) + ARRAY_SIZE(extcode_tab_us102)];
};

/* Special Scan Code set 1 codes */
#define SC1_LSHIFT 		(0x2A)
#define SC1_RSHIFT 		(0x36)
#define SC1_SCROLL 		(0x46)
#define SC1_PREFIX_E0 	(0xE0)
#define SC1_PREFIX_E1 	(0xE1)

/* Scan Code Set 1 break mask */
#define SC1_BREAK_MASK 	(0x80) 

/* Break and Make key flags */ 
#define SCAN_CODE_FLAG_MAKE 	0x01
#define SCAN_CODE_FLAG_BREAK 	0x02

static int nvec_keys_notifier(struct notifier_block *nb,
				unsigned long event_type, void *data)
{
	int code, codebytes, scancodeflags, i, pressed;
	struct nvec_event *ev  = (struct nvec_event *)data;
	struct nvec_keys *keys = container_of(nb, struct nvec_keys, notifier);

	/* If not a keyboard event, do not process it */ 
	if (event_type != NVEC_EV_KEYBOARD) 
		return NOTIFY_DONE;
		
	/* Pack scan code bytes from payload buffer into 32-bit dword */
    code = ev->data[0];
	codebytes = 1;
	scancodeflags = 0;
	
	if (ev->size == 1) {
		dev_dbg(keys->dev,"EC Payload = 0x%02x\n", ev->data[0]);
	} else {
		for (i = 0; i < ev->size; i++) {
			dev_dbg(keys->dev,"EC Payload = 0x%02x\n", ev->data[i]);
        }

        for (i = 1; i < ev->size; i++)
        {
            if (ev->data[i-1] == SC1_PREFIX_E0)
            {
                /* Temporary clear break flag just to check for extended shifts.
                   If detected, remove the entire extended shift sequence, as
                   it has no effect on SC1-to-VK translation */
                u8 sc = ev->data[i] & (~SC1_BREAK_MASK);
                if ((sc == SC1_LSHIFT) || (sc == SC1_RSHIFT))
                {
                    code = code >> 8;
                    codebytes--;
                    continue;
                }
                else if (ev->data[i] == SC1_SCROLL)
                {
                    /* If extended ScrollLock = Ctrl+Break, detected store it,
                       set both make/break flags, and abort buffer packing, as
                       the following bytes are just the break part of sequence */
                    code = (code << 8) | ((u32)ev->data[i]);
                    codebytes++;
                    scancodeflags = SCAN_CODE_FLAG_MAKE |
                                    SCAN_CODE_FLAG_BREAK;
                    break;
                }
            }
            if (ev->data[i] == SC1_PREFIX_E1)
            {
                /* If 2nd half of Pause key is detected, set both make/break
                   flags, and abort buffer packing, as the following bytes
                   are just the break part of sequence */
                scancodeflags = SCAN_CODE_FLAG_MAKE |
                                SCAN_CODE_FLAG_BREAK;
                break;
            }
            /* If not intercepted by special cases, pack scan code byte into
               the output dword */
            code = (code << 8) | ((u32)ev->data[i]);
            codebytes++;
        }

        /* After above packing all SC1 sequences are shrinked to 1-3 byte
           scan codes; 3-byte scan code always has both make/break flags
           already set; 2- and 1- byte scan code have break flag in low byte
           of low word */
        if (!scancodeflags)
        {
            switch (codebytes)
            {
                case 2:
                case 1:
                    scancodeflags = (code & ((u32)SC1_BREAK_MASK)) ?
                                    SCAN_CODE_FLAG_BREAK :
                                    SCAN_CODE_FLAG_MAKE;
                    code &= ~((u32)SC1_BREAK_MASK);
                    break;

                case 0:
                    /* Dummy sequence, no actual keystrokes (FIXME - assert ?) */
					dev_err(keys->dev,"Dummy sequence - no actual keystrokes: payld = 0x%02x\n", ev->data[i]);
                    return NOTIFY_STOP;

                default:
                    /* Not an SC1 payload - failed to pack */
					dev_err(keys->dev,"Not an SC1 payload - Failed to pack\n");
                    return NOTIFY_STOP;
            }
        }
		
		/* Check if key was pressed */
		pressed = (scancodeflags & SCAN_CODE_FLAG_MAKE);

		if ((code >= EC_FIRST_CODE) && 
			(code <= EC_LAST_CODE)) {
			
			code -= EC_FIRST_CODE;
			code = code_tab_102us[code];
			dev_dbg(keys->dev,"reporting key %s for key %d\n", pressed ? "press" : "release", code);
			input_report_key(keys->input, code, pressed);
			input_sync(keys->input);
		}
		else 
		if ((code >= EC_EXT_CODE_FIRST) &&
			(code <= EC_EXT_CODE_LAST)) {

			code -= EC_EXT_CODE_FIRST;
			code = extcode_tab_us102[code];
			dev_dbg(keys->dev,"reporting key %s for key %d\n", pressed ? "press" : "release", code);
			input_report_key(keys->input, code, pressed);
			input_sync(keys->input);
		}		
	}

	return NOTIFY_STOP;
}

static int nvec_kbd_event(struct input_dev *input, unsigned int type,
				unsigned int code, int value)
{
	struct nvec_keys *keys = input_get_drvdata(input);
	struct NVEC_REQ_KEYBOARD_SETLEDS_PAYLOAD setLeds;

	if (type == EV_REP)
		return 0;

	if (type != EV_LED)
		return -1;

	/* Based on the led, set the requested state */
	switch (code) {
	case LED_CAPSL:
		if (value) {
			keys->leds |=  NVEC_REQ_KEYBOARD_SET_LEDS_0_CAPS_LOCK_LED_ON;
		} else {
			keys->leds &= ~NVEC_REQ_KEYBOARD_SET_LEDS_0_CAPS_LOCK_LED_ON;
		}
		break;
	case LED_NUML:
		if (value) {
			keys->leds |=  NVEC_REQ_KEYBOARD_SET_LEDS_0_NUM_LOCK_LED_ON;
		} else {
			keys->leds &= ~NVEC_REQ_KEYBOARD_SET_LEDS_0_NUM_LOCK_LED_ON;
		}
		break;
	case LED_SCROLLL:
		if (value) {
			keys->leds |=  NVEC_REQ_KEYBOARD_SET_LEDS_0_SCROLL_LOCK_LED_ON;
		} else {
			keys->leds &= ~NVEC_REQ_KEYBOARD_SET_LEDS_0_SCROLL_LOCK_LED_ON;
		}
		break;
	default:
		return -1;
	}
	
	/* Set the led state */
	setLeds.LedFlag = keys->leds;
	if (nvec_cmd_xfer(keys->master,	NVEC_CMD_KEYBOARD,NVEC_CMD_KEYBOARD_SETLEDS,
						&setLeds,sizeof(setLeds),NULL,0) < 0) {
		dev_err(keys->dev,"failed to set leds: 0x%02x\n", keys->leds);						
	}
	
	return 0;
}

static int __devinit nvec_kbd_probe(struct platform_device *pdev)
{
	int i, j, err;
	struct input_dev *input;
	
	struct nvec_keys *keys = kzalloc(sizeof(*keys), GFP_KERNEL);
	if (keys == NULL) {
		dev_err(&pdev->dev, "can't allocate context\n");
		return -ENOMEM;
	}
	dev_set_drvdata(&pdev->dev, keys);
	platform_set_drvdata(pdev, keys);

	keys->master = pdev->dev.parent; 		
	keys->dev = &pdev->dev;
	
	/* Allocate a new input device */
	input = input_allocate_device();	
	keys->input = input;
	input_set_drvdata(input, keys);
	
	input->name = "NVEC keyboard";
	input->phys = "NVEC";
	input->dev.parent = &pdev->dev;
	input->id.bustype = BUS_HOST;
	input->id.vendor  = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100; 

	/* Set capabilities */
	set_bit(EV_KEY,input->evbit);
	set_bit(EV_REP,input->evbit);
	set_bit(EV_LED,input->evbit);
		
	set_bit(LED_CAPSL,input->ledbit);
	set_bit(LED_NUML,input->ledbit);
	set_bit(LED_SCROLLL,input->ledbit);
	
	input->event = nvec_kbd_event;
	
	j = 0;
	for (i = 0; i < ARRAY_SIZE(code_tab_102us); ++i)
		keys->keycodes[j++] = code_tab_102us[i];
	for (i = 0; i < ARRAY_SIZE(extcode_tab_us102); ++i)
		keys->keycodes[j++] = extcode_tab_us102[i];
	for (i = 0; i < ARRAY_SIZE(keys->keycodes); ++i)
		set_bit(keys->keycodes[i], input->keybit);

	clear_bit(0, input->keybit);
	
	input->keycode = keys->keycodes;
	input->keycodemax = ARRAY_SIZE(keys->keycodes);
	input->keycodesize = sizeof(u8);

	/* Register input device */
	err = input_register_device(input);
	if (err) {
		dev_err(&pdev->dev, "can't register input device\n");
		goto fail;
	}

	keys->notifier.notifier_call = nvec_keys_notifier;
	nvec_add_eventhandler(keys->master, &keys->notifier);

	/* reset the EC to start the keyboard scanning */
	if (nvec_cmd_xfer(keys->master,NVEC_CMD_KEYBOARD,NVEC_CMD_KEYBOARD_ENABLE,
						NULL,0,NULL,0) < 0) {
		dev_err(&pdev->dev, "unable to enable keyboard\n");
	}

	/* enable keyboard as wake up source */
	{
		struct NVEC_REQ_KEYBOARD_CONFIGUREWAKE_PAYLOAD cfgWake = {
			.WakeEnable = NVEC_REQ_KEYBOARD_WAKE_ENABLE_0_ACTION_ENABLE,
			.EventTypes = NVEC_REQ_KEYBOARD_EVENT_TYPE_0_ANY_KEY_PRESS_ENABLE,
		};
		if (nvec_cmd_xfer(keys->master,NVEC_CMD_KEYBOARD,NVEC_CMD_KEYBOARD_CONFIGUREWAKE,
							&cfgWake,sizeof(cfgWake),NULL,0) < 0) {
			dev_err(&pdev->dev, "unable to enable keyboard as wakeup source\n");
		}
	}
	
	/* Enable key reporting on wake up (so we know the pressed key that woke us up!) */
	{
		struct NVEC_REQ_KEYBOARD_CONFIGUREWAKEKEYREPORT_PAYLOAD cfgWakeReport = {
			.ReportWakeKey = NVEC_REQ_KEYBOARD_REPORT_WAKE_KEY_0_ACTION_ENABLE,
		};
		if (nvec_cmd_xfer(keys->master,NVEC_CMD_KEYBOARD,NVEC_CMD_KEYBOARD_CONFIGUREWAKEKEYREPORT,
							&cfgWakeReport,sizeof(cfgWakeReport),NULL,0) < 0) {
			dev_err(&pdev->dev, "unable to enable keyboard reporting on wakeup\n");							
		}
	}
		
	/* Reset keyboard/mouse */
	{
		struct NVEC_REQ_AUXDEVICE_SENDCOMMAND_PAYLOAD sendCmd = {
			.Operation = 0xFF,
			.NumBytesToReceive = 3,
		};
		if (nvec_cmd_xfer(keys->master,(NVEC_CMD_AUXDEVICE | NVEC_SUBTYPE_0_AUX_PORT_ID_0), NVEC_CMD_AUXDEVICE_SENDCOMMAND,
							&sendCmd,sizeof(sendCmd),NULL,0) < 0) {
			dev_err(&pdev->dev, "unable to reset keyboard\n");
		}
	}

	return 0;

fail:
	input_free_device(input);
	kfree(keys);
	return err;
}

static int __devexit nvec_kbd_remove(struct platform_device *pdev)
{
	struct nvec_keys *keys = platform_get_drvdata(pdev);

	nvec_remove_eventhandler(keys->master, &keys->notifier);

	/* Disable key reporting on wake up (so we know the pressed key that woke us up!) */
	{
		struct NVEC_REQ_KEYBOARD_CONFIGUREWAKEKEYREPORT_PAYLOAD cfgWakeReport = {
			.ReportWakeKey = NVEC_REQ_KEYBOARD_REPORT_WAKE_KEY_0_ACTION_DISABLE,
		};
		if (nvec_cmd_xfer(keys->master,NVEC_CMD_KEYBOARD,NVEC_CMD_KEYBOARD_CONFIGUREWAKEKEYREPORT,
							&cfgWakeReport,sizeof(cfgWakeReport),NULL,0) < 0) {
			dev_err(&pdev->dev, "error disabling key reporting on wake\n");
		}
	}

	/* Disable keyboard as wake up source */
	{
		struct NVEC_REQ_KEYBOARD_CONFIGUREWAKE_PAYLOAD cfgWake = {
			.WakeEnable = NVEC_REQ_KEYBOARD_WAKE_ENABLE_0_ACTION_DISABLE,
			.EventTypes = NVEC_REQ_KEYBOARD_EVENT_TYPE_0_ANY_KEY_PRESS_ENABLE,
		};
		if (nvec_cmd_xfer(keys->master,	NVEC_CMD_KEYBOARD,NVEC_CMD_KEYBOARD_CONFIGUREWAKE,
							&cfgWake,sizeof(cfgWake),NULL,0) < 0) {
			dev_err(&pdev->dev, "error disabling keyboard as wake source\n");
		}
	}
	

	/* reset the EC to stop the keyboard scanning */
	if (nvec_cmd_xfer(keys->master, NVEC_CMD_KEYBOARD,NVEC_CMD_KEYBOARD_DISABLE,
						NULL,0,NULL,0) < 0) {
		dev_err(&pdev->dev, "error disabling keyboard\n");
	}

	input_free_device(keys->input);
	kfree(keys);
	return 0;
}


static struct platform_driver nvec_kbd_driver = {
	.probe	= nvec_kbd_probe,
	.remove = __devexit_p(nvec_kbd_remove),
	.driver	= {
		.name	= "nvec-kbd",
		.owner	= THIS_MODULE,
	},
};

static int __init nvec_kbd_init(void)
{
	return platform_driver_register(&nvec_kbd_driver);
}

static void __exit nvec_kbd_exit(void)
{
	platform_driver_unregister(&nvec_kbd_driver);
}

module_init(nvec_kbd_init);
module_exit(nvec_kbd_exit);

MODULE_AUTHOR("Marc Dietrich <marvin24@gmx.de> / Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_DESCRIPTION("NVEC keyboard driver");
MODULE_LICENSE("GPL");
