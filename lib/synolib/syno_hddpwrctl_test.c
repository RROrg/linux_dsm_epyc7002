#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#include <linux/init.h>
#include <linux/module.h>
#include <linux/libata.h>
#include <linux/synolib.h>

static struct kobject *SynoHddPwrCtlObject = NULL;
int iSlot = 0;

extern int SYNO_SUPPORT_HDD_DYNAMIC_ENABLE_POWER(int index);
extern int SYNO_CTRL_HDD_POWERON(int index, int value);
extern int SYNO_CHECK_HDD_ENABLE(int index);
extern int SYNO_CHECK_HDD_DETECT(int index);
#ifdef MY_ABC_HERE
extern int gSynoInternalHddNumber;
#endif /* MY_ABC_HERE */

static ssize_t pwrctl_slot_show (struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf (buf, PAGE_SIZE, "%d\n", iSlot);
}
static ssize_t pwrctl_slot_store (struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    if (0 > kstrtoint(buf, 0, &iSlot)) {
		printk(KERN_WARNING "Failed to convert string to unsigned int.\n");
		goto END;
	}
    if (1 > iSlot || gSynoInternalHddNumber < iSlot) {
		printk(KERN_WARNING "Invalid Slot Number\n");
		iSlot = 0;
		goto END;
    }

END:
	return count;
}

static ssize_t pwrctl_present_show (struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int iRet = 0;
    int iPrzPinVal = 1;
    
	if (1 > iSlot || gSynoInternalHddNumber < iSlot) {
		printk(KERN_WARNING "Invalid Slot Number\n");
		goto END;
    }

	iPrzPinVal = SYNO_CHECK_HDD_DETECT(iSlot);

	iRet += scnprintf (buf, PAGE_SIZE, "Slot%d is %s\n", iSlot,
		(iPrzPinVal == 1) ? "Present" :
			(iPrzPinVal == 0) ? "Not Present": "Unknown");

END:
	return iRet;
}
static ssize_t pwrctl_present_store (struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    printk(KERN_WARNING "Present pin is not writable\n");
	return count;
}

static ssize_t pwrctl_enable_show (struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int iRet = 0;
	/* defult is not enable */
    int iEnPinVal = 0;


	if (1 > iSlot || gSynoInternalHddNumber < iSlot) {
		printk(KERN_WARNING "Invalid Slot Number\n");
		goto END;
    }

	iEnPinVal = SYNO_CHECK_HDD_ENABLE(iSlot);

	iRet += scnprintf (buf, PAGE_SIZE, "Slot%d is %sEnable\n", iSlot, iEnPinVal?"":"Not ");
END:
	return iRet;
}
static ssize_t pwrctl_enable_store (struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    int iValue = 0;

    if (0 > kstrtoint(buf, 0, &iValue)) {
		printk(KERN_WARNING "Failed to convert string to unsigned int.\n");
		goto END;
	}

    if (0 > iValue || 1 < iValue) {
        printk(KERN_WARNING "Invalid Input Value\n");
        goto END;
    }

	if (1 > iSlot || gSynoInternalHddNumber < iSlot) {
		printk(KERN_WARNING "Invalid Slot Number\n");
		goto END;
    }

    SYNO_CTRL_HDD_POWERON(iSlot, iValue);
END:
	return count;
}

// register function to attribute
static struct kobj_attribute pwrctl_slot = __ATTR( pwrctl_slot, 0640, pwrctl_slot_show, pwrctl_slot_store);
static struct kobj_attribute pwrctl_present = __ATTR( pwrctl_present, 0640, pwrctl_present_show, pwrctl_present_store);
static struct kobj_attribute pwrctl_enable = __ATTR( pwrctl_enable, 0640, pwrctl_enable_show, pwrctl_enable_store);

// put attribute to attribute group
static struct attribute *SynoHddPwrCtlAttr[] = {
	&pwrctl_slot.attr,
	&pwrctl_present.attr,
	&pwrctl_enable.attr,
	NULL,   /* NULL terminate the list*/
};
static struct attribute_group SynoHddPwrCtlGroup = {
	.attrs = SynoHddPwrCtlAttr
};

static int syno_hddpwrctl_test_init(void)
{
	int iRet = -1;
	SynoHddPwrCtlObject = kobject_create_and_add("syno_hddpwrctl_test", kernel_kobj);
	if (!SynoHddPwrCtlObject) {
		iRet = -ENOMEM;
		goto END;
	}

	//create attributes (files)
	if(sysfs_create_group(SynoHddPwrCtlObject, &SynoHddPwrCtlGroup)){
		iRet = -ENOMEM;
		goto END;
	}

	if (!SYNO_SUPPORT_HDD_DYNAMIC_ENABLE_POWER(1)){
		printk(KERN_ERR "Not support HDD dynamic power control\n");
		goto END;
	}

	iRet = 0;
END:
	if (0 != iRet) {
		if (SynoHddPwrCtlObject) {
			kobject_put(SynoHddPwrCtlObject);
		}
	}
	return iRet;
}

static void syno_hddpwrctl_test_exit(void)
{
	kobject_put(SynoHddPwrCtlObject);
}

MODULE_LICENSE("GPL");
module_init(syno_hddpwrctl_test_init);
module_exit(syno_hddpwrctl_test_exit);
