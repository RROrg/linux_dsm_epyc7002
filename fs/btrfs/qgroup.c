#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/btrfs.h>
#include <linux/sched/mm.h>
#ifdef MY_ABC_HERE
#include <net/netlink.h>
#include <net/genetlink.h>
#endif /* MY_ABC_HERE */

#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"
#include "locking.h"
#include "ulist.h"
#include "backref.h"
#include "extent_io.h"
#include "qgroup.h"
#include "block-group.h"
#include "sysfs.h"

/* TODO XXX FIXME
 *  - subvol delete -> delete when ref goes to 0? delete limits also?
 *  - reorganize keys
 *  - compressed
 *  - sync
 *  - copy also limits on subvol creation
 *  - limit
 *  - caches for ulists
 *  - performance benchmarks
 *  - check all ioctl parameters
 */

#ifdef MY_ABC_HERE
enum {
	SENT_UNDER = -1,
	SENT_NONE = 0,
	SENT_OVER = 1,
};

u64 qgroup_soft_limit = 0;

static const struct genl_multicast_group qgroup_mcgrps[] = {
	{ .name = "events", },
};

/* Netlink family structure for quota */
static struct genl_family btrfs_qgroup_genl_family __ro_after_init = {
	.module = THIS_MODULE,
	.hdrsize = 0,
	.name = "BTRFS_QUOTA",
	.version = 1,
	.maxattr = QGROUP_NL_A_MAX,
	.mcgrps = qgroup_mcgrps,
	.n_mcgrps = ARRAY_SIZE(qgroup_mcgrps),
};

#ifdef MY_ABC_HERE
static void prepare_netlink_notification(struct btrfs_qgroup *qg,
	u64 *soft_qgroup_subvol_id, u64 *soft_qgroup_limit, u64 *soft_qgroup_used,
	bool *over_limit)
{
	u64 soft_limit;

	if (!(qg->lim_flags & BTRFS_QGROUP_LIMIT_MAX_RFER) || !qgroup_soft_limit)
		return;

	soft_limit = div_u64(qg->max_rfer * qgroup_soft_limit, 100);
	// Should we send QGROUP_NL_C_OVER_LIMIT?
	if (qg->last_sent != SENT_OVER && qg->rfer > soft_limit) {
		qg->last_sent = SENT_OVER;
		*over_limit = true;
		goto notify;
	}

	// Should we send QGROUP_NL_C_UNDER_LIMIT?
	if (qg->last_sent != SENT_UNDER) {
		if (soft_limit <= SZ_1M * 100)
			return;
		if (qg->rfer >= div_u64(qg->max_rfer * (qgroup_soft_limit - 1), 100))
			return;
		if (qg->rfer >= soft_limit - (SZ_1M * 100))
			return;

		qg->last_sent = SENT_UNDER;
		*over_limit = false;
		goto notify;
	}

	return;
notify:
	*soft_qgroup_subvol_id = qg->qgroupid;
	*soft_qgroup_limit = qg->max_rfer;
	*soft_qgroup_used = qg->rfer;
}

static void send_netlink_notification(struct btrfs_fs_info *fs_info, u64 qgroupid,
		u64 quota_limit, u64 quota_used, int type)
{
	static atomic_t seq = ATOMIC_INIT(0);
	struct sk_buff *skb;
	void *msg_head;
	int ret;
	int msg_size = nla_total_size(BTRFS_FSID_SIZE) + (3 * nla_total_size_64bit(sizeof(u64)));

	/* We have to allocate using GFP_NOFS as we are called from a
	 * filesystem performing write and thus further recursion into
	 * the fs to free some data could cause deadlocks. */
	skb = genlmsg_new(msg_size, GFP_NOFS);
	if (!skb) {
		btrfs_warn(fs_info, "Not enough memory to send qgroup warning.\n");
		return;
	}
	msg_head = genlmsg_put(skb, 0, atomic_add_return(1, &seq),
			&btrfs_qgroup_genl_family, 0, type);
	if (!msg_head) {
		btrfs_warn(fs_info, "Cannot store netlink header in qgroup warning.\n");
		goto err_out;
	}
	ret = nla_put(skb, QGROUP_NL_A_FSID, BTRFS_FSID_SIZE, fs_info->super_copy->fsid);
	if (ret)
		goto attr_err_out;
	ret = nla_put_u64_64bit(skb, QGROUP_NL_A_SUBVOL_ID, qgroupid, QUOTA_NL_A_PAD);
	if (ret)
		goto attr_err_out;
	ret = nla_put_u64_64bit(skb, QGROUP_NL_A_QUOTA_LIMIT, quota_limit, QUOTA_NL_A_PAD);
	if (ret)
		goto attr_err_out;
	ret = nla_put_u64_64bit(skb, QGROUP_NL_A_QUOTA_USED, quota_used, QUOTA_NL_A_PAD);
	if (ret)
		goto attr_err_out;
	genlmsg_end(skb, msg_head);

	genlmsg_multicast(&btrfs_qgroup_genl_family, skb, 0, 0, GFP_NOFS);
	return;

attr_err_out:
	btrfs_warn(fs_info, "Not enough space to compose qgroup netlink message!\n");
err_out:
	kfree_skb(skb);
}
#endif /* MY_ABC_HERE */

int __init qgroup_netlink_init(void)
{
	if (genl_register_family(&btrfs_qgroup_genl_family) != 0)
		printk(KERN_ERR
		       "Failed to create btrfs qgroup netlink interface.\n");
	return 0;
};

void qgroup_netlink_exit(void)
{
	genl_unregister_family(&btrfs_qgroup_genl_family);
};
#endif /* MY_ABC_HERE */

/*
 * Helpers to access qgroup reservation
 *
 * Callers should ensure the lock context and type are valid
 */

static u64 qgroup_rsv_total(const struct btrfs_qgroup *qgroup)
{
	u64 ret = 0;
	int i;

	for (i = 0; i < BTRFS_QGROUP_RSV_LAST; i++)
		ret += qgroup->rsv.values[i];

	return ret;
}

#ifdef CONFIG_BTRFS_DEBUG
static const char *qgroup_rsv_type_str(enum btrfs_qgroup_rsv_type type)
{
	if (type == BTRFS_QGROUP_RSV_DATA)
		return "data";
	if (type == BTRFS_QGROUP_RSV_META_PERTRANS)
		return "meta_pertrans";
	if (type == BTRFS_QGROUP_RSV_META_PREALLOC)
		return "meta_prealloc";
	return NULL;
}
#endif

static void qgroup_rsv_add(struct btrfs_fs_info *fs_info,
			   struct btrfs_qgroup *qgroup, u64 num_bytes,
			   enum btrfs_qgroup_rsv_type type)
{
	trace_qgroup_update_reserve(fs_info, qgroup, num_bytes, type);
	qgroup->rsv.values[type] += num_bytes;
}

static void qgroup_rsv_release(struct btrfs_fs_info *fs_info,
			       struct btrfs_qgroup *qgroup, u64 num_bytes,
			       enum btrfs_qgroup_rsv_type type)
{
	trace_qgroup_update_reserve(fs_info, qgroup, -(s64)num_bytes, type);
	if (qgroup->rsv.values[type] >= num_bytes) {
		qgroup->rsv.values[type] -= num_bytes;
		return;
	}
#ifdef MY_ABC_HERE
	WARN_ONCE(1, "qgroup %llu reserved space underflow, have %llu to free %llu",
		qgroup->qgroupid,
		qgroup->rsv.values[type], num_bytes);
#endif /* MY_ABC_HERE */
#ifdef CONFIG_BTRFS_DEBUG
	WARN_RATELIMIT(1,
		"qgroup %llu %s reserved space underflow, have %llu to free %llu",
		qgroup->qgroupid, qgroup_rsv_type_str(type),
		qgroup->rsv.values[type], num_bytes);
#endif
	qgroup->rsv.values[type] = 0;
}

#ifdef MY_ABC_HERE
#else
static void qgroup_rsv_add_by_qgroup(struct btrfs_fs_info *fs_info,
				     struct btrfs_qgroup *dest,
				     struct btrfs_qgroup *src)
{
	int i;

	for (i = 0; i < BTRFS_QGROUP_RSV_LAST; i++)
		qgroup_rsv_add(fs_info, dest, src->rsv.values[i], i);
}

static void qgroup_rsv_release_by_qgroup(struct btrfs_fs_info *fs_info,
					 struct btrfs_qgroup *dest,
					  struct btrfs_qgroup *src)
{
	int i;

	for (i = 0; i < BTRFS_QGROUP_RSV_LAST; i++)
		qgroup_rsv_release(fs_info, dest, src->rsv.values[i], i);
}

static void btrfs_qgroup_update_old_refcnt(struct btrfs_qgroup *qg, u64 seq,
					   int mod)
{
	if (qg->old_refcnt < seq)
		qg->old_refcnt = seq;
	qg->old_refcnt += mod;
}

static void btrfs_qgroup_update_new_refcnt(struct btrfs_qgroup *qg, u64 seq,
					   int mod)
{
	if (qg->new_refcnt < seq)
		qg->new_refcnt = seq;
	qg->new_refcnt += mod;
}

static inline u64 btrfs_qgroup_get_old_refcnt(struct btrfs_qgroup *qg, u64 seq)
{
	if (qg->old_refcnt < seq)
		return 0;
	return qg->old_refcnt - seq;
}

static inline u64 btrfs_qgroup_get_new_refcnt(struct btrfs_qgroup *qg, u64 seq)
{
	if (qg->new_refcnt < seq)
		return 0;
	return qg->new_refcnt - seq;
}
#endif /* MY_ABC_HERE */

/*
 * glue structure to represent the relations between qgroups.
 */
struct btrfs_qgroup_list {
	struct list_head next_group;
	struct list_head next_member;
	struct btrfs_qgroup *group;
	struct btrfs_qgroup *member;
};

static inline u64 qgroup_to_aux(struct btrfs_qgroup *qg)
{
	return (u64)(uintptr_t)qg;
}

static inline struct btrfs_qgroup* unode_aux_to_qgroup(struct ulist_node *n)
{
	return (struct btrfs_qgroup *)(uintptr_t)n->aux;
}

static int
qgroup_rescan_init(struct btrfs_fs_info *fs_info, u64 progress_objectid,
		   int init_flags);
static void qgroup_rescan_zero_tracking(struct btrfs_fs_info *fs_info);

/* must be called with qgroup_ioctl_lock held */
static struct btrfs_qgroup *find_qgroup_rb(struct btrfs_fs_info *fs_info,
					   u64 qgroupid)
{
	struct rb_node *n = fs_info->qgroup_tree.rb_node;
	struct btrfs_qgroup *qgroup;

	while (n) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		if (qgroup->qgroupid < qgroupid)
			n = n->rb_left;
		else if (qgroup->qgroupid > qgroupid)
			n = n->rb_right;
		else
			return qgroup;
	}
	return NULL;
}

/* must be called with qgroup_lock held */
static struct btrfs_qgroup *add_qgroup_rb(struct btrfs_fs_info *fs_info,
					  u64 qgroupid)
{
	struct rb_node **p = &fs_info->qgroup_tree.rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_qgroup *qgroup;

	while (*p) {
		parent = *p;
		qgroup = rb_entry(parent, struct btrfs_qgroup, node);

		if (qgroup->qgroupid < qgroupid)
			p = &(*p)->rb_left;
		else if (qgroup->qgroupid > qgroupid)
			p = &(*p)->rb_right;
		else
			return qgroup;
	}

	qgroup = kzalloc(sizeof(*qgroup), GFP_ATOMIC);
	if (!qgroup)
		return ERR_PTR(-ENOMEM);

	qgroup->qgroupid = qgroupid;
	INIT_LIST_HEAD(&qgroup->groups);
	INIT_LIST_HEAD(&qgroup->members);
	INIT_LIST_HEAD(&qgroup->dirty);

	rb_link_node(&qgroup->node, parent, p);
	rb_insert_color(&qgroup->node, &fs_info->qgroup_tree);

	return qgroup;
}

static void __del_qgroup_rb(struct btrfs_fs_info *fs_info,
			    struct btrfs_qgroup *qgroup)
{
	struct btrfs_qgroup_list *list;

	list_del(&qgroup->dirty);
	while (!list_empty(&qgroup->groups)) {
		list = list_first_entry(&qgroup->groups,
					struct btrfs_qgroup_list, next_group);
		list_del(&list->next_group);
		list_del(&list->next_member);
		kfree(list);
	}

	while (!list_empty(&qgroup->members)) {
		list = list_first_entry(&qgroup->members,
					struct btrfs_qgroup_list, next_member);
		list_del(&list->next_group);
		list_del(&list->next_member);
		kfree(list);
	}
}

/* must be called with qgroup_lock held */
static int del_qgroup_rb(struct btrfs_fs_info *fs_info, u64 qgroupid)
{
	struct btrfs_qgroup *qgroup = find_qgroup_rb(fs_info, qgroupid);

	if (!qgroup)
		return -ENOENT;

	rb_erase(&qgroup->node, &fs_info->qgroup_tree);
	__del_qgroup_rb(fs_info, qgroup);
	return 0;
}

/* must be called with qgroup_lock held */
static int add_relation_rb(struct btrfs_fs_info *fs_info,
			   u64 memberid, u64 parentid)
{
	struct btrfs_qgroup *member;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup_list *list;

	member = find_qgroup_rb(fs_info, memberid);
	parent = find_qgroup_rb(fs_info, parentid);
	if (!member || !parent)
		return -ENOENT;

	list = kzalloc(sizeof(*list), GFP_ATOMIC);
	if (!list)
		return -ENOMEM;

	list->group = parent;
	list->member = member;
	list_add_tail(&list->next_group, &member->groups);
	list_add_tail(&list->next_member, &parent->members);

	return 0;
}

/* must be called with qgroup_lock held */
static int del_relation_rb(struct btrfs_fs_info *fs_info,
			   u64 memberid, u64 parentid)
{
	struct btrfs_qgroup *member;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup_list *list;

	member = find_qgroup_rb(fs_info, memberid);
	parent = find_qgroup_rb(fs_info, parentid);
	if (!member || !parent)
		return -ENOENT;

	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent) {
			list_del(&list->next_group);
			list_del(&list->next_member);
			kfree(list);
			return 0;
		}
	}
	return -ENOENT;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
int btrfs_verify_qgroup_counts(struct btrfs_fs_info *fs_info, u64 qgroupid,
			       u64 rfer, u64 excl)
{
	struct btrfs_qgroup *qgroup;

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup)
		return -EINVAL;
	if (qgroup->rfer != rfer || qgroup->excl != excl)
		return -EINVAL;
	return 0;
}
#endif

#ifdef MY_ABC_HERE
static void update_syno_quota_rescan_progress(struct btrfs_root *quota_root,
        struct syno_quota_rescan_ctx *ctx, u64 subvol_id,
        enum syno_quota_rescan_progress_update_type type);
#endif /* MY_ABC_HERE */

/*
 * The full config is read in one go, only called from open_ctree()
 * It doesn't use any locking, as at this point we're still single-threaded
 */
int btrfs_read_qgroup_config(struct btrfs_fs_info *fs_info)
{
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_path *path = NULL;
	struct extent_buffer *l;
	int slot;
	int ret = 0;
	u64 flags = 0;
	u64 rescan_progress = 0;
#ifdef MY_ABC_HERE
	u64 subvol_id;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags) &&
		!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags))
#else
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
#endif /* MY_ABC_HERE */
		return 0;

	fs_info->qgroup_ulist = ulist_alloc(GFP_KERNEL);
	if (!fs_info->qgroup_ulist) {
		ret = -ENOMEM;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
#ifdef MY_ABC_HERE
	path->reada = READA_FORWARD_ALWAYS;
#endif /* MY_ABC_HERE */

	ret = btrfs_sysfs_add_qgroups(fs_info);
	if (ret < 0)
		goto out;
	/* default this to quota off, in case no status key is found */
	fs_info->qgroup_flags = 0;

	/*
	 * pass 1: read status, all qgroup infos and limits
	 */
	key.objectid = 0;
	key.type = 0;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(quota_root, &key, path, 1, 1);
	if (ret)
		goto out;

	while (1) {
		struct btrfs_qgroup *qgroup;

		slot = path->slots[0];
		l = path->nodes[0];
		btrfs_item_key_to_cpu(l, &found_key, slot);

		if (found_key.type == BTRFS_QGROUP_STATUS_KEY) {
			struct btrfs_qgroup_status_item *ptr;

			ptr = btrfs_item_ptr(l, slot,
					     struct btrfs_qgroup_status_item);

#ifdef MY_ABC_HERE
			if (test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags) &&
			    btrfs_qgroup_status_version(l, ptr) !=
			    BTRFS_QGROUP_V2_STATUS_VERSION) {
				btrfs_err(fs_info,
					"syno quota v2 found bad %llu version, quota disabled",
					btrfs_qgroup_status_version(l, ptr));
				goto out;
			}
			if (test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags) &&
			    btrfs_qgroup_status_version(l, ptr) !=
			    BTRFS_QGROUP_STATUS_VERSION) {
				btrfs_err(fs_info,
					"syno quota v1 found bad %llu version, quota disabled",
					btrfs_qgroup_status_version(l, ptr));
				goto out;
			}
#else
			if (btrfs_qgroup_status_version(l, ptr) !=
			    BTRFS_QGROUP_STATUS_VERSION) {
				btrfs_err(fs_info,
				 "old qgroup version, quota disabled");
				goto out;
			}
#endif /* MY_ABC_HERE */
			if (btrfs_qgroup_status_generation(l, ptr) !=
			    fs_info->generation) {
				flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
				btrfs_err(fs_info,
					"qgroup generation mismatch, marked as inconsistent");
			}
			fs_info->qgroup_flags = btrfs_qgroup_status_flags(l,
									  ptr);
			rescan_progress = btrfs_qgroup_status_rescan(l, ptr);
			goto next1;
		}

		if (found_key.type != BTRFS_QGROUP_INFO_KEY &&
		    found_key.type != BTRFS_QGROUP_LIMIT_KEY)
			goto next1;

		qgroup = find_qgroup_rb(fs_info, found_key.offset);
		if ((qgroup && found_key.type == BTRFS_QGROUP_INFO_KEY) ||
		    (!qgroup && found_key.type == BTRFS_QGROUP_LIMIT_KEY)) {
			btrfs_err(fs_info, "inconsistent qgroup config");
			flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		}
		if (!qgroup) {
			qgroup = add_qgroup_rb(fs_info, found_key.offset);
			if (IS_ERR(qgroup)) {
				ret = PTR_ERR(qgroup);
				goto out;
			}
		}
		ret = btrfs_sysfs_add_one_qgroup(fs_info, qgroup);
		if (ret < 0)
			goto out;

		switch (found_key.type) {
		case BTRFS_QGROUP_INFO_KEY: {
			struct btrfs_qgroup_info_item *ptr;

			ptr = btrfs_item_ptr(l, slot,
					     struct btrfs_qgroup_info_item);
			qgroup->rfer = btrfs_qgroup_info_rfer(l, ptr);
			qgroup->rfer_cmpr = btrfs_qgroup_info_rfer_cmpr(l, ptr);
			qgroup->excl = btrfs_qgroup_info_excl(l, ptr);
			qgroup->excl_cmpr = btrfs_qgroup_info_excl_cmpr(l, ptr);
			/* generation currently unused */
			break;
		}
		case BTRFS_QGROUP_LIMIT_KEY: {
			struct btrfs_qgroup_limit_item *ptr;

			ptr = btrfs_item_ptr(l, slot,
					     struct btrfs_qgroup_limit_item);
			qgroup->lim_flags = btrfs_qgroup_limit_flags(l, ptr);
			qgroup->max_rfer = btrfs_qgroup_limit_max_rfer(l, ptr);
			qgroup->max_excl = btrfs_qgroup_limit_max_excl(l, ptr);
			qgroup->rsv_rfer = btrfs_qgroup_limit_rsv_rfer(l, ptr);
			qgroup->rsv_excl = btrfs_qgroup_limit_rsv_excl(l, ptr);
			break;
		}
		}
next1:
		ret = btrfs_next_item(quota_root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;
	}
	btrfs_release_path(path);

#ifdef MY_ABC_HERE
	// Setup rescan before pass 2, since we may goto out in pass 2 and miss the rescan setup.
	subvol_id = rescan_progress;

	// No need to rescan. Go to pass 2.
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags) || !subvol_id)
		goto pass2;

	fs_info->syno_quota_rescan_ctx = kzalloc(sizeof(struct syno_quota_rescan_ctx), GFP_KERNEL);
	if (!fs_info->syno_quota_rescan_ctx) {
		btrfs_warn(fs_info, "Failed to alloc syno_quota_rescan_ctx");
		ret = -ENOMEM;
		goto out;
	}

	fs_info->syno_quota_rescan_subvol_ulist = ulist_alloc(GFP_KERNEL);
	if (!fs_info->syno_quota_rescan_subvol_ulist) {
		btrfs_warn(fs_info, "Failed to alloc syno_quota_rescan_subvol_ulist");
		ret = -ENOMEM;
		goto out;
	}

	// Read rescan subvol list.
	while (subvol_id) {
		struct btrfs_syno_quota_rescan_item *ptr;
		struct syno_quota_rescan_ctx *ctx = fs_info->syno_quota_rescan_ctx;
		u64 flags;
		u64 ino;

		key.objectid = 0;
		key.type = BTRFS_SYNO_QUOTA_RESCAN_KEY;
		key.offset = subvol_id;

		ret = btrfs_search_slot(NULL, quota_root, &key, path, 0, 0);
		if (ret) {
			btrfs_warn(fs_info,
				"Failed to read syno quota rescan item, root = %llu", subvol_id);
			if (ret > 0)
				ret = -ENOENT;
			break;
		}

		slot = path->slots[0];
		l = path->nodes[0];
		ptr = btrfs_item_ptr(l, slot, struct btrfs_syno_quota_rescan_item);

		flags = btrfs_syno_quota_rescan_flags(l, ptr);
		if (!(flags & (SYNO_QUOTA_RESCAN_QUEUED | SYNO_QUOTA_RESCAN_DOING))) {
			update_syno_quota_rescan_progress(fs_info->quota_root, ctx,
				subvol_id, SYNO_QUOTA_PROGRESS_ADD_FINISHED);
			subvol_id = btrfs_syno_quota_rescan_next_root(l, ptr);
			btrfs_release_path(path);
			continue;
		}

		ino = btrfs_syno_quota_rescan_inode(l, ptr);
		ret = ulist_add(fs_info->syno_quota_rescan_subvol_ulist, subvol_id, ino, GFP_KERNEL);
		if (ret != 1) {
			if (ret == 0) {
				btrfs_warn(fs_info, "Syno quota rescan detect duplicate items, the list is broken.");
				ret = -EEXIST;
			} else {
				btrfs_warn(fs_info, "Syno quota rescan encounter -ENOMEM.");
				ret = -ENOMEM;
			}
			break;
		}
		update_syno_quota_rescan_progress(fs_info->quota_root, ctx,
			subvol_id, SYNO_QUOTA_PROGRESS_ADD_NEW);

		subvol_id = btrfs_syno_quota_rescan_next_root(l, ptr);
		btrfs_release_path(path);
	}
	btrfs_release_path(path);
pass2:
#endif /* MY_ABC_HERE */

	/*
	 * pass 2: read all qgroup relations
	 */
	key.objectid = 0;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(quota_root, &key, path, 1, 0);
	if (ret)
		goto out;
	while (1) {
		slot = path->slots[0];
		l = path->nodes[0];
		btrfs_item_key_to_cpu(l, &found_key, slot);

		if (found_key.type != BTRFS_QGROUP_RELATION_KEY)
			goto next2;

		if (found_key.objectid > found_key.offset) {
			/* parent <- member, not needed to build config */
			/* FIXME should we omit the key completely? */
			goto next2;
		}

		ret = add_relation_rb(fs_info, found_key.objectid,
				      found_key.offset);
		if (ret == -ENOENT) {
			btrfs_warn(fs_info,
				"orphan qgroup relation 0x%llx->0x%llx",
				found_key.objectid, found_key.offset);
			ret = 0;	/* ignore the error */
		}
		if (ret)
			goto out;
next2:
		ret = btrfs_next_item(quota_root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;
	}

out:
	btrfs_free_path(path);
	fs_info->qgroup_flags |= flags;
	if (!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_ON))
#ifdef MY_ABC_HERE
	{
		clear_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags);
		clear_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags);
	}
#else
		clear_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
#endif /* MY_ABC_HERE */
	else if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN &&
		 ret >= 0)
		ret = qgroup_rescan_init(fs_info, rescan_progress, 0);

	if (ret < 0) {
		ulist_free(fs_info->qgroup_ulist);
		fs_info->qgroup_ulist = NULL;
#ifdef MY_ABC_HERE
		ulist_free(fs_info->syno_quota_rescan_subvol_ulist);
		fs_info->syno_quota_rescan_subvol_ulist = NULL;
		kfree(fs_info->syno_quota_rescan_ctx);
		fs_info->syno_quota_rescan_ctx = NULL;
#endif /* MY_ABC_HERE */
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
		btrfs_sysfs_del_qgroups(fs_info);
	}

	return ret < 0 ? ret : 0;
}

/*
 * Called in close_ctree() when quota is still enabled.  This verifies we don't
 * leak some reserved space.
 *
 * Return false if no reserved space is left.
 * Return true if some reserved space is leaked.
 */
bool btrfs_check_quota_leak(struct btrfs_fs_info *fs_info)
{
	struct rb_node *node;
	bool ret = false;

#ifdef MY_ABC_HERE
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags) &&
		!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags))
