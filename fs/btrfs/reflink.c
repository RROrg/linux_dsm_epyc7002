#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0

#include <linux/blkdev.h>
#include <linux/iversion.h>
#ifdef MY_ABC_HERE
#include <linux/fsnotify.h>
#include <linux/security.h>
#endif /* MY_ABC_HERE */
#include "compression.h"
#include "ctree.h"
#include "delalloc-space.h"
#include "reflink.h"
#include "transaction.h"
#ifdef MY_ABC_HERE
#include "qgroup.h"
#endif /* MY_ABC_HERE */

#define BTRFS_MAX_DEDUPE_LEN	SZ_16M
#ifdef MY_ABC_HERE
struct btrfs_syno_clone_range_v2 {
	u64 src_off;
	u64 src_len;
	u64 dest_off;
	u64 dest_len;
	u64 ref_limit;
	u32 flag;
};
#endif /* MY_ABC_HERE */

static int clone_finish_inode_update(struct btrfs_trans_handle *trans,
				     struct inode *inode,
				     u64 endoff,
				     const u64 destoff,
				     const u64 olen,
				     int no_time_update)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	inode_inc_iversion(inode);
	if (!no_time_update)
		inode->i_mtime = inode->i_ctime = current_time(inode);
	/*
	 * We round up to the block size at eof when determining which
	 * extents to clone above, but shouldn't round up the file size.
	 */
	if (endoff > destoff + olen)
		endoff = destoff + olen;
	if (endoff > inode->i_size) {
		i_size_write(inode, endoff);
		btrfs_inode_safe_disk_i_size_write(inode, 0);
	}

	ret = btrfs_update_inode(trans, root, inode);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		goto out;
	}
	ret = btrfs_end_transaction(trans);
out:
	return ret;
}

static int copy_inline_to_page(struct btrfs_inode *inode,
			       const u64 file_offset,
			       char *inline_data,
			       const u64 size,
			       const u64 datal,
			       const u8 comp_type)
{
	const u64 block_size = btrfs_inode_sectorsize(inode);
	const u64 range_end = file_offset + block_size - 1;
	const size_t inline_size = size - btrfs_file_extent_calc_inline_size(0);
	char *data_start = inline_data + btrfs_file_extent_calc_inline_size(0);
	struct extent_changeset *data_reserved = NULL;
	struct page *page = NULL;
	struct address_space *mapping = inode->vfs_inode.i_mapping;
	int ret;

	ASSERT(IS_ALIGNED(file_offset, block_size));

	/*
	 * We have flushed and locked the ranges of the source and destination
	 * inodes, we also have locked the inodes, so we are safe to do a
	 * reservation here. Also we must not do the reservation while holding
	 * a transaction open, otherwise we would deadlock.
	 */
	ret = btrfs_delalloc_reserve_space(inode, &data_reserved, file_offset,
					   block_size);
	if (ret)
		goto out;

	page = find_or_create_page(mapping, file_offset >> PAGE_SHIFT,
				   btrfs_alloc_write_mask(mapping));
	if (!page) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	set_page_extent_mapped(page);
	clear_extent_bit(&inode->io_tree, file_offset, range_end,
			 EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG,
			 0, 0, NULL);
	ret = btrfs_set_extent_delalloc(inode, file_offset, range_end, 0, NULL);
	if (ret)
		goto out_unlock;

	/*
	 * After dirtying the page our caller will need to start a transaction,
	 * and if we are low on metadata free space, that can cause flushing of
	 * delalloc for all inodes in order to get metadata space released.
	 * However we are holding the range locked for the whole duration of
	 * the clone/dedupe operation, so we may deadlock if that happens and no
	 * other task releases enough space. So mark this inode as not being
	 * possible to flush to avoid such deadlock. We will clear that flag
	 * when we finish cloning all extents, since a transaction is started
	 * after finding each extent to clone.
	 */
	set_bit(BTRFS_INODE_NO_DELALLOC_FLUSH, &inode->runtime_flags);

	if (comp_type == BTRFS_COMPRESS_NONE) {
		char *map;

		map = kmap(page);
		memcpy(map, data_start, datal);
		flush_dcache_page(page);
		kunmap(page);
	} else {
		ret = btrfs_decompress(comp_type, data_start, page, 0,
				       inline_size, datal);
		if (ret)
			goto out_unlock;
		flush_dcache_page(page);
	}

	/*
	 * If our inline data is smaller then the block/page size, then the
	 * remaining of the block/page is equivalent to zeroes. We had something
	 * like the following done:
	 *
	 * $ xfs_io -f -c "pwrite -S 0xab 0 500" file
	 * $ sync  # (or fsync)
	 * $ xfs_io -c "falloc 0 4K" file
	 * $ xfs_io -c "pwrite -S 0xcd 4K 4K"
	 *
	 * So what's in the range [500, 4095] corresponds to zeroes.
	 */
	if (datal < block_size) {
		char *map;

		map = kmap(page);
		memset(map + datal, 0, block_size - datal);
		flush_dcache_page(page);
		kunmap(page);
	}

	SetPageUptodate(page);
	ClearPageChecked(page);
	set_page_dirty(page);
out_unlock:
	if (page) {
		unlock_page(page);
		put_page(page);
	}
	if (ret)
		btrfs_delalloc_release_space(inode, data_reserved, file_offset,
					     block_size, true);
	btrfs_delalloc_release_extents(inode, block_size);
out:
	extent_changeset_free(data_reserved);

	return ret;
}

/*
 * Deal with cloning of inline extents. We try to copy the inline extent from
 * the source inode to destination inode when possible. When not possible we
 * copy the inline extent's data into the respective page of the inode.
 */
static int clone_copy_inline_extent(struct inode *dst,
				    struct btrfs_path *path,
				    struct btrfs_key *new_key,
				    const u64 drop_start,
				    const u64 datal,
				    const u64 size,
				    const u8 comp_type,
				    char *inline_data,
				    struct btrfs_trans_handle **trans_out)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dst->i_sb);
	struct btrfs_root *root = BTRFS_I(dst)->root;
	const u64 aligned_end = ALIGN(new_key->offset + datal,
				      fs_info->sectorsize);
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_drop_extents_args drop_args = { 0 };
	int ret;
	struct btrfs_key key;

	if (new_key->offset > 0) {
		ret = copy_inline_to_page(BTRFS_I(dst), new_key->offset,
					  inline_data, size, datal, comp_type);
		goto out;
	}

	key.objectid = btrfs_ino(BTRFS_I(dst));
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		return ret;
	} else if (ret > 0) {
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				return ret;
			else if (ret > 0)
				goto copy_inline_extent;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid == btrfs_ino(BTRFS_I(dst)) &&
		    key.type == BTRFS_EXTENT_DATA_KEY) {
			/*
			 * There's an implicit hole at file offset 0, copy the
			 * inline extent's data to the page.
			 */
			ASSERT(key.offset > 0);
			goto copy_to_page;
		}
	} else if (i_size_read(dst) <= datal) {
		struct btrfs_file_extent_item *ei;

		ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
				    struct btrfs_file_extent_item);
		/*
		 * If it's an inline extent replace it with the source inline
		 * extent, otherwise copy the source inline extent data into
		 * the respective page at the destination inode.
		 */
		if (btrfs_file_extent_type(path->nodes[0], ei) ==
		    BTRFS_FILE_EXTENT_INLINE)
			goto copy_inline_extent;

		goto copy_to_page;
	}

copy_inline_extent:
	/*
	 * We have no extent items, or we have an extent at offset 0 which may
	 * or may not be inlined. All these cases are dealt the same way.
	 */
	if (i_size_read(dst) > datal) {
		/*
		 * At the destination offset 0 we have either a hole, a regular
		 * extent or an inline extent larger then the one we want to
		 * clone. Deal with all these cases by copying the inline extent
		 * data into the respective page at the destination inode.
		 */
		goto copy_to_page;
	}

	/*
	 * Release path before starting a new transaction so we don't hold locks
	 * that would confuse lockdep.
	 */
	btrfs_release_path(path);
	/*
	 * If we end up here it means were copy the inline extent into a leaf
	 * of the destination inode. We know we will drop or adjust at most one
	 * extent item in the destination root.
	 *
	 * 1 unit - adjusting old extent (we may have to split it)
	 * 1 unit - add new extent
	 * 1 unit - inode update
	 */
	trans = btrfs_start_transaction(root, 3);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	drop_args.start = drop_start;
	drop_args.end = aligned_end;
	drop_args.drop_cache = true;
	ret = btrfs_drop_extents(trans, root, BTRFS_I(dst), &drop_args);
	if (ret)
		goto out;
	ret = btrfs_insert_empty_item(trans, root, path, new_key, size);
	if (ret)
		goto out;

	write_extent_buffer(path->nodes[0], inline_data,
			    btrfs_item_ptr_offset(path->nodes[0],
						  path->slots[0]),
			    size);
#ifdef MY_ABC_HERE
	down_read(&root->rescan_lock);
	btrfs_update_inode_bytes(BTRFS_I(dst), datal, drop_args.bytes_found);
	btrfs_qgroup_syno_accounting(BTRFS_I(dst), datal,
					drop_args.bytes_found, UPDATE_QUOTA);
	btrfs_usrquota_syno_accounting(BTRFS_I(dst), datal,
					drop_args.bytes_found, UPDATE_QUOTA);
	up_read(&root->rescan_lock);
#else
	btrfs_update_inode_bytes(BTRFS_I(dst), datal, drop_args.bytes_found);
#endif /* MY_ABC_HERE */
	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &BTRFS_I(dst)->runtime_flags);
	ret = btrfs_inode_set_file_extent_range(BTRFS_I(dst), 0, aligned_end);
out:
	if (!ret && !trans) {
		/*
		 * No transaction here means we copied the inline extent into a
		 * page of the destination inode.
		 *
		 * 1 unit to update inode item
		 */
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
		}
	}
	if (ret && trans) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
	}
	if (!ret)
		*trans_out = trans;

	return ret;

copy_to_page:
	/*
	 * Release our path because we don't need it anymore and also because
	 * copy_inline_to_page() needs to reserve data and metadata, which may
	 * need to flush delalloc when we are low on available space and
	 * therefore cause a deadlock if writeback of an inline extent needs to
	 * write to the same leaf or an ordered extent completion needs to write
	 * to the same leaf.
	 */
	btrfs_release_path(path);

	ret = copy_inline_to_page(BTRFS_I(dst), new_key->offset,
				  inline_data, size, datal, comp_type);
	goto out;
}

