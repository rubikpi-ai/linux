/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Virtio-iommu definition v0.12
 *
 * Copyright (C) 2019 Arm Ltd.
 */
#ifndef _UAPI_LINUX_VIRTIO_IOMMU_H
#define _UAPI_LINUX_VIRTIO_IOMMU_H

#include <linux/types.h>

/* Feature bits */
#define VIRTIO_IOMMU_F_INPUT_RANGE		0
#define VIRTIO_IOMMU_F_DOMAIN_RANGE		1
#define VIRTIO_IOMMU_F_MAP_UNMAP		2
#define VIRTIO_IOMMU_F_BYPASS			3
#define VIRTIO_IOMMU_F_PROBE			4
#define VIRTIO_IOMMU_F_MMIO			5
#define VIRTIO_IOMMU_F_BYPASS_CONFIG		6
#define VIRTIO_IOMMU_F_ATTACH_TABLE		7

struct virtio_iommu_range_64 {
	__le64					start;
	__le64					end;
};

struct virtio_iommu_range_32 {
	__le32					start;
	__le32					end;
};

struct virtio_iommu_config {
	/* Supported page sizes */
	__le64					page_size_mask;
	/* Supported IOVA range */
	struct virtio_iommu_range_64		input_range;
	/* Max domain ID size */
	struct virtio_iommu_range_32		domain_range;
	/* Probe buffer size */
	__le32					probe_size;
	__u8					bypass;
	__u8					reserved[3];
};

/* Request types */
#define VIRTIO_IOMMU_T_ATTACH			0x01
#define VIRTIO_IOMMU_T_DETACH			0x02
#define VIRTIO_IOMMU_T_MAP			0x03
#define VIRTIO_IOMMU_T_UNMAP			0x04
#define VIRTIO_IOMMU_T_PROBE			0x05
#define VIRTIO_IOMMU_T_ATTACH_TABLE		0x06
#define VIRTIO_IOMMU_T_INVALIDATE		0x07

/* Status types */
#define VIRTIO_IOMMU_S_OK			0x00
#define VIRTIO_IOMMU_S_IOERR			0x01
#define VIRTIO_IOMMU_S_UNSUPP			0x02
#define VIRTIO_IOMMU_S_DEVERR			0x03
#define VIRTIO_IOMMU_S_INVAL			0x04
#define VIRTIO_IOMMU_S_RANGE			0x05
#define VIRTIO_IOMMU_S_NOENT			0x06
#define VIRTIO_IOMMU_S_FAULT			0x07
#define VIRTIO_IOMMU_S_NOMEM			0x08

struct virtio_iommu_req_head {
	__u8					type;
	__u8					reserved[3];
};

struct virtio_iommu_req_tail {
	__u8					status;
	__u8					reserved[3];
};

#define VIRTIO_IOMMU_ATTACH_F_BYPASS		(1 << 0)

struct virtio_iommu_req_attach {
	struct virtio_iommu_req_head		head;
	__le32					domain;
	__le32					endpoint;
	__le32					flags;
	__u8					reserved[4];
	struct virtio_iommu_req_tail		tail;
};

#define VIRTIO_IOMMU_ATTACH_TABLE_ARM_SMMU3	0x1
#define VIRTIO_IOMMU_ATTACH_TABLE_INTEL_PT	0x2
#define VIRTIO_IOMMU_ATTACH_TABLE_RISCV		0x4
#define VIRTIO_IOMMU_ATTACH_TABLE_AMD_GCR3	0x5
#define VIRTIO_IOMMU_ATTACH_TABLE_AMD_PT	0x6

struct virtio_iommu_req_attach_table {
	struct virtio_iommu_req_head	head;
	__le32				domain;
	__le32				endpoint;
	__u8				format;
	__u8				descriptor[111];
	struct virtio_iommu_req_tail	tail;
};

struct virtio_iommu_req_detach {
	struct virtio_iommu_req_head		head;
	__le32					domain;
	__le32					endpoint;
	__u8					reserved[8];
	struct virtio_iommu_req_tail		tail;
};

#define VIRTIO_IOMMU_MAP_F_READ			(1 << 0)
#define VIRTIO_IOMMU_MAP_F_WRITE		(1 << 1)
#define VIRTIO_IOMMU_MAP_F_MMIO			(1 << 2)

#define VIRTIO_IOMMU_MAP_F_MASK			(VIRTIO_IOMMU_MAP_F_READ |	\
						 VIRTIO_IOMMU_MAP_F_WRITE |	\
						 VIRTIO_IOMMU_MAP_F_MMIO)

struct virtio_iommu_req_map {
	struct virtio_iommu_req_head		head;
	__le32					domain;
	__le64					virt_start;
	__le64					virt_end;
	__le64					phys_start;
	__le32					flags;
	struct virtio_iommu_req_tail		tail;
};

struct virtio_iommu_req_unmap {
	struct virtio_iommu_req_head		head;
	__le32					domain;
	__le64					virt_start;
	__le64					virt_end;
	__u8					reserved[4];
	struct virtio_iommu_req_tail		tail;
};

/* Should we define that bits[15:0] of id are asid for arm64? */
#define VIRTIO_IOMMU_INVAL_S_DOMAIN	0x1
#define VIRTIO_IOMMU_INVAL_S_PASID	0x2
#define VIRTIO_IOMMU_INVAL_S_ADDRESS	0x3

