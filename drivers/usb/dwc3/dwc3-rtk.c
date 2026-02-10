#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0
/**
 * dwc3-rtk.c - Realtek DWC3 Specific Glue layer
 *
 * Copyright (C) 2017 Realtek Semiconductor Corporation
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/usb/otg.h>
#include <linux/usb/of.h>
#include <linux/usb/role.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/suspend.h>
#include <linux/sys_soc.h>
#include <soc/realtek/rtk-usb-manager.h>

#include "dwc3-rtk.h"
#include "dwc3-rtk-drd.h"
#include "core.h"
#include "io.h"

static const struct soc_device_attribute rtk_soc_phoenix[] = {
	{
		.family = "Realtek Phoenix",
	},
	{
		/* empty */
	}
};

static const struct soc_device_attribute rtk_soc_kylin[] = {
	{
		.family = "Realtek Kylin",
	},
	{
		/* empty */
	}
};

static const struct soc_device_attribute rtk_soc_kylin_a00[] = {
	{
		.family = "Realtek Kylin",
		.revision = "A00",
	},
	{
		/* empty */
	}
};

static const struct soc_device_attribute rtk_soc_hercules[] = {
	{
		.family = "Realtek Hercules",
	},
	{
		/* empty */
	}
};

#ifdef CONFIG_USB_PATCH_ON_RTK
#ifdef CONFIG_RTK_USB3PHY
extern void rtk_usb3_phy_toggle(struct usb_phy *usb3_phy, bool isConnect,
	    int port);
#endif

void RTK_dwc3_usb3_phy_toggle(struct device *hcd_dev, bool isConnect, int port)
{
	struct device *dwc3_dev = NULL;
	struct dwc3 *dwc = NULL;

	if (hcd_dev == NULL)
		return;

	dwc3_dev = hcd_dev->parent;
	if (dwc3_dev == NULL)
		return;

	dwc = dev_get_drvdata(dwc3_dev);
#ifdef CONFIG_RTK_USB3PHY
	dev_dbg(dwc3_dev, "%s port=%d\n", __func__, port);
	if (dwc != NULL)
		rtk_usb3_phy_toggle(dwc->usb3_phy, isConnect, port);
#endif
}

#ifdef CONFIG_RTK_USB2PHY
extern void rtk_usb2_phy_toggle(struct usb_phy *usb3_phy, bool isConnect,
	    int port);
#endif

int RTK_dwc3_usb2_phy_toggle(struct device *hcd_dev, bool isConnect, int port)
{
	struct device *dwc3_dev = NULL;
	struct dwc3 *dwc = NULL;

	if (hcd_dev == NULL)
		return -1;

	dwc3_dev = hcd_dev->parent;
	if (dwc3_dev == NULL)
		return -1;

	dwc = dev_get_drvdata(dwc3_dev);
	if (dwc == NULL)
		return -1;

#ifdef CONFIG_RTK_USB2PHY
	dev_dbg(dwc3_dev, "%s port=%d\n", __func__, port);
	rtk_usb2_phy_toggle(dwc->usb2_phy, isConnect, port);
#endif
	return 0;
}
#endif // CONFIG_USB_PATCH_ON_RTK

bool dwc3_rtk_is_support_drd_mode(struct dwc3_rtk *dwc3_rtk)
{
	if (!dwc3_rtk) {
		pr_err("%s: ERROR: dwc3_rtk is NULL!", __func__);
		return false;
	}

	return dwc3_rtk->support_drd_mode;
}

bool dwc3_rtk_is_connected_on_device_mode(struct dwc3_rtk *dwc3_rtk)
{
	bool connected = true;
	int no_host_connect = 0;
	int no_run_gadget = 0;
	u32 dsts, dctl;

	if (!dwc3_rtk) {
		pr_err("%s: ERROR: dwc3_rtk is NULL!", __func__);
		return connected;
	}
	if (dwc3_rtk->cur_dr_mode != USB_DR_MODE_PERIPHERAL) {
		dev_info(dwc3_rtk->dev,
			    "%s: Error: not in device mode (cur_dr_mode=%x)\n",
			    __func__, dwc3_rtk->cur_dr_mode);
		return connected;
	}

	dsts = dwc3_readl(dwc3_rtk->dwc->regs, DWC3_DSTS);
	dctl = dwc3_readl(dwc3_rtk->dwc->regs, DWC3_DCTL);

	dev_info(dwc3_rtk->dev, "%s: Device mode check DSTS=%x DCTL=%x\n",
		    __func__,
		    dsts, dctl);
	no_host_connect = DWC3_DSTS_USBLNKST(dsts) >= DWC3_LINK_STATE_SS_DIS;
	no_host_connect = no_host_connect | ((dsts & 0x0003FFF8) == BIT(17));
	no_run_gadget = (dctl & BIT(31)) == 0x0;
	if (no_host_connect || no_run_gadget)
		connected = false;

	return connected;
}

