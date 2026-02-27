#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0+
/*
 * cdc-acm.c
 *
 * Copyright (c) 1999 Armin Fuerst	<fuerst@in.tum.de>
 * Copyright (c) 1999 Pavel Machek	<pavel@ucw.cz>
 * Copyright (c) 1999 Johannes Erdfelt	<johannes@erdfelt.com>
 * Copyright (c) 2000 Vojtech Pavlik	<vojtech@suse.cz>
 * Copyright (c) 2004 Oliver Neukum	<oliver@neukum.name>
 * Copyright (c) 2005 David Kubicek	<dave@awk.cz>
 * Copyright (c) 2011 Johan Hovold	<jhovold@gmail.com>
 *
 * USB Abstract Control Model driver for USB modems and ISDN adapters
 *
 * Sponsored by SuSE
 */

#undef DEBUG
#undef VERBOSE_DEBUG

#ifdef MY_DEF_HERE
//TODO:check headers dependency, move the segment to below
#include <linux/libata.h>
#include <scsi/scsi_device.h>
#include <linux/synolib.h>
#include <linux/synobios.h>
#include <linux/string.h>
#define SYNO_EUNIT_ACM_WAITING_TRANSMIT_RETRY 5
#define SYNO_EUNIT_ACM_WAITING_TRANSMIT_DELAY_MS 100
#define SYNO_EUNIT_ACM_WAITING_RESPONSE_RETRY 100
#define SYNO_EUNIT_ACM_WAITING_RESPONSE_DELAY_MS 1000
#define SYNO_EUNIT_STATUS_REPORT_DELIM ','
#define SYNO_EUNIT_COMMAND_DELIM '/'
#define SYNO_EUNIT_STATUS_BUFFER_SIZE 1024
#define SYNO_EUNIT_STATUS_ERROR 'E'
#define SYNO_EUNIT_DEFAULT_UNIQUE_ID "SynoEunit"

struct acm_device_temp {
	char disk_name[DISK_NAME_LEN];
	char usb_path[SYNO_DTS_PROPERTY_CONTENT_LENGTH];
	struct list_head device_list;
};

static LIST_HEAD(acm_temp_device_list);

#include <linux/pci.h>
extern struct klist syno_ata_port_head;
extern int syno_pciepath_dts_pattern_get(struct pci_dev *pdev, char *szPciePath, const int size);
extern void device_pm_move_to_tail(struct device *dev);

#endif /* MY_DEF_HERE */
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/log2.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/idr.h>
#include <linux/list.h>

#include "cdc-acm.h"

#ifdef MY_DEF_HERE
#define CREATE_TRACE_POINTS
#include "cdc-acm-trace.h"
#endif /* MY_DEF_HERE */

#define DRIVER_AUTHOR "Armin Fuerst, Pavel Machek, Johannes Erdfelt, Vojtech Pavlik, David Kubicek, Johan Hovold"
#define DRIVER_DESC "USB Abstract Control Model driver for USB modems and ISDN adapters"

static struct usb_driver acm_driver;
static struct tty_driver *acm_tty_driver;

static DEFINE_IDR(acm_minors);
static DEFINE_MUTEX(acm_minors_lock);

static void acm_tty_set_termios(struct tty_struct *tty,
				struct ktermios *termios_old);

#ifdef MY_DEF_HERE
static DEFINE_SPINLOCK(acm_list_lock);
struct syno_acm_list {
	struct acm* acm;
	struct list_head device_list;
};
static LIST_HEAD(syno_acm_list_head);

static bool syno_is_synology_acm(struct acm *acm)
{
	if (acm && 0 == strncmp(interface_to_usbdev(acm->control)->product, SYNO_EUNIT_NAME_HEAD, strlen(SYNO_EUNIT_NAME_HEAD))) {
		return true;
	}
	return false;
}
#endif /* MY_DEF_HERE */

/*
 * acm_minors accessors
 */

/*
 * Look up an ACM structure by minor. If found and not disconnected, increment
 * its refcount and return it with its mutex held.
 */
static struct acm *acm_get_by_minor(unsigned int minor)
{
	struct acm *acm;

	mutex_lock(&acm_minors_lock);
	acm = idr_find(&acm_minors, minor);
	if (acm) {
		mutex_lock(&acm->mutex);
		if (acm->disconnected) {
			mutex_unlock(&acm->mutex);
			acm = NULL;
		} else {
			tty_port_get(&acm->port);
			mutex_unlock(&acm->mutex);
		}
	}
	mutex_unlock(&acm_minors_lock);
	return acm;
}

/*
 * Try to find an available minor number and if found, associate it with 'acm'.
 */
static int acm_alloc_minor(struct acm *acm)
{
	int minor;

	mutex_lock(&acm_minors_lock);
	minor = idr_alloc(&acm_minors, acm, 0, ACM_TTY_MINORS, GFP_KERNEL);
	mutex_unlock(&acm_minors_lock);

	return minor;
}

/* Release the minor number associated with 'acm'.  */
static void acm_release_minor(struct acm *acm)
{
	mutex_lock(&acm_minors_lock);
	idr_remove(&acm_minors, acm->minor);
	mutex_unlock(&acm_minors_lock);
}

/*
 * Functions for ACM control messages.
 */

static int acm_ctrl_msg(struct acm *acm, int request, int value,
							void *buf, int len)
{
	int retval;

	retval = usb_autopm_get_interface(acm->control);
	if (retval)
		return retval;

	retval = usb_control_msg(acm->dev, usb_sndctrlpipe(acm->dev, 0),
		request, USB_RT_ACM, value,
		acm->control->altsetting[0].desc.bInterfaceNumber,
		buf, len, 5000);

	dev_dbg(&acm->control->dev,
		"%s - rq 0x%02x, val %#x, len %#x, result %d\n",
		__func__, request, value, len, retval);

	usb_autopm_put_interface(acm->control);

	return retval < 0 ? retval : 0;
}

/* devices aren't required to support these requests.
 * the cdc acm descriptor tells whether they do...
 */
static inline int acm_set_control(struct acm *acm, int control)
{
	if (acm->quirks & QUIRK_CONTROL_LINE_STATE)
		return -EOPNOTSUPP;

	return acm_ctrl_msg(acm, USB_CDC_REQ_SET_CONTROL_LINE_STATE,
			control, NULL, 0);
}

#define acm_set_line(acm, line) \
	acm_ctrl_msg(acm, USB_CDC_REQ_SET_LINE_CODING, 0, line, sizeof *(line))
#define acm_send_break(acm, ms) \
	acm_ctrl_msg(acm, USB_CDC_REQ_SEND_BREAK, ms, NULL, 0)

static void acm_poison_urbs(struct acm *acm)
{
	int i;

	usb_poison_urb(acm->ctrlurb);
	for (i = 0; i < ACM_NW; i++)
		usb_poison_urb(acm->wb[i].urb);
	for (i = 0; i < acm->rx_buflimit; i++)
		usb_poison_urb(acm->read_urbs[i]);
}

static void acm_unpoison_urbs(struct acm *acm)
{
	int i;

	for (i = 0; i < acm->rx_buflimit; i++)
		usb_unpoison_urb(acm->read_urbs[i]);
	for (i = 0; i < ACM_NW; i++)
		usb_unpoison_urb(acm->wb[i].urb);
	usb_unpoison_urb(acm->ctrlurb);
}


/*
 * Write buffer management.
 * All of these assume proper locks taken by the caller.
 */

static int acm_wb_alloc(struct acm *acm)
{
	int i, wbn;
	struct acm_wb *wb;

	wbn = 0;
	i = 0;
	for (;;) {
		wb = &acm->wb[wbn];
		if (!wb->use) {
			wb->use = true;
			wb->len = 0;
			return wbn;
		}
		wbn = (wbn + 1) % ACM_NW;
		if (++i >= ACM_NW)
			return -1;
	}
}

static int acm_wb_is_avail(struct acm *acm)
{
	int i, n;
	unsigned long flags;

	n = ACM_NW;
	spin_lock_irqsave(&acm->write_lock, flags);
	for (i = 0; i < ACM_NW; i++)
		if(acm->wb[i].use)
			n--;
	spin_unlock_irqrestore(&acm->write_lock, flags);
	return n;
}

/*
 * Finish write. Caller must hold acm->write_lock
 */
static void acm_write_done(struct acm *acm, struct acm_wb *wb)
{
	wb->use = false;
	acm->transmitting--;
	usb_autopm_put_interface_async(acm->control);
}

/*
 * Poke write.
 *
 * the caller is responsible for locking
 */

static int acm_start_wb(struct acm *acm, struct acm_wb *wb)
{
	int rc;

	acm->transmitting++;

	wb->urb->transfer_buffer = wb->buf;
	wb->urb->transfer_dma = wb->dmah;
	wb->urb->transfer_buffer_length = wb->len;
	wb->urb->dev = acm->dev;

	rc = usb_submit_urb(wb->urb, GFP_ATOMIC);
	if (rc < 0) {
		if (rc != -EPERM)
			dev_err(&acm->data->dev,
				"%s - usb_submit_urb(write bulk) failed: %d\n",
				__func__, rc);
		acm_write_done(acm, wb);
	}
	return rc;
}

/*
 * attributes exported through sysfs
 */
static ssize_t bmCapabilities_show
(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct acm *acm = usb_get_intfdata(intf);

	return sprintf(buf, "%d", acm->ctrl_caps);
}
static DEVICE_ATTR_RO(bmCapabilities);

static ssize_t wCountryCodes_show
(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct acm *acm = usb_get_intfdata(intf);

	memcpy(buf, acm->country_codes, acm->country_code_size);
	return acm->country_code_size;
}

static DEVICE_ATTR_RO(wCountryCodes);

static ssize_t iCountryCodeRelDate_show
(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct acm *acm = usb_get_intfdata(intf);

	return sprintf(buf, "%d", acm->country_rel_date);
}

static DEVICE_ATTR_RO(iCountryCodeRelDate);
/*
 * Interrupt handlers for various ACM device responses
 */

static void acm_process_notification(struct acm *acm, unsigned char *buf)
{
	int newctrl;
	int difference;
	unsigned long flags;
	struct usb_cdc_notification *dr = (struct usb_cdc_notification *)buf;
	unsigned char *data = buf + sizeof(struct usb_cdc_notification);

	switch (dr->bNotificationType) {
	case USB_CDC_NOTIFY_NETWORK_CONNECTION:
		dev_dbg(&acm->control->dev,
			"%s - network connection: %d\n", __func__, dr->wValue);
		break;

	case USB_CDC_NOTIFY_SERIAL_STATE:
		if (le16_to_cpu(dr->wLength) != 2) {
			dev_dbg(&acm->control->dev,
				"%s - malformed serial state\n", __func__);
			break;
		}

		newctrl = get_unaligned_le16(data);
		dev_dbg(&acm->control->dev,
			"%s - serial state: 0x%x\n", __func__, newctrl);

		if (!acm->clocal && (acm->ctrlin & ~newctrl & ACM_CTRL_DCD)) {
			dev_dbg(&acm->control->dev,
				"%s - calling hangup\n", __func__);
			tty_port_tty_hangup(&acm->port, false);
		}

		difference = acm->ctrlin ^ newctrl;
		spin_lock_irqsave(&acm->read_lock, flags);
		acm->ctrlin = newctrl;
		acm->oldcount = acm->iocount;

		if (difference & ACM_CTRL_DSR)
			acm->iocount.dsr++;
		if (difference & ACM_CTRL_DCD)
			acm->iocount.dcd++;
		if (newctrl & ACM_CTRL_BRK) {
			acm->iocount.brk++;
			tty_insert_flip_char(&acm->port, 0, TTY_BREAK);
		}
		if (newctrl & ACM_CTRL_RI)
			acm->iocount.rng++;
		if (newctrl & ACM_CTRL_FRAMING)
			acm->iocount.frame++;
		if (newctrl & ACM_CTRL_PARITY)
			acm->iocount.parity++;
		if (newctrl & ACM_CTRL_OVERRUN)
			acm->iocount.overrun++;
		spin_unlock_irqrestore(&acm->read_lock, flags);

		if (difference)
			wake_up_all(&acm->wioctl);

		break;

	default:
		dev_dbg(&acm->control->dev,
			"%s - unknown notification %d received: index %d len %d\n",
			__func__,
			dr->bNotificationType, dr->wIndex, dr->wLength);
	}
}

/* control interface reports status changes with "interrupt" transfers */
static void acm_ctrl_irq(struct urb *urb)
{
	struct acm *acm = urb->context;
	struct usb_cdc_notification *dr = urb->transfer_buffer;
	unsigned int current_size = urb->actual_length;
	unsigned int expected_size, copy_size, alloc_size;
	int retval;
	int status = urb->status;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&acm->control->dev,
			"%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(&acm->control->dev,
			"%s - nonzero urb status received: %d\n",
			__func__, status);
		goto exit;
	}

	usb_mark_last_busy(acm->dev);

	if (acm->nb_index)
		dr = (struct usb_cdc_notification *)acm->notification_buffer;

	/* size = notification-header + (optional) data */
	expected_size = sizeof(struct usb_cdc_notification) +
					le16_to_cpu(dr->wLength);

	if (current_size < expected_size) {
		/* notification is transmitted fragmented, reassemble */
		if (acm->nb_size < expected_size) {
			u8 *new_buffer;
			alloc_size = roundup_pow_of_two(expected_size);
			/* Final freeing is done on disconnect. */
			new_buffer = krealloc(acm->notification_buffer,
					      alloc_size, GFP_ATOMIC);
			if (!new_buffer) {
				acm->nb_index = 0;
				goto exit;
			}

			acm->notification_buffer = new_buffer;
			acm->nb_size = alloc_size;
			dr = (struct usb_cdc_notification *)acm->notification_buffer;
		}

		copy_size = min(current_size,
				expected_size - acm->nb_index);

		memcpy(&acm->notification_buffer[acm->nb_index],
		       urb->transfer_buffer, copy_size);
		acm->nb_index += copy_size;
		current_size = acm->nb_index;
	}

	if (current_size >= expected_size) {
		/* notification complete */
		acm_process_notification(acm, (unsigned char *)dr);
		acm->nb_index = 0;
	}

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval && retval != -EPERM && retval != -ENODEV)
		dev_err(&acm->control->dev,
			"%s - usb_submit_urb failed: %d\n", __func__, retval);
	else
		dev_vdbg(&acm->control->dev,
			"control resubmission terminated %d\n", retval);
}

static int acm_submit_read_urb(struct acm *acm, int index, gfp_t mem_flags)
{
	int res;

	if (!test_and_clear_bit(index, &acm->read_urbs_free))
		return 0;

	res = usb_submit_urb(acm->read_urbs[index], mem_flags);
	if (res) {
		if (res != -EPERM && res != -ENODEV) {
			dev_err(&acm->data->dev,
				"urb %d failed submission with %d\n",
				index, res);
		} else {
			dev_vdbg(&acm->data->dev, "intended failure %d\n", res);
		}
		set_bit(index, &acm->read_urbs_free);
		return res;
	} else {
		dev_vdbg(&acm->data->dev, "submitted urb %d\n", index);
	}

	return 0;
}

static int acm_submit_read_urbs(struct acm *acm, gfp_t mem_flags)
{
	int res;
	int i;

	for (i = 0; i < acm->rx_buflimit; ++i) {
		res = acm_submit_read_urb(acm, i, mem_flags);
		if (res)
			return res;
	}

	return 0;
}

static void acm_process_read_urb(struct acm *acm, struct urb *urb)
{
	if (!urb->actual_length)
		return;

	tty_insert_flip_string(&acm->port, urb->transfer_buffer,
			urb->actual_length);
	tty_flip_buffer_push(&acm->port);
}

#ifdef MY_DEF_HERE
static const char * const syno_usb_eunit_names[] = {
	[EUNIT_STATUS_EXPCTRL] = DT_EUNIT_STATUS_EXPCTRL,
	[EUNIT_STATUS_FANPWM] = DT_EUNIT_STATUS_FANPWM,
	[EUNIT_STATUS_FANSPEED] = DT_EUNIT_STATUS_FANSPEED,
	[EUNIT_STATUS_HDDCTRL] =DT_EUNIT_STATUS_HDDCTRL,
	[EUNIT_STATUS_DISKLED] = DT_EUNIT_STATUS_DISKLED,
	[EUNIT_STATUS_7SEGLED] = DT_EUNIT_STATUS_7SEGLED,
	[EUNIT_STATUS_EXPSNSET] = DT_EUNIT_STATUS_EXPSNSET,
	[EUNIT_STATUS_EXPIDSET] = DT_EUNIT_STATUS_EXPIDSET,
	[EUNIT_STATUS_UPVERSION] = DT_EUNIT_STATUS_UPVERSION,
	[EUNIT_STATUS_HDDPRESENT] = DT_EUNIT_STATUS_HDDPRESENT,
	[EUNIT_STATUS_HDDENABLE] = DT_EUNIT_STATUS_HDDENABLE,
	[EUNIT_STATUS_MONCURRENT] = DT_EUNIT_STATUS_MONCURRENT,
	[EUNIT_STATUS_MONVOLTAGE] = DT_EUNIT_STATUS_MONVOLTAGE,
	[EUNIT_STATUS_MONTHERMAL] = DT_EUNIT_STATUS_MONTHERMAL,
	[EUNIT_STATUS_POWERMODULE] = DT_EUNIT_STATUS_POWERMODULE,
	[EUNIT_STATUS_EXPBPSNSET] = DT_EUNIT_STATUS_EXPBPSNSET,
	[EUNIT_STATUS_HDDSEDSET] = DT_EUNIT_STATUS_HDDSEDSET,
	[EUNIT_STATUS_ACK] = DT_EUNIT_STATUS_ACK,
	[EUNIT_STATUS_DIMMER] = DT_EUNIT_STATUS_DIMMER,
	[EUNIT_STATUS_UNKNOWN] = "Unknown"
};

