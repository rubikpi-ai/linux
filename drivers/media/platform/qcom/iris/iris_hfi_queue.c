// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hfi_defines.h"
#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_hfi_packet.h"
#include "iris_hfi_queue.h"

static void __set_queue_hdr_defaults(struct hfi_queue_header *q_hdr)
{
	q_hdr->qhdr_status = 0x1;
	q_hdr->qhdr_type = IFACEQ_DFLT_QHDR;
	q_hdr->qhdr_q_size = IFACEQ_QUEUE_SIZE / 4;
	q_hdr->qhdr_pkt_size = 0;
	q_hdr->qhdr_rx_wm = 0x1;
	q_hdr->qhdr_tx_wm = 0x1;
	q_hdr->qhdr_rx_req = 0x1;
	q_hdr->qhdr_tx_req = 0x0;
	q_hdr->qhdr_rx_irq_status = 0x0;
	q_hdr->qhdr_tx_irq_status = 0x0;
	q_hdr->qhdr_read_idx = 0x0;
	q_hdr->qhdr_write_idx = 0x0;
}

static int __write_queue(struct iface_q_info *qinfo, void *packet)
{
	u32 empty_space, read_idx, write_idx, new_write_idx;
	struct hfi_queue_header *queue;
	struct hfi_header *header;
	u32 packet_size, residue;
	u32 *write_ptr;

	queue = qinfo->qhdr;
	if (!queue)
		return -EINVAL;

	header = packet;
	packet_size = header->size;

	if (!packet_size || packet_size > qinfo->q_array.size)
		return -EINVAL;

	read_idx = queue->qhdr_read_idx * sizeof(u32);
	write_idx = queue->qhdr_write_idx * sizeof(u32);

	empty_space = (write_idx >=  read_idx) ?
		(qinfo->q_array.size - (write_idx -  read_idx)) :
		(read_idx - write_idx);
	if (empty_space <= packet_size) {
		queue->qhdr_tx_req = 1;
		return -ENOSPC;
	}

	queue->qhdr_tx_req =  0;

	new_write_idx = write_idx + packet_size;
	write_ptr = (u32 *)((u8 *)qinfo->q_array.kernel_vaddr + write_idx);

	if (write_ptr < (u32 *)qinfo->q_array.kernel_vaddr ||
	    write_ptr > (u32 *)(qinfo->q_array.kernel_vaddr +
	    qinfo->q_array.size))
		return -EINVAL;

	if (new_write_idx < qinfo->q_array.size) {
		memcpy(write_ptr, packet, packet_size);
	} else {
		residue = new_write_idx - qinfo->q_array.size;
		memcpy(write_ptr, packet, (packet_size - residue));
		memcpy(qinfo->q_array.kernel_vaddr,
		       packet + (packet_size - residue), residue);
		new_write_idx = residue;
	}

	/* Make sure packet is written before updating the write index */
	mb();
	queue->qhdr_write_idx = new_write_idx / sizeof(u32);

	/* Make sure write index is updated before an interrupt is raised */
	mb();

	return 0;
}

static int __read_queue(struct iface_q_info *qinfo, void *packet)
{
	u32 read_idx, write_idx, new_read_idx;
	struct hfi_queue_header *queue;
	u32 receive_request = 0;
	u32 packet_size, residue;
	u32 *read_ptr;
	int ret = 0;

	/* Make sure data is valid before reading it */
	mb();
	queue = qinfo->qhdr;
	if (!queue)
		return -EINVAL;

	if (queue->qhdr_type & IFACEQ_MSGQ_ID)
		receive_request = 1;

	read_idx = queue->qhdr_read_idx * sizeof(u32);
	write_idx = queue->qhdr_write_idx * sizeof(u32);

	if (read_idx == write_idx) {
		queue->qhdr_rx_req = receive_request;
		/* Ensure qhdr is updated in main memory */
		mb();
		return -ENODATA;
	}

	read_ptr = (u32 *)(qinfo->q_array.kernel_vaddr + read_idx);
	if (read_ptr < (u32 *)qinfo->q_array.kernel_vaddr ||
	    read_ptr > (u32 *)(qinfo->q_array.kernel_vaddr +
	    qinfo->q_array.size - sizeof(*read_ptr)))
		return -ENODATA;

	packet_size = *read_ptr;
	if (!packet_size)
		return -EINVAL;

	new_read_idx = read_idx + packet_size;
	if (packet_size <= IFACEQ_CORE_PKT_SIZE &&
	    read_idx <= qinfo->q_array.size) {
		if (new_read_idx < qinfo->q_array.size) {
			memcpy(packet, read_ptr, packet_size);
		} else {
			residue = new_read_idx - qinfo->q_array.size;
			memcpy(packet, read_ptr, (packet_size - residue));
			memcpy((packet + (packet_size - residue)),
			       qinfo->q_array.kernel_vaddr, residue);
			new_read_idx = residue;
		}
	} else {
		new_read_idx = write_idx;
		ret = -EBADMSG;
	}

	queue->qhdr_rx_req = receive_request;

	queue->qhdr_read_idx = new_read_idx / sizeof(u32);
	/* Ensure qhdr is updated in main memory */
	mb();

	return ret;
}

