// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_instance.h"
#include "vpu_iris3_buffer.h"

static u32 dec_bin_size_iris3(struct iris_inst *inst)
{
	u32 width, height, num_vpp_pipes;
	struct iris_core *core;
	struct v4l2_format *f;
	u32 size = 0;

	core = inst->core;

	num_vpp_pipes = core->cap[NUM_VPP_PIPE].value;

	f = inst->fmt_src;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	if (inst->codec == H264)
		size = hfi_buffer_bin_h264d(width, height, num_vpp_pipes);
	else if (inst->codec == HEVC)
		size = hfi_buffer_bin_h265d(width, height, num_vpp_pipes);
	else if (inst->codec == VP9)
		size = hfi_buffer_bin_vp9d(width, height,
					   num_vpp_pipes);
	return size;
}

static u32 dec_comv_size_iris3(struct iris_inst *inst)
{
	u32 width, height, num_comv;
	struct v4l2_format *f;
	u32 size = 0;

	f = inst->fmt_src;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	num_comv = inst->buffers.output.min_count;

	if (inst->codec == H264)
		size = hfi_buffer_comv_h264d(width, height, num_comv);
	else if (inst->codec == HEVC)
		size = hfi_buffer_comv_h265d(width, height, num_comv);

	inst->cap[NUM_COMV].value = num_comv;

	return size;
}

static u32 dec_non_comv_size_iris3(struct iris_inst *inst)
{
	u32 width, height, num_vpp_pipes;
	struct iris_core *core;
	struct v4l2_format *f;
	u32 size = 0;

	core = inst->core;

	num_vpp_pipes = core->cap[NUM_VPP_PIPE].value;

	f = inst->fmt_src;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	if (inst->codec == H264)
		size = hfi_buffer_non_comv_h264d(width, height, num_vpp_pipes);
	else if (inst->codec == HEVC)
		size = hfi_buffer_non_comv_h265d(width, height, num_vpp_pipes);

	return size;
}

static u32 dec_line_size_iris3(struct iris_inst *inst)
{
	u32 width, height, out_min_count, num_vpp_pipes;
	struct iris_core *core;
	struct v4l2_format *f;
	u32 size = 0;
	bool is_opb;

	core = inst->core;
	num_vpp_pipes = core->cap[NUM_VPP_PIPE].value;

	is_opb = true;

	f = inst->fmt_src;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;
	out_min_count = inst->buffers.output.min_count;
	if (inst->codec == H264)
		size = hfi_buffer_line_h264d(width, height, is_opb,
					     num_vpp_pipes);
	else if (inst->codec == HEVC)
		size = hfi_buffer_line_h265d(width, height, is_opb,
					     num_vpp_pipes);
	else if (inst->codec == VP9)
		size = hfi_buffer_line_vp9d(width, height, out_min_count,
					    is_opb, num_vpp_pipes);
	return size;
}

static u32 dec_persist_size_iris3(struct iris_inst *inst)
{
	u32 size = 0;

	if (inst->codec == H264)
		size = hfi_buffer_persist_h264d(0);
	else if (inst->codec == HEVC)
		size = hfi_buffer_persist_h265d(0);
	else if (inst->codec == VP9)
		size = hfi_buffer_persist_vp9d();

	return size;
}

static u32 dec_dpb_size_iris3(struct iris_inst *inst)
{
	struct v4l2_format *f;
	u32 width, height;
	u32 color_fmt;
	u32 size = 0;

	f = inst->fmt_dst;
	color_fmt = f->fmt.pix_mp.pixelformat;
	if (!is_linear_colorformat(color_fmt))
		return size;

	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	if (color_fmt == V4L2_PIX_FMT_NV12 ||
	    color_fmt == V4L2_PIX_FMT_QC08C) {
		size =
		hfi_nv12_ubwc_il_calc_buf_size_v2(width, height,
						  ALIGN(width, 128),
						  ALIGN(height, 32),
						  ALIGN(width, 128),
						  ALIGN((height + 1) >> 1, 32),
						  ALIGN(DIV_ROUND_UP(width, 32), 64),
						  ALIGN(DIV_ROUND_UP(height, 8), 16),
						  ALIGN(DIV_ROUND_UP((width + 1) >> 1, 16), 64),
						  ALIGN(DIV_ROUND_UP((height + 1) >> 1, 8), 16));
	} else if (color_fmt == V4L2_PIX_FMT_QC10C) {
		size =
		hfi_yuv420_tp10_ubwc_calc_buf_size(ALIGN(ALIGN(width, 192) * 4 / 3, 256),
						   ALIGN(height, 16),
						   ALIGN(ALIGN(width, 192) * 4 / 3, 256),
						   ALIGN((height + 1) >> 1, 16),
						   ALIGN(DIV_ROUND_UP(width, 48), 64),
						   ALIGN(DIV_ROUND_UP(height, 4), 16),
						   ALIGN(DIV_ROUND_UP((width + 1) >> 1, 24), 64),
						   ALIGN(DIV_ROUND_UP((height + 1) >> 1, 4), 16));
	}

	return size;
}