static int parsing_key(char *key)
{
	int i = 0;

	if (NULL == key) {
		return EUNIT_STATUS_UNKNOWN;
	}

	for (i = 0; i < ARRAY_SIZE(syno_usb_eunit_names); i++) {
		const char *pszEunitName = NULL;

		if (NULL == (pszEunitName = syno_usb_eunit_names[i])){
			continue;
		}

		if (!strcmp(pszEunitName, key)) {
			return i;
		}
	}

	return EUNIT_STATUS_UNKNOWN;
}

static bool syno_eunit_resched_queue_work(struct acm *acm, int ackId)
{
	bool bRet = false;

	if (!acm) {
		goto END;
	}

	/* issue_work is on */
	if (!delayed_work_pending(&acm->cmd_issue_work)) {
		goto END;
	}

	if ((ackId % SYNO_EUNIT_QUEUE_SIZE) + 1 != acm->cmd_idx_tail) {
		goto END;
	}


	bRet = true;
END:
	return bRet;
}

static void syno_eunit_status_callback(struct acm *acm, EUNIT_STATUS_INDEX key, const char *sepptr)
{
	int ackId = 0;
	unsigned long flags;

	if (!acm || !sepptr)
		goto END;

	switch (key) {

		case EUNIT_STATUS_ACK:
			if (kstrtoint(sepptr, 10, &ackId)) {
				goto END;
			}

			trace_syno_eunit_ack_status(acm, ackId);

			spin_lock_irqsave(&acm->syno_acm_q_lock, flags); /* Lock */
			if (acm->syno_acm_ack_to_cmpl[ackId-1]) {
				complete_all(acm->syno_acm_ack_to_cmpl[ackId-1]);
				acm->syno_acm_ack_to_cmpl[ackId-1] = NULL;
			}
			spin_unlock_irqrestore(&acm->syno_acm_q_lock, flags); /* Unlock */

			if (syno_eunit_resched_queue_work(acm, ackId)) {
				mod_delayed_work(system_wq, &acm->cmd_issue_work, 0);
			}
			break;

		default:
			break;
	}
END:
	return;
}

static void parse_single_status(char *input, struct acm *acm)
{
	char *sepptr = input;
	EUNIT_STATUS_INDEX key = EUNIT_STATUS_UNKNOWN;

	if (NULL == input || NULL == acm) {
		goto END;
	}

	sepptr = strchr(input, ':');
	if (!sepptr) {
		goto END;
	}
	*sepptr = 0;
	sepptr++;
	key = parsing_key(input);
	snprintf(acm->cached_expstatus[key], SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s", sepptr);

	/* Additional operations */
	syno_eunit_status_callback(acm, key, sepptr);

END:
	return;
}

// must get acm->status_lock
static void parsing_input(char *input, struct acm *acm)
{
	char *status = input;
	char *commaptr = NULL;
	int input_len = 0;

	if (NULL == input || NULL == acm) {
		return;
	}
	input_len = strlen(input);

	while(commaptr <= input + input_len) {
		commaptr = strchr(status, SYNO_EUNIT_STATUS_REPORT_DELIM);
		if (commaptr) {
			*commaptr = 0;
		}
		parse_single_status(status, acm);
		if (!commaptr) {
			break;
		}
		status = commaptr + 1;
	}

	return;
}

static void syno_expstatus_parsing(struct acm *acm)
{
	char acm_buffer[SYNO_EUNIT_STATUS_BUFFER_SIZE] = {0};

	if (!acm || acm->disconnected) {
		return;
	}

	if (strchr(acm->acm_buffer, SYNO_EUNIT_STATUS_REPORT_DELIM)) {
		snprintf(acm_buffer, SYNO_EUNIT_STATUS_BUFFER_SIZE, "%s", acm->acm_buffer);
		memset(acm->acm_buffer, 0, SYNO_EUNIT_STATUS_BUFFER_SIZE);
		snprintf(acm->acm_buffer, SYNO_EUNIT_STATUS_BUFFER_SIZE, "%s", strrchr(acm_buffer, SYNO_EUNIT_STATUS_REPORT_DELIM)+1);
		*strrchr(acm_buffer, SYNO_EUNIT_STATUS_REPORT_DELIM) = '\0';
		parsing_input(acm_buffer, acm);
	}

	return;
}
#endif /* MY_DEF_HERE */

static void acm_read_bulk_callback(struct urb *urb)
{
	struct acm_rb *rb = urb->context;
	struct acm *acm = rb->instance;
	int status = urb->status;
	bool stopped = false;
	bool stalled = false;
	bool cooldown = false;
#ifdef MY_DEF_HERE
	unsigned long flags;
#endif /* MY_DEF_HERE */

	dev_vdbg(&acm->data->dev, "%s - urb %d, len %d\n", __func__,
					rb->index, urb->actual_length);

	if (!acm->dev) {
		set_bit(rb->index, &acm->read_urbs_free);
		dev_dbg(&acm->data->dev, "%s - disconnected\n", __func__);
		return;
	}

#ifdef MY_DEF_HERE
	if (acm->acm_buffer && urb->actual_length) {
		write_lock_irqsave(&acm->status_lock, flags);
		strncat(acm->acm_buffer, urb->transfer_buffer,
				(SYNO_EUNIT_STATUS_BUFFER_SIZE > strlen(acm->acm_buffer) + urb->actual_length)?
				urb->actual_length : SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(acm->acm_buffer) - 1);
		syno_expstatus_parsing(acm);
		write_unlock_irqrestore(&acm->status_lock, flags);
	}
#endif /* MY_DEF_HERE */

	switch (status) {
	case 0:
		usb_mark_last_busy(acm->dev);
		acm_process_read_urb(acm, urb);
		break;
	case -EPIPE:
		set_bit(EVENT_RX_STALL, &acm->flags);
		stalled = true;
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&acm->data->dev,
			"%s - urb shutting down with status: %d\n",
			__func__, status);
		stopped = true;
		break;
	case -EOVERFLOW:
	case -EPROTO:
		dev_dbg(&acm->data->dev,
			"%s - cooling babbling device\n", __func__);
		usb_mark_last_busy(acm->dev);
		set_bit(rb->index, &acm->urbs_in_error_delay);
		set_bit(ACM_ERROR_DELAY, &acm->flags);
		cooldown = true;
		break;
	default:
		dev_dbg(&acm->data->dev,
			"%s - nonzero urb status received: %d\n",
			__func__, status);
		break;
	}

	/*
	 * Make sure URB processing is done before marking as free to avoid
	 * racing with unthrottle() on another CPU. Matches the barriers
	 * implied by the test_and_clear_bit() in acm_submit_read_urb().
	 */
	smp_mb__before_atomic();
	set_bit(rb->index, &acm->read_urbs_free);
	/*
	 * Make sure URB is marked as free before checking the throttled flag
	 * to avoid racing with unthrottle() on another CPU. Matches the
	 * smp_mb() in unthrottle().
	 */
	smp_mb__after_atomic();

	if (stopped || stalled || cooldown) {
		if (stalled)
			schedule_delayed_work(&acm->dwork, 0);
		else if (cooldown)
			schedule_delayed_work(&acm->dwork, HZ / 2);
		return;
	}

	if (test_bit(ACM_THROTTLED, &acm->flags))
		return;

	acm_submit_read_urb(acm, rb->index, GFP_ATOMIC);
}

/* data interface wrote those outgoing bytes */
static void acm_write_bulk(struct urb *urb)
{
	struct acm_wb *wb = urb->context;
	struct acm *acm = wb->instance;
	unsigned long flags;
	int status = urb->status;

	if (status || (urb->actual_length != urb->transfer_buffer_length))
		dev_vdbg(&acm->data->dev, "wrote len %d/%d, status %d\n",
			urb->actual_length,
			urb->transfer_buffer_length,
			status);

	spin_lock_irqsave(&acm->write_lock, flags);
	acm_write_done(acm, wb);
	spin_unlock_irqrestore(&acm->write_lock, flags);
	set_bit(EVENT_TTY_WAKEUP, &acm->flags);
	schedule_delayed_work(&acm->dwork, 0);
}

static void acm_softint(struct work_struct *work)
{
	int i;
	struct acm *acm = container_of(work, struct acm, dwork.work);

	if (test_bit(EVENT_RX_STALL, &acm->flags)) {
		smp_mb(); /* against acm_suspend() */
		if (!acm->susp_count) {
			for (i = 0; i < acm->rx_buflimit; i++)
				usb_kill_urb(acm->read_urbs[i]);
			usb_clear_halt(acm->dev, acm->in);
			acm_submit_read_urbs(acm, GFP_KERNEL);
			clear_bit(EVENT_RX_STALL, &acm->flags);
		}
	}

	if (test_and_clear_bit(ACM_ERROR_DELAY, &acm->flags)) {
		for (i = 0; i < acm->rx_buflimit; i++)
			if (test_and_clear_bit(i, &acm->urbs_in_error_delay))
				acm_submit_read_urb(acm, i, GFP_KERNEL);
	}

	if (test_and_clear_bit(EVENT_TTY_WAKEUP, &acm->flags))
		tty_port_tty_wakeup(&acm->port);
}

/*
 * TTY handlers
 */

static int acm_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct acm *acm;
	int retval;

	acm = acm_get_by_minor(tty->index);
	if (!acm)
		return -ENODEV;

	retval = tty_standard_install(driver, tty);
	if (retval)
		goto error_init_termios;

	/*
	 * Suppress initial echoing for some devices which might send data
	 * immediately after acm driver has been installed.
	 */
	if (acm->quirks & DISABLE_ECHO)
		tty->termios.c_lflag &= ~ECHO;

	tty->driver_data = acm;

	return 0;

error_init_termios:
	tty_port_put(&acm->port);
	return retval;
}

static int acm_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct acm *acm = tty->driver_data;

	return tty_port_open(&acm->port, tty, filp);
}

static void acm_port_dtr_rts(struct tty_port *port, int raise)
{
	struct acm *acm = container_of(port, struct acm, port);
	int val;
	int res;

	if (raise)
		val = ACM_CTRL_DTR | ACM_CTRL_RTS;
	else
		val = 0;

	/* FIXME: add missing ctrlout locking throughout driver */
	acm->ctrlout = val;

	res = acm_set_control(acm, val);
	if (res && (acm->ctrl_caps & USB_CDC_CAP_LINE))
		/* This is broken in too many devices to spam the logs */
		dev_dbg(&acm->control->dev, "failed to set dtr/rts\n");
}

static int acm_port_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct acm *acm = container_of(port, struct acm, port);
	int retval = -ENODEV;
	int i;

	mutex_lock(&acm->mutex);
	if (acm->disconnected)
		goto disconnected;

	retval = usb_autopm_get_interface(acm->control);
	if (retval)
		goto error_get_interface;

	/*
	 * FIXME: Why do we need this? Allocating 64K of physically contiguous
	 * memory is really nasty...
	 */
	set_bit(TTY_NO_WRITE_SPLIT, &tty->flags);
	acm->control->needs_remote_wakeup = 1;

	acm->ctrlurb->dev = acm->dev;
	retval = usb_submit_urb(acm->ctrlurb, GFP_KERNEL);
	if (retval) {
		dev_err(&acm->control->dev,
			"%s - usb_submit_urb(ctrl irq) failed\n", __func__);
		goto error_submit_urb;
	}

	acm_tty_set_termios(tty, NULL);

	/*
	 * Unthrottle device in case the TTY was closed while throttled.
	 */
	clear_bit(ACM_THROTTLED, &acm->flags);

	retval = acm_submit_read_urbs(acm, GFP_KERNEL);
	if (retval)
		goto error_submit_read_urbs;

	usb_autopm_put_interface(acm->control);

	mutex_unlock(&acm->mutex);

	return 0;

error_submit_read_urbs:
	for (i = 0; i < acm->rx_buflimit; i++)
		usb_kill_urb(acm->read_urbs[i]);
	usb_kill_urb(acm->ctrlurb);
error_submit_urb:
	usb_autopm_put_interface(acm->control);
error_get_interface:
disconnected:
	mutex_unlock(&acm->mutex);

	return usb_translate_errors(retval);
}

static void acm_port_destruct(struct tty_port *port)
{
	struct acm *acm = container_of(port, struct acm, port);

	acm_release_minor(acm);
	usb_put_intf(acm->control);
	kfree(acm->country_codes);
	kfree(acm);
}

static void acm_port_shutdown(struct tty_port *port)
{
	struct acm *acm = container_of(port, struct acm, port);
	struct urb *urb;
	struct acm_wb *wb;

	/*
	 * Need to grab write_lock to prevent race with resume, but no need to
	 * hold it due to the tty-port initialised flag.
	 */
	acm_poison_urbs(acm);
	spin_lock_irq(&acm->write_lock);
	spin_unlock_irq(&acm->write_lock);

	usb_autopm_get_interface_no_resume(acm->control);
	acm->control->needs_remote_wakeup = 0;
	usb_autopm_put_interface(acm->control);

	for (;;) {
		urb = usb_get_from_anchor(&acm->delayed);
		if (!urb)
			break;
		wb = urb->context;
		wb->use = false;
		usb_autopm_put_interface_async(acm->control);
	}

	acm_unpoison_urbs(acm);

}

static void acm_tty_cleanup(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;

	tty_port_put(&acm->port);
}

static void acm_tty_hangup(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;

	tty_port_hangup(&acm->port);
}

static void acm_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct acm *acm = tty->driver_data;

	tty_port_close(&acm->port, tty, filp);
}

static int acm_tty_write(struct tty_struct *tty,
					const unsigned char *buf, int count)
{
	struct acm *acm = tty->driver_data;
	int stat;
	unsigned long flags;
	int wbn;
	struct acm_wb *wb;
#ifdef MY_DEF_HERE
	int iWaitTime = 0;
#endif /* MY_DEF_HERE */

	if (!count)
		return 0;

	dev_vdbg(&acm->data->dev, "%d bytes from tty layer\n", count);
#ifdef MY_DEF_HERE
	trace_acm_tty_write(acm, buf, count);

	if (syno_is_synology_acm(acm)) {
		for (iWaitTime = 0; iWaitTime < SYNO_EUNIT_ACM_WAITING_TRANSMIT_RETRY; iWaitTime++) {
			if (0 == acm->transmitting) {
				break;
			}
			msleep(SYNO_EUNIT_ACM_WAITING_TRANSMIT_DELAY_MS);
		}
	}
#endif /* MY_DEF_HERE */

	spin_lock_irqsave(&acm->write_lock, flags);
	wbn = acm_wb_alloc(acm);
	if (wbn < 0) {
		spin_unlock_irqrestore(&acm->write_lock, flags);
		return 0;
	}
	wb = &acm->wb[wbn];

	if (!acm->dev) {
		wb->use = false;
		spin_unlock_irqrestore(&acm->write_lock, flags);
		return -ENODEV;
	}

	count = (count > acm->writesize) ? acm->writesize : count;
	dev_vdbg(&acm->data->dev, "writing %d bytes\n", count);
	memcpy(wb->buf, buf, count);
	wb->len = count;

	stat = usb_autopm_get_interface_async(acm->control);
	if (stat) {
		wb->use = false;
		spin_unlock_irqrestore(&acm->write_lock, flags);
		return stat;
	}

	if (acm->susp_count) {
		usb_anchor_urb(wb->urb, &acm->delayed);
		spin_unlock_irqrestore(&acm->write_lock, flags);
		return count;
	}

	stat = acm_start_wb(acm, wb);
	spin_unlock_irqrestore(&acm->write_lock, flags);

	if (stat < 0)
		return stat;
	return count;
}

static int acm_tty_write_room(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;
	/*
	 * Do not let the line discipline to know that we have a reserve,
	 * or it might get too enthusiastic.
	 */
	return acm_wb_is_avail(acm) ? acm->writesize : 0;
}

static int acm_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;
	/*
	 * if the device was unplugged then any remaining characters fell out
	 * of the connector ;)
	 */
	if (acm->disconnected)
		return 0;
	/*
	 * This is inaccurate (overcounts), but it works.
	 */
	return (ACM_NW - acm_wb_is_avail(acm)) * acm->writesize;
}

static void acm_tty_throttle(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;

	set_bit(ACM_THROTTLED, &acm->flags);
}

static void acm_tty_unthrottle(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;

	clear_bit(ACM_THROTTLED, &acm->flags);

	/* Matches the smp_mb__after_atomic() in acm_read_bulk_callback(). */
	smp_mb();

	acm_submit_read_urbs(acm, GFP_KERNEL);
}

static int acm_tty_break_ctl(struct tty_struct *tty, int state)
{
	struct acm *acm = tty->driver_data;
	int retval;

	retval = acm_send_break(acm, state ? 0xffff : 0);
	if (retval < 0)
		dev_dbg(&acm->control->dev,
			"%s - send break failed\n", __func__);
	return retval;
}

