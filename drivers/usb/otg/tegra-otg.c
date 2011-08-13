/*
 * drivers/usb/otg/tegra-otg.c
 *
 * OTG transceiver driver for Tegra UTMI phy
 *
 * Copyright (C) 2010 NVIDIA Corp.
 * Copyright (C) 2010 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/platform_device.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>

#define USB_PHY_WAKEUP		0x408
#define  USB_ID_INT_EN		(1 << 0)
#define  USB_ID_INT_STATUS	(1 << 1)
#define  USB_ID_STATUS		(1 << 2)
#define  USB_ID_PIN_WAKEUP_EN	(1 << 6)
#define  USB_VBUS_WAKEUP_EN	(1 << 30)
#define  USB_VBUS_INT_EN	(1 << 8)
#define  USB_VBUS_INT_STATUS	(1 << 9)
#define  USB_VBUS_STATUS	(1 << 10)
#define  USB_INTS		(USB_VBUS_INT_STATUS | USB_ID_INT_STATUS)

#define  USB_VBUS_WAKEUP_SW_VALUE	(1 << 12)
#define  USB_VBUS_WAKEUP_SW_ENABLE	(1 << 11)
#define  USB_ID_SW_VALUE			(1 << 4)
#define  USB_ID_SW_ENABLE			(1 << 3)


struct tegra_otg_data {
	struct otg_transceiver otg;
	unsigned long int_status;
	spinlock_t lock;
	void __iomem *regs;
	struct clk *clk;
	int irq;
	struct platform_device *host;
	struct platform_device *pdev;
	struct work_struct work;
	unsigned int intr_reg_data;
	bool detect_vbus;
	bool clk_enabled;
};

static struct tegra_otg_data *tegra_clone = NULL;

static inline unsigned long otg_readl(struct tegra_otg_data *tegra,
				      unsigned int offset)
{
	return readl(tegra->regs + offset);
}

static inline void otg_writel(struct tegra_otg_data *tegra, unsigned long val,
			      unsigned int offset)
{
	writel(val, tegra->regs + offset);
}

static void tegra_otg_enable_clk(void)
{
	if (!tegra_clone->clk_enabled)
		clk_enable(tegra_clone->clk);
	tegra_clone->clk_enabled = true;
}

static void tegra_otg_disable_clk(void)
{
	if (tegra_clone->clk_enabled)
		clk_disable(tegra_clone->clk);
	tegra_clone->clk_enabled = false;
}

static const char *tegra_state_name(enum usb_otg_state state)
{
	if (state == OTG_STATE_A_HOST)
		return "HOST";
	if (state == OTG_STATE_B_PERIPHERAL)
		return "PERIPHERAL";
	if (state == OTG_STATE_A_SUSPEND)
		return "SUSPEND";
	return "INVALID";
}

void tegra_start_host(struct tegra_otg_data *tegra)
{
	struct tegra_otg_platform_data *pdata = tegra->otg.dev->platform_data;
	if (!tegra->pdev) {
		tegra->pdev = pdata->host_register();
	}
}

void tegra_stop_host(struct tegra_otg_data *tegra)
{
	struct tegra_otg_platform_data *pdata = tegra->otg.dev->platform_data;
	if (tegra->pdev) {
		pdata->host_unregister(tegra->pdev);
		tegra->pdev = NULL;
	}
}

static void irq_work(struct work_struct *work)
{
	struct tegra_otg_data *tegra =
		container_of(work, struct tegra_otg_data, work);
	struct tegra_otg_platform_data *pdata = tegra->otg.dev->platform_data;
	struct otg_transceiver *otg = &tegra->otg;
	enum usb_otg_state from = otg->state;
	enum usb_otg_state to = OTG_STATE_UNDEFINED;
	unsigned long flags;
	unsigned long status;
	unsigned long val;

	if (tegra->detect_vbus) {
		tegra->detect_vbus = false;
		tegra_otg_enable_clk();
		return;
	}

	clk_enable(tegra->clk);

	spin_lock_irqsave(&tegra->lock, flags);

	status = tegra->int_status;

	if (tegra->int_status & USB_ID_INT_STATUS) {
		if (status & USB_ID_STATUS) {
			if ((status & USB_VBUS_STATUS) && (from != OTG_STATE_A_HOST))
				to = OTG_STATE_B_PERIPHERAL;
			else
				to = OTG_STATE_A_SUSPEND;
		}
		else {
			if (from != OTG_STATE_B_PERIPHERAL)
				to = OTG_STATE_A_HOST;
			else
				to = OTG_STATE_A_SUSPEND;
		}
	}
	if (from != OTG_STATE_A_HOST) {
		if (tegra->int_status & USB_VBUS_INT_STATUS) {
			if (status & USB_VBUS_STATUS)
				to = OTG_STATE_B_PERIPHERAL;
			else
				to = OTG_STATE_A_SUSPEND;
		}
	}
	spin_unlock_irqrestore(&tegra->lock, flags);

	if (to != from &&
		to != OTG_STATE_UNDEFINED) {
		
		otg->state = to;

		dev_info(tegra->otg.dev, "%s --> %s\n", tegra_state_name(from),
					      tegra_state_name(to));

		if (to == OTG_STATE_A_SUSPEND) {
			if (from == OTG_STATE_A_HOST) {

				/* Apply the override if we are not using the real state */
				if (pdata->get_cableid_state) {
					val = otg_readl(tegra, USB_PHY_WAKEUP);
					val |= USB_ID_SW_VALUE;
					otg_writel(tegra, val, USB_PHY_WAKEUP);
				}
			
				tegra_stop_host(tegra);
			}
			else if (from == OTG_STATE_B_PERIPHERAL && otg->gadget) {

				/* Apply the override if we are not using the real state */
				if (pdata->get_vbus_state) {
					val = otg_readl(tegra, USB_PHY_WAKEUP);
					val &= ~USB_VBUS_WAKEUP_SW_VALUE;
					otg_writel(tegra, val, USB_PHY_WAKEUP);
				}
			
				usb_gadget_vbus_disconnect(otg->gadget);
			}
		} else if (to == OTG_STATE_B_PERIPHERAL && otg->gadget) {
			if (from == OTG_STATE_A_SUSPEND) {

				/* Apply the override if we are not using the real state */
				if (pdata->get_vbus_state) {
					val = otg_readl(tegra, USB_PHY_WAKEUP);
					val |= USB_VBUS_WAKEUP_SW_VALUE;
					otg_writel(tegra, val, USB_PHY_WAKEUP);
				}
			
				usb_gadget_vbus_connect(otg->gadget);
			}
		} else if (to == OTG_STATE_A_HOST) {
			if (from == OTG_STATE_A_SUSPEND) {

				/* Apply the override if we are not using the real state */
				if (pdata->get_cableid_state) {
					val = otg_readl(tegra, USB_PHY_WAKEUP);
					val &= ~USB_ID_SW_VALUE;
					otg_writel(tegra, val, USB_PHY_WAKEUP);
				}
			
				tegra_start_host(tegra);
			}
		}
	}
	clk_disable(tegra->clk);
	tegra_otg_disable_clk();
}

