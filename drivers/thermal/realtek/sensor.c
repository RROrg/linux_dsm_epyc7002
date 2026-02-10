#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0-only
/*
 * sensor.c - Realtek generic thermal sensor driver
 *
 * Copyright (C) 2017-2018,2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include "../thermal_core.h"
#include "sensor.h"

#if defined(MY_ABC_HERE)
static struct thermal_sensor_device* g_syno_tdev = NULL;
#endif /* MY_ABC_HERE */

static inline int thermal_sensor_hw_init(struct thermal_sensor_device *tdev)
{
	const struct thermal_sensor_desc *desc = tdev->desc;
	int ret;

	if (!desc->hw_ops->init)
		return 0;

	ret = desc->hw_ops->init(tdev);
	tdev->last_reset_time = ktime_get();
	return ret;
}

static inline void thermal_sensor_hw_exit(struct thermal_sensor_device *tdev)
{
	const struct thermal_sensor_desc *desc = tdev->desc;

	if (!desc->hw_ops->exit)
		return;

	desc->hw_ops->exit(tdev);
}

static inline void thermal_sensor_hw_reset(struct thermal_sensor_device *tdev)
{
	const struct thermal_sensor_desc *desc = tdev->desc;

	if (!desc->hw_ops->reset)
		return;

	desc->hw_ops->reset(tdev);
	tdev->last_reset_time = ktime_get();
}

static inline int thermal_sensor_hw_get_temp(struct thermal_sensor_device *tdev,
					     int *temp)
{
	const struct thermal_sensor_desc *desc = tdev->desc;

	if (!desc->hw_ops->get_temp)
		return -EINVAL;

	if (desc->reset_time_ms) {
		s64 t;

		t = ktime_ms_delta(ktime_add_ms(tdev->last_reset_time, desc->reset_time_ms),
				ktime_get());
		if (t > 0) {
			dev_info(tdev->dev, "wait %lldms to be ready\n", t);
			msleep(t);
		}
	}

	return desc->hw_ops->get_temp(tdev, temp);
}

static inline
void thermal_sensor_hw_dump_status(struct thermal_sensor_device *tdev)
{
	const struct thermal_sensor_desc *desc = tdev->desc;
	unsigned int val;
	int ret;
	int i;

	if (!desc->status_cnt)
		return;

	for (i = 0; i < desc->status_cnt; i++) {
		ret = thermal_sensor_device_reg_read(tdev, desc->status_ofs + i * 4, &val);
		dev_warn(tdev->dev, "status%d, ofs=%04x, val=%08x, ret=%d\n",
			 i, desc->status_ofs + i * 4, val, ret);
	}
}

static const struct thermal_zone_of_device_ops thermal_sensor_ops;
static const struct of_device_id thermal_sensor_of_match[];

static int thermal_sensor_device_add(struct device *dev,
				     struct thermal_sensor_device *tdev,
				     const struct thermal_sensor_desc *desc)
{
	int ret;

	tdev->dev = dev;
	tdev->desc = desc;

	ret = thermal_sensor_hw_init(tdev);
	if (ret)
		return ret;

	tdev->tz = thermal_zone_of_sensor_register(dev, 0, tdev,
						   &thermal_sensor_ops);
	if (IS_ERR(tdev->tz)) {
		thermal_sensor_hw_exit(tdev);
		return PTR_ERR(tdev->tz);
	}
#if defined(MY_ABC_HERE)
	g_syno_tdev = tdev;
#endif /* MY_ABC_HERE */

	return 0;
}

static void thermal_sensor_device_remove(struct thermal_sensor_device *tdev)
{
	thermal_zone_of_sensor_unregister(tdev->dev, tdev->tz);
	thermal_sensor_hw_exit(tdev);
}

static int thermal_sensor_get_temp(void *data, int *temp)
{
	struct thermal_sensor_device *tdev = data;
	int ret;

	ret = thermal_sensor_hw_get_temp(tdev, temp);

	if (ret || !is_vaild_temp(*temp)) {
		dev_warn(tdev->dev, "invalid temp=%d\n", *temp);
		thermal_sensor_hw_dump_status(tdev);

		thermal_sensor_hw_reset(tdev);

		ret = thermal_sensor_hw_get_temp(tdev, temp);
	}

	dev_dbg(tdev->dev, "temp=%d\n", *temp);
	return ret;
}

