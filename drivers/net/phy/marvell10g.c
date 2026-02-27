#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0+
/*
 * Marvell 10G 88x3310 PHY driver
 *
 * Based upon the ID registers, this PHY appears to be a mixture of IPs
 * from two different companies.
 *
 * There appears to be several different data paths through the PHY which
 * are automatically managed by the PHY.  The following has been determined
 * via observation and experimentation for a setup using single-lane Serdes:
 *
 *       SGMII PHYXS -- BASE-T PCS -- 10G PMA -- AN -- Copper (for <= 1G)
 *  10GBASE-KR PHYXS -- BASE-T PCS -- 10G PMA -- AN -- Copper (for 10G)
 *  10GBASE-KR PHYXS -- BASE-R PCS -- Fiber
 *
 * With XAUI, observation shows:
 *
 *        XAUI PHYXS -- <appropriate PCS as above>
 *
 * and no switching of the host interface mode occurs.
 *
 * If both the fiber and copper ports are connected, the first to gain
 * link takes priority and the other port is completely locked out.
 */
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/marvell_phy.h>
#include <linux/phy.h>
#include <linux/sfp.h>
#if defined(MY_DEF_HERE)
#include <linux/netdevice.h>
#endif /* MY_DEF_HERE */

#define MV_PHY_ALASKA_NBT_QUIRK_MASK	0xfffffffe
#define MV_PHY_ALASKA_NBT_QUIRK_REV	(MARVELL_PHY_ID_88X3310 | 0xa)

enum {
	MV_PMA_FW_VER0		= 0xc011,
	MV_PMA_FW_VER1		= 0xc012,
	MV_PMA_BOOT		= 0xc050,
	MV_PMA_BOOT_FATAL	= BIT(0),

	MV_PCS_BASE_T		= 0x0000,
	MV_PCS_BASE_R		= 0x1000,
	MV_PCS_1000BASEX	= 0x2000,

	MV_PCS_CSCR1		= 0x8000,
	MV_PCS_CSCR1_ED_MASK	= 0x0300,
	MV_PCS_CSCR1_ED_OFF	= 0x0000,
	MV_PCS_CSCR1_ED_RX	= 0x0200,
	MV_PCS_CSCR1_ED_NLP	= 0x0300,
	MV_PCS_CSCR1_MDIX_MASK	= 0x0060,
	MV_PCS_CSCR1_MDIX_MDI	= 0x0000,
	MV_PCS_CSCR1_MDIX_MDIX	= 0x0020,
	MV_PCS_CSCR1_MDIX_AUTO	= 0x0060,

	MV_PCS_CSSR1		= 0x8008,
	MV_PCS_CSSR1_SPD1_MASK	= 0xc000,
	MV_PCS_CSSR1_SPD1_SPD2	= 0xc000,
	MV_PCS_CSSR1_SPD1_1000	= 0x8000,
	MV_PCS_CSSR1_SPD1_100	= 0x4000,
	MV_PCS_CSSR1_SPD1_10	= 0x0000,
	MV_PCS_CSSR1_DUPLEX_FULL= BIT(13),
	MV_PCS_CSSR1_RESOLVED	= BIT(11),
	MV_PCS_CSSR1_MDIX	= BIT(6),
	MV_PCS_CSSR1_SPD2_MASK	= 0x000c,
	MV_PCS_CSSR1_SPD2_5000	= 0x0008,
	MV_PCS_CSSR1_SPD2_2500	= 0x0004,
	MV_PCS_CSSR1_SPD2_10000	= 0x0000,

	/* Temperature read register (88E2110 only) */
	MV_PCS_TEMP		= 0x8042,

	MV_PCS_PAIRSWAP		= 0x8182,
	MV_PCS_PAIRSWAP_MASK	= 0x0003,
	MV_PCS_PAIRSWAP_AB	= 0x0002,
	MV_PCS_PAIRSWAP_NONE	= 0x0003,