#ifdef MY_ABC_HERE
int btrfs_get_extent_refs_count(struct btrfs_fs_info *fs_info, u64 bytenr,
		                      u64 num_bytes, u64 *refs)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_transaction *cur_trans;
	struct btrfs_extent_item *ei;
	struct extent_buffer *extent_leaf;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	/* Check committed refs */
	ret = btrfs_search_slot(NULL, fs_info->extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (!ret) {
		extent_leaf = path->nodes[0];
		ei = btrfs_item_ptr(extent_leaf, path->slots[0], struct btrfs_extent_item);
		*refs += btrfs_extent_refs(extent_leaf, ei);
	}
	ret = 0;
	/* Check delayed refs */
	spin_lock(&fs_info->trans_lock);
	cur_trans = fs_info->running_transaction;
	if (cur_trans)
		refcount_inc(&cur_trans->use_count);
	spin_unlock(&fs_info->trans_lock);
	if (!cur_trans)
		goto out;
	delayed_refs = &cur_trans->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(delayed_refs, bytenr);
	if (!head) {
		spin_unlock(&delayed_refs->lock);
		btrfs_put_transaction(cur_trans);
		goto out;
	}
	*refs += head->ref_mod;
	spin_unlock(&delayed_refs->lock);
	btrfs_put_transaction(cur_trans);
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_clone_auto_rewrite(struct inode *inode, u64 off, u64 len, bool wait)
{
	int ret = 0;
	int ret_pages;
	struct page **pages = NULL;
	unsigned long max_cluster = SZ_256K >> PAGE_SHIFT;
	unsigned long total_cluster = len/PAGE_SIZE;
	unsigned long num_pages;
	unsigned long idx = 0;
	unsigned long start_idx = off >> PAGE_SHIFT;

	pages = kmalloc_array(max_cluster, sizeof(struct page *), GFP_NOFS);
	if (!pages) {
		ret = -ENOMEM;
		goto err;
	}

	while (idx < total_cluster) {
		num_pages = min(total_cluster - idx, max_cluster);
		ret_pages = cluster_pages_for_defrag(inode, pages,
				        start_idx + idx, num_pages);
		if (ret_pages <= 0) {
			if (ret_pages == 0)
				ret_pages = -ENOMEM;
			ret = ret_pages;
			goto err;
		}
		idx += ret_pages;
		balance_dirty_pages_ratelimited(inode->i_mapping);
	}
	if (wait)
		ret = btrfs_wait_ordered_range(inode, off, len);
	else
		filemap_flush(inode->i_mapping);
err:
	kfree(pages);
	return ret;
}
#endif /* MY_ABC_HERE */

/**
 * btrfs_clone() - clone a range from inode file to another
 *
 * @src: Inode to clone from
 * @inode: Inode to clone to
 * @off: Offset within source to start clone from
 * @olen: Original length, passed by user, of range to clone
 * @olen_aligned: Block-aligned value of olen
 * @destoff: Offset within @inode to start clone
 * @no_time_update: Whether to update mtime/ctime on the target inode
 */
static int btrfs_clone(struct inode *src, struct inode *inode,
		       const u64 off, const u64 olen, const u64 olen_aligned,
		       const u64 destoff, int no_time_update
#ifdef MY_ABC_HERE
		       , struct btrfs_syno_clone_range_v2 *v2_args
#endif /* MY_ABC_HERE */
		       )
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_path *path = NULL;
	struct extent_buffer *leaf;
	struct btrfs_trans_handle *trans;
	char *buf = NULL;
	struct btrfs_key key;
	u32 nritems;
	int slot;
	int ret;
	const u64 len = olen_aligned;
	u64 last_dest_end = destoff;
#ifdef MY_ABC_HERE
	int need_rewrite_dst = 0;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	struct ulist *disko_ulist;
	bool set_clone_range; // Shall we set BTRFS_EXTENT_FLAG_HAS_CLONE_RANGE?
	bool check_backref;
#endif /* MY_ABC_HERE */

	ret = -ENOMEM;
	buf = kvmalloc(fs_info->nodesize, GFP_KERNEL);
	if (!buf)
		return ret;

	path = btrfs_alloc_path();
	if (!path) {
		kvfree(buf);
		return ret;
	}

#ifdef MY_ABC_HERE
	disko_ulist = ulist_alloc(GFP_KERNEL);
	if (!disko_ulist) {
		btrfs_free_path(path);
		kvfree(buf);
		return ret;
	}

	set_clone_range = (off != destoff);
	if (off == 0 && destoff == 0 && inode_get_bytes(inode) == 0)
		check_backref = false;
	else
		check_backref = true;

	down_read(&fs_info->inflight_reserve_lock);
	ret = btrfs_qgroup_syno_reserve(BTRFS_I(inode)->root, olen_aligned);
	if (ret < 0)
		goto free_path;

	ret = btrfs_usrquota_syno_reserve(BTRFS_I(inode), olen_aligned);
	if (ret < 0)
		goto free_qgroup;
#endif /* MY_ABC_HERE */

	path->reada = READA_FORWARD;
	/* Clone data */
	key.objectid = btrfs_ino(BTRFS_I(src));
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = off;

	while (1) {
		u64 next_key_min_offset = key.offset + 1;
		struct btrfs_file_extent_item *extent;
		u64 extent_gen;
		int type;
		u32 size;
		struct btrfs_key new_key;
		u64 disko = 0, diskl = 0;
		u64 datao = 0, datal = 0;
#ifdef MY_ABC_HERE
		u64 ram_bytes = 0;
#endif /* MY_ABC_HERE */
		u8 comp;
		u64 drop_start;

		/* Note the key will change type as we walk through the tree */
		path->leave_spinning = 1;
		ret = btrfs_search_slot(NULL, BTRFS_I(src)->root, &key, path,
				0, 0);
		if (ret < 0)
			goto out;
		/*
		 * First search, if no extent item that starts at offset off was
		 * found but the previous item is an extent item, it's possible
		 * it might overlap our target range, therefore process it.
		 */
		if (key.offset == off && ret > 0 && path->slots[0] > 0) {
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0] - 1);
			if (key.type == BTRFS_EXTENT_DATA_KEY)
				path->slots[0]--;
		}

		nritems = btrfs_header_nritems(path->nodes[0]);
process_slot:
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(BTRFS_I(src)->root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			nritems = btrfs_header_nritems(path->nodes[0]);
		}
		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.type > BTRFS_EXTENT_DATA_KEY ||
		    key.objectid != btrfs_ino(BTRFS_I(src)))
			break;

		ASSERT(key.type == BTRFS_EXTENT_DATA_KEY);

		extent = btrfs_item_ptr(leaf, slot,
					struct btrfs_file_extent_item);
		extent_gen = btrfs_file_extent_generation(leaf, extent);
		comp = btrfs_file_extent_compression(leaf, extent);
		type = btrfs_file_extent_type(leaf, extent);
		if (type == BTRFS_FILE_EXTENT_REG ||
		    type == BTRFS_FILE_EXTENT_PREALLOC) {
			disko = btrfs_file_extent_disk_bytenr(leaf, extent);
			diskl = btrfs_file_extent_disk_num_bytes(leaf, extent);
			datao = btrfs_file_extent_offset(leaf, extent);
			datal = btrfs_file_extent_num_bytes(leaf, extent);
#ifdef MY_ABC_HERE
			ram_bytes = btrfs_file_extent_ram_bytes(leaf, extent);
#endif /* MY_ABC_HERE */
		} else if (type == BTRFS_FILE_EXTENT_INLINE) {
			/* Take upper bound, may be compressed */
			datal = btrfs_file_extent_ram_bytes(leaf, extent);
		}

		/*
		 * The first search might have left us at an extent item that
		 * ends before our target range's start, can happen if we have
		 * holes and NO_HOLES feature enabled.
		 */
		if (key.offset + datal <= off) {
			path->slots[0]++;
			goto process_slot;
		} else if (key.offset >= off + len) {
			break;
		}
		next_key_min_offset = key.offset + datal;
		size = btrfs_item_size_nr(leaf, slot);
		read_extent_buffer(leaf, buf, btrfs_item_ptr_offset(leaf, slot),
				   size);

		btrfs_release_path(path);
		path->leave_spinning = 0;

		memcpy(&new_key, &key, sizeof(new_key));
		new_key.objectid = btrfs_ino(BTRFS_I(inode));
		if (off <= key.offset)
			new_key.offset = key.offset + destoff - off;
		else
			new_key.offset = destoff;

#ifdef MY_ABC_HERE
		if (type == BTRFS_FILE_EXTENT_REG && disko != 0 &&
			v2_args && v2_args->ref_limit) {
			u64 refs = 0;
			if (!btrfs_get_extent_refs_count(fs_info, disko, diskl, &refs) &&
			    refs >= v2_args->ref_limit) {
				if (off > key.offset) {
					v2_args->src_off = off;
					v2_args->src_len = datal - (off - key.offset);
				} else {
					v2_args->src_off = key.offset;
					v2_args->src_len = datal;
				}
				v2_args->ref_limit = refs;
				if (v2_args->flag & BTRFS_CLONE_RANGE_V2_AUTO_REWRITE_DST) {
					need_rewrite_dst = 1;
				} else {
					ret = -EMLINK;
					goto out;
				}
			}
		}
