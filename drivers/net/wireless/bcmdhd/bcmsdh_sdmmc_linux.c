/*
 * BCMSDH Function Driver for the native SDIO/MMC driver in the Linux Kernel
 *
 * Copyright (C) 2024 Synaptics Incorporated. All rights reserved.
 *
 * This software is licensed to you under the terms of the
 * GNU General Public License version 2 (the "GPL") with Broadcom special exception.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION
 * DOES NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES,
 * SYNAPTICS' TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT
 * EXCEED ONE HUNDRED U.S. DOLLARS
 *
 * Copyright (C) 2024, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <typedefs.h>
#include <bcmutils.h>
#include <sdio.h>	/* SDIO Device and Protocol Specs */
#include <bcmsdbus.h>	/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>	/* to get msglevel bit values */

#include <linux/sched.h>	/* request_irq() */

#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <dhd_linux.h>
#include <bcmsdh_sdmmc.h>
#include <dhd_dbg.h>
#include <bcmdevs.h>

#if !defined(SDIO_VENDOR_ID_BROADCOM)
#define SDIO_VENDOR_ID_BROADCOM		0x02d0
#endif /* !defined(SDIO_VENDOR_ID_BROADCOM) */

#if !defined(SDIO_VENDOR_ID_SYNAPTICS)
#define SDIO_VENDOR_ID_SYNAPTICS	0x06cb
#endif /* !defined(SDIO_VENDOR_ID_SYNAPTICS) */

#define SDIO_DEVICE_ID_BROADCOM_DEFAULT	0x0000

extern void wl_cfg80211_set_parent_dev(void *dev);
extern void sdioh_sdmmc_devintr_off(sdioh_info_t *sd);
extern void sdioh_sdmmc_devintr_on(sdioh_info_t *sd);
extern void* bcmsdh_probe(osl_t *osh, void *dev, void *sdioh, void *adapter_info, uint bus_type,
	uint bus_num, uint slot_num);
extern int bcmsdh_remove(bcmsdh_info_t *bcmsdh);

int sdio_function_init(void);
void sdio_function_cleanup(void);

#define DESCRIPTION "bcmsdh_sdmmc Driver"
#define AUTHOR "Broadcom Corporation"

/* module param defaults */
static int clockoverride = 0;

module_param(clockoverride, int, 0644);
MODULE_PARM_DESC(clockoverride, "SDIO card clock override");

#ifdef GLOBAL_SDMMC_INSTANCE
PBCMSDH_SDMMC_INSTANCE gInstance;
#endif

/* Maximum number of bcmsdh_sdmmc devices supported by driver */
#define BCMSDH_SDMMC_MAX_DEVICES 1

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) && defined(CONFIG_PM_SLEEP)
extern volatile bool dhd_mmc_suspend;
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) && defined(CONFIG_PM_SLEEP) */

