// Copyright (c) 2000-2024 Synology Inc. All rights reserved.
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include "dpm_int.h"

#define QUOTA_EXPIRE_DEF_TIME 60 // 60s
#define REQ_AGAIN_EXPIRE_DEF_TIME 20 // 20s

struct string_buffer {
	char *buf;
	size_t buf_size;
	size_t buf_offset;
};

static LIST_HEAD(g_machine_pm_list);
static DEFINE_SPINLOCK(g_machine_pm_list_lock);
static bool g_hotplug_monitor_activated = false;
static struct work_struct interrupt_rescan_work;
static struct workqueue_struct *dpm_quota_wq = NULL;
static struct workqueue_struct *dpm_interrupt_rescan_wq = NULL;
static struct task_struct *monitor_thread = NULL;

extern bool g_support_syno_dpm;
extern int g_syno_dpm_debug_level;

enum HOTPLUG_PLAN_FITER {
	HOTPLUG_PLAN_INTR = (1 << 0),
	HOTPLUG_PLAN_POLL = (1 << 1),
};

struct hotplug_check_plan {
	char *uuid;
	int slot_size;
	struct slot_info *slot_info_list;
	int *slot_enable_seq;
	struct pm_ctrl_config pm_conf;
	struct pm_ctrl_method pm_ctr;
	bool all_slot_handled;
	struct list_head list;
};

static struct hotplug_check_plan *gen_hotplug_check_plan(const struct machine_pm_info *mpi);
static inline void free_hotplug_check_plan(struct hotplug_check_plan *plan);

void topology_config_init(struct topology_config *topo_cfg)
{
	int i = 0;
	for (i = 0; i < MAX_CTR_ARGS; i++) {
		topo_cfg->ctl_args[i] = NULL;
	}
	memset(topo_cfg, 0, sizeof(struct topology_config));
	topo_cfg->ctl_method = SYNO_DPM_CTL_METHOD_UNKNOWN;
	topo_cfg->mon_mode = SYNO_DPM_MONITOR_MODE_UNKNOWN;
}

static bool topology_config_valid_check(const struct topology_config *topo_cfg)
{
	int i = 0;
	bool arg_seq_break = false;
	int uninit_en_seq_count = 0;

	if (!topo_cfg) {
		return false;
	}
	if (strlen(topo_cfg->uuid) == 0) {
		printk(ERR_LOG_FMT "UUID should be set\n");
		return false;
	}
	if (topo_cfg->slot_size <= 0 ||
		topo_cfg->quota_size <= 0 ) {
		printk(ERR_LOG_FMT "Slot size and quota size should be set\n");
		return false;
	}
	if (topo_cfg->ctl_method == SYNO_DPM_CTL_METHOD_UNKNOWN ||
		topo_cfg->mon_mode == SYNO_DPM_MONITOR_MODE_UNKNOWN) {
		printk(ERR_LOG_FMT "Control Method and Monitor Mode should be set\n");
		return false;
	}

	switch (topo_cfg->mon_mode) {
		case SYNO_DPM_MONITOR_MODE_POLLING:
			if (topo_cfg->polling_interval <= 0) {
				printk(ERR_LOG_FMT "Polling interval should be set on polling mode\n");
				return false;
			}
			break;
		case SYNO_DPM_MONITOR_MODE_INTERRUPT:
			break;
		case SYNO_DPM_MONITOR_MODE_UNKNOWN:
		default:
			return false;
	}


	for (i = 0; i < topo_cfg->slot_size; i++) {
		if (topo_cfg->slot_enable_seq[i] == 0) {
			uninit_en_seq_count++;
		}
	}
	if (uninit_en_seq_count != 0 &&
		uninit_en_seq_count != topo_cfg->slot_size) {
		printk(ERR_LOG_FMT "invalid slot_enable_seq\n");
		return false;
	}

	for (i = 0; i < MAX_CTR_ARGS; i++) {
		if (topo_cfg->ctl_args[i] == NULL) {
			arg_seq_break = true;
		} else {
			if (arg_seq_break) {
				printk(ERR_LOG_FMT "Control Method Args should be in sequence\n");
				return false;
			}
		}
	}

	return true;
}

static void machine_pm_info_free(struct machine_pm_info *mpi)
{
	int i = 0;
	if (!mpi) {
		return;
	}

	for (i = 0; i < MAX_CTR_ARGS; i++) {
		kfree(mpi->pm_ctr.argv[i]);
	}
	if (mpi->power_ctrl_wq) {
		destroy_workqueue(mpi->power_ctrl_wq);
	}
	list_del(&mpi->info_list);
	kfree(mpi->slot_enable_seq);
	kfree(mpi->slot_info_list);
	kfree(mpi->quota_list);
	kfree(mpi);
}

static void machine_pm_info_slot_status_init(struct machine_pm_info* mpi)
{
	int i = 0;
	int enable_result = -1;

	if (!mpi || !mpi->slot_info_list) {
		return;
	}

	for (i = 0; i < mpi->pm_conf.slot_size; i++) {
		enable_result = dpm_disk_power_enable_check(&mpi->pm_ctr, i + 1);

		if (enable_result == -1) {
			mpi->slot_info_list[i].status = SYNO_DPM_SLOT_STATUS_UNKNOWN;
		} else if (enable_result == 1) {
			mpi->slot_info_list[i].status = SYNO_DPM_SLOT_STATUS_DETECTED;
		} else {
			mpi->slot_info_list[i].status = SYNO_DPM_SLOT_STATUS_UNDETECTED;
		}
	}
	return;
}

