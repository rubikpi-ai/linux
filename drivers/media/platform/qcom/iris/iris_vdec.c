// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-event.h>

#include "hfi_defines.h"
#include "iris_buffer.h"
#include "iris_common.h"
#include "iris_ctrls.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_hfi_packet.h"
#include "iris_power.h"
#include "iris_vdec.h"

#define UNSPECIFIED_COLOR_FORMAT 5

struct vdec_prop_type_handle {
	u32 type;
	int (*handle)(struct iris_inst *inst);
};

int vdec_inst_init(struct iris_inst *inst)
{
	struct v4l2_format *f;
	int ret;

	inst->fmt_src  = kzalloc(sizeof(*inst->fmt_src), GFP_KERNEL);
	inst->fmt_dst  = kzalloc(sizeof(*inst->fmt_dst), GFP_KERNEL);
	inst->vb2q_src = kzalloc(sizeof(*inst->vb2q_src), GFP_KERNEL);
	inst->vb2q_dst = kzalloc(sizeof(*inst->vb2q_dst), GFP_KERNEL);

	f = inst->fmt_src;
	f->type = INPUT_MPLANE;
	f->fmt.pix_mp.width = DEFAULT_WIDTH;
	f->fmt.pix_mp.height = DEFAULT_HEIGHT;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_INPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;

	f = inst->fmt_dst;
	f->type = OUTPUT_MPLANE;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_QC08C;
	f->fmt.pix_mp.width = ALIGN(DEFAULT_WIDTH, 128);
	f->fmt.pix_mp.height = ALIGN(DEFAULT_HEIGHT, 32);
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(DEFAULT_WIDTH, 128);
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->buffers.output.min_count = iris_get_buf_min_count(inst, BUF_OUTPUT);
	inst->buffers.output.actual_count = inst->buffers.output.min_count;
	inst->buffers.output.size = f->fmt.pix_mp.plane_fmt[0].sizeimage;
	inst->fw_min_count = 0;

	ret = codec_change(inst, inst->fmt_src->fmt.pix_mp.pixelformat);

	return ret;
}

void vdec_inst_deinit(struct iris_inst *inst)
{
	kfree(inst->fmt_dst);
	kfree(inst->fmt_src);
}

static int vdec_check_colorformat_supported(struct iris_inst *inst,
					    enum colorformat_type colorformat)
{
	bool supported = true;

	if (!inst->vb2q_src->streaming)
		return true;

	if (inst->cap[BIT_DEPTH].value == BIT_DEPTH_8 &&
	    !is_8bit_colorformat(colorformat))
		supported = false;
	if (inst->cap[BIT_DEPTH].value == BIT_DEPTH_10 &&
	    !is_10bit_colorformat(colorformat))
		supported = false;
	if (inst->cap[CODED_FRAMES].value == CODED_FRAMES_INTERLACE)
		supported = false;

	return supported;
}

int vdec_enum_fmt(struct iris_inst *inst, struct v4l2_fmtdesc *f)
{
	struct iris_core *core;
	u32 array[32] = {0};
	u32 i = 0;

	if (f->index >= ARRAY_SIZE(array))
		return -EINVAL;

	core = inst->core;
	if (f->type == INPUT_MPLANE) {
		u32 codecs = core->cap[DEC_CODECS].value;
		u32 codecs_count = hweight32(codecs);
		u32 idx = 0;

		for (i = 0; i <= codecs_count; i++) {
			if (codecs & BIT(i)) {
				if (idx >= ARRAY_SIZE(array))
					break;
				array[idx] = codecs & BIT(i);
				idx++;
			}
		}
		if (!array[f->index])
			return -EINVAL;
		f->pixelformat = v4l2_codec_from_driver(inst, array[f->index]);
		f->flags = V4L2_FMT_FLAG_COMPRESSED | V4L2_FMT_FLAG_DYN_RESOLUTION;
		strscpy(f->description, "codec", sizeof(f->description));
	} else if (f->type == OUTPUT_MPLANE) {
		u32 formats = inst->cap[PIX_FMTS].step_or_mask;
		u32 idx = 0;

		for (i = 0; i <= 31; i++) {
			if (formats & BIT(i)) {
				if (idx >= ARRAY_SIZE(array))
					break;
				if (vdec_check_colorformat_supported(inst, formats & BIT(i))) {
					array[idx] = formats & BIT(i);
					idx++;
				}
			}
		}
		if (!array[f->index])
			return -EINVAL;
		f->pixelformat = v4l2_colorformat_from_driver(inst, array[f->index]);
		strscpy(f->description, "colorformat", sizeof(f->description));
	}

	if (!f->pixelformat)
		return -EINVAL;

	memset(f->reserved, 0, sizeof(f->reserved));

	return 0;
}

int vdec_try_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct v4l2_format *f_inst;
	u32 pix_fmt;

	memset(pixmp->reserved, 0, sizeof(pixmp->reserved));
	if (f->type == INPUT_MPLANE) {
		pix_fmt = v4l2_codec_to_driver(inst, f->fmt.pix_mp.pixelformat);
		if (!pix_fmt) {
			f_inst = inst->fmt_src;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
			f->fmt.pix_mp.pixelformat = f_inst->fmt.pix_mp.pixelformat;
			pix_fmt = v4l2_codec_to_driver(inst, f->fmt.pix_mp.pixelformat);
		}
	} else if (f->type == OUTPUT_MPLANE) {
		pix_fmt = v4l2_colorformat_to_driver(inst, f->fmt.pix_mp.pixelformat);
		if (!pix_fmt) {
			f_inst = inst->fmt_dst;
			f->fmt.pix_mp.pixelformat = f_inst->fmt.pix_mp.pixelformat;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
		}
		if (inst->vb2q_src->streaming) {
			f_inst = inst->fmt_src;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
		}
	} else {
		return -EINVAL;
	}

	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;

	pixmp->num_planes = 1;

	return 0;
}

