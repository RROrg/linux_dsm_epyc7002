/* Copyright (c) 2000-2021 Synology Inc. All rights reserved. */
#include <linux/synolib.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/synobios.h>
#include <linux/libata.h>

extern int syno_pciepath_dts_pattern_get(struct pci_dev *pdev, char *szPciePath, const int size);

/*
 * Return corresponding eunit device node for pci dev
 *
 * @param pdev [IN] syno pcie path
 *             [IN] exactly: true comapre the pcie path string and length
 *                           false compare the first n byte of the pcie path
 *
 * Return NULL : not found
 *        others: found and return pointer to device node
 */
struct device_node *syno_pcie_path_to_eunit_root_port(const char *pciepath, bool exactly)
{
	const char *eunit_pcie_root = NULL;
	struct device_node *ret = NULL;
	struct device_node *node = NULL;

	if (!pciepath) {
		goto END;
	}

	for_each_child_of_node(of_root, node) {
		if (!node->full_name) {
			goto PUT_NODE;
		}
		if (strncmp(node->full_name, DT_PCIE_EUNIT_SLOT, strlen(DT_PCIE_EUNIT_SLOT))) {
			goto PUT_NODE;
		}

		if (of_property_read_string(node, DT_PCIE_ROOT, &eunit_pcie_root)) {
			goto PUT_NODE;
		}

		if (0 == strncmp(eunit_pcie_root, pciepath, strlen(eunit_pcie_root))) {
			ret = node;
			goto END;
		}
PUT_NODE:
	}
END:
	if (exactly && ret) {
		if(strlen(eunit_pcie_root) != strlen(pciepath)) {
			ret = NULL;
		}
	}
	return ret;

}
EXPORT_SYMBOL(syno_pcie_path_to_eunit_root_port);

/*
 * Return if pci dev subsystem ID is synology eunit
 *
 * @param pdev [IN] PCI device structure
 * 	       [IN] string for eunit name
 * 	       NULL for any eunit, others for assigned eunit name
 *
 * Return NULL  : not found
 *        others: eunit device node pointer
 */
struct device_node *syno_pci_dev_to_eunit_node(struct pci_dev *pdev, char *eunit_name)
{
	u32 vendor_id = 0, device_id = 0;
	u32 dts_eunit_vendor_id = 0, dts_eunit_device_id = 0;
	struct device_node *ret = NULL;
	struct device_node *node = NULL;

	if (!pdev) {
		goto END;
	}
	vendor_id = pdev->subsystem_vendor;
	device_id = pdev->subsystem_device;

	for_each_child_of_node(of_root, node) {
		if (eunit_name) {
			if (0 != strcmp(eunit_name, node->full_name)) {
				goto PUT_NODE;
			}
		}
		if (0 != of_property_read_u32_index(node, DT_PCIE_EUNIT_SSID, 0, &dts_eunit_vendor_id)) {
			goto PUT_NODE;
		}

		if (0 != of_property_read_u32_index(node, DT_PCIE_EUNIT_SSID, 1, &dts_eunit_device_id)) {
			goto PUT_NODE;
		}

		if (vendor_id != dts_eunit_vendor_id || device_id != dts_eunit_device_id) {
			goto PUT_NODE;
		}

		ret = node;
		goto END;
PUT_NODE:
	}
END:
	return ret;
}
EXPORT_SYMBOL(syno_pci_dev_to_eunit_node);
/*
 * Return if pci dev subsystem ID is RX1224
 *
 * @param pdev [IN] PCI device structure
 *
 * Return true/false
 */
bool syno_is_pci_dev_rx1224rp(struct pci_dev *pdev)
{
	bool ret = false;
	struct device_node *node = syno_pci_dev_to_eunit_node(pdev, EBOX_INFO_UNIQUE_RX1224RP);

	if (node) {
		ret = true;
		of_node_put(node);
	}
	return ret;
}
EXPORT_SYMBOL(syno_is_pci_dev_rx1224rp);
/*
 * Return if ata port subsystem ID is RX1224
 *
 * @param pdev [IN] PCI device structure
 *
 * Return true/false
 */
