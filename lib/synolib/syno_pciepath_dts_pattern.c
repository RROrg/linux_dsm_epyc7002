#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/synolib.h>
#include <linux/of.h>
/**
 * syno_pciepath_dts_pattern_get - save pciepath of pdev into szPciePath which is a char array with length "size"
 *                                 format root_bus:device.function,device.function,...
 *                                 only keep root_bus, because bus of child layer may change
 * @pdev [IN]- the pcie device
 * @szPciePath [IN/OUT]- the char array for saving pciepath. If failed, array content is cleared.
 * @size [IN]- the length of szPciePath
 *
 * return 0: success
 *       -1: failed
 */
int syno_pciepath_dts_pattern_get(struct pci_dev *pdev, char *szPciePath, const int size)
{
	int ret = -1;
	struct pci_dev *pDevUpstream = NULL;
	char szTmp[SYNO_DTS_PROPERTY_CONTENT_LENGTH]={0};
	if (NULL == pdev || NULL == szPciePath || 0 >= size || SYNO_DTS_PROPERTY_CONTENT_LENGTH < size) {
		goto END;
	}

	while (NULL != pdev) {
		if (NULL == pdev->bus) {
			goto END;
		}

		pDevUpstream = pci_upstream_bridge(pdev);
		if (NULL == pDevUpstream) {
			/* pdev is pcie root */
			if (0 == *szPciePath) {
#ifdef MY_ABC_HERE
				snprintf(szPciePath, size, "%04x:%02x:%02x.%x", pci_domain_nr(pdev->bus),
						pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
#else /* MY_ABC_HERE */
				snprintf(szPciePath, size, "%02x:%02x.%x", pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
#endif /* MY_ABC_HERE */
			} else {
				/* Concatenate child pcie function and device */
				strncpy(szTmp, szPciePath, size);
#ifdef MY_ABC_HERE
				snprintf(szPciePath, size, "%04x:%02x:%02x.%x,%s", pci_domain_nr(pdev->bus),
						pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7, szTmp);
#else /* MY_ABC_HERE */
				snprintf(szPciePath, size, "%02x:%02x.%x,%s", pdev->bus->number, (pdev->devfn) >> 3, (pdev->devfn) & 0x7, szTmp);
#endif /* MY_ABC_HERE */
			}
			break;
		}

		if (0 == *szPciePath) {
			snprintf(szPciePath, size, "%02x.%x", (pdev->devfn) >> 3, (pdev->devfn) & 0x7);
		} else {
			strncpy(szTmp, szPciePath, size);
			snprintf(szPciePath, size, "%02x.%x,%s", (pdev->devfn) >> 3, (pdev->devfn) & 0x7, szTmp);
		}

		pdev = pDevUpstream;
	}
	ret = 0;
END:
	if (-1 == ret) {
		memset(szPciePath, 0, size);
	}
	return ret;
}
EXPORT_SYMBOL(syno_pciepath_dts_pattern_get);

#ifdef MY_ABC_HERE
int syno_compare_dts_pciepath(struct pci_dev *pdev, const struct device_node *pDeviceNode)
{
	int ret = -1;
	char szDevPciePath[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	char *szDtsNodePciePath = NULL;
	szDtsNodePciePath = (char *)of_get_property(pDeviceNode, DT_PCIE_ROOT, NULL);

	if (NULL == szDtsNodePciePath) {
		printk(KERN_ERR "%s: Read pcie_root from dts error\n", __func__);
		goto END;
	}
	if (-1 == syno_pciepath_dts_pattern_get(pdev, szDevPciePath, SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		goto END;
	}
	if (0 == strncmp(szDtsNodePciePath, szDevPciePath, SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		ret = 0;
	}
END:
	return ret;
}
EXPORT_SYMBOL(syno_compare_dts_pciepath);

#ifdef MY_DEF_HERE
extern struct device_node *syno_pcie_path_to_eunit_root_port(const char *pciepath, bool exactly);
int syno_compare_dts_eunit_pciepath(struct pci_dev *pdev, const struct device_node *pDeviceNode)
{
	int ret = -1;
	char szDevPciePath[SYNO_DTS_PROPERTY_CONTENT_LENGTH] = {'\0'};
	char *szDtsNodePciePath = NULL;
	int offset = -1;
	szDtsNodePciePath = (char *)of_get_property(pDeviceNode, DT_PCIE_ROOT, NULL);

	if (NULL == szDtsNodePciePath) {
		printk(KERN_ERR "%s: Read pcie_root from dts error\n", __func__);
		goto END;
	}
	if (-1 == syno_pciepath_dts_pattern_get(pdev, szDevPciePath, SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		goto END;
	}
	if (NULL == syno_pcie_path_to_eunit_root_port(szDevPciePath, false)) {
		goto END;
	}
	offset=strlen(szDevPciePath)-strlen(szDtsNodePciePath);
	if (0 > offset) {
		goto END;
	}
	if (0 == strncmp(szDtsNodePciePath, szDevPciePath+offset, SYNO_DTS_PROPERTY_CONTENT_LENGTH)) {
		ret = 0;
	}
END:
	return ret;
}
EXPORT_SYMBOL(syno_compare_dts_eunit_pciepath);
#endif /* MY_DEF_HERE */
#endif /* MY_ABC_HERE */