#else
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
#endif /* MY_ABC_HERE */
		return ret;
	/*
	 * Since we're unmounting, there is no race and no need to grab qgroup
	 * lock.  And here we don't go post-order to provide a more user
	 * friendly sorted result.
	 */
	for (node = rb_first(&fs_info->qgroup_tree); node; node = rb_next(node)) {
		struct btrfs_qgroup *qgroup;
		int i;

		qgroup = rb_entry(node, struct btrfs_qgroup, node);
		for (i = 0; i < BTRFS_QGROUP_RSV_LAST; i++) {
			if (qgroup->rsv.values[i]) {
				ret = true;
				btrfs_warn(fs_info,
		"qgroup %hu/%llu has unreleased space, type %d rsv %llu",
				   btrfs_qgroup_level(qgroup->qgroupid),
				   btrfs_qgroup_subvolid(qgroup->qgroupid),
				   i, qgroup->rsv.values[i]);
			}
		}
	}
	return ret;
}

/*
 * This is called from close_ctree() or open_ctree() or btrfs_quota_disable(),
 * first two are in single-threaded paths.And for the third one, we have set
 * quota_root to be null with qgroup_lock held before, so it is safe to clean
 * up the in-memory structures without qgroup_lock held.
 */
void btrfs_free_qgroup_config(struct btrfs_fs_info *fs_info)
{
	struct rb_node *n;
	struct btrfs_qgroup *qgroup;

	while ((n = rb_first(&fs_info->qgroup_tree))) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		rb_erase(n, &fs_info->qgroup_tree);
		__del_qgroup_rb(fs_info, qgroup);
		btrfs_sysfs_del_one_qgroup(fs_info, qgroup);
		kfree(qgroup);
	}
	/*
	 * We call btrfs_free_qgroup_config() when unmounting
	 * filesystem and disabling quota, so we set qgroup_ulist
	 * to be null here to avoid double free.
	 */
	ulist_free(fs_info->qgroup_ulist);
	fs_info->qgroup_ulist = NULL;
#ifdef MY_ABC_HERE
	ulist_free(fs_info->syno_quota_rescan_subvol_ulist);
	fs_info->syno_quota_rescan_subvol_ulist = NULL;
	kfree(fs_info->syno_quota_rescan_ctx);
	fs_info->syno_quota_rescan_ctx = NULL;
#endif /* MY_ABC_HERE */
	btrfs_sysfs_del_qgroups(fs_info);
}

static int add_qgroup_relation_item(struct btrfs_trans_handle *trans, u64 src,
				    u64 dst)
{
	int ret;
	struct btrfs_root *quota_root = trans->fs_info->quota_root;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = src;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = dst;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key, 0);

	btrfs_mark_buffer_dirty(path->nodes[0]);

	btrfs_free_path(path);
	return ret;
}

static int del_qgroup_relation_item(struct btrfs_trans_handle *trans, u64 src,
				    u64 dst)
{
	int ret;
	struct btrfs_root *quota_root = trans->fs_info->quota_root;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = src;
	key.type = BTRFS_QGROUP_RELATION_KEY;
	key.offset = dst;

	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, quota_root, path);
out:
	btrfs_free_path(path);
	return ret;
}

static int add_qgroup_item(struct btrfs_trans_handle *trans,
			   struct btrfs_root *quota_root, u64 qgroupid)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_qgroup_info_item *qgroup_info;
	struct btrfs_qgroup_limit_item *qgroup_limit;
	struct extent_buffer *leaf;
	struct btrfs_key key;

	if (btrfs_is_testing(quota_root->fs_info))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroupid;

	/*
	 * Avoid a transaction abort by catching -EEXIST here. In that
	 * case, we proceed by re-initializing the existing structure
	 * on disk.
	 */

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*qgroup_info));
	if (ret && ret != -EEXIST)
		goto out;

	leaf = path->nodes[0];
	qgroup_info = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_qgroup_info_item);
	btrfs_set_qgroup_info_generation(leaf, qgroup_info, trans->transid);
	btrfs_set_qgroup_info_rfer(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_rfer_cmpr(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_excl(leaf, qgroup_info, 0);
	btrfs_set_qgroup_info_excl_cmpr(leaf, qgroup_info, 0);

	btrfs_mark_buffer_dirty(leaf);

	btrfs_release_path(path);

	key.type = BTRFS_QGROUP_LIMIT_KEY;
	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*qgroup_limit));
	if (ret && ret != -EEXIST)
		goto out;

	leaf = path->nodes[0];
	qgroup_limit = btrfs_item_ptr(leaf, path->slots[0],
				  struct btrfs_qgroup_limit_item);
	btrfs_set_qgroup_limit_flags(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_max_rfer(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_max_excl(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_rsv_rfer(leaf, qgroup_limit, 0);
	btrfs_set_qgroup_limit_rsv_excl(leaf, qgroup_limit, 0);

	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int del_qgroup_item(struct btrfs_trans_handle *trans, u64 qgroupid)
{
	int ret;
	struct btrfs_root *quota_root = trans->fs_info->quota_root;
	struct btrfs_path *path;
	struct btrfs_key key;
#ifdef MY_ABC_HERE
	struct btrfs_fs_info *fs_info = trans->fs_info;
#endif /* MY_ABC_HERE */

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroupid;
	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, quota_root, path);
	if (ret)
		goto out;

	btrfs_release_path(path);

	key.type = BTRFS_QGROUP_LIMIT_KEY;
	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, quota_root, path);

#ifdef MY_ABC_HERE
	btrfs_release_path(path);

	// Remove rescan item.
	mutex_lock(&fs_info->qgroup_rescan_lock);
	key.type = BTRFS_SYNO_QUOTA_RESCAN_KEY;
	ret = btrfs_search_slot(trans, quota_root, &key, path, -1, 1);
	if (ret) {
		if (ret > 0)
			ret = 0; // This subvol may be from quota 1.0 or vanilla kernel.
		goto unlock_rescan_lock;
	}

	ret = btrfs_del_item(trans, quota_root, path);

unlock_rescan_lock:
	mutex_unlock(&fs_info->qgroup_rescan_lock);
#endif /* MY_ABC_HERE */
out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_limit_item(struct btrfs_trans_handle *trans,
				    struct btrfs_qgroup *qgroup)
{
	struct btrfs_root *quota_root = trans->fs_info->quota_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_limit_item *qgroup_limit;
	int ret;
	int slot;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_LIMIT_KEY;
	key.offset = qgroup->qgroupid;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, quota_root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	qgroup_limit = btrfs_item_ptr(l, slot, struct btrfs_qgroup_limit_item);
	btrfs_set_qgroup_limit_flags(l, qgroup_limit, qgroup->lim_flags);
	btrfs_set_qgroup_limit_max_rfer(l, qgroup_limit, qgroup->max_rfer);
	btrfs_set_qgroup_limit_max_excl(l, qgroup_limit, qgroup->max_excl);
	btrfs_set_qgroup_limit_rsv_rfer(l, qgroup_limit, qgroup->rsv_rfer);
	btrfs_set_qgroup_limit_rsv_excl(l, qgroup_limit, qgroup->rsv_excl);

	btrfs_mark_buffer_dirty(l);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_info_item(struct btrfs_trans_handle *trans,
				   struct btrfs_qgroup *qgroup)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_info_item *qgroup_info;
	int ret;
	int slot;

	if (btrfs_is_testing(fs_info))
		return 0;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_INFO_KEY;
	key.offset = qgroup->qgroupid;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, quota_root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	qgroup_info = btrfs_item_ptr(l, slot, struct btrfs_qgroup_info_item);
	btrfs_set_qgroup_info_generation(l, qgroup_info, trans->transid);
	btrfs_set_qgroup_info_rfer(l, qgroup_info, qgroup->rfer);
	btrfs_set_qgroup_info_rfer_cmpr(l, qgroup_info, qgroup->rfer_cmpr);
	btrfs_set_qgroup_info_excl(l, qgroup_info, qgroup->excl);
	btrfs_set_qgroup_info_excl_cmpr(l, qgroup_info, qgroup->excl_cmpr);

	btrfs_mark_buffer_dirty(l);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_qgroup_status_item(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *l;
	struct btrfs_qgroup_status_item *ptr;
	int ret;
	int slot;

	key.objectid = 0;
	key.type = BTRFS_QGROUP_STATUS_KEY;
	key.offset = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, quota_root, &key, path, 0, 1);
	if (ret > 0)
		ret = -ENOENT;

	if (ret)
		goto out;

	l = path->nodes[0];
	slot = path->slots[0];
	ptr = btrfs_item_ptr(l, slot, struct btrfs_qgroup_status_item);
	btrfs_set_qgroup_status_flags(l, ptr, fs_info->qgroup_flags);
	btrfs_set_qgroup_status_generation(l, ptr, trans->transid);
	btrfs_set_qgroup_status_rescan(l, ptr,
				fs_info->qgroup_rescan_progress.objectid);

	btrfs_mark_buffer_dirty(l);

out:
	btrfs_free_path(path);
	return ret;
}

#ifdef MY_ABC_HERE
int btrfs_read_syno_quota_rescan_item(struct btrfs_root *quota_root, u64 subvol_id,
		struct btrfs_syno_quota_rescan_item *rescan_item)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	if (unlikely(!quota_root))
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_SYNO_QUOTA_RESCAN_KEY;
	key.offset = subvol_id;

	ret = btrfs_search_slot(NULL, quota_root, &key, path, 0, 0);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto out;
	}

	read_extent_buffer(path->nodes[0], rescan_item,
		btrfs_item_ptr_offset(path->nodes[0], path->slots[0]), sizeof(*rescan_item));

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

// Will read tree size from btree rescan item, so rescan item must be updated before us.
static void update_syno_quota_rescan_progress(struct btrfs_root *quota_root,
	struct syno_quota_rescan_ctx *ctx, u64 subvol_id,
	enum syno_quota_rescan_progress_update_type type)
{
	struct btrfs_syno_quota_rescan_item rescan_item;
	int ret;

	ret = btrfs_read_syno_quota_rescan_item(quota_root, subvol_id, &rescan_item);
	if (ret) {
		btrfs_warn(quota_root->fs_info, "Failed to read syno quota rescan item, id = %llu, ret = %d",
			subvol_id, ret);
		return;
	}

	switch (type) {
	case SYNO_QUOTA_PROGRESS_REMOVE_SCANNING: // Same as SYNO_QUOTA_PROGRESS_FINISH_ONE.
		ctx->subvol_id = 0;
		ctx->total_finished_size += rescan_item.tree_size;
		memset(ctx->current_path, 0, sizeof(ctx->current_path));
		break;
	case SYNO_QUOTA_PROGRESS_REMOVE_QUEUED:
		if (ctx->total_size >= rescan_item.tree_size)
			ctx->total_size -= rescan_item.tree_size;
		else
			WARN_ON_ONCE(1);
		break;
	case SYNO_QUOTA_PROGRESS_REMOVE_FINISHED:
		break;
	case SYNO_QUOTA_PROGRESS_ADD_NEW:
		ctx->total_size += rescan_item.tree_size;
		break;
	case SYNO_QUOTA_PROGRESS_ADD_FINISHED:
		ctx->total_size += rescan_item.tree_size;
		ctx->total_finished_size += rescan_item.tree_size;
		break;
	case SYNO_QUOTA_PROGRESS_FINISH_ONE: // Same as SYNO_QUOTA_PROGRESS_REMOVE_SCANNING.
		ctx->subvol_id = 0;
		ctx->total_finished_size += rescan_item.tree_size;
		memset(ctx->current_path, 0, sizeof(ctx->current_path));
		break;
	case SYNO_QUOTA_PROGRESS_FINISH_ALL:
		WARN_ON_ONCE(ctx->total_finished_size + rescan_item.tree_size != ctx->total_size);

		// Reset progress.
		memset(ctx, 0, sizeof(*ctx));
		break;
	default:
		WARN_ON_ONCE(1);
	}
}

// Cannot used on new empty rescan item.
static void update_syno_quota_rescan_flags(struct extent_buffer *leaf,
			struct btrfs_syno_quota_rescan_item *rescan_item, u64 flags)
{
	u64 orig_flags = btrfs_syno_quota_rescan_flags(leaf, rescan_item);

	switch (flags) {
	case SYNO_QUOTA_RESCAN_DONE: {
		// If error was found after we ran the scan, we should trigger another new scan to fix.
		orig_flags &= (SYNO_QUOTA_RESCAN_ERR | SYNO_QUOTA_RESCAN_NEED);
		btrfs_set_syno_quota_rescan_flags(leaf, rescan_item,
							orig_flags | SYNO_QUOTA_RESCAN_DONE);
		break;
	}
	case SYNO_QUOTA_RESCAN_QUEUED: {
		if (orig_flags & SYNO_QUOTA_RESCAN_DOING) {
			WARN_ON_ONCE(1);
			break;
		}
		// Start a new scan, clear all errors.
		btrfs_set_syno_quota_rescan_flags(leaf, rescan_item, SYNO_QUOTA_RESCAN_QUEUED);
		break;
	}
	case SYNO_QUOTA_RESCAN_DOING: {
		if (orig_flags & SYNO_QUOTA_RESCAN_DONE) {
			WARN_ON_ONCE(1);
			break;
		}
		orig_flags &= ~SYNO_QUOTA_RESCAN_QUEUED;
		btrfs_set_syno_quota_rescan_flags(leaf, rescan_item,
							orig_flags | SYNO_QUOTA_RESCAN_DOING);
		break;
	}
	case SYNO_QUOTA_RESCAN_ERR: {
		if (orig_flags & SYNO_QUOTA_RESCAN_DONE) {
			WARN_ON_ONCE(1);
			break;
		}
		btrfs_set_syno_quota_rescan_flags(leaf, rescan_item,
							orig_flags | SYNO_QUOTA_RESCAN_ERR);
		break;
	}
	case (SYNO_QUOTA_RESCAN_ERR | SYNO_QUOTA_RESCAN_DONE): {
		orig_flags &= SYNO_QUOTA_RESCAN_NEED;
		btrfs_set_syno_quota_rescan_flags(leaf, rescan_item,
			orig_flags | SYNO_QUOTA_RESCAN_DONE | SYNO_QUOTA_RESCAN_ERR);
		break;
	}
	case SYNO_QUOTA_RESCAN_NEED: {
		btrfs_set_syno_quota_rescan_flags(leaf, rescan_item,
							orig_flags | SYNO_QUOTA_RESCAN_NEED);
		break;
	}
	default:
		WARN_ON_ONCE(1);
	}
}

int btrfs_add_update_syno_quota_rescan_item(struct btrfs_trans_handle *trans,
		struct btrfs_root *quota_root, u64 subvol_id,
		struct syno_quota_rescan_item_updater *updater)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_syno_quota_rescan_item *rescan_item;
	struct extent_buffer *leaf;
	struct btrfs_key key;

	if (unlikely(!quota_root))
		return -EINVAL;

	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &trans->fs_info->flags) &&
			!updater->enable)
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_SYNO_QUOTA_RESCAN_KEY;
	key.offset = subvol_id;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*rescan_item));
	if (ret && ret != -EEXIST)
		goto out;

	// Insert a new item, SYNO_QUOTA_RESCAN_ITEM_SKIP is not allowed.
	if (ret != -EEXIST && syno_quota_rescan_item_check(updater)) {
		WARN_ON_ONCE(1);
		ret = -EINVAL;
		goto out;
	}

	leaf = path->nodes[0];
	rescan_item = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_syno_quota_rescan_item);

	if (updater->flags != SYNO_QUOTA_RESCAN_ITEM_SKIP) {
		if (ret != -EEXIST)
			btrfs_set_syno_quota_rescan_flags(leaf, rescan_item, updater->flags);
		else
			update_syno_quota_rescan_flags(leaf, rescan_item, updater->flags);
	}
	if (updater->version != SYNO_QUOTA_RESCAN_ITEM_SKIP)
		btrfs_set_syno_quota_rescan_version(leaf, rescan_item, updater->version);
	btrfs_set_syno_quota_rescan_generation(leaf, rescan_item, trans->transid);
	if (updater->rescan_inode != SYNO_QUOTA_RESCAN_ITEM_SKIP)
		btrfs_set_syno_quota_rescan_inode(leaf, rescan_item, updater->rescan_inode);
	if (updater->end_inode != SYNO_QUOTA_RESCAN_ITEM_SKIP)
		btrfs_set_syno_quota_rescan_end_inode(leaf, rescan_item, updater->end_inode);
	if (updater->tree_size != SYNO_QUOTA_RESCAN_ITEM_SKIP)
		btrfs_set_syno_quota_rescan_tree_size(leaf, rescan_item, updater->tree_size);
	if (updater->next_root != SYNO_QUOTA_RESCAN_ITEM_SKIP)
		btrfs_set_syno_quota_rescan_next_root(leaf, rescan_item, updater->next_root);

	btrfs_mark_buffer_dirty(leaf);
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}
#endif /* MY_ABC_HERE */

/*
 * called with qgroup_lock held
 */
static int btrfs_clean_quota_tree(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf = NULL;
	int ret;
	int nr = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;

	key.objectid = 0;
	key.offset = 0;
	key.type = 0;

	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto out;
		leaf = path->nodes[0];
		nr = btrfs_header_nritems(leaf);
		if (!nr)
			break;
		/*
		 * delete the leaf one by one
		 * since the whole tree is going
		 * to be deleted.
		 */
		path->slots[0] = 0;
		ret = btrfs_del_items(trans, root, path, 0, nr);
		if (ret)
			goto out;

		btrfs_release_path(path);
	}
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

#ifdef MY_ABC_HERE
int btrfs_quota_enable(struct btrfs_fs_info *fs_info, u64 cmd)
#else
int btrfs_quota_enable(struct btrfs_fs_info *fs_info)
#endif /* MY_ABC_HERE */
{
	struct btrfs_root *quota_root;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_path *path = NULL;
	struct btrfs_qgroup_status_item *ptr;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_qgroup *qgroup = NULL;
	struct btrfs_trans_handle *trans = NULL;
	struct ulist *ulist = NULL;
	int ret = 0;
	int slot;

#ifdef MY_ABC_HERE
	// Default using v2 quota.
	if (cmd == BTRFS_QUOTA_CTL_ENABLE)
		cmd = BTRFS_QUOTA_V2_CTL_ENABLE;

	if (btrfs_test_opt(fs_info, NO_QUOTA_TREE)) {
		btrfs_info(fs_info, "Can't enable quota with mount_opt no_quota_tree");
		return -EINVAL;
	}
	down_write(&fs_info->inflight_reserve_lock);
#endif /* MY_ABC_HERE */

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (fs_info->quota_root)
		goto out;

	ulist = ulist_alloc(GFP_KERNEL);
	if (!ulist) {
		ret = -ENOMEM;
		goto out;
	}

	ret = btrfs_sysfs_add_qgroups(fs_info);
	if (ret < 0)
		goto out;

	/*
	 * Unlock qgroup_ioctl_lock before starting the transaction. This is to
	 * avoid lock acquisition inversion problems (reported by lockdep) between
	 * qgroup_ioctl_lock and the vfs freeze semaphores, acquired when we
	 * start a transaction.
	 * After we started the transaction lock qgroup_ioctl_lock again and
	 * check if someone else created the quota root in the meanwhile. If so,
	 * just return success and release the transaction handle.
	 *
	 * Also we don't need to worry about someone else calling
	 * btrfs_sysfs_add_qgroups() after we unlock and getting an error because
	 * that function returns 0 (success) when the sysfs entries already exist.
	 */
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	/*
	 * 1 for quota root item
	 * 1 for BTRFS_QGROUP_STATUS item
	 *
	 * Yet we also need 2*n items for a QGROUP_INFO/QGROUP_LIMIT items
	 * per subvolume. However those are not currently reserved since it
	 * would be a lot of overkill.
	 */
	trans = btrfs_start_transaction(tree_root, 2);

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	if (fs_info->quota_root)
		goto out;

	fs_info->qgroup_ulist = ulist;
	ulist = NULL;

	/*
	 * initially create the quota tree
	 */
#ifdef MY_ABC_HERE
	if (cmd == BTRFS_QUOTA_V2_CTL_ENABLE)
		quota_root = btrfs_create_tree(trans, BTRFS_SYNO_QUOTA_V2_TREE_OBJECTID);
	else
#endif /* MY_ABC_HERE */
		quota_root = btrfs_create_tree(trans, BTRFS_QUOTA_TREE_OBJECTID);
	if (IS_ERR(quota_root)) {
		ret =  PTR_ERR(quota_root);
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		btrfs_abort_transaction(trans, ret);
		goto out_free_root;
	}

	key.objectid = 0;
	key.type = BTRFS_QGROUP_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_insert_empty_item(trans, quota_root, path, &key,
				      sizeof(*ptr));
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

	leaf = path->nodes[0];
	ptr = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_qgroup_status_item);
	btrfs_set_qgroup_status_generation(leaf, ptr, trans->transid);
#ifdef MY_ABC_HERE
	if (cmd == BTRFS_QUOTA_V2_CTL_ENABLE)
		btrfs_set_qgroup_status_version(leaf, ptr, BTRFS_QGROUP_V2_STATUS_VERSION);
	else
#endif /* MY_ABC_HERE */
		btrfs_set_qgroup_status_version(leaf, ptr, BTRFS_QGROUP_STATUS_VERSION);
	fs_info->qgroup_flags = BTRFS_QGROUP_STATUS_FLAG_ON |
				BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	btrfs_set_qgroup_status_flags(leaf, ptr, fs_info->qgroup_flags);
	btrfs_set_qgroup_status_rescan(leaf, ptr, 0);

	btrfs_mark_buffer_dirty(leaf);

	key.objectid = 0;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = 0;

	btrfs_release_path(path);
	ret = btrfs_search_slot_for_read(tree_root, &key, path, 1, 0);
	if (ret > 0)
		goto out_add_root;
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.type == BTRFS_ROOT_REF_KEY) {

			/* Release locks on tree_root before we access quota_root */
			btrfs_release_path(path);

			ret = add_qgroup_item(trans, quota_root,
					      found_key.offset);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				goto out_free_path;
			}

			qgroup = add_qgroup_rb(fs_info, found_key.offset);
			if (IS_ERR(qgroup)) {
				ret = PTR_ERR(qgroup);
				btrfs_abort_transaction(trans, ret);
				goto out_free_path;
			}
			ret = btrfs_sysfs_add_one_qgroup(fs_info, qgroup);
			if (ret < 0) {
				btrfs_abort_transaction(trans, ret);
				goto out_free_path;
			}
			ret = btrfs_search_slot_for_read(tree_root, &found_key,
							 path, 1, 0);
			if (ret < 0) {
				btrfs_abort_transaction(trans, ret);
				goto out_free_path;
			}
			if (ret > 0) {
				/*
				 * Shouldn't happen, but in case it does we
				 * don't need to do the btrfs_next_item, just
				 * continue.
				 */
				continue;
			}
#ifdef MY_ABC_HERE
			if (!ret && cmd == BTRFS_QUOTA_V2_CTL_ENABLE) {
				struct syno_quota_rescan_item_updater updater;
				struct btrfs_root *fs_root;

				syno_quota_rescan_item_init(&updater);
				updater.flags = SYNO_QUOTA_RESCAN_DONE | SYNO_QUOTA_RESCAN_NEED;
				updater.version = 0; // So root->invalid_quota will be set.
				updater.rescan_inode = 0;
				updater.end_inode = (u64)-1;
				updater.tree_size = 0;
				updater.next_root = 0;
				updater.enable = true;

				ret = btrfs_add_update_syno_quota_rescan_item(trans, quota_root,
					found_key.offset, &updater);
				if (ret)
					btrfs_warn(fs_info,
						"Failed to create syno quota rescan item for root %llu, ret = %d",
						found_key.offset, ret);

				/*
				 * If fs root is not in memory, we set invalid_quota in
				 * btrfs_read_syno_quota_for_root().
				 */
				fs_root = btrfs_lookup_fs_root(fs_info, found_key.offset);
				if (fs_root) {
					fs_root->invalid_quota = true;
					fs_root->rescan_inode = 0;
					btrfs_put_root(fs_root);
				}

				ret = 0;
			}
#endif /* MY_ABC_HERE */
		}
		ret = btrfs_next_item(tree_root, path);
		if (ret < 0) {
			btrfs_abort_transaction(trans, ret);
			goto out_free_path;
		}
		if (ret)
			break;
	}

out_add_root:
	btrfs_release_path(path);
	ret = add_qgroup_item(trans, quota_root, BTRFS_FS_TREE_OBJECTID);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

	qgroup = add_qgroup_rb(fs_info, BTRFS_FS_TREE_OBJECTID);
	if (IS_ERR(qgroup)) {
		ret = PTR_ERR(qgroup);
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}
	ret = btrfs_sysfs_add_one_qgroup(fs_info, qgroup);
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

#ifdef MY_ABC_HERE
	if (!ret && cmd == BTRFS_QUOTA_V2_CTL_ENABLE) {
		struct syno_quota_rescan_item_updater updater;

		syno_quota_rescan_item_init(&updater);
		updater.flags = SYNO_QUOTA_RESCAN_DONE;
		updater.version = BTRFS_QGROUP_V2_STATUS_VERSION;
		updater.rescan_inode = (u64)-1;
		updater.end_inode = (u64)-1;
		updater.tree_size = 0;
		updater.next_root = 0;
		updater.enable = true;

		ret = btrfs_add_update_syno_quota_rescan_item(trans, quota_root,
			BTRFS_FS_TREE_OBJECTID, &updater);
		if (ret)
			btrfs_warn(fs_info,
				"Failed to create syno quota rescan item for root 5, ret = %d", ret);
		ret = 0; // No need to abort transaction, it is not that critical.
	}
#endif /* MY_ABC_HERE */

	ret = btrfs_commit_transaction(trans);
	trans = NULL;
	if (ret)
		goto out_free_path;

	/*
	 * Set quota enabled flag after committing the transaction, to avoid
	 * deadlocks on fs_info->qgroup_ioctl_lock with concurrent snapshot
	 * creation.
	 */
	spin_lock(&fs_info->qgroup_lock);
	fs_info->quota_root = quota_root;