static void switch_u2_dr_mode(struct dwc3_rtk *rtk, int dr_mode)
{
	switch (dr_mode) {
	case USB_DR_MODE_PERIPHERAL:
		writel(USB2_PHY_SWITCH_DEVICE |
			    (~USB2_PHY_SWITCH_MASK &
			      readl(rtk->regs + WRAP_USB2_PHY_reg)),
			    rtk->regs + WRAP_USB2_PHY_reg);
		break;
	case USB_DR_MODE_HOST:
		writel(USB2_PHY_SWITCH_HOST |
			    (~USB2_PHY_SWITCH_MASK &
			      readl(rtk->regs + WRAP_USB2_PHY_reg)),
			    rtk->regs + WRAP_USB2_PHY_reg);
		break;
	case USB_DR_MODE_OTG:
		//writel(BIT(11) , rtk->regs + WRAP_USB2_PHY_reg);
		dev_info(rtk->dev, "%s: USB_DR_MODE_OTG\n", __func__);
		break;
	}
}

static void switch_dwc3_dr_mode(struct dwc3_rtk *rtk, int dr_mode)
{
#ifdef CONFIG_USB_DWC3_RTK_DUAL_ROLE
	switch (dr_mode) {
	case USB_DR_MODE_PERIPHERAL:
		dev_info(rtk->dev, "%s dr_mode=USB_DR_MODE_PERIPHERAL\n",
			    __func__);
		dwc3_drd_to_device(rtk->dwc);
		break;
	case USB_DR_MODE_HOST:
		dev_info(rtk->dev, "%s dr_mode=USB_DR_MODE_HOST\n",
			    __func__);
		dwc3_drd_to_host(rtk->dwc);
		break;
	default:
		dev_info(rtk->dev, "%s dr_mode=%d\n", __func__, dr_mode);
		dwc3_drd_to_stop_all(rtk->dwc);
	}
#else
	dev_info(rtk->dev, "Not support CONFIG_USB_DWC3_RTK_DUAL_ROLE\n");
#endif /* CONFIG_USB_DWC3_RTK_DUAL_ROLE */
}

int dwc3_rtk_get_dr_mode(struct dwc3_rtk *rtk)
{
	return rtk->cur_dr_mode;
}

int dwc3_rtk_set_dr_mode(struct dwc3_rtk *rtk, int dr_mode)
{
	if (!rtk->support_drd_mode)
		return rtk->cur_dr_mode;

	if (!rtk->dwc) {
		dev_err(rtk->dev, "%s Error! dwc3 is NULL", __func__);
		return rtk->cur_dr_mode;
	}

	dev_dbg(rtk->dev, "%s START....", __func__);

	rtk->cur_dr_mode = dr_mode;
	rtk->dwc->dr_mode = dr_mode;

	if (dr_mode != USB_DR_MODE_HOST) {
		dev_info(rtk->dev, "%s: To disable power\n", __func__);
		rtk_usb_port_power_on_off(rtk->dev, false);
	}

	switch_dwc3_dr_mode(rtk, dr_mode);
	mdelay(10);
	switch_u2_dr_mode(rtk, dr_mode);

	if (dr_mode == USB_DR_MODE_HOST) {
		dev_info(rtk->dev, "%s: To enable power\n", __func__);
		rtk_usb_port_power_on_off(rtk->dev, true);
	}

	dev_dbg(rtk->dev, "%s END....", __func__);

	return rtk->cur_dr_mode;
}

