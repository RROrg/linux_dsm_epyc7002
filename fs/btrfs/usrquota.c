#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/*
 * Copyright (C) 2021 Synology Inc.  All rights reserved.
 */
#include <linux/rbtree.h>
#include <linux/fs.h>
#include <linux/btrfs.h>

#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"
#include "locking.h"
#include "extent_io.h"
#include "qgroup.h"
#include "backref.h"

#define USRQUOTA_RO_SUBVOL_EXIST_GEN 10

struct btrfs_usrquota {
	u64 uq_objectid;
	u64 uq_uid; // __kuid_val(inode->i_uid)

	// info item
	u64 uq_generation;
	u64 uq_rfer_used;
	// limit item
	u64 uq_rfer_soft;
	u64 uq_rfer_hard;
	// reservation tracking
	u64 uq_reserved;

	struct list_head uq_dirty;
	u64 uq_refcnt; // accurate on dummy node only

	// tree of userquota
	struct rb_node uq_node;

	bool need_rescan;
	bool update_limit;
};

struct btrfs_subvol_list  {
	u64 subvol_id;
	struct list_head list;
};

static void usrquota_subtree_unload_nolock(struct btrfs_fs_info *fs_info, u64 rootid);

static u64 find_next_valid_objectid(struct btrfs_fs_info *fs_info, u64 objectid)
{
	struct rb_node *node = fs_info->usrquota_tree.rb_node;
	struct btrfs_usrquota *usrquota;
	u64 valid_objectid = 0;

	while (node) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		if (usrquota->uq_objectid > objectid) {
			valid_objectid = usrquota->uq_objectid;
			node = node->rb_left;
		} else if (usrquota->uq_objectid < objectid)
			node = node->rb_right;
		 else
			break;
	}
	if (node) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		valid_objectid = usrquota->uq_objectid;
	}
	return valid_objectid;
}

/*
 * *_usrquota_rb should protected by usrquota_lock
 */
static struct rb_node *find_usrquota_first_rb(struct btrfs_fs_info *fs_info,
                                            u64 objectid)
{
	struct rb_node *node = fs_info->usrquota_tree.rb_node;
	struct rb_node *found = NULL;
	struct btrfs_usrquota *usrquota;

again:
	while (node) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		if (usrquota->uq_objectid > objectid)
			node = node->rb_left;
		else if (usrquota->uq_objectid < objectid)
			node = node->rb_right;
		else
			break;
	}
	if (!node)
		goto out;
	found = node;
	while (node->rb_left) {
		usrquota = rb_entry(node->rb_left, struct btrfs_usrquota, uq_node);
		node = node->rb_left;
		if (usrquota->uq_objectid != objectid)
			goto again;
		found = node;
	}
out:
	return found;
}


static struct btrfs_usrquota *find_usrquota_rb(struct btrfs_fs_info *fs_info,
                                               u64 objectid, u64 uid)
{
	struct rb_node *node = fs_info->usrquota_tree.rb_node;
	struct btrfs_usrquota *usrquota;

	while (node) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		if (usrquota->uq_objectid > objectid)
			node = node->rb_left;
		else if (usrquota->uq_objectid < objectid)
			node = node->rb_right;
		else if (usrquota->uq_uid > uid)
			node = node->rb_left;
		else if (usrquota->uq_uid < uid)
			node = node->rb_right;
		else
			return usrquota;
	}
	return NULL;
}

static struct btrfs_usrquota *__add_usrquota_rb(struct btrfs_fs_info *fs_info,
                                                u64 objectid, u64 uid, int subtree_check)
{
	struct rb_node **p = &fs_info->usrquota_tree.rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_usrquota *usrquota;
	struct btrfs_root *subvol_root;
	int subtree_loaded = 0;

	while (*p) {
		parent = *p;
		usrquota = rb_entry(parent, struct btrfs_usrquota, uq_node);

		if (usrquota->uq_objectid > objectid)
			p = &(*p)->rb_left;
		else if (usrquota->uq_objectid < objectid)
			p = &(*p)->rb_right;
		else if (usrquota->uq_uid > uid) {
			subtree_loaded = 1;
			p = &(*p)->rb_left;
		} else if (usrquota->uq_uid < uid) {
			subtree_loaded = 1;
			p = &(*p)->rb_right;
		} else
			return usrquota;
	}

	if (subtree_check && !subtree_loaded) {
		subvol_root = btrfs_get_fs_root(fs_info, objectid, true);
		if (IS_ERR(subvol_root)) {
			btrfs_err(fs_info, "Failed to get subvol from rootid[%llu].", objectid);
		} else {
			btrfs_debug(fs_info, "Usrquota subtree for rootid [%llu], read-only[%d], is not loaded.",
			    objectid, btrfs_root_readonly(subvol_root));
			btrfs_put_root(subvol_root);
		}
		return ERR_PTR(-ENOENT);
	}

	usrquota = kzalloc(sizeof(*usrquota), GFP_ATOMIC);
	if (!usrquota)
		return ERR_PTR(-ENOMEM);

	usrquota->uq_objectid = objectid;
	usrquota->uq_uid = uid;
	INIT_LIST_HEAD(&usrquota->uq_dirty);

	rb_link_node(&usrquota->uq_node, parent, p);
	rb_insert_color(&usrquota->uq_node, &fs_info->usrquota_tree);

	return usrquota;
}

/*
 * Caller of add_usrquota_rb[_no_check] should hold usrquota_lock
 */
static inline struct btrfs_usrquota *add_usrquota_rb_nocheck(struct btrfs_fs_info *fs_info,
                                                    u64 objectid, u64 uid)
{
	return __add_usrquota_rb(fs_info, objectid, uid, 0);
}

static inline struct btrfs_usrquota *add_usrquota_dummy_rb_nocheck(struct btrfs_fs_info *fs_info,
                                                    u64 objectid)
{
	struct btrfs_usrquota *usrquota;

	usrquota =  __add_usrquota_rb(fs_info, objectid, 0, 0);
	if (!IS_ERR_OR_NULL(usrquota))
		usrquota->uq_refcnt = 1;
	return usrquota;
}

static inline struct btrfs_usrquota *add_usrquota_rb(struct btrfs_fs_info *fs_info,
                                                    u64 objectid, u64 uid)
{
	return __add_usrquota_rb(fs_info, objectid, uid, 1);
}

static int del_usrquota_rb(struct btrfs_fs_info *fs_info, struct btrfs_usrquota *usrquota)
{
	rb_erase(&usrquota->uq_node, &fs_info->usrquota_tree);
	list_del(&usrquota->uq_dirty);
	kfree(usrquota);
	return 0;
}

static void usrquota_free_reserve(struct btrfs_fs_info *fs_info,
			struct btrfs_usrquota *usrquota,
			struct btrfs_inode *b_inode, u64 num_bytes)
{
#ifdef USRQUOTA_DEBUG
	printk(KERN_INFO "usrquota_free_reserve debug: root = %llu, ino = %lu, uid = %llu, "
		"reserved = %llu, to_free = %llu", usrquota->uq_objectid,
		(b_inode) ? b_inode->vfs_inode.i_ino : 0,
		usrquota->uq_uid, usrquota->uq_reserved, num_bytes);
#endif /* USRQUOTA_DEBUG */

	if (usrquota->uq_reserved >= num_bytes)
		usrquota->uq_reserved -= num_bytes;
	else {
		WARN_ONCE(1, "user quota root %llu uid %llu reserved space underflow, "
			"have %llu to free %llu",
			usrquota->uq_objectid, usrquota->uq_uid,
			usrquota->uq_reserved, num_bytes);
		usrquota->uq_reserved = 0;
	}

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags) &&
			!btrfs_usrquota_fast_chown_enable(&b_inode->vfs_inode))
		return;

	if (b_inode->uq_reserved >= num_bytes)
		b_inode->uq_reserved -= num_bytes;
	else {
		WARN_ONCE(1, "user quota root %llu inode %llu reserved space underflow, "
			"have %llu to free %llu",
			usrquota->uq_objectid, b_inode->location.objectid,
			b_inode->uq_reserved, num_bytes);
		b_inode->uq_reserved = 0;
	}
}

/*
 * Caller of usrquota_subtree_load has responsibility to call usrquota_subtree_unload
 * to dec reference count except btrfs_usrquota_load_config
 * Caller should make sure user quota won't leave us.
 */
int usrquota_subtree_load(struct btrfs_fs_info *fs_info, u64 rootid)
{
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *usrquota_root = fs_info->usrquota_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_usrquota_info_item *info_item;
	struct btrfs_usrquota_limit_item *limit_item;
	struct btrfs_usrquota *usrquota;
	int slot;
	int ret = 0;
	struct rb_node *node;

	spin_lock(&fs_info->usrquota_lock);
	if (!fs_info->usrquota_root) {
		spin_unlock(&fs_info->usrquota_lock);
		return -EINVAL;
	}

	node = find_usrquota_first_rb(fs_info, rootid);
	if (node) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		usrquota->uq_refcnt++;
		spin_unlock(&fs_info->usrquota_lock);
		return 0;
	}

	// insert a dummy node to identify if subtree is loaded or not
	usrquota = add_usrquota_dummy_rb_nocheck(fs_info, rootid);
	if (IS_ERR(usrquota)) {
		spin_unlock(&fs_info->usrquota_lock);
		return PTR_ERR(usrquota);
	}
	spin_unlock(&fs_info->usrquota_lock);

	path = btrfs_alloc_path();
	if (!path) {
		return -ENOMEM;
	}

	key.objectid = rootid;
	key.type = 0;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(usrquota_root, &key, path, 1, 0);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = 0;
		goto out;
	}
	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.objectid > rootid)
			break;
		if (found_key.type != BTRFS_USRQUOTA_INFO_KEY &&
		    found_key.type != BTRFS_USRQUOTA_LIMIT_KEY)
			goto next_item;

		spin_lock(&fs_info->usrquota_lock);
		if (!fs_info->usrquota_root) {
			spin_unlock(&fs_info->usrquota_lock);
			ret = -EINVAL;
			goto out;
		}

		usrquota = add_usrquota_rb_nocheck(fs_info, found_key.objectid, found_key.offset);
		if (IS_ERR(usrquota)) {
			spin_unlock(&fs_info->usrquota_lock);
			ret = PTR_ERR(usrquota);
			goto out;
		}

		switch (found_key.type) {
		case BTRFS_USRQUOTA_INFO_KEY:
			info_item = btrfs_item_ptr(leaf, slot,
					           struct btrfs_usrquota_info_item);
			usrquota->uq_generation = btrfs_usrquota_info_generation(leaf, info_item);
			usrquota->uq_rfer_used = btrfs_usrquota_info_rfer_used(leaf, info_item);
			break;
		case BTRFS_USRQUOTA_LIMIT_KEY:
			limit_item = btrfs_item_ptr(leaf, slot,
					            struct btrfs_usrquota_limit_item);
			usrquota->uq_rfer_soft = btrfs_usrquota_limit_rfer_soft(leaf, limit_item);
			usrquota->uq_rfer_hard = btrfs_usrquota_limit_rfer_hard(leaf, limit_item);
			break;
		}
		spin_unlock(&fs_info->usrquota_lock);
