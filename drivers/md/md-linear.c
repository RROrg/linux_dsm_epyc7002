#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
/*
   linear.c : Multiple Devices driver for Linux
	      Copyright (C) 1994-96 Marc ZYNGIER
	      <zyngier@ufr-info-p7.ibp.fr> or
	      <maz@gloups.fdn.fr>

   Linear mode management functions.

*/

#include <linux/blkdev.h>
#include <linux/raid/md_u.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <trace/events/block.h>
#include "md.h"
#include "md-linear.h"

/*
 * find which device holds a particular offset
 */
#ifdef MY_ABC_HERE
static inline struct dev_info *which_dev(struct mddev *mddev, sector_t sector, bool take_rcu)
#else /* MY_ABC_HERE */
static inline struct dev_info *which_dev(struct mddev *mddev, sector_t sector)
#endif /* MY_ABC_HERE */
{
	int lo, mid, hi;
	struct linear_conf *conf;

	lo = 0;
	hi = mddev->raid_disks - 1;
#ifdef MY_ABC_HERE
	if (take_rcu)
		conf = rcu_dereference(mddev->private);
	else
		conf = mddev->private;
#else /* MY_ABC_HERE */
	conf = mddev->private;
#endif /* MY_ABC_HERE */

	/*
	 * Binary Search
	 */

	while (hi > lo) {

		mid = (hi + lo) / 2;
		if (sector < conf->disks[mid].end_sector)
			hi = mid;
		else
			lo = mid + 1;
	}

	return conf->disks + lo;
}

static sector_t linear_size(struct mddev *mddev, sector_t sectors, int raid_disks)
{
	struct linear_conf *conf;
	sector_t array_sectors;

	conf = mddev->private;
	WARN_ONCE(sectors || raid_disks,
		  "%s does not support generic reshape\n", __func__);
	array_sectors = conf->array_sectors;

	return array_sectors;
}

static struct linear_conf *linear_conf(struct mddev *mddev, int raid_disks)
{
	struct linear_conf *conf;
	struct md_rdev *rdev;
	int i, cnt;
	bool discard_supported = false;

	conf = kzalloc(struct_size(conf, disks, raid_disks), GFP_KERNEL);
	if (!conf)
		return NULL;

	cnt = 0;
	conf->array_sectors = 0;

	rdev_for_each(rdev, mddev) {
		int j = rdev->raid_disk;
		struct dev_info *disk = conf->disks + j;
		sector_t sectors;

		if (j < 0 || j >= raid_disks || disk->rdev) {
			pr_warn("md/linear:%s: disk numbering problem. Aborting!\n",
				mdname(mddev));
			goto out;
		}

		disk->rdev = rdev;
		if (mddev->chunk_sectors) {
			sectors = rdev->sectors;
			sector_div(sectors, mddev->chunk_sectors);
			rdev->sectors = sectors * mddev->chunk_sectors;
		}

		disk_stack_limits(mddev->gendisk, rdev->bdev,
				  rdev->data_offset << 9);

		conf->array_sectors += rdev->sectors;
		cnt++;

		if (blk_queue_discard(bdev_get_queue(rdev->bdev)))
			discard_supported = true;
	}
	if (cnt != raid_disks) {
#ifdef MY_ABC_HERE
		if (mddev->syno_nodev_and_crashed != MD_CRASHED_ASSEMBLE)
			mddev->syno_nodev_and_crashed = MD_CRASHED;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
		/*
		 * for Linear status consistense to other raid type
		 * Let it can assemble.
		 */
		mddev->degraded = mddev->raid_disks - cnt;
		pr_warn("md/linear:%s: not enough drives present.\n",
			mdname(mddev));
		return conf;
#else /* MY_ABC_HERE */
		pr_warn("md/linear:%s: not enough drives present. Aborting!\n",
			mdname(mddev));
		goto out;
#endif /* MY_ABC_HERE */
	}

	if (!discard_supported)
		blk_queue_flag_clear(QUEUE_FLAG_DISCARD, mddev->queue);
	else
		blk_queue_flag_set(QUEUE_FLAG_DISCARD, mddev->queue);

