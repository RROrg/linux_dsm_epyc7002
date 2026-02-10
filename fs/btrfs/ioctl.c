#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/writeback.h>
#include <linux/compat.h>
#include <linux/security.h>
#include <linux/xattr.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/uuid.h>
#include <linux/btrfs.h>
#include <linux/uaccess.h>
#include <linux/iversion.h>
#include "ctree.h"
#include "disk-io.h"
#include "export.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "print-tree.h"
#include "volumes.h"
#include "locking.h"
#include "inode-map.h"
#include "backref.h"
#include "rcu-string.h"
#include "send.h"
#include "dev-replace.h"
#include "props.h"
#include "sysfs.h"
#include "qgroup.h"
#include "tree-log.h"
#include "compression.h"
#include "space-info.h"
#include "delalloc-space.h"
#include "block-group.h"
#ifdef MY_ABC_HERE
#include "reflink.h"
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
#include "syno-feat-tree.h"
#endif /* MY_ABC_HERE */

#ifdef CONFIG_64BIT
/* If we have a 32-bit userspace and 64-bit kernel, then the UAPI
 * structures are incorrect, as the timespec structure from userspace
 * is 4 bytes too small. We define these alternatives here to teach
 * the kernel about the 32-bit struct packing.
 */
struct btrfs_ioctl_timespec_32 {
	__u64 sec;
	__u32 nsec;
} __attribute__ ((__packed__));

struct btrfs_ioctl_received_subvol_args_32 {
	char	uuid[BTRFS_UUID_SIZE];	/* in */
	__u64	stransid;		/* in */
	__u64	rtransid;		/* out */
	struct btrfs_ioctl_timespec_32 stime; /* in */
	struct btrfs_ioctl_timespec_32 rtime; /* out */
	__u64	flags;			/* in */
#ifdef MY_ABC_HERE
	struct btrfs_ioctl_timespec_32 otime; /* in */
	// why 2 reserved is used(64+64=128bits) but
	// otime only occupies 64+32=96(bits)
	// This is for compatible to 32bits userspace
	// After this change, sizeof(btrfs_ioctl_received_subvol_args_32)
	// changed from 192 bytes to 188 bytes;
	__u64   reserved[14];
#else /* MY_ABC_HERE */
	__u64	reserved[16];		/* in */
#endif /* MY_ABC_HERE */
} __attribute__ ((__packed__));

#define BTRFS_IOC_SET_RECEIVED_SUBVOL_32 _IOWR(BTRFS_IOCTL_MAGIC, 37, \
				struct btrfs_ioctl_received_subvol_args_32)
#endif

#if defined(CONFIG_64BIT) && defined(CONFIG_COMPAT)
struct btrfs_ioctl_send_args_32 {
	__s64 send_fd;			/* in */
	__u64 clone_sources_count;	/* in */
	compat_uptr_t clone_sources;	/* in */
	__u64 parent_root;		/* in */
	__u64 flags;			/* in */
	__u64 reserved[4];		/* in */
} __attribute__ ((__packed__));

#define BTRFS_IOC_SEND_32 _IOW(BTRFS_IOCTL_MAGIC, 38, \
			       struct btrfs_ioctl_send_args_32)
#endif

/* Mask out flags that are inappropriate for the given type of inode. */
static unsigned int btrfs_mask_fsflags_for_type(struct inode *inode,
		unsigned int flags)
{
	if (S_ISDIR(inode->i_mode))
		return flags;
	else if (S_ISREG(inode->i_mode))
		return flags & ~FS_DIRSYNC_FL;
	else
		return flags & (FS_NODUMP_FL | FS_NOATIME_FL);
}

/*
 * Export internal inode flags to the format expected by the FS_IOC_GETFLAGS
 * ioctl.
 */
static unsigned int btrfs_inode_flags_to_fsflags(unsigned int flags)
{
	unsigned int iflags = 0;

	if (flags & BTRFS_INODE_SYNC)
		iflags |= FS_SYNC_FL;
	if (flags & BTRFS_INODE_IMMUTABLE)
		iflags |= FS_IMMUTABLE_FL;
	if (flags & BTRFS_INODE_APPEND)
		iflags |= FS_APPEND_FL;
	if (flags & BTRFS_INODE_NODUMP)
		iflags |= FS_NODUMP_FL;
	if (flags & BTRFS_INODE_NOATIME)
		iflags |= FS_NOATIME_FL;
	if (flags & BTRFS_INODE_DIRSYNC)
		iflags |= FS_DIRSYNC_FL;
	if (flags & BTRFS_INODE_NODATACOW)
		iflags |= FS_NOCOW_FL;

	if (flags & BTRFS_INODE_NOCOMPRESS)
		iflags |= FS_NOCOMP_FL;
	else if (flags & BTRFS_INODE_COMPRESS)
		iflags |= FS_COMPR_FL;

	return iflags;
}

/*
 * Update inode->i_flags based on the btrfs internal flags.
 */
void btrfs_sync_inode_flags_to_i_flags(struct inode *inode)
{
	struct btrfs_inode *binode = BTRFS_I(inode);
	unsigned int new_fl = 0;

	if (binode->flags & BTRFS_INODE_SYNC)
		new_fl |= S_SYNC;
	if (binode->flags & BTRFS_INODE_IMMUTABLE)
		new_fl |= S_IMMUTABLE;
	if (binode->flags & BTRFS_INODE_APPEND)
		new_fl |= S_APPEND;
	if (binode->flags & BTRFS_INODE_NOATIME)
		new_fl |= S_NOATIME;
	if (binode->flags & BTRFS_INODE_DIRSYNC)
		new_fl |= S_DIRSYNC;

	set_mask_bits(&inode->i_flags,
		      S_SYNC | S_APPEND | S_IMMUTABLE | S_NOATIME | S_DIRSYNC,
		      new_fl);
}

static int btrfs_ioctl_getflags(struct file *file, void __user *arg)
{
	struct btrfs_inode *binode = BTRFS_I(file_inode(file));
	unsigned int flags = btrfs_inode_flags_to_fsflags(binode->flags);

#ifdef MY_ABC_HERE
	int ret;
	enum locker_state state;

	ret = syno_op_locker_state_get(file_inode(file), &state);
	if (!ret) {
		if (IS_LOCKER_STATE_IMMUTABLE(state))
			flags |= FS_IMMUTABLE_FL;
		if (IS_LOCKER_STATE_APPENDABLE(state))
			flags |= FS_APPEND_FL;
	}
#endif /* MY_ABC_HERE */

	if (copy_to_user(arg, &flags, sizeof(flags)))
		return -EFAULT;
	return 0;
}

/*
 * Check if @flags are a supported and valid set of FS_*_FL flags and that
 * the old and new flags are not conflicting
 */
static int check_fsflags(unsigned int old_flags, unsigned int flags)
{
	if (flags & ~(FS_IMMUTABLE_FL | FS_APPEND_FL | \
		      FS_NOATIME_FL | FS_NODUMP_FL | \
		      FS_SYNC_FL | FS_DIRSYNC_FL | \
		      FS_NOCOMP_FL | FS_COMPR_FL |
		      FS_NOCOW_FL))
		return -EOPNOTSUPP;

	/* COMPR and NOCOMP on new/old are valid */
	if ((flags & FS_NOCOMP_FL) && (flags & FS_COMPR_FL))
		return -EINVAL;

	if ((flags & FS_COMPR_FL) && (flags & FS_NOCOW_FL))
		return -EINVAL;

	/* NOCOW and compression options are mutually exclusive */
	if ((old_flags & FS_NOCOW_FL) && (flags & (FS_COMPR_FL | FS_NOCOMP_FL)))
		return -EINVAL;
	if ((flags & FS_NOCOW_FL) && (old_flags & (FS_COMPR_FL | FS_NOCOMP_FL)))
		return -EINVAL;

	return 0;
}

static int btrfs_ioctl_setflags(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_root *root = binode->root;
	struct btrfs_trans_handle *trans;
	unsigned int fsflags, old_fsflags;
	int ret;
	const char *comp = NULL;
	u32 binode_flags;

	if (!inode_owner_or_capable(inode))
		return -EPERM;

	if (btrfs_root_readonly(root))
		return -EROFS;

	if (copy_from_user(&fsflags, arg, sizeof(fsflags)))
		return -EFAULT;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	inode_lock(inode);
	fsflags = btrfs_mask_fsflags_for_type(inode, fsflags);
	old_fsflags = btrfs_inode_flags_to_fsflags(binode->flags);

#ifdef MY_ABC_HERE
	/*
	 * after locker mode is set, tranditional IMMUTABLE_LF and APPEND_FL are
	 * not allowed to prevent the mixed behavior with locker.
	 */
	spin_lock(&root->locker_lock);
	if (root->locker_mode != LM_NONE && (fsflags & (FS_IMMUTABLE_FL|FS_APPEND_FL))) {
		ret = -EPERM;
		spin_unlock(&root->locker_lock);
		goto out_unlock;
	}
	spin_unlock(&root->locker_lock);
#endif /* MY_ABC_HERE */

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	/*
	 * we use IMMUTABLE & SWAPFILE protected data,
	 */
	if (IS_SWAPFILE(inode) &&
		((fsflags ^ old_fsflags) & FS_IMMUTABLE_FL)) {
		ret = -ETXTBSY;
		goto out_unlock;
	}
#endif /* MY_ABC_HERE || MY_ABC_HERE */

	ret = vfs_ioc_setflags_prepare(inode, old_fsflags, fsflags);
	if (ret)
		goto out_unlock;

#ifdef MY_ABC_HERE
	if (fsflags & FS_NOCOW_FL) {
		fsflags &= ~(FS_COMPR_FL | FS_NOCOMP_FL);
		old_fsflags &= ~(FS_COMPR_FL | FS_NOCOMP_FL);
	}
#endif /* MY_ABC_HERE */

	ret = check_fsflags(old_fsflags, fsflags);
	if (ret)
		goto out_unlock;

	binode_flags = binode->flags;
	if (fsflags & FS_SYNC_FL)
		binode_flags |= BTRFS_INODE_SYNC;
	else
		binode_flags &= ~BTRFS_INODE_SYNC;
	if (fsflags & FS_IMMUTABLE_FL)
		binode_flags |= BTRFS_INODE_IMMUTABLE;
	else
		binode_flags &= ~BTRFS_INODE_IMMUTABLE;
	if (fsflags & FS_APPEND_FL)
		binode_flags |= BTRFS_INODE_APPEND;
	else
		binode_flags &= ~BTRFS_INODE_APPEND;
	if (fsflags & FS_NODUMP_FL)
		binode_flags |= BTRFS_INODE_NODUMP;
	else
		binode_flags &= ~BTRFS_INODE_NODUMP;
	if (fsflags & FS_NOATIME_FL)
		binode_flags |= BTRFS_INODE_NOATIME;
	else
		binode_flags &= ~BTRFS_INODE_NOATIME;
	if (fsflags & FS_DIRSYNC_FL)
		binode_flags |= BTRFS_INODE_DIRSYNC;
	else
		binode_flags &= ~BTRFS_INODE_DIRSYNC;
	if (fsflags & FS_NOCOW_FL) {
		if (S_ISREG(inode->i_mode)) {
			/*
			 * It's safe to turn csums off here, no extents exist.
			 * Otherwise we want the flag to reflect the real COW
			 * status of the file and will not set it.
			 */
			if (inode->i_size == 0)
				binode_flags |= BTRFS_INODE_NODATACOW |
						BTRFS_INODE_NODATASUM;
		} else {
			binode_flags |= BTRFS_INODE_NODATACOW;
		}
	} else {
		/*
		 * Revert back under same assumptions as above
		 */
		if (S_ISREG(inode->i_mode)) {
			if (inode->i_size == 0)
				binode_flags &= ~(BTRFS_INODE_NODATACOW |
						  BTRFS_INODE_NODATASUM);
		} else {
			binode_flags &= ~BTRFS_INODE_NODATACOW;
		}
	}

	/*
	 * The COMPRESS flag can only be changed by users, while the NOCOMPRESS
	 * flag may be changed automatically if compression code won't make
	 * things smaller.
	 */
	if (fsflags & FS_NOCOMP_FL) {
		binode_flags &= ~BTRFS_INODE_COMPRESS;
		binode_flags |= BTRFS_INODE_NOCOMPRESS;
	} else if (fsflags & FS_COMPR_FL) {

		if (IS_SWAPFILE(inode)) {
			ret = -ETXTBSY;
			goto out_unlock;
		}

		binode_flags |= BTRFS_INODE_COMPRESS;
		binode_flags &= ~BTRFS_INODE_NOCOMPRESS;

		comp = btrfs_compress_type2str(fs_info->compress_type);
		if (!comp || comp[0] == 0)
#ifdef MY_ABC_HERE
			comp = btrfs_compress_type2str(BTRFS_COMPRESS_DEFAULT);
#else
			comp = btrfs_compress_type2str(BTRFS_COMPRESS_ZLIB);
#endif /* MY_ABC_HERE */
	} else {
		binode_flags &= ~(BTRFS_INODE_COMPRESS | BTRFS_INODE_NOCOMPRESS);
	}

	/*
	 * 1 for inode item
	 * 2 for properties
	 */
	trans = btrfs_start_transaction(root, 3);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_unlock;
	}

	if (comp) {
		ret = btrfs_set_prop(trans, inode, "btrfs.compression", comp,
				     strlen(comp), 0);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out_end_trans;
		}
	} else {
		ret = btrfs_set_prop(trans, inode, "btrfs.compression", NULL,
				     0, 0);
		if (ret && ret != -ENODATA) {
			btrfs_abort_transaction(trans, ret);
			goto out_end_trans;
		}
	}

	binode->flags = binode_flags;
	btrfs_sync_inode_flags_to_i_flags(inode);
	inode_inc_iversion(inode);
	inode->i_ctime = current_time(inode);
	ret = btrfs_update_inode(trans, root, inode);

 out_end_trans:
	btrfs_end_transaction(trans);
 out_unlock:
	inode_unlock(inode);
	mnt_drop_write_file(file);
	return ret;
}

/*
 * Translate btrfs internal inode flags to xflags as expected by the
 * FS_IOC_FSGETXATT ioctl. Filter only the supported ones, unknown flags are
 * silently dropped.
 */
static unsigned int btrfs_inode_flags_to_xflags(unsigned int flags)
{
	unsigned int xflags = 0;

	if (flags & BTRFS_INODE_APPEND)
		xflags |= FS_XFLAG_APPEND;
	if (flags & BTRFS_INODE_IMMUTABLE)
		xflags |= FS_XFLAG_IMMUTABLE;
	if (flags & BTRFS_INODE_NOATIME)
		xflags |= FS_XFLAG_NOATIME;
	if (flags & BTRFS_INODE_NODUMP)
		xflags |= FS_XFLAG_NODUMP;
	if (flags & BTRFS_INODE_SYNC)
		xflags |= FS_XFLAG_SYNC;

	return xflags;
}

/* Check if @flags are a supported and valid set of FS_XFLAGS_* flags */
static int check_xflags(unsigned int flags)
{
	if (flags & ~(FS_XFLAG_APPEND | FS_XFLAG_IMMUTABLE | FS_XFLAG_NOATIME |
		      FS_XFLAG_NODUMP | FS_XFLAG_SYNC))
		return -EOPNOTSUPP;
	return 0;
}

bool btrfs_exclop_start(struct btrfs_fs_info *fs_info,
			enum btrfs_exclusive_operation type)
{
	return !cmpxchg(&fs_info->exclusive_operation, BTRFS_EXCLOP_NONE, type);
}

void btrfs_exclop_finish(struct btrfs_fs_info *fs_info)
{
	WRITE_ONCE(fs_info->exclusive_operation, BTRFS_EXCLOP_NONE);
	sysfs_notify(&fs_info->fs_devices->fsid_kobj, NULL, "exclusive_operation");
}

/*
 * Set the xflags from the internal inode flags. The remaining items of fsxattr
 * are zeroed.
 */
static int btrfs_ioctl_fsgetxattr(struct file *file, void __user *arg)
{
	struct btrfs_inode *binode = BTRFS_I(file_inode(file));
	struct fsxattr fa;

	simple_fill_fsxattr(&fa, btrfs_inode_flags_to_xflags(binode->flags));
	if (copy_to_user(arg, &fa, sizeof(fa)))
		return -EFAULT;

	return 0;
}

static int btrfs_ioctl_fssetxattr(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_root *root = binode->root;
	struct btrfs_trans_handle *trans;
	struct fsxattr fa, old_fa;
	unsigned old_flags;
	unsigned old_i_flags;
	int ret = 0;

	if (!inode_owner_or_capable(inode))
		return -EPERM;

	if (btrfs_root_readonly(root))
		return -EROFS;

	if (copy_from_user(&fa, arg, sizeof(fa)))
		return -EFAULT;

	ret = check_xflags(fa.fsx_xflags);
	if (ret)
		return ret;

	if (fa.fsx_extsize != 0 || fa.fsx_projid != 0 || fa.fsx_cowextsize != 0)
		return -EOPNOTSUPP;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	inode_lock(inode);

	old_flags = binode->flags;
	old_i_flags = inode->i_flags;

	simple_fill_fsxattr(&old_fa,
			    btrfs_inode_flags_to_xflags(binode->flags));
	ret = vfs_ioc_fssetxattr_check(inode, &old_fa, &fa);
	if (ret)
		goto out_unlock;

	if (fa.fsx_xflags & FS_XFLAG_SYNC)
		binode->flags |= BTRFS_INODE_SYNC;
	else
		binode->flags &= ~BTRFS_INODE_SYNC;
	if (fa.fsx_xflags & FS_XFLAG_IMMUTABLE)
		binode->flags |= BTRFS_INODE_IMMUTABLE;
	else
		binode->flags &= ~BTRFS_INODE_IMMUTABLE;
	if (fa.fsx_xflags & FS_XFLAG_APPEND)
		binode->flags |= BTRFS_INODE_APPEND;
	else
		binode->flags &= ~BTRFS_INODE_APPEND;
	if (fa.fsx_xflags & FS_XFLAG_NODUMP)
		binode->flags |= BTRFS_INODE_NODUMP;
	else
		binode->flags &= ~BTRFS_INODE_NODUMP;
	if (fa.fsx_xflags & FS_XFLAG_NOATIME)
		binode->flags |= BTRFS_INODE_NOATIME;
	else
		binode->flags &= ~BTRFS_INODE_NOATIME;

	/* 1 item for the inode */
	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_unlock;
	}

	btrfs_sync_inode_flags_to_i_flags(inode);
	inode_inc_iversion(inode);
	inode->i_ctime = current_time(inode);
	ret = btrfs_update_inode(trans, root, inode);

	btrfs_end_transaction(trans);

out_unlock:
	if (ret) {
		binode->flags = old_flags;
		inode->i_flags = old_i_flags;
	}

	inode_unlock(inode);
	mnt_drop_write_file(file);

	return ret;
}

static int btrfs_ioctl_getversion(struct file *file, int __user *arg)
{
	struct inode *inode = file_inode(file);

	return put_user(inode->i_generation, arg);
}

static noinline int btrfs_ioctl_fitrim(struct btrfs_fs_info *fs_info,
					void __user *arg)
{
	struct btrfs_device *device;
	struct request_queue *q;
	struct fstrim_range range;
	u64 minlen = ULLONG_MAX;
	u64 num_devices = 0;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * If the fs is mounted with nologreplay, which requires it to be
	 * mounted in RO mode as well, we can not allow discard on free space
	 * inside block groups, because log trees refer to extents that are not
	 * pinned in a block group's free space cache (pinning the extents is
	 * precisely the first phase of replaying a log tree).
	 */
	if (btrfs_test_opt(fs_info, NOLOGREPLAY))
		return -EROFS;

	rcu_read_lock();
	list_for_each_entry_rcu(device, &fs_info->fs_devices->devices,
				dev_list) {
		if (!device->bdev)
			continue;
		q = bdev_get_queue(device->bdev);
		if (blk_queue_discard(q)) {
			num_devices++;
			minlen = min_t(u64, q->limits.discard_granularity,
				     minlen);
		}
	}
	rcu_read_unlock();

	if (!num_devices)
		return -EOPNOTSUPP;
	if (copy_from_user(&range, arg, sizeof(range)))
		return -EFAULT;

	/*
	 * NOTE: Don't truncate the range using super->total_bytes.  Bytenr of
	 * block group is in the logical address space, which can be any
	 * sectorsize aligned bytenr in  the range [0, U64_MAX].
	 */
	if (range.len < fs_info->sb->s_blocksize)
		return -EINVAL;

	range.minlen = max(range.minlen, minlen);
	ret = btrfs_trim_fs(fs_info, &range
#ifdef MY_ABC_HERE
			    , TRIM_SEND_TRIM
#endif /* MY_ABC_HERE */
			    );
	if (ret < 0)
		return ret;

	if (copy_to_user(arg, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

#ifdef MY_ABC_HERE
static noinline int btrfs_ioctl_hint_unused(struct file *file, void __user *arg)
{
	struct fstrim_range range;
	struct btrfs_fs_info *fs_info = btrfs_sb(file_inode(file)->i_sb);
	struct btrfs_device *device;
	u64 num_devices = 0;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	rcu_read_lock();
	list_for_each_entry_rcu(device, &fs_info->fs_devices->devices,
				dev_list) {
		if (!device->bdev)
			continue;
		if (blk_queue_unused_hint(bdev_get_queue(device->bdev)))
			num_devices++;
	}
	rcu_read_unlock();

	if (!num_devices)
		return -EOPNOTSUPP;

	if (copy_from_user(&range, (struct fstrim_range __user *)arg,
			   sizeof(range)))
		return -EFAULT;

	/*
	 * NOTE: Don't truncate the range using super->total_bytes.  Bytenr of
	 * block group is in the logical address space, which can be any
	 * sectorsize aligned bytenr in  the range [0, U64_MAX].
	 */
	if (range.len < fs_info->sb->s_blocksize)
		return -EINVAL;

	ret = btrfs_trim_fs(fs_info, &range, TRIM_SEND_HINT);
	if (!ret)
		btrfs_notice(fs_info, "total send %llu bytes hints", range.len);

	return ret;
}
#endif /* MY_ABC_HERE */

int __pure btrfs_is_empty_uuid(u8 *uuid)
{
	int i;

	for (i = 0; i < BTRFS_UUID_SIZE; i++) {
		if (uuid[i])
			return 0;
	}
	return 1;
}

static noinline int create_subvol(struct inode *dir,
				  struct dentry *dentry,
				  const char *name, int namelen,
				  struct btrfs_qgroup_inherit *inherit)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_key key;
	struct btrfs_root_item *root_item;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_root *new_root;
	struct btrfs_block_rsv block_rsv;
	struct timespec64 cur_time = current_time(dir);
	struct inode *inode;
	int ret;
	dev_t anon_dev;
	u64 objectid;
	u64 new_dirid = BTRFS_FIRST_FREE_OBJECTID;
	u64 index = 0;
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE) || \
	defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	int credit_for_syno = 0;
#endif /* MY_ABC_HERE || MY_ABC_HERE ||
          MY_ABC_HERE || MY_ABC_HERE */
#ifdef MY_ABC_HERE
	struct btrfs_syno_usage_root_status syno_usage_root_status;
#endif /* MY_ABC_HERE */
#if defined(MY_ABC_HERE)
	struct btrfs_new_fs_root_args *new_fs_root_args = NULL;
#endif /* MY_ABC_HERE */

	root_item = kzalloc(sizeof(*root_item), GFP_KERNEL);
	if (!root_item)
		return -ENOMEM;

	ret = btrfs_find_free_objectid(fs_info->tree_root, &objectid);
	if (ret)
		goto out_root_item;

	/*
	 * Don't create subvolume whose level is not zero. Or qgroup will be
	 * screwed up since it assumes subvolume qgroup's level to be 0.
	 */
	if (btrfs_qgroup_level(objectid)) {
		ret = -ENOSPC;
		goto out_root_item;
	}

	ret = get_anon_bdev(&anon_dev);
	if (ret < 0)
		goto out_root_item;

#if defined(MY_ABC_HERE)
	new_fs_root_args = btrfs_alloc_new_fs_root_args();
	if (IS_ERR(new_fs_root_args)) {
		ret = PTR_ERR(new_fs_root_args);
		new_fs_root_args = NULL;
		goto out_root_item;
	}
#endif /* MY_ABC_HERE */

	btrfs_init_block_rsv(&block_rsv, BTRFS_BLOCK_RSV_TEMP);
	/*
	 * The same as the snapshot creation, please see the comment
	 * of create_snapshot().
	 */
#ifdef MY_ABC_HERE
	// 1 for dir_item_caseless
	if (btrfs_super_compat_flags(fs_info->super_copy) & BTRFS_FEATURE_COMPAT_SYNO_CASELESS)
		credit_for_syno++;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	// 1 for xattr to store archive bit
	credit_for_syno++;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	credit_for_syno++;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	// 1 for syno_usage_root_status_item
	if (test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		credit_for_syno++;
#endif /* MY_ABC_HERE */

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE) || \
    defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	ret = btrfs_subvolume_reserve_metadata(root, &block_rsv, 8 + credit_for_syno, false);
#else /* MY_ABC_HERE || MY_ABC_HERE || \
         MY_ABC_HERE || MY_ABC_HERE */
	ret = btrfs_subvolume_reserve_metadata(root, &block_rsv, 8, false);
#endif /* MY_ABC_HERE || MY_ABC_HERE ||
          MY_ABC_HERE || MY_ABC_HERE */
	if (ret)
		goto out_anon_dev;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		btrfs_subvolume_release_metadata(root, &block_rsv);
		goto out_anon_dev;
	}
	trans->block_rsv = &block_rsv;
	trans->bytes_reserved = block_rsv.size;

	ret = btrfs_qgroup_inherit(trans, 0, objectid, inherit);
	if (ret)
		goto out;
#ifdef MY_ABC_HERE
	ret = btrfs_usrquota_mksubvol(trans, objectid);
	if (ret)
		goto out;
#endif /* MY_ABC_HERE */

	leaf = btrfs_alloc_tree_block(trans, root, 0, objectid, NULL, 0, 0, 0,
				      BTRFS_NESTING_NORMAL);
	if (IS_ERR(leaf)) {
		ret = PTR_ERR(leaf);
		goto out;
	}

	btrfs_mark_buffer_dirty(leaf);

	inode_item = &root_item->inode;
	btrfs_set_stack_inode_generation(inode_item, 1);
	btrfs_set_stack_inode_size(inode_item, 3);
	btrfs_set_stack_inode_nlink(inode_item, 1);
	btrfs_set_stack_inode_nbytes(inode_item,
				     fs_info->nodesize);
	btrfs_set_stack_inode_mode(inode_item, S_IFDIR | 0755);

#ifdef MY_ABC_HERE
	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags))
		btrfs_set_root_flags(root_item, BTRFS_ROOT_SUBVOL_CMPR_RATIO);
	else
#endif /* MY_ABC_HERE */
	btrfs_set_root_flags(root_item, 0);
	btrfs_set_root_limit(root_item, 0);
	btrfs_set_stack_inode_flags(inode_item, BTRFS_INODE_ROOT_ITEM_INIT);

	btrfs_set_root_bytenr(root_item, leaf->start);
	btrfs_set_root_generation(root_item, trans->transid);
	btrfs_set_root_level(root_item, 0);
	btrfs_set_root_refs(root_item, 1);
	btrfs_set_root_used(root_item, leaf->len);
	btrfs_set_root_last_snapshot(root_item, 0);

	btrfs_set_root_generation_v2(root_item,
			btrfs_root_generation(root_item));
	generate_random_guid(root_item->uuid);
	btrfs_set_stack_timespec_sec(&root_item->otime, cur_time.tv_sec);
	btrfs_set_stack_timespec_nsec(&root_item->otime, cur_time.tv_nsec);
	root_item->ctime = root_item->otime;
	btrfs_set_root_ctransid(root_item, trans->transid);
	btrfs_set_root_otransid(root_item, trans->transid);

	btrfs_tree_unlock(leaf);

	btrfs_set_root_dirid(root_item, new_dirid);

	key.objectid = objectid;
	key.offset = 0;
	key.type = BTRFS_ROOT_ITEM_KEY;
	ret = btrfs_insert_root(trans, fs_info->tree_root, &key,
				root_item);
	if (ret) {
		/*
		 * Since we don't abort the transaction in this case, free the
		 * tree block so that we don't leak space and leave the
		 * filesystem in an inconsistent state (an extent item in the
		 * extent tree without backreferences). Also no need to have
		 * the tree block locked since it is not in any tree at this
		 * point, so no other task can find it and use it.
		 */
		btrfs_free_tree_block(trans, root, leaf, 0, 1);
		free_extent_buffer(leaf);
		goto out;
	}

	free_extent_buffer(leaf);
	leaf = NULL;

#ifdef MY_ABC_HERE
	if (test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags)) {
		btrfs_syno_usage_root_status_init(&syno_usage_root_status, NULL, false, false);
		ret = btrfs_syno_usage_root_status_update(trans, objectid, &syno_usage_root_status);
		if (ret)
			goto out;
	}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if (test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags)) {
		struct syno_quota_rescan_item_updater updater;

		syno_quota_rescan_item_init(&updater);
		updater.flags = SYNO_QUOTA_RESCAN_DONE;
		updater.version = BTRFS_QGROUP_V2_STATUS_VERSION;
		updater.rescan_inode = (u64)-1;
		updater.end_inode = (u64)-1;
		updater.tree_size = 0;
		updater.next_root = 0;
		ret = btrfs_add_update_syno_quota_rescan_item(trans, fs_info->quota_root,
			objectid, &updater);
		if (ret)
			btrfs_warn(fs_info,
				"Failed to create syno quota rescan item for root %llu, ret = %d",
				objectid, ret);
		ret = 0; // No need to abort transaction, we can fix it by doing a quota rescan.
	}
