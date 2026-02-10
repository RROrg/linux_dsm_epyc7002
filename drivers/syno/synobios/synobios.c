#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#include <linux/syno.h>

#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>  /* error codes */
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/genhd.h>
#include <linux/major.h>
#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/sched.h>
#include <linux/bcd.h>
#include <linux/synolib.h>
#include <linux/tty.h>

#if SYNO_HAVE_KERNEL_VERSION(3,10,0)
#include <linux/seq_file.h>
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/

#ifdef CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK
#include "syno_shutdown_hook.h"
#endif /* CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK */

#include "synobios.h"
#include <linux/ioport.h>
#include "mapping.h"
#include "rtc/rtc.h"
#include <asm/io.h> // readl(), writel()
#include <linux/proc_fs.h>
#ifdef MY_DEF_HERE
#include <linux/libata.h>
#endif
#ifdef CONFIG_MD
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SECTOR_STATUS_REPORT)
extern int (*funcSYNOSendRaidEvent)(unsigned int type, unsigned int raidno, unsigned int diskno, unsigned long long sector);
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SECTOR_STATUS_REPORT) */
#endif /* CONFIG_MD */
#if defined(CONFIG_SYNO_TTY_EXPORT) || defined(MY_DEF_HERE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
#include "syno_ttyS.h"
extern int (*syno_get_current)(unsigned char, struct tty_struct *);
#ifdef CONFIG_SYNO_SYNOBIOS_EVENT
#else /* CONFIG_SYNO_SYNOBIOS_EVENT */
extern int (*funcSYNOMicropGetEvent)(struct tty_struct *);
#endif /* CONFIG_SYNO_SYNOBIOS_EVENT */
#endif /* SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
#endif /* MY_DEF_HERE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */

#ifdef MY_DEF_HERE
extern int (*funcSYNOSendScsiErrorEvent)(SYNOBIOS_EVENT_PARM parms);
#endif /* MY_DEF_HERE */

#if defined(CONFIG_SYNO_SATA_PM_DEVICE_I2C) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT)
extern int (*funcSYNOSendEunitResetEvent)(SYNOBIOS_EVENT_PARM parms);
#endif /* defined(CONFIG_SYNO_SATA_PM_DEVICE_I2C) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT) */

#if 0
#define	DBGMESG(x...)	printk(x)
#else
#ifndef MY_DEF_HERE
#define	DBGMESG(x...)
#endif
#endif

static DEFINE_MUTEX(sys_status_lock);

static DEFINE_RWLOCK(LedSetLock);

SYNO_AUTO_POWERON gPwSched;
static int check_fan = 1;
module_param(check_fan, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(check_fan, "seconds to delay before using a new device");

static int system_mode = 0;
module_param(system_mode, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(system_mode, "Distinguish in junior mode or system mode");


static struct synobios_ops *synobios_ops;
/* Each platform or model should implement their own init/cleanup function */
int synobios_model_init(struct file_operations *fops, struct synobios_ops **ops);
int synobios_model_cleanup(struct file_operations *fops, struct synobios_ops **ops);

struct sd_softc {
	int	countEvents;
	int	idxPtr;
	SYNOBIOSEVENT	rgEvents[SYNOBIOS_NEVENTS];
	wait_queue_head_t wq_poll;
	spinlock_t 	lock;
};
static struct sd_softc scSynoBios;
static SYNO_SYS_STATUS *pgSysStatus = NULL;

#define SZ_PROC_SYNOBIOS_ROOT	"synobios"
struct proc_dir_entry *proc_synobios_root = NULL;

#if (defined(CONFIG_ATA) || defined(CONFIG_SCSI_SATA))
#ifdef MY_DEF_HERE
extern int (*funcSYNOSendDiskResetPwrEvent)(unsigned int type, unsigned int diskno);
#endif /* MY_DEF_HERE */
#ifdef MY_DEF_HERE
extern int (*funcSYNOSendDiskPortDisEvent)(unsigned int type, unsigned int diskno, unsigned int shostno);
#ifdef MY_DEF_HERE
extern int (*funcSYNOSendDiskPortLostEvent)(unsigned int diskno,
		unsigned int errorType);
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
#if (defined(MY_DEF_HERE) || defined(SYNO_HAS_SDCARDREADER))
extern int (*funcSYNOGetHwCapability)(CAPABILITY *);
#ifdef MY_DEF_HERE
extern EUNIT_PWRON_TYPE (*funcSynoEunitPowerctlType)(void);
#endif
#endif
#ifdef MY_DEF_HERE
extern int (*funcSYNOSendEboxRefreshEvent)(int portIndex);
#endif
#ifdef MY_DEF_HERE
extern int (*funcSYNOSataErrorReport)(SYNOBIOS_EVENT_PARM parms);
extern int (*funcSYNODiskRetryReport)(unsigned int uiSlotIndex, unsigned int uiPmpLinks);
extern int (*funcSYNODiskTimeoutReport)(SYNOBIOS_EVENT_PARM parms);
extern int (*funcSYNODiskResetFailReport)(SYNOBIOS_EVENT_PARM parms);
#endif /* MY_DEF_HERE */
#ifdef MY_DEF_HERE
extern int (*funcSYNODeepSleepEvent)(unsigned int uiSlotIndex, unsigned int uiPmpLinks);
#endif /* MY_DEF_HERE */
#ifdef MY_DEF_HERE
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
extern int (*funcSYNODiskPowerShortBreakReport)(unsigned int uiDiskType, unsigned int uiSlotIndex, unsigned int uiSlotEMID, unsigned int uiPmpPortNumber);
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
extern int (*funcSYNODiskPowerShortBreakReport)(unsigned int uiSlotIndex, unsigned int uiPmpLinks);
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
#endif /* MY_DEF_HERE */
#endif /* CONFIG_ATA */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_ECC_NOTIFICATION)
extern int (*funcSYNOECCNotification)(unsigned int type, unsigned int syndrome, u64 memAddr);
#endif /* MY_DEF_HERE || CONFIG_SYNO_ECC_NOTIFICATION */
#ifdef CONFIG_SYNO_BUZZER_MUTE_IRQ
extern void (*funcSYNOBuzzerMuteIRQ)(void);
#endif/* CONFIG_SYNO_BUZZER_MUTE_IRQ */
#ifdef CONFIG_SYNO_DISPLAY_CPUINFO
extern unsigned int gSynoCPUInfoCore;
#if defined(CONFIG_SYNO_GRANTLEY) || defined(CONFIG_SYNO_PURLEY)
extern int gSynoMultiCPUInfoCore[CONFIG_SYNO_MULTI_CPU_NUM];
#endif
extern char gSynoCPUInfoClock[16];
#endif/* CONFIG_SYNO_DISPLAY_CPUINFO */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
extern int (*funcSYNOSATADiskLedCtrl)(int iHostNum, SYNO_DISK_LED diskLed);
#endif /* MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL */
#ifdef CONFIG_SYNO_IE_SOFT_POWER_OFF
extern int (*funcSYNOSendPowerButtonEvent)(void);
#endif /* CONFIG_SYNO_IE_SOFT_POWER_OFF */

#if defined(CONFIG_SYNO_USB_FORBID) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT)
extern void (*funcSYNOUsbProhibitEvent)(void);
#endif /* CONFIG_SYNO_USB_FORBID && ! CONFIG_SYNO_SYNOBIOS_EVENT */

#if defined(CONFIG_SYNO_SERIAL_CONSOLE_FORBID) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT)
extern void (*funcSYNOConsoleProhibitEvent)(void);
#endif /* CONFIG_SYNO_SERIAL_CONSOLE_FORBID && ! CONFIG_SYNO_SYNOBIOS_EVENT */

static int synobios_record_event_new(struct sd_softc *sc, SYNOBIOSEVENT *pEvent)
{
	unsigned long flags;

	if (scSynoBios.countEvents == SYNOBIOS_NEVENTS) {
		return 1;
	}

	spin_lock_irqsave(&scSynoBios.lock, flags);
	scSynoBios.countEvents++;
	scSynoBios.rgEvents[scSynoBios.idxPtr] = *pEvent;
	scSynoBios.idxPtr++;
	scSynoBios.idxPtr %= SYNOBIOS_NEVENTS;
	spin_unlock_irqrestore(&scSynoBios.lock, flags);

	wake_up_interruptible(&(scSynoBios.wq_poll));

	return 0;
}

int synobios_record_event(struct sd_softc *sc, u_int event_type)
{
	SYNOBIOSEVENT   event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = event_type;
	return synobios_record_event_new(sc, &event);
}

static int synobios_record_shutdown_event(unsigned int type, SYNO_SHUTDOWN_LOG shutdown_event)
{
	int ret;
	SYNOBIOSEVENT   event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_SHUTDOWN_LOG;
	event.data[0] = shutdown_event;
	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}

static int my_atoi(const char *name)
{
	int val = 0;

	for (;; name++) {
		switch (*name) {
		case '0' ... '9':
			val = 10*val+(*name-'0');
			break;
		default:
			return val;
		}
	}
}

#ifdef CONFIG_MD
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SECTOR_STATUS_REPORT)
static int synobios_record_raid_event(unsigned int type, unsigned int raidno, unsigned int diskno, unsigned long long sector)
{
	int ret;
	SYNOBIOSEVENT   event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_RAID;
	event.data[0] = type;
	event.data[1] = raidno;
	event.data[2] = diskno + 1;   // scemd record disk1,2,3,4,
								// raid driver use disk0,1,2,3
	event.data[3] = sector;

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SECTOR_STATUS_REPORT) */

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_AUTO_REMAP_REPORT)
extern int (*funcSYNOSendAutoRemapRaidEvent)(unsigned int, unsigned long long, unsigned int);


static int synobios_autoremap_raid_event(unsigned int raidno, unsigned long long sector, unsigned int diskno)
{
	int ret;
	SYNOBIOSEVENT   event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_RAID_REMAP_RECORD;
	event.data[0] = diskno + 1;
	event.data[1] = raidno;
	event.data[2] = sector;

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_AUTO_REMAP_REPORT) */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SYNC_STATUS_REPORT)
extern int (*funcSYNOSendRaidSyncEvent)(const char *szSyncType, int isSyncFinish, int isSyncInterrupt, int md_minor);

static unsigned int raid_sync_type_get(const char *szSyncType)
{
	if (NULL == szSyncType) {
		return RAID_SYNC_NONE;
	}

	if (0 == strcmp("resync", szSyncType)) {
		return RAID_SYNC_RESYNC;
	} else if (0 == strcmp("requested-resync", szSyncType)) {
		return RAID_SYNC_REQUESTED_RESYNC;
	} else if (0 == strcmp("data-check", szSyncType)) {
		return RAID_SYNC_CHECK;
	} else if (0 == strcmp("recovery", szSyncType)) {
		return RAID_SYNC_RECOVERY;
	} else if (0 == strcmp("reshape", szSyncType)) {
		return RAID_SYNC_RESHAPE;
	} else {
		return RAID_SYNC_NONE;
	}
}

static int synobios_raid_sync_event(const char *szSyncType, int isSyncFinish, int isSyncInterrupt, int md_minor)
{
	int ret = 0;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_RAID_SYNC;
	event.data[0] = raid_sync_type_get(szSyncType);
	event.data[1] = isSyncFinish;
	event.data[2] = isSyncInterrupt;
	event.data[3] = md_minor;

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SYNC_STATUS_REPORT) */
#endif /* CONFIG_MD */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_EXT4_ERROR_REPORT)
extern int (*funcSYNOSendErrorFsEvent)(const unsigned char*, const unsigned int);

static int synobios_error_fs_event(const unsigned char* szDsmVersion, const unsigned int iErrorCount)
{
	int ret;
	SYNOBIOSEVENT   event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_ERROR_FS;
	event.data[0] = my_atoi(szDsmVersion);
	event.data[1] = iErrorCount;

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}
#endif /* MY_DEF_HERE || CONFIG_SYNO_EXT4_ERROR_REPORT */

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_BTRFS_DATA_CORRECTION)
extern int (*funcSYNOSendErrorFsBtrfsEvent)(const u8*);

static int synobios_error_fs_btrfs_event(const u8* fsid)
{
	int ret;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_ERROR_FS_BTRFS;
	event.data[0] = *((unsigned long long *)fsid);
	event.data[1] = *((unsigned long long *)fsid + 1);

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}

extern int (*funcSYNOMetaCorruptedEvent)(const u8*, u64);

static int synobios_error_btrfs_meta_corrupted_event(const u8* fsid, u64 lba)
{
	int ret;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_ERROR_BTRFS_META_CORRUPTED;
	event.data[0] = *((unsigned long long *)fsid);
	event.data[1] = *((unsigned long long *)fsid + 1);
	event.data[2] = (unsigned long long)lba;

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}
#endif /* MY_DEF_HERE || CONFIG_SYNO_BTRFS_DATA_CORRECTION */

#ifdef MY_DEF_HERE
static int synobios_record_ebox_refresh_event(int portIndex)
{
	int ret = 0;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_EBOX_REFRESH;
	event.data[0] = portIndex;

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}
#endif

static int synobios_record_disk_pwr_reset_event(unsigned int type, unsigned int diskno)
{
	int ret;
	SYNOBIOSEVENT   event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_DISK_PWR_RESET;

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}

static int synobios_record_disk_port_disabled_event(unsigned int type, unsigned int diskno, unsigned int shostno)
{
	int ret;
	SYNOBIOSEVENT   event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_DISK_PORT_DISABLED;
	event.data[0] = type;
	event.data[1] = diskno;
	event.data[2] = shostno;

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}

static int synobios_disk_port_lost_report(unsigned int diskno,
		unsigned int errorType)
{
	int iRet;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_DISK_PORT_LOST;
	event.data[0] = diskno; // get by syno_libata_index_get, see DeviceNameGet for usage.
	event.data[1] = errorType; // lost error type, reference to kernel include/linux/libata.h or libhwcontrol.

	iRet = synobios_record_event_new(&scSynoBios, &event);

	return iRet;
}

static int synobios_sata_error_report(SYNOBIOS_EVENT_PARM parms)
{
	int iRet;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_SATA_ERROR_REPORT;
	event.data[0] = parms.data[0]; //get by syno_libata_index_get, see DeviceNameGet for usage.
	event.data[1] = parms.data[1] ? parms.data[2] + 1 : 0; //ex. ata5.02, pmp index is 3
	event.data[2] = parms.data[3];
	event.data[3] = parms.data[4];
	event.data[4] = parms.data[5];

	iRet = synobios_record_event_new(&scSynoBios, &event);

	return iRet;
}

static int synobios_disk_retry_report(unsigned int uiSlotIndex, unsigned int uiPmpLinks)
{
	int iRet;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_DISK_RETRY_REPORT;
	event.data[0] = uiSlotIndex; //get by syno_libata_index_get, see DeviceNameGet for usage.
	event.data[1] = 0 == uiPmpLinks ? 0 : 1; //0 if it's internal port, 1 if it's pmp port.

	iRet = synobios_record_event_new(&scSynoBios, &event);

	return iRet;
}