int vdec_s_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_format *fmt, *output_fmt;
	enum colorformat_type colorformat;
	u32 codec_align, stride = 0;
	int ret = 0;

	vdec_try_fmt(inst, f);

	if (f->type == INPUT_MPLANE) {
		if (inst->fmt_src->fmt.pix_mp.pixelformat !=
			f->fmt.pix_mp.pixelformat) {
			ret = codec_change(inst, f->fmt.pix_mp.pixelformat);
			if (ret)
				return ret;
		}

		fmt = inst->fmt_src;
		fmt->type = INPUT_MPLANE;

		codec_align = inst->fmt_src->fmt.pix_mp.pixelformat ==
			V4L2_PIX_FMT_HEVC ? 32 : 16;
		fmt->fmt.pix_mp.width = ALIGN(f->fmt.pix_mp.width, codec_align);
		fmt->fmt.pix_mp.height = ALIGN(f->fmt.pix_mp.height, codec_align);
		fmt->fmt.pix_mp.num_planes = 1;
		fmt->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
		fmt->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_INPUT);
		inst->buffers.input.min_count = iris_get_buf_min_count(inst, BUF_INPUT);
		if (inst->buffers.input.actual_count <
			inst->buffers.input.min_count) {
			inst->buffers.input.actual_count =
				inst->buffers.input.min_count;
		}
		inst->buffers.input.size =
			fmt->fmt.pix_mp.plane_fmt[0].sizeimage;

		fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
		fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
		fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

		output_fmt = inst->fmt_dst;
		output_fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
		output_fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
		output_fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		output_fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

		inst->crop.left = 0;
		inst->crop.top = 0;
		inst->crop.width = f->fmt.pix_mp.width;
		inst->crop.height = f->fmt.pix_mp.height;
	} else if (f->type == OUTPUT_MPLANE) {
		fmt = inst->fmt_dst;
		fmt->type = OUTPUT_MPLANE;
		if (inst->vb2q_src->streaming) {
			f->fmt.pix_mp.height = inst->fmt_src->fmt.pix_mp.height;
			f->fmt.pix_mp.width = inst->fmt_src->fmt.pix_mp.width;
		}
		fmt->fmt.pix_mp.pixelformat = f->fmt.pix_mp.pixelformat;
		codec_align = f->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_QC10C ? 192 : 128;
		fmt->fmt.pix_mp.width = ALIGN(f->fmt.pix_mp.width, codec_align);
		codec_align = f->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_QC10C ? 16 : 32;
		fmt->fmt.pix_mp.height = ALIGN(f->fmt.pix_mp.height, codec_align);
		fmt->fmt.pix_mp.num_planes = 1;
		if (f->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_QC10C) {
			stride = ALIGN(f->fmt.pix_mp.width, 192);
			fmt->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(stride * 4 / 3, 256);
		} else {
			fmt->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(f->fmt.pix_mp.width, 128);
		}
		fmt->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);

		if (!inst->vb2q_src->streaming)
			inst->buffers.output.min_count = iris_get_buf_min_count(inst, BUF_OUTPUT);
		if (inst->buffers.output.actual_count <
			inst->buffers.output.min_count) {
			inst->buffers.output.actual_count =
				inst->buffers.output.min_count;
		}

		colorformat = v4l2_colorformat_to_driver(inst, fmt->fmt.pix_mp.pixelformat);
		inst->buffers.output.size =
			fmt->fmt.pix_mp.plane_fmt[0].sizeimage;
		inst->cap[PIX_FMTS].value = colorformat;

		if (!inst->vb2q_src->streaming) {
			inst->crop.top = 0;
			inst->crop.left = 0;
			inst->crop.width = f->fmt.pix_mp.width;
			inst->crop.height = f->fmt.pix_mp.height;
		}
	} else {
		return -EINVAL;
	}
	memcpy(f, fmt, sizeof(*fmt));

	return ret;
}

