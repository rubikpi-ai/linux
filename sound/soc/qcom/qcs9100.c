// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.

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

#define DRIVER_NAME		"qcs9100"
#define WCN_CDC_SLIM_RX_CH_MAX	2
#define WCN_CDC_SLIM_TX_CH_MAX	2
#define NAME_SIZE	32

struct qcs9100_snd_data {
	bool stream_prepared[AFE_PORT_MAX];
	struct snd_soc_card *card;
	struct sdw_stream_runtime *sruntime[AFE_PORT_MAX];
	struct snd_soc_jack jack;
	bool jack_setup;
	struct clk *macro;
	struct clk *dcodec;
	struct snd_soc_jack hdmi_jack[8];
};

static int qcs9100_snd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct qcs9100_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	int ret = 0;
	char jack_name[NAME_SIZE];
	struct snd_soc_jack *hdmi_jack  = NULL;
	int hdmi_pcm_id = 0;
	struct snd_soc_dai *codec_dai;
	int rval, i;

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
	case PRIMARY_TDM_RX_0:
	case PRIMARY_TDM_TX_0:
		break;
	case DISPLAY_PORT_RX_0:
		hdmi_pcm_id = 0;
		hdmi_jack = &data->hdmi_jack[0];
		break;
	case DISPLAY_PORT_RX_1:
		hdmi_pcm_id = 1;
		hdmi_jack = &data->hdmi_jack[1];
		break;

	default:
		break;
	}
	if (hdmi_jack) {
		snprintf(jack_name, sizeof(jack_name), "HDMI/DP%d Jack", hdmi_pcm_id);
		rval = snd_soc_card_jack_new(rtd->card, jack_name, SND_JACK_AVOUT, hdmi_jack);

		if (rval)
			return rval;

		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			rval = snd_soc_component_set_jack(codec_dai->component, hdmi_jack, NULL);
			if (rval != 0 && rval != -EOPNOTSUPP) {
				dev_warn(rtd->card->dev, "Failed to set HDMI jack: %d\n", rval);
				return rval;
			}
		}
		return qcom_snd_wcd_jack_setup(rtd, &data->jack, &data->jack_setup);
	}

	return ret;
}

static int qcs9100_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				      struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = 48000;
	rate->max = 48000;
	channels->min = 2;
	channels->max = 2;

	return 0;
}

