/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_HFI_H_
#define _IRIS_HFI_H_

#include "iris_instance.h"
#include "iris_core.h"

#define MAX_PC_SKIP_COUNT 10

int iris_hfi_core_init(struct iris_core *core);
int iris_hfi_core_deinit(struct iris_core *core);
int iris_hfi_session_open(struct iris_inst *inst);
int iris_hfi_session_close(struct iris_inst *inst);
int iris_hfi_session_subscribe_mode(struct iris_inst *inst,
				    u32 cmd, u32 plane, u32 payload_type,
				    void *payload, u32 payload_size);
int iris_hfi_set_property(struct iris_inst *inst,
			  u32 packet_type, u32 flag, u32 plane, u32 payload_type,
			  void *payload, u32 payload_size);

int iris_hfi_session_set_codec(struct iris_inst *inst);
int iris_hfi_session_set_default_header(struct iris_inst *inst);

int iris_hfi_start(struct iris_inst *inst, u32 plane);
int iris_hfi_stop(struct iris_inst *inst, u32 plane);
int iris_hfi_drain(struct iris_inst *inst, u32 plane);
int iris_hfi_pause(struct iris_inst *inst, u32 plane);
int iris_hfi_resume(struct iris_inst *inst, u32 plane, u32 cmd);
int iris_hfi_queue_buffer(struct iris_inst *inst,
			  struct iris_buffer *buffer);
int iris_hfi_release_buffer(struct iris_inst *inst,
			    struct iris_buffer *buffer);
int iris_hfi_pm_suspend(struct iris_core *core);
int iris_hfi_pm_resume(struct iris_core *core);
int prepare_pc(struct iris_core *core);

irqreturn_t iris_hfi_isr(int irq, void *data);
irqreturn_t iris_hfi_isr_handler(int irq, void *data);
int iris_hfi_set_ir_period(struct iris_inst *inst,
			   u32 packet_type, u32 flag, u32 plane, u32 payload_type,
			   void *payload, u32 payload_size);

#endif