#endif /* MY_ABC_HERE */
		/*
		 * Deal with a hole that doesn't have an extent item that
		 * represents it (NO_HOLES feature enabled).
		 * This hole is either in the middle of the cloning range or at
		 * the beginning (fully overlaps it or partially overlaps it).
		 */
		if (new_key.offset != last_dest_end)
			drop_start = last_dest_end;
		else
			drop_start = new_key.offset;

		if (type == BTRFS_FILE_EXTENT_REG ||
		    type == BTRFS_FILE_EXTENT_PREALLOC) {
			struct btrfs_replace_extent_info clone_info;

			/*
			 *    a  | --- range to clone ---|  b
			 * | ------------- extent ------------- |
			 */

			/* Subtract range b */
			if (key.offset + datal > off + len)
				datal = off + len - key.offset;

			/* Subtract range a */
			if (off > key.offset) {
				datao += off - key.offset;
				datal -= off - key.offset;
			}

			clone_info.disk_offset = disko;
			clone_info.disk_len = diskl;
			clone_info.data_offset = datao;
			clone_info.data_len = datal;
			clone_info.file_offset = new_key.offset;
			clone_info.extent_buf = buf;
			clone_info.is_new_extent = false;
#ifdef MY_ABC_HERE
			clone_info.ram_bytes = ram_bytes;
			clone_info.clone_range = set_clone_range;
			clone_info.clone_account_quota = false;
			clone_info.clone_check_backref = check_backref;

			if (test_bit(BTRFS_FS_SYNO_QUOTA_V1_ENABLED, &fs_info->flags)) {
				ret = ulist_add_lru_adjust(disko_ulist, disko, 0, GFP_KERNEL);
				if (ret)
					clone_info.clone_account_quota = true;
				if (ret == -ENOMEM)
					clone_info.clone_check_backref = true;
				if (disko_ulist->nnodes > ULIST_NODES_MAX) {
					clone_info.clone_check_backref = true;
					ulist_remove_first(disko_ulist);
				}
			}
#endif /* MY_ABC_HERE */
			ret = btrfs_replace_file_extents(inode, path, drop_start,
					new_key.offset + datal - 1, &clone_info,
					&trans
#ifdef MY_ABC_HERE
					, NULL
#endif /* MY_ABC_HERE */
					);
			if (ret)
				goto out;
#ifdef MY_ABC_HERE
			btrfs_drop_extent_cache(BTRFS_I(inode), drop_start, new_key.offset + datal - 1, 0);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
			if (need_rewrite_dst)
				v2_args->dest_len = datal;
#endif /* MY_ABC_HERE */
		} else if (type == BTRFS_FILE_EXTENT_INLINE) {
			/*
			 * Inline extents always have to start at file offset 0
			 * and can never be bigger then the sector size. We can
			 * never clone only parts of an inline extent, since all
			 * reflink operations must start at a sector size aligned
			 * offset, and the length must be aligned too or end at
			 * the i_size (which implies the whole inlined data).
			 */
			ASSERT(key.offset == 0);
			ASSERT(datal <= fs_info->sectorsize);
			if (key.offset != 0 || datal > fs_info->sectorsize)
#ifdef MY_ABC_HERE
			{
				up_read(&fs_info->inflight_reserve_lock);
				return -EUCLEAN;
			}
#else
				return -EUCLEAN;
#endif /* MY_ABC_HERE */

			ret = clone_copy_inline_extent(inode, path, &new_key,
						       drop_start, datal, size,
						       comp, buf, &trans);
			if (ret)
				goto out;
		}

		btrfs_release_path(path);

		/*
		 * If this is a new extent update the last_reflink_trans of both
		 * inodes. This is used by fsync to make sure it does not log
		 * multiple checksum items with overlapping ranges. For older
		 * extents we don't need to do it since inode logging skips the
		 * checksums for older extents. Also ignore holes and inline
		 * extents because they don't have checksums in the csum tree.
		 */
		if (extent_gen == trans->transid && disko > 0) {
			BTRFS_I(src)->last_reflink_trans = trans->transid;
			BTRFS_I(inode)->last_reflink_trans = trans->transid;
		}

		last_dest_end = ALIGN(new_key.offset + datal,
				      fs_info->sectorsize);
		ret = clone_finish_inode_update(trans, inode, last_dest_end,
						destoff, olen, no_time_update);
		if (ret)
			goto out;
#ifdef MY_ABC_HERE
		if (need_rewrite_dst) {
			ret = -EMLINK;
			goto out;
		}
#endif /* MY_ABC_HERE */
		if (new_key.offset + datal >= destoff + len)
			break;

		btrfs_release_path(path);
		key.offset = next_key_min_offset;

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}

		cond_resched();
	}
	ret = 0;

	if (last_dest_end < destoff + len) {
		/*
		 * We have an implicit hole that fully or partially overlaps our
		 * cloning range at its end. This means that we either have the
		 * NO_HOLES feature enabled or the implicit hole happened due to
		 * mixing buffered and direct IO writes against this file.
		 */
		btrfs_release_path(path);
		path->leave_spinning = 0;

		/*
		 * When using NO_HOLES and we are cloning a range that covers
		 * only a hole (no extents) into a range beyond the current
		 * i_size, punching a hole in the target range will not create
		 * an extent map defining a hole, because the range starts at or
		 * beyond current i_size. If the file previously had an i_size
		 * greater than the new i_size set by this clone operation, we
		 * need to make sure the next fsync is a full fsync, so that it
		 * detects and logs a hole covering a range from the current
		 * i_size to the new i_size. If the clone range covers extents,
		 * besides a hole, then we know the full sync flag was already
		 * set by previous calls to btrfs_replace_file_extents() that
		 * replaced file extent items.
		 */
		if (last_dest_end >= i_size_read(inode))
			set_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
				&BTRFS_I(inode)->runtime_flags);

		ret = btrfs_replace_file_extents(inode, path, last_dest_end,
				destoff + len - 1, NULL, &trans
#ifdef MY_ABC_HERE
				, NULL
#endif /* MY_ABC_HERE */
				);
		if (ret)
			goto out;

		ret = clone_finish_inode_update(trans, inode, destoff + len,
						destoff, olen, no_time_update);
	}

out:
#ifdef MY_ABC_HERE
	btrfs_usrquota_syno_free(BTRFS_I(inode), olen_aligned);
free_qgroup:
	btrfs_qgroup_syno_free(BTRFS_I(inode)->root, olen_aligned);
free_path:
	up_read(&fs_info->inflight_reserve_lock);
	ulist_free(disko_ulist);
#endif /* MY_ABC_HERE */
	btrfs_free_path(path);
	kvfree(buf);
	clear_bit(BTRFS_INODE_NO_DELALLOC_FLUSH, &BTRFS_I(inode)->runtime_flags);

	return ret;
}

static void btrfs_double_extent_unlock(struct inode *inode1, u64 loff1,
				       struct inode *inode2, u64 loff2, u64 len)
{
	unlock_extent(&BTRFS_I(inode1)->io_tree, loff1, loff1 + len - 1);
	unlock_extent(&BTRFS_I(inode2)->io_tree, loff2, loff2 + len - 1);
}

static void btrfs_double_extent_lock(struct inode *inode1, u64 loff1,
				     struct inode *inode2, u64 loff2, u64 len)
{
	if (inode1 < inode2) {
		swap(inode1, inode2);
		swap(loff1, loff2);
	} else if (inode1 == inode2 && loff2 < loff1) {
		swap(loff1, loff2);
	}
	lock_extent(&BTRFS_I(inode1)->io_tree, loff1, loff1 + len - 1);
	lock_extent(&BTRFS_I(inode2)->io_tree, loff2, loff2 + len - 1);
}

static int btrfs_extent_same_range(struct inode *src, u64 loff, u64 len,
				   struct inode *dst, u64 dst_loff)
{
	const u64 bs = BTRFS_I(src)->root->fs_info->sb->s_blocksize;
	int ret;

	/*
	 * Lock destination range to serialize with concurrent readpages() and
	 * source range to serialize with relocation.
	 */
	btrfs_double_extent_lock(src, loff, dst, dst_loff, len);
	ret = btrfs_clone(src, dst, loff, len, ALIGN(len, bs), dst_loff, 1
#ifdef MY_ABC_HERE
				    , NULL
#endif /* MY_ABC_HERE */
				    );
	btrfs_double_extent_unlock(src, loff, dst, dst_loff, len);

	return ret;
}

static int btrfs_extent_same(struct inode *src, u64 loff, u64 olen,
			     struct inode *dst, u64 dst_loff)
{
	int ret;
	u64 i, tail_len, chunk_count;
	struct btrfs_root *root_dst = BTRFS_I(dst)->root;

	spin_lock(&root_dst->root_item_lock);
	if (root_dst->send_in_progress) {
		btrfs_warn_rl(root_dst->fs_info,
"cannot deduplicate to root %llu while send operations are using it (%d in progress)",
			      root_dst->root_key.objectid,
			      root_dst->send_in_progress);
		spin_unlock(&root_dst->root_item_lock);
		return -EAGAIN;
	}
	root_dst->dedupe_in_progress++;
	spin_unlock(&root_dst->root_item_lock);

	tail_len = olen % BTRFS_MAX_DEDUPE_LEN;
	chunk_count = div_u64(olen, BTRFS_MAX_DEDUPE_LEN);

	for (i = 0; i < chunk_count; i++) {
		ret = btrfs_extent_same_range(src, loff, BTRFS_MAX_DEDUPE_LEN,
					      dst, dst_loff);
		if (ret)
			goto out;

		loff += BTRFS_MAX_DEDUPE_LEN;
		dst_loff += BTRFS_MAX_DEDUPE_LEN;
	}

	if (tail_len > 0)
		ret = btrfs_extent_same_range(src, loff, tail_len, dst, dst_loff);
out:
	spin_lock(&root_dst->root_item_lock);
	root_dst->dedupe_in_progress--;
	spin_unlock(&root_dst->root_item_lock);

	return ret;
}
#ifdef MY_ABC_HERE
int get_extent_item_list(struct inode *inode, u64 offset, u64 len,
				struct ulist *extent_item_list)
{
	int ret = 0;
	u64 end = offset + len;
	u64 bytenr, num_bytes;
	struct btrfs_key key;
	u64 ino = btrfs_ino(BTRFS_I(inode));
	struct btrfs_path *path = NULL;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_buffer *leaf = NULL;
	struct btrfs_file_extent_item *fi = NULL;

	ulist_reinit(extent_item_list);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_lookup_file_extent_by_file_offset(NULL, root, path, ino,
						      offset, 0);
	if (ret < 0)
		goto out;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

	while (key.offset < end) {
		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);

		bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
		num_bytes = btrfs_file_extent_disk_num_bytes(leaf, fi);
		if (bytenr && num_bytes) {
			ret = ulist_add(extent_item_list, bytenr, num_bytes,
					GFP_NOFS);
			if (ret < 0)
				break;
			if (extent_item_list->nnodes > ULIST_NODES_MAX) {
				btrfs_warn_rl(BTRFS_I(inode)->root->fs_info,
					      "Add too much node, bad release size in syno_extent_same");
				break;
			}
		}

