#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef _UAPI_LINUX_BTRFS_H
#define _UAPI_LINUX_BTRFS_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define BTRFS_IOCTL_MAGIC 0x94
#define BTRFS_VOL_NAME_MAX 255
#define BTRFS_LABEL_SIZE 256

/* this should be 4k */
#define BTRFS_PATH_NAME_MAX 4087
struct btrfs_ioctl_vol_args {
	__s64 fd;
	char name[BTRFS_PATH_NAME_MAX + 1];
};

#define BTRFS_DEVICE_PATH_NAME_MAX	1024
#define BTRFS_SUBVOL_NAME_MAX 		4039

#ifndef __KERNEL__
/* Deprecated since 5.7 */
# define BTRFS_SUBVOL_CREATE_ASYNC	(1ULL << 0)
#endif
#define BTRFS_SUBVOL_RDONLY		(1ULL << 1)
#define BTRFS_SUBVOL_QGROUP_INHERIT	(1ULL << 2)

#define BTRFS_DEVICE_SPEC_BY_ID		(1ULL << 3)

#define BTRFS_SUBVOL_SPEC_BY_ID	(1ULL << 4)
#ifdef MY_ABC_HERE
#define BTRFS_SUBVOL_HIDE		(1ULL << 32)
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
#define BTRFS_SUBVOL_NOLOAD_USRQUOTA	(1ULL << 33)
#define BTRFS_SUBVOL_CMPR_RATIO         (1ULL << 34)
#define BTRFS_SUBVOL_DISABLE_QUOTA	(1ULL << 35)
#endif /* MY_ABC_HERE */

#define BTRFS_VOL_ARG_V2_FLAGS_SUPPORTED		\
			(BTRFS_SUBVOL_RDONLY |		\
			BTRFS_SUBVOL_QGROUP_INHERIT |	\
			BTRFS_DEVICE_SPEC_BY_ID |	\
			BTRFS_SUBVOL_SPEC_BY_ID)

#define BTRFS_FSID_SIZE 16
#define BTRFS_UUID_SIZE 16
#define BTRFS_UUID_UNPARSED_SIZE	37

/*
 * flags definition for qgroup limits
 *
 * Used by:
 * struct btrfs_qgroup_limit.flags
 * struct btrfs_qgroup_limit_item.flags
 */
#define BTRFS_QGROUP_LIMIT_MAX_RFER	(1ULL << 0)
#define BTRFS_QGROUP_LIMIT_MAX_EXCL	(1ULL << 1)
#define BTRFS_QGROUP_LIMIT_RSV_RFER	(1ULL << 2)
#define BTRFS_QGROUP_LIMIT_RSV_EXCL	(1ULL << 3)
#ifdef MY_ABC_HERE
#define BTRFS_QGROUP_LIMIT_SOFT_RFER	(1ULL << 2)
#define BTRFS_QGROUP_LIMIT_SOFT_EXCL	(1ULL << 3)
#endif /* MY_ABC_HERE */
#define BTRFS_QGROUP_LIMIT_RFER_CMPR	(1ULL << 4)
#define BTRFS_QGROUP_LIMIT_EXCL_CMPR	(1ULL << 5)

struct btrfs_qgroup_limit {
	__u64	flags;
	__u64	max_rfer;
	__u64	max_excl;
#ifdef MY_ABC_HERE
	union {
		__u64	rsv_rfer;
		__u64   soft_rfer;
	};
	union {
		__u64	rsv_excl;
		__u64   soft_excl;
	};
#else
	__u64	rsv_rfer;
	__u64	rsv_excl;
#endif /* MY_ABC_HERE */
};

/*
 * flags definition for qgroup inheritance
 *
 * Used by:
 * struct btrfs_qgroup_inherit.flags
 */
#define BTRFS_QGROUP_INHERIT_SET_LIMITS	(1ULL << 0)

struct btrfs_qgroup_inherit {
	__u64	flags;
	__u64	num_qgroups;
	__u64	num_ref_copies;
	__u64	num_excl_copies;
	struct btrfs_qgroup_limit lim;
	__u64	qgroups[0];
};

struct btrfs_ioctl_qgroup_limit_args {
	__u64	qgroupid;
	struct btrfs_qgroup_limit lim;
};

/*
 * Arguments for specification of subvolumes or devices, supporting by-name or
 * by-id and flags
 *
 * The set of supported flags depends on the ioctl
 *
 * BTRFS_SUBVOL_RDONLY is also provided/consumed by the following ioctls:
 * - BTRFS_IOC_SUBVOL_GETFLAGS
 * - BTRFS_IOC_SUBVOL_SETFLAGS
 */

/* Supported flags for BTRFS_IOC_RM_DEV_V2 */
#define BTRFS_DEVICE_REMOVE_ARGS_MASK					\
	(BTRFS_DEVICE_SPEC_BY_ID)

/* Supported flags for BTRFS_IOC_SNAP_CREATE_V2 and BTRFS_IOC_SUBVOL_CREATE_V2 */
#define BTRFS_SUBVOL_CREATE_ARGS_MASK					\
	 (BTRFS_SUBVOL_RDONLY |						\
	 BTRFS_SUBVOL_QGROUP_INHERIT)

/* Supported flags for BTRFS_IOC_SNAP_DESTROY_V2 */
#define BTRFS_SUBVOL_DELETE_ARGS_MASK					\
	(BTRFS_SUBVOL_SPEC_BY_ID)

struct btrfs_ioctl_vol_args_v2 {
	__s64 fd;
	__u64 transid;
	__u64 flags;
	union {
		struct {
			__u64 size;
			struct btrfs_qgroup_inherit __user *qgroup_inherit;
		};
#ifdef MY_ABC_HERE
		struct {
			__u64 padding[3];
			__u64 copy_limit_from;
		};
#endif /* MY_ABC_HERE */
		__u64 unused[4];
	};
	union {
		char name[BTRFS_SUBVOL_NAME_MAX + 1];
		__u64 devid;
		__u64 subvolid;
	};
};

/*
 * structure to report errors and progress to userspace, either as a
 * result of a finished scrub, a canceled scrub or a progress inquiry
 */
struct btrfs_scrub_progress {
	__u64 data_extents_scrubbed;	/* # of data extents scrubbed */
	__u64 tree_extents_scrubbed;	/* # of tree extents scrubbed */
	__u64 data_bytes_scrubbed;	/* # of data bytes scrubbed */
	__u64 tree_bytes_scrubbed;	/* # of tree bytes scrubbed */
	__u64 read_errors;		/* # of read errors encountered (EIO) */
	__u64 csum_errors;		/* # of failed csum checks */
	__u64 verify_errors;		/* # of occurences, where the metadata
					 * of a tree block did not match the
					 * expected values, like generation or
					 * logical */
	__u64 no_csum;			/* # of 4k data block for which no csum
					 * is present, probably the result of
					 * data written with nodatasum */
	__u64 csum_discards;		/* # of csum for which no data was found
					 * in the extent tree. */
	__u64 super_errors;		/* # of bad super blocks encountered */
	__u64 malloc_errors;		/* # of internal kmalloc errors. These
					 * will likely cause an incomplete
					 * scrub */
	__u64 uncorrectable_errors;	/* # of errors where either no intact
					 * copy was found or the writeback
					 * failed */
	__u64 corrected_errors;		/* # of errors corrected */
	__u64 last_physical;		/* last physical address scrubbed. In
					 * case a scrub was aborted, this can
					 * be used to restart the scrub */
	__u64 unverified_errors;	/* # of occurences where a read for a
					 * full (64k) bio failed, but the re-
					 * check succeeded for each 4k piece.
					 * Intermittent error. */
};

#define BTRFS_SCRUB_READONLY	1
struct btrfs_ioctl_scrub_args {
	__u64 devid;				/* in */
	__u64 start;				/* in */
	__u64 end;				/* in */
	__u64 flags;				/* in */
	struct btrfs_scrub_progress progress;	/* out */
	/* pad to 1k */
	__u64 unused[(1024-32-sizeof(struct btrfs_scrub_progress))/8];
};

#define BTRFS_IOCTL_DEV_REPLACE_CONT_READING_FROM_SRCDEV_MODE_ALWAYS	0
#define BTRFS_IOCTL_DEV_REPLACE_CONT_READING_FROM_SRCDEV_MODE_AVOID	1
struct btrfs_ioctl_dev_replace_start_params {
	__u64 srcdevid;	/* in, if 0, use srcdev_name instead */
	__u64 cont_reading_from_srcdev_mode;	/* in, see #define
						 * above */
	__u8 srcdev_name[BTRFS_DEVICE_PATH_NAME_MAX + 1];	/* in */
	__u8 tgtdev_name[BTRFS_DEVICE_PATH_NAME_MAX + 1];	/* in */
};

#define BTRFS_IOCTL_DEV_REPLACE_STATE_NEVER_STARTED	0
#define BTRFS_IOCTL_DEV_REPLACE_STATE_STARTED		1
#define BTRFS_IOCTL_DEV_REPLACE_STATE_FINISHED		2
#define BTRFS_IOCTL_DEV_REPLACE_STATE_CANCELED		3
#define BTRFS_IOCTL_DEV_REPLACE_STATE_SUSPENDED		4
struct btrfs_ioctl_dev_replace_status_params {
	__u64 replace_state;	/* out, see #define above */
	__u64 progress_1000;	/* out, 0 <= x <= 1000 */
	__u64 time_started;	/* out, seconds since 1-Jan-1970 */
	__u64 time_stopped;	/* out, seconds since 1-Jan-1970 */
	__u64 num_write_errors;	/* out */
	__u64 num_uncorrectable_read_errors;	/* out */
};