static int acm_tty_tiocmget(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;

	return (acm->ctrlout & ACM_CTRL_DTR ? TIOCM_DTR : 0) |
	       (acm->ctrlout & ACM_CTRL_RTS ? TIOCM_RTS : 0) |
	       (acm->ctrlin  & ACM_CTRL_DSR ? TIOCM_DSR : 0) |
	       (acm->ctrlin  & ACM_CTRL_RI  ? TIOCM_RI  : 0) |
	       (acm->ctrlin  & ACM_CTRL_DCD ? TIOCM_CD  : 0) |
	       TIOCM_CTS;
}

static int acm_tty_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct acm *acm = tty->driver_data;
	unsigned int newctrl;

	newctrl = acm->ctrlout;
	set = (set & TIOCM_DTR ? ACM_CTRL_DTR : 0) |
					(set & TIOCM_RTS ? ACM_CTRL_RTS : 0);
	clear = (clear & TIOCM_DTR ? ACM_CTRL_DTR : 0) |
					(clear & TIOCM_RTS ? ACM_CTRL_RTS : 0);

	newctrl = (newctrl & ~clear) | set;

	if (acm->ctrlout == newctrl)
		return 0;
	return acm_set_control(acm, acm->ctrlout = newctrl);
}

static int get_serial_info(struct tty_struct *tty, struct serial_struct *ss)
{
	struct acm *acm = tty->driver_data;

	ss->line = acm->minor;
	ss->close_delay	= jiffies_to_msecs(acm->port.close_delay) / 10;
	ss->closing_wait = acm->port.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
				ASYNC_CLOSING_WAIT_NONE :
				jiffies_to_msecs(acm->port.closing_wait) / 10;
	return 0;
}

static int set_serial_info(struct tty_struct *tty, struct serial_struct *ss)
{
	struct acm *acm = tty->driver_data;
	unsigned int closing_wait, close_delay;
	int retval = 0;

	close_delay = msecs_to_jiffies(ss->close_delay * 10);
	closing_wait = ss->closing_wait == ASYNC_CLOSING_WAIT_NONE ?
			ASYNC_CLOSING_WAIT_NONE :
			msecs_to_jiffies(ss->closing_wait * 10);

	mutex_lock(&acm->port.mutex);

	if (!capable(CAP_SYS_ADMIN)) {
		if ((close_delay != acm->port.close_delay) ||
		    (closing_wait != acm->port.closing_wait))
			retval = -EPERM;
	} else {
		acm->port.close_delay  = close_delay;
		acm->port.closing_wait = closing_wait;
	}

	mutex_unlock(&acm->port.mutex);
	return retval;
}

static int wait_serial_change(struct acm *acm, unsigned long arg)
{
	int rv = 0;
	DECLARE_WAITQUEUE(wait, current);
	struct async_icount old, new;

	do {
		spin_lock_irq(&acm->read_lock);
		old = acm->oldcount;
		new = acm->iocount;
		acm->oldcount = new;
		spin_unlock_irq(&acm->read_lock);

		if ((arg & TIOCM_DSR) &&
			old.dsr != new.dsr)
			break;
		if ((arg & TIOCM_CD)  &&
			old.dcd != new.dcd)
			break;
		if ((arg & TIOCM_RI) &&
			old.rng != new.rng)
			break;

		add_wait_queue(&acm->wioctl, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		remove_wait_queue(&acm->wioctl, &wait);
		if (acm->disconnected) {
			if (arg & TIOCM_CD)
				break;
			else
				rv = -ENODEV;
		} else {
			if (signal_pending(current))
				rv = -ERESTARTSYS;
		}
	} while (!rv);

	

	return rv;
}

static int acm_tty_get_icount(struct tty_struct *tty,
					struct serial_icounter_struct *icount)
{
	struct acm *acm = tty->driver_data;

	icount->dsr = acm->iocount.dsr;
	icount->rng = acm->iocount.rng;
	icount->dcd = acm->iocount.dcd;
	icount->frame = acm->iocount.frame;
	icount->overrun = acm->iocount.overrun;
	icount->parity = acm->iocount.parity;
	icount->brk = acm->iocount.brk;

	return 0;
}

static int acm_tty_ioctl(struct tty_struct *tty,
					unsigned int cmd, unsigned long arg)
{
	struct acm *acm = tty->driver_data;
	int rv = -ENOIOCTLCMD;

	switch (cmd) {
	case TIOCMIWAIT:
		rv = usb_autopm_get_interface(acm->control);
		if (rv < 0) {
			rv = -EIO;
			break;
		}
		rv = wait_serial_change(acm, arg);
		usb_autopm_put_interface(acm->control);
		break;
	}

	return rv;
}

static void acm_tty_set_termios(struct tty_struct *tty,
						struct ktermios *termios_old)
{
	struct acm *acm = tty->driver_data;
	struct ktermios *termios = &tty->termios;
	struct usb_cdc_line_coding newline;
	int newctrl = acm->ctrlout;

	newline.dwDTERate = cpu_to_le32(tty_get_baud_rate(tty));
	newline.bCharFormat = termios->c_cflag & CSTOPB ? 2 : 0;
	newline.bParityType = termios->c_cflag & PARENB ?
				(termios->c_cflag & PARODD ? 1 : 2) +
				(termios->c_cflag & CMSPAR ? 2 : 0) : 0;
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		newline.bDataBits = 5;
		break;
	case CS6:
		newline.bDataBits = 6;
		break;
	case CS7:
		newline.bDataBits = 7;
		break;
	case CS8:
	default:
		newline.bDataBits = 8;
		break;
	}
	/* FIXME: Needs to clear unsupported bits in the termios */
	acm->clocal = ((termios->c_cflag & CLOCAL) != 0);

	if (C_BAUD(tty) == B0) {
		newline.dwDTERate = acm->line.dwDTERate;
		newctrl &= ~ACM_CTRL_DTR;
	} else if (termios_old && (termios_old->c_cflag & CBAUD) == B0) {
		newctrl |=  ACM_CTRL_DTR;
	}

	if (newctrl != acm->ctrlout)
		acm_set_control(acm, acm->ctrlout = newctrl);

	if (memcmp(&acm->line, &newline, sizeof newline)) {
		memcpy(&acm->line, &newline, sizeof newline);
		dev_dbg(&acm->control->dev, "%s - set line: %d %d %d %d\n",
			__func__,
			le32_to_cpu(newline.dwDTERate),
			newline.bCharFormat, newline.bParityType,
			newline.bDataBits);
		acm_set_line(acm, &acm->line);
	}
}

static const struct tty_port_operations acm_port_ops = {
	.dtr_rts = acm_port_dtr_rts,
	.shutdown = acm_port_shutdown,
	.activate = acm_port_activate,
	.destruct = acm_port_destruct,
};

/*
 * USB probe and disconnect routines.
 */

/* Little helpers: write/read buffers free */
static void acm_write_buffers_free(struct acm *acm)
{
	int i;
	struct acm_wb *wb;

	for (wb = &acm->wb[0], i = 0; i < ACM_NW; i++, wb++)
		usb_free_coherent(acm->dev, acm->writesize, wb->buf, wb->dmah);
}

static void acm_read_buffers_free(struct acm *acm)
{
	int i;

	for (i = 0; i < acm->rx_buflimit; i++)
		usb_free_coherent(acm->dev, acm->readsize,
			  acm->read_buffers[i].base, acm->read_buffers[i].dma);
}

/* Little helper: write buffers allocate */
static int acm_write_buffers_alloc(struct acm *acm)
{
	int i;
	struct acm_wb *wb;

	for (wb = &acm->wb[0], i = 0; i < ACM_NW; i++, wb++) {
		wb->buf = usb_alloc_coherent(acm->dev, acm->writesize, GFP_KERNEL,
		    &wb->dmah);
		if (!wb->buf) {
			while (i != 0) {
				--i;
				--wb;
				usb_free_coherent(acm->dev, acm->writesize,
				    wb->buf, wb->dmah);
			}
			return -ENOMEM;
		}
	}
	return 0;
}

#ifdef MY_DEF_HERE

int syno_acm_slotindex_get(int *slot_index, struct acm *acm) {
	int ret = -1;
	const char *control_string = NULL, *usb_port_string = NULL;
	struct device_node *device_node = NULL, *control_method = NULL, *usb_port = NULL;
	int eunit_index = 0;

	if (NULL == acm || NULL == slot_index) {
		goto END;
	}
	for_each_child_of_node(of_root, device_node) {
		if (!device_node->full_name) {
			continue;
		}

		if (strstr(device_node->full_name, DT_ESATA_SLOT)) {
			sscanf(device_node->full_name, DT_ESATA_SLOT"@%d", &eunit_index);
		} else if (strstr(device_node->full_name, DT_CX4_SLOT)) {
			sscanf(device_node->full_name, DT_CX4_SLOT"@%d", &eunit_index);
		} else if (strstr(device_node->full_name, DT_PCIE_EUNIT_SLOT)) {
			sscanf(device_node->full_name, DT_PCIE_EUNIT_SLOT"@%d", &eunit_index);
		} else {
			continue;
		}

		for_each_child_of_node(device_node, control_method) {
			if (!control_method->name || strcmp(DT_EUNIT_CONTROL_METHOD, control_method->name)) {
				continue;
			}
			if (0 > of_property_read_string(control_method, DT_EUNIT_CONTROL_TYPE, &control_string)) {
				continue;
			}
			if (0 != strcmp(DT_USB_TO_TTY, control_string)) {
				continue;
			}
			for_each_child_of_node(control_method, usb_port) {
				if (usb_port->name && 0 == strcmp(usb_port->name, DT_USB2)) {
					if (0 > of_property_read_string(usb_port, DT_USB_PORT, &usb_port_string)) {
						continue;
					}
					if (0 == strcmp(dev_name(&acm->dev->dev), usb_port_string)) {
						*slot_index = eunit_index;
						ret = 0;
					}
					//TODO: do early break
				}
			}
		}
	}

END:
	return ret;
}

int syno_usb_acm_container_index_get_by_diskname(int *slot_index, const char *disk_name)
{
	struct acm *acm;
	int ret = -1;
	struct syno_device_list *sdl = NULL, *tmp = NULL;
	struct syno_acm_list *sal = NULL, *sal_tmp = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&acm_list_lock, flags);
	list_for_each_entry_safe(sal, sal_tmp, &syno_acm_list_head, device_list) {
		acm = sal->acm;
		list_for_each_entry_safe(sdl, tmp, &acm->syno_device_list, device_list) {
			if (0 == strcmp(sdl->disk_name, disk_name)) {
				if (0 <= syno_acm_slotindex_get(slot_index, acm)){
					ret = 0;
				}
			}
		}
	}
	spin_unlock_irqrestore(&acm_list_lock, flags);
	return ret;
}

typedef struct __syno_acm_work {
	struct list_head acm_list;
	char *szCmd;
	int tagId;
	struct completion cmpl;
	struct kref refcount;
	bool blocking;
	bool aborted;
} syno_acm_work;

#define SYNO_EUNIT_POSTFIX "\xd\x0"
static syno_acm_work* init_syno_acm_work(void)
{
	syno_acm_work *work = NULL;

	if (NULL == (work = kzalloc(sizeof(syno_acm_work), GFP_ATOMIC))) {
		printk(KERN_ERR "%s: Alloc work failed\n", __FUNCTION__);
		goto END;
	}

	/* Alloc & assign szCmd */	
	if (NULL == (work->szCmd = kzalloc(sizeof(char)*SYNO_EUNIT_STATUS_BUFFER_SIZE, GFP_ATOMIC))) {
		printk(KERN_ERR "%s: Alloc szCmd failed\n", __FUNCTION__);
		goto FAIL;
	}

	/* Init reference count */
	kref_init(&work->refcount);

	/* Init completion */
	init_completion(&work->cmpl);

	/* Default: non-blocking */
	work->blocking = false;

	work->aborted = false;
END:
	return work;

FAIL:
	if (work) {
		kfree(work);
		work = NULL;
	}
	return NULL;
}

static void free_syno_acm_work(syno_acm_work *acm_work)
{
	if (!acm_work)
		return;

	if (acm_work->szCmd) {
		kfree(acm_work->szCmd);
	}
	
	kfree(acm_work);
	return;
}

static bool syno_eunit_queue_full(struct acm *acm)
{
	return ((acm->cmd_idx_head % SYNO_EUNIT_QUEUE_SIZE) + 1 == acm->cmd_idx_tail);
}

static int syno_eunit_queue_head_get_then_increase(struct acm *acm)
{
	int ret = acm->cmd_idx_head;

	acm->cmd_idx_head = (acm->cmd_idx_head % SYNO_EUNIT_QUEUE_SIZE) + 1;
	
	return ret;
}

static void syno_eunit_queue_tail_set_and_increse(struct acm *acm, int tagId)
{
	acm->cmd_idx_tail = (tagId % SYNO_EUNIT_QUEUE_SIZE) + 1;
}

/*
 * Try to merge work2 into work1
 *
 * @work1: work1
 * @work2: work2
 *
 * Return: 1: merged
 *         0: not merged
 */

static bool syno_eunit_merge_cmd(syno_acm_work *work1, syno_acm_work *work2)
{
	bool bMerged = false;

	char *szCmd = NULL; /* Merged command */
	char *szHead1 = NULL, *szHead2 = NULL; /* Heads for duplicated command */
	char *szToken1 = NULL, *szToken2 = NULL; /* Parsing tokens */
	char *szTmp1 = NULL, *szTmp2 = NULL; /* Temporary tokens */

	if (!work1 || !work2) {
		goto END;
	}

	if (NULL == (szCmd = kzalloc(sizeof(char)*SYNO_EUNIT_STATUS_BUFFER_SIZE, GFP_ATOMIC))) {
		goto END;
	}

	/* Duplicate commands */
	if (NULL == (szHead1 = kstrdup(work1->szCmd, GFP_ATOMIC)) ||
			NULL == (szHead2 = kstrdup(work2->szCmd, GFP_ATOMIC))) {
		goto END;
	}

	/* Assign parsing token */
	szToken1 = szHead1;
	szToken2 = szHead2;

	/* Compare then skip command name */
	if (NULL == (szTmp1 = strsep(&szToken1, ":")) ||
			NULL == (szTmp2 = strsep(&szToken2, ":")) ||
			0 != strcmp(szTmp1, szTmp2)) {
		goto END;
	}

	snprintf(szCmd, SYNO_EUNIT_STATUS_BUFFER_SIZE, "%s:", szTmp1);

	if (0 == strcmp(szTmp1, DT_EUNIT_STATUS_EXPSTATUS)) {
		/* expstatus has differnt format */
		goto SKIP_CHECK;
	}

	/* Replace last '/' with '\0' */
	if (NULL == (szTmp1 = strrchr(szToken1, SYNO_EUNIT_COMMAND_DELIM)) ||
			NULL == (szTmp2 = strrchr(szToken2, SYNO_EUNIT_COMMAND_DELIM))) {
		goto END;
	}
	szTmp1[0] = '\0';
	szTmp2[0] = '\0';

	/* Compare and replace */
	szTmp1 = strsep(&szToken1, "/");
	szTmp2 = strsep(&szToken2, "/");
	
	while (szTmp1 || szTmp2) {

		if (NULL == szTmp1) {
			snprintf(szCmd + strlen(szCmd), SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(szCmd), "%s/", szTmp2);
		} else if (NULL == szTmp2) {
			snprintf(szCmd + strlen(szCmd), SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(szCmd), "%s/", szTmp1);
		} else if ('R' == szTmp1[0]) {
			snprintf(szCmd + strlen(szCmd), SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(szCmd), "%s/", szTmp2);
		} else if ('R' == szTmp2[0]) {
			snprintf(szCmd + strlen(szCmd), SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(szCmd), "%s/", szTmp1);
		} else if (0 == strcmp(szTmp1, szTmp2)) {
			snprintf(szCmd + strlen(szCmd), SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(szCmd), "%s/", szTmp1);
		} else {
			goto END;
		}

		szTmp1 = strsep(&szToken1, "/");
		szTmp2 = strsep(&szToken2, "/");
	}

SKIP_CHECK:
	/* Append ACK ID */
	strncpy(work1->szCmd, szCmd, SYNO_EUNIT_STATUS_BUFFER_SIZE);

	bMerged = true;
END:
	kfree(szHead1);
	kfree(szHead2);
	kfree(szCmd);
	return bMerged;
}

static bool syno_eunit_is_same_command(syno_acm_work *work1, syno_acm_work *work2)
{
	bool bRet = false;
	int i = 0;

	if (!work1 || !work2) {
		goto END;
	}

	for (i = 0; i < SYNO_EUNIT_STATUS_BUFFER_SIZE; i++) {
		if (':' == work1->szCmd[i] && ':' == work2->szCmd[i]) {
			bRet = true;
			break;
		} else if (work1->szCmd[i] != work2->szCmd[i]) {
			break;
		}
	}

END:
	return bRet;
}