static ssize_t set_dr_mode_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct dwc3_rtk *rtk = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "Now cur_dr_mode is %s (default dwc3 dr_mode is %s)\n",
		    ({ char *tmp;
		switch (rtk->cur_dr_mode) {
		case USB_DR_MODE_PERIPHERAL:
		    tmp = "USB_DR_MODE_PERIPHERAL"; break;
		case USB_DR_MODE_HOST:
		    tmp = "USB_DR_MODE_HOST"; break;
		default:
		    tmp = "USB_DR_MODE_UNKNOWN"; break;
		    } tmp; }),
		    ({ char *tmp;
		switch (rtk->default_dwc3_dr_mode) {
		case USB_DR_MODE_PERIPHERAL:
		    tmp = "USB_DR_MODE_PERIPHERAL"; break;
		case USB_DR_MODE_HOST:
		    tmp = "USB_DR_MODE_HOST"; break;
		default:
		    tmp = "USB_DR_MODE_UNKNOWN"; break;
		    } tmp; }));

	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "write host -> switch to Host mode\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "write device -> switch to Device mode\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t set_dr_mode_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct dwc3_rtk *rtk = dev_get_drvdata(dev);

	if (!strncmp(buf, "host", 4))
		dwc3_rtk_set_dr_mode(rtk, USB_DR_MODE_HOST);
	else if (!strncmp(buf, "device", 6))
		dwc3_rtk_set_dr_mode(rtk, USB_DR_MODE_PERIPHERAL);
	else
		dwc3_rtk_set_dr_mode(rtk, 0);

	return count;
}
static DEVICE_ATTR_RW(set_dr_mode);

#if IS_ENABLED(CONFIG_USB_ROLE_SWITCH)
static int dwc3_usb_role_switch_set(struct usb_role_switch *sw, enum usb_role role)
{
	struct dwc3_rtk *rtk =  usb_role_switch_get_drvdata(sw);

	if (!rtk) {
		pr_err("ERROR: %s dwc3_rtk is NULL!", __func__);
		return role;
	}

	switch (role) {
	case USB_ROLE_HOST:
		dwc3_rtk_set_dr_mode(rtk, USB_DR_MODE_HOST);
		break;
	case USB_ROLE_DEVICE:
		dwc3_rtk_set_dr_mode(rtk, USB_DR_MODE_PERIPHERAL);
		break;
	default:
		dwc3_rtk_set_dr_mode(rtk, 0);
	}

	return 0;
}

static enum usb_role dwc3_usb_role_switch_get(struct usb_role_switch *sw)
{
	struct dwc3_rtk *rtk = usb_role_switch_get_drvdata(sw);
	enum usb_role role = USB_ROLE_NONE;
	int dr_mode;

	if (!rtk) {
		pr_err("ERROR: %s dwc3_rtk is NULL!", __func__);
		return role;
	}

	dr_mode = dwc3_rtk_get_dr_mode(rtk);
	switch (dr_mode) {
	case USB_DR_MODE_HOST:
		role = USB_ROLE_HOST;
		break;
	case USB_DR_MODE_PERIPHERAL:
		role = USB_ROLE_DEVICE;
		break;
	default:
		dev_info(rtk->dev, "%s dr_mode=%d", __func__, dr_mode);
		break;
	}
	return role;
}

static int dwc3_rtk_setup_role_switch(struct dwc3_rtk *rtk)
{
	struct usb_role_switch_desc dwc3_role_switch = {NULL};

	dwc3_role_switch.driver_data = rtk;
	dwc3_role_switch.allow_userspace_control = true;
	dwc3_role_switch.fwnode = dev_fwnode(rtk->dev);
	dwc3_role_switch.set = dwc3_usb_role_switch_set;
	dwc3_role_switch.get = dwc3_usb_role_switch_get;
	rtk->role_switch = usb_role_switch_register(rtk->dev, &dwc3_role_switch);
	if (IS_ERR(rtk->role_switch))
		return PTR_ERR(rtk->role_switch);

	return 0;
}

static int dwc3_rtk_remove_role_switch(struct dwc3_rtk *rtk)
{
	if (rtk->role_switch)
		usb_role_switch_unregister(rtk->role_switch);

	rtk->role_switch = NULL;

	return 0;
}
#else
#if defined(MY_ABC_HERE)
#define dwc3_rtk_setup_role_switch(x)
#define dwc3_rtk_remove_role_switch(x)
#else /* MY_ABC_HERE */
#define dwc3_rtk_setup_role_switch(x) 0
#define dwc3_rtk_remove_role_switch(x) 0
#endif /* MY_ABC_HERE */
#endif

