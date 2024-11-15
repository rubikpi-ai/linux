/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_INSTANCE_H_
#define _IRIS_INSTANCE_H_

#include <media/v4l2-ctrls.h>

#include "hfi_defines.h"
#include "iris_buffer.h"
#include "iris_common.h"
#include "iris_core.h"
#include "iris_common.h"
#include "platform_common.h"

/**
 * struct iris_inst - holds per video instance parameters
 *
 * @list: used for attach an instance to the core
 * @core: pointer to core structure
 * @session_id: id of current video session
 * @vb2q_src: source vb2 queue
 * @vb2q_dst: destination vb2 queue
 * @ctx_q_lock: lock to serialize queues related ioctls
 * @lock: lock to seralise forward and reverse threads
 * @fh: reference of v4l2 file handler
 * @fmt_src: structure of v4l2_format for source
 * @fmt_dst: structure of v4l2_format for destination
 * @ctrl_handler: reference of v4l2 ctrl handler
 * @crop: structure of crop info
 * @compose: structure of compose info
 * @packet: HFI packet
 * @packet_size: HFI packet size
 * @completions: structure of signal completions
 * @cap: array of supported instance capabilities
 * @num_ctrls: supported number of controls
 * @caps_list: list head of capability
 * @codec: codec type
 * @domain: domain type: encoder or decoder
 * @mem_pool: pointer to memory pool of buffers
 * @buffers: structure of buffer info
 * @fw_min_count: minimnum count of buffers needed by fw
 * @state: instance state
 * @sub_state: instance sub state
 * @ipsc_properties_set: boolean to set ipsc properties to fw
 * @opsc_properties_set: boolean to set opsc properties to fw
 * @hfi_frame_info: structure of frame info
 * @src_subcr_params: subscription params to fw on input port
 * @dst_subcr_params: subscription params to fw on output port
 * @dpb_list_payload: array of dpb buffers
 * @once_per_session_set: boolean to set once per session property
 * @max_rate: max input rate
 * @max_input_data_size: max size of input data
 * @power: structure of power info
 * @bus_data: structure of bus data
 * @input_timer_list: list head of input timer
 * @ir_enabled: boolean for intra refresh
 * @hfi_rc_type: rate control type
 * @hfi_layer_type: type of HFI layer encoding
 * @has_bframe: boolean to set if bitstream contains B frame
 * @iframe: boolean to set if bitstream contains I frame
 */

struct iris_inst {
	struct list_head		list;
	struct iris_core		*core;
	u32				session_id;
	struct vb2_queue		*vb2q_src;
	struct vb2_queue		*vb2q_dst;
	struct mutex			ctx_q_lock;/* lock to serialize queues related ioctls */
	struct mutex			lock;
	struct v4l2_fh			fh;
	struct v4l2_format		*fmt_src;
	struct v4l2_format		*fmt_dst;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct rect_desc		crop;
	struct rect_desc		compose;
	void				*packet;
	u32				packet_size;
	struct completion		completions[MAX_SIGNAL];
	struct plat_inst_cap		cap[INST_CAP_MAX + 1];
	u32				num_ctrls;
	struct list_head		caps_list;
	enum codec_type			codec;
	enum domain_type		domain;
	struct iris_mem_pool		*mem_pool;
	struct iris_buffers_info	buffers;
	u32				fw_min_count;
	enum iris_inst_state		state;
	enum iris_inst_sub_state	sub_state;
	bool				ipsc_properties_set;
	bool				opsc_properties_set;
	struct iris_hfi_frame_info	hfi_frame_info;
	struct subscription_params	src_subcr_params;
	struct subscription_params	dst_subcr_params;
	u32				dpb_list_payload[MAX_DPB_LIST_ARRAY_SIZE];
	bool				once_per_session_set;
	u32				max_rate;
	u32				max_input_data_size;
	struct iris_inst_power		power;
	struct bus_vote_data		bus_data;
	struct list_head		input_timer_list;
	bool				ir_enabled;
	u32				hfi_rc_type;
	u32				hfi_layer_type;
	bool				has_bframe;
	bool				iframe;
};

#endif
