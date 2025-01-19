// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#include <linux/platform_device.h>
#include <linux/virtio-iommu.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/pci_ids.h>

#include "arm-smmu-v3.h"
#include "../../iommu-sva.h"

/*
 * @probe_complete:
 * A positive number if arm_smmu_init_structures() has completed successfully,
 * and client devices can be added. Negative on error. Zero otherwise.
 */
struct arm_vsmmu_device {
	struct arm_smmu_device smmu;
	struct viommu_dev *viommu;
	int probe_complete;
	u32 idr[ARM_SMMU_IIDR/sizeof(u32) + 1];
};

#define ARM_VSMMU_DRIVER_NAME "arm-smmu-v3-virtio-pdev"
#define ARM_SMMU_MIN_VIRTIO_ID         1

static const char * const probe_t_field_names[] = {
	"IDR0",
	"IDR1",
	NULL, /* Reserved */
	"IDR3",
	NULL, /* Reserved */
	"IDR5",
};

/*
 * Ideally we would call arm_smmu_device_hw_probe & arm_smmu_init_structures
 * from arm_smm_virtio_device_probe. However, we don't know the idr register
 * values until after a client device calls ops->probe_device.
 *
 * An alternate implementation would be to call VIRTIO_IOMMU_T_PROBE with
 * incrementing endpoint ids until one returns successfully.
 */
static int arm_vsmmu_impl_probe_t_hw_arm_smmu3(struct arm_smmu_device *smmu, struct device *dev,
		struct virtio_iommu_probe_hw_arm_smmu3 *prop)
{
	struct arm_vsmmu_device *vsmmu = container_of(smmu, struct arm_vsmmu_device, smmu);
	int ret, i;
	u32 idr[ARRAY_SIZE(vsmmu->idr)] = {0};


	idr[0] = __le64_to_cpu(prop->idr0);
	idr[1] = __le64_to_cpu(prop->idr1);
	idr[3] = __le64_to_cpu(prop->idr3);
	idr[5] = __le64_to_cpu(prop->idr5);

	mutex_lock(&smmu->streams_mutex);
	if (vsmmu->probe_complete) {
		mutex_unlock(&smmu->streams_mutex);

		if (vsmmu->probe_complete < 0)
			return vsmmu->probe_complete;

		if (WARN(memcmp(idr, vsmmu->idr, sizeof(vsmmu->idr)),
				"%s Unexpected idr value %#x %#x %#x %#x\n",
				dev_name(dev), idr[0], idr[1], idr[3], idr[5]))
			return -EINVAL;
		return 0;
	}

	vsmmu->idr[0] = idr[0];
	vsmmu->idr[1] = idr[1];
	vsmmu->idr[3] = idr[3];
	vsmmu->idr[5] = idr[5];

	dev_info(smmu->dev, "Detected configuration:\n");
	for (i = 0; i < ARRAY_SIZE(probe_t_field_names); i++)
		if (probe_t_field_names[i])
			pr_info(" %s: %#x\n", probe_t_field_names[i], vsmmu->idr[i]);

	/* Now that idr registers are known, finish setting up the hardware */
	ret = arm_smmu_device_hw_probe(smmu);
	if (ret)
		goto out_unlock;

	/*
	 * Reduce the size of queues to prevent memory waste. Min cmdq size is derived from
	 * check in arm_smmu_device_hw_probe.
	 */
	smmu->cmdq.q.llq.max_n_shift = ilog2(CMDQ_BATCH_ENTRIES) + 1;
	smmu->evtq.q.llq.max_n_shift = 1;
	if (smmu->features & ARM_SMMU_FEAT_PRI)
		smmu->priq.q.llq.max_n_shift = 1;

	/* Initialise in-memory data structures */
	ret = arm_smmu_init_structures(smmu);
	if (ret)
		goto out_unlock;

out_unlock:
	vsmmu->probe_complete = ret ? ret : true;
	mutex_unlock(&smmu->streams_mutex);
	return ret;
}

/*
 * Based on viommu_probe_endpoint() from virtio-iommu.c.
 * Deltas:
 * dev_iommu_priv_get() is not a 'struct viommu_endpoint'
 * PROBE_T_RESV_MEM unsupported
 */