static syno_acm_work *syno_acm_try_merge_work(struct acm *acm, syno_acm_work *work)
{
	syno_acm_work *iter = NULL;
	bool bMerge = false;

	list_for_each_entry_reverse(iter, &acm->syno_acm_q, acm_list) {
		if (syno_eunit_is_same_command(iter, work)) {
			bMerge = syno_eunit_merge_cmd(iter, work);
			break;
		}
	}
	
	return bMerge ? iter : NULL;
}

static void syno_acm_completion_release(struct kref *ref)
{
	syno_acm_work *work = container_of(ref, syno_acm_work, refcount);
	free_syno_acm_work(work);
}

static int syno_samd_tty_write_raw(struct acm *acm, const char *command)
{
	char *buffer = NULL;
	struct tty_struct *tty = NULL;
	int iRet = 0;

	if (!acm || !command) {
		iRet = -EINVAL;
		goto END;
	}

	tty = kzalloc(sizeof(struct tty_struct), GFP_ATOMIC);
	if (!tty) {
		iRet = -ENOMEM;
		goto END;
	}
	tty->driver_data = acm;
	buffer = kzalloc(sizeof(char)*SYNO_EUNIT_STATUS_BUFFER_SIZE, GFP_ATOMIC);
	if (!buffer) {
		iRet = -ENOMEM;
		goto END;
	}
	snprintf(buffer, SYNO_EUNIT_STATUS_BUFFER_SIZE, "%s%s", command, SYNO_EUNIT_POSTFIX);
	iRet = acm_tty_write(tty, buffer, strlen(buffer));

END:
	kfree(tty);
	kfree(buffer);

	return iRet;
}

static int syno_samd_tty_write(struct acm *acm, const char *command, bool high_priority, bool blocking)
{
	int ret = 0;

	unsigned long flags; /* For lock */
	syno_acm_work *work = NULL;
	syno_acm_work *workMerge = NULL;

	if (!acm || !command) {
		printk(KERN_ERR "%s: Invalid parameter\n", __FUNCTION__);
		ret = -EINVAL;
		goto END;
	}

	if (NULL == (work = init_syno_acm_work())) {
		printk(KERN_ERR "%s: Init syno_acm_work failed\n", __FUNCTION__);
		ret = -ENOMEM;
		goto END;
	}	

	spin_lock_irqsave(&acm->syno_acm_q_lock, flags); /* Lock */

	if (syno_eunit_queue_full(acm)) {
		printk(KERN_ERR "%s: Queue is full\n", __FUNCTION__);
		ret = -ENOMEM;
		spin_unlock_irqrestore(&acm->syno_acm_q_lock, flags); /* Unlock */
		goto END;
	}

	snprintf(work->szCmd, SYNO_EUNIT_STATUS_BUFFER_SIZE, "%s", command);
	
	/* Try to merge commands */
	if (NULL != (workMerge = syno_acm_try_merge_work(acm, work))) {
		/* Success */
		kref_put(&work->refcount, syno_acm_completion_release);
		work = workMerge;
	} else {
		/* Failed to merge, add command into queue */
		if (high_priority) {
			list_add(&work->acm_list, &acm->syno_acm_q);
		} else {
			list_add_tail(&work->acm_list, &acm->syno_acm_q);
		}
		syno_eunit_queue_head_get_then_increase(acm);
	}

	if (blocking) {
		kref_get(&work->refcount);
		work->blocking = true;
	}

	spin_unlock_irqrestore(&acm->syno_acm_q_lock, flags); /* Unlock */

	if (blocking) {
		if (0 == wait_for_completion_timeout(&work->cmpl, msecs_to_jiffies(4000))) {
			printk(KERN_ERR "%s: Timeout\n", __FUNCTION__);
			ret = -ETIMEDOUT;
		}

		spin_lock_irqsave(&acm->syno_acm_q_lock, flags); /* Lock */
		work->aborted = true; /* Abort */
		if (work->tagId) {
			acm->syno_acm_ack_to_cmpl[work->tagId-1] = NULL;
		}
		spin_unlock_irqrestore(&acm->syno_acm_q_lock, flags); /* Unlock */

		kref_put(&work->refcount, syno_acm_completion_release);
	}

END:
	return ret;
}

static ssize_t syno_eunit_info_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct syno_device_list *sdl = NULL, *tmp = NULL;
	char szTmp[SYNO_EUNIT_STATUS_BUFFER_SIZE] = {0};
	char *szTmp1 = NULL;
	struct usb_interface *intf = to_usb_interface(dev);
	struct acm *acm = usb_get_intfdata(intf);
	struct usb_device *usb_dev = interface_to_usbdev(acm->control);
	unsigned long flags = 0;

	if (acm->disconnected) {
		return 0;
	}

	szTmp1 = (char*) kzalloc(PAGE_SIZE, GFP_KERNEL);

	if (NULL == szTmp1) {
		printk(KERN_WARNING "%s kzalloc failed\n", __FUNCTION__);
		return 0;
	}

	spin_lock_irqsave(&acm_list_lock, flags);
	list_for_each_entry_safe(sdl, tmp, &acm->syno_device_list, device_list) {
		snprintf(szTmp1, BDEVNAME_SIZE, "/dev/%s,", sdl->disk_name);
		strncat(szTmp, szTmp1, BDEVNAME_SIZE);
	}
	spin_unlock_irqrestore(&acm_list_lock, flags);
	if (strlen(szTmp)) {
		szTmp[strlen(szTmp)-1] = '\0';
	}

	snprintf(szTmp1, PAGE_SIZE, "%s%s%s%s", EBOX_INFO_DEV_LIST_KEY, "=\"", szTmp, "\"\n");

	/* vendor id and device id */
	snprintf(szTmp,
			BDEVNAME_SIZE,
			"%s=%s0x%x%s", EBOX_INFO_VENDOR_KEY, "\"",
			usb_dev->descriptor.idVendor,
			"\"\n");
	strncat(szTmp1, szTmp, BDEVNAME_SIZE);
	snprintf(szTmp,
			BDEVNAME_SIZE,
			"%s=%s0x%x%s", EBOX_INFO_DEVICE_KEY, "\"",
			usb_dev->descriptor.idProduct,
			"\"\n");
	strncat(szTmp1, szTmp, BDEVNAME_SIZE);
	snprintf(szTmp, BDEVNAME_SIZE,
			EBOX_INFO_USB_PATH"=\"%s\"\n",
			dev_name(&acm->dev->dev));
	strncat(szTmp1, szTmp, BDEVNAME_SIZE);

	snprintf(szTmp,
			sizeof(acm->uuid) + 16,
			"%s=\"%s\"\n", EBOX_INFO_UUID_KEY, acm->uuid);
	strncat(szTmp1, szTmp, sizeof(acm->uuid) + 16);

	/* deepsleep support */
	snprintf(szTmp,
			BDEVNAME_SIZE,
			"%s=\"%s\"\n", EBOX_INFO_DEEP_SLEEP, "yes");
	strncat(szTmp1, szTmp, BDEVNAME_SIZE);

	if (NULL != acm->cached_expstatus[EUNIT_STATUS_EXPIDSET] && 0 < strlen(acm->cached_expstatus[EUNIT_STATUS_EXPIDSET])) {
		snprintf(szTmp,
			BDEVNAME_SIZE,
			"%s=\"%s\"\n%s=\"%d\"\n",
			EBOX_INFO_UNIQUE_KEY,
			acm->cached_expstatus[EUNIT_STATUS_EXPIDSET],
			EBOX_INFO_EMID_KEY,
			0);
		strncat(szTmp1, szTmp, BDEVNAME_SIZE);
	} else {
		snprintf(szTmp,
			BDEVNAME_SIZE,
			"%s=\"%s\"\n%s=\"%d\"\n",
			EBOX_INFO_UNIQUE_KEY,
			"Unknown",
			EBOX_INFO_EMID_KEY,
			0);
		strncat(szTmp1, szTmp, BDEVNAME_SIZE);
	}

	/* put it together */
	snprintf(buf, PAGE_SIZE, "%s", szTmp1);
	kfree(szTmp1);

	return strlen(buf);
}

static DEVICE_ATTR(syno_eunit_info, S_IRUGO, syno_eunit_info_show, NULL);