static struct machine_pm_info*
machine_pm_info_from_config_build(const struct topology_config *topo_cfg)
{
	int i = 0;
	bool failed = true;
	char str_wq_name[SYNO_DPM_UUID_LEN_MAX + 7] = {0};
	struct machine_pm_info* mpi = NULL;

	if (!topo_cfg) {
		printk(ERR_LOG_FMT "invalid arguement\n");
		goto ERR;
	}

	if (!topology_config_valid_check(topo_cfg)) {
		printk(ERR_LOG_FMT "invalid topology config\n");
		goto ERR;
	}

	mpi = kzalloc(sizeof(struct machine_pm_info), GFP_KERNEL);
	if (!mpi) {
		printk(ERR_LOG_FMT "kzalloc fail\n");
		goto ERR;
	}

	strncpy(mpi->uuid, topo_cfg->uuid, SYNO_DPM_UUID_LEN_MAX);
	mpi->pm_conf.slot_size = topo_cfg->slot_size;
	mpi->pm_conf.quota_count = topo_cfg->quota_size;
	mpi->pm_conf.additional_delay = topo_cfg->additional_tune_delay;
	mpi->pm_conf.deepsleep_fixed_delay = topo_cfg->deepsleep_fixed_delay;
	mpi->pm_ctr.ctl_method = topo_cfg->ctl_method;
	for (i = 0; i < MAX_CTR_ARGS; i++) {
		if (topo_cfg->ctl_args[i]) {
			mpi->pm_ctr.argv[i] = kstrdup(topo_cfg->ctl_args[i], GFP_KERNEL);
			if (!mpi->pm_ctr.argv[i]) {
				printk(ERR_LOG_FMT "kstrdup fail\n");
				goto ERR;
			}
			mpi->pm_ctr.argc++;
		}
	}
	mpi->mon_cfg.mon_mode = topo_cfg->mon_mode;
	if (mpi->mon_cfg.mon_mode == SYNO_DPM_MONITOR_MODE_INTERRUPT) {
		mpi->trigger_rescan_by_inter = true;
	}

	mpi->mon_cfg.polling_interval = topo_cfg->polling_interval;
	mpi->slot_info_list = kzalloc(
		sizeof(struct slot_info) * mpi->pm_conf.slot_size, GFP_KERNEL);
	if (!mpi->slot_info_list) {
		printk(ERR_LOG_FMT "kzalloc fail\n");
		goto ERR;
	}

	mpi->slot_enable_seq = kzalloc(
		sizeof(enum SYNO_DPM_SLOT_STATUS) * mpi->pm_conf.slot_size, GFP_KERNEL);
	if (!mpi->slot_enable_seq) {
		printk(ERR_LOG_FMT "kzalloc fail\n");
		goto ERR;
	}

	for (i = 0; i < mpi->pm_conf.slot_size; i++) {
		mpi->slot_info_list[i].status = SYNO_DPM_SLOT_STATUS_UNKNOWN;
		mpi->slot_info_list[i].changing_status = SYNO_DPM_SLOT_CHANGING_STATUS_NONE;
		mpi->slot_info_list[i].changing_threshold = 0;
		if (topo_cfg->slot_enable_seq[i] != 0) {
			mpi->slot_enable_seq[i] = topo_cfg->slot_enable_seq[i];
		} else {
			mpi->slot_enable_seq[i] = i + 1;
		}
	}

	mpi->quota_list = kzalloc(
		sizeof(struct quota_status) * mpi->pm_conf.quota_count, GFP_KERNEL);
	if (!mpi->quota_list) {
		printk(ERR_LOG_FMT "kzalloc fail\n");
		goto ERR;
	}

	snprintf(str_wq_name, sizeof(str_wq_name), "dpm_wq_%s", mpi->uuid);
	mpi->power_ctrl_wq = alloc_workqueue(str_wq_name, 0, 1);
	if (!mpi->power_ctrl_wq) {
		printk(ERR_LOG_FMT "unable to alloc power_ctrl_wq for [%s]\n",
				mpi->uuid);
		goto ERR;
	}

	INIT_LIST_HEAD(&mpi->info_list);
	failed = false;
ERR:
	if (failed) {
		machine_pm_info_free(mpi);
		mpi = NULL;
	}
	return mpi;
}

static void machine_pm_list_free(struct list_head *mpi_list)
{
	struct machine_pm_info *mpi = NULL;
	struct machine_pm_info *mpi_tmp = NULL;

	list_for_each_entry_safe(mpi, mpi_tmp, mpi_list, info_list) {
		machine_pm_info_free(mpi);
	}
	return;
}

int machine_pm_list_update(const struct list_head *new_config_list)
{
	int ret = -1;
	int idx = 0;
	bool locked = false;
	unsigned long flags;
	struct topology_config *new_conf = NULL;
	struct machine_pm_info *curr_mpi = NULL;
	struct machine_pm_info *temp_mpi = NULL;
	struct machine_pm_info *new_mpi = NULL;
	bool potential_new[60] = {true};
	LIST_HEAD(new_machine_pm_list);
	LIST_HEAD(removed_machine_pm_list);

	if (!new_config_list) {
		return ret;
	}

	list_for_each_entry(new_conf, new_config_list, conf_list) {
		new_mpi = machine_pm_info_from_config_build(new_conf);
		if (!new_mpi) {
			printk(ERR_LOG_FMT "failed to build machine pm info\n");
			goto END;
		}
		list_add_tail(&new_mpi->info_list, &new_machine_pm_list);
	}

	idx = 0;
	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	list_for_each_entry(new_mpi, &new_machine_pm_list, info_list) {
		potential_new[idx] = true;
		list_for_each_entry(curr_mpi, &g_machine_pm_list, info_list) {
			if (0 == strcmp(curr_mpi->uuid, new_mpi->uuid)) {
				potential_new[idx] = false;
				break;
			}
		}
		idx++;
	}
	spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);

	idx = 0;
	list_for_each_entry(new_mpi, &new_machine_pm_list, info_list) {
		if (potential_new[idx]) {
			machine_pm_info_slot_status_init(new_mpi);
		}
		idx++;
	}

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	locked = true;
	// Remove disappeared machine first
	list_for_each_entry_safe(curr_mpi, temp_mpi, &g_machine_pm_list, info_list) {
		bool found = false;
		list_for_each_entry(new_mpi, &new_machine_pm_list, info_list) {
			if (0 == strcmp(curr_mpi->uuid, new_mpi->uuid)) {
				found = true;
				break;
			}
		}
		if (!found) {
			list_del(&curr_mpi->info_list);
			list_add_tail(&curr_mpi->info_list, &removed_machine_pm_list);
		}
	}

	// Add new machine
	ret = 0;
	list_for_each_entry_safe(new_mpi, temp_mpi, &new_machine_pm_list, info_list) {
		bool found = false;
		list_for_each_entry(curr_mpi, &g_machine_pm_list, info_list) {
			if (0 == strcmp(curr_mpi->uuid, new_mpi->uuid)) {
				found = true;
				break;
			}
		}
		if (!found) {
			list_del(&new_mpi->info_list);
			list_add_tail(&new_mpi->info_list, &g_machine_pm_list);
		}
	}
