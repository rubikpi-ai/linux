/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_CTRLS_H_
#define _IRIS_CTRLS_H_

#include "iris_instance.h"

struct cap_entry {
	struct list_head list;
	enum plat_inst_cap_type cap_id;
};

struct ctrl_data {
	bool skip_s_ctrl;
};

int set_u32_enum(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_stage(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_pipe(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_u32(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int adjust_output_order(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_profile(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int vidc_ctrl_handler_deinit(struct iris_inst *inst);
int prepare_dependency_list(struct iris_inst *inst);
int iris_init_instance_caps(struct iris_core *core);
int iris_init_core_caps(struct iris_core *core);
int get_inst_capability(struct iris_inst *inst);
int set_v4l2_properties(struct iris_inst *inst);
int adjust_v4l2_properties(struct iris_inst *inst);
int ctrls_init(struct iris_inst *inst, bool init);
int set_q16(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_level(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int decide_quality_mode(struct iris_inst *inst);
int set_req_sync_frame(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_flip(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_rotation(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_header_mode(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_gop_size(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_bitrate(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_layer_bitrate(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_peak_bitrate(struct iris_inst *inst,
		     enum plat_inst_cap_type cap_id);
int set_bitrate_mode(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_use_and_mark_ltr(struct iris_inst *inst,
			 enum plat_inst_cap_type cap_id);
int set_ir_period(struct iris_inst *inst,
		  enum plat_inst_cap_type cap_id);
int set_min_qp(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_max_qp(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_frame_qp(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_layer_count_and_type(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_slice_count(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int adjust_bitrate(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_layer_bitrate(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_peak_bitrate(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_bitrate_mode(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_gop_size(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_b_frame(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_ltr_count(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_use_ltr(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_mark_ltr(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_ir_period(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_min_quality(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_layer_count(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_entropy_mode(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_slice_count(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_transform_8x8(struct iris_inst *inst, struct v4l2_ctrl *ctrl);

#endif