#define BTRFS_IOCTL_DEV_REPLACE_CMD_START			0
#define BTRFS_IOCTL_DEV_REPLACE_CMD_STATUS			1
#define BTRFS_IOCTL_DEV_REPLACE_CMD_CANCEL			2
#define BTRFS_IOCTL_DEV_REPLACE_RESULT_NO_ERROR			0
#define BTRFS_IOCTL_DEV_REPLACE_RESULT_NOT_STARTED		1
#define BTRFS_IOCTL_DEV_REPLACE_RESULT_ALREADY_STARTED		2
#define BTRFS_IOCTL_DEV_REPLACE_RESULT_SCRUB_INPROGRESS		3
struct btrfs_ioctl_dev_replace_args {
	__u64 cmd;	/* in */
	__u64 result;	/* out */

	union {
		struct btrfs_ioctl_dev_replace_start_params start;
		struct btrfs_ioctl_dev_replace_status_params status;
	};	/* in/out */

	__u64 spare[64];
};

struct btrfs_ioctl_dev_info_args {
	__u64 devid;				/* in/out */
	__u8 uuid[BTRFS_UUID_SIZE];		/* in/out */
	__u64 bytes_used;			/* out */
	__u64 total_bytes;			/* out */
	__u64 unused[379];			/* pad to 4k */
	__u8 path[BTRFS_DEVICE_PATH_NAME_MAX];	/* out */
};

/*
 * Retrieve information about the filesystem
 */

/* Request information about checksum type and size */
#define BTRFS_FS_INFO_FLAG_CSUM_INFO			(1 << 0)

/* Request information about filesystem generation */
#define BTRFS_FS_INFO_FLAG_GENERATION			(1 << 1)
/* Request information about filesystem metadata UUID */
#define BTRFS_FS_INFO_FLAG_METADATA_UUID		(1 << 2)

struct btrfs_ioctl_fs_info_args {
	__u64 max_id;				/* out */
	__u64 num_devices;			/* out */
	__u8 fsid[BTRFS_FSID_SIZE];		/* out */
	__u32 nodesize;				/* out */
	__u32 sectorsize;			/* out */
	__u32 clone_alignment;			/* out */
	/* See BTRFS_FS_INFO_FLAG_* */
	__u16 csum_type;			/* out */
	__u16 csum_size;			/* out */
	__u64 flags;				/* in/out */
	__u64 generation;			/* out */
	__u8 metadata_uuid[BTRFS_FSID_SIZE];	/* out */
	__u8 reserved[944];			/* pad to 1k */
};

/*
 * feature flags
 *
 * Used by:
 * struct btrfs_ioctl_feature_flags
 */
#define BTRFS_FEATURE_COMPAT_RO_FREE_SPACE_TREE		(1ULL << 0)
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
#define BTRFS_FEATURE_COMPAT_BLOCK_GROUP_CACHE_TREE_AUTO    (1ULL << 61)
#define BTRFS_FEATURE_COMPAT_BLOCK_GROUP_CACHE_TREE (1ULL << 62)
#define BTRFS_FEATURE_COMPAT_SYNO_CASELESS  (1ULL << 63)
#endif /* MY_ABC_HERE || MY_ABC_HERE */
/*
 * Older kernels (< 4.9) on big-endian systems produced broken free space tree
 * bitmaps, and btrfs-progs also used to corrupt the free space tree (versions
 * < 4.7.3).  If this bit is clear, then the free space tree cannot be trusted.
 * btrfs-progs can also intentionally clear this bit to ask the kernel to
 * rebuild the free space tree, however this might not work on older kernels
 * that do not know about this bit. If not sure, clear the cache manually on
 * first mount when booting older kernel versions.
 */
#define BTRFS_FEATURE_COMPAT_RO_FREE_SPACE_TREE_VALID	(1ULL << 1)

#ifdef MY_ABC_HERE
#define BTRFS_FEATURE_COMPAT_RO_LOCKER		(1ULL << 59)	    // the 17th prime number
#endif /* MY_ABC_HERE */

#define BTRFS_FEATURE_INCOMPAT_MIXED_BACKREF	(1ULL << 0)
#define BTRFS_FEATURE_INCOMPAT_DEFAULT_SUBVOL	(1ULL << 1)
#define BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS	(1ULL << 2)
#define BTRFS_FEATURE_INCOMPAT_COMPRESS_LZO	(1ULL << 3)
#define BTRFS_FEATURE_INCOMPAT_COMPRESS_ZSTD	(1ULL << 4)

/*
 * older kernels tried to do bigger metadata blocks, but the
 * code was pretty buggy.  Lets not let them try anymore.
 */
#define BTRFS_FEATURE_INCOMPAT_BIG_METADATA	(1ULL << 5)

#define BTRFS_FEATURE_INCOMPAT_EXTENDED_IREF	(1ULL << 6)
#define BTRFS_FEATURE_INCOMPAT_RAID56		(1ULL << 7)
#define BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA	(1ULL << 8)
#define BTRFS_FEATURE_INCOMPAT_NO_HOLES		(1ULL << 9)
#define BTRFS_FEATURE_INCOMPAT_METADATA_UUID	(1ULL << 10)
#define BTRFS_FEATURE_INCOMPAT_RAID1C34		(1ULL << 11)

#ifdef MY_ABC_HERE
#define BTRFS_FEATURE_SYNO_CAPABILITY_RBD_META	(1ULL << 0)
#endif /* MY_ABC_HERE */

struct btrfs_ioctl_feature_flags {
	__u64 compat_flags;
	__u64 compat_ro_flags;
	__u64 incompat_flags;
};

#ifdef MY_ABC_HERE
#define BTRFS_LOCKER_CLOCK		(1ULL << 0)
#define BTRFS_LOCKER_CLOCK_DELTA        (1ULL << 1)
#define BTRFS_LOCKER_ENABLED		(1ULL << 2)
#define BTRFS_LOCKER_MODE		(1ULL << 3)
#define BTRFS_LOCKER_DEFAULT_STATE	(1ULL << 4)
#define BTRFS_LOCKER_WAITTIME		(1ULL << 5)
#define BTRFS_LOCKER_DURATION		(1ULL << 6)
#define BTRFS_LOCKER_CLOCK_ADJUSTMENT	(1ULL << 7)
#define BTRFS_LOCKER_UPDATE_TIME_FLOOR	(1ULL << 8)
#define BTRFS_LOCKER_LOCKABLE		(1ULL << 9)
#define BTRFS_LOCKER_STATE		(1ULL << 10)
#define BTRFS_LOCKER_RAW_STATE		(1ULL << 11)
#define BTRFS_LOCKER_UPDATE_TIME	(1ULL << 12)
#define BTRFS_LOCKER_BEGIN		(1ULL << 13)
#define BTRFS_LOCKER_END		(1ULL << 14)
#define BTRFS_LOCKER_END_EXT_BEGIN	(1ULL << 15)
#define BTRFS_LOCKER_END_EXT_END	(1ULL << 16)
#define BTRFS_LOCKER_END_EXT_CURRENT	(1ULL << 17)

#define BTRFS_LOCKER_FS_PROP_MASK	(\
	BTRFS_LOCKER_CLOCK		|\
	BTRFS_LOCKER_CLOCK_DELTA	 \
)

#define BTRFS_LOCKER_ROOT_PROP_MASK	(\
	BTRFS_LOCKER_ENABLED		|\
	BTRFS_LOCKER_MODE		|\
	BTRFS_LOCKER_DEFAULT_STATE	|\
	BTRFS_LOCKER_WAITTIME		|\
	BTRFS_LOCKER_DURATION		|\
	BTRFS_LOCKER_CLOCK_ADJUSTMENT	|\
	BTRFS_LOCKER_UPDATE_TIME_FLOOR	 \
)

#define BTRFS_LOCKER_PERIOD_MASK        (\
	BTRFS_LOCKER_BEGIN		|\
	BTRFS_LOCKER_END		|\
	BTRFS_LOCKER_END_EXT_BEGIN	|\
	BTRFS_LOCKER_END_EXT_END	|\
	BTRFS_LOCKER_END_EXT_CURRENT	 \
)

#define BTRFS_LOCKER_INODE_PROP_MASK	(\
	BTRFS_LOCKER_LOCKABLE		|\
	BTRFS_LOCKER_STATE		|\
	BTRFS_LOCKER_RAW_STATE		|\
	BTRFS_LOCKER_UPDATE_TIME	|\
	BTRFS_LOCKER_PERIOD_MASK	 \
)

#define BTRFS_LOCKER_MASK_ALL		(\
	BTRFS_LOCKER_FS_PROP_MASK	|\
	BTRFS_LOCKER_ROOT_PROP_MASK	|\
	BTRFS_LOCKER_INODE_PROP_MASK	 \
)

struct btrfs_ioctl_syno_locker_args {
	__u64 flags;

	/* volume properties */
	__s64 clock;			//  0: [out] epoch time from 1970/01/01
	__s64 clock_delta;		//  1: [out] delta = system_clock - volume_clock

	/* subvolume properties */
	__u16 enabled;			//  2: [in/out]
	__u16 mode;			//  3: [in/out]
	__u16 default_state;		//  4: [in/out]
	__u16 reserved1;

	__s64 waittime;			//  5: [in/out] seconds
	__s64 duration;			//  6: [in/out] seconds
	__s64 clock_adjustment;		//  7: [in/out] seconds
	__s64 update_time_floor;	//  8: [out] epoch time in volume clock

	/* inode properties */
	__u16 lockable;			//  9: [out]
	__u16 state;			// 10: [in/out]
	__u16 raw_state;		// 11: [out]
	__u16 reserved2;

	__s64 update_time;		// 12: [in/out] epoch time in volume clock
	__s64 period_begin;		// 13: [in/out] epoch time in volume clock
	__s64 period_end;		// 14: [in/out] epoch time in volume clock

