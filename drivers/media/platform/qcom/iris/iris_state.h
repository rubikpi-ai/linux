/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_STATE_H_
#define _IRIS_STATE_H_

struct iris_core;
struct iris_inst;

enum iris_core_state {
	IRIS_CORE_DEINIT,
	IRIS_CORE_INIT_WAIT,
	IRIS_CORE_INIT,
	IRIS_CORE_ERROR,
};

enum iris_inst_state {
	IRIS_INST_OPEN,
	IRIS_INST_INPUT_STREAMING,
	IRIS_INST_OUTPUT_STREAMING,
	IRIS_INST_STREAMING,
	IRIS_INST_CLOSE,
	IRIS_INST_ERROR,
};

#define IRIS_INST_SUB_NONE		0
#define IRIS_INST_SUB_STATES		6
#define IRIS_INST_MAX_SUB_STATE_VALUE	((1 << IRIS_INST_SUB_STATES) - 1)

enum iris_inst_sub_state {
	IRIS_INST_SUB_DRAIN		= BIT(0),
	IRIS_INST_SUB_DRC		= BIT(1),
	IRIS_INST_SUB_DRAIN_LAST	= BIT(2),
	IRIS_INST_SUB_DRC_LAST		= BIT(3),
	IRIS_INST_SUB_INPUT_PAUSE	= BIT(4),
	IRIS_INST_SUB_OUTPUT_PAUSE	= BIT(5),
};

enum state_change {
	STATE_CHANGE_ALLOW,
	STATE_CHANGE_DISALLOW,
	STATE_CHANGE_IGNORE,
};

#define IS_SESSION_ERROR(inst) \
((inst)->state == IRIS_INST_ERROR)

bool core_in_valid_state(struct iris_core *core);
int iris_change_core_state(struct iris_core *core,
			   enum iris_core_state request_state);

int iris_inst_change_state(struct iris_inst *inst,
			   enum iris_inst_state request_state);
int iris_inst_change_sub_state(struct iris_inst *inst,
			       enum iris_inst_sub_state clear_sub_state,
			       enum iris_inst_sub_state set_sub_state);

bool allow_s_fmt(struct iris_inst *inst, u32 type);
bool allow_reqbufs(struct iris_inst *inst, u32 type);
bool allow_qbuf(struct iris_inst *inst, u32 type);
bool allow_streamon(struct iris_inst *inst, u32 type);
bool allow_streamoff(struct iris_inst *inst, u32 type);
bool allow_s_ctrl(struct iris_inst *inst, u32 cap_id);

int iris_inst_state_change_streamon(struct iris_inst *inst, u32 plane);
int iris_inst_state_change_streamoff(struct iris_inst *inst, u32 plane);

int iris_inst_sub_state_change_drc(struct iris_inst *inst);
int iris_inst_sub_state_change_drain_last(struct iris_inst *inst);
int iris_inst_sub_state_change_drc_last(struct iris_inst *inst);
int iris_inst_sub_state_change_pause(struct iris_inst *inst, u32 plane);
bool is_drc_pending(struct iris_inst *inst);
bool is_drain_pending(struct iris_inst *inst);
bool allow_cmd(struct iris_inst *inst, u32 cmd);

#endif