static u32 enc_bin_size_iris3(struct iris_inst *inst)
{
	u32 width, height, num_vpp_pipes, stage, profile;
	struct iris_core *core;
	struct v4l2_format *f;
	u32 size = 0;

	core = inst->core;

	num_vpp_pipes = core->cap[NUM_VPP_PIPE].value;
	stage = inst->cap[STAGE].value;
	f = inst->fmt_dst;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;
	profile = inst->cap[PROFILE].value;

	if (inst->codec == H264)
		size = hfi_buffer_bin_h264e(inst->hfi_rc_type, width, height,
					    stage, num_vpp_pipes, profile);
	else if (inst->codec == HEVC)
		size = hfi_buffer_bin_h265e(inst->hfi_rc_type, width, height,
					    stage, num_vpp_pipes, profile);

	return size;
}

u32 get_recon_buf_count(struct iris_inst *inst)
{
	s32 n_bframe, ltr_count, hp_layers = 0, hb_layers = 0;
	bool is_hybrid_hp = false;
	u32 num_buf_recon = 0;
	u32 hfi_codec = 0;

	n_bframe = inst->cap[B_FRAME].value;
	ltr_count = inst->cap[LTR_COUNT].value;

	if (inst->hfi_layer_type == HFI_HIER_B) {
		hb_layers = inst->cap[ENH_LAYER_COUNT].value + 1;
	} else {
		hp_layers = inst->cap[ENH_LAYER_COUNT].value + 1;
		if (inst->hfi_layer_type == HFI_HIER_P_HYBRID_LTR)
			is_hybrid_hp = true;
	}

	if (inst->codec == H264)
		hfi_codec = HFI_CODEC_ENCODE_AVC;
	else if (inst->codec == HEVC)
		hfi_codec = HFI_CODEC_ENCODE_HEVC;

	num_buf_recon = hfi_iris3_enc_recon_buf_count(n_bframe, ltr_count,
						      hp_layers, hb_layers,
						      is_hybrid_hp, hfi_codec);

	return num_buf_recon;
}

static u32 enc_comv_size_iris3(struct iris_inst *inst)
{
	u32 width, height, num_recon = 0;
	struct v4l2_format *f;
	u32 size = 0;

	f = inst->fmt_dst;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	num_recon = get_recon_buf_count(inst);
	if (inst->codec == H264)
		size = hfi_buffer_comv_h264e(width, height, num_recon);
	else if (inst->codec == HEVC)
		size = hfi_buffer_comv_h265e(width, height, num_recon);

	return size;
}

static u32 enc_non_comv_size_iris3(struct iris_inst *inst)
{
	u32 width, height, num_vpp_pipes;
	struct iris_core *core;
	struct v4l2_format *f;
	u32 size = 0;

	core = inst->core;

	num_vpp_pipes = core->cap[NUM_VPP_PIPE].value;
	f = inst->fmt_dst;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	if (inst->codec == H264)
		size = hfi_buffer_non_comv_h264e(width, height, num_vpp_pipes);
	else if (inst->codec == HEVC)
		size = hfi_buffer_non_comv_h265e(width, height, num_vpp_pipes);

	return size;
}

static u32 enc_line_size_iris3(struct iris_inst *inst)
{
	u32 width, height, pixfmt, num_vpp_pipes;
	struct iris_core *core;
	bool is_tenbit = false;
	struct v4l2_format *f;
	u32 size = 0;

	core = inst->core;
	num_vpp_pipes = core->cap[NUM_VPP_PIPE].value;
	pixfmt = inst->cap[PIX_FMTS].value;

	f = inst->fmt_dst;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;
	is_tenbit = (pixfmt == FMT_TP10C);

	if (inst->codec == H264)
		size = hfi_buffer_line_h264e(width, height, is_tenbit, num_vpp_pipes);
	else if (inst->codec == HEVC)
		size = hfi_buffer_line_h265e(width, height, is_tenbit, num_vpp_pipes);

	return size;
}

