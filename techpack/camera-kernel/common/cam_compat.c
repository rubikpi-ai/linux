// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include <soc/qcom/rpmh.h>

#include "cam_compat.h"
#include "cam_debug_util.h"
#include "cam_cpas_api.h"
#include "camera_main.h"
#include "cam_eeprom_dev.h"
#include "cam_eeprom_core.h"

#if IS_ENABLED(CONFIG_SPECTRA_USE_RPMH_DRV_API)
#define CAM_RSC_DRV_IDENTIFIER "cam_rsc"

const struct device *cam_cpas_get_rsc_dev_for_drv(uint32_t index)
{
	const struct device *rsc_dev;

	rsc_dev = rpmh_get_device(CAM_RSC_DRV_IDENTIFIER, index);
	if (!rsc_dev) {
		CAM_ERR(CAM_CPAS, "Invalid dev for index: %u", index);
		return NULL;
	}

	return rsc_dev;
}

int cam_cpas_start_drv_for_dev(const struct device *dev)
{
	int rc = 0;

	if (!dev) {
		CAM_ERR(CAM_CPAS, "Invalid dev for DRV enable");
		return -EINVAL;
	}

	rc = rpmh_drv_start(dev);
	if (rc) {
		CAM_ERR(CAM_CPAS, "[%s] Failed in DRV start", dev_name(dev));
		return rc;
	}

	return rc;
}

int cam_cpas_stop_drv_for_dev(const struct device *dev)
{
	int rc = 0;

	if (!dev) {
		CAM_ERR(CAM_CPAS, "Invalid dev for DRV disable");
		return -EINVAL;
	}

	rc = rpmh_drv_stop(dev);
	if (rc) {
		CAM_ERR(CAM_CPAS, "[%s] Failed in DRV stop", dev_name(dev));
		return rc;
	}

	return rc;
}

int cam_cpas_drv_channel_switch_for_dev(const struct device *dev)
{
	int rc = 0;

	if (!dev) {
		CAM_ERR(CAM_CPAS, "Invalid dev for DRV channel switch");
		return -EINVAL;
	}

	rc = rpmh_write_sleep_and_wake_no_child(dev);
	if (rc) {
		CAM_ERR(CAM_CPAS, "[%s] Failed in DRV channel switch", dev_name(dev));
		return rc;
	}

	return rc;
}

#else
const struct device *cam_cpas_get_rsc_dev_for_drv(uint32_t index)
{
	return NULL;
}

int cam_cpas_start_drv_for_dev(const struct device *dev)

{
	return 0;
}

int cam_cpas_stop_drv_for_dev(const struct device *dev)
{
	return 0;
}

int cam_cpas_drv_channel_switch_for_dev(const struct device *dev)
{
	return 0;
}
#endif

#ifndef CONFIG_SPECTRA_KT

int cam_smmu_fetch_csf_version(struct cam_csf_version *csf_version)
{
#ifdef CONFIG_SPECTRA_SECURE
	struct csf_version csf_ver;
	int rc;

	/* Fetch CSF version from SMMU proxy driver */
	rc = smmu_proxy_get_csf_version(&csf_ver);
	if (rc) {
		CAM_ERR(CAM_SMMU,
			"Failed to get CSF version from SMMU proxy: %d", rc);
		return rc;
	}

	csf_version->arch_ver = csf_ver.arch_ver;
	csf_version->max_ver = csf_ver.max_ver;
	csf_version->min_ver = csf_ver.min_ver;
#else
	/* This defaults to the legacy version */
	csf_version->arch_ver = 2;
	csf_version->max_ver = 0;
	csf_version->min_ver = 0;
#endif /* CONFIG_SPECTRA_SECURE */
	return 0;
}

#endif /* ifndef CONFIG_SPECTRA_KT */

unsigned long cam_update_dma_map_attributes(unsigned long attrs)
{
#ifdef CONFIG_SPECTRA_SECURE
	attrs |= DMA_ATTR_QTI_SMMU_PROXY_MAP;
#endif /* CONFIG_SPECTRA_SECURE */
	return attrs;
}