	/* These registers appear at 0x800X and 0xa00X - the 0xa00X control
	 * registers appear to set themselves to the 0x800X when AN is
	 * restarted, but status registers appear readable from either.
	 */
	MV_AN_CTRL1000		= 0x8000, /* 1000base-T control register */
	MV_AN_STAT1000		= 0x8001, /* 1000base-T status register */

	MV_PHYXS_CTRL1		= 0xf07c, /* 10baseKR AN   */
	MV_PHYXS_CTRL2		= 0xf084, /* 10baseKR AN   */

	/* Vendor2 MMD registers */
	MV_V2_PORT_CTRL		= 0xf001,
	MV_V2_PORT_CTRL_SWRST	= BIT(15),
	MV_V2_PORT_CTRL_PWRDOWN = BIT(11),
	MV_V2_PORT_MAC_TYPE_MASK = 0x7,
	MV_V2_PORT_MAC_TYPE_RATE_MATCH = 0x6,
	/* Temperature control/read registers (88X3310 only) */
	MV_V2_TEMP_CTRL		= 0xf08a,
	MV_V2_TEMP_CTRL_MASK	= 0xc000,
	MV_V2_TEMP_CTRL_SAMPLE	= 0x0000,
	MV_V2_TEMP_CTRL_DISABLE	= 0xc000,
	MV_V2_TEMP		= 0xf08c,
	MV_V2_TEMP_UNKNOWN	= 0x9600, /* unknown function */

#if defined(MY_DEF_HERE)
	/* Vendor2 MMD registers */
	MV_AN_CTRL1_MG		= 0x0020,
	MV_V2_MODE_CFG          = 0xf000,
	MV_V2_LED0_CTRL         = 0xf020,
	MV_V2_LED1_CTRL         = 0xf021,
	MV_V2_LED2_CTRL         = 0xf022,
	MV_V2_LED3_CTRL         = 0xf023,
	MV_V2_PORT_INT_MASK	= 0xf043,
	MV_V2_HOST_KR_ENABLE    = 0xf084,
	MV_V2_MAC_ADDR_LSB	= 0xf06b,
	MV_V2_MAC_ADDR_ISB	= 0xf06c,
	MV_V2_MAC_ADDR_HSB	= 0xf06d,
	MV_V2_WOL_CTRL		= 0xf06e,
	MV_V2_HOST_KR_TUNE      = 0xf07c,
#endif /* MY_DEF_HERE */
};

struct mv3310_priv {
	struct device *hwmon_dev;
	char *hwmon_name;
};

#if defined(MY_DEF_HERE)
/* Some PHYs within the Alaska family like 88x3310 has problems with the
 * KR Auto-negotiation. marvell datasheet for 88x3310 section 6.2.11 says that
 * KR auto-negotitaion can be enabled to adapt to the incoming SERDES by writing
 * to autoneg registers and the PMA/PMD registers
 */
