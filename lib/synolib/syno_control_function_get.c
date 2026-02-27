
#include <linux/of.h>
#include <linux/synolib.h>

static const char *syno_control_method_get(const int slot_type, const int slot_index)
{
	const char *control_string = NULL;
	struct device_node *device_node = NULL, *control_method = NULL;
	int dts_slot_index = 0;

	if (0 >= slot_type || 0 >= slot_index) {
		goto END;
	}
	for_each_child_of_node(of_root, device_node) {
		if (!device_node->full_name) {
			continue;
		}

		if (EUNIT_DEVICE == slot_type) {
			if (strstr(device_node->full_name, DT_ESATA_SLOT)) {
				sscanf(device_node->full_name, DT_ESATA_SLOT"@%d", &dts_slot_index);
			} else if (strstr(device_node->full_name, DT_CX4_SLOT)) {
				sscanf(device_node->full_name, DT_CX4_SLOT"@%d", &dts_slot_index);
			} else {
				continue;
			}
		} else {
			continue;
		}

		if (dts_slot_index != slot_index) {
			continue;
		}

		for_each_child_of_node(device_node, control_method) {
			if (!control_method->name || strcmp(DT_EUNIT_CONTROL_METHOD, control_method->name)) {
				continue;
			}
			if (0 > of_property_read_string(control_method, DT_EUNIT_CONTROL_TYPE, &control_string)) {
				continue;
			}
		}
	}

END:
	return control_string;
}

extern int syno_usb_acm_container_index_get_by_diskname(int *, const char *);
extern int syno_usb_acm_unique_get(const int, const int, char *, int);
extern int syno_usb_eunit_deep_sleep_indicator(const int, const int, const int);
extern int syno_usb_eunit_disk_delay_waiting(const int, const int, const int, int);
extern int syno_usb_eunit_disk_is_wait_power_on(const int, const int, const int);

struct syno_control_operations syno_control_operations_lists[] = {
	{
		.control_method = DT_USB_TO_TTY,
		.container_index_get_by_diskname = syno_usb_acm_container_index_get_by_diskname,
		.unique_get = syno_usb_acm_unique_get,
		.deep_sleep_indicator_ctrl = syno_usb_eunit_deep_sleep_indicator,
		.power_control = NULL,
		.disk_delay_waiting = syno_usb_eunit_disk_delay_waiting,
		.disk_is_wait_power_on = syno_usb_eunit_disk_is_wait_power_on,
	},
	/* add new control method here */
	{ },
};
EXPORT_SYMBOL(syno_control_operations_lists);

struct syno_control_operations *syno_control_operation_get(const int slot_type, const int slot_index)
{
	struct syno_control_operations *ctrl_op = NULL;
	const char *control_string = NULL;

	if (0 >= slot_type || 0 >= slot_index) {
		goto END;
	}

	if (NULL == (control_string = syno_control_method_get(slot_type, slot_index))) {
		goto END;
	}

	for (ctrl_op = syno_control_operations_lists; ctrl_op && strlen(ctrl_op->control_method); ctrl_op++) {
		if (0 == strcmp(ctrl_op->control_method, control_string)) {
			return ctrl_op;
		}
	}

END:
	return NULL;
}
EXPORT_SYMBOL(syno_control_operation_get);
