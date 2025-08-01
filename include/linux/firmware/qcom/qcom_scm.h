/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2010-2015, 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __QCOM_SCM_H
#define __QCOM_SCM_H

#include <linux/err.h>
#include <linux/types.h>
#include <linux/cpumask.h>

#include <dt-bindings/firmware/qcom,scm.h>
#ifdef CONFIG_QCOM_SCM_ADDON
#include "qcom_scm_addon.h"
#endif

#define QCOM_SCM_VERSION(major, minor)	(((major) << 16) | ((minor) & 0xFF))
#define QCOM_SCM_CPU_PWR_DOWN_L2_ON	0x0
#define QCOM_SCM_CPU_PWR_DOWN_L2_OFF	0x1
#define QCOM_SCM_HDCP_MAX_REQ_CNT	5

struct qcom_scm_hdcp_req {
	u32 addr;
	u32 val;
};

struct qcom_scm_vmperm {
	int vmid;
	int perm;
};

enum qcom_scm_ocmem_client {
	QCOM_SCM_OCMEM_UNUSED_ID = 0x0,
	QCOM_SCM_OCMEM_GRAPHICS_ID,
	QCOM_SCM_OCMEM_VIDEO_ID,
	QCOM_SCM_OCMEM_LP_AUDIO_ID,
	QCOM_SCM_OCMEM_SENSORS_ID,
	QCOM_SCM_OCMEM_OTHER_OS_ID,
	QCOM_SCM_OCMEM_DEBUG_ID,
};

enum qcom_scm_sec_dev_id {
	QCOM_SCM_MDSS_DEV_ID    = 1,
	QCOM_SCM_OCMEM_DEV_ID   = 5,
	QCOM_SCM_PCIE0_DEV_ID   = 11,
	QCOM_SCM_PCIE1_DEV_ID   = 12,
	QCOM_SCM_GFX_DEV_ID     = 18,
	QCOM_SCM_UFS_DEV_ID     = 19,
	QCOM_SCM_ICE_DEV_ID     = 20,
};

enum qcom_scm_ice_cipher {
	QCOM_SCM_ICE_CIPHER_AES_128_XTS = 0,
	QCOM_SCM_ICE_CIPHER_AES_128_CBC = 1,
	QCOM_SCM_ICE_CIPHER_AES_256_XTS = 3,
	QCOM_SCM_ICE_CIPHER_AES_256_CBC = 4,
};

struct qcom_scm_current_perm_info {
	__le32 vmid;
	__le32 perm;
	__le64 ctx;
	__le32 ctx_size;
	__le32 unused;
};

struct qcom_scm_mem_map_info {
	__le64 mem_addr;
	__le64 mem_size;
};

#define QCOM_SCM_PERM_READ       0x4
#define QCOM_SCM_PERM_WRITE      0x2
#define QCOM_SCM_PERM_EXEC       0x1
#define QCOM_SCM_PERM_RW (QCOM_SCM_PERM_READ | QCOM_SCM_PERM_WRITE)
#define QCOM_SCM_PERM_RWX (QCOM_SCM_PERM_RW | QCOM_SCM_PERM_EXEC)

static inline void qcom_scm_populate_vmperm_info(
		struct qcom_scm_current_perm_info *destvm, int vmid, int perm)
{
	if (!destvm)
		return;

	destvm->vmid = cpu_to_le32(vmid);
	destvm->perm = cpu_to_le32(perm);
	destvm->ctx = 0;
	destvm->ctx_size = 0;
}

static inline void qcom_scm_populate_mem_map_info(
		struct qcom_scm_mem_map_info *mem_to_map,
		phys_addr_t mem_addr, size_t mem_size)
{
	if (!mem_to_map)
		return;

	mem_to_map->mem_addr = cpu_to_le64(mem_addr);
	mem_to_map->mem_size = cpu_to_le64(mem_size);
}

extern bool qcom_scm_is_available(void);

extern int qcom_scm_set_cold_boot_addr(void *entry);
extern int qcom_scm_set_warm_boot_addr(void *entry);
extern void qcom_scm_cpu_power_down(u32 flags);
extern int qcom_scm_set_remote_state(u32 state, u32 id);

struct qcom_scm_pas_metadata {
	void *ptr;
	dma_addr_t phys;
	ssize_t size;
	bool shm_bridge_needed;
};