#endif /* MY_ABC_HERE */

	key.offset = (u64)-1;
	new_root = btrfs_get_new_fs_root(fs_info, objectid, anon_dev
#if defined(MY_ABC_HERE)
									, new_fs_root_args
#endif /* MY_ABC_HERE */
									);
	if (IS_ERR(new_root)) {
		ret = PTR_ERR(new_root);
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
	/* anon_dev is owned by new_root now. */
	anon_dev = 0;

	btrfs_record_root_in_trans(trans, new_root);

	ret = btrfs_create_subvol_root(trans, new_root, root, new_dirid);
	btrfs_put_root(new_root);
	if (ret) {
		/* We potentially lose an unused inode item here */
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	mutex_lock(&new_root->objectid_mutex);
	new_root->highest_objectid = new_dirid;
	mutex_unlock(&new_root->objectid_mutex);

	/*
	 * insert the directory item
	 */
	ret = btrfs_set_inode_index(BTRFS_I(dir), &index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = btrfs_insert_dir_item(trans, name, namelen, BTRFS_I(dir), &key,
				    BTRFS_FT_DIR, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	btrfs_i_size_write(BTRFS_I(dir), dir->i_size + namelen * 2);
	ret = btrfs_update_inode(trans, root, dir);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = btrfs_add_root_ref(trans, objectid, root->root_key.objectid,
				 btrfs_ino(BTRFS_I(dir)), index, name, namelen);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = btrfs_uuid_tree_add(trans, root_item->uuid,
				  BTRFS_UUID_KEY_SUBVOL, objectid);
	if (ret)
		btrfs_abort_transaction(trans, ret);

out:
	trans->block_rsv = NULL;
	trans->bytes_reserved = 0;
	btrfs_subvolume_release_metadata(root, &block_rsv);

	if (ret)
		btrfs_end_transaction(trans);
	else
		ret = btrfs_commit_transaction(trans);

	if (!ret) {
#ifdef MY_ABC_HERE
		inode = btrfs_lookup_dentry(dir, dentry, 0);
#else /* MY_ABC_HERE */
		inode = btrfs_lookup_dentry(dir, dentry);
#endif /* MY_ABC_HERE */
		if (IS_ERR(inode))
			return PTR_ERR(inode);
		d_instantiate(dentry, inode);
	}
out_anon_dev:
	if (anon_dev)
		free_anon_bdev(anon_dev);
out_root_item:
#if defined(MY_ABC_HERE)
	btrfs_free_new_fs_root_args(new_fs_root_args);
#endif /* MY_ABC_HERE */
	kfree(root_item);
	return ret;
}

static int create_snapshot(struct btrfs_root *root, struct inode *dir,
			   struct dentry *dentry, bool readonly,
			   struct btrfs_qgroup_inherit *inherit
#ifdef MY_ABC_HERE
			   ,u64 copy_limit_from
#endif /* MY_ABC_HERE */
			  )
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct inode *inode;
	struct btrfs_pending_snapshot *pending_snapshot;
	struct btrfs_trans_handle *trans;
	int ret;
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE) \
					     || defined(MY_ABC_HERE)
	int credit_for_syno = 0;
#endif /* MY_ABC_HERE || MY_ABC_HERE 
					  || MY_ABC_HERE */
#ifdef MY_ABC_HERE
	u64 reserve_usrquota_items = 0;
	u64 reserve_usrquota_leafs = 0;
#endif /* MY_ABC_HERE */

	if (!test_bit(BTRFS_ROOT_SHAREABLE, &root->state))
		return -EINVAL;

	if (atomic_read(&root->nr_swapfiles)) {
		btrfs_warn(fs_info,
			   "cannot snapshot subvolume with active swapfile");
		return -ETXTBSY;
	}

	pending_snapshot = kzalloc(sizeof(*pending_snapshot), GFP_KERNEL);
	if (!pending_snapshot)
		return -ENOMEM;

#ifdef MY_ABC_HERE
	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) ||
			test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
		ret = usrquota_subtree_load(fs_info, root->root_key.objectid);
		if (ret)
			btrfs_warn(fs_info,
				"failed to load usrquota subtree %llu", root->root_key.objectid);

		if (!ret && copy_limit_from)
		ret = usrquota_subtree_load(fs_info, copy_limit_from);
		if (ret)
			btrfs_warn(fs_info,
				"failed to load usrquota subtree %llu", copy_limit_from);

		if (ret) {
			ret = -ENOENT;
			mutex_unlock(&fs_info->usrquota_ioctl_lock);
			goto free_pending;
		}
	}
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
#endif /* MY_ABC_HERE */

	ret = get_anon_bdev(&pending_snapshot->anon_dev);
	if (ret < 0)
		goto free_pending;

#if defined(MY_ABC_HERE)
	pending_snapshot->new_fs_root_args = btrfs_alloc_new_fs_root_args();
	if (IS_ERR(pending_snapshot->new_fs_root_args)) {
		ret = PTR_ERR(pending_snapshot->new_fs_root_args);
		pending_snapshot->new_fs_root_args = NULL;
		goto free_pending;
	}
#endif /* MY_ABC_HERE */

	pending_snapshot->root_item = kzalloc(sizeof(struct btrfs_root_item),
			GFP_KERNEL);
	pending_snapshot->path = btrfs_alloc_path();
	if (!pending_snapshot->root_item || !pending_snapshot->path) {
		ret = -ENOMEM;
		goto free_pending;
	}

	btrfs_init_block_rsv(&pending_snapshot->block_rsv,
			     BTRFS_BLOCK_RSV_TEMP);
	/*
	 * 1 - parent dir inode
	 * 2 - dir entries
	 * 1 - root item
	 * 2 - root ref/backref
	 * 1 - root of snapshot
	 * 1 - UUID item
	 */

#ifdef MY_ABC_HERE
	ret = btrfs_usrquota_calc_reserve_snap(root, copy_limit_from, &reserve_usrquota_items);
	if (ret < 0)
		goto free_pending;
	reserve_usrquota_leafs = 1 + div_u64(reserve_usrquota_items,
					(u32)BTRFS_USRQUOTA_MAX_ITEMS_LEAF(fs_info));
	credit_for_syno += (int)reserve_usrquota_leafs;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	// 1 for dir_item_caseless
	if (btrfs_super_compat_flags(fs_info->super_copy) & BTRFS_FEATURE_COMPAT_SYNO_CASELESS)
		credit_for_syno++;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	// 1 for syno_usage_root_status_item
	if (test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		credit_for_syno++;
#endif /*MY_ABC_HERE */

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	ret = btrfs_subvolume_reserve_metadata(BTRFS_I(dir)->root,
					&pending_snapshot->block_rsv,
					8 + credit_for_syno,
					false);
#else /* MY_ABC_HERE || MY_ABC_HERE */
	ret = btrfs_subvolume_reserve_metadata(BTRFS_I(dir)->root,
					&pending_snapshot->block_rsv, 8,
					false);
#endif /* MY_ABC_HERE || MY_ABC_HERE*/
	if (ret)
		goto free_pending;

	pending_snapshot->dentry = dentry;
	pending_snapshot->root = root;
	pending_snapshot->readonly = readonly;
	pending_snapshot->dir = dir;
	pending_snapshot->inherit = inherit;
#ifdef MY_ABC_HERE
	pending_snapshot->copy_limit_from = copy_limit_from;
#endif /* MY_ABC_HERE */

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto fail;
	}

	spin_lock(&fs_info->trans_lock);
	list_add(&pending_snapshot->list,
		 &trans->transaction->pending_snapshots);
	spin_unlock(&fs_info->trans_lock);

	ret = btrfs_commit_transaction(trans);
	if (ret)
		goto fail;

	ret = pending_snapshot->error;
	if (ret)
		goto fail;

	ret = btrfs_orphan_cleanup(pending_snapshot->snap);
	if (ret)
		goto fail;

#ifdef MY_ABC_HERE
	inode = btrfs_lookup_dentry(d_inode(dentry->d_parent), dentry, 0);
#else /* MY_ABC_HERE */
	inode = btrfs_lookup_dentry(d_inode(dentry->d_parent), dentry);
#endif /* MY_ABC_HERE */
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto fail;
	}

	d_instantiate(dentry, inode);
	ret = 0;
	pending_snapshot->anon_dev = 0;
fail:
	/* Prevent double freeing of anon_dev */
	if (ret && pending_snapshot->snap)
		pending_snapshot->snap->anon_dev = 0;
	btrfs_put_root(pending_snapshot->snap);
	btrfs_subvolume_release_metadata(root, &pending_snapshot->block_rsv);
free_pending:
	if (pending_snapshot->anon_dev)
		free_anon_bdev(pending_snapshot->anon_dev);
#if defined(MY_ABC_HERE)
	btrfs_free_new_fs_root_args(pending_snapshot->new_fs_root_args);
#endif /* MY_ABC_HERE */
	kfree(pending_snapshot->root_item);
	btrfs_free_path(pending_snapshot->path);
	kfree(pending_snapshot);
#ifdef MY_ABC_HERE
	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) ||
			test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
		usrquota_subtree_unload(fs_info, root->root_key.objectid);
		usrquota_subtree_unload(fs_info, copy_limit_from);
	}
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
#endif /* MY_ABC_HERE */

	return ret;
}

/*  copy of may_delete in fs/namei.c()
 *	Check whether we can remove a link victim from directory dir, check
 *  whether the type of victim is right.
 *  1. We can't do it if dir is read-only (done in permission())
 *  2. We should have write and exec permissions on dir
 *  3. We can't remove anything from append-only dir
 *  4. We can't do anything with immutable dir (done in permission())
 *  5. If the sticky bit on dir is set we should either
 *	a. be owner of dir, or
 *	b. be owner of victim, or
 *	c. have CAP_FOWNER capability
 *  6. If the victim is append-only or immutable we can't do anything with
 *     links pointing to it.
 *  7. If we were asked to remove a directory and victim isn't one - ENOTDIR.
 *  8. If we were asked to remove a non-directory and victim isn't one - EISDIR.
 *  9. We can't remove a root or mountpoint.
 * 10. We don't allow removal of NFS sillyrenamed files; it's handled by
 *     nfs_async_unlink().
 */

static int btrfs_may_delete(struct inode *dir, struct dentry *victim, int isdir)
{
	int error;

	if (d_really_is_negative(victim))
		return -ENOENT;

	BUG_ON(d_inode(victim->d_parent) != dir);
	audit_inode_child(dir, victim, AUDIT_TYPE_CHILD_DELETE);

	error = inode_permission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;
	if (IS_APPEND(dir))
		return -EPERM;

#ifdef MY_ABC_HERE
	if ((IS_APPEND(d_inode(victim)) || IS_IMMUTABLE(d_inode(victim))) &&
	    !IS_EXPIRED(d_inode(victim)))
		return -EPERM;
	if (check_sticky(dir, d_inode(victim)) || IS_SWAPFILE(d_inode(victim)))
		return -EPERM;
#else
	if (check_sticky(dir, d_inode(victim)) || IS_APPEND(d_inode(victim)) ||
	    IS_IMMUTABLE(d_inode(victim)) || IS_SWAPFILE(d_inode(victim)))
		return -EPERM;
#endif /* MY_ABC_HERE */

	if (isdir) {
		if (!d_is_dir(victim))
			return -ENOTDIR;
		if (IS_ROOT(victim))
			return -EBUSY;
	} else if (d_is_dir(victim))
		return -EISDIR;
	if (IS_DEADDIR(dir))
		return -ENOENT;
	if (victim->d_flags & DCACHE_NFSFS_RENAMED)
		return -EBUSY;
	return 0;
}

/* copy of may_create in fs/namei.c() */
static inline int btrfs_may_create(struct inode *dir, struct dentry *child)
{
	if (d_really_is_positive(child))
		return -EEXIST;
	if (IS_DEADDIR(dir))
		return -ENOENT;
	return inode_permission(dir, MAY_WRITE | MAY_EXEC);
}

/*
 * Create a new subvolume below @parent.  This is largely modeled after
 * sys_mkdirat and vfs_mkdir, but we only do a single component lookup
 * inside this filesystem so it's quite a bit simpler.
 */
static noinline int btrfs_mksubvol(const struct path *parent,
				   const char *name, int namelen,
				   struct btrfs_root *snap_src,
				   bool readonly,
				   struct btrfs_qgroup_inherit *inherit
#ifdef MY_ABC_HERE
				   ,u64 copy_limit_from
#endif /* MY_ABC_HERE */
				  )
{
	struct inode *dir = d_inode(parent->dentry);
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct dentry *dentry;
	int error;

	error = down_write_killable_nested(&dir->i_rwsem, I_MUTEX_PARENT);
	if (error == -EINTR)
		return error;

	dentry = lookup_one_len(name, parent->dentry, namelen);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out_unlock;

	error = btrfs_may_create(dir, dentry);
	if (error)
		goto out_dput;

	/*
	 * even if this name doesn't exist, we may get hash collisions.
	 * check for them now when we can safely fail
	 */
	error = btrfs_check_dir_item_collision(BTRFS_I(dir)->root,
					       dir->i_ino,
					       name,
					       namelen
#ifdef MY_ABC_HERE
					       , 1
#endif /* MY_ABC_HERE */
					       );
	if (error)
		goto out_dput;

	down_read(&fs_info->subvol_sem);

	if (btrfs_root_refs(&BTRFS_I(dir)->root->root_item) == 0)
		goto out_up_read;

	if (snap_src)
		error = create_snapshot(snap_src, dir, dentry, readonly, inherit
#ifdef MY_ABC_HERE
			,copy_limit_from
#endif /* MY_ABC_HERE */
			);
	else
		error = create_subvol(dir, dentry, name, namelen, inherit);

	if (!error)
		fsnotify_mkdir(dir, dentry);
out_up_read:
	up_read(&fs_info->subvol_sem);
out_dput:
	dput(dentry);
out_unlock:
	inode_unlock(dir);
	return error;
}

static noinline int btrfs_mksnapshot(const struct path *parent,
				   const char *name, int namelen,
				   struct btrfs_root *root,
				   bool readonly,
				   struct btrfs_qgroup_inherit *inherit
#ifdef MY_ABC_HERE
				   ,u64 copy_limit_from
#endif /* MY_ABC_HERE */
				   )
{
	int ret;
	bool snapshot_force_cow = false;

	/*
	 * Force new buffered writes to reserve space even when NOCOW is
	 * possible. This is to avoid later writeback (running dealloc) to
	 * fallback to COW mode and unexpectedly fail with ENOSPC.
	 */
	btrfs_drew_read_lock(&root->snapshot_lock);

	ret = btrfs_start_delalloc_snapshot(root);
	if (ret)
		goto out;

	/*
	 * All previous writes have started writeback in NOCOW mode, so now
	 * we force future writes to fallback to COW mode during snapshot
	 * creation.
	 */
	atomic_inc(&root->snapshot_force_cow);
	snapshot_force_cow = true;

	btrfs_wait_ordered_extents(root, U64_MAX, 0, (u64)-1);

	ret = btrfs_mksubvol(parent, name, namelen,
			     root, readonly, inherit
#ifdef MY_ABC_HERE
			    ,copy_limit_from
#endif /* MY_ABC_HERE */
			    );
out:
	if (snapshot_force_cow)
		atomic_dec(&root->snapshot_force_cow);
	btrfs_drew_read_unlock(&root->snapshot_lock);
	return ret;
}

/*
 * When we're defragging a range, we don't want to kick it off again
 * if it is really just waiting for delalloc to send it down.
 * If we find a nice big extent or delalloc range for the bytes in the
 * file you want to defrag, we return 0 to let you know to skip this
 * part of the file
 */
static int check_defrag_in_cache(struct inode *inode, u64 offset, u32 thresh)
{
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_map *em = NULL;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	u64 end;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, offset, PAGE_SIZE);
	read_unlock(&em_tree->lock);

	if (em) {
		end = extent_map_end(em);
		free_extent_map(em);
		if (end - offset > thresh)
			return 0;
	}
	/* if we already have a nice delalloc here, just stop */
	thresh /= 2;
	end = count_range_bits(io_tree, &offset, offset + thresh,
			       thresh, EXTENT_DELALLOC, 1);
	if (end >= thresh)
		return 0;
	return 1;
}

/*
 * helper function to walk through a file and find extents
 * newer than a specific transid, and smaller than thresh.
 *
 * This is used by the defragging code to find new and small
 * extents
 */
static int find_new_extents(struct btrfs_root *root,
			    struct inode *inode, u64 newer_than,
			    u64 *off, u32 thresh)
{
	struct btrfs_path *path;
	struct btrfs_key min_key;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *extent;
	int type;
	int ret;
	u64 ino = btrfs_ino(BTRFS_I(inode));

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	min_key.objectid = ino;
	min_key.type = BTRFS_EXTENT_DATA_KEY;
	min_key.offset = *off;

	while (1) {
		ret = btrfs_search_forward(root, &min_key, path, newer_than);
		if (ret != 0)
			goto none;
process_slot:
		if (min_key.objectid != ino)
			goto none;
		if (min_key.type != BTRFS_EXTENT_DATA_KEY)
			goto none;

		leaf = path->nodes[0];
		extent = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_file_extent_item);

		type = btrfs_file_extent_type(leaf, extent);
		if (type == BTRFS_FILE_EXTENT_REG &&
		    btrfs_file_extent_num_bytes(leaf, extent) < thresh &&
		    check_defrag_in_cache(inode, min_key.offset, thresh)) {
			*off = min_key.offset;
			btrfs_free_path(path);
			return 0;
		}

		path->slots[0]++;
		if (path->slots[0] < btrfs_header_nritems(leaf)) {
			btrfs_item_key_to_cpu(leaf, &min_key, path->slots[0]);
			goto process_slot;
		}

		if (min_key.offset == (u64)-1)
			goto none;

		min_key.offset++;
		btrfs_release_path(path);
	}
none:
	btrfs_free_path(path);
	return -ENOENT;
}

static struct extent_map *defrag_lookup_extent(struct inode *inode, u64 start)
{
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_map *em;
	u64 len = PAGE_SIZE;

	/*
	 * hopefully we have this extent in the tree already, try without
	 * the full extent lock
	 */
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	read_unlock(&em_tree->lock);

	if (!em) {
		struct extent_state *cached = NULL;
		u64 end = start + len - 1;

		/* get the big lock and read metadata off disk */
		lock_extent_bits(io_tree, start, end, &cached);
		em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, start, len);
		unlock_extent_cached(io_tree, start, end, &cached);

		if (IS_ERR(em))
			return NULL;
	}

	return em;
}

static bool defrag_check_next_extent(struct inode *inode, struct extent_map *em)
{
	struct extent_map *next;
	bool ret = true;

	/* this is the last extent */
	if (em->start + em->len >= i_size_read(inode))
		return false;

	next = defrag_lookup_extent(inode, em->start + em->len);
	if (!next || next->block_start >= EXTENT_MAP_LAST_BYTE)
		ret = false;
	else if ((em->block_start + em->block_len == next->block_start) &&
		 (em->block_len > SZ_128K && next->block_len > SZ_128K))
		ret = false;

	free_extent_map(next);
	return ret;
}

#ifdef MY_ABC_HERE
/*
 * Check if extent item usage is below threshold, this traverse the file
 * extent data item in the way that clone range does.
 */
static int reclaim_check_extent_usage(struct inode *inode,
			        struct btrfs_ioctl_defrag_range_args *range,
			        struct ulist *disko_ulist, u64 start, u64 *endoff, u64 *release_size)
{
	int ret = 0;
	int extent_rewrite = 0;
	int slot;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct ulist_node *unode;
	struct btrfs_path *path = NULL;
	struct btrfs_file_extent_item *item;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_inode *binode = BTRFS_I(inode);
	u8 type;
	u64 extent_item_use = 0;
	u32 syno_ratio_denom = 3; // Use 2/3 as default value
	u32 syno_ratio_nom = 2;
	u32 syno_thresh = 8 * 1024 * 1024; // Default thresh is 8MiB
	u64 extent_disko = 0;
	u64 extent_ram_bytes = 0;
	u64 extent_datao = 0;
	u64 num_bytes;
	u64 search_end = 0;
	u32 nritems;
	u64 relative_offset;
	bool skip_cross_ref_check = false, strict = false;

	if (range->syno_ratio_denom != 0 && range->syno_ratio_nom != 0) {
		syno_ratio_denom = range->syno_ratio_denom;
		syno_ratio_nom = range->syno_ratio_nom;
	}
	if (range->syno_thresh != 0)
		syno_thresh = (u32)range->syno_thresh * 4096;
	if (range->flags & BTRFS_DEFRAG_RANGE_SKIP_CROSS_REF_CHECK)
		skip_cross_ref_check = true;
	else if (range->flags & BTRFS_DEFRAG_RANGE_SKIP_FAST_SNAPSHOT_CHECK)
		strict = true;

	path = btrfs_alloc_path();
	if (!path) {
		extent_rewrite = -ENOMEM;
		goto out;
	}

	path->reada = READA_FORWARD;
	path->leave_spinning = 1;

	key.objectid = btrfs_ino(binode);
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = start;

again:
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		extent_rewrite = ret;
		goto out;
	}

	/*
	 * First search, if no extent item that starts at offset off was
	 * found but the previous item is an extent item, it's possible
	 * it might overlap our target range, therefore process it.
	 */
	if (key.offset == start && ret > 0 && path->slots[0] > 0) {
		btrfs_item_key_to_cpu(path->nodes[0], &key,
				      path->slots[0] - 1);
		if (key.type == BTRFS_EXTENT_DATA_KEY)
			path->slots[0]--;
	}
	nritems = btrfs_header_nritems(path->nodes[0]);
	if (path->slots[0] >= nritems) {
		ret = btrfs_next_leaf(root, path);
		if (ret < 0) {
			extent_rewrite = ret;
			goto out;
		}
		if (ret > 0) {
			*endoff = (u64) -1; // skip to the end
			goto out;
		}
	}
	leaf = path->nodes[0];
	slot = path->slots[0];

	btrfs_item_key_to_cpu(leaf, &key, slot);
	if (key.type > BTRFS_EXTENT_DATA_KEY ||
	    key.objectid != btrfs_ino(binode)) {
		*endoff = (u64) -1; // skip to the end
		goto out;
	}

	if (key.type != BTRFS_EXTENT_DATA_KEY) {
		btrfs_release_path(path);
		key.offset++;
		goto again;
	}
	item = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
	type = btrfs_file_extent_type(leaf, item);
	if (type == BTRFS_FILE_EXTENT_INLINE) {
		*endoff = (u64)-1; // skip to the end
		goto out;
	}
	extent_disko = btrfs_file_extent_disk_bytenr(leaf, item);
	extent_ram_bytes = btrfs_file_extent_ram_bytes(leaf, item);
	extent_datao = btrfs_file_extent_offset(leaf, item);
	num_bytes = btrfs_file_extent_num_bytes(leaf, item);

	*endoff = key.offset + num_bytes - 1;
	if (extent_disko == 0)
		goto out;

	unode = ulist_search(disko_ulist, extent_disko);
	if (unode) {
		btrfs_free_path(path);
		return unode->aux;
	}

	if (btrfs_file_extent_compression(leaf, item) ||
	    btrfs_file_extent_encryption(leaf, item) ||
	    btrfs_file_extent_other_encoding(leaf, item) ||
	    btrfs_extent_readonly(root->fs_info, extent_disko))
		goto add_list;

#ifdef MY_DEF_HERE
	if (skip_cross_ref_check) {
		// don't cow the data which we already dedupe while deduping reclaim
		if (BTRFS_FILE_EXTENT_DEDUPED & btrfs_file_extent_syno_flag(leaf, item))
			goto add_list;
	}
#endif

	/*
	 * If this EXTENT_ITEM spans across the file offset beyond our range,
	 * don't defrag it.
	 */
	relative_offset = key.offset - extent_datao;
	if (relative_offset >= LLONG_MAX)
		relative_offset = 0;
	if (relative_offset < range->start)
		goto add_list;

	btrfs_release_path(path);

	if (!skip_cross_ref_check) {
		/*
		 * There's possible race between the time this check is done
		 * and before we actuaully rewrite all extent data key that
		 * reference this extent item.
		 */
		ret = btrfs_cross_ref_exist(root, btrfs_ino(binode),
					    key.offset - extent_datao, extent_disko, strict);
		if (ret)
			goto add_list;
	}

	extent_item_use = num_bytes;
	search_end = key.offset + extent_ram_bytes - extent_datao;
	key.offset += num_bytes;
	while (1) {
		u64 disko, datal;
		u64 next_key_min_offset = key.offset + 1;

		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0) {
			extent_rewrite = ret;
			goto out;
		}
		nritems = btrfs_header_nritems(path->nodes[0]);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				extent_rewrite = ret;
				goto out;
			}
			if (ret > 0)
				break;
		}
		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.type != BTRFS_EXTENT_DATA_KEY ||
		    key.objectid != btrfs_ino(binode))
			break;
		if (key.offset > search_end)
			break;
		item = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
		type = btrfs_file_extent_type(leaf, item);
		if (type == BTRFS_FILE_EXTENT_INLINE)
			goto next;
		disko = btrfs_file_extent_disk_bytenr(leaf, item);
		datal = btrfs_file_extent_num_bytes(leaf, item);
		next_key_min_offset = key.offset + datal;
		/*
		 * This extent data points to a hole
		 */
		if (disko == 0)
			goto next;
		/*
		 * <---written---><---prealloc--->
		 * <------- extent item 1 ------->
		 * There are some parts of extent that are prealloc, so don't
		 * rewrite this. Otherwise, we'll end up like the following,
		 * <---written--->                 <---prealloc--->
		 * <extent item 2>  <------- extent item 1 ------->
		 */
		if (disko != extent_disko)
			goto next;
		if (type == BTRFS_FILE_EXTENT_PREALLOC)
			goto add_list;
		/*
		 * If this EXTENT_ITEM spans across the file offset beyond our range,
		 * don't reclaim it.
		 */
		if (range->len != (u64) -1 && range->len != 0 &&
		    key.offset + datal > range->start + range->len)
			goto add_list;
		extent_item_use += datal;
next:
		btrfs_release_path(path);
		key.offset = next_key_min_offset;
	}
	if (extent_item_use * syno_ratio_denom <= extent_ram_bytes * syno_ratio_nom ||
		extent_ram_bytes >= extent_item_use + syno_thresh) {
		extent_rewrite = 1;
		*release_size += extent_ram_bytes - extent_item_use;
	}
add_list:
	btrfs_release_path(path);
	/*
	 * bytenr is stored in val.
	 * If the extent_item is to be rewritten, we have aux = 1.
	 * Otherwise, aux = 0.
	 */
	if (ulist_add_lru_adjust(disko_ulist, extent_disko, extent_rewrite, GFP_NOFS) &&
		disko_ulist->nnodes > ULIST_NODES_MAX)
		ulist_remove_first(disko_ulist);
out:
	btrfs_free_path(path);
	return extent_rewrite;
}

static int reclaim_check_partial_used(struct inode *inode, u64 start, u64 *endoff,
					struct ulist *fileo_ulist, u64 *rewrite_size)
{
	int ret = 0;
	int slot;
	int extent_rewrite = 0;
	u64 disk_offset = 0, disk_bytenr = 0;
	u64 file_extent_start = 0, file_extent_num_bytes = 0;
	struct ulist_node *unode;
	struct btrfs_key key;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_path *path = NULL;
	struct btrfs_file_extent_item *item;
	struct extent_buffer *leaf;

	path = btrfs_alloc_path();
	if (!path) {
		extent_rewrite = -ENOMEM;
		goto out;
	}

	ret = btrfs_lookup_file_extent_by_file_offset(NULL, root, path,
					btrfs_ino(BTRFS_I(inode)), start, 0);
	if (0 > ret) {
		extent_rewrite = ret;
		goto out;
	}

	leaf = path->nodes[0];
	slot = path->slots[0];

	btrfs_item_key_to_cpu(leaf, &key, slot);
	item = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);

	disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, item);
	disk_offset = btrfs_file_extent_offset(leaf, item);
	file_extent_num_bytes = btrfs_file_extent_num_bytes(leaf, item);
	file_extent_start = key.offset;

	*endoff = key.offset + file_extent_num_bytes - 1;

	unode = ulist_search(fileo_ulist, file_extent_start);
	if (unode) {
		extent_rewrite = unode->aux;
		goto out;
	}

	if (BTRFS_FILE_EXTENT_REG != btrfs_file_extent_type(leaf, item) ||
		btrfs_file_extent_compression(leaf, item) ||
		btrfs_file_extent_encryption(leaf, item) ||
		btrfs_file_extent_other_encoding(leaf, item) ||
		btrfs_extent_readonly(root->fs_info, disk_bytenr))
		goto out;

	/* skip full used and hole */
	if (file_extent_num_bytes >= btrfs_file_extent_disk_num_bytes(leaf, item))
		goto out;

#ifdef MY_DEF_HERE
	// don't cow the data which we already dedupe while deduping reclaim
	if (BTRFS_FILE_EXTENT_DEDUPED & btrfs_file_extent_syno_flag(leaf, item))
		goto out;
#endif
	extent_rewrite = 1;
	*rewrite_size += file_extent_num_bytes;

out:
	if (ulist_add_lru_adjust(fileo_ulist, file_extent_start, extent_rewrite, GFP_NOFS) &&
		fileo_ulist->nnodes > ULIST_NODES_MAX)
		ulist_remove_first(fileo_ulist);

	btrfs_free_path(path);
	return extent_rewrite;
}

static int should_force_reclaim_range(struct inode *inode, u64 start,
			       u64 *skip, u64 *defrag_end, struct ulist *fileo_ulist,
				   u64 *rewrite_size)
{
	int ret;

	ret = reclaim_check_partial_used(inode, start, skip, fileo_ulist, rewrite_size);
	*defrag_end = *skip;
	return ret;
}

static int should_reclaim_range(struct inode *inode, u64 start,
			       u64 *skip, u64 *defrag_end,
			       struct btrfs_ioctl_defrag_range_args *range,
			       struct ulist *disko_ulist,
			       u64 *release_size)
{
	int ret;

	ret = reclaim_check_extent_usage(inode, range,
			        disko_ulist, start, skip, release_size);
	*defrag_end = *skip;
	return ret;
}
#endif /* MY_ABC_HERE */