bool syno_is_ap_rx1224rp(struct ata_port *ap)
{
	bool ret = false;
	struct pci_dev *pdev = NULL;

	if (ap && ap->host) {
		pdev = to_pci_dev(ap->host->dev);
	}
	if(!pdev) {
		goto END;
	}
	ret = syno_is_pci_dev_rx1224rp(pci_upstream_bridge(pdev));
END:
	return ret;
}
EXPORT_SYMBOL(syno_is_ap_rx1224rp);

/*
 * Return if pci dev subsystem ID is eunit entry device
 *
 * @param pdev [IN] PCI device structure
 *
 * Return true/false
 */
bool syno_is_pci_dev_eunit_entry(struct pci_dev *pdev)
{
	bool ret = false;

	if (!pdev) {
		goto END;
	}
	if (0 == pdev->syno_eunit_layer) {
		goto END;
	}
	ret = true;
END:
	return ret;
}
EXPORT_SYMBOL(syno_is_pci_dev_eunit_entry);

/*
 * Add/Remove disk name to pci dev device list
 *
 * @param pdev [IN] PCI device structure
 * 	       [IN] add 0 for remove, others for add
 * 	       [IN] string for disk name
 */
void syno_pci_dev_device_list_set(struct pci_dev *pdev, int add, const char *disk_name)
{
	struct syno_device_list *syno_device_node = NULL;

	if (NULL == pdev || NULL == disk_name) {
		return;
	}

	do {
		pdev = pci_upstream_bridge(pdev);
		if (syno_is_pci_dev_eunit_entry(pdev)) {
			break;
		}
	} while (pdev);

	if (!pdev) {
		return;
	}

	if (add) {
		syno_device_node = kzalloc(sizeof(struct syno_device_list), GFP_KERNEL);
		strncpy(syno_device_node->disk_name, disk_name, DISK_NAME_LEN);
		list_add(&syno_device_node->device_list, &pdev->syno_device_list);
	} else {
		// change to list_for_each_entry_safe
		list_for_each_entry(syno_device_node, &pdev->syno_device_list, device_list) {
			if (disk_name == syno_device_node->disk_name) {
				list_del(&syno_device_node->device_list);
				kfree(syno_device_node);
				break;
			}
		}
	}
}
EXPORT_SYMBOL(syno_libata_device_list_set);

/*
 * Return if the pci_dev support ASM2824 I2C and corresponding bus number.
 *
 * @param pdev [IN] PCI device structure
 *
 * Return -1: not found
 *        others: found and return value is bus number
 */
