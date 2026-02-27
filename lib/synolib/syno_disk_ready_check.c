#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/synolib.h>

atomic_t syno_disk_not_ready_count = ATOMIC_INIT(0);

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

	if (0 == atomic_read(&syno_disk_not_ready_count)) {
		ret = 1;
	}

	return ret;
}

