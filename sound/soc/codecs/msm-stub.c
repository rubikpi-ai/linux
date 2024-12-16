// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#define DRV_NAME "msm-stub-codec"

static const struct snd_soc_dapm_widget msm_stub_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("STUB_AIF0_RX"),
	SND_SOC_DAPM_INPUT("STUB_AIF0_TX"),
	SND_SOC_DAPM_OUTPUT("STUB_AIF1_RX"),
	SND_SOC_DAPM_INPUT("STUB_AIF1_TX"),
	SND_SOC_DAPM_OUTPUT("STUB_AIF2_RX"),
	SND_SOC_DAPM_INPUT("STUB_AIF2_TX"),
	SND_SOC_DAPM_OUTPUT("STUB_AIF3_RX"),
	SND_SOC_DAPM_INPUT("STUB_AIF3_TX"),
};

static const struct snd_soc_dapm_route msm_stub_dapm_routes[] = {
	{"STUB_AIF0_RX", NULL, "STUB_AIF0_RX Playback"},
	{"STUB_AIF0_TX Capture", NULL, "STUB_AIF0_TX"},
	{"STUB_AIF1_RX", NULL, "STUB_AIF1_RX Playback"},
	{"STUB_AIF1_TX Capture", NULL, "STUB_AIF1_TX"},
	{"STUB_AIF2_RX", NULL, "STUB_AIF2_RX Playback"},
	{"STUB_AIF2_TX Capture", NULL, "STUB_AIF2_TX"},
	{"STUB_AIF3_RX", NULL, "STUB_AIF3_RX Playback"},
	{"STUB_AIF3_TX Capture", NULL, "STUB_AIF3_TX"},
};

static struct snd_soc_dai_driver msm_stub_dais[] = {
	{
		.name = "msm-stub-aif0-rx",
		.playback = {
			.stream_name = "STUB_AIF0_RX Playback",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
			.rate_min = 8000,
			.rate_max = 384000,
		},
	},
	{
		.name = "msm-stub-aif0-tx",
		.capture = {
			.stream_name = "STUB_AIF0_TX Capture",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
			.rate_min = 8000,
			.rate_max = 384000,
		},
	},
	{
		.name = "msm-stub-aif1-rx",
		.playback = {
			.stream_name = "STUB_AIF1_RX Playback",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
			.rate_min = 8000,
			.rate_max = 384000,
		},
	},
	{
		.name = "msm-stub-aif1-tx",
		.capture = {
			.stream_name = "STUB_AIF1_TX Capture",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
			.rate_min = 8000,
			.rate_max = 384000,
		},
	},
	{
		.name = "msm-stub-aif2-rx",
		.playback = {
			.stream_name = "STUB_AIF2_RX Playback",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
			.rate_min = 8000,
			.rate_max = 384000,
		},
	},
	{
		.name = "msm-stub-aif2-tx",
		.capture = {
			.stream_name = "STUB_AIF2_TX Capture",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_S24_LE |
					SNDRV_PCM_FMTBIT_S24_3LE |
					SNDRV_PCM_FMTBIT_S32_LE),
			.rate_min = 8000,
			.rate_max = 384000,
		},
	},
	{
		.name = "msm-stub-aif3-rx",
		.playback = {
			.stream_name = "STUB_AIF3_RX Playback",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
			.rate_min = 8000,
			.rate_max = 384000,
		},
	},
	{
		.name = "msm-stub-aif3-tx",
		.capture = {
			.stream_name = "STUB_AIF3_TX Capture",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_S24_LE |
					SNDRV_PCM_FMTBIT_S24_3LE |
					SNDRV_PCM_FMTBIT_S32_LE),
			.rate_min = 8000,
			.rate_max = 384000,
		},
	},
};

static const struct snd_soc_component_driver soc_msm_stub = {
	.name = DRV_NAME,
	.dapm_widgets = msm_stub_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(msm_stub_dapm_widgets),
	.dapm_routes = msm_stub_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(msm_stub_dapm_routes),
};

static int msm_stub_dev_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev, &soc_msm_stub, msm_stub_dais,
			ARRAY_SIZE(msm_stub_dais));
}

static const struct of_device_id msm_stub_codec_dt_match[] = {
	{.compatible = "qcom,msm-stub-codec", },
	{}
};
MODULE_DEVICE_TABLE(of, msm_stub_codec_dt_match);

static struct platform_driver msm_stub_driver = {
	.driver = {
		.name = "msm-stub-codec",
		.of_match_table = of_match_ptr(msm_stub_codec_dt_match),
	},
	.probe = msm_stub_dev_probe,
};

module_platform_driver(msm_stub_driver);

MODULE_DESCRIPTION("MSM STUB CODEC driver");
MODULE_LICENSE("GPL");
