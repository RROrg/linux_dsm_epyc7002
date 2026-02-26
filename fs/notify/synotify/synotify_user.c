#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#ifdef MY_ABC_HERE
#include <linux/syscalls.h>
#endif /* MY_ABC_HERE */
#include <linux/file.h>
#include <linux/fs.h> /* struct inode */
#include <linux/fsnotify_backend.h>
#include <linux/idr.h>
#include <linux/init.h> /* module_init */
#include <linux/synotify.h>
#include <linux/kernel.h> /* roundup() */
#include <linux/namei.h> /* LOOKUP_FOLLOW */
#include <linux/sched.h> /* struct user */
#include <linux/slab.h> /* struct kmem_cache */
#include <linux/types.h>
#include <linux/anon_inodes.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/mount.h> /* struct vfsmount */
#include <linux/memcontrol.h>

#include <asm/ioctls.h>

#include "synotify.h"
#ifdef MY_ABC_HERE
#include <linux/syno_acl.h>
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
#define SYNOTIFY_DEFAULT_MAX_EVENTS	16384 /* per group */
#define SYNOTIFY_DEFAULT_MAX_WATCHERS	8192 /* per group */
#define SYNOTIFY_DEFAULT_MAX_INSTANCES	128 /* per user */

extern const struct fsnotify_ops synotify_fsnotify_ops;

static int synotify_max_queued_events = SYNOTIFY_DEFAULT_MAX_EVENTS;

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
struct ctl_table synotify_table[] = {
	{
		.procname       = "max_queued_events",
		.data           = &synotify_max_queued_events,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = SYSCTL_ZERO
	},
	{ }
};
#endif /* CONFIG_SYSCTL */

// Not including name !
static int get_event_fixed_size(struct fsnotify_group *group)
{
	if (group->synotify_data.event_version == 2)
		return sizeof(struct synotify_event_v2);
	else
		return sizeof(struct synotify_event);
}

static int round_event_name_len(struct fsnotify_group *group,
		struct synotify_event_info *event)
{
	if (!event->full_path_len)
		return 0;

	if (group->synotify_data.event_version == 2)
		return roundup(event->full_path_len + 1, sizeof(struct synotify_event_v2));
	else
		return roundup(event->full_path_len + 1, sizeof(struct synotify_event));
}

/*
 * Get an fsnotify notification event if one exists and is small
 * enough to fit in "count". Return an error pointer if the count
 * is not large enough.
 */
static struct synotify_event_info *get_one_event(struct fsnotify_group *group,
					    size_t count)
{
	size_t event_size;
	struct synotify_event_info *event = NULL;

	pr_debug("%s: group=%p count=%zd\n", __func__, group, count);

	spin_lock(&group->notification_lock);
	if (fsnotify_notify_queue_is_empty(group))
		goto out;

	event = SYNOTIFY_E(fsnotify_peek_first_event(group));

	if (event->path_ready == false && event->overflow_event == false) {
		event = NULL;
		goto out;
	}

	event_size = get_event_fixed_size(group);
	event_size += round_event_name_len(group, event);
	if (event_size > count) {
		event = ERR_PTR(-EINVAL);
		goto out;
	}
	event = SYNOTIFY_E(fsnotify_remove_first_event(group));
out:
	spin_unlock(&group->notification_lock);
	return event;
}

static inline u32 synotify_mask_to_arg(__u32 mask)
{
	return mask & SYNO_ALL_EVENTS;
}

