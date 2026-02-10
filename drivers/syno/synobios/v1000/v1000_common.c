#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2019 Synology Inc. All rights reserved.

#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include <linux/fs.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_SYNO_HWMON_PMBUS
#include <linux/syno_fdt.h>
#endif /* CONFIG_SYNO_HWMON_PMBUS */
#include "../rtc/rtc.h"
#include "../i2c/i2c-linux.h"
#include "../pmbus/pmbus.h"

#include "v1000_common.h"

#define GPIO_POWER_GOOD	1

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL */

static int Uninitialize(void);
static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
static SYNO_HWMON_FAN_ORDER *hwmon_fan_order_list = NULL;
//static int *hdd_enable = NULL;
//static int *hdd_detect = NULL;

static
int SetFanStatusMircopWithGPIO(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;
	int iFanDuty = -1;
	char szUartCmd[5] = {0};

	if( status == FAN_STATUS_STOP ) {
		speed = FAN_SPEED_STOP;
	}

	if( FAN_SPEED_PWM_FORMAT_SHIFT <= (int)speed ) {
		/* PWM format is only for bandon and QC test */
		iFanDuty = FAN_SPEED_SHIFT_DUTY_GET((int)speed);
		/* set fan speed hz */
		if(0 < FAN_SPEED_SHIFT_HZ_GET((int)speed)) {
			snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_FAN_FREQUENCY, FAN_SPEED_SHIFT_HZ_GET((int)speed));
			if( 0 > SetUart(szUartCmd) ) {
				goto END;
			}
		}
	} else {
		if( 0 > (iFanDuty = model_ops->x86_fan_speed_mapping(speed)) ) {
			printk("No matched fan speed!\n");
			goto END;
		}
	}

	/* set fan speed duty cycle  */
	snprintf(szUartCmd, sizeof(szUartCmd), "%s%02d", SZ_UART_FAN_DUTY_CYCLE, iFanDuty);
	if( 0 > SetUart(szUartCmd) ) {
		goto END;
	}

	iRet = 0;
END:
	return iRet;
}

int GetModel(void)
{
	int model = MODEL_INVALID;

	if ( !strcmp(gszSynoHWVersion, HW_DS1621p) ) {
		model = MODEL_DS1621p;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS1221p) ) {
		model = MODEL_RS1221p;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS1221rpp) ) {
		model = MODEL_RS1221rpp;
	} else if ( !strcmp(gszSynoHWVersion, HW_DS1821p) ) {
		model = MODEL_DS1821p;
	} else if ( !strcmp(gszSynoHWVersion, HW_DS2422p) ) {
		model = MODEL_DS2422p;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS2421p) ) {
		model = MODEL_RS2421p;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS2421rpp) ) {
		model = MODEL_RS2421rpp;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS2821rpp) ) {
		model = MODEL_RS2821rpp;
	} else if ( !strcmp(gszSynoHWVersion, HW_FS2500) ) {
		model = MODEL_FS2500;
	} else if ( !strcmp(gszSynoHWVersion, HW_FS2500T) ) {
		model = MODEL_FS2500T;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS822p) ) {
		model = MODEL_RS822p;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS822rpp) ) {
		model = MODEL_RS822rpp;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS2423p) ) {
		model = MODEL_RS2423p;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS2423rpp) ) {
		model = MODEL_RS2423rpp;
	} else if ( !strcmp(gszSynoHWVersion, HW_DS1823xsp) ) {
		model = MODEL_DS1823xsp;
	} else if ( !strcmp(gszSynoHWVersion, HW_RS1623xsp) ) {
		model = MODEL_RS1623xsp;
	} else if ( !strcmp(gszSynoHWVersion, HW_DS1623p) ) {
		model = MODEL_DS1623p;
	} else if ( !strcmp(gszSynoHWVersion, HW_DS1823p) ) {
		model = MODEL_DS1823p;
	} else if ( !strcmp(gszSynoHWVersion, HW_SC2500) ) {
		model = MODEL_SC2500;
	}

	return model;
}

