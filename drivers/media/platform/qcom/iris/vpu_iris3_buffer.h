/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _VPU_IRIS3_BUFFER_H_
#define _VPU_IRIS3_BUFFER_H_

#include <linux/types.h>
#include <linux/minmax.h>
#include <linux/align.h>

#include "iris_instance.h"

#define DMA_ALIGNMENT 256

#define BUFFER_ALIGNMENT_4096_BYTES 4096
#define BUFFER_ALIGNMENT_512_BYTES 512
#define BUFFER_ALIGNMENT_256_BYTES 256
#define BUFFER_ALIGNMENT_128_BYTES 128
#define BUFFER_ALIGNMENT_64_BYTES 64
#define BUFFER_ALIGNMENT_32_BYTES 32
#define BUFFER_ALIGNMENT_16_BYTES 16

#define HFI_ALIGNMENT_4096 (4096)

#define HFI_COL_FMT_NV12C_Y_TILE_HEIGHT (8)
#define HFI_COL_FMT_NV12C_Y_TILE_WIDTH (32)
#define HFI_COL_FMT_NV12C_UV_TILE_HEIGHT (8)
#define HFI_COL_FMT_NV12C_UV_TILE_WIDTH (16)
#define HFI_COL_FMT_TP10C_Y_TILE_HEIGHT (4)
#define HFI_COL_FMT_TP10C_Y_TILE_WIDTH (48)

#define NUM_HW_PIC_BUF 32
#define SIZE_HW_PIC(size_per_buf) (NUM_HW_PIC_BUF * (size_per_buf))

#define MAX_TILE_COLUMNS 32

#define LCU_MAX_SIZE_PELS 64
#define LCU_MIN_SIZE_PELS 16

#define HDR10_HIST_EXTRADATA_SIZE (4 * 1024)

#define BIN_BUFFER_THRESHOLD (1280 * 736)

#define VPP_CMD_MAX_SIZE (BIT(20))

#define H264D_MAX_SLICE 1800

#define SIZE_H264D_BUFTAB_T (256)
#define SIZE_H264D_HW_PIC_T (BIT(11))
#define SIZE_H264D_BSE_CMD_PER_BUF (32 * 4)
#define SIZE_H264D_VPP_CMD_PER_BUF (512)

#define NUM_SLIST_BUF_H264 (256 + 32)
#define SIZE_SLIST_BUF_H264 (512)
#define H264_DISPLAY_BUF_SIZE (3328)
#define H264_NUM_FRM_INFO (66)

#define H265_NUM_TILE_COL 32
#define H265_NUM_TILE_ROW 128
#define H265_NUM_TILE (H265_NUM_TILE_ROW * H265_NUM_TILE_COL + 1)
#define SIZE_H265D_BSE_CMD_PER_BUF (16 * sizeof(u32))

#define NUM_SLIST_BUF_H265 (80 + 20)
#define SIZE_SLIST_BUF_H265 (BIT(10))
#define H265_DISPLAY_BUF_SIZE (3072)
#define H265_NUM_FRM_INFO (48)

#define VP9_NUM_FRAME_INFO_BUF 32
#define VP9_NUM_PROBABILITY_TABLE_BUF (VP9_NUM_FRAME_INFO_BUF + 4)
#define VP9_PROB_TABLE_SIZE (3840)
#define VP9_FRAME_INFO_BUF_SIZE (6144)

#define VP9_UDC_HEADER_BUF_SIZE (3 * 128)
#define MAX_SUPERFRAME_HEADER_LEN (34)
#define CCE_TILE_OFFSET_SIZE ALIGN(32 * 4 * 4, BUFFER_ALIGNMENT_32_BYTES)

#define SIZE_SEI_USERDATA (4096)
#define SIZE_DOLBY_RPU_METADATA (41 * 1024)

#define H264_CABAC_HDR_RATIO_HD_TOT 1
#define H264_CABAC_RES_RATIO_HD_TOT 3

#define H265D_MAX_SLICE 1200
#define SIZE_H265D_HW_PIC_T SIZE_H264D_HW_PIC_T
#define H265_CABAC_HDR_RATIO_HD_TOT 2
#define H265_CABAC_RES_RATIO_HD_TOT 2
#define SIZE_H265D_VPP_CMD_PER_BUF (256)

#define VPX_DECODER_FRAME_CONCURENCY_LVL (2)
#define VPX_DECODER_FRAME_BIN_HDR_BUDGET 1
#define VPX_DECODER_FRAME_BIN_RES_BUDGET 3
#define VPX_DECODER_FRAME_BIN_DENOMINATOR 2

#define VPX_DECODER_FRAME_BIN_RES_BUDGET_RATIO (3 / 2)

#define MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE 64
#define MAX_FE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE 64
#define MAX_FE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE 64

#define MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE (128 / 8)
#define MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE (128 / 8)
#define MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE (128 / 8)

#define MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE (64 * 2 * 3)
#define MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE 640

#define HFI_BUFFER_ARP_ENC(size) (size = 204800)

#define HFI_MAX_COL_FRAME 6
#define HFI_VENUS_WIDTH_ALIGNMENT 128
#define HFI_VENUS_HEIGHT_ALIGNMENT 32
#define VENUS_METADATA_STRIDE_MULTIPLE 64
#define VENUS_METADATA_HEIGHT_MULTIPLE 16

#ifndef SYSTEM_LAL_TILE10
#define SYSTEM_LAL_TILE10 192
#endif

#define SIZE_SLICE_CMD_BUFFER (ALIGN(20480, BUFFER_ALIGNMENT_256_BYTES))
#define SIZE_SPS_PPS_SLICE_HDR (2048 + 4096)

#define SIZE_BSE_SLICE_CMD_BUF ((((8192 << 2) + 7) & (~7)) * 3)
#define SIZE_LAMBDA_LUT (256 * 11)

#define HFI_WORKMODE_1 1
#define HFI_WORKMODE_2 2

static inline
u32 size_h264d_hw_bin_buffer(u32 frame_width, u32 frame_height,
			     u32 num_vpp_pipes)
{
	u32 size_yuv, size_bin_hdr, size_bin_res;

	size_yuv = ((frame_width * frame_height) <= BIN_BUFFER_THRESHOLD) ?
			((BIN_BUFFER_THRESHOLD * 3) >> 1) :
			((frame_width * frame_height * 3) >> 1);
	size_bin_hdr = size_yuv * H264_CABAC_HDR_RATIO_HD_TOT;
	size_bin_res = size_yuv * H264_CABAC_RES_RATIO_HD_TOT;
	size_bin_hdr = ALIGN(size_bin_hdr / num_vpp_pipes,
			     DMA_ALIGNMENT) * num_vpp_pipes;
	size_bin_res = ALIGN(size_bin_res / num_vpp_pipes,
			     DMA_ALIGNMENT) * num_vpp_pipes;

	return size_bin_hdr + size_bin_res;
}

static inline
u32 hfi_buffer_bin_h264d(u32 frame_width, u32 frame_height,
			 u32 num_vpp_pipes)
{
	u32 n_aligned_w, n_aligned_h;

	n_aligned_w =
		ALIGN(frame_width, BUFFER_ALIGNMENT_16_BYTES);
	n_aligned_h =
		ALIGN(frame_height, BUFFER_ALIGNMENT_16_BYTES);

	return size_h264d_hw_bin_buffer(n_aligned_w, n_aligned_h,
					num_vpp_pipes);
}

static inline
u32 size_h265d_hw_bin_buffer(u32 frame_width, u32 frame_height,
			     u32 num_vpp_pipes)
{
	u32 size_yuv, size_bin_hdr, size_bin_res;

	size_yuv = ((frame_width * frame_height) <= BIN_BUFFER_THRESHOLD) ?
		((BIN_BUFFER_THRESHOLD * 3) >> 1) :
		((frame_width * frame_height * 3) >> 1);
	size_bin_hdr = size_yuv * H265_CABAC_HDR_RATIO_HD_TOT;
	size_bin_res = size_yuv * H265_CABAC_RES_RATIO_HD_TOT;
	size_bin_hdr = ALIGN(size_bin_hdr / num_vpp_pipes, DMA_ALIGNMENT) *
			num_vpp_pipes;
	size_bin_res = ALIGN(size_bin_res / num_vpp_pipes, DMA_ALIGNMENT) *
			num_vpp_pipes;

	return size_bin_hdr + size_bin_res;
}