	__s64 update_time_sys;		// 15: [out] epoch time in system clock
	__s64 period_begin_sys;		// 16: [out] epoch time in system clock
	__s64 period_end_sys;		// 17: [out] epoch time in system clock

	/*
	 * The following flags control the behavior of setting period_end in volume clock.
	 *
	 *  BTRFS_LOCKER_END:
	 *     new period_end = args.period_end as epoch time
	 *  BTRFS_LOCKER_END_EXT_BEGIN:
	 *     new period_end = current period_begin + args.period_end
	 *  BTRFS_LOCKER_END_EXT_END
	 *     new period_end = current period_end + args.period_end
	 *  BTRFS_LOCKER_END_EXT_CURRENT
	 *     new period_end = current volume_clock + args.period_end
	 */

	__u64 reserved[5];
};
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
struct btrfs_ioctl_syno_flags {
	__u64 syno_capability_flags;
	__u64 reserved[3];
};
#endif /* MY_ABC_HERE */

/* balance control ioctl modes */
#define BTRFS_BALANCE_CTL_PAUSE		1
#define BTRFS_BALANCE_CTL_CANCEL	2

/*
 * this is packed, because it should be exactly the same as its disk
 * byte order counterpart (struct btrfs_disk_balance_args)
 */
struct btrfs_balance_args {
	__u64 profiles;
	union {
		__u64 usage;
		struct {
			__u32 usage_min;
			__u32 usage_max;
		};
	};
	__u64 devid;
	__u64 pstart;
	__u64 pend;
	__u64 vstart;
	__u64 vend;

	__u64 target;

	__u64 flags;

	/*
	 * BTRFS_BALANCE_ARGS_LIMIT with value 'limit'
	 * BTRFS_BALANCE_ARGS_LIMIT_RANGE - the extend version can use minimum
	 * and maximum
	 */
	union {
		__u64 limit;		/* limit number of processed chunks */
		struct {
			__u32 limit_min;
			__u32 limit_max;
		};
	};

	/*
	 * Process chunks that cross stripes_min..stripes_max devices,
	 * BTRFS_BALANCE_ARGS_STRIPES_RANGE
	 */
	__u32 stripes_min;
	__u32 stripes_max;

	__u64 unused[6];
} __attribute__ ((__packed__));

/* report balance progress to userspace */
struct btrfs_balance_progress {
	__u64 expected;		/* estimated # of chunks that will be
				 * relocated to fulfill the request */
	__u64 considered;	/* # of chunks we have considered so far */
	__u64 completed;	/* # of chunks relocated so far */
};

/*
 * flags definition for balance
 *
 * Restriper's general type filter
 *
 * Used by:
 * btrfs_ioctl_balance_args.flags
 * btrfs_balance_control.flags (internal)
 */
#define BTRFS_BALANCE_DATA		(1ULL << 0)
#define BTRFS_BALANCE_SYSTEM		(1ULL << 1)
#define BTRFS_BALANCE_METADATA		(1ULL << 2)

#define BTRFS_BALANCE_TYPE_MASK		(BTRFS_BALANCE_DATA |	    \
					 BTRFS_BALANCE_SYSTEM |	    \
					 BTRFS_BALANCE_METADATA)

#define BTRFS_BALANCE_FORCE		(1ULL << 3)
#define BTRFS_BALANCE_RESUME		(1ULL << 4)
#ifdef MY_ABC_HERE
#define BTRFS_BALANCE_DRY_RUN          (1ULL << 15)
#endif /* SYNO_BTRFS_BALANCE_DRY_RUN */

/*
 * flags definitions for per-type balance args
 *
 * Balance filters
 *
 * Used by:
 * struct btrfs_balance_args
 */
#define BTRFS_BALANCE_ARGS_PROFILES	(1ULL << 0)
#define BTRFS_BALANCE_ARGS_USAGE	(1ULL << 1)
#define BTRFS_BALANCE_ARGS_DEVID	(1ULL << 2)
#define BTRFS_BALANCE_ARGS_DRANGE	(1ULL << 3)
#define BTRFS_BALANCE_ARGS_VRANGE	(1ULL << 4)
#define BTRFS_BALANCE_ARGS_LIMIT	(1ULL << 5)
#define BTRFS_BALANCE_ARGS_LIMIT_RANGE	(1ULL << 6)
#define BTRFS_BALANCE_ARGS_STRIPES_RANGE (1ULL << 7)
#define BTRFS_BALANCE_ARGS_USAGE_RANGE	(1ULL << 10)

#define BTRFS_BALANCE_ARGS_MASK			\
	(BTRFS_BALANCE_ARGS_PROFILES |		\
	 BTRFS_BALANCE_ARGS_USAGE |		\
	 BTRFS_BALANCE_ARGS_DEVID | 		\
	 BTRFS_BALANCE_ARGS_DRANGE |		\
	 BTRFS_BALANCE_ARGS_VRANGE |		\
	 BTRFS_BALANCE_ARGS_LIMIT |		\
	 BTRFS_BALANCE_ARGS_LIMIT_RANGE |	\
	 BTRFS_BALANCE_ARGS_STRIPES_RANGE |	\
	 BTRFS_BALANCE_ARGS_USAGE_RANGE)

/*
 * Profile changing flags.  When SOFT is set we won't relocate chunk if
 * it already has the target profile (even though it may be
 * half-filled).
 */
#define BTRFS_BALANCE_ARGS_CONVERT	(1ULL << 8)
#define BTRFS_BALANCE_ARGS_SOFT		(1ULL << 9)


/*
 * flags definition for balance state
 *
 * Used by:
 * struct btrfs_ioctl_balance_args.state
 */
#define BTRFS_BALANCE_STATE_RUNNING	(1ULL << 0)
#define BTRFS_BALANCE_STATE_PAUSE_REQ	(1ULL << 1)
#define BTRFS_BALANCE_STATE_CANCEL_REQ	(1ULL << 2)

struct btrfs_ioctl_balance_args {
	__u64 flags;				/* in/out */
	__u64 state;				/* out */

	struct btrfs_balance_args data;		/* in/out */
	struct btrfs_balance_args meta;		/* in/out */
	struct btrfs_balance_args sys;		/* in/out */

	struct btrfs_balance_progress stat;	/* out */

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	__u64 unused[70];			/* pad to 1k */
	__u64 key_offset;				/* out */ /* for quick balance */
	__u64 total_chunk_used;			/* out */ /* for dry run */
#else
	__u64 unused[72];			/* pad to 1k */
#endif /* defined(MY_ABC_HERE) || defined(MY_ABC_HERE) */
};

#define BTRFS_INO_LOOKUP_PATH_MAX 4080
struct btrfs_ioctl_ino_lookup_args {
	__u64 treeid;
	__u64 objectid;
	char name[BTRFS_INO_LOOKUP_PATH_MAX];
};

#define BTRFS_INO_LOOKUP_USER_PATH_MAX (4080 - BTRFS_VOL_NAME_MAX - 1)
struct btrfs_ioctl_ino_lookup_user_args {
	/* in, inode number containing the subvolume of 'subvolid' */
	__u64 dirid;
	/* in */
	__u64 treeid;
	/* out, name of the subvolume of 'treeid' */
	char name[BTRFS_VOL_NAME_MAX + 1];
	/*
	 * out, constructed path from the directory with which the ioctl is
	 * called to dirid
	 */
	char path[BTRFS_INO_LOOKUP_USER_PATH_MAX];
};

#ifdef MY_DEF_HERE
#define BTRFS_SEARCH_FLAG_ADJUST_MIN (1 << 0)
#define BTRFS_SEARCH_FLAG_REPORT_BUF_FULL (1 << 1)
#define BTRFS_SEARCH_FLAG_READAHEAD (1 << 2)
#endif /* MY_DEF_HERE */

/* Search criteria for the btrfs SEARCH ioctl family. */
struct btrfs_ioctl_search_key {
	/*
	 * The tree we're searching in. 1 is the tree of tree roots, 2 is the
	 * extent tree, etc...
	 *
	 * A special tree_id value of 0 will cause a search in the subvolume
	 * tree that the inode which is passed to the ioctl is part of.
	 */
	__u64 tree_id;		/* in */

	/*
	 * When doing a tree search, we're actually taking a slice from a
	 * linear search space of 136-bit keys.
	 *
	 * A full 136-bit tree key is composed as:
	 *   (objectid << 72) + (type << 64) + offset
	 *
	 * The individual min and max values for objectid, type and offset
	 * define the min_key and max_key values for the search range. All
	 * metadata items with a key in the interval [min_key, max_key] will be
	 * returned.
	 *
	 * Additionally, we can filter the items returned on transaction id of
	 * the metadata block they're stored in by specifying a transid range.
	 * Be aware that this transaction id only denotes when the metadata
	 * page that currently contains the item got written the last time as
	 * result of a COW operation.  The number does not have any meaning
	 * related to the transaction in which an individual item that is being
	 * returned was created or changed.
	 */
	__u64 min_objectid;	/* in */
	__u64 max_objectid;	/* in */
	__u64 min_offset;	/* in */
	__u64 max_offset;	/* in */
	__u64 min_transid;	/* in */
	__u64 max_transid;	/* in */
	__u32 min_type;		/* in */
	__u32 max_type;		/* in */

	/*
	 * input: The maximum amount of results desired.
	 * output: The actual amount of items returned, restricted by any of:
	 *  - reaching the upper bound of the search range
	 *  - reaching the input nr_items amount of items
	 *  - completely filling the supplied memory buffer
	 */
	__u32 nr_items;		/* in/out */

	/* align to 64 bits */
	__u32 unused;

	/* some extra for later */
#ifdef MY_DEF_HERE
	__u32 search_flag;
	__u32 unused1;
#else /* MY_DEF_HERE */
	__u64 unused1;
#endif /* MY_DEF_HERE */
	__u64 unused2;
	__u64 unused3;
	__u64 unused4;
};

