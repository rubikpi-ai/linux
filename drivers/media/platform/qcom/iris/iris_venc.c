// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hfi_defines.h"
#include "iris_buffer.h"
#include "iris_common.h"
#include "iris_ctrls.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_hfi_packet.h"
#include "iris_power.h"
#include "iris_venc.h"

#define SCALE_FACTOR 8
#define UNSPECIFIED_COLOR_FORMAT 5

static const u32 enc_input_properties[] = {
	HFI_PROP_NO_OUTPUT,
};

static const u32 enc_output_properties[] = {
	HFI_PROP_PICTURE_TYPE,
	HFI_PROP_BUFFER_MARK,
};

struct venc_prop_type_handle {
	u32 type;
	int (*handle)(struct iris_inst *inst);
};

int venc_inst_init(struct iris_inst *inst)
{
	struct v4l2_format *f;
	int ret;

	inst->fmt_src = kzalloc(sizeof(*inst->fmt_src), GFP_KERNEL);
	if (!inst->fmt_src)
		return -ENOMEM;

	inst->fmt_dst  = kzalloc(sizeof(*inst->fmt_dst), GFP_KERNEL);
	if (!inst->fmt_dst)
		return -ENOMEM;

	inst->vb2q_src = kzalloc(sizeof(*inst->vb2q_src), GFP_KERNEL);
	if (!inst->vb2q_src)
		return -ENOMEM;

	inst->vb2q_dst = kzalloc(sizeof(*inst->vb2q_dst), GFP_KERNEL);
	if (!inst->vb2q_dst)
		return -ENOMEM;

	f = inst->fmt_dst;
	f->type = OUTPUT_MPLANE;
	f->fmt.pix_mp.width = DEFAULT_WIDTH;
	f->fmt.pix_mp.height = DEFAULT_HEIGHT;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->buffers.output.min_count = iris_get_buf_min_count(inst, BUF_OUTPUT);
	inst->buffers.output.actual_count = inst->buffers.output.min_count;
	inst->buffers.output.size = f->fmt.pix_mp.plane_fmt[0].sizeimage;

	inst->crop.left = 0;
	inst->crop.top = 0;
	inst->crop.width = f->fmt.pix_mp.width;
	inst->crop.height = f->fmt.pix_mp.height;

	inst->compose.left = 0;
	inst->compose.top = 0;
	inst->compose.width = f->fmt.pix_mp.width;
	inst->compose.height = f->fmt.pix_mp.height;

	f = inst->fmt_src;
	f->type = INPUT_MPLANE;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_QC08C;
	f->fmt.pix_mp.width = DEFAULT_WIDTH;
	f->fmt.pix_mp.height = DEFAULT_HEIGHT;
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(DEFAULT_WIDTH, 128);
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_INPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->buffers.input.min_count = iris_get_buf_min_count(inst, BUF_INPUT);
	inst->buffers.input.actual_count = inst->buffers.input.min_count;
	inst->buffers.input.size = f->fmt.pix_mp.plane_fmt[0].sizeimage;

	inst->hfi_rc_type = HFI_RC_VBR_CFR;
	inst->hfi_layer_type = HFI_HIER_P_SLIDING_WINDOW;

	ret = codec_change(inst, inst->fmt_dst->fmt.pix_mp.pixelformat);

	return ret;
}

void venc_inst_deinit(struct iris_inst *inst)
{
	kfree(inst->fmt_dst);
	kfree(inst->fmt_src);
}

