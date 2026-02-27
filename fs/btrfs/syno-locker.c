#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000-2022 Synology Inc.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/iversion.h>
#include <linux/timekeeping.h>
#include <linux/string_helpers.h>

#include "ctree.h"
#include "disk-io.h"
#include "btrfs_inode.h"
#include "syno-feat-tree.h"

static bool locker_enfore_norename = false;
static bool locker_feature_support = true;
extern struct file_system_type *__btrfs_root_fs_type;

bool btrfs_syno_locker_feature_is_support(void)
{
	return locker_feature_support;
}

/*
 * runtime disable locker only if no volume is mounted
 */
int btrfs_syno_locker_feature_disable(void)
{
	/* btrfs_root_fs_type isn't registered and cannot get from get_fs_type */
	if (!hlist_empty(&__btrfs_root_fs_type->fs_supers))
		return 1;

	if (locker_feature_support) {
		locker_feature_support = false;
		pr_info("BTRFS: locker disabled\n");
	}

	return 0;
}

int btrfs_syno_locker_rename_disable(void)
{
	locker_enfore_norename = true;

	return 0;
}

static inline void __maybe_unused __dump_locker_root(struct btrfs_root* root)
{
	btrfs_debug(root->fs_info, "root-id=%llu, enabled=%u, mode=%d, def-state=%d, wait-t=%lld, dura=%lld\n",
		root->root_key.objectid, root->locker_enabled, root->locker_mode,
		root->locker_default_state, root->locker_waittime, root->locker_duration);
}

static inline void __maybe_unused __dump_locker_inode(struct btrfs_inode* binode)
{
	btrfs_debug(binode->root->fs_info, "ino=%lu, state=%d, update-t=%lld, begin=%lld, end=%lld\n",
		binode->vfs_inode.i_ino, binode->locker_state, binode->locker_update_time,
		binode->locker_period_begin, binode->locker_period_end);
}

static inline bool btrfs_is_ro_snapshot(struct btrfs_inode *binode)
{
	if (btrfs_ino(binode) != BTRFS_FIRST_FREE_OBJECTID)
		return false;

	if (!btrfs_root_readonly(binode->root))
		return false;

	return true;
}

static inline bool syno_locker_is_fs_clock_initialized(struct btrfs_fs_info *fs_info)
{
	lockdep_assert_held(&fs_info->locker_lock);

	return fs_info->locker_clock.tv_sec != 0;
}

static inline void syno_locker_fs_clock_init(struct btrfs_fs_info *fs_info)
{
	spin_lock(&fs_info->locker_lock);
	/* clock can only be initialized once */
	if (!syno_locker_is_fs_clock_initialized(fs_info)) {
		ktime_get_real_ts64(&fs_info->locker_clock);
		ktime_get_raw_ts64(&fs_info->locker_prev_raw_clock);
	}
	spin_unlock(&fs_info->locker_lock);
}

/*
 * tick the raw fs clock and return the time. please do not access
 * fs_info->locker_clock directly, or it'll not tick.
 */
struct timespec64 btrfs_syno_locker_fs_clock_get(struct btrfs_fs_info *fs_info)
{
	struct timespec64 raw_time, delta;

	spin_lock(&fs_info->locker_lock);

	/* uninitialized clock will not tick */
	if (!syno_locker_is_fs_clock_initialized(fs_info))
		goto out;

	ktime_get_raw_ts64(&raw_time);
	delta = timespec64_sub(raw_time, fs_info->locker_prev_raw_clock);
	fs_info->locker_clock = timespec64_add(fs_info->locker_clock, delta);
	fs_info->locker_prev_raw_clock = raw_time;

out:
	spin_unlock(&fs_info->locker_lock);

	return fs_info->locker_clock;
}

static inline struct timespec64 syno_locker_root_clock_get(struct btrfs_root *root)
{
	struct timespec64 clock = {0};

#ifdef MY_ABC_HERE
	if (btrfs_root_readonly(root)) {
		/*
		 * if subvolume clock is enabled, the clock is frozen in ro snapshot.
		 * locker_update_time_floor was borrowed to store the frozen clock.
		 */
		spin_lock(&root->locker_lock);
		clock.tv_sec = root->locker_update_time_floor;
		spin_unlock(&root->locker_lock);

		return clock;
	}
#endif /* MY_ABC_HERE */

	clock = btrfs_syno_locker_fs_clock_get(root->fs_info);

#ifdef MY_ABC_HERE
	spin_lock(&root->locker_lock);
	clock.tv_sec += root->locker_clock_adjustment;
	spin_unlock(&root->locker_lock);
#endif /* MY_ABC_HERE */

	return clock;
}

static inline struct timespec64 syno_locker_clock_get(struct btrfs_inode *binode)
{
	/*
	 * subvolumes always refers to fs_clock, because root_clock may not tick for
	 * ro snapshot if SUBVOLUME clock is enable.
	 */
	if (btrfs_ino(binode) == BTRFS_FIRST_FREE_OBJECTID)
		return btrfs_syno_locker_fs_clock_get(binode->root->fs_info);

	return syno_locker_root_clock_get(binode->root);
}

/*
 * return the delta between system clock and volume clock
 *
 * delta     = sys_clock - vol_clock
 * vol_clock = sys_clock - delta
 * sys_clock = vol_clock + delta
 */
static inline struct timespec64
syno_locker_sys_clock_delta(struct btrfs_inode *binode)
{
	struct timespec64 vol_clock, sys_clock, delta;

	vol_clock = syno_locker_clock_get(binode);
	WARN_ON(vol_clock.tv_sec == 0);

	ktime_get_real_ts64(&sys_clock);
	delta = timespec64_sub(sys_clock, vol_clock);

	return delta;
}

void btrfs_syno_locker_update_work_kick(struct btrfs_fs_info *fs_info)
{
	if (btrfs_fs_closing(fs_info))
		return;

	mod_delayed_work(system_wq, &fs_info->locker_update_work, fs_info->locker_update_interval * HZ);
}

void btrfs_syno_locker_update_work_fn(struct work_struct *work)
{
	struct btrfs_fs_info *fs_info = container_of(work, struct btrfs_fs_info, locker_update_work.work);

	if (sb_rdonly(fs_info->sb) || !btrfs_fs_compat_ro(fs_info, LOCKER)) {
		btrfs_syno_locker_update_work_kick(fs_info);
		return;
	}

	btrfs_set_pending(fs_info, COMMIT);
	btrfs_sync_fs(fs_info->sb, 1);
	btrfs_info(fs_info, "force to update superblock (%llu)", fs_info->generation);
}

static inline bool is_lockable(struct btrfs_inode *binode)
{
	return test_bit(BTRFS_INODE_LOCKER_LOCKABLE, &binode->runtime_flags);
}

static inline bool is_nolock(struct btrfs_inode *binode)
{
	return test_bit(BTRFS_INODE_LOCKER_NOLOCK, &binode->runtime_flags);
}

static inline bool is_lockable_unknown(struct btrfs_inode *binode)
{
	return !is_lockable(binode) && !is_nolock(binode);
}

static inline void set_lockable(struct btrfs_inode *binode)
{
	set_bit(BTRFS_INODE_LOCKER_LOCKABLE, &binode->runtime_flags);
	clear_bit(BTRFS_INODE_LOCKER_NOLOCK, &binode->runtime_flags);
}

static inline void set_nolock(struct btrfs_inode *binode)
{
	set_bit(BTRFS_INODE_LOCKER_NOLOCK, &binode->runtime_flags);
	clear_bit(BTRFS_INODE_LOCKER_LOCKABLE, &binode->runtime_flags);
}

static inline bool d_is_subvolume(struct dentry *dentry)
{
	return btrfs_ino(BTRFS_I(d_inode(dentry))) == BTRFS_FIRST_FREE_OBJECTID;
}

#define LOCKER_PREFIX_EADIR         "@eaDir"
#define LOCKER_PREFIX_TMP           "@tmp"
#define LOCKER_PREFIX_SHAREBIN      "@sharebin"
#define LOCKER_PREFIX_RECYCLE       "#recycle"

