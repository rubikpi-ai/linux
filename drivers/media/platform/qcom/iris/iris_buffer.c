// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_buffer.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "hfi_defines.h"
#include "iris_hfi_packet.h"
#include "iris_instance.h"
#include "memory.h"
#include "vpu_iris3_buffer.h"

static const u32 dec_ip_int_buf_type[] = {
	BUF_BIN,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
};

static const u32 dec_op_int_buf_type[] = {
	BUF_DPB,
};

static const u32 enc_ip_int_buf_type[] = {
	BUF_VPSS,
};

static const u32 enc_op_int_buf_type[] = {
	BUF_BIN,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
	BUF_DPB,
};

static u32 video_buffer_size(u32 colorformat,
			     u32 pix_width,
			     u32 pix_height)
{
	u32 size = 0;
	u32 y_plane, uv_plane, y_stride,
		uv_stride, y_sclines, uv_sclines;
	u32 y_ubwc_plane = 0, uv_ubwc_plane = 0;
	u32 y_meta_stride = 0, y_meta_scanlines = 0;
	u32 uv_meta_stride = 0, uv_meta_scanlines = 0;
	u32 y_meta_plane = 0, uv_meta_plane = 0;

	if (!pix_width || !pix_height)
		goto invalid_input;

	switch (colorformat) {
	case V4L2_PIX_FMT_NV12:
		y_stride = ALIGN(pix_width, 128);
		uv_stride = ALIGN(pix_width, 128);
		y_sclines = ALIGN(pix_height, 32);
		uv_sclines = ALIGN((pix_height + 1) >> 1, 16);
		y_plane = y_stride * y_sclines;
		uv_plane = uv_stride * uv_sclines;
		size = y_plane + uv_plane;
		break;
	case V4L2_PIX_FMT_QC08C:
		y_stride = ALIGN(pix_width, 128);
		uv_stride = ALIGN(pix_width, 128);
		y_sclines = ALIGN(pix_height, 32);
		uv_sclines = ALIGN((pix_height + 1) >> 1, 32);
		y_meta_stride = ALIGN(DIV_ROUND_UP(pix_width, 32), 64);
		uv_meta_stride = ALIGN(DIV_ROUND_UP((pix_width + 1) >> 1, 16), 64);
		y_ubwc_plane =
			ALIGN(y_stride * y_sclines, 4096);
		uv_ubwc_plane =
			ALIGN(uv_stride * uv_sclines, 4096);
		y_meta_scanlines =
			ALIGN(DIV_ROUND_UP(pix_height, 8), 16);
		y_meta_plane =
			ALIGN(y_meta_stride * y_meta_scanlines, 4096);
		uv_meta_scanlines =
			ALIGN(DIV_ROUND_UP((pix_height + 1) >> 1, 8), 16);
		uv_meta_plane =
			ALIGN(uv_meta_stride * uv_meta_scanlines, 4096);
		size = (y_ubwc_plane + uv_ubwc_plane + y_meta_plane +
			uv_meta_plane);
		break;
	case V4L2_PIX_FMT_QC10C:
		y_stride =
			ALIGN(ALIGN(pix_width, 192) * 4 / 3, 256);
		uv_stride =
			ALIGN(ALIGN(pix_width, 192) * 4 / 3, 256);
		y_sclines =
			ALIGN(pix_height, 16);
		uv_sclines =
			ALIGN((pix_height + 1) >> 1, 16);
		y_ubwc_plane =
			ALIGN(y_stride * y_sclines, 4096);
		uv_ubwc_plane =
			ALIGN(uv_stride * uv_sclines, 4096);
		y_meta_stride =
			ALIGN(DIV_ROUND_UP(pix_width, 48), 64);
		y_meta_scanlines =
			ALIGN(DIV_ROUND_UP(pix_height, 4), 16);
		y_meta_plane =
			ALIGN(y_meta_stride * y_meta_scanlines, 4096);
		uv_meta_stride =
			ALIGN(DIV_ROUND_UP((pix_width + 1) >> 1, 24), 64);
		uv_meta_scanlines =
			ALIGN(DIV_ROUND_UP((pix_height + 1) >> 1, 4), 16);
		uv_meta_plane =
			ALIGN(uv_meta_stride * uv_meta_scanlines, 4096);

		size = y_ubwc_plane + uv_ubwc_plane + y_meta_plane +
			uv_meta_plane;
		break;
	default:
		break;
	}

invalid_input:

	return ALIGN(size, 4096);
}

