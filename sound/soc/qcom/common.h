/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2018, The Linux Foundation. All rights reserved.
// Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.

#ifndef __QCOM_SND_COMMON_H__
#define __QCOM_SND_COMMON_H__

#include <sound/soc.h>

struct qcom_snd_dailink_data {
	u32 mclk_fs;
	u32 mclk_id;
	u32 clk_direction;
};

struct qcom_snd_common_data {
	struct qcom_snd_dailink_data *link_data;
};

int qcom_snd_parse_of(struct snd_soc_card *card);
int qcom_snd_parse_of_v2(struct snd_soc_card *card, struct qcom_snd_common_data *priv);
int qcom_snd_wcd_jack_setup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_soc_jack *jack, bool *jack_setup);

#endif
