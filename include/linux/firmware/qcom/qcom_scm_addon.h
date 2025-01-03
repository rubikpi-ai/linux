/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_SCM_ADDON_H
#define __QCOM_SCM_ADDON_H

#include <asm-generic/errno-base.h>

#ifdef CONFIG_QCOM_SCM_ADDON

#define QCOM_SCM_CAMERA_MAX_QOS_CNT	2

struct qcom_scm_camera_qos {
	u32 offset;
	u32 val;
};

extern int qcom_scm_camera_update_camnoc_qos(uint32_t use_case_id,
		uint32_t qos_cnt, struct qcom_scm_camera_qos *scm_buf);
extern bool qcom_scm_dcvs_ca_available(void);
extern bool qcom_scm_dcvs_core_available(void);
extern int qcom_scm_dcvs_reset(void);
extern int qcom_scm_dcvs_init_v2(phys_addr_t addr, size_t size,
			int *version);
extern int qcom_scm_dcvs_update(int level, s64 total_time,
			s64 busy_time);
extern int qcom_scm_dcvs_update_v2(int level, s64 total_time,
			s64 busy_time);
extern int qcom_scm_dcvs_update_ca_v2(int level, s64 total_time,
			s64 busy_time, int context_count);
extern int qcom_scm_dcvs_init_ca_v2(phys_addr_t addr, size_t size);
extern int qcom_scm_io_reset(void);
extern int qcom_scm_get_tz_log_feat_id(u64 *version);
extern int qcom_scm_get_tz_feat_id_version(u64 feat_id, u64 *version);
extern int qcom_scm_register_qsee_log_buf(phys_addr_t buf, size_t len);
extern int qcom_scm_query_encrypted_log_feature(u64 *enabled);
extern int qcom_scm_request_encrypted_log(phys_addr_t buf,
			size_t len, uint32_t log_id, bool is_full_tz_logs_supported,
			bool is_full_tz_logs_enabled);
extern bool qcom_scm_kgsl_set_smmu_aperture_available(void);
extern int qcom_scm_kgsl_set_smmu_aperture(unsigned int num_context_bank);
extern int qcom_scm_kgsl_init_regs(u32 gpu_req);
extern int qcom_scm_invoke_smc(phys_addr_t in_buf, size_t in_buf_size,
			phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
			u64 *response_type, unsigned int *data);
extern int qcom_scm_invoke_smc_legacy(phys_addr_t in_buf, size_t in_buf_size,
			phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
			u64 *response_type, unsigned int *data);
extern int qcom_scm_invoke_callback_response(phys_addr_t out_buf,
			size_t out_buf_size, int32_t *result, u64 *response_type,
			unsigned int *data);
extern int qcom_scm_sec_wdog_deactivate(void);
extern int qcom_scm_sec_wdog_trigger(void);
extern int qcom_scm_enable_shm_bridge(void);
extern int qcom_scm_delete_shm_bridge(u64 handle);
extern int qcom_scm_create_shm_bridge(u64 pfn_and_ns_perm_flags,
			u64 ipfn_and_s_perm_flags, u64 size_and_flags, u64 ns_vmids,
			u64 *handle);
extern int qcom_scm_spin_cpu(void);
extern int qcom_scm_ddrbw_profiler(phys_addr_t in_buf, size_t in_buf_size,
				   phys_addr_t out_buf, size_t out_buf_size);
extern int qcom_scm_she_op(u64 _arg1, u64 _arg2, u64 _arg3, u64 _arg4, u64 *res1);
#else
static inline bool qcom_scm_dcvs_ca_available(void)
{
	return false;
}

static inline bool qcom_scm_dcvs_core_available(void)
{
	return false;
}

static inline int qcom_scm_dcvs_reset(void)
{
	return -EPERM;
}

static inline int qcom_scm_dcvs_init_v2(phys_addr_t addr, size_t size,
			int *version)
{
	return -EPERM;
}

static inline int qcom_scm_dcvs_update(int level, s64 total_time,
			s64 busy_time)
{
	return -EPERM;
}

static inline int qcom_scm_dcvs_update_v2(int level, s64 total_time,
			s64 busy_time)
{
	return -EPERM;
}

static inline int qcom_scm_dcvs_update_ca_v2(int level, s64 total_time,
			s64 busy_time, int context_count)
{
	return -EPERM;
}

static inline int qcom_scm_dcvs_init_ca_v2(phys_addr_t addr, size_t size)
{
	return -EPERM;
}

static inline int qcom_scm_io_reset(void)
{
	return -EPERM;
}

static inline int qcom_scm_get_tz_log_feat_id(u64 *version)
{
	return -EPERM;
}

static inline int qcom_scm_get_tz_feat_id_version(u64 feat_id, u64 *version)
{
	return -EPERM;
}

static inline int qcom_scm_register_qsee_log_buf(phys_addr_t buf, size_t len)
{
	return -EPERM;
}

static inline int qcom_scm_query_encrypted_log_feature(u64 *enabled)
{
	return -EPERM;
}

static inline int qcom_scm_request_encrypted_log(phys_addr_t buf,
			size_t len, uint32_t log_id, bool is_full_tz_logs_supported,
			bool is_full_tz_logs_enabled)
{
	return -EPERM;
}

static inline bool qcom_scm_kgsl_set_smmu_aperture_available(void)
{
	return false;
}

static inline int qcom_scm_kgsl_set_smmu_aperture(unsigned int num_context_bank)
{
	return -EPERM;
}

static inline int qcom_scm_kgsl_init_regs(u32 gpu_req)
{
	return -EPERM;
}

static inline int qcom_scm_invoke_smc(phys_addr_t in_buf, size_t in_buf_size,
			phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
			u64 *response_type, unsigned int *data)
{
	return -EPERM;
}

static inline int qcom_scm_invoke_smc_legacy(phys_addr_t in_buf, size_t in_buf_size,
			phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
			u64 *response_type, unsigned int *data)
{
	return -EPERM;
}

static inline int qcom_scm_invoke_callback_response(phys_addr_t out_buf,
			size_t out_buf_size, int32_t *result, u64 *response_type,
			unsigned int *data)
{
	return -EPERM;
}

static inline int qcom_scm_sec_wdog_deactivate(void)
{
	return -EPERM;
}

static inline int qcom_scm_sec_wdog_trigger(void)
{
	return -EPERM;
}

static inline int qcom_scm_enable_shm_bridge(void)
{
	return -EPERM;
}

static inline int qcom_scm_delete_shm_bridge(u64 handle)
{
	return -EPERM;
}

static inline int qcom_scm_create_shm_bridge(u64 pfn_and_ns_perm_flags,
			u64 ipfn_and_s_perm_flags, u64 size_and_flags, u64 ns_vmids,
			u64 *handle)
{
	return -EPERM;
}

static inline int qcom_scm_spin_cpu(void)
{
	return -EPERM;
}

static inline int qcom_scm_ddrbw_profiler(phys_addr_t in_buf, size_t in_buf_size,
				   phys_addr_t out_buf, size_t out_buf_size)
{
	return -EPERM;
}

static inline int qcom_scm_she_op(u64 _arg1, u64 _arg2, u64 _arg3, u64 _arg4, u64 *res1)
{
	return -EPERM;
}
#endif
#endif