static struct qstr nolock_name[] = {
	QSTR_INIT(LOCKER_PREFIX_EADIR, sizeof(LOCKER_PREFIX_EADIR) - 1),
	QSTR_INIT(LOCKER_PREFIX_TMP, sizeof(LOCKER_PREFIX_TMP) - 1),
	QSTR_INIT(LOCKER_PREFIX_SHAREBIN, sizeof(LOCKER_PREFIX_SHAREBIN) - 1),
	QSTR_INIT(LOCKER_PREFIX_RECYCLE, sizeof(LOCKER_PREFIX_RECYCLE) - 1),
};

/*
 * if a btrfs inode is not a lockable object, such as regular file, symbolic
 * link, or a ro snapshot, it won't be lockable.
 *
 * if a btrfs inode is whitelisted, it won't be lockable either. a runtime flag
 * will also be marked on this btrfs inode.
 */
static bool syno_locker_is_whitelisted(struct btrfs_inode *binode)
{
	int i = 0;
	bool lockable = true;
	struct dentry *p = NULL, *pp = NULL;
	struct btrfs_inode *ancestor;
	struct super_block *sb = binode->vfs_inode.i_sb;

	/*
	 * to reduce the overhead, inode with multiple links doesn't be treated
	 * as whitelisted, even all the paths comply with the patterns.
	 */
	if (binode->vfs_inode.i_nlink != 1) {
		goto out;
	}

	p = d_find_any_alias(&binode->vfs_inode);
	if (!p)
		goto out;

	pp = dget_parent(p);
	while (true) {
		if (!pp || d_really_is_negative(pp) || sb != pp->d_sb)
			goto out;

		if (d_is_subvolume(pp))
			break;

		/*
		 * it won't be a fs root because we'll reach subvolume root first.
		 * if we meet the condition, it'll be a disconnected dentry from nfs,
		 * but it should not happen.
		 */
		if (IS_ROOT(pp)) {
			if (pp->d_flags & DCACHE_DISCONNECTED)
				pr_warn_ratelimited("locker: %pd is disconnected.", pp);
			goto out;
		}

		dput(p);
		p = pp;
		pp = dget_parent(p);
	}

	ancestor = BTRFS_I(d_inode(p));
	if (!is_lockable_unknown(ancestor)) {
		lockable = is_lockable(ancestor);
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(nolock_name); ++i) {
		if (!strcmp(nolock_name[i].name, p->d_name.name)) {
			set_nolock(ancestor);
			lockable = false;
			goto out;
		}
	}

	set_lockable(ancestor);
out:
	dput(p);
	dput(pp);

	return !lockable;
}

/*
 * regardless of whitelisted, only regular files, soft links and ro
 * snapshots are supported objects.
 */
static inline bool syno_locker_is_lockable_object(struct btrfs_inode *binode)
{
	struct inode *inode = &binode->vfs_inode;

	if (!btrfs_is_ro_snapshot(binode) && btrfs_root_readonly(binode->root))
		return false;

	return S_ISLNK(inode->i_mode) || S_ISREG(inode->i_mode) || btrfs_is_ro_snapshot(binode);
}

int btrfs_syno_locker_disk_root_update(struct btrfs_trans_handle *trans, struct btrfs_root *root)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_root_locker_item *item;
	struct btrfs_root *feat_root = root->fs_info->syno_feat_root;

	key.objectid = BTRFS_SYNO_BTRFS_LOCKER_OBJECTID;
	key.type = BTRFS_ROOT_LOCKER_KEY;
	key.offset = root->root_key.objectid;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	ret = btrfs_search_slot(trans, feat_root, &key, path, 0, 1);
	if (ret > 0) {
		btrfs_release_path(path);
		ret = btrfs_insert_empty_item(trans, feat_root, path, &key, sizeof(*item));
		if (ret)
			goto out;

		leaf = path->nodes[0];
		item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_locker_item);
		memzero_extent_buffer(leaf, (unsigned long)item, sizeof(*item));
	} else if (ret == 0) {
		leaf = path->nodes[0];
		item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_locker_item);
	} else
		goto out;

	spin_lock(&root->locker_lock);
	btrfs_set_root_locker_enabled(leaf, item, root->locker_enabled);
	btrfs_set_root_locker_mode(leaf, item, root->locker_mode);
	btrfs_set_root_locker_default_state(leaf, item, root->locker_default_state);
	btrfs_set_root_locker_waittime(leaf, item, root->locker_waittime);
	btrfs_set_root_locker_duration(leaf, item, root->locker_duration);
	btrfs_set_root_locker_clock_adjustment(leaf, item, root->locker_clock_adjustment);
	btrfs_set_root_locker_update_time_floor(leaf, item, root->locker_update_time_floor);
	btrfs_set_root_locker_state(leaf, item, root->locker_state);
	btrfs_set_root_locker_period_begin(leaf, item, root->locker_period_begin);
	btrfs_set_root_locker_period_begin_sys(leaf, item, root->locker_period_begin_sys);
	btrfs_set_root_locker_period_end(leaf, item, root->locker_period_end);
	btrfs_set_root_locker_period_end_sys(leaf, item, root->locker_period_end_sys);
	spin_unlock(&root->locker_lock);

	btrfs_mark_buffer_dirty(path->nodes[0]);

	ret = 0;
out:
	btrfs_free_path(path);

	if (!ret)
		btrfs_set_fs_compat_ro(root->fs_info, LOCKER);

	return ret;
}

static int btrfs_syno_locker_disk_root_update_trans(struct btrfs_root *root)
{
	int ret;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *feat_root = root->fs_info->syno_feat_root;

	if (!btrfs_syno_check_feat_tree_enable(root->fs_info))
		return -EPERM;

	trans = btrfs_start_transaction(feat_root, 1);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_syno_locker_disk_root_update(trans, root);
	if (ret) {
		btrfs_err(root->fs_info, "failed to update disk root locker item. err=%d", ret);
		goto abort;
	}

	return btrfs_commit_transaction(trans);

abort:
	btrfs_abort_transaction(trans, ret);
	btrfs_end_transaction(trans);
	return ret;
}

int btrfs_syno_locker_disk_root_read(struct btrfs_root *root)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_root_locker_item *item;
	struct btrfs_root *feat_root = root->fs_info->syno_feat_root;

	if (!btrfs_syno_check_feat_tree_enable(root->fs_info))
		return 0;

	key.objectid = BTRFS_SYNO_BTRFS_LOCKER_OBJECTID;
	key.type = BTRFS_ROOT_LOCKER_KEY;
	key.offset = root->root_key.objectid;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	ret = btrfs_search_slot(NULL, feat_root, &key, path, 0, 0);
	if (ret)
		goto out;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_locker_item);

	spin_lock(&root->locker_lock);
	root->locker_enabled            = btrfs_root_locker_enabled(leaf, item);
	root->locker_mode               = btrfs_root_locker_mode(leaf, item);
	root->locker_default_state      = btrfs_root_locker_default_state(leaf, item);
	root->locker_waittime           = btrfs_root_locker_waittime(leaf, item);
	root->locker_duration           = btrfs_root_locker_duration(leaf, item);
	root->locker_clock_adjustment   = btrfs_root_locker_clock_adjustment(leaf, item);
	root->locker_update_time_floor  = btrfs_root_locker_update_time_floor(leaf, item);
	root->locker_state              = btrfs_root_locker_state(leaf, item);
	root->locker_period_begin       = btrfs_root_locker_period_begin(leaf, item);
	root->locker_period_begin_sys   = btrfs_root_locker_period_begin_sys(leaf, item);
	root->locker_period_end         = btrfs_root_locker_period_end(leaf, item);
	root->locker_period_end_sys     = btrfs_root_locker_period_end_sys(leaf, item);
	spin_unlock(&root->locker_lock);

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * find if there's any root_locker_item
 *
 * @return: 1 has root_locker_item
 *          0 no root_locker_item
 *         <0 error
 */
