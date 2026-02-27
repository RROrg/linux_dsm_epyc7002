#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2024 Synology Inc. All rights reserved.

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#include "dpm_int.h"

extern int SYNO_CTRL_HDD_POWERON(int index, int value);
extern int SYNO_CHECK_HDD_ENABLE(int index);
#ifdef MY_ABC_HERE
extern int SYNO_CHECK_HDD_DETECT(int index);
#else
extern int SYNO_CHECK_HDD_PRESENT(int index);
#endif /* MY_ABC_HERE */
#ifdef MY_DEF_HERE
extern int syno_usb_eunit_single_hdd_ctrl_by_uuid(
		const char *uuid, unsigned int slot, int hdd_ctrl);
extern int syno_usb_eunit_disk_present_check_by_uuid(
		const char *uuid, unsigned int slot);
extern int syno_usb_eunit_disk_enable_check_by_uuid(
		const char *uuid, unsigned int slot);
#endif

struct disk_pwr_work
{
	int slot;
	int argc;
	char uuid[SYNO_DPM_UUID_LEN_MAX];
	char* ctl_args[MAX_CTR_ARGS];
	bool power_on;
	struct work_struct work;
};

static inline void free_disk_pwr_work(struct disk_pwr_work *work)
{
	int i = 0;
	if (work) {
		for (i = 0; i < MAX_CTR_ARGS; i++) {
			kfree(work->ctl_args[i]);
		}
		kfree(work);
	}
}

static void host_power_handler(struct work_struct *work)
{
	struct disk_pwr_work *dp_work =
			container_of(work, struct disk_pwr_work, work);

	SYNO_CTRL_HDD_POWERON(dp_work->slot, dp_work->power_on ? 1 : 0);
	free_disk_pwr_work(dp_work);
	return;
}

#ifdef MY_DEF_HERE
static void eunit_usb_power_handler(struct work_struct *work)
{
	struct disk_pwr_work *dp_work =
		container_of(work, struct disk_pwr_work, work);

	if (dp_work->argc != 1 || dp_work->ctl_args[0] == NULL) {
		printk(ERR_LOG_FMT "invalid argument for eunit usb power control\n");
		goto END;
	}

	if (0 != syno_usb_eunit_single_hdd_ctrl_by_uuid(
		dp_work->ctl_args[0], dp_work->slot, dp_work->power_on ? 1 : 0)) {
		printk(ERR_LOG_FMT "failed to control usb eunit\n");
		goto END;
	}
END:
	free_disk_pwr_work(dp_work);
	return;
}
#endif /* MY_DEF_HERE */

#ifdef DPM_DEBUG
static void power_debug_handler(struct work_struct *work)
{
	struct disk_pwr_work *dp_work =
			container_of(work, struct disk_pwr_work, work);
	printk(INFO_LOG_FMT "debug_handler -> trying to %s slot %d of %s\n",
		dp_work->power_on ? "power on" : "power off", dp_work->slot, dp_work->uuid);
	free_disk_pwr_work(dp_work);
}
#endif

static inline int strdup_control_args(
	const struct machine_pm_info *target_mpi, struct disk_pwr_work *dp_work)
{
	int i = 0;

	dp_work->argc = target_mpi->pm_ctr.argc;
	for (i = 0; i < dp_work->argc; i++) {
		dp_work->ctl_args[i] = kstrdup(target_mpi->pm_ctr.argv[i], GFP_NOWAIT);
		if (!dp_work->ctl_args[i]) {
			printk(ERR_LOG_FMT "failed to alloc ctl_args[0]\n");
			return -1;
		}
	}
	return 0;
}

static int dpm_disk_power_schedule(const struct machine_pm_info *target_mpi,
	unsigned int slot, bool power_on)
{
	int ret = -1;
	struct disk_pwr_work *dp_work = NULL;

	if (!target_mpi) {
		printk(ERR_LOG_FMT "invalid target machine pm info\n");
		goto ERR;
	}
	if (slot == 0 || target_mpi->pm_conf.slot_size < slot) {
		printk(ERR_LOG_FMT "invalid slot[%d] for target machine pm info\n", slot);
		goto ERR;
	}

	dp_work = kmalloc(sizeof(struct machine_pm_info), GFP_NOWAIT);
	if (!dp_work) {
		printk(ERR_LOG_FMT "failed to alloc dp_work\n");
		goto ERR;
	}
	memset(dp_work, 0, sizeof(struct disk_pwr_work));

	switch (target_mpi->pm_ctr.ctl_method) {
		case SYNO_DPM_CTL_METHOD_HOST:
			INIT_WORK(&dp_work->work, host_power_handler);
			break;
		case SYNO_DPM_CTL_METHOD_EUNIT_USB:
#ifdef MY_DEF_HERE
			if (0 != strdup_control_args(target_mpi, dp_work)) {
				printk(ERR_LOG_FMT "failed to alloc ctl_args[0]\n");
				goto ERR;
			}
			INIT_WORK(&dp_work->work, eunit_usb_power_handler);
			break;
#else
			printk(ERR_LOG_FMT "This platform doesn't support usb eunit\n");
			goto ERR;
#endif
#ifdef DPM_DEBUG
		case SYNO_DPM_CTL_METHOD_DEBUG:
			INIT_WORK(&dp_work->work, power_debug_handler);
			break;
#endif
		case SYNO_DPM_CTL_METHOD_HOST_USERSPACE_HELPER:
		case SYNO_DPM_CTL_METHOD_EUNIT_USERSPACE_HELPER:
		default:
			printk(ERR_LOG_FMT "unknown power control method : %d",
					target_mpi->pm_ctr.ctl_method);
			goto ERR;
	}

	snprintf(dp_work->uuid, SYNO_DPM_UUID_LEN_MAX, "%s", target_mpi->uuid);
	dp_work->slot = slot;
	dp_work->power_on = power_on;
	queue_work(target_mpi->power_ctrl_wq, &dp_work->work);
	dp_work = NULL;
	ret = 0;
ERR:
	free_disk_pwr_work(dp_work);
	return ret;
}

