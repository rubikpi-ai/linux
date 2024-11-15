// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_state.h"

#define IRIS_STATE(name)[IRIS_CORE_##name] = "CORE_"#name

static const char * const core_state_names[] = {
	IRIS_STATE(DEINIT),
	IRIS_STATE(INIT_WAIT),
	IRIS_STATE(INIT),
	IRIS_STATE(ERROR),
};

#undef IRIS_STATE

bool core_in_valid_state(struct iris_core *core)
{
	return core->state == IRIS_CORE_INIT ||
		core->state == IRIS_CORE_INIT_WAIT;
}

static const char *core_state_name(enum iris_core_state state)
{
	if ((unsigned int)state < ARRAY_SIZE(core_state_names))
		return core_state_names[state];

	return "UNKNOWN_STATE";
}

static bool iris_allow_core_state_change(struct iris_core *core,
					 enum iris_core_state req_state)
{
	if (core->state == IRIS_CORE_DEINIT)
		return req_state == IRIS_CORE_INIT_WAIT || req_state == IRIS_CORE_ERROR;
	else if (core->state == IRIS_CORE_INIT_WAIT)
		return req_state == IRIS_CORE_INIT || req_state == IRIS_CORE_ERROR;
	else if (core->state == IRIS_CORE_INIT)
		return req_state == IRIS_CORE_DEINIT || req_state == IRIS_CORE_ERROR;
	else if (core->state == IRIS_CORE_ERROR)
		return req_state == IRIS_CORE_DEINIT;

	dev_warn(core->dev, "core state change %s -> %s is not allowed\n",
		 core_state_name(core->state), core_state_name(req_state));

	return false;
}

int iris_change_core_state(struct iris_core *core,
			   enum iris_core_state request_state)
{
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (core->state == request_state)
		return 0;

	if (!iris_allow_core_state_change(core, request_state))
		return -EINVAL;

	core->state = request_state;

	return ret;
}

struct iris_inst_state_change_allow {
	enum iris_inst_state        from;
	enum iris_inst_state        to;
	enum state_change           allow;
};

static enum state_change iris_allow_inst_state_change(struct iris_inst *inst,
						      enum iris_inst_state req_state)
{
	enum state_change allow = STATE_CHANGE_ALLOW;
	int cnt;
	static struct iris_inst_state_change_allow state[] = {
		/* from, to, allow */
		{IRIS_INST_OPEN,             IRIS_INST_OPEN,               STATE_CHANGE_IGNORE    },
		{IRIS_INST_OPEN,             IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_ALLOW     },
		{IRIS_INST_OPEN,             IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_ALLOW     },
		{IRIS_INST_OPEN,             IRIS_INST_STREAMING,          STATE_CHANGE_DISALLOW  },
		{IRIS_INST_OPEN,             IRIS_INST_CLOSE,              STATE_CHANGE_ALLOW     },
		{IRIS_INST_OPEN,             IRIS_INST_ERROR,              STATE_CHANGE_ALLOW     },

		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_OPEN,               STATE_CHANGE_ALLOW     },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_IGNORE    },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_DISALLOW  },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_STREAMING,          STATE_CHANGE_ALLOW     },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_CLOSE,              STATE_CHANGE_ALLOW     },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_ERROR,              STATE_CHANGE_ALLOW     },

		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_OPEN,               STATE_CHANGE_ALLOW     },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_DISALLOW  },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_IGNORE    },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_STREAMING,          STATE_CHANGE_ALLOW     },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_CLOSE,              STATE_CHANGE_ALLOW     },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_ERROR,              STATE_CHANGE_ALLOW     },

		{IRIS_INST_STREAMING,        IRIS_INST_OPEN,               STATE_CHANGE_DISALLOW  },
		{IRIS_INST_STREAMING,        IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_ALLOW     },
		{IRIS_INST_STREAMING,        IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_ALLOW     },
		{IRIS_INST_STREAMING,        IRIS_INST_STREAMING,          STATE_CHANGE_IGNORE    },
		{IRIS_INST_STREAMING,        IRIS_INST_CLOSE,              STATE_CHANGE_ALLOW     },
		{IRIS_INST_STREAMING,        IRIS_INST_ERROR,              STATE_CHANGE_ALLOW     },

		{IRIS_INST_CLOSE,            IRIS_INST_OPEN,               STATE_CHANGE_DISALLOW  },
		{IRIS_INST_CLOSE,            IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_DISALLOW  },
		{IRIS_INST_CLOSE,            IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_DISALLOW  },
		{IRIS_INST_CLOSE,            IRIS_INST_STREAMING,          STATE_CHANGE_DISALLOW  },
		{IRIS_INST_CLOSE,            IRIS_INST_CLOSE,              STATE_CHANGE_IGNORE    },
		{IRIS_INST_CLOSE,            IRIS_INST_ERROR,              STATE_CHANGE_IGNORE    },

		{IRIS_INST_ERROR,            IRIS_INST_OPEN,               STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_STREAMING,          STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_CLOSE,              STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_ERROR,              STATE_CHANGE_IGNORE    },
	};

	for (cnt = 0; cnt < ARRAY_SIZE(state); cnt++) {
		if (state[cnt].from == inst->state && state[cnt].to == req_state) {
			allow = state[cnt].allow;
			break;
		}
	}

	return allow;
}