size_t cam_align_dma_buf_size(size_t len)
{
#ifdef CONFIG_SPECTRA_SECURE
	len = ALIGN(len, SMMU_PROXY_MEM_ALIGNMENT);
#endif /* CONFIG_SPECTRA_SECURE */
	return len;
}

int cam_reserve_icp_fw(struct cam_fw_alloc_info *icp_fw, size_t fw_length)
{
	int rc = 0;
	struct device_node *of_node;
	struct device_node *mem_node;
	struct resource     res;

	of_node = (icp_fw->fw_dev)->of_node;
	mem_node = of_parse_phandle(of_node, "memory-region", 0);
	if (!mem_node) {
		rc = -ENOMEM;
		CAM_ERR(CAM_SMMU, "FW memory carveout not found");
		goto end;
	}
	rc = of_address_to_resource(mem_node, 0, &res);
	of_node_put(mem_node);
	if (rc < 0) {
		CAM_ERR(CAM_SMMU, "Unable to get start of FW mem carveout");
		goto end;
	}
	icp_fw->fw_hdl = res.start;
	icp_fw->fw_kva = ioremap_wc(icp_fw->fw_hdl, fw_length);
	if (!icp_fw->fw_kva) {
		CAM_ERR(CAM_SMMU, "Failed to map the FW.");
		rc = -ENOMEM;
		goto end;
	}
	memset_io(icp_fw->fw_kva, 0, fw_length);

end:
	return rc;
}

void cam_unreserve_icp_fw(struct cam_fw_alloc_info *icp_fw, size_t fw_length)
{
	iounmap(icp_fw->fw_kva);
}

#ifdef CONFIG_SPECTRA_SECURE
int cam_ife_notify_safe_lut_scm(bool safe_trigger)
{
	const uint32_t smmu_se_ife = 0;
	uint32_t camera_hw_version, rc = 0;

	rc = cam_cpas_get_cpas_hw_version(&camera_hw_version);
	if (!rc && qcom_scm_smmu_notify_secure_lut(smmu_se_ife, safe_trigger)) {
		switch (camera_hw_version) {
		case CAM_CPAS_TITAN_170_V100:
		case CAM_CPAS_TITAN_170_V110:
		case CAM_CPAS_TITAN_175_V100:
			CAM_ERR(CAM_ISP, "scm call to enable safe failed");
			rc = -EINVAL;
			break;
		default:
			break;
		}
	}

	return rc;
}

int cam_csiphy_notify_secure_mode(struct csiphy_device *csiphy_dev,
	bool protect, int32_t offset)
{
	int rc = 0;

	if (offset >= CSIPHY_MAX_INSTANCES_PER_PHY) {
		CAM_ERR(CAM_CSIPHY, "Invalid CSIPHY offset");
		rc = -EINVAL;
	} else if (qcom_scm_camera_protect_phy_lanes(protect,
			csiphy_dev->csiphy_info[offset]
				.csiphy_cpas_cp_reg_mask)) {
		CAM_ERR(CAM_CSIPHY, "SCM call to hypervisor failed");
		rc = -EINVAL;
	}

	return rc;
}

void cam_cpastop_scm_write(struct cam_cpas_hw_errata_wa *errata_wa)
{
	int reg_val;

	qcom_scm_io_readl(errata_wa->data.reg_info.offset, &reg_val);
	reg_val |= errata_wa->data.reg_info.value;
	qcom_scm_io_writel(errata_wa->data.reg_info.offset, reg_val);
}
#endif /* CONFIG_SPECTRA_SECURE */

static int camera_platform_compare_dev(struct device *dev, const void *data)
{
	return platform_bus_type.match(dev, (struct device_driver *) data);
}

static int camera_i2c_compare_dev(struct device *dev, const void *data)
{
	return i2c_bus_type.match(dev, (struct device_driver *) data);
}
void cam_free_clear(const void * ptr)
{
	kfree_sensitive(ptr);
}

/* Callback to compare device from match list before adding as component */
static inline int camera_component_compare_dev(struct device *dev, void *data)
{
	return dev == data;
}

#ifdef CONFIG_SPECTRA_KT

