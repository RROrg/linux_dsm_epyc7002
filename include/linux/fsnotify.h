#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_NOTIFY_H
#define _LINUX_FS_NOTIFY_H

/*
 * include/linux/fsnotify.h - generic hooks for filesystem notification, to
 * reduce in-source duplication from both dnotify and inotify.
 *
 * We don't compile any of this away in some complicated menagerie of ifdefs.
 * Instead, we rely on the code inside to optimize away as needed.
 *
 * (C) Copyright 2005 Robert Love
 */

#include <linux/fsnotify_backend.h>
#include <linux/audit.h>
#include <linux/slab.h>
#include <linux/bug.h>
#ifdef MY_ABC_HERE
#include <linux/mount.h>

extern int SYNONotify(struct dentry *dentry, __u32 mask);
#endif /* MY_ABC_HERE */

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
static inline void syno_archive_bit_modify(struct inode *inode, int set_smb_archive)
{
	struct dentry *dentry;
#ifdef MY_ABC_HERE
	u32 new_archive_bit;
	u32 old_archive_bit;
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	u32 sb_archive_ver;
	int err;
#endif /* MY_ABC_HERE */

	if (NULL == inode)
		return;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
			S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode))
		return;
	if (!strcmp(inode->i_sb->s_type->name, "c2fs"))
		return;
	dentry = d_find_alias(inode);
	if (!dentry)
		return;

#ifdef MY_ABC_HERE
	mutex_lock(&inode->i_archive_bit_mutex);
	if (syno_op_get_archive_bit(dentry, &old_archive_bit))
		goto unlock;

	if (set_smb_archive)
		new_archive_bit = old_archive_bit | S2_SMB_ARCHIVE | ALL_IARCHIVE;
	else
		new_archive_bit = old_archive_bit | ALL_IARCHIVE;

	if (new_archive_bit != old_archive_bit)
		syno_op_set_archive_bit_nolock(dentry, new_archive_bit);

unlock:
	mutex_unlock(&inode->i_archive_bit_mutex);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
	err = syno_op_get_sb_archive_version(inode->i_sb, &sb_archive_ver);
	if (err)
		goto out;
	err = syno_op_set_inode_archive_version(dentry, sb_archive_ver + 1);
out:
#endif /* MY_ABC_HERE */
	if (dentry)
		dput(dentry);
}
#endif /* MY_ABC_HERE || MY_ABC_HERE*/

/*
 * Notify this @dir inode about a change in a child directory entry.
 * The directory entry may have turned positive or negative or its inode may
 * have changed (i.e. renamed over).
 *
 * Unlike fsnotify_parent(), the event will be reported regardless of the
 * FS_EVENT_ON_CHILD mask on the parent inode and will not be reported if only
 * the child is interested and not the parent.
 */
static inline void fsnotify_name(struct inode *dir, __u32 mask,
				 struct inode *child,
				 const struct qstr *name, u32 cookie)
{
	fsnotify(mask, child, FSNOTIFY_EVENT_INODE, dir, name, NULL, cookie);
}

static inline void fsnotify_dirent(struct inode *dir, struct dentry *dentry,
				   __u32 mask)
{
	fsnotify_name(dir, mask, d_inode(dentry), &dentry->d_name, 0);
}

static inline void fsnotify_inode(struct inode *inode, __u32 mask)
{
	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify(mask, inode, FSNOTIFY_EVENT_INODE, NULL, NULL, inode, 0);
}

/* Notify this dentry's parent about a child's events. */
static inline int fsnotify_parent(struct dentry *dentry, __u32 mask,
				  const void *data, int data_type)
{
	struct inode *inode = d_inode(dentry);

	if (S_ISDIR(inode->i_mode)) {
		mask |= FS_ISDIR;

		/* sb/mount marks are not interested in name of directory */
		if (!(dentry->d_flags & DCACHE_FSNOTIFY_PARENT_WATCHED))
			goto notify_child;
	}

	/* disconnected dentry cannot notify parent */
	if (IS_ROOT(dentry))
		goto notify_child;

	return __fsnotify_parent(dentry, mask, data, data_type);

notify_child:
	return fsnotify(mask, data, data_type, NULL, NULL, inode, 0);
}