int venc_enum_fmt(struct iris_inst *inst, struct v4l2_fmtdesc *f)
{
	struct iris_core *core;
	u32 array[32] = {0};
	u32 i = 0;

	core = inst->core;

	if (f->type == OUTPUT_MPLANE) {
		u32 codecs = core->cap[ENC_CODECS].value;
		u32 idx = 0;

		for (i = 0; i <= 31; i++) {
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
		if (!f->pixelformat)
			return -EINVAL;
		f->flags = V4L2_FMT_FLAG_COMPRESSED;
		strscpy(f->description, "codec", sizeof(f->description));
	} else if (f->type == INPUT_MPLANE) {
		u32 formats = inst->cap[PIX_FMTS].step_or_mask;
		u32 idx = 0;

		for (i = 0; i <= 31; i++) {
			if (formats & BIT(i)) {
				if (idx >= ARRAY_SIZE(array))
					break;
				array[idx] = formats & BIT(i);
				idx++;
			}
		}
		if (!array[f->index])
			return -EINVAL;
		f->pixelformat = v4l2_colorformat_from_driver(inst, array[f->index]);
		if (!f->pixelformat)
			return -EINVAL;
		strscpy(f->description, "colorformat", sizeof(f->description));
	}

	memset(f->reserved, 0, sizeof(f->reserved));

	return 0;
}

int venc_try_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	u32 pix_fmt;

	memset(pixmp->reserved, 0, sizeof(pixmp->reserved));

	if (f->type == INPUT_MPLANE) {
		pix_fmt = v4l2_colorformat_to_driver(inst, f->fmt.pix_mp.pixelformat);
		if (!pix_fmt) {
			f->fmt.pix_mp.pixelformat = inst->fmt_src->fmt.pix_mp.pixelformat;
			f->fmt.pix_mp.width = inst->fmt_src->fmt.pix_mp.width;
			f->fmt.pix_mp.height = inst->fmt_src->fmt.pix_mp.height;
		}
	} else if (f->type == OUTPUT_MPLANE) {
		pix_fmt = v4l2_codec_to_driver(inst, f->fmt.pix_mp.pixelformat);
		if (!pix_fmt) {
			f->fmt.pix_mp.width = inst->fmt_dst->fmt.pix_mp.width;
			f->fmt.pix_mp.height = inst->fmt_dst->fmt.pix_mp.height;
			f->fmt.pix_mp.pixelformat = inst->fmt_dst->fmt.pix_mp.pixelformat;
		}
	} else {
		return -EINVAL;
	}

	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;
	pixmp->num_planes = 1;

	return 0;
}

static int venc_s_fmt_output(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_format *fmt;
	enum codec_type codec;
	u32 codec_align;
	u32 width, height;
	int ret = 0;

	venc_try_fmt(inst, f);

	fmt = inst->fmt_dst;
	if (fmt->fmt.pix_mp.pixelformat != f->fmt.pix_mp.pixelformat) {
		ret = codec_change(inst, f->fmt.pix_mp.pixelformat);
		if (ret)
			return ret;
	}
	fmt->type = OUTPUT_MPLANE;

	codec = v4l2_codec_to_driver(inst, f->fmt.pix_mp.pixelformat);

	codec_align = (codec == HEVC) ? 32 : 16;
	width = inst->compose.width;
	height = inst->compose.height;
	if (inst->cap[ROTATION].value == 90 || inst->cap[ROTATION].value == 270) {
		width = inst->compose.height;
		height = inst->compose.width;
	}
	fmt->fmt.pix_mp.width = ALIGN(width, codec_align);
	fmt->fmt.pix_mp.height = ALIGN(height, codec_align);
	fmt->fmt.pix_mp.num_planes = 1;
	fmt->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
	fmt->fmt.pix_mp.plane_fmt[0].sizeimage =
		iris_get_buffer_size(inst, BUF_OUTPUT);
	if (f->fmt.pix_mp.colorspace != V4L2_COLORSPACE_DEFAULT &&
	    f->fmt.pix_mp.colorspace != V4L2_COLORSPACE_REC709)
		f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;
	inst->buffers.output.min_count = iris_get_buf_min_count(inst, BUF_OUTPUT);
	if (inst->buffers.output.actual_count <
		inst->buffers.output.min_count) {
		inst->buffers.output.actual_count =
			inst->buffers.output.min_count;
	}
	inst->buffers.output.size =
		fmt->fmt.pix_mp.plane_fmt[0].sizeimage;

	memcpy(f, fmt, sizeof(struct v4l2_format));

	return ret;
}