static inline
u32 hfi_buffer_bin_h265d(u32 frame_width, u32 frame_height,
			 u32 num_vpp_pipes)
{
	u32 n_aligned_w, n_aligned_h;

	n_aligned_w = ALIGN(frame_width, BUFFER_ALIGNMENT_16_BYTES);
	n_aligned_h = ALIGN(frame_height, BUFFER_ALIGNMENT_16_BYTES);
	return size_h265d_hw_bin_buffer(n_aligned_w, n_aligned_h,
					num_vpp_pipes);
}

static inline
u32 hfi_buffer_bin_vp9d(u32 frame_width, u32 frame_height,
			u32 num_vpp_pipes)
{
	u32 _size_yuv, _size;

	_size_yuv = ALIGN(frame_width, BUFFER_ALIGNMENT_16_BYTES) *
		ALIGN(frame_height, BUFFER_ALIGNMENT_16_BYTES) * 3 / 2;
	_size = ALIGN(((max_t(u32, _size_yuv, ((BIN_BUFFER_THRESHOLD * 3) >> 1)) *
			VPX_DECODER_FRAME_BIN_HDR_BUDGET / VPX_DECODER_FRAME_BIN_DENOMINATOR *
			VPX_DECODER_FRAME_CONCURENCY_LVL) / num_vpp_pipes),
		      DMA_ALIGNMENT) +
		ALIGN(((max_t(u32, _size_yuv, ((BIN_BUFFER_THRESHOLD * 3) >> 1)) *
			VPX_DECODER_FRAME_BIN_RES_BUDGET / VPX_DECODER_FRAME_BIN_DENOMINATOR *
			VPX_DECODER_FRAME_CONCURENCY_LVL) / num_vpp_pipes),
		      DMA_ALIGNMENT);

	return _size * num_vpp_pipes;
}

static inline
u32 hfi_buffer_comv_h264d(u32 frame_width, u32 frame_height,
			  u32 _comv_bufcount)
{
	u32 frame_width_in_mbs = ((frame_width + 15) >> 4);
	u32 frame_height_in_mbs = ((frame_height + 15) >> 4);
	u32 col_mv_aligned_width = (frame_width_in_mbs << 7);
	u32 col_zero_aligned_width = (frame_width_in_mbs << 2);
	u32 col_zero_size = 0, size_colloc = 0;

	col_mv_aligned_width =
		ALIGN(col_mv_aligned_width, BUFFER_ALIGNMENT_16_BYTES);
	col_zero_aligned_width =
		ALIGN(col_zero_aligned_width, BUFFER_ALIGNMENT_16_BYTES);
	col_zero_size = col_zero_aligned_width *
			((frame_height_in_mbs + 1) >> 1);
	col_zero_size =
		ALIGN(col_zero_size, BUFFER_ALIGNMENT_64_BYTES);
	col_zero_size <<= 1;
	col_zero_size =
		ALIGN(col_zero_size, BUFFER_ALIGNMENT_512_BYTES);
	size_colloc = col_mv_aligned_width *
			((frame_height_in_mbs + 1) >> 1);
	size_colloc =
		ALIGN(size_colloc, BUFFER_ALIGNMENT_64_BYTES);
	size_colloc <<= 1;
	size_colloc =
		ALIGN(size_colloc, BUFFER_ALIGNMENT_512_BYTES);
	size_colloc += (col_zero_size + SIZE_H264D_BUFTAB_T * 2);

	return (size_colloc * (_comv_bufcount)) +
		BUFFER_ALIGNMENT_512_BYTES;
}

static inline
u32 hfi_buffer_comv_h265d(u32 frame_width, u32 frame_height,
			  u32 _comv_bufcount)
{
	u32 _size;

	_size = ALIGN(((((frame_width + 15) >> 4) *
		       ((frame_height + 15) >> 4)) << 8),
		     BUFFER_ALIGNMENT_512_BYTES);
	_size *= _comv_bufcount;
	_size += BUFFER_ALIGNMENT_512_BYTES;

	return _size;
}

static inline
u32 size_h264d_bse_cmd_buf(u32 frame_width, u32 frame_height)
{
	u32 _height = ALIGN(frame_height,
			    BUFFER_ALIGNMENT_32_BYTES);
	return min_t(u32, (((_height + 15) >> 4) * 48), H264D_MAX_SLICE) *
		SIZE_H264D_BSE_CMD_PER_BUF;
}

static inline
u32 size_h264d_vpp_cmd_buf(u32 frame_width, u32 frame_height)
{
	u32 _size, _height;

	_height = ALIGN(frame_height, BUFFER_ALIGNMENT_32_BYTES);
	_size = min_t(u32, (((_height + 15) >> 4) * 48), H264D_MAX_SLICE) *
			SIZE_H264D_VPP_CMD_PER_BUF;

	if (_size > VPP_CMD_MAX_SIZE)
		_size = VPP_CMD_MAX_SIZE;

	return _size;
}

static inline
u32 hfi_buffer_non_comv_h264d(u32 frame_width, u32 frame_height,
			      u32 num_vpp_pipes)
{
	u32 _size_bse, _size_vpp, _size;

	_size_bse = size_h264d_bse_cmd_buf(frame_width, frame_height);
	_size_vpp = size_h264d_vpp_cmd_buf(frame_width, frame_height);
	_size = ALIGN(_size_bse, DMA_ALIGNMENT) +
		ALIGN(_size_vpp, DMA_ALIGNMENT) +
		ALIGN(SIZE_HW_PIC(SIZE_H264D_HW_PIC_T), DMA_ALIGNMENT);

	return ALIGN(_size, DMA_ALIGNMENT);
}

