// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hfi_defines.h"
#include "iris_buffer.h"
#include "iris_helpers.h"
#include "iris_hfi_packet.h"
#include "iris_hfi_queue.h"
#include "iris_hfi_response.h"
#include "iris_vdec.h"
#include "memory.h"

struct iris_core_hfi_range {
	u32 begin;
	u32 end;
	int (*handle)(struct iris_core *core, struct hfi_packet *pkt);
};

struct iris_inst_hfi_range {
	u32 begin;
	u32 end;
	int (*handle)(struct iris_inst *inst, struct hfi_packet *pkt);
};

struct iris_hfi_buffer_handle {
	enum hfi_buffer_type type;
	int (*handle)(struct iris_inst *inst, struct hfi_buffer *buffer);
};

struct iris_hfi_packet_handle {
	enum hfi_buffer_type type;
	int (*handle)(struct iris_inst *inst, struct hfi_packet *pkt);
};

static void print_sfr_message(struct iris_core *core)
{
	struct sfr_buffer *vsfr = NULL;
	u32 vsfr_size = 0;
	void *p = NULL;

	vsfr = (struct sfr_buffer *)core->sfr.kernel_vaddr;
	if (vsfr) {
		if (vsfr->bufsize != core->sfr.size)
			return;
		vsfr_size = vsfr->bufsize - sizeof(u32);
		p = memchr(vsfr->rg_data, '\0', vsfr_size);
	/* SFR isn't guaranteed to be NULL terminated */
		if (!p)
			vsfr->rg_data[vsfr_size - 1] = '\0';

		dev_err(core->dev, "SFR Message from FW: %s\n", vsfr->rg_data);
	}
}

static bool is_valid_hfi_buffer_type(u32 buffer_type)
{
	if (buffer_type != HFI_BUFFER_BITSTREAM &&
	    buffer_type != HFI_BUFFER_RAW &&
	    buffer_type != HFI_BUFFER_BIN &&
	    buffer_type != HFI_BUFFER_ARP &&
	    buffer_type != HFI_BUFFER_COMV &&
	    buffer_type != HFI_BUFFER_NON_COMV &&
	    buffer_type != HFI_BUFFER_LINE &&
	    buffer_type != HFI_BUFFER_DPB &&
	    buffer_type != HFI_BUFFER_PERSIST &&
	    buffer_type != HFI_BUFFER_VPSS) {
		return false;
	}

	return true;
}

static bool is_valid_hfi_port(u32 port, u32 buffer_type)
{
	if (port == HFI_PORT_NONE &&
	    buffer_type != HFI_BUFFER_ARP &&
		buffer_type != HFI_BUFFER_PERSIST)
		return false;

	if (port != HFI_PORT_BITSTREAM && port != HFI_PORT_RAW)
		return false;

	return true;
}

static void cache_operation_dqbuf(struct iris_buffer *buf)
{
	if  (buf->type == BUF_OUTPUT && buf->dmabuf) {
		dma_buf_begin_cpu_access(buf->dmabuf, DMA_FROM_DEVICE);
		dma_buf_end_cpu_access(buf->dmabuf, DMA_FROM_DEVICE);
	}
}

static int get_driver_buffer_flags(struct iris_inst *inst, u32 hfi_flags)
{
	u32 driver_flags = 0;

	if (inst->hfi_frame_info.picture_type & HFI_PICTURE_IDR)
		driver_flags |= BUF_FLAG_KEYFRAME;
	else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_P)
		driver_flags |= BUF_FLAG_PFRAME;
	else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_B)
		driver_flags |= BUF_FLAG_BFRAME;
	else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_I)
		driver_flags |= BUF_FLAG_KEYFRAME;
	else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_CRA)
		driver_flags |= BUF_FLAG_KEYFRAME;
	else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_BLA)
		driver_flags |= BUF_FLAG_KEYFRAME;

	if (inst->hfi_frame_info.data_corrupt)
		driver_flags |= BUF_FLAG_ERROR;

	if (inst->hfi_frame_info.overflow)
		driver_flags |= BUF_FLAG_ERROR;

	if ((inst->domain == ENCODER && (hfi_flags & HFI_BUF_FW_FLAG_LAST)) ||
	    (inst->domain == DECODER && (hfi_flags & HFI_BUF_FW_FLAG_LAST ||
					 hfi_flags & HFI_BUF_FW_FLAG_PSC_LAST)))
		driver_flags |= BUF_FLAG_LAST;

	return driver_flags;
}