struct btrfs_ioctl_search_header {
	__u64 transid;
	__u64 objectid;
	__u64 offset;
	__u32 type;
	__u32 len;
};

#define BTRFS_SEARCH_ARGS_BUFSIZE (4096 - sizeof(struct btrfs_ioctl_search_key))
/*
 * the buf is an array of search headers where
 * each header is followed by the actual item
 * the type field is expanded to 32 bits for alignment
 */
struct btrfs_ioctl_search_args {
	struct btrfs_ioctl_search_key key;
	char buf[BTRFS_SEARCH_ARGS_BUFSIZE];
};

struct btrfs_ioctl_search_args_v2 {
	struct btrfs_ioctl_search_key key; /* in/out - search parameters */
	__u64 buf_size;		   /* in - size of buffer
					    * out - on EOVERFLOW: needed size
					    *       to store item */
	__u64 buf[0];                       /* out - found items */
};

struct btrfs_ioctl_clone_range_args {
  __s64 src_fd;
  __u64 src_offset, src_length;
  __u64 dest_offset;
};

#ifdef MY_ABC_HERE
/*
 * The following bit is used by clone_range_args_v2::flag
 * If REWRITE_SRC or REWRITE_DST is specified, clone will rewrite SRC or
 * DST automatically rather then return EMLINK immediately when refs of
 * EXTENT_ITEM reaches ref_limit. The extent item pointed by destination
 * file will thus has low reference count.
 */
#define BTRFS_CLONE_RANGE_V2_AUTO_REWRITE_SRC (1 << 0)
#define BTRFS_CLONE_RANGE_V2_AUTO_REWRITE_DST (1 << 1)

#ifdef MY_ABC_HERE
#define BTRFS_CLONE_RANGE_V2_SKIP_CHECK_COMPR_DIR (1 << 2)
#endif /* MY_ABC_HERE */

struct btrfs_ioctl_syno_clone_range_args_v2 {
	__s64 src_fd;
	/*
	 * If the ref_limit is reached,
	 * src_offset and src_lenght will be set to appropriate file
	 * offset and lenght to indicate which part of the file reaches
	 * the extent reflink limit. errno EMLINK is returned.
	 * When EMLINK, src_offset and src_length will be set to the
	 * position and lenght of file that could not be cloned,
	 * and ref_limit is set to the current reflinnk count of
	 * that extent.
	 */
	__u64 src_offset;	/* in/out */
	__u64 src_length;	/* in/out */
	__u64 dest_offset;
	__u64 ref_limit;	/* in/out */
	__u32 flag;
	__u32 reserved32;
	__u64 reserved[2];
};
#endif /* MY_ABC_HERE */

/*
 * flags definition for the defrag range ioctl
 *
 * Used by:
 * struct btrfs_ioctl_defrag_range_args.flags
 */
#define BTRFS_DEFRAG_RANGE_COMPRESS 1
#define BTRFS_DEFRAG_RANGE_START_IO 2
#ifdef MY_ABC_HERE
#define BTRFS_DEFRAG_RANGE_SYNO_DEFRAG  (1ULL << 2)
#define BTRFS_DEFRAG_RANGE_PRINT_STDOUT (1ULL << 3)
#define BTRFS_DEFRAG_RANGE_SKIP_FAST_SNAPSHOT_CHECK  (1ULL << 4)
#define BTRFS_DEFRAG_RANGE_START_IO_RANGE  (1ULL << 5)
#define BTRFS_DEFRAG_RANGE_FORCE_RECLAIM  (1ULL << 6)
#define BTRFS_DEFRAG_RANGE_SKIP_CROSS_REF_CHECK  (1ULL << 7)
#endif /* MY_ABC_HERE */
struct btrfs_ioctl_defrag_range_args {
	/* start of the defrag operation */
	__u64 start;

	/* number of bytes to defrag, use (u64)-1 to say all */
	__u64 len;

	/*
	 * flags for the operation, which can include turning
	 * on compression for this one defrag
	 */
	__u64 flags;

	/*
	 * any extent bigger than this will be considered
	 * already defragged.  Use 0 to take the kernel default
	 * Use 1 to say every single extent must be rewritten
	 */
	__u32 extent_thresh;

	/*
	 * which compression method to use if turning on compression
	 * for this defrag operation.  If unspecified, zlib will
	 * be used
	 */
	__u32 compress_type;

#ifdef MY_ABC_HERE
	__u64 release_size;
	/*
	 * This value is multiple of 4K
	 */
	__u16 syno_thresh;
	/*
	 * The ratio between number of bytes of file extent data that
	 * point to processing extent and extent disk byte number.
	 * If actual ratio is below this condition, defrag would be
	 * triggered. For example syno_ratio_denom=3 and syno_ratio_nom=2,
	 * if the (file extent num) <= (extent disk byte num) * 2/3,
	 * this extent would be defragged.
	 */
	__u8 syno_ratio_denom;
	__u8 syno_ratio_nom;
	__u32 unused[1];
#else /* MY_ABC_HERE */
	/* spare for later */
	__u32 unused[4];
#endif /* MY_ABC_HERE */
};


#define BTRFS_SAME_DATA_DIFFERS	1
/* For extent-same ioctl */
struct btrfs_ioctl_same_extent_info {
	__s64 fd;		/* in - destination file */
	__u64 logical_offset;	/* in - start of extent in destination */
	__u64 bytes_deduped;	/* out - total # of bytes we were able
				 * to dedupe from this file */
	/* status of this dedupe operation:
	 * 0 if dedup succeeds
	 * < 0 for error
	 * == BTRFS_SAME_DATA_DIFFERS if data differs
	 */
	__s32 status;		/* out - see above description */
	__u32 reserved;
};

struct btrfs_ioctl_same_args {
	__u64 logical_offset;	/* in - start of extent in source */
	__u64 length;		/* in - length of extent */
	__u16 dest_count;	/* in - total elements in info array */
	__u16 reserved1;
	__u32 reserved2;
	struct btrfs_ioctl_same_extent_info info[0];
};

#ifdef MY_DEF_HERE
enum btrfs_ioctl_syno_dedupe_cmd_action {
	DEDUPE_CMD_NONE = 0,
	DEDUPE_CMD_SET,
	DEDUPE_CMD_CLEAR,
	DEDUPE_CMD_SET_NODEDUPE,
	DEDUPE_CMD_CLEAR_NODEDUPE,
	DEDUPE_CMD_SET_SMALL_EXTENT_SIZE,
	DEDUPE_CMD_SET_INLINE_DEDUPE,
	DEDUPE_CMD_CLEAR_INLINE_DEDUPE,
	DEDUPE_CMD_MAX
};

struct btrfs_ioctl_syno_dedupe_cmd_args {
	__u64 rootid;		/* in */
	__u64 objectid;		/* in */
	__u64 offset;		/* in */
	__u64 len;		/* in */
	__u8  action;		/* in, btrfs_ioctl_syno_dedupe_cmd_action */
};

enum btrfs_ioctl_syno_extent_same_status {
	SYNO_EXTENT_SAME_SUCCESS = 0,
	SYNO_EXTENT_SAME_DITTO,
	SYNO_EXTENT_SAME_DIFF,
	SYNO_EXTENT_SAME_SRC_NOT_FOUND,
	SYNO_EXTENT_SAME_DST_NOT_FOUND,
	SYNO_EXTENT_SAME_MAX,
};

struct btrfs_ioctl_syno_extent_same_args {
	__u64 src_rootid;		/* in */
	__u64 src_objectid;		/* in */
	__u64 src_offset;		/* in */
	__u64 dst_rootid;		/* in */
	__u64 dst_objectid;		/* in */
	__u64 dst_offset;		/* in */
	__u64 length;			/* in */
	__u32 min_dedupe_length;	/* in */
	__u32 backref_limit;		/* in */
	__u64 failed_dst_offset;	/* out */
	__u64 failed_dst_length;	/* out */
	__u64 release_size;		/* out */
	__u8  status;			/* out */
};
#endif /* MY_DEF_HERE */

struct btrfs_ioctl_space_info {
	__u64 flags;
	__u64 total_bytes;
	__u64 used_bytes;
};

struct btrfs_ioctl_space_args {
	__u64 space_slots;
	__u64 total_spaces;
	struct btrfs_ioctl_space_info spaces[0];
};

struct btrfs_data_container {
	__u32	bytes_left;	/* out -- bytes not needed to deliver output */
	__u32	bytes_missing;	/* out -- additional bytes needed for result */
	__u32	elem_cnt;	/* out */
	__u32	elem_missed;	/* out */
	__u64	val[0];		/* out */
};

struct btrfs_ioctl_ino_path_args {
	__u64				inum;		/* in */
	__u64				size;		/* in */
	__u64				reserved[4];
	/* struct btrfs_data_container	*fspath;	   out */
	__u64				fspath;		/* out */
};

struct btrfs_ioctl_logical_ino_args {
	__u64				logical;	/* in */
	__u64				size;		/* in */
	__u64				reserved[3];	/* must be 0 for now */
	__u64				flags;		/* in, v2 only */
	/* struct btrfs_data_container	*inodes;	out   */
	__u64				inodes;
};
/* Return every ref to the extent, not just those containing logical block.
 * Requires logical == extent bytenr. */
#define BTRFS_LOGICAL_INO_ARGS_IGNORE_OFFSET	(1ULL << 0)

enum btrfs_dev_stat_values {
	/* disk I/O failure stats */
	BTRFS_DEV_STAT_WRITE_ERRS, /* EIO or EREMOTEIO from lower layers */
	BTRFS_DEV_STAT_READ_ERRS, /* EIO or EREMOTEIO from lower layers */
	BTRFS_DEV_STAT_FLUSH_ERRS, /* EIO or EREMOTEIO from lower layers */