int iris_inst_change_state(struct iris_inst *inst,
			   enum iris_inst_state request_state)
{
	enum state_change allow;

	if (IS_SESSION_ERROR(inst))
		return 0;

	if (inst->state == request_state)
		return 0;

	allow = iris_allow_inst_state_change(inst, request_state);
	if (allow != STATE_CHANGE_ALLOW)
		return (allow == STATE_CHANGE_DISALLOW ? -EINVAL : 0);

	inst->state = request_state;

	return 0;
}

bool allow_s_fmt(struct iris_inst *inst, u32 type)
{
	return (inst->state == IRIS_INST_OPEN) ||
		(type == OUTPUT_MPLANE && inst->state == IRIS_INST_INPUT_STREAMING) ||
		(type == INPUT_MPLANE && inst->state == IRIS_INST_OUTPUT_STREAMING);
}

bool allow_reqbufs(struct iris_inst *inst, u32 type)
{
	return (inst->state == IRIS_INST_OPEN) ||
		(type == OUTPUT_MPLANE && inst->state == IRIS_INST_INPUT_STREAMING) ||
		(type == INPUT_MPLANE && inst->state == IRIS_INST_OUTPUT_STREAMING);
}

bool allow_qbuf(struct iris_inst *inst, u32 type)
{
	return (type == INPUT_MPLANE && inst->state == IRIS_INST_INPUT_STREAMING) ||
		(type == INPUT_MPLANE && inst->state == IRIS_INST_STREAMING) ||
		(type == OUTPUT_MPLANE && inst->state == IRIS_INST_OUTPUT_STREAMING) ||
		(type == OUTPUT_MPLANE && inst->state == IRIS_INST_STREAMING);
}

bool allow_streamon(struct iris_inst *inst, u32 type)
{
	return (type == INPUT_MPLANE && inst->state == IRIS_INST_OPEN) ||
		(type == INPUT_MPLANE && inst->state == IRIS_INST_OUTPUT_STREAMING) ||
		(type == OUTPUT_MPLANE && inst->state == IRIS_INST_OPEN) ||
		(type == OUTPUT_MPLANE && inst->state == IRIS_INST_INPUT_STREAMING);
}

