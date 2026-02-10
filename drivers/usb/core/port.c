#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0
/*
 * usb port device code
 *
 * Copyright (C) 2012 Intel Corp
 *
 * Author: Lan Tianyu <tianyu.lan@intel.com>
 */

#include <linux/slab.h>
#include <linux/pm_qos.h>
#ifdef MY_ABC_HERE
#include <linux/syno_gpio.h>
#endif /* MY_ABC_HERE */

#include "hub.h"

static int usb_port_block_power_off;

static const struct attribute_group *port_dev_group[];

static ssize_t location_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct usb_port *port_dev = to_usb_port(dev);

	return sprintf(buf, "0x%08x\n", port_dev->location);
}
static DEVICE_ATTR_RO(location);

static ssize_t connect_type_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct usb_port *port_dev = to_usb_port(dev);
	char *result;

	switch (port_dev->connect_type) {
	case USB_PORT_CONNECT_TYPE_HOT_PLUG:
		result = "hotplug";
		break;
	case USB_PORT_CONNECT_TYPE_HARD_WIRED:
		result = "hardwired";
		break;
	case USB_PORT_NOT_USED:
		result = "not used";
		break;
	default:
		result = "unknown";
		break;
	}

	return sprintf(buf, "%s\n", result);
}
static DEVICE_ATTR_RO(connect_type);

static ssize_t over_current_count_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct usb_port *port_dev = to_usb_port(dev);

	return sprintf(buf, "%u\n", port_dev->over_current_count);
}
static DEVICE_ATTR_RO(over_current_count);

static ssize_t quirks_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct usb_port *port_dev = to_usb_port(dev);

	return sprintf(buf, "%08x\n", port_dev->quirks);
}

static ssize_t quirks_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct usb_port *port_dev = to_usb_port(dev);
	u32 value;

	if (kstrtou32(buf, 16, &value))
		return -EINVAL;

	port_dev->quirks = value;
	return count;
}
static DEVICE_ATTR_RW(quirks);

static ssize_t usb3_lpm_permit_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct usb_port *port_dev = to_usb_port(dev);
	const char *p;

	if (port_dev->usb3_lpm_u1_permit) {
		if (port_dev->usb3_lpm_u2_permit)
			p = "u1_u2";
		else
			p = "u1";
	} else {
		if (port_dev->usb3_lpm_u2_permit)
			p = "u2";
		else
			p = "0";
	}

	return sprintf(buf, "%s\n", p);
}

static ssize_t usb3_lpm_permit_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct usb_port *port_dev = to_usb_port(dev);
	struct usb_device *udev = port_dev->child;
	struct usb_hcd *hcd;

	if (!strncmp(buf, "u1_u2", 5)) {
		port_dev->usb3_lpm_u1_permit = 1;
		port_dev->usb3_lpm_u2_permit = 1;

	} else if (!strncmp(buf, "u1", 2)) {
		port_dev->usb3_lpm_u1_permit = 1;
		port_dev->usb3_lpm_u2_permit = 0;

	} else if (!strncmp(buf, "u2", 2)) {
		port_dev->usb3_lpm_u1_permit = 0;
		port_dev->usb3_lpm_u2_permit = 1;

	} else if (!strncmp(buf, "0", 1)) {
		port_dev->usb3_lpm_u1_permit = 0;
		port_dev->usb3_lpm_u2_permit = 0;
	} else
		return -EINVAL;

	/* If device is connected to the port, disable or enable lpm
	 * to make new u1 u2 setting take effect immediately.
	 */
	if (udev) {
		hcd = bus_to_hcd(udev->bus);
		if (!hcd)
			return -EINVAL;
		usb_lock_device(udev);
		mutex_lock(hcd->bandwidth_mutex);
		if (!usb_disable_lpm(udev))
			usb_enable_lpm(udev);
		mutex_unlock(hcd->bandwidth_mutex);
		usb_unlock_device(udev);
	}

	return count;
}
static DEVICE_ATTR_RW(usb3_lpm_permit);

