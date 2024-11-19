
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_DISPLAY_INTERNAL_H__
#define __QCOM_DISPLAY_INTERNAL_H__

#include <linux/iommu.h>
#include <linux/dma-buf.h>
#include <linux/soc/qcom/llcc-qcom.h>

#define MMRM_CLK_CLIENT_NAME_SIZE  128
#define MMRM_CLIENT_DATA_FLAG_RESERVE_ONLY  0x0001
#define DMA_ATTR_QTI_SMMU_PROXY_MAP	(1UL << 18)
#define MMRM_CLIENT_DATA_FLAG_RESERVE_ONLY  0x0001

enum mmrm_crm_drv_type {
	MMRM_CRM_SW_DRV,
	MMRM_CRM_HW_DRV,
};

struct mmrm_client_data {
	u32 num_hw_blocks;
	u32 flags;

	/* CESTA data */
	enum mmrm_crm_drv_type drv_type;
	u32 crm_drv_idx;
	u32 pwr_st;
};

enum mmrm_client_priority {
	MMRM_CLIENT_PRIOR_HIGH = 0x1,
	MMRM_CLIENT_PRIOR_LOW  = 0x2
};
enum mmrm_client_domain {
	MMRM_CLIENT_DOMAIN_CAMERA = 0x1,
	MMRM_CLIENT_DOMAIN_CVP = 0x2,
	MMRM_CLIENT_DOMAIN_DISPLAY = 0x3,
	MMRM_CLIENT_DOMAIN_VIDEO = 0x4,
};

enum mmrm_client_type {
	MMRM_CLIENT_CLOCK,
};
enum mmrm_cb_type {
	MMRM_CLIENT_RESOURCE_VALUE_CHANGE = 0x1,
};
enum altmode_send_msg_type {
	ALTMODE_PAN_EN = 0x10,
	ALTMODE_PAN_ACK,
};
struct mmrm_client {
	enum mmrm_client_type client_type;
	u32 client_uid;
};

struct mmrm_res_val_chng {
	unsigned long old_val;
	unsigned long new_val;
};

struct mmrm_client_notifier_data {
	enum mmrm_cb_type cb_type;
	union {
		struct mmrm_res_val_chng val_chng;
	} cb_data;
	void *pvt_data;
};

struct mmrm_clk_client_desc {
	u32 client_domain;
	u32 client_id;
	const char name[MMRM_CLK_CLIENT_NAME_SIZE];
	struct clk *clk;
	/* CESTA data */
	u32 hw_drv_instances;
	u32 num_pwr_states;
};

struct mmrm_client_desc {
	enum mmrm_client_type client_type;
	union {
		struct mmrm_clk_client_desc desc;
	} client_info;
	enum mmrm_client_priority priority;
	void *pvt_data;
	int (*notifier_callback_fn)(
		void *notifier_data);
};
struct altmode_client;

/**
 * Enum describing the various staling modes available for clients to use.
 */
enum llcc_staling_mode {
	LLCC_STALING_MODE_CAPACITY, /* Default option on reset */
	LLCC_STALING_MODE_NOTIFY,
	LLCC_STALING_MODE_MAX
};

enum llcc_staling_notify_op {
	LLCC_NOTIFY_STALING_WRITEBACK,
	LLCC_NOTIFY_STALING_NO_WRITEBACK,
	LLCC_NOTIFY_STALING_OPS_MAX
};

struct llcc_staling_mode_params {
	enum llcc_staling_mode staling_mode;
	union {
		/* STALING_MODE_CAPACITY needs no params */
		struct staling_mode_notify_params {
			u8 staling_distance;
			enum llcc_staling_notify_op op;
		} notify_params;
	};
};

enum sid_switch_direction {
	SID_ACQUIRE,
	SID_RELEASE,
};

struct qtee_shm {
	phys_addr_t paddr;
 	void *vaddr;
 	size_t size;
};

struct altmode_pan_ack_msg {
	u32 cmd_type;
	u8 port_index;
};

static inline int qcom_iommu_sid_switch(struct device *dev, enum sid_switch_direction dir)
{
	return -EINVAL;
}

static inline int msm_dma_unmap_all_for_dev(struct device *dev)
{
	return -EINVAL;
}

static inline int qcom_iommu_enable_s1_translation(struct iommu_domain *domain)
{
	return 0;
}

static inline int mem_buf_dma_buf_copy_vmperm(struct dma_buf *dmabuf, int **vmids, int **perms,
 		int *nr_acl_entries)
{
	return 0;
}
static inline int of_fdt_get_ddrtype(void)
{
	return 0;
}

static inline bool  msm_gpio_get_pin_address(const int gpio_pin, struct resource *res)
{
	return false;
}
static inline int spmi_pmic_arb_map_address(const struct device *dev,
 				u32 spmi_address, struct resource *res_out)
{
	return -ENODEV;
}

static inline bool qtee_shmbridge_is_enabled(void)
{
	return false;
}

static inline int32_t qtee_shmbridge_allocate_shm(size_t size, struct qtee_shm *shm)
{
	return -ENOMEM;
}

static inline int qcom_scm_mem_protect_sd_ctrl(u32 devid, phys_addr_t mem_addr,
						u64 mem_size, u32 vmid)
{
	return -ENOMEM;
}

static void qtee_shmbridge_free_shm(struct qtee_shm *shm)
{

}

static inline struct mmrm_client *mmrm_client_register(
	struct mmrm_client_desc *desc)
{
	return NULL;
}

static inline int mmrm_client_set_value(struct mmrm_client *client,
	struct mmrm_client_data *client_data, unsigned long val)
{
	return -EINVAL;
}
static inline int mmrm_client_deregister(struct mmrm_client *client)
{
	return 0;
}

static inline int llcc_configure_staling_mode(struct llcc_slice_desc *desc,
			struct llcc_staling_mode_params *p)
{
	return -EINVAL;
}
static inline int llcc_notif_staling_inc_counter(struct llcc_slice_desc *desc)
{
	return -EINVAL;
}
static inline void *ipc_log_context_create(int max_num_pages, const char *modname,
		uint32_t feature_version)
{
	return NULL;
}
static inline void *ipc_log_context_destroy(void *ctxt)
{
        return NULL;
}
static inline void ipc_log_string(void *ilctxt, const char *fmt, ...)
{

}
static inline int altmode_send_data(int client, void *data, size_t len)
{
	return 0;
}
#endif
