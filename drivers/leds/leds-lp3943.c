#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/leds-lp3943.h>
#ifdef MY_ABC_HERE
#include <linux/spinlock.h>
#endif /* MY_ABC_HERE */
#ifdef CONFIG_SYNO_LEDS_LP3943_PROBE_ACPI
#include <linux/acpi.h>
#endif /* CONFIG_SYNO_LEDS_LP3943_PROBE_ACPI */

#define MAX_NUM_LEDS		16
#define MAX_BRIGHTNESS		255
#define LED_OFF			0

/* Registers */
#define LP3943_INPUT1		0x00
#define LP3943_PSC0		0x02
#define LP3943_PWM0		0x03
#define LP3943_PSC1		0x04
#define LP3943_PWM1		0x05
#define LP3943_LS0		0x06
#define LP3943_LS1		0x07
#define LP3943_LS2		0x08
#define LP3943_LS3		0x09

/* Mask, shift */
#define LP3943_SEL0_M		0x03
#define LP3943_SEL1_M		0x0C
#define LP3943_SEL2_M		0x30
#define LP3943_SEL3_M		0xC0
#define LP3943_SEL0_S		0
#define LP3943_SEL1_S		2
#define LP3943_SEL2_S		4
#define LP3943_SEL3_S		6

static const u8 mask_sel[] = {
	LP3943_SEL0_M,
	LP3943_SEL1_M,
	LP3943_SEL2_M,
	LP3943_SEL3_M,
};

static const u8 shift_sel[] = {
	LP3943_SEL0_S,
	LP3943_SEL1_S,
	LP3943_SEL2_S,
	LP3943_SEL3_S,
};

struct lp3943_led {
	int id;
	struct led_classdev cdev;
	struct lp3943_led_node *node;
	struct work_struct brtwork;
	u8 brightness;
#ifdef MY_ABC_HERE
	int retry_count;
#endif /* MY_ABC_HERE */
};

struct lp3943 {
	struct i2c_client *client;
	struct device *dev;
	struct lp3943_led led[MAX_NUM_LEDS];
	int num_leds;
};

#ifdef MY_ABC_HERE
static struct i2c_client *gpClient = NULL;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static DEFINE_MUTEX(ModeLock);

enum lp3943_led_channel ch0[] = {
	LP3943_LED0,
};

enum lp3943_led_channel ch1[] = {
	LP3943_LED1,
};

enum lp3943_led_channel ch2[] = {
	LP3943_LED2,
};

enum lp3943_led_channel ch3[] = {
	LP3943_LED3,
};

enum lp3943_led_channel ch4[] = {
	LP3943_LED4,
};

enum lp3943_led_channel ch5[] = {
	LP3943_LED5,
};

enum lp3943_led_channel ch6[] = {
	LP3943_LED6,
};

enum lp3943_led_channel ch7[] = {
	LP3943_LED7,
};

enum lp3943_led_channel ch8[] = {
	LP3943_LED8,
};

enum lp3943_led_channel ch9[] = {
	LP3943_LED9,
};

enum lp3943_led_channel ch10[] = {
	LP3943_LED10,
};

enum lp3943_led_channel ch11[] = {
	LP3943_LED11,
};

enum lp3943_led_channel ch12[] = {
	LP3943_LED12,
};

enum lp3943_led_channel ch13[] = {
	LP3943_LED13,
};

enum lp3943_led_channel ch14[] = {
	LP3943_LED14,
};

enum lp3943_led_channel ch15[] = {
	LP3943_LED15,
};

