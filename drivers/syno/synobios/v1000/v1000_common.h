// Copyright (c) 2000-2018 Synology Inc. All rights reserved.

#include "synobios.h"
#include "../mapping.h"
#ifdef CONFIG_SYNO_LEDS_TRIGGER
#include "../led/led_trigger.h"
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
#include "../common/common.h"
#include "../led/led_jmb585.h"
#include "../led/led_asm116x.h"
#include "../led/led_1475.h"
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
#include <linux/synolib.h>
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

int GetModel(void);
int GetGpioPin(GPIO_PIN *pPin);
int SetGpioPin(GPIO_PIN *pPin);

extern int SetUart(const char* cmd);
extern int ReadUart(const char *szGoCmd, const char *szStopCmd, char *szResult, size_t leng);
extern struct model_ops ds1621p_ops;
extern struct model_ops rs1221p_ops;
extern struct model_ops rs1221rpp_ops;
extern struct model_ops ds1821p_ops;
extern struct model_ops ds2422p_ops;
extern struct model_ops rs2421p_ops;
extern struct model_ops rs2421rpp_ops;
extern struct model_ops rs2821rpp_ops;
extern struct model_ops fs2500_ops;
extern struct model_ops fs2500t_ops;
extern struct model_ops rs822p_ops;
extern struct model_ops rs822rpp_ops;
extern struct model_ops rs2423p_ops;
extern struct model_ops rs2423rpp_ops;
extern struct model_ops ds1823xsp_ops;
extern struct model_ops rs1623xsp_ops;
extern struct model_ops ds1623p_ops;
extern struct model_ops ds1823p_ops;
extern struct model_ops sc2500_ops;

#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
extern long g_smbus_hdd_powerctl;
extern int gSynoSmbusHddAdapter;
extern int gSynoSmbusHddAddress;
extern SYNO_SMBUS_HDD_POWERCTL SynoSmbusHddPowerCtl;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

extern int syno_get_adt_fan_speed_rpm(SYNO_HWMON_SENSOR_TYPE *);
extern int syno_get_adt_thermal_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern int syno_get_adt_voltage_sensor(SYNO_HWMON_SENSOR_TYPE*);
extern int syno_get_adt_fan_speed_rpm_by_order(SYNO_HWMON_SENSOR_TYPE *, SYNO_HWMON_FAN_ORDER *);
extern struct hwmon_sensor_list ds1621p_sensor_list;
extern struct hwmon_sensor_list rs1221p_sensor_list;
extern struct hwmon_sensor_list rs1221rpp_sensor_list;
extern struct hwmon_sensor_list ds1821p_sensor_list;
extern struct hwmon_sensor_list ds2422p_sensor_list;
extern struct hwmon_sensor_list rs2421p_sensor_list;
extern struct hwmon_sensor_list rs2421rpp_sensor_list;
extern struct hwmon_sensor_list rs2821rpp_sensor_list;
extern struct hwmon_sensor_list fs2500_sensor_list;
extern struct hwmon_sensor_list fs2500t_sensor_list;
extern struct hwmon_sensor_list rs822p_sensor_list;
extern struct hwmon_sensor_list rs822rpp_sensor_list;
extern struct hwmon_sensor_list rs2423p_sensor_list;
extern struct hwmon_sensor_list rs2423rpp_sensor_list;
extern struct hwmon_sensor_list ds1823xsp_sensor_list;
extern struct hwmon_sensor_list rs1623xsp_sensor_list;
extern struct hwmon_sensor_list ds1623p_sensor_list;
extern struct hwmon_sensor_list ds1823p_sensor_list;
extern struct hwmon_sensor_list sc2500_sensor_list;

extern SYNO_HWMON_FAN_ORDER rs822rpp_fan_order_list;

#ifdef CONFIG_SYNO_X86_PINCTRL_GPIO
#include <linux/gpio.h>
#endif /* CONFIG_SYNO_X86_PINCTRL_GPIO */
#if defined(CONFIG_SYNO_K10TEMP) || defined(CONFIG_SYNO_HWMON_AMD_K10TEMP)
extern int syno_k10cpu_temperature(struct _SynoCpuTemp *pCpuTemp);
#endif /* CONFIG_SYNO_K10TEMP || CONFIG_SYNO_HWMON_AMD_K10TEMP */

#define SZ_UART_CMD_PREFIX         "-"
#define SZ_UART_ALARM_LED_ON       "LA1"
#define SZ_UART_ALARM_LED_BLINKING "LA2"
#define SZ_UART_ALARM_LED_OFF      "LA3"
#define SZ_UART_FAN_DUTY_CYCLE     "V"
#define SZ_UART_FAN_FREQUENCY      "W"
#define SZ_UART_CPUFAN_DUTY_CYCLE  "X"
#define SZ_UART_CPUFAN_FREQUENCY   "Y"
#define SZ_UART_PWR_LED_ON         "4"
#define SZ_UART_PWR_LED_OFF        "6"

struct model_ops {
	int	(*x86_init_module_type)(struct synobios_ops *ops);
	int	(*x86_fan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_set_esata_led_status)(SYNO_DISK_LED status);
	int	(*x86_cpufan_speed_mapping)(FAN_SPEED speed);
	int	(*x86_get_buzzer_cleared)(unsigned char *buzzer_cleared);
	int	(*x86_get_power_status)(POWER_INFO *power_info);
	int	(*x86_get_fan_status)(int fanno, FAN_STATUS *pStatus);
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

