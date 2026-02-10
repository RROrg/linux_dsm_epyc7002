#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Realtek RPC driver
 *
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <soc/realtek/avcpu.h>
#include <soc/realtek/kernel-rpc.h>
#include <soc/realtek/rtk_chip.h>
#include <soc/realtek/rtk_ipc_shm.h>
#include <soc/realtek/uapi/ion_rtk.h>
#include <soc/realtek/uapi/ion.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rtk_rpc.h>

#include "rtk_rpc.h"
#include "mem_allocator/ion.h"

#define SHOW_TASKS_ON_DEBUGFS

EXPORT_TRACEPOINT_SYMBOL_GPL(rtk_rpc_peek_rpc_request);
EXPORT_TRACEPOINT_SYMBOL_GPL(rtk_rpc_peek_rpc_reply);

void __iomem *rpc_int_base;
void __iomem *rpc_acpu_int_flag;
EXPORT_SYMBOL(rpc_acpu_int_flag);
void __iomem *rpc_vcpu_int_flag;
EXPORT_SYMBOL(rpc_vcpu_int_flag);

#ifdef CONFIG_REALTEK_RPC_VE3
void __iomem *rpc_ve3_int_flag;
struct regmap *rpc_ve3_base;
#endif

struct refclk_device *refclk;
struct device *rpc_dev;
EXPORT_SYMBOL(rpc_dev);

int chip_id;
EXPORT_SYMBOL(chip_id);

#ifdef CONFIG_FB_RTK
extern spinlock_t gASLock;
extern void dc_irq_handler(void);
#else
static DEFINE_SPINLOCK(gASLock);
#endif /* CONFIG_FB_RTK */


static int rpc_major;
static int rpc_acpu_irq;
static int rpc_vcpu_irq;
#if defined(MY_ABC_HERE)
#else /* MY_ABC_HERE */
static int rpc_ve3_irq;
#endif /* MY_ABC_HERE */


#ifdef SHOW_TASKS_ON_DEBUGFS
static int rpc_debug_node_show(struct seq_file *s, void *unused)
{
	int i;
	RPC_DEV_EXTRA *extra = (RPC_DEV_EXTRA *)s->private;
	RPC_DEV *dev = extra->dev;
	RPC_PROCESS *curr = (RPC_PROCESS *)extra->currProc;
	RPC_HANDLER *handler;
	RPC_PROCESS *proc;
	RPC_THREAD *thread;
	struct task_struct *task;

	seq_printf(s, "name: %s\n", extra->name);
	seq_printf(s, "currProc: %d\n", curr ? curr->pid : 0);
	seq_printf(s, "nextRpc: %x\n", extra->nextRpc);
	seq_printf(s, "RingBuf: %x\n", dev->ringBuf);
	seq_printf(s, "RingStart: %x\n", dev->ringStart);
	seq_printf(s, "RingIn: %x\n", dev->ringIn);
	seq_printf(s, "RingOut: %x\n", dev->ringOut);
	seq_printf(s, "RingEnd: %x\n", dev->ringEnd);

	seq_printf(s, "\nRingBuffer:\n");
	for (i = 0; i < RPC_RING_SIZE; i += 16) {
		uint32_t *addr = (uint32_t *)(AVCPU2SCPU(dev->ringStart) + i);
		seq_printf(s, "%x: %08x %08x %08x %08x\n",
			   dev->ringStart + i,
			   ntohl(*(addr + 0)),
			   ntohl(*(addr + 1)),
			   ntohl(*(addr + 2)),
			   ntohl(*(addr + 3)));
	}

	spin_lock_bh(&extra->lock);
	list_for_each_entry(proc, &extra->tasks, list) {
		task = find_task_by_vpid(proc->pid);

		seq_printf(s, "\nProcess:%s\n", task ? task->comm : "N/A");

		list_for_each_entry(handler, &proc->handlers, list) {
			seq_printf(s, "\tprogramID:%u\n", handler->programID);
		}

		seq_printf(s, "\tp:%d\n", proc->pid);

		list_for_each_entry(thread, &proc->threads, list) {
			seq_printf(s, "\tt:%d\n", thread->pid);
		}
	}
	spin_unlock_bh(&extra->lock);
	return 0;
}