next_item:
		ret = btrfs_next_item(usrquota_root, path);
		if (ret < 0) {
			btrfs_err(fs_info, "failed to get next_item of usrquota tree");
			goto out;
		}
		if (ret) {
			ret = 0;
			break;
		}
	}
out:
	if (ret) {
		 // refcnt is possible greater than 1,
		 // such that we use unload function to check refcnt
		usrquota_subtree_unload(fs_info, rootid);
	}
	btrfs_free_path(path);
	return ret;
}

static void usrquota_ro_subvol_check(struct btrfs_fs_info *fs_info, struct btrfs_root *root)
{
	int ret;
	mutex_lock(&fs_info->usrquota_ro_roots_lock);

	if (!btrfs_root_readonly(root)) {
		goto out;
	}

	root->usrquota_loaded_gen = fs_info->generation;
	if (!list_empty(&root->usrquota_ro_root))
		goto out;

	ret = usrquota_subtree_load(fs_info, root->root_key.objectid);
	if (ret) {
		btrfs_err(fs_info, "failed to load ro subvol usrquota subtree [%llu].",
			root->root_key.objectid);
		goto out;
	}
	btrfs_debug(fs_info, "Load ro sub [id:%llu, gen:%llu]", root->root_key.objectid,
		root->usrquota_loaded_gen);
	list_add_tail(&root->usrquota_ro_root, &fs_info->usrquota_ro_roots);
	btrfs_grab_root(root);
out:
	mutex_unlock(&fs_info->usrquota_ro_roots_lock);
	return;
}

static void usrquota_subtree_unload_nolock(struct btrfs_fs_info *fs_info, u64 rootid)
{
	struct btrfs_usrquota *usrquota;
	struct rb_node *node;

	node = find_usrquota_first_rb(fs_info, rootid);
	if (node) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		WARN_ON(usrquota->uq_uid); // This should be the dummy node.
		if (usrquota->uq_refcnt > 1) {
			usrquota->uq_refcnt--;
			return;
		}
	}
	while (node) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		node = rb_next(node);
		if (usrquota->uq_objectid > rootid)
			break;
		del_usrquota_rb(fs_info, usrquota);
	}
}

// Caller should make sure user quota won't leave us.
void usrquota_subtree_unload(struct btrfs_fs_info *fs_info, u64 rootid)
{
	spin_lock(&fs_info->usrquota_lock);
	if (fs_info->usrquota_root)
		usrquota_subtree_unload_nolock(fs_info, rootid);
	spin_unlock(&fs_info->usrquota_lock);
}

static int usrquota_subtree_load_one(struct btrfs_fs_info *fs_info, struct btrfs_path *path, struct list_head *subvol_queue, u64 subvol_id)
{
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *subvol_root;
	struct extent_buffer *leaf;
	struct btrfs_subvol_list *subvol_list;
	int slot;
	int ret = 0;

	subvol_root = btrfs_get_fs_root(fs_info, subvol_id, true);
	if (IS_ERR(subvol_root)) {
		ret = PTR_ERR(subvol_root);
		goto out;
	}
	if (btrfs_root_readonly(subvol_root))
		goto update_queue;

	ret = usrquota_subtree_load(fs_info, subvol_id);
	if (ret) {
		goto out;
	}

update_queue:
	if (btrfs_root_noload_usrquota(subvol_root))
		goto out;

	key.objectid = subvol_id;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(fs_info->tree_root, &key, path, 1, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = 0; //no entry
		goto out;
	}
	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.type != BTRFS_ROOT_REF_KEY)
			break;

		if (!IS_ERR_OR_NULL(subvol_root))
			btrfs_put_root(subvol_root);
		subvol_root = btrfs_get_fs_root(fs_info, found_key.offset, true);
		if (IS_ERR(subvol_root)) {
			ret = PTR_ERR(subvol_root);
			goto out;
		}

		subvol_list = kzalloc(sizeof(*subvol_list), GFP_KERNEL);
		if (!subvol_list) {
			ret = -ENOMEM;
			goto out;
		}
		INIT_LIST_HEAD(&subvol_list->list);
		subvol_list->subvol_id = found_key.offset;
		list_add_tail(&subvol_list->list, subvol_queue);
		ret = btrfs_next_item(fs_info->tree_root, path);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			ret = 0; //no entry
			goto out;
		}
	}
out:
	btrfs_release_path(path);
	if (!IS_ERR_OR_NULL(subvol_root))
		btrfs_put_root(subvol_root);
	return ret;
}

static int usrquota_subtree_load_all(struct btrfs_fs_info *fs_info)
{
	struct btrfs_path *path = NULL;
	struct list_head subvol_queue;
	struct btrfs_subvol_list *subvol_list;
	int ret = 0;

	INIT_LIST_HEAD(&subvol_queue);
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	subvol_list = kzalloc(sizeof(*subvol_list), GFP_KERNEL);
	if (!subvol_list) {
		ret = -ENOMEM;
		goto out;
	}
	INIT_LIST_HEAD(&subvol_list->list);
	subvol_list->subvol_id = BTRFS_FS_TREE_OBJECTID;
	list_add_tail(&subvol_list->list, &subvol_queue);

	while (!list_empty(&subvol_queue)) {
		subvol_list = list_first_entry(&subvol_queue, struct btrfs_subvol_list, list);
		list_del_init(&subvol_list->list);
		if (!ret) {
			ret = usrquota_subtree_load_one(fs_info, path, &subvol_queue, subvol_list->subvol_id);
			if (ret) {
				btrfs_err(fs_info, "failed to load usrquota subtree %llu, ret=%d", subvol_list->subvol_id, ret);
			}
		}
		kfree(subvol_list);
	}
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_read_usrquota_compat_config(struct btrfs_fs_info *fs_info)
{
	int ret = 0;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *usrquota_root = fs_info->usrquota_root;
	struct extent_buffer *leaf;
	struct btrfs_usrquota_compat_item *compat_item;
	int slot;

        if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	fs_info->usrquota_compat_flags = 0;

	key.objectid = 0;
	key.type = BTRFS_USRQUOTA_COMPAT_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, usrquota_root, &key, path, 0, 0);
	if (ret) {
		if (ret > 0) {
			fs_info->usrquota_compat_flags = BTRFS_USRQUOTA_COMPAT_FLAG;
			ret = 0;
		}
		goto out;
	}

	slot = path->slots[0];
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, slot);

	compat_item = btrfs_item_ptr(leaf, slot,
			     struct btrfs_usrquota_compat_item);

	fs_info->usrquota_compat_flags |= btrfs_usrquota_compat_flags(leaf, compat_item);
	fs_info->usrquota_compat_flags &= BTRFS_USRQUOTA_COMPAT_FLAG;

	if (btrfs_usrquota_compat_generation(leaf, compat_item) != fs_info->generation) {
		fs_info->usrquota_compat_flags = 0;
	}
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_read_usrquota_config(struct btrfs_fs_info *fs_info)
{
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *usrquota_root = fs_info->usrquota_root;
	struct btrfs_path *path = NULL;
	struct extent_buffer *leaf;
	struct btrfs_usrquota_status_item *status_item;
	int slot;
	int ret = 0;

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags))
		return 0;

	// Enable user quota only if we have enabled qgroup.
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags))
		goto out;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	/* default this to quota off, in case no status key is found */
	fs_info->usrquota_flags = 0;

	key.objectid = 0;
	key.type = BTRFS_USRQUOTA_STATUS_KEY;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(usrquota_root, &key, path, 1, 0);
	if (ret) {
		// Disable user quota but don't fail the mount process.
		ret = 0;
		goto out;
	}

	slot = path->slots[0];
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, slot);

	if (found_key.type != BTRFS_USRQUOTA_STATUS_KEY)
		goto out;

	status_item = btrfs_item_ptr(leaf, slot,
			     struct btrfs_usrquota_status_item);

	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			btrfs_usrquota_status_version(leaf, status_item) !=
			BTRFS_USRQUOTA_STATUS_VERSION) {
		btrfs_err(fs_info,
				"syno user quota v1 found bad %llu version, quota disabled",
				btrfs_usrquota_status_version(leaf, status_item));
		goto out;
	}
	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags) &&
			btrfs_usrquota_status_version(leaf, status_item) !=
			BTRFS_USRQUOTA_V2_STATUS_VERSION) {
		btrfs_err(fs_info,
				"syno user quota v2 found bad %llu version, quota disabled",
				btrfs_usrquota_status_version(leaf, status_item));
		goto out;
	}

	if (btrfs_usrquota_status_generation(leaf, status_item) != fs_info->generation) {
		fs_info->usrquota_flags |= BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT;
		btrfs_err(fs_info,
			"user quota generation mismatch, marked as inconsistent");
	}

	fs_info->usrquota_flags |= btrfs_usrquota_status_flags(leaf, status_item);
	btrfs_release_path(path);

	ret = btrfs_read_usrquota_compat_config(fs_info);
	if (ret)
		goto out;

	ret = usrquota_subtree_load_all(fs_info);
out:
	if (ret)
		fs_info->usrquota_flags &= ~BTRFS_USRQUOTA_STATUS_FLAG_ON;

	if (!(fs_info->usrquota_flags & BTRFS_USRQUOTA_STATUS_FLAG_ON)) {
		clear_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags);
		clear_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags);
		btrfs_err(fs_info, "usrquota disabled due to faield to load tree\n");
	}

	btrfs_free_path(path);
	return ret;
}

/*
 * Called in close_ctree() when user quota is still enabled.  This verifies we don't
 * leak some reserved space.
 *
 * Return false if no reserved space is left.
 * Return true if some reserved space is leaked.
 */
bool btrfs_check_usrquota_leak(struct btrfs_fs_info *fs_info)
{
	struct rb_node *node;
	struct btrfs_usrquota *usrquota;
	bool ret = false;

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags))
		return ret;
	/*
	 * Since we're unmounting, there is no race and no need to grab usrquota
	 * lock.  And here we don't go post-order to provide a more user
	 * friendly sorted result.
	 */
	for (node = rb_first(&fs_info->usrquota_tree); node; node = rb_next(node)) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		if (usrquota->uq_reserved) {
			ret = true;
			btrfs_warn(fs_info, "user quota %llu:%llu has unreleased space = %llu",
				usrquota->uq_objectid, usrquota->uq_uid, usrquota->uq_reserved);
		}
	}
	return ret;
}

