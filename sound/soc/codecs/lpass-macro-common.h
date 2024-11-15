/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LPASS_MACRO_COMMON_H__
#define __LPASS_MACRO_COMMON_H__

/* NPL clock is expected */
#define LPASS_MACRO_FLAG_HAS_NPL_CLOCK		BIT(0)

struct lpass_macro {
	struct device *macro_pd;
	struct device *dcodec_pd;
};

struct lpass_macro *lpass_macro_pds_init(struct device *dev);
void lpass_macro_pds_exit(struct lpass_macro *pds);
int lpass_macro_add_chmap_ctls(struct snd_soc_pcm_runtime *rtd,
				struct snd_soc_dai *dai, int dir);
bool lpass_macro_get_lpi_state(void);
void lpass_macro_set_lpi_state(int status);
#endif /* __LPASS_MACRO_COMMON_H__ */