static int rpc_debug_node_open(struct inode *inode, struct file *file)
{
	return single_open(file, rpc_debug_node_show, inode->i_private);
}

static const struct file_operations rpc_debug_node_ops = {
	.open =		rpc_debug_node_open,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	single_release,
};
#endif /* SHOW_TASKS_ON_DEBUGFS */

struct file_operations *rpc_fop_array[] = {
	&rpc_poll_fops, /* poll */
	&rpc_intr_fops /* intr */
};

void **rpc_data_ptr_array[] = {
	(void **) &rpc_poll_devices, /* poll */
	(void **) &rpc_intr_devices /* intr */
};

int rpc_data_size_array[] = {
	sizeof(RPC_DEV), /* poll */
	sizeof(RPC_DEV) /* intr */
};

/*
 * Finally, the module stuff
 */
static struct class *rpc_class;

void rpc_set_ir_wakeup_key(uint32_t uScancode, uint32_t uScancode_msk)
{
	struct rtk_ipc_shm __iomem *ipc = (void __iomem *)IPC_SHM_VIRT;

	dev_info(rpc_dev, "uScancode = 0x%x , uScancode_msk = 0x%x ", uScancode,
		uScancode_msk);
	/* audio RPC flag for scancode mask */
	writel(__cpu_to_be32(uScancode_msk), &(ipc->ir_scancode_mask));
	/* audio RPC flag for scancode key */
	writel(__cpu_to_be32(uScancode), &(ipc->ir_wakeup_scancode));
}

void rpc_set_flag(int type, uint32_t flag)
{
	struct rtk_ipc_shm __iomem *ipc = (void __iomem *) IPC_SHM_VIRT;

	/* audio RPC flag */
	if (type == RPC_AUDIO)
		writel(__cpu_to_be32(flag), &(ipc->audio_rpc_flag));

	/* video RPC flag */
	if (type == RPC_VIDEO)
		writel(__cpu_to_be32(flag), &(ipc->video_rpc_flag));

#ifdef CONFIG_REALTEK_RPC_VE3
	if (type == RPC_VE3)
		writel(__cpu_to_be32(flag), &(ipc->ve3_rpc_flag));
#endif
}

uint32_t rpc_get_flag(int type)
{
	struct rtk_ipc_shm __iomem *ipc = (void __iomem *)IPC_SHM_VIRT;

	if (type == RPC_AUDIO) {
		return __be32_to_cpu(readl(&(ipc->audio_rpc_flag)));
	}

	if (type == RPC_VIDEO)
		return __be32_to_cpu(readl(&(ipc->video_rpc_flag)));

#ifdef CONFIG_REALTEK_RPC_VE3
	if (type == RPC_VE3)
		return __be32_to_cpu(readl(&(ipc->ve3_rpc_flag)));
#endif

	dev_err(rpc_dev, "rpc_get_flag type error!\n");

	return 0xdeaddead;
}