	/*
	 * Here we calculate the device offsets.
	 */
	conf->disks[0].end_sector = conf->disks[0].rdev->sectors;

	for (i = 1; i < raid_disks; i++)
		conf->disks[i].end_sector =
			conf->disks[i-1].end_sector +
			conf->disks[i].rdev->sectors;

	/*
	 * conf->raid_disks is copy of mddev->raid_disks. The reason to
	 * keep a copy of mddev->raid_disks in struct linear_conf is,
	 * mddev->raid_disks may not be consistent with pointers number of
	 * conf->disks[] when it is updated in linear_add() and used to
	 * iterate old conf->disks[] earray in linear_congested().
	 * Here conf->raid_disks is always consitent with number of
	 * pointers in conf->disks[] array, and mddev->private is updated
	 * with rcu_assign_pointer() in linear_addr(), such race can be
	 * avoided.
	 */
	conf->raid_disks = raid_disks;

	return conf;

out:
	kfree(conf);
	return NULL;
}

static int linear_run (struct mddev *mddev)
{
	struct linear_conf *conf;
	int ret;

	if (md_check_no_bitmap(mddev))
		return -EINVAL;
#ifdef MY_ABC_HERE
	mddev->degraded = 0;
#endif /* MY_ABC_HERE */
	conf = linear_conf(mddev, mddev->raid_disks);

	if (!conf)
		return 1;
	mddev->private = conf;
	md_set_array_sectors(mddev, linear_size(mddev, 0, 0));

	ret =  md_integrity_register(mddev);
	if (ret) {
		kfree(conf);
		mddev->private = NULL;
	}
	return ret;
}

static int linear_add(struct mddev *mddev, struct md_rdev *rdev)
{
	/* Adding a drive to a linear array allows the array to grow.
	 * It is permitted if the new drive has a matching superblock
	 * already on it, with raid_disk equal to raid_disks.
	 * It is achieved by creating a new linear_private_data structure
	 * and swapping it in in-place of the current one.
	 * The current one is never freed until the array is stopped.
	 * This avoids races.
	 */
	struct linear_conf *newconf, *oldconf;

	if (rdev->saved_raid_disk != mddev->raid_disks)
		return -EINVAL;

	rdev->raid_disk = rdev->saved_raid_disk;
	rdev->saved_raid_disk = -1;

	newconf = linear_conf(mddev,mddev->raid_disks+1);

	if (!newconf)
		return -ENOMEM;

	/* newconf->raid_disks already keeps a copy of * the increased
	 * value of mddev->raid_disks, WARN_ONCE() is just used to make
	 * sure of this. It is possible that oldconf is still referenced
	 * in linear_congested(), therefore kfree_rcu() is used to free
	 * oldconf until no one uses it anymore.
	 */
	mddev_suspend(mddev);
	oldconf = rcu_dereference_protected(mddev->private,
			lockdep_is_held(&mddev->reconfig_mutex));
	mddev->raid_disks++;
	WARN_ONCE(mddev->raid_disks != newconf->raid_disks,
		"copied raid_disks doesn't match mddev->raid_disks");
	rcu_assign_pointer(mddev->private, newconf);
	md_set_array_sectors(mddev, linear_size(mddev, 0, 0));
	set_capacity(mddev->gendisk, mddev->array_sectors);
	mddev_resume(mddev);
	revalidate_disk_size(mddev->gendisk, true);
	kfree_rcu(oldconf, rcu);
	return 0;
}

static void linear_free(struct mddev *mddev, void *priv)
{
	struct linear_conf *conf = priv;

	kfree(conf);
}

#ifdef MY_ABC_HERE
/**
 * This is end_io callback function.
 * We can use this for bad sector report and device error
 * handing. Prevent umount panic from file system
 *
 * @author \$Author: khchen $
 * @version \$Revision: 1.1
 *
 * @param bio    Should not be NULL. Passing from block layer
 * @param error  error number
 */