static int should_defrag_range(struct inode *inode, u64 start, u32 thresh,
			       u64 *last_len, u64 *skip, u64 *defrag_end,
			       int compress)
{
	struct extent_map *em;
	int ret = 1;
	bool next_mergeable = true;
	bool prev_mergeable = true;

	/*
	 * make sure that once we start defragging an extent, we keep on
	 * defragging it
	 */
	if (start < *defrag_end)
		return 1;

	*skip = 0;

	em = defrag_lookup_extent(inode, start);
	if (!em)
		return 0;

	/* this will cover holes, and inline extents */
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		ret = 0;
		goto out;
	}

	if (!*defrag_end)
		prev_mergeable = false;

	next_mergeable = defrag_check_next_extent(inode, em);
	/*
	 * we hit a real extent, if it is big or the next extent is not a
	 * real extent, don't bother defragging it
	 */
	if (!compress && (*last_len == 0 || *last_len >= thresh) &&
	    (em->len >= thresh || (!next_mergeable && !prev_mergeable)))
		ret = 0;
out:
	/*
	 * last_len ends up being a counter of how many bytes we've defragged.
	 * every time we choose not to defrag an extent, we reset *last_len
	 * so that the next tiny extent will force a defrag.
	 *
	 * The end result of this is that tiny extents before a single big
	 * extent will force at least part of that big extent to be defragged.
	 */
	if (ret) {
		*defrag_end = extent_map_end(em);
	} else {
		*last_len = 0;
		*skip = extent_map_end(em);
		*defrag_end = 0;
	}

	free_extent_map(em);
	return ret;
}

/*
 * it doesn't do much good to defrag one or two pages
 * at a time.  This pulls in a nice chunk of pages
 * to COW and defrag.
 *
 * It also makes sure the delalloc code has enough
 * dirty data to avoid making new small extents as part
 * of the defrag
 *
 * It's a good idea to start RA on this range
 * before calling this.
 */
#ifdef MY_ABC_HERE
int cluster_pages_for_defrag(struct inode *inode,
#else
static int cluster_pages_for_defrag(struct inode *inode,
#endif /* MY_ABC_HERE */
				    struct page **pages,
				    unsigned long start_index,
				    unsigned long num_pages)
{
	unsigned long file_end;
	u64 isize = i_size_read(inode);
	u64 page_start;
	u64 page_end;
	u64 page_cnt;
	u64 start = (u64)start_index << PAGE_SHIFT;
	u64 search_start;
	int ret;
	int i;
	int i_done;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	struct extent_io_tree *tree;
	struct extent_changeset *data_reserved = NULL;
	gfp_t mask = btrfs_alloc_write_mask(inode->i_mapping);

	file_end = (isize - 1) >> PAGE_SHIFT;
	if (!isize || start_index > file_end)
		return 0;

	page_cnt = min_t(u64, (u64)num_pages, (u64)file_end - start_index + 1);

	ret = btrfs_delalloc_reserve_space(BTRFS_I(inode), &data_reserved,
			start, page_cnt << PAGE_SHIFT);
	if (ret)
		return ret;
	i_done = 0;
	tree = &BTRFS_I(inode)->io_tree;

	/* step one, lock all the pages */
	for (i = 0; i < page_cnt; i++) {
		struct page *page;
again:
		page = find_or_create_page(inode->i_mapping,
					   start_index + i, mask);
		if (!page)
			break;

		page_start = page_offset(page);
		page_end = page_start + PAGE_SIZE - 1;
		while (1) {
			lock_extent_bits(tree, page_start, page_end,
					 &cached_state);
			ordered = btrfs_lookup_ordered_extent(BTRFS_I(inode),
							      page_start);
			unlock_extent_cached(tree, page_start, page_end,
					     &cached_state);
			if (!ordered)
				break;

			unlock_page(page);
			btrfs_start_ordered_extent(ordered, 1);
			btrfs_put_ordered_extent(ordered);
			lock_page(page);
			/*
			 * we unlocked the page above, so we need check if
			 * it was released or not.
			 */
			if (page->mapping != inode->i_mapping) {
				unlock_page(page);
				put_page(page);
				goto again;
			}
		}

		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				put_page(page);
				ret = -EIO;
				break;
			}
		}

		if (page->mapping != inode->i_mapping) {
			unlock_page(page);
			put_page(page);
			goto again;
		}

		pages[i] = page;
		i_done++;
	}
	if (!i_done || ret)
		goto out;

	if (!(inode->i_sb->s_flags & SB_ACTIVE))
		goto out;

	/*
	 * so now we have a nice long stream of locked
	 * and up to date pages, lets wait on them
	 */
	for (i = 0; i < i_done; i++)
		wait_on_page_writeback(pages[i]);

	page_start = page_offset(pages[0]);
	page_end = page_offset(pages[i_done - 1]) + PAGE_SIZE;

	lock_extent_bits(&BTRFS_I(inode)->io_tree,
			 page_start, page_end - 1, &cached_state);

	/*
	 * When defragmenting we skip ranges that have holes or inline extents,
	 * (check should_defrag_range()), to avoid unnecessary IO and wasting
	 * space. At btrfs_defrag_file(), we check if a range should be defragged
	 * before locking the inode and then, if it should, we trigger a sync
	 * page cache readahead - we lock the inode only after that to avoid
	 * blocking for too long other tasks that possibly want to operate on
	 * other file ranges. But before we were able to get the inode lock,
	 * some other task may have punched a hole in the range, or we may have
	 * now an inline extent, in which case we should not defrag. So check
	 * for that here, where we have the inode and the range locked, and bail
	 * out if that happened.
	 */
	search_start = page_start;
	while (search_start < page_end) {
		struct extent_map *em;

		em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, search_start,
				      page_end - search_start);
		if (IS_ERR(em)) {
			ret = PTR_ERR(em);
			goto out_unlock_range;
		}
		if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
			free_extent_map(em);
			/* Ok, 0 means we did not defrag anything */
			ret = 0;
			goto out_unlock_range;
		}
		search_start = extent_map_end(em);
		free_extent_map(em);
	}

	clear_extent_bit(&BTRFS_I(inode)->io_tree, page_start,
			  page_end - 1, EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING |
			  EXTENT_DEFRAG, 0, 0, &cached_state);

	if (i_done != page_cnt) {
		spin_lock(&BTRFS_I(inode)->lock);
		btrfs_mod_outstanding_extents(BTRFS_I(inode), 1);
		spin_unlock(&BTRFS_I(inode)->lock);
		btrfs_delalloc_release_space(BTRFS_I(inode), data_reserved,
				start, (page_cnt - i_done) << PAGE_SHIFT, true);
	}


	set_extent_defrag(&BTRFS_I(inode)->io_tree, page_start, page_end - 1,
			  &cached_state);

	unlock_extent_cached(&BTRFS_I(inode)->io_tree,
			     page_start, page_end - 1, &cached_state);

	for (i = 0; i < i_done; i++) {
		clear_page_dirty_for_io(pages[i]);
		ClearPageChecked(pages[i]);
		set_page_extent_mapped(pages[i]);
		set_page_dirty(pages[i]);
		unlock_page(pages[i]);
		put_page(pages[i]);
	}
	btrfs_delalloc_release_extents(BTRFS_I(inode), page_cnt << PAGE_SHIFT);
	extent_changeset_free(data_reserved);
	return i_done;

out_unlock_range:
	unlock_extent_cached(&BTRFS_I(inode)->io_tree,
			     page_start, page_end - 1, &cached_state);
out:
	for (i = 0; i < i_done; i++) {
		unlock_page(pages[i]);
		put_page(pages[i]);
	}
	btrfs_delalloc_release_space(BTRFS_I(inode), data_reserved,
			start, page_cnt << PAGE_SHIFT, true);
	btrfs_delalloc_release_extents(BTRFS_I(inode), page_cnt << PAGE_SHIFT);
	extent_changeset_free(data_reserved);
	return ret;

}

#ifdef MY_ABC_HERE
extern int write_buf(struct file *filp, const void *buf, u32 len, loff_t *off);
#endif /* MY_ABC_HERE */

int btrfs_defrag_file(struct inode *inode, struct file *file,
		      struct btrfs_ioctl_defrag_range_args *range,
		      u64 newer_than, unsigned long max_to_defrag)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct file_ra_state *ra = NULL;
	unsigned long last_index;
	u64 isize = i_size_read(inode);
	u64 last_len = 0;
	u64 skip = 0;
	u64 defrag_end = 0;
	u64 newer_off = range->start;
	unsigned long i;
	unsigned long ra_index = 0;
	int ret;
	int defrag_count = 0;
#ifdef MY_ABC_HERE
	int compress_type = BTRFS_COMPRESS_DEFAULT;
#else
	int compress_type = BTRFS_COMPRESS_ZLIB;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	u64 last_rec_pos = 0;
	u64 one_tenth_isize = i_size_read(inode) / 10;
	int should_defrag_range_ret = 0;
	int defrag_success = 0;
	struct ulist *disko_ulist = NULL;
	struct ulist *fileo_ulist = NULL;
	struct ulist *orig_extent = NULL;
	time64_t last_show = ktime_get_seconds();
	int print_stdout = 0;
	u64 release_size = 0, rewrite_size = 0;
	struct file *file_stdout = NULL;
	loff_t off;
	char buf[512];
#endif /* MY_ABC_HERE */
	u32 extent_thresh = range->extent_thresh;
	unsigned long max_cluster = SZ_256K >> PAGE_SHIFT;
	unsigned long cluster = max_cluster;
	u64 new_align = ~((u64)SZ_128K - 1);
	struct page **pages = NULL;
	bool do_compress = range->flags & BTRFS_DEFRAG_RANGE_COMPRESS;

	if (isize == 0)
		return 0;

#ifdef MY_ABC_HERE
	if (range->flags & BTRFS_DEFRAG_RANGE_SYNO_DEFRAG &&
	    range->flags & BTRFS_DEFRAG_RANGE_PRINT_STDOUT) {
		memset(buf, 0, sizeof(buf));
		off = 0;
		snprintf(buf, sizeof(buf), "[syno defrag] root:%llu ino:%llu "
		        "start:%llu len:%llu thresh:%u dem:%u nom:%u\n",
		        root->root_key.objectid, btrfs_ino(BTRFS_I(inode)),
		        range->start, range->len,
		        range->syno_thresh, range->syno_ratio_denom,
		        range->syno_ratio_nom);
		file_stdout = fget(1);
		write_buf(file_stdout, buf, sizeof(buf), &off);
		if (one_tenth_isize < 256 * 1024 * 1024)
			one_tenth_isize = 256 * 1024 * 1024;
	}
	i = 0; // To avoid use maybe-uninitialized warning
#endif /* MY_ABC_HERE */
	if (range->start >= isize)
		return -EINVAL;

	if (do_compress) {
		if (range->compress_type >= BTRFS_NR_COMPRESS_TYPES)
			return -EINVAL;
		if (range->compress_type)
			compress_type = range->compress_type;
	}

	if (extent_thresh == 0)
		extent_thresh = SZ_256K;

	/*
	 * If we were not given a file, allocate a readahead context. As
	 * readahead is just an optimization, defrag will work without it so
	 * we don't error out.
	 */
	if (!file) {
		ra = kzalloc(sizeof(*ra), GFP_KERNEL);
		if (ra)
			file_ra_state_init(ra, inode->i_mapping);
	} else {
		ra = &file->f_ra;
	}

#ifdef MY_ABC_HERE
	if (range->flags & BTRFS_DEFRAG_RANGE_SYNO_DEFRAG) {
		disko_ulist = ulist_alloc(GFP_NOFS);
		if (!disko_ulist) {
			ret = -ENOMEM;
			goto out_ra;
		}
	} else if (range->flags & BTRFS_DEFRAG_RANGE_FORCE_RECLAIM) {
		fileo_ulist = ulist_alloc(GFP_NOFS);
		if (!fileo_ulist) {
			ret = -ENOMEM;
			goto out_ra;
		}
		orig_extent = ulist_alloc(GFP_NOFS);
		if (!orig_extent) {
			ret = -ENOMEM;
			goto out_ra;
		}
		ret = get_extent_item_list(inode, range->start, range->len, orig_extent);
		if (0 > ret)
			goto out_ra;
	}
#endif /* MY_ABC_HERE */
	pages = kmalloc_array(max_cluster, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto out_ra;
	}

	/* find the last page to defrag */
	if (range->start + range->len > range->start) {
		last_index = min_t(u64, isize - 1,
			 range->start + range->len - 1) >> PAGE_SHIFT;
	} else {
		last_index = (isize - 1) >> PAGE_SHIFT;
	}

	if (newer_than) {
		ret = find_new_extents(root, inode, newer_than,
				       &newer_off, SZ_64K);
		if (!ret) {
			range->start = newer_off;
			/*
			 * we always align our defrag to help keep
			 * the extents in the file evenly spaced
			 */
			i = (newer_off & new_align) >> PAGE_SHIFT;
		} else
			goto out_ra;
	} else {
		i = range->start >> PAGE_SHIFT;
	}
	if (!max_to_defrag)
		max_to_defrag = last_index - i + 1;

	/*
	 * make writeback starts from i, so the defrag range can be
	 * written sequentially.
	 */
	if (i < inode->i_mapping->writeback_index)
		inode->i_mapping->writeback_index = i;

	while (i <= last_index && defrag_count < max_to_defrag &&
	       (i < DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE))) {
		/*
		 * make sure we stop running if someone unmounts
		 * the FS
		 */
		if (!(inode->i_sb->s_flags & SB_ACTIVE))
			break;

		if (btrfs_defrag_cancelled(fs_info)) {
			btrfs_debug(fs_info, "defrag_file cancelled");
			ret = -EAGAIN;
			break;
		}

#ifdef MY_ABC_HERE
		if (range->flags & BTRFS_DEFRAG_RANGE_SYNO_DEFRAG) {
			if (range->flags & BTRFS_DEFRAG_RANGE_PRINT_STDOUT) {
				if (((u64)i << PAGE_SHIFT) - last_rec_pos >= one_tenth_isize) {
					last_rec_pos = (u64)i << PAGE_SHIFT;
					print_stdout = 1;
				}
				if (print_stdout || ktime_get_seconds() - last_show > 60) {
					memset(buf, 0, sizeof(buf));
					off = 0;
					snprintf(buf, sizeof(buf), "[syno defrag status] root:%llu ino:%llu "
						                       "progress:%lu/%lu release size:%llu\n",
							    root->root_key.objectid, btrfs_ino(BTRFS_I(inode)),
					            i, last_index, release_size);
					write_buf(file_stdout, buf, sizeof(buf), &off);
					last_show = ktime_get_seconds();
					print_stdout = 0;
				}
			}
			should_defrag_range_ret = should_reclaim_range(inode, (u64)i << PAGE_SHIFT,
						 &skip, &defrag_end, range, disko_ulist, &release_size);
			if (should_defrag_range_ret < 0) {
				ret = should_defrag_range_ret;
				goto out_ra;
			}
			if (!should_defrag_range_ret) {
				unsigned long next;
				if (skip == (u64) -1)
					break;
				/*
				 * the should_defrag function tells us how much to skip
				 * bump our counter by the suggested amount
				 */
				next = DIV_ROUND_UP(skip, PAGE_SIZE);
				i = max(i + 1, next);
				continue;
			}
		} else if (range->flags & BTRFS_DEFRAG_RANGE_FORCE_RECLAIM) {
			should_defrag_range_ret = should_force_reclaim_range(inode, (u64)i << PAGE_SHIFT,
						 &skip, &defrag_end, fileo_ulist, &rewrite_size);
			if (should_defrag_range_ret < 0) {
				ret = should_defrag_range_ret;
				goto out_ra;
			}
			if (!should_defrag_range_ret) {
				unsigned long next;
				if (skip == (u64) -1)
					break;
				/*
				 * the should_defrag function tells us how much to skip
				 * bump our counter by the suggested amount
				 */
				next = DIV_ROUND_UP(skip, PAGE_SIZE);
				i = max(i + 1, next);
				continue;
			}
		} else
#endif /* MY_ABC_HERE */
		if (!should_defrag_range(inode, (u64)i << PAGE_SHIFT,
					 extent_thresh, &last_len, &skip,
					 &defrag_end, do_compress)){
			unsigned long next;
			/*
			 * the should_defrag function tells us how much to skip
			 * bump our counter by the suggested amount
			 */
			next = DIV_ROUND_UP(skip, PAGE_SIZE);
			i = max(i + 1, next);
			continue;
		}

		if (!newer_than) {
			cluster = (PAGE_ALIGN(defrag_end) >>
				   PAGE_SHIFT) - i;
			cluster = min(cluster, max_cluster);
		} else {
			cluster = max_cluster;
		}

		if (i + cluster > ra_index) {
			ra_index = max(i, ra_index);
			if (ra)
				page_cache_sync_readahead(inode->i_mapping, ra,
						file, ra_index, cluster);
			ra_index += cluster;
		}

		inode_lock(inode);
		if (IS_SWAPFILE(inode)) {
			ret = -ETXTBSY;
		} else {
			if (do_compress)
				BTRFS_I(inode)->defrag_compress = compress_type;
			ret = cluster_pages_for_defrag(inode, pages, i, cluster);
		}
		if (ret < 0) {
			inode_unlock(inode);
			goto out_ra;
		}
#ifdef MY_ABC_HERE
		defrag_success = 1;
#endif /* MY_ABC_HERE */

		defrag_count += ret;
		balance_dirty_pages_ratelimited(inode->i_mapping);
		inode_unlock(inode);

		if (newer_than) {
			if (newer_off == (u64)-1)
				break;

			if (ret > 0)
				i += ret;

			newer_off = max(newer_off + 1,
					(u64)i << PAGE_SHIFT);

			ret = find_new_extents(root, inode, newer_than,
					       &newer_off, SZ_64K);
			if (!ret) {
				range->start = newer_off;
				i = (newer_off & new_align) >> PAGE_SHIFT;
			} else {
				break;
			}
		} else {
			if (ret > 0) {
				i += ret;
				last_len += ret << PAGE_SHIFT;
			} else {
				i++;
				last_len = 0;
			}
		}
	}

#ifdef MY_ABC_HERE
	if (defrag_success && (range->flags & BTRFS_DEFRAG_RANGE_START_IO_RANGE)) {
		btrfs_wait_ordered_range(inode, range->start, range->len);
	} else
#endif /* MY_ABC_HERE */
	if ((range->flags & BTRFS_DEFRAG_RANGE_START_IO)) {
		filemap_flush(inode->i_mapping);
		if (test_bit(BTRFS_INODE_HAS_ASYNC_EXTENT,
			     &BTRFS_I(inode)->runtime_flags))
			filemap_flush(inode->i_mapping);
	}

	if (range->compress_type == BTRFS_COMPRESS_LZO) {
		btrfs_set_fs_incompat(fs_info, COMPRESS_LZO);
	} else if (range->compress_type == BTRFS_COMPRESS_ZSTD) {
		btrfs_set_fs_incompat(fs_info, COMPRESS_ZSTD);
	}

	ret = defrag_count;

out_ra:
	if (do_compress) {
		inode_lock(inode);
		BTRFS_I(inode)->defrag_compress = BTRFS_COMPRESS_NONE;
		inode_unlock(inode);
	}
#ifdef MY_ABC_HERE
	if (range->flags & BTRFS_DEFRAG_RANGE_SYNO_DEFRAG) {
		if (range->flags & BTRFS_DEFRAG_RANGE_PRINT_STDOUT) {
			memset(buf, 0, sizeof(buf));
			off = 0;
			snprintf(buf, sizeof(buf), "[syno defrag] finish root:%llu ino:%llu "
				"end_pos: %lu release size:%llu\n",
				root->root_key.objectid, btrfs_ino(BTRFS_I(inode)), i, release_size);
			write_buf(file_stdout, buf, sizeof(buf), &off);
		}
		range->release_size = release_size;
		ulist_free(disko_ulist);
	} else if (range->flags & BTRFS_DEFRAG_RANGE_FORCE_RECLAIM) {
		u64 release_extent_size = 0;
		if (!extent_same_release_size_accounting(orig_extent, root, &release_extent_size)) {
			/*
			 * rewrite_size may larger than released in force reclaim,
			 * but range->release_size is unsigned, user space should handle it.
			 */
			range->release_size = release_extent_size - rewrite_size;
		}
		ulist_free(fileo_ulist);
		ulist_free(orig_extent);
	}
	if (file_stdout)
		fput(file_stdout);
#endif /* MY_ABC_HERE */
	if (!file)
		kfree(ra);
	kfree(pages);
	return ret;
}

static noinline int btrfs_ioctl_resize(struct file *file,
					void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 new_size;
	u64 old_size;
	u64 devid = 1;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_vol_args *vol_args;
	struct btrfs_trans_handle *trans;
	struct btrfs_device *device = NULL;
	char *sizestr;
	char *retptr;
	char *devstr = NULL;
	int ret = 0;
	int mod = 0;
#ifdef MY_ABC_HERE
	int dry_run = 0;
#endif /* MY_ABC_HERE */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	if (!btrfs_exclop_start(fs_info, BTRFS_EXCLOP_RESIZE)) {
		mnt_drop_write_file(file);
		return BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
	}

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args)) {
		ret = PTR_ERR(vol_args);
		goto out;
	}

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';

	sizestr = vol_args->name;
	devstr = strchr(sizestr, ':');
	if (devstr) {
		sizestr = devstr + 1;
		*devstr = '\0';
		devstr = vol_args->name;
		ret = kstrtoull(devstr, 10, &devid);
		if (ret)
			goto out_free;
		if (!devid) {
			ret = -EINVAL;
			goto out_free;
		}
		btrfs_info(fs_info, "resizing devid %llu", devid);
	}

	device = btrfs_find_device(fs_info->fs_devices, devid, NULL, NULL, true);
	if (!device) {
		btrfs_info(fs_info, "resizer unable to find device %llu",
			   devid);
		ret = -ENODEV;
		goto out_free;
	}

	if (!test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		btrfs_info(fs_info,
			   "resizer unable to apply on readonly device %llu",
		       devid);
		ret = -EPERM;
		goto out_free;
	}

	if (!strcmp(sizestr, "max"))
		new_size = device->bdev->bd_inode->i_size;
	else {
		if (sizestr[0] == '-') {
			mod = -1;
			sizestr++;
		} else if (sizestr[0] == '+') {
			mod = 1;
			sizestr++;
#ifdef MY_ABC_HERE
			if (sizestr[0] == '?') {
				dry_run = 1;
				sizestr++;
			}
#endif /* MY_ABC_HERE */
		}
		new_size = memparse(sizestr, &retptr);
		if (*retptr != '\0' || new_size == 0) {
			ret = -EINVAL;
			goto out_free;
		}
	}

	if (test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state)) {
		ret = -EPERM;
		goto out_free;
	}

	old_size = btrfs_device_get_total_bytes(device);

	if (mod < 0) {
		if (new_size > old_size) {
			ret = -EINVAL;
			goto out_free;
		}
		new_size = old_size - new_size;
	} else if (mod > 0) {
		if (new_size > ULLONG_MAX - old_size) {
			ret = -ERANGE;
			goto out_free;
		}
		new_size = old_size + new_size;
	}

	if (new_size < SZ_256M) {
		ret = -EINVAL;
		goto out_free;
	}
	if (new_size > device->bdev->bd_inode->i_size) {
		ret = -EFBIG;
		goto out_free;
	}
#ifdef MY_ABC_HERE
	if (dry_run)
		goto out_free;
#endif /* MY_ABC_HERE */

	new_size = round_down(new_size, fs_info->sectorsize);

	if (new_size > old_size) {
		trans = btrfs_start_transaction(root, 0);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out_free;
		}
		ret = btrfs_grow_device(trans, device, new_size);
		btrfs_commit_transaction(trans);
	} else if (new_size < old_size) {
		ret = btrfs_shrink_device(device, new_size);
	} /* equal, nothing need to do */

	if (ret == 0 && new_size != old_size)
		btrfs_info_in_rcu(fs_info,
			"resize device %s (devid %llu) from %llu to %llu",
			rcu_str_deref(device->name), device->devid,
			old_size, new_size);
out_free:
	kfree(vol_args);
out:
	btrfs_exclop_finish(fs_info);
	mnt_drop_write_file(file);
	return ret;
}

static noinline int __btrfs_ioctl_snap_create(struct file *file,
				const char *name, unsigned long fd, int subvol,
				bool readonly,
				struct btrfs_qgroup_inherit *inherit
#ifdef MY_ABC_HERE
				,u64 copy_limit_from
#endif /* MY_ABC_HERE */
				)
{
	int namelen;
	int ret = 0;

	if (!S_ISDIR(file_inode(file)->i_mode))
		return -ENOTDIR;

	ret = mnt_want_write_file(file);
	if (ret)
		goto out;

	namelen = strlen(name);
	if (strchr(name, '/')) {
		ret = -EINVAL;
		goto out_drop_write;
	}

	if (name[0] == '.' &&
	   (namelen == 1 || (name[1] == '.' && namelen == 2))) {
		ret = -EEXIST;
		goto out_drop_write;
	}

	if (subvol) {
		ret = btrfs_mksubvol(&file->f_path, name, namelen,
				     NULL, readonly, inherit
#ifdef MY_ABC_HERE
				    ,copy_limit_from
#endif /* MY_ABC_HERE */
				    );
	} else {
		struct fd src = fdget(fd);
		struct inode *src_inode;
		if (!src.file) {
			ret = -EINVAL;
			goto out_drop_write;
		}

		src_inode = file_inode(src.file);
		if (src_inode->i_sb != file_inode(file)->i_sb) {
			btrfs_info(BTRFS_I(file_inode(file))->root->fs_info,
				   "Snapshot src from another FS");
			ret = -EXDEV;
		} else if (!inode_owner_or_capable(src_inode)) {
			/*
			 * Subvolume creation is not restricted, but snapshots
			 * are limited to own subvolumes only
			 */
			ret = -EPERM;
		} else {
			ret = btrfs_mksnapshot(&file->f_path, name, namelen,
					     BTRFS_I(src_inode)->root,
					     readonly, inherit
#ifdef MY_ABC_HERE
					     ,copy_limit_from
#endif /* MY_ABC_HERE */
					     );
		}
		fdput(src);
	}
out_drop_write:
	mnt_drop_write_file(file);
out:
	return ret;
}

static noinline int btrfs_ioctl_snap_create(struct file *file,
					    void __user *arg, int subvol)
{
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	if (!S_ISDIR(file_inode(file)->i_mode))
		return -ENOTDIR;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);
	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';

	ret = __btrfs_ioctl_snap_create(file, vol_args->name, vol_args->fd,
					subvol, false, NULL
#ifdef MY_ABC_HERE
					,0
#endif /* MY_ABC_HERE */
					);

	kfree(vol_args);
	return ret;
}

static noinline int btrfs_ioctl_snap_create_v2(struct file *file,
					       void __user *arg, int subvol)
{
	struct btrfs_ioctl_vol_args_v2 *vol_args;
	int ret;
	bool readonly = false;
	struct btrfs_qgroup_inherit *inherit = NULL;

	if (!S_ISDIR(file_inode(file)->i_mode))
		return -ENOTDIR;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);
	vol_args->name[BTRFS_SUBVOL_NAME_MAX] = '\0';

	if (vol_args->flags & ~BTRFS_SUBVOL_CREATE_ARGS_MASK) {
		ret = -EOPNOTSUPP;
		goto free_args;
	}

	if (vol_args->flags & BTRFS_SUBVOL_RDONLY)
		readonly = true;
	if (vol_args->flags & BTRFS_SUBVOL_QGROUP_INHERIT) {
		u64 nums;

		if (vol_args->size < sizeof(*inherit) ||
		    vol_args->size > PAGE_SIZE) {
			ret = -EINVAL;
			goto free_args;
		}
		inherit = memdup_user(vol_args->qgroup_inherit, vol_args->size);
		if (IS_ERR(inherit)) {
			ret = PTR_ERR(inherit);
			goto free_args;
		}

		if (inherit->num_qgroups > PAGE_SIZE ||
		    inherit->num_ref_copies > PAGE_SIZE ||
		    inherit->num_excl_copies > PAGE_SIZE) {
			ret = -EINVAL;
			goto free_inherit;
		}

		nums = inherit->num_qgroups + 2 * inherit->num_ref_copies +
		       2 * inherit->num_excl_copies;
		if (vol_args->size != struct_size(inherit, qgroups, nums)) {
			ret = -EINVAL;
			goto free_inherit;
		}
	}

	ret = __btrfs_ioctl_snap_create(file, vol_args->name, vol_args->fd,
					subvol, readonly, inherit
#ifdef MY_ABC_HERE
					,vol_args->copy_limit_from
#endif /* MY_ABC_HERE */
					);
	if (ret)
		goto free_inherit;
free_inherit:
	kfree(inherit);
free_args:
	kfree(vol_args);
	return ret;
}

static noinline int btrfs_ioctl_subvol_getflags(struct file *file,
						void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;
	u64 flags = 0;

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID)
		return -EINVAL;

	down_read(&fs_info->subvol_sem);
	if (btrfs_root_readonly(root))
		flags |= BTRFS_SUBVOL_RDONLY;
#ifdef MY_ABC_HERE
	if (btrfs_root_hide(root))
		flags |= BTRFS_SUBVOL_HIDE;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	if (btrfs_root_disable_quota(root))
		flags |= BTRFS_SUBVOL_DISABLE_QUOTA;
	if (btrfs_root_noload_usrquota(root))
		flags |= BTRFS_SUBVOL_NOLOAD_USRQUOTA;
	if (btrfs_root_cmpr_ratio(root))
		flags |= BTRFS_SUBVOL_CMPR_RATIO;
#endif /* MY_ABC_HERE */
	up_read(&fs_info->subvol_sem);

	if (copy_to_user(arg, &flags, sizeof(flags)))
		ret = -EFAULT;

	return ret;
}

static noinline int btrfs_ioctl_subvol_setflags(struct file *file,
					      void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	u64 root_flags;
	u64 flags;
	int ret = 0;
#if defined(MY_ABC_HERE) || \
    defined(MY_ABC_HERE)
        u64 mask = BTRFS_SUBVOL_RDONLY;
#endif /* MY_ABC_HERE ||
	  MY_ABC_HERE */

	if (!inode_owner_or_capable(inode))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		goto out;

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID) {
		ret = -EINVAL;
		goto out_drop_write;
	}

	if (copy_from_user(&flags, arg, sizeof(flags))) {
		ret = -EFAULT;
		goto out_drop_write;
	}