static bool validate_packet_payload(struct hfi_packet *pkt)
{
	u32 payload_size = 0;

	switch (pkt->payload_info) {
	case HFI_PAYLOAD_U32:
	case HFI_PAYLOAD_S32:
	case HFI_PAYLOAD_Q16:
	case HFI_PAYLOAD_U32_ENUM:
	case HFI_PAYLOAD_32_PACKED:
		payload_size = 4;
		break;
	case HFI_PAYLOAD_U64:
	case HFI_PAYLOAD_S64:
	case HFI_PAYLOAD_64_PACKED:
		payload_size = 8;
		break;
	case HFI_PAYLOAD_STRUCTURE:
		if (pkt->type == HFI_CMD_BUFFER)
			payload_size = sizeof(struct hfi_buffer);
		break;
	default:
		payload_size = 0;
		break;
	}

	if (pkt->size < sizeof(struct hfi_packet) + payload_size)
		return false;

	return true;
}

static int validate_packet(u8 *response_pkt, u8 *core_resp_pkt, u32 core_resp_pkt_size)
{
	u32 response_pkt_size = 0;
	u8 *response_limit;

	if (!response_pkt || !core_resp_pkt || !core_resp_pkt_size)
		return -EINVAL;

	response_limit = core_resp_pkt + core_resp_pkt_size;

	if (response_pkt < core_resp_pkt || response_pkt > response_limit)
		return -EINVAL;

	response_pkt_size = *(u32 *)response_pkt;
	if (!response_pkt_size)
		return -EINVAL;

	if (response_pkt_size < sizeof(struct hfi_packet))
		return -EINVAL;

	if (response_pkt + response_pkt_size > response_limit)
		return -EINVAL;

	return 0;
}

static int validate_hdr_packet(struct iris_core *core, struct hfi_header *hdr)
{
	struct hfi_packet *packet;
	int i, ret = 0;
	u8 *pkt;

	if (hdr->size < sizeof(*hdr) + sizeof(*packet))
		return -EINVAL;

	pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));

	for (i = 0; i < hdr->num_packets; i++) {
		packet = (struct hfi_packet *)pkt;
		ret = validate_packet(pkt, core->response_packet, core->packet_size);
		if (ret)
			return ret;

		pkt += packet->size;
	}

	return ret;
}

static int handle_session_info(struct iris_inst *inst,
			       struct hfi_packet *pkt)
{
	struct iris_core *core;
	int ret = 0;
	char *info;

	core = inst->core;

	switch (pkt->type) {
	case HFI_INFO_UNSUPPORTED:
		info = "unsupported";
		break;
	case HFI_INFO_DATA_CORRUPT:
		info = "data corrupt";
		inst->hfi_frame_info.data_corrupt = 1;
		break;
	case HFI_INFO_BUFFER_OVERFLOW:
		info = "buffer overflow";
		inst->hfi_frame_info.overflow = 1;
		break;
	case HFI_INFO_HFI_FLAG_DRAIN_LAST:
		info = "drain last flag";
		ret = iris_inst_sub_state_change_drain_last(inst);
		break;
	case HFI_INFO_HFI_FLAG_PSC_LAST:
		info = "drc last flag";
		ret = iris_inst_sub_state_change_drc_last(inst);
		break;
	default:
		info = "unknown";
		break;
	}

	dev_dbg(core->dev, "session info received %#x: %s\n",
		pkt->type, info);

	return ret;
}

static int handle_session_error(struct iris_inst *inst,
				struct hfi_packet *pkt)
{
	struct iris_core *core;
	char *error;

	core = inst->core;

	switch (pkt->type) {
	case HFI_ERROR_MAX_SESSIONS:
		error = "exceeded max sessions";
		break;
	case HFI_ERROR_UNKNOWN_SESSION:
		error = "unknown session id";
		break;
	case HFI_ERROR_INVALID_STATE:
		error = "invalid operation for current state";
		break;
	case HFI_ERROR_INSUFFICIENT_RESOURCES:
		error = "insufficient resources";
		break;
	case HFI_ERROR_BUFFER_NOT_SET:
		error = "internal buffers not set";
		break;
	case HFI_ERROR_FATAL:
		error = "fatal error";
		break;
	default:
		error = "unknown";
		break;
	}