END:
	if (locked) {
		spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
	}
	machine_pm_list_free(&new_machine_pm_list);
	machine_pm_list_free(&removed_machine_pm_list);
	return ret;
}

static int string_buffer_append(struct string_buffer* sb, char *str, size_t str_len)
{
	if (!sb->buf || sb->buf_offset + str_len > sb->buf_size) {
		printk(ERR_LOG_FMT "buf size is not enough\n");
		return -ENOMEM;
	}

	memcpy(sb->buf + sb->buf_offset, str, str_len);
	sb->buf_offset += str_len;
	return 0;
}

int machine_pm_list_dump(char *str_result, size_t buf_size)
{
#define STR_APPEND(fmt, ...) \
do { \
	snprintf(str_line, sizeof(str_line), fmt, ##__VA_ARGS__); \
	if (0 != string_buffer_append(&str_buf, str_line, strlen(str_line))) { \
		ret = -ENOMEM; \
		goto ERR; \
	} \
} while (0) \

	int i = 0;
	int ret = -1;
	bool locked = false;
	unsigned long flags;
	struct machine_pm_info *curr_mpi = NULL;
	struct string_buffer str_buf;
	char str_line[1024];

	if (!str_result) {
		goto ERR;
	}

	str_buf.buf = str_result;
	str_buf.buf_size = buf_size;
	str_buf.buf_offset = 0;
	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	locked = true;
	list_for_each_entry(curr_mpi, &g_machine_pm_list, info_list) {
		STR_APPEND("[%s]\n", curr_mpi->uuid);
		STR_APPEND("slot_size=\"%d\"\n", curr_mpi->pm_conf.slot_size);
		STR_APPEND("quota_count=\"%d\"\n", curr_mpi->pm_conf.quota_count);
		STR_APPEND("additional_delay=\"%d\"\n", curr_mpi->pm_conf.additional_delay);
		STR_APPEND("deepsleep_fixed_delay=\"%d\"\n", curr_mpi->pm_conf.deepsleep_fixed_delay);
		STR_APPEND("ctl_method=\"%d\"\n", curr_mpi->pm_ctr.ctl_method);
		STR_APPEND("ctl_method_argc=\"%d\"\n", curr_mpi->pm_ctr.argc);
		for (i = 0; i < curr_mpi->pm_ctr.argc; i++) {
			STR_APPEND("ctl_method_argv_%d=\"%s\"\n",
				i + 1, curr_mpi->pm_ctr.argv[i]);
		}
		STR_APPEND("mon_mode=\"%d\"\n", curr_mpi->mon_cfg.mon_mode);
		STR_APPEND("polling_interval=\"%d\"\n",
				curr_mpi->mon_cfg.polling_interval);
		STR_APPEND("slot_status=\"");
		for (i = 0; i < curr_mpi->pm_conf.slot_size; i++) {
			if (i == 0) {
				STR_APPEND("%d", curr_mpi->slot_info_list[i].status);
			} else {
				STR_APPEND(",%d", curr_mpi->slot_info_list[i].status);
			}
		}
		STR_APPEND("\"\n");

		STR_APPEND("slot_enable_seq=\"");
		for (i = 0; i < curr_mpi->pm_conf.slot_size; i++) {
			if (i == 0) {
				STR_APPEND("%d", curr_mpi->slot_enable_seq[i]);
			} else {
				STR_APPEND(",%d", curr_mpi->slot_enable_seq[i]);
			}
		}
		STR_APPEND("\"\n");

		STR_APPEND("quota_ower_slot=\"");
		for (i = 0; i < curr_mpi->pm_conf.quota_count; i++) {
			if (i == 0) {
				STR_APPEND("%d", curr_mpi->quota_list[i].ower_slot);
			} else {
				STR_APPEND(",%d", curr_mpi->quota_list[i].ower_slot);
			}
		}
		STR_APPEND("\"\n");
		STR_APPEND("quota_start_time_jiffy=\"");
		for (i = 0; i < curr_mpi->pm_conf.quota_count; i++) {
			if (i == 0) {
				STR_APPEND("%lu", curr_mpi->quota_list[i].start_time_jiffy);
			} else {
				STR_APPEND(",%lu", curr_mpi->quota_list[i].start_time_jiffy);
			}
		}
		STR_APPEND("\"\n");
		STR_APPEND("quota_expire_time_jiffy=\"");
		for (i = 0; i < curr_mpi->pm_conf.quota_count; i++) {
			if (i == 0) {
				STR_APPEND("%lu", curr_mpi->quota_list[i].expire_time_jiffy);
			} else {
				STR_APPEND(",%lu", curr_mpi->quota_list[i].expire_time_jiffy);
			}
		}
		STR_APPEND("\"\n");
		STR_APPEND("quota_rel_method=\"");
		for (i = 0; i < curr_mpi->pm_conf.quota_count; i++) {
			if (i == 0) {
				STR_APPEND("%d", curr_mpi->quota_list[i].rel_method);
			} else {
				STR_APPEND(",%d", curr_mpi->quota_list[i].rel_method);
			}
		}
		STR_APPEND("\"\n");
	}

	ret = 0;
ERR:
	if (locked) {
		spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
	}
	return ret;
}

static inline struct machine_pm_info *get_mpi_by_uuid(const char *uuid)
{
	struct machine_pm_info *iter_mpi = NULL;

	list_for_each_entry(iter_mpi, &g_machine_pm_list, info_list) {
		if (0 == strncmp(iter_mpi->uuid, uuid, SYNO_DPM_UUID_LEN_MAX)) {
			return iter_mpi;
		}
	}
	return NULL;
}

static void release_quota(struct machine_pm_info *target_mpi, unsigned int slot)
{
	int i = 0;
	struct quota_status *quota = NULL;

	for (i = 0; i < target_mpi->pm_conf.quota_count; i++) {
		quota = &target_mpi->quota_list[i];
		if (quota->ower_slot == slot) {
			memset(&(target_mpi->quota_list[i]), 0, sizeof(struct quota_status));
			break;
		}
	}
}

static void quota_timeout_checker(struct work_struct *work)
{
	int i = 0;
	unsigned long flags;
	struct delayed_work *dwork = to_delayed_work(work);
	struct machine_pm_info *curr_mpi = NULL;

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	list_for_each_entry(curr_mpi, &g_machine_pm_list, info_list) {
		for (i = 0; i < curr_mpi->pm_conf.quota_count; i++) {
			if (curr_mpi->quota_list[i].ower_slot != 0 &&
				time_after(jiffies, (curr_mpi->quota_list[i].expire_time_jiffy))) {
				if (curr_mpi->quota_list[i].rel_method ==
						SYNO_DPM_RELEASE_METHOD_DEVICE_DRIVER) {
					printk(ERR_LOG_FMT "quota of slot %d on [%s] expired\n",
						curr_mpi->quota_list[i].ower_slot, curr_mpi->uuid);
				}
				release_quota(curr_mpi, curr_mpi->quota_list[i].ower_slot);
			}
		}
	}
	spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
	kfree(dwork);
}

static inline int schedule_quota_timeout_checker(unsigned long delay)
{
	struct delayed_work *dwork = kmalloc(sizeof(struct delayed_work), GFP_NOWAIT);
	if (!dwork) {
		printk(ERR_LOG_FMT "failed to alloc dwork\n");
		return -1;
	}

	INIT_DELAYED_WORK(dwork, quota_timeout_checker);
	queue_delayed_work(dpm_quota_wq, dwork, delay);
	return 0;
}

static bool register_quota(struct machine_pm_info *target_mpi, unsigned int slot,
		enum SYNO_DPM_RELEASE_METHOD rel_method, unsigned int rel_time)
{
	int i = 0;
	int empty_quota_idx = -1;
	int free_quota_count = 0;
	int waking_slot_count = 0;

	for (i = 0; i < target_mpi->pm_conf.quota_count; i++) {
		struct quota_status *quota = &target_mpi->quota_list[i];
		if (quota->ower_slot == 0) {
			empty_quota_idx = i;
			free_quota_count++;
		} else if (quota->ower_slot == slot) {
			return true;
		}
	}

	if (target_mpi->slot_info_list[slot - 1].status != SYNO_DPM_SLOT_STATUS_WAIT_FOR_WAKE) {
		for (i = 0; i < target_mpi->pm_conf.slot_size; i++) {
			if (target_mpi->slot_info_list[i].status == SYNO_DPM_SLOT_STATUS_WAIT_FOR_WAKE) {
				waking_slot_count++;
			}
		}

		/*
		 * Reserve the quota for slot waiting for waiting from deep sleep to
		 * to prevent protocol retry timeout leads to disk drop.
		 */
		if (free_quota_count <= waking_slot_count) {
			return false;
		}
	}

	if (empty_quota_idx == -1) {
		return false;
	}

	target_mpi->quota_list[empty_quota_idx].ower_slot = slot;
	target_mpi->quota_list[empty_quota_idx].start_time_jiffy = jiffies;
	target_mpi->quota_list[empty_quota_idx].expire_time_jiffy =
		jiffies + rel_time * HZ;
	target_mpi->quota_list[empty_quota_idx].rel_method = rel_method;
	if (0 != schedule_quota_timeout_checker(rel_time * HZ)) {
		release_quota(target_mpi, slot);
		printk(ERR_LOG_FMT "failed to schedule quota checker\n");
		return false;
	}
	return true;
}

static int dpm_req_pwr_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name, bool no_power_ctl, bool rel_by_fixed_delay,
		 unsigned long rel_time)
{
	int ret = -1;
	bool locked = false;
	unsigned long flags;
	struct machine_pm_info *target_mpi = NULL;

	if (!uuid || !caller_name) {
		printk(ERR_LOG_FMT "invalid paramter\n");
		goto OUT;
	}

	if (g_syno_dpm_debug_level > 1) {
		if (printk_ratelimit()) {
			if (no_power_ctl) {
				printk(INFO_LOG_FMT "%s requests to get quota of slot %d on [%s]\n",
					caller_name, slot, uuid);
			} else {
				printk(INFO_LOG_FMT "%s requests to enable power of slot %d on [%s]\n",
					caller_name, slot, uuid);
			}
		}
	}

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	locked = true;
	target_mpi = get_mpi_by_uuid(uuid);
	if (!target_mpi) {
		printk(ERR_LOG_FMT "uuid[%s] isn't in machine list\n", uuid);
		goto OUT;
	}
	if (slot == 0 || slot > target_mpi->pm_conf.slot_size) {
		printk(ERR_LOG_FMT "slot[%d] isn't valid for [%s]\n", slot, uuid);
		goto OUT;
	}

	if (!register_quota(target_mpi, slot,
			rel_by_fixed_delay ?
				SYNO_DPM_RELEASE_METHOD_DEEPSLEEP_FIXED_DELAY :
				SYNO_DPM_RELEASE_METHOD_DEVICE_DRIVER,
			rel_time)) {
		if (g_syno_dpm_debug_level > 3) {
			if (printk_ratelimit()) {
				printk(DEBUG_LOG_FMT "no quota left for %s\n", uuid);
			}
		}
		ret = 1;
		goto OUT;
	}

	if (!no_power_ctl) {
		if (0 != dpm_disk_power_enable_schedule(target_mpi, slot)) {
			printk(ERR_LOG_FMT "unable to schedule power enable on slot %d of [%s]\n",
				slot, uuid);
			release_quota(target_mpi, slot);
			goto OUT;
		}
	}
	target_mpi->slot_info_list[slot - 1].status = SYNO_DPM_SLOT_STATUS_DETECTED;
	ret = 0;
	if (no_power_ctl) {
		printk(INFO_LOG_FMT "%s requests to get quota of slot %d on [%s] success\n",
			caller_name, slot, uuid);
	} else {
		printk(INFO_LOG_FMT "%s requests to enable power of slot %d on [%s] success\n",
			caller_name, slot, uuid);
	}
OUT:
	if (locked) {
		spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
	}
	return ret;
}