int vdec_subscribe_event(struct iris_inst *inst, const struct v4l2_event_subscription *sub)
{
	int ret = 0;

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		ret = v4l2_event_subscribe(&inst->fh, sub, MAX_EVENTS, NULL);
		break;
	case V4L2_EVENT_SOURCE_CHANGE:
		ret = v4l2_src_change_event_subscribe(&inst->fh, sub);
		break;
	case V4L2_EVENT_CTRL:
		ret = v4l2_ctrl_subscribe_event(&inst->fh, sub);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int vdec_subscribe_property(struct iris_inst *inst, u32 plane)
{
	const u32 *subcribe_prop = NULL;
	u32 subscribe_prop_size = 0;
	struct iris_core *core;
	u32 payload[32] = {0};
	u32 i;

	core = inst->core;

	payload[0] = HFI_MODE_PROPERTY;

	if (plane == INPUT_MPLANE) {
		subscribe_prop_size = core->platform_data->dec_input_prop_size;
		subcribe_prop = core->platform_data->dec_input_prop;
	} else if (plane == OUTPUT_MPLANE) {
		if (inst->codec == H264) {
			subscribe_prop_size = core->platform_data->dec_output_prop_size_avc;
			subcribe_prop = core->platform_data->dec_output_prop_avc;
		} else if (inst->codec == HEVC) {
			subscribe_prop_size = core->platform_data->dec_output_prop_size_hevc;
			subcribe_prop = core->platform_data->dec_output_prop_hevc;
		} else if (inst->codec == VP9) {
			subscribe_prop_size = core->platform_data->dec_output_prop_size_vp9;
			subcribe_prop = core->platform_data->dec_output_prop_vp9;
		} else {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	for (i = 0; i < subscribe_prop_size; i++)
		payload[i + 1] = subcribe_prop[i];

	return iris_hfi_session_subscribe_mode(inst,
					HFI_CMD_SUBSCRIBE_MODE,
					plane,
					HFI_PAYLOAD_U32_ARRAY,
					&payload[0],
					(subscribe_prop_size + 1) * sizeof(u32));
}

static int vdec_set_bitstream_resolution(struct iris_inst *inst)
{
	u32 resolution;

	resolution = inst->fmt_src->fmt.pix_mp.width << 16 |
		inst->fmt_src->fmt.pix_mp.height;
	inst->src_subcr_params.bitstream_resolution = resolution;

	return iris_hfi_set_property(inst,
					HFI_PROP_BITSTREAM_RESOLUTION,
					HFI_HOST_FLAGS_NONE,
					get_hfi_port(inst, INPUT_MPLANE),
					HFI_PAYLOAD_U32,
					&resolution,
					sizeof(u32));
}

static int vdec_set_crop_offsets(struct iris_inst *inst)
{
	u32 left_offset, top_offset, right_offset, bottom_offset;
	u32 payload[2] = {0};

	left_offset = inst->crop.left;
	top_offset = inst->crop.top;
	right_offset = (inst->fmt_src->fmt.pix_mp.width -
		inst->crop.width);
	bottom_offset = (inst->fmt_src->fmt.pix_mp.height -
		inst->crop.height);

	payload[0] = left_offset << 16 | top_offset;
	payload[1] = right_offset << 16 | bottom_offset;
	inst->src_subcr_params.crop_offsets[0] = payload[0];
	inst->src_subcr_params.crop_offsets[1] = payload[1];

	return iris_hfi_set_property(inst,
					HFI_PROP_CROP_OFFSETS,
					HFI_HOST_FLAGS_NONE,
					get_hfi_port(inst, INPUT_MPLANE),
					HFI_PAYLOAD_64_PACKED,
					&payload,
					sizeof(u64));
}

static int vdec_set_bit_depth(struct iris_inst *inst)
{
	u32 bitdepth = 8 << 16 | 8;
	u32 pix_fmt;

	pix_fmt = inst->fmt_dst->fmt.pix_mp.pixelformat;
	if (is_10bit_colorformat(pix_fmt))
		bitdepth = 10 << 16 | 10;

	inst->src_subcr_params.bit_depth = bitdepth;
	inst->cap[BIT_DEPTH].value = bitdepth;

	return iris_hfi_set_property(inst,
					HFI_PROP_LUMA_CHROMA_BIT_DEPTH,
					HFI_HOST_FLAGS_NONE,
					get_hfi_port(inst, INPUT_MPLANE),
					HFI_PAYLOAD_U32,
					&bitdepth,
					sizeof(u32));
}

static int vdec_set_coded_frames(struct iris_inst *inst)
{
	u32 coded_frames = 0;

	if (inst->cap[CODED_FRAMES].value == CODED_FRAMES_PROGRESSIVE)
		coded_frames = HFI_BITMASK_FRAME_MBS_ONLY_FLAG;
	inst->src_subcr_params.coded_frames = coded_frames;

	return iris_hfi_set_property(inst,
					HFI_PROP_CODED_FRAMES,
					HFI_HOST_FLAGS_NONE,
					get_hfi_port(inst, INPUT_MPLANE),
					HFI_PAYLOAD_U32,
					&coded_frames,
					sizeof(u32));
}

static int vdec_set_min_output_count(struct iris_inst *inst)
{
	u32 min_output;

	min_output = inst->buffers.output.min_count;
	inst->src_subcr_params.fw_min_count = min_output;

	return iris_hfi_set_property(inst,
					HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT,
					HFI_HOST_FLAGS_NONE,
					get_hfi_port(inst, INPUT_MPLANE),
					HFI_PAYLOAD_U32,
					&min_output,
					sizeof(u32));
}

static int vdec_set_picture_order_count(struct iris_inst *inst)
{
	u32 poc = 0;

	inst->src_subcr_params.pic_order_cnt = poc;

	return iris_hfi_set_property(inst,
					HFI_PROP_PIC_ORDER_CNT_TYPE,
					HFI_HOST_FLAGS_NONE,
					get_hfi_port(inst, INPUT_MPLANE),
					HFI_PAYLOAD_U32,
					&poc,
					sizeof(u32));
}

static int vdec_set_colorspace(struct iris_inst *inst)
{
	u32 video_signal_type_present_flag = 0, color_info = 0;
	u32 matrix_coeff = HFI_MATRIX_COEFF_RESERVED;
	u32 video_format = UNSPECIFIED_COLOR_FORMAT;
	struct v4l2_pix_format_mplane *pixmp = NULL;
	u32 full_range = V4L2_QUANTIZATION_DEFAULT;
	u32 transfer_char = HFI_TRANSFER_RESERVED;
	u32 colour_description_present_flag = 0;
	u32 primaries = HFI_PRIMARIES_RESERVED;

	int ret;

	if (inst->codec == VP9)
		return 0;

	pixmp = &inst->fmt_src->fmt.pix_mp;
	if (pixmp->colorspace != V4L2_COLORSPACE_DEFAULT ||
	    pixmp->ycbcr_enc != V4L2_YCBCR_ENC_DEFAULT ||
	    pixmp->xfer_func != V4L2_XFER_FUNC_DEFAULT) {
		colour_description_present_flag = 1;
		video_signal_type_present_flag = 1;
		primaries = get_hfi_color_primaries(pixmp->colorspace);
		matrix_coeff = get_hfi_matrix_coefficients(pixmp->ycbcr_enc);
		transfer_char = get_hfi_transer_char(pixmp->xfer_func);
	}

	if (pixmp->quantization != V4L2_QUANTIZATION_DEFAULT) {
		video_signal_type_present_flag = 1;
		full_range = pixmp->quantization ==
			V4L2_QUANTIZATION_FULL_RANGE ? 1 : 0;
	}

	color_info = (matrix_coeff & 0xFF) |
		((transfer_char << 8) & 0xFF00) |
		((primaries << 16) & 0xFF0000) |
		((colour_description_present_flag << 24) & 0x1000000) |
		((full_range << 25) & 0x2000000) |
		((video_format << 26) & 0x1C000000) |
		((video_signal_type_present_flag << 29) & 0x20000000);

	inst->src_subcr_params.color_info = color_info;

	ret = iris_hfi_set_property(inst,
				    HFI_PROP_SIGNAL_COLOR_INFO,
				    HFI_HOST_FLAGS_NONE,
				    get_hfi_port(inst, INPUT_MPLANE),
				    HFI_PAYLOAD_32_PACKED,
				    &color_info,
				    sizeof(u32));

	return ret;
}

static int vdec_set_profile(struct iris_inst *inst)
{
	u32 profile;

	profile = inst->cap[PROFILE].value;
	inst->src_subcr_params.profile = profile;

	return iris_hfi_set_property(inst,
				     HFI_PROP_PROFILE,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, INPUT_MPLANE),
				     HFI_PAYLOAD_U32_ENUM,
				     &profile,
				     sizeof(u32));
}

static int vdec_set_level(struct iris_inst *inst)
{
	u32 level;

	level = inst->cap[LEVEL].value;
	inst->src_subcr_params.level = level;

	return iris_hfi_set_property(inst,
				     HFI_PROP_LEVEL,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, INPUT_MPLANE),
				     HFI_PAYLOAD_U32_ENUM,
				     &level,
				     sizeof(u32));
}

static int vdec_set_tier(struct iris_inst *inst)
{
	u32 tier;

	tier = inst->cap[HEVC_TIER].value;
	inst->src_subcr_params.tier = tier;

	return iris_hfi_set_property(inst,
				     HFI_PROP_TIER,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, INPUT_MPLANE),
				     HFI_PAYLOAD_U32_ENUM,
				     &tier,
				     sizeof(u32));
}