static struct attribute *port_dev_attrs[] = {
	&dev_attr_connect_type.attr,
	&dev_attr_location.attr,
	&dev_attr_quirks.attr,
	&dev_attr_over_current_count.attr,
	NULL,
};

static struct attribute_group port_dev_attr_grp = {
	.attrs = port_dev_attrs,
};

static const struct attribute_group *port_dev_group[] = {
	&port_dev_attr_grp,
	NULL,
};

static struct attribute *port_dev_usb3_attrs[] = {
	&dev_attr_usb3_lpm_permit.attr,
	NULL,
};

static struct attribute_group port_dev_usb3_attr_grp = {
	.attrs = port_dev_usb3_attrs,
};

static const struct attribute_group *port_dev_usb3_group[] = {
	&port_dev_attr_grp,
	&port_dev_usb3_attr_grp,
	NULL,
};

static void usb_port_device_release(struct device *dev)
{
	struct usb_port *port_dev = to_usb_port(dev);

	kfree(port_dev->req);
	kfree(port_dev);
}

#ifdef CONFIG_PM
static int usb_port_runtime_resume(struct device *dev)
{
	struct usb_port *port_dev = to_usb_port(dev);
	struct usb_device *hdev = to_usb_device(dev->parent->parent);
	struct usb_interface *intf = to_usb_interface(dev->parent);
	struct usb_hub *hub = usb_hub_to_struct_hub(hdev);
	struct usb_device *udev = port_dev->child;
	struct usb_port *peer = port_dev->peer;
	int port1 = port_dev->portnum;
	int retval;

	if (!hub)
		return -EINVAL;
	if (hub->in_reset) {
		set_bit(port1, hub->power_bits);
		return 0;
	}

	/*
	 * Power on our usb3 peer before this usb2 port to prevent a usb3
	 * device from degrading to its usb2 connection
	 */
	if (!port_dev->is_superspeed && peer)
		pm_runtime_get_sync(&peer->dev);

	retval = usb_autopm_get_interface(intf);
	if (retval < 0)
		return retval;

	retval = usb_hub_set_port_power(hdev, hub, port1, true);
	msleep(hub_power_on_good_delay(hub));
	if (udev && !retval) {
		/*
		 * Our preference is to simply wait for the port to reconnect,
		 * as that is the lowest latency method to restart the port.
		 * However, there are cases where toggling port power results in
		 * the host port and the device port getting out of sync causing
		 * a link training live lock.  Upon timeout, flag the port as
		 * needing warm reset recovery (to be performed later by
		 * usb_port_resume() as requested via usb_wakeup_notification())
		 */
		if (hub_port_debounce_be_connected(hub, port1) < 0) {
			dev_dbg(&port_dev->dev, "reconnect timeout\n");
			if (hub_is_superspeed(hdev))
				set_bit(port1, hub->warm_reset_bits);
		}

		/* Force the child awake to revalidate after the power loss. */
		if (!test_and_set_bit(port1, hub->child_usage_bits)) {
			pm_runtime_get_noresume(&port_dev->dev);
			pm_request_resume(&udev->dev);
		}
	}

	usb_autopm_put_interface(intf);

	return retval;
}

static int usb_port_runtime_suspend(struct device *dev)
{
	struct usb_port *port_dev = to_usb_port(dev);
	struct usb_device *hdev = to_usb_device(dev->parent->parent);
	struct usb_interface *intf = to_usb_interface(dev->parent);
	struct usb_hub *hub = usb_hub_to_struct_hub(hdev);
	struct usb_port *peer = port_dev->peer;
	int port1 = port_dev->portnum;
	int retval;

	if (!hub)
		return -EINVAL;
	if (hub->in_reset)
		return -EBUSY;

	if (dev_pm_qos_flags(&port_dev->dev, PM_QOS_FLAG_NO_POWER_OFF)
			== PM_QOS_FLAGS_ALL)
		return -EAGAIN;

	if (usb_port_block_power_off)
		return -EBUSY;

	retval = usb_autopm_get_interface(intf);
	if (retval < 0)
		return retval;

	retval = usb_hub_set_port_power(hdev, hub, port1, false);
	usb_clear_port_feature(hdev, port1, USB_PORT_FEAT_C_CONNECTION);
	if (!port_dev->is_superspeed)
		usb_clear_port_feature(hdev, port1, USB_PORT_FEAT_C_ENABLE);
	usb_autopm_put_interface(intf);

	/*
	 * Our peer usb3 port may now be able to suspend, so
	 * asynchronously queue a suspend request to observe that this
	 * usb2 port is now off.
	 */
	if (!port_dev->is_superspeed && peer)
		pm_runtime_put(&peer->dev);

	return retval;
}
#endif