#ifdef MY_ABC_HERE
	if (cmd == BTRFS_QUOTA_V1_CTL_ENABLE)
		set_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags);
	else
		set_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags);
#else
	set_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
#endif /* MY_ABC_HERE */
	spin_unlock(&fs_info->qgroup_lock);

#ifdef MY_ABC_HERE
#else
	ret = qgroup_rescan_init(fs_info, 0, 1);
	if (!ret) {
	        qgroup_rescan_zero_tracking(fs_info);
		fs_info->qgroup_rescan_running = true;
	        btrfs_queue_work(fs_info->qgroup_rescan_workers,
	                         &fs_info->qgroup_rescan_work);
	}
#endif /* MY_ABC_HERE */

out_free_path:
	btrfs_free_path(path);
out_free_root:
	if (ret)
		btrfs_put_root(quota_root);
out:
	if (ret) {
		ulist_free(fs_info->qgroup_ulist);
		fs_info->qgroup_ulist = NULL;
		btrfs_sysfs_del_qgroups(fs_info);
	}
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
#ifdef MY_ABC_HERE
	up_write(&fs_info->inflight_reserve_lock);
#endif /* MY_ABC_HERE */

	if (ret && trans)
		btrfs_end_transaction(trans);
	else if (trans)
		ret = btrfs_end_transaction(trans);
	ulist_free(ulist);
	return ret;
}

int btrfs_quota_disable(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *quota_root;
	struct btrfs_trans_handle *trans = NULL;
	int ret = 0;

#ifdef MY_ABC_HERE
	/*
	 * Protected by fs_info->subvol_sem, so user quota will not do enable
	 * before we finish qgroup disable.
	 */
	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) ||
			test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
		btrfs_warn(fs_info,
			"Should disable user quota before disable qgroup.");
		return -EINVAL;
	}
#endif /* MY_ABC_HERE */

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root)
		goto out;
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	/*
	 * 1 For the root item
	 *
	 * We should also reserve enough items for the quota tree deletion in
	 * btrfs_clean_quota_tree but this is not done.
	 *
	 * Also, we must always start a transaction without holding the mutex
	 * qgroup_ioctl_lock, see btrfs_quota_enable().
	 */
	trans = btrfs_start_transaction(fs_info->tree_root, 1);

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	if (!fs_info->quota_root)
		goto out;

#ifdef MY_ABC_HERE
	fs_info->need_clear_reserve = true;
	smp_wmb();
	clear_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags);
	clear_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags);
#else
	clear_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);
#endif /* MY_ABC_HERE */
	btrfs_qgroup_wait_for_completion(fs_info, false);
	spin_lock(&fs_info->qgroup_lock);
	quota_root = fs_info->quota_root;
	fs_info->quota_root = NULL;
	fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_ON;
	spin_unlock(&fs_info->qgroup_lock);

	btrfs_free_qgroup_config(fs_info);

	ret = btrfs_clean_quota_tree(trans, quota_root);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = btrfs_del_root(trans, &quota_root->root_key);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	list_del(&quota_root->dirty_list);

	btrfs_tree_lock(quota_root->node);
	btrfs_clean_tree_block(quota_root->node);
	btrfs_tree_unlock(quota_root->node);
	btrfs_free_tree_block(trans, quota_root, quota_root->node, 0, 1);

	btrfs_put_root(quota_root);

#ifdef MY_ABC_HERE
	ret = btrfs_end_transaction(trans);
	trans = NULL;
	btrfs_start_delalloc_roots(fs_info, U64_MAX, false);
	btrfs_wait_ordered_roots(fs_info, U64_MAX, 0, (u64)-1);
	fs_info->need_clear_reserve = false;
#endif /* MY_ABC_HERE */

out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	if (ret && trans)
		btrfs_end_transaction(trans);
	else if (trans)
		ret = btrfs_end_transaction(trans);

	return ret;
}

#ifdef MY_ABC_HERE
int btrfs_quota_unload(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *quota_root;
	struct btrfs_trans_handle *trans = NULL;
	int ret = 0;

	/*
	 * Protected by fs_info->subvol_sem, so user quota will not do enable
	 * before we finish qgroup disable.
	 */
	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) ||
			test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
		btrfs_warn(fs_info,
			"Should disable user quota before disable qgroup.");
		return -EINVAL;
	}

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root)
		goto out;
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	/*
	 * 1 For the root item
	 *
	 * We should also reserve enough items for the quota tree deletion in
	 * btrfs_clean_quota_tree but this is not done.
	 *
	 * Also, we must always start a transaction without holding the mutex
	 * qgroup_ioctl_lock, see btrfs_quota_enable().
	 */
	trans = btrfs_start_transaction(fs_info->tree_root, 1);

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	if (!fs_info->quota_root)
		goto out;

	fs_info->need_clear_reserve = true;
	smp_wmb();
	clear_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags);
	clear_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags);
	btrfs_qgroup_wait_for_completion(fs_info, false);
	spin_lock(&fs_info->qgroup_lock);
	quota_root = fs_info->quota_root;
	fs_info->quota_root = NULL;
	fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_ON;
	spin_unlock(&fs_info->qgroup_lock);
	btrfs_free_qgroup_config(fs_info);

	btrfs_start_delalloc_roots(fs_info, U64_MAX, false);
	btrfs_wait_ordered_roots(fs_info, U64_MAX, 0, (u64)-1);
	fs_info->need_clear_reserve = false;
	ret = btrfs_commit_transaction(trans);
	trans = NULL;

	list_del(&quota_root->dirty_list);
	btrfs_put_root(quota_root);

out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	if (ret && trans)
		btrfs_end_transaction(trans);
	else if (trans)
		ret = btrfs_end_transaction(trans);

	return ret;
}

int btrfs_quota_remove_v1(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *root;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_path *path = NULL;
	struct btrfs_key location;
	int ret = 0;
	int nr;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	// Read old qgroup root.
	location.objectid = BTRFS_QUOTA_TREE_OBJECTID;
	location.type = BTRFS_ROOT_ITEM_KEY;
	location.offset = 0;

	root = btrfs_read_tree_root(tree_root, &location);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out;
	}
	set_bit(BTRFS_ROOT_TRACK_DIRTY, &root->state);

	location.objectid = 0;
	location.offset = 0;
	location.type = 0;

	while (1) {
		trans = btrfs_start_transaction(tree_root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto free_root;
		}

		ret = btrfs_search_slot(trans, root, &location, path, -1, 1);
		if (ret < 0)
			goto free_root;
		nr = btrfs_header_nritems(path->nodes[0]);
		if (!nr)
			break;
		path->slots[0] = 0;
		ret = btrfs_del_items(trans, root, path, 0, nr);
		if (ret)
			goto free_root;

		btrfs_release_path(path);
		btrfs_end_transaction_throttle(trans);
		trans = NULL;
		cond_resched();
	}
	btrfs_release_path(path);

	// Remove root item from root tree.
	ret = btrfs_del_root(trans, &root->root_key);

free_root:
	btrfs_release_path(path);
	list_del(&root->dirty_list);
	btrfs_tree_lock(root->node);
	btrfs_clean_tree_block(root->node);
	btrfs_tree_unlock(root->node);
	btrfs_free_tree_block(trans, root, root->node, 0, 1);

	free_extent_buffer(root->node);
	free_extent_buffer(root->commit_root);
	kfree(root);

	if (trans) {
		if (!ret)
			ret = btrfs_commit_transaction(trans);
		else
			btrfs_end_transaction(trans);
	}
out:
	btrfs_free_path(path);
	return ret;
}
#endif /* MY_ABC_HERE */

static void qgroup_dirty(struct btrfs_fs_info *fs_info,
			 struct btrfs_qgroup *qgroup)
{
	if (list_empty(&qgroup->dirty))
		list_add(&qgroup->dirty, &fs_info->dirty_qgroups);
}

/*
 * The easy accounting, we're updating qgroup relationship whose child qgroup
 * only has exclusive extents.
 *
 * In this case, all exclusive extents will also be exclusive for parent, so
 * excl/rfer just get added/removed.
 *
 * So is qgroup reservation space, which should also be added/removed to
 * parent.
 * Or when child tries to release reservation space, parent will underflow its
 * reservation (for relationship adding case).
 *
 * Caller should hold fs_info->qgroup_lock.
 */
#ifdef MY_ABC_HERE
#else
static int __qgroup_excl_accounting(struct btrfs_fs_info *fs_info,
				    struct ulist *tmp, u64 ref_root,
				    struct btrfs_qgroup *src, int sign)
{
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup_list *glist;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	u64 num_bytes = src->excl;
	int ret = 0;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	qgroup->rfer += sign * num_bytes;
	qgroup->rfer_cmpr += sign * num_bytes;

	WARN_ON(sign < 0 && qgroup->excl < num_bytes);
	qgroup->excl += sign * num_bytes;
	qgroup->excl_cmpr += sign * num_bytes;

	if (sign > 0)
		qgroup_rsv_add_by_qgroup(fs_info, qgroup, src);
	else
		qgroup_rsv_release_by_qgroup(fs_info, qgroup, src);

	qgroup_dirty(fs_info, qgroup);

	/* Get all of the parent groups that contain this qgroup */
	list_for_each_entry(glist, &qgroup->groups, next_group) {
		ret = ulist_add(tmp, glist->group->qgroupid,
				qgroup_to_aux(glist->group), GFP_ATOMIC);
		if (ret < 0)
			goto out;
	}

	/* Iterate all of the parents and adjust their reference counts */
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(tmp, &uiter))) {
		qgroup = unode_aux_to_qgroup(unode);
		qgroup->rfer += sign * num_bytes;
		qgroup->rfer_cmpr += sign * num_bytes;
		WARN_ON(sign < 0 && qgroup->excl < num_bytes);
		qgroup->excl += sign * num_bytes;
		if (sign > 0)
			qgroup_rsv_add_by_qgroup(fs_info, qgroup, src);
		else
			qgroup_rsv_release_by_qgroup(fs_info, qgroup, src);
		qgroup->excl_cmpr += sign * num_bytes;
		qgroup_dirty(fs_info, qgroup);

		/* Add any parents of the parents */
		list_for_each_entry(glist, &qgroup->groups, next_group) {
			ret = ulist_add(tmp, glist->group->qgroupid,
					qgroup_to_aux(glist->group), GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
	ret = 0;
out:
	return ret;
}
#endif /* MY_ABC_HERE */

/*
 * Quick path for updating qgroup with only excl refs.
 *
 * In that case, just update all parent will be enough.
 * Or we needs to do a full rescan.
 * Caller should also hold fs_info->qgroup_lock.
 *
 * Return 0 for quick update, return >0 for need to full rescan
 * and mark INCONSISTENT flag.
 * Return < 0 for other error.
 */
static int quick_update_accounting(struct btrfs_fs_info *fs_info,
				   struct ulist *tmp, u64 src, u64 dst,
				   int sign)
{
#ifdef MY_ABC_HERE
	return 0;
#else
	struct btrfs_qgroup *qgroup;
	int ret = 1;
	int err = 0;

	qgroup = find_qgroup_rb(fs_info, src);
	if (!qgroup)
		goto out;
	if (qgroup->excl == qgroup->rfer) {
		ret = 0;
		err = __qgroup_excl_accounting(fs_info, tmp, dst,
					       qgroup, sign);
		if (err < 0) {
			ret = err;
			goto out;
		}
	}
out:
	if (ret)
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	return ret;
#endif /* MY_ABC_HERE */
}

int btrfs_add_qgroup_relation(struct btrfs_trans_handle *trans, u64 src,
			      u64 dst)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup *member;
	struct btrfs_qgroup_list *list;
	struct ulist *tmp;
	unsigned int nofs_flag;
	int ret = 0;

	/* Check the level of src and dst first */
	if (btrfs_qgroup_level(src) >= btrfs_qgroup_level(dst))
		return -EINVAL;

	/* We hold a transaction handle open, must do a NOFS allocation. */
	nofs_flag = memalloc_nofs_save();
	tmp = ulist_alloc(GFP_KERNEL);
	memalloc_nofs_restore(nofs_flag);
	if (!tmp)
		return -ENOMEM;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
#ifdef MY_ABC_HERE
		ret = -ESRCH;
#else
		ret = -ENOTCONN;
#endif /* MY_ABC_HERE */
		goto out;
	}
	member = find_qgroup_rb(fs_info, src);
	parent = find_qgroup_rb(fs_info, dst);
	if (!member || !parent) {
		ret = -EINVAL;
		goto out;
	}

	/* check if such qgroup relation exist firstly */
	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent) {
			ret = -EEXIST;
			goto out;
		}
	}

	ret = add_qgroup_relation_item(trans, src, dst);
	if (ret)
		goto out;

	ret = add_qgroup_relation_item(trans, dst, src);
	if (ret) {
		del_qgroup_relation_item(trans, src, dst);
		goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
	ret = add_relation_rb(fs_info, src, dst);
	if (ret < 0) {
		spin_unlock(&fs_info->qgroup_lock);
		goto out;
	}
	ret = quick_update_accounting(fs_info, tmp, src, dst, 1);
	spin_unlock(&fs_info->qgroup_lock);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	ulist_free(tmp);
	return ret;
}

static int __del_qgroup_relation(struct btrfs_trans_handle *trans, u64 src,
				 u64 dst)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup *parent;
	struct btrfs_qgroup *member;
	struct btrfs_qgroup_list *list;
	struct ulist *tmp;
	bool found = false;
	unsigned int nofs_flag;
	int ret = 0;
	int ret2;

	/* We hold a transaction handle open, must do a NOFS allocation. */
	nofs_flag = memalloc_nofs_save();
	tmp = ulist_alloc(GFP_KERNEL);
	memalloc_nofs_restore(nofs_flag);
	if (!tmp)
		return -ENOMEM;

	if (!fs_info->quota_root) {
		ret = -ENOTCONN;
		goto out;
	}

	member = find_qgroup_rb(fs_info, src);
	parent = find_qgroup_rb(fs_info, dst);
	/*
	 * The parent/member pair doesn't exist, then try to delete the dead
	 * relation items only.
	 */
	if (!member || !parent)
		goto delete_item;

	/* check if such qgroup relation exist firstly */
	list_for_each_entry(list, &member->groups, next_group) {
		if (list->group == parent) {
			found = true;
			break;
		}
	}

delete_item:
	ret = del_qgroup_relation_item(trans, src, dst);
	if (ret < 0 && ret != -ENOENT)
		goto out;
	ret2 = del_qgroup_relation_item(trans, dst, src);
	if (ret2 < 0 && ret2 != -ENOENT)
		goto out;

	/* At least one deletion succeeded, return 0 */
	if (!ret || !ret2)
		ret = 0;

	if (found) {
		spin_lock(&fs_info->qgroup_lock);
		del_relation_rb(fs_info, src, dst);
		ret = quick_update_accounting(fs_info, tmp, src, dst, -1);
		spin_unlock(&fs_info->qgroup_lock);
	}
out:
	ulist_free(tmp);
	return ret;
}

int btrfs_del_qgroup_relation(struct btrfs_trans_handle *trans, u64 src,
			      u64 dst)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	ret = __del_qgroup_relation(trans, src, dst);
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	return ret;
}

int btrfs_create_qgroup(struct btrfs_trans_handle *trans, u64 qgroupid)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *qgroup;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
#ifdef MY_ABC_HERE
		ret = -ESRCH;
#else
		ret = -ENOTCONN;
#endif /* MY_ABC_HERE */
		goto out;
	}
	quota_root = fs_info->quota_root;
	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (qgroup) {
		ret = -EEXIST;
		goto out;
	}

	ret = add_qgroup_item(trans, quota_root, qgroupid);
	if (ret)
		goto out;

	spin_lock(&fs_info->qgroup_lock);
	qgroup = add_qgroup_rb(fs_info, qgroupid);
	spin_unlock(&fs_info->qgroup_lock);

	if (IS_ERR(qgroup)) {
		ret = PTR_ERR(qgroup);
		goto out;
	}
	ret = btrfs_sysfs_add_one_qgroup(fs_info, qgroup);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

#ifdef MY_ABC_HERE
/*
 * struct btrfs_ioctl_qgroup_query_args should be initialized to zero
 */
int btrfs_qgroup_query(struct btrfs_root *root,
			struct btrfs_ioctl_qgroup_query_args *qqa)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_qgroup *qgroup;
	u64 qgroupid = root->root_key.objectid;
	int ret;

#ifdef MY_ABC_HERE
	if (unlikely(root->invalid_quota))
		return -ESRCH;
#endif /* MY_ABC_HERE */

	mutex_lock(&fs_info->qgroup_ioctl_lock);
#ifdef MY_ABC_HERE
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags) &&
		!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags)) {
#else
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags)) {
#endif /* MY_ABC_HERE */
		ret = -ESRCH;
		goto unlock;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		ret = -ENOENT;
		goto unlock;
	}

	qqa->rfer = qgroup->rfer;
	qqa->rfer_cmpr = qgroup->rfer_cmpr;
	qqa->excl = qgroup->excl;
	qqa->excl_cmpr = qgroup->excl_cmpr;

	if (qgroup->lim_flags & BTRFS_QGROUP_LIMIT_MAX_RFER)
		qqa->max_rfer = qgroup->max_rfer;
	if (qgroup->lim_flags & BTRFS_QGROUP_LIMIT_MAX_EXCL)
		qqa->max_excl = qgroup->max_excl;
	if (qgroup->lim_flags & BTRFS_QGROUP_LIMIT_RSV_RFER)
		qqa->rsv_rfer = qgroup->rsv_rfer;
	if (qgroup->lim_flags & BTRFS_QGROUP_LIMIT_RSV_EXCL)
		qqa->rsv_excl = qgroup->rsv_excl;
	qqa->reserved = qgroup->rsv.values[BTRFS_QGROUP_RSV_DATA];
	ret = 0;
unlock:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
// We will remove rescan item later in del_qgroup_item().
void btrfs_remove_queued_syno_rescan(struct btrfs_trans_handle *trans, u64 subvol_id)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct syno_quota_rescan_ctx *ctx = fs_info->syno_quota_rescan_ctx;
	struct ulist *ulist = fs_info->syno_quota_rescan_subvol_ulist;
	struct ulist_node *node;

	if (!fs_info->quota_root || !ctx || !ulist)
		return;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	node = ulist_search(ulist, subvol_id);
	if (node) {
		struct ulist_node *prev_node = NULL;
		struct ulist_node *next_node = NULL;
		u64 prev_subvol_id = 0;
		u64 next_subvol_id = 0;
		int ret;

		if (node->list.prev != &ulist->nodes) {
			prev_node = list_entry(node->list.prev, struct ulist_node, list);
			prev_subvol_id = prev_node->val;
		}

		if (node->list.next != &ulist->nodes) {
			next_node = list_entry(node->list.next, struct ulist_node, list);
			next_subvol_id = next_node->val;
		}

		if (prev_subvol_id) {
			struct syno_quota_rescan_item_updater updater;

			syno_quota_rescan_item_init(&updater);
			updater.next_root = (next_subvol_id)? next_subvol_id : 0;
			ret = btrfs_add_update_syno_quota_rescan_item(trans, fs_info->quota_root,
				prev_subvol_id, &updater);
			if (ret)
				btrfs_warn(fs_info,
					"Failed to update syno quota rescan item, id = %llu, ret = %d.",
					prev_subvol_id, ret);
		}

		if (fs_info->qgroup_rescan_progress.objectid == subvol_id)
			fs_info->qgroup_rescan_progress.objectid = next_subvol_id;
		ulist_del(ulist, subvol_id, node->aux);

		// Update progress info.
		if (!fs_info->qgroup_rescan_progress.objectid) { // No next subvol. All rescan are done.
			update_syno_quota_rescan_progress(fs_info->quota_root, ctx,
				subvol_id, SYNO_QUOTA_PROGRESS_FINISH_ALL);
			fs_info->qgroup_rescan_progress.objectid = 0;
		} else if (ctx->subvol_id == subvol_id) // We are removing the current scanning subvol.
			update_syno_quota_rescan_progress(fs_info->quota_root, ctx,
				subvol_id, SYNO_QUOTA_PROGRESS_FINISH_ONE);
		else // We are removing a queued subvol.
			update_syno_quota_rescan_progress(fs_info->quota_root, ctx,
				subvol_id, SYNO_QUOTA_PROGRESS_REMOVE_QUEUED);
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);
	return;
}
#endif /* MY_ABC_HERE */

int btrfs_remove_qgroup(struct btrfs_trans_handle *trans, u64 qgroupid)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup *qgroup;
	struct btrfs_qgroup_list *list;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
#ifdef MY_ABC_HERE
		ret = -ESRCH;
#else
		ret = -ENOTCONN;
#endif /* MY_ABC_HERE */
		goto out;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		ret = -ENOENT;
		goto out;
	}

	/* Check if there are no children of this qgroup */
	if (!list_empty(&qgroup->members)) {
		ret = -EBUSY;
		goto out;
	}

#ifdef MY_ABC_HERE
	btrfs_remove_queued_syno_rescan(trans, qgroupid);
#endif /* MY_ABC_HERE */

	ret = del_qgroup_item(trans, qgroupid);
	if (ret && ret != -ENOENT)
		goto out;

	while (!list_empty(&qgroup->groups)) {
		list = list_first_entry(&qgroup->groups,
					struct btrfs_qgroup_list, next_group);
		ret = __del_qgroup_relation(trans, qgroupid,
					    list->group->qgroupid);
		if (ret)
			goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
	del_qgroup_rb(fs_info, qgroupid);
	spin_unlock(&fs_info->qgroup_lock);

	/*
	 * Remove the qgroup from sysfs now without holding the qgroup_lock
	 * spinlock, since the sysfs_remove_group() function needs to take
	 * the mutex kernfs_mutex through kernfs_remove_by_name_ns().
	 */
	btrfs_sysfs_del_one_qgroup(fs_info, qgroup);
	kfree(qgroup);
out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_limit_qgroup(struct btrfs_trans_handle *trans, u64 qgroupid,
		       struct btrfs_qgroup_limit *limit)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup *qgroup;
	int ret = 0;
#ifdef MY_ABC_HERE
	struct btrfs_root *root = trans->root;
	bool has_limit = false;
#endif /* MY_ABC_HERE */
	/* Sometimes we would want to clear the limit on this qgroup.
	 * To meet this requirement, we treat the -1 as a special value
	 * which tell kernel to clear the limit on this qgroup.
	 */
	const u64 CLEAR_VALUE = -1;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
#ifdef MY_ABC_HERE
		ret = -ESRCH;
#else
		ret = -ENOTCONN;
#endif /* MY_ABC_HERE */
		goto out;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		ret = -ENOENT;
		goto out;
	}

	spin_lock(&fs_info->qgroup_lock);
#ifdef MY_ABC_HERE
	if (!limit->flags) {
		qgroup->lim_flags = limit->flags;
		qgroup->max_rfer = limit->max_rfer;
		qgroup->max_excl = limit->max_excl;
		qgroup->rsv_rfer = limit->rsv_rfer;
		qgroup->rsv_excl = limit->rsv_excl;
	}
#endif /* MY_ABC_HERE */
	if (limit->flags & BTRFS_QGROUP_LIMIT_MAX_RFER) {
		if (limit->max_rfer == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_MAX_RFER;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_MAX_RFER;
			qgroup->max_rfer = 0;
		} else {
			qgroup->max_rfer = limit->max_rfer;
		}
	}
	if (limit->flags & BTRFS_QGROUP_LIMIT_MAX_EXCL) {
		if (limit->max_excl == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_MAX_EXCL;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_MAX_EXCL;
			qgroup->max_excl = 0;
		} else {
			qgroup->max_excl = limit->max_excl;
		}
	}
	if (limit->flags & BTRFS_QGROUP_LIMIT_RSV_RFER) {
		if (limit->rsv_rfer == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_RSV_RFER;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_RSV_RFER;
			qgroup->rsv_rfer = 0;
		} else {
			qgroup->rsv_rfer = limit->rsv_rfer;
		}
	}
	if (limit->flags & BTRFS_QGROUP_LIMIT_RSV_EXCL) {
		if (limit->rsv_excl == CLEAR_VALUE) {
			qgroup->lim_flags &= ~BTRFS_QGROUP_LIMIT_RSV_EXCL;
			limit->flags &= ~BTRFS_QGROUP_LIMIT_RSV_EXCL;
			qgroup->rsv_excl = 0;
		} else {
			qgroup->rsv_excl = limit->rsv_excl;
		}
	}
	qgroup->lim_flags |= limit->flags;

#ifdef MY_ABC_HERE
	if ((qgroup->lim_flags & BTRFS_QGROUP_LIMIT_MAX_RFER && qgroup->max_rfer) ||
	    (qgroup->lim_flags & BTRFS_QGROUP_LIMIT_MAX_EXCL && qgroup->max_excl))
		has_limit = true;
	btrfs_root_set_has_quota_limit(root, has_limit);
#endif /* MY_ABC_HERE */
	spin_unlock(&fs_info->qgroup_lock);

	ret = update_qgroup_limit_item(trans, qgroup);
	if (ret) {
#ifdef MY_ABC_HERE
#else
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
#endif /* MY_ABC_HERE */
		btrfs_info(fs_info, "unable to update quota limit for %llu",
		       qgroupid);
	}

out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

