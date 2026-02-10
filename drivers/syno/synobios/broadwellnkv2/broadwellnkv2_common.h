#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2017 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#include "../led/led_9235.h"
#include "../led/led_trigger.h"
#include "../led/led_1475.h"
#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
#include <linux/synolib.h>
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern char gszSynoHWVersion[];
extern struct model_ops rs4022xsp_ops;
extern struct model_ops fs3410_ops;
extern struct model_ops sa3410_ops;
extern struct model_ops sa3610_ops;

extern struct hwmon_sensor_list fs3410_sensor_list;
extern struct hwmon_sensor_list sa3410_sensor_list;
extern struct hwmon_sensor_list sa3610_sensor_list;

#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
extern long g_smbus_hdd_powerctl;
extern int gSynoSmbusHddAdapter;
extern int gSynoSmbusHddAddress;
extern SYNO_SMBUS_HDD_POWERCTL SynoSmbusHddPowerCtl;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */

extern int syno_get_adt_fan_speed_rpm(SYNO_HWMON_SENSOR_TYPE *);
extern int syno_get_pmbus_status(SYNO_HWMON_SENSOR_TYPE* , int);
extern int syno_get_adt_thermal_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern int syno_get_adt_voltage_sensor(SYNO_HWMON_SENSOR_TYPE*);

extern u32 syno_pch_lpc_gpio_pin(int pin, int *pValue, int isWrite);
#ifdef CONFIG_SYNO_X86_CORETEMP
extern int syno_cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */
#ifdef MY_DEF_HERE
extern int syno_sys_temperature(struct _SynoThermalTemp *pThermalTemp);
#endif /*MY_DEF_HERE*/

extern void FS3410SMBusSwitchInit(void);
extern void SA3410SMBusSwitchInit(void);
extern void SA3610SMBusSwitchInit(void);

#define BROADWELLNKV2_POWER1_PIN        50
#define BROADWELLNKV2_POWER2_PIN        54
#define BROADWELLNKV2_BUZZER_OFF_PIN    2
#define BROADWELLNKV2_BUZZER_CTRL_PIN   3
#define BROADWELLNKV2_ALARM_LED_PIN     28
#define BROADWELLNKV2_DISK_LED_ACTIVATE_PIN 45

#define SZ_UART_CMD_PREFIX         "-"
#define SZ_UART_ALARM_LED_ON       "LA1"
#define SZ_UART_ALARM_LED_BLINKING "LA2"
#define SZ_UART_ALARM_LED_OFF      "LA3"
#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"

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

};

struct hwmon_sensor_list {
	SYNO_HWMON_SENSOR_TYPE *thermal_sensor;
	SYNO_HWMON_SENSOR_TYPE *voltage_sensor;
	SYNO_HWMON_SENSOR_TYPE *fan_speed_rpm;
	SYNO_HWMON_SENSOR_TYPE *psu_status;
	SYNO_HWMON_SENSOR_TYPE *hdd_backplane;
};