bool allow_streamoff(struct iris_inst *inst, u32 type)
{
	return (type == INPUT_MPLANE && inst->state == IRIS_INST_INPUT_STREAMING) ||
		(type == INPUT_MPLANE && inst->state == IRIS_INST_STREAMING) ||
		(type == OUTPUT_MPLANE && inst->state == IRIS_INST_OUTPUT_STREAMING) ||
		(type == OUTPUT_MPLANE && inst->state == IRIS_INST_STREAMING);
}

bool allow_s_ctrl(struct iris_inst *inst, u32 cap_id)
{
	return ((inst->state == IRIS_INST_OPEN) ||
		((inst->cap[cap_id].flags & CAP_FLAG_DYNAMIC_ALLOWED) &&
		 ((inst->state == IRIS_INST_INPUT_STREAMING && inst->domain == DECODER) ||
		  (inst->state == IRIS_INST_OUTPUT_STREAMING && inst->domain == ENCODER) ||
		  (inst->state == IRIS_INST_STREAMING))));
}

int iris_inst_state_change_streamon(struct iris_inst *inst, u32 plane)
{
	enum iris_inst_state new_state = IRIS_INST_ERROR;

	if (plane == INPUT_MPLANE) {
		if (inst->state == IRIS_INST_OPEN)
			new_state = IRIS_INST_INPUT_STREAMING;
		else if (inst->state == IRIS_INST_OUTPUT_STREAMING)
			new_state = IRIS_INST_STREAMING;
	} else if (plane == OUTPUT_MPLANE) {
		if (inst->state == IRIS_INST_OPEN)
			new_state = IRIS_INST_OUTPUT_STREAMING;
		else if (inst->state == IRIS_INST_INPUT_STREAMING)
			new_state = IRIS_INST_STREAMING;
	}

	return iris_inst_change_state(inst, new_state);
}

int iris_inst_state_change_streamoff(struct iris_inst *inst, u32 plane)
{
	enum iris_inst_state new_state = IRIS_INST_ERROR;

	if (plane == INPUT_MPLANE) {
		if (inst->state == IRIS_INST_INPUT_STREAMING)
			new_state = IRIS_INST_OPEN;
		else if (inst->state == IRIS_INST_STREAMING)
			new_state = IRIS_INST_OUTPUT_STREAMING;
	} else if (plane == OUTPUT_MPLANE) {
		if (inst->state == IRIS_INST_OUTPUT_STREAMING)
			new_state = IRIS_INST_OPEN;
		else if (inst->state == IRIS_INST_STREAMING)
			new_state = IRIS_INST_INPUT_STREAMING;
	}

	return iris_inst_change_state(inst, new_state);
}

struct iris_inst_sub_state_change_allow {
	enum iris_inst_state state;
	enum state_change allow;
	u32 sub_state_mask;
};