static u32 enc_dpb_size_iris3(struct iris_inst *inst)
{
	u32 width, height, pixfmt;
	struct v4l2_format *f;
	bool is_tenbit;
	u32 size = 0;

	f = inst->fmt_dst;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	pixfmt = inst->cap[PIX_FMTS].value;
	is_tenbit = (pixfmt == FMT_TP10C);

	if (inst->codec == H264)
		size = hfi_buffer_dpb_h264e(width, height);
	else if (inst->codec == HEVC)
		size = hfi_buffer_dpb_h265e(width, height, is_tenbit);

	return size;
}

static u32 enc_arp_size_iris3(struct iris_inst *inst)
{
	u32 size = 0;

	HFI_BUFFER_ARP_ENC(size);

	return size;
}

static u32 enc_vpss_size_iris3(struct iris_inst *inst)
{
	bool ds_enable = false, is_tenbit = false;
	struct v4l2_format *f;
	u32 width, height;
	u32 size = 0;

	ds_enable = is_scaling_enabled(inst);

	f = inst->fmt_dst;
	if (inst->cap[ROTATION].value == 90 ||
	    inst->cap[ROTATION].value == 270) {
		width = f->fmt.pix_mp.height;
		height = f->fmt.pix_mp.width;
	} else {
		width = f->fmt.pix_mp.width;
		height = f->fmt.pix_mp.height;
	}

	f = inst->fmt_src;
	is_tenbit = is_10bit_colorformat(f->fmt.pix_mp.pixelformat);

	size = hfi_buffer_vpss_enc(width, height, ds_enable, 0, is_tenbit);

	return size;
}

u32 enc_output_buffer_size_iris3(struct iris_inst *inst)
{
	u32 hfi_rc_type = HFI_RC_VBR_CFR;
	enum codec_type codec;
	int bitrate_mode, frame_rc;
	bool is_ten_bit = false;
	struct v4l2_format *f;
	u32 frame_size;

	f = inst->fmt_dst;
	codec = v4l2_codec_to_driver(inst, f->fmt.pix_mp.pixelformat);
	if (codec == HEVC)
		is_ten_bit = true;

	bitrate_mode = inst->cap[BITRATE_MODE].value;
	frame_rc = inst->cap[FRAME_RC_ENABLE].value;
	if (!frame_rc)
		hfi_rc_type = HFI_RC_OFF;
	else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CQ)
		hfi_rc_type = HFI_RC_CQ;

	frame_size = hfi_buffer_bitstream_enc(f->fmt.pix_mp.width,
					      f->fmt.pix_mp.height,
					      hfi_rc_type, is_ten_bit);

	return frame_size;
}

struct iris_buf_type_handle {
	enum iris_buffer_type type;
	u32 (*handle)(struct iris_inst *inst);
};

int iris_int_buf_size_iris3(struct iris_inst *inst,
			    enum iris_buffer_type buffer_type)
{
	const struct iris_buf_type_handle *buf_type_handle_arr = NULL;
	u32 size = 0, buf_type_handle_size = 0;
	int i;

	static const struct iris_buf_type_handle dec_internal_buf_type_handle[] = {
		{BUF_BIN,             dec_bin_size_iris3          },
		{BUF_COMV,            dec_comv_size_iris3         },
		{BUF_NON_COMV,        dec_non_comv_size_iris3     },
		{BUF_LINE,            dec_line_size_iris3         },
		{BUF_PERSIST,         dec_persist_size_iris3      },
		{BUF_DPB,             dec_dpb_size_iris3          },
	};
	static const struct iris_buf_type_handle enc_internal_buf_type_handle[] = {
		{BUF_BIN,             enc_bin_size_iris3          },
		{BUF_COMV,            enc_comv_size_iris3         },
		{BUF_NON_COMV,        enc_non_comv_size_iris3     },
		{BUF_LINE,            enc_line_size_iris3         },
		{BUF_DPB,             enc_dpb_size_iris3          },
		{BUF_ARP,             enc_arp_size_iris3          },
		{BUF_VPSS,            enc_vpss_size_iris3         },
	};

	if (inst->domain == DECODER) {
		buf_type_handle_size = ARRAY_SIZE(dec_internal_buf_type_handle);
		buf_type_handle_arr = dec_internal_buf_type_handle;
	} else if (inst->domain == ENCODER) {
		buf_type_handle_size = ARRAY_SIZE(enc_internal_buf_type_handle);
		buf_type_handle_arr = enc_internal_buf_type_handle;
	}

	if (!buf_type_handle_arr || !buf_type_handle_size)
		return size;

	for (i = 0; i < buf_type_handle_size; i++) {
		if (buf_type_handle_arr[i].type == buffer_type) {
			size = buf_type_handle_arr[i].handle(inst);
			break;
		}
	}

	return size;
}
