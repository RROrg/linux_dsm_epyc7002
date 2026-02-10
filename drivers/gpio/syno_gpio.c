#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Synology NAS Board GPIO Setup
 *
 * Maintained by:  Comsumer Platform Team <cpt@synology.com>
 *
 * Copyright 2009-2015 Synology, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/syno_gpio.h>

#ifdef MY_ABC_HERE
#include <linux/synolib.h>
#include <linux/of.h>
#include <linux/string.h>
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#include <linux/leds.h>
#endif /* MY_ABC_HERE */

SYNO_GPIO syno_gpio = {
	.fan_ctrl =NULL,
	.fan_fail =NULL,
	.hdd_fail_led =NULL,
	.hdd_present_led =NULL,
	.hdd_act_led =NULL,
	.hdd_detect =NULL,
	.hdd_enable =NULL,
	.model_id =NULL,
	.alarm_led =NULL,
	.power_led =NULL,
	.disk_led_ctrl =NULL,
	.phy_led_ctrl =NULL,
	.copy_button_detect =NULL,
	.redundant_power_detect =NULL,
};
EXPORT_SYMBOL(syno_gpio);

void syno_gpio_direction_output(int pin, int pValue)
{
	int iErr = 0;
	iErr = gpio_request(pin, NULL);
	if (iErr) {
		printk("%s:%s(%d) gpio_request pin %d fail!\n", __FILE__, __FUNCTION__, __LINE__, pin);
		goto END;
	}
	iErr = gpio_direction_output(pin, pValue);
	if (iErr) {
		printk("%s:%s(%d) set gpio pin %d value %d fail!\n", __FILE__, __FUNCTION__, __LINE__, pin, pValue);
		goto UNLOCK;
	}
UNLOCK:
	gpio_free(pin);
END:
	return;
}
EXPORT_SYMBOL(syno_gpio_direction_output);

void syno_gpio_direction_input(int pin)
{
	int iErr = 0;
	iErr = gpio_request(pin, NULL);
	if (iErr) {
		printk("%s:%s(%d) gpio_request pin %d fail!\n", __FILE__, __FUNCTION__, __LINE__, pin);
		goto END;
	}
	iErr = gpio_direction_input(pin);
	if (iErr) {
		printk("%s:%s(%d) set gpio pin %d input fail!\n", __FILE__, __FUNCTION__, __LINE__, pin);
		goto UNLOCK;
	}
UNLOCK:
	gpio_free(pin);
END:
	return;
}
EXPORT_SYMBOL(syno_gpio_direction_input);

int syno_gpio_to_irq(int pin)
{
	return gpio_to_irq(pin);
}
EXPORT_SYMBOL(syno_gpio_to_irq);

int SYNO_GPIO_READ(int pin)
{
	return gpio_get_value(pin);
}
EXPORT_SYMBOL(SYNO_GPIO_READ);

void SYNO_GPIO_WRITE(int pin, int pValue)
{
	gpio_set_value(pin, pValue);
}
EXPORT_SYMBOL(SYNO_GPIO_WRITE);

int SynoHaveRPDetectPin(void)
{
	if (syno_gpio.redundant_power_detect &&
		HAVE_RP_DETECT(1) &&
		HAVE_RP_DETECT(2)) {
		return 1;
	}
	return 0;
}
int SynoAllRedundantPowerDetected(void)
{
	if (syno_gpio.redundant_power_detect && 2 == syno_gpio.redundant_power_detect->nr_gpio &&
		!(SYNO_GPIO_READ(RP_DETECT_PIN(1)) ^ SYNO_GPIO_READ(RP_DETECT_PIN(2)))) {
		return 1;
	}
	return 0;
}

#ifdef MY_ABC_HERE
extern int giSynoSpinupGroupDebug;
void DBG_SpinupGroupListGpio(void)
{
	int i = 0;
	if (giSynoSpinupGroupDebug && NULL != syno_gpio.hdd_detect) {
		for (i = 0; i < syno_gpio.hdd_detect->nr_gpio; i++) {
			printk("gpio debug: hdd detect pin %d, value= %d\n", HDD_DETECT_PIN(i + 1), SYNO_GPIO_READ(HDD_DETECT_PIN(i + 1)));
		}
		for (i = 0; i < syno_gpio.hdd_enable->nr_gpio; i++) {
			printk("gpio debug: hdd enable pin %d, value= %d\n", HDD_ENABLE_PIN(i + 1), SYNO_GPIO_READ(HDD_ENABLE_PIN(i + 1)));
		}
		if (syno_gpio.redundant_power_detect && 2 == syno_gpio.redundant_power_detect->nr_gpio) {
			printk("gpio debug: redundant power detect pin %d, value= %d\n", RP_DETECT_PIN(1), SYNO_GPIO_READ(RP_DETECT_PIN(1)));
			printk("gpio debug: redundant power detect pin %d, value= %d\n", RP_DETECT_PIN(2), SYNO_GPIO_READ(RP_DETECT_PIN(2)));
		}
	}
}
EXPORT_SYMBOL(DBG_SpinupGroupListGpio);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
/**
 * syno_disk_gpio_pin_get - get the property content of internal slot
 * @diskPort [IN]:          internel slot number
 * @szPropertyName [IN]:
 * @propertyIndex [IN]: index of number need be read in szPropertyName
 *
 * return >=0: property number in device tree of internal slot
 *        -1: fail
 */
