#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Simple MTD partitioning layer
 *
 * Copyright © 2000 Nicolas Pitre <nico@fluxnic.net>
 * Copyright © 2002 Thomas Gleixner <gleixner@linutronix.de>
 * Copyright © 2000-2010 David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kmod.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/err.h>
#include <linux/of.h>
#ifdef MY_ABC_HERE
#include <linux/synolib.h>
#endif /* MY_ABC_HERE */

#include "mtdcore.h"

#ifdef MY_ABC_HERE
extern unsigned char grgbLanMac[SYNO_MAC_MAX_NUMBER][16];
extern int giVenderFormatVersion;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
extern char gszSerialNum[];
extern char gszCustomSerialNum[];
#define SYNO_SN_TAG "SN="
#define SYNO_CHKSUM_TAG "CHK="
#define SYNO_SN_12_SIG SYNO_SN_TAG  // signature for 12 serial number
#endif /* MY_ABC_HERE */

/*
 * MTD methods which simply translate the effective address and pass through
 * to the _real_ device.
 */

static inline void free_partition(struct mtd_info *mtd)
{
	kfree(mtd->name);
	kfree(mtd);
}

#ifdef MY_ABC_HERE
#define SYNO_MAC_LEN            6
#define SYNO_MAC_CHK_LEN        1
#define SYNO_MAC_MAX_NUM        8
#define SYNO_MAC_BUF_LEN        16
#endif /* MY_ABC_HERE */

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
#define SYNO_VENDER_HEADER_SIZE     0x10
#define SYNO_VENDER_SN_SIZE     0x20
#define SYNO_VENDER_CUSTSN_SIZE     0x20
#define SYNO_VENDER_TESTFLAG_SIZE   0x80
#define SYNO_VENDER_MAC_SIZE        (SYNO_MAC_LEN + SYNO_MAC_CHK_LEN)

#define SYNO_VENDER_SN_OFFSET       SYNO_VENDER_HEADER_SIZE
#define SYNO_VENDER_CUSTSN_OFFSET   (SYNO_VENDER_SN_OFFSET + SYNO_VENDER_SN_SIZE)
#define SYNO_VENDER_TESTFLAG_OFFSET (SYNO_VENDER_CUSTSN_OFFSET + SYNO_VENDER_CUSTSN_SIZE)
#define SYNO_VENDER_MAC_OFFSET      (SYNO_VENDER_TESTFLAG_OFFSET + SYNO_VENDER_TESTFLAG_SIZE)

#define SYNO_VENDER_MAGIC_HEADER    "SYNO!!!!"
#define SYNO_VENDER_SN_TOKEN        "SN="
#define SYNO_VENDER_SN_CKSUM_TOKEN  "CHK="

#ifdef MY_ABC_HERE
static int syno_vender_sn_verify(char *szSN)
{
	int errRes = 0;
	char *pSNum = NULL;
	char *pCksum = NULL;
	unsigned int uiCalChksum = 0, uiReadChksum = 0;

	if (NULL == szSN) {
		errRes = -EINVAL;
		goto END;
	}

	pSNum = strstr(szSN, SYNO_VENDER_SN_TOKEN);
	pCksum = strstr(szSN, SYNO_VENDER_SN_CKSUM_TOKEN);

	if (NULL == pSNum || NULL == pCksum) {
		errRes = -EBADF;
		goto END;
	}

	pSNum += strlen(SYNO_VENDER_SN_TOKEN);
	pCksum += strlen(SYNO_VENDER_SN_CKSUM_TOKEN);

	while ( '\0' != *pCksum) {
		if ('0' > *pCksum || '9' < *pCksum) {
			uiReadChksum = 0;
			break;
		}

		uiReadChksum = (uiReadChksum * 10) + (unsigned int)(*pCksum - '0');
		pCksum++;
	}

	while (',' != *pSNum && '\0' != *pSNum) {
		uiCalChksum += (unsigned int) *pSNum;
		pSNum++;
	}

	if ((0 == uiCalChksum) || (uiCalChksum != uiReadChksum)) {
		errRes = -EBADF;
		goto END;
	}

	*pSNum = '\0';
END:
	return errRes;
}

