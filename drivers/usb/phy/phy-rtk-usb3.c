#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0
/*
 *  phy-rtk-usb3.c RTK usb3.0 phy driver
 *
 * copyright (c) 2017 realtek semiconductor corporation
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/nvmem-consumer.h>
#include <linux/sys_soc.h>
#include <linux/usb/phy.h>

#include "phy-rtk-usb.h"

#define RTK_USB3PHY_NAME "rtk-usb3phy"

#define USB_ST_BUSY		BIT(7)
#define PHY_ADDR_0x09 0x09
#define PHY_ADDR_0x1F 0x1F
#define PHY_ADDR_0x20 0x20
#define PHY_ADDR_0x30 0x30

#define USB_U3_TX_LFPS_SWING_TRIM_SHIFT 4
#define USB_U3_TX_LFPS_SWING_TRIM_MASK 0xF

#define PHY_ADDR_MAP_ARRAY_INDEX(addr) (addr)
#define ARRAY_INDEX_MAP_PHY_ADDR(index) (index)

struct reg_addr {
	void __iomem *REG_MDIO_CTL;
};

struct phy_parameter {
	u8 addr;
	u16 data;
};

struct phy_data {
	int size;
	struct phy_parameter *parameter;

	u16 saved_trim_value;//=0xFFFF;
	u8 connected;//=0;

	bool check_efuse;
	u8 efuse_usb_u3_tx_lfps_swing_trim;
	bool do_toggle;
	bool do_toggle_once;
	bool use_default_parameter;
};

static int rtk_usb_phy3_wait_vbusy(struct reg_addr *regAddr)
{
	return utmi_wait_register(regAddr->REG_MDIO_CTL, USB_ST_BUSY, 0);
}

static u16 rtk_usb_phy_read(struct reg_addr *regAddr, char addr)
{
	unsigned int regVal;
	u32 value;

	regVal = (addr << 8);

	writel(regVal, regAddr->REG_MDIO_CTL);

	rtk_usb_phy3_wait_vbusy(regAddr);

	value = readl(regAddr->REG_MDIO_CTL);
	value = value >> 16;

	return (u16)value;
}

static int rtk_usb_phy_write(struct reg_addr *regAddr, char addr, u16 data)
{
	unsigned int regVal;

	regVal = BIT(0)     |
			(addr << 8) |
			(data << 16);

	writel(regVal, regAddr->REG_MDIO_CTL);

	rtk_usb_phy3_wait_vbusy(regAddr);

	return 0;
}

static void rtk_usb_phy_shutdown(struct usb_phy *phy)
{
	/* Todo */
}

static int __get_phy_parameter_by_efuse(struct rtk_usb_phy_s *rtk_phy,
	    struct phy_data *phy_data, int index)
{
	u8 value = 0;
	struct nvmem_cell *cell;

	cell = nvmem_cell_get(rtk_phy->dev, "usb_u3_tx_lfps_swing_trim");
	if (IS_ERR(cell)) {
		dev_warn(rtk_phy->dev,
			    "%s failed to get usb_u3_tx_lfps_swing_trim: %ld\n",
			    __func__, PTR_ERR(cell));
	} else {
		unsigned char *buf;
		size_t buf_size;
#if defined(MY_ABC_HERE)
#else /* MY_ABC_HERE */
		int value_size = 4;
#endif /* MY_ABC_HERE */

		buf = nvmem_cell_read(cell, &buf_size);

		value = buf[0] & USB_U3_TX_LFPS_SWING_TRIM_MASK;

		dev_info(rtk_phy->dev,
			    "phy index=%d buf=0x%x buf_size=%d value=0x%x\n",
			    index, buf[0], (int)buf_size, value);
		kfree(buf);
		nvmem_cell_put(cell);
	}

	if (value < 0x8)
		phy_data->efuse_usb_u3_tx_lfps_swing_trim = 0x8;
	else
		phy_data->efuse_usb_u3_tx_lfps_swing_trim = (u8)value;

	dev_info(rtk_phy->dev, "Get Efuse usb_u3_tx_lfps_swing_trim=0x%x (value=0x%x)\n",
		    phy_data->efuse_usb_u3_tx_lfps_swing_trim, value);

	return 0;
}

