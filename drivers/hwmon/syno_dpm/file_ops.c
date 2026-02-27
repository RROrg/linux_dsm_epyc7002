// Copyright (c) 2000-2024 Synology Inc. All rights reserved.

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "dpm_int.h"

#define SZ_PROC_SYNO_DPM_ROOT SZ_SYNO_DPM_NAME
#define SZ_PROC_SYNO_DPM_TOPOLOGY "machine_topology"
#define SZ_PROC_SYNO_DPM_ACT_MON "activate_hotplug_monitor"
#define SZ_PROC_SYNO_DPM_LOG_LEVEL "log_level"
#ifdef DPM_DEBUG
#define SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ "debug_pwr_request"
#define SZ_PROC_SYNO_DPM_DEBUG_INTR_TRIG "debug_intr_trig"
#endif

#define SZK_TOPOLOGY_SLOT_SIZE "slot_size"
#define SZK_TOPOLOGY_QUOTA_SIZE "quota_size"
#define SZK_TOPOLOGY_CTR_METHOD "control_method"
#define SZK_TOPOLOGY_MON_MODE "monitor_mode"
#define SZK_TOPOLOGY_POLL_INTERVAL "polling_interval"
#define SZK_TOPOLOGY_TUNE_DELAY "additional_tune_delay"
#define SZK_TOPOLOGY_DEEPSLEEP_FIXED_DELAY "deepsleep_fixed_delay"
#define SZK_TOPOLOGY_SLOT_ENABLE_SEQ "slot_enable_seq"
#define SZK_TOPOLOGY_CTR_ARG_PREFIX "control_arg"

#define SZ_TOPOLOGY_VALUE_CTL_ME_HOST "host"
#define SZ_TOPOLOGY_VALUE_CTL_ME_EUNIT_USB "eunit_usb"
#define SZ_TOPOLOGY_VALUE_CTL_ME_HOST_USERHELPER "host_userspace_helper"
#define SZ_TOPOLOGY_VALUE_CTL_ME_EUNIT_USERHELPER "eunit_userspace_helper"
#ifdef DPM_DEBUG
#define SZ_TOPOLOGY_VALUE_CTL_ME_DEBUG "debug"
#endif

#define SZ_TOPOLOGY_VALUE_MON_MODE_INTR "interrupt"
#define SZ_TOPOLOGY_VALUE_MON_MODE_POLL "polling"

extern int g_syno_dpm_debug_level;

#ifdef DPM_DEBUG
#define SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_PWR "req_pwr"
#define SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_QUOTA "req_quota"
#define SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REL_PWR "rel_pwr"
#define SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_DISABLE "req_disable"
#define SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_DEEP_SLEEP "req_deep_sleep"
#define SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_DEEP_RETRY "req_deep_retry"
#define SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_CALLER_NAME "procfs_debug"

extern int syno_dpm_req_pwr_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name);
extern int syno_dpm_req_quota_only_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name);
extern int syno_dpm_rel_pwr_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name);
extern int syno_dpm_req_disble_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name);
extern int syno_dpm_req_deep_sleep_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name);
extern int syno_dpm_req_deep_retry_by_slot(const char *uuid, unsigned int slot,
		const char *caller_name);
extern void syno_dpm_trigger_hotplug_rescan(void);
#endif

static struct proc_dir_entry *proc_syno_dpm_root = NULL;

static int syno_dpm_toplogy_proc_show(struct seq_file *m, void *v)
{
	char *str_buf = NULL;

	str_buf = kmalloc(4096, GFP_KERNEL);
	if (!str_buf) {
		return -1;
	}

	memset(str_buf, 0, 4096);
	if (0 != machine_pm_list_dump(str_buf, 4096)) {
		kfree(str_buf);
		return -1;
	}

	seq_printf(m, "%s", str_buf);
	kfree(str_buf);
	return 0;
}
static int syno_dpm_toplogy_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_dpm_toplogy_proc_show, NULL);
}