		ret = btrfs_search_next_file_extent(&key, root, path);
		if (ret)
			break;
		leaf = path->nodes[0];
	}
	ret = 0;

out:
	if (ret == -ENOENT)
		ret = 0;
	btrfs_free_path(path);
	return ret;
}

int delayed_backref_count(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	int count = 0;
	struct btrfs_transaction *trans = NULL;
	struct btrfs_delayed_ref_head *head = NULL; // the header of delayed ref for the extent item
	struct btrfs_delayed_ref_root *delayed_refs = NULL; // delayed ref in this trans

	/* Check trans */
	spin_lock(&fs_info->trans_lock);
	trans = fs_info->running_transaction;
	if (trans)
		refcount_inc(&trans->use_count);
	spin_unlock(&fs_info->trans_lock);
	if (!trans)
		return 0;

	delayed_refs = &trans->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(delayed_refs, bytenr);
	if (!head) {
		spin_unlock(&delayed_refs->lock);
		goto out;
	}
	count = head->ref_mod;
	spin_unlock(&delayed_refs->lock);
out:
	btrfs_put_transaction(trans);
	return count;
}

int extent_same_release_size_accounting(struct ulist *dst_extent_item,
					       struct btrfs_root *root,
					       u64 *release_size)
{
	int ret = 0;
	int refcount = 0;
	struct ulist_iterator uiter;
	struct ulist_node *node = NULL;
	struct btrfs_key key;
	struct btrfs_path *path = NULL;
	struct btrfs_extent_item *ei = NULL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ULIST_ITER_INIT(&uiter);
	key.type = BTRFS_EXTENT_ITEM_KEY;

	while ((node = ulist_next(dst_extent_item, &uiter))) {
		key.objectid = node->val;
		key.offset = node->aux;

		refcount = delayed_backref_count(root->fs_info, node->val);
		ret = btrfs_search_slot(NULL, root->fs_info->extent_root, &key,
					path, 0, 0);
		if (!ret) {
			ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
					    struct btrfs_extent_item);
			refcount += btrfs_extent_refs(path->nodes[0], ei);
		}
		if (refcount <= 0)
			*release_size += node->aux;

		btrfs_release_path(path);
	}

	btrfs_free_path(path);

	return 0;
}
#endif /* MY_ABC_HERE */
#ifdef MY_DEF_HERE
/* ported from 4.4.x */
static struct page *extent_same_get_page(struct inode *inode, pgoff_t index)
{
	struct page *page;

	page = grab_cache_page(inode->i_mapping, index);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (!PageUptodate(page)) {
		int ret;

		ret = btrfs_readpage(NULL, page);
		if (ret)
			return ERR_PTR(ret);
		lock_page(page);
		if (!PageUptodate(page)) {
			unlock_page(page);
			put_page(page);
			return ERR_PTR(-EIO);
		}
		if (page->mapping != inode->i_mapping) {
			unlock_page(page);
			put_page(page);
			return ERR_PTR(-EAGAIN);
		}
	}

	return page;
}

/* ported from 4.4.x */
static int gather_extent_pages(struct inode *inode, struct page **pages,
			       int num_pages, u64 off)
{
	int i;
	pgoff_t index = off >> PAGE_SHIFT;

	for (i = 0; i < num_pages; i++) {
again:
		pages[i] = extent_same_get_page(inode, index + i);
		if (IS_ERR(pages[i])) {
			int err = PTR_ERR(pages[i]);

			if (err == -EAGAIN)
				goto again;
			pages[i] = NULL;
			return err;
		}
	}
	return 0;
}

static int check_ordered_extent(struct inode *inode, u64 off, u64 len)
{
	struct btrfs_ordered_extent *ordered;
	int ret = -EAGAIN;

	ordered = btrfs_lookup_first_ordered_extent(BTRFS_I(inode), off + len - 1);
	if ((!ordered ||
	     ordered->file_offset + ordered->num_bytes <= off ||
	     ordered->file_offset >= off + len) &&
	    !test_range_bit(&BTRFS_I(inode)->io_tree, off,
			    off + len - 1, EXTENT_DELALLOC, 0, NULL)) {
		ret = 0;
		goto end;
	}

	ret = -EAGAIN;

end:
	if (ordered)
		btrfs_put_ordered_extent(ordered);
	return ret;
}

/* ported from 4.4.x */
struct cmp_pages {
	int		num_pages;
	struct page	**src_pages;
	struct page	**dst_pages;
};

/* ported from 4.4.x */
static void btrfs_cmp_data_free(struct cmp_pages *cmp)
{
	int i;
	struct page *pg;

	for (i = 0; i < cmp->num_pages; i++) {
		pg = cmp->src_pages[i];
		if (pg) {
			unlock_page(pg);
			put_page(pg);
		}
		pg = cmp->dst_pages[i];
		if (pg) {
			unlock_page(pg);
			put_page(pg);
		}
	}
	kfree(cmp->src_pages);
	kfree(cmp->dst_pages);
}

/* ported from 4.4.x */
static int btrfs_cmp_data_prepare(struct inode *src, u64 loff,
				  struct inode *dst, u64 dst_loff,
				  u64 len, struct cmp_pages *cmp)
{
	int ret;
	int num_pages = PAGE_ALIGN(len) >> PAGE_SHIFT;
	struct page **src_pgarr, **dst_pgarr;

	/*
	 * We must gather up all the pages before we initiate our
	 * extent locking. We use an array for the page pointers. Size
	 * of the array is bounded by len, which is in turn bounded by
	 * BTRFS_MAX_DEDUPE_LEN.
	 */
	src_pgarr = kcalloc(num_pages, sizeof(struct page *), GFP_KERNEL);
	dst_pgarr = kcalloc(num_pages, sizeof(struct page *), GFP_KERNEL);
	if (!src_pgarr || !dst_pgarr) {
		kfree(src_pgarr);
		kfree(dst_pgarr);
		return -ENOMEM;
	}
	cmp->num_pages = num_pages;
	cmp->src_pages = src_pgarr;
	cmp->dst_pages = dst_pgarr;

	/*
	 * If deduping ranges in the same inode, locking rules make it mandatory
	 * to always lock pages in ascending order to avoid deadlocks with
	 * concurrent tasks (such as starting writeback/delalloc).
	 */
	if (src == dst && dst_loff < loff) {
		swap(src_pgarr, dst_pgarr);
		swap(loff, dst_loff);
	}

	ret = gather_extent_pages(src, src_pgarr, cmp->num_pages, loff);
	if (ret)
		goto out;

	ret = gather_extent_pages(dst, dst_pgarr, cmp->num_pages, dst_loff);

out:
	if (ret)
		btrfs_cmp_data_free(cmp);
	return ret;
}

static inline void *btrfs_cmp_data_kmap_page(struct page *page)
{
	void *addr;

	ASSERT(PageLocked(page));
	addr = kmap_atomic(page);
	flush_dcache_page(page);

	return addr;
}

/* copy from 4.4.x btrfs_cmp_data */
static bool btrfs_cmp_data_and_truncate_len(struct cmp_pages *cmp, u64 total_len,
					    u64 *same_len, u64 *diff_len)
{
	int i;
	bool diff_start = false;
	unsigned int cmp_len = PAGE_SIZE;
	void *src_addr, *dst_addr;

	*same_len = *diff_len = 0;
	for (i = 0;i < cmp->num_pages;i++) {
		if (total_len < PAGE_SIZE)
			cmp_len = total_len;

		src_addr = btrfs_cmp_data_kmap_page(cmp->src_pages[i]);
		dst_addr = btrfs_cmp_data_kmap_page(cmp->dst_pages[i]);

		if (!memcmp(src_addr, dst_addr, cmp_len)) {
			if (diff_start) {
				/* we got the end of different data */
				kunmap_atomic(src_addr);
				kunmap_atomic(dst_addr);
				break;
			}
			*same_len += cmp_len;
		} else {
			diff_start = true;
			*diff_len += cmp_len;
		}
		kunmap_atomic(src_addr);
		kunmap_atomic(dst_addr);
		total_len -= cmp_len;
		if (!total_len)
			break;
	}

	return diff_start;
}

/* ported from 4.4.x */
static int extent_same_check_offsets(struct inode *inode, u64 off, u64 *plen,
				     u64 olen)
{
	u64 len = *plen;
	u64 bs = BTRFS_I(inode)->root->fs_info->sb->s_blocksize;

	if (off + olen > inode->i_size || off + olen < off)
		return -EINVAL;

	/* if we extend to eof, continue to block boundary */
	if (off + len == inode->i_size)
		*plen = len = ALIGN(inode->i_size, bs) - off;

	/* Check that we are block aligned - btrfs_clone() requires this */
	if (!IS_ALIGNED(off, bs) || !IS_ALIGNED(off + len, bs))
		return -EINVAL;

	return 0;
}

static int syno_extent_same_check_offset(struct inode *src, u64 loff,
					 struct inode *dst, u64 dst_loff,
					 u64 *len)
{
	int ret = -1;
	u64 olen = *len;

	ret = extent_same_check_offsets(src, loff, len, olen);
	if (ret)
		goto out;
	ret = extent_same_check_offsets(dst, dst_loff, len, olen);
	if (ret)
		goto out;
	if (src == dst) {
		/* extent_same_check_offsets may extend len over i_size(align bs).
		 * it is no sense to do it in the same inode.
		 */
		if (*len != olen) {
			ret = -EINVAL;
			goto out;
		}
		/* Check for overlapping ranges */
		if (dst_loff + *len > loff && dst_loff < loff + *len) {
			ret = -EINVAL;
			goto out;
		}
	}
	ret = 0;
out:
	return ret;
}

static void btrfs_extent_same_ra(struct inode *inode, u64 off, u64 len)
{
	int i = 0;
	struct page *page = NULL;
	int num_pages = PAGE_ALIGN(len) >> PAGE_SHIFT;
	pgoff_t index = off >> PAGE_SHIFT;
	struct file_ra_state *ra = kzalloc(sizeof(struct file_ra_state), GFP_NOFS);

	if (!ra) {
		/* it will read pages later, ignore */
		btrfs_warn_rl(BTRFS_I(inode)->root->fs_info,
			      "btrfs_extent_same_ra kmalloc file_ra_state failed");
		goto out;
	}

	file_ra_state_init(ra, inode->i_mapping);
	ra->ra_pages = num_pages;
	page_cache_sync_readahead(inode->i_mapping, ra, NULL,
				  off >> PAGE_SHIFT, num_pages);

	for (i = 0; i < num_pages; i++) {
		page = grab_cache_page(inode->i_mapping, index + i);
		if (page) {
			unlock_page(page);
			put_page(page);
		}
	}

out:
	kfree(ra);

	return;
}

