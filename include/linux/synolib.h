#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// Copyright (c) 2000-2020 Synology Inc. All rights reserved.
#ifndef __SYNOLIB_H_
#define __SYNOLIB_H_

#include <linux/bitops.h>
#include <linux/blk-mq.h>

#ifdef MY_ABC_HERE
/* Maximum number of MAC addresses */
#define SYNO_MAC_MAX_NUMBER 8
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
void syno_draw_auto_remap_buffer(char *buffer, int size);
#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
int SynoNVMeGetDeviceIndex(struct gendisk *disk);
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
int syno_disk_get_device_index(struct block_device *bdev);
#endif /* MY_ABC_HERE */
#ifdef MY_DEF_HERE
int syno_pci_dev_to_i2c_bus(struct pci_dev*);
struct syno_device_list {
	char disk_name[DISK_NAME_LEN];
	struct list_head device_list;
};
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE
#define DT_INTERNAL_SLOT "internal_slot"
#define DT_ESATA_SLOT "esata_port"
#ifdef MY_DEF_HERE
#define DT_EUNIT_SLOT "eunit_slot"
#define DT_PCIE_EUNIT_MASTER_PORT "pcie_eunit_master_port"
#define DT_PCIE_EUNIT_NEXT_PORT "pcie_eunit_next_port"
#define DT_PCIE_EUNIT_SSID "pcie_eunit_ssid"
#define DT_PCIE_EUNIT_SLOT "pcie_eunit_slot"
#endif /* MY_DEF_HERE */
#define DT_CX4_SLOT "cx4_port"
#define DT_PCIE_SLOT "pcie_slot"
#define DT_USB_SLOT "usb_slot"
#define DT_HUB_SLOT "usb_hub"
#define DT_POWER_PIN_GPIO "power_pin_gpio"
#define DT_DETECT_PIN_GPIO "detect_pin_gpio"
#define DT_SWITCH_NO "switch_no"
#define DT_HDD_LED_TYPE "led_type"
#define DT_HDD_LED_TYPE_LP3943 "lp3943"
#define DT_HDD_LED_TYPE_ATMEGA1608 "atmega1608"
#define DT_HDD_LED_TYPE_GPIO "gpio"
#define DT_HDD_LED_TYPE_TRIG_DISK_SYNO "trig_disk_syno"
#define DT_HDD_ORANGE_LED "led_orange"
#define DT_HDD_GREEN_LED "led_green"
#define DT_HDD_LED_NAME "led_name"
#define DT_HDD_ACT_LED "led_activity"
#define DT_SYNO_GPIO "syno_gpio"
#define DT_PCIE_ROOT "pcie_root"
#define DT_ATA_PORT "ata_port"
#define DT_AHCI "ahci"
#define DT_RTK_AHCI "rtk_ahci"
#define DT_AHCI_MVEBU "ahci_mvebu"
#define DT_MV14XX "mv14xx"
#define DT_VIRTIO "virtio"
#define DT_PHY "phy"
#define DT_USB2 "usb2"
#define DT_USB3 "usb3"
#define DT_USB_PORT "usb_port"
#define DT_USB_HUB "usb_hub"
#define DT_USB_COPY "usb_copy"
#define DT_VBUS "vbus"
#define DT_SHARED "shared"
#define DT_SYNO_SPINUP_GROUP "syno_spinup_group"
#define DT_SYNO_SPINUP_GROUP_DELAY "syno_spinup_group_delay"
#define DT_HDD_POWERUP_SEQ "syno_hdd_powerup_seq"
#define DT_PROPERTY_SW_ACTIVITY "sw_activity"
#define DT_DISK_LED_TYPE_GPIO "gpio"
#define DT_FORM_FACTOR "form_factor"
#define DT_EXPANDER "expander"
#define DT_MODEL_NAME "model_name"
#define DT_SWITCHTEC "switchtec"
#define DT_LED_OFF_GPIO "led_off_gpio"
#define DT_I2C_BUS "i2c_bus"
#define DT_I2C_DEVICE "i2c_device"
#define DT_I2C_ADDRESS "i2c_address"
#define DT_I2C_DEVICE_NAME "i2c_device_name"
#define DT_DEVICE_INDEX "device_index"
#define DT_ACPI_HID "acpi_hid"
#define DT_ACPI_UID "acpi_uid"
#define DT_PHY_ID "phy_id"
#define DT_SAS "sas"

#define DT_SYSTEM_SLOT "system_slot"
#define DT_MV9XXX "mv9xxx"
#define DT_JMB585 "jmb585"
#define DT_ASM1061 "asm1061"
#define DT_ASM116x "asm116x"
#define DT_SIGNAL_DATA_GEN_FMT "signal_data_gen%d"
#define DT_SET_SSC_OFF "set_ssc_off"

