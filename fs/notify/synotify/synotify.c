#include <linux/synotify.h>
#include <linux/fdtable.h>
#include <linux/fsnotify_backend.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h> /* UINT_MAX */
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/nsproxy.h>
#include <linux/mnt_namespace.h>
#include <linux/module.h>
#include <linux/sched/mm.h>

#include "synotify.h"

static struct kmem_cache *synotify_event_info_cachep = NULL;

static void path_append(char *parent_path, const char *child_name, int n)
{
	int i = strlen(parent_path);

	if (parent_path[i] != '/' && child_name[0] != '/' && n - 1 > i) {
		parent_path[i] = '/';
		parent_path[i + 1] = '\0';
		strncat(parent_path, child_name, n - i - 2);
	} else
		strncat(parent_path, child_name, n - i - 1);
}

/*
 * Fetch full mount point path,
 * It traverse vfsmount from down to up by following mnt_parent
 * @in: struct vfsmount: vfsmount structure, size_t buf_len: path buffer length
 * @out: mnt_full_path: full path of vfsmount struct
 * @return: < 0 : failed, 0 : success
 */
static int syno_fetch_mountpoint_fullpath(struct vfsmount *mnt, char *mnt_full_path, char *d_path_buf)
{
	int ret = -1;
	char *mnt_dentry_path = NULL;
	struct nsproxy *nsproxy = current->nsproxy;
	struct mnt_namespace *mnt_space = NULL;
	struct mount *root_mnt = NULL;
	struct path root_path;
	struct path mnt_path;
	struct task_struct *parent;


	if (!nsproxy) {
		/*
		 * When a process exits, CLOSE events will be sent asynchronously through
		 * do_exit -> exit_files -> ... -> delayed_fput ->  __fput -> fsnotify_close
		 *
		 * At this point it may be troublesome to access current's namespace. To
		 * work round it, parent's namespace is used instead because most of
		 * the time processes have the same namespaces as their parents.
		 */
		rcu_read_lock();
		parent = rcu_dereference(current->real_parent);

		if (parent && parent->nsproxy)
			mnt_space = parent->nsproxy->mnt_ns;
		rcu_read_unlock();
	} else
		mnt_space = nsproxy->mnt_ns;

	if (!mnt_space || !mnt_space->root)
		return -EINVAL;

	get_mnt_ns(mnt_space);

	root_mnt = mnt_space->root;
	memset(&root_path, 0, sizeof(struct path));
	root_path.mnt = &root_mnt->mnt;
	root_path.dentry = root_mnt->mnt.mnt_root;

	memset(&mnt_path, 0, sizeof(struct path));
	mnt_path.mnt = mnt;
	mnt_path.dentry = mnt->mnt_root;

	path_get(&mnt_path);
	path_get(&root_path);

	mnt_dentry_path = __d_path(&mnt_path, &root_path, d_path_buf, PATH_MAX - 1);
	if (IS_ERR_OR_NULL(mnt_dentry_path)) {
		ret = -ENOENT;
		goto RESOURCE_PUT;
	}

	path_append(mnt_full_path, mnt_dentry_path, PATH_MAX);

	ret = 0;

RESOURCE_PUT:
	path_put(&root_path);
	path_put(&mnt_path);
	put_mnt_ns(mnt_space);
	d_path_buf[0] = '\0';
	return ret;
}

