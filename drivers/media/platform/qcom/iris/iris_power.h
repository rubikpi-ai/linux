/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_POWER_H_
#define _IRIS_POWER_H_

#include "iris_instance.h"

int iris_scale_power(struct iris_inst *inst);
int iris_update_input_rate(struct iris_inst *inst, u64 time_us);
int iris_flush_input_timer(struct iris_inst *inst);

#endif
