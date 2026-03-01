#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* Copyright (c) 2009-2020 Synology Inc. All rights reserved. */
#ifndef SYNO_GPIO_TYPE_H
#define SYNO_GPIO_TYPE_H

#include <linux/gpio.h>

#define GPIO_UNDEF 0xFF
#define SYNO_GPIO_PIN_MAX_NUM 8

#define INPUT 0
#define OUTPUT 1

#define INIT_LOW 0
#define INIT_HIGH 1
#define INIT_KEEP_VALUE 0xFF

#define ACTIVE_HIGH 0
#define ACTIVE_LOW 1
#define ACTIVE_IGNORE 0xFF

#ifdef MY_ABC_HERE
#include <linux/synolib.h>
enum SYNO_GPIO_INDEX
{
	SYNO_GPIO_PIN = 0,
	SYNO_POLARITY_PIN,

	/* Must be the last item, DO NOT append after this. */
	SYNO_GPIO_INDEX_MAX
};
#endif /* MY_ABC_HERE */

/* The following GPIO macro are 1-based */
#define HAVE_GPIO_PIN(index, type)             ((syno_gpio.type) && (0 < index) && (index <= syno_gpio.type->nr_gpio))
#define GPIO_PORT(index, type)                 syno_gpio.type->gpio_port[index-1]
#define GPIO_POLARITY(type)                    syno_gpio.type->gpio_polarity

#define HAVE_FAN_CTRL(index)                   HAVE_GPIO_PIN(index, fan_ctrl)
#define HAVE_FAN_FAIL(index)                   HAVE_GPIO_PIN(index, fan_fail)
#ifdef MY_ABC_HERE
#define HAVE_HDD_DETECT(index)                 syno_disk_gpio_pin_have(index, DT_DETECT_PIN_GPIO)
#define HAVE_HDD_ENABLE(index)                 syno_disk_gpio_pin_have(index, DT_POWER_PIN_GPIO)
/* Testify existence of led pin of "name@index" */
#define HAVE_HDD_FAIL_LED_BY_SLOT(name, index)             syno_led_pin_have(name, index, DT_HDD_ORANGE_LED)
#define HAVE_HDD_PRESENT_LED_BY_SLOT(name, index)          syno_led_pin_have(name, index, DT_HDD_GREEN_LED)
#define HAVE_HDD_ACT_LED_BY_SLOT(name, index)              syno_led_pin_have(name, index, DT_HDD_ACT_LED)
#endif /* MY_ABC_HERE */

#define HAVE_MODEL_ID(index)                   HAVE_GPIO_PIN(index, model_id)
#define HAVE_ALARM_LED()                       HAVE_GPIO_PIN(1, alarm_led)
#define HAVE_POWER_LED()                       HAVE_GPIO_PIN(1, power_led)
#define HAVE_DISK_LED_CTRL()                   HAVE_GPIO_PIN(1, disk_led_ctrl)
#define HAVE_PHY_LED_CTRL()                    HAVE_GPIO_PIN(1, phy_led_ctrl)
#define HAVE_COPY_BUTTON_DETECT()              HAVE_GPIO_PIN(1, copy_button_detect)
#define HAVE_MUTE_BUTTON_DETECT()              HAVE_GPIO_PIN(1, mute_button_detect)
#define HAVE_BUZZER_MUTE_CTRL()                HAVE_GPIO_PIN(1, buzzer_mute_ctrl)
#define HAVE_RP_DETECT(index)                  HAVE_GPIO_PIN(index, redundant_power_detect)
#define HAVE_RP_FAN_CTRL()                     HAVE_GPIO_PIN(1, redundant_power_fan_ctrl)

#define FAN_CTRL_PIN(index)                    GPIO_PORT(index, fan_ctrl)
#define FAN_FAIL_PIN(index)                    GPIO_PORT(index, fan_fail)
#ifdef MY_ABC_HERE
#define HDD_DETECT_PIN(index)                  syno_disk_gpio_pin_get(index, DT_DETECT_PIN_GPIO, SYNO_GPIO_PIN)
#define HDD_ENABLE_PIN(index)                  syno_disk_gpio_pin_get(index, DT_POWER_PIN_GPIO, SYNO_GPIO_PIN)
#define HDD_SWITCH_NO(index)                   syno_disk_gpio_pin_get(index, DT_SWITCH_NO, 0) /* only one value for switch no */
/* Get led pin# of "name@index" */
#define HDD_FAIL_LED_PIN_BY_SLOT(name, index)          syno_led_pin_get(name, index, DT_HDD_ORANGE_LED, SYNO_GPIO_PIN)
#define HDD_PRESENT_LED_PIN_BY_SLOT(name, index)       syno_led_pin_get(name, index, DT_HDD_GREEN_LED, SYNO_GPIO_PIN)
#define HDD_ACT_LED_PIN_BY_SLOT(name, index)           syno_led_pin_get(name, index, DT_HDD_ACT_LED, SYNO_GPIO_PIN)
#define HDD_FAIL_LED_NAME_BY_SLOT(name, index, led_name, length)         syno_led_name_get(name, index, DT_HDD_ORANGE_LED, led_name, length)
#define HDD_PRESENT_LED_NAME_BY_SLOT(name, index, led_name, length)      syno_led_name_get(name, index, DT_HDD_GREEN_LED, led_name, length)
#define HDD_ACT_LED_NAME_BY_SLOT(name, index, led_name, length)          syno_led_name_get(name, index, DT_HDD_ACT_LED, led_name, length)
#endif /* MY_ABC_HERE */