static int venc_s_fmt_input(struct iris_inst *inst, struct v4l2_format *f)
{
	u32 pix_fmt, width, height, size, bytesperline;
	struct v4l2_format *fmt, *output_fmt;
	int ret = 0;

	venc_try_fmt(inst, f);

	pix_fmt = v4l2_colorformat_to_driver(inst, f->fmt.pix_mp.pixelformat);
	inst->cap[PIX_FMTS].value = pix_fmt;

	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	bytesperline = pix_fmt == FMT_TP10C ?
		ALIGN(ALIGN(f->fmt.pix_mp.width, 192) * 4 / 3, 256) :
		ALIGN(f->fmt.pix_mp.width, 128);

	fmt = inst->fmt_src;
	fmt->type = INPUT_MPLANE;
	fmt->fmt.pix_mp.width = width;
	fmt->fmt.pix_mp.height = height;
	fmt->fmt.pix_mp.num_planes = 1;
	fmt->fmt.pix_mp.pixelformat = f->fmt.pix_mp.pixelformat;
	fmt->fmt.pix_mp.plane_fmt[0].bytesperline = bytesperline;
	size = iris_get_buffer_size(inst, BUF_INPUT);
	fmt->fmt.pix_mp.plane_fmt[0].sizeimage = size;
	fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

	output_fmt = inst->fmt_dst;
	output_fmt->fmt.pix_mp.colorspace = fmt->fmt.pix_mp.colorspace;
	output_fmt->fmt.pix_mp.xfer_func = fmt->fmt.pix_mp.xfer_func;
	output_fmt->fmt.pix_mp.ycbcr_enc = fmt->fmt.pix_mp.ycbcr_enc;
	output_fmt->fmt.pix_mp.quantization = fmt->fmt.pix_mp.quantization;

	inst->buffers.input.min_count = iris_get_buf_min_count(inst, BUF_INPUT);
	if (inst->buffers.input.actual_count <
	    inst->buffers.input.min_count) {
		inst->buffers.input.actual_count =
			inst->buffers.input.min_count;
	}
	inst->buffers.input.size = size;

	if (f->fmt.pix_mp.width != inst->crop.width ||
	    f->fmt.pix_mp.height != inst->crop.height) {
		inst->crop.top = 0;
		inst->crop.left = 0;
		inst->crop.width = f->fmt.pix_mp.width;
		inst->crop.height = f->fmt.pix_mp.height;

		inst->compose.top = 0;
		inst->compose.left = 0;
		inst->compose.width = f->fmt.pix_mp.width;
		inst->compose.height = f->fmt.pix_mp.height;

		ret = venc_s_fmt_output(inst, output_fmt);
		if (ret)
			return ret;
	}

	memcpy(f, fmt, sizeof(struct v4l2_format));

	return ret;
}

int venc_s_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	int ret;

	if (f->type == INPUT_MPLANE)
		ret = venc_s_fmt_input(inst, f);
	else if (f->type == OUTPUT_MPLANE)
		ret = venc_s_fmt_output(inst, f);
	else
		ret = -EINVAL;

	return ret;
}