int GetMaxInternalDiskNum(void)
{
	int iInternalDiskNum = -1;
	switch(GetModel()) {
		case MODEL_DS1621p:
		case MODEL_DS1623p:
			iInternalDiskNum = 6;
			break;
		case MODEL_RS1221p:
		case MODEL_RS1221rpp:
		case MODEL_DS1821p:
		case MODEL_DS1823xsp:
		case MODEL_DS1823p:
			iInternalDiskNum = 8;
			break;
		case MODEL_DS2422p:
		case MODEL_RS2421p:
		case MODEL_RS2421rpp:
		case MODEL_FS2500:
		case MODEL_FS2500T:
		case MODEL_RS2423p:
		case MODEL_RS2423rpp:
		case MODEL_SC2500:
			iInternalDiskNum = 12;
			break;
		case MODEL_RS2821rpp:
			iInternalDiskNum = 16;
			break;
		case MODEL_RS822p:
		case MODEL_RS822rpp:
		case MODEL_RS1623xsp:
			iInternalDiskNum = 4;
			break;
		default:
			printk(KERN_INFO "synobios cannot find internal disk number.\n");
			break;
	}
	return iInternalDiskNum;
}

static
int InitModuleType(struct synobios_ops *ops)
{
	int iRet = -1;

	if (model_ops && model_ops->x86_init_module_type) {
		iRet = model_ops->x86_init_module_type(ops);
	}

	return iRet;
}

static
int GetCpuTemperature(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;

	if (NULL == pCpuTemp) {
		goto END;
	}
#if defined(CONFIG_SYNO_K10TEMP) || defined(CONFIG_SYNO_HWMON_AMD_K10TEMP)
	iRet = syno_k10cpu_temperature(pCpuTemp);
#endif /* CONFIG_SYNO_HWMON_AMD_K10TEMP */

END:
	return iRet;
}


static
int SetAlarmLed(unsigned char type)
{
	int iBlinking = 0;

	if (type) {
		iBlinking = 1;
	}
	return SYNO_CTRL_ALARM_LED_SET(iBlinking);
}

static
int GetBuzzerCleared(unsigned char *buzzer_cleared)
{
	int ret = 0;

	if (model_ops && model_ops->x86_get_buzzer_cleared) {
		ret = model_ops->x86_get_buzzer_cleared(buzzer_cleared);
	}

	return ret;
}

static int GetPowerStatus(POWER_INFO *power_info)
{
	int ret = 0;

	if (model_ops && model_ops->x86_get_power_status) {
		ret = model_ops->x86_get_power_status(power_info);
	}else{
		ret = -1;
	}

	return ret;
}

static int SetHddActLed(SYNO_LED ledStatus)
{
	if (SYNO_LED_OFF == ledStatus) {
		SYNO_ENABLE_HDD_LED(0);
	} else {
		SYNO_ENABLE_HDD_LED(1);
	}

	return 0;
}

static int SetPowerLedStatus(SYNO_LED ledStatus)
{
	char szCommand[5] = {0};
	int iError = -1;

        switch(ledStatus){
		case SYNO_LED_ON:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_ON);
			break;
		case SYNO_LED_OFF:
			snprintf(szCommand, sizeof(szCommand), "%s", SZ_UART_PWR_LED_OFF);
			break;
		default:
			goto ERR;
	}

	if (0 > SetUart(szCommand)) {
		goto ERR;
	}

	iError = 0;
ERR:
	return iError;
}

static int SetHddActLedBoth(SYNO_LED ledStatus)
{
	// set GPIO
	SetHddActLed(ledStatus);

	// set led trigger through i2c 
	return SetHddActLedByLedTrigger(ledStatus);
}

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
	unsigned int freq = cpufreq_quick_get(0);

	if (!freq)
		freq = cpu_khz;

	snprintf(cpu->clock, sizeof(char) * maxLength, "%u.%3u", freq / 1000, freq % 1000);

	cpu->core = cpu_data(0).booted_cores;

}

static
int SetRedundantPowerFanFullSpeed(unsigned char type)
{
	int iFullSpeed = 0;

	if (type) {
		iFullSpeed = 1;
	}
	return SYNO_CTRL_RP_FAN(iFullSpeed);
}