static int handle_key_value_to_topo_cfg(
	const char *key, const char *val, struct topology_config *topo_cfg)
{
	int arg_idx = 0;

	if (!topo_cfg || !key || !val) {
		return -1;
	}

	if (strcmp(key, SZK_TOPOLOGY_SLOT_SIZE) == 0) {
		topo_cfg->slot_size = simple_strtol(val, NULL, 10);
	} else if (strcmp(key, SZK_TOPOLOGY_QUOTA_SIZE) == 0) {
		topo_cfg->quota_size = simple_strtol(val, NULL, 10);
	} else if (strcmp(key, SZK_TOPOLOGY_CTR_METHOD) == 0) {
		if (strcmp(val, SZ_TOPOLOGY_VALUE_CTL_ME_HOST) == 0) {
			topo_cfg->ctl_method = SYNO_DPM_CTL_METHOD_HOST;
		} else if (strcmp(val, SZ_TOPOLOGY_VALUE_CTL_ME_EUNIT_USB) == 0) {
			topo_cfg->ctl_method = SYNO_DPM_CTL_METHOD_EUNIT_USB;
		} else if (strcmp(val, SZ_TOPOLOGY_VALUE_CTL_ME_HOST_USERHELPER) == 0) {
			topo_cfg->ctl_method = SYNO_DPM_CTL_METHOD_HOST_USERSPACE_HELPER;
		} else if (strcmp(val, SZ_TOPOLOGY_VALUE_CTL_ME_EUNIT_USERHELPER) == 0) {
			topo_cfg->ctl_method = SYNO_DPM_CTL_METHOD_EUNIT_USERSPACE_HELPER;
#ifdef DPM_DEBUG
		} else if (strcmp(val, SZ_TOPOLOGY_VALUE_CTL_ME_DEBUG) == 0) {
			topo_cfg->ctl_method = SYNO_DPM_CTL_METHOD_DEBUG;
#endif
		} else {
			printk(ERR_LOG_FMT "unknown control method: %s\n", val);
			return -1;
		}
	} else if (strcmp(key, SZK_TOPOLOGY_MON_MODE) == 0) {
		if (strcmp(val, SZ_TOPOLOGY_VALUE_MON_MODE_INTR) == 0) {
			topo_cfg->mon_mode = SYNO_DPM_MONITOR_MODE_INTERRUPT;
		} else if (strcmp(val, SZ_TOPOLOGY_VALUE_MON_MODE_POLL) == 0) {
			topo_cfg->mon_mode = SYNO_DPM_MONITOR_MODE_POLLING;
		} else {
			printk(ERR_LOG_FMT "unknown monitor mode: %s\n", val);
			return -1;
		}
	} else if (strcmp(key, SZK_TOPOLOGY_POLL_INTERVAL) == 0) {
		topo_cfg->polling_interval = simple_strtol(val, NULL, 10);
	} else if (strcmp(key, SZK_TOPOLOGY_TUNE_DELAY) == 0) {
		topo_cfg->additional_tune_delay = simple_strtol(val, NULL, 10);
	} else if (strcmp(key, SZK_TOPOLOGY_DEEPSLEEP_FIXED_DELAY) == 0) {
		topo_cfg->deepsleep_fixed_delay = simple_strtol(val, NULL, 10);
	} else if (strncmp(key, SZK_TOPOLOGY_CTR_ARG_PREFIX,
						strlen(SZK_TOPOLOGY_CTR_ARG_PREFIX)) == 0) {
		arg_idx = simple_strtol(
				key + sizeof(SZK_TOPOLOGY_CTR_ARG_PREFIX) - 1, NULL, 10) - 1;
		if (arg_idx < 0 || MAX_CTR_ARGS <= arg_idx) {
			printk(ERR_LOG_FMT "invalid argument number\n");
			return -1;
		}
		if (topo_cfg->ctl_args[arg_idx]) {
			printk(ERR_LOG_FMT "argument %d already set\n", arg_idx);
			return -1;
		}

		topo_cfg->ctl_args[arg_idx] = kstrdup(val, GFP_KERNEL);
		if (!topo_cfg->ctl_args[arg_idx]) {
			printk(ERR_LOG_FMT "kstrdup fail\n");
			return -1;
		}
	} else if (strcmp(key, SZK_TOPOLOGY_SLOT_ENABLE_SEQ) == 0) {
		int offset = 0;
		char *tmp_buf = NULL;
		char *save_ptr = NULL;
		char *seq_ptr = NULL;

		tmp_buf = kstrdup(val, GFP_KERNEL);
		if (!tmp_buf) {
			printk(ERR_LOG_FMT "kstrdup fail\n");
			return -1;
		}

		save_ptr = tmp_buf;
		while (NULL != (seq_ptr = strsep(&save_ptr, ","))) {
			if (offset >= MAX_SLOT_SIZE) {
				printk(ERR_LOG_FMT "slot enable sequence is too long\n");
				kfree(tmp_buf);
				return -1;
			}
			topo_cfg->slot_enable_seq[offset++] = simple_strtol(seq_ptr, NULL, 10);
		}
		kfree(tmp_buf);
	} else {
		printk(ERR_LOG_FMT "key [%s] isn't matched any known paramter\n", key);
		return -1;
	}
	return 0;
}