int venc_s_selection(struct iris_inst *inst, struct v4l2_selection *s)
{
	struct v4l2_format *output_fmt;
	int ret;

	if (s->type != INPUT_MPLANE && s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		if (s->r.left || s->r.top) {
			s->r.left = 0;
			s->r.top = 0;
		}
		if (s->r.width > inst->fmt_src->fmt.pix_mp.width)
			s->r.width = inst->fmt_src->fmt.pix_mp.width;

		if (s->r.height > inst->fmt_src->fmt.pix_mp.height)
			s->r.height = inst->fmt_src->fmt.pix_mp.height;

		inst->crop.left = s->r.left;
		inst->crop.top = s->r.top;
		inst->crop.width = s->r.width;
		inst->crop.height = s->r.height;
		inst->compose.left = inst->crop.left;
		inst->compose.top = inst->crop.top;
		inst->compose.width = inst->crop.width;
		inst->compose.height = inst->crop.height;
		output_fmt = inst->fmt_dst;
		ret = venc_s_fmt_output(inst, output_fmt);
		if (ret)
			return ret;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		if (s->r.left < inst->crop.left)
			s->r.left = inst->crop.left;

		if (s->r.top < inst->crop.top)
			s->r.top = inst->crop.top;

		if (s->r.width > inst->crop.width)
			s->r.width = inst->crop.width;

		if (s->r.height > inst->crop.height)
			s->r.height = inst->crop.height;
		inst->compose.left = s->r.left;
		inst->compose.top = s->r.top;
		inst->compose.width = s->r.width;
		inst->compose.height = s->r.height;

		output_fmt = inst->fmt_dst;
		ret = venc_s_fmt_output(inst, output_fmt);
		if (ret)
			return ret;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int venc_s_param(struct iris_inst *inst, struct v4l2_streamparm *s_parm)
{
	struct v4l2_fract *timeperframe = NULL;
	u32 q16_rate, max_rate, default_rate;
	u64 us_per_frame = 0, input_rate = 0;
	bool is_frame_rate = false;
	int ret = 0;

	if (s_parm->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		timeperframe = &s_parm->parm.output.timeperframe;
		max_rate = inst->cap[OPERATING_RATE].max >> 16;
		default_rate = inst->cap[OPERATING_RATE].value >> 16;
		s_parm->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	} else {
		timeperframe = &s_parm->parm.capture.timeperframe;
		is_frame_rate = true;
		max_rate = inst->cap[FRAME_RATE].max >> 16;
		default_rate = inst->cap[FRAME_RATE].value >> 16;
		s_parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	}

	if (!timeperframe->denominator || !timeperframe->numerator) {
		if (!timeperframe->numerator)
			timeperframe->numerator = 1;
		if (!timeperframe->denominator)
			timeperframe->denominator = default_rate;
	}

	us_per_frame = timeperframe->numerator * (u64)USEC_PER_SEC;
	us_per_frame = div64_u64(us_per_frame, timeperframe->denominator);

	if (!us_per_frame) {
		ret = -EINVAL;
		goto exit;
	}

	input_rate = (u64)USEC_PER_SEC;
	input_rate = div64_u64(input_rate, us_per_frame);

	q16_rate = (u32)input_rate << 16;
	if (is_frame_rate)
		inst->cap[FRAME_RATE].value = q16_rate;
	else
		inst->cap[OPERATING_RATE].value = q16_rate;

	if ((s_parm->type == INPUT_MPLANE && inst->vb2q_src->streaming) ||
	    (s_parm->type == OUTPUT_MPLANE && inst->vb2q_dst->streaming)) {
		ret = check_core_mbps_mbpf(inst);
		if (ret)
			goto reset_rate;
		ret = input_rate > max_rate;
		if (ret) {
			ret = -ENOMEM;
			goto reset_rate;
		}
	}

	if (is_frame_rate)
		inst->cap[FRAME_RATE].flags |= CAP_FLAG_CLIENT_SET;
	else
		inst->cap[OPERATING_RATE].flags |= CAP_FLAG_CLIENT_SET;

	if (inst->vb2q_dst->streaming) {
		ret = iris_hfi_set_property(inst,
					    HFI_PROP_FRAME_RATE,
					    HFI_HOST_FLAGS_NONE,
					    HFI_PORT_BITSTREAM,
					    HFI_PAYLOAD_Q16,
					    &q16_rate,
					    sizeof(u32));
		if (ret)
			goto exit;
	}

	return ret;

reset_rate:
	if (ret) {
		if (is_frame_rate)
			inst->cap[FRAME_RATE].value = default_rate << 16;
		else
			inst->cap[OPERATING_RATE].value = default_rate << 16;
	}
exit:
	return ret;
}

int venc_g_param(struct iris_inst *inst, struct v4l2_streamparm *s_parm)
{
	struct v4l2_fract *timeperframe = NULL;

	if (s_parm->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		timeperframe = &s_parm->parm.output.timeperframe;
		timeperframe->numerator = 1;
		timeperframe->denominator =
			inst->cap[OPERATING_RATE].value >> 16;
		s_parm->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	} else {
		timeperframe = &s_parm->parm.capture.timeperframe;
		timeperframe->numerator = 1;
		timeperframe->denominator =
			inst->cap[FRAME_RATE].value >> 16;
		s_parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	}

	return 0;
}

int venc_subscribe_event(struct iris_inst *inst,
			 const struct v4l2_event_subscription *sub)
{
	int ret;

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		ret = v4l2_event_subscribe(&inst->fh, sub, MAX_EVENTS, NULL);
		break;
	case V4L2_EVENT_CTRL:
		ret = v4l2_ctrl_subscribe_event(&inst->fh, sub);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

int venc_start_cmd(struct iris_inst *inst)
{
	vb2_clear_last_buffer_dequeued(inst->vb2q_dst);

	return process_resume(inst);
}

int venc_stop_cmd(struct iris_inst *inst)
{
	int ret;

	ret = iris_hfi_drain(inst, INPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_inst_change_sub_state(inst, 0, IRIS_INST_SUB_DRAIN);

	iris_scale_power(inst);

	return ret;
}

int venc_qbuf(struct iris_inst *inst, struct vb2_buffer *vb2)
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

	return queue_buffer(inst, buf);
}

static int check_scaling_supported(struct iris_inst *inst)
{
	u32 iwidth, owidth, iheight, oheight;

	if (!(inst->crop.left != inst->compose.left ||
	      inst->crop.top != inst->compose.top ||
	      inst->crop.width != inst->compose.width ||
	      inst->crop.height != inst->compose.height))
		return 0;

	iwidth = inst->crop.width;
	iheight = inst->crop.height;
	owidth = inst->compose.width;
	oheight = inst->compose.height;

	if (owidth > iwidth || oheight > iheight)
		return -EINVAL;

	if (iwidth > owidth * SCALE_FACTOR || iheight > oheight * SCALE_FACTOR)
		return -EINVAL;

	return 0;
}

static int venc_set_colorformat(struct iris_inst *inst)
{
	u32 hfi_colorformat;
	u32 pixelformat;

	pixelformat = inst->fmt_src->fmt.pix_mp.pixelformat;
	hfi_colorformat = get_hfi_colorformat(pixelformat);

	return iris_hfi_set_property(inst,
				     HFI_PROP_COLOR_FORMAT,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, INPUT_MPLANE),
				     HFI_PAYLOAD_U32_ENUM,
				     &hfi_colorformat,
				     sizeof(u32));
}

static int venc_set_stride_scanline(struct iris_inst *inst)
{
	u32 color_format, stride_y, scanline_y;
	u32 stride_uv = 0, scanline_uv = 0;
	u32 payload[2];

	color_format = inst->cap[PIX_FMTS].value;
	if (!is_linear_colorformat(color_format))
		return 0;

	stride_y = color_format == FMT_TP10C ?
		ALIGN(inst->fmt_src->fmt.pix_mp.width, 192) :
		ALIGN(inst->fmt_src->fmt.pix_mp.width, 128);
	scanline_y = color_format == FMT_TP10C ?
		ALIGN(inst->fmt_src->fmt.pix_mp.height, 16) :
		ALIGN(inst->fmt_src->fmt.pix_mp.height, 32);

	if (color_format == FMT_NV12) {
		stride_uv = stride_y;
		scanline_uv = scanline_y / 2;
	}

	payload[0] = stride_y << 16 | scanline_y;
	payload[1] = stride_uv << 16 | scanline_uv;

	return iris_hfi_set_property(inst,
				     HFI_PROP_LINEAR_STRIDE_SCANLINE,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, INPUT_MPLANE),
				     HFI_PAYLOAD_64_PACKED,
				     &payload,
				     sizeof(u64));
}

static int venc_set_raw_resolution(struct iris_inst *inst)
{
	u32 resolution;

	resolution = (inst->fmt_src->fmt.pix_mp.width << 16) |
		inst->fmt_src->fmt.pix_mp.height;

	return iris_hfi_set_property(inst,
				     HFI_PROP_RAW_RESOLUTION,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, INPUT_MPLANE),
				     HFI_PAYLOAD_32_PACKED,
				     &resolution,
				     sizeof(u32));
}

