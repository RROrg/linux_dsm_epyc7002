/* Copyright (c) 2000-2021 Synology Inc. All rights reserved. */
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/synolib.h>
#include "portdrv.h"

#define ASM28xx_I2C_NUM_MAX 16
static struct platform_device *asm28xx_device[ASM28xx_I2C_NUM_MAX];

static int asm28xx_i2c_probe(struct pcie_device *dev)
{
	int busno = 0, err = 0;

	busno = syno_pci_dev_to_i2c_bus(dev->port);
	if (busno < 0 || busno >= ASM28xx_I2C_NUM_MAX) {
		err = -EINVAL;
		goto END;
	}

	asm28xx_device[busno] = platform_device_alloc("asm28xx-i2c", busno);
	if (NULL == asm28xx_device[busno]) {
		err = -ENOMEM;
		goto END;
	}

	if (platform_device_add_data(asm28xx_device[busno], &(dev->port), sizeof(&(dev->port)))) {
		pr_err("Adding pci_dev data failed\n");
		goto END;
	}

	if (platform_device_add(asm28xx_device[busno])) {
		pr_err("Adding pci_dev failed\n");
		goto END;
	}

	return 0;
END:
	if (0 <= busno && asm28xx_device[busno]) {
		platform_device_put(asm28xx_device[busno]);
	}
	return err;
}

static void asm28xx_i2c_remove(struct pcie_device *dev)
{
	int busno = syno_pci_dev_to_i2c_bus(dev->port);

	if (busno < 0 || busno >= ASM28xx_I2C_NUM_MAX) {
		return;
	}

	platform_device_unregister(asm28xx_device[busno]);
	asm28xx_device[busno] = NULL;
}

static struct pcie_port_service_driver asm28xx_i2c_driver = {
	.name		= "i2c",
	.port_type	= PCIE_ANY_PORT,
	.service	= PCIE_PORT_SERVICE_SWITCH_I2C,
	.probe		= asm28xx_i2c_probe,
	.remove		= asm28xx_i2c_remove,
};

int __init pcie_switch_i2c_service_init(void)
{
	memset(asm28xx_device, 0, sizeof(asm28xx_device));
	return pcie_port_service_register(&asm28xx_i2c_driver);
}