#ifdef MY_ABC_HERE
	mask |= BTRFS_SUBVOL_HIDE;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	mask |= BTRFS_SUBVOL_NOLOAD_USRQUOTA;
	mask |= BTRFS_SUBVOL_CMPR_RATIO;
	mask |= BTRFS_SUBVOL_DISABLE_QUOTA;
#endif /* MY_ABC_HERE */

#if defined(MY_ABC_HERE) || \
    defined(MY_ABC_HERE)
	if (flags & ~mask) {
#else
	if (flags & ~BTRFS_SUBVOL_RDONLY) {
#endif /* MY_ABC_HERE ||
	  MY_ABC_HERE */
		ret = -EOPNOTSUPP;
		goto out_drop_write;
	}

	down_write(&fs_info->subvol_sem);

#ifdef MY_ABC_HERE
	if (!!(flags & BTRFS_SUBVOL_HIDE) != btrfs_root_hide(root))
		goto update_flags;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	if (!!(flags & BTRFS_SUBVOL_DISABLE_QUOTA) != btrfs_root_disable_quota(root))
		goto update_flags;
	if (!!(flags & BTRFS_SUBVOL_NOLOAD_USRQUOTA) != btrfs_root_noload_usrquota(root))
		goto update_flags;
	if (!!(flags & BTRFS_SUBVOL_CMPR_RATIO) != btrfs_root_cmpr_ratio(root))
		goto update_flags;
#endif /* MY_ABC_HERE */
	/* nothing to do */
	if (!!(flags & BTRFS_SUBVOL_RDONLY) == btrfs_root_readonly(root))
		goto out_drop_sem;

#if defined(MY_ABC_HERE) || \
    defined(MY_ABC_HERE)
update_flags:
#endif /* MY_ABC_HERE ||
	  MY_ABC_HERE */
	root_flags = btrfs_root_flags(&root->root_item);
	if (flags & BTRFS_SUBVOL_RDONLY) {
		btrfs_set_root_flags(&root->root_item,
				     root_flags | BTRFS_ROOT_SUBVOL_RDONLY);
#ifdef MY_ABC_HERE
		if (test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) &&
		    test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state)) {
			spin_lock(&root->syno_usage_lock);
			if (!(root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY)) {
				spin_lock(&fs_info->syno_usage_lock);
				if (fs_info->syno_usage_status.total_syno_subvol_usage_items >= root->syno_usage_root_status.total_syno_subvol_usage_items)
					fs_info->syno_usage_status.total_syno_subvol_usage_items -= root->syno_usage_root_status.total_syno_subvol_usage_items;
				else
					fs_info->syno_usage_status.total_syno_subvol_usage_items = 0;
				spin_unlock(&fs_info->syno_usage_lock);
			}
			root->syno_usage_root_status.flags |= BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY;
			spin_unlock(&root->syno_usage_lock);
		}
#endif /* MY_ABC_HERE */
	} else {
		/*
		 * Block RO -> RW transition if this subvolume is involved in
		 * send
		 */
		spin_lock(&root->root_item_lock);
		if (root->send_in_progress == 0) {
			btrfs_set_root_flags(&root->root_item,
				     root_flags & ~BTRFS_ROOT_SUBVOL_RDONLY);
#ifdef MY_ABC_HERE
			if (test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) &&
			    test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state)) {
				spin_lock(&root->syno_usage_lock);
				if (root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY) {
					spin_lock(&fs_info->syno_usage_lock);
					fs_info->syno_usage_status.total_syno_subvol_usage_items += root->syno_usage_root_status.total_syno_subvol_usage_items;
					spin_unlock(&fs_info->syno_usage_lock);
				}
				root->syno_usage_root_status.flags &= ~BTRFS_SYNO_USAGE_ROOT_FLAG_READONLY;
				spin_unlock(&root->syno_usage_lock);
			}
#endif /* MY_ABC_HERE */
			spin_unlock(&root->root_item_lock);
		} else {
			spin_unlock(&root->root_item_lock);
			btrfs_warn(fs_info,
				   "Attempt to set subvolume %llu read-write during send",
				   root->root_key.objectid);
			ret = -EPERM;
			goto out_drop_sem;
		}
	}

#ifdef MY_ABC_HERE
	root_flags = btrfs_root_flags(&root->root_item);
	if (flags & BTRFS_SUBVOL_HIDE)
		btrfs_set_root_flags(&root->root_item,
				root_flags | BTRFS_ROOT_SUBVOL_HIDE);
	else
		btrfs_set_root_flags(&root->root_item,
				root_flags & ~BTRFS_ROOT_SUBVOL_HIDE);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	root_flags = btrfs_root_flags(&root->root_item);
	if (flags & BTRFS_SUBVOL_DISABLE_QUOTA)
		btrfs_set_root_flags(&root->root_item,
					root_flags | BTRFS_ROOT_SUBVOL_DISABLE_QUOTA);
	else
		btrfs_set_root_flags(&root->root_item,
					root_flags & ~BTRFS_ROOT_SUBVOL_DISABLE_QUOTA);

	root_flags = btrfs_root_flags(&root->root_item);
	if (flags & BTRFS_SUBVOL_CMPR_RATIO)
		btrfs_set_root_flags(&root->root_item,
					root_flags | BTRFS_ROOT_SUBVOL_CMPR_RATIO);
	else
		btrfs_set_root_flags(&root->root_item,
					root_flags & ~BTRFS_ROOT_SUBVOL_CMPR_RATIO);

	root_flags = btrfs_root_flags(&root->root_item);
	if (flags & BTRFS_SUBVOL_NOLOAD_USRQUOTA)
		btrfs_set_root_flags(&root->root_item,
				root_flags | BTRFS_ROOT_SUBVOL_NOLOAD_USRQUOTA);
	else
		btrfs_set_root_flags(&root->root_item,
				root_flags & ~BTRFS_ROOT_SUBVOL_NOLOAD_USRQUOTA);
#endif /* MY_ABC_HERE */

	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_reset;
	}

	ret = btrfs_update_root(trans, fs_info->tree_root,
				&root->root_key, &root->root_item);
	if (ret < 0) {
		btrfs_end_transaction(trans);
		goto out_reset;
	}

	ret = btrfs_commit_transaction(trans);

out_reset:
	if (ret)
		btrfs_set_root_flags(&root->root_item, root_flags);
out_drop_sem:
	up_write(&fs_info->subvol_sem);
out_drop_write:
	mnt_drop_write_file(file);
out:
	return ret;
}

static noinline int key_in_sk(struct btrfs_key *key,
			      struct btrfs_ioctl_search_key *sk)
{
	struct btrfs_key test;
	int ret;

	test.objectid = sk->min_objectid;
	test.type = sk->min_type;
	test.offset = sk->min_offset;

	ret = btrfs_comp_cpu_keys(key, &test);
	if (ret < 0)
		return 0;

	test.objectid = sk->max_objectid;
	test.type = sk->max_type;
	test.offset = sk->max_offset;

	ret = btrfs_comp_cpu_keys(key, &test);
	if (ret > 0)
		return 0;
	return 1;
}

static noinline int copy_to_sk(struct btrfs_path *path,
			       struct btrfs_key *key,
			       struct btrfs_ioctl_search_key *sk,
			       size_t *buf_size,
			       char __user *ubuf,
			       unsigned long *sk_offset,
			       int *num_found)
{
	u64 found_transid;
	struct extent_buffer *leaf;
	struct btrfs_ioctl_search_header sh;
	struct btrfs_key test;
	unsigned long item_off;
	unsigned long item_len;
	int nritems;
	int i;
	int slot;
	int ret = 0;

	leaf = path->nodes[0];
	slot = path->slots[0];
	nritems = btrfs_header_nritems(leaf);

	if (btrfs_header_generation(leaf) > sk->max_transid) {
		i = nritems;
		goto advance_key;
	}
	found_transid = btrfs_header_generation(leaf);

	for (i = slot; i < nritems; i++) {
		item_off = btrfs_item_ptr_offset(leaf, i);
		item_len = btrfs_item_size_nr(leaf, i);

		btrfs_item_key_to_cpu(leaf, key, i);
		if (!key_in_sk(key, sk))
			continue;

		if (sizeof(sh) + item_len > *buf_size) {
			if (*num_found) {
#ifdef MY_DEF_HERE
				ret = -EAGAIN;
#else /* MY_DEF_HERE */
				ret = 1;
#endif /* MY_DEF_HERE */
				goto out;
			}

			/*
			 * return one empty item back for v1, which does not
			 * handle -EOVERFLOW
			 */

			*buf_size = sizeof(sh) + item_len;
			item_len = 0;
			ret = -EOVERFLOW;
		}

		if (sizeof(sh) + item_len + *sk_offset > *buf_size) {
#ifdef MY_DEF_HERE
			ret = -EAGAIN;
#else /* MY_DEF_HERE */
			ret = 1;
#endif /* MY_DEF_HERE */
			goto out;
		}

		sh.objectid = key->objectid;
		sh.offset = key->offset;
		sh.type = key->type;
		sh.len = item_len;
		sh.transid = found_transid;

		/*
		 * Copy search result header. If we fault then loop again so we
		 * can fault in the pages and -EFAULT there if there's a
		 * problem. Otherwise we'll fault and then copy the buffer in
		 * properly this next time through
		 */
		if (copy_to_user_nofault(ubuf + *sk_offset, &sh, sizeof(sh))) {
			ret = 0;
			goto out;
		}

		*sk_offset += sizeof(sh);

		if (item_len) {
			char __user *up = ubuf + *sk_offset;
			/*
			 * Copy the item, same behavior as above, but reset the
			 * * sk_offset so we copy the full thing again.
			 */
			if (read_extent_buffer_to_user_nofault(leaf, up,
						item_off, item_len)) {
				ret = 0;
				*sk_offset -= sizeof(sh);
				goto out;
			}

			*sk_offset += item_len;
		}
		(*num_found)++;

		if (ret) /* -EOVERFLOW from above */
			goto out;

		if (*num_found >= sk->nr_items) {
#ifdef MY_DEF_HERE
			ret = -EAGAIN;
#else /* MY_DEF_HERE */
			ret = 1;
#endif /* MY_DEF_HERE */
			goto out;
		}
	}
advance_key:
	ret = 0;
	test.objectid = sk->max_objectid;
	test.type = sk->max_type;
	test.offset = sk->max_offset;
	if (btrfs_comp_cpu_keys(key, &test) >= 0)
		ret = 1;
	else if (key->offset < (u64)-1)
		key->offset++;
	else if (key->type < (u8)-1) {
		key->offset = 0;
		key->type++;
	} else if (key->objectid < (u64)-1) {
		key->offset = 0;
		key->type = 0;
		key->objectid++;
	} else
		ret = 1;
out:
	/*
	 *  0: all items from this leaf copied, continue with next
	 *  1: * more items can be copied, but unused buffer is too small
	 *     * all items were found
	 *     Either way, it will stops the loop which iterates to the next
	 *     leaf
#ifdef MY_DEF_HERE
	 *  -EAGAIN: try again to get more
#endif
	 *  -EOVERFLOW: item was to large for buffer
	 *  -EFAULT: could not copy extent buffer back to userspace
	 */
	return ret;
}

static noinline int search_ioctl(struct inode *inode,
				 struct btrfs_ioctl_search_key *sk,
				 size_t *buf_size,
				 char __user *ubuf)
{
	struct btrfs_fs_info *info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root;
	struct btrfs_key key;
	struct btrfs_path *path;
	int ret;
	int num_found = 0;
#ifdef MY_DEF_HERE
	u64 orig_min_offset = sk->min_offset;
#endif /* MY_DEF_HERE */
	unsigned long sk_offset = 0;

	if (*buf_size < sizeof(struct btrfs_ioctl_search_header)) {
		*buf_size = sizeof(struct btrfs_ioctl_search_header);
		return -EOVERFLOW;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (sk->tree_id == 0) {
		/* search the root of the inode that was passed */
		root = btrfs_grab_root(BTRFS_I(inode)->root);
	} else {
		root = btrfs_get_fs_root(info, sk->tree_id, true);
		if (IS_ERR(root)) {
			btrfs_free_path(path);
			return PTR_ERR(root);
		}
	}

	key.objectid = sk->min_objectid;
	key.type = sk->min_type;
	key.offset = sk->min_offset;

#ifdef MY_DEF_HERE
	if (sk->search_flag & BTRFS_SEARCH_FLAG_READAHEAD)
		path->reada = READA_FORWARD_ALWAYS;
	if ((sk->search_flag & BTRFS_SEARCH_FLAG_ADJUST_MIN) &&
	    (sk->min_type == BTRFS_EXTENT_DATA_KEY)) {
		ret = btrfs_lookup_file_extent_by_file_offset(NULL, root,
				path, sk->min_objectid, sk->min_offset, 0);
		if (0 > ret && -ENOENT != ret)
			goto err;
		if (!ret) {
			btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
			sk->min_offset = key.offset;
		}
		btrfs_release_path(path);
	}
#endif /* MY_DEF_HERE */

	while (1) {
		ret = fault_in_pages_writeable(ubuf + sk_offset,
					       *buf_size - sk_offset);
		if (ret)
			break;

		ret = btrfs_search_forward(root, &key, path, sk->min_transid);
		if (ret != 0) {
			if (ret > 0)
				ret = 0;
			goto err;
		}
		ret = copy_to_sk(path, &key, sk, buf_size, ubuf,
				 &sk_offset, &num_found);
		btrfs_release_path(path);
		if (ret)
			break;

	}
	if (ret > 0)
		ret = 0;
err:
#ifdef MY_DEF_HERE
	sk->min_offset = orig_min_offset;
#endif /* MY_DEF_HERE */
	sk->nr_items = num_found;
	btrfs_put_root(root);
	btrfs_free_path(path);
	return ret;
}

static noinline int btrfs_ioctl_tree_search(struct file *file,
					   void __user *argp)
{
	struct btrfs_ioctl_search_args __user *uargs;
	struct btrfs_ioctl_search_key sk;
	struct inode *inode;
	int ret;
	size_t buf_size;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	uargs = (struct btrfs_ioctl_search_args __user *)argp;

	if (copy_from_user(&sk, &uargs->key, sizeof(sk)))
		return -EFAULT;

	buf_size = sizeof(uargs->buf);

	inode = file_inode(file);
	ret = search_ioctl(inode, &sk, &buf_size, uargs->buf);

	/*
	 * In the origin implementation an overflow is handled by returning a
	 * search header with a len of zero, so reset ret.
	 */
#ifdef MY_DEF_HERE
	if (ret == -EOVERFLOW || ret == -EAGAIN)
#else /* MY_DEF_HERE */
	if (ret == -EOVERFLOW)
#endif /* MY_DEF_HERE */
		ret = 0;

	if (ret == 0 && copy_to_user(&uargs->key, &sk, sizeof(sk)))
		ret = -EFAULT;
	return ret;
}

static noinline int btrfs_ioctl_tree_search_v2(struct file *file,
					       void __user *argp)
{
	struct btrfs_ioctl_search_args_v2 __user *uarg;
	struct btrfs_ioctl_search_args_v2 args;
	struct inode *inode;
	int ret;
	size_t buf_size;
	const size_t buf_limit = SZ_16M;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* copy search header and buffer size */
	uarg = (struct btrfs_ioctl_search_args_v2 __user *)argp;
	if (copy_from_user(&args, uarg, sizeof(args)))
		return -EFAULT;

	buf_size = args.buf_size;

	/* limit result size to 16MB */
	if (buf_size > buf_limit)
		buf_size = buf_limit;

	inode = file_inode(file);
	ret = search_ioctl(inode, &args.key, &buf_size,
			   (char __user *)(&uarg->buf[0]));
#ifdef MY_DEF_HERE
	if (!(args.key.search_flag & BTRFS_SEARCH_FLAG_REPORT_BUF_FULL) && ret == -EAGAIN)
		ret = 0;
	if ((ret == 0 || ret == -EAGAIN) && copy_to_user(&uarg->key, &args.key, sizeof(args.key)))
#else /* MY_DEF_HERE */
	if (ret == 0 && copy_to_user(&uarg->key, &args.key, sizeof(args.key)))
#endif /* MY_DEF_HERE */
		ret = -EFAULT;
	else if (ret == -EOVERFLOW &&
		copy_to_user(&uarg->buf_size, &buf_size, sizeof(buf_size)))
		ret = -EFAULT;

	return ret;
}

/*
 * Search INODE_REFs to identify path name of 'dirid' directory
 * in a 'tree_id' tree. and sets path name to 'name'.
 */
static noinline int btrfs_search_path_in_tree(struct btrfs_fs_info *info,
				u64 tree_id, u64 dirid, char *name)
{
	struct btrfs_root *root;
	struct btrfs_key key;
	char *ptr;
	int ret = -1;
	int slot;
	int len;
	int total_len = 0;
	struct btrfs_inode_ref *iref;
	struct extent_buffer *l;
	struct btrfs_path *path;

	if (dirid == BTRFS_FIRST_FREE_OBJECTID) {
		name[0]='\0';
		return 0;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ptr = &name[BTRFS_INO_LOOKUP_PATH_MAX - 1];

	root = btrfs_get_fs_root(info, tree_id, true);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		root = NULL;
		goto out;
	}

	key.objectid = dirid;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = (u64)-1;

	while (1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
		else if (ret > 0) {
			ret = btrfs_previous_item(root, path, dirid,
						  BTRFS_INODE_REF_KEY);
			if (ret < 0)
				goto out;
			else if (ret > 0) {
				ret = -ENOENT;
				goto out;
			}
		}

		l = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(l, &key, slot);

		iref = btrfs_item_ptr(l, slot, struct btrfs_inode_ref);
		len = btrfs_inode_ref_name_len(l, iref);
		ptr -= len + 1;
		total_len += len + 1;
		if (ptr < name) {
			ret = -ENAMETOOLONG;
			goto out;
		}

		*(ptr + len) = '/';
		read_extent_buffer(l, ptr, (unsigned long)(iref + 1), len);

		if (key.offset == BTRFS_FIRST_FREE_OBJECTID)
			break;

		btrfs_release_path(path);
		key.objectid = key.offset;
		key.offset = (u64)-1;
		dirid = key.objectid;
	}
	memmove(name, ptr, total_len);
	name[total_len] = '\0';
	ret = 0;
out:
	btrfs_put_root(root);
	btrfs_free_path(path);
	return ret;
}

static int btrfs_search_path_in_tree_user(struct inode *inode,
				struct btrfs_ioctl_ino_lookup_user_args *args)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct super_block *sb = inode->i_sb;
	struct btrfs_key upper_limit = BTRFS_I(inode)->location;
	u64 treeid = BTRFS_I(inode)->root->root_key.objectid;
	u64 dirid = args->dirid;
	unsigned long item_off;
	unsigned long item_len;
	struct btrfs_inode_ref *iref;
	struct btrfs_root_ref *rref;
	struct btrfs_root *root = NULL;
	struct btrfs_path *path;
	struct btrfs_key key, key2;
	struct extent_buffer *leaf;
	struct inode *temp_inode;
	char *ptr;
	int slot;
	int len;
	int total_len = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * If the bottom subvolume does not exist directly under upper_limit,
	 * construct the path in from the bottom up.
	 */
	if (dirid != upper_limit.objectid) {
		ptr = &args->path[BTRFS_INO_LOOKUP_USER_PATH_MAX - 1];

		root = btrfs_get_fs_root(fs_info, treeid, true);
		if (IS_ERR(root)) {
			ret = PTR_ERR(root);
			goto out;
		}

		key.objectid = dirid;
		key.type = BTRFS_INODE_REF_KEY;
		key.offset = (u64)-1;
		while (1) {
			ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
			if (ret < 0) {
				goto out_put;
			} else if (ret > 0) {
				ret = btrfs_previous_item(root, path, dirid,
							  BTRFS_INODE_REF_KEY);
				if (ret < 0) {
					goto out_put;
				} else if (ret > 0) {
					ret = -ENOENT;
					goto out_put;
				}
			}

			leaf = path->nodes[0];
			slot = path->slots[0];
			btrfs_item_key_to_cpu(leaf, &key, slot);

			iref = btrfs_item_ptr(leaf, slot, struct btrfs_inode_ref);
			len = btrfs_inode_ref_name_len(leaf, iref);
			ptr -= len + 1;
			total_len += len + 1;
			if (ptr < args->path) {
				ret = -ENAMETOOLONG;
				goto out_put;
			}

			*(ptr + len) = '/';
			read_extent_buffer(leaf, ptr,
					(unsigned long)(iref + 1), len);

			/* Check the read+exec permission of this directory */
			ret = btrfs_previous_item(root, path, dirid,
						  BTRFS_INODE_ITEM_KEY);
			if (ret < 0) {
				goto out_put;
			} else if (ret > 0) {
				ret = -ENOENT;
				goto out_put;
			}

			leaf = path->nodes[0];
			slot = path->slots[0];
			btrfs_item_key_to_cpu(leaf, &key2, slot);
			if (key2.objectid != dirid) {
				ret = -ENOENT;
				goto out_put;
			}

			temp_inode = btrfs_iget(sb, key2.objectid, root);
			if (IS_ERR(temp_inode)) {
				ret = PTR_ERR(temp_inode);
				goto out_put;
			}
			ret = inode_permission(temp_inode, MAY_READ | MAY_EXEC);
			iput(temp_inode);
			if (ret) {
				ret = -EACCES;
				goto out_put;
			}

			if (key.offset == upper_limit.objectid)
				break;
			if (key.objectid == BTRFS_FIRST_FREE_OBJECTID) {
				ret = -EACCES;
				goto out_put;
			}

			btrfs_release_path(path);
			key.objectid = key.offset;
			key.offset = (u64)-1;
			dirid = key.objectid;
		}

		memmove(args->path, ptr, total_len);
		args->path[total_len] = '\0';
		btrfs_put_root(root);
		root = NULL;
		btrfs_release_path(path);
	}

	/* Get the bottom subvolume's name from ROOT_REF */
	key.objectid = treeid;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = args->treeid;
	ret = btrfs_search_slot(NULL, fs_info->tree_root, &key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	leaf = path->nodes[0];
	slot = path->slots[0];
	btrfs_item_key_to_cpu(leaf, &key, slot);

	item_off = btrfs_item_ptr_offset(leaf, slot);
	item_len = btrfs_item_size_nr(leaf, slot);
	/* Check if dirid in ROOT_REF corresponds to passed dirid */
	rref = btrfs_item_ptr(leaf, slot, struct btrfs_root_ref);
	if (args->dirid != btrfs_root_ref_dirid(leaf, rref)) {
		ret = -EINVAL;
		goto out;
	}

	/* Copy subvolume's name */
	item_off += sizeof(struct btrfs_root_ref);
	item_len -= sizeof(struct btrfs_root_ref);
	read_extent_buffer(leaf, args->name, item_off, item_len);
	args->name[item_len] = 0;

out_put:
	btrfs_put_root(root);
out:
	btrfs_free_path(path);
	return ret;
}

static noinline int btrfs_ioctl_ino_lookup(struct file *file,
					   void __user *argp)
{
	struct btrfs_ioctl_ino_lookup_args *args;
	struct inode *inode;
	int ret = 0;

	args = memdup_user(argp, sizeof(*args));
	if (IS_ERR(args))
		return PTR_ERR(args);

	inode = file_inode(file);

	/*
	 * Unprivileged query to obtain the containing subvolume root id. The
	 * path is reset so it's consistent with btrfs_search_path_in_tree.
	 */
	if (args->treeid == 0)
		args->treeid = BTRFS_I(inode)->root->root_key.objectid;

	if (args->objectid == BTRFS_FIRST_FREE_OBJECTID) {
		args->name[0] = 0;
		goto out;
	}

	if (!capable(CAP_SYS_ADMIN)) {
		ret = -EPERM;
		goto out;
	}

	ret = btrfs_search_path_in_tree(BTRFS_I(inode)->root->fs_info,
					args->treeid, args->objectid,
					args->name);

out:
	if (ret == 0 && copy_to_user(argp, args, sizeof(*args)))
		ret = -EFAULT;

	kfree(args);
	return ret;
}

/*
 * Version of ino_lookup ioctl (unprivileged)
 *
 * The main differences from ino_lookup ioctl are:
 *
 *   1. Read + Exec permission will be checked using inode_permission() during
 *      path construction. -EACCES will be returned in case of failure.
 *   2. Path construction will be stopped at the inode number which corresponds
 *      to the fd with which this ioctl is called. If constructed path does not
 *      exist under fd's inode, -EACCES will be returned.
 *   3. The name of bottom subvolume is also searched and filled.
 */
static int btrfs_ioctl_ino_lookup_user(struct file *file, void __user *argp)
{
	struct btrfs_ioctl_ino_lookup_user_args *args;
	struct inode *inode;
	int ret;

	args = memdup_user(argp, sizeof(*args));
	if (IS_ERR(args))
		return PTR_ERR(args);

	inode = file_inode(file);

	if (args->dirid == BTRFS_FIRST_FREE_OBJECTID &&
	    BTRFS_I(inode)->location.objectid != BTRFS_FIRST_FREE_OBJECTID) {
		/*
		 * The subvolume does not exist under fd with which this is
		 * called
		 */
		kfree(args);
		return -EACCES;
	}

	ret = btrfs_search_path_in_tree_user(inode, args);

	if (ret == 0 && copy_to_user(argp, args, sizeof(*args)))
		ret = -EFAULT;

	kfree(args);
	return ret;
}

/* Get the subvolume information in BTRFS_ROOT_ITEM and BTRFS_ROOT_BACKREF */
static int btrfs_ioctl_get_subvol_info(struct file *file, void __user *argp)
{
	struct btrfs_ioctl_get_subvol_info_args *subvol_info;
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root_item *root_item;
	struct btrfs_root_ref *rref;
	struct extent_buffer *leaf;
	unsigned long item_off;
	unsigned long item_len;
	struct inode *inode;
	int slot;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	subvol_info = kzalloc(sizeof(*subvol_info), GFP_KERNEL);
	if (!subvol_info) {
		btrfs_free_path(path);
		return -ENOMEM;
	}

	inode = file_inode(file);
	fs_info = BTRFS_I(inode)->root->fs_info;

	/* Get root_item of inode's subvolume */
	key.objectid = BTRFS_I(inode)->root->root_key.objectid;
	root = btrfs_get_fs_root(fs_info, key.objectid, true);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out_free;
	}
	root_item = &root->root_item;

	subvol_info->treeid = key.objectid;

	subvol_info->generation = btrfs_root_generation(root_item);
	subvol_info->flags = btrfs_root_flags(root_item);

	memcpy(subvol_info->uuid, root_item->uuid, BTRFS_UUID_SIZE);
	memcpy(subvol_info->parent_uuid, root_item->parent_uuid,
						    BTRFS_UUID_SIZE);
	memcpy(subvol_info->received_uuid, root_item->received_uuid,
						    BTRFS_UUID_SIZE);

	subvol_info->ctransid = btrfs_root_ctransid(root_item);
	subvol_info->ctime.sec = btrfs_stack_timespec_sec(&root_item->ctime);
	subvol_info->ctime.nsec = btrfs_stack_timespec_nsec(&root_item->ctime);

	subvol_info->otransid = btrfs_root_otransid(root_item);
	subvol_info->otime.sec = btrfs_stack_timespec_sec(&root_item->otime);
	subvol_info->otime.nsec = btrfs_stack_timespec_nsec(&root_item->otime);

	subvol_info->stransid = btrfs_root_stransid(root_item);
	subvol_info->stime.sec = btrfs_stack_timespec_sec(&root_item->stime);
	subvol_info->stime.nsec = btrfs_stack_timespec_nsec(&root_item->stime);

	subvol_info->rtransid = btrfs_root_rtransid(root_item);
	subvol_info->rtime.sec = btrfs_stack_timespec_sec(&root_item->rtime);
	subvol_info->rtime.nsec = btrfs_stack_timespec_nsec(&root_item->rtime);

	if (key.objectid != BTRFS_FS_TREE_OBJECTID) {
		/* Search root tree for ROOT_BACKREF of this subvolume */
		key.type = BTRFS_ROOT_BACKREF_KEY;
		key.offset = 0;
		ret = btrfs_search_slot(NULL, fs_info->tree_root, &key, path, 0, 0);
		if (ret < 0) {
			goto out;
		} else if (path->slots[0] >=
			   btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(fs_info->tree_root, path);
			if (ret < 0) {
				goto out;
			} else if (ret > 0) {
				ret = -EUCLEAN;
				goto out;
			}
		}

		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid == subvol_info->treeid &&
		    key.type == BTRFS_ROOT_BACKREF_KEY) {
			subvol_info->parent_id = key.offset;

			rref = btrfs_item_ptr(leaf, slot, struct btrfs_root_ref);
			subvol_info->dirid = btrfs_root_ref_dirid(leaf, rref);

			item_off = btrfs_item_ptr_offset(leaf, slot)
					+ sizeof(struct btrfs_root_ref);
			item_len = btrfs_item_size_nr(leaf, slot)
					- sizeof(struct btrfs_root_ref);
			read_extent_buffer(leaf, subvol_info->name,
					   item_off, item_len);
		} else {
			ret = -ENOENT;
			goto out;
		}
	}

	if (copy_to_user(argp, subvol_info, sizeof(*subvol_info)))
		ret = -EFAULT;

out:
	btrfs_put_root(root);
out_free:
	btrfs_free_path(path);
	kfree(subvol_info);
	return ret;
}

/*
 * Return ROOT_REF information of the subvolume containing this inode
 * except the subvolume name.
 */