u32 syno_disk_gpio_pin_get(const int diskPort, const char *szPropertyName, const int propertyIndex)
{
	int index= 0;
	u32 synoGpioPin = U32_MAX;
	struct device_node *pSlotNode = NULL;

	if (NULL == szPropertyName || 1 > diskPort || 0 >propertyIndex) {
		goto END;
	}

	for_each_child_of_node(of_root, pSlotNode) {
		// get index number of internal_slot, e.g. internal_slot@4 --> 4
		if (!pSlotNode->full_name || 1 != sscanf(pSlotNode->full_name, DT_INTERNAL_SLOT"@%d", &index)) {
			continue;
		}

		if (diskPort == index) {
			break;
		}
	}

	if (NULL == pSlotNode) {
		goto END;
	}
	of_property_read_u32_index(pSlotNode, szPropertyName, propertyIndex, &synoGpioPin);
	of_node_put(pSlotNode);

END:
	return synoGpioPin;
}
EXPORT_SYMBOL(syno_disk_gpio_pin_get);

/**
 * syno_disk_gpio_pin_have - determine the szPropertyName of the internal slot is defined in device tree
 * @diskPort [IN]:          internel slot number
 * @szPropertyName [IN]:
 *
 * return 1: property exist
 *        0: property not exist
 */