int syno_dpm_req_pwr_by_slot_with_fixed_delay(const char *uuid, unsigned int slot,
		const char *caller_name, unsigned long fixed_delay)
{
	return dpm_req_pwr_by_slot(uuid, slot, caller_name, false, true,
		fixed_delay);
}

int syno_dpm_req_pwr_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name)
{
	return dpm_req_pwr_by_slot(uuid, slot, caller_name, false, false,
		QUOTA_EXPIRE_DEF_TIME);
}
EXPORT_SYMBOL(syno_dpm_req_pwr_by_slot);

int syno_dpm_req_quota_only_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name)
{
	return dpm_req_pwr_by_slot(uuid, slot, caller_name, true, false,
		QUOTA_EXPIRE_DEF_TIME);
}
EXPORT_SYMBOL(syno_dpm_req_quota_only_by_slot);

int syno_dpm_req_pwr_with_fixed_delayed_by_uuid(
	const char *uuid, const char *caller_name)
{
	int i = 0;
	bool found = false;
	unsigned long flags;
	struct machine_pm_info *target_mpi = NULL;

	if (uuid == NULL || caller_name == NULL) {
		printk(ERR_LOG_FMT "invalid parameter\n");
		return -1;
	}

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	target_mpi = get_mpi_by_uuid(uuid);
	if (target_mpi) {
		found = true;
		target_mpi->mon_expired_time_jiffy = jiffies;
		for (i = 0; i < target_mpi->pm_conf.slot_size; i++) {
			if (target_mpi->slot_info_list[i].status == SYNO_DPM_SLOT_STATUS_DEEP_SLEEP_RETRY) {
				target_mpi->slot_info_list[i].status = SYNO_DPM_SLOT_STATUS_WAIT_FOR_WAKE;
			}
		}
	}
	spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);

	if (!found) {
		printk(ERR_LOG_FMT "unable to found [%s] in machine list\n", uuid);
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(syno_dpm_req_pwr_with_fixed_delayed_by_uuid);

int syno_dpm_rel_pwr_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name)
{
	int ret = -1;
	unsigned long flags;
	bool locked = false;
	struct machine_pm_info *target_mpi = NULL;

	if (!uuid || !caller_name) {
		printk(ERR_LOG_FMT "invalid paramter\n");
		goto OUT;
	}

	if (g_syno_dpm_debug_level > 1) {
		printk(INFO_LOG_FMT "%s requests to release quota of slot %d on [%s]\n",
			caller_name, slot, uuid);
	}
	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	locked = true;

	target_mpi = get_mpi_by_uuid(uuid);
	if (!target_mpi) {
		printk(ERR_LOG_FMT "uuid[%s] isn't in machine list\n", uuid);
		goto OUT;
	}
	if (slot == 0 || slot > target_mpi->pm_conf.slot_size) {
		printk(ERR_LOG_FMT "slot[%d] isn't valid for [%s]\n", slot, uuid);
		goto OUT;
	}

	release_quota(target_mpi, slot);
	ret = 0;
	if (g_syno_dpm_debug_level > 0) {
		printk(INFO_LOG_FMT "%s requests to release quota of slot %d on [%s] success\n",
			caller_name, slot, uuid);
	}
OUT:
	if (locked) {
		spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
	}
	return ret;
}
EXPORT_SYMBOL(syno_dpm_rel_pwr_by_slot);

