// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define QCOM_SCM_MP_CP_SMMU_APERTURE_ID         0x1b
#define QCOM_SCM_CP_APERTURE_REG    0x0
#define QCOM_SCM_SVC_GPU            0x28
#define QCOM_SCM_SVC_GPU_INIT_REGS          0x1

#define QCOM_SCM_SVC_SMCINVOKE          0x06
#define QCOM_SCM_SMCINVOKE_INVOKE_LEGACY    0x00
#define QCOM_SCM_SMCINVOKE_INVOKE       0x02
#define QCOM_SCM_SMCINVOKE_CB_RSP       0x01

#define QCOM_SCM_SVC_INFO       0x06
#define QCOM_SCM_INFO_IS_CALL_AVAIL 0x01
#define QCOM_SCM_INFO_GET_FEAT_VERSION_CMD  0x03

/* TOS Services and Function IDs */
#define QCOM_SCM_SVC_QSEELOG            0x01
#define QCOM_SCM_QSEELOG_REGISTER       0x06
#define QCOM_SCM_QUERY_ENCR_LOG_FEAT_ID     0x0b
#define QCOM_SCM_REQUEST_ENCR_LOG_ID        0x0c

/* Feature IDs for QCOM_SCM_INFO_GET_FEAT_VERSION */
#define QCOM_SCM_TZ_DBG_ETM_FEAT_ID     0x08
#define QCOM_SCM_FEAT_LOG_ID            0x0a
#define QCOM_SCM_MP_CP_FEAT_ID          0x0c

#define QCOM_SCM_SVC_DCVS           0x0D
#define QCOM_SCM_DCVS_RESET         0x07
#define QCOM_SCM_DCVS_UPDATE            0x08
#define QCOM_SCM_DCVS_INIT          0x09
#define QCOM_SCM_DCVS_UPDATE_V2         0x0a
#define QCOM_SCM_DCVS_INIT_V2           0x0b
#define QCOM_SCM_DCVS_INIT_CA_V2        0x0c
#define QCOM_SCM_DCVS_UPDATE_CA_V2      0x0d

#define QCOM_SCM_IO_RESET           0x03

/* IDs for sdi and sec wdog control */
#define QCOM_SCM_BOOT_SEC_WDOG_DIS	0x07
#define QCOM_SCM_BOOT_SEC_WDOG_TRIGGER	0x08
#define QCOM_SCM_BOOT_WDOG_DEBUG_PART	0x09
#define QCOM_SCM_BOOT_SPIN_CPU		0x0d

/* IDs for SHE */
#define QCOM_SCM_SVC_SHE                        0x21
#define QCOM_SCM_SHE_ID                         0x1

/* IDs for QCOM_SCM_INFO_BW_PROF_ID */
#define QCOM_SCM_INFO_BW_PROF_ID	0x07

/* IDs for CAMERA */
#define QCOM_SCM_SVC_CAMERA			0x18
#define QCOM_SCM_CAMERA_UPDATE_CAMNOC_QOS	0xA

/* IDs for Dump Table*/
#define QCOM_SCM_SVC_UTIL			0x03
#define QCOM_SCM_UTIL_DUMP_TABLE_ASSIGN		0x13

static int __qcom_scm_get_feat_version(struct device *dev, u64 feat_id, u64 *version)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_GET_FEAT_VERSION_CMD,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = feat_id,
		.arginfo = QCOM_SCM_ARGS(1),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (version)
		*version = res.result[0];

	return ret;
}

/**
 * qcom_scm_dcvs_ca_available() - check if context aware DCVS operations are
 * available
 */
bool qcom_scm_dcvs_ca_available(void)
{
	struct device *dev = __scm ? __scm->dev : NULL;

	return __qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					QCOM_SCM_DCVS_INIT_CA_V2) &&
			__qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					QCOM_SCM_DCVS_UPDATE_CA_V2);
}
EXPORT_SYMBOL_GPL(qcom_scm_dcvs_ca_available);

/**
 * qcom_scm_dcvs_core_available() - check if core DCVS operations are available
 */
