// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#include <linux/platform_device.h>
#include <linux/virtio-iommu.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/pci_ids.h>

#include "arm-smmu-v3.h"
#include "../../iommu-sva.h"

struct arm_vsmmu_device {
	struct arm_smmu_device smmu;
	struct viommu_dev *viommu;
};

#define ARM_VSMMU_DRIVER_NAME "arm-smmu-v3-virtio-pdev"

static int arm_smmu_virtio_device_probe(struct virtio_device *vdev)
{
	struct device *parent_dev = vdev->dev.parent;
	struct device *dev = &vdev->dev;
	int ret;
	struct platform_device *child;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1) ||
	     virtio_has_feature(vdev, VIRTIO_IOMMU_F_MAP_UNMAP))
		return -ENODEV;

	ret = viommu_probe_common(vdev);
	if (ret)
		return ret;

	child = platform_device_alloc(ARM_VSMMU_DRIVER_NAME, vdev->index);
	if (!child)
		goto err_free_vqs;
	child->dev.parent = dev;
	device_set_node(&child->dev, dev_fwnode(parent_dev));
	ret = platform_device_add(child);
	if (ret) {
		dev_err(dev, "Failed to add child device\n");
		goto err_platform_device;
	}

	return 0;

err_platform_device:
	platform_device_put(child);

err_free_vqs:
	/* Stop all virtqueues */
	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);

	return ret;
}

static int arm_smmu_virtio_remove_child(struct device *dev, void *unused)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);

	platform_device_unregister(pdev);
	return 0;
}

static void arm_smmu_virtio_device_remove(struct virtio_device *vdev)
{

	device_for_each_child(&vdev->dev, NULL, arm_smmu_virtio_remove_child);

	/* Stop all virtqueues */
	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);

	dev_dbg(&vdev->dev, "device removed\n");
}

static void arm_smmu_virtio_config_changed(struct virtio_device *vdev)
{
	dev_warn(&vdev->dev, "config changed\n");
}

static unsigned int arm_smmu_virtio_features[] = {
	VIRTIO_IOMMU_F_ATTACH_TABLE,
	/*
	 * We don't support MAP_UNMAP, but need to mention it in order to call
	 * virtio_has_feature with MAP_UNMAP later. The upstream virtio-iommu
	 * driver will observe no MAP_UNMAP support and will not probe.
	 */
	VIRTIO_IOMMU_F_MAP_UNMAP,
	VIRTIO_IOMMU_F_PROBE,
	/*
	 * We don't actually support below features.
	 * They are listed here to avoid a splat from virtio_cread_le_feature
	 * in viommu_probe_common.
	 */
	VIRTIO_IOMMU_F_INPUT_RANGE,
	VIRTIO_IOMMU_F_DOMAIN_RANGE,
	VIRTIO_IOMMU_F_MMIO,
};

static struct virtio_device_id arm_smmu_virtio_id_table[] = {
	{ VIRTIO_ID_IOMMU, PCI_VENDOR_ID_QCOM },
	{ 0 }
};
MODULE_DEVICE_TABLE(virtio, arm_smmu_virtio_id_table);

static struct virtio_driver arm_smmu_virtio_driver = {
	.driver.name		= KBUILD_MODNAME,
	.driver.owner		= THIS_MODULE,
	.id_table		= arm_smmu_virtio_id_table,
	.feature_table		= arm_smmu_virtio_features,
	.feature_table_size	= ARRAY_SIZE(arm_smmu_virtio_features),
	.probe			= arm_smmu_virtio_device_probe,
	.remove			= arm_smmu_virtio_device_remove,
	.config_changed		= arm_smmu_virtio_config_changed,
};

/*
 * This platform device is dynamically created by arm_smmu_virtio_driver.
 * Calling dma-apis such as dmam_alloc_coherent & dma_set_mask_and_coherent
 * is not supported by the virtio_bus; thus a child device is used instead.
 *
 * Compared to a standard arm-smmu-v3 driver, this virtio-iommu driver has
 * reduced functionality. It does not have direct access to ARM smmu v3
 * registers, interrupts or queues; these are exposed in a limited manner
 * through the virtio-iommu interface for arm-smmu-v3.
 *
 * raw IDR registers are read through VIRTIO_IOMMU_T_PROBE, and parsed in
 * arm_smmu_device_hw_probe() via impl_ops->read_idr.
 *
 * arm_smmu_init_structures allocates memory for queues and the STE table.
 * The STE table is a local copy of the raw ste values which will be sent
 * though VIRTIO_IOMMU_T_ATTACH_TABLE. The queues are currently unused.
 *
 * Unused:
 * smmu->base
 * smmu->page1
 * smmu->evtq.q.irq
 * smmu->priq.q.irq
 * smmu->gerr_irq
 * smmu->combined_irq
 */
static int arm_vsmmu_device_probe(struct platform_device *pdev)
{
	int ret;
	struct arm_vsmmu_device *vsmmu;
	struct arm_smmu_device *smmu;
	struct device *dev = &pdev->dev;

	vsmmu = devm_kzalloc(dev, sizeof(*vsmmu), GFP_KERNEL);
	if (!vsmmu)
		return -ENOMEM;

	/* Few virtio details */
	vsmmu->viommu = dev_to_virtio(dev->parent)->priv;
	smmu = &vsmmu->smmu;

	smmu->dev = dev;
	smmu->options |= ARM_SMMU_OPT_VIRTIO;
	smmu->options |= ARM_SMMU_OPT_SKIP_PREFETCH;

	ret = arm_smmu_device_dt_probe(pdev, smmu);
	if (ret)
		return ret;

	/* Record our private device structure */
	platform_set_drvdata(pdev, smmu);

	return 0;
}

static void arm_vsmmu_device_remove(struct platform_device *pdev)
{
	struct arm_smmu_device *smmu = platform_get_drvdata(pdev);

	iopf_queue_free(smmu->evtq.iopf);
	ida_destroy(&smmu->vmid_map);
}

struct platform_driver arm_vsmmu_driver = {
	.driver	= {
		.name			= ARM_VSMMU_DRIVER_NAME,
		.suppress_bind_attrs	= true,
	},
	.probe	= arm_vsmmu_device_probe,
	.remove_new = arm_vsmmu_device_remove,
};

int arm_smmu_qcom_virtio_init(void)
{
	int ret;

	ret = register_virtio_driver(&arm_smmu_virtio_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&arm_vsmmu_driver);
	if (ret) {
		unregister_virtio_driver(&arm_smmu_virtio_driver);
		return ret;
	}

	return 0;
}

void arm_smmu_qcom_virtio_exit(void)
{
	platform_driver_unregister(&arm_vsmmu_driver);
	unregister_virtio_driver(&arm_smmu_virtio_driver);
}
