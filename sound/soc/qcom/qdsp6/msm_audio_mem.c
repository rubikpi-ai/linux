// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/iosys-map.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/ioctl.h>
#include <linux/platform_device.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <soc/qcom/secure_buffer.h>
#include "msm_audio_mem.h"

MODULE_IMPORT_NS(DMA_BUF);

#define MSM_AUDIO_MEM_PROBED (1 << 0)

#define MSM_AUDIO_MEM_PHYS_ADDR(alloc_data) \
	alloc_data->table->sgl->dma_address

#define MSM_AUDIO_SMMU_SID_OFFSET 32
#define MSM_AUDIO_MEM_DRIVER_NAME "msm_audio_mem"
#define MINOR_NUMBER_COUNT 1
struct msm_audio_mem_private {
	bool smmu_enabled;
	struct device *cb_dev;
	u8 device_status;
	struct list_head alloc_list;
	struct mutex list_mutex;
	u64 smmu_sid_bits;
	char *driver_name;
	/*char dev related data */
	dev_t mem_major;
	struct class *mem_class;
	struct device *chardev;
	struct cdev cdev;
};

struct msm_audio_alloc_data {
	size_t len;
	struct iosys_map *vmap;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *table;
	struct list_head list;
};

struct msm_audio_mem_fd_list_private {
	struct mutex list_mutex;
	/*list to store fd, phy. addr and handle data */
	struct list_head fd_list;
};

static struct msm_audio_mem_fd_list_private msm_audio_mem_fd_list = {0,};
static bool msm_audio_mem_fd_list_init;

struct msm_audio_fd_data {
	int fd;
	size_t plen;
	void *handle;
	dma_addr_t paddr;
	struct device *dev;
	struct list_head list;
	bool hyp_assign;
};

static void msm_audio_mem_add_allocation(
	struct msm_audio_mem_private *msm_audio_mem_data,
	struct msm_audio_alloc_data *alloc_data)
{
	/*
	 * Since these APIs can be invoked by multiple
	 * clients, there is need to make sure the list
	 * of allocations is always protected
	 */
	mutex_lock(&(msm_audio_mem_data->list_mutex));
	list_add_tail(&(alloc_data->list),
		      &(msm_audio_mem_data->alloc_list));
	mutex_unlock(&(msm_audio_mem_data->list_mutex));
}

static int msm_audio_mem_map_kernel(struct dma_buf *dma_buf,
	struct msm_audio_mem_private *mem_data, struct iosys_map *iosys_vmap)
{
	int rc = 0;
	struct msm_audio_alloc_data *alloc_data = NULL;

	rc = dma_buf_begin_cpu_access(dma_buf, DMA_BIDIRECTIONAL);
	if (rc) {
		pr_err("%s: kmap dma_buf_begin_cpu_access fail\n", __func__);
		goto exit;
	}

	rc = dma_buf_vmap(dma_buf, iosys_vmap);
	if (rc) {
		pr_err("%s: kernel mapping of dma_buf failed\n",
		       __func__);
		goto exit;
	}

	/*
	 * TBD: remove the below section once new API
	 * for mapping kernel virtual address is available.
	 */
	mutex_lock(&(mem_data->list_mutex));
	list_for_each_entry(alloc_data, &(mem_data->alloc_list),
			    list) {
		if (alloc_data->dma_buf == dma_buf) {
			alloc_data->vmap = iosys_vmap;
			break;
		}
	}
	mutex_unlock(&(mem_data->list_mutex));

exit:
	return rc;
}

static int msm_audio_dma_buf_map(struct dma_buf *dma_buf,
				 dma_addr_t *addr, size_t *len, bool is_iova,
				 struct msm_audio_mem_private *mem_data)
{

	struct msm_audio_alloc_data *alloc_data = NULL;
	int rc = 0;
	struct iosys_map *iosys_vmap = NULL;
	struct device *cb_dev = mem_data->cb_dev;