static int syno_locker_has_root_locker_item(struct btrfs_fs_info *fs_info)
{
	int ret;
	int slot;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	struct btrfs_item *item;
	struct extent_buffer *leaf;
	struct btrfs_root *feat_root = fs_info->syno_feat_root;

	key.objectid = BTRFS_SYNO_BTRFS_LOCKER_OBJECTID;
	key.type = BTRFS_ROOT_LOCKER_KEY;
	key.offset = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(NULL, feat_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(feat_root, path);
			if (ret < 0)
				goto out;
			if (ret > 0) {
				ret = 0;
				goto out;
			}
			continue;
		}

		item = btrfs_item_nr(slot);
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.type == BTRFS_ROOT_LOCKER_KEY) {
			ret = 1;
			break;
		}

		path->slots[0]++;
	}

out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_syno_locker_disk_root_delete(struct btrfs_trans_handle *trans, struct btrfs_root *root)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_path *path;
	struct btrfs_root *feat_root = root->fs_info->syno_feat_root;

	key.objectid = BTRFS_SYNO_BTRFS_LOCKER_OBJECTID;
	key.type = BTRFS_ROOT_LOCKER_KEY;
	key.offset = root->root_key.objectid;

	if (!btrfs_syno_check_feat_tree_enable(root->fs_info))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	ret = btrfs_search_slot(trans, feat_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	else if (ret) {
		ret = 0;
		goto out;
	}

	ret = btrfs_del_item(trans, feat_root, path);
	if (ret)
		goto out;

	ret = 0;
out:
	btrfs_free_path(path);

	if (0 == syno_locker_has_root_locker_item(root->fs_info))
		btrfs_clear_fs_compat_ro(root->fs_info, LOCKER);

	return ret;
}

int btrfs_syno_locker_disk_root_delete_trans(struct btrfs_root *root)
{
	int ret = 0;
	struct btrfs_trans_handle *trans;

	if (!btrfs_syno_check_feat_tree_enable(root->fs_info))
		return 0;

	trans = btrfs_start_transaction(root->fs_info->syno_feat_root, 1);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_syno_locker_disk_root_delete(trans, root);
	if (ret) {
		btrfs_err(root->fs_info, "failed to delete disk root locker item. err=%d", ret);
		goto abort;
	}

	return btrfs_commit_transaction(trans);

abort:
	btrfs_abort_transaction(trans, ret);
	btrfs_end_transaction(trans);
	return ret;
}

int btrfs_syno_locker_snapshot_clone(struct btrfs_trans_handle *trans,
				     struct btrfs_root *dest, struct btrfs_root *source)
{
	bool dest_ro = btrfs_root_readonly(dest);
	bool source_ro = btrfs_root_readonly(source);

	if (source->locker_mode == LM_NONE)
		return 0;

	spin_lock(&source->locker_lock);

	/* locker is always disabled with new cloned subvolumes */
	dest->locker_enabled            = 0;
	dest->locker_mode               = source->locker_mode;
	dest->locker_default_state      = source->locker_default_state;
	dest->locker_waittime           = source->locker_waittime;
	dest->locker_duration           = source->locker_duration;
	dest->locker_clock_adjustment	= source->locker_clock_adjustment;
	dest->locker_update_time_floor  = source->locker_update_time_floor;

	dest->locker_state              = LS_OPEN;
	dest->locker_period_begin       = LOCKER_DEFAULT_PERIOD_BEGIN;
	dest->locker_period_begin_sys   = LOCKER_DEFAULT_PERIOD_BEGIN;
	dest->locker_period_end         = LOCKER_DEFAULT_PERIOD_END;
	dest->locker_period_end_sys     = LOCKER_DEFAULT_PERIOD_END;

	spin_unlock(&source->locker_lock);

	/*
	 * when a ro snapshot is taken from a rw subvolume, current fs_clock
	 * will be stored in dest update_time_floor (borrowed). therefor, we
	 * can exactly know what's the time (in fs_clock) the snapshot was taken.
	 *
	 * please remember that update_time_floor has totally different meaning
	 * in a rw subvolume.
	 *
	 * when a rw subvolume is cloned from ro snapshot, update_time_floor is used
	 * to calculate the (accumulated) clock_adjustment for root_clock.
	 * all non-locked files in this new-cloned rw subvolume will has a
	 * update_time that is later than the time the subvolume was restored.
	 *
	 * root_clock = fs_clock + subvolume clock_adjustment
	 */
	if (dest_ro && !source_ro) {
		struct timespec64 fs_clock;

		fs_clock = btrfs_syno_locker_fs_clock_get(source->fs_info);
		dest->locker_update_time_floor = fs_clock.tv_sec;
	} else if (!dest_ro && source_ro) {
		struct timespec64 fs_clock, root_clock;

		fs_clock = btrfs_syno_locker_fs_clock_get(source->fs_info);
		dest->locker_clock_adjustment -= fs_clock.tv_sec - source->locker_update_time_floor;

		root_clock = syno_locker_root_clock_get(dest);
		dest->locker_update_time_floor = root_clock.tv_sec;
	}

	return btrfs_syno_locker_disk_root_update(trans, dest);
}

int btrfs_syno_locker_may_destroy_subvol(struct btrfs_root *root)
{
	int ret = 0;

	spin_lock(&root->locker_lock);

	if (!root->locker_enabled)
		goto out;

	if (root->locker_mode != LM_COMPLIANCE)
		goto out;

	/*
	 * A ro subvolume is deletable even if it's in compliance mode. If a
	 * snapshot would like to be locked, SYNO_BTRFS_LOCKER_SNAPSHOT should
	 * be enabled and setting snapshot to immutable state.
	 */
	if (btrfs_root_readonly(root))
		goto out;

	ret = -EPERM;
out:
	spin_unlock(&root->locker_lock);
	return ret;
}

/**
 * disallow to rename subvolumes protected by locker
 * disallow to rename non-empty directories
 *
 * @reset: clear runtime flags for whitelist if successful
 */
int btrfs_syno_locker_may_rename(struct inode *old_dir, struct dentry *old_dentry,
				 struct inode *new_dir, struct dentry *new_dentry,
				 bool reset)
{
	int ret = 0;
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct btrfs_root *root = BTRFS_I(old_inode)->root;
	struct btrfs_root *dest = new_inode ? BTRFS_I(new_inode)->root : NULL;
	u64 old_ino = btrfs_ino(BTRFS_I(old_inode));
	u64 new_ino = new_inode ? btrfs_ino(BTRFS_I(new_inode)) : 0;

	if (root->locker_mode == LM_NONE)
		goto out;

	if (syno_locker_is_whitelisted(BTRFS_I(old_inode)) &&
	    (!new_inode || syno_locker_is_whitelisted(BTRFS_I(new_inode))))
		goto out;

	/*
	 * the expired old file still cannot be renamed but it would pass may_delete()
	 * check in vfs_rename(). an additional validation is required here.
	 *
	 * if the new file exists and is already expired, it can be replaced.
	 */
	if (IS_EXPIRED(old_inode)) {
		ret = -EPERM;
		goto out;
	}

	spin_lock(&root->locker_lock);
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID && locker_enfore_norename &&
	    root->locker_enabled && root->locker_mode != LM_NONE) {
		ret = -EPERM;
		spin_unlock(&root->locker_lock);
		goto out;
	}

	if (old_ino != BTRFS_FIRST_FREE_OBJECTID &&
	    root->locker_enabled && root->locker_mode != LM_NONE &&
	    S_ISDIR(old_inode->i_mode) && old_inode->i_size > BTRFS_EMPTY_DIR_SIZE) {
		ret = -EOPNOTSUPP;
		spin_unlock(&root->locker_lock);
		goto out;
	}
	spin_unlock(&root->locker_lock);

	if (!dest)
		goto out;

	spin_lock(&dest->locker_lock);
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID && dest &&
	    dest->locker_enabled && dest->locker_mode != LM_NONE) {
		ret = -EPERM;
		spin_unlock(&dest->locker_lock);
		goto out;
	}

	if (dest && dest->locker_enabled && dest->locker_mode != LM_NONE &&
	    S_ISDIR(new_inode->i_mode) && new_inode->i_size > BTRFS_EMPTY_DIR_SIZE) {
		ret = -ENOTEMPTY;
		spin_unlock(&dest->locker_lock);
		goto out;
	}
	spin_unlock(&dest->locker_lock);

