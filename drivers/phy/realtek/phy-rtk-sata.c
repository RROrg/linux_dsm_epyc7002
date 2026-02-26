#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek SATA3 AHCI Controller PHY Driver
 *
 * Copyright (C) 2020 Realtek
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/sys_soc.h>
#include <linux/nvmem-consumer.h>

#define DRV_NAME		"rtk-sata-phy"

#define RTK_SATA_PHY_NUMBER	2
#define PHY_MAX_CLK		5
#define PHY_MAX_RST		5

#define REG_PHY_CTRL		0x50
#define REG_SB2_PHY_CTRL	0x980

#define REG_CLKEN		0x18
#define CLKEN_SHIFT		7

#define REG_SATA_CTRL		0x20
#define OOBS_EN0		BIT(20)
#define OOBS_EN1		BIT(21)
#define OOBS_DATA0		BIT(22)
#define OOBS_DATA1		BIT(23)

#define REG_PHY0_ADJ		0x28
#define REG_PHY1_ADJ		0x2C

#define REG_MDIO_CTRL		0x60
#define MDIO_REG_SHIFT		8
#define MDIO_PHYADDR_SHIFT	14
#define MDIO_DATA_SHIFT		16
#define MDIO_BUSY		BIT(7)
#define MDIO_RDY		BIT(4)
#define MDIO_WRITE		BIT(0)

#define TCALR_MSK		GENMASK(4, 0)

#define REG_MDIO_CTRL1		0x64
#define REG_PHY_SPD		0x68

#define OOBS_K_count_limit   9

enum mdio_phy_addr {
	PHY_ADDR_SATA1 = 0,
	PHY_ADDR_SATA2 = 1,
	PHY_ADDR_SATA3 = 2,
	PHY_ADDR_ALL = 3,
};

static const struct soc_device_attribute rtk_soc_kylin[] = {
	{ .family = "Realtek Kylin", },
	{ /* empty */ }
};

static const struct soc_device_attribute rtk_soc_kylin_a00[] = {
	{ .family = "Realtek Kylin", .revision = "A00" },
	{ /* empty */ }
};

static const struct soc_device_attribute rtk_soc_kylin_a01[] = {
	{ .family = "Realtek Kylin", .revision = "A01" },
	{ /* empty */ }
};

static const struct soc_device_attribute rtk_soc_kylin_b00[] = {
	{ .family = "Realtek Kylin", .revision = "B00" },
	{ /* empty */ }
};

static const struct soc_device_attribute rtk_soc_kylin_b01[] = {
	{ .family = "Realtek Kylin", .revision = "B01" },
	{ /* empty */ }
};

static const struct soc_device_attribute rtk_soc_thor[] = {
	{ .family = "Realtek Thor", },
	{ /* empty */ }
};

static const struct soc_device_attribute rtk_soc_hank[] = {
	{ .family = "Realtek Hank", },
	{ /* empty */ }
};

static const struct soc_device_attribute rtk_soc_stark[] = {
	{ .family = "Realtek Stark", },
	{ /* empty */ }
};

struct rtk_sata_port {
	unsigned int index;
	unsigned int param_size;
	unsigned int *param_table;
	int tx_swing[PHY_ADDR_ALL];
	int tx_emphasis[PHY_ADDR_ALL];
	struct phy *phy;
	struct reset_control *rsts[PHY_MAX_RST];
};

struct rtk_sata_phy {
	struct device *dev;
	struct rtk_sata_port **phys;

	void __iomem *base;
	struct regmap *phyctl;
	struct regmap *crt;

	unsigned int nphys;

	struct reset_control *rsts[PHY_MAX_RST];
};

static int mdio_wait_busy(void __iomem *base)
{
	unsigned int val;
	int cnt = 0;

	val = readl(base + REG_MDIO_CTRL);
	while((val & MDIO_BUSY) && cnt++ < 5) {
		udelay(10);
		val = readl(base + REG_MDIO_CTRL);
	}

	if (val & MDIO_BUSY)
		return -EBUSY;
	return 0;
}