static int input_min_count(struct iris_inst *inst)
{
	u32 input_min_count = 0;
	u32 total_hb_layer = 0;

	if (inst->domain == DECODER) {
		input_min_count = MIN_BUFFERS;
	} else if (inst->domain == ENCODER) {
		total_hb_layer = is_hierb_type_requested(inst) ?
			inst->cap[ENH_LAYER_COUNT].value + 1 : 0;
		if (inst->codec == H264 &&
		    !inst->cap[LAYER_ENABLE].value) {
			total_hb_layer = 0;
		}
		input_min_count =
			hfi_iris3_enc_min_input_buf_count(total_hb_layer);
	} else {
		return 0;
	}

	return input_min_count;
}

static int output_min_count(struct iris_inst *inst)
{
	int output_min_count;

	if (inst->domain != DECODER && inst->domain != ENCODER)
		return 0;

	if (inst->domain == ENCODER)
		return MIN_BUFFERS;

	/* fw_min_count > 0 indicates reconfig event has already arrived */
	if (inst->fw_min_count) {
		if (is_split_mode_enabled(inst) && inst->codec == VP9)
			return min_t(u32, 4, inst->fw_min_count);
		else
			return inst->fw_min_count;
	}

	switch (inst->codec) {
	case H264:
	case HEVC:
		output_min_count = 4;
		break;
	case VP9:
		output_min_count = 9;
		break;
	default:
		output_min_count = 4;
		break;
	}

	return output_min_count;
}

