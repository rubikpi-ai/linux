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

#define DEFAULT_MCLK_RATE		24576000
#define TDM_BCLK_RATE			6144000
#define MI2S_BCLK_RATE			1536000

struct qcm6490_snd_data {
	bool stream_prepared[AFE_PORT_MAX];
	struct snd_soc_card *card;
	uint32_t pri_mi2s_clk_count;
	uint32_t sec_mi2s_clk_count;
	uint32_t quat_mi2s_clk_count;
	uint32_t quat_tdm_clk_count;
	uint32_t pri_mi2s_mclk_count;
	struct sdw_stream_runtime *sruntime[AFE_PORT_MAX];
	struct snd_soc_jack jack;
	bool jack_setup;
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

/* The ES8316 IC requires MCLK to be constantly on. If MCLK switches on
   and offas playback starts and stops, it can easily cause POP sound */
static int qcm6490_mi2s_mclk_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;
	struct snd_soc_card *card = rtd->card;
	struct qcm6490_snd_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
		if (++(data->pri_mi2s_mclk_count) == 1) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_MCLK_1,
				DEFAULT_MCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;
	default:
		dev_err(rtd->dev, "%s: invalid dai id 0x%x\n", __func__, cpu_dai->id);
	}

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
	case SECONDARY_MI2S_RX:
	case SECONDARY_MI2S_TX:
	case TERTIARY_MI2S_RX:
	case TERTIARY_MI2S_TX:
	case QUATERNARY_MI2S_RX:
	case QUATERNARY_MI2S_TX:
	case PRIMARY_TDM_RX_0:
	case PRIMARY_TDM_TX_0:
		return 0;
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
		return qcm6490_mi2s_mclk_init(rtd);
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

static int qcm6490_snd_startup(struct snd_pcm_substream *substream)
{
	unsigned int fmt = SND_SOC_DAIFMT_BP_FP;
	unsigned int codec_dai_fmt = SND_SOC_DAIFMT_BC_FC;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct qcm6490_snd_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int j;
	int ret;

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
		codec_dai_fmt |= SND_SOC_DAIFMT_NB_NF;
		if (++(data->pri_mi2s_clk_count) == 1) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT,
				MI2S_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		}
		snd_soc_dai_set_fmt(cpu_dai, fmt);
		snd_soc_dai_set_fmt(codec_dai, codec_dai_fmt);
		break;

	case SECONDARY_MI2S_TX:
		codec_dai_fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_I2S;
		if (++(data->sec_mi2s_clk_count) == 1) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_SEC_MI2S_IBIT,
				MI2S_BCLK_RATE,	SNDRV_PCM_STREAM_CAPTURE);
		}
		snd_soc_dai_set_fmt(cpu_dai, fmt);
		snd_soc_dai_set_fmt(codec_dai, codec_dai_fmt);
		break;
	case QUATERNARY_MI2S_RX:
		codec_dai_fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_I2S;
		if (++(data->quat_mi2s_clk_count) == 1) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_QUAD_MI2S_IBIT,
				MI2S_BCLK_RATE * 2, SNDRV_PCM_STREAM_PLAYBACK);
		}
		snd_soc_dai_set_fmt(cpu_dai, fmt);
		snd_soc_dai_set_fmt(codec_dai, codec_dai_fmt);
		break;

	case QUATERNARY_TDM_RX_0:
	case QUATERNARY_TDM_TX_0:
		if (++(data->quat_tdm_clk_count) == 1) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_QUAD_TDM_IBIT,
				TDM_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		}

		codec_dai_fmt |= SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_DSP_B;

		for_each_rtd_codec_dais(rtd, j, codec_dai) {

			if (!strcmp(codec_dai->component->name_prefix,
				    "Left")) {
				ret = snd_soc_dai_set_fmt(
						codec_dai, codec_dai_fmt);
				if (ret < 0) {
					dev_err(rtd->dev,
						"Left TDM fmt err:%d\n", ret);
					return ret;
				}
			}

			if (!strcmp(codec_dai->component->name_prefix,
				    "Right")) {
				ret = snd_soc_dai_set_fmt(
						codec_dai, codec_dai_fmt);
				if (ret < 0) {
					dev_err(rtd->dev,
						"Right TDM slot err:%d\n", ret);
					return ret;
				}
			}
		}
		break;
	case SLIMBUS_0_RX...SLIMBUS_6_TX:
		break;

	default:
		pr_err("%s: invalid dai id 0x%x\n", __func__, cpu_dai->id);
		break;
	}
	return 0;
}

static void  qcm6490_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct qcm6490_snd_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
		if (--(data->pri_mi2s_clk_count) == 0) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT,
				0, SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;

	case SECONDARY_MI2S_TX:
		if (--(data->sec_mi2s_clk_count) == 0) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_SEC_MI2S_IBIT,
				0, SNDRV_PCM_STREAM_CAPTURE);
		}
		break;

	case QUATERNARY_TDM_RX_0:
	case QUATERNARY_TDM_TX_0:
		if (--(data->quat_tdm_clk_count) == 0) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_QUAD_TDM_IBIT,
				0, SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;
	case SLIMBUS_0_RX...SLIMBUS_6_TX:
		break;
	case QUATERNARY_MI2S_RX:
		if (--(data->quat_mi2s_clk_count) == 0) {
			snd_soc_dai_set_sysclk(cpu_dai,
				Q6AFE_LPASS_CLK_ID_QUAD_MI2S_IBIT,
				0, SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;

	default:
		pr_err("%s: invalid dai id 0x%x\n", __func__, cpu_dai->id);
		break;
	}
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
	.startup = qcm6490_snd_startup,
	.shutdown = qcm6490_snd_shutdown,
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
