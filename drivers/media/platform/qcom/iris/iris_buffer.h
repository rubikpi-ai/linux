/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_BUFFER_H_
#define _IRIS_BUFFER_H_

#define MIN_BUFFERS  4

#include "iris_common.h"

struct iris_inst;

struct iris_buffers {
	struct list_head	list; // list of "struct iris_buffer"
	u32			min_count;
	u32			actual_count;
	u32			size;
	bool			reuse;
};

struct iris_buffers_info {
	struct iris_buffers	input;
	struct iris_buffers	output;
	struct iris_buffers	read_only;
	struct iris_buffers	bin;
	struct iris_buffers	arp;
	struct iris_buffers	comv;
	struct iris_buffers	non_comv;
	struct iris_buffers	line;
	struct iris_buffers	dpb;
	struct iris_buffers	persist;
	struct iris_buffers	vpss;
};

int update_buffer_count(struct iris_inst *inst, u32 plane);
int iris_get_buf_min_count(struct iris_inst *inst,
			   enum iris_buffer_type buffer_type);
int iris_get_buffer_size(struct iris_inst *inst,
			 enum iris_buffer_type buffer_type);
struct iris_buffers *iris_get_buffer_list(struct iris_inst *inst,
					  enum iris_buffer_type buffer_type);
int iris_allocate_buffers(struct iris_inst *inst,
			  enum iris_buffer_type buf_type,
			  u32 num_buffers);
int iris_free_buffers(struct iris_inst *inst,
		      enum iris_buffer_type buf_type);

int iris_get_internal_buffers(struct iris_inst *inst,
			      u32 plane);
int iris_create_input_internal_buffers(struct iris_inst *inst);
int iris_create_output_internal_buffers(struct iris_inst *inst);
int iris_queue_input_internal_buffers(struct iris_inst *inst);
int iris_queue_output_internal_buffers(struct iris_inst *inst);
int iris_destroy_internal_buffer(struct iris_inst *inst,
				 struct iris_buffer *buffer);
int iris_destroy_internal_buffers(struct iris_inst *inst,
				  u32 plane);
int iris_alloc_and_queue_session_int_bufs(struct iris_inst *inst,
					  enum iris_buffer_type buffer_type);
int iris_alloc_and_queue_input_int_bufs(struct iris_inst *inst);
int iris_alloc_and_queue_additional_dpb_buffers(struct iris_inst *inst);

#endif