/*
 * Simple wrappers to consolidate calls to fsnotify_parent() when an event
 * is on a file/dentry.
 */
static inline void fsnotify_dentry(struct dentry *dentry, __u32 mask)
{
	fsnotify_parent(dentry, mask, d_inode(dentry), FSNOTIFY_EVENT_INODE);
}

static inline int fsnotify_file(struct file *file, __u32 mask)
{
	const struct path *path = &file->f_path;

	if (file->f_mode & FMODE_NONOTIFY)
		return 0;

	return fsnotify_parent(path->dentry, mask, path, FSNOTIFY_EVENT_PATH);
}

/* Simple call site for access decisions */
static inline int fsnotify_perm(struct file *file, int mask)
{
	int ret;
	__u32 fsnotify_mask = 0;

	if (!(mask & (MAY_READ | MAY_OPEN)))
		return 0;

	if (mask & MAY_OPEN) {
		fsnotify_mask = FS_OPEN_PERM;

		if (file->f_flags & __FMODE_EXEC) {
			ret = fsnotify_file(file, FS_OPEN_EXEC_PERM);

			if (ret)
				return ret;
		}
	} else if (mask & MAY_READ) {
		fsnotify_mask = FS_ACCESS_PERM;
	}

	return fsnotify_file(file, fsnotify_mask);
}

/*
 * fsnotify_link_count - inode's link count changed
 */
static inline void fsnotify_link_count(struct inode *inode)
{
	fsnotify_inode(inode, FS_ATTRIB);
}

#ifdef MY_ABC_HERE
struct synotify_rename_path {
	char *old_full_path;
	char *new_full_path;
	struct vfsmount *vfs_mnt;
	struct synotify_rename_path *next;
};
#endif /* MY_ABC_HERE */

/*
 * fsnotify_move - file old_name at old_dir was moved to new_name at new_dir
 */
static inline void fsnotify_move(struct inode *old_dir, struct inode *new_dir,
				 const struct qstr *old_name,
				 int isdir, struct inode *target,
				 struct dentry *moved
#ifdef MY_ABC_HERE
				 , struct synotify_rename_path *path_list, bool is_exchange
#endif /* MY_ABC_HERE */
				 )
{
	struct inode *source = moved->d_inode;
	u32 fs_cookie = fsnotify_get_cookie();
	__u32 old_dir_mask = FS_MOVED_FROM;
	__u32 new_dir_mask = FS_MOVED_TO;
	const struct qstr *new_name = &moved->d_name;

	if (old_dir == new_dir)
		old_dir_mask |= FS_DN_RENAME;

	if (isdir) {
		old_dir_mask |= FS_ISDIR;
		new_dir_mask |= FS_ISDIR;
	}

#ifdef MY_ABC_HERE
	/* handle syno notify:
	 * 1. we should check if file/dir moved within same mnt point. If does, we simply
	 *    notify a rename event.
	 * 2. if this rename does not occur within same mnt point, then we have to send MOVE_FROM
	 *    and MOVE_TO to mnt points respectively.
	 */

	// prepare source notify data
	while(path_list) {
		struct synotify_rename_path *tmp = path_list;
		struct path tmp_path;

		memset (&tmp_path, 0, sizeof(struct path));

		tmp_path.mnt = tmp->vfs_mnt;

		if (is_exchange) {
			__SYNONotify(old_dir_mask, &tmp_path, FSNOTIFY_EVENT_SYNO_MOVE, tmp->new_full_path, fs_cookie);
			__SYNONotify(new_dir_mask, &tmp_path, FSNOTIFY_EVENT_SYNO_MOVE, tmp->old_full_path, fs_cookie);
		} else {
			__SYNONotify(old_dir_mask, &tmp_path, FSNOTIFY_EVENT_SYNO_MOVE, tmp->old_full_path, fs_cookie);
			__SYNONotify(new_dir_mask, &tmp_path, FSNOTIFY_EVENT_SYNO_MOVE, tmp->new_full_path, fs_cookie);
		}
		path_list = path_list->next;
	}
#endif /* MY_ABC_HERE */

	fsnotify_name(old_dir, old_dir_mask, source, old_name, fs_cookie);
	fsnotify_name(new_dir, new_dir_mask, source, new_name, fs_cookie);

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	syno_archive_bit_modify(old_dir, 0);
	if (old_dir != new_dir)
		syno_archive_bit_modify(new_dir, 0);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

	if (target)
		fsnotify_link_count(target);
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	if (target)
		syno_archive_bit_modify(target, 0);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

	fsnotify_inode(source, FS_MOVE_SELF);
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	syno_archive_bit_modify(source, 1);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

	audit_inode_child(new_dir, moved, AUDIT_TYPE_CHILD_CREATE);
}