/* Compute the interrupts we need to enable */
static unsigned long tegra_otg_get_status_overrides(struct tegra_otg_data *tegra,
						unsigned long orgstatus)
{
	struct tegra_otg_platform_data *pdata = tegra->otg.dev->platform_data;
	unsigned long val = orgstatus;
		
	/* Override the detected values using the supplied callbacks */
	if (pdata->get_vbus_state) {
		if (pdata->get_vbus_state()) {
			val |= USB_VBUS_STATUS;
		} else {
			val &= ~USB_VBUS_STATUS;
		}
	}
	
	if (pdata->get_cableid_state) {
		if (pdata->get_cableid_state()) {
			val |= USB_ID_STATUS;
		} else {
			val &= ~USB_ID_STATUS;
		}
	}
	
	return val;
}

/* Computes the int sources based on the status */
static unsigned long tegra_compute_int_sources(unsigned long val)
{
	if ((val & USB_ID_STATUS) && (val & USB_VBUS_STATUS)) {
		val |= USB_VBUS_INT_STATUS;
	} else if (!(val & USB_ID_STATUS)) {
		val |= USB_ID_INT_STATUS;
	} else {
		val &= ~(USB_ID_INT_STATUS | USB_VBUS_INT_STATUS);
	}
	return val;
}

static irqreturn_t tegra_otg_irq(int irq, void *data)
{
	struct tegra_otg_data *tegra = data;
	unsigned long flags;
	unsigned long val;

	spin_lock_irqsave(&tegra->lock, flags);

	/* Get the interrupt cause */
	val = otg_readl(tegra, USB_PHY_WAKEUP);
	
	/* And clean it */
	otg_writel(tegra, val, USB_PHY_WAKEUP);
	
	/* Override the read values if the user specified callbacks */
	val = tegra_otg_get_status_overrides(tegra, val);

	/* Compute the interrupt sources based on the values */
	val = tegra_compute_int_sources(val);

	/* If interrupts enabled */
	if (val & (USB_VBUS_INT_EN | USB_ID_INT_EN)) {

		/* If an interrupt has to be processed */
		if ((val & USB_ID_INT_STATUS) || (val & USB_VBUS_INT_STATUS)) {
			tegra->int_status = val;
			tegra->detect_vbus = false;
			schedule_work(&tegra->work);
		}
	}

	spin_unlock_irqrestore(&tegra->lock, flags);

	return IRQ_HANDLED;
}