static int syno_vender_cust_sn_verify(char *szCustSN)
{
	int errRes = 0;
	unsigned char uCalCksum = 0;
	int iCnt = 0;

	if (NULL == szCustSN) {
		errRes = -EINVAL;
		goto END;
	}

	for (iCnt=0; iCnt<(SYNO_VENDER_CUSTSN_SIZE - 1); iCnt++) {
		uCalCksum += szCustSN[iCnt];
	}

	if ((0 == uCalCksum) ||
	    (uCalCksum != szCustSN[(SYNO_VENDER_CUSTSN_SIZE - 1)])) {
		errRes = -EBADF;
		goto END;
	}

	szCustSN[(SYNO_VENDER_CUSTSN_SIZE - 1)] = '\0';

END:
	return errRes;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int syno_vender_mac_parser(u_char* rgbszBuf)
{
	int errRes = 0;
	unsigned char ucMacBuf[SYNO_VENDER_MAC_SIZE] = {0};
	unsigned char ucCksum = 0;
	int iCnt = 0, iNrZeroBit, iNrFFBit = 0;
	int iMacNum = 0;

	if (!rgbszBuf) {
		errRes = -EINVAL;
		goto END;
	}

	while(SYNO_MAC_MAX_NUM > iMacNum){
		memcpy(ucMacBuf, (rgbszBuf+(iMacNum*SYNO_VENDER_MAC_SIZE)), sizeof(ucMacBuf));
		ucCksum = 0;

		for (iCnt = 0, iNrZeroBit = 0, iNrFFBit = 0; iCnt < SYNO_MAC_LEN; iCnt++) {
			if (0 == ucMacBuf[iCnt]) {
				iNrZeroBit++;
			} else if (0xff == ucMacBuf[iCnt]) {
				iNrFFBit++;
			}
			ucCksum += ucMacBuf[iCnt];
		}

		if (SYNO_MAC_LEN == iNrFFBit) {
			break;
		}

		if ((SYNO_MAC_LEN > iNrZeroBit) && (ucCksum == ucMacBuf[SYNO_MAC_LEN])) {
			snprintf(grgbLanMac[iMacNum], sizeof(grgbLanMac),
						"%02x%02x%02x%02x%02x%02x",
						ucMacBuf[0],
						ucMacBuf[1],
						ucMacBuf[2],
						ucMacBuf[3],
						ucMacBuf[4],
						ucMacBuf[5]);

			printk("vender Mac%d address : %c%c:%c%c:%c%c:%c%c:%c%c:%c%c\n", iMacNum,
												  grgbLanMac[iMacNum][0],
												  grgbLanMac[iMacNum][1],
												  grgbLanMac[iMacNum][2],
												  grgbLanMac[iMacNum][3],
												  grgbLanMac[iMacNum][4],
												  grgbLanMac[iMacNum][5],
												  grgbLanMac[iMacNum][6],
												  grgbLanMac[iMacNum][7],
												  grgbLanMac[iMacNum][8],
												  grgbLanMac[iMacNum][9],
												  grgbLanMac[iMacNum][10],
												  grgbLanMac[iMacNum][11]);

		} else {
			grgbLanMac[iMacNum][0] = '\0';
		}

		iMacNum++;
	}

END:
	return errRes;
}
#endif /* MY_ABC_HERE */

static int syno_vender_v2_parser(struct mtd_info *mtd_vender)
{
	int errRes = -1;
	u_char rgbszBuf[128];
	size_t retlen;
	char szHeader[SYNO_VENDER_HEADER_SIZE] = {0};
	char szSN[SYNO_VENDER_SN_SIZE] = {0};
	char szCustSN[SYNO_VENDER_CUSTSN_SIZE] = {0};
	u_char* ptr = NULL;

	memset(rgbszBuf, 0, 128*sizeof(u_char));
	mtd_read(mtd_vender, 0, 128, &retlen, rgbszBuf);
	ptr = rgbszBuf;

	// check header
	memcpy(szHeader, ptr, SYNO_VENDER_HEADER_SIZE);
	if (0 != strncmp(szHeader, SYNO_VENDER_MAGIC_HEADER, strlen(SYNO_VENDER_MAGIC_HEADER))) {
		errRes = -ENOENT;
		goto END;
	}

#ifdef MY_ABC_HERE
	memset(gszSerialNum, 0, 32);
	memset(gszCustomSerialNum, 0, 32);

	// read SN
	ptr += SYNO_VENDER_HEADER_SIZE;
	memcpy(szSN, ptr, sizeof(szSN));
	errRes = syno_vender_sn_verify(szSN);
	if (0 == errRes) {
		snprintf(gszSerialNum, 32, "%s", szSN + strlen(SYNO_VENDER_SN_TOKEN));
	}
	printk("Serial Number='%s'", gszSerialNum);

	// read custom SN
	ptr += SYNO_VENDER_SN_SIZE;
	memcpy(szCustSN, ptr, sizeof(szCustSN));
	errRes = syno_vender_cust_sn_verify(szCustSN);
	if (0 == errRes) {
		snprintf(gszCustomSerialNum, 32, "%s", szCustSN);
		printk("Custom Serial Number: %s\n", gszCustomSerialNum);
	}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	memset(rgbszBuf, 0, 128*sizeof(u_char));
	mtd_read(mtd_vender, SYNO_VENDER_MAC_OFFSET, (SYNO_MAC_MAX_NUM*SYNO_VENDER_MAC_SIZE), &retlen, rgbszBuf);

	// read MACs
	syno_vender_mac_parser(rgbszBuf);
#endif /* MY_ABC_HERE */

	errRes = 0;
END:
	return errRes;
}

static int syno_vender_v1_parser(struct mtd_info *mtd_vender)
{
	u_char rgbszBuf[128];
	size_t retlen;

	int i = 0, x = 0;
	unsigned int Sum;
	u_char ucSum;
#ifdef MY_ABC_HERE
	int n = 0;
	int MacNumber = 4;
	char rgbLanMac[SYNO_MAC_MAX_NUMBER][6];
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	char szSerialBuffer[32];
	char *ptr;
	char szSerial[32];
	char szCheckSum[32];
	unsigned long ulchksum = 0;
	unsigned long ulTemp = 0;
#endif /* MY_ABC_HERE */

	mtd_read(mtd_vender, 0, 128, &retlen, rgbszBuf);

#ifdef MY_ABC_HERE
	x = 0;
	for (n = 0; n < MacNumber; n++) {
		for (Sum=0,ucSum=0,i=0; i<6; i++) {
			Sum+=rgbszBuf[i+x];
			ucSum+=rgbszBuf[i+x];
			rgbLanMac[n][i] = rgbszBuf[i+x];
		}
		x+=6;

		if (0==Sum) {
			printk("vender Mac%d doesn't set ucSum:0x%02x Buf:0x%02x Sum:%d.\n",
					n, ucSum, rgbszBuf[x], Sum);
		} else if (ucSum!=rgbszBuf[x]) {
			printk("vender Mac%d checksum error ucSum:0x%02x Buf:0x%02x Sum:%d.\n",
					n, ucSum, rgbszBuf[x], Sum);
			grgbLanMac[n][0] = '\0';
		} else {
			printk("vender Mac%d address : %02x:%02x:%02x:%02x:%02x:%02x\n",n,rgbLanMac[n][0],
											  rgbLanMac[n][1],
											  rgbLanMac[n][2],
											  rgbLanMac[n][3],
											  rgbLanMac[n][4],
											  rgbLanMac[n][5]);
			snprintf(grgbLanMac[n], sizeof(grgbLanMac),
					"%02x%02x%02x%02x%02x%02x",
			rgbLanMac[n][0],
			rgbLanMac[n][1],
			rgbLanMac[n][2],
			rgbLanMac[n][3],
			rgbLanMac[n][4],
			rgbLanMac[n][5]);
		}

		x++;
	}
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	memset(szSerial, 0, sizeof(szSerial));
	memset(szCheckSum, 0, sizeof(szCheckSum));
	memset(gszSerialNum, 0, 32);
	memcpy(szSerialBuffer, &(rgbszBuf[32]), 32);

	// this is new defined SN
	if (0 == strncmp(szSerialBuffer, SYNO_SN_12_SIG,strlen(SYNO_SN_12_SIG))) {
		//paring serial number with format 'SN=1350KKN99999'
		ptr = strstr(szSerialBuffer, SYNO_SN_TAG);
		if (NULL == ptr) {
			printk("no serial tag found, serial buffer='%s'\n", szSerialBuffer);
			goto SKIP_SERIAL;
		}
		ptr += strlen(SYNO_SN_TAG);
		i = 0;
		while (0 != *ptr && ',' != *ptr) {
			szSerial[i++] = *ptr;
			ptr++;
		}
		szSerial[i] = '\0';

		//paring serial number with format 'CHK=125'
		ptr = strstr(szSerialBuffer, SYNO_CHKSUM_TAG);
		if (NULL == ptr) {
			printk("no checksum tag found, serial buffer='%s'\n", szSerialBuffer);
			goto SKIP_SERIAL;
		}
		ptr += strlen(SYNO_CHKSUM_TAG);
		i = 0;
		while (0 != *ptr && ',' != *ptr) {
			szCheckSum[i++] = *ptr;
			ptr++;
		}
		szCheckSum[i] = '\0';

		//calculate checksum
		for (i = 0 ; i < strlen(szSerial); i++) {
			ulchksum += szSerial[i];
		}

		//------ check checksum ------
		if (0 != kstrtoul(szCheckSum, 10, &ulTemp)) {
			printk("string conversion error: '%s'\n", szCheckSum);
			goto SKIP_SERIAL;
		} else if (ulchksum != ulTemp) {
			printk("serial number checksum error, serial='%s', checksum='%lu' not '%lu'\n", szSerial, ulchksum, ulTemp);
			goto SKIP_SERIAL;
		}
	} else {
		unsigned char ucChkSum = 0;
		//calculate checksum
		for (i = 0 ; i < 10; i++) {
			ucChkSum += szSerialBuffer[i];
		}
		//------ check checksum ------
		if (ucChkSum != szSerialBuffer[10]) {
			printk("serial number checksum error, serial='%s', checksum='%d' not '%d'", szSerialBuffer, ucChkSum, szSerialBuffer[10]);
			goto SKIP_SERIAL;
		} else {
			memcpy(szSerial, szSerialBuffer, 10);
		}
	}
	snprintf(gszSerialNum, 32, "%s", szSerial);
SKIP_SERIAL:
	printk("Serial Number='%s'", gszSerialNum);

	//read custom serial number out, it is in the vender partion shift 64 bytes.
	x = 64;
	for (Sum=0,ucSum=0,i=0; i<31; i++) {
		Sum+=rgbszBuf[i+x];
		ucSum+=rgbszBuf[i+x];
		gszCustomSerialNum[i] = rgbszBuf[i+x];
	}
	x+=31;
	if (Sum==0 || ucSum!=rgbszBuf[x]) {
		for (i=0; i<32; i++) {
			gszCustomSerialNum[i] = 0;
		}
	} else {
		printk("Custom Serial Number: %s\n", gszCustomSerialNum);
	}
#endif /* MY_ABC_HERE */

	return 0;
}
#endif /* MY_ABC_HERE) || MY_ABC_HERE */

static struct mtd_info *allocate_partition(struct mtd_info *parent,
					   const struct mtd_partition *part,
					   int partno, uint64_t cur_offset)
{
	struct mtd_info *master = mtd_get_master(parent);
	int wr_alignment = (parent->flags & MTD_NO_ERASE) ?
			   master->writesize : master->erasesize;
	u64 parent_size = mtd_is_partition(parent) ?
			  parent->part.size : parent->size;
	struct mtd_info *child;
	u32 remainder;
	char *name;
	u64 tmp;

	/* allocate the partition structure */
	child = kzalloc(sizeof(*child), GFP_KERNEL);
	name = kstrdup(part->name, GFP_KERNEL);
	if (!name || !child) {
		printk(KERN_ERR"memory allocation error while creating partitions for \"%s\"\n",
		       parent->name);
		kfree(name);
		kfree(child);
		return ERR_PTR(-ENOMEM);
	}

	/* set up the MTD object for this partition */
	child->type = parent->type;
	child->part.flags = parent->flags & ~part->mask_flags;
	child->part.flags |= part->add_flags;
	child->flags = child->part.flags;
	child->part.size = part->size;
	child->writesize = parent->writesize;
	child->writebufsize = parent->writebufsize;
	child->oobsize = parent->oobsize;
	child->oobavail = parent->oobavail;
	child->subpage_sft = parent->subpage_sft;

	child->name = name;
	child->owner = parent->owner;

	/* NOTE: Historically, we didn't arrange MTDs as a tree out of
	 * concern for showing the same data in multiple partitions.
	 * However, it is very useful to have the master node present,
	 * so the MTD_PARTITIONED_MASTER option allows that. The master
	 * will have device nodes etc only if this is set, so make the
	 * parent conditional on that option. Note, this is a way to
	 * distinguish between the parent and its partitions in sysfs.
	 */
	child->dev.parent = IS_ENABLED(CONFIG_MTD_PARTITIONED_MASTER) || mtd_is_partition(parent) ?
			    &parent->dev : parent->dev.parent;
	child->dev.of_node = part->of_node;
	child->parent = parent;
	child->part.offset = part->offset;
	INIT_LIST_HEAD(&child->partitions);

	if (child->part.offset == MTDPART_OFS_APPEND)
		child->part.offset = cur_offset;
	if (child->part.offset == MTDPART_OFS_NXTBLK) {
		tmp = cur_offset;
		child->part.offset = cur_offset;
		remainder = do_div(tmp, wr_alignment);
		if (remainder) {
			child->part.offset += wr_alignment - remainder;
			printk(KERN_NOTICE "Moving partition %d: "
			       "0x%012llx -> 0x%012llx\n", partno,
			       (unsigned long long)cur_offset,
			       child->part.offset);
		}
	}
	if (child->part.offset == MTDPART_OFS_RETAIN) {
		child->part.offset = cur_offset;
		if (parent_size - child->part.offset >= child->part.size) {
			child->part.size = parent_size - child->part.offset -
					   child->part.size;
		} else {
			printk(KERN_ERR "mtd partition \"%s\" doesn't have enough space: %#llx < %#llx, disabled\n",
				part->name, parent_size - child->part.offset,
				child->part.size);
			/* register to preserve ordering */
			goto out_register;
		}
	}
	if (child->part.size == MTDPART_SIZ_FULL)
		child->part.size = parent_size - child->part.offset;

	printk(KERN_NOTICE "0x%012llx-0x%012llx : \"%s\"\n",
	       child->part.offset, child->part.offset + child->part.size,
	       child->name);

	/* let's do some sanity checks */
	if (child->part.offset >= parent_size) {
		/* let's register it anyway to preserve ordering */
		child->part.offset = 0;
		child->part.size = 0;

		/* Initialize ->erasesize to make add_mtd_device() happy. */
		child->erasesize = parent->erasesize;
		printk(KERN_ERR"mtd: partition \"%s\" is out of reach -- disabled\n",
			part->name);
		goto out_register;
	}
	if (child->part.offset + child->part.size > parent->size) {
		child->part.size = parent_size - child->part.offset;
		printk(KERN_WARNING"mtd: partition \"%s\" extends beyond the end of device \"%s\" -- size truncated to %#llx\n",
			part->name, parent->name, child->part.size);
	}

	if (parent->numeraseregions > 1) {
		/* Deal with variable erase size stuff */
		int i, max = parent->numeraseregions;
		u64 end = child->part.offset + child->part.size;
		struct mtd_erase_region_info *regions = parent->eraseregions;

		/* Find the first erase regions which is part of this
		 * partition. */
		for (i = 0; i < max && regions[i].offset <= child->part.offset;
		     i++)
			;
		/* The loop searched for the region _behind_ the first one */
		if (i > 0)
			i--;

		/* Pick biggest erasesize */
		for (; i < max && regions[i].offset < end; i++) {
			if (child->erasesize < regions[i].erasesize)
				child->erasesize = regions[i].erasesize;
		}
		BUG_ON(child->erasesize == 0);
	} else {
		/* Single erase size */
		child->erasesize = master->erasesize;
	}

	/*
	 * Child erasesize might differ from the parent one if the parent
	 * exposes several regions with different erasesize. Adjust
	 * wr_alignment accordingly.
	 */
	if (!(child->flags & MTD_NO_ERASE))
		wr_alignment = child->erasesize;

	tmp = mtd_get_master_ofs(child, 0);
	remainder = do_div(tmp, wr_alignment);
	if ((child->flags & MTD_WRITEABLE) && remainder) {
		/* Doesn't start on a boundary of major erase size */
		/* FIXME: Let it be writable if it is on a boundary of
		 * _minor_ erase size though */
		child->flags &= ~MTD_WRITEABLE;
		printk(KERN_WARNING"mtd: partition \"%s\" doesn't start on an erase/write block boundary -- force read-only\n",
			part->name);
	}

	tmp = mtd_get_master_ofs(child, 0) + child->part.size;
	remainder = do_div(tmp, wr_alignment);
	if ((child->flags & MTD_WRITEABLE) && remainder) {
		child->flags &= ~MTD_WRITEABLE;
		printk(KERN_WARNING"mtd: partition \"%s\" doesn't end on an erase/write block -- force read-only\n",
			part->name);
	}

	child->size = child->part.size;
	child->ecc_step_size = parent->ecc_step_size;
	child->ecc_strength = parent->ecc_strength;
	child->bitflip_threshold = parent->bitflip_threshold;

	if (master->_block_isbad) {
		uint64_t offs = 0;

		while (offs < child->part.size) {
			if (mtd_block_isreserved(child, offs))
				child->ecc_stats.bbtblocks++;
			else if (mtd_block_isbad(child, offs))
				child->ecc_stats.badblocks++;
			offs += child->erasesize;
		}
	}

out_register:
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	if ((memcmp(part->name, "vender", 7)==0) ||
		(memcmp(part->name, "vendor", 7)==0)) {

		switch (giVenderFormatVersion) {
		case 1:
			syno_vender_v1_parser(child);
			break;
		case 2:
			syno_vender_v2_parser(child);
			break;
		default:
			printk(KERN_ERR "Undefined verder version %d\n", giVenderFormatVersion);
		}
	}
#endif /* MY_ABC_HERE || MY_ABC_HERE */
	return child;
}

static ssize_t mtd_partition_offset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%lld\n", mtd->part.offset);
}

