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
	struct clk *macro;
	struct clk *dcodec;
};

static int qcm6490_slim_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;
	unsigned int rx_ch[WCN_CDC_SLIM_RX_CH_MAX] = {157, 158};
	unsigned int tx_ch[WCN_CDC_SLIM_TX_CH_MAX]  = {159, 162};
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);

	ret = snd_soc_dai_set_channel_map(codec_dai, ARRAY_SIZE(tx_ch),
		tx_ch, ARRAY_SIZE(rx_ch), rx_ch);

	return ret;
}

static int qcm6490_snd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct qcm6490_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	int ret = 0;

	switch (cpu_dai->id) {
	case TX_CODEC_DMA_TX_3:
	case RX_CODEC_DMA_RX_0:
		ret = qcom_snd_wcd_jack_setup(rtd, &data->jack, &data->jack_setup);
		break;
	case VA_CODEC_DMA_TX_0:
	case WSA_CODEC_DMA_RX_0:
	case WSA_CODEC_DMA_TX_0:
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
	case PRIMARY_TDM_RX_0:
	case PRIMARY_TDM_TX_0:
		break;
	case SLIMBUS_0_RX:
	case SLIMBUS_0_TX:
		ret = qcm6490_slim_dai_init(rtd);
	default:
		break;
	}

	return ret;
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
	int ret = 0;

	switch (cpu_dai->id) {
	case TERTIARY_MI2S_RX:
	case TERTIARY_MI2S_TX:
	case TERTIARY_TDM_RX_0:
	case TERTIARY_TDM_TX_0:
		/* clock setting is done for qcs9100 target to support high
		 * speed i2s interface
		 */
		if (pdata->macro) {
			ret = clk_prepare_enable(pdata->macro);
			if (ret) {
				dev_err(pdata->card->dev, "unable to prepare macro\n");
				return ret;
			}
		}
		if (pdata->dcodec) {
			ret = clk_prepare_enable(pdata->dcodec);
			if (ret) {
				dev_err(pdata->card->dev, "unable to prepare decode\n");
				return ret;
			}
		}
		break;
	default:
		break;
	}
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

static const struct snd_soc_dapm_widget qcs6490_rb3gen2_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_PINCTRL("STUB_AIF1_PINCTRL", "stub_aif1_active", "stub_aif1_sleep"),
};

static const struct snd_soc_dapm_route qcs6490_rb3gen2_dapm_routes[] = {
	{"STUB_AIF1_RX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF1_PINCTRL"},
};

static const struct snd_soc_dapm_widget qcs6490_rb3gen2_ia_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};

static const struct snd_soc_dapm_widget qcs6490_rb3gen2_ptz_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_PINCTRL("STUB_AIF1_PINCTRL", "stub_aif1_active", "stub_aif1_sleep"),
};

static const struct snd_soc_dapm_route qcs6490_rb3gen2_ptz_dapm_routes[] = {
	{"STUB_AIF1_RX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF1_PINCTRL"},
};

static const struct snd_soc_dapm_widget qcs6490_rb3gen2_video_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_PINCTRL("STUB_AIF1_PINCTRL", "stub_aif1_active", "stub_aif1_sleep"),
};

static const struct snd_soc_dapm_route qcs6490_rb3gen2_video_dapm_routes[] = {
	{"STUB_AIF1_RX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF1_PINCTRL"},
};

static const struct snd_soc_dapm_widget qcs6490_rb3gen2_vision_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_PINCTRL("STUB_AIF1_PINCTRL", "stub_aif1_active", "stub_aif1_sleep"),
};

static const struct snd_soc_dapm_route qcs6490_rb3gen2_vision_dapm_routes[] = {
	{"STUB_AIF1_RX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF1_PINCTRL"},
};

static const struct snd_soc_dapm_widget qcs8300_dapm_widgets[] = {
	SND_SOC_DAPM_PINCTRL("STUB_AIF1_PINCTRL", "stub_aif1_active", "stub_aif1_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF2_PINCTRL", "stub_aif2_active", "stub_aif2_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF3_PINCTRL", "stub_aif3_active", "stub_aif3_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF4_PINCTRL", "stub_aif4_active", "stub_aif4_sleep"),
};

static const struct snd_soc_dapm_route qcs8300_dapm_routes[] = {
	{"STUB_AIF1_RX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF2_RX", NULL, "STUB_AIF2_PINCTRL"},
	{"STUB_AIF2_TX", NULL, "STUB_AIF2_PINCTRL"},
	{"STUB_AIF1_RX", NULL, "STUB_AIF3_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF3_PINCTRL"},
	{"STUB_AIF1_RX", NULL, "STUB_AIF4_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF4_PINCTRL"},
};

static const struct snd_soc_dapm_widget qcs9100_dapm_widgets[] = {
	SND_SOC_DAPM_PINCTRL("STUB_AIF0_PINCTRL", "stub_aif0_active", "stub_aif0_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF1_PINCTRL", "stub_aif1_active", "stub_aif1_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF2_PINCTRL", "stub_aif2_active", "stub_aif2_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF3_PINCTRL", "stub_aif3_active", "stub_aif3_sleep"),
};