/* Add component matches to list for master of aggregate driver */
int camera_component_match_add_drivers(struct device *master_dev,
	struct component_match **match_list)
{
	int i, rc = 0;
	struct platform_device *pdev = NULL;
	struct device *start_dev = NULL, *match_dev = NULL;

	if (!master_dev || !match_list) {
		CAM_ERR(CAM_UTIL, "Invalid parameters for component match add");
		rc = -EINVAL;
		goto end;
	}

	for (i = 0; i < ARRAY_SIZE(cam_component_drivers); i++) {
		struct device_driver const *drv =
			&cam_component_drivers[i]->driver;
		const void *drv_ptr = (const void *)drv;
		start_dev = NULL;
		while ((match_dev = bus_find_device(&platform_bus_type,
			start_dev, drv_ptr, &camera_platform_compare_dev))) {
			put_device(start_dev);
			pdev = to_platform_device(match_dev);
			CAM_DBG(CAM_UTIL, "Adding matched component:%s", pdev->name);
			component_match_add(master_dev, match_list,
				camera_component_compare_dev, match_dev);
			start_dev = match_dev;
		}
		put_device(start_dev);
	}

end:
	return rc;
}

#else

/* Add component matches to list for master of aggregate driver */
int camera_component_match_add_drivers(struct device *master_dev,
	struct component_match **match_list)
{
	int i, rc = 0;
	struct platform_device *pdev = NULL;
	struct i2c_client *client = NULL;
	struct device *start_dev = NULL, *match_dev = NULL;

	if (!master_dev || !match_list) {
		CAM_ERR(CAM_UTIL, "Invalid parameters for component match add");
		rc = -EINVAL;
		goto end;
	}

	for (i = 0; i < ARRAY_SIZE(cam_component_platform_drivers); i++) {
		struct device_driver const *drv =
			&cam_component_platform_drivers[i]->driver;
		const void *drv_ptr = (const void *)drv;
		start_dev = NULL;
		while ((match_dev = bus_find_device(&platform_bus_type,
			start_dev, drv_ptr, &camera_platform_compare_dev))) {
			put_device(start_dev);
			pdev = to_platform_device(match_dev);
			CAM_DBG(CAM_UTIL, "Adding matched component:%s", pdev->name);
			component_match_add(master_dev, match_list,
				camera_component_compare_dev, match_dev);
			start_dev = match_dev;
		}
		put_device(start_dev);
	}

	for (i = 0; i < ARRAY_SIZE(cam_component_i2c_drivers); i++) {
		struct device_driver const *drv =
			&cam_component_i2c_drivers[i]->driver;
		const void *drv_ptr = (const void *)drv;
		start_dev = NULL;
		while ((match_dev = bus_find_device(&i2c_bus_type,
			start_dev, drv_ptr, &camera_i2c_compare_dev))) {
			put_device(start_dev);
			client = to_i2c_client(match_dev);
			CAM_DBG(CAM_UTIL, "Adding matched component:%s", client->name);
			component_match_add(master_dev, match_list,
				camera_component_compare_dev, match_dev);
			start_dev = match_dev;
		}
		put_device(start_dev);
	}

end:
	return rc;
}

#endif /* ifdef CONFIG_SPECTRA_KT */

struct iommu_fault_ids {
	int bid;
	int pid;
	int mid;
};
static int iommu_get_fault_ids(struct iommu_domain *domain, struct iommu_fault_ids *fault_ids)
{
	WARN_ONCE(1, "iommu_get_fault_ids unimplemented");
	return 0;
}
void cam_check_iommu_faults(struct iommu_domain *domain,
	struct cam_smmu_pf_info *pf_info)
{
	struct iommu_fault_ids fault_ids = {0, 0, 0};

	if (iommu_get_fault_ids(domain, &fault_ids))
		CAM_ERR(CAM_SMMU, "Error: Can not get smmu fault ids");

	CAM_ERR(CAM_SMMU, "smmu fault ids bid:%d pid:%d mid:%d",
		fault_ids.bid, fault_ids.pid, fault_ids.mid);

	pf_info->bid = fault_ids.bid;
	pf_info->pid = fault_ids.pid;
	pf_info->mid = fault_ids.mid;
}