static int arm_vsmmu_impl_probe_device(struct arm_smmu_device *smmu, struct device *dev)
{
	int ret;
	u16 type, len;
	size_t cur = 0;
	size_t probe_len;
	struct virtio_iommu_req_probe *probe;
	struct virtio_iommu_probe_property *prop;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct viommu_dev *viommu = dev_to_virtio(smmu->dev->parent)->priv;

	if (!fwspec->num_ids)
		return -EINVAL;

	probe_len = sizeof(*probe) + viommu->probe_size +
		    sizeof(struct virtio_iommu_req_tail);
	probe = kzalloc(probe_len, GFP_KERNEL);
	if (!probe)
		return -ENOMEM;

	probe->head.type = VIRTIO_IOMMU_T_PROBE;
	/*
	 * For now, assume that properties of an endpoint that outputs multiple
	 * IDs are consistent. Only probe the first one.
	 */
	probe->endpoint = cpu_to_le32(fwspec->ids[0]);

	ret = viommu_send_req_sync(viommu, probe, probe_len);
	if (ret)
		goto out_free;

	prop = (void *)probe->properties;
	type = le16_to_cpu(prop->type) & VIRTIO_IOMMU_PROBE_T_MASK;

	while (type != VIRTIO_IOMMU_PROBE_T_NONE &&
	       cur < viommu->probe_size) {
		len = le16_to_cpu(prop->length) + sizeof(*prop);

		switch (type) {
		case VIRTIO_IOMMU_PROBE_T_RESV_MEM:
			/* ignore */
			break;
		case VIRTIO_IOMMU_PROBE_T_HW_ARM_SMMU3:
			ret = arm_vsmmu_impl_probe_t_hw_arm_smmu3(smmu, dev, (void *)prop);
			break;
		default:
			dev_err(dev, "unknown viommu prop 0x%x\n", type);
		}

		if (ret) {
			dev_err(dev, "failed to parse viommu prop 0x%x\n", type);
			goto out_free;
		}

		cur += len;
		if (cur >= viommu->probe_size)
			break;

		prop = (void *)probe->properties + cur;
		type = le16_to_cpu(prop->type) & VIRTIO_IOMMU_PROBE_T_MASK;
	}

out_free:
	kfree(probe);
	return ret;
}

static u32 arm_vsmmu_impl_read_idr(struct arm_smmu_device *smmu, u32 offset)
{
	struct arm_vsmmu_device *vsmmu;
	u32 index;
	u32 val;

	vsmmu = container_of(smmu, struct arm_vsmmu_device, smmu);

	index = offset / sizeof(*vsmmu->idr);
	if (WARN(index >= ARRAY_SIZE(vsmmu->idr), "Unexpected idr offset: %x\n", offset))
		return 0;

	val = vsmmu->idr[index];
	return val;
}

static int arm_vsmmu_impl_detach_table(struct arm_smmu_device *smmu,
			struct arm_smmu_domain *old_domain,
			__le64 *step, u32 sid)
{
	struct arm_vsmmu_device *vsmmu = container_of(smmu, struct arm_vsmmu_device, smmu);
	struct viommu_dev *viommu = vsmmu->viommu;
	int ret;
	unsigned long flags;
	struct virtio_iommu_req_detach req = {0};

	if (!old_domain)
		return 0;

	req.head.type = VIRTIO_IOMMU_T_DETACH;
	req.domain = cpu_to_le32(old_domain->virtio.id);
	req.endpoint = cpu_to_le32(sid);

	spin_lock_irqsave(&old_domain->virtio.lock, flags);
	ret = viommu_send_req_sync(viommu, &req, sizeof(req));

	if (refcount_dec_and_test(&old_domain->virtio.refs)) {
		ida_free(&viommu->domain_ids, old_domain->virtio.id);
		old_domain->virtio.id = 0;
	}
	spin_unlock_irqrestore(&old_domain->virtio.lock, flags);

	WARN(ret, "%s: VIRTIO_IOMMU_T_DETACH: Domain: %#x Sid: %#x\n",
			__func__,  le32_to_cpu(req.domain), sid);
	return ret;
}

