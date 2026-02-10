#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2014 Synology Inc. All rights reserved.
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/synolib.h>
#include <linux/device.h>
#include <linux/slab.h>

#ifdef MY_DEF_HERE
#include <linux/of.h>
#include <linux/syno_fdt.h>
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE

typedef struct _tag_SYNO_LED_TRIGGER_TIMER {
	struct timer_list Timer;
	int DiskActivity;
	int DiskLastActivity;
	int DiskFaulty;
	unsigned long LedNum;
} SYNO_LED_TRIGGER_TIMER;

static SYNO_LED_TRIGGER_TIMER *syno_led_trigger_timer = NULL;
static struct led_trigger *syno_led_ledtrig = NULL;
char **syno_led_trigger_name;
EXPORT_SYMBOL(syno_led_trigger_name);

static int num_of_led_trigger = 0;

#ifdef MY_DEF_HERE
#define SYNO_MAX_LED 255
#else /* MY_DEF_HERE */
#define SYNO_MAX_LED 16
#endif /* MY_DEF_HERE */

int *gpGreenLedMap, *gpOrangeLedMap = NULL; //mapping disk index to disk led; must be initialized before used
EXPORT_SYMBOL(gpGreenLedMap);
EXPORT_SYMBOL(gpOrangeLedMap);

void syno_ledtrig_set(int iLedNum, enum led_brightness brightness)
{
	if(0 > iLedNum || num_of_led_trigger <= iLedNum || NULL == syno_led_ledtrig){
		return;
	}

	led_trigger_event(&syno_led_ledtrig[iLedNum], brightness);
}
EXPORT_SYMBOL(syno_ledtrig_set);

void syno_ledtrig_active_set(int iLedNum)
{
	SYNO_LED_TRIGGER_TIMER *pTriggerTimer = NULL;

	if(0 > iLedNum || num_of_led_trigger <= iLedNum || NULL == syno_led_ledtrig || NULL == syno_led_trigger_timer) {
		goto END;
	}

	pTriggerTimer = &syno_led_trigger_timer[iLedNum];
	if (1 == pTriggerTimer->DiskFaulty){
		goto END;
	}

	pTriggerTimer->DiskActivity++;
	if (!timer_pending(&pTriggerTimer->Timer)){
		mod_timer(&pTriggerTimer->Timer, jiffies + msecs_to_jiffies(100));
	}

END:
	return;

}
EXPORT_SYMBOL(syno_ledtrig_active_set);

void syno_ledtrig_faulty_set(int iLedNum, int iFaulty)
{
	SYNO_LED_TRIGGER_TIMER *pTriggerTimer = NULL;

	if(0 > iLedNum || num_of_led_trigger <= iLedNum || 0 > iFaulty || NULL == syno_led_trigger_timer) {
		return;
	}

	pTriggerTimer = &syno_led_trigger_timer[iLedNum];
	pTriggerTimer->DiskFaulty = iFaulty;
}
EXPORT_SYMBOL(syno_ledtrig_faulty_set);

static void syno_active_ledtrig_timerfunc(struct timer_list *t)
{
	SYNO_LED_TRIGGER_TIMER *pTriggerTimer = from_timer(pTriggerTimer, t, Timer);
	unsigned long iLedNum = pTriggerTimer->LedNum;

	if (pTriggerTimer->DiskLastActivity != pTriggerTimer->DiskActivity) {
		pTriggerTimer->DiskLastActivity = pTriggerTimer->DiskActivity;
		led_trigger_event(&syno_led_ledtrig[iLedNum], LED_HALF);
		mod_timer(&pTriggerTimer->Timer, jiffies + msecs_to_jiffies(150));
	}else if( 1 == pTriggerTimer->DiskFaulty){
		led_trigger_event(&syno_led_ledtrig[iLedNum], LED_OFF);
	}else{
		led_trigger_event(&syno_led_ledtrig[iLedNum], LED_FULL);
	}
}

static int __init syno_ledtrig_init(void)
{
	int iTriggerNum = 0;
	int err = 0;
	SYNO_LED_TRIGGER_TIMER *pTriggerTimer = NULL;

#ifdef MY_DEF_HERE
	if (of_root) {
		if (of_find_property(of_root, "number_of_led_trigger", NULL)) {
			of_property_read_u32_index(of_root, "number_of_led_trigger", 0, &num_of_led_trigger);
		}
	}
#endif /* MY_DEF_HERE */

	if (0 == num_of_led_trigger) {
		num_of_led_trigger = SYNO_MAX_LED;
	}

	syno_led_trigger_name = (char **)kmalloc(num_of_led_trigger * sizeof (char* ), GFP_KERNEL);
	syno_led_ledtrig = (struct led_trigger*)kmalloc(num_of_led_trigger * sizeof(struct led_trigger), GFP_KERNEL);
	syno_led_trigger_timer = (SYNO_LED_TRIGGER_TIMER*)kmalloc(num_of_led_trigger * sizeof(SYNO_LED_TRIGGER_TIMER), GFP_KERNEL);

	if(NULL == syno_led_trigger_name || NULL == syno_led_ledtrig || NULL == syno_led_trigger_timer) {
		printk("fail to allocate memory for led triggers \n");
		goto END;
	}
	memset(syno_led_trigger_name, 0, num_of_led_trigger * sizeof(char*));
	memset(syno_led_ledtrig, 0, num_of_led_trigger * sizeof(struct led_trigger));
	memset(syno_led_trigger_timer, 0, num_of_led_trigger * sizeof(SYNO_LED_TRIGGER_TIMER));

	/*register all triggers used in DSM*/
	for(iTriggerNum = 0 ; iTriggerNum < num_of_led_trigger ; iTriggerNum++){
		syno_led_trigger_name[iTriggerNum] = (char*)kmalloc(64 * sizeof (char), GFP_KERNEL);
		snprintf(syno_led_trigger_name[iTriggerNum], 64, "syno_led%d_ledtrig", iTriggerNum);
		syno_led_ledtrig[iTriggerNum].name = syno_led_trigger_name[iTriggerNum];

		err = led_trigger_register(&syno_led_ledtrig[iTriggerNum]);
		if (0 != err ){
			printk("fail to regist tirgger Num %d \n", iTriggerNum);
			break;
		}
		pTriggerTimer = &syno_led_trigger_timer[iTriggerNum];
		pTriggerTimer->DiskFaulty = 0;
		pTriggerTimer->LedNum = iTriggerNum;
		timer_setup(&pTriggerTimer->Timer, syno_active_ledtrig_timerfunc, 0);
	}

END:
	return err;
}
module_init(syno_ledtrig_init);

static void __exit syno_ledtrig_exit(void)
{
	int iTriggerNum = 0;

	/*unregister triggers*/
	for(iTriggerNum = 0 ; iTriggerNum < num_of_led_trigger ; iTriggerNum++){
		led_trigger_unregister_simple(&syno_led_ledtrig[iTriggerNum]);
	}
}
module_exit(syno_ledtrig_exit);

#endif // MY_ABC_HERE