static int venc_set_bitstream_resolution(struct iris_inst *inst)
{
	u32 resolution;

	resolution = (inst->fmt_dst->fmt.pix_mp.width << 16) |
		inst->fmt_dst->fmt.pix_mp.height;

	return iris_hfi_set_property(inst,
				     HFI_PROP_BITSTREAM_RESOLUTION,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, OUTPUT_MPLANE),
				     HFI_PAYLOAD_32_PACKED,
				     &resolution,
				     sizeof(u32));
}

static int venc_set_inp_crop_offsets(struct iris_inst *inst)
{
	u32 left_offset, top_offset, right_offset, bottom_offset;
	u32 crop[2] = {0};
	u32 width, height;

	left_offset = inst->crop.left;
	top_offset = inst->crop.top;
	width = inst->crop.width;
	height = inst->crop.height;

	right_offset = (inst->fmt_src->fmt.pix_mp.width - width);
	bottom_offset = (inst->fmt_src->fmt.pix_mp.height - height);

	crop[0] = left_offset << 16 | top_offset;
	crop[1] = right_offset << 16 | bottom_offset;

	return iris_hfi_set_property(inst,
				     HFI_PROP_CROP_OFFSETS,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, INPUT_MPLANE),
				     HFI_PAYLOAD_64_PACKED,
				     &crop,
				     sizeof(u64));
}