#define syno_usb_eunit_actconfig_show(field)		\
	static ssize_t syno_eunit_##field##_show(struct device *dev,					\
			struct device_attribute *attr, char *buf)				\
{										\
	struct acm *acm = usb_get_intfdata(to_usb_interface(dev));		\
	unsigned long flags = 0; 										\
	int len = 0; 	\
	read_lock_irqsave(&acm->status_lock, flags);	\
	len = snprintf(buf, PAGE_SIZE, "%s", acm->cached_expstatus[parsing_key(#field)]);	\
	read_unlock_irqrestore(&acm->status_lock, flags);		\
	return len;		\
}					\

#define syno_usb_eunit_actconfig_attr(field)        \
	syno_usb_eunit_actconfig_show(field)        \
	static DEVICE_ATTR_RO(syno_eunit_##field)

syno_usb_eunit_actconfig_attr(upversion);
syno_usb_eunit_actconfig_attr(hddenable);
syno_usb_eunit_actconfig_attr(hddpresent);
syno_usb_eunit_actconfig_attr(monthermal);
syno_usb_eunit_actconfig_attr(moncurrent);
syno_usb_eunit_actconfig_attr(monvoltage);
syno_usb_eunit_actconfig_attr(expctrl);
syno_usb_eunit_actconfig_attr(fanpwm);
syno_usb_eunit_actconfig_attr(fanspeed);
syno_usb_eunit_actconfig_attr(hddctrl);
syno_usb_eunit_actconfig_attr(diskled);
syno_usb_eunit_actconfig_attr(7segled);
syno_usb_eunit_actconfig_attr(expidset);
syno_usb_eunit_actconfig_attr(expsnset);
syno_usb_eunit_actconfig_attr(powermodule);
syno_usb_eunit_actconfig_attr(expbpsnset);
syno_usb_eunit_actconfig_attr(hddsedset);
syno_usb_eunit_actconfig_attr(dimmer);

static ssize_t syno_eunit_write_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t len)
{
	struct acm *acm = usb_get_intfdata(to_usb_interface(dev));
	if (strstr(buf, "hddsedset")) {
		if (syno_samd_tty_write(acm, buf, false, true)) {
			return -1;
		}
	} else {
		if (syno_samd_tty_write(acm, buf, false, false)) {
			return -1;
		}
	}
	return len;
}
static DEVICE_ATTR(syno_eunit_write, S_IWUSR, NULL, syno_eunit_write_store);

static void syno_remove_eunit_files(struct device *dev)
{
	device_remove_file(dev, &dev_attr_syno_eunit_expctrl);
	device_remove_file(dev, &dev_attr_syno_eunit_fanpwm);
	device_remove_file(dev, &dev_attr_syno_eunit_fanspeed);
	device_remove_file(dev, &dev_attr_syno_eunit_hddctrl);
	device_remove_file(dev, &dev_attr_syno_eunit_diskled);
	device_remove_file(dev, &dev_attr_syno_eunit_7segled);
	device_remove_file(dev, &dev_attr_syno_eunit_expidset);
	device_remove_file(dev, &dev_attr_syno_eunit_expsnset);
	device_remove_file(dev, &dev_attr_syno_eunit_info);
	device_remove_file(dev, &dev_attr_syno_eunit_upversion);
	device_remove_file(dev, &dev_attr_syno_eunit_hddenable);
	device_remove_file(dev, &dev_attr_syno_eunit_hddpresent);
	device_remove_file(dev, &dev_attr_syno_eunit_monthermal);
	device_remove_file(dev, &dev_attr_syno_eunit_moncurrent);
	device_remove_file(dev, &dev_attr_syno_eunit_monvoltage);
	device_remove_file(dev, &dev_attr_syno_eunit_powermodule);
	device_remove_file(dev, &dev_attr_syno_eunit_expbpsnset);
	device_remove_file(dev, &dev_attr_syno_eunit_write);
	device_remove_file(dev, &dev_attr_syno_eunit_hddsedset);
	device_remove_file(dev, &dev_attr_syno_eunit_dimmer);
}

int syno_acm_get_usb_port(const char **usb_port_string, int eunit_slot) {
	int ret = -1;
	const char *control_string = NULL;
	struct device_node *device_node = NULL, *control_method = NULL, *usb_port = NULL;
	int eunit_index = 0;

	if (NULL == usb_port_string || 0 >= eunit_slot) {
		goto END;
	}
	for_each_child_of_node(of_root, device_node) {
		if (!device_node->full_name) {
			continue;
		}

		if (strstr(device_node->full_name, DT_ESATA_SLOT)) {
			sscanf(device_node->full_name, DT_ESATA_SLOT"@%d", &eunit_index);
		} else if (strstr(device_node->full_name, DT_CX4_SLOT)) {
			sscanf(device_node->full_name, DT_CX4_SLOT"@%d", &eunit_index);
		} else if (strstr(device_node->full_name, DT_PCIE_EUNIT_SLOT)) {
			sscanf(device_node->full_name, DT_PCIE_EUNIT_SLOT"@%d", &eunit_index);
		} else {
			continue;
		}

		if (eunit_index != eunit_slot) {
			continue;
		}

		for_each_child_of_node(device_node, control_method) {
			if (!control_method->name || strcmp(DT_EUNIT_CONTROL_METHOD, control_method->name)) {
				continue;
			}
			if (0 > of_property_read_string(control_method, DT_EUNIT_CONTROL_TYPE, &control_string)) {
				continue;
			}
			if (0 != strcmp(DT_USB_TO_TTY, control_string)) {
				continue;
			}
			for_each_child_of_node(control_method, usb_port) {
				if (usb_port->name && 0 == strcmp(usb_port->name, DT_USB2)) {
					if (0 > of_property_read_string(usb_port, DT_USB_PORT, usb_port_string)) {
						continue;
					}
					ret = 0;
					//TODO: do early break
				}
			}
		}
	}

END:
	return ret;
}
EXPORT_SYMBOL(syno_acm_get_usb_port);

static void syno_create_eunit_files(struct device *dev)
{
	int retval = 0;
	//TODO: receive return error code
	retval = device_create_file(dev, &dev_attr_syno_eunit_expctrl);
	retval = device_create_file(dev, &dev_attr_syno_eunit_fanpwm);
	retval = device_create_file(dev, &dev_attr_syno_eunit_fanspeed);
	retval = device_create_file(dev, &dev_attr_syno_eunit_hddctrl);
	retval = device_create_file(dev, &dev_attr_syno_eunit_diskled);
	retval = device_create_file(dev, &dev_attr_syno_eunit_7segled);
	retval = device_create_file(dev, &dev_attr_syno_eunit_expidset);
	retval = device_create_file(dev, &dev_attr_syno_eunit_expsnset);
	retval = device_create_file(dev, &dev_attr_syno_eunit_info);
	retval = device_create_file(dev, &dev_attr_syno_eunit_monthermal);
	retval = device_create_file(dev, &dev_attr_syno_eunit_monvoltage);
	retval = device_create_file(dev, &dev_attr_syno_eunit_moncurrent);
	retval = device_create_file(dev, &dev_attr_syno_eunit_upversion);
	retval = device_create_file(dev, &dev_attr_syno_eunit_hddenable);
	retval = device_create_file(dev, &dev_attr_syno_eunit_hddpresent);
	retval = device_create_file(dev, &dev_attr_syno_eunit_powermodule);
	retval = device_create_file(dev, &dev_attr_syno_eunit_expbpsnset);
	retval = device_create_file(dev, &dev_attr_syno_eunit_write);
	retval = device_create_file(dev, &dev_attr_syno_eunit_hddsedset);
	retval = device_create_file(dev, &dev_attr_syno_eunit_dimmer);
}

void syno_acm_device_list_add(int slot_index, const char* device_name)
{
	const char *usb_port_string = NULL;
	struct acm *acm = NULL;
	struct syno_device_list *sdl = NULL;
	struct acm_device_temp *adt = NULL;
	struct syno_acm_list *sal = NULL, *sal_tmp = NULL;
	unsigned long flags = 0;

	if (0 >= slot_index || !device_name) {
		goto END;
	}

	if (0 > syno_acm_get_usb_port(&usb_port_string, slot_index)) {
		goto END;
	}

	spin_lock_irqsave(&acm_list_lock, flags);
	list_for_each_entry_safe(sal, sal_tmp, &syno_acm_list_head, device_list) {
		acm = sal->acm;
		if (!acm->disconnected && 0 == strcmp(usb_port_string, dev_name(&acm->dev->dev))) {
			sdl = kzalloc(sizeof(*sdl), GFP_ATOMIC);
			if (!sdl) {
				continue;
			}
			snprintf(sdl->disk_name, DISK_NAME_LEN, "%s", device_name);
			list_add(&sdl->device_list, &acm->syno_device_list);
		}
	}
	adt = kzalloc(sizeof(*adt), GFP_ATOMIC);
	if (!adt) {
		spin_unlock_irqrestore(&acm_list_lock, flags);
		goto END;
	}
	snprintf(adt->disk_name, DISK_NAME_LEN, "%s", device_name);
	snprintf(adt->usb_path, SYNO_DTS_PROPERTY_CONTENT_LENGTH, "%s", usb_port_string);
	list_add(&adt->device_list, &acm_temp_device_list);
	spin_unlock_irqrestore(&acm_list_lock, flags);

END:
	return;
}

void syno_acm_device_list_delete(const char* device_name)
{
	struct acm *acm;
	struct syno_device_list *sdl = NULL, *tmp = NULL;
	struct syno_acm_list *sal = NULL, *sal_tmp = NULL;
	unsigned long flags = 0;
	struct acm_device_temp *adt = NULL, *adt_tmp = NULL;

	spin_lock_irqsave(&acm_list_lock, flags);
	list_for_each_entry_safe(sal, sal_tmp, &syno_acm_list_head, device_list) {
		acm = sal->acm;
		list_for_each_entry_safe(sdl, tmp, &acm->syno_device_list, device_list) {
			if (0 == strcmp(sdl->disk_name, device_name)) {
				list_del(&sdl->device_list);
				kfree(sdl);
			}
		}
	}

	list_for_each_entry_safe(adt, adt_tmp, &acm_temp_device_list, device_list) {
		if (0 == strcmp(adt->disk_name, device_name)) {
			list_del(&adt->device_list);
			kfree(adt);
		}
	}

	spin_unlock_irqrestore(&acm_list_lock, flags);
	return;
}

void syno_acm_device_list_set(struct scsi_device *sdev, int add, const char* device_name)
{
	struct ata_port *ap = NULL;
	int slot_index = -1;

	if (add) {

		/* Check scsi_device */
		if (!sdev) {
			goto END;
		}

		/* Get ATA port */
		if (NULL == (ap = ata_shost_to_port(sdev->host))) {
			goto END;
		}

		/* Get Slot Index */		
		if (0 >= (slot_index = syno_external_libata_index_get(ap))) {
			goto END;
		}

		syno_acm_device_list_add(slot_index, device_name);
	}
	else
		syno_acm_device_list_delete(device_name);

END:

}

EXPORT_SYMBOL(syno_acm_device_list_set);
EXPORT_SYMBOL(syno_acm_device_list_add);
EXPORT_SYMBOL(syno_acm_device_list_delete);

#define SYNO_EUNIT_PERIODIC_CACHE_UPDATE_INTERVAL 4000
#define SYNO_EUNIT_PERIODIC_CACHE_CHECK_INTERVAL  1000

static void syno_period_cache_update(struct work_struct *work)
{
	struct acm *acm = container_of(to_delayed_work(work), struct acm, cache_update_work);
	
	if (acm->disconnected)
		return;

	/* Queue Polling Command */
	if (time_after(jiffies, 
				acm->last_poll_jiffies + msecs_to_jiffies(SYNO_EUNIT_PERIODIC_CACHE_UPDATE_INTERVAL))) {
		syno_samd_tty_write(acm, DT_EUNIT_STATUS_EXPSTATUS":", false, false);
	}

	schedule_delayed_work(&acm->cache_update_work, SYNO_EUNIT_PERIODIC_CACHE_CHECK_INTERVAL);
	return;
}

#define SYNO_EUNIT_PERIODIC_CMD_ISSUE_INTERVAL 1000
static void syno_period_cmd_issue(struct work_struct *work)
{
	struct acm *acm = container_of(to_delayed_work(work), struct acm, cmd_issue_work);
	unsigned long flags = 0; /* For Locking */
	syno_acm_work *acm_work = NULL;


	spin_lock_irqsave(&acm->syno_acm_q_lock, flags); /* Lock */
	if (NULL == (acm_work = list_first_entry_or_null(&acm->syno_acm_q, typeof(*acm_work), acm_list))) {
		spin_unlock_irqrestore(&acm->syno_acm_q_lock, flags); /* Unlock */
		goto END;
	}

	list_del_init(&acm_work->acm_list); /* Delete entry */
	acm_work->tagId = acm->cmd_idx_tail;
	syno_eunit_queue_tail_set_and_increse(acm, acm_work->tagId);


	/* Timeout, abort command */
	if (acm_work->aborted) {
		spin_unlock_irqrestore(&acm->syno_acm_q_lock, flags); /* Unlock */
		goto END;
	}

	/* Assign */
	if (acm_work->blocking)
		acm->syno_acm_ack_to_cmpl[acm_work->tagId-1] = &acm_work->cmpl;

	spin_unlock_irqrestore(&acm->syno_acm_q_lock, flags); /* Unlock */

	snprintf(acm_work->szCmd, SYNO_EUNIT_STATUS_BUFFER_SIZE, "%s%d;", acm_work->szCmd, acm_work->tagId);

	/* Update last poll time */
	if (0 == strncmp(acm_work->szCmd, DT_EUNIT_STATUS_EXPSTATUS, strlen(DT_EUNIT_STATUS_EXPSTATUS))) {
		acm->last_poll_jiffies = jiffies;
	}

	syno_samd_tty_write_raw(acm, acm_work->szCmd);

END:
	if (acm_work)
		kref_put(&acm_work->refcount, syno_acm_completion_release);
	schedule_delayed_work(&acm->cmd_issue_work, SYNO_EUNIT_PERIODIC_CMD_ISSUE_INTERVAL);
}

#endif /* MY_DEF_HERE */

#ifdef MY_DEF_HERE
static struct acm *syno_acm_get_by_usbport(const char *usb_port)
{
	struct acm *acm = NULL;
	struct acm *acm_tmp = NULL;
	struct syno_acm_list *sal = NULL, *sal_tmp = NULL;
	unsigned long flags = 0;

	if (!usb_port) {
		return acm;
	}

	spin_lock_irqsave(&acm_list_lock, flags);
	list_for_each_entry_safe(sal, sal_tmp, &syno_acm_list_head, device_list) {
		acm_tmp = sal->acm;
		if (!acm_tmp->disconnected) {
			if (0 == strcmp(usb_port, dev_name(&acm_tmp->dev->dev))) {
				acm = acm_tmp;
			}
		}
		if (acm) {
			break;
		}
	}
	spin_unlock_irqrestore(&acm_list_lock, flags);

	return acm;
}

static struct acm *syno_acm_get_by_uuid(const char* uuid)
{
	unsigned long flags = 0;
	struct acm *acm = NULL;
	struct acm *acm_tmp = NULL;
	struct syno_acm_list *sal = NULL, *sal_tmp = NULL;

	spin_lock_irqsave(&acm_list_lock, flags);
	list_for_each_entry_safe(sal, sal_tmp, &syno_acm_list_head, device_list) {
		acm_tmp = sal->acm;
		if (!acm_tmp->disconnected) {
			if (strcmp(acm_tmp->uuid, uuid) == 0) {
				acm = acm_tmp;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&acm_list_lock, flags);

	return acm;
}

int syno_usb_acm_unique_get(const int slot_type, const int slot_index, char *unique, int unique_size)
{
	const char *usb_port = NULL;
	int ret = -1;
	struct acm *acm = NULL;

	if (EUNIT_DEVICE != slot_type || 0 >= slot_index || !unique || 0 >= unique_size) {
		goto END;
	}

	if (0 > syno_acm_get_usb_port(&usb_port, slot_index)) {
		goto END;
	}

	if (NULL == (acm = syno_acm_get_by_usbport(usb_port))) {
		goto END;
	}

	if (NULL == acm->cached_expstatus || NULL == acm->cached_expstatus[EUNIT_STATUS_EXPIDSET] ||
		strlen(acm->cached_expstatus[EUNIT_STATUS_EXPIDSET]) >= unique_size) {
		goto END;
	}
	snprintf(unique, unique_size, "%s", acm->cached_expstatus[EUNIT_STATUS_EXPIDSET]);

	ret = 0;

END:
	return ret;
}

static int syno_usb_get_hdd_count(const char *eunit_model_name) {
	int iCount = 0;
	struct device_node *device_node = NULL, *pmp_slot_node = NULL;

	if (!eunit_model_name) {
		return iCount;
	}

	for_each_child_of_node(of_root, device_node) {
		if (NULL == device_node->full_name || NULL == (strstr(device_node->full_name, eunit_model_name))) {
			continue;
		}
		for_each_child_of_node(device_node, pmp_slot_node) {
			if (pmp_slot_node->name && 0 == (strcmp(DT_PMP_SLOT, pmp_slot_node->name))) {
				iCount += 1;
			}
		}
	}

	return iCount;
}

/* Control the enable pin of single disk by ACM UUID
 *
 * @param [IN] uuid: ACM UUID
 * @param [IN] slot: slot index, 1-based
 * @param [IN] hdd_ctrl: 1 is power-on, 0 is power down
 *
 * @return 0: Success
 *        -1: Error
 */
int syno_usb_eunit_single_hdd_ctrl_by_uuid(
	const char *uuid, unsigned int slot, int hdd_ctrl)
{
	int i = 0;
	int disk_count = 0;
	char hdd_cmd[SYNO_EUNIT_STATUS_BUFFER_SIZE] = {0};
	struct acm *acm = NULL;

	if (hdd_ctrl != 0 && hdd_ctrl != 1) {
		return -1;
	}

	acm = syno_acm_get_by_uuid(uuid);
	if (acm == NULL || acm->cached_expstatus == NULL ||
		acm->cached_expstatus[EUNIT_STATUS_EXPIDSET] == NULL) {
		return -1;
	}

	disk_count = syno_usb_get_hdd_count(acm->cached_expstatus[EUNIT_STATUS_EXPIDSET]);
	if (0 >= disk_count) {
		return -1;
	}

	snprintf(hdd_cmd, SYNO_EUNIT_STATUS_BUFFER_SIZE, "%s:", DT_EUNIT_STATUS_HDDCTRL);
	for (i = 0; i < disk_count; i++) {
		if (slot == i + 1) {
			snprintf(hdd_cmd + strlen(hdd_cmd),
					SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(hdd_cmd), "%d/", hdd_ctrl);
			break;
		}
		snprintf(hdd_cmd + strlen(hdd_cmd),
				SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(hdd_cmd), "R/");
	}
	syno_samd_tty_write(acm, hdd_cmd, false, false);
	return 0;
}
EXPORT_SYMBOL(syno_usb_eunit_single_hdd_ctrl_by_uuid);

int syno_usb_eunit_hdd_reset(syno_nvme_disk_loc diskLoc)
{
	int ret = -1, i = 0;
	char hdd_cmd[SYNO_EUNIT_STATUS_BUFFER_SIZE] = {0};
	struct acm *acm = NULL;
	const char *usb_port = NULL;

	if (0 > syno_acm_get_usb_port(&usb_port, diskLoc.iContainer)) {
		printk(KERN_ERR "%s: syno_acm_get_usb_port failed\n", __FUNCTION__);
		goto END;
	}

	if (NULL == (acm = syno_acm_get_by_usbport(usb_port))) {
		printk(KERN_ERR "%s: syno_acm_get_by_usbport failed\n", __FUNCTION__);
		goto END;
	}

	if (0 >= snprintf(hdd_cmd, SYNO_EUNIT_STATUS_BUFFER_SIZE, "%s:", DT_EUNIT_STATUS_HDDRESET)) {
		printk(KERN_ERR "%s: snprintf failed\n", __FUNCTION__);
		goto END;
	}

	for (i = 0; i < diskLoc.iSlot - 1; i++) {
		if (0 >= snprintf(hdd_cmd + strlen(hdd_cmd) , SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(hdd_cmd), "R/")) {
			printk(KERN_ERR "%s: snprintf failed\n", __FUNCTION__);
			goto END;
		}
	}
	snprintf(hdd_cmd + strlen(hdd_cmd) , SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(hdd_cmd), "0/");

	syno_samd_tty_write(acm, hdd_cmd, true, false);

	msleep(500);
	ret = 0;

END:
	return ret;
}
EXPORT_SYMBOL(syno_usb_eunit_hdd_reset);

int syno_usb_eunit_deep_sleep_indicator(const int slot_type, const int slot_index, const int control) {
	int ret = -1, offset = 0;
	char indicator_cmd[SYNO_EUNIT_STATUS_BUFFER_SIZE] = {0};
	int i = 0;
	struct acm *acm = NULL;
	struct device_node *device_node = NULL, *deep_sleep_indicator = NULL;
	const char *command = NULL, *eunit_model_name = NULL, *usb_port = NULL;

	if (0 >= slot_type || 0 >= slot_index || (1 != control && 0 != control)) {
		goto END;
	}

	if (0 > syno_acm_get_usb_port(&usb_port, slot_index)) {
		goto END;
	}

	if (NULL == (acm = syno_acm_get_by_usbport(usb_port))) {
		goto END;
	}

	if (NULL == acm->cached_expstatus || NULL == acm->cached_expstatus[EUNIT_STATUS_EXPIDSET] ||
		NULL == (eunit_model_name = acm->cached_expstatus[EUNIT_STATUS_EXPIDSET])) {
		goto END;
	}

	for_each_child_of_node(of_root, device_node) {
		if (NULL == device_node->full_name || NULL == (strstr(device_node->full_name, eunit_model_name))) {
			continue;
		}
		for_each_child_of_node(device_node, deep_sleep_indicator) {
			if (!deep_sleep_indicator->name || strcmp(SZ_DTS_EBOX_I2C_DEEPSELLP_INDICATOR, deep_sleep_indicator->name)) {
				continue;
			}
			if (0 > of_property_read_string(deep_sleep_indicator, DT_EUNIT_COMMAND, &command)) {
				continue;
			}
			if (0 > of_property_read_u32(deep_sleep_indicator, SZ_DTS_EBOX_I2C_OFFSET, &offset)) {
				continue;
			}
			snprintf(indicator_cmd, SYNO_EUNIT_STATUS_BUFFER_SIZE, "%s:", command);
			for (i = 0; i < offset; i++) {
				strncat(indicator_cmd, "R/", SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(indicator_cmd) - 1);
			}
			snprintf(indicator_cmd + strlen(indicator_cmd), SYNO_EUNIT_STATUS_BUFFER_SIZE - strlen(indicator_cmd),
					"%d/", control);
		}
	}

	if (acm && strlen(indicator_cmd)) {
		syno_samd_tty_write(acm, indicator_cmd, false, false);
		ret = 0;
	}
END:
	return ret;
}
EXPORT_SYMBOL(syno_usb_eunit_deep_sleep_indicator);

int syno_usb_eunit_disk_delay_waiting(const int slot_type, const int slot_index, const int disk_id, int spinup)
{
	struct device_node *of_eunit = NULL;
	int spinup_group_delay = 0, spinup_group = 0, group_num = 0, spinup_group_size = 0;
	int accum_waking_disks_available = 0, waken_disk_count = 0;
	const char *usb_port = NULL;
	struct acm *acm = NULL;
	int need_waiting = 0;
	unsigned long flags = 0;

	if (0 > slot_type || 0 >= slot_index || 0 > disk_id || 0 > spinup) {
		return -EINVAL;
	}

	if (0 > syno_acm_get_usb_port(&usb_port, slot_index)) {
		return -ENOENT;
	}

	if (NULL == (acm = syno_acm_get_by_usbport(usb_port))) {
		return -ENOENT;
	}

	if (NULL == (of_eunit = of_get_child_by_name(of_root, acm->cached_expstatus[EUNIT_STATUS_EXPIDSET]))) {
		return -ENODEV;
	}

	if (of_property_read_u32_index(of_eunit, DT_SYNO_SPINUP_GROUP_DELAY, 0, &spinup_group_delay)) {
		// do not support spinup_group_delay, no waiting need
		return need_waiting;
	}

	if (0 > (spinup_group_size = of_property_count_elems_of_size(of_eunit, DT_SYNO_SPINUP_GROUP, sizeof(u32)))) {
		// do not support spinup_group, no waiting need
		return need_waiting;
	}

	write_lock_irqsave(&acm->status_lock, flags);
	if (SPINUP_CHECK == spinup) {
		if (time_before(jiffies, acm->last_waking_time + spinup_group_delay*HZ) && 0 == (acm->waken_disks & (1 << disk_id))) {
			waken_disk_count = get_count_order(acm->waken_disks);
			for (group_num = 0; group_num < spinup_group_size && spinup == SPINUP_CHECK; group_num++) {
				of_property_read_u32_index(of_eunit, DT_SYNO_SPINUP_GROUP, group_num, &spinup_group);

				accum_waking_disks_available += spinup_group;
				if (accum_waking_disks_available == waken_disk_count) {
					// all previous waking up disks have occupy entire available waking up group
					// the previous group is full, this disk need to wait for delay
					spinup = SPINUP_DELAY;
				} else if (accum_waking_disks_available > waken_disk_count) {
					// this disk is in a waking up group, do not need delay
					spinup = SPINUP_NODELAY;
				}
			}
		} else {
			spinup = SPINUP_NODELAY;
		}
	}

	switch (spinup) {
	case SPINDOWN:
		acm->waken_disks &= ~(1 << disk_id);
		break;
	case SPINUP_CHECK:
		dev_info(&acm->control->dev, "waken disks status not checked, status should be SPINUP_NODELAY or SPINUP_DELAY\n");
		break;
	case SPINUP_NODELAY:
		acm->waken_disks |= (1 << disk_id);
		acm->last_waking_time = jiffies;
		break;
	case SPINUP_DELAY:
		need_waiting = 1;
		break;
	default:
		dev_info(&acm->control->dev, "%x is not a spinup opertion\n", spinup);
		break;
	}
	write_unlock_irqrestore(&acm->status_lock, flags);
	if (of_eunit) {
		of_node_put(of_eunit);
	}
	return need_waiting;
}

/**
* This function is used to check usb acm eunit hddpresent is on and hddctrl is on too
* of a slot.
*
* @param eunit_slot_type eunit slot type (should be EUNIT_DEVICE)
* @param eunit_slot_index eunit slot number
* @param disk_slot_index which disk slot to check
*
* @return  1: Need to wait (present is on and enable is off)
* 		   0: No need to wait
*/
int syno_usb_eunit_disk_is_wait_power_on(const int eunit_slot_type, const int eunit_slot_index, const int disk_slot_index)
{
	const char *usb_port = NULL;
	struct acm *acm = NULL;
	int iOffset = (disk_slot_index - 1) * 2;
	int iNeedWait = -1;

	if (0 > eunit_slot_type || 0 >= eunit_slot_index || 0 >= disk_slot_index) {
		iNeedWait = -EINVAL;
		goto END;
	}

	if (0 > syno_acm_get_usb_port(&usb_port, eunit_slot_index)) {
		iNeedWait = -ENOENT;
		goto END;
	}

	if (NULL == (acm = syno_acm_get_by_usbport(usb_port))) {
		iNeedWait = -ENOENT;
		goto END;
	}

	if ((acm->cached_expstatus[EUNIT_STATUS_HDDCTRL] && (iOffset < strlen(acm->cached_expstatus[EUNIT_STATUS_HDDCTRL])) ) &&
		(acm->cached_expstatus[EUNIT_STATUS_HDDPRESENT] && (iOffset < strlen(acm->cached_expstatus[EUNIT_STATUS_HDDPRESENT])) )) {
		if (('1' == acm->cached_expstatus[EUNIT_STATUS_HDDPRESENT][iOffset]) &
			('0' == acm->cached_expstatus[EUNIT_STATUS_HDDCTRL][iOffset])) {
			iNeedWait = 1;
		} else {
			iNeedWait = 0;
		}
	}

END:
	return iNeedWait;
}

int syno_usb_eunit_disk_present_check_by_uuid(
	const char *uuid, unsigned int slot)
{
	int i = 0;
	unsigned long flags = 0;
	char statusBuf[SYNO_EUNIT_STATUS_BUFFER_SIZE] = {0};
	char *sep_ptr = NULL;
	char *save_ptr = NULL;
	struct acm *acm = NULL;

	acm = syno_acm_get_by_uuid(uuid);
	if (acm == NULL || acm->cached_expstatus == NULL ||
		acm->cached_expstatus[EUNIT_STATUS_EXPIDSET] == NULL) {
		return -1;
	}

	read_lock_irqsave(&acm->status_lock, flags);
	snprintf(statusBuf, sizeof(statusBuf), "%s",
		acm->cached_expstatus[EUNIT_STATUS_HDDPRESENT]);
	read_unlock_irqrestore(&acm->status_lock, flags);

	save_ptr = statusBuf;
	for (i = 0; i < slot; i++) {
		sep_ptr = strsep(&save_ptr, "/");
		if (sep_ptr == NULL) {
			return -1;
		}
	}
	if (0 == strcmp(sep_ptr, "0")) {
		return 0;
	} else if (0 == strcmp(sep_ptr, "1")) {
		return 1;
	} else {
		return -1;
	}
}
EXPORT_SYMBOL(syno_usb_eunit_disk_present_check_by_uuid);

int syno_usb_eunit_disk_enable_check_by_uuid(
	const char *uuid, unsigned int slot)
{
	int i = 0;
	unsigned long flags = 0;
	char statusBuf[SYNO_EUNIT_STATUS_BUFFER_SIZE] = {0};
	char *sep_ptr = NULL;
	char *save_ptr = NULL;
	struct acm *acm = NULL;

	acm = syno_acm_get_by_uuid(uuid);
	if (acm == NULL || acm->cached_expstatus == NULL ||
		acm->cached_expstatus[EUNIT_STATUS_EXPIDSET] == NULL) {
		return -1;
	}

	read_lock_irqsave(&acm->status_lock, flags);
	snprintf(statusBuf, sizeof(statusBuf), "%s",
		acm->cached_expstatus[EUNIT_STATUS_HDDCTRL]);
	read_unlock_irqrestore(&acm->status_lock, flags);

	save_ptr = statusBuf;
	for (i = 0; i < slot; i++) {
		sep_ptr = strsep(&save_ptr, "/");
		if (sep_ptr == NULL) {
			return -1;
		}
	}
	if (0 == strcmp(sep_ptr, "0")) {
		return 0;
	} else if (0 == strcmp(sep_ptr, "1")) {
		return 1;
	} else {
		return -1;
	}
}
EXPORT_SYMBOL(syno_usb_eunit_disk_enable_check_by_uuid);

static int syno_acm_ready_check(struct acm *acm)
{
	int i = 0;
	int disk_count = 0;

	if (!acm) {
		return -1;
	}
	/* Retry! wait for usb return hddpresent and parse usb string*/
	for (i = 0; i < SYNO_EUNIT_ACM_WAITING_RESPONSE_RETRY; i++) {

		// read hdd present status by sending expstatus priorly
		acm_submit_read_urbs(acm, GFP_KERNEL);
		syno_samd_tty_write(acm, DT_EUNIT_STATUS_EXPSTATUS":", false, false);

		msleep(SYNO_EUNIT_ACM_WAITING_RESPONSE_DELAY_MS);

		if (!strcmp(acm->cached_expstatus[EUNIT_STATUS_EXPIDSET], SYNO_EUNIT_DEFAULT_UNIQUE_ID)) {
			/* if unique id is factory default id, we do not wait for disk present ready */
			dev_info(&acm->control->dev, "Default unique detected, skip ready check\n");
			return 0;
		}

		if (0 == strlen(acm->cached_expstatus[EUNIT_STATUS_HDDPRESENT])
			|| 0 == strlen(acm->cached_expstatus[EUNIT_STATUS_EXPIDSET])
			|| 0 == strlen(acm->cached_expstatus[EUNIT_STATUS_EXPCTRL])) {
			continue;
		}

		if ('1' != acm->cached_expstatus[EUNIT_STATUS_EXPCTRL][0]) {
			continue;
		}

		if (!disk_count) {
			disk_count = syno_usb_get_hdd_count(acm->cached_expstatus[EUNIT_STATUS_EXPIDSET]);
		}
		if (strlen(acm->cached_expstatus[EUNIT_STATUS_HDDPRESENT]) != (2 * disk_count)) {
			continue;
		}
		if (strchr(acm->cached_expstatus[EUNIT_STATUS_HDDPRESENT], SYNO_EUNIT_STATUS_ERROR)) {
			continue;
		}
		return 0;
	}
	return -1;
}

/**
 * syno_acm_adjust_disk_poweroff_seq - adjust disk poweroff sequence
 * @acm: acm device
 * @return 0: success
 * 	  -1: error
 * 
 * This function is used to adjust disk poweroff sequence
 * It match achi pcie root and ata_port of eunit node from dts
 * and reorder the power manage sequence
 * 
 * TODO: currently support ata eunit only. 
 * 			For pcie eunit, need to add pcie port device match (maybe by device drivers)
 */
static int syno_acm_adjust_disk_poweroff_seq(struct acm *acm)
{
	int ret = -1;
	struct device_node *device_node = NULL;
	int eunit_index = 0, slot_index = 0;

	struct ata_port *ap = NULL;
	struct klist_iter klist_iter;
	struct klist_node *ata_node = NULL;

	if (!acm) {
		goto END;
	}

	if (0 > syno_acm_slotindex_get(&slot_index, acm)) {
		goto END;
	}

	for_each_child_of_node(of_root, device_node) {
		if (!device_node->full_name) {
			continue;
		}

		/* Find eunit node */
		if (strstr(device_node->full_name, DT_ESATA_SLOT)) {
			sscanf(device_node->full_name, DT_ESATA_SLOT"@%d", &eunit_index);
		} else if (strstr(device_node->full_name, DT_CX4_SLOT)) {
			sscanf(device_node->full_name, DT_CX4_SLOT"@%d", &eunit_index);
		} else {
			continue;
		}

		if (eunit_index != slot_index) {
			continue;
		}

		/* Match ata port device */
		klist_iter_init(&syno_ata_port_head, &klist_iter);
		for (ata_node = klist_next(&klist_iter); NULL != ata_node; ata_node = klist_next(&klist_iter)) {
			ap = container_of(ata_node, struct ata_port, ata_port_list);
			if (!ap->ops->syno_compare_node_info || !ap->ops->syno_compare_node_info(ap, device_node)) {
				continue;
			}

			ata_port_dbg(ap, "reorder power manage sequence\n");
			device_pm_move_to_tail(ap->dev);
			ret = 0;
			break;
		}
		klist_iter_exit(&klist_iter);
	}

END:
	if (-1 == ret) {
		dev_dbg(&acm->control->dev, "%s no ahci node found\n", __func__);
	}
	return ret;
}

#endif /* MY_DEF_HERE */

static int acm_probe(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	struct usb_cdc_union_desc *union_header = NULL;
	struct usb_cdc_call_mgmt_descriptor *cmgmd = NULL;
	unsigned char *buffer = intf->altsetting->extra;
	int buflen = intf->altsetting->extralen;
	struct usb_interface *control_interface;
	struct usb_interface *data_interface;
	struct usb_endpoint_descriptor *epctrl = NULL;
	struct usb_endpoint_descriptor *epread = NULL;
	struct usb_endpoint_descriptor *epwrite = NULL;
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_cdc_parsed_header h;
	struct acm *acm;
	int minor;
	int ctrlsize, readsize;
	u8 *buf;
	int call_intf_num = -1;
	int data_intf_num = -1;
	unsigned long quirks;
	int num_rx_buf;
	int i;
	int combined_interfaces = 0;
	struct device *tty_dev;
	int rv = -ENOMEM;
	int res;
#ifdef MY_DEF_HERE
	struct syno_device_list *sdl = NULL;
	struct acm_device_temp *adt = NULL, *tmp = NULL;
	struct syno_acm_list *sal = NULL;
	unsigned long flags = 0;
	unsigned char tmp_uuid[16];
#endif /* MY_DEF_HERE */

	/* normal quirks */
	quirks = (unsigned long)id->driver_info;

	if (quirks == IGNORE_DEVICE)
		return -ENODEV;

	memset(&h, 0x00, sizeof(struct usb_cdc_parsed_header));

	num_rx_buf = (quirks == SINGLE_RX_URB) ? 1 : ACM_NR;

	/* handle quirks deadly to normal probing*/
	if (quirks == NO_UNION_NORMAL) {
		data_interface = usb_ifnum_to_if(usb_dev, 1);
		control_interface = usb_ifnum_to_if(usb_dev, 0);
		/* we would crash */
		if (!data_interface || !control_interface)
			return -ENODEV;
		goto skip_normal_probe;
	}

	/* normal probing*/
	if (!buffer) {
		dev_err(&intf->dev, "Weird descriptor references\n");
		return -EINVAL;
	}

	if (!buflen) {
		if (intf->cur_altsetting->endpoint &&
				intf->cur_altsetting->endpoint->extralen &&
				intf->cur_altsetting->endpoint->extra) {
			dev_dbg(&intf->dev,
				"Seeking extra descriptors on endpoint\n");
			buflen = intf->cur_altsetting->endpoint->extralen;
			buffer = intf->cur_altsetting->endpoint->extra;
		} else {
			dev_err(&intf->dev,
				"Zero length descriptor references\n");
			return -EINVAL;
		}
	}

	cdc_parse_cdc_header(&h, intf, buffer, buflen);
	union_header = h.usb_cdc_union_desc;
	cmgmd = h.usb_cdc_call_mgmt_descriptor;
	if (cmgmd)
		call_intf_num = cmgmd->bDataInterface;

	if (!union_header) {
		if (intf->cur_altsetting->desc.bNumEndpoints == 3) {
			dev_dbg(&intf->dev, "No union descriptor, assuming single interface\n");
			combined_interfaces = 1;
			control_interface = data_interface = intf;
			goto look_for_collapsed_interface;
		} else if (call_intf_num > 0) {
			dev_dbg(&intf->dev, "No union descriptor, using call management descriptor\n");
			data_intf_num = call_intf_num;
			data_interface = usb_ifnum_to_if(usb_dev, data_intf_num);
			control_interface = intf;
		} else {
			dev_dbg(&intf->dev, "No union descriptor, giving up\n");
			return -ENODEV;
		}
	} else {
		int class = -1;

		data_intf_num = union_header->bSlaveInterface0;
		control_interface = usb_ifnum_to_if(usb_dev, union_header->bMasterInterface0);
		data_interface = usb_ifnum_to_if(usb_dev, data_intf_num);

		if (control_interface)
			class = control_interface->cur_altsetting->desc.bInterfaceClass;

		if (class != USB_CLASS_COMM && class != USB_CLASS_CDC_DATA) {
			dev_dbg(&intf->dev, "Broken union descriptor, assuming single interface\n");
			combined_interfaces = 1;
			control_interface = data_interface = intf;
			goto look_for_collapsed_interface;
		}
	}

	if (!control_interface || !data_interface) {
		dev_dbg(&intf->dev, "no interfaces\n");
		return -ENODEV;
	}

	if (data_intf_num != call_intf_num)
		dev_dbg(&intf->dev, "Separate call control interface. That is not fully supported.\n");

	if (control_interface == data_interface) {
		/* some broken devices designed for windows work this way */
		dev_warn(&intf->dev,"Control and data interfaces are not separated!\n");
		combined_interfaces = 1;
		/* a popular other OS doesn't use it */
		quirks |= NO_CAP_LINE;
		if (data_interface->cur_altsetting->desc.bNumEndpoints != 3) {
			dev_err(&intf->dev, "This needs exactly 3 endpoints\n");
			return -EINVAL;
		}
look_for_collapsed_interface:
		res = usb_find_common_endpoints(data_interface->cur_altsetting,
				&epread, &epwrite, &epctrl, NULL);
		if (res)
			return res;

		goto made_compressed_probe;
	}

skip_normal_probe:

	/*workaround for switched interfaces */
	if (data_interface->cur_altsetting->desc.bInterfaceClass != USB_CLASS_CDC_DATA) {
		if (control_interface->cur_altsetting->desc.bInterfaceClass == USB_CLASS_CDC_DATA) {
			dev_dbg(&intf->dev,
				"Your device has switched interfaces.\n");
			swap(control_interface, data_interface);
		} else {
			return -EINVAL;
		}
	}

	/* Accept probe requests only for the control interface */
	if (!combined_interfaces && intf != control_interface)
		return -ENODEV;

	if (!combined_interfaces && usb_interface_claimed(data_interface)) {
		/* valid in this context */
		dev_dbg(&intf->dev, "The data interface isn't available\n");
		return -EBUSY;
	}


	if (data_interface->cur_altsetting->desc.bNumEndpoints < 2 ||
	    control_interface->cur_altsetting->desc.bNumEndpoints == 0)
		return -EINVAL;

	epctrl = &control_interface->cur_altsetting->endpoint[0].desc;
	epread = &data_interface->cur_altsetting->endpoint[0].desc;
	epwrite = &data_interface->cur_altsetting->endpoint[1].desc;


	/* workaround for switched endpoints */
	if (!usb_endpoint_dir_in(epread)) {
		/* descriptors are swapped */
		dev_dbg(&intf->dev,
			"The data interface has switched endpoints\n");
		swap(epread, epwrite);
	}
made_compressed_probe:
	dev_dbg(&intf->dev, "interfaces are valid\n");

	acm = kzalloc(sizeof(struct acm), GFP_KERNEL);
	if (acm == NULL)
		goto alloc_fail;

	tty_port_init(&acm->port);
	acm->port.ops = &acm_port_ops;

	ctrlsize = usb_endpoint_maxp(epctrl);
	readsize = usb_endpoint_maxp(epread) *
				(quirks == SINGLE_RX_URB ? 1 : 2);
	acm->combined_interfaces = combined_interfaces;
	acm->writesize = usb_endpoint_maxp(epwrite) * 20;
	acm->control = control_interface;
	acm->data = data_interface;

	usb_get_intf(acm->control); /* undone in destruct() */

	minor = acm_alloc_minor(acm);
	if (minor < 0)
		goto alloc_fail1;

	acm->minor = minor;
	acm->dev = usb_dev;
	if (h.usb_cdc_acm_descriptor)
		acm->ctrl_caps = h.usb_cdc_acm_descriptor->bmCapabilities;
	if (quirks & NO_CAP_LINE)
		acm->ctrl_caps &= ~USB_CDC_CAP_LINE;
	acm->ctrlsize = ctrlsize;
	acm->readsize = readsize;
	acm->rx_buflimit = num_rx_buf;
	INIT_DELAYED_WORK(&acm->dwork, acm_softint);
	init_waitqueue_head(&acm->wioctl);
	spin_lock_init(&acm->write_lock);
	spin_lock_init(&acm->read_lock);
	mutex_init(&acm->mutex);
	if (usb_endpoint_xfer_int(epread)) {
		acm->bInterval = epread->bInterval;
		acm->in = usb_rcvintpipe(usb_dev, epread->bEndpointAddress);
	} else {
		acm->in = usb_rcvbulkpipe(usb_dev, epread->bEndpointAddress);
	}
	if (usb_endpoint_xfer_int(epwrite))
		acm->out = usb_sndintpipe(usb_dev, epwrite->bEndpointAddress);
	else
		acm->out = usb_sndbulkpipe(usb_dev, epwrite->bEndpointAddress);
	init_usb_anchor(&acm->delayed);
	acm->quirks = quirks;

	buf = usb_alloc_coherent(usb_dev, ctrlsize, GFP_KERNEL, &acm->ctrl_dma);
	if (!buf)
		goto alloc_fail1;
	acm->ctrl_buffer = buf;

	if (acm_write_buffers_alloc(acm) < 0)
		goto alloc_fail2;

	acm->ctrlurb = usb_alloc_urb(0, GFP_KERNEL);
	if (!acm->ctrlurb)
		goto alloc_fail3;

	for (i = 0; i < num_rx_buf; i++) {
		struct acm_rb *rb = &(acm->read_buffers[i]);
		struct urb *urb;

		rb->base = usb_alloc_coherent(acm->dev, readsize, GFP_KERNEL,
								&rb->dma);
		if (!rb->base)
			goto alloc_fail4;
		rb->index = i;
		rb->instance = acm;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			goto alloc_fail4;

		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_dma = rb->dma;
		if (usb_endpoint_xfer_int(epread))
			usb_fill_int_urb(urb, acm->dev, acm->in, rb->base,
					 acm->readsize,
					 acm_read_bulk_callback, rb,
					 acm->bInterval);
		else
			usb_fill_bulk_urb(urb, acm->dev, acm->in, rb->base,
					  acm->readsize,
					  acm_read_bulk_callback, rb);

		acm->read_urbs[i] = urb;
		__set_bit(i, &acm->read_urbs_free);
	}
	for (i = 0; i < ACM_NW; i++) {
		struct acm_wb *snd = &(acm->wb[i]);

		snd->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (snd->urb == NULL)
			goto alloc_fail5;

		if (usb_endpoint_xfer_int(epwrite))
			usb_fill_int_urb(snd->urb, usb_dev, acm->out,
				NULL, acm->writesize, acm_write_bulk, snd, epwrite->bInterval);
		else
			usb_fill_bulk_urb(snd->urb, usb_dev, acm->out,
				NULL, acm->writesize, acm_write_bulk, snd);
		snd->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		if (quirks & SEND_ZERO_PACKET)
			snd->urb->transfer_flags |= URB_ZERO_PACKET;
		snd->instance = acm;
	}

	usb_set_intfdata(intf, acm);

	i = device_create_file(&intf->dev, &dev_attr_bmCapabilities);
	if (i < 0)
		goto alloc_fail5;

	if (h.usb_cdc_country_functional_desc) { /* export the country data */
		struct usb_cdc_country_functional_desc * cfd =
					h.usb_cdc_country_functional_desc;

		acm->country_codes = kmalloc(cfd->bLength - 4, GFP_KERNEL);
		if (!acm->country_codes)
			goto skip_countries;
		acm->country_code_size = cfd->bLength - 4;
		memcpy(acm->country_codes, (u8 *)&cfd->wCountyCode0,
							cfd->bLength - 4);
		acm->country_rel_date = cfd->iCountryCodeRelDate;

		i = device_create_file(&intf->dev, &dev_attr_wCountryCodes);
		if (i < 0) {
			kfree(acm->country_codes);
			acm->country_codes = NULL;
			acm->country_code_size = 0;
			goto skip_countries;
		}

		i = device_create_file(&intf->dev,
						&dev_attr_iCountryCodeRelDate);
		if (i < 0) {
			device_remove_file(&intf->dev, &dev_attr_wCountryCodes);
			kfree(acm->country_codes);
			acm->country_codes = NULL;
			acm->country_code_size = 0;
			goto skip_countries;
		}
	}

skip_countries:
	usb_fill_int_urb(acm->ctrlurb, usb_dev,
			 usb_rcvintpipe(usb_dev, epctrl->bEndpointAddress),
			 acm->ctrl_buffer, ctrlsize, acm_ctrl_irq, acm,
			 /* works around buggy devices */
			 epctrl->bInterval ? epctrl->bInterval : 16);
	acm->ctrlurb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	acm->ctrlurb->transfer_dma = acm->ctrl_dma;
	acm->notification_buffer = NULL;
	acm->nb_index = 0;
	acm->nb_size = 0;

	dev_info(&intf->dev, "ttyACM%d: USB ACM device\n", minor);

	acm->line.dwDTERate = cpu_to_le32(9600);
	acm->line.bDataBits = 8;
	acm_set_line(acm, &acm->line);

	usb_driver_claim_interface(&acm_driver, data_interface, acm);
	usb_set_intfdata(data_interface, acm);

#ifdef MY_DEF_HERE
	if (syno_is_synology_acm(acm)) {
		rwlock_init(&acm->status_lock);
		generate_random_uuid(tmp_uuid);
		snprintf(acm->uuid, sizeof(acm->uuid), "%pU", tmp_uuid);

		spin_lock_init(&acm->syno_acm_q_lock);
		acm->last_poll_jiffies = 0;
		acm->cmd_idx_head = 1;
		acm->cmd_idx_tail = 1;

		spin_lock_irqsave(&acm_list_lock, flags);
		INIT_LIST_HEAD(&acm->syno_device_list);
		sal = kzalloc(sizeof(*sal), GFP_ATOMIC);
		if (!sal) {
			spin_unlock_irqrestore(&acm_list_lock, flags);
			goto alloc_fail6;
		}
		sal->acm = acm;
		spin_unlock_irqrestore(&acm_list_lock, flags);

		acm->acm_buffer = kzalloc(sizeof(char)*SYNO_EUNIT_STATUS_BUFFER_SIZE, GFP_KERNEL);
		if (!acm->acm_buffer) {
			goto alloc_fail6;
		}
		acm->cached_expstatus = kzalloc(sizeof(char *) * EUNIT_STATUS_INDEX_END, GFP_KERNEL);
		if (!acm->cached_expstatus) {
			goto alloc_fail6;
		}

		for (i = 0; i < EUNIT_STATUS_INDEX_END; i++) {
			acm->cached_expstatus[i] = kzalloc(sizeof(char)*SYNO_DTS_PROPERTY_CONTENT_LENGTH, GFP_KERNEL);
			if (!acm->cached_expstatus[i]) {
				goto alloc_fail6;
			}
		}
		INIT_DELAYED_WORK(&acm->cache_update_work, syno_period_cache_update);
		INIT_DELAYED_WORK(&acm->cmd_issue_work, syno_period_cmd_issue);
		INIT_LIST_HEAD(&acm->syno_acm_q); /* Init queue for commands */

		acm_submit_read_urbs(acm, GFP_KERNEL);
		//TODO: macro
		syno_samd_tty_write(acm, DT_EUNIT_STATUS_EXPCTRL":1/", false, false);

		syno_period_cmd_issue(&acm->cmd_issue_work.work);

		syno_create_eunit_files(&intf->dev);

		syno_acm_ready_check(acm);

		syno_period_cache_update(&acm->cache_update_work.work);

		spin_lock_irqsave(&acm_list_lock, flags);
		list_add(&sal->device_list, &syno_acm_list_head);
		list_for_each_entry_safe(adt, tmp, &acm_temp_device_list, device_list) {
			if (0 == strcmp(adt->usb_path, dev_name(&acm->dev->dev))) {
				sdl = kzalloc(sizeof(*sdl), GFP_ATOMIC);
				if (!sdl) {
					continue;
				}
				snprintf(sdl->disk_name, DISK_NAME_LEN, "%s", adt->disk_name);
				list_add(&sdl->device_list, &acm->syno_device_list);
			}
		}
		spin_unlock_irqrestore(&acm_list_lock, flags);

		syno_acm_adjust_disk_poweroff_seq(acm);
	}
#endif /* MY_DEF_HERE */

	tty_dev = tty_port_register_device(&acm->port, acm_tty_driver, minor,
			&control_interface->dev);
	if (IS_ERR(tty_dev)) {
		rv = PTR_ERR(tty_dev);
		goto alloc_fail6;
	}

	if (quirks & CLEAR_HALT_CONDITIONS) {
		usb_clear_halt(usb_dev, acm->in);
		usb_clear_halt(usb_dev, acm->out);
	}

	return 0;
alloc_fail6:
	if (!acm->combined_interfaces) {
		/* Clear driver data so that disconnect() returns early. */
		usb_set_intfdata(data_interface, NULL);
		usb_driver_release_interface(&acm_driver, data_interface);
	}

#ifdef MY_DEF_HERE
	if (syno_is_synology_acm(acm)) {
		cancel_delayed_work_sync(&acm->cache_update_work);
		cancel_delayed_work_sync(&acm->cmd_issue_work);
		syno_remove_eunit_files(&acm->control->dev);

		spin_lock_irqsave(&acm_list_lock, flags);
		if (sal) {
			list_del(&sal->device_list);
			kfree(sal);
		}
		spin_unlock_irqrestore(&acm_list_lock, flags);

		if (acm->acm_buffer) {
			kfree(acm->acm_buffer);
		}

		if (acm->cached_expstatus) {
			for (i = 0; i < EUNIT_STATUS_INDEX_END; i++) {
				if (acm->cached_expstatus[i]) {
					kfree(acm->cached_expstatus[i]);
				}
			}
			kfree(acm->cached_expstatus);
		}
	}
#endif /* MY_DEF_HERE */

	if (acm->country_codes) {
		device_remove_file(&acm->control->dev,
				&dev_attr_wCountryCodes);
		device_remove_file(&acm->control->dev,
				&dev_attr_iCountryCodeRelDate);
	}
	device_remove_file(&acm->control->dev, &dev_attr_bmCapabilities);
alloc_fail5:
	usb_set_intfdata(intf, NULL);
	for (i = 0; i < ACM_NW; i++)
		usb_free_urb(acm->wb[i].urb);
alloc_fail4:
	for (i = 0; i < num_rx_buf; i++)
		usb_free_urb(acm->read_urbs[i]);
	acm_read_buffers_free(acm);
	usb_free_urb(acm->ctrlurb);
alloc_fail3:
	acm_write_buffers_free(acm);
alloc_fail2:
	usb_free_coherent(usb_dev, ctrlsize, acm->ctrl_buffer, acm->ctrl_dma);
alloc_fail1:
	tty_port_put(&acm->port);
alloc_fail:
	return rv;
}

static void acm_disconnect(struct usb_interface *intf)
{
	struct acm *acm = usb_get_intfdata(intf);
	struct tty_struct *tty;
	int i;
#ifdef MY_DEF_HERE
	struct syno_acm_list *sal = NULL, *sal_tmp = NULL;
	unsigned long flags = 0;
#endif /* MY_DEF_HERE */

	/* sibling interface is already cleaning up */
	if (!acm)
		return;

	acm->disconnected = true;

#ifdef MY_DEF_HERE
	mutex_lock(&acm->mutex);
	if (syno_is_synology_acm(acm)) {
		spin_lock_irqsave(&acm_list_lock, flags);
		list_for_each_entry_safe(sal, sal_tmp, &syno_acm_list_head, device_list) {
			if (sal->acm == acm) {
				list_del(&sal->device_list);
				kfree(sal);
			}
		}
		spin_unlock_irqrestore(&acm_list_lock, flags);

		if (acm->acm_buffer) {
			kfree(acm->acm_buffer);
		}
		cancel_delayed_work_sync(&acm->cache_update_work);
		cancel_delayed_work_sync(&acm->cmd_issue_work);
		syno_remove_eunit_files(&acm->control->dev);
		for (i = 0; i < EUNIT_STATUS_INDEX_END; i++) {
			if (acm->cached_expstatus[i]) {
				kfree(acm->cached_expstatus[i]);
			}
		}
		if (acm->cached_expstatus) {
			kfree(acm->cached_expstatus);
		}
		//TODO: macro
		if (SYSTEM_POWER_OFF == system_state) {
			syno_samd_tty_write_raw(acm, DT_EUNIT_STATUS_EXPCTRL":0/");
			for (i = 0; i < SYNO_EUNIT_ACM_WAITING_TRANSMIT_RETRY; i++) {
				if (0 == acm->transmitting) {
					break;
				}
				msleep(SYNO_EUNIT_ACM_WAITING_TRANSMIT_DELAY_MS);
			}
		}
	}
	mutex_unlock(&acm->mutex);
#endif /* MY_DEF_HERE */

	/*
	 * there is a circular dependency. acm_softint() can resubmit
	 * the URBs in error handling so we need to block any
	 * submission right away
	 */
	acm_poison_urbs(acm);
	mutex_lock(&acm->mutex);
	if (acm->country_codes) {
		device_remove_file(&acm->control->dev,
				&dev_attr_wCountryCodes);
		device_remove_file(&acm->control->dev,
				&dev_attr_iCountryCodeRelDate);
	}
	wake_up_all(&acm->wioctl);
	device_remove_file(&acm->control->dev, &dev_attr_bmCapabilities);
	usb_set_intfdata(acm->control, NULL);
	usb_set_intfdata(acm->data, NULL);
	mutex_unlock(&acm->mutex);

	tty = tty_port_tty_get(&acm->port);
	if (tty) {
		tty_vhangup(tty);
		tty_kref_put(tty);
	}

	cancel_delayed_work_sync(&acm->dwork);

	tty_unregister_device(acm_tty_driver, acm->minor);

	usb_free_urb(acm->ctrlurb);
	for (i = 0; i < ACM_NW; i++)
		usb_free_urb(acm->wb[i].urb);
	for (i = 0; i < acm->rx_buflimit; i++)
		usb_free_urb(acm->read_urbs[i]);
	acm_write_buffers_free(acm);
	usb_free_coherent(acm->dev, acm->ctrlsize, acm->ctrl_buffer, acm->ctrl_dma);
	acm_read_buffers_free(acm);

	kfree(acm->notification_buffer);

	if (!acm->combined_interfaces)
		usb_driver_release_interface(&acm_driver, intf == acm->control ?
					acm->data : acm->control);

	tty_port_put(&acm->port);
}

#ifdef CONFIG_PM
static int acm_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct acm *acm = usb_get_intfdata(intf);
	int cnt;

	spin_lock_irq(&acm->write_lock);
	if (PMSG_IS_AUTO(message)) {
		if (acm->transmitting) {
			spin_unlock_irq(&acm->write_lock);
			return -EBUSY;
		}
	}
	cnt = acm->susp_count++;
	spin_unlock_irq(&acm->write_lock);

	if (cnt)
		return 0;

	acm_poison_urbs(acm);
	cancel_delayed_work_sync(&acm->dwork);
	acm->urbs_in_error_delay = 0;

	return 0;
}

static int acm_resume(struct usb_interface *intf)
{
	struct acm *acm = usb_get_intfdata(intf);
	struct urb *urb;
	int rv = 0;

	spin_lock_irq(&acm->write_lock);

	if (--acm->susp_count)
		goto out;

	acm_unpoison_urbs(acm);

	if (tty_port_initialized(&acm->port)) {
		rv = usb_submit_urb(acm->ctrlurb, GFP_ATOMIC);

		for (;;) {
			urb = usb_get_from_anchor(&acm->delayed);
			if (!urb)
				break;

			acm_start_wb(acm, urb->context);
		}

		/*
		 * delayed error checking because we must
		 * do the write path at all cost
		 */
		if (rv < 0)
			goto out;

		rv = acm_submit_read_urbs(acm, GFP_ATOMIC);
	}
out:
	spin_unlock_irq(&acm->write_lock);

	return rv;
}

static int acm_reset_resume(struct usb_interface *intf)
{
	struct acm *acm = usb_get_intfdata(intf);

	if (tty_port_initialized(&acm->port))
		tty_port_tty_hangup(&acm->port, false);

	return acm_resume(intf);
}

#endif /* CONFIG_PM */

static int acm_pre_reset(struct usb_interface *intf)
{
	struct acm *acm = usb_get_intfdata(intf);

	clear_bit(EVENT_RX_STALL, &acm->flags);
	acm->nb_index = 0; /* pending control transfers are lost */

	return 0;
}

#define NOKIA_PCSUITE_ACM_INFO(x) \
		USB_DEVICE_AND_INTERFACE_INFO(0x0421, x, \
		USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM, \
		USB_CDC_ACM_PROTO_VENDOR)

#define SAMSUNG_PCSUITE_ACM_INFO(x) \
		USB_DEVICE_AND_INTERFACE_INFO(0x04e7, x, \
		USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM, \
		USB_CDC_ACM_PROTO_VENDOR)

/*
 * USB driver structure.
 */

static const struct usb_device_id acm_ids[] = {
	/* quirky and broken devices */
	{ USB_DEVICE(0x0424, 0x274e), /* Microchip Technology, Inc. (formerly SMSC) */
	  .driver_info = DISABLE_ECHO, }, /* DISABLE ECHO in termios flag */
	{ USB_DEVICE(0x076d, 0x0006), /* Denso Cradle CU-321 */
	.driver_info = NO_UNION_NORMAL, },/* has no union descriptor */
	{ USB_DEVICE(0x17ef, 0x7000), /* Lenovo USB modem */
	.driver_info = NO_UNION_NORMAL, },/* has no union descriptor */
	{ USB_DEVICE(0x0870, 0x0001), /* Metricom GS Modem */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x045b, 0x023c),	/* Renesas USB Download mode */
	.driver_info = DISABLE_ECHO,	/* Don't echo banner */
	},
	{ USB_DEVICE(0x045b, 0x0248),	/* Renesas USB Download mode */
	.driver_info = DISABLE_ECHO,	/* Don't echo banner */
	},
	{ USB_DEVICE(0x045b, 0x024D),	/* Renesas USB Download mode */
	.driver_info = DISABLE_ECHO,	/* Don't echo banner */
	},
	{ USB_DEVICE(0x0e8d, 0x0003), /* FIREFLY, MediaTek Inc; andrey.arapov@gmail.com */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x0e8d, 0x2000), /* MediaTek Inc Preloader */
	.driver_info = DISABLE_ECHO, /* DISABLE ECHO in termios flag */
	},
	{ USB_DEVICE(0x0e8d, 0x3329), /* MediaTek Inc GPS */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x0482, 0x0203), /* KYOCERA AH-K3001V */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x079b, 0x000f), /* BT On-Air USB MODEM */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x0ace, 0x1602), /* ZyDAS 56K USB MODEM */
	.driver_info = SINGLE_RX_URB,
	},
	{ USB_DEVICE(0x0ace, 0x1608), /* ZyDAS 56K USB MODEM */
	.driver_info = SINGLE_RX_URB, /* firmware bug */
	},
	{ USB_DEVICE(0x0ace, 0x1611), /* ZyDAS 56K USB MODEM - new version */
	.driver_info = SINGLE_RX_URB, /* firmware bug */
	},
	{ USB_DEVICE(0x11ca, 0x0201), /* VeriFone Mx870 Gadget Serial */
	.driver_info = SINGLE_RX_URB,
	},
	{ USB_DEVICE(0x1965, 0x0018), /* Uniden UBC125XLT */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x22b8, 0x7000), /* Motorola Q Phone */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x0803, 0x3095), /* Zoom Telephonics Model 3095F USB MODEM */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x0572, 0x1321), /* Conexant USB MODEM CX93010 */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x0572, 0x1324), /* Conexant USB MODEM RD02-D400 */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x0572, 0x1328), /* Shiro / Aztech USB MODEM UM-3100 */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x0572, 0x1349), /* Hiro (Conexant) USB MODEM H50228 */
	.driver_info = NO_UNION_NORMAL, /* has no union descriptor */
	},
	{ USB_DEVICE(0x20df, 0x0001), /* Simtec Electronics Entropy Key */
	.driver_info = QUIRK_CONTROL_LINE_STATE, },
	{ USB_DEVICE(0x2184, 0x001c) },	/* GW Instek AFG-2225 */
	{ USB_DEVICE(0x2184, 0x0036) },	/* GW Instek AFG-125 */
	{ USB_DEVICE(0x22b8, 0x6425), /* Motorola MOTOMAGX phones */
	},
	/* Motorola H24 HSPA module: */
	{ USB_DEVICE(0x22b8, 0x2d91) }, /* modem                                */
	{ USB_DEVICE(0x22b8, 0x2d92),   /* modem           + diagnostics        */
	.driver_info = NO_UNION_NORMAL, /* handle only modem interface          */
	},
	{ USB_DEVICE(0x22b8, 0x2d93),   /* modem + AT port                      */
	.driver_info = NO_UNION_NORMAL, /* handle only modem interface          */
	},
	{ USB_DEVICE(0x22b8, 0x2d95),   /* modem + AT port + diagnostics        */
	.driver_info = NO_UNION_NORMAL, /* handle only modem interface          */
	},
	{ USB_DEVICE(0x22b8, 0x2d96),   /* modem                         + NMEA */
	.driver_info = NO_UNION_NORMAL, /* handle only modem interface          */
	},
	{ USB_DEVICE(0x22b8, 0x2d97),   /* modem           + diagnostics + NMEA */
	.driver_info = NO_UNION_NORMAL, /* handle only modem interface          */
	},
	{ USB_DEVICE(0x22b8, 0x2d99),   /* modem + AT port               + NMEA */
	.driver_info = NO_UNION_NORMAL, /* handle only modem interface          */
	},
	{ USB_DEVICE(0x22b8, 0x2d9a),   /* modem + AT port + diagnostics + NMEA */
	.driver_info = NO_UNION_NORMAL, /* handle only modem interface          */
	},

	{ USB_DEVICE(0x0572, 0x1329), /* Hummingbird huc56s (Conexant) */
	.driver_info = NO_UNION_NORMAL, /* union descriptor misplaced on
					   data interface instead of
					   communications interface.
					   Maybe we should define a new
					   quirk for this. */
	},
	{ USB_DEVICE(0x0572, 0x1340), /* Conexant CX93010-2x UCMxx */
	.driver_info = NO_UNION_NORMAL,
	},
	{ USB_DEVICE(0x05f9, 0x4002), /* PSC Scanning, Magellan 800i */
	.driver_info = NO_UNION_NORMAL,
	},
	{ USB_DEVICE(0x1bbb, 0x0003), /* Alcatel OT-I650 */
	.driver_info = NO_UNION_NORMAL, /* reports zero length descriptor */
	},
	{ USB_DEVICE(0x1576, 0x03b1), /* Maretron USB100 */
	.driver_info = NO_UNION_NORMAL, /* reports zero length descriptor */
	},
	{ USB_DEVICE(0xfff0, 0x0100), /* DATECS FP-2000 */
	.driver_info = NO_UNION_NORMAL, /* reports zero length descriptor */
	},
	{ USB_DEVICE(0x09d8, 0x0320), /* Elatec GmbH TWN3 */
	.driver_info = NO_UNION_NORMAL, /* has misplaced union descriptor */
	},
	{ USB_DEVICE(0x0ca6, 0xa050), /* Castles VEGA3000 */
	.driver_info = NO_UNION_NORMAL, /* reports zero length descriptor */
	},

	{ USB_DEVICE(0x2912, 0x0001), /* ATOL FPrint */
	.driver_info = CLEAR_HALT_CONDITIONS,
	},

	/* Nokia S60 phones expose two ACM channels. The first is
	 * a modem and is picked up by the standard AT-command
	 * information below. The second is 'vendor-specific' but
	 * is treated as a serial device at the S60 end, so we want
	 * to expose it on Linux too. */
	{ NOKIA_PCSUITE_ACM_INFO(0x042D), }, /* Nokia 3250 */
	{ NOKIA_PCSUITE_ACM_INFO(0x04D8), }, /* Nokia 5500 Sport */
	{ NOKIA_PCSUITE_ACM_INFO(0x04C9), }, /* Nokia E50 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0419), }, /* Nokia E60 */
	{ NOKIA_PCSUITE_ACM_INFO(0x044D), }, /* Nokia E61 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0001), }, /* Nokia E61i */
	{ NOKIA_PCSUITE_ACM_INFO(0x0475), }, /* Nokia E62 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0508), }, /* Nokia E65 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0418), }, /* Nokia E70 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0425), }, /* Nokia N71 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0486), }, /* Nokia N73 */
	{ NOKIA_PCSUITE_ACM_INFO(0x04DF), }, /* Nokia N75 */
	{ NOKIA_PCSUITE_ACM_INFO(0x000e), }, /* Nokia N77 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0445), }, /* Nokia N80 */
	{ NOKIA_PCSUITE_ACM_INFO(0x042F), }, /* Nokia N91 & N91 8GB */
	{ NOKIA_PCSUITE_ACM_INFO(0x048E), }, /* Nokia N92 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0420), }, /* Nokia N93 */
	{ NOKIA_PCSUITE_ACM_INFO(0x04E6), }, /* Nokia N93i  */
	{ NOKIA_PCSUITE_ACM_INFO(0x04B2), }, /* Nokia 5700 XpressMusic */
	{ NOKIA_PCSUITE_ACM_INFO(0x0134), }, /* Nokia 6110 Navigator (China) */
	{ NOKIA_PCSUITE_ACM_INFO(0x046E), }, /* Nokia 6110 Navigator */
	{ NOKIA_PCSUITE_ACM_INFO(0x002f), }, /* Nokia 6120 classic &  */
	{ NOKIA_PCSUITE_ACM_INFO(0x0088), }, /* Nokia 6121 classic */
	{ NOKIA_PCSUITE_ACM_INFO(0x00fc), }, /* Nokia 6124 classic */
	{ NOKIA_PCSUITE_ACM_INFO(0x0042), }, /* Nokia E51 */
	{ NOKIA_PCSUITE_ACM_INFO(0x00b0), }, /* Nokia E66 */
	{ NOKIA_PCSUITE_ACM_INFO(0x00ab), }, /* Nokia E71 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0481), }, /* Nokia N76 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0007), }, /* Nokia N81 & N81 8GB */
	{ NOKIA_PCSUITE_ACM_INFO(0x0071), }, /* Nokia N82 */
	{ NOKIA_PCSUITE_ACM_INFO(0x04F0), }, /* Nokia N95 & N95-3 NAM */
	{ NOKIA_PCSUITE_ACM_INFO(0x0070), }, /* Nokia N95 8GB  */
	{ NOKIA_PCSUITE_ACM_INFO(0x00e9), }, /* Nokia 5320 XpressMusic */
	{ NOKIA_PCSUITE_ACM_INFO(0x0099), }, /* Nokia 6210 Navigator, RM-367 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0128), }, /* Nokia 6210 Navigator, RM-419 */
	{ NOKIA_PCSUITE_ACM_INFO(0x008f), }, /* Nokia 6220 Classic */
	{ NOKIA_PCSUITE_ACM_INFO(0x00a0), }, /* Nokia 6650 */
	{ NOKIA_PCSUITE_ACM_INFO(0x007b), }, /* Nokia N78 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0094), }, /* Nokia N85 */
	{ NOKIA_PCSUITE_ACM_INFO(0x003a), }, /* Nokia N96 & N96-3  */
	{ NOKIA_PCSUITE_ACM_INFO(0x00e9), }, /* Nokia 5320 XpressMusic */
	{ NOKIA_PCSUITE_ACM_INFO(0x0108), }, /* Nokia 5320 XpressMusic 2G */
	{ NOKIA_PCSUITE_ACM_INFO(0x01f5), }, /* Nokia N97, RM-505 */
	{ NOKIA_PCSUITE_ACM_INFO(0x02e3), }, /* Nokia 5230, RM-588 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0178), }, /* Nokia E63 */
	{ NOKIA_PCSUITE_ACM_INFO(0x010e), }, /* Nokia E75 */
	{ NOKIA_PCSUITE_ACM_INFO(0x02d9), }, /* Nokia 6760 Slide */
	{ NOKIA_PCSUITE_ACM_INFO(0x01d0), }, /* Nokia E52 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0223), }, /* Nokia E72 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0275), }, /* Nokia X6 */
	{ NOKIA_PCSUITE_ACM_INFO(0x026c), }, /* Nokia N97 Mini */
	{ NOKIA_PCSUITE_ACM_INFO(0x0154), }, /* Nokia 5800 XpressMusic */
	{ NOKIA_PCSUITE_ACM_INFO(0x04ce), }, /* Nokia E90 */
	{ NOKIA_PCSUITE_ACM_INFO(0x01d4), }, /* Nokia E55 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0302), }, /* Nokia N8 */
	{ NOKIA_PCSUITE_ACM_INFO(0x0335), }, /* Nokia E7 */
	{ NOKIA_PCSUITE_ACM_INFO(0x03cd), }, /* Nokia C7 */
	{ SAMSUNG_PCSUITE_ACM_INFO(0x6651), }, /* Samsung GTi8510 (INNOV8) */

	/* Support for Owen devices */
	{ USB_DEVICE(0x03eb, 0x0030), }, /* Owen SI30 */

	/* NOTE: non-Nokia COMM/ACM/0xff is likely MSFT RNDIS... NOT a modem! */