static int write_mdio_reg(void __iomem *base, unsigned int phyaddr,
			  unsigned int reg, unsigned int value)
{
	unsigned int val;
	int i;

	if (mdio_wait_busy(base))
		goto mdio_busy;

	if (phyaddr == PHY_ADDR_ALL) {
		for (i = 0; i < PHY_ADDR_ALL; i++) {
			val = (i << MDIO_PHYADDR_SHIFT) |
			      (value << MDIO_DATA_SHIFT) |
			      (reg << MDIO_REG_SHIFT) |
			      MDIO_RDY | MDIO_WRITE;
			writel(val, base + REG_MDIO_CTRL);

			if (mdio_wait_busy(base))
				goto mdio_busy;
		}
	} else {
		val = (phyaddr << MDIO_PHYADDR_SHIFT) |
		      (value << MDIO_DATA_SHIFT) |
		      (reg << MDIO_REG_SHIFT) |
		      MDIO_RDY | MDIO_WRITE;
		writel(val, base + REG_MDIO_CTRL);
	}

	return 0;
mdio_busy:
	pr_err(DRV_NAME ": %s - mdio is busy\n", __func__);
	return -EBUSY;
}

__attribute__((unused)) static unsigned int
read_mdio_reg(void __iomem *base, unsigned int phyaddr, unsigned int reg)
{
	unsigned int addr;

	if (mdio_wait_busy(base))
		goto mdio_busy;

	addr = (phyaddr << MDIO_PHYADDR_SHIFT) | (reg << MDIO_REG_SHIFT);
	writel(addr | MDIO_RDY, base + REG_MDIO_CTRL);

	if (mdio_wait_busy(base))
		goto mdio_busy;

	return readl(base + REG_MDIO_CTRL) >> MDIO_DATA_SHIFT;
mdio_busy:
	pr_err(DRV_NAME ": %s - mdio is busy\n", __func__);
	return -EBUSY;
}

static void rtk_sata_phy_enable(struct rtk_sata_phy *priv)
{
	struct device *dev = priv->dev;
	struct clk *clk;
	int i;

	for (i = 0; i < PHY_MAX_CLK; i++) {
		clk = of_clk_get(dev->of_node, i);
		if (IS_ERR(clk))
			break;
		clk_prepare_enable(clk);
	}

	for (i = 0; i < PHY_MAX_RST; i++) {
		if (IS_ERR(priv->rsts[i]))
			break;
		reset_control_deassert(priv->rsts[i]);
	}
}

static void rtk_sata_phy_disable(struct rtk_sata_phy *priv)
{
	struct device *dev = priv->dev;
	struct clk *clk;
	int i;

	for (i=0; i<PHY_MAX_RST; i++) {
		if (IS_ERR(priv->rsts[i]))
			break;
		reset_control_assert(priv->rsts[i]);
	}
	for (i=0; i<PHY_MAX_CLK; i++) {
		clk = of_clk_get(dev->of_node, i);
		if (IS_ERR(clk))
			break;
		clk_disable_unprepare(clk);
	}

	return;
}

static void bubble_sort(unsigned int *qq, unsigned int count)
{
	unsigned int i, j;

	for (i = 1; i < count; i++) {
		for (j = 0; j < (count-i); j++) {
			if (qq[j] > qq[j+1]) {
				unsigned int t;

				t = qq[j];
				qq[j] = qq[j+1];
				qq[j+1] = t;
			}
		}
	}
}

static unsigned int find_mode(unsigned int *qq, unsigned int count)
{
	unsigned int i, j;
	unsigned int cnt[3];

	cnt[0] = 1;
	j = cnt[1] = cnt[2] = 0;
	for (i = 0; i < (count-1); i++) {
		if ((qq[i] != qq[i+1]) && (3 > j))
			j++;
		cnt[j]++;
	}
	if (cnt[0] >= cnt[1]) {
		if (cnt[0] >= cnt[2])
			return(qq[0]);
		else
			return(qq[cnt[0]+cnt[1]]);
	} else {
		if (cnt[1] >= cnt[2])
			return(qq[cnt[0]]);
		else
			return(qq[cnt[0]+cnt[1]]);
	}
}

