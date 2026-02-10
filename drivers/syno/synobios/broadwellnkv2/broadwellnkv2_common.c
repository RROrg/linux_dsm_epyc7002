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
#include "../rtc/rtc.h"
#include "../i2c/i2c-linux.h"
#include "../pmbus/pmbus.h"
#include "broadwellnkv2_common.h"

#ifdef MY_DEF_HERE
extern int giDenoOfTimeInterval;
#endif /* MY_DEF_HERE */

static struct model_ops *model_ops = NULL;
static struct hwmon_sensor_list *hwmon_sensor_list = NULL;
int (*GetMaxInternalHostNum)(void) = NULL;

#ifdef MY_DEF_HERE
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE */

#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
extern int (*syno_valid_lsi3008_led)(u8 cmd);
#endif /* CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL */

#ifdef CONFIG_SYNO_ICH_GPIO_CTRL
int SetIchGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if ( NULL == pPin ) {
		goto End;
	}

	if ( 75 < pPin->pin ) {
		goto End;
	}

	if (0 != syno_pch_lpc_gpio_pin((int)pPin->pin, (int*)&pPin->value, 1)) {
		goto End;
	}

	ret = 0;
End:
	return ret;
}

int GetIchGpioPin( GPIO_PIN *pPin )
{
	int ret = -1;

	if ( NULL == pPin ) {
		goto End;
	}

	if ( 75 < pPin->pin ) {
		goto End;
	}

	if (0 != syno_pch_lpc_gpio_pin((int)pPin->pin, (int*)&pPin->value, 0)) {
		goto End;
	}

	ret = 0;
End:
	return ret;
}
#endif /* CONFIG_SYNO_ICH_GPIO_CTRL */
/**
 *	Set Max internal disk numbers
 */
int GetMaxInternalDiskNum(void)
{
	int iMaxInternalDiskNum = 0;

	switch(GetModel()) {
		case MODEL_RS4022xsp:
			iMaxInternalDiskNum = 16;
			break;
		case MODEL_FS3410:
			iMaxInternalDiskNum = 24;
			break;
		case MODEL_SA3410:
		case MODEL_SA3610:
			iMaxInternalDiskNum = 12;
			break;
	}
	return iMaxInternalDiskNum;
}

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

	Pin.pin = BROADWELLNKV2_BUZZER_CTRL_PIN;
	Pin.value = buzzer_cleared;
	if (0 > SetIchGpioPin(&Pin)) {
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

	Pin.pin = BROADWELLNKV2_BUZZER_OFF_PIN;
    if ( 0 > GetIchGpioPin( &Pin ) ) {
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

int xsCPUFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;

	return iDutyCycle;
}

#define GPIO_POWER_GOOD	1
int Broadwellnkv2RedundantPowerGetPowerStatus(POWER_INFO *power_info)
{
	int err = -1;
	int power1_Value = 0, power2_Value = 0;

	if ( 0 != syno_pch_lpc_gpio_pin(BROADWELLNKV2_POWER1_PIN , &power1_Value, 0) ) {
		goto FAIL;
	}

	if ( 0 != syno_pch_lpc_gpio_pin(BROADWELLNKV2_POWER2_PIN , &power2_Value, 0) ) {
		goto FAIL;
	}

	if (power1_Value == GPIO_POWER_GOOD) {
		power_info->power_1 = POWER_STATUS_GOOD;
	}else{
		power_info->power_1 = POWER_STATUS_BAD;
	}

	if (power2_Value == GPIO_POWER_GOOD) {
		power_info->power_2 = POWER_STATUS_GOOD;
	}else{
		power_info->power_2 = POWER_STATUS_BAD;
	}

	err = 0;

FAIL:
	return err;
}

static
int GetFanStatus(int fanno, FAN_STATUS *pStatus)
{
	return -1;
}

int SetFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;

	return iRet;
}

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

#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
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
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */

static
int SetCpuFanStatus(FAN_STATUS status, FAN_SPEED speed)
{
	int iRet = -1;

	return iRet;
}

static
int Uninitialize(void)
{
	SYNORTCTIMEPKT rtc_time_pkt;
	rtc_bandon_get_time(&rtc_time_pkt);
	return 0;
}

