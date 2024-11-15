// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "firmware.h"
#include "hfi_defines.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_hfi_packet.h"
#include "iris_hfi_response.h"
#include "iris_helpers.h"
#include "vpu_common.h"

static bool __valdiate_session(struct iris_core *core,
			       struct iris_inst *inst)
{
	struct iris_inst *temp;
	bool valid = false;
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return false;

	list_for_each_entry(temp, &core->instances, list) {
		if (temp == inst) {
			valid = true;
			break;
		}
	}

	return valid;
}

static int iris_power_off(struct iris_core *core)
{
	int ret;

	if (!core->power_enabled)
		return 0;

	ret = call_vpu_op(core, power_off, core);
	if (ret) {
		dev_err(core->dev, "Failed to power off, err: %d\n", ret);
		return ret;
	}
	core->power_enabled = false;

	return ret;
}

static int iris_power_on(struct iris_core *core)
{
	int ret;

	if (core->power_enabled)
		return 0;

	ret = call_vpu_op(core, power_on, core);
	if (ret) {
		dev_err(core->dev, "Failed to power on, err: %d\n", ret);
		return ret;
	}

	core->power_enabled = true;

	return ret;
}

static int sys_init(struct iris_core *core)
{
	int ret;

	ret = hfi_packet_sys_init(core, core->packet, core->packet_size);
	if (ret)
		return ret;

	ret = iris_hfi_queue_cmd_write(core, core->packet);

	return ret;
}

static int sys_image_version(struct iris_core *core)
{
	int ret;

	ret = hfi_packet_image_version(core, core->packet, core->packet_size);
	if (ret)
		return ret;

	ret = iris_hfi_queue_cmd_write(core, core->packet);

	return ret;
}

static int cache_operation_qbuf(struct iris_buffer *buffer)
{
	int ret = 0;

	if (buffer->type == BUF_INPUT && buffer->dmabuf) {
		ret = dma_buf_begin_cpu_access(buffer->dmabuf, DMA_TO_DEVICE);
		if (ret)
			return ret;

		ret = dma_buf_end_cpu_access(buffer->dmabuf, DMA_FROM_DEVICE);
		if (ret)
			return ret;
	} else if (buffer->type == BUF_OUTPUT && buffer->dmabuf) {
		ret = dma_buf_begin_cpu_access(buffer->dmabuf, DMA_FROM_DEVICE);
		if (ret)
			return ret;

		ret = dma_buf_end_cpu_access(buffer->dmabuf, DMA_FROM_DEVICE);
		if (ret)
			return ret;
	}

	return ret;
}

int iris_hfi_core_init(struct iris_core *core)
{
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	ret = iris_hfi_queue_init(core);
	if (ret)
		goto error;

	ret = iris_power_on(core);
	if (ret)
		goto error;

	ret = iris_fw_load(core);
	if (ret)
		goto error_power_off;

	ret = call_vpu_op(core, boot_firmware, core);
	if (ret)
		goto error_power_off;

	ret = sys_init(core);
	if (ret)
		goto error_power_off;

	ret = sys_image_version(core);
	if (ret)
		goto error_power_off;

	return ret;

error_power_off:
	iris_power_off(core);
error:
	dev_err(core->dev, "%s(): failed\n", __func__);

	return ret;
}

int iris_hfi_core_deinit(struct iris_core *core)
{
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (core->state == IRIS_CORE_DEINIT)
		return 0;

	iris_fw_unload(core);
	iris_power_off(core);

	return ret;
}

int iris_hfi_session_open(struct iris_inst *inst)
{
	struct iris_core *core;
	int ret;

	inst->packet_size = 4096;
	inst->packet = kzalloc(inst->packet_size, GFP_KERNEL);
	if (!inst->packet)
		return -ENOMEM;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto fail_free_packet;
	}

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_OPEN,
					 HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED,
					 HFI_PORT_NONE,
					 0,
					 HFI_PAYLOAD_U32,
					 &inst->session_id,
					 sizeof(u32));
	if (ret)
		goto fail_free_packet;

	ret = iris_hfi_queue_cmd_write(core, inst->packet);
	if (ret)
		goto fail_free_packet;

	mutex_unlock(&core->lock);

	return ret;