static int btrfs_ioctl_get_subvol_rootref(struct file *file, void __user *argp)
{
	struct btrfs_ioctl_get_subvol_rootref_args *rootrefs;
	struct btrfs_root_ref *rref;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct inode *inode;
	u64 objectid;
	int slot;
	int ret;
	u8 found;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	rootrefs = memdup_user(argp, sizeof(*rootrefs));
	if (IS_ERR(rootrefs)) {
		btrfs_free_path(path);
		return PTR_ERR(rootrefs);
	}

	inode = file_inode(file);
	root = BTRFS_I(inode)->root->fs_info->tree_root;
	objectid = BTRFS_I(inode)->root->root_key.objectid;

	key.objectid = objectid;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = rootrefs->min_treeid;
	found = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (path->slots[0] >=
		   btrfs_header_nritems(path->nodes[0])) {
		ret = btrfs_next_leaf(root, path);
		if (ret < 0) {
			goto out;
		} else if (ret > 0) {
			ret = -EUCLEAN;
			goto out;
		}
	}
	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid != objectid || key.type != BTRFS_ROOT_REF_KEY) {
			ret = 0;
			goto out;
		}

		if (found == BTRFS_MAX_ROOTREF_BUFFER_NUM) {
			ret = -EOVERFLOW;
			goto out;
		}

		rref = btrfs_item_ptr(leaf, slot, struct btrfs_root_ref);
		rootrefs->rootref[found].treeid = key.offset;
		rootrefs->rootref[found].dirid =
				  btrfs_root_ref_dirid(leaf, rref);
		found++;

		ret = btrfs_next_item(root, path);
		if (ret < 0) {
			goto out;
		} else if (ret > 0) {
			ret = -EUCLEAN;
			goto out;
		}
	}

out:
	if (!ret || ret == -EOVERFLOW) {
		rootrefs->num_items = found;
		/* update min_treeid for next search */
		if (found)
			rootrefs->min_treeid =
				rootrefs->rootref[found - 1].treeid + 1;
		if (copy_to_user(argp, rootrefs, sizeof(*rootrefs)))
			ret = -EFAULT;
	}

	kfree(rootrefs);
	btrfs_free_path(path);

	return ret;
}

static noinline int btrfs_ioctl_snap_destroy(struct file *file,
					     void __user *arg,
					     bool destroy_v2)
{
	struct dentry *parent = file->f_path.dentry;
	struct btrfs_fs_info *fs_info = btrfs_sb(parent->d_sb);
	struct dentry *dentry;
	struct inode *dir = d_inode(parent);
	struct inode *inode;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_root *dest = NULL;
	struct btrfs_ioctl_vol_args *vol_args = NULL;
	struct btrfs_ioctl_vol_args_v2 *vol_args2 = NULL;
	char *subvol_name, *subvol_name_ptr = NULL;
	int subvol_namelen;
	int err = 0;
	bool destroy_parent = false;

	if (destroy_v2) {
		vol_args2 = memdup_user(arg, sizeof(*vol_args2));
		if (IS_ERR(vol_args2))
			return PTR_ERR(vol_args2);

		if (vol_args2->flags & ~BTRFS_SUBVOL_DELETE_ARGS_MASK) {
			err = -EOPNOTSUPP;
			goto out;
		}

		/*
		 * If SPEC_BY_ID is not set, we are looking for the subvolume by
		 * name, same as v1 currently does.
		 */
		if (!(vol_args2->flags & BTRFS_SUBVOL_SPEC_BY_ID)) {
			vol_args2->name[BTRFS_SUBVOL_NAME_MAX] = 0;
			subvol_name = vol_args2->name;

			err = mnt_want_write_file(file);
			if (err)
				goto out;
		} else {
			if (vol_args2->subvolid < BTRFS_FIRST_FREE_OBJECTID) {
				err = -EINVAL;
				goto out;
			}

			err = mnt_want_write_file(file);
			if (err)
				goto out;

			dentry = btrfs_get_dentry(fs_info->sb,
					BTRFS_FIRST_FREE_OBJECTID,
					vol_args2->subvolid, 0, 0);
			if (IS_ERR(dentry)) {
				err = PTR_ERR(dentry);
				goto out_drop_write;
			}

			/*
			 * Change the default parent since the subvolume being
			 * deleted can be outside of the current mount point.
			 */
			parent = btrfs_get_parent(dentry);

			/*
			 * At this point dentry->d_name can point to '/' if the
			 * subvolume we want to destroy is outsite of the
			 * current mount point, so we need to release the
			 * current dentry and execute the lookup to return a new
			 * one with ->d_name pointing to the
			 * <mount point>/subvol_name.
			 */
			dput(dentry);
			if (IS_ERR(parent)) {
				err = PTR_ERR(parent);
				goto out_drop_write;
			}
			dir = d_inode(parent);

			/*
			 * If v2 was used with SPEC_BY_ID, a new parent was
			 * allocated since the subvolume can be outside of the
			 * current mount point. Later on we need to release this
			 * new parent dentry.
			 */
			destroy_parent = true;

			subvol_name_ptr = btrfs_get_subvol_name_from_objectid(
						fs_info, vol_args2->subvolid);
			if (IS_ERR(subvol_name_ptr)) {
				err = PTR_ERR(subvol_name_ptr);
				goto free_parent;
			}
			/* subvol_name_ptr is already NULL termined */
			subvol_name = (char *)kbasename(subvol_name_ptr);
		}
	} else {
		vol_args = memdup_user(arg, sizeof(*vol_args));
		if (IS_ERR(vol_args))
			return PTR_ERR(vol_args);

		vol_args->name[BTRFS_PATH_NAME_MAX] = 0;
		subvol_name = vol_args->name;

		err = mnt_want_write_file(file);
		if (err)
			goto out;
	}

	subvol_namelen = strlen(subvol_name);

	if (strchr(subvol_name, '/') ||
	    strncmp(subvol_name, "..", subvol_namelen) == 0) {
		err = -EINVAL;
		goto free_subvol_name;
	}

	if (!S_ISDIR(dir->i_mode)) {
		err = -ENOTDIR;
		goto free_subvol_name;
	}

	err = down_write_killable_nested(&dir->i_rwsem, I_MUTEX_PARENT);
	if (err == -EINTR)
		goto free_subvol_name;
	dentry = lookup_one_len(subvol_name, parent, subvol_namelen);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_unlock_dir;
	}

	if (d_really_is_negative(dentry)) {
		err = -ENOENT;
		goto out_dput;
	}

	inode = d_inode(dentry);
	dest = BTRFS_I(inode)->root;
	if (!capable(CAP_SYS_ADMIN)) {
		/*
		 * Regular user.  Only allow this with a special mount
		 * option, when the user has write+exec access to the
		 * subvol root, and when rmdir(2) would have been
		 * allowed.
		 *
		 * Note that this is _not_ check that the subvol is
		 * empty or doesn't contain data that we wouldn't
		 * otherwise be able to delete.
		 *
		 * Users who want to delete empty subvols should try
		 * rmdir(2).
		 */
		err = -EPERM;
		if (!btrfs_test_opt(fs_info, USER_SUBVOL_RM_ALLOWED))
			goto out_dput;

		/*
		 * Do not allow deletion if the parent dir is the same
		 * as the dir to be deleted.  That means the ioctl
		 * must be called on the dentry referencing the root
		 * of the subvol, not a random directory contained
		 * within it.
		 */
		err = -EINVAL;
		if (root == dest)
			goto out_dput;

		err = inode_permission(inode, MAY_WRITE | MAY_EXEC);
		if (err)
			goto out_dput;
	}

	/* check if subvolume may be deleted by a user */
	err = btrfs_may_delete(dir, dentry, 1);
	if (err)
		goto out_dput;

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID) {
		err = -EINVAL;
		goto out_dput;
	}

	inode_lock(inode);
	err = btrfs_delete_subvolume(dir, dentry);
	inode_unlock(inode);
	if (!err) {
		fsnotify_rmdir(dir, dentry);
		d_delete(dentry);
	}

out_dput:
	dput(dentry);
out_unlock_dir:
	inode_unlock(dir);
free_subvol_name:
	kfree(subvol_name_ptr);
free_parent:
	if (destroy_parent)
		dput(parent);
out_drop_write:
	mnt_drop_write_file(file);
out:
	kfree(vol_args2);
	kfree(vol_args);
	return err;
}

#ifdef MY_DEF_HERE
static inline void get_min_max_range(u64 *min, u64 *max, u64 file_extent_offset,
				     u64 extent_item_offset, u64 extent_item_size)
{
	u64 local_start = 0, local_end = 0;

	if (file_extent_offset > extent_item_offset)
		local_start = file_extent_offset - extent_item_offset;

	local_end = file_extent_offset + (extent_item_size - extent_item_offset);

	*min = min(*min, local_start);
	*max = max(*max, local_end);
}

static void syno_reclaim_range_adjust(struct btrfs_inode *inode,
				      struct btrfs_ioctl_defrag_range_args *range)
{
	int ret = 0;
	u64 ino = btrfs_ino(inode);
	u64 end = range->start + range->len;
	u64 min_begin = range->start;
	u64 max_end = range->start + range->len;
	struct btrfs_root *root = inode->root;
	struct btrfs_key key;
	struct btrfs_path *path = NULL;
	struct extent_buffer *leaf = NULL;
	struct btrfs_file_extent_item *fi = NULL;

	if (0 == min_begin && (u64)-1 == max_end)
		return;

	path = btrfs_alloc_path();
	if (!path)
		return;

	ret = btrfs_lookup_file_extent_by_file_offset(NULL, root, path, ino,
						      range->start, 0);
	if (0 > ret) {
		if (-ENOENT != ret) {
			btrfs_info(root->fs_info,
				   "lookup ino[%llu] offset[%llu] failed %d",
				   ino, range->start, ret);
		}
		goto out;
	}

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	while (key.offset < end) {
		fi = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_file_extent_item);

		get_min_max_range(&min_begin, &max_end, key.offset,
				  btrfs_file_extent_offset(leaf, fi),
				  btrfs_file_extent_disk_num_bytes(leaf, fi));

		ret = btrfs_search_next_file_extent(&key, root, path);
		if (ret) {
			break;
		}
		leaf = path->nodes[0];
	}

	if (min_begin < range->start)
		range->start = min_begin;
	if (max_end - min_begin > range->len)
		range->len = max_end - min_begin;

out:
	btrfs_free_path(path);
	return;
}
#endif /* MY_DEF_HERE */

static int btrfs_ioctl_defrag(struct file *file, void __user *argp)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_defrag_range_args *range;
	int ret;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	if (btrfs_root_readonly(root)) {
		ret = -EROFS;
		goto out;
	}

	switch (inode->i_mode & S_IFMT) {
	case S_IFDIR:
		if (!capable(CAP_SYS_ADMIN)) {
			ret = -EPERM;
			goto out;
		}
		ret = btrfs_defrag_root(root);
		break;
	case S_IFREG:
		/*
		 * Note that this does not check the file descriptor for write
		 * access. This prevents defragmenting executables that are
		 * running and allows defrag on files open in read-only mode.
		 */
		if (!capable(CAP_SYS_ADMIN) &&
		    inode_permission(inode, MAY_WRITE)) {
			ret = -EPERM;
			goto out;
		}

		range = kzalloc(sizeof(*range), GFP_KERNEL);
		if (!range) {
			ret = -ENOMEM;
			goto out;
		}

		if (argp) {
			if (copy_from_user(range, argp,
					   sizeof(*range))) {
				ret = -EFAULT;
				kfree(range);
				goto out;
			}
			/* compression requires us to start the IO */
			if ((range->flags & BTRFS_DEFRAG_RANGE_COMPRESS)) {
				range->flags |= BTRFS_DEFRAG_RANGE_START_IO;
				range->extent_thresh = (u32)-1;
			}
#ifdef MY_DEF_HERE
			if (range->flags & BTRFS_DEFRAG_RANGE_SYNO_DEFRAG) {
				syno_reclaim_range_adjust(BTRFS_I(inode), range);
			}
#endif /* MY_DEF_HERE */
		} else {
			/* the rest are all set to zero by kzalloc */
			range->len = (u64)-1;
		}
		ret = btrfs_defrag_file(file_inode(file), file,
					range, BTRFS_OLDEST_GENERATION, 0);
		if (ret > 0)
			ret = 0;
#ifdef MY_ABC_HERE
		if (argp && ret == 0 && copy_to_user(argp, range, sizeof(*range))) {
			ret = -EFAULT;
			WARN_ON_ONCE(1);
		}
#endif /* MY_ABC_HERE */
		kfree(range);
		break;
	default:
		ret = -EINVAL;
	}
out:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_add_dev(struct btrfs_fs_info *fs_info, void __user *arg)
{
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!btrfs_exclop_start(fs_info, BTRFS_EXCLOP_DEV_ADD))
		return BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args)) {
		ret = PTR_ERR(vol_args);
		goto out;
	}

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	ret = btrfs_init_new_device(fs_info, vol_args->name);

	if (!ret)
		btrfs_info(fs_info, "disk added %s", vol_args->name);

	kfree(vol_args);
out:
	btrfs_exclop_finish(fs_info);
	return ret;
}

static long btrfs_ioctl_rm_dev_v2(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_vol_args_v2 *vol_args;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args)) {
		ret = PTR_ERR(vol_args);
		goto err_drop;
	}

	if (vol_args->flags & ~BTRFS_DEVICE_REMOVE_ARGS_MASK) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (!btrfs_exclop_start(fs_info, BTRFS_EXCLOP_DEV_REMOVE)) {
		ret = BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
		goto out;
	}

	if (vol_args->flags & BTRFS_DEVICE_SPEC_BY_ID) {
		ret = btrfs_rm_device(fs_info, NULL, vol_args->devid);
	} else {
		vol_args->name[BTRFS_SUBVOL_NAME_MAX] = '\0';
		ret = btrfs_rm_device(fs_info, vol_args->name, 0);
	}
	btrfs_exclop_finish(fs_info);

	if (!ret) {
		if (vol_args->flags & BTRFS_DEVICE_SPEC_BY_ID)
			btrfs_info(fs_info, "device deleted: id %llu",
					vol_args->devid);
		else
			btrfs_info(fs_info, "device deleted: %s",
					vol_args->name);
	}
out:
	kfree(vol_args);
err_drop:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_rm_dev(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	if (!btrfs_exclop_start(fs_info, BTRFS_EXCLOP_DEV_REMOVE)) {
		ret = BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
		goto out_drop_write;
	}

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args)) {
		ret = PTR_ERR(vol_args);
		goto out;
	}

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	ret = btrfs_rm_device(fs_info, vol_args->name, 0);

	if (!ret)
		btrfs_info(fs_info, "disk deleted %s", vol_args->name);
	kfree(vol_args);
out:
	btrfs_exclop_finish(fs_info);
out_drop_write:
	mnt_drop_write_file(file);

	return ret;
}

static long btrfs_ioctl_fs_info(struct btrfs_fs_info *fs_info,
				void __user *arg)
{
	struct btrfs_ioctl_fs_info_args *fi_args;
	struct btrfs_device *device;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	u64 flags_in;
	int ret = 0;

	fi_args = memdup_user(arg, sizeof(*fi_args));
	if (IS_ERR(fi_args))
		return PTR_ERR(fi_args);

	flags_in = fi_args->flags;
	memset(fi_args, 0, sizeof(*fi_args));

	rcu_read_lock();
	fi_args->num_devices = fs_devices->num_devices;

	list_for_each_entry_rcu(device, &fs_devices->devices, dev_list) {
		if (device->devid > fi_args->max_id)
			fi_args->max_id = device->devid;
	}
	rcu_read_unlock();

	memcpy(&fi_args->fsid, fs_devices->fsid, sizeof(fi_args->fsid));
	fi_args->nodesize = fs_info->nodesize;
	fi_args->sectorsize = fs_info->sectorsize;
	fi_args->clone_alignment = fs_info->sectorsize;

	if (flags_in & BTRFS_FS_INFO_FLAG_CSUM_INFO) {
		fi_args->csum_type = btrfs_super_csum_type(fs_info->super_copy);
		fi_args->csum_size = btrfs_super_csum_size(fs_info->super_copy);
		fi_args->flags |= BTRFS_FS_INFO_FLAG_CSUM_INFO;
	}

	if (flags_in & BTRFS_FS_INFO_FLAG_GENERATION) {
		fi_args->generation = fs_info->generation;
		fi_args->flags |= BTRFS_FS_INFO_FLAG_GENERATION;
	}

	if (flags_in & BTRFS_FS_INFO_FLAG_METADATA_UUID) {
		memcpy(&fi_args->metadata_uuid, fs_devices->metadata_uuid,
		       sizeof(fi_args->metadata_uuid));
		fi_args->flags |= BTRFS_FS_INFO_FLAG_METADATA_UUID;
	}

	if (copy_to_user(arg, fi_args, sizeof(*fi_args)))
		ret = -EFAULT;

	kfree(fi_args);
	return ret;
}

static long btrfs_ioctl_dev_info(struct btrfs_fs_info *fs_info,
				 void __user *arg)
{
	struct btrfs_ioctl_dev_info_args *di_args;
	struct btrfs_device *dev;
	int ret = 0;
	char *s_uuid = NULL;

	di_args = memdup_user(arg, sizeof(*di_args));
	if (IS_ERR(di_args))
		return PTR_ERR(di_args);

	if (!btrfs_is_empty_uuid(di_args->uuid))
		s_uuid = di_args->uuid;

	rcu_read_lock();
	dev = btrfs_find_device(fs_info->fs_devices, di_args->devid, s_uuid,
				NULL, true);

	if (!dev) {
		ret = -ENODEV;
		goto out;
	}

	di_args->devid = dev->devid;
	di_args->bytes_used = btrfs_device_get_bytes_used(dev);
	di_args->total_bytes = btrfs_device_get_total_bytes(dev);
	memcpy(di_args->uuid, dev->uuid, sizeof(di_args->uuid));
	if (dev->name) {
		strncpy(di_args->path, rcu_str_deref(dev->name),
				sizeof(di_args->path) - 1);
		di_args->path[sizeof(di_args->path) - 1] = 0;
	} else {
		di_args->path[0] = '\0';
	}

out:
	rcu_read_unlock();
	if (ret == 0 && copy_to_user(arg, di_args, sizeof(*di_args)))
		ret = -EFAULT;

	kfree(di_args);
	return ret;
}

static long btrfs_ioctl_default_subvol(struct file *file, void __user *argp)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_root *new_root;
	struct btrfs_dir_item *di;
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path = NULL;
	struct btrfs_disk_key disk_key;
	u64 objectid = 0;
	u64 dir_id;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	if (copy_from_user(&objectid, argp, sizeof(objectid))) {
		ret = -EFAULT;
		goto out;
	}

	if (!objectid)
		objectid = BTRFS_FS_TREE_OBJECTID;

	new_root = btrfs_get_fs_root(fs_info, objectid, true);
	if (IS_ERR(new_root)) {
		ret = PTR_ERR(new_root);
		goto out;
	}
	if (!is_fstree(new_root->root_key.objectid)) {
		ret = -ENOENT;
		goto out_free;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out_free;
	}
	path->leave_spinning = 1;

	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_free;
	}

	dir_id = btrfs_super_root_dir(fs_info->super_copy);
	di = btrfs_lookup_dir_item(trans, fs_info->tree_root, path,
				   dir_id, "default", 7, 1);
	if (IS_ERR_OR_NULL(di)) {
		btrfs_release_path(path);
		btrfs_end_transaction(trans);
		btrfs_err(fs_info,
			  "Umm, you don't have the default diritem, this isn't going to work");
		ret = -ENOENT;
		goto out_free;
	}

	btrfs_cpu_key_to_disk(&disk_key, &new_root->root_key);
	btrfs_set_dir_item_key(path->nodes[0], di, &disk_key);
	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_release_path(path);

	btrfs_set_fs_incompat(fs_info, DEFAULT_SUBVOL);
	btrfs_end_transaction(trans);
out_free:
	btrfs_put_root(new_root);
	btrfs_free_path(path);
out:
	mnt_drop_write_file(file);
	return ret;
}

static void get_block_group_info(struct list_head *groups_list,
				 struct btrfs_ioctl_space_info *space)
{
	struct btrfs_block_group *block_group;

	space->total_bytes = 0;
	space->used_bytes = 0;
	space->flags = 0;
	list_for_each_entry(block_group, groups_list, list) {
		space->flags = block_group->flags;
		space->total_bytes += block_group->length;
		space->used_bytes += block_group->used;
	}
}

static long btrfs_ioctl_space_info(struct btrfs_fs_info *fs_info,
				   void __user *arg)
{
	struct btrfs_ioctl_space_args space_args;
	struct btrfs_ioctl_space_info space;
	struct btrfs_ioctl_space_info *dest;
	struct btrfs_ioctl_space_info *dest_orig;
	struct btrfs_ioctl_space_info __user *user_dest;
	struct btrfs_space_info *info;
	static const u64 types[] = {
		BTRFS_BLOCK_GROUP_DATA,
		BTRFS_BLOCK_GROUP_SYSTEM,
		BTRFS_BLOCK_GROUP_METADATA,
		BTRFS_BLOCK_GROUP_DATA | BTRFS_BLOCK_GROUP_METADATA
	};
	int num_types = 4;
	int alloc_size;
	int ret = 0;
	u64 slot_count = 0;
	int i, c;

	if (copy_from_user(&space_args,
			   (struct btrfs_ioctl_space_args __user *)arg,
			   sizeof(space_args)))
		return -EFAULT;

	for (i = 0; i < num_types; i++) {
		struct btrfs_space_info *tmp;

		info = NULL;
		list_for_each_entry(tmp, &fs_info->space_info, list) {
			if (tmp->flags == types[i]) {
				info = tmp;
				break;
			}
		}

		if (!info)
			continue;

		down_read(&info->groups_sem);
		for (c = 0; c < BTRFS_NR_RAID_TYPES; c++) {
			if (!list_empty(&info->block_groups[c]))
				slot_count++;
		}
		up_read(&info->groups_sem);
	}

	/*
	 * Global block reserve, exported as a space_info
	 */
	slot_count++;

	/* space_slots == 0 means they are asking for a count */
	if (space_args.space_slots == 0) {
		space_args.total_spaces = slot_count;
		goto out;
	}

	slot_count = min_t(u64, space_args.space_slots, slot_count);

	alloc_size = sizeof(*dest) * slot_count;

	/* we generally have at most 6 or so space infos, one for each raid
	 * level.  So, a whole page should be more than enough for everyone
	 */
	if (alloc_size > PAGE_SIZE)
		return -ENOMEM;

	space_args.total_spaces = 0;
	dest = kmalloc(alloc_size, GFP_KERNEL);
	if (!dest)
		return -ENOMEM;
	dest_orig = dest;

	/* now we have a buffer to copy into */
	for (i = 0; i < num_types; i++) {
		struct btrfs_space_info *tmp;

		if (!slot_count)
			break;

		info = NULL;
		list_for_each_entry(tmp, &fs_info->space_info, list) {
			if (tmp->flags == types[i]) {
				info = tmp;
				break;
			}
		}

		if (!info)
			continue;
		down_read(&info->groups_sem);
		for (c = 0; c < BTRFS_NR_RAID_TYPES; c++) {
			if (!list_empty(&info->block_groups[c])) {
				get_block_group_info(&info->block_groups[c],
						     &space);
				memcpy(dest, &space, sizeof(space));
				dest++;
				space_args.total_spaces++;
				slot_count--;
			}
			if (!slot_count)
				break;
		}
		up_read(&info->groups_sem);
	}

	/*
	 * Add global block reserve
	 */
	if (slot_count) {
		struct btrfs_block_rsv *block_rsv = &fs_info->global_block_rsv;

		spin_lock(&block_rsv->lock);
		space.total_bytes = block_rsv->size;
		space.used_bytes = block_rsv->size - block_rsv->reserved;
		spin_unlock(&block_rsv->lock);
		space.flags = BTRFS_SPACE_INFO_GLOBAL_RSV;
		memcpy(dest, &space, sizeof(space));
		space_args.total_spaces++;
	}

	user_dest = (struct btrfs_ioctl_space_info __user *)
		(arg + sizeof(struct btrfs_ioctl_space_args));

	if (copy_to_user(user_dest, dest_orig, alloc_size))
		ret = -EFAULT;

	kfree(dest_orig);
out:
	if (ret == 0 && copy_to_user(arg, &space_args, sizeof(space_args)))
		ret = -EFAULT;

	return ret;
}

#ifdef MY_ABC_HERE
static long btrfs_ioctl_trigger_transcation(struct super_block *sb)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_root *root = fs_info->tree_root;

	trans = btrfs_attach_transaction_barrier(root);
	if (IS_ERR(trans)) {
		/* no transaction, don't bother */
		if (PTR_ERR(trans) == -ENOENT) {
			/*
			 * Exit unless we have some pending changes
			 * that need to go through commit
			 */
			if (fs_info->pending_changes == 0)
				return 0;
			/*
			 * A non-blocking test if the fs is frozen. We must not
			 * start a new transaction here otherwise a deadlock
			 * happens. The pending operations are delayed to the
			 * next commit after thawing.
			 */
			if (sb_start_write_trylock(sb))
				sb_end_write(sb);
			else
				return 0;
			trans = btrfs_start_transaction(root, 0);
		}
		if (IS_ERR(trans))
			return PTR_ERR(trans);
	}
	return btrfs_commit_transaction(trans);
}
#endif /* MY_ABC_HERE */

static noinline long btrfs_ioctl_start_sync(struct btrfs_root *root,
					    void __user *argp)
{
	struct btrfs_trans_handle *trans;
	u64 transid;
	int ret;

	trans = btrfs_attach_transaction_barrier(root);
	if (IS_ERR(trans)) {
		if (PTR_ERR(trans) != -ENOENT)
			return PTR_ERR(trans);

		/* No running transaction, don't bother */
		transid = root->fs_info->last_trans_committed;
		goto out;
	}
	transid = trans->transid;
	ret = btrfs_commit_transaction_async(trans, 0);
	if (ret) {
		btrfs_end_transaction(trans);
		return ret;
	}
out:
	if (argp)
		if (copy_to_user(argp, &transid, sizeof(transid)))
			return -EFAULT;
	return 0;
}

static noinline long btrfs_ioctl_wait_sync(struct btrfs_fs_info *fs_info,
					   void __user *argp)
{
	u64 transid;

	if (argp) {
		if (copy_from_user(&transid, argp, sizeof(transid)))
			return -EFAULT;
	} else {
		transid = 0;  /* current trans */
	}
	return btrfs_wait_for_commit(fs_info, transid);
}

static long btrfs_ioctl_scrub(struct file *file, void __user *arg)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(file_inode(file)->i_sb);
	struct btrfs_ioctl_scrub_args *sa;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	if (!(sa->flags & BTRFS_SCRUB_READONLY)) {
		ret = mnt_want_write_file(file);
		if (ret)
			goto out;
	}

	ret = btrfs_scrub_dev(fs_info, sa->devid, sa->start, sa->end,
			      &sa->progress, sa->flags & BTRFS_SCRUB_READONLY,
			      0);

	/*
	 * Copy scrub args to user space even if btrfs_scrub_dev() returned an
	 * error. This is important as it allows user space to know how much
	 * progress scrub has done. For example, if scrub is canceled we get
	 * -ECANCELED from btrfs_scrub_dev() and return that error back to user
	 * space. Later user space can inspect the progress from the structure
	 * btrfs_ioctl_scrub_args and resume scrub from where it left off
	 * previously (btrfs-progs does this).
	 * If we fail to copy the btrfs_ioctl_scrub_args structure to user space
	 * then return -EFAULT to signal the structure was not copied or it may
	 * be corrupt and unreliable due to a partial copy.
	 */
	if (copy_to_user(arg, sa, sizeof(*sa)))
		ret = -EFAULT;

	if (!(sa->flags & BTRFS_SCRUB_READONLY))
		mnt_drop_write_file(file);
out:
	kfree(sa);
	return ret;
}

static long btrfs_ioctl_scrub_cancel(struct btrfs_fs_info *fs_info)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return btrfs_scrub_cancel(fs_info);
}

static long btrfs_ioctl_scrub_progress(struct btrfs_fs_info *fs_info,
				       void __user *arg)
{
	struct btrfs_ioctl_scrub_args *sa;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	ret = btrfs_scrub_progress(fs_info, sa->devid, &sa->progress);

	if (ret == 0 && copy_to_user(arg, sa, sizeof(*sa)))
		ret = -EFAULT;

	kfree(sa);
	return ret;
}

static long btrfs_ioctl_get_dev_stats(struct btrfs_fs_info *fs_info,
				      void __user *arg)
{
	struct btrfs_ioctl_get_dev_stats *sa;
	int ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	if ((sa->flags & BTRFS_DEV_STATS_RESET) && !capable(CAP_SYS_ADMIN)) {
		kfree(sa);
		return -EPERM;
	}

	ret = btrfs_get_dev_stats(fs_info, sa);

	if (ret == 0 && copy_to_user(arg, sa, sizeof(*sa)))
		ret = -EFAULT;

	kfree(sa);
	return ret;
}