static int inline cam_subdev_list_cmp(struct cam_subdev *entry_1, struct cam_subdev *entry_2)
{
	if (entry_1->close_seq_prior > entry_2->close_seq_prior)
		return 1;
	else if (entry_1->close_seq_prior < entry_2->close_seq_prior)
		return -1;
	else
		return 0;
}

int cam_compat_util_get_dmabuf_va(struct dma_buf *dmabuf, uintptr_t *vaddr)
{
	struct iosys_map mapping = {0};
	int error_code = dma_buf_vmap(dmabuf, &mapping);

	if (error_code) {
		*vaddr = 0;
	} else {
		*vaddr = (mapping.is_iomem) ?
			(uintptr_t)mapping.vaddr_iomem : (uintptr_t)mapping.vaddr;
		CAM_DBG(CAM_MEM,
			"dmabuf=%p, *vaddr=%p, is_iomem=%d, vaddr_iomem=%p, vaddr=%p",
			dmabuf, *vaddr, mapping.is_iomem, mapping.vaddr_iomem, mapping.vaddr);
	}

	return error_code;
}

void cam_compat_util_put_dmabuf_va(struct dma_buf *dmabuf, void *vaddr)
{
	struct iosys_map mapping = IOSYS_MAP_INIT_VADDR(vaddr);

	dma_buf_vunmap(dmabuf, &mapping);
}

void cam_smmu_util_iommu_custom(struct device *dev,
	dma_addr_t discard_start, size_t discard_length)
{
	return;
}

int cam_get_ddr_type(void)
{
	int ret;
	u64 ddr_type;
	struct device_node *root_node;
	struct device_node *mem_node = NULL;

	root_node = of_find_node_by_path("/");

	if (root_node == NULL) {
		CAM_ERR(CAM_UTIL, "Unable to find root node");
		return -ENOENT;
	}

	do {
		mem_node = of_get_next_child(root_node, mem_node);
		if (of_node_name_prefix(mem_node, "memory")) {
			CAM_DBG(CAM_UTIL,
					"memory node found with full name %s",
					mem_node->full_name);
			break;
		}
	} while (mem_node != NULL);

	of_node_put(root_node);
	if (mem_node == NULL) {
		CAM_ERR(CAM_UTIL, "memory node not found");
		return -ENOENT;
	}

	ret = of_property_read_u64(mem_node, "ddr_device_type", &ddr_type);

	of_node_put(mem_node);
	if (ret < 0) {
		CAM_ERR(CAM_UTIL, "ddr_device_type read failed");
		return ret;
	}

	CAM_DBG(CAM_UTIL, "DDR Type %lld", ddr_type);
	return ddr_type;
}

int cam_req_mgr_ordered_list_cmp(void *priv,
	const struct list_head *head_1, const struct list_head *head_2)
{
	return cam_subdev_list_cmp(list_entry(head_1, struct cam_subdev, list),
		list_entry(head_2, struct cam_subdev, list));
}

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_i3c_driver_remove(struct i3c_device *client)
{
	CAM_DBG(CAM_SENSOR, "I3C remove invoked for %s",
		(client ? dev_name(&client->dev) : "none"));
}
#else
int cam_i3c_driver_remove(struct i3c_device *client)
{
    CAM_DBG(CAM_SENSOR, "I3C remove invoked for %s",
        (client ? dev_name(&client->dev) : "none"));
    return 0;
}
#endif

long cam_dma_buf_set_name(struct dma_buf *dmabuf, const char *name)
{
	return 0;
}

#ifdef CONFIG_SPECTRA_KT
int cam_compat_util_get_irq(struct cam_hw_soc_info *soc_info)
{
	int rc = 0;

	soc_info->irq_num = platform_get_irq(soc_info->pdev, 0);
	if (soc_info->irq_num < 0) {
		rc = soc_info->irq_num;
		return rc;
	}

	return rc;
}

#else

int cam_compat_util_get_irq(struct cam_hw_soc_info *soc_info)
{
	int rc = 0;

	soc_info->irq_num[0] = platform_get_irq(soc_info->pdev, 0);
	if (soc_info->irq_num[0] < 0) {
		rc = soc_info->irq_num[0];
		return rc;
	}

	return rc;
}
#endif /* ifdef CONFIG_SPECTRA_KT */