static inline
u32 size_h265d_bse_cmd_buf(u32 frame_width, u32 frame_height)
{
	u32 _size;

	_size = ALIGN(((ALIGN(frame_width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
		      (ALIGN(frame_height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS)) *
		     NUM_HW_PIC_BUF, DMA_ALIGNMENT);
	_size = min_t(u32, _size, H265D_MAX_SLICE + 1);
	_size = 2 * _size * SIZE_H265D_BSE_CMD_PER_BUF;

	return _size;
}

static inline
u32 size_h265d_vpp_cmd_buf(u32 frame_width, u32 frame_height)
{
	u32 _size;

	_size = ALIGN(((ALIGN(frame_width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
		      (ALIGN(frame_height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS)) *
		     NUM_HW_PIC_BUF, DMA_ALIGNMENT);
	_size = min_t(u32, _size, H265D_MAX_SLICE + 1);
	_size = ALIGN(_size, 4);
	_size = 2 * _size * SIZE_H265D_VPP_CMD_PER_BUF;
	if (_size > VPP_CMD_MAX_SIZE)
		_size = VPP_CMD_MAX_SIZE;

	return _size;
}

static inline
u32 hfi_buffer_non_comv_h265d(u32 frame_width, u32 frame_height,
			      u32 num_vpp_pipes)
{
	u32 _size_bse, _size_vpp, _size;

	_size_bse = size_h265d_bse_cmd_buf(frame_width, frame_height);
	_size_vpp = size_h265d_vpp_cmd_buf(frame_width, frame_height);
	_size = ALIGN(_size_bse, DMA_ALIGNMENT) +
		ALIGN(_size_vpp, DMA_ALIGNMENT) +
		ALIGN(NUM_HW_PIC_BUF * 20 * 22 * 4, DMA_ALIGNMENT) +
		ALIGN(2 * sizeof(u16) *
		      (ALIGN(frame_width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
		      (ALIGN(frame_height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS),
		      DMA_ALIGNMENT) +
		ALIGN(SIZE_HW_PIC(SIZE_H265D_HW_PIC_T),
		      DMA_ALIGNMENT) +
		HDR10_HIST_EXTRADATA_SIZE;

	return ALIGN(_size, DMA_ALIGNMENT);
}

static inline u32 size_h264d_lb_fe_top_data(u32 frame_width, u32 frame_height)
{
	return MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE * ALIGN(frame_width, 16) * 3;
}

static inline u32 size_h264d_lb_fe_top_ctrl(u32 frame_width, u32 frame_height)
{
	return MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * ((frame_width + 15) >> 4);
}

static inline u32 size_h264d_lb_fe_left_ctrl(u32 frame_width, u32 frame_height)
{
	return MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * ((frame_height + 15) >> 4);
}

static inline u32 size_h264d_lb_se_top_ctrl(u32 frame_width, u32 frame_height)
{
	return MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * ((frame_width + 15) >> 4);
}

static inline u32 size_h264d_lb_se_left_ctrl(u32 frame_width, u32 frame_height)
{
	return MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * ((frame_height + 15) >> 4);
}

static inline u32 size_h264d_lb_pe_top_data(u32 frame_width, u32 frame_height)
{
	return MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE *  ((frame_width + 15) >> 4);
}

static inline u32 size_h264d_lb_vsp_top(u32 frame_width, u32 frame_height)
{
	return (((frame_width + 15) >> 4) << 7);
}

static inline u32 size_h264d_lb_recon_dma_metadata_wr(u32 frame_width, u32 frame_height)
{
	return ALIGN(frame_height, 16) * 32;
}

static inline u32 size_h264d_qp(u32 frame_width, u32 frame_height)
{
	return ((frame_width + 63) >> 6) * ((frame_height + 63) >> 6) * 128;
}

static inline
u32 size_vpss_lb(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 vpss_4tap_left_buffer_size = 0, vpss_div2_left_buffer_size = 0;
	u32 vpss_4tap_top_buffer_size = 0, vpss_div2_top_buffer_size = 0;
	u32 opb_lb_wr_llb_y_buffer_size, opb_lb_wr_llb_uv_buffer_size;
	u32 opb_wr_top_line_chroma_buffer_size;
	u32 opb_wr_top_line_luma_buffer_size;
	u32 macrotiling_size = 32, size;

	opb_wr_top_line_luma_buffer_size =
		ALIGN(frame_width, macrotiling_size) /
		macrotiling_size * 256;
	opb_wr_top_line_luma_buffer_size =
		ALIGN(opb_wr_top_line_luma_buffer_size, DMA_ALIGNMENT) +
		(MAX_TILE_COLUMNS - 1) * 256;
	opb_wr_top_line_luma_buffer_size =
		max_t(u32, opb_wr_top_line_luma_buffer_size,
		      (32 * ALIGN(frame_height, 8)));
	opb_wr_top_line_chroma_buffer_size =
		opb_wr_top_line_luma_buffer_size;
	opb_lb_wr_llb_uv_buffer_size =
		ALIGN((ALIGN(frame_height, 8) / (4 / 2)) * 64,
		      BUFFER_ALIGNMENT_32_BYTES);
	opb_lb_wr_llb_y_buffer_size =
		ALIGN((ALIGN(frame_height, 8) / (4 / 2)) * 64,
		      BUFFER_ALIGNMENT_32_BYTES);
	size = num_vpp_pipes * 2 *
		(vpss_4tap_top_buffer_size + vpss_div2_top_buffer_size) +
		2 * (vpss_4tap_left_buffer_size + vpss_div2_left_buffer_size) +
		opb_wr_top_line_luma_buffer_size +
		opb_wr_top_line_chroma_buffer_size +
		opb_lb_wr_llb_uv_buffer_size +
		opb_lb_wr_llb_y_buffer_size;

	return size;
}

static inline
u32 hfi_buffer_line_h264d(u32 frame_width, u32 frame_height,
			  bool is_opb, u32 num_vpp_pipes)
{
	u32 vpss_lb_size = 0;
	u32 _size;

	_size = ALIGN(size_h264d_lb_fe_top_data(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_fe_top_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_fe_left_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h264d_lb_se_top_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_se_left_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h264d_lb_pe_top_data(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_vsp_top(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_recon_dma_metadata_wr(frame_width, frame_height),
		      DMA_ALIGNMENT) * 2 +
		ALIGN(size_h264d_qp(frame_width, frame_height),
		      DMA_ALIGNMENT);
	_size = ALIGN(_size, DMA_ALIGNMENT);
	if (is_opb)
		vpss_lb_size = size_vpss_lb(frame_width, frame_height,
					    num_vpp_pipes);

	_size = ALIGN((_size + vpss_lb_size),
		      DMA_ALIGNMENT);

	return _size;
}

static inline
u32 size_h265d_lb_fe_top_data(u32 frame_width, u32 frame_height)
{
	return MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE *
		(ALIGN(frame_width, 64) + 8) * 2;
}

static inline
u32 size_h265d_lb_fe_top_ctrl(u32 frame_width, u32 frame_height)
{
	return MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE *
		(ALIGN(frame_width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS);
}

static inline
u32 size_h265d_lb_fe_left_ctrl(u32 frame_width, u32 frame_height)
{
	return MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE *
		(ALIGN(frame_height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS);
}

static inline
u32 size_h265d_lb_se_top_ctrl(u32 frame_width, u32 frame_height)
{
	return (LCU_MAX_SIZE_PELS / 8 * (128 / 8)) * ((frame_width + 15) >> 4);
}

static inline
u32 size_h265d_lb_se_left_ctrl(u32 frame_width, u32 frame_height)
{
	return max_t(u32, ((frame_height + 16 - 1) / 8) *
		     MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE,
		     max_t(u32, ((frame_height + 32 - 1) / 8) *
			   MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE,
			   ((frame_height + 64 - 1) / 8) *
			   MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE));
}

static inline
u32 size_h265d_lb_pe_top_data(u32 frame_width, u32 frame_height)
{
	return MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE *
		(ALIGN(frame_width, LCU_MIN_SIZE_PELS) / LCU_MIN_SIZE_PELS);
}

static inline
u32 size_h265d_lb_vsp_top(u32 frame_width, u32 frame_height)
{
	return ((frame_width + 63) >> 6) * 128;
}

static inline
u32 size_h265d_lb_vsp_left(u32 frame_width, u32 frame_height)
{
	return ((frame_height + 63) >> 6) * 128;
}

static inline
u32 size_h265d_lb_recon_dma_metadata_wr(u32 frame_width, u32 frame_height)
{
	return size_h264d_lb_recon_dma_metadata_wr(frame_width, frame_height);
}

static inline
u32 size_h265d_qp(u32 frame_width, u32 frame_height)
{
	return size_h264d_qp(frame_width, frame_height);
}

static inline
u32 hfi_buffer_line_h265d(u32 frame_width, u32 frame_height,
			  bool is_opb, u32 num_vpp_pipes)
{
	u32 vpss_lb_size = 0, _size;

	_size = ALIGN(size_h265d_lb_fe_top_data(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_fe_top_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_fe_left_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h265d_lb_se_left_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h265d_lb_se_top_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_pe_top_data(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_vsp_top(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_vsp_left(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h265d_lb_recon_dma_metadata_wr(frame_width, frame_height),
		      DMA_ALIGNMENT) * 4 +
		ALIGN(size_h265d_qp(frame_width, frame_height),
		      DMA_ALIGNMENT);
	if (is_opb)
		vpss_lb_size = size_vpss_lb(frame_width, frame_height, num_vpp_pipes);

	return ALIGN((_size + vpss_lb_size), DMA_ALIGNMENT);
}

static inline
u32 size_vpxd_lb_fe_left_ctrl(u32 frame_width, u32 frame_height)
{
	return max_t(u32, ((frame_height + 15) >> 4) * MAX_FE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE,
		     max_t(u32, ((frame_height + 31) >> 5) * MAX_FE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE,
			   ((frame_height + 63) >> 6) * MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE));
}

static inline
u32 size_vpxd_lb_fe_top_ctrl(u32 frame_width, u32 frame_height)
{
	return ((ALIGN(frame_width, 64) + 8) * 10 * 2);
}

static inline
u32 size_vpxd_lb_se_top_ctrl(u32 frame_width, u32 frame_height)
{
	return ((frame_width + 15) >> 4) * MAX_FE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE;
}

static inline
u32 size_vpxd_lb_se_left_ctrl(u32 frame_width, u32 frame_height)
{
	return max_t(u32, ((frame_height + 15) >> 4) * MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE,
		     max_t(u32, ((frame_height + 31) >> 5) * MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE,
			   ((frame_height + 63) >> 6) * MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE));
}

static inline
u32 size_vpxd_lb_recon_dma_metadata_wr(u32 frame_width, u32 frame_height)
{
	return ALIGN((ALIGN(frame_height, 8) / (4 / 2)) * 64,
		     BUFFER_ALIGNMENT_32_BYTES);
}

static inline
u32 size_mp2d_lb_fe_top_data(u32 frame_width, u32 frame_height)
{
	return ((ALIGN(frame_width, 16) + 8) * 10 * 2);
}

static inline
u32 size_vp9d_lb_fe_top_data(u32 frame_width, u32 frame_height)
{
	return (ALIGN(ALIGN(frame_width, 8), 64) + 8) * 10 * 2;
}

static inline
u32 size_vp9d_lb_pe_top_data(u32 frame_width, u32 frame_height)
{
	return ((ALIGN(ALIGN(frame_width, 8), 64) >> 6) * 176);
}

static inline
u32 size_vp9d_lb_vsp_top(u32 frame_width, u32 frame_height)
{
	return (((ALIGN(ALIGN(frame_width, 8), 64) >> 6) * 64 * 8) + 256);
}

static inline u32 hfi_iris3_vp9d_comv_size(void)
{
	return (((8192 + 63) >> 6) * ((4320 + 63) >> 6) * 8 * 8 * 2 * 8);
}

static inline
u32 size_vp9d_qp(u32 frame_width, u32 frame_height)
{
	return size_h264d_qp(frame_width, frame_height);
}

static inline
u32 hfi_iris3_vp9d_lb_size(u32 frame_width, u32 frame_height,
			   u32 num_vpp_pipes)
{
	return ALIGN(size_vpxd_lb_fe_left_ctrl(frame_width, frame_height),
		     DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_vpxd_lb_se_left_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_vp9d_lb_vsp_top(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_vpxd_lb_fe_top_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		2 * ALIGN(size_vpxd_lb_recon_dma_metadata_wr(frame_width, frame_height),
			  DMA_ALIGNMENT) +
		ALIGN(size_vpxd_lb_se_top_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_vp9d_lb_pe_top_data(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_vp9d_lb_fe_top_data(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_vp9d_qp(frame_width, frame_height),
		      DMA_ALIGNMENT);
}

static inline
u32 hfi_buffer_line_vp9d(u32 frame_width, u32 frame_height,
			 u32 _yuv_bufcount_min, bool is_opb,
			 u32 num_vpp_pipes)
{
	u32 vpss_lb_size = 0;
	u32 _lb_size = 0;

	_lb_size = hfi_iris3_vp9d_lb_size(frame_width, frame_height,
					  num_vpp_pipes);

	if (is_opb)
		vpss_lb_size = size_vpss_lb(frame_width, frame_height,
					    num_vpp_pipes);

	return _lb_size + vpss_lb_size;
}

static inline u32 hfi_buffer_persist_h264d(u32 rpu_enabled)
{
	return ALIGN(SIZE_SLIST_BUF_H264 * NUM_SLIST_BUF_H264 +
		    H264_DISPLAY_BUF_SIZE * H264_NUM_FRM_INFO +
		    NUM_HW_PIC_BUF * SIZE_SEI_USERDATA +
		    (rpu_enabled) * NUM_HW_PIC_BUF * SIZE_DOLBY_RPU_METADATA,
		    DMA_ALIGNMENT);
}

static inline
u32 hfi_buffer_persist_h265d(u32 rpu_enabled)
{
	return ALIGN((SIZE_SLIST_BUF_H265 * NUM_SLIST_BUF_H265 +
		      H265_NUM_FRM_INFO * H265_DISPLAY_BUF_SIZE +
		      H265_NUM_TILE * sizeof(u32) +
		      NUM_HW_PIC_BUF * SIZE_SEI_USERDATA +
		      rpu_enabled * NUM_HW_PIC_BUF * SIZE_DOLBY_RPU_METADATA),
		     DMA_ALIGNMENT);
}

static inline u32 hfi_buffer_persist_vp9d(void)
{
	return ALIGN(VP9_NUM_PROBABILITY_TABLE_BUF * VP9_PROB_TABLE_SIZE,
		     DMA_ALIGNMENT) +
		ALIGN(hfi_iris3_vp9d_comv_size(), DMA_ALIGNMENT) +
		ALIGN(MAX_SUPERFRAME_HEADER_LEN, DMA_ALIGNMENT) +
		ALIGN(VP9_UDC_HEADER_BUF_SIZE, DMA_ALIGNMENT) +
		ALIGN(VP9_NUM_FRAME_INFO_BUF * CCE_TILE_OFFSET_SIZE,
		      DMA_ALIGNMENT) +
		ALIGN(VP9_NUM_FRAME_INFO_BUF * VP9_FRAME_INFO_BUF_SIZE,
		      DMA_ALIGNMENT) +
		HDR10_HIST_EXTRADATA_SIZE;
}

static inline
u32 hfi_nv12_ubwc_il_calc_y_buf_size(u32 frame_width, u32 frame_height,
				     u32 stride_multiple,
				     u32 min_buf_height_multiple)
{
	u32 stride, buf_height;

	stride = ALIGN(frame_width, stride_multiple);
	buf_height = ALIGN(frame_height, min_buf_height_multiple);

	return ALIGN(stride * buf_height, HFI_ALIGNMENT_4096);
}

static inline
u32 hfi_nv12_ubwc_il_calc_uv_buf_size(u32 frame_width, u32 frame_height,
				      u32 stride_multiple,
				      u32 min_buf_height_multiple)
{
	u32 uv_stride, uv_buf_height;

	uv_stride = ALIGN(frame_width, stride_multiple);
	uv_buf_height = ALIGN(((frame_height + 1) >> 1),
			      min_buf_height_multiple);

	return ALIGN(uv_stride * uv_buf_height, HFI_ALIGNMENT_4096);
}

static inline
u32 hfi_ubwc_calc_metadata_plane_stride(u32 frame_width,
					u32 metadata_stride_multiple,
					u32 tile_width_in_pels)
{
	return ALIGN(((frame_width + (tile_width_in_pels - 1)) / tile_width_in_pels),
		     metadata_stride_multiple);
}

static inline
u32 hfi_ubwc_metadata_plane_bufheight(u32 frame_height,
				      u32 metadata_height_multiple,
				      u32 tile_height_in_pels)
{
	return ALIGN(((frame_height + (tile_height_in_pels - 1)) / tile_height_in_pels),
		     metadata_height_multiple);
}

static inline
u32 hfi_ubwc_uv_metadata_plane_stride(u32 frame_width,
				      u32 metadata_stride_multiple,
				      u32 tile_width_in_pels)
{
	return ALIGN(((((frame_width + 1) >> 1) + tile_width_in_pels - 1) /
		      tile_width_in_pels), metadata_stride_multiple);
}

static inline
u32 hfi_ubwc_uv_metadata_plane_bufheight(u32 frame_height,
					 u32 metadata_height_multiple,
					 u32 tile_height_in_pels)
{
	return ALIGN(((((frame_height + 1) >> 1) + tile_height_in_pels - 1) /
		      tile_height_in_pels), metadata_height_multiple);
}

static inline
u32 hfi_ubwc_metadata_plane_buffer_size(u32 _metadata_tride, u32 _metadata_buf_height)
{
	return ALIGN(_metadata_tride * _metadata_buf_height, HFI_ALIGNMENT_4096);
}

static inline
u32 hfi_nv12_ubwc_il_calc_buf_size_v2(u32 frame_width,
				      u32 frame_height,
				      u32 y_stride_multiple,
				      u32 y_buffer_height_multiple,
				      u32 uv_stride_multiple,
				      u32 uv_buffer_height_multiple,
				      u32 y_metadata_stride_multiple,
				      u32 y_metadata_buffer_height_multiple,
				      u32 uv_metadata_stride_multiple,
				      u32 uv_metadata_buffer_height_multiple)
{
	u32 y_buf_size, uv_buf_size, y_meta_size, uv_meta_size;
	u32 half_height = (frame_height + 1) >> 1;
	u32 stride, _height;

	y_buf_size =
		hfi_nv12_ubwc_il_calc_y_buf_size(frame_width, half_height,
						 y_stride_multiple,
						 y_buffer_height_multiple);
	uv_buf_size =
		hfi_nv12_ubwc_il_calc_uv_buf_size(frame_width, half_height,
						  uv_stride_multiple,
						  uv_buffer_height_multiple);
	stride =
		hfi_ubwc_calc_metadata_plane_stride(frame_width,
						    y_metadata_stride_multiple,
						    HFI_COL_FMT_NV12C_Y_TILE_WIDTH);
	_height =
		hfi_ubwc_metadata_plane_bufheight(half_height,
						  y_metadata_buffer_height_multiple,
						  HFI_COL_FMT_NV12C_Y_TILE_HEIGHT);
	y_meta_size = hfi_ubwc_metadata_plane_buffer_size(stride, _height);
	stride =
		hfi_ubwc_uv_metadata_plane_stride(frame_width,
						  uv_metadata_stride_multiple,
						  HFI_COL_FMT_NV12C_UV_TILE_WIDTH);
	_height =
		hfi_ubwc_uv_metadata_plane_bufheight(half_height,
						     uv_metadata_buffer_height_multiple,
						     HFI_COL_FMT_NV12C_UV_TILE_HEIGHT);
	uv_meta_size = hfi_ubwc_metadata_plane_buffer_size(stride, _height);

	return (y_buf_size + uv_buf_size + y_meta_size + uv_meta_size) << 1;
}

static inline
u32 hfi_yuv420_tp10_ubwc_calc_y_buf_size(u32 y_stride,
					 u32 y_buf_height)
{
	return ALIGN(y_stride * y_buf_height, HFI_ALIGNMENT_4096);
}

static inline
u32 hfi_yuv420_tp10_ubwc_calc_uv_buf_size(u32 uv_stride,
					  u32 uv_buf_height)
{
	return ALIGN(uv_stride * uv_buf_height, HFI_ALIGNMENT_4096);
}

static inline
u32 hfi_yuv420_tp10_ubwc_calc_buf_size(u32 y_stride, u32 y_buf_height,
				       u32 uv_stride, u32 uv_buf_height,
				       u32 y_md_stride, u32 y_md_height,
				       u32 uv_md_stride, u32 uv_md_height)
{
	u32 y_data_size, uv_data_size, y_md_size, uv_md_size;

	y_data_size = hfi_yuv420_tp10_ubwc_calc_y_buf_size(y_stride, y_buf_height);
	uv_data_size = hfi_yuv420_tp10_ubwc_calc_uv_buf_size(uv_stride, uv_buf_height);
	y_md_size = hfi_ubwc_metadata_plane_buffer_size(y_md_stride, y_md_height);
	uv_md_size = hfi_ubwc_metadata_plane_buffer_size(uv_md_stride, uv_md_height);

	return y_data_size + uv_data_size + y_md_size + uv_md_size;
}

int iris_int_buf_size_iris3(struct iris_inst *inst,
			    enum iris_buffer_type buffer_type);

static inline
u32 hfi_buffer_bitstream_enc(u32 frame_width, u32 frame_height,
			     u32 rc_type, bool is_ten_bit)
{
	u32 aligned_width, aligned_height, bitstream_size, yuv_size;

	aligned_width = ALIGN(frame_width, 32);
	aligned_height = ALIGN(frame_height, 32);
	bitstream_size = aligned_width * aligned_height * 3;
	yuv_size = (aligned_width * aligned_height * 3) >> 1;
	if (aligned_width * aligned_height > (4096 * 2176))
		/* bitstream_size = 0.25 * yuv_size; */
		bitstream_size = (bitstream_size >> 3);
	else if (aligned_width * aligned_height > (1280 * 720))
		/* bitstream_size = 0.5 * yuv_size; */
		bitstream_size = (bitstream_size >> 2);

	if ((rc_type == HFI_RC_CQ || rc_type == HFI_RC_OFF) &&
	    bitstream_size < yuv_size)
		bitstream_size = (bitstream_size << 1);

	if (is_ten_bit)
		bitstream_size = (bitstream_size) + (bitstream_size >> 2);

	return ALIGN(bitstream_size, HFI_ALIGNMENT_4096);
}

static inline
u32 hfi_iris3_enc_recon_buf_count(s32 n_bframe, s32 ltr_count,
				  s32 _total_hp_layers,
				  s32 _total_hb_layers,
				  bool hybrid_hp,
				  u32 codec_standard)
{
	u32 num_ref = 1;

	if (n_bframe)
		num_ref = 2;

	if (_total_hp_layers > 1) {
		if (hybrid_hp)
			num_ref = (_total_hp_layers + 1) >> 1;
		else if (codec_standard == HFI_CODEC_ENCODE_HEVC)
			num_ref = (_total_hp_layers + 1) >> 1;
		else if (codec_standard == HFI_CODEC_ENCODE_AVC &&
			 _total_hp_layers < 4)
			num_ref = (_total_hp_layers - 1);
		else
			num_ref = _total_hp_layers;
	}

	if (ltr_count)
		num_ref = num_ref + ltr_count;

	if (_total_hb_layers > 1) {
		if (codec_standard == HFI_CODEC_ENCODE_HEVC)
			num_ref = (_total_hb_layers);
		else if (codec_standard == HFI_CODEC_ENCODE_AVC)
			num_ref = (1 << (_total_hb_layers - 2)) + 1;
	}

	return num_ref + 1;
}

static inline
u32 size_bin_bitstream_enc(u32 rc_type, u32 frame_width, u32 frame_height,
			   u32 work_mode, u32 lcu_size, u32 profile)
{
	u32 size_aligned_width = 0, size_aligned_height = 0;
	u32 bitstream_size_eval = 0;

	size_aligned_width = ALIGN((frame_width), lcu_size);
	size_aligned_height = ALIGN((frame_height), lcu_size);
	if (work_mode == HFI_WORKMODE_2) {
		if (rc_type == HFI_RC_CQ || rc_type == HFI_RC_OFF) {
			bitstream_size_eval = (((size_aligned_width) *
						(size_aligned_height) * 3) >> 1);
		} else {
			bitstream_size_eval = ((size_aligned_width) *
					       (size_aligned_height) * 3);
			if ((size_aligned_width * size_aligned_height) > (4096 * 2176))
				bitstream_size_eval >>= 3;
			else if ((size_aligned_width * size_aligned_height) > (480 * 320))
				bitstream_size_eval >>= 2;

			if (profile == HFI_H265_PROFILE_MAIN_10 ||
			    profile == HFI_H265_PROFILE_MAIN_10_STILL_PICTURE)
				bitstream_size_eval = (bitstream_size_eval * 5 >> 2);
		}
	} else {
		bitstream_size_eval = size_aligned_width * size_aligned_height * 3;
	}

	return ALIGN(bitstream_size_eval, BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 size_enc_single_pipe(u32 rc_type, u32 bitbin_size, u32 num_vpp_pipes,
			 u32 frame_width, u32 frame_height, u32 lcu_size)
{
	u32 size_single_pipe_eval = 0, sao_bin_buffer_size = 0;
	u32 size_aligned_width = 0, size_aligned_height = 0;
	u32 _padded_bin_sz = 0;

	size_aligned_width = ALIGN((frame_width), lcu_size);
	size_aligned_height = ALIGN((frame_height), lcu_size);
	if ((size_aligned_width * size_aligned_height) > (3840 * 2160))
		size_single_pipe_eval = (bitbin_size / num_vpp_pipes);
	else if (num_vpp_pipes > 2)
		size_single_pipe_eval = bitbin_size / 2;
	else
		size_single_pipe_eval = bitbin_size;

	sao_bin_buffer_size =
		(64 * ((((frame_width) + BUFFER_ALIGNMENT_32_BYTES) *
			((frame_height) + BUFFER_ALIGNMENT_32_BYTES)) >> 10)) + 384;
	_padded_bin_sz = ALIGN(size_single_pipe_eval, BUFFER_ALIGNMENT_256_BYTES);
	size_single_pipe_eval = sao_bin_buffer_size + _padded_bin_sz;
	size_single_pipe_eval = ALIGN(size_single_pipe_eval, BUFFER_ALIGNMENT_256_BYTES);

	return size_single_pipe_eval;
}

static inline
u32 hfi_buffer_bin_enc(u32 rc_type, u32 frame_width, u32 frame_height, u32 lcu_size,
		       u32 work_mode, u32 num_vpp_pipes, u32 profile)
{
	u32 bitstream_size = 0, total_bitbin_buffers = 0;
	u32 size_single_pipe = 0, bitbin_size = 0;
	u32 _size = 0;

	bitstream_size = size_bin_bitstream_enc(rc_type, frame_width,
						frame_height, work_mode,
						lcu_size, profile);

	if (work_mode == HFI_WORKMODE_2) {
		total_bitbin_buffers = 3;
		bitbin_size = bitstream_size * 12 / 10;
		bitbin_size = ALIGN(bitbin_size, BUFFER_ALIGNMENT_256_BYTES);
	} else if ((lcu_size == 16) || (num_vpp_pipes > 1)) {
		total_bitbin_buffers = 1;
		bitbin_size = bitstream_size;
	}

	if (total_bitbin_buffers > 0) {
		size_single_pipe = size_enc_single_pipe(rc_type, bitbin_size,
							num_vpp_pipes, frame_width,
							frame_height, lcu_size);
		bitbin_size = size_single_pipe * num_vpp_pipes;
		_size = ALIGN(bitbin_size, BUFFER_ALIGNMENT_256_BYTES) *
			      total_bitbin_buffers + 512;
	} else {
		/* Avoid 512 Bytes allocation in case of 1Pipe HEVC Direct Mode*/
		_size = 0;
	}

	return _size;
}

static inline
u32 hfi_buffer_bin_h264e(u32 rc_type, u32 frame_width, u32 frame_height,
			 u32 work_mode, u32 num_vpp_pipes, u32 profile)
{
	return hfi_buffer_bin_enc(rc_type, frame_width, frame_height, 16,
				  work_mode, num_vpp_pipes, profile);
}

static inline
u32 hfi_buffer_bin_h265e(u32 rc_type, u32 frame_width, u32 frame_height,
			 u32 work_mode, u32 num_vpp_pipes, u32 profile)
{
	return hfi_buffer_bin_enc(rc_type, frame_width, frame_height, 32,
				  work_mode, num_vpp_pipes, profile);
}

static inline
u32 size_enc_slice_info_buf(u32 num_lcu_in_frame)
{
	return ALIGN((256 + (num_lcu_in_frame << 4)), BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 size_line_buf_ctrl(u32 frame_width_coded)
{
	return ALIGN(frame_width_coded, BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 size_line_buf_ctrl_id2(u32 frame_width_coded)
{
	return ALIGN(frame_width_coded, BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 size_linebuff_data(bool is_ten_bit, u32 frame_width_coded)
{
	u32 _size = 0;

	_size = is_ten_bit ?
		(((((10 * (frame_width_coded) + 1024) + (BUFFER_ALIGNMENT_256_BYTES - 1)) &
		   (~(BUFFER_ALIGNMENT_256_BYTES - 1))) * 1) +
		 (((((10 * (frame_width_coded) + 1024) >> 1) + (BUFFER_ALIGNMENT_256_BYTES - 1)) &
		   (~(BUFFER_ALIGNMENT_256_BYTES - 1))) * 2)) :
		(((((8 * (frame_width_coded) + 1024) + (BUFFER_ALIGNMENT_256_BYTES - 1)) &
		   (~(BUFFER_ALIGNMENT_256_BYTES - 1))) * 1) +
		 (((((8 * (frame_width_coded) + 1024) >> 1) + (BUFFER_ALIGNMENT_256_BYTES - 1)) &
		   (~(BUFFER_ALIGNMENT_256_BYTES - 1))) * 2));

	return _size;
}

static inline
u32 size_left_linebuff_ctrl(u32 standard, u32 frame_height_coded,
			    u32 num_vpp_pipes_enc)
{
	u32 _size = 0;

	_size = standard == HFI_CODEC_ENCODE_HEVC ?
		(((frame_height_coded) +
		 (BUFFER_ALIGNMENT_32_BYTES)) / BUFFER_ALIGNMENT_32_BYTES * 4 * 16) :
		(((frame_height_coded) + 15) / 16 * 5 * 16);

	if ((num_vpp_pipes_enc) > 1) {
		_size += BUFFER_ALIGNMENT_512_BYTES;
		_size = ALIGN(_size, BUFFER_ALIGNMENT_512_BYTES) *
			num_vpp_pipes_enc;
	}

	return ALIGN(_size, BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 size_left_linebuff_recon_pix(bool is_ten_bit, u32 frame_height_coded,
				 u32 num_vpp_pipes_enc)
{
	return (((is_ten_bit + 1) * 2 * (frame_height_coded) + BUFFER_ALIGNMENT_256_BYTES) +
		(BUFFER_ALIGNMENT_256_BYTES << (num_vpp_pipes_enc - 1)) - 1) &
		(~((BUFFER_ALIGNMENT_256_BYTES << (num_vpp_pipes_enc - 1)) - 1)) * 1;
}

static inline
u32 size_top_linebuff_ctrl_fe(u32 frame_width_coded, u32 standard)
{
	u32 _size = 0;

	_size = standard == HFI_CODEC_ENCODE_HEVC ?
		(64 * ((frame_width_coded) >> 5)) :
		(BUFFER_ALIGNMENT_256_BYTES + 16 * ((frame_width_coded) >> 4));

	return ALIGN(_size, BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 size_left_linebuff_ctrl_fe(u32 frame_height_coded, u32 num_vpp_pipes_enc)
{
	return (((BUFFER_ALIGNMENT_256_BYTES + 64 * ((frame_height_coded) >> 4)) +
		 (BUFFER_ALIGNMENT_256_BYTES << (num_vpp_pipes_enc - 1)) - 1) &
		 (~((BUFFER_ALIGNMENT_256_BYTES << (num_vpp_pipes_enc - 1)) - 1)) * 1) *
		num_vpp_pipes_enc;
}

static inline
u32 size_left_linebuff_metadata_recon_y(u32 frame_height_coded,
					bool is_ten_bit,
					u32 num_vpp_pipes_enc)
{
	u32 _size = 0;

	_size = ((BUFFER_ALIGNMENT_256_BYTES + 64 * ((frame_height_coded) /
		  (8 * (is_ten_bit ? 4 : 8)))));
	_size = ALIGN(_size, BUFFER_ALIGNMENT_256_BYTES);

	return (_size * num_vpp_pipes_enc);
}

static inline
u32 size_left_linebuff_metadata_recon_uv(u32 frame_height_coded,
					 bool is_ten_bit,
					 u32 num_vpp_pipes_enc)
{
	u32 _size = 0;

	_size = ((BUFFER_ALIGNMENT_256_BYTES + 64 * ((frame_height_coded) /
		  (4 * (is_ten_bit ? 4 : 8)))));
	_size = ALIGN(_size, BUFFER_ALIGNMENT_256_BYTES);

	return (_size * num_vpp_pipes_enc);
}

static inline
u32 size_linebuff_recon_pix(bool is_ten_bit, u32 frame_width_coded)
{
	return ALIGN(((is_ten_bit ? 3 : 2) * (frame_width_coded)),
		     BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 size_frame_rc_buf_size(u32 standard, u32 frame_height_coded,
			   u32 num_vpp_pipes_enc)
{
	u32 _size = 0;

	_size = (standard == HFI_CODEC_ENCODE_HEVC) ?
		(256 + 16 * (14 + ((((frame_height_coded) >> 5) + 7) >> 3))) :
		(256 + 16 * (14 + ((((frame_height_coded) >> 4) + 7) >> 3)));
	_size *= 11;
	if (num_vpp_pipes_enc > 1)
		_size = ALIGN(_size, BUFFER_ALIGNMENT_256_BYTES) * num_vpp_pipes_enc;

	return ALIGN(_size, BUFFER_ALIGNMENT_512_BYTES) * HFI_MAX_COL_FRAME;
}

static inline u32 enc_bitcnt_buf_size(u32 num_lcu_in_frame)
{
	return ALIGN((256 + (4 * (num_lcu_in_frame))), BUFFER_ALIGNMENT_256_BYTES);
}

static inline u32 enc_bitmap_buf_size(u32 num_lcu_in_frame)
{
	return ALIGN((256 + ((num_lcu_in_frame) >> 3)), BUFFER_ALIGNMENT_256_BYTES);
}

static inline u32 size_line_buf_sde(u32 frame_width_coded)
{
	return ALIGN((256 + (16 * ((frame_width_coded) >> 4))), BUFFER_ALIGNMENT_256_BYTES);
}

static inline u32 size_override_buf(u32 num_lcumb)
{
	return ALIGN(((16 * (((num_lcumb) + 7) >> 3))), BUFFER_ALIGNMENT_256_BYTES) * 2;
}

static inline u32 size_ir_buf(u32 num_lcu_in_frame)
{
	return ALIGN((((((num_lcu_in_frame) << 1) + 7) & (~7)) * 3), BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 size_vpss_line_buf(u32 num_vpp_pipes_enc, u32 frame_height_coded,
		       u32 frame_width_coded)
{
	return ALIGN(((((((8192) >> 2) << 5) * (num_vpp_pipes_enc)) + 64) +
		      (((((max_t(u32, (frame_width_coded),
				 (frame_height_coded)) + 3) >> 2) << 5) + 256) * 16)),
		     BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 size_top_line_buf_first_stg_sao(u32 frame_width_coded)
{
	return ALIGN((16 * ((frame_width_coded) >> 5)), BUFFER_ALIGNMENT_256_BYTES);
}

static inline
u32 hfi_buffer_line_enc(u32 frame_width, u32 frame_height, bool is_ten_bit,
			u32 num_vpp_pipes_enc, u32 lcu_size, u32 standard)
{
	u32 line_buff_data_size = 0, left_line_buff_ctrl_size = 0;
	u32 frame_width_coded = 0, frame_height_coded = 0;
	u32 left_line_buff_metadata_recon__uv__size = 0;
	u32 left_line_buff_metadata_recon__y__size = 0;
	u32 width_in_lcus = 0, height_in_lcus = 0;
	u32 left_line_buff_recon_pix_size = 0;
	u32 top_line_buff_ctrl_fe_size = 0;
	u32 line_buff_recon_pix_size = 0;

	width_in_lcus = ((frame_width) + (lcu_size) - 1) / (lcu_size);
	height_in_lcus = ((frame_height) + (lcu_size) - 1) / (lcu_size);
	frame_width_coded = width_in_lcus * (lcu_size);
	frame_height_coded = height_in_lcus * (lcu_size);
	line_buff_data_size = size_linebuff_data(is_ten_bit, frame_width_coded);
	left_line_buff_ctrl_size =
		size_left_linebuff_ctrl(standard, frame_height_coded, num_vpp_pipes_enc);
	left_line_buff_recon_pix_size =
		size_left_linebuff_recon_pix(is_ten_bit, frame_height_coded,
					     num_vpp_pipes_enc);
	top_line_buff_ctrl_fe_size =
		size_top_linebuff_ctrl_fe(frame_width_coded, standard);
	left_line_buff_metadata_recon__y__size =
		size_left_linebuff_metadata_recon_y(frame_height_coded, is_ten_bit,
						    num_vpp_pipes_enc);
	left_line_buff_metadata_recon__uv__size =
		size_left_linebuff_metadata_recon_uv(frame_height_coded, is_ten_bit,
						     num_vpp_pipes_enc);
	line_buff_recon_pix_size = size_linebuff_recon_pix(is_ten_bit, frame_width_coded);

	return size_line_buf_ctrl(frame_width_coded) +
		size_line_buf_ctrl_id2(frame_width_coded) +
		line_buff_data_size +
		left_line_buff_ctrl_size +
		left_line_buff_recon_pix_size +
		top_line_buff_ctrl_fe_size +
		left_line_buff_metadata_recon__y__size +
		left_line_buff_metadata_recon__uv__size +
		line_buff_recon_pix_size +
		size_left_linebuff_ctrl_fe(frame_height_coded,
					   num_vpp_pipes_enc) +
		size_line_buf_sde(frame_width_coded) +
		size_vpss_line_buf(num_vpp_pipes_enc, frame_height_coded,
				   frame_width_coded) +
		size_top_line_buf_first_stg_sao(frame_width_coded);
}

static inline
u32 hfi_buffer_line_h264e(u32 frame_width, u32 frame_height, bool is_ten_bit,
			  u32 num_vpp_pipes)
{
	return hfi_buffer_line_enc(frame_width, frame_height, 0,
				   num_vpp_pipes, 16,
				   HFI_CODEC_ENCODE_AVC);
}

static inline
u32 hfi_buffer_line_h265e(u32 frame_width, u32 frame_height, bool is_ten_bit,
			  u32 num_vpp_pipes)
{
	return hfi_buffer_line_enc(frame_width, frame_height,
				   is_ten_bit, num_vpp_pipes, 32,
				   HFI_CODEC_ENCODE_HEVC);
}

static inline
u32 hfi_buffer_comv_enc(u32 frame_width, u32 frame_height, u32 lcu_size,
			u32 num_recon, u32 standard)
{
	u32 width_in_lcus, height_in_lcus, num_lcu_in_frame;
	u32 size_colloc_mv = 0, size_colloc_rc = 0;
	u32 mb_height = ((frame_height) + 15) >> 4;
	u32 mb_width = ((frame_width) + 15) >> 4;

	width_in_lcus = ((frame_width) + (lcu_size) - 1) / (lcu_size);
	height_in_lcus = ((frame_height) + (lcu_size) - 1) / (lcu_size);
	num_lcu_in_frame = width_in_lcus * height_in_lcus;
	size_colloc_mv = (standard == HFI_CODEC_ENCODE_HEVC) ?
		(16 * ((num_lcu_in_frame << 2) + BUFFER_ALIGNMENT_32_BYTES)) :
		(3 * 16 * (width_in_lcus * height_in_lcus + BUFFER_ALIGNMENT_32_BYTES));
	size_colloc_mv = ALIGN(size_colloc_mv, BUFFER_ALIGNMENT_256_BYTES) * num_recon;
	size_colloc_rc = (((mb_width + 7) >> 3) * 16 * 2 * mb_height);
	size_colloc_rc = ALIGN(size_colloc_rc, BUFFER_ALIGNMENT_256_BYTES) * HFI_MAX_COL_FRAME;

	return size_colloc_mv + size_colloc_rc;
}

static inline
u32 hfi_buffer_comv_h264e(u32 frame_width, u32 frame_height, u32 num_recon)
{
	return hfi_buffer_comv_enc(frame_width, frame_height, 16,
				   num_recon, HFI_CODEC_ENCODE_AVC);
}

static inline
u32 hfi_buffer_comv_h265e(u32 frame_width, u32 frame_height, u32 num_recon)
{
	return hfi_buffer_comv_enc(frame_width, frame_height, 32,
				   num_recon, HFI_CODEC_ENCODE_HEVC);
}

static inline
u32 hfi_buffer_non_comv_enc(u32 frame_width, u32 frame_height,
			    u32 num_vpp_pipes_enc, u32 lcu_size, u32 standard)
{
	u32 frame_width_coded = 0, frame_height_coded = 0;
	u32 width_in_lcus = 0, height_in_lcus = 0;
	u32 num_lcu_in_frame = 0, num_lcumb = 0;
	u32 frame_rc_buf_size = 0;

	width_in_lcus = ((frame_width) + (lcu_size) - 1) / (lcu_size);
	height_in_lcus = ((frame_height) + (lcu_size) - 1) / (lcu_size);
	num_lcu_in_frame = width_in_lcus * height_in_lcus;
	frame_width_coded = width_in_lcus * (lcu_size);
	frame_height_coded = height_in_lcus * (lcu_size);
	num_lcumb = (frame_height_coded / lcu_size) *
		((frame_width_coded + lcu_size * 8) / lcu_size);
	frame_rc_buf_size =
		size_frame_rc_buf_size(standard, frame_height_coded,
				       num_vpp_pipes_enc);
	return size_enc_slice_info_buf(num_lcu_in_frame) +
		   SIZE_SLICE_CMD_BUFFER +
		   SIZE_SPS_PPS_SLICE_HDR +
		   frame_rc_buf_size +
		   enc_bitcnt_buf_size(num_lcu_in_frame) +
		   enc_bitmap_buf_size(num_lcu_in_frame) +
		   SIZE_BSE_SLICE_CMD_BUF +
		   SIZE_LAMBDA_LUT +
		   size_override_buf(num_lcumb) +
		   size_ir_buf(num_lcu_in_frame);
}

static inline
u32 hfi_buffer_non_comv_h264e(u32 frame_width, u32 frame_height,
			      u32 num_vpp_pipes_enc)
{
	return hfi_buffer_non_comv_enc(frame_width, frame_height,
				       num_vpp_pipes_enc, 16,
				       HFI_CODEC_ENCODE_AVC);
}

static inline
u32 hfi_buffer_non_comv_h265e(u32 frame_width, u32 frame_height,
			      u32 num_vpp_pipes_enc)
{
	return hfi_buffer_non_comv_enc(frame_width, frame_height,
				       num_vpp_pipes_enc, 32,
				       HFI_CODEC_ENCODE_HEVC);
}

static inline
u32 size_enc_ref_buffer(u32 frame_width, u32 frame_height)
{
	u32 u_buffer_width = 0, u_buffer_height = 0;
	u32 u_chroma_buffer_height = 0;

	u_buffer_height = ALIGN(frame_height, HFI_VENUS_HEIGHT_ALIGNMENT);
	u_chroma_buffer_height = frame_height >> 1;
	u_chroma_buffer_height = ALIGN(u_chroma_buffer_height, HFI_VENUS_HEIGHT_ALIGNMENT);
	u_buffer_width = ALIGN(frame_width, HFI_VENUS_WIDTH_ALIGNMENT);

	return (u_buffer_height + u_chroma_buffer_height) * u_buffer_width;
}

static inline
u32 size_enc_ten_bit_ref_buffer(u32 frame_width, u32 frame_height)
{
	u32 ref_buf_height = 0, ref_luma_stride_in_bytes = 0;
	u32 chroma_size = 0, ref_buf_size = 0;
	u32 u_ref_stride = 0, luma_size = 0;
	u32 ref_chrm_height_in_bytes = 0;

	ref_buf_height = (frame_height + (HFI_VENUS_HEIGHT_ALIGNMENT - 1)) &
			(~(HFI_VENUS_HEIGHT_ALIGNMENT - 1));
	ref_luma_stride_in_bytes = ((frame_width + SYSTEM_LAL_TILE10 - 1) / SYSTEM_LAL_TILE10) *
		SYSTEM_LAL_TILE10;
	u_ref_stride = 4 * (ref_luma_stride_in_bytes / 3);
	u_ref_stride = (u_ref_stride + (BUFFER_ALIGNMENT_128_BYTES - 1)) &
		(~(BUFFER_ALIGNMENT_128_BYTES - 1));
	luma_size = ref_buf_height * u_ref_stride;
	ref_chrm_height_in_bytes = (((frame_height + 1) >> 1) + (BUFFER_ALIGNMENT_32_BYTES - 1)) &
		(~(BUFFER_ALIGNMENT_32_BYTES - 1));
	chroma_size = u_ref_stride * ref_chrm_height_in_bytes;
	luma_size = (luma_size + (BUFFER_ALIGNMENT_4096_BYTES - 1)) &
		(~(BUFFER_ALIGNMENT_4096_BYTES - 1));
	chroma_size = (chroma_size + (BUFFER_ALIGNMENT_4096_BYTES - 1)) &
		(~(BUFFER_ALIGNMENT_4096_BYTES - 1));
	ref_buf_size = luma_size + chroma_size;

	return ref_buf_size;
}

static inline
u32 hfi_buffer_dpb_enc(u32 frame_width, u32 frame_height, bool is_ten_bit)
{
	u32 metadata_stride, metadata_buf_height, meta_size_y, meta_size_c;
	u32 ten_bit_ref_buf_size = 0, ref_buf_size = 0;
	u32 _size = 0;

	if (!is_ten_bit) {
		ref_buf_size = size_enc_ref_buffer(frame_width, frame_height);
		metadata_stride =
			hfi_ubwc_calc_metadata_plane_stride(frame_width, 64,
							    HFI_COL_FMT_NV12C_Y_TILE_WIDTH);
		metadata_buf_height =
			hfi_ubwc_metadata_plane_bufheight(frame_height, 16,
							  HFI_COL_FMT_NV12C_Y_TILE_HEIGHT);
		meta_size_y =
			hfi_ubwc_metadata_plane_buffer_size(metadata_stride, metadata_buf_height);
		meta_size_c =
			hfi_ubwc_metadata_plane_buffer_size(metadata_stride, metadata_buf_height);
		_size = ref_buf_size + meta_size_y + meta_size_c;
	} else {
		ten_bit_ref_buf_size = size_enc_ten_bit_ref_buffer(frame_width, frame_height);
		metadata_stride =
			hfi_ubwc_calc_metadata_plane_stride(frame_width,
							    VENUS_METADATA_STRIDE_MULTIPLE,
							    HFI_COL_FMT_TP10C_Y_TILE_WIDTH);
		metadata_buf_height =
			hfi_ubwc_metadata_plane_bufheight(frame_height,
							  VENUS_METADATA_HEIGHT_MULTIPLE,
							  HFI_COL_FMT_TP10C_Y_TILE_HEIGHT);
		meta_size_y =
			hfi_ubwc_metadata_plane_buffer_size(metadata_stride, metadata_buf_height);
		meta_size_c =
			hfi_ubwc_metadata_plane_buffer_size(metadata_stride, metadata_buf_height);
		_size = ten_bit_ref_buf_size + meta_size_y + meta_size_c;
	}

	return _size;
}

static inline
u32 hfi_buffer_dpb_h264e(u32 frame_width, u32 frame_height)
{
	return hfi_buffer_dpb_enc(frame_width, frame_height, 0);
}

static inline
u32 hfi_buffer_dpb_h265e(u32 frame_width, u32 frame_height, bool is_ten_bit)
{
	return hfi_buffer_dpb_enc(frame_width, frame_height, is_ten_bit);
}

static inline
u32 hfi_buffer_vpss_enc(u32 dswidth, u32 dsheight, bool ds_enable,
			u32 blur, bool is_ten_bit)
{
	u32 vpss_size = 0;

	if (ds_enable || blur)
		vpss_size = hfi_buffer_dpb_enc(dswidth, dsheight, is_ten_bit);

	return vpss_size;
}

static inline
u32 hfi_iris3_enc_min_input_buf_count(u32 totalhblayers)
{
	u32 numinput = 3;

	if (totalhblayers >= 2)
		numinput = (1 << (totalhblayers - 1)) + 2;

	return numinput;
}

u32 enc_output_buffer_size_iris3(struct iris_inst *inst);
u32 get_recon_buf_count(struct iris_inst *inst);

#endif