static long btrfs_ioctl_dev_replace(struct btrfs_fs_info *fs_info,
				    void __user *arg)
{
	struct btrfs_ioctl_dev_replace_args *p;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	p = memdup_user(arg, sizeof(*p));
	if (IS_ERR(p))
		return PTR_ERR(p);

	switch (p->cmd) {
	case BTRFS_IOCTL_DEV_REPLACE_CMD_START:
		if (sb_rdonly(fs_info->sb)) {
			ret = -EROFS;
			goto out;
		}
		if (!btrfs_exclop_start(fs_info, BTRFS_EXCLOP_DEV_REPLACE)) {
			ret = BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
		} else {
			ret = btrfs_dev_replace_by_ioctl(fs_info, p);
			btrfs_exclop_finish(fs_info);
		}
		break;
	case BTRFS_IOCTL_DEV_REPLACE_CMD_STATUS:
		btrfs_dev_replace_status(fs_info, p);
		ret = 0;
		break;
	case BTRFS_IOCTL_DEV_REPLACE_CMD_CANCEL:
		p->result = btrfs_dev_replace_cancel(fs_info);
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if ((ret == 0 || ret == -ECANCELED) && copy_to_user(arg, p, sizeof(*p)))
		ret = -EFAULT;
out:
	kfree(p);
	return ret;
}

static long btrfs_ioctl_ino_to_path(struct btrfs_root *root, void __user *arg)
{
	int ret = 0;
	int i;
	u64 rel_ptr;
	int size;
	struct btrfs_ioctl_ino_path_args *ipa = NULL;
	struct inode_fs_paths *ipath = NULL;
	struct btrfs_path *path;

	if (!capable(CAP_DAC_READ_SEARCH))
		return -EPERM;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	ipa = memdup_user(arg, sizeof(*ipa));
	if (IS_ERR(ipa)) {
		ret = PTR_ERR(ipa);
		ipa = NULL;
		goto out;
	}

	size = min_t(u32, ipa->size, 4096);
	ipath = init_ipath(size, root, path);
	if (IS_ERR(ipath)) {
		ret = PTR_ERR(ipath);
		ipath = NULL;
		goto out;
	}

	ret = paths_from_inode(ipa->inum, ipath);
	if (ret < 0)
		goto out;

	for (i = 0; i < ipath->fspath->elem_cnt; ++i) {
		rel_ptr = ipath->fspath->val[i] -
			  (u64)(unsigned long)ipath->fspath->val;
		ipath->fspath->val[i] = rel_ptr;
	}

	ret = copy_to_user((void __user *)(unsigned long)ipa->fspath,
			   ipath->fspath, size);
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

out:
	btrfs_free_path(path);
	free_ipath(ipath);
	kfree(ipa);

	return ret;
}

#ifdef MY_ABC_HERE
/*
 * Similar to BTRFS_IOC_INO_PATHS, but we only output one path, regardless of how many
 * links this inode should have, since the vfs caller should not know too much about
 * how to parse struct btrfs_ioctl_ino_path_args and struct inode_fs_paths.
 */
int btrfs_vfs_ino_to_path(struct inode *inode, u64 inum, char *outpath, int len)
{
	int ret = 0;
	struct inode_fs_paths *ipath = NULL;
	struct btrfs_path *path;
	struct btrfs_root *root;

	if (len < PATH_MAX)
		return -EINVAL;

	if (inode->i_sb->s_magic == BTRFS_SUPER_MAGIC)
		root = BTRFS_I(inode)->root;
	else
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ipath = init_ipath(len + offsetof(struct btrfs_data_container, val[1]),
			   root, path);
	if (IS_ERR(ipath)) {
		ret = PTR_ERR(ipath);
		ipath = NULL;
		goto out;
	}

	ret = paths_from_inode(inum, ipath);
	if (ret < 0)
		goto out;

	if (ipath->fspath->elem_cnt > 0)
		strncpy(outpath, (char *)(ipath->fspath->val[0]), len);
	else
		ret = -ENOENT;

out:
	free_ipath(ipath);
	btrfs_free_path(path);

	return ret;
}
EXPORT_SYMBOL(btrfs_vfs_ino_to_path);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
/* copy from backref:iterate_irefs_t */
typedef int (iterate_irefs_t)(u64 parent, u32 name_len, unsigned long name_off,
			      struct extent_buffer *eb, void *ctx);

/* copy from backref:iterate_inode_refs */
static int iterate_inode_refs(u64 inum, struct btrfs_root *fs_root,
			      struct btrfs_path *path, struct btrfs_list_hardlinks_iter_index *index,
			      iterate_irefs_t *iterate, void *ctx)
{
	int ret = 0;
	int slot;
	u32 cur;
	u32 len;
	u32 name_len;
	u64 dir_index;
	u64 skip_dir = index->dir;
	u64 skip_dir_index = index->dir_index;
	u64 parent = index->dir;
	struct extent_buffer *eb;
	struct btrfs_item *item;
	struct btrfs_inode_ref *iref;
	struct btrfs_key found_key;

	while (!ret) {
		ret = btrfs_find_item(fs_root, path, inum,
				parent, BTRFS_INODE_REF_KEY,
				&found_key);

		if (ret < 0)
			break;
		if (ret) {
			ret = 0;
			break;
		}

		parent = found_key.offset;
		slot = path->slots[0];
		eb = btrfs_clone_extent_buffer(path->nodes[0]);
		if (!eb) {
			ret = -ENOMEM;
			break;
		}
		btrfs_release_path(path);

		item = btrfs_item_nr(slot);
		iref = btrfs_item_ptr(eb, slot, struct btrfs_inode_ref);

		for (cur = 0; cur < btrfs_item_size(eb, item); cur += len) {
			name_len = btrfs_inode_ref_name_len(eb, iref);
			dir_index = btrfs_inode_ref_index(eb, iref);

			if (parent < skip_dir)
				goto next;
			if (parent == skip_dir && dir_index <= skip_dir_index)
				goto next;
			ret = iterate(parent, name_len,
				      (unsigned long)(iref + 1), eb, ctx);
			if (ret)
				break;
next:
			if (parent > index->dir ||
				(parent == index->dir && dir_index > index->dir_index)) {
				index->dir = parent;
				index->dir_index = dir_index;
			}
			len = sizeof(*iref) + name_len;
			iref = (struct btrfs_inode_ref *)((char *)iref + len);
		}
		free_extent_buffer(eb);
		parent++;
	}

	btrfs_release_path(path);

	return ret;
}

/* copy from backref:iterate_inode_extrefs */
static int iterate_inode_extrefs(u64 inum, struct btrfs_root *fs_root,
				 struct btrfs_path *path, struct btrfs_list_hardlinks_iter_index *index,
				 iterate_irefs_t *iterate, void *ctx)
{
	int ret;
	int slot;
	u64 skip_offset = index->offset;
	u64 offset = index->offset;
	u64 parent;
	struct extent_buffer *eb;
	struct btrfs_inode_extref *extref;
	u32 item_size;
	u32 cur_offset;
	unsigned long ptr;

	while (1) {
		ret = btrfs_find_one_extref(fs_root, inum, offset, path, &extref,
					    &offset);
		if (ret < 0 && ret != -ENOENT)
			break;
		if (ret) {
			ret = 0;
			break;
		}

		if (offset <= skip_offset) {
			btrfs_release_path(path);
			goto next;
		}

		slot = path->slots[0];
		eb = btrfs_clone_extent_buffer(path->nodes[0]);
		if (!eb) {
			ret = -ENOMEM;
			break;
		}
		btrfs_release_path(path);

		item_size = btrfs_item_size_nr(eb, slot);
		ptr = btrfs_item_ptr_offset(eb, slot);
		cur_offset = 0;

		/*
		 * Because EXTREF is not sorted, all refs in the
		 * entire item must be output together, otherwise
		 * there will be a duplicate item next time.
		 *
		 * Because btrfs btrfs_hardlink_entry is smaller than
		 * btrfs_inode_extref, so we only need to check
		 * free space >= item size.
		 */
		if (index->free_space < item_size) {
			ret = -ENOSPC;
			goto free;
		}

		while (cur_offset < item_size) {
			u32 name_len;

			extref = (struct btrfs_inode_extref *)(ptr + cur_offset);
			parent = btrfs_inode_extref_parent(eb, extref);
			name_len = btrfs_inode_extref_name_len(eb, extref);
			ret = iterate(parent, name_len,
				      (unsigned long)&extref->name, eb, ctx);
			if (ret)
				break;

			cur_offset += btrfs_inode_extref_name_len(eb, extref);
			cur_offset += sizeof(*extref);
		}
free:
		free_extent_buffer(eb);
next:
		if (ret)
			break;
		if (offset > index->offset)
			index->offset = offset;
		offset++;
	}

	btrfs_release_path(path);

	return ret;
}

static int iterate_irefs(u64 inum, struct btrfs_root *fs_root,
			struct btrfs_path *path, struct btrfs_list_hardlinks_iter_index *index,
			iterate_irefs_t *iterate, void *ctx)
{
	int ret;

	if (index->type == SYNO_BTRFS_LIST_HARDLINKS_INDEX_TYPE_INODE_REF) {
		ret = iterate_inode_refs(inum, fs_root, path, index, iterate, ctx);
		if (ret)
			goto out;
		index->type = SYNO_BTRFS_LIST_HARDLINKS_INDEX_TYPE_INODE_EXTREF;
		index->dir = -1;
		index->dir_index = -1;
	}

	ret = iterate_inode_extrefs(inum, fs_root, path, index, iterate, ctx);
out:
	return ret;
}

static int record_hardlink(u64 inum, u32 name_len, unsigned long name_off,
			 struct extent_buffer *eb, void *ctx)
{
	int ret;
	struct btrfs_list_hardlinks_args *args = ctx;
	struct btrfs_hardlink_entry *entry;
	u32 entry_len = sizeof(*entry) + name_len + 1;
	unsigned long ptr;
	char *dest;

	if (entry_len > args->index.free_space) {
		ret = -ENOSPC;
		goto out;
	}

	ptr = (unsigned long)args->buf;
	ptr += args->index.cursor;
	entry = (struct btrfs_hardlink_entry *)ptr;
	dest = (char*)(entry + 1);

	entry->record_len = entry_len;
	entry->parent_inum = inum;
	entry->name_len = name_len;
	read_extent_buffer(eb, dest, name_off, name_len);
	dest[name_len] = '\0';

	args->elem_cnt++;
	args->index.cursor += entry_len;
	args->index.free_space -= entry_len;

	ret = 0;
out:
	return ret;
}

static int links_from_inum(struct btrfs_root *fs_root, struct btrfs_path *path, struct btrfs_list_hardlinks_iter_index *index, struct btrfs_list_hardlinks_args *args)
{
	return iterate_irefs(args->inum, fs_root, path, index, record_hardlink, args);
}

int btrfs_list_hardlinks(struct btrfs_list_hardlinks_args *args)
{
	int ret;
	struct btrfs_path *path = NULL;
	struct btrfs_root *root;

	if (!args ||
		!args->inode ||
		!S_ISREG(args->inode->i_mode) ||
		args->inode->i_sb->s_magic != BTRFS_SUPER_MAGIC ||
		!args->buf_size) {
		ret = -EINVAL;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	root = BTRFS_I(args->inode)->root;
	args->elem_cnt = 0;
	args->index.cursor = 0;
	args->index.free_space = args->buf_size;

	ret = links_from_inum(root, path, &args->index, args);
	if (ret < 0)
		goto out;

	ret = 0;
out:
	btrfs_free_path(path);

	return ret;
}
EXPORT_SYMBOL(btrfs_list_hardlinks);
#endif /* MY_ABC_HERE */

static int build_ino_list(u64 inum, u64 offset, u64 root, void *ctx
#ifdef MY_ABC_HERE
			  , int extent_type
#endif /* MY_ABC_HERE */
			  )
{
	struct btrfs_data_container *inodes = ctx;
	const size_t c = 3 * sizeof(u64);

	if (inodes->bytes_left >= c) {
		inodes->bytes_left -= c;
		inodes->val[inodes->elem_cnt] = inum;
		inodes->val[inodes->elem_cnt + 1] = offset;
		inodes->val[inodes->elem_cnt + 2] = root;
		inodes->elem_cnt += 3;
	} else {
		inodes->bytes_missing += c - inodes->bytes_left;
		inodes->bytes_left = 0;
		inodes->elem_missed += 3;
	}

	return 0;
}

static long btrfs_ioctl_logical_to_ino(struct btrfs_fs_info *fs_info,
					void __user *arg, int version)
{
	int ret = 0;
	int size;
	struct btrfs_ioctl_logical_ino_args *loi;
	struct btrfs_data_container *inodes = NULL;
	struct btrfs_path *path = NULL;
	bool ignore_offset;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	loi = memdup_user(arg, sizeof(*loi));
	if (IS_ERR(loi))
		return PTR_ERR(loi);

	if (version == 1) {
		ignore_offset = false;
		size = min_t(u32, loi->size, SZ_64K);
	} else {
		/* All reserved bits must be 0 for now */
		if (memchr_inv(loi->reserved, 0, sizeof(loi->reserved))) {
			ret = -EINVAL;
			goto out_loi;
		}
		/* Only accept flags we have defined so far */
		if (loi->flags & ~(BTRFS_LOGICAL_INO_ARGS_IGNORE_OFFSET)) {
			ret = -EINVAL;
			goto out_loi;
		}
		ignore_offset = loi->flags & BTRFS_LOGICAL_INO_ARGS_IGNORE_OFFSET;
		size = min_t(u32, loi->size, SZ_16M);
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	inodes = init_data_container(size);
	if (IS_ERR(inodes)) {
		ret = PTR_ERR(inodes);
		inodes = NULL;
		goto out;
	}

	ret = iterate_inodes_from_logical(loi->logical, fs_info, path,
					  build_ino_list, inodes, ignore_offset);
	if (ret == -EINVAL)
		ret = -ENOENT;
	if (ret < 0)
		goto out;

	ret = copy_to_user((void __user *)(unsigned long)loi->inodes, inodes,
			   size);
	if (ret)
		ret = -EFAULT;

out:
	btrfs_free_path(path);
	kvfree(inodes);
out_loi:
	kfree(loi);

	return ret;
}

void btrfs_update_ioctl_balance_args(struct btrfs_fs_info *fs_info,
			       struct btrfs_ioctl_balance_args *bargs)
{
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;

	bargs->flags = bctl->flags;

	if (test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags))
		bargs->state |= BTRFS_BALANCE_STATE_RUNNING;
	if (atomic_read(&fs_info->balance_pause_req))
		bargs->state |= BTRFS_BALANCE_STATE_PAUSE_REQ;
	if (atomic_read(&fs_info->balance_cancel_req))
		bargs->state |= BTRFS_BALANCE_STATE_CANCEL_REQ;

	memcpy(&bargs->data, &bctl->data, sizeof(bargs->data));
	memcpy(&bargs->meta, &bctl->meta, sizeof(bargs->meta));
	memcpy(&bargs->sys, &bctl->sys, sizeof(bargs->sys));

#ifdef MY_ABC_HERE
	bargs->total_chunk_used = bctl->total_chunk_used;
#endif /* SYNO_BTRFS_BALANCE_DRY_RUN */
	spin_lock(&fs_info->balance_lock);
	memcpy(&bargs->stat, &bctl->stat, sizeof(bargs->stat));
	spin_unlock(&fs_info->balance_lock);
}

static long btrfs_ioctl_balance(struct file *file, void __user *arg)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ioctl_balance_args *bargs;
	struct btrfs_balance_control *bctl;
	bool need_unlock; /* for mut. excl. ops lock */
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

again:
	if (btrfs_exclop_start(fs_info, BTRFS_EXCLOP_BALANCE)) {
		mutex_lock(&fs_info->balance_mutex);
		need_unlock = true;
		goto locked;
	}

	/*
	 * mut. excl. ops lock is locked.  Three possibilities:
	 *   (1) some other op is running
	 *   (2) balance is running
	 *   (3) balance is paused -- special case (think resume)
	 */
	mutex_lock(&fs_info->balance_mutex);
	if (fs_info->balance_ctl) {
		/* this is either (2) or (3) */
		if (!test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags)) {
			mutex_unlock(&fs_info->balance_mutex);
			/*
			 * Lock released to allow other waiters to continue,
			 * we'll reexamine the status again.
			 */
			mutex_lock(&fs_info->balance_mutex);

			if (fs_info->balance_ctl &&
			    !test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags)) {
				/* this is (3) */
				need_unlock = false;
				goto locked;
			}

			mutex_unlock(&fs_info->balance_mutex);
			goto again;
		} else {
			/* this is (2) */
			mutex_unlock(&fs_info->balance_mutex);
			ret = -EINPROGRESS;
			goto out;
		}
	} else {
		/* this is (1) */
		mutex_unlock(&fs_info->balance_mutex);
		ret = BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
		goto out;
	}

locked:

	if (arg) {
		bargs = memdup_user(arg, sizeof(*bargs));
		if (IS_ERR(bargs)) {
			ret = PTR_ERR(bargs);
			goto out_unlock;
		}

		if (bargs->flags & BTRFS_BALANCE_RESUME) {
			if (!fs_info->balance_ctl) {
				ret = -ENOTCONN;
				goto out_bargs;
			}

			bctl = fs_info->balance_ctl;
			spin_lock(&fs_info->balance_lock);
			bctl->flags |= BTRFS_BALANCE_RESUME;
			spin_unlock(&fs_info->balance_lock);

			goto do_balance;
		}
	} else {
		bargs = NULL;
	}

	if (fs_info->balance_ctl) {
		ret = -EINPROGRESS;
		goto out_bargs;
	}

	bctl = kzalloc(sizeof(*bctl), GFP_KERNEL);
	if (!bctl) {
		ret = -ENOMEM;
		goto out_bargs;
	}

	if (arg) {
		memcpy(&bctl->data, &bargs->data, sizeof(bctl->data));
		memcpy(&bctl->meta, &bargs->meta, sizeof(bctl->meta));
		memcpy(&bctl->sys, &bargs->sys, sizeof(bctl->sys));

		bctl->flags = bargs->flags;
	} else {
		/* balance everything - no filters */
		bctl->flags |= BTRFS_BALANCE_TYPE_MASK;
	}

#ifdef MY_ABC_HERE
	if (bargs->key_offset) {
		if (fs_info->super_copy->total_bytes <= 50ULL * SZ_1G) {
			ret = -ENOSPC;
			goto out_bctl;
		} else
			bctl->fast_key_offset = bargs->key_offset;
	}
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	bctl->total_chunk_used = 0;
	if (bctl->flags & ~(BTRFS_BALANCE_ARGS_MASK | BTRFS_BALANCE_TYPE_MASK | BTRFS_BALANCE_DRY_RUN)) {
#else
	if (bctl->flags & ~(BTRFS_BALANCE_ARGS_MASK | BTRFS_BALANCE_TYPE_MASK)) {
#endif /* SYNO_BTRFS_BALANCE_DRY_RUN */
		ret = -EINVAL;
		goto out_bctl;
	}

do_balance:
	/*
	 * Ownership of bctl and exclusive operation goes to btrfs_balance.
	 * bctl is freed in reset_balance_state, or, if restriper was paused
	 * all the way until unmount, in free_fs_info.  The flag should be
	 * cleared after reset_balance_state.
	 */
	need_unlock = false;

	ret = btrfs_balance(fs_info, bctl, bargs);
	bctl = NULL;

	if ((ret == 0 || ret == -ECANCELED) && arg) {
		if (copy_to_user(arg, bargs, sizeof(*bargs)))
			ret = -EFAULT;
	}

out_bctl:
	kfree(bctl);
out_bargs:
	kfree(bargs);
out_unlock:
	mutex_unlock(&fs_info->balance_mutex);
	if (need_unlock)
		btrfs_exclop_finish(fs_info);
out:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_balance_ctl(struct btrfs_fs_info *fs_info, int cmd)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd) {
	case BTRFS_BALANCE_CTL_PAUSE:
		return btrfs_pause_balance(fs_info);
	case BTRFS_BALANCE_CTL_CANCEL:
		return btrfs_cancel_balance(fs_info);
	}

	return -EINVAL;
}

static long btrfs_ioctl_balance_progress(struct btrfs_fs_info *fs_info,
					 void __user *arg)
{
	struct btrfs_ioctl_balance_args *bargs;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	mutex_lock(&fs_info->balance_mutex);
	if (!fs_info->balance_ctl) {
		ret = -ENOTCONN;
		goto out;
	}

	bargs = kzalloc(sizeof(*bargs), GFP_KERNEL);
	if (!bargs) {
		ret = -ENOMEM;
		goto out;
	}

	btrfs_update_ioctl_balance_args(fs_info, bargs);

	if (copy_to_user(arg, bargs, sizeof(*bargs)))
		ret = -EFAULT;

	kfree(bargs);
out:
	mutex_unlock(&fs_info->balance_mutex);
	return ret;
}

static long btrfs_ioctl_quota_ctl(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_quota_ctl_args *sa;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa)) {
		ret = PTR_ERR(sa);
		goto drop_write;
	}

	down_write(&fs_info->subvol_sem);

	switch (sa->cmd) {
	case BTRFS_QUOTA_CTL_ENABLE:
#ifdef MY_ABC_HERE
	case BTRFS_QUOTA_V1_CTL_ENABLE:
	case BTRFS_QUOTA_V2_CTL_ENABLE:
		ret = btrfs_quota_enable(fs_info, sa->cmd);
#else
		ret = btrfs_quota_enable(fs_info);
#endif /* MY_ABC_HERE */
		break;
	case BTRFS_QUOTA_CTL_DISABLE:
		ret = btrfs_quota_disable(fs_info);
		break;
#ifdef MY_ABC_HERE
	case BTRFS_QUOTA_CTL_UNLOAD:
		ret = btrfs_quota_unload(fs_info);
		break;
	case BTRFS_QUOTA_CTL_REMOVE_V1:
		ret = btrfs_quota_remove_v1(fs_info);
		break;
#endif /* MY_ABC_HERE */
	default:
		ret = -EINVAL;
		break;
	}

	kfree(sa);
	up_write(&fs_info->subvol_sem);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_qgroup_assign(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_qgroup_assign_args *sa;
	struct btrfs_trans_handle *trans;
	int ret;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa)) {
		ret = PTR_ERR(sa);
		goto drop_write;
	}

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	if (sa->assign) {
		ret = btrfs_add_qgroup_relation(trans, sa->src, sa->dst);
	} else {
		ret = btrfs_del_qgroup_relation(trans, sa->src, sa->dst);
	}

	/* update qgroup status and info */
	err = btrfs_run_qgroups(trans);
	if (err < 0)
		btrfs_handle_fs_error(fs_info, err,
				      "failed to update qgroup status and info");
	err = btrfs_end_transaction(trans);
	if (err && !ret)
		ret = err;

out:
	kfree(sa);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_qgroup_create(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_qgroup_create_args *sa;
	struct btrfs_trans_handle *trans;
	int ret;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa)) {
		ret = PTR_ERR(sa);
		goto drop_write;
	}

	if (!sa->qgroupid) {
		ret = -EINVAL;
		goto out;
	}

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	if (sa->create) {
		ret = btrfs_create_qgroup(trans, sa->qgroupid);
	} else {
		ret = btrfs_remove_qgroup(trans, sa->qgroupid);
	}

	err = btrfs_end_transaction(trans);
	if (err && !ret)
		ret = err;

out:
	kfree(sa);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_qgroup_limit(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_qgroup_limit_args *sa;
	struct btrfs_trans_handle *trans;
	int ret;
	int err;
	u64 qgroupid;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

#ifdef MY_ABC_HERE
	if (root->invalid_quota)
		return -ESRCH;
#endif /* MY_ABC_HERE */

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa)) {
		ret = PTR_ERR(sa);
		goto drop_write;
	}

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	qgroupid = sa->qgroupid;
	if (!qgroupid) {
		/* take the current subvol as qgroup */
		qgroupid = root->root_key.objectid;
	}

	ret = btrfs_limit_qgroup(trans, qgroupid, &sa->lim);

	err = btrfs_end_transaction(trans);
	if (err && !ret)
		ret = err;

out:
	kfree(sa);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_quota_rescan(struct file *file, void __user *arg)
{
#ifdef MY_ABC_HERE
	return -EOPNOTSUPP;
#else
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_quota_rescan_args *qsa;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	qsa = memdup_user(arg, sizeof(*qsa));
	if (IS_ERR(qsa)) {
		ret = PTR_ERR(qsa);
		goto drop_write;
	}

	if (qsa->flags) {
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_qgroup_rescan(fs_info);

out:
	kfree(qsa);
drop_write:
	mnt_drop_write_file(file);
	return ret;
#endif /* MY_ABC_HERE */
}

static long btrfs_ioctl_quota_rescan_status(struct btrfs_fs_info *fs_info,
						void __user *arg)
{
#ifdef MY_ABC_HERE
	return -EOPNOTSUPP;
#else
	struct btrfs_ioctl_quota_rescan_args *qsa;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	qsa = kzalloc(sizeof(*qsa), GFP_KERNEL);
	if (!qsa)
		return -ENOMEM;

	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		qsa->flags = 1;
		qsa->progress = fs_info->qgroup_rescan_progress.objectid;
	}

	if (copy_to_user(arg, qsa, sizeof(*qsa)))
		ret = -EFAULT;

	kfree(qsa);
	return ret;
#endif /* MY_ABC_HERE */
}

static long btrfs_ioctl_quota_rescan_wait(struct btrfs_fs_info *fs_info,
						void __user *arg)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return btrfs_qgroup_wait_for_completion(fs_info, true);
}

#ifdef MY_ABC_HERE
static long btrfs_ioctl_usrquota_ctl(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_usrquota_ctl_args *ctl_args;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	ctl_args = memdup_user(arg, sizeof(*ctl_args));
	if (IS_ERR(ctl_args)) {
		ret = PTR_ERR(ctl_args);
		goto drop_write;
	}

	if (ctl_args->cmd == BTRFS_USRQUOTA_CTL_DUMPTREE) {
		ret = -EOPNOTSUPP;
		goto free_ctl_args;
	}

	down_write(&fs_info->subvol_sem);

	switch (ctl_args->cmd) {
	case BTRFS_USRQUOTA_CTL_ENABLE:
	case BTRFS_USRQUOTA_V1_CTL_ENABLE:
	case BTRFS_USRQUOTA_V2_CTL_ENABLE:
		ret = btrfs_usrquota_enable(fs_info, ctl_args->cmd);
		break;
	case BTRFS_USRQUOTA_CTL_DISABLE:
		ret = btrfs_usrquota_disable(fs_info);
		break;
	case BTRFS_USRQUOTA_CTL_UNLOAD:
		ret = btrfs_usrquota_unload(fs_info);
		break;
	case BTRFS_USRQUOTA_CTL_REMOVE_V1:
		ret = btrfs_usrquota_remove_v1(fs_info);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	up_write(&fs_info->subvol_sem);
free_ctl_args:
	kfree(ctl_args);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_usrquota_limit(struct file *file, void __user *arg)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_ioctl_usrquota_limit_args *limit_args;
	struct btrfs_trans_handle *trans;
	int ret;
	int err;
	u64 rootid;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

#ifdef MY_ABC_HERE
	if (root->invalid_quota)
		return -ESRCH;
#endif /* MY_ABC_HERE */

	if (btrfs_root_readonly(root))
		return -EROFS;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	limit_args = memdup_user(arg, sizeof(*limit_args));
	if (IS_ERR(limit_args)) {
		ret = PTR_ERR(limit_args);
		goto drop_write;
	}

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	rootid = root->root_key.objectid;
	ret = btrfs_usrquota_limit(trans, rootid,
	                           limit_args->uid, limit_args->rfer_soft,
	                           limit_args->rfer_hard);

	err = btrfs_end_transaction(trans);
	if (err && !ret)
		ret = err;

out:
	kfree(limit_args);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_usrquota_rescan(struct file *file)
{
	// Please use qgroup rescan.
	return -EOPNOTSUPP;
}

static long btrfs_ioctl_usrquota_rescan_status(struct file *file, void __user *arg)
{
	// Please use qgroup rescan.
	return -EOPNOTSUPP;
}

static inline long btrfs_ioctl_usrquota_rescan_wait(struct file *file)
{
	// Please use qgroup rescan.
	return -EOPNOTSUPP;
}

static long btrfs_ioctl_usrquota_query(struct file *file, void __user *arg)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_ioctl_usrquota_query_args uqa;
	int ret = 0;

	if (copy_from_user(&uqa, arg, sizeof(uqa))) {
		ret = -EFAULT;
		goto out;
	}

	ret = btrfs_usrquota_query(root, &uqa);
	if (ret)
		goto out;

	if (copy_to_user(arg, &uqa, sizeof(uqa)))
		ret = -EFAULT;
out:
	return ret;
}

static long btrfs_ioctl_usrquota_clean(struct file *file, void __user *arg)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_trans_handle *trans;
	int ret, err;
	u64 uid;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	if (copy_from_user(&uid, arg, sizeof(uid))) {
		ret = -EFAULT;
		goto out;
	}
	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	ret = btrfs_usrquota_clean(trans, uid);
	err = btrfs_end_transaction(trans);
	if (err && !ret)
		ret = err;
out:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_syno_quota_rescan(struct file *file, void __user *arg)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ioctl_syno_quota_rescan_args *qsa;
	struct btrfs_trans_handle *trans;
	int ret;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags))
		return -ESRCH;

	qsa = memdup_user(arg, sizeof(*qsa));
	if (IS_ERR(qsa)) {
		ret = PTR_ERR(qsa);
		return ret;
	}

	switch (qsa->flags) {
	case BTRFS_SYNO_QUOTA_RESCAN:
		ret = mnt_want_write_file(file);
		if (ret)
			break;

		ret = btrfs_syno_quota_rescan(root);

		mnt_drop_write_file(file);
		break;
	case BTRFS_SYNO_QUOTA_RESCAN_PAUSE:
		if (!fs_info->qgroup_rescan_running) {
			btrfs_info(fs_info, "Syno quota rescan is not running.");
			ret = -ENOENT;
		} else {
			fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_PAUSE;
			btrfs_info(fs_info, "Sending pause to syno quota rescan worker.");
			ret = 0;
		}
		break;
	case BTRFS_SYNO_QUOTA_RESCAN_RESUME:
		if (fs_info->qgroup_rescan_running) {
			btrfs_info(fs_info, "Syno quota rescan is already running.");
			ret = -EEXIST;
		} else if (!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN)) {
			btrfs_info(fs_info, "No quota rescan work to resume.");
			ret = -ENOENT;
		} else {
			btrfs_qgroup_rescan_resume(fs_info);
			btrfs_info(fs_info, "Syno quota rescan has been resumed.");
			ret = 0;
		}
		break;
	case BTRFS_SYNO_QUOTA_RESCAN_SET_VOL_V2:
		ret = mnt_want_write_file(file);
		if (ret)
			break;

		trans = btrfs_start_transaction(fs_info->fs_root, 2);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			mnt_drop_write_file(file);
			break;
		}

		ret = 0;
		err = btrfs_reset_qgroup_status(trans);
		if (err) {
			btrfs_err(fs_info, "Failed to set qgroup status to v2.");
			ret = err;
		}

		err = btrfs_reset_usrquota_status(trans);
		if (err) {
			btrfs_err(fs_info, "Failed to set usrquota status to v2.");
			ret = err;
		}

		err = btrfs_commit_transaction(trans);
		if (err)
			ret = err;
		mnt_drop_write_file(file);
		break;
	case BTRFS_SYNO_QUOTA_RESCAN_TRANSFER_LIMIT:
		ret = mnt_want_write_file(file);
		if (ret)
			break;

		ret = 0;
		err = btrfs_syno_qgroup_transfer_limit(root);
		if (err)
			ret = err;

		err = btrfs_syno_usrquota_transfer_limit(root);
		if (err)
			ret = err;

		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}
		err = btrfs_commit_transaction(trans);
		if (err)
			ret = err;
		mnt_drop_write_file(file);
		break;
	default:
		ret = -EINVAL;
	}

	kfree(qsa);
	return ret;
}