static inline void trim_tailing_chars(char *str_line, const char *cs)
{
	size_t len = 0;
	if (!str_line) {
		return;
	}

	len = strlen(str_line);
	while (len > 0 && strchr(cs, str_line[len - 1])) {
		str_line[len - 1] = '\0';
		len--;
	}
}

static int get_key_and_value(
	char *str_line, char *key, size_t key_size,
	char *val, size_t val_size)
{
	size_t key_len = 0;
	size_t val_len = 0;
	char *eq_ptr = strchr(str_line, '=');
	char *key_ptr = NULL;
	char *val_ptr = NULL;

	if (eq_ptr == NULL) {
		if (g_syno_dpm_debug_level > 3) {
			printk(ERR_LOG_FMT "no '=' found in the line: %s\n", str_line);
		}
		return -1;
	}

	*eq_ptr = '\0';
	val_ptr = strchr(eq_ptr + 1, '"');
	if (!val_ptr) {
		if (g_syno_dpm_debug_level > 3) {
			printk(ERR_LOG_FMT "no '\"' found in the line: %s\n", str_line);
		}
		return -1;
	}
	val_ptr++;
	trim_tailing_chars(val_ptr, "\" ");
	key_ptr = str_line;
	trim_tailing_chars(key_ptr, "\"");

	key_len = strlen(key_ptr);
	val_len = strlen(val_ptr);
	if (key_len == 0 || key_len >= key_size || val_len == 0 || val_len >= val_size) {
		printk(ERR_LOG_FMT "key or value is empty or too long: %s\n", str_line);
		return -1;
	}

	snprintf(key, key_size, "%s", key_ptr);
	snprintf(val, val_size, "%s", val_ptr);
	return 0;
}

static ssize_t syno_dpm_toplogy_proc_write(
	struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	int ret = 0;
	char *kbuf = NULL;
	char *save_ptr = NULL;
	char *str_line = NULL;
	char *tmp_ptr = NULL;
	char key_str[TOPOLOGY_KEY_VALUE_MAX_SIZE] = {0};
	char val_str[TOPOLOGY_KEY_VALUE_MAX_SIZE] = {0};
	struct topology_config *topo_cfg = NULL;
	struct topology_config *topo_cfg_tmp = NULL;
	LIST_HEAD(new_config_list);

	if (!(kbuf = kmalloc(count + 1, GFP_KERNEL))) {
		printk(ERR_LOG_FMT "unable to alloc mem\n");
		goto ERR;
	}
	if (copy_from_user(kbuf, buf, count)) {
		goto ERR;
	}
	kbuf[count] = '\0';

	save_ptr = kbuf;
	str_line = strsep(&save_ptr, "\n");
	while (NULL != str_line) {
		if (*str_line != '[') {
			str_line = strsep(&save_ptr, "\n");
			continue;
		}
		tmp_ptr = strchr(str_line, ']');
		if (!tmp_ptr || (tmp_ptr - str_line - 1) > SYNO_DPM_UUID_LEN_MAX) {
			printk(ERR_LOG_FMT "invalid section format: \"%s\"\n", str_line);
			goto ERR;
		}

		topo_cfg = kmalloc(sizeof(struct topology_config), GFP_KERNEL);
		if (!topo_cfg) {
			printk(ERR_LOG_FMT "unable to alloc mem\n");
			goto ERR;
		}
		topology_config_init(topo_cfg);
		list_add_tail(&(topo_cfg->conf_list), &new_config_list);

		strncpy(topo_cfg->uuid, str_line + 1, (tmp_ptr - str_line - 1));
		while (NULL != (str_line = strsep(&save_ptr, "\n"))) {
			if (*str_line == '[') {
				break;
			}
			if (0 != get_key_and_value(str_line, key_str, sizeof(key_str),
						val_str, sizeof(val_str))) {
				continue;
			}
			if (0 != handle_key_value_to_topo_cfg(key_str, val_str, topo_cfg)) {
				goto ERR;
			}
		}
	}

	if (0 != machine_pm_list_update(&new_config_list)) {
		printk(ERR_LOG_FMT "failed to update the machine pm list\n");
		goto ERR;
	}

	ret = count;
ERR:
	list_for_each_entry_safe(topo_cfg, topo_cfg_tmp, &new_config_list, conf_list) {
		list_del(&topo_cfg->conf_list);
		kfree(topo_cfg);
	}

	kfree(kbuf);
	return ret;
}