out:
	if (!ret && reset) {
		clear_bit(BTRFS_INODE_LOCKER_NOLOCK, &BTRFS_I(old_inode)->runtime_flags);
		clear_bit(BTRFS_INODE_LOCKER_LOCKABLE, &BTRFS_I(old_inode)->runtime_flags);
		if (new_inode) {
			clear_bit(BTRFS_INODE_LOCKER_NOLOCK, &BTRFS_I(new_inode)->runtime_flags);
			clear_bit(BTRFS_INODE_LOCKER_LOCKABLE, &BTRFS_I(new_inode)->runtime_flags);
		}
	}

	return ret;
}

static inline int syno_locker_root_enable(struct btrfs_root *root)
{
	lockdep_assert_held(&root->locker_lock);

	if (root->locker_mode == LM_NONE)
		return -EPERM;

	root->locker_enabled = 1;

	return 0;
}

struct btrfs_locker_xattr {
	__u8 state;
	__s64 update_time;
	__s64 period_begin;
	__s64 period_end;
} __attribute__ ((__packed__));

static int syno_locker_xattr_set(struct btrfs_trans_handle *trans, struct inode *inode,
				       const void *buffer, size_t size, int flags)
{
	int ret;

	if (trans)
		ret = btrfs_setxattr(trans, inode, XATTR_SYNO_LOCKER, buffer, size, flags);
	else
		ret = btrfs_setxattr_trans(inode, XATTR_SYNO_LOCKER, buffer, size, flags);

	return ret;
}

static int syno_locker_disk_inode_update(struct btrfs_trans_handle *trans, struct inode *inode)
{
	int ret;
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_locker_xattr xattr;

	WARN_ON(btrfs_ino(binode) == BTRFS_FIRST_FREE_OBJECTID);

	spin_lock(&binode->locker_lock);
	xattr.state          = binode->locker_state;
	xattr.update_time    = cpu_to_le64(binode->locker_update_time);
	xattr.period_begin   = cpu_to_le64(binode->locker_period_begin);
	xattr.period_end     = cpu_to_le64(binode->locker_period_end);
	spin_unlock(&binode->locker_lock);

	ret = syno_locker_xattr_set(trans, inode, &xattr, sizeof(xattr), 0);

	if (ret) {
		btrfs_err(binode->root->fs_info, "failed to set locker xattr. err=%d", ret);
		btrfs_abort_transaction(trans, ret);
	}

	return ret;
}

int btrfs_syno_locker_disk_inode_update_trans(struct inode *inode)
{
	int ret;
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_root *root = binode->root;
	struct btrfs_trans_handle *trans;

	if (!binode->locker_dirty)
		return 0;

	/*
	 * the inode for subvolume is special and cannot be modified if
	 * the subvolume is read-only, or btrfs send/recv will have trouble.
	 */
	if (btrfs_ino(BTRFS_I(inode)) == BTRFS_FIRST_FREE_OBJECTID) {
		spin_lock(&binode->locker_lock);

		if (binode->locker_state == LS_OPEN &&
		    binode->locker_period_end == LOCKER_DEFAULT_PERIOD_END) {
			spin_unlock(&binode->locker_lock);
			/* do nothing if they are default values */
			return 0;
		}

		root->locker_state          = binode->locker_state;
		root->locker_period_begin   = binode->locker_period_begin;
		root->locker_period_end     = binode->locker_period_end;
		spin_unlock(&binode->locker_lock);

		ret = btrfs_syno_locker_disk_root_update_trans(root);
		if (!ret)
			binode->locker_dirty = false;

		return ret;
	}

	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = syno_locker_disk_inode_update(trans, inode);
	if (ret) {
		btrfs_err(root->fs_info, "failed to update disk inode locker attrs. err=%d", ret);
		goto abort;
	}

	inode_inc_iversion(inode);
	set_bit(BTRFS_INODE_COPY_EVERYTHING, &BTRFS_I(inode)->runtime_flags);
	ret = btrfs_update_inode_fallback(trans, root, inode);
	if (ret) {
		btrfs_err(root->fs_info, "failed to update inode. err=%d", ret);
		goto abort;
	}

	ret = btrfs_end_transaction(trans);
	if (!ret)
		binode->locker_dirty = false;

	return ret;

abort:
	btrfs_abort_transaction(trans, ret);
	btrfs_end_transaction(trans);
	return ret;
}

int btrfs_syno_locker_disk_inode_read(struct inode *inode)
{
	int ret;
	struct btrfs_locker_xattr xattr;
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_root *root = binode->root;

	if (btrfs_ino(binode) == BTRFS_FIRST_FREE_OBJECTID) {
		/* btrfs root has been ready in memory. copy attributes form it */
		spin_lock(&binode->locker_lock);
		binode->__locker_state          = root->locker_state;
		binode->__locker_update_time    = 0;
		binode->__locker_period_begin   = root->locker_period_begin;
		binode->__locker_period_end     = root->locker_period_end;
		binode->locker_dirty            = false;
		spin_unlock(&binode->locker_lock);
		return 0;
	}

	ret = btrfs_getxattr(inode, XATTR_SYNO_LOCKER, &xattr, sizeof(xattr));

	if (ret == -ENODATA)
		goto out;
	else if (ret != sizeof(xattr)) {
		btrfs_err(BTRFS_I(inode)->root->fs_info, "failed to get locker xattr. ret=%d", ret);
		goto out;
	}

	spin_lock(&binode->locker_lock);
	binode->__locker_state          = xattr.state;
	binode->__locker_update_time    = le64_to_cpu(xattr.update_time);
	binode->__locker_period_begin   = le64_to_cpu(xattr.period_begin);
	binode->__locker_period_end     = le64_to_cpu(xattr.period_end);
	binode->locker_dirty            = false;
	spin_unlock(&binode->locker_lock);

	ret = 0;
out:
	return ret;
}

/**
 * if time is too large or overflow, limit it to 9999/12/31 00:00:00 for
 * userspace display.
 */
static inline void __truncate_time(time64_t *time)
{
	if (*time < 0 || *time > 253402214400)
		*time = 253402214400;
}

/*
 * update time is the base of auto-lock. this routine refreshes and returns
 * the update_time of a file in volume clock.
 *
 * new update_time = max(update_time, mtime, ctime, root->update_time_floor)
 */
static time64_t syno_locker_update_time(struct btrfs_inode *binode)
{
	time64_t sys_update_time;
	struct timespec64 delta;
	struct inode *inode = &binode->vfs_inode;

	lockdep_assert_held(&binode->locker_lock);

	if (btrfs_ino(binode) == BTRFS_FIRST_FREE_OBJECTID)
		return 0;

	if (binode->root->locker_mode == LM_NONE)
		goto out;

	/* update_time isn't expected to be changed after a file is locked */
	if (binode->locker_state != LS_OPEN)
		goto out;

	delta = syno_locker_sys_clock_delta(binode);
	sys_update_time = (timespec64_compare(&inode->i_mtime, &inode->i_ctime) > 0) ?
		inode->i_mtime.tv_sec : inode->i_ctime.tv_sec;

	if (binode->locker_update_time == LOCKER_DEFAULT_UPDATE_TIME) {
		binode->__locker_update_time = sys_update_time - delta.tv_sec;
		binode->locker_dirty = true;
	}

	if (binode->locker_update_time < sys_update_time - delta.tv_sec) {
		binode->__locker_update_time = sys_update_time - delta.tv_sec;
		binode->locker_dirty = true;
	}

	if (binode->locker_update_time < binode->root->locker_update_time_floor) {
		binode->__locker_update_time = binode->root->locker_update_time_floor;
		binode->locker_dirty = true;
	}

out:
	return binode->locker_update_time;
}

static inline time64_t syno_locker_timestamp_get(struct btrfs_inode *binode, int flag)
{
	struct btrfs_root *root = binode->root;
	struct inode *inode = &binode->vfs_inode;

	if (btrfs_ino(binode) == BTRFS_FIRST_FREE_OBJECTID) {
		if (flag == S_MTIME)
			return 0;
		if (flag == S_CTIME)
			return root->locker_period_begin_sys;
		if (flag == S_ATIME)
			return root->locker_period_end_sys;
	} else {
		if (flag == S_MTIME)
			return inode->i_mtime.tv_sec;
		if (flag == S_CTIME)
			return inode->i_ctime.tv_sec;
		if (flag == S_ATIME)
			return inode->i_atime.tv_sec;
	}

	return -EINVAL;
}