#if IS_ENABLED(CONFIG_INPUT_IMS_PCU)
	{ USB_DEVICE(0x04d8, 0x0082),	/* Application mode */
	.driver_info = IGNORE_DEVICE,
	},
	{ USB_DEVICE(0x04d8, 0x0083),	/* Bootloader mode */
	.driver_info = IGNORE_DEVICE,
	},
#endif

#if IS_ENABLED(CONFIG_IR_TOY)
	{ USB_DEVICE(0x04d8, 0xfd08),
	.driver_info = IGNORE_DEVICE,
	},

	{ USB_DEVICE(0x04d8, 0xf58b),
	.driver_info = IGNORE_DEVICE,
	},
#endif

	/*Samsung phone in firmware update mode */
	{ USB_DEVICE(0x04e8, 0x685d),
	.driver_info = IGNORE_DEVICE,
	},

	/* Exclude Infineon Flash Loader utility */
	{ USB_DEVICE(0x058b, 0x0041),
	.driver_info = IGNORE_DEVICE,
	},

	/* Exclude ETAS ES58x */
	{ USB_DEVICE(0x108c, 0x0159), /* ES581.4 */
	.driver_info = IGNORE_DEVICE,
	},
	{ USB_DEVICE(0x108c, 0x0168), /* ES582.1 */
	.driver_info = IGNORE_DEVICE,
	},
	{ USB_DEVICE(0x108c, 0x0169), /* ES584.1 */
	.driver_info = IGNORE_DEVICE,
	},

	{ USB_DEVICE(0x1bc7, 0x0021), /* Telit 3G ACM only composition */
	.driver_info = SEND_ZERO_PACKET,
	},
	{ USB_DEVICE(0x1bc7, 0x0023), /* Telit 3G ACM + ECM composition */
	.driver_info = SEND_ZERO_PACKET,
	},

	/* Exclude Goodix Fingerprint Reader */
	{ USB_DEVICE(0x27c6, 0x5395),
	.driver_info = IGNORE_DEVICE,
	},

	/* Exclude Heimann Sensor GmbH USB appset demo */
	{ USB_DEVICE(0x32a7, 0x0000),
	.driver_info = IGNORE_DEVICE,
	},