void rpc_send_interrupt(int type)
{
	switch (type) {
	case RPC_AUDIO:
		if (rpc_acpu_int_flag != NULL && RPC_HAS_BIT(rpc_acpu_int_flag, AUDIO_RPC_SET_NOTIFY)) {
			spin_lock_irq(&gASLock);
			RPC_SET_BIT(rpc_acpu_int_flag, AUDIO_RPC_FEEDBACK_NOTIFY);
			spin_unlock_irq(&gASLock);
		}
		dev_dbg(rpc_dev, "send audio interrupt\n");
		writel_relaxed((RPC_INT_SA | RPC_INT_WRITE_1), rpc_int_base + RPC_SB2_INT);
		break;
	case RPC_VIDEO:

		if (rpc_vcpu_int_flag != NULL && RPC_HAS_BIT(rpc_vcpu_int_flag, VIDEO_RPC_SET_NOTIFY))
			RPC_SET_BIT(rpc_vcpu_int_flag, VIDEO_RPC_FEEDBACK_NOTIFY);

		dev_dbg(rpc_dev, "send video interrupt\n");
		writel_relaxed((RPC_INT_SV | RPC_INT_WRITE_1), rpc_int_base + RPC_SB2_INT);

		break;
#ifdef CONFIG_REALTEK_RPC_VE3
	case RPC_VE3:
		if (rpc_ve3_int_flag != NULL && RPC_HAS_BIT(rpc_ve3_int_flag, VE3_RPC_SET_NOTIFY))
			RPC_SET_BIT(rpc_ve3_int_flag, VE3_RPC_FEEDBACK_NOTIFY);

		dev_dbg(rpc_dev, "send ve3 interrupt\n");
		regmap_write(rpc_ve3_base, 0x78, RPC_INT_SVE3);

		break;
#endif
	default:
		break;
	}

}
EXPORT_SYMBOL(rpc_send_interrupt);

#ifdef MY_COPY
int my_copy_to_user(int *des, int *src, int size)
{
	char buf[256];
	int count = size;
	void *pSrc = (void *)src;
	int ret = 0;
	int i = 0;

	if (size > 256) {
		BUG();
	}

	while (size >= 4) {
		*(int *)&buf[i] = __raw_readl(pSrc);
		i += 4;
		pSrc += 4;
		size -= 4;
	}

	while (size > 0) {
		buf[i] = __raw_readb(pSrc);
		i++;
		pSrc++;
		size--;
	}

	ret = copy_to_user((int *)des, (int *)buf, count);

	return ret;
}

int my_copy_from_user(volatile void __iomem *des, const void *src, int size)
{

	char buf[256];
	int ret = 0;
	int i = 0;
	volatile char *cdes;

	if (size > 256) {
		BUG();
	}

	dev_dbg(rpc_dev, "(des:%p, src:%p, size:%d) pid:%d tid:%d comm:%s\n",
			 des, src, size, current->tgid, current->pid, current->comm);

	ret = copy_from_user((unsigned int *) buf, (unsigned int __user *) src, size);

	if (ret != 0)
		dev_err(rpc_dev, "copy_from_user error: %d bytes\n", ret);

	cdes = (char *)des;
	for (i = 0 ; i < size ; i++)
		cdes[i] = buf[i];

	return 0;
}


int my_copy_user(int *des, int *src, int size)
{
	char *csrc, *cdes;
	int i;

	dev_dbg(rpc_dev, "(des:%p, src:%p, size:%d) pid:%d tid:%d comm:%s\n",
			 des, src, size, current->tgid, current->pid, current->comm);

	might_fault();

	if ((unsigned long)des < 0xc0000000 &&
	    access_ok(des, size) == 0)
		BUG();

	if ((unsigned long)src < 0xc0000000 &&
	    access_ok(src, size) == 0)
		BUG();

	if (((unsigned long)src & 0x3) || ((unsigned long)des & 0x3))
		dev_warn(rpc_dev, "my_copy_user: unaligned happen...\n");

	while (size >= 4) {
		*des++ = *src++;
		size -= 4;
	}

	csrc = (char *)src;
	cdes = (char *)des;

	for (i = 0 ; i < size ; i++)
		cdes[i] = csrc[i];

	return 0;
}
#endif /* MY_COPY */