int syno_pci_dev_to_i2c_bus(struct pci_dev *pdev)
{
	int ret = -1;
	int index;
	struct device_node *eunit_node = NULL;
	char sztemp[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	struct of_phandle_args i2cNode;
	memset(&i2cNode, 0, sizeof(struct of_phandle_args));

	if (NULL == pdev || NULL == of_root) {
		goto END;
	}

	if (!pdev->syno_eunit_layer) {
		goto END;
	}

	syno_pciepath_dts_pattern_get(pdev, sztemp, SYNO_DTS_PROPERTY_CONTENT_LENGTH);

	eunit_node = syno_pcie_path_to_eunit_root_port(sztemp, false);
	if (!eunit_node || !eunit_node->full_name) {
		goto END;
	}
	if (of_parse_phandle_with_fixed_args(eunit_node, "i2c_bus", 0,
				pdev->syno_eunit_layer-1, &i2cNode)) {
		goto END;
	}
	if (1 != sscanf(i2cNode.np->full_name, DT_I2C_BUS"@%d", &index)) {
		goto END;
	}
	ret = index;

END:
	if (eunit_node) {
		of_node_put(eunit_node);
	}
	return ret;
}

/*
 * comapre the tail of pciepath with port
 * and cut the tail of pciepath when the same
 *
 * @param pdev [IN] PCI device path string
 * 	       [IN] port string
 *
 * Return 0 for the same, -1 for not different
 */
int syno_pciepath_port_compare(char *path, const char *port)
{
	int ret = -1;
	size_t path_len, port_len;

	if (!path || !port) {
		goto END;
	}

	path_len = strlen(path);
	port_len = strlen(port);

	if (port_len >= path_len) {
		goto END;
	}

	if(0 != strcmp(path+path_len-port_len, port)) {
		goto END;
	}
	path[path_len-port_len] = 0;

	ret = 0;
END:
	return ret;
}

/*
 * jump to correspoding upstream by port string length
 * ex ",00.0" five char so jump one layer
 *
 * @param pdev [IN] PCI device path string
 * 	       [IN] port string
 *
 * Return NULL for unable to jump, others the PCI device struct after jump
 */
struct pci_dev *syno_pcie_dev_port_jump(struct pci_dev *pdev, const char *port)
{
	int i;
	struct pci_dev *ret = NULL;
	size_t port_len;

	if (!pdev || !port) {
		goto END;
	}

	port_len = strlen(port);

	for (i = 0; i < port_len/5; ++i) {
		pdev = pci_upstream_bridge(pdev);
		if (!pdev) {
			goto END;
		}
	}

	ret = pdev;
END:
	return ret;
}

/*
 * comapre with port and cut the tail of pciepath when the same
 * then jump to correspoding upstream by port string length
 *
 * @param pdev [IN] PCI device structure
 * 	       [IN] PCI device path string
 * 	       [IN] port string
 *
 * Return NULL for compare/jump fail, others the PCI device struct after comapre/jump
 */
struct pci_dev *syno_pci_dev_port_compare_and_jump(struct pci_dev *pdev, char *pciepath, const char *port_name)
{
	const char *port = NULL;
	struct device_node *eunit_node = NULL;
	struct pci_dev *ret = NULL;

	eunit_node = syno_pci_dev_to_eunit_node(pdev, NULL);
	if (!eunit_node) {
		goto END;
	}
	if (of_property_read_string(eunit_node, port_name, &port)) {
		dev_err(&pdev->dev, "eunit node %s does not customized %s\n",
			eunit_node->full_name, port_name);
		goto END;
	}

	if(0 != syno_pciepath_port_compare(pciepath, port)) {
		goto END;
	}
	ret = syno_pcie_dev_port_jump(pdev, port);
END:
	if (eunit_node) {
		of_node_put(eunit_node);
	}
	return ret;
}

/*
 * Return pci dev eunit layer number
 * will also update syno_eunit_layer in pci dev
 *
 * @param pdev [IN] PCI device structure
 *
 * Return 0 : on eunit
 * 	  -1: not on eunit
 */
unsigned int syno_pci_dev_to_layer(struct pci_dev *pdev)
{
	int ret = -1;
	struct device_node *eunit_node = NULL;
	char pciepath[SYNO_DTS_PROPERTY_CONTENT_LENGTH]={0};
	struct pci_dev *upstream = NULL;

	if (!pdev) {
		goto END;
	}
	pdev->syno_eunit_layer = 0;
	syno_pciepath_dts_pattern_get(pdev, pciepath, SYNO_DTS_PROPERTY_CONTENT_LENGTH);

	upstream = syno_pci_dev_port_compare_and_jump(pdev, pciepath, DT_PCIE_EUNIT_MASTER_PORT);
	if (!upstream) {
		goto END;
	}

	eunit_node = syno_pcie_path_to_eunit_root_port(pciepath, true);
	if (NULL != eunit_node) {
		pdev->syno_eunit_layer = 1;
		ret = 0;
		goto END;
	}

	upstream = syno_pci_dev_port_compare_and_jump(upstream, pciepath, DT_PCIE_EUNIT_NEXT_PORT);
	if (!upstream) {
		goto END;
	}

	pdev->syno_eunit_layer = upstream->syno_eunit_layer+1;

	ret = 0;
END:
	if (eunit_node) {
		of_node_put(eunit_node);
	}
	return ret;
}

u8
syno_is_synology_pci_eunit(const struct ata_port *ap)
{
	u8 ret = 0;
	if (!ap) {
		goto END;
	}
	if (IS_SYNOLOGY_RX1224RP(ap->PMSynoUnique)) {
		ret = 1;
		goto END;
	}
END:
	return ret;
}
EXPORT_SYMBOL(syno_is_synology_pci_eunit);

int syno_pci_dev_to_eunit_index(struct pci_dev *pdev)
{
	struct device_node *pEunitNode = NULL;
	int iRet = -1, iIndex = 0;
	char sztemp[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};

	while (pdev) {
		pdev = pci_upstream_bridge(pdev);
		if (syno_is_pci_dev_eunit_entry(pdev)) {
			break;
		}
	}

	if (!pdev) {
		goto END;
	}

	syno_pciepath_dts_pattern_get(pdev, sztemp, SYNO_DTS_PROPERTY_CONTENT_LENGTH);
	pEunitNode = syno_pcie_path_to_eunit_root_port(sztemp, false);

        if (0 != of_property_read_u32_index(pEunitNode, DT_PCIE_EUNIT_PORT, 0, &iIndex)) {
                printk(KERN_ERR "%s read phy from %s node in dts error\n", __func__, DT_PCIE_EUNIT_PORT);
                goto END;
        }

	iRet = pdev->syno_eunit_layer + iIndex - 1;

END:
	if (pEunitNode) {
		of_node_put(pEunitNode);
	}
	return iRet;
}

void syno_pci_eunit_unique_fill(struct ata_port *ap)
{
	if (syno_is_ap_rx1224rp(ap)) {
		ap->PMSynoUnique = SYNOLOGY_RX1224RP_ID;
		goto END;
	}
END:
	return;
}
EXPORT_SYMBOL(syno_pci_eunit_unique_fill);

int syno_ap_to_port_index(const struct ata_port *ap)
{
	int iRet = -1;
	struct pci_dev *pdev = NULL;
	struct device_node *pSlotNode = NULL;
	struct device_node *pAhciNode = NULL;
	struct device_node *pEunitNode = NULL;
	u32 ata_port_no = U32_MAX;
	if (ap && ap->host) {
		pdev = to_pci_dev(ap->host->dev);
	}
	if (!pdev) {
		goto END;
	}
	pEunitNode = syno_pci_dev_to_eunit_node(pci_upstream_bridge(pdev), NULL);
	if (!pEunitNode) {
		goto END;
	}
	for_each_child_of_node(pEunitNode, pSlotNode) {

		/* Skip non-disk slot */
		if ((0 != strncmp(pSlotNode->full_name, DT_STORAGE_SLOT, strlen(DT_STORAGE_SLOT)))) {
			continue;
		}

		/* Get AHCI node */
		if (NULL == (pAhciNode = of_get_child_by_name(pSlotNode, DT_AHCI))) {
			printk("Can not get ahci node: %s\n", pSlotNode->full_name);
			continue;
		}

		/* Match PCIe path */
		if (0 != syno_compare_dts_eunit_pciepath(pdev, pAhciNode)) {
			continue;
		}

		/* Get ATA port index */
		if (0 != of_property_read_u32_index(pAhciNode, DT_ATA_PORT, 0, &ata_port_no)) {
			continue;
		}

		/* Check ATA port index is vaild */
		if (ap->port_no != ata_port_no) {
			continue;
		}

		if (1 != sscanf(pSlotNode->full_name, DT_STORAGE_SLOT"@%d", &iRet)) {
			iRet = -1;
		}

		of_node_put(pSlotNode);
		break;
	}
END:
	if (pEunitNode) {
		of_node_put(pEunitNode);
	}
	return iRet;
}
EXPORT_SYMBOL(syno_ap_to_port_index);

/* this function will return the pci eunit device node for ata port
 * @param ap    [IN]  the ata port
 *
 * @return NULL: not found
 *         others: pointer to device node
 */
struct device_node *syno_ap_to_eunit_node(struct ata_port *ap)
{
	struct pci_dev *pdev = NULL;
	struct device_node *pRet = NULL;
	if (ap && ap->host) {
		pdev = to_pci_dev(ap->host->dev);
	}
	if (!pdev) {
		goto END;
	}
	pRet = syno_pci_dev_to_eunit_node(pci_upstream_bridge(pdev), NULL);
END:
	return pRet;
}
EXPORT_SYMBOL(syno_ap_to_eunit_node);

/* this function will return the pci eunit i2c addr for smbus type
 * @param ap    [IN]  the ata port
 *
 * @return NULL: not found
 *         others: smbus type
 */
static char *syno_ap_to_pci_eunit_smbus_type(struct ata_port *ap)
{
	char *szRet = NULL;
	struct device_node *pEunitNode = NULL;

	pEunitNode = syno_ap_to_eunit_node(ap);
	if (!pEunitNode) {
		goto END;
	}
	szRet = (char *)of_get_property(pEunitNode, DT_SYNO_HDD_SMBUS_TYPE, NULL);
END:
	if (pEunitNode) {
		of_node_put(pEunitNode);
	}
	return szRet;
}

/* this function will return the pci eunit i2c bus for ata port
 * @param ap    [IN]  the ata port
 *
 * @return 0: not found
 *         others: i2c bus number
 */
int syno_ap_to_pci_eunit_i2c_bus(const struct ata_port *ap)
{
	int iRet = -1;
	struct pci_dev *pdev = NULL;
	if (ap && ap->host) {
		pdev = to_pci_dev(ap->host->dev);
	}

	while (pdev) {
		iRet = syno_pci_dev_to_i2c_bus(pdev);
		if (iRet > 0) {
			goto END;
		}
		pdev = pci_upstream_bridge(pdev);
	}
END:
	return iRet;
}
EXPORT_SYMBOL(syno_ap_to_pci_eunit_i2c_bus);

/* this function will return the pci eunit i2c addr for ata port
 * @param ap    [IN]  the ata port
 *
 * @return 0: not found
 *         others: i2c bus number
 */
static int syno_ap_to_pci_eunit_i2c_addr(struct ata_port *ap)
{
	int iRet = -1;
	struct device_node *pEunitNode = NULL;
	if (!ap) {
		goto END;
	}
	pEunitNode = syno_ap_to_eunit_node(ap);
	if (!pEunitNode) {
		goto END;
	}

	if (of_property_read_u32_index(pEunitNode, DT_SYNO_HDD_SMBUS_ADDRESS, 0, &iRet)) {
		goto END;
	}
END:
	if (pEunitNode) {
		of_node_put(pEunitNode);
	}
	return iRet;
}

/* this function will set the pci eunit ata power smbus type 'microp'
 * @param ap    [IN]  the ata port
 *        pwrOp [IN]  1: on, 0: off
 *
 * @return 0: failed
 *         1: success
 */
static int syno_microp_pci_eunit_slot_power_ctl(struct ata_port *ap, u8 pwrOp)
{
	int iRet = 0;
	int iBus, iAddr;
	if (!ap) {
		goto END;
	}
	iAddr = syno_ap_to_pci_eunit_i2c_addr(ap);
	if (0 >= iAddr) {
		goto END;
	}

	iBus = syno_ap_to_pci_eunit_i2c_bus(ap);
	if (0 >= iBus) {
		goto END;
	}

	if (syno_microp_hdd_auto_enable_manual_disable(iBus, iAddr, pwrOp)) {
		goto END;
	}

	iRet = 1;
END:
	return iRet;
}

/* this function will set the pci eunit ata power
 * @param ap    [IN]  the ata port
 *        pwrOp [IN]  1: on, 0: off
 *
 * @return 0: failed
 *         1: success
 */
int syno_pci_eunit_slot_power_ctl(struct ata_port *ap, u8 pwrOp)
{
	char *szSmbusType = NULL;
	int iRet = 0;
	if (!ap) {
		goto END;
	}

	szSmbusType = syno_ap_to_pci_eunit_smbus_type(ap);
	if (!szSmbusType) {
		goto END;
	}

	if (0 == strcmp(szSmbusType, "microp")) {
		if(!syno_microp_pci_eunit_slot_power_ctl(ap, pwrOp)) {
			goto END;
		}
	} else {
		goto END;
	}
	iRet = 1;
END:
	return iRet;
}
EXPORT_SYMBOL(syno_pci_eunit_slot_power_ctl);

int syno_pci_eunit_index_get(const struct ata_port *ap)
{
	int index = -1;
	struct pci_dev *pdev = NULL;
	if (ap && ap->host) {
		pdev = to_pci_dev(ap->host->dev);
	}
	if (!pdev) {
		goto END;
	}
	index = syno_pci_dev_to_eunit_index(pdev);
END:
	return index;
}
EXPORT_SYMBOL(syno_pci_eunit_index_get);
