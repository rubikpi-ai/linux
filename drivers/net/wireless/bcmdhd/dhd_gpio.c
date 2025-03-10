
#include <osl.h>
#include <dhd_linux.h>
#include <linux/gpio.h>
#ifdef BCMDHD_DTS
#include <linux/of_gpio.h>
#endif
#ifdef BCMDHD_PLATDEV
#include <linux/platform_device.h>
#endif

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>
#include <crypto/hash.h>
#include <crypto/kpp.h>
#include <crypto/dh.h>
#include <crypto/kdf_sp800108.h>
#include <linux/string.h>
#include <linux/skbuff.h>

#ifdef BCMDHD_DTS
/* This is sample code in dts file.
bcmdhd_wlan {
	compatible = "android,bcmdhd_wlan";
	gpio_wl_reg_on = <&gpio GPIOH_4 GPIO_ACTIVE_HIGH>;
	gpio_wl_host_wake = <&gpio GPIOZ_15 GPIO_ACTIVE_HIGH>;
};
*/
#define DHD_DT_COMPAT_ENTRY		"linux,bcmdhd_wlan"
#define GPIO_WL_REG_ON_PROPNAME		"gpio_wl_reg_on" ADAPTER_IDX_STR
#define GPIO_WL_HOST_WAKE_PROPNAME	"gpio_wl_host_wake" ADAPTER_IDX_STR
#endif
#define GPIO_WL_REG_ON_NAME 	"WL_REG_ON" ADAPTER_IDX_STR
#define GPIO_WL_HOST_WAKE_NAME	"WL_HOST_WAKE" ADAPTER_IDX_STR

#ifdef CONFIG_DHD_USE_STATIC_BUF
#if defined(BCMDHD_MDRIVER) && !defined(DHD_STATIC_IN_DRIVER)
extern void *dhd_wlan_mem_prealloc(uint bus_type, int index,
	int section, unsigned long size);
#else
extern void *dhd_wlan_mem_prealloc(int section, unsigned long size);
#endif
#endif /* CONFIG_DHD_USE_STATIC_BUF */

#if defined(BCMPCIE) && defined(PCIE_ATU_FIXUP)
extern void pcie_power_on_atu_fixup(void);
#endif

#define CONFIG_TXT "/usr/lib/firmware/config.txt"
#define NVRAM_TXT "/usr/lib/firmware/nvram.txt"
#define SHA256_HASH_SIZE 32

const unsigned char sha256_config[] = {
    0xbb, 0x35, 0xf2, 0xeb, 0x32, 0x81, 0x59, 0x7b,
    0x28, 0x43, 0x1b, 0x04, 0xc1, 0xc7, 0xf2, 0xbc,
    0x22, 0x38, 0xbb, 0x0d, 0xa7, 0xfc, 0xd6, 0x89,
    0x5b, 0xec, 0x47, 0x8e, 0x56, 0x6f, 0x5b, 0xf6
};

const unsigned char sha256_nvram[] = {
    0x58, 0x84, 0x30, 0xb4, 0xc2, 0xc1, 0x67, 0xca,
    0x03, 0x5f, 0x17, 0xda, 0x74, 0x78, 0xbe, 0xc3,
    0xd4, 0x92, 0x24, 0x87, 0x15, 0x26, 0x10, 0xd3,
    0x85, 0xd5, 0xcc, 0xb8, 0xf7, 0xc0, 0xa7, 0x5f
};