extern int qcom_scm_pas_init_image(u32 peripheral, const void *metadata,
				   size_t size,
				   struct qcom_scm_pas_metadata *ctx);
extern void qcom_scm_pas_metadata_release(struct qcom_scm_pas_metadata *ctx);
extern int qcom_scm_pas_mem_setup(u32 peripheral, phys_addr_t addr,
				  phys_addr_t size);
extern int qcom_scm_pas_auth_and_reset(u32 peripheral);
extern int qcom_scm_pas_shutdown(u32 peripheral);
extern bool qcom_scm_pas_supported(u32 peripheral);

extern int qcom_scm_io_readl(phys_addr_t addr, unsigned int *val);
extern int qcom_scm_io_writel(phys_addr_t addr, unsigned int val);

extern bool qcom_scm_restore_sec_cfg_available(void);
extern int qcom_scm_restore_sec_cfg(u32 device_id, u32 spare);
int qcom_scm_set_gpu_smmu_aperture(unsigned int context_bank);
bool qcom_scm_set_gpu_smmu_aperture_is_available(void);
extern int qcom_scm_iommu_secure_ptbl_size(u32 spare, size_t *size);
extern int qcom_scm_iommu_secure_ptbl_init(u64 addr, u32 size, u32 spare);
extern int qcom_scm_iommu_set_cp_pool_size(u32 spare, u32 size);
extern int qcom_scm_mem_protect_video_var(u32 cp_start, u32 cp_size,
					  u32 cp_nonpixel_start,
					  u32 cp_nonpixel_size);
extern int
qcom_scm_assign_mem_regions(struct qcom_scm_mem_map_info *mem_regions,
		size_t mem_regions_sz, u32 *srcvms, size_t src_sz,
		struct qcom_scm_current_perm_info *newvms,
		size_t newvms_sz);
extern int qcom_scm_assign_mem(phys_addr_t mem_addr, size_t mem_sz,
			       u64 *src,
			       const struct qcom_scm_vmperm *newvm,
			       unsigned int dest_cnt);

extern bool qcom_scm_ocmem_lock_available(void);
extern int qcom_scm_ocmem_lock(enum qcom_scm_ocmem_client id, u32 offset,
			       u32 size, u32 mode);
extern int qcom_scm_ocmem_unlock(enum qcom_scm_ocmem_client id, u32 offset,
				 u32 size);

extern bool qcom_scm_ice_available(void);
extern int qcom_scm_ice_invalidate_key(u32 index);
extern int qcom_scm_ice_set_key(u32 index, const u8 *key, u32 key_size,
				enum qcom_scm_ice_cipher cipher,
				u32 data_unit_size);
extern int qcom_scm_derive_sw_secret(const u8 *wkey, size_t wkey_size,
				     u8 *sw_secret, size_t sw_secret_size);
extern int qcom_scm_generate_ice_key(u8 *lt_key, size_t lt_key_size);
extern int qcom_scm_prepare_ice_key(const u8 *lt_key, size_t lt_key_size,
				    u8 *eph_key, size_t eph_size);
extern int qcom_scm_import_ice_key(const u8 *imp_key, size_t imp_size,
				   u8 *lt_key, size_t lt_key_size);
extern bool qcom_scm_hdcp_available(void);
extern int qcom_scm_hdcp_req(struct qcom_scm_hdcp_req *req, u32 req_cnt,
			     u32 *resp);

extern int qcom_scm_iommu_set_pt_format(u32 sec_id, u32 ctx_num, u32 pt_fmt);
extern int qcom_scm_qsmmu500_wait_safe_toggle(bool en);

extern int qcom_scm_lmh_dcvsh(u32 payload_fn, u32 payload_reg, u32 payload_val,
			      u64 limit_node, u32 node_id, u64 version);
extern int qcom_scm_lmh_profile_change(u32 profile_id);
extern bool qcom_scm_lmh_dcvsh_available(void);

int qcom_scm_shm_bridge_enable(void);
int qcom_scm_shm_bridge_create(struct device *dev, u64 pfn_and_ns_perm_flags,
			       u64 ipfn_and_s_perm_flags, u64 size_and_flags,
			       u64 ns_vmids, u64 *handle);
int qcom_scm_shm_bridge_delete(struct device *dev, u64 handle);

#endif