void tegra_otg_check_vbus_detection(void)
{
	tegra_clone->detect_vbus = true;
	schedule_work(&tegra_clone->work);
}
EXPORT_SYMBOL(tegra_otg_check_vbus_detection);

void tegra_otg_check_status(void)
{
	unsigned long val;
	
	/* Avoid crashes if not initialized yet */
	if (!tegra_clone) 
		return;
		
	val = tegra_clone->int_status;
	
	/* Get the status as specified by the user */
	val = tegra_otg_get_status_overrides(tegra_clone, val);

	/* Compute the interrupt sources based on the values */
	val = tegra_compute_int_sources(val);

	/* If interrupts enabled */
	if (val & (USB_VBUS_INT_EN | USB_ID_INT_EN)) {

		/* If an interrupt has to be processed */
		if ((val & USB_ID_INT_STATUS) || (val & USB_VBUS_INT_STATUS)) {
			tegra_clone->int_status = val;
			tegra_clone->detect_vbus = false;
			schedule_work(&tegra_clone->work);
		}
	}
}
EXPORT_SYMBOL(tegra_otg_check_status);

static int tegra_otg_set_peripheral(struct otg_transceiver *otg,
				struct usb_gadget *gadget)
{
	struct tegra_otg_data *tegra;
	unsigned long val;

	tegra = container_of(otg, struct tegra_otg_data, otg);
	otg->gadget = gadget;

	clk_enable(tegra->clk);
	val = otg_readl(tegra, USB_PHY_WAKEUP);
	val |= (USB_VBUS_INT_EN | USB_VBUS_WAKEUP_EN);
	val |= (USB_ID_INT_EN | USB_ID_PIN_WAKEUP_EN);
	otg_writel(tegra, val, USB_PHY_WAKEUP);
	clk_disable(tegra->clk);
	
	/* Override the read values if the user specified callbacks */
	val = tegra_otg_get_status_overrides(tegra, val);

	/* Compute the interrupt sources based on the values */
	val = tegra_compute_int_sources(val);

	/* Execute the ISR based on the status */
	if ((val & USB_ID_INT_STATUS) || (val & USB_VBUS_INT_STATUS)) {
		tegra->int_status = val;
		tegra->detect_vbus = false;
		schedule_work (&tegra->work);
	}

	return 0;
}

static int tegra_otg_set_host(struct otg_transceiver *otg,
				struct usb_bus *host)
{
	struct tegra_otg_data *tegra;
	unsigned long val;
	tegra = container_of(otg, struct tegra_otg_data, otg);

	otg->host = host;

	clk_enable(tegra->clk);
	val = otg_readl(tegra, USB_PHY_WAKEUP);
	val &= ~(USB_VBUS_INT_STATUS | USB_ID_INT_STATUS);
	val |= USB_ID_PIN_WAKEUP_EN | USB_ID_INT_EN;
	otg_writel(tegra, val, USB_PHY_WAKEUP);
	clk_disable(tegra->clk);

	return 0;
}

static int tegra_otg_set_power(struct otg_transceiver *otg, unsigned mA)
{
	return 0;
}

static int tegra_otg_set_suspend(struct otg_transceiver *otg, int suspend)
{
	return 0;
}