static void phy_rxidle_adjust(struct phy *phy)
{
	struct rtk_sata_port *port = phy_get_drvdata(phy);
	struct rtk_sata_phy *priv = dev_get_drvdata(phy->dev.parent);
	void __iomem *base = priv->base;
	unsigned int reg;
	unsigned int PreDIV, N_code, F_code, DIV, DIVMODE, DCSB_Freq_Sel;
	unsigned int preset, tmp, gap, up, down, BUS_H_KHz;

	regmap_read(priv->crt, 0x1b0, &reg);
	PreDIV = ((0x3 << 18) & reg) >> 18;
	DIV = ((0x7 << 22) & reg) >> 22;
	DIVMODE = ((0x1 << 25) & reg) >> 25;

	regmap_read(priv->crt, 0x544, &reg);
	N_code = ((0xff << 11) & reg) >> 11;
	F_code = (0x7ff << 0) & reg;

	regmap_read(priv->crt, 0x30, &reg);
	DCSB_Freq_Sel = ((0x3 << 2) & reg) >> 2;

	if (DIVMODE == 0)
		N_code = N_code + 4;
	else
		N_code = N_code + 3;

	switch (PreDIV) {
	case 0: BUS_H_KHz = 100 * 270 * (N_code * 2048 + F_code); break;
	case 1: BUS_H_KHz = 100 * 135 * (N_code * 2048 + F_code); break;
	case 2: BUS_H_KHz = 100 * 90 * (N_code * 2048 + F_code); break;
	case 3: BUS_H_KHz = 10 * 675 * (N_code * 2048 + F_code); break;
	}

	BUS_H_KHz = BUS_H_KHz / 2048 / (DIV + 1);

	switch (DCSB_Freq_Sel) {
	case 2: BUS_H_KHz /= 2; break;
	case 3: BUS_H_KHz /= 4; break;
	}

	up = 1120 * BUS_H_KHz / 10000000;
	tmp = (1120 * BUS_H_KHz) % 10000000;
	if (0 != tmp) up++;

	down = 1013 * BUS_H_KHz / 10000000;
	tmp = (1013 * BUS_H_KHz) % 10000000;
	if (0 == tmp) down--;

	gap = BUS_H_KHz / 100000;
	tmp = BUS_H_KHz % 100000;
	if (0 != tmp) gap++;

	preset = (0x1 << 26) | (0x1 << 24) | ((0xf & gap) << 16) | ((0xff & down) << 8) | (0xff & up);

	if (port->index == 0)
		writel(preset, base + REG_PHY0_ADJ);
	else if (port->index == 1)
		writel(preset, base + REG_PHY1_ADJ);
}

