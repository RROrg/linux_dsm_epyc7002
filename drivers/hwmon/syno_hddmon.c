#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/synosata.h>
#include <linux/syno_gpio.h>

MODULE_LICENSE("Proprietary");

#define SYNO_HDDMON_POLL_SEC 1
#define SYNO_HDDMON_EN_WAIT_SEC 7
#define SYNO_HDDMON_STR "Syno_HDDMon"
#define SYNO_HDDMON_UPLG_STR "Syno_HDDMon_UPLGM"

extern int SYNO_CTRL_HDD_POWERON(int index, int value);
extern int SYNO_CHECK_HDD_DETECT(int index);
extern DISK_PWRCTRL_TYPE SYNO_GET_DISK_PWR_TYPE(int index);

extern struct ata_port *syno_ata_port_get_by_port(const unsigned short diskPort);

typedef struct __SynoHddMonData {
	int iProcessingIdx;
	int blHddHotPlugSupport;
	int iMaxHddNum;
	int* blHddEnStat;
	int iHddEnWait;
} SynoHddMonData_t;

static struct task_struct *pHddPrzPolling;
static SynoHddMonData_t synoHddMonData;

static int syno_hddmon_data_init(SynoHddMonData_t *pData)
{
	int iRet = -1;
	int idx = 0;

	if (NULL == pData) {
		goto END;
	}

	memset(pData, 0, sizeof(SynoHddMonData_t));

	/* HDD monitor only service internal slots */
	if (0 == gSynoInternalHddNumber) {
		goto END;
	}

	/* HDD monitor can't work without present pins */
	for (idx = 1; idx <= gSynoInternalHddNumber; idx++) {
		if (PWRCTRL_TYPE_GPIO == SYNO_GET_DISK_PWR_TYPE(idx)) {
			pData->blHddHotPlugSupport = 1;
			if (!HAVE_HDD_DETECT(idx)) {
				pData->blHddHotPlugSupport = 0;
				goto END;
			}
		}
	}
	if (0 == pData->blHddHotPlugSupport) {
		goto END;
	}

	pData->blHddHotPlugSupport = 1;
	pData->iMaxHddNum = gSynoInternalHddNumber;
	pData->blHddEnStat = kmalloc(sizeof(int) * pData->iMaxHddNum, GFP_KERNEL);
	memset(pData->blHddEnStat, 0, sizeof(int) * pData->iMaxHddNum);
	/* default wait time 7s */
	pData->iHddEnWait = SYNO_HDDMON_EN_WAIT_SEC;

#ifdef MY_ABC_HERE
	/* override wait time if setup spinup group */
	if (0 < giSynoSpinupGroupDelay) {
		pData->iHddEnWait = giSynoSpinupGroupDelay;
	}
#endif /* MY_ABC_HERE */

	iRet = 0;
END:
	return iRet;
}

static int syno_hddmon_unplug_monitor(void *args)
{
	int iRet = -1;
	int iIdx;
	int iPrzPinVal;
	SynoHddMonData_t *pData = NULL;
	unsigned int uiTimeout;

	if (NULL == args) {
		goto END;
	}

	pData = (SynoHddMonData_t *) args;

	while (1) {
		if (kthread_should_stop()) {
			break;
		}

		for (iIdx = 1; iIdx <= pData->iMaxHddNum; iIdx++) {
			if (pData->iProcessingIdx == iIdx) {
				continue;
			}

			if (PWRCTRL_TYPE_GPIO != SYNO_GET_DISK_PWR_TYPE(iIdx)) {
				continue;
			}

			iPrzPinVal = SYNO_CHECK_HDD_DETECT(iIdx);

			if (iPrzPinVal) {
				continue;
			}

			mdelay(200);
			SYNO_CTRL_HDD_POWERON(iIdx, iPrzPinVal);
			pData->blHddEnStat[iIdx-1] = iPrzPinVal;
		}

		uiTimeout = SYNO_HDDMON_POLL_SEC * HZ;
		do {
			set_current_state(TASK_INTERRUPTIBLE);
			uiTimeout = schedule_timeout(uiTimeout);
		} while (uiTimeout);
	}

	iRet = 0;
END:
	return iRet;
}

#ifdef MY_ABC_HERE
extern int iIsSynoIRQOff(const struct ata_port *ap);

static int syno_hddmon_is_disk_deepsleep(int iDiskIdx, SynoHddMonData_t *pData)
{
	struct ata_port  *pAtaPrt = NULL;
	int iErr = -1;

	if (NULL == pData) {
		goto END;
	}

	if (1 > iDiskIdx || pData->iMaxHddNum < iDiskIdx) {
		iErr = -EINVAL;
		goto END;
	}

	if (NULL == (pAtaPrt = syno_ata_port_get_by_port(iDiskIdx))) {
		iErr = -ENODEV;
		goto END;
	}

	iErr = iIsSynoIRQOff(pAtaPrt);
END:
	return iErr;
}
#endif /* MY_ABC_HERE */