static inline int syno_locker_timestamp_set(struct btrfs_inode *binode, time64_t time, int flag)
{
	struct btrfs_root *root = binode->root;
	struct inode *inode = &binode->vfs_inode;
	struct timespec64 ts = { .tv_sec = time };

	if (flag != S_MTIME && flag != S_CTIME && flag != S_ATIME)
		return -EINVAL;

	if (btrfs_ino(binode) == BTRFS_FIRST_FREE_OBJECTID) {
		if (flag == S_MTIME)
			return 0;
		if (flag == S_CTIME)
			root->locker_period_begin_sys = time;
		if (flag == S_ATIME)
			root->locker_period_end_sys = time;

		return btrfs_syno_locker_disk_root_update_trans(root);
	}

	return inode->i_op->update_time(inode, &ts, flag);
}

/**
 * store volume clock in locker_period_begin, and system clock in ctime.
 *
 * the delta changes with time. if we store only one of them, we will
 * lose the other one.
 *
 * @time: epoch time in volume clock
 */
static inline int
syno_locker_inode_period_begin_set(struct btrfs_inode *binode, time64_t time)
{
	time64_t ctime, mtime;
	struct timespec64 delta;

	lockdep_assert_held(&binode->locker_lock);

	if (binode->root->locker_mode == LM_NONE)
		return -EINVAL;

	if (time < 0)
		return -EINVAL;

	/* the end of lock-period should be in the future first. */
	if (time > binode->locker_period_end)
		return -EINVAL;

	delta = syno_locker_sys_clock_delta(binode);

	/*
	 * update_time is determined and can be stored in mtime.
	 * this step should be processed before begin_time because it'll refer
	 * to ctime.
	 */
	mtime = syno_locker_update_time(binode) + delta.tv_sec;
	__truncate_time(&mtime);

	/* begin time in volume clock */
	binode->__locker_period_begin = time;
	binode->locker_dirty = true;

	/* begin time in system clock */
	ctime = time + delta.tv_sec;
	__truncate_time(&ctime);

	spin_unlock(&binode->locker_lock);
	syno_locker_timestamp_set(binode, mtime, S_MTIME);
	syno_locker_timestamp_set(binode, ctime, S_CTIME);
	spin_lock(&binode->locker_lock);

	return 0;
}

/**
 * set current volume clock into the begin of lock-period.
 */
static inline int
syno_locker_inode_period_begin_set_current(struct btrfs_inode *binode)
{
	struct timespec64 vol_clock;

	vol_clock = syno_locker_clock_get(binode);

	return syno_locker_inode_period_begin_set(binode, vol_clock.tv_sec);
}

/**
 * for relock, the period_end is allowed to be changed after it was reached
 * and the raw state became expired.
 *
 * @time: epoch time in volume clock
 */
static inline int
syno_locker_inode_period_end_set(struct btrfs_inode *binode, time64_t time)
{
	int ret = 0;
	time64_t atime;
	struct timespec64 delta;

	lockdep_assert_held(&binode->locker_lock);

	if (binode->root->locker_enabled) {
		if (time < 0) {
			ret = -EINVAL;
			goto out;
		}

		/* extend only after locked */
		if (binode->locker_state != LS_OPEN && time < binode->locker_period_end) {
			ret = -EINVAL;
			goto out;
		}
	}

	binode->__locker_period_end = time;
	binode->locker_dirty = true;

	/*
	 * although btrfs_syno_locker_fillattr() always reports period_end to atime
	 * before expired, we still need to update atime here. because we may update
	 * period_end after a file is expired for relock (through atime). if atime
	 * isn't updated in this situation, it'd be confused.
	 */
	delta = syno_locker_sys_clock_delta(binode);
	atime = time + delta.tv_sec;

	spin_unlock(&binode->locker_lock);
	syno_locker_timestamp_set(binode, atime, S_ATIME);
	spin_lock(&binode->locker_lock);

out:
	if (ret)
		btrfs_debug(binode->root->fs_info, "invalid lock period end. cur:%lld, new:%lld",
			  binode->locker_period_end, time);

	return ret;
}

inline int btrfs_syno_locker_fillattr(struct inode *inode, struct kstat *stat)
{
	enum locker_state state;
	struct timespec64 delta;
	struct btrfs_inode *binode = BTRFS_I(inode);

	if (binode->root->locker_mode == LM_NONE)
		return 0;

	btrfs_syno_locker_state_get(inode, &state);
	delta = syno_locker_sys_clock_delta(binode);

	/*
	 * when a file is not expired (even not locked), the lock period_end isn't
	 * yet fixed in system clock (atime), but could be estimated.
	 */
	if (binode->locker_period_end != LOCKER_DEFAULT_PERIOD_END &&
	    state != LS_EXPIRED_I && state != LS_EXPIRED_A) {
		stat->atime.tv_sec = binode->locker_period_end + delta.tv_sec;
		stat->atime.tv_nsec = 0;
		__truncate_time(&stat->atime.tv_sec);
	}

	/*
	 * show no write permission if immutable
	 */
	if (IS_LOCKER_STATE_IMMUTABLE(state))
		stat->mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);

	return 0;
}

/**
 * @time: time in system clock
 */
inline int btrfs_syno_locker_period_end_set(struct inode *inode, struct timespec64 *time)
{
	int ret;
	struct timespec64 delta, vol_time;
	struct btrfs_inode *binode = BTRFS_I(inode);

	if (binode->root->locker_mode == LM_NONE)
		return 0;

	delta = syno_locker_sys_clock_delta(binode);
	vol_time = timespec64_sub(*time, delta);

	spin_lock(&binode->locker_lock);
	ret = syno_locker_inode_period_end_set(binode, vol_time.tv_sec);
	spin_unlock(&binode->locker_lock);

	return ret;
}

/*
 * The locker state of a file may be transited by manual-lock in `syno_locker_state_set()`,
 * or auto-lock in `btrfs_syno_locker_state_get()`.  All the transitions should follow the state
 * machine.
 *
 * @startuml
 * hide empty description
 * State Open
 * State Immutable
 * State Appendable
 * State "Expired\nfrom Immutable" as expired_i
 * State "Expired\nfrom Appendable" as expired_a
 *
 * Open -right-> Immutable : Auto/Manual
 * Open -down-> Appendable : Auto/Manual
 * Immutable -right-> expired_i : Auto
 * Immutable -down[dashed]-> Appendable : Manual\n(empty)
 * Appendable -right-> expired_a : Auto
 * Appendable -up[dashed]-> Immutable : Manual
 * expired_a -left[dashed]-> Appendable : Manual
 * expired_a -left[dashed]-> Immutable : Manual
 * expired_i -[dashed]-> Appendable: Manual\n(empty)
 * expired_i -left[dashed]-> Immutable : Manual
 * expired_a -right[dashed]-> [*] : Manual Delete
 * expired_i -right[dashed]-> [*] : Manual Delete
 *
 * State Weak {
 *   	State "Weak\nImmutable" as weak_i
 * 	State "Weak\nAppendable" as weak_a
 * }
 * weak_i -right[dashed]-> weak_a : Manual
 * weak_a -left[dashed]-> weak_i : Manual
 * weak_i --> expired_i : Auto
 * weak_a --> expired_a : Auto
 * Open -[dashed]-> Weak : Manual
 * Weak -[dashed]-> Open : Manual
 * @enduml
 */

