/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef _LINUX_VIRTIO_IOMMU_QCOM_H
#define _LINUX_VIRTIO_IOMMU_QCOM_H

#include <linux/iommu.h>
#include <linux/virtio.h>
#include <uapi/linux/virtio_iommu.h>

#define VIOMMU_REQUEST_VQ		0
#define VIOMMU_EVENT_VQ			1
#define VIOMMU_NR_VQS			2

struct viommu_dev {
	struct iommu_device		iommu;
	struct device			*dev;
	struct virtio_device		*vdev;

	struct ida			domain_ids;

	struct virtqueue		*vqs[VIOMMU_NR_VQS];
	spinlock_t			request_lock;
	struct list_head		requests;
	void				*evts;

	/* Device configuration */
	struct iommu_domain_geometry	geometry;
	u64				pgsize_bitmap;
	u32				first_domain;
	u32				last_domain;
	/* Supported MAP flags */
	u32				map_flags;
	u32				probe_size;

	void				(*fault_handler)(struct viommu_dev *viommu,
						struct virtio_iommu_fault *fault);
};

int viommu_probe_common(struct virtio_device *vdev);
int viommu_send_req_sync(struct viommu_dev *viommu, void *buf,
				size_t len);

#endif /* _LINUX_VIRTIO_IOMMU_QCOM_H */