static void
syno_linear_end_request(struct bio *bio)
{
	struct mddev *mddev = NULL;
	struct md_rdev *rdev = NULL;
	struct bio *orig_bio;

	orig_bio = bio->bi_private;

	rdev = (struct md_rdev *)orig_bio->bi_next;
	mddev = rdev->mddev;
	orig_bio->bi_next = bio->bi_next;
	orig_bio->bi_status = bio->bi_status;

#ifdef MY_ABC_HERE
	if (bio->bi_status) {
		struct dev_info *tmp_dev;
		sector_t report_sector;

		rcu_read_lock();
		tmp_dev = which_dev(mddev, orig_bio->bi_iter.bi_sector, true);
		report_sector = orig_bio->bi_iter.bi_sector -
		                (tmp_dev->end_sector - rdev->sectors) +
		                rdev->data_offset;
		rcu_read_unlock();

		md_error(mddev, rdev);
		if (!syno_is_device_disappear(rdev->bdev)) {
			syno_report_bad_sector(report_sector, bio_data_dir(bio),
					       mddev->md_minor, rdev->bdev, __func__);
			if (bio_data_dir(bio) == READ)
				syno_report_uncorrected_bad_sector(report_sector, mddev->md_minor,
								   rdev->bdev, __func__);
		}
	}
#else /* MY_ABC_HERE */
	if (bio->bi_status)
		md_error(mddev, rdev);
#endif /* MY_ABC_HERE */

	atomic_dec(&rdev->nr_pending);
	bio_put(bio);
	/**
	 * Let mount could successful and bad sector could keep accessing,
	 * no matter it success or not
	 */
	bio_endio(orig_bio);
}
#endif /* MY_ABC_HERE */

static bool linear_make_request(struct mddev *mddev, struct bio *bio)
{
	char b[BDEVNAME_SIZE];
	struct dev_info *tmp_dev;
	sector_t start_sector, end_sector, data_offset;
	sector_t bio_sector = bio->bi_iter.bi_sector;
#ifdef MY_ABC_HERE
	struct bio *cloned_bio, *orig_bio;
#endif /* MY_ABC_HERE */

	if (unlikely(bio->bi_opf & REQ_PREFLUSH)
	    && md_flush_request(mddev, bio))
		return true;

#ifdef MY_ABC_HERE
	if (mddev->syno_nodev_and_crashed) {
		bio_io_error(bio);
		return true;
	}
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	if (mddev->degraded) {
		bio_io_error(bio);
		return true;
	}
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	tmp_dev = which_dev(mddev, bio_sector, false);
#else /* MY_ABC_HERE */
	tmp_dev = which_dev(mddev, bio_sector);
#endif /* MY_ABC_HERE */
	start_sector = tmp_dev->end_sector - tmp_dev->rdev->sectors;
	end_sector = tmp_dev->end_sector;
	data_offset = tmp_dev->rdev->data_offset;

	if (unlikely(bio_sector >= end_sector ||
		     bio_sector < start_sector))
		goto out_of_bounds;

	if (unlikely(is_mddev_broken(tmp_dev->rdev, "linear"))) {
		bio_io_error(bio);
		return true;
	}

	if (unlikely(bio_end_sector(bio) > end_sector)) {
		/* This bio crosses a device boundary, so we have to split it */
		struct bio *split = bio_split(bio, end_sector - bio_sector,
					      GFP_NOIO, &mddev->bio_set);
		bio_chain(split, bio);
#ifdef MY_ABC_HERE
		bio_set_flag(bio, BIO_SYNO_DELAYED);
#endif /* MY_ABC_HERE */
		submit_bio_noacct(bio);
		bio = split;
	}

#ifdef MY_ABC_HERE
	cloned_bio = bio_clone_fast(bio, GFP_NOIO, &mddev->bio_set);

	if (cloned_bio) {
		atomic_inc(&tmp_dev->rdev->nr_pending);
		cloned_bio->bi_end_io = syno_linear_end_request;
		cloned_bio->bi_private = bio;

		orig_bio = bio;
		orig_bio->bi_next = (void *)tmp_dev->rdev;
		bio = cloned_bio;
	}
#endif /* MY_ABC_HERE */

	bio_set_dev(bio, tmp_dev->rdev->bdev);
	bio->bi_iter.bi_sector = bio->bi_iter.bi_sector -
		start_sector + data_offset;

	if (unlikely((bio_op(bio) == REQ_OP_DISCARD) &&
		     !blk_queue_discard(bio->bi_disk->queue))) {
		/* Just ignore it */
#ifdef MY_ABC_HERE
		if (cloned_bio) {
			atomic_dec(&tmp_dev->rdev->nr_pending);
			orig_bio->bi_next = bio->bi_next;
			bio_put(bio);
			bio = orig_bio;
		}
#endif /* MY_ABC_HERE */
		bio_endio(bio);
	} else {
		if (mddev->gendisk)
			trace_block_bio_remap(bio->bi_disk->queue,
					      bio, disk_devt(mddev->gendisk),
					      bio_sector);
		mddev_check_writesame(mddev, bio);
		mddev_check_write_zeroes(mddev, bio);
		submit_bio_noacct(bio);
	}
	return true;

out_of_bounds:
	pr_err("md/linear:%s: make_request: Sector %llu out of bounds on dev %s: %llu sectors, offset %llu\n",
	       mdname(mddev),
	       (unsigned long long)bio->bi_iter.bi_sector,
	       bdevname(tmp_dev->rdev->bdev, b),
	       (unsigned long long)tmp_dev->rdev->sectors,
	       (unsigned long long)start_sector);
	bio_io_error(bio);
	return true;
}