static int iris_inst_allow_sub_state(struct iris_inst *inst, enum iris_inst_sub_state sub_state)
{
	int i;
	static struct iris_inst_sub_state_change_allow sub_state_allow[] = {
		{IRIS_INST_OPEN,              STATE_CHANGE_DISALLOW,    IRIS_INST_SUB_DRC         |
									IRIS_INST_SUB_DRAIN       |
									IRIS_INST_SUB_DRC_LAST    |
									IRIS_INST_SUB_DRAIN_LAST  |
									IRIS_INST_SUB_INPUT_PAUSE |
									IRIS_INST_SUB_OUTPUT_PAUSE},

		{IRIS_INST_INPUT_STREAMING,   STATE_CHANGE_DISALLOW,    IRIS_INST_SUB_DRC_LAST    |
									IRIS_INST_SUB_DRAIN_LAST  |
									IRIS_INST_SUB_OUTPUT_PAUSE},

		{IRIS_INST_INPUT_STREAMING,   STATE_CHANGE_ALLOW,       IRIS_INST_SUB_DRC         |
									IRIS_INST_SUB_DRAIN       |
									IRIS_INST_SUB_INPUT_PAUSE },

		{IRIS_INST_OUTPUT_STREAMING,  STATE_CHANGE_DISALLOW,    IRIS_INST_SUB_DRC         |
									IRIS_INST_SUB_DRAIN       |
									IRIS_INST_SUB_INPUT_PAUSE },

		{IRIS_INST_OUTPUT_STREAMING,  STATE_CHANGE_ALLOW,       IRIS_INST_SUB_DRC_LAST    |
									IRIS_INST_SUB_DRAIN_LAST  |
									IRIS_INST_SUB_OUTPUT_PAUSE},

		{IRIS_INST_STREAMING,         STATE_CHANGE_ALLOW,       IRIS_INST_SUB_DRC         |
									IRIS_INST_SUB_DRAIN       |
									IRIS_INST_SUB_DRC_LAST    |
									IRIS_INST_SUB_DRAIN_LAST  |
									IRIS_INST_SUB_INPUT_PAUSE |
									IRIS_INST_SUB_OUTPUT_PAUSE},

		{IRIS_INST_CLOSE,             STATE_CHANGE_ALLOW,       IRIS_INST_SUB_DRC         |
									IRIS_INST_SUB_DRAIN       |
									IRIS_INST_SUB_DRC_LAST    |
									IRIS_INST_SUB_DRAIN_LAST  |
									IRIS_INST_SUB_INPUT_PAUSE |
									IRIS_INST_SUB_OUTPUT_PAUSE},

		{IRIS_INST_ERROR,             STATE_CHANGE_ALLOW,       IRIS_INST_SUB_DRC         |
									IRIS_INST_SUB_DRAIN       |
									IRIS_INST_SUB_DRC_LAST    |
									IRIS_INST_SUB_DRAIN_LAST  |
									IRIS_INST_SUB_INPUT_PAUSE |
									IRIS_INST_SUB_OUTPUT_PAUSE},
	};

	if (!sub_state)
		return 0;

	for (i = 0; i < ARRAY_SIZE(sub_state_allow); i++) {
		if (sub_state_allow[i].state != inst->state)
			continue;

		if (sub_state_allow[i].allow == STATE_CHANGE_DISALLOW)
			if (sub_state_allow[i].sub_state_mask & sub_state) {
				dev_dbg(inst->core->dev, "state %d with disallowed sub state %x\n",
					inst->state, sub_state);
				return -EINVAL;
			}

		if (sub_state_allow[i].allow == STATE_CHANGE_ALLOW) {
			if ((sub_state_allow[i].sub_state_mask & sub_state) != sub_state)
				return -EINVAL;
		}
	}

	return 0;
}

int iris_inst_change_sub_state(struct iris_inst *inst,
			       enum iris_inst_sub_state clear_sub_state,
			       enum iris_inst_sub_state set_sub_state)
{
	enum iris_inst_sub_state prev_sub_state;
	int ret;

	if (IS_SESSION_ERROR(inst))
		return 0;

	if (!clear_sub_state && !set_sub_state)
		return 0;

	if ((clear_sub_state & set_sub_state) ||
	    set_sub_state > IRIS_INST_MAX_SUB_STATE_VALUE ||
	    clear_sub_state > IRIS_INST_MAX_SUB_STATE_VALUE)
		return -EINVAL;

	prev_sub_state = inst->sub_state;

	ret = iris_inst_allow_sub_state(inst, set_sub_state);
	if (ret)
		return ret;

	inst->sub_state |= set_sub_state;
	inst->sub_state &= ~clear_sub_state;

	if (inst->sub_state != prev_sub_state)
		dev_dbg(inst->core->dev, "state %d and sub state changed to %x\n",
			inst->sub_state, inst->sub_state);

	return 0;
}