static int vdec_subscribe_src_change_param(struct iris_inst *inst)
{
	const u32 *src_change_param;
	u32 src_change_param_size;
	struct iris_core *core;
	u32 payload[32] = {0};
	int ret;
	u32 i, j;

	static const struct vdec_prop_type_handle prop_type_handle_arr[] = {
		{HFI_PROP_BITSTREAM_RESOLUTION,          vdec_set_bitstream_resolution   },
		{HFI_PROP_CROP_OFFSETS,                  vdec_set_crop_offsets           },
		{HFI_PROP_LUMA_CHROMA_BIT_DEPTH,         vdec_set_bit_depth              },
		{HFI_PROP_CODED_FRAMES,                  vdec_set_coded_frames           },
		{HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT,    vdec_set_min_output_count       },
		{HFI_PROP_PIC_ORDER_CNT_TYPE,            vdec_set_picture_order_count    },
		{HFI_PROP_SIGNAL_COLOR_INFO,             vdec_set_colorspace             },
		{HFI_PROP_PROFILE,                       vdec_set_profile                },
		{HFI_PROP_LEVEL,                         vdec_set_level                  },
		{HFI_PROP_TIER,                          vdec_set_tier                   },
	};

	core = inst->core;

	payload[0] = HFI_MODE_PORT_SETTINGS_CHANGE;
	if (inst->codec == H264) {
		src_change_param_size = core->platform_data->avc_subscribe_param_size;
		src_change_param = core->platform_data->avc_subscribe_param;
	} else if (inst->codec == HEVC) {
		src_change_param_size = core->platform_data->hevc_subscribe_param_size;
		src_change_param = core->platform_data->hevc_subscribe_param;
	} else if (inst->codec == VP9) {
		src_change_param_size = core->platform_data->vp9_subscribe_param_size;
		src_change_param = core->platform_data->vp9_subscribe_param;
	} else {
		src_change_param = NULL;
		return -EINVAL;
	}

	if (!src_change_param || !src_change_param_size)
		return -EINVAL;

	for (i = 0; i < src_change_param_size; i++)
		payload[i + 1] = src_change_param[i];

	ret = iris_hfi_session_subscribe_mode(inst,
					      HFI_CMD_SUBSCRIBE_MODE,
					      INPUT_MPLANE,
					      HFI_PAYLOAD_U32_ARRAY,
					      &payload[0],
					      ((src_change_param_size + 1) * sizeof(u32)));
	if (ret)
		return ret;

	for (i = 0; i < src_change_param_size; i++) {
		for (j = 0; j < ARRAY_SIZE(prop_type_handle_arr); j++) {
			if (prop_type_handle_arr[j].type == src_change_param[i]) {
				ret = prop_type_handle_arr[j].handle(inst);
				if (ret)
					return ret;
				break;
			}
		}
	}

	return ret;
}

