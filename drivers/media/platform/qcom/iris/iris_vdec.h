/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_VDEC_H_
#define _IRIS_VDEC_H_

#include "iris_instance.h"

int vdec_inst_init(struct iris_inst *inst);
void vdec_inst_deinit(struct iris_inst *inst);
int vdec_enum_fmt(struct iris_inst *inst, struct v4l2_fmtdesc *f);
int vdec_try_fmt(struct iris_inst *inst, struct v4l2_format *f);
int vdec_s_fmt(struct iris_inst *inst, struct v4l2_format *f);
int vdec_subscribe_event(struct iris_inst *inst, const struct v4l2_event_subscription *sub);
int vdec_init_src_change_param(struct iris_inst *inst);
int vdec_src_change(struct iris_inst *inst);
int vdec_streamon_input(struct iris_inst *inst);
int vdec_streamon_output(struct iris_inst *inst);
int vdec_qbuf(struct iris_inst *inst, struct vb2_buffer *vb2);
int vdec_start_cmd(struct iris_inst *inst);
int vdec_stop_cmd(struct iris_inst *inst);

#endif