static int sdioh_probe(struct sdio_func *func)
{
	int host_idx = func->card->host->index;
	uint32 rca = func->card->rca;
	wifi_adapter_info_t *adapter;
	osl_t *osh = NULL;
	sdioh_info_t *sdioh = NULL;

	sd_err(("bus num (host idx)=%d, slot num (rca)=%d, caps=0x%x\n",
		host_idx, rca, func->card->host->caps));
	adapter = dhd_wifi_platform_get_adapter(SDIO_BUS, host_idx, rca);
	if (adapter != NULL) {
		sd_err(("found adapter info '%s'\n", adapter->name));
		adapter->bus_type = SDIO_BUS;
		adapter->bus_num = host_idx;
		adapter->slot_num = rca;
		adapter->sdio_func = func;
	} else {
		sd_err(("can't find adapter info for this chip\n"));
#ifdef ADAPTER_IDX
		goto fail;
#endif
	}

#ifdef WL_CFG80211
	wl_cfg80211_set_parent_dev(&func->dev);
#endif

	/* allocate SDIO Host Controller state info */
	osh = osl_attach(&func->dev, SDIO_BUS, TRUE);
	if (osh == NULL) {
		sd_err(("%s: osl_attach failed\n", __FUNCTION__));
		goto fail;
	}
	osl_static_mem_init(osh, adapter);
	sdioh = sdioh_attach(osh, func);
	if (sdioh == NULL) {
		sd_err(("%s: sdioh_attach failed\n", __FUNCTION__));
		goto fail;
	}
	if (!(func->card->host->caps & MMC_CAP_NONREMOVABLE)) {
		sd_err(("%s: MMC_CAP_NONREMOVABLE not enabled in SDIO driver\n", __FUNCTION__));
//		func->card->host->caps |= MMC_CAP_NONREMOVABLE;
	}
	if ((func->card->host->caps & MMC_CAP_NEEDS_POLL)) {
		sd_err(("%s: MMC_CAP_NEEDS_POLL enabled in SDIO driver\n", __FUNCTION__));
//		func->card->host->caps &= ~MMC_CAP_NEEDS_POLL;
	}
	sdioh->bcmsdh = bcmsdh_probe(osh, &func->dev, sdioh, adapter, SDIO_BUS, host_idx, rca);
	if (sdioh->bcmsdh == NULL) {
		sd_err(("%s: bcmsdh_probe failed\n", __FUNCTION__));
		goto fail;
	}

	sdio_set_drvdata(func, sdioh);
	return 0;

fail:
	if (sdioh != NULL)
		sdioh_detach(osh, sdioh);
	if (osh != NULL)
		osl_detach(osh);
	return -ENOMEM;
}

static void sdioh_remove(struct sdio_func *func)
{
	sdioh_info_t *sdioh;
	osl_t *osh;

	sdioh = sdio_get_drvdata(func);
	if (sdioh == NULL) {
		sd_err(("%s: error, no sdioh handler found\n", __FUNCTION__));
		return;
	}
	sd_err(("%s: Enter\n", __FUNCTION__));

	osh = sdioh->osh;
	bcmsdh_remove(sdioh->bcmsdh);
	sdioh_detach(osh, sdioh);
	osl_detach(osh);
}

static int bcmsdh_sdmmc_probe(struct sdio_func *func,
                              const struct sdio_device_id *id)
{
	int ret = 0;

	if (func == NULL)
		return -EINVAL;

	sd_err(("%s: Enter num=%d\n", __FUNCTION__, func->num));
	sd_info(("sdio_bcmsdh: func->class=%x\n", func->class));
	sd_info(("sdio_vendor: 0x%04x\n", func->vendor));
	sd_info(("sdio_device: 0x%04x\n", func->device));
	sd_info(("Function#: 0x%04x\n", func->num));

#ifdef GLOBAL_SDMMC_INSTANCE
	gInstance->func[func->num] = func;
#endif

	/* 4318 doesn't have function 2 */
	if ((func->num == 2) || (func->num == 1 && func->device == 0x4))
		ret = sdioh_probe(func);

	return ret;
}

static void bcmsdh_sdmmc_remove(struct sdio_func *func)
{
	if (func == NULL) {
		sd_err(("%s is called with NULL SDIO function pointer\n", __FUNCTION__));
		return;
	}

	sd_trace(("bcmsdh_sdmmc: %s Enter\n", __FUNCTION__));
	sd_info(("sdio_bcmsdh: func->class=%x\n", func->class));
	sd_info(("sdio_vendor: 0x%04x\n", func->vendor));
	sd_info(("sdio_device: 0x%04x\n", func->device));
	sd_info(("Function#: 0x%04x\n", func->num));

	if ((func->num == 2) || (func->num == 1 && func->device == 0x4))
		sdioh_remove(func);
}