static int mv3310_amd_quirk(struct phy_device *phydev)
{
	int reg=0, count=0;
	int version, subversion;

	version = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, 0xC011);
	subversion = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, 0xC012);
	dev_dbg(&phydev->mdio.dev,"%s: Marvell FW Version: %x.%x \n", __func__, version, subversion);

	reg = phy_read_mmd(phydev, MDIO_MMD_PHYXS, MV_V2_HOST_KR_ENABLE);
	reg |= 0x8000;
	phy_write_mmd(phydev, MDIO_MMD_PHYXS, MV_V2_HOST_KR_ENABLE, reg);

	reg = phy_read_mmd(phydev, MDIO_MMD_PHYXS, MV_V2_HOST_KR_TUNE);
	reg = (reg & ~0x8000) | 0x4000;
	phy_write_mmd(phydev, MDIO_MMD_PHYXS, MV_V2_HOST_KR_TUNE, reg);

	if((reg & BIT(8)) && (reg & BIT(11))) {
		reg = phy_read_mmd(phydev, MDIO_MMD_AN, MV_PCS_BASE_R);

		/* disable BASE-R */
		phy_write_mmd(phydev, MDIO_MMD_AN, MV_PCS_BASE_R, reg);
	} else {
		reg = phy_read_mmd(phydev, MDIO_MMD_AN, MV_PCS_BASE_R);
		/* enable BASE-R for KR initiation */
		reg |= 0x1000;
		phy_write_mmd(phydev, MDIO_MMD_AN, MV_PCS_BASE_R, reg);
	}

	/* down the port if no link */
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_MODE_CFG);
	reg &= 0xFFF7;
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_MODE_CFG, reg);

	/* Do not advertise 2.5Gbe & 5GbE */
	reg = phy_read_mmd(phydev, MDIO_MMD_AN, MV_AN_CTRL1_MG);
	reg &= ~0x0180;
	phy_write_mmd(phydev, MDIO_MMD_AN, MV_AN_CTRL1_MG, reg);

	/* Do not advertise 100M & 10M */
	reg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_ADVERTISE);
	reg &= ~0x01e0;
	phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_ADVERTISE, reg);

	/* SYNO workaround: some model need to delay 1s to avoid no linkup problem */
	msleep(1000);

	/* reset port to effect above change */
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL);
	reg |= 0x8018;
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL, reg);

	/* wait till reset complete */

	count = 50;
	do {
		msleep(10);
		reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL);
	} while ((reg & 0x8000) && --count);

	if(reg & 0x8000){
		dev_err(&phydev->mdio.dev,"%s: Port Reset taking long time\n", __func__);
		return -ETIMEDOUT;
	}

	/* Set LED0 Activtiy Status LED */
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_LED0_CTRL);
	reg &= 0xE000;
	reg |= 0x128;
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_LED0_CTRL, reg);

	/* Set LED2 1GbE Link LED */
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_LED2_CTRL);
	reg &= 0xE000;
	reg |= 0x68;
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_LED2_CTRL, reg);

	/* Set LED3 10GbE Link LED */
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_LED3_CTRL);
	reg &= 0xE000;
	reg |= 0x58;
	phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_LED3_CTRL, reg);

	/* Set PCS, PMA/PMD to normal mode */
	reg = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1);
	reg &= ~0x0800;
	phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	reg = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1);
	reg &= ~0x0800;
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1, reg);

	/* PHY reusme */
	phydev->drv->resume(phydev);

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL);

	dev_err(&phydev->mdio.dev,"%s: quirk applied, 0x%x \n", __func__, reg);

	return 0;
}
#endif /* MY_DEF_HERE */

#ifdef NO_CONFIG_HWMON
static umode_t mv3310_hwmon_is_visible(const void *data,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	if (type == hwmon_chip && attr == hwmon_chip_update_interval)
		return 0444;
	if (type == hwmon_temp && attr == hwmon_temp_input)
		return 0444;
	return 0;
}

static int mv3310_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *value)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int temp;

	if (type == hwmon_chip && attr == hwmon_chip_update_interval) {
		*value = MSEC_PER_SEC;
		return 0;
	}

	if (type == hwmon_temp && attr == hwmon_temp_input) {
		temp = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_TEMP);
		if (temp < 0)
			return temp;

		*value = ((temp & 0xff) - 75) * 1000;

		return 0;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops mv3310_hwmon_ops = {
	.is_visible = mv3310_hwmon_is_visible,
	.read = mv3310_hwmon_read,
};

static u32 mv3310_hwmon_chip_config[] = {
	HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL,
	0,
};

static const struct hwmon_channel_info mv3310_hwmon_chip = {
	.type = hwmon_chip,
	.config = mv3310_hwmon_chip_config,
};

static u32 mv3310_hwmon_temp_config[] = {
	HWMON_T_INPUT,
	0,
};