static void do_rtk_usb3_phy_toggle(struct rtk_usb_phy_s *rtk_phy,
	    int index, bool isConnect);

static int do_rtk_usb_phy_init(struct usb_phy *phy, int phy_index)
{
	struct rtk_usb_phy_s *rtk_phy = (struct rtk_usb_phy_s *) phy;
	struct reg_addr *regAddr =
		    &((struct reg_addr *)rtk_phy->reg_addr)[phy_index];
	struct phy_data *phy_data =
		    &((struct phy_data *)rtk_phy->phy_data)[phy_index];
	int index = 0;
	struct phy_parameter *phy_parameter = phy_data->parameter;


	dev_info(phy->dev, "%s Init RTK USB 3.0 PHY phy#%d\n",
		    __func__, phy_index);

	if (phy_data->use_default_parameter) {
		dev_info(phy->dev, "%s phy#%d use default parameter\n",
			    __func__, phy_index);
		goto do_toggle;
	}

	for (index = 0; index < phy_data->size; index++) {
		u8 addr = (phy_parameter + index)->addr;
		u16 data = (phy_parameter + index)->data;

		if (addr == 0xFF)
			continue;

		if (addr == PHY_ADDR_0x20) {
			u8 efuse_val = phy_data->efuse_usb_u3_tx_lfps_swing_trim;
			u16 val_mask = USB_U3_TX_LFPS_SWING_TRIM_MASK;
			int val_shift = USB_U3_TX_LFPS_SWING_TRIM_SHIFT;

			if (efuse_val) {
				data &= ~(val_mask << val_shift);
				data |= ((efuse_val & val_mask) << val_shift);
			}
		}

		rtk_usb_phy_write(regAddr, addr, data);
	}

	for (index = 0; index < phy_data->size; index++) {
		u8 addr = (phy_parameter + index)->addr;
		u16 data = (phy_parameter + index)->data;

		if (addr == 0xFF)
			continue;

		dev_dbg(phy->dev, "[USB3_PHY], addr = 0x%02x, data = 0x%04x ==> read value = 0x%04x\n",
			    addr, data,
			    rtk_usb_phy_read(regAddr, addr));
	}

do_toggle:
	if (phy_data->do_toggle_once)
		phy_data->do_toggle = true;

	do_rtk_usb3_phy_toggle(rtk_phy, phy_index, false);

	if (phy_data->do_toggle_once) {
		u16 check_value = 0;
		int count = 10;

		check_value = rtk_usb_phy_read(regAddr, PHY_ADDR_0x30);

		while (!(check_value & BIT(15))) {
			check_value = rtk_usb_phy_read(regAddr, PHY_ADDR_0x30);
			mdelay(1);
			if (count-- < 0)
				break;
		}

		if (!(check_value & BIT(15)))
			dev_info(phy->dev, "toggle fail addr=0x%02x, data=0x%04x\n",
				    PHY_ADDR_0x30, check_value);
		else
			dev_info(phy->dev, "toggle okay addr=0x%02x, data=0x%04x\n",
				    PHY_ADDR_0x30, check_value);

		phy_data->do_toggle = false;
	}

	return 0;
}

static int rtk_usb_phy_init(struct usb_phy *phy)
{
	struct rtk_usb_phy_s *rtk_phy = (struct rtk_usb_phy_s *) phy;
	int ret = 0;
	int i;
	unsigned long phy_init_time = jiffies;

	if (!rtk_phy) {
		pr_err("%s rtk_phy is NULL!\n", __func__);
		return -1;
	}

	dev_info(phy->dev, "%s Init RTK USB 3.0 PHY\n", __func__);
	for (i = 0; i < rtk_phy->phyN; i++)
		ret = do_rtk_usb_phy_init(phy, i);

	dev_info(phy->dev, "%s Initialized RTK USB 3.0 PHY (take %dms)\n",
		    __func__,
		    jiffies_to_msecs(jiffies - phy_init_time));
	return ret;
}