int iris_hfi_queue_cmd_write(struct iris_core *core, void *pkt)
{
	struct iface_q_info *q_info;
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (!core_in_valid_state(core))
		return -EINVAL;

	q_info = &core->command_queue;
	if (!q_info || !q_info->q_array.kernel_vaddr || !pkt) {
		dev_err(core->dev, "cannot write to shared CMD Q's\n");
		return -ENODATA;
	}

	if (!__write_queue(q_info, pkt)) {
		call_vpu_op(core, raise_interrupt, core);
	} else {
		dev_err(core->dev, "queue full\n");
		return -ENODATA;
	}

	return ret;
}

int iris_hfi_queue_msg_read(struct iris_core *core, void *pkt)
{
	struct iface_q_info *q_info;

	if (!core_in_valid_state(core))
		return -EINVAL;

	q_info = &core->message_queue;
	if (!q_info || !q_info->q_array.kernel_vaddr || !pkt) {
		dev_err(core->dev, "cannot read from shared MSG Q's\n");
		return -ENODATA;
	}

	if (__read_queue(q_info, pkt))
		return -ENODATA;

	return 0;
}

int iris_hfi_queue_dbg_read(struct iris_core *core, void *pkt)
{
	struct iface_q_info *q_info;

	q_info = &core->debug_queue;
	if (!q_info || !q_info->q_array.kernel_vaddr || !pkt) {
		dev_err(core->dev, "cannot read from shared DBG Q's\n");
		return -ENODATA;
	}

	if (__read_queue(q_info, pkt))
		return -ENODATA;

	return 0;
}

static void iris_hfi_set_queue_header(struct iris_core *core, u32 queue_id,
				      struct iface_q_info *iface_q)
{
	__set_queue_hdr_defaults(iface_q->qhdr);
	iface_q->qhdr->qhdr_start_addr = iface_q->q_array.device_addr;
	iface_q->qhdr->qhdr_type |= queue_id;

	/*
	 * Set receive request to zero on debug queue as there is no
	 * need of interrupt from video hardware for debug messages
	 */
	if (queue_id == IFACEQ_DBGQ_ID)
		iface_q->qhdr->qhdr_rx_req = 0;
}

static void __queue_init(struct iris_core *core, u32 queue_id, struct iface_q_info *iface_q)
{
	unsigned int offset = 0;

	offset = core->iface_q_table.size + (queue_id * IFACEQ_QUEUE_SIZE);
	iface_q->q_array.device_addr = core->iface_q_table.device_addr + offset;
	iface_q->q_array.kernel_vaddr =
			(void *)((char *)core->iface_q_table.kernel_vaddr + offset);
	iface_q->q_array.size = IFACEQ_QUEUE_SIZE;
	iface_q->qhdr =
		IFACEQ_GET_QHDR_START_ADDR(core->iface_q_table.kernel_vaddr, queue_id);

	iris_hfi_set_queue_header(core, queue_id, iface_q);
}