#ifdef CONFIG_SYNO_ADT7490_FEATURES
static
int HWMONGetThermalSensorFromADT(SYNO_HWMON_SENSOR_TYPE *SysThermal)
{
	int iRet = -1;

	if (NULL == SysThermal || NULL == hwmon_sensor_list) {
		goto END;
	}

	memcpy(SysThermal, hwmon_sensor_list->thermal_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_thermal_sensor(SysThermal);

END:
	return iRet;
}

static
int HWMONGetFanSpeedRPMFromADT(SYNO_HWMON_SENSOR_TYPE *FanSpeedRpm)
{
	int iRet = -1;

	if (NULL == FanSpeedRpm || NULL == hwmon_sensor_list) {
		goto END;
	}

	memcpy(FanSpeedRpm, hwmon_sensor_list->fan_speed_rpm, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_fan_speed_rpm(FanSpeedRpm);

END:
	return iRet;
}

static
int HWMONGetFanSpeedRPMFromADTByOrder(SYNO_HWMON_SENSOR_TYPE *FanSpeedRpm)
{
	int iRet = -1;

	if (NULL == FanSpeedRpm || NULL == hwmon_sensor_list || NULL == hwmon_fan_order_list) {
		goto END;
	}

	memcpy(FanSpeedRpm, hwmon_sensor_list->fan_speed_rpm, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_fan_speed_rpm_by_order(FanSpeedRpm, hwmon_fan_order_list);

END:
	return iRet;
}

static
int HWMONGetVoltageSensorFromADT(SYNO_HWMON_SENSOR_TYPE *SysVoltage)
{
	int iRet = -1;

	if (NULL == SysVoltage || NULL == hwmon_sensor_list) {
		goto END;
	}

	memcpy(SysVoltage, hwmon_sensor_list->voltage_sensor, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iRet = syno_get_adt_voltage_sensor(SysVoltage);

END:
	return iRet;
}
#endif /* CONFIG_SYNO_ADT7490_FEATURES */

#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
extern void syno_smbus_hdd_powerctl_init(void);
static
int HWMONGetHDDBackPlaneStatusBySMBUS(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 0;
	int val = 0;
	unsigned long hdd_detect = 0;
	unsigned long hdd_enable = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list) {
		printk("hdd_backplane null\n");
		goto End;
	}
	if (0 >= g_smbus_hdd_powerctl) {
		goto End;
	}
	if (!SynoSmbusHddPowerCtl.bl_init){
		syno_smbus_hdd_powerctl_init();
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	for (index = 0; index < GetMaxInternalDiskNum(); index++){
		if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read) {
			val = SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read(gSynoSmbusHddAdapter, gSynoSmbusHddAddress, index+1);
			hdd_detect |= (val & 0x01) << index;
		}
		if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read) {
			val = SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read(gSynoSmbusHddAdapter, gSynoSmbusHddAddress, index+1);
			hdd_enable |= (val & 0x01) << index;
		}
	}

	if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read) {
		snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_detect);
	}

	if (NULL != SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read) {
		snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_enable);
	}

	iRet = 0;
End:
	return iRet;
}
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

int HWMONGetHDDBackPlaneStatusByGPIO(struct _SYNO_HWMON_SENSOR_TYPE *hdd_backplane)
{
	int iRet = -1;
	int index = 1;
	int iInternalDiskNum = -1;
	unsigned long hdd_detect = 0;
	unsigned long hdd_enable = 0;

	if (NULL == hdd_backplane || NULL == hwmon_sensor_list) {
		printk("hdd_backplane null\n");
		goto End;
	}

	memcpy(hdd_backplane, hwmon_sensor_list->hdd_backplane, sizeof(SYNO_HWMON_SENSOR_TYPE));

	iInternalDiskNum = GetMaxInternalDiskNum();

	for (index = 1; index <= iInternalDiskNum; index++) {
		if (HAVE_HDD_DETECT(index)) {
			hdd_detect |= ((SYNO_GPIO_READ(HDD_DETECT_PIN(index)) ^ HDD_DETECT_POLARITY(index)) & 0x01) << (index-1);
		}
	}

	for (index = 1; index <= iInternalDiskNum; index++) {
		if (HAVE_HDD_ENABLE(index)) {
			hdd_enable |= ((SYNO_GPIO_READ(HDD_ENABLE_PIN(index)) ^ HDD_ENABLE_POLARITY(index)) & 0x01) << (index-1);
		}
	}

	snprintf(hdd_backplane->sensor[0].value, sizeof(hdd_backplane->sensor[0].value), "%ld", hdd_detect);
	snprintf(hdd_backplane->sensor[1].value, sizeof(hdd_backplane->sensor[1].value), "%ld", hdd_enable);

	iRet = 0;

End:
	return iRet;
}

