#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LEDS_LP3943_H__
#define __LEDS_LP3943_H__

enum lp3943_led_mode {
	LP3943_LED_OFF,
	LP3943_LED_ON,
	LP3943_LED_DIM0,
	LP3943_LED_DIM1,
};

enum lp3943_led_channel {
	LP3943_LED0,
	LP3943_LED1,
	LP3943_LED2,
	LP3943_LED3,
	LP3943_LED4,
	LP3943_LED5,
	LP3943_LED6,
	LP3943_LED7,
	LP3943_LED8,
	LP3943_LED9,
	LP3943_LED10,
	LP3943_LED11,
	LP3943_LED12,
	LP3943_LED13,
	LP3943_LED14,
	LP3943_LED15,
};

/*
 * struct lp3943_led_node
 * @name         : led node name which is used for led device name internally
 * @mode         : led ctrl mode - on/dim0/dim1 (Addr 06h ~ 09h)
 * @prescale     : frequency prescaler setting (Addr 02h, 04h)
 *                 only valid when lp3943_led_mode is DIM0 or DIM1
 * @channel      : led channel(s) which is(are) controlled simultaneously
 * @num_channels : numbers of led channels
 */
struct lp3943_led_node {
	char *name;
	enum lp3943_led_mode mode;
	u8 prescale;
	enum lp3943_led_channel *channel;
	int num_channels;
#ifdef MY_ABC_HERE
	char *default_trigger;
#endif /* MY_ABC_HERE */
};

/*
 * struct lp3943_platform_data
 * @node      : LED nodes description
 * @num_nodes : numbers of led nodes
 */
struct lp3943_platform_data {
	struct lp3943_led_node *node;
	int num_nodes;
};

#endif