#ifdef MY_ABC_HERE
int btrfs_insert_quota_record(struct btrfs_trans_handle *trans,
				struct btrfs_delayed_ref_node *node)
{
	struct btrfs_delayed_data_ref *ref;
	struct btrfs_transaction *cur_trans = trans->transaction;
	struct btrfs_quota_account_rec *record;

	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &trans->fs_info->flags))
		return 0;

	ref = btrfs_delayed_node_to_data_ref(node);
	if (ref->skip_qgroup)
		return 0;

	record = kzalloc(sizeof(*record), GFP_NOFS);
	if (!record)
		return -ENOMEM;

	record->ref_root = ref->root;
	record->num_bytes = node->num_bytes;
	record->ram_bytes = ref->ram_bytes;
	record->reserved = ref->reserved;
	record->uid = ref->uid;
	if (node->action == BTRFS_DROP_DELAYED_REF)
		record->sign = -1;
	else
		record->sign = 1;
	record->inode = ref->inode;
	syno_usrquota_inode_get(record->inode);

	spin_lock(&cur_trans->quota_account_lock);
	list_add_tail(&record->list, &cur_trans->quota_account_list);
	spin_unlock(&cur_trans->quota_account_lock);
	return 0;
}

void btrfs_quota_syno_v1_accounting(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_quota_account_rec *record;
	struct btrfs_transaction *cur_trans = trans->transaction;

	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags)) {
		WARN_ON(!list_empty(&cur_trans->quota_account_list));
		return;
	}

	while (1) {
		spin_lock(&cur_trans->quota_account_lock);
		if (list_empty(&cur_trans->quota_account_list)) {
			spin_unlock(&cur_trans->quota_account_lock);
			break;
		}

		record = list_first_entry(&cur_trans->quota_account_list,
					struct btrfs_quota_account_rec, list);
		list_del_init(&record->list);
		spin_unlock(&cur_trans->quota_account_lock);

		if (!trans->aborted) {
			btrfs_qgroup_syno_v1_accounting(fs_info, record);
			btrfs_usrquota_syno_v1_accounting(trans, record);
		}
		syno_usrquota_inode_put(record->inode);
		kfree(record);
	}

	return;
}
#else
int btrfs_qgroup_trace_extent_nolock(struct btrfs_fs_info *fs_info,
				struct btrfs_delayed_ref_root *delayed_refs,
				struct btrfs_qgroup_extent_record *record)
{
	struct rb_node **p = &delayed_refs->dirty_extent_root.rb_node;
	struct rb_node *parent_node = NULL;
	struct btrfs_qgroup_extent_record *entry;
	u64 bytenr = record->bytenr;

	lockdep_assert_held(&delayed_refs->lock);
	trace_btrfs_qgroup_trace_extent(fs_info, record);

	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node, struct btrfs_qgroup_extent_record,
				 node);
		if (bytenr < entry->bytenr) {
			p = &(*p)->rb_left;
		} else if (bytenr > entry->bytenr) {
			p = &(*p)->rb_right;
		} else {
			if (record->data_rsv && !entry->data_rsv) {
				entry->data_rsv = record->data_rsv;
				entry->data_rsv_refroot =
					record->data_rsv_refroot;
			}
			return 1;
		}
	}

	rb_link_node(&record->node, parent_node, p);
	rb_insert_color(&record->node, &delayed_refs->dirty_extent_root);
	return 0;
}

int btrfs_qgroup_trace_extent_post(struct btrfs_fs_info *fs_info,
				   struct btrfs_qgroup_extent_record *qrecord)
{
	struct ulist *old_root;
	u64 bytenr = qrecord->bytenr;
	int ret;

	ret = btrfs_find_all_roots(NULL, fs_info, bytenr, 0, &old_root, false);
	if (ret < 0) {
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		btrfs_warn(fs_info,
"error accounting new delayed refs extent (err code: %d), quota inconsistent",
			ret);
		return 0;
	}

	/*
	 * Here we don't need to get the lock of
	 * trans->transaction->delayed_refs, since inserted qrecord won't
	 * be deleted, only qrecord->node may be modified (new qrecord insert)
	 *
	 * So modifying qrecord->old_roots is safe here
	 */
	qrecord->old_roots = old_root;
	return 0;
}

int btrfs_qgroup_trace_extent(struct btrfs_trans_handle *trans, u64 bytenr,
			      u64 num_bytes, gfp_t gfp_flag)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup_extent_record *record;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags)
	    || bytenr == 0 || num_bytes == 0)
		return 0;
	record = kzalloc(sizeof(*record), gfp_flag);
	if (!record)
		return -ENOMEM;

	delayed_refs = &trans->transaction->delayed_refs;
	record->bytenr = bytenr;
	record->num_bytes = num_bytes;
	record->old_roots = NULL;

	spin_lock(&delayed_refs->lock);
	ret = btrfs_qgroup_trace_extent_nolock(fs_info, delayed_refs, record);
	spin_unlock(&delayed_refs->lock);
	if (ret > 0) {
		kfree(record);
		return 0;
	}
	return btrfs_qgroup_trace_extent_post(fs_info, record);
}

int btrfs_qgroup_trace_leaf_items(struct btrfs_trans_handle *trans,
				  struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int nr = btrfs_header_nritems(eb);
	int i, extent_type, ret;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	u64 bytenr, num_bytes;

	/* We can be called directly from walk_up_proc() */
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		return 0;

	for (i = 0; i < nr; i++) {
		btrfs_item_key_to_cpu(eb, &key, i);

		if (key.type != BTRFS_EXTENT_DATA_KEY)
			continue;

		fi = btrfs_item_ptr(eb, i, struct btrfs_file_extent_item);
		/* filter out non qgroup-accountable extents  */
		extent_type = btrfs_file_extent_type(eb, fi);

		if (extent_type == BTRFS_FILE_EXTENT_INLINE)
			continue;

		bytenr = btrfs_file_extent_disk_bytenr(eb, fi);
		if (!bytenr)
			continue;

		num_bytes = btrfs_file_extent_disk_num_bytes(eb, fi);

		ret = btrfs_qgroup_trace_extent(trans, bytenr, num_bytes,
						GFP_NOFS);
		if (ret)
			return ret;
	}
	cond_resched();
	return 0;
}

/*
 * Walk up the tree from the bottom, freeing leaves and any interior
 * nodes which have had all slots visited. If a node (leaf or
 * interior) is freed, the node above it will have it's slot
 * incremented. The root node will never be freed.
 *
 * At the end of this function, we should have a path which has all
 * slots incremented to the next position for a search. If we need to
 * read a new node it will be NULL and the node above it will have the
 * correct slot selected for a later read.
 *
 * If we increment the root nodes slot counter past the number of
 * elements, 1 is returned to signal completion of the search.
 */
static int adjust_slots_upwards(struct btrfs_path *path, int root_level)
{
	int level = 0;
	int nr, slot;
	struct extent_buffer *eb;

	if (root_level == 0)
		return 1;

	while (level <= root_level) {
		eb = path->nodes[level];
		nr = btrfs_header_nritems(eb);
		path->slots[level]++;
		slot = path->slots[level];
		if (slot >= nr || level == 0) {
			/*
			 * Don't free the root -  we will detect this
			 * condition after our loop and return a
			 * positive value for caller to stop walking the tree.
			 */
			if (level != root_level) {
				btrfs_tree_unlock_rw(eb, path->locks[level]);
				path->locks[level] = 0;

				free_extent_buffer(eb);
				path->nodes[level] = NULL;
				path->slots[level] = 0;
			}
		} else {
			/*
			 * We have a valid slot to walk back down
			 * from. Stop here so caller can process these
			 * new nodes.
			 */
			break;
		}

		level++;
	}

	eb = path->nodes[root_level];
	if (path->slots[root_level] >= btrfs_header_nritems(eb))
		return 1;

	return 0;
}

/*
 * Helper function to trace a subtree tree block swap.
 *
 * The swap will happen in highest tree block, but there may be a lot of
 * tree blocks involved.
 *
 * For example:
 *  OO = Old tree blocks
 *  NN = New tree blocks allocated during balance
 *
 *           File tree (257)                  Reloc tree for 257
 * L2              OO                                NN
 *               /    \                            /    \
 * L1          OO      OO (a)                    OO      NN (a)
 *            / \     / \                       / \     / \
 * L0       OO   OO OO   OO                   OO   OO NN   NN
 *                  (b)  (c)                          (b)  (c)
 *
 * When calling qgroup_trace_extent_swap(), we will pass:
 * @src_eb = OO(a)
 * @dst_path = [ nodes[1] = NN(a), nodes[0] = NN(c) ]
 * @dst_level = 0
 * @root_level = 1
 *
 * In that case, qgroup_trace_extent_swap() will search from OO(a) to
 * reach OO(c), then mark both OO(c) and NN(c) as qgroup dirty.
 *
 * The main work of qgroup_trace_extent_swap() can be split into 3 parts:
 *
 * 1) Tree search from @src_eb
 *    It should acts as a simplified btrfs_search_slot().
 *    The key for search can be extracted from @dst_path->nodes[dst_level]
 *    (first key).
 *
 * 2) Mark the final tree blocks in @src_path and @dst_path qgroup dirty
 *    NOTE: In above case, OO(a) and NN(a) won't be marked qgroup dirty.
 *    They should be marked during previous (@dst_level = 1) iteration.
 *
 * 3) Mark file extents in leaves dirty
 *    We don't have good way to pick out new file extents only.
 *    So we still follow the old method by scanning all file extents in
 *    the leave.
 *
 * This function can free us from keeping two paths, thus later we only need
 * to care about how to iterate all new tree blocks in reloc tree.
 */
static int qgroup_trace_extent_swap(struct btrfs_trans_handle* trans,
				    struct extent_buffer *src_eb,
				    struct btrfs_path *dst_path,
				    int dst_level, int root_level,
				    bool trace_leaf)
{
	struct btrfs_key key;
	struct btrfs_path *src_path;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	u32 nodesize = fs_info->nodesize;
	int cur_level = root_level;
	int ret;

	BUG_ON(dst_level > root_level);
	/* Level mismatch */
	if (btrfs_header_level(src_eb) != root_level)
		return -EINVAL;

	src_path = btrfs_alloc_path();
	if (!src_path) {
		ret = -ENOMEM;
		goto out;
	}

	if (dst_level)
		btrfs_node_key_to_cpu(dst_path->nodes[dst_level], &key, 0);
	else
		btrfs_item_key_to_cpu(dst_path->nodes[dst_level], &key, 0);

	/* For src_path */
	atomic_inc(&src_eb->refs);
	src_path->nodes[root_level] = src_eb;
	src_path->slots[root_level] = dst_path->slots[root_level];
	src_path->locks[root_level] = 0;

	/* A simplified version of btrfs_search_slot() */
	while (cur_level >= dst_level) {
		struct btrfs_key src_key;
		struct btrfs_key dst_key;

		if (src_path->nodes[cur_level] == NULL) {
			struct extent_buffer *eb;
			int parent_slot;

			eb = src_path->nodes[cur_level + 1];
			parent_slot = src_path->slots[cur_level + 1];

			eb = btrfs_read_node_slot(eb, parent_slot);
			if (IS_ERR(eb)) {
				ret = PTR_ERR(eb);
				goto out;
			}

			src_path->nodes[cur_level] = eb;

			btrfs_tree_read_lock(eb);
			btrfs_set_lock_blocking_read(eb);
			src_path->locks[cur_level] = BTRFS_READ_LOCK_BLOCKING;
		}

		src_path->slots[cur_level] = dst_path->slots[cur_level];
		if (cur_level) {
			btrfs_node_key_to_cpu(dst_path->nodes[cur_level],
					&dst_key, dst_path->slots[cur_level]);
			btrfs_node_key_to_cpu(src_path->nodes[cur_level],
					&src_key, src_path->slots[cur_level]);
		} else {
			btrfs_item_key_to_cpu(dst_path->nodes[cur_level],
					&dst_key, dst_path->slots[cur_level]);
			btrfs_item_key_to_cpu(src_path->nodes[cur_level],
					&src_key, src_path->slots[cur_level]);
		}
		/* Content mismatch, something went wrong */
		if (btrfs_comp_cpu_keys(&dst_key, &src_key)) {
			ret = -ENOENT;
			goto out;
		}
		cur_level--;
	}

	/*
	 * Now both @dst_path and @src_path have been populated, record the tree
	 * blocks for qgroup accounting.
	 */
	ret = btrfs_qgroup_trace_extent(trans, src_path->nodes[dst_level]->start,
			nodesize, GFP_NOFS);
	if (ret < 0)
		goto out;
	ret = btrfs_qgroup_trace_extent(trans,
			dst_path->nodes[dst_level]->start,
			nodesize, GFP_NOFS);
	if (ret < 0)
		goto out;

	/* Record leaf file extents */
	if (dst_level == 0 && trace_leaf) {
		ret = btrfs_qgroup_trace_leaf_items(trans, src_path->nodes[0]);
		if (ret < 0)
			goto out;
		ret = btrfs_qgroup_trace_leaf_items(trans, dst_path->nodes[0]);
	}
out:
	btrfs_free_path(src_path);
	return ret;
}

/*
 * Helper function to do recursive generation-aware depth-first search, to
 * locate all new tree blocks in a subtree of reloc tree.
 *
 * E.g. (OO = Old tree blocks, NN = New tree blocks, whose gen == last_snapshot)
 *         reloc tree
 * L2         NN (a)
 *          /    \
 * L1    OO        NN (b)
 *      /  \      /  \
 * L0  OO  OO    OO  NN
 *               (c) (d)
 * If we pass:
 * @dst_path = [ nodes[1] = NN(b), nodes[0] = NULL ],
 * @cur_level = 1
 * @root_level = 1
 *
 * We will iterate through tree blocks NN(b), NN(d) and info qgroup to trace
 * above tree blocks along with their counter parts in file tree.
 * While during search, old tree blocks OO(c) will be skipped as tree block swap
 * won't affect OO(c).
 */
static int qgroup_trace_new_subtree_blocks(struct btrfs_trans_handle* trans,
					   struct extent_buffer *src_eb,
					   struct btrfs_path *dst_path,
					   int cur_level, int root_level,
					   u64 last_snapshot, bool trace_leaf)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct extent_buffer *eb;
	bool need_cleanup = false;
	int ret = 0;
	int i;

	/* Level sanity check */
	if (cur_level < 0 || cur_level >= BTRFS_MAX_LEVEL - 1 ||
	    root_level < 0 || root_level >= BTRFS_MAX_LEVEL - 1 ||
	    root_level < cur_level) {
		btrfs_err_rl(fs_info,
			"%s: bad levels, cur_level=%d root_level=%d",
			__func__, cur_level, root_level);
		return -EUCLEAN;
	}

	/* Read the tree block if needed */
	if (dst_path->nodes[cur_level] == NULL) {
		int parent_slot;
		u64 child_gen;

		/*
		 * dst_path->nodes[root_level] must be initialized before
		 * calling this function.
		 */
		if (cur_level == root_level) {
			btrfs_err_rl(fs_info,
	"%s: dst_path->nodes[%d] not initialized, root_level=%d cur_level=%d",
				__func__, root_level, root_level, cur_level);
			return -EUCLEAN;
		}

		/*
		 * We need to get child blockptr/gen from parent before we can
		 * read it.
		  */
		eb = dst_path->nodes[cur_level + 1];
		parent_slot = dst_path->slots[cur_level + 1];
		child_gen = btrfs_node_ptr_generation(eb, parent_slot);

		/* This node is old, no need to trace */
		if (child_gen < last_snapshot)
			goto out;

		eb = btrfs_read_node_slot(eb, parent_slot);
		if (IS_ERR(eb)) {
			ret = PTR_ERR(eb);
			goto out;
		}

		dst_path->nodes[cur_level] = eb;
		dst_path->slots[cur_level] = 0;

		btrfs_tree_read_lock(eb);
		btrfs_set_lock_blocking_read(eb);
		dst_path->locks[cur_level] = BTRFS_READ_LOCK_BLOCKING;
		need_cleanup = true;
	}

	/* Now record this tree block and its counter part for qgroups */
	ret = qgroup_trace_extent_swap(trans, src_eb, dst_path, cur_level,
				       root_level, trace_leaf);
	if (ret < 0)
		goto cleanup;

	eb = dst_path->nodes[cur_level];

	if (cur_level > 0) {
		/* Iterate all child tree blocks */
		for (i = 0; i < btrfs_header_nritems(eb); i++) {
			/* Skip old tree blocks as they won't be swapped */
			if (btrfs_node_ptr_generation(eb, i) < last_snapshot)
				continue;
			dst_path->slots[cur_level] = i;

			/* Recursive call (at most 7 times) */
			ret = qgroup_trace_new_subtree_blocks(trans, src_eb,
					dst_path, cur_level - 1, root_level,
					last_snapshot, trace_leaf);
			if (ret < 0)
				goto cleanup;
		}
	}

cleanup:
	if (need_cleanup) {
		/* Clean up */
		btrfs_tree_unlock_rw(dst_path->nodes[cur_level],
				     dst_path->locks[cur_level]);
		free_extent_buffer(dst_path->nodes[cur_level]);
		dst_path->nodes[cur_level] = NULL;
		dst_path->slots[cur_level] = 0;
		dst_path->locks[cur_level] = 0;
	}
out:
	return ret;
}

static int qgroup_trace_subtree_swap(struct btrfs_trans_handle *trans,
				struct extent_buffer *src_eb,
				struct extent_buffer *dst_eb,
				u64 last_snapshot, bool trace_leaf)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_path *dst_path = NULL;
	int level;
	int ret;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		return 0;

	/* Wrong parameter order */
	if (btrfs_header_generation(src_eb) > btrfs_header_generation(dst_eb)) {
		btrfs_err_rl(fs_info,
		"%s: bad parameter order, src_gen=%llu dst_gen=%llu", __func__,
			     btrfs_header_generation(src_eb),
			     btrfs_header_generation(dst_eb));
		return -EUCLEAN;
	}

	if (!extent_buffer_uptodate(src_eb) || !extent_buffer_uptodate(dst_eb)) {
		ret = -EIO;
		goto out;
	}

	level = btrfs_header_level(dst_eb);
	dst_path = btrfs_alloc_path();
	if (!dst_path) {
		ret = -ENOMEM;
		goto out;
	}
	/* For dst_path */
	atomic_inc(&dst_eb->refs);
	dst_path->nodes[level] = dst_eb;
	dst_path->slots[level] = 0;
	dst_path->locks[level] = 0;

	/* Do the generation aware breadth-first search */
	ret = qgroup_trace_new_subtree_blocks(trans, src_eb, dst_path, level,
					      level, last_snapshot, trace_leaf);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	btrfs_free_path(dst_path);
	if (ret < 0)
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	return ret;
}

int btrfs_qgroup_trace_subtree(struct btrfs_trans_handle *trans,
			       struct extent_buffer *root_eb,
			       u64 root_gen, int root_level)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int ret = 0;
	int level;
	struct extent_buffer *eb = root_eb;
	struct btrfs_path *path = NULL;

	BUG_ON(root_level < 0 || root_level >= BTRFS_MAX_LEVEL);
	BUG_ON(root_eb == NULL);

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		return 0;

	if (!extent_buffer_uptodate(root_eb)) {
		ret = btrfs_read_buffer(root_eb, root_gen, root_level, NULL);
		if (ret)
			goto out;
	}

	if (root_level == 0) {
		ret = btrfs_qgroup_trace_leaf_items(trans, root_eb);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * Walk down the tree.  Missing extent blocks are filled in as
	 * we go. Metadata is accounted every time we read a new
	 * extent block.
	 *
	 * When we reach a leaf, we account for file extent items in it,
	 * walk back up the tree (adjusting slot pointers as we go)
	 * and restart the search process.
	 */
	atomic_inc(&root_eb->refs);	/* For path */
	path->nodes[root_level] = root_eb;
	path->slots[root_level] = 0;
	path->locks[root_level] = 0; /* so release_path doesn't try to unlock */
walk_down:
	level = root_level;
	while (level >= 0) {
		if (path->nodes[level] == NULL) {
			int parent_slot;
			u64 child_bytenr;

			/*
			 * We need to get child blockptr from parent before we
			 * can read it.
			  */
			eb = path->nodes[level + 1];
			parent_slot = path->slots[level + 1];
			child_bytenr = btrfs_node_blockptr(eb, parent_slot);

			eb = btrfs_read_node_slot(eb, parent_slot);
			if (IS_ERR(eb)) {
				ret = PTR_ERR(eb);
				goto out;
			}

			path->nodes[level] = eb;
			path->slots[level] = 0;

			btrfs_tree_read_lock(eb);
			btrfs_set_lock_blocking_read(eb);
			path->locks[level] = BTRFS_READ_LOCK_BLOCKING;

			ret = btrfs_qgroup_trace_extent(trans, child_bytenr,
							fs_info->nodesize,
							GFP_NOFS);
			if (ret)
				goto out;
		}

		if (level == 0) {
			ret = btrfs_qgroup_trace_leaf_items(trans,
							    path->nodes[level]);
			if (ret)
				goto out;

			/* Nonzero return here means we completed our search */
			ret = adjust_slots_upwards(path, root_level);
			if (ret)
				break;

			/* Restart search with new slots */
			goto walk_down;
		}

		level--;
	}

	ret = 0;
out:
	btrfs_free_path(path);

	return ret;
}

#define UPDATE_NEW	0
#define UPDATE_OLD	1
/*
 * Walk all of the roots that points to the bytenr and adjust their refcnts.
 */
static int qgroup_update_refcnt(struct btrfs_fs_info *fs_info,
				struct ulist *roots, struct ulist *tmp,
				struct ulist *qgroups, u64 seq, int update_old)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct ulist_node *tmp_unode;
	struct ulist_iterator tmp_uiter;
	struct btrfs_qgroup *qg;
	int ret = 0;

	if (!roots)
		return 0;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(roots, &uiter))) {
		qg = find_qgroup_rb(fs_info, unode->val);
		if (!qg)
			continue;

		ulist_reinit(tmp);
		ret = ulist_add(qgroups, qg->qgroupid, qgroup_to_aux(qg),
				GFP_ATOMIC);
		if (ret < 0)
			return ret;
		ret = ulist_add(tmp, qg->qgroupid, qgroup_to_aux(qg), GFP_ATOMIC);
		if (ret < 0)
			return ret;
		ULIST_ITER_INIT(&tmp_uiter);
		while ((tmp_unode = ulist_next(tmp, &tmp_uiter))) {
			struct btrfs_qgroup_list *glist;

			qg = unode_aux_to_qgroup(tmp_unode);
			if (update_old)
				btrfs_qgroup_update_old_refcnt(qg, seq, 1);
			else
				btrfs_qgroup_update_new_refcnt(qg, seq, 1);
			list_for_each_entry(glist, &qg->groups, next_group) {
				ret = ulist_add(qgroups, glist->group->qgroupid,
						qgroup_to_aux(glist->group),
						GFP_ATOMIC);
				if (ret < 0)
					return ret;
				ret = ulist_add(tmp, glist->group->qgroupid,
						qgroup_to_aux(glist->group),
						GFP_ATOMIC);
				if (ret < 0)
					return ret;
			}
		}
	}
	return 0;
}

/*
 * Update qgroup rfer/excl counters.
 * Rfer update is easy, codes can explain themselves.
 *
 * Excl update is tricky, the update is split into 2 parts.
 * Part 1: Possible exclusive <-> sharing detect:
 *	|	A	|	!A	|
 *  -------------------------------------
 *  B	|	*	|	-	|
 *  -------------------------------------
 *  !B	|	+	|	**	|
 *  -------------------------------------
 *
 * Conditions:
 * A:	cur_old_roots < nr_old_roots	(not exclusive before)
 * !A:	cur_old_roots == nr_old_roots	(possible exclusive before)
 * B:	cur_new_roots < nr_new_roots	(not exclusive now)
 * !B:	cur_new_roots == nr_new_roots	(possible exclusive now)
 *
 * Results:
 * +: Possible sharing -> exclusive	-: Possible exclusive -> sharing
 * *: Definitely not changed.		**: Possible unchanged.
 *
 * For !A and !B condition, the exception is cur_old/new_roots == 0 case.
 *
 * To make the logic clear, we first use condition A and B to split
 * combination into 4 results.
 *
 * Then, for result "+" and "-", check old/new_roots == 0 case, as in them
 * only on variant maybe 0.
 *
 * Lastly, check result **, since there are 2 variants maybe 0, split them
 * again(2x2).
 * But this time we don't need to consider other things, the codes and logic
 * is easy to understand now.
 */
static int qgroup_update_counters(struct btrfs_fs_info *fs_info,
				  struct ulist *qgroups,
				  u64 nr_old_roots,
				  u64 nr_new_roots,
				  u64 num_bytes, u64 seq)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct btrfs_qgroup *qg;
	u64 cur_new_count, cur_old_count;

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(qgroups, &uiter))) {
		bool dirty = false;

		qg = unode_aux_to_qgroup(unode);
		cur_old_count = btrfs_qgroup_get_old_refcnt(qg, seq);
		cur_new_count = btrfs_qgroup_get_new_refcnt(qg, seq);

		trace_qgroup_update_counters(fs_info, qg, cur_old_count,
					     cur_new_count);

		/* Rfer update part */
		if (cur_old_count == 0 && cur_new_count > 0) {
			qg->rfer += num_bytes;
			qg->rfer_cmpr += num_bytes;
			dirty = true;
		}
		if (cur_old_count > 0 && cur_new_count == 0) {
			qg->rfer -= num_bytes;
			qg->rfer_cmpr -= num_bytes;
			dirty = true;
		}

		/* Excl update part */
		/* Exclusive/none -> shared case */
		if (cur_old_count == nr_old_roots &&
		    cur_new_count < nr_new_roots) {
			/* Exclusive -> shared */
			if (cur_old_count != 0) {
				qg->excl -= num_bytes;
				qg->excl_cmpr -= num_bytes;
				dirty = true;
			}
		}

		/* Shared -> exclusive/none case */
		if (cur_old_count < nr_old_roots &&
		    cur_new_count == nr_new_roots) {
			/* Shared->exclusive */
			if (cur_new_count != 0) {
				qg->excl += num_bytes;
				qg->excl_cmpr += num_bytes;
				dirty = true;
			}
		}

		/* Exclusive/none -> exclusive/none case */
		if (cur_old_count == nr_old_roots &&
		    cur_new_count == nr_new_roots) {
			if (cur_old_count == 0) {
				/* None -> exclusive/none */

				if (cur_new_count != 0) {
					/* None -> exclusive */
					qg->excl += num_bytes;
					qg->excl_cmpr += num_bytes;
					dirty = true;
				}
				/* None -> none, nothing changed */
			} else {
				/* Exclusive -> exclusive/none */

				if (cur_new_count == 0) {
					/* Exclusive -> none */
					qg->excl -= num_bytes;
					qg->excl_cmpr -= num_bytes;
					dirty = true;
				}
				/* Exclusive -> exclusive, nothing changed */
			}
		}

		if (dirty)
			qgroup_dirty(fs_info, qg);
	}
	return 0;
}