static const struct hwmon_channel_info mv3310_hwmon_temp = {
	.type = hwmon_temp,
	.config = mv3310_hwmon_temp_config,
};

static const struct hwmon_channel_info *mv3310_hwmon_info[] = {
	&mv3310_hwmon_chip,
	&mv3310_hwmon_temp,
	NULL,
};

static const struct hwmon_chip_info mv3310_hwmon_chip_info = {
	.ops = &mv3310_hwmon_ops,
	.info = mv3310_hwmon_info,
};

static int mv3310_hwmon_config(struct phy_device *phydev, bool enable)
{
	u16 val;
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_TEMP,
			    MV_V2_TEMP_UNKNOWN);
	if (ret < 0)
		return ret;

	val = enable ? MV_V2_TEMP_CTRL_SAMPLE : MV_V2_TEMP_CTRL_DISABLE;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2, MV_V2_TEMP_CTRL,
			      MV_V2_TEMP_CTRL_MASK, val);
}

static void mv3310_hwmon_disable(void *data)
{
	struct phy_device *phydev = data;

	mv3310_hwmon_config(phydev, false);
}

static int mv3310_hwmon_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct mv3310_priv *priv = dev_get_drvdata(&phydev->mdio.dev);
	int i, j, ret;

	priv->hwmon_name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!priv->hwmon_name)
		return -ENODEV;

	for (i = j = 0; priv->hwmon_name[i]; i++) {
		if (isalnum(priv->hwmon_name[i])) {
			if (i != j)
				priv->hwmon_name[j] = priv->hwmon_name[i];
			j++;
		}
	}
	priv->hwmon_name[j] = '\0';

	ret = mv3310_hwmon_config(phydev, true);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, mv3310_hwmon_disable, phydev);
	if (ret)
		return ret;

	priv->hwmon_dev = devm_hwmon_device_register_with_info(dev,
				priv->hwmon_name, phydev,
				&mv3310_hwmon_chip_info, NULL);

	return PTR_ERR_OR_ZERO(priv->hwmon_dev);
}
#else
static inline int mv3310_hwmon_config(struct phy_device *phydev, bool enable)
{
	return 0;
}

static int mv3310_hwmon_probe(struct phy_device *phydev)
{
	return 0;
}
#endif

static int mv3310_probe(struct phy_device *phydev)
{
	struct mv3310_priv *priv;
	u32 mmd_mask = MDIO_DEVS_PMAPMD | MDIO_DEVS_AN;
	int ret;

	if (!phydev->is_c45 ||
	    (phydev->c45_ids.devices_in_package & mmd_mask) != mmd_mask)
		return -ENODEV;

	ret = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MV_PMA_BOOT);
	if (ret < 0)
		return ret;

	if (ret & MV_PMA_BOOT_FATAL) {
		dev_warn(&phydev->mdio.dev,
			 "PHY failed to boot firmware, status=%04x\n", ret);
		return -ENODEV;
	}

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&phydev->mdio.dev, priv);

	ret = mv3310_hwmon_probe(phydev);
	if (ret)
		return ret;

	return 0;
}

static int mv3310_suspend(struct phy_device *phydev)
{
#ifdef MY_DEF_HERE
	int reg=0;

	/* Set PCS, PMA/PMD to low power mode */
	reg = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1);
	reg |= 0x0800;
	phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	reg = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1);
	reg |= 0x0800;
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1, reg);

	/* delay 1s for mtu switching workaround */
	msleep(1000);
#endif /* MY_DEF_HERE */
	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL,
				MV_V2_PORT_CTRL_PWRDOWN);
}

static int mv3310_resume(struct phy_device *phydev)
{
	int ret;
#ifdef MY_DEF_HERE
	int reg=0;

	// /* Set PCS, PMA/PMD to normal mode */
	reg = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1);
	reg &= ~0x0800;
	phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	reg = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1);
	reg &= ~0x0800;
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1, reg);
#endif /* MY_DEF_HERE */

	ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL,
				 MV_V2_PORT_CTRL_PWRDOWN);

	if (ret)
		return ret;

	return mv3310_hwmon_config(phydev, true);
}

