#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#ifdef MY_ABC_HERE
#include <linux/fsnotify_backend.h>
#include <linux/path.h>
#include <linux/slab.h>
#include "../../mount.h"

/* flags used for synotify_set_mark() */
#define SYNOTIFY_MARK_ADD	0x00000001
#define SYNOTIFY_MARK_REMOVE	0x00000002

/*
 * Structure for normal synotify events. It gets allocated in
 * synotify_handle_event() and freed when the information is retrieved by
 * userspace
 */
struct synotify_event_info {
	struct fsnotify_event fse;
	u32 mask;
	u32 sync_cookie;
	/*
	 * We hold ref to this path so it may be dereferenced at any point
	 * during this object's lifetime
	 */
	struct path path;
	const char *full_path;
	size_t full_path_len;
	const char *file_path;
	bool path_ready;
	bool overflow_event;

	// v2 event
	pid_t pid;
	uid_t uid;
};

static inline struct synotify_event_info *SYNOTIFY_E(struct fsnotify_event *fse)
{
	return container_of(fse, struct synotify_event_info, fse);
}

struct synotify_event_info *synotify_alloc_event(struct fsnotify_group *group,
						u32 mask, const void *data, u32 cookie);
#endif /* MY_ABC_HERE */
