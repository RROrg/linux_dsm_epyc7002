// Copyright (c) 2000-2024 Synology Inc. All rights reserved.
#include <linux/list.h>
#include <linux/synolib.h>
#include <linux/kern_levels.h>
#include <linux/timekeeping.h>

// #define DPM_DEBUG

#define MAX_SLOT_SIZE 120
#define TOPOLOGY_KEY_VALUE_MAX_SIZE 64
#define MAX_CTR_ARGS 6

#define SZ_SYNO_DPM_NAME "syno_disk_pwr_mgr"

#define DPM_FMT SZ_SYNO_DPM_NAME ": "
#define DEBUG_LOG_FMT KERN_DEBUG DPM_FMT
#define INFO_LOG_FMT KERN_INFO DPM_FMT
#define NOTICE_LOG_FMT KERN_NOTICE DPM_FMT
#define ERR_LOG_FMT KERN_ERR DPM_FMT

#define SYNO_DPM_CHANGING_ENABLE_THRESHOLD 20
#define SYNO_DPM_CHANGING_DISABLE_THRESHOLD 1

enum SYNO_DPM_CTL_METHOD {
	SYNO_DPM_CTL_METHOD_UNKNOWN = 0,
	SYNO_DPM_CTL_METHOD_HOST,
	SYNO_DPM_CTL_METHOD_EUNIT_USB,
    SYNO_DPM_CTL_METHOD_HOST_USERSPACE_HELPER,
    SYNO_DPM_CTL_METHOD_EUNIT_USERSPACE_HELPER,
#ifdef DPM_DEBUG
	SYNO_DPM_CTL_METHOD_DEBUG = 100,
#endif
};

enum SYNO_DPM_MONITOR_MODE {
	SYNO_DPM_MONITOR_MODE_UNKNOWN = 0,
	SYNO_DPM_MONITOR_MODE_POLLING,
	SYNO_DPM_MONITOR_MODE_INTERRUPT,
};

enum SYNO_DPM_SLOT_STATUS {
	SYNO_DPM_SLOT_STATUS_UNKNOWN = 0,
	SYNO_DPM_SLOT_STATUS_UNDETECTED,// undetected
	SYNO_DPM_SLOT_STATUS_DETECTED, // detected
	SYNO_DPM_SLOT_STATUS_DEEP_SLEEP_RETRY, // in deep sleep or deep retry
	SYNO_DPM_SLOT_STATUS_WAIT_FOR_WAKE, // wake from deep sleep
};

enum SYNO_DPM_SLOT_CHANGING_STATUS {
	SYNO_DPM_SLOT_CHANGING_STATUS_NONE = 0,
	SYNO_DPM_SLOT_CHANGING_STATUS_ENABLE,
	SYNO_DPM_SLOT_CHANGING_STATUS_DISABLE,
};

enum SYNO_DPM_RELEASE_METHOD {
	SYNO_DPM_RELEASE_METHOD_UNKNOWN = 0,
	SYNO_DPM_RELEASE_METHOD_DEVICE_DRIVER,
	SYNO_DPM_RELEASE_METHOD_DEEPSLEEP_FIXED_DELAY,
};

struct slot_info {
	enum SYNO_DPM_SLOT_STATUS status;
	enum SYNO_DPM_SLOT_CHANGING_STATUS changing_status;
	int changing_threshold;
};

struct pm_ctrl_method {
	enum SYNO_DPM_CTL_METHOD ctl_method;
	int argc;
	char *argv[MAX_CTR_ARGS];
};

struct monitor_config {
	enum SYNO_DPM_MONITOR_MODE mon_mode;
	int polling_interval;
};

struct pm_ctrl_config {
	int slot_size;
	int quota_count;
	int additional_delay;
	int deepsleep_fixed_delay;
};

struct quota_status {
	int ower_slot;
	unsigned long start_time_jiffy;
	unsigned long expire_time_jiffy;
	enum SYNO_DPM_RELEASE_METHOD rel_method;
};

struct machine_pm_info {
	char uuid[SYNO_DPM_UUID_LEN_MAX];
	struct pm_ctrl_config pm_conf;
	struct pm_ctrl_method pm_ctr;
	struct monitor_config mon_cfg;

	struct slot_info *slot_info_list;
	struct quota_status *quota_list;
	unsigned long mon_expired_time_jiffy;
	struct workqueue_struct *power_ctrl_wq;
	int *slot_enable_seq;
	bool trigger_rescan_by_inter;

	struct list_head info_list;
};

struct topology_config {
	char uuid[SYNO_DPM_UUID_LEN_MAX];
	int slot_size;
	int quota_size;
	enum SYNO_DPM_CTL_METHOD ctl_method;
	char* ctl_args[MAX_CTR_ARGS];

	enum SYNO_DPM_MONITOR_MODE mon_mode;
	int polling_interval;
	int additional_tune_delay;
	int deepsleep_fixed_delay;
	int slot_enable_seq[MAX_SLOT_SIZE];

	struct list_head conf_list;
};

/* file opperations */
int syno_dmp_procfs_init(void);
void syno_dmp_procfs_free(void);

/* topology config */
void topology_config_init(struct topology_config *topo_cfg);

/* machine pm list */
int machine_pm_list_update(const struct list_head *new_config_list);
int machine_pm_list_dump(char *str_result, size_t buf_size);

/* hotplug monitor */
bool syno_dpm_hotplug_monitor_activated(void);
int syno_dpm_monitor_start(void);

/* disk power control */
/*
 * Schedule the disk power enable task, this function is expected to be called by
 * any content.
 *
 * @param [IN] target_mpi: the target machine pm info
 * @param [IN] slot: the slot index
 *
 * @return 0: success,
 *        -1: error
 */
int dpm_disk_power_enable_schedule(
	const struct machine_pm_info *target_mpi, unsigned int slot);
/*
 * Schedule the disk power disable task, this function is expected to be called by
 * any content.
 *
 * @param [IN] target_mpi: the target machine pm info
 * @param [IN] slot: the slot index
 *
 * @return 0: success,
 *        -1: error
 */
int dpm_disk_power_disable_schedule(
	const struct machine_pm_info *target_mpi, unsigned int slot);
/*
 * Check the enable pin, this function may content switch
 *
 * @param [IN] pm_ctr: the power control method info
 * @param [IN] slot: the slot index
 *
 * @return 1: enable pin is enable
 * 	       0: enable pin is disable
 *        -1: error
 */
int dpm_disk_power_enable_check(const struct pm_ctrl_method *pm_ctr, int slot);
/*
 * Check the present pin, this function may content switch
 *
 * @param [IN] pm_ctr: the power control method info
 * @param [IN] slot: the slot index
 *
 * @return 1: enable pin is enable
 * 	       0: enable pin is disable
 *        -1: error
 */
int dpm_disk_power_present_check(const struct pm_ctrl_method *pm_ctr, int slot);
