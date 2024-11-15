/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __H_VPU_IRIS2_POWER_H__
#define __H_VPU_IRIS2_POWER_H__

u64 iris_calc_freq_iris2(struct iris_inst *inst, u32 data_size);
int iris_calc_bw_iris2(struct iris_inst *inst,
		       struct bus_vote_data *vote_data);

#endif