static int syno_dpm_act_mon_proc_show(struct seq_file *m, void *v)
{
	if (syno_dpm_hotplug_monitor_activated()) {
		seq_printf(m, "1\n");
	} else {
		seq_printf(m, "0\n");
	}
	return 0;
}
static int syno_dpm_act_mon_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_dpm_act_mon_proc_show, NULL);
}
static ssize_t syno_dpm_act_mon_proc_write(
	struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	if (0 != syno_dpm_monitor_start()) {
		return 0;
	}
	return count;
}

static int syno_dpm_log_level_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_syno_dpm_debug_level);
	return 0;
}
static int syno_dpm_log_level_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_dpm_log_level_proc_show, NULL);
}

static ssize_t syno_dpm_log_level_proc_write(
	struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	char *kbuf = NULL;

	if (!(kbuf = kmalloc(count + 1, GFP_KERNEL))) {
		printk(ERR_LOG_FMT "unable to alloc mem\n");
		goto END;
	}
	if (copy_from_user(kbuf, buf, count)) {
		goto END;
	}

	g_syno_dpm_debug_level = simple_strtol(kbuf, NULL, 10);
END:
	kfree(kbuf);
	return count;
}

#ifdef DPM_DEBUG
static void syno_dpm_debug_helper(void)
{
	printk(INFO_LOG_FMT
		"[command] [uuid] [slot]\n"
		"command: req_pwr|req_quota|rel_pwr|req_disable|req_deep_sleep|req_deep_retry\n");
}

static int syno_dpm_debug_pwr_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "\n");
	syno_dpm_debug_helper();
	return 0;
}
static int syno_dpm_debug_pwr_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_dpm_debug_pwr_proc_show, NULL);
}

static ssize_t syno_dpm_debug_pwr_proc_write(
	struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	int ret = -1;
	int slot = 0;
	char *kbuf = NULL;
	char command[32];
	char uuid[32];
	char str_slot[32];

	if (!(kbuf = kmalloc(count, GFP_KERNEL))) {
		printk(ERR_LOG_FMT "unable to alloc mem\n");
		goto ERR;
	}
	if (copy_from_user(kbuf, buf, count)) {
		printk(ERR_LOG_FMT "copy_from_user fail\n");
		goto ERR;
	}

	if (sscanf(kbuf, "%s %s %s", command, uuid, str_slot) != 3) {
		printk(ERR_LOG_FMT "invalid input\n");
		syno_dpm_debug_helper();
		goto ERR;
	}
	slot = simple_strtol(str_slot, NULL, 10);

	if (strcmp(command, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_PWR) == 0) {
		if (0 != syno_dpm_req_pwr_by_slot(
				uuid, slot,	SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_CALLER_NAME)) {
			goto ERR;
		}
	} else if (strcmp(command, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_QUOTA) == 0) {
		if (0 != syno_dpm_req_quota_only_by_slot(
				uuid, slot, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_CALLER_NAME)) {
			goto ERR;
		}
	} else if (strcmp(command, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REL_PWR) == 0) {
		if (0 != syno_dpm_rel_pwr_by_slot(
				uuid, slot, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_CALLER_NAME)) {
			goto ERR;
		}
	} else if (strcmp(command, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_DISABLE) == 0) {
		if (0 != syno_dpm_req_disble_by_slot(
				uuid, slot, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_CALLER_NAME)) {
			goto ERR;
		}
	} else if (strcmp(command, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_DEEP_SLEEP) == 0) {
		if (0 != syno_dpm_req_deep_sleep_by_slot(
				uuid, slot, false, 0, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_CALLER_NAME)) {
			goto ERR;
		}
	} else if (strcmp(command, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_REQ_DEEP_RETRY) == 0) {
		if (0 != syno_dpm_req_deep_retry_by_slot(
				uuid, slot, SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ_CALLER_NAME)) {
			goto ERR;
		}
	} else {
		printk(ERR_LOG_FMT "unknown command: %s\n", command);
		syno_dpm_debug_helper();
		goto ERR;
	}
	ret = count;
ERR:
	kfree(kbuf);
	return ret;
}