/* Some PHYs in the Alaska family such as the 88X3310 and the 88E2010
 * don't set bit 14 in PMA Extended Abilities (1.11), although they do
 * support 2.5GBASET and 5GBASET. For these models, we can still read their
 * 2.5G/5G extended abilities register (1.21). We detect these models based on
 * the PMA device identifier, with a mask matching models known to have this
 * issue
 */
static bool mv3310_has_pma_ngbaset_quirk(struct phy_device *phydev)
{
	if (!(phydev->c45_ids.devices_in_package & MDIO_DEVS_PMAPMD))
		return false;

	/* Only some revisions of the 88X3310 family PMA seem to be impacted */
	return (phydev->c45_ids.device_ids[MDIO_MMD_PMAPMD] &
		MV_PHY_ALASKA_NBT_QUIRK_MASK) == MV_PHY_ALASKA_NBT_QUIRK_REV;
}

static int mv3310_config_init(struct phy_device *phydev)
{
	/* Check that the PHY interface type is compatible */
	if (
#ifdef MY_DEF_HERE
	    phydev->interface != PHY_INTERFACE_MODE_10GKR &&
#endif /* MY_DEF_HERE */
	    phydev->interface != PHY_INTERFACE_MODE_SGMII &&
	    phydev->interface != PHY_INTERFACE_MODE_2500BASEX &&
	    phydev->interface != PHY_INTERFACE_MODE_XAUI &&
	    phydev->interface != PHY_INTERFACE_MODE_RXAUI &&
	    phydev->interface != PHY_INTERFACE_MODE_10GBASER)
		return -ENODEV;

#ifdef MY_DEF_HERE
	mv3310_amd_quirk(phydev);
#else
	/* Power up so reset works */
	err = mv3310_power_up(phydev);
	if (err)
		return err;
#endif /* MY_DEF_HERE */

#ifdef MY_DEF_HERE
	return 0;
#else
	/* Enable EDPD mode - saving 600mW */
	return mv3310_set_edpd(phydev, ETHTOOL_PHY_EDPD_DFLT_TX_MSECS);
#endif /* MY_DEF_HERE */
}

static int mv3310_get_features(struct phy_device *phydev)
{
	int ret, val;

	ret = genphy_c45_pma_read_abilities(phydev);
	if (ret)
		return ret;

	if (mv3310_has_pma_ngbaset_quirk(phydev)) {
		val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD,
				   MDIO_PMA_NG_EXTABLE);
		if (val < 0)
			return val;

		linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
				 phydev->supported,
				 val & MDIO_PMA_NG_EXTABLE_2_5GBT);

		linkmode_mod_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
				 phydev->supported,
				 val & MDIO_PMA_NG_EXTABLE_5GBT);
	}

	return 0;
}

static int mv3310_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	u16 reg;
	int ret;

	/* We don't support manual MDI control */
	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	if (phydev->autoneg == AUTONEG_DISABLE)
		return genphy_c45_pma_setup_forced(phydev);

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	/* Clause 45 has no standardized support for 1000BaseT, therefore
	 * use vendor registers for this mode.
	 */
	reg = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);
	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN, MV_AN_CTRL1000,
			     ADVERTISE_1000FULL | ADVERTISE_1000HALF, reg);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	return genphy_c45_check_and_restart_aneg(phydev, changed);
}

static int mv3310_aneg_done(struct phy_device *phydev)
{
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_BASE_R + MDIO_STAT1);
	if (val < 0)
		return val;

	if (val & MDIO_STAT1_LSTATUS)
		return 1;

	return genphy_c45_aneg_done(phydev);
}

