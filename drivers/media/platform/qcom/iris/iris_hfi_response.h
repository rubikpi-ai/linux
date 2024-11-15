/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_HFI_RESPONSE_H_
#define _IRIS_HFI_RESPONSE_H_

#include "iris_core.h"

struct sfr_buffer {
	u32 bufsize;
	u8 rg_data[];
};

int __response_handler(struct iris_core *core);

#endif
