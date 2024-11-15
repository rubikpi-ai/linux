/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_HFI_PACKET_H_
#define _IRIS_HFI_PACKET_H_

#include "hfi_defines.h"
#include "iris_core.h"
#include "iris_instance.h"

struct hfi_header {
	u32 size;
	u32 session_id;
	u32 header_id;
	u32 reserved[4];
	u32 num_packets;
};

struct hfi_packet {
	u32 size;
	u32 type;
	u32 flags;
	u32 payload_info;
	u32 port;
	u32 packet_id;
	u32 reserved[2];
};

struct hfi_buffer {
	u32 type;
	u32 index;
	u64 base_address;
	u32 addr_offset;
	u32 buffer_size;
	u32 data_offset;
	u32 data_size;
	u64 timestamp;
	u32 flags;
	u32 reserved[5];
};

enum hfi_packet_host_flags {
	HFI_HOST_FLAGS_NONE			= 0x00000000,
	HFI_HOST_FLAGS_INTR_REQUIRED		= 0x00000001,
	HFI_HOST_FLAGS_RESPONSE_REQUIRED	= 0x00000002,
	HFI_HOST_FLAGS_NON_DISCARDABLE		= 0x00000004,
	HFI_HOST_FLAGS_GET_PROPERTY		= 0x00000008,
};

enum hfi_packet_firmware_flags {
	HFI_FW_FLAGS_NONE		= 0x00000000,
	HFI_FW_FLAGS_SUCCESS		= 0x00000001,
	HFI_FW_FLAGS_INFORMATION	= 0x00000002,
	HFI_FW_FLAGS_SESSION_ERROR	= 0x00000004,
	HFI_FW_FLAGS_SYSTEM_ERROR	= 0x00000008,
};

enum hfi_packet_payload_info {
	HFI_PAYLOAD_NONE	= 0x00000000,
	HFI_PAYLOAD_U32		= 0x00000001,
	HFI_PAYLOAD_S32		= 0x00000002,
	HFI_PAYLOAD_U64		= 0x00000003,
	HFI_PAYLOAD_S64		= 0x00000004,
	HFI_PAYLOAD_STRUCTURE	= 0x00000005,
	HFI_PAYLOAD_BLOB	= 0x00000006,
	HFI_PAYLOAD_STRING	= 0x00000007,
	HFI_PAYLOAD_Q16		= 0x00000008,
	HFI_PAYLOAD_U32_ENUM	= 0x00000009,
	HFI_PAYLOAD_32_PACKED	= 0x0000000a,
	HFI_PAYLOAD_U32_ARRAY	= 0x0000000b,
	HFI_PAYLOAD_S32_ARRAY	= 0x0000000c,
	HFI_PAYLOAD_64_PACKED	= 0x0000000d,
};

enum hfi_packet_port_type {
	HFI_PORT_NONE		= 0x00000000,
	HFI_PORT_BITSTREAM	= 0x00000001,
	HFI_PORT_RAW		= 0x00000002,
};

u32 get_hfi_port_from_buffer_type(struct iris_inst *inst, enum iris_buffer_type buffer_type);
u32 get_hfi_port(struct iris_inst *inst, u32 plane);
u32 get_hfi_colorformat(u32 colorformat);
u32 get_hfi_codec(struct iris_inst *inst);
u32 get_hfi_color_primaries(u32 primaries);
u32 get_hfi_transer_char(u32 characterstics);
u32 get_hfi_matrix_coefficients(u32 coefficients);
u32 get_v4l2_color_primaries(u32 hfi_primaries);
u32 get_v4l2_transer_char(u32 hfi_characterstics);
u32 get_v4l2_matrix_coefficients(u32 hfi_coefficients);
u32 hfi_buf_type_to_driver(enum domain_type domain, enum hfi_buffer_type buf_type);
int get_hfi_buffer(struct iris_inst *inst,
		   struct iris_buffer *buffer, struct hfi_buffer *buf);

int hfi_packet_sys_init(struct iris_core *core,
			u8 *pkt, u32 pkt_size);
int hfi_packet_image_version(struct iris_core *core,
			     u8 *pkt, u32 pkt_size);
int hfi_packet_session_command(struct iris_inst *inst, u32 pkt_type,
			       u32 flags, u32 port, u32 session_id,
			       u32 payload_type, void *payload,
			       u32 payload_size);
int hfi_packet_session_property(struct iris_inst *inst,
				u32 pkt_type, u32 flags, u32 port,
				u32 payload_type, void *payload, u32 payload_size);
int hfi_packet_sys_interframe_powercollapse(struct iris_core *core,
					    u8 *pkt, u32 pkt_size);
int hfi_packet_sys_pc_prep(struct iris_core *core,
			   u8 *pkt, u32 pkt_size);
int hfi_create_header(u8 *packet, u32 packet_size, u32 session_id,
		      u32 header_id);
int hfi_create_packet(u8 *packet, u32 packet_size, u32 pkt_type,
		      u32 pkt_flags, u32 payload_type, u32 port,
		      u32 packet_id, void *payload, u32 payload_size);

#endif