irqreturn_t rpc_isr(int irq, void *dev_id)
{
	int itr;

	itr = readl_relaxed(rpc_int_base + RPC_SB2_INT_ST);

	if (RPC_HAS_BIT(rpc_acpu_int_flag, RPC_AUDIO_FEEDBACK_NOTIFY)) {
		RPC_RESET_BIT(rpc_acpu_int_flag, RPC_AUDIO_FEEDBACK_NOTIFY);
	} else if (RPC_HAS_BIT(rpc_vcpu_int_flag, RPC_VIDEO_FEEDBACK_NOTIFY)) {
		RPC_RESET_BIT(rpc_vcpu_int_flag, RPC_VIDEO_FEEDBACK_NOTIFY);
	} else {
		/* to clear interrupt, set bit[0] to 0 then we can clear A2S int */
		if (itr & (1 << 1))
			writel_relaxed(1 << 1, rpc_int_base + RPC_SB2_INT_ST);
		if (itr & (RPC_INT_VS))
			writel_relaxed(RPC_INT_VS, rpc_int_base + RPC_SB2_INT_ST);

		return IRQ_HANDLED;
	}

	while ((itr & 1 << 1) || (itr & RPC_INT_VS)) {
		if (itr & 1 << 1) {
			/* to clear interrupt, set bit[0] to 0 then we can clear A2S int */
			writel_relaxed(1 << 1, rpc_int_base + RPC_SB2_INT_ST);

			if (rpc_intr_devices[RPC_INTR_DEV_AS_ID1].ringIn !=
				rpc_intr_devices[RPC_INTR_DEV_AS_ID1].ringOut) {
				tasklet_schedule(&(rpc_intr_extra[RPC_INTR_DEV_AS_ID1].tasklet));
			}

			if (rpc_kern_devices[RPC_KERN_DEV_AS_ID1].ringIn !=
				rpc_kern_devices[RPC_KERN_DEV_AS_ID1].ringOut) {
				wake_up_interruptible(&(rpc_kern_devices[RPC_KERN_DEV_AS_ID1].ptrSync->waitQueue));
			}
		}

		if (itr & RPC_INT_VS) {
			/* to clear interrupt, set bit[0] to 0 then we can clear A2S int */
			writel_relaxed(RPC_INT_VS, rpc_int_base + RPC_SB2_INT_ST);

			if (rpc_intr_devices[RPC_INTR_DEV_V1S_ID3].ringIn !=
				rpc_intr_devices[RPC_INTR_DEV_V1S_ID3].ringOut) {
				tasklet_schedule(&(rpc_intr_extra[RPC_INTR_DEV_V1S_ID3].tasklet));
			}

			if (rpc_kern_devices[RPC_KERN_DEV_V1S_ID3].ringIn !=
				rpc_kern_devices[RPC_KERN_DEV_V1S_ID3].ringOut) {
				wake_up_interruptible(&(rpc_kern_devices[RPC_KERN_DEV_V1S_ID3].ptrSync->waitQueue));
			}
		}
		itr = readl_relaxed(rpc_int_base + RPC_SB2_INT_ST);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_REALTEK_RPC_VE3
irqreturn_t rpc_ve3_isr(int irq, void *dev_id)
{
	int itr;

	regmap_read(rpc_ve3_base, 0x88, &itr);

	if (RPC_HAS_BIT(rpc_ve3_int_flag, RPC_VE3_FEEDBACK_NOTIFY)) {
		RPC_RESET_BIT(rpc_ve3_int_flag, RPC_VE3_FEEDBACK_NOTIFY);
	} else {
		/* to clear interrupt, set bit[0] to 0 then we can clear A2S int */
		if (itr & RPC_INT_VE3S_ST) {
			regmap_write(rpc_ve3_base, 0x88, itr & (~RPC_INT_VE3S_ST));
		}
		return IRQ_HANDLED;
	}

	while (itr & RPC_INT_VE3S_ST) {

		/* to clear interrupt, set bit[0] to 0 then we can clear A2S int */
		regmap_write(rpc_ve3_base, 0x88, itr & (~RPC_INT_VE3S_ST));

		if (rpc_intr_ve3_devices[RPC_INTR_DEV_VE3S_ID1].ringIn !=
			rpc_intr_ve3_devices[RPC_INTR_DEV_VE3S_ID1].ringOut) {
			tasklet_schedule(&(rpc_intr_extra[RPC_INTR_DEV_VE3S_ID1 + RPC_INTR_DEV_TOTAL].tasklet));
		}

		if (rpc_kern_ve3_devices[RPC_KERN_DEV_VE3S_ID1].ringIn !=
			rpc_kern_ve3_devices[RPC_KERN_DEV_VE3S_ID1].ringOut) {
			wake_up_interruptible(&(rpc_kern_ve3_devices[RPC_KERN_DEV_AS_ID1].ptrSync->waitQueue));
		}

		regmap_read(rpc_ve3_base, 0x88, &itr);
	}

	return IRQ_HANDLED;
}
#endif

static char *rpc_devnode(struct device *dev, umode_t *mode)
{
	*mode = 0666;
	return NULL;
}

static ssize_t kernel_remote_allocate_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 1);
}

