// Copyright (c) 2000-2011 Synology Inc. All rights reserved.

#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/delay.h>
#include "synobios.h"
#include <linux/gpio.h>
#include "evansport_common.h"

PWM_FAN_SPEED_MAPPING gDS214playSpeedMapping[] = {
	{ .fanSpeed = FAN_SPEED_STOP,       .iDutyCycle = 0  },
	{ .fanSpeed = FAN_SPEED_ULTRA_LOW,  .iDutyCycle = 20 },
	{ .fanSpeed = FAN_SPEED_VERY_LOW,   .iDutyCycle = 30 },
	{ .fanSpeed = FAN_SPEED_LOW,        .iDutyCycle = 40 },
	{ .fanSpeed = FAN_SPEED_MIDDLE,     .iDutyCycle = 50 },
	{ .fanSpeed = FAN_SPEED_HIGH,       .iDutyCycle = 65 },
	{ .fanSpeed = FAN_SPEED_VERY_HIGH,  .iDutyCycle = 80 },
	{ .fanSpeed = FAN_SPEED_ULTRA_HIGH, .iDutyCycle = 99 },
	{ .fanSpeed = FAN_SPEED_FULL,       .iDutyCycle = 99 },
};

static
int DS214playInitModuleType(struct synobios_ops *ops)
{	
	module_t type_214play = MODULE_T_DS214playv1;
	module_t *pType = &type_214play;
	
	module_type_set(pType);
	return 0;
}

static
int DS214playFanSpeedMapping(FAN_SPEED speed)
{
	int iDutyCycle = -1;
	size_t i;

	for( i = 0; i < sizeof(gDS214playSpeedMapping)/sizeof(PWM_FAN_SPEED_MAPPING); ++i ) {
		if( gDS214playSpeedMapping[i].fanSpeed == speed ) {
			iDutyCycle = gDS214playSpeedMapping[i].iDutyCycle;
			break;
		}
	}

	return iDutyCycle;
}

#define SZ_CMD_LED_ESATA_ON    "LE1"
#define SZ_CMD_LED_ESATA_BLINK "LE2"
#define SZ_CMD_LED_ESATA_OFF   "LE3"
static 
int DS214playSetEsataLedStatus(SYNO_DISK_LED status)
{
	int iRet = -1;

	switch ( status ) {
	case DISK_LED_OFF:
		SetUart(SZ_CMD_LED_ESATA_OFF);
		break;
	case DISK_LED_GREEN_SOLID:
		SetUart(SZ_CMD_LED_ESATA_ON);
		break;
	default:
		printk("%s:%d No such eSata LED operation.", __FILE__, __LINE__);
		goto End;
	}

	iRet = 0;
End:
	return iRet;
}

struct model_ops ds214play_ops = {
	.x86_init_module_type = DS214playInitModuleType,
	.x86_fan_speed_mapping = DS214playFanSpeedMapping, 
	.x86_set_esata_led_status = DS214playSetEsataLedStatus,
	.x86_cpufan_speed_mapping = NULL,
	.x86_get_buzzer_cleared = NULL,
};