#ifdef MY_DEF_HERE
	{ USB_DEVICE(0x4d8, 0xa),
	.driver_info = DISABLE_ECHO, /* DISABLE ECHO in termios flag */
	},
#endif /* MY_DEF_HERE */

	/* control interfaces without any protocol set */
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_PROTO_NONE) },

	/* control interfaces with various AT-command sets */
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_V25TER) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_PCCA101) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_PCCA101_WAKE) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_GSM) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_3G) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_CDMA) },

	{ USB_DEVICE(0x1519, 0x0452), /* Intel 7260 modem */
	.driver_info = SEND_ZERO_PACKET,
	},

	{ }
};

MODULE_DEVICE_TABLE(usb, acm_ids);

static struct usb_driver acm_driver = {
	.name =		"cdc_acm",
	.probe =	acm_probe,
	.disconnect =	acm_disconnect,
#ifdef CONFIG_PM
	.suspend =	acm_suspend,
	.resume =	acm_resume,
	.reset_resume =	acm_reset_resume,
#endif
	.pre_reset =	acm_pre_reset,
	.id_table =	acm_ids,
#ifdef CONFIG_PM
	.supports_autosuspend = 1,
#endif
	.disable_hub_initiated_lpm = 1,
};

/*
 * TTY driver structures.
 */