#ifdef MY_DEF_HERE
#define DT_SYNO_HDD_SMBUS_TYPE "syno_smbus_hdd_type"
#define DT_SYNO_HDD_SMBUS_ADAPTER "syno_smbus_hdd_adapter"
#define DT_SYNO_HDD_SMBUS_ADDRESS "syno_smbus_hdd_address"

#define SMBUS_SWITCH_MAX_COUNT 16
#define DT_SYNO_SMBUS_SWITCH_ADAPTERS "syno_smbus_switch_adapters"
#define DT_SYNO_SMBUS_SWITCH_ADDRS "syno_smbus_switch_addrs"
#define DT_SYNO_SMBUS_SWITCH_VALS "syno_smbus_switch_vals"
#endif /* MY_DEF_HERE */

#ifdef MY_DEF_HERE
#define DT_SYNO_PMBUS_ADAPTER "syno_pmbus_adapter"
#define DT_SYNO_PMBUS_ADDRESS "syno_pmbus_address"
#define DT_SYNO_PMBUS_PIN_REG "syno_pmbus_pin_register"
#define DT_SYNO_PMBUS_POUT_REG "syno_pmbus_pout_register"
#define DT_SYNO_PMBUS_TEMP1_REG "syno_pmbus_temp1_register"
#define DT_SYNO_PMBUS_TEMP2_REG "syno_pmbus_temp2_register"
#define DT_SYNO_PMBUS_TEMP3_REG "syno_pmbus_temp3_register"
#define DT_SYNO_PMBUS_FAN_REG "syno_pmbus_fan_register"
#define DT_SYNO_PMBUS_STATUS_REG "syno_pmbus_status_register"
#define DT_SYNO_PMBUS_PSU_OFF_BIT "syno_pmbus_psu_off_bit"
#define DT_SYNO_PMBUS_PSU_PRESENT_BIT "syno_pmbus_psu_present_bit"
#endif /* MY_DEF_HERE */

#define SZ_DTS_EBOX_I2C_PWR_BTN "power_btn"
#define SZ_DTS_EBOX_I2C_OFFSET "offset"
#define SZ_DTS_EBOX_I2C_MASK "mask"
#define SZ_DTS_EBOX_I2C_PWR_CTL "power_control"
#define SZ_DTS_EBOX_I2C_SN_READ "ebox_sn_read"
#define SZ_DTS_EBOX_RP "rp_power"
#define SZ_DTS_EBOX_RP_INFO "rp_power_info"
#define SZ_DTS_EBOX_I2C_DEEPSELLP_CTL "deep_sleep_control"
#define SZ_DTS_EBOX_I2C_DEEPSELLP_INDICATOR "deep_sleep_indicator"
#define SZ_DTS_EBOX_I2C_REG_MANUAL_ENABLE "reg_manual_enable"

#define SYNO_DTS_PROPERTY_CONTENT_LENGTH 128 // If used to retrive PCIe path, can only accept 9 layer PCIe switch.
#define MAX_NODENAME_LEN 31

#ifdef MY_DEF_HERE
#define DT_AHCI_INTERNAL_MODE "internal_mode"
#endif /* MY_DEF_HERE */

#define DT_SEG7_NUM "seg7_num"
#define DT_SEG7_LED_MAP_0 "seg7_led_map_0"
#define DT_SEG7_LED_MAP_1 "seg7_led_map_1"
#define DT_SEG7_LED_MAP_2 "seg7_led_map_2"

/* This enum must sync with synosdk/fs.h for user space having same DISK_PORT_TYPE mapping */
typedef enum _tag_DISK_PORT_TYPE{
	UNKNOWN_DEVICE = 0,
	INTERNAL_DEVICE,
	EXTERNAL_SATA_DEVICE,
	EUNIT_DEVICE,
	EXTERNAL_USB_DEVICE,
	SYNOBOOT_DEVICE,
	ISCSI_DEVICE,
	CACHE_DEVICE,
	USB_HUB_DEVICE,
	SDCARD_DEVICE,
	INVALID_DEVICE,
	SYSTEM_DEVICE,
	DISK_PORT_TYPE_END,
} DISK_PORT_TYPE;

#endif /* MY_ABC_HERE */

#ifdef MY_ABC_HERE
typedef enum _tag_DISK_PWRCTRL_TYPE {
	PWRCTRL_TYPE_UNKNOWN = 0,
	PWRCTRL_TYPE_GPIO,
	PWRCTRL_TYPE_SMBUS,
	PWRCTRL_TYPE_END,
} DISK_PWRCTRL_TYPE;
#endif /* MY_ABC_HERE */