	dev_err(core->dev, "session error received %#x: %s\n",
		pkt->type, error);
	iris_inst_change_state(inst, IRIS_INST_ERROR);

	return 0;
}

static int handle_system_error(struct iris_core *core,
			       struct hfi_packet *pkt)
{
	print_sfr_message(core);

	iris_core_deinit(core);

	return 0;
}

static int handle_system_init(struct iris_core *core,
			      struct hfi_packet *pkt)
{
	if (!(pkt->flags & HFI_FW_FLAGS_SUCCESS))
		return 0;

	mutex_lock(&core->lock);
	if (pkt->packet_id != core->sys_init_id)
		goto unlock;

	iris_change_core_state(core, IRIS_CORE_INIT);

unlock:
	mutex_unlock(&core->lock);

	return 0;
}

static int handle_session_close(struct iris_inst *inst,
				struct hfi_packet *pkt)
{
	signal_session_msg_receipt(inst, SIGNAL_CMD_CLOSE);

	return 0;
}

static int handle_read_only_buffer(struct iris_inst *inst,
				   struct iris_buffer *buf)
{
	struct iris_buffer *ro_buf, *iter = NULL;
	bool found = false;

	if (inst->domain != DECODER && inst->domain != ENCODER)
		return 0;

	list_for_each_entry(iter, &inst->buffers.read_only.list, list) {
		if (iter->device_addr == buf->device_addr) {
			found = true;
			ro_buf = iter;
			break;
		}
	}

	if (!found) {
		ro_buf = iris_get_buffer_from_pool(inst);
		if (!ro_buf)
			return -ENOMEM;
		ro_buf->index = -1;
		ro_buf->inst = inst;
		ro_buf->type = buf->type;
		ro_buf->fd = buf->fd;
		ro_buf->dmabuf = buf->dmabuf;
		ro_buf->device_addr = buf->device_addr;
		ro_buf->data_offset = buf->data_offset;
		INIT_LIST_HEAD(&ro_buf->list);
		list_add_tail(&ro_buf->list, &inst->buffers.read_only.list);
	}
	ro_buf->attr |= BUF_ATTR_READ_ONLY;

	return 0;
}

static int handle_non_read_only_buffer(struct iris_inst *inst,
				       struct hfi_buffer *buffer)
{
	struct iris_buffer *ro_buf;

	if (inst->domain != DECODER && inst->domain != ENCODER)
		return 0;

	list_for_each_entry(ro_buf, &inst->buffers.read_only.list, list) {
		if (ro_buf->device_addr == buffer->base_address) {
			ro_buf->attr &= ~BUF_ATTR_READ_ONLY;
			break;
		}
	}

	return 0;
}

static int handle_release_output_buffer(struct iris_inst *inst,
					struct hfi_buffer *buffer)
{
	struct iris_buffer *buf, *iter;
	bool found = false;