static int rtk_sata_phy_calibration(struct phy *phy, unsigned int addr)
{
	struct rtk_sata_port *port = phy_get_drvdata(phy);
	struct rtk_sata_phy *priv = dev_get_drvdata(phy->dev.parent);
	struct device *dev = priv->dev;
	void __iomem *base = priv->base;

	unsigned int i, val, TCALR, taclr[OOBS_K_count_limit], cnt = 0;

	val = readl(base + REG_SATA_CTRL);
	if (port->index == 0)
		val = (val & ~OOBS_DATA0) | OOBS_EN0;
	else
		val = (val & ~OOBS_DATA1) | OOBS_EN1;
	writel(val, base + REG_SATA_CTRL);

	for (i = 0; i < OOBS_K_count_limit; i++) {
		val = read_mdio_reg(base, addr, 0x9);
		val = (val & ~BIT(4)) | BIT(9);
		write_mdio_reg(base, addr, 0x9, val);

		val = read_mdio_reg(base, addr, 0x9);
		val = val & ~BIT(9);
		write_mdio_reg(base, addr, 0x9, val);

		val = read_mdio_reg(base, addr, 0x9);
		val = val | BIT(9);
		write_mdio_reg(base, addr, 0x9, val);

		val = read_mdio_reg(base, addr, 0xd);
		val = val | BIT(6);
		write_mdio_reg(base, addr, 0xd, val);

		val = read_mdio_reg(base, addr, 0x1e);
		val = (val & ~BIT(12) & ~BIT(14) & ~BIT(7)) | BIT(13);
		write_mdio_reg(base, addr, 0x1e, val);

		val = read_mdio_reg(base, 0, 0x35);
		while(val & BIT(14)) {
			msleep(1);
			val = read_mdio_reg(base, 0, 0x35);
			if (cnt++ >= 10)
				goto calibration_err;
		}

		val = read_mdio_reg(base, addr, 0x1e);
		val = (val & ~BIT(14) & ~BIT(7)) | BIT(13) | BIT(12);
		write_mdio_reg(base, addr, 0x1e, val);

		mdelay(1);

		taclr[i] = read_mdio_reg(base, 0, 0x36) & TCALR_MSK;
	}

	bubble_sort(taclr, OOBS_K_count_limit);
	TCALR = find_mode(taclr, OOBS_K_count_limit);

	val = read_mdio_reg(base, addr, 0x3);
	val = (val & ~(TCALR_MSK << 1)) | (TCALR << 1);
	write_mdio_reg(base, addr, 0x3, val);

	val = read_mdio_reg(base, addr, 0x9);
	val = val | BIT(4);
	write_mdio_reg(base, addr, 0x9, val);

	val = read_mdio_reg(base, addr, 0x1e);
	val = (val & ~BIT(14) & ~BIT(13) & ~BIT(7)) | BIT(13) | BIT(12);
	write_mdio_reg(base, addr, 0x1e, val);

	val = readl(base + REG_SATA_CTRL);
	if (port->index == 0)
		val = (val & ~OOBS_EN0) | OOBS_DATA0;
	else
		val = (val & ~OOBS_EN1) | OOBS_DATA1;
	writel(val, base + REG_SATA_CTRL);

	val = read_mdio_reg(base, 0, 0x36);
	while(!(val & BIT(7))) {
		msleep(1);
		val = read_mdio_reg(base, 0, 0x36);
		if (cnt++ >= 10)
			goto calibration_err;
	}

	return TCALR;

calibration_err:
	dev_err(dev, "port%d gen%d rx calibration err\n", port->index, addr+1);
	return -1;
}

static void rtk_sata_phy_fg_offset(struct phy *phy)
{
	struct rtk_sata_phy *priv = dev_get_drvdata(phy->dev.parent);
	struct device *dev = priv->dev;
	void __iomem *base = priv->base;
	unsigned int val, i, fg_offset;

	for (i = 0; i < PHY_ADDR_ALL; i++) {
		val = read_mdio_reg(base, i, 0x1e);
		val = (val & ~BIT(14) & ~BIT(12)) | BIT(7) | BIT(13);
		write_mdio_reg(base, i, 0x1e, val);
	}

	fg_offset = read_mdio_reg(base, 0, 0x36) & GENMASK(3, 0);
	dev_info(dev, "foreground offset 0x%x\n", fg_offset);

	for (i = 0; i < PHY_ADDR_ALL; i++) {
		val = read_mdio_reg(base, i, 0x0b);
		val = (val & ~GENMASK(8, 5)) | (fg_offset << 5);
		write_mdio_reg(base, i, 0x0b, val);

		val = read_mdio_reg(base, i, 0x0d);
		val = val & ~BIT(13);
		write_mdio_reg(base, i, 0x0d, val);
	}
}