#ifdef MY_DEF_HERE
typedef struct _syno_smbus_hdd_powerctl {
        bool bl_init;
        int (*syno_smbus_hdd_enable_write)(int adapter, int address, int index, int val);
        int (*syno_smbus_hdd_enable_read)(int adapter, int address, int index);
        int (*syno_smbus_hdd_present_read)(int adapter, int address, int index);
        int (*syno_smbus_hdd_enable_write_all_once)(int adapter, int address);
} SYNO_SMBUS_HDD_POWERCTL;
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE
#define SYNO_SPINUP_GROUP_MAX 16
#define SYNO_SPINUP_GROUP_PIN_MAX_NUM 8
extern int g_syno_rp_detect_no;
extern int g_syno_rp_detect_list[SYNO_SPINUP_GROUP_PIN_MAX_NUM];
extern int g_syno_hdd_detect_no;
extern int g_syno_hdd_detect_list[SYNO_SPINUP_GROUP_PIN_MAX_NUM];
extern int g_syno_hdd_enable_no;
extern int g_syno_hdd_enable_list[SYNO_SPINUP_GROUP_PIN_MAX_NUM];
#endif /* MY_ABC_HERE */

#ifdef MY_DEF_HERE
#define PCI_ADDR_LEN_MAX 9
#define PCI_ADDR_NUM_MAX CONFIG_SYNO_PCI_MAX_SLOT
extern char gszPciAddrList[PCI_ADDR_NUM_MAX][PCI_ADDR_LEN_MAX];
extern int gPciAddrNum;
extern int syno_check_on_option_pci_slot(struct pci_dev *pdev);
#endif /* MY_DEF_HERE */

#if defined(MY_ABC_HERE) || defined(MY_ABC_HERE)
#define SYNOBIOS_EVENTDATA_NUM_MAX 8
typedef struct _synobios_event_parm_tag {
	unsigned long long data[SYNOBIOS_EVENTDATA_NUM_MAX];
} SYNOBIOS_EVENT_PARM;

typedef int (*FUNC_SYNOBIOS_EVENT)(SYNOBIOS_EVENT_PARM parms);

typedef struct _synobios_evnet_action_tag {
	unsigned long long synobios_event_type;
	SYNOBIOS_EVENT_PARM parms;
	struct list_head list;
} SYNOBIOS_EVENT_ACTION_LIST;
#endif /* MY_ABC_HERE || MY_ABC_HERE */

#ifdef MY_DEF_HERE
/*
 * Notice
 * ------
 *  Before calling syno_kexec_test() or reading kexex_test_flags, please
 *  ensure that syno_kexec_test_init() has been called.
 */
#define KEXEC_TEST_DECOMPRESSION	0	/* Did we skip compressed/head_64.S ? */
#define KEXEC_TEST_BOOTLOADER		1	/* Is bootloader type 0xD ? */
#define KEXEC_TEST_E820_TABLE		2	/* Is the minimal start address of usable memory in e820 table 0x100 ? */
#define KEXEC_TEST_SETUP_DATA		3	/* Did we receive setup_data with type SETUP_NONE or SETUP_EFI ? */

extern unsigned long kexec_test_flags;

/*
 * kexec_test_flags initializer.
 */
void __init syno_kexec_test_init(void);
/*
 * Test whether the above KEXEC_TEST_* bits are set.
 */
static __always_inline bool syno_kexec_test(int test)
{
	return 0 != test_bit(test, &kexec_test_flags);
}
#endif /* MY_DEF_HERE */

#ifdef MY_ABC_HERE
/**
 * How to use :
 * 1. module itself register the proprietary instance into the kernel
 *    by a predined MAGIC-key.
 * 2. Others can query the module registration by the same MAGIC-key
 *    and get the instance handle.
 * ********************************************************************
 * Beware of casting/handing "instance", you must know
 * what you are doing before accessing the instance.
 * ********************************************************************
 */
/* For plugin-instance registration */
int syno_plugin_register(int plugin_magic, void *instance);
int syno_plugin_unregister(int plugin_magic);
/* For getting the plugin-instance */
int syno_plugin_handle_get(int plugin_magic, void **hnd);
void * syno_plugin_handle_instance(void *hnd);
void syno_plugin_handle_put(void *hnd);

/* Magic definition */
#define EPIO_PLUGIN_MAGIC_NUMBER    0x20120815
#define RODSP_PLUGIN_MAGIC_NUMBER    0x20141111
#endif /* MY_ABC_HERE */

#endif //__SYNOLIB_H_