static int dpm_req_disble_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name, bool deep_retry_sleep)
{
	int ret = -1;
	unsigned long flags;
	bool locked = false;
	struct machine_pm_info *target_mpi = NULL;

	if (!uuid || !caller_name) {
		printk(ERR_LOG_FMT "invalid paramter\n");
		goto OUT;
	}

	if (g_syno_dpm_debug_level > 1) {
		printk(INFO_LOG_FMT "%s requests to disable power of slot %d on [%s]\n",
			caller_name, slot, uuid);
	}
	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	locked = true;

	target_mpi = get_mpi_by_uuid(uuid);
	if (!target_mpi) {
		printk(ERR_LOG_FMT "uuid[%s] isn't in machine list\n", uuid);
		goto OUT;
	}
	if (slot == 0 || slot > target_mpi->pm_conf.slot_size) {
		printk(ERR_LOG_FMT "slot[%d] isn't valid for [%s]\n", slot, uuid);
		goto OUT;
	}

	if (0 != dpm_disk_power_disable_schedule(target_mpi, slot)) {
		printk(ERR_LOG_FMT "unable to schedule power disable on slot %d of [%s]\n",
			slot, uuid);
		goto OUT;
	}

	target_mpi->slot_info_list[slot - 1].status = (deep_retry_sleep) ?
		SYNO_DPM_SLOT_STATUS_DEEP_SLEEP_RETRY : SYNO_DPM_SLOT_STATUS_UNDETECTED;
	/*
	 * Disk may been pluged-out when spinup. Once we decide to disable the power,
	 * release the quota immediately.
	 */
	release_quota(target_mpi, slot);
	ret = 0;

	if (deep_retry_sleep) {
		// avoid the log wake the disk up from deep sleep
		printk(INFO_LOG_FMT "%s requests to disable power of slot %d on [%s] success\n",
				caller_name, slot, uuid);
	} else {
		printk(NOTICE_LOG_FMT "%s requests to disable power of slot %d on [%s] success\n",
				caller_name, slot, uuid);
	}
OUT:
	if (locked) {
		spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
	}
	return ret;
}

int syno_dpm_req_disble_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name)
{
	return dpm_req_disble_by_slot(uuid, slot, caller_name, false);
}
EXPORT_SYMBOL(syno_dpm_req_disble_by_slot);

static int wait_for_power_disable(
	struct hotplug_check_plan *plan, int start_slot, int end_slot, int timeout)
{
	int i = 0;
	int disableCount = 0;
	int enable_result = -1;
	int slot_size = end_slot - start_slot + 1;
	unsigned long start_time = jiffies;

	// check for the slots to be powered off before timeout
	start_time = jiffies;
	while (1) {
		disableCount = 0;
		for (i = start_slot; i <= end_slot; i++) {
			enable_result = dpm_disk_power_enable_check(&plan->pm_ctr, i);
			if (enable_result == -1) {
				printk(INFO_LOG_FMT "failed to check slot %d power status on [%s]\n", i, plan->uuid);
				continue;
			} else if (enable_result == 0) {
				disableCount++;
			}
		}

		if (disableCount == slot_size) {
			printk(INFO_LOG_FMT "all slots are powered off on [%s]\n", plan->uuid);
			break;
		}

		if (time_after(jiffies, start_time + timeout * HZ)) {
			printk(NOTICE_LOG_FMT "timeout to wait for power off all slots on [%s]\n", plan->uuid);
			return 1;
		}
		msleep(1000);
	}
	return 0;
}

