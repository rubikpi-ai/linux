// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022, The Linux Foundation. All rights reserved.
// Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/export.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/core.h>
#include <sound/tlv.h>

#include "lpass-macro-common.h"

static bool lpi_state;

bool lpass_macro_get_lpi_state(void)
{
	return lpi_state;
}
EXPORT_SYMBOL_GPL(lpass_macro_get_lpi_state);

void lpass_macro_set_lpi_state(int status)
{
	if (status)
		lpi_state = true;
	else
		lpi_state = false;
}
EXPORT_SYMBOL_GPL(lpass_macro_set_lpi_state);

static int lpass_macro_chmap_ctl_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dai *dai = info->private_data;
	uint32_t *chmap_data = NULL;
	uint32_t rx_ch_cnt = 0;
	uint32_t tx_ch_cnt = 0;
	uint32_t rx_ch, tx_ch;

	chmap_data = kzalloc(sizeof(uint32_t) * 2, GFP_KERNEL);
	if (!chmap_data)
		return -ENOMEM;

	snd_soc_dai_get_channel_map(dai, &tx_ch_cnt, &tx_ch, &rx_ch_cnt, &rx_ch);
	if (rx_ch_cnt) {
		chmap_data[0] = rx_ch_cnt;
		chmap_data[1] = rx_ch;
	} else if (tx_ch_cnt) {
		chmap_data[0] = tx_ch_cnt;
		chmap_data[1] = tx_ch;
	}
	memcpy(ucontrol->value.bytes.data, chmap_data, sizeof(uint32_t) * 2);

	kfree(chmap_data);
	return 0;
}

int lpass_macro_add_chmap_ctls(struct snd_soc_pcm_runtime *rtd,
			       struct snd_soc_dai *dai, int dir)
{
	struct snd_pcm_chmap *info;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret =  snd_pcm_add_chmap_ctls(rtd->pcm, dir, NULL,
				      2 * sizeof(uint32_t), 0, &info);
	if (ret < 0) {
		kfree(info);
		return ret;
	}

	/* override handlers */
	info->private_data = dai;
	info->kctl->get = lpass_macro_chmap_ctl_get;
	return 0;
}
EXPORT_SYMBOL_GPL(lpass_macro_add_chmap_ctls);

struct lpass_macro *lpass_macro_pds_init(struct device *dev)
{
	struct lpass_macro *l_pds;
	int ret;

	if (!of_property_present(dev->of_node, "power-domains"))
		return NULL;

	l_pds = devm_kzalloc(dev, sizeof(*l_pds), GFP_KERNEL);
	if (!l_pds)
		return ERR_PTR(-ENOMEM);

	l_pds->macro_pd = dev_pm_domain_attach_by_name(dev, "macro");
	if (IS_ERR_OR_NULL(l_pds->macro_pd)) {
		ret = l_pds->macro_pd ? PTR_ERR(l_pds->macro_pd) : -ENODATA;
		goto macro_err;
	}

	ret = pm_runtime_resume_and_get(l_pds->macro_pd);
	if (ret < 0)
		goto macro_sync_err;

	l_pds->dcodec_pd = dev_pm_domain_attach_by_name(dev, "dcodec");
	if (IS_ERR_OR_NULL(l_pds->dcodec_pd)) {
		ret = l_pds->dcodec_pd ? PTR_ERR(l_pds->dcodec_pd) : -ENODATA;
		goto dcodec_err;
	}

	ret = pm_runtime_resume_and_get(l_pds->dcodec_pd);
	if (ret < 0)
		goto dcodec_sync_err;
	return l_pds;

dcodec_sync_err:
	dev_pm_domain_detach(l_pds->dcodec_pd, false);
dcodec_err:
	pm_runtime_put(l_pds->macro_pd);
macro_sync_err:
	dev_pm_domain_detach(l_pds->macro_pd, false);
macro_err:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(lpass_macro_pds_init);

void lpass_macro_pds_exit(struct lpass_macro *pds)
{
	if (pds) {
		pm_runtime_put(pds->macro_pd);
		dev_pm_domain_detach(pds->macro_pd, false);
		pm_runtime_put(pds->dcodec_pd);
		dev_pm_domain_detach(pds->dcodec_pd, false);
	}
}
EXPORT_SYMBOL_GPL(lpass_macro_pds_exit);

MODULE_DESCRIPTION("Common macro driver");
MODULE_LICENSE("GPL");