struct lp3943_led_node syno_led_nodes[] = {
	{
		.name = "syno_led0",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch0,
		.num_channels = ARRAY_SIZE(ch0),
		.default_trigger = "syno_led0_ledtrig",
	},	{
		.name = "syno_led1",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch1,
		.num_channels = ARRAY_SIZE(ch1),
		.default_trigger = "syno_led1_ledtrig",
	},	{
		.name = "syno_led2",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch2,
		.num_channels = ARRAY_SIZE(ch2),
		.default_trigger = "syno_led2_ledtrig",
	},	{
		.name = "syno_led3",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch3,
		.num_channels = ARRAY_SIZE(ch3),
		.default_trigger = "syno_led3_ledtrig",
	},	{
		.name = "syno_led4",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch4,
		.num_channels = ARRAY_SIZE(ch4),
		.default_trigger = "syno_led4_ledtrig",
	},	{
		.name = "syno_led5",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch5,
		.num_channels = ARRAY_SIZE(ch5),
		.default_trigger = "syno_led5_ledtrig",
	},	{
		.name = "syno_led6",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch6,
		.num_channels = ARRAY_SIZE(ch6),
		.default_trigger = "syno_led6_ledtrig",
	},	{
		.name = "syno_led7",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch7,
		.num_channels = ARRAY_SIZE(ch7),
		.default_trigger = "syno_led7_ledtrig",
	},	{
		.name = "syno_led8",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch8,
		.num_channels = ARRAY_SIZE(ch8),
		.default_trigger = "syno_led8_ledtrig",
	},	{
		.name = "syno_led9",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch9,
		.num_channels = ARRAY_SIZE(ch9),
		.default_trigger = "syno_led9_ledtrig",
	},	{
		.name = "syno_led10",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch10,
		.num_channels = ARRAY_SIZE(ch10),
		.default_trigger = "syno_led10_ledtrig",
	},	{
		.name = "syno_led11",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch11,
		.num_channels = ARRAY_SIZE(ch11),
		.default_trigger = "syno_led11_ledtrig",
	},	{
		.name = "syno_led12",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch12,
		.num_channels = ARRAY_SIZE(ch12),
		.default_trigger = "syno_led12_ledtrig",
	},	{
		.name = "syno_led13",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch13,
		.num_channels = ARRAY_SIZE(ch13),
		.default_trigger = "syno_led13_ledtrig",
	},	{
		.name = "syno_led14",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch14,
		.num_channels = ARRAY_SIZE(ch14),
		.default_trigger = "syno_led14_ledtrig",
	},	{
		.name = "syno_led15",
		.mode = LP3943_LED_DIM0,
		.prescale = 30,
		.channel = ch15,
		.num_channels = ARRAY_SIZE(ch15),
		.default_trigger = "syno_led15_ledtrig",
	},
};

struct lp3943_platform_data syno_lp3943_pdata = {
	.node = syno_led_nodes,
	.num_nodes = ARRAY_SIZE(syno_led_nodes),
};

struct i2c_board_info __initdata LedI2CBoardInfo[] = {
	{
		I2C_BOARD_INFO("lp3943", 0x60),
		.platform_data = &syno_lp3943_pdata,
	},
};
#define SYNO_LP3943_MAX_RETRY 5
extern void (*funcSynoLP3943Mutex)(bool);
static struct mutex syno_lp3943_i2c_lock;
static void syno_lp3943_i2c_mutex (bool lock)
{
	if (lock)
	{
		mutex_lock(&syno_lp3943_i2c_lock);
	}
	else
	{
		mutex_unlock(&syno_lp3943_i2c_lock);
	}
}
#endif /* MY_ABC_HERE */

static int lp3943_read_byte(struct lp3943 *lp, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(lp->client, reg);
	if (ret < 0) {
		dev_err(lp->dev, "failed to read 0x%.2x\n", reg);
		return ret;
	}

	*data = (u8)ret;
	return 0;
}

static int lp3943_write_byte(struct lp3943 *lp, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(lp->client, reg, data);
}

static int lp3943_update_bits(struct lp3943 *lp, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

#ifdef MY_ABC_HERE
/*LS registers are modified in this function only, so this is the only part that we have to protect as a critical section*/
	mutex_lock(&ModeLock);
#endif /* MY_ABC_HERE */

	ret = lp3943_read_byte(lp, reg, &tmp);
	if (ret)
#ifdef MY_ABC_HERE
		goto END;
#else /* MY_ABC_HERE */
		return ret;
#endif /* MY_ABC_HERE */

	tmp &= ~mask;
	tmp |= data & mask;

#ifdef MY_ABC_HERE
	ret = lp3943_write_byte(lp, reg, tmp);

END:
	mutex_unlock(&ModeLock);
	return ret;
#else /* MY_ABC_HERE */
	return lp3943_write_byte(lp, reg, tmp);
#endif /* MY_ABC_HERE */
}