	/* stats for indirect indications for I/O failures */
	BTRFS_DEV_STAT_CORRUPTION_ERRS, /* checksum error, bytenr error or
					 * contents is illegal: this is an
					 * indication that the block was damaged
					 * during read or write, or written to
					 * wrong location or read from wrong
					 * location */
	BTRFS_DEV_STAT_GENERATION_ERRS, /* an indication that blocks have not
					 * been written */

	BTRFS_DEV_STAT_VALUES_MAX
};

/* Reset statistics after reading; needs SYS_ADMIN capability */
#define	BTRFS_DEV_STATS_RESET		(1ULL << 0)

struct btrfs_ioctl_get_dev_stats {
	__u64 devid;				/* in */
	__u64 nr_items;				/* in/out */
	__u64 flags;				/* in/out */

	/* out values: */
	__u64 values[BTRFS_DEV_STAT_VALUES_MAX];

	/*
	 * This pads the struct to 1032 bytes. It was originally meant to pad to
	 * 1024 bytes, but when adding the flags field, the padding calculation
	 * was not adjusted.
	 */
	__u64 unused[128 - 2 - BTRFS_DEV_STAT_VALUES_MAX];
};

#ifdef MY_ABC_HERE
struct btrfs_ioctl_cksumfailed_files_args {
	__u64 sub_vol;
	__u64 ino;
};
#endif /* MY_ABC_HERE */

#define BTRFS_QUOTA_CTL_ENABLE	1
#define BTRFS_QUOTA_CTL_DISABLE	2
#define BTRFS_QUOTA_CTL_RESCAN__NOTUSED	3
#ifdef MY_ABC_HERE
#define BTRFS_QUOTA_V1_CTL_ENABLE  10
#define BTRFS_QUOTA_V2_CTL_ENABLE  11
#define BTRFS_QUOTA_CTL_UNLOAD     12
#define BTRFS_QUOTA_CTL_REMOVE_V1  13
#endif /* MY_ABC_HERE */
struct btrfs_ioctl_quota_ctl_args {
	__u64 cmd;
	__u64 status;
};

struct btrfs_ioctl_quota_rescan_args {
	__u64	flags;
	__u64   progress;
	__u64   reserved[6];
};

#ifdef MY_ABC_HERE
// "in" parameter for btrfs_ioctl_syno_quota_rescan_args flags:
#define BTRFS_SYNO_QUOTA_RESCAN                 (1ULL << 0)
#define BTRFS_SYNO_QUOTA_RESCAN_PAUSE           (1ULL << 1)
#define BTRFS_SYNO_QUOTA_RESCAN_RESUME          (1ULL << 2)
#define BTRFS_SYNO_QUOTA_RESCAN_SET_VOL_V2      (1ULL << 3)
#define BTRFS_SYNO_QUOTA_RESCAN_TRANSFER_LIMIT  (1ULL << 4)

struct btrfs_ioctl_syno_quota_rescan_args {
	__u64	flags;
	__u64   reserved[3];
};

// "in" parameter for struct btrfs_ioctl_syno_quota_status_args cmd:
// Query volume rescan progress.
#define BTRFS_QUOTA_STATUS_RESCAN_VOL_PROGRESS      (1ULL <<  1)
// Query subvolume rescan progress.
#define BTRFS_QUOTA_STATUS_RESCAN_SUBVOL_PROGRESS   (1ULL <<  2)

// "out" parameter of struct btrfs_ioctl_syno_quota_status_args status:
#define BTRFS_QUOTA_STATUS_VOL_DISABLED             (1ULL <<  0)
#define BTRFS_QUOTA_STATUS_SUBVOL_DISABLED          (1ULL <<  1)
#define BTRFS_QUOTA_STATUS_VOL_UPSTREAM_ENABLED     (1ULL <<  2)
#define BTRFS_QUOTA_STATUS_VOL_SYNO_V1_ENABLED      (1ULL <<  3)
#define BTRFS_QUOTA_STATUS_VOL_SYNO_V2_ENABLED      (1ULL <<  4)
#define BTRFS_QUOTA_STATUS_SUBVOL_ENABLED           (1ULL <<  5)
#define BTRFS_QUOTA_STATUS_VOL_RESCAN_DOING         (1ULL <<  6)
#define BTRFS_QUOTA_STATUS_VOL_RESCAN_PAUSED        (1ULL <<  7)
#define BTRFS_QUOTA_STATUS_SUBVOL_RESCAN_QUEUED     (1ULL <<  8)
#define BTRFS_QUOTA_STATUS_SUBVOL_RESCANNING        (1ULL <<  9)
#define BTRFS_QUOTA_STATUS_INCONSISTENT             (1ULL << 10)
#define BTRFS_USRQUOTA_STATUS_INCONSISTENT          (1ULL << 11)
#define BTRFS_QUOTA_STATUS_VOL_PROGRESS_VALID       (1ULL << 12)
#define BTRFS_QUOTA_STATUS_SUBVOL_PROGRESS_VALID    (1ULL << 13)

struct btrfs_ioctl_syno_quota_status_args {
	__u64	cmd;
	__u64	status;
	__u64	progress;		// 0~10000. Caller can devide it by 100 to get percentage.
	__u64	next_subvol_id;		// If "this" subvol is in recan list, report next subvol in the list.
	__u64	scanning_subvol_id;	// Currently scanning subvol. May not be the same as "this" subvol.
	__u64	reserved[3];
};
#endif /* MY_ABC_HERE */

struct btrfs_ioctl_qgroup_assign_args {
	__u64 assign;
	__u64 src;
	__u64 dst;
};

struct btrfs_ioctl_qgroup_create_args {
	__u64 create;
	__u64 qgroupid;
};

#ifdef MY_ABC_HERE
struct btrfs_ioctl_qgroup_query_args {
	/* state */
	__u64 rfer;       // disk size (could be smaller than rfer_cmpr due to compression)
	__u64 rfer_cmpr;  // original size before compression (bad naming due to compatibility)
	__u64 excl;
	__u64 excl_cmpr;

	/* limit */
	__u64 max_rfer;
	__u64 max_excl;
#ifdef MY_ABC_HERE
	union {
		__u64 soft_rfer;
		__u64 rsv_rfer;
	};
	union {
		__u64 soft_excl;
		__u64 rsv_excl;
	};
#else
	__u64 rsv_rfer;
	__u64 rsv_excl;
#endif /* MY_ABC_HERE */

	/* reservation tracking */
	__u64 reserved;
};
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_USRQUOTA_CTL_ENABLE 1
#define BTRFS_USRQUOTA_CTL_DISABLE 2
#define BTRFS_USRQUOTA_CTL_DUMPTREE 3
#ifdef MY_ABC_HERE
#define BTRFS_USRQUOTA_V1_CTL_ENABLE  10
#define BTRFS_USRQUOTA_V2_CTL_ENABLE  11
#define BTRFS_USRQUOTA_CTL_UNLOAD     12
#define BTRFS_USRQUOTA_CTL_REMOVE_V1  13
#endif /* MY_ABC_HERE */
struct btrfs_ioctl_usrquota_ctl_args {
	__u64 cmd;
	__u64 status;
};

struct btrfs_ioctl_usrquota_limit_args {
	__u64 uid;
	__u64 rfer_soft;
	__u64 rfer_hard;
};

// Deprecated. Please use qgroup's rescan.
struct btrfs_ioctl_usrquota_rescan_args {
	__u64 flags;
	__u64 rootid;
	__u64 objectid;
	__u64 reserved[5];
};

struct btrfs_ioctl_usrquota_query_args {
	__u64 uid;
	/* state */
	__u64 rfer_used;
	/* limits */
	__u64 rfer_soft;
	__u64 rfer_hard;
	/* reservation tracking */
	__u64 reserved;
};
#endif /* MY_ABC_HERE */

struct btrfs_ioctl_timespec {
	__u64 sec;
	__u32 nsec;
};

struct btrfs_ioctl_received_subvol_args {
	char	uuid[BTRFS_UUID_SIZE];	/* in */
	__u64	stransid;		/* in */
	__u64	rtransid;		/* out */
	struct btrfs_ioctl_timespec stime; /* in */
	struct btrfs_ioctl_timespec rtime; /* out */
	__u64	flags;			/* in */
#ifdef MY_ABC_HERE
	struct btrfs_ioctl_timespec otime; /* in */
	__u64	reserved[14];		/* in */
#else /* MY_ABC_HERE */
	__u64	reserved[16];		/* in */
#endif /* MY_ABC_HERE */
};

#ifdef MY_ABC_HERE

#define BTRFS_SNAP_SIZE_SHOW_EXCL_SIZE 0x1
#define BTRFS_SNAP_SIZE_SHOW_PROCESSED_SIZE 0x2
#define BTRFS_SNAP_SIZE_SHOW_MARGINAL_SIZE 0x4

struct btrfs_ioctl_snapshot_size_id_size_map {
	__u64 snap_id;
	__u64 marginal_size;
};

struct btrfs_ioctl_snapshot_size_query_args {
	/* number of snap ids pointed by *snap_id */
	__u64 snap_count;
	__u64 flags;
	/* output exclusive size in progress to fd

		if BTRFS_SNAP_SIZE_QUERY_OUTPUT_READALBE_PRGRESS
		   BTRFS_SNAP_SIZE_SHOW_PROCESSED_SIZE
		   BTRFS_SNAP_SIZE_SHOW_MARGINAL_SIZE
		are all set
		the output format will like:

			subvol(360) 0 bytes
			subvol(361) 0 bytes
			subvol(362) 2048 bytes
				.
				.
			subvol(512) 40967 bytes
			exclusive  43014 bytes
			processed  203503 bytes

			subvol(360) 0 bytes
			subvol(361) 12802 bytes
			subvol(362) 4096 bytes
				.
				.
			subvol(512) 60967 bytes
			exclusive 77865 bytes
			processed 302122 bytes
			.
			.

	*/
	__s64 fd;
	/* address of snap ids/marginal size map to caluclate */
	struct btrfs_ioctl_snapshot_size_id_size_map __user *id_maps;
	/* exclusive size in byte */
	__u64 calc_size;
	__u64 processed_size;
};
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_LOG_TREE_BG_RSV_ADD    (1ULL << 0)
#define BTRFS_LOG_TREE_BG_RSV_REMOVE (1ULL << 1)