int dpm_disk_power_enable_schedule(const struct machine_pm_info *target_mpi,
	unsigned int slot)
{
	return dpm_disk_power_schedule(target_mpi, slot, true);
}
int dpm_disk_power_disable_schedule(const struct machine_pm_info *target_mpi,
	unsigned int slot)
{
	return dpm_disk_power_schedule(target_mpi, slot, false);
}

static int host_power_present_check(int slot)
{
	// Reference from syno_hddmon.c
#ifdef MY_ABC_HERE
	return SYNO_CHECK_HDD_DETECT(slot);
#else
	return SYNO_CHECK_HDD_PRESENT(slot);
#endif /* MY_ABC_HERE */
}

#ifdef MY_DEF_HERE
static int eunit_usb_power_present_check(const struct pm_ctrl_method *pm_ctr, int slot)
{
	if (pm_ctr->argc != 1 || pm_ctr->argv[0] == NULL) {
		printk(ERR_LOG_FMT "invalid argument for eunit usb power present check\n");
		return -1;
	}

	return syno_usb_eunit_disk_present_check_by_uuid(pm_ctr->argv[0], slot);
}
static int eunit_usb_power_enable_check(const struct pm_ctrl_method *pm_ctr, int slot)
{
	if (pm_ctr->argc != 1 || pm_ctr->argv[0] == NULL) {
		printk(ERR_LOG_FMT "invalid argument for eunit usb power present check\n");
		return -1;
	}

	return syno_usb_eunit_disk_enable_check_by_uuid(pm_ctr->argv[0], slot);
}
#endif /* MY_DEF_HERE */

int dpm_disk_power_present_check(const struct pm_ctrl_method *pm_ctr, int slot)
{
	if (!pm_ctr) {
		printk(ERR_LOG_FMT "invalid pm_ctrl_method\n");
		return -1;
	}

	switch (pm_ctr->ctl_method) {
		case SYNO_DPM_CTL_METHOD_HOST:
			return host_power_present_check(slot);
		case SYNO_DPM_CTL_METHOD_EUNIT_USB:
#ifdef MY_DEF_HERE
			return eunit_usb_power_present_check(pm_ctr, slot);
#else /* MY_DEF_HERE */
			printk(ERR_LOG_FMT "This platform dones't support usb eunit\n");
			return -1;
#endif /* MY_DEF_HERE */
		case SYNO_DPM_CTL_METHOD_HOST_USERSPACE_HELPER:
			printk(ERR_LOG_FMT "host userspace helper power present check not implement\n");
			return -1;
		case SYNO_DPM_CTL_METHOD_EUNIT_USERSPACE_HELPER:
			printk(ERR_LOG_FMT "eunit userspace helper power present check not implement\n");
			return -1;
		default:
			printk(ERR_LOG_FMT "unknown power control method : %d",
					pm_ctr->ctl_method);
			return -1;
	}
}

static int host_power_enable_check(int slot)
{
	return SYNO_CHECK_HDD_ENABLE(slot);
}


int dpm_disk_power_enable_check(const struct pm_ctrl_method *pm_ctr, int slot)
{
	if (!pm_ctr) {
		printk(ERR_LOG_FMT "invalid pm_ctrl_method\n");
		return -1;
	}

	switch (pm_ctr->ctl_method) {
		case SYNO_DPM_CTL_METHOD_HOST:
			return host_power_enable_check(slot);
		case SYNO_DPM_CTL_METHOD_EUNIT_USB:
#ifdef MY_DEF_HERE
			return eunit_usb_power_enable_check(pm_ctr, slot);
#else /* MY_DEF_HERE */
			printk(ERR_LOG_FMT "This platform dones't support usb eunit\n");
			return -1;
#endif /* MY_DEF_HERE */
		case SYNO_DPM_CTL_METHOD_HOST_USERSPACE_HELPER:
			printk(ERR_LOG_FMT "host userspace helper power present check not implement\n");
			return -1;
		case SYNO_DPM_CTL_METHOD_EUNIT_USERSPACE_HELPER:
			printk(ERR_LOG_FMT "eunit userspace helper power present check not implement\n");
			return -1;
		default:
			printk(ERR_LOG_FMT "unknown power control method : %d",
					pm_ctr->ctl_method);
			return -1;
	}
}
