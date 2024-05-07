// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <linux/soundwire/sdw.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include "lpass.h"
#include "qdsp6/q6afe.h"
#include "qdsp6/q6prm.h"
#include "common.h"
#include "sdw.h"

#define DRIVER_NAME		"qcm6490"
#define TDM_SLOTS_PER_FRAME	8
#define TDM_SLOT_WIDTH		16
#define WCN_CDC_SLIM_RX_CH_MAX	2
#define WCN_CDC_SLIM_TX_CH_MAX	2

struct qcm6490_snd_data {
	bool stream_prepared[AFE_PORT_MAX];
	struct snd_soc_card *card;
	struct sdw_stream_runtime *sruntime[AFE_PORT_MAX];
	struct snd_soc_jack jack;
	bool jack_setup;
};

static int qcm6490_slim_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;
	unsigned int rx_ch[WCN_CDC_SLIM_RX_CH_MAX] = {157, 158};
	unsigned int tx_ch[WCN_CDC_SLIM_TX_CH_MAX]  = {159, 160};
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);

	ret = snd_soc_dai_set_channel_map(codec_dai, ARRAY_SIZE(tx_ch),
		tx_ch, ARRAY_SIZE(rx_ch), rx_ch);

	return ret;
}

static int qcm6490_snd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct qcm6490_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case TX_CODEC_DMA_TX_3:
	/* case LPASS_CDC_DMA_TX3: */
	case RX_CODEC_DMA_RX_0:
		return qcom_snd_wcd_jack_setup(rtd, &data->jack, &data->jack_setup);
	case VA_CODEC_DMA_TX_0:
	case WSA_CODEC_DMA_RX_0:
	case WSA_CODEC_DMA_TX_0:
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
	case SECONDARY_MI2S_RX:
	case SECONDARY_MI2S_TX:
	case TERTIARY_MI2S_RX:
	case TERTIARY_MI2S_TX:
	case PRIMARY_TDM_RX_0:
	case PRIMARY_TDM_TX_0:
		return 0;
	case SLIMBUS_0_RX:
	case SLIMBUS_0_TX:
		return qcm6490_slim_dai_init(rtd);
	default:
		dev_err(rtd->dev, "%s: invalid dai id 0x%x\n", __func__, cpu_dai->id);
	}

	return -EINVAL;
}

static int qcm6490_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = 48000;
	channels->min = 2;
	channels->max = 2;
	switch (cpu_dai->id) {
	case TX_CODEC_DMA_TX_0:
	case TX_CODEC_DMA_TX_1:
	case TX_CODEC_DMA_TX_2:
	case TX_CODEC_DMA_TX_3:
		channels->min = 1;
		break;
	default:
		break;
	}

	return 0;
}

static int qcm6490_snd_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct qcm6490_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);

	return qcom_snd_sdw_hw_params(substream, params, &pdata->sruntime[cpu_dai->id]);
}

static int qcm6490_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct qcm6490_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_prepare(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static int qcm6490_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct qcm6490_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_hw_free(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static const struct snd_soc_dapm_widget qcm6490_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_PINCTRL("STUB_AIF1_PINCTRL", "stub_aif1_active", "stub_aif1_sleep"),
};

static const struct snd_soc_dapm_route qcm6490_dapm_routes[] = {
	{"STUB_AIF1_RX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF1_PINCTRL"},
};

static const struct snd_soc_ops qcm6490_be_ops = {
	.hw_params = qcm6490_snd_hw_params,
	.hw_free = qcm6490_snd_hw_free,
	.prepare = qcm6490_snd_prepare,
};

static void qcm6490_add_be_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		if ((link->num_codecs != 1) || (link->codecs->dai_name
					&& strcmp(link->codecs->dai_name, "snd-soc-dummy-dai"))) {
			link->init = qcm6490_snd_init;
			link->be_hw_params_fixup = qcm6490_be_hw_params_fixup;
			link->ops = &qcm6490_be_ops;
		}
	}
}

static int qcm6490_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct qcm6490_snd_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;
	card->owner = THIS_MODULE;
	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card->dev = dev;
	card->dapm_widgets = qcm6490_dapm_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(qcm6490_dapm_widgets);
	card->dapm_routes = qcm6490_dapm_routes;
	card->num_dapm_routes = ARRAY_SIZE(qcm6490_dapm_routes);

	dev_set_drvdata(dev, card);
	snd_soc_card_set_drvdata(card, data);
	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	card->driver_name = DRIVER_NAME;
	qcm6490_add_be_ops(card);
	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id snd_qcm6490_dt_match[] = {
	{.compatible = "qcom,qcm6490-sndcard",},
	{}
};

MODULE_DEVICE_TABLE(of, snd_qcm6490_dt_match);

static struct platform_driver snd_qcm6490_driver = {
	.probe  = qcm6490_platform_probe,
	.driver = {
		.name = "snd-qcm6490",
		.of_match_table = snd_qcm6490_dt_match,
	},
};
module_platform_driver(snd_qcm6490_driver);
MODULE_DESCRIPTION("qcm6490 ASoC Machine Driver");
MODULE_LICENSE("GPL");