static int lp3943_update_selector(struct lp3943 *lp, enum lp3943_led_mode mode,
				enum lp3943_led_channel channel)
{
	u8 addr, mask, shift, idx;

	switch (channel) {
	case LP3943_LED0 ... LP3943_LED3:
		addr = LP3943_LS0;
		break;
	case LP3943_LED4 ... LP3943_LED7:
		addr = LP3943_LS1;
		break;
	case LP3943_LED8 ... LP3943_LED11:
		addr = LP3943_LS2;
		break;
	case LP3943_LED12 ... LP3943_LED15:
		addr = LP3943_LS3;
		break;
	default:
		return -EINVAL;
	}

	idx = channel % 4;
	mask = mask_sel[idx];
	shift = shift_sel[idx];

	return lp3943_update_bits(lp, addr, mask, mode << shift);
}

static int lp3943_update_scale(struct lp3943 *lp, enum lp3943_led_mode mode,
				u8 prescale)
{
	u8 addr;

	switch (mode) {
	case LP3943_LED_DIM0:
		addr = LP3943_PSC0;
		break;
	case LP3943_LED_DIM1:
		addr = LP3943_PSC1;
		break;
	default:
		return 0;
	}

	return lp3943_write_byte(lp, addr, prescale);
}

static int lp3943_update_pwm(struct lp3943 *lp, enum lp3943_led_mode mode,
				u8 pwm)
{
	u8 addr;

	switch (mode) {
	case LP3943_LED_DIM0:
		addr = LP3943_PWM0;
		break;
	case LP3943_LED_DIM1:
		addr = LP3943_PWM1;
		break;
	default:
		return 0;
	}

	return lp3943_write_byte(lp, addr, pwm);
}

#ifdef MY_ABC_HERE
static void lp3943_syno_brightness_set(u8 brightness, enum lp3943_led_mode *mode, enum lp3943_led_mode nodeMode)
{
	if (!mode) {
		goto END;
	}
	switch (brightness) {
		case 0:
			*mode = LP3943_LED_OFF;
			break;
		case 255:
			*mode = LP3943_LED_ON;
			break;
		default:
			*mode = nodeMode;
			break;
	}

END:
	return;
}
#endif /* MY_ABC_HERE */

static int lp3943_update_brightness(struct lp3943_led *led)
{
	struct lp3943 *lp = container_of(led, struct lp3943, led[led->id]);
	struct lp3943_led_node *node = led->node;
	enum lp3943_led_channel *channel;
	enum lp3943_led_mode mode;
	int i, ret;

	for (i = 0 ; i < node->num_channels ; i++) {
		channel = node->channel + i;

#ifdef MY_ABC_HERE
		lp3943_syno_brightness_set(led->brightness, &mode, node->mode);
#else /* MY_ABC_HERE */
		mode = led->brightness == 0 ? LP3943_LED_OFF : node->mode;
#endif /* MY_ABC_HERE */

		ret = lp3943_update_selector(lp, mode, *channel);
		if (ret)
			return ret;

#ifdef MY_ABC_HERE
		if (mode == LP3943_LED_OFF || mode == LP3943_LED_ON)
#else /* MY_ABC_HERE */
		if (mode == LP3943_LED_OFF)
#endif /* MY_ABC_HERE */
			continue;

		ret = lp3943_update_scale(lp, mode, node->prescale);
		if (ret)
			return ret;

		ret = lp3943_update_pwm(lp, mode, led->brightness);
		if (ret)
			return ret;
	}

	return 0;
}

static void lp3943_brightness_force_off(struct lp3943 *lp)
{
	int i;
	u8 addr[] = { LP3943_LS0, LP3943_LS1, LP3943_LS2, LP3943_LS3 };

	for (i = 0 ; i < ARRAY_SIZE(addr) ; i++)
		lp3943_write_byte(lp, addr[i], LED_OFF);
}