int syno_dpm_req_deep_sleep_by_slot(const char *uuid, unsigned int slot,
		bool blocking, const int timeout, const char *caller_name)
{
	int ret = -1;
	unsigned long flags;
	struct machine_pm_info *target_mpi = NULL;
	struct hotplug_check_plan *plan = NULL;

	ret = dpm_req_disble_by_slot(uuid, slot, caller_name, true);
	if (ret != 0) {
		goto END;
	}
	if (!blocking) {
		ret = 0;
		goto END;
	}

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	target_mpi = get_mpi_by_uuid(uuid);
	if (target_mpi) {
		plan = gen_hotplug_check_plan(target_mpi);
		if (!plan) {
			printk(ERR_LOG_FMT "unable to found [%s] in machine list\n", uuid);
			spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
			goto END;
		}
	}
	spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);

	ret = wait_for_power_disable(plan, slot, slot, timeout);
	if (ret != 0) {
		goto END;
	}

	ret = 0;
END:
	free_hotplug_check_plan(plan);
	return ret;
}
EXPORT_SYMBOL(syno_dpm_req_deep_sleep_by_slot);

int syno_dpm_req_deep_sleep_by_uuid(const char *uuid, const int timeout, const char *caller_name)
{
	int i = 0;
	int slot_size = 0;
	int ret = -1;
	unsigned long flags;
	struct machine_pm_info *target_mpi = NULL;
	struct hotplug_check_plan *plan = NULL;

	if (uuid == NULL || caller_name == NULL) {
		printk(ERR_LOG_FMT "invalid parameter\n");
		goto END;
	}

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	target_mpi = get_mpi_by_uuid(uuid);
	if (target_mpi) {
		plan = gen_hotplug_check_plan(target_mpi);
		if (!plan) {
			printk(ERR_LOG_FMT "unable to found [%s] in machine list\n", uuid);
			spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
			goto END;
		}

		target_mpi->mon_expired_time_jiffy = ULONG_MAX;
		slot_size = plan->slot_size;
	}
	spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);

	for (i = 1; i <= slot_size; i++) {
		if (plan->slot_info_list[i - 1].status == SYNO_DPM_SLOT_STATUS_DETECTED) {
			if (0 != syno_dpm_req_deep_sleep_by_slot(uuid, i, false, 0, __func__)) {
				printk(ERR_LOG_FMT "unable set deep sleep on slot %d on [%s]\n", i, uuid);
				goto END;
			}
		}
	}

	ret = wait_for_power_disable(plan, 1, slot_size, timeout);
	if (ret != 0) {
		goto END;
	}

	ret = 0;
END:
	if (ret < 0) {
		// rollback the slot status to power the disk on
		syno_dpm_req_pwr_with_fixed_delayed_by_uuid(uuid, __func__);
	}
	free_hotplug_check_plan(plan);
	return ret;
}
EXPORT_SYMBOL(syno_dpm_req_deep_sleep_by_uuid);

static inline void free_hotplug_check_plan(
			struct hotplug_check_plan *plan)
{
	int i = 0;
	if (plan) {
		for (i = 0; i < plan->pm_ctr.argc; i++) {
			kfree(plan->pm_ctr.argv[i]);
		}
		kfree(plan->slot_info_list);
		kfree(plan->slot_enable_seq);
		kfree(plan->uuid);
		kfree(plan);
	}
}

static inline void free_hotplug_check_plan_list(
		struct list_head *plan_list)
{
	struct hotplug_check_plan *plan = NULL;
	struct hotplug_check_plan *plan_tmp = NULL;
	list_for_each_entry_safe(plan, plan_tmp, plan_list, list) {
		list_del(&plan->list);
		free_hotplug_check_plan(plan);
	}
}

static int update_mpi_slot_changing_status(
	const char *uuid, int slot, enum SYNO_DPM_SLOT_CHANGING_STATUS status, int threshold)
{
	int ret = -1;
	bool locked = false;
	unsigned long flags;
	struct machine_pm_info *target_mpi = NULL;

	if (uuid == NULL) {
		printk(ERR_LOG_FMT "invalid parameter\n");
		goto END;
	}

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	locked = true;
	target_mpi = get_mpi_by_uuid(uuid);
	if (!target_mpi) {
		printk(ERR_LOG_FMT "unable to found [%s] in machine list\n", uuid);
		goto END;
	}

	if (slot == 0 || slot > target_mpi->pm_conf.slot_size) {
		printk(ERR_LOG_FMT "slot[%d] isn't valid for [%s]\n", slot, uuid);
		goto END;
	}

	target_mpi->slot_info_list[slot - 1].changing_status = status;
	target_mpi->slot_info_list[slot - 1].changing_threshold = threshold;
	ret = 0;
END:
	if (locked) {
		spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
	}
	return ret;
}

static int handle_slot_power_enable(
	const struct hotplug_check_plan *plan, int slot, const char *caller_name)
{
	int req_result = -1;
	int func_result = -1;

	if (plan->slot_info_list[slot - 1].changing_status == SYNO_DPM_SLOT_CHANGING_STATUS_ENABLE &&
		plan->slot_info_list[slot - 1].changing_threshold != SYNO_DPM_CHANGING_ENABLE_THRESHOLD) {
		if (update_mpi_slot_changing_status(plan->uuid, slot,
				SYNO_DPM_SLOT_CHANGING_STATUS_ENABLE,
				plan->slot_info_list[slot - 1].changing_threshold + 1) != 0) {
				printk(ERR_LOG_FMT "failed to update slot %d changing status on [%s]\n",
					slot, plan->uuid);
		}
		// Treat as isn't handled.
		if (g_syno_dpm_debug_level > 0) {
			printk(NOTICE_LOG_FMT "delay power re-enable of %d on [%s]\n", slot, plan->uuid);
		}
		req_result = 1;
	} else {
		if (plan->slot_info_list[slot - 1].status == SYNO_DPM_SLOT_STATUS_WAIT_FOR_WAKE &&
			plan->pm_conf.deepsleep_fixed_delay != 0) {
			func_result = syno_dpm_req_pwr_by_slot_with_fixed_delay(
					plan->uuid, slot, caller_name,
					plan->pm_conf.deepsleep_fixed_delay);
			if (func_result == -1) {
				printk(ERR_LOG_FMT "failed to enable slot %d on [%s] by fixed delay\n",
					slot, plan->uuid);
			}
		} else {
			func_result = syno_dpm_req_pwr_by_slot(
				plan->uuid, slot, caller_name);
			if (func_result == -1) {
				printk(ERR_LOG_FMT "failed to enable slot %d on [%s]\n",
					slot, plan->uuid);
			}
		}

		if (func_result == -1) {
			req_result = 1;
		} else {
			req_result = func_result;
		}

		if (req_result == 0) {
			if (update_mpi_slot_changing_status(plan->uuid, slot,
					SYNO_DPM_SLOT_CHANGING_STATUS_ENABLE, 1) != 0) {
					printk(ERR_LOG_FMT "failed to update slot %d changing status on [%s]\n",
						slot, plan->uuid);
			}
		}
	}
	return req_result;
}