#ifdef MY_ABC_HERE
static void
syno_linear_status(struct seq_file *seq, struct mddev *mddev)
{
	int i;
	struct linear_conf *conf = mddev->private;

	seq_printf(seq, " %dk rounding", mddev->chunk_sectors / 2);
	seq_printf(seq, " [%d/%d] [", mddev->raid_disks, mddev->raid_disks - mddev->degraded);
	rcu_read_lock();
	for (i = 0; i < mddev->raid_disks; i++)
	{
		struct md_rdev *rdev = rcu_dereference(conf->disks[i].rdev);
#ifdef MY_ABC_HERE
		seq_printf(seq, "%s", rdev && test_bit(In_sync, &rdev->flags)
			   ? (test_bit(SynoDiskError, &rdev->flags) ? "E" : "U")
			   : "_");
#else /* MY_ABC_HERE */
		seq_printf(seq, "%s", rdev && test_bit(In_sync, &rdev->flags) ? "U" : "_");
#endif /* MY_ABC_HERE */
	}
	rcu_read_unlock();
	seq_printf(seq, "]");
}
#else /* MY_ABC_HERE */
static void linear_status (struct seq_file *seq, struct mddev *mddev)
{
	seq_printf(seq, " %dk rounding", mddev->chunk_sectors / 2);
}
#endif /* MY_ABC_HERE */

static void linear_quiesce(struct mddev *mddev, int state)
{
}

#ifdef MY_ABC_HERE
static int
syno_linear_remove_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	int err = 0;
	struct linear_conf *conf = mddev->private;
	int number;

	if (!rdev)
		goto END;

	number = rdev->raid_disk;

	/*
	 *	use the same synchronize method as RAID5
	 *	see raid5.c:raid5_remove_disk
	 */
	conf->disks[number].rdev = NULL;
	synchronize_rcu();
	if (atomic_read(&rdev->nr_pending)) {
		/* lost the race, try later */
		err = -EBUSY;
		conf->disks[number].rdev = rdev;
		goto END;
	}

END:
	return err;
}

/**
 * This is our implement for raid handler.
 * It mainly for handling device hotplug.
 * We let it look like other raid type.
 * Set it faulty could let SDK know it's status
 *
 * @author \$Author: khchen $
 * @version \$Revision: 1.1
 *
 * @param mddev  Should not be NULL. passing from md.c
 * @param rdev   Should not be NULL. passing from md.c
 */