static void do_rtk_usb3_phy_toggle(struct rtk_usb_phy_s *rtk_phy, int i,
	    bool isConnect)
{
	struct reg_addr *regAddr = &((struct reg_addr *)rtk_phy->reg_addr)[i];
	struct phy_data *phy_data = &((struct phy_data *)rtk_phy->phy_data)[i];
	struct phy_parameter *phy_parameter;
	size_t index;

	if (!rtk_phy) {
		pr_err("%s rtk_phy is NULL!\n", __func__);
		return;
	}

	regAddr = &((struct reg_addr *)rtk_phy->reg_addr)[i];
	phy_data = &((struct phy_data *)rtk_phy->phy_data)[i];

	if (!phy_data) {
		dev_err(rtk_phy->dev, "%s phy_data is NULL!\n", __func__);
		return;
	}

	if (!phy_data->do_toggle)
		return;

	phy_parameter = phy_data->parameter;

	index = PHY_ADDR_MAP_ARRAY_INDEX(PHY_ADDR_0x09);

	if (index < phy_data->size) {
		u8 addr = (phy_parameter + index)->addr;
		u16 data = (phy_parameter + index)->data;

		if (addr == 0xFF) {
			addr = ARRAY_INDEX_MAP_PHY_ADDR(index);
			data = rtk_usb_phy_read(regAddr, addr);
			(phy_parameter + index)->addr = addr;
			(phy_parameter + index)->data = data;
		}
		mdelay(1);
		dev_info(rtk_phy->dev,
			    "%s ########## to toggle PHY addr 0x09 BIT(9)\n",
			    __func__);
		rtk_usb_phy_write(regAddr, addr, data&(~BIT(9)));
		mdelay(1);
		rtk_usb_phy_write(regAddr, addr, data);
	}
	dev_info(rtk_phy->dev, "%s ########## PHY addr 0x1f = 0x%04x\n",
		    __func__, rtk_usb_phy_read(regAddr, PHY_ADDR_0x1F));
}

void rtk_usb3_phy_toggle(struct usb_phy *usb3_phy, bool isConnect, int port)
{
	int index = port;
	struct rtk_usb_phy_s *rtk_phy = NULL;

	if (usb3_phy != NULL && usb3_phy->dev != NULL)
		rtk_phy = dev_get_drvdata(usb3_phy->dev);

	if (rtk_phy == NULL) {
		pr_err("%s ERROR! NO this device\n", __func__);
		return;
	}

	if (index > rtk_phy->phyN) {
		pr_err("%s %d ERROR! port=%d > phyN=%d\n",
			    __func__, __LINE__, index, rtk_phy->phyN);
		return;
	}

	do_rtk_usb3_phy_toggle(rtk_phy, index, isConnect);
}
EXPORT_SYMBOL(rtk_usb3_phy_toggle);

#ifdef CONFIG_DEBUG_FS
static struct dentry *create_phy_debug_root(void)
{
	struct dentry *phy_debug_root;

	phy_debug_root = debugfs_lookup("phy", usb_debug_root);
	if (!phy_debug_root) {
		phy_debug_root = debugfs_create_dir("phy", usb_debug_root);
		if (!phy_debug_root)
			pr_err("%s Error phy_debug_root is NULL\n", __func__);
		else
			pr_info("%s Create phy_debug_root folder\n", __func__);
	}

	return phy_debug_root;
}

static int rtk_usb3_parameter_show(struct seq_file *s, void *unused)
{
	struct rtk_usb_phy_s		*rtk_phy = s->private;
	int i, index;

	for (i = 0; i < rtk_phy->phyN; i++) {
		struct reg_addr *regAddr =
			    &((struct reg_addr *)rtk_phy->reg_addr)[i];
		struct phy_data *phy_data =
			    &((struct phy_data *)rtk_phy->phy_data)[i];
		struct phy_parameter *phy_parameter;

		phy_parameter = phy_data->parameter;

		seq_printf(s, "[USB3_PHY] PHY %d\n", i);

		for (index = 0; index < phy_data->size; index++) {
			u8 addr = ARRAY_INDEX_MAP_PHY_ADDR(index);
			u16 data = (phy_parameter + index)->data;

			if ((phy_parameter + index)->addr == 0xFF)
				seq_printf(s, "[USB3_PHY], addr = 0x%02x, data = none   ==> read value = 0x%04x\n",
					    addr,
					    rtk_usb_phy_read(regAddr, addr));
			else
				seq_printf(s, "[USB3_PHY], addr = 0x%02x, data = 0x%04x ==> read value = 0x%04x\n",
					    addr, data,
					    rtk_usb_phy_read(regAddr, addr));
		}
	}
	return 0;
}

