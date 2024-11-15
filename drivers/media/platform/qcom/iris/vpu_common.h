/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _VPU_COMMON_H_
#define _VPU_COMMON_H_

#include <linux/types.h>

struct iris_core;

#define call_vpu_op(d, op, ...)			\
	(((d) && (d)->vpu_ops && (d)->vpu_ops->op) ? \
	((d)->vpu_ops->op(__VA_ARGS__)) : 0)

struct compat_handle {
	const char *compat;
	int (*init)(struct iris_core *core);
};

struct vpu_ops {
	int (*boot_firmware)(struct iris_core *core);
	int (*raise_interrupt)(struct iris_core *core);
	int (*clear_interrupt)(struct iris_core *core);
	int (*watchdog)(struct iris_core *core, u32 intr_status);
	int (*power_on)(struct iris_core *core);
	int (*power_off)(struct iris_core *core);
	int (*prepare_pc)(struct iris_core *core);
};

#define call_session_op(c, op, ...)			\
	(((c) && (c)->session_ops && (c)->session_ops->op) ? \
	((c)->session_ops->op(__VA_ARGS__)) : 0)

struct vpu_session_ops {
	int (*int_buf_size)(struct iris_inst *inst, enum iris_buffer_type type);
	u64 (*calc_freq)(struct iris_inst *inst, u32 data_size);
	int (*calc_bw)(struct iris_inst *inst, struct bus_vote_data *data);
};

int init_vpu(struct iris_core *core);

int write_register(struct iris_core *core, u32 reg, u32 value);
int write_register_masked(struct iris_core *core, u32 reg, u32 value, u32 mask);
int read_register(struct iris_core *core, u32 reg, u32 *value);
int read_register_with_poll_timeout(struct iris_core *core, u32 reg,
				    u32 mask, u32 exp_val, u32 sleep_us,
				    u32 timeout_us);
int set_preset_registers(struct iris_core *core);

#endif
