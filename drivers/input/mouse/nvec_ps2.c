/*
 * mouse driver for a NVIDIA compliant embedded controller
 *
 * Copyright (C) 2011 Marc Dietrich <marvin24@gmx.de>
 *
 * Authors:  Pierre-Hugues Husson <phhusson@free.fr>
 *           Ilya Petrov <ilya.muromec@gmail.com>
 *           Marc Dietrich <marvin24@gmx.de>
 *           Eduardo José Tagle <ejtagle@tutopia.com> 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/slab.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mfd/nvec.h>

struct nvec_ps2 {
	struct notifier_block 	notifier;
	struct serio 			ser_dev;
	
	struct device *			master;
	struct device *			dev;
	
};

static int ps2_startstreaming(struct serio *ser_dev)
{
	struct nvec_ps2* ps2 = ser_dev->port_data;
	struct NVEC_REQ_AUXDEVICE_AUTORECEIVEBYTES_PAYLOAD autoRx = {
		.NumBytesToReceive = 1,
	};
	if (nvec_cmd_xfer(ps2->master,(NVEC_CMD_AUXDEVICE | NVEC_SUBTYPE_0_AUX_PORT_ID_0), NVEC_CMD_AUXDEVICE_AUTORECEIVEBYTES,
						&autoRx,sizeof(autoRx),NULL,0) < 0) {
		dev_err(ps2->dev,"error starting to stream\n");
	}
	return 0;
}

static void ps2_stopstreaming(struct serio *ser_dev)
{
	struct nvec_ps2* ps2 = ser_dev->port_data;
	if (nvec_cmd_xfer(ps2->master,(NVEC_CMD_AUXDEVICE | NVEC_SUBTYPE_0_AUX_PORT_ID_0), NVEC_CMD_AUXDEVICE_CANCELAUTORECEIVE,
						NULL,0,NULL,0) < 0) {
		dev_err(ps2->dev,"error stopping streaming\n");
	}
}

static int ps2_sendcommand(struct serio *ser_dev, unsigned char cmd)
{
	struct nvec_ps2* ps2 = ser_dev->port_data;
	struct NVEC_REQ_AUXDEVICE_SENDCOMMAND_PAYLOAD sendCmd = {
		.Operation = cmd,
		.NumBytesToReceive = 1,
	};
	dev_dbg(ps2->dev, "Sending ps2 cmd %02x\n", cmd);
	if (nvec_cmd_xfer(ps2->master,(NVEC_CMD_AUXDEVICE | NVEC_SUBTYPE_0_AUX_PORT_ID_0), NVEC_CMD_AUXDEVICE_SENDCOMMAND,
						&sendCmd,sizeof(sendCmd),NULL,0) < 0) {
		dev_err(ps2->dev,"error sending command 0x%02x\n",cmd);
	}
	return 0;
}


static int nvec_ps2_notifier(struct notifier_block *nb,
				unsigned long event_type, void *data)
{
	int i;
	struct nvec_ps2 *ps2 = container_of(nb, struct nvec_ps2, notifier);
	struct nvec_event *ev = (struct nvec_event *)data;
	
	/* If not targeting keyboard, do not process it */
	if (event_type != NVEC_EV_AUXDEVICE0) 
		return NOTIFY_DONE;

	for (i = 0; i < ev->size; i++) {
		dev_dbg(ps2->dev, "got byte %02x\n", ev->data[i]);
		serio_interrupt(&ps2->ser_dev, ev->data[i], 0);
	}
	return NOTIFY_STOP;
}


static int __devinit nvec_mouse_probe(struct platform_device *pdev)
{
	struct nvec_ps2* ps2;

	/* Allocate memory for context */
	ps2 = kzalloc(sizeof(*ps2), GFP_KERNEL);
	if (ps2 == NULL) {
		dev_err(&pdev->dev, "no memory for context\n");
		return -ENOMEM;
	}
	dev_set_drvdata(&pdev->dev, ps2);
	platform_set_drvdata(pdev, ps2);
	
	ps2->master = pdev->dev.parent;
	ps2->dev = &pdev->dev;
	
	ps2->ser_dev.id.type = SERIO_8042;
	ps2->ser_dev.write = ps2_sendcommand;
	ps2->ser_dev.open  = ps2_startstreaming;
	ps2->ser_dev.close = ps2_stopstreaming;
	ps2->ser_dev.port_data = ps2;
	ps2->ser_dev.dev.parent = &pdev->dev;

	strlcpy(ps2->ser_dev.name, "NVEC mouse", sizeof(ps2->ser_dev.name));
	strlcpy(ps2->ser_dev.phys, "NVEC", sizeof(ps2->ser_dev.phys));

	/* mouse reset */
	{
		struct NVEC_REQ_AUXDEVICE_SENDCOMMAND_PAYLOAD sendCmd = {
			.Operation = 0xFF,
			.NumBytesToReceive = 3,
		};
		if (nvec_cmd_xfer(ps2->master,(NVEC_CMD_AUXDEVICE | NVEC_SUBTYPE_0_AUX_PORT_ID_0), NVEC_CMD_AUXDEVICE_SENDCOMMAND,
							&sendCmd,sizeof(sendCmd),NULL,0) < 0) {
			dev_err(&pdev->dev,"unable to reset mouse\n");
		}
	}

	serio_register_port(&ps2->ser_dev);
	
	ps2->notifier.notifier_call = nvec_ps2_notifier;
	nvec_add_eventhandler(ps2->master, &ps2->notifier);

	return 0;
}

static int __devexit nvec_mouse_remove(struct platform_device *pdev)
{
	struct nvec_ps2* ps2 = platform_get_drvdata(pdev);
	ps2_stopstreaming(&ps2->ser_dev);
	nvec_remove_eventhandler(ps2->master, &ps2->notifier);
	serio_unregister_port(&ps2->ser_dev);
	kfree(ps2);
	return 0;
}

static struct platform_driver nvec_mouse_driver = {
	.probe	= nvec_mouse_probe,
	.remove	= __devexit_p(nvec_mouse_remove),
	.driver	= {
		.name	= "nvec-mouse",
		.owner	= THIS_MODULE,
	},
};

static int __init nvec_mouse_init(void)
{
	return platform_driver_register(&nvec_mouse_driver);
}

static void __exit nvec_mouse_exit(void)
{
	platform_driver_unregister(&nvec_mouse_driver);
}


module_init(nvec_mouse_init);
module_exit(nvec_mouse_exit);

MODULE_DESCRIPTION("NVEC mouse driver");
MODULE_AUTHOR("Marc Dietrich <marvin24@gmx.de> / Eduardo José Tagle <ejtagle@tutopia.com>");
MODULE_LICENSE("GPL");