void btrfs_free_usrquota_config(struct btrfs_fs_info *fs_info)
{
	struct rb_node *node;
	struct btrfs_usrquota *usrquota;

	while ((node = rb_first(&fs_info->usrquota_tree))) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		del_usrquota_rb(fs_info, usrquota);
	}
}

static int update_usrquota_root_item(struct btrfs_trans_handle *trans,
                                      struct btrfs_path *path,
                                      u64 objectid, int info_item_diff, int limit_item_diff)
{
	struct btrfs_key key;
	struct extent_buffer *leaf = NULL;
	struct btrfs_usrquota_root_item *usrquota_root = NULL;
	u64 info_item_cnt;
	u64 limit_item_cnt;
	int ret;

	key.objectid = objectid;
	key.type = BTRFS_USRQUOTA_ROOT_KEY;
	key.offset = 0;
	ret = btrfs_insert_empty_item(trans, trans->fs_info->usrquota_root, path, &key,
                                  sizeof(struct btrfs_usrquota_root_item));
	if (ret && ret != -EEXIST)
		goto out;

	leaf = path->nodes[0];
	usrquota_root = btrfs_item_ptr(leaf, path->slots[0],
                                   struct btrfs_usrquota_root_item);
	if (ret == -EEXIST) {
		info_item_cnt = btrfs_usrquota_root_info_item_cnt(leaf, usrquota_root);
		limit_item_cnt = btrfs_usrquota_root_limit_item_cnt(leaf, usrquota_root);
		info_item_cnt += info_item_diff;
		limit_item_cnt += limit_item_diff;
	} else {
		info_item_cnt = info_item_diff;
		limit_item_cnt = limit_item_diff;
	}

	if (info_item_cnt > (1ULL << 63) || limit_item_cnt > (1ULL << 63)) {
		WARN_ON(1);
		ret = -ERANGE;
		goto out;
	}

	btrfs_set_usrquota_root_info_item_cnt(leaf, usrquota_root, info_item_cnt);
	btrfs_set_usrquota_root_limit_item_cnt(leaf, usrquota_root, limit_item_cnt);
	btrfs_mark_buffer_dirty(leaf);
	ret = 0;
out:
	btrfs_release_path(path);
	return ret;
}

static int remove_usrquota_item(struct btrfs_trans_handle *trans,
					u64 rootid, u64 uid, int type)
{
	int ret = 0;
	struct btrfs_root *usrquota_root = trans->fs_info->usrquota_root;
	struct btrfs_path *path;
	struct btrfs_key key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = rootid;
	key.offset = uid;
	key.type = type;
	ret = btrfs_search_slot(trans, usrquota_root, &key, path, -1, 1);
	if (ret) {
		ret = 0;
		goto out;
	}

	ret = btrfs_del_item(trans, usrquota_root, path);
	if (ret) {
		btrfs_err(trans->fs_info, "failed to delete user quota item, rootid=%llu, uid=%llu\n", rootid, uid);
		goto out;
	}
	btrfs_release_path(path);

	if (BTRFS_USRQUOTA_INFO_KEY == type)
		ret = update_usrquota_root_item(trans, path, rootid, -1, 0);
	else if (BTRFS_USRQUOTA_LIMIT_KEY == type)
		ret = update_usrquota_root_item(trans, path, rootid, 0, -1);
	if (ret) {
		btrfs_err(trans->fs_info, "failed to dec user quota item cnt, rootid=%llu, uid=%llu\n", rootid, uid);
		goto out;
	}
out:
	btrfs_free_path(path);
	return ret;
}

static int update_usrquota_limit_item(struct btrfs_trans_handle *trans,
                                      u64 objectid, u64 uid, u64 rfer_soft, u64 rfer_hard)
{
	struct btrfs_root *usrquota_root = trans->fs_info->usrquota_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_usrquota_limit_item *usrquota_limit;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = objectid;
	key.type = BTRFS_USRQUOTA_LIMIT_KEY;
	key.offset = uid;
	ret = btrfs_insert_empty_item(trans, usrquota_root, path, &key,
	                              sizeof(struct btrfs_usrquota_limit_item));
	if (ret && ret != -EEXIST)
		goto out;

	leaf = path->nodes[0];
	usrquota_limit = btrfs_item_ptr(leaf, path->slots[0],
	                                struct btrfs_usrquota_limit_item);
	btrfs_set_usrquota_limit_rfer_soft(leaf, usrquota_limit, rfer_soft);
	btrfs_set_usrquota_limit_rfer_hard(leaf, usrquota_limit, rfer_hard);
	btrfs_mark_buffer_dirty(leaf);

	if (ret == 0) {
		btrfs_release_path(path);
		ret = update_usrquota_root_item(trans, path, objectid, 0, 1);
	} else // ret == -EEXIST
		ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int update_usrquota_info_item(struct btrfs_trans_handle *trans,
                                     u64 objectid, u64 uid, u64 rfer_used)
{
	struct btrfs_root *usrquota_root = trans->fs_info->usrquota_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_usrquota_info_item *usrquota_info;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = objectid;
	key.type = BTRFS_USRQUOTA_INFO_KEY;
	key.offset = uid;
	ret = btrfs_insert_empty_item(trans, usrquota_root, path, &key,
                                      sizeof(struct btrfs_usrquota_info_item));
	if (ret && ret != -EEXIST)
		goto out;

	leaf = path->nodes[0];
	usrquota_info = btrfs_item_ptr(leaf, path->slots[0],
	                               struct btrfs_usrquota_info_item);
	btrfs_set_usrquota_info_rfer_used(leaf, usrquota_info, rfer_used);
	btrfs_set_usrquota_info_generation(leaf, usrquota_info, trans->transid);
	btrfs_mark_buffer_dirty(leaf);

	if (ret == 0) {
		btrfs_release_path(path);
		ret = update_usrquota_root_item(trans, path, objectid, 1, 0);
	} else // ret == -EEXIST
		ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int update_usrquota_status_item(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *root = fs_info->usrquota_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_usrquota_status_item *ptr;
	int ret;
	int slot;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_USRQUOTA_STATUS_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto out;
	}

	leaf = path->nodes[0];
	slot = path->slots[0];
	ptr = btrfs_item_ptr(leaf, slot, struct btrfs_usrquota_status_item);
	btrfs_set_usrquota_status_flags(leaf, ptr, fs_info->usrquota_flags);
	btrfs_set_usrquota_status_generation(leaf, ptr, trans->transid);
	btrfs_mark_buffer_dirty(leaf);

out:
	btrfs_free_path(path);
	return ret;
}

static int insert_usrquota_compat_item(struct btrfs_trans_handle *trans,
		struct btrfs_fs_info *fs_info, struct btrfs_root *root)
{
	struct btrfs_path *path = NULL;
	struct btrfs_usrquota_compat_item *ptr;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = 0;
	key.type = BTRFS_USRQUOTA_COMPAT_KEY;
	key.offset = 0;
	ret = btrfs_insert_empty_item(trans, root, path, &key, sizeof(*ptr));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	ptr = btrfs_item_ptr(leaf, path->slots[0],
	                     struct btrfs_usrquota_compat_item);
	btrfs_set_usrquota_compat_generation(leaf, ptr, trans->transid);
	btrfs_set_usrquota_compat_flags(leaf, ptr, fs_info->usrquota_compat_flags);
	btrfs_mark_buffer_dirty(leaf);

out:
	btrfs_free_path(path);
	return ret;
}

static int update_usrquota_compat_item(struct btrfs_trans_handle *trans,
		struct btrfs_fs_info *fs_info, struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_usrquota_compat_item *ptr;
	int ret;
	int slot;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = 0;
	key.type = BTRFS_USRQUOTA_COMPAT_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret) {
		if (ret > 0) {
			btrfs_release_path(path);
			ret = insert_usrquota_compat_item(trans, fs_info, root);
		}
		goto out;
	}

	leaf = path->nodes[0];
	slot = path->slots[0];
	ptr = btrfs_item_ptr(leaf, slot, struct btrfs_usrquota_compat_item);
	btrfs_set_usrquota_compat_generation(leaf, ptr, trans->transid);
	btrfs_set_usrquota_compat_flags(leaf, ptr, fs_info->usrquota_compat_flags);
	btrfs_mark_buffer_dirty(leaf);

out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_clean_usrquota_tree(struct btrfs_trans_handle *trans,
                                     struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
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

int btrfs_usrquota_enable(struct btrfs_fs_info *fs_info, u64 cmd)
{
	struct btrfs_root *usrquota_root;
	struct btrfs_path *path = NULL;
	struct btrfs_usrquota_status_item *ptr;
	struct extent_buffer *leaf;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_key key;
	int ret = 0;

	// Default using v2 quota.
	if (cmd == BTRFS_USRQUOTA_CTL_ENABLE)
		cmd = BTRFS_USRQUOTA_V2_CTL_ENABLE;

	if (btrfs_test_opt(fs_info, NO_QUOTA_TREE)) {
		btrfs_info(fs_info, "Can't enable usrquota with mount_opt no_quota_tree");
		return -EINVAL;
	}

	/*
	 * Protected by fs_info->subvol_sem, so qgroup will not do disable
	 * before we finish user quota enable.
	 */
	if (!test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_QUOTA_V2_ENABLED, &fs_info->flags)) {
		btrfs_warn(fs_info,
			"Should enable qgroup before enable user quota.");
		return -EINVAL;
	}
	down_write(&fs_info->inflight_reserve_lock);
	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (fs_info->usrquota_root)
		goto out;
	mutex_unlock(&fs_info->usrquota_ioctl_lock);

	trans = btrfs_start_transaction(fs_info->tree_root, 2);
	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	if (fs_info->usrquota_root)
		goto out;

	if (cmd == BTRFS_USRQUOTA_V1_CTL_ENABLE)
		usrquota_root = btrfs_create_tree(trans, BTRFS_USRQUOTA_TREE_OBJECTID);
	else
		usrquota_root = btrfs_create_tree(trans, BTRFS_SYNO_USRQUOTA_V2_TREE_OBJECTID);
	if (IS_ERR(usrquota_root)) {
		ret = PTR_ERR(usrquota_root);
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
	key.type = BTRFS_USRQUOTA_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_insert_empty_item(trans, usrquota_root, path, &key, sizeof(*ptr));
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_root;
	}

	leaf = path->nodes[0];
	ptr = btrfs_item_ptr(leaf, path->slots[0],
	                     struct btrfs_usrquota_status_item);
	btrfs_set_usrquota_status_generation(leaf, ptr, trans->transid);
	if (cmd == BTRFS_USRQUOTA_V1_CTL_ENABLE)
		btrfs_set_usrquota_status_version(leaf, ptr, BTRFS_USRQUOTA_STATUS_VERSION);
	else
		btrfs_set_usrquota_status_version(leaf, ptr, BTRFS_USRQUOTA_V2_STATUS_VERSION);
	fs_info->usrquota_flags = BTRFS_USRQUOTA_STATUS_FLAG_ON |
				  BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT;
	btrfs_set_usrquota_status_flags(leaf, ptr, fs_info->usrquota_flags);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	if (cmd == BTRFS_USRQUOTA_V1_CTL_ENABLE) {
		fs_info->usrquota_compat_flags = BTRFS_USRQUOTA_COMPAT_FLAG;
		ret = insert_usrquota_compat_item(trans, fs_info, usrquota_root);
		if (ret) {
			fs_info->usrquota_compat_flags = 0;
			btrfs_abort_transaction(trans, ret);
			goto out_free_root;
		}
	}

	fs_info->usrquota_root = usrquota_root;

	ret = usrquota_subtree_load_all(fs_info);
	if (ret) {
		btrfs_err(fs_info, "failed to init usrquota subtree during enable usrquota");
		btrfs_abort_transaction(trans, ret);
		goto out_free_root;
	}

	ret = btrfs_commit_transaction(trans);
	trans = NULL;
	if (ret)
		goto out_free_root;

	/*
	 * Set quota enabled flag after committing the transaction, to avoid
	 * deadlocks on fs_info->usrquota_ioctl_lock with concurrent snapshot
	 * creation.
	 */
	spin_lock(&fs_info->usrquota_lock);
	fs_info->usrquota_root = usrquota_root;
	if (cmd == BTRFS_USRQUOTA_V1_CTL_ENABLE)
		set_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags);
	else
		set_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags);
	spin_unlock(&fs_info->usrquota_lock);