	iosys_vmap = kzalloc(sizeof(*iosys_vmap), GFP_KERNEL);
	if (!iosys_vmap)
		return -ENOMEM;
	/* Data required per buffer mapping */
	alloc_data = kzalloc(sizeof(*alloc_data), GFP_KERNEL);
	if (!alloc_data) {
		kfree(iosys_vmap);
		return -ENOMEM;
	}
	alloc_data->dma_buf = dma_buf;
	alloc_data->len = dma_buf->size;
	*len = dma_buf->size;

	/* Attach the dma_buf to context bank device */
	alloc_data->attach = dma_buf_attach(alloc_data->dma_buf,
					    cb_dev);
	if (IS_ERR(alloc_data->attach)) {
		rc = PTR_ERR(alloc_data->attach);
		dev_err(cb_dev,
			"%s: Fail to attach dma_buf to CB, rc = %d\n",
			__func__, rc);
		goto free_alloc_data;
	}

	/*
	 * Get the scatter-gather list.
	 * There is no info as this is a write buffer or
	 * read buffer, hence the request is bi-directional
	 * to accommodate both read and write mappings.
	 */
	alloc_data->table = dma_buf_map_attachment(alloc_data->attach,
				DMA_BIDIRECTIONAL);
	if (IS_ERR(alloc_data->table)) {
		rc = PTR_ERR(alloc_data->table);
		dev_err(cb_dev,
			"%s: Fail to map attachment, rc = %d\n",
			__func__, rc);
		goto detach_dma_buf;
	}

	/* physical address from mapping */
	if (!is_iova) {
		*addr = sg_phys(alloc_data->table->sgl);
		rc = msm_audio_mem_map_kernel((void *)dma_buf, mem_data, iosys_vmap);
		if (rc) {
			pr_err("%s: MEM memory mapping for AUDIO failed, err:%d\n",
				__func__, rc);
			rc = -ENOMEM;
			goto detach_dma_buf;
		}
		alloc_data->vmap = iosys_vmap;
	} else {
		*addr = MSM_AUDIO_MEM_PHYS_ADDR(alloc_data);
	}

	msm_audio_mem_add_allocation(mem_data, alloc_data);
	return rc;

detach_dma_buf:
	dma_buf_detach(alloc_data->dma_buf,
		       alloc_data->attach);
free_alloc_data:
	kfree(iosys_vmap);
	kfree(alloc_data);
	alloc_data = NULL;

	return rc;
}

static int msm_audio_dma_buf_unmap(struct dma_buf *dma_buf, struct msm_audio_mem_private *mem_data)
{
	int rc = 0;
	struct msm_audio_alloc_data *alloc_data = NULL;
	struct list_head *ptr, *next;
	bool found = false;
	struct device *cb_dev = mem_data->cb_dev;

	/*
	 * Though list_for_each_safe is delete safe, lock
	 * should be explicitly acquired to avoid race condition
	 * on adding elements to the list.
	 */
	mutex_lock(&(mem_data->list_mutex));
	list_for_each_safe(ptr, next,
			    &(mem_data->alloc_list)) {

		alloc_data = list_entry(ptr, struct msm_audio_alloc_data,
					list);

		if (alloc_data->dma_buf == dma_buf) {
			found = true;
			dma_buf_unmap_attachment(alloc_data->attach,
						 alloc_data->table,
						 DMA_BIDIRECTIONAL);

			dma_buf_detach(alloc_data->dma_buf,
				       alloc_data->attach);

			dma_buf_put(alloc_data->dma_buf);

			list_del(&(alloc_data->list));
			kfree(alloc_data->vmap);
			kfree(alloc_data);
			alloc_data = NULL;
			break;
		}
	}
	mutex_unlock(&(mem_data->list_mutex));

	if (!found) {
		dev_err(cb_dev,
			"%s: cannot find allocation, dma_buf %pK\n",
			__func__, dma_buf);
		rc = -EINVAL;
	}

	return rc;
}

static int msm_audio_mem_get_phys(struct dma_buf *dma_buf,
				  dma_addr_t *addr, size_t *len, bool is_iova,
				  struct msm_audio_mem_private *mem_data)
{
	int rc = 0;

	rc = msm_audio_dma_buf_map(dma_buf, addr, len, is_iova, mem_data);
	if (rc) {
		pr_err("%s: failed to map DMA buf, err = %d\n",
			__func__, rc);
		goto err;
	}
	if (mem_data->smmu_enabled && is_iova) {
		/* Append the SMMU SID information to the IOVA address */
		*addr |= mem_data->smmu_sid_bits;
	}