int update_buffer_count(struct iris_inst *inst, u32 plane)
{
	switch (plane) {
	case INPUT_MPLANE:
		inst->buffers.input.min_count = input_min_count(inst);
		if (inst->buffers.input.actual_count < inst->buffers.input.min_count)
			inst->buffers.input.actual_count = inst->buffers.input.min_count;

		break;
	case OUTPUT_MPLANE:
		if (!inst->vb2q_src->streaming)
			inst->buffers.output.min_count = output_min_count(inst);
		if (inst->buffers.output.actual_count < inst->buffers.output.min_count)
			inst->buffers.output.actual_count = inst->buffers.output.min_count;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static u32 internal_buffer_count(struct iris_inst *inst,
				 enum iris_buffer_type buffer_type)
{
	u32 count = 0;

	if (inst->domain == ENCODER)
		return 1;

	if (inst->domain == DECODER) {
		if (buffer_type == BUF_BIN || buffer_type == BUF_LINE ||
		    buffer_type == BUF_PERSIST) {
			count = 1;
		} else if (buffer_type == BUF_COMV || buffer_type == BUF_NON_COMV) {
			if (inst->codec == H264 || inst->codec == HEVC)
				count = 1;
			else
				count = 0;
		} else {
			count = 0;
		}
	}

	return count;
}

static int dpb_count(struct iris_inst *inst)
{
	int count = 0;

	if (inst->domain == ENCODER)
		return get_recon_buf_count(inst);

	if (is_split_mode_enabled(inst)) {
		count = inst->fw_min_count ?
			inst->fw_min_count : inst->buffers.output.min_count;
	}

	return count;
}

int iris_get_buf_min_count(struct iris_inst *inst,
			   enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return input_min_count(inst);
	case BUF_OUTPUT:
		return output_min_count(inst);
	case BUF_BIN:
	case BUF_COMV:
	case BUF_NON_COMV:
	case BUF_LINE:
	case BUF_PERSIST:
	case BUF_ARP:
		return internal_buffer_count(inst, buffer_type);
	case BUF_DPB:
		return dpb_count(inst);
	default:
		return 0;
	}
}

static u32 dec_input_buffer_size(struct iris_inst *inst)
{
	u32 base_res_mbs = NUM_MBS_4k;
	u32 frame_size, num_mbs;
	struct v4l2_format *f;
	u32 div_factor = 1;
	u32 codec;

	f = inst->fmt_src;
	codec = f->fmt.pix_mp.pixelformat;

	num_mbs = get_mbpf(inst);
	if (num_mbs > NUM_MBS_4k) {
		div_factor = 4;
		base_res_mbs = inst->cap[MBPF].value;
	} else {
		base_res_mbs = NUM_MBS_4k;
		if (codec == V4L2_PIX_FMT_VP9)
			div_factor = 1;
		else
			div_factor = 2;
	}

	frame_size = base_res_mbs * MB_IN_PIXEL * 3 / 2 / div_factor;

	 /* multiply by 10/8 (1.25) to get size for 10 bit case */
	if (codec == V4L2_PIX_FMT_VP9 || codec == V4L2_PIX_FMT_HEVC)
		frame_size = frame_size + (frame_size >> 2);

	return ALIGN(frame_size, SZ_4K);
}

static u32 dec_output_buffer_size(struct iris_inst *inst)
{
	struct v4l2_format *f;
	u32 size;

	f = inst->fmt_dst;

	size = video_buffer_size(f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width,
				 f->fmt.pix_mp.height);
	return size;
}

static u32 enc_input_buffer_size(struct iris_inst *inst)
{
	struct v4l2_format *f;
	u32 size;

	f = inst->fmt_src;

	size = video_buffer_size(f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width,
				 f->fmt.pix_mp.height);
	return size;
}

int iris_get_buffer_size(struct iris_inst *inst,
			 enum iris_buffer_type buffer_type)
{
	if (inst->domain == DECODER) {
		switch (buffer_type) {
		case BUF_INPUT:
			return dec_input_buffer_size(inst);
		case BUF_OUTPUT:
			return dec_output_buffer_size(inst);
		default:
			break;
		}
	} else if (inst->domain == ENCODER) {
		switch (buffer_type) {
		case BUF_INPUT:
			return enc_input_buffer_size(inst);
		case BUF_OUTPUT:
			return enc_output_buffer_size_iris3(inst);
		default:
			break;
		}
	}

	return 0;
}

struct iris_buffers *iris_get_buffer_list(struct iris_inst *inst,
					  enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return &inst->buffers.input;
	case BUF_OUTPUT:
		return &inst->buffers.output;
	case BUF_READ_ONLY:
		return &inst->buffers.read_only;
	case BUF_BIN:
		return &inst->buffers.bin;
	case BUF_ARP:
		return &inst->buffers.arp;
	case BUF_COMV:
		return &inst->buffers.comv;
	case BUF_NON_COMV:
		return &inst->buffers.non_comv;
	case BUF_LINE:
		return &inst->buffers.line;
	case BUF_DPB:
		return &inst->buffers.dpb;
	case BUF_PERSIST:
		return &inst->buffers.persist;
	case BUF_VPSS:
		return &inst->buffers.vpss;
	default:
		return NULL;
	}
}

int iris_allocate_buffers(struct iris_inst *inst,
			  enum iris_buffer_type buf_type,
			  u32 num_buffers)
{
	struct iris_buffer *buf = NULL;
	struct iris_buffers *buffers;
	int idx = 0;

	buffers = iris_get_buffer_list(inst, buf_type);
	if (!buffers)
		return -EINVAL;

	for (idx = 0; idx < num_buffers; idx++) {
		buf = iris_get_buffer_from_pool(inst);
		if (!buf)
			return -EINVAL;

		INIT_LIST_HEAD(&buf->list);
		list_add_tail(&buf->list, &buffers->list);
		buf->type = buf_type;
		buf->index = idx;
	}

	return 0;
}

int iris_free_buffers(struct iris_inst *inst,
		      enum iris_buffer_type buf_type)
{
	struct iris_buffer *buf, *dummy;
	struct iris_buffers *buffers;

	buffers = iris_get_buffer_list(inst, buf_type);
	if (!buffers)
		return -EINVAL;

	list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
		list_del_init(&buf->list);
		iris_return_buffer_to_pool(inst, buf);
	}

	return 0;
}