out_free_root:
	if (ret) {
		fs_info->usrquota_root = NULL;
		btrfs_put_root(usrquota_root);
	}
out:
	btrfs_free_path(path);
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	up_write(&fs_info->inflight_reserve_lock);
	if (ret && trans)
		btrfs_end_transaction(trans);
	else if (trans)
		ret = btrfs_end_transaction(trans);
	return ret;
}

int btrfs_usrquota_disable(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *usrquota_root;
	struct btrfs_trans_handle *trans = NULL;
	int ret = 0;

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!fs_info->usrquota_root)
		goto out;
	mutex_unlock(&fs_info->usrquota_ioctl_lock);

	trans = btrfs_start_transaction(fs_info->tree_root, 1);

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	if (!fs_info->usrquota_root)
		goto out;

	clear_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags);
	clear_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags);
	btrfs_qgroup_wait_for_completion(fs_info, false);
	spin_lock(&fs_info->usrquota_lock);
	usrquota_root = fs_info->usrquota_root;
	fs_info->usrquota_root = NULL;
	fs_info->usrquota_flags &= ~BTRFS_USRQUOTA_STATUS_FLAG_ON;
	spin_unlock(&fs_info->usrquota_lock);

	btrfs_free_usrquota_config(fs_info);

	ret = btrfs_clean_usrquota_tree(trans, usrquota_root);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = btrfs_del_root(trans, &usrquota_root->root_key);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	list_del(&usrquota_root->dirty_list);

	btrfs_tree_lock(usrquota_root->node);
	btrfs_clean_tree_block(usrquota_root->node);
	btrfs_tree_unlock(usrquota_root->node);
	btrfs_free_tree_block(trans, usrquota_root, usrquota_root->node, 0, 1);

	btrfs_put_root(usrquota_root);

out:
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	if (ret && trans)
		btrfs_end_transaction(trans);
	else if (trans)
		ret = btrfs_end_transaction(trans);

	return ret;
}

int btrfs_usrquota_unload(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *usrquota_root;
	struct btrfs_trans_handle *trans = NULL;
	int ret = 0;

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!fs_info->usrquota_root)
		goto out;
	mutex_unlock(&fs_info->usrquota_ioctl_lock);

	trans = btrfs_start_transaction(fs_info->tree_root, 1);

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	if (!fs_info->usrquota_root)
		goto out;

	clear_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags);
	clear_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags);
	btrfs_qgroup_wait_for_completion(fs_info, false);
	spin_lock(&fs_info->usrquota_lock);
	usrquota_root = fs_info->usrquota_root;
	fs_info->usrquota_root = NULL;
	fs_info->usrquota_flags &= ~BTRFS_USRQUOTA_STATUS_FLAG_ON;
	spin_unlock(&fs_info->usrquota_lock);

	btrfs_free_usrquota_config(fs_info);

	btrfs_commit_transaction(trans);
	trans = NULL;

	list_del(&usrquota_root->dirty_list);
	btrfs_put_root(usrquota_root);

out:
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	if (ret && trans)
		btrfs_end_transaction(trans);
	else if (trans)
		ret = btrfs_end_transaction(trans);

	return ret;
}

int btrfs_usrquota_remove_v1(struct btrfs_fs_info *fs_info)
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

	// Read old user quota root.
	location.objectid = BTRFS_USRQUOTA_TREE_OBJECTID;
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

/*
 * This function should protected by usrquota_lock
 */
static void usrquota_dirty(struct btrfs_fs_info *fs_info,
                           struct btrfs_usrquota *usrquota)
{
	if (list_empty(&usrquota->uq_dirty))
		list_add(&usrquota->uq_dirty, &fs_info->dirty_usrquota);
}

int btrfs_usrquota_limit(struct btrfs_trans_handle *trans,
                         u64 objectid, u64 uid, u64 rfer_soft, u64 rfer_hard)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_usrquota *usrquota;
	kuid_t tmp_uid;
	u64 kernel_uid;
	int ret = 0;
#ifdef MY_ABC_HERE
	struct btrfs_root *root = trans->root;
	bool need_check = false;
#endif /* MY_ABC_HERE */

	tmp_uid = make_kuid(current_user_ns(), (uid_t)uid);
	if (!uid_valid(tmp_uid))
		return -EINVAL;
	kernel_uid = __kuid_val(tmp_uid);

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!fs_info->usrquota_root) {
		ret = -ESRCH;
		goto out;
	}

	spin_lock(&fs_info->usrquota_lock);
	usrquota = add_usrquota_rb(fs_info, objectid, kernel_uid);
	if (IS_ERR(usrquota)) {
		spin_unlock(&fs_info->usrquota_lock);
		ret = PTR_ERR(usrquota);
		goto out;
	}
#ifdef MY_ABC_HERE
	if ((rfer_soft || rfer_hard) && !btrfs_root_has_usrquota_limit(root))
		btrfs_root_set_has_usrquota_limit(root, true);
	// When update limit to zero, we should re-check quota limit.
	else if (!rfer_soft && !rfer_hard &&
	    (usrquota->uq_rfer_soft || usrquota->uq_rfer_hard))
		need_check = true;
#endif /* MY_ABC_HERE */
	usrquota->uq_rfer_soft = rfer_soft;
	usrquota->uq_rfer_hard = rfer_hard;
	spin_unlock(&fs_info->usrquota_lock);

	ret = update_usrquota_limit_item(trans, objectid, kernel_uid, rfer_soft, rfer_hard);
	if (ret) {
		fs_info->usrquota_flags |= BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT;
		btrfs_err(fs_info, "failed to update limit item");
		goto out;
	}

#ifdef MY_ABC_HERE
	if (need_check)
		btrfs_check_usrquota_limit(root);
#endif /* MY_ABC_HERE */
out:
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	return ret;
}

int btrfs_usrquota_clean(struct btrfs_trans_handle *trans, u64 uid)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_usrquota *usrquota;
	int ret = 0;
	kuid_t tmp_uid;
	u64 kernel_uid;
	u64 objectid = 0;

	tmp_uid = make_kuid(current_user_ns(), (uid_t)uid);
	if (!uid_valid(tmp_uid))
		return -EINVAL;
	kernel_uid = __kuid_val(tmp_uid);

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
		ret = -ESRCH;
		goto out;
	}

	while (1) {
		spin_lock(&fs_info->usrquota_lock);
		objectid = find_next_valid_objectid(fs_info, objectid);
		if (!objectid) {
			spin_unlock(&fs_info->usrquota_lock);
			break;
		}
		usrquota = find_usrquota_rb(fs_info, objectid, kernel_uid);
		if (usrquota) {
			usrquota->uq_rfer_soft = 0;
			usrquota->uq_rfer_hard = 0;
			spin_unlock(&fs_info->usrquota_lock);
			ret = remove_usrquota_item(trans, objectid, kernel_uid, BTRFS_USRQUOTA_LIMIT_KEY);
			if (ret) {
				btrfs_err(fs_info, "failed to remove limit item");
				break;
			}
		} else {
			spin_unlock(&fs_info->usrquota_lock);
		}
		objectid++;
	}
out:
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	return ret;
}

/*
 * Use after inode_add_bytes() / inode_sub_bytes(), so we are always in a transaction
 * and our accounting will be committed in btrfs_run_usrquota().
 */
int btrfs_usrquota_syno_accounting(struct btrfs_inode *b_inode,
		u64 add_bytes, u64 del_bytes, enum syno_quota_account_type type)
{
	struct btrfs_usrquota *usrquota;
	struct btrfs_root *root = b_inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct inode *inode = &b_inode->vfs_inode;
	u64 uid;
	u64 ref_root = root->root_key.objectid;
	u64 ino = b_inode->location.objectid;
	int ret = 0;

	if (!is_fstree(ref_root))
		return -EINVAL;

	if (add_bytes == del_bytes && type != UPDATE_QUOTA_FREE_RESERVED)
		return 0;

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags))
		return 0;

	if (btrfs_root_disable_quota(root))
		return 0;

	WARN_ON_ONCE(btrfs_root_readonly(root));
	spin_lock(&fs_info->usrquota_lock);

	if (!fs_info->usrquota_root)
		goto out;

	// Get uid after usrquota_lock, so that uid won't changed by btrfs_usrquota_transfer().
	uid = __kuid_val(inode->i_uid);
	usrquota = add_usrquota_rb(fs_info, ref_root, uid);
	if (IS_ERR(usrquota)) {
		ret = PTR_ERR(usrquota);
		goto out;
	}

	add_bytes = round_up(add_bytes, fs_info->sectorsize);
	del_bytes = round_up(del_bytes, fs_info->sectorsize);