bool qcom_scm_dcvs_core_available(void)
{
	struct device *dev = __scm ? __scm->dev : NULL;

	return __qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					QCOM_SCM_DCVS_INIT) &&
			 __qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					QCOM_SCM_DCVS_UPDATE) &&
			 __qcom_scm_is_call_available(dev, QCOM_SCM_SVC_DCVS,
					QCOM_SCM_DCVS_RESET);
}
EXPORT_SYMBOL_GPL(qcom_scm_dcvs_core_available);

int qcom_scm_dcvs_reset(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_RESET,
		.owner = ARM_SMCCC_OWNER_SIP
	};

	return qcom_scm_call(__scm ? __scm->dev : NULL, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_dcvs_reset);

int qcom_scm_dcvs_init_v2(phys_addr_t addr, size_t size, int *version)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_INIT_V2,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = addr,
		.args[1] = size,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RW, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (ret >= 0)
		*version = res.result[0];
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_scm_dcvs_init_v2);

int qcom_scm_dcvs_update(int level, s64 total_time, s64 busy_time)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_UPDATE,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = level,
		.args[1] = total_time,
		.args[2] = busy_time,
		.arginfo = QCOM_SCM_ARGS(3),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call_atomic(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_dcvs_update);

int qcom_scm_dcvs_update_v2(int level, s64 total_time, s64 busy_time)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_UPDATE_V2,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = level,
		.args[1] = total_time,
		.args[2] = busy_time,
		.arginfo = QCOM_SCM_ARGS(3),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_dcvs_update_v2);

int qcom_scm_dcvs_update_ca_v2(int level, s64 total_time, s64 busy_time,
		int context_count)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_UPDATE_CA_V2,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = level,
		.args[1] = total_time,
		.args[2] = busy_time,
		.args[3] = context_count,
		.arginfo = QCOM_SCM_ARGS(4),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];

}
EXPORT_SYMBOL_GPL(qcom_scm_dcvs_update_ca_v2);

int qcom_scm_dcvs_init_ca_v2(phys_addr_t addr, size_t size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_DCVS,
		.cmd = QCOM_SCM_DCVS_INIT_CA_V2,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = addr,
		.args[1] = size,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RW, QCOM_SCM_VAL),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_dcvs_init_ca_v2);

int qcom_scm_io_reset(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_IO,
		.cmd = QCOM_SCM_IO_RESET,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = QCOM_SCM_ARGS(2),
	};

	return qcom_scm_call_atomic(__scm ? __scm->dev : NULL, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_io_reset);

int qcom_scm_get_tz_log_feat_id(u64 *version)
{
	return __qcom_scm_get_feat_version(__scm->dev, QCOM_SCM_FEAT_LOG_ID, version);
}
EXPORT_SYMBOL_GPL(qcom_scm_get_tz_log_feat_id);

int qcom_scm_get_tz_feat_id_version(u64 feat_id, u64 *version)
{
	return __qcom_scm_get_feat_version(__scm->dev, feat_id, version);
}
EXPORT_SYMBOL_GPL(qcom_scm_get_tz_feat_id_version);

int qcom_scm_register_qsee_log_buf(phys_addr_t buf, size_t len)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_QSEELOG,
		.cmd = QCOM_SCM_QSEELOG_REGISTER,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = buf,
		.args[1] = len,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RW),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_register_qsee_log_buf);

int qcom_scm_query_encrypted_log_feature(u64 *enabled)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_QSEELOG,
		.cmd = QCOM_SCM_QUERY_ENCR_LOG_FEAT_ID,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);
	if (enabled)
		*enabled = res.result[0];

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_scm_query_encrypted_log_feature);

int qcom_scm_request_encrypted_log(phys_addr_t buf,
					size_t len,
					uint32_t log_id,
					bool is_full_tz_logs_supported,
					bool is_full_tz_logs_enabled)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_QSEELOG,
		.cmd = QCOM_SCM_REQUEST_ENCR_LOG_ID,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = buf,
		.args[1] = len,
		.args[2] = log_id
	};
	struct qcom_scm_res res;

	if (is_full_tz_logs_supported) {
		if (is_full_tz_logs_enabled) {
			/* requesting full logs */
			desc.args[3] = 1;
		} else {
			/* requesting incremental logs */
			desc.args[3] = 0;
		}
		desc.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_RW);
	} else {
		desc.arginfo = QCOM_SCM_ARGS(3, QCOM_SCM_RW);
	}
	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_request_encrypted_log);