static int get_extent_ref_remain(struct btrfs_root *root, struct btrfs_path *path,
				u64 objectid, int *ref_remain)
{
	int ret = 0;
	struct btrfs_key key;

	while (*ref_remain > 0) {
		/* skip extent item, get first backref */
		path->slots[0]++;
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(root, path);
			if (ret)
				break;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		if (key.objectid != objectid ||
			(BTRFS_EXTENT_DATA_REF_KEY != key.type &&
			BTRFS_SHARED_DATA_REF_KEY != key.type))
			break;

		(*ref_remain)--;
	}

	return ret;
}

static int inline_backref_count(struct extent_buffer *eb, int slot)
{
	int type = 0;
	int count = 0;
	u32 item_size = 0;
	unsigned long ptr = 0, end = 0;
	struct btrfs_extent_inline_ref *iref = NULL;
	struct btrfs_extent_item *ei = NULL;

	ei = btrfs_item_ptr(eb, slot, struct btrfs_extent_item);
	item_size = btrfs_item_size_nr(eb, slot);
	if (item_size < sizeof(*ei))
		return 0;
	ptr = (unsigned long)(struct btrfs_extent_inline_ref *)(ei + 1);
	end = (unsigned long)ei + item_size;
	while (ptr < end) {
		iref = (struct btrfs_extent_inline_ref *)ptr;
		type = btrfs_extent_inline_ref_type(eb, iref);
		if (BTRFS_EXTENT_DATA_REF_KEY == type || BTRFS_SHARED_DATA_REF_KEY == type)
			count++;
		ptr += btrfs_extent_inline_ref_size(type);
	}
	return count;
}

static int get_backref_remain(struct btrfs_fs_info *fs_info, u64 bytenr,
			      u64 num_bytes, int *ref_remain)
{
	int ret = 1;
	struct btrfs_key key;
	struct btrfs_path *path = NULL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	ret = btrfs_search_slot(NULL, fs_info->extent_root, &key, path, 0, 0);
	if (ret) {
		if (ret > 0)
			ret = 0; // not found.
		goto out;
	}
	*ref_remain -= inline_backref_count(path->nodes[0], path->slots[0]);
	*ref_remain -= delayed_backref_count(fs_info, bytenr);
	/* we should check ref_remain after delayed ref, because it may drop backref in trans */
	ret = get_extent_ref_remain(fs_info->extent_root, path, bytenr,
				    ref_remain);
	if (ret < 0)
		goto out;

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int get_backref_remain_list(struct ulist *backref_remain_list,
				   struct btrfs_fs_info *fs_info,
				   struct inode *src, u64 start, u64 len,
				   int ref_limit)
{
	int ret = 0;
	int ref_remain = 0;
	struct ulist_iterator uiter;
	struct ulist_node *node = NULL;
	struct ulist *extent_item_list = NULL;

	extent_item_list = ulist_alloc(GFP_NOFS);
	if (!extent_item_list) {
		ret = -ENOMEM;
		goto out;
	}

	ret = get_extent_item_list(src, start, len, extent_item_list);
	if (ret < 0)
		goto out;

	ULIST_ITER_INIT(&uiter);

	while ((node = ulist_next(extent_item_list, &uiter))) {
		ref_remain = ref_limit;

		ret = get_backref_remain(fs_info, node->val, node->aux,
					 &ref_remain);
		if (ret < 0)
			break;
		ret = ulist_add(backref_remain_list, node->val,
				(0 < ref_remain)? (u64)ref_remain:0, GFP_NOFS);
		if (ret < 0)
			break;
	}

out:
	ulist_free(extent_item_list);
	return ret;
}

static inline int update_ditto_info(struct btrfs_path *path,
				    struct ulist *backref_remain_list,
				    u64 start, u64 end, u64 file_extent_start,
				    u64 *ditto_offset, u64 *ditto_len)
{
	u64 num_bytes = 0, ditto_end = 0;
	struct ulist_node *node = NULL;
	struct extent_buffer *leaf = path->nodes[0];
	struct btrfs_file_extent_item *fi = NULL;

	fi = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_file_extent_item);

	node = ulist_search(backref_remain_list, btrfs_file_extent_disk_bytenr(leaf, fi));
	if (!node)
		return 0;
	if (node->aux > 0) {
		node->aux--;
		return 0;
	}
	num_bytes = btrfs_file_extent_num_bytes(leaf, fi);

	*ditto_offset = max(start, file_extent_start); // file_extent_start may smaller than request

	ditto_end = min(file_extent_start + num_bytes, end);
	*ditto_len = ditto_end - *ditto_offset;

	return 1;
}

static int check_backref_limit(struct inode *inode, u64 start, u64 len,
			       int ref_limit, u64 *ditto_offset, u64 *ditto_len)
{
	int ret = 0;
	u64 ino = btrfs_ino(BTRFS_I(inode));
	u64 end = start + len;
	struct ulist *backref_remain_list = NULL;
	struct btrfs_key key;
	struct btrfs_path *path = NULL;
	struct btrfs_root *root = BTRFS_I(inode)->root;

	backref_remain_list = ulist_alloc(GFP_NOFS);
	if (!backref_remain_list) {
		ret = -ENOMEM;
		goto out;
	}

	ret = get_backref_remain_list(backref_remain_list, root->fs_info, inode,
				      start, len, ref_limit);
	if (ret < 0)
		goto out;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	ret = btrfs_lookup_file_extent_by_file_offset(NULL, root, path, ino,
						      start, 0);
	if (ret < 0)
		goto out;
	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

	while (key.offset < end) {

		ret = update_ditto_info(path, backref_remain_list, start, end,
					key.offset, ditto_offset, ditto_len);
		if (ret)
			break;

		ret = btrfs_search_next_file_extent(&key, root, path);
		if (ret)
			break;
	}

out:
	if (ret == -ENOENT)
		ret = 0;
	btrfs_free_path(path);
	ulist_free(backref_remain_list);
	return ret;
}

/* copy from btrfs_extent_same() */
static int __syno_extent_same(struct inode *src, u64 src_off, u64 olen,
			      struct inode *dst, u64 dst_off, u64 min_len,
			      u64 *diff_offset, u64 *diff_len)
{
	int ret = 0;
	u64 len = olen;
	u64 lock_len = 0;
	struct cmp_pages cmp;

	/* try to readahead pages before we get inode lock */
	btrfs_extent_same_ra(src, src_off, olen);
	btrfs_extent_same_ra(dst, dst_off, olen);

again:
	lock_two_nondirectories(src, dst);
	ret = syno_extent_same_check_offset(src, src_off, dst, dst_off, &len);
	if (ret)
		goto out_unlock;

	ret = btrfs_cmp_data_prepare(src, src_off, dst, dst_off, olen, &cmp);
	if (ret)
		goto out_unlock;

	lock_len = len;
	btrfs_double_extent_lock(src, src_off, dst, dst_off, lock_len);
	if (-EAGAIN == check_ordered_extent(src, src_off, lock_len) ||
	    -EAGAIN == check_ordered_extent(dst, dst_off, lock_len)) {
		btrfs_double_extent_unlock(src, src_off, dst, dst_off, lock_len);
		btrfs_cmp_data_free(&cmp);
		unlock_two_nondirectories(src, dst);
		btrfs_wait_ordered_range(src, src_off, len);
		btrfs_wait_ordered_range(dst, dst_off, len);
		goto again;
	}

	/* if we got different data, truncate clone length */
	if (btrfs_cmp_data_and_truncate_len(&cmp, lock_len, &len, diff_len))
		*diff_offset = dst_off + len;

	if (len && len >= min_len)
#ifdef MY_ABC_HERE
		ret = btrfs_clone(src, dst, src_off, olen, len, dst_off, 1, NULL);
#else
		ret = btrfs_clone(src, dst, src_off, olen, len, dst_off, 1);
#endif /* MY_ABC_HERE */

	btrfs_double_extent_unlock(src, src_off, dst, dst_off, lock_len);

	btrfs_cmp_data_free(&cmp);

out_unlock:
	unlock_two_nondirectories(src, dst);

	return ret;
}

static int syno_extent_same(struct inode *src, struct inode *dst,
			    struct btrfs_ioctl_syno_extent_same_args *same,
			    u64 ditto_offset, u64 ditto_len)
{
	int ret = 0;
	u32 tail_len = 0;
	u64 chunk_count = 0, i = 0;
	u64 diff_offset = 0, diff_len = 0;
	u64 min_len = same->min_dedupe_length;
	u64 len = ditto_len ? (ditto_offset - same->src_offset) : same->length;
	u64 src_offset = same->src_offset, dst_offset = same->dst_offset;

	if (len < min_len)
		goto out;

	chunk_count = div_u64_rem(len, BTRFS_MAX_DEDUPE_LEN, &tail_len);
	for (i = 0; i < chunk_count; i++) {
		ret = __syno_extent_same(src, src_offset, BTRFS_MAX_DEDUPE_LEN,
					 dst, dst_offset, min_len,
					 &diff_offset, &diff_len);
		if (ret || diff_len)
			goto out;
		src_offset += BTRFS_MAX_DEDUPE_LEN;
		dst_offset += BTRFS_MAX_DEDUPE_LEN;
		min_len = 0;
	}
	if (tail_len)
		ret = __syno_extent_same(src, src_offset, tail_len,
					 dst, dst_offset, min_len,
					 &diff_offset, &diff_len);

out:
	if (diff_len) {
		same->status = SYNO_EXTENT_SAME_DIFF;
		same->failed_dst_offset = diff_offset;
		same->failed_dst_length = diff_len;
	} else if (ditto_len) {
		same->status = SYNO_EXTENT_SAME_DITTO;
		same->failed_dst_offset = same->dst_offset + ditto_offset - same->src_offset;
		same->failed_dst_length = ditto_len;
	}

	return ret;
}