static CLASS_ATTR_RO(kernel_remote_allocate);

static int rpc_interrupt_init(struct device_node *np)
{
	int max_count = 5000;
	int wait_time = 0;
	int ret = -1;

	spin_lock_irq(&gASLock);
	RPC_SET_BIT(rpc_acpu_int_flag, RPC_AUDIO_SET_NOTIFY);
	spin_unlock_irq(&gASLock);

	RPC_SET_BIT(rpc_vcpu_int_flag, RPC_VIDEO_SET_NOTIFY);

#ifdef CONFIG_REALTEK_RPC_VE3
	if (chip_id == CHIP_ID_RTD1619B) {
		RPC_SET_BIT(rpc_ve3_int_flag, RPC_VE3_SET_NOTIFY);
	}
#endif

	writel_relaxed(RPC_INT_SA | RPC_INT_SV | RPC_INT_WRITE_1,
		       rpc_int_base + RPC_SB2_INT_EN);

	rpc_acpu_irq = irq_of_parse_and_map(np, 0);
	if (WARN_ON(!rpc_acpu_irq))
		dev_warn(rpc_dev, "can not parse ACPU irq\n");

	dev_info(rpc_dev, "rpc_int_base: %p\n", rpc_int_base);
	dev_info(rpc_dev, "acpu irq: %d\n", rpc_acpu_irq);

	ret = request_irq(rpc_acpu_irq,
			  rpc_isr, IRQF_SHARED | IRQF_NO_SUSPEND,
			  "a_rpc",
			  (void *) RPC_ID);
	if (ret) {
		dev_err(rpc_dev, "register acpu irq handler failed\n");
		goto exit;
	}

	/* Enable the interrupt from system to audio & video */
	rpc_send_interrupt(RPC_AUDIO);
	rpc_set_flag(RPC_AUDIO, 0xffffffff);

	rpc_vcpu_irq = irq_of_parse_and_map(np, 1);
	if (WARN_ON(!rpc_vcpu_irq))
		dev_warn(rpc_dev, "can not parse VCPU irq\n");

	dev_info(rpc_dev, "vcpu irq: %d\n", rpc_vcpu_irq);

	ret = request_irq(rpc_vcpu_irq,
			rpc_isr, IRQF_SHARED | IRQF_NO_SUSPEND,
			"v_rpc",
			(void *)RPC_ID);
	if (ret) {
		dev_err(rpc_dev, "register vcpu irq handler failed\n");
		goto exit;
	}

	rpc_send_interrupt(RPC_VIDEO);
	rpc_set_flag(RPC_VIDEO, 0xffffffff);

	dev_warn(rpc_dev, "wait vcpu ready");

	while ((rpc_get_flag(RPC_VIDEO) == 0xffffffff) && ((max_count--) > 0)) {
		mdelay(1);
		if ((++wait_time) == 10)
			wait_time = 0;
	}

	while ((--wait_time) > 0)
		dev_warn(rpc_dev, ".");

	dev_warn(rpc_dev, "%s (RPC_VIDEO FLAG = 0x%08x)\n",
		(max_count > 0) ? "OK" : "timeout", rpc_get_flag(RPC_VIDEO));

#ifdef CONFIG_REALTEK_RPC_VE3
	if (chip_id == CHIP_ID_RTD1619B) {
		max_count = 5000;
		rpc_ve3_irq = irq_of_parse_and_map(np, 2);
		if (WARN_ON(!rpc_ve3_irq))
			dev_warn(rpc_dev, "can not parse VE3 irq\n");

		dev_info(rpc_dev, "ve3 irq: %d\n", rpc_ve3_irq);

		ret = request_irq(rpc_ve3_irq,
				rpc_ve3_isr, IRQF_SHARED | IRQF_NO_SUSPEND,
				"ve3_rpc",
				(void *)RPC_ID);
		if (ret) {
			dev_err(rpc_dev, "register ve3 irq handler failed\n");
			goto exit;
		}
		rpc_send_interrupt(RPC_VE3);
		rpc_set_flag(RPC_VE3, 0xffffffff);

		dev_warn(rpc_dev, "wait ve3 ready");

		while ((rpc_get_flag(RPC_VE3) == 0xffffffff) && ((max_count--) > 0)) {
			mdelay(1);
			if ((++wait_time) == 10)
				wait_time = 0;
		}

		while ((--wait_time) > 0)
			dev_warn(rpc_dev, ".");

		dev_warn(rpc_dev, "%s (RPC_VE3 FLAG = 0x%08x)\n",
			(max_count > 0) ? "OK" : "timeout", rpc_get_flag(RPC_VE3));
	}
#endif
exit:
	return ret;
}