#define VIRTIO_IOMMU_INVAL_C_PASID	(1 << 0)
#define VIRTIO_IOMMU_INVAL_C_TLB	(1 << 1)

#define VIRTIO_IOMMU_INVAL_F_LEAF	(1 << 0)
#define VIRTIO_IOMMU_INVAL_F_PASID	(1 << 1)
#define VIRTIO_IOMMU_INVAL_F_ID		(1 << 2)
#define VIRTIO_IOMMU_INVAL_F_GLOBAL	(1 << 3)

struct virtio_iommu_req_invalidate {
	struct virtio_iommu_req_head	head;
	__u8	scope;
	__u8	caches;
	__le16	flags;
	__le32	domain;
	__le32	pasid;
	__le64	id;
	__le64	address;
	__le64	nr_pages;
	__u8	page_size;
	__u8	reserved[19];
	struct virtio_iommu_req_tail	tail;
};


#define VIRTIO_IOMMU_PROBE_T_NONE		0
#define VIRTIO_IOMMU_PROBE_T_RESV_MEM		1
#define VIRTIO_IOMMU_PROBE_T_HW_ARM_SMMU3	2
#define VIRTIO_IOMMU_PROBE_T_HW_INTEL_VTD	3
#define VIRTIO_IOMMU_PROBE_T_HW_RISCV		4
#define VIRTIO_IOMMU_PROBE_T_HW_AMD		5


#define VIRTIO_IOMMU_PROBE_T_MASK		0xfff

struct virtio_iommu_probe_property {
	__le16					type;
	__le16					length;
};

#define VIRTIO_IOMMU_RESV_MEM_T_RESERVED	0
#define VIRTIO_IOMMU_RESV_MEM_T_MSI		1

struct virtio_iommu_probe_resv_mem {
	struct virtio_iommu_probe_property	head;
	__u8					subtype;
	__u8					reserved[3];
	__le64					start;
	__le64					end;
};

struct virtio_iommu_req_probe {
	struct virtio_iommu_req_head		head;
	__le32					endpoint;
	__u8					reserved[64];

	__u8					properties[];

	/*
	 * Tail follows the variable-length properties array. No padding,
	 * property lengths are all aligned on 8 bytes.
	 */
};

/* Fault types */
#define VIRTIO_IOMMU_FAULT_R_UNKNOWN		0
#define VIRTIO_IOMMU_FAULT_R_DOMAIN		1
#define VIRTIO_IOMMU_FAULT_R_MAPPING		2

#define VIRTIO_IOMMU_FAULT_F_READ		(1 << 0)
#define VIRTIO_IOMMU_FAULT_F_WRITE		(1 << 1)
#define VIRTIO_IOMMU_FAULT_F_EXEC		(1 << 2)
#define VIRTIO_IOMMU_FAULT_F_ADDRESS		(1 << 8)
#define VIRTIO_IOMMU_FAULT_F_PASID		(1 << 9)

struct virtio_iommu_fault {
	__u8					reason;
	__u8					reserved[3];
	__le32					flags;
	__le32					endpoint;
	__le32					pasid;
	__le64					address;
};

/* ARM_SMMU_V3 Acceleration */
struct virtio_iommu_probe_hw_arm_smmu3 {
	struct virtio_iommu_probe_property head;
	__u8 reserved[4];
	__le64 idr0;
	__le64 idr1;
	__le64 reserved2;
	__le64 idr3;
	__le64 reserved4;
	__le64 idr5;
};

#define VIRTIO_IOMMU_HW_ARM_STE0_S1FMT_SHIFT	4
#define VIRTIO_IOMMU_HW_ARM_STE0_S1FMT_MASK	0x3
#define VIRTIO_IOMMU_HW_ARM_STE0_S1FMT_LINEAR	0
#define VIRTIO_IOMMU_HW_ARM_STE0_S1FMT_4KL2i	1
#define VIRTIO_IOMMU_HW_ARM_STE0_S1FMT_64KL2	2

#define VIRTIO_IOMMU_HW_ARM_STE0_S1PTR_MASK	0xfffffffffffc0

#define VIRTIO_IOMMU_HW_ARM_STE0_S1CDMAX_SHIFT	59
#define VIRTIO_IOMMU_HW_ARM_STE0_S1CDMAX_MASK	0x1f

#define VIRTIO_IOMMU_HW_ARM_STE1_S1DSS_SHIFT	0
#define VIRTIO_IOMMU_HW_ARM_STE1_S1DSS_MASK	0x3
#define VIRTIO_IOMMU_HW_ARM_STE1_S1DSS_TERM	0
#define VIRTIO_IOMMU_HW_ARM_STE1_S1DSS_BYPASS	1
#define VIRTIO_IOMMU_HW_ARM_STE1_S1DSS_SSZERO	2


struct virtio_iommu_req_attach_table_arm_smmu3 {
	struct virtio_iommu_req_head	head;
	__le32				domain;
	__le32				endpoint;
	__u8				format;
	__u8				reserved0[3];
	__le64				ste0;
	__le64				ste1;
	__u8				reserved1[92];
	struct virtio_iommu_req_tail	tail;
};

#define VIRTIO_IOMMU_HW_ARM_INVALIDATE_ID_ASID	0xffff
#endif