	pr_debug("phys=%pK, len=%zd, rc=%d\n", &(*addr), *len, rc);
err:
	return rc;
}

static int msm_audio_mem_unmap_kernel(struct dma_buf *dma_buf,
		struct msm_audio_mem_private *mem_data)
{
	int rc = 0;
	struct iosys_map *iosys_vmap = NULL;
	struct msm_audio_alloc_data *alloc_data = NULL;
	struct device *cb_dev = mem_data->cb_dev;

	/*
	 * TBD: remove the below section once new API
	 * for unmapping kernel virtual address is available.
	 */
	mutex_lock(&(mem_data->list_mutex));
	list_for_each_entry(alloc_data, &(mem_data->alloc_list),
			    list) {
		if (alloc_data->dma_buf == dma_buf) {
			iosys_vmap = alloc_data->vmap;
			break;
		}
	}
	mutex_unlock(&(mem_data->list_mutex));

	if (!iosys_vmap) {
		dev_err(cb_dev,
			"%s: cannot find allocation for dma_buf %pK\n",
			__func__, dma_buf);
		rc = -EINVAL;
		goto err;
	}

	dma_buf_vunmap(dma_buf, iosys_vmap);

	rc = dma_buf_end_cpu_access(dma_buf, DMA_BIDIRECTIONAL);
	if (rc) {
		dev_err(cb_dev, "%s: kmap dma_buf_end_cpu_access fail\n",
			__func__);
		goto err;
	}

err:
	return rc;
}

static int msm_audio_mem_map_buf(struct dma_buf *dma_buf, dma_addr_t *paddr,
				 size_t *plen, struct iosys_map *iosys_vmap,
				 struct msm_audio_mem_private *mem_data)
{
	int rc = 0;
	bool is_iova = true;