static void usb_port_shutdown(struct device *dev)
{
	struct usb_port *port_dev = to_usb_port(dev);

	if (port_dev->child)
		usb_disable_usb2_hardware_lpm(port_dev->child);
}

static const struct dev_pm_ops usb_port_pm_ops = {
#ifdef CONFIG_PM
	.runtime_suspend =	usb_port_runtime_suspend,
	.runtime_resume =	usb_port_runtime_resume,
#endif
};

struct device_type usb_port_device_type = {
	.name =		"usb_port",
	.release =	usb_port_device_release,
	.pm =		&usb_port_pm_ops,
};

static struct device_driver usb_port_driver = {
	.name = "usb",
	.owner = THIS_MODULE,
	.shutdown = usb_port_shutdown,
};

static int link_peers(struct usb_port *left, struct usb_port *right)
{
	struct usb_port *ss_port, *hs_port;
	int rc;

	if (left->peer == right && right->peer == left)
		return 0;

	if (left->peer || right->peer) {
		struct usb_port *lpeer = left->peer;
		struct usb_port *rpeer = right->peer;
		char *method;

		if (left->location && left->location == right->location)
			method = "location";
		else
			method = "default";

		pr_debug("usb: failed to peer %s and %s by %s (%s:%s) (%s:%s)\n",
			dev_name(&left->dev), dev_name(&right->dev), method,
			dev_name(&left->dev),
			lpeer ? dev_name(&lpeer->dev) : "none",
			dev_name(&right->dev),
			rpeer ? dev_name(&rpeer->dev) : "none");
		return -EBUSY;
	}

	rc = sysfs_create_link(&left->dev.kobj, &right->dev.kobj, "peer");
	if (rc)
		return rc;
	rc = sysfs_create_link(&right->dev.kobj, &left->dev.kobj, "peer");
	if (rc) {
		sysfs_remove_link(&left->dev.kobj, "peer");
		return rc;
	}

	/*
	 * We need to wake the HiSpeed port to make sure we don't race
	 * setting ->peer with usb_port_runtime_suspend().  Otherwise we
	 * may miss a suspend event for the SuperSpeed port.
	 */
	if (left->is_superspeed) {
		ss_port = left;
		WARN_ON(right->is_superspeed);
		hs_port = right;
	} else {
		ss_port = right;
		WARN_ON(!right->is_superspeed);
		hs_port = left;
	}
	pm_runtime_get_sync(&hs_port->dev);

	left->peer = right;
	right->peer = left;

	/*
	 * The SuperSpeed reference is dropped when the HiSpeed port in
	 * this relationship suspends, i.e. when it is safe to allow a
	 * SuperSpeed connection to drop since there is no risk of a
	 * device degrading to its powered-off HiSpeed connection.
	 *
	 * Also, drop the HiSpeed ref taken above.
	 */
	pm_runtime_get_sync(&ss_port->dev);
	pm_runtime_put(&hs_port->dev);

	return 0;
}

static void link_peers_report(struct usb_port *left, struct usb_port *right)
{
	int rc;

	rc = link_peers(left, right);
	if (rc == 0) {
		dev_dbg(&left->dev, "peered to %s\n", dev_name(&right->dev));
	} else {
		dev_dbg(&left->dev, "failed to peer to %s (%d)\n",
				dev_name(&right->dev), rc);
		pr_warn_once("usb: port power management may be unreliable\n");
		usb_port_block_power_off = 1;
	}
}

