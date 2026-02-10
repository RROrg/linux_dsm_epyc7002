// Copyright (c) 2000-2018 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include "purley_common.h"

#define PSU_I2C_BUS 		0
#define PSU_TOP_I2C_ADDR 	0x58
#define PSU_BTM_I2C_ADDR 	0x59

// extern function from purley_common
extern int I2CSmbusReadPowerStatus(int i2c_bus_no, u16 i2c_addr, SYNO_POWER_STATUS* status);
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);

static
int FS6400I2CGetPowerInfo(POWER_INFO *power_info)
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

SYNO_HWMON_SENSOR_TYPE FS6400_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE FS6400_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE FS6400_fan_speed_rpm = {
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

int FS6400InitModuleType(struct synobios_ops *ops)
{
    module_t type_fs6400 = MODULE_T_FS6400;
    module_t *pType = &type_fs6400;

    module_type_set(pType);
    return 0;
}

struct model_ops fs6400_ops = {
    .x86_init_module_type = FS6400InitModuleType,
    .x86_fan_speed_mapping = xsFanSpeedMapping,
    .x86_set_esata_led_status = NULL,
    .x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
    .x86_get_buzzer_cleared = xsGetBuzzerCleared,
    .x86_get_power_status    = FS6400I2CGetPowerInfo,
    .x86_set_buzzer_clear = xsSetBuzzerClear,
};

struct hwmon_sensor_list fs6400_sensor_list = {
        .thermal_sensor = &FS6400_thermal_sensor,
        .voltage_sensor = &FS6400_voltage_sensor,
        .fan_speed_rpm = &FS6400_fan_speed_rpm,
        .psu_status = NULL,
        .hdd_backplane = NULL,
};
