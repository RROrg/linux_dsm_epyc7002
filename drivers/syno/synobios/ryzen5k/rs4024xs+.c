// Copyright (c) 2000-2021 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "ryzen5k_common.h"

#define I2C_SWITCH_VAL 0x9

#define PSU_I2C_BUS 		0
#define PSU_TOP_I2C_ADDR 	0x58
#define PSU_BTM_I2C_ADDR 	0x59

// extern function from ryzen5k_common
extern int I2CSmbusReadPowerStatus(int i2c_bus_no, u16 i2c_addr, SYNO_POWER_STATUS* status);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

void RS4024xspSMBusSwitchInit(void) {
	u8 data = I2C_SWITCH_VAL;
	SMBusSwitchRegWrite(0, 1, &data);
}

SYNO_HWMON_SENSOR_TYPE RS4024xsp_thermal_sensor = {
	.type_name = HWMON_SYS_THERMAL_NAME,
	.sensor_num = 3,
	.sensor[0] = {
		.sensor_name = "Remote1",
	},
	.sensor[1] = {
		.sensor_name = "Local",
	},
	.sensor[2] = {
		.sensor_name = "Remote2",
	},
};

SYNO_HWMON_SENSOR_TYPE RS4024xsp_voltage_sensor = {
	.type_name = HWMON_SYS_VOLTAGE_NAME,
	.sensor_num = 5,
	.sensor[0] = {
		.sensor_name = "VCC",
	},
	.sensor[1] = {
		.sensor_name = "VPP",
	},
	.sensor[2] = {
		.sensor_name = "V33",
	},
	.sensor[3] = {
		.sensor_name = "V5",
	},
	.sensor[4] = {
		.sensor_name = "V12",
	},
};

SYNO_HWMON_SENSOR_TYPE RS4024xsp_fan_speed_rpm = {
	.type_name = HWMON_SYS_FAN_RPM_NAME,
	.sensor_num = 4,
	.sensor[0] = {
		.sensor_name = HWMON_SYS_FAN1_RPM,
	},
	.sensor[1] = {
		.sensor_name = HWMON_SYS_FAN2_RPM,
	},
	.sensor[2] = {
		.sensor_name = HWMON_SYS_FAN3_RPM,
	},
	.sensor[3] = {
		.sensor_name = HWMON_SYS_FAN4_RPM,
	},
};

SYNO_HWMON_SENSOR_TYPE RS4024xsp_psu_status[2] = {
	{
		.type_name = HWMON_PSU1_STATUS_NAME,
		.sensor_num = 5,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
	{
		.type_name = HWMON_PSU2_STATUS_NAME,
		.sensor_num = 5,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
};

SYNO_HWMON_SENSOR_TYPE RS4024xsp_hdd_backplane_status = {
	.type_name = HWMON_HDD_BP_STATUS_NAME,
	.sensor_num = 2,
	.sensor[0] = {
		.sensor_name = HWMON_HDD_BP_DETECT,
	},
	.sensor[1] = {
		.sensor_name = HWMON_HDD_BP_ENABLE,
	},
};

static
int RS4024xspI2CGetPowerInfo(POWER_INFO *power_info)
{
	int ret = -1;
	int err = -1;

	if (NULL == power_info) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_TOP_I2C_ADDR, &(power_info->power_1));
	if (0 != err) {
		goto FAIL;
	}

	err = I2CSmbusReadPowerStatus(PSU_I2C_BUS, PSU_BTM_I2C_ADDR, &(power_info->power_2));
	if (0 != err) {
		goto FAIL;
	}

        ret = 0;

FAIL:
	return ret;
}

static SYNO_GPIO_INFO alarm_led = {
	.nr_gpio                = 1,
	.gpio_port              = {57},
	.gpio_polarity  = ACTIVE_HIGH,
};

static
void RS4024xspGpioInit(void)
{
	syno_gpio.alarm_led			= &alarm_led;
}

static
void RS4024xspGpioCleanup(void)
{
	syno_gpio.alarm_led			= NULL;
}

static
int RS4024xspInitModuleType(struct synobios_ops *ops)
{
	module_t type_rs4024xsp = MODULE_T_RS4024xsp;
	module_t *pType = &type_rs4024xsp;
	GPIO_PIN Pin;

	/* If user put "buzzer off" of redundant power then poweron,
	 * It may cause gpio RYZEN5K_BUZZER_CTRL_PIN set to low, it will casue unwanted buzzer off event*/
	if (ops && ops->set_gpio_pin) {
		Pin.pin = RYZEN5K_BUZZER_CTRL_PIN;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}

	module_type_set(pType);
	return 0;
}

struct model_ops rs4024xsp_ops = {
	.x86_init_module_type = RS4024xspInitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status = RS4024xspI2CGetPowerInfo,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
	.x86_gpio_init = RS4024xspGpioInit,
	.x86_gpio_cleanup = RS4024xspGpioCleanup,
};

struct hwmon_sensor_list rs4024xsp_sensor_list = {
	.thermal_sensor = &RS4024xsp_thermal_sensor,
	.voltage_sensor = &RS4024xsp_voltage_sensor,
	.fan_speed_rpm = &RS4024xsp_fan_speed_rpm,
	.psu_status = RS4024xsp_psu_status,
	.hdd_backplane = &RS4024xsp_hdd_backplane_status,
};