int V1000RedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	GPIO_PIN pinPower1;
	GPIO_PIN pinPower2;
	int err = -1;

	pinPower1.pin = syno_gpio.redundant_power_detect->gpio_port[0];
	pinPower2.pin = syno_gpio.redundant_power_detect->gpio_port[1];
	if (0 > GetGpioPin(&pinPower1) || 0 > GetGpioPin(&pinPower2)) {
		goto END;
	}


	if (pinPower1.value == GPIO_POWER_GOOD) {
		power_info->power_1 = POWER_STATUS_GOOD;
	} else {
		power_info->power_1 = POWER_STATUS_BAD;
	}

	if (pinPower2.value == GPIO_POWER_GOOD) {
		power_info->power_2 = POWER_STATUS_GOOD;
	} else {
		power_info->power_2 = POWER_STATUS_BAD;
	}

	err = 0;

END:
	return err;
}

#ifdef CONFIG_SYNO_HWMON_PMBUS
int V1000RedundantPowerGetPowerStatusByI2C(POWER_INFO *power_info)
{
	int iRet = -1;

	if (NULL == power_info) {
		printk("%s:%d in %s: parameters error!\n", __FILE__, __LINE__, __FUNCTION__);
		goto END;
	}

	if (0 > I2CPmbusReadPowerStatus(0, &(power_info->power_1))) {
		goto END;
	}
	if (0 > I2CPmbusReadPowerStatus(1, &(power_info->power_2))) {
		goto END;
	}

	iRet = 0;

END:
	return iRet;
}
#endif /* CONFIG_SYNO_HWMON_PMBUS */

static struct synobios_ops synobios_ops = {
	.owner               = THIS_MODULE,
	.get_brand           = GetBrand,
	.get_model           = GetModel,
	.get_rtc_time        = rtc_seiko_get_time,
	.set_rtc_time        = rtc_seiko_set_time,
	.get_auto_poweron    = rtc_get_auto_poweron,
	.set_auto_poweron    = rtc_seiko_set_auto_poweron,
	.init_auto_poweron   = rtc_seiko_auto_poweron_init,
	.uninit_auto_poweron = rtc_seiko_auto_poweron_uninit,
	.get_fan_status      = GetFanStatusActivePulse,
	.set_fan_status      = SetFanStatusMircopWithGPIO,
	.get_sys_temperature = NULL,
	.get_cpu_temperature = GetCpuTemperature,
	.set_cpu_fan_status  = NULL,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
	.set_disk_led        = NULL,
	.set_alarm_led       = SetAlarmLed,
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.get_power_status    = GetPowerStatus,
	.uninitialize        = Uninitialize,
	.get_cpu_info        = GetCPUInfo,
	.set_aha_led         = NULL,
	.get_copy_button_status = NULL, // for matching userspace usage, button pressed = 0, else = 1
	.hwmon_get_fan_speed_rpm    = NULL,
	.hwmon_get_sys_thermal      = NULL,
	.hwmon_get_sys_voltage      = NULL,
	.hwmon_get_psu_status       = NULL,
	.hwmon_get_backplane_status = NULL,
	.set_rp_fan          = NULL,
};

#ifdef CONFIG_SYNO_HWMON_PMBUS
int V1000GetPSUStatusByI2C(SYNO_HWMON_SENSOR_TYPE *psu_status, int iPsuNumber)
{
	int iRet = -1;

	if (NULL == hwmon_sensor_list) {
		goto END;
	}
	memcpy(psu_status, hwmon_sensor_list->psu_status,
		iPsuNumber * sizeof(SYNO_HWMON_SENSOR_TYPE));
	iRet = HWMONGetPSUStatusByI2C(psu_status, iPsuNumber);
END:
	return iRet;
}
#endif /* CONFIG_SYNO_HWMON_PMBUS */