static int handle_slot_power_disable(
	const struct hotplug_check_plan *plan, int slot, const char *caller_name)
{
	int req_result = 1;

	if (plan->slot_info_list[slot - 1].changing_status == SYNO_DPM_SLOT_CHANGING_STATUS_DISABLE) {
		if (plan->slot_info_list[slot - 1].changing_threshold == SYNO_DPM_CHANGING_DISABLE_THRESHOLD) {
			if (0 != syno_dpm_req_disble_by_slot(
					plan->uuid, slot, caller_name)) {
				printk(ERR_LOG_FMT "failed to disable slot %d on [%s]\n",
						slot, plan->uuid);
				req_result = 1;
				return req_result;
			} else {
				req_result = 0;
				plan->slot_info_list[slot - 1].changing_threshold = 0;
			}
		} else {
			printk(NOTICE_LOG_FMT "[WARNING] delay power disable of %d on [%s]\n", slot, plan->uuid);
		}
	} else {
		printk(NOTICE_LOG_FMT "[WARNING] delay power disable of %d on [%s]\n", slot, plan->uuid);
		plan->slot_info_list[slot - 1].changing_threshold = 0;
	}

	if (update_mpi_slot_changing_status(plan->uuid, slot,
			SYNO_DPM_SLOT_CHANGING_STATUS_DISABLE,
			plan->slot_info_list[slot - 1].changing_threshold + 1) != 0) {
			printk(ERR_LOG_FMT "failed to update slot %d changing status on [%s]\n",
				slot, plan->uuid);
	}

	return req_result;
}

static bool handle_hotplug_check_plan(
	const struct list_head *plan_list, const char *caller_name)
{
	int i = 0;
	int req_result = -1;
	int present_result = -1;
	int enable_result = -1;
	bool all_done = true;
	struct hotplug_check_plan *plan = NULL;

	list_for_each_entry(plan, plan_list, list) {
		plan->all_slot_handled = true;
		for (i = 1; i <= plan->slot_size; i++) {
			int slot = plan->slot_enable_seq[i - 1];
			if (plan->slot_info_list[slot - 1].status ==
					SYNO_DPM_SLOT_STATUS_DEEP_SLEEP_RETRY) {
				/*
				 * skip the slot which is in deep sleep retry, this slot
				 * will be handled by its function.
				 */
				continue;
			}

			enable_result = dpm_disk_power_enable_check(&plan->pm_ctr, slot);
			present_result = dpm_disk_power_present_check(&plan->pm_ctr, slot);
			if (present_result == -1 || enable_result == -1) {
				if (g_syno_dpm_debug_level > 3) {
					/* This log may flood when eunit is plugin out */
					printk(ERR_LOG_FMT "failed to check slot %d on [%s]\n", slot, plan->uuid);
				}
				continue;
			}

			if (enable_result == present_result) {
				if (update_mpi_slot_changing_status(plan->uuid, slot,
					SYNO_DPM_SLOT_CHANGING_STATUS_NONE, 0) != 0) {
					printk(ERR_LOG_FMT "failed to update slot %d changing status on [%s]\n",
						slot, plan->uuid);
				}
				continue;
			}

			req_result = 0;
			if (present_result == 1 && enable_result == 0) {
				req_result = handle_slot_power_enable(plan, slot, caller_name);
			} else if (present_result == 0 && enable_result == 1) {
				req_result = handle_slot_power_disable(plan, slot, caller_name);
			}
			if (1 == req_result) {
				plan->all_slot_handled = false;
				all_done = false;
			}
		}
	}
	return all_done;
}

static struct hotplug_check_plan *gen_hotplug_check_plan(
		const struct machine_pm_info *mpi)
{
	int i = 0;
	bool failed = false;
	struct hotplug_check_plan *plan = NULL;

	plan = kzalloc(sizeof(struct hotplug_check_plan), GFP_NOWAIT);
	if (!plan) {
		printk(ERR_LOG_FMT "failed to alloc hotplug_check_plan\n");
		failed = true;
		goto END;
	}

	plan->uuid = kstrdup(mpi->uuid, GFP_NOWAIT);
	if (!plan->uuid) {
		printk(ERR_LOG_FMT "failed to alloc uuid\n");
		failed = true;
		goto END;
	}
	plan->all_slot_handled = false;
	plan->slot_size = mpi->pm_conf.slot_size;
	plan->pm_conf = mpi->pm_conf;
	plan->pm_ctr.ctl_method = mpi->pm_ctr.ctl_method;
	plan->pm_ctr.argc = mpi->pm_ctr.argc;
	for (i = 0; i < plan->pm_ctr.argc; i++) {
		plan->pm_ctr.argv[i] = kstrdup(
				mpi->pm_ctr.argv[i], GFP_NOWAIT);
		if (!plan->pm_ctr.argv[i]) {
			printk(ERR_LOG_FMT "failed to alloc argv\n");
			failed = true;
			goto END;
		}
	}
	plan->slot_info_list = kmemdup(
		mpi->slot_info_list,
		plan->slot_size * sizeof(struct slot_info),
		GFP_NOWAIT);
	if (!plan->slot_info_list) {
		printk(ERR_LOG_FMT "failed to alloc slot_info_list\n");
		failed = true;
		goto END;
	}
	plan->slot_enable_seq = kmemdup(
		mpi->slot_enable_seq,
		plan->slot_size * sizeof(int),
		GFP_NOWAIT);
	if (!plan->slot_enable_seq) {
		printk(ERR_LOG_FMT "failed to alloc slot_enable_seq\n");
		failed = true;
		goto END;
	}
END:
	if (failed) {
		free_hotplug_check_plan(plan);
		plan = NULL;
	}
	return plan;
}