static bool dedupe_in_progress_add(struct btrfs_root *root)
{
	int ret = true;

	spin_lock(&root->root_item_lock);
	if (root->send_in_progress) {
		btrfs_warn_rl(root->fs_info,
			      "cannot deduplicate to root %llu while send operations are using it (%d in progress)",
			      root->root_key.objectid, root->send_in_progress);
		ret = false;
		goto out;
	}
	root->dedupe_in_progress++;
out:
	spin_unlock(&root->root_item_lock);
	return ret;
}

static void dedupe_in_progress_dec(struct btrfs_root *root)
{
	spin_lock(&root->root_item_lock);
	root->dedupe_in_progress--;
	spin_unlock(&root->root_item_lock);
}

static int btrfs_syno_extent_same(struct inode *src, struct inode *dst,
				struct btrfs_ioctl_syno_extent_same_args *same)
{
	int ret = -1;
	u64 ditto_offset = 0, ditto_len = 0;
	struct btrfs_root *root_dst = BTRFS_I(dst)->root;
	struct ulist *extent_item_list = NULL;

	/* don't touch certain kinds of inodes */
	if (IS_IMMUTABLE(dst))
		return -EPERM;

	/* swap file can't do extent_same */
	if (IS_SWAPFILE(src) || IS_SWAPFILE(dst))
		return -ETXTBSY;

	/* ignore "no dedupe" files */
	if (BTRFS_I(src)->flags & BTRFS_INODE_NODEDUPE ||
	    BTRFS_I(dst)->flags & BTRFS_INODE_NODEDUPE)
		return -EINVAL;

	/* don't make the dst file partial checksummed */
	if ((BTRFS_I(src)->flags & BTRFS_INODE_NODATASUM) !=
	    (BTRFS_I(dst)->flags & BTRFS_INODE_NODATASUM))
		return -EINVAL;

	/* don't touch readonly root */
	if (btrfs_root_readonly(BTRFS_I(dst)->root))
		return -EROFS;

	/* don't modify file extent if it is doing send */
	if (!dedupe_in_progress_add(root_dst))
		return -EAGAIN;

	/* flush all data before extent_same */
	btrfs_wait_ordered_range(src, same->src_offset, same->length);
	btrfs_wait_ordered_range(dst, same->dst_offset, same->length);

	/* backref should be limited, we check it before extent_same */
	ret = check_backref_limit(src, same->src_offset, same->length,
				  same->backref_limit, &ditto_offset, &ditto_len);
	if (ret < 0)
		goto out;

	extent_item_list = ulist_alloc(GFP_NOFS);
	if (!extent_item_list) {
		ret = -ENOMEM;
		goto out;
	}

	/* collect dst extent item list before we do extent_same */
	ret = get_extent_item_list(dst, same->dst_offset,
				   same->length, extent_item_list);
	if (ret < 0)
		goto out;

	ret = syno_extent_same(src, dst, same, ditto_offset, ditto_len);
	if (ret < 0)
		goto out;

	/* count how much extent item released */
	ret = extent_same_release_size_accounting(extent_item_list,
			BTRFS_I(dst)->root, &same->release_size);
	if (ret < 0)
		goto out;

	ret = btrfs_file_extent_deduped_set_range(dst, same->dst_offset, same->length, true);

out:
	ulist_free(extent_item_list);
	dedupe_in_progress_dec(root_dst);

	return ret;
}

long btrfs_ioctl_syno_extent_same(struct file *file,
		struct btrfs_ioctl_syno_extent_same_args __user *argp)
{
	int ret = -1;
	struct inode *src = NULL;
	struct inode *dst = NULL;
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ioctl_syno_extent_same_args *same = NULL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (WARN_ON_ONCE(fs_info->sb->s_blocksize < PAGE_SIZE))
		return -EINVAL;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	same = memdup_user(argp, sizeof(struct btrfs_ioctl_syno_extent_same_args));
	if (IS_ERR(same)) {
		ret = PTR_ERR(same);
		same = NULL;
		goto out;
	}

	/* set return value to 0 */
	same->failed_dst_offset = 0;
	same->failed_dst_length = 0;
	same->release_size = 0;
	same->status = 0;

	src = btrfs_get_regular_file_inode(fs_info->sb, same->src_rootid,
					   same->src_objectid);
	if (IS_ERR(src)) {
		ret = PTR_ERR(src);
		if (-ESTALE == ret) {
			same->status = SYNO_EXTENT_SAME_SRC_NOT_FOUND;
			ret = 0;
		}
		goto out;
	}

	dst = btrfs_get_regular_file_inode(fs_info->sb, same->dst_rootid,
					   same->dst_objectid);
	if (IS_ERR(dst)) {
		ret = PTR_ERR(dst);
		if (-ESTALE == ret) {
			same->status = SYNO_EXTENT_SAME_DST_NOT_FOUND;
			ret = 0;
		}
		goto out;
	}

	ret = btrfs_syno_extent_same(src, dst, same);

out:
	if (!ret) {
		ret = copy_to_user(argp, same,
				   sizeof(struct btrfs_ioctl_syno_extent_same_args));
		if (ret)
			ret = -EFAULT;
		else if (same->status)
			ret = -EMLINK; // this errno should be handled in user space
	}
	if (src && !IS_ERR(src))
		iput(src);
	if (dst && !IS_ERR(dst))
		iput(dst);
	mnt_drop_write_file(file);
	kfree(same);

	return ret;
}

void btrfs_mark_buffer_dirty(struct extent_buffer *buf);
/* modify from and insert_reserved_file_extent() */
int insert_dedupe_file_extent(struct btrfs_trans_handle *trans, struct inode *inode,
			     u64 offset, u64 len, u64 disk_bytenr, u64 disk_num_bytes, u64 disk_offset)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_drop_extents_args drop_args = { 0 };
	int ret;
	u64 bytes_to_add = 0;
	bool release_trans = false;
	int syno_usage;
	struct btrfs_ref ref = { 0 };
	struct btrfs_file_extent_item *fi;
	struct btrfs_path *path = NULL;
	struct extent_buffer *leaf;
	struct btrfs_key ins;

	/*
	 * Still need to make sure the inode looks like it's been updated so
	 * that any holes get logged if we fsync.
	 */
	if (disk_bytenr == 0 && btrfs_fs_incompat(root->fs_info, NO_HOLES)) {
		BTRFS_I(inode)->last_trans = root->fs_info->generation;
		BTRFS_I(inode)->last_sub_trans = root->log_transid;
		BTRFS_I(inode)->last_log_commit = root->last_log_commit;
		return 0;
	}

	if (!trans) {
		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}
		release_trans = true;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * we may be replacing one extent in the tree with another.
	 * The new extent is pinned in the extent map, and we don't want
	 * to drop it from the cache until it is completely in the btree.
	 *
	 * So, tell btrfs_drop_extents to leave this extent in the cache.
	 * the caller is expected to unpin it and allow it to be merged
	 * with the others.
	 */
	drop_args.path = path;
	drop_args.start = offset;
	drop_args.end = offset + len;
	drop_args.replace_extent = true;
	drop_args.extent_item_size = sizeof(*fi);
	ret = btrfs_drop_extents(trans, root, BTRFS_I(inode), &drop_args);
	if (ret)
		goto out;

	if (!drop_args.extent_inserted) {
		ins.objectid = btrfs_ino(BTRFS_I(inode));
		ins.offset = offset;
		ins.type = BTRFS_EXTENT_DATA_KEY;

		path->leave_spinning = 1;
		ret = btrfs_insert_empty_item(trans, root, path, &ins,
					      sizeof(*fi));
		if (ret)
			goto out;
	}
	leaf = path->nodes[0];
	fi = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_file_extent_item);
	//TODO:fix compression
	btrfs_set_file_extent_generation(leaf, fi, trans->transid);
	btrfs_set_file_extent_type(leaf, fi, BTRFS_FILE_EXTENT_REG);
	btrfs_set_file_extent_disk_bytenr(leaf, fi, disk_bytenr);
	btrfs_set_file_extent_disk_num_bytes(leaf, fi, disk_num_bytes);
	btrfs_set_file_extent_offset(leaf, fi, disk_offset);
	btrfs_set_file_extent_num_bytes(leaf, fi, len);
	btrfs_set_file_extent_ram_bytes(leaf, fi, len);
	btrfs_set_file_extent_compression(leaf, fi, 0);
	btrfs_set_file_extent_encryption(leaf, fi, 0);
	btrfs_set_file_extent_other_encoding(leaf, fi, 0);
	btrfs_set_file_extent_syno_flag(leaf, fi, BTRFS_FILE_EXTENT_DEDUPED);

	btrfs_mark_buffer_dirty(leaf);

	syno_usage = btrfs_syno_usage_ref_check(root, btrfs_ino(BTRFS_I(inode)), offset);

	btrfs_release_path(path);

	if (disk_bytenr)
		bytes_to_add = len;

	down_read(&root->rescan_lock);
	btrfs_update_inode_bytes(BTRFS_I(inode), bytes_to_add, drop_args.bytes_found);
	btrfs_qgroup_syno_accounting(BTRFS_I(inode),
					bytes_to_add, drop_args.bytes_found, UPDATE_QUOTA);
	btrfs_usrquota_syno_accounting(BTRFS_I(inode),
					bytes_to_add, drop_args.bytes_found, UPDATE_QUOTA);
	up_read(&root->rescan_lock);

	ret = btrfs_inode_set_file_extent_range(BTRFS_I(inode), offset, len);
	if (ret)
		goto out;

	btrfs_update_inode(trans, root, inode);

	if (disk_bytenr) {
		btrfs_init_generic_ref(&ref,
				BTRFS_ADD_DELAYED_REF,
				disk_bytenr, disk_num_bytes, 0);
		btrfs_init_data_ref(&ref,
				root->root_key.objectid,
				btrfs_ino(BTRFS_I(inode)),
				offset - disk_offset
				, syno_usage
				);
		ref.skip_qgroup = true;
		ret = btrfs_inc_extent_ref(trans, &ref);
	}

out:
	if (release_trans) {
		if (ret)
			btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
	}
	btrfs_free_path(path);
	return ret;
}

#define INLINE_DEDUPE_MIN_PAGES 32