static int dwc3_rtk_init(struct dwc3_rtk *rtk)
{
	struct device		*dev = rtk->dev;
	void __iomem		*regs = rtk->regs;

	if (soc_device_match(rtk_soc_kylin_a00)) {
		writel(DISABLE_MULTI_REQ | readl(regs + WRAP_CTR_reg),
				regs + WRAP_CTR_reg);
		dev_info(dev, "[bug fixed] 1295/1296 A00: add workaround to disable multiple request for D-Bus");
	}

	if (soc_device_match(rtk_soc_hercules)) {
		writel(USB2_PHY_EN_PHY_PLL_PORT1 |
			    readl(regs + WRAP_USB2_PHY_reg),
			    regs + WRAP_USB2_PHY_reg);
		dev_info(dev, "[bug fixed] 1395 add workaround to disable usb2 port 2 suspend!");

	}

	if (rtk->dis_u3_port) {
		void __iomem *reg;
		int val;

		reg =  rtk->regs + WRAP_USB_HMAC_CTR0_reg;
		val = U3PORT_DIS | readl(reg);
		writel(val, reg);

		dev_info(rtk->dev, "%s: only set dis_u3_port\n",
			    __func__);
	}

	if (rtk->disable_usb3) {
		void __iomem *reg;
		int val;

		reg = rtk->regs + WRAP_CTR_reg;
		val = FORCE_PIPE3_PHY_STATUS_TO_0 | readl(reg);
		writel(val, reg);

		reg = rtk->regs + WRAP_PHY_PIPE_reg;
		val = ~CLOCk_ENABLE_FOR_PIPE3_PCLK & readl(reg);
		val |= RESET_DISABLE_PIPE3_P0;
		writel(val, reg);

		reg =  rtk->regs + WRAP_USB_HMAC_CTR0_reg;
		val = U3PORT_DIS | readl(reg);
		writel(val, reg);

		reg = rtk->regs + WRAP_APHY_reg;
		val = ~USB3_MBIAS_ENABLE & readl(reg);
		writel(val, reg);

		dev_info(rtk->dev, "%s: disable usb 3.0 phy\n",
			    __func__);
	}

	return 0;
}

static int dwc3_rtk_initiated(struct dwc3_rtk *rtk)
{
	struct device		*dev;
	struct dwc3		*dwc;

	if (!rtk) {
		pr_err("ERROR! rtk is NULL\n");
		return -1;
	}

	dev = rtk->dev;
	dwc = rtk->dwc;
	if (!dwc) {
		dev_err(dev, "ERROR! dwc3 is NULL\n");
		return -1;
	}

	dev_info(dev, "%s\n", __func__);

	/* workaround: to avoid transaction error and cause port reset
	 * we enable threshold control for TX/RX
	 * [Dev_Fix] Enable DWC3 threshold control for USB compatibility issue
	 * commit 77f116ba77cc089ee2a6ceca1d2aa496b39c98ba
	 * [Dev_Fix] change RX threshold packet count from 1 to 3,
	 * it will get better performance
	 * commit fe8905c2112f899f9ec3ddbfd83e0f183d3fbf7d
	 * [DEV_FIX] In case there may have transaction error once
	 * system bus busy
	 * commit b36294740c5cf66932c0fec429f4c5399e26f591
	 */
	if (soc_device_match(rtk_soc_phoenix) ||
		    soc_device_match(rtk_soc_kylin) ||
		    soc_device_match(rtk_soc_hercules)) {
		dwc3_writel(dwc->regs, DWC3_GTXTHRCFG,
			    DWC3_GTXTHRCFG_TXPKTCNT(1) |
			    DWC3_GTXTHRCFG_MAXTXBURSTSIZE(1));
		dwc3_writel(dwc->regs, DWC3_GRXTHRCFG,
			    DWC3_GRXTHRCFG_PKTCNTSEL |
			    DWC3_GRXTHRCFG_RXPKTCNT(3) |
			    DWC3_GRXTHRCFG_MAXRXBURSTSIZE(3));

		/* enable auto retry */
		dwc3_writel(dwc->regs, DWC3_GUCTL,
			    dwc3_readl(dwc->regs, DWC3_GUCTL) |
			    DWC3_GUCTL_HSTINAUTORETRY);
	}

	if (dwc->revision >= DWC3_REVISION_300A)
		dwc3_writel(dwc->regs, DWC3_DEV_IMOD(0),
			    dwc3_readl(dwc->regs, DWC3_DEV_IMOD(0)) |
			      DWC3_DEVICE_IMODI(0x1));

	return 0;
}