static long btrfs_ioctl_syno_quota_status(struct file *file, void __user *arg)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_ioctl_syno_quota_status_args sa;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&sa, arg, sizeof(sa)))
		return -EFAULT;

	ret = btrfs_syno_quota_status(root, &sa);

	if (ret == 0 && copy_to_user(arg, &sa, sizeof(sa)))
		ret = -EFAULT;

	return ret;
}
#endif /* MY_ABC_HERE */

static long _btrfs_ioctl_set_received_subvol(struct file *file,
					    struct btrfs_ioctl_received_subvol_args *sa)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_root_item *root_item = &root->root_item;
	struct btrfs_trans_handle *trans;
	struct timespec64 ct = current_time(inode);
	int ret = 0;
	int received_uuid_changed;

	if (!inode_owner_or_capable(inode))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret < 0)
		return ret;

	down_write(&fs_info->subvol_sem);

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID) {
		ret = -EINVAL;
		goto out;
	}

	if (btrfs_root_readonly(root)) {
		ret = -EROFS;
		goto out;
	}

	/*
	 * 1 - root item
	 * 2 - uuid items (received uuid + subvol uuid)
	 */
	trans = btrfs_start_transaction(root, 3);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	sa->rtransid = trans->transid;
	sa->rtime.sec = ct.tv_sec;
	sa->rtime.nsec = ct.tv_nsec;

	received_uuid_changed = memcmp(root_item->received_uuid, sa->uuid,
				       BTRFS_UUID_SIZE);
	if (received_uuid_changed &&
	    !btrfs_is_empty_uuid(root_item->received_uuid)) {
		ret = btrfs_uuid_tree_remove(trans, root_item->received_uuid,
					  BTRFS_UUID_KEY_RECEIVED_SUBVOL,
					  root->root_key.objectid);
		if (ret && ret != -ENOENT) {
		        btrfs_abort_transaction(trans, ret);
		        btrfs_end_transaction(trans);
		        goto out;
		}
	}
	memcpy(root_item->received_uuid, sa->uuid, BTRFS_UUID_SIZE);
	btrfs_set_root_stransid(root_item, sa->stransid);
	btrfs_set_root_rtransid(root_item, sa->rtransid);
	btrfs_set_stack_timespec_sec(&root_item->stime, sa->stime.sec);
	btrfs_set_stack_timespec_nsec(&root_item->stime, sa->stime.nsec);
	btrfs_set_stack_timespec_sec(&root_item->rtime, sa->rtime.sec);
	btrfs_set_stack_timespec_nsec(&root_item->rtime, sa->rtime.nsec);
#ifdef MY_ABC_HERE
	btrfs_set_stack_timespec_sec(&root_item->otime, sa->otime.sec);
	btrfs_set_stack_timespec_nsec(&root_item->otime, sa->otime.nsec);
#endif /* MY_ABC_HERE */


	ret = btrfs_update_root(trans, fs_info->tree_root,
				&root->root_key, &root->root_item);
	if (ret < 0) {
		btrfs_end_transaction(trans);
		goto out;
	}
	if (received_uuid_changed && !btrfs_is_empty_uuid(sa->uuid)) {
		ret = btrfs_uuid_tree_add(trans, sa->uuid,
					  BTRFS_UUID_KEY_RECEIVED_SUBVOL,
					  root->root_key.objectid);
		if (ret < 0 && ret != -EEXIST) {
			btrfs_abort_transaction(trans, ret);
			btrfs_end_transaction(trans);
			goto out;
		}
	}
	ret = btrfs_commit_transaction(trans);
out:
	up_write(&fs_info->subvol_sem);
	mnt_drop_write_file(file);
	return ret;
}

#ifdef CONFIG_64BIT
static long btrfs_ioctl_set_received_subvol_32(struct file *file,
						void __user *arg)
{
	struct btrfs_ioctl_received_subvol_args_32 *args32 = NULL;
	struct btrfs_ioctl_received_subvol_args *args64 = NULL;
	int ret = 0;

	args32 = memdup_user(arg, sizeof(*args32));
	if (IS_ERR(args32))
		return PTR_ERR(args32);

	args64 = kmalloc(sizeof(*args64), GFP_KERNEL);
	if (!args64) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(args64->uuid, args32->uuid, BTRFS_UUID_SIZE);
	args64->stransid = args32->stransid;
	args64->rtransid = args32->rtransid;
	args64->stime.sec = args32->stime.sec;
	args64->stime.nsec = args32->stime.nsec;
	args64->rtime.sec = args32->rtime.sec;
	args64->rtime.nsec = args32->rtime.nsec;
#ifdef MY_ABC_HERE
	args64->otime.sec = args32->otime.sec;
	args64->otime.nsec = args32->otime.nsec;
#endif /* MY_ABC_HERE */
	args64->flags = args32->flags;

	ret = _btrfs_ioctl_set_received_subvol(file, args64);
	if (ret)
		goto out;

	memcpy(args32->uuid, args64->uuid, BTRFS_UUID_SIZE);
	args32->stransid = args64->stransid;
	args32->rtransid = args64->rtransid;
	args32->stime.sec = args64->stime.sec;
	args32->stime.nsec = args64->stime.nsec;
	args32->rtime.sec = args64->rtime.sec;
	args32->rtime.nsec = args64->rtime.nsec;
#ifdef MY_ABC_HERE
	args32->otime.sec = args64->otime.sec;
	args32->otime.nsec = args64->otime.nsec;
#endif /* MY_ABC_HERE */
	args32->flags = args64->flags;

	ret = copy_to_user(arg, args32, sizeof(*args32));
	if (ret)
		ret = -EFAULT;

out:
	kfree(args32);
	kfree(args64);
	return ret;
}
#endif

static long btrfs_ioctl_set_received_subvol(struct file *file,
					    void __user *arg)
{
	struct btrfs_ioctl_received_subvol_args *sa = NULL;
	int ret = 0;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	ret = _btrfs_ioctl_set_received_subvol(file, sa);

	if (ret)
		goto out;

	ret = copy_to_user(arg, sa, sizeof(*sa));
	if (ret)
		ret = -EFAULT;

out:
	kfree(sa);
	return ret;
}

static int btrfs_ioctl_get_fslabel(struct btrfs_fs_info *fs_info,
					void __user *arg)
{
	size_t len;
	int ret;
	char label[BTRFS_LABEL_SIZE];

	spin_lock(&fs_info->super_lock);
	memcpy(label, fs_info->super_copy->label, BTRFS_LABEL_SIZE);
	spin_unlock(&fs_info->super_lock);

	len = strnlen(label, BTRFS_LABEL_SIZE);

	if (len == BTRFS_LABEL_SIZE) {
		btrfs_warn(fs_info,
			   "label is too long, return the first %zu bytes",
			   --len);
	}

	ret = copy_to_user(arg, label, len);

	return ret ? -EFAULT : 0;
}

static int btrfs_ioctl_set_fslabel(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_super_block *super_block = fs_info->super_copy;
	struct btrfs_trans_handle *trans;
	char label[BTRFS_LABEL_SIZE];
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(label, arg, sizeof(label)))
		return -EFAULT;

	if (strnlen(label, BTRFS_LABEL_SIZE) == BTRFS_LABEL_SIZE) {
		btrfs_err(fs_info,
			  "unable to set label with more than %d bytes",
			  BTRFS_LABEL_SIZE - 1);
		return -EINVAL;
	}

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_unlock;
	}

	spin_lock(&fs_info->super_lock);
	strcpy(super_block->label, label);
	spin_unlock(&fs_info->super_lock);
	ret = btrfs_commit_transaction(trans);

out_unlock:
	mnt_drop_write_file(file);
	return ret;
}

#define INIT_FEATURE_FLAGS(suffix) \
	{ .compat_flags = BTRFS_FEATURE_COMPAT_##suffix, \
	  .compat_ro_flags = BTRFS_FEATURE_COMPAT_RO_##suffix, \
	  .incompat_flags = BTRFS_FEATURE_INCOMPAT_##suffix }

int btrfs_ioctl_get_supported_features(void __user *arg)
{
	static const struct btrfs_ioctl_feature_flags features[3] = {
		INIT_FEATURE_FLAGS(SUPP),
		INIT_FEATURE_FLAGS(SAFE_SET),
		INIT_FEATURE_FLAGS(SAFE_CLEAR)
	};

	if (copy_to_user(arg, &features, sizeof(features)))
		return -EFAULT;

	return 0;
}

static int btrfs_ioctl_get_features(struct btrfs_fs_info *fs_info,
					void __user *arg)
{
	struct btrfs_super_block *super_block = fs_info->super_copy;
	struct btrfs_ioctl_feature_flags features;

	features.compat_flags = btrfs_super_compat_flags(super_block);
	features.compat_ro_flags = btrfs_super_compat_ro_flags(super_block);
	features.incompat_flags = btrfs_super_incompat_flags(super_block);

	if (copy_to_user(arg, &features, sizeof(features)))
		return -EFAULT;

	return 0;
}

static int check_feature_bits(struct btrfs_fs_info *fs_info,
			      enum btrfs_feature_set set,
			      u64 change_mask, u64 flags, u64 supported_flags,
			      u64 safe_set, u64 safe_clear)
{
	const char *type = btrfs_feature_set_name(set);
	char *names;
	u64 disallowed, unsupported;
	u64 set_mask = flags & change_mask;
	u64 clear_mask = ~flags & change_mask;

	unsupported = set_mask & ~supported_flags;
	if (unsupported) {
		names = btrfs_printable_features(set, unsupported);
		if (names) {
			btrfs_warn(fs_info,
				   "this kernel does not support the %s feature bit%s",
				   names, strchr(names, ',') ? "s" : "");
			kfree(names);
		} else
			btrfs_warn(fs_info,
				   "this kernel does not support %s bits 0x%llx",
				   type, unsupported);
		return -EOPNOTSUPP;
	}

	disallowed = set_mask & ~safe_set;
	if (disallowed) {
		names = btrfs_printable_features(set, disallowed);
		if (names) {
			btrfs_warn(fs_info,
				   "can't set the %s feature bit%s while mounted",
				   names, strchr(names, ',') ? "s" : "");
			kfree(names);
		} else
			btrfs_warn(fs_info,
				   "can't set %s bits 0x%llx while mounted",
				   type, disallowed);
		return -EPERM;
	}

	disallowed = clear_mask & ~safe_clear;
	if (disallowed) {
		names = btrfs_printable_features(set, disallowed);
		if (names) {
			btrfs_warn(fs_info,
				   "can't clear the %s feature bit%s while mounted",
				   names, strchr(names, ',') ? "s" : "");
			kfree(names);
		} else
			btrfs_warn(fs_info,
				   "can't clear %s bits 0x%llx while mounted",
				   type, disallowed);
		return -EPERM;
	}

	return 0;
}

#define check_feature(fs_info, change_mask, flags, mask_base)	\
check_feature_bits(fs_info, FEAT_##mask_base, change_mask, flags,	\
		   BTRFS_FEATURE_ ## mask_base ## _SUPP,	\
		   BTRFS_FEATURE_ ## mask_base ## _SAFE_SET,	\
		   BTRFS_FEATURE_ ## mask_base ## _SAFE_CLEAR)

static int btrfs_ioctl_set_features(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_super_block *super_block = fs_info->super_copy;
	struct btrfs_ioctl_feature_flags flags[2];
	struct btrfs_trans_handle *trans;
	u64 newflags;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(flags, arg, sizeof(flags)))
		return -EFAULT;

	/* Nothing to do */
	if (!flags[0].compat_flags && !flags[0].compat_ro_flags &&
	    !flags[0].incompat_flags)
		return 0;

	ret = check_feature(fs_info, flags[0].compat_flags,
			    flags[1].compat_flags, COMPAT);
	if (ret)
		return ret;

	ret = check_feature(fs_info, flags[0].compat_ro_flags,
			    flags[1].compat_ro_flags, COMPAT_RO);
	if (ret)
		return ret;

	ret = check_feature(fs_info, flags[0].incompat_flags,
			    flags[1].incompat_flags, INCOMPAT);
	if (ret)
		return ret;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_drop_write;
	}

	spin_lock(&fs_info->super_lock);
	newflags = btrfs_super_compat_flags(super_block);
	newflags |= flags[0].compat_flags & flags[1].compat_flags;
	newflags &= ~(flags[0].compat_flags & ~flags[1].compat_flags);
	btrfs_set_super_compat_flags(super_block, newflags);

	newflags = btrfs_super_compat_ro_flags(super_block);
	newflags |= flags[0].compat_ro_flags & flags[1].compat_ro_flags;
	newflags &= ~(flags[0].compat_ro_flags & ~flags[1].compat_ro_flags);
	btrfs_set_super_compat_ro_flags(super_block, newflags);

	newflags = btrfs_super_incompat_flags(super_block);
	newflags |= flags[0].incompat_flags & flags[1].incompat_flags;
	newflags &= ~(flags[0].incompat_flags & ~flags[1].incompat_flags);
	btrfs_set_super_incompat_flags(super_block, newflags);

	spin_unlock(&fs_info->super_lock);

	ret = btrfs_commit_transaction(trans);
out_drop_write:
	mnt_drop_write_file(file);

	return ret;
}

#ifdef MY_ABC_HERE
/*
 * For backward compatiblity, we should not put capability flags into
 * `struct btrfs_ioctl_feature_flags`.
 */
static int btrfs_ioctl_get_syno_flags(struct file *file, void __user *arg)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_super_block *super_block = root->fs_info->super_copy;
	struct btrfs_ioctl_syno_flags flags;

	flags.syno_capability_flags = btrfs_super_syno_capability_flags(super_block);

	if (copy_to_user(arg, &flags, sizeof(flags)))
		return -EFAULT;
	return 0;
}

static int btrfs_ioctl_set_syno_flags(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_super_block *super_block = root->fs_info->super_copy;
	struct btrfs_ioctl_syno_flags flags[2];
	struct btrfs_trans_handle *trans;
	u64 newflags;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(flags, arg, sizeof(flags)))
		return -EFAULT;

	/* Nothing to do */
	if (!flags[0].syno_capability_flags)
		return 0;

	ret = check_feature(fs_info, flags[0].syno_capability_flags,
			    flags[1].syno_capability_flags, SYNO_CAPABILITY);
	if (ret)
		return ret;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_drop_write;
	}

	spin_lock(&fs_info->super_lock);

	newflags = btrfs_super_syno_capability_flags(super_block);
	newflags |= flags[0].syno_capability_flags & flags[1].syno_capability_flags;
	newflags &= ~(flags[0].syno_capability_flags & ~flags[1].syno_capability_flags);
	btrfs_set_super_syno_capability_flags(super_block, newflags);

	spin_unlock(&fs_info->super_lock);

	ret = btrfs_commit_transaction(trans);
out_drop_write:
	mnt_drop_write_file(file);

	return ret;
}

#endif /* MY_ABC_HERE */

static int _btrfs_ioctl_send(struct file *file, void __user *argp, bool compat)
{
	struct btrfs_ioctl_send_args *arg;
	int ret;

	if (compat) {
#if defined(CONFIG_64BIT) && defined(CONFIG_COMPAT)
		struct btrfs_ioctl_send_args_32 args32;

		ret = copy_from_user(&args32, argp, sizeof(args32));
		if (ret)
			return -EFAULT;
		arg = kzalloc(sizeof(*arg), GFP_KERNEL);
		if (!arg)
			return -ENOMEM;
		arg->send_fd = args32.send_fd;
		arg->clone_sources_count = args32.clone_sources_count;
		arg->clone_sources = compat_ptr(args32.clone_sources);
		arg->parent_root = args32.parent_root;
		arg->flags = args32.flags;
		memcpy(arg->reserved, args32.reserved,
		       sizeof(args32.reserved));
#else
		return -ENOTTY;
#endif
	} else {
		arg = memdup_user(argp, sizeof(*arg));
		if (IS_ERR(arg))
			return PTR_ERR(arg);
	}
	ret = btrfs_ioctl_send(file, arg);
#ifdef MY_ABC_HERE
	if (copy_to_user(argp, arg, sizeof(*arg))) {
		ret = -EFAULT;
		goto out;
	}

out:
#endif /* MY_ABC_HERE */
	kfree(arg);
	return ret;
}

#ifdef MY_ABC_HERE
static long btrfs_ioctl_qgroup_query(struct file *file, void __user *arg)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_ioctl_qgroup_query_args qqa;
	int ret = 0;

	memset(&qqa, 0, sizeof(qqa));

	// use subvol id as qgroup id
	ret = btrfs_qgroup_query(root, &qqa);
	if (ret)
		goto out;

	if (copy_to_user(arg, &qqa, sizeof(qqa)))
		ret = -EFAULT;
out:
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int btrfs_ioctl_syno_reserve_log_tree_bg(struct file *file,
			  struct btrfs_ioctl_log_tree_reserve_bg_args __user *argp)
{
	struct btrfs_ioctl_log_tree_reserve_bg_args rsv_args;
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_bio *multi = NULL;
	u64 rsv_start = 0;
	u64 rsv_size = 0;
	u64 length = fs_info->nodesize;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&rsv_args, argp, sizeof(rsv_args)))
		return -EFAULT;

	mutex_lock(&fs_info->log_tree_rsv_alloc);

	switch(rsv_args.flags) {
	case BTRFS_LOG_TREE_BG_RSV_REMOVE:
		fs_info->log_tree_rsv_start = 0;
		fs_info->log_tree_rsv_size = 0;
		goto out;
	case BTRFS_LOG_TREE_BG_RSV_ADD:
		if (fs_info->log_tree_rsv_start) {
			rsv_start = fs_info->log_tree_rsv_start;
			rsv_size = fs_info->log_tree_rsv_size;
			goto map_logical;
		}
		ret = btrfs_reserve_log_tree_bg(root, &rsv_start, &rsv_size);
		if (ret)
			goto out;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

map_logical:
	ret = btrfs_map_block(fs_info, READ, rsv_start, &length, &multi, 1);
out:
	if (!ret && rsv_start) {
		if (put_user(rsv_start, &argp->start) ||
		    put_user(rsv_size, &argp->size) ||
		    put_user(multi->stripes[0].physical, &argp->map_start))
			ret = -EINVAL;
	}
	kfree(multi);
	mutex_unlock(&fs_info->log_tree_rsv_alloc);
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int btrfs_ioctl_free_space_analyze(struct file *file, struct btrfs_ioctl_free_space_analyze_args __user *argp)
{
	int ret = 0;
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ioctl_free_space_analyze_args args;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE))
		return -EOPNOTSUPP;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	if (!mutex_trylock(&fs_info->free_space_analyze_ioctl_lock))
		return -EBUSY;

	if (args.flags & BTRFS_FREE_SPACE_ANALYZE_FLAG_FULL)
		ret = btrfs_free_space_analyze_full(fs_info, &args);
	else
		ret = btrfs_free_space_analyze(fs_info, &args);

	mutex_unlock(&fs_info->free_space_analyze_ioctl_lock);

	if (copy_to_user(argp, &args, sizeof(args)))
		return -EFAULT;

	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int btrfs_ioctl_syno_find_next_chunk_info(struct file *file,
			  struct btrfs_ioctl_find_next_chunk_info_args __user *argp)
{
	int ret = -1;
	struct btrfs_ioctl_find_next_chunk_info_args args;
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_group *block_group = NULL;
	u64 profile;
	u64 length;
	struct btrfs_bio *bbio = NULL;
	int i;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	block_group = btrfs_lookup_first_block_group(fs_info, args.start);
	while (block_group) {
		if (block_group->flags & args.flags)
			break;
		block_group = btrfs_next_block_group(block_group);
	}

	args.stripe_count = 0;

	if (block_group) {
		profile = block_group->flags & BTRFS_BLOCK_GROUP_PROFILE_MASK;
		if ((profile & BTRFS_BLOCK_GROUP_DUP) || !profile) {
			length = block_group->length;
			ret = btrfs_map_block(fs_info, BTRFS_MAP_GET_READ_MIRRORS,
					      block_group->start, &length,
					      &bbio, 0);
			if (ret || !bbio) {
				if (!ret)
					ret = -EIO;
				goto out;
			}
			args.start = block_group->start;
			args.size = block_group->length;
			args.stripe_count = bbio->num_stripes > 2 ? 2 : bbio->num_stripes;

			for (i = 0; i < args.stripe_count; i++)
				args.stripe_offset[i] = bbio->stripes[i].physical;
		}
	}

	if (copy_to_user(argp, &args, sizeof(args))) {
		ret = -EFAULT;
		goto out;
	}
	ret = 0;
out:
	if (block_group)
		btrfs_put_block_group(block_group);
	btrfs_put_bbio(bbio);
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define LOOKUP_COMPR_FILE_READA_THR ((20 * SZ_1M))

static long btrfs_ioctl_compr_ctl(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_inode *btrfs_inode = BTRFS_I(inode);
	struct btrfs_ioctl_compr_ctl_args compr_args;
	struct btrfs_root *root = btrfs_inode->root;
	struct btrfs_path *path = NULL;
	struct ulist *disko_ulist = NULL;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key found_key;
	int ret = 0;
	int extent_type;
	int slot;
	u64 ino = btrfs_ino(btrfs_inode);
	u64 disko;
	u64 len;
	u64 compressed_size = 0;
	u64 size = 0;

	if (S_ISDIR(inode->i_mode))
		return -EISDIR;

	if (copy_from_user(&compr_args, arg, sizeof(compr_args)))
		return -EFAULT;

	if (compr_args.flags & BTRFS_COMPR_CTL_SET)
		return -EOPNOTSUPP;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	disko_ulist = ulist_alloc(GFP_NOFS);
	if (!disko_ulist) {
		ret = -ENOMEM;
		goto out_free;
	}

	inode_lock(inode);

	/*
	 * do any pending delalloc/csum calc on inode, one way or
	 * another, and lock file content
	 */
	btrfs_wait_ordered_range(inode, 0, (u64)-1);
	len = i_size_read(inode);
	lock_extent(&btrfs_inode->io_tree, 0, len);

	if (len > LOOKUP_COMPR_FILE_READA_THR) // May be many file extent items, do readahead.
		path->reada = READA_FORWARD;

	ret = btrfs_lookup_file_extent(NULL, root, path, ino, 0, 0);
	if (ret < 0)
		goto out_unlock;
	else if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
		ret = btrfs_next_leaf(root, path);
		if (ret < 0)
			goto out_unlock;
		else if (ret > 0)
			goto done;
	}

	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.objectid != ino ||
		    found_key.type != BTRFS_EXTENT_DATA_KEY)
			break;

		fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
		extent_type = btrfs_file_extent_type(leaf, fi);

		if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
			disko = btrfs_file_extent_disk_bytenr(leaf, fi);
			if (disko &&
			    ulist_add_lru_adjust(disko_ulist, disko, 0, GFP_NOFS)) {
				compressed_size += btrfs_file_extent_disk_num_bytes(
							leaf, fi);
				size += btrfs_file_extent_num_bytes(leaf, fi);
				if (disko_ulist->nnodes > ULIST_NODES_MAX)
					ulist_remove_first(disko_ulist);
			}
		} else {
			compressed_size += btrfs_file_extent_inline_item_len(
						leaf, btrfs_item_nr(slot));
			size += btrfs_file_extent_ram_bytes(leaf, fi);
		}
		ret = btrfs_next_item(root, path);
		if (ret < 0)
			goto out_unlock;
		if (ret > 0)
			break;
	}
done:
	ret = 0;

	compr_args.size = size;
	compr_args.compressed_size = compressed_size;
	if (btrfs_inode->prop_compress != BTRFS_COMPRESS_NONE)
		compr_args.flags |= BTRFS_COMPR_CTL_COMPR_FL;

	if (copy_to_user(arg, &compr_args, sizeof(compr_args)))
		ret = -EFAULT;

out_unlock:
	unlock_extent(&btrfs_inode->io_tree, 0, len);
	inode_unlock(inode);

out_free:
	ulist_free(disko_ulist);
	btrfs_free_path(path);

	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int btrfs_ioctl_snapshot_size_query(struct file *file,
					   void __user *argp)
{
	struct btrfs_ioctl_snapshot_size_query_args snap_args;
	struct btrfs_ioctl_snapshot_size_id_size_map *user_id_maps;
	size_t id_maps_size;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&snap_args, argp, sizeof(snap_args)))
		return -EFAULT;

	if (!snap_args.snap_count || 0 > snap_args.fd)
		return -EINVAL;

	id_maps_size = sizeof(struct btrfs_ioctl_snapshot_size_id_size_map) *
		       snap_args.snap_count;
	user_id_maps = snap_args.id_maps;

	if (!access_ok(snap_args.id_maps, id_maps_size))
		return -EFAULT;

	snap_args.id_maps = memdup_user(snap_args.id_maps, id_maps_size);
	if (IS_ERR(snap_args.id_maps))
		return PTR_ERR(snap_args.id_maps);

	ret = btrfs_snapshot_size_query(file, &snap_args);

	if (copy_to_user(argp + offsetof(
			 struct btrfs_ioctl_snapshot_size_query_args,
			 calc_size), &snap_args.calc_size,
			 sizeof(snap_args.calc_size))) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_to_user(argp + offsetof(
			 struct btrfs_ioctl_snapshot_size_query_args,
			 processed_size), &snap_args.processed_size,
			 sizeof(snap_args.processed_size))) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_to_user(user_id_maps, snap_args.id_maps, id_maps_size)) {
		ret = -EFAULT;
		goto out;
	}

	if (ret > 0)
		ret = 0;
out:
	kfree(snap_args.id_maps);
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static void __btrfs_syno_usage_rescan_progress_accounting(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 root_new_total_size;
	if (!(root->syno_usage_root_status.flags & BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_PROGRESS_ACCOUNTING)) {
		root_new_total_size = btrfs_root_used(&root->root_item);
		if (root_new_total_size > root->syno_usage_root_status.total_full_rescan_size)
			root->syno_usage_root_status.total_full_rescan_size = root_new_total_size;
		root->syno_usage_root_status.flags |= BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_PROGRESS_ACCOUNTING;
		spin_lock(&fs_info->syno_usage_lock);
		fs_info->syno_usage_status.total_full_rescan_size += (root->syno_usage_root_status.total_full_rescan_size -
															  root->syno_usage_root_status.cur_full_rescan_size);
		spin_unlock(&fs_info->syno_usage_lock);
	}
}