static int rpc_fs_init(void)
{
	char buf[16];
	int ret = -1;
	struct device *dev;
	int i = 0;
	int node_num;
	RPC_DEV_EXTRA *extra;

#ifdef SHOW_TASKS_ON_DEBUGFS
	struct dentry *rpcnode;
	struct dentry *rpcroot;
#endif /* SHOW_TASKS_ON_DEBUGFS */

	/* register rpc_poll_fops as default file operation */
	rpc_major = RPC_MAJOR;
	ret = register_chrdev(rpc_major, "realtek-rpc", &rpc_poll_fops);
	if (ret < 0) {
		dev_dbg(rpc_dev, "can not get major %d\n", rpc_major);
		goto exit;
	}

	if (rpc_major == 0)
		rpc_major = ret; /* dynamic */

	dev_dbg(rpc_dev, "rpc major number: %d\n", rpc_major);

	rpc_class = class_create(THIS_MODULE, "rpc");
	if (IS_ERR(rpc_class)) {
		ret = PTR_ERR(rpc_class);
		goto exit;
	}

	rpc_class->devnode = rpc_devnode;

	ret = class_create_file(rpc_class, &class_attr_kernel_remote_allocate);
	if (ret) {
		dev_err(rpc_dev, "create class file failed\n");
		ret = -EINVAL;
		goto exit;
	}

#ifdef SHOW_TASKS_ON_DEBUGFS
	rpcroot = debugfs_create_dir("rpc", NULL);
#endif /* SHOW_TASKS_ON_DEBUGFS */

	if (chip_id == CHIP_ID_RTD1619B)
		node_num = RPC_NR_DEVS + RPC_INTR_VE3_DEV_TOTAL;
	else
		node_num = RPC_NR_DEVS;

	for (i = 0; i < node_num; i++) {
		if (i >= RPC_NR_DEVS) {
			if (i % RPC_NR_PAIR == 0)
				extra = &rpc_intr_extra[i / RPC_NR_PAIR];
			else
				extra = &rpc_intr_extra[i / RPC_NR_PAIR + 1];
		} else {
			extra = ((i % RPC_NR_PAIR) == 0) ?
			&rpc_poll_extra[i / RPC_NR_PAIR] :
			&rpc_intr_extra[i / RPC_NR_PAIR];
		}

		dev = device_create(rpc_class,
				    NULL,
				    MKDEV(rpc_major, i),
				    NULL,
				    "rpc%d",
				    i);

#ifdef SHOW_TASKS_ON_SYSFS
		device_create_file(dev, &dev_attr_tasks);
#endif /* SHOW_TASKS_ON_SYSFS */

#ifdef SHOW_TASKS_ON_DEBUGFS
		sprintf(buf, "rpc%d", i);
		rpcnode = debugfs_create_file(buf,
					      0444,
					      rpcroot,
					      extra,
					      &rpc_debug_node_ops);
#endif /* SHOW_TASKS_ON_DEBUGFS */

		extra->sdev = dev;
		dev_set_drvdata(dev, extra);
	}

	device_create(rpc_class, NULL, MKDEV(rpc_major, 100), NULL, "rpc100");
exit:
	return ret;
}