static int venc_set_out_crop_offsets(struct iris_inst *inst)
{
	u32 left_offset, top_offset, right_offset, bottom_offset;
	u32 crop[2] = {0};
	u32 width, height;

	left_offset = inst->compose.left;
	top_offset = inst->compose.top;
	width = inst->compose.width;
	height = inst->compose.height;
	if (inst->cap[ROTATION].value == 90 || inst->cap[ROTATION].value == 270) {
		width = inst->compose.height;
		height = inst->compose.width;
	}

	right_offset = (inst->fmt_dst->fmt.pix_mp.width - width);
	bottom_offset = (inst->fmt_dst->fmt.pix_mp.height - height);

	crop[0] = left_offset << 16 | top_offset;
	crop[1] = right_offset << 16 | bottom_offset;

	return iris_hfi_set_property(inst,
				     HFI_PROP_CROP_OFFSETS,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, OUTPUT_MPLANE),
				     HFI_PAYLOAD_64_PACKED,
				     &crop,
				     sizeof(u64));
}

static int venc_set_colorspace(struct iris_inst *inst)
{
	u32 video_signal_type_present_flag = 0, payload = 0;
	u32 matrix_coeff = HFI_MATRIX_COEFF_RESERVED;
	u32 video_format = UNSPECIFIED_COLOR_FORMAT;
	struct v4l2_pix_format_mplane *pixmp = NULL;
	u32 transfer_char = HFI_TRANSFER_RESERVED;
	u32 colour_description_present_flag = 0;
	u32 primaries = HFI_PRIMARIES_RESERVED;
	u32 full_range = 0;

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

	payload = (matrix_coeff & 0xFF) |
		((transfer_char << 8) & 0xFF00) |
		((primaries << 16) & 0xFF0000) |
		((colour_description_present_flag << 24) & 0x1000000) |
		((full_range << 25) & 0x2000000) |
		((video_format << 26) & 0x1C000000) |
		((video_signal_type_present_flag << 29) & 0x20000000);

	return iris_hfi_set_property(inst,
				     HFI_PROP_SIGNAL_COLOR_INFO,
				     HFI_HOST_FLAGS_NONE,
				     get_hfi_port(inst, INPUT_MPLANE),
				     HFI_PAYLOAD_32_PACKED,
				     &payload,
				     sizeof(u32));
}

static int venc_set_quality_mode(struct iris_inst *inst)
{
	u32 mode;

	mode = decide_quality_mode(inst);
	return iris_hfi_set_property(inst,
				     HFI_PROP_QUALITY_MODE,
				     HFI_HOST_FLAGS_NONE,
				     HFI_PORT_BITSTREAM,
				     HFI_PAYLOAD_U32_ENUM,
				     &mode,
				     sizeof(u32));
}