struct btrfs_ioctl_log_tree_reserve_bg_args {
	__u64 start;
	__u64 size;
	__u64 map_start;
	__u64 flags;
	__u64 reserved[4];
};
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_FREE_SPACE_ANALYZE_NR_INTERVAL	15
#define BTRFS_FREE_SPACE_ANALYZE_FLAG_FULL	(1ULL << 0)

struct btrfs_ioctl_free_space_analyze_args {
	/* arguments */
	__u64 flags;
	__u64 min_continuous_size;

	/* results */
	__u64 interval_cnts[BTRFS_FREE_SPACE_ANALYZE_NR_INTERVAL];  // For intervals from [4K,8K), [8K,16K) to [64M,128M)
	__u64 frag_cnts;              // Count of the free blocks which < min_continuous_size
	__u64 continuous_cnts;        // Count of the free blocks which >= min_continuous_size
	__u64 total_frag_size;        // The total size of the free blocks which < min_continuous_size
	__u64 total_continuous_size;  // The total size of the free blocks which >= min_continuous_size

	/* reserved */
	__u64 reserved[8];
};
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
struct btrfs_ioctl_find_next_chunk_info_args {
	__u64 start;
	__u64 flags;
	__u64 size;
	__u64 stripe_count;
	__u64 stripe_offset[2];
	__u64 reserved[4];
};
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
/* flags for the compression ioctl */
#define BTRFS_COMPR_CTL_SET 		0x1
#define BTRFS_COMPR_CTL_COMPR_FL 	0x2

struct btrfs_ioctl_compr_ctl_args {
	__u64	flags;			/* in/out */
	__u64	size;			/* out */
	__u64	compressed_size;	/* out */
	__u64	reserved[1];
};
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_SYNO_FEAT_TREE_CTL_ENABLE 1
#define BTRFS_SYNO_FEAT_TREE_CTL_DISABLE 2
#define BTRFS_SYNO_FEAT_TREE_CTL_STATUS 3

struct btrfs_ioctl_syno_feat_tree_ctl_args {
	__u64 cmd; // commands

	/* for status */
	__u64 status;

	/* for reserve */
	__u64 reserved[4];
};

enum btrfs_syno_feature_tree_status_enum {
	SYNO_FEAT_TREE_ST_DISABLE = 0,
	SYNO_FEAT_TREE_ST_ENABLE,
};
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
enum btrfs_syno_usage_state_enum {
	SYNO_USAGE_STATE_NONE = 0,
	SYNO_USAGE_STATE_INITIAL,
	SYNO_USAGE_STATE_RESCAN,
	SYNO_USAGE_STATE_RESCAN_ERROR,
	SYNO_USAGE_STATE_RESCAN_PAUSE,
	SYNO_USAGE_STATE_ENABLE,
	SYNO_USAGE_STATE_DISABLE,
};

enum btrfs_syno_usage_root_state_enum {
	SYNO_USAGE_ROOT_STATE_NORMAL = 0,
	SYNO_USAGE_ROOT_STATE_RESCAN,
};

enum btrfs_syno_usage_type_enum {
	SYNO_USAGE_TYPE_NONE	= 0,
	SYNO_USAGE_TYPE_RO_SNAPSHOT,
	SYNO_USAGE_TYPE_MAX = 256,
};

#define BTRFS_SYNO_USAGE_CTL_ENABLE	1
#define BTRFS_SYNO_USAGE_CTL_DISABLE	2
#define BTRFS_SYNO_USAGE_CTL_STATUS	3
#define BTRFS_SYNO_USAGE_CTL_RESCAN	4
#define BTRFS_SYNO_USAGE_CTL_RESCAN_PAUSE	5
#define BTRFS_SYNO_USAGE_CTL_SUBVOL_TYPE_SET	6
#define BTRFS_SYNO_USAGE_CTL_SUBVOL_TYPE_GET	7
#define BTRFS_SYNO_USAGE_CTL_USAGE_GET_BY_TYPE	8
struct btrfs_ioctl_syno_usage_ctl_args {
	__u64 cmd; // commands

	/* for status */
	__u64 state;
	__u64 flags;
	__u32 pending_fast_rescan_count;
	__u32 pending_full_rescan_count;
	__u32 fast_rescan_pid;
	__u32 full_rescan_pid;

	/*
	 * for status usage
	 * for subvol type get/set
	 */
	__u8 type;

	/* for rescan */
	__s32 error_code;
	__u64 cur_rescan_size;
	__u64 total_rescan_size;

	/* for status usage */
	__u64 num_bytes;

	/* for reserve */
	__u64 reserved[4];
};
#endif /* MY_ABC_HERE */

/*
 * Caller doesn't want file data in the send stream, even if the
 * search of clone sources doesn't find an extent. UPDATE_EXTENT
 * commands will be sent instead of WRITE commands.
 */
#define BTRFS_SEND_FLAG_NO_FILE_DATA		0x1

/*
 * Do not add the leading stream header. Used when multiple snapshots
 * are sent back to back.
 */
#define BTRFS_SEND_FLAG_OMIT_STREAM_HEADER	0x2

/*
 * Omit the command at the end of the stream that indicated the end
 * of the stream. This option is used when multiple snapshots are
 * sent back to back.
 */
#define BTRFS_SEND_FLAG_OMIT_END_CMD		0x4

#ifdef MY_ABC_HERE
/*
 * Calculate the amount (in bytes) of new file data between the send and
 * parent snapshots, or in case of a full send, the total amount of file data
 * we will send.
 */
#define BTRFS_SEND_FLAG_CALCULATE_DATA_SIZE     0x8

/*
 * find_extent_clone in send could take a lot of time but end up find nothing
 * to clone, especially in iSCSI BLun thick provision. This flag is used
 * to indicate send to skip find_extent_clone.
 */
#define BTRFS_SEND_FLAG_SKIP_FIND_CLONE         0x10

/*
 * Use fallocate command to pre-allocate file extents and punch file holes,
 * instead of write commands with data buffers filled with 0 value bytes.
 */
#define BTRFS_SEND_FLAG_SUPPORT_FALLOCATE       0x20

/*
 * Convert value of xattr btrfs.compression of set_xattr from "zstd" to "lzo"
 */
#define BTRFS_SEND_FLAG_FALLBACK_COMPRESSION    0x40

/*
 * Support synology btrfs send/recv features by this flag so that DSM kernel
 * can be compatible with native btrfs cmd stream.
 */
#define BTRFS_SEND_FLAG_SYNO_FEATURES           0x80

#define BTRFS_SEND_FLAG_VERBOSE                 0x100

/*
 * This mask is used to clear syno flags generating cmds customized by synology
 * if BTRFS_SEND_FLAG_SYNO_FEATURES isn't turned on.
 */
#define BTRFS_SEND_GEN_SYNO_CMD_FLAG_MASK \
	 (BTRFS_SEND_FLAG_SUPPORT_FALLOCATE)
/*
 * In order to simplify how BTRFS_SEND_FLAG_MASK works with syno defined flags,
 * all send flags defined by syno config have to depend on
 * SYNO_BTRFS_SEND_FLAGS_SUPPORT.
 */
#define BTRFS_SEND_FLAG_MASK \
	(BTRFS_SEND_FLAG_NO_FILE_DATA | \
	 BTRFS_SEND_FLAG_OMIT_STREAM_HEADER | \
	 BTRFS_SEND_FLAG_OMIT_END_CMD | \
	 BTRFS_SEND_FLAG_CALCULATE_DATA_SIZE | \
	 BTRFS_SEND_FLAG_SKIP_FIND_CLONE | \
	 BTRFS_SEND_FLAG_SUPPORT_FALLOCATE | \
	 BTRFS_SEND_FLAG_FALLBACK_COMPRESSION | \
	 BTRFS_SEND_FLAG_SYNO_FEATURES | \
	 BTRFS_SEND_FLAG_VERBOSE)

#else /* MY_ABC_HERE */
#define BTRFS_SEND_FLAG_MASK \
	(BTRFS_SEND_FLAG_NO_FILE_DATA | \
	 BTRFS_SEND_FLAG_OMIT_STREAM_HEADER | \
	 BTRFS_SEND_FLAG_OMIT_END_CMD)
#endif /* MY_ABC_HERE */

struct btrfs_ioctl_send_args {
	__s64 send_fd;			/* in */
	__u64 clone_sources_count;	/* in */
	__u64 __user *clone_sources;	/* in */
	__u64 parent_root;		/* in */
	__u64 flags;			/* in */
#ifdef MY_ABC_HERE
	__u64 total_data_size;   /* out */
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	__u64 skip_cmd_count;
#endif /* MY_ABC_HERE */
#if defined(MY_ABC_HERE) && defined(MY_ABC_HERE)
	__u64 reserved[2];		/* in */
#elif defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	__u64 reserved[3];		/* in */
#else /* !defined(MY_ABC_HERE) && !defined(MY_ABC_HERE) */
	__u64 reserved[4];		/* in */
#endif /* defined(MY_ABC_HERE) && defined(MY_ABC_HERE) */
};

/*
 * Information about a fs tree root.
 *
 * All items are filled by the ioctl
 */
struct btrfs_ioctl_get_subvol_info_args {
	/* Id of this subvolume */
	__u64 treeid;