static void lp3943_brightness_work(struct work_struct *work)
{
#ifdef MY_ABC_HERE
	int ret = 0;
#endif /* MY_ABC_HERE */
	struct lp3943_led *led;

	led = container_of(work, struct lp3943_led, brtwork);
#ifdef MY_ABC_HERE
	syno_lp3943_i2c_mutex (true);
	ret = lp3943_update_brightness(led);
	syno_lp3943_i2c_mutex (false);

	if (ret) {
		if (led->retry_count < SYNO_LP3943_MAX_RETRY) {
			++led->retry_count;
			pr_err("%s: retry to recover %s for %d times\n",
				__func__, led->node->name, led->retry_count);
			schedule_work(&led->brtwork);
		} else {
			pr_err("%s: failed to recover %s\n",
				__func__, led->node->name);
		}
	} else {
		led->retry_count = 0;
	}
#else /* MY_ABC_HERE */
	lp3943_update_brightness(led);
#endif /* MY_ABC_HERE */
}

static void lp3943_brightness_set(struct led_classdev *led_cdev,
			     enum led_brightness brightness)
{
	struct lp3943_led *led;

	led = container_of(led_cdev, struct lp3943_led, cdev);
#ifdef MY_ABC_HERE
	if(brightness == led->brightness) {
		return;
	}
#endif /* MY_ABC_HERE */
	led->brightness = brightness;
	schedule_work(&led->brtwork);
}

static int lp3943_leds_register(struct lp3943 *lp,
				struct lp3943_platform_data *pdata)
{
	struct lp3943_led_node *node;
	int i, ret;

	for (i = 0 ; i < lp->num_leds ; i++) {
		node = pdata->node + i;

		if (!node || !node->name) {
			dev_err(lp->dev, "invalid data on node%d\n", i);
			ret = -EINVAL;
			goto err_dev;
		}
#ifdef MY_ABC_HERE
		INIT_WORK(&lp->led[i].brtwork, lp3943_brightness_work);
#endif /* MY_ABC_HERE */

		lp->led[i].id = i;
		lp->led[i].node = node;
		lp->led[i].cdev.name = node->name;
		lp->led[i].cdev.max_brightness = MAX_BRIGHTNESS;
		lp->led[i].cdev.brightness_set = lp3943_brightness_set;
#ifdef MY_ABC_HERE
		lp->led[i].cdev.default_trigger = node->default_trigger;
		lp->led[i].retry_count = 0;
#endif /* MY_ABC_HERE */

		ret = led_classdev_register(lp->dev, &lp->led[i].cdev);
		if (ret) {
			dev_err(lp->dev, "led(%d/%d) register err: %d\n",
					i, lp->num_leds, ret);
			goto err_dev;
		}

#ifdef MY_ABC_HERE
		/* Move to above */
#else /* MY_ABC_HERE */
		INIT_WORK(&lp->led[i].brtwork, lp3943_brightness_work);
#endif /* MY_ABC_HERE */
	}

	return 0;

err_dev:
	while (--i >= 0) {
		led_classdev_unregister(&lp->led[i].cdev);
		cancel_work_sync(&lp->led[i].brtwork);
	}
	return ret;
}

static void lp3943_leds_unregister(struct lp3943 *lp)
{
	int i;

	for (i = 0 ; i < lp->num_leds ; i++) {
		led_classdev_unregister(&lp->led[i].cdev);
		cancel_work_sync(&lp->led[i].brtwork);
	}
}

static int lp3943_validate_platform_data(struct device *dev,
				struct lp3943_platform_data *pdata)
{
	if (!pdata || !pdata->node) {
		dev_err(dev, "invalid platform data\n");
		goto err;
	}

	if (pdata->num_nodes == 0 || pdata->num_nodes > MAX_NUM_LEDS) {
		dev_err(dev, "invalid num_nodes: %d\n", pdata->num_nodes);
		goto err;
	}

	return 0;
err:
	return -EINVAL;
}

static int lp3943_chip_detect(struct lp3943 *lp)
{
	u8 val;
	return lp3943_read_byte(lp, LP3943_INPUT1, &val);
}