static DEVICE_ATTR(offset, S_IRUGO, mtd_partition_offset_show, NULL);

static const struct attribute *mtd_partition_attrs[] = {
	&dev_attr_offset.attr,
	NULL
};

static int mtd_add_partition_attrs(struct mtd_info *new)
{
	int ret = sysfs_create_files(&new->dev.kobj, mtd_partition_attrs);
	if (ret)
		printk(KERN_WARNING
		       "mtd: failed to create partition attrs, err=%d\n", ret);
	return ret;
}

int mtd_add_partition(struct mtd_info *parent, const char *name,
		      long long offset, long long length)
{
	struct mtd_info *master = mtd_get_master(parent);
	u64 parent_size = mtd_is_partition(parent) ?
			  parent->part.size : parent->size;
	struct mtd_partition part;
	struct mtd_info *child;
	int ret = 0;

	/* the direct offset is expected */
	if (offset == MTDPART_OFS_APPEND ||
	    offset == MTDPART_OFS_NXTBLK)
		return -EINVAL;

	if (length == MTDPART_SIZ_FULL)
		length = parent_size - offset;

	if (length <= 0)
		return -EINVAL;

	memset(&part, 0, sizeof(part));
	part.name = name;
	part.size = length;
	part.offset = offset;

	child = allocate_partition(parent, &part, -1, offset);
	if (IS_ERR(child))
		return PTR_ERR(child);

	mutex_lock(&master->master.partitions_lock);
	list_add_tail(&child->part.node, &parent->partitions);
	mutex_unlock(&master->master.partitions_lock);

	ret = add_mtd_device(child);
	if (ret)
		goto err_remove_part;

	mtd_add_partition_attrs(child);

	return 0;

err_remove_part:
	mutex_lock(&master->master.partitions_lock);
	list_del(&child->part.node);
	mutex_unlock(&master->master.partitions_lock);

	free_partition(child);

	return ret;
}
EXPORT_SYMBOL_GPL(mtd_add_partition);