bool qcom_scm_kgsl_set_smmu_aperture_available(void)
{
	int ret;

	ret = __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_MP,
				QCOM_SCM_MP_CP_SMMU_APERTURE_ID);

	return ret > 0;
}
EXPORT_SYMBOL_GPL(qcom_scm_kgsl_set_smmu_aperture_available);

int qcom_scm_kgsl_set_smmu_aperture(unsigned int num_context_bank)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_MP,
		.cmd = QCOM_SCM_MP_CP_SMMU_APERTURE_ID,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 0xffff0000
				| ((QCOM_SCM_CP_APERTURE_REG & 0xff) << 8)
				| (num_context_bank & 0xff),
		.args[1] = 0xffffffff,
		.args[2] = 0xffffffff,
		.args[3] = 0xffffffff,
		.arginfo = QCOM_SCM_ARGS(4),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);

}
EXPORT_SYMBOL_GPL(qcom_scm_kgsl_set_smmu_aperture);

int qcom_scm_kgsl_init_regs(u32 gpu_req)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_GPU,
		.cmd = QCOM_SCM_SVC_GPU_INIT_REGS,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = gpu_req,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_kgsl_init_regs);

int qcom_scm_multi_kgsl_init_regs(u32 gpu_req, u32 cmd)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_GPU,
		.cmd = cmd,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = gpu_req,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_multi_kgsl_init_regs);

int qcom_scm_invoke_smc(phys_addr_t in_buf, size_t in_buf_size,
		phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
		u64 *response_type, unsigned int *data)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SMCINVOKE,
		.cmd = QCOM_SCM_SMCINVOKE_INVOKE,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = in_buf,
		.args[1] = in_buf_size,
		.args[2] = out_buf,
		.args[3] = out_buf_size,
		.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_RW, QCOM_SCM_VAL,
					QCOM_SCM_RW, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (result)
		*result = res.result[1];

	if (response_type)
		*response_type = res.result[0];

	if (data)
		*data = res.result[2];

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_scm_invoke_smc);

int qcom_scm_invoke_smc_legacy(phys_addr_t in_buf, size_t in_buf_size,
		phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
		u64 *response_type, unsigned int *data)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SMCINVOKE,
		.cmd = QCOM_SCM_SMCINVOKE_INVOKE_LEGACY,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = in_buf,
		.args[1] = in_buf_size,
		.args[2] = out_buf,
		.args[3] = out_buf_size,
		.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_RW, QCOM_SCM_VAL,
					QCOM_SCM_RW, QCOM_SCM_VAL),
	};

	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (result)
		*result = res.result[1];

	if (response_type)
		*response_type = res.result[0];

	if (data)
		*data = res.result[2];

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_scm_invoke_smc_legacy);

int qcom_scm_invoke_callback_response(phys_addr_t out_buf,
	size_t out_buf_size, int32_t *result, u64 *response_type,
	unsigned int *data)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SMCINVOKE,
		.cmd = QCOM_SCM_SMCINVOKE_CB_RSP,
		.owner = ARM_SMCCC_OWNER_TRUSTED_OS,
		.args[0] = out_buf,
		.args[1] = out_buf_size,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_RW, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	if (result)
		*result = res.result[1];

	if (response_type)
		*response_type = res.result[0];

	if (data)
		*data = res.result[2];

	return ret;

}
EXPORT_SYMBOL_GPL(qcom_scm_invoke_callback_response);

/**
 * qcm_scm_sec_wdog_deactivate() - Deactivate secure watchdog
 */
int qcom_scm_sec_wdog_deactivate(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SEC_WDOG_DIS,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 1,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_sec_wdog_deactivate);

/**
 * qcom_scm_sec_wdog_trigger() - Trigger secure watchdog
 */
int qcom_scm_sec_wdog_trigger(void)
{
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SEC_WDOG_TRIGGER,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 0,
		.arginfo = QCOM_SCM_ARGS(1),
	};
	struct qcom_scm_res res;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_sec_wdog_trigger);

/**
 * qcom_scm_spin_cpu(void) - spin on cpu
 */
int qcom_scm_spin_cpu(void)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_BOOT,
		.cmd = QCOM_SCM_BOOT_SPIN_CPU,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = 0,
		.arginfo = QCOM_SCM_ARGS(1),
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_spin_cpu);