#ifdef USRQUOTA_DEBUG
	printk(KERN_INFO "btrfs_usrquota_syno_accounting debug: root = %llu, ino = %lu, "
		"uid = %llu, used = %llu, type = %d, add_bytes = %llu, del_bytes = %llu",
		ref_root, inode->i_ino, uid, usrquota->uq_rfer_used, type, add_bytes, del_bytes);
#endif /* USRQUOTA_DEBUG */

	switch (type) {
	case ADD_QUOTA_RESCAN:
		usrquota->uq_rfer_used += add_bytes;
			break;
	case UPDATE_QUOTA_FREE_RESERVED:
		usrquota_free_reserve(fs_info, usrquota, b_inode, add_bytes);
		/* fall through */
	case UPDATE_QUOTA:
		if (btrfs_quota_rescan_check(root, ino)) {
			usrquota->uq_rfer_used += add_bytes;

			if (usrquota->uq_rfer_used < del_bytes) {
				if (!root->invalid_quota)
					WARN_ONCE(1, "user quota %llu:%llu ref underflow, "
						"have %llu to free %llu", ref_root, uid,
						usrquota->uq_rfer_used, del_bytes);
				usrquota->uq_rfer_used = 0;
				usrquota->need_rescan = true;
			} else
				usrquota->uq_rfer_used -= del_bytes;
		}
		break;
	}

	usrquota_dirty(fs_info, usrquota);

out:
	spin_unlock(&fs_info->usrquota_lock);
	return ret;
}

// Similar to btrfs_usrquota_syno_accounting().
int btrfs_usrquota_syno_v1_accounting(struct btrfs_trans_handle *trans,
					struct btrfs_quota_account_rec *record)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_usrquota *usrquota;
	struct inode *inode = record->inode;
	struct btrfs_inode *binode;
	u64 ref_root = record->ref_root;
	u64 num_bytes = record->num_bytes;
	u64 reserved = record->reserved;
	u64 uid = record->uid;
	int sign = record->sign;
	int ret = 0;
	bool fast_chown;

	if (!is_fstree(ref_root))
		return -EINVAL;

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags))
		return 0;

	spin_lock(&fs_info->usrquota_lock);

	if (!fs_info->usrquota_root)
		goto out;

	if (inode)
		uid =  __kuid_val(inode->i_uid);
	fast_chown = btrfs_usrquota_fast_chown_enable(inode);

	usrquota = add_usrquota_rb(fs_info, ref_root, uid);
	if (IS_ERR(usrquota)) {
		ret = PTR_ERR(usrquota);
		goto out;
	}

	num_bytes = round_up(num_bytes, fs_info->sectorsize);
	reserved = round_up(reserved, fs_info->sectorsize);

	// Update user quota.
	if (unlikely(sign < 0 && usrquota->uq_rfer_used < num_bytes)) {
		/*WARN_ONCE(1, "user quota root %llu uid %llu ref underflow, "
				"have %llu to free %llu", ref_root, uid,
				usrquota->uq_rfer_used, num_bytes);*/
		usrquota->uq_rfer_used = 0;
	} else
		usrquota->uq_rfer_used += sign * num_bytes;

	// Update user quota reserve.
	if (unlikely(usrquota->uq_reserved < reserved)) {
		WARN_ONCE(1, "user quota root %llu uid %llu reserved space underflow, "
			"have %llu to free %llu", ref_root, uid,
			usrquota->uq_reserved, reserved);
		usrquota->uq_reserved = 0;
	} else
		usrquota->uq_reserved -= reserved;

	// Update in-memory inode's quota for fast chown.
	if (fast_chown) {
		binode = BTRFS_I(inode);
		if (unlikely(binode->uq_reserved < reserved)) {
			WARN_ONCE(1, "inode %llu:%lu uq_reserved underflow, "
				"have %llu to free %llu", ref_root, inode->i_ino,
				binode->uq_reserved, reserved);
			binode->uq_reserved = 0;
		} else
			binode->uq_reserved -= reserved;


		if (unlikely(sign < 0 && (binode->uq_rfer_used < num_bytes))) {
			/*WARN_ONCE(1, "inode %llu:%lu uq ref underflow, "
				"have %llu to free %llu", ref_root, inode->i_ino,
				binode->uq_rfer_used, num_bytes);*/
			binode->uq_rfer_used = 0;
		} else
			binode->uq_rfer_used += sign * num_bytes;
	}

	usrquota_dirty(fs_info, usrquota);
out:
	spin_unlock(&fs_info->usrquota_lock);
	if (!ret && fast_chown) {
		struct btrfs_block_rsv *rsv = trans->block_rsv;
		if (test_bit(BTRFS_INODE_USRQUOTA_META_RESERVED, &BTRFS_I(inode)->runtime_flags)) {
			trans->block_rsv = &BTRFS_I(inode)->block_rsv;
		} else {
			trans->block_rsv = NULL;
		}
		ret = btrfs_update_inode_fallback(trans, BTRFS_I(inode)->root, inode);
		trans->block_rsv = rsv;
		if (ret)
			btrfs_abort_transaction(trans, ret);
	}

	return ret;
}

/*
 * Similar to btrfs_usrquota_syno_accounting(), but used only in rescan, where
 * we don't have in-memory inode.
 */
int btrfs_usrquota_syno_accounting_rescan(struct btrfs_root *root, u64 uid, u64 num_bytes)
{
	struct btrfs_usrquota *usrquota;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 subvol_id = root->root_key.objectid;
	int ret = 0;

	if (num_bytes == 0)
		return 0;

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags))
		return 0;

	usrquota_ro_subvol_check(fs_info, root);
	spin_lock(&fs_info->usrquota_lock);

	if (!fs_info->usrquota_root)
		goto out;

	num_bytes = round_up(num_bytes, fs_info->sectorsize);
	usrquota = add_usrquota_rb(fs_info, subvol_id, uid);
	if (IS_ERR(usrquota)) {
		ret = PTR_ERR(usrquota);
		goto out;
	}

	usrquota->uq_rfer_used += num_bytes;
	usrquota_dirty(fs_info, usrquota);

out:
	spin_unlock(&fs_info->usrquota_lock);
	return ret;
}

int btrfs_reset_usrquota_status(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *usrquota_root = fs_info->usrquota_root;
	struct btrfs_path *path = NULL;
	struct btrfs_usrquota_status_item *ptr;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	int ret;

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!fs_info->usrquota_root) {
		ret = -ENOENT;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = 0;
	key.type = BTRFS_USRQUOTA_STATUS_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(trans, usrquota_root, &key, path, 0, 1);
	if (ret)
		goto out;

	leaf = path->nodes[0];
	ptr = btrfs_item_ptr(leaf, path->slots[0],
				 struct btrfs_usrquota_status_item);
	fs_info->usrquota_flags &= ~BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT;
	btrfs_set_usrquota_status_flags(leaf, ptr, fs_info->usrquota_flags);
	btrfs_set_usrquota_status_generation(leaf, ptr, trans->transid);
	btrfs_set_usrquota_status_version(leaf, ptr, BTRFS_USRQUOTA_V2_STATUS_VERSION);
	btrfs_mark_buffer_dirty(leaf);

out:
	btrfs_free_path(path);
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	return ret;
}

int btrfs_syno_usrquota_transfer_limit(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *old_root = NULL;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path = NULL;
	struct extent_buffer *leaf;
	struct btrfs_usrquota_limit_item *ptr;
	struct btrfs_usrquota *usrquota;
	u64 subvol_id = 0;
	int ret = 0;
	int slot;

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!fs_info->usrquota_root) {
		ret = -ESRCH;
		goto out;
	}

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
		ret = -ESRCH;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
	path->reada = READA_FORWARD_ALWAYS;

	key.objectid = BTRFS_USRQUOTA_TREE_OBJECTID;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = 0;
	old_root = btrfs_read_tree_root(fs_info->tree_root, &key);
	if (IS_ERR(old_root)) {
		ret = PTR_ERR(old_root);
		old_root = NULL;
		goto out;
	}

	key.objectid = 0;
	key.type = BTRFS_USRQUOTA_LIMIT_KEY;
	key.offset = 0;
again:
	ret = btrfs_search_slot_for_read(old_root, &key, path, 1, 0);
	if (ret)
		goto out;

	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		// We may have many info items before us, jump to the limit item.
		if (found_key.type < BTRFS_USRQUOTA_LIMIT_KEY) {
			btrfs_release_path(path);
			key.objectid = found_key.objectid;
			key.type = BTRFS_USRQUOTA_LIMIT_KEY;
			key.offset = 0;
			goto again;
		}

		// BTRFS_USRQUOTA_COMPAT_KEY?
		if (found_key.type > BTRFS_USRQUOTA_LIMIT_KEY) {
			btrfs_release_path(path);
			key.objectid = found_key.objectid + 1;
			key.type = BTRFS_USRQUOTA_LIMIT_KEY;
			key.offset = 0;
			goto again;
		}

		ptr = btrfs_item_ptr(leaf, slot,
				     struct btrfs_usrquota_limit_item);
		if (subvol_id != found_key.objectid) {
			if (subvol_id)
				usrquota_subtree_unload(fs_info, subvol_id);
			subvol_id = found_key.objectid;
			ret = usrquota_subtree_load(fs_info, subvol_id);
			if (ret) {
				btrfs_release_path(path);
				key.objectid = subvol_id + 1;
				key.type = BTRFS_USRQUOTA_LIMIT_KEY;
				key.offset = 0;
				subvol_id = 0;
				goto again;
			}
		}

		spin_lock(&fs_info->usrquota_lock);
		usrquota = add_usrquota_rb(fs_info,
				found_key.objectid, found_key.offset);
		if (!IS_ERR(usrquota) && usrquota->uq_rfer_soft == 0
				&& usrquota->uq_rfer_hard == 0) {
			usrquota->uq_rfer_soft = btrfs_usrquota_limit_rfer_soft(leaf, ptr);
			usrquota->uq_rfer_hard = btrfs_usrquota_limit_rfer_hard(leaf, ptr);
			usrquota->update_limit = true;
			usrquota_dirty(fs_info, usrquota);
		}
		spin_unlock(&fs_info->usrquota_lock);

		ret = btrfs_next_item(old_root, path);
		if (ret)
			break;
	}

out:
	if (subvol_id)
		usrquota_subtree_unload(fs_info, subvol_id);
	btrfs_free_path(path);
	if (old_root) {
		free_extent_buffer(old_root->node);
		free_extent_buffer(old_root->commit_root);
		kfree(old_root);
	}
	mutex_unlock(&fs_info->usrquota_ioctl_lock);

	if (ret > 0)
		ret = 0;
	return ret;
}

