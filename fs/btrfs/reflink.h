#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_REFLINK_H
#define BTRFS_REFLINK_H

#include <linux/fs.h>

loff_t btrfs_remap_file_range(struct file *file_in, loff_t pos_in,
			      struct file *file_out, loff_t pos_out,
			      loff_t len, unsigned int remap_flags);

#ifdef MY_ABC_HERE
int btrfs_ioctl_syno_clone_range_v2(struct file *dst_file,
		struct btrfs_ioctl_syno_clone_range_args_v2 __user *argp);
#endif /* MY_ABC_HERE */

#ifdef MY_DEF_HERE
long btrfs_ioctl_syno_extent_same(struct file *file,
		struct btrfs_ioctl_syno_extent_same_args __user *argp);
#endif /* MY_DEF_HERE */

#endif /* BTRFS_REFLINK_H */
