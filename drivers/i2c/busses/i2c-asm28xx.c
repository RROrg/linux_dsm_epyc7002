/* Copyright (c) 2000-2021 Synology Inc. All rights reserved. */
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/synolib.h>

#define ASM28XX_I2C_CTRL_STS_REG	0xF8 //USP PCI offset F8h: Control/Status register
#define ASM28XX_I2C_SLAVE_ADDR_REG	0xF9 //USP PCI offset F9h: Slave Address register
#define ASM28XX_I2C_COMMAND_REG		0xFA //USP PCI offset FAh: Command Code register
#define ASM28XX_I2C_DATA_REG		0xFB //USP PCI offset FBh: Data register
#define ASM28XX_GPIO_BASE			0x80
#define ASM28XX_GPIO_INPUT_REGISTER_BASE			0x930
#define ASM28XX_GPIO_OUTPUT_REGISTER_BASE			0x928
#define ASM28XX_PCI_CFG_SWITCH			0xFFF

struct asm28xx_priv {
	struct i2c_adapter adapter;
	struct pci_dev *pci_dev;
	struct mutex xfer_lock;
	int busno;
};

typedef union _I2C_CTRL_STS_ {
	struct {
		u8	Run : 1;	//bit[0]: run
		u8	RW : 1;		//bit[1]: read/write protocol
		u8	Rsvd : 2;	//bit[3:2]: reserved
		u8	Err : 4;	//bit[7:4]: error status
	};
	u8	AsByte;
} I2C_CTRL_STS;

//******************************************************************************
// Asm28XXI2cReadByte
// 		Read Byte Protocol
// Input:	Bus	- ASM28XX USP PCI bus nu mber
//		Dev	- ASM28XX USP PCI device number
//		Fun	- ASM28XX USP PCI function number
//		Addr	- I2C slave address. Bit[6:0] is 7 bits slave address
//		Cmd	- I2C command code
//		pData	- Data buffer for read byte
// Output:	TRUE	- I2C operation success
//		FALSE	- Error occurred
//******************************************************************************
bool Asm28XXI2cReadByte(struct asm28xx_priv *priv, u8 Addr, u8 Cmd, u8 *pData)
{
	int i, ret = -1;
	int retry = priv->adapter.timeout * 1000 / HZ / 10;
	I2C_CTRL_STS CtrlSts;
	struct pci_dev *dev = priv->pci_dev;

	//Write slave address
	pci_write_config_byte(dev, ASM28XX_I2C_SLAVE_ADDR_REG, Addr);
	//Write command code
	pci_write_config_byte(dev, ASM28XX_I2C_COMMAND_REG, Cmd);
	//Start I2C process
	CtrlSts.AsByte = 0; //Clear variable
	CtrlSts.RW = 0; //Read byte protocol
	CtrlSts.Run = 1; //Run
	pci_write_config_byte(dev, ASM28XX_I2C_CTRL_STS_REG, CtrlSts.AsByte);

	for (i = 0; i < retry; ++i) {
		pci_read_config_byte(dev, ASM28XX_I2C_CTRL_STS_REG, &CtrlSts.AsByte);
		if (CtrlSts.Err != 0) {
			ret = -EIO;
			goto END;
		}
		if (CtrlSts.Run == 0)
			break;
		msleep(10);
	}
	if (i == retry) {
		ret = -EIO;
		goto END;
	}
	//Read byte data
	pci_read_config_byte(dev, ASM28XX_I2C_DATA_REG, pData);
	ret = 0;
END:
	return ret;
}

int Asm28XXGpioWriteByte (struct asm28xx_priv *priv, u8 Addr, u8 Data)
{
	int ret = -1;
	struct pci_dev *dev = priv->pci_dev;

	pci_write_config_byte(dev, Addr + ASM28XX_GPIO_OUTPUT_REGISTER_BASE, Data);
	ret = 0;
	return ret;
}

int Asm28XXGpioReadByte (struct asm28xx_priv *priv, u8 Addr, u8 *pData)
{
	int ret = -1;
	struct pci_dev *dev = priv->pci_dev;

	pci_read_config_byte(dev, Addr + ASM28XX_GPIO_INPUT_REGISTER_BASE, pData);
	ret = 0;
	return ret;
}