static int iris_get_internal_buf_info(struct iris_inst *inst,
				      enum iris_buffer_type buffer_type)
{
	struct iris_buffers *buffers;
	struct iris_core *core;
	u32 buf_count;
	u32 buf_size;

	core = inst->core;

	buf_size = call_session_op(core, int_buf_size,
				   inst, buffer_type);

	buf_count = iris_get_buf_min_count(inst, buffer_type);

	buffers = iris_get_buffer_list(inst, buffer_type);
	if (!buffers)
		return -EINVAL;

	if (buf_size && buf_size <= buffers->size &&
	    buf_count && buf_count <= buffers->min_count) {
		buffers->reuse = true;
	} else {
		buffers->reuse = false;
		buffers->size = buf_size;
		buffers->min_count = buf_count;
	}

	return 0;
}

int iris_get_internal_buffers(struct iris_inst *inst,
			      u32 plane)
{
	int ret = 0;
	u32 i = 0;

	if (inst->domain == DECODER) {
		if (plane == INPUT_MPLANE) {
			for (i = 0; i < ARRAY_SIZE(dec_ip_int_buf_type); i++) {
				ret = iris_get_internal_buf_info(inst, dec_ip_int_buf_type[i]);
				if (ret)
					return ret;
			}
		} else {
			return iris_get_internal_buf_info(inst, BUF_DPB);
		}
	} else if (inst->domain == ENCODER) {
		if (plane == INPUT_MPLANE) {
			for (i = 0; i < ARRAY_SIZE(enc_ip_int_buf_type); i++) {
				ret = iris_get_internal_buf_info(inst, enc_ip_int_buf_type[i]);
				if (ret)
					return ret;
			}
		} else {
			for (i = 0; i < ARRAY_SIZE(enc_op_int_buf_type); i++) {
				ret = iris_get_internal_buf_info(inst, enc_op_int_buf_type[i]);
				if (ret)
					return ret;
			}
		}
	}

	return ret;
}

static int iris_create_internal_buffer(struct iris_inst *inst,
				       enum iris_buffer_type buffer_type, u32 index)
{
	struct iris_buffers *buffers;
	struct iris_buffer *buffer;
	struct iris_core *core;

	core = inst->core;

	buffers = iris_get_buffer_list(inst, buffer_type);
	if (!buffers)
		return -EINVAL;

	if (!buffers->size)
		return 0;

	buffer = iris_get_buffer_from_pool(inst);
	if (!buffer)
		return -ENOMEM;

	INIT_LIST_HEAD(&buffer->list);
	buffer->type = buffer_type;
	buffer->index = index;
	buffer->buffer_size = buffers->size;
	buffer->dma_attrs = DMA_ATTR_WRITE_COMBINE | DMA_ATTR_NO_KERNEL_MAPPING;
	list_add_tail(&buffer->list, &buffers->list);

	buffer->kvaddr = dma_alloc_attrs(core->dev, buffer->buffer_size,
					 &buffer->device_addr, GFP_KERNEL, buffer->dma_attrs);

	if (!buffer->kvaddr)
		return -ENOMEM;

	return 0;
}

static int iris_create_internal_buffers(struct iris_inst *inst,
					enum iris_buffer_type buffer_type)
{
	struct iris_buffers *buffers;
	int ret = 0;
	int i;

	buffers = iris_get_buffer_list(inst, buffer_type);
	if (!buffers)
		return -EINVAL;

	if (buffers->reuse)
		return 0;

	for (i = 0; i < buffers->min_count; i++) {
		ret = iris_create_internal_buffer(inst, buffer_type, i);
		if (ret)
			return ret;
	}

	return ret;
}

int iris_create_input_internal_buffers(struct iris_inst *inst)
{
	int ret = 0;
	u32 i = 0;

	if (inst->domain == DECODER) {
		for (i = 0; i < ARRAY_SIZE(dec_ip_int_buf_type); i++) {
			ret = iris_create_internal_buffers(inst, dec_ip_int_buf_type[i]);
			if (ret)
				return ret;
		}
	} else if (inst->domain == ENCODER) {
		for (i = 0; i < ARRAY_SIZE(enc_ip_int_buf_type); i++) {
			ret = iris_create_internal_buffers(inst, enc_ip_int_buf_type[i]);
			if (ret)
				return ret;
		}
	}

	return ret;
}

