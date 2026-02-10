#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0 OR MIT
/* r8169soc.c: RealTek 8169soc ethernet driver.
 *
 * Copyright (c) 2002 ShuChen <shuchen@realtek.com.tw>
 * Copyright (c) 2003 - 2007 Francois Romieu <romieu@fr.zoreil.com>
 * Copyright (c) 2014 YuKuen Wu <yukuen@realtek.com>
 * Copyright (c) 2015 Eric Wang <ericwang@realtek.com>
 * Copyright (c) 2019 Realtek Semiconductor Corp.
 * Copyright (c) a lot of people too. Please respect their work.
 *
 * See MAINTAINERS file for support contact information.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/version.h>
#include <linux/nvmem-consumer.h>
#include <linux/suspend.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/reset.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sys_soc.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define RTL8169_VERSION "1.3.2"
#define MODULENAME "r8169"
#define PFX MODULENAME ": "
#define CURRENT_MDIO_PAGE 0xFFFFFFFF

#ifdef RTL8169_DEBUG
#define assert(expr)							\
do {									\
	if (!(expr)) {							\
		pr_debug("Assertion failed! %s,%s,%s,line=%d\n",	\
		#expr, __FILE__, __func__, __LINE__);			\
	}								\
} while (0)
#define dprintk(fmt, args...) pr_debug(PFX fmt, ## args)
#else
#define assert(expr) do {} while (0)
#define dprintk(fmt, args...)	do {} while (0)
#endif /* RTL8169_DEBUG */

#define R8169_MSG_DEFAULT \
	(NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN)

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
 * The RTL chips use a 64 element hash table based on the Ethernet CRC.
 */
static const int multicast_filter_limit = 32;

#define MAX_READ_REQUEST_SHIFT	12
#define TX_DMA_BURST	4	/* Maximum DMA burst, '7' is unlimited */
#define INTER_FRAME_GAP	0x03	/* 3 means INTER_FRAME_GAP = the shortest one */

#define R8169_REGS_SIZE		256
#define R8169_NAPI_WEIGHT	64
#define NUM_TX_DESC	1024	/* Number of Tx descriptor registers */
#if defined(CONFIG_RTL_RX_NO_COPY)
#define NUM_RX_DESC	4096	/* Number of Rx descriptor registers */
#else
#define NUM_RX_DESC	1024	/* Number of Rx descriptor registers */
#endif /* CONFIG_RTL_RX_NO_COPY */
#define R8169_TX_RING_BYTES	(NUM_TX_DESC * sizeof(struct tx_desc))
#define R8169_RX_RING_BYTES	(NUM_RX_DESC * sizeof(struct rx_desc))

#define RTL8169_TX_TIMEOUT	(6 * HZ)
#define MAC_INIT_TIMEOUT	20
#define PHY_LOCK_TIMEOUT	1000

#if defined(CONFIG_RTL_RX_NO_COPY)
#define RX_BUF_SIZE	0x05F3	/* 0x05F3 = 1522bye + 1 */
#define RTK_RX_ALIGN	8
#endif /* CONFIG_RTL_RX_NO_COPY */

#define RTL_PROC 1

/* write/read MMIO register */
#define RTL_W8(reg, val8)	(writeb((val8), ioaddr + (reg)))
#define RTL_W16(reg, val16)	(writew((val16), ioaddr + (reg)))
#define RTL_W32(reg, val32)	(writel((val32), ioaddr + (reg)))
#define RTL_R8(reg)		(readb(ioaddr + (reg)))
#define RTL_R16(reg)		(readw(ioaddr + (reg)))
#define RTL_R32(reg)		(readl(ioaddr + (reg)))

enum rtl_tx_desc_version {
	RTL_TD_0	= 0,
	RTL_TD_1	= 1,
};

#define JUMBO_1K	ETH_DATA_LEN
#define JUMBO_4K	(4 * 1024 - ETH_HLEN - 2)
#define JUMBO_6K	(6 * 1024 - ETH_HLEN - 2)
#define JUMBO_7K	(7 * 1024 - ETH_HLEN - 2)
#define JUMBO_9K	(9 * 1024 - ETH_HLEN - 2)

enum cfg_version {
	RTL_CFG_0 = 0x00,
	RTL_CFG_1,
	RTL_CFG_2
};

#if defined(CONFIG_RTL_RX_NO_COPY)
static int rx_buf_sz = 1523;	/* 0x05F3 = 1522bye + 1 */
static int rx_buf_sz_new = 1523;
#else
static int rx_buf_sz = 16383;
#endif /* CONFIG_RTL_RX_NO_COPY */

static struct {
	u32 msg_enable;
} debug = {-1};

enum rtl_registers {
	MAC0				= 0,	/* Ethernet hardware address. */
	MAC4				= 4,
	MAR0				= 8,	/* Multicast filter. */
	COUNTER_ADDR_LOW		= 0x10,
	COUNTER_ADDR_HIGH		= 0x14,
	LEDSEL				= 0x18,
	TX_DESC_START_ADDR_LOW		= 0x20,
	TX_DESC_START_ADDR_HIGH		= 0x24,
	TXH_DESC_START_ADDR_LOW		= 0x28,
	TXH_DESC_START_ADDR_HIGH	= 0x2c,
	FLASH				= 0x30,
	ERSR				= 0x36,
	CHIP_CMD			= 0x37,
	TX_POLL				= 0x38,
	INTR_MASK			= 0x3c,
	INTR_STATUS			= 0x3e,

	TX_CONFIG			= 0x40,
#define	TXCFG_AUTO_FIFO			BIT(7)	/* 8111e-vl */
#define	TXCFG_EMPTY			BIT(11)	/* 8111e-vl */

	RX_CONFIG			= 0x44,
#define	RX128_INT_EN			BIT(15)	/* 8111c and later */
#define	RX_MULTI_EN			BIT(14)	/* 8111c only */
#define	RXCFG_FIFO_SHIFT		13
					/* No threshold before first PCI xfer */
#define	RX_FIFO_THRESH			(7 << RXCFG_FIFO_SHIFT)
#define	RX_EARLY_OFF			BIT(11)
#define	RXCFG_DMA_SHIFT			8
					/* Unlimited maximum PCI burst. */
#define	RX_DMA_BURST			(3 << RXCFG_DMA_SHIFT)	/* 128 bytes */

	RX_MISSED			= 0x4c,
	CFG9346				= 0x50,
	CONFIG0				= 0x51,
	CONFIG1				= 0x52,
	CONFIG2				= 0x53,
#define PME_SIGNAL			BIT(5)	/* 8168c and later */

	CONFIG3				= 0x54,
	CONFIG4				= 0x55,
	CONFIG5				= 0x56,
	MULTI_INTR			= 0x5c,
	PHYAR				= 0x60,
	PHY_STATUS			= 0x6c,
	RX_MAX_SIZE			= 0xda,
	C_PLUS_CMD			= 0xe0,
	INTR_MITIGATE			= 0xe2,
	RX_DESC_ADDR_LOW		= 0xe4,
	RX_DESC_ADDR_HIGH		= 0xe8,
	EARLY_TX_THRES			= 0xec,	/* 8169. Unit of 32 bytes. */

#define NO_EARLY_TX	0x3f	/* Max value : no early transmit. */

	MAX_TX_PACKET_SIZE		= 0xec,	/* Unit of 128 bytes. */

#define TX_PACKET_MAX	(8064 >> 7)
#define EARLY_SIZE	0x27

	FUNC_EVENT			= 0xf0,
	FUNC_EVENT_MASK			= 0xf4,
	FUNC_PRESET_STATE		= 0xf8,
	FUNC_FORCE_EVENT		= 0xfc,
};

enum rtl8168_8101_registers {
	CSIDR			= 0x64,
	CSIAR			= 0x68,
#define	CSIAR_FLAG			0x80000000
#define	CSIAR_WRITE_CMD			0x80000000
#define	CSIAR_BYTE_ENABLE		0x0f
#define	CSIAR_BYTE_ENABLE_SHIFT		12
#define	CSIAR_ADDR_MASK			0x0fff
#define CSIAR_FUNC_CARD			0x00000000
#define CSIAR_FUNC_SDIO			0x00010000
#define CSIAR_FUNC_NIC			0x00020000
	PMCH			= 0x6f,
	EPHYAR			= 0x80,
#define	EPHYAR_FLAG			0x80000000
#define	EPHYAR_WRITE_CMD		0x80000000
#define	EPHYAR_REG_MASK			0x1f
#define	EPHYAR_REG_SHIFT		16
#define	EPHYAR_DATA_MASK		0xffff
	DLLPR			= 0xd0,
#define	PFM_EN				BIT(6)
	DBG_REG			= 0xd1,
#define	FIX_NAK_1			BIT(4)
#define	FIX_NAK_2			BIT(3)
	TWSI			= 0xd2,
	MCU			= 0xd3,
#define	NOW_IS_OOB			BIT(7)
#define	TX_EMPTY			BIT(5)
#define	RX_EMPTY			BIT(4)
#define	RXTX_EMPTY			(TX_EMPTY | RX_EMPTY)
#define	EN_NDP				BIT(3)
#define	EN_OOB_RESET			BIT(2)
#define	LINK_LIST_RDY			BIT(1)
#define	DIS_MCU_CLROOB			BIT(0)
	EFUSEAR			= 0xdc,
#define	EFUSEAR_FLAG			0x80000000
#define	EFUSEAR_WRITE_CMD		0x80000000
#define	EFUSEAR_READ_CMD		0x00000000
#define	EFUSEAR_REG_MASK		0x03ff
#define	EFUSEAR_REG_SHIFT		8
#define	EFUSEAR_DATA_MASK		0xff
};

enum rtl8168_registers {
	LED_FREQ		= 0x1a,
	EEE_LED			= 0x1b,
	ERIDR			= 0x70,
	ERIAR			= 0x74,
#define ERIAR_FLAG			0x80000000
#define ERIAR_WRITE_CMD			0x80000000
#define ERIAR_READ_CMD			0x00000000
#define ERIAR_ADDR_BYTE_ALIGN		4
#define ERIAR_TYPE_SHIFT		16
#define ERIAR_EXGMAC			(0x00 << ERIAR_TYPE_SHIFT)
#define ERIAR_MSIX			(0x01 << ERIAR_TYPE_SHIFT)
#define ERIAR_ASF			(0x02 << ERIAR_TYPE_SHIFT)
#define ERIAR_MASK_SHIFT		12
#define ERIAR_MASK_0001			(0x1 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0010			(0x2 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0011			(0x3 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0100			(0x4 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0101			(0x5 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_1000			(0x8 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_1100			(0xc << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_1111			(0xf << ERIAR_MASK_SHIFT)
	EPHY_RXER_NUM		= 0x7c,
	OCPDR			= 0xb0,	/* OCP GPHY access */
#define OCPDR_WRITE_CMD			0x80000000
#define OCPDR_READ_CMD			0x00000000
#define OCPDR_REG_MASK			0x7fff
#define OCPDR_REG_SHIFT			16
#define OCPDR_DATA_MASK			0xffff
	RDSAR1			= 0xd0,	/* 8168c only. Undocumented on 8168dp */
	MISC			= 0xf0,	/* 8168e only. */
#define TXPLA_RST			BIT(29)
#define DISABLE_LAN_EN			BIT(23) /* Enable GPIO pin */
#define PWM_EN				BIT(22)
#define RXDV_GATED_EN			BIT(19)
#define EARLY_TALLY_EN			BIT(16)
};

enum r8169soc_registers {
	TX_DESC_TAIL_IDX	= 0x20,	/* the last descriptor index */
	TX_DESC_CLOSE_IDX	= 0x22,	/* the closed descriptor index */
#define TX_DESC_CNT_MASK		0x3FFF
#define TX_DESC_CNT_SIZE		0x4000
};

enum rtl_register_content {
	/* InterruptStatusBits */
	SYS_ERR		= 0x8000,
	PCS_TIMEOUT	= 0x4000,
	SW_INT		= 0x0100,
	TX_DESC_UNAVAIL	= 0x0080,
	RX_FIFO_OVER	= 0x0040,
	LINK_CHG	= 0x0020,
	RX_OVERFLOW	= 0x0010,
	TX_ERR		= 0x0008,
	TX_OK		= 0x0004,
	RX_ERR		= 0x0002,
	RX_OK		= 0x0001,

	/* RxStatusDesc */
	RX_BOVF		= BIT(24),
	RX_FOVF		= BIT(23),
	RX_RWT		= BIT(22),
	RX_RES		= BIT(21),
	RX_RUNT		= BIT(20),
	RX_CRC		= BIT(19),

	/* ChipCmdBits */
	STOP_REQ	= 0x80,
	CMD_RESET	= 0x10,
	CMD_RX_ENB	= 0x08,
	CMD_TX_ENB	= 0x04,
	RX_BUF_EMPTY	= 0x01,

	/* TXPoll register p.5 */
	HPQ		= 0x80,		/* Poll cmd on the high prio queue */
	NPQ		= 0x40,		/* Poll cmd on the low prio queue */
	FSW_INT		= 0x01,		/* Forced software interrupt */

	/* Cfg9346Bits */
	CFG9346_LOCK	= 0x00,
	CFG9346_UNLOCK	= 0xc0,

	/* rx_mode_bits */
	ACCEPT_ERR		= 0x20,
	ACCEPT_RUNT		= 0x10,
	ACCEPT_BROADCAST	= 0x08,
	ACCEPT_MULTICAST	= 0x04,
	ACCEPT_MY_PHYS		= 0x02,
	ACCEPT_ALL_PHYS		= 0x01,
#define RX_CONFIG_ACCEPT_MASK		0x3f

	/* TxConfigBits */
	TX_INTER_FRAME_GAP_SHIFT = 24,
	TX_DMA_SHIFT = 8,	/* DMA burst value (0-7)
				 * is shift this many bits
				 */

	/* CONFIG1 register p.24 */
	LEDS1		= BIT(7),
	LEDS0		= BIT(6),
	SPEED_DOWN	= BIT(4),
	MEMMAP		= BIT(3),
	IOMAP		= BIT(2),
	VPD		= BIT(1),
	PM_ENABLE	= BIT(0),	/* Power Management Enable */

	/* CONFIG2 register p. 25 */
	CLK_REQ_EN	= BIT(7),	/* Clock Request Enable */
	MSI_ENABLE	= BIT(5),	/* 8169 only. Reserved in the 8168. */
	PCI_CLOCK_66MHZ = 0x01,
	PCI_CLOCK_33MHZ = 0x00,

	/* CONFIG3 register p.25 */
	MAGIC_PKT	= BIT(5),	/* Wake up when receives a Magic Pkt */
	LINK_UP		= BIT(4),	/* Wake up when the cable connection
					 * is re-established
					 */
	JUMBO_EN0	= BIT(2),	/* 8168 only. Reserved in the 8168b */
	BEACON_EN	= BIT(0),	/* 8168 only. Reserved in the 8168b */

	/* CONFIG4 register */
	JUMBO_EN1	= BIT(1),	/* 8168 only. Reserved in the 8168b */

	/* CONFIG5 register p.27 */
	BWF		= BIT(6),	/* Accept Broadcast wakeup frame */
	MWF		= BIT(5),	/* Accept Multicast wakeup frame */
	UWF		= BIT(4),	/* Accept Unicast wakeup frame */
	SPI_EN		= BIT(3),
	LAN_WAKE	= BIT(1),	/* LAN_WAKE enable/disable */
	PME_STATUS	= BIT(0),	/* PME status can be reset by PCI RST */
	ASPM_EN		= BIT(0),	/* ASPM enable */

	/* C_PLUS_CMD p.31 */
	ENABLE_BIST		= BIT(15),	/* 8168 8101 */
	MAC_DBGO_OE		= BIT(14),	/* 8168 8101 */
	NORMAL_MODE		= BIT(13),	/* unused */
	FORCE_HALF_DUP		= BIT(12),	/* 8168 8101 */
	FORCE_RXFLOW_EN		= BIT(11),	/* 8168 8101 */
	FORCE_TXFLOW_EN		= BIT(10),	/* 8168 8101 */
	CXPL_DBG_SEL		= BIT(9),	/* 8168 8101 */
	ASF			= BIT(8),	/* 8168 8101 */
	PKT_CNTR_DISABLE	= BIT(7),	/* 8168 8101 */
	MAC_DBGO_SEL		= 0x001c,	/* 8168 */
	RX_VLAN			= BIT(6),
	RX_CHK_SUM		= BIT(5),
	PCIDAC			= BIT(4),
	PCI_MUL_RW		= BIT(3),
	INTT_0			= 0x0000,	/* 8168 */
	INTT_1			= 0x0001,	/* 8168 */
	INTT_2			= 0x0002,	/* 8168 */
	INTT_3			= 0x0003,	/* 8168 */

	/* rtl8169_PHYstatus */
	PWR_SAVE_STATUS	= 0x80,
	TX_FLOW_CTRL	= 0x40,
	RX_FLOW_CTRL	= 0x20,
	_1000BPSF	= 0x10,
	_100BPS		= 0x08,
	_10BPS		= 0x04,
	LINK_STATUS	= 0x02,
	FULL_DUP	= 0x01,

	/* DumpCounterCommand */
	COUNTER_DUMP	= 0x8,
};

enum rtl_desc_bit {
	/* First doubleword. */
	DESC_OWN	= BIT(31), /* Descriptor is owned by NIC */
	RING_END	= BIT(30), /* End of descriptor ring */
	FIRST_FRAG	= BIT(29), /* First segment of a packet */
	LAST_FRAG	= BIT(28), /* Final segment of a packet */
};

/* Generic case. */
enum rtl_tx_desc_bit {
	/* First doubleword. */
	TD_LSO		= BIT(27),		/* Large Send Offload */
#define TD_MSS_MAX			0x07ffu	/* MSS value */

	/* Second doubleword. */
	TX_VLAN_TAG	= BIT(17),		/* Add VLAN tag */
};

/* 8169, 8168b and 810x except 8102e. */
enum rtl_tx_desc_bit_0 {
	/* First doubleword. */
#define TD0_MSS_SHIFT			16	/* MSS position (11 bits) */
	TD0_TCP_CS	= BIT(16),		/* Calculate TCP/IP checksum */
	TD0_UDP_CS	= BIT(17),		/* Calculate UDP/IP checksum */
	TD0_IP_CS	= BIT(18),		/* Calculate IP checksum */
};

/* 8102e, 8168c and beyond. */
enum rtl_tx_desc_bit_1 {
	/* Second doubleword. */
#define TD1_MSS_SHIFT			18	/* MSS position (11 bits) */
	TD1_IP_CS	= BIT(29),		/* Calculate IP checksum */
	TD1_TCP_CS	= BIT(30),		/* Calculate TCP/IP checksum */
	TD1_UDP_CS	= BIT(31),		/* Calculate UDP/IP checksum */
};

static const struct rtl_tx_desc_info {
	struct {
		u32 udp;
		u32 tcp;
	} checksum;
	u16 mss_shift;
	u16 opts_offset;
} tx_desc_info = {
	.checksum = {
		.udp	= TD1_IP_CS | TD1_UDP_CS,
		.tcp	= TD1_IP_CS | TD1_TCP_CS
	},
	.mss_shift	= TD1_MSS_SHIFT,
	.opts_offset	= 1
};

enum rtl_rx_desc_bit {
	/* Rx private */
	PID1		= BIT(18), /* Protocol ID bit 1/2 */
	PID0		= BIT(17), /* Protocol ID bit 2/2 */

#define RX_PROTO_UDP	(PID1)
#define RX_PROTO_TCP	(PID0)
#define RX_PROTO_IP	(PID1 | PID0)
#define RX_PROTO_MASK	RX_PROTO_IP

	IP_FAIL		= BIT(16), /* IP checksum failed */
	UDP_FAIL	= BIT(15), /* UDP/IP checksum failed */
	TCP_FAIL	= BIT(14), /* TCP/IP checksum failed */
	RX_VLAN_TAG	= BIT(16), /* VLAN tag available */
};

#define RSVD_MASK	0x3fffc000

struct tx_desc {
	__le32 opts1;
	__le32 opts2;
	__le64 addr;
};

struct rx_desc {
	__le32 opts1;
	__le32 opts2;
	__le64 addr;
};

struct ring_info {
	struct sk_buff	*skb;
	u32		len;
	u8		__pad[sizeof(void *) - sizeof(u32)];
};

enum features {
	RTL_FEATURE_WOL		= BIT(0),
	RTL_FEATURE_MSI		= BIT(1),
	RTL_FEATURE_GMII	= BIT(2),
	RTL_FEATURE_TX_NO_CLOSE	= BIT(3),
	RTL_FEATURE_RX_NO_COPY	= BIT(4),
	RTL_FEATURE_ADJUST_FIFO	= BIT(5),
	RTL_FEATURE_ACP		= BIT(6),
	RTL_FEATURE_EEE		= BIT(7),
	RTL_FEATURE_OCP_MDIO	= BIT(8),
	RTL_FEATURE_PAT_WAKE	= BIT(9),
};

struct rtl8169_counters {
	__le64	tx_packets;
	__le64	rx_packets;
	__le64	tx_errors;
	__le32	rx_errors;
	__le16	rx_missed;
	__le16	align_errors;
	__le32	tx_one_collision;
	__le32	tx_multi_collision;
	__le64	rx_unicast;
	__le64	rx_broadcast;
	__le32	rx_multicast;
	__le16	tx_aborted;
	__le16	tx_underrun;
};

enum rtl_flag {
	RTL_FLAG_TASK_ENABLED,
	RTL_FLAG_TASK_SLOW_PENDING,
	RTL_FLAG_TASK_RESET_PENDING,
	RTL_FLAG_TASK_PHY_PENDING,
	RTL_FLAG_MAX
};

struct rtl8169_stats {
	u64			packets;
	u64			bytes;
	struct u64_stats_sync	syncp;
};

enum rtl_output_mode {
	OUTPUT_EMBEDDED_PHY,
	OUTPUT_RGMII_TO_MAC,
	OUTPUT_RGMII_TO_PHY,
	OUTPUT_SGMII_TO_MAC,
	OUTPUT_SGMII_TO_PHY,
	OUTPUT_RMII,
	OUTPUT_FORCE_LINK,
	OUTPUT_MAX
};

enum drv_status {
	RTL_STATUS_DOWN		= BIT(0),
};

/* ISO base addr 0x98007000 */
enum common_iso_registers {
	ISO_UMSK_ISR			= 0x0004,
	ISO_PWRCUT_ETN			= 0x005c,
	ISO_ETN_TESTIO			= 0x0060,
	ISO_PLL_WDOUT			= 0x0070,
	ISO_SOFT_RESET			= 0x0088,
	ISO_CLOCK_ENABLE		= 0x008c,
	ISO_POR_CTRL			= 0x0210,
	ISO_POR_VTH			= 0x0214,
	ISO_POR_DATAI			= 0x0218,
};

#define POR_XV_MASK	0x00000111
#define POR_NUM		3

/* RTD119X */
enum rtd119x_iso_registers {
	RTD119X_ISO_MUXPAD0		= 0x0310,
};

/* RTD129X */
/* ISO base addr 0x98007000 */
enum rtd129x_iso_registers {
	RTD129X_ISO_RGMII_MDIO_TO_GMAC	= 0x0064,
	RTD129X_ISO_MUXPAD0		= 0x0310,
	RTD129X_ISO_DBUS_CTRL		= 0x0fc0,
};

/* SB2 base addr 0x9801a000 */
enum rtd129x_sb2_registers {
	RTD129X_SB2_PFUNC_RG0		= 0x0960,
	RTD129X_SB2_PFUNC_RG1		= 0x0964,
	RTD129X_SB2_PFUNC_RG2		= 0x0968,
};

enum rtd129x_rgmii_voltage {
	RTD129X_VOLTAGE_1_DOT_8V = 1,
	RTD129X_VOLTAGE_2_DOT_5V,
	RTD129X_VOLTAGE_3_DOT_3V,
	RTD129X_VOLTAGE_MAX
};

enum rtd129x_rgmii_delay {
	RTD129X_RGMII_DELAY_0NS,
	RTD129X_RGMII_DELAY_2NS,
	RTD129X_RGMII_DELAY_MAX
};

/* RTD139X */
#define RTD139X_R_K_DEFAULT		0x8
#define RTD139X_IDAC_FINE_DEFAULT	0x33

/* SDS base addr 0x981c8000 */
enum rtd139x_sds_registers {
	RTD139X_SDS_REG02	= 0x0008,
	RTD139X_SDS_REG28	= 0x0070,
	RTD139X_SDS_REG29	= 0x0074,
	RTD139X_SDS_MISC	= 0x1804,
	RTD139X_SDS_LINK	= 0x1810,
};

/* ISO testmux base addr 0x9804e000 */
enum rtd139x_testmux_registers {
	RTD139X_ISO_TESTMUX_MUXPAD0	= 0x0000,
	RTD139X_ISO_TESTMUX_MUXPAD1	= 0x0004,
	RTD139X_ISO_TESTMUX_MUXPAD2	= 0x0008,
};

/* SBX base addr 0x9801c000 */
enum rtd139x_sbx_registers {
	RTD139X_SBX_SB3_CHANNEL_REQ_MASK	= 0x020c,
	RTD139X_SBX_SB3_CHANNEL_REQ_BUSY	= 0x0210,
	RTD139X_SBX_ACP_CHANNEL_REQ_MASK	= 0x080c,
	RTD139X_SBX_ACP_CHANNEL_REQ_BUSY	= 0x0810,
	RTD139X_SBX_ACP_MISC_CTRL		= 0x0814,
};

/* SCPU_WRAPPER base addr 0x9801d000 */
enum rtd139x_sc_wrap_registers {
	RTD139X_SC_WRAP_ACP_CRT_CTRL		= 0x0030,
	RTD139X_SC_WRAP_CRT_CTRL		= 0x0100,
	RTD139X_SC_WRAP_INTERFACE_EN		= 0x0124,
	RTD139X_SC_WRAP_ACP_CTRL		= 0x0800,
};

enum rtd139x_phy_addr_e {
	/* embedded PHY PHY ID */
	RTD139X_INT_PHY_ADDR		= 1,
	/* embedded SerDes DPHY PHY ID 0, RL6481_T28_SGMII.doc ANA00~ANA0F */
	RTD139X_SERDES_DPHY_0		= 0,
	/* embedded SerDes DPHY PHY ID 1, RL6481_T28_SGMII.doc ANA20~ANA2F */
	RTD139X_SERDES_DPHY_1		= 1,
	/* external RTL8211FS SGMII PHY ID */
	RTD139X_EXT_PHY_ADDR		= 3,
};

enum rtd139x_sgmii_swing_e {
	RTD139X_TX_SWING_1040MV		= (0X0 << 8),	/* DEFAULT */
	RTD139X_TX_SWING_693MV		= (0X1 << 8),
	RTD139X_TX_SWING_474MV		= (0X2 << 8),
	RTD139X_TX_SWING_352MV		= (0X3 << 8),
	RTD139X_TX_SWING_312MV		= (0X4 << 8),
};

#define RTD139X_SGMII_SWING		(0X3 << 8)

/* RTD16XX */
#define RTD16XX_RC_K_DEFAULT		0x8888
#define RTD16XX_R_K_DEFAULT		0x8888
#define RTD16XX_AMP_K_DEFAULT		0x7777
#define RTD16XX_ADC_BIAS_K_DEFAULT	0x8888

/* SDS base addr 0x981c8000 */
enum rtd16xx_sds_registers {
	RTD16XX_SDS_REG02	= 0x0008,
	RTD16XX_SDS_MISC	= 0x1804,
	RTD16XX_SDS_LINK	= 0x180c,
	RTD16XX_SDS_DEBUG	= 0x1810,
};

/* ISO testmux base addr 0x9804e000 */
enum rtd16xx_testmux_registers {
	RTD16XX_ISO_TESTMUX_MUXPAD0	= 0x0000,
	RTD16XX_ISO_TESTMUX_MUXPAD1	= 0x0004,
	RTD16XX_ISO_TESTMUX_MUXPAD2	= 0x0008,
};

/* SBX base addr 0x9801c000 */
enum rtd16xx_sbx_registers {
	RTD16XX_SBX_SB3_CHANNEL_REQ_MASK	= 0x020c,
	RTD16XX_SBX_SB3_CHANNEL_REQ_BUSY	= 0x0210,
	RTD16XX_SBX_ACP_CHANNEL_REQ_MASK	= 0x080c,
	RTD16XX_SBX_ACP_CHANNEL_REQ_BUSY	= 0x0810,
	RTD16XX_SBX_ACP_MISC_CTRL		= 0x0814,
};

/* SCPU_WRAPPER base addr 0x9801d000 */
enum rtd16xx_sc_wrap_registers {
	RTD16XX_SC_WRAP_ACP_CRT_CTRL		= 0x0030,
	RTD16XX_SC_WRAP_CRT_CTRL		= 0x0100,
	RTD16XX_SC_WRAP_INTERFACE_EN		= 0x0124,
	RTD16XX_SC_WRAP_ACP_CTRL		= 0x0800,
};

enum rtd16xx_phy_addr_e {
	/* embedded PHY PHY ID */
	RTD16XX_INT_PHY_ADDR		= 1,
	/* embedded SerDes DPHY PHY ID 0, RL6481_T28_SGMII.doc ANA00~ANA0F */
	RTD16XX_SERDES_DPHY_0		= 0,
	/* embedded SerDes DPHY PHY ID 1, RL6481_T28_SGMII.doc ANA20~ANA2F */
	RTD16XX_SERDES_DPHY_1		= 1,
	/* external RTL8211FS SGMII PHY ID */
	RTD16XX_EXT_PHY_ADDR		= 3,
};

enum rtd16xx_sgmii_swing_e {
	RTD16XX_TX_SWING_550MV,
	RTD16XX_TX_SWING_380MV,
	RTD16XX_TX_SWING_250MV,
	RTD16XX_TX_SWING_190MV
};

/* RTD13XX */
#define RTD13XX_R_K_DEFAULT		0x8
#define RTD13XX_IDAC_FINE_DEFAULT	0x77

/* ISO testmux base addr 0x9804e000 */
enum rtd13xx_testmux_registers {
	RTD13XX_ISO_TESTMUX_MUXPAD0	= 0x0000,
	RTD13XX_ISO_TESTMUX_MUXPAD1	= 0x0004,
	RTD13XX_ISO_TESTMUX_MUXPAD2	= 0x0008,
	RTD13XX_ISO_TESTMUX_MUXPAD5	= 0x0014,
	RTD13XX_ISO_TESTMUX_MUXPAD6	= 0x0018,
	RTD13XX_ISO_TESTMUX_PFUNC9	= 0x0040, /* MDC/MDIO current */
	RTD13XX_ISO_TESTMUX_PFUNC20	= 0x006c, /* RGMII current */
	RTD13XX_ISO_TESTMUX_PFUNC21	= 0x0070, /* RGMII current */
	RTD13XX_ISO_TESTMUX_PFUNC25	= 0x0090, /* RGMII BIAS */
};

/* SBX base addr 0x9801c000 */
enum rtd13xx_sbx_registers {
	RTD13XX_SBX_SB3_CHANNEL_REQ_MASK	= 0x020c,
	RTD13XX_SBX_SB3_CHANNEL_REQ_BUSY	= 0x0210,
	RTD13XX_SBX_ACP_CHANNEL_REQ_MASK	= 0x080c,
	RTD13XX_SBX_ACP_CHANNEL_REQ_BUSY	= 0x0810,
	RTD13XX_SBX_ACP_MISC_CTRL		= 0x0814,
};

/* SCPU_WRAPPER base addr 0x9801d000 */
enum rtd13xx_sc_wrap_registers {
	RTD13XX_SC_WRAP_ACP_CRT_CTRL		= 0x0030,
	RTD13XX_SC_WRAP_CRT_CTRL		= 0x0100,
	RTD13XX_SC_WRAP_INTERFACE_EN		= 0x0124,
	RTD13XX_SC_WRAP_ACP_CTRL		= 0x0800,
};

enum rtd13xx_phy_addr_e {
	/* embedded PHY PHY ID */
	RTD13XX_INT_PHY_ADDR		= 1,
	/* external PHY ID */
	RTD13XX_EXT_PHY_ADDR		= 1,
};

/* RTD16XXB */
#define RTD16XXB_R_K_DEFAULT		0x8
#define RTD16XXB_AMP_K_DEFAULT		0x7777
#define RTD16XXB_RC_K_DEFAULT		0x8888

/* ISO testmux base addr 0x9804e000 */
enum rtd16xxb_testmux_registers {
	RTD16XXB_ISO_TESTMUX_MUXPAD2	= 0x0008, /* LED */
	RTD16XXB_ISO_TESTMUX_PFUNC12	= 0x0050, /* MDC/MDIO current */
};

enum rtd16xxb_phy_addr_e {
	/* embedded PHY PHY ID */
	RTD16XXB_INT_PHY_ADDR		= 1,
	/* external PHY ID */
	RTD16XXB_EXT_PHY_ADDR		= 1,
};

/* end of register locations per chip */

#ifdef RTL_PROC
static struct proc_dir_entry *rtw_proc;
#endif
/* wol_enable
 * BIT 0: WoL enable
 * BIT 1: CRC match
 * BIT 2: WPD
 */
enum wol_flags {
	WOL_MAGIC			= 0x1,
	WOL_CRC_MATCH			= 0x2,
	WOL_WPD				= 0x4,
};

#define WOL_BUF_LEN		128

/* CRC WAKE UP supports 16 rules */
/* EXACTLY PATTERN WAKE UP supports 32 rules */
#define RTL_WAKE_SIZE		32
#define RTL_WAKE_SIZE_CRC	16

/* CRC WAKE UP supports 128-bit mask (16 bytes) */
/* EXACTLY PATTERN WAKE UP supports 128-bit mask + 8-bit padding (17 bytes) */
#define RTL_WAKE_MASK_SIZE	17
#define RTL_WAKE_MASK_SIZE_CRC	16

#define RTL_WAKE_MASK_REG_SIZE	32
#define RTL_WAKE_PATTERN_SIZE	136
struct rtl_wake_rule_s {
	u8 flag;
		#define WAKE_FLAG_ENABLE	0x01
	u8 mask_size; /* 0 ~ 17, include padding */
	u8 pattern_size; /* 0 ~ 136, include padding */
	u16 crc; /* CRC-16 */
	u16 offset; /* 0 ~ 1535 */
	u8 mask[RTL_WAKE_MASK_SIZE];
	u8 pattern[RTL_WAKE_PATTERN_SIZE];
};

struct rtl8169_rgmii_info {
	u8 voltage; /* 1:1.8V, 2: 2.5V, 3:3.3V */
	u8 tx_delay; /* 0: 0ns, 1: 2ns */
	u8 rx_delay; /* 0: 0ns, 1: 2ns */
};

struct rtl8169_sgmii_info {
	u8 swing; /* 0:640mV, 1:380mV, 2:250mV, 3:190mV */
};

struct rtl8169_rmii_info {
	u8 voltage; /* 1:1.8V, 2: 2.5V, 3:3.3V */
	u8 tx_delay; /* 0: 0ns, 1: 2ns */
	u8 rx_delay; /* 0 ~ 7 x 0.5ns */
};

struct rtl8169_private {
	void __iomem *mmio_addr;	/* memory map physical address */
	struct regmap *iso_base;
	struct regmap *sb2_base;
	struct regmap *sbx_base;
	struct regmap *scpu_wrap_base;
	struct regmap *pinctrl_base;
	struct regmap *sds_base;

	struct platform_device *pdev;
	struct net_device *dev;
	struct napi_struct napi;
	u32 msg_enable;
	u16 txd_version;
	u16 mac_version;
	u32 cur_rx; /* Index into the Rx descriptor buffer of next Rx pkt. */
	u32 cur_tx; /* Index into the Tx descriptor buffer of next Rx pkt. */
	u32 dirty_tx;

#if defined(CONFIG_RTL_RX_NO_COPY)
	u32 dirty_rx;
	u32 last_dirty_tx;
	u32 tx_reset_count;
	u32 last_cur_rx;
	u32 rx_reset_count;
	u8 check_rdu;
#endif				/* CONFIG_RTL_RX_NO_COPY */

	struct rtl8169_stats rx_stats;
	struct rtl8169_stats tx_stats;
	struct tx_desc *tx_desc_array;	/* 256-aligned Tx descriptor ring */
	struct rx_desc *rx_desc_array;	/* 256-aligned Rx descriptor ring */
	dma_addr_t tx_phy_addr;
	dma_addr_t rx_phy_addr;

	/* Rx data buffers */
	#if defined(CONFIG_RTL_RX_NO_COPY)
	struct sk_buff *rx_databuff[NUM_RX_DESC]; /* RTL_FEATURE_RX_NO_COPY */
	#else
	void *rx_databuff[NUM_RX_DESC];
	#endif /* CONFIG_RTL_RX_NO_COPY */

	struct ring_info tx_skb[NUM_TX_DESC];	/* Tx data buffers */
	u16 cp_cmd;

	u16 event_slow;

#ifdef RTL_PROC
	struct proc_dir_entry *dir_dev;
#endif

	struct {
		DECLARE_BITMAP(flags, RTL_FLAG_MAX);
		struct mutex mutex; /* mutex for work */
		struct work_struct work;
	} wk;

	unsigned int features;

	struct mii_if_info mii;
	struct rtl8169_counters counters;
	u32 saved_wolopts;
	u32 opts1_mask;

	u8 wol_enable;
	u8 wol_crc_cnt;
	struct rtl_wake_rule_s wol_rule[RTL_WAKE_SIZE];
	struct rtl_wake_rule_s wol_rule_buf;

	u32 ocp_base;
	u32 led_cfg;

	struct r8169soc_chip_info *chip;
	u32 phy_irq[POR_NUM];
	u8 phy_irq_map[POR_NUM];
	u8 phy_irq_num;
	u32 phy_por_xv_mask;
	atomic_t phy_reinit_flag;
	enum rtl_output_mode output_mode;
	union {
		struct rtl8169_rgmii_info rgmii;
		struct rtl8169_sgmii_info sgmii;
		struct rtl8169_rmii_info rmii;
	};

	bool bypass_enable;	/* 0: disable, 1: enable */
	u8 ext_phy_id;		/* 0 ~ 31 */
	bool eee_enable;	/* 0: disable, 1: enable */
	bool acp_enable;	/* 0: disable, 1: enable */
	bool ext_phy;
	bool pwr_saving;	/* power saving of suspend mode */
	bool netif_is_running;
	u32 status;
};

#define REVISION_A00	0xA00
#define REVISION_A01	0xA01
#define REVISION_A02	0xA02
#define REVISION_B00	0xB00
#define REVISION_B01	0xB01
#define REVISION_B02	0xB02
#define REVISION_NONE	0xFFF

struct r8169soc_chip_info {
	const char *name;
	void (*mdio_write)(struct rtl8169_private *tp, int page, int reg, int value);
	int (*mdio_read)(struct rtl8169_private *tp, int page, int reg);
	void (*mmd_write)(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr, u32 value);
	u32 (*mmd_read)(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr);
	void (*mdio_lock)(struct rtl8169_private *tp);
	void (*mdio_unlock)(struct rtl8169_private *tp);
	void (*pll_power_down)(struct rtl8169_private *tp);
	void (*pll_power_up)(struct rtl8169_private *tp);
	void (*csi_write)(struct rtl8169_private *tp, int addr, int value);
	u32 (*csi_read)(struct rtl8169_private *tp, int addr);
	int (*set_speed)(struct net_device *dev, u8 aneg, u16 sp, u8 dpx,
			 u32 adv);
	void (*phy_reset_enable)(struct rtl8169_private *tp);
	u32 (*phy_reset_pending)(struct rtl8169_private *tp);
	u32 (*link_ok)(void __iomem *ioaddr);
	int (*do_ioctl)(struct rtl8169_private *tp,
			struct mii_ioctl_data *data, int cmd);
	void (*hw_start)(struct net_device *dev);
	void (*reset_phy_gmac)(struct rtl8169_private *tp);
	void (*acp_init)(struct rtl8169_private *tp);
	void (*pll_clock_init)(struct rtl8169_private *tp);
	void (*mdio_init)(struct rtl8169_private *tp);
	void (*mac_mcu_patch)(struct rtl8169_private *tp);
	void (*hw_phy_config)(struct rtl8169_private *tp);
	void (*wakeup_set)(struct rtl8169_private *tp, bool enable);
	void (*eee_set)(struct rtl8169_private *tp, bool enable);
	void (*led_set)(struct rtl8169_private *tp, bool enable);
	void (*dump_regs)(struct seq_file *m, struct rtl8169_private *tp);
	void (*dump_var)(struct seq_file *m, struct rtl8169_private *tp);

	int mac_version;
	int cfg_version;
	enum rtl_tx_desc_version txd_version;
	u32 led_cfg;
	u32 region;
	u32 align;
	u32 features;
	u16 event_slow;
	u16 jumbo_max;
	bool jumbo_tx_csum;
};

MODULE_AUTHOR("Realtek and the Linux r8169soc crew <netdev@vger.kernel.org>");
MODULE_DESCRIPTION("RealTek RTL-8169soc Gigabit Ethernet driver");
module_param_named(debug, debug.msg_enable, int, 0644);

MODULE_PARM_DESC(debug, "Debug verbosity level (0=none, ..., 16=all)");
MODULE_LICENSE("GPL");
MODULE_VERSION(RTL8169_VERSION);

static void rtl_lock_work(struct rtl8169_private *tp)
{
	mutex_lock(&tp->wk.mutex);
}

static void rtl_unlock_work(struct rtl8169_private *tp)
{
	mutex_unlock(&tp->wk.mutex);
}

struct rtl_cond {
	bool (*check)(struct rtl8169_private *tp);
	const char *msg;
};

static void rtl_udelay(unsigned int d)
{
	usleep_range(d, d + 1);
}

static bool rtl_loop_wait(struct rtl8169_private *tp, const struct rtl_cond *c,
			  void (*delay)(unsigned int),
			  unsigned int d, int n, bool high)
{
	int i;

	for (i = 0; i < n; i++) {
		delay(d);
		if (c->check(tp) == high)
			return true;
	}
	netif_err(tp, drv, tp->dev, "%s == %d (loop: %d, delay: %d).\n",
		  c->msg, !high, n, d);
	return false;
}

static bool rtl_udelay_loop_wait_high(struct rtl8169_private *tp,
				      const struct rtl_cond *c,
				      unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, rtl_udelay, d, n, true);
}

static bool rtl_udelay_loop_wait_low(struct rtl8169_private *tp,
				     const struct rtl_cond *c,
				     unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, rtl_udelay, d, n, false);
}

static __maybe_unused bool
rtl_msleep_loop_wait_high(struct rtl8169_private *tp,
			  const struct rtl_cond *c, unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, msleep, d, n, true);
}

static bool rtl_msleep_loop_wait_low(struct rtl8169_private *tp,
				     const struct rtl_cond *c,
				     unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, msleep, d, n, false);
}

#define DECLARE_RTL_COND(name)				\
static bool name ## _check(struct rtl8169_private *);	\
							\
static const struct rtl_cond name = {			\
	.check	= name ## _check,			\
	.msg	= #name					\
};							\
							\
static bool name ## _check(struct rtl8169_private *tp)

DECLARE_RTL_COND(rtl_eriar_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(ERIAR) & ERIAR_FLAG;
}

static bool rtl_ocp_reg_failure(struct rtl8169_private *tp, u32 reg)
{
	if (reg & 0xffff0001) {
		netif_err(tp, drv, tp->dev, "Invalid ocp reg %x!\n", reg);
		return true;
	}
	return false;
}

static void rtl_ocp_write(struct rtl8169_private *tp, u32 reg, u32 value)
{
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned int wdata;

	if (rtl_ocp_reg_failure(tp, reg))
		return;

	wdata = OCPDR_WRITE_CMD |
		(((reg >> 1) & OCPDR_REG_MASK) << OCPDR_REG_SHIFT) |
		(value & OCPDR_DATA_MASK);
	RTL_W32(OCPDR, wdata);
}

static u32 rtl_ocp_read(struct rtl8169_private *tp, u32 reg)
{
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned int wdata;
	unsigned int rdata;

	if (rtl_ocp_reg_failure(tp, reg))
		return 0;

	wdata = OCPDR_READ_CMD |
		(((reg >> 1) & OCPDR_REG_MASK) << OCPDR_REG_SHIFT);
	RTL_W32(OCPDR, wdata);
	rdata = RTL_R32(OCPDR);
	return (rdata & OCPDR_DATA_MASK);
}

#define OCP_STD_PHY_BASE	0xa400

static __maybe_unused void mac_mcu_write(struct rtl8169_private *tp, int reg,
					 int value)
{
	if (reg == 0x1f) {
		tp->ocp_base = value << 4;
		return;
	}

	rtl_ocp_write(tp, tp->ocp_base + reg, value);
}

static __maybe_unused int mac_mcu_read(struct rtl8169_private *tp, int reg)
{
	return rtl_ocp_read(tp, tp->ocp_base + reg);
}

DECLARE_RTL_COND(rtl_int_phyar_cond)
{
	return rtl_ocp_read(tp, 0xDE0A) & BIT(14);
}

DECLARE_RTL_COND(rtl_phyar_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(PHYAR) & 0x80000000;
}

static void __int_set_phy_addr(struct rtl8169_private *tp, int phy_addr)
{
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				GENMASK(20, 16), (phy_addr << 16),
				NULL, false, true);
}

static inline int __int_mdio_read(struct rtl8169_private *tp, int reg)
{
	int value;
	void __iomem *ioaddr = tp->mmio_addr;

	/* read reg */
	RTL_W32(PHYAR, 0x0 | (reg & 0x1f) << 16);

	value = rtl_udelay_loop_wait_high(tp, &rtl_phyar_cond, 25, 20) ?
		RTL_R32(PHYAR) & 0xffff : ~0;

	/* According to hardware specs a 20us delay is required after read
	 * complete indication, but before sending next command.
	 */
	usleep_range(20, 21);

	return value;
}

static inline void __int_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* write reg */
	RTL_W32(PHYAR, 0x80000000 | (reg & 0x1f) << 16 | (value & 0xffff));

	rtl_udelay_loop_wait_low(tp, &rtl_phyar_cond, 25, 20);
	/* According to hardware specs a 20us delay is required after write
	 * complete indication, but before sending next command.
	 */
	usleep_range(20, 21);
}

static int int_mdio_read(struct rtl8169_private *tp, int page, int reg)
{
	int value;

	tp->chip->mdio_lock(tp);

	/* write page */
	if (page != CURRENT_MDIO_PAGE)
		__int_mdio_write(tp, 0x1f, page);

	/* read reg */
	value = __int_mdio_read(tp, reg);

	tp->chip->mdio_unlock(tp);
	return value;
}

static void int_mdio_write(struct rtl8169_private *tp, int page, int reg, int value)
{
	tp->chip->mdio_lock(tp);

	/* write page */
	if (page != CURRENT_MDIO_PAGE)
		__int_mdio_write(tp, 0x1f, page);

	/* write reg */
	__int_mdio_write(tp, reg, value);

	tp->chip->mdio_unlock(tp);
}

static int int_ocp_mdio_read(struct rtl8169_private *tp, int page, int reg)
{
	int value;

	rtl_ocp_write(tp, 0xDE0C, page);
	rtl_ocp_write(tp, 0xDE0A, reg & 0x1f);

	value = rtl_udelay_loop_wait_low(tp, &rtl_int_phyar_cond, 25, 20) ?
		rtl_ocp_read(tp, 0xDE08) & 0xffff : ~0;

	return value;
}

static void int_ocp_mdio_write(struct rtl8169_private *tp, int page, int reg, int value)
{
	rtl_ocp_write(tp, 0xDE0C, page);
	rtl_ocp_write(tp, 0xDE08, value);
	rtl_ocp_write(tp, 0xDE0A, BIT(15) | (reg & 0x1f));

	rtl_udelay_loop_wait_low(tp, &rtl_int_phyar_cond, 25, 20);
}

DECLARE_RTL_COND(rtl_ext_phyar_cond)
{
	return rtl_ocp_read(tp, 0xDE2A) & BIT(14);
}

DECLARE_RTL_COND(rtl_ephyar_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(EPHYAR) & EPHYAR_FLAG;
}

static void __ext_set_phy_addr(struct rtl8169_private *tp, int phy_addr)
{
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				GENMASK(25, 21), (phy_addr << 21),
				NULL, false, true);
}

static inline int __ext_mdio_read(struct rtl8169_private *tp, int reg)
{
	int value;
	void __iomem *ioaddr = tp->mmio_addr;

	/* read reg */
	RTL_W32(EPHYAR, (reg & EPHYAR_REG_MASK) << EPHYAR_REG_SHIFT);

	value = rtl_udelay_loop_wait_high(tp, &rtl_ephyar_cond, 10, 100) ?
		RTL_R32(EPHYAR) & EPHYAR_DATA_MASK : ~0;

	return value;
}

static inline void __ext_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* write reg */
	RTL_W32(EPHYAR, EPHYAR_WRITE_CMD | (value & EPHYAR_DATA_MASK) |
		(reg & EPHYAR_REG_MASK) << EPHYAR_REG_SHIFT);

	rtl_udelay_loop_wait_low(tp, &rtl_ephyar_cond, 10, 100);

	usleep_range(10, 11);
}

static int ext_mdio_read(struct rtl8169_private *tp, int page, int reg)
{
	int value;

	tp->chip->mdio_lock(tp);

	/* write page */
	if (page != CURRENT_MDIO_PAGE)
		__ext_mdio_write(tp, 0x1f, page);

	/* read reg */
	value = __ext_mdio_read(tp, reg);

	tp->chip->mdio_unlock(tp);
	return value;
}

static void ext_mdio_write(struct rtl8169_private *tp, int page, int reg, int value)
{
	tp->chip->mdio_lock(tp);

	/* write page */
	if (page != CURRENT_MDIO_PAGE)
		__ext_mdio_write(tp, 0x1f, page);

	/* write reg */
	__ext_mdio_write(tp, reg, value);

	tp->chip->mdio_unlock(tp);
}

static int ext_ocp_mdio_read(struct rtl8169_private *tp, int page, int reg)
{
	int value;

	rtl_ocp_write(tp, 0xDE2C, page);
	rtl_ocp_write(tp, 0xDE2A, reg & 0x1f);

	value = rtl_udelay_loop_wait_low(tp, &rtl_ext_phyar_cond, 25, 20) ?
		rtl_ocp_read(tp, 0xDE28) & 0xffff : ~0;
	return value;
}

static void ext_ocp_mdio_write(struct rtl8169_private *tp, int page, int reg, int value)
{
	rtl_ocp_write(tp, 0xDE2C, page);
	rtl_ocp_write(tp, 0xDE28, value);
	rtl_ocp_write(tp, 0xDE2A, BIT(15) | (reg & 0x1f));

	rtl_udelay_loop_wait_low(tp, &rtl_ext_phyar_cond, 25, 20);
}

static void rtl_init_mdio_ops(struct rtl8169_private *tp)
{
	if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
		if (tp->chip->features & RTL_FEATURE_OCP_MDIO) {
			tp->chip->mdio_write = int_ocp_mdio_write;
			tp->chip->mdio_read = int_ocp_mdio_read;
		} else {
			tp->chip->mdio_write = int_mdio_write;
			tp->chip->mdio_read = int_mdio_read;
		}
	} else {
		if (tp->chip->features & RTL_FEATURE_OCP_MDIO) {
			tp->chip->mdio_write = ext_ocp_mdio_write;
			tp->chip->mdio_read = ext_ocp_mdio_read;
		} else {
			tp->chip->mdio_write = ext_mdio_write;
			tp->chip->mdio_read = ext_mdio_read;
		}
	}
}

static inline void rtl_phy_write(struct rtl8169_private *tp, int page, int reg, u32 val)
{
	tp->chip->mdio_write(tp, page, reg, val);
}

static inline int rtl_phy_read(struct rtl8169_private *tp, int page, int reg)
{
	return tp->chip->mdio_read(tp, page, reg);
}

static void rtl_mdio_write(struct net_device *dev, int phy_id, int location, int val)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_phy_write(tp, 0, location, val);
}

static int rtl_mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	return rtl_phy_read(tp, 0, location);
}

static inline void rtl_patchphy(struct rtl8169_private *tp, int page,
				int reg_addr, int value)
{
	rtl_phy_write(tp, page, reg_addr,
		      rtl_phy_read(tp, page, reg_addr) | value);
}

static inline void rtl_w1w0_phy(struct rtl8169_private *tp, int page,
				int reg_addr, int p, int m)
{
	rtl_phy_write(tp, page, reg_addr,
		      (rtl_phy_read(tp, page, reg_addr) | p) & ~m);
}

void int_mmd_write(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr, u32 value)
{
	tp->chip->mdio_lock(tp);

	/* set page 0 */
	__int_mdio_write(tp, 0x1f, 0);
	/* address mode */
	__int_mdio_write(tp, 13, (0x1f & dev_addr));
	/* write the desired address */
	__int_mdio_write(tp, 14, reg_addr);
	/* data mode, no post increment */
	__int_mdio_write(tp, 13, ((0x1f & dev_addr) | (0x1 << 14)));
	/* write the content of the selected register */
	__int_mdio_write(tp, 14, value);

	tp->chip->mdio_unlock(tp);
}

u32 int_mmd_read(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr)
{
	u32 value;

	tp->chip->mdio_lock(tp);

	/* set page 0 */
	__int_mdio_write(tp, 0x1f, 0);
	/* address mode */
	__int_mdio_write(tp, 13, (0x1f & dev_addr));
	/* write the desired address */
	__int_mdio_write(tp, 14, reg_addr);
	/* data mode, no post increment */
	__int_mdio_write(tp, 13, ((0x1f & dev_addr) | (0x1 << 14)));
	/* read the content of the selected register */
	value = __int_mdio_read(tp, 14);

	tp->chip->mdio_unlock(tp);
	return value;
}

void ext_mmd_write(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr, u32 value)
{
	tp->chip->mdio_lock(tp);

	/* set page 0 */
	__ext_mdio_write(tp, 0x1f, 0);
	/* address mode */
	__ext_mdio_write(tp, 13, (0x1f & dev_addr));
	/* write the desired address */
	__ext_mdio_write(tp, 14, reg_addr);
	/* data mode, no post increment */
	__ext_mdio_write(tp, 13, ((0x1f & dev_addr) | (0x1 << 14)));
	/* write the content of the selected register */
	__ext_mdio_write(tp, 14, value);

	tp->chip->mdio_unlock(tp);
}

u32 ext_mmd_read(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr)
{
	u32 value;

	tp->chip->mdio_lock(tp);

	/* set page 0 */
	__ext_mdio_write(tp, 0x1f, 0);
	/* address mode */
	__ext_mdio_write(tp, 13, (0x1f & dev_addr));
	/* write the desired address */
	__ext_mdio_write(tp, 14, reg_addr);
	/* data mode, no post increment */
	__ext_mdio_write(tp, 13, ((0x1f & dev_addr) | (0x1 << 14)));
	/* read the content of the selected register */
	value = __ext_mdio_read(tp, 14);

	tp->chip->mdio_unlock(tp);
	return value;
}

void int_ocp_mmd_write(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr, u32 value)
{
	/* address mode */
	int_ocp_mdio_write(tp, 0, 13, (0x1f & dev_addr));
	/* write the desired address */
	int_ocp_mdio_write(tp, 0, 14, reg_addr);
	/* data mode, no post increment */
	int_ocp_mdio_write(tp, 0, 13, ((0x1f & dev_addr) | (0x1 << 14)));
	/* write the content of the selected register */
	int_ocp_mdio_write(tp, 0, 14, value);
}

u32 int_ocp_mmd_read(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr)
{
	/* address mode */
	int_ocp_mdio_write(tp, 0, 13, (0x1f & dev_addr));
	/* write the desired address */
	int_ocp_mdio_write(tp, 0, 14, reg_addr);
	/* data mode, no post increment */
	int_ocp_mdio_write(tp, 0, 13, ((0x1f & dev_addr) | (0x1 << 14)));
	/* read the content of the selected register */
	return int_ocp_mdio_read(tp, 0, 14);
}

void ext_ocp_mmd_write(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr, u32 value)
{
	/* address mode */
	ext_ocp_mdio_write(tp, 0, 13, (0x1f & dev_addr));
	/* write the desired address */
	ext_ocp_mdio_write(tp, 0, 14, reg_addr);
	/* data mode, no post increment */
	ext_ocp_mdio_write(tp, 0, 13, ((0x1f & dev_addr) | (0x1 << 14)));
	/* write the content of the selected register */
	ext_ocp_mdio_write(tp, 0, 14, value);
}

u32 ext_ocp_mmd_read(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr)
{
	/* address mode */
	ext_ocp_mdio_write(tp, 0, 13, (0x1f & dev_addr));
	/* write the desired address */
	ext_ocp_mdio_write(tp, 0, 14, reg_addr);
	/* data mode, no post increment */
	ext_ocp_mdio_write(tp, 0, 13, ((0x1f & dev_addr) | (0x1 << 14)));
	/* read the content of the selected register */
	return ext_ocp_mdio_read(tp, 0, 14);
}

static void rtl_init_mmd_ops(struct rtl8169_private *tp)
{
	if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
		if (tp->chip->features & RTL_FEATURE_OCP_MDIO) {
			tp->chip->mmd_write = int_ocp_mmd_write;
			tp->chip->mmd_read = int_ocp_mmd_read;
		} else {
			tp->chip->mmd_write = int_mmd_write;
			tp->chip->mmd_read = int_mmd_read;
		}
	} else {
		if (tp->chip->features & RTL_FEATURE_OCP_MDIO) {
			tp->chip->mmd_write = ext_ocp_mmd_write;
			tp->chip->mmd_read = ext_ocp_mmd_read;
		} else {
			tp->chip->mmd_write = ext_mmd_write;
			tp->chip->mmd_read = ext_mmd_read;
		}
	}
}

void rtl_mmd_write(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr, u32 value)
{
	tp->chip->mmd_write(tp, dev_addr, reg_addr, value);
}

u32 rtl_mmd_read(struct rtl8169_private *tp, u32 dev_addr, u32 reg_addr)
{
	return tp->chip->mmd_read(tp, dev_addr, reg_addr);
}

static void rtl_eri_write(struct rtl8169_private *tp, int addr, u32 mask,
			  u32 val, int type)
{
	void __iomem *ioaddr = tp->mmio_addr;

	WARN_ON((addr & 3) || (mask == 0));
	RTL_W32(ERIDR, val);
	RTL_W32(ERIAR, ERIAR_WRITE_CMD | type | mask | addr);

	rtl_udelay_loop_wait_low(tp, &rtl_eriar_cond, 100, 100);
}

static u32 rtl_eri_read(struct rtl8169_private *tp, int addr, int type)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(ERIAR, ERIAR_READ_CMD | type | ERIAR_MASK_1111 | addr);

	return rtl_udelay_loop_wait_high(tp, &rtl_eriar_cond, 100, 100) ?
		RTL_R32(ERIDR) : ~0;
}

static void rtl_w1w0_eri(struct rtl8169_private *tp, int addr, u32 mask, u32 p,
			 u32 m, int type)
{
	u32 val;

	val = rtl_eri_read(tp, addr, type);
	rtl_eri_write(tp, addr, mask, (val & ~m) | p, type);
}

struct exgmac_reg {
	u16 addr;
	u16 mask;
	u32 val;
};

static void rtl_write_exgmac_batch(struct rtl8169_private *tp,
				   const struct exgmac_reg *r, int len)
{
	while (len-- > 0) {
		rtl_eri_write(tp, r->addr, r->mask, r->val, ERIAR_EXGMAC);
		r++;
	}
}

static u16 rtl_get_events(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R16(INTR_STATUS);
}

static void rtl_ack_events(struct rtl8169_private *tp, u16 bits)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W16(INTR_STATUS, bits);
}

static void rtl_irq_disable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W16(INTR_MASK, 0);
}

static void rtl_irq_enable(struct rtl8169_private *tp, u16 bits)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W16(INTR_MASK, bits);
}

#define RTL_EVENT_NAPI_RX	(RX_OK | RX_ERR | RX_OVERFLOW)
#define RTL_EVENT_NAPI_TX	(TX_OK | TX_ERR)
#define RTL_EVENT_NAPI		(RTL_EVENT_NAPI_RX | RTL_EVENT_NAPI_TX)

static void rtl_irq_enable_all(struct rtl8169_private *tp)
{
	rtl_irq_enable(tp, RTL_EVENT_NAPI | tp->event_slow);
}

static void rtl8169_irq_mask_and_ack(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	rtl_irq_disable(tp);
	rtl_ack_events(tp, RTL_EVENT_NAPI | tp->event_slow);
	RTL_R8(CHIP_CMD);
}

static unsigned int rtl8169_xmii_reset_pending(struct rtl8169_private *tp)
{
	int ret;

	ret = rtl_phy_read(tp, 0, MII_BMCR);
	return ret & BMCR_RESET;
}

static unsigned int rtl8169_xmii_link_ok(void __iomem *ioaddr)
{
	return RTL_R8(PHY_STATUS) & LINK_STATUS;
}

static unsigned int rtl8169_xmii_always_link_ok(void __iomem *ioaddr)
{
	return true;
}

static void rtl8169_xmii_reset_enable(struct rtl8169_private *tp)
{
	unsigned int val;

	val = rtl_phy_read(tp, 0, MII_BMCR) | BMCR_RESET;
	rtl_phy_write(tp, 0, MII_BMCR, val & 0xffff);
}

void r8169_display_eee_info(struct net_device *dev, struct seq_file *m,
			    struct rtl8169_private *tp)
{
	/* display DUT and link partner EEE capability */
	unsigned int temp, tmp1, tmp2;
	int speed = 0;
	bool eee1000, eee100, eee10;
	int duplex;

	temp = rtl_phy_read(tp, 0x0a43, 26);
	if (0 == ((0x1 << 2) & temp)) {
		seq_printf(m, "%s: link is down\n", dev->name);
		return;
	}

	if ((0x1 << 3) & temp)
		duplex = 1;
	else
		duplex = 0;

	if ((0x0 << 4) == ((0x3 << 4) & temp)) {
		speed = 10;

		tmp1 = rtl_phy_read(tp, 0x0bcd, 19);

		tmp2 = rtl_phy_read(tp, 0xa60, 16);

		if (0 == ((0x1 << 4) & tmp1))
			eee10 = false;
		else
			eee10 = true;

		seq_printf(m, "%s: link speed = %dM %s, EEE = %s, PCS_Status = 0x%02x\n",
			   dev->name, speed,
			   duplex ? "full" : "half",
			   eee10 ? "Y" : "N",
			   tmp2 & 0xff);
		return;
	}
	if ((0x1 << 4) == ((0x3 << 4) & temp))
		speed = 100;
	if ((0x2 << 4) == ((0x3 << 4) & temp))
		speed = 1000;
	if ((0x1 << 8) == ((0x1 << 8) & temp)) {
		seq_printf(m, "%s: link speed = %dM %s, EEE = Y\n", dev->name,
			   speed, duplex ? "full" : "half");

		tmp1 = rtl_phy_read(tp, 0xa60, 16);

		tmp2 = rtl_phy_read(tp, 0xa5c, 19);
		seq_printf(m, "PCS_Status = 0x%02x, EEE_wake_error = 0x%04x\n",
			   tmp1 & 0xff, tmp2);
	} else {
		seq_printf(m, "%s: link speed = %dM %s, EEE = N\n", dev->name,
			   speed, duplex ? "full" : "half");

		temp = rtl_mmd_read(tp, 0x7, 0x3d);
		if (0 == (temp & (0x1 << 2)))
			eee1000 = false;
		else
			eee1000 = true;

		if (0 == (temp & (0x1 << 1)))
			eee100 = false;
		else
			eee100 = true;

		seq_printf(m, "%s: Link Partner EEE1000=%s, EEE100=%s\n",
			   dev->name, eee1000 ? "Y" : "N", eee100 ? "Y" : "N");
	}
}

static void __rtl8169_check_link_status(struct net_device *dev,
					struct rtl8169_private *tp,
					void __iomem *ioaddr, bool pm)
{
	if (tp->chip->link_ok(ioaddr)) {
		/* This is to cancel a scheduled suspend if there's one. */
		if (pm)
			pm_request_resume(&tp->pdev->dev);
		netif_carrier_on(dev);
		if (net_ratelimit())
			netif_info(tp, ifup, dev, "link up\n");
	} else {
		netif_carrier_off(dev);
		netif_info(tp, ifdown, dev, "link down\n");
		if (pm)
			pm_schedule_suspend(&tp->pdev->dev, 5000);
	}
}

static void rtl8169_check_link_status(struct net_device *dev,
				      struct rtl8169_private *tp,
				      void __iomem *ioaddr)
{
	__rtl8169_check_link_status(dev, tp, ioaddr, false);
}

#define WAKE_ANY (WAKE_PHY | WAKE_MAGIC | WAKE_UCAST | WAKE_BCAST | WAKE_MCAST)

static u32 __rtl8169_get_wol(struct rtl8169_private *tp)
{
	u32 wolopts = 0;

	if (tp->wol_enable & WOL_MAGIC)
		wolopts |= WAKE_MAGIC;

	return wolopts;
}

static void rtl8169_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_lock_work(tp);

	wol->supported = WAKE_ANY;
	wol->wolopts = __rtl8169_get_wol(tp);

	rtl_unlock_work(tp);
}

static void __rtl8169_set_wol(struct rtl8169_private *tp, u32 wolopts)
{
	if (wolopts & WAKE_MAGIC)
		tp->wol_enable |= WOL_MAGIC;
	else
		tp->wol_enable &= ~WOL_MAGIC;
}

static int rtl8169_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_lock_work(tp);

	if (wol->wolopts)
		tp->features |= RTL_FEATURE_WOL;
	else
		tp->features &= ~RTL_FEATURE_WOL;
	__rtl8169_set_wol(tp, wol->wolopts);

	rtl_unlock_work(tp);

	device_set_wakeup_enable(&tp->pdev->dev, wol->wolopts);

	return 0;
}

static void rtl8169_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	strscpy(info->driver, MODULENAME, sizeof(info->driver));
	strscpy(info->version, RTL8169_VERSION, sizeof(info->version));
	strscpy(info->bus_info, "RTK-ETN", sizeof(info->bus_info));
}

static int rtl8169_get_regs_len(struct net_device *dev)
{
	return R8169_REGS_SIZE;
}

static int rtl8169_set_speed_xmii(struct net_device *dev,
				  u8 autoneg, u16 speed, u8 duplex, u32 adv)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int giga_ctrl, bmcr;
	int rc = -EINVAL;

	if (autoneg == AUTONEG_ENABLE) {
		int auto_nego;

		auto_nego = rtl_phy_read(tp, 0, MII_ADVERTISE);
		auto_nego &= ~(ADVERTISE_10HALF | ADVERTISE_10FULL |
			ADVERTISE_100HALF | ADVERTISE_100FULL);

		if (adv & ADVERTISED_10baseT_Half)
			auto_nego |= ADVERTISE_10HALF;
		if (adv & ADVERTISED_10baseT_Full)
			auto_nego |= ADVERTISE_10FULL;
		if (adv & ADVERTISED_100baseT_Half)
			auto_nego |= ADVERTISE_100HALF;
		if (adv & ADVERTISED_100baseT_Full)
			auto_nego |= ADVERTISE_100FULL;

		auto_nego |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;

		giga_ctrl = rtl_phy_read(tp, 0, MII_CTRL1000);
		giga_ctrl &= ~(ADVERTISE_1000FULL | ADVERTISE_1000HALF);

		/* The 8100e/8101e/8102e do Fast Ethernet only. */
		if (tp->mii.supports_gmii) {
			if (adv & ADVERTISED_1000baseT_Half)
				giga_ctrl |= ADVERTISE_1000HALF;
			if (adv & ADVERTISED_1000baseT_Full)
				giga_ctrl |= ADVERTISE_1000FULL;
		} else if (adv & (ADVERTISED_1000baseT_Half |
				ADVERTISED_1000baseT_Full)) {
			netif_info(tp, link, dev,
				   "PHY does not support 1000Mbps\n");
			goto out;
		}

		bmcr = BMCR_ANENABLE | BMCR_ANRESTART;

		rtl_phy_write(tp, 0, MII_ADVERTISE, auto_nego);
		rtl_phy_write(tp, 0, MII_CTRL1000, giga_ctrl);
	} else {
		giga_ctrl = 0;

		if (speed == SPEED_10)
			bmcr = 0;
		else if (speed == SPEED_100)
			bmcr = BMCR_SPEED100;
		else
			goto out;

		if (duplex == DUPLEX_FULL)
			bmcr |= BMCR_FULLDPLX;
	}

	rtl_phy_write(tp, 0, MII_BMCR, bmcr);

	rc = 0;
out:
	return rc;
}

static int rtl8169_set_speed(struct net_device *dev,
			     u8 autoneg, u16 speed, u8 duplex, u32 advertising)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret;

	ret = tp->chip->set_speed(dev, autoneg, speed, duplex, advertising);
	return ret;
}

static netdev_features_t rtl8169_fix_features(struct net_device *dev,
					      netdev_features_t features)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (dev->mtu > TD_MSS_MAX)
		features &= ~NETIF_F_ALL_TSO;

	if (dev->mtu > JUMBO_1K && !tp->chip->jumbo_tx_csum)
		features &= ~NETIF_F_IP_CSUM;

	return features;
}

static void __rtl8169_set_features(struct net_device *dev,
				   netdev_features_t features)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	netdev_features_t changed = features ^ dev->features;
	void __iomem *ioaddr = tp->mmio_addr;

	if (!(changed & (NETIF_F_RXALL | NETIF_F_RXCSUM |
			 NETIF_F_HW_VLAN_CTAG_RX)))
		return;

	if (changed & (NETIF_F_RXCSUM | NETIF_F_HW_VLAN_CTAG_RX)) {
		if (features & NETIF_F_RXCSUM)
			tp->cp_cmd |= RX_CHK_SUM;
		else
			tp->cp_cmd &= ~RX_CHK_SUM;

		if (dev->features & NETIF_F_HW_VLAN_CTAG_RX)
			tp->cp_cmd |= RX_VLAN;
		else
			tp->cp_cmd &= ~RX_VLAN;

		RTL_W16(C_PLUS_CMD, tp->cp_cmd);
		RTL_R16(C_PLUS_CMD);
	}
	if (changed & NETIF_F_RXALL) {
		int tmp = (RTL_R32(RX_CONFIG) & ~(ACCEPT_ERR | ACCEPT_RUNT));

		if (features & NETIF_F_RXALL)
			tmp |= (ACCEPT_ERR | ACCEPT_RUNT);
		RTL_W32(RX_CONFIG, tmp);
	}
}

static int rtl8169_set_features(struct net_device *dev,
				netdev_features_t features)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_lock_work(tp);
	__rtl8169_set_features(dev, features);
	rtl_unlock_work(tp);

	return 0;
}

static inline u32 rtl8169_tx_vlan_tag(struct sk_buff *skb)
{
	return (skb_vlan_tag_present(skb)) ?
		TX_VLAN_TAG | swab16(skb_vlan_tag_get(skb)) : 0x00;
}

static void rtl8169_rx_vlan_tag(struct rx_desc *desc, struct sk_buff *skb)
{
	u32 opts2 = le32_to_cpu(desc->opts2);

	if (opts2 & RX_VLAN_TAG)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       swab16(opts2 & 0xffff));
}

static int rtl8169_get_link_ksettings(struct net_device *dev,
				      struct ethtool_link_ksettings *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_lock_work(tp);
	mii_ethtool_get_link_ksettings(&tp->mii, cmd);
	rtl_unlock_work(tp);

	return 0;
}

static int rtl8169_set_link_ksettings(struct net_device *dev,
				      const struct ethtool_link_ksettings *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret;
	u32 advertising;

	ethtool_convert_link_mode_to_legacy_u32(&advertising, cmd->link_modes.advertising);

	rtl_lock_work(tp);
	ret = rtl8169_set_speed(dev, cmd->base.autoneg, cmd->base.speed,
				cmd->base.duplex, advertising);
	rtl_unlock_work(tp);

	return ret;
}

static void rtl8169_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			     void *p)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	u32 __iomem *data = tp->mmio_addr;
	u32 *dw = p;
	int i;

	rtl_lock_work(tp);
	for (i = 0; i < R8169_REGS_SIZE; i += 4)
		memcpy_fromio(dw++, data++, 4);
	rtl_unlock_work(tp);
}

static u32 rtl8169_get_msglevel(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	return tp->msg_enable;
}

static void rtl8169_set_msglevel(struct net_device *dev, u32 value)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	tp->msg_enable = value;
}

static const char rtl8169_gstrings[][ETH_GSTRING_LEN] = {
	"tx_packets",
	"rx_packets",
	"tx_errors",
	"rx_errors",
	"rx_missed",
	"align_errors",
	"tx_single_collisions",
	"tx_multi_collisions",
	"unicast",
	"broadcast",
	"multicast",
	"tx_aborted",
	"tx_underrun",
};

static int rtl8169_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(rtl8169_gstrings);
	default:
		return -EOPNOTSUPP;
	}
}

DECLARE_RTL_COND(rtl_counters_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(COUNTER_ADDR_LOW) & COUNTER_DUMP;
}

static void rtl8169_update_counters(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct device *d = &tp->pdev->dev;
	struct rtl8169_counters *counters;
	dma_addr_t paddr;
	u32 cmd;
	int node = dev->dev.parent ? dev_to_node(dev->dev.parent) : -1;

	/* Some chips are unable to dump tally counters when the receiver
	 * is disabled.
	 */
	if ((RTL_R8(CHIP_CMD) & CMD_RX_ENB) == 0)
		return;

	if (tp->acp_enable) {
		counters = kzalloc_node(sizeof(*counters), GFP_KERNEL, node);
		paddr = virt_to_phys(counters);
	} else {
		counters = dma_alloc_coherent(d, sizeof(*counters), &paddr,
					      GFP_KERNEL);
	}
	if (!counters)
		return;

	RTL_W32(COUNTER_ADDR_HIGH, (u64)paddr >> 32);
	cmd = (u64)paddr & DMA_BIT_MASK(32);
	RTL_W32(COUNTER_ADDR_LOW, cmd);
	RTL_W32(COUNTER_ADDR_LOW, cmd | COUNTER_DUMP);

	if (rtl_udelay_loop_wait_low(tp, &rtl_counters_cond, 10, 1000))
		memcpy(&tp->counters, counters, sizeof(*counters));

	RTL_W32(COUNTER_ADDR_LOW, 0);
	RTL_W32(COUNTER_ADDR_HIGH, 0);

	if (tp->acp_enable)
		kfree(counters);
	else
		dma_free_coherent(d, sizeof(*counters), counters, paddr);
}

static void rtl8169_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	ASSERT_RTNL();

	rtl8169_update_counters(dev);

	data[0] = le64_to_cpu(tp->counters.tx_packets);
	data[1] = le64_to_cpu(tp->counters.rx_packets);
	data[2] = le64_to_cpu(tp->counters.tx_errors);
	data[3] = le32_to_cpu(tp->counters.rx_errors);
	data[4] = le16_to_cpu(tp->counters.rx_missed);
	data[5] = le16_to_cpu(tp->counters.align_errors);
	data[6] = le32_to_cpu(tp->counters.tx_one_collision);
	data[7] = le32_to_cpu(tp->counters.tx_multi_collision);
	data[8] = le64_to_cpu(tp->counters.rx_unicast);
	data[9] = le64_to_cpu(tp->counters.rx_broadcast);
	data[10] = le32_to_cpu(tp->counters.rx_multicast);
	data[11] = le16_to_cpu(tp->counters.tx_aborted);
	data[12] = le16_to_cpu(tp->counters.tx_underrun);
}

static void rtl8169_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, rtl8169_gstrings, sizeof(rtl8169_gstrings));
		break;
	}
}

static const struct ethtool_ops rtl8169_ethtool_ops = {
	.get_link_ksettings	= rtl8169_get_link_ksettings,
	.set_link_ksettings	= rtl8169_set_link_ksettings,
	.get_drvinfo		= rtl8169_get_drvinfo,
	.get_regs_len		= rtl8169_get_regs_len,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= rtl8169_get_msglevel,
	.set_msglevel		= rtl8169_set_msglevel,
	.get_regs		= rtl8169_get_regs,
	.get_wol		= rtl8169_get_wol,
	.set_wol		= rtl8169_set_wol,
	.get_strings		= rtl8169_get_strings,
	.get_sset_count		= rtl8169_get_sset_count,
	.get_ethtool_stats	= rtl8169_get_ethtool_stats,
	.get_ts_info		= ethtool_op_get_ts_info,
};

struct phy_reg {
	u16 reg;
	u16 val;
};

static __maybe_unused void rtl_rar_exgmac_set(struct rtl8169_private *tp,
					      u8 *addr)
{
	const u16 w[] = {
		addr[0] | (addr[1] << 8),
		addr[2] | (addr[3] << 8),
		addr[4] | (addr[5] << 8)
	};
	const struct exgmac_reg e[] = {
		{.addr = 0xe0, ERIAR_MASK_1111, .val = w[0] | (w[1] << 16)},
		{.addr = 0xe4, ERIAR_MASK_1111, .val = w[2]},
		{.addr = 0xf0, ERIAR_MASK_1111, .val = w[0] << 16},
		{.addr = 0xf4, ERIAR_MASK_1111, .val = w[1] | (w[2] << 16)}
	};

	rtl_write_exgmac_batch(tp, e, ARRAY_SIZE(e));
}

static void rtl8168g_2_hw_mac_mcu_patch(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* power down PHY */
	rtl_phy_write(tp, 0, MII_BMCR,
		      rtl_phy_read(tp, 0, MII_BMCR) | BMCR_PDOWN);

	tp->chip->mac_mcu_patch(tp);

	/* set dis_mcu_clroob to avoid WOL fail when ALDPS mode is enabled */
	RTL_W8(MCU, RTL_R8(MCU) | DIS_MCU_CLROOB);
}

static void rtl8168g_2_hw_phy_config(struct rtl8169_private *tp)
{
	tp->chip->hw_phy_config(tp);
}

static void rtl_hw_phy_config(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8168g_2_hw_mac_mcu_patch(tp);
	rtl8168g_2_hw_phy_config(tp);
}

static void rtl_schedule_task(struct rtl8169_private *tp, enum rtl_flag flag)
{
	if (!test_and_set_bit(flag, tp->wk.flags))
		schedule_work(&tp->wk.work);
}

static void rtl_rar_set(struct rtl8169_private *tp, u8 *addr)
{
	void __iomem *ioaddr = tp->mmio_addr;

	rtl_lock_work(tp);

	RTL_W8(CFG9346, CFG9346_UNLOCK);

	RTL_W32(MAC4, addr[4] | addr[5] << 8);
	RTL_R32(MAC4);

	RTL_W32(MAC0, addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	RTL_R32(MAC0);

	RTL_W8(CFG9346, CFG9346_LOCK);

	rtl_unlock_work(tp);
}

static void rtl_phy_reinit(struct rtl8169_private *tp)
{
	u32 tmp;

	/* fill fuse_rdy & rg_ext_ini_done */
	rtl_phy_write(tp, 0x0a46, 20,
		      rtl_phy_read(tp, 0x0a46, 20) | (BIT(1) | BIT(0)));

	/* init_autoload_done = 1 */
	tmp = rtl_ocp_read(tp, 0xe004);
	tmp |= BIT(7);
	rtl_ocp_write(tp, 0xe004, tmp);

	/* wait LAN-ON */
	tmp = 0;
	do {
		tmp += 1;
		mdelay(1);
		if (tmp >= 2000) {
			pr_err("PHY status is not 0x3, current = 0x%02x\n",
			       (rtl_phy_read(tp, 0x0a42, 16) & 0x07));
			break;
		}
	} while (0x3 != (rtl_phy_read(tp, 0x0a42, 16) & 0x07));
	pr_info("wait %d ms for PHY ready, current = 0x%x\n",
		tmp, rtl_phy_read(tp, 0x0a42, 16));

	tp->chip->phy_reset_enable(tp);
}

static void rtl_phy_work(struct rtl8169_private *tp)
{
	rtl_phy_reinit(tp);
	atomic_dec(&tp->phy_reinit_flag);
}

static void rtl8169_release_board(struct platform_device *pdev,
				  struct net_device *dev, void __iomem *ioaddr)
{
	iounmap(ioaddr);
	free_netdev(dev);
}

DECLARE_RTL_COND(rtl_phy_reset_cond)
{
	return tp->chip->phy_reset_pending(tp);
}

static void rtl8169_phy_reset(struct net_device *dev,
			      struct rtl8169_private *tp)
{
	tp->chip->phy_reset_enable(tp);
	rtl_msleep_loop_wait_low(tp, &rtl_phy_reset_cond, 1, 100);
}

static void rtl8169_init_phy(struct net_device *dev, struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	rtl_hw_phy_config(dev);

	/* disable now_is_oob */
	RTL_W8(MCU, RTL_R8(MCU) & ~NOW_IS_OOB);

	rtl8169_phy_reset(dev, tp);

	rtl8169_set_speed(dev, AUTONEG_ENABLE, SPEED_1000, DUPLEX_FULL,
			  ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
			  ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full |
			  (tp->mii.supports_gmii ?
			   ADVERTISED_1000baseT_Half |
			   ADVERTISED_1000baseT_Full : 0));
}

static int rtl_set_mac_address(struct net_device *dev, void *p)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	rtl_rar_set(tp, dev->dev_addr);

	return 0;
}

static int rtl8169_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(ifr);

	return netif_running(dev) ? tp->chip->do_ioctl(tp, data, cmd) : -ENODEV;
}

static int rtl_xmii_ioctl(struct rtl8169_private *tp,
			  struct mii_ioctl_data *data, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = 32;	/* Internal PHY */
		return 0;

	case SIOCGMIIREG:
		data->val_out = rtl_phy_read(tp, 0, data->reg_num & 0x1f);
		return 0;

	case SIOCSMIIREG:
		rtl_phy_write(tp, 0, data->reg_num & 0x1f, data->val_in);
		return 0;
	}
	return -EOPNOTSUPP;
}

static void rtl_wpd_set(struct rtl8169_private *tp)
{
	u32 tmp;
	u32 i;

	/* clear WPD registers */
	rtl_ocp_write(tp, 0xD23A, 0);
	rtl_ocp_write(tp, 0xD23C, 0);
	rtl_ocp_write(tp, 0xD23E, 0);
	for (i = 0; i < 128; i += 2)
		rtl_ocp_write(tp, 0xD240 + i, 0);

	tmp = rtl_ocp_read(tp, 0xC0C2);
	tmp &= ~BIT(4);
	rtl_ocp_write(tp, 0xC0C2, tmp);

	/* enable WPD */
	tmp = rtl_ocp_read(tp, 0xC0C2);
	tmp |= BIT(0);
	rtl_ocp_write(tp, 0xC0C2, tmp);
}

/* EXACTLY PATTERN WAKE UP */
static int rtl_cp_reduced_pattern(struct rtl8169_private *tp, u32 idx,
				  u8 *src, u32 len)
{
	u32 i;
	u32 j;
	u32 src_offset = 0;
	u32 dst_offset = 0;

	memset(&tp->wol_rule[idx].pattern[0], 0, RTL_WAKE_PATTERN_SIZE);

	if (tp->wol_rule[idx].mask_size >= RTL_WAKE_MASK_SIZE) {
		pr_err("WOL rule[%d]: incorrect mask size %d\n", idx,
		       tp->wol_rule[idx].mask_size);
		return 0;
	}

	for (i = 0; i < tp->wol_rule[idx].mask_size; i++) {
		for (j = 0; j < 8; j++) {
			if (tp->wol_rule[idx].mask[i] & (1 << j)) {
				dst_offset = (i * 8) + j;
				tp->wol_rule[idx].pattern[dst_offset] =
					src[src_offset++];
			}
		}
	}

	if (src_offset != len)
		pr_err("WOL rule[%d]: expected pattern len %d, actual len %d\n",
		       idx, len, src_offset);

	return (dst_offset + 1);
}

static void rtl_write_pat_wakeup_pattern(struct rtl8169_private *tp, u32 idx)
{
	u8 i;
	u32 reg_shift;
	u32 reg_offset;
	u16 mask_sel;
	u16 cross_flag;
	u8 zero_prefix;
	u16 tmp;
	u16 reg;
	u8 temp_mask[RTL_WAKE_MASK_REG_SIZE];
	u8 zero_mask[] = {0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F};
	u16 data_byte;
	u16 half_byte;
	u8 temp_pattern[RTL_WAKE_PATTERN_SIZE + 2];
	u16 *ptr;

	/* check size of wake-mask */
	if (tp->wol_rule[idx].mask_size > RTL_WAKE_MASK_SIZE) {
		pr_err("[%s] size of WoL wake-mask%d is too long\n",
		       MODULENAME, idx);
		pr_err("mask size %d (max %d)\n",
		       tp->wol_rule[idx].mask_size, RTL_WAKE_MASK_SIZE);
		return;
	}

	/* check size of wake-pattern */
	if (tp->wol_rule[idx].pattern_size >= RTL_WAKE_PATTERN_SIZE) {
		pr_err("[%s] size of WoL wake-pattern%d is too long\n",
		       MODULENAME, idx);
		pr_err("pattern size %d (max %d)\n",
		       tp->wol_rule[idx].pattern_size, RTL_WAKE_PATTERN_SIZE);
		return;
	}

	/* make sure mask contains correct zero prefix */
	zero_prefix = zero_mask[tp->wol_rule[idx].offset % 8];
	if (tp->wol_rule[idx].mask[0] & zero_prefix) {
		pr_err("[%s] incorrect zero prefix of WoL wake-mask%d\n",
		       MODULENAME, idx);
		pr_err("offset %d, zero prefix 0x%x, mask[0] 0x%x\n",
		       tp->wol_rule[idx].offset, zero_prefix,
		       tp->wol_rule[idx].mask[0]);
		return;
	}

	/* make sure pattern contains correct zero prefix */
	tmp = tp->wol_rule[idx].offset % 8;
	for (i = 0; i < tmp; i++) {
		if (tp->wol_rule[idx].pattern[i] == 0x00)
			continue;
		pr_err("[%s] incorrect zero prefix of WoL wake-pattern%d\n",
		       MODULENAME, idx);
		pr_err("offset %d, zero prefix cnt %d, pattern[i] 0x%x\n",
		       tp->wol_rule[idx].offset, tmp,
		       tp->wol_rule[idx].pattern[i]);
		return;
	}

	/* fill CRC_MASK_SEL_SET reg */
	reg_offset = (idx / 4) * 2;
	reg_shift  = idx % 4;

	cross_flag = ((tp->wol_rule[idx].offset % 256) >= 128) ? 1 : 0;
	mask_sel = ((tp->wol_rule[idx].offset / 256) << 1) | cross_flag;

	reg = 0xC130 + reg_offset;
	tmp = rtl_ocp_read(tp, reg);
	tmp &= ~(0xF << reg_shift);
	tmp |= mask_sel << reg_shift;
	rtl_ocp_write(tp, reg, tmp);

	/* create 32-byte WAKE_MASK according to 17-byte tp->wol_rule[idx].mask */
	memset(temp_mask, 0, RTL_WAKE_MASK_REG_SIZE);
	tmp = (tp->wol_rule[idx].offset % 256) / 8;
	for (i = 0; i < tp->wol_rule[idx].mask_size; i++)
		temp_mask[(tmp + i) % RTL_WAKE_MASK_REG_SIZE] =
			tp->wol_rule[idx].mask[i];

	/* fill WAKE_MASK reg */
	reg_offset = (idx / 4) * 2;
	reg_shift = (idx % 4) * 4;

	for (i = 0; i < 64; i++) {
		reg = 0x7C00 + reg_offset + (i * 16);
		tmp = rtl_ocp_read(tp, reg);
		tmp &= ~(0xF << reg_shift);
		data_byte = temp_mask[(i * 4) / 8];
		half_byte = (data_byte >> ((i * 4) % 8)) & 0xF;
		tmp |= half_byte << reg_shift;
		rtl_ocp_write(tp, reg, tmp);
	}

	/* fill CRC reg */
	if (idx < 8)
		reg = 0xC020 + (idx * 2);
	else
		reg = 0xC100 + ((idx - 8) * 2);
	rtl_ocp_write(tp, reg, tp->wol_rule[idx].crc);

	/* fill start byte and exactly match pattern */
	memset(temp_pattern, 0, sizeof(temp_pattern));
	ptr = (u16 *)&temp_pattern[0];
	*ptr = tp->wol_rule[idx].offset & 0x7F8; /* 8 alignment */

	for (i = 0; i < tp->wol_rule[idx].pattern_size; i++)
		temp_pattern[i + 2] = tp->wol_rule[idx].pattern[i];

	for (i = 0; i < (RTL_WAKE_PATTERN_SIZE + 2) / 2; i++) {
		reg = 0x6A00 + (idx * 138) + (i * 2);
		ptr = (u16 *)&temp_pattern[i * 2];
		rtl_ocp_write(tp, reg, *ptr);
	}
}

static void rtl_mdns_pat_wakeup(struct rtl8169_private *tp)
{
	int i;

	for (i = 0; i < RTL_WAKE_SIZE; i++)
		if (tp->wol_rule[i].flag & WAKE_FLAG_ENABLE)
			rtl_write_pat_wakeup_pattern(tp, i);
}

static void rtl_clear_pat_wakeup_pattern(struct rtl8169_private *tp)
{
	rtl_ocp_write(tp, 0xC140, 0xFFFF);
	rtl_ocp_write(tp, 0xC142, 0xFFFF);
}

static void rtl_pat_wakeup_set(struct rtl8169_private *tp, bool enable)
{
	u16 tmp;

	rtl_clear_pat_wakeup_pattern(tp);
	if (tp->wol_crc_cnt > 0 && (tp->wol_enable & WOL_CRC_MATCH)) {
		if (enable) {
			rtl_mdns_pat_wakeup(tp);

			tmp = rtl_ocp_read(tp, 0xE8DE);
			tmp |= BIT(14); /* borrow 8K SRAM */
			rtl_ocp_write(tp, 0xE8DE, tmp);

			tmp = rtl_ocp_read(tp, 0xC0C2);
			tmp |= BIT(3); /* enable wol exactly pattern match */
			rtl_ocp_write(tp, 0xC0C2, tmp);
		} else {
			tmp = rtl_ocp_read(tp, 0xC0C2);
			tmp &= ~BIT(3); /* disable wol exactly pattern match */
			rtl_ocp_write(tp, 0xC0C2, tmp);

			tmp = rtl_ocp_read(tp, 0xE8DE);
			tmp &= ~BIT(14); /* stop borrow 8K SRAM */
			rtl_ocp_write(tp, 0xE8DE, tmp);
		}
	}
}

/* CRC WAKE UP */
static void rtl_write_crc_wakeup_pattern(struct rtl8169_private *tp, u32 idx)
{
	u8 i;
	u8 j;
	u32 reg_mask;
	u32 reg_shift;
	u32 reg_offset;

	reg_offset = idx & ~(BIT(0) | BIT(1));
	reg_shift = (idx % 4) * 8;
	switch (reg_shift) {
	case 0:
		reg_mask = ERIAR_MASK_0001;
		break;
	case 8:
		reg_mask = ERIAR_MASK_0010;
		break;
	case 16:
		reg_mask = ERIAR_MASK_0100;
		break;
	case 24:
		reg_mask = ERIAR_MASK_1000;
		break;
	default:
		pr_err(PFX "Invalid shift bit 0x%x, idx = %d\n", reg_shift, idx);
		return;
	}

	for (i = 0, j = 0; i < 0x80; i += 8, j++) {
		rtl_eri_write(tp, i + reg_offset, reg_mask,
			      tp->wol_rule[idx].mask[j] << reg_shift, ERIAR_EXGMAC);
	}

	reg_offset = idx * 2;
	if (idx % 2) {
		reg_mask = ERIAR_MASK_1100;
		reg_offset -= 2;
		reg_shift = 16;
	} else {
		reg_mask = ERIAR_MASK_0011;
		reg_shift = 0;
	}
	rtl_eri_write(tp, (int)(0x80 + reg_offset), reg_mask,
		      tp->wol_rule[idx].crc << reg_shift, ERIAR_EXGMAC);
}

static void rtl_mdns_crc_wakeup(struct rtl8169_private *tp)
{
	int i;

	for (i = 0; i < RTL_WAKE_SIZE_CRC; i++)
		if (tp->wol_rule[i].flag & WAKE_FLAG_ENABLE)
			rtl_write_crc_wakeup_pattern(tp, i);
}

static void rtl_clear_crc_wakeup_pattern(struct rtl8169_private *tp)
{
	u8 i;

	for (i = 0; i < 0x80; i += 4)
		rtl_eri_write(tp, i, ERIAR_MASK_1111, 0x0, ERIAR_EXGMAC);

	for (i = 0x80; i < 0x90; i += 4)
		rtl_eri_write(tp, i, ERIAR_MASK_1111, 0x0, ERIAR_EXGMAC);
}

static void rtl_crc_wakeup_set(struct rtl8169_private *tp, bool enable)
{
	rtl_clear_crc_wakeup_pattern(tp);
	if (tp->wol_crc_cnt > 0 && (tp->wol_enable & WOL_CRC_MATCH) && enable)
		rtl_mdns_crc_wakeup(tp);
}

static void rtl_speed_down(struct rtl8169_private *tp)
{
	u32 adv;
	int lpa;

	lpa = rtl_phy_read(tp, 0, MII_LPA);

	if (lpa & (LPA_10HALF | LPA_10FULL))
		adv = ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full;
	else if (lpa & (LPA_100HALF | LPA_100FULL))
		adv = ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
			ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full;
	else
		adv = ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
			ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full |
			(tp->mii.supports_gmii ?
			ADVERTISED_1000baseT_Half |
			ADVERTISED_1000baseT_Full : 0);

	rtl8169_set_speed(tp->dev, AUTONEG_ENABLE, SPEED_1000, DUPLEX_FULL,
			  adv);
}

static void rtl_wol_suspend_quirk(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(RX_CONFIG, RTL_R32(RX_CONFIG) |
		ACCEPT_BROADCAST | ACCEPT_MULTICAST | ACCEPT_MY_PHYS);
}

static bool rtl_wol_pll_power_down(struct rtl8169_private *tp)
{
	if (!(__rtl8169_get_wol(tp) & WAKE_ANY))
		return false;

	rtl_speed_down(tp);
	rtl_wol_suspend_quirk(tp);

	return true;
}

static void r8168_phy_power_up(struct rtl8169_private *tp)
{
	rtl_phy_write(tp, 0, MII_BMCR, BMCR_ANENABLE);
}

static void r8168_phy_power_down(struct rtl8169_private *tp)
{
	rtl_phy_write(tp, 0, MII_BMCR, BMCR_PDOWN);
}

static void r8168_pll_power_down(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(CFG9346, CFG9346_UNLOCK);
	if (tp->wol_enable & WOL_MAGIC) {
		/* enable now_is_oob */
		RTL_W8(MCU, RTL_R8(MCU) | NOW_IS_OOB);
		RTL_W8(CONFIG5, RTL_R8(CONFIG5) | LAN_WAKE);
		RTL_W8(CONFIG3, RTL_R8(CONFIG3) | MAGIC_PKT);

		tp->chip->wakeup_set(tp, true);
		if (tp->wol_enable & WOL_WPD)
			rtl_wpd_set(tp);
	} else {
		RTL_W8(CONFIG5, RTL_R8(CONFIG5) & ~LAN_WAKE);
		RTL_W8(CONFIG3, RTL_R8(CONFIG3) & ~MAGIC_PKT);
	}
	RTL_W8(CFG9346, CFG9346_LOCK);

	if (rtl_wol_pll_power_down(tp))
		return;

	r8168_phy_power_down(tp);
}

static void r8168_pll_power_up(struct rtl8169_private *tp)
{
	r8168_phy_power_up(tp);
}

static inline void rtl_generic_op(struct rtl8169_private *tp,
				  void (*op)(struct rtl8169_private *))
{
	if (op)
		op(tp);
}

static inline void rtl_pll_power_down(struct rtl8169_private *tp)
{
	rtl_generic_op(tp, tp->chip->pll_power_down);
}

static inline void rtl_pll_power_up(struct rtl8169_private *tp)
{
	rtl_generic_op(tp, tp->chip->pll_power_up);
}

static void rtl_init_rxcfg(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* disable RX128_INT_EN to reduce CPU loading */
	RTL_W32(RX_CONFIG, RX_DMA_BURST | RX_EARLY_OFF);
}

static void rtl8169_init_ring_indexes(struct rtl8169_private *tp)
{
	tp->dirty_tx = 0;
	tp->cur_tx = 0;
	tp->cur_rx = 0;

#if defined(CONFIG_RTL_RX_NO_COPY)
	tp->dirty_rx = 0;
#endif /* CONFIG_RTL_RX_NO_COPY */
}

DECLARE_RTL_COND(rtl_chipcmd_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R8(CHIP_CMD) & CMD_RESET;
}

static void rtl_hw_reset(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(CHIP_CMD, CMD_RESET);

	rtl_udelay_loop_wait_low(tp, &rtl_chipcmd_cond, 100, 100);
}

static void rtl_rx_close(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(RX_CONFIG, RTL_R32(RX_CONFIG) & ~RX_CONFIG_ACCEPT_MASK);
}

DECLARE_RTL_COND(rtl_txcfg_empty_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(TX_CONFIG) & TXCFG_EMPTY;
}

static void rtl8169_hw_reset(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* Disable interrupts */
	rtl8169_irq_mask_and_ack(tp);

	rtl_rx_close(tp);

	RTL_W8(CHIP_CMD, RTL_R8(CHIP_CMD) | STOP_REQ);
	rtl_udelay_loop_wait_high(tp, &rtl_txcfg_empty_cond, 100, 666);

	rtl_hw_reset(tp);

	if (tp->chip->features & RTL_FEATURE_TX_NO_CLOSE) {
		/* disable tx no close mode */
		rtl_ocp_write(tp, 0xE610,
			      rtl_ocp_read(tp, 0xE610) & ~(BIT(4) | BIT(6)));
	};
}

static void rtl_set_rx_tx_config_registers(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* Set DMA burst size and Interframe Gap Time */
	RTL_W32(TX_CONFIG, (TX_DMA_BURST << TX_DMA_SHIFT) |
		(INTER_FRAME_GAP << TX_INTER_FRAME_GAP_SHIFT));
}

static void rtl_hw_start(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	tp->chip->hw_start(dev);

	rtl_irq_enable_all(tp);
}

static void rtl_set_rx_tx_desc_registers(struct rtl8169_private *tp,
					 void __iomem *ioaddr)
{
	/* Magic spell: some iop3xx ARM board needs the TxDescAddrHigh
	 * register to be written before TxDescAddrLow to work.
	 * Switching from MMIO to I/O access fixes the issue as well.
	 */
	RTL_W32(TX_DESC_START_ADDR_HIGH, ((u64)tp->tx_phy_addr) >> 32);
	RTL_W32(TX_DESC_START_ADDR_LOW,
		((u64)tp->tx_phy_addr) & DMA_BIT_MASK(32));
	RTL_W32(RX_DESC_ADDR_HIGH, ((u64)tp->rx_phy_addr) >> 32);
	RTL_W32(RX_DESC_ADDR_LOW, ((u64)tp->rx_phy_addr) & DMA_BIT_MASK(32));
}

static void rtl_set_rx_max_size(void __iomem *ioaddr, unsigned int rx_buf_sz)
{
	/* Low hurts. Let's disable the filtering. */
	RTL_W16(RX_MAX_SIZE, rx_buf_sz);
}

static void rtl_set_rx_mode(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u32 mc_filter[2];	/* Multicast hash filter */
	int rx_mode;
	u32 tmp = 0;
	u32 data;

	if (dev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		netif_notice(tp, link, dev, "Promiscuous mode enabled\n");
		rx_mode =
			ACCEPT_BROADCAST | ACCEPT_MULTICAST | ACCEPT_MY_PHYS |
			ACCEPT_ALL_PHYS;
		mc_filter[1] = 0xffffffff;
		mc_filter[0] = 0xffffffff;
	} else if ((netdev_mc_count(dev) > multicast_filter_limit) ||
		(dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = ACCEPT_BROADCAST | ACCEPT_MULTICAST | ACCEPT_MY_PHYS;
		mc_filter[1] = 0xffffffff;
		mc_filter[0] = 0xffffffff;
	} else {
		struct netdev_hw_addr *ha;

		rx_mode = ACCEPT_BROADCAST | ACCEPT_MY_PHYS;
		mc_filter[1] = 0;
		mc_filter[0] = 0;
		netdev_for_each_mc_addr(ha, dev) {
			int bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;

			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			rx_mode |= ACCEPT_MULTICAST;
		}
	}

	if (dev->features & NETIF_F_RXALL)
		rx_mode |= (ACCEPT_ERR | ACCEPT_RUNT);

	tmp = (RTL_R32(RX_CONFIG) & ~RX_CONFIG_ACCEPT_MASK) | rx_mode;

	data = mc_filter[0];

	mc_filter[0] = swab32(mc_filter[1]);
	mc_filter[1] = swab32(data);

	RTL_W32(MAR0 + 4, mc_filter[1]);
	RTL_W32(MAR0 + 0, mc_filter[0]);

	RTL_W32(RX_CONFIG, tmp);
}

static inline void rtl_csi_write(struct rtl8169_private *tp, int addr,
				 int value)
{
	tp->chip->csi_write(tp, addr, value);
}

static inline u32 rtl_csi_read(struct rtl8169_private *tp, int addr)
{
	return tp->chip->csi_read(tp, addr);
}

static void rtl_csi_access_enable(struct rtl8169_private *tp, u32 bits)
{
	u32 csi;

	csi = rtl_csi_read(tp, 0x070c) & 0x00ffffff;
	rtl_csi_write(tp, 0x070c, csi | bits);
}

static void rtl_csi_access_enable_1(struct rtl8169_private *tp)
{
	rtl_csi_access_enable(tp, 0x17000000);
}

DECLARE_RTL_COND(rtl_csiar_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(CSIAR) & CSIAR_FLAG;
}

static void r8169_csi_write(struct rtl8169_private *tp, int addr, int value)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(CSIDR, value);
	RTL_W32(CSIAR, CSIAR_WRITE_CMD | (addr & CSIAR_ADDR_MASK) |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT);

	rtl_udelay_loop_wait_low(tp, &rtl_csiar_cond, 10, 100);
}

static u32 r8169_csi_read(struct rtl8169_private *tp, int addr)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(CSIAR, (addr & CSIAR_ADDR_MASK) |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT);

	return rtl_udelay_loop_wait_high(tp, &rtl_csiar_cond, 10, 100) ?
		RTL_R32(CSIDR) : ~0;
}

static void rtl_led_set(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* LED setting */
	if (tp->led_cfg)
		RTL_W32(LEDSEL, 0x00060000 | tp->led_cfg);
	else
		RTL_W32(LEDSEL, tp->chip->led_cfg);
}

static void rtl_hw_start_8168g_1(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(TX_CONFIG, RTL_R32(TX_CONFIG) | TXCFG_AUTO_FIFO);

	rtl_csi_access_enable_1(tp);

	rtl_w1w0_eri(tp, 0xdc, ERIAR_MASK_0001, 0x00, 0x01, ERIAR_EXGMAC);
	rtl_w1w0_eri(tp, 0xdc, ERIAR_MASK_0001, 0x01, 0x00, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0x2f8, ERIAR_MASK_0011, 0x1d8f, ERIAR_EXGMAC);

	RTL_W8(CHIP_CMD, CMD_TX_ENB | CMD_RX_ENB);
	RTL_W32(MISC, RTL_R32(MISC) & ~RXDV_GATED_EN);
	RTL_W8(MAX_TX_PACKET_SIZE, EARLY_SIZE);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);

	/* Adjust EEE LED frequency */
	RTL_W8(EEE_LED, RTL_R8(EEE_LED) & ~0x07);

	rtl_w1w0_eri(tp, 0x2fc, ERIAR_MASK_0001, 0x01, 0x06, ERIAR_EXGMAC);
	rtl_w1w0_eri(tp, 0x1b0, ERIAR_MASK_0011, 0x0000, 0x1000, ERIAR_EXGMAC);
}

static void rtl_hw_start_8168g_2(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	rtl_hw_start_8168g_1(tp);

	rtl_led_set(tp);

	/* disable aspm and clock request before access ephy */
	RTL_W8(CONFIG2, RTL_R8(CONFIG2) & ~CLK_REQ_EN);
	RTL_W8(CONFIG5, RTL_R8(CONFIG5) & ~ASPM_EN);
}

static void rtl_hw_start_8168(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u32 tmp;
	u32 phy_status;
	const struct soc_device_attribute rtk_soc_rtd161x_a01[] = {
		{
			.family = "Realtek Thor",
			.revision = "A01",
		},
		{
		/* empty */
		}
	};
	const struct soc_device_attribute rtk_soc_rtd131x[] = {
		{
			.family = "Realtek Hank",
		},
		{
		/* empty */
		}
	};

	RTL_W8(CFG9346, CFG9346_UNLOCK);

	RTL_W8(MAX_TX_PACKET_SIZE, TX_PACKET_MAX);

	rtl_set_rx_max_size(ioaddr, rx_buf_sz);

#if defined(CONFIG_RTL_RX_NO_COPY)
	tp->cp_cmd |= RTL_R16(C_PLUS_CMD) | PKT_CNTR_DISABLE | INTT_3;
#else
	tp->cp_cmd |= RTL_R16(C_PLUS_CMD) | PKT_CNTR_DISABLE | INTT_1;
#endif /* CONFIG_RTL_RX_NO_COPY */
	/* Disable VLAN De-tagging */
	tp->cp_cmd &= ~RX_VLAN;

	RTL_W16(C_PLUS_CMD, tp->cp_cmd);

#if defined(MY_ABC_HERE)
	RTL_W16(INTR_MITIGATE, 0x1111);
#else /* MY_ABC_HERE */
	RTL_W16(INTR_MITIGATE, 0x5151);
#endif /* MY_ABC_HERE */

	rtl_set_rx_tx_desc_registers(tp, ioaddr);

	rtl_set_rx_tx_config_registers(tp);

	if (tp->chip->features & RTL_FEATURE_TX_NO_CLOSE) {
		/* enable tx no close mode */
		rtl_ocp_write(tp, 0xE610,
			      rtl_ocp_read(tp, 0xE610) | (BIT(4) | BIT(6)));
	}

	/* disable pause frame resend caused by nearfull for rtd161x A01 */
	if (soc_device_match(rtk_soc_rtd161x_a01))
		rtl_ocp_write(tp, 0xE862, rtl_ocp_read(tp, 0xE862) | BIT(0));

	tp->event_slow &= ~RX_OVERFLOW;

	if (tp->chip->features & RTL_FEATURE_ADJUST_FIFO) {
		/* TX FIFO threshold */
		rtl_ocp_write(tp, 0xE618, 0x0006);
		rtl_ocp_write(tp, 0xE61A, 0x0010);

		/* RX FIFO threshold */
		rtl_ocp_write(tp, 0xC0A0, 0x0002);
		rtl_ocp_write(tp, 0xC0A2, 0x0008);

		phy_status = rtl_ocp_read(tp, 0xde40);
		if ((phy_status & 0x0030) == 0x0020) {
			/* 1000 Mbps */
			rtl_ocp_write(tp, 0xC0A4, 0x0088);
			rtl_ocp_write(tp, 0xC0A8, 0x00A8);
		} else {
			/* 10/100 Mbps */
			rtl_ocp_write(tp, 0xC0A4, 0x0038);
			rtl_ocp_write(tp, 0xC0A8, 0x0048);
		}
	} else {
		/* TX FIFO threshold */
		rtl_ocp_write(tp, 0xE618, 0x0006);
		rtl_ocp_write(tp, 0xE61A, 0x0010);

		/* RX FIFO threshold */
		rtl_ocp_write(tp, 0xC0A0, 0x0002);
		rtl_ocp_write(tp, 0xC0A2, 0x0008);
		rtl_ocp_write(tp, 0xC0A4, 0x0038);
		rtl_ocp_write(tp, 0xC0A8, 0x0048);
	}

	tp->chip->wakeup_set(tp, false);
	RTL_W8(CONFIG5, RTL_R8(CONFIG5) & ~LAN_WAKE);
	RTL_W8(CONFIG3, RTL_R8(CONFIG3) & ~MAGIC_PKT);

	RTL_R8(INTR_MASK);

	rtl_hw_start_8168g_2(tp);

	RTL_W8(CFG9346, CFG9346_LOCK);

	RTL_W8(CHIP_CMD, CMD_TX_ENB | CMD_RX_ENB);

	if (soc_device_match(rtk_soc_rtd131x)) {
		phy_status = rtl_ocp_read(tp, 0xde40);
		switch (tp->output_mode) {
		case OUTPUT_EMBEDDED_PHY:
			/* do nothing */
			break;
		case OUTPUT_RMII:
			/* adjust RMII interface setting, speed */
			tmp = rtl_ocp_read(tp, 0xea30) & ~(BIT(6) | BIT(5));
			switch (phy_status & 0x0030) { /* link speed */
			case 0x0000:
				/* 10M, RGMII clock speed = 2.5MHz */
				break;
			case 0x0010:
				/* 100M, RGMII clock speed = 25MHz */
				tmp |= BIT(5);
			}
			/* adjust RGMII interface setting, duplex */
			if ((phy_status & BIT(3)) == 0)
				/* ETN spec, half duplex */
				tmp &= ~BIT(4);
			else	/* ETN spec, full duplex */
				tmp |= BIT(4);
			rtl_ocp_write(tp, 0xea30, tmp);
			break;
		case OUTPUT_RGMII_TO_MAC:
		case OUTPUT_RGMII_TO_PHY:
			/* adjust RGMII interface setting, duplex */
			tmp = rtl_ocp_read(tp, 0xea34) & ~(BIT(4) | BIT(3));
			switch (phy_status & 0x0030) { /* link speed */
			case 0x0000:
				/* 10M, RGMII clock speed = 2.5MHz */
				break;
			case 0x0010:
				/* 100M, RGMII clock speed = 25MHz */
				tmp |= BIT(3);
				break;
			case 0x0020:
				/* 1000M, RGMII clock speed = 125MHz */
				tmp |= BIT(4);
				break;
			}
			/* adjust RGMII interface setting, duplex */
			if ((phy_status & BIT(3)) == 0)
				/* ETN spec, half duplex */
				tmp &= ~BIT(2);
			else	/* ETN spec, full duplex */
				tmp |= BIT(2);
			rtl_ocp_write(tp, 0xea34, tmp);
			break;
		default:
			pr_err(PFX "invalid output mode %d\n", tp->output_mode);
			return;
		}
	}

	rtl_set_rx_mode(dev);

	RTL_W16(MULTI_INTR, RTL_R16(MULTI_INTR) & 0xF000);
}

static int rtl8169_change_mtu(struct net_device *dev, int new_mtu)
{
#if defined(CONFIG_RTL_RX_NO_COPY)
	struct rtl8169_private *tp = netdev_priv(dev);

	if (!netif_running(dev)) {
		rx_buf_sz_new = (new_mtu > ETH_DATA_LEN) ?
			new_mtu + ETH_HLEN + 8 + 1 : RX_BUF_SIZE;
		rx_buf_sz = rx_buf_sz_new;
		goto out;
	}

	rx_buf_sz_new = (new_mtu > ETH_DATA_LEN) ?
		new_mtu + ETH_HLEN + 8 + 1 : RX_BUF_SIZE;

	rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);

out:
#endif /* CONFIG_RTL_RX_NO_COPY */

	dev->mtu = new_mtu;
	netdev_update_features(dev);

	return 0;
}

static inline void rtl8169_make_unusable_by_asic(struct rx_desc *desc)
{
	desc->addr = cpu_to_le64(0x0badbadbadbadbadull);
	desc->opts1 &= ~cpu_to_le32(DESC_OWN | RSVD_MASK);
}

#if defined(CONFIG_RTL_RX_NO_COPY)
static void rtl8169_free_rx_databuff(struct rtl8169_private *tp,
				     struct sk_buff **data_buff,
				     struct rx_desc *desc)
{
	if (!tp->acp_enable)
		dma_unmap_single(&tp->pdev->dev, le64_to_cpu(desc->addr),
				 rx_buf_sz, DMA_FROM_DEVICE);

	dev_kfree_skb(*data_buff);
	*data_buff = NULL;
	rtl8169_make_unusable_by_asic(desc);
}
#else
static void rtl8169_free_rx_databuff(struct rtl8169_private *tp,
				     void **data_buff, struct rx_desc *desc)
{
	if (!tp->acp_enable)
		dma_unmap_single(&tp->pdev->dev, le64_to_cpu(desc->addr),
				 rx_buf_sz, DMA_FROM_DEVICE);

	kfree(*data_buff);
	*data_buff = NULL;
	rtl8169_make_unusable_by_asic(desc);
}
#endif /* CONFIG_RTL_RX_NO_COPY */

static inline void rtl8169_mark_to_asic(struct rx_desc *desc, u32 rx_buf_sz)
{
	u32 eor = le32_to_cpu(desc->opts1) & RING_END;

	desc->opts1 = cpu_to_le32(DESC_OWN | eor | rx_buf_sz);
}

static inline void rtl8169_map_to_asic(struct rx_desc *desc, dma_addr_t mapping,
				       u32 rx_buf_sz)
{
	desc->addr = cpu_to_le64(mapping);
	wmb(); /* make sure this RX descriptor is ready */
	rtl8169_mark_to_asic(desc, rx_buf_sz);
}

static inline void *rtl8169_align(void *data)
{
	return (void *)ALIGN((long)data, 16);
}

#if defined(CONFIG_RTL_RX_NO_COPY)
static int
rtl8168_alloc_rx_skb(struct rtl8169_private *tp, struct sk_buff **sk_buff,
		     struct rx_desc *desc, int rx_buf_sz)
{
	struct device *d = &tp->pdev->dev;
	struct sk_buff *skb;
	dma_addr_t mapping;
	int ret = 0;

	skb = dev_alloc_skb(rx_buf_sz + RTK_RX_ALIGN);
	if (!skb)
		goto err_out;

	skb_reserve(skb, RTK_RX_ALIGN);

	if (tp->acp_enable) {
		mapping = virt_to_phys(skb->data);
	} else {
		mapping = dma_map_single(d, skb->data, rx_buf_sz,
					 DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(d, mapping))) {
			if (unlikely(net_ratelimit()))
				netif_err(tp, drv, tp->dev,
					  "Failed to map RX DMA!\n");
			goto err_out;
		}
	}
	*sk_buff = skb;
	rtl8169_map_to_asic(desc, mapping, rx_buf_sz);

out:
	return ret;

err_out:
	if (skb)
		dev_kfree_skb(skb);
	ret = -ENOMEM;
	rtl8169_make_unusable_by_asic(desc);
	goto out;
}
#else
static struct sk_buff *rtl8169_alloc_rx_data(struct rtl8169_private *tp,
					     struct rx_desc *desc)
{
	void *data;
	dma_addr_t mapping;
	struct device *d = &tp->pdev->dev;
	struct net_device *dev = tp->dev;
	int node = dev->dev.parent ? dev_to_node(dev->dev.parent) : -1;

	data = kmalloc_node(rx_buf_sz, GFP_KERNEL, node);
	if (!data)
		return NULL;

	if (rtl8169_align(data) != data) {
		kfree(data);
		data = kmalloc_node(rx_buf_sz + 15, GFP_KERNEL, node);
		if (!data)
			return NULL;
	}

	if (tp->acp_enable) {
		mapping = virt_to_phys(rtl8169_align(data));
	} else {
		mapping = dma_map_single(d, rtl8169_align(data), rx_buf_sz,
					 DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(d, mapping))) {
			if (net_ratelimit())
				netif_err(tp, drv, tp->dev,
					  "Failed to map RX DMA!\n");
			goto err_out;
		}
	}

	rtl8169_map_to_asic(desc, mapping, rx_buf_sz);
	return data;

err_out:
	kfree(data);
	return NULL;
}
#endif /* CONFIG_RTL_RX_NO_COPY */

static void rtl8169_rx_clear(struct rtl8169_private *tp)
{
	unsigned int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		if (tp->rx_databuff[i]) {
			rtl8169_free_rx_databuff(tp, tp->rx_databuff + i,
						 tp->rx_desc_array + i);
		}
	}
}

static inline void rtl8169_mark_as_last_descriptor(struct rx_desc *desc)
{
	desc->opts1 |= cpu_to_le32(RING_END);
}

#if defined(CONFIG_RTL_RX_NO_COPY)
static u32
rtl8168_rx_fill(struct rtl8169_private *tp,
		struct net_device *dev, u32 start, u32 end)
{
	u32 cur;

	for (cur = start; end - cur > 0; cur++) {
		int ret, i = cur % NUM_RX_DESC;

		if (tp->rx_databuff[i])
			continue;
		ret = rtl8168_alloc_rx_skb(tp, tp->rx_databuff + i,
					   tp->rx_desc_array + i, rx_buf_sz);
		if (ret < 0)
			break;
		if (i == (NUM_RX_DESC - 1))
			rtl8169_mark_as_last_descriptor(tp->rx_desc_array +
							NUM_RX_DESC - 1);
	}
	return cur - start;
}
#else
static int rtl8169_rx_fill(struct rtl8169_private *tp)
{
	unsigned int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		void *data;

		if (tp->rx_databuff[i])
			continue;

		data = rtl8169_alloc_rx_data(tp, tp->rx_desc_array + i);
		if (!data) {
			rtl8169_make_unusable_by_asic(tp->rx_desc_array + i);
			goto err_out;
		}
		tp->rx_databuff[i] = data;
	}

	rtl8169_mark_as_last_descriptor(tp->rx_desc_array + NUM_RX_DESC - 1);
	return 0;

err_out:
	rtl8169_rx_clear(tp);
	return -ENOMEM;
}
#endif /* CONFIG_RTL_RX_NO_COPY */

static int rtl8169_init_ring(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret;

	rtl8169_init_ring_indexes(tp);

	memset(tp->tx_skb, 0x0, NUM_TX_DESC * sizeof(struct ring_info));
	memset(tp->rx_databuff, 0x0, NUM_RX_DESC * sizeof(void *));

#if defined(CONFIG_RTL_RX_NO_COPY)
	ret = rtl8168_rx_fill(tp, dev, 0, NUM_RX_DESC);
	if (ret < NUM_RX_DESC)
		ret = -ENOMEM;
	else
		ret = 0;
#else
	ret = rtl8169_rx_fill(tp);
#endif /* CONFIG_RTL_RX_NO_COPY */

	return ret;
}

static void rtl8169_unmap_tx_skb(struct rtl8169_private *tp, struct device *d,
				 struct ring_info *tx_skb, struct tx_desc *desc)
{
	unsigned int len = tx_skb->len;

	if (!tp->acp_enable)
		dma_unmap_single(d, le64_to_cpu(desc->addr), len,
				 DMA_TO_DEVICE);

	desc->opts1 = 0x00;
	desc->opts2 = 0x00;
	desc->addr = 0x00;
	tx_skb->len = 0;
}

static void rtl8169_tx_clear_range(struct rtl8169_private *tp, u32 start,
				   unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		unsigned int entry = (start + i) % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		unsigned int len = tx_skb->len;

		if (len) {
			struct sk_buff *skb = tx_skb->skb;

			rtl8169_unmap_tx_skb(tp, &tp->pdev->dev, tx_skb,
					     tp->tx_desc_array + entry);
			if (skb) {
				tp->dev->stats.tx_dropped++;
				dev_kfree_skb(skb);
				tx_skb->skb = NULL;
			}
		}
	}
}

static void rtl8169_tx_clear(struct rtl8169_private *tp)
{
	rtl8169_tx_clear_range(tp, tp->dirty_tx, NUM_TX_DESC);
	tp->cur_tx = 0;
	tp->dirty_tx = 0;
}

static void rtl_reset_work(struct rtl8169_private *tp)
{
	struct net_device *dev = tp->dev;
	int i;

	napi_disable(&tp->napi);
	netif_stop_queue(dev);
	synchronize_rcu();

	rtl8169_hw_reset(tp);

#if defined(CONFIG_RTL_RX_NO_COPY)
	rtl8169_rx_clear(tp);
	rtl8169_tx_clear(tp);

	if (rx_buf_sz_new != rx_buf_sz)
		rx_buf_sz = rx_buf_sz_new;

	memset(tp->tx_desc_array, 0x0, NUM_TX_DESC * sizeof(struct tx_desc));
	for (i = 0; i < NUM_TX_DESC; i++) {
		if (i == (NUM_TX_DESC - 1))
			tp->tx_desc_array[i].opts1 = cpu_to_le32(RING_END);
	}
	memset(tp->rx_desc_array, 0x0, NUM_RX_DESC * sizeof(struct rx_desc));

	if (rtl8169_init_ring(dev) < 0) {
		napi_enable(&tp->napi);
		netif_wake_queue(dev);
		netif_warn(tp, drv, dev, "No memory. Try to restart......\n");
		msleep(1000);
		rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
		return;
	}
#else
	for (i = 0; i < NUM_RX_DESC; i++)
		rtl8169_mark_to_asic(tp->rx_desc_array + i, rx_buf_sz);

	rtl8169_tx_clear(tp);
	rtl8169_init_ring_indexes(tp);
#endif /* CONFIG_RTL_RX_NO_COPY */

	napi_enable(&tp->napi);
	rtl_hw_start(dev);
	netif_wake_queue(dev);
	rtl8169_check_link_status(dev, tp, tp->mmio_addr);
}

static void rtl8169_tx_timeout(struct net_device *dev, unsigned int nouse)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
}

static int rtl8169_xmit_frags(struct rtl8169_private *tp, struct sk_buff *skb,
			      u32 *opts)
{
	struct skb_shared_info *info = skb_shinfo(skb);
	unsigned int cur_frag, entry;
	struct tx_desc *txd = NULL;
	struct device *d = &tp->pdev->dev;

	entry = tp->cur_tx;
	for (cur_frag = 0; cur_frag < info->nr_frags; cur_frag++) {
		const skb_frag_t *frag = info->frags + cur_frag;
		dma_addr_t mapping;
		u32 status, len;
		void *addr;

		entry = (entry + 1) % NUM_TX_DESC;

		txd = tp->tx_desc_array + entry;
		len = skb_frag_size(frag);
		addr = skb_frag_address(frag);
		if (tp->acp_enable) {
			mapping = virt_to_phys(addr);
		} else {
			mapping = dma_map_single(d, addr, len, DMA_TO_DEVICE);
			if (unlikely(dma_mapping_error(d, mapping))) {
				if (net_ratelimit())
					netif_err(tp, drv, tp->dev,
						  "Failed to map TX fragments DMA!\n");
				goto err_out;
			}
		}

		/* Anti gcc 2.95.3 bugware (sic) */
		status = opts[0] | len |
			(RING_END * !((entry + 1) % NUM_TX_DESC));

		txd->opts1 = cpu_to_le32(status);
		txd->opts2 = cpu_to_le32(opts[1]);
		txd->addr = cpu_to_le64(mapping);

		tp->tx_skb[entry].len = len;
	}

	if (cur_frag) {
		tp->tx_skb[entry].skb = skb;
		txd->opts1 |= cpu_to_le32(LAST_FRAG);
	}

	return cur_frag;

err_out:
	rtl8169_tx_clear_range(tp, tp->cur_tx + 1, cur_frag);
	return -EIO;
}

static inline bool rtl8169_tso_csum(struct rtl8169_private *tp,
				    struct sk_buff *skb, u32 *opts)
{
	const struct rtl_tx_desc_info *info = &tx_desc_info;
	u32 mss = skb_shinfo(skb)->gso_size;
	int offset = info->opts_offset;

	if (mss) {
		opts[0] |= TD_LSO;
		opts[offset] |= min(mss, TD_MSS_MAX) << info->mss_shift;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const struct iphdr *ip = ip_hdr(skb);

		if (ip->protocol == IPPROTO_TCP)
			opts[offset] |= info->checksum.tcp;
		else if (ip->protocol == IPPROTO_UDP)
			opts[offset] |= info->checksum.udp;
		else
			WARN_ON_ONCE(1);
	}
	return true;
}

static inline bool rtl_tx_slots_avail(struct rtl8169_private *tp,
				      unsigned int nr_frags)
{
	unsigned int slots_avail = tp->dirty_tx + NUM_TX_DESC - tp->cur_tx;

	/* A skbuff with nr_frags needs nr_frags+1 entries in the tx queue */
	return slots_avail > nr_frags;
}

/* TX w/ own flag */
static netdev_tx_t rtl_start_xmit(struct sk_buff *skb,
				  struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned int entry = tp->cur_tx % NUM_TX_DESC;
	struct tx_desc *txd = tp->tx_desc_array + entry;
	void __iomem *ioaddr = tp->mmio_addr;
	struct device *d = &tp->pdev->dev;
	dma_addr_t mapping;
	u32 status, len;
	u32 opts[2];
	int frags;

	if (unlikely(!rtl_tx_slots_avail(tp, skb_shinfo(skb)->nr_frags))) {
		netif_err(tp, drv, dev,
			  "BUG! Tx Ring full when queue awake!\n");
		goto err_stop_0;
	}
	if (unlikely(le32_to_cpu(txd->opts1) & DESC_OWN))
		goto err_stop_0;

	opts[1] = cpu_to_le32(rtl8169_tx_vlan_tag(skb));
	opts[0] = DESC_OWN;

	if (!rtl8169_tso_csum(tp, skb, opts))
		goto err_update_stats;

	len = skb_headlen(skb);
	if (tp->acp_enable) {
		mapping = virt_to_phys(skb->data);
	} else {
		mapping = dma_map_single(d, skb->data, len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(d, mapping))) {
			if (net_ratelimit())
				netif_err(tp, drv, dev,
					  "Failed to map TX DMA!\n");
			goto err_dma_0;
		}
	}

	tp->tx_skb[entry].len = len;
	txd->addr = cpu_to_le64(mapping);

	frags = rtl8169_xmit_frags(tp, skb, opts);
	if (frags < 0) {
		goto err_dma_1;
	} else if (frags) {
		opts[0] |= FIRST_FRAG;
	} else {
		opts[0] |= FIRST_FRAG | LAST_FRAG;
		tp->tx_skb[entry].skb = skb;
	}

	txd->opts2 = cpu_to_le32(opts[1]);

	skb_tx_timestamp(skb);

	wmb(); /* make sure txd->addr and txd->opts2 is ready */

	/* Anti gcc 2.95.3 bugware (sic) */
	status = opts[0] | len | (RING_END * !((entry + 1) % NUM_TX_DESC));
	txd->opts1 = cpu_to_le32(status);

	tp->cur_tx += frags + 1;

	wmb(); /* make sure this TX descriptor is ready */

	RTL_W8(TX_POLL, NPQ);

	if (!rtl_tx_slots_avail(tp, MAX_SKB_FRAGS)) {
		/* Avoid wrongly optimistic queue wake-up: rtl_tx thread must
		 * not miss a ring update when it notices a stopped queue.
		 */
		smp_wmb();
		netif_stop_queue(dev);
		/* Sync with rtl_tx:
		 * - publish queue status and cur_tx ring index (write barrier)
		 * - refresh dirty_tx ring index (read barrier).
		 * May the current thread have a pessimistic view of the ring
		 * status and forget to wake up queue, a racing rtl_tx thread
		 * can't.
		 */
		smp_mb();
		if (rtl_tx_slots_avail(tp, MAX_SKB_FRAGS))
			netif_wake_queue(dev);
	}

	return NETDEV_TX_OK;

err_dma_1:
	rtl8169_unmap_tx_skb(tp, d, tp->tx_skb + entry, txd);
err_dma_0:
	dev_kfree_skb(skb);
err_update_stats:
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;

err_stop_0:
	netif_stop_queue(dev);
	dev->stats.tx_dropped++;
	return NETDEV_TX_BUSY;
}

static void rtl_tx(struct net_device *dev, struct rtl8169_private *tp)
{
	unsigned int dirty_tx, tx_left;

	dirty_tx = tp->dirty_tx;
	smp_rmb(); /* make sure dirty_tx is updated */
	tx_left = tp->cur_tx - dirty_tx;

	while (tx_left > 0) {
		unsigned int entry = dirty_tx % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		u32 status;

		rmb(); /* make sure this TX descriptor is ready */
		status = le32_to_cpu(tp->tx_desc_array[entry].opts1);
		if (status & DESC_OWN)
			break;

		rtl8169_unmap_tx_skb(tp, &tp->pdev->dev, tx_skb,
				     tp->tx_desc_array + entry);
		if (status & LAST_FRAG) {
			u64_stats_update_begin(&tp->tx_stats.syncp);
			tp->tx_stats.packets++;
			tp->tx_stats.bytes += tx_skb->skb->len;
			u64_stats_update_end(&tp->tx_stats.syncp);
			dev_kfree_skb(tx_skb->skb);
			tx_skb->skb = NULL;
		}
		dirty_tx++;
		tx_left--;
	}

	if (tp->dirty_tx != dirty_tx) {
		tp->dirty_tx = dirty_tx;
		/* Sync with rtl_start_xmit:
		 * - publish dirty_tx ring index (write barrier)
		 * - refresh cur_tx ring index and queue status (read barrier)
		 * May the current thread miss the stopped queue condition,
		 * a racing xmit thread can only have a right view of the
		 * ring status.
		 */
		smp_mb();
		if (netif_queue_stopped(dev) &&
		    rtl_tx_slots_avail(tp, MAX_SKB_FRAGS)) {
			netif_wake_queue(dev);
		}
		/* 8168 hack: TX_POLL requests are lost when the Tx packets are
		 * too close. Let's kick an extra TX_POLL request when a burst
		 * of start_xmit activity is detected (if it is not detected,
		 * it is slow enough). -- FR
		 */
		if (tp->cur_tx != dirty_tx) {
			void __iomem *ioaddr = tp->mmio_addr;

			RTL_W8(TX_POLL, NPQ);
		}
	}
}

/* TX w/o own flag;
 * start of RTL_FEATURE_TX_NO_CLOSE
 */
static netdev_tx_t rtl_start_xmit_no_close(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned int entry = tp->cur_tx % NUM_TX_DESC;
	struct tx_desc *txd = tp->tx_desc_array + entry;
	void __iomem *ioaddr = tp->mmio_addr;
	struct device *d = &tp->pdev->dev;
	dma_addr_t mapping;
	u32 status, len;
	u32 opts[2];
	int frags;
	u16 close_idx;
	u16 tail_idx;

	if (unlikely(!rtl_tx_slots_avail(tp, skb_shinfo(skb)->nr_frags))) {
		netif_err(tp, drv, dev,
			  "BUG! Tx Ring full when queue awake!\n");
		goto err_stop_0;
	}

	close_idx = RTL_R16(TX_DESC_CLOSE_IDX) & TX_DESC_CNT_MASK;
	tail_idx = tp->cur_tx & TX_DESC_CNT_MASK;
	if ((tail_idx > close_idx && (tail_idx - close_idx == NUM_TX_DESC)) ||
	    (tail_idx < close_idx &&
	     (TX_DESC_CNT_SIZE - close_idx + tail_idx == NUM_TX_DESC)))
		goto err_stop_0;

	opts[1] = cpu_to_le32(rtl8169_tx_vlan_tag(skb));
	opts[0] = DESC_OWN;

	if (!rtl8169_tso_csum(tp, skb, opts))
		goto err_update_stats;

	len = skb_headlen(skb);
	if (tp->acp_enable) {
		mapping = virt_to_phys(skb->data);
	} else {
		mapping = dma_map_single(d, skb->data, len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(d, mapping))) {
			if (net_ratelimit())
				netif_err(tp, drv, dev,
					  "Failed to map TX DMA!\n");
			goto err_dma_0;
		}
	}

	tp->tx_skb[entry].len = len;
	txd->addr = cpu_to_le64(mapping);

	frags = rtl8169_xmit_frags(tp, skb, opts);
	if (frags < 0) {
		goto err_dma_1;
	} else if (frags) {
		opts[0] |= FIRST_FRAG;
	} else {
		opts[0] |= FIRST_FRAG | LAST_FRAG;
		tp->tx_skb[entry].skb = skb;
	}

	txd->opts2 = cpu_to_le32(opts[1]);

	skb_tx_timestamp(skb);

	wmb(); /* make sure txd->addr and txd->opts2 is ready */

	/* Anti gcc 2.95.3 bugware (sic) */
	status = opts[0] | len | (RING_END * !((entry + 1) % NUM_TX_DESC));
	txd->opts1 = cpu_to_le32(status);

	tp->cur_tx += frags + 1;

	RTL_W16(TX_DESC_TAIL_IDX, tp->cur_tx & TX_DESC_CNT_MASK);

	wmb(); /* make sure this TX descriptor is ready */

	RTL_W8(TX_POLL, NPQ);

	if (!rtl_tx_slots_avail(tp, MAX_SKB_FRAGS)) {
		/* Avoid wrongly optimistic queue wake-up:
		 * rtl_tx_no_close thread must
		 * not miss a ring update when it notices a stopped queue.
		 */
		smp_wmb();
		netif_stop_queue(dev);
		/* Sync with rtl_tx_no_close:
		 * - publish queue status and cur_tx ring index (write barrier)
		 * - refresh dirty_tx ring index (read barrier).
		 * May the current thread have a pessimistic view of the ring
		 * status and forget to wake up queue, a racing rtl_tx thread
		 * can't.
		 */
		smp_mb();
		if (rtl_tx_slots_avail(tp, MAX_SKB_FRAGS))
			netif_wake_queue(dev);
	}

	return NETDEV_TX_OK;

err_dma_1:
	rtl8169_unmap_tx_skb(tp, d, tp->tx_skb + entry, txd);
err_dma_0:
	dev_kfree_skb(skb);
err_update_stats:
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;

err_stop_0:
	netif_stop_queue(dev);
	dev->stats.tx_dropped++;
	return NETDEV_TX_BUSY;
}

static void rtl_tx_no_close(struct net_device *dev, struct rtl8169_private *tp)
{
	unsigned int dirty_tx, tx_left;
	u16 close_idx;
	u16 dirty_tx_idx;
	void __iomem *ioaddr = tp->mmio_addr;

	dirty_tx = tp->dirty_tx;
	close_idx = RTL_R16(TX_DESC_CLOSE_IDX) & TX_DESC_CNT_MASK;
	dirty_tx_idx = dirty_tx & TX_DESC_CNT_MASK;
	smp_rmb(); /* make sure dirty_tx and close_idx are updated */

	if (dirty_tx_idx <= close_idx)
		tx_left = close_idx - dirty_tx_idx;
	else
		tx_left = close_idx + TX_DESC_CNT_SIZE - dirty_tx_idx;

	while (tx_left > 0) {
		unsigned int entry = dirty_tx % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		u32 status;

		rmb(); /* make sure this TX descriptor is ready */
		status = le32_to_cpu(tp->tx_desc_array[entry].opts1);

		rtl8169_unmap_tx_skb(tp, &tp->pdev->dev, tx_skb,
				     tp->tx_desc_array + entry);
		if (status & LAST_FRAG) {
			u64_stats_update_begin(&tp->tx_stats.syncp);
			tp->tx_stats.packets++;
			tp->tx_stats.bytes += tx_skb->skb->len;
			u64_stats_update_end(&tp->tx_stats.syncp);
			dev_kfree_skb(tx_skb->skb);
			tx_skb->skb = NULL;
		}
		dirty_tx++;
		tx_left--;
	}

	if (tp->dirty_tx != dirty_tx) {
		tp->dirty_tx = dirty_tx;
		/* Sync with rtl_start_xmit_no_close:
		 * - publish dirty_tx ring index (write barrier)
		 * - refresh cur_tx ring index and queue status (read barrier)
		 * May the current thread miss the stopped queue condition,
		 * a racing xmit thread can only have a right view of the
		 * ring status.
		 */
		smp_mb();
		if (netif_queue_stopped(dev) &&
		    rtl_tx_slots_avail(tp, MAX_SKB_FRAGS)) {
			netif_wake_queue(dev);
		}
		/* 8168 hack: TX_POLL requests are lost when the Tx packets are
		 * too close. Let's kick an extra TX_POLL request when a burst
		 * of start_xmit activity is detected (if it is not detected,
		 * it is slow enough). -- FR
		 */
		if (tp->cur_tx != dirty_tx) {
			void __iomem *ioaddr = tp->mmio_addr;

			RTL_W8(TX_POLL, NPQ);
		}
	}
}

/* end of RTL_FEATURE_TX_NO_CLOSE */

static netdev_tx_t rtl8169_start_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (tp->chip->features & RTL_FEATURE_TX_NO_CLOSE)
		return rtl_start_xmit_no_close(skb, dev);
	else
		return rtl_start_xmit(skb, dev);
}

static inline int rtl8169_fragmented_frame(u32 status)
{
	return (status & (FIRST_FRAG | LAST_FRAG)) != (FIRST_FRAG | LAST_FRAG);
}

static inline void rtl8169_rx_csum(struct sk_buff *skb, u32 opts1)
{
	u32 status = opts1 & RX_PROTO_MASK;

	if ((status == RX_PROTO_TCP && !(opts1 & TCP_FAIL)) ||
	    (status == RX_PROTO_UDP && !(opts1 & UDP_FAIL)))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb_checksum_none_assert(skb);
}

#if defined(CONFIG_RTL_RX_NO_COPY)
static int rtl_rx(struct net_device *dev, struct rtl8169_private *tp,
		  u32 budget)
{
	unsigned int cur_rx, rx_left;
	unsigned int count, delta;
	struct device *d = &tp->pdev->dev;

	cur_rx = tp->cur_rx;

	rx_left = NUM_RX_DESC + tp->dirty_rx - cur_rx;
	rx_left = min(budget, rx_left);

	for (; rx_left > 0; rx_left--, cur_rx++) {
		unsigned int entry = cur_rx % NUM_RX_DESC;
		struct rx_desc *desc = tp->rx_desc_array + entry;
		u32 status;

		rmb(); /* make sure this RX descriptor is ready */
		status = le32_to_cpu(desc->opts1) & tp->opts1_mask;

		if (status & DESC_OWN)
			break;
		if (unlikely(status & RX_RES)) {
			netif_info(tp, rx_err, dev, "Rx ERROR. status = %08x\n",
				   status);
			dev->stats.rx_errors++;
			if (status & (RX_RWT | RX_RUNT))
				dev->stats.rx_length_errors++;
			if (status & RX_CRC)
				dev->stats.rx_crc_errors++;
			if (status & RX_FOVF) {
				rtl_schedule_task(tp,
						  RTL_FLAG_TASK_RESET_PENDING);
				dev->stats.rx_fifo_errors++;
			}
			if ((status & (RX_RUNT | RX_CRC)) &&
			    !(status & (RX_RWT | RX_FOVF)) &&
			    (dev->features & NETIF_F_RXALL))
				goto process_pkt;
		} else {
			struct sk_buff *skb;
			dma_addr_t addr;
			int pkt_size;

process_pkt:

			skb = tp->rx_databuff[entry];
			addr = le64_to_cpu(desc->addr);
			if (likely(!(dev->features & NETIF_F_RXFCS)))
				pkt_size = (status & 0x00003fff) - 4;
			else
				pkt_size = status & 0x00003fff;

			/* The driver does not support incoming fragmented
			 * frames. They are seen as a symptom of over-mtu
			 * sized frames.
			 */
			if (unlikely(rtl8169_fragmented_frame(status))) {
				dev->stats.rx_dropped++;
				dev->stats.rx_length_errors++;
				continue;
			}

			if (!tp->acp_enable)
				dma_sync_single_for_cpu(d, addr, pkt_size,
							DMA_FROM_DEVICE);
			tp->rx_databuff[entry] = NULL;
			if (!tp->acp_enable)
				dma_unmap_single(d, addr, pkt_size,
						 DMA_FROM_DEVICE);

			rtl8169_rx_csum(skb, status);
			skb_put(skb, pkt_size);
			skb->protocol = eth_type_trans(skb, dev);

			rtl8169_rx_vlan_tag(desc, skb);

			napi_gro_receive(&tp->napi, skb);

			u64_stats_update_begin(&tp->rx_stats.syncp);
			tp->rx_stats.packets++;
			tp->rx_stats.bytes += pkt_size;
			u64_stats_update_end(&tp->rx_stats.syncp);
		}
	}

	count = cur_rx - tp->cur_rx;
	tp->cur_rx = cur_rx;

	delta = rtl8168_rx_fill(tp, dev, tp->dirty_rx, tp->cur_rx);
	/* netif_err(tp, drv, tp->dev, "delta =%x\n",delta); */
	tp->dirty_rx += delta;

	if (tp->dirty_rx + NUM_RX_DESC == tp->cur_rx) {
		rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
		netif_err(tp, drv, tp->dev, "%s: Rx buffers exhausted\n",
			  dev->name);
	}

	return count;
}
#else
static struct sk_buff *rtl8169_try_rx_copy(void *data,
					   struct rtl8169_private *tp,
					   int pkt_size, dma_addr_t addr)
{
	struct sk_buff *skb;
	struct device *d = &tp->pdev->dev;

	data = rtl8169_align(data);
	if (!tp->acp_enable)
		dma_sync_single_for_cpu(d, addr, pkt_size, DMA_FROM_DEVICE);
	prefetch(data);
	skb = netdev_alloc_skb_ip_align(tp->dev, pkt_size);
	if (skb)
		memcpy(skb->data, data, pkt_size);
	if (!tp->acp_enable)
		dma_sync_single_for_device(d, addr, pkt_size, DMA_FROM_DEVICE);

	return skb;
}

static int rtl_rx(struct net_device *dev, struct rtl8169_private *tp,
		  u32 budget)
{
	unsigned int cur_rx, rx_left;
	unsigned int count;
	const unsigned int num_rx_desc = NUM_RX_DESC;

	cur_rx = tp->cur_rx;

	for (rx_left = min(budget, num_rx_desc); rx_left > 0;
		rx_left--, cur_rx++) {
		unsigned int entry = cur_rx % NUM_RX_DESC;
		struct rx_desc *desc = tp->rx_desc_array + entry;
		u32 status;

		rmb(); /* make sure this RX descriptor is ready */
		status = le32_to_cpu(desc->opts1) & tp->opts1_mask;

		if (status & DESC_OWN)
			break;
		if (unlikely(status & RX_RES)) {
			netif_info(tp, rx_err, dev, "Rx ERROR. status = %08x\n",
				   status);
			dev->stats.rx_errors++;
			if (status & (RX_RWT | RX_RUNT))
				dev->stats.rx_length_errors++;
			if (status & RX_CRC)
				dev->stats.rx_crc_errors++;
			if (status & RX_FOVF) {
				rtl_schedule_task(tp,
						  RTL_FLAG_TASK_RESET_PENDING);
				dev->stats.rx_fifo_errors++;
			}
			if ((status & (RX_RUNT | RX_CRC)) &&
			    !(status & (RX_RWT | RX_FOVF)) &&
			    (dev->features & NETIF_F_RXALL))
				goto process_pkt;
		} else {
			struct sk_buff *skb;
			dma_addr_t addr;
			int pkt_size;

process_pkt:
			addr = le64_to_cpu(desc->addr);
			if (likely(!(dev->features & NETIF_F_RXFCS)))
				pkt_size = (status & 0x00003fff) - 4;
			else
				pkt_size = status & 0x00003fff;

			/* The driver does not support incoming fragmented
			 * frames. They are seen as a symptom of over-mtu
			 * sized frames.
			 */
			if (unlikely(rtl8169_fragmented_frame(status))) {
				dev->stats.rx_dropped++;
				dev->stats.rx_length_errors++;
				goto release_descriptor;
			}

			skb = rtl8169_try_rx_copy(tp->rx_databuff[entry],
						  tp, pkt_size, addr);
			if (!skb) {
				dev->stats.rx_dropped++;
				goto release_descriptor;
			}

			rtl8169_rx_csum(skb, status);
			skb_put(skb, pkt_size);
			skb->protocol = eth_type_trans(skb, dev);

			rtl8169_rx_vlan_tag(desc, skb);

			napi_gro_receive(&tp->napi, skb);

			u64_stats_update_begin(&tp->rx_stats.syncp);
			tp->rx_stats.packets++;
			tp->rx_stats.bytes += pkt_size;
			u64_stats_update_end(&tp->rx_stats.syncp);
		}
release_descriptor:
		desc->opts2 = 0;
		wmb(); /* make sure this RX descriptor is useless */
		rtl8169_mark_to_asic(desc, rx_buf_sz);
	}

	count = cur_rx - tp->cur_rx;
	tp->cur_rx = cur_rx;

	return count;
}
#endif /* CONFIG_RTL_RX_NO_COPY */

static irqreturn_t phy_irq_handler(int irq, void *dev_instance)
{
	struct rtl8169_private *tp = dev_instance;
	u32 isr;
	u32 por;
	int i;
	bool hit = false;

	regmap_read(tp->iso_base, ISO_UMSK_ISR, &isr);
	pr_info(PFX "ISO_UMSK_ISR\t[0x98007004] = %08x\n", isr);
	regmap_read(tp->iso_base, ISO_POR_DATAI, &por);
	pr_info(PFX "ISO_POR_DATAI\t[0x98007218] = %08x\n", por);

	for (i = 0; i < tp->phy_irq_num; i++) {
		if (irq == tp->phy_irq[i] &&
		    (isr & (1 << tp->phy_irq_map[i]))) {
			pr_info(PFX "phy_0: get IRQ %d for GPHY POR interrupt (%d)\n",
				irq, tp->phy_irq_map[i]);
			regmap_write(tp->iso_base, ISO_UMSK_ISR, 1 << tp->phy_irq_map[i]);
			hit = true;
			break;
		}
	}

	if (hit) {
		if ((por & tp->phy_por_xv_mask) != tp->phy_por_xv_mask) {
			pr_err(PFX "phy_0: GPHY has power issue (0x%x)\n", por);
			goto out;
		}

		if (!test_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags))
			rtl_phy_reinit(tp);
		else if (atomic_inc_return(&tp->phy_reinit_flag) == 1)
			rtl_schedule_task(tp, RTL_FLAG_TASK_PHY_PENDING);
		else
			atomic_dec(&tp->phy_reinit_flag);
	}

out:
	return IRQ_RETVAL(IRQ_HANDLED);
}

static irqreturn_t rtl8169_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct rtl8169_private *tp = netdev_priv(dev);
	int handled = 0;
	u16 status;

	status = rtl_get_events(tp);
	if (status && status != 0xffff) {
		status &= RTL_EVENT_NAPI | tp->event_slow;
		if (status) {
			handled = 1;

			rtl_irq_disable(tp);
			napi_schedule(&tp->napi);
		}
	}
	return IRQ_RETVAL(handled);
}

/* Workqueue context.
 */
static void rtl_slow_event_work(struct rtl8169_private *tp)
{
	struct net_device *dev = tp->dev;
	void __iomem *ioaddr = tp->mmio_addr;
	u16 status;

	status = rtl_get_events(tp) & tp->event_slow;
	rtl_ack_events(tp, status);

	if (status & LINK_CHG) {
		__rtl8169_check_link_status(dev, tp, tp->mmio_addr, true);

		/* prevet ALDPS enter MAC Powercut Tx/Rx disable */
		/* use MAC reset to set counter to offset 0 */
		if (tp->chip->link_ok(ioaddr))
			rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
	}

	rtl_irq_enable_all(tp);
}

static void rtl_task(struct work_struct *work)
{
	static const struct {
		int bitnr;
		void (*action)(struct rtl8169_private *tp);
	} rtl_work[] = {
		/* XXX - keep rtl_slow_event_work() as first element. */
		{ RTL_FLAG_TASK_SLOW_PENDING,	rtl_slow_event_work },
		{ RTL_FLAG_TASK_RESET_PENDING,	rtl_reset_work },
		{ RTL_FLAG_TASK_PHY_PENDING,	rtl_phy_work }
	};
	struct rtl8169_private *tp =
		container_of(work, struct rtl8169_private, wk.work);
	struct net_device *dev = tp->dev;
	int i;

	rtl_lock_work(tp);

	if (!netif_running(dev) ||
	    !test_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags))
		goto out_unlock;

	for (i = 0; i < ARRAY_SIZE(rtl_work); i++) {
		bool pending;

		pending = test_and_clear_bit(rtl_work[i].bitnr, tp->wk.flags);
		if (pending)
			rtl_work[i].action(tp);
	}

out_unlock:
	rtl_unlock_work(tp);
}

static int rtl8169_poll(struct napi_struct *napi, int budget)
{
	struct rtl8169_private *tp =
		container_of(napi, struct rtl8169_private, napi);
	struct net_device *dev = tp->dev;
	u16 enable_mask = RTL_EVENT_NAPI | tp->event_slow;
	int work_done = 0;
	u16 status;

#if defined(CONFIG_RTL_RX_NO_COPY)
	u32 old_dirty_rx;
#endif /* CONFIG_RTL_RX_NO_COPY */

	status = rtl_get_events(tp);
	rtl_ack_events(tp, status & ~(tp->event_slow | RX_OVERFLOW));

#if defined(CONFIG_RTL_RX_NO_COPY)
	old_dirty_rx = tp->dirty_rx;
#endif /* CONFIG_RTL_RX_NO_COPY */

	/* always check rx queue */
	work_done = rtl_rx(dev, tp, (u32)budget);

#if defined(CONFIG_RTL_RX_NO_COPY)
	if ((status & RX_OVERFLOW) && tp->dirty_rx != old_dirty_rx)
		rtl_ack_events(tp, RX_OVERFLOW);
#else
	if ((status & RX_OVERFLOW) && work_done > 0)
		rtl_ack_events(tp, RX_OVERFLOW);
#endif /* CONFIG_RTL_RX_NO_COPY */

	if (status & RTL_EVENT_NAPI_TX) {
		if (tp->chip->features & RTL_FEATURE_TX_NO_CLOSE)
			rtl_tx_no_close(dev, tp);
		else
			rtl_tx(dev, tp);
	}

	if (status & tp->event_slow) {
		enable_mask &= ~tp->event_slow;

		rtl_schedule_task(tp, RTL_FLAG_TASK_SLOW_PENDING);
	}

	if (work_done < budget) {
		napi_complete(napi);

		rtl_irq_enable(tp, enable_mask);
	}

	return work_done;
}

static void rtl8169_down(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	napi_disable(&tp->napi);
	netif_stop_queue(dev);

	rtl8169_hw_reset(tp);
	/* At this point device interrupts can not be enabled in any function,
	 * as netif_running is not true (rtl8169_interrupt, rtl8169_reset_task)
	 * and napi is disabled (rtl8169_poll).
	 */

	/* Give a racing hard_start_xmit a few cycles to complete. */
	synchronize_rcu();

	rtl8169_tx_clear(tp);

	rtl8169_rx_clear(tp);

	rtl_pll_power_down(tp);

	tp->status |= RTL_STATUS_DOWN;
}

static int rtl8169_close(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct platform_device *pdev = tp->pdev;

	/* Update counters before going down */
	rtl8169_update_counters(dev);

	rtl_lock_work(tp);
	clear_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);

	rtl8169_down(dev);

	rtl_unlock_work(tp);

	free_irq(dev->irq, dev);

	if (tp->acp_enable) {
		kfree(tp->rx_desc_array);
		kfree(tp->tx_desc_array);
	} else {
		dma_free_coherent(&pdev->dev, R8169_RX_RING_BYTES,
				  tp->rx_desc_array, tp->rx_phy_addr);
		dma_free_coherent(&pdev->dev, R8169_TX_RING_BYTES,
				  tp->tx_desc_array, tp->tx_phy_addr);
	}
	tp->tx_desc_array = NULL;
	tp->rx_desc_array = NULL;

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void rtl8169_netpoll(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_interrupt(tp->pdev->irq, dev);
}
#endif

static int rtl_open(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct platform_device *pdev = tp->pdev;
	int retval = -ENOMEM;
	int node = dev->dev.parent ? dev_to_node(dev->dev.parent) : -1;

	/* Rx and Tx descriptors needs 256 bytes alignment.
	 * dma_alloc_coherent provides more.
	 */
	if (tp->acp_enable) {
		tp->tx_desc_array = kzalloc_node(R8169_TX_RING_BYTES,
						 GFP_KERNEL, node);
		tp->tx_phy_addr = virt_to_phys(tp->tx_desc_array);
	} else {
		tp->tx_desc_array = dma_alloc_coherent(&pdev->dev,
						       R8169_TX_RING_BYTES,
						       &tp->tx_phy_addr,
						       GFP_KERNEL);
	}
	if (!tp->tx_desc_array)
		goto err_pm_runtime_put;

	if (tp->acp_enable) {
		tp->rx_desc_array = kzalloc_node(R8169_RX_RING_BYTES,
						 GFP_KERNEL, node);
		tp->rx_phy_addr = virt_to_phys(tp->rx_desc_array);
	} else {
		tp->rx_desc_array = dma_alloc_coherent(&pdev->dev,
						       R8169_RX_RING_BYTES,
						       &tp->rx_phy_addr,
						       GFP_KERNEL);
	}
	if (!tp->rx_desc_array)
		goto err_free_tx_0;

	retval = rtl8169_init_ring(dev);
	if (retval < 0)
		goto err_free_rx_1;

	INIT_WORK(&tp->wk.work, rtl_task);

	smp_mb(); /* make sure TX/RX rings are ready */

	retval = request_irq(dev->irq, rtl8169_interrupt,
			     (tp->features & RTL_FEATURE_MSI) ? 0 : IRQF_SHARED,
			     dev->name, dev);
	if (retval < 0)
		goto err_free_rx_2;

	rtl_lock_work(tp);

	set_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);

	napi_enable(&tp->napi);

	if (tp->status & RTL_STATUS_DOWN) {
		tp->status &= ~RTL_STATUS_DOWN;
		rtl8169_init_phy(dev, tp);
	}

	__rtl8169_set_features(dev, dev->features);

	rtl_pll_power_up(tp);

	rtl_hw_start(dev);

	netif_start_queue(dev);

	rtl_unlock_work(tp);

	tp->saved_wolopts = 0;

	rtl8169_check_link_status(dev, tp, ioaddr);
out:
	return retval;

err_free_rx_2:
	rtl8169_rx_clear(tp);
err_free_rx_1:
	if (tp->acp_enable)
		kfree(tp->rx_desc_array);
	else
		dma_free_coherent(&pdev->dev, R8169_RX_RING_BYTES,
				  tp->rx_desc_array, tp->rx_phy_addr);
	tp->rx_desc_array = NULL;
err_free_tx_0:
	if (tp->acp_enable)
		kfree(tp->tx_desc_array);
	else
		dma_free_coherent(&pdev->dev, R8169_TX_RING_BYTES,
				  tp->tx_desc_array, tp->tx_phy_addr);
	tp->tx_desc_array = NULL;
err_pm_runtime_put:
	goto out;
}

static void
rtl8169_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned int start;

	do {
		start = u64_stats_fetch_begin_irq(&tp->rx_stats.syncp);
		stats->rx_packets = tp->rx_stats.packets;
		stats->rx_bytes = tp->rx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&tp->rx_stats.syncp, start));

	do {
		start = u64_stats_fetch_begin_irq(&tp->tx_stats.syncp);
		stats->tx_packets = tp->tx_stats.packets;
		stats->tx_bytes = tp->tx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&tp->tx_stats.syncp, start));

	stats->rx_dropped	= dev->stats.rx_dropped;
	stats->tx_dropped	= dev->stats.tx_dropped;
	stats->rx_length_errors	= dev->stats.rx_length_errors;
	stats->rx_errors	= dev->stats.rx_errors;
	stats->rx_crc_errors	= dev->stats.rx_crc_errors;
	stats->rx_fifo_errors	= dev->stats.rx_fifo_errors;
	stats->rx_missed_errors	= dev->stats.rx_missed_errors;
}

static void rtl8169_protocol_offload(struct rtl8169_private *tp);
static void rtl8169_net_suspend(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (!netif_running(dev))
		return;

	netif_device_detach(dev);
	netif_stop_queue(dev);

	rtl_lock_work(tp);
	napi_disable(&tp->napi);
	clear_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);
	rtl_unlock_work(tp);

	/* disable EEE if it is enabled */
	if (tp->eee_enable)
		tp->chip->eee_set(tp, false);

	rtl8169_protocol_offload(tp);

	rtl_pll_power_down(tp);
}

static void rtl_hw_initialize(struct rtl8169_private *tp);
static void rtl_reinit_mac_phy(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	rtl_lock_work(tp);
	tp->chip->reset_phy_gmac(tp);
	tp->chip->acp_init(tp);
	tp->chip->pll_clock_init(tp);
	tp->chip->mdio_init(tp);
	/* after r8169soc_mdio_init(),
	 * SGMII : tp->ext_phy == true  ==> external MDIO,
	 * RGMII : tp->ext_phy == true  ==> external MDIO,
	 * RMII  : tp->ext_phy == false ==> internal MDIO,
	 * FE PHY: tp->ext_phy == false ==> internal MDIO
	 */

	/* Enable ALDPS */
	rtl_phy_write(tp, 0x0a43, 24,
		      rtl_phy_read(tp, 0x0a43, 24) | BIT(2));

	rtl_init_rxcfg(tp);

	rtl_irq_disable(tp);

	rtl_hw_initialize(tp);

	rtl_hw_reset(tp);

	rtl_ack_events(tp, 0xffff);

	RTL_W8(CFG9346, CFG9346_UNLOCK);
	RTL_W8(CONFIG1, RTL_R8(CONFIG1) | PM_ENABLE);
	RTL_W8(CONFIG5, RTL_R8(CONFIG5) & PME_STATUS);

	/* disable magic packet WOL */
	RTL_W8(CONFIG3, RTL_R8(CONFIG3) & ~MAGIC_PKT);
	RTL_W8(CFG9346, CFG9346_LOCK);

	rtl_unlock_work(tp);

	/* Set MAC address */
	rtl_rar_set(tp, tp->dev->dev_addr);

	rtl_led_set(tp);
}

static __maybe_unused void rtl_mac_reinit(struct rtl8169_private *tp)
{
	if (netif_running(tp->dev)) {
		tp->netif_is_running = true;
		pr_info(PFX "take %s down\n", tp->dev->name);
		rtl8169_close(tp->dev);
	} else {
		tp->netif_is_running = false;
	}

	rtl_reinit_mac_phy(tp);

	if (tp->netif_is_running) {
		pr_info(PFX "bring %s up\n", tp->dev->name);
		rtl_open(tp->dev);
	}

	rtl_lock_work(tp);
	/* enable EEE according to tp->eee_enable */
	tp->chip->eee_set(tp, tp->eee_enable);

	rtl8169_init_phy(tp->dev, tp);
	rtl_unlock_work(tp);
}

#ifdef CONFIG_PM

static int rtl8169_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rtl8169_private *tp = netdev_priv(ndev);

	pr_info(PFX "Enter %s\n", __func__);

	if (tp->pwr_saving) {
		if (netif_running(ndev)) {
			tp->netif_is_running = true;
			pr_info(PFX "take %s down\n", ndev->name);
			rtl8169_close(ndev);
		} else {
			tp->netif_is_running = false;
		}

		pr_info(PFX "disable ETN clk and reset\n");
		tp->chip->reset_phy_gmac(tp);
	} else {
		rtl8169_net_suspend(ndev);
	}

	/* turn off LED, and current solution is switch pad to GPIO input */
	if (tp->output_mode == OUTPUT_EMBEDDED_PHY)
		tp->chip->led_set(tp, false);

	pr_info(PFX "Exit %s\n", __func__);

	return 0;
}

static void __rtl8169_resume(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	netif_device_attach(dev);

	/* turn on LED */
	if (tp->output_mode == OUTPUT_EMBEDDED_PHY)
		tp->chip->led_set(tp, true);

	tp->chip->wakeup_set(tp, false);

	rtl_pll_power_up(tp);
	rtl_rar_set(tp, tp->dev->dev_addr);

	rtl_lock_work(tp);
	napi_enable(&tp->napi);
	set_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);
	rtl_unlock_work(tp);

	rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
}

static int rtl8169_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rtl8169_private *tp = netdev_priv(ndev);

	pr_info(PFX "Enter %s\n", __func__);

	if (tp->pwr_saving) {
		pr_info(PFX "enable ETN clk and reset\n");
		rtl_reinit_mac_phy(tp);

		if (tp->netif_is_running) {
			pr_info(PFX "bring %s up\n", ndev->name);
			rtl_open(ndev);
		}
	} else if (netif_running(ndev)) {
		__rtl8169_resume(ndev);
	}

	/* enable EEE according to tp->eee_enable */
	tp->chip->eee_set(tp, tp->eee_enable);

	rtl8169_init_phy(ndev, tp);

	pr_info(PFX "Exit %s\n", __func__);

	return 0;
}

static __maybe_unused int rtl8169_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rtl8169_private *tp = netdev_priv(ndev);

	if (!tp->tx_desc_array)
		return 0;

	rtl_lock_work(tp);
	tp->saved_wolopts = __rtl8169_get_wol(tp);
	__rtl8169_set_wol(tp, WAKE_ANY);
	rtl_unlock_work(tp);

	rtl8169_net_suspend(ndev);

	return 0;
}

static __maybe_unused int rtl8169_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rtl8169_private *tp = netdev_priv(ndev);

	if (!tp->tx_desc_array)
		return 0;

	rtl_lock_work(tp);
	__rtl8169_set_wol(tp, tp->saved_wolopts);
	tp->saved_wolopts = 0;
	rtl_unlock_work(tp);

	rtl8169_init_phy(ndev, tp);

	__rtl8169_resume(ndev);

	return 0;
}

static __maybe_unused int rtl8169_runtime_idle(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rtl8169_private *tp = netdev_priv(ndev);

	return tp->tx_desc_array ? -EBUSY : 0;
}

static const struct dev_pm_ops rtl8169_pm_ops = {
	.suspend		= rtl8169_suspend,
	.resume			= rtl8169_resume,
	.freeze			= rtl8169_suspend,
	.thaw			= rtl8169_resume,
	.poweroff		= rtl8169_suspend,
	.restore		= rtl8169_resume,
	.runtime_suspend	= rtl8169_runtime_suspend,
	.runtime_resume		= rtl8169_runtime_resume,
	.runtime_idle		= rtl8169_runtime_idle,
};

#define RTL8169_PM_OPS	(&rtl8169_pm_ops)

#else /* !CONFIG_PM */

#define RTL8169_PM_OPS	NULL

#endif /* !CONFIG_PM */

static void rtl_shutdown(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_net_suspend(dev);

#ifdef MY_ABC_HERE
	//  We customize MAC(s), avoid restoring original mac address when shutdown.
#else /* MY_ABC_HERE */
	/* Restore original MAC address */
	rtl_rar_set(tp, dev->perm_addr);
#endif /* MY_ABC_HERE */

	rtl8169_hw_reset(tp);

	if (system_state == SYSTEM_POWER_OFF) {
		if (__rtl8169_get_wol(tp) & WAKE_ANY)
			rtl_wol_suspend_quirk(tp);
	}
}

static int rtl_remove_one(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);
	int i;

#ifdef RTL_PROC
	do {
		if (!tp->dir_dev)
			break;

		remove_proc_entry("wol_enable", tp->dir_dev);
		remove_proc_entry("pwr_saving", tp->dir_dev);
		remove_proc_entry("mac_reinit", tp->dir_dev);
		remove_proc_entry("phy_reinit", tp->dir_dev);
		remove_proc_entry("eee", tp->dir_dev);
		remove_proc_entry("driver_var", tp->dir_dev);
		remove_proc_entry("eth_phy", tp->dir_dev);
		remove_proc_entry("ext_regs", tp->dir_dev);
		remove_proc_entry("registers", tp->dir_dev);
		remove_proc_entry("tx_desc", tp->dir_dev);
		remove_proc_entry("rx_desc", tp->dir_dev);
		remove_proc_entry("tally", tp->dir_dev);
		remove_proc_entry("wpd_event", tp->dir_dev);
		remove_proc_entry("wol_packet", tp->dir_dev);
		remove_proc_entry("wake_mask", tp->dir_dev);
		remove_proc_entry("wake_crc", tp->dir_dev);
		remove_proc_entry("wake_idx_en", tp->dir_dev);
		remove_proc_entry("wake_dump", tp->dir_dev);
		if (tp->chip->features & RTL_FEATURE_PAT_WAKE) {
			remove_proc_entry("wake_offset", tp->dir_dev);
			remove_proc_entry("wake_pattern", tp->dir_dev);
		}

		if (!rtw_proc)
			break;

		remove_proc_entry(MODULENAME, rtw_proc);
		remove_proc_entry("eth0", init_net.proc_net);

		rtw_proc = NULL;

	} while (0);
#endif

	cancel_work_sync(&tp->wk.work);

	netif_napi_del(&tp->napi);

	unregister_netdev(dev);

	/* restore original MAC address */
	rtl_rar_set(tp, dev->perm_addr);

	for (i = 0; i < tp->phy_irq_num; i++)
		free_irq(tp->phy_irq[i], tp);

	rtl8169_release_board(pdev, dev, tp->mmio_addr);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct net_device_ops rtl_netdev_ops = {
	.ndo_open		= rtl_open,
	.ndo_stop		= rtl8169_close,
	.ndo_get_stats64	= rtl8169_get_stats64,
	.ndo_start_xmit		= rtl8169_start_xmit,
	.ndo_tx_timeout		= rtl8169_tx_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= rtl8169_change_mtu,
	.ndo_fix_features	= rtl8169_fix_features,
	.ndo_set_features	= rtl8169_set_features,
	.ndo_set_mac_address	= rtl_set_mac_address,
	.ndo_do_ioctl		= rtl8169_ioctl,
	.ndo_set_rx_mode	= rtl_set_rx_mode,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= rtl8169_netpoll,
#endif

};

DECLARE_RTL_COND(rtl_link_list_ready_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R8(MCU) & LINK_LIST_RDY;
}

DECLARE_RTL_COND(rtl_rxtx_empty_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return (RTL_R8(MCU) & RXTX_EMPTY) == RXTX_EMPTY;
}

/* protocol offload driver flow */
static void rtl8169_protocol_offload(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	u32 tmp;

	/* Set Now_is_OOB = 0 */
	RTL_W8(MCU, RTL_R8(MCU) & ~NOW_IS_OOB);

	/* Flow control related (only for OOB) */
	/* RX FIFO threshold */
	rtl_ocp_write(tp, 0xC0A0, 0x0003);
	rtl_ocp_write(tp, 0xC0A2, 0x0180);
	rtl_ocp_write(tp, 0xC0A4, 0x004A);
	rtl_ocp_write(tp, 0xC0A8, 0x005A);

	/* Turn off RCR (IO offset 0x44 set to 0) */
	rtl_rx_close(tp);

	/* Set rxdv_gated_en (IO 0xF2 bit3 = 1) */
	RTL_W32(MISC, RTL_R32(MISC) | RXDV_GATED_EN);

	/* check FIFO empty (IO 0xD2 bit[12:13]) */
	if (!rtl_udelay_loop_wait_high(tp, &rtl_rxtx_empty_cond, 100, 42))
		return;

	/* disable Tx/Rx enable = 0 (IO 0x36 bit[10:11]=0b) */
	RTL_W8(CHIP_CMD, RTL_R8(CHIP_CMD) & ~(CMD_TX_ENB | CMD_RX_ENB));
	usleep_range(1000, 1100);

	/* check link_list ready =1 (IO 0xD2 bit9=1) */
	if (!rtl_udelay_loop_wait_high(tp, &rtl_link_list_ready_cond, 100, 42))
		return;

	/* set re_ini_ll =1 (MACOCP : 0xE8DE bit15=1) */
	tmp = rtl_ocp_read(tp, 0xE8DE);
	tmp |= BIT(15);
	rtl_ocp_write(tp, 0xE8DE, tmp);

	/* check link_list ready =1 (IO 0xD2 bit9=1) */
	if (!rtl_udelay_loop_wait_high(tp, &rtl_link_list_ready_cond, 100, 42))
		return;

	/* Setting RMS (IO offset 0xdb-0xda set to 0x05F3) */
	rtl_set_rx_max_size(ioaddr, 0x05F3);

	/* Enable VLAN De-tagging (IO offset 0xE0 bit6 set to 1) */
	RTL_W16(C_PLUS_CMD, RTL_R16(C_PLUS_CMD) | RX_VLAN);

	/* Enable now_is_oob (IO offset 0xd3 bit 7 set to 1b) */
	RTL_W8(MCU, RTL_R8(MCU) | NOW_IS_OOB);

	/*  set  MACOCP 0xE8DE bit14 mcu_borw_en to 1b for modifying ShareFIFO's points */
	tmp = rtl_ocp_read(tp, 0xE8DE);
	tmp |= BIT(14); /* borrow 8K SRAM */
	rtl_ocp_write(tp, 0xE8DE, tmp);

	/* Patch code to share FIFO if need */

	/* Set rxdv_gated_en = 0 (IO 0xF2 bit3=0) */
	RTL_W32(MISC, RTL_R32(MISC) & ~RXDV_GATED_EN);

	/* Turn on RCR (IO offset 0x44 set to 0x0e) */
	tmp = RTL_R32(RX_CONFIG) & ~RX_CONFIG_ACCEPT_MASK;
	tmp |= ACCEPT_BROADCAST | ACCEPT_MULTICAST | ACCEPT_MY_PHYS;
	RTL_W32(RX_CONFIG, tmp);

	/* Set Multicast Registers to accept all addresses */
	RTL_W32(MAR0, 0xFFFFFFFF);
	RTL_W32(MAR0 + 4, 0xFFFFFFFF);
}

static void rtl_hw_init_8168g(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	u32 data;

	tp->ocp_base = OCP_STD_PHY_BASE;

	RTL_W32(MISC, RTL_R32(MISC) | RXDV_GATED_EN);

	if (!rtl_udelay_loop_wait_high(tp, &rtl_txcfg_empty_cond, 100, 42))
		return;

	if (!rtl_udelay_loop_wait_high(tp, &rtl_rxtx_empty_cond, 100, 42))
		return;

	RTL_W8(CHIP_CMD, RTL_R8(CHIP_CMD) & ~(CMD_TX_ENB | CMD_RX_ENB));
	usleep_range(1000, 1100);
	RTL_W8(MCU, RTL_R8(MCU) & ~NOW_IS_OOB);

	data = rtl_ocp_read(tp, 0xe8de);
	data &= ~BIT(14);
	rtl_ocp_write(tp, 0xe8de, data);

	if (!rtl_udelay_loop_wait_high(tp, &rtl_link_list_ready_cond, 100, 42))
		return;

	data = rtl_ocp_read(tp, 0xe8de);
	data |= BIT(15);
	rtl_ocp_write(tp, 0xe8de, data);

	if (!rtl_udelay_loop_wait_high(tp, &rtl_link_list_ready_cond, 100, 42))
		return;
}

static void rtl_hw_initialize(struct rtl8169_private *tp)
{
	rtl_hw_init_8168g(tp);
}

#ifdef RTL_PROC
static int wol_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	pr_info("WoL setting:\n");
	pr_info("\tBIT 0:\t WoL enable\n");
	pr_info("\tBIT 1:\t CRC match\n");
	pr_info("\tBIT 2:\t WPD\n");
	pr_info("wol_enable = 0x%x\n", tp->wol_enable);
	seq_printf(m, "%d\n", tp->wol_enable);
	return 0;
}

static ssize_t wol_write_proc(struct file *file, const char __user *buffer,
			      size_t count, loff_t *pos)
{
	struct net_device *dev = (struct net_device *)
		((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	char tmp[32];
	u32 val = 0;
	u32 len = 0;
	int ret;

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';
		ret = kstrtou32(tmp, 0, &val);
		if (ret) {
			pr_err("invalid WoL setting [%s], ret = %d\n",
			       tmp, ret);
			return count;
		}
		tp->wol_enable = val;
		pr_info("set wol_enable = %x\n", tp->wol_enable);
	}
	return count;
}

static int wol_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, wol_read_proc, dev);
}

static const struct proc_ops wol_proc_fops = {
	.proc_open	= wol_proc_open,
	.proc_read	= seq_read,
	.proc_write	= wol_write_proc,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int pwr_saving_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	pr_info("Power saving of suspend mode:\n");
	pr_info("pwr_saving = 0x%x\n", tp->pwr_saving);
	seq_printf(m, "%d\n", tp->pwr_saving);
	return 0;
}

static ssize_t pwr_saving_write_proc(struct file *file, const char __user *buffer,
				     size_t count, loff_t *pos)
{
	struct net_device *dev = (struct net_device *)
		((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	char tmp[32];
	u32 val = 0;
	u32 len = 0;
	int ret;

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';
		ret = kstrtou32(tmp, 0, &val);
		if (ret) {
			pr_err("invalid power saving setting [%s], ret = %d\n",
			       tmp, ret);
			return count;
		}
		tp->pwr_saving = val;
		pr_info("set pwr_saving = %x\n", tp->pwr_saving);
	}
	return count;
}

static int pwr_saving_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, pwr_saving_read_proc, dev);
}

static const struct proc_ops pwr_saving_proc_fops = {
	.proc_open		= pwr_saving_proc_open,
	.proc_read		= seq_read,
	.proc_write		= pwr_saving_write_proc,
	.proc_lseek		= seq_lseek,
	.proc_release		= single_release,
};

static int mac_reinit_read_proc(struct seq_file *m, void *v)
{
	seq_puts(m, "\n\nUsage: echo 1 > mac_reinit\n");

	return 0;
}

static ssize_t mac_reinit_write_proc(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *pos)
{
	struct net_device *dev = (struct net_device *)
		((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	char tmp[32];
	u32 val = 0;
	u32 len = 0;
	int ret;

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';
		ret = kstrtou32(tmp, 0, &val);
		if (ret || val == 0) {
			pr_err("invalid mac_reinit [%s], ret = %d\n",
			       tmp, ret);
			return count;
		}
		pr_info("mac_reinit = %x\n", val);

		rtl_mac_reinit(tp);
	}
	return count;
}

static int mac_reinit_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, mac_reinit_read_proc, dev);
}

static const struct proc_ops mac_reinit_proc_fops = {
	.proc_open		= mac_reinit_proc_open,
	.proc_read		= seq_read,
	.proc_write		= mac_reinit_write_proc,
	.proc_lseek		= seq_lseek,
	.proc_release		= single_release,
};

static int phy_reinit_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	u32 isr;
	u32 por;

	regmap_read(tp->iso_base, ISO_UMSK_ISR, &isr);
	seq_printf(m, "ISO_UMSK_ISR\t[0x98007004] = %08x\n", isr);
	regmap_read(tp->iso_base, ISO_POR_DATAI, &por);
	seq_printf(m, "ISO_POR_DATAI\t[0x98007218] = %08x\n", por);

	seq_puts(m, "\n\nUsage: echo 1 > phy_reinit\n");

	return 0;
}

static ssize_t phy_reinit_write_proc(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *pos)
{
	struct net_device *dev = (struct net_device *)
		((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	char tmp[32];
	u32 val = 0;
	u32 len = 0;
	int ret;

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';
		ret = kstrtou32(tmp, 0, &val);
		if (ret || val == 0) {
			pr_err("invalid phy_reinit [%s], ret = %d\n",
			       tmp, ret);
			return count;
		}
		pr_info("phy_reinit = %x\n", val);

		if (!test_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags))
			rtl_phy_reinit(tp);
		else if (atomic_inc_return(&tp->phy_reinit_flag) == 1)
			rtl_schedule_task(tp, RTL_FLAG_TASK_PHY_PENDING);
		else
			atomic_dec(&tp->phy_reinit_flag);
	}
	return count;
}

static int phy_reinit_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, phy_reinit_read_proc, dev);
}

static const struct proc_ops phy_reinit_proc_fops = {
	.proc_open	= phy_reinit_proc_open,
	.proc_read	= seq_read,
	.proc_write	= phy_reinit_write_proc,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int eee_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned short e040, e080;

	rtl_lock_work(tp);
	e040 = rtl_ocp_read(tp, 0xe040);	/* EEE */
	e080 = rtl_ocp_read(tp, 0xe080);	/* EEE+ */
	seq_printf(m, "%s: eee = %d, OCP 0xe040 = 0x%x, OCP 0xe080 = 0x%x\n",
		   dev->name, tp->eee_enable, e040, e080);
	r8169_display_eee_info(dev, m, tp);
	rtl_unlock_work(tp);

	return 0;
}

static ssize_t eee_write_proc(struct file *file, const char __user *buffer,
			      size_t count, loff_t *pos)
{
	char tmp[32];
	u32 val = 0;
	u32 len = 0;
	int ret;
	bool chg = false;
	struct net_device *dev = (struct net_device *)
		((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';
		ret = kstrtou32(tmp, 0, &val);
		if (ret) {
			pr_err("invalid EEE setting [%s], ret = %d\n",
			       tmp, ret);
			return count;
		}
		pr_err("write %s eee = %x\n", dev->name, val);

		if (val > 0 && !tp->eee_enable) {
			tp->eee_enable = true;
			chg = true;
		} else if (val == 0 && tp->eee_enable) {
			tp->eee_enable = false;
			chg = true;
		}
	}

	if (!chg)
		goto done;

	rtl_lock_work(tp);
	/* power down PHY */
	rtl_phy_write(tp, 0, MII_BMCR,
		      rtl_phy_read(tp, 0, MII_BMCR) | BMCR_PDOWN);

	tp->chip->eee_set(tp, tp->eee_enable);
	mdelay(100);	/* wait PHY ready */
	rtl8169_phy_reset(dev, tp);
	rtl_unlock_work(tp);

done:
	return count;
}

/* seq_file wrappers for procfile show routines. */
static int eee_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, eee_read_proc, dev);
}

static const struct proc_ops eee_proc_fops = {
	.proc_open	= eee_proc_open,
	.proc_read	= seq_read,
	.proc_write	= eee_write_proc,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static void r8169soc_dump_regs(struct seq_file *m, struct rtl8169_private *tp)
{
	u32 addr;
	u32 val;
	u32 i;

	tp->chip->dump_regs(m, tp);

	seq_puts(m, "ETN MAC regs:\n");
	for (i = 0; i < 256; i += 4) {
		addr = 0x98016000 + i;
		val = readl(tp->mmio_addr + i);
		seq_printf(m, "[%08x] = %08x\n", addr, val);
	}
}

static int registers_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_lock_work(tp);
	r8169soc_dump_regs(m, tp);
	rtl_unlock_work(tp);

	return 0;
}

/* seq_file wrappers for procfile show routines. */
static int registers_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, registers_read_proc, dev);
}

static const struct proc_ops registers_proc_fops = {
	.proc_open	= registers_proc_open,
	.proc_read	= seq_read,
	.proc_write	= NULL,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static void r8169soc_dump_tx_desc(struct seq_file *m,
				  struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	u32 i;

	if (!tp->tx_desc_array) {
		seq_puts(m, "no tx_desc_array\n");
		return;
	}

	seq_printf(m, "SW TX INDEX: %d\n", tp->cur_tx % NUM_TX_DESC);
	seq_printf(m, "RECYCLED TX INDEX: %d\n", tp->dirty_tx % NUM_TX_DESC);
	if (tp->chip->features & RTL_FEATURE_TX_NO_CLOSE) {
		i = RTL_R16(TX_DESC_CLOSE_IDX) & TX_DESC_CNT_MASK;
		seq_printf(m, "HW TX INDEX: %d\n", i % NUM_TX_DESC);
	}
	seq_puts(m, "TX DESC:\n");
	for (i = 0; i < NUM_TX_DESC; i++)
		seq_printf(m, "Desc[%04d] opts1 0x%08x, opts2 0x%08x, addr 0x%llx\n",
			   i, tp->tx_desc_array[i].opts1,
			   tp->tx_desc_array[i].opts2,
			   tp->tx_desc_array[i].addr);
}

static int tx_desc_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_lock_work(tp);
	r8169soc_dump_tx_desc(m, tp);
	rtl_unlock_work(tp);

	return 0;
}

/* seq_file wrappers for procfile show routines. */
static int tx_desc_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, tx_desc_read_proc, dev);
}

static const struct proc_ops tx_desc_proc_fops = {
	.proc_open	= tx_desc_proc_open,
	.proc_read	= seq_read,
	.proc_write	= NULL,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static void r8169soc_dump_rx_desc(struct seq_file *m,
				  struct rtl8169_private *tp)
{
	u32 i;

	if (!tp->rx_desc_array) {
		seq_puts(m, "no rx_desc_array\n");
		return;
	}

	seq_printf(m, "SW RX INDEX: %d\n", tp->cur_rx % NUM_RX_DESC);
	#if defined(CONFIG_RTL_RX_NO_COPY)
	seq_printf(m, "REFILLED RX INDEX: %d\n", tp->dirty_rx % NUM_RX_DESC);
	#endif /* CONFIG_RTL_RX_NO_COPY */
	seq_puts(m, "RX DESC:\n");
	for (i = 0; i < NUM_RX_DESC; i++)
		seq_printf(m, "Desc[%04d] opts1 0x%08x, opts2 0x%08x, addr 0x%llx\n",
			   i, tp->rx_desc_array[i].opts1,
			   tp->rx_desc_array[i].opts2,
			   tp->rx_desc_array[i].addr);
}

static int rx_desc_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_lock_work(tp);
	r8169soc_dump_rx_desc(m, tp);
	rtl_unlock_work(tp);

	return 0;
}

/* seq_file wrappers for procfile show routines. */
static int rx_desc_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, rx_desc_read_proc, dev);
}

static const struct proc_ops rx_desc_proc_fops = {
	.proc_open	= rx_desc_proc_open,
	.proc_read	= seq_read,
	.proc_write	= NULL,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int driver_var_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	int i;

	seq_puts(m, "\nDump Driver Variable\n");

	rtl_lock_work(tp);
	seq_puts(m, "Variable\tValue\n----------\t-----\n");
	seq_printf(m, "MODULENAME\t%s\n", MODULENAME);
	seq_printf(m, "mac version\t%d\n", tp->chip->mac_version);
	seq_printf(m, "chipset_name\t%s\n", tp->chip->name);
	seq_printf(m, "driver version\t%s\n", RTL8169_VERSION);
	seq_printf(m, "txd version\t%d\n", tp->chip->txd_version);
	seq_printf(m, "chip features\t0x%x\n", tp->chip->features);
	seq_printf(m, "msg_enable\t0x%x\n", tp->msg_enable);
	seq_printf(m, "mtu\t\t%d\n", dev->mtu);
	seq_printf(m, "NUM_RX_DESC\t0x%x\n", NUM_RX_DESC);
	seq_printf(m, "cur_rx\t\t0x%x\n", tp->cur_rx);
#if defined(CONFIG_RTL_RX_NO_COPY)
	seq_printf(m, "dirty_rx\t0x%x\n", tp->dirty_rx);
#endif /* CONFIG_RTL_RX_NO_COPY */
	seq_printf(m, "NUM_TX_DESC\t0x%x\n", NUM_TX_DESC);
	seq_printf(m, "cur_tx\t\t0x%x\n", tp->cur_tx);
	seq_printf(m, "dirty_tx\t0x%x\n", tp->dirty_tx);
	seq_printf(m, "rx_buf_sz\t%d\n", rx_buf_sz);
	seq_printf(m, "cp_cmd\t\t0x%x\n", tp->cp_cmd);
	seq_printf(m, "event_slow\t0x%x\n", tp->event_slow);
	seq_printf(m, "wol_enable\t0x%x\n", tp->wol_enable);
	seq_printf(m, "pwr_saving\t0x%x\n", tp->pwr_saving);
	seq_printf(m, "saved_wolopts\t0x%x\n", tp->saved_wolopts);
	seq_printf(m, "opts1_mask\t0x%x\n", tp->opts1_mask);
	seq_printf(m, "wol_crc_cnt\t%d\n", tp->wol_crc_cnt);
	seq_printf(m, "led_cfg\t\t0x%x\n", tp->led_cfg);
	seq_printf(m, "cur_features\t0x%x\n", tp->features);
	seq_printf(m, "eee_enable\t%d\n", tp->eee_enable);
	seq_printf(m, "acp_enable\t%d\n", tp->acp_enable);
	seq_printf(m, "ext_phy\t\t%d\n", tp->ext_phy);
	seq_printf(m, "output_mode\t%d\n", tp->output_mode);
	tp->chip->dump_var(m, tp);
	seq_printf(m, "ETN IRQ\t\t%d\n", dev->irq);
	seq_printf(m, "phy_irq_num\t%d\n", tp->phy_irq_num);
	for (i = 0; i < tp->phy_irq_num; i++)
		seq_printf(m, "GPHY IRQ\t%d maps to ISR bit %d\n",
			   tp->phy_irq[i], tp->phy_irq_map[i]);
	seq_printf(m, "perm_addr\t%pM\n", dev->perm_addr);
	seq_printf(m, "dev_addr\t%pM\n", dev->dev_addr);
	rtl_unlock_work(tp);

	seq_putc(m, '\n');
	return 0;
}

static int driver_var_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, driver_var_read_proc, dev);
}

static const struct proc_ops driver_var_proc_fops = {
	.proc_open	= driver_var_proc_open,
	.proc_read	= seq_read,
	.proc_write	= NULL,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int tally_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	struct rtl8169_counters *counters = &tp->counters;

	rtl8169_update_counters(dev);

	seq_puts(m, "\nDump Tally Counter\n");
	seq_puts(m, "Statistics\t\tValue\n----------\t\t-----\n");
	seq_printf(m, "tx_packets\t\t%lld\n",
		   le64_to_cpu(counters->tx_packets));
	seq_printf(m, "rx_packets\t\t%lld\n",
		   le64_to_cpu(counters->rx_packets));
	seq_printf(m, "tx_errors\t\t%lld\n", le64_to_cpu(counters->tx_errors));
	seq_printf(m, "rx_errors\t\t%d\n", le32_to_cpu(counters->rx_errors));
	seq_printf(m, "rx_missed\t\t%d\n", le16_to_cpu(counters->rx_missed));
	seq_printf(m, "align_errors\t\t%d\n",
		   le16_to_cpu(counters->align_errors));
	seq_printf(m, "tx_one_collision\t%d\n",
		   le32_to_cpu(counters->tx_one_collision));
	seq_printf(m, "tx_multi_collision\t%d\n",
		   le32_to_cpu(counters->tx_multi_collision));
	seq_printf(m, "rx_unicast\t\t%lld\n",
		   le64_to_cpu(counters->rx_unicast));
	seq_printf(m, "rx_broadcast\t\t%lld\n",
		   le64_to_cpu(counters->rx_broadcast));
	seq_printf(m, "rx_multicast\t\t%d\n",
		   le32_to_cpu(counters->rx_multicast));
	seq_printf(m, "tx_aborted\t\t%d\n", le16_to_cpu(counters->tx_aborted));
	seq_printf(m, "tx_underrun\t\t%d\n",
		   le16_to_cpu(counters->tx_underrun));

	seq_putc(m, '\n');
	return 0;
}

static int tally_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, tally_read_proc, dev);
}

static const struct proc_ops tally_proc_fops = {
	.proc_open	= tally_proc_open,
	.proc_read	= seq_read,
	.proc_write	= NULL,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int eth_phy_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	int i, n, max = 16;
	u16 word_rd;
	struct rtl8169_private *tp = netdev_priv(dev);

	seq_puts(m, "\nDump Ethernet PHY\n");
	seq_puts(m, "\nOffset\tValue\n------\t-----\n ");

	rtl_lock_work(tp);
	seq_puts(m, "\n####################page 0##################\n ");
	for (n = 0; n < max;) {
		seq_printf(m, "\n0x%02x:\t", n);

		for (i = 0; i < 8 && n < max; i++, n++) {
			word_rd = rtl_phy_read(tp, 0, n);
			seq_printf(m, "%04x ", word_rd);
		}
	}
	rtl_unlock_work(tp);

	seq_putc(m, '\n');
	return 0;
}

static ssize_t eth_phy_write_proc(struct file *file, const char __user *buffer,
				  size_t count, loff_t *pos)
{
	char tmp[32];
	u32 page;
	u32 reg;
	u32 val;
	u32 len;
	int ret;
	char *p;
	char *t;
	char *delim = " ";
	struct net_device *dev = (struct net_device *)
			((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';

		p = tmp;
		t = strsep(&p, delim);
		if (!t)
			goto usage;
		ret = kstrtou32(t, 0, &page);
		if (ret) {
			pr_err("invalid page [%s], ret = %d\n", t, ret);
			goto usage;
		}

		t = strsep(&p, delim);
		if (!t)
			goto usage;
		ret = kstrtou32(t, 0, &reg);
		if (ret) {
			pr_err("invalid reg [%s], ret = %d\n", t, ret);
			goto usage;
		}

		t = strsep(&p, delim);
		if (!t) {
			/* read cmd */
			val = rtl_phy_read(tp, page, reg & 0x1f);
			pr_err("Read page 0x%x reg 0x%x, value = 0x%04x\n", page, reg & 0x1f, val);
			goto out;
		}
		ret = kstrtou32(t, 0, &val);
		if (ret) {
			pr_err("invalid value [%s], ret = %d\n", t, ret);
			goto usage;
		} else {
			/* write cmd */
			rtl_phy_write(tp, page, reg & 0x1f, val);
			pr_info("Write page 0x%x reg 0x%x value 0x%04x\n", page, reg & 0x1f, val);
			goto out;
		}
	}

usage:
	pr_err("USAGE: echo \"<page> <reg> [<value>]\" > eth_phy\n");
out:
	return count;
}

static int eth_phy_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, eth_phy_read_proc, dev);
}

static const struct proc_ops eth_phy_proc_fops = {
	.proc_open	= eth_phy_proc_open,
	.proc_read	= seq_read,
	.proc_write	= eth_phy_write_proc,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int ext_regs_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	int i, n, max = 256;
	u32 dword_rd;
	struct rtl8169_private *tp = netdev_priv(dev);

	seq_puts(m, "\nDump Extended Registers\n");
	seq_puts(m, "\nOffset\tValue\n------\t-----\n ");

	rtl_lock_work(tp);
	for (n = 0; n < max;) {
		seq_printf(m, "\n0x%02x:\t", n);

		for (i = 0; i < 4 && n < max; i++, n += 4) {
			dword_rd = rtl_eri_read(tp, n, ERIAR_EXGMAC);
			seq_printf(m, "%08x ", dword_rd);
		}
	}
	rtl_unlock_work(tp);

	seq_putc(m, '\n');
	return 0;
}

static int ext_regs_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, ext_regs_read_proc, dev);
}

static const struct proc_ops ext_regs_proc_fops = {
	.proc_open	= ext_regs_proc_open,
	.proc_read	= seq_read,
	.proc_write	= NULL,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int wpd_evt_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	u32 tmp;

	rtl_lock_work(tp);
	/* check if wol_own and wpd_en are both set */
	tmp = rtl_ocp_read(tp, 0xC0C2);
	if ((tmp & BIT(4)) == 0 || (tmp & BIT(0)) == 0) {
		seq_puts(m, "\nNo WPD event\n");
		rtl_unlock_work(tp);
		return 0;
	}

	seq_puts(m, "\nWPD event:\n");
	tmp = rtl_ocp_read(tp, 0xD23A) & 0x0F01;
	seq_printf(m, "Type (0: CRC match,  1: magic pkt) = %d\n",
		   tmp & 0x1);
	if ((tmp & 0x1) == 0)
		seq_printf(m, "CRC match ID = %d\n", tmp >> 8);
	seq_printf(m, "Original packet length = %d\n",
		   rtl_ocp_read(tp, 0xD23C));
	seq_printf(m, "Stored packet length = %d\n",
		   rtl_ocp_read(tp, 0xD23E));
	rtl_unlock_work(tp);

	seq_putc(m, '\n');
	return 0;
}

static int wpd_evt_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, wpd_evt_read_proc, dev);
}

static const struct proc_ops wpd_evt_proc_fops = {
	.proc_open	= wpd_evt_proc_open,
	.proc_read	= seq_read,
	.proc_write	= NULL,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int wol_pkt_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	int i;
	char wol_pkt[WOL_BUF_LEN];
	u32 len;
	u32 tmp;
	u16 *ptr;

	memset(wol_pkt, 0, WOL_BUF_LEN);

	rtl_lock_work(tp);
	/* check if wol_own and wpd_en are both set */
	tmp = rtl_ocp_read(tp, 0xC0C2);
	if ((tmp & BIT(4)) == 0 || (tmp & BIT(0)) == 0) {
		rtl_unlock_work(tp);
		return 0;
	}

	/* read 128-byte packet buffer */
	for (i = 0; i < 128; i += 2) {
		ptr = (u16 *)&wol_pkt[i];
		*ptr = rtl_ocp_read(tp, 0xD240 + i);
	}

	/* get stored packet length */
	len = rtl_ocp_read(tp, 0xD23E);
	for (i = 0; i < len; i++) {
		if ((i % 16) == 0)
			seq_puts(m, "\n");
		else if ((i % 8) == 0)
			seq_puts(m, "  ");
		seq_printf(m, "%02x ", wol_pkt[i]);
	}
	rtl_unlock_work(tp);

	seq_putc(m, '\n');
	return 0;
}

static int wol_pkt_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, wol_pkt_read_proc, dev);
}

static const struct proc_ops wol_pkt_proc_fops = {
	.proc_open	= wol_pkt_proc_open,
	.proc_read	= seq_read,
	.proc_write	= NULL,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static void rtl_proc_hex_dump(struct seq_file *m, char *ptr, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if ((i % 16) == 0)
			seq_putc(m, '\n');
		else if ((i % 8) == 0)
			seq_puts(m, "- ");

		seq_printf(m, "%02X ", ptr[i]);
	}
}

static int rtl_str2hex(char *src, char *dst)
{
	int i = 0;
	char *p;
	char *t;
	char *delim = " ";
	int ret;

	p = src;
	while ((t = strsep(&p, delim)) != NULL) {
		ret = kstrtou8(t, 16, &dst[i]);
		if (ret) {
			pr_err("%s:%d: invalid token[%s] to hex, ret 0x%x\n",
			       __func__, __LINE__,
			       t, ret);
			return i;
		}
		i++;
	}

	return i;
}

static int wake_mask_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	seq_puts(m, "\nMASK = [");
	rtl_proc_hex_dump(m, tp->wol_rule_buf.mask, tp->wol_rule_buf.mask_size);
	seq_puts(m, "\n]\n");

	return 0;
}

static ssize_t wake_mask_write_proc(struct file *file,
				    const char __user *buffer,
				    size_t count, loff_t *pos)
{
	char tmp[512];
	u32 len;
	struct net_device *dev = (struct net_device *)
			((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';

		if (strlen(tmp) >= (RTL_WAKE_MASK_SIZE * 3)) {
			pr_err("input length should be smaller than %d\n",
			       RTL_WAKE_MASK_SIZE * 3);
			goto out;
		}
		memset(tp->wol_rule_buf.mask, 0, RTL_WAKE_MASK_SIZE);

		tp->wol_rule_buf.mask_size =
			rtl_str2hex(tmp, tp->wol_rule_buf.mask);
	}

out:
	return count;
}

static int wake_mask_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, wake_mask_read_proc, dev);
}

static const struct proc_ops wake_mask_proc_fops = {
	.proc_open           = wake_mask_proc_open,
	.proc_read           = seq_read,
	.proc_write          = wake_mask_write_proc,
	.proc_lseek          = seq_lseek,
	.proc_release        = single_release,
};

static int wake_crc_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	seq_printf(m, "CRC16 = [0x%04X]\n", tp->wol_rule_buf.crc);

	return 0;
}

static ssize_t wake_crc_write_proc(struct file *file, const char __user *buffer,
				   size_t count, loff_t *pos)
{
	char tmp[80];
	u32 len;
	struct net_device *dev = (struct net_device *)
			((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret;

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';
		ret = kstrtou16(tmp, 16, &tp->wol_rule_buf.crc);
		if (ret) {
			pr_err("%s:%d: invalid token[%s] to hex, ret 0x%x\n",
			       __func__, __LINE__,
			       tmp, ret);
			return count;
		}
	}

	return count;
}

static int wake_crc_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, wake_crc_read_proc, dev);
}

static const struct proc_ops wake_crc_proc_fops = {
	.proc_open           = wake_crc_proc_open,
	.proc_read           = seq_read,
	.proc_write          = wake_crc_write_proc,
	.proc_lseek          = seq_lseek,
	.proc_release        = single_release,
};

static int wake_offset_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	seq_printf(m, "OFFSET = [0x%04X]\n", tp->wol_rule_buf.offset);

	return 0;
}

static ssize_t wake_offset_write_proc(struct file *file,
				      const char __user *buffer,
				      size_t count, loff_t *pos)
{
	char tmp[80];
	u32 len;
	struct net_device *dev = (struct net_device *)
			((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret;

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';
		ret = kstrtou16(tmp, 0, &tp->wol_rule_buf.offset);
		if (ret) {
			pr_err("%s:%d: invalid token[%s] to hex, ret 0x%x\n",
			       __func__, __LINE__,
			       tmp, ret);
			return count;
		}
	}

	return count;
}

static int wake_offset_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, wake_offset_read_proc, dev);
}

static const struct proc_ops wake_offset_proc_fops = {
	.proc_open           = wake_offset_proc_open,
	.proc_read           = seq_read,
	.proc_write          = wake_offset_write_proc,
	.proc_lseek          = seq_lseek,
	.proc_release        = single_release,
};

static int wake_pattern_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	seq_puts(m, "\nPATTERN = [");
	rtl_proc_hex_dump(m, tp->wol_rule_buf.pattern,
			  tp->wol_rule_buf.pattern_size);
	seq_puts(m, "\n]\n");

	return 0;
}

static ssize_t wake_pattern_write_proc(struct file *file,
				       const char __user *buffer,
				       size_t count, loff_t *pos)
{
	char tmp[512];
	u32 len;
	struct net_device *dev = (struct net_device *)
			((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';
		if (strlen(tmp) >= (RTL_WAKE_PATTERN_SIZE * 3)) {
			pr_err("input length should be smaller than %d\n",
			       RTL_WAKE_PATTERN_SIZE * 3);
			goto out;
		}
		memset(tp->wol_rule_buf.pattern, 0, RTL_WAKE_PATTERN_SIZE);

		tp->wol_rule_buf.pattern_size =
			rtl_str2hex(tmp, tp->wol_rule_buf.pattern);
	}

out:
	return count;
}

static int wake_pattern_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, wake_pattern_read_proc, dev);
}

static const struct proc_ops wake_pattern_proc_fops = {
	.proc_open           = wake_pattern_proc_open,
	.proc_read           = seq_read,
	.proc_write          = wake_pattern_write_proc,
	.proc_lseek          = seq_lseek,
	.proc_release        = single_release,
};

static int wake_idx_en_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	int wake_size;
	int i;

	seq_puts(m, "USAGE:\necho \"[index] [enable]\" > wake_idx_en\n");
	seq_puts(m, "\tindex\t0 ~ 31\n");
	seq_puts(m, "\tenable\t0 ~ 1\n");
	seq_puts(m, "\t\tenable = 0 ==> remove rule[index]\n");
	if (tp->chip->features & RTL_FEATURE_PAT_WAKE) {
		seq_puts(m, "\t\tenable = 1 ==> add {wake_mask, wake_crc, ");
		seq_puts(m, "wake_offset, wake_pattern} to rule[index]\n");
	} else {
		seq_puts(m, "\t\tenable = 1 ==> add {wake_mask, wake_crc} to ");
		seq_puts(m, "rule[index]\n");
	}

	if (tp->chip->features & RTL_FEATURE_PAT_WAKE)
		wake_size = RTL_WAKE_SIZE;
	else
		wake_size = RTL_WAKE_SIZE_CRC;

	seq_printf(m, "## Total rules = %d\n", tp->wol_crc_cnt);
	for (i = 0; i < wake_size; i++)
		seq_printf(m, "rule[%d] = %s\n", i,
			   tp->wol_rule[i].flag & WAKE_FLAG_ENABLE ? "enable" : "disable");
	return 0;
}

static ssize_t wake_idx_en_write_proc(struct file *file,
				      const char __user *buffer,
				      size_t count, loff_t *pos)
{
	char tmp[80];
	u32 len;
	struct net_device *dev = (struct net_device *)
			((struct seq_file *)file->private_data)->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	char *p;
	char *t;
	char *delim = " ";
	int ret;
	u8 idx;
	u8 en;

	len = min(count, sizeof(tmp) - 1);
	if (buffer && !copy_from_user(tmp, buffer, len)) {
		if (len)
			tmp[len - 1] = '\0';

		p = tmp;
		t = strsep(&p, delim);
		if (!t)
			goto usage;
		ret = kstrtou8(t, 0, &idx);
		if (ret) {
			pr_err("invalid idx token[%s], ret 0x%x\n", t, ret);
			goto usage;
		}

		if (idx > 31) {
			pr_err("invalid idx [%d] (idx <= 31)\n", idx);
			goto usage;
		}

		t = strsep(&p, delim);
		if (!t)
			goto usage;
		ret = kstrtou8(t, 0, &en);
		if (ret) {
			pr_err("invalid en token[%s], ret 0x%x\n", t, ret);
			goto usage;
		}

		if (en > 1) {
			pr_err("invalid en [%d] (en = 0 or 1)\n", idx);
			goto usage;
		}

		if (en == 1) {
			if (!(tp->wol_rule[idx].flag & WAKE_FLAG_ENABLE))
				tp->wol_crc_cnt++;
			/* add/replace a rule */
			tp->wol_rule_buf.flag = WAKE_FLAG_ENABLE;
			memcpy(&tp->wol_rule[idx], &tp->wol_rule_buf,
			       sizeof(struct rtl_wake_rule_s));
		} else {
			if (tp->wol_rule[idx].flag & WAKE_FLAG_ENABLE)
				tp->wol_crc_cnt--;
			/* del a rule */
			tp->wol_rule_buf.flag &= ~WAKE_FLAG_ENABLE;
			tp->wol_rule[idx].flag &= ~WAKE_FLAG_ENABLE;
		}
		goto out;
	}

usage:
	pr_info("USAGE:\necho \"[index] [enable]\" > wake_idx_en\n");
	pr_info("\tindex\t0 ~ 31\n");
	pr_info("\tenable\t0 ~ 1\n");
	pr_info("\t\tenable = 0 ==> remove rule[index]\n");
	if (tp->chip->features & RTL_FEATURE_PAT_WAKE) {
		pr_info("\t\tenable = 1 ==> add {wake_mask, wake_crc, ");
		pr_info("wake_offset, wake_pattern} to rule[index]\n");
	} else {
		pr_info("\t\tenable = 1 ==> add {wake_mask, wake_crc} to ");
		pr_info("rule[index]\n");
	}

out:
	return count;
}

static int wake_idx_en_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, wake_idx_en_read_proc, dev);
}

static const struct proc_ops wake_idx_en_proc_fops = {
	.proc_open           = wake_idx_en_proc_open,
	.proc_read           = seq_read,
	.proc_write          = wake_idx_en_write_proc,
	.proc_lseek         = seq_lseek,
	.proc_release        = single_release,
};

static int wake_dump_read_proc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct rtl8169_private *tp = netdev_priv(dev);
	int i;

	seq_puts(m, "\nWOL rules dump:\n");
	for (i = 0; i < RTL_WAKE_SIZE; i++) {
		if (!(tp->wol_rule[i].flag & WAKE_FLAG_ENABLE))
			continue;

		seq_printf(m, "##### index %d #####\n", i);

		seq_puts(m, "\nMASK = [");
		rtl_proc_hex_dump(m, tp->wol_rule[i].mask,
				  tp->wol_rule[i].mask_size);
		seq_puts(m, "\n]\n");

		seq_printf(m, "CRC16  = 0x%04X\n", tp->wol_rule[i].crc);
		if (tp->chip->features & RTL_FEATURE_PAT_WAKE) {
			seq_printf(m, "OFFSET = 0x%04X\n", tp->wol_rule[i].offset);
			seq_puts(m, "\nPATTERN = [");
			rtl_proc_hex_dump(m, tp->wol_rule[i].pattern,
					  tp->wol_rule[i].pattern_size);
			seq_puts(m, "\n]\n");
		}
	}

	seq_putc(m, '\n');
	return 0;
}

static int wake_dump_proc_open(struct inode *inode, struct file *file)
{
	struct net_device *dev = proc_get_parent_data(inode);

	return single_open(file, wake_dump_read_proc, dev);
}

static const struct proc_ops wake_dump_proc_fops = {
	.proc_open           = wake_dump_proc_open,
	.proc_read           = seq_read,
	.proc_write          = NULL,
	.proc_lseek          = seq_lseek,
	.proc_release        = single_release,
};
#endif

static __must_check
void *r8169soc_read_otp(struct rtl8169_private *tp, const char *name)
{
	struct device *dev = &tp->pdev->dev;
	struct device_node *np = dev->of_node;
	struct nvmem_cell *cell;
	unsigned char *buf;
	size_t buf_size;

	cell = of_nvmem_cell_get(np, name);
	if (IS_ERR(cell)) {
		dev_err(dev, "failed to get nvmem cell %s: %ld\n",
			name, PTR_ERR(cell));
		return ERR_CAST(cell);
	}

	buf = nvmem_cell_read(cell, &buf_size);
	if (IS_ERR(buf))
		dev_err(dev, "failed to read nvmem cell %s: %ld\n",
			name, PTR_ERR(buf));
	nvmem_cell_put(cell);
	return buf;
}

/* dummy functions */
static void dummy_mdio_lock(struct rtl8169_private *tp)
{
	/* no lock */
}

static void dummy_mdio_unlock(struct rtl8169_private *tp)
{
	/* no unlock */
}

static void dummy_wakeup_set(struct rtl8169_private *tp, bool enable)
{
	/* no wake up */
}

static void dummy_reset_phy_gmac(struct rtl8169_private *tp)
{
	pr_info(PFX "%s is called\n", __func__);
}

static void dummy_pll_clock_init(struct rtl8169_private *tp)
{
	pr_info(PFX "%s is called\n", __func__);
}

static void dummy_acp_init(struct rtl8169_private *tp)
{
	/* no acp */
}

static void dummy_mdio_init(struct rtl8169_private *tp)
{
	/* no MDIO init */
}

static void dummy_mac_mcu_patch(struct rtl8169_private *tp)
{
	/* no patch */
}

static void dummy_hw_phy_config(struct rtl8169_private *tp)
{
	/* no config */
}

static void dummy_eee_set(struct rtl8169_private *tp, bool enable)
{
	/* no EEE */
}

static void dummy_led_set(struct rtl8169_private *tp, bool enable)
{
	/* no LED */
}

static void dummy_dump_regs(struct seq_file *m, struct rtl8169_private *tp)
{
	/* no special registers */
}

static void dummy_dump_var(struct seq_file *m, struct rtl8169_private *tp)
{
	/* no special variables */
}

/* RTD119X */
static void rtd119x_reset_phy_gmac(struct rtl8169_private *tp)
{
	struct clk *clk_etn  = clk_get(&tp->pdev->dev, "etn");
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_etn =
		reset_control_get_exclusive(&tp->pdev->dev, "rst_etn");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	/* pre-init clk */
	clk_prepare_enable(clk_etn);
	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);

	/* disable clk and hold reset bits */
	clk_disable_unprepare(clk_etn_sys);
	clk_disable_unprepare(clk_etn_250m);
	clk_disable_unprepare(clk_etn);
	reset_control_assert(rstc_gphy);
	reset_control_assert(rstc_gmac);
	reset_control_assert(rstc_etn);

	/* release resource */
	reset_control_put(rstc_etn);
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd119x_pll_clock_init(struct rtl8169_private *tp)
{
	struct clk *clk_etn  = clk_get(&tp->pdev->dev, "etn");
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_etn =
		reset_control_get_exclusive(&tp->pdev->dev, "rst_etn");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	/* enable clk bits and release reset bits */
	clk_prepare_enable(clk_etn);
	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);
	reset_control_deassert(rstc_etn);
	reset_control_deassert(rstc_gphy);
	reset_control_deassert(rstc_gmac);

	mdelay(10);		/* wait 10ms for GMAC uC to be stable */

	/* release resource */
	reset_control_put(rstc_etn);
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd119x_mdio_init(struct rtl8169_private *tp)
{
}

static void rtd119x_mac_mcu_patch(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	const struct soc_device_attribute rtk_soc_rtd119x_a00[] = {
		{
			.family = "Realtek Phoenix",
			.revision = "A00",
		},
		{
		/* empty */
		}
	};

	if (soc_device_match(rtk_soc_rtd119x_a00))
		return;

	/* Fix ALDPS */
	RTL_W32(OCPDR, 0xFE140000);
	RTL_W32(OCPDR, 0xFE150000);
	RTL_W32(OCPDR, 0xFE160000);
	RTL_W32(OCPDR, 0xFE170000);
	RTL_W32(OCPDR, 0xFE180000);
	RTL_W32(OCPDR, 0xFE190000);
	RTL_W32(OCPDR, 0xFE1A0000);
	RTL_W32(OCPDR, 0xFE1B0000);
	mdelay(3);
	RTL_W32(OCPDR, 0xFE130000);

	RTL_W32(OCPDR, 0xFC00E008);
	RTL_W32(OCPDR, 0xFC01E051);
	RTL_W32(OCPDR, 0xFC02E059);
	RTL_W32(OCPDR, 0xFC03E05B);
	RTL_W32(OCPDR, 0xFC04E05D);
	RTL_W32(OCPDR, 0xFC05E05F);
	RTL_W32(OCPDR, 0xFC06E061);
	RTL_W32(OCPDR, 0xFC07E063);
	RTL_W32(OCPDR, 0xFC08C422);
	RTL_W32(OCPDR, 0xFC097380);
	RTL_W32(OCPDR, 0xFC0A49B7);
	RTL_W32(OCPDR, 0xFC0BF003);
	RTL_W32(OCPDR, 0xFC0C1D02);
	RTL_W32(OCPDR, 0xFC0D8D80);
	RTL_W32(OCPDR, 0xFC0EC51D);
	RTL_W32(OCPDR, 0xFC0F73A0);
	RTL_W32(OCPDR, 0xFC101300);
	RTL_W32(OCPDR, 0xFC11F104);
	RTL_W32(OCPDR, 0xFC1273A2);
	RTL_W32(OCPDR, 0xFC131300);
	RTL_W32(OCPDR, 0xFC14F010);
	RTL_W32(OCPDR, 0xFC15C517);
	RTL_W32(OCPDR, 0xFC1676A0);
	RTL_W32(OCPDR, 0xFC1774A2);
	RTL_W32(OCPDR, 0xFC180601);
	RTL_W32(OCPDR, 0xFC193720);
	RTL_W32(OCPDR, 0xFC1A9EA0);
	RTL_W32(OCPDR, 0xFC1B9CA2);
	RTL_W32(OCPDR, 0xFC1CC50F);
	RTL_W32(OCPDR, 0xFC1D73A2);
	RTL_W32(OCPDR, 0xFC1E4023);
	RTL_W32(OCPDR, 0xFC1FF813);
	RTL_W32(OCPDR, 0xFC20F304);
	RTL_W32(OCPDR, 0xFC2173A0);
	RTL_W32(OCPDR, 0xFC224033);
	RTL_W32(OCPDR, 0xFC23F80F);
	RTL_W32(OCPDR, 0xFC24C206);
	RTL_W32(OCPDR, 0xFC257340);
	RTL_W32(OCPDR, 0xFC2649B7);
	RTL_W32(OCPDR, 0xFC27F013);
	RTL_W32(OCPDR, 0xFC28C207);
	RTL_W32(OCPDR, 0xFC29BA00);
	RTL_W32(OCPDR, 0xFC2AC0BC);
	RTL_W32(OCPDR, 0xFC2BD2C8);
	RTL_W32(OCPDR, 0xFC2CD2CC);
	RTL_W32(OCPDR, 0xFC2DC0C4);
	RTL_W32(OCPDR, 0xFC2ED2E4);
	RTL_W32(OCPDR, 0xFC2F100A);
	RTL_W32(OCPDR, 0xFC30104C);
	RTL_W32(OCPDR, 0xFC310C7E);
	RTL_W32(OCPDR, 0xFC321D02);
	RTL_W32(OCPDR, 0xFC33C6F7);
	RTL_W32(OCPDR, 0xFC348DC0);
	RTL_W32(OCPDR, 0xFC351C01);
	RTL_W32(OCPDR, 0xFC36C5F7);
	RTL_W32(OCPDR, 0xFC378CA1);
	RTL_W32(OCPDR, 0xFC38C6F8);
	RTL_W32(OCPDR, 0xFC39BE00);
	RTL_W32(OCPDR, 0xFC3AC5F4);
	RTL_W32(OCPDR, 0xFC3B74A0);
	RTL_W32(OCPDR, 0xFC3C49C0);
	RTL_W32(OCPDR, 0xFC3DF010);
	RTL_W32(OCPDR, 0xFC3E74A2);
	RTL_W32(OCPDR, 0xFC3F76A4);
	RTL_W32(OCPDR, 0xFC404034);
	RTL_W32(OCPDR, 0xFC41F804);
	RTL_W32(OCPDR, 0xFC420601);
	RTL_W32(OCPDR, 0xFC439EA4);
	RTL_W32(OCPDR, 0xFC44E009);
	RTL_W32(OCPDR, 0xFC451D02);
	RTL_W32(OCPDR, 0xFC46C4E4);
	RTL_W32(OCPDR, 0xFC478D80);
	RTL_W32(OCPDR, 0xFC48C5E5);
	RTL_W32(OCPDR, 0xFC4964A1);
	RTL_W32(OCPDR, 0xFC4A4845);
	RTL_W32(OCPDR, 0xFC4B8CA1);
	RTL_W32(OCPDR, 0xFC4CE7EC);
	RTL_W32(OCPDR, 0xFC4D1C20);
	RTL_W32(OCPDR, 0xFC4EC5DC);
	RTL_W32(OCPDR, 0xFC4F8CA1);
	RTL_W32(OCPDR, 0xFC50C2E1);
	RTL_W32(OCPDR, 0xFC51BA00);
	RTL_W32(OCPDR, 0xFC521D02);
	RTL_W32(OCPDR, 0xFC53C606);
	RTL_W32(OCPDR, 0xFC548DC0);
	RTL_W32(OCPDR, 0xFC551D20);
	RTL_W32(OCPDR, 0xFC568DC0);
	RTL_W32(OCPDR, 0xFC57C603);
	RTL_W32(OCPDR, 0xFC58BE00);
	RTL_W32(OCPDR, 0xFC59C0BC);
	RTL_W32(OCPDR, 0xFC5A0E22);
	RTL_W32(OCPDR, 0xFC5BC102);
	RTL_W32(OCPDR, 0xFC5CB900);
	RTL_W32(OCPDR, 0xFC5D02A2);
	RTL_W32(OCPDR, 0xFC5EC602);
	RTL_W32(OCPDR, 0xFC5FBE00);
	RTL_W32(OCPDR, 0xFC600000);
	RTL_W32(OCPDR, 0xFC61C602);
	RTL_W32(OCPDR, 0xFC62BE00);
	RTL_W32(OCPDR, 0xFC630000);
	RTL_W32(OCPDR, 0xFC64C602);
	RTL_W32(OCPDR, 0xFC65BE00);
	RTL_W32(OCPDR, 0xFC660000);
	RTL_W32(OCPDR, 0xFC67C602);
	RTL_W32(OCPDR, 0xFC68BE00);
	RTL_W32(OCPDR, 0xFC690000);
	RTL_W32(OCPDR, 0xFC6AC602);
	RTL_W32(OCPDR, 0xFC6BBE00);
	RTL_W32(OCPDR, 0xFC6C0000);

	RTL_W32(OCPDR, 0xFE138000);
	RTL_W32(OCPDR, 0xFE140FD1);
	RTL_W32(OCPDR, 0xFE150D21);
	RTL_W32(OCPDR, 0xFE16029D);

	/* MDC/MDIO clock speedup */
	RTL_W32(OCPDR, 0xEf080040);
}

static void rtd119x_hw_phy_config(struct rtl8169_private *tp)
{
	int revision = REVISION_NONE;
	const struct soc_device_attribute *soc;
	const struct soc_device_attribute rtk_soc_rtd119x[] = {
		{
			.family = "Realtek Phoenix",
			.revision = "A00",
			.data = (void *)REVISION_A00,
		},
		{
			.family = "Realtek Phoenix",
			.revision = "A01",
			.data = (void *)REVISION_A01,
		},
		{
		/* empty */
		}
	};

	soc = soc_device_match(rtk_soc_rtd119x);
	if (soc)
		revision = (uintptr_t)soc->data;

	switch (revision) {
	case REVISION_A00:
		/* disable green */
		rtl_phy_write(tp, 0x0a43, 0x1b, 0x8011);
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x1737);

		/* write abiq /ldvbias */
		rtl_phy_write(tp, 0x0bcc, 0x11, 0x4444);

		/* R/RC auto k */
		/* default ff08  disable auto k RC/R */
		rtl_phy_write(tp, 0x0a43, 0x1b, 0x8013);
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x0F08);

		/* force tapbin = 4 */
		rtl_phy_write(tp, 0x0bce, 0x10, 0x4444);

		rtl_phy_write(tp, 0x0bcd, 0x17, 0x8888);		/* tx rc */
		rtl_phy_write(tp, 0x0bcd, 0x16, 0x9999);		/* rx rc */

		/* increase sd thd */
		/* master sd  : 0x1e00 */
		rtl_phy_write(tp, 0x0a43, 0x1b, 0x8101);
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x3E00);

		/* default sd  : 0x0e00 */
		rtl_phy_write(tp, 0x0a43, 0x1b, 0x80E2);
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x3E00);

		/* slave sd  : 0x0e00 */
		rtl_phy_write(tp, 0x0a43, 0x1b, 0x8120);
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x3E00);
		break;
	case REVISION_A01:
		/* GDAC updown */
		rtl_phy_write(tp, 0x0bcc, 0x12, 0x00BE);

		/* R_RC */
		/* fbfe [15:8] tapbin tx -3, [7:0] tapbin rx -2 */
		rtl_phy_write(tp, 0x0a43, 0x1b, 0x81DE);
		rtl_phy_write(tp, 0x0a43, 0x1c, 0xFDFE);

		rtl_phy_write(tp, 0x0a43, 0x1b, 0x81E0);
		rtl_phy_write(tp, 0x0a43, 0x1c, 0xFDFF);		/* [15:8] rlen  -3 */

		rtl_phy_write(tp, 0x0a43, 0x1b, 0x81E2);
		/* [15:8] rlen_100 +4, -3+4=1 */
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x0400);
		rtl_phy_write(tp, 0x0a43, 0x1b, 0x80d3);
		/* fnet cable length constant 0aa4 */
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x04a4);

		rtl_phy_write(tp, 0x0a43, 0x1b, 0x8111);
		/* slave cable length constant fa7f */
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x0a7f);

		rtl_phy_write(tp, 0x0a43, 0x1b, 0x810d);
		/* slave const dagc 0606 */
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x5604);

		rtl_phy_write(tp, 0x0a43, 0x1b, 0x80f4);
		/* master cable length delta 3df7 */
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x5df7);

		rtl_phy_write(tp, 0x0a43, 0x1b, 0x80f2);
		/* master cable length constant fa8f */
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x0a8f);

		rtl_phy_write(tp, 0x0a43, 0x1b, 0x80f6);
		/* master delta dagc 63ca */
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x54ca);

		rtl_phy_write(tp, 0x0a43, 0x1b, 0x80ec);
		/* master aagc 146c */
		rtl_phy_write(tp, 0x0a43, 0x1c, 0x007c);

		/* disable ALDPS interrupt */
		rtl_phy_write(tp, 0x0a42, 0x12,
			      rtl_phy_read(tp, 0x0a42, 0x12) & ~BIT(9));

		/* enable ALDPS */
		rtl_phy_write(tp, 0x0a43, 0x10,
			      rtl_phy_read(tp, 0x0a43, 0x10) | BIT(2));
	}
}

static void rtd119x_eee_set(struct rtl8169_private *tp, bool enable)
{
}

static void rtd119x_led_set(struct rtl8169_private *tp, bool enable)
{
	struct reset_control *rstc_etn =
		reset_control_get_exclusive(&tp->pdev->dev, "rst_etn");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	if (enable) {
		/* hold reset bits */
		reset_control_assert(rstc_gphy);
		reset_control_assert(rstc_gmac);
		reset_control_assert(rstc_etn);

		/* release reset bits */
		reset_control_deassert(rstc_etn);
		reset_control_deassert(rstc_gphy);
		reset_control_deassert(rstc_gmac);

		msleep(300);

		regmap_update_bits_base(tp->iso_base, RTD119X_ISO_MUXPAD0,
					GENMASK(31, 28), (5 << 28), NULL, false, true);
	} else {
		regmap_update_bits_base(tp->iso_base, RTD119X_ISO_MUXPAD0,
					GENMASK(31, 28), 0, NULL, false, true);
	}

	/* release resource */
	reset_control_put(rstc_etn);
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd119x_dump_regs(struct seq_file *m, struct rtl8169_private *tp)
{
}

static void rtd119x_dump_var(struct seq_file *m, struct rtl8169_private *tp)
{
}

/******************* END of RTD119X ****************************/

/* RTD129X */
static void rtd129x_mdio_lock(struct rtl8169_private *tp)
{
	/* disable interrupt from PHY to MCU */
	rtl_ocp_write(tp, 0xFC1E,
		      rtl_ocp_read(tp, 0xFC1E) & ~(BIT(1) | BIT(11) | BIT(12)));
}

static void rtd129x_mdio_unlock(struct rtl8169_private *tp)
{
	u32 tmp;

	/* enable interrupt from PHY to MCU */
	tmp = rtl_ocp_read(tp, 0xFC1E);
	if (tp->output_mode == OUTPUT_RGMII_TO_MAC)
		tmp |= (BIT(11) | BIT(12)); /* ignore BIT(1):mac_intr*/
	else
		tmp |= (BIT(1) | BIT(11) | BIT(12));
	rtl_ocp_write(tp, 0xFC1E, tmp);
}

static void rtd129x_reset_phy_gmac(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	if (unlikely(!__clk_is_enabled(clk_etn_sys) || !__clk_is_enabled(clk_etn_250m))) {
		/* pre-init clk bits */
		clk_prepare_enable(clk_etn_sys);
		clk_prepare_enable(clk_etn_250m);

		/* disable clk bits and hold reset bits */
		clk_disable_unprepare(clk_etn_sys);
		clk_disable_unprepare(clk_etn_250m);
		reset_control_assert(rstc_gphy);
		reset_control_assert(rstc_gmac);
	}

	/* release resource */
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd129x_pll_clock_init(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	if (likely(__clk_is_enabled(clk_etn_sys) && __clk_is_enabled(clk_etn_250m))) {
		/* enable again to prevent clk framework from disabling them */
		clk_prepare_enable(clk_etn_sys);
		clk_prepare_enable(clk_etn_250m);
	} else {
		/* 1. reg_0x98007088[10] = 1 */
		/* ISO spec, reset bit of gphy */
		reset_control_deassert(rstc_gphy);

		/* 2. CPU software waiting 200uS */
		usleep_range(200, 210);

		/* 3. reg_0x98007060[1] = 0 */
		/* ISO spec, Ethernet Boot up bypass gphy ready mode */
		regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
					BIT(1), 0, NULL, false, true);

		/* 4. reg_0x98007fc0[0] = 0 */
		/* ISO spec, Ethernet Boot up disable dbus clock gating */
		regmap_update_bits_base(tp->iso_base, RTD129X_ISO_DBUS_CTRL,
					BIT(0), 0, NULL, false, true);

		/* 5. CPU software waiting 200uS */
		usleep_range(200, 210);

		/* 6. reg_0x9800708c[12:11] = 11 */
		/* ISO spec, clock enable bit for etn clock & etn 250MHz */
		clk_prepare_enable(clk_etn_sys);
		clk_prepare_enable(clk_etn_250m);

		/* 7. reg_0x9800708c[12:11] = 00 */
		/* ISO spec, clock enable bit for etn clock & etn 250MHz */
		clk_disable_unprepare(clk_etn_sys);
		clk_disable_unprepare(clk_etn_250m);

		/* 8. reg_0x98007088[9] = 1 */
		/* ISO spec, reset bit of gmac */
		reset_control_deassert(rstc_gmac);

		/* 9. reg_0x9800708c[12:11] = 11 */
		/* ISO spec, clock enable bit for etn clock & etn 250MHz */
		clk_prepare_enable(clk_etn_sys);
		clk_prepare_enable(clk_etn_250m);

		msleep(100);
	}

	/* release resource */
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd129x_mac_setup(struct rtl8169_private *tp)
{
	unsigned int tmp;

	if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
		tmp = rtl_ocp_read(tp, 0xea34) &
			~(BIT(0) | BIT(1));
		tmp |= BIT(1); /* MII */
		rtl_ocp_write(tp, 0xea34, tmp);
	} else { /* RGMII */
		if (tp->output_mode == OUTPUT_RGMII_TO_PHY) {
			/* # ETN spec, MDC freq=2.5MHz */
			tmp = rtl_ocp_read(tp, 0xde30);
			rtl_ocp_write(tp, 0xde30, tmp & ~(BIT(6) | BIT(7)));
			/* # ETN spec, set external PHY addr */
			tmp = rtl_ocp_read(tp, 0xde24) & ~(0x1F);
			rtl_ocp_write(tp, 0xde24,
				      tmp | (tp->ext_phy_id & 0x1F));
		}

		tmp = rtl_ocp_read(tp, 0xea34) & ~(BIT(0) | BIT(1));
		tmp |= BIT(0); /* RGMII */

		if (tp->output_mode == OUTPUT_RGMII_TO_MAC) {
			tmp &= ~(BIT(3) | BIT(4));
			tmp |= BIT(4); /* speed: 1G */
			tmp |= BIT(2); /* full duplex */
		}
		if (tp->rgmii.rx_delay == RTD129X_RGMII_DELAY_0NS)
			tmp &= ~BIT(6);
		else
			tmp |= BIT(6);

		if (tp->rgmii.tx_delay == RTD129X_RGMII_DELAY_0NS)
			tmp &= ~BIT(7);
		else
			tmp |= BIT(7);

		rtl_ocp_write(tp, 0xea34, tmp);

		/* adjust RGMII voltage */
		switch (tp->rgmii.voltage) {
		case RTD129X_VOLTAGE_1_DOT_8V:
			regmap_write(tp->sb2_base, RTD129X_SB2_PFUNC_RG0, 0);
			regmap_write(tp->sb2_base, RTD129X_SB2_PFUNC_RG1, 0x44444444);
			regmap_write(tp->sb2_base, RTD129X_SB2_PFUNC_RG2, 0x24444444);
			break;
		case RTD129X_VOLTAGE_2_DOT_5V:
			regmap_write(tp->sb2_base, RTD129X_SB2_PFUNC_RG0, 0);
			regmap_write(tp->sb2_base, RTD129X_SB2_PFUNC_RG1, 0x44444444);
			regmap_write(tp->sb2_base, RTD129X_SB2_PFUNC_RG2, 0x64444444);
			break;
		case RTD129X_VOLTAGE_3_DOT_3V:
			regmap_write(tp->sb2_base, RTD129X_SB2_PFUNC_RG0, 0x3F);
			regmap_write(tp->sb2_base, RTD129X_SB2_PFUNC_RG1, 0);
			regmap_write(tp->sb2_base, RTD129X_SB2_PFUNC_RG2, 0xA4000000);
		}

		/* switch RGMII/MDIO to GMAC */
		regmap_update_bits_base(tp->iso_base, RTD129X_ISO_RGMII_MDIO_TO_GMAC,
					BIT(1), BIT(1), NULL, false, true);

		if (tp->output_mode == OUTPUT_RGMII_TO_MAC) {
			/* force GMAC link up */
			rtl_ocp_write(tp, 0xde40, 0x30EC);
			/* ignore mac_intr from PHY */
			tmp = rtl_ocp_read(tp, 0xfc1e) & ~BIT(1);
			rtl_ocp_write(tp, 0xfc1e, tmp);
		}
	}
}

static void rtd129x_mac_mcu_patch(struct rtl8169_private *tp)
{
	const struct soc_device_attribute rtk_soc_rtd129x_bxx[] = {
		{
			.family = "Realtek Kylin",
			.revision = "B0*",
		},
		{
		/* empty */
		}
	};

	/* disable break point */
	rtl_ocp_write(tp, 0xfc28, 0);
	rtl_ocp_write(tp, 0xfc2a, 0);
	rtl_ocp_write(tp, 0xfc2c, 0);
	rtl_ocp_write(tp, 0xfc2e, 0);
	rtl_ocp_write(tp, 0xfc30, 0);
	rtl_ocp_write(tp, 0xfc32, 0);
	rtl_ocp_write(tp, 0xfc34, 0);
	rtl_ocp_write(tp, 0xfc36, 0);
	mdelay(3);

	/* disable base address */
	rtl_ocp_write(tp, 0xfc26, 0);

	/* patch code */
	rtl_ocp_write(tp, 0xf800, 0xE008);
	rtl_ocp_write(tp, 0xf802, 0xE012);
	rtl_ocp_write(tp, 0xf804, 0xE044);
	rtl_ocp_write(tp, 0xf806, 0xE046);
	rtl_ocp_write(tp, 0xf808, 0xE048);
	rtl_ocp_write(tp, 0xf80a, 0xE04A);
	rtl_ocp_write(tp, 0xf80c, 0xE04C);
	rtl_ocp_write(tp, 0xf80e, 0xE04E);
	rtl_ocp_write(tp, 0xf810, 0x44E3);
	rtl_ocp_write(tp, 0xf812, 0xC708);
	rtl_ocp_write(tp, 0xf814, 0x75E0);
	rtl_ocp_write(tp, 0xf816, 0x485D);
	rtl_ocp_write(tp, 0xf818, 0x9DE0);
	rtl_ocp_write(tp, 0xf81a, 0xC705);
	rtl_ocp_write(tp, 0xf81c, 0xC502);
	rtl_ocp_write(tp, 0xf81e, 0xBD00);
	rtl_ocp_write(tp, 0xf820, 0x01EE);
	rtl_ocp_write(tp, 0xf822, 0xE85A);
	rtl_ocp_write(tp, 0xf824, 0xE000);
	rtl_ocp_write(tp, 0xf826, 0xC72D);
	rtl_ocp_write(tp, 0xf828, 0x76E0);
	rtl_ocp_write(tp, 0xf82a, 0x49ED);
	rtl_ocp_write(tp, 0xf82c, 0xF026);
	rtl_ocp_write(tp, 0xf82e, 0xC02A);
	rtl_ocp_write(tp, 0xf830, 0x7400);
	rtl_ocp_write(tp, 0xf832, 0xC526);
	rtl_ocp_write(tp, 0xf834, 0xC228);
	rtl_ocp_write(tp, 0xf836, 0x9AA0);
	rtl_ocp_write(tp, 0xf838, 0x73A2);
	rtl_ocp_write(tp, 0xf83a, 0x49BE);
	rtl_ocp_write(tp, 0xf83c, 0xF11E);
	rtl_ocp_write(tp, 0xf83e, 0xC324);
	rtl_ocp_write(tp, 0xf840, 0x9BA2);
	rtl_ocp_write(tp, 0xf842, 0x73A2);
	rtl_ocp_write(tp, 0xf844, 0x49BE);
	rtl_ocp_write(tp, 0xf846, 0xF0FE);
	rtl_ocp_write(tp, 0xf848, 0x73A2);
	rtl_ocp_write(tp, 0xf84a, 0x49BE);
	rtl_ocp_write(tp, 0xf84c, 0xF1FE);
	rtl_ocp_write(tp, 0xf84e, 0x1A02);
	rtl_ocp_write(tp, 0xf850, 0x49C9);
	rtl_ocp_write(tp, 0xf852, 0xF003);
	rtl_ocp_write(tp, 0xf854, 0x4821);
	rtl_ocp_write(tp, 0xf856, 0xE002);
	rtl_ocp_write(tp, 0xf858, 0x48A1);
	rtl_ocp_write(tp, 0xf85a, 0x73A2);
	rtl_ocp_write(tp, 0xf85c, 0x49BE);
	rtl_ocp_write(tp, 0xf85e, 0xF10D);
	rtl_ocp_write(tp, 0xf860, 0xC313);
	rtl_ocp_write(tp, 0xf862, 0x9AA0);
	rtl_ocp_write(tp, 0xf864, 0xC312);
	rtl_ocp_write(tp, 0xf866, 0x9BA2);
	rtl_ocp_write(tp, 0xf868, 0x73A2);
	rtl_ocp_write(tp, 0xf86a, 0x49BE);
	rtl_ocp_write(tp, 0xf86c, 0xF0FE);
	rtl_ocp_write(tp, 0xf86e, 0x73A2);
	rtl_ocp_write(tp, 0xf870, 0x49BE);
	rtl_ocp_write(tp, 0xf872, 0xF1FE);
	rtl_ocp_write(tp, 0xf874, 0x48ED);
	rtl_ocp_write(tp, 0xf876, 0x9EE0);
	rtl_ocp_write(tp, 0xf878, 0xC602);
	rtl_ocp_write(tp, 0xf87a, 0xBE00);
	rtl_ocp_write(tp, 0xf87c, 0x0532);
	rtl_ocp_write(tp, 0xf87e, 0xDE00);
	rtl_ocp_write(tp, 0xf880, 0xE85A);
	rtl_ocp_write(tp, 0xf882, 0xE086);
	rtl_ocp_write(tp, 0xf884, 0x0A44);
	rtl_ocp_write(tp, 0xf886, 0x801F);
	rtl_ocp_write(tp, 0xf888, 0x8015);
	rtl_ocp_write(tp, 0xf88a, 0x0015);
	rtl_ocp_write(tp, 0xf88c, 0xC602);
	rtl_ocp_write(tp, 0xf88e, 0xBE00);
	rtl_ocp_write(tp, 0xf890, 0x0000);
	rtl_ocp_write(tp, 0xf892, 0xC602);
	rtl_ocp_write(tp, 0xf894, 0xBE00);
	rtl_ocp_write(tp, 0xf896, 0x0000);
	rtl_ocp_write(tp, 0xf898, 0xC602);
	rtl_ocp_write(tp, 0xf89a, 0xBE00);
	rtl_ocp_write(tp, 0xf89c, 0x0000);
	rtl_ocp_write(tp, 0xf89e, 0xC602);
	rtl_ocp_write(tp, 0xf8a0, 0xBE00);
	rtl_ocp_write(tp, 0xf8a2, 0x0000);
	rtl_ocp_write(tp, 0xf8a4, 0xC602);
	rtl_ocp_write(tp, 0xf8a6, 0xBE00);
	rtl_ocp_write(tp, 0xf8a8, 0x0000);
	rtl_ocp_write(tp, 0xf8aa, 0xC602);
	rtl_ocp_write(tp, 0xf8ac, 0xBE00);
	rtl_ocp_write(tp, 0xf8ae, 0x0000);

	/* enable base address */
	rtl_ocp_write(tp, 0xfc26, 0x8000);

	/* enable breakpoint */
	rtl_ocp_write(tp, 0xfc28, 0x01ED);
	rtl_ocp_write(tp, 0xfc2a, 0x0531);

	if (tp->eee_enable) {
		/* EEE MAC mode */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) | (BIT(1) | BIT(0)));
		/* EEE+ MAC mode */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) | BIT(1));
		/* enable EEE of 10Mbps */
		rtl_patchphy(tp, 0x0a43, 25, BIT(4));
	}

	if (soc_device_match(rtk_soc_rtd129x_bxx))
		rtd129x_mac_setup(tp);
}

static void rtd129x_hw_phy_config(struct rtl8169_private *tp)
{
	/* enable ALDPS mode */
	rtl_w1w0_phy(tp, 0x0a43, 24, BIT(2), BIT(12) | BIT(1) | BIT(0));
}

static void rtd129x_eee_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		rtl_mmd_write(tp, 0x7, 0x3c, 0x6);

		/* turn on EEE */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) | BIT(1) | BIT(0));
		/* turn on EEE+ */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) | BIT(1));
	} else {
		rtl_mmd_write(tp, 0x7, 0x3c, 0);

		/* turn off EEE */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) & ~(BIT(1) | BIT(0)));
		/* turn off EEE+ */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) & ~BIT(1));
	}
}

static void rtd129x_led_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		regmap_update_bits_base(tp->iso_base, RTD129X_ISO_MUXPAD0,
					GENMASK(29, 26), (5 << 26), NULL, false, true);
	} else {
		regmap_update_bits_base(tp->iso_base, RTD129X_ISO_MUXPAD0,
					GENMASK(29, 26), 0, NULL, false, true);
	}
}

static void rtd129x_dump_regs(struct seq_file *m, struct rtl8169_private *tp)
{
	u32 val;

	regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	seq_printf(m, "ISO_UMSK_ISR\t[0x98007004] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_PWRCUT_ETN, &val);
	seq_printf(m, "ISO_PWRCUT_ETN\t[0x9800705c] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_ETN_TESTIO, &val);
	seq_printf(m, "ISO_ETN_TESTIO\t[0x98007060] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_SOFT_RESET, &val);
	seq_printf(m, "ETN_RESET_CTRL\t[0x98007088] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_CLOCK_ENABLE, &val);
	seq_printf(m, "ETN_CLK_CTRL\t[0x9800708c] = %08x\n", val);
}

static void rtd129x_dump_var(struct seq_file *m, struct rtl8169_private *tp)
{
	seq_printf(m, "rgmii_voltage\t%d\n", tp->rgmii.voltage);
	seq_printf(m, "rgmii_tx_delay\t%d\n", tp->rgmii.tx_delay);
	seq_printf(m, "rgmii_rx_delay\t%d\n", tp->rgmii.rx_delay);
}

/******************* END of RTD129X ****************************/

/* RTD139X */
#define MDIO_WAIT_TIMEOUT	100
static void rtd139x_mdio_lock(struct rtl8169_private *tp)
{
	u32 wait_cnt = 0;
	u32 log_de4e = 0;

	/* disable EEE IMR */
	rtl_ocp_write(tp, 0xE044,
		      rtl_ocp_read(tp, 0xE044) &
		      ~(BIT(3) | BIT(2) | BIT(1) | BIT(0)));
	/* disable timer 2 */
	rtl_ocp_write(tp, 0xE404,
		      rtl_ocp_read(tp, 0xE404) | BIT(9));
	/* wait MDIO channel is free */
	log_de4e = BIT(0) & rtl_ocp_read(tp, 0xDE4E);
	log_de4e = (log_de4e << 1) |
		(BIT(0) & rtl_ocp_read(tp, 0xDE4E));
	/* check if 0 for continuous 2 times */
	while (0 != (((0x1 << 2) - 1) & log_de4e)) {
		wait_cnt++;
		udelay(1);
		log_de4e = (log_de4e << 1) | (BIT(0) &
			rtl_ocp_read(tp, 0xDE4E));
		if (wait_cnt > MDIO_WAIT_TIMEOUT)
			break;
	}
	/* enter driver mode */
	rtl_ocp_write(tp, 0xDE42, rtl_ocp_read(tp, 0xDE42) | BIT(0));
	if (wait_cnt > MDIO_WAIT_TIMEOUT)
		pr_err(PFX "%s:%d: MDIO lock failed\n", __func__, __LINE__);
}

static void rtd139x_mdio_unlock(struct rtl8169_private *tp)
{
	/* exit driver mode */
	rtl_ocp_write(tp, 0xDE42, rtl_ocp_read(tp, 0xDE42) & ~BIT(0));
	/* enable timer 2 */
	rtl_ocp_write(tp, 0xE404,
		      rtl_ocp_read(tp, 0xE404) & ~BIT(9));
	/* enable EEE IMR */
	rtl_ocp_write(tp, 0xE044,
		      rtl_ocp_read(tp, 0xE044) | BIT(3) | BIT(2) | BIT(1) |
				   BIT(0));
}

static void rtd139x_reset_phy_gmac(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");
	struct clk *clk_en_sds = NULL;
	struct reset_control *rstc_sds_reg = NULL;
	struct reset_control *rstc_sds = NULL;
	struct reset_control *rstc_pcie0_power = NULL;
	struct reset_control *rstc_pcie0_phy = NULL;
	struct reset_control *rstc_pcie0_sgmii_mdio = NULL;
	struct reset_control *rstc_pcie0_phy_mdio = NULL;

	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);

	/* reg_0x9800708c[12:11] = 00 */
	/* ISO spec, clock enable bit for etn clock & etn 250MHz */
	clk_disable_unprepare(clk_etn_sys);
	clk_disable_unprepare(clk_etn_250m);

	/* reg_0x98007088[10:9] = 00 */
	/* ISO spec, rstn_gphy & rstn_gmac */
	reset_control_assert(rstc_gphy);
	reset_control_assert(rstc_gmac);

	/* reg_0x98007060[1] = 1 */
	/* ISO spec, bypass mode enable */
	regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
				BIT(1), BIT(1), NULL, false, true);

	/* reg_0x9800705c = 0x00616703 */
	/* ISO spec, default value */
	regmap_write(tp->iso_base, ISO_PWRCUT_ETN, 0x00616703);

	/* RESET for SGMII if needed */
	if (tp->output_mode != OUTPUT_EMBEDDED_PHY) {
		clk_en_sds = clk_get(&tp->pdev->dev, "sds");
		rstc_sds_reg = reset_control_get_exclusive(&tp->pdev->dev,
							   "sds_reg");
		rstc_sds = reset_control_get_exclusive(&tp->pdev->dev, "sds");
		rstc_pcie0_power =
			reset_control_get_exclusive(&tp->pdev->dev,
						    "pcie0_power");
		rstc_pcie0_phy =
			reset_control_get_exclusive(&tp->pdev->dev,
						    "pcie0_phy");
		rstc_pcie0_sgmii_mdio =
			reset_control_get_exclusive(&tp->pdev->dev,
						    "pcie0_sgmii_mdio");
		rstc_pcie0_phy_mdio =
			reset_control_get_exclusive(&tp->pdev->dev,
						    "pcie0_phy_mdio");

		clk_prepare_enable(clk_en_sds);

		/* reg_0x9800000c[7] = 0 */
		/* CRT spec, clk_en_sds */
		clk_disable_unprepare(clk_en_sds);

		/* reg_0x98000000[4:3] = 00 */
		/* CRT spec, rstn_sds_reg & rstn_sds */
		reset_control_assert(rstc_sds);
		reset_control_assert(rstc_sds_reg);

		/* reg_0x98000004[7] = 0 */
		/* reg_0x98000004[14] = 0 */
		/* CRT spec, rstn_pcie0_power & rstn_pcie0_phy */
		reset_control_assert(rstc_pcie0_power);
		reset_control_assert(rstc_pcie0_phy);

		/* reg_0x98000050[13] = 0 */
		/* reg_0x98000050[16] = 0 */
		/* CRT spec, rstn_pcie0_sgmii_mdio & rstn_pcie0_phy_mdio */
		reset_control_assert(rstc_pcie0_sgmii_mdio);
		reset_control_assert(rstc_pcie0_phy_mdio);

		reset_control_put(rstc_sds_reg);
		reset_control_put(rstc_sds);
		reset_control_put(rstc_pcie0_power);
		reset_control_put(rstc_pcie0_phy);
		reset_control_put(rstc_pcie0_sgmii_mdio);
		reset_control_put(rstc_pcie0_phy_mdio);
	}

	mdelay(1);

	/* release resource */
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd139x_acp_init(struct rtl8169_private *tp)
{
	u32 tmp;
	u32 val;

	/* SBX spec, Select ETN access DDR path. */
	if (tp->acp_enable) {
		/* reg_0x9801c20c[6] = 1 */
		/* SBX spec, Mask ETN_ALL to SB3 DBUS REQ */
		regmap_update_bits_base(tp->sbx_base, RTD139X_SBX_SB3_CHANNEL_REQ_MASK,
					BIT(6), BIT(6), NULL, false, true);

		pr_info(PFX "wait all SB3 access finished...");
		tmp = 0;
		regmap_read(tp->sbx_base, RTD139X_SBX_SB3_CHANNEL_REQ_BUSY, &val);
		while ((val & BIT(6)) != 0) {
			mdelay(1);
			tmp += 1;
			if (tmp >= 100) {
				pr_err("\n wait SB3 access failed (wait %d ms)\n",
				       tmp);
				break;
			}
			regmap_read(tp->sbx_base, RTD139X_SBX_SB3_CHANNEL_REQ_BUSY, &val);
		}
		if (tmp < 100)
			pr_info("done.\n");

		/* reg_0x9801d100[29] = 0 */
		/* SCPU wrapper spec, CLKACP division, 0 = div 2, 1 = div 3 */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD139X_SC_WRAP_CRT_CTRL,
					BIT(29), 0, NULL, false, true);

		/* reg_0x9801d124[1:0] = 00 */
		/* SCPU wrapper spec, ACP master active, 0 = active */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD139X_SC_WRAP_INTERFACE_EN,
					GENMASK(1, 0), 0, NULL, false, true);

		/* reg_0x9801d100[30] = 1 */
		/* SCPU wrapper spec, dy_icg_en_acp */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD139X_SC_WRAP_CRT_CTRL,
					BIT(30), BIT(30), NULL, false, true);

		/* reg_0x9801d100[21] = 1 */
		/* SCPU wrapper spec, ACP CLK enable */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD139X_SC_WRAP_CRT_CTRL,
					BIT(21), BIT(21), NULL, false, true);

		/* reg_0x9801d100[14] = 1 */
		/* Do not apply reset to ACP port axi3 master */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD139X_SC_WRAP_CRT_CTRL,
					BIT(14), BIT(14), NULL, false, true);

		/* reg_0x9801d800[3:0] = 0111 */
		/* reg_0x9801d800[7:4] = 0111 */
		/* reg_0x9801d800[9] = 1 */
		/* reg_0x9801d800[20:16] = 01100 */
		/* reg_0x9801d800[28:24] = 01110 */
		/* Configure control of ACP port */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD139X_SC_WRAP_ACP_CTRL,
					GENMASK(28, 24) | GENMASK(20, 16) |
					BIT(9) | GENMASK(7, 4) | GENMASK(3, 0),
					(0x0e << 24) | (0x0c << 16) | BIT(9) |
					(0x7 << 4) | (0x7 << 0),
					NULL, false, true);

		/* reg_0x9801d030[28] = 1 */
		/* SCPU wrapper spec, dy_icg_en_acp */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD139X_SC_WRAP_ACP_CRT_CTRL,
					BIT(28), BIT(28), NULL, false, true);

		/* reg_0x9801d030[16] = 1 */
		/* SCPU wrapper spec, ACP CLK Enable for acp of scpu_chip_top */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD139X_SC_WRAP_ACP_CRT_CTRL,
					BIT(16), BIT(16), NULL, false, true);

		/* reg_0x9801d030[0] = 1 */
		/* Do not apply reset to ACP port axi3 master */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD139X_SC_WRAP_ACP_CRT_CTRL,
					BIT(0), BIT(0), NULL, false, true);

		/* reg_0x9801c814[17] = 1 */
		/* through ACP to SCPU_ACP */
		regmap_update_bits_base(tp->sbx_base, RTD139X_SBX_ACP_MISC_CTRL,
					BIT(17), BIT(17), NULL, false, true);

		pr_info(PFX "ARM ACP on\n.");
	}
}

static void rtd139x_pll_clock_init(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	/* reg_0x98007004[27] = 1 */
	/* ISO spec, ETN_PHY_INTR, clear ETN interrupt for ByPassMode */
	regmap_write(tp->iso_base, ISO_UMSK_ISR, BIT(27));

	/* reg_0x98007088[10] = 1 */
	/* ISO spec, reset bit of gphy */
	reset_control_deassert(rstc_gphy);

	mdelay(1);	/* wait 1ms for PHY PLL stable */

	/* In Hercules, EPHY need choose the bypass mode or Non-bypass mode */
	/* Bypass mode : ETN MAC bypass efuse update flow.
	 * SW need to take this sequence.
	 */
	/* Non-Bypass mode : ETN MAC set efuse update and efuse_rdy setting */
	/* Default : Bypass mode (0x9800_7060[1] = 1'b1) */
	if (!tp->bypass_enable) {
		/* reg_0x98007060[1] = 0 */
		/* ISO spec, bypass mode disable */
		regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
					BIT(1), 0, NULL, false, true);
	} else {
		/* reg_0x98007060[1] = 1 */
		/* ISO spec, bypass mode enable */
		/* bypass mode, SW need to handle the EPHY Status check ,
		 * EFUSE data update and EPHY fuse_rdy setting.
		 */
		regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
					BIT(1), BIT(1), NULL, false, true);
	}

	/* reg_0x98007088[9] = 1 */
	/* ISO spec, reset bit of gmac */
	reset_control_deassert(rstc_gmac);

	/* reg_0x9800708c[12:11] = 11 */
	/* ISO spec, clock enable bit for etn clock & etn 250MHz */
	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);

	mdelay(10);	/* wait 10ms for GMAC uC to be stable */

	/* release resource */
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

/* Hercules only uses 13 bits in OTP 0x9801_72C8[12:8] and 0x9801_72D8[7:0]
 * if 0x9801_72C8[12] is 1, then
 * 0x9801_72C8[11:8] (R-calibration) is used to set PHY
 * 0x9801_72D8[7:0] is used to set idac_fine for PHY
 */
static void rtd139x_load_otp_content(struct rtl8169_private *tp)
{
	u32 otp;
	u16 tmp;
	u8 *buf;

	buf = r8169soc_read_otp(tp, "para");
	if (IS_ERR(buf))
		goto set_idac;

	otp = *buf;
	/* OTP[4] = valid flag, OTP[3:0] = content */
	if (0 != ((0x1 << 4) & otp)) {
		/* frc_r_value_default = 0x8 */
		tmp = otp ^ RTD139X_R_K_DEFAULT;
		rtl_phy_write(tp, 0x0bc0, 20,
			      tmp | (rtl_phy_read(tp, 0x0bc0, 20) & ~(0x1f << 0)));
	}

	kfree(buf);

set_idac:

	buf = r8169soc_read_otp(tp, "idac");
	if (IS_ERR(buf))
		return;

	otp = *buf;
	tmp = otp ^ RTD139X_IDAC_FINE_DEFAULT;	/* IDAC_FINE_DEFAULT = 0x33 */
	rtl_phy_write(tp, 0x0bc0, 23,
		      tmp | (rtl_phy_read(tp, 0x0bc0, 23) & ~(0xff << 0)));

	kfree(buf);
}

static u32 rtd139x_serdes_init(struct rtl8169_private *tp)
{
	u32 stable_ticks;
	u32 tmp;
	u32 val;
	struct clk *clk_en_sds = clk_get(&tp->pdev->dev, "sds");
	struct reset_control *rstc_sds_reg =
		reset_control_get_exclusive(&tp->pdev->dev, "sds_reg");
	struct reset_control *rstc_sds =
		reset_control_get_exclusive(&tp->pdev->dev, "sds");
	struct reset_control *rstc_pcie0_power =
		reset_control_get_exclusive(&tp->pdev->dev, "pcie0_power");
	struct reset_control *rstc_pcie0_phy =
		reset_control_get_exclusive(&tp->pdev->dev, "pcie0_phy");
	struct reset_control *rstc_pcie0_sgmii_mdio =
		reset_control_get_exclusive(&tp->pdev->dev, "pcie0_sgmii_mdio");
	struct reset_control *rstc_pcie0_phy_mdio =
		reset_control_get_exclusive(&tp->pdev->dev, "pcie0_phy_mdio");

	/* reg_0x98000000[4:3] = 11 */
	/* CRT spec, rstn_sds_reg & rstn_sds */
	reset_control_deassert(rstc_sds);
	reset_control_deassert(rstc_sds_reg);

	/* reg_0x98000004[7] = 1 */
	/* reg_0x98000004[14] = 1 */
	/* CRT spec, rstn_pcie0_power & rstn_pcie0_phy */
	reset_control_deassert(rstc_pcie0_power);
	reset_control_deassert(rstc_pcie0_phy);

	/* reg_0x98000050[13] = 1 */
	/* reg_0x98000050[16] = 1 */
	/* CRT spec, rstn_pcie0_sgmii_mdio & rstn_pcie0_phy_mdio */
	reset_control_deassert(rstc_pcie0_sgmii_mdio);
	reset_control_deassert(rstc_pcie0_phy_mdio);

	/* reg_0x9800000c[7] = 1 */
	/* CRT spec, clk_en_sds */
	clk_prepare_enable(clk_en_sds);

	/* reg_0x9800705c[6] = 1 */
	/* ISO spec, set PCIE channel to SGMII */
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				BIT(6), BIT(6), NULL, false, true);

	/* reg_0x9800705c[7] = 1 */
	/* ISO spec, set internal MDIO to PCIE */
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				BIT(7), BIT(7), NULL, false, true);

	/* ### Beginning of SGMII DPHY register tuning ### */
	/* reg_0x9800705c[20:16] = 00000 */
	/* ISO spec, set internal PHY addr to SERDES_DPHY_0 */
	__int_set_phy_addr(tp, RTD139X_SERDES_DPHY_0);

	/* # DPHY spec, DPHY reg13[8:7]=00, choose 1.25GHz */
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x0d,
		       int_mdio_read(tp, CURRENT_MDIO_PAGE, 0x0d) & ~(BIT(8) | BIT(7)));

	/* # DPHY spec, 5GHz tuning */
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x4, 0x52f5);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x5, 0xead7);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x6, 0x0010);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0xa, 0xc653);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x1, 0xa830);

	/* reg_0x9800705c[20:16] = 00001 */
	/* ISO spec, set internal PHY addr to SERDES_DPHY_1 */
	__int_set_phy_addr(tp, RTD139X_SERDES_DPHY_1);

	/* RTD139X_TX_SWING_1040MV */
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x0, 0xd4aa);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x1, 0x88aa);

	/* reg_0x9800705c[20:16] = 00001 */
	/* ISO spec, set internal PHY addr to INT_PHY_ADDR */
	__int_set_phy_addr(tp, RTD139X_INT_PHY_ADDR);

	tp->ext_phy = true;
	mdelay(10);     /* wait for clock stable */
	/* ext_phy == true now */

	/* reg_0x981c8070[9:0] = 0000000110 */
	/* SDS spec, set SP_CFG_SDS_DBG_SEL to get PHY_Ready */
	regmap_update_bits_base(tp->sds_base, RTD139X_SDS_REG28,
				GENMASK(9, 0), (0x006 << 0), NULL, false, true);

	tmp = 0;
	stable_ticks = 0;
	while (stable_ticks < 10) {
		/* # SDS spec, wait for phy ready */
		regmap_read(tp->sds_base, RTD139X_SDS_REG29, &val);
		while ((val & BIT(14)) == 0) {
			stable_ticks = 0;
			if (tmp >= 100) {
				stable_ticks = 10;
				pr_err(PFX "SGMII PHY not ready in 100ms\n");
				break;
			}
			regmap_read(tp->sds_base, RTD139X_SDS_REG29, &val);
		}
		stable_ticks++;
		tmp++;
		mdelay(1);
	}
	pr_info(PFX "SGMII PHY is ready in %d ms", tmp);

	/* reg_0x9800705c[4] = 1 */
	/* ISO spec, set data path to SGMII */
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				BIT(4), BIT(4), NULL, false, true);

	/* reg_0x981c8008[9:8] = 00 */
	/* # SDS spec, SP_SDS_FRC_AN, SERDES auto mode */
	regmap_update_bits_base(tp->sds_base, RTD139X_SDS_REG02,
				GENMASK(9, 8), 0, NULL, false, true);

	/* # SDS spec, wait for SERDES link up */
	tmp = 0;
	stable_ticks = 0;
	while (stable_ticks < 10) {
		regmap_read(tp->sds_base, RTD139X_SDS_MISC, &val);
		while ((val & GENMASK(13, 12)) != GENMASK(13, 12)) {
			stable_ticks = 0;
			if (tmp >= 100) {
				stable_ticks = 10;
				pr_err(PFX "SGMII link down in 100ms\n");
				break;
			}
			regmap_read(tp->sds_base, RTD139X_SDS_MISC, &val);
		}
		stable_ticks++;
		tmp++;
		mdelay(1);
	}
	pr_info(PFX "SGMII link up in %d ms", tmp);

	reset_control_put(rstc_sds_reg);
	reset_control_put(rstc_sds);
	reset_control_put(rstc_pcie0_power);
	reset_control_put(rstc_pcie0_phy);
	reset_control_put(rstc_pcie0_sgmii_mdio);
	reset_control_put(rstc_pcie0_phy_mdio);
	return 0;
}

static void rtd139x_phy_iol_tuning(struct rtl8169_private *tp)
{
	int revision = REVISION_NONE;
	const struct soc_device_attribute *soc;
	const struct soc_device_attribute rtk_soc_rtd139x[] = {
		{
			.family = "Realtek Hercules",
			.revision = "A00",
			.data = (void *)REVISION_A00,
		},
		{
			.family = "Realtek Hercules",
			.revision = "A01",
			.data = (void *)REVISION_A01,
		},
		{
			.family = "Realtek Hercules",
			.revision = "A02",
			.data = (void *)REVISION_A02,
		},
		{
		/* empty */
		}
	};

	soc = soc_device_match(rtk_soc_rtd139x);
	if (soc)
		revision = (uintptr_t)soc->data;

	switch (revision) {
	case REVISION_A00: /* TSMC, cut A */
	case REVISION_A01: /* TSMC, cut B */
		/* idacfine */
		int_mdio_write(tp, 0x0bc0, 23, 0x0088);

		/* abiq */
		int_mdio_write(tp, 0x0bc0, 21, 0x0004);

		/* ldvbias */
		int_mdio_write(tp, 0x0bc0, 22, 0x0777);

		/* iatt */
		int_mdio_write(tp, 0x0bd0, 16, 0x0300);

		/* vcm_ref, cf_l */
		int_mdio_write(tp, 0x0bd0, 17, 0xe8ca);
		break;
	case REVISION_A02: /* UMC, cut C */
		/* 100M Swing */
		/* idac_fine_mdix, idac_fine_mdi */
		int_mdio_write(tp, 0x0bc0, 23, 0x0044);

		/* 100M Tr/Tf */
		/* abiq_10m=0x0, abiq_100m_short=0x4, abiq_normal=0x6 */
		int_mdio_write(tp, 0x0bc0, 21, 0x0046);

		/* 10M */
		/* ldvbias_10m=0x7, ldvbias_10m_short=0x4, ldvbias_normal=0x4 */
		int_mdio_write(tp, 0x0bc0, 22, 0x0744);

		/* vcmref=0x0, cf_l=0x3 */
		int_mdio_write(tp, 0x0bd0, 17, 0x18ca);

		/* iatt=0x2 */
		int_mdio_write(tp, 0x0bd0, 16, 0x0200);
		break;

	/* default: */
	}
}

static void rtd139x_mdio_init(struct rtl8169_private *tp)
{
	u32 tmp;
	u32 val;
	struct pinctrl *p_sgmii_mdio;
	struct pinctrl_state *ps_sgmii_mdio;
	void __iomem *ioaddr = tp->mmio_addr;

	/* ETN_PHY_INTR, wait interrupt from PHY and it means MDIO is ready */
	tmp = 0;
	regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	while ((val & BIT(27)) == 0) {
		tmp += 1;
		mdelay(1);
		if (tmp >= 100) {
			pr_err(PFX "PHY PHY_Status timeout.\n");
			break;
		}
		regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	}
	pr_info(PFX "wait %d ms for PHY interrupt. UMSK_ISR = 0x%x\n", tmp, val);

	/* In Hercules ByPass mode,
	 * SW need to handle the EPHY Status check ,
	 * OTP data update and EPHY fuse_rdy setting.
	 */
	if (tp->bypass_enable) {
		/* PHY will stay in state 1 mode */
		tmp = 0;
		while (0x1 != (int_mdio_read(tp, 0x0a42, 16) & 0x07)) {
			tmp += 1;
			mdelay(1);
			if (tmp >= 2000) {
				pr_err(PFX "PHY status is not 0x1 in bypass mode, current = 0x%02x\n",
				       (int_mdio_read(tp, 0x0a42, 16) & 0x07));
				break;
			}
		}

		/* adjust FE PHY electrical characteristics */
		rtd139x_phy_iol_tuning(tp);

		/* 1. read OTP 0x9801_72C8[12:8]
		 * 2. xor 0x08
		 * 3. set value to PHY registers to correct R-calibration
		 * 4. read OTP 0x9801_72D8[7:0]
		 * 5. xor 0x33
		 * 6. set value to PHY registers to correct AMP
		 */
		rtd139x_load_otp_content(tp);

		/* fill fuse_rdy & rg_ext_ini_done */
		int_mdio_write(tp, 0x0a46, 20,
			       (int_mdio_read(tp, 0x0a46, 20) | (BIT(1) | BIT(0))));
	} else {
		/* adjust FE PHY electrical characteristics */
		rtd139x_phy_iol_tuning(tp);
	}

	/* init_autoload_done = 1 */
	tmp = rtl_ocp_read(tp, 0xe004);
	tmp |= BIT(7);
	rtl_ocp_write(tp, 0xe004, tmp);
	/* ee_mode = 3 */
	RTL_W8(CFG9346, CFG9346_UNLOCK);

	/* wait LAN-ON */
	tmp = 0;
	do {
		tmp += 1;
		mdelay(1);
		if (tmp >= 2000) {
			pr_err(PFX "PHY status is not 0x3, current = 0x%02x\n",
			       (int_mdio_read(tp, 0x0a42, 16) & 0x07));
			break;
		}
	} while (0x3 != (int_mdio_read(tp, 0x0a42, 16) & 0x07));
	pr_info(PFX "wait %d ms for PHY ready, current = 0x%x\n",
		tmp, int_mdio_read(tp, 0x0a42, 16));

	if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
		/* Init PHY path */
		/* reg_0x9800705c[5] = 0 */
		/* reg_0x9800705c[7] = 0 */
		/* ISO spec, set internal MDIO to access PHY */
		regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
					BIT(7) | BIT(5), 0, NULL, false, true);

		/* reg_0x9800705c[4] = 0 */
		/* ISO spec, set data path to access PHY */
		regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
					BIT(4), 0, NULL, false, true);

		/* # ETN spec, GMAC data path select MII-like(embedded GPHY),
		 * not SGMII(external PHY)
		 */
		tmp = rtl_ocp_read(tp, 0xea34) & ~(BIT(0) | BIT(1));
		tmp |= BIT(1); /* MII */
		rtl_ocp_write(tp, 0xea34, tmp);
	} else {
		/* SGMII */
		/* # ETN spec, adjust MDC freq=2.5MHz */
		rtl_ocp_write(tp, 0xDE30,
			      rtl_ocp_read(tp, 0xDE30) & ~(BIT(7) | BIT(6)));
		/* # ETN spec, set external PHY addr */
		rtl_ocp_write(tp, 0xDE24,
			      ((rtl_ocp_read(tp, 0xDE24) & ~(0x1f << 0)) |
					     (tp->ext_phy_id & 0x1f)));
		/* ISO mux spec, GPIO29 is set to MDC pin */
		/* ISO mux spec, GPIO46 is set to MDIO pin */
		p_sgmii_mdio = devm_pinctrl_get(&tp->pdev->dev);
		ps_sgmii_mdio = pinctrl_lookup_state(p_sgmii_mdio, "sgmii");
		pinctrl_select_state(p_sgmii_mdio, ps_sgmii_mdio);

		/* check if external PHY is available */
		pr_info(PFX "Searching external PHY...");
		tp->ext_phy = true;
		tmp = 0;
		while (ext_mdio_read(tp, 0x0a43, 31) != 0x0a43) {
			pr_info(".");
			tmp += 1;
			mdelay(1);
			if (tmp >= 2000) {
				pr_err("\n External SGMII PHY not found, current = 0x%02x\n",
				       ext_mdio_read(tp, 0x0a43, 31));
				break;
			}
		}
		if (tmp < 2000)
			pr_info("found.\n");

		/* lower SGMII TX swing of RTL8211FS to reduce EMI */
		/* TX swing = 470mV, default value */
		ext_mdio_write(tp, 0x0dcd, 16, 0x104e);

		tp->ext_phy = false;

		/* # ETN spec, GMAC data path select SGMII(external PHY),
		 * not MII-like(embedded GPHY)
		 */
		tmp = rtl_ocp_read(tp, 0xea34) & ~(BIT(0) | BIT(1));
		tmp |= BIT(1) | BIT(0); /* SGMII */
		rtl_ocp_write(tp, 0xea34, tmp);

		if (rtd139x_serdes_init(tp) != 0)
			pr_err(PFX "SERDES init failed\n");
		/* ext_phy == true now */

		/* SDS spec, auto update SGMII link capability */
		regmap_update_bits_base(tp->sds_base, RTD139X_SDS_LINK,
					BIT(2), BIT(2), NULL, false, true);
	}
}

static void rtd139x_eee_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		tp->chip->mdio_lock(tp);
		if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
			/* 100M EEE capability */
			rtl_mmd_write(tp, 0x7, 60, BIT(1));
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) | BIT(4) | BIT(2));
			/* disable dynamic RX power in PHY */
			rtl_phy_write(tp, 0x0bd0, 21,
				      (rtl_phy_read(tp, 0x0bd0, 21) & ~BIT(8)) | BIT(9));
		} else { /* SGMII */
			/* 1000M & 100M EEE capability */
			rtl_mmd_write(tp, 0x7, 60, (BIT(2) | BIT(1)));
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) | BIT(4) | BIT(2));
		}
		/* EEE MAC mode */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) | BIT(1) | BIT(0));
		/* EEE+ MAC mode */
		/* timer to wait FEPHY ready */
		rtl_ocp_write(tp, 0xe08a, 0x00a7);
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) | BIT(1));
	} else {
		if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
			/* no EEE capability */
			rtl_mmd_write(tp, 0x7, 60, 0);
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) & ~(BIT(4) | BIT(2)));
		} else { /* SGMII */
			/* no EEE capability */
			rtl_mmd_write(tp, 0x7, 60, 0);
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) & ~(BIT(4) | BIT(2)));
		}
		/* reset to default value */
		rtl_ocp_write(tp, 0xe040, rtl_ocp_read(tp, 0xe040) | BIT(13));
		/* EEE MAC mode */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) & ~(BIT(1) | BIT(0)));
		/* EEE+ MAC mode */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) & ~BIT(1));
		rtl_ocp_write(tp, 0xe08a, 0x003f); /* default value */
	}
}

static void rtd139x_led_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		regmap_update_bits_base(tp->pinctrl_base, RTD139X_ISO_TESTMUX_MUXPAD1,
					GENMASK(7, 4), (5 << 4), NULL, false, true);
	} else {
		regmap_update_bits_base(tp->pinctrl_base, RTD139X_ISO_TESTMUX_MUXPAD1,
					GENMASK(7, 4), 0, NULL, false, true);
	}
}

static void rtd139x_dump_regs(struct seq_file *m, struct rtl8169_private *tp)
{
	u32 val;

	regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	seq_printf(m, "ISO_UMSK_ISR\t[0x98007004] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_PWRCUT_ETN, &val);
	seq_printf(m, "ISO_PWRCUT_ETN\t[0x9800705c] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_ETN_TESTIO, &val);
	seq_printf(m, "ISO_ETN_TESTIO\t[0x98007060] = %08x\n", val);
	regmap_read(tp->iso_base,  ISO_SOFT_RESET, &val);
	seq_printf(m, "ETN_RESET_CTRL\t[0x98007088] = %08x\n", val);
	regmap_read(tp->iso_base,  ISO_CLOCK_ENABLE, &val);
	seq_printf(m, "ETN_CLK_CTRL\t[0x9800708c] = %08x\n", val);
	if (tp->output_mode != OUTPUT_EMBEDDED_PHY) {
		regmap_read(tp->sds_base, RTD139X_SDS_REG02, &val);
		seq_printf(m, "SDS_REG02\t[0x981c8008] = %08x\n", val);
		regmap_read(tp->sds_base, RTD139X_SDS_REG28, &val);
		seq_printf(m, "SDS_REG28\t[0x981c8070] = %08x\n", val);
		regmap_read(tp->sds_base, RTD139X_SDS_REG29, &val);
		seq_printf(m, "SDS_REG29\t[0x981c8074] = %08x\n", val);
		regmap_read(tp->sds_base, RTD139X_SDS_MISC, &val);
		seq_printf(m, "SDS_MISC\t\t[0x981c9804] = %08x\n", val);
		regmap_read(tp->sds_base, RTD139X_SDS_LINK, &val);
		seq_printf(m, "SDS_LINK\t\t[0x981c9810] = %08x\n", val);
	}
}

static void rtd139x_dump_var(struct seq_file *m, struct rtl8169_private *tp)
{
	seq_printf(m, "bypass_enable\t%d\n", tp->bypass_enable);
}

/******************* END of RTD139X ****************************/

/* RTD16XX */
static void rtd16xx_mdio_lock(struct rtl8169_private *tp)
{
	rtd139x_mdio_lock(tp);
}

static void rtd16xx_mdio_unlock(struct rtl8169_private *tp)
{
	rtd139x_mdio_unlock(tp);
}

static void rtd16xx_reset_phy_gmac(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");
	struct clk *clk_en_sds = NULL;
	struct reset_control *rstc_sds_reg = NULL;
	struct reset_control *rstc_sds = NULL;
	struct reset_control *rstc_pcie0_power = NULL;
	struct reset_control *rstc_pcie0_phy = NULL;
	struct reset_control *rstc_pcie0_sgmii_mdio = NULL;
	struct reset_control *rstc_pcie0_phy_mdio = NULL;

	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);

	if (tp->output_mode != OUTPUT_EMBEDDED_PHY) {
		clk_en_sds = clk_get(&tp->pdev->dev, "sds");
		rstc_sds_reg =
			reset_control_get_exclusive(&tp->pdev->dev, "sds_reg");
		rstc_sds = reset_control_get_exclusive(&tp->pdev->dev, "sds");
		rstc_pcie0_power =
			reset_control_get_exclusive(&tp->pdev->dev, "pcie0_power");
		rstc_pcie0_phy =
			reset_control_get_exclusive(&tp->pdev->dev, "pcie0_phy");
		rstc_pcie0_sgmii_mdio =
			reset_control_get_exclusive(&tp->pdev->dev, "pcie0_sgmii_mdio");
		rstc_pcie0_phy_mdio =
			reset_control_get_exclusive(&tp->pdev->dev, "pcie0_phy_mdio");
	}

	/* reg_0x9800708c[12:11] = 00 */
	/* ISO spec, clock enable bit for etn clock & etn 250MHz */
	clk_disable_unprepare(clk_etn_sys);
	clk_disable_unprepare(clk_etn_250m);

	/* reg_0x98007088[10:9] = 00 */
	/* ISO spec, rstn_gphy & rstn_gmac */
	reset_control_assert(rstc_gphy);
	reset_control_assert(rstc_gmac);

	/* reg_0x98007060[1] = 1 */
	/* ISO spec, bypass mode enable */
	regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
				BIT(1), BIT(1), NULL, false, true);

	/* reg_0x9800705c = 0x00616703 */
	/* ISO spec, default value */
	regmap_write(tp->iso_base, ISO_PWRCUT_ETN, 0x00616703);

	if (tp->output_mode != OUTPUT_EMBEDDED_PHY) {
		/* RESET for SGMII if needed */
		clk_prepare_enable(clk_en_sds);

		/* reg_0x98000050[13:12] = 10 */
		/* CRT spec, clk_en_sds */
		clk_disable_unprepare(clk_en_sds);

		/* reg_0x98000000[7:6] = 10 */
		/* reg_0x98000000[9:8] = 10 */
		/* CRT spec, rstn_sds_reg & rstn_sds */
		reset_control_assert(rstc_sds);
		reset_control_assert(rstc_sds_reg);

		/* reg_0x98000004[25:24] = 10 CRT spec, rstn_pcie0_sgmii_mdio */
		/* reg_0x98000004[23:22] = 10 CRT spec, rstn_pcie0_phy_mdio */
		/* reg_0x98000004[19:18] = 10 CRT spec, rstn_pcie0_power */
		/* reg_0x98000004[13:12] = 10 CRT spec, rstn_pcie0_phy */
		reset_control_assert(rstc_pcie0_sgmii_mdio);
		reset_control_assert(rstc_pcie0_phy_mdio);
		reset_control_assert(rstc_pcie0_power);
		reset_control_assert(rstc_pcie0_phy);
	}

	mdelay(1);

	/* release resource */
	if (tp->output_mode != OUTPUT_EMBEDDED_PHY) {
		reset_control_put(rstc_sds_reg);
		reset_control_put(rstc_sds);
		reset_control_put(rstc_pcie0_power);
		reset_control_put(rstc_pcie0_phy);
		reset_control_put(rstc_pcie0_sgmii_mdio);
		reset_control_put(rstc_pcie0_phy_mdio);
	}
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd16xx_acp_init(struct rtl8169_private *tp)
{
	u32 tmp;
	u32 val;

	/* SBX spec, Select ETN access DDR path. */
	if (tp->acp_enable) {
		/* reg_0x9801c20c[6] = 1 */
		/* SBX spec, Mask ETN_ALL to SB3 DBUS REQ */
		regmap_update_bits_base(tp->sbx_base, RTD16XX_SBX_SB3_CHANNEL_REQ_MASK,
					BIT(6), BIT(6), NULL, false, true);

		pr_info(PFX "wait all SB3 access finished...");
		tmp = 0;
		regmap_read(tp->sbx_base, RTD16XX_SBX_SB3_CHANNEL_REQ_BUSY, &val);
		while (val & BIT(6)) {
			mdelay(1);
			tmp += 1;
			if (tmp >= 100) {
				pr_err("\n wait SB3 access failed (wait %d ms)\n", tmp);
				break;
			}
			regmap_read(tp->sbx_base, RTD16XX_SBX_SB3_CHANNEL_REQ_BUSY, &val);
		}
		if (tmp < 100)
			pr_info("done.\n");

		/* reg_0x9801d100[29] = 0 */
		/* SCPU wrapper spec, CLKACP division, 0 = div 2, 1 = div 3 */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_CRT_CTRL,
					BIT(29), 0, NULL, false, true);

		/* reg_0x9801d124[1:0] = 00 */
		/* SCPU wrapper spec, ACP master active, 0 = active */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_INTERFACE_EN,
					GENMASK(1, 0), 0, NULL, false, true);

		/* reg_0x9801d100[30] = 1 */
		/* SCPU wrapper spec, dy_icg_en_acp */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_CRT_CTRL,
					BIT(30), BIT(30), NULL, false, true);

		/* reg_0x9801d100[21] = 1 */
		/* SCPU wrapper spec, ACP CLK enable */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_CRT_CTRL,
					BIT(21), BIT(21), NULL, false, true);

		/* reg_0x9801d100[14] = 1 */
		/* SCPU wrapper spec,
		 * Do not apply reset to ACP port axi3 master
		 */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_CRT_CTRL,
					BIT(14), BIT(14), NULL, false, true);

		/* reg_0x9801d800[3:0] = 0111 */
		/* reg_0x9801d800[7:4] = 0111 */
		/* reg_0x9801d800[9] = 1 */
		/* reg_0x9801d800[20:16] = 01100 */
		/* reg_0x9801d800[28:24] = 01110 */
		/* Configure control of ACP port */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_ACP_CTRL,
					GENMASK(28, 24) | GENMASK(20, 16) |
					BIT(9) | GENMASK(7, 4) | GENMASK(3, 0),
					(0x0e << 24) | (0x0c << 16) | BIT(9) |
					(0x7 << 4) | (0x7 << 0),
					NULL, false, true);

		/* reg_0x9801d030[28] = 1 */
		/* SCPU wrapper spec, dy_icg_en_acp */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_ACP_CRT_CTRL,
					BIT(28), BIT(28), NULL, false, true);

		/* reg_0x9801d030[16] = 1 */
		/* SCPU wrapper spec, ACP CLK Enable for acp of scpu_chip_top */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_ACP_CRT_CTRL,
					BIT(16), BIT(16), NULL, false, true);

		/* reg_0x9801d030[0] = 1 */
		/* SCPU wrapper spec,
		 * Do not apply reset to ACP port axi3 master
		 */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_ACP_CRT_CTRL,
					BIT(0), BIT(0), NULL, false, true);

		/* reg_0x9801c814[17] = 1 */
		/* through ACP to SCPU_ACP */
		regmap_update_bits_base(tp->sbx_base, RTD16XX_SBX_ACP_MISC_CTRL,
					BIT(17), BIT(17), NULL, false, true);

		/* SBX spec, Remove mask ETN_ALL to ACP DBUS REQ */
		regmap_update_bits_base(tp->sbx_base, RTD16XX_SBX_ACP_CHANNEL_REQ_MASK,
					BIT(1), 0, NULL, false, true);

		pr_info(PFX "ARM ACP on\n.");
	} else {
		/* SBX spec, Mask ETN_ALL to ACP DBUS REQ */
		regmap_update_bits_base(tp->sbx_base, RTD16XX_SBX_ACP_CHANNEL_REQ_MASK,
					BIT(1), BIT(1), NULL, false, true);

		pr_info(PFX "wait all ACP access finished...");
		tmp = 0;
		regmap_read(tp->sbx_base, RTD16XX_SBX_ACP_CHANNEL_REQ_BUSY, &val);
		while (val & BIT(1)) {
			mdelay(1);
			tmp += 1;
			if (tmp >= 100) {
				pr_err("\n ACP channel is still busy (wait %d ms)\n", tmp);
				break;
			}
			regmap_read(tp->sbx_base, RTD16XX_SBX_ACP_CHANNEL_REQ_BUSY, &val);
		}
		if (tmp < 100)
			pr_info("done.\n");

		/* SCPU wrapper spec, Inactive MP4 AINACTS signal */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_INTERFACE_EN,
					GENMASK(1, 0), GENMASK(1, 0), NULL, false, true);

		/* SCPU wrapper spec, nACPRESET_DVFS & CLKENACP_DVFS */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_CRT_CTRL,
					(BIT(21) | BIT(14)), 0, NULL, false, true);

		/* SCPU wrapper spec, nACPRESET & CLKENACP */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD16XX_SC_WRAP_ACP_CRT_CTRL,
					(BIT(16) | BIT(0)), 0, NULL, false, true);

		/* reg_0x9801c814[17] = 0 */
		/* SBX spec, Switch ETN_ALL to DC_SYS path */
		regmap_update_bits_base(tp->sbx_base, RTD16XX_SBX_ACP_MISC_CTRL,
					BIT(17), 0, NULL, false, true);

		/* SBX spec, Remove mask ETN_ALL to SB3 DBUS REQ */
		regmap_update_bits_base(tp->sbx_base, RTD16XX_SBX_SB3_CHANNEL_REQ_MASK,
					BIT(6), 0, NULL, false, true);

		pr_info(PFX "ARM ACP off\n.");
	}
}

static void rtd16xx_pll_clock_init(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	/* reg_0x98007004[27] = 1 */
	/* ISO spec, ETN_PHY_INTR, clear ETN interrupt for ByPassMode */
	regmap_write(tp->iso_base, ISO_UMSK_ISR, BIT(27));

	/* reg_0x98007088[10] = 1 */
	/* ISO spec, reset bit of gphy */
	reset_control_deassert(rstc_gphy);

	mdelay(1);		/* wait 1ms for PHY PLL stable */

	/* Thor only supports the bypass mode */
	/* Bypass mode : ETN MAC bypass efuse update flow.
	 * SW need to take this sequence.
	 */
	/* reg_0x98007060[1] = 1 */
	/* ISO spec, bypass mode enable */
	/* bypass mode, SW need to handle the EPHY Status check ,
	 * EFUSE data update and EPHY fuse_rdy setting.
	 */
	regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
				BIT(1), BIT(1), NULL, false, true);

	/* reg_0x98007088[9] = 1 */
	/* ISO spec, reset bit of gmac */
	reset_control_deassert(rstc_gmac);

	/* reg_0x9800708c[12:11] = 11 */
	/* ISO spec, clock enable bit for etn clock & etn 250MHz */
	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);

	mdelay(10);		/* wait 10ms for GMAC uC to be stable */

	/* release resource */
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd16xx_load_otp_content(struct rtl8169_private *tp)
{
	u32 otp;
	u16 tmp;
	u8 *buf;

	/* RC-K 0x980174F8[27:24] */
	buf = r8169soc_read_otp(tp, "rc_k");
	if (IS_ERR(buf))
		goto set_r_amp_cal;

	otp = *buf;
	tmp = (otp << 12) | (otp << 8) | (otp << 4) | otp;
	tmp ^= RTD16XX_RC_K_DEFAULT;
	int_mdio_write(tp, 0x0bcd, 22, tmp);
	int_mdio_write(tp, 0x0bcd, 23, tmp);

	kfree(buf);

set_r_amp_cal:

	buf = r8169soc_read_otp(tp, "r_amp_k");
	if (IS_ERR(buf))
		return;

	/* R-K 0x98017500[18:15] */
	otp = ((buf[5] & 0x80) >> 7) | ((buf[6] & 0x07) << 1);
	tmp = (otp << 12) | (otp << 8) | (otp << 4) | otp;
	tmp ^= RTD16XX_R_K_DEFAULT;
	int_mdio_write(tp, 0x0bce, 16, tmp);
	int_mdio_write(tp, 0x0bce, 17, tmp);

	/* Amp-K 0x980174FC[15:0] */
	otp = buf[0] | (buf[1] << 8);
	tmp = otp ^ RTD16XX_AMP_K_DEFAULT;
	int_mdio_write(tp, 0x0bca, 22, tmp);

	/* Bias-K 0x980174FC[31:16] */
	otp = buf[2] | (buf[3] << 8);
	tmp = otp ^ RTD16XX_ADC_BIAS_K_DEFAULT;
	int_mdio_write(tp, 0x0bcf, 22, tmp);

	kfree(buf);
}

static u32 rtd16xx_serdes_init(struct rtl8169_private *tp)
{
	u32 stable_ticks;
	u32 tmp;
	u32 val;
	struct clk *clk_en_sds = clk_get(&tp->pdev->dev, "sds");
	struct reset_control *rstc_sds_reg =
		reset_control_get_exclusive(&tp->pdev->dev, "sds_reg");
	struct reset_control *rstc_sds =
		reset_control_get_exclusive(&tp->pdev->dev, "sds");
	struct reset_control *rstc_pcie0_power =
		reset_control_get_exclusive(&tp->pdev->dev, "pcie0_power");
	struct reset_control *rstc_pcie0_phy =
		reset_control_get_exclusive(&tp->pdev->dev, "pcie0_phy");
	struct reset_control *rstc_pcie0_sgmii_mdio =
		reset_control_get_exclusive(&tp->pdev->dev, "pcie0_sgmii_mdio");
	struct reset_control *rstc_pcie0_phy_mdio =
		reset_control_get_exclusive(&tp->pdev->dev, "pcie0_phy_mdio");

	/* reg_0x98000050[13:12] = 11 */
	/* CRT spec, clk_en_sds */
	clk_prepare_enable(clk_en_sds);

	/* reg_0x98000000[9:8] = 11 */
	/* reg_0x98000000[7:6] = 11 */
	/* CRT spec, rstn_sds_reg & rstn_sds */
	reset_control_deassert(rstc_sds);
	reset_control_deassert(rstc_sds_reg);

	/* reg_0x98000004[25:24] = 11   CRT spec, rstn_pcie0_sgmii_mdio */
	/* reg_0x98000004[23:22] = 11   CRT spec, rstn_pcie0_phy_mdio */
	/* reg_0x98000004[19:18] = 11   CRT spec, rstn_pcie0_power */
	/* reg_0x98000004[13:12] = 11   CRT spec, rstn_pcie0_phy */
	reset_control_deassert(rstc_pcie0_power);
	reset_control_deassert(rstc_pcie0_phy);
	reset_control_deassert(rstc_pcie0_sgmii_mdio);
	reset_control_deassert(rstc_pcie0_phy_mdio);

	/* reg_0x9800705c[6] = 1 */
	/* ISO spec, set PCIe channel to SGMII */
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				BIT(6), BIT(6), NULL, false, true);

	/* ### Beginning of SGMII DPHY register tuning ### */
	/* reg_0x9800705c[7] = 1 */
	/* ISO spec, set internal MDIO to PCIe */
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				BIT(7), BIT(7), NULL, false, true);

	/* reg_0x9800705c[20:16] = 00000 */
	/* ISO spec, set internal PHY addr to SERDES_DPHY_0 */
	__int_set_phy_addr(tp, RTD16XX_SERDES_DPHY_0);

	/* # DPHY spec, 5GHz tuning */
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x4, 0x52f5);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x5, 0xead7);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x6, 0x0010);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0xa, 0xc653);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x1, 0xe030);
	int_mdio_write(tp, CURRENT_MDIO_PAGE, 0xd, 0xee1c);

	/* reg_0x9800705c[20:16] = 00001 */
	/* ISO spec, set internal PHY addr to SERDES_DPHY_1 */
	__int_set_phy_addr(tp, RTD16XX_SERDES_DPHY_1);

	/* tx_swing_550mv by default */
	switch (tp->sgmii.swing) {
	case RTD16XX_TX_SWING_190MV:
		int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x0, 0xd411);
		int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x1, 0x2277);
		break;
	case RTD16XX_TX_SWING_250MV:
		int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x0, 0xd433);
		int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x1, 0x2244);
		break;
	case RTD16XX_TX_SWING_380MV:
		int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x0, 0xd433);
		int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x1, 0x22aa);
		break;
	case RTD16XX_TX_SWING_550MV:	/* recommended by RDC */
	default:
		int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x0, 0xd455);
		int_mdio_write(tp, CURRENT_MDIO_PAGE, 0x1, 0x2828);
	}

	/* reg_0x9800705c[20:16] = 00001 */
	/* ISO spec, set internal PHY addr to INT_PHY_ADDR */
	__int_set_phy_addr(tp, RTD16XX_INT_PHY_ADDR);

	mdelay(10);		/* wait for clock stable */

	/* reg_0x9800705c[5] = 0 */
	/* reg_0x9800705c[7] = 0 */
	/* ISO spec, set internal MDIO to GPHY */
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				(BIT(7) | BIT(5)), 0, NULL, false, true);

	tp->ext_phy = true;
	/* ext_phy == true now */

	tmp = 0;
	stable_ticks = 0;
	while (stable_ticks < 10) {
		/* # SDS spec, wait for phy ready */
		regmap_read(tp->sds_base, RTD16XX_SDS_LINK, &val);
		while ((val & BIT(24)) == 0) {
			stable_ticks = 0;
			if (tmp >= 100) {
				stable_ticks = 10;
				pr_err(PFX "SGMII PHY not ready in 100ms\n");
				break;
			}
			regmap_read(tp->sds_base, RTD16XX_SDS_LINK, &val);
		}
		stable_ticks++;
		tmp++;
		mdelay(1);
	}
	pr_info(PFX "SGMII PHY is ready in %d ms", tmp);

	/* reg_0x9800705c[4] = 1 */
	/* ISO spec, set data path to SGMII */
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				BIT(4), BIT(4), NULL, false, true);

	/* reg_0x981c8008[9:8] = 00 */
	/* # SDS spec, SP_SDS_FRC_AN, SERDES auto mode */
	regmap_update_bits_base(tp->sds_base, RTD16XX_SDS_REG02,
				GENMASK(9, 8), 0, NULL, false, true);

	/* # SDS spec, wait for SERDES link up */
	tmp = 0;
	stable_ticks = 0;
	while (stable_ticks < 10) {
		regmap_read(tp->sds_base, RTD16XX_SDS_MISC, &val);
		while ((val & BIT(12)) == 0) {
			stable_ticks = 0;
			if (tmp >= 100) {
				stable_ticks = 10;
				pr_err(PFX "SGMII link down in 100ms\n");
				break;
			}
			regmap_read(tp->sds_base, RTD16XX_SDS_MISC, &val);
		}
		stable_ticks++;
		tmp++;
		mdelay(1);
	}
	pr_info(PFX "SGMII link up in %d ms", tmp);

	reset_control_put(rstc_sds_reg);
	reset_control_put(rstc_sds);
	reset_control_put(rstc_pcie0_power);
	reset_control_put(rstc_pcie0_phy);
	reset_control_put(rstc_pcie0_sgmii_mdio);
	reset_control_put(rstc_pcie0_phy_mdio);
	return 0;
}

static void rtd16xx_phy_iol_tuning(struct rtl8169_private *tp)
{
	/* for common mode voltage */
	int_mdio_write(tp, 0x0bc0, 17,
		       (int_mdio_read(tp, 0x0bc0, 17) & ~(0xff << 4)) | (0xb4 << 4));

	/* for 1000 Base-T, Transmitter Distortion */
	int_mdio_write(tp, 0x0a43, 27, 0x8082);
	int_mdio_write(tp, 0x0a43, 28,
		       (int_mdio_read(tp, 0x0a43, 28) & ~(0xff << 8)) | (0xae << 8));

	/* for 1000 Base-T, Master Filtered Jitter */
	int_mdio_write(tp, 0x0a43, 27, 0x807c);
	/* adjust ldvbias_busy, abiq_busy, gdac_ib_up_tm
	 * to accelerate slew rate
	 */
	int_mdio_write(tp, 0x0a43, 28, 0xf001);
	/* set CP_27to25=7 and REF_27to25_L=4 to decrease jitter */
	int_mdio_write(tp, 0x0bc5, 16, 0xc67f);
}

static void rtd16xx_phy_sram_table(struct rtl8169_private *tp)
{
	/* enable echo power*2 */
	int_mdio_write(tp, 0x0a42, 22, 0x0f10);

	/* Channel estimation, 100Mbps adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x8087);	/* gain_i slope */
	int_mdio_write(tp, 0x0a43, 28, 0x42f0);	/* 0x43 => 0x42 */
	int_mdio_write(tp, 0x0a43, 27, 0x808e);	/* clen_i_c initial value */
	int_mdio_write(tp, 0x0a43, 28, 0x14a4);	/* 0x13 => 0x14 */
	/* adc peak adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x8088);	/* aagc_lvl_c initial value 0x3f0 => ok */
	int_mdio_write(tp, 0x0a43, 28, 0xf0eb);	/* delta_a slope 0x1e => 0x1d */
	/* cb0 adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x808c);	/* cb0_i_c initial value */
	int_mdio_write(tp, 0x0a43, 28, 0xef09);	/* 0x9ef => ok */
	int_mdio_write(tp, 0x0a43, 27, 0x808f);	/* delta_b slope */
	int_mdio_write(tp, 0x0a43, 28, 0xa4c6);	/* 0xa4 => ok */
	/* DAGC adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x808a);	/* cg_i_c initial value */
	int_mdio_write(tp, 0x0a43, 28, 0x400a);	/* 0xb40 => 0xa40 */
	int_mdio_write(tp, 0x0a43, 27, 0x8092);	/* delta_g slope */
	int_mdio_write(tp, 0x0a43, 28, 0xc21e);	/* 0x0c0 => 0x0c2 */

	/* 1000Mbps master adjustment */
	/* line adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x8099);	/* gain_i slope */
	int_mdio_write(tp, 0x0a43, 28, 0x2ae0);	/* 0x2e => 0x2a */
	int_mdio_write(tp, 0x0a43, 27, 0x80a0);	/* clen_i_c initial value */
	int_mdio_write(tp, 0x0a43, 28, 0xf28f);	/* 0xfe => 0xf2 */
	/* adc peak adjustment */
	/* aagc_lvl_c initial value 0x3e0 => 0x470 */
	int_mdio_write(tp, 0x0a43, 27, 0x809a);
	int_mdio_write(tp, 0x0a43, 28, 0x7084);	/* delta_a slope 0x0d => 0x10 */
	/* cb0 adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x809e);	/* cb0_i_c initial value */
	int_mdio_write(tp, 0x0a43, 28, 0xa008);	/* 0x7a1 => 0x8a0 */
	int_mdio_write(tp, 0x0a43, 27, 0x80a1);	/* delta_b slope */
	int_mdio_write(tp, 0x0a43, 28, 0x783d);	/* 0x8f => 0x78 */
	/* DAGC adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x809c);	/* cg_i_c initial value */
	int_mdio_write(tp, 0x0a43, 28, 0x8008);	/* 0xb00 => 0x880 */
	int_mdio_write(tp, 0x0a43, 27, 0x80a4);	/* delta_g slope */
	int_mdio_write(tp, 0x0a43, 28, 0x580c);	/* 0x063 => 0x058 */

	/* 1000Mbps slave adjustment */
	/* line adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x80ab);	/* gain_i slope */
	int_mdio_write(tp, 0x0a43, 28, 0x2b4a);	/* 0x2e => 0x2b */
	int_mdio_write(tp, 0x0a43, 27, 0x80b2);	/* clen_i_c initial value */
	int_mdio_write(tp, 0x0a43, 28, 0xf47f);	/* 0xfa => 0xf4 */
	/* adc peak adjustment */
	/* aagc_lvl_c initial value 0x44a => 0x488 */
	int_mdio_write(tp, 0x0a43, 27, 0x80ac);
	int_mdio_write(tp, 0x0a43, 28, 0x8884);	/* delta_a slope 0x0e => 0x10 */
	/* cb0 adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x80b0);	/* cb0_i_c initial value */
	int_mdio_write(tp, 0x0a43, 28, 0xa107);	/* 0x6a1 => 0x7a1 */
	int_mdio_write(tp, 0x0a43, 27, 0x80b3);	/* delta_b slope */
	int_mdio_write(tp, 0x0a43, 28, 0x683d);	/* 0x7f => 0x68 */
	/* DAGC adjustment */
	int_mdio_write(tp, 0x0a43, 27, 0x80ae);	/* cg_i_c initial value */
	int_mdio_write(tp, 0x0a43, 28, 0x2006);	/* 0x760 => 0x620 */
	int_mdio_write(tp, 0x0a43, 27, 0x80b6);	/* delta_g slope */
	int_mdio_write(tp, 0x0a43, 28, 0x850c);	/* 0x090 => 0x085 */
}

static void rtd16xx_patch_gphy_uc_code(struct rtl8169_private *tp)
{
	u32 tmp;

	/* patch for GPHY uC firmware,
	 * adjust 1000M EEE lpi_waketx_timer = 1.3uS
	 */
#define PATCH_KEY_ADDR  0x8028	/* for RL6525 */
#define PATCH_KEY       0x5600	/* for RL6525 */

	/* Patch request & wait for the asserting of patch_rdy */
	int_mdio_write(tp, 0x0b82, 16,
		       int_mdio_read(tp, 0x0b82, 16) | BIT(4));

	tmp = 0;
	while ((int_mdio_read(tp, 0x0b80, 16) & BIT(6)) == 0) {
		tmp += 1;
		mdelay(1);
		if (tmp >= 100) {
			pr_err(PFX "GPHY patch_rdy timeout.\n");
			break;
		}
	}
	pr_info(PFX "wait %d ms for GPHY patch_rdy. reg = 0x%x\n",
		tmp, int_mdio_read(tp, 0x0b80, 16));
	pr_info(PFX "patch_rdy is asserted!!\n");

	/* Set patch_key & patch_lock */
	int_mdio_write(tp, 0, 27, PATCH_KEY_ADDR);
	int_mdio_write(tp, 0, 28, PATCH_KEY);
	int_mdio_write(tp, 0, 27, PATCH_KEY_ADDR);
	pr_info(PFX "check patch key = %04x\n", int_mdio_read(tp, 0, 28));
	int_mdio_write(tp, 0, 27, 0xb82e);
	int_mdio_write(tp, 0, 28, 0x0001);

	/* uC patch code, released by Digital Designer */
	int_mdio_write(tp, 0, 27, 0xb820);
	int_mdio_write(tp, 0, 28, 0x0290);

	int_mdio_write(tp, 0, 27, 0xa012);
	int_mdio_write(tp, 0, 28, 0x0000);

	int_mdio_write(tp, 0, 27, 0xa014);
	int_mdio_write(tp, 0, 28, 0x2c04);
	int_mdio_write(tp, 0, 28, 0x2c06);
	int_mdio_write(tp, 0, 28, 0x2c09);
	int_mdio_write(tp, 0, 28, 0x2c0c);
	int_mdio_write(tp, 0, 28, 0xd093);
	int_mdio_write(tp, 0, 28, 0x2265);
	int_mdio_write(tp, 0, 28, 0x9e20);
	int_mdio_write(tp, 0, 28, 0xd703);
	int_mdio_write(tp, 0, 28, 0x2502);
	int_mdio_write(tp, 0, 28, 0x9e40);
	int_mdio_write(tp, 0, 28, 0xd700);
	int_mdio_write(tp, 0, 28, 0x0800);
	int_mdio_write(tp, 0, 28, 0x9e80);
	int_mdio_write(tp, 0, 28, 0xd70d);
	int_mdio_write(tp, 0, 28, 0x202e);

	int_mdio_write(tp, 0, 27, 0xa01a);
	int_mdio_write(tp, 0, 28, 0x0000);

	int_mdio_write(tp, 0, 27, 0xa006);
	int_mdio_write(tp, 0, 28, 0x002d);

	int_mdio_write(tp, 0, 27, 0xa004);
	int_mdio_write(tp, 0, 28, 0x0507);

	int_mdio_write(tp, 0, 27, 0xa002);
	int_mdio_write(tp, 0, 28, 0x0501);

	int_mdio_write(tp, 0, 27, 0xa000);
	int_mdio_write(tp, 0, 28, 0x1264);

	int_mdio_write(tp, 0, 27, 0xb820);
	int_mdio_write(tp, 0, 28, 0x0210);

	mdelay(10);

	/* Clear patch_key & patch_lock */
	int_mdio_write(tp, 0, 27, PATCH_KEY_ADDR);
	int_mdio_write(tp, 0, 28, 0);
	int_mdio_write(tp, 0x0b82, 23, 0);

	/* Release patch request & wait for the deasserting of patch_rdy. */
	int_mdio_write(tp, 0x0b82, 16,
		       int_mdio_read(tp, 0x0b82, 16) & ~BIT(4));

	tmp = 0;
	while ((int_mdio_read(tp, 0x0b80, 16) & BIT(6)) != 0) {
		tmp += 1;
		mdelay(1);
		if (tmp >= 100) {
			pr_err(PFX "GPHY patch_rdy timeout.\n");
			break;
		}
	}
	pr_info(PFX "wait %d ms for GPHY patch_rdy. reg = 0x%x\n",
		tmp, int_mdio_read(tp, 0x0b80, 16));

	pr_info(PFX "patch_rdy is de-asserted!!\n");
	pr_info(PFX "GPHY uC code patched.\n");
}

static void rtd16xx_mdio_init(struct rtl8169_private *tp)
{
	u32 tmp;
	u32 val;
	struct pinctrl *p_sgmii_mdio;
	struct pinctrl_state *ps_sgmii_mdio;
	void __iomem *ioaddr = tp->mmio_addr;

	/* ISO spec, ETN_PHY_INTR,
	 * wait interrupt from PHY and it means MDIO is ready
	 */
	tmp = 0;
	regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	while ((val & BIT(27)) == 0) {
		tmp += 1;
		mdelay(1);
		if (tmp >= 100) {
			pr_err(PFX "PHY PHY_Status timeout.\n");
			break;
		}
		regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	}
	pr_info(PFX "wait %d ms for PHY interrupt. UMSK_ISR = 0x%x\n", tmp, val);

	/* In ByPass mode,
	 * SW need to handle the EPHY Status check ,
	 * OTP data update and EPHY fuse_rdy setting.
	 */
	/* PHY will stay in state 1 mode */
	tmp = 0;
	while ((int_mdio_read(tp, 0x0a42, 16) & 0x07) != 0x1) {
		tmp += 1;
		mdelay(1);
		if (tmp >= 2000) {
			pr_err(PFX "PHY status is not 0x1 in bypass mode, current = 0x%02x\n",
			       int_mdio_read(tp, 0x0a42, 16) & 0x07);
			break;
		}
	}

	/* fill fuse_rdy & rg_ext_ini_done */
	int_mdio_write(tp, 0x0a46, 20,
		       int_mdio_read(tp, 0x0a46, 20) | (BIT(1) | BIT(0)));

	/* init_autoload_done = 1 */
	tmp = rtl_ocp_read(tp, 0xe004);
	tmp |= BIT(7);
	rtl_ocp_write(tp, 0xe004, tmp);

	/* ee_mode = 3 */
	RTL_W8(CFG9346, CFG9346_UNLOCK);

	/* wait LAN-ON */
	tmp = 0;
	do {
		tmp += 1;
		mdelay(1);
		if (tmp >= 2000) {
			pr_err(PFX "PHY status is not 0x3, current = 0x%02x\n",
			       int_mdio_read(tp, 0x0a42, 16) & 0x07);
			break;
		}
	} while ((int_mdio_read(tp, 0x0a42, 16) & 0x07) != 0x3);
	pr_info(PFX "wait %d ms for PHY ready, current = 0x%x\n",
		tmp, int_mdio_read(tp, 0x0a42, 16));

	/* adjust PHY SRAM table */
	rtd16xx_phy_sram_table(tp);

	/* GPHY patch code */
	rtd16xx_patch_gphy_uc_code(tp);

	/* adjust PHY electrical characteristics */
	rtd16xx_phy_iol_tuning(tp);

	/* load OTP contents (RC-K, R-K, Amp-K, and Bias-K)
	 * RC-K:        0x980174F8[27:24]
	 * R-K:         0x98017500[18:15]
	 * Amp-K:       0x980174FC[15:0]
	 * Bias-K:      0x980174FC[31:16]
	 */
	rtd16xx_load_otp_content(tp);

	if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
		/* Init PHY path */
		/* reg_0x9800705c[5] = 0 */
		/* reg_0x9800705c[7] = 0 */
		/* ISO spec, set internal MDIO to access PHY */
		regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
					(BIT(7) | BIT(5)), 0, NULL, false, true);

		/* reg_0x9800705c[4] = 0 */
		/* ISO spec, set data path to access PHY */
		regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
					BIT(4), 0, NULL, false, true);

		/* ETN spec, GMAC data path select MII-like(embedded GPHY),
		 * not SGMII(external PHY)
		 */
		tmp = rtl_ocp_read(tp, 0xea34) & ~(BIT(0) | BIT(1));
		tmp |= BIT(1);	/* MII */
		rtl_ocp_write(tp, 0xea34, tmp);
	} else {
		/* SGMII */
		/* ETN spec, adjust MDC freq=2.5MHz */
		tmp = rtl_ocp_read(tp, 0xDE30) & ~(BIT(7) | BIT(6));
		rtl_ocp_write(tp, 0xDE30, tmp);
		/* ETN spec, set external PHY addr */
		tmp = rtl_ocp_read(tp, 0xDE24) & ~(0x1f << 0);
		rtl_ocp_write(tp, 0xDE24, tmp | (tp->ext_phy_id & 0x1f));
		/* ISO mux spec, GPIO29 is set to MDC pin */
		/* ISO mux spec, GPIO46 is set to MDIO pin */
		p_sgmii_mdio = devm_pinctrl_get(&tp->pdev->dev);
		ps_sgmii_mdio = pinctrl_lookup_state(p_sgmii_mdio, "sgmii");
		pinctrl_select_state(p_sgmii_mdio, ps_sgmii_mdio);

		/* check if external PHY is available */
		pr_info(PFX "Searching external PHY...");
		tp->ext_phy = true;
		tmp = 0;
		while (ext_mdio_read(tp, 0x0a43, 31) != 0x0a43) {
			pr_info(".");
			tmp += 1;
			mdelay(1);
			if (tmp >= 2000) {
				pr_err("\n External SGMII PHY not found, current = 0x%02x\n",
				       ext_mdio_read(tp, 0x0a43, 31));
				return;
			}
		}
		if (tmp < 2000)
			pr_info("found.\n");

		/* lower SGMII TX swing of RTL8211FS to reduce EMI */
		/* TX swing = 470mV, default value */
		ext_mdio_write(tp, 0x0dcd, 16, 0x104e);

		tp->ext_phy = false;

		/* ETN spec, GMAC data path select SGMII(external PHY),
		 * not MII-like(embedded GPHY)
		 */
		tmp = rtl_ocp_read(tp, 0xea34) & ~(BIT(0) | BIT(1));
		tmp |= BIT(1) | BIT(0);	/* SGMII */
		rtl_ocp_write(tp, 0xea34, tmp);

		if (rtd16xx_serdes_init(tp) != 0)
			pr_err(PFX "SERDES init failed\n");
		/* ext_phy == true now */

		/* SDS spec, auto update SGMII link capability */
		regmap_update_bits_base(tp->sds_base, RTD16XX_SDS_DEBUG,
					BIT(2), BIT(2), NULL, false, true);

		/* enable 8b/10b symbol error
		 * even it is only valid in 1000Mbps
		 */
		ext_mdio_write(tp, 0x0dcf, 16,
			       (ext_mdio_read(tp, 0x0dcf, 16) &
				 ~(BIT(2) | BIT(1) | BIT(0))));
		ext_mdio_read(tp, 0x0dcf, 17);	/* dummy read */
	}
}

static void rtd16xx_eee_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		tp->chip->mdio_lock(tp);
		if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
			/* 1000M/100M EEE capability */
			rtl_mmd_write(tp, 0x7, 60, (BIT(2) | BIT(1)));
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) | (BIT(4) | BIT(2)));
			/* disable dynamic RX power in PHY */
			rtl_phy_write(tp, 0x0bd0, 21,
				      (rtl_phy_read(tp, 0x0bd0, 21) & ~BIT(8)) | BIT(9));
		} else {	/* SGMII */
			/* 1000M & 100M EEE capability */
			rtl_mmd_write(tp, 0x7, 60, (BIT(2) | BIT(1)));
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) | (BIT(4) | BIT(2)));
		}
		/* EEE MAC mode */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) | (BIT(1) | BIT(0)));
		/* EEE+ MAC mode */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) | BIT(1));
	} else {
		if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
			/* no EEE capability */
			rtl_mmd_write(tp, 0x7, 60, 0);
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) & ~(BIT(4) | BIT(2)));
		} else {	/* SGMII */
			/* no EEE capability */
			rtl_mmd_write(tp, 0x7, 60, 0);
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) & ~(BIT(4) | BIT(2)));
		}
		/* reset to default value */
		rtl_ocp_write(tp, 0xe040, rtl_ocp_read(tp, 0xe040) | BIT(13));
		/* EEE MAC mode */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) & ~(BIT(1) | BIT(0)));
		/* EEE+ MAC mode */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) & ~BIT(1));
	}
}

static void rtd16xx_led_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		regmap_update_bits_base(tp->pinctrl_base, RTD16XX_ISO_TESTMUX_MUXPAD0,
					GENMASK(29, 26), (5 << 26), NULL, false, true);
		regmap_update_bits_base(tp->pinctrl_base, RTD16XX_ISO_TESTMUX_MUXPAD2,
					GENMASK(29, 27), (3 << 27), NULL, false, true);
	} else {
		regmap_update_bits_base(tp->pinctrl_base, RTD16XX_ISO_TESTMUX_MUXPAD0,
					GENMASK(29, 26), 0, NULL, false, true);
		regmap_update_bits_base(tp->pinctrl_base, RTD16XX_ISO_TESTMUX_MUXPAD2,
					GENMASK(29, 27), 0, NULL, false, true);
	}
}

static void rtd16xx_dump_regs(struct seq_file *m, struct rtl8169_private *tp)
{
	u32 val;

	regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	seq_printf(m, "ISO_UMSK_ISR\t[0x98007004] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_PWRCUT_ETN, &val);
	seq_printf(m, "ISO_PWRCUT_ETN\t[0x9800705c] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_ETN_TESTIO, &val);
	seq_printf(m, "ISO_ETN_TESTIO\t[0x98007060] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_SOFT_RESET, &val);
	seq_printf(m, "ETN_RESET_CTRL\t[0x98007088] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_CLOCK_ENABLE, &val);
	seq_printf(m, "ETN_CLK_CTRL\t[0x9800708c] = %08x\n", val);
	if (tp->output_mode != OUTPUT_EMBEDDED_PHY) {
		regmap_read(tp->sds_base, RTD16XX_SDS_REG02, &val);
		seq_printf(m, "SDS_REG02\t[0x981c8008] = %08x\n", val);
		regmap_read(tp->sds_base, RTD16XX_SDS_MISC, &val);
		seq_printf(m, "SDS_MISC\t\t[0x981c9804] = %08x\n", val);
		regmap_read(tp->sds_base, RTD16XX_SDS_LINK, &val);
		seq_printf(m, "SDS_LINK\t\t[0x981c980c] = %08x\n", val);
		regmap_read(tp->sds_base, RTD16XX_SDS_DEBUG, &val);
		seq_printf(m, "SDS_DEBUG\t\t[0x981c9810] = %08x\n", val);
	}
}

static void rtd16xx_dump_var(struct seq_file *m, struct rtl8169_private *tp)
{
	seq_printf(m, "sgmii_swing\t%d\n", tp->sgmii.swing);
}

/******************* END of RTD16XX ****************************/

/* RTD13XX */
static void rtd13xx_mdio_lock(struct rtl8169_private *tp)
{
	rtd139x_mdio_lock(tp);
}

static void rtd13xx_mdio_unlock(struct rtl8169_private *tp)
{
	rtd139x_mdio_unlock(tp);
}

static void rtd13xx_reset_phy_gmac(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);

	/* reg_0x9800708c[12:11] = 00 */
	/* ISO spec, clock enable bit for etn clock & etn 250MHz */
	clk_disable_unprepare(clk_etn_sys);
	clk_disable_unprepare(clk_etn_250m);

	/* reg_0x98007088[10:9] = 00 */
	/* ISO spec, rstn_gphy & rstn_gmac */
	reset_control_assert(rstc_gphy);
	reset_control_assert(rstc_gmac);

	/* reg_0x98007060[1] = 1 */
	/* ISO spec, bypass mode enable */
	regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
				BIT(1), BIT(1), NULL, false, true);

	/* reg_0x9800705c[5] = 0 */
	/* ISO spec, default value and specify internal/external PHY ID as 1 */
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				BIT(5), 0, NULL, false, true);
	/* set int PHY addr */
	__int_set_phy_addr(tp, RTD13XX_INT_PHY_ADDR);
	/* set ext PHY addr */
	__ext_set_phy_addr(tp, RTD13XX_EXT_PHY_ADDR);

	mdelay(1);

	/* release resource */
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd13xx_acp_init(struct rtl8169_private *tp)
{
	u32 tmp;
	u32 val;

	/* SBX spec, Select ETN access DDR path. */
	if (tp->acp_enable) {
		/* reg_0x9801c20c[6] = 1 */
		/* SBX spec, Mask ETN_ALL to SB3 DBUS REQ */
		regmap_update_bits_base(tp->sbx_base, RTD13XX_SBX_SB3_CHANNEL_REQ_MASK,
					BIT(6), BIT(6), NULL, false, true);

		pr_info(PFX "wait all SB3 access finished...");
		tmp = 0;
		regmap_read(tp->sbx_base, RTD13XX_SBX_SB3_CHANNEL_REQ_BUSY, &val);
		while (val & BIT(6)) {
			mdelay(10);
			tmp += 10;
			if (tmp >= 100) {
				pr_err("\n wait SB3 access failed (wait %d ms)\n", tmp);
				break;
			}
			regmap_read(tp->sbx_base, RTD13XX_SBX_SB3_CHANNEL_REQ_BUSY, &val);
		}
		if (tmp < 100)
			pr_info("done.\n");

		/* reg_0x9801d100[29] = 0 */
		/* SCPU wrapper spec, CLKACP division, 0 = div 2, 1 = div 3 */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_CRT_CTRL,
					BIT(29), 0, NULL, false, true);

		/* reg_0x9801d124[1:0] = 00 */
		/* SCPU wrapper spec, ACP master active, 0 = active */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_INTERFACE_EN,
					GENMASK(1, 0), 0, NULL, false, true);

		/* reg_0x9801d100[30] = 1 */
		/* SCPU wrapper spec, dy_icg_en_acp */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_CRT_CTRL,
					BIT(30), BIT(30), NULL, false, true);

		/* reg_0x9801d100[21] = 1 */
		/* SCPU wrapper spec, ACP CLK enable */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_CRT_CTRL,
					BIT(21), BIT(21), NULL, false, true);

		/* reg_0x9801d100[14] = 1 */
		/* Do not apply reset to ACP port axi3 master */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_CRT_CTRL,
					BIT(14), BIT(14), NULL, false, true);

		/* reg_0x9801d800[3:0] = 0111 */
		/* reg_0x9801d800[7:4] = 0111 */
		/* reg_0x9801d800[9] = 1 */
		/* reg_0x9801d800[20:16] = 01100 */
		/* reg_0x9801d800[28:24] = 01110 */
		/* Configure control of ACP port */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_ACP_CTRL,
					GENMASK(28, 24) | GENMASK(20, 16) |
					BIT(9) | GENMASK(7, 4) | GENMASK(3, 0),
					(0x0e << 24) | (0x0c << 16) | BIT(9) |
					(0x7 << 4) | (0x7 << 0),
					NULL, false, true);

		/* reg_0x9801d030[28] = 1 */
		/* SCPU wrapper spec, dy_icg_en_acp */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_ACP_CRT_CTRL,
					BIT(28), BIT(28), NULL, false, true);

		/* reg_0x9801d030[16] = 1 */
		/* ACP CLK Enable for acp of scpu_chip_top */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_ACP_CRT_CTRL,
					BIT(16), BIT(16), NULL, false, true);

		/* reg_0x9801d030[0] = 1 */
		/* Do not apply reset to ACP port axi3 master */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_ACP_CRT_CTRL,
					BIT(0), BIT(0), NULL, false, true);

		/* reg_0x9801c814[17] = 1 */
		/* through ACP to SCPU_ACP */
		regmap_update_bits_base(tp->sbx_base, RTD13XX_SBX_ACP_MISC_CTRL,
					BIT(17), BIT(17), NULL, false, true);

		/* SBX spec, Remove mask ETN_ALL to ACP DBUS REQ */
		regmap_update_bits_base(tp->sbx_base, RTD13XX_SBX_ACP_CHANNEL_REQ_MASK,
					BIT(1), 0, NULL, false, true);

		pr_info(PFX "ARM ACP on\n.");
	} else {
		/* SBX spec, Mask ETN_ALL to ACP DBUS REQ */
		regmap_update_bits_base(tp->sbx_base, RTD13XX_SBX_ACP_CHANNEL_REQ_MASK,
					BIT(1), BIT(1), NULL, false, true);

		pr_info(PFX "wait all ACP access finished...");
		tmp = 0;
		regmap_read(tp->sbx_base, RTD13XX_SBX_ACP_CHANNEL_REQ_BUSY, &val);
		while (val & BIT(1)) {
			mdelay(1);
			tmp += 1;
			if (tmp >= 100) {
				pr_err("\n ACP channel is still busy (wait %d ms)\n", tmp);
				break;
			}
			regmap_read(tp->sbx_base, RTD13XX_SBX_ACP_CHANNEL_REQ_BUSY, &val);
		}
		if (tmp < 100)
			pr_info("done.\n");

		/* SCPU wrapper spec, Inactive MP4 AINACTS signal */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_INTERFACE_EN,
					GENMASK(1, 0), GENMASK(1, 0), NULL, false, true);

		/* SCPU wrapper spec, nACPRESET_DVFS & CLKENACP_DVFS */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_CRT_CTRL,
					(BIT(21) | BIT(14)), 0, NULL, false, true);

		/* SCPU wrapper spec, nACPRESET & CLKENACP */
		regmap_update_bits_base(tp->scpu_wrap_base, RTD13XX_SC_WRAP_ACP_CRT_CTRL,
					(BIT(16) | BIT(0)), 0, NULL, false, true);

		/* reg_0x9801c814[17] = 0 */
		/* SBX spec, Switch ETN_ALL to DC_SYS path */
		regmap_update_bits_base(tp->sbx_base, RTD13XX_SBX_ACP_MISC_CTRL,
					BIT(17), 0, NULL, false, true);

		/* SBX spec, Remove mask ETN_ALL to SB3 DBUS REQ */
		regmap_update_bits_base(tp->sbx_base, RTD13XX_SBX_SB3_CHANNEL_REQ_MASK,
					BIT(6), 0, NULL, false, true);

		pr_info(PFX "ARM ACP off\n.");
	}
}

static void rtd13xx_pll_clock_init(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	/* reg_0x98007004[27] = 1 */
	/* ISO spec, ETN_PHY_INTR, clear ETN interrupt for ByPassMode */
	regmap_write(tp->iso_base, ISO_UMSK_ISR, BIT(27));

	/* reg_0x98007088[10] = 1 */
	/* ISO spec, reset bit of fephy */
	reset_control_deassert(rstc_gphy);

	mdelay(1);	/* wait 1ms for PHY PLL stable */

	/* reg_0x98007060[1] = 1 */
	/* ISO spec, bypass mode enable */
	regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
				BIT(1), BIT(1), NULL, false, true);

	/* reg_0x98007088[9] = 1 */
	/* ISO spec, reset bit of gmac */
	reset_control_deassert(rstc_gmac);

	/* reg_0x9800708c[12:11] = 11 */
	/* ISO spec, clock enable bit for etn clock & etn 250MHz */
	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);

	mdelay(10);	/* wait 10ms for GMAC uC to be stable */

	/* release resource */
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd13xx_load_otp_content(struct rtl8169_private *tp)
{
	u32 otp;
	u16 tmp;
	u8 *buf;

	buf = r8169soc_read_otp(tp, "para");
	if (IS_ERR(buf))
		goto set_idac;

	/* enable 0x980174FC[4] */
	/* R-K 0x980174FC[3:0] */
	otp = *buf;
	if (otp & BIT(4)) {
		tmp = otp ^ RTD13XX_R_K_DEFAULT;
		int_mdio_write(tp, 0x0bc0, 20,
			       (int_mdio_read(tp, 0x0bc0, 20) & ~(0x1f << 0)) | tmp);
	}

	kfree(buf);

set_idac:

	buf = r8169soc_read_otp(tp, "idac");
	if (IS_ERR(buf))
		return;

	/* Amp-K 0x98017500[7:0] */
	otp = *buf;
	tmp = otp ^ RTD13XX_IDAC_FINE_DEFAULT;
	int_mdio_write(tp, 0x0bc0, 23,
		       (int_mdio_read(tp, 0x0bc0, 23) & ~(0xff << 0)) | tmp);

	kfree(buf);
}

static void rtd13xx_phy_iol_tuning(struct rtl8169_private *tp)
{
	u16 tmp;

	/* rs_offset=rsw_offset=0xc */
	tmp = 0xcc << 8;
	int_mdio_write(tp, 0x0bc0, 20,
		       (int_mdio_read(tp, 0x0bc0, 20) & ~(0xff << 8)) | tmp);

	/* 100M Tr/Tf */
	/* reg_cf_l=0x2 */
	tmp = 0x2 << 11;
	int_mdio_write(tp, 0x0bd0, 17,
		       (int_mdio_read(tp, 0x0bd0, 17) & ~(0x7 << 11)) | tmp);

	/* 100M Swing */
	/* idac_fine_mdix, idac_fine_mdi */
	tmp = 0x55 << 0;
	int_mdio_write(tp, 0x0bc0, 23,
		       (int_mdio_read(tp, 0x0bc0, 23) & ~(0xff << 0)) | tmp);
}

static void rtd13xx_mdio_init(struct rtl8169_private *tp)
{
	u32 tmp;
	u32 val;
	struct pinctrl *p_rgmii_mdio;
	struct pinctrl_state *ps_rgmii_mdio;
	void __iomem *ioaddr = tp->mmio_addr;
	const struct soc_device_attribute rtk_soc_rtd13xx_a00[] = {
		{
			.family = "Realtek Hank",
			.revision = "A00",
		},
		{
		/* empty */
		}
	};

	/* ETN_PHY_INTR, wait interrupt from PHY and it means MDIO is ready */
	tmp = 0;
	regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	while ((val & BIT(27)) == 0) {
		tmp += 1;
		mdelay(1);
		if (tmp >= 100) {
			pr_err(PFX "PHY PHY_Status timeout.\n");
			break;
		}
		regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	}
	pr_info(PFX "wait %d ms for PHY interrupt. UMSK_ISR = 0x%x\n", tmp, val);

	/* In ByPass mode,
	 * SW need to handle the EPHY Status check ,
	 * OTP data update and EPHY fuse_rdy setting.
	 */
	/* PHY will stay in state 1 mode */
	tmp = 0;
	while (0x1 != (int_mdio_read(tp, 0x0a42, 16) & 0x07)) {
		tmp += 1;
		mdelay(1);
		if (tmp >= 2000) {
			pr_err(PFX "PHY status is not 0x1 in bypass mode, current = 0x%02x\n",
			       (int_mdio_read(tp, 0x0a42, 16) & 0x07));
			break;
		}
	}

	/* fix A00 known issue */
	if (soc_device_match(rtk_soc_rtd13xx_a00)) {
		/* fix AFE RX power as 1 */
		int_mdio_write(tp, 0x0bd0, 21, 0x7201);
	}

	/* ETN spec. set MDC freq = 8.9MHZ */
	tmp = rtl_ocp_read(tp, 0xde10) & ~(BIT(7) | BIT(6));
	tmp |= BIT(6); /* MDC freq = 8.9MHz */
	rtl_ocp_write(tp, 0xde10, tmp);

	/* fill fuse_rdy & rg_ext_ini_done */
	int_mdio_write(tp, 0x0a46, 20,
		       int_mdio_read(tp, 0x0a46, 20) | (BIT(1) | BIT(0)));

	/* init_autoload_done = 1 */
	tmp = rtl_ocp_read(tp, 0xe004);
	tmp |= BIT(7);
	rtl_ocp_write(tp, 0xe004, tmp);

	/* ee_mode = 3 */
	RTL_W8(CFG9346, CFG9346_UNLOCK);

	/* wait LAN-ON */
	tmp = 0;
	do {
		tmp += 1;
		mdelay(1);
		if (tmp >= 2000) {
			pr_err(PFX "PHY status is not 0x3, current = 0x%02x\n",
			       (int_mdio_read(tp, 0x0a42, 16) & 0x07));
			break;
		}
	} while (0x3 != (int_mdio_read(tp, 0x0a42, 16) & 0x07));
	pr_info(PFX "wait %d ms for PHY ready, current = 0x%x\n",
		tmp, int_mdio_read(tp, 0x0a42, 16));

	/* adjust PHY electrical characteristics */
	rtd13xx_phy_iol_tuning(tp);

	/* load OTP contents (R-K, Amp)
	 * R-K:		0x980174FC[4:0]
	 * Amp:		0x98017500[7:0]
	 */
	rtd13xx_load_otp_content(tp);

	switch (tp->output_mode) {
	case OUTPUT_EMBEDDED_PHY:
		/* ISO spec, set data path to FEPHY */
		tmp = rtl_ocp_read(tp, 0xea30);
		tmp &= ~(BIT(0));
		rtl_ocp_write(tp, 0xea30, tmp);

		tmp = rtl_ocp_read(tp, 0xea34);
		tmp &= ~(BIT(1) | BIT(0));
		tmp |= BIT(1);  /* FEPHY */
		rtl_ocp_write(tp, 0xea34, tmp);

		/* LED high active circuit */
		/* output current: 4mA */
		regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC9,
					GENMASK(23, 16), 0, NULL, false, true);
		break;
	case OUTPUT_RGMII_TO_MAC:
		pr_err(PFX "%s:%d: FIXME! PHY_TYPE_RGMII_MAC\n", __func__, __LINE__);
		break;
	case OUTPUT_RMII:
		pr_err(PFX "%s:%d: FIXME! PHY_TYPE_RMII\n", __func__, __LINE__);
		break;
	case OUTPUT_RGMII_TO_PHY:
		/* ISO mux spec, GPIO15 is set to MDC pin */
		/* ISO mux spec, GPIO14 is set to MDIO pin */
		/* ISO mux spec, GPIO65~76 is set to TX/RX pin */
		p_rgmii_mdio = devm_pinctrl_get(&tp->pdev->dev);
		ps_rgmii_mdio = pinctrl_lookup_state(p_rgmii_mdio, "rgmii");
		pinctrl_select_state(p_rgmii_mdio, ps_rgmii_mdio);

		/* no Schmitt_Trigger */
		switch (tp->rgmii.voltage) {
		case 3: /* 3.3v */
			/* MODE2=1, MODE=0, SR=1, smt=0, pudsel=0, puden=0,
			 * E2=0
			 */
			/* GPIO[70:65] */
			regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC20,
						GENMASK(27, 4), 0, NULL, false, true);

			/* GPIO[76:71] */
			regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC21,
						GENMASK(31, 0),
						(0x2 << 30) | (0x10 << 25) | (0x10 << 20) |
						 (0x10 << 15) | (0x10 << 10) | (0x10 << 5) |
						 (0x10 << 0),
						NULL, false, true);

			/* DP=000, DN=111 */
			regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC25,
						GENMASK(5, 0),
						(0x0 << 3) | (0x7 << 0),
						NULL, false, true);
			break;
		case 2: /* 2.5v */
			/* MODE2=0, MODE=1, SR=0, smt=0, pudsel=0, puden=0,
			 * E2=1
			 */
			/* GPIO[70:65] */
			regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC20,
						GENMASK(27, 4),
						(0x1 << 24) | (0x1 << 20) | (0x1 << 16) |
						 (0x1 << 12) | (0x1 << 8) | (0x1 << 4),
						NULL, false, true);

			/* GPIO[76:71] */
			regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC21,
						GENMASK(31, 0),
						(0x1 << 30) | (0x1 << 25) | (0x1 << 20) |
						 (0x1 << 15) | (0x1 << 10) | (0x1 << 5) |
						 (0x1 << 0),
						NULL, false, true);

			/* DP=000, DN=111 */
			regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC25,
						GENMASK(5, 0),
						(0x0 << 3) | (0x7 << 0),
						NULL, false, true);
			break;
		case 1: /* 1.8v */
		default:
			/* MODE2=0, MODE=0, SR=0, smt=0, pudsel=0, puden=0,
			 * E2=1
			 */
			/* GPIO[70:65] */
			regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC20,
						GENMASK(27, 4),
						(0x1 << 24) | (0x1 << 20) | (0x1 << 16) |
						 (0x1 << 12) | (0x1 << 8) | (0x1 << 4),
						NULL, false, true);

			/* GPIO[76:71] */
			regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC21,
						GENMASK(31, 0),
						(0x0 << 30) | (0x1 << 25) | (0x1 << 20) |
						 (0x1 << 15) | (0x1 << 10) | (0x1 << 5) |
						 (0x1 << 0),
						NULL, false, true);

			/* DP=001, DN=110 */
			regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_PFUNC25,
						GENMASK(5, 0),
						(0x1 << 3) | (0x6 << 0),
						NULL, false, true);
		}

		/* check if external PHY is available */
		pr_info(PFX "Searching external PHY...");
		tp->ext_phy = true;
		tmp = 0;
		while (ext_mdio_read(tp, 0x0a43, 31) != 0x0a43) {
			pr_info(".");
			tmp += 1;
			mdelay(1);
			if (tmp >= 2000) {
				pr_err("\n External RGMII PHY not found, current = 0x%02x\n",
				       ext_mdio_read(tp, 0x0a43, 31));
				return;
			}
		}
		if (tmp < 2000)
			pr_info("found.\n");

		/* set RGMII RX delay */
		tmp = rtl_ocp_read(tp, 0xea34) &
			~(BIT(14) | BIT(9) | BIT(8) | BIT(6));
		switch (tp->rgmii.rx_delay) {
		case 1:
			tmp |= (0x0 << 6) | (0x1 << 8);
			break;
		case 2:
			tmp |= (0x0 << 6) | (0x2 << 8);
			break;
		case 3:
			tmp |= (0x0 << 6) | (0x3 << 8);
			break;
		case 4:
			tmp |= (0x1 << 6) | (0x0 << 8);
			break;
		case 5:
			tmp |= (0x1 << 6) | (0x1 << 8);
			break;
		case 6:
			tmp |= (0x1 << 6) | (0x2 << 8);
			break;
		case 7:
			tmp |= (0x1 << 6) | (0x3 << 8);
		}
		rtl_ocp_write(tp, 0xea34, tmp);

		tmp = rtl_ocp_read(tp, 0xea36) & ~(BIT(0)); /* rg_clk_en */
		rtl_ocp_write(tp, 0xea36, tmp);

		/* external PHY RGMII timing tuning,
		 * rg_rgmii_id_mode = 1 (default)
		 */
		tmp = ext_mdio_read(tp, 0x0d08, 21);
		switch (tp->rgmii.rx_delay) {
		case 0:
		case 1:
		case 2:
		case 3:
			tmp |= BIT(3);
			break;
		case 4:
		case 5:
		case 6:
		case 7:
			tmp &= ~BIT(3);
		}
		ext_mdio_write(tp, 0x0d08, 21, tmp);

		/* set RGMII TX delay */
		if (tp->rgmii.tx_delay == 0) {
			tmp = rtl_ocp_read(tp, 0xea34) & ~(BIT(7));
			rtl_ocp_write(tp, 0xea34, tmp);

			/* external PHY RGMII timing tuning, tx_dly_mode = 1 */
			ext_mdio_write(tp, 0x0d08, 17,
				       ext_mdio_read(tp, 0x0d08, 17) | BIT(8));
		} else {	/* tp->tx_delay == 1 (2ns) */
			tmp = rtl_ocp_read(tp, 0xea34) | (BIT(7));
			rtl_ocp_write(tp, 0xea34, tmp);

			/* external PHY RGMII timing tuning, tx_dly_mode = 0 */
			ext_mdio_write(tp, 0x0d08, 17,
				       ext_mdio_read(tp, 0x0d08, 17) & ~BIT(8));
		}

		/* ISO spec, data path is set to RGMII */
		tmp = rtl_ocp_read(tp, 0xea30) & ~(BIT(0));
		rtl_ocp_write(tp, 0xea30, tmp);

		tmp = rtl_ocp_read(tp, 0xea34) & ~(BIT(0) | BIT(1));
		tmp |= BIT(0); /* RGMII */
		rtl_ocp_write(tp, 0xea34, tmp);

		/* ext_phy == true now */

		break;
	default:
		pr_err(PFX "%s:%d: unsupport output mode (%d) in FPGA\n",
		       __func__, __LINE__, tp->output_mode);
	}
}

static void rtd13xx_eee_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		tp->chip->mdio_lock(tp);
		if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
			/* enable eee auto fallback function */
			rtl_phy_write(tp, 0x0a4b, 17,
				      rtl_phy_read(tp, 0x0a4b, 17) | BIT(2));
			/* 1000M/100M EEE capability */
			rtl_mmd_write(tp, 0x7, 60, (BIT(2) | BIT(1)));
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) | BIT(4) | BIT(2));
			/* keep RXC in LPI mode */
			rtl_mmd_write(tp, 0x3, 0,
				      rtl_mmd_read(tp, 0x3, 0) & ~BIT(10));
		} else { /* RGMII */
			/* enable eee auto fallback function */
			rtl_phy_write(tp, 0x0a4b, 17,
				      rtl_phy_read(tp, 0x0a4b, 17) | BIT(2));
			/* 1000M & 100M EEE capability */
			rtl_mmd_write(tp, 0x7, 60, (BIT(2) | BIT(1)));
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) | BIT(4) | BIT(2));
			/* stop RXC in LPI mode */
			rtl_mmd_write(tp, 0x3, 0,
				      rtl_mmd_read(tp, 0x3, 0) | BIT(10));
		}
		/* EEE Tw_sys_tx timing adjustment,
		 * make sure MAC would send data after FEPHY wake up
		 */
		/* default 0x001F, 100M EEE */
		rtl_ocp_write(tp, 0xe04a, 0x002f);
		/* default 0x0011, 1000M EEE */
		rtl_ocp_write(tp, 0xe04c, 0x001a);
		/* EEEP Tw_sys_tx timing adjustment,
		 * make sure MAC would send data after FEPHY wake up
		 */
		 /* default 0x3f, 10M EEEP */
		if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
			/* FEPHY needs 160us */
			rtl_ocp_write(tp, 0xe08a, 0x00a8);
		} else {	/* RGMII, GPHY needs 25us */
			rtl_ocp_write(tp, 0xe08a, 0x0020);
		}
		/* default 0x0005, 100M/1000M EEEP */
		rtl_ocp_write(tp, 0xe08c, 0x0008);
		/* EEE MAC mode */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) | BIT(1) | BIT(0));
		/* EEE+ MAC mode */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) | BIT(1));
	} else {
		tp->chip->mdio_lock(tp);
		if (tp->output_mode == OUTPUT_EMBEDDED_PHY) {
			/* disable eee auto fallback function */
			rtl_phy_write(tp, 0x0a4b, 17,
				      rtl_phy_read(tp, 0x0a4b, 17) & ~BIT(2));
			/* 100M & 1000M EEE capability */
			rtl_mmd_write(tp, 0x7, 60, 0);
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) & ~(BIT(4) | BIT(2)));
			/* keep RXC in LPI mode */
			rtl_mmd_write(tp, 0x3, 0,
				      rtl_mmd_read(tp, 0x3, 0) & ~BIT(10));
		} else { /* RGMII */
			/* disable eee auto fallback function */
			rtl_phy_write(tp, 0x0a4b, 17,
				      rtl_phy_read(tp, 0x0a4b, 17) & ~BIT(2));
			/* 100M & 1000M EEE capability */
			rtl_mmd_write(tp, 0x7, 60, 0);
			/* 10M EEE */
			rtl_phy_write(tp, 0x0a43, 25,
				      rtl_phy_read(tp, 0x0a43, 25) & ~(BIT(4) | BIT(2)));
			/* keep RXC in LPI mode */
			rtl_mmd_write(tp, 0x3, 0,
				      rtl_mmd_read(tp, 0x3, 0) & ~BIT(10));
		}
		/* reset to default value */
		rtl_ocp_write(tp, 0xe040, rtl_ocp_read(tp, 0xe040) | BIT(13));
		/* EEE MAC mode */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) & ~(BIT(1) | BIT(0)));
		/* EEE+ MAC mode */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) & ~BIT(1));
		/* EEE Tw_sys_tx timing restore */
		/* default 0x001F, 100M EEE */
		rtl_ocp_write(tp, 0xe04a, 0x001f);
		/* default 0x0011, 1000M EEE */
		rtl_ocp_write(tp, 0xe04c, 0x0011);
		/* EEEP Tw_sys_tx timing restore */
		/* default 0x3f, 10M EEEP */
		rtl_ocp_write(tp, 0xe08a, 0x003f);
		/* default 0x0005, 100M/1000M EEEP */
		rtl_ocp_write(tp, 0xe08c, 0x0005);
	}
}

static void rtd13xx_led_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_MUXPAD2,
					GENMASK(9, 4), (9 << 4), NULL, false, true);
	} else {
		regmap_update_bits_base(tp->pinctrl_base, RTD13XX_ISO_TESTMUX_MUXPAD2,
					GENMASK(9, 4), 0, NULL, false, true);
	}
}

static void rtd13xx_dump_regs(struct seq_file *m, struct rtl8169_private *tp)
{
	u32 val;

	regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	seq_printf(m, "ISO_UMSK_ISR\t[0x98007004] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_PWRCUT_ETN, &val);
	seq_printf(m, "ISO_PWRCUT_ETN\t[0x9800705c] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_ETN_TESTIO, &val);
	seq_printf(m, "ISO_ETN_TESTIO\t[0x98007060] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_SOFT_RESET, &val);
	seq_printf(m, "ETN_RESET_CTRL\t[0x98007088] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_CLOCK_ENABLE, &val);
	seq_printf(m, "ETN_CLK_CTRL\t[0x9800708c] = %08x\n", val);
}

static void rtd13xx_dump_var(struct seq_file *m, struct rtl8169_private *tp)
{
	seq_printf(m, "voltage \t%d\n", tp->rgmii.voltage);
	seq_printf(m, "tx_delay\t%d\n", tp->rgmii.tx_delay);
	seq_printf(m, "rx_delay\t%d\n", tp->rgmii.rx_delay);
}

/******************* END of RTD13XX ****************************/

/* RTD16XXB */
static void rtd16xxb_mdio_lock(struct rtl8169_private *tp)
{
	rtd139x_mdio_lock(tp);
}

static void rtd16xxb_mdio_unlock(struct rtl8169_private *tp)
{
	rtd139x_mdio_unlock(tp);
}

static void rtd16xxb_reset_phy_gmac(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");

	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);

	/* reg_0x9800708c[12:11] = 00 */
	/* ISO spec, clock enable bit for etn clock & etn 250MHz */
	clk_disable_unprepare(clk_etn_sys);
	clk_disable_unprepare(clk_etn_250m);

	/* reg_0x98007088[10:9] = 00 */
	/* ISO spec, rstn_gphy & rstn_gmac */
	reset_control_assert(rstc_gphy);
	reset_control_assert(rstc_gmac);

	/* reg_0x98007060[1] = 1 */
	/* ISO spec, bypass mode enable */
	regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
				BIT(1), BIT(1), NULL, false, true);

	/* reg_0x9800705c[5] = 0 */
	/* ISO spec, default value and specify internal/external PHY ID as 1 */
	regmap_update_bits_base(tp->iso_base, ISO_PWRCUT_ETN,
				BIT(5), 0, NULL, false, true);
	/* set int PHY addr */
	__int_set_phy_addr(tp, RTD16XXB_INT_PHY_ADDR);
	/* set ext PHY addr */
	__ext_set_phy_addr(tp, RTD16XXB_EXT_PHY_ADDR);

	mdelay(1);

	/* release resource */
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd16xxb_pll_clock_init(struct rtl8169_private *tp)
{
	struct clk *clk_etn_sys  = clk_get(&tp->pdev->dev, "etn_sys");
	struct clk *clk_etn_250m = clk_get(&tp->pdev->dev, "etn_250m");
	struct reset_control *rstc_gphy =
		reset_control_get_exclusive(&tp->pdev->dev, "gphy");
	struct reset_control *rstc_gmac =
		reset_control_get_exclusive(&tp->pdev->dev, "gmac");
	u32 tmp;
	u32 val;

	/* reg_0x98007004[27] = 1 */
	/* ISO spec, ETN_PHY_INTR, clear ETN interrupt for ByPassMode */
	regmap_write(tp->iso_base, ISO_UMSK_ISR, BIT(27));

	/* reg_0x98007088[10] = 1 */
	/* ISO spec, reset bit of fephy */
	reset_control_deassert(rstc_gphy);

	mdelay(1);	/* wait 1ms for PHY PLL stable */

	/* reg_0x98007060[1] = 1 */
	/* ISO spec, bypass mode enable */
	regmap_update_bits_base(tp->iso_base, ISO_ETN_TESTIO,
				BIT(1), BIT(1), NULL, false, true);

	/* reg_0x98007088[9] = 1 */
	/* ISO spec, reset bit of gmac */
	reset_control_deassert(rstc_gmac);

	/* reg_0x9800708c[12:11] = 11 */
	/* ISO spec, clock enable bit for etn clock & etn 250MHz */
	clk_prepare_enable(clk_etn_sys);
	clk_prepare_enable(clk_etn_250m);

	/* Check ETN MAC init_autoload_done reg_0x98007070[0] = 1 */
	/* Bit 0 changes to 1 means ETN Rbus is valid */
	regmap_read(tp->iso_base, ISO_PLL_WDOUT, &val);
	tmp = 0;
	while (0x1 != (val & BIT(0))) {
		tmp += 1;
		mdelay(1);
		if (tmp >= MAC_INIT_TIMEOUT) {
			pr_err("GMAC init timeout\n");
			break;
		}
		regmap_read(tp->iso_base, ISO_PLL_WDOUT, &val);
	}
	if (tmp < MAC_INIT_TIMEOUT)
		pr_info("GMAC wait %d ms for init_autoload_done\n", tmp);

	/* release resource */
	reset_control_put(rstc_gphy);
	reset_control_put(rstc_gmac);
}

static void rtd16xxb_load_otp_content(struct rtl8169_private *tp)
{
	u32 otp;
	u16 tmp;
	u8 *buf;

	/* R-K		0x9803_2504[3:0]  : 4 bits, common setting
	 * Amp-K	0x9803_2504[31:16]: total 16 bits, 4 bits/channel
	 * RC-K		0x9803_2508[15:0] : total 16 bits, 4 bits/channel
	 */

	buf = r8169soc_read_otp(tp, "rc_r_amp_cal");
	if (IS_ERR(buf))
		return;

	/* R-K 0x9803_2504[3:0] */
	otp = *buf & 0xF;
	tmp = otp ^ RTD16XXB_R_K_DEFAULT;
	/* Set tapbin_p3 & tapbin_p2 */
	int_mdio_write(tp, 0x0bcf, 18, ((tmp << 8) | tmp));
	/* Set tapbin_p1 & tapbin_p0 */
	int_mdio_write(tp, 0x0bcf, 19, ((tmp << 8) | tmp));
	/* Set tapbin_pm_p3 & tapbin_pm_p2 */
	int_mdio_write(tp, 0x0bcf, 20, ((tmp << 8) | tmp));
	/* Set tapbin_pm_p1 & tapbin_pm_p0 */
	int_mdio_write(tp, 0x0bcf, 21, ((tmp << 8) | tmp));

	/* Amp-K 0x9803_2504[31:16] */
	otp = buf[2] | (buf[3] << 8);
	tmp = otp ^ RTD16XXB_AMP_K_DEFAULT;
	int_mdio_write(tp, 0x0bca, 22, tmp);

	/* RC-K 0x9803_2508[15:0] */
	otp = buf[4] | (buf[5] << 8);
	tmp = otp ^ RTD16XXB_RC_K_DEFAULT;
	int_mdio_write(tp, 0x0bcd, 22, tmp); /* len_p[3:0] */
	int_mdio_write(tp, 0x0bcd, 23, tmp); /* rlen_p[3:0] */

	kfree(buf);
}

static void rtd16xxb_phy_iol_tuning(struct rtl8169_private *tp)
{
	u32 otp;
	u8 *buf;
	u16 tmp;

	/* for common mode voltage */
	tmp = 0xb4 << 4;
	int_mdio_write(tp, 0x0bc0, 17,
		       (int_mdio_read(tp, 0x0bc0, 17) & ~(0xff << 4)) | tmp);

       /* Set LD_COMP to 0x0 */
	tmp = 0 << 9;
	int_mdio_write(tp, 0x0bc0, 23,
		       (int_mdio_read(tp, 0x0bc0, 23) & ~(0x3 << 9)) | tmp);

	/* set lock main */
	tmp = 0x1 << 1;
	int_mdio_write(tp, 0x0a46, 21,
		       int_mdio_read(tp, 0x0a46, 21) | tmp);
	/* wait until locked */
	tmp = 0;
	while (0x1 != (int_mdio_read(tp, 0x0a60, 16) & (0xff << 0))) {
		tmp += 10;
		usleep_range(10, 11);
		if (tmp >= PHY_LOCK_TIMEOUT) {
			pr_err("Ethernet PHY is not locked (0x1), current = 0x%02x\n",
			       (int_mdio_read(tp, 0x0a60, 16) & (0xff << 0)));
			break;
		}
	}
	if (tmp < PHY_LOCK_TIMEOUT)
		pr_info("Ethernet PHY: wait %d ns to start IOL tuning\n", tmp);

	/* Change green table default LDVBIAS to 0x66 */
	tmp = 0x66 << 8;
	int_mdio_write(tp, 0x0a43, 27, 0x8049);
	int_mdio_write(tp, 0x0a43, 28,
		       (int_mdio_read(tp, 0x0a43, 28) & ~(0xff << 8)) | tmp);

	/* Change green table 10M LDVBIAS to 0x66 */
	int_mdio_write(tp, 0x0a43, 27, 0x8050);
	int_mdio_write(tp, 0x0a43, 28,
		       (int_mdio_read(tp, 0x0a43, 28) & ~(0xff << 8)) | tmp);

	/* Change green table 100M short LDVBIAS to 0x66 */
	int_mdio_write(tp, 0x0a43, 27, 0x8057);
	int_mdio_write(tp, 0x0a43, 28,
		       (int_mdio_read(tp, 0x0a43, 28) & ~(0xff << 8)) | tmp);

	/* Change green table 100M long LDVBIAS to 0x66 */
	int_mdio_write(tp, 0x0a43, 27, 0x805e);
	int_mdio_write(tp, 0x0a43, 28,
		       (int_mdio_read(tp, 0x0a43, 28) & ~(0xff << 8)) | tmp);

	/* Change green table GIGA short LDVBIAS to 0x66 */
	int_mdio_write(tp, 0x0a43, 27, 0x8065);
	int_mdio_write(tp, 0x0a43, 28,
		       (int_mdio_read(tp, 0x0a43, 28) & ~(0xff << 8)) | tmp);

	/* Change green table GIGA middle LDVBIAS to 0x66 */
	int_mdio_write(tp, 0x0a43, 27, 0x806c);
	int_mdio_write(tp, 0x0a43, 28,
		       (int_mdio_read(tp, 0x0a43, 28) & ~(0xff << 8)) | tmp);

	/* Change green table GIGA long LDVBIAS to 0x66 */
	int_mdio_write(tp, 0x0a43, 27, 0x8073);
	int_mdio_write(tp, 0x0a43, 28,
		       (int_mdio_read(tp, 0x0a43, 28) & ~(0xff << 8)) | tmp);

	/* Use OTP 0x98032508 bit[17:16] to identify amp-k pattern version */
	/* b'00: original amp-k pattern */
	/* b'01: new amp-k pattern (rg_cal_itune = 0, LDVDC = 1, LD_CMFB = 2).
	 * Change LD_CMFB to 0x2 and LDVDC to 0x5 in normal mode.
	 */
	buf = r8169soc_read_otp(tp, "rc_r_amp_cal");
	if (IS_ERR(buf))
		goto out;

	otp = buf[6];
	kfree(buf);

	if ((otp & 0x3) == 0x1) {
		/* Change LD_CMFB to 0x2 */
		tmp = 0x2 << 12;
		int_mdio_write(tp, 0x0bc0, 23,
			       (int_mdio_read(tp, 0x0bc0, 23) & ~(0x3 << 12)) | tmp);

		/* Change green table default LDVDC to 0x5 */
		tmp = 0x5 << 8;
		int_mdio_write(tp, 0x0a43, 27, 0x804d);
		int_mdio_write(tp, 0x0a43, 28,
			       (int_mdio_read(tp, 0x0a43, 28) & ~(0x7 << 8)) | tmp);

		/* Change green table 10M LDVDC to 0x5 */
		int_mdio_write(tp, 0x0a43, 27, 0x8054);
		int_mdio_write(tp, 0x0a43, 28,
			       (int_mdio_read(tp, 0x0a43, 28) & ~(0x7 << 8)) | tmp);

		/* Change green table 100M short LDVDC to 0x5 */
		int_mdio_write(tp, 0x0a43, 27, 0x805b);
		int_mdio_write(tp, 0x0a43, 28,
			       (int_mdio_read(tp, 0x0a43, 28) & ~(0x7 << 8)) | tmp);

		/* Change green table 100M long LDVDC to 0x5 */
		int_mdio_write(tp, 0x0a43, 27, 0x8062);
		int_mdio_write(tp, 0x0a43, 28,
			       (int_mdio_read(tp, 0x0a43, 28) & ~(0x7 << 8)) | tmp);

		/* Change green table Giga short LDVDC to 0x5 */
		int_mdio_write(tp, 0x0a43, 27, 0x8069);
		int_mdio_write(tp, 0x0a43, 28,
			       (int_mdio_read(tp, 0x0a43, 28) & ~(0x7 << 8)) | tmp);

		/* Change green table Giga middle LDVDC to 0x5 */
		int_mdio_write(tp, 0x0a43, 27, 0x8070);
		int_mdio_write(tp, 0x0a43, 28,
			       (int_mdio_read(tp, 0x0a43, 28) & ~(0x7 << 8)) | tmp);

		/* Change green table Giga long LDVDC to 0x5 */
		int_mdio_write(tp, 0x0a43, 27, 0x8077);
		int_mdio_write(tp, 0x0a43, 28,
			       (int_mdio_read(tp, 0x0a43, 28) & ~(0x7 << 8)) | tmp);
	}

out:
	/* release lock main */
	tmp = 0x0 << 1;
	int_mdio_write(tp, 0x0a46, 21,
		       (int_mdio_read(tp, 0x0a46, 21) & ~(0x1 << 1)) | tmp);
}

static void rtd16xxb_mdio_init(struct rtl8169_private *tp)
{
	u32 tmp;
	void __iomem *ioaddr = tp->mmio_addr;

	mdelay(10);     /* wait for MDIO ready */

	/* PHY will stay in state 1 mode */
	tmp = 0;
	while (0x1 != (int_mdio_read(tp, 0x0a42, 16) & 0x07)) {
		tmp += 1;
		mdelay(1);
		if (tmp >= 2000) {
			pr_err(PFX "PHY status is not 0x1 in bypass mode, current = 0x%02x\n",
			       (int_mdio_read(tp, 0x0a42, 16) & 0x07));
			break;
		}
	}

	/* ETN spec. set MDC freq = 8.9MHZ */
	tmp = rtl_ocp_read(tp, 0xde10) & ~(BIT(7) | BIT(6));
	tmp |= BIT(6); /* MDC freq = 8.9MHz */
	rtl_ocp_write(tp, 0xde10, tmp);

	/* fill fuse_rdy & rg_ext_ini_done */
	int_mdio_write(tp, 0x0a46, 20,
		       int_mdio_read(tp, 0x0a46, 20) | (BIT(1) | BIT(0)));

	/* init_autoload_done = 1 */
	tmp = rtl_ocp_read(tp, 0xe004);
	tmp |= BIT(7);
	rtl_ocp_write(tp, 0xe004, tmp);

	/* ee_mode = 3 */
	RTL_W8(CFG9346, CFG9346_UNLOCK);

	/* wait LAN-ON */
	tmp = 0;
	do {
		tmp += 1;
		mdelay(1);
		if (tmp >= 2000) {
			pr_err(PFX "PHY status is not 0x3, current = 0x%02x\n",
			       (int_mdio_read(tp, 0x0a42, 16) & 0x07));
			break;
		}
	} while (0x3 != (int_mdio_read(tp, 0x0a42, 16) & 0x07));
	pr_info(PFX "wait %d ms for PHY ready, current = 0x%x\n",
		tmp, int_mdio_read(tp, 0x0a42, 16));

	/* adjust PHY electrical characteristics */
	rtd16xxb_phy_iol_tuning(tp);

	/* load OTP contents (R-K, Amp)
	 */
	rtd16xxb_load_otp_content(tp);

	switch (tp->output_mode) {
	case OUTPUT_EMBEDDED_PHY:
		/* ISO spec, set data path to FEPHY */
		tmp = rtl_ocp_read(tp, 0xea30);
		tmp &= ~(BIT(0));
		rtl_ocp_write(tp, 0xea30, tmp);

		tmp = rtl_ocp_read(tp, 0xea34);
		tmp &= ~(BIT(1) | BIT(0));
		tmp |= BIT(1);  /* FEPHY */
		rtl_ocp_write(tp, 0xea34, tmp);

		/* LED low active circuit */
		/* output current: 4mA */
		regmap_update_bits_base(tp->pinctrl_base, RTD16XXB_ISO_TESTMUX_PFUNC12,
					GENMASK(14, 0),
					(0x16 << 10) | (0x16 << 5) | (0x16 << 0),
					NULL, false, true);
		break;
	default:
		pr_err(PFX "%s:%d: unsupport output mode (%d)\n",
		       __func__, __LINE__, tp->output_mode);
	}
}

static void rtd16xxb_eee_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		/* enable eee auto fallback function */
		rtl_phy_write(tp, 0x0a4b, 17,
			      rtl_phy_read(tp, 0x0a4b, 17) | BIT(2));
		/* 1000M/100M EEE capability */
		rtl_mmd_write(tp, 0x7, 60, (BIT(2) | BIT(1)));
		/* 10M EEE */
		rtl_phy_write(tp, 0x0a43, 25,
			      rtl_phy_read(tp, 0x0a43, 25) | (BIT(4) | BIT(2)));
		/* keep RXC in LPI mode */
		rtl_mmd_write(tp, 0x3, 0,
			      rtl_mmd_read(tp, 0x3, 0) & ~BIT(10));
		/* EEE Tw_sys_tx timing adjustment,
		 * make sure MAC would send data after FEPHY wake up
		 */
		/* default 0x001F, 100M EEE */
		rtl_ocp_write(tp, 0xe04a, 0x002f);
		/* default 0x0011, 1000M EEE */
		rtl_ocp_write(tp, 0xe04c, 0x001a);
		/* EEEP Tw_sys_tx timing adjustment,
		 * make sure MAC would send data after FEPHY wake up
		 */
		 /* default 0x3f, 10M EEEP */
		/* FEPHY needs 160us */
		rtl_ocp_write(tp, 0xe08a, 0x00a8);
		/* default 0x0005, 100M/1000M EEEP */
		rtl_ocp_write(tp, 0xe08c, 0x0008);
		/* EEE MAC mode */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) | BIT(1) | BIT(0));
		/* EEE+ MAC mode */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) | BIT(1));
	} else {
		/* disable eee auto fallback function */
		rtl_phy_write(tp, 0x0a4b, 17,
			      rtl_phy_read(tp, 0x0a4b, 17) & ~BIT(2));
		/* 100M & 1000M EEE capability */
		rtl_mmd_write(tp, 0x7, 60, 0);
		/* 10M EEE */
		rtl_phy_write(tp, 0x0a43, 25,
			      rtl_phy_read(tp, 0x0a43, 25) & ~(BIT(4) | BIT(2)));
		/* keep RXC in LPI mode */
		rtl_mmd_write(tp, 0x3, 0,
			      rtl_mmd_read(tp, 0x3, 0) & ~BIT(10));
		/* reset to default value */
		rtl_ocp_write(tp, 0xe040, rtl_ocp_read(tp, 0xe040) | BIT(13));
		/* EEE MAC mode */
		rtl_ocp_write(tp, 0xe040,
			      rtl_ocp_read(tp, 0xe040) & ~(BIT(1) | BIT(0)));
		/* EEE+ MAC mode */
		rtl_ocp_write(tp, 0xe080, rtl_ocp_read(tp, 0xe080) & ~BIT(1));
		/* EEE Tw_sys_tx timing restore */
		/* default 0x001F, 100M EEE */
		rtl_ocp_write(tp, 0xe04a, 0x001f);
		/* default 0x0011, 1000M EEE */
		rtl_ocp_write(tp, 0xe04c, 0x0011);
		/* EEEP Tw_sys_tx timing restore */
		/* default 0x3f, 10M EEEP */
		rtl_ocp_write(tp, 0xe08a, 0x003f);
		/* default 0x0005, 100M/1000M EEEP */
		rtl_ocp_write(tp, 0xe08c, 0x0005);
	}
}

static void rtd16xxb_led_set(struct rtl8169_private *tp, bool enable)
{
	if (enable) {
		regmap_update_bits_base(tp->pinctrl_base, RTD16XXB_ISO_TESTMUX_MUXPAD2,
					GENMASK(30, 29) | GENMASK(12, 8),
					(0x1 << 29) | (0x1 << 11) | (0x1 << 8),
					NULL, false, true);
	} else {
		regmap_update_bits_base(tp->pinctrl_base, RTD16XXB_ISO_TESTMUX_MUXPAD2,
					GENMASK(30, 29) | GENMASK(12, 8),
					0, NULL, false, true);
	}
}

static void rtd16xxb_dump_regs(struct seq_file *m, struct rtl8169_private *tp)
{
	u32 val;

	regmap_read(tp->iso_base, ISO_UMSK_ISR, &val);
	seq_printf(m, "ISO_UMSK_ISR\t[0x98007004] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_PWRCUT_ETN, &val);
	seq_printf(m, "ISO_PWRCUT_ETN\t[0x9800705c] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_ETN_TESTIO, &val);
	seq_printf(m, "ISO_ETN_TESTIO\t[0x98007060] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_SOFT_RESET, &val);
	seq_printf(m, "ETN_RESET_CTRL\t[0x98007088] = %08x\n", val);
	regmap_read(tp->iso_base, ISO_CLOCK_ENABLE, &val);
	seq_printf(m, "ETN_CLK_CTRL\t[0x9800708c] = %08x\n", val);
}

/******************* END of RTD16XXB ****************************/
static struct r8169soc_chip_info rtd119x_info = {
	.name			= "RTD119X",
	.reset_phy_gmac		= rtd119x_reset_phy_gmac,
	.acp_init		= NULL,
	.pll_clock_init		= rtd119x_pll_clock_init,
	.mdio_init		= rtd119x_mdio_init,
	.mac_mcu_patch		= rtd119x_mac_mcu_patch,
	.hw_phy_config		= rtd119x_hw_phy_config,
	.eee_set		= rtd119x_eee_set,
	.led_set		= rtd119x_led_set,
	.dump_regs		= rtd119x_dump_regs,
	.dump_var		= rtd119x_dump_var,
	.mac_version		= 41,
	.cfg_version		= RTL_CFG_1,
	.txd_version		= RTL_TD_1,
	.jumbo_max		= JUMBO_9K,
	.jumbo_tx_csum		= false,
	.led_cfg		= 0x000670CA,
	.region			= 2,
	.align			= 8,
	.event_slow		= SYS_ERR | LINK_CHG | RX_OVERFLOW,
	.features		= RTL_FEATURE_GMII | RTL_FEATURE_MSI,
};

static struct r8169soc_chip_info rtd129x_info = {
	.name			= "RTD129X",
	.mdio_lock		= rtd129x_mdio_lock,
	.mdio_unlock		= rtd129x_mdio_unlock,
	.reset_phy_gmac		= rtd129x_reset_phy_gmac,
	.acp_init		= NULL,
	.pll_clock_init		= rtd129x_pll_clock_init,
	.mac_mcu_patch		= rtd129x_mac_mcu_patch,
	.hw_phy_config		= rtd129x_hw_phy_config,
	.eee_set		= rtd129x_eee_set,
	.led_set		= rtd129x_led_set,
	.dump_regs		= rtd129x_dump_regs,
	.dump_var		= rtd129x_dump_var,
	.mac_version		= 41,
	.cfg_version		= RTL_CFG_1,
	.txd_version		= RTL_TD_1,
	.jumbo_max		= JUMBO_9K,
	.jumbo_tx_csum		= false,
	.led_cfg		= 0x000670CA,
	.region			= 2,
	.align			= 8,
	.event_slow		= SYS_ERR | LINK_CHG | RX_OVERFLOW,
	.features		= RTL_FEATURE_GMII | RTL_FEATURE_MSI |
				  RTL_FEATURE_EEE,
};

static struct r8169soc_chip_info rtd139x_info = {
	.name			= "RTD139X",
	.mdio_lock		= rtd139x_mdio_lock,
	.mdio_unlock		= rtd139x_mdio_unlock,
	.reset_phy_gmac		= rtd139x_reset_phy_gmac,
	.acp_init		= rtd139x_acp_init,
	.pll_clock_init		= rtd139x_pll_clock_init,
	.mdio_init		= rtd139x_mdio_init,
	.wakeup_set		= rtl_crc_wakeup_set,
	.eee_set		= rtd139x_eee_set,
	.led_set		= rtd139x_led_set,
	.dump_regs		= rtd139x_dump_regs,
	.dump_var		= rtd139x_dump_var,
	.mac_version		= 41,
	.cfg_version		= RTL_CFG_1,
	.txd_version		= RTL_TD_1,
	.jumbo_max		= JUMBO_9K,
	.jumbo_tx_csum		= false,
	.led_cfg		= 0x000679A9,
	.region			= 2,
	.align			= 8,
	.event_slow		= SYS_ERR | LINK_CHG | RX_OVERFLOW,
	.features		= RTL_FEATURE_GMII | RTL_FEATURE_MSI |
				  RTL_FEATURE_ACP | RTL_FEATURE_EEE,
};

static struct r8169soc_chip_info rtd16xx_info = {
	.name			= "RTD16XX",
	.mdio_lock		= rtd16xx_mdio_lock,
	.mdio_unlock		= rtd16xx_mdio_unlock,
	.reset_phy_gmac		= rtd16xx_reset_phy_gmac,
	.acp_init		= rtd16xx_acp_init,
	.pll_clock_init		= rtd16xx_pll_clock_init,
	.mdio_init		= rtd16xx_mdio_init,
	.wakeup_set		= rtl_crc_wakeup_set,
	.eee_set		= rtd16xx_eee_set,
	.led_set		= rtd16xx_led_set,
	.dump_regs		= rtd16xx_dump_regs,
	.dump_var		= rtd16xx_dump_var,
	.mac_version		= 41,
	.cfg_version		= RTL_CFG_1,
	.txd_version		= RTL_TD_1,
	.jumbo_max		= JUMBO_9K,
	.jumbo_tx_csum		= false,
	.led_cfg		= 0x00067CA9,
	.region			= 2,
	.align			= 8,
	.event_slow		= SYS_ERR | LINK_CHG | RX_OVERFLOW,
	.features		= RTL_FEATURE_GMII | RTL_FEATURE_MSI |
				  RTL_FEATURE_TX_NO_CLOSE |
				  RTL_FEATURE_ADJUST_FIFO |
				  RTL_FEATURE_ACP | RTL_FEATURE_EEE,
};

static struct r8169soc_chip_info rtd13xx_info = {
	.name			= "RTD13XX",
	.mdio_lock		= rtd13xx_mdio_lock,
	.mdio_unlock		= rtd13xx_mdio_unlock,
	.reset_phy_gmac		= rtd13xx_reset_phy_gmac,
	.acp_init		= rtd13xx_acp_init,
	.pll_clock_init		= rtd13xx_pll_clock_init,
	.mdio_init		= rtd13xx_mdio_init,
	.wakeup_set		= rtl_crc_wakeup_set,
	.eee_set		= rtd13xx_eee_set,
	.led_set		= rtd13xx_led_set,
	.dump_regs		= rtd13xx_dump_regs,
	.dump_var		= rtd13xx_dump_var,
	.mac_version		= 41,
	.cfg_version		= RTL_CFG_1,
	.txd_version		= RTL_TD_1,
	.jumbo_max		= JUMBO_9K,
	.jumbo_tx_csum		= false,
	.led_cfg		= 0x00067CA9,
	.region			= 2,
	.align			= 8,
	.event_slow		= SYS_ERR | LINK_CHG | RX_OVERFLOW,
	.features		= RTL_FEATURE_GMII | RTL_FEATURE_MSI |
				  RTL_FEATURE_TX_NO_CLOSE |
				  RTL_FEATURE_ADJUST_FIFO |
				  RTL_FEATURE_ACP | RTL_FEATURE_EEE,
};

static struct r8169soc_chip_info rtd16xxb_info = {
	.name			= "RTD16XXB",
	.mdio_lock		= rtd16xxb_mdio_lock,
	.mdio_unlock		= rtd16xxb_mdio_unlock,
	.reset_phy_gmac		= rtd16xxb_reset_phy_gmac,
	.pll_clock_init		= rtd16xxb_pll_clock_init,
	.mdio_init		= rtd16xxb_mdio_init,
	.wakeup_set		= rtl_pat_wakeup_set,
	.eee_set		= rtd16xxb_eee_set,
	.led_set		= rtd16xxb_led_set,
	.dump_regs		= rtd16xxb_dump_regs,
	.mac_version		= 41,
	.cfg_version		= RTL_CFG_1,
	.txd_version		= RTL_TD_1,
	.jumbo_max		= JUMBO_9K,
	.jumbo_tx_csum		= false,
	.led_cfg		= 0x17000CA9,
	.region			= 2,
	.align			= 8,
	.event_slow		= SYS_ERR | LINK_CHG | RX_OVERFLOW,
	.features		= RTL_FEATURE_GMII | RTL_FEATURE_MSI |
				  RTL_FEATURE_TX_NO_CLOSE |
				  RTL_FEATURE_ADJUST_FIFO |
				  RTL_FEATURE_EEE |
				  RTL_FEATURE_OCP_MDIO |
				  RTL_FEATURE_PAT_WAKE,
};

static const struct of_device_id r8169soc_dt_ids[] = {
	{
		.compatible = "realtek,rtd119x-r8169soc",
		.data = &rtd119x_info,
	},
	{
		.compatible = "realtek,rtd129x-r8169soc",
		.data = &rtd129x_info,
	},
	{
		.compatible = "realtek,rtd139x-r8169soc",
		.data = &rtd139x_info,
	},
	{
		.compatible = "realtek,rtd16xx-r8169soc",
		.data = &rtd16xx_info,
	},
	{
		.compatible = "realtek,rtd13xx-r8169soc",
		.data = &rtd13xx_info,
	},
	{
		.compatible = "realtek,rtd16xxb-r8169soc",
		.data = &rtd16xxb_info,
	},
	{
	}
};

MODULE_DEVICE_TABLE(of, r8169soc_dt_ids);

static void rtk_chip_info_check(struct r8169soc_chip_info *chip)
{
	if (!chip)
		return;

	if (!chip->mdio_lock)
		chip->mdio_lock = dummy_mdio_lock;
	if (!chip->mdio_unlock)
		chip->mdio_unlock = dummy_mdio_unlock;
	if (!chip->pll_power_down)
		chip->pll_power_down = r8168_pll_power_down;
	if (!chip->pll_power_up)
		chip->pll_power_up = r8168_pll_power_up;
	if (!chip->csi_write)
		chip->csi_write = r8169_csi_write;
	if (!chip->csi_read)
		chip->csi_read = r8169_csi_read;
	if (!chip->set_speed)
		chip->set_speed = rtl8169_set_speed_xmii;
	if (!chip->phy_reset_enable)
		chip->phy_reset_enable = rtl8169_xmii_reset_enable;
	if (!chip->phy_reset_pending)
		chip->phy_reset_pending = rtl8169_xmii_reset_pending;
	if (!chip->link_ok)
		chip->link_ok = rtl8169_xmii_link_ok;
	if (!chip->do_ioctl)
		chip->do_ioctl = rtl_xmii_ioctl;
	if (!chip->hw_start)
		chip->hw_start = rtl_hw_start_8168;
	if (!chip->reset_phy_gmac)
		chip->reset_phy_gmac = dummy_reset_phy_gmac;
	if (!chip->acp_init)
		chip->acp_init = dummy_acp_init;
	if (!chip->pll_clock_init)
		chip->pll_clock_init = dummy_pll_clock_init;
	if (!chip->mdio_init)
		chip->mdio_init = dummy_mdio_init;
	if (!chip->mac_mcu_patch)
		chip->mac_mcu_patch = dummy_mac_mcu_patch;
	if (!chip->hw_phy_config)
		chip->hw_phy_config = dummy_hw_phy_config;
	if (!chip->wakeup_set)
		chip->wakeup_set = dummy_wakeup_set;
	if (!chip->eee_set)
		chip->eee_set = dummy_eee_set;
	if (!chip->led_set)
		chip->led_set = dummy_led_set;
	if (!chip->dump_regs)
		chip->dump_regs = dummy_dump_regs;
	if (!chip->dump_var)
		chip->dump_var = dummy_dump_var;

	#if defined(CONFIG_RTL_RX_NO_COPY)
	chip->features |= RTL_FEATURE_RX_NO_COPY;
	#endif /* CONFIG_RTL_RX_NO_COPY */
}

static int rtl_init_one(struct platform_device *pdev)
{
	struct r8169soc_chip_info *chip;
	struct rtl8169_private *tp;
	struct mii_if_info *mii;
	struct net_device *ndev;
	void __iomem *ioaddr;
	int i;
	int rc;
	int led_config;
	u32 tmp;
	int irq;
	int retry;
	const char *mac_addr;
	struct property *wake_mask;
	struct property *wake_pattern;
	char tmp_str[80];
	int wake_size;
	int wake_mask_size;
	struct device_node *phy_0;
	u32 phy_irq_num = 0;
	u32 phy_irq_mask = 0;
	u32 phy_irq[POR_NUM] = {0, 0, 0};
	u8 phy_irq_map[POR_NUM + 1] = {0, 0, 0, 0};
	u32 phy_por_xv_mask;
	static char phy_irq_name[POR_NUM][IFNAMSIZ];
#ifdef RTL_PROC
	struct proc_dir_entry *dir_dev = NULL;
	struct proc_dir_entry *entry = NULL;
#endif
	u32 bypass_enable = 0;
	u32 acp_enable = 0;
	u32 sgmii_swing = 0;
	u32 voltage = 1;
	u32 tx_delay = 0;
	u32 rx_delay = 0;
	u32 output_mode = 0;
	u32 ext_phy_id = 1;
	u32 eee_enable = 0;
	u32 wol_enable = 0;

	if (netif_msg_drv(&debug)) {
		pr_info("%s Gigabit Ethernet driver %s loaded\n",
			MODULENAME, RTL8169_VERSION);
	}

	chip = (struct r8169soc_chip_info *)of_device_get_match_data(&pdev->dev);
	if (chip) {
		rtk_chip_info_check(chip);
	} else {
		pr_err(PFX "%s no proper chip matched\n", __func__);
		rc = -EINVAL;
		goto out;
	}
	if (of_property_read_u32(pdev->dev.of_node, "output-mode", &output_mode))
		dprintk("%s can't get output mode", __func__);
	if (of_property_read_u32(pdev->dev.of_node, "wol-enable", &wol_enable))
		dprintk("%s can't get wol_enable", __func__);
	if (of_property_read_u32(pdev->dev.of_node, "eee", &eee_enable))
		dprintk("%s can't get eee_enable", __func__);
	if (of_property_read_u32(pdev->dev.of_node, "acp", &acp_enable))
		dprintk("%s can't get acp", __func__);

	/* optional properties */
	if (of_property_read_u32(pdev->dev.of_node, "bypass",
				 &bypass_enable))
		bypass_enable = 1;
	if (of_property_read_u32(pdev->dev.of_node, "led-cfg", &led_config))
		led_config = 0;
	if (of_property_read_u32(pdev->dev.of_node, "ext-phy-id", &ext_phy_id))
		ext_phy_id = 0;
	if (of_property_read_u32(pdev->dev.of_node, "sgmii-swing",
				 &sgmii_swing))
		sgmii_swing = 0;
	if (of_property_read_u32(pdev->dev.of_node, "voltage", &voltage))
		voltage = 1;
	if (of_property_read_u32(pdev->dev.of_node, "tx-delay", &tx_delay))
		tx_delay = 0;
	if (of_property_read_u32(pdev->dev.of_node, "rx-delay", &rx_delay))
		rx_delay = 0;
	if (of_get_property(pdev->dev.of_node, "force-Gb-off", NULL)) {
		pr_info(PFX "~~~ disable Gb features~~~\n");
		chip->features &= ~RTL_FEATURE_GMII;
	}

	phy_0 = of_get_child_by_name(pdev->dev.of_node, "phy_0");
	if (phy_0) {
		phy_irq_num = of_irq_count(phy_0);
		pr_info(PFX "get %d phy_irq\n", phy_irq_num);
		if (phy_irq_num > POR_NUM) {
			pr_err(PFX "%d exceed max. GPHY IRQ number\n",
			       phy_irq_num);
			phy_irq_num = POR_NUM;
		}

		if (of_property_read_u32(phy_0, "irq-mask", &phy_irq_mask)) {
			pr_err(PFX "no irq-mask is defined\n");
			phy_irq_num = 0;
		} else {
			tmp = 0;
			for (i = 0; i < 32; i++) {
				if (phy_irq_mask & (1 << i))
					phy_irq_map[tmp++] = i;

				if (tmp > phy_irq_num)
					break;
			}
			if (tmp != phy_irq_num) {
				pr_err(PFX "IRQ number %d and IRQ mask 0x%08x don't match\n",
				       phy_irq_num, phy_irq_mask);
				phy_irq_num = min(tmp, phy_irq_num);
			}
		}
		for (i = 0; i < phy_irq_num; i++) {
			phy_irq[i] = irq_of_parse_and_map(phy_0, i);
			pr_info(PFX "get phy_irq %d for bit %d\n",
				phy_irq[i], phy_irq_map[i]);
		}

		if (of_property_read_u32(phy_0, "por-xv-mask",
					 &phy_por_xv_mask)) {
			pr_info(PFX "no por-xv-mask is defined\n");
			phy_por_xv_mask = POR_XV_MASK;
		}
	}
	/* end of optional properties */

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	ioaddr = of_iomap(pdev->dev.of_node, 0);

	ndev = alloc_etherdev(sizeof(*tp));
	if (!ndev) {
		rc = -ENOMEM;
		goto err_out_iomap;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->irq = irq;
	ndev->netdev_ops = &rtl_netdev_ops;
	tp = netdev_priv(ndev);
	tp->dev = ndev;
	tp->pdev = pdev;
	tp->chip = chip;
	tp->msg_enable = netif_msg_init(debug.msg_enable, R8169_MSG_DEFAULT);
	tp->mac_version = tp->chip->mac_version;
	tp->mmio_addr = ioaddr;
	tp->led_cfg = led_config;
	tp->output_mode = output_mode;
	tp->ext_phy_id = ext_phy_id;
	if ((tp->chip->features & RTL_FEATURE_EEE) && eee_enable > 0)
		tp->eee_enable = true;
	else
		tp->eee_enable = false;
	tp->phy_irq_num = phy_irq_num;
	tp->phy_por_xv_mask = phy_por_xv_mask;
	for (i = 0; i < tp->phy_irq_num; i++) {
		tp->phy_irq[i] = phy_irq[i];
		tp->phy_irq_map[i] = phy_irq_map[i];
	}
	atomic_set(&tp->phy_reinit_flag, 0);

	/* ISO regs 0x98007000 */
	tp->iso_base = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "realtek,iso");
	if (IS_ERR_OR_NULL(tp->iso_base)) {
		pr_err(PFX "Fail to get iso_base address\n");
		rc = -ENODEV;
		goto err_out_netdev;
	} else {
		pr_info(PFX "Get iso_base address\n");
	}

	/* SB2 regs 0x9801a000 (optional) */
	/* it should be valid for rtd129x */
	tp->sb2_base = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "realtek,sb2");
	if (IS_ERR_OR_NULL(tp->sb2_base)) {
		if (tp->chip == &rtd129x_info) {
			pr_err(PFX "Fail to get sb2_base address\n");
			rc = -ENODEV;
			goto err_out_netdev;
		}
	} else {
		pr_info(PFX "Get sb2_base address\n");
	}

	/* SBX regs 0x9801c000 (optional) */
	/* it is not used by rtd119x, rtd129x, and rtd16xxb */
	/* it should be valid for rtd139x, rtd16xx, and rtd13xx */
	tp->sbx_base = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "realtek,sbx");
	if (IS_ERR_OR_NULL(tp->sbx_base)) {
		if (tp->chip != &rtd119x_info &&
		    tp->chip != &rtd129x_info &&
		    tp->chip != &rtd16xxb_info) {
			pr_err(PFX "Fail to get sbx_base address\n");
			rc = -ENODEV;
			goto err_out_netdev;
		}
	} else {
		pr_info(PFX "Get sbx_base address\n");
	}

	/* SCPU wrapper regs 0x9801d000 (optional) */
	/* it is not used by rtd119x, rtd129x, and rtd16xxb */
	/* it should be valid for rtd139x, rtd16xx, and rtd13xx */
	tp->scpu_wrap_base = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							     "realtek,scpu_wrapper");
	if (IS_ERR_OR_NULL(tp->scpu_wrap_base)) {
		if (tp->chip != &rtd119x_info &&
		    tp->chip != &rtd129x_info &&
		    tp->chip != &rtd16xxb_info) {
			pr_err(PFX "Fail to get scpu_wrap_base address\n");
			rc = -ENODEV;
			goto err_out_netdev;
		}
	} else {
		pr_info(PFX "Get scpu_wrap_base address\n");
	}

	/* pinctrl regs 0x9804e000 (optional) */
	/* it is not used by rtd119x and rtd129x */
	/* it should be valid for rtd139x, rtd16xx, and rtd13xx */
	tp->pinctrl_base = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "realtek,pinctrl");
	if (IS_ERR_OR_NULL(tp->pinctrl_base)) {
		if (tp->chip != &rtd119x_info && tp->chip != &rtd129x_info) {
			pr_err(PFX "Fail to get pinctrl_base address\n");
			rc = -ENODEV;
			goto err_out_netdev;
		}
	} else {
		pr_info(PFX "Get pinctrl_base address\n");
	}

	/* SDS regs 0x981c8000 (optional) */
	/* it should be valid for rtd139x and rtd16xx */
	tp->sds_base = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "realtek,sds");
	if (IS_ERR_OR_NULL(tp->sds_base)) {
		if (tp->chip == &rtd139x_info || tp->chip == &rtd16xx_info) {
			pr_err(PFX "Fail to get sds_base address\n");
			rc = -ENODEV;
			goto err_out_netdev;
		}
	} else {
		pr_info(PFX "Get sds_base address\n");
	}

	tp->bypass_enable = !!(bypass_enable > 0);
	if ((tp->chip->features & RTL_FEATURE_ACP) && acp_enable > 0)
		tp->acp_enable = true;
	else
		tp->acp_enable = false;
	tp->ext_phy = false;

	switch (tp->output_mode) {
	case OUTPUT_RGMII_TO_MAC:
	case OUTPUT_RGMII_TO_PHY:
		tp->rgmii.voltage = voltage;
		tp->rgmii.tx_delay = tx_delay;
		tp->rgmii.rx_delay = rx_delay;
		break;
	case OUTPUT_SGMII_TO_MAC:
	case OUTPUT_SGMII_TO_PHY:
		tp->sgmii.swing = sgmii_swing;
		break;
	case OUTPUT_RMII:
		tp->rmii.voltage = voltage;
		tp->rmii.tx_delay = tx_delay;
		tp->rmii.rx_delay = rx_delay;
		break;
	case OUTPUT_EMBEDDED_PHY:
	case OUTPUT_FORCE_LINK:
	default:
		/* do nothing */
		break;
	}

	mii = &tp->mii;
	mii->dev = ndev;
	mii->mdio_read = rtl_mdio_read;
	mii->mdio_write = rtl_mdio_write;
	mii->phy_id_mask = 0x1f;
	mii->reg_num_mask = 0x1f;
	mii->supports_gmii = !!(tp->chip->features & RTL_FEATURE_GMII);

	rtl_init_mdio_ops(tp);
	rtl_init_mmd_ops(tp);
	tp->chip->reset_phy_gmac(tp);
	tp->chip->acp_init(tp);
	tp->chip->pll_clock_init(tp);
	tp->chip->mdio_init(tp);
	/* after r8169soc_mdio_init(),
	 * SGMII : tp->ext_phy == true  ==> external MDIO,
	 * RGMII : tp->ext_phy == true  ==> external MDIO,
	 * RMII  : tp->ext_phy == false ==> internal MDIO,
	 * FE PHY: tp->ext_phy == false ==> internal MDIO
	 */

	tp->chip->eee_set(tp, tp->eee_enable);

	/* Enable ALDPS */
	rtl_phy_write(tp, 0x0a43, 24,
		      rtl_phy_read(tp, 0x0a43, 24) | BIT(2));

	tp->wol_enable = wol_enable;
	tp->wol_crc_cnt = 0;
	if (tp->chip->features & RTL_FEATURE_PAT_WAKE) {
		wake_size = RTL_WAKE_SIZE;
		wake_mask_size = RTL_WAKE_MASK_SIZE;
	} else {
		wake_size = RTL_WAKE_SIZE_CRC;
		wake_mask_size = RTL_WAKE_MASK_SIZE_CRC;
	}
	for (i = 0; i < wake_size; i++) {
		memset(tmp_str, 0, 80);
		sprintf(tmp_str, "wake-mask%d", i);
		wake_mask = of_find_property(pdev->dev.of_node, tmp_str, NULL);
		if (!wake_mask)
			break;
		tp->wol_rule[i].mask_size = wake_mask->length;
		memcpy(&tp->wol_rule[i].mask[0], wake_mask->value,
		       (wake_mask->length > wake_mask_size) ?
		       wake_mask_size : wake_mask->length);

		sprintf(tmp_str, "wake-crc%d", i);
		if (of_property_read_u32(pdev->dev.of_node, tmp_str, &tmp))
			break;
		tp->wol_rule[i].crc = tmp & 0xFFFF;
		if (tp->chip->features & RTL_FEATURE_PAT_WAKE) {
			sprintf(tmp_str, "wake-pattern%d", i);
			wake_pattern = of_find_property(pdev->dev.of_node, tmp_str, NULL);
			if (!wake_pattern)
				break;
			tmp = (wake_pattern->length > RTL_WAKE_PATTERN_SIZE) ?
				RTL_WAKE_PATTERN_SIZE : wake_pattern->length;
			tp->wol_rule[i].pattern_size =
				rtl_cp_reduced_pattern(tp, i, wake_pattern->value, tmp);

			sprintf(tmp_str, "wake-offset%d", i);
			if (of_property_read_u32(pdev->dev.of_node, tmp_str, &tmp))
				break;
			tp->wol_rule[i].offset = tmp & 0xFFFF;
		}
		tp->wol_crc_cnt += 1;
		tp->wol_rule[i].flag |= WAKE_FLAG_ENABLE;
	}

#ifdef RTL_PROC
	do {
		/* create /proc/net/$rtw_proc_name */
		rtw_proc = proc_mkdir_data("eth0", 0555,
					   init_net.proc_net, NULL);

		if (!rtw_proc) {
			pr_info(PFX "procfs:create /proc/net/eth0 failed\n");
			break;
		}

		/* create /proc/net/$rtw_proc_name/$dev->name */
		if (!tp->dir_dev) {
			tp->dir_dev = proc_mkdir_data(MODULENAME,
						      S_IFDIR | 0555,
						      rtw_proc, ndev);
			dir_dev = tp->dir_dev;

			if (!dir_dev) {
				pr_info(PFX "procfs:create /proc/net/eth0/r8169 failed\n");

				if (rtw_proc) {
					remove_proc_entry("eth0",
							  init_net.proc_net);
					rtw_proc = NULL;
				}
				break;
			}
		} else {
			break;
		}

		entry = proc_create_data("wol_enable", S_IFREG | 0644,
					 dir_dev, &wol_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/wol_enable failed\n");
			break;
		}

		entry = proc_create_data("pwr_saving", S_IFREG | 0644,
					 dir_dev, &pwr_saving_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/pwr_saving failed\n");
			break;
		}

		entry = proc_create_data("mac_reinit", S_IFREG | 0644,
					 dir_dev, &mac_reinit_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/mac_reinit failed\n");
			break;
		}

		entry = proc_create_data("phy_reinit", S_IFREG | 0644,
					 dir_dev, &phy_reinit_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/phy_reinit failed\n");
			break;
		}

		entry = proc_create_data("eee", S_IFREG | 0644,
					 dir_dev, &eee_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/eee failed\n");
			break;
		}

		entry = proc_create_data("driver_var", S_IFREG | 0444,
					 dir_dev, &driver_var_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/driver_var failed\n");
			break;
		}

		entry = proc_create_data("eth_phy", S_IFREG | 0666,
					 dir_dev, &eth_phy_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/eth_phy failed\n");
			break;
		}

		entry = proc_create_data("ext_regs", S_IFREG | 0444,
					 dir_dev, &ext_regs_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/ext_regs failed\n");
			break;
		}

		entry = proc_create_data("registers", S_IFREG | 0444,
					 dir_dev, &registers_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/registers failed\n");
			break;
		}

		entry = proc_create_data("tx_desc", S_IFREG | 0444,
					 dir_dev, &tx_desc_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/tx_desc failed\n");
			break;
		}

		entry = proc_create_data("rx_desc", S_IFREG | 0444,
					 dir_dev, &rx_desc_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/rx_desc failed\n");
			break;
		}

		entry = proc_create_data("tally", S_IFREG | 0444,
					 dir_dev, &tally_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/tally failed\n");
			break;
		}

		entry = proc_create_data("wpd_event", S_IFREG | 0444,
					 dir_dev, &wpd_evt_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/wpd_event failed\n");
			break;
		}

		entry = proc_create_data("wol_packet", S_IFREG | 0444,
					 dir_dev, &wol_pkt_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/wol_packet failed\n");
			break;
		}

		entry = proc_create_data("wake_mask", S_IFREG | 0644,
					 dir_dev, &wake_mask_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/wake_mask failed\n");
			break;
		}

		entry = proc_create_data("wake_crc", S_IFREG | 0644,
					 dir_dev, &wake_crc_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/wake_crc failed\n");
			break;
		}

		entry = proc_create_data("wake_idx_en", S_IFREG | 0644,
					 dir_dev, &wake_idx_en_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/wake_idx_en failed\n");
			break;
		}

		entry = proc_create_data("wake_dump", S_IFREG | 0444,
					 dir_dev, &wake_dump_proc_fops, NULL);
		if (!entry) {
			pr_info(PFX "procfs:create /proc/net/eth0/r8169/wake_dump failed\n");
			break;
		}

		if (tp->chip->features & RTL_FEATURE_PAT_WAKE) {
			entry = proc_create_data("wake_offset", S_IFREG | 0644,
						 dir_dev, &wake_offset_proc_fops, NULL);
			if (!entry) {
				pr_info(PFX "procfs:create /proc/net/eth0/r8169/wake_offset failed\n");
				break;
			}

			entry = proc_create_data("wake_pattern", S_IFREG | 0644,
						 dir_dev, &wake_pattern_proc_fops, NULL);
			if (!entry) {
				pr_info(PFX "procfs:create /proc/net/eth0/r8169/wake_pattern failed\n");
				break;
			}
		}
	} while (0);
#endif

	rtl_init_rxcfg(tp);

	rtl_irq_disable(tp);

	rtl_hw_initialize(tp);

	rtl_hw_reset(tp);

	rtl_ack_events(tp, 0xffff);

	tp->txd_version = tp->chip->txd_version;

	RTL_W8(CFG9346, CFG9346_UNLOCK);
	RTL_W8(CONFIG1, RTL_R8(CONFIG1) | PM_ENABLE);
	RTL_W8(CONFIG5, RTL_R8(CONFIG5) & PME_STATUS);

	/* disable magic packet WOL */
	RTL_W8(CONFIG3, RTL_R8(CONFIG3) & ~MAGIC_PKT);

	if ((RTL_R8(CONFIG3) & (LINK_UP | MAGIC_PKT)) != 0)
		tp->features |= RTL_FEATURE_WOL;
	if ((RTL_R8(CONFIG5) & (UWF | BWF | MWF)) != 0)
		tp->features |= RTL_FEATURE_WOL;
	RTL_W8(CFG9346, CFG9346_LOCK);

	if (tp->output_mode == OUTPUT_SGMII_TO_MAC ||
	    tp->output_mode == OUTPUT_RGMII_TO_MAC)
		tp->chip->link_ok = rtl8169_xmii_always_link_ok;

	mutex_init(&tp->wk.mutex);

	/* Get MAC address */
	mac_addr = of_get_mac_address(pdev->dev.of_node);
	if (mac_addr)
		rtl_rar_set(tp, (u8 *)mac_addr);

	/* workaround: avoid getting deadbeef */
#define RETRY_MAX	10
	for (retry = 0; retry < RETRY_MAX; retry++) {
		for (i = 0; i < ETH_ALEN; i++)
			ndev->dev_addr[i] = RTL_R8(MAC0 + i);

		if (*(u32 *)ndev->dev_addr == 0xdeadbeef) {
			pr_err(PFX "get invalid MAC address %pM, retry %d\n",
			       ndev->dev_addr, retry);
			tmp = RTL_R32(PHYAR);	/* read something else */
			usleep_range(10000, 11000);
		} else {
			break;
		}
	}
	if (retry == RETRY_MAX)
		pr_err(PFX "get invalid MAC address %pM, give up!\n", ndev->dev_addr);

	rtl_led_set(tp);

	ndev->ethtool_ops = &rtl8169_ethtool_ops;
	ndev->watchdog_timeo = RTL8169_TX_TIMEOUT;

	netif_napi_add(ndev, &tp->napi, rtl8169_poll, R8169_NAPI_WEIGHT);

	/* don't enable SG, IP_CSUM and TSO by default - it might not work
	 * properly for all devices
	 */
	ndev->features |=
		NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO | NETIF_F_RXCSUM |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;

	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
		NETIF_F_RXCSUM | NETIF_F_HW_VLAN_CTAG_TX |
		NETIF_F_HW_VLAN_CTAG_RX;
	ndev->vlan_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
		NETIF_F_HIGHDMA;

	ndev->hw_features |= NETIF_F_RXALL;
	ndev->hw_features |= NETIF_F_RXFCS;

	ndev->min_mtu = ETH_ZLEN;
	ndev->max_mtu = tp->chip->jumbo_max;

	tp->event_slow = tp->chip->event_slow;

	tp->opts1_mask = ~(RX_BOVF | RX_FOVF);

	rc = register_netdev(ndev);
	if (rc < 0)
		goto err_out_napi;

	platform_set_drvdata(pdev, ndev);

	netif_info(tp, probe, ndev, "%s, XID %08x IRQ %d\n",
		   tp->chip->name,
		   (u32)(RTL_R32(TX_CONFIG) & 0x9cf0f8ff), ndev->irq);
	if (tp->chip->jumbo_max != JUMBO_1K) {
		netif_info(tp, probe, ndev, "jumbo features [frames: %d bytes, tx checksumming: %s]\n",
			   tp->chip->jumbo_max,
			   tp->chip->jumbo_tx_csum ? "ok" : "ko");
	}

	device_set_wakeup_enable(&pdev->dev, tp->features & RTL_FEATURE_WOL);

	netif_carrier_off(ndev);

	if (tp->phy_irq_num) {
		/* monitor rising edge of POR interrupts */
		regmap_write(tp->iso_base, ISO_POR_CTRL, 0x00000333);
		/* clear GPHY HV/DV/AV POR interrupts */
		regmap_write(tp->iso_base, ISO_UMSK_ISR, 0x70000000);
	}

	for (i = 0; i < tp->phy_irq_num; i++) {
		memset(phy_irq_name[i], 0, IFNAMSIZ);
		sprintf(phy_irq_name[i], "eth_phy%d", i);
		rc = request_irq(tp->phy_irq[i], phy_irq_handler, IRQF_SHARED,
				 phy_irq_name[i], tp);
		if (rc < 0) {
			pr_err(PFX "unable to request %s IRQ %d, ret = 0x%x\n",
			       phy_irq_name[i], tp->phy_irq[i], rc);
		} else {
			pr_info(PFX "request %s IRQ %d successfully\n",
				phy_irq_name[i], tp->phy_irq[i]);
		}
	}

	rtl8169_init_phy(ndev, tp);
out:
	return rc;

err_out_napi:
	netif_napi_del(&tp->napi);

#ifdef RTL_PROC
	do {
		if (!tp->dir_dev)
			break;

		remove_proc_entry("wol_enable", tp->dir_dev);
		remove_proc_entry("pwr_saving", tp->dir_dev);
		remove_proc_entry("mac_reinit", tp->dir_dev);
		remove_proc_entry("phy_reinit", tp->dir_dev);
		remove_proc_entry("eee", tp->dir_dev);
		remove_proc_entry("driver_var", tp->dir_dev);
		remove_proc_entry("eth_phy", tp->dir_dev);
		remove_proc_entry("ext_regs", tp->dir_dev);
		remove_proc_entry("registers", tp->dir_dev);
		remove_proc_entry("tx_desc", tp->dir_dev);
		remove_proc_entry("rx_desc", tp->dir_dev);
		remove_proc_entry("tally", tp->dir_dev);
		remove_proc_entry("wpd_event", tp->dir_dev);
		remove_proc_entry("wol_packet", tp->dir_dev);
		remove_proc_entry("wake_mask", tp->dir_dev);
		remove_proc_entry("wake_crc", tp->dir_dev);
		remove_proc_entry("wake_idx_en", tp->dir_dev);
		remove_proc_entry("wake_dump", tp->dir_dev);
		if (tp->chip->features & RTL_FEATURE_PAT_WAKE) {
			remove_proc_entry("wake_offset", tp->dir_dev);
			remove_proc_entry("wake_pattern", tp->dir_dev);
		}

		if (!rtw_proc)
			break;

		remove_proc_entry(MODULENAME, rtw_proc);
		remove_proc_entry("eth0", init_net.proc_net);

		rtw_proc = NULL;

	} while (0);
#endif

err_out_netdev:
	free_netdev(ndev);

err_out_iomap:
	iounmap(ioaddr);
	return rc;
}

static struct platform_driver rtl8169_soc_driver = {
	.probe		= rtl_init_one,
	.remove		= rtl_remove_one,
	.shutdown	= rtl_shutdown,
	.driver = {
		.name		= MODULENAME,
		.owner		= THIS_MODULE,
		.pm		= RTL8169_PM_OPS,
		.of_match_table = of_match_ptr(r8169soc_dt_ids),
	},
};

module_platform_driver(rtl8169_soc_driver);