static u64 inode_pgoff_hash_get(struct inode *inode, pgoff_t page_off, bool *zero)
{
	u64 hash = 0;
	struct page *page = NULL;
	char *kaddr = NULL;

	*zero = false;

	page = find_get_page(inode->i_mapping, page_off);
	if (!page) {
		printk("find_get_page failed, ino:%llu off:%lu\n", btrfs_ino(BTRFS_I(inode)), page_off << PAGE_SHIFT);
		goto out;
	}

	kaddr = kmap_atomic(page);
	if (!memcmp(kaddr, empty_zero_page, PAGE_SIZE))
		*zero = true;
	kunmap_atomic(kaddr);

	put_page(page);

out:
	return hash;
}

static u64 inline_dedupe_zero_cmp(struct inode *inode, pgoff_t page_start, u64 check_pgs)
{
	bool zero = false;
	u64 page_cur = page_start;
	u64 page_end = page_start + check_pgs;

	for (; page_cur < page_end; page_cur++) {
		inode_pgoff_hash_get(inode, page_cur, &zero);
		if (!zero)
			break;
	}
	return page_cur - page_start;
}

static bool __inline_dedupe_search(struct inode *inode, struct inode *src_inode,
				pgoff_t start_pgoff, u64 check_pages, u64 *match_pgoff, u64 *match_src_pgoff,
				u64 *match_pages, bool *match_zero)
{
	u64 cur = 0;
	u64 pages = 0;
	u64 hash = 0;
	bool zero = false;

	for (cur = 0; cur < check_pages; cur++) {
		hash = inode_pgoff_hash_get(inode, start_pgoff + cur, &zero);
		if (zero) {
			pages = inline_dedupe_zero_cmp(inode, start_pgoff + cur, check_pages - cur);
			if (pages >= INLINE_DEDUPE_MIN_PAGES || pages == check_pages) {
				*match_pgoff = start_pgoff + cur;
				*match_pages = pages;
				*match_zero = true;
				return true;
			}
			continue;
		}
	}

	return false;
}

/*
 * return
 * 0:		could inline dedupe
 * -ENOENT:	not match
 * -EINVAL: bad parameter
 */
int inline_dedupe_search(struct inode *inode, u64 start, u64 len,
			u64 *disk_bytenr, u64 *disk_num_bytes, u64 *disk_offset,
			u64 *match_off, u64 *match_len)
{
	int ret = -ENOENT;
	u64 match_pgoff = 0, src_pgoff = 0, match_pages = 0;
	bool zero = false;

	if (!inode || start % PAGE_SIZE || !len || !disk_bytenr || !disk_num_bytes || !disk_offset || !match_len)
		return -EINVAL;

	*disk_bytenr = *disk_num_bytes = *disk_offset = *match_off = *match_len = 0;

	if (!__inline_dedupe_search(inode, NULL, start >> PAGE_SHIFT,
			len >> PAGE_SHIFT, &match_pgoff, &src_pgoff,
			&match_pages, &zero)) {
		goto out;
	}
	*match_off = match_pgoff << PAGE_SHIFT;

	if (zero) {
		*match_len = match_pages << PAGE_SHIFT;
		ret = insert_dedupe_file_extent(NULL, inode, *match_off, *match_len, 0, 0, 0);
		goto out;
	}

out:
	return ret;
}
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE
static void syno_inode_clone_change_flags(struct inode *src,
					  struct inode *inode,
					  u64 destoff,
					  unsigned int remap_flags)
{
	int ret = -1;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans = NULL;
	u64 oldflags;

	if ((BTRFS_I(src)->flags & BTRFS_INODE_NODATASUM) ==
		(BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM) ||
	    (remap_flags && (remap_flags & ~REMAP_FILE_CAN_SHORTEN)) ||
	     0 != destoff)
		return;

	/* wait all lockless writes */
	down_write(&BTRFS_I(inode)->dio_sem);
	btrfs_wait_ordered_range(inode, 0, -1);

	if (0 != inode_get_bytes(inode))
		goto out_unlock;

	oldflags = BTRFS_I(inode)->flags;

	if (BTRFS_I(src)->flags & BTRFS_INODE_NODATASUM)
		BTRFS_I(inode)->flags |= BTRFS_INODE_NODATASUM|BTRFS_INODE_NODATACOW;
	else
		BTRFS_I(inode)->flags &= ~(BTRFS_INODE_NODATASUM|BTRFS_INODE_NODATACOW);

	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans))
		goto out_drop;

	inode_inc_iversion(inode);
	inode->i_ctime = current_time(inode);
	ret = btrfs_update_inode(trans, root, inode);

	btrfs_end_transaction(trans);
out_drop:
	if (ret)
		BTRFS_I(inode)->flags = oldflags;
out_unlock:
	up_write(&BTRFS_I(inode)->dio_sem);
	return;
}
#endif /* MY_ABC_HERE */

static noinline int btrfs_clone_files(struct file *file, struct file *file_src,
					u64 off, u64 olen, u64 destoff
#ifdef MY_ABC_HERE
					, struct btrfs_syno_clone_range_v2 *v2_args
#endif /* MY_ABC_HERE */
					)
{
	struct inode *inode = file_inode(file);
	struct inode *src = file_inode(file_src);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int ret;
	int wb_ret;
	u64 len = olen;
	u64 bs = fs_info->sb->s_blocksize;
#ifdef MY_ABC_HERE
	u64 orig_destoff = 0;
	u64 orig_len = 0;
#endif /* MY_ABC_HERE */

	/*
	 * VFS's generic_remap_file_range_prep() protects us from cloning the
	 * eof block into the middle of a file, which would result in corruption
	 * if the file size is not blocksize aligned. So we don't need to check
	 * for that case here.
	 */
	if (off + len == src->i_size)
		len = ALIGN(src->i_size, bs) - off;

	if (destoff > inode->i_size) {
		const u64 wb_start = ALIGN_DOWN(inode->i_size, bs);

		ret = btrfs_cont_expand(inode, inode->i_size, destoff);
		if (ret)
			return ret;
		/*
		 * We may have truncated the last block if the inode's size is
		 * not sector size aligned, so we need to wait for writeback to
		 * complete before proceeding further, otherwise we can race
		 * with cloning and attempt to increment a reference to an
		 * extent that no longer exists (writeback completed right after
		 * we found the previous extent covering eof and before we
		 * attempted to increment its reference count).
		 */
		ret = btrfs_wait_ordered_range(inode, wb_start,
					       destoff - wb_start);
		if (ret)
			return ret;
	}

#ifdef MY_ABC_HERE
	orig_destoff = destoff;
	orig_len = len;
clone_again:
#endif /* MY_ABC_HERE */
	/*
	 * Lock destination range to serialize with concurrent readpages() and
	 * source range to serialize with relocation.
	 */
	btrfs_double_extent_lock(src, off, inode, destoff, len);
	ret = btrfs_clone(src, inode, off, olen, len, destoff, 0
#ifdef MY_ABC_HERE
					    , v2_args
#endif /* MY_ABC_HERE */
					    );
	btrfs_double_extent_unlock(src, off, inode, destoff, len);

#ifdef MY_ABC_HERE
	if (ret == -EMLINK && v2_args) {
		if (v2_args->flag & BTRFS_CLONE_RANGE_V2_AUTO_REWRITE_SRC) {
			if (btrfs_root_readonly(BTRFS_I(src)->root))
				goto fail_out;
			ret = btrfs_clone_auto_rewrite(src, v2_args->src_off,
						       v2_args->src_len, true);
			if (0 > ret)
				goto fail_out;
			destoff = destoff + (v2_args->src_off - off);
			len = len - (v2_args->src_off - off);
			off = v2_args->src_off;
			olen = len;
			goto clone_again;
		} else if (v2_args->flag & BTRFS_CLONE_RANGE_V2_AUTO_REWRITE_DST) {
			ret = btrfs_clone_auto_rewrite(inode,
						       destoff + (v2_args->src_off - off),
						       v2_args->dest_len, false);
			if (0 > ret)
				goto fail_out;
			destoff = destoff + (v2_args->src_off - off) + v2_args->dest_len;
			len = len - (v2_args->src_off - off) - v2_args->dest_len;
			off = v2_args->src_off + v2_args->dest_len;
			olen = len;
			if (len)
				goto clone_again;
		}
	}
fail_out:
	/*
	 * We may have copied an inline extent into a page of the destination
	 * range, so wait for writeback to complete before truncating pages
	 * from the page cache. This is a rare case.
	 */
	wb_ret = btrfs_wait_ordered_range(inode, orig_destoff, orig_len);
	ret = ret ? ret : wb_ret;
	/*
	 * Truncate page cache pages so that future reads will see the cloned
	 * data immediately and not the previous data.
	 */
	truncate_inode_pages_range(&inode->i_data,
				round_down(orig_destoff, PAGE_SIZE),
				round_up(orig_destoff + orig_len, PAGE_SIZE) - 1);
#else /* MY_ABC_HERE */
	/*
	 * We may have copied an inline extent into a page of the destination
	 * range, so wait for writeback to complete before truncating pages
	 * from the page cache. This is a rare case.
	 */
	wb_ret = btrfs_wait_ordered_range(inode, destoff, len);
	ret = ret ? ret : wb_ret;
	/*
	 * Truncate page cache pages so that future reads will see the cloned
	 * data immediately and not the previous data.
	 */
	truncate_inode_pages_range(&inode->i_data,
				round_down(destoff, PAGE_SIZE),
				round_up(destoff + len, PAGE_SIZE) - 1);
#endif /* MY_ABC_HERE */

	return ret;
}

#ifdef MY_ABC_HERE
static inline
int btrfs_clone_check_compr(const struct inode *inode_in,
			    const struct inode *inode_out,
			    const unsigned int flags)
{
	const struct btrfs_inode *in = BTRFS_I(inode_in);
	const struct btrfs_inode *out = BTRFS_I(inode_out);

	if (flags & REMAP_FILE_SKIP_CHECK_COMPR_DIR)
		return 0;

	if ((in->flags & BTRFS_INODE_COMPRESS) !=
	    (out->flags & BTRFS_INODE_COMPRESS))
		return -EINVAL;

	return 0;
}
#endif /* MY_ABC_HERE */

