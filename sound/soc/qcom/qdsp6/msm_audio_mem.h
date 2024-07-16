/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _MSM_AUDIO_MEM_H
#define _MSM_AUDIO_MEM_H

#include <linux/dma-mapping.h>
#include <uapi/sound/qcom/msm_audio.h>

int msm_audio_get_phy_addr(int fd, dma_addr_t *paddr, size_t *pa_len);
void msm_audio_mem_crash_handler(void);
#endif /* _LINUX_MSM_AUDIO_MEM_H */
