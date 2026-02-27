// Copyright (c) 2000-2024 Synology Inc. All rights reserved.

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/synolib.h>


DEFINE_MUTEX(syno_usb_eunit_check_mutex);
LIST_HEAD(syno_usb_eunit_not_ready_list);

void syno_usb_eunit_not_ready_set(const char *device_name)
{
	struct syno_device_list *sdl = NULL;
	int device_in_list = false;

	if (!device_name) {
		return;
	}
	mutex_lock(&syno_usb_eunit_check_mutex);
	list_for_each_entry(sdl, &syno_usb_eunit_not_ready_list, device_list) {
		if (!strcmp(sdl->disk_name, device_name)) {
			device_in_list = true;
			break;
		}
	}

	if (!device_in_list) {
		sdl = kzalloc(sizeof(*sdl), GFP_KERNEL);
		snprintf(sdl->disk_name, DISK_NAME_LEN, "%s", device_name);
		list_add(&sdl->device_list, &syno_usb_eunit_not_ready_list);
	}
	mutex_unlock(&syno_usb_eunit_check_mutex);
}
EXPORT_SYMBOL(syno_usb_eunit_not_ready_set);
void syno_usb_eunit_not_ready_clear(const char *device_name)
{
	struct syno_device_list *sdl = NULL;

	if (!device_name) {
		return;
	}
	mutex_lock(&syno_usb_eunit_check_mutex);
	list_for_each_entry(sdl, &syno_usb_eunit_not_ready_list, device_list) {
		if (!strcmp(sdl->disk_name, device_name)) {
			list_del(&sdl->device_list);
			kfree(sdl);
			break;
		}
	}
	mutex_unlock(&syno_usb_eunit_check_mutex);
}
EXPORT_SYMBOL(syno_usb_eunit_not_ready_clear);

/*
 * Return 0 if any of usb eunits aren't ready and timeout isn't over.
 * Otherwise return 1.
 */
int syno_usb_eunit_ready_check(void)
{
	int ret = 0;

	mutex_lock(&syno_usb_eunit_check_mutex);
	if (list_empty(&syno_usb_eunit_not_ready_list)) {
		ret = 1;
	}
	mutex_unlock(&syno_usb_eunit_check_mutex);

	return ret;
}