static int rtk_sata_phy_init(struct phy *phy)
{
	struct rtk_sata_port *port = phy_get_drvdata(phy);
	struct rtk_sata_phy *priv = dev_get_drvdata(phy->dev.parent);
	struct nvmem_cell *cell;
	void __iomem *base = priv->base;
	unsigned int reg_phy_ctrl;
	unsigned int phy_en;
	unsigned int tcalr;
	unsigned int reg;
	unsigned char *buf;
	size_t buf_size;
	int i, swing[PHY_ADDR_ALL], emphasis[PHY_ADDR_ALL];

	for (i = 0; i < PHY_MAX_RST; i++) {
		if (IS_ERR(port->rsts[i]))
			break;
		reset_control_deassert(port->rsts[i]);
	}

	if (!soc_device_match(rtk_soc_kylin)) {
		reg_phy_ctrl = REG_PHY_CTRL;
		phy_en = (BIT(0) | BIT(2) | BIT(4) | BIT(8)) << port->index;

		regmap_read(priv->phyctl, reg_phy_ctrl, &reg);
		phy_en |= reg;
		regmap_update_bits(priv->phyctl, reg_phy_ctrl, phy_en, phy_en);
		reg = readl(base + REG_CLKEN) | (1 << (port->index + CLKEN_SHIFT));
		writel(reg, base + REG_CLKEN);
	}

	if (soc_device_match(rtk_soc_stark)) {
		cell = nvmem_cell_get(priv->dev, "sata-cal");

		buf = nvmem_cell_read(cell, &buf_size);

		swing[PHY_ADDR_SATA1] = ((buf[port->index] & 0xf) ^ 0xB) +
					port->tx_swing[PHY_ADDR_SATA1];
		swing[PHY_ADDR_SATA2] = (((buf[port->index] & 0xf0) >> 4) ^ 0xB)
					+ port->tx_swing[PHY_ADDR_SATA2];
		swing[PHY_ADDR_SATA3] = port->tx_swing[PHY_ADDR_SATA3] + 0xc;
		emphasis[PHY_ADDR_SATA1] = port->tx_emphasis[PHY_ADDR_SATA1];
		emphasis[PHY_ADDR_SATA2] = port->tx_emphasis[PHY_ADDR_SATA2];
		emphasis[PHY_ADDR_SATA3] = port->tx_emphasis[PHY_ADDR_SATA3] + 0xb;

		if (port->index == 0)
			tcalr = buf[2] & TCALR_MSK;
		else if (port->index == 1)
			tcalr = ((buf[2] & 0xe0) >> 5) | ((buf[3] & 0x3) << 3);

		for (i = 0; i < PHY_ADDR_ALL; i++) {
			if (swing[i] < 0)
				swing[i] = 0;
			if (swing[i] > 0xf)
				swing[i] = 0xf;
			if (emphasis[i] < 0)
				emphasis[i] = 0;
			if (emphasis[i] > 0xf)
				emphasis[i] = 0xf;
		}

		nvmem_cell_put(cell);
	}
	writel(port->index, base + REG_MDIO_CTRL1);

	if (soc_device_match(rtk_soc_kylin)) {
		write_mdio_reg(base, PHY_ADDR_ALL, 0x2, 0x7000);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x5, 0x336a);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x11, 0x0);
		write_mdio_reg(base, PHY_ADDR_SATA1, 0x1, 0xe070);
		write_mdio_reg(base, PHY_ADDR_SATA2, 0x1, 0xe05c);
		write_mdio_reg(base, PHY_ADDR_SATA3, 0x1, 0xe04a);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x6, 0x15);
		write_mdio_reg(base, PHY_ADDR_ALL, 0xa, 0xc660);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x19, 0x2004);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x16, 0x770);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x10, 0x2900);
		write_mdio_reg(base, PHY_ADDR_ALL, 0xc, 0x4000);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x17, 0x27);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x4, 0x538e);
		if (soc_device_match(rtk_soc_kylin_a00) || soc_device_match(rtk_soc_kylin_a01)) {
			write_mdio_reg(base, PHY_ADDR_ALL, 0x9, 0x7210);
			write_mdio_reg(base, PHY_ADDR_ALL, 0x2a, 0x4000);
			write_mdio_reg(base, PHY_ADDR_SATA1, 0x3, 0x2775);
			write_mdio_reg(base, PHY_ADDR_SATA2, 0x3, 0x276e);
			write_mdio_reg(base, PHY_ADDR_SATA3, 0x3, 0x276c);
		} else {
			write_mdio_reg(base, PHY_ADDR_ALL, 0x9, 0x4210);
			write_mdio_reg(base, PHY_ADDR_ALL, 0x2a, 0x7c00);
			write_mdio_reg(base, PHY_ADDR_SATA1, 0x3, 0x276f);
			write_mdio_reg(base, PHY_ADDR_SATA2, 0x3, 0x276d);
			write_mdio_reg(base, PHY_ADDR_SATA3, 0x3, 0x276d);
		}
	} else if (soc_device_match(rtk_soc_hank)) {
		write_mdio_reg(base, PHY_ADDR_ALL, 0xe, 0x2010);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x6, 0x000e);
		write_mdio_reg(base, PHY_ADDR_ALL, 0xa, 0x8660);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x26, 0x040E);
		write_mdio_reg(base, PHY_ADDR_SATA1, 0x1, 0xE054);
		write_mdio_reg(base, PHY_ADDR_SATA2, 0x1, 0xE048);
		write_mdio_reg(base, PHY_ADDR_SATA3, 0x1, 0xE044);
		write_mdio_reg(base, PHY_ADDR_ALL, 0xd, 0xEF54);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x22, 0x0013);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x23, 0xBB76);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x1b, 0xFF04);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x2, 0x6046);
		write_mdio_reg(base, PHY_ADDR_ALL, 0xb, 0x9904);
	} else if (soc_device_match(rtk_soc_stark)) {
		write_mdio_reg(base, PHY_ADDR_ALL, 0x6, 0x1f);
		write_mdio_reg(base, PHY_ADDR_SATA1, 0xa, 0xb670);
		write_mdio_reg(base, PHY_ADDR_SATA2, 0xa, 0xb670);
		write_mdio_reg(base, PHY_ADDR_SATA3, 0xa, 0xb650);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x1b, 0xff13);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x19, 0x1900);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x26, 0x040e);
		write_mdio_reg(base, PHY_ADDR_SATA1, 0x1, 0xe055);
		write_mdio_reg(base, PHY_ADDR_SATA2, 0x1, 0xe048);