	/* Name of this subvolume, used to get the real name at mount point */
	char name[BTRFS_VOL_NAME_MAX + 1];

	/*
	 * Id of the subvolume which contains this subvolume.
	 * Zero for top-level subvolume or a deleted subvolume.
	 */
	__u64 parent_id;

	/*
	 * Inode number of the directory which contains this subvolume.
	 * Zero for top-level subvolume or a deleted subvolume
	 */
	__u64 dirid;

	/* Latest transaction id of this subvolume */
	__u64 generation;

	/* Flags of this subvolume */
	__u64 flags;

	/* UUID of this subvolume */
	__u8 uuid[BTRFS_UUID_SIZE];

	/*
	 * UUID of the subvolume of which this subvolume is a snapshot.
	 * All zero for a non-snapshot subvolume.
	 */
	__u8 parent_uuid[BTRFS_UUID_SIZE];

	/*
	 * UUID of the subvolume from which this subvolume was received.
	 * All zero for non-received subvolume.
	 */
	__u8 received_uuid[BTRFS_UUID_SIZE];

	/* Transaction id indicating when change/create/send/receive happened */
	__u64 ctransid;
	__u64 otransid;
	__u64 stransid;
	__u64 rtransid;
	/* Time corresponding to c/o/s/rtransid */
	struct btrfs_ioctl_timespec ctime;
	struct btrfs_ioctl_timespec otime;
	struct btrfs_ioctl_timespec stime;
	struct btrfs_ioctl_timespec rtime;

	/* Must be zero */
	__u64 reserved[8];
};

#define BTRFS_MAX_ROOTREF_BUFFER_NUM 255
struct btrfs_ioctl_get_subvol_rootref_args {
		/* in/out, minimum id of rootref's treeid to be searched */
		__u64 min_treeid;

		/* out */
		struct {
			__u64 treeid;
			__u64 dirid;
		} rootref[BTRFS_MAX_ROOTREF_BUFFER_NUM];

		/* out, number of found items */
		__u8 num_items;
		__u8 align[7];
};

/* Error codes as returned by the kernel */
enum btrfs_err_code {
	BTRFS_ERROR_DEV_RAID1_MIN_NOT_MET = 1,
	BTRFS_ERROR_DEV_RAID10_MIN_NOT_MET,
	BTRFS_ERROR_DEV_RAID5_MIN_NOT_MET,
	BTRFS_ERROR_DEV_RAID6_MIN_NOT_MET,
	BTRFS_ERROR_DEV_TGT_REPLACE,
	BTRFS_ERROR_DEV_MISSING_NOT_FOUND,
	BTRFS_ERROR_DEV_ONLY_WRITABLE,
	BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS,
	BTRFS_ERROR_DEV_RAID1C3_MIN_NOT_MET,
	BTRFS_ERROR_DEV_RAID1C4_MIN_NOT_MET,
};
#ifdef __KERNEL__
#ifdef MY_ABC_HERE
long btrfs_lazy_clone(struct file *file, unsigned long srcfd, u64 off,
	u64 olen, u64 destoff);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
int btrfs_vfs_ino_to_path(struct inode *inode, u64 inum, char *outpath, int len);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
struct btrfs_list_hardlinks_iter_index {
	u64 type;			/* INODE_REF/INODE_EXTREF */
	u64 dir;			/* for INODE_REF */
	u64 dir_index;		/* for INODE_REF */
	u64 offset;			/* for INODE_EXTREF */
	u64 cursor;			/* internal use */
	u64 free_space;		/* internal use */
};
enum btrfs_list_hardlinks_index_type_enum {
	SYNO_BTRFS_LIST_HARDLINKS_INDEX_TYPE_INODE_REF = 0,
	SYNO_BTRFS_LIST_HARDLINKS_INDEX_TYPE_INODE_EXTREF,
};
struct btrfs_hardlink_entry {
	u32 record_len;
	u32 name_len;
	u64 parent_inum;
} __attribute__ ((__packed__));
struct btrfs_list_hardlinks_args {
	struct inode *inode;	/* in : subvolume dir inode */
	u64 inum;				/* in */
	u64 buf_size;			/* in : buffer size */
	struct btrfs_list_hardlinks_iter_index index; /* in/out */
	u64 elem_cnt;			/* out */
	u8 buf[0];				/* out */
};
int btrfs_list_hardlinks(struct btrfs_list_hardlinks_args *args);
#endif /* MY_ABC_HERE */
#endif /* __KERNEL__ */

#define BTRFS_IOC_SNAP_CREATE _IOW(BTRFS_IOCTL_MAGIC, 1, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_DEFRAG _IOW(BTRFS_IOCTL_MAGIC, 2, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_RESIZE _IOW(BTRFS_IOCTL_MAGIC, 3, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_SCAN_DEV _IOW(BTRFS_IOCTL_MAGIC, 4, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_FORGET_DEV _IOW(BTRFS_IOCTL_MAGIC, 5, \
				   struct btrfs_ioctl_vol_args)
/* trans start and trans end are dangerous, and only for
 * use by applications that know how to avoid the
 * resulting deadlocks
 */
#define BTRFS_IOC_TRANS_START  _IO(BTRFS_IOCTL_MAGIC, 6)
#define BTRFS_IOC_TRANS_END    _IO(BTRFS_IOCTL_MAGIC, 7)
#define BTRFS_IOC_SYNC         _IO(BTRFS_IOCTL_MAGIC, 8)

#define BTRFS_IOC_CLONE        _IOW(BTRFS_IOCTL_MAGIC, 9, int)
#define BTRFS_IOC_ADD_DEV _IOW(BTRFS_IOCTL_MAGIC, 10, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_RM_DEV _IOW(BTRFS_IOCTL_MAGIC, 11, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_BALANCE _IOW(BTRFS_IOCTL_MAGIC, 12, \
				   struct btrfs_ioctl_vol_args)

#define BTRFS_IOC_CLONE_RANGE _IOW(BTRFS_IOCTL_MAGIC, 13, \
				  struct btrfs_ioctl_clone_range_args)

#define BTRFS_IOC_SUBVOL_CREATE _IOW(BTRFS_IOCTL_MAGIC, 14, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_SNAP_DESTROY _IOW(BTRFS_IOCTL_MAGIC, 15, \
				struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_DEFRAG_RANGE _IOW(BTRFS_IOCTL_MAGIC, 16, \
				struct btrfs_ioctl_defrag_range_args)
#define BTRFS_IOC_TREE_SEARCH _IOWR(BTRFS_IOCTL_MAGIC, 17, \
				   struct btrfs_ioctl_search_args)
#define BTRFS_IOC_TREE_SEARCH_V2 _IOWR(BTRFS_IOCTL_MAGIC, 17, \
					   struct btrfs_ioctl_search_args_v2)
#define BTRFS_IOC_INO_LOOKUP _IOWR(BTRFS_IOCTL_MAGIC, 18, \
				   struct btrfs_ioctl_ino_lookup_args)
#define BTRFS_IOC_DEFAULT_SUBVOL _IOW(BTRFS_IOCTL_MAGIC, 19, __u64)
#define BTRFS_IOC_SPACE_INFO _IOWR(BTRFS_IOCTL_MAGIC, 20, \
				    struct btrfs_ioctl_space_args)
#define BTRFS_IOC_START_SYNC _IOR(BTRFS_IOCTL_MAGIC, 24, __u64)
#define BTRFS_IOC_WAIT_SYNC  _IOW(BTRFS_IOCTL_MAGIC, 22, __u64)
#define BTRFS_IOC_SNAP_CREATE_V2 _IOW(BTRFS_IOCTL_MAGIC, 23, \
				   struct btrfs_ioctl_vol_args_v2)
#define BTRFS_IOC_SUBVOL_CREATE_V2 _IOW(BTRFS_IOCTL_MAGIC, 24, \
				   struct btrfs_ioctl_vol_args_v2)
#define BTRFS_IOC_SUBVOL_GETFLAGS _IOR(BTRFS_IOCTL_MAGIC, 25, __u64)
#define BTRFS_IOC_SUBVOL_SETFLAGS _IOW(BTRFS_IOCTL_MAGIC, 26, __u64)
#define BTRFS_IOC_SCRUB _IOWR(BTRFS_IOCTL_MAGIC, 27, \
			      struct btrfs_ioctl_scrub_args)
#define BTRFS_IOC_SCRUB_CANCEL _IO(BTRFS_IOCTL_MAGIC, 28)
#define BTRFS_IOC_SCRUB_PROGRESS _IOWR(BTRFS_IOCTL_MAGIC, 29, \
				       struct btrfs_ioctl_scrub_args)
#define BTRFS_IOC_DEV_INFO _IOWR(BTRFS_IOCTL_MAGIC, 30, \
				 struct btrfs_ioctl_dev_info_args)
#define BTRFS_IOC_FS_INFO _IOR(BTRFS_IOCTL_MAGIC, 31, \
			       struct btrfs_ioctl_fs_info_args)
#define BTRFS_IOC_BALANCE_V2 _IOWR(BTRFS_IOCTL_MAGIC, 32, \
				   struct btrfs_ioctl_balance_args)
#define BTRFS_IOC_BALANCE_CTL _IOW(BTRFS_IOCTL_MAGIC, 33, int)
#define BTRFS_IOC_BALANCE_PROGRESS _IOR(BTRFS_IOCTL_MAGIC, 34, \
					struct btrfs_ioctl_balance_args)
#define BTRFS_IOC_INO_PATHS _IOWR(BTRFS_IOCTL_MAGIC, 35, \
					struct btrfs_ioctl_ino_path_args)
#define BTRFS_IOC_LOGICAL_INO _IOWR(BTRFS_IOCTL_MAGIC, 36, \
					struct btrfs_ioctl_logical_ino_args)