static const struct tty_operations acm_ops = {
	.install =		acm_tty_install,
	.open =			acm_tty_open,
	.close =		acm_tty_close,
	.cleanup =		acm_tty_cleanup,
	.hangup =		acm_tty_hangup,
	.write =		acm_tty_write,
	.write_room =		acm_tty_write_room,
	.ioctl =		acm_tty_ioctl,
	.throttle =		acm_tty_throttle,
	.unthrottle =		acm_tty_unthrottle,
	.chars_in_buffer =	acm_tty_chars_in_buffer,
	.break_ctl =		acm_tty_break_ctl,
	.set_termios =		acm_tty_set_termios,
	.tiocmget =		acm_tty_tiocmget,
	.tiocmset =		acm_tty_tiocmset,
	.get_serial =		get_serial_info,
	.set_serial =		set_serial_info,
	.get_icount =		acm_tty_get_icount,
};

/*
 * Init / exit.
 */

static int __init acm_init(void)
{
	int retval;
	acm_tty_driver = alloc_tty_driver(ACM_TTY_MINORS);
	if (!acm_tty_driver)
		return -ENOMEM;
	acm_tty_driver->driver_name = "acm",
	acm_tty_driver->name = "ttyACM",
	acm_tty_driver->major = ACM_TTY_MAJOR,
	acm_tty_driver->minor_start = 0,
	acm_tty_driver->type = TTY_DRIVER_TYPE_SERIAL,
	acm_tty_driver->subtype = SERIAL_TYPE_NORMAL,
	acm_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	acm_tty_driver->init_termios = tty_std_termios;
	acm_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD |
								HUPCL | CLOCAL;
	tty_set_operations(acm_tty_driver, &acm_ops);

	retval = tty_register_driver(acm_tty_driver);
	if (retval) {
		put_tty_driver(acm_tty_driver);
		return retval;
	}

	retval = usb_register(&acm_driver);
	if (retval) {
		tty_unregister_driver(acm_tty_driver);
		put_tty_driver(acm_tty_driver);
		return retval;
	}

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_DESC "\n");

	return 0;
}

static void __exit acm_exit(void)
{
	usb_deregister(&acm_driver);
	tty_unregister_driver(acm_tty_driver);
	put_tty_driver(acm_tty_driver);
	idr_destroy(&acm_minors);
}

module_init(acm_init);
module_exit(acm_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(ACM_TTY_MAJOR);