int vdec_init_src_change_param(struct iris_inst *inst)
{
	u32 left_offset, top_offset, right_offset, bottom_offset;
	struct v4l2_pix_format_mplane *pixmp_ip, *pixmp_op;
	u32 primaries, matrix_coeff, transfer_char;
	struct subscription_params *subsc_params;
	u32 colour_description_present_flag = 0;
	u32 video_signal_type_present_flag = 0;
	u32 full_range = 0, video_format = 0;

	subsc_params = &inst->src_subcr_params;
	pixmp_ip = &inst->fmt_src->fmt.pix_mp;
	pixmp_op = &inst->fmt_dst->fmt.pix_mp;

	subsc_params->bitstream_resolution =
		pixmp_ip->width << 16 | pixmp_ip->height;

	left_offset = inst->crop.left;
	top_offset = inst->crop.top;
	right_offset = (pixmp_ip->width - inst->crop.width);
	bottom_offset = (pixmp_ip->height - inst->crop.height);
	subsc_params->crop_offsets[0] =
			left_offset << 16 | top_offset;
	subsc_params->crop_offsets[1] =
			right_offset << 16 | bottom_offset;

	subsc_params->fw_min_count = inst->buffers.output.min_count;

	primaries = get_hfi_color_primaries(pixmp_op->colorspace);
	matrix_coeff = get_hfi_matrix_coefficients(pixmp_op->ycbcr_enc);
	transfer_char = get_hfi_transer_char(pixmp_op->xfer_func);
	full_range = pixmp_op->quantization == V4L2_QUANTIZATION_FULL_RANGE ? 1 : 0;
	subsc_params->color_info =
		(matrix_coeff & 0xFF) |
		((transfer_char << 8) & 0xFF00) |
		((primaries << 16) & 0xFF0000) |
		((colour_description_present_flag << 24) & 0x1000000) |
		((full_range << 25) & 0x2000000) |
		((video_format << 26) & 0x1C000000) |
		((video_signal_type_present_flag << 29) & 0x20000000);

	subsc_params->profile = inst->cap[PROFILE].value;
	subsc_params->level = inst->cap[LEVEL].value;
	subsc_params->tier = inst->cap[HEVC_TIER].value;
	subsc_params->pic_order_cnt = inst->cap[POC].value;
	subsc_params->bit_depth = inst->cap[BIT_DEPTH].value;
	if (inst->cap[CODED_FRAMES].value ==
			CODED_FRAMES_PROGRESSIVE)
		subsc_params->coded_frames = HFI_BITMASK_FRAME_MBS_ONLY_FLAG;
	else
		subsc_params->coded_frames = 0;

	return 0;
}