/**
 * __mtd_del_partition - delete MTD partition
 *
 * @priv: MTD structure to be deleted
 *
 * This function must be called with the partitions mutex locked.
 */
static int __mtd_del_partition(struct mtd_info *mtd)
{
	struct mtd_info *child, *next;
	int err;

	list_for_each_entry_safe(child, next, &mtd->partitions, part.node) {
		err = __mtd_del_partition(child);
		if (err)
			return err;
	}

	sysfs_remove_files(&mtd->dev.kobj, mtd_partition_attrs);

	err = del_mtd_device(mtd);
	if (err)
		return err;

	list_del(&child->part.node);
	free_partition(mtd);

	return 0;
}

/*
 * This function unregisters and destroy all slave MTD objects which are
 * attached to the given MTD object, recursively.
 */
static int __del_mtd_partitions(struct mtd_info *mtd)
{
	struct mtd_info *child, *next;
	LIST_HEAD(tmp_list);
	int ret, err = 0;

	list_for_each_entry_safe(child, next, &mtd->partitions, part.node) {
		if (mtd_has_partitions(child))
			__del_mtd_partitions(child);

		pr_info("Deleting %s MTD partition\n", child->name);
		ret = del_mtd_device(child);
		if (ret < 0) {
			pr_err("Error when deleting partition \"%s\" (%d)\n",
			       child->name, ret);
			err = ret;
			continue;
		}

		list_del(&child->part.node);
		free_partition(child);
	}

	return err;
}