fail_free_packet:
	kfree(inst->packet);
	inst->packet = NULL;
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_session_close(struct iris_inst *inst)
{
	struct iris_core *core;
	int ret;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_CLOSE,
					 (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED |
					 HFI_HOST_FLAGS_NON_DISCARDABLE),
					 HFI_PORT_NONE,
					 inst->session_id,
					 HFI_PAYLOAD_NONE,
					 NULL,
					 0);
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_session_set_codec(struct iris_inst *inst)
{
	struct iris_core *core;
	int ret;
	u32 codec;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	codec = get_hfi_codec(inst);
	ret = hfi_packet_session_property(inst,
					  HFI_PROP_CODEC,
					  HFI_HOST_FLAGS_NONE,
					  HFI_PORT_NONE,
					  HFI_PAYLOAD_U32_ENUM,
					  &codec,
					  sizeof(u32));
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_session_set_default_header(struct iris_inst *inst)
{
	struct iris_core *core;
	u32 default_header = false;
	int ret;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	default_header = inst->cap[DEFAULT_HEADER].value;
	ret = hfi_packet_session_property(inst,
					  HFI_PROP_DEC_DEFAULT_HEADER,
					  HFI_HOST_FLAGS_NONE,
					  get_hfi_port(inst, INPUT_MPLANE),
					  HFI_PAYLOAD_U32,
					  &default_header,
					  sizeof(u32));
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_start(struct iris_inst *inst, u32 plane)
{
	struct iris_core *core;
	int ret = 0;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	if (plane != INPUT_MPLANE && plane != OUTPUT_MPLANE)
		goto unlock;

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_START,
					 (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED),
					 get_hfi_port(inst, plane),
					 inst->session_id,
					 HFI_PAYLOAD_NONE,
					 NULL,
					 0);
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_stop(struct iris_inst *inst, u32 plane)
{
	struct iris_core *core;
	int ret = 0;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	if (plane != INPUT_MPLANE && plane != OUTPUT_MPLANE)
		goto unlock;

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_STOP,
					 (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED |
					 HFI_HOST_FLAGS_NON_DISCARDABLE),
					 get_hfi_port(inst, plane),
					 inst->session_id,
					 HFI_PAYLOAD_NONE,
					 NULL,
					 0);
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_session_subscribe_mode(struct iris_inst *inst,
				    u32 cmd, u32 plane, u32 payload_type,
				    void *payload, u32 payload_size)
{
	struct iris_core *core;
	int ret;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = hfi_packet_session_command(inst,
					 cmd,
					 (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED),
					 get_hfi_port(inst, plane),
					 inst->session_id,
					 payload_type,
					 payload,
					 payload_size);
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_pause(struct iris_inst *inst, u32 plane)
{
	struct iris_core *core;
	int ret = 0;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	if (plane != INPUT_MPLANE && plane != OUTPUT_MPLANE)
		goto unlock;

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_PAUSE,
					 (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED),
					 get_hfi_port(inst, plane),
					 inst->session_id,
					 HFI_PAYLOAD_NONE,
					 NULL,
					 0);
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_resume(struct iris_inst *inst, u32 plane, u32 payload)
{
	struct iris_core *core;
	int ret = 0;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	if (plane != INPUT_MPLANE && plane != OUTPUT_MPLANE)
		goto unlock;

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_RESUME,
					 (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED),
					 get_hfi_port(inst, plane),
					 inst->session_id,
					 HFI_PAYLOAD_U32,
					 &payload,
					 sizeof(u32));
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_drain(struct iris_inst *inst, u32 plane)
{
	struct iris_core *core;
	int ret = 0;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	if (plane != INPUT_MPLANE)
		goto unlock;

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_DRAIN,
					 (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED |
					 HFI_HOST_FLAGS_NON_DISCARDABLE),
					 get_hfi_port(inst, plane),
					 inst->session_id,
					 HFI_PAYLOAD_NONE,
					 NULL,
					 0);
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

irqreturn_t iris_hfi_isr(int irq, void *data)
{
	disable_irq_nosync(irq);

	return IRQ_WAKE_THREAD;
}

irqreturn_t iris_hfi_isr_handler(int irq, void *data)
{
	struct iris_core *core = data;
	int ret;

	if (!core)
		return IRQ_NONE;

	iris_pm_touch(core);
	ret = iris_pm_get(core);
	if (ret)
		goto exit;

	mutex_lock(&core->lock);
	call_vpu_op(core, clear_interrupt, core);
	mutex_unlock(&core->lock);

	__response_handler(core);

	iris_pm_put(core, true);

exit:
	if (!call_vpu_op(core, watchdog, core, core->intr_status))
		enable_irq(irq);

	return IRQ_HANDLED;
}

int iris_hfi_set_property(struct iris_inst *inst,
			  u32 packet_type, u32 flag, u32 plane, u32 payload_type,
			  void *payload, u32 payload_size)
{
	struct iris_core *core;
	int ret;

	core = inst->core;
	mutex_lock(&core->lock);

	ret = hfi_packet_session_property(inst,
					  packet_type,
					  flag,
					  plane,
					  payload_type,
					  payload,
					  payload_size);
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_set_ir_period(struct iris_inst *inst,
			   u32 packet_type, u32 flag, u32 plane, u32 payload_type,
			   void *payload, u32 payload_size)
{
	u32 sync_frame_req = 0;
	struct iris_core *core;
	int ret;

	core = inst->core;

	mutex_lock(&core->lock);

	ret = hfi_create_header(inst->packet, inst->packet_size,
				inst->session_id, core->header_id++);
	if (ret)
		goto exit;

	if (!inst->ir_enabled) {
		inst->ir_enabled = ((*(u32 *)payload > 0) ? true : false);
		if (inst->ir_enabled && inst->vb2q_dst->streaming) {
			sync_frame_req = HFI_SYNC_FRAME_REQUEST_WITH_PREFIX_SEQ_HDR;
			ret = hfi_create_packet(inst->packet, inst->packet_size,
						HFI_PROP_REQUEST_SYNC_FRAME,
						HFI_HOST_FLAGS_NONE,
						HFI_PAYLOAD_U32_ENUM,
						HFI_PORT_BITSTREAM,
						core->packet_id++,
						&sync_frame_req,
						sizeof(u32));
			if (ret)
				goto exit;
		}
	}

	ret = hfi_create_packet(inst->packet, inst->packet_size,
				packet_type,
				HFI_HOST_FLAGS_NONE,
				HFI_PAYLOAD_U32,
				plane,
				core->packet_id++,
				payload,
				sizeof(u32));
	if (ret)
		goto exit;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

exit:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_queue_buffer(struct iris_inst *inst,
			  struct iris_buffer *buffer)
{
	struct hfi_buffer hfi_buffer;
	struct iris_core *core;
	int ret;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = get_hfi_buffer(inst, buffer, &hfi_buffer);
	if (ret)
		goto unlock;

	ret = cache_operation_qbuf(buffer);
	if (ret)
		goto unlock;

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_BUFFER,
					 HFI_HOST_FLAGS_INTR_REQUIRED,
					 get_hfi_port_from_buffer_type(inst, buffer->type),
					 inst->session_id,
					 HFI_PAYLOAD_STRUCTURE,
					 &hfi_buffer,
					 sizeof(hfi_buffer));
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_release_buffer(struct iris_inst *inst,
			    struct iris_buffer *buffer)
{
	struct hfi_buffer hfi_buffer;
	struct iris_core *core;
	int ret;

	if (!inst->packet || !buffer)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = get_hfi_buffer(inst, buffer, &hfi_buffer);
	if (ret)
		goto unlock;

	hfi_buffer.flags |= HFI_BUF_HOST_FLAG_RELEASE;

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_BUFFER,
					 (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED),
					 get_hfi_port_from_buffer_type(inst, buffer->type),
					 inst->session_id,
					 HFI_PAYLOAD_STRUCTURE,
					 &hfi_buffer,
					 sizeof(hfi_buffer));
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int prepare_pc(struct iris_core *core)
{
	int ret;

	ret = hfi_packet_sys_pc_prep(core, core->packet, core->packet_size);
	if (ret)
		goto err_pc_prep;

	ret = iris_hfi_queue_cmd_write(core, core->packet);
	if (ret)
		goto err_pc_prep;

	return ret;

err_pc_prep:
	dev_err(core->dev, "Failed to prepare venus for power off\n");

	return ret;
}

int iris_hfi_pm_suspend(struct iris_core *core)
{
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (!core_in_valid_state(core))
		return -EINVAL;

	if (!core->power_enabled)
		return 0;

	if (core->skip_pc_count >= MAX_PC_SKIP_COUNT) {
		dev_err(core->dev, "Failed to PC for %d times\n", core->skip_pc_count);
		core->skip_pc_count = 0;
		iris_change_core_state(core, IRIS_CORE_ERROR);
		iris_core_deinit_locked(core);
		return -EINVAL;
	}

	iris_flush_debug_queue(core, core->packet, core->packet_size);

	ret = call_vpu_op(core, prepare_pc, core);
	if (ret) {
		core->skip_pc_count++;
		iris_pm_touch_unlocked(core);
		return -EAGAIN;
	}

	ret = iris_set_hw_state(core, false);
	if (ret)
		return ret;

	ret = iris_power_off(core);
	if (ret)
		return ret;

	core->skip_pc_count = 0;

	return ret;
}

int iris_hfi_pm_resume(struct iris_core *core)
{
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (!core_in_valid_state(core))
		return -EINVAL;

	if (core->power_enabled)
		return 0;

	ret = iris_power_on(core);
	if (ret)
		goto error;

	ret = iris_set_hw_state(core, true);
	if (ret)
		goto err_power_off;

	ret = call_vpu_op(core, boot_firmware, core);
	if (ret)
		goto err_suspend_hw;

	ret = hfi_packet_sys_interframe_powercollapse(core, core->packet, core->packet_size);
	if (ret)
		goto err_suspend_hw;

	ret = iris_hfi_queue_cmd_write(core, core->packet);
	if (ret)
		goto err_suspend_hw;

	return ret;

err_suspend_hw:
	iris_set_hw_state(core, false);
err_power_off:
	iris_power_off(core);
error:
	dev_err(core->dev, "Failed to Resume\n");

	return -EBUSY;
}