/*
 * fsnotify_inode_delete - and inode is being evicted from cache, clean up is needed
 */
static inline void fsnotify_inode_delete(struct inode *inode)
{
	__fsnotify_inode_delete(inode);
}

/*
 * fsnotify_vfsmount_delete - a vfsmount is being destroyed, clean up is needed
 */
static inline void fsnotify_vfsmount_delete(struct vfsmount *mnt)
{
	__fsnotify_vfsmount_delete(mnt);
}

/*
 * fsnotify_inoderemove - an inode is going away
 */
static inline void fsnotify_inoderemove(struct inode *inode)
{
	fsnotify_inode(inode, FS_DELETE_SELF);
	__fsnotify_inode_delete(inode);
}

/*
 * fsnotify_create - 'name' was linked in
 */
static inline void fsnotify_create(struct inode *inode, struct dentry *dentry)
{
	audit_inode_child(inode, dentry, AUDIT_TYPE_CHILD_CREATE);

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	syno_archive_bit_modify(dentry->d_inode, 0);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

#ifdef MY_ABC_HERE
	SYNONotify(dentry, FS_CREATE);
#endif /* MY_ABC_HERE */

	fsnotify_dirent(inode, dentry, FS_CREATE);
}

/*
 * fsnotify_link - new hardlink in 'inode' directory
 * Note: We have to pass also the linked inode ptr as some filesystems leave
 *   new_dentry->d_inode NULL and instantiate inode pointer later
 */
static inline void fsnotify_link(struct inode *dir, struct inode *inode,
				 struct dentry *new_dentry)
{
	fsnotify_link_count(inode);
	audit_inode_child(dir, new_dentry, AUDIT_TYPE_CHILD_CREATE);

#ifdef MY_ABC_HERE
	SYNONotify(new_dentry, FS_CREATE);
#endif /* MY_ABC_HERE */

	fsnotify_name(dir, FS_CREATE, inode, &new_dentry->d_name, 0);
}

/*
 * fsnotify_unlink - 'name' was unlinked
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsnotify_unlink(struct inode *dir, struct dentry *dentry)
{
	/* Expected to be called before d_delete() */
	WARN_ON_ONCE(d_is_negative(dentry));

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	syno_archive_bit_modify(dir, 0);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

#ifdef MY_ABC_HERE
	SYNONotify(dentry, FS_DELETE);
#endif /* MY_ABC_HERE */

	fsnotify_dirent(dir, dentry, FS_DELETE);
}

/*
 * fsnotify_mkdir - directory 'name' was created
 */
static inline void fsnotify_mkdir(struct inode *inode, struct dentry *dentry)
{
	audit_inode_child(inode, dentry, AUDIT_TYPE_CHILD_CREATE);

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	syno_archive_bit_modify(dentry->d_inode, 0);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

#ifdef MY_ABC_HERE
	SYNONotify(dentry, FS_CREATE | FS_ISDIR);
#endif /* MY_ABC_HERE */

	fsnotify_dirent(inode, dentry, FS_CREATE | FS_ISDIR);
}