/*
 * Check if the @roots potentially is a list of fs tree roots
 *
 * Return 0 for definitely not a fs/subvol tree roots ulist
 * Return 1 for possible fs/subvol tree roots in the list (considering an empty
 *          one as well)
 */
static int maybe_fs_roots(struct ulist *roots)
{
	struct ulist_node *unode;
	struct ulist_iterator uiter;

	/* Empty one, still possible for fs roots */
	if (!roots || roots->nnodes == 0)
		return 1;

	ULIST_ITER_INIT(&uiter);
	unode = ulist_next(roots, &uiter);
	if (!unode)
		return 1;

	/*
	 * If it contains fs tree roots, then it must belong to fs/subvol
	 * trees.
	 * If it contains a non-fs tree, it won't be shared with fs/subvol trees.
	 */
	return is_fstree(unode->val);
}

int btrfs_qgroup_account_extent(struct btrfs_trans_handle *trans, u64 bytenr,
				u64 num_bytes, struct ulist *old_roots,
				struct ulist *new_roots)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct ulist *qgroups = NULL;
	struct ulist *tmp = NULL;
	u64 seq;
	u64 nr_new_roots = 0;
	u64 nr_old_roots = 0;
	int ret = 0;

	/*
	 * If quotas get disabled meanwhile, the resouces need to be freed and
	 * we can't just exit here.
	 */
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		goto out_free;

	if (new_roots) {
		if (!maybe_fs_roots(new_roots))
			goto out_free;
		nr_new_roots = new_roots->nnodes;
	}
	if (old_roots) {
		if (!maybe_fs_roots(old_roots))
			goto out_free;
		nr_old_roots = old_roots->nnodes;
	}

	/* Quick exit, either not fs tree roots, or won't affect any qgroup */
	if (nr_old_roots == 0 && nr_new_roots == 0)
		goto out_free;

	BUG_ON(!fs_info->quota_root);

	trace_btrfs_qgroup_account_extent(fs_info, trans->transid, bytenr,
					num_bytes, nr_old_roots, nr_new_roots);

	qgroups = ulist_alloc(GFP_NOFS);
	if (!qgroups) {
		ret = -ENOMEM;
		goto out_free;
	}
	tmp = ulist_alloc(GFP_NOFS);
	if (!tmp) {
		ret = -ENOMEM;
		goto out_free;
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		if (fs_info->qgroup_rescan_progress.objectid <= bytenr) {
			mutex_unlock(&fs_info->qgroup_rescan_lock);
			ret = 0;
			goto out_free;
		}
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	spin_lock(&fs_info->qgroup_lock);
	seq = fs_info->qgroup_seq;

	/* Update old refcnts using old_roots */
	ret = qgroup_update_refcnt(fs_info, old_roots, tmp, qgroups, seq,
				   UPDATE_OLD);
	if (ret < 0)
		goto out;

	/* Update new refcnts using new_roots */
	ret = qgroup_update_refcnt(fs_info, new_roots, tmp, qgroups, seq,
				   UPDATE_NEW);
	if (ret < 0)
		goto out;

	qgroup_update_counters(fs_info, qgroups, nr_old_roots, nr_new_roots,
			       num_bytes, seq);

	/*
	 * Bump qgroup_seq to avoid seq overlap
	 */
	fs_info->qgroup_seq += max(nr_old_roots, nr_new_roots) + 1;
out:
	spin_unlock(&fs_info->qgroup_lock);
out_free:
	ulist_free(tmp);
	ulist_free(qgroups);
	ulist_free(old_roots);
	ulist_free(new_roots);
	return ret;
}

int btrfs_qgroup_account_extents(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_qgroup_extent_record *record;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct ulist *new_roots = NULL;
	struct rb_node *node;
	u64 num_dirty_extents = 0;
	u64 qgroup_to_skip;
	int ret = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	qgroup_to_skip = delayed_refs->qgroup_to_skip;
	while ((node = rb_first(&delayed_refs->dirty_extent_root))) {
		record = rb_entry(node, struct btrfs_qgroup_extent_record,
				  node);

		num_dirty_extents++;
		trace_btrfs_qgroup_account_extents(fs_info, record);

		if (!ret) {
			/*
			 * Old roots should be searched when inserting qgroup
			 * extent record
			 */
			if (WARN_ON(!record->old_roots)) {
				/* Search commit root to find old_roots */
				ret = btrfs_find_all_roots(NULL, fs_info,
						record->bytenr, 0,
						&record->old_roots, false);
				if (ret < 0)
					goto cleanup;
			}

			/* Free the reserved data space */
			btrfs_qgroup_free_refroot(fs_info,
					record->data_rsv_refroot,
					record->data_rsv,
					BTRFS_QGROUP_RSV_DATA);
			/*
			 * Use SEQ_LAST as time_seq to do special search, which
			 * doesn't lock tree or delayed_refs and search current
			 * root. It's safe inside commit_transaction().
			 */
			ret = btrfs_find_all_roots(trans, fs_info,
				record->bytenr, SEQ_LAST, &new_roots, false);
			if (ret < 0)
				goto cleanup;
			if (qgroup_to_skip) {
				ulist_del(new_roots, qgroup_to_skip, 0);
				ulist_del(record->old_roots, qgroup_to_skip,
					  0);
			}
			ret = btrfs_qgroup_account_extent(trans, record->bytenr,
							  record->num_bytes,
							  record->old_roots,
							  new_roots);
			record->old_roots = NULL;
			new_roots = NULL;
		}
cleanup:
		ulist_free(record->old_roots);
		ulist_free(new_roots);
		new_roots = NULL;
		rb_erase(node, &delayed_refs->dirty_extent_root);
		kfree(record);

	}
	trace_qgroup_num_dirty_extents(fs_info, trans->transid,
				       num_dirty_extents);
	return ret;
}
#endif /* MY_ABC_HERE */

/*
 * called from commit_transaction. Writes all changed qgroups to disk.
 */
int btrfs_run_qgroups(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	int ret = 0;

	if (!fs_info->quota_root)
		return ret;

	spin_lock(&fs_info->qgroup_lock);
	while (!list_empty(&fs_info->dirty_qgroups)) {
		struct btrfs_qgroup *qgroup;
		qgroup = list_first_entry(&fs_info->dirty_qgroups,
					  struct btrfs_qgroup, dirty);
		list_del_init(&qgroup->dirty);
		spin_unlock(&fs_info->qgroup_lock);
		ret = update_qgroup_info_item(trans, qgroup);
#ifdef MY_ABC_HERE
		if ((ret || qgroup->need_rescan) &&
				test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags)) {
			struct syno_quota_rescan_item_updater updater;

			syno_quota_rescan_item_init(&updater);
			updater.flags = SYNO_QUOTA_RESCAN_NEED;
			btrfs_add_update_syno_quota_rescan_item(trans,
				fs_info->quota_root,
				btrfs_qgroup_subvolid(qgroup->qgroupid), &updater);
			qgroup->need_rescan = false;
		}
#else
		if (ret)
			fs_info->qgroup_flags |=
					BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
#endif /* MY_ABC_HERE */
		ret = update_qgroup_limit_item(trans, qgroup);
		if (ret)
			fs_info->qgroup_flags |=
					BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
		spin_lock(&fs_info->qgroup_lock);
	}
#ifdef MY_ABC_HERE
	if (test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags) ||
		test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags))
#else
	if (test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
#endif /* MY_ABC_HERE */
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_ON;
	else
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_ON;
	spin_unlock(&fs_info->qgroup_lock);

	ret = update_qgroup_status_item(trans);
	if (ret)
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;

	return ret;
}

/*
 * Copy the accounting information between qgroups. This is necessary
 * when a snapshot or a subvolume is created. Throwing an error will
 * cause a transaction abort so we take extra care here to only error
 * when a readonly fs is a reasonable outcome.
 */
int btrfs_qgroup_inherit(struct btrfs_trans_handle *trans, u64 srcid,
			 u64 objectid, struct btrfs_qgroup_inherit *inherit)
{
	int ret = 0;
	int i;
	u64 *i_qgroups;
	bool committing = false;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root;
	struct btrfs_qgroup *srcgroup;
	struct btrfs_qgroup *dstgroup;
	bool need_rescan = false;
	u32 level_size = 0;
	u64 nums;

	/*
	 * There are only two callers of this function.
	 *
	 * One in create_subvol() in the ioctl context, which needs to hold
	 * the qgroup_ioctl_lock.
	 *
	 * The other one in create_pending_snapshot() where no other qgroup
	 * code can modify the fs as they all need to either start a new trans
	 * or hold a trans handler, thus we don't need to hold
	 * qgroup_ioctl_lock.
	 * This would avoid long and complex lock chain and make lockdep happy.
	 */
	spin_lock(&fs_info->trans_lock);
	if (trans->transaction->state == TRANS_STATE_COMMIT_DOING)
		committing = true;
	spin_unlock(&fs_info->trans_lock);

	if (!committing)
		mutex_lock(&fs_info->qgroup_ioctl_lock);
#ifdef MY_ABC_HERE
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags) &&
		!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags))
#else
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
#endif /* MY_ABC_HERE */
		goto out;

	quota_root = fs_info->quota_root;
	if (!quota_root) {
#ifdef MY_ABC_HERE
		ret = -ESRCH;
#else
		ret = -EINVAL;
#endif /* MY_ABC_HERE */
		goto out;
	}

	if (inherit) {
		i_qgroups = (u64 *)(inherit + 1);
		nums = inherit->num_qgroups + 2 * inherit->num_ref_copies +
		       2 * inherit->num_excl_copies;
		for (i = 0; i < nums; ++i) {
			srcgroup = find_qgroup_rb(fs_info, *i_qgroups);

			/*
			 * Zero out invalid groups so we can ignore
			 * them later.
			 */
			if (!srcgroup ||
			    ((srcgroup->qgroupid >> 48) <= (objectid >> 48)))
				*i_qgroups = 0ULL;

			++i_qgroups;
		}
	}

	/*
	 * create a tracking group for the subvol itself
	 */
	ret = add_qgroup_item(trans, quota_root, objectid);
	if (ret)
		goto out;

	/*
	 * add qgroup to all inherited groups
	 */
	if (inherit) {
		i_qgroups = (u64 *)(inherit + 1);
		for (i = 0; i < inherit->num_qgroups; ++i, ++i_qgroups) {
			if (*i_qgroups == 0)
				continue;
			ret = add_qgroup_relation_item(trans, objectid,
						       *i_qgroups);
			if (ret && ret != -EEXIST)
				goto out;
			ret = add_qgroup_relation_item(trans, *i_qgroups,
						       objectid);
			if (ret && ret != -EEXIST)
				goto out;
		}
		ret = 0;
	}


	spin_lock(&fs_info->qgroup_lock);

	dstgroup = add_qgroup_rb(fs_info, objectid);
	if (IS_ERR(dstgroup)) {
		ret = PTR_ERR(dstgroup);
		goto unlock;
	}

	if (inherit && inherit->flags & BTRFS_QGROUP_INHERIT_SET_LIMITS) {
		dstgroup->lim_flags = inherit->lim.flags;
		dstgroup->max_rfer = inherit->lim.max_rfer;
		dstgroup->max_excl = inherit->lim.max_excl;
		dstgroup->rsv_rfer = inherit->lim.rsv_rfer;
		dstgroup->rsv_excl = inherit->lim.rsv_excl;

		ret = update_qgroup_limit_item(trans, dstgroup);
		if (ret) {
#ifdef MY_ABC_HERE
#else
			fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
#endif /* MY_ABC_HERE */
			btrfs_info(fs_info,
				   "unable to update quota limit for %llu",
				   dstgroup->qgroupid);
			goto unlock;
		}
	}

	if (srcid) {
		srcgroup = find_qgroup_rb(fs_info, srcid);
		if (!srcgroup)
			goto unlock;

		/*
		 * We call inherit after we clone the root in order to make sure
		 * our counts don't go crazy, so at this point the only
		 * difference between the two roots should be the root node.
		 */
		level_size = fs_info->nodesize;
#ifdef MY_ABC_HERE
		// In quota 2.0, we don't count metadata quota.
		level_size = 0;
#endif /* MY_ABC_HERE */
		dstgroup->rfer = srcgroup->rfer;
		dstgroup->rfer_cmpr = srcgroup->rfer_cmpr;
		dstgroup->excl = level_size;
		dstgroup->excl_cmpr = level_size;
		srcgroup->excl = level_size;
		srcgroup->excl_cmpr = level_size;

		/* inherit the limit info */
		dstgroup->lim_flags = srcgroup->lim_flags;
		dstgroup->max_rfer = srcgroup->max_rfer;
		dstgroup->max_excl = srcgroup->max_excl;
		dstgroup->rsv_rfer = srcgroup->rsv_rfer;
		dstgroup->rsv_excl = srcgroup->rsv_excl;

		qgroup_dirty(fs_info, dstgroup);
		qgroup_dirty(fs_info, srcgroup);
	}

	if (!inherit)
		goto unlock;

	i_qgroups = (u64 *)(inherit + 1);
	for (i = 0; i < inherit->num_qgroups; ++i) {
		if (*i_qgroups) {
			ret = add_relation_rb(fs_info, objectid, *i_qgroups);
			if (ret)
				goto unlock;
		}
		++i_qgroups;

		/*
		 * If we're doing a snapshot, and adding the snapshot to a new
		 * qgroup, the numbers are guaranteed to be incorrect.
		 */
		if (srcid)
			need_rescan = true;
	}

	for (i = 0; i <  inherit->num_ref_copies; ++i, i_qgroups += 2) {
		struct btrfs_qgroup *src;
		struct btrfs_qgroup *dst;

		if (!i_qgroups[0] || !i_qgroups[1])
			continue;

		src = find_qgroup_rb(fs_info, i_qgroups[0]);
		dst = find_qgroup_rb(fs_info, i_qgroups[1]);

		if (!src || !dst) {
			ret = -EINVAL;
			goto unlock;
		}

		dst->rfer = src->rfer - level_size;
		dst->rfer_cmpr = src->rfer_cmpr - level_size;

		/* Manually tweaking numbers certainly needs a rescan */
		need_rescan = true;
	}
	for (i = 0; i <  inherit->num_excl_copies; ++i, i_qgroups += 2) {
		struct btrfs_qgroup *src;
		struct btrfs_qgroup *dst;

		if (!i_qgroups[0] || !i_qgroups[1])
			continue;

		src = find_qgroup_rb(fs_info, i_qgroups[0]);
		dst = find_qgroup_rb(fs_info, i_qgroups[1]);

		if (!src || !dst) {
			ret = -EINVAL;
			goto unlock;
		}

		dst->excl = src->excl + level_size;
		dst->excl_cmpr = src->excl_cmpr + level_size;
		need_rescan = true;
	}

unlock:
	spin_unlock(&fs_info->qgroup_lock);
	if (!ret)
		ret = btrfs_sysfs_add_one_qgroup(fs_info, dstgroup);
out:
	if (!committing)
		mutex_unlock(&fs_info->qgroup_ioctl_lock);
#ifdef MY_ABC_HERE
// We don't use exclusive quota, so don't need rescan here.
#else
	if (need_rescan)
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
#endif /* MY_ABC_HERE */
	return ret;
}

static bool qgroup_check_limits(const struct btrfs_qgroup *qg, u64 num_bytes)
{
	if ((qg->lim_flags & BTRFS_QGROUP_LIMIT_MAX_RFER) &&
	    qgroup_rsv_total(qg) + (s64)qg->rfer + num_bytes > qg->max_rfer)
		return false;

	if ((qg->lim_flags & BTRFS_QGROUP_LIMIT_MAX_EXCL) &&
	    qgroup_rsv_total(qg) + (s64)qg->excl + num_bytes > qg->max_excl)
		return false;

	return true;
}

#ifdef MY_ABC_HERE
/*
 * Return 1 if we don't reserve qgroup, but it's not an EDQUOT error.
 * Caller is allowed to write.
 */
#endif /* MY_ABC_HERE */
static int qgroup_reserve(struct btrfs_root *root, u64 num_bytes, bool enforce,
			  enum btrfs_qgroup_rsv_type type)
{
	struct btrfs_qgroup *qgroup;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 ref_root = root->root_key.objectid;
	int ret = 0;
	struct ulist_node *unode;
	struct ulist_iterator uiter;

	if (!is_fstree(ref_root))
		return 0;

	if (num_bytes == 0)
		return 0;

	if (test_bit(BTRFS_FS_QUOTA_OVERRIDE, &fs_info->flags) &&
	    capable(CAP_SYS_RESOURCE))
		enforce = false;

	spin_lock(&fs_info->qgroup_lock);
#ifdef MY_ABC_HERE
	if (!fs_info->quota_root) {
		ret = 1;
		goto out;
	}
#else
	if (!fs_info->quota_root)
		goto out;
#endif /* MY_ABC_HERE */

	qgroup = find_qgroup_rb(fs_info, ref_root);
#ifdef MY_ABC_HERE
	if (!qgroup) {
		ret = 1;
		goto out;
	}
#else
	if (!qgroup)
		goto out;
#endif /* MY_ABC_HERE */

	/*
	 * in a first step, we check all affected qgroups if any limits would
	 * be exceeded
	 */
	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
			qgroup_to_aux(qgroup), GFP_ATOMIC);
	if (ret < 0)
		goto out;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = unode_aux_to_qgroup(unode);

		if (enforce && !qgroup_check_limits(qg, num_bytes)
#ifdef MY_ABC_HERE
			    && !root->invalid_quota
#endif /* MY_ABC_HERE */
		) {
			ret = -EDQUOT;
			goto out;
		}

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					qgroup_to_aux(glist->group), GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
	ret = 0;
	/*
	 * no limits exceeded, now record the reservation into all qgroups
	 */
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;

		qg = unode_aux_to_qgroup(unode);

		qgroup_rsv_add(fs_info, qg, num_bytes, type);
	}

out:
	spin_unlock(&fs_info->qgroup_lock);
	return ret;
}

/*
 * Free @num_bytes of reserved space with @type for qgroup.  (Normally level 0
 * qgroup).
 *
 * Will handle all higher level qgroup too.
 *
 * NOTE: If @num_bytes is (u64)-1, this means to free all bytes of this qgroup.
 * This special case is only used for META_PERTRANS type.
 */
void btrfs_qgroup_free_refroot(struct btrfs_fs_info *fs_info,
			       u64 ref_root, u64 num_bytes,
			       enum btrfs_qgroup_rsv_type type)
{
	struct btrfs_qgroup *qgroup;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	int ret = 0;

	if (!is_fstree(ref_root))
		return;

	if (num_bytes == 0)
		return;

	if (num_bytes == (u64)-1 && type != BTRFS_QGROUP_RSV_META_PERTRANS) {
		WARN(1, "%s: Invalid type to free", __func__);
		return;
	}
	spin_lock(&fs_info->qgroup_lock);

	if (!fs_info->quota_root)
		goto out;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	if (num_bytes == (u64)-1)
		/*
		 * We're freeing all pertrans rsv, get reserved value from
		 * level 0 qgroup as real num_bytes to free.
		 */
		num_bytes = qgroup->rsv.values[type];

	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
			qgroup_to_aux(qgroup), GFP_ATOMIC);
	if (ret < 0)
		goto out;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = unode_aux_to_qgroup(unode);

		qgroup_rsv_release(fs_info, qg, num_bytes, type);

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					qgroup_to_aux(glist->group), GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}

out:
	spin_unlock(&fs_info->qgroup_lock);
}

#ifdef MY_ABC_HERE
int btrfs_qgroup_syno_reserve(struct btrfs_root *root, u64 num_bytes)
{
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &root->fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &root->fs_info->flags))
		return 0;

	if (btrfs_root_disable_quota(root))
		return 0;

	num_bytes = round_up(num_bytes, root->fs_info->sectorsize);
	return qgroup_reserve(root, num_bytes, true, BTRFS_QGROUP_RSV_DATA);
}

void btrfs_qgroup_syno_free(struct btrfs_root *root, u64 num_bytes)
{
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &root->fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &root->fs_info->flags))
		return;

	if (btrfs_root_disable_quota(root))
		return;

	num_bytes = round_up(num_bytes, root->fs_info->sectorsize);
	return btrfs_qgroup_free_refroot(root->fs_info,
                               root->root_key.objectid, num_bytes,
                               BTRFS_QGROUP_RSV_DATA);
}

/*
 * Copied from btrfs_qgroup_free_refroot()
 * Use after inode_add_bytes() / inode_sub_bytes(), so we are always in a transaction
 * and our accounting will be committed in btrfs_run_qgroups().
 */
int btrfs_qgroup_syno_accounting(struct btrfs_inode *b_inode,
		u64 add_bytes, u64 del_bytes, enum syno_quota_account_type type)
{
	struct btrfs_qgroup *qgroup;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	struct btrfs_root *root = b_inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 ref_root = root->root_key.objectid;
	u64 ino = b_inode->location.objectid;
	int ret = 0;
#ifdef MY_ABC_HERE
	u64 soft_qgroup_subvol_id = 0;
	u64 soft_qgroup_limit = 0;
	u64 soft_qgroup_used = 0;
	bool over_limit;
#endif /* MY_ABC_HERE */

	if (!is_fstree(ref_root))
		return -EINVAL;

	if (add_bytes == del_bytes && type != UPDATE_QUOTA_FREE_RESERVED)
		return 0;

	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags))
		return 0;

	if (btrfs_root_disable_quota(root))
		return 0;

	spin_lock(&fs_info->qgroup_lock);

	if (!fs_info->quota_root)
		goto out;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	add_bytes = round_up(add_bytes, fs_info->sectorsize);
	del_bytes = round_up(del_bytes, fs_info->sectorsize);

	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
			qgroup_to_aux(qgroup), GFP_ATOMIC);
	if (ret < 0) {
		qgroup->need_rescan = true;
		goto out;
	}
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = unode_aux_to_qgroup(unode);

		switch (type) {
		case ADD_QUOTA_RESCAN:
			qg->rfer += add_bytes;
#ifdef MY_ABC_HERE
			if (!soft_qgroup_subvol_id)
				prepare_netlink_notification(qg, &soft_qgroup_subvol_id,
					&soft_qgroup_limit, &soft_qgroup_used, &over_limit);
#endif /* MY_ABC_HERE */
			break;
		case UPDATE_QUOTA_FREE_RESERVED:
			qgroup_rsv_release(fs_info, qg, add_bytes, BTRFS_QGROUP_RSV_DATA);
			/* fall through */
		case UPDATE_QUOTA:
			if (btrfs_quota_rescan_check(root, ino)) {
				qg->rfer += add_bytes;

				if (qg->rfer < del_bytes) {
					if (!root->invalid_quota)
						WARN_ONCE(1, "qgroup %llu ref underflow, have "
							"%llu to free %llu", qgroup->qgroupid, qg->rfer, del_bytes);
					qg->rfer = 0;
					qg->need_rescan = true;
				} else
					qg->rfer -= del_bytes;

#ifdef MY_ABC_HERE
				if (!soft_qgroup_subvol_id)
					prepare_netlink_notification(qg, &soft_qgroup_subvol_id,
						&soft_qgroup_limit, &soft_qgroup_used, &over_limit);
#endif /* MY_ABC_HERE */
			}
			break;
		}

		qgroup_dirty(fs_info, qg);

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					qgroup_to_aux(glist->group), GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
	ret = 0;

out:
	spin_unlock(&fs_info->qgroup_lock);
#ifdef MY_ABC_HERE
	if (soft_qgroup_subvol_id && (add_bytes != del_bytes))
		send_netlink_notification(fs_info, soft_qgroup_subvol_id,
			soft_qgroup_limit, soft_qgroup_used,
			(over_limit)? QGROUP_NL_C_OVER_LIMIT : QGROUP_NL_C_UNDER_LIMIT);
#endif /* MY_ABC_HERE */
	return ret;
}

// Similar to btrfs_qgroup_syno_accounting().
int btrfs_qgroup_syno_v1_accounting(struct btrfs_fs_info *fs_info,
				struct btrfs_quota_account_rec *record)
{
	struct btrfs_qgroup *qgroup;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	u64 ref_root = record->ref_root;
	u64 num_bytes = record->num_bytes;
	u64 ram_bytes = record->ram_bytes;
	u64 reserved = record->reserved;
	int sign = record->sign;
	int ret = 0;

	if (!is_fstree(ref_root))
		return -EINVAL;

	spin_lock(&fs_info->qgroup_lock);

	if (!fs_info->quota_root)
		goto out;

	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;