static long calculate_sha256(const char *filename, unsigned char *output) {

    struct file *file;
    struct shash_desc *sdesc;
    struct crypto_shash *tfm;
    unsigned char *buffer;
    size_t len;
    long rc;

    file = filp_open(filename, O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_err("Failed to open file: %ld\n", PTR_ERR(file));
        return PTR_ERR(file);
    }

    buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!buffer) {
        filp_close(file, NULL);
        return -ENOMEM;
    }

    len = kernel_read(file, buffer, PAGE_SIZE, &file->f_pos);
    if (len < 0) {
        pr_err("Failed to read file: %ld\n", len);
        kfree(buffer);
        filp_close(file, NULL);
        return len;
    }

    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm)) {
        pr_err("Failed to allocate SHA256 hash context\n");
        kfree(buffer);
        filp_close(file, NULL);
        return PTR_ERR(tfm);
    }

    sdesc = kmalloc(sizeof(*sdesc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!sdesc) {
        pr_err("Failed to allocate shash_desc\n");
        crypto_free_shash(tfm);
        kfree(buffer);
        filp_close(file, NULL);
        return -ENOMEM;
    }

    sdesc->tfm = tfm;

    rc = crypto_shash_init(sdesc);
    if (rc) {
        pr_err("SHA256 init failed\n");
        kfree(sdesc);
        crypto_free_shash(tfm);
        kfree(buffer);
        filp_close(file, NULL);
        return rc;
    }

    rc = crypto_shash_update(sdesc, buffer, len);
    if (rc) {
        pr_err("SHA256 update failed\n");
        kfree(sdesc);
        crypto_free_shash(tfm);
        kfree(buffer);
        filp_close(file, NULL);
        return rc;
    }

    rc = crypto_shash_final(sdesc, output);
    if (rc) {
        pr_err("SHA256 final failed\n");
    }

    kfree(sdesc);
    crypto_free_shash(tfm);
    kfree(buffer);
    filp_close(file, NULL);
    return rc;
}

static int
dhd_wlan_set_power(wifi_adapter_info_t *adapter, int on)
{
	int gpio_wl_reg_on = adapter->gpio_wl_reg_on;
	int err = 0;

	if (on) {
		printf("======== PULL WL_REG_ON(%d) HIGH! ========\n", gpio_wl_reg_on);
		if (gpio_wl_reg_on >= 0) {
			err = gpio_direction_output(gpio_wl_reg_on, 1);
			if (err) {
				printf("%s: WL_REG_ON didn't output high\n", __FUNCTION__);
				return -EIO;
			}
		}
#if defined(BCMPCIE) && defined(PCIE_ATU_FIXUP)
		mdelay(100);
		pcie_power_on_atu_fixup();
#endif
#ifdef BUS_POWER_RESTORE
#ifdef BCMPCIE
		if (adapter->pci_dev) {
			mdelay(100);
			printf("======== pci_set_power_state PCI_D0! ========\n");
			pci_set_power_state(adapter->pci_dev, PCI_D0);
			if (adapter->pci_saved_state)
				pci_load_and_free_saved_state(adapter->pci_dev, &adapter->pci_saved_state);
			pci_restore_state(adapter->pci_dev);
			err = pci_enable_device(adapter->pci_dev);
			if (err < 0)
				printf("%s: PCI enable device failed", __FUNCTION__);
			pci_set_master(adapter->pci_dev);
		}
#endif /* BCMPCIE */
#endif /* BUS_POWER_RESTORE */
		/* Lets customer power to get stable */
	} else {
#ifdef BUS_POWER_RESTORE
#ifdef BCMPCIE
		if (adapter->pci_dev) {
			printf("======== pci_set_power_state PCI_D3hot! ========\n");
			pci_save_state(adapter->pci_dev);
			adapter->pci_saved_state = pci_store_saved_state(adapter->pci_dev);
			if (pci_is_enabled(adapter->pci_dev))
				pci_disable_device(adapter->pci_dev);
			pci_set_power_state(adapter->pci_dev, PCI_D3hot);
		}
#endif /* BCMPCIE */
#endif /* BUS_POWER_RESTORE */
		printf("======== PULL WL_REG_ON(%d) LOW! ========\n", gpio_wl_reg_on);
		if (gpio_wl_reg_on >= 0) {
			err = gpio_direction_output(gpio_wl_reg_on, 0);
			if (err) {
				printf("%s: WL_REG_ON didn't output low\n", __FUNCTION__);
				return -EIO;
			}
		}
	}

	return err;
}

static int
dhd_wlan_set_reset(int onoff)
{
	return 0;
}