int iris_create_output_internal_buffers(struct iris_inst *inst)
{
	int ret = 0;
	u32 i = 0;

	if (inst->domain == DECODER) {
		return iris_create_internal_buffers(inst, BUF_DPB);
	} else if (inst->domain == ENCODER) {
		for (i = 0; i < ARRAY_SIZE(enc_op_int_buf_type); i++) {
			ret = iris_create_internal_buffers(inst, enc_op_int_buf_type[i]);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int set_num_comv(struct iris_inst *inst)
{
	u32 num_comv;

	num_comv = inst->cap[NUM_COMV].value;

	return iris_hfi_set_property(inst,
					HFI_PROP_COMV_BUFFER_COUNT,
					HFI_HOST_FLAGS_NONE,
					HFI_PORT_BITSTREAM,
					HFI_PAYLOAD_U32,
					&num_comv,
					sizeof(u32));
}

static int iris_queue_internal_buffers(struct iris_inst *inst,
				       enum iris_buffer_type buffer_type)
{
	struct iris_buffer *buffer, *dummy;
	struct iris_buffers *buffers;
	int ret = 0;

	if (inst->domain == DECODER && buffer_type == BUF_COMV) {
		ret = set_num_comv(inst);
		if (ret)
			return ret;
	}

	buffers = iris_get_buffer_list(inst, buffer_type);
	if (!buffers)
		return -EINVAL;

	list_for_each_entry_safe(buffer, dummy, &buffers->list, list) {
		if (buffer->attr & BUF_ATTR_PENDING_RELEASE)
			continue;
		if (buffer->attr & BUF_ATTR_QUEUED)
			continue;
		ret = iris_hfi_queue_buffer(inst, buffer);
		if (ret)
			return ret;
		buffer->attr |= BUF_ATTR_QUEUED;
	}

	return ret;
}

int iris_queue_input_internal_buffers(struct iris_inst *inst)
{
	int ret = 0;
	u32 i;

	if (inst->domain == DECODER) {
		for (i = 0; i < ARRAY_SIZE(dec_ip_int_buf_type); i++) {
			ret = iris_queue_internal_buffers(inst, dec_ip_int_buf_type[i]);
			if (ret)
				return ret;
		}
	} else if (inst->domain == ENCODER) {
		for (i = 0; i < ARRAY_SIZE(enc_ip_int_buf_type); i++) {
			ret = iris_queue_internal_buffers(inst, enc_ip_int_buf_type[i]);
			if (ret)
				return ret;
		}
	}

	return ret;
}

int iris_queue_output_internal_buffers(struct iris_inst *inst)
{
	int ret = 0;
	u32 i = 0;

	if (inst->domain == DECODER) {
		return iris_queue_internal_buffers(inst, BUF_DPB);
	} else if (inst->domain == ENCODER) {
		for (i = 0; i < ARRAY_SIZE(enc_op_int_buf_type); i++) {
			ret = iris_queue_internal_buffers(inst, enc_op_int_buf_type[i]);
			if (ret)
				return ret;
		}
	}

	return ret;
}

int iris_destroy_internal_buffer(struct iris_inst *inst,
				 struct iris_buffer *buffer)
{
	struct iris_buffer *buf, *dummy;
	struct iris_buffers *buffers;
	struct iris_core *core;

	core = inst->core;

	buffers = iris_get_buffer_list(inst, buffer->type);
	if (!buffers)
		return -EINVAL;

	list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
		if (buf->device_addr == buffer->device_addr) {
			list_del(&buf->list);
			dma_free_attrs(core->dev, buf->buffer_size, buf->kvaddr,
				       buf->device_addr, buf->dma_attrs);
			buf->kvaddr = NULL;
			buf->device_addr = 0;
			iris_return_buffer_to_pool(inst, buf);
			break;
		}
	}

	return 0;
}

int iris_destroy_internal_buffers(struct iris_inst *inst,
				  u32 plane)
{
	struct iris_buffer *buf, *dummy;
	struct iris_buffers *buffers;
	const u32 *internal_buf_type = NULL;
	int ret = 0;
	u32 i, len = 0;

	if (inst->domain == DECODER) {
		if (plane == INPUT_MPLANE) {
			internal_buf_type = dec_ip_int_buf_type;
			len = ARRAY_SIZE(dec_ip_int_buf_type);
		} else {
			internal_buf_type = dec_op_int_buf_type;
			len = ARRAY_SIZE(dec_op_int_buf_type);
		}
	} else if (inst->domain == ENCODER) {
		if (plane == INPUT_MPLANE) {
			internal_buf_type = enc_ip_int_buf_type;
			len = ARRAY_SIZE(enc_ip_int_buf_type);
		} else {
			internal_buf_type = enc_op_int_buf_type;
			len = ARRAY_SIZE(enc_op_int_buf_type);
		}
	}

	for (i = 0; i < len; i++) {
		buffers = iris_get_buffer_list(inst, internal_buf_type[i]);
		if (!buffers)
			return -EINVAL;

		if (buffers->reuse)
			continue;

		list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
			ret = iris_destroy_internal_buffer(inst, buf);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int iris_release_internal_buffers(struct iris_inst *inst,
					 enum iris_buffer_type buffer_type)
{
	struct iris_buffer *buffer, *dummy;
	struct iris_buffers *buffers;
	int ret = 0;

	buffers = iris_get_buffer_list(inst, buffer_type);
	if (!buffers)
		return -EINVAL;

	if (buffers->reuse)
		return 0;

	list_for_each_entry_safe(buffer, dummy, &buffers->list, list) {
		if (buffer->attr & BUF_ATTR_PENDING_RELEASE)
			continue;
		if (!(buffer->attr & BUF_ATTR_QUEUED))
			continue;
		ret = iris_hfi_release_buffer(inst, buffer);
		if (ret)
			return ret;
		buffer->attr |= BUF_ATTR_PENDING_RELEASE;
	}

	return ret;
}

static int iris_release_input_internal_buffers(struct iris_inst *inst)
{
	int ret = 0;
	u32 i = 0;

	if (inst->domain == DECODER) {
		for (i = 0; i < ARRAY_SIZE(dec_ip_int_buf_type); i++) {
			ret = iris_release_internal_buffers(inst, dec_ip_int_buf_type[i]);
			if (ret)
				return ret;
		}
	}

	return ret;
}

int iris_alloc_and_queue_session_int_bufs(struct iris_inst *inst,
					  enum iris_buffer_type buffer_type)
{
	int ret;

	if (buffer_type != BUF_ARP && buffer_type != BUF_PERSIST)
		return -EINVAL;

	ret = iris_get_internal_buf_info(inst, buffer_type);
	if (ret)
		return ret;

	ret = iris_create_internal_buffers(inst, buffer_type);
	if (ret)
		return ret;

	ret = iris_queue_internal_buffers(inst, buffer_type);

	return ret;
}

int iris_alloc_and_queue_input_int_bufs(struct iris_inst *inst)
{
	int ret;

	ret = iris_get_internal_buffers(inst, INPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_release_input_internal_buffers(inst);
	if (ret)
		return ret;

	ret = iris_create_input_internal_buffers(inst);
	if (ret)
		return ret;

	ret = iris_queue_input_internal_buffers(inst);

	return ret;
}

int iris_alloc_and_queue_additional_dpb_buffers(struct iris_inst *inst)
{
	struct iris_buffers *buffers;
	struct iris_buffer *buffer;
	int cur_min_count = 0;
	int ret;
	int i;

	ret = iris_get_internal_buf_info(inst, BUF_DPB);
	if (ret)
		return ret;

	buffers = iris_get_buffer_list(inst, BUF_DPB);
	if (!buffers)
		return -EINVAL;

	list_for_each_entry(buffer, &buffers->list, list)
		cur_min_count++;

	if (cur_min_count >= buffers->min_count)
		return 0;

	for (i = cur_min_count; i < buffers->min_count; i++) {
		ret = iris_create_internal_buffers(inst, BUF_DPB);
		if (ret)
			return ret;
	}

	ret = iris_queue_internal_buffers(inst, BUF_DPB);

	return ret;
}