static int dwc3_rtk_probe_dwc3core(struct dwc3_rtk *rtk)
{
	struct device		*dev = rtk->dev;
	struct device_node	*node = dev->of_node;
	struct device_node	*dwc3_node;
	int    ret = 0;

	dwc3_rtk_init(rtk);

	if (node) {
		ret = of_platform_populate(node, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to add dwc3 core\n");
			return ret;
		}

		dwc3_node = of_get_compatible_child(node, "synopsys,dwc3");
		if (!dwc3_node) {
			dev_info(dev, "Get dwc3_node at first child node\n");
			dwc3_node = of_get_next_child(node, NULL);
		}
		if (dwc3_node != NULL) {
			struct device *dwc3_dev;
			struct platform_device *dwc3_pdev;
			int dr_mode;

			dwc3_pdev = of_find_device_by_node(dwc3_node);
			dwc3_dev = &dwc3_pdev->dev;
			rtk->dwc = platform_get_drvdata(dwc3_pdev);

			dr_mode = usb_get_dr_mode(dwc3_dev);
			if (dr_mode != rtk->dwc->dr_mode) {
				dev_info(dev, "dts set dr_mode=%d, but dwc3 set dr_mode=%d\n",
					    dr_mode, rtk->dwc->dr_mode);

				dr_mode = rtk->dwc->dr_mode;
			}

			rtk->default_dwc3_dr_mode = dr_mode;
			rtk->cur_dr_mode = dr_mode;

			switch_u2_dr_mode(rtk, dr_mode);

		} else {
			dev_err(dev, "dwc3 node is NULL\n");
		}
	} else {
		dev_err(dev, "dwc3_rtk node is NULL\n");
	}

	ret = dwc3_rtk_initiated(rtk);

	if (rtk->cur_dr_mode == USB_DR_MODE_HOST)
		rtk_usb_init_port_power_on_off(rtk->dev, true);
	else
		rtk_usb_init_port_power_on_off(rtk->dev, false);

	if (rtk->support_drd_mode) {
		device_create_file(dev, &dev_attr_set_dr_mode);

		dwc3_rtk_setup_role_switch(rtk);
	}

	return ret;
}

static void dwc3_rtk_probe_work(struct work_struct *work)
{
	struct dwc3_rtk *rtk = container_of(work, struct dwc3_rtk, work);
	struct device		*dev = rtk->dev;
	int    ret = 0;

	unsigned long probe_time = jiffies;

	dev_info(dev, "%s Start ...\n", __func__);

	ret = dwc3_rtk_probe_dwc3core(rtk);

	if (ret)
		dev_err(dev, "%s failed to add dwc3 core\n", __func__);

	dev_info(dev, "%s End ... ok! (take %d ms)\n", __func__,
		    jiffies_to_msecs(jiffies - probe_time));
}

static int dwc3_rtk_probe(struct platform_device *pdev)
{
	struct dwc3_rtk	*rtk;
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;

	struct resource         *res;
	void __iomem            *regs;

	int			ret = -ENOMEM;
	unsigned long probe_time = jiffies;

	dev_info(&pdev->dev, "Probe Realtek-SoC USB DWC3 Host Controller\n");

	rtk = devm_kzalloc(dev, sizeof(*rtk), GFP_KERNEL);
	if (!rtk)
		goto err1;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);

	platform_set_drvdata(pdev, rtk);

	rtk->dev	= dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "missing memory resource\n");
		ret = -ENODEV;
		goto err1;
	}

	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto err1;
	}

	rtk->regs = regs;
	rtk->regs_size = resource_size(res);

	dwc3_rtk_debugfs_init(rtk);

	if (node) {

		rtk->dis_u3_port = false;
		if (of_property_read_bool(node, "dis_u3_port"))
			rtk->dis_u3_port = true;

		rtk->disable_usb3 = false;
		if (of_property_read_bool(node, "disable_usb3"))
			rtk->disable_usb3 = true;

		rtk->support_drd_mode = false;
		if (of_property_read_bool(node, "drd_mode")) {
			dev_info(rtk->dev, "%s: support drd_mode\n", __func__);
			rtk->support_drd_mode = true;
		}
	}

	if (node) {
		if (of_property_read_bool(node, "delay_probe_work")) {
			INIT_WORK(&rtk->work, dwc3_rtk_probe_work);
			if (of_property_read_bool(node, "ordered_probe"))
				rtk_usb_manager_schedule_work(dev, &rtk->work);
			else
				schedule_work(&rtk->work);
		} else {
			ret = dwc3_rtk_probe_dwc3core(rtk);
			if (ret) {
				dev_err(dev, "%s failed to add dwc3 core\n",
					    __func__);
				goto err1;
			}
		}
	} else {
		dev_err(dev, "no device node, failed to add dwc3 core\n");
		ret = -ENODEV;
		goto err1;
	}

	dev_info(dev, "%s ok! (take %d ms)\n", __func__,
		    jiffies_to_msecs(jiffies - probe_time));

	return 0;