static const struct snd_soc_dapm_route qcs9100_dapm_routes[] = {
	{"STUB_AIF0_RX", NULL, "STUB_AIF0_PINCTRL"},
	{"STUB_AIF0_TX", NULL, "STUB_AIF0_PINCTRL"},
	{"STUB_AIF1_RX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF2_RX", NULL, "STUB_AIF2_PINCTRL"},
	{"STUB_AIF2_TX", NULL, "STUB_AIF2_PINCTRL"},
	{"STUB_AIF3_RX", NULL, "STUB_AIF3_PINCTRL"},
	{"STUB_AIF3_TX", NULL, "STUB_AIF3_PINCTRL"},
};

static const struct snd_soc_ops qcm6490_be_ops = {
	.hw_params = qcm6490_snd_hw_params,
	.hw_free = qcm6490_snd_hw_free,
	.prepare = qcm6490_snd_prepare,
};

static struct snd_soc_card qcm6490_data = {
	.name = "qcm6490",
	.dapm_widgets = qcm6490_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qcm6490_dapm_widgets),
	.dapm_routes = qcm6490_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(qcm6490_dapm_routes),
};

static struct snd_soc_card qcs6490_rb3gen2_data = {
	.name = "qcs6490-rb3gen2",
	.dapm_widgets = qcs6490_rb3gen2_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qcs6490_rb3gen2_dapm_widgets),
	.dapm_routes = qcs6490_rb3gen2_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(qcs6490_rb3gen2_dapm_routes),
};

static struct snd_soc_card qcs6490_rb3gen2_ia_data = {
	.name = "qcs6490-rb3gen2-ia-mezz",
	.dapm_widgets = qcs6490_rb3gen2_ia_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qcs6490_rb3gen2_ia_dapm_widgets),
};

static struct snd_soc_card qcs6490_rb3gen2_ptz_data = {
	.name = "qcs6490-rb3gen2-ptz-mezz",
	.dapm_widgets = qcs6490_rb3gen2_ptz_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qcs6490_rb3gen2_ptz_dapm_widgets),
	.dapm_routes = qcs6490_rb3gen2_ptz_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(qcs6490_rb3gen2_ptz_dapm_routes),
};

static struct snd_soc_card qcs6490_rb3gen2_video_data = {
	.name = "qcs6490-rb3gen2-video-mezz",
	.dapm_widgets = qcs6490_rb3gen2_video_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qcs6490_rb3gen2_video_dapm_widgets),
	.dapm_routes = qcs6490_rb3gen2_video_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(qcs6490_rb3gen2_video_dapm_routes),
};

static struct snd_soc_card qcs6490_rb3gen2_vision_data = {
	.name = "qcs6490-rb3gen2-vision-mezz",
	.dapm_widgets = qcs6490_rb3gen2_vision_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qcs6490_rb3gen2_vision_dapm_widgets),
	.dapm_routes = qcs6490_rb3gen2_vision_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(qcs6490_rb3gen2_vision_dapm_routes),
};

static struct snd_soc_card snd_soc_qcs8300_data = {
	.name = "qcs8300",
	.dapm_widgets = qcs8300_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qcs8300_dapm_widgets),
	.dapm_routes = qcs8300_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(qcs8300_dapm_routes),
};

static struct snd_soc_card snd_soc_qcs9100_data = {
	.name = "qcs9100",
	.dapm_widgets = qcs9100_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qcs9100_dapm_widgets),
	.dapm_routes = qcs9100_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(qcs9100_dapm_routes),
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

	card = (struct snd_soc_card *)of_device_get_match_data(&pdev->dev);
	if (!card)
		return -EINVAL;

	card->owner = THIS_MODULE;
	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card->dev = dev;

	dev_set_drvdata(dev, card);
	snd_soc_card_set_drvdata(card, data);
	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	card->driver_name = DRIVER_NAME;
	qcm6490_add_be_ops(card);

	/* get clock info to set clock for qcs9100 target to support high
	 * speed i2s interface
	 */
	data->macro = devm_clk_get_optional(dev, "macro");
	if (IS_ERR(data->macro))
		dev_info(dev, "getting macro clock info FAILED\n");
	data->dcodec = devm_clk_get_optional(dev, "dcodec");
	if (IS_ERR(data->dcodec))
		dev_info(dev, "getting decode clock info FAILED\n");

	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id snd_qcm6490_dt_match[] = {
	{.compatible = "qcom,qcm6490-sndcard", .data = &qcm6490_data},
	{.compatible = "qcom,qcs6490-rb3gen2-sndcard", .data = &qcs6490_rb3gen2_data},
	{.compatible = "qcom,qcs6490-rb3gen2-ia-sndcard", .data = &qcs6490_rb3gen2_ia_data},
	{.compatible = "qcom,qcs6490-rb3gen2-ptz-sndcard", .data = &qcs6490_rb3gen2_ptz_data},
	{.compatible = "qcom,qcs6490-rb3gen2-video-sndcard", .data = &qcs6490_rb3gen2_video_data},
	{.compatible = "qcom,qcs6490-rb3gen2-vision-sndcard", .data = &qcs6490_rb3gen2_vision_data},
	{.compatible = "qcom,qcs8300-sndcard", .data = &snd_soc_qcs8300_data},
	{.compatible = "qcom,qcs9100-sndcard", .data = &snd_soc_qcs9100_data},
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