static void gen_hotplug_plan_list(struct list_head *plan_list)
{
	unsigned long flags;
	struct machine_pm_info *curr_mpi = NULL;
	struct hotplug_check_plan *plan = NULL;

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	list_for_each_entry(curr_mpi, &g_machine_pm_list, info_list) {
		if (curr_mpi->mon_cfg.mon_mode == SYNO_DPM_MONITOR_MODE_POLLING) {
			if (!time_after(jiffies, curr_mpi->mon_expired_time_jiffy)) {
				continue;
			}
		} else {
			if (!curr_mpi->trigger_rescan_by_inter) {
				continue;
			}
		}

		plan = gen_hotplug_check_plan(curr_mpi);
		if (!plan) {
			continue;
		}
		list_add_tail(&plan->list, plan_list);
	}
	spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
}

static bool CheckAllQuotaReleased(void)
{
	int i = 0;
	bool result = true;
	unsigned long flags;
	struct machine_pm_info *curr_mpi = NULL;

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	list_for_each_entry(curr_mpi, &g_machine_pm_list, info_list) {
		for (i = 0; i < curr_mpi->pm_conf.quota_count; i++) {
			if (curr_mpi->quota_list[i].ower_slot != 0) {
				result = false;
				break;
			}
		}
		if (!result) {
			break;
		}
	}
	spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
	return result;
}

static int monitor_thread_fn(void * unused)
{
	bool all_handled = false;
	unsigned long flags;
	struct hotplug_check_plan *plan = NULL;
	struct machine_pm_info *target_mpi = NULL;
	LIST_HEAD(hotplug_check_plan_list);

	while (!kthread_should_stop()) {
		gen_hotplug_plan_list(&hotplug_check_plan_list);
		all_handled = handle_hotplug_check_plan(
				&hotplug_check_plan_list, "hotplug_polling_checker");

		spin_lock_irqsave(&g_machine_pm_list_lock, flags);
		list_for_each_entry(plan, &hotplug_check_plan_list, list) {
			if (plan->all_slot_handled) {
				target_mpi = get_mpi_by_uuid(plan->uuid);
				if (!target_mpi) {
					printk(ERR_LOG_FMT "uuid[%s] isn't in machine list\n", plan->uuid);
					continue;
				}

				if (target_mpi->mon_cfg.mon_mode == SYNO_DPM_MONITOR_MODE_POLLING) {
					target_mpi->mon_expired_time_jiffy = jiffies +
						target_mpi->mon_cfg.polling_interval * HZ;
				} else {
					target_mpi->trigger_rescan_by_inter = false;
				}
			}
		}
		spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
		free_hotplug_check_plan_list(&hotplug_check_plan_list);

		if (all_handled && unlikely(!g_hotplug_monitor_activated)) {
			if (CheckAllQuotaReleased()) {
				g_hotplug_monitor_activated = true;
			}
		}
		msleep(1000);
	}
	return 0;
}

bool syno_dpm_hotplug_monitor_activated(void)
{
	return g_hotplug_monitor_activated;
}

int syno_dpm_monitor_start(void)
{
	if (!monitor_thread) {
		monitor_thread = kthread_run(
				monitor_thread_fn, NULL, "syno_dpm_monitor");
		if (!monitor_thread) {
			printk(ERR_LOG_FMT "unable to start monitor thread\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void interrupt_rescan_handler(struct work_struct *work)
{
	unsigned long flags;
	struct machine_pm_info *curr_mpi = NULL;

	spin_lock_irqsave(&g_machine_pm_list_lock, flags);
	list_for_each_entry(curr_mpi, &g_machine_pm_list, info_list) {
		if (curr_mpi->mon_cfg.mon_mode == SYNO_DPM_MONITOR_MODE_INTERRUPT) {
			curr_mpi->trigger_rescan_by_inter = true;
		}
	}
	spin_unlock_irqrestore(&g_machine_pm_list_lock, flags);
}

void syno_dpm_trigger_hotplug_rescan(void)
{
	queue_work(dpm_interrupt_rescan_wq, &interrupt_rescan_work);
}
EXPORT_SYMBOL(syno_dpm_trigger_hotplug_rescan);

bool syno_dpm_check_support(const char *uuid)
{
	if (!g_support_syno_dpm || !uuid) {
		return false;
	}
	if (strlen(uuid) == 0) {
		return false;
	}

	return true;
}
EXPORT_SYMBOL(syno_dpm_check_support);

static int __init syno_dpm_init(void)
{
	if (!g_support_syno_dpm) {
		printk(INFO_LOG_FMT "This model doesn't support Disk Power Manager\n");
		return 0;
	}

	dpm_quota_wq = alloc_workqueue("dpm_quota_wq", 0, 1);
	if (!dpm_quota_wq) {
		printk(ERR_LOG_FMT "unable to alloc dpm_quota_wq\n");
		return -EINVAL;
	}
	dpm_interrupt_rescan_wq = alloc_workqueue("dpm_interrupt_rescan_wq", 0, 1);
	if (!dpm_interrupt_rescan_wq) {
		printk(ERR_LOG_FMT "unable to alloc dpm_interrupt_rescan_wq\n");
		return -EINVAL;
	}
	INIT_WORK(&interrupt_rescan_work, interrupt_rescan_handler);

	if (0 != syno_dmp_procfs_init()) {
		return -EINVAL;
	}
	return 0;
}

static void __exit syno_dpm_exit(void)
{
	if (!g_support_syno_dpm) {
		printk(INFO_LOG_FMT "This model doesn't support Disk Power Manager\n");
		return;
	}

	syno_dmp_procfs_free();

	if (monitor_thread) {
		kthread_stop(monitor_thread);
		monitor_thread = NULL;
	}

	if (dpm_interrupt_rescan_wq) {
		destroy_workqueue(dpm_interrupt_rescan_wq);
		dpm_interrupt_rescan_wq = NULL;
	}
	if (dpm_quota_wq) {
		destroy_workqueue(dpm_quota_wq);
		dpm_quota_wq = NULL;
	}

	machine_pm_list_free(&g_machine_pm_list);
	return;
}

MODULE_AUTHOR("BUT");
MODULE_DESCRIPTION("Synology Disk Power Manager\n");
MODULE_LICENSE("GPL v2");

device_initcall(syno_dpm_init);
module_exit(syno_dpm_exit);