static int rtk_rpc_probe(struct platform_device *pdev)
{
	int ret = -1;
	struct device_node *np = pdev->dev.of_node;
	struct rtk_ipc_shm __iomem *ipc = (void __iomem *) IPC_SHM_VIRT;
#if defined(MY_ABC_HERE)
#else /* MY_ABC_HERE */
	struct device_node *syscon_np;
#endif /* MY_ABC_HERE */

	rpc_dev = &pdev->dev;
	chip_id = get_rtd_chip_id();

	dev_info(rpc_dev, "enter %s\n", __func__);

	if (WARN_ON(!np))
		dev_err(rpc_dev, "can not found device node\n");

	rpc_int_base = of_iomap(np, 0);
	if (WARN_ON(!rpc_int_base)) {
		dev_warn(rpc_dev, "can not map regnsters for %s\n", np->name);
		goto exit;
	}

	refclk = of_refclk_get(np, 0);
	if (IS_ERR(refclk)) {
		ret = PTR_ERR(refclk);
		if (ret == -EPROBE_DEFER) {
			dev_dbg(rpc_dev, "refclk not ready, retry\n");
			goto exit;
		} else {
			dev_err(rpc_dev, "failed to get refclk: %d\n", ret);
			goto exit;
		}
	}

	rpc_acpu_int_flag = (void __iomem *)&ipc->vo_int_sync;
	/* todo: use chipinfo replace it */

	rpc_vcpu_int_flag = (void __iomem *)&ipc->video_int_sync;

#ifdef CONFIG_REALTEK_RPC_VE3
	if (chip_id == CHIP_ID_RTD1619B) {
		rpc_ve3_int_flag = (void __iomem *)&ipc->ve3_int_sync;
		syscon_np = of_parse_phandle(np, "syscon", 0);
		if (IS_ERR_OR_NULL(syscon_np))
			return -ENODEV;

		rpc_ve3_base = syscon_node_to_regmap(syscon_np);
		if (IS_ERR_OR_NULL(rpc_ve3_base)) {
			of_node_put(syscon_np);
			return -EINVAL;
		}
	}
#endif

	ret = rpc_poll_init();
	ret = rpc_intr_init();
	ret = rpc_kern_init();
	ret = rpc_interrupt_init(np);
	ret = rpc_fs_init();

	dev_info(rpc_dev, "exit %s\n", __func__);
exit:
	return ret;
}

static int __maybe_unused rtk_rpc_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_PM
/*
 * Disable the interrupt from system to audio & video
 */
static int rtk_rpc_pm_suspend(struct device *dev)
{
	int max_count = 500;

#ifdef CONFIG_RTK_XEN_SUPPORT
	if (xen_domain() && !xen_initial_domain()) {
		dev_info(dev, "skip %s in DomU\n");
		return 0;
	}
#endif
	dev_info(dev, "enter %s\n", __func__);

	rpc_set_flag(RPC_AUDIO, 0xdaedffff); /* STOP AUDIO HAS_CHECK */
	while ((rpc_get_flag(RPC_AUDIO) != 0x0) && (max_count > 0)) {
		mdelay(1);
		max_count--;
	}

	RPC_RESET_BIT(rpc_acpu_int_flag, RPC_AUDIO_SET_NOTIFY); /* Disable Interrupt */

	wmb();

	rpc_set_flag(RPC_AUDIO, 0xdeadffff); /* WAIT AUDIO RPC SUSPEND READY */
	while ((rpc_get_flag(RPC_AUDIO) != 0x0) && (max_count > 0)) {
		mdelay(1);
		max_count--;
	}

	dev_info(dev, "wait %d ms\n", (500 - max_count));


	dev_info(dev, "exit %s\n", __func__);

	return 0;
}