static void unlink_peers(struct usb_port *left, struct usb_port *right)
{
	struct usb_port *ss_port, *hs_port;

	WARN(right->peer != left || left->peer != right,
			"%s and %s are not peers?\n",
			dev_name(&left->dev), dev_name(&right->dev));

	/*
	 * We wake the HiSpeed port to make sure we don't race its
	 * usb_port_runtime_resume() event which takes a SuperSpeed ref
	 * when ->peer is !NULL.
	 */
	if (left->is_superspeed) {
		ss_port = left;
		hs_port = right;
	} else {
		ss_port = right;
		hs_port = left;
	}

	pm_runtime_get_sync(&hs_port->dev);

	sysfs_remove_link(&left->dev.kobj, "peer");
	right->peer = NULL;
	sysfs_remove_link(&right->dev.kobj, "peer");
	left->peer = NULL;

	/* Drop the SuperSpeed ref held on behalf of the active HiSpeed port */
	pm_runtime_put(&ss_port->dev);

	/* Drop the ref taken above */
	pm_runtime_put(&hs_port->dev);
}

/*
 * For each usb hub device in the system check to see if it is in the
 * peer domain of the given port_dev, and if it is check to see if it
 * has a port that matches the given port by location
 */
static int match_location(struct usb_device *peer_hdev, void *p)
{
	int port1;
	struct usb_hcd *hcd, *peer_hcd;
	struct usb_port *port_dev = p, *peer;
	struct usb_hub *peer_hub = usb_hub_to_struct_hub(peer_hdev);
	struct usb_device *hdev = to_usb_device(port_dev->dev.parent->parent);

	if (!peer_hub)
		return 0;

	hcd = bus_to_hcd(hdev->bus);
	peer_hcd = bus_to_hcd(peer_hdev->bus);
	/* peer_hcd is provisional until we verify it against the known peer */
	if (peer_hcd != hcd->shared_hcd)
		return 0;

	for (port1 = 1; port1 <= peer_hdev->maxchild; port1++) {
		peer = peer_hub->ports[port1 - 1];
		if (peer && peer->location == port_dev->location) {
			link_peers_report(port_dev, peer);
			return 1; /* done */
		}
	}

	return 0;
}

/*
 * Find the peer port either via explicit platform firmware "location"
 * data, the peer hcd for root hubs, or the upstream peer relationship
 * for all other hubs.
 */
static void find_and_link_peer(struct usb_hub *hub, int port1)
{
	struct usb_port *port_dev = hub->ports[port1 - 1], *peer;
	struct usb_device *hdev = hub->hdev;
	struct usb_device *peer_hdev;
	struct usb_hub *peer_hub;

	/*
	 * If location data is available then we can only peer this port
	 * by a location match, not the default peer (lest we create a
	 * situation where we need to go back and undo a default peering
	 * when the port is later peered by location data)
	 */
	if (port_dev->location) {
		/* we link the peer in match_location() if found */
		usb_for_each_dev(port_dev, match_location);
		return;
	} else if (!hdev->parent) {
		struct usb_hcd *hcd = bus_to_hcd(hdev->bus);
		struct usb_hcd *peer_hcd = hcd->shared_hcd;

		if (!peer_hcd)
			return;

		peer_hdev = peer_hcd->self.root_hub;
	} else {
		struct usb_port *upstream;
		struct usb_device *parent = hdev->parent;
		struct usb_hub *parent_hub = usb_hub_to_struct_hub(parent);

		if (!parent_hub)
			return;

		upstream = parent_hub->ports[hdev->portnum - 1];
		if (!upstream || !upstream->peer)
			return;

		peer_hdev = upstream->peer->child;
	}

	peer_hub = usb_hub_to_struct_hub(peer_hdev);
	if (!peer_hub || port1 > peer_hdev->maxchild)
		return;

	/*
	 * we found a valid default peer, last check is to make sure it
	 * does not have location data
	 */
	peer = peer_hub->ports[port1 - 1];
	if (peer && peer->location == 0)
		link_peers_report(port_dev, peer);
}