	num_bytes = round_up(num_bytes, fs_info->sectorsize);

	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
			qgroup_to_aux(qgroup), GFP_ATOMIC);
	if (ret < 0)
		goto out;

	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = unode_aux_to_qgroup(unode);

		if (unlikely(sign < 0 && qg->rfer < num_bytes)) {
			/*WARN_ONCE(1, "qgroup %llu ref underflow, have "
				"%llu to free %llu", qg->qgroupid, qg->rfer, num_bytes);*/
			qg->rfer = 0;
		} else
			qg->rfer += sign * num_bytes;

		if (unlikely(sign < 0 && qg->rfer_cmpr < ram_bytes)) {
			/*WARN_ONCE(1, "qgroup %llu rfer_cmpr underflow, have "
				"%llu to free %llu", qg->qgroupid, qg->rfer_cmpr, ram_bytes);*/
			qg->rfer_cmpr = 0;
		} else
			qg->rfer_cmpr += sign * ram_bytes;

		if (unlikely(sign > 0 && qg->rsv.values[BTRFS_QGROUP_RSV_DATA] < reserved)) {
			WARN_ONCE(1, "qgroup %llu reserved space underflow, have %llu to free %llu",
				qg->qgroupid, qg->rsv.values[BTRFS_QGROUP_RSV_DATA], reserved);
			qg->rsv.values[BTRFS_QGROUP_RSV_DATA] = 0;
		} else
			qg->rsv.values[BTRFS_QGROUP_RSV_DATA] -= reserved;

		qgroup_dirty(fs_info, qg);

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					qgroup_to_aux(glist->group), GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
	ret = 0;

out:
	spin_unlock(&fs_info->qgroup_lock);
	return ret;
}

/*
 * Similar to btrfs_qgroup_syno_accounting(), but used only in rescan, where
 * we don't have in-memory inode.
 */
static int btrfs_qgroup_syno_accounting_rescan(struct btrfs_root *root, u64 num_bytes)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_qgroup *qgroup;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	u64 subvol_id = root->root_key.objectid;
	int ret = 0;

	if (num_bytes == 0)
		return 0;

	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags))
		return 0;

	spin_lock(&fs_info->qgroup_lock);

	if (!fs_info->quota_root)
		goto out;

	num_bytes = round_up(num_bytes, fs_info->sectorsize);
	qgroup = find_qgroup_rb(fs_info, subvol_id);
	if (!qgroup)
		goto out;

	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
			qgroup_to_aux(qgroup), GFP_ATOMIC);
	if (ret < 0)
		goto out;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = unode_aux_to_qgroup(unode);

		qg->rfer += num_bytes;
		qgroup_dirty(fs_info, qg);

		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					qgroup_to_aux(glist->group), GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
	ret = 0;

out:
	spin_unlock(&fs_info->qgroup_lock);
	return ret;
}

#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
// Progress path can be ctx->current_path or ctx->end_path.
static void syno_quota_update_progress_path(int progress_path[BTRFS_MAX_LEVEL][2],
		struct btrfs_path *path)
{
	int i;

	for (i = 1; i < BTRFS_MAX_LEVEL; i++) {
		if (path->nodes[i]) {
			progress_path[i][0] = btrfs_header_nritems(path->nodes[i]);
			progress_path[i][1] = path->slots[i];
		} else {
			progress_path[i][0] = 0;
			progress_path[i][1] = 0;
		}
	}
}

/*
 * Return 0 when more leafs are to be scanned.
 * Return 1 when done.
 * Never return -1 since we can try next subvol if error occurs.
 */
static int syno_quota_rescan_leaf(struct btrfs_trans_handle *trans,
			      struct btrfs_path *path)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct syno_quota_rescan_ctx *ctx = fs_info->syno_quota_rescan_ctx;
	struct ulist *ulist = fs_info->syno_quota_rescan_subvol_ulist;
	struct ulist_node *node;
	struct btrfs_root *root;
	struct extent_buffer *leaf;
	struct btrfs_key key, found_key;
	struct inode *inode;
	struct btrfs_inode_item *inode_item;
	struct syno_quota_rescan_item_updater updater;
	u64 subvol_id;
	u64 ino;
	u64 max_objectid = 0;
	u64 num_bytes;
	u64 uid;
	int nritems;
	int ret = 0;
	int err = 0;

	if (unlikely(!ctx || !ulist)) {
		WARN_ON_ONCE(1);
		return 1;
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (list_empty(&ulist->nodes)) {
		mutex_unlock(&fs_info->qgroup_rescan_lock);
		return 1;
	}
	node = list_entry(ulist->nodes.next, struct ulist_node, list);
	subvol_id = node->val;
	ino = node->aux;
	ino++;
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	root = btrfs_get_fs_root(fs_info, subvol_id, true);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		btrfs_err(fs_info, "Failed to call btrfs_get_fs_root() for root %llu, ret = %d", subvol_id, ret);
		goto error_clean;
	}

	if (unlikely(btrfs_root_dead(root))) {
		btrfs_put_root(root);
		goto error_clean;
	}

	// We have changed to another root, reset progress info.
	if (ctx->subvol_id != subvol_id) {
		struct btrfs_syno_quota_rescan_item rescan_item;

		ret = btrfs_read_syno_quota_rescan_item(fs_info->quota_root, subvol_id, &rescan_item);
		if (ret)
			goto error_clean;

		ctx->subvol_id = subvol_id;
		ctx->subvol_size = rescan_item.tree_size;
		ctx->subvol_progress = 0;
		memset(ctx->current_path, 0, sizeof(ctx->current_path));
	}

	key.objectid = ino;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

search_again:
	ret = btrfs_search_slot_for_read(root, &key, path, 1, 0);
	if (ret < 0) {
		root->invalid_quota = false;
		btrfs_put_root(root);
		btrfs_err(fs_info, "btrfs_search_slot() failed in root %llu, ret = %d", subvol_id, ret);
		goto error_clean;
	} else if (ret > 0)
		goto out;

	leaf = path->nodes[0];
	nritems = btrfs_header_nritems(path->nodes[0]);
	if (nritems) {
		btrfs_item_key_to_cpu(leaf, &found_key, nritems - 1);
		max_objectid = found_key.objectid;
		syno_quota_update_progress_path(ctx->current_path, path);
	}

next_slot:
	if (path->slots[0] >= nritems)
		goto out;
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
	if (found_key.type != BTRFS_INODE_ITEM_KEY || found_key.offset != 0) {
		if (found_key.objectid == max_objectid)
			goto out;

		path->slots[0]++;
		goto next_slot;
	}

	ino = found_key.objectid;
	if (ino > root->rescan_end_inode)
		goto out;

	down_write(&root->rescan_lock);
	if (!btrfs_test_inode_nowait(fs_info->sb, ino, root)) {
		inode_item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_inode_item);
		num_bytes = btrfs_inode_nbytes(leaf, inode_item);
		uid = btrfs_inode_uid(leaf, inode_item);

		ret = btrfs_qgroup_syno_accounting_rescan(root, num_bytes);
		if (ret) {
			btrfs_warn(fs_info, "Failed in btrfs_qgroup_syno_accounting_rescan(), "
				"subvol_id = %llu, ino = %llu, ret = %d", subvol_id, ino, ret);
			err = ret;
		}

		ret = btrfs_usrquota_syno_accounting_rescan(root, uid, num_bytes);
		if (ret) {
			btrfs_warn(fs_info, "Failed in btrfs_usrquota_syno_accounting_rescan(), "
				"subvol_id = %llu, ino = %llu, uid = %llu, ret = %d",
				subvol_id, ino, uid, ret);
			err = ret;
		}

		root->rescan_inode = ino;
		up_write(&root->rescan_lock);

		path->slots[0]++;
		goto next_slot;
	}

	up_write(&root->rescan_lock);
	btrfs_release_path(path);
	inode = btrfs_iget(fs_info->sb, ino, root);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		if (ret != -ENOENT) {
			btrfs_warn(fs_info, "Failed to call btrfs_iget(), "
				"subvol_id = %llu, ino = %llu, ret = %d", subvol_id, ino, ret);
			err = ret;
		}
	} else {
		down_write(&root->rescan_lock);
		num_bytes = inode_get_bytes(inode);

		ret = btrfs_qgroup_syno_accounting(BTRFS_I(inode), num_bytes, 0, ADD_QUOTA_RESCAN);
		if (ret) {
			btrfs_warn(fs_info, "Failed in btrfs_qgroup_syno_accounting(), "
				"subvol_id = %llu, ino = %llu, ret = %d", subvol_id, ino, ret);
			err = ret;
		}

		ret = btrfs_usrquota_syno_accounting(BTRFS_I(inode), num_bytes, 0, ADD_QUOTA_RESCAN);
		if (ret) {
			btrfs_warn(fs_info, "Failed in btrfs_usrquota_syno_accounting(), "
				"subvol_id = %llu, ino = %llu, uid = %llu, ret = %d",
				subvol_id, ino, uid, ret);
			err = ret;
		}

		root->rescan_inode = ino;
		up_write(&root->rescan_lock);
		btrfs_add_delayed_iput(inode);
	}

	if (ino < max_objectid) {
		key.objectid = ino + 1;
		goto search_again;
	}

out:
	if (ino < max_objectid)
		ino = max_objectid;
	btrfs_release_path(path);

	// Mark err but continue the rescan, or we'll see very strange (perhaps zero) quota usage.
	if (unlikely(err)) {
		syno_quota_rescan_item_init(&updater);
		updater.flags = SYNO_QUOTA_RESCAN_ERR;
		btrfs_add_update_syno_quota_rescan_item(trans, quota_root, subvol_id, &updater);
	}
	ret = 0;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	node = ulist_search(ulist, subvol_id);
	if (node) {
		if (ino > root->rescan_end_inode) { // This subvol is done. Switch to next subvol.
			root->rescan_inode = (u64)-1;
			root->rescan_end_inode = (u64)-1;

			ulist_del(ulist, subvol_id, node->aux);
			syno_quota_rescan_item_init(&updater);
			updater.rescan_inode = (u64)-1;
			updater.end_inode = (u64)-1;
			updater.flags = SYNO_QUOTA_RESCAN_DONE;
			ret = btrfs_add_update_syno_quota_rescan_item(trans, quota_root, subvol_id, &updater);
			if (ret)
				btrfs_warn(fs_info, "Failed to update syno quota rescan item, ret = %d", ret);

			if (!list_empty(&ulist->nodes)) {
				update_syno_quota_rescan_progress(quota_root, ctx,
					subvol_id, SYNO_QUOTA_PROGRESS_FINISH_ONE);
				ret = 0;
			} else {
				update_syno_quota_rescan_progress(quota_root, ctx,
					subvol_id, SYNO_QUOTA_PROGRESS_FINISH_ALL);
				fs_info->qgroup_rescan_progress.objectid = 0;
				ret = 1;
			}
		} else {
			WARN_ON_ONCE(ino < node->aux);
			node->aux = (ino > node->aux)? ino : node->aux;
			syno_quota_rescan_item_init(&updater);
			updater.rescan_inode = node->aux;
			updater.flags = SYNO_QUOTA_RESCAN_DOING;
			ret = btrfs_add_update_syno_quota_rescan_item(trans, quota_root, subvol_id, &updater);
			if (ret)
				btrfs_warn(fs_info, "Failed to update syno quota rescan item, ret = %d", ret);
			ret = 0;
		}
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);
	btrfs_put_root(root);

	return ret;

error_clean:
	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (ret < 0) {
		syno_quota_rescan_item_init(&updater);
		updater.rescan_inode = (u64)-1;
		updater.end_inode = (u64)-1;
		updater.flags = SYNO_QUOTA_RESCAN_ERR | SYNO_QUOTA_RESCAN_DONE;
		btrfs_add_update_syno_quota_rescan_item(trans, quota_root, subvol_id, &updater);
	}
	node = ulist_search(ulist, subvol_id);
	if (node) {
		ulist_del(ulist, subvol_id, node->aux);
		if (!list_empty(&ulist->nodes)) {
			update_syno_quota_rescan_progress(quota_root, ctx,
				subvol_id, SYNO_QUOTA_PROGRESS_REMOVE_SCANNING);
			ret = 0; // Scan next subvol.
		} else {
			update_syno_quota_rescan_progress(quota_root, ctx,
				subvol_id, SYNO_QUOTA_PROGRESS_FINISH_ALL);
			fs_info->qgroup_rescan_progress.objectid = 0;
			ret = 1;
		}
	} else
		ret = 0; // Scan next subvol.
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	return ret;
}
#else
/*
 * Check if the leaf is the last leaf. Which means all node pointers
 * are at their last position.
 */
static bool is_last_leaf(struct btrfs_path *path)
{
	int i;

	for (i = 1; i < BTRFS_MAX_LEVEL && path->nodes[i]; i++) {
		if (path->slots[i] != btrfs_header_nritems(path->nodes[i]) - 1)
			return false;
	}
	return true;
}

/*
 * returns < 0 on error, 0 when more leafs are to be scanned.
 * returns 1 when done.
 */
static int qgroup_rescan_leaf(struct btrfs_trans_handle *trans,
			      struct btrfs_path *path)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_key found;
	struct extent_buffer *scratch_leaf = NULL;
	struct ulist *roots = NULL;
	u64 num_bytes;
	bool done;
	int slot;
	int ret;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	ret = btrfs_search_slot_for_read(fs_info->extent_root,
					 &fs_info->qgroup_rescan_progress,
					 path, 1, 0);

	btrfs_debug(fs_info,
		"current progress key (%llu %u %llu), search_slot ret %d",
		fs_info->qgroup_rescan_progress.objectid,
		fs_info->qgroup_rescan_progress.type,
		fs_info->qgroup_rescan_progress.offset, ret);

	if (ret) {
		/*
		 * The rescan is about to end, we will not be scanning any
		 * further blocks. We cannot unset the RESCAN flag here, because
		 * we want to commit the transaction if everything went well.
		 * To make the live accounting work in this phase, we set our
		 * scan progress pointer such that every real extent objectid
		 * will be smaller.
		 */
		fs_info->qgroup_rescan_progress.objectid = (u64)-1;
		btrfs_release_path(path);
		mutex_unlock(&fs_info->qgroup_rescan_lock);
		return ret;
	}
	done = is_last_leaf(path);

	btrfs_item_key_to_cpu(path->nodes[0], &found,
			      btrfs_header_nritems(path->nodes[0]) - 1);
	fs_info->qgroup_rescan_progress.objectid = found.objectid + 1;

	scratch_leaf = btrfs_clone_extent_buffer(path->nodes[0]);
	if (!scratch_leaf) {
		ret = -ENOMEM;
		mutex_unlock(&fs_info->qgroup_rescan_lock);
		goto out;
	}
	slot = path->slots[0];
	btrfs_release_path(path);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	for (; slot < btrfs_header_nritems(scratch_leaf); ++slot) {
		btrfs_item_key_to_cpu(scratch_leaf, &found, slot);
		if (found.type != BTRFS_EXTENT_ITEM_KEY &&
		    found.type != BTRFS_METADATA_ITEM_KEY)
			continue;
		if (found.type == BTRFS_METADATA_ITEM_KEY)
			num_bytes = fs_info->nodesize;
		else
			num_bytes = found.offset;

		ret = btrfs_find_all_roots(NULL, fs_info, found.objectid, 0,
					   &roots, false);
		if (ret < 0)
			goto out;
		/* For rescan, just pass old_roots as NULL */
		ret = btrfs_qgroup_account_extent(trans, found.objectid,
						  num_bytes, NULL, roots);
		if (ret < 0)
			goto out;
	}
out:
	if (scratch_leaf)
		free_extent_buffer(scratch_leaf);

	if (done && !ret) {
		ret = 1;
		fs_info->qgroup_rescan_progress.objectid = (u64)-1;
	}
	return ret;
}
#endif /* MY_ABC_HERE */

static bool rescan_should_stop(struct btrfs_fs_info *fs_info)
{
	return btrfs_fs_closing(fs_info) ||
		test_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state)
#ifdef MY_ABC_HERE
		|| fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_PAUSE
#endif /* MY_ABC_HERE */
		;
}

static void btrfs_qgroup_rescan_worker(struct btrfs_work *work)
{
	struct btrfs_fs_info *fs_info = container_of(work, struct btrfs_fs_info,
						     qgroup_rescan_work);
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans = NULL;
	int err = -ENOMEM;
	int ret = 0;
	bool stopped = false;

#ifdef MY_ABC_HERE
again:
#endif /* MY_ABC_HERE */

	path = btrfs_alloc_path();
	if (!path)
		goto out;
	/*
	 * Rescan should only search for commit root, and any later difference
	 * should be recorded by qgroup
	 */
#ifdef MY_ABC_HERE
	path->reada = READA_FORWARD_ALWAYS;
#else
	path->search_commit_root = 1;
	path->skip_locking = 1;
#endif /* MY_ABC_HERE */

	err = 0;
	while (!err && !(stopped = rescan_should_stop(fs_info))) {
		trans = btrfs_start_transaction(fs_info->fs_root, 0);
		if (IS_ERR(trans)) {
			err = PTR_ERR(trans);
			break;
		}
#ifdef MY_ABC_HERE
		if (!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags)) {
			err = -EINTR;
		} else {
			err = syno_quota_rescan_leaf(trans, path);;
		}
#else
		if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags)) {
			err = -EINTR;
		} else {
			err = qgroup_rescan_leaf(trans, path);
		}
#endif /* MY_ABC_HERE */
		if (err > 0)
			btrfs_commit_transaction(trans);
		else
			btrfs_end_transaction(trans);
	}

out:
	btrfs_free_path(path);

	mutex_lock(&fs_info->qgroup_rescan_lock);
	if (err > 0 &&
	    fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT) {
#ifdef MY_ABC_HERE
		// Now we clear inconsistent flag by ioctl.
#else
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
#endif /* MY_ABC_HERE */
	} else if (err < 0) {
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	/*
	 * only update status, since the previous part has already updated the
	 * qgroup info.
	 */
	trans = btrfs_start_transaction(fs_info->quota_root, 1);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		trans = NULL;
		btrfs_err(fs_info,
			  "fail to start transaction for status update: %d",
			  err);
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);
#ifdef MY_ABC_HERE
	// In case another rescan join in after we left syno_quota_rescan_leaf().
	if (err >= 0 && !stopped && fs_info->syno_quota_rescan_subvol_ulist &&
			!list_empty(&fs_info->syno_quota_rescan_subvol_ulist->nodes)) {
		if (trans)
			btrfs_end_transaction(trans);
		mutex_unlock(&fs_info->qgroup_rescan_lock);
		goto again;
	}
#endif /* MY_ABC_HERE */
	if (!stopped)
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	if (trans) {
		ret = update_qgroup_status_item(trans);
		if (ret < 0) {
			err = ret;
			btrfs_err(fs_info, "fail to update qgroup status: %d",
				  err);
		}
	}
	fs_info->qgroup_rescan_running = false;
	complete_all(&fs_info->qgroup_rescan_completion);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	if (!trans)
		return;

	btrfs_end_transaction(trans);

	if (stopped) {
		btrfs_info(fs_info, "qgroup scan paused");
	} else if (err >= 0) {
#ifdef MY_ABC_HERE
		// Now we clear inconsistent flag by ioctl.
		btrfs_info(fs_info, "qgroup scan completed");
#else
		btrfs_info(fs_info, "qgroup scan completed%s",
			err > 0 ? " (inconsistency flag cleared)" : "");
#endif /* MY_ABC_HERE */
	} else {
		btrfs_err(fs_info, "qgroup scan failed with %d", err);
	}
}

/*
 * Checks that (a) no rescan is running and (b) quota is enabled. Allocates all
 * memory required for the rescan context.
 */
static int
qgroup_rescan_init(struct btrfs_fs_info *fs_info, u64 progress_objectid,
		   int init_flags)
{
	int ret = 0;

	if (!init_flags) {
		/* we're resuming qgroup rescan at mount time */
		if (!(fs_info->qgroup_flags &
		      BTRFS_QGROUP_STATUS_FLAG_RESCAN)) {
			btrfs_warn(fs_info,
			"qgroup rescan init failed, qgroup rescan is not queued");
			ret = -EINVAL;
		} else if (!(fs_info->qgroup_flags &
			     BTRFS_QGROUP_STATUS_FLAG_ON)) {
			btrfs_warn(fs_info,
			"qgroup rescan init failed, qgroup is not enabled");
			ret = -EINVAL;
		}

		if (ret)
			return ret;
	}

	mutex_lock(&fs_info->qgroup_rescan_lock);

	if (init_flags) {
		if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
#ifdef MY_ABC_HERE
#else
			btrfs_warn(fs_info,
				   "qgroup rescan is already in progress");
#endif /* MY_ABC_HERE */
			ret = -EINPROGRESS;
		} else if (!(fs_info->qgroup_flags &
			     BTRFS_QGROUP_STATUS_FLAG_ON)) {
			btrfs_warn(fs_info,
			"qgroup rescan init failed, qgroup is not enabled");
#ifdef MY_ABC_HERE
			ret = -ESRCH;
#else
			ret = -EINVAL;
#endif /* MY_ABC_HERE */
		}

#ifdef MY_ABC_HERE
		if (!ret && !fs_info->syno_quota_rescan_ctx) {
			fs_info->syno_quota_rescan_ctx =
				kzalloc(sizeof(struct syno_quota_rescan_ctx), GFP_KERNEL);
			if (!fs_info->syno_quota_rescan_ctx)
				ret = -ENOMEM;
		}

		if (!ret && !fs_info->syno_quota_rescan_subvol_ulist) {
			fs_info->syno_quota_rescan_subvol_ulist = ulist_alloc(GFP_KERNEL);
			if (!fs_info->syno_quota_rescan_subvol_ulist) {
				kfree(fs_info->syno_quota_rescan_ctx);
				fs_info->syno_quota_rescan_ctx = NULL;
				ret = -ENOMEM;
			}
		}
#endif /* MY_ABC_HERE */

		if (ret) {
			mutex_unlock(&fs_info->qgroup_rescan_lock);
			return ret;
		}
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	}

	memset(&fs_info->qgroup_rescan_progress, 0,
		sizeof(fs_info->qgroup_rescan_progress));
	fs_info->qgroup_rescan_progress.objectid = progress_objectid;
	init_completion(&fs_info->qgroup_rescan_completion);

#ifdef MY_ABC_HERE
	fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_PAUSE;
#endif /* MY_ABC_HERE */
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	btrfs_init_work(&fs_info->qgroup_rescan_work,
			btrfs_qgroup_rescan_worker, NULL, NULL);
	return 0;
}

static void
qgroup_rescan_zero_tracking(struct btrfs_fs_info *fs_info)
{
	struct rb_node *n;
	struct btrfs_qgroup *qgroup;

	spin_lock(&fs_info->qgroup_lock);
	/* clear all current qgroup tracking information */
	for (n = rb_first(&fs_info->qgroup_tree); n; n = rb_next(n)) {
		qgroup = rb_entry(n, struct btrfs_qgroup, node);
		qgroup->rfer = 0;
		qgroup->rfer_cmpr = 0;
		qgroup->excl = 0;
		qgroup->excl_cmpr = 0;
		qgroup_dirty(fs_info, qgroup);
	}
	spin_unlock(&fs_info->qgroup_lock);
}

int
btrfs_qgroup_rescan(struct btrfs_fs_info *fs_info)
{
	int ret = 0;
	struct btrfs_trans_handle *trans;

	ret = qgroup_rescan_init(fs_info, 0, 1);
	if (ret)
		return ret;

	/*
	 * We have set the rescan_progress to 0, which means no more
	 * delayed refs will be accounted by btrfs_qgroup_account_ref.
	 * However, btrfs_qgroup_account_ref may be right after its call
	 * to btrfs_find_all_roots, in which case it would still do the
	 * accounting.
	 * To solve this, we're committing the transaction, which will
	 * ensure we run all delayed refs and only after that, we are
	 * going to clear all tracking information for a clean start.
	 */

	trans = btrfs_join_transaction(fs_info->fs_root);
	if (IS_ERR(trans)) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
		return PTR_ERR(trans);
	}
	ret = btrfs_commit_transaction(trans);
	if (ret) {
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
		return ret;
	}

	qgroup_rescan_zero_tracking(fs_info);

	mutex_lock(&fs_info->qgroup_rescan_lock);
	fs_info->qgroup_rescan_running = true;
	btrfs_queue_work(fs_info->qgroup_rescan_workers,
			 &fs_info->qgroup_rescan_work);
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	return 0;
}

int btrfs_qgroup_wait_for_completion(struct btrfs_fs_info *fs_info,
				     bool interruptible)
{
	int running;
	int ret = 0;

	mutex_lock(&fs_info->qgroup_rescan_lock);
	running = fs_info->qgroup_rescan_running;
	mutex_unlock(&fs_info->qgroup_rescan_lock);

	if (!running)
		return 0;

	if (interruptible)
		ret = wait_for_completion_interruptible(
					&fs_info->qgroup_rescan_completion);
	else
		wait_for_completion(&fs_info->qgroup_rescan_completion);

	return ret;
}

/*
 * this is only called from open_ctree where we're still single threaded, thus
 * locking is omitted here.
 */
void
btrfs_qgroup_rescan_resume(struct btrfs_fs_info *fs_info)
{
	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		mutex_lock(&fs_info->qgroup_rescan_lock);
		fs_info->qgroup_rescan_running = true;
#ifdef MY_ABC_HERE
		fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_PAUSE;
#endif /* MY_ABC_HERE */
		btrfs_queue_work(fs_info->qgroup_rescan_workers,
				 &fs_info->qgroup_rescan_work);
		mutex_unlock(&fs_info->qgroup_rescan_lock);
	}
}

#ifdef MY_ABC_HERE
int btrfs_reset_qgroup_status(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_path *path = NULL;
	struct btrfs_qgroup_status_item *ptr;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	int ret;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
		ret = -ENOENT;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = 0;
	key.type = BTRFS_QGROUP_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(trans, quota_root, &key, path, 0, 1);
	if (ret)
		goto out;

	leaf = path->nodes[0];
	ptr = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_qgroup_status_item);
	fs_info->qgroup_flags &= ~(BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT);
	btrfs_set_qgroup_status_flags(leaf, ptr, fs_info->qgroup_flags);
	btrfs_set_qgroup_status_generation(leaf, ptr, trans->transid);
	btrfs_set_qgroup_status_version(leaf, ptr, BTRFS_QGROUP_V2_STATUS_VERSION);
	btrfs_mark_buffer_dirty(leaf);