void btrfs_usrquota_zero_tracking(struct btrfs_fs_info *fs_info, u64 subvol_id)
{
	struct btrfs_usrquota *usrquota;
	struct rb_node *node;
	int ret;

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags))
		return;

	/*
	 * We may have no user quota record in volume migration case.
	 * No need to print error.
	 */
	ret = usrquota_subtree_load(fs_info, subvol_id);
	if (ret)
		return;

	spin_lock(&fs_info->usrquota_lock);
	if (!fs_info->usrquota_root) {
		spin_unlock(&fs_info->usrquota_lock);
		return;
	}

	node = find_usrquota_first_rb(fs_info, subvol_id);
	while (node) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		node = rb_next(node);
		if (usrquota->uq_objectid > subvol_id)
			break;
		usrquota->uq_rfer_used = 0;
		usrquota->uq_generation = 0;
		usrquota_dirty(fs_info, usrquota);
	}
	spin_unlock(&fs_info->usrquota_lock);

	usrquota_subtree_unload(fs_info, subvol_id);
}

int btrfs_usrquota_v1_transfer(struct inode *inode, kuid_t new_uid)
{
	struct btrfs_usrquota *usrquota_orig;
	struct btrfs_usrquota *usrquota_dest;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 rootid = root->root_key.objectid;

	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_file_extent_item *fi;
	struct btrfs_trans_handle *trans;
	u64 disko;
	int ret = 0;
	int no_quota = 0;
	u64 num_bytes = 0;
	int type;
	u64 datal, diskl;
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct quota_check qc;
	u64 inflight_num_bytes = 0;
	u64 uid;

	if (btrfs_usrquota_fast_chown_enable(inode))
		goto transfer;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	// flush delayed write and wait for it
	btrfs_wait_ordered_range(inode, 0, (u64)-1);

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto search_end;
	}

	ret = btrfs_run_delayed_refs(trans, BTRFS_USRQUOTA_DELAYED_REF_SCAN);
	if (ret) {
		btrfs_end_transaction(trans);
		goto search_end;
	}
	ret = btrfs_end_transaction(trans);
	if (ret)
		goto search_end;

	key.objectid = BTRFS_I(inode)->location.objectid;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;

again:
	ret = btrfs_search_slot_for_read(root, &key, path, 1, 0);
	if (ret) {
		if (ret == 1) /* found nothing */
			ret = 0;
		goto search_end;
	}
	while (1) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		if (found_key.objectid != key.objectid)
			break;
		if (found_key.type != BTRFS_EXTENT_DATA_KEY)
			break;
		fi = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_file_extent_item);
		type = btrfs_file_extent_type(leaf, fi);
		if (type == BTRFS_FILE_EXTENT_REG || type == BTRFS_FILE_EXTENT_PREALLOC) {
			disko = btrfs_file_extent_disk_bytenr(leaf, fi);
			diskl = btrfs_file_extent_disk_num_bytes(leaf, fi);
			datal = btrfs_file_extent_num_bytes(leaf, fi);
			if (!disko)
				break;

			key.offset = found_key.offset + datal;
			btrfs_release_path(path);

			trans = btrfs_join_transaction(root);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				goto search_end;
			}

			qc.bytenr = disko;
			qc.root_objectid = rootid;
			qc.ino = inode->i_ino;
			qc.offset = found_key.offset;
			qc.in_run_delayed = false;
			no_quota = check_root_inode_ref(trans, &qc);
			ret = btrfs_end_transaction(trans);

			if (ret)
				goto search_end;
			if (no_quota < 0)
				goto search_end;
			if (!no_quota)
				num_bytes += diskl;
			goto again;
		}
		cond_resched();
		ret = btrfs_next_item(root, path);
		if (ret) {
			if (ret < 0)
				goto search_end;
			ret = 0;
			break;
		}
	}

search_end:
	btrfs_free_path(path);
	if (ret)
		return ret;

transfer:
	spin_lock(&fs_info->usrquota_lock);

	if (!fs_info->usrquota_root)
		goto unlock;

	if (binode->flags & BTRFS_INODE_UQ_REF_USED)
		num_bytes = binode->uq_rfer_used;
	inflight_num_bytes = binode->uq_reserved;

	uid = __kuid_val(inode->i_uid);
	usrquota_orig = add_usrquota_rb(fs_info, rootid, uid);
	if (IS_ERR(usrquota_orig)) {
		ret = PTR_ERR(usrquota_orig);
		goto unlock;
	}

	usrquota_dest = add_usrquota_rb(fs_info, rootid, (u64)__kuid_val(new_uid));
	if (IS_ERR(usrquota_dest)) {
		ret = PTR_ERR(usrquota_dest);
		goto unlock;
	}

	if (usrquota_dest->uq_rfer_hard && !capable(CAP_SYS_RESOURCE)) {
		if (usrquota_dest->uq_rfer_used + usrquota_dest->uq_reserved +
				num_bytes + inflight_num_bytes > usrquota_dest->uq_rfer_hard) {
			ret = -EDQUOT;
			goto unlock;
		}
	}

	if (unlikely(usrquota_orig->uq_rfer_used < num_bytes)) {
		/*WARN_ONCE(1, "user quota chown %llu:%llu ref underflow, "
			"have %llu to free %llu", rootid, uid,
			usrquota_orig->uq_rfer_used, num_bytes);*/

		usrquota_orig->uq_rfer_used = 0;
	} else
		usrquota_orig->uq_rfer_used -= num_bytes;
	usrquota_dest->uq_rfer_used += num_bytes;

	if (unlikely(usrquota_orig->uq_reserved < inflight_num_bytes)) {
		WARN_ONCE(1, "user quota chown %llu/%lu reserved underflow, "
			"have %llu to free %llu", rootid, inode->i_ino,
			usrquota_orig->uq_reserved, inflight_num_bytes);

		usrquota_orig->uq_reserved = 0;
	} else
		usrquota_orig->uq_reserved -= inflight_num_bytes;
	usrquota_dest->uq_reserved += inflight_num_bytes;

	inode->i_uid = new_uid; // Do this inside of usrquota_lock.
	usrquota_dirty(fs_info, usrquota_orig);
	usrquota_dirty(fs_info, usrquota_dest);

unlock:
	spin_unlock(&fs_info->usrquota_lock);
	return ret;
}

// For syno quota v2.
int btrfs_usrquota_transfer(struct inode *inode, kuid_t new_uid)
{
	struct btrfs_usrquota *usrquota_from, *usrquota_to;
	struct btrfs_inode *b_inode = BTRFS_I(inode);
	struct btrfs_root *root = b_inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 uid;
	u64 ref_root = root->root_key.objectid;
	u64 ino = b_inode->location.objectid;
	loff_t num_bytes;
	bool enforce = true;
	int ret = 0;

	if (!is_fstree(ref_root))
		return 0;

	if (capable(CAP_SYS_RESOURCE))
		enforce = false;

	usrquota_ro_subvol_check(fs_info, root);
	down_write(&root->rescan_lock);
	spin_lock(&fs_info->usrquota_lock);

	if (!fs_info->usrquota_root)
		goto out;

	uid = __kuid_val(inode->i_uid);
	usrquota_from = add_usrquota_rb(fs_info, ref_root, uid);
	if (IS_ERR(usrquota_from)) {
		ret = PTR_ERR(usrquota_from);
		goto out;
	}

	usrquota_to = add_usrquota_rb(fs_info, ref_root, (u64)__kuid_val(new_uid));
	if (IS_ERR(usrquota_to)) {
		ret = PTR_ERR(usrquota_to);
		goto out;
	}
	num_bytes = inode_get_bytes(inode);
	num_bytes = round_up(num_bytes, fs_info->sectorsize);

#ifdef USRQUOTA_DEBUG
	printk(KERN_INFO "btrfs_usrquota_transfer debug: root = %llu, ino = %lu, "
		"uid_from = %llu, uid_to = %llu, used = %llu, num_bytes = %llu",
		ref_root, inode->i_ino, uid, (u64)__kuid_val(new_uid),
		usrquota_from->uq_rfer_used, num_bytes);
#endif /* USRQUOTA_DEBUG */

	if (enforce && usrquota_to->uq_rfer_hard && !root->invalid_quota) {
		if (usrquota_to->uq_rfer_used + usrquota_to->uq_reserved +
					num_bytes + b_inode->uq_reserved >
					usrquota_to->uq_rfer_hard) {
			ret = -EDQUOT;
			goto out;
		}
	}

	if (btrfs_quota_rescan_check(root, ino)) {
		usrquota_to->uq_rfer_used += num_bytes;
		if (usrquota_from->uq_rfer_used < num_bytes && !root->invalid_quota) {
			WARN_ONCE(1, "user quota chown %llu:%llu ref underflow, "
				"have %llu to free %llu", ref_root, uid,
				usrquota_from->uq_rfer_used, num_bytes);
			usrquota_from->uq_rfer_used = 0;
			usrquota_to->need_rescan = true;
		} else
			usrquota_from->uq_rfer_used -= num_bytes;
	}

	usrquota_to->uq_reserved += b_inode->uq_reserved;
	if (usrquota_from->uq_reserved < b_inode->uq_reserved) {
		WARN_ONCE(1, "user quota chown %llu/%llu reserved underflow, "
			"have %llu to free %llu", ref_root, ino,
			usrquota_from->uq_reserved, b_inode->uq_reserved);
		usrquota_from->uq_reserved = 0;
	} else
		usrquota_from->uq_reserved -= b_inode->uq_reserved;

	usrquota_dirty(fs_info, usrquota_from);
	usrquota_dirty(fs_info, usrquota_to);
	inode->i_uid = new_uid; // Do this inside of usrquota_lock.

out:
	spin_unlock(&fs_info->usrquota_lock);
	up_write(&root->rescan_lock);
	return ret;
}

/*
 * called from commit_transaction. Writes all changed usrquota to disk.
 */
