#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2000-2021 Synology Inc.
 */
#ifndef _LIBMD_REPORT_H
#define _LIBMD_REPORT_H

#ifdef MY_ABC_HERE
extern int (*funcSYNOSendRaidEvent)(unsigned int type, unsigned int raidno,
				    unsigned int diskno, unsigned long long sector);

void syno_report_bad_sector(sector_t sector, unsigned long rw,
			    int md_minor, struct block_device *bdev, const char *func_name);

void syno_report_uncorrected_bad_sector(sector_t sector, int md_minor,
					struct block_device *bdev, const char *func_name);

void syno_report_correct_bad_sector(sector_t sector, int md_minor,
				    struct block_device *bdev, const char *func_name);

void syno_report_faulty_device(int md_minor, struct block_device *bdev);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
extern int (*funcSYNOSendAutoRemapRaidEvent)(unsigned int, unsigned long long, unsigned int);
#endif /* MY_ABC_HERE */
#endif /* _LIBMD_REPORT_H */