/* devices we support, null terminated */
static const struct sdio_device_id bcmsdh_sdmmc_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_DEFAULT) },
	/* XXX This should not be in the external release, as it will attach to any SDIO
	 * device, even non-WLAN devices.
	 * Need to add IDs for the FALCON-based chips and put this under BCMINTERNAL
	 { SDIO_DEVICE_CLASS(SDIO_CLASS_NONE) },
	 */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM4362_CHIP_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43751_CHIP_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43752_CHIP_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43756_CHIP_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43012_CHIP_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43014_CHIP_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43014_D11N_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43014_D11N2G_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43014_D11N5G_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43013_CHIP_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43013_D11N_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43013_D11N2G_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM43013_D11N5G_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_ANY_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM4381_CHIP_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, BCM4382_CHIP_ID) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_SYNAPTICS, BCM43711_CHIP_ID) },
	{ SDIO_DEVICE_CLASS(SDIO_CLASS_NONE)		},
	{ 0, 0, 0, 0 /* end: all zeroes */
	},
};

MODULE_DEVICE_TABLE(sdio, bcmsdh_sdmmc_ids);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) && defined(CONFIG_PM_SLEEP)
static int bcmsdh_sdmmc_suspend(struct device *pdev)
{
	int err;
	sdioh_info_t *sdioh;
	struct sdio_func *func = dev_to_sdio_func(pdev);
	mmc_pm_flag_t sdio_flags;

	printf("%s Enter func->num=%d\n", __FUNCTION__, func->num);
	if (func->num != 2)
		return 0;

	dhd_mmc_suspend = TRUE;
	sdioh = sdio_get_drvdata(func);
	err = bcmsdh_suspend(sdioh->bcmsdh);
	if (err) {
		printf("%s bcmsdh_suspend err=%d\n", __FUNCTION__, err);
		dhd_mmc_suspend = FALSE;
		return err;
	}

	sdio_flags = sdio_get_host_pm_caps(func);
	if (!(sdio_flags & MMC_PM_KEEP_POWER)) {
		sd_err(("%s: can't keep power while host is suspended\n", __FUNCTION__));
		dhd_mmc_suspend = FALSE;
		return  -EINVAL;
	}

	/* keep power while host suspended */
	err = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (err) {
		sd_err(("%s: error while trying to keep power\n", __FUNCTION__));
		dhd_mmc_suspend = FALSE;
		return err;
	}
	smp_mb();

	printf("%s Exit\n", __FUNCTION__);
	return 0;
}

static int bcmsdh_sdmmc_resume(struct device *pdev)
{
	sdioh_info_t *sdioh;
	struct sdio_func *func = dev_to_sdio_func(pdev);

	printf("%s Enter func->num=%d\n", __FUNCTION__, func->num);
	if (func->num != 2)
		return 0;

	dhd_mmc_suspend = FALSE;
	sdioh = sdio_get_drvdata(func);
	bcmsdh_resume(sdioh->bcmsdh);

	smp_mb();
	printf("%s Exit\n", __FUNCTION__);
	return 0;
}

#ifdef DEVICE_PM_CALLBACK
static int bcmsdh_sdmmc_prepare(struct device *pdev)
{
	int err;
	sdioh_info_t *sdioh;
	struct sdio_func *func = dev_to_sdio_func(pdev);

	printf("%s Enter func->num=%d\n", __FUNCTION__, func->num);
	if (func->num != 2)
		return 0;

	sdioh = sdio_get_drvdata(func);
	err = bcmsdh_prepare(sdioh->bcmsdh);
	if (err) {
		printf("%s bcmsdh_suspend err=%d\n", __FUNCTION__, err);
		return err;
	}

	printf("%s Exit\n", __FUNCTION__);
	return 0;
}

static void bcmsdh_sdmmc_complete(struct device *pdev)
{
	sdioh_info_t *sdioh;
	struct sdio_func *func = dev_to_sdio_func(pdev);

	printf("%s Enter func->num=%d\n", __FUNCTION__, func->num);
	if (func->num != 2)
		return;

	sdioh = sdio_get_drvdata(func);
	bcmsdh_complete(sdioh->bcmsdh);

	printf("%s Exit\n", __FUNCTION__);
	return;
}
#endif /* DEVICE_PM_CALLBACK */