int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops)
{
	int i = 0;
	DISKLEDSTATUS diskLedStatus;
	const char* abbrName[] = {"internal", "sys"};
	*ops = &synobios_ops;

	switch(GetModel())
	{
		case MODEL_DS1621p:
			model_ops = &ds1621p_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &ds1621p_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS; 
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS1221p:
			model_ops = &rs1221p_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs1221p_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS; 
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS1221rpp:
			model_ops = &rs1221rpp_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs1221rpp_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS; 
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_DS1821p:
			model_ops = &ds1821p_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &ds1821p_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_DS2422p:
			model_ops = &ds2422p_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &ds2422p_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#if defined(CONFIG_SYNO_JMICRON_585_GPIO_LED_CTRL) || defined(CONFIG_SYNO_SATA_JMB585_GPIO_LED_CTRL)
			//wait jmb585 initialization then close orange light of all disks
			syno_jmb585_led_wait();
			synobios_ops.set_disk_led = SetDiskLedStatusByJMB585GPIOByPort;
			diskLedStatus.szNodeName = abbrName[0];
			diskLedStatus.iNodeNameLen = strlen("internal");
			diskLedStatus.status = 0;
			for (i = 1; i <= 12; i++) {
				diskLedStatus.diskno = i;
				SetDiskLedStatusByJMB585GPIOByPort(&diskLedStatus);
			}
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByJMB585GPIOByPort;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif /* CONFIG_SYNO_JMICRON_585_GPIO_LED_CTRL || CONFIG_SYNO_SATA_JMB585_GPIO_LED_CTRL */
			break;
		case MODEL_RS2421p:
			model_ops = &rs2421p_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs2421p_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#if defined (CONFIG_SYNO_ASM_116X_GPIO_LED_CTRL) || defined(CONFIG_SYNO_SATA_ASM116X_GPIO_LED_CTRL)
			synobios_ops.set_disk_led = SetDiskLedStatusByASM116XGPIOByPort;
			diskLedStatus.szNodeName = abbrName[0];
			diskLedStatus.iNodeNameLen = strlen("internal");
			diskLedStatus.status = 0;
			for (i = 1; i <= 12; i++) {
				diskLedStatus.diskno = i;
				SetDiskLedStatusByASM116XGPIOByPort(&diskLedStatus);
			}
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByASM116XGPIOByPort;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif /* CONFIG_SYNO_ASM_116X_GPIO_LED_CTRL || CONFIG_SYNO_SATA_ASM116X_GPIO_LED_CTRL */
			break;
		case MODEL_RS2421rpp:
			model_ops = &rs2421rpp_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs2421rpp_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = V1000GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#if defined (CONFIG_SYNO_ASM_116X_GPIO_LED_CTRL) || defined(CONFIG_SYNO_SATA_ASM116X_GPIO_LED_CTRL)
			synobios_ops.set_disk_led = SetDiskLedStatusByASM116XGPIOByPort;
			diskLedStatus.szNodeName = abbrName[0];
			diskLedStatus.iNodeNameLen = strlen("internal");
			diskLedStatus.status = 0;
			for (i = 1; i <= 12; i++) {
				diskLedStatus.diskno = i;
				SetDiskLedStatusByASM116XGPIOByPort(&diskLedStatus);
			}
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByASM116XGPIOByPort;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif /* CONFIG_SYNO_ASM_116X_GPIO_LED_CTRL || CONFIG_SYNO_SATA_ASM116X_GPIO_LED_CTRL */
			break;
		case MODEL_RS2821rpp:
			model_ops = &rs2821rpp_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs2821rpp_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = V1000GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
#if defined(CONFIG_SYNO_JMICRON_585_GPIO_LED_CTRL) || defined(CONFIG_SYNO_SATA_JMB585_GPIO_LED_CTRL)
			//wait jmb585 initialization then close orange light of all disks
			syno_jmb585_led_wait();
			synobios_ops.set_disk_led = SetDiskLedStatusByJMB585GPIOByPort;
			diskLedStatus.szNodeName = abbrName[0];
			diskLedStatus.iNodeNameLen = strlen("internal");
			diskLedStatus.status = 0;
			for (i = 1; i <= 16; i++) {
				diskLedStatus.diskno = i;
				SetDiskLedStatusByJMB585GPIOByPort(&diskLedStatus);
			}
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByJMB585GPIOByPort;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif /* CONFIG_SYNO_JMICRON_585_GPIO_LED_CTRL || CONFIG_SYNO_SATA_JMB585_GPIO_LED_CTRL */
			break;
		case MODEL_FS2500:
			model_ops = &fs2500_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &fs2500_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
			diskLedStatus.szNodeName = abbrName[0];
			diskLedStatus.iNodeNameLen = strlen("internal");
			diskLedStatus.status = 0;
#ifdef CONFIG_SYNO_MV1475_SGPIO_LED_CTRL
			synobios_ops.set_disk_led = SYNODiskLedCtrlBy1475SGPIO;
#endif /* CONFIG_SYNO_MV1475_SGPIO_LED_CTRL */
			break;
		case MODEL_FS2500T:
			model_ops = &fs2500t_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &fs2500t_sensor_list;
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLed;
			synobios_ops.set_power_led = SetPowerLedStatus;
			diskLedStatus.szNodeName = abbrName[0];
			diskLedStatus.iNodeNameLen = strlen("internal");
			diskLedStatus.status = 0;
#ifdef CONFIG_SYNO_MV1475_SGPIO_LED_CTRL
			synobios_ops.set_disk_led = SYNODiskLedCtrlBy1475SGPIO;
#endif /* CONFIG_SYNO_MV1475_SGPIO_LED_CTRL */
			break;
		case MODEL_RS822p:
			model_ops = &rs822p_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs822p_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO; 
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS822rpp:
			model_ops = &rs822rpp_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs822rpp_sensor_list;
			hwmon_fan_order_list = &rs822rpp_fan_order_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADTByOrder;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO; 
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
			synobios_ops.set_rp_fan = SetRedundantPowerFanFullSpeed;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS2423p:
			model_ops = &rs2423p_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs2423p_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			for (i = 1; i <= 12; i++) {
				syno_ahci_disk_led_enable_by_port(i, 1);
			}
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		case MODEL_RS2423rpp:
			model_ops = &rs2423rpp_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs2423rpp_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = V1000GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			for (i = 1; i <= 12; i++) {
				syno_ahci_disk_led_enable_by_port(i, 1);
			}
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;
		
		case MODEL_DS1823xsp:
			model_ops = &ds1823xsp_ops;

			I2cBusChange(0);

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &ds1823xsp_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLedBoth;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;

		case MODEL_RS1623xsp:
			model_ops = &rs1623xsp_ops;

			I2cBusChange(0);

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &rs1623xsp_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusByGPIO;
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;

		case MODEL_DS1623p:
			model_ops = &ds1623p_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &ds1623p_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLedBoth;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;

		case MODEL_DS1823p:
			model_ops = &ds1823p_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &ds1823p_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
			synobios_ops.set_hdd_led = SetHddActLedBoth;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			syno_ahci_disk_led_enable_by_port(1, 1);
			syno_ahci_disk_led_enable_by_port(2, 1);
			syno_ahci_disk_led_enable_by_port(3, 1);
			syno_ahci_disk_led_enable_by_port(4, 1);
			syno_ahci_disk_led_enable_by_port(5, 1);
			syno_ahci_disk_led_enable_by_port(6, 1);
			syno_ahci_disk_led_enable_by_port(7, 1);
			syno_ahci_disk_led_enable_by_port(8, 1);
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;

		case MODEL_SC2500:
			model_ops = &sc2500_ops;

			if ( model_ops->x86_gpio_init ) {
				model_ops->x86_gpio_init();
			}
			hwmon_sensor_list = &sc2500_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#if defined(CONFIG_SYNO_SMBUS_HDD_POWERCTL) || defined(CONFIG_SYNO_SATA_PWR_CTRL_SMBUS)
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL || CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = V1000GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
			synobios_ops.set_power_led = SetPowerLedStatus;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			for (i = 1; i <= 12; i++) {
				syno_ahci_disk_led_enable_by_port(i, 1);
			}
#endif /* CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY */
			break;

		default :
			break;
	}

	if( synobios_ops.init_auto_poweron ) {
		synobios_ops.init_auto_poweron();
	}

	return 0;
}

static
int Uninitialize(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;

	if( synobios_ops.get_rtc_time ) {
		synobios_ops.get_rtc_time(&rtc_time_pkt);
	}

	if( synobios_ops.uninit_auto_poweron ) {
		synobios_ops.uninit_auto_poweron();
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
	if ( model_ops->x86_gpio_cleanup ) {
		model_ops->x86_gpio_cleanup();
	}

	return 0;
}
