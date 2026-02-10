#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2009 Synology Inc. All rights reserved.

#include <linux/syno.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include <linux/fs.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include "../i2c/i2c-linux.h"
#include "../rtc/rtc.h"
#include "purley_common.h"
#include "../led/led_gpio.h"
#include "../pmbus/pmbus.h"

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

static struct model_ops *model_ops = NULL;

#ifdef CONFIG_SYNO_XR17V35X_SERIAL
extern int gSynoBuzzerMutePressed;
#endif

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL */
extern int SA6500GetDiskIntf(SYNO_DISK_INTF_INFO *input);

static struct hwmon_sensor_list *hwmon_sensor_list = NULL;

// following functions are not chagned in any bromolow models, so we move them from model.o to here
// If there is any necessary to modify these function on any other model, please rewrite another function to replace it
PWM_FAN_SPEED_MAPPING gxsSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 15 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 25 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 35 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 45 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 55 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

PWM_FAN_SPEED_MAPPING gxsCPUFanSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 15 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 25 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 35 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 45 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 55 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

int xsFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gxsSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gxsSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gxsSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

/*
 * On RS10613xsp, and RS3413xsp we have no buzzer clear button anymore, so we need another way to stop redundant power buzzer
 */
int xsSetBuzzerClear(unsigned char buzzer_cleared)
{
	GPIO_PIN Pin;
	int ret = -1;

	Pin.pin = PURLEY_BUZZER_CTRL_PIN;
	Pin.value = buzzer_cleared;
	if (0 > SetGpioPin(&Pin)) {
#ifdef MY_DEF_HERE
		giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
		goto End;
	}
	ret = 0;
End:
	return ret;
}

int xsGetBuzzerCleared(unsigned char *buzzer_cleared)
{
    GPIO_PIN Pin;
    int ret = -1;

	if ( NULL == buzzer_cleared ) {
		goto End;
	}

	*buzzer_cleared = 0;

	Pin.pin = PURLEY_BUZZER_OFF_BUTTON_PIN;
    if ( 0 > GetGpioPin( &Pin ) ) {
#ifdef MY_DEF_HERE
		giDenoOfTimeInterval = -1;
#endif /* MY_DEF_HERE */
        goto End;
    }

    if ( 0 == Pin.value ) {
        *buzzer_cleared = 1;
		// after read buzzer cleared, pull it back to high
		// an workaround for #48623, because gpio 4 & 5 are jointed together
		if (model_ops && model_ops->x86_set_buzzer_clear) {
			model_ops->x86_set_buzzer_clear(1);
		}
    }

    ret = 0;
End:
    return ret;

}

int GetMaxInternalDiskNum(void)
{
	int iInternalDiskNum = -1;
	switch(GetModel()) {
		case MODEL_FS6400:
		case MODEL_FS6600N:
		case MODEL_FS6500:
			iInternalDiskNum = 24;
			break;
		case MODEL_SA6500:
			iInternalDiskNum = 20;
			break;
		case MODEL_HD6500:
			iInternalDiskNum = 60;
			break;
		default:
			printk(KERN_INFO "synobios cannot find internal disk number.\n");
			break;
	}
	return iInternalDiskNum;
}

int xsCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;

	return iDutyCycle;
}

#define GPIO_POWER_GOOD	1
int PurleyRedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	int err = -1;

	return err;
}

static
int GetFanStatusMircopWithGPIO(int fanno, FAN_STATUS *pStatus)
{
	return -1;
}

static
int GetFanStatus(int fanno, FAN_STATUS *pStatus)
{
	return -1;
}

static
int SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;

	return iRet;
}

static
int SetCpuFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;

	return iRet;
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

static struct synobios_ops synobios_ops;
static
int Uninitialize(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;
	rtc_bandon_get_time(&rtc_time_pkt);

	switch(GetModel())
	{
		case MODEL_FS6600N:
			if (synobios_ops.set_hdd_led) {
				synobios_ops.set_hdd_led (SYNO_LED_OFF);
			}
			break;
		default:
			break;
	}

	return 0;
}