#if defined(MY_DEF_HERE)
		write_mdio_reg(base, PHY_ADDR_SATA3, 0x1, 0x1846);
#else /* MY_DEF_HERE */
		write_mdio_reg(base, PHY_ADDR_SATA3, 0x1, 0xe046);
#endif /* MY_DEF_HERE */
		write_mdio_reg(base, PHY_ADDR_ALL, 0x5, 0x336a);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x9, 0x721c);
		write_mdio_reg(base, PHY_ADDR_ALL, 0xb, 0x9904);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x22, 0x13);
		write_mdio_reg(base, PHY_ADDR_ALL, 0x23, 0xcb66);
	}

	if (port->param_size) {
		for (i = 0; i < port->param_size; i++) {
			reg = port->param_table[i];
			write_mdio_reg(base,
				      (reg >> MDIO_PHYADDR_SHIFT) & 0x3,
				      (reg >> MDIO_REG_SHIFT) & 0x3f,
				      (reg >> MDIO_DATA_SHIFT) & 0xffff);
		}
	} else {
		if (soc_device_match(rtk_soc_kylin)) {
			write_mdio_reg(base, PHY_ADDR_ALL, 0x20, 0x94aa);
			write_mdio_reg(base, PHY_ADDR_ALL, 0x21, 0x88aa);
		} else if (soc_device_match(rtk_soc_hank)) {
			write_mdio_reg(base, PHY_ADDR_SATA1, 0x20, 0x40a5);
			write_mdio_reg(base, PHY_ADDR_SATA2, 0x20, 0x40a5);
			write_mdio_reg(base, PHY_ADDR_SATA3, 0x20, 0x40a8);
			write_mdio_reg(base, PHY_ADDR_SATA1, 0x21, 0x384A);
			write_mdio_reg(base, PHY_ADDR_SATA2, 0x21, 0x385A);
			write_mdio_reg(base, PHY_ADDR_SATA3, 0x21, 0xC88A);
		} else if (soc_device_match(rtk_soc_stark)) {
			for (i = 0; i < PHY_ADDR_ALL; i++) {
				write_mdio_reg(base, i, 0x20, 0x40a0 | swing[i]);
				write_mdio_reg(base, i, 0x1a, 0x1260 | emphasis[i]);
			}
		}
	}

	if (soc_device_match(rtk_soc_stark)) {
		if (!IS_ERR(priv->crt))
			phy_rxidle_adjust(phy);

		rtk_sata_phy_calibration(phy, PHY_ADDR_SATA1);
		rtk_sata_phy_calibration(phy, PHY_ADDR_SATA2);

		reg = read_mdio_reg(base, PHY_ADDR_SATA3, 0x3) & ~(TCALR_MSK << 1);
		reg |= ((tcalr ^ 0x18) << 1);
		write_mdio_reg(base, PHY_ADDR_SATA3, 0x3, reg);

		rtk_sata_phy_fg_offset(phy);

		for (i = 0; i < PHY_ADDR_ALL; i++) {
			reg = read_mdio_reg(base, i, 0x20);
			dev_info(priv->dev, "port%d gen%d tx swing = 0x%x\n",
				port->index, i+1, reg);
			reg = read_mdio_reg(base, i, 0x1a);
			dev_info(priv->dev, "port%d gen%d tx emphasis = 0x%x\n",
				port->index, i+1, reg);
#if defined(MY_DEF_HERE)
			reg = read_mdio_reg(base, i, 0x1);
			dev_info(priv->dev, "port%d gen%d leq default = 0x%x\n",
				port->index, i+1, reg);
#endif /* MY_DEF_HERE */
		}
	}

	return 0;
}

