/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _MEMORY_H_
#define _MEMORY_H_

#include "iris_core.h"

struct iris_inst;

struct iris_mem_pool_header {
	struct list_head	list;
	bool			busy;
	void			*buf;
};

struct iris_mem_pool {
	u32			size;
	struct list_head	free_hdr_list; /* list of struct iris_mem_pool_header */
	struct list_head	busy_hdr_list; /* list of struct iris_mem_pool_header */
};

int iris_mem_pool_init(struct iris_inst *inst);
void iris_mem_pool_deinit(struct iris_inst *inst);
void *iris_get_buffer_from_pool(struct iris_inst *inst);
void iris_return_buffer_to_pool(struct iris_inst *inst, void *vidc_buf);

#endif
