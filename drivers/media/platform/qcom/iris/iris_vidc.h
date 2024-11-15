/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_VIDC_H_
#define _IRIS_VIDC_H_

#include "iris_core.h"

int init_ops(struct iris_core *core);
int vidc_open(struct file *filp);
int vidc_close(struct file *filp);

#endif