static int rtk_sata_phy_power_on(struct phy *phy)
{
	struct rtk_sata_port *port = phy_get_drvdata(phy);
	struct rtk_sata_phy *priv = dev_get_drvdata(phy->dev.parent);
	unsigned int reg;
	unsigned int reg_phy_ctrl;
	unsigned int phy_en;

	if (soc_device_match(rtk_soc_kylin)) {
		reg_phy_ctrl = REG_SB2_PHY_CTRL;
		phy_en = ((BIT(0) | BIT(2) | BIT(4)) << port->index) | BIT(8);
		regmap_read(priv->phyctl, reg_phy_ctrl, &reg);
		phy_en |= reg;
		regmap_update_bits(priv->phyctl, reg_phy_ctrl, phy_en, phy_en);
	}

	return 0;
}

static int rtk_sata_phy_power_off(struct phy *phy)
{
	struct rtk_sata_port *port = phy_get_drvdata(phy);
	int i;

	for (i = 0; i < PHY_MAX_RST; i++) {
		if (IS_ERR(port->rsts[i]))
			break;
		reset_control_assert(port->rsts[i]);
	}
	return 0;
}

static struct phy_ops rtk_sata_phy_ops = {
	.init = rtk_sata_phy_init,
	.power_on = rtk_sata_phy_power_on,
	.power_off = rtk_sata_phy_power_off,
//	.set_mode = phy_rtk_sata_set_mode,
	.owner = THIS_MODULE,
};

static struct phy *rtk_sata_phy_xlate(struct device *dev, struct of_phandle_args *args)
{
	struct rtk_sata_phy *priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < priv->nphys; i++) {
		if (!priv->phys[i])
			continue;
		if (priv->phys[i]->index == args->args[0])
			break;
	}
	if (i == priv->nphys)
		return ERR_PTR(-ENODEV);

	return priv->phys[i]->phy;
}

