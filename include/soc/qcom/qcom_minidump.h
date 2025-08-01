/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_MINIDUMP_H_
#define _QCOM_MINIDUMP_H_

#define MAX_NAME_LENGTH		12

/**
 * struct qcom_minidump_region - APSS Minidump region information
 *
 * @name:	Entry name, Minidump will dump binary with this name.
 * @phys_addr:	Physical address of the entry to dump.
 * @virt_addr:  Virtual address of the entry.
 * @size:	Number of byte to dump from @address location,
 *		and it should be 4 byte aligned.
 */
struct qcom_minidump_region {
	char name[MAX_NAME_LENGTH];
	phys_addr_t phys_addr;
	void *virt_addr;
	size_t size;
};

struct rproc;
struct rproc_dump_segment;

#if IS_ENABLED(CONFIG_QCOM_RPROC_MINIDUMP)
void qcom_rproc_minidump(struct rproc *rproc, unsigned int minidump_id,
		   void (*rproc_dumpfn_t)(struct rproc *rproc,
		   struct rproc_dump_segment *segment, void *dest, size_t offset,
		   size_t size));
#else
static inline void qcom_rproc_minidump(struct rproc *rproc, unsigned int minidump_id,
		   void (*rproc_dumpfn_t)(struct rproc *rproc,
		   struct rproc_dump_segment *segment, void *dest, size_t offset,
		   size_t size)) { }
#endif /* CONFIG_QCOM_RPROC_MINIDUMP */
#endif /* _QCOM_MINIDUMP_H_ */