static ssize_t copy_event_to_user(struct fsnotify_group *group,
				  struct synotify_event_info *event,
				  char __user *buf)
{
	struct synotify_event_v2 synotify_event; // Use largest version.
	size_t event_size;
	size_t pad_name_len;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	/*
	 * round up event->name_len so it is a multiple of event_size
	 * plus an extra byte for the terminating '\0'.
	 */
	pad_name_len = round_event_name_len(group, event);
	synotify_event.len = pad_name_len;
	synotify_event.mask = synotify_mask_to_arg(event->mask);
	synotify_event.cookie = event->sync_cookie;

	if (group->synotify_data.event_version == 2) {
		event_size = sizeof(struct synotify_event_v2);
		synotify_event.pid = (u32)event->pid;
		synotify_event.uid = (u32)event->uid;
	} else
		event_size = sizeof(struct synotify_event);

	/* send the main event */
	if (copy_to_user(buf, &synotify_event, event_size))
		return -EFAULT;

	buf += event_size;

	/*
	 * fsnotify only stores the pathname, so here we have to send the pathname
	 * and then pad that pathname out to a multiple of sizeof(synotify_event)
	 * with zeros.
	 */
	if (pad_name_len) {
		/* copy the path name */
		if (copy_to_user(buf, event->full_path, event->full_path_len))
			return -EFAULT;
		buf += event->full_path_len;

		/* fill userspace with 0's */
		if (clear_user(buf, pad_name_len - event->full_path_len))
			return -EFAULT;
		event_size += pad_name_len;
	}

	return event_size;
}

/* return true if the notify queue is empty, false otherwise */
static bool synotify_queue_is_empty(struct fsnotify_group *group)
{
	struct synotify_event_info *event = NULL;

	if (fsnotify_notify_queue_is_empty(group))
		return true;

	event = SYNOTIFY_E(fsnotify_peek_first_event(group));
	return (event->path_ready || event->overflow_event) ? false : true;
}

/* synotifiy userspace file descriptor functions */
static __poll_t synotify_poll(struct file *file, poll_table *wait)
{
	struct fsnotify_group *group = file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &group->notification_waitq, wait);
	spin_lock(&group->notification_lock);
	if (!synotify_queue_is_empty(group))
		ret = POLLIN | POLLRDNORM;
	spin_unlock(&group->notification_lock);

	return ret;
}

static ssize_t synotify_read(struct file *file, char __user *buf,
			     size_t count, loff_t *pos)
{
	struct fsnotify_group *group;
	struct synotify_event_info *event;
	char __user *start;
	int ret;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	start = buf;
	group = file->private_data;

	pr_debug("%s: group=%p\n", __func__, group);

	add_wait_queue(&group->notification_waitq, &wait);
	while (1) {
		/*
		 * User can supply arbitrarily large buffer. Avoid softlockups
		 * in case there are lots of available events.
		 */
		cond_resched();
		event = get_one_event(group, count);
		if (IS_ERR(event)) {
			ret = PTR_ERR(event);
			break;
		}

		if (!event) {
			ret = -EAGAIN;
			if (file->f_flags & O_NONBLOCK)
				break;

			ret = -ERESTARTSYS;
			if (signal_pending(current))
				break;

			if (start != buf)
				break;

			wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
			continue;
		}

		ret = copy_event_to_user(group, event, buf);
		if (unlikely(ret == -EOPENSTALE)) {
			/*
			 * We cannot report events with stale fd so drop it.
			 * Setting ret to 0 will continue the event loop and
			 * do the right thing if there are no more events to
			 * read (i.e. return bytes read, -EAGAIN or wait).
			 */
			ret = 0;
		}

		fsnotify_destroy_event(group, &event->fse);
		if (ret < 0)
			break;
		buf += ret;
		count -= ret;
	}
	remove_wait_queue(&group->notification_waitq, &wait);

	if (start != buf && ret != -EFAULT)
		ret = buf - start;
	return ret;
}

static int synotify_release(struct inode *ignored, struct file *file)
{
	struct fsnotify_group *group = file->private_data;

	pr_debug("%s: group=%p\n", __func__, group);

	/* matches the SYNONotifyInit->fsnotify_alloc_group */
	fsnotify_destroy_group(group);

	return 0;
}

static long synotify_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct fsnotify_group *group;
	struct fsnotify_event *fsn_event;
	struct synotify_event_info *event = NULL;
	void __user *p;
	int ret = -ENOTTY;
	size_t send_len = 0;

	group = file->private_data;
	p = (void __user *) arg;

	pr_debug("%s: group=%p cmd=%u\n", __func__, group, cmd);

	switch (cmd) {
	case FIONREAD:
		spin_lock(&group->notification_lock);
		list_for_each_entry(fsn_event, &group->notification_list, list) {
			event = SYNOTIFY_E(fsn_event);

			if (!event->path_ready && !event->overflow_event) {
				break;
			}
			send_len += get_event_fixed_size(group);
			send_len += round_event_name_len(group, event);
		}
		spin_unlock(&group->notification_lock);
		ret = put_user(send_len, (int __user *) p);
		break;
	}

	return ret;
}

