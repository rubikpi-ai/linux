/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_HELPERS_H_
#define _IRIS_HELPERS_H_

#include <linux/align.h>
#include <linux/types.h>

#include "iris_instance.h"
#include "iris_buffer.h"
#include "iris_instance.h"
#include "platform_common.h"

#define NUM_MBS_PER_FRAME(__height, __width) \
	((ALIGN(__height, 16) / 16) * (ALIGN(__width, 16) / 16))

int check_core_lock(struct iris_core *core);
bool res_is_less_than(u32 width, u32 height,
		      u32 ref_width, u32 ref_height);
u32 get_port_info(struct iris_inst *inst,
		  enum plat_inst_cap_type cap_id);
enum iris_buffer_type v4l2_type_to_driver(u32 type);
u32 v4l2_type_from_driver(enum iris_buffer_type buffer_type);
void v4l2_to_hfi_enum(struct iris_inst *inst,
		      enum plat_inst_cap_type cap_id, u32 *value);
int get_mbpf(struct iris_inst *inst);
int close_session(struct iris_inst *inst);

bool is_linear_colorformat(u32 colorformat);
bool is_10bit_colorformat(enum colorformat_type colorformat);
bool is_8bit_colorformat(enum colorformat_type colorformat);
bool is_scaling_enabled(struct iris_inst *inst);
bool is_split_mode_enabled(struct iris_inst *inst);
bool is_hierb_type_requested(struct iris_inst *inst);
int signal_session_msg_receipt(struct iris_inst *inst,
			       enum signal_session_response cmd);
struct iris_inst *to_instance(struct iris_core *core, u32 session_id);

u32 v4l2_codec_from_driver(struct iris_inst *inst, enum codec_type codec);
enum codec_type v4l2_codec_to_driver(struct iris_inst *inst, u32 v4l2_codec);
u32 v4l2_colorformat_from_driver(struct iris_inst *inst, enum colorformat_type colorformat);
enum colorformat_type v4l2_colorformat_to_driver(struct iris_inst *inst, u32 v4l2_colorformat);
struct vb2_queue *get_vb2q(struct iris_inst *inst, u32 type);
int check_core_mbps_mbpf(struct iris_inst *inst);
int check_session_supported(struct iris_inst *inst);

struct iris_buffer *get_driver_buf(struct iris_inst *inst, u32 plane, u32 index);
int queue_buffer(struct iris_inst *inst, struct iris_buffer *buf);
int queue_deferred_buffers(struct iris_inst *inst, enum iris_buffer_type buf_type);
int iris_vb2_buffer_done(struct iris_inst *inst,
			 struct iris_buffer *buf);
int iris_release_nonref_buffers(struct iris_inst *inst);
void iris_destroy_buffers(struct iris_inst *inst);
int session_streamoff(struct iris_inst *inst, u32 plane);
int process_resume(struct iris_inst *inst);
int codec_change(struct iris_inst *inst, u32 v4l2_codec);
int process_streamon_input(struct iris_inst *inst);
int process_streamon_output(struct iris_inst *inst);
int vb2_buffer_to_driver(struct vb2_buffer *vb2,
			 struct iris_buffer *buf);
int iris_pm_get(struct iris_core *core);
int iris_pm_put(struct iris_core *core, bool autosuspend);
int iris_pm_get_put(struct iris_core *core);
void iris_pm_touch(struct iris_core *core);
void iris_pm_touch_unlocked(struct iris_core *core);

#endif
