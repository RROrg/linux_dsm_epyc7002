#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/synolib.h>

atomic_t syno_disk_not_ready_count = ATOMIC_INIT(0);

DEFINE_MUTEX(syno_device_mutex);
LIST_HEAD(syno_device_not_ready_list);

void syno_device_not_ready_set(const char *device_name)
{
	struct syno_device_list *sdl = NULL;
	int device_in_list = false;

	if (!device_name) {
		return;
	}
	mutex_lock(&syno_device_mutex);
	list_for_each_entry(sdl, &syno_device_not_ready_list, device_list) {
		if (!strcmp(sdl->disk_name, device_name)) {
			device_in_list = true;
			break;
		}
	}

	if (!device_in_list) {
		sdl = kzalloc(sizeof(*sdl), GFP_KERNEL);
		snprintf(sdl->disk_name, DISK_NAME_LEN, "%s", device_name);
		list_add(&sdl->device_list, &syno_device_not_ready_list);
	}
	mutex_unlock(&syno_device_mutex);
}
EXPORT_SYMBOL(syno_device_not_ready_set);
void syno_device_not_ready_clear(const char *device_name)
{
	struct syno_device_list *sdl = NULL;

	if (!device_name) {
		return;
	}
	mutex_lock(&syno_device_mutex);
	list_for_each_entry(sdl, &syno_device_not_ready_list, device_list) {
		if (!strcmp(sdl->disk_name, device_name)) {
			list_del(&sdl->device_list);
			kfree(sdl);
			break;
		}
	}
	mutex_unlock(&syno_device_mutex);
}
EXPORT_SYMBOL(syno_device_not_ready_clear);

void syno_disk_not_ready_count_increase(void)
{
	atomic_inc(&syno_disk_not_ready_count);
}
EXPORT_SYMBOL(syno_disk_not_ready_count_increase);

void syno_disk_not_ready_count_decrease(void)
{
	/*
	 * the counter shouldn't be decreased to a negative number, so we have to
	 * warn about someone calling this function while counter is zero and stop
	 * decreaseing.
	 *
	 */
	WARN_ON_ONCE(!atomic_add_unless(&syno_disk_not_ready_count, -1, 0));
}
EXPORT_SYMBOL(syno_disk_not_ready_count_decrease);

/*
 * Return 0 if any of disks aren't ready and timeout isn't over.
 * Otherwise return 1.
 */
int syno_scsi_disk_ready_check(void)
{
	int ret = 0;

	if (0 == atomic_read(&syno_disk_not_ready_count) &&
		list_empty(&syno_device_not_ready_list)) {
		ret = 1;
	}

	return ret;
}