static int synobios_disk_timeout_report(SYNOBIOS_EVENT_PARM parms)
{
	int iRet;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_DISK_TIMEOUT_REPORT;
	event.data[0] = parms.data[0]; //get by syno_libata_index_get, see DeviceNameGet for usage.
	event.data[1] = parms.data[1] ? parms.data[2] + 1 : 0; //ex. ata5.02, pmp index is 3
	event.data[2] = parms.data[3];
	event.data[3] = parms.data[4];
	event.data[4] = parms.data[5];

	iRet = synobios_record_event_new(&scSynoBios, &event);

	return iRet;
}

static int synobios_disk_reset_fail_report(SYNOBIOS_EVENT_PARM parms)
{
	int iRet;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_DISK_RESET_FAIL_REPORT;
	event.data[0] = parms.data[0]; //get by syno_libata_index_get, see DeviceNameGet for usage.
	event.data[1] = parms.data[1] ? parms.data[2] + 1 : 0; //ex. ata5.02, pmp index is 3
	event.data[2] = parms.data[3];
	event.data[3] = parms.data[4];
	event.data[4] = parms.data[5];

	iRet = synobios_record_event_new(&scSynoBios, &event);

	return iRet;
}

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_ECC_NOTIFICATION)
static int synobios_event_ecc_notification(unsigned int type, unsigned int syndrome, u64 memAddr)
{
	int ret = 0;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_ECC_NOTIFICATION;
	event.data[0] = type;
	event.data[1] = syndrome;
	event.data[2] = memAddr;

	printk("synobios: ECC notification event.");
	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}
#endif /* MY_DEF_HERE || CONFIG_SYNO_ECC_NOTIFICATION */

static int synobios_wake_from_deep_sleep(unsigned int uiSlotIndex, unsigned int uiPmpLinks)
{
	int iRet;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_WAKE_FROM_DEEP_SLEEP;
	event.data[0] = uiSlotIndex; //get by syno_libata_index_get, see DeviceNameGet for usage.
	event.data[1] = uiPmpLinks; //ex. pmp link number. if it is internal disk, uiPmpLinks = 0.

	iRet = synobios_record_event_new(&scSynoBios, &event);

	return iRet;
}

#ifdef CONFIG_SYNO_MPC824X
extern int (*funcSYNORtcSetTime)(struct _SynoRtcTimePkt *pRtcTimePkt);
#endif /* CONFIG_SYNO_MPC824X */

#ifdef CONFIG_SYNO_PORT_MAPPING_V2
static int synobios_disk_power_short_break_report (unsigned int uiDiskType, unsigned int uiSlotIndex, unsigned int uiSlotEMID, unsigned int uiPmpPortNumber)
{
	int iRet;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_DSIK_POWER_SHORT_BREAK;
	event.data[0] = uiDiskType;
	event.data[1] = uiSlotIndex;
	event.data[2] = uiSlotEMID;
	event.data[3] = uiPmpPortNumber; //ex. pmp link number.

	iRet = synobios_record_event_new(&scSynoBios, &event);

	return iRet;
}
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
static int synobios_disk_power_short_break_report (unsigned int uiSlotIndex, unsigned int uiPmpPortNumber)
{
	int iRet;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_DSIK_POWER_SHORT_BREAK;
	event.data[0] = uiSlotIndex; // ata port number
	event.data[1] = uiPmpPortNumber; //ex. pmp link number.

	iRet = synobios_record_event_new(&scSynoBios, &event);

	return iRet;
}
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_OOM_NOTIFICATION)
extern int (*funcSYNOSendErrorOOMEvent)(const char * process_name);

static void my_prcoesstodata(const char * proc, SYNOBIOSEVENT *event)
{
	int i;
	unsigned long long *data;
	int size = strlen(proc);
	for (i = 0; i < size; i++) {
		if (i < 8)
			data = &event->data[0];
		else if (i >= 8 && i < 16)
			data = &event->data[1];
		else if (i >= 16 && i < 24)
			data = &event->data[2];
		else if (i >= 24 && i < 32)
			data = &event->data[3];
		*data = (*data) + (((unsigned long long)proc[i]) << (8 * (i % 8)));
	}
}

static int synobios_error_oom_event(const char * process_name)
{
	int ret;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_ERROR_OOM;
	my_prcoesstodata(process_name, &event);

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}
#endif /* (MY_DEF_HERE) || (CONFIG_SYNO_OOM_NOTIFICATION) */

void synobios_rtc_init(void)
{
	int ret;
	struct _SynoRtcTimePkt RtcTimePkt;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct timespec64 tv;
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	struct timespec tv;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	module_t* pSynoModule = NULL;

	memset(&RtcTimePkt, 0, sizeof(struct _SynoRtcTimePkt));
	pSynoModule = module_type_get();

	if (RTC_UNKNOWN == pSynoModule->rtc_type) {
		goto END;
	}

	/* 1. read time from rtc. */
	if (synobios_ops->get_rtc_time) {
		ret = synobios_ops->get_rtc_time(&RtcTimePkt);
	}else{
		ret = -1;
	}

	if (ret < 0) {
		printk("%s(%d) read RTC error\n", __FILE__, __LINE__);
	}
	//printk("%s(%d) XX YYYY/MM/DD hh:mm:ss %04x/%02x/%02x %02x:%02x:%02x\n", __FILE__, __LINE__, RtcTimePkt.year, RtcTimePkt.month, RtcTimePkt.day, RtcTimePkt.hour, RtcTimePkt.min, RtcTimePkt.sec);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	tv.tv_sec = mktime64(RtcTimePkt.year + 1900, RtcTimePkt.month + 1, RtcTimePkt.day, RtcTimePkt.hour, RtcTimePkt.min, RtcTimePkt.sec);
	tv.tv_nsec = 0;
	do_settimeofday64(&tv);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	tv.tv_sec = mktime(RtcTimePkt.year + 1900, RtcTimePkt.month + 1, RtcTimePkt.day, RtcTimePkt.hour, RtcTimePkt.min, RtcTimePkt.sec);
	tv.tv_nsec = 0;
	do_settimeofday(&tv);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
	printk("%d-%d-%d %d:%d:%d UTC\n", RtcTimePkt.year + 1900, RtcTimePkt.month + 1, RtcTimePkt.day, \
									RtcTimePkt.hour, RtcTimePkt.min, RtcTimePkt.sec);

END:
	return;

}

int update_comp_stat(SYNO_SYS_STATUS *pSysStatus, sys_comp_stat_t com_stat)
{
	int res = 0;
	int comp_num = sizeof(SYNO_SYS_STATUS)/sizeof(sys_comp_stat_t);
	SYNO_SYS_STAT_SIGNATURE signature = SIGNATURE_GET(com_stat);
	int idx;
	sys_comp_stat_t *pCom_stat;

	pCom_stat = (sys_comp_stat_t *)pSysStatus;
	for (idx = 0; idx < comp_num; idx++, pCom_stat++) {
		SYNO_SYS_STAT_SIGNATURE comp_signature = SIGNATURE_GET((*pCom_stat));
		if (signature == comp_signature) {
			*pCom_stat = com_stat;
			break;
		}
	}

	if (idx == comp_num) {
		res = -1;
	}

	return res;
}

#ifdef CONFIG_SYNO_DISPLAY_CPUINFO
static void syno_display_cpu_info(void)
{
	SYNO_CPU_INFO cpu;
	memset(&cpu, 0, sizeof(SYNO_CPU_INFO));

	if (synobios_ops->get_cpu_info) {
		synobios_ops->get_cpu_info(&cpu, sizeof(gSynoCPUInfoClock));
	}
	gSynoCPUInfoCore = cpu.core;
	snprintf(gSynoCPUInfoClock, sizeof(gSynoCPUInfoClock), cpu.clock);
#if defined(CONFIG_SYNO_GRANTLEY) || defined(CONFIG_SYNO_PURLEY)
	memset(gSynoMultiCPUInfoCore, 0,  CONFIG_SYNO_MULTI_CPU_NUM * sizeof(int));
	memcpy(gSynoMultiCPUInfoCore, cpu.cpucore, CONFIG_SYNO_MULTI_CPU_NUM * sizeof(int));
#endif
}
#endif

static void syno_buzzer_cleared_event(void)
{
	static unsigned long last_jiffies = INITIAL_JIFFIES;
	static unsigned int buzzer_press_count = 0;
	if (time_after(jiffies, last_jiffies + msecs_to_jiffies(500))) {
		buzzer_press_count = 0;
	} else {
		buzzer_press_count++;
	}
	if (buzzer_press_count < 3) {
		synobios_record_event(&scSynoBios, SYNO_EVENT_BUTTON_BUZZER_CLEAR);
		printk(KERN_INFO "synobios: buzzer stop button pressed\n");
		last_jiffies = jiffies;
	}
}

#ifdef CONFIG_SYNO_IE_SOFT_POWER_OFF
static void syno_power_button_event(void)
{
	synobios_record_event(&scSynoBios, SYNO_EVENT_BUTTON_SHUTDOWN);
	printk("synobios: IE soft power off\n");
}
#endif /* CONFIG_SYNO_IE_SOFT_POWER_OFF */

static void syno_usb_prohibit_event(void)
{
	synobios_record_event(&scSynoBios, SYNO_EVENT_USB_PROHIBIT);
}

static void syno_console_prohibit_event(void)
{
	synobios_record_event(&scSynoBios, SYNO_EVENT_CONSOLE_PROHIBIT);
}

static int syno_microp_get_event(struct tty_struct *tty)
{
	if (0 != strcmp(tty->name, TTY_NAME)) {
		goto END;
	}

	synobios_record_event(&scSynoBios, SYNO_EVENT_MICROP_GET);
END:
	return 0;
}

static int synobios_record_ebox_reset_event(SYNOBIOS_EVENT_PARM parms)
{
	int ret;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_EBOX_RESET;
	event.data[0] = parms.data[0];

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}

static int
synobios_record_scsi_error_event(SYNOBIOS_EVENT_PARM parms)
{
	int ret;
	SYNOBIOSEVENT event;

	memset(&event, 0, sizeof(SYNOBIOSEVENT));
	event.event = SYNO_EVENT_SCSI_ERROR;
	event.data[0] = parms.data[0];
	event.data[1] = parms.data[1] + 1;   // scemd record disk1,2,3,4,
								// scsi driver use disk0,1,2,3
	event.data[2] = parms.data[2];
	event.data[3] = parms.data[3];

	ret = synobios_record_event_new(&scSynoBios, &event);

	return ret;
}

static unsigned int synobios_poll(struct file *pfile, struct poll_table_struct *ppolltable)
{
	int revents = 0;

	if(synobios_ops->get_buzzer_cleared) {
		unsigned char buzzer_cleared = 0;
		if ( 0 == synobios_ops->get_buzzer_cleared(&buzzer_cleared) ) {
			if ( buzzer_cleared ) {
				syno_buzzer_cleared_event();
			}
		}
	}

	if (scSynoBios.countEvents) {
		revents |= (POLLIN | POLLRDNORM);
	} else {
		poll_wait(pfile, &(scSynoBios.wq_poll), ppolltable);
	}
	return (revents);
}

