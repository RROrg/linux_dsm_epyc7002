#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* Copyright (c) 2000-2020 Synology Inc. All rights reserved. */
#ifndef __SYNO_FDT_H_
#define __SYNO_FDT_H_

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/device.h>

#ifdef MY_DEF_HERE
int syno_pmbus_property_get(unsigned int *pmbus_property, const char *property_name, int index);
#endif /* MY_DEF_HERE */
bool syno_of_i2c_driver_match_device(struct device *dev, const struct device_driver *drv);
struct device_node* syno_of_i2c_bus_match(struct device *dev, int* index);
struct device_node* syno_of_i2c_device_match(struct i2c_client *client, const char *i2c_dev_name, struct device_node *pI2CNode);
struct device_node* syno_of_i2c_adapter_match(struct i2c_adapter *adap);
struct i2c_adapter* syno_i2c_adapter_get_by_node(struct device_node *pI2CBusNode);

#endif /* __SYNO_FDT_H_ */