#ifdef MY_ABC_HERE
/**
 * syno_usb_hub_node_get - return usb or hub slot index by usb 2 or 3 bus and port
 * @hdev [IN]: usb device
 * @portnum [IN]: port number
 * @prefix [IN]: node name in dts
 *
 * return >0: slot number
 *        -1: slot not found
 */
static struct device_node *__syno_usb_hub_node_get(const struct usb_device *hdev, const int portnum, const char *szNodeName)
{
	int size = 0;
	struct device_node *pUsbSlotNode = NULL, *pUsbNode = NULL;
	char *szUsbNodeBusPort = NULL;
	char szUsbBusPort[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};

	if (NULL == hdev || NULL == szNodeName || 0 > portnum) {
		goto END;
	}

	if (0 == strncmp(dev_name(&hdev->dev), "usb", strlen("usb"))) {
		snprintf(szUsbBusPort, SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s-%d", dev_name(&hdev->dev), portnum);
		sscanf(szUsbBusPort, "usb%s", szUsbBusPort);
	} else {
		snprintf(szUsbBusPort, SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s.%d", dev_name(&hdev->dev), portnum);
	}

	for_each_child_of_node(of_root, pUsbSlotNode) {
		if (pUsbSlotNode->name && (strlen(pUsbSlotNode->name) == strlen(szNodeName))
			&& 0 == (of_node_cmp(pUsbSlotNode->name, szNodeName))) {

			pUsbNode = of_get_child_by_name(pUsbSlotNode, DT_USB2);
			szUsbNodeBusPort = (char *)of_get_property(pUsbNode, DT_USB_PORT, &size);
			if (szUsbNodeBusPort && (strlen(szUsbNodeBusPort) == strlen(szUsbBusPort))
				&& 0 == strcmp(szUsbNodeBusPort, szUsbBusPort)) {
				goto END;
			}
			of_node_put(pUsbNode);

			pUsbNode = of_get_child_by_name(pUsbSlotNode, DT_USB3);
			szUsbNodeBusPort = (char *)of_get_property(pUsbNode, DT_USB_PORT, &size);
			if (szUsbNodeBusPort && (strlen(szUsbNodeBusPort) == strlen(szUsbBusPort))
				&& 0 == strcmp(szUsbNodeBusPort, szUsbBusPort)) {
				goto END;
			}
			of_node_put(pUsbNode);
		}
	}
END:
	return pUsbSlotNode;
}

#if defined(MY_ABC_HERE)
/**
 * syno_hub_node_get - return hub slot index by usb 2 or 3 bus and port
 * @hdev [IN]: usb device
 * @portnum [IN]: port number
 *
 * return >0: slot number
 *        -1: slot not found
 */
struct device_node *syno_hub_node_get(const struct usb_device *hdev, const int portnum)
{
	struct device_node *pDeviceNode = NULL;
	if (NULL == hdev || 1 > portnum) {
		goto END;
	}
	pDeviceNode = __syno_usb_hub_node_get(hdev, portnum, DT_HUB_SLOT);
END:
	return pDeviceNode;
}
EXPORT_SYMBOL(syno_hub_node_get);
#endif /* MY_ABC_HERE */

/**
 * syno_usb_hub_node_get - return usb or hub slot index by usb 2 or 3 bus and port
 * @hdev [IN]: usb device
 * @portnum [IN]: port number
 *
 * return >0: slot number
 *        -1: slot not found
 */
struct device_node *syno_usb_hub_node_get(const struct usb_device *hdev, const int portnum)
{
	struct device_node *pDeviceNode = NULL;
	if (NULL == hdev || 0 > portnum) {
		goto END;
	}
	pDeviceNode = __syno_usb_hub_node_get(hdev, portnum, DT_USB_SLOT);
	if (NULL != pDeviceNode) {
		goto END;
	}
	pDeviceNode = __syno_usb_hub_node_get(hdev, portnum, DT_HUB_SLOT);
END:
	return pDeviceNode;
}
EXPORT_SYMBOL(syno_usb_hub_node_get);

/**
 * syno_vbus_gpio_set - set vbus gpio
 * @vbus_gpio_pin [IN]: vbus gpio pin
 * @vbus_gpio_polarity [IN]: polarity of the pin
 * @port1 [IN]: usb port
 *
 * return void
 */
void syno_vbus_gpio_set(const char *vbus_host_addr, const unsigned vbus_gpio_pin, const unsigned vbus_gpio_polarity, const int port1)
{
	if (UINT_MAX == vbus_gpio_pin || UINT_MAX == vbus_gpio_polarity || 0 > port1) {
		return;
	}

	if (vbus_gpio_polarity != SYNO_GPIO_READ(vbus_gpio_pin)) {
		SYNO_GPIO_WRITE(vbus_gpio_pin, vbus_gpio_polarity);
		printk(KERN_INFO " port%d is going to power up Vbus by "
				"GPIO#%d(%s)\n", port1, vbus_gpio_pin,
				vbus_gpio_polarity ? "ACTIVE_HIGH" : "ACTIVE_LOW");

		mdelay(100);
	}
	return;
}
#endif /* MY_ABC_HERE */

int usb_hub_create_port_device(struct usb_hub *hub, int port1)
{
	struct usb_port *port_dev;
	struct usb_device *hdev = hub->hdev;
	int retval;
#ifdef MY_DEF_HERE
	extern char gSynoCastratedXhcAddr[CONFIG_SYNO_USB_NUM_CASTRATED_XHC][32];
	extern unsigned gSynoCastratedXhcPortBitmap[CONFIG_SYNO_USB_NUM_CASTRATED_XHC];
	int i = 0;
#endif /* MY_DEF_HERE */
#ifdef MY_ABC_HERE
	u32 vbusGpioPin = U32_MAX, vbusGpioPolarity = 0;
	char *szGpioShared = NULL;
	struct device_node *pVbusNode = NULL, *pUSBNode = NULL;
#endif /* MY_ABC_HERE */

	port_dev = kzalloc(sizeof(*port_dev), GFP_KERNEL);
	if (!port_dev)
		return -ENOMEM;

	port_dev->req = kzalloc(sizeof(*(port_dev->req)), GFP_KERNEL);
	if (!port_dev->req) {
		kfree(port_dev);
		return -ENOMEM;
	}

	hub->ports[port1 - 1] = port_dev;
	port_dev->portnum = port1;
	set_bit(port1, hub->power_bits);
	port_dev->dev.parent = hub->intfdev;
	if (hub_is_superspeed(hdev)) {
		port_dev->usb3_lpm_u1_permit = 1;
		port_dev->usb3_lpm_u2_permit = 1;
		port_dev->dev.groups = port_dev_usb3_group;
	} else
		port_dev->dev.groups = port_dev_group;
	port_dev->dev.type = &usb_port_device_type;
	port_dev->dev.driver = &usb_port_driver;
	if (hub_is_superspeed(hub->hdev))
		port_dev->is_superspeed = 1;
	dev_set_name(&port_dev->dev, "%s-port%d", dev_name(&hub->hdev->dev),
			port1);

#ifdef MY_ABC_HERE
	port_dev->power_cycle_counter = SYNO_POWER_CYCLE_TRIES;
#endif /* MY_ABC_HERE */

#ifdef MY_DEF_HERE
	if (hdev && hdev->serial) {
		for (i = 0; i < CONFIG_SYNO_USB_NUM_CASTRATED_XHC; i++) {
			if (0 == strcmp(gSynoCastratedXhcAddr[i], hdev->serial) &&
				gSynoCastratedXhcPortBitmap[i] & (0x01 << (port1 - 1))) {
				/* Castrated xHC-port is an outer USB-port which is serviced by
				 * a xHCI and without physical links of USB3 (i.e. without USB3
				 * capability.
				 */
				port_dev->flag |= SYNO_USB_PORT_CASTRATED_XHC;
				if (hub_is_superspeed(hdev))
					dev_info (&port_dev->dev, "is a castrated xHC-port\n");
			}
		}
	}
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE
	port_dev->syno_vbus_gpp = -1;
	port_dev->syno_vbus_gpp_pol = -1;
	if (hdev && hdev->serial) {
		pUSBNode = syno_usb_hub_node_get(hdev, port1);
		if (NULL == pUSBNode) {
			goto PUT_NODE;
		}

		pVbusNode = of_get_child_by_name(pUSBNode, DT_VBUS);
		if (NULL == pVbusNode) {
			goto PUT_NODE;
		}

		if (0 != of_property_read_u32_index(pVbusNode, DT_SYNO_GPIO, SYNO_GPIO_PIN, &vbusGpioPin)) {
			printk(KERN_ERR "%s reading vbus vbusGpioPin failed.\n", __func__);
			goto PUT_NODE;
		}

		if (0 != of_property_read_u32_index(pVbusNode, DT_SYNO_GPIO, SYNO_POLARITY_PIN, &vbusGpioPolarity)) {
			printk(KERN_ERR "%s reading vbus vbusGpioPolarity failed.\n", __func__);
			goto PUT_NODE;
		}

		syno_vbus_gpio_set(NULL, vbusGpioPin, vbusGpioPolarity, port1);

		szGpioShared = (char *)of_get_property(pUSBNode, DT_SHARED, NULL);

		if (szGpioShared && 0 == strncmp(szGpioShared, "true", strlen("true"))) {
			goto PUT_NODE;
		}

		port_dev->syno_vbus_gpp = vbusGpioPin;
		port_dev->syno_vbus_gpp_pol = vbusGpioPolarity;
		printk(KERN_INFO "host %s port %d has Vbus GPIO#%d with polarity "
				"%s\n",hdev->serial, port1, port_dev->syno_vbus_gpp,
				port_dev->syno_vbus_gpp_pol ? "ACTIVE_HIGH" : "ACTIVE_LOW");
PUT_NODE:
		if (pUSBNode) {
			of_node_put(pUSBNode);
		}
		if (pVbusNode) {
			of_node_put(pVbusNode);
		}
	}
#endif /* MY_ABC_HERE */

	mutex_init(&port_dev->status_lock);
	retval = device_register(&port_dev->dev);
	if (retval) {
		put_device(&port_dev->dev);
		return retval;
	}

	/* Set default policy of port-poweroff disabled. */
	retval = dev_pm_qos_add_request(&port_dev->dev, port_dev->req,
			DEV_PM_QOS_FLAGS, PM_QOS_FLAG_NO_POWER_OFF);
	if (retval < 0) {
		device_unregister(&port_dev->dev);
		return retval;
	}

	find_and_link_peer(hub, port1);

	/*
	 * Enable runtime pm and hold a refernce that hub_configure()
	 * will drop once the PM_QOS_NO_POWER_OFF flag state has been set
	 * and the hub has been fully registered (hdev->maxchild set).
	 */
	pm_runtime_set_active(&port_dev->dev);
	pm_runtime_get_noresume(&port_dev->dev);
	pm_runtime_enable(&port_dev->dev);
	device_enable_async_suspend(&port_dev->dev);

	/*
	 * Keep hidden the ability to enable port-poweroff if the hub
	 * does not support power switching.
	 */
	if (!hub_is_port_power_switchable(hub))
		return 0;

	/* Attempt to let userspace take over the policy. */
	retval = dev_pm_qos_expose_flags(&port_dev->dev,
			PM_QOS_FLAG_NO_POWER_OFF);
	if (retval < 0) {
		dev_warn(&port_dev->dev, "failed to expose pm_qos_no_poweroff\n");
		return 0;
	}

	/* Userspace owns the policy, drop the kernel 'no_poweroff' request. */
	retval = dev_pm_qos_remove_request(port_dev->req);
	if (retval >= 0) {
		kfree(port_dev->req);
		port_dev->req = NULL;
	}
	return 0;
}

void usb_hub_remove_port_device(struct usb_hub *hub, int port1)
{
	struct usb_port *port_dev = hub->ports[port1 - 1];
	struct usb_port *peer;

	peer = port_dev->peer;
	if (peer)
		unlink_peers(port_dev, peer);
	device_unregister(&port_dev->dev);
}