static int rtk_sata_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *child;
	struct rtk_sata_phy *priv;
	struct phy_provider *phy_provider;
	struct resource *res;
	unsigned int idx = 0;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	dev_set_drvdata(dev, priv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base)) {
		dev_err(dev, "failed to remap phy base regs\n");
		return PTR_ERR(priv->base);
	}

	priv->phyctl = syscon_regmap_lookup_by_phandle(node, "realtek,phyctl");
	if (IS_ERR(priv->phyctl)) {
		dev_err(dev, "failed to remap phy select regs\n");
		return PTR_ERR(priv->phyctl);
	}

	priv->crt = syscon_regmap_lookup_by_phandle(node, "realtek,crt");
	if (IS_ERR(priv->crt))
		dev_info(dev, "can't find crt regmap\n");


	for (i = 0; i < PHY_MAX_RST; i++) {
		priv->rsts[i] = of_reset_control_get_by_index(dev->of_node, i);
		if (PTR_ERR(priv->rsts[i]) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		if (IS_ERR(priv->rsts[i]))
			break;
	}

	rtk_sata_phy_enable(priv);

	priv->nphys = of_get_child_count(dev->of_node);
	if (priv->nphys == 0 || priv->nphys > RTK_SATA_PHY_NUMBER)
		return -ENODEV;

	priv->phys = devm_kcalloc(dev, priv->nphys, sizeof(*priv->phys), GFP_KERNEL);
	if (!priv->phys)
		return -ENOMEM;

	for_each_available_child_of_node(node, child) {
		struct rtk_sata_port *port;

		port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
		if (!port)
			return -ENOMEM;

		of_property_read_u32(child, "reg", &port->index);

		i = of_property_count_u32_elems(child, "tx-drv");
		if (i > 0) {
			port->param_size = i;
			port->param_table = devm_kzalloc(dev,
					sizeof(unsigned int)*i, GFP_KERNEL);
			if (!port->param_table)
				return -ENOMEM;
			of_property_read_u32_array(child, "tx-drv",
					port->param_table, port->param_size);
		}

		for (i = 0; i < PHY_ADDR_ALL; i++) {
			of_property_read_u32_index(child, "tx-swing", i, &port->tx_swing[i]);
			of_property_read_u32_index(child, "tx-emphasis", i, &port->tx_emphasis[i]);
		}

		for (i = 0; i < PHY_MAX_RST; i++) {
			port->rsts[i] = of_reset_control_get_exclusive_by_index(child, i);
			if (PTR_ERR(port->rsts[i]) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			if (IS_ERR(port->rsts[i]))
				break;
		}

		port->phy = devm_phy_create(dev, child, &rtk_sata_phy_ops);
		if (IS_ERR(port->phy)) {
			dev_err(dev, "failed to create phy %d\n", port->index);
			return PTR_ERR(port->phy);
		}
		phy_set_drvdata(port->phy, port);
		priv->phys[idx++] = port;
	}

	phy_provider = devm_of_phy_provider_register(dev, rtk_sata_phy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

#ifdef CONFIG_PM_SLEEP
static int rtk_sata_phy_suspend(struct device *dev)
{
	struct rtk_sata_phy *priv = dev_get_drvdata(dev);

	rtk_sata_phy_disable(priv);

	return 0;
}

static int rtk_sata_phy_resume(struct device *dev)
{
	struct rtk_sata_phy *priv = dev_get_drvdata(dev);

	rtk_sata_phy_enable(priv);

	return 0;
}

static const struct dev_pm_ops rtk_sata_phy_pm_ops = {
	.suspend = rtk_sata_phy_suspend,
	.resume = rtk_sata_phy_resume,
};
#else
static const struct dev_pm_ops rtk_sata_phy_pm_ops = {};
#endif

static const struct of_device_id rtk_sata_phy_of_match[] = {
	{ .compatible = "realtek,rtk-sata-phy" },
	{ },
};

static struct platform_driver rtk_sata_phy_driver = {
	.probe	= rtk_sata_phy_probe,

	.driver	= {
		.name		= DRV_NAME,
		.of_match_table	= rtk_sata_phy_of_match,
		.pm = &rtk_sata_phy_pm_ops,
	},
};
module_platform_driver(rtk_sata_phy_driver);

MODULE_DESCRIPTION("Realtek SATA PHY driver");
MODULE_AUTHOR("Simon HSU <simon_hsu@realtek.com>");
MODULE_LICENSE("GPL v2");