int GetModel(void)
{
	int model = MODEL_DS3018xs;

	if (!strncmp(gszSynoHWVersion, HW_RS4022xsp, strlen(HW_RS4022xsp))) {
			model = MODEL_RS4022xsp;
	}else if (!strncmp(gszSynoHWVersion, HW_FS3410, strlen(HW_FS3410))) {
		model = MODEL_FS3410;
	}else if (!strncmp(gszSynoHWVersion, HW_SA3410, strlen(HW_SA3410))) {
		model = MODEL_SA3410;
	}else if (!strncmp(gszSynoHWVersion, HW_SA3610, strlen(HW_SA3610))) {
		model = MODEL_SA3610;
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

	if (NULL == pCpuTemp) {
		goto END;
	}

#ifdef CONFIG_SYNO_X86_CORETEMP
	iRet = syno_cpu_temperature(pCpuTemp);
#endif /* CONFIG_SYNO_X86_CORETEMP */

END:
	return iRet;
}

static
int SetAlarmLed(unsigned char ledON)
{
	GPIO_PIN Pin;
	int iRet = -1;

	Pin.pin = BROADWELLNKV2_ALARM_LED_PIN;
	/**
	 *	Attetion!!
	 *	0 Means alarm on
	 *	1 Means alarm off
	 *	need to reverse
	 */
	if (ledON) {
		Pin.value = 0;
	} else {
		Pin.value = 1;
	}

	if (0 > SetIchGpioPin(&Pin)) {
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

	if (!freq)
		freq = cpu_khz;

	snprintf(cpu->clock, sizeof(char) * maxLength, "%u.%3u", freq / 1000, freq % 1000);

	cpu->core = cpu_data(0).booted_cores;

}

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
	.get_gpio_pin        = GetIchGpioPin,
	.set_gpio_pin        = SetIchGpioPin,
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
int Broadwellnkv2GetPSUStatusByI2C(SYNO_HWMON_SENSOR_TYPE *psu_status, int iPsuNumber)
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
	int i, maxdisk = 0;
	*ops = &synobios_ops;

	// for those model that have mix ahci and 9xxx internal disk, please implement GetMaxInternalHostNum
	GetMaxInternalHostNum = NULL;

	switch(GetModel())
	{
		case MODEL_RS4022xsp:
			model_ops = &rs4022xsp_ops;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
			break;
		case MODEL_FS3410:
			model_ops = &fs3410_ops;
			FS3410SMBusSwitchInit();
			hwmon_sensor_list = &fs3410_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = Broadwellnkv2GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
			g_smbus_hdd_powerctl = 1;
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */
			synobios_ops.set_hdd_led = SetHddActLedByLedTrigger;
#ifdef CONFIG_SYNO_LEDS_TRIGGER
			synobios_ops.set_disk_led = SetDiskLedStatusByLedTrigger;
			SetupDiskLedMap();
#ifdef MY_DEF_HERE
			funcSYNOSATADiskLedCtrl = SYNOSetDiskLedStatusByLedTrigger;
#endif // MY_DEF_HERE
#endif // CONFIG_SYNO_LEDS_TRIGGER
#ifdef CONFIG_SYNO_AHCI_SOFTWARE_ACITIVITY
			maxdisk = GetMaxInternalDiskNum();
			for (i = 1; i <= maxdisk; ++i) {
				syno_ahci_disk_led_enable_by_port(i, 1);
			}
#endif
			break;
		case MODEL_SA3410:
			model_ops = &sa3410_ops;
			hwmon_sensor_list = &sa3410_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = Broadwellnkv2GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */
			break;
		case MODEL_SA3610:
			model_ops = &sa3610_ops;
			hwmon_sensor_list = &sa3610_sensor_list;
#ifdef CONFIG_SYNO_ADT7490_FEATURES
			synobios_ops.hwmon_get_fan_speed_rpm = HWMONGetFanSpeedRPMFromADT;
			synobios_ops.hwmon_get_sys_voltage = HWMONGetVoltageSensorFromADT;
			synobios_ops.hwmon_get_sys_thermal = HWMONGetThermalSensorFromADT;
#endif /* CONFIG_SYNO_ADT7490_FEATURES */
#ifdef CONFIG_SYNO_HWMON_PMBUS
			synobios_ops.hwmon_get_psu_status = Broadwellnkv2GetPSUStatusByI2C;
#endif /* CONFIG_SYNO_HWMON_PMBUS */
#ifdef CONFIG_SYNO_SMBUS_HDD_POWERCTL
			synobios_ops.hwmon_get_backplane_status = HWMONGetHDDBackPlaneStatusBySMBUS;
#endif /* CONFIG_SYNO_SMBUS_HDD_POWERCTL */
			break;
		default:
			break;
	}

	return 0;
}

int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops)
{
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

#ifdef CONFIG_SYNO_HWMON_PMBUS
// Portmappingv2 method to get PSU status
int redundantPowerGetPowerStatusByI2C(POWER_INFO *power_info)
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