int btrfs_run_usrquota(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *subvol_root, *next;
	int ret = 0;

	if (!fs_info->usrquota_root)
		goto out;

	spin_lock(&fs_info->usrquota_lock);
	while (!list_empty(&fs_info->dirty_usrquota)) {
		struct btrfs_usrquota tmp_usrquota;
		struct btrfs_usrquota *usrquota;

		usrquota = list_first_entry(&fs_info->dirty_usrquota,
		                            struct btrfs_usrquota, uq_dirty);
		list_del_init(&usrquota->uq_dirty);

		// Copy things out since we may free usrquota later.
		memcpy(&tmp_usrquota, usrquota, sizeof(tmp_usrquota));
		usrquota->need_rescan = false;
		usrquota->update_limit = false;

		/*
		 * Remove empty record. To mark tree loaded, 
		 * we need to keep the last record in the tree.
		 */
		if (!usrquota->uq_rfer_hard && !usrquota->uq_rfer_soft
		      && !usrquota->uq_rfer_used && !usrquota->uq_reserved && usrquota->uq_uid) {
			del_usrquota_rb(fs_info, usrquota);
			usrquota = NULL;
			spin_unlock(&fs_info->usrquota_lock);
			ret = remove_usrquota_item(trans, tmp_usrquota.uq_objectid,
							tmp_usrquota.uq_uid, BTRFS_USRQUOTA_INFO_KEY);
		} else {
			spin_unlock(&fs_info->usrquota_lock);
			ret = update_usrquota_info_item(trans, tmp_usrquota.uq_objectid,
			                                tmp_usrquota.uq_uid, tmp_usrquota.uq_rfer_used);
			if ((ret || tmp_usrquota.need_rescan) &&
					test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
				struct syno_quota_rescan_item_updater updater;

				syno_quota_rescan_item_init(&updater);
				updater.flags = SYNO_QUOTA_RESCAN_NEED;
				btrfs_add_update_syno_quota_rescan_item(trans, fs_info->quota_root,
					tmp_usrquota.uq_objectid, &updater);
			}

			if (tmp_usrquota.update_limit) {
				ret = update_usrquota_limit_item(trans, tmp_usrquota.uq_objectid, tmp_usrquota.uq_uid,
							tmp_usrquota.uq_rfer_soft, tmp_usrquota.uq_rfer_hard);
				if (ret)
					fs_info->usrquota_flags |= BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT;
			}
		}
		spin_lock(&fs_info->usrquota_lock);
	}
	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) ||
			test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags))
		fs_info->usrquota_flags |= BTRFS_USRQUOTA_STATUS_FLAG_ON;
	else
		fs_info->usrquota_flags &= ~BTRFS_USRQUOTA_STATUS_FLAG_ON;
	spin_unlock(&fs_info->usrquota_lock);

	ret = update_usrquota_status_item(trans);
	if (ret)
		fs_info->usrquota_flags |= BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT;

	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags)) {
		ret = update_usrquota_compat_item(trans, fs_info, fs_info->usrquota_root);
		if (ret)
			fs_info->usrquota_flags |= BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT;
	}

out:
	mutex_lock(&fs_info->usrquota_ro_roots_lock);
	spin_lock(&fs_info->usrquota_lock);
	list_for_each_entry_safe(subvol_root, next, &fs_info->usrquota_ro_roots, usrquota_ro_root) {
		if (subvol_root->usrquota_loaded_gen + USRQUOTA_RO_SUBVOL_EXIST_GEN < fs_info->generation) {
			list_del_init(&subvol_root->usrquota_ro_root);
			btrfs_put_root(subvol_root);
			btrfs_debug(fs_info, "Unload ro sub [id:%llu] uq subtree [%llu, %llu]",
			          subvol_root->root_key.objectid, subvol_root->usrquota_loaded_gen,
				  fs_info->generation);
			usrquota_subtree_unload_nolock(fs_info, subvol_root->root_key.objectid);
		}
	}
	spin_unlock(&fs_info->usrquota_lock);
	mutex_unlock(&fs_info->usrquota_ro_roots_lock);
	return ret;
}

int btrfs_usrquota_syno_reserve(struct btrfs_inode *b_inode, u64 num_bytes)
{
	if (btrfs_root_disable_quota(b_inode->root))
		return 0;

	num_bytes = round_up(num_bytes, b_inode->root->fs_info->sectorsize);
	return usrquota_reserve(b_inode, num_bytes, true);
}

/*
 * Return 1 if we don't reserve user quota, but it's not an EDQUOT error.
 * Caller is allowed to write.
 */
int usrquota_reserve(struct btrfs_inode *b_inode, u64 num_bytes, bool enforce)
{
	struct btrfs_usrquota *usrquota;
	struct btrfs_root *root = b_inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 ref_root = root->root_key.objectid;
	u64 uid;
	int ret = 0;

	if (!is_fstree(ref_root))
		return 0;

	if (num_bytes == 0)
		return 0;

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags))
		return 1;

	if (test_bit(BTRFS_FS_QUOTA_OVERRIDE, &fs_info->flags) &&
	    capable(CAP_SYS_RESOURCE))
		enforce = false;

	usrquota_ro_subvol_check(fs_info, root);
	spin_lock(&fs_info->usrquota_lock);

	if (!fs_info->usrquota_root) {
		ret = 1;
		goto out;
	}

	// Get uid after usrquota_lock, so that uid won't changed by btrfs_usrquota_transfer().
	uid = __kuid_val(b_inode->vfs_inode.i_uid);
	usrquota = add_usrquota_rb(fs_info, ref_root, uid);
	if (IS_ERR(usrquota)) {
		ret = 1;
		goto out;
	}

#ifdef USRQUOTA_DEBUG
	printk(KERN_INFO "usrquota_reserve debug: root = %llu, ino = %lu, uid = %llu, used = %llu, "
			"reserved = %llu, want = %llu, limit = %llu",
			ref_root, b_inode->vfs_inode.i_ino, uid, usrquota->uq_rfer_used,
			usrquota->uq_reserved, num_bytes, usrquota->uq_rfer_hard);
#endif /* USRQUOTA_DEBUG */

	if (enforce && usrquota->uq_rfer_hard && !root->invalid_quota) {
		if (usrquota->uq_rfer_used + usrquota->uq_reserved + num_bytes > usrquota->uq_rfer_hard) {
			ret = -EDQUOT;
			goto out;
		}
	}
	usrquota->uq_reserved += num_bytes;
	if (test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags) ||
			btrfs_usrquota_fast_chown_enable(&b_inode->vfs_inode))
		b_inode->uq_reserved += num_bytes;

out:
	spin_unlock(&fs_info->usrquota_lock);
	return ret;
}

void btrfs_usrquota_syno_free(struct btrfs_inode *b_inode, u64 num_bytes)
{
	struct btrfs_usrquota *usrquota;
	struct btrfs_root *root = b_inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 ref_root = root->root_key.objectid;
	u64 uid;

	if (!is_fstree(ref_root))
		return;

	if (num_bytes == 0)
		return;

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags))
		return;

	if (btrfs_root_disable_quota(root))
		return;

	spin_lock(&fs_info->usrquota_lock);
	if (!fs_info->usrquota_root)
		goto unlock;

	// Get uid after usrquota_lock, so that uid won't changed by btrfs_usrquota_transfer().
	uid = __kuid_val(b_inode->vfs_inode.i_uid);
	usrquota = find_usrquota_rb(fs_info, ref_root, uid);
	if (!usrquota)
		goto unlock;

	num_bytes = round_up(num_bytes, fs_info->sectorsize);
	usrquota_free_reserve(fs_info, usrquota, b_inode, num_bytes);

unlock:
	spin_unlock(&fs_info->usrquota_lock);
	return;
}

/*
 * Calculate number of usrquota_{info/limit}_item that need to be reserved for space
 * when taking snapshot.
 */
int btrfs_usrquota_calc_reserve_snap(struct btrfs_root *root,
                                     u64 copy_limit_from, u64 *reserve_items)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path = NULL;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_usrquota_root_item *item;
	int ret = 0;

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
		goto unlock_ioctl;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto unlock_ioctl;
	}

	key.objectid = root->root_key.objectid;
	key.type = BTRFS_USRQUOTA_ROOT_KEY;
	key.offset = 0;
	*reserve_items = 0;

//calc_info_items:
	ret = btrfs_search_slot(NULL, fs_info->usrquota_root, &key, path, 0, 0);
	if (ret < 0)
		goto unlock_tree;
	if (ret == 1)
		goto calc_limit_items;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_usrquota_root_item);
	*reserve_items += btrfs_usrquota_root_info_item_cnt(leaf, item);

calc_limit_items:
	if (copy_limit_from == 0)
		goto success;

	btrfs_release_path(path);
	key.objectid = copy_limit_from;
	ret = btrfs_search_slot(NULL, fs_info->usrquota_root, &key, path, 0, 0);
	if (ret < 0)
		goto unlock_tree;
	if (ret == 1)
		goto success;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_usrquota_root_item);
	*reserve_items += btrfs_usrquota_root_limit_item_cnt(leaf, item);

success:
	ret = 0;
unlock_tree:
	btrfs_free_path(path);
unlock_ioctl:
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	return ret;
}

int btrfs_usrquota_mksubvol(struct btrfs_trans_handle *trans, u64 objectid)
{
	int ret = 0;
	struct btrfs_usrquota *usrquota;
	struct btrfs_fs_info *fs_info = trans->fs_info;

	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags))
		return 0;

	// insert dummy node
	spin_lock(&fs_info->usrquota_lock);
	if (!fs_info->usrquota_root)
		goto unlock;

	usrquota = add_usrquota_dummy_rb_nocheck(fs_info, objectid);
	if (IS_ERR(usrquota)) {
		btrfs_err(fs_info, "failed to add_usrquota_rb %ld", PTR_ERR(usrquota));
		ret = PTR_ERR(usrquota);
		goto unlock;
	}
unlock:
	spin_unlock(&fs_info->usrquota_lock);
	return ret;
}

int btrfs_usrquota_mksnap(struct btrfs_trans_handle *trans,
                          u64 srcid, u64 objectid,
                          bool readonly, u64 copy_limit_from)
{
	int ret = 0;
	int src_loaded = 0;
	int copy_loaded = 0;

	struct rb_node *node;
	struct btrfs_usrquota *usrquota_new;
	struct btrfs_usrquota *usrquota_orig;
	struct btrfs_fs_info *fs_info = trans->fs_info;

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
		goto out;
	}
	BUG_ON(!fs_info->usrquota_root);

	// create dummy node
	spin_lock(&fs_info->usrquota_lock);
	usrquota_new = add_usrquota_dummy_rb_nocheck(fs_info, objectid);
	if (IS_ERR(usrquota_new)) {
		btrfs_err(fs_info, "failed to add_usrquota_rb %ld", PTR_ERR(usrquota_new));
		ret = PTR_ERR(usrquota_new);
		goto unlock;
	}
	spin_unlock(&fs_info->usrquota_lock);

	if (!copy_limit_from)
		goto copy_info_items;

	ret = usrquota_subtree_load(fs_info, copy_limit_from);
	if (ret) {
		btrfs_err(fs_info, "failed to load usrquota subtree %llu", copy_limit_from);
		goto out;
	}
	copy_loaded = 1;
	spin_lock(&fs_info->usrquota_lock);
	node = find_usrquota_first_rb(fs_info, copy_limit_from);
	while (node) {
		usrquota_orig = rb_entry(node, struct btrfs_usrquota, uq_node);
		node = rb_next(node);
		if (usrquota_orig->uq_objectid > copy_limit_from)
			break;
		if (!usrquota_orig->uq_rfer_soft && !usrquota_orig->uq_rfer_hard)
			continue;

		usrquota_new = add_usrquota_rb_nocheck(fs_info, objectid, usrquota_orig->uq_uid);
		if (IS_ERR(usrquota_new)) {
			btrfs_err(fs_info, "failed to add_usrquota_rb %ld", PTR_ERR(usrquota_new));
			ret = PTR_ERR(usrquota_new);
			goto unlock;
		}
		usrquota_new->uq_rfer_soft = usrquota_orig->uq_rfer_soft;
		usrquota_new->uq_rfer_hard = usrquota_orig->uq_rfer_hard;
	}
	spin_unlock(&fs_info->usrquota_lock);
	cond_resched();