static int
dhd_wlan_set_carddetect(wifi_adapter_info_t *adapter, int present)
{
	int err = 0;

	if (present) {
#if defined(BCMSDIO)
		printf("======== Card detection to detect SDIO card! ========\n");
#ifdef CUSTOMER_HW_PLATFORM
		err = sdhci_force_presence_change(&sdmmc_channel, 1);
#endif /* CUSTOMER_HW_PLATFORM */
#elif defined(BCMPCIE)
		printf("======== Card detection to detect PCIE card! ========\n");
#endif
	} else {
#if defined(BCMSDIO)
		printf("======== Card detection to remove SDIO card! ========\n");
#ifdef CUSTOMER_HW_PLATFORM
		err = sdhci_force_presence_change(&sdmmc_channel, 0);
#endif /* CUSTOMER_HW_PLATFORM */
#elif defined(BCMPCIE)
		printf("======== Card detection to remove PCIE card! ========\n");
#endif
	}

	return err;
}

static int
dhd_wlan_get_mac_addr(wifi_adapter_info_t *adapter,
	unsigned char *buf, int ifidx)
{
	int err = -1;

	if (ifidx == 0) {
		/* Here is for wlan0 MAC address and please enable CONFIG_BCMDHD_CUSTOM_MAC in Makefile */
#ifdef EXAMPLE_GET_MAC
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
#endif /* EXAMPLE_GET_MAC */
	}
	else if (ifidx == 1) {
		/* Here is for wlan1 MAC address and please enable CUSTOM_MULTI_MAC in Makefile */
#ifdef EXAMPLE_GET_MAC
		struct ether_addr ea_example = {{0x02, 0x11, 0x22, 0x33, 0x44, 0x55}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
#endif /* EXAMPLE_GET_MAC */
	}
	else {
		printf("%s: invalid ifidx=%d\n", __FUNCTION__, ifidx);
	}

	printf("======== %s err=%d ========\n", __FUNCTION__, err);

	return err;
}

static struct cntry_locales_custom brcm_wlan_translate_custom_table[] = {
	/* Table should be filled out based on custom platform regulatory requirement */
#ifdef EXAMPLE_TABLE
	{"",   "XT", 49},  /* Universal if Country code is unknown or empty */
	{"US", "US", 0},
#endif /* EXMAPLE_TABLE */
};

#ifdef CUSTOM_FORCE_NODFS_FLAG
struct cntry_locales_custom brcm_wlan_translate_nodfs_table[] = {
#ifdef EXAMPLE_TABLE
	{"",   "XT", 50},  /* Universal if Country code is unknown or empty */
	{"US", "US", 0},
#endif /* EXMAPLE_TABLE */
};
#endif

static void *dhd_wlan_get_country_code(char *ccode
#ifdef CUSTOM_FORCE_NODFS_FLAG
	, u32 flags
#endif
)
{
	struct cntry_locales_custom *locales;
	int size;
	int i;

	if (!ccode)
		return NULL;

#ifdef CUSTOM_FORCE_NODFS_FLAG
	if (flags & WLAN_PLAT_NODFS_FLAG) {
		locales = brcm_wlan_translate_nodfs_table;
		size = ARRAY_SIZE(brcm_wlan_translate_nodfs_table);
	} else {
#endif
		locales = brcm_wlan_translate_custom_table;
		size = ARRAY_SIZE(brcm_wlan_translate_custom_table);
#ifdef CUSTOM_FORCE_NODFS_FLAG
	}
#endif

	for (i = 0; i < size; i++)
		if (strcmp(ccode, locales[i].iso_abbrev) == 0)
			return &locales[i];
	return NULL;
}

struct wifi_platform_data dhd_wlan_control = {
	.set_power	= dhd_wlan_set_power,
	.set_reset	= dhd_wlan_set_reset,
	.set_carddetect	= dhd_wlan_set_carddetect,
	.get_mac_addr	= dhd_wlan_get_mac_addr,
#ifdef CONFIG_DHD_USE_STATIC_BUF
	.mem_prealloc	= dhd_wlan_mem_prealloc,
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	.get_country_code = dhd_wlan_get_country_code,
};

static int
dhd_wlan_init_gpio(wifi_adapter_info_t *adapter)
{

	unsigned char sha256_config_out[SHA256_HASH_SIZE];
	unsigned char sha256_nvram_out[SHA256_HASH_SIZE];

	if (calculate_sha256(CONFIG_TXT, sha256_config_out) != 0) {
		return -EFAULT;
	}

	if (memcmp(sha256_config_out, sha256_config, sizeof(sha256_config_out)) != 0) {
		return -EFAULT;
	}

	if (calculate_sha256(NVRAM_TXT, sha256_nvram_out) != 0) {
		return -EFAULT;
	}

	if (memcmp(sha256_nvram_out, sha256_nvram, sizeof(sha256_nvram_out)) != 0) {
		return -EFAULT;
	}

#ifdef BCMDHD_DTS
	char wlan_node[32];
	struct device_node *root_node = NULL;
#endif
	int err = 0;
	int gpio_wl_reg_on = -1;
#ifdef CUSTOMER_OOB
	int gpio_wl_host_wake = -1;
	int host_oob_irq = -1;
	uint host_oob_irq_flags = 0;
#endif

	/* Please check your schematic and fill right GPIO number which connected to
	* WL_REG_ON and WL_HOST_WAKE.
	*/
#ifdef BCMDHD_DTS
#ifdef BCMDHD_PLATDEV
	if (adapter->pdev) {
		root_node = adapter->pdev->dev.of_node;
		strcpy(wlan_node, root_node->name);
	} else {
		printf("%s: adapter->pdev is NULL\n", __FUNCTION__);
		return -1;
	}
#else
	strcpy(wlan_node, DHD_DT_COMPAT_ENTRY);
	root_node = of_find_compatible_node(NULL, NULL, wlan_node);
#endif
	printf("======== Get GPIO from DTS(%s) ========\n", wlan_node);
	if (root_node) {
		gpio_wl_reg_on = of_get_named_gpio(root_node, GPIO_WL_REG_ON_PROPNAME, 0);
#ifdef CUSTOMER_OOB
		gpio_wl_host_wake = of_get_named_gpio(root_node, GPIO_WL_HOST_WAKE_PROPNAME, 0);
#endif
	} else
#endif
	{
		gpio_wl_reg_on = -1;
#ifdef CUSTOMER_OOB
		gpio_wl_host_wake = -1;
#endif
	}

	if (gpio_wl_reg_on >= 0) {
		err = gpio_request(gpio_wl_reg_on, GPIO_WL_REG_ON_NAME);
		if (err < 0) {
			printf("%s: gpio_request(%d) for WL_REG_ON failed %d\n",
				__FUNCTION__, gpio_wl_reg_on, err);
			gpio_wl_reg_on = -1;
		}
#if defined(BCMPCIE) && defined(PCIE_ATU_FIXUP)
		printf("======== PULL WL_REG_ON(%d) HIGH! ========\n", gpio_wl_reg_on);
		err = gpio_direction_output(gpio_wl_reg_on, 1);
		if (err) {
			printf("%s: WL_REG_ON didn't output high\n", __FUNCTION__);
			gpio_wl_reg_on = -1;
		} else {
			OSL_SLEEP(WIFI_TURNON_DELAY);
		}
		pcie_power_on_atu_fixup();
#endif
	}
	adapter->gpio_wl_reg_on = gpio_wl_reg_on;

#ifdef CUSTOMER_OOB
	adapter->gpio_wl_host_wake = -1;
	if (gpio_wl_host_wake >= 0) {
		err = gpio_request(gpio_wl_host_wake, GPIO_WL_HOST_WAKE_NAME);
		if (err < 0) {
			printf("%s: gpio_request(%d) for WL_HOST_WAKE failed %d\n",
				__FUNCTION__, gpio_wl_host_wake, err);
			return -1;
		}
		adapter->gpio_wl_host_wake = gpio_wl_host_wake;
		err = gpio_direction_input(gpio_wl_host_wake);
		if (err < 0) {
			printf("%s: gpio_direction_input(%d) for WL_HOST_WAKE failed %d\n",
				__FUNCTION__, gpio_wl_host_wake, err);
			gpio_free(gpio_wl_host_wake);
			return -1;
		}
		host_oob_irq = gpio_to_irq(gpio_wl_host_wake);
		if (host_oob_irq < 0) {
			printf("%s: gpio_to_irq(%d) for WL_HOST_WAKE failed %d\n",
				__FUNCTION__, gpio_wl_host_wake, host_oob_irq);
			gpio_free(gpio_wl_host_wake);
			return -1;
		}
	}

#ifdef HW_OOB
#ifdef HW_OOB_LOW_LEVEL
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL | IORESOURCE_IRQ_SHAREABLE;
#else
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE;
#endif
#else
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE | IORESOURCE_IRQ_SHAREABLE;
#endif
	host_oob_irq_flags &= IRQF_TRIGGER_MASK;

	adapter->irq_num = host_oob_irq;
	adapter->intr_flags = host_oob_irq_flags;
	printf("%s: WL_HOST_WAKE=%d, oob_irq=%d, oob_irq_flags=0x%x\n", __FUNCTION__,
		gpio_wl_host_wake, host_oob_irq, host_oob_irq_flags);
#endif /* CUSTOMER_OOB */
	printf("%s: WL_REG_ON=%d\n", __FUNCTION__, gpio_wl_reg_on);

	return 0;
}

static void
dhd_wlan_deinit_gpio(wifi_adapter_info_t *adapter)
{
	int gpio_wl_reg_on = adapter->gpio_wl_reg_on;
#ifdef CUSTOMER_OOB
	int gpio_wl_host_wake = adapter->gpio_wl_host_wake;
#endif

	if (gpio_wl_reg_on >= 0) {
		printf("%s: gpio_free(WL_REG_ON %d)\n", __FUNCTION__, gpio_wl_reg_on);
		gpio_free(gpio_wl_reg_on);
		adapter->gpio_wl_reg_on = -1;
	}
#ifdef CUSTOMER_OOB
	if (gpio_wl_host_wake >= 0) {
		printf("%s: gpio_free(WL_HOST_WAKE %d)\n", __FUNCTION__, gpio_wl_host_wake);
		gpio_free(gpio_wl_host_wake);
		adapter->gpio_wl_host_wake = -1;
	}
#endif /* CUSTOMER_OOB */
}

#if defined(BCMDHD_MDRIVER)
static void
dhd_wlan_init_adapter(wifi_adapter_info_t *adapter)
{
#ifdef ADAPTER_IDX
	adapter->index = ADAPTER_IDX;
	if (adapter->index == 0) {
		adapter->bus_num = 1;
		adapter->slot_num = 1;
	} else if (adapter->index == 1) {
		adapter->bus_num = 2;
		adapter->slot_num = 1;
	}
#ifdef BCMSDIO
	adapter->bus_type = SDIO_BUS;
#elif defined(BCMPCIE)
	adapter->bus_type = PCI_BUS;
#elif defined(BCMDBUS)
	adapter->bus_type = USB_BUS;
#endif
	printf("bus_type=%d, bus_num=%d, slot_num=%d\n",
		adapter->bus_type, adapter->bus_num, adapter->slot_num);
#endif /* ADAPTER_IDX */

#ifdef DHD_STATIC_IN_DRIVER
	adapter->index = 0;
#elif !defined(ADAPTER_IDX)
#ifdef BCMSDIO
	adapter->index = 0;
#elif defined(BCMPCIE)
	adapter->index = 1;
#elif defined(BCMDBUS)
	adapter->index = 2;
#endif
#endif /* DHD_STATIC_IN_DRIVER */
}
#endif /* BCMDHD_MDRIVER */

int
dhd_wlan_init_plat_data(wifi_adapter_info_t *adapter)
{
	int err = 0;

#ifdef BCMDHD_MDRIVER
	dhd_wlan_init_adapter(adapter);
#endif /* BCMDHD_MDRIVER */

	err = dhd_wlan_init_gpio(adapter);
	if (err)
		goto exit;

exit:
	return err;
}

void
dhd_wlan_deinit_plat_data(wifi_adapter_info_t *adapter)
{
	dhd_wlan_deinit_gpio(adapter);
}