static int tegra_otg_probe(struct platform_device *pdev)
{
	struct tegra_otg_data *tegra;
	struct resource *res;
	int err;
	unsigned long val;
	struct tegra_otg_platform_data *pdata = pdev->dev.platform_data;
	
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data\n");
		return -ENODEV;
	}

	tegra = kzalloc(sizeof(struct tegra_otg_data), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	tegra->otg.dev = &pdev->dev;
	tegra->otg.label = "tegra-otg";
	tegra->otg.state = OTG_STATE_UNDEFINED;
	tegra->otg.set_host = tegra_otg_set_host;
	tegra->otg.set_peripheral = tegra_otg_set_peripheral;
	tegra->otg.set_suspend = tegra_otg_set_suspend;
	tegra->otg.set_power = tegra_otg_set_power;
	spin_lock_init(&tegra->lock);

	platform_set_drvdata(pdev, tegra);
	tegra_clone = tegra;
	tegra->clk_enabled = false;

	tegra->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(tegra->clk)) {
		dev_err(&pdev->dev, "Can't get otg clock\n");
		err = PTR_ERR(tegra->clk);
		goto err_clk;
	}

	err = clk_enable(tegra->clk);
	if (err)
		goto err_clken;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto err_io;
	}
	tegra->regs = ioremap(res->start, resource_size(res));
	if (!tegra->regs) {
		err = -ENOMEM;
		goto err_io;
	}

	tegra->otg.state = OTG_STATE_A_SUSPEND;

	/* Read the configuration */
	val = otg_readl(tegra, USB_PHY_WAKEUP);
	
	/* Set overrides for all the detections to override with custom functions */
	if (pdata->get_vbus_state) {
		val |= USB_VBUS_WAKEUP_SW_ENABLE;
		if (pdata->get_vbus_state()) {
			val |= USB_VBUS_WAKEUP_SW_VALUE;
		} else {
			val &= ~USB_VBUS_WAKEUP_SW_VALUE;
		}
	} else {
		val &= ~USB_VBUS_WAKEUP_SW_ENABLE;
	}
	
	if (pdata->get_cableid_state) {
		val |= USB_ID_SW_ENABLE;
		if (pdata->get_cableid_state()) {
			val |= USB_ID_SW_VALUE;
		} else {
			val &= ~USB_ID_SW_VALUE;
		}
	} else {
		val &= ~USB_ID_SW_ENABLE;
	}
	
	/* And write the configuration */
	otg_writel(tegra, val, USB_PHY_WAKEUP);
	
	err = otg_set_transceiver(&tegra->otg);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver (%d)\n", err);
		goto err_otg;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENXIO;
		goto err_irq;
	}
	tegra->irq = res->start;
	err = request_threaded_irq(tegra->irq, tegra_otg_irq,
				NULL,
				IRQF_SHARED, "tegra-otg", tegra);
	if (err) {
		dev_err(&pdev->dev, "Failed to register IRQ\n");
		goto err_irq;
	}

	INIT_WORK (&tegra->work, irq_work);

	dev_info(&pdev->dev, "otg transceiver registered\n");
	return 0;

err_irq:
	otg_set_transceiver(NULL);
err_otg:
	iounmap(tegra->regs);
err_io:
	clk_disable(tegra->clk);
err_clken:
	clk_put(tegra->clk);
err_clk:
	platform_set_drvdata(pdev, NULL);
	kfree(tegra);
	return err;
}

static int __exit tegra_otg_remove(struct platform_device *pdev)
{
	struct tegra_otg_data *tegra = platform_get_drvdata(pdev);

	free_irq(tegra->irq, tegra);
	otg_set_transceiver(NULL);
	iounmap(tegra->regs);
	clk_disable(tegra->clk);
	clk_put(tegra->clk);
	platform_set_drvdata(pdev, NULL);
	kfree(tegra);

	return 0;
}

#ifdef CONFIG_PM
static int tegra_otg_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_otg_data *tegra_otg = platform_get_drvdata(pdev);
	struct otg_transceiver *otg = &tegra_otg->otg;
	enum usb_otg_state from = otg->state;
	/* store the interupt enable for cable ID and VBUS */
	clk_enable(tegra_otg->clk);
	tegra_otg->intr_reg_data = readl(tegra_otg->regs + USB_PHY_WAKEUP);
	clk_disable(tegra_otg->clk);

	if (from == OTG_STATE_B_PERIPHERAL && otg->gadget)
		usb_gadget_vbus_disconnect(otg->gadget);

	tegra_otg_disable_clk();
	return 0;
}

static void tegra_otg_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_otg_data *tegra_otg = platform_get_drvdata(pdev);

	tegra_otg_enable_clk();

	/* Following delay is intentional.
	 * It is placed here after observing system hang.
	 * Root cause is not confirmed.
	 */
	msleep(1);
	/* restore the interupt enable for cable ID and VBUS */
	clk_enable(tegra_otg->clk);
	writel(tegra_otg->intr_reg_data, (tegra_otg->regs + USB_PHY_WAKEUP));
	clk_disable(tegra_otg->clk);

	return;
}

static const struct dev_pm_ops tegra_otg_pm_ops = {
	.complete = tegra_otg_resume,
	.suspend = tegra_otg_suspend,
};
#endif

static struct platform_driver tegra_otg_driver = {
	.driver = {
		.name  = "tegra-otg",
#ifdef CONFIG_PM
		.pm    = &tegra_otg_pm_ops,
#endif
	},
	.remove  = __exit_p(tegra_otg_remove),
	.probe   = tegra_otg_probe,
};

static int __init tegra_otg_init(void)
{
	return platform_driver_register(&tegra_otg_driver);
}
subsys_initcall(tegra_otg_init);

static void __exit tegra_otg_exit(void)
{
	platform_driver_unregister(&tegra_otg_driver);
}
module_exit(tegra_otg_exit);