static int syno_locker_state_set(struct btrfs_inode *binode, enum locker_state state)
{
	int ret = -EINVAL;
	struct btrfs_root *root = binode->root;

	lockdep_assert_held(&binode->locker_lock);

	if (state > LS_MAX)
		goto out;

	/* a special way for btrfs recv to set up locker attrs */
	if (!root->locker_enabled && root->locker_mode == LM_NONE) {
		binode->__locker_state = state;
		binode->locker_dirty = true;
		return 0;
	}

	if (state == binode->locker_state)
		return 0;

	if (binode->locker_period_end < 0)
		goto out;

	if (!root->locker_enabled)
		goto setup;

	switch (binode->locker_state) {
	case LS_OPEN:
		if (state != LS_IMMUTABLE && state != LS_APPENDABLE &&
		    state != LS_W_IMMUTABLE && state != LS_W_APPENDABLE)
			goto out;
		break;

	case LS_IMMUTABLE:
		if (state != LS_APPENDABLE)
			goto out;
		if (state == LS_APPENDABLE && binode->vfs_inode.i_size)
			goto out;
		break;

	case LS_APPENDABLE:
		if (state != LS_IMMUTABLE)
			goto out;
		break;

	case LS_EXPIRED_I:
		if (state != LS_IMMUTABLE && state != LS_APPENDABLE)
			goto out;
		if (state == LS_APPENDABLE && binode->vfs_inode.i_size)
			goto out;
		break;

	case LS_EXPIRED_A:
		if (state != LS_IMMUTABLE && state != LS_APPENDABLE)
			goto out;
		break;

	case LS_W_IMMUTABLE:
		if (state != LS_OPEN && state != LS_W_APPENDABLE)
			goto out;
		break;

	case LS_W_APPENDABLE:
		if (state != LS_OPEN && state != LS_W_IMMUTABLE)
			goto out;
		break;
	}

setup:
	/* prevent to be locked immediately by auto-lock */
	if (state == LS_OPEN) {
		struct timespec64 clock = syno_locker_clock_get(binode);
		binode->__locker_update_time = clock.tv_sec;
	}

	/* update period_begin if manually locked from open, or relock */
	if (binode->locker_state == LS_OPEN || binode->locker_state == LS_EXPIRED_I ||
	    binode->locker_state == LS_EXPIRED_A) {
		if (state == LS_IMMUTABLE || state == LS_APPENDABLE ||
		    state == LS_W_IMMUTABLE || state == LS_W_APPENDABLE) {
			syno_locker_inode_period_begin_set_current(binode);
		}
	}

	binode->__locker_state = state;
	binode->locker_dirty = true;

	ret = 0;
out:
	return ret;
}

int btrfs_syno_locker_mode_get(struct inode *inode, enum locker_mode *mode)
{
	*mode = BTRFS_I(inode)->root->locker_mode;

	return 0;
}

int btrfs_syno_locker_state_set(struct inode *inode, enum locker_state state)
{
	int ret;
	struct btrfs_inode *binode = BTRFS_I(inode);

	spin_lock(&binode->locker_lock);
	ret = syno_locker_state_set(binode, state);
	spin_unlock(&binode->locker_lock);

	if (ret)
		goto out;

	ret = btrfs_syno_locker_disk_inode_update_trans(inode);

out:
	return ret;
}

/*
 * auto-lock is triggered when the locker state of a file is observed.
 */
int btrfs_syno_locker_state_get(struct inode *inode, enum locker_state *state)
{
	int ret = 0;
	time64_t target;
	struct timespec64 vol_clock, delta;
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_root *root = binode->root;

	if (root->locker_mode == LM_NONE)
		return -EOPNOTSUPP;

	WARN_ON_ONCE(!btrfs_syno_check_feat_tree_enable(root->fs_info));
	ASSERT(btrfs_syno_check_feat_tree_enable(root->fs_info));

	if (!root->locker_enabled) {
		*state = LS_OPEN;
		goto out;
	}

#ifdef MY_ABC_HERE
	/* not supported for pure directories and r/w subvolumes */
	if (S_ISDIR(inode->i_mode) && !btrfs_is_ro_snapshot(binode)) {
		*state = LS_OPEN;
		goto out;
	}
#else
	if (S_ISDIR(inode->i_mode)) {
		*state = LS_OPEN;
		goto out;
	}
#endif /* MY_ABC_HERE */

	if (!syno_locker_is_lockable_object(binode) ||
	    syno_locker_is_whitelisted(binode)) {
		*state = LS_OPEN;
		goto out;
	}

	vol_clock = syno_locker_clock_get(binode);
	delta = syno_locker_sys_clock_delta(binode);
	WARN_ON_ONCE(vol_clock.tv_sec == 0);

	spin_lock(&binode->locker_lock);
	*state = binode->locker_state;

	switch (binode->locker_state) {
	case LS_OPEN:
#ifdef MY_ABC_HERE
		/* no auto-lock for r/o snapshots */
		if (btrfs_is_ro_snapshot(binode))
			break;
#endif /* MY_ABC_HERE */

		/* auto-lock */
		target = syno_locker_update_time(binode) + root->locker_waittime;
		if (target > 0 && vol_clock.tv_sec >= target + 5) { /* 5 seconds for tolerance */
			*state = root->locker_default_state;
			if (target + root->locker_duration < 0)
				syno_locker_inode_period_end_set(binode, TIME64_MAX); /* overflow */
			else
				syno_locker_inode_period_end_set(binode, target + root->locker_duration);

			syno_locker_inode_period_begin_set(binode, target);
		}
		break;

	case LS_IMMUTABLE:
		WARN_ON(binode->locker_period_end == LOCKER_DEFAULT_PERIOD_END);

		if (vol_clock.tv_sec >= binode->locker_period_end)
			*state = LS_EXPIRED_I;
		break;

	case LS_APPENDABLE:
		WARN_ON(binode->locker_period_end == LOCKER_DEFAULT_PERIOD_END);

		if (vol_clock.tv_sec >= binode->locker_period_end)
			*state = LS_EXPIRED_A;
		break;

	case LS_EXPIRED_I:
		/* do nothing */
		break;

	case LS_EXPIRED_A:
		/* do nothing */
		break;

	case LS_W_IMMUTABLE:
		WARN_ON(binode->locker_period_end == LOCKER_DEFAULT_PERIOD_END);

		if (vol_clock.tv_sec >= binode->locker_period_end)
			*state = LS_EXPIRED_I;
		break;

	case LS_W_APPENDABLE:
		WARN_ON(binode->locker_period_end == LOCKER_DEFAULT_PERIOD_END);

		if (vol_clock.tv_sec >= binode->locker_period_end)
			*state = LS_EXPIRED_A;
		break;
	}

	if (binode->locker_state != *state) {
		time64_t atime = binode->locker_period_end + delta.tv_sec;

		binode->__locker_state = *state;
		binode->locker_dirty = true;
		__truncate_time(&atime);
		spin_unlock(&binode->locker_lock);

		if (*state == LS_EXPIRED_I || *state == LS_EXPIRED_A)
			syno_locker_timestamp_set(binode, atime, S_ATIME);
		ret = btrfs_syno_locker_disk_inode_update_trans(inode);
	} else
		spin_unlock(&binode->locker_lock);

	__dump_locker_root(root);
	__dump_locker_inode(binode);

out:
	return ret;
}

static void __fill_ioctl_sys_time(struct btrfs_inode *binode, struct btrfs_ioctl_syno_locker_args *args)
{
	time64_t delta = args->clock_delta;

	lockdep_assert_held(&binode->locker_lock);

	if (args->flags & BTRFS_LOCKER_BEGIN) {
		/*
		 * once a file is not in open state, period_begin will not be the initial
		 * value (LOCKER_DEFAULT_PERIOD_BEGIN) and be stored in ctime (in system
		 * clock). we should report the ctime but not `period_begin + delta` for
		 * system clock.
		 */
		if (binode->locker_period_begin == LOCKER_DEFAULT_PERIOD_BEGIN)
			args->period_begin_sys = LOCKER_DEFAULT_PERIOD_BEGIN;
		else
			args->period_begin_sys = syno_locker_timestamp_get(binode, S_CTIME);
	}

	if (args->flags & BTRFS_LOCKER_END) {
		/*
		 * when the raw state of a object isn't expired, period_end is calculated and
		 * changes over the time, because delta is unfixed.
		 *
		 * period_end will be stored in atime if it's updated, but only report it after
		 * it's fixed (expired).
		 */
		if (binode->locker_period_end == LOCKER_DEFAULT_PERIOD_END) {
			args->period_end_sys = LOCKER_DEFAULT_PERIOD_END;
		} else if (binode->locker_state != LS_EXPIRED_I && binode->locker_state != LS_EXPIRED_A) {
			args->period_end_sys = binode->locker_period_end + delta;
			__truncate_time(&args->period_end_sys);
		} else {
			args->period_end_sys = syno_locker_timestamp_get(binode, S_ATIME);
		}
	}

	if (args->flags & BTRFS_LOCKER_UPDATE_TIME) {
		/*
		 * once a file is locked (raw state is not LS_OPEN), update_time will be never
		 * changed and stored in mtime.
		 */
		if (binode->locker_state == LS_OPEN)
			args->update_time_sys = syno_locker_update_time(binode) + delta;
		else
			args->update_time_sys = syno_locker_timestamp_get(binode, S_MTIME);
	}
}