#ifdef CONFIG_SYNO_LEDS_LP3943_PROBE_ACPI
static const struct acpi_device_id lp3943_acpi_ids[] = {
	{ "LED3943", (kernel_ulong_t)&syno_lp3943_pdata },
	{ }
};
MODULE_DEVICE_TABLE(acpi, lp3943_acpi_ids);
#endif /* CONFIG_SYNO_LEDS_LP3943_PROBE_ACPI */

static int lp3943_probe(struct i2c_client *cl,
				const struct i2c_device_id *id)
{
	struct lp3943 *lp;
	struct lp3943_platform_data *pdata = cl->dev.platform_data;
	int ret;
#ifdef CONFIG_SYNO_LEDS_LP3943_PROBE_ACPI
	const struct acpi_device_id *aid;

	aid = acpi_match_device(lp3943_acpi_ids, &cl->dev);
	if (aid) {
		pdata = (struct lp3943_platform_data *) aid->driver_data;
	} else {
		return -ENODEV;
	}
#endif /* CONFIG_SYNO_LEDS_LP3943_PROBE_ACPI */

	if (!i2c_check_functionality(cl->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EIO;

#ifdef CONFIG_SYNO_LEDS_LP3943_PROBE_OF
	pdata = &syno_lp3943_pdata;
#endif /* CONFIG_SYNO_LEDS_LP3943_PROBE_OF */

	ret = lp3943_validate_platform_data(&cl->dev, pdata);
	if (ret)
		return ret;

	lp = devm_kzalloc(&cl->dev, sizeof(struct lp3943), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	lp->client = cl;
	lp->dev = &cl->dev;
	lp->num_leds = pdata->num_nodes;
	i2c_set_clientdata(cl, lp);

	ret = lp3943_chip_detect(lp);
	if (ret) {
		dev_err(lp->dev, "chip detection err: %d\n", ret);
		return ret;
	}

#ifdef MY_ABC_HERE
	mutex_init(&syno_lp3943_i2c_lock);
	funcSynoLP3943Mutex = syno_lp3943_i2c_mutex;
#endif /* MY_ABC_HERE */
	return lp3943_leds_register(lp, pdata);
}

static int lp3943_remove(struct i2c_client *cl)
{
	struct lp3943 *lp = i2c_get_clientdata(cl);

	lp3943_brightness_force_off(lp);
	lp3943_leds_unregister(lp);
	return 0;
}

static const struct i2c_device_id lp3943_id[] = {
	{"lp3943", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, lp3943_id);

static struct i2c_driver lp3943_driver = {
	.probe = lp3943_probe,
	.remove = lp3943_remove,
	.driver = {
		.name = "lp3943",
		.owner = THIS_MODULE,
#ifdef CONFIG_SYNO_LEDS_LP3943_PROBE_ACPI
		.acpi_match_table = ACPI_PTR(lp3943_acpi_ids),
#endif /* CONFIG_SYNO_LEDS_LP3943_PROBE_ACPI */
	},
	.id_table = lp3943_id,
};

static int __init lp3943_init(void)
{
#ifdef MY_ABC_HERE
	int iErr = -1;
	struct i2c_adapter *pAdapter = NULL;
	/* instantiate the devices explicitly */
	pAdapter = i2c_get_adapter(0);
	if (pAdapter == NULL) {
		printk(KERN_ERR "led-lp3943 initial error: failed to get i2c adapter\n");
		goto END;
	}

	i2c_put_adapter(pAdapter);

	/*regist board info*/
	gpClient = i2c_new_client_device(pAdapter, &LedI2CBoardInfo[0]);
	if (gpClient == NULL) {
		printk(KERN_ERR "led-lp3943 initial error: failed to initial device\n");
		goto END;
	}
	iErr = i2c_add_driver(&lp3943_driver);

END:
	return iErr;
#else /* MY_ABC_HERE */
	return i2c_add_driver(&lp3943_driver);
#endif /* MY_ABC_HERE */
}
module_init(lp3943_init);

static void __exit lp3943_exit(void)
{
#ifdef MY_ABC_HERE
	i2c_unregister_device(gpClient);
#endif /* MY_ABC_HERE */
	i2c_del_driver(&lp3943_driver);
}
module_exit(lp3943_exit);

MODULE_DESCRIPTION("National Semiconductor/TI LP3943 LED Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