static const struct file_operations synotify_fops = {
	.poll		= synotify_poll,
	.read		= synotify_read,
	.fasync		= NULL,
	.release	= synotify_release,
	.unlocked_ioctl	= synotify_ioctl,
	.compat_ioctl	= synotify_ioctl,
	.llseek		= noop_llseek,
};

static int synotify_find_path(const char __user *filename,
			      struct path *path, unsigned int flags, __u64 mask)
{
	int ret;

	pr_debug("%s: filename=%p flags=%x\n", __func__, filename, flags);

	ret = user_path_at(AT_FDCWD, filename, flags, path);
	if (ret)
		return ret;

	/* you can only watch an inode if you have read permissions on it */
#ifdef MY_ABC_HERE
	if (IS_SYNOACL(path->dentry))
		ret = synoacl_op_permission(path->dentry, MAY_READ);
	else
#endif /* MY_ABC_HERE */
		ret = inode_permission(path->dentry->d_inode, MAY_READ);
	if (ret)
		goto out;

	ret = security_path_notify(path, mask, FSNOTIFY_OBJ_TYPE_SYNO_VFSMOUNT);

out:
	if (ret)
		path_put(path);
	return ret;
}

static __u32 synotify_mark_remove_from_mask(struct fsnotify_mark *fsn_mark,
					    __u32 mask, int *destroy)
{
	__u32 oldmask;

	mask &= ~SYNO_DONT_FOLLOW;
	spin_lock(&fsn_mark->lock);

	oldmask = fsn_mark->mask;
	fsn_mark->mask &= ~mask;
	*destroy = !(fsn_mark->mask & ~SYNO_DONT_FOLLOW);

	spin_unlock(&fsn_mark->lock);

	return mask & oldmask;
}

static int synotify_remove_vfsmount_mark(struct fsnotify_group *group,
					 struct vfsmount *mnt, __u32 mask)
{
	struct fsnotify_mark *fsn_mark = NULL;
	fsnotify_connp_t *connp = &real_mount(mnt)->mnt_fsnotify_syno_marks;
	__u32 removed;
	int destroy_mark;

	mutex_lock(&group->mark_mutex);
	fsn_mark = fsnotify_find_mark(connp, group);
	if (!fsn_mark) {
		mutex_unlock(&group->mark_mutex);
		return -ENOENT;
	}

	removed = synotify_mark_remove_from_mask(fsn_mark, mask, &destroy_mark);
	if (removed & fsnotify_conn_mask(fsn_mark->connector))
		fsnotify_recalc_mask(fsn_mark->connector);
	if (destroy_mark)
		fsnotify_detach_mark(fsn_mark);
	mutex_unlock(&group->mark_mutex);
	if (destroy_mark)
		fsnotify_free_mark(fsn_mark);

	/* matches the fsnotify_find_mark() */
	fsnotify_put_mark(fsn_mark);
	return 0;
}

static __u32 synotify_mark_add_to_mask(struct fsnotify_mark *fsn_mark,
				       __u32 mask)
{
	__u32 oldmask = 0;
	__u32 setmask = 0;

	setmask = mask & ~SYNO_DONT_FOLLOW;

	spin_lock(&fsn_mark->lock);

	oldmask = fsn_mark->mask;
	fsn_mark->mask |= setmask;

	spin_unlock(&fsn_mark->lock);

	/* return new add event */
	return setmask & ~oldmask;
}

static struct fsnotify_mark *synotify_add_new_mark(struct fsnotify_group *group,
						   fsnotify_connp_t *connp)
{
	struct fsnotify_mark *mark;
	int ret;

	if (atomic_read(&group->num_marks) > group->synotify_data.max_watchers)
		return ERR_PTR(-ENOSPC);

	mark = kmalloc(sizeof(struct fsnotify_mark), GFP_KERNEL_ACCOUNT);
	if (!mark)
		return ERR_PTR(-ENOMEM);

	fsnotify_init_mark(mark, group);
	ret = fsnotify_add_mark_locked(mark, connp, FSNOTIFY_OBJ_TYPE_SYNO_VFSMOUNT, 0, NULL);
	if (ret) {
		fsnotify_put_mark(mark);
		return ERR_PTR(ret);
	}

	return mark;
}

