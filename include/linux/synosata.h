#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2003-2015 Synology Inc. All rights reserved.
#ifndef __SYNO_SATA_H_
#define __SYNO_SATA_H_

#ifdef MY_ABC_HERE
#include <linux/synobios.h>
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
extern int gSynoInternalHddNumber;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
extern int gSynoHddPowerupSeq;
extern long g_syno_hdd_powerup_seq;
extern long syno_boot_hd_count;
extern int giSynoSpinupGroup[16];
extern int giSynoSpinupGroupNum;
extern int giSynoSpinupGroupDelay;
extern int giSynoSpinupGroupDebug;
extern int giSynoDSleepCurrentSpinupGroupNum;
extern int giSynoDSleepCurrentSpinupGroupDiskNum;
extern int giSynoDSleepCurrentPoweronDisks;
extern void DBG_SpinupGroupListGpio(void);
extern int SynoHaveRPDetectPin(void);
extern int SynoAllRedundantPowerDetected(void);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#include <linux/mutex.h>
static struct mutex mutex_spin;
static DEFINE_MUTEX(mutex_spin);

#define DBG_SpinupGroup(x...)	\
	if (0 < giSynoSpinupGroupDebug) printk(x)

static inline int SpinupDelayEval(void)
{

	static int iDisksInSpinupGroup = 0;
	static int iCurrentSpinupGroup = 0;
	int iDelay = 0;

	if (0 >= giSynoSpinupGroupNum) {
		goto END;
	}

	mutex_lock(&mutex_spin);

	DBG_SpinupGroupListGpio();
	if (iDisksInSpinupGroup >= giSynoSpinupGroup[iCurrentSpinupGroup]) {
		iCurrentSpinupGroup++;
		iDisksInSpinupGroup = 0;
	}
	iDisksInSpinupGroup++;
 
	if (SynoHaveRPDetectPin() && SynoAllRedundantPowerDetected()) {
		goto SKIP;
	}
		
	iDelay = iCurrentSpinupGroup * giSynoSpinupGroupDelay;
SKIP:
	mutex_unlock(&mutex_spin);
END:
	return iDelay;
}