int syno_disk_gpio_pin_have(const int diskPort, const char *szPropertyName)
{
	u32 synoGpioPin = U32_MAX;
	int ret = -1;

	synoGpioPin = syno_disk_gpio_pin_get(diskPort, szPropertyName, SYNO_GPIO_PIN);

	if (U32_MAX != synoGpioPin) {
		ret = 1;
	} else {
		ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL(syno_disk_gpio_pin_have);
/**
 * syno_led_pin_get - get the szLedName pin of target slot
 * @szSlotName [IN]:    slot name
 * @diskPort [IN]:      slot number
 * @szLedName [IN]:		LED node name in device node
 * @propertyIndex [IN]: index of number need be read in DT_SYNO_GPIO
 *
 * return >=0: property number in device tree of target slot
 *        -1: fail
 */
u32 syno_led_pin_get(const char* szSlotName, const int diskPort, const char *szLedName, const int propertyIndex)
{
	u32 synoGpioPin = U32_MAX;
	struct device_node *pSlotNode = NULL, *pLedNode = NULL;
	char szFullName[MAX_NODENAME_LEN] = {0};

	if (NULL == szSlotName || NULL == szLedName || 1 > diskPort || 0 > propertyIndex) {
		goto END;
	}
	if (0 > snprintf(szFullName, MAX_NODENAME_LEN - 1, "%s@%d", szSlotName, diskPort)) {
		goto END;
	}

	for_each_child_of_node(of_root, pSlotNode) {
		if (pSlotNode->full_name && 0 == strcmp(pSlotNode->full_name, szFullName)) {
			break;
		}
	}

	if (NULL == pSlotNode) {
		goto END;
	}
	pLedNode = of_get_child_by_name(pSlotNode, szLedName);
	of_node_put(pSlotNode);
	if (NULL == pLedNode) {
		goto END;
	}
	of_property_read_u32_index(pLedNode, DT_SYNO_GPIO, propertyIndex, &synoGpioPin);
	of_node_put(pLedNode);

END:
	return synoGpioPin;
}
EXPORT_SYMBOL(syno_led_pin_get);

/**
 * syno_led_pin_have - determine the szLedName of the target slot is defined in device tree
 * @szSlotName [IN]: slot name
 * @diskPort [IN]:   slot number
 * @szLedName [IN]:	LED node name in device node
 *
 * return 1: szLedName exist
 *        0: szLedName not exist
 */
int syno_led_pin_have(const char* szSlotName, const int diskPort, const char *szLedName)
{
	u32 synoGpioPin = U32_MAX;
	int ret = -1;

	if (szSlotName && szLedName) {
		synoGpioPin = syno_led_pin_get(szSlotName, diskPort, szLedName, SYNO_GPIO_PIN);
	}

	if (U32_MAX != synoGpioPin) {
		ret = 1;
	} else {
		ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL(syno_led_pin_have);

/**
 * syno_led_name_get - get the szLedName led_name of target slot
 * @szSlotName [IN]:    slot name
 * @diskPort [IN]:      slot number
 * @szLedType [IN]:		LED node name in device node
 *
 */
int  syno_led_name_get(const char* szSlotName, const int diskPort, const char *szLedType, char *szSynoLedName, unsigned int cbSynoLedName)
{
	int iRet = -1;
	struct device_node *pSlotNode = NULL, *pLedNode = NULL;
	char szFullName[MAX_NODENAME_LEN] = {0};
	const char *szLedName = NULL;

	if (NULL == szSlotName || NULL == szLedType || 1 > diskPort || NULL == szSynoLedName) {
		goto END;
	}
	if (0 > snprintf(szFullName, MAX_NODENAME_LEN - 1, "%s@%d", szSlotName, diskPort)) {
		goto END;
	}

	for_each_child_of_node(of_root, pSlotNode) {
		if (pSlotNode->full_name && 0 == strcmp(pSlotNode->full_name, szFullName)) {
			break;
		}
	}

	if (NULL == pSlotNode) {
		goto END;
	}
	pLedNode = of_get_child_by_name(pSlotNode, szLedType);
	of_node_put(pSlotNode);
	if (NULL == pLedNode) {
		goto END;
	}
	of_property_read_string(pLedNode, DT_HDD_LED_NAME, &szLedName);
	of_node_put(pLedNode);
	if (0 > snprintf(szSynoLedName, cbSynoLedName, "%s", szLedName)) {
		goto END;
	}
	iRet = 0;
END:
	return iRet;
}
EXPORT_SYMBOL(syno_led_name_get);

/**
 * syno_led_type_get - get the szLedType led_type of target slot
 * @szSlotName [IN]:    slot name
 * @diskPort [IN]:      slot number
 *
 */
int  syno_led_type_get(const char* szSlotName, const int diskPort, char *szSynoLedType, unsigned int cbSynoLedType)
{
	int iRet = -1;
	struct device_node *pSlotNode = NULL;
	char szFullName[MAX_NODENAME_LEN] = {0};
	const char *szLedType = NULL;

	if (NULL == szSlotName || 1 > diskPort || NULL == szSynoLedType) {
		goto END;
	}
	if (0 > snprintf(szFullName, MAX_NODENAME_LEN - 1, "%s@%d", szSlotName, diskPort)) {
		goto END;
	}

	for_each_child_of_node(of_root, pSlotNode) {
		if (pSlotNode->full_name && 0 == strcmp(pSlotNode->full_name, szFullName)) {
			break;
		}
	}

	if (NULL == pSlotNode) {
		goto END;
	}

	of_property_read_string(pSlotNode, DT_HDD_LED_TYPE, &szLedType);
	of_node_put(pSlotNode);
	if (0 > snprintf(szSynoLedType, cbSynoLedType, "%s", szLedType)) {
		goto END;
	}
	iRet = 0;
END:
	return iRet;
}
EXPORT_SYMBOL(syno_led_type_get);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
/**
 * syno_led_dev_get - get the szLedType led_type of target slot
 * @szSlotName [IN]:    slot name
 * @diskPort [IN]:      slot number
 *
 */
struct led_classdev* syno_led_dev_get(const char* szSlotName, const int diskPort, const char* szledName)
{
	struct led_classdev* led_cdev = NULL;
	struct device_node *pSlotNode = NULL;
	struct device_node *pLedNode = NULL;
	char szFullName[MAX_NODENAME_LEN] = {0};

	if (NULL == szSlotName || 1 > diskPort || NULL == szledName) {
		goto END;
	}

	if (0 > snprintf(szFullName, MAX_NODENAME_LEN - 1, "%s@%d", szSlotName, diskPort)) {
		goto END;
	}

	for_each_child_of_node(of_root, pSlotNode) {
		if (pSlotNode->full_name && 0 == strcmp(pSlotNode->full_name, szFullName)) {
			break;
		}
	}

	if (NULL == pSlotNode) {
		goto END;
	}

	pLedNode = of_parse_phandle(pSlotNode, szledName, 0);
	of_node_put(pSlotNode);
	if (!pLedNode) {
		printk(KERN_WARNING "No LED %s.\n", szledName);
		goto END;
	}

	led_cdev = of_leddev_get(pLedNode);
	of_node_put(pLedNode);
	if (IS_ERR(led_cdev)) {
		led_cdev = NULL;
		printk(KERN_ERR "can't get class\n");
		goto END;
	}

END:
	return led_cdev;
}
EXPORT_SYMBOL(syno_led_dev_get);
#endif /* MY_ABC_HERE */