static int venc_set_input_properties(struct iris_inst *inst)
{
	int j, ret;
	static const struct venc_prop_type_handle prop_type_handle_arr[] = {
		{HFI_PROP_COLOR_FORMAT,               venc_set_colorformat                 },
		{HFI_PROP_RAW_RESOLUTION,             venc_set_raw_resolution              },
		{HFI_PROP_CROP_OFFSETS,               venc_set_inp_crop_offsets            },
		{HFI_PROP_LINEAR_STRIDE_SCANLINE,     venc_set_stride_scanline             },
		{HFI_PROP_SIGNAL_COLOR_INFO,          venc_set_colorspace                  },
	};

	for (j = 0; j < ARRAY_SIZE(prop_type_handle_arr); j++) {
		ret = prop_type_handle_arr[j].handle(inst);
		if (ret)
			return ret;
	}

	return ret;
}

static int venc_property_subscription(struct iris_inst *inst, u32 plane)
{
	u32 payload[32] = {0};
	u32 payload_size = 0;
	u32 i;

	payload[0] = HFI_MODE_PROPERTY;
	if (plane == INPUT_MPLANE) {
		for (i = 0; i < ARRAY_SIZE(enc_input_properties); i++)
			payload[i + 1] = enc_input_properties[i];
		payload_size = (ARRAY_SIZE(enc_input_properties) + 1) *
				sizeof(u32);
	} else if (plane == OUTPUT_MPLANE) {
		for (i = 0; i < ARRAY_SIZE(enc_output_properties); i++)
			payload[i + 1] = enc_output_properties[i];
		payload_size = (ARRAY_SIZE(enc_output_properties) + 1) *
				sizeof(u32);
	} else {
		return -EINVAL;
	}

	return iris_hfi_session_subscribe_mode(inst,
					       HFI_CMD_SUBSCRIBE_MODE,
					       plane,
					       HFI_PAYLOAD_U32_ARRAY,
					       &payload[0],
					       payload_size);
}

int venc_streamon_input(struct iris_inst *inst)
{
	int ret;

	ret = check_session_supported(inst);
	if (ret)
		goto error;

	ret = check_scaling_supported(inst);
	if (ret)
		goto error;

	ret = venc_set_input_properties(inst);
	if (ret)
		goto error;

	ret = iris_get_internal_buffers(inst, INPUT_MPLANE);
	if (ret)
		goto error;

	ret = iris_destroy_internal_buffers(inst, INPUT_MPLANE);
	if (ret)
		goto error;

	ret = iris_create_input_internal_buffers(inst);
	if (ret)
		goto error;

	ret = iris_queue_input_internal_buffers(inst);
	if (ret)
		goto error;

	ret = venc_property_subscription(inst, INPUT_MPLANE);
	if (ret)
		goto error;

	ret = process_streamon_input(inst);
	if (ret)
		goto error;

	return ret;

error:
	return ret;
}

static int venc_set_output_properties(struct iris_inst *inst)
{
	int j, ret;
	static const struct venc_prop_type_handle prop_type_handle_arr[] = {
		{HFI_PROP_BITSTREAM_RESOLUTION,       venc_set_bitstream_resolution    },
		{HFI_PROP_CROP_OFFSETS,               venc_set_out_crop_offsets        },
	};

	for (j = 0; j < ARRAY_SIZE(prop_type_handle_arr); j++) {
		ret = prop_type_handle_arr[j].handle(inst);
		if (ret)
			return ret;
	}

	return ret;
}

int venc_streamon_output(struct iris_inst *inst)
{
	int ret;

	ret = venc_set_output_properties(inst);
	if (ret)
		goto error;

	ret = set_v4l2_properties(inst);
	if (ret)
		goto error;

	ret = venc_set_quality_mode(inst);
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

	ret = iris_queue_output_internal_buffers(inst);
	if (ret)
		goto error;

	ret = venc_property_subscription(inst, OUTPUT_MPLANE);
	if (ret)
		goto error;

	ret = process_streamon_output(inst);
	if (ret)
		goto error;

	return ret;

error:
	session_streamoff(inst, OUTPUT_MPLANE);

	return ret;
}