static inline void SynoResetDSleepGroup(void)
{
	if (0 < giSynoSpinupGroupNum) {
		//Reset to group 0
		giSynoDSleepCurrentSpinupGroupNum = 0;
		//Reset disk num of group 0, ex [2,1,1,1], here is 2
		giSynoDSleepCurrentSpinupGroupDiskNum = giSynoSpinupGroup[0];
		giSynoDSleepCurrentPoweronDisks = 0;
	}
}
static inline int SynoDSleepNeedUpdateLastPmOn(void)
{
	int ret = 0;
	if (0 == giSynoSpinupGroupNum) {
		ret = 1;
	} else {
		/* no RP or with RP pin but not plug both them */
		if (!SynoHaveRPDetectPin() ||
			(SynoHaveRPDetectPin() && !SynoAllRedundantPowerDetected())) {
			giSynoDSleepCurrentPoweronDisks++;
		}

		/* with RP pin and plug both them  */
		if (SynoHaveRPDetectPin() && SynoAllRedundantPowerDetected()) {
			/* don't add giSynoDSleepCurrentPoweronDisks means always not full */
			ret = 1;
		}

		if (giSynoDSleepCurrentPoweronDisks >= giSynoDSleepCurrentSpinupGroupDiskNum) {
			DBG_SpinupGroup("Disk Group %d is full, going to delay for power on.\n",giSynoDSleepCurrentSpinupGroupNum);
			DBG_SpinupGroupListGpio();
			giSynoDSleepCurrentPoweronDisks = 0;
			giSynoDSleepCurrentSpinupGroupNum++;
			if (giSynoDSleepCurrentSpinupGroupNum < giSynoSpinupGroupNum) {
				giSynoDSleepCurrentSpinupGroupDiskNum = giSynoSpinupGroup[giSynoDSleepCurrentSpinupGroupNum];
			} else {
				/* if syno_spinup_group not use all disks, left hdd poweron 1 by 1 */
				giSynoDSleepCurrentSpinupGroupDiskNum = 1;
			}
			ret = 1;
		}
	}
	return ret;
}
static inline unsigned long SynoWakeInterval(void)
{
	/* original WAKEINTERVAL = 7UL*HZ */
	static unsigned long uiSynoWakeInterval = 7UL*HZ;
	if (0 < giSynoSpinupGroupDelay) {
		uiSynoWakeInterval = (unsigned long)giSynoSpinupGroupDelay * HZ;
	}
	return uiSynoWakeInterval;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static inline unsigned char
syno_pm_is_jmb575(unsigned short vendor, unsigned short devid)
{
	return (vendor == 0x197b && devid == 0x5755);
}

#define MAX_EBOX_SN_LEN 16
typedef enum _tag_SYNO_PM_I2C_OPERATION {
	PM_I2C_OP_UNKNOWON	= 0x00,
	PM_I2C_OP_WRITE		= 0x01,
	PM_I2C_OP_READ		= 0x02,
} SYNO_PM_I2C_OPERATION;
# define SYNO_PMP_I2C_MAX_DATA_LEN 7
typedef struct _tag_SYNO_PM_I2C_PKG {
	SYNO_PM_I2C_OPERATION op;
	unsigned short addr;
	unsigned short offset;
	unsigned short len;
	unsigned char inputData[SYNO_PMP_I2C_MAX_DATA_LEN];
	unsigned char resultData[SYNO_PMP_I2C_MAX_DATA_LEN];
	bool blIsErr;
} SYNO_PM_I2C_PKG;

static inline void syno_init_i2c_pkg(SYNO_PM_I2C_PKG *pPkg, SYNO_PM_I2C_OPERATION op, \
									 unsigned short addr, unsigned short offset, unsigned short len)
{
	if (!pPkg) {
		goto END;
	}

	pPkg->op = op;
	pPkg->addr = addr;
	pPkg->offset = offset;
	pPkg->len = len;
	pPkg->blIsErr = false;

	memset(pPkg->inputData, 0, SYNO_PMP_I2C_MAX_DATA_LEN);
	memset(pPkg->resultData, 0, SYNO_PMP_I2C_MAX_DATA_LEN);
END:
	return;
}


typedef enum _tag_SYNO_PM_I2C_SYSFS_OP {
	SYNO_PM_I2C_SYSFS_UNKNOWN  			= 0x00,
	SYNO_PM_I2C_SYSFS_I2C_READ 			= 0x01,
	SYNO_PM_I2C_SYSFS_I2C_WRITE			= 0x02,
	SYNO_PM_I2C_SYSFS_JMB575_LED_CTL	= 0x03,
	SYNO_PM_I2C_SYSFS_POLLING           = 0x04,
} SYNO_PM_I2C_SYSFS_OP;

#define GPIO_9705_PKG_INIT(addr,data)	((addr << 10) | (0x3 << 8) | data)
/**
 * Kernel gpio package of our ebox.
 */
typedef struct _tag_SYNO_PM_PKG {
	/*	use for read/write */
	unsigned int	var;

	/* the gpio address */
	int	gpio_addr;

	/* the encode of gpio */
	void (*encode)(struct _tag_SYNO_PM_PKG *pm_pkg, int rw);

	/* the decode of gpio */
	void (*decode)(struct _tag_SYNO_PM_PKG *pm_pkg, int rw);
} SYNO_PM_PKG;

/* 9705 GPIO table */
/*
 *   NA      NA      NA      NA      GPIO19  GPIO18  GPIO17  GPIO16
 * R --      --      --      --      --      --      --      --
 * W --      --      --      --      A2	     A1      A0      Mask
 *
 *   GPIO15  GPIO14  GPIO13  GPIO12  GPIO11  GPIO10  GPIO09  GPIO08
 * R --      --      GPI8    GPI7    GPI6    LED_5   LED_4   LED_3
 * W R_CTL   W_CTL   GPO8    GPO7    GPO6    LED_5   LED_4   LED_3
 *
 *   GPIO07  GPIO06  GPIO05  GPIO04  GPIO03  GPIO02  GPIO01  GPIO00
 * R LED_2   LED_1   LED_H   GPI5    GPI4    GPI3    GPI2    GPI1
 * W LED_2   LED_1   LED_H   GPO5    GPO4    GPO3    GPO2    GPO1
 *
 */

/**
 * You should reference ebox spec for
 * the gpio definition of our 9705.
 *
 * Otherwise, you don't know what we do here
 *
 * @param pPM_pkg [OUT] Store the result. Should not be NULL.
 * @param rw      [IN] indicate the request is read or write.
 *                0: read
 *                1: write
 */
static inline void
SIMG9705_gpio_decode(SYNO_PM_PKG *pPM_pkg, int rw)
{
#define GPI_9705_BIT1(GPIO)	(1&GPIO)
#define GPI_9705_BIT2(GPIO)	((1<<1)&GPIO)
#define GPI_9705_BIT3(GPIO)	((1<<2)&GPIO)
#define GPI_9705_BIT4(GPIO)	((1<<3)&GPIO)
#define GPI_9705_BIT5(GPIO)	((1<<4)&GPIO)
#define GPI_9705_BIT6(GPIO)	((1<<11)&GPIO)>>6
#define GPI_9705_BIT7(GPIO)	((1<<12)&GPIO)>>6
#define GPI_9705_BIT8(GPIO)	((1<<13)&GPIO)>>6

	if (!rw) {
		pPM_pkg->var =
			GPI_9705_BIT1(pPM_pkg->var)|
			GPI_9705_BIT2(pPM_pkg->var)|
			GPI_9705_BIT3(pPM_pkg->var)|
			GPI_9705_BIT4(pPM_pkg->var)|
			GPI_9705_BIT5(pPM_pkg->var)|
			GPI_9705_BIT6(pPM_pkg->var)|
			GPI_9705_BIT7(pPM_pkg->var)|
			GPI_9705_BIT8(pPM_pkg->var);
	}
}

/**
 * You should reference ebox spec for
 * the gpio definition of our 9705.
 *
 * Otherwise, you don't know what we do here
 *
 * @param pPM_pkg [OUT] Store the result. Should not be NULL.
 * @param rw      [IN] indicate the request is read or write.
 *                0: read
 *                1: write
 */
static inline void
SIMG9705_gpio_encode(SYNO_PM_PKG *pPM_pkg, int rw)
{
#define GPIO_9705_BIT00(GPO)	(1&GPO)
#define GPIO_9705_BIT01(GPO)	((1<<1)&GPO)
#define GPIO_9705_BIT02(GPO)	((1<<2)&GPO)
#define GPIO_9705_BIT03(GPO)	((1<<3)&GPO)
#define GPIO_9705_BIT04(GPO)	((1<<4)&GPO)
#define GPIO_9705_BIT11(GPO)	((1<<5)&GPO)<<6
#define GPIO_9705_BIT12(GPO)	((1<<6)&GPO)<<6
#define GPIO_9705_BIT13(GPO)	((1<<7)&GPO)<<6
#define GPIO_9705_BIT14(GPO)	((1<<8)&GPO)<<6
#define GPIO_9705_BIT15(GPO)	((1<<9)&GPO)<<6
#define GPIO_9705_BIT17(GPO)	((1<<10)&GPO)<<7
#define GPIO_9705_BIT18(GPO)	((1<<11)&GPO)<<7
#define GPIO_9705_BIT19(GPO)	((1<<12)&GPO)<<7

	if (rw) {
		pPM_pkg->var =
			GPIO_9705_BIT00(pPM_pkg->var)|
			GPIO_9705_BIT01(pPM_pkg->var)|
			GPIO_9705_BIT02(pPM_pkg->var)|
			GPIO_9705_BIT03(pPM_pkg->var)|
			GPIO_9705_BIT04(pPM_pkg->var)|
			GPIO_9705_BIT11(pPM_pkg->var)|
			GPIO_9705_BIT12(pPM_pkg->var)|
			GPIO_9705_BIT13(pPM_pkg->var)|
			GPIO_9705_BIT14(pPM_pkg->var)|
			GPIO_9705_BIT15(pPM_pkg->var)|
			GPIO_9705_BIT17(pPM_pkg->var)|
			GPIO_9705_BIT18(pPM_pkg->var)|
			GPIO_9705_BIT19(pPM_pkg->var);
	}
}

static inline unsigned char
syno_pm_is_9705(unsigned short vendor, unsigned short devid)
{
	return (vendor == 0x1B4B  && devid == 0x9705);
}

static inline void
syno_pm_systemstate_pkg_init(unsigned short vendor, unsigned short devid, SYNO_PM_PKG *pPKG)
{
	/* do not check parameters, caller should do it */

	memset(pPKG, 0, sizeof(*pPKG));
	if (syno_pm_is_9705(vendor, devid)) {
		pPKG->var = GPIO_9705_PKG_INIT(3,0);
	}

	/* add other port multiplier here */
}

static inline void
syno_pm_unique_pkg_init(unsigned short vendor, unsigned short devid, SYNO_PM_PKG *pPKG)
{
	/* do not check parameters, caller should do it */

	memset(pPKG, 0, sizeof(*pPKG));
	if (syno_pm_is_9705(vendor, devid)) {
		pPKG->var = GPIO_9705_PKG_INIT(0,0);
	}

	/* add other port multiplier here */
}

static inline void
syno_pm_raidledstate_pkg_init(unsigned short vendor, unsigned short devid, SYNO_PM_PKG *pPKG)
{
	/* do not check parameters, caller should do it */

	memset(pPKG, 0, sizeof(*pPKG));
	if (syno_pm_is_9705(vendor, devid)) {
		pPKG->var = GPIO_9705_PKG_INIT(4,0);
	}
	/* add other port multiplier here */
}

static inline void
syno_pm_fanstatus_pkg_init(unsigned short vendor, unsigned short devid, SYNO_PM_PKG *pPKG)
{
	/* do not check parameters, caller should do it */

	memset(pPKG, 0, sizeof(*pPKG));
	if (syno_pm_is_9705(vendor, devid)) {
		pPKG->var = GPIO_9705_PKG_INIT(2,0);
	}

	/* add other port multiplier here */
}

static inline void
syno_pm_poweron_pkg_init(unsigned short vendor, unsigned short devid, SYNO_PM_PKG *pPKG, unsigned char blCLR)
{
	/* do not check parameters, caller should do it */

	memset(pPKG, 0, sizeof(*pPKG));
	if (syno_pm_is_9705(vendor, devid)) {
		if (blCLR) {
			pPKG->var = GPIO_9705_PKG_INIT(4,0b10);
		} else {
			pPKG->var = GPIO_9705_PKG_INIT(4,0b10010);
		}
	}

	/* add other port multiplier here */
}

/**
 * Init eunit deepsleep indicator
 *
 * @param vendor  [IN] PMP vendor
 * @param devid   [IN] device id
 * @param pPM_pkg [IN] Store the result. Should not be NULL.
 * @param blCLR   [IN] clean or not
 *
 * return 0: not support deepsleep indicator
 *        1: support deepsleep indicator
 */
static inline int
syno_pm_deepsleep_indicator_pkg_init(unsigned short vendor, unsigned short devid, SYNO_PM_PKG *pPKG, unsigned char blCLR)
{
	/* do not check parameters, caller should do it */
	int iRet = 0;

	memset(pPKG, 0, sizeof(*pPKG));
	if (syno_pm_is_9705(vendor, devid)) {
		if (blCLR) {
			pPKG->var = GPIO_9705_PKG_INIT(1,0);
		} else {
			pPKG->var = GPIO_9705_PKG_INIT(1,0x80);
		}
		iRet = 1;
	}
	/* add other port multiplier here */
	return iRet;
}

static inline void
syno_pm_enable_powerbtn_pkg_init(unsigned short vendor, unsigned short devid, SYNO_PM_PKG *pPKG)
{
	/* do not check parameters, caller should do it */

	memset(pPKG, 0, sizeof(*pPKG));
	if (syno_pm_is_9705(vendor, devid)) {
		pPKG->var = GPIO_9705_PKG_INIT(4,0x20);
	}

	/* add other port multiplier here */
}

static inline unsigned int
syno_support_disk_num(unsigned short vendor,
					  unsigned short devid,
					  unsigned int syno_uniq)
{
	unsigned int ret = 0;

	if (syno_pm_is_9705(vendor, devid)) {
		if (IS_SYNOLOGY_RX413(syno_uniq) || IS_SYNOLOGY_RX418(syno_uniq)) {
			ret = 4;
		} else if (IS_SYNOLOGY_RX1214(syno_uniq) || IS_SYNOLOGY_RX1217(syno_uniq) ||
			   IS_SYNOLOGY_DX1215(syno_uniq) || IS_SYNOLOGY_DX1222(syno_uniq) ||
			   IS_SYNOLOGY_DX1215II(syno_uniq)) {
			ret = 3;
		} else if (IS_SYNOLOGY_DX517(syno_uniq)) {
			ret = 5;
		} else {
			printk("%s not synology device", __FUNCTION__);
			ret = 5;
		}
	} else if (syno_pm_is_jmb575(vendor, devid)) {
		if (IS_SYNOLOGY_RX1223RP(syno_uniq)) {
			ret = 3;
		}
	}

	/* add other chip here */

	return ret;
}

static inline void
syno_pm_hddled_status_pkg_init(unsigned short vendor, unsigned short devid, SYNO_PM_PKG *pPKG)
{
	/* do not check parameters, caller should do it */

	memset(pPKG, 0, sizeof(*pPKG));

	if (syno_pm_is_9705(vendor, devid)) {
		pPKG->var = GPIO_9705_PKG_INIT(1,0);
	}

	/* add other port multiplier here */
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE

/*
 *back porting from linux 2.6.28. add SYNO prefix in order to not mixed with libata
 */
#define SYNO_ATA_ID_MAJOR_VER	 80
#define SYNO_ATA_ID_MINOR_VER	 81
#define SYNO_ATA_ID_COMMAND_SET_1 82
#define SYNO_ATA_ID_COMMAND_SET_2 83
#define SYNO_ATA_ID_CFSSE		 84
#define SYNO_ATA_ID_ROT_SPEED	 217

/**
 * Determind the ata version.
 *
 * Copy from ata.h
 *
 * @param id     [IN] Should not be NULL. ata identify buffer.
 *
 * @return ata version
 */
static inline unsigned int
ata_major_version(const unsigned short *id)
{
	unsigned int mver;

	if (id[SYNO_ATA_ID_MAJOR_VER] == 0xFFFF)
		return 0;

	for (mver = 14; mver >= 1; mver--)
		if (id[SYNO_ATA_ID_MAJOR_VER] & (1 << mver))
			break;
	return mver;
}

/**
 * Determind the ata version.
 *
 * Copy from linux-2.6.28 later in ata.h. Original from mail
 * list. But it has bug. So i customized it.
 *
 * Sometime you can't just only take care in major version.
 * The actually ATA version might need to look minor version.
 * Please refer smartmontools-5.38/atacmds.cpp
 * const char minor_str []  = ...
 *
 * @param id     [IN] Should not be NULL. ata identify buffer.
 *
 * @return ata version
 */
static inline int
syno_ata_id_is_ssd(const unsigned short *id)
{
	int res = 0;
	unsigned int major_id = ata_major_version(id);

	/* ATA8-ACS version 4c or higher (=> 4c or 6 at the moment) */
	if (7 <= major_id){
		if (id[SYNO_ATA_ID_ROT_SPEED] == 0x01) {
			// intel ssd, and the laters ssd
			res = 1;
			goto END;
		}
	}

	if ((id[SYNO_ATA_ID_COMMAND_SET_2]>>14) == 0x01 &&
		!(id[SYNO_ATA_ID_COMMAND_SET_1] & 0x0001)) {
		// not support smart. like innodisk
		res = 1;
		goto END;
	}

	// transcend. Not support smart error log
	if ((id[SYNO_ATA_ID_COMMAND_SET_2]>>14) == 0x01 &&
		(id[SYNO_ATA_ID_COMMAND_SET_1] & 0x0001) &&
		!(id[SYNO_ATA_ID_CFSSE] & 0x1)) {
		res = 1;
		goto END;
	}

END:
	return res;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define SZK_PMP_UEVENT "SYNO_PMP_EVENT"
#define SZV_PMP_CONNECT "CABLE_CONNECT"
#define SZV_PMP_DISCONNECT "CABLE_DISCONNECT"
#endif /* MY_ABC_HERE */

#endif /* __SYNO_SATA_H_ */