static int synotify_add_vfsmount_mark(struct fsnotify_group *group,
				      struct vfsmount *mnt, __u32 mask)
{
	struct fsnotify_mark *fsn_mark;
	fsnotify_connp_t *connp = &real_mount(mnt)->mnt_fsnotify_syno_marks;
	__u32 added;

	mutex_lock(&group->mark_mutex);
	fsn_mark = fsnotify_find_mark(connp, group);
	if (!fsn_mark) {
		fsn_mark = synotify_add_new_mark(group, connp);
		if (IS_ERR(fsn_mark)) {
			mutex_unlock(&group->mark_mutex);
			return PTR_ERR(fsn_mark);
		}
	}
	added = synotify_mark_add_to_mask(fsn_mark, mask);
	if (added & ~fsnotify_conn_mask(fsn_mark->connector))
		fsnotify_recalc_mask(fsn_mark->connector);
	mutex_unlock(&group->mark_mutex);

	fsnotify_put_mark(fsn_mark);
	return 0;
}

static struct fsnotify_event *synotify_alloc_overflow_event(void)
{
	struct synotify_event_info *oevent;

	oevent = kzalloc(sizeof(*oevent), GFP_KERNEL_ACCOUNT);
	if (!oevent)
		return NULL;

	fsnotify_init_event(&oevent->fse, 0);
	oevent->mask = SYNO_Q_OVERFLOW;
	oevent->overflow_event = true;

	return &oevent->fse;
}

static int __syno_notify_init(unsigned int flags)
{
	struct fsnotify_group *group;
	int f_flags = 0;
	int fd = 0;
	struct user_struct *user;

	pr_debug("%s: flags=%x\n", __func__, flags);

	if (flags & ~(SYNO_NONBLOCK | SYNO_CLOEXEC | SYNO_EVENT_V2))
		return -EINVAL;

	if (flags & SYNO_CLOEXEC)
		f_flags |= O_CLOEXEC;
	if (flags & SYNO_NONBLOCK)
		f_flags |= O_NONBLOCK;

	user = get_current_user();
	if (!uid_eq(user->uid, GLOBAL_ROOT_UID))
		return -EPERM;

	if (atomic_read(&user->synotify_instances) > SYNOTIFY_DEFAULT_MAX_INSTANCES) {
		free_uid(user);
		return -EMFILE;
	}

	/* fsnotify_alloc_group takes a ref.  Dropped in synotify_release */
	group = fsnotify_alloc_group(&synotify_fsnotify_ops);
	if (IS_ERR(group)) {
		free_uid(user);
		return PTR_ERR(group);
	}

	group->synotify_data.synotify_full_path_buf = kzalloc(PATH_MAX, GFP_KERNEL_ACCOUNT);
	group->synotify_data.synotify_d_path_buf = kzalloc(PATH_MAX, GFP_KERNEL_ACCOUNT);
	if (!group->synotify_data.synotify_full_path_buf || !group->synotify_data.synotify_d_path_buf) {
		fd = -ENOMEM;
		goto out_destroy_group;
	}

	group->synotify_data.user = user;
	atomic_inc(&user->synotify_instances);
	group->memcg = get_mem_cgroup_from_mm(current->mm);

	group->overflow_event = synotify_alloc_overflow_event();
	if (unlikely(!group->overflow_event)) {
		fd = -ENOMEM;
		goto out_destroy_group;
	}

	group->max_events = synotify_max_queued_events;
	group->synotify_data.max_watchers = SYNOTIFY_DEFAULT_MAX_WATCHERS;
	printk(KERN_INFO "Synotify use %d event queue size\n", group->max_events);

	if (flags & SYNO_EVENT_V2)
		group->synotify_data.event_version = 2;
	else
		group->synotify_data.event_version = 1;

	fd = anon_inode_getfd("[synotify]", &synotify_fops, group, f_flags);
	if (fd < 0)
		goto out_destroy_group;

	return fd;

out_destroy_group:
	fsnotify_destroy_group(group); // Also free group's path buffer
	return fd;
}