static int arm_vsmmu_impl_attach_table(struct arm_smmu_device *smmu,
			struct arm_smmu_domain *new_domain,
			struct arm_smmu_domain *old_domain,
			__le64 *step, u32 sid)
{
	struct arm_vsmmu_device *vsmmu = container_of(smmu, struct arm_vsmmu_device, smmu);
	struct viommu_dev *viommu = vsmmu->viommu;
	int ret;
	unsigned long flags;
	struct virtio_iommu_req_attach_table_arm_smmu3 req = {0};

	/*
	 * Detach first if already attached. Virtio-Iommu devices may or
	 * may not support attaching to an endpoint which is already attached
	 * to a domain.
	 */
	ret = arm_vsmmu_impl_detach_table(smmu, old_domain, step, sid);
	if (ret)
		return ret;

	if (!new_domain)
		return 0;

	spin_lock_irqsave(&new_domain->virtio.lock, flags);
	if (!new_domain->virtio.id) {
		ret = ida_alloc_range(&viommu->domain_ids,
				max(ARM_SMMU_MIN_VIRTIO_ID, viommu->first_domain),
				viommu->last_domain, GFP_KERNEL);
		if (ret < 0) {
			spin_unlock_irqrestore(&new_domain->virtio.lock, flags);
			return ret;
		}

		new_domain->virtio.id = ret;
		refcount_set(&new_domain->virtio.refs, 1);
	} else {
		refcount_inc(&new_domain->virtio.refs);
	}

	req.head.type = VIRTIO_IOMMU_T_ATTACH_TABLE;
	req.domain = cpu_to_le32(new_domain->virtio.id);
	req.endpoint = cpu_to_le32(sid);
	req.format = VIRTIO_IOMMU_ATTACH_TABLE_ARM_SMMU3;
	req.ste0 = step[0];
	req.ste1 = step[1];

	ret = viommu_send_req_sync(viommu, &req, sizeof(req));
	if (WARN(ret, "%s: VIRTIO_IOMMU_T_ATTACH: Domain: %#x Sid: %#x ste0: %#llx ste1:%#llx\n",
				__func__, new_domain->virtio.id, sid,
				 le64_to_cpu(step[0]), le64_to_cpu(step[1])))
		goto out_free_ida;

	spin_unlock_irqrestore(&new_domain->virtio.lock, flags);
	return 0;

out_free_ida:
	if (refcount_dec_and_test(&new_domain->virtio.refs)) {
		ida_free(&viommu->domain_ids, new_domain->virtio.id);
		new_domain->virtio.id = 0;
	}
	spin_unlock_irqrestore(&new_domain->virtio.lock, flags);
	return ret;
}

/* Based on arm_smmu_install_ste_for_dev */
static int arm_vsmmu_impl_install_ste(struct arm_smmu_master *master,
				struct arm_smmu_domain *new_domain,
				struct arm_smmu_domain *old_domain)
{
	int i, j, ret;
	struct arm_smmu_device *smmu = master->smmu;

	for (i = 0; i < master->num_streams; ++i) {
		u32 sid = master->streams[i].id;
		__le64 *step = arm_smmu_get_step_for_sid(smmu, sid);

		/* Bridged PCI devices may end up with duplicated IDs */
		for (j = 0; j < i; j++)
			if (master->streams[j].id == sid)
				break;
		if (j < i)
			continue;

		ret = arm_vsmmu_impl_attach_table(smmu, new_domain, old_domain, step, sid);
		if (ret)
			break;
	}

	if (ret) {
		for (i--; i >= 0; i--) {
			u32 sid = master->streams[i].id;
			__le64 *step = arm_smmu_get_step_for_sid(smmu, sid);

			/* Bridged PCI devices may end up with duplicated IDs */
			for (j = 0; j < i; j++)
				if (master->streams[j].id == sid)
					break;
			if (j < i)
				continue;

			arm_vsmmu_impl_attach_table(smmu, NULL, new_domain, step, sid);
		}
	}

	return ret;
}

static const struct arm_smmu_impl_ops vsmmu_impl_ops = {
	.read_idr = arm_vsmmu_impl_read_idr,
	.probe_device = arm_vsmmu_impl_probe_device,
	.install_ste = arm_vsmmu_impl_install_ste,
};

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
	smmu->impl_ops = &vsmmu_impl_ops;

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