	if (!dma_buf || !paddr || !plen) {
		pr_err("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	rc = msm_audio_mem_get_phys(dma_buf, paddr, plen, is_iova, mem_data);
	if (rc) {
		pr_err("%s: MEM Get Physical for AUDIO failed, rc = %d\n",
				__func__, rc);
		dma_buf_put(dma_buf);
		goto err;
	}

	rc = msm_audio_mem_map_kernel(dma_buf, mem_data, iosys_vmap);
	if (rc) {
		pr_err("%s: MEM memory mapping for AUDIO failed, err:%d\n",
			__func__, rc);
		rc = -ENOMEM;
		msm_audio_dma_buf_unmap(dma_buf, mem_data);
		goto err;
	}

err:
	return rc;
}

void msm_audio_fd_list_debug(void)
{
	struct msm_audio_fd_data *msm_audio_fd_data = NULL;

	list_for_each_entry(msm_audio_fd_data,
			&msm_audio_mem_fd_list.fd_list, list) {
		pr_debug("%s fd %d handle %pK phy. addr %pK\n", __func__,
			msm_audio_fd_data->fd, msm_audio_fd_data->handle,
			(void *)msm_audio_fd_data->paddr);
	}
}

void msm_audio_update_fd_list(struct msm_audio_fd_data *msm_audio_fd_data)
{
	struct msm_audio_fd_data *msm_audio_fd_data1 = NULL;

	mutex_lock(&(msm_audio_mem_fd_list.list_mutex));
	list_for_each_entry(msm_audio_fd_data1,
			&msm_audio_mem_fd_list.fd_list, list) {
		if (msm_audio_fd_data1->fd == msm_audio_fd_data->fd) {
			pr_err("%s fd already present, not updating the list\n",
				__func__);
			mutex_unlock(&(msm_audio_mem_fd_list.list_mutex));
			return;
		}
	}
	list_add_tail(&msm_audio_fd_data->list, &msm_audio_mem_fd_list.fd_list);
	mutex_unlock(&(msm_audio_mem_fd_list.list_mutex));
}

void msm_audio_delete_fd_entry(void *handle)
{
	struct msm_audio_fd_data *msm_audio_fd_data = NULL;
	struct list_head *ptr, *next;

	mutex_lock(&(msm_audio_mem_fd_list.list_mutex));
	list_for_each_safe(ptr, next,
			&msm_audio_mem_fd_list.fd_list) {
		msm_audio_fd_data = list_entry(ptr, struct msm_audio_fd_data,
					list);
		if (msm_audio_fd_data->handle == handle) {
			pr_debug("%s deleting handle %pK entry from list\n",
				__func__, handle);
			list_del(&(msm_audio_fd_data->list));
			kfree(msm_audio_fd_data);
			break;
		}
	}
	mutex_unlock(&(msm_audio_mem_fd_list.list_mutex));
}

int msm_audio_get_phy_addr(int fd, dma_addr_t *paddr, size_t *pa_len)
{
	struct msm_audio_fd_data *msm_audio_fd_data = NULL;
	int status = -EINVAL;

	if (!paddr) {
		pr_err("%s Invalid paddr param status %d\n", __func__, status);
		return status;
	}
	pr_debug("%s, fd %d\n", __func__, fd);
	mutex_lock(&(msm_audio_mem_fd_list.list_mutex));
	list_for_each_entry(msm_audio_fd_data,
			&msm_audio_mem_fd_list.fd_list, list) {
		if (msm_audio_fd_data->fd == fd) {
			*paddr = msm_audio_fd_data->paddr;
			*pa_len = msm_audio_fd_data->plen;
			status = 0;
			pr_debug("%s Found fd %d paddr %pK\n",
				__func__, fd, paddr);
			mutex_unlock(&(msm_audio_mem_fd_list.list_mutex));
			return status;
		}
	}
	mutex_unlock(&(msm_audio_mem_fd_list.list_mutex));
	return status;
}
EXPORT_SYMBOL_GPL(msm_audio_get_phy_addr);

int msm_audio_set_hyp_assign(int fd, bool assign)
{
	struct msm_audio_fd_data *msm_audio_fd_data = NULL;
	int status = -EINVAL;

	mutex_lock(&(msm_audio_mem_fd_list.list_mutex));
	list_for_each_entry(msm_audio_fd_data,
			&msm_audio_mem_fd_list.fd_list, list) {
		if (msm_audio_fd_data->fd == fd) {
			status = 0;
			pr_debug("%s Found fd %d\n", __func__, fd);
			msm_audio_fd_data->hyp_assign = assign;
			mutex_unlock(&(msm_audio_mem_fd_list.list_mutex));
			return status;
		}
	}
	mutex_unlock(&(msm_audio_mem_fd_list.list_mutex));
	return status;
}

void msm_audio_get_handle(int fd, void **handle)
{
	struct msm_audio_fd_data *msm_audio_fd_data = NULL;

	pr_debug("%s fd %d\n", __func__, fd);
	mutex_lock(&(msm_audio_mem_fd_list.list_mutex));
	list_for_each_entry(msm_audio_fd_data,
			&msm_audio_mem_fd_list.fd_list, list) {
		if (msm_audio_fd_data->fd == fd) {
			*handle = (struct dma_buf *)msm_audio_fd_data->handle;
			pr_debug("%s handle %pK\n", __func__, *handle);
			break;
		}
	}
	mutex_unlock(&(msm_audio_mem_fd_list.list_mutex));
}

/**
 * msm_audio_mem_import-
 *        Import MEM buffer with given file descriptor
 *
 * @dma_buf: dma_buf for the MEM memory
 * @fd: file descriptor for the MEM memory
 * @bufsz: buffer size
 * @paddr: Physical address to be assigned with allocated region
 * @plen: length of allocated region to be assigned
 * @iosys_vmap: Virtual mapping vmap pointer to be assigned
 *
 * Returns 0 on success or error on failure
 */
static int msm_audio_mem_import(struct dma_buf **dma_buf, int fd,
			size_t bufsz, dma_addr_t *paddr,
			size_t *plen, struct iosys_map *iosys_vmap,
			struct msm_audio_mem_private *mem_data)
{
	int rc = 0;

	if (!(mem_data->device_status & MSM_AUDIO_MEM_PROBED)) {
		pr_debug("%s: probe is not done, deferred\n", __func__);
		return -EPROBE_DEFER;
	}

	if (!dma_buf || !paddr || !plen) {
		pr_err("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	/* bufsz should be 0 and fd shouldn't be 0 as of now */
	*dma_buf = dma_buf_get(fd);
	pr_debug("%s: dma_buf =%pK, fd=%d\n", __func__, *dma_buf, fd);
	if (IS_ERR_OR_NULL((void *)(*dma_buf))) {
		pr_err("%s: dma_buf_get failed\n", __func__);
		return -EINVAL;
	}

	if (mem_data->smmu_enabled) {
		rc = msm_audio_mem_map_buf(*dma_buf, paddr, plen, iosys_vmap, mem_data);
		if (rc) {
			pr_err("%s: failed to map MEM buf, rc = %d\n", __func__, rc);
			goto err;
		}
		pr_debug("%s: mapped address = %pK, size=%zd\n", __func__,
				iosys_vmap->vaddr, bufsz);
	} else {
		msm_audio_dma_buf_map(*dma_buf, paddr, plen, true, mem_data);
	}
	return 0;
err:
	dma_buf_put(*dma_buf);
	*dma_buf = NULL;
	return rc;
}

/**
 * msm_audio_mem_free -
 *        fress MEM memory for given client and handle
 *
 * @dma_buf: dma_buf for the MEM memory
 *
 * Returns 0 on success or error on failure
 */
static int msm_audio_mem_free(struct dma_buf *dma_buf, struct msm_audio_mem_private *mem_data)
{
	int ret = 0;

	if (!dma_buf) {
		pr_err("%s: dma_buf invalid\n", __func__);
		return -EINVAL;
	}

	if (mem_data->smmu_enabled) {
		ret = msm_audio_mem_unmap_kernel(dma_buf, mem_data);
		if (ret)
			return ret;
	}

	msm_audio_dma_buf_unmap(dma_buf, mem_data);

	return 0;
}

static int msm_audio_hyp_unassign(struct msm_audio_fd_data *msm_audio_fd_data)
{
	int ret = 0;
	u64 src_vmid_unmap_list = BIT(VMID_LPASS) | BIT(VMID_ADSP_HEAP);
	struct qcom_scm_vmperm dst_vmids_unmap[] = {{QCOM_SCM_VMID_HLOS,
		PERM_READ | PERM_WRITE | PERM_EXEC}};

	if (msm_audio_fd_data->hyp_assign) {
		ret = qcom_scm_assign_mem(msm_audio_fd_data->paddr, msm_audio_fd_data->plen,
				&src_vmid_unmap_list, dst_vmids_unmap, ARRAY_SIZE(dst_vmids_unmap));
		if (ret < 0) {
			pr_err("%s: qcom assign unmap failed result = %d addr = 0x%llx size = %zu\n",
				__func__, ret, msm_audio_fd_data->paddr, msm_audio_fd_data->plen);
		}
		msm_audio_fd_data->hyp_assign = false;
		pr_debug("%s: qcom scm unmap success\n", __func__);
	}
	return ret;
}

/**
 * msm_audio_mem_crash_handler -
 *        handles cleanup after userspace crashes.
 *
 * To be called from machine driver.
 */
void msm_audio_mem_crash_handler(void)
{
	struct msm_audio_fd_data *msm_audio_fd_data = NULL;
	struct list_head *ptr, *next;
	void *handle = NULL;
	struct msm_audio_mem_private *mem_data = NULL;

	mutex_lock(&(msm_audio_mem_fd_list.list_mutex));
	list_for_each_entry(msm_audio_fd_data,
		&msm_audio_mem_fd_list.fd_list, list) {
		handle = msm_audio_fd_data->handle;
		mem_data = dev_get_drvdata(msm_audio_fd_data->dev);
		/*  clean if CMA was used*/
		if (msm_audio_fd_data->hyp_assign)
			msm_audio_hyp_unassign(msm_audio_fd_data);
		if (handle)
			msm_audio_mem_free(handle, mem_data);
	}
	list_for_each_safe(ptr, next,
		&msm_audio_mem_fd_list.fd_list) {
		msm_audio_fd_data = list_entry(ptr, struct msm_audio_fd_data,
						list);
		list_del(&(msm_audio_fd_data->list));
		kfree(msm_audio_fd_data);
	}
	mutex_unlock(&(msm_audio_mem_fd_list.list_mutex));
}
EXPORT_SYMBOL_GPL(msm_audio_mem_crash_handler);

static int msm_audio_mem_open(struct inode *inode, struct file *file)
{
	struct msm_audio_mem_private *mem_data = container_of(inode->i_cdev,
						struct msm_audio_mem_private,
						cdev);
	struct device *dev = mem_data->chardev;

	get_device(dev);
	return 0;
}

static int msm_audio_mem_release(struct inode *inode, struct file *file)
{
	struct msm_audio_mem_private *mem_data = container_of(inode->i_cdev,
						struct msm_audio_mem_private,
						cdev);
	struct device *dev = mem_data->chardev;

	put_device(dev);
	return 0;
}

static long msm_audio_mem_ioctl(struct file *file, unsigned int ioctl_num,
				unsigned long __user ioctl_param)
{
	void *mem_handle;
	dma_addr_t paddr;
	size_t pa_len = 0;
	struct iosys_map *iosys_vmap = NULL;
	int ret = 0;
	struct msm_audio_fd_data *msm_audio_fd_data = NULL;
	struct msm_audio_mem_private *mem_data =
			container_of(file->f_inode->i_cdev, struct msm_audio_mem_private, cdev);
	u64 src_vmid_map_list = BIT(QCOM_SCM_VMID_HLOS);
	struct qcom_scm_vmperm dst_vmids_map[] = {{VMID_LPASS, PERM_READ | PERM_WRITE},
						{VMID_ADSP_HEAP, PERM_READ | PERM_WRITE}};
	u64 src_vmid_unmap_list = BIT(VMID_LPASS) | BIT(VMID_ADSP_HEAP);
	struct qcom_scm_vmperm dst_vmids_unmap[] = {{QCOM_SCM_VMID_HLOS,
					PERM_READ | PERM_WRITE | PERM_EXEC}};

	switch (ioctl_num) {
	case IOCTL_MAP_PHYS_ADDR:
		iosys_vmap = kzalloc(sizeof(struct msm_audio_fd_data), GFP_KERNEL);
		if (!iosys_vmap)
			return -ENOMEM;
		msm_audio_fd_data = kzalloc((sizeof(struct msm_audio_fd_data)),
					GFP_KERNEL);
		if (!msm_audio_fd_data) {
			kfree(iosys_vmap);
			return -ENOMEM;
		}
		ret = msm_audio_mem_import((struct dma_buf **)&mem_handle, (int)ioctl_param,
					0, &paddr, &pa_len, iosys_vmap, mem_data);
		if (ret < 0) {
			pr_err("%s Memory map Failed %d\n", __func__, ret);
			kfree(iosys_vmap);
			kfree(msm_audio_fd_data);
			return ret;
		}
		msm_audio_fd_data->fd = (int)ioctl_param;
		msm_audio_fd_data->handle = mem_handle;
		msm_audio_fd_data->paddr = paddr;
		msm_audio_fd_data->plen = pa_len;
		msm_audio_fd_data->dev = mem_data->cb_dev;
		msm_audio_update_fd_list(msm_audio_fd_data);
		break;
	case IOCTL_UNMAP_PHYS_ADDR:
		msm_audio_get_handle((int)ioctl_param, &mem_handle);
		ret = msm_audio_mem_free(mem_handle, mem_data);
		if (ret < 0) {
			pr_err("%s Ion free failed %d\n", __func__, ret);
			return ret;
		}
		msm_audio_delete_fd_entry(mem_handle);
		break;
	case IOCTL_MAP_HYP_ASSIGN:
		ret = msm_audio_get_phy_addr((int)ioctl_param, &paddr, &pa_len);
		if (ret < 0) {
			pr_err("%s get phys addr failed %d\n", __func__, ret);
			return ret;
		}
		ret = qcom_scm_assign_mem(paddr, pa_len, &src_vmid_map_list,
			dst_vmids_map, ARRAY_SIZE(dst_vmids_map));
		if (ret < 0) {
			pr_err("%s: qcom_assign failed result = %d addr = 0x%llx size = %lu\n",
					__func__, ret, paddr, pa_len);
			return ret;
		}
		pr_debug("%s: qcom scm assign success\n", __func__);
		msm_audio_set_hyp_assign((int)ioctl_param, true);
		break;
	case IOCTL_UNMAP_HYP_ASSIGN:
		ret = msm_audio_get_phy_addr((int)ioctl_param, &paddr, &pa_len);
		if (ret < 0) {
			pr_err("%s get phys addr failed %d\n", __func__, ret);
			return ret;
		}
		ret = qcom_scm_assign_mem(paddr, pa_len, &src_vmid_unmap_list,
			dst_vmids_unmap, ARRAY_SIZE(dst_vmids_unmap));
		if (ret < 0) {
			pr_err("%s: qcom scm unassign failed result = %d addr = 0x%llx size = %lu\n",
					__func__, ret, paddr, pa_len);
			return ret;
		}
		pr_debug("%s: qcom scm unassign success\n", __func__);
		msm_audio_set_hyp_assign((int)ioctl_param, false);
		break;
	default:
		pr_err("%s Entered default. Invalid ioctl num %u\n",
			__func__, ioctl_num);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const struct of_device_id msm_audio_mem_dt_match[] = {
	{ .compatible = "qcom,msm-audio-mem" },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_audio_mem_dt_match);

static const struct file_operations msm_audio_mem_fops = {
	.owner = THIS_MODULE,
	.open = msm_audio_mem_open,
	.release = msm_audio_mem_release,
	.unlocked_ioctl = msm_audio_mem_ioctl,
};

static int msm_audio_mem_reg_chrdev(struct msm_audio_mem_private *mem_data)
{
	int ret = 0;

	ret = alloc_chrdev_region(&mem_data->mem_major, 0,
				MINOR_NUMBER_COUNT, mem_data->driver_name);
	if (ret < 0) {
		pr_err("%s alloc_chr_dev_region failed ret : %d\n",
			__func__, ret);
		return ret;
	}
	pr_debug("%s major number %d\n", __func__, MAJOR(mem_data->mem_major));
	mem_data->mem_class = class_create(mem_data->driver_name);
	if (IS_ERR(mem_data->mem_class)) {
		ret = PTR_ERR(mem_data->mem_class);
		pr_err("%s class create failed. ret : %d\n", __func__, ret);
		goto err_class;
	}
	mem_data->chardev = device_create(mem_data->mem_class, NULL,
				mem_data->mem_major, NULL,
				mem_data->driver_name);
	if (IS_ERR(mem_data->chardev)) {
		ret = PTR_ERR(mem_data->chardev);
		pr_err("%s device create failed ret : %d\n", __func__, ret);
		goto err_device;
	}
	cdev_init(&mem_data->cdev, &msm_audio_mem_fops);
	ret = cdev_add(&mem_data->cdev, mem_data->mem_major, 1);
	if (ret) {
		pr_err("%s cdev add failed, ret : %d\n", __func__, ret);
		goto err_cdev;
	}
	return ret;

err_cdev:
	device_destroy(mem_data->mem_class, mem_data->mem_major);
err_device:
	class_destroy(mem_data->mem_class);
err_class:
	unregister_chrdev_region(0, MINOR_NUMBER_COUNT);
	return ret;
}

static int msm_audio_mem_unreg_chrdev(struct msm_audio_mem_private *mem_data)
{
	cdev_del(&mem_data->cdev);
	device_destroy(mem_data->mem_class, mem_data->mem_major);
	class_destroy(mem_data->mem_class);
	unregister_chrdev_region(0, MINOR_NUMBER_COUNT);
	return 0;
}
static int msm_audio_mem_probe(struct platform_device *pdev)
{
	int rc = 0;
	u64 smmu_sid = 0;
	u64 smmu_sid_mask = 0;
	const char *msm_audio_mem_dt = "qcom,smmu-enabled";
	const char *msm_audio_mem_smmu_sid_mask = "qcom,smmu-sid-mask";
	bool smmu_enabled;
	struct device *dev = &pdev->dev;
	struct of_phandle_args iommuspec;
	struct msm_audio_mem_private *msm_audio_mem_data = NULL;

	if (dev->of_node == NULL) {
		dev_err(dev,
			"%s: device tree is not found\n",
			__func__);
		return 0;
	}

	msm_audio_mem_data = devm_kzalloc(&pdev->dev, (sizeof(struct msm_audio_mem_private)),
			GFP_KERNEL);
	if (!msm_audio_mem_data)
		return -ENOMEM;

	smmu_enabled = of_property_read_bool(dev->of_node,
					     msm_audio_mem_dt);
	msm_audio_mem_data->smmu_enabled = smmu_enabled;

	dev_dbg(dev, "%s: SMMU is %s\n", __func__, (!smmu_enabled) ? "Disabled" : "Enabled");

	if (smmu_enabled) {
		msm_audio_mem_data->driver_name = "msm_audio_mem";
		/* Get SMMU SID information from Devicetree */
		rc = of_property_read_u64(dev->of_node,
					msm_audio_mem_smmu_sid_mask,
					&smmu_sid_mask);
		if (rc) {
			dev_err(dev,
				"%s: qcom,smmu-sid-mask missing in DT node, using default\n",
				__func__);
			smmu_sid_mask = 0xFFFFFFFFFFFFFFFF;
		}

		rc = of_parse_phandle_with_args(dev->of_node, "iommus",
						"#iommu-cells", 0, &iommuspec);
		if (rc)
			dev_err(dev, "%s: could not get smmu SID, ret = %d\n",
				__func__, rc);
		else
			smmu_sid = (iommuspec.args[0] & smmu_sid_mask);

		msm_audio_mem_data->smmu_sid_bits =
			smmu_sid << MSM_AUDIO_SMMU_SID_OFFSET;
	} else {
		msm_audio_mem_data->driver_name = "msm_audio_mem_cma";

		rc = of_reserved_mem_device_init(dev);
		if (rc) {
			pr_err("%s: No reserved DMA memory, ret=%d\n", __func__, rc);
			return -EINVAL;
		}

		rc = cma_heap_add(dev->cma_area, NULL);
		if (rc) {
			pr_err("%s: cma_heap_add failed, ret=%d\n", __func__, rc);
			return -EINVAL;
		}
	}

	if (!rc)
		msm_audio_mem_data->device_status |= MSM_AUDIO_MEM_PROBED;

	msm_audio_mem_data->cb_dev = dev;
	dev_set_drvdata(dev, msm_audio_mem_data);
	if (!msm_audio_mem_fd_list_init) {
		INIT_LIST_HEAD(&msm_audio_mem_fd_list.fd_list);
		mutex_init(&(msm_audio_mem_fd_list.list_mutex));
		msm_audio_mem_fd_list_init = true;
	}
	INIT_LIST_HEAD(&msm_audio_mem_data->alloc_list);
	mutex_init(&(msm_audio_mem_data->list_mutex));
	rc = msm_audio_mem_reg_chrdev(msm_audio_mem_data);
	if (rc) {
		pr_err("%s register char dev failed, rc : %d\n", __func__, rc);
		return rc;
	}
	return rc;
}

static int msm_audio_mem_remove(struct platform_device *pdev)
{
	struct msm_audio_mem_private *mem_data = dev_get_drvdata(&pdev->dev);

	mem_data->smmu_enabled = false;
	mem_data->device_status = 0;
	msm_audio_mem_unreg_chrdev(mem_data);
	return 0;
}

static struct platform_driver msm_audio_mem_driver = {
	.driver = {
		.name = "msm-audio-mem",
		.of_match_table = msm_audio_mem_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = msm_audio_mem_probe,
	.remove = msm_audio_mem_remove,
};

int __init msm_audio_mem_init(void)
{
	return platform_driver_register(&msm_audio_mem_driver);
}

void msm_audio_mem_exit(void)
{
	platform_driver_unregister(&msm_audio_mem_driver);
}

module_init(msm_audio_mem_init);
module_exit(msm_audio_mem_exit);
MODULE_DESCRIPTION("MSM Audio MEM module");
MODULE_LICENSE("GPL");