int iris_hfi_queue_init(struct iris_core *core)
{
	struct hfi_queue_table_header *q_tbl_hdr;

	if (core->iface_q_table.kernel_vaddr) {
		iris_hfi_set_queue_header(core, IFACEQ_CMDQ_ID, &core->command_queue);
		iris_hfi_set_queue_header(core, IFACEQ_MSGQ_ID, &core->message_queue);
		iris_hfi_set_queue_header(core, IFACEQ_DBGQ_ID, &core->debug_queue);
		return 0;
	}

	core->iface_q_table.kernel_vaddr = dma_alloc_attrs(core->dev, ALIGNED_QUEUE_SIZE,
							   &core->iface_q_table.device_addr,
							   GFP_KERNEL, DMA_ATTR_WRITE_COMBINE);
	if (!core->iface_q_table.kernel_vaddr) {
		dev_err(core->dev, "%s: queues alloc and map failed\n", __func__);
		return -ENOMEM;
	}
	core->iface_q_table.size = IFACEQ_TABLE_SIZE;

	__queue_init(core, IFACEQ_CMDQ_ID, &core->command_queue);
	__queue_init(core, IFACEQ_MSGQ_ID, &core->message_queue);
	__queue_init(core, IFACEQ_DBGQ_ID, &core->debug_queue);

	q_tbl_hdr = (struct hfi_queue_table_header *)
			core->iface_q_table.kernel_vaddr;
	q_tbl_hdr->qtbl_version = 0;
	q_tbl_hdr->device_addr = (void *)core;
	strscpy(q_tbl_hdr->name, "iris-hfi-queues", sizeof(q_tbl_hdr->name));
	q_tbl_hdr->qtbl_size = IFACEQ_TABLE_SIZE;
	q_tbl_hdr->qtbl_qhdr0_offset = sizeof(*q_tbl_hdr);
	q_tbl_hdr->qtbl_qhdr_size = sizeof(struct hfi_queue_header);
	q_tbl_hdr->qtbl_num_q = IFACEQ_NUMQ;
	q_tbl_hdr->qtbl_num_active_q = IFACEQ_NUMQ;

	core->sfr.kernel_vaddr = dma_alloc_attrs(core->dev, ALIGNED_SFR_SIZE,
						 &core->sfr.device_addr,
						 GFP_KERNEL, DMA_ATTR_WRITE_COMBINE);
	if (!core->sfr.kernel_vaddr) {
		dev_err(core->dev, "%s: sfr alloc and map failed\n", __func__);
		return -ENOMEM;
	}
	core->sfr.size = ALIGNED_SFR_SIZE;
	 /* Write sfr size in first word to be used by firmware */
	*((u32 *)core->sfr.kernel_vaddr) = core->sfr.size;

	return 0;
}

static void __queue_deinit(struct iface_q_info *iface_q)
{
	iface_q->qhdr = NULL;
	iface_q->q_array.kernel_vaddr = NULL;
	iface_q->q_array.device_addr = 0;
}

void iris_hfi_queue_deinit(struct iris_core *core)
{
	if (!core->iface_q_table.kernel_vaddr)
		return;

	dma_free_attrs(core->dev, core->iface_q_table.size, core->iface_q_table.kernel_vaddr,
		       core->iface_q_table.device_addr, core->iface_q_table.attrs);

	dma_free_attrs(core->dev, core->sfr.size, core->sfr.kernel_vaddr,
		       core->sfr.device_addr, core->sfr.attrs);

	__queue_deinit(&core->command_queue);
	__queue_deinit(&core->message_queue);
	__queue_deinit(&core->debug_queue);

	core->iface_q_table.kernel_vaddr = NULL;
	core->iface_q_table.device_addr = 0;

	core->sfr.kernel_vaddr = NULL;
	core->sfr.device_addr = 0;
}

void iris_flush_debug_queue(struct iris_core *core,
			    u8 *packet, u32 packet_size)
{
	struct hfi_debug_header *pkt;
	bool local_packet = false;
	u8 *log;

	if (!packet || !packet_size) {
		packet = kzalloc(IFACEQ_CORE_PKT_SIZE, GFP_KERNEL);
		if (!packet)
			return;

		packet_size = IFACEQ_CORE_PKT_SIZE;

		local_packet = true;
	}

	while (!iris_hfi_queue_dbg_read(core, packet)) {
		pkt = (struct hfi_debug_header *)packet;

		if (pkt->size < sizeof(*pkt))
			continue;

		if (pkt->size >= packet_size)
			continue;

		packet[pkt->size] = '\0';
		log = (u8 *)packet + sizeof(*pkt) + 1;
		dev_dbg(core->dev, "%s", log);
	}

	if (local_packet)
		kfree(packet);
}