out:
	btrfs_free_path(path);
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

int btrfs_syno_qgroup_transfer_limit(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *old_root = NULL;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path = NULL;
	struct extent_buffer *leaf;
	struct btrfs_qgroup_limit_item *ptr;
	struct btrfs_qgroup *qgroup;
	int ret = 0;
	int slot;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
		ret = -ESRCH;
		goto out;
	}

	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags)) {
		ret = -ESRCH;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = BTRFS_QUOTA_TREE_OBJECTID;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = 0;
	old_root = btrfs_read_tree_root(fs_info->tree_root, &key);
	if (IS_ERR(old_root)) {
		ret = PTR_ERR(old_root);
		old_root = NULL;
		goto out;
	}

	key.objectid = 0;
	key.type = BTRFS_QGROUP_LIMIT_KEY;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(old_root, &key, path, 1, 0);
	if (ret)
		goto out;

	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.type > BTRFS_QGROUP_LIMIT_KEY)
			break;

		if (found_key.type == BTRFS_QGROUP_LIMIT_KEY) {
			ptr = btrfs_item_ptr(leaf, slot,
					     struct btrfs_qgroup_limit_item);

			spin_lock(&fs_info->qgroup_lock);
			qgroup = find_qgroup_rb(fs_info, found_key.offset);
			if (qgroup && qgroup->lim_flags == 0
					&& qgroup->max_rfer == 0 && qgroup->max_excl == 0
					&& qgroup->rsv_rfer == 0 && qgroup->rsv_excl == 0) {
				qgroup->lim_flags = btrfs_qgroup_limit_flags(leaf, ptr);
				qgroup->max_rfer = btrfs_qgroup_limit_max_rfer(leaf, ptr);
				qgroup->max_excl = btrfs_qgroup_limit_max_excl(leaf, ptr);
				qgroup->rsv_rfer = btrfs_qgroup_limit_rsv_rfer(leaf, ptr);
				qgroup->rsv_excl = btrfs_qgroup_limit_rsv_excl(leaf, ptr);
				qgroup_dirty(fs_info, qgroup);
			}
			spin_unlock(&fs_info->qgroup_lock);
		}

		ret = btrfs_next_item(old_root, path);
		if (ret)
			break;
	}

out:
	btrfs_free_path(path);
	if (old_root) {
		free_extent_buffer(old_root->node);
		free_extent_buffer(old_root->commit_root);
		kfree(old_root);
	}
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	if (ret > 0)
		ret = 0;
	return ret;
}

// We may have no qgroup record in volume migration case.
static void qgroup_zero_tracking(struct btrfs_fs_info *fs_info, u64 subvol_id)
{
	struct btrfs_qgroup *qgroup;

	spin_lock(&fs_info->qgroup_lock);
	qgroup = find_qgroup_rb(fs_info, subvol_id);
	if (qgroup) {
		qgroup->rfer = 0;
		qgroup->rfer_cmpr = 0;
		qgroup->excl = 0;
		qgroup->excl_cmpr = 0;
		qgroup_dirty(fs_info, qgroup);
	}
	spin_unlock(&fs_info->qgroup_lock);
}

int btrfs_syno_quota_rescan(struct btrfs_root *root)
{
	int ret = 0;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct syno_quota_rescan_ctx *ctx;
	struct ulist *ulist;
	struct ulist_node *node;
	struct btrfs_trans_handle *trans;
	struct syno_quota_rescan_item_updater updater;
	u64 subvol_id = root->root_key.objectid;
	u64 prev_subvol_id = 0;

	trans = btrfs_start_transaction(root, 2);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		return ret;
	}

	ret = qgroup_rescan_init(fs_info, 0, 1);
	if (ret && ret != -EINPROGRESS) {
		btrfs_end_transaction(trans);
		return ret;
	}

	// Take qgroup_ioctl_lock after we start the transaction. See comments in btrfs_quota_enable().
	mutex_lock(&fs_info->qgroup_ioctl_lock);
	mutex_lock(&fs_info->qgroup_rescan_lock);
	ctx = fs_info->syno_quota_rescan_ctx;
	ulist = fs_info->syno_quota_rescan_subvol_ulist;
	if (!list_empty(&ulist->nodes)) {
		node = list_entry(ulist->nodes.prev, struct ulist_node, list);
		prev_subvol_id = node->val;
	}
	ret = ulist_add(ulist, subvol_id, 0, GFP_KERNEL);
	if (ret != 1) {
		if (ret == 0)
			ret = -EEXIST;
		else
			ret = -ENOMEM;
		goto out;
	}

	if (prev_subvol_id) {
		syno_quota_rescan_item_init(&updater);
		updater.next_root = subvol_id;
		ret = btrfs_add_update_syno_quota_rescan_item(trans, fs_info->quota_root,
									prev_subvol_id, &updater);
		if (ret)
			goto out;
	}

	/*
	 * Step 1. Update rescan item. This may fail so it must be the first step.
	 * Step 2. Set root->rescan_inode, so existing inodes won't do quota accounting until
	 *         they are scanned.
	 * Step 3. Zero quota.
	 * Step 4. Set root->rescan_end_inode, so new inode will do normal quota accounting.
	 */
	mutex_lock(&root->objectid_mutex);
	syno_quota_rescan_item_init(&updater);
	updater.flags = SYNO_QUOTA_RESCAN_QUEUED;
	updater.version = BTRFS_QGROUP_V2_STATUS_VERSION;
	updater.rescan_inode = 0;
	updater.end_inode = root->highest_objectid;
	updater.tree_size = btrfs_root_used(&root->root_item);
	updater.next_root = 0;
	ret = btrfs_add_update_syno_quota_rescan_item(trans, fs_info->quota_root, subvol_id, &updater);
	if (ret) {
		mutex_unlock(&root->objectid_mutex);
		goto out; // prev_subvol_id will point to a invalid rescan item, but it's OK, no need to abort.
	}

	if (!fs_info->qgroup_rescan_progress.objectid)
		fs_info->qgroup_rescan_progress.objectid = subvol_id;

	root->rescan_inode = 0;
	smp_wmb();
	qgroup_zero_tracking(fs_info, subvol_id);
	btrfs_usrquota_zero_tracking(fs_info, subvol_id);
	root->invalid_quota = false;
	root->rescan_end_inode = root->highest_objectid;
	mutex_unlock(&root->objectid_mutex);

	// Remove compression ratio flag from quota v1.
	if (btrfs_root_cmpr_ratio(root)) {
		btrfs_set_root_flags(&root->root_item,
			btrfs_root_flags(&root->root_item) & ~BTRFS_ROOT_SUBVOL_CMPR_RATIO);
		ret = btrfs_update_root(trans, fs_info->tree_root,
			&root->root_key, &root->root_item);
		if (ret) {
			// Print wraning but we can fix it manually, no need to abort.
			btrfs_warn(fs_info,
				"Failed to remove compression ratio flag for root %llu",
				root->root_key.objectid);
			ret = 0;
		}
	}

	// Remove fast chown flag from quota v1.
	fs_info->usrquota_compat_flags &= ~BTRFS_USRQUOTA_COMPAT_FLAG_INODE_QUOTA;

	btrfs_end_transaction(trans);
	trans = NULL;

	// Update progress info.
	update_syno_quota_rescan_progress(fs_info->quota_root, ctx,
		subvol_id, SYNO_QUOTA_PROGRESS_ADD_NEW);

	if (!fs_info->qgroup_rescan_running) {
		fs_info->qgroup_rescan_running = true;
		btrfs_queue_work(fs_info->qgroup_rescan_workers,
			 &fs_info->qgroup_rescan_work);
	}
	ret = 0;

out:
	if (trans)
		btrfs_end_transaction(trans);
	if (ret && ret != -EEXIST) {
		ulist_del(ulist, subvol_id, 0);
		if (list_empty(&ulist->nodes))
			fs_info->qgroup_flags &= ~BTRFS_QGROUP_STATUS_FLAG_RESCAN;
	}
	mutex_unlock(&fs_info->qgroup_rescan_lock);
	mutex_unlock(&fs_info->qgroup_ioctl_lock);

	/*
	 * We want everything is on-disk.
	 * But we can't commit the transaction with qgroup_rescan_lock, or we deadlock with rescan_worker.
	 */
	if (!ret) {
		trans = btrfs_join_transaction(root);
		if (!IS_ERR(trans))
			btrfs_commit_transaction(trans);
	}

	return ret;
}

static int syno_quota_rescan_progress(struct btrfs_root *root,
			struct btrfs_ioctl_syno_quota_status_args *sa,
			bool query_vol_progress, bool query_subvol_progress)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	struct syno_quota_rescan_ctx *ctx;
	struct ulist *ulist;
	struct ulist_node *node;
	struct btrfs_path *path = NULL;
	struct btrfs_root *scanning_root = NULL;
	u64 subvol_id = root->root_key.objectid;
	int ret;

	mutex_lock(&fs_info->qgroup_rescan_lock);

	// Update flags for rescan status.
	ctx = fs_info->syno_quota_rescan_ctx;
	ulist = fs_info->syno_quota_rescan_subvol_ulist;
	if (!ctx || !ulist || list_empty(&ulist->nodes) ||
			!(fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN)) {
		sa->progress = 0;
		sa->next_subvol_id = 0;
		sa->scanning_subvol_id = 0;
		ret = 0;
		goto out;
	} else if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_PAUSE)
		sa->status |= BTRFS_QUOTA_STATUS_VOL_RESCAN_PAUSED;
	else if (fs_info->qgroup_rescan_running)
		sa->status |= BTRFS_QUOTA_STATUS_VOL_RESCAN_DOING;
	else if (rescan_should_stop(fs_info)) {
		ret = -ECANCELED;
		goto out;
	} else {
		btrfs_warn_rl(fs_info, "Unexpected state in syno_quota_rescan_progress() but no harm");
		ret = -EINVAL;
		goto out;
	}

	// Update current scanning subvol.
	sa->scanning_subvol_id = ctx->subvol_id;

	// Update next subvol id.
	node = ulist_search(ulist, subvol_id);
	if (node) {
		if (subvol_id == ctx->subvol_id)
			sa->status |= BTRFS_QUOTA_STATUS_SUBVOL_RESCANNING;
		else
			sa->status |= BTRFS_QUOTA_STATUS_SUBVOL_RESCAN_QUEUED;

		if (node->list.next != &ulist->nodes) {
			node = list_entry(node->list.next, struct ulist_node, list);
			sa->next_subvol_id = node->val;
		}
	} else
		sa->next_subvol_id = 0;

	/*
	 * Query volume progress, or query subvol progress that is scanning.
	 * In both case we need to update ctx->subvol_id progress.
	 */
	if (query_vol_progress || (query_subvol_progress && ctx->subvol_id == subvol_id)) {
		u64 subvol_progress;
		u64 vol_progress;
		u64 denominator = 0;
		u64 numerator = 0;
		u64 tmp;
		int i;

		path = btrfs_alloc_path();
		if (!path) {
			ret = -ENOMEM;
			goto out;
		}

		scanning_root = btrfs_get_fs_root(fs_info, ctx->subvol_id, true);
		if (IS_ERR(scanning_root)) {
			ret = PTR_ERR(scanning_root);
			scanning_root = NULL;
			goto out;
		}

		key.objectid = scanning_root->rescan_end_inode;
		key.type = BTRFS_INODE_ITEM_KEY;
		key.offset = 0;

		ret = btrfs_search_slot(NULL, scanning_root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		syno_quota_update_progress_path(ctx->end_path, path);
		btrfs_release_path(path);

		// Calaulate subvol denominator.
		tmp = SYNO_QUOTA_RESCAN_100_PROGRESS;
		for (i = BTRFS_MAX_LEVEL - 1; i > 0; i--) {
			if (ctx->end_path[i][0]) {
				tmp /= ctx->end_path[i][0];
				denominator += (tmp * ctx->end_path[i][1]);
			}
		}

		// Calaulate subvol numerator.
		tmp = SYNO_QUOTA_RESCAN_100_PROGRESS;
		for (i = BTRFS_MAX_LEVEL - 1; i > 0; i--) {
			if (ctx->current_path[i][0]) {
				tmp /= ctx->current_path[i][0];
				numerator += (tmp * ctx->current_path[i][1]);
			}
		}

		if (denominator == 0)
			denominator = 1;
		if (numerator > denominator)
			numerator = denominator;

		subvol_progress = (numerator * SYNO_QUOTA_RESCAN_100_PROGRESS) / denominator;
		if (subvol_progress < ctx->subvol_progress)
			subvol_progress = ctx->subvol_progress;
		ctx->subvol_progress = subvol_progress;

		if (query_subvol_progress) { // Report subvol progress.
			sa->progress = subvol_progress;
			sa->status |= BTRFS_QUOTA_STATUS_SUBVOL_PROGRESS_VALID;
		} else { // Report vol progress.
			denominator = ctx->total_size;
			numerator = ctx->total_finished_size +
				(ctx->subvol_size * subvol_progress / SYNO_QUOTA_RESCAN_100_PROGRESS);

			if (numerator > denominator) {
				numerator = denominator;
				WARN_ON_ONCE(1);
			}

			vol_progress = (numerator * SYNO_QUOTA_RESCAN_100_PROGRESS) / denominator;
			if (vol_progress < ctx->vol_progress)
				vol_progress = ctx->vol_progress;
			ctx->vol_progress = vol_progress;
			sa->progress = vol_progress;
			sa->status |= BTRFS_QUOTA_STATUS_VOL_PROGRESS_VALID;
		}
	} else if (query_subvol_progress) { // Query subvol progress that is not currently scanning.
		struct btrfs_syno_quota_rescan_item rescan_item;

		ret = btrfs_read_syno_quota_rescan_item(fs_info->quota_root, subvol_id, &rescan_item);
		if (ret)
			goto out;

		if (rescan_item.flags & SYNO_QUOTA_RESCAN_DONE) {
			sa->progress = SYNO_QUOTA_RESCAN_100_PROGRESS;
			sa->status |= BTRFS_QUOTA_STATUS_SUBVOL_PROGRESS_VALID;
		} else if (rescan_item.flags & SYNO_QUOTA_RESCAN_QUEUED) {
			sa->progress = 0;
			sa->status |= BTRFS_QUOTA_STATUS_SUBVOL_PROGRESS_VALID;
		} else {
			WARN_ON_ONCE(1);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = 0;
out:
	btrfs_put_root(scanning_root);
	btrfs_free_path(path);
	mutex_unlock(&fs_info->qgroup_rescan_lock);
	return ret;
}

int btrfs_syno_quota_status(struct btrfs_root *root,
			struct btrfs_ioctl_syno_quota_status_args *sa)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret = 0;
	bool query_vol_progress = false;
	bool query_subvol_progress = false;

	if (sa->cmd & BTRFS_QUOTA_STATUS_RESCAN_VOL_PROGRESS)
		query_vol_progress = true;
	if (sa->cmd & BTRFS_QUOTA_STATUS_RESCAN_SUBVOL_PROGRESS)
		query_subvol_progress = true;

	// We'll return sa to user, so zero it first.
	memset(sa, 0, sizeof(*sa));

	if (query_vol_progress && query_subvol_progress)
		return -EINVAL;

	mutex_lock(&fs_info->qgroup_ioctl_lock);
	if (!fs_info->quota_root) {
		sa->status |= BTRFS_QUOTA_STATUS_VOL_DISABLED;
		sa->status |= BTRFS_QUOTA_STATUS_SUBVOL_DISABLED;
		goto out;
	}

	if (test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags))
		sa->status |= BTRFS_QUOTA_STATUS_VOL_SYNO_V1_ENABLED;
	else if (test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags))
		sa->status |= BTRFS_QUOTA_STATUS_VOL_SYNO_V2_ENABLED;
	else {
		sa->status |= BTRFS_QUOTA_STATUS_VOL_DISABLED;
		sa->status |= BTRFS_QUOTA_STATUS_SUBVOL_DISABLED;
		goto out;
	}

	if (root->invalid_quota || btrfs_root_disable_quota(root))
		sa->status |= BTRFS_QUOTA_STATUS_SUBVOL_DISABLED;
	else
		sa->status |= BTRFS_QUOTA_STATUS_SUBVOL_ENABLED;

	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT)
		sa->status |= BTRFS_QUOTA_STATUS_INCONSISTENT;
	if (fs_info->usrquota_flags & BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT)
		sa->status |= BTRFS_USRQUOTA_STATUS_INCONSISTENT;


	ret = syno_quota_rescan_progress(root, sa,
				query_vol_progress, query_subvol_progress);

out:
	mutex_unlock(&fs_info->qgroup_ioctl_lock);
	return ret;
}

void btrfs_read_syno_quota_for_root(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_syno_quota_rescan_item rescan_item;
	int ret;

#ifdef MY_ABC_HERE
	btrfs_check_usrquota_limit(root);
	btrfs_check_quota_limit(root);
#endif /* MY_ABC_HERE */

	// Only v2 has subvol quota version.
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags)) {
		root->invalid_quota = false;
		return;
	}

	ret = btrfs_read_syno_quota_rescan_item(fs_info->quota_root,
							root->root_key.objectid, &rescan_item);
	if (ret) {
		btrfs_info(fs_info, "Failed to read syno quota for root %llu, ret = %d",
							root->root_key.objectid, ret);
		return;
	}

	root->rescan_inode = rescan_item.rescan_inode;
	root->rescan_end_inode = rescan_item.end_inode;
	if (rescan_item.version == BTRFS_QGROUP_V2_STATUS_VERSION)
		root->invalid_quota = false;

	return;
}
#endif /* MY_ABC_HERE */

#define rbtree_iterate_from_safe(node, next, start)				\
       for (node = start; node && ({ next = rb_next(node); 1;}); node = next)

static int qgroup_unreserve_range(struct btrfs_inode *inode,
				  struct extent_changeset *reserved, u64 start,
				  u64 len)
{
	struct rb_node *node;
	struct rb_node *next;
	struct ulist_node *entry;
	int ret = 0;

	node = reserved->range_changed.root.rb_node;
	if (!node)
		return 0;
	while (node) {
		entry = rb_entry(node, struct ulist_node, rb_node);
		if (entry->val < start)
			node = node->rb_right;
		else
			node = node->rb_left;
	}

	if (entry->val > start && rb_prev(&entry->rb_node))
		entry = rb_entry(rb_prev(&entry->rb_node), struct ulist_node,
				 rb_node);

	rbtree_iterate_from_safe(node, next, &entry->rb_node) {
		u64 entry_start;
		u64 entry_end;
		u64 entry_len;
		int clear_ret;

		entry = rb_entry(node, struct ulist_node, rb_node);
		entry_start = entry->val;
		entry_end = entry->aux;
		entry_len = entry_end - entry_start + 1;

		if (entry_start >= start + len)
			break;
		if (entry_start + entry_len <= start)
			continue;
		/*
		 * Now the entry is in [start, start + len), revert the
		 * EXTENT_QGROUP_RESERVED bit.
		 */
		clear_ret = clear_extent_bits(&inode->io_tree, entry_start,
					      entry_end, EXTENT_QGROUP_RESERVED);
		if (!ret && clear_ret < 0)
			ret = clear_ret;

		ulist_del(&reserved->range_changed, entry->val, entry->aux);
		if (likely(reserved->bytes_changed >= entry_len)) {
			reserved->bytes_changed -= entry_len;
		} else {
			WARN_ON(1);
			reserved->bytes_changed = 0;
		}
	}

	return ret;
}

/*
 * Try to free some space for qgroup.
 *
 * For qgroup, there are only 3 ways to free qgroup space:
 * - Flush nodatacow write
 *   Any nodatacow write will free its reserved data space at run_delalloc_range().
 *   In theory, we should only flush nodatacow inodes, but it's not yet
 *   possible, so we need to flush the whole root.
 *
 * - Wait for ordered extents
 *   When ordered extents are finished, their reserved metadata is finally
 *   converted to per_trans status, which can be freed by later commit
 *   transaction.
 *
 * - Commit transaction
 *   This would free the meta_per_trans space.
 *   In theory this shouldn't provide much space, but any more qgroup space
 *   is needed.
 */
static int try_flush_qgroup(struct btrfs_root *root)
{
	struct btrfs_trans_handle *trans;
	int ret;
	bool can_commit = true;

	/*
	 * If current process holds a transaction, we shouldn't flush, as we
	 * assume all space reservation happens before a transaction handle is
	 * held.
	 *
	 * But there are cases like btrfs_delayed_item_reserve_metadata() where
	 * we try to reserve space with one transction handle already held.
	 * In that case we can't commit transaction, but at least try to end it
	 * and hope the started data writes can free some space.
	 */
	if (current->journal_info &&
	    current->journal_info != BTRFS_SEND_TRANS_STUB)
		can_commit = false;

	/*
	 * We don't want to run flush again and again, so if there is a running
	 * one, we won't try to start a new flush, but exit directly.
	 */
	if (test_and_set_bit(BTRFS_ROOT_QGROUP_FLUSHING, &root->state)) {
		/*
		 * We are already holding a transaction, thus we can block other
		 * threads from flushing.  So exit right now. This increases
		 * the chance of EDQUOT for heavy load and near limit cases.
		 * But we can argue that if we're already near limit, EDQUOT is
		 * unavoidable anyway.
		 */
		if (!can_commit)
			return 0;

		wait_event(root->qgroup_flush_wait,
			!test_bit(BTRFS_ROOT_QGROUP_FLUSHING, &root->state));
		return 0;
	}

	ret = btrfs_start_delalloc_snapshot(root);
	if (ret < 0)
		goto out;
	btrfs_wait_ordered_extents(root, U64_MAX, 0, (u64)-1);

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	if (can_commit)
		ret = btrfs_commit_transaction(trans);
	else
		ret = btrfs_end_transaction(trans);
out:
	clear_bit(BTRFS_ROOT_QGROUP_FLUSHING, &root->state);
	wake_up(&root->qgroup_flush_wait);
	return ret;
}

static int qgroup_reserve_data(struct btrfs_inode *inode,
			struct extent_changeset **reserved_ret, u64 start,
			u64 len)
{
	struct btrfs_root *root = inode->root;
	struct extent_changeset *reserved;
	bool new_reserved = false;
	u64 orig_reserved;
	u64 to_reserve;
	int ret;

#ifdef MY_ABC_HERE
	if ((!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &root->fs_info->flags) &&
		!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &root->fs_info->flags)) ||
#else
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &root->fs_info->flags) ||
#endif /* MY_ABC_HERE */
	    !is_fstree(root->root_key.objectid) || len == 0)
		return 0;

#ifdef MY_ABC_HERE
	if (btrfs_root_disable_quota(root))
		return 0;
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	if (!btrfs_root_has_usrquota_limit(root) && !btrfs_root_has_quota_limit(root))
		return 0;
#endif /* MY_ABC_HERE */

	/* @reserved parameter is mandatory for qgroup */
	if (WARN_ON(!reserved_ret))
		return -EINVAL;
	if (!*reserved_ret) {
		new_reserved = true;
		*reserved_ret = extent_changeset_alloc();
		if (!*reserved_ret)
			return -ENOMEM;
	}
	reserved = *reserved_ret;
	/* Record already reserved space */
	orig_reserved = reserved->bytes_changed;
	ret = set_record_extent_bits(&inode->io_tree, start,
			start + len -1, EXTENT_QGROUP_RESERVED, reserved);

	/* Newly reserved space */
	to_reserve = reserved->bytes_changed - orig_reserved;
	trace_btrfs_qgroup_reserve_data(&inode->vfs_inode, start, len,
					to_reserve, QGROUP_RESERVE);
	if (ret < 0)
		goto out;

#ifdef MY_ABC_HERE
	ret = usrquota_reserve(inode, to_reserve, true);
	if (ret != 0) {
		if (ret > 0)
			ret = 0;
		goto usr_reserve_fail;
	}
#endif /* MY_ABC_HERE */

	ret = qgroup_reserve(root, to_reserve, true, BTRFS_QGROUP_RSV_DATA);
#ifdef MY_ABC_HERE
	if (ret != 0) {
		if (ret > 0)
			ret = 0;
		goto cleanup;
	}
#else
	if (ret < 0)
		goto cleanup;
#endif /* MY_ABC_HERE */

	return ret;

cleanup:
#ifdef MY_ABC_HERE
	btrfs_usrquota_syno_free(inode, to_reserve);
usr_reserve_fail:
#endif /* MY_ABC_HERE */
	qgroup_unreserve_range(inode, reserved, start, len);
out:
	if (new_reserved) {
		extent_changeset_release(reserved);
		kfree(reserved);
		*reserved_ret = NULL;
	}
	return ret;
}

/*
 * Reserve qgroup space for range [start, start + len).
 *
 * This function will either reserve space from related qgroups or do nothing
 * if the range is already reserved.
 *
 * Return 0 for successful reservation
 * Return <0 for error (including -EQUOT)
 *
 * NOTE: This function may sleep for memory allocation, dirty page flushing and
 *	 commit transaction. So caller should not hold any dirty page locked.
 */
int btrfs_qgroup_reserve_data(struct btrfs_inode *inode,
			struct extent_changeset **reserved_ret, u64 start,
			u64 len)
{
	int ret;

	ret = qgroup_reserve_data(inode, reserved_ret, start, len);
	if (ret <= 0 && ret != -EDQUOT)
		return ret;

	ret = try_flush_qgroup(inode->root);
	if (ret < 0)
		return ret;
	return qgroup_reserve_data(inode, reserved_ret, start, len);
}

/* Free ranges specified by @reserved, normally in error path */
static int qgroup_free_reserved_data(struct btrfs_inode *inode,
			struct extent_changeset *reserved, u64 start, u64 len)
{
	struct btrfs_root *root = inode->root;
	struct ulist_node *unode;
#ifdef MY_ABC_HERE
	struct rb_node *node;
#else
	struct ulist_iterator uiter;
#endif /* MY_ABC_HERE */
	struct extent_changeset changeset;
	int freed = 0;
	int ret;