static int btrfs_ioctl_syno_usage_subvol_type_set(struct file *file,
					struct btrfs_ioctl_syno_usage_ctl_args *syno_usage_ctl_args,
					struct btrfs_ioctl_syno_usage_ctl_args __user *argp)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_syno_usage_root_status *usage_root_status;
	int ret = 0;
	bool resume = false;
	struct btrfs_key first_key, last_key;

	first_key.objectid = 0;
	first_key.type = 0;
	first_key.offset = 0;
	last_key.objectid = -1;
	last_key.type = -1;
	last_key.offset = -1;

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID) {
		ret = -EINVAL;
		goto out;
	}

	if (syno_usage_ctl_args->type >= SYNO_USAGE_TYPE_MAX ||
		syno_usage_ctl_args->type == SYNO_USAGE_TYPE_NONE) {
		ret = -EINVAL;
		goto out;
	}

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		goto out;

	btrfs_syno_usage_root_initialize(root);
	if (!test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state) ||
		root->syno_usage_root_status.new_type == syno_usage_ctl_args->type)
		goto out;

	if (btrfs_root_readonly(root))
		goto out;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	spin_lock(&root->syno_usage_lock);
	usage_root_status = &root->syno_usage_root_status;
	if (usage_root_status->new_type == syno_usage_ctl_args->type) {
		spin_unlock(&root->syno_usage_lock);
		goto out;
	}

	if (test_bit(SYNO_USAGE_ROOT_RUNTIME_FLAG_RESCAN, &root->syno_usage_runtime_flags)) {
		ret = -EBUSY;
		spin_unlock(&root->syno_usage_lock);
		goto out;
	}

	if (usage_root_status->state == SYNO_USAGE_ROOT_STATE_NORMAL ||
		usage_root_status->num_bytes == 0 ||
		btrfs_comp_cpu_keys(&first_key, &usage_root_status->fast_rescan_progress) == 0 ||
		btrfs_comp_cpu_keys(&last_key, &usage_root_status->fast_rescan_progress) == 0) {

		usage_root_status->new_type = syno_usage_ctl_args->type;

		usage_root_status->fast_rescan_progress.objectid = 0;
		usage_root_status->fast_rescan_progress.type = 0;
		usage_root_status->fast_rescan_progress.offset = 0;
		usage_root_status->state = SYNO_USAGE_ROOT_STATE_RESCAN;
		usage_root_status->flags |= BTRFS_SYNO_USAGE_ROOT_FLAG_FAST_RESCAN;

		if (usage_root_status->new_type == SYNO_USAGE_TYPE_RO_SNAPSHOT)
			usage_root_status->flags |= BTRFS_SYNO_USAGE_ROOT_FLAG_FORCE_EXTENT;

		if (usage_root_status->new_type == SYNO_USAGE_TYPE_RO_SNAPSHOT &&
			usage_root_status->type == SYNO_USAGE_TYPE_NONE) {
			usage_root_status->fast_rescan_progress.objectid = -1;
			usage_root_status->fast_rescan_progress.type = -1;
			usage_root_status->fast_rescan_progress.offset = -1;
			usage_root_status->flags &= ~BTRFS_SYNO_USAGE_ROOT_FLAG_FAST_RESCAN;
		}
		if (usage_root_status->num_bytes == 0) {
			usage_root_status->fast_rescan_progress.objectid = -1;
			usage_root_status->fast_rescan_progress.type = -1;
			usage_root_status->fast_rescan_progress.offset = -1;
			usage_root_status->flags &= ~BTRFS_SYNO_USAGE_ROOT_FLAG_FAST_RESCAN;
		}
		if (btrfs_comp_cpu_keys(&last_key, &usage_root_status->fast_rescan_progress) == 0 &&
			btrfs_comp_cpu_keys(&last_key, &usage_root_status->full_rescan_progress) == 0) {
			if (usage_root_status->flags & BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_PROGRESS_ACCOUNTING) {
				spin_lock(&fs_info->syno_usage_lock);
				fs_info->syno_usage_status.cur_full_rescan_size += usage_root_status->total_full_rescan_size - usage_root_status->cur_full_rescan_size;
				usage_root_status->cur_full_rescan_size = 0;
				usage_root_status->total_full_rescan_size = 0;
				spin_unlock(&fs_info->syno_usage_lock);
			}
			usage_root_status->type = usage_root_status->new_type;
			usage_root_status->state = SYNO_USAGE_ROOT_STATE_NORMAL;
			usage_root_status->flags &= ~(BTRFS_SYNO_USAGE_ROOT_FLAG_RESCAN_MASK);
		}
		if (usage_root_status->state == SYNO_USAGE_ROOT_STATE_RESCAN &&
		    usage_root_status->new_type != SYNO_USAGE_TYPE_RO_SNAPSHOT) {
			if (fs_info->syno_usage_status.state >= SYNO_USAGE_STATE_INITIAL &&
			    fs_info->syno_usage_status.state <= SYNO_USAGE_STATE_RESCAN_PAUSE &&
			    usage_root_status->flags & BTRFS_SYNO_USAGE_ROOT_FLAG_FULL_RESCAN)
				__btrfs_syno_usage_rescan_progress_accounting(root);
			spin_lock(&fs_info->syno_usage_full_rescan_lock);
			spin_lock(&fs_info->syno_usage_fast_rescan_lock);
			if ((fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN ||
			     fs_info->syno_usage_status.state == SYNO_USAGE_STATE_ENABLE) &&
			    !test_bit(SYNO_USAGE_ROOT_RUNTIME_FLAG_RESCAN, &root->syno_usage_runtime_flags) &&
			    list_empty(&root->syno_usage_rescan_list)) {
				btrfs_grab_root(root);
				if (usage_root_status->flags & BTRFS_SYNO_USAGE_ROOT_FLAG_FULL_RESCAN) {
					list_move_tail(&root->syno_usage_rescan_list, &fs_info->syno_usage_pending_full_rescan_roots);
					atomic_inc(&fs_info->syno_usage_pending_full_rescan_count);
				} else {
					list_move_tail(&root->syno_usage_rescan_list, &fs_info->syno_usage_pending_fast_rescan_roots);
					atomic_inc(&fs_info->syno_usage_pending_fast_rescan_count);
				}
				resume = true;
			}
			spin_unlock(&fs_info->syno_usage_fast_rescan_lock);
			spin_unlock(&fs_info->syno_usage_full_rescan_lock);
		}
	} else {
		ret = -EBUSY;
		spin_unlock(&root->syno_usage_lock);
		goto out;
	}
	spin_unlock(&root->syno_usage_lock);

	btrfs_record_root_in_trans(trans, root);
	if (resume)
		btrfs_syno_usage_rescan_resume(fs_info);
	ret = 0;
out:
	if (trans)
		btrfs_end_transaction(trans);
	return ret;
}

static int btrfs_ioctl_syno_usage_get_by_type(struct file *file,
					struct btrfs_ioctl_syno_usage_ctl_args *syno_usage_ctl_args,
					void __user *argp)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret = 0;

	if (syno_usage_ctl_args->type >= SYNO_USAGE_TYPE_MAX ||
		syno_usage_ctl_args->type == SYNO_USAGE_TYPE_NONE) {
		ret = -EINVAL;
		goto out;
	}

	if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
		goto out;

	spin_lock(&fs_info->syno_usage_lock);
	syno_usage_ctl_args->num_bytes = fs_info->syno_usage_status.syno_usage_type_num_bytes[syno_usage_ctl_args->type];
	spin_unlock(&fs_info->syno_usage_lock);

	if (copy_to_user(argp, syno_usage_ctl_args, sizeof(*syno_usage_ctl_args))) {
		ret = -EFAULT;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

static int btrfs_ioctl_syno_usage_ctl(struct file *file, void __user *argp)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ioctl_syno_usage_ctl_args syno_usage_ctl_args;
	int ret = 0;
	struct btrfs_trans_handle *trans;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&syno_usage_ctl_args, argp, sizeof(syno_usage_ctl_args)))
		return -EFAULT;

	switch (syno_usage_ctl_args.cmd) {
	case BTRFS_SYNO_USAGE_CTL_ENABLE:
		if(!mutex_trylock(&fs_info->syno_usage_ioctl_lock)) {
			ret = -EBUSY;
			goto out;
		}
		ret = btrfs_syno_usage_enable(fs_info);
		mutex_unlock(&fs_info->syno_usage_ioctl_lock);
		break;
	case BTRFS_SYNO_USAGE_CTL_DISABLE:
		if(!mutex_trylock(&fs_info->syno_usage_ioctl_lock)) {
			ret = -EBUSY;
			goto out;
		}
		ret = btrfs_syno_usage_disable(fs_info);
		mutex_unlock(&fs_info->syno_usage_ioctl_lock);
		break;
	case BTRFS_SYNO_USAGE_CTL_STATUS:
		syno_usage_ctl_args.state = fs_info->syno_usage_status.state;
		syno_usage_ctl_args.flags = fs_info->syno_usage_status.flags;
		syno_usage_ctl_args.pending_fast_rescan_count = atomic_read(&fs_info->syno_usage_pending_fast_rescan_count);
		syno_usage_ctl_args.pending_full_rescan_count = atomic_read(&fs_info->syno_usage_pending_full_rescan_count);
		syno_usage_ctl_args.fast_rescan_pid = fs_info->syno_usage_fast_rescan_pid;
		syno_usage_ctl_args.full_rescan_pid = fs_info->syno_usage_full_rescan_pid;
		if (fs_info->syno_usage_status.state == SYNO_USAGE_STATE_INITIAL ||
			fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN ||
			fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE ||
			fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_ERROR ||
			fs_info->syno_usage_status.state == SYNO_USAGE_STATE_DISABLE) {
			syno_usage_ctl_args.cur_rescan_size = fs_info->syno_usage_status.cur_full_rescan_size;
			syno_usage_ctl_args.total_rescan_size = fs_info->syno_usage_status.total_full_rescan_size;
		}
		if (fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_ERROR)
			syno_usage_ctl_args.error_code = fs_info->syno_usage_status.error_code;
		if (copy_to_user(argp, &syno_usage_ctl_args, sizeof(syno_usage_ctl_args))) {
			ret = -EFAULT;
			goto out;
		}
		break;
	case BTRFS_SYNO_USAGE_CTL_RESCAN:
		if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags))
			goto out;
		if(!mutex_trylock(&fs_info->syno_usage_ioctl_lock)) {
			ret = -EBUSY;
			goto out;
		}
		if (fs_info->syno_usage_status.state == SYNO_USAGE_STATE_INITIAL ||
			fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_ERROR ||
			fs_info->syno_usage_status.state == SYNO_USAGE_STATE_RESCAN_PAUSE) {
			trans = btrfs_start_transaction(root, 0);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				mutex_unlock(&fs_info->syno_usage_ioctl_lock);
				goto out;
			}
			fs_info->syno_usage_status.state = SYNO_USAGE_STATE_RESCAN;
			btrfs_end_transaction(trans);
		}
		btrfs_syno_usage_rescan_resume(fs_info);
		mutex_unlock(&fs_info->syno_usage_ioctl_lock);
		break;
	case BTRFS_SYNO_USAGE_CTL_RESCAN_PAUSE:
		if (!test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) ||
		    fs_info->syno_usage_status.state != SYNO_USAGE_STATE_RESCAN)
			goto out;
		if(!mutex_trylock(&fs_info->syno_usage_ioctl_lock)) {
			ret = -EBUSY;
			goto out;
		}
		trans = btrfs_start_transaction(root, 0);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			mutex_unlock(&fs_info->syno_usage_ioctl_lock);
			goto out;
		}
		fs_info->syno_usage_status.state = SYNO_USAGE_STATE_RESCAN_PAUSE;
		clear_bit(BTRFS_FS_SYNO_SPACE_USAGE_RESCAN_CHECK_ALL, &fs_info->flags);
		btrfs_end_transaction(trans);
		mutex_unlock(&fs_info->syno_usage_ioctl_lock);
		break;
	case BTRFS_SYNO_USAGE_CTL_SUBVOL_TYPE_SET:
		ret = btrfs_ioctl_syno_usage_subvol_type_set(file, &syno_usage_ctl_args, argp);
		break;
	case BTRFS_SYNO_USAGE_CTL_SUBVOL_TYPE_GET:
		if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID) {
			ret = -EINVAL;
			goto out;
		}
		if (test_bit(BTRFS_FS_SYNO_SPACE_USAGE_ENABLED, &fs_info->flags) &&
		    test_bit(BTRFS_ROOT_SYNO_SPACE_USAGE_ENABLED, &root->state))
			syno_usage_ctl_args.type = root->syno_usage_root_status.new_type;
		else
			syno_usage_ctl_args.type = SYNO_USAGE_TYPE_NONE;
		if (copy_to_user(argp, &syno_usage_ctl_args, sizeof(syno_usage_ctl_args))) {
			ret = -EFAULT;
			goto out;
		}
		break;
	case BTRFS_SYNO_USAGE_CTL_USAGE_GET_BY_TYPE:
		ret = btrfs_ioctl_syno_usage_get_by_type(file, &syno_usage_ctl_args, argp);
		break;
	default:
		ret = -EINVAL;
		break;
	}
out:
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
static int btrfs_ioctl_cksumfailed_files_get(struct file *file, void __user *arg)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(file_inode(file))->root->fs_info;
	struct cksumfailed_file_rec rec;
	struct btrfs_ioctl_cksumfailed_files_args cksumfailed_files;
	unsigned int len;

	spin_lock(&fs_info->cksumfailed_files_write_lock);
	len = kfifo_out(&fs_info->cksumfailed_files, &rec,
			sizeof(struct cksumfailed_file_rec));
	spin_unlock(&fs_info->cksumfailed_files_write_lock);
	if (len == sizeof(struct cksumfailed_file_rec)) {
		cksumfailed_files.sub_vol = rec.sub_vol;
		cksumfailed_files.ino = rec.ino;
	} else if (0 == len) {
		return -ENOENT;
	} else {
		return -EFAULT;
	}

	if (copy_to_user(arg, &cksumfailed_files,
			 sizeof(struct btrfs_ioctl_cksumfailed_files_args)))
		return -EFAULT;

	return 0;
}
#endif /* MY_ABC_HERE */

#ifdef MY_DEF_HERE
static int btrfs_dedupe_set_inode_no_dedupe(struct inode *inode, bool on_off)
{
	int ret = -1;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans = NULL;

	inode_lock(inode);
	if (on_off) {
		if (BTRFS_I(inode)->flags & BTRFS_INODE_NODEDUPE)
			goto out;
		BTRFS_I(inode)->flags |= BTRFS_INODE_NODEDUPE;
	} else {
		if (!(BTRFS_I(inode)->flags & BTRFS_INODE_NODEDUPE))
			goto out;
		BTRFS_I(inode)->flags &= ~BTRFS_INODE_NODEDUPE;
	}

	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}
	inode_inc_iversion(inode);
	inode->i_ctime = current_time(inode);

	ret = btrfs_update_inode(trans, root, inode);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		goto out;
	}
	ret = btrfs_end_transaction(trans);

out:
	inode_unlock(inode);
	return ret;
}

static long _btrfs_ioctl_syno_dedupe_cmd_file(struct file *file,
			struct btrfs_ioctl_syno_dedupe_cmd_args *dedupe_cmd_args)
{
	int ret = -1;
	u64 objectid = dedupe_cmd_args->objectid;
	struct inode *inode = NULL;
	bool get_inode = false;

	if (!objectid) {
		inode = file_inode(file);
	} else {
		inode = btrfs_get_regular_file_inode(file_inode(file)->i_sb,
						     dedupe_cmd_args->rootid,
						     objectid);
		if (IS_ERR(inode)) {
			ret = PTR_ERR(inode);
			goto out;
		}
		get_inode = true;
	}

	if (!inode_owner_or_capable(inode)) {
		ret = -EPERM;
		goto out;
	}

	switch(dedupe_cmd_args->action) {
	case DEDUPE_CMD_SET:
		ret = btrfs_file_extent_deduped_set_range(inode,
				dedupe_cmd_args->offset, dedupe_cmd_args->len, true);
		break;
	case DEDUPE_CMD_CLEAR:
		ret = btrfs_file_extent_deduped_set_range(inode,
				dedupe_cmd_args->offset, dedupe_cmd_args->len, false);
		break;
	case DEDUPE_CMD_SET_NODEDUPE:
		ret = btrfs_dedupe_set_inode_no_dedupe(inode, true);
		break;
	case DEDUPE_CMD_CLEAR_NODEDUPE:
		ret = btrfs_dedupe_set_inode_no_dedupe(inode, false);
		break;
	default:
		break;
	}

out:
	if (get_inode)
		iput(inode);

	return ret;
}

static long _btrfs_ioctl_syno_dedupe_cmd_root(struct file *file,
			struct btrfs_ioctl_syno_dedupe_cmd_args *dedupe_cmd_args)
{
	int ret = -1;
	u64 objectid = dedupe_cmd_args->objectid;
	u64 len = dedupe_cmd_args->len;
	struct btrfs_root *root = NULL;
	bool hold_root = false;

	if (!objectid) {
		root = BTRFS_I(file_inode(file))->root;
	} else {
		if (objectid < BTRFS_FIRST_FREE_OBJECTID || objectid > BTRFS_LAST_FREE_OBJECTID)
			return -ESTALE;

		root = btrfs_get_fs_root(btrfs_sb(file_inode(file)->i_sb), objectid, true);
		if (IS_ERR(root))
			return PTR_ERR(root);

		hold_root = true;
	}

	switch(dedupe_cmd_args->action) {
	case DEDUPE_CMD_SET_SMALL_EXTENT_SIZE:
		if ((len % PAGE_SIZE) || len < SZ_128K) {
			ret = -EINVAL;
			goto out;
		}
		root->small_extent_size = len;
		break;
	case DEDUPE_CMD_SET_INLINE_DEDUPE:
		root->inline_dedupe = true;
		break;
	case DEDUPE_CMD_CLEAR_INLINE_DEDUPE:
		root->inline_dedupe = false;
		break;
	default:
		break;
	}

	ret = 0;

out:
	if (hold_root)
		btrfs_put_root(root);
	return ret;
}

static long btrfs_ioctl_syno_dedupe_cmd(struct file *file,
			struct btrfs_ioctl_syno_dedupe_cmd_args __user *argp)
{
	int ret = -1;
	struct btrfs_ioctl_syno_dedupe_cmd_args *dedupe_cmd_args = NULL;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	dedupe_cmd_args = memdup_user(argp, sizeof(struct btrfs_ioctl_syno_dedupe_cmd_args));
	if (!dedupe_cmd_args) {
		ret = -ENOMEM;
		goto out;
	}

	switch(dedupe_cmd_args->action) {
	case DEDUPE_CMD_SET:
	case DEDUPE_CMD_CLEAR:
	case DEDUPE_CMD_SET_NODEDUPE:
	case DEDUPE_CMD_CLEAR_NODEDUPE:
		ret = _btrfs_ioctl_syno_dedupe_cmd_file(file, dedupe_cmd_args);
		break;
	case DEDUPE_CMD_SET_SMALL_EXTENT_SIZE:
	case DEDUPE_CMD_SET_INLINE_DEDUPE:
	case DEDUPE_CMD_CLEAR_INLINE_DEDUPE:
		ret = _btrfs_ioctl_syno_dedupe_cmd_root(file, dedupe_cmd_args);
		break;
	default:
		printk("unknown dedupe cmd:%d\n", dedupe_cmd_args->action);
		ret = -EINVAL;
	}

out:
	mnt_drop_write_file(file);
	kfree(dedupe_cmd_args);

	return ret;
}
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE
static int btrfs_ioctl_syno_feat_tree_ctl(struct file *file, struct btrfs_ioctl_syno_feat_tree_ctl_args __user *argp)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ioctl_syno_feat_tree_ctl_args syno_feat_ctl_args;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&syno_feat_ctl_args, argp, sizeof(syno_feat_ctl_args)))
		return -EFAULT;

	switch (syno_feat_ctl_args.cmd) {

		case BTRFS_SYNO_FEAT_TREE_CTL_ENABLE:
			if(!mutex_trylock(&fs_info->syno_feat_tree_ioctl_lock)) {
				ret = -EBUSY;
				goto out;
			}
			ret = btrfs_syno_feat_tree_enable(fs_info);
			if (!ret)
				btrfs_info(root->fs_info, "syno feature tree is enabled");
			mutex_unlock(&fs_info->syno_feat_tree_ioctl_lock);
			break;
		case BTRFS_SYNO_FEAT_TREE_CTL_DISABLE:
#ifdef MY_ABC_HERE
			/* feature-tree isn't able to be disabled */
			ret = -EPERM;
			break;
#else
			if(!mutex_trylock(&fs_info->syno_feat_tree_ioctl_lock)) {
				ret = -EBUSY;
				goto out;
			}
			ret = btrfs_syno_feat_tree_disable(fs_info);
			mutex_unlock(&fs_info->syno_feat_tree_ioctl_lock);
			break;
#endif /* MY_ABC_HERE */
		case BTRFS_SYNO_FEAT_TREE_CTL_STATUS:
			syno_feat_ctl_args.status = fs_info->syno_feat_tree_status.status;

			if (copy_to_user(argp, &syno_feat_ctl_args, sizeof(syno_feat_ctl_args))) {
				ret = -EFAULT;
				goto out;
			}
			break;
		default:
			ret = -EINVAL;
			break;
	}
out:
	return ret;
}
#endif /* MY_ABC_HERE */

long btrfs_ioctl(struct file *file, unsigned int
		cmd, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		return btrfs_ioctl_getflags(file, argp);
	case FS_IOC_SETFLAGS:
		return btrfs_ioctl_setflags(file, argp);
	case FS_IOC_GETVERSION:
		return btrfs_ioctl_getversion(file, argp);
	case FS_IOC_GETFSLABEL:
		return btrfs_ioctl_get_fslabel(fs_info, argp);
	case FS_IOC_SETFSLABEL:
		return btrfs_ioctl_set_fslabel(file, argp);
	case FITRIM:
		return btrfs_ioctl_fitrim(fs_info, argp);
#ifdef MY_ABC_HERE
	case FIHINTUNUSED:
		return btrfs_ioctl_hint_unused(file, argp);
#endif /* MY_ABC_HERE */
	case BTRFS_IOC_SNAP_CREATE:
		return btrfs_ioctl_snap_create(file, argp, 0);
	case BTRFS_IOC_SNAP_CREATE_V2:
		return btrfs_ioctl_snap_create_v2(file, argp, 0);
	case BTRFS_IOC_SUBVOL_CREATE:
		return btrfs_ioctl_snap_create(file, argp, 1);
	case BTRFS_IOC_SUBVOL_CREATE_V2:
		return btrfs_ioctl_snap_create_v2(file, argp, 1);
	case BTRFS_IOC_SNAP_DESTROY:
		return btrfs_ioctl_snap_destroy(file, argp, false);
	case BTRFS_IOC_SNAP_DESTROY_V2:
		return btrfs_ioctl_snap_destroy(file, argp, true);
	case BTRFS_IOC_SUBVOL_GETFLAGS:
		return btrfs_ioctl_subvol_getflags(file, argp);
	case BTRFS_IOC_SUBVOL_SETFLAGS:
		return btrfs_ioctl_subvol_setflags(file, argp);
	case BTRFS_IOC_DEFAULT_SUBVOL:
		return btrfs_ioctl_default_subvol(file, argp);
	case BTRFS_IOC_DEFRAG:
		return btrfs_ioctl_defrag(file, NULL);
	case BTRFS_IOC_DEFRAG_RANGE:
		return btrfs_ioctl_defrag(file, argp);
	case BTRFS_IOC_RESIZE:
		return btrfs_ioctl_resize(file, argp);
	case BTRFS_IOC_ADD_DEV:
		return btrfs_ioctl_add_dev(fs_info, argp);
	case BTRFS_IOC_RM_DEV:
		return btrfs_ioctl_rm_dev(file, argp);
	case BTRFS_IOC_RM_DEV_V2:
		return btrfs_ioctl_rm_dev_v2(file, argp);
	case BTRFS_IOC_FS_INFO:
		return btrfs_ioctl_fs_info(fs_info, argp);
	case BTRFS_IOC_DEV_INFO:
		return btrfs_ioctl_dev_info(fs_info, argp);
	case BTRFS_IOC_BALANCE:
		return btrfs_ioctl_balance(file, NULL);
	case BTRFS_IOC_TREE_SEARCH:
		return btrfs_ioctl_tree_search(file, argp);
	case BTRFS_IOC_TREE_SEARCH_V2:
		return btrfs_ioctl_tree_search_v2(file, argp);
	case BTRFS_IOC_INO_LOOKUP:
		return btrfs_ioctl_ino_lookup(file, argp);
	case BTRFS_IOC_INO_PATHS:
		return btrfs_ioctl_ino_to_path(root, argp);
	case BTRFS_IOC_LOGICAL_INO:
		return btrfs_ioctl_logical_to_ino(fs_info, argp, 1);
	case BTRFS_IOC_LOGICAL_INO_V2:
		return btrfs_ioctl_logical_to_ino(fs_info, argp, 2);
	case BTRFS_IOC_SPACE_INFO:
		return btrfs_ioctl_space_info(fs_info, argp);
#ifdef MY_ABC_HERE
	case BTRFS_IOC_SYNC_SYNO: {
		int ret;

		ret = btrfs_ioctl_trigger_transcation(inode->i_sb);
		wake_up_process(fs_info->transaction_kthread);
		return ret;
	}
#endif /* MY_ABC_HERE */
	case BTRFS_IOC_SYNC: {
		int ret;

		ret = btrfs_start_delalloc_roots(fs_info, U64_MAX, false);
		if (ret)
			return ret;
		ret = btrfs_sync_fs(inode->i_sb, 1);
		/*
		 * The transaction thread may want to do more work,
		 * namely it pokes the cleaner kthread that will start
		 * processing uncleaned subvols.
		 */
		wake_up_process(fs_info->transaction_kthread);
		return ret;
	}
	case BTRFS_IOC_START_SYNC:
		return btrfs_ioctl_start_sync(root, argp);
	case BTRFS_IOC_WAIT_SYNC:
		return btrfs_ioctl_wait_sync(fs_info, argp);
	case BTRFS_IOC_SCRUB:
		return btrfs_ioctl_scrub(file, argp);
	case BTRFS_IOC_SCRUB_CANCEL:
		return btrfs_ioctl_scrub_cancel(fs_info);
	case BTRFS_IOC_SCRUB_PROGRESS:
		return btrfs_ioctl_scrub_progress(fs_info, argp);
	case BTRFS_IOC_BALANCE_V2:
		return btrfs_ioctl_balance(file, argp);
	case BTRFS_IOC_BALANCE_CTL:
		return btrfs_ioctl_balance_ctl(fs_info, arg);
	case BTRFS_IOC_BALANCE_PROGRESS:
		return btrfs_ioctl_balance_progress(fs_info, argp);
	case BTRFS_IOC_SET_RECEIVED_SUBVOL:
		return btrfs_ioctl_set_received_subvol(file, argp);
#ifdef CONFIG_64BIT
	case BTRFS_IOC_SET_RECEIVED_SUBVOL_32:
		return btrfs_ioctl_set_received_subvol_32(file, argp);
#endif
	case BTRFS_IOC_SEND:
		return _btrfs_ioctl_send(file, argp, false);
#if defined(CONFIG_64BIT) && defined(CONFIG_COMPAT)
	case BTRFS_IOC_SEND_32:
		return _btrfs_ioctl_send(file, argp, true);
#endif
	case BTRFS_IOC_GET_DEV_STATS:
		return btrfs_ioctl_get_dev_stats(fs_info, argp);
	case BTRFS_IOC_QUOTA_CTL:
		return btrfs_ioctl_quota_ctl(file, argp);
	case BTRFS_IOC_QGROUP_ASSIGN:
		return btrfs_ioctl_qgroup_assign(file, argp);
	case BTRFS_IOC_QGROUP_CREATE:
		return btrfs_ioctl_qgroup_create(file, argp);
	case BTRFS_IOC_QGROUP_LIMIT:
		return btrfs_ioctl_qgroup_limit(file, argp);
	case BTRFS_IOC_QUOTA_RESCAN:
		return btrfs_ioctl_quota_rescan(file, argp);
	case BTRFS_IOC_QUOTA_RESCAN_STATUS:
		return btrfs_ioctl_quota_rescan_status(fs_info, argp);
	case BTRFS_IOC_QUOTA_RESCAN_WAIT:
		return btrfs_ioctl_quota_rescan_wait(fs_info, argp);
#ifdef MY_ABC_HERE
	case BTRFS_IOC_USRQUOTA_CTL:
		return btrfs_ioctl_usrquota_ctl(file, argp);
	case BTRFS_IOC_USRQUOTA_LIMIT:
		return btrfs_ioctl_usrquota_limit(file, argp);
	case BTRFS_IOC_USRQUOTA_RESCAN:
		return btrfs_ioctl_usrquota_rescan(file);
	case BTRFS_IOC_USRQUOTA_RESCAN_STATUS:
		return btrfs_ioctl_usrquota_rescan_status(file, argp);
	case BTRFS_IOC_USRQUOTA_RESCAN_WAIT:
		return btrfs_ioctl_usrquota_rescan_wait(file);
	case BTRFS_IOC_USRQUOTA_QUERY:
		return btrfs_ioctl_usrquota_query(file, argp);
	case BTRFS_IOC_USRQUOTA_CLEAN:
		return btrfs_ioctl_usrquota_clean(file, argp);
	case BTRFS_IOC_SYNO_QUOTA_RESCAN:
		return btrfs_ioctl_syno_quota_rescan(file, argp);
	case BTRFS_IOC_SYNO_QUOTA_STATUS:
		return btrfs_ioctl_syno_quota_status(file, argp);
#endif /* MY_ABC_HERE */
	case BTRFS_IOC_DEV_REPLACE:
		return btrfs_ioctl_dev_replace(fs_info, argp);
	case BTRFS_IOC_GET_SUPPORTED_FEATURES:
		return btrfs_ioctl_get_supported_features(argp);
	case BTRFS_IOC_GET_FEATURES:
		return btrfs_ioctl_get_features(fs_info, argp);
	case BTRFS_IOC_SET_FEATURES:
		return btrfs_ioctl_set_features(file, argp);
	case FS_IOC_FSGETXATTR:
		return btrfs_ioctl_fsgetxattr(file, argp);
	case FS_IOC_FSSETXATTR:
		return btrfs_ioctl_fssetxattr(file, argp);
	case BTRFS_IOC_GET_SUBVOL_INFO:
		return btrfs_ioctl_get_subvol_info(file, argp);
	case BTRFS_IOC_GET_SUBVOL_ROOTREF:
		return btrfs_ioctl_get_subvol_rootref(file, argp);
	case BTRFS_IOC_INO_LOOKUP_USER:
		return btrfs_ioctl_ino_lookup_user(file, argp);
#ifdef MY_ABC_HERE
	case BTRFS_IOC_FIND_NEXT_CHUNK_INFO:
		return btrfs_ioctl_syno_find_next_chunk_info(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_SYNO_RESERVE_LOG_TREE_BLOCK_GROUP:
		return btrfs_ioctl_syno_reserve_log_tree_bg(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_QGROUP_QUERY:
		return btrfs_ioctl_qgroup_query(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_SYNO_CLONE_RANGE_V2:
		return btrfs_ioctl_syno_clone_range_v2(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_FREE_SPACE_ANALYZE:
		return btrfs_ioctl_free_space_analyze(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_COMPR_CTL:
		return btrfs_ioctl_compr_ctl(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_SNAPSHOT_SIZE_QUERY:
		return btrfs_ioctl_snapshot_size_query(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_SYNO_USAGE_CTL:
		return btrfs_ioctl_syno_usage_ctl(file, argp);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	case BTRFS_IOC_CKSUMFAILED_FILES_GET:
		return btrfs_ioctl_cksumfailed_files_get(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_DEF_HERE
	case BTRFS_IOC_SYNO_SET_DEDUPE_FLAG:
		return btrfs_ioctl_syno_dedupe_cmd(file, argp);
	case BTRFS_IOC_SYNO_EXTENT_SAME:
		return btrfs_ioctl_syno_extent_same(file, argp);
#endif /* MY_DEF_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_SYNO_FEAT_TREE_CTL:
		return btrfs_ioctl_syno_feat_tree_ctl(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_SYNO_LOCKER_GET:
		return btrfs_ioctl_syno_locker_get(file, argp);
	case BTRFS_IOC_SYNO_LOCKER_SET:
		return btrfs_ioctl_syno_locker_set(file, argp);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	case BTRFS_IOC_GET_SYNO_FLAGS:
		return btrfs_ioctl_get_syno_flags(file, argp);
	case BTRFS_IOC_SET_SYNO_FLAGS:
		return btrfs_ioctl_set_syno_flags(file, argp);
#endif /* MY_ABC_HERE */
	}

	return -ENOTTY;
}

#ifdef CONFIG_COMPAT
long btrfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/*
	 * These all access 32-bit values anyway so no further
	 * handling is necessary.
	 */
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	case FS_IOC32_GETVERSION:
		cmd = FS_IOC_GETVERSION;
		break;
	}

	return btrfs_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif
