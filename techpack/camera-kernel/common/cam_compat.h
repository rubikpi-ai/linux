/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CAM_COMPAT_H_
#define _CAM_COMPAT_H_

#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/component.h>
#include <linux/iommu.h>
#include <linux/list_sort.h>
#include <linux/spi/spi.h>

#include "cam_csiphy_dev.h"
#include "cam_cpastop_hw.h"
#include "cam_smmu_api.h"

#include <linux/qcom-dma-mapping.h>
#include <linux/i3c/master.h>

#include <linux/interconnect.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
MODULE_IMPORT_NS(DMA_BUF);
#endif

#ifdef CONFIG_DOMAIN_ID_SECURE_CAMERA
#include <linux/IClientEnv.h>
#include <linux/ITrustedCameraDriver.h>
#include <linux/CTrustedCameraDriver.h>
#endif

#define IS_CSF25(x, y) ((((x) == 2) && ((y) == 5)) ? 1 : 0)

struct cam_fw_alloc_info {
	struct device *fw_dev;
	void          *fw_kva;
	uint64_t       fw_hdl;
};

int cam_reserve_icp_fw(struct cam_fw_alloc_info *icp_fw, size_t fw_length);
void cam_unreserve_icp_fw(struct cam_fw_alloc_info *icp_fw, size_t fw_length);
int camera_component_match_add_drivers(struct device *master_dev,
	struct component_match **match_list);
void cam_check_iommu_faults(struct iommu_domain *domain,
	struct cam_smmu_pf_info *pf_info);
void cam_free_clear(const void *);
int cam_compat_util_get_dmabuf_va(struct dma_buf *dmabuf, uintptr_t *vaddr);
void cam_compat_util_put_dmabuf_va(struct dma_buf *dmabuf, void *vaddr);
void cam_smmu_util_iommu_custom(struct device *dev,
	dma_addr_t discard_start, size_t discard_length);
int cam_get_ddr_type(void);

int cam_req_mgr_ordered_list_cmp(void *priv,
	const struct list_head *head_1, const struct list_head *head_2);

int cam_compat_util_get_irq(struct cam_hw_soc_info *soc_info);

long cam_dma_buf_set_name(struct dma_buf *dmabuf, const char *name);

const struct device *cam_cpas_get_rsc_dev_for_drv(uint32_t index);

int cam_cpas_start_drv_for_dev(const struct device *dev);

int cam_cpas_stop_drv_for_dev(const struct device *dev);

int cam_cpas_drv_channel_switch_for_dev(const struct device *dev);

inline struct icc_path *cam_icc_get_path(struct device *dev,
	const int src_id, const int dst_id, const char *path_name, bool use_path_name);

#ifdef CONFIG_SPECTRA_SECURE
void cam_cpastop_scm_write(struct cam_cpas_hw_errata_wa *errata_wa);
int cam_ife_notify_safe_lut_scm(bool safe_trigger);
int cam_csiphy_notify_secure_mode(struct csiphy_device *csiphy_dev,
	bool protect, int32_t offset);
#endif /* CONFIG_SPECTRA_SECURE */

void qcom_clk_dump(struct clk *clk, void *regulator,
		   bool calltrace);

int cam_actuator_i2c_driver_remove_common(struct i2c_client *client);
int cam_eeprom_i2c_driver_remove_common(struct i2c_client *client);
int cam_flash_i2c_driver_remove_common(struct i2c_client *client);
int cam_ois_i2c_driver_remove_common(struct i2c_client *client);
int cam_sensor_i2c_driver_remove_common(struct i2c_client *client);
int cam_eeprom_spi_driver_remove_common(struct spi_device *sdev);

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_i3c_driver_remove(struct i3c_device *client);
#else
int cam_i3c_driver_remove(struct i3c_device *client);
#endif

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_actuator_i2c_driver_remove(struct i2c_client *client);
#else
static int32_t cam_actuator_i2c_driver_remove(struct i2c_client *client);
#endif

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_eeprom_i2c_driver_remove(struct i2c_client *client);
#else
static int32_t cam_eeprom_i2c_driver_remove(struct i2c_client *client);
#endif

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_flash_i2c_driver_remove(struct i2c_client *client);
#else
static int32_t cam_flash_i2c_driver_remove(struct i2c_client *client);
#endif

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_ois_i2c_driver_remove(struct i2c_client *client);
#else
static int32_t cam_ois_i2c_driver_remove(struct i2c_client *client);
#endif

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_sensor_i2c_driver_remove(struct i2c_client *client);
#else
static int32_t cam_sensor_i2c_driver_remove(struct i2c_client *client);
#endif

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
void cam_eeprom_spi_driver_remove(struct spi_device *sdev);
#else
static int32_t cam_eeprom_spi_driver_remove(struct spi_device *sdev);
#endif

#ifndef CONFIG_SPECTRA_KT
int cam_smmu_fetch_csf_version(struct cam_csf_version *csf_version);
#endif /* ifndef CONFIG_SPECTRA_KT */

bool cam_secure_get_vfe_fd_port_config(void);
unsigned long cam_update_dma_map_attributes(unsigned long attr);
size_t cam_align_dma_buf_size(size_t len);
int cam_get_subpart_info(uint32_t *part_info, uint32_t max_num_cam);
#endif /* _CAM_COMPAT_H_ */