#ifdef CONFIG_SPECTRA_PARTIAL_CAMERA
int cam_get_subpart_info(uint32_t *part_info, uint32_t max_num_cam)
{
	int rc = 0;
	int num_cam;

	num_cam = socinfo_get_part_count(PART_CAMERA);
	if (num_cam != max_num_cam) {
		CAM_ERR(CAM_CPAS, "Unsupported number of parts: %d", num_cam);
		return -EINVAL;
	}

	/*
	 * If bit value in part_info is "0" then HW is available.
	 * If bit value in part_info is "1" then HW is unavailable.
	 */
	rc = socinfo_get_subpart_info(PART_CAMERA, part_info, num_cam);
	if (rc) {
		CAM_ERR(CAM_CPAS, "Failed while getting subpart_info, rc = %d.", rc);
		return rc;
	}

	return 0;
}
#else
int cam_get_subpart_info(uint32_t *part_info, uint32_t max_num_cam)
{
	return 0;
}
#endif

bool cam_secure_get_vfe_fd_port_config(void)
{
	return true;
}

inline struct icc_path *cam_icc_get_path(struct device *dev,
		const int src_id, const int dst_id, const char *path_name, bool use_path_name)
{
	CAM_DBG(CAM_UTIL, "Get icc path name: %s src_id:%d dst_id:%d use_path_name:%s", path_name,
		src_id, dst_id, CAM_BOOL_TO_YESNO(use_path_name));

	if (!use_path_name) {
		CAM_ERR(CAM_UTIL, "Must use path names to get icc path handle");
		return NULL;
	}

	return of_icc_get(dev, path_name);
}

void qcom_clk_dump(struct clk *clk, void *regulator,
		   bool calltrace)
{
	return;
}

#if CONFIG_SPECTRA_SENSOR
#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_actuator_i2c_driver_remove(struct i2c_client *client)
{
	cam_actuator_i2c_driver_remove_common(client);

	return;
}
#else
static int32_t cam_actuator_i2c_driver_remove(struct i2c_client *client)
{
	int rc = 0;

	rc = cam_actuator_i2c_driver_remove_common(client);

	return rc;
}
#endif

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_sensor_i2c_driver_remove(struct i2c_client *client)
{
	cam_sensor_i2c_driver_remove_common(client);

	return;
}
#else
static int cam_sensor_i2c_driver_remove(struct i2c_client *client)
{
	int rc = 0;

	rc = cam_sensor_i2c_driver_remove_common(client);

	return rc;
}
#endif

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_flash_i2c_driver_remove(struct i2c_client *client)
{
	cam_flash_i2c_driver_remove_common(client);

	return;
}
#else
static int32_t cam_flash_i2c_driver_remove(struct i2c_client *client)
{
	int rc = 0;

	rc = cam_flash_i2c_driver_remove_common(client);

	return rc;
}
#endif

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_eeprom_i2c_driver_remove(struct i2c_client *client)
{
	cam_eeprom_i2c_driver_remove_common(client);

	return;
}
#else
static int cam_eeprom_i2c_driver_remove(struct i2c_client *client)
{
	int rc = 0;

	rc = cam_eeprom_i2c_driver_remove_common(client);

	return rc;
}
#endif

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
void cam_ois_i2c_driver_remove(struct i2c_client *client)
{
	cam_ois_i2c_driver_remove_common(client);

	return;
}
#else
static int cam_ois_i2c_driver_remove(struct i2c_client *client)
{
	int rc = 0;

	rc = cam_ois_i2c_driver_remove_common(client);

	return rc;
}
#endif

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
void cam_eeprom_spi_driver_remove(struct spi_device *sdev)
{
	cam_eeprom_spi_driver_remove_common(sdev);

	return;
}
#else
static int32_t cam_eeprom_spi_driver_remove(struct spi_device *sdev)
{
	int rc = 0;

	rc = cam_eeprom_spi_driver_remove_common(sdev);

	return rc;
}
#endif
#endif