static int syno_dpm_debug_intr_trig_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "\n");
	return 0;
}
static int syno_dpm_debug_intr_trig_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, syno_dpm_debug_intr_trig_proc_show, NULL);
}

static ssize_t syno_dpm_debug_intr_trig_proc_write(
	struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	syno_dpm_trigger_hotplug_rescan();
	return count;
}
#endif

static const struct proc_ops syno_dpm_toplogy_proc_fops = {
	.proc_open		= syno_dpm_toplogy_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
	.proc_write		= syno_dpm_toplogy_proc_write,
};

static const struct proc_ops syno_dpm_act_mon_proc_fops = {
	.proc_open		= syno_dpm_act_mon_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
	.proc_write		= syno_dpm_act_mon_proc_write,
};

static const struct proc_ops syno_dpm_log_level_proc_fops = {
	.proc_open		= syno_dpm_log_level_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
	.proc_write		= syno_dpm_log_level_proc_write,
};

#ifdef DPM_DEBUG
static const struct proc_ops syno_dpm_debug_pwr_proc_fops = {
	.proc_open		= syno_dpm_debug_pwr_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
	.proc_write		= syno_dpm_debug_pwr_proc_write,
};

static const struct proc_ops syno_dpm_debug_intr_trig_proc_fops = {
	.proc_open		= syno_dpm_debug_intr_trig_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
	.proc_write		= syno_dpm_debug_intr_trig_proc_write,
};
#endif

int syno_dmp_procfs_init()
{
	/* create procfs root entry */
	proc_syno_dpm_root = proc_mkdir(SZ_PROC_SYNO_DPM_ROOT, NULL);
	if (!proc_syno_dpm_root) {
		printk(ERR_LOG_FMT "create procfs root entry fail\n");
		return -1;
	}

	if (!proc_create_data(SZ_PROC_SYNO_DPM_TOPOLOGY, 0644,
			proc_syno_dpm_root, &syno_dpm_toplogy_proc_fops, NULL)) {
		printk(ERR_LOG_FMT "create procfs entry[%s] fail\n",
			SZ_PROC_SYNO_DPM_TOPOLOGY);
		return -1;
	}

	if (!proc_create_data(SZ_PROC_SYNO_DPM_ACT_MON, 0644,
			proc_syno_dpm_root, &syno_dpm_act_mon_proc_fops, NULL)) {
		printk(ERR_LOG_FMT "create procfs entry[%s] fail\n",
			SZ_PROC_SYNO_DPM_ACT_MON);
		return -1;
	}

	if (!proc_create_data(SZ_PROC_SYNO_DPM_LOG_LEVEL, 0644,
			proc_syno_dpm_root, &syno_dpm_log_level_proc_fops, NULL)) {
		printk(ERR_LOG_FMT "create procfs entry[%s] fail\n",
			SZ_PROC_SYNO_DPM_LOG_LEVEL);
		return -1;
	}

#ifdef DPM_DEBUG
	if (!proc_create_data(SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ, 0644,
			proc_syno_dpm_root, &syno_dpm_debug_pwr_proc_fops, NULL)) {
		printk(ERR_LOG_FMT "create procfs entry[%s] fail\n",
			SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ);
		return -1;
	}
	if (!proc_create_data(SZ_PROC_SYNO_DPM_DEBUG_INTR_TRIG, 0644,
			proc_syno_dpm_root, &syno_dpm_debug_intr_trig_proc_fops, NULL)) {
		printk(ERR_LOG_FMT "create procfs entry[%s] fail\n",
			SZ_PROC_SYNO_DPM_DEBUG_INTR_TRIG);
		return -1;
	}
#endif

	return 0;
}

void syno_dmp_procfs_free()
{
	/* remove procfs root entry */
#ifdef DPM_DEBUG
	remove_proc_entry(SZ_PROC_SYNO_DPM_DEBUG_INTR_TRIG, proc_syno_dpm_root);
	remove_proc_entry(SZ_PROC_SYNO_DPM_DEBUG_PWR_REQ, proc_syno_dpm_root);
#endif
	remove_proc_entry(SZ_PROC_SYNO_DPM_LOG_LEVEL, proc_syno_dpm_root);
	remove_proc_entry(SZ_PROC_SYNO_DPM_ACT_MON, proc_syno_dpm_root);
	remove_proc_entry(SZ_PROC_SYNO_DPM_TOPOLOGY, proc_syno_dpm_root);
	remove_proc_entry(SZ_PROC_SYNO_DPM_ROOT, NULL);
}