static void syno_hddmon_task(SynoHddMonData_t *pData)
{
	int iIdx;
	int iPrzPinVal;
	static struct task_struct *pUnplugMonitor;
	unsigned int uiTimeout;

	if (NULL == pData) {
		goto END;
	}

	for (iIdx = 1; iIdx <= pData->iMaxHddNum; iIdx++) {
		pUnplugMonitor = NULL;
		pData->iProcessingIdx = iIdx;

		if (PWRCTRL_TYPE_GPIO != SYNO_GET_DISK_PWR_TYPE(iIdx)) {
			continue;
		}
		iPrzPinVal = SYNO_CHECK_HDD_DETECT(iIdx);

		if (pData->blHddEnStat[iIdx-1] != iPrzPinVal) {
#ifdef MY_ABC_HERE
			/*if the disk is plugged in while deep-sleep, do nothing*/
			if (iPrzPinVal && syno_hddmon_is_disk_deepsleep(iIdx, pData)) {
				continue;
			}
#endif /* MY_ABC_HERE */

			if (iPrzPinVal) {
				//while starting a port, monitoring other ports for the disks unplugged
				pUnplugMonitor = kthread_run(syno_hddmon_unplug_monitor, pData, SYNO_HDDMON_UPLG_STR);
			}

			mdelay(200);

			SYNO_CTRL_HDD_POWERON(iIdx, iPrzPinVal);
			pData->blHddEnStat[iIdx-1] = iPrzPinVal;

			if (iPrzPinVal) {
				uiTimeout = pData->iHddEnWait * HZ;
				do {
					set_current_state(TASK_INTERRUPTIBLE);
					uiTimeout = schedule_timeout(uiTimeout);
				} while (uiTimeout);
			}

			if (NULL != pUnplugMonitor) {
				kthread_stop(pUnplugMonitor);
			}
		}
	}

END:
	return;
}

static void syno_hddmon_sync(SynoHddMonData_t *pData)
{
	int iIdx;
	int iPrzPinVal;

	if (NULL == pData) {
		goto END;
	}

	for (iIdx = 1; iIdx <= pData->iMaxHddNum; iIdx++) {
		pData->iProcessingIdx = iIdx;

		if (PWRCTRL_TYPE_GPIO != SYNO_GET_DISK_PWR_TYPE(iIdx)) {
			continue;
		}
		iPrzPinVal = SYNO_CHECK_HDD_DETECT(iIdx);

		/* HDD Enable pins must be high just after boot-up,
		 * so turns the pins to low if the hdds do not present.
		 */
		if (!iPrzPinVal) {
			mdelay(200);
			SYNO_CTRL_HDD_POWERON(iIdx, iPrzPinVal);
			pData->blHddEnStat[iIdx-1] = iPrzPinVal;
		}

		/*sync the states*/
		pData->blHddEnStat[iIdx-1] = iPrzPinVal;

	}

END:
	return;
}

static int syno_hddmon_routine(void *args)
{
	int iRet = -1;
	SynoHddMonData_t *pData = NULL;
	unsigned int uiTimeout;

	if (NULL == args) {
		goto END;
	}

	pData = (SynoHddMonData_t *) args;

	while (1) {
		if (kthread_should_stop()) {
			break;
		}

		syno_hddmon_task(pData);

		uiTimeout = SYNO_HDDMON_POLL_SEC * HZ;
		do {
			set_current_state(TASK_INTERRUPTIBLE);
			uiTimeout = schedule_timeout(uiTimeout);
		} while (uiTimeout);
	}

	iRet = 0;
END:
	return iRet;
}

static int __init syno_hddmon_init(void)
{
	int iRet = -1;

	iRet = syno_hddmon_data_init(&synoHddMonData);
	if (0 > iRet) {
		goto END;
	}

	syno_hddmon_sync(&synoHddMonData);

	/* processing */
	pHddPrzPolling = kthread_create(syno_hddmon_routine, &synoHddMonData, SYNO_HDDMON_STR);

	if (IS_ERR(pHddPrzPolling)) {
		iRet = PTR_ERR(pHddPrzPolling);
		pHddPrzPolling = NULL;
		goto END;
	}

	wake_up_process(pHddPrzPolling);

	printk("Syno_HddMon: Initialization completed.\n");

	iRet = 0;
END:
	return iRet;
}

static void __exit syno_hddman_exit(void)
{
	if (pHddPrzPolling) {
		printk("###\n");
		WARN_ON(1);
		kthread_stop(pHddPrzPolling);
	}
	
	if (synoHddMonData.blHddEnStat) {
		kfree(synoHddMonData.blHddEnStat);
	}
	synoHddMonData.blHddEnStat = NULL;
}

MODULE_AUTHOR("Yikai Peng");
MODULE_DESCRIPTION("Syno_HddMon\n");
MODULE_LICENSE("Synology Inc.");

module_init(syno_hddmon_init);
module_exit(syno_hddman_exit);