#define BTRFS_IOC_SET_RECEIVED_SUBVOL _IOWR(BTRFS_IOCTL_MAGIC, 37, \
				struct btrfs_ioctl_received_subvol_args)
#define BTRFS_IOC_SEND _IOW(BTRFS_IOCTL_MAGIC, 38, struct btrfs_ioctl_send_args)
#define BTRFS_IOC_DEVICES_READY _IOR(BTRFS_IOCTL_MAGIC, 39, \
				     struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_QUOTA_CTL _IOWR(BTRFS_IOCTL_MAGIC, 40, \
			       struct btrfs_ioctl_quota_ctl_args)
#define BTRFS_IOC_QGROUP_ASSIGN _IOW(BTRFS_IOCTL_MAGIC, 41, \
			       struct btrfs_ioctl_qgroup_assign_args)
#define BTRFS_IOC_QGROUP_CREATE _IOW(BTRFS_IOCTL_MAGIC, 42, \
			       struct btrfs_ioctl_qgroup_create_args)
#define BTRFS_IOC_QGROUP_LIMIT _IOR(BTRFS_IOCTL_MAGIC, 43, \
			       struct btrfs_ioctl_qgroup_limit_args)
#define BTRFS_IOC_QUOTA_RESCAN _IOW(BTRFS_IOCTL_MAGIC, 44, \
			       struct btrfs_ioctl_quota_rescan_args)
#define BTRFS_IOC_QUOTA_RESCAN_STATUS _IOR(BTRFS_IOCTL_MAGIC, 45, \
			       struct btrfs_ioctl_quota_rescan_args)
#define BTRFS_IOC_QUOTA_RESCAN_WAIT _IO(BTRFS_IOCTL_MAGIC, 46)
#define BTRFS_IOC_GET_FSLABEL 	FS_IOC_GETFSLABEL
#define BTRFS_IOC_SET_FSLABEL	FS_IOC_SETFSLABEL
#define BTRFS_IOC_GET_DEV_STATS _IOWR(BTRFS_IOCTL_MAGIC, 52, \
				      struct btrfs_ioctl_get_dev_stats)
#define BTRFS_IOC_DEV_REPLACE _IOWR(BTRFS_IOCTL_MAGIC, 53, \
				    struct btrfs_ioctl_dev_replace_args)
#define BTRFS_IOC_FILE_EXTENT_SAME _IOWR(BTRFS_IOCTL_MAGIC, 54, \
					 struct btrfs_ioctl_same_args)
#define BTRFS_IOC_GET_FEATURES _IOR(BTRFS_IOCTL_MAGIC, 57, \
				   struct btrfs_ioctl_feature_flags)
#define BTRFS_IOC_SET_FEATURES _IOW(BTRFS_IOCTL_MAGIC, 57, \
				   struct btrfs_ioctl_feature_flags[2])
#define BTRFS_IOC_GET_SUPPORTED_FEATURES _IOR(BTRFS_IOCTL_MAGIC, 57, \
				   struct btrfs_ioctl_feature_flags[3])
#define BTRFS_IOC_RM_DEV_V2 _IOW(BTRFS_IOCTL_MAGIC, 58, \
				   struct btrfs_ioctl_vol_args_v2)
#define BTRFS_IOC_LOGICAL_INO_V2 _IOWR(BTRFS_IOCTL_MAGIC, 59, \
					struct btrfs_ioctl_logical_ino_args)
#define BTRFS_IOC_GET_SUBVOL_INFO _IOR(BTRFS_IOCTL_MAGIC, 60, \
				struct btrfs_ioctl_get_subvol_info_args)
#define BTRFS_IOC_GET_SUBVOL_ROOTREF _IOWR(BTRFS_IOCTL_MAGIC, 61, \
				struct btrfs_ioctl_get_subvol_rootref_args)
#define BTRFS_IOC_INO_LOOKUP_USER _IOWR(BTRFS_IOCTL_MAGIC, 62, \
				struct btrfs_ioctl_ino_lookup_user_args)
#define BTRFS_IOC_SNAP_DESTROY_V2 _IOW(BTRFS_IOCTL_MAGIC, 63, \
				struct btrfs_ioctl_vol_args_v2)

#ifdef MY_ABC_HERE
#define BTRFS_IOC_SYNO_LOCKER_GET _IOR(BTRFS_IOCTL_MAGIC, 232, struct btrfs_ioctl_syno_locker_args)
#define BTRFS_IOC_SYNO_LOCKER_SET _IOW(BTRFS_IOCTL_MAGIC, 233, struct btrfs_ioctl_syno_locker_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_GET_SYNO_FLAGS _IOR(BTRFS_IOCTL_MAGIC, 234, \
				struct btrfs_ioctl_syno_flags)
#define BTRFS_IOC_SET_SYNO_FLAGS _IOW(BTRFS_IOCTL_MAGIC, 234, \
				struct btrfs_ioctl_syno_flags[2])
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_SYNO_QUOTA_STATUS _IOWR(BTRFS_IOCTL_MAGIC, 235, \
				struct btrfs_ioctl_syno_quota_status_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_SYNO_FEAT_TREE_CTL _IOWR(BTRFS_IOCTL_MAGIC, 236, \
				   struct btrfs_ioctl_syno_feat_tree_ctl_args)
#endif /* MY_ABC_HERE */

#ifdef MY_DEF_HERE
#define BTRFS_IOC_SYNO_SET_DEDUPE_FLAG _IOWR(BTRFS_IOCTL_MAGIC, 237, \
					     struct btrfs_ioctl_syno_dedupe_cmd_args)
#define BTRFS_IOC_SYNO_EXTENT_SAME _IOWR(BTRFS_IOCTL_MAGIC, 238, \
					 struct btrfs_ioctl_syno_extent_same_args)
#endif /* MY_DEF_HERE */
#ifdef MY_ABC_HERE
#define BTRFS_IOC_FREE_SPACE_ANALYZE	_IOWR(BTRFS_IOCTL_MAGIC, 240, \
				   struct btrfs_ioctl_free_space_analyze_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_SYNO_USAGE_CTL	_IOWR(BTRFS_IOCTL_MAGIC, 241, \
				   struct btrfs_ioctl_syno_usage_ctl_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_FIND_NEXT_CHUNK_INFO  _IOWR(BTRFS_IOCTL_MAGIC, 242, \
				struct btrfs_ioctl_find_next_chunk_info_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_SYNO_RESERVE_LOG_TREE_BLOCK_GROUP _IOWR(BTRFS_IOCTL_MAGIC, 244, \
				    struct btrfs_ioctl_log_tree_reserve_bg_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_SYNC_SYNO _IO(BTRFS_IOCTL_MAGIC, 246)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_SNAPSHOT_SIZE_QUERY _IOWR(BTRFS_IOCTL_MAGIC, 247, \
				  struct btrfs_ioctl_snapshot_size_query_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_COMPR_CTL _IOR(BTRFS_IOCTL_MAGIC, 248, \
				    struct btrfs_ioctl_compr_ctl_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_QGROUP_QUERY _IOR(BTRFS_IOCTL_MAGIC, 253, \
                                    struct btrfs_ioctl_qgroup_query_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_CKSUMFAILED_FILES_GET _IOR(BTRFS_IOCTL_MAGIC, 254, \
		                            struct btrfs_ioctl_cksumfailed_files_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_SYNO_CLONE_RANGE_V2 _IOWR(BTRFS_IOCTL_MAGIC, 245, \
				    struct btrfs_ioctl_syno_clone_range_args_v2)
#endif /* CONFIG_SYNO_BTRFS_CLONE_RANGE */

#ifdef MY_ABC_HERE
#define BTRFS_IOC_USRQUOTA_CTL _IOWR(BTRFS_IOCTL_MAGIC, 250, \
                                    struct btrfs_ioctl_usrquota_ctl_args)
#define BTRFS_IOC_USRQUOTA_LIMIT _IOW(BTRFS_IOCTL_MAGIC, 250, \
                                      struct btrfs_ioctl_usrquota_limit_args)
#define BTRFS_IOC_USRQUOTA_RESCAN _IO(BTRFS_IOCTL_MAGIC, 250)
#define BTRFS_IOC_USRQUOTA_RESCAN_STATUS _IOR(BTRFS_IOCTL_MAGIC, 251, \
                                              struct btrfs_ioctl_usrquota_rescan_args)
#define BTRFS_IOC_USRQUOTA_RESCAN_WAIT _IO(BTRFS_IOCTL_MAGIC, 251)
#define BTRFS_IOC_USRQUOTA_QUERY _IOR(BTRFS_IOCTL_MAGIC, 252, \
                                      struct btrfs_ioctl_usrquota_query_args)
#define BTRFS_IOC_USRQUOTA_CLEAN _IOW(BTRFS_IOCTL_MAGIC, 252, __u64)

#define BTRFS_IOC_SYNO_QUOTA_RESCAN _IOW(BTRFS_IOCTL_MAGIC, 255, \
			       struct btrfs_ioctl_syno_quota_rescan_args)
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
enum {
	QGROUP_NL_C_UNSPEC,
	QGROUP_NL_C_OVER_LIMIT,
	QGROUP_NL_C_UNDER_LIMIT,
	__QGROUP_NL_C_MAX,
};
#define QGROUP_NL_C_MAX (__QGROUP_NL_C_MAX - 1)

enum {
	QGROUP_NL_A_FSID,
	QGROUP_NL_A_SUBVOL_ID,
	QGROUP_NL_A_QUOTA_LIMIT,
	QGROUP_NL_A_QUOTA_USED,
	QGROUP_NL_A_PAD,
	__QGROUP_NL_A_MAX,
};
#define QGROUP_NL_A_MAX (__QGROUP_NL_A_MAX - 1)
#endif /* MY_ABC_HERE */

#endif /* _UAPI_LINUX_BTRFS_H */