static int qcs9100_snd_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct qcs9100_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);
	int ret = 0;

	switch (cpu_dai->id) {
	case TERTIARY_MI2S_RX:
	case TERTIARY_MI2S_TX:
	case TERTIARY_TDM_RX_0:
	case TERTIARY_TDM_TX_0:
	case PRIMARY_SDR_MI2S_RX ... QUINARY_SDR_TDM_TX_0:
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

static int qcs9100_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct qcs9100_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_prepare(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static int qcs9100_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct qcs9100_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	switch (cpu_dai->id) {
	case TERTIARY_MI2S_RX:
	case TERTIARY_MI2S_TX:
	case TERTIARY_TDM_RX_0:
	case TERTIARY_TDM_TX_0:
	case PRIMARY_SDR_MI2S_RX ... QUINARY_SDR_TDM_TX_0:
		/* clock disable is done for qcs9100 target to support high
		 * speed i2s interface
		 */
		if (data->dcodec)
			clk_disable_unprepare(data->dcodec);

		if (data->macro)
			clk_disable_unprepare(data->macro);

		break;
	default:
		break;
	}

	return qcom_snd_sdw_hw_free(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static const struct snd_soc_dapm_widget qcs8300_dapm_widgets[] = {
	SND_SOC_DAPM_PINCTRL("STUB_AIF1_PINCTRL", "stub_aif1_active", "stub_aif1_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF2_PINCTRL", "stub_aif2_active", "stub_aif2_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF3_PINCTRL", "stub_aif3_active", "stub_aif3_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF4_PINCTRL", "stub_aif4_active", "stub_aif4_sleep"),
};

static const struct snd_soc_dapm_route qcs8300_dapm_routes[] = {
	{"STUB_AIF0_RX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF0_TX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF1_RX", NULL, "STUB_AIF2_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF2_PINCTRL"},
	{"STUB_AIF2_RX", NULL, "STUB_AIF3_PINCTRL"},
	{"STUB_AIF2_TX", NULL, "STUB_AIF3_PINCTRL"},
	{"STUB_AIF3_RX", NULL, "STUB_AIF4_PINCTRL"},
	{"STUB_AIF3_TX", NULL, "STUB_AIF4_PINCTRL"},
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

static const struct snd_soc_dapm_widget qcs9075_dapm_widgets[] = {
	SND_SOC_DAPM_PINCTRL("MI2S_OUT_PINCTRL", "mi2s_aud_out_active", "mi2s_aud_out_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF0_PINCTRL", "stub_aif0_active", "stub_aif0_sleep"),
	SND_SOC_DAPM_PINCTRL("STUB_AIF1_PINCTRL", "stub_aif1_active", "stub_aif1_sleep"),
};

static const struct snd_soc_dapm_route qcs9075_dapm_routes[] = {
	{"Speaker", NULL, "MI2S_OUT_PINCTRL"},
	{"DMic", NULL, "MI2S_OUT_PINCTRL"},
	{"STUB_AIF0_RX", NULL, "STUB_AIF0_PINCTRL"},
	{"STUB_AIF0_TX", NULL, "STUB_AIF0_PINCTRL"},
	{"STUB_AIF1_RX", NULL, "STUB_AIF1_PINCTRL"},
	{"STUB_AIF1_TX", NULL, "STUB_AIF1_PINCTRL"},
};

static const struct snd_soc_ops qcs9100_be_ops = {
	.hw_params = qcs9100_snd_hw_params,
	.hw_free = qcs9100_snd_hw_free,
	.prepare = qcs9100_snd_prepare,
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

static struct snd_soc_card snd_soc_qcs9075_rb8_data = {
	.name = "qcs9075-rb8",
	.dapm_widgets = qcs9075_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qcs9075_dapm_widgets),
	.dapm_routes = qcs9075_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(qcs9075_dapm_routes),
};

static void qcs9100_add_be_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		if (link->no_pcm == 1) {
			link->init = qcs9100_snd_init;
			link->be_hw_params_fixup = qcs9100_be_hw_params_fixup;
			link->ops = &qcs9100_be_ops;
		}
	}
}

static int qcs9100_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct qcs9100_snd_data *data;
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
	qcs9100_add_be_ops(card);

	/* get clock info to set clock for qcs9100 target to support high
	 * speed i2s interface
	 */
	data->macro = devm_clk_get_optional(dev, "macro");
	if (IS_ERR(data->macro)) {
		dev_info(dev, "getting macro clock info FAILED, ret %d\n", PTR_ERR(data->macro));
		return PTR_ERR(data->macro);
	}
	data->dcodec = devm_clk_get_optional(dev, "dcodec");
	if (IS_ERR(data->dcodec)) {
		dev_info(dev, "getting decode clock info FAILED, ret %d\n", PTR_ERR(data->dcodec));
		return PTR_ERR(data->dcodec);
	}

	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id snd_qcs9100_dt_match[] = {
	{.compatible = "qcom,qcs8300-sndcard", .data = &snd_soc_qcs8300_data},
	{.compatible = "qcom,qcs9100-sndcard", .data = &snd_soc_qcs9100_data},
	{.compatible = "qcom,qcs9075-rb8-sndcard", .data = &snd_soc_qcs9075_rb8_data},
	{}
};

MODULE_DEVICE_TABLE(of, snd_qcs9100_dt_match);

static struct platform_driver snd_qcs9100_driver = {
	.probe  = qcs9100_platform_probe,
	.driver = {
		.name = "snd-qcs9100",
		.of_match_table = snd_qcs9100_dt_match,
	},
};
module_platform_driver(snd_qcs9100_driver);
MODULE_DESCRIPTION("qcs9100 ASoC Machine Driver");
MODULE_LICENSE("GPL");