int btrfs_ioctl_syno_locker_get(struct file *file, struct btrfs_ioctl_syno_locker_args __user *argp)
{
	int ret;
	enum locker_state state;
	struct timespec64 clock;
	struct inode *inode = file_inode(file);
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_root *root = binode->root;
	struct btrfs_ioctl_syno_locker_args locker_args;

	memset(&locker_args, 0, sizeof(locker_args));

	clock = syno_locker_clock_get(binode);
	if (clock.tv_sec) {
		struct timespec64 delta = syno_locker_sys_clock_delta(binode);

		locker_args.flags |= BTRFS_LOCKER_CLOCK;
		locker_args.clock = clock.tv_sec;
		locker_args.flags |= BTRFS_LOCKER_CLOCK_DELTA;
		locker_args.clock_delta = delta.tv_sec;
	}

	spin_lock(&root->locker_lock);
	locker_args.flags |= BTRFS_LOCKER_ENABLED;
	locker_args.enabled = root->locker_enabled;

	locker_args.flags |= BTRFS_LOCKER_MODE;
	locker_args.mode = root->locker_mode;

	locker_args.flags |= (BTRFS_LOCKER_DEFAULT_STATE|BTRFS_LOCKER_WAITTIME|BTRFS_LOCKER_DURATION);
	locker_args.default_state = root->locker_default_state;
	locker_args.waittime = root->locker_waittime;
	locker_args.duration = root->locker_duration;

	locker_args.flags |= (BTRFS_LOCKER_CLOCK_ADJUSTMENT|BTRFS_LOCKER_UPDATE_TIME_FLOOR);
	locker_args.clock_adjustment = root->locker_clock_adjustment;
	locker_args.update_time_floor = root->locker_update_time_floor;
	spin_unlock(&root->locker_lock);

	ret = syno_op_locker_state_get(inode, &state);
	if (!ret) {
		locker_args.flags |= BTRFS_LOCKER_STATE;
		locker_args.state = state;
	}

	locker_args.flags |= BTRFS_LOCKER_LOCKABLE;
	if (locker_args.enabled)
		locker_args.lockable = syno_locker_is_lockable_object(binode) &&
				       !syno_locker_is_whitelisted(binode);

	spin_lock(&binode->locker_lock);
	locker_args.flags |= BTRFS_LOCKER_RAW_STATE;
	locker_args.raw_state = binode->locker_state;

	locker_args.flags |= (BTRFS_LOCKER_BEGIN|BTRFS_LOCKER_END);
	locker_args.period_begin = binode->locker_period_begin;
	locker_args.period_end = binode->locker_period_end;

	locker_args.flags |= BTRFS_LOCKER_UPDATE_TIME;
	locker_args.update_time = binode->locker_update_time;

	if (locker_args.flags & BTRFS_LOCKER_RAW_STATE)
		__fill_ioctl_sys_time(binode, &locker_args);

	spin_unlock(&binode->locker_lock);

	if (copy_to_user(argp, &locker_args, sizeof(locker_args)))
		return -EFAULT;

	return 0;
}

/*
 * validate the arguments from ioctl to set attributes
 */
static int syno_locker_ioctl_args_validate(struct btrfs_inode *binode,
					   struct btrfs_ioctl_syno_locker_args *args)
{
	struct btrfs_root *root = binode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;

	spin_lock(&binode->locker_lock);
	spin_lock(&root->locker_lock);

	args->flags &= BTRFS_LOCKER_MASK_ALL;

	if ((args->flags & BTRFS_LOCKER_CLOCK) ||
	    (args->flags & BTRFS_LOCKER_CLOCK_DELTA)) {
		btrfs_debug(fs_info, "volume clock is read-only");
		goto fail_unlock;
	}

	if (args->flags & BTRFS_LOCKER_CLOCK_ADJUSTMENT) {
		if (root->locker_enabled) {
			btrfs_debug(fs_info, "volume clock adjustment is read-only");
			goto fail_unlock;
		}
	}

	if (args->flags & BTRFS_LOCKER_ENABLED) {
		if (!args->enabled) {
			btrfs_debug(fs_info, "you shall not disable locker");
			goto fail_unlock;
		}
		if (root->locker_mode == LM_NONE &&
		    (!(args->flags & BTRFS_LOCKER_MODE) || (args->mode == LM_NONE))) {
			btrfs_debug(fs_info, "mode isn't specified when enabling locker");
			goto fail_unlock;
		}
	}

	if (args->flags & BTRFS_LOCKER_MODE) {
		if (root->locker_enabled && root->locker_mode == LM_COMPLIANCE) {
			btrfs_debug(fs_info, "compliance mode isn't alterable after locker is enabled");
			goto fail_unlock;
		}
		if (args->mode > LM_MAX) {
			btrfs_debug(fs_info, "invalid mode (%d)", args->mode);
			goto fail_unlock;
		}
	}

	if (args->flags & BTRFS_LOCKER_DEFAULT_STATE) {
		if (args->default_state != LS_IMMUTABLE && args->default_state != LS_APPENDABLE) {
			btrfs_debug(fs_info, "invalid default state (%d) for auto-lock", args->default_state);
			goto fail_unlock;
		}
	}

	if (args->flags & BTRFS_LOCKER_WAITTIME) {
		if (args->waittime < 0) {
			btrfs_debug(fs_info, "invalid waittime (%lld) for auto-lock", args->waittime);
			goto fail_unlock;
		}
	}

	if (args->flags & BTRFS_LOCKER_DURATION) {
		if (args->duration < 0) {
			btrfs_debug(fs_info, "invalid default duration (%lld) for auto-lock", args->duration);
			goto fail_unlock;
		}
	}

	if (args->flags & BTRFS_LOCKER_STATE) {
		/*
		 * for btrfs recv, it's ok to set the state of a file when locker is not
		 * enabled. 'raw_state' will reveal the result, but (effective) `state` may not.
		 */
		if (args->state > LS_MAX) {
			btrfs_debug(fs_info, "invalid state (%d)", args->state);
			goto fail_unlock;
		}
		if (args->state != LS_OPEN && S_ISDIR(binode->vfs_inode.i_mode) &&
		    btrfs_ino(binode) != BTRFS_FIRST_FREE_OBJECTID) {
			btrfs_debug(fs_info, "invalid state (%d) for directory", args->state);
			goto fail_unlock;
		}
		if (root->locker_enabled && binode->locker_period_end < 0 &&
		    (!(args->flags & BTRFS_LOCKER_PERIOD_MASK) || args->period_end < 0)) {
			btrfs_debug(fs_info, "the end of lock period isn't set");
			goto fail_unlock;
		}
	}

	if (args->flags & BTRFS_LOCKER_UPDATE_TIME) {
		if (root->locker_enabled) {
			btrfs_debug(fs_info, "update-time is read-only");
			goto fail_unlock;
		}
	}

	if (args->flags & BTRFS_LOCKER_UPDATE_TIME_FLOOR) {
		btrfs_debug(fs_info, "update-time-floor is read-only");
		goto fail_unlock;
	}

	if (args->flags & BTRFS_LOCKER_BEGIN) {
		if (root->locker_enabled) {
			btrfs_debug(fs_info, "period_begin is read-only");
			goto fail_unlock;
		}
	}

