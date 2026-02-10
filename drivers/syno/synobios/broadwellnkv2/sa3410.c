// Copyright (c) 2000-2022 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/synobios.h>
#include "broadwellnkv2_common.h"
#ifdef CONFIG_SYNO_HWMON_PMBUS
//#include <linux/syno_fdt.h>
extern int redundantPowerGetPowerStatusByI2C(POWER_INFO *power_info);
#endif /* CONFIG_SYNO_HWMON_PMBUS */

// extern function from broadwellnkv2_common
extern int xsSetBuzzerClear(unsigned char buzzer_cleared);
extern int xsGetBuzzerCleared(unsigned char *buzzer_cleared);
extern int xsCPUFanSpeedMapping(FAN_SPEED speed);
extern int xsFanSpeedMapping(FAN_SPEED speed);


/* FIXME: should not directly copy following function
 * Modified from drivers/i2c/muxes/i2c-mux-pca954x.c */
int SA3410SMBusSwitchRegWrite(int bus_no, u16 addr, u8 val)
{
        int ret = -1;
        struct i2c_adapter *adap = NULL;

        adap = i2c_get_adapter(bus_no);
        if (!adap) {
                printk("Cannot get i2c adapter!\n");
                goto END;
        }

        if (adap->algo->master_xfer) {
                struct i2c_msg msg;
                char buf[1];

                msg.addr = addr;
                msg.flags = 0;
                msg.len = 1;
                buf[0] = val;
                msg.buf = buf;
                ret = __i2c_transfer(adap, &msg, 1);
        } else {
                union i2c_smbus_data data;
                ret = adap->algo->smbus_xfer(adap, addr,
                                0, I2C_SMBUS_WRITE,
                                val, I2C_SMBUS_BYTE, &data);
        }
END:
        return ret;
}

void SA3410SMBusSwitchInit(void) {

    //
    // PCIe SMBus switch 0x70 write 0x4, to open the route to PCIe Slot#3)
    // 1. SMBus topology : PCIe Slot#3 ---> SAS HBA card ---> HDD BP ---> PIC (slave addr 0x47)
    // 2. PIC (slave addr 0x47) to control disk power-on delay (default no delay) and to read hdd present
    //
	SA3410SMBusSwitchRegWrite(0, 0x70, 0x4);
}

SYNO_HWMON_SENSOR_TYPE SA3410_thermal_sensor = {
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

SYNO_HWMON_SENSOR_TYPE SA3410_voltage_sensor = {
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

SYNO_HWMON_SENSOR_TYPE SA3410_fan_speed_rpm = {
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

SYNO_HWMON_SENSOR_TYPE SA3410_psu_status[2] = {
	{
		.type_name = HWMON_PSU1_STATUS_NAME,
		.sensor_num = 7,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP2,
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP3,
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[6] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
	{
		.type_name = HWMON_PSU2_STATUS_NAME,
		.sensor_num = 7,
		.sensor[0] = {
			.sensor_name = HWMON_PSU_SENSOR_PIN,
		},
		.sensor[1] = {
			.sensor_name = HWMON_PSU_SENSOR_POUT,
		},
		.sensor[2] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP1,
		},
		.sensor[3] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP2,
		},
		.sensor[4] = {
			.sensor_name = HWMON_PSU_SENSOR_TEMP3,
		},
		.sensor[5] = {
			.sensor_name = HWMON_PSU_SENSOR_FAN,
		},
		.sensor[6] = {
			.sensor_name = HWMON_PSU_SENSOR_STATUS,
		},
	},
};

SYNO_HWMON_SENSOR_TYPE SA3410_hdd_backplane_status = {
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
int SA3410InitModuleType(struct synobios_ops *ops)
{
	module_t type_sa3410 = MODULE_T_SA3410;
	module_t *pType = &type_sa3410;
	GPIO_PIN Pin;

	/* If user put "buzzer off" of redundant power then poweron,
	 * It may cause gpio BROADWELLNKV2_BUZZER_CTRL_PIN set to low, it will casue unwanted buzzer off event*/
	if (ops && ops->set_gpio_pin) {
		Pin.pin = BROADWELLNKV2_BUZZER_CTRL_PIN;
		Pin.value = 1;
		ops->set_gpio_pin(&Pin);
	}

	module_type_set(pType);
	return 0;
}

struct model_ops sa3410_ops = {
	.x86_init_module_type = SA3410InitModuleType,
	.x86_fan_speed_mapping = xsFanSpeedMapping,
	.x86_set_esata_led_status = NULL,
	.x86_cpufan_speed_mapping = xsCPUFanSpeedMapping,
	.x86_get_buzzer_cleared = xsGetBuzzerCleared,
	.x86_get_power_status    = redundantPowerGetPowerStatusByI2C,
	.x86_set_buzzer_clear = xsSetBuzzerClear,
};

struct hwmon_sensor_list sa3410_sensor_list = {
        .thermal_sensor = &SA3410_thermal_sensor,
        .voltage_sensor = &SA3410_voltage_sensor,
        .fan_speed_rpm = &SA3410_fan_speed_rpm,
        .psu_status = SA3410_psu_status,
        .hdd_backplane = &SA3410_hdd_backplane_status,
};