static int vdec_read_input_subcr_params(struct iris_inst *inst)
{
	struct v4l2_pix_format_mplane *pixmp_ip, *pixmp_op;
	u32 primaries, matrix_coeff, transfer_char;
	struct subscription_params subsc_params;
	u32 colour_description_present_flag = 0;
	u32 video_signal_type_present_flag = 0;
	u32 full_range = 0;
	u32 width, height;

	subsc_params = inst->src_subcr_params;
	pixmp_ip = &inst->fmt_src->fmt.pix_mp;
	pixmp_op = &inst->fmt_dst->fmt.pix_mp;
	width = (subsc_params.bitstream_resolution &
		HFI_BITMASK_BITSTREAM_WIDTH) >> 16;
	height = subsc_params.bitstream_resolution &
		HFI_BITMASK_BITSTREAM_HEIGHT;

	pixmp_ip->width = width;
	pixmp_ip->height = height;

	pixmp_op->width = pixmp_op->pixelformat == V4L2_PIX_FMT_QC10C ?
		ALIGN(width, 192) : ALIGN(width, 128);
	pixmp_op->height = pixmp_op->pixelformat == V4L2_PIX_FMT_QC10C ?
		ALIGN(height, 16) : ALIGN(height, 32);
	pixmp_op->plane_fmt[0].bytesperline =
		pixmp_op->pixelformat == V4L2_PIX_FMT_QC10C ?
		ALIGN(ALIGN(width, 192) * 4 / 3, 256) :
		ALIGN(width, 128);
	pixmp_op->plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);

	matrix_coeff = subsc_params.color_info & 0xFF;
	transfer_char = (subsc_params.color_info & 0xFF00) >> 8;
	primaries = (subsc_params.color_info & 0xFF0000) >> 16;
	colour_description_present_flag =
		(subsc_params.color_info & 0x1000000) >> 24;
	full_range = (subsc_params.color_info & 0x2000000) >> 25;
	video_signal_type_present_flag =
		(subsc_params.color_info & 0x20000000) >> 29;

	pixmp_op->colorspace = V4L2_COLORSPACE_DEFAULT;
	pixmp_op->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	pixmp_op->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pixmp_op->quantization = V4L2_QUANTIZATION_DEFAULT;

	if (video_signal_type_present_flag) {
		pixmp_op->quantization =
			full_range ?
			V4L2_QUANTIZATION_FULL_RANGE :
			V4L2_QUANTIZATION_LIM_RANGE;
		if (colour_description_present_flag) {
			pixmp_op->colorspace =
				get_v4l2_color_primaries(primaries);
			pixmp_op->xfer_func =
				get_v4l2_transer_char(transfer_char);
			pixmp_op->ycbcr_enc =
				get_v4l2_matrix_coefficients(matrix_coeff);
		}
	}

	pixmp_ip->colorspace = pixmp_op->colorspace;
	pixmp_ip->xfer_func = pixmp_op->xfer_func;
	pixmp_ip->ycbcr_enc = pixmp_op->ycbcr_enc;
	pixmp_ip->quantization = pixmp_op->quantization;

	inst->crop.top = subsc_params.crop_offsets[0] & 0xFFFF;
	inst->crop.left = (subsc_params.crop_offsets[0] >> 16) & 0xFFFF;
	inst->crop.height = pixmp_ip->height -
		(subsc_params.crop_offsets[1] & 0xFFFF) - inst->crop.top;
	inst->crop.width = pixmp_ip->width -
		((subsc_params.crop_offsets[1] >> 16) & 0xFFFF) - inst->crop.left;

	inst->cap[PROFILE].value = subsc_params.profile;
	inst->cap[LEVEL].value = subsc_params.level;
	inst->cap[HEVC_TIER].value = subsc_params.tier;
	inst->cap[POC].value = subsc_params.pic_order_cnt;

	if (subsc_params.bit_depth == BIT_DEPTH_8)
		inst->cap[BIT_DEPTH].value = BIT_DEPTH_8;
	else
		inst->cap[BIT_DEPTH].value = BIT_DEPTH_10;

	if (subsc_params.coded_frames & HFI_BITMASK_FRAME_MBS_ONLY_FLAG)
		inst->cap[CODED_FRAMES].value = CODED_FRAMES_PROGRESSIVE;
	else
		inst->cap[CODED_FRAMES].value = CODED_FRAMES_INTERLACE;

	inst->fw_min_count = subsc_params.fw_min_count;
	inst->buffers.output.min_count = iris_get_buf_min_count(inst, BUF_OUTPUT);

	return 0;
}

int vdec_src_change(struct iris_inst *inst)
{
	struct v4l2_event event = {0};
	u32 ret;

	if (!inst->vb2q_src->streaming)
		return 0;

	ret = vdec_read_input_subcr_params(inst);
	if (ret)
		return ret;

	event.type = V4L2_EVENT_SOURCE_CHANGE;
	event.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION;
	v4l2_event_queue_fh(&inst->fh, &event);

	return ret;
}

static int vdec_set_colorformat(struct iris_inst *inst)
{
	u32 hfi_colorformat;
	u32 pixelformat;

	pixelformat = inst->fmt_dst->fmt.pix_mp.pixelformat;
	hfi_colorformat = get_hfi_colorformat(pixelformat);

	return iris_hfi_set_property(inst,
					 HFI_PROP_COLOR_FORMAT,
					 HFI_HOST_FLAGS_NONE,
					 get_hfi_port(inst, OUTPUT_MPLANE),
					 HFI_PAYLOAD_U32,
					 &hfi_colorformat,
					 sizeof(u32));
}

static int vdec_set_linear_stride_scanline(struct iris_inst *inst)
{
	u32 stride_y, scanline_y, stride_uv, scanline_uv;
	u32 pixelformat;
	u32 payload[2];

	pixelformat = inst->fmt_dst->fmt.pix_mp.pixelformat;

	if (!is_linear_colorformat(pixelformat))
		return 0;

	stride_y = inst->fmt_dst->fmt.pix_mp.width;
	scanline_y = inst->fmt_dst->fmt.pix_mp.height;
	stride_uv = stride_y;
	scanline_uv = scanline_y / 2;

	payload[0] = stride_y << 16 | scanline_y;
	payload[1] = stride_uv << 16 | scanline_uv;

	return iris_hfi_set_property(inst,
					 HFI_PROP_LINEAR_STRIDE_SCANLINE,
					 HFI_HOST_FLAGS_NONE,
					 get_hfi_port(inst, OUTPUT_MPLANE),
					 HFI_PAYLOAD_U64,
					 &payload,
					 sizeof(u64));
}