static void
syno_linear_error_for_hotplug(struct mddev *mddev, struct md_rdev *rdev)
{
	char b[BDEVNAME_SIZE];

	pr_crit("md/raid:%s: Disk failure on %s, disabling device.\n",
		mdname(mddev), bdevname(rdev->bdev, b));
	if (test_and_clear_bit(In_sync, &rdev->flags)) {
		if (mddev->degraded < mddev->raid_disks) {
			struct syno_update_sb_work *update_sb = NULL;

			mddev->degraded++;
#ifdef MY_ABC_HERE
			if (mddev->syno_nodev_and_crashed != MD_CRASHED_ASSEMBLE)
				mddev->syno_nodev_and_crashed = MD_CRASHED;
#endif /* MY_ABC_HERE */
			set_bit(Faulty, &rdev->flags);
#ifdef MY_ABC_HERE
			clear_bit(SynoDiskError, &rdev->flags);
#endif /* MY_ABC_HERE */

			update_sb = kzalloc(sizeof(*update_sb), GFP_ATOMIC);
			if (!update_sb) {
				WARN_ON(!update_sb);
				goto END;
			}

			INIT_WORK(&update_sb->work, syno_update_sb_task);
			update_sb->mddev = mddev;
			schedule_work(&update_sb->work);
		}
	}
END:
	return;
}

/**
 * This is our implement for raid handler.
 * It mainly for mdadm set device faulty. We let it look like
 * other raid type. Let it become read only (scemd would remount
 * if it find SynoDiskError)
 *
 * You should not sync super block in the same thread, otherwise
 * would panic.
 *
 * @author \$Author: khchen $
 * @version \$Revision: 1.1  *
 *
 * @param mddev  Should not be NULL. passing from md.c
 * @param rdev   Should not be NULL. passing from md.c
 */
static void
syno_linear_error_for_internal(struct mddev *mddev, struct md_rdev *rdev)
{
	char b[BDEVNAME_SIZE];

	pr_crit("md/raid:%s: Disk failure on %s, disabling device.\n",
		mdname(mddev), bdevname(rdev->bdev, b));
#ifdef MY_ABC_HERE
	if (!test_bit(SynoDiskError, &rdev->flags)) {
		struct syno_update_sb_work *update_sb = NULL;

		set_bit(SynoDiskError, &rdev->flags);
		update_sb = kzalloc(sizeof(*update_sb), GFP_ATOMIC);
		if (update_sb == NULL) {
			WARN_ON(!update_sb);
			return;
		}

		INIT_WORK(&update_sb->work, syno_update_sb_task);
		update_sb->mddev = mddev;
		schedule_work(&update_sb->work);
	}
#endif /* MY_ABC_HERE */
}
#endif /* MY_ABC_HERE */

static struct md_personality linear_personality =
{
	.name		= "linear",
	.level		= LEVEL_LINEAR,
	.owner		= THIS_MODULE,
	.make_request	= linear_make_request,
	.run		= linear_run,
	.free		= linear_free,
#ifdef MY_ABC_HERE
	.status		= syno_linear_status,
#else /* MY_ABC_HERE */
	.status		= linear_status,
#endif /* MY_ABC_HERE */
	.hot_add_disk	= linear_add,
#ifdef MY_ABC_HERE
	.hot_remove_disk    = syno_linear_remove_disk,
	.error_handler      = syno_linear_error_for_internal,
	.syno_error_handler = syno_linear_error_for_hotplug,
#endif /* MY_ABC_HERE */
	.size		= linear_size,
	.quiesce	= linear_quiesce,
};

static int __init linear_init (void)
{
	return register_md_personality (&linear_personality);
}

static void linear_exit (void)
{
	unregister_md_personality (&linear_personality);
}

module_init(linear_init);
module_exit(linear_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Linear device concatenation personality for MD");
MODULE_ALIAS("md-personality-1"); /* LINEAR - deprecated*/
MODULE_ALIAS("md-linear");
MODULE_ALIAS("md-level--1");
