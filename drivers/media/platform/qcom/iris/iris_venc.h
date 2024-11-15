/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_VENC_H_
#define _IRIS_VENC_H_

#include "iris_instance.h"

int venc_inst_init(struct iris_inst *inst);
void venc_inst_deinit(struct iris_inst *inst);
int venc_enum_fmt(struct iris_inst *inst, struct v4l2_fmtdesc *f);
int venc_try_fmt(struct iris_inst *inst, struct v4l2_format *f);
int venc_s_fmt(struct iris_inst *inst, struct v4l2_format *f);
int venc_s_selection(struct iris_inst *inst, struct v4l2_selection *s);
int venc_s_param(struct iris_inst *inst, struct v4l2_streamparm *s_parm);
int venc_g_param(struct iris_inst *inst, struct v4l2_streamparm *s_parm);
int venc_subscribe_event(struct iris_inst *inst,
			 const struct v4l2_event_subscription *sub);
int venc_start_cmd(struct iris_inst *inst);
int venc_stop_cmd(struct iris_inst *inst);
int venc_qbuf(struct iris_inst *inst, struct vb2_buffer *vb2);
int venc_streamon_input(struct iris_inst *inst);
int venc_streamon_output(struct iris_inst *inst);

#endif