static int btrfs_remap_file_range_prep(struct file *file_in, loff_t pos_in,
				       struct file *file_out, loff_t pos_out,
				       loff_t *len, unsigned int remap_flags)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);
	u64 bs = BTRFS_I(inode_out)->root->fs_info->sb->s_blocksize;
	bool same_inode = inode_out == inode_in;
	u64 wb_len;
	int ret;

	if (!(remap_flags & REMAP_FILE_DEDUP)) {
		struct btrfs_root *root_out = BTRFS_I(inode_out)->root;

		if (btrfs_root_readonly(root_out))
			return -EROFS;

		if (file_in->f_path.mnt != file_out->f_path.mnt ||
		    inode_in->i_sb != inode_out->i_sb)
			return -EXDEV;
	}
#ifdef MY_ABC_HERE
	syno_inode_clone_change_flags(inode_in, inode_out,
				      pos_out, remap_flags);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	ret = btrfs_clone_check_compr(inode_in, inode_out, remap_flags);
	if (ret)
		return ret;
#endif /* MY_ABC_HERE */

	/* Don't make the dst file partly checksummed */
	if ((BTRFS_I(inode_in)->flags & BTRFS_INODE_NODATASUM) !=
	    (BTRFS_I(inode_out)->flags & BTRFS_INODE_NODATASUM)) {
		return -EINVAL;
	}

	/*
	 * Now that the inodes are locked, we need to start writeback ourselves
	 * and can not rely on the writeback from the VFS's generic helper
	 * generic_remap_file_range_prep() because:
	 *
	 * 1) For compression we must call filemap_fdatawrite_range() range
	 *    twice (btrfs_fdatawrite_range() does it for us), and the generic
	 *    helper only calls it once;
	 *
	 * 2) filemap_fdatawrite_range(), called by the generic helper only
	 *    waits for the writeback to complete, i.e. for IO to be done, and
	 *    not for the ordered extents to complete. We need to wait for them
	 *    to complete so that new file extent items are in the fs tree.
	 */
	if (*len == 0 && !(remap_flags & REMAP_FILE_DEDUP))
		wb_len = ALIGN(inode_in->i_size, bs) - ALIGN_DOWN(pos_in, bs);
	else
		wb_len = ALIGN(*len, bs);

	/*
	 * Since we don't lock ranges, wait for ongoing lockless dio writes (as
	 * any in progress could create its ordered extents after we wait for
	 * existing ordered extents below).
	 */
	inode_dio_wait(inode_in);
	if (!same_inode)
		inode_dio_wait(inode_out);

	/*
	 * Workaround to make sure NOCOW buffered write reach disk as NOCOW.
	 *
	 * Btrfs' back references do not have a block level granularity, they
	 * work at the whole extent level.
	 * NOCOW buffered write without data space reserved may not be able
	 * to fall back to CoW due to lack of data space, thus could cause
	 * data loss.
	 *
	 * Here we take a shortcut by flushing the whole inode, so that all
	 * nocow write should reach disk as nocow before we increase the
	 * reference of the extent. We could do better by only flushing NOCOW
	 * data, but that needs extra accounting.
	 *
	 * Also we don't need to check ASYNC_EXTENT, as async extent will be
	 * CoWed anyway, not affecting nocow part.
	 */
	ret = filemap_flush(inode_in->i_mapping);
	if (ret < 0)
		return ret;

	ret = btrfs_wait_ordered_range(inode_in, ALIGN_DOWN(pos_in, bs),
				       wb_len);
	if (ret < 0)
		return ret;
	ret = btrfs_wait_ordered_range(inode_out, ALIGN_DOWN(pos_out, bs),
				       wb_len);
	if (ret < 0)
		return ret;

	return generic_remap_file_range_prep(file_in, pos_in, file_out, pos_out,
					    len, remap_flags);
}

#ifdef MY_ABC_HERE
static int clone_range_v2_verify_area(struct file *file, loff_t pos, loff_t len,
			     bool write)
{
	struct inode *inode = file_inode(file);

	if (unlikely(pos < 0 || len < 0))
		return -EINVAL;

	if (unlikely((loff_t) (pos + len) < 0))
		return -EINVAL;

	if (unlikely(inode->i_flctx && mandatory_lock(inode))) {
		loff_t end = len ? pos + len - 1 : OFFSET_MAX;
		int retval;

		retval = locks_mandatory_area(inode, file, pos, end,
				write ? F_WRLCK : F_RDLCK);
		if (retval < 0)
			return retval;
	}

	return security_file_permission(file, write ? MAY_WRITE : MAY_READ);
}

int btrfs_ioctl_syno_clone_range_v2(struct file *dst_file,
		struct btrfs_ioctl_syno_clone_range_args_v2 __user *argp)
{
	struct fd src_file;
	__s64 src_fd = 0;
	struct btrfs_syno_clone_range_v2 args;
	struct inode *src_inode;
	struct inode *dst_inode = file_inode(dst_file);
	bool same_inode;
	u64 len;
	int ret;
#ifdef MY_ABC_HERE
	unsigned int flags = 0;
#endif /* MY_ABC_HERE */

	memset(&args, 0, sizeof(args));
	if (copy_from_user(&args.src_off, &argp->src_offset, sizeof(args.src_off)) ||
	    copy_from_user(&args.src_len, &argp->src_length, sizeof(args.src_len)) ||
	    copy_from_user(&args.dest_off, &argp->dest_offset, sizeof(args.dest_off)) ||
	    copy_from_user(&args.ref_limit, &argp->ref_limit, sizeof(args.ref_limit)) ||
	    copy_from_user(&args.flag, &argp->flag, sizeof(args.flag)) ||
	    copy_from_user(&src_fd, &argp->src_fd, sizeof(src_fd)))
		return -EFAULT;

	if ((args.flag & BTRFS_CLONE_RANGE_V2_AUTO_REWRITE_SRC) &&
	    (args.flag & BTRFS_CLONE_RANGE_V2_AUTO_REWRITE_DST))
		return -EINVAL;

	/*
	 * Follow ioctl_file_clone_range all the way to btrfs_clone_files,
	 * most of the checks are done in
	 * btrfs_remap_file_range_prep/generic_remap_file_range_prep.
	 * What left are remap_verify_area(src/dst), and file mode check,
	 * so we have to check it by ourselves here.
	 */
	src_file = fdget(src_fd);
	if (!src_file.file)
		return -EBADF;

	file_start_write(dst_file);
	if (!(src_file.file->f_mode & FMODE_READ) ||
	    !(dst_file->f_mode & FMODE_WRITE) ||
	    (dst_file->f_flags & O_APPEND)) {
		ret = -EBADF;
		goto out;
	}

	if ((args.flag & BTRFS_CLONE_RANGE_V2_AUTO_REWRITE_SRC) &&
	    !(src_file.file->f_mode & FMODE_WRITE)) {
		ret = -EBADF;
		goto out;
	}

	len = args.src_len;
	ret = clone_range_v2_verify_area(src_file.file, args.src_off, len, false);
	if (ret)
		goto out;
	ret = clone_range_v2_verify_area(dst_file, args.dest_off, len, true);
	if (ret)
		goto out;

#ifdef MY_ABC_HERE
	/*
	 * There's workarounds for DSM #81059 and DSM#150209. We allow to clone
	 * between compression and no compression dirs in TWO conditions:
	 * 1. if we do IOC_CLONE_RANGE with whole file.
	 * 2. if the args.flag is with the specified flag. (btrfs receive)
	 */
	if ((args.flag & BTRFS_CLONE_RANGE_V2_SKIP_CHECK_COMPR_DIR) ||
	    (!args.src_off && !args.dest_off && !len))
		flags = REMAP_FILE_SKIP_CHECK_COMPR_DIR;
#endif /* MY_ABC_HERE */

	src_inode = file_inode(src_file.file);
	same_inode = dst_inode == src_inode;
	if (same_inode)
		inode_lock(src_inode);
	else
		lock_two_nondirectories(src_inode, dst_inode);

	ret = btrfs_remap_file_range_prep(src_file.file, args.src_off,
	                                  dst_file, args.dest_off, &len
#ifdef MY_ABC_HERE
					  , flags
#else /* MY_ABC_HERE */
					  , 0
#endif /* MY_ABC_HERE */
					  );
	if (ret < 0 || len == 0)
		goto out_unlock;

	ret = btrfs_clone_files(dst_file, src_file.file, args.src_off,
	                        len, args.dest_off, &args);
	if (ret < 0 && ret != -EMLINK)
		goto out_unlock;

	if (ret == -EMLINK &&
	    (put_user(args.src_off, &argp->src_offset) ||
	     put_user(args.src_len, &argp->src_length) ||
	     put_user(args.ref_limit, &argp->ref_limit))) {
		ret = -EFAULT;
		goto out_unlock;
	}
	fsnotify_access(src_file.file);
	fsnotify_modify(dst_file);
out_unlock:
	if (same_inode)
		inode_unlock(src_inode);
	else
		unlock_two_nondirectories(src_inode, dst_inode);
out:
	file_end_write(dst_file);
	fdput(src_file);
	return ret;
}
#endif /* MY_ABC_HERE */

loff_t btrfs_remap_file_range(struct file *src_file, loff_t off,
		struct file *dst_file, loff_t destoff, loff_t len,
		unsigned int remap_flags)
{
	struct inode *src_inode = file_inode(src_file);
	struct inode *dst_inode = file_inode(dst_file);
	bool same_inode = dst_inode == src_inode;
	int ret;

	if (remap_flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_ADVISORY))
		return -EINVAL;

	if (same_inode)
		inode_lock(src_inode);
	else
		lock_two_nondirectories(src_inode, dst_inode);

	ret = btrfs_remap_file_range_prep(src_file, off, dst_file, destoff,
					  &len, remap_flags);
	if (ret < 0 || len == 0)
		goto out_unlock;

	if (remap_flags & REMAP_FILE_DEDUP)
		ret = btrfs_extent_same(src_inode, off, len, dst_inode, destoff);
	else
		ret = btrfs_clone_files(dst_file, src_file, off, len, destoff
#ifdef MY_ABC_HERE
					    , NULL
#endif /* MY_ABC_HERE */
					    );

out_unlock:
	if (same_inode)
		inode_unlock(src_inode);
	else
		unlock_two_nondirectories(src_inode, dst_inode);

	return ret < 0 ? ret : len;
}