//*************************************************************************************************************
// Asm28XXI2cWriteByte
//		Read Byte Protocol
// Input:	Bus	- ASM28XX USP PCI bus number
//		Dev	- ASM28XX USP PCI device number
//		Fun	- ASM28XX USP PCI function number
//		Addr	- I2C slave address. Bit[6:0] is 7 bits slave address
//		Cmd	- I2C command code
//		Data	- Byte data
// Output:	0	- I2C operation success
//		others	- Error occurred
//***
int Asm28XXI2cWriteByte (struct asm28xx_priv *priv, u8 Addr, u8 Cmd, u8 Data)
{
	int i, ret = -1;
	int retry = priv->adapter.timeout * 1000 / HZ / 10;
	I2C_CTRL_STS CtrlSts;
	struct pci_dev *dev = priv->pci_dev;

	//Write slave address
	pci_write_config_byte(dev, ASM28XX_I2C_SLAVE_ADDR_REG, Addr);
	//Write command code
	pci_write_config_byte(dev, ASM28XX_I2C_COMMAND_REG, Cmd);
	//Write data
	pci_write_config_byte(dev, ASM28XX_I2C_DATA_REG, Data);
	//Start I2C process
	CtrlSts.AsByte = 0; //Clear variable
	CtrlSts.RW = 1; //Write byte protocol
	CtrlSts.Run = 1; //Run
	pci_write_config_byte(dev, ASM28XX_I2C_CTRL_STS_REG, CtrlSts.AsByte);
	for (i = 0; i < retry; ++i)
	{
		pci_read_config_byte(dev, ASM28XX_I2C_CTRL_STS_REG, &CtrlSts.AsByte);
		if (CtrlSts.Err != 0) {
			ret = -EIO;
			goto END;
		}
		if (CtrlSts.Run == 0)
			break;
		msleep(10);
	}
	if (i == retry) {
		ret = -EIO;
		goto END;
	}
	ret = 0;
END:
	return ret;
}

/* Return negative errno on error. */
static s32 asm28XX_access(struct i2c_adapter *adap, u16 addr,
		unsigned short flags, char read_write, u8 command,
		int size, union i2c_smbus_data *data)
{
	int ret = 0;
	u8 Addr;
	struct asm28xx_priv *priv = i2c_get_adapdata(adap);
	struct pci_dev *dev = priv->pci_dev;

	mutex_lock(&priv->xfer_lock);
	if (flags & I2C_CLIENT_PEC) {
		goto out;
	}

	if (size != I2C_SMBUS_BYTE_DATA) {
		goto out;
	}

	if (Addr >= ASM28XX_GPIO_BASE) {
		pci_write_config_byte(dev, ASM28XX_PCI_CFG_SWITCH, 0x1);
		if (read_write == I2C_SMBUS_WRITE) {
			ret = Asm28XXGpioWriteByte(priv, Addr - ASM28XX_GPIO_BASE, data->byte);
		} else {
			ret = Asm28XXGpioReadByte(priv, Addr - ASM28XX_GPIO_BASE, &data->byte);
		}
	} else {
		Addr = addr & 0x7f;
		pci_write_config_byte(dev, ASM28XX_PCI_CFG_SWITCH, 0x0);
		if (read_write == I2C_SMBUS_WRITE) {
			ret = Asm28XXI2cWriteByte(priv, Addr, command, data->byte);
		} else {
			ret = Asm28XXI2cReadByte(priv, Addr, command, &data->byte);
		}
	}
out:
	mutex_unlock(&priv->xfer_lock);
	return ret;
}

static u32 asm28XX_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA;
}

static const struct i2c_algorithm asm28XX_smbus_algorithm = {
	.smbus_xfer     = asm28XX_access,
	.functionality  = asm28XX_func,
};

static inline unsigned int asm28XX_get_adapter_class(struct asm28xx_priv *priv)
{
	return I2C_CLASS_HWMON;
}

static int asm28XX_probe(struct platform_device *pdev)
{
	int err;
	struct asm28xx_priv *priv;
	struct pci_dev **pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(struct asm28xx_priv), GFP_KERNEL);
	if (!priv) {
		return -ENOMEM;
	}

	strlcpy(priv->adapter.name, "ASM28XX I2C adapter", sizeof(priv->adapter.name));
	i2c_set_adapdata(&priv->adapter, priv);
	priv->adapter.owner = THIS_MODULE;
	priv->adapter.class = asm28XX_get_adapter_class(priv);
	priv->adapter.algo = &asm28XX_smbus_algorithm;
	priv->adapter.dev.parent = &pdev->dev;
	priv->adapter.retries = 3;
	/* Default timeout in interrupt mode: 200 ms */
	priv->adapter.timeout = HZ / 5;
	priv->adapter.nr = pdev->id;
	priv->pci_dev = *pdata;

	mutex_init(&priv->xfer_lock);

	err = i2c_add_numbered_adapter(&priv->adapter);
	if (err) {
		dev_err(&pdev->dev, "Failed to add SMBus adapter\n");
		return err;
	}
	platform_set_drvdata(pdev, priv);
	return 0;
}

static int asm28XX_remove(struct platform_device *pdev)
{
	struct asm28xx_priv *priv = platform_get_drvdata(pdev);

	if (priv) {
		i2c_del_adapter(&priv->adapter);
	}

	return 0;
}

static struct platform_driver asm28XX_driver = {
	.driver = {
		.name = "asm28xx-i2c",
	},
	.probe	= asm28XX_probe,
	.remove	= asm28XX_remove,
};

static int __init i2c_asm28XX_init(void)
{
	return platform_driver_register(&asm28XX_driver);
}

static void __exit i2c_asm28XX_exit(void)
{
	platform_driver_unregister(&asm28XX_driver);
}

MODULE_AUTHOR("Jason Peng <jasonpeng@synology.com>");
MODULE_DESCRIPTION("ASM28XX SMBus driver");
MODULE_LICENSE("GPL");

module_init(i2c_asm28XX_init);
module_exit(i2c_asm28XX_exit);
