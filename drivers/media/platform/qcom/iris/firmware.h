/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _FIRMWARE_H_
#define _FIRMWARE_H_

#include "iris_core.h"

int iris_fw_load(struct iris_core *core);
int iris_fw_unload(struct iris_core *core);
int iris_set_hw_state(struct iris_core *core, bool resume);

#endif