static void mv3310_update_interface(struct phy_device *phydev)
{
	/* In "XFI with Rate Matching" mode the PHY interface is fixed at
	 * 10Gb. The PHY adapts the rate to actual wire speed with help of
	 * internal 16KB buffer.
	 */
	if ((phydev->interface == PHY_INTERFACE_MODE_SGMII ||
	     phydev->interface == PHY_INTERFACE_MODE_2500BASEX ||
	     phydev->interface == PHY_INTERFACE_MODE_10GKR) &&
	    phydev->link) {
		/* The PHY automatically switches its serdes interface (and
		 * active PHYXS instance) between Cisco SGMII, 10GBase-R and
		 * 2500BaseX modes according to the speed.  Florian suggests
		 * setting phydev->interface to communicate this to the MAC.
		 * Only do this if we are already in one of the above modes.
		 */
		switch (phydev->speed) {
		case SPEED_10000:
			phydev->interface = PHY_INTERFACE_MODE_10GKR;
			break;
		case SPEED_2500:
			phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
			break;
		case SPEED_1000:
		case SPEED_100:
		case SPEED_10:
			phydev->interface = PHY_INTERFACE_MODE_SGMII;
			break;
		default:
			break;
		}
	}
}

/* 10GBASE-ER,LR,LRM,SR do not support autonegotiation. */
static int mv3310_read_status_10gbaser(struct phy_device *phydev)
{
	phydev->link = 1;
	phydev->speed = SPEED_10000;
	phydev->duplex = DUPLEX_FULL;

	mv3310_update_interface(phydev);

	return 0;
}

static int mv3310_read_status(struct phy_device *phydev)
{
	int val;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	linkmode_zero(phydev->lp_advertising);
	phydev->link = 0;
	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->mdix = ETH_TP_MDI_INVALID;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_BASE_R + MDIO_STAT1);
	if (val < 0)
		return val;

	if (val & MDIO_STAT1_LSTATUS)
		return mv3310_read_status_10gbaser(phydev);

	val = genphy_c45_read_link(phydev);
	if (val < 0)
 		return val;

	if (val & MDIO_AN_STAT1_COMPLETE) {
 		val = genphy_c45_read_lpa(phydev);
 		if (val < 0)
			return val;

		/* Read the link partner's 1G advertisement */
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MV_AN_STAT1000);
		if (val < 0)
			return val;

		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, val);

		if (phydev->autoneg == AUTONEG_ENABLE)
			phy_resolve_aneg_linkmode(phydev);
 	}

	if (phydev->autoneg != AUTONEG_ENABLE) {
		val = genphy_c45_read_pma(phydev);
		if (val < 0)
			return val;
 	}

	if (phydev->speed == SPEED_10000) {
		val = genphy_c45_read_mdix(phydev);
		if (val < 0)
			return val;
	} else {
		val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_PAIRSWAP);
		if (val < 0)
			return val;
 
		switch (val & MV_PCS_PAIRSWAP_MASK) {
		case MV_PCS_PAIRSWAP_AB:
			phydev->mdix = ETH_TP_MDI_X;
			break;
		case MV_PCS_PAIRSWAP_NONE:
			phydev->mdix = ETH_TP_MDI;
			break;
		default:
			phydev->mdix = ETH_TP_MDI_INVALID;
			break;
		}
	}
 
	// /* Update the pause status */
	// phy_resolve_aneg_pause(phydev);

	mv3310_update_interface(phydev);

	return 0;
}

static int mv3310_soft_reset(struct phy_device *phydev) {
	int ret = 0;
	return ret;
}

