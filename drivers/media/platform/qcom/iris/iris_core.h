/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_CORE_H_
#define _IRIS_CORE_H_

#include <linux/types.h>
#include <media/v4l2-device.h>

#include "iris_hfi_queue.h"
#include "iris_state.h"
#include "platform_common.h"
#include "resources.h"
#include "vpu_common.h"

/**
 * struct iris_core - holds core parameters valid for all instances
 *
 * @dev: reference to device structure
 * @reg_base: IO memory base address
 * @irq: iris irq
 * @v4l2_dev: a holder for v4l2 device structure
 * @vdev_dec: iris video device structure for decoder
 * @vdev_enc: iris video device structure for encoder
 * @v4l2_file_ops: iris v4l2 file ops
 * @v4l2_ioctl_ops_dec: iris v4l2 ioctl ops for decoder
 * @v4l2_ioctl_ops_enc: iris v4l2 ioctl ops for encoder
 * @bus_tbl: table of iris buses
 * @bus_count: count of iris buses
 * @power_domain_tbl: table of iris power domains
 * @pd_count: count of iris power domains
 * @clock_tbl: table of iris clocks
 * @clk_count: count of iris clocks
 * @reset_tbl: table of iris reset clocks
 * @reset_count: count of iris reset clocks
 * @vb2_ops: iris vb2 ops
 * @vb2_mem_ops: iris vb2 memory ops
 * @state: current state of core
 * @iface_q_table: Interface queue table memory
 * @command_queue: shared interface queue to send commands to firmware
 * @message_queue: shared interface queue to receive responses from firmware
 * @debug_queue: shared interface queue to receive debug info from firmware
 * @sfr: SFR register memory
 * @lock: a lock for this strucure
 * @packet: pointer to packet from driver to fw
 * @packet_size: size of packet
 * @response_packet: a pointer to response packet from fw to driver
 * @sys_init_id: id of sys init packet
 * @header_id: id of packet header
 * @packet_id: id of packet
 * @vpu_ops: a pointer to vpu ops
 * @session_ops: a pointer to session level ops
 * @enc_codecs_count: supported codec count for encoder
 * @dec_codecs_count: supported codec count for decoder
 * @platform_data: a structure for platform data
 * @cap: an array for supported core capabilities
 * @inst_caps: a pointer to supported instance capabilities
 * @instances: a list_head of all instances
 * @intr_status: interrupt status
 * @spur_count: counter for spurious interrupt
 * @reg_count: counter for interrupts
 * @fw_version: firmware version
 * @skip_pc_count: a counter to retry power collapse
 * @power_enabled: a boolean to check if power is on or off
 * @power: a structure for clock and bw information
 * @num_pending_requests: a reference counter for calling pm operations
 */

struct iris_core {
	struct device				*dev;
	void __iomem				*reg_base;
	int					irq;
	struct v4l2_device			v4l2_dev;
	struct video_device			*vdev_dec;
	struct video_device			*vdev_enc;
	const struct v4l2_file_operations	*v4l2_file_ops;
	const struct v4l2_ioctl_ops		*v4l2_ioctl_ops_dec;
	const struct v4l2_ioctl_ops		*v4l2_ioctl_ops_enc;
	struct bus_info				*bus_tbl;
	u32					bus_count;
	struct power_domain_info		*power_domain_tbl;
	u32					pd_count;
	struct clock_info			*clock_tbl;
	u32					clk_count;
	struct reset_info			*reset_tbl;
	u32					reset_count;
	const struct vb2_ops			*vb2_ops;
	struct vb2_mem_ops			*vb2_mem_ops;
	enum iris_core_state			state;
	struct mem_desc				iface_q_table;
	struct iface_q_info			command_queue;
	struct iface_q_info			message_queue;
	struct iface_q_info			debug_queue;
	struct mem_desc				sfr;
	struct mutex				lock; /* lock for core structure */
	u8					*packet;
	u32					packet_size;
	u8					*response_packet;
	u32					sys_init_id;
	u32					header_id;
	u32					packet_id;
	const struct vpu_ops			*vpu_ops;
	const struct vpu_session_ops		*session_ops;
	u32					dec_codecs_count;
	u32					enc_codecs_count;
	struct platform_data			*platform_data;
	struct plat_core_cap			cap[CORE_CAP_MAX + 1];
	struct plat_inst_caps			*inst_caps;
	struct list_head			instances;
	u32					intr_status;
	u32					spur_count;
	u32					reg_count;
	char					fw_version[IRIS_VERSION_LENGTH];
	u32					skip_pc_count;
	bool					power_enabled;
	struct iris_core_power			power;
	s32					num_pending_requests;
};

int iris_core_init(struct iris_core *core);
int iris_core_init_wait(struct iris_core *core);
int iris_core_deinit(struct iris_core *core);
int iris_core_deinit_locked(struct iris_core *core);

#endif
