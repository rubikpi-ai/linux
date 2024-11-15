/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_HFI_QUEUE_H_
#define _IRIS_HFI_QUEUE_H_

struct iris_core;

/*
 * Shared queues are used for communication between driver and firmware.
 * There are 3 types of queues:
 * Command queue - driver to write any command to firmware.
 * Message queue - firmware to send any response to driver.
 * Debug queue - firmware to write debug message.
 */

/* Host-firmware shared queue ids */
enum iris_iface_queue {
	IFACEQ_CMDQ_ID	= 0,
	IFACEQ_MSGQ_ID,
	IFACEQ_DBGQ_ID,
	IFACEQ_NUMQ, /* not an index */
};

#define IFACEQ_MAX_BUF_COUNT		50
/*
 * Max session supported are 16.
 * this value is used to calcualte the size of
 * individual shared queue.
 */
#define IFACE_MAX_PARALLEL_CLNTS	16
#define IFACEQ_DFLT_QHDR		0x01010000

#define IFACEQ_MAX_PKT_SIZE		1024
#define IFACEQ_CORE_PKT_SIZE	(1024 * 4)

struct hfi_queue_table_header {
	u32 qtbl_version;
	u32 qtbl_size;
	u32 qtbl_qhdr0_offset;
	u32 qtbl_qhdr_size;
	u32 qtbl_num_q;
	u32 qtbl_num_active_q;
	void *device_addr;
	char name[256];
};

struct hfi_queue_header {
	u32 qhdr_status;
	u32 qhdr_start_addr;
	u32 qhdr_type;
	u32 qhdr_q_size;
	u32 qhdr_pkt_size;
	u32 qhdr_pkt_drop_cnt;
	u32 qhdr_rx_wm;
	u32 qhdr_tx_wm;
	u32 qhdr_rx_req;
	u32 qhdr_tx_req;
	u32 qhdr_rx_irq_status;
	u32 qhdr_tx_irq_status;
	u32 qhdr_read_idx;
	u32 qhdr_write_idx;
};

#define IFACEQ_TABLE_SIZE	(sizeof(struct hfi_queue_table_header) + \
			sizeof(struct hfi_queue_header) * IFACEQ_NUMQ)

#define IFACEQ_QUEUE_SIZE	(IFACEQ_MAX_PKT_SIZE *  \
	IFACEQ_MAX_BUF_COUNT * IFACE_MAX_PARALLEL_CLNTS)

#define IFACEQ_GET_QHDR_START_ADDR(ptr, i)     \
	((void *)(((ptr) + sizeof(struct hfi_queue_table_header)) + \
		((i) * sizeof(struct hfi_queue_header))))

#define SFR_SIZE	SZ_4K
#define QUEUE_SIZE	(IFACEQ_TABLE_SIZE + \
			(IFACEQ_QUEUE_SIZE * IFACEQ_NUMQ))

#define ALIGNED_SFR_SIZE	ALIGN(SFR_SIZE, SZ_4K)
#define ALIGNED_QUEUE_SIZE	ALIGN(QUEUE_SIZE, SZ_4K)
#define SHARED_QSIZE		ALIGN(ALIGNED_SFR_SIZE + ALIGNED_QUEUE_SIZE, SZ_1M)

struct mem_desc {
	dma_addr_t		device_addr;
	void			*kernel_vaddr;
	u32				size;
	unsigned long	attrs;
};

struct iface_q_info {
	struct hfi_queue_header *qhdr;
	struct mem_desc q_array;
};

int iris_hfi_queue_init(struct iris_core *core);
void iris_hfi_queue_deinit(struct iris_core *core);
int iris_hfi_queue_cmd_write(struct iris_core *core, void *pkt);
int iris_hfi_queue_msg_read(struct iris_core *core, void *pkt);
int iris_hfi_queue_dbg_read(struct iris_core *core, void *pkt);
void iris_flush_debug_queue(struct iris_core *core,
			    u8 *packet, u32 packet_size);

#endif