static long synobios_ioctl (struct file *filp,
				 unsigned int cmd, unsigned long arg)
{
	struct _SynoRtcTimePkt rtcPkt;
	struct _SynoRtcTimePkt *pRtcTimePkt = &rtcPkt;
	int ret = 0;
	int i;
	unsigned long flags;

	if (_IOC_TYPE(cmd) != SYNOBIOS_IOC_MAGIC) {
		ret = -ENOTTY;
		goto END;
	}

	switch (cmd) {
		case SYNOIO_EXDISPLAY:
#ifdef CONFIG_SYNO_MV88F6281_USBSTATION
#define USBSTATION_GPP_LED_DISK_ORANGE  40
#define USBSTATION_GPP_LED_DISK_GREEN   36
#define USBSTATION_GPP_LED_POWER        37
			{
				struct _SynoMsgPkt msgPkt;
				struct _SynoMsgPkt *pMsgPkt = &msgPkt;
				GPIO_PIN pin;
				SYNO_LED *pLed;
				SYNO_LED LedBlink = SYNO_LED_BLINKING;
				SYNO_LED LedOn = SYNO_LED_ON;
				int act_high = 0;

				/* Check fp availability */
				if (NULL == synobios_ops->set_gpio_pin ||
						NULL == synobios_ops->set_gpio_blink) {
					ret = -EFAULT;
					break;
				}
				if (copy_from_user((void *)pMsgPkt, (const void __user *)arg, sizeof(struct _SynoMsgPkt))) {
					ret = -EFAULT;
					break;
				}
				pLed = (SYNO_LED *)pMsgPkt->szMsg;
				/* Determine which GPIO pin to use */
				switch(pMsgPkt->usNum) {
					case SYNO_LED_USBSTATION_DISK_ORANGE:
						act_high = 1;
						pin.pin = USBSTATION_GPP_LED_DISK_ORANGE;
						break;
					case SYNO_LED_USBSTATION_DISK_GREEN:
						act_high = 1;
						pin.pin = USBSTATION_GPP_LED_DISK_GREEN;
						break;
					case SYNO_LED_USBSTATION_POWER:
						pin.pin = USBSTATION_GPP_LED_POWER;
						break;
					case SYNO_SYS_RUN:
						/* RUN means to steady power/status LED */
						pin.pin = USBSTATION_GPP_LED_POWER;
						pLed = &LedOn;
						break;
					case SYNO_SYS_SHUTDOWN:
						/* SHUTDOWN means to flash power/status LED */
						pin.pin = USBSTATION_GPP_LED_POWER;
						pLed = &LedBlink;
						break;
					case SYNO_BEEP_ON:
						/* No buzzer. Dont care. */
						pin.pin = -1;
						break;
					default:
						pin.pin = -1;
						printk("Unhandled msg num %08lx\n", pMsgPkt->usNum);
						break;
				}
				if (pin.pin < 0) {
					ret = -EFAULT;
					break;
				}
				/* Manipulate GPIO pin */
				switch(*pLed) {
					case SYNO_LED_OFF:
						pin.value = act_high ? 0 : 1;
						ret = synobios_ops->set_gpio_pin(&pin);
						pin.value = 0;
						synobios_ops->set_gpio_blink(&pin);
						break;
					case SYNO_LED_ON:
						pin.value = act_high ? 1 : 0;
						ret = synobios_ops->set_gpio_pin(&pin);
						pin.value = 0;
						synobios_ops->set_gpio_blink(&pin);
						break;
					case SYNO_LED_BLINKING:
						pin.value = act_high ? 1 : 0;
						ret = synobios_ops->set_gpio_pin(&pin);
						pin.value = 1;
						synobios_ops->set_gpio_blink(&pin);
						break;
					default:
						printk("Unknown LED state %d for %08lx\n",
								*pLed, pMsgPkt->usNum);
						ret = -EFAULT;
						break;
				}
			}
			break;
#else
			{
				struct _SynoMsgPkt msgPkt;
				struct _SynoMsgPkt *pMsgPkt = &msgPkt;

				if (synobios_ops->exdisplay_handler) {
					//printk("private handle for SYNOIO_EXDISPLAY, cmd 0x%04lx\n", pMsgPkt->usNum);
					if (copy_from_user((void *)pMsgPkt, (const void __user *)arg, sizeof(struct _SynoMsgPkt))) {
						ret = -EFAULT;
						break;
					}
					synobios_ops->exdisplay_handler(pMsgPkt);
				} else {
					printk("exdisplay_handler not implemented\n");
					ret = -EFAULT;
				}
			}
			break;
#endif
		case SYNOIO_NEXTEVENT:
			{
				SYNOBIOSEVENT event;
				int isSucceed = 0;

				spin_lock_irqsave(&scSynoBios.lock, flags);
				if (scSynoBios.countEvents < 0) {
					ret = -EINVAL;
				} else if (scSynoBios.countEvents == 0) {
					ret = -EAGAIN;
				} else {
					i = scSynoBios.idxPtr + SYNOBIOS_NEVENTS - scSynoBios.countEvents;
					i %= SYNOBIOS_NEVENTS;
					memcpy(&event, &scSynoBios.rgEvents[i], sizeof(SYNOBIOSEVENT));
					isSucceed = 1;
					scSynoBios.countEvents--;
				}
				spin_unlock_irqrestore(&scSynoBios.lock, flags);

				if (isSucceed) {
					if (copy_to_user((void __user *)arg, &event, sizeof(SYNOBIOSEVENT))) {
						ret = -EFAULT;
					}
				}
			}
			break;
		case SYNOIO_RTC_TIME_READ:
			if (copy_from_user((void *)pRtcTimePkt, (const void __user *)arg, sizeof(struct _SynoRtcTimePkt))) {
				ret = -EFAULT;
				break;
			}
			if (synobios_ops->get_rtc_time) {
				ret = synobios_ops->get_rtc_time(pRtcTimePkt);
			}else{
				ret = -1;
			}
			if (ret < 0) {
				printk("%s: Failed to get rtc time.\n", __FUNCTION__);
			}
			DBGMESG("(0h, %x) (1h, %x) (2h, %x) (3h, %x) (4h, %x) (5h, %x) (6h, %x)\n", (unsigned int)pRtcTimePkt->sec, (unsigned int)pRtcTimePkt->min, (unsigned int)pRtcTimePkt->hour, (unsigned int)pRtcTimePkt->weekday, (unsigned int)pRtcTimePkt->day, (unsigned int)pRtcTimePkt->month, (unsigned int)pRtcTimePkt->year);
			if (copy_to_user((void __user *)arg, pRtcTimePkt, sizeof(struct _SynoRtcTimePkt))) {
				ret=-EFAULT;
			}
			break;
		case SYNOIO_RTC_TIME_WRITE:
			if (copy_from_user((void *)pRtcTimePkt, (const void __user *)arg, sizeof(struct _SynoRtcTimePkt))) {
				ret = -EFAULT;
				break;
			}
			DBGMESG("synobios_ioctl: SYNOIO_RTC_TIME_WRITE\n");
			if (pRtcTimePkt->year < 105) {
				printk("%s: Failed to set rtc time before 2005/01/01\n",__FUNCTION__);
				ret = -1;
				break;
			}

			if (synobios_ops->set_rtc_time) {
				ret = synobios_ops->set_rtc_time(pRtcTimePkt);
			}else{
				ret=-1;
			}
			if (ret < 0) {
				printk("%s: Failed to set rtc time\n", __FUNCTION__);
			}
			DBGMESG("(0h, %x) (1h, %x) (2h, %x) (3h, %x) (4h, %x) (5h, %x) (6h, %x)\n", (unsigned int)pRtcTimePkt->sec, (unsigned int)pRtcTimePkt->min, (unsigned int)pRtcTimePkt->hour, (unsigned int)pRtcTimePkt->weekday, (unsigned int)pRtcTimePkt->day, (unsigned int)pRtcTimePkt->month, (unsigned int)pRtcTimePkt->year);
			if (copy_to_user((void __user *)arg, pRtcTimePkt, sizeof(struct _SynoRtcTimePkt))) {
				ret=-EFAULT;
			}
			break;

		case SYNOIO_MANUTIL_MODE:
			if (copy_from_user((int *)&i, (const void __user *)arg, sizeof(int))) {
				ret = -EFAULT;
				break;
			}

			if (i != 0) {
				/*for manutil test, send manutil mode swich event*/
				printk(KERN_INFO "synobios_ioctl: MANUTIL BUTTON MODE\n");
				ret = synobios_record_event(&scSynoBios, SYNO_EVENT_BUTTON_MANUTIL);
			} else {
				printk(KERN_INFO"synobios_ioctl: NORMAL BUTTON MODE\n");
				ret = synobios_record_event(&scSynoBios, SYNO_EVENT_BUTTON_NORMAL);
			}
			break;
		case SYNOIO_RECORD_EVENT:
			/*for event test, generate event from user space*/
			printk(KERN_INFO "synobios_ioctl: SYNOIO_RECORD_EVENT, event id %x\n", *((u_int *) arg));
			ret = synobios_record_event(&scSynoBios, *(u_int *)arg);
			break;

		case SYNOIO_BUTTON_RESET:
			ret = synobios_record_event(&scSynoBios, SYNO_EVENT_BUTTON_RESET);
			printk("synobios: reset button pressed, ret = %d\n", ret);
			break;
		case SYNOIO_BUTTON_POWER:
			ret = synobios_record_event(&scSynoBios, SYNO_EVENT_BUTTON_SHUTDOWN);
			printk("synobios: power button pressed, ret = %d\n", ret);
			break;
		case SYNOIO_POWER_OFF:
			ret = synobios_record_event(&scSynoBios, SYNO_EVENT_BUTTON_SHUTDOWN);
			break;
		case SYNOIO_BUTTON_USB:
			ret = synobios_record_event(&scSynoBios, SYNO_EVENT_USBCOPY_START);
			printk("synobios: usb button pressed, ret = %d\n", ret);
			break;
		case SYNOIO_SET_DISK_LED:
			{
#if defined(CONFIG_SYNO_PORT_MAPPING_V2)
				DISKLEDSTATUS diskLedStatus;
				char szNodeName[MAX_NODENAME_LEN] = {0};

				if (copy_from_user((void *)&diskLedStatus, (const void __user *)arg, sizeof(DISKLEDSTATUS))) {
					ret = -EFAULT;
					break;
				}
				if (diskLedStatus.iNodeNameLen >= MAX_NODENAME_LEN) {
					ret = -EINVAL;
					break;
				}
				if (copy_from_user((void *)szNodeName, (const void __user*)(diskLedStatus.szNodeName), diskLedStatus.iNodeNameLen)) {
					ret = -EFAULT;
					break;
				}
				diskLedStatus.szNodeName = szNodeName;
				if (synobios_ops->set_disk_led) {
					write_lock(&LedSetLock);
					ret = synobios_ops->set_disk_led(&diskLedStatus);
					write_unlock(&LedSetLock);
				} else {
					ret = -EPERM;
				}
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
				DISKLEDSTATUS diskLedStatus;
				if (copy_from_user((void *)&diskLedStatus, (const void __user *)arg, sizeof(DISKLEDSTATUS))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->set_disk_led) {
#ifdef CONFIG_SYNO_RTD129X
					/* rtd1296 control gpio by gpio_requst and gpio_direction_output,
					 * so rtd1296 needn't apply lock again */
					ret = synobios_ops->set_disk_led(diskLedStatus.diskno, diskLedStatus.status);
#else /* CONFIG_SYNO_RTD129X */
					write_lock(&LedSetLock);
					ret = synobios_ops->set_disk_led(diskLedStatus.diskno, diskLedStatus.status);
					write_unlock(&LedSetLock);
#endif /* CONFIG_SYNO_RTD129X */
				}else{
					ret=-1;
				}
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
				break;
			}
		case SYNOIO_GET_FAN_STATUS:
			{
				FANSTATUS  FanStatus;
				if (copy_from_user((void *)&FanStatus, (const void __user *)arg, sizeof(FANSTATUS))) {
					ret = -EFAULT;
					break;
				}
				if (check_fan == 0) {
					FanStatus.status = FAN_STATUS_RUNNING;
					return 0;
				}
				if (synobios_ops->get_fan_status) {
					ret = synobios_ops->get_fan_status(FanStatus.fanno, &(FanStatus.status));
				}else{
					ret=-1;
				}
				if (copy_to_user((void __user *)arg, &FanStatus, sizeof(FANSTATUS))) {
					ret=-EFAULT;
				}

				break;
			}
		case SYNOIO_SET_FAN_STATUS:
			{
				FANSTATUS  FanStatus;
				if (copy_from_user((void *)&FanStatus, (const void __user *)arg, sizeof(FANSTATUS))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->set_fan_status) {
					ret = synobios_ops->set_fan_status(FanStatus.status, FanStatus.speed);
				}else{
					ret = -1;
				}

				break;
			}
		case SYNOIO_GET_FAN_NUM:
			{
				int fanNum = 0;
				ret = GetFanNum(&fanNum);
				if (copy_to_user((void __user *)arg, &fanNum, sizeof(int))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_GET_DS_BRAND:
			{
				int brand = 0;

				if (synobios_ops->get_brand) {
					brand = synobios_ops->get_brand();
				}

				if (brand != BRAND_SYNOLOGY && brand != BRAND_LOGITEC && brand != BRAND_SYNOLOGY_USA) {
					ret = -EINVAL;
				}
				if (copy_to_user((void __user *)arg, &brand, sizeof(int))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_GET_DS_MODEL:
			{
				int model = 0;

				if (synobios_ops->get_model) {
					model = synobios_ops->get_model();
				}
				if (copy_to_user((void __user *)arg, &model, sizeof(int))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_GET_CPLD_VERSION:
			{
				int version = 0;

				if (synobios_ops->get_cpld_version) {
					version = synobios_ops->get_cpld_version();
				}
				if (copy_to_user((void __user *)arg, &version, sizeof(int))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_GET_TEMPERATURE:
			{
				struct _SynoThermalTemp thermalTemp;

				if (copy_from_user((void *)&thermalTemp, (const void __user *)arg, sizeof(struct _SynoThermalTemp))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->get_sys_temperature) {
					ret = synobios_ops->get_sys_temperature(&thermalTemp);
				}else{
					ret=-1;
				}
				if (copy_to_user((void __user *)arg, &thermalTemp, sizeof(struct _SynoThermalTemp))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_GET_CPLD_REG:
			{
				CPLDREG cpld;

				memset(&cpld, 0, sizeof(CPLDREG));
				if (synobios_ops->get_cpld_reg) {
					ret = synobios_ops->get_cpld_reg(&cpld);
				}else{
					ret=-1;
				}
				if (copy_to_user((void __user *)arg, &cpld, sizeof(cpld))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_SET_MEM_BYTE:
			{
				MEMORY_BYTE memory;
				if (copy_from_user((void *)&memory, (const void __user *)arg, sizeof(MEMORY_BYTE))) {
					ret = -EFAULT;
					break;
				}

				if (synobios_ops->set_mem_byte) {
					ret = synobios_ops->set_mem_byte(&memory);
				}else{
					ret=-1;
				}
				break;
			}
		case SYNOIO_GET_MEM_BYTE:
			{
				MEMORY_BYTE memory;

				memset(&memory, 0, sizeof(MEMORY_BYTE));
				if (synobios_ops->get_mem_byte) {
					ret = synobios_ops->get_mem_byte(&memory);
				}else{
					ret=-1;
				}
				if (copy_to_user((void __user *)arg, &memory, sizeof(MEMORY_BYTE))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_GPIO_PIN_WRITE:
			{
				GPIO_PIN gpin;;
				if (copy_from_user((void *)&gpin, (const void __user *)arg, sizeof(GPIO_PIN))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->set_gpio_pin) {
					ret = synobios_ops->set_gpio_pin(&gpin);
				}else{
					ret=-1;
				}
				break;
			}
		case SYNOIO_GPIO_PIN_READ:
			{
				GPIO_PIN gpin;
				if (copy_from_user((void *)&gpin, (const void __user *)arg, sizeof(GPIO_PIN))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->get_gpio_pin) {
					ret = synobios_ops->get_gpio_pin(&gpin);
				}else{
					ret=-1;
				}
				if (copy_to_user((void __user *)arg, &gpin, sizeof(GPIO_PIN))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_GET_AUTO_POWERON:
			{
				SYNO_AUTO_POWERON autoPowerOn;
				memset(&autoPowerOn, 0, sizeof(SYNO_AUTO_POWERON));

				if (synobios_ops->get_auto_poweron) {
					ret = synobios_ops->get_auto_poweron(&autoPowerOn);
				}else{
					ret=-1;
				}
				if (copy_to_user((void __user *)arg, &autoPowerOn, sizeof(SYNO_AUTO_POWERON))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_SET_AUTO_POWERON:
			{
				SYNO_AUTO_POWERON autoPowerOn;

				if (copy_from_user((void *)&autoPowerOn, (const void __user *)arg, sizeof(SYNO_AUTO_POWERON))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->set_auto_poweron) {
					ret = synobios_ops->set_auto_poweron(&autoPowerOn);
				}else{
					ret=-1;
				}
				break;
			}
		case SYNOIO_GET_HW_CAPABILITY:
			{
				CAPABILITY capability;
				if (copy_from_user((void *)&capability, (const void __user *)arg, sizeof(CAPABILITY))) {
					ret = -EFAULT;
					break;
				}
				ret = GetHwCapability(&capability);
				if (copy_to_user((void __user *)arg, &capability, sizeof(CAPABILITY))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_SET_ALARM_LED:
			{
				if(synobios_ops->set_alarm_led) {
					ret = synobios_ops->set_alarm_led((unsigned char)arg);
				}else{
					ret = -1;
				}
				break;
			}
		case SYNOIO_SET_OK_TO_REMOVE_LED:
			{
				if(synobios_ops->set_ok_to_remove_led) {
					ret = synobios_ops->set_ok_to_remove_led((unsigned char)arg);
				} else {
					ret = -1;
				}
				break;
			}
		case SYNOIO_GET_BUZZER_CLEARED:
			{
				//for manutil
				unsigned char ucBuzzer_cleared = 0;
				if(synobios_ops->get_buzzer_cleared) {
					ret = synobios_ops->get_buzzer_cleared(&ucBuzzer_cleared);
					if ( ucBuzzer_cleared ) {
						printk(KERN_INFO "synobios: buzzer stop button pressed, ret = %d\n", ret);
					}
				}else{
					ret = -1;
				}
				if (copy_to_user((void __user *)arg, &ucBuzzer_cleared, sizeof(char))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_SET_BUZZER_CLEAR:
			{
				if(synobios_ops->set_buzzer_clear) {
					ret = synobios_ops->set_buzzer_clear((unsigned char)arg);
				}else{
					ret = -1;
				}
				break;
			}
		case SYNOIO_GET_POWER_STATUS:
			{
				POWER_INFO powerinfo;

				memset(&powerinfo, 0, sizeof(POWER_INFO));
				if(synobios_ops->get_power_status) {
					ret = synobios_ops->get_power_status(&powerinfo);
				}else{
					ret = -1;
				}
				if (copy_to_user((void __user *)arg, &powerinfo, sizeof(POWER_INFO))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_SHUTDOWN_LOG:
			{
				int event = (SYNO_SHUTDOWN_LOG)arg;
				ret = synobios_record_shutdown_event(SYNO_EVENT_SHUTDOWN_LOG, event);
				break;
			}
		case SYNOIO_UNINITIALIZE:
			{
				if(synobios_ops->uninitialize) {
					ret = synobios_ops->uninitialize();
				}else{
					ret = -1;
				}
				break;
			}
		case SYNOIO_GET_SYS_STATUS:
			{
				SYNO_SYS_STATUS *pUSysStat = (SYNO_SYS_STATUS *)arg;
				if (NULL != pUSysStat){
					mutex_lock(&sys_status_lock);
					if (copy_to_user(pUSysStat,
								pgSysStatus,
								sizeof(SYNO_SYS_STATUS))) {
						ret = -EFAULT;
					}
					mutex_unlock(&sys_status_lock);
				} else{
					ret = -1;
				}
				break;
			}
		case SYNOIO_SET_SYS_STATUS:
			{
				sys_comp_stat_t uSysStat = (sys_comp_stat_t)arg;
				mutex_lock(&sys_status_lock);
				ret = update_comp_stat(pgSysStatus, uSysStat);
				mutex_unlock(&sys_status_lock);
				break;
			}
		case SYNOIO_GET_MODULE_TYPE:
			{
				if (copy_to_user((void __user *)arg,
							module_type_get(),
							sizeof(module_t))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_GET_BACKPLANE_STATUS:
			{
				BACKPLANE_STATUS backSt;
				memset(&backSt, 0, sizeof(BACKPLANE_STATUS));
				if (synobios_ops->get_backplane_status) {
					ret = synobios_ops->get_backplane_status(&backSt);
				}else{
					ret = -1;
				}
				if (copy_to_user((void __user *)arg, &backSt, sizeof(BACKPLANE_STATUS))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_SET_UART2:
			{
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_TTY_MICROP_CTRL)
				char szBuf[17] = {'\0'};
				int retVal = -1;
#if defined(CONFIG_SYNO_PWM_CONTROL_LED)
				struct _SynoMsgPkt MsgPkt;
#endif /* defined(CONFIG_SYNO_PWM_CONTROL_LED) */

				if (copy_from_user((char *)&szBuf, (const void __user *)arg, sizeof(szBuf) - sizeof(char))) {
					ret = -EFAULT;
					break;
				}
				szBuf[16] = '\0';
#if defined(CONFIG_SYNO_PWM_CONTROL_LED)
				if (synobios_ops->exdisplay_handler) {
					memset(&MsgPkt, 0, sizeof(struct _SynoMsgPkt));
					MsgPkt.usNum = SYNO_PURE_MESSAGE;
					MsgPkt.usLen = sizeof(szBuf);
					strncpy(MsgPkt.szMsg, szBuf, sizeof(szBuf));
					retVal = synobios_ops->exdisplay_handler(&MsgPkt);
					if (0 == retVal) {
						break;
					}
				}
#endif /* defined(CONFIG_SYNO_PWM_CONTROL_LED) */
				retVal = synobios_lock_ttyS_write(szBuf);
				if (0 > retVal) {
					ret = -1;
				}
#else /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_TTY_MICROP_CTRL) */
				ret = -1;
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_TTY_MICROP_CTRL) */
				break;
			}
			case SYNOIO_SET_AND_GET_UART2:
			{
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_TTY_MICROP_CTRL)
				UART2_BUFFER Buffer;
				if (copy_from_user((void *)&Buffer, (const void __user *)arg, sizeof(UART2_BUFFER))) {
					ret = -EFAULT;
					break;
				}
				Buffer.szBuf[UART_BUF_SIZE - 1] = '\0';
				Buffer.size = (UART_BUF_SIZE > Buffer.size + 1) ? Buffer.size + 1: UART_BUF_SIZE;
				ret = synobios_lock_ttyS_write_and_read(Buffer.szBuf, Buffer.size);
				if (0 <= ret && copy_to_user((void __user *)arg, &Buffer, sizeof(UART2_BUFFER))) {
					ret = -EFAULT;
				}
#else /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_TTY_MICROP_CTRL) */
				ret = -1;
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_TTY_MICROP_CTRL) */
				break;
			}
		case SYNOIO_GET_UART2:
			{
				UART2_BUFFER Buffer;
				if (copy_from_user((void *)&Buffer, (const void __user *)arg, sizeof(UART2_BUFFER))) {
					ret = -EFAULT;
					break;
				}
				if (Buffer.size <= 0) {
					ret = -EFAULT;
					break;
				}
				memset(Buffer.szBuf, 0, UART_BUF_SIZE);
				ret = synobios_lock_ttyS_read(Buffer.szBuf, (UART_BUF_SIZE > Buffer.size + 1) ? Buffer.size + 1 : UART_BUF_SIZE);
				if (0 <= ret && copy_to_user((void __user *)arg, &Buffer, sizeof(UART2_BUFFER))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_UART_DEBUG_CONTROL:
			{
				int micropLogControl = 0;
				if (copy_from_user((void *)&micropLogControl, (const void __user *)arg, sizeof(int))) {
					ret = -EFAULT;
					break;
				}
				if ((MICROP_LOG_OFF != micropLogControl)
				 && (MICROP_LOG_ON != micropLogControl)) {
					printk(KERN_ERR "Error microp log level, should be MICROP_LOG_ON or MICROP_LOG_OFF\n");
					ret = -EFAULT;
					break;
				}
				set_log_switch(micropLogControl);
				break;
			}
		case SYNOIO_GET_CPU_TEMPERATURE:
			{
				struct _SynoCpuTemp cpuTmp;

				if (copy_from_user((void *)&cpuTmp, (const void __user *)arg, sizeof(struct _SynoCpuTemp))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->get_cpu_temperature) {
					ret = synobios_ops->get_cpu_temperature(&cpuTmp);
				} else {
					ret = -1;
				}
				if (copy_to_user((void __user *)arg, &cpuTmp, sizeof(struct _SynoCpuTemp))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_SET_CPU_FAN_STATUS:
			{
				FANSTATUS  FanStatus;

				if (copy_from_user((void *)&FanStatus, (const void __user *)arg, sizeof(FANSTATUS))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->set_cpu_fan_status) {
					ret = synobios_ops->set_cpu_fan_status(FanStatus.status, FanStatus.speed);
				}else{
					ret = -1;
				}

				break;
			}
		case SYNOIO_SET_PHY_LED:
			{
				if (synobios_ops->set_phy_led) {
					ret = synobios_ops->set_phy_led((SYNO_LED)arg);
				}else{
					ret = -1;
				}
				break;
			}
		case SYNOIO_SET_HDD_LED:
			{
				if (synobios_ops->set_hdd_led) {
					ret = synobios_ops->set_hdd_led((SYNO_LED)arg);
				}else{
#ifdef CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL
					ret = 0;
#else 
					ret = -1;
#endif /*CONFIG_SYNO_SAS_HOST_DISK_LED_CTRL*/
				}
				break;
			}
		case SYNOIO_SET_PWR_LED:
			{
				if (synobios_ops->set_power_led) {
					synobios_ops->set_power_led((SYNO_LED)arg);
				}else{
					ret = -1;
				}
				break;
			}
		case SYNOIO_PWM_CTL:
			{
				struct _SynoPWMCTL pwmctl;
				if (copy_from_user((void *)&pwmctl, (const void __user *)arg, sizeof(struct _SynoPWMCTL))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->pwm_ctl) {
					ret = synobios_ops->pwm_ctl(&pwmctl);
				}else{
					ret = -1;
				}
				if (copy_to_user((void __user *)arg, &pwmctl, sizeof(struct _SynoPWMCTL))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_CHECK_MICROP_ID:
			{
				if (synobios_ops->check_microp_id) {
					ret = synobios_ops->check_microp_id(synobios_ops);
				} else {
					ret = 0;
				}
				break;
			}
		case SYNOIO_GET_EUNIT_TYPE:
			{
				EUNIT_PWRON_TYPE pwronType = GetEUnitType();
				if (copy_to_user((void __user *)arg,
							&pwronType,
							sizeof(EUNIT_PWRON_TYPE))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_SUPERIO_READ:
			{
				if (synobios_ops->get_superio){
					synobios_ops->get_superio((SYNO_SUPERIO_PACKAGE *)arg);
				}
				ret = 0;
				break;
			}
		case SYNOIO_SUPERIO_WRITE:
			{
				if(synobios_ops->set_superio){
					synobios_ops->set_superio((SYNO_SUPERIO_PACKAGE *)arg);
				}
				ret = 0;
				break;
			}
		case SYNOIO_IS_FULLY_SUPPORT_EUP:
			{
#ifdef CONFIG_SYNO_CEDARVIEW
				if (synobios_ops->set_superio) {
					if (synobios_ops->get_rtc_time != rtc_bandon_get_time) {
						/* there's an external RTC */
						ret = EUP_FULLY_SUPPORT;
					} else {
						ret = EUP_NOT_FULLY_SUPPORT;
					}
				} else {
					ret = EUP_NOT_SUPPORT;
				}
				break;
#else
				ret = EUP_NOT_SUPPORT;
				break;
#endif
			}
#define MEM_READ(a) readl(a)
#define MEM_WRITE(a,v) writel(v,a)
		case SYNOIO_WRITE_MEM:
			{
				SYNO_MEM_ACCESS mem_access;

				if (copy_from_user((void *)&mem_access, (void __user *)arg, sizeof(SYNO_MEM_ACCESS))) {
					ret = -EFAULT;
					break;
				}

				if (synobios_ops->write_memory) {
					ret = synobios_ops->write_memory(&mem_access);
				} else {
					MEM_WRITE((void *)(uintptr_t)mem_access.addr, mem_access.value);
					ret = 0;
				}
				break;
			}
		case SYNOIO_READ_MEM:
			{
				SYNO_MEM_ACCESS mem_access;

				if (copy_from_user((void *)&mem_access, (const void __user *)arg, sizeof(SYNO_MEM_ACCESS))) {
					ret = -EFAULT;
					break;
				}

				if (synobios_ops->read_memory) {
					ret = synobios_ops->read_memory(&mem_access);
				} else {
					mem_access.value = MEM_READ((void *)(uintptr_t)mem_access.addr);
					ret = 0;
				}

				if (0 == ret &&
					0 != (ret = copy_to_user((void __user *)arg, &mem_access, sizeof(SYNO_MEM_ACCESS))))
				{
					ret = -EFAULT;
				}

				break;
			}
		case SYNOIO_SET_AHA_LED:
			{
				if (synobios_ops->set_aha_led) {
					synobios_ops->set_aha_led(synobios_ops, (SYNO_AHA_LED)arg);
				} else {
					ret = -1;
				}
				break;
			}
		case SYNOIO_GET_COPY_BUTTON:
			{
				int button_status;
				if(synobios_ops->get_copy_button_status) {
					// for matching userspace usage, return button_status = 0 if pressed, else = 1
					button_status = synobios_ops->get_copy_button_status();
				} else {
					ret = -ENOSYS;
					break;
				}
				if (copy_to_user((void __user *)arg, &button_status, sizeof(int))) {
					ret = -EFAULT;
				}
				break;
			}
		case HWMON_GET_SUPPORT:
			{
				SYNO_HWMON_SUPPORT hwmon_support;
				if (copy_from_user((void *)&hwmon_support, (const void __user *)arg, sizeof(SYNO_HWMON_SUPPORT))) {
					ret = -EFAULT;
					break;
				}
				ret = GetHWMONSupport(&hwmon_support);
				if (copy_to_user((void __user *)arg, &hwmon_support, sizeof(SYNO_HWMON_SUPPORT))) {
					ret = -EFAULT;
				}
				break;
			}
		case HWMON_GET_CPU_TEMPERATURE:
			{
				SYNO_HWMON_SENSOR_TYPE cpu_temp;
				SYNOCPUTEMP cpuTmp;
				int index = 0;

				if (copy_from_user((void *)&cpu_temp, (const void __user *)arg, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
					break;
				}

				cpuTmp.blSurface = 1;
				if (synobios_ops->get_cpu_temperature) {
					ret = synobios_ops->get_cpu_temperature(&cpuTmp);
					cpu_temp.sensor_num = cpuTmp.cpu_num;
					for (index = 0 ; index < cpu_temp.sensor_num ; index++) {
						snprintf(cpu_temp.sensor[index].value, sizeof(cpu_temp.sensor[index].value), "%d", cpuTmp.cpu_temp[index]);
					}
				} else {
					ret = -1;
					break;
				}

				if (copy_to_user((void __user *)arg, &cpu_temp, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
				}
				break;
			}
		case HWMON_GET_FAN_SPEED_RPM:
			{
				SYNO_HWMON_SENSOR_TYPE fan_rpm;
				if (copy_from_user((void *)&fan_rpm, (const void __user *)arg, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->hwmon_get_fan_speed_rpm) {
					ret = synobios_ops->hwmon_get_fan_speed_rpm(&fan_rpm);
				} else{
					ret = -1;
					break;
				}
				if (copy_to_user((void __user *)arg, &fan_rpm, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
				}
				break;
			}
		case HWMON_GET_PSU_STATUS:
			{
				SYNO_HWMON_SUPPORT psu_support;
				SYNO_HWMON_SENSOR_TYPE *psusts = NULL;

				memset(&psu_support, 0, sizeof(SYNO_HWMON_SUPPORT));
				psu_support.id = HWMON_PSU_STATUS;
				if ( 0 > (ret = GetHWMONSupport(&psu_support))) {
					ret = -EFAULT;
					break;
				}

				if (NULL == (psusts = kmalloc(psu_support.support * sizeof(SYNO_HWMON_SENSOR_TYPE), GFP_KERNEL))) {
					ret = -EFAULT;
					break;
				}

				if (copy_from_user((void *)psusts, (const void __user *)arg, psu_support.support * sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
					if (psusts) {
						kfree(psusts);
					}
					break;
				}
				if (synobios_ops->hwmon_get_psu_status) {
					ret = synobios_ops->hwmon_get_psu_status(psusts, psu_support.support);
				} else{
					ret = -1;
					if (psusts) {
						kfree(psusts);
					}
					break;
				}
				if (copy_to_user((void __user *)arg, psusts, psu_support.support *sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
				}
				if (psusts) {
					kfree(psusts);
				}
				break;
			}
		case HWMON_GET_SYS_VOLTAGE:
			{
				SYNO_HWMON_SENSOR_TYPE sysvol;
				if (copy_from_user((void *)&sysvol, (const void __user *)arg, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->hwmon_get_sys_voltage) {
					ret = synobios_ops->hwmon_get_sys_voltage(&sysvol);
				} else{
					ret = -1;
					break;
				}
				if (copy_to_user((void __user *)arg, &sysvol, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
				}
				break;
			}
		case HWMON_GET_SYS_THERMAL:
			{
				SYNO_HWMON_SENSOR_TYPE systhermal;

				if (copy_from_user((void *)&systhermal, (const void __user *)arg, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->hwmon_get_sys_thermal) {
					ret = synobios_ops->hwmon_get_sys_thermal(&systhermal);
				} else{
					ret = -1;
					break;
				}
				if (copy_to_user((void __user *)arg, &systhermal, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
				}
				break;
			}
		case HWMON_GET_HDD_BACKPLANE:
			{
				SYNO_HWMON_SENSOR_TYPE hddbp;
				if (copy_from_user((void *)&hddbp, (const void __user *)arg, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->hwmon_get_backplane_status) {
					ret = synobios_ops->hwmon_get_backplane_status(&hddbp);
				} else{
					ret = -1;
					break;
				}
				if (copy_to_user((void __user *)arg, &hddbp, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
				}
				break;
			}
		case HWMON_GET_SYS_CURRENT:
			{
				SYNO_HWMON_SENSOR_TYPE syscurrent;
				if (copy_from_user((void *)&syscurrent, (const void __user *)arg, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
					break;
				}
				if (synobios_ops->hwmon_get_sys_current) {
					ret = synobios_ops->hwmon_get_sys_current(&syscurrent);
				} else{
					ret = -1;
					break;
				}
				if (copy_to_user((void __user *)arg, &syscurrent, sizeof(SYNO_HWMON_SENSOR_TYPE))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_GET_SYS_CURRENT:
			{
				unsigned long ulSysCurrent = 0;
				
				if(synobios_ops->get_sys_current) {
					ret = synobios_ops->get_sys_current(&ulSysCurrent);
				} else {
					ret = -ENOSYS;
					break;
				}
				if (copy_to_user((void __user *)arg, &ulSysCurrent, sizeof(unsigned long))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_CHECK_DISK_INTF:
			{
				SYNO_DISK_INTF_INFO model;

				if (synobios_ops->get_disk_intf) {
					ret = synobios_ops->get_disk_intf(&model);
				} else {
					ret = -1;
					break;
				}

				if (copy_to_user((void __user *)arg, &model, sizeof(SYNO_DISK_INTF_INFO))) {
					ret = -EFAULT;
				}
				break;
			}
		case SYNOIO_SET_RP_FAN:
			{
				if(synobios_ops->set_rp_fan) {
					ret = synobios_ops->set_rp_fan((unsigned char)arg);
				}
				break;
			}
		default:
			ret=-ENOSYS;
			//printk(KERN_INFO "synobios_ioctl: un-defined ioctl number %x\n", cmd);
			break;
	}
END:
	return ret;
}

typedef struct _tag_SYNO_CPU_MAPPING {
	CPU_ARCH_INFO_T id;
	char *cpu_number;
} SYNO_CPU_MAPPING;

static SYNO_CPU_MAPPING gSynoCPUMapping[] = {
	{CPU_E5_2620v3, "INTEL, Xeon, E5-2620 v3, 12"},
	{CPU_E5_2609v3, "INTEL, Xeon, E5-2609 v3, 12"},
	{CPU_E3_1230v2, "INTEL, Xeon, E3-1230 v2, 4"},
	{CPU_D_1508, "INTEL, Pentium, D1508, 2"},
	{CPU_D_1521, "INTEL, Xeon, D-1521, 4"},
	{CPU_D_1527, "INTEL, Xeon, D-1527, 4"},
	{CPU_D_1528, "INTEL, Xeon, D-1528, 6"},
	{CPU_D_1531, "INTEL, Xeon, D-1531, 6"},
	{CPU_D_1541, "INTEL, Xeon, D-1541, 8"},
	{CPU_D_1567, "INTEL, Xeon, D-1567, 12"},
	{CPU_I3_2100, "INTEL, Core, i3-2100, 2"},
	{CPU_I3_4130, "INTEL, Core, i3-4130, 2"},
	{CPU_D410, "INTEL, Atom, D410, 1"},
	{CPU_D425, "INTEL, Atom, D425, 1"},
	{CPU_D510, "INTEL, Atom, D510, 2"},
	{CPU_D525, "INTEL, Atom, D525, 2"},
	{CPU_D2700, "INTEL, Atom, D2700, 2"},
	{CPU_CE5335, "INTEL, Atom, CE5335, 2"},
	{CPU_88F6281, "MARVELL, Kirkwood, 88F6281, 1"},
	{CPU_88F6282, "MARVELL, Kirkwood, 88F6282, 1"},
	{CPU_88F6702, "MARVELL, Kirkwood, 88F6702, 1"},
	{CPU_88F6707, "MARVELL, Armada 370, 88F6707, 1"},
	{CPU_88F6720, "MARVELL, Armada 375, 88F6720, 2"},
	{CPU_88F6820, "MARVELL, Armada 385, 88F6820, 2"},
	{CPU_88F6828, "MARVELL, Armada 388, 88F6828, 2"},
	{CPU_88F3710, "MARVELL, Armada 3710, 88F3710, 1"},
	{CPU_88F3720, "MARVELL, Armada 3720, 88F3720, 2"},
	{CPU_MV78230, "MARVELL, Armada XP, MV78230, 2"},
	{CPU_8241, "FREESCALE, PowerQUICC II, 8241, 1"},
	{CPU_8533e, "FREESCALE, PowerQUICC III, 8533e, 1"},
	{CPU_P1022, "FREESCALE, QorIQ, P1022, 2"},
	{CPU_C2000, "MINDSPEED, Comcerto, C2000, 2"},
	{CPU_LS1024, "Freescale, QorIQ series, LS1024, 2"},
	{CPU_AL212, "ANNAPURNALABS, Alpine, AL212, 2"},
	{CPU_AL314, "ANNAPURNALABS, Alpine, AL314, 4"},
	{CPU_AL514, "ANNAPURNALABS, Alpine, AL514, 4"},
	{CPU_C2538, "INTEL, Atom, C2538, 4"},
	{CPU_J1800, "INTEL, Celeron, J1800, 2"},
	{CPU_J3355, "INTEL, Celeron, J3355, 2"},
	{CPU_J3455, "INTEL, Celeron, J3455, 4"},
	{CPU_J4005, "INTEL, Celeron, J4005, 2"},
	{CPU_J4025, "INTEL, Celeron, J4025, 2"},
	{CPU_J4105, "INTEL, Celeron, J4105, 4"},
	{CPU_J4125, "INTEL, Celeron, J4125, 4"},
	{CPU_J4205, "INTEL, Pentium, J4205, 4"},
	{CPU_H412, "STM, Monaco, STiH412, 2"},
	{CPU_HI3535, "Hisillicon, Hi3535, V100, 2"},
	{CPU_N3000, "INTEL, Celeron, N3000, 2"},
	{CPU_N3050, "INTEL, Celeron, N3050, 2"},
	{CPU_N3150, "INTEL, Celeron, N3150, 4"},
	{CPU_N3700, "INTEL, Pentium, N3700, 4"},
	{CPU_NVR, "Embedded, NVR, Soc, 2"},
	{CPU_N3010, "INTEL, Celeron, N3010, 2"},
	{CPU_N3060, "INTEL, Celeron, N3060, 2"},
	{CPU_N3160, "INTEL, Celeron, N3160, 4"},
	{CPU_N3710, "INTEL, Pentium, N3710, 4"},
	{CPU_HI3536, "Hisilicon, Hi3536, , 4"},
	{CPU_RTD1296, "Realtek, RTD1296, SoC, 4"},
	{CPU_RTD1293, "Realtek, RTD1293, SoC, 2"},
	{CPU_C3538, "INTEL, Atom, C3538, 4"},
	{CPU_SILVER_4110, "INTEL, Xeon, Silver 4110, 16"},
	{CPU_RTD1619, "Realtek, RTD1619, SoC, 6"},
	{CPU_D_2143IT, "INTEL, Xeon, D-2143IT, 8"},
	{CPU_V1500B, "AMD, Ryzen, V1500B, 4"},
	{CPU_SILVER_4210R, "INTEL, Xeon, Silver 4210R, 20"},
	{CPU_SILVER_4214R, "INTEL, Xeon, Silver 4214R, 24"},
	{CPU_V1780B, "AMD, Ryzen, V1780B, 4"},
	{CPU_EPYC7272, "AMD, EPYC, 7272, 12"},
	{CPU_R1600, "AMD, Ryzen, R1600, 2"},
	{CPU_RTD1619B, "Realtek, RTD1619B, SoC, 4"},
	{CPU_EPYC7232P, "AMD, EPYC, 7232P, 8"},
	{CPU_EPYC7443P, "AMD, EPYC, 7443P, 24"},
	{CPU_UNKNOWN, "unknown, unknown, unknown, unknown"}
};

typedef struct _tag_SYNO_CRYPTO_MAPPING {
	CRYPTO_HW_INFO_T hw_name;
	char *crypto_capabilities;
} SYNO_CRYPTO_MAPPING;

/* this is a hint for userspace to utilize crypto engine */
static SYNO_CRYPTO_MAPPING gSynoCRYPTOMapping[] = {
	{CRYPTO_A370,       "AES_CBC, DES_CBC, 3DES_CBC, MD5, MD5_HMAC, SHA1, SHA1_HMAC"},
	{CRYPTO_A375,       "AES_CBC, DES_CBC, 3DES_CBC, MD5, MD5_HMAC, SHA1, SHA1_HMAC"},
	{CRYPTO_A38X,       "AES_CBC, DES_CBC, 3DES_CBC, MD5, MD5_HMAC, SHA1, SHA1_HMAC"},
	{CRYPTO_RTD129X,    "AES_CBC, DES_CBC, 3DES_CBC, MD5, MD5_HMAC, SHA1, SHA1_HMAC"},
	{CRYPTO_AXP,        "AES_CBC, DES_CBC, 3DES_CBC, MD5, MD5_HMAC, SHA1, SHA1_HMAC"},
	{CRYPTO_COMCERTO2K, "DES_CBC, 3DES_CBC, MD5, MD5_HMAC, SHA1, SHA1_HMAC, SHA2_256_HMAC"},
	{CRYPTO_ALPINE,     "AES_CBC, AES_ECB, AES_CTR, DES_CBC, DEC_ECB, 3DES_ECB, 3DES_CBC, SHA1_HMAC, SHA2_256_HMAC, SHA2_384_HMAC, SHA2_512_HMAC"},
	{CRYPTO_QORIQ,      "none"}, //removed cryptodev, for more info please refer to DSM #39064
	{CRYPTO_853X,      "none"},  //removed cryptodev, same reason as QORIQ
	{CRYPTO_628X,      "AES_CBC"},
	{CRYPTO_HW_NONE,    "none"}
};

#define CPU_MODEL_SZ 80
static char cpu_model_synofmt[CPU_MODEL_SZ] = "";

static bool string_ends_with(const char *str, const char *suffix)
{
	size_t str_len, suffix_len;

	if (!str || !suffix) {
		return false;
	}

	str_len = strlen(str);
	suffix_len = strlen(suffix);

	if (str_len < suffix_len) {
		return false;
	}

	return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static void synobios_detect_cpu_model(void)
{
	char *p, *cpu_model, *cpu_model_orig;
	int part = 0, written = 0;

	cpu_model_orig = cpu_model =
		kstrdup(boot_cpu_data.x86_model_id, GFP_KERNEL);
	if (unlikely(!cpu_model))
		return;

	pr_info("%s: model name: %s, nr_cpu_ids: %d\n", __func__, cpu_model,
		nr_cpu_ids);

	while ((p = strsep(&cpu_model, " "))) {
		if (!*p)
			continue;

		if (*p == '@' || *p == '-' || strcmp(p, "with") == 0 ||
		    strcmp(p, "w/") == 0)
			break;
		if (strcmp(p, "CPU") == 0 || strcmp(p, "Genuine") == 0 ||
		    strcmp(p, "Processor") == 0 ||
		    strcmp(p, "processor") == 0 || strcmp(p, "Gen") == 0 ||
		    string_ends_with(p, "th") || strstr(p, "-Core") != NULL)
			continue;

		if (part > 0) {
			if (part < 3)
				written +=
					scnprintf(cpu_model_synofmt + written,
						  CPU_MODEL_SZ - written, ",");
			written += scnprintf(cpu_model_synofmt + written,
					     CPU_MODEL_SZ - written, " ");
		}
		written += scnprintf(cpu_model_synofmt + written,
				     CPU_MODEL_SZ - written, "%s", p);
		++part;
	}
	while (++part < 4)
		written += scnprintf(cpu_model_synofmt + written,
				     CPU_MODEL_SZ - written, ", Processor");
	written += scnprintf(cpu_model_synofmt + written,
			     CPU_MODEL_SZ - written, ", %u", nr_cpu_ids);

	kfree(cpu_model_orig);
}

#if SYNO_HAVE_KERNEL_VERSION(3,10,0)
static int synobios_read_proc_cpu_arch(struct seq_file *m, void *v)
#else /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/
static int synobios_read_proc_cpu_arch(char *page, char **start, off_t off,
		int count, int *eof, void *data)
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/ 
{
	int len = 0;
	SYNO_CPU_MAPPING *p_cpu_mapping = NULL;
	static SYNO_CPU_MAPPING cpu_mapping = { .cpu_number =
							cpu_model_synofmt };
	module_t *syno_module = module_type_get();
	if (!syno_module) {
		printk("get cpu arch information failed\n");
		goto END;
	}

	p_cpu_mapping = gSynoCPUMapping;
	while (CPU_UNKNOWN != p_cpu_mapping->id) {
		if (p_cpu_mapping->id == syno_module->cpu_arch_info)
			break;
		p_cpu_mapping++;
	}

	if (CPU_UNKNOWN == p_cpu_mapping->id && *cpu_model_synofmt)
		p_cpu_mapping = &cpu_mapping;

#if SYNO_HAVE_KERNEL_VERSION(4,4,0)
	seq_printf(m, "%s\n", p_cpu_mapping->cpu_number);
#elif SYNO_HAVE_KERNEL_VERSION(3,10,0)
	len = seq_printf(m, "%s\n", p_cpu_mapping->cpu_number);
#else /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/
	len = sprintf(page, "%s\n", p_cpu_mapping->cpu_number);
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/ 
END:
	return len;
}

#if SYNO_HAVE_KERNEL_VERSION(3,10,0)
static int synobios_read_proc_cpu_arch_open(struct inode *inode, struct file *file)
{
	return single_open(file, synobios_read_proc_cpu_arch, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
static const struct proc_ops synobios_read_proc_cpu_arch_ops = {
	.proc_open      = synobios_read_proc_cpu_arch_open,
	.proc_read      = seq_read,
	.proc_lseek     = seq_lseek,
	.proc_release   = single_release,
};
#else
static const struct file_operations synobios_read_proc_cpu_arch_fops = {
	.owner		= THIS_MODULE,
	.open		= synobios_read_proc_cpu_arch_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/

static int synobios_add_proc_cpu_arch(void)
{
	struct proc_dir_entry *synobios_proc_entry;

	if (boot_cpu_data.x86_model_id[0])
		synobios_detect_cpu_model();

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
	synobios_proc_entry =  proc_create_data("syno_cpu_arch", 0444, NULL,
						&synobios_read_proc_cpu_arch_ops, NULL);
#elif SYNO_HAVE_KERNEL_VERSION(3,10,0)
	synobios_proc_entry =  proc_create_data("syno_cpu_arch", 0444, NULL,
						&synobios_read_proc_cpu_arch_fops, NULL);
#else
	synobios_proc_entry = create_proc_read_entry("syno_cpu_arch", 0444, NULL,
                                                 synobios_read_proc_cpu_arch, NULL);
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/

	if (!synobios_proc_entry) {
		printk("create cpu_arch proc entry fail\n");
		return -EFAULT;
	}

	printk(KERN_INFO"synobios cpu_arch proc entry initialized\n");

	return 0;
}

static int synobios_remove_proc_cpu_arch(void)
{
	remove_proc_entry("syno_cpu_arch", NULL);
	return 0;
}

#if SYNO_HAVE_KERNEL_VERSION(3,10,0)
static int synobios_read_proc_crypto_hw(struct seq_file *m, void *v)
#else /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/
static int synobios_read_proc_crypto_hw(char *page, char **start, off_t off,
		int count, int *eof, void *data)
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/
{
	int len = 0;
	SYNO_CRYPTO_MAPPING *p_crypto_mapping = NULL;
	module_t *syno_module = module_type_get();
	if (!syno_module) {
		printk("get crypto hw information failed\n");
		goto END;
	}

	p_crypto_mapping = gSynoCRYPTOMapping;
	while (CRYPTO_HW_NONE != p_crypto_mapping->hw_name) {
		if (p_crypto_mapping->hw_name == syno_module->crypto_hw_info)
			break;
		p_crypto_mapping++;
	}
#if SYNO_HAVE_KERNEL_VERSION(4,4,0)
    seq_printf(m, "%s\n", p_crypto_mapping->crypto_capabilities);
#elif SYNO_HAVE_KERNEL_VERSION(3,10,0)
    len = seq_printf(m, "%s\n", p_crypto_mapping->crypto_capabilities);
#else /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/
    len = sprintf(page, "%s\n", p_crypto_mapping->crypto_capabilities);
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/
END:
	return len;
}

#if SYNO_HAVE_KERNEL_VERSION(3,10,0)
static int synobios_read_proc_crypto_hw_open(struct inode *inode, struct file *file)
{
	return single_open(file, synobios_read_proc_crypto_hw, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
static const struct proc_ops synobios_read_proc_crypto_hw_ops = {
	.proc_open		= synobios_read_proc_crypto_hw_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};
#else
static const struct file_operations synobios_read_proc_crypto_hw_fops = {
	.owner		= THIS_MODULE,
	.open		= synobios_read_proc_crypto_hw_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/

/* this is a hint for userspace to utilize crypto engine */
static int synobios_add_proc_crypto_hw(void)
{
	struct proc_dir_entry *synobios_proc_entry;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
    synobios_proc_entry =  proc_create_data("syno_crypto_hw", 0400, proc_synobios_root,
                        &synobios_read_proc_crypto_hw_ops, NULL);
#elif SYNO_HAVE_KERNEL_VERSION(3,10,0)
    synobios_proc_entry =  proc_create_data("syno_crypto_hw", 0400, proc_synobios_root,
                        &synobios_read_proc_crypto_hw_fops, NULL);
#else
    synobios_proc_entry = create_proc_read_entry("syno_crypto_hw", 0400, proc_synobios_root, synobios_read_proc_crypto_hw, NULL);
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/

	if (!synobios_proc_entry) {
		printk("create crypto hw proc entry fail\n");
		return -EFAULT;
	}

	printk(KERN_INFO"synobios crypto_hw proc entry initialized\n");

	return 0;
}

static int synobios_remove_proc_crypto_hw(void)
{
	remove_proc_entry("syno_crypto_hw", proc_synobios_root);
	return 0;
}

#if SYNO_HAVE_KERNEL_VERSION(3,10,0)
static int synobios_read_proc_platform_name(struct seq_file *m, void *v)
{
#if SYNO_HAVE_KERNEL_VERSION(4,4,0)
	seq_printf(m, "%s\n", SYNO_PLATFORM);
	return 0;
#else
	return seq_printf(m, "%s\n", SYNO_PLATFORM);
#endif /* SYNO_HAVE_KERNEL_VERSION(4,4,0) */
}

static int synobios_read_proc_platform_open(struct inode *inode, struct file *file)
{
	return single_open(file, synobios_read_proc_platform_name, NULL);
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
static const struct proc_ops synobios_read_proc_platform_ops = {
	.proc_open      = synobios_read_proc_platform_open,
	.proc_read      = seq_read,
	.proc_lseek     = seq_lseek,
	.proc_release   = single_release,
};
#else
static const struct file_operations synobios_read_proc_platform_fops = {
	.owner		= THIS_MODULE,
	.open		= synobios_read_proc_platform_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif
#else /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/
static int synobios_read_proc_platform_name(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	return sprintf(page, "%s\n", SYNO_PLATFORM);
}
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/

static int synobios_add_proc_platform_name(void)
{
	struct proc_dir_entry *synobios_proc_entry;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
	synobios_proc_entry = proc_create_data("syno_platform", 0444, NULL,
						&synobios_read_proc_platform_ops, NULL);
#elif SYNO_HAVE_KERNEL_VERSION(3,10,0)
	synobios_proc_entry = proc_create_data("syno_platform", 0444, NULL,
						&synobios_read_proc_platform_fops, NULL);
#else
	synobios_proc_entry = create_proc_read_entry("syno_platform", 0444, NULL, synobios_read_proc_platform_name, NULL);
#endif /* SYNO_HAVE_KERNEL_VERSION(3,10,0) */
	if (!synobios_proc_entry) {
		printk("create syno_platform proc entry fail\n");
		return -EFAULT;
	}

	printk(KERN_INFO"synobios syno_platform proc entry initialized\n");

	return 0;
}

static int synobios_remove_proc_platform_name(void)
{
	remove_proc_entry("syno_platform", NULL);
	return 0;
}

int synobios_open(struct inode *inode, struct file *filp)
{
	return 0;
}


int synobios_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/*****
 * Helper function to notify user space
 * HW has capability to detect SD card change
 *****/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
static const struct proc_ops card_detect_proc_ops;
#elif SYNO_HAVE_KERNEL_VERSION(3,10,0)
static const struct file_operations card_detect_proc_fops;
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/

#define SD_DETECT_PROC_ENTRY	"syno_card_detect"
int add_card_detect_proc(void)
{
	struct proc_dir_entry *proc_entry;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
	proc_entry = proc_create(SD_DETECT_PROC_ENTRY, 0400, proc_synobios_root, &card_detect_proc_ops);
#elif SYNO_HAVE_KERNEL_VERSION(3,10,0)
	proc_entry = proc_create(SD_DETECT_PROC_ENTRY, 0400, proc_synobios_root, &card_detect_proc_fops);
#else
	proc_entry = create_proc_entry(SD_DETECT_PROC_ENTRY, 0400, proc_synobios_root);
#endif /*SYNO_HAVE_KERNEL_VERSION(3,10,0)*/
	if (!proc_entry) {
		printk("create card detect proc entry fail\n");
		return -EFAULT;
	}

	printk(KERN_INFO"add card detect proc entry\n");

	return 0;
}
int remove_card_detect_proc(void)
{
	remove_proc_entry(SD_DETECT_PROC_ENTRY, proc_synobios_root);
	return 0;
}

struct file_operations synobios_fops = {
	/*llseek:*/
	/*read:*/
	/*write:*/
	/*readdir:*/
	poll:     synobios_poll,
	unlocked_ioctl:	  synobios_ioctl,
	compat_ioctl:   synobios_ioctl,
/*	int (*mmap) (struct file *, struct vm_area_struct *);*/
	open:     synobios_open,
/*	int (*flush) (struct file *);*/
	release:  synobios_release,
/*	int (*fsync) (struct file *, struct dentry *, int datasync);*/
/*	int (*fasync) (int, struct file *, int);*/
/*	int (*lock) (struct file *, int, struct file_lock *);*/
/*	ssize_t (*readv) (struct file *, const struct iovec *, unsigned long, loff_t *);*/
/*	ssize_t (*writev) (struct file *, const struct iovec *, unsigned long, loff_t *);*/
/*	ssize_t (*sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);*/
/*	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);*/
};

typedef struct _tag_SYNO_MODEL_MAPPING {
	PRODUCT_MODEL	model;
	char *szModelName;
} SYNO_MODEL_MAPPING;

static SYNO_MODEL_MAPPING gSynoModelMapping[] = {
	{MODEL_RS3411rpxs,  "RS-3411rpxs"},
	{MODEL_RS3411xs,    "RS-3411xs"},
	{MODEL_RS10613xsp,  "RS-10613xs+"},
	{MODEL_DS3611xs,    "DS-3611xs"},
	{MODEL_DS412p,       "DS-412+"},
	{MODEL_DS713p,       "DS-713+"},
	{MODEL_RS812p,       "RS-812+"},
	{MODEL_RS812rpp,       "RS-812rp+"},
	{MODEL_DS1812p,       "DS-1812+"},
	{MODEL_RS2212p,	    "RS-2212+"},
	{MODEL_RS2414p,	    "RS-2414+"},
	{MODEL_RS2414rpp,	"RS-2414rp+"},
	{MODEL_RS2212rpp,	"RS-2212rp+"},
	{MODEL_DS2413p,	    "DS-2413+"},
	{MODEL_DS2415p,	    "DS-2415+"},
	{MODEL_RS812,       "RS-812"},
	{MODEL_DS1512p,     "DS-1512+"},
	{MODEL_RS3412rpxs,  "RS-3412rpxs"},
	{MODEL_RS3412xs,    "RS-3412xs"},
	{MODEL_DS3612xs,    "DS-3612xs"},
	{MODEL_RS3413xsp,   "RS-3413xs+"},
	{MODEL_RS3614xs,    "RS-3614xs"},
	{MODEL_RS3614rpxs,    "RS-3614rpxs"},
	{MODEL_RS3614xsp,    "RS-3614xs+"},
	{MODEL_DS2414xs,    "DS-2414xs"},
	{MODEL_DS414j,       "DS-414j"},
	{MODEL_DS415j,       "DS-415j"},
	{MODEL_DS213,       "DS-213"},
	{MODEL_DS1513p,     "DS-1513+"},
	{MODEL_DS1813p,       "DS-1813+"},
	{MODEL_DS213j,       "DS-213j"},
	{MODEL_DS114,       "DS-114"},
	{MODEL_RS214,       "RS-214"},
	{MODEL_DS214,       "DS-214"},
	{MODEL_DS214p,       "DS-214+"},
	{MODEL_DS214se,       "DS-214se"},
	{MODEL_DS114p,       "DS-114+"},
	{MODEL_DS414,       "DS-414"},
	{MODEL_RS814,       "RS-814"},
	{MODEL_RC18015xsp,  "RC-18015xs+"},
	{MODEL_RS814p,      "RS-814+"},
	{MODEL_RS814rpp,    "RS-814rp+"},
	{MODEL_DS214play,       "DS-214play"},
	{MODEL_DS414slim,   "DS-414slim"},
	{MODEL_DS415play,       "DS-415play"},
	{MODEL_DS2015xs,        "DS-2015xs"},
	{MODEL_DS115j,      "DS-115j"},
	{MODEL_RS3415xsp,    "RS-3415xs+"},
	{MODEL_DS415p,       "DS-415+"},
	{MODEL_DS3615xs,    "DS-3615xs"},
	{MODEL_DS1815p,		"DS-1815+"},
	{MODEL_DS1515p,		"DS-1515+"},
	{MODEL_DS215j,       "DS-215j"},
	{MODEL_DS115,       "DS-115"},
	{MODEL_RS815p,		"RS-815+"},
	{MODEL_RS815rpp,	"RS-815rp+"},
	{MODEL_DS715,		"DS-715"},
	{MODEL_RS815,       "RS-815"},
	{MODEL_RS18016xsp,  "RS-18016xs+"},
	{MODEL_RS18016Dxsp,  "RS-18016Dxs+"},
	{MODEL_DS1515,      "DS-1515"},
	{MODEL_DS215p,      "DS-215+"},
	{MODEL_VirtualDSM,  		"VirtualDSM"},
	{MODEL_DS416,       "DS-416"},
	{MODEL_RS2416p,		"RS-2416+"},
	{MODEL_RS2416rpp,	"RS-2416rp+"},
	{MODEL_DS216play,		"DS-216play"},
	{MODEL_DS216se,     "DS-216se"},
	{MODEL_RSD18016xsp,  "RSD-18016xs+"},
	{MODEL_DS916p,	    "DS-916+"},
	{MODEL_DS1616p,		"DS-1616+"},
	{MODEL_DS716p,          "DS-716+"},
	{MODEL_RS3618xs,  "RS-3618xs"},
	{MODEL_RS3617rpxs,  "RS-3617rpxs"},
	{MODEL_RS3617xsp,  "RS-3617xs+"},
	{MODEL_DS416j,	    "DS-416j"},
	{MODEL_DS216,	    "DS-216"},
	{MODEL_DS416slim,	    "DS-416slim"},
	{MODEL_DS216p,		"DS-216+"},
	{MODEL_DS216j,		"DS-216j"},
	{MODEL_RS816,		"RS-816"},
	{MODEL_DS116,		"DS-116"},
	{MODEL_DS416play,	"DS-416play"},
	{MODEL_DS216pII,          "DS-216+II"},
	{MODEL_DS716pII,          "DS-716+II"},
	{MODEL_RS217,		"RS-217"},
	{MODEL_FS3017,		"FS-3017"},
	{MODEL_RS3617xs,    "RS-3617xs"},
	{MODEL_RS2418p,		"RS-2418+"},
	{MODEL_RS2418rpp,	"RS-2418rp+"},
	{MODEL_DS918p,    "DS-918+"},
	{MODEL_RS4017xsp,  "RS-4017xs+"},
	{MODEL_DS3617xs,  "DS-3617xs"},
	{MODEL_RS18017xsp,  "RS-18017xs+"},
	{MODEL_DS218p,    "DS-218+"},
	{MODEL_DS1618p,	"DS-1618+"},
	{MODEL_C2DSM,	"C2DSM"},
	{MODEL_DS1517p,     "DS-1517+"},
	{MODEL_DS1817p,     "DS-1817+"},
	{MODEL_DS718p,      "DS-718+"},
	{MODEL_FS2017,      "FS-2017"},
	{MODEL_DS418,       "DS-418"},
	{MODEL_DS418j,	    "DS-418j"},
	{MODEL_DS218play,   "DS-218play"},
	{MODEL_DS118,       "DS-118"},
	{MODEL_DS218,       "DS-218"},
	{MODEL_EDS19,		"EDS-19"},
	{MODEL_RS819,		"RS-819"},
	{MODEL_DS220j,		"DS-220j"},
	{MODEL_DS420j,      "DS-420j"},
	{MODEL_DS1817,      "DS-1817"},
	{MODEL_DS1517,      "DS-1517"},
	{MODEL_DS3017xs,    "DS-3017xs"},
	{MODEL_DS3018xs,    "DS-3018xs"},
	{MODEL_FS1018,      "FS-1018"},
	{MODEL_DS219j,      "DS-219j"},
	{MODEL_FS6400,      "FS-6400"},
	{MODEL_RS818p,      "RS-818+"},
	{MODEL_RS818rpp,    "RS-818rp+"},
	{MODEL_DS418play,   "DS-418play"},
	{MODEL_RS2818rpp,   "RS-2818rp+"},
	{MODEL_DS219se,		"DS-219se"},
	{MODEL_NVR1218,      "NVR1218"},
	{MODEL_DS218j,		"DS-218j"},
	{MODEL_DS3619xs,	"DS-3619xs"},
	{MODEL_DS119j,      "DS-119j"},
	{MODEL_TAIPEI, 	    "TAIPEI"},
	{MODEL_RS1619xsp,   "RS-1619xs+"},
	{MODEL_RS1219p,     "RS-1219+"},
	{MODEL_DS2419p,     "DS-2419+"},
	{MODEL_DS419p,      "DS-419+"},
	{MODEL_DS1019p,     "DS-1019+"},
	{MODEL_DS719p,      "DS-719+"},
	{MODEL_DS1819p,     "DS-1819+"},
	{MODEL_DS620slim,   "DS-620slim"},
	{MODEL_RS419p,      "RS-419+"},
	{MODEL_DS419slim,   "DS-419slim"},
	{MODEL_RS1219,      "RS-1219"},
	{MODEL_DVA3219,     "DVA-3219"},
	{MODEL_SA3400,      "SA-3400"},
	{MODEL_DS420p,		"DS-420+"},
	{MODEL_DS720p,		"DS-720+"},
	{MODEL_DS220p,      "DS-220+"},
	{MODEL_RS820p,      "RS-820+"},
	{MODEL_RS820rpp,    "RS-820rp+"},
	{MODEL_FS3400,      "FS-3400"},
	{MODEL_SA3600,      "SA-3600"},
	{MODEL_FS3600,      "FS-3600"},
	{MODEL_HD3400,      "HD-3400"},
	{MODEL_DS220play,   "DS-220play"},
	{MODEL_DS220,       "DS-220"},
	{MODEL_DS120j,      "DS-120j"},
	{MODEL_DS1520p,     "DS-1520+"},
	{MODEL_RS1220p,     "RS-1220+"},
	{MODEL_RS1220rpp,   "RS-1220rp+"},
	{MODEL_DS920p,      "DS-920+"},
	{MODEL_SA6500,      "SA-6500"},
	{MODEL_DS1621p,     "DS-1621+"},
	{MODEL_HD6500,      "HD-6500"},
	{MODEL_SA3200d,     "SA-3200d"},
	{MODEL_SA3400d,     "SA-3400d"},
	{MODEL_DVA3221,     "DVA-3221"},
	{MODEL_AliDSM,      "AliDSM"},
	{MODEL_RS1221p,     "RS-1221+"},
	{MODEL_RS1221rpp,   "RS-1221rp+"},
	{MODEL_DS1821p,     "DS-1821+"},
	{MODEL_DS2422p,     "DS-2422+"},
	{MODEL_RS2421p,     "RS-2421+"},
	{MODEL_RS2421rpp,   "RS-2421rp+"},
	{MODEL_RS2821rpp,   "RS-2821rp+"},
	{MODEL_FS6600N,     "FS-6600N"},
	{MODEL_RS3621xsp,   "RS-3621xs+"},
	{MODEL_RS3621rpxs,  "RS-3621rpxs"},
	{MODEL_RS4021xsp,  "RS-4021xs+"},
	{MODEL_FS6500,      "FS-6500"},
	{MODEL_DS3617xsII,  "DS-3617xsII"},
	{MODEL_DS3622xsp,   "DS-3622xs+"},
	{MODEL_DS2419pII,   "DS-2419+II"},
	{MODEL_FS2500,      "FS2500"},
	{MODEL_FS2500T,     "FS2500T"},
	{MODEL_DVA1622,     "DVA-1622"},
	{MODEL_RS4023xsp,   "RS-4023xs+"},
	{MODEL_RS4022xsp,   "RS-4022xs+"},
	{MODEL_FS3410,      "FS-3410"},
	{MODEL_DS223j,      "DS-223j"},
	{MODEL_DS423,       "DS-423"},
	{MODEL_DS223,       "DS-223"},
	{MODEL_DS723p,      "DS-723+"},
	{MODEL_DS923p,      "DS-923+"},
	{MODEL_RS422p,      "RS-422+"},
	{MODEL_DS1522p,     "DS-1522+"},
	{MODEL_RS822p,      "RS-822+"},
	{MODEL_RS822rpp,    "RS-822rp+"},
	{MODEL_RS2423p,     "RS-2423+"},
	{MODEL_RS2423rpp,   "RS-2423rp+"},
	{MODEL_DS1823xsp,   "DS-1823xs+"},
	{MODEL_RS1623xsp,   "RS-1623xs+"},
	{MODEL_FS6410,      "FS-6410"},
	{MODEL_SA6400,      "SA-6400"},
	{MODEL_SA6200,      "SA-6200"},
	{MODEL_SC6200,      "SC-6200"},
	{MODEL_DS423p,      "DS-423+"},
	{MODEL_SA3410,      "SA-3410"},
	{MODEL_SA3610,      "SA-3610"},
	{MODEL_DS124,       "DS-124"},
	{MODEL_DS224p,      "DS-224+"},
	{MODEL_RS4024xsp,   "RS-4024xs+"},
	{MODEL_VS750hd,	    "VS-750hd"},
	{MODEL_DS1623p,     "DS-1623+"},
	{MODEL_DS1823p,     "DS-1823+"},
	{MODEL_SC2500,      "SC-2500"},
	{MODEL_INVALID,	    "Unknown"},
};

static void synobios_print_model(void)
{
	int brand, model;
	SYNO_MODEL_MAPPING *pModelMapping;

	if (synobios_ops->get_brand) {
		brand = synobios_ops->get_brand();
		switch (brand) {
		case BRAND_SYNOLOGY:
			printk("Brand: Synology\n");
			break;
		case BRAND_LOGITEC:
			printk("Brand: Logitec\n");
			break;
		default:
			printk("Brand: Unknown brand\n");
		}
	} else {
		printk("Get brand function not defined.\n");
	}
	if (synobios_ops->get_model) {
		model = synobios_ops->get_model();
		pModelMapping = gSynoModelMapping;
		while (pModelMapping->model != MODEL_INVALID) {
			if (pModelMapping->model == model) {
				break;
			}
			pModelMapping++;
		}
		printk("Model: %s\n", pModelMapping->szModelName);
	} else {
		printk("Get model function not defined.\n");
	}

	return;
}

static void synobios_schedule_poweron_init(void)
{
	gPwSched.num = 0;
	gPwSched.enabled = SYNO_AUTO_POWERON_DISABLE;
	memset(gPwSched.RtcAlarmPkt, 0, sizeof(SYNORTCALARMPKT)*MAX_POWER_SCHEDULE_TASKS);

	return;
}

int
GetHWMONSupport(SYNO_HWMON_SUPPORT *hwmon_support)
{
	int iRet = -1;
	module_t *syno_module = module_type_get();

	if (NULL == hwmon_support) {
		iRet = -EINVAL;
		goto End;
	}

	hwmon_support->support = 0;

	switch (hwmon_support->id) {
		case HWMON_CPU_TEMP:
			if (CPUTMP_YES == syno_module->cputmp_type) {
				hwmon_support->support = 1;
			}
			break;
		case HWMON_SYS_THERMAL:
			if (synobios_ops->hwmon_get_sys_thermal) {
				hwmon_support->support = 1;
			}
			break;
		case HWMON_SYS_VOLTAGE:
			if (synobios_ops->hwmon_get_sys_voltage) {
				hwmon_support->support = 1;
			}
			break;
		case HWMON_FAN_SPEED_RPM:
			if (synobios_ops->hwmon_get_fan_speed_rpm) {
				hwmon_support->support = 1;
			}
			break;
		case HWMON_HDD_BACKPLANE:
			if (synobios_ops->hwmon_get_backplane_status) {
				hwmon_support->support = 1;
			}
			break;
		case HWMON_PSU_STATUS:
			if (synobios_ops->hwmon_get_psu_status) {
				if (POWER_DUAL == syno_module->dual_power_type) {
					hwmon_support->support = 2;
				}
				else {
					hwmon_support->support = 1;
				}
			}
			break;
		case HWMON_SYS_CURRENT:
			if (synobios_ops->hwmon_get_sys_current) {
				hwmon_support->support = 1;
			}
			break;
		default:
			iRet = -EINVAL;
			goto End;
	}

	iRet = 0;
End:
	return iRet;
}

int synobios_event_handler(unsigned long long synobios_event_type, ...)
{
	int args_num = 0, i = 0, ret = -EPERM;
	unsigned long long parms[SYNOBIOS_EVENTDATA_NUM_MAX] = {0};
	va_list args;
	struct tty_struct tty;
	SYNOBIOS_EVENT_PARM event_parms;
	memset(&event_parms, 0, sizeof(SYNOBIOS_EVENT_PARM));

	va_start(args, synobios_event_type);

	args_num = va_arg(args, unsigned long long);
	if (SYNOBIOS_EVENTDATA_NUM_MAX < args_num) {
		pr_warn("args number %d from kernel exceeds synobios expected max %d.\n", args_num, SYNOBIOS_EVENTDATA_NUM_MAX);
		ret = -EINVAL;
		va_end(args);
		goto END;
	}
	for (i = 0; i < args_num && i < SYNOBIOS_EVENTDATA_NUM_MAX; i++) {
		parms[i] = va_arg(args, unsigned long long);
		event_parms.data[i] = parms[i];
	}
	va_end(args);

	switch (synobios_event_type) {
	case SYNO_EVENT_DSIK_POWER_SHORT_BREAK:
#ifdef CONFIG_SYNO_PORT_MAPPING_V2
		synobios_disk_power_short_break_report(__SYNOEVENTCALL_PARM4(parms));
#else /* CONFIG_SYNO_PORT_MAPPING_V2 */
		synobios_disk_power_short_break_report(__SYNOEVENTCALL_PARM2(parms));
#endif /* CONFIG_SYNO_PORT_MAPPING_V2 */
		break;
	case SYNO_EVENT_DISK_PWR_RESET:
		synobios_record_disk_pwr_reset_event(__SYNOEVENTCALL_PARM2(parms));
		break;
	case SYNO_EVENT_DISK_PORT_DISABLED:
		synobios_record_disk_port_disabled_event(__SYNOEVENTCALL_PARM3(parms));
		break;
	case SYNO_EVENT_WAKE_FROM_DEEP_SLEEP:
		synobios_wake_from_deep_sleep(__SYNOEVENTCALL_PARM2(parms));
		break;
	case SYNO_EVENT_DISK_PORT_LOST:
		synobios_disk_port_lost_report(__SYNOEVENTCALL_PARM2(parms));
		break;
	case SYNO_EVENT_DISK_RESET_FAIL_REPORT:
		synobios_disk_reset_fail_report(event_parms);
		break;
	case SYNO_EVENT_DISK_TIMEOUT_REPORT:
		synobios_disk_timeout_report(event_parms);
		break;
	case SYNO_EVENT_SATA_ERROR_REPORT:
		synobios_sata_error_report(event_parms);
		break;
	case SYNO_EVENT_DISK_RETRY_REPORT:
		synobios_disk_retry_report(__SYNOEVENTCALL_PARM2(parms));
		break;
	case SYNO_EVENT_USB_PROHIBIT:
		syno_usb_prohibit_event();
		break;
	case SYNO_EVENT_CONSOLE_PROHIBIT:
		syno_console_prohibit_event();
		break;
	case SYNO_EVENT_MICROP_GET:
		/* FIXME: Remove tty struct and refine syno_microp_get_event parms
		 * when kernel previous 4.4 does not use this synobios.
		 * The tty struct is for compatibility for 4.4 version synobios.
		 * In 4.4 synobios the tty name check is in synobios,
		 * but we move tty name check to kernel in 5.10 version.
		 */
		try_wakeup_waiting_microp();
		memset(&tty, 0, sizeof(struct tty_struct));
		snprintf(tty.name, 64, "%s", TTY_NAME);
		syno_microp_get_event(&tty);
		break;
	case SYNO_EVENT_SCSI_ERROR:
		synobios_record_scsi_error_event(event_parms);
		break;
	case SYNO_EVENT_EBOX_RESET:
		synobios_record_ebox_reset_event(event_parms);
		break;
	default:
		pr_warn("Unknown synobios event type.\n");
		ret = -EINVAL;
		goto END;
	}

	ret = 0;
END:
	return ret;
}

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_ERROR_REPORT)
extern struct list_head gSynoBiosEventHead;
extern spinlock_t syno_sata_error_lock;
void process_synobios_event_list(void)
{
	SYNOBIOS_EVENT_ACTION_LIST *synobios_action = NULL;
	SYNOBIOS_EVENT_ACTION_LIST *nex_synobios_action = NULL;
	unsigned long syno_sata_error_lock_flags;

	spin_lock_irqsave(&syno_sata_error_lock, syno_sata_error_lock_flags);
	list_for_each_entry_safe(synobios_action, nex_synobios_action, &gSynoBiosEventHead, list) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
		synobios_event_handler(synobios_action->synobios_event_type, 5, synobios_action->parms.data[0],
				synobios_action->parms.data[1], synobios_action->parms.data[2], synobios_action->parms.data[3], synobios_action->parms.data[4]);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
		if (NULL != synobios_action->funcSynobiosEvent && NULL != *(synobios_action->funcSynobiosEvent)) {
			(*(synobios_action->funcSynobiosEvent))(synobios_action->parms);
		}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0) */
		list_del(&synobios_action->list);
		kfree(synobios_action);
	}
	spin_unlock_irqrestore(&syno_sata_error_lock, syno_sata_error_lock_flags);
}
#endif /* MY_DEF_HERE || CONFIG_SYNO_SATA_ERROR_REPORT */

#ifdef CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK
extern int (*funcSYNOShutdownHook)(void);
SYNO_SHUTDOWN_HOOK_LIST synoShutdownHooks;

static int syno_shutdown_hooks(void)
{
	SYNO_SHUTDOWN_HOOK *ops;

	mutex_lock(&synoShutdownHooks.hookLock);
	list_for_each_entry(ops, &synoShutdownHooks.hookList, node) {
		if (system_state == SYSTEM_POWER_OFF && ops->shutdown) {
			ops->shutdown();
		}else if (system_state == SYSTEM_RESTART && ops->restart) {
			ops->restart();
		}
	}
	mutex_unlock(&synoShutdownHooks.hookLock);

	return 0;
}

static void syno_init_shutdown_hooks(void) 
{
	mutex_init(&synoShutdownHooks.hookLock);
	INIT_LIST_HEAD(&synoShutdownHooks.hookList);
	
	funcSYNOShutdownHook = syno_shutdown_hooks;
}

static void syno_uninit_shutdown_hooks(void) 
{
	mutex_destroy(&synoShutdownHooks.hookLock);
	
	funcSYNOShutdownHook = NULL;
}

void syno_append_shutdown_hook(SYNO_SHUTDOWN_HOOK *ops) 
{	
	if (NULL == ops) {
		return;
	}

	mutex_lock(&synoShutdownHooks.hookLock);
	list_add_tail(&ops->node, &synoShutdownHooks.hookList);
	mutex_unlock(&synoShutdownHooks.hookLock);
}
EXPORT_SYMBOL(syno_append_shutdown_hook);
#endif /* CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK */

int synobios_init(void)
{
	int iRet = -1;

	scSynoBios.countEvents = 0;
	scSynoBios.idxPtr = 0;
	spin_lock_init(&scSynoBios.lock);

	/*initialize wait queues*/
	init_waitqueue_head(&(scSynoBios.wq_poll));

	/* create procfs root entry */
	proc_synobios_root = proc_mkdir(SZ_PROC_SYNOBIOS_ROOT, NULL);
	if (!proc_synobios_root) {
		printk("synobios: create procfs root entry fail\n");
	}

	syno_ttyS_open();

#ifdef CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK
	/* Initialize Shutdown Hook functions:
	 *   
	 * How to add customized shutdown function
	 *   syno_append_shutdown_hook(SYNO_SHUTDOWN_HOOK *ops)
	 *
	 * Note: the execution order of SHUTDOWN_HOOK will be same as the 
	 *	 order of appending.
	 */
	syno_init_shutdown_hooks();
#endif /* CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK */

	synobios_model_init(&synobios_fops, &synobios_ops);

	if (synobios_ops->module_type_init) {
		synobios_ops->module_type_init(synobios_ops);
	} else {
		module_type_set(NULL);
	}

	synobios_rtc_init();

	if (NULL == (pgSysStatus = kzalloc(sizeof(SYNO_SYS_STATUS), GFP_KERNEL))) {
		printk("malloc SYNO_SYS_STATUS fail\n");
		iRet = 0;
		goto END;
	}

	pgSysStatus->fan_fail |= SIGNATURE_FAN_FAIL;
	pgSysStatus->volume_degrade |= SIGNATURE_VOLUME_DEGRADE;
	pgSysStatus->volume_crashed |= SIGNATURE_VOLUME_CRASHED;
	pgSysStatus->power_fail |= SIGNATURE_POWER_FAIL;
	pgSysStatus->ebox_fan_fail |= SIGNATURE_EBOX_FAN_FAIL;
	pgSysStatus->cpu_fan_fail |= SIGNATURE_CPU_FAN_FAIL;

/*
 * Defined in drivers/md/libmd-report.c:
 *  funcSYNOSendRaidEvent
 */
#ifdef CONFIG_MD
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SECTOR_STATUS_REPORT)
	funcSYNOSendRaidEvent = synobios_record_raid_event;
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SECTOR_STATUS_REPORT) */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_AUTO_REMAP_REPORT)
	funcSYNOSendAutoRemapRaidEvent = synobios_autoremap_raid_event;
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_AUTO_REMAP_REPORT) */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SYNC_STATUS_REPORT)
	funcSYNOSendRaidSyncEvent = synobios_raid_sync_event;
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SYNC_STATUS_REPORT) */
#endif /* CONFIG_MD */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_EXT4_ERROR_REPORT)
	funcSYNOSendErrorFsEvent = synobios_error_fs_event;
#endif /* MY_DEF_HERE || CONFIG_SYNO_EXT4_ERROR_REPORT */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_BTRFS_DATA_CORRECTION)
	funcSYNOSendErrorFsBtrfsEvent = synobios_error_fs_btrfs_event;
	funcSYNOMetaCorruptedEvent = synobios_error_btrfs_meta_corrupted_event;
#endif /* MY_DEF_HERE || CONFIG_SYNO_BTRFS_DATA_CORRECTION */
/*
 * Defined in drivers/ata/libata-core.c:
 *  funcSYNOGetHwCapability
 *  funcSYNOSendEboxRefreshEvent
 */
#if (defined(CONFIG_ATA) || defined(CONFIG_SCSI_SATA))
#ifdef MY_DEF_HERE
	funcSYNOSendDiskResetPwrEvent = synobios_record_disk_pwr_reset_event;
#endif /* MY_DEF_HERE */
#ifdef MY_DEF_HERE
	funcSYNOSendDiskPortDisEvent = synobios_record_disk_port_disabled_event;
#ifdef MY_DEF_HERE
	funcSYNOSendDiskPortLostEvent = synobios_disk_port_lost_report;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
#if (defined(MY_DEF_HERE) || defined(SYNO_HAS_SDCARDREADER))
	funcSYNOGetHwCapability = GetHwCapability;
#ifdef MY_DEF_HERE
	funcSynoEunitPowerctlType = GetEUnitType;
#endif
#endif
#ifdef MY_DEF_HERE
	funcSYNOSendEboxRefreshEvent = synobios_record_ebox_refresh_event;
#endif
#ifdef MY_DEF_HERE
	funcSYNOSataErrorReport = synobios_sata_error_report;
	funcSYNODiskRetryReport = synobios_disk_retry_report;
	funcSYNODiskTimeoutReport = synobios_disk_timeout_report;
	funcSYNODiskResetFailReport = synobios_disk_reset_fail_report;
#endif /* MY_DEF_HERE */
#endif /* CONFIG_ATA */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_ECC_NOTIFICATION)
	funcSYNOECCNotification = synobios_event_ecc_notification;
#endif /* MY_DEF_HERE || CONFIG_SYNO_ECC_NOTIFICATION */
#ifdef MY_DEF_HERE
	funcSYNODeepSleepEvent = synobios_wake_from_deep_sleep;
#endif /* MY_DEF_HERE */
#if defined(MY_DEF_HERE)
	funcSYNODiskPowerShortBreakReport = synobios_disk_power_short_break_report;
#endif /* MY_DEF_HERE */

#ifdef CONFIG_SYNO_MPC824X
	if( synobios_ops->set_rtc_time ) {
		funcSYNORtcSetTime = synobios_ops->set_rtc_time;
	}
#endif /*CONFIG_SYNO_MPC824X*/
#ifdef CONFIG_SYNO_BUZZER_MUTE_IRQ
	funcSYNOBuzzerMuteIRQ = syno_buzzer_cleared_event;
#endif/* CONFIG_SYNO_BUZZER_MUTE_IRQ */
#ifdef CONFIG_SYNO_DISPLAY_CPUINFO
	syno_display_cpu_info();
#endif/* CONFIG_SYNO_DISPLAY_CPUINFO */
#ifdef CONFIG_SYNO_IE_SOFT_POWER_OFF
	funcSYNOSendPowerButtonEvent = syno_power_button_event;
#endif /* CONFIG_SYNO_IE_SOFT_POWER_OFF */

#if defined(CONFIG_SYNO_USB_FORBID) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT)
	funcSYNOUsbProhibitEvent = syno_usb_prohibit_event;
#endif /* CONFIG_SYNO_USB_FORBID && ! CONFIG_SYNO_SYNOBIOS_EVENT */

#if defined(CONFIG_SYNO_SERIAL_CONSOLE_FORBID) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT)
	funcSYNOConsoleProhibitEvent = syno_console_prohibit_event;
#endif /* CONFIG_SYNO_SERIAL_CONSOLE_FORBID && ! CONFIG_SYNO_SYNOBIOS_EVENT */

#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_OOM_NOTIFICATION)
	funcSYNOSendErrorOOMEvent = synobios_error_oom_event;
#endif /* (MY_DEF_HERE) || (CONFIG_SYNO_OOM_NOTIFICATION) */

#if defined(CONFIG_SYNO_TTY_EXPORT) || defined(MY_DEF_HERE)
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(SYNO_TTY_RECEIVE)
	funcSYNOMicropGetEvent = syno_microp_get_event;
#endif /* SYNO_TTY_RECEIVE */
#endif /* MY_DEF_HERE */

/*
 * Defined in drivers/scsi/libsyno_report.c
 */
#if defined(MY_DEF_HERE) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT)
	funcSYNOSendScsiErrorEvent = synobios_record_scsi_error_event;
#endif /* MY_DEF_HERE && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT) */

#if defined(CONFIG_SYNO_SATA_PM_DEVICE_I2C) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT)
	funcSYNOSendEunitResetEvent = synobios_record_ebox_reset_event;
#endif /* defined(CONFIG_SYNO_SATA_PM_DEVICE_I2C) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT) ))*/

#ifdef CONFIG_SYNO_SYNOBIOS_EVENT
	func_synobios_event_handler = synobios_event_handler;
#endif /* CONFIG_SYNO_SYNOBIOS_EVENT */
	printk(KERN_INFO "synobios: load, major number %d\n", SYNOBIOS_MAJOR);
	iRet = register_chrdev(SYNOBIOS_MAJOR, "synobios", &synobios_fops);
	if (iRet < 0) {
		printk(KERN_INFO "synobios: can't set major number\n");
		goto END;
	}
	synobios_print_model();
	synobios_schedule_poweron_init();
#ifdef MY_DEF_HERE
	SetGroupWakeConfig();
#endif

	if (synobios_ops->set_microp_id) {
		synobios_ops->set_microp_id();
	}

	synobios_add_proc_cpu_arch();
	synobios_add_proc_crypto_hw();
	synobios_add_proc_platform_name();
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_ERROR_REPORT)
	if (1 == system_mode) {
		process_synobios_event_list();
	}
#endif /* MY_DEF_HERE || CONFIG_SYNO_SATA_ERROR_REPORT */
	iRet = 0;

END:
	return iRet;
}

void synobios_cleanup(void)
{
#ifdef CONFIG_MD
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SECTOR_STATUS_REPORT)
	funcSYNOSendRaidEvent = NULL;
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SECTOR_STATUS_REPORT)*/
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_AUTO_REMAP_REPORT)
	funcSYNOSendAutoRemapRaidEvent = NULL;
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_AUTO_REMAP_REPORT) */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SYNC_STATUS_REPORT)
	funcSYNOSendRaidSyncEvent = NULL;
#endif /* defined(MY_DEF_HERE) || defined(CONFIG_SYNO_MD_SYNC_STATUS_REPORT) */
#endif /* CONFIG_MD */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_EXT4_ERROR_REPORT)
	funcSYNOSendErrorFsEvent = NULL;
#endif /* MY_DEF_HERE || CONFIG_SYNO_EXT4_ERROR_REPORT */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_BTRFS_DATA_CORRECTION)
	funcSYNOSendErrorFsBtrfsEvent = NULL;
#endif /* MY_DEF_HERE || CONFIG_SYNO_BTRFS_DATA_CORRECTION */
#if (defined(CONFIG_ATA) || defined(CONFIG_SCSI_SATA))
#ifdef MY_DEF_HERE
	funcSYNOSendDiskResetPwrEvent = NULL;
#endif /* MY_DEF_HERE */
#ifdef MY_DEF_HERE
	funcSYNOSendDiskPortDisEvent = NULL;
#ifdef MY_DEF_HERE
	funcSYNOSendDiskPortLostEvent = NULL;
#endif /* MY_DEF_HERE */
#endif /* MY_DEF_HERE */
#if (defined(MY_DEF_HERE) || defined(SYNO_HAS_SDCARDREADER))
	funcSYNOGetHwCapability = NULL;
#ifdef MY_DEF_HERE
	funcSynoEunitPowerctlType = NULL;
#endif
#endif
#ifdef MY_DEF_HERE
	funcSYNOSendEboxRefreshEvent = NULL;
#endif
#ifdef MY_DEF_HERE
	funcSYNOSataErrorReport = NULL;
	funcSYNODiskRetryReport = NULL;
	funcSYNODiskTimeoutReport = NULL;
	funcSYNODiskResetFailReport = NULL;
#endif /* MY_DEF_HERE */
#ifdef MY_DEF_HERE
	funcSYNODeepSleepEvent = NULL;
#endif /* MY_DEF_HERE */
#ifdef MY_DEF_HERE
	funcSYNODiskPowerShortBreakReport = NULL;
#endif /* MY_DEF_HERE */
#endif /* CONFIG_ATA */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_ECC_NOTIFICATION)
	funcSYNOECCNotification = NULL;
#endif /* MY_DEF_HERE || CONFIG_SYNO_ECC_NOTIFICATION */
#ifdef CONFIG_SYNO_MPC824X
	 funcSYNORtcSetTime = NULL;
#endif /* CONFIG_SYNO_MPC824X */
#ifdef CONFIG_SYNO_BUZZER_MUTE_IRQ
	funcSYNOBuzzerMuteIRQ = NULL;
#endif /* CONFIG_SYNO_BUZZER_MUTE_IRQ */
#ifdef CONFIG_SYNO_IE_SOFT_POWER_OFF
	funcSYNOSendPowerButtonEvent = NULL;
#endif /* CONFIG_SYNO_IE_SOFT_POWER_OFF */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_SATA_DISK_LED_CONTROL)
	funcSYNOSATADiskLedCtrl = NULL;
#endif /* MY_DEF_HERE || CONFIG_SYNO_SATA_DISK_LED_CONTROL */
#if defined(MY_DEF_HERE) || defined(CONFIG_SYNO_OOM_NOTIFICATION)
	funcSYNOSendErrorOOMEvent = NULL;
#endif /* (MY_DEF_HERE) || (CONFIG_SYNO_OOM_NOTIFICATION) */

#if defined(CONFIG_SYNO_USB_FORBID) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT)
	funcSYNOUsbProhibitEvent = NULL;
#endif /* CONFIG_SYNO_USB_FORBID && ! CONFIG_SYNO_SYNOBIOS_EVENT */

#if defined(CONFIG_SYNO_SERIAL_CONSOLE_FORBID) && ! defined(CONFIG_SYNO_SYNOBIOS_EVENT)
	funcSYNOConsoleProhibitEvent = NULL;
#endif /* CONFIG_SYNO_SERIAL_CONSOLE_FORBID && ! CONFIG_SYNO_SYNOBIOS_EVENT */

#ifdef MY_DEF_HERE
	funcSYNOSendScsiErrorEvent = NULL;
#endif /* MY_DEF_HERE */

#ifdef CONFIG_SYNO_SYNOBIOS_EVENT
	func_synobios_event_handler = NULL;
#endif /* CONFIG_SYNO_SYNOBIOS_EVENT */

	if (pgSysStatus) {
		kfree(pgSysStatus);
	}
	synobios_remove_proc_cpu_arch();
	synobios_remove_proc_crypto_hw();
	synobios_remove_proc_platform_name();

	syno_ttyS_close();
	synobios_model_cleanup(&synobios_fops, &synobios_ops);

	/* remove procfs root entry */
	if (proc_synobios_root) {
		remove_proc_entry(SZ_PROC_SYNOBIOS_ROOT, NULL);
	}

#if defined(CONFIG_SYNO_TTY_EXPORT) || defined(MY_DEF_HERE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
#if defined(CONFIG_SYNO_TTY_RECEIVE) || defined(SYNO_TTY_RECEIVE) || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS)
	if (syno_get_current) {
		syno_get_current = NULL;
	}

#ifdef CONFIG_SYNO_SYNOBIOS_EVENT
#else /* CONFIG_SYNO_SYNOBIOS_EVENT */
	funcSYNOMicropGetEvent = NULL;
#endif /* CONFIG_SYNO_SYNOBIOS_EVENT */
#endif /* SYNO_TTY_RECEIVE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */
#endif /* MY_DEF_HERE || defined(CONFIG_SYNO_TTY_MICROP_FUNCTIONS) */

#ifdef CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK
	syno_uninit_shutdown_hooks();
#endif /* CONFIG_SYNO_SYSTEM_SHUTDOWN_HOOK */

	printk("synobios: unload\n");
	unregister_chrdev(SYNOBIOS_MAJOR, "synobios");
}

MODULE_AUTHOR("Alex Wang");
MODULE_DESCRIPTION("synobios\n") ;
MODULE_LICENSE("GPL");

module_init(synobios_init);
module_exit(synobios_cleanup);