/*
 * fsnotify_rmdir - directory 'name' was removed
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsnotify_rmdir(struct inode *dir, struct dentry *dentry)
{
	/* Expected to be called before d_delete() */
	WARN_ON_ONCE(d_is_negative(dentry));

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	syno_archive_bit_modify(dir, 0);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

#ifdef MY_ABC_HERE
	SYNONotify(dentry, FS_DELETE | FS_ISDIR);
#endif /* MY_ABC_HERE */

	fsnotify_dirent(dir, dentry, FS_DELETE | FS_ISDIR);
}

/*
 * fsnotify_access - file was read
 */
static inline void fsnotify_access(struct file *file)
{
	fsnotify_file(file, FS_ACCESS);
}

/*
 * fsnotify_modify - file was modified
 */
static inline void fsnotify_modify(struct file *file)
{
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	syno_archive_bit_modify(file_inode(file), 1);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

	fsnotify_file(file, FS_MODIFY);
}

/*
 * fsnotify_open - file was opened
 */
static inline void fsnotify_open(struct file *file)
{
	__u32 mask = FS_OPEN;

	if (file->f_flags & __FMODE_EXEC)
		mask |= FS_OPEN_EXEC;

	fsnotify_file(file, mask);
}

/*
 * fsnotify_close - file was closed
 */
static inline void fsnotify_close(struct file *file)
{
	__u32 mask = (file->f_mode & FMODE_WRITE) ? FS_CLOSE_WRITE :
						    FS_CLOSE_NOWRITE;

	fsnotify_file(file, mask);
}

/*
 * fsnotify_xattr - extended attributes were changed
 */
static inline void fsnotify_xattr(struct dentry *dentry)
{
#ifdef MY_ABC_HERE
	__u32 mask = FS_ATTRIB;

	if (S_ISDIR(dentry->d_inode->i_mode))
		mask |= FS_ISDIR;
#endif /* MY_ABC_HERE */

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	syno_archive_bit_modify(dentry->d_inode, 1);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

#ifdef MY_ABC_HERE
	SYNONotify(dentry, mask);
#endif /* MY_ABC_HERE */

	fsnotify_dentry(dentry, FS_ATTRIB);
}

/*
 * fsnotify_change - notify_change event.  file was modified and/or metadata
 * was changed.
 */
static inline void fsnotify_change(struct dentry *dentry, unsigned int ia_valid)
{
	__u32 mask = 0;

	if (ia_valid & ATTR_UID)
		mask |= FS_ATTRIB;
	if (ia_valid & ATTR_GID)
		mask |= FS_ATTRIB;
	if (ia_valid & ATTR_SIZE)
		mask |= FS_MODIFY;
#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
	if (ia_valid & ATTR_SIZE)
		syno_archive_bit_modify(dentry->d_inode, 1);
#endif /* MY_ABC_HERE || MY_ABC_HERE */

	/* both times implies a utime(s) call */
	if ((ia_valid & (ATTR_ATIME | ATTR_MTIME)) == (ATTR_ATIME | ATTR_MTIME))
		mask |= FS_ATTRIB;
	else if (ia_valid & ATTR_ATIME)
		mask |= FS_ACCESS;
	else if (ia_valid & ATTR_MTIME)
		mask |= FS_MODIFY;

	if (ia_valid & ATTR_MODE)
		mask |= FS_ATTRIB;

	if (mask)
		fsnotify_dentry(dentry, mask);

#ifdef MY_ABC_HERE
	if (mask) {
		if (S_ISDIR(dentry->d_inode->i_mode))
			mask |= FS_ISDIR;
		SYNONotify(dentry, mask);
	}
#endif /* MY_ABC_HERE */
}
#ifdef MY_ABC_HERE
extern void free_rename_path_list(struct synotify_rename_path * rename_path_list);
extern struct synotify_rename_path * get_rename_path_list(struct dentry *old_dentry, struct dentry *new_dentry);
#endif /* MY_ABC_HERE */

#endif	/* _LINUX_FS_NOTIFY_H */