#if defined(MY_ABC_HERE)
int syno_rtd_get_temperature(void)
{
	int ret = -1;
	int temp = 0;

	if (NULL == g_syno_tdev) {
		printk(KERN_ERR "syno: thermal device not found\n");
		goto out;
	}

	// rtk thermal will return degree * 1000, we need to change unit
	thermal_sensor_hw_get_temp(g_syno_tdev, &temp);
	ret = temp/1000;

out:
	return ret;
}
EXPORT_SYMBOL(syno_rtd_get_temperature);
#endif /* MY_ABC_HERE */

static int thermal_sensor_get_trend(void *data, int i,
				    enum thermal_trend *trend)
{
	struct thermal_sensor_device *tdev = data;
	struct thermal_zone_device *tz = tdev->tz;
	const struct thermal_trip *trip;
	int delta;

	/* .get_trend will be called at thermal_zone_register. */
	if (!tz || !of_thermal_is_trip_valid(tz, i))
		return -EINVAL;

	trip = of_thermal_get_trip_points(tz) + i;
	delta = tz->temperature - trip->temperature;

	if (-500 < delta && delta < 500)
		*trend = THERMAL_TREND_STABLE;
	else if (delta > 5000)
		*trend = THERMAL_TREND_RAISE_FULL;
	else if (delta > 0)
		*trend = THERMAL_TREND_RAISING;
	else
		*trend = THERMAL_TREND_DROPPING;

	dev_dbg(tdev->dev, "trip%d, delta=%d, trend=%d\n", i,
		delta, *trend);

	return 0;
}

static const struct thermal_zone_of_device_ops thermal_sensor_ops = {
	.get_temp  = thermal_sensor_get_temp,
	.get_trend = thermal_sensor_get_trend,
};

static int thermal_sensor_resume(struct device *dev)
{
	struct thermal_sensor_device *tdev = dev_get_drvdata(dev);

	dev_info(dev, "enter %s\n", __func__);
	thermal_sensor_hw_reset(tdev);
	dev_info(dev, "exit %s\n", __func__);
	return 0;
}

static const struct dev_pm_ops thermal_sensor_pm_ops = {
	.resume = thermal_sensor_resume,
};

static int thermal_sensor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct thermal_sensor_device *tdev;
	int ret = 0;
	struct resource res;
	const struct thermal_sensor_desc *desc;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	tdev = devm_kzalloc(dev, sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;

	tdev->base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!tdev->base)
		return -ENOMEM;

	if (of_find_property(np, "realtek,scpu-wrapper", NULL)) {
		tdev->regmap = syscon_regmap_lookup_by_phandle(np,
			"realtek,scpu-wrapper");
		if (IS_ERR(tdev->regmap))
			return PTR_ERR(tdev->regmap);
	}

	ret = thermal_sensor_device_add(dev, tdev, desc);
	if (ret)
		dev_err(dev, "failed to add thermal sensor: %d\n", ret);
	platform_set_drvdata(pdev, tdev);
	return 0;
}

static int thermal_sensor_remove(struct platform_device *pdev)
{
	struct thermal_sensor_device *tdev = platform_get_drvdata(pdev);

	thermal_sensor_device_remove(tdev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id thermal_sensor_of_match[] = {
	{
		.compatible = "realtek,rtd119x-thermal-sensor",
		.data = &rtd119x_sensor_desc,
	},
	{
		.compatible = "realtek,rtd129x-thermal-sensor",
		.data = &rtd129x_sensor_desc,
	},
	{
		.compatible = "realtek,rtd139x-thermal-sensor",
		.data = &rtd139x_sensor_desc,
	},
	{
		.compatible = "realtek,rtd161x-thermal-sensor",
		.data = &rtd1619_sensor_desc,
	},
	{
		.compatible = "realtek,rtd131x-thermal-sensor",
		.data = &rtd1319_sensor_desc,
	},
	{
		.compatible = "realtek,rtd1619b-thermal-sensor",
		.data = &rtd1619b_sensor_desc,
	},
	{ .compatible = "realtek,rtd1312c-thermal-sensor", .data = &rtd1312c_sensor_desc, },
	{}
};
MODULE_DEVICE_TABLE(of, thermal_sensor_of_match);

static struct platform_driver thermal_sensor_drv = {
	.driver = {
		.name           = "rtk-thermal-sensor",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(thermal_sensor_of_match),
		.pm             = &thermal_sensor_pm_ops,
	},
	.probe    = thermal_sensor_probe,
	.remove   = thermal_sensor_remove,
};
module_platform_driver(thermal_sensor_drv);

MODULE_DESCRIPTION("Realtek Thermal Sensor Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
