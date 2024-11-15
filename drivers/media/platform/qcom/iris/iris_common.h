/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _IRIS_COMMON_H_
#define _IRIS_COMMON_H_

#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-v4l2.h>

struct iris_inst;

#define INPUT_MPLANE V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
#define OUTPUT_MPLANE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240
#define DEFAULT_BSE_VPP_DELAY    2
#define IRIS_VERSION_LENGTH   128

#define MAX_EVENTS   30

#define MB_IN_PIXEL (16 * 16)

#define NUM_MBS_4k (((4096 + 15) >> 4) * ((2304 + 15) >> 4))

#define MAX_DPB_COUNT 32

#define MAX_DPB_LIST_ARRAY_SIZE (16 * 4)
#define MAX_DPB_LIST_PAYLOAD_SIZE (16 * 4 * 4)

#define INPUT_TIMER_LIST_SIZE 30

#define CABAC_MAX_BITRATE 160000000

#define CAVLC_MAX_BITRATE 220000000

enum domain_type {
	ENCODER	= BIT(0),
	DECODER	= BIT(1),
};

enum codec_type {
	H264	= BIT(0),
	HEVC	= BIT(1),
	VP9	= BIT(2),
};

enum colorformat_type {
	FMT_NONE	= 0,
	FMT_NV12C	= BIT(0),
	FMT_NV12	= BIT(1),
	FMT_TP10C	= BIT(2),
};

struct rect_desc {
	u32 left;
	u32 top;
	u32 width;
	u32 height;
};

enum signal_session_response {
	SIGNAL_CMD_STOP_INPUT = 0,
	SIGNAL_CMD_STOP_OUTPUT,
	SIGNAL_CMD_CLOSE,
	MAX_SIGNAL,
};

enum iris_buffer_type {
	BUF_NONE,
	BUF_INPUT,
	BUF_OUTPUT,
	BUF_READ_ONLY,
	BUF_BIN,
	BUF_ARP,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
	BUF_DPB,
	BUF_PERSIST,
	BUF_VPSS,
};

enum iris_buffer_flags {
	BUF_FLAG_KEYFRAME	= 0x00000008,
	BUF_FLAG_PFRAME		= 0x00000010,
	BUF_FLAG_BFRAME		= 0x00000020,
	BUF_FLAG_ERROR		= 0x00000040,
	BUF_FLAG_LAST		= 0x00100000,
};

enum iris_buffer_attributes {
	BUF_ATTR_DEFERRED		= BIT(0),
	BUF_ATTR_READ_ONLY		= BIT(1),
	BUF_ATTR_PENDING_RELEASE	= BIT(2),
	BUF_ATTR_QUEUED			= BIT(3),
	BUF_ATTR_DEQUEUED		= BIT(4),
	BUF_ATTR_BUFFER_DONE		= BIT(5),
};

struct iris_buffer {
	struct list_head		list;
	struct iris_inst		*inst;
	enum iris_buffer_type		type;
	u32				index;
	int				fd;
	u32				buffer_size;
	u32				data_offset;
	u32				data_size;
	u64				device_addr;
	void				*kvaddr;
	unsigned long			dma_attrs;
	u32				flags;
	u64				timestamp;
	enum iris_buffer_attributes	attr;
	void				*dmabuf;
	struct sg_table			*sg_table;
	struct dma_buf_attachment	*attach;
};

struct subscription_params {
	u32	bitstream_resolution;
	u32	crop_offsets[2];
	u32	bit_depth;
	u32	coded_frames;
	u32	fw_min_count;
	u32	pic_order_cnt;
	u32	color_info;
	u32	profile;
	u32	level;
	u32	tier;
};

struct iris_hfi_frame_info {
	u32	picture_type;
	u32	no_output;
	u32	data_corrupt;
	u32	overflow;
};

struct iris_input_timer {
	struct list_head	list;
	u64			time_us;
};

#endif