static int vdec_set_ubwc_stride_scanline(struct iris_inst *inst)
{
	u32 meta_stride_y, meta_scanline_y, meta_stride_uv, meta_scanline_uv;
	u32 stride_y, scanline_y, stride_uv, scanline_uv;
	u32 pix_fmt, width, height;
	u32 payload[4];

	pix_fmt = inst->fmt_dst->fmt.pix_mp.pixelformat;
	width = inst->fmt_dst->fmt.pix_mp.width;
	height = inst->fmt_dst->fmt.pix_mp.height;

	if (is_linear_colorformat(pix_fmt))
		return 0;

	if (pix_fmt == V4L2_PIX_FMT_QC08C) {
		stride_y = ALIGN(width, 128);
		scanline_y = ALIGN(height, 32);
		stride_uv = ALIGN(width, 128);
		scanline_uv = ALIGN((height + 1) >> 1, 32);
		meta_stride_y = ALIGN(DIV_ROUND_UP(width, 32), 64);
		meta_scanline_y = ALIGN(DIV_ROUND_UP(height, 8), 16);
		meta_stride_uv = ALIGN(DIV_ROUND_UP((width + 1) >> 1, 16), 64);
		meta_scanline_uv = ALIGN(DIV_ROUND_UP((height + 1) >> 1, 8), 16);
	} else {
		stride_y = ALIGN(ALIGN(width, 192) * 4 / 3, 256);
		scanline_y = ALIGN(height, 16);
		stride_uv = ALIGN(ALIGN(width, 192) * 4 / 3, 256);
		scanline_uv = ALIGN((height + 1) >> 1, 16);
		meta_stride_y = ALIGN(DIV_ROUND_UP(width, 48), 64);
		meta_scanline_y = ALIGN(DIV_ROUND_UP(height, 4), 16);
		meta_stride_uv = ALIGN(DIV_ROUND_UP((width + 1) >> 1, 24), 64);
		meta_scanline_uv = ALIGN(DIV_ROUND_UP((height + 1) >> 1, 4), 16);
	}

	payload[0] = stride_y << 16 | scanline_y;
	payload[1] = stride_uv << 16 | scanline_uv;
	payload[2] = meta_stride_y << 16 | meta_scanline_y;
	payload[3] = meta_stride_uv << 16 | meta_scanline_uv;

	return iris_hfi_set_property(inst,
				     HFI_PROP_UBWC_STRIDE_SCANLINE,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, OUTPUT_MPLANE),
				     HFI_PAYLOAD_U32_ARRAY,
				     &payload[0],
				     sizeof(u32) * 4);
}

static int vdec_set_output_property(struct iris_inst *inst)
{
	int ret;

	ret = vdec_set_colorformat(inst);
	if (ret)
		return ret;

	ret = vdec_set_linear_stride_scanline(inst);
	if (ret)
		return ret;

	return vdec_set_ubwc_stride_scanline(inst);
}

static int vdec_subscribe_dst_change_param(struct iris_inst *inst)
{
	u32 prop_type, payload_size, payload_type;
	struct subscription_params subsc_params;
	const u32 *dst_change_param = NULL;
	u32 dst_change_param_size = 0;
	struct iris_core *core;
	u32 payload[32] = {0};
	int ret;
	u32 i;

	core = inst->core;

	payload[0] = HFI_MODE_PORT_SETTINGS_CHANGE;
	if (inst->codec == H264) {
		dst_change_param_size = core->platform_data->avc_subscribe_param_size;
		dst_change_param = core->platform_data->avc_subscribe_param;
	} else if (inst->codec == HEVC) {
		dst_change_param_size = core->platform_data->hevc_subscribe_param_size;
		dst_change_param = core->platform_data->hevc_subscribe_param;
	} else if (inst->codec == VP9) {
		dst_change_param_size = core->platform_data->vp9_subscribe_param_size;
		dst_change_param = core->platform_data->vp9_subscribe_param;
	} else {
		dst_change_param = NULL;
		return -EINVAL;
	}

	if (!dst_change_param || !dst_change_param_size)
		return -EINVAL;

	payload[0] = HFI_MODE_PORT_SETTINGS_CHANGE;
	for (i = 0; i < dst_change_param_size; i++)
		payload[i + 1] = dst_change_param[i];

	ret = iris_hfi_session_subscribe_mode(inst,
					      HFI_CMD_SUBSCRIBE_MODE,
					      OUTPUT_MPLANE,
					      HFI_PAYLOAD_U32_ARRAY,
					      &payload[0],
					      ((dst_change_param_size + 1) * sizeof(u32)));
	if (ret)
		return ret;

	subsc_params = inst->dst_subcr_params;
	for (i = 0; i < dst_change_param_size; i++) {
		payload[0] = 0;
		payload[1] = 0;
		payload_size = 0;
		payload_type = 0;
		prop_type = dst_change_param[i];
		switch (prop_type) {
		case HFI_PROP_BITSTREAM_RESOLUTION:
			payload[0] = subsc_params.bitstream_resolution;
			payload_size = sizeof(u32);
			payload_type = HFI_PAYLOAD_U32;
			break;
		case HFI_PROP_CROP_OFFSETS:
			payload[0] = subsc_params.crop_offsets[0];
			payload[1] = subsc_params.crop_offsets[1];
			payload_size = sizeof(u64);
			payload_type = HFI_PAYLOAD_64_PACKED;
			break;
		case HFI_PROP_LUMA_CHROMA_BIT_DEPTH:
			payload[0] = subsc_params.bit_depth;
			payload_size = sizeof(u32);
			payload_type = HFI_PAYLOAD_U32;
			break;
		case HFI_PROP_CODED_FRAMES:
			payload[0] = subsc_params.coded_frames;
			payload_size = sizeof(u32);
			payload_type = HFI_PAYLOAD_U32;
			break;
		case HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT:
			payload[0] = subsc_params.fw_min_count;
			payload_size = sizeof(u32);
			payload_type = HFI_PAYLOAD_U32;
			break;
		case HFI_PROP_PIC_ORDER_CNT_TYPE:
			payload[0] = subsc_params.pic_order_cnt;
			payload_size = sizeof(u32);
			payload_type = HFI_PAYLOAD_U32;
			break;
		case HFI_PROP_SIGNAL_COLOR_INFO:
			payload[0] = subsc_params.color_info;
			payload_size = sizeof(u32);
			payload_type = HFI_PAYLOAD_U32;
			break;
		case HFI_PROP_PROFILE:
			payload[0] = subsc_params.profile;
			payload_size = sizeof(u32);
			payload_type = HFI_PAYLOAD_U32;
			break;
		case HFI_PROP_LEVEL:
			payload[0] = subsc_params.level;
			payload_size = sizeof(u32);
			payload_type = HFI_PAYLOAD_U32;
			break;
		case HFI_PROP_TIER:
			payload[0] = subsc_params.tier;
			payload_size = sizeof(u32);
			payload_type = HFI_PAYLOAD_U32;
			break;
		default:
			prop_type = 0;
			ret = -EINVAL;
			break;
		}
		if (prop_type) {
			ret = iris_hfi_set_property(inst,
						    prop_type,
						    HFI_HOST_FLAGS_NONE,
						    get_hfi_port(inst, OUTPUT_MPLANE),
						    payload_type,
						    &payload,
						    payload_size);
			if (ret)
				return ret;
		}
	}

	return ret;
}