int qcom_scm_ddrbw_profiler(phys_addr_t in_buf, size_t in_buf_size,
			    phys_addr_t out_buf, size_t out_buf_size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_INFO,
		.cmd = QCOM_SCM_INFO_BW_PROF_ID,
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	desc.args[0] = in_buf;
	desc.args[1] = in_buf_size;
	desc.args[2] = out_buf;
	desc.args[3] = out_buf_size;
	desc.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_RW, QCOM_SCM_VAL,
				     QCOM_SCM_RW, QCOM_SCM_VAL);

	return qcom_scm_call(__scm ? __scm->dev : NULL, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_ddrbw_profiler);

/**
 * qcom_scm_she_op() - request TZ SHE (Secure Hardware Extension) service
 *		       to perform crypto operations based on SHE tables.
 */
int qcom_scm_she_op(u64 _arg1, u64 _arg2, u64 _arg3, u64 _arg4, u64 *res1)
{
	int ret;

	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_SHE,
		.cmd = QCOM_SCM_SHE_ID,
		.owner = ARM_SMCCC_OWNER_SIP,
		.arginfo = QCOM_SCM_ARGS(4, QCOM_SCM_VAL, QCOM_SCM_VAL,
				     QCOM_SCM_RW, QCOM_SCM_VAL),
	};
	struct qcom_scm_res res;

	desc.args[0] = _arg1;
	desc.args[1] = _arg2;
	desc.args[2] = _arg3;
	desc.args[3] = _arg4;

	ret = qcom_scm_call_atomic(__scm ? __scm->dev : NULL, &desc, &res);

	/* The result from SHE service must be handled by the SHE client */
	if (res1)
		*res1 = res.result[0];

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_scm_she_op);

int qcom_scm_camera_update_camnoc_qos(uint32_t use_case_id,
	uint32_t cam_qos_cnt, struct qcom_scm_camera_qos *cam_qos)
{
	int ret;
	dma_addr_t payload_phys;
	u32 *payload_buf = NULL;
	u32 payload_size = 0;

	if ((cam_qos_cnt > QCOM_SCM_CAMERA_MAX_QOS_CNT) || (cam_qos_cnt && !cam_qos)) {
		pr_err("Invalid input SmartQoS count: %d\n", cam_qos_cnt);
		return -EINVAL;
	}

	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_CAMERA,
		.cmd = QCOM_SCM_CAMERA_UPDATE_CAMNOC_QOS,
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = use_case_id,
		.args[2] = payload_size,
		.arginfo = QCOM_SCM_ARGS(3, QCOM_SCM_VAL, QCOM_SCM_RW, QCOM_SCM_VAL),
	};

	payload_size = cam_qos_cnt * sizeof(struct qcom_scm_camera_qos);

	/* fill all required qos settings */
	if (use_case_id && payload_size && cam_qos) {
		payload_buf = dma_alloc_coherent(__scm->dev,
						 payload_size, &payload_phys, GFP_KERNEL);
		if (!payload_buf)
			return -ENOMEM;

		memcpy(payload_buf, cam_qos, payload_size);
		desc.args[1] = payload_phys;
		desc.args[2] = payload_size;

	}
	ret = qcom_scm_call(__scm->dev, &desc, NULL);

	if (payload_buf)
		dma_free_coherent(__scm->dev, payload_size, payload_buf, payload_phys);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_scm_camera_update_camnoc_qos);

int qcom_scm_assign_dump_table_region(bool is_assign, phys_addr_t addr, size_t size)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_UTIL,
		.cmd = QCOM_SCM_UTIL_DUMP_TABLE_ASSIGN,
		.arginfo = QCOM_SCM_ARGS(3, QCOM_SCM_VAL, QCOM_SCM_VAL, QCOM_SCM_VAL),
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = is_assign,
		.args[1] = addr,
		.args[2] = size,
	};

	return qcom_scm_call(__scm->dev, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_assign_dump_table_region);

int qcom_scm_load_ccu_qup_fw(u32 qup_type)
{
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_TZ_CCU_QUP,
		.cmd = QCOM_SCM_LOAD_CCU_QUP_FW,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = qup_type,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;
	int ret;

	ret = qcom_scm_call(__scm->dev, &desc, &res);

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_load_ccu_qup_fw);