int del_mtd_partitions(struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master(mtd);
	int ret;

	pr_info("Deleting MTD partitions on \"%s\":\n", mtd->name);

	mutex_lock(&master->master.partitions_lock);
	ret = __del_mtd_partitions(mtd);
	mutex_unlock(&master->master.partitions_lock);

	return ret;
}

int mtd_del_partition(struct mtd_info *mtd, int partno)
{
	struct mtd_info *child, *master = mtd_get_master(mtd);
	int ret = -EINVAL;

	mutex_lock(&master->master.partitions_lock);
	list_for_each_entry(child, &mtd->partitions, part.node) {
		if (child->index == partno) {
			ret = __mtd_del_partition(child);
			break;
		}
	}
	mutex_unlock(&master->master.partitions_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(mtd_del_partition);

/*
 * This function, given a parent MTD object and a partition table, creates
 * and registers the child MTD objects which are bound to the parent according
 * to the partition definitions.
 *
 * For historical reasons, this function's caller only registers the parent
 * if the MTD_PARTITIONED_MASTER config option is set.
 */

int add_mtd_partitions(struct mtd_info *parent,
		       const struct mtd_partition *parts,
		       int nbparts)
{
	struct mtd_info *child, *master = mtd_get_master(parent);
	uint64_t cur_offset = 0;
	int i, ret;

	printk(KERN_NOTICE "Creating %d MTD partitions on \"%s\":\n",
	       nbparts, parent->name);

	for (i = 0; i < nbparts; i++) {
		child = allocate_partition(parent, parts + i, i, cur_offset);
		if (IS_ERR(child)) {
			ret = PTR_ERR(child);
			goto err_del_partitions;
		}

		mutex_lock(&master->master.partitions_lock);
		list_add_tail(&child->part.node, &parent->partitions);
		mutex_unlock(&master->master.partitions_lock);

		ret = add_mtd_device(child);
		if (ret) {
			mutex_lock(&master->master.partitions_lock);
			list_del(&child->part.node);
			mutex_unlock(&master->master.partitions_lock);

			free_partition(child);
			goto err_del_partitions;
		}

		mtd_add_partition_attrs(child);

		/* Look for subpartitions */
		parse_mtd_partitions(child, parts[i].types, NULL);

		cur_offset = child->part.offset + child->part.size;
	}

	return 0;

err_del_partitions:
	del_mtd_partitions(master);

	return ret;
}

static DEFINE_SPINLOCK(part_parser_lock);
static LIST_HEAD(part_parsers);

static struct mtd_part_parser *mtd_part_parser_get(const char *name)
{
	struct mtd_part_parser *p, *ret = NULL;

	spin_lock(&part_parser_lock);

	list_for_each_entry(p, &part_parsers, list)
		if (!strcmp(p->name, name) && try_module_get(p->owner)) {
			ret = p;
			break;
		}

	spin_unlock(&part_parser_lock);

	return ret;
}

static inline void mtd_part_parser_put(const struct mtd_part_parser *p)
{
	module_put(p->owner);
}

/*
 * Many partition parsers just expected the core to kfree() all their data in
 * one chunk. Do that by default.
 */
static void mtd_part_parser_cleanup_default(const struct mtd_partition *pparts,
					    int nr_parts)
{
	kfree(pparts);
}

int __register_mtd_parser(struct mtd_part_parser *p, struct module *owner)
{
	p->owner = owner;

	if (!p->cleanup)
		p->cleanup = &mtd_part_parser_cleanup_default;

	spin_lock(&part_parser_lock);
	list_add(&p->list, &part_parsers);
	spin_unlock(&part_parser_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(__register_mtd_parser);

void deregister_mtd_parser(struct mtd_part_parser *p)
{
	spin_lock(&part_parser_lock);
	list_del(&p->list);
	spin_unlock(&part_parser_lock);
}
EXPORT_SYMBOL_GPL(deregister_mtd_parser);

/*
 * Do not forget to update 'parse_mtd_partitions()' kerneldoc comment if you
 * are changing this array!
 */
static const char * const default_mtd_part_types[] = {
	"cmdlinepart",
	"ofpart",
	NULL
};

/* Check DT only when looking for subpartitions. */
static const char * const default_subpartition_types[] = {
	"ofpart",
	NULL
};

static int mtd_part_do_parse(struct mtd_part_parser *parser,
			     struct mtd_info *master,
			     struct mtd_partitions *pparts,
			     struct mtd_part_parser_data *data)
{
	int ret;

	ret = (*parser->parse_fn)(master, &pparts->parts, data);
	pr_debug("%s: parser %s: %i\n", master->name, parser->name, ret);
	if (ret <= 0)
		return ret;

	pr_notice("%d %s partitions found on MTD device %s\n", ret,
		  parser->name, master->name);

	pparts->nr_parts = ret;
	pparts->parser = parser;

	return ret;
}

/**
 * mtd_part_get_compatible_parser - find MTD parser by a compatible string
 *
 * @compat: compatible string describing partitions in a device tree
 *
 * MTD parsers can specify supported partitions by providing a table of
 * compatibility strings. This function finds a parser that advertises support
 * for a passed value of "compatible".
 */
static struct mtd_part_parser *mtd_part_get_compatible_parser(const char *compat)
{
	struct mtd_part_parser *p, *ret = NULL;

	spin_lock(&part_parser_lock);

	list_for_each_entry(p, &part_parsers, list) {
		const struct of_device_id *matches;

		matches = p->of_match_table;
		if (!matches)
			continue;

		for (; matches->compatible[0]; matches++) {
			if (!strcmp(matches->compatible, compat) &&
			    try_module_get(p->owner)) {
				ret = p;
				break;
			}
		}

		if (ret)
			break;
	}

	spin_unlock(&part_parser_lock);

	return ret;
}

static int mtd_part_of_parse(struct mtd_info *master,
			     struct mtd_partitions *pparts)
{
	struct mtd_part_parser *parser;
	struct device_node *np;
	struct property *prop;
	const char *compat;
	const char *fixed = "fixed-partitions";
	int ret, err = 0;

	np = mtd_get_of_node(master);
	if (mtd_is_partition(master))
		of_node_get(np);
	else
		np = of_get_child_by_name(np, "partitions");

	of_property_for_each_string(np, "compatible", prop, compat) {
		parser = mtd_part_get_compatible_parser(compat);
		if (!parser)
			continue;
		ret = mtd_part_do_parse(parser, master, pparts, NULL);
		if (ret > 0) {
			of_node_put(np);
			return ret;
		}
		mtd_part_parser_put(parser);
		if (ret < 0 && !err)
			err = ret;
	}
	of_node_put(np);

	/*
	 * For backward compatibility we have to try the "fixed-partitions"
	 * parser. It supports old DT format with partitions specified as a
	 * direct subnodes of a flash device DT node without any compatibility
	 * specified we could match.
	 */
	parser = mtd_part_parser_get(fixed);
	if (!parser && !request_module("%s", fixed))
		parser = mtd_part_parser_get(fixed);
	if (parser) {
		ret = mtd_part_do_parse(parser, master, pparts, NULL);
		if (ret > 0)
			return ret;
		mtd_part_parser_put(parser);
		if (ret < 0 && !err)
			err = ret;
	}

	return err;
}

/**
 * parse_mtd_partitions - parse and register MTD partitions
 *
 * @master: the master partition (describes whole MTD device)
 * @types: names of partition parsers to try or %NULL
 * @data: MTD partition parser-specific data
 *
 * This function tries to find & register partitions on MTD device @master. It
 * uses MTD partition parsers, specified in @types. However, if @types is %NULL,
 * then the default list of parsers is used. The default list contains only the
 * "cmdlinepart" and "ofpart" parsers ATM.
 * Note: If there are more then one parser in @types, the kernel only takes the
 * partitions parsed out by the first parser.
 *
 * This function may return:
 * o a negative error code in case of failure
 * o number of found partitions otherwise
 */
int parse_mtd_partitions(struct mtd_info *master, const char *const *types,
			 struct mtd_part_parser_data *data)
{
	struct mtd_partitions pparts = { };
	struct mtd_part_parser *parser;
	int ret, err = 0;

	if (!types)
		types = mtd_is_partition(master) ? default_subpartition_types :
			default_mtd_part_types;

	for ( ; *types; types++) {
		/*
		 * ofpart is a special type that means OF partitioning info
		 * should be used. It requires a bit different logic so it is
		 * handled in a separated function.
		 */
		if (!strcmp(*types, "ofpart")) {
			ret = mtd_part_of_parse(master, &pparts);
		} else {
			pr_debug("%s: parsing partitions %s\n", master->name,
				 *types);
			parser = mtd_part_parser_get(*types);
			if (!parser && !request_module("%s", *types))
				parser = mtd_part_parser_get(*types);
			pr_debug("%s: got parser %s\n", master->name,
				parser ? parser->name : NULL);
			if (!parser)
				continue;
			ret = mtd_part_do_parse(parser, master, &pparts, data);
			if (ret <= 0)
				mtd_part_parser_put(parser);
		}
		/* Found partitions! */
		if (ret > 0) {
			err = add_mtd_partitions(master, pparts.parts,
						 pparts.nr_parts);
			mtd_part_parser_cleanup(&pparts);
			return err ? err : pparts.nr_parts;
		}
		/*
		 * Stash the first error we see; only report it if no parser
		 * succeeds
		 */
		if (ret < 0 && !err)
			err = ret;
	}
	return err;
}

void mtd_part_parser_cleanup(struct mtd_partitions *parts)
{
	const struct mtd_part_parser *parser;

	if (!parts)
		return;

	parser = parts->parser;
	if (parser) {
		if (parser->cleanup)
			parser->cleanup(parts->parts, parts->nr_parts);

		mtd_part_parser_put(parser);
	}
}

/* Returns the size of the entire flash chip */
uint64_t mtd_get_device_size(const struct mtd_info *mtd)
{
	struct mtd_info *master = mtd_get_master((struct mtd_info *)mtd);

	return master->size;
}
EXPORT_SYMBOL_GPL(mtd_get_device_size);

#ifdef MY_ABC_HERE
int SYNOMTDModifyPartInfo(struct mtd_info *mtd, unsigned long offset, unsigned long length)
{
	struct mtd_info *master = mtd_get_master((struct mtd_info *)mtd);

	mtd->part.offset = offset;
	mtd->part.offset &= master->size-1;

	mtd->part.size = length;

	if (mtd->part.offset + mtd->part.size > master->size) {
		return -EFAULT;
	}

	return 0;
}
#endif /* MY_ABC_HERE */