static int synotify_set_mark(int synotify_fd, const char __user * pathname, __u64 mask, unsigned int synotify_flag)
{
	struct vfsmount *mnt = NULL;
	struct fsnotify_group *group;
	struct path path;
	struct fd f;
	int ret = -EINVAL;
	unsigned int flags = 0;

	pr_debug("%s: synotify_fd=%d pathname=%p mask=%llx\n",__FUNCTION__, synotify_fd, pathname, mask);

	/* we only use the lower 32 bits as of right now. */
	if (mask & ((__u64)0xffffffff << 32))
		return -EINVAL;

	if (mask & ~(SYNO_ALL_EVENTS | SYNO_DONT_FOLLOW))
		return -EINVAL;

	f = fdget(synotify_fd);
	if (unlikely(!f.file))
		return -EBADF;

	/* verify that this is indeed an synotify instance */
	if (unlikely(f.file->f_op != &synotify_fops)) {
		ret = -EINVAL;
		goto fput_and_out;
	}

	if (!(mask & SYNO_DONT_FOLLOW))
		flags |= LOOKUP_FOLLOW;

	ret = synotify_find_path(pathname, &path, flags,
		(mask & SYNO_ALL_EVENTS));
	if (ret)
		goto fput_and_out;

	group = f.file->private_data;

	mnt = path.mnt;

	/* add/remove an vfsmount mark */
	switch (synotify_flag & (SYNOTIFY_MARK_ADD | SYNOTIFY_MARK_REMOVE)) {
	case SYNOTIFY_MARK_ADD:
		ret = synotify_add_vfsmount_mark(group, mnt, mask);
		break;
	case SYNOTIFY_MARK_REMOVE:
		ret = synotify_remove_vfsmount_mark(group, mnt, mask);
		break;
	default:
		ret = -EINVAL;
	}

	path_put(&path);
fput_and_out:
	fdput(f);
	return ret;
}
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
/* synotify syscalls */
SYSCALL_DEFINE1(syno_notify_init, unsigned int, flags)
{
#ifdef MY_ABC_HERE
	return __syno_notify_init(flags);
#else
	return -EOPNOTSUPP;
#endif /* MY_ABC_HERE */
}

SYSCALL_DEFINE1(SYNONotifyInit, unsigned int, flags)
{
#ifdef MY_ABC_HERE
	return __syno_notify_init(flags);
#else
	return -EOPNOTSUPP;
#endif /* MY_ABC_HERE */
}

SYSCALL_DEFINE3(syno_notify_remove_watch, int, synotify_fd, const char __user *, pathname, __u64, mask)
{
#ifdef MY_ABC_HERE
	return synotify_set_mark(synotify_fd, pathname, mask, SYNOTIFY_MARK_REMOVE);
#else
	return -EOPNOTSUPP;
#endif /* MY_ABC_HERE */
}

SYSCALL_DEFINE3(SYNONotifyRemoveWatch, int, synotify_fd, const char __user *, pathname, __u64, mask)
{
#ifdef MY_ABC_HERE
	return synotify_set_mark(synotify_fd, pathname, mask, SYNOTIFY_MARK_REMOVE);
#else
	return -EOPNOTSUPP;
#endif /* MY_ABC_HERE */
}

SYSCALL_DEFINE3(syno_notify_add_watch, int, synotify_fd, const char __user *, pathname, __u64, mask)
{
#ifdef MY_ABC_HERE
	return synotify_set_mark(synotify_fd, pathname, mask, SYNOTIFY_MARK_ADD);
#else
	return -EOPNOTSUPP;
#endif /* MY_ABC_HERE */
}

SYSCALL_DEFINE3(SYNONotifyAddWatch, int, synotify_fd, const char __user *, pathname, __u64, mask)
{
#ifdef MY_ABC_HERE
	return synotify_set_mark(synotify_fd, pathname, mask, SYNOTIFY_MARK_ADD);
#else
	return -EOPNOTSUPP;
#endif /* MY_ABC_HERE */
}
#endif /* MY_ABC_HERE */