#if defined(MY_DEF_HERE)
static int syno_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	int ret, val;

	/* Force PHY not the advertise 10GbE */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, MV_AN_CTRL1_MG);
	val &= ~0x1000;
	ret = phy_write_mmd(phydev, MDIO_MMD_AN, MV_AN_CTRL1_MG, val);
	if (ret) {
		dev_err(&phydev->mdio.dev,"%s: failed to config advertise\n", __func__);
		return ret;
	}

	/* advertise 100M & 10M */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_ADVERTISE);
	val |= 0x01e0;
	ret = phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_ADVERTISE, val);
	if (ret) {
		dev_err(&phydev->mdio.dev,"%s: failed to config advertise\n", __func__);
		return ret;
	}

	/* Enable PHY WOL interrupt */
	val = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_INT_MASK);
	val |= 0x0100;
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_INT_MASK, val);
	if (ret) {
		dev_err(&phydev->mdio.dev,"%s: failed to enable wol int\n", __func__);
		return ret;
	}

	/* Set MAC address for magic packet detection */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_MAC_ADDR_HSB,
			((phydev->attached_dev->dev_addr[5] << 8) |
			 phydev->attached_dev->dev_addr[4]));
	if (ret) {
		dev_err(&phydev->mdio.dev,"%s: failed to set MAC address HSB\n", __func__);
		return ret;
	}
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_MAC_ADDR_ISB,
			((phydev->attached_dev->dev_addr[3] << 8) |
			 phydev->attached_dev->dev_addr[2]));
	if (ret) {
		dev_err(&phydev->mdio.dev,"%s: failed to set MAC address ISB\n", __func__);
		return ret;
	}
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_MAC_ADDR_LSB,
			((phydev->attached_dev->dev_addr[1] << 8) |
			 phydev->attached_dev->dev_addr[0]));
	if (ret) {
		dev_err(&phydev->mdio.dev,"%s: failed to set MAC address LSB\n", __func__);
		return ret;
	}

	/* Magic packet detection enabled */
	val = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_WOL_CTRL);
	val |= 0x01;
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_WOL_CTRL, val);
	if (ret) {
		dev_err(&phydev->mdio.dev,"%s: failed to enable magic packet detection \n", __func__);
		return ret;
	}

	/* Softreset to effect above change */
	val = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL);
	val |= 0x8000;
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL, val);
	if (ret) {
		dev_err(&phydev->mdio.dev,"%s: failed to softreset phy \n", __func__);
		return ret;
	}
	msleep(500);

	return 0;
}
#endif /* MY_DEF_HERE */


static struct phy_driver mv3310_drivers[] = {
	{
		.phy_id		= MARVELL_PHY_ID_88X3310,
		.phy_id_mask	= MARVELL_PHY_ID_MASK,
		.name		= "mv88x3310",
		.get_features	= mv3310_get_features,
		.soft_reset	= mv3310_soft_reset,
		.config_init	= mv3310_config_init,
		.probe		= mv3310_probe,
		.suspend	= mv3310_suspend,
		.resume		= mv3310_resume,
		.config_aneg	= mv3310_config_aneg,
		.aneg_done	= mv3310_aneg_done,
		.read_status	= mv3310_read_status,
#if defined(MY_DEF_HERE)
		.set_wol	= syno_set_wol,
#endif /* MY_DEF_HERE */
	},
	{
		.phy_id		= MARVELL_PHY_ID_88E2110,
		.phy_id_mask	= MARVELL_PHY_ID_MASK,
		.name		= "mv88x2110",
		.probe		= mv3310_probe,
		.suspend	= mv3310_suspend,
		.resume		= mv3310_resume,
		.soft_reset	= mv3310_soft_reset,
		.config_init	= mv3310_config_init,
		.config_aneg	= mv3310_config_aneg,
		.aneg_done	= mv3310_aneg_done,
		.read_status	= mv3310_read_status,
	},
};

module_phy_driver(mv3310_drivers);

static struct mdio_device_id __maybe_unused mv3310_tbl[] = {
	{ MARVELL_PHY_ID_88X3310, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E2110, MARVELL_PHY_ID_MASK },
	{ },
};
MODULE_DEVICE_TABLE(mdio, mv3310_tbl);
MODULE_DESCRIPTION("Marvell Alaska X 10Gigabit Ethernet PHY driver (MV88X3310)");
MODULE_LICENSE("GPL");