static const struct dev_pm_ops bcmsdh_sdmmc_pm_ops = {
	.suspend	= bcmsdh_sdmmc_suspend,
	.resume		= bcmsdh_sdmmc_resume,
#ifdef DEVICE_PM_CALLBACK
	.prepare	= bcmsdh_sdmmc_prepare,
	.complete	= bcmsdh_sdmmc_complete,
#endif /* DEVICE_PM_CALLBACK */
};
#endif  /* (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) && defined(CONFIG_PM_SLEEP) */

#if defined(BCMLXSDMMC)
static struct semaphore *notify_semaphore = NULL;

static int dummy_probe(struct sdio_func *func,
                              const struct sdio_device_id *id)
{
	if (func)
		sd_err(("%s: func->num=0x%x; \n", __FUNCTION__, func->num));
	if (id) {
		if ((id->vendor != SDIO_VENDOR_ID_BROADCOM) &&
			(id->vendor != SDIO_VENDOR_ID_SYNAPTICS)) {
				sd_err(("%s: class=0x%x; vendor=0x%x; device=0x%x\n", __FUNCTION__,
					id->class, id->vendor, id->device));
				return -ENODEV;
		}
	}

	if (func && (func->num != 2)) {
		return 0;
	}

	if (notify_semaphore)
		up(notify_semaphore);
	return 0;
}

static void dummy_remove(struct sdio_func *func)
{
}

static struct sdio_driver dummy_sdmmc_driver = {
	.probe		= dummy_probe,
	.remove		= dummy_remove,
	.name		= "dummy_sdmmc",
	.id_table	= bcmsdh_sdmmc_ids,
};

int sdio_func_reg_notify(void* semaphore)
{
	notify_semaphore = semaphore;
	return sdio_register_driver(&dummy_sdmmc_driver);
}

void sdio_func_unreg_notify(void)
{
	OSL_SLEEP(15);
	sdio_unregister_driver(&dummy_sdmmc_driver);
}

#endif /* defined(BCMLXSDMMC) */

static struct sdio_driver bcmsdh_sdmmc_driver = {
	.probe		= bcmsdh_sdmmc_probe,
	.remove		= bcmsdh_sdmmc_remove,
	.name		= "bcmsdh_sdmmc",
	.id_table	= bcmsdh_sdmmc_ids,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) && defined(CONFIG_PM_SLEEP)
	.drv = {
	.pm	= &bcmsdh_sdmmc_pm_ops,
	},
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) && defined(CONFIG_PM_SLEEP) */
	};

struct sdos_info {
	sdioh_info_t *sd;
	spinlock_t lock;
};

/* Interrupt enable/disable */
SDIOH_API_RC
sdioh_interrupt_set(sdioh_info_t *sd, bool enable)
{
	if (!sd)
		return BCME_BADARG;

	sd_trace(("%s: %s\n", __FUNCTION__, enable ? "Enabling" : "Disabling"));
	return SDIOH_API_RC_SUCCESS;
}

#ifdef BCMSDH_MODULE
static int __init
bcmsdh_module_init(void)
{
	int error = 0;
	error = sdio_function_init();
	return error;
}

static void __exit
bcmsdh_module_cleanup(void)
{
	sdio_function_cleanup();
}

module_init(bcmsdh_module_init);
module_exit(bcmsdh_module_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_AUTHOR(AUTHOR);

#endif /* BCMSDH_MODULE */
/*
 * module init
*/
int bcmsdh_register_client_driver(void)
{
	return sdio_register_driver(&bcmsdh_sdmmc_driver);
}

/*
 * module cleanup
*/
void bcmsdh_unregister_client_driver(void)
{
	sdio_unregister_driver(&bcmsdh_sdmmc_driver);
}