copy_info_items:
	ret = usrquota_subtree_load(fs_info, srcid);
	if (ret) {
		btrfs_err(fs_info, "failed to load usrquota subtree %llu", srcid);
		goto out;
	}
	src_loaded = 1;
	spin_lock(&fs_info->usrquota_lock);
	node = find_usrquota_first_rb(fs_info, srcid);
	while (node) {
		usrquota_orig = rb_entry(node, struct btrfs_usrquota, uq_node);
		node = rb_next(node);
		if (usrquota_orig->uq_objectid > srcid)
			break;
		usrquota_new = add_usrquota_rb_nocheck(fs_info, objectid, usrquota_orig->uq_uid);
		if (IS_ERR(usrquota_new)) {
			btrfs_err(fs_info, "failed to add_usrquota_rb %ld", PTR_ERR(usrquota_new));
			ret = PTR_ERR(usrquota_new);
			goto unlock;
		}
		usrquota_new->uq_rfer_used = usrquota_orig->uq_rfer_used;
		usrquota_new->uq_generation = usrquota_orig->uq_generation;
	}

	// add info & limit items
	node = find_usrquota_first_rb(fs_info, objectid);
	while (node) {
		usrquota_new = rb_entry(node, struct btrfs_usrquota, uq_node);
		node = rb_next(node);
		if (usrquota_new->uq_objectid > objectid)
			break;
		if (usrquota_new->uq_rfer_soft || usrquota_new->uq_rfer_hard) {
			spin_unlock(&fs_info->usrquota_lock);
			ret = update_usrquota_limit_item(trans, objectid,
			                                 usrquota_new->uq_uid,
			                                 usrquota_new->uq_rfer_soft,
			                                 usrquota_new->uq_rfer_hard);
			spin_lock(&fs_info->usrquota_lock);
			if (ret) {
				fs_info->usrquota_flags |= BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT;
				break;
			}
		}
		if (usrquota_new->uq_rfer_used || usrquota_new->uq_generation) {
			spin_unlock(&fs_info->usrquota_lock);
			ret = update_usrquota_info_item(trans, objectid,
			                                usrquota_new->uq_uid,
			                                usrquota_new->uq_rfer_used);
			spin_lock(&fs_info->usrquota_lock);
			if (ret) {
				fs_info->usrquota_flags |= BTRFS_USRQUOTA_STATUS_FLAG_INCONSISTENT;
				break;
			}
		}
	}
unlock:
	spin_unlock(&fs_info->usrquota_lock);
out:
	if (src_loaded)
		usrquota_subtree_unload(fs_info, srcid);
	if (copy_loaded)
		usrquota_subtree_unload(fs_info, copy_limit_from);
	if (ret || readonly)
		usrquota_subtree_unload(fs_info, objectid);
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	return ret;
}

int btrfs_usrquota_delsnap(struct btrfs_trans_handle *trans, struct btrfs_root *subvol_root)
{
	int ret = 0;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_root *usrquota_root = fs_info->usrquota_root;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	u64 rootid = subvol_root->root_key.objectid;
	int pending_del_nr = 0;
	int pending_del_slot = 0;

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!usrquota_root) {
		ret = -EINVAL;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&fs_info->usrquota_ro_roots_lock);
	if (!list_empty(&subvol_root->usrquota_ro_root)) {
		list_del_init(&subvol_root->usrquota_ro_root);
		btrfs_put_root(subvol_root);
		btrfs_debug(fs_info, "Unload ro sub [id:%llu] uq subtree [%llu, %llu]",
		          subvol_root->root_key.objectid, subvol_root->usrquota_loaded_gen,
			  fs_info->generation);
		usrquota_subtree_unload(fs_info, subvol_root->root_key.objectid);
	}
	mutex_unlock(&fs_info->usrquota_ro_roots_lock);
	usrquota_subtree_unload(fs_info, rootid);

	// copyed from btrfs_truncate_inode_items
	key.objectid = rootid;
	key.offset = (u64) -1;
	key.type = (u8) -1;
search_again:
	path->leave_spinning = 1;
	ret = btrfs_search_slot(trans, usrquota_root, &key, path, -1, 1);
	if (ret < 0) {
		goto out;
	}
	if (ret > 0) {
		ret = 0;
		if (path->slots[0] == 0) {
			btrfs_err(fs_info, "failed to search usrquota");
			goto out;
		}
		path->slots[0]--;
	}
	while (1) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != rootid)
			break;

		if (!pending_del_nr) {
			pending_del_nr = 1;
			pending_del_slot = path->slots[0];
		} else {
			pending_del_nr++;
			pending_del_slot = path->slots[0];
		}

		if (path->slots[0] == 0) {
			if (pending_del_nr) {
				ret = btrfs_del_items(trans, usrquota_root, path,
				                      pending_del_slot,
						      pending_del_nr);
				if (ret) {
					btrfs_abort_transaction(trans, ret);
					goto out;
				}
				pending_del_nr = 0;
			}
			btrfs_release_path(path);
			goto search_again;
		} else {
			path->slots[0]--;
		}
	}
	if (pending_del_nr) {
		ret = btrfs_del_items(trans, usrquota_root, path,
		                      pending_del_slot, pending_del_nr);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
	}
out:
	btrfs_free_path(path);
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	return ret;
}

/*
 * struct btrfs_ioctl_usrquota_query_args should be initialized to zero
 */
int btrfs_usrquota_query(struct btrfs_root *root,
                          struct btrfs_ioctl_usrquota_query_args *uqa)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_usrquota *usrquota;
	kuid_t tmp_uid;
	u64 rootid = root->root_key.objectid;
	u64 kernel_uid;
	int ret;

	if (unlikely(root->invalid_quota))
		return -ESRCH;

	tmp_uid = make_kuid(current_user_ns(), (uid_t)uqa->uid);
	if (!uid_valid(tmp_uid))
		return -EINVAL;
	kernel_uid = __kuid_val(tmp_uid);

	mutex_lock(&fs_info->usrquota_ioctl_lock);
	if (!test_bit(BTRFS_FS_SYNO_USRQUOTA_V1_ENABLED, &fs_info->flags) &&
			!test_bit(BTRFS_FS_SYNO_USRQUOTA_V2_ENABLED, &fs_info->flags)) {
		ret = -ESRCH;
		goto unlock;
	}

	if (usrquota_subtree_load(fs_info, rootid)) {
		ret = -ENOENT;
		goto unlock;
	}

	ret = 0; // It is normal that we have no such entry.
	spin_lock(&fs_info->usrquota_lock);
	usrquota = find_usrquota_rb(fs_info, rootid, kernel_uid);
	if (!usrquota)
		goto unload;

	uqa->rfer_used = usrquota->uq_rfer_used;
	uqa->rfer_soft = usrquota->uq_rfer_soft;
	uqa->rfer_hard = usrquota->uq_rfer_hard;
	uqa->reserved = usrquota->uq_reserved;
unload:
	spin_unlock(&fs_info->usrquota_lock);
	usrquota_subtree_unload(fs_info, rootid);
unlock:
	mutex_unlock(&fs_info->usrquota_ioctl_lock);
	return ret;
}

#ifdef MY_ABC_HERE
static bool check_usrquota_from_disk(struct btrfs_fs_info *fs_info, u64 rootid)
{
	int ret;
	int slot;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *usrquota_root = fs_info->usrquota_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_usrquota_limit_item *limit_item;
	bool has_limit = false;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = rootid;
	key.type = BTRFS_USRQUOTA_LIMIT_KEY;
	key.offset = 0;
	ret = btrfs_search_slot_for_read(usrquota_root, &key, path, 1, 0);
	if (ret < 0)
		goto out;
	else if (ret) {
		ret = 0;
		goto out;
	}
	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.objectid > rootid)
			break;
		else if (found_key.type != BTRFS_USRQUOTA_LIMIT_KEY)
			goto next_item;

		limit_item = btrfs_item_ptr(leaf, slot,
					    struct btrfs_usrquota_limit_item);
		if (btrfs_usrquota_limit_rfer_soft(leaf, limit_item) ||
			btrfs_usrquota_limit_rfer_hard(leaf, limit_item)) {
			has_limit = true;
			break;
		}
next_item:
		ret = btrfs_next_item(usrquota_root, path);
		if (ret < 0)
			goto out;
		else if (ret) {
			ret = 0;
			break;
		}
	}
	ret = 0;
out:
	btrfs_free_path(path);
	// When an error occurr, we always treat it as having quota_limt.
	return (ret) ? true : has_limit;
}

static bool check_usrquota_from_rbtree(struct rb_node *node,
				u64 rootid)
{
	struct btrfs_usrquota *usrquota;
	bool has_limit = false;
	while (node) {
		usrquota = rb_entry(node, struct btrfs_usrquota, uq_node);
		if (usrquota->uq_objectid > rootid)
			break;
		else if (usrquota->uq_rfer_soft || usrquota->uq_rfer_hard) {
			has_limit = true;
			break;
		}
		node = rb_next(node);
	}
	return has_limit;
}

void btrfs_check_usrquota_limit(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	bool has_limit = false;
	struct rb_node *node;
	u64 rootid = root->root_key.objectid;

	spin_lock(&fs_info->usrquota_lock);
	if (!fs_info->usrquota_root) {
		spin_unlock(&fs_info->usrquota_lock);
		return;
	}

	node = find_usrquota_first_rb(fs_info, rootid);
	if (!node) {
		// subtree is unloaded, read from disk.
		spin_unlock(&fs_info->usrquota_lock);
		has_limit = check_usrquota_from_disk(fs_info, rootid);
	} else {
		has_limit = check_usrquota_from_rbtree(node, rootid);
		spin_unlock(&fs_info->usrquota_lock);
	}
	btrfs_root_set_has_usrquota_limit(root, has_limit);
}
#endif /* MY_ABC_HERE */