static int rtk_usb3_parameter_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_usb3_parameter_show, inode->i_private);
}

static const struct file_operations rtk_usb3_parameter_fops = {
	.open			= rtk_usb3_parameter_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int rtk_usb3_set_parameter_show(struct seq_file *s, void *unused)
{
	//struct rtk_usb_phy_s	*rtk_phy = s->private;

	seq_puts(s, "Set Phy parameter by following command\n");
	seq_puts(s, "echo \"phy_num addr value\" > set_parameter\n");
	seq_puts(s, "echo \"0 0x00 0x4008\" > set_parameter\n");
	seq_puts(s, "echo \"0 0x21 0x88AA\" > set_parameter\n");

	return 0;
}

static int rtk_usb3_set_parameter_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_usb3_set_parameter_show, inode->i_private);
}

static ssize_t rtk_usb3_set_parameter_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct rtk_usb_phy_s		*rtk_phy = s->private;
	struct reg_addr *regAddr;
	struct phy_data *phy_data;
	struct phy_parameter *phy_parameter;
	int index, i, ret = 0;
	char buffer[40];
	char *buf = buffer;
	u32 addr;
	u32 value;

	if (copy_from_user(&buffer, ubuf,
		    min_t(size_t, sizeof(buffer) - 1, count)))
		return -EFAULT;

	ret = kstrtoint(buf, 0, &i);
	if (ret < 0)
		return -EFAULT;

	buf = buf + 1;

	regAddr = &((struct reg_addr *)rtk_phy->reg_addr)[i];
	phy_data = &((struct phy_data *)rtk_phy->phy_data)[i];
	phy_parameter = phy_data->parameter;

	buf = skip_spaces(buf);
	ret = sscanf(buf, "%x %x", &addr, &value);
	if (ret < 0)
		return -EFAULT;

	index = PHY_ADDR_MAP_ARRAY_INDEX(addr);

	if (index < phy_data->size) {
		(phy_parameter + index)->addr = addr;
		(phy_parameter + index)->data = value;
		rtk_usb_phy_write(regAddr, addr, value);
	}

	return count;
}

static const struct file_operations rtk_usb3_set_parameter_fops = {
	.open			= rtk_usb3_set_parameter_open,
	.write			= rtk_usb3_set_parameter_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int rtk_usb3_toggle_show(struct seq_file *s, void *unused)
{
	struct rtk_usb_phy_s		*rtk_phy = s->private;
	struct phy_data *phy_data;
	int i;

	for (i = 0; i < rtk_phy->phyN; i++) {
		phy_data = &((struct phy_data *)rtk_phy->phy_data)[i];
		seq_printf(s, "Now phy#%d do_toggle is %s.\n",
			    i, phy_data->do_toggle?"Enable":"Disable");
	}
	seq_puts(s, "ehco 1 to enable toggle phy parameter.\n");

	return 0;
}

static int rtk_usb3_toggle_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_usb3_toggle_show, inode->i_private);
}

static ssize_t rtk_usb3_toggle_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct rtk_usb_phy_s		*rtk_phy = s->private;
	char			buf[32];
	struct phy_data *phy_data;
	bool enable = false;
	int i;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "1", 1))
		enable = true;

	for (i = 0; i < rtk_phy->phyN; i++) {
		phy_data = &((struct phy_data *)rtk_phy->phy_data)[i];
		phy_data->do_toggle = enable;
		dev_info(rtk_phy->dev, "Set phy#%d do_toggle is %s.\n",
			    i, phy_data->do_toggle?"Enable":"Disable");
	}

	return count;
}