int vdec_streamon_input(struct iris_inst *inst)
{
	int ret;

	ret = check_session_supported(inst);
	if (ret)
		return ret;

	ret = set_v4l2_properties(inst);
	if (ret)
		return ret;

	ret = iris_get_internal_buffers(inst, INPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_destroy_internal_buffers(inst, INPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_create_input_internal_buffers(inst);
	if (ret)
		return ret;

	ret = iris_queue_input_internal_buffers(inst);
	if (ret)
		return ret;

	if (!inst->ipsc_properties_set) {
		ret = vdec_subscribe_src_change_param(inst);
		if (ret)
			return ret;
		inst->ipsc_properties_set = true;
	}

	ret = vdec_subscribe_property(inst, INPUT_MPLANE);
	if (ret)
		return ret;

	ret = process_streamon_input(inst);
	if (ret)
		return ret;

	return ret;
}

int vdec_streamon_output(struct iris_inst *inst)
{
	int ret;

	ret = check_session_supported(inst);
	if (ret)
		return ret;

	ret = vdec_set_output_property(inst);
	if (ret)
		goto error;

	if (!inst->opsc_properties_set) {
		memcpy(&inst->dst_subcr_params,
		       &inst->src_subcr_params,
		       sizeof(inst->src_subcr_params));
		ret = vdec_subscribe_dst_change_param(inst);
		if (ret)
			goto error;
		inst->opsc_properties_set = true;
	}

	ret = vdec_subscribe_property(inst, OUTPUT_MPLANE);
	if (ret)
		goto error;

	ret = iris_get_internal_buffers(inst, OUTPUT_MPLANE);
	if (ret)
		goto error;

	ret = iris_destroy_internal_buffers(inst, OUTPUT_MPLANE);
	if (ret)
		goto error;

	ret = iris_create_output_internal_buffers(inst);
	if (ret)
		goto error;

	ret = process_streamon_output(inst);
	if (ret)
		goto error;

	ret = iris_queue_output_internal_buffers(inst);
	if (ret)
		goto error;

	return ret;

error:
	session_streamoff(inst, OUTPUT_MPLANE);

	return ret;
}

int vdec_qbuf(struct iris_inst *inst, struct vb2_buffer *vb2)
{
	struct iris_buffer *buf = NULL;
	int ret;

	buf = get_driver_buf(inst, vb2->type, vb2->index);
	if (!buf)
		return -EINVAL;

	ret = vb2_buffer_to_driver(vb2, buf);
	if (ret)
		return ret;

	if (!allow_qbuf(inst, vb2->type)) {
		buf->attr |= BUF_ATTR_DEFERRED;
		return 0;
	}

	iris_scale_power(inst);

	ret = queue_buffer(inst, buf);
	if (ret)
		return ret;

	if (vb2->type == OUTPUT_MPLANE)
		ret = iris_release_nonref_buffers(inst);

	return ret;
}

int vdec_start_cmd(struct iris_inst *inst)
{
	int ret;

	vb2_clear_last_buffer_dequeued(inst->vb2q_dst);

	if (inst->sub_state & IRIS_INST_SUB_DRC &&
	    inst->sub_state & IRIS_INST_SUB_DRC_LAST &&
	    inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
		ret = iris_alloc_and_queue_input_int_bufs(inst);
		if (ret)
			return ret;

		ret = set_stage(inst, STAGE);
		if (ret)
			return ret;

		ret = set_pipe(inst, PIPE);
		if (ret)
			return ret;
	}

	ret = iris_alloc_and_queue_additional_dpb_buffers(inst);
	if (ret)
		return ret;

	ret = queue_deferred_buffers(inst, BUF_OUTPUT);
	if (ret)
		return ret;

	ret = process_resume(inst);

	return ret;
}

int vdec_stop_cmd(struct iris_inst *inst)
{
	int ret;

	ret = iris_hfi_drain(inst, INPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_inst_change_sub_state(inst, 0, IRIS_INST_SUB_DRAIN);

	return ret;
}
