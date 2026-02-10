#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2021 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#include "../led/led_9235.h"
#include "../led/led_trigger.h"
#include "../led/led_1475.h"
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
#include <linux/synolib.h>
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

#define I2C_BUS_NO 0
extern void RS4024xspSMBusSwitchInit(void);
extern void SMBusSwitchRegWrite(u8 command, u8 length, u8 *data);

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern char gszSynoHWVersion[];
extern struct model_ops rs4024xsp_ops;

#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
extern long g_smbus_hdd_powerctl;
extern int gSynoSmbusHddAdapter;
extern int gSynoSmbusHddAddress;
extern SYNO_SMBUS_HDD_POWERCTL SynoSmbusHddPowerCtl;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

extern int syno_get_adt_fan_speed_rpm(SYNO_HWMON_SENSOR_TYPE *);
extern int syno_get_pmbus_status(SYNO_HWMON_SENSOR_TYPE* , int);
extern int syno_get_adt_thermal_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern int syno_get_adt_voltage_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern struct hwmon_sensor_list rs4024xsp_sensor_list;

#ifdef CONFIG_SYNO_X86_CORETEMP
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */
#ifdef MY_DEF_HERE
extern int syno_sys_temperature(struct _SynoThermalTemp *pThermalTemp);
#endif /*MY_DEF_HERE*/

#define RYZEN5K_BUZZER_OFF_PIN    53
#define RYZEN5K_BUZZER_CTRL_PIN   52
#define RYZEN5K_DISK_LED_ACTIVATE_PIN 45

#define SZ_UART_CMD_PREFIX         "-"
#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"
#define SZ_UART_PWR_LED_ON         "4"
#define SZ_UART_PWR_LED_OFF        "6"

#define PSU_DELTA_AC139_I2C_REG 		0x79
#define PSU_DELTA_AC139_I2C_REG_ABNORMAL_STATUS_BIT 	0x0800

struct model_ops {
	int	(*x86_init_module_type)(struct synobios_ops *ops);
	int	(*x86_fan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_set_esata_led_status)(SYNO_DISK_LED status);
	int	(*x86_cpufan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_get_buzzer_cleared)(unsigned char *buzzer_cleared);
	int	(*x86_get_power_status)(POWER_INFO *power_info);
	int	(*x86_set_buzzer_clear)(unsigned char buzzer_cleared);
	void	(*x86_gpio_init)(void);
	void	(*x86_gpio_cleanup)(void);
};

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};