	extent_changeset_init(&changeset);
	len = round_up(start + len, root->fs_info->sectorsize);
	start = round_down(start, root->fs_info->sectorsize);

#ifdef MY_ABC_HERE
	unode = ulist_search_with_prev(&reserved->range_changed, start);
	while (unode) {
		u64 range_start = unode->val;
		/* unode->aux is the inclusive end */
		u64 range_len = unode->aux - range_start + 1;
		u64 free_start;
		u64 free_len;

		extent_changeset_release(&changeset);

		/* Only free range in range [start, start + len) */
		if (range_start + range_len <= start)
			goto next;
		if (range_start >= start + len)
			break;
		free_start = max(range_start, start);
		free_len = min(start + len, range_start + range_len) -
			   free_start;
		/*
		 * TODO: To also modify reserved->ranges_reserved to reflect
		 * the modification.
		 *
		 * However as long as we free qgroup reserved according to
		 * EXTENT_QGROUP_RESERVED, we won't double free.
		 * So not need to rush.
		 */
		ret = clear_record_extent_bits(&inode->io_tree, free_start,
				free_start + free_len - 1,
				EXTENT_QGROUP_RESERVED, &changeset);
		if (ret < 0)
			goto out;
		freed += changeset.bytes_changed;
next:
		node = rb_next(&unode->rb_node);
		if (!node)
			break;
		unode = rb_entry(node, struct ulist_node, rb_node);
	}
#else
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(&reserved->range_changed, &uiter))) {
		u64 range_start = unode->val;
		/* unode->aux is the inclusive end */
		u64 range_len = unode->aux - range_start + 1;
		u64 free_start;
		u64 free_len;

		extent_changeset_release(&changeset);

		/* Only free range in range [start, start + len) */
		if (range_start >= start + len ||
		    range_start + range_len <= start)
			continue;
		free_start = max(range_start, start);
		free_len = min(start + len, range_start + range_len) -
			   free_start;
		/*
		 * TODO: To also modify reserved->ranges_reserved to reflect
		 * the modification.
		 *
		 * However as long as we free qgroup reserved according to
		 * EXTENT_QGROUP_RESERVED, we won't double free.
		 * So not need to rush.
		 */
		ret = clear_record_extent_bits(&inode->io_tree, free_start,
				free_start + free_len - 1,
				EXTENT_QGROUP_RESERVED, &changeset);
		if (ret < 0)
			goto out;
		freed += changeset.bytes_changed;
	}
#endif /* MY_ABC_HERE */
	btrfs_qgroup_free_refroot(root->fs_info, root->root_key.objectid, freed,
				  BTRFS_QGROUP_RSV_DATA);
	ret = freed;
out:
	extent_changeset_release(&changeset);
	return ret;
}

static int __btrfs_qgroup_release_data(struct btrfs_inode *inode,
			struct extent_changeset *reserved, u64 start, u64 len,
			int free)
{
	struct extent_changeset changeset;
	int trace_op = QGROUP_RELEASE;
	int ret;

#ifdef MY_ABC_HERE
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &inode->root->fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &inode->root->fs_info->flags)) {
		if (inode->root->fs_info->need_clear_reserve) {
			clear_extent_bit(&inode->io_tree, start, start + len -1,
				       EXTENT_QGROUP_RESERVED, 0, 0, NULL);
			spin_lock(&inode->root->fs_info->usrquota_lock);
			inode->uq_reserved = 0;
			spin_unlock(&inode->root->fs_info->usrquota_lock);
		}
		return 0;
	}
#else
	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &inode->root->fs_info->flags))
		return 0;
#endif /* MY_ABC_HERE */

	/* In release case, we shouldn't have @reserved */
	WARN_ON(!free && reserved);
	if (free && reserved)
		return qgroup_free_reserved_data(inode, reserved, start, len);
	extent_changeset_init(&changeset);
	ret = clear_record_extent_bits(&inode->io_tree, start, start + len -1,
				       EXTENT_QGROUP_RESERVED, &changeset);
	if (ret < 0)
		goto out;

	if (free)
		trace_op = QGROUP_FREE;
	trace_btrfs_qgroup_release_data(&inode->vfs_inode, start, len,
					changeset.bytes_changed, trace_op);
	if (free)
		btrfs_qgroup_free_refroot(inode->root->fs_info,
				inode->root->root_key.objectid,
				changeset.bytes_changed, BTRFS_QGROUP_RSV_DATA);
	ret = changeset.bytes_changed;
out:
	extent_changeset_release(&changeset);
	return ret;
}

/*
 * Free a reserved space range from io_tree and related qgroups
 *
 * Should be called when a range of pages get invalidated before reaching disk.
 * Or for error cleanup case.
 * if @reserved is given, only reserved range in [@start, @start + @len) will
 * be freed.
 *
 * For data written to disk, use btrfs_qgroup_release_data().
 *
 * NOTE: This function may sleep for memory allocation.
 */
int btrfs_qgroup_free_data(struct btrfs_inode *inode,
			struct extent_changeset *reserved, u64 start, u64 len)
{
#ifdef MY_ABC_HERE
	int to_free;

	to_free = __btrfs_qgroup_release_data(inode, reserved, start, len, 1);
	if (to_free > 0)
		btrfs_usrquota_syno_free(inode, to_free);
	return to_free;
#else
	return __btrfs_qgroup_release_data(inode, reserved, start, len, 1);
#endif /* MY_ABC_HERE */
}

/*
 * Release a reserved space range from io_tree only.
 *
 * Should be called when a range of pages get written to disk and corresponding
 * FILE_EXTENT is inserted into corresponding root.
 *
 * Since new qgroup accounting framework will only update qgroup numbers at
 * commit_transaction() time, its reserved space shouldn't be freed from
 * related qgroups.
 *
 * But we should release the range from io_tree, to allow further write to be
 * COWed.
 *
 * NOTE: This function may sleep for memory allocation.
 */
int btrfs_qgroup_release_data(struct btrfs_inode *inode, u64 start, u64 len)
{
	return __btrfs_qgroup_release_data(inode, NULL, start, len, 0);
}

#ifdef MY_ABC_HERE
#else
static void add_root_meta_rsv(struct btrfs_root *root, int num_bytes,
			      enum btrfs_qgroup_rsv_type type)
{
	if (type != BTRFS_QGROUP_RSV_META_PREALLOC &&
	    type != BTRFS_QGROUP_RSV_META_PERTRANS)
		return;
	if (num_bytes == 0)
		return;

	spin_lock(&root->qgroup_meta_rsv_lock);
	if (type == BTRFS_QGROUP_RSV_META_PREALLOC)
		root->qgroup_meta_rsv_prealloc += num_bytes;
	else
		root->qgroup_meta_rsv_pertrans += num_bytes;
	spin_unlock(&root->qgroup_meta_rsv_lock);
}

static int sub_root_meta_rsv(struct btrfs_root *root, int num_bytes,
			     enum btrfs_qgroup_rsv_type type)
{
	if (type != BTRFS_QGROUP_RSV_META_PREALLOC &&
	    type != BTRFS_QGROUP_RSV_META_PERTRANS)
		return 0;
	if (num_bytes == 0)
		return 0;

	spin_lock(&root->qgroup_meta_rsv_lock);
	if (type == BTRFS_QGROUP_RSV_META_PREALLOC) {
		num_bytes = min_t(u64, root->qgroup_meta_rsv_prealloc,
				  num_bytes);
		root->qgroup_meta_rsv_prealloc -= num_bytes;
	} else {
		num_bytes = min_t(u64, root->qgroup_meta_rsv_pertrans,
				  num_bytes);
		root->qgroup_meta_rsv_pertrans -= num_bytes;
	}
	spin_unlock(&root->qgroup_meta_rsv_lock);
	return num_bytes;
}
#endif /* MY_ABC_HERE */

int btrfs_qgroup_reserve_meta(struct btrfs_root *root, int num_bytes,
			      enum btrfs_qgroup_rsv_type type, bool enforce)
{
#ifdef MY_ABC_HERE
	return 0;
#else
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags) ||
	    !is_fstree(root->root_key.objectid) || num_bytes == 0)
		return 0;

	BUG_ON(num_bytes != round_down(num_bytes, fs_info->nodesize));
	trace_qgroup_meta_reserve(root, (s64)num_bytes, type);
	ret = qgroup_reserve(root, num_bytes, enforce, type);
	if (ret < 0)
		return ret;
	/*
	 * Record what we have reserved into root.
	 *
	 * To avoid quota disabled->enabled underflow.
	 * In that case, we may try to free space we haven't reserved
	 * (since quota was disabled), so record what we reserved into root.
	 * And ensure later release won't underflow this number.
	 */
	add_root_meta_rsv(root, num_bytes, type);
	return ret;
#endif /* MY_ABC_HERE */
}

int __btrfs_qgroup_reserve_meta(struct btrfs_root *root, int num_bytes,
				enum btrfs_qgroup_rsv_type type, bool enforce)
{
#ifdef MY_ABC_HERE
	return 0;
#else
	int ret;

	ret = btrfs_qgroup_reserve_meta(root, num_bytes, type, enforce);
	if (ret <= 0 && ret != -EDQUOT)
		return ret;

	ret = try_flush_qgroup(root);
	if (ret < 0)
		return ret;
	return btrfs_qgroup_reserve_meta(root, num_bytes, type, enforce);
#endif /* MY_ABC_HERE */
}

void btrfs_qgroup_free_meta_all_pertrans(struct btrfs_root *root)
{
#ifdef MY_ABC_HERE
#else
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags) ||
	    !is_fstree(root->root_key.objectid))
		return;

	/* TODO: Update trace point to handle such free */
	trace_qgroup_meta_free_all_pertrans(root);
	/* Special value -1 means to free all reserved space */
	btrfs_qgroup_free_refroot(fs_info, root->root_key.objectid, (u64)-1,
				  BTRFS_QGROUP_RSV_META_PERTRANS);
#endif /* MY_ABC_HERE */
}

void __btrfs_qgroup_free_meta(struct btrfs_root *root, int num_bytes,
			      enum btrfs_qgroup_rsv_type type)
{
#ifdef MY_ABC_HERE
#else
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags) ||
	    !is_fstree(root->root_key.objectid))
		return;

	/*
	 * reservation for META_PREALLOC can happen before quota is enabled,
	 * which can lead to underflow.
	 * Here ensure we will only free what we really have reserved.
	 */
	num_bytes = sub_root_meta_rsv(root, num_bytes, type);
	BUG_ON(num_bytes != round_down(num_bytes, fs_info->nodesize));
	trace_qgroup_meta_reserve(root, -(s64)num_bytes, type);
	btrfs_qgroup_free_refroot(fs_info, root->root_key.objectid,
				  num_bytes, type);
#endif /* MY_ABC_HERE */
}

#ifdef MY_ABC_HERE
#else
static void qgroup_convert_meta(struct btrfs_fs_info *fs_info, u64 ref_root,
				int num_bytes)
{
	struct btrfs_qgroup *qgroup;
	struct ulist_node *unode;
	struct ulist_iterator uiter;
	int ret = 0;

	if (num_bytes == 0)
		return;
	if (!fs_info->quota_root)
		return;

	spin_lock(&fs_info->qgroup_lock);
	qgroup = find_qgroup_rb(fs_info, ref_root);
	if (!qgroup)
		goto out;
	ulist_reinit(fs_info->qgroup_ulist);
	ret = ulist_add(fs_info->qgroup_ulist, qgroup->qgroupid,
		       qgroup_to_aux(qgroup), GFP_ATOMIC);
	if (ret < 0)
		goto out;
	ULIST_ITER_INIT(&uiter);
	while ((unode = ulist_next(fs_info->qgroup_ulist, &uiter))) {
		struct btrfs_qgroup *qg;
		struct btrfs_qgroup_list *glist;

		qg = unode_aux_to_qgroup(unode);

		qgroup_rsv_release(fs_info, qg, num_bytes,
				BTRFS_QGROUP_RSV_META_PREALLOC);
		qgroup_rsv_add(fs_info, qg, num_bytes,
				BTRFS_QGROUP_RSV_META_PERTRANS);
		list_for_each_entry(glist, &qg->groups, next_group) {
			ret = ulist_add(fs_info->qgroup_ulist,
					glist->group->qgroupid,
					qgroup_to_aux(glist->group), GFP_ATOMIC);
			if (ret < 0)
				goto out;
		}
	}
out:
	spin_unlock(&fs_info->qgroup_lock);
}
#endif /* MY_ABC_HERE */

void btrfs_qgroup_convert_reserved_meta(struct btrfs_root *root, int num_bytes)
{
#ifdef MY_ABC_HERE
#else
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags) ||
	    !is_fstree(root->root_key.objectid))
		return;
	/* Same as btrfs_qgroup_free_meta_prealloc() */
	num_bytes = sub_root_meta_rsv(root, num_bytes,
				      BTRFS_QGROUP_RSV_META_PREALLOC);
	trace_qgroup_meta_convert(root, num_bytes);
	qgroup_convert_meta(fs_info, root->root_key.objectid, num_bytes);
#endif /* MY_ABC_HERE */
}

/*
 * Check qgroup reserved space leaking, normally at destroy inode
 * time
 */
void btrfs_qgroup_check_reserved_leak(struct btrfs_inode *inode)
{
	struct extent_changeset changeset;
	struct ulist_node *unode;
	struct ulist_iterator iter;
	int ret;

	extent_changeset_init(&changeset);
	ret = clear_record_extent_bits(&inode->io_tree, 0, (u64)-1,
			EXTENT_QGROUP_RESERVED, &changeset);

	WARN_ON(ret < 0);
	if (WARN_ON(changeset.bytes_changed)) {
		ULIST_ITER_INIT(&iter);
		while ((unode = ulist_next(&changeset.range_changed, &iter))) {
			btrfs_warn(inode->root->fs_info,
		"leaking qgroup reserved space, ino: %llu, start: %llu, end: %llu",
				btrfs_ino(inode), unode->val, unode->aux);
		}
		btrfs_qgroup_free_refroot(inode->root->fs_info,
				inode->root->root_key.objectid,
				changeset.bytes_changed, BTRFS_QGROUP_RSV_DATA);

	}
	extent_changeset_release(&changeset);
}

void btrfs_qgroup_init_swapped_blocks(
	struct btrfs_qgroup_swapped_blocks *swapped_blocks)
{
#ifdef MY_ABC_HERE
#else
	int i;

	spin_lock_init(&swapped_blocks->lock);
	for (i = 0; i < BTRFS_MAX_LEVEL; i++)
		swapped_blocks->blocks[i] = RB_ROOT;
	swapped_blocks->swapped = false;
#endif /* MY_ABC_HERE */
}

/*
 * Delete all swapped blocks record of @root.
 * Every record here means we skipped a full subtree scan for qgroup.
 *
 * Gets called when committing one transaction.
 */
void btrfs_qgroup_clean_swapped_blocks(struct btrfs_root *root)
{
#ifdef MY_ABC_HERE
#else
	struct btrfs_qgroup_swapped_blocks *swapped_blocks;
	int i;

	swapped_blocks = &root->swapped_blocks;

	spin_lock(&swapped_blocks->lock);
	if (!swapped_blocks->swapped)
		goto out;
	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		struct rb_root *cur_root = &swapped_blocks->blocks[i];
		struct btrfs_qgroup_swapped_block *entry;
		struct btrfs_qgroup_swapped_block *next;

		rbtree_postorder_for_each_entry_safe(entry, next, cur_root,
						     node)
			kfree(entry);
		swapped_blocks->blocks[i] = RB_ROOT;
	}
	swapped_blocks->swapped = false;
out:
	spin_unlock(&swapped_blocks->lock);
#endif /* MY_ABC_HERE */
}

/*
 * Add subtree roots record into @subvol_root.
 *
 * @subvol_root:	tree root of the subvolume tree get swapped
 * @bg:			block group under balance
 * @subvol_parent/slot:	pointer to the subtree root in subvolume tree
 * @reloc_parent/slot:	pointer to the subtree root in reloc tree
 *			BOTH POINTERS ARE BEFORE TREE SWAP
 * @last_snapshot:	last snapshot generation of the subvolume tree
 */
int btrfs_qgroup_add_swapped_blocks(struct btrfs_trans_handle *trans,
		struct btrfs_root *subvol_root,
		struct btrfs_block_group *bg,
		struct extent_buffer *subvol_parent, int subvol_slot,
		struct extent_buffer *reloc_parent, int reloc_slot,
		u64 last_snapshot)
{
#ifdef MY_ABC_HERE
	return 0;
#else
	struct btrfs_fs_info *fs_info = subvol_root->fs_info;
	struct btrfs_qgroup_swapped_blocks *blocks = &subvol_root->swapped_blocks;
	struct btrfs_qgroup_swapped_block *block;
	struct rb_node **cur;
	struct rb_node *parent = NULL;
	int level = btrfs_header_level(subvol_parent) - 1;
	int ret = 0;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		return 0;

	if (btrfs_node_ptr_generation(subvol_parent, subvol_slot) >
	    btrfs_node_ptr_generation(reloc_parent, reloc_slot)) {
		btrfs_err_rl(fs_info,
		"%s: bad parameter order, subvol_gen=%llu reloc_gen=%llu",
			__func__,
			btrfs_node_ptr_generation(subvol_parent, subvol_slot),
			btrfs_node_ptr_generation(reloc_parent, reloc_slot));
		return -EUCLEAN;
	}

	block = kmalloc(sizeof(*block), GFP_NOFS);
	if (!block) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * @reloc_parent/slot is still before swap, while @block is going to
	 * record the bytenr after swap, so we do the swap here.
	 */
	block->subvol_bytenr = btrfs_node_blockptr(reloc_parent, reloc_slot);
	block->subvol_generation = btrfs_node_ptr_generation(reloc_parent,
							     reloc_slot);
	block->reloc_bytenr = btrfs_node_blockptr(subvol_parent, subvol_slot);
	block->reloc_generation = btrfs_node_ptr_generation(subvol_parent,
							    subvol_slot);
	block->last_snapshot = last_snapshot;
	block->level = level;

	/*
	 * If we have bg == NULL, we're called from btrfs_recover_relocation(),
	 * no one else can modify tree blocks thus we qgroup will not change
	 * no matter the value of trace_leaf.
	 */
	if (bg && bg->flags & BTRFS_BLOCK_GROUP_DATA)
		block->trace_leaf = true;
	else
		block->trace_leaf = false;
	btrfs_node_key_to_cpu(reloc_parent, &block->first_key, reloc_slot);

	/* Insert @block into @blocks */
	spin_lock(&blocks->lock);
	cur = &blocks->blocks[level].rb_node;
	while (*cur) {
		struct btrfs_qgroup_swapped_block *entry;

		parent = *cur;
		entry = rb_entry(parent, struct btrfs_qgroup_swapped_block,
				 node);

		if (entry->subvol_bytenr < block->subvol_bytenr) {
			cur = &(*cur)->rb_left;
		} else if (entry->subvol_bytenr > block->subvol_bytenr) {
			cur = &(*cur)->rb_right;
		} else {
			if (entry->subvol_generation !=
					block->subvol_generation ||
			    entry->reloc_bytenr != block->reloc_bytenr ||
			    entry->reloc_generation !=
					block->reloc_generation) {
				/*
				 * Duplicated but mismatch entry found.
				 * Shouldn't happen.
				 *
				 * Marking qgroup inconsistent should be enough
				 * for end users.
				 */
				WARN_ON(IS_ENABLED(CONFIG_BTRFS_DEBUG));
				ret = -EEXIST;
			}
			kfree(block);
			goto out_unlock;
		}
	}
	rb_link_node(&block->node, parent, cur);
	rb_insert_color(&block->node, &blocks->blocks[level]);
	blocks->swapped = true;
out_unlock:
	spin_unlock(&blocks->lock);
out:
	if (ret < 0)
		fs_info->qgroup_flags |=
			BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	return ret;
#endif /* MY_ABC_HERE */
}

/*
 * Check if the tree block is a subtree root, and if so do the needed
 * delayed subtree trace for qgroup.
 *
 * This is called during btrfs_cow_block().
 */
int btrfs_qgroup_trace_subtree_after_cow(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root,
					 struct extent_buffer *subvol_eb)
{
#ifdef MY_ABC_HERE
	return 0;
#else
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_qgroup_swapped_blocks *blocks = &root->swapped_blocks;
	struct btrfs_qgroup_swapped_block *block;
	struct extent_buffer *reloc_eb = NULL;
	struct rb_node *node;
	bool found = false;
	bool swapped = false;
	int level = btrfs_header_level(subvol_eb);
	int ret = 0;
	int i;

	if (!test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags))
		return 0;
	if (!is_fstree(root->root_key.objectid) || !root->reloc_root)
		return 0;

	spin_lock(&blocks->lock);
	if (!blocks->swapped) {
		spin_unlock(&blocks->lock);
		return 0;
	}
	node = blocks->blocks[level].rb_node;

	while (node) {
		block = rb_entry(node, struct btrfs_qgroup_swapped_block, node);
		if (block->subvol_bytenr < subvol_eb->start) {
			node = node->rb_left;
		} else if (block->subvol_bytenr > subvol_eb->start) {
			node = node->rb_right;
		} else {
			found = true;
			break;
		}
	}
	if (!found) {
		spin_unlock(&blocks->lock);
		goto out;
	}
	/* Found one, remove it from @blocks first and update blocks->swapped */
	rb_erase(&block->node, &blocks->blocks[level]);
	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		if (RB_EMPTY_ROOT(&blocks->blocks[i])) {
			swapped = true;
			break;
		}
	}
	blocks->swapped = swapped;
	spin_unlock(&blocks->lock);

	/* Read out reloc subtree root */
	reloc_eb = read_tree_block(fs_info, block->reloc_bytenr, 0,
				   block->reloc_generation, block->level,
				   &block->first_key);
	if (IS_ERR(reloc_eb)) {
		ret = PTR_ERR(reloc_eb);
		reloc_eb = NULL;
		goto free_out;
	}
	if (!extent_buffer_uptodate(reloc_eb)) {
		ret = -EIO;
		goto free_out;
	}

	ret = qgroup_trace_subtree_swap(trans, reloc_eb, subvol_eb,
			block->last_snapshot, block->trace_leaf);
free_out:
	kfree(block);
	free_extent_buffer(reloc_eb);
out:
	if (ret < 0) {
		btrfs_err_rl(fs_info,
			     "failed to account subtree at bytenr %llu: %d",
			     subvol_eb->start, ret);
		fs_info->qgroup_flags |= BTRFS_QGROUP_STATUS_FLAG_INCONSISTENT;
	}
	return ret;
#endif /* MY_ABC_HERE */
}

void btrfs_qgroup_destroy_extent_records(struct btrfs_transaction *trans)
{
#ifdef MY_ABC_HERE
#else
	struct btrfs_qgroup_extent_record *entry;
	struct btrfs_qgroup_extent_record *next;
	struct rb_root *root;

	root = &trans->delayed_refs.dirty_extent_root;
	rbtree_postorder_for_each_entry_safe(entry, next, root, node) {
		ulist_free(entry->old_roots);
		kfree(entry);
	}
#endif /* MY_ABC_HERE */
}

#ifdef MY_ABC_HERE
static bool check_quota_from_disk(struct btrfs_fs_info *fs_info, u64 qgroupid)
{
	int ret;
	int slot;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *quota_root = fs_info->quota_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_qgroup_limit_item *limit_item;
	bool has_limit = false;
	u64 flags;
	u64 max_rfer;
	u64 max_excl;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = 0;
	key.type = BTRFS_QGROUP_LIMIT_KEY;
	key.offset = qgroupid;
	ret = btrfs_search_slot_for_read(quota_root, &key, path, 1, 0);
	if (ret < 0)
		goto out;
	else if (ret) {
		ret = 0;
		goto out;
	}

	slot = path->slots[0];
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, slot);
	if (found_key.offset != qgroupid ||
	    found_key.type != BTRFS_QGROUP_LIMIT_KEY) {
		ret = 0;
		goto out;
	}

	limit_item = btrfs_item_ptr(leaf, slot,
				struct btrfs_qgroup_limit_item);
	flags = btrfs_qgroup_limit_flags(leaf, limit_item);
	max_rfer = btrfs_qgroup_limit_max_rfer(leaf, limit_item);
	max_excl = btrfs_qgroup_limit_max_excl(leaf, limit_item);
	if ((flags & BTRFS_QGROUP_LIMIT_MAX_RFER && max_rfer) ||
	    (flags & BTRFS_QGROUP_LIMIT_MAX_EXCL && max_excl))
		has_limit = true;
	ret = 0;
out:
	btrfs_free_path(path);
	// When an error occurr, we always treat it as having quota_limt.
	return (ret) ? true : has_limit;
}

void btrfs_check_quota_limit(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	bool has_limit = false;
	u64 qgroupid = root->root_key.objectid;
	struct btrfs_qgroup *qgroup;

	spin_lock(&fs_info->qgroup_lock);
	if (!fs_info->quota_root) {
		spin_unlock(&fs_info->qgroup_lock);
		return;
	}

	qgroup = find_qgroup_rb(fs_info, qgroupid);
	if (!qgroup) {
		// subtree is unloaded, read from disk.
		spin_unlock(&fs_info->qgroup_lock);
		has_limit = check_quota_from_disk(fs_info, qgroupid);
	} else {
		if ((qgroup->lim_flags & BTRFS_QGROUP_LIMIT_MAX_RFER && qgroup->max_rfer) ||
		    (qgroup->lim_flags & BTRFS_QGROUP_LIMIT_MAX_EXCL && qgroup->max_excl))
			has_limit = true;
		spin_unlock(&fs_info->qgroup_lock);
	}
	btrfs_root_set_has_quota_limit(root, has_limit);
}
#endif /* MY_ABC_HERE */