static void rtk_rpc_pm_shutdown(struct platform_device *pdev)
{
	int max_count = 500;
	struct device *dev = &pdev->dev;

	dev_info(dev, "enter %s\n", __func__);

	rpc_set_flag(RPC_AUDIO, 0xdaedffff); /* STOP AUDIO HAS_CHECK */
	while ((rpc_get_flag(RPC_AUDIO) != 0x0) && (max_count > 0)) {
		mdelay(1);
		max_count--;
	}


	rpc_set_flag(RPC_VIDEO, 0xdaedffff); /* STOP VIDEO HAS_CHECK */
	while ((rpc_get_flag(RPC_VIDEO) != 0x0) && (max_count > 0)) {
		mdelay(1);
		max_count--;
	}


	/* disable Interrupt */
	RPC_RESET_BIT(rpc_acpu_int_flag, RPC_AUDIO_SET_NOTIFY);

	/* disable interrupt */
	RPC_RESET_BIT(rpc_vcpu_int_flag, RPC_VIDEO_SET_NOTIFY);

	wmb();

	rpc_set_flag(RPC_AUDIO, 0xdeadffff); /* WAIT AUDIO RPC SUSPEND READY */
	while ((rpc_get_flag(RPC_AUDIO) != 0x0) && (max_count > 0)) {
		mdelay(1);
		max_count--;
	}

	rpc_set_flag(RPC_VIDEO, 0xdaedffff); /* STOP VIDEO HAS_CHECK */
	while ((rpc_get_flag(RPC_VIDEO) != 0x0) && (max_count > 0)) {
		mdelay(1);
		max_count--;
	}

	dev_info(dev, "wait %d ms\n", (500 - max_count));
	dev_info(dev, "Exit %s\n", __func__);
}

/*
 * Enable the interrupt from system to audio & video
 */
static int rtk_rpc_pm_resume(struct device *dev)
{
#ifdef CONFIG_RTK_XEN_SUPPORT
	if (xen_domain() && !xen_initial_domain()) {
		dev_info(dev, "skip %s in DomU\n", __func__);
		return 0;
	}
#endif

	dev_info(dev, "enter %s\n", __func__);

	RPC_SET_BIT(rpc_acpu_int_flag, RPC_AUDIO_SET_NOTIFY);
	rpc_set_flag(RPC_AUDIO, 0xffffffff);

	dev_info(dev, "exit %s\n", __func__);

	return 0;
}

static const struct dev_pm_ops rtk_rpc_pm_ops = {
	.suspend_late = rtk_rpc_pm_suspend,
	.resume_early = rtk_rpc_pm_resume,
	.poweroff = rtk_rpc_pm_suspend,
#ifdef CONFIG_HIBERNATION
	.freeze = rtk_rpc_pm_suspend,
	.thaw = rtk_rpc_pm_resume,
	.restore = rtk_rpc_pm_resume,
#endif
};
#endif /* CONFIG_PM */

static struct of_device_id rtk_rpc_ids[] = {
	{.compatible = "realtek,rpc" },
	{/* Sentinel */ },
};

static struct platform_driver rtk_rpc_driver = {
	.probe = rtk_rpc_probe,
	.remove = rtk_rpc_remove,
#ifdef CONFIG_PM
	.shutdown = rtk_rpc_pm_shutdown,
#endif /* CONFIG_PM */
	.driver = {
		.name = "realtek-rpc",
		.bus = &platform_bus_type,
		.of_match_table = rtk_rpc_ids,
#ifdef CONFIG_PM
		.pm = &rtk_rpc_pm_ops,
#endif /* CONFIG_PM */
	},
};

static int rtk_rpc_init(void)
{
	return platform_driver_register(&rtk_rpc_driver);
}
device_initcall(rtk_rpc_init);