static int synotify_fetch_path(struct fsnotify_event *fsnotify_event, struct fsnotify_group *group)
{
	struct synotify_event_info *event = SYNOTIFY_E(fsnotify_event);
	char *synotify_full_path_buf = NULL;
	char *synotify_d_path_buf = NULL;
	char *dentry_path = NULL;
	struct vfsmount *mnt = event->path.mnt;
	struct mem_cgroup *old_memcg;
	int ret = 0;

	if (unlikely(event->overflow_event))
		return 0;

	mutex_lock(&group->notification_mutex);
	synotify_full_path_buf = group->synotify_data.synotify_full_path_buf;
	synotify_d_path_buf = group->synotify_data.synotify_d_path_buf;
	synotify_full_path_buf[0] = '\0';
	synotify_d_path_buf[0] = '\0';

	ret = syno_fetch_mountpoint_fullpath(mnt, synotify_full_path_buf, synotify_d_path_buf);
	if (ret < 0)
		goto ERR;

	if (!event->file_path) {
		struct path root_path;
		root_path.mnt = mnt;
		root_path.dentry = mnt->mnt_root;
		dentry_path = __d_path(&event->path, &root_path, synotify_d_path_buf, PATH_MAX-1);
		if (unlikely(IS_ERR_OR_NULL(dentry_path))) {
			ret = -ENOENT;
			goto ERR;
		}
		path_append(synotify_full_path_buf, dentry_path, PATH_MAX);
	} else // From fsnotify_move()
		path_append(synotify_full_path_buf, event->file_path, PATH_MAX);

	/* Whoever is interested in the event, pays for the allocation. */
	old_memcg = set_active_memcg(group->memcg);
	event->full_path = kstrdup(synotify_full_path_buf, GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	set_active_memcg(old_memcg);
	if (unlikely(!event->full_path)) {
		printk_ratelimited(KERN_WARNING "synotify encountered ENOMEM in fetch_path\n");
		ret = -ENOMEM;
		goto ERR;
	}
	event->full_path_len = strlen(event->full_path);
	smp_wmb();  // make sure that no instruction reordering here
	event->path_ready = true;

ERR:
	synotify_full_path_buf[0] = '\0';
	synotify_d_path_buf[0] = '\0';
	mutex_unlock(&group->notification_mutex);
	return ret;
}

static bool should_merge(struct fsnotify_event *old_fsn, struct fsnotify_event *new_fsn)
{
	struct synotify_event_info *old, *new;

	pr_debug("%s: old=%p new=%p\n", __func__, old_fsn, new_fsn);
	old = SYNOTIFY_E(old_fsn);
	new = SYNOTIFY_E(new_fsn);

	if (old->mask != new->mask)
		return false;

	if ((new->mask & (FS_ATTRIB | FS_ACCESS | FS_MODIFY))
			&& (old->path.mnt == new->path.mnt)
			&& (old->path.dentry == new->path.dentry))
		return true;

	return false;
}

/* and the list better be locked by something too! */
static int synotify_merge(struct list_head *list,
					     struct fsnotify_event *event)
{
	struct fsnotify_event *last_event;
	pr_debug("%s: list=%p event=%p\n", __func__, list, event);

	last_event = list_entry(list->prev, struct fsnotify_event, list);
	return should_merge(last_event, event);
}

struct synotify_event_info *synotify_alloc_event(struct fsnotify_group *group,
	u32 mask, const void *data, u32 cookie)
{
	const struct path *path = fsnotify_data_path(data, FSNOTIFY_EVENT_PATH);
	struct synotify_event_info *event;
	struct mem_cgroup *old_memcg;

	if (unlikely(!synotify_event_info_cachep))
		return NULL;

	/* Whoever is interested in the event, pays for the allocation. */
	old_memcg = set_active_memcg(group->memcg);
	event = kmem_cache_alloc(synotify_event_info_cachep,
		GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	set_active_memcg(old_memcg);
	if (!event)
		return NULL;

	fsnotify_init_event(&event->fse, 0);
	event->mask = mask;
	if (path) {
		event->path = *path;
		path_get(&event->path);
	} else {
		event->path.mnt = NULL;
		event->path.dentry = NULL;
	}
	event->full_path = NULL;
	event->full_path_len = 0;
	event->sync_cookie = cookie;
	event->file_path = NULL;
	event->path_ready = false;
	event->overflow_event = false;

	return event;
}

static int synotify_handle_event(struct fsnotify_group *group, u32 mask,
				 const void *data, int data_type,
				 struct inode *dir,
				 const struct qstr *file_path, u32 cookie,
				 struct fsnotify_iter_info *iter_info)
{
	int ret = 0;
	struct synotify_event_info *event;
	struct fsnotify_event *fsn_event;

	BUILD_BUG_ON(SYNO_ACCESS != FS_ACCESS);
	BUILD_BUG_ON(SYNO_MODIFY != FS_MODIFY);
	BUILD_BUG_ON(SYNO_ATTRIB != FS_ATTRIB);
	BUILD_BUG_ON(SYNO_CLOSE_NOWRITE != FS_CLOSE_NOWRITE);
	BUILD_BUG_ON(SYNO_CLOSE_WRITE != FS_CLOSE_WRITE);
	BUILD_BUG_ON(SYNO_OPEN != FS_OPEN);
	BUILD_BUG_ON(SYNO_MOVE_TO != FS_MOVED_TO);
	BUILD_BUG_ON(SYNO_MOVE_FROM != FS_MOVED_FROM);
	BUILD_BUG_ON(SYNO_CREATE != FS_CREATE);
	BUILD_BUG_ON(SYNO_DELETE != FS_DELETE);
	BUILD_BUG_ON(SYNO_Q_OVERFLOW != FS_Q_OVERFLOW);
	BUILD_BUG_ON(SYNO_ONDIR != FS_ISDIR);

	pr_debug("%s: group=%p mask=%x\n", __func__, group, mask);

	if (data_type != FSNOTIFY_EVENT_PATH && data_type != FSNOTIFY_EVENT_SYNO_MOVE)
		return 0;

	event = synotify_alloc_event(group, mask, data, cookie);
	if (unlikely(!event)) {
		printk_ratelimited(KERN_WARNING "synotify encountered ENOMEM in alloc_event\n");
		return -ENOMEM;
	}

	if (file_path && data_type == FSNOTIFY_EVENT_SYNO_MOVE)
		event->file_path = file_path->name;
	fsn_event = &event->fse;

	// v2 event
	event->pid = task_tgid_nr_ns(current, &init_pid_ns);
	event->uid = from_kuid_munged(&init_user_ns, current_uid());

	ret = fsnotify_add_event(group, fsn_event, synotify_merge);
	if (ret) {
		fsnotify_destroy_event(group, fsn_event);
		return ret > 0 ? 0 : ret;
	}

	return 0;
}

static void synotify_free_group_priv(struct fsnotify_group *group)
{
	struct user_struct *user;

	user = group->synotify_data.user;
	if (user) {
		atomic_dec(&user->synotify_instances);
		free_uid(user);
	}

	kfree(group->synotify_data.synotify_full_path_buf);
	kfree(group->synotify_data.synotify_d_path_buf);
}

static void synotify_free_event(struct fsnotify_event *fsn_event)
{
	struct synotify_event_info *event;

	event = SYNOTIFY_E(fsn_event);

	if (event->overflow_event) {
		// allocated in synotify_alloc_overflow_event()
		kfree(event);
	} else {
		// allocated in synotify_alloc_event()
		path_put(&event->path);
		kfree(event->full_path);
		kmem_cache_free(synotify_event_info_cachep, event);
	}
}

static void synotify_free_mark(struct fsnotify_mark *fsn_mark)
{
	kfree(fsn_mark);
}

const struct fsnotify_ops synotify_fsnotify_ops = {
	.handle_event = synotify_handle_event,
	.free_group_priv = synotify_free_group_priv,
	.free_event = synotify_free_event,
	.free_mark = synotify_free_mark,
	.fetch_path = synotify_fetch_path,
};

static int __init synotify_setup(void)
{
	synotify_event_info_cachep = kmem_cache_create("synotify_event_info",
		sizeof(struct synotify_event_info), 0, SLAB_MEM_SPREAD, NULL);
	if (!synotify_event_info_cachep)
		printk(KERN_ERR "synotify failed to kmem_cache_create synotify_event_info, disable synotify!\n");

	return 0;
}

static void __exit exit_synotify(void)
{
	kmem_cache_destroy(synotify_event_info_cachep);
}

device_initcall(synotify_setup);
module_exit(exit_synotify)