int GetModel(void)
{
	int model = MODEL_FS6400;

	if (!strncmp(gszSynoHWVersion, HW_FS6400, strlen(HW_FS6400))) {
		model = MODEL_FS6400;
	} else if (!strncmp(gszSynoHWVersion, HW_FS6500, strlen(HW_FS6500))) {
		model = MODEL_FS6500;
	} else if (!strncmp(gszSynoHWVersion, HW_SA6500, strlen(HW_SA6500))) {
		model = MODEL_SA6500;
	} else if (!strncmp(gszSynoHWVersion, HW_HD6500, strlen(HW_HD6500))) {
		model = MODEL_HD6500;
	} else if (!strncmp(gszSynoHWVersion, HW_FS6600N, strlen(HW_FS6600N))) {
		model = MODEL_FS6600N;
	}

	return model;
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
int GetSysTemperature(struct _SynoThermalTemp *pThermalTemp)
{
	if (!pThermalTemp) {
		return -1;
	}

#ifdef MY_DEF_HERE
	syno_sys_temperature(pThermalTemp);
#endif /*MY_DEF_HERE*/

	return 0;
}

static
int GetCpuTemperatureI3Transfer(struct _SynoCpuTemp *pCpuTemp)
{
	int iRet = -1;
	int iCPUIdx;

	if ( NULL == pCpuTemp ) {
		goto END;
	}
#ifdef CONFIG_SYNO_ADT7490_FEATURES
	iRet = syno_get_adt_peci(pCpuTemp);
#endif /* CONFIG_SYNO_ADT7490_FEATURES */

	if( 0 != iRet) {
		goto END;
	}

	for(iCPUIdx = 0; iCPUIdx < pCpuTemp->cpu_num; iCPUIdx++) {
		pCpuTemp->cpu_temp[iCPUIdx] = pCpuTemp->cpu_temp[iCPUIdx] / 1000;
	}
END:
	return iRet;
}

static
int SetAlarmLed(unsigned char ledON)
{
	GPIO_PIN Pin;
	int iRet = -1;

	Pin.pin = PURLEY_ALARM_LED_PIN;
	/**
	 *      Attetion!!
	 *      0 Means alarm on
	 *      1 Means alarm off
	 *      need to reverse
	 */
	if (ledON) {
		Pin.value = 0;
	} else {
		Pin.value = 1;
	}

	if (0 > SetGpioPin(&Pin)) {
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

static
int SetBuzzerClear(unsigned char buzzer_cleared)
{
	int ret = 0;

	if (model_ops && model_ops->x86_set_buzzer_clear) {
		ret = model_ops->x86_set_buzzer_clear(buzzer_cleared);
	}

	return ret;
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

void GetCPUInfo(SYNO_CPU_INFO *cpu, const unsigned int maxLength)
{
	unsigned int freq = cpufreq_quick_get(0);
	int iProcNum = 0;
	int iCpuCore = 0;
	int iCpuNum = 0;

	if (!freq)
		freq = cpu_khz;

	snprintf(cpu->clock, sizeof(char) * maxLength, "%u.%3u", freq / 1000, freq % 1000);

	memset(cpu->cpucore, 0, CONFIG_SYNO_MULTI_CPU_NUM * sizeof(unsigned int));
	for_each_online_cpu(iProcNum) {
		if(iCpuNum < cpu_data(iProcNum).phys_proc_id + 1) {
			cpu->cpucore[iCpuNum] = cpu_data(iProcNum).booted_cores;
			iCpuNum++;
			iCpuCore += cpu_data(iProcNum).booted_cores;
		}
	}
	cpu->core = iCpuCore;
	return ;

}

#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
extern int (*syno_valid_lsi3008_led)(SYNO_LED ledStatus);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
extern int (*syno_valid_lsi3008_led)(u8 cmd);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
#endif /*CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL*/

static int SetHddActLed(SYNO_LED ledStatus)
{
	if (SYNO_LED_OFF == ledStatus) {
		SYNO_ENABLE_HDD_LED(0);
	} else {
		SYNO_ENABLE_HDD_LED(1);
	}

	return 0;
}

static struct synobios_ops synobios_ops = {
	.owner               = THIS_MODULE,
	.get_brand           = GetBrand,
	.get_model           = GetModel,
	.get_rtc_time        = rtc_bandon_get_time,
	.set_rtc_time        = rtc_bandon_set_time,
	.get_auto_poweron	 = rtc_get_auto_poweron,
	.set_auto_poweron	 = rtc_bandon_set_auto_poweron,
	.get_fan_status      = GetFanStatus,
	.set_fan_status      = SetFanStatus,
	.get_sys_temperature = GetSysTemperature,
	.get_cpu_temperature = GetCpuTemperatureI3Transfer,
	.set_cpu_fan_status  = SetCpuFanStatus,
	.get_gpio_pin        = GetGpioPin,
	.set_gpio_pin        = SetGpioPin,
	.set_disk_led        = NULL,
	.set_alarm_led       = SetAlarmLed,
	.module_type_init    = InitModuleType,
	.get_buzzer_cleared  = GetBuzzerCleared,
	.get_power_status    = GetPowerStatus,
	.uninitialize		 = Uninitialize,
	.check_microp_id	 = NULL,
	.set_microp_id		 = NULL,
	.set_buzzer_clear	 = SetBuzzerClear,
	.get_cpu_info		 = GetCPUInfo,
	.set_aha_led          = NULL,
	.hwmon_get_fan_speed_rpm    = NULL,
	.hwmon_get_sys_thermal      = NULL,
	.hwmon_get_sys_voltage      = NULL,
	.hwmon_get_psu_status       = NULL,
	.hwmon_get_backplane_status = NULL,
};

#ifdef CONFIG_SYNO_HWMON_PMBUS
int PurleyGetPSUStatusByI2C(SYNO_HWMON_SENSOR_TYPE *psu_status, int iPsuNumber)
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
	*ops = &synobios_ops;

	switch(GetModel())
	{
		case MODEL_FS6400:
			model_ops = &fs6400_ops;
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
			if (NULL != syno_valid_lsi3008_led){
				synobios_ops.set_hdd_led = syno_valid_lsi3008_led;
			}
#endif /*config_syno_sas_host_disk_led_ctrl*/
			hwmon_sensor_list = &fs6400_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
			break;
		case MODEL_FS6500:
			model_ops = &fs6500_ops;
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
			if (NULL != syno_valid_lsi3008_led){
				synobios_ops.set_hdd_led = syno_valid_lsi3008_led;
			}
#endif /*config_syno_sas_host_disk_led_ctrl*/
			hwmon_sensor_list = &fs6500_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
			break;
		case MODEL_SA6500:
			model_ops = &sa6500_ops;
			SA6500SMBusSwitchInit();
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
		if (NULL != syno_valid_lsi3008_led){
			synobios_ops.set_hdd_led = syno_valid_lsi3008_led;
		}
#endif /*config_syno_sas_host_disk_led_ctrl*/
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL
#endif /* CONFIG_SYNO_LEDS_TRIGGER */
			hwmon_sensor_list = &sa6500_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = PurleyGetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			synobios_ops.get_disk_intf = SA6500GetDiskIntf;
			break;
		case MODEL_HD6500:
			model_ops = &hd6500_ops;
			synobios_ops.set_disk_led = synoDiskLedControlByGpio;
			hwmon_sensor_list = &hd6500_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = PurleyGetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			if (model_ops->x86_gpio_init) {
				model_ops->x86_gpio_init();
			}
			synobios_ops.set_hdd_led = SetHddActLed;
			break;
		case MODEL_FS6600N:
			model_ops = &fs6600n_ops;
			hwmon_sensor_list = &fs6600n_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = PurleyGetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
			if (0 == switchtec_init()) {
				synobios_ops.set_hdd_led = switchtec_led_set;
				synobios_ops.set_disk_led = switchtec_set_disk_led;
			}
			break;
		default:
			break;
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
	switch(GetModel())
	{
		case MODEL_FS6600N:
			switchtec_deinit();
			break;
		default:
			break;
	}
	return 0;
}

int I2CSmbusReadPowerStatus(int i2c_bus_no, u16 i2c_addr, SYNO_POWER_STATUS* status)
{
	int ret = -1;
	int err = -1;
	u16 data = 0xffff;
	static u16 uLostAddr = 0;

	if (NULL == status) {
		printk("%s:%d in %s: parameters error!\n", __FILE__, __LINE__, __FUNCTION__);
		goto FAIL;
	}

	err = linuxI2CSmbusRegRead(i2c_bus_no, i2c_addr, PSU_DELTA_AC139_I2C_REG, &data);
	if (0 != err) {
		if (uLostAddr != i2c_addr) {
			uLostAddr = i2c_addr;
			printk("%s:%d return error!\n", __FILE__, __LINE__);
		}
		// i2c will read failed when 800W power is not inserted
		// report bad power status with success return to avoid repeated log
		*status = POWER_STATUS_BAD;
		ret = 0;
		goto FAIL;
	}
	if (uLostAddr == i2c_addr) {
		uLostAddr = 0;
	}

	if (data & PSU_DELTA_AC139_I2C_REG_ABNORMAL_STATUS_BIT) {
		*status = POWER_STATUS_BAD;
	} else {
		*status = POWER_STATUS_GOOD;
	}

	ret = 0;

FAIL:
	return ret;
}