static const struct file_operations rtk_usb3_toggle_fops = {
	.open			= rtk_usb3_toggle_open,
	.write			= rtk_usb3_toggle_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static inline void create_debug_files(struct rtk_usb_phy_s *rtk_phy)
{
	struct dentry *phy_debug_root = NULL;

	dev_dbg(rtk_phy->dev, "%s", __func__);

	phy_debug_root = create_phy_debug_root();

	if (!phy_debug_root) {
		dev_err(rtk_phy->dev, "%s Error phy_debug_root is NULL",
			    __func__);
		return;
	}
	rtk_phy->debug_dir = debugfs_create_dir(dev_name(rtk_phy->dev),
		    phy_debug_root);
	if (!rtk_phy->debug_dir) {
		dev_err(rtk_phy->dev, "%s Error debug_dir is NULL", __func__);
		return;
	}

	if (!debugfs_create_file("parameter", 0444,
		    rtk_phy->debug_dir, rtk_phy,
		    &rtk_usb3_parameter_fops))
		goto file_error;

	if (!debugfs_create_file("set_parameter", 0644,
		    rtk_phy->debug_dir, rtk_phy,
		    &rtk_usb3_set_parameter_fops))
		goto file_error;

	if (!debugfs_create_file("toggle", 0644, rtk_phy->debug_dir, rtk_phy,
		    &rtk_usb3_toggle_fops))
		goto file_error;

	return;

file_error:
	debugfs_remove_recursive(rtk_phy->debug_dir);
}
#endif //CONFIG_DEBUG_FS

#define DEFAULT_CHIP_REVISION 0xA00
#define MAX_CHIP_REVISION 0xC00

static int __get_chip_revision(void)
{
	int chip_revision = 0xFFF;
	char revision[] = "FFF";
	struct soc_device_attribute soc_att[] = {{.revision = revision}, {}};
	struct soc_device_attribute *soc_att_match = NULL;

	while (soc_att_match == NULL) {
		chip_revision--;

		if (chip_revision <= DEFAULT_CHIP_REVISION)
			break;
		if (chip_revision > MAX_CHIP_REVISION)
			chip_revision = MAX_CHIP_REVISION;
		else if ((chip_revision & 0xFF) > 0xF)
			chip_revision = (chip_revision & 0xF00) + 0xF;

		snprintf(revision, 4, "%X", chip_revision);

		soc_att_match = (struct soc_device_attribute *)
			    soc_device_match(soc_att);
	}

	if (soc_att_match) {
		pr_debug("%s get chip_revision %x\n", __func__, chip_revision);
		return chip_revision;
	}

	pr_debug("%s: Use default chip_revision %x\n", __func__,
		    DEFAULT_CHIP_REVISION);
	return DEFAULT_CHIP_REVISION;
}

static int __get_phy_parameter_v1(struct device *dev, struct phy_data *phy_data,
	    struct device_node *sub_node)
{
	struct phy_parameter *phy_parameter;
	int revision, i, ret = 0;
	u8 *addr;
	u16 *data;

	ret = of_property_read_u32_index(sub_node, "phy_data_size", 0,
		    &phy_data->size);
	if (ret)
		goto out;

	phy_data->parameter = devm_kzalloc(dev,
		    sizeof(struct phy_parameter) * phy_data->size,
		    GFP_KERNEL);
	if (!phy_data->parameter)
		return -ENOMEM;

	addr = kzalloc(sizeof(u8) * phy_data->size,
		    GFP_KERNEL);
	if (!addr) {
		kfree(phy_data->parameter);
		phy_data->parameter = NULL;
		return -ENOMEM;
	}

	data = kzalloc(sizeof(u16) * phy_data->size,
		    GFP_KERNEL);
	if (!data) {
		kfree(phy_data->parameter);
		kfree(addr);
		phy_data->parameter = NULL;
		return -ENOMEM;
	}
	ret = of_property_read_u8_array(sub_node, "phy_data_addr",
		    addr, phy_data->size);
	if (ret)
		goto out;

	revision = __get_chip_revision();
	dev_dbg(dev, "%s: Chip revision is %x\n", __func__, revision);

	while (revision >= DEFAULT_CHIP_REVISION) {
		char phy_data_revision[14] = {0};

		snprintf(phy_data_revision, 13, "phy_data_%X",
			    revision);
		ret = of_property_read_u16_array(sub_node,
			    phy_data_revision,
			    data, phy_data->size);
		if (!ret) {
			dev_dbg(dev, "%s load %s parameter\n",
				    __func__, phy_data_revision);
			break;
		}
		revision--;
		if ((revision & 0xFF) > 0xF)
			revision = (revision & 0xF00) + 0xF;
	}

	/* For old device tree */
	if (ret) {
		ret = of_property_read_u16_array(sub_node,
			    "phy_data_revA",
			    data, phy_data->size);
		if (ret)
			goto out;
		else
			dev_info(dev, "%s load parameter\n", __func__);
	}

	phy_parameter = phy_data->parameter;

	for (i = 0; i < phy_data->size; i++) {
		(phy_parameter + i)->addr = *(addr + i);
		(phy_parameter + i)->data = *(data + i);
		dev_dbg(dev, "%s i=%d addr=0x%x data=0x%x\n",
			    __func__, i, (phy_parameter + i)->addr,
			    (phy_parameter + i)->data);
	}

out:
	kfree(addr);
	kfree(data);

	return ret;
}

static int __get_phy_parameter_v2(struct device *dev, struct phy_data *phy_data,
	    struct device_node *sub_node)
{
	struct phy_parameter *phy_parameter;
	int revision, i, ret = 0;
	int data_size , num_cells = 2;
	char phy_data_revision[14] = {0};

	ret = of_property_read_u32_index(sub_node, "phy_data_size", 0,
		    &phy_data->size);
	if (ret)
		goto out;

	phy_data->parameter = devm_kzalloc(dev,
		    sizeof(struct phy_parameter) * phy_data->size,
		    GFP_KERNEL);
	if (!phy_data->parameter)
		return -ENOMEM;

	revision = __get_chip_revision();
	dev_dbg(dev, "%s: Chip revision is %x\n", __func__, revision);

	while (revision >= DEFAULT_CHIP_REVISION) {
		snprintf(phy_data_revision, 13, "phy_data_%X",
			    revision);
		if (of_get_property(sub_node, phy_data_revision, &data_size)) {
			dev_dbg(dev, "%s load %s parameter (data_size=%d)\n",
				    __func__, phy_data_revision, data_size);
			break;
		}
		revision--;
		if ((revision & 0xFF) > 0xF)
			revision = (revision & 0xF00) + 0xF;

		data_size = 0;
		ret = 0;
	}

	phy_parameter = phy_data->parameter;
	for (i = 0; i < phy_data->size; i++)
		(phy_parameter + i)->addr = 0xFF;

	data_size = data_size / (sizeof(u32) * num_cells);
	for (i = 0; i < data_size; i++) {
		struct phy_parameter *parameter;
		u32 addr, data;
		int offset, index;

		offset = i * num_cells;

		ret = of_property_read_u32_index(sub_node, phy_data_revision,
			    offset, &addr);
		if (ret) {
			dev_err(dev, "ERROR: To get %s i=%d addr=0x%x\n",
				    phy_data_revision, i, addr);
			break;
		}

		ret = of_property_read_u32_index(sub_node, phy_data_revision,
			    offset + 1, &data);
		if (ret) {
			dev_err(dev, "ERROR: To get %s i=%d addr=0x%x\n",
				    phy_data_revision, i, data);
			break;
		}

		index = PHY_ADDR_MAP_ARRAY_INDEX(addr);
		parameter = (phy_parameter + index);
		parameter->addr = (u8)addr;
		parameter->data = (u16)data;

		dev_dbg(dev, "%s index=%d addr=0x%x data=0x%x\n",
			    phy_data_revision, index,
			    parameter->addr, parameter->data);
	}

out:
	return ret;
}

static int rtk_usb3phy_probe(struct platform_device *pdev)
{
	struct rtk_usb_phy_s *rtk_usb_phy;
	struct device *dev = &pdev->dev;
	int i, ret, phyN;

	rtk_usb_phy = devm_kzalloc(dev, sizeof(*rtk_usb_phy), GFP_KERNEL);
	if (!rtk_usb_phy)
		return -ENOMEM;

	rtk_usb_phy->dev			= &pdev->dev;
	rtk_usb_phy->phy.dev		= rtk_usb_phy->dev;
	rtk_usb_phy->phy.label		= RTK_USB3PHY_NAME;
	rtk_usb_phy->phy.init		= rtk_usb_phy_init;
	rtk_usb_phy->phy.shutdown	= rtk_usb_phy_shutdown;

	if (!dev->of_node) {
		dev_err(dev, "%s %d No device node\n", __func__, __LINE__);
		goto err;
	}

	ret = of_property_read_u32_index(dev->of_node, "phyN", 0,
		    &phyN);
	if (ret)
		goto err;

	rtk_usb_phy->phyN = phyN;

	rtk_usb_phy->reg_addr = devm_kzalloc(dev,
		    sizeof(struct reg_addr) * phyN, GFP_KERNEL);
	if (!rtk_usb_phy->reg_addr)
		return -ENOMEM;
	rtk_usb_phy->phy_data = devm_kzalloc(dev,
		    sizeof(struct phy_data) * phyN, GFP_KERNEL);
	if (!rtk_usb_phy->phy_data)
		return -ENOMEM;

	for (i = 0; i < phyN; i++) {
		struct reg_addr *addr =
			    &((struct reg_addr *)rtk_usb_phy->reg_addr)[i];
		struct phy_data *phy_data =
			    &((struct phy_data *)rtk_usb_phy->phy_data)[i];

		char phy_name[5], phy_name_v2[10];
		struct device_node *sub_node;

		addr->REG_MDIO_CTL = of_iomap(dev->of_node, i);
		dev_dbg(dev, "%s %d #%d REG_MDIO_CTL=%p\n",
			    __func__, __LINE__, i, addr->REG_MDIO_CTL);

		snprintf(phy_name, 5, "phy%d", i);

		sub_node = of_get_child_by_name(dev->of_node, phy_name);
		if (sub_node) {
			dev_info(dev, "%s %d: #%d Get phy data v1 sub_node for %s\n",
				    __func__, __LINE__, i, phy_name);
			ret = __get_phy_parameter_v1(dev, phy_data, sub_node);
			if (ret)
				goto err;
		} else {
			snprintf(phy_name_v2, 10, "phy%d_data", i);
			sub_node = of_get_child_by_name(dev->of_node, phy_name_v2);
			if (sub_node) {
				dev_info(dev, "%s %d: #%d Get phy data v2 sub_node for %s\n",
				    __func__, __LINE__, i, phy_name_v2);
				ret = __get_phy_parameter_v2(dev, phy_data, sub_node);
				if (ret)
					goto err;
			}
		}

		if (!sub_node) {
			dev_err(dev, "%s %d No device sub node for %s\n",
				    __func__, __LINE__, phy_name);
			goto err;
		}

		phy_data->saved_trim_value = 0xFFFF;
		phy_data->connected = 0;

		if (of_property_read_bool(sub_node, "do_toggle_once"))
			phy_data->do_toggle_once = true;
		else
			phy_data->do_toggle_once = false;

		if (of_property_read_bool(sub_node, "do_toggle"))
			phy_data->do_toggle = true;
		else
			phy_data->do_toggle = false;

		if (of_property_read_bool(sub_node, "use_default_parameter"))
			phy_data->use_default_parameter = true;
		else
			phy_data->use_default_parameter = false;

		if (of_property_read_bool(sub_node, "check_efuse"))
			phy_data->check_efuse = true;
		else
			phy_data->check_efuse = false;

		if (phy_data->check_efuse)
			__get_phy_parameter_by_efuse(rtk_usb_phy, phy_data, i);
	}

	platform_set_drvdata(pdev, rtk_usb_phy);

	ret = usb_add_phy_dev(&rtk_usb_phy->phy);
	if (ret)
		goto err;

#ifdef CONFIG_DEBUG_FS
	create_debug_files(rtk_usb_phy);
#endif

	dev_info(&pdev->dev, "%s Probe RTK USB 3.0 PHY\n", __FILE__);
err:
	return ret;
}

static int rtk_usb3phy_remove(struct platform_device *pdev)
{
	struct rtk_usb_phy_s *rtk_usb_phy = platform_get_drvdata(pdev);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(rtk_usb_phy->debug_dir);
#endif

	usb_remove_phy(&rtk_usb_phy->phy);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id usbphy_rtk_dt_match[] = {
	{ .compatible = "realtek,usb3phy", },
	{},
};
MODULE_DEVICE_TABLE(of, usbphy_rtk_dt_match);
#endif

static struct platform_driver rtk_usb3phy_driver = {
	.probe		= rtk_usb3phy_probe,
	.remove		= rtk_usb3phy_remove,
	.driver		= {
		.name	= RTK_USB3PHY_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(usbphy_rtk_dt_match),
	},
};

module_platform_driver(rtk_usb3phy_driver);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" RTK_USB3PHY_NAME);