#define MODEL_ID_PIN(index)                    GPIO_PORT(index, model_id)
#define ALARM_LED_PIN()                        GPIO_PORT(1, alarm_led)
#define POWER_LED_PIN()                        GPIO_PORT(1, power_led)
#define DISK_LED_CTRL_PIN()                    GPIO_PORT(1, disk_led_ctrl)
#define PHY_LED_CTRL_PIN()                     GPIO_PORT(1, phy_led_ctrl)
#define COPY_BUTTON_DETECT_PIN()               GPIO_PORT(1, copy_button_detect)
#define MUTE_BUTTON_DETECT_PIN()               GPIO_PORT(1, mute_button_detect)
#define BUZZER_MUTE_CTRL_PIN()                 GPIO_PORT(1, buzzer_mute_ctrl)
#define RP_DETECT_PIN(index)                   GPIO_PORT(index, redundant_power_detect)
#define RP_FAN_CTRL_PIN()                      GPIO_PORT(1, redundant_power_fan_ctrl)

#define FAN_CTRL_POLARITY()                    GPIO_POLARITY(fan_ctrl)
#define FAN_FAIL_POLARITY()                    GPIO_POLARITY(fan_fail)
#ifdef MY_ABC_HERE
#define HDD_DETECT_POLARITY(index)             syno_disk_gpio_pin_get(index, DT_DETECT_PIN_GPIO, SYNO_POLARITY_PIN)
#define HDD_ENABLE_POLARITY(index)             syno_disk_gpio_pin_get(index, DT_POWER_PIN_GPIO, SYNO_POLARITY_PIN)
/* Get led pin polarity of "name@index" */
#define HDD_FAIL_LED_POLARITY_BY_SLOT(name, index)     syno_led_pin_get(name, index, DT_HDD_ORANGE_LED, SYNO_POLARITY_PIN)
#define HDD_PRESENT_LED_POLARITY_BY_SLOT(name, index)  syno_led_pin_get(name, index, DT_HDD_GREEN_LED, SYNO_POLARITY_PIN)
#define HDD_ACT_LED_POLARITY_BY_SLOT(name, index)      syno_led_pin_get(name, index, DT_HDD_ACT_LED, SYNO_POLARITY_PIN)
#endif /* MY_ABC_HERE */
#define MODEL_ID_POLARITY()                    GPIO_POLARITY(model_id)
#define ALARM_LED_POLARITY()                   GPIO_POLARITY(alarm_led)
#define POWER_LED_POLARITY()                   GPIO_POLARITY(power_led)
#define DISK_LED_CTRL_POLARITY()               GPIO_POLARITY(disk_led_ctrl)
#define PHY_LED_CTRL_POLARITY()                GPIO_POLARITY(phy_led_ctrl)
#define COPY_BUTTON_DETECT_POLARITY()          GPIO_POLARITY(copy_button_detect)
#define MUTE_BUTTON_DETECT_POLARITY()          GPIO_POLARITY(mute_button_detect)
#define BUZZER_MUTE_CTRL_POLARITY()            GPIO_POLARITY(buzzer_mute_ctrl)
#define RP_DETECT_POLARITY()                   GPIO_POLARITY(redundant_power_detect)
#define RP_FAN_CTRL_POLARITY()                 GPIO_POLARITY(redundant_power_fan_ctrl)

typedef struct _tag_SYNO_GPIO_INFO {
	const char *name;
	u8 nr_gpio;
	u8 gpio_port[SYNO_GPIO_PIN_MAX_NUM];
	u8 gpio_direction;
	u8 gpio_init_value;
	u8 gpio_polarity;
} SYNO_GPIO_INFO;

typedef struct __tag_SYNO_GPIO {
	SYNO_GPIO_INFO *fan_ctrl;
	SYNO_GPIO_INFO *fan_fail;
	SYNO_GPIO_INFO *hdd_fail_led;
	SYNO_GPIO_INFO *hdd_present_led;
	SYNO_GPIO_INFO *hdd_act_led;
	SYNO_GPIO_INFO *hdd_detect;
	SYNO_GPIO_INFO *hdd_enable;
	SYNO_GPIO_INFO *model_id;
	SYNO_GPIO_INFO *alarm_led;
	SYNO_GPIO_INFO *power_led;
	SYNO_GPIO_INFO *disk_led_ctrl; // control all disk led on/off
	SYNO_GPIO_INFO *phy_led_ctrl;  // control all phy led on/off
	SYNO_GPIO_INFO *copy_button_detect;
	SYNO_GPIO_INFO *mute_button_detect;
	SYNO_GPIO_INFO *buzzer_mute_ctrl;
	SYNO_GPIO_INFO *redundant_power_detect;
	SYNO_GPIO_INFO *redundant_power_fan_ctrl;
} SYNO_GPIO;

#ifdef MY_ABC_HERE
u32 syno_disk_gpio_pin_get(const int diskPort, const char *szPropertyName, const int propertyIndex);
int syno_disk_gpio_pin_have(const int diskPort, const char *szPropertyName);
u32 syno_led_pin_get(const char* name, const int diskPort, const char *szLedName, const int propertyIndex);
int syno_led_pin_have(const char* name, const int diskPort, const char *szLedName);
int  syno_led_type_get(const char* szSlotName, const int diskPort, char *szSynoLedType, unsigned int cbSynoLedType);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
extern void syno_gpio_direction_output(int pin, int pValue);
extern void syno_gpio_direction_input(int pin);
extern int syno_gpio_to_irq(int pin);
extern int SYNO_GPIO_READ(int pin);
extern void SYNO_GPIO_WRITE(int pin, int pValue);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
extern void DBG_SpinupGroupListGpio(void);
extern int SynoHaveRPDetectPin(void);
extern int SynoAllRedundantPowerDetected(void);
#endif /* MY_ABC_HERE */

#endif /* SYNO_GPIO_TYPE_H */