err1:
	return ret;
}

static int dwc3_rtk_remove(struct platform_device *pdev)
{
	struct dwc3_rtk	*rtk = platform_get_drvdata(pdev);

	rtk->dwc = NULL;

	if (rtk->support_drd_mode) {
		device_remove_file(rtk->dev, &dev_attr_set_dr_mode);

		dwc3_rtk_remove_role_switch(rtk);
	}

	dwc3_rtk_debugfs_exit(rtk);

	of_platform_depopulate(rtk->dev);

	rtk_usb_remove_port_power_on_off(rtk->dev, false);
	return 0;
}

static void dwc3_rtk_shutdown(struct platform_device *pdev)
{
#if defined(MY_ABC_HERE)
#else /* MY_ABC_HERE */
	struct dwc3_rtk	*rtk = platform_get_drvdata(pdev);
#endif /* MY_ABC_HERE */
	struct device		*dev = &pdev->dev;

	dev_info(dev, "%s start ...\n", __func__);

	dev_info(dev, "%s ok!\n", __func__);
}

#ifdef CONFIG_OF
static const struct of_device_id rtk_dwc3_match[] = {
	{ .compatible = "realtek,dwc3" },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_dwc3_match);
#endif

#ifdef CONFIG_PM_SLEEP
static int dwc3_rtk_suspend(struct device *dev)
{
	struct dwc3_rtk *rtk = dev_get_drvdata(dev);

	dev_info(dev, "[USB] Enter %s\n", __func__);

	if (!rtk) {
		dev_err(dev, "[USB] %s dwc3_rtk is NULL\n", __func__);
		goto out;
	}

#ifdef CONFIG_USB_PATCH_ON_RTK
#ifdef CONFIG_RTK_PLATFORM
	if (RTK_PM_STATE == PM_SUSPEND_STANDBY) {
		//For idle mode
		dev_info(dev, "[USB] %s Idle mode\n", __func__);
		goto out;
	}
#endif
#endif // CONFIG_USB_PATCH_ON_RTK

	//For suspend mode
	dev_info(dev,  "[USB] %s Suspend mode\n", __func__);

out:
	dev_info(dev, "[USB] Exit %s", __func__);
	return 0;
}

static int dwc3_rtk_resume(struct device *dev)
{
	struct dwc3_rtk *rtk = dev_get_drvdata(dev);
	struct dwc3 *dwc = rtk->dwc;

	dev_info(dev, "[USB] Enter %s", __func__);

	if (!rtk) {
		dev_err(dev, "[USB] %s dwc3_rtk is NULL\n", __func__);
		goto out;
	}

#ifdef CONFIG_USB_PATCH_ON_RTK
#ifdef CONFIG_RTK_PLATFORM
	if (RTK_PM_STATE == PM_SUSPEND_STANDBY) {
		//For idle mode
		dev_info(dev, "[USB] %s Idle mode\n", __func__);
		goto out;
	}
#endif
#endif // CONFIG_USB_PATCH_ON_RTK

	//For suspend mode
	dev_info(dev,  "[USB] %s Suspend mode\n", __func__);

	dwc3_rtk_init(rtk);

	switch_u2_dr_mode(rtk, dwc->dr_mode);

	dwc3_rtk_initiated(rtk);

	/* runtime set active to reflect active state. */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

out:
	dev_info(dev, "[USB] Exit %s\n", __func__);
	return 0;
}

static const struct dev_pm_ops dwc3_rtk_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_rtk_suspend, dwc3_rtk_resume)
};

#define DEV_PM_OPS	(&dwc3_rtk_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver dwc3_rtk_driver = {
	.probe		= dwc3_rtk_probe,
	.remove		= dwc3_rtk_remove,
	.driver		= {
		.name	= "rtk-dwc3",
		.of_match_table = of_match_ptr(rtk_dwc3_match),
		.pm	= DEV_PM_OPS,
	},
	.shutdown	= dwc3_rtk_shutdown,
};

module_platform_driver(dwc3_rtk_driver);

MODULE_ALIAS("platform:rtk-dwc3");
MODULE_LICENSE("GPL");