	if (args->flags & BTRFS_LOCKER_END) {
		if (root->locker_enabled && args->period_end < 0) {
			btrfs_debug(fs_info, "invalid period_end (%lld)", args->period_end);
			goto fail_unlock;
		}
	}
	if (args->flags & BTRFS_LOCKER_END_EXT_BEGIN) {
		if (args->period_end < 0 || binode->locker_period_begin == LOCKER_DEFAULT_PERIOD_BEGIN) {
			btrfs_debug(fs_info, "invalid period_end (%lld) to extended from begin", args->period_end);
			goto fail_unlock;
		}
	}
	if (args->flags & BTRFS_LOCKER_END_EXT_END) {
		if (args->period_end < 0 || binode->locker_period_end == LOCKER_DEFAULT_PERIOD_END) {
			btrfs_debug(fs_info, "invalid period_end (%lld) to extended from end", args->period_end);
			goto fail_unlock;
		}
	}
	if (args->flags & BTRFS_LOCKER_END_EXT_CURRENT) {
		if (args->period_end < 0) {
			btrfs_debug(fs_info, "invalid period_end (%lld) to extended from current", args->period_end);
			goto fail_unlock;
		}
	}

	spin_unlock(&root->locker_lock);
	spin_unlock(&binode->locker_lock);

	return 0;

fail_unlock:
	spin_unlock(&root->locker_lock);
	spin_unlock(&binode->locker_lock);
	return -EINVAL;
}

static int syno_locker_root_set(struct btrfs_root *root, struct btrfs_ioctl_syno_locker_args *args)
{
	spin_lock(&root->locker_lock);

	if (args->flags & BTRFS_LOCKER_MODE)
		root->locker_mode = args->mode;
	if (args->flags & BTRFS_LOCKER_DEFAULT_STATE)
		root->locker_default_state = args->default_state;
	if (args->flags & BTRFS_LOCKER_WAITTIME)
		root->locker_waittime = args->waittime;
	if (args->flags & BTRFS_LOCKER_DURATION)
		root->locker_duration = args->duration;
	if (args->flags & BTRFS_LOCKER_CLOCK_ADJUSTMENT)
		root->locker_clock_adjustment = args->clock_adjustment;

	/*
	 * if we set up locker on a ro snapshot, we should guarantee its
	 * update_time_floor is not zero, because it's used as a frozen subvolume
	 * clock in some situations.
	 *
	 * normally it's assigned as the fs_clock at that time cloned from a r/w
	 * subvolume, but at that moment there may be no locker enabled in this
	 * volume. if so it will cause update_time_floor to be 0. we should assign
	 * a proper value to it.
	 */
	if (btrfs_root_readonly(root) && root->locker_update_time_floor == 0) {
		struct timespec64 fs_clock;

		fs_clock = btrfs_syno_locker_fs_clock_get(root->fs_info);
		root->locker_update_time_floor = fs_clock.tv_sec;
	}

	/* enabled at the last because it doesn't accept incorrect mode */
	if ((args->flags & BTRFS_LOCKER_ENABLED) && args->enabled)
		syno_locker_root_enable(root);

	spin_unlock(&root->locker_lock);

	return 0;
}

static int syno_locker_binode_set(struct btrfs_inode *binode, struct btrfs_ioctl_syno_locker_args *args)
{
	int ret = 0;

	spin_lock(&binode->locker_lock);
	if (args->flags & BTRFS_LOCKER_END)
		ret = syno_locker_inode_period_end_set(binode, args->period_end);
	else if (args->flags & BTRFS_LOCKER_END_EXT_BEGIN)
		ret = syno_locker_inode_period_end_set(binode, args->period_end + binode->locker_period_begin);
	else if (args->flags & BTRFS_LOCKER_END_EXT_END)
		ret = syno_locker_inode_period_end_set(binode, args->period_end + binode->locker_period_end);
	else if (args->flags & BTRFS_LOCKER_END_EXT_CURRENT) {
		struct timespec64 vol_clock = syno_locker_clock_get(binode);
		ret = syno_locker_inode_period_end_set(binode, args->period_end + vol_clock.tv_sec);
	}
	if (ret)
		goto out;

	if (args->flags & BTRFS_LOCKER_STATE) {
		ret = syno_locker_state_set(binode, (enum locker_state)args->state);
		if (ret)
			goto out;
	}

	/*
	 * update raw update_time and period_begin fields directly before locker
	 * is enabled. this is only for btrfs recv because there's no additional
	 * validation for these values.
	 */
	if (args->flags & BTRFS_LOCKER_UPDATE_TIME) {
		binode->__locker_update_time = args->update_time;
		binode->locker_dirty = true;
	}
	if (args->flags & BTRFS_LOCKER_BEGIN) {
		binode->__locker_period_begin = args->period_begin;
		binode->locker_dirty = true;
	}

out:
	spin_unlock(&binode->locker_lock);

	return ret;
}

int btrfs_xattr_syno_set_locker(struct inode *inode, const void *buffer, size_t size)
{
	int ret;
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_locker_xattr *xattr;
	struct btrfs_ioctl_syno_locker_args args = {};

	if (size != sizeof(struct btrfs_locker_xattr))
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	xattr = (struct btrfs_locker_xattr *)buffer;

	args.flags          = BTRFS_LOCKER_STATE|BTRFS_LOCKER_UPDATE_TIME|BTRFS_LOCKER_BEGIN|BTRFS_LOCKER_END;
	args.state          = xattr->state;
	args.update_time    = le64_to_cpu(xattr->update_time);
	args.period_begin   = le64_to_cpu(xattr->period_begin);
	args.period_end     = le64_to_cpu(xattr->period_end);

	ret = syno_locker_ioctl_args_validate(binode, &args);
	if (ret)
		goto out;

	syno_locker_fs_clock_init(binode->root->fs_info);

	ret = syno_locker_binode_set(binode, &args);
	if (ret)
		goto out;

	/* FIXME: it may be somewhat expansive to update each on-disk inode */
	ret = btrfs_syno_locker_disk_inode_update_trans(&binode->vfs_inode);
	if (ret)
		goto out;

out:
	if (ret)
		btrfs_err(binode->root->fs_info, "failed to set xattr (%u, %ptT, %ptT, %ptT). err=%d",
			args.state, &args.update_time, &args.period_begin, &args.period_end, ret);

	return ret;
}

int btrfs_ioctl_syno_locker_set(struct file *file, struct btrfs_ioctl_syno_locker_args __user *argp)
{
	int ret;
	char* pathname;
	struct inode *inode = file_inode(file);
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_root *root = binode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ioctl_syno_locker_args locker_args;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&locker_args, argp, sizeof(locker_args)))
		return -EFAULT;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	ret = syno_locker_ioctl_args_validate(binode, &locker_args);
	if (ret)
		goto out;

	syno_locker_fs_clock_init(root->fs_info);

	/* enable feature-tree for btrfs_root_locker_item */
	if (!btrfs_syno_check_feat_tree_enable(root->fs_info)) {
		mutex_lock(&fs_info->syno_feat_tree_ioctl_lock);
		ret = btrfs_syno_feat_tree_enable(fs_info);
		mutex_unlock(&fs_info->syno_feat_tree_ioctl_lock);
		if (ret)
			goto out;
	}

	if (locker_args.flags & BTRFS_LOCKER_ROOT_PROP_MASK) {
		ret = syno_locker_root_set(root, &locker_args);
		if (ret)
			goto out;
		ret = btrfs_syno_locker_disk_root_update_trans(root);
		if (ret)
			goto out;
	}

	if (locker_args.flags & BTRFS_LOCKER_INODE_PROP_MASK) {
		ret = syno_locker_binode_set(binode, &locker_args);
		if (ret)
			goto out;
		ret = btrfs_syno_locker_disk_inode_update_trans(inode);
		if (ret)
			goto out;
	}

	ret = 0;
out:
	mnt_drop_write_file(file);

	if (ret) {
		pathname = kstrdup_quotable_file(file, GFP_KERNEL);
		btrfs_debug(fs_info, "failed to set locker properties of '%s'. err=%d.", pathname, ret);
		kfree(pathname);
	}

	return ret;
}