int iris_inst_sub_state_change_drc(struct iris_inst *inst)
{
	enum iris_inst_sub_state set_sub_state = 0;

	if (inst->sub_state & IRIS_INST_SUB_DRC)
		return STATE_CHANGE_DISALLOW;

	if (inst->state == IRIS_INST_INPUT_STREAMING ||
	    inst->state == IRIS_INST_OPEN)
		set_sub_state = IRIS_INST_SUB_INPUT_PAUSE;
	else
		set_sub_state = IRIS_INST_SUB_DRC | IRIS_INST_SUB_INPUT_PAUSE;

	return iris_inst_change_sub_state(inst, 0, set_sub_state);
}

int iris_inst_sub_state_change_drain_last(struct iris_inst *inst)
{
	enum iris_inst_sub_state set_sub_state = IRIS_INST_SUB_NONE;

	if (inst->sub_state & IRIS_INST_SUB_DRAIN_LAST)
		return STATE_CHANGE_DISALLOW;

	if (!(inst->sub_state & IRIS_INST_SUB_DRAIN) ||
	    !(inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE))
		return STATE_CHANGE_DISALLOW;

	set_sub_state = IRIS_INST_SUB_DRAIN_LAST | IRIS_INST_SUB_OUTPUT_PAUSE;

	return iris_inst_change_sub_state(inst, 0, set_sub_state);
}

int iris_inst_sub_state_change_drc_last(struct iris_inst *inst)
{
	enum iris_inst_sub_state set_sub_state = IRIS_INST_SUB_NONE;

	if (inst->sub_state & IRIS_INST_SUB_DRC_LAST)
		return STATE_CHANGE_DISALLOW;

	if (!(inst->sub_state & IRIS_INST_SUB_DRC) ||
	    !(inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE))
		return STATE_CHANGE_DISALLOW;

	set_sub_state = IRIS_INST_SUB_DRC_LAST | IRIS_INST_SUB_OUTPUT_PAUSE;

	return iris_inst_change_sub_state(inst, 0, set_sub_state);
}

int iris_inst_sub_state_change_pause(struct iris_inst *inst, u32 plane)
{
	enum iris_inst_sub_state set_sub_state = IRIS_INST_SUB_NONE;

	if (plane == INPUT_MPLANE) {
		if (inst->sub_state & IRIS_INST_SUB_DRC &&
		    !(inst->sub_state & IRIS_INST_SUB_DRC_LAST))
			return STATE_CHANGE_DISALLOW;

		if (inst->sub_state & IRIS_INST_SUB_DRAIN &&
		    !(inst->sub_state & IRIS_INST_SUB_DRAIN_LAST))
			return STATE_CHANGE_DISALLOW;

		set_sub_state = IRIS_INST_SUB_INPUT_PAUSE;
	} else {
		set_sub_state = IRIS_INST_SUB_OUTPUT_PAUSE;
	}

	return iris_inst_change_sub_state(inst, 0, set_sub_state);
}

bool is_drc_pending(struct iris_inst *inst)
{
	return inst->sub_state & IRIS_INST_SUB_DRC &&
		inst->sub_state & IRIS_INST_SUB_DRC_LAST;
}

bool is_drain_pending(struct iris_inst *inst)
{
	return inst->sub_state & IRIS_INST_SUB_DRAIN &&
		inst->sub_state & IRIS_INST_SUB_DRAIN_LAST;
}

bool allow_cmd(struct iris_inst *inst, u32 cmd)
{
	if (cmd == V4L2_DEC_CMD_START) {
		if (inst->state == IRIS_INST_INPUT_STREAMING ||
		    inst->state == IRIS_INST_OUTPUT_STREAMING ||
		    inst->state == IRIS_INST_STREAMING)
			if (is_drc_pending(inst) || is_drain_pending(inst))
				return true;
	} else if (cmd == V4L2_DEC_CMD_STOP) {
		if (inst->state == IRIS_INST_INPUT_STREAMING ||
		    inst->state == IRIS_INST_STREAMING)
			if (inst->sub_state != IRIS_INST_SUB_DRAIN)
				return true;
	}

	return false;
}
