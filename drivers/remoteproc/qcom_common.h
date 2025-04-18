/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RPROC_QCOM_COMMON_H__
#define __RPROC_QCOM_COMMON_H__

#include <linux/remoteproc.h>
#include "remoteproc_internal.h"
#include <linux/soc/qcom/qmi.h>

struct qcom_glink_smem;
struct qcom_sysmon;

struct qcom_rproc_glink {
	struct rproc_subdev subdev;

	const char *ssr_name;

	struct device *dev;
	struct device_node *node;
	struct qcom_glink_smem *edge;
};

struct qcom_rproc_subdev {
	struct rproc_subdev subdev;

	struct device *dev;
	struct device_node *node;
	struct qcom_smd_edge *edge;
};

struct qcom_ssr_subsystem;

struct qcom_rproc_ssr {
	struct rproc_subdev subdev;
	struct qcom_ssr_subsystem *info;
};

/**
 * struct qcom_devmem_info - iommu devmem region
 * @da: device address
 * @pa: physical address
 * @len: length (in bytes)
 * @flags: iommu protection flags
 *
 * The resource entry carries the device address to which a physical address is
 * to be mapped with required permissions in flag. The pa, len is expected to
 * be a physically contiguous memory region.
 */
struct qcom_devmem_info {
	u64 da;
	u64 pa;
	u32 len;
	u32 flags;
};

/**
 * struct qcom_devmem_table - iommu devmem entries
 * @num_entries: number of devmem entries
 * @entries: devmem entries
 *
 * The table that carries each devmem resource entry.
 */
struct qcom_devmem_table {
	int num_entries;
	struct qcom_devmem_info entries[];
};

void qcom_minidump(struct rproc *rproc, unsigned int minidump_id,
			void (*rproc_dumpfn_t)(struct rproc *rproc,
				struct rproc_dump_segment *segment, void *dest, size_t offset,
				size_t size));

void qcom_add_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink,
			   const char *ssr_name);
void qcom_remove_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink);

int qcom_register_dump_segments(struct rproc *rproc, const struct firmware *fw);

void qcom_add_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd);
void qcom_remove_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd);

void qcom_add_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr,
			 const char *ssr_name);
void qcom_remove_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr);

int qcom_map_unmap_carveout(struct rproc *rproc, phys_addr_t mem_phys, size_t mem_size,
			    bool map, bool use_sid, unsigned long sid);

int qcom_map_devmem(struct rproc *rproc, struct qcom_devmem_table *table,
		    bool use_sid, unsigned long sid);
void qcom_unmap_devmem(struct rproc *rproc, struct qcom_devmem_table *table,
		       bool use_sid);

#if IS_ENABLED(CONFIG_QCOM_SYSMON)
struct qcom_sysmon *qcom_add_sysmon_subdev(struct rproc *rproc,
					   const char *name,
					   int ssctl_instance);
void qcom_remove_sysmon_subdev(struct qcom_sysmon *sysmon);
bool qcom_sysmon_shutdown_acked(struct qcom_sysmon *sysmon);
#else
static inline struct qcom_sysmon *qcom_add_sysmon_subdev(struct rproc *rproc,
							 const char *name,
							 int ssctl_instance)
{
	return NULL;
}

static inline void qcom_remove_sysmon_subdev(struct qcom_sysmon *sysmon)
{
}

static inline bool qcom_sysmon_shutdown_acked(struct qcom_sysmon *sysmon)
{
	return false;
}
#endif

#endif