	list_for_each_entry(iter, &inst->buffers.read_only.list, list) {
		if (iter->device_addr == buffer->base_address &&
		    iter->attr & BUF_ATTR_PENDING_RELEASE) {
			found = true;
			buf = iter;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	buf->attr &= ~BUF_ATTR_READ_ONLY;
	buf->attr &= ~BUF_ATTR_PENDING_RELEASE;

	return 0;
}

static int handle_input_buffer(struct iris_inst *inst,
			       struct hfi_buffer *buffer)
{
	struct iris_buffers *buffers;
	struct iris_buffer *buf, *iter;
	bool found;

	buffers = iris_get_buffer_list(inst, BUF_INPUT);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(iter, &buffers->list, list) {
		if (iter->index == buffer->index) {
			found = true;
			buf = iter;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	if (!(buf->attr & BUF_ATTR_QUEUED))
		return 0;

	buf->data_size = buffer->data_size;
	buf->attr &= ~BUF_ATTR_QUEUED;
	buf->attr |= BUF_ATTR_DEQUEUED;

	buf->flags = get_driver_buffer_flags(inst, buffer->flags);

	return 0;
}

static int handle_output_buffer(struct iris_inst *inst,
				struct hfi_buffer *hfi_buffer)
{
	struct iris_buffer *buf, *iter;
	struct iris_buffers *buffers;
	bool found, fatal = false;
	int ret = 0;

	if (hfi_buffer->flags & HFI_BUF_FW_FLAG_LAST) {
		ret = iris_inst_sub_state_change_drain_last(inst);
		if (ret)
			return ret;
	}

	if (inst->domain == DECODER) {
		if (hfi_buffer->flags & HFI_BUF_FW_FLAG_RELEASE_DONE)
			return handle_release_output_buffer(inst, hfi_buffer);

		if (hfi_buffer->flags & HFI_BUF_FW_FLAG_PSC_LAST) {
			ret = iris_inst_sub_state_change_drc_last(inst);
			if (ret)
				return ret;
		}

		if (!(hfi_buffer->flags & HFI_BUF_FW_FLAG_READONLY))
			ret = handle_non_read_only_buffer(inst, hfi_buffer);
	}

	buffers = iris_get_buffer_list(inst, BUF_OUTPUT);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(iter, &buffers->list, list) {
		if (!(iter->attr & BUF_ATTR_QUEUED))
			continue;

		if (inst->domain == DECODER)
			found = (iter->index == hfi_buffer->index &&
				 iter->device_addr == hfi_buffer->base_address &&
				 iter->data_offset == hfi_buffer->data_offset);
		else
			found = iter->index == hfi_buffer->index;

		if (found) {
			buf = iter;
			break;
		}
	}
	if (!found)
		return 0;

	buf->data_offset = hfi_buffer->data_offset;
	buf->data_size = hfi_buffer->data_size;
	buf->timestamp = hfi_buffer->timestamp;

	buf->attr &= ~BUF_ATTR_QUEUED;
	buf->attr |= BUF_ATTR_DEQUEUED;

	if (inst->domain == ENCODER) {
		if (inst->hfi_frame_info.data_corrupt)
			fatal = true;
		if (inst->hfi_frame_info.overflow) {
			if (!hfi_buffer->data_size && inst->hfi_rc_type == HFI_RC_CBR_CFR)
				fatal = true;
		}
		if (fatal)
			iris_inst_change_state(inst, IRIS_INST_ERROR);
	}

	if (inst->domain == DECODER) {
		if (inst->buffers.dpb.size && hfi_buffer->flags & HFI_BUF_FW_FLAG_READONLY)
			iris_inst_change_state(inst, IRIS_INST_ERROR);

		if (hfi_buffer->flags & HFI_BUF_FW_FLAG_READONLY) {
			buf->attr |= BUF_ATTR_READ_ONLY;
			ret = handle_read_only_buffer(inst, buf);
		} else {
			buf->attr &= ~BUF_ATTR_READ_ONLY;
		}
	}

	buf->flags = get_driver_buffer_flags(inst, hfi_buffer->flags);

	return ret;
}

static int handle_dequeue_buffers(struct iris_inst *inst)
{
	struct iris_buffers *buffers;
	struct iris_buffer *dummy;
	struct iris_buffer *buf;
	int ret = 0;
	int i;
	static const enum iris_buffer_type buffer_type[] = {
		BUF_INPUT,
		BUF_OUTPUT,
	};

	for (i = 0; i < ARRAY_SIZE(buffer_type); i++) {
		buffers = iris_get_buffer_list(inst, buffer_type[i]);
		if (!buffers)
			return -EINVAL;

		list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
			if (buf->attr & BUF_ATTR_DEQUEUED) {
				buf->attr &= ~BUF_ATTR_DEQUEUED;
				if (!(buf->attr & BUF_ATTR_BUFFER_DONE)) {
					buf->attr |= BUF_ATTR_BUFFER_DONE;

					cache_operation_dqbuf(buf);

					ret = iris_vb2_buffer_done(inst, buf);
					if (ret)
						ret = 0;
				}
			}
		}
	}

	return ret;
}

static int handle_release_internal_buffer(struct iris_inst *inst,
					  struct hfi_buffer *buffer)
{
	struct iris_buffers *buffers;
	struct iris_buffer *buf, *iter;
	int ret = 0;
	bool found;

	buffers = iris_get_buffer_list(inst, hfi_buf_type_to_driver(inst->domain, buffer->type));
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(iter, &buffers->list, list) {
		if (iter->device_addr == buffer->base_address) {
			found = true;
			buf = iter;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	buf->attr &= ~BUF_ATTR_QUEUED;

	if (buf->attr & BUF_ATTR_PENDING_RELEASE)
		ret = iris_destroy_internal_buffer(inst, buf);

	return ret;
}

static int handle_session_stop(struct iris_inst *inst,
			       struct hfi_packet *pkt)
{
	int ret = 0;
	enum signal_session_response signal_type = -1;

	if (inst->domain == DECODER) {
		if (pkt->port == HFI_PORT_RAW) {
			signal_type = SIGNAL_CMD_STOP_OUTPUT;
			ret = iris_inst_sub_state_change_pause(inst, OUTPUT_MPLANE);
		} else if (pkt->port == HFI_PORT_BITSTREAM) {
			signal_type = SIGNAL_CMD_STOP_INPUT;
			ret = iris_inst_sub_state_change_pause(inst, INPUT_MPLANE);
		}
	} else if (inst->domain == ENCODER) {
		if (pkt->port == HFI_PORT_RAW) {
			signal_type = SIGNAL_CMD_STOP_INPUT;
			ret = iris_inst_sub_state_change_pause(inst, INPUT_MPLANE);
		} else if (pkt->port == HFI_PORT_BITSTREAM) {
			signal_type = SIGNAL_CMD_STOP_OUTPUT;
			ret = iris_inst_sub_state_change_pause(inst, OUTPUT_MPLANE);
		}
	}

	if (signal_type != -1)
		signal_session_msg_receipt(inst, signal_type);

	return ret;
}

static int handle_session_buffer(struct iris_inst *inst,
				 struct hfi_packet *pkt)
{
	struct hfi_buffer *buffer;
	u32 hfi_handle_size = 0;
	int i, ret = 0;
	const struct iris_hfi_buffer_handle *hfi_handle_arr = NULL;
	static const struct iris_hfi_buffer_handle dec_input_hfi_handle[] = {
		{HFI_BUFFER_BITSTREAM,      handle_input_buffer               },
		{HFI_BUFFER_BIN,            handle_release_internal_buffer    },
		{HFI_BUFFER_COMV,           handle_release_internal_buffer    },
		{HFI_BUFFER_NON_COMV,       handle_release_internal_buffer    },
		{HFI_BUFFER_LINE,           handle_release_internal_buffer    },
		{HFI_BUFFER_PERSIST,        handle_release_internal_buffer    },
	};
	static const struct iris_hfi_buffer_handle dec_output_hfi_handle[] = {
		{HFI_BUFFER_RAW,            handle_output_buffer              },
		{HFI_BUFFER_DPB,            handle_release_internal_buffer    },
	};
	static const struct iris_hfi_buffer_handle enc_input_hfi_handle[] = {
		{HFI_BUFFER_RAW,            handle_input_buffer               },
		{HFI_BUFFER_VPSS,           handle_release_internal_buffer    },
	};
	static const struct iris_hfi_buffer_handle enc_output_hfi_handle[] = {
		{HFI_BUFFER_BITSTREAM,      handle_output_buffer              },
		{HFI_BUFFER_BIN,            handle_release_internal_buffer    },
		{HFI_BUFFER_COMV,           handle_release_internal_buffer    },
		{HFI_BUFFER_NON_COMV,       handle_release_internal_buffer    },
		{HFI_BUFFER_LINE,           handle_release_internal_buffer    },
		{HFI_BUFFER_ARP,            handle_release_internal_buffer    },
		{HFI_BUFFER_DPB,            handle_release_internal_buffer    },
	};


	if (pkt->payload_info == HFI_PAYLOAD_NONE)
		return 0;

	if (!validate_packet_payload(pkt)) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return 0;
	}

	buffer = (struct hfi_buffer *)((u8 *)pkt + sizeof(*pkt));
	if (!is_valid_hfi_buffer_type(buffer->type))
		return 0;

	if (!is_valid_hfi_port(pkt->port, buffer->type))
		return 0;

	if (inst->domain == DECODER) {
		if (pkt->port == HFI_PORT_BITSTREAM) {
			hfi_handle_size = ARRAY_SIZE(dec_input_hfi_handle);
			hfi_handle_arr = dec_input_hfi_handle;
		} else if (pkt->port == HFI_PORT_RAW) {
			hfi_handle_size = ARRAY_SIZE(dec_output_hfi_handle);
			hfi_handle_arr = dec_output_hfi_handle;
		}
	} else if (inst->domain == ENCODER) {
		if (pkt->port == HFI_PORT_RAW) {
			hfi_handle_size = ARRAY_SIZE(enc_input_hfi_handle);
			hfi_handle_arr = enc_input_hfi_handle;
		} else if (pkt->port == HFI_PORT_BITSTREAM) {
			hfi_handle_size = ARRAY_SIZE(enc_output_hfi_handle);
			hfi_handle_arr = enc_output_hfi_handle;
		}
	}

	if (!hfi_handle_arr || !hfi_handle_size)
		return -EINVAL;

	for (i = 0; i < hfi_handle_size; i++) {
		if (hfi_handle_arr[i].type == buffer->type) {
			ret = hfi_handle_arr[i].handle(inst, buffer);
			if (ret)
				return ret;
			break;
		}
	}

	if (i == hfi_handle_size)
		return -EINVAL;

	return ret;
}

static int handle_session_drain(struct iris_inst *inst,
				struct hfi_packet *pkt)
{
	int ret = 0;

	if (inst->sub_state & IRIS_INST_SUB_DRAIN)
		ret = iris_inst_change_sub_state(inst, 0, IRIS_INST_SUB_INPUT_PAUSE);

	return ret;
}

static int handle_src_change(struct iris_inst *inst,
			     struct hfi_packet *pkt)
{
	int ret;

	if (pkt->port != HFI_PORT_BITSTREAM)
		return 0;

	ret = iris_inst_sub_state_change_drc(inst);
	if (ret)
		return ret;

	ret = vdec_src_change(inst);

	return ret;
}

static int handle_session_command(struct iris_inst *inst,
				  struct hfi_packet *pkt)
{
	int i, ret = 0;
	static const struct iris_hfi_packet_handle hfi_pkt_handle[] = {
		{HFI_CMD_OPEN,              NULL                    },
		{HFI_CMD_CLOSE,             handle_session_close    },
		{HFI_CMD_START,             NULL                    },
		{HFI_CMD_STOP,              handle_session_stop     },
		{HFI_CMD_DRAIN,             handle_session_drain    },
		{HFI_CMD_BUFFER,            handle_session_buffer   },
		{HFI_CMD_SETTINGS_CHANGE,   handle_src_change       },
		{HFI_CMD_SUBSCRIBE_MODE,    NULL                    },
		{HFI_CMD_PAUSE,             NULL                    },
		{HFI_CMD_RESUME,            NULL                    },
	};

	for (i = 0; i < ARRAY_SIZE(hfi_pkt_handle); i++) {
		if (hfi_pkt_handle[i].type == pkt->type) {
			if (hfi_pkt_handle[i].handle) {
				ret = hfi_pkt_handle[i].handle(inst, pkt);
				if (ret)
					return ret;
			}
			break;
		}
	}

	if (i == ARRAY_SIZE(hfi_pkt_handle))
		return -EINVAL;

	return ret;
}

static int handle_dpb_list_property(struct iris_inst *inst,
				    struct hfi_packet *pkt)
{
	u8 *payload_start;
	u32 payload_size;

	if (inst->domain != DECODER)
		return -EINVAL;

	payload_size = pkt->size - sizeof(*pkt);
	payload_start = (u8 *)((u8 *)pkt + sizeof(*pkt));
	memset(inst->dpb_list_payload, 0, MAX_DPB_LIST_ARRAY_SIZE);

	if (payload_size > MAX_DPB_LIST_PAYLOAD_SIZE) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return -EINVAL;
	}

	memcpy(inst->dpb_list_payload, payload_start, payload_size);

	return 0;
}

static int handle_session_property(struct iris_inst *inst,
				   struct hfi_packet *pkt)
{
	u32 *payload_ptr = NULL;
	int ret = 0;

	if (pkt->flags & HFI_FW_FLAGS_INFORMATION)
		return 0;

	payload_ptr = (u32 *)((u8 *)pkt + sizeof(*pkt));
	if (!payload_ptr)
		return -EINVAL;

	switch (pkt->type) {
	case HFI_PROP_BITSTREAM_RESOLUTION:
		inst->src_subcr_params.bitstream_resolution = payload_ptr[0];
		break;
	case HFI_PROP_CROP_OFFSETS:
		inst->src_subcr_params.crop_offsets[0] = payload_ptr[0];
		inst->src_subcr_params.crop_offsets[1] = payload_ptr[1];
		break;
	case HFI_PROP_LUMA_CHROMA_BIT_DEPTH:
		inst->src_subcr_params.bit_depth = payload_ptr[0];
		break;
	case HFI_PROP_CODED_FRAMES:
		inst->src_subcr_params.coded_frames = payload_ptr[0];
		break;
	case HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT:
		inst->src_subcr_params.fw_min_count = payload_ptr[0];
		break;
	case HFI_PROP_PIC_ORDER_CNT_TYPE:
		inst->src_subcr_params.pic_order_cnt = payload_ptr[0];
		break;
	case HFI_PROP_SIGNAL_COLOR_INFO:
		inst->src_subcr_params.color_info = payload_ptr[0];
		break;
	case HFI_PROP_PROFILE:
		inst->src_subcr_params.profile = payload_ptr[0];
		break;
	case HFI_PROP_LEVEL:
		inst->src_subcr_params.level = payload_ptr[0];
		break;
	case HFI_PROP_TIER:
		inst->src_subcr_params.tier = payload_ptr[0];
		break;
	case HFI_PROP_PICTURE_TYPE:
		inst->hfi_frame_info.picture_type = payload_ptr[0];
		if (inst->hfi_frame_info.picture_type & HFI_PICTURE_B)
			inst->has_bframe = true;
		if (inst->hfi_frame_info.picture_type & HFI_PICTURE_IDR)
			inst->iframe = true;
		else
			inst->iframe = false;
		break;
	case HFI_PROP_CABAC_SESSION:
		if (payload_ptr[0] == 1)
			inst->cap[ENTROPY_MODE].value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC;
		else
			inst->cap[ENTROPY_MODE].value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC;
		break;
	case HFI_PROP_DPB_LIST:
		ret = handle_dpb_list_property(inst, pkt);
		if (ret)
			break;
		break;
	case HFI_PROP_NO_OUTPUT:
		inst->hfi_frame_info.no_output = 1;
		break;
	case HFI_PROP_QUALITY_MODE:
	case HFI_PROP_STAGE:
	case HFI_PROP_PIPE:
		break;
	default:
		break;
	}

	return ret;
}

static int handle_image_version_property(struct iris_core *core,
					 struct hfi_packet *pkt)
{
	u8 *str_image_version;
	u32 req_bytes;
	u32 i = 0;

	req_bytes = pkt->size - sizeof(*pkt);
	if (req_bytes < IRIS_VERSION_LENGTH - 1)
		return -EINVAL;

	str_image_version = (u8 *)pkt + sizeof(*pkt);

	for (i = 0; i < IRIS_VERSION_LENGTH - 1; i++) {
		if (str_image_version[i] != '\0')
			core->fw_version[i] = str_image_version[i];
		else
			core->fw_version[i] = ' ';
	}
	core->fw_version[i] = '\0';

	return 0;
}

static int handle_system_property(struct iris_core *core,
				  struct hfi_packet *pkt)
{
	int ret = 0;

	switch (pkt->type) {
	case HFI_PROP_IMAGE_VERSION:
		ret = handle_image_version_property(core, pkt);
		break;
	default:
		break;
	}

	return ret;
}

static int handle_system_response(struct iris_core *core,
				  struct hfi_header *hdr)
{
	struct hfi_packet *packet;
	u8 *pkt, *start_pkt;
	int ret = 0;
	int i, j;
	static const struct iris_core_hfi_range be[] = {
		{HFI_SYSTEM_ERROR_BEGIN,   HFI_SYSTEM_ERROR_END,   handle_system_error     },
		{HFI_PROP_BEGIN,           HFI_PROP_END,           handle_system_property  },
		{HFI_CMD_BEGIN,            HFI_CMD_END,            handle_system_init      },
	};

	start_pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
	for (i = 0; i < ARRAY_SIZE(be); i++) {
		pkt = start_pkt;
		for (j = 0; j < hdr->num_packets; j++) {
			packet = (struct hfi_packet *)pkt;
			if (packet->flags & HFI_FW_FLAGS_SYSTEM_ERROR) {
				ret = handle_system_error(core, packet);
				return ret;
			}

			if (packet->type > be[i].begin && packet->type < be[i].end) {
				ret = be[i].handle(core, packet);
				if (ret)
					return ret;

				if (packet->type >  HFI_SYSTEM_ERROR_BEGIN &&
				    packet->type < HFI_SYSTEM_ERROR_END)
					return 0;
			}
			pkt += packet->size;
		}
	}

	return ret;
}

static int handle_session_response(struct iris_core *core,
				   struct hfi_header *hdr)
{
	struct hfi_packet *packet;
	struct iris_inst *inst;
	bool dequeue = false;
	u8 *pkt, *start_pkt;
	int ret = 0;
	int i, j;
	static const struct iris_inst_hfi_range be[] = {
		{HFI_SESSION_ERROR_BEGIN,  HFI_SESSION_ERROR_END,  handle_session_error    },
		{HFI_INFORMATION_BEGIN,    HFI_INFORMATION_END,    handle_session_info     },
		{HFI_PROP_BEGIN,           HFI_PROP_END,           handle_session_property },
		{HFI_CMD_BEGIN,            HFI_CMD_END,            handle_session_command  },
	};

	inst = to_instance(core, hdr->session_id);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	memset(&inst->hfi_frame_info, 0, sizeof(struct iris_hfi_frame_info));

	pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
	for (i = 0; i < hdr->num_packets; i++) {
		packet = (struct hfi_packet *)pkt;
		if (packet->type == HFI_CMD_SETTINGS_CHANGE) {
			if (packet->port == HFI_PORT_BITSTREAM) {
				vdec_init_src_change_param(inst);
				break;
			}
		}
		pkt += packet->size;
	}

	start_pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
	for (i = 0; i < ARRAY_SIZE(be); i++) {
		pkt = start_pkt;
		for (j = 0; j < hdr->num_packets; j++) {
			packet = (struct hfi_packet *)pkt;
			if (packet->flags & HFI_FW_FLAGS_SESSION_ERROR)
				handle_session_error(inst, packet);

			if (packet->type > be[i].begin && packet->type < be[i].end) {
				dequeue |= (packet->type == HFI_CMD_BUFFER);
				ret = be[i].handle(inst, packet);
				if (ret)
					iris_inst_change_state(inst, IRIS_INST_ERROR);
			}
			pkt += packet->size;
		}
	}

	if (dequeue) {
		ret = handle_dequeue_buffers(inst);
		if (ret)
			goto unlock;
	}

	memset(&inst->hfi_frame_info, 0, sizeof(struct iris_hfi_frame_info));

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int handle_response(struct iris_core *core, void *response)
{
	struct hfi_header *hdr;
	int ret;

	iris_pm_touch(core);

	hdr = (struct hfi_header *)response;
	ret = validate_hdr_packet(core, hdr);
	if (ret)
		return handle_system_error(core, NULL);

	if (!hdr->session_id)
		return handle_system_response(core, hdr);
	else
		return handle_session_response(core, hdr);

	return ret;
}

int __response_handler(struct iris_core *core)
{
	int ret = 0;

	if (call_vpu_op(core, watchdog, core, core->intr_status)) {
		struct hfi_packet pkt = {.type = HFI_SYS_ERROR_WD_TIMEOUT};

		mutex_lock(&core->lock);
		iris_change_core_state(core, IRIS_CORE_ERROR);
		dev_err(core->dev, "%s: CPU WD error received\n", __func__);
		mutex_unlock(&core->lock);

		return handle_system_error(core, &pkt);
	}

	memset(core->response_packet, 0, core->packet_size);
	while (!iris_hfi_queue_msg_read(core, core->response_packet)) {
		ret = handle_response(core, core->response_packet);
		if (ret)
			continue;
		if (core->state != IRIS_CORE_INIT)
			break;
		memset(core->response_packet, 0, core->packet_size);
	}

	iris_flush_debug_queue(core, core->response_packet, core->packet_size);

	return ret;
}
