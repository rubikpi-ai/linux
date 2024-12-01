// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory allocator for buffers shared with the TrustZone.
 *
 * Copyright (C) 2023-2024 Linaro Ltd.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "qcom_tzmem: [%d][%s]: " fmt, __LINE__,  __func__

#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware/qcom/qcom_tzmem.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <dt-bindings/firmware/qcom,scm.h>
#include <linux/genalloc.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/radix-tree.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/device.h>

#include "qcom_tzmem.h"

struct qcom_tzmem_area {
	struct list_head list;
	void *vaddr;
	dma_addr_t paddr;
	size_t size;
	void *priv;
};

struct qcom_tzmem_pool {
	struct gen_pool *genpool;
	struct list_head areas;
	enum qcom_tzmem_policy policy;
	size_t increment;
	size_t max_size;
	spinlock_t lock;
};

struct qcom_tzmem_chunk {
	phys_addr_t paddr;
	size_t size;
	struct qcom_tzmem_pool *owner;
};
#define MAXSHMVMS 4
#define PERM_BITS 3
#define VM_BITS 16
#define VMID_NUM_HYP 0
#define VMID_NUM_HLOS 1
#define SELF_OWNER_BIT 1
#define SHM_NUM_VM_SHIFT 9
#define SHM_VM_MASK 0xFFFF
#define SHM_PERM_MASK 0x7

#define SHMBRIDGE_E_NOT_SUPPORTED 4	/* SHMbridge is not implemented */

#define AC_ERR_SHARED_MEMORY_SINGLE_SOURCE 15

/* ns_vmids */
#define UPDATE_NS_VMIDS(ns_vmids, id)	\
				(((uint64_t)(ns_vmids) << VM_BITS) \
				| ((uint64_t)(id) & SHM_VM_MASK))

/* ns_perms */
#define UPDATE_NS_PERMS(ns_perms, perm)	\
				(((uint64_t)(ns_perms) << PERM_BITS) \
				| ((uint64_t)(perm) & SHM_PERM_MASK))

/* pfn_and_ns_perm_flags = paddr | ns_perms */
#define UPDATE_PFN_AND_NS_PERM_FLAGS(paddr, ns_perms)	\
				((uint64_t)(paddr) | (ns_perms))


/* ipfn_and_s_perm_flags = ipaddr | tz_perm */
#define UPDATE_IPFN_AND_S_PERM_FLAGS(ipaddr, tz_perm)	\
				((uint64_t)(ipaddr) | (uint64_t)(tz_perm))

/* size_and_flags when dest_vm is not HYP */
#define UPDATE_SIZE_AND_FLAGS(size, destnum)	\
				((size) | (destnum) << SHM_NUM_VM_SHIFT)

struct bridge_list {
	struct list_head head;
	struct mutex lock;
};

struct bridge_list_entry {
	struct list_head list;
	phys_addr_t paddr;
	uint64_t handle;
	int32_t ref_count;
};

static struct device *qcom_tzmem_dev;
static RADIX_TREE(qcom_tzmem_chunks, GFP_ATOMIC);
static DEFINE_SPINLOCK(qcom_tzmem_chunks_lock);

#if IS_ENABLED(CONFIG_QCOM_TZMEM_MODE_GENERIC)

static int qcom_tzmem_init(void)
{
	return 0;
}

static int qcom_tzmem_init_area(struct qcom_tzmem_area *area)
{
	return 0;
}

static void qcom_tzmem_cleanup_area(struct qcom_tzmem_area *area)
{

}

static int32_t __qcom_tzmem_register(phys_addr_t paddr, size_t size, uint32_t *ns_vmid_list,
		uint32_t *ns_vm_perm_list, uint32_t ns_vmid_num, uint32_t tz_perm, uint64_t *handle)
{
	return 0;
}

static int32_t __qcom_tzmem_deregister(uint64_t handle)
{
	return 0;
}

#elif IS_ENABLED(CONFIG_QCOM_TZMEM_MODE_SHMBRIDGE)

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/of.h>

#define QCOM_SHM_BRIDGE_NUM_VM_SHIFT 9

static bool qcom_tzmem_using_shm_bridge;
static struct bridge_list bridge_list_head;
static bool support_hyp;

/* List of machines that are known to not support SHM bridge correctly. */
static const char *const qcom_tzmem_blacklist[] = {
	"qcom,sc8180x",
	NULL
};

static int qcom_tzmem_init(void)
{
	const char *const *platform;
	int ret;

	for (platform = qcom_tzmem_blacklist; *platform; platform++) {
		if (of_machine_is_compatible(*platform))
			goto notsupp;
	}

	ret = qcom_scm_shm_bridge_enable();
	if (ret == -EOPNOTSUPP)
		goto notsupp;

	if (!ret)
		qcom_tzmem_using_shm_bridge = true;

	mutex_init(&bridge_list_head.lock);
	INIT_LIST_HEAD(&bridge_list_head.head);

	return ret;

notsupp:
	dev_info(qcom_tzmem_dev, "SHM Bridge not supported\n");
	return 0;
}

static int qcom_tzmem_init_area(struct qcom_tzmem_area *area)
{
	u64 pfn_and_ns_perm, ipfn_and_s_perm, size_and_flags;
	int ret;

	if (!qcom_tzmem_using_shm_bridge)
		return 0;

	pfn_and_ns_perm = (u64)area->paddr | QCOM_SCM_PERM_RW;
	ipfn_and_s_perm = (u64)area->paddr | QCOM_SCM_PERM_RW;
	size_and_flags = area->size | (1 << QCOM_SHM_BRIDGE_NUM_VM_SHIFT);

	u64 *handle __free(kfree) = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	ret = qcom_scm_shm_bridge_create(qcom_tzmem_dev, pfn_and_ns_perm,
					 ipfn_and_s_perm, size_and_flags,
					 QCOM_SCM_VMID_HLOS, handle);
	if (ret)
		return ret;

	area->priv = no_free_ptr(handle);

	return 0;
}

static void qcom_tzmem_cleanup_area(struct qcom_tzmem_area *area)
{
	u64 *handle = area->priv;

	if (!qcom_tzmem_using_shm_bridge)
		return;

	qcom_scm_shm_bridge_delete(qcom_tzmem_dev, *handle);
	kfree(handle);
}

static int32_t qcom_tzmem_list_add_locked(phys_addr_t paddr,
						uint64_t handle)
{
	struct bridge_list_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->handle = handle;
	entry->paddr = paddr;
	entry->ref_count = 0;

	list_add_tail(&entry->list, &bridge_list_head.head);
	return 0;
}

static void qcom_tzmem_list_del_locked(uint64_t handle)
{
	struct bridge_list_entry *entry;

	list_for_each_entry(entry, &bridge_list_head.head, list) {
		if (entry->handle == handle) {
			list_del(&entry->list);
			kfree(entry);
			break;
		}
	}
}

/**
 * qcom_tzmem_list_dec_refcount_locked: Decrement the reference count of
 *					registered shmbridge and if refcount reached to zero delete
 *					the shmbridge (i.e send a scm call to tz and remove that
 *					from out local list too.
 *					API suppose to be called in a locked enviorment.
 *
 * Return 0 in case of success else error code in case of failure
 */
static int32_t qcom_tzmem_list_dec_refcount_locked(uint64_t handle)
{
	struct bridge_list_entry *entry;
	int32_t ret = -EINVAL;

	list_for_each_entry(entry, &bridge_list_head.head, list)
		if (entry->handle == handle) {

			if (entry->ref_count > 0) {
				/* decrement reference count */
				entry->ref_count--;
				pr_debug("bridge on %lld exists decrease refcount :%d\n",
						handle, entry->ref_count);

				if (entry->ref_count == 0) {
				/* All valid reference are freed, it's time to delete the bridge */
					//ret = qcom_scm_delete_shm_bridge(handle);
					ret = qcom_scm_shm_bridge_delete(qcom_tzmem_dev, handle);
					if (ret) {
						pr_err("Failed to del bridge %lld, ret = %d\n",
								handle, ret);
						/* restore reference count in case of failure */
						entry->ref_count++;
						goto exit;
					}
					qcom_tzmem_list_del_locked(handle);
				}
				ret = 0;
			} else
				pr_err("ref_count should not be negative handle %lld , refcount: %d\n",
					 handle, entry->ref_count);
			break;
		}
exit:
	if (ret == -EINVAL)
		pr_err("Not able to find bridge handle %lld in map\n", handle);

	return ret;
}

/**
 * qcom_tzmem_list_inc_refcount_locked: Increment the ref count in case if
 *					we try to register a pre-registered phyaddr with shmbridge
 *					and provide a valid handle to the caller API which was
 *					passed by caller as a pointer.
 *					API suppose to be called in a locked enviorment.
 *
 * Return 0 in case of success else error code in case of failure.
 */
static int32_t qcom_tzmem_list_inc_refcount_locked(phys_addr_t paddr, uint64_t *handle)
{
	struct bridge_list_entry *entry;
	int32_t ret = -EINVAL;

	list_for_each_entry(entry, &bridge_list_head.head, list)
		if (entry->paddr == paddr) {

			entry->ref_count++;
			pr_debug("%s: bridge on %llx exists increase refcount :%d\n",
				__func__, (uint64_t)paddr, entry->ref_count);

			/* update handle in case we found paddr already exist */
			*handle = entry->handle;
			ret = 0;
			break;
		}
	if (ret)
		pr_err("Not able to find bridge paddr %llx in map\n", (uint64_t)paddr);
	return ret;
}

static int32_t qcom_tzmem_query_locked(phys_addr_t paddr)
{
	struct bridge_list_entry *entry;

	list_for_each_entry(entry, &bridge_list_head.head, list)
		if (entry->paddr == paddr) {
			pr_debug("A bridge on %llx exists\n", (uint64_t)paddr);
			return -EEXIST;
		}
	return 0;
}

/* Register paddr & size as a bridge, return bridge handle */
static int32_t __qcom_tzmem_register(
		phys_addr_t paddr,
		size_t size,
		uint32_t *ns_vmid_list,
		uint32_t *ns_vm_perm_list,
		uint32_t ns_vmid_num,
		uint32_t tz_perm,
		uint64_t *handle)

{
	int32_t ret = 0;
	uint64_t pfn_and_ns_perm_flags = 0;
	uint64_t ipfn_and_s_perm_flags = 0;
	uint64_t size_and_flags = 0;
	uint64_t ns_perms = 0;
	uint64_t ns_vmids = 0;
	int i = 0;

	if (!qcom_tzmem_using_shm_bridge)
		return 0;

	if (!handle || !ns_vmid_list || !ns_vm_perm_list ||
				ns_vmid_num > MAXSHMVMS) {
		pr_err("invalid input parameters\n");
		return -EINVAL;
	}

	mutex_lock(&bridge_list_head.lock);
	ret = qcom_tzmem_query_locked(paddr);
	if (ret) {
		pr_debug("found 0%llu already exist with shmbridge\n", paddr);
		goto bridge_exist;
	}

	for (i = 0; i < ns_vmid_num; i++) {
		ns_perms = UPDATE_NS_PERMS(ns_perms, ns_vm_perm_list[i]);
		ns_vmids = UPDATE_NS_VMIDS(ns_vmids, ns_vmid_list[i]);
	}

	pfn_and_ns_perm_flags = UPDATE_PFN_AND_NS_PERM_FLAGS(paddr, ns_perms);
	ipfn_and_s_perm_flags = UPDATE_IPFN_AND_S_PERM_FLAGS(paddr, tz_perm);
	size_and_flags = UPDATE_SIZE_AND_FLAGS(size, ns_vmid_num);

	if (support_hyp) {
		size_and_flags |= SELF_OWNER_BIT << 1;
		size_and_flags |= QCOM_SCM_PERM_RW << 2;
	}

	pr_debug("%s: desc.args[0] %llx, args[1] %llx, args[2] %llx, args[3] %llx\n",
		__func__, pfn_and_ns_perm_flags, ipfn_and_s_perm_flags,
		size_and_flags, ns_vmids);

	ret = qcom_scm_shm_bridge_create(qcom_tzmem_dev, pfn_and_ns_perm_flags,
			ipfn_and_s_perm_flags, size_and_flags, ns_vmids,
			handle);

	if (ret) {
		pr_err("create shmbridge failed, ret = %d\n", ret);

		/*
		 * If bridge is already existing and we are not real owner also paddr
		 * not exist in our map we will add an entry in our map and go for
		 * deregister for this since QTEE also maintain ref_count. So for this
		 * we should deregister to decrease ref_count in QTEE.
		 */
		if (ret == AC_ERR_SHARED_MEMORY_SINGLE_SOURCE)
			pr_err("bridge %llu exist but not registered in our map\n", paddr);
		else {
			ret = -EINVAL;
			goto exit;
		}
	}

	ret = qcom_tzmem_list_add_locked(paddr, *handle);
bridge_exist:
	ret = qcom_tzmem_list_inc_refcount_locked(paddr, handle);
exit:
	mutex_unlock(&bridge_list_head.lock);
	return ret;
}

/* Deregister bridge */
static int32_t __qcom_tzmem_deregister(uint64_t handle)
{
	int32_t ret = 0;

	if (!qcom_tzmem_using_shm_bridge)
		return 0;

	mutex_lock(&bridge_list_head.lock);
	ret = qcom_tzmem_list_dec_refcount_locked(handle);
	mutex_unlock(&bridge_list_head.lock);

	return ret;
}

#endif /* CONFIG_QCOM_TZMEM_MODE_SHMBRIDGE */

int32_t qcom_tzmem_register(phys_addr_t paddr, size_t size, uint32_t *ns_vmid_list,
		uint32_t *ns_vm_perm_list, uint32_t ns_vmid_num, uint32_t tz_perm, uint64_t *handle)
{
	return __qcom_tzmem_register(paddr, size, ns_vmid_list, ns_vm_perm_list,
			ns_vmid_num, tz_perm, handle);
}
EXPORT_SYMBOL_GPL(qcom_tzmem_register);

int32_t qcom_tzmem_deregister(uint64_t handle)
{
	return __qcom_tzmem_deregister(handle);
}
EXPORT_SYMBOL_GPL(qcom_tzmem_deregister);

static int qcom_tzmem_pool_add_memory(struct qcom_tzmem_pool *pool,
				      size_t size, gfp_t gfp)
{
	int ret;

	struct qcom_tzmem_area *area __free(kfree) = kzalloc(sizeof(*area),
							     gfp);
	if (!area)
		return -ENOMEM;

	area->size = PAGE_ALIGN(size);

	area->vaddr = dma_alloc_coherent(qcom_tzmem_dev, area->size,
					 &area->paddr, gfp);
	if (!area->vaddr)
		return -ENOMEM;

	ret = qcom_tzmem_init_area(area);
	if (ret) {
		dma_free_coherent(qcom_tzmem_dev, area->size,
				  area->vaddr, area->paddr);
		return ret;
	}

	ret = gen_pool_add_virt(pool->genpool, (unsigned long)area->vaddr,
				(phys_addr_t)area->paddr, size, -1);
	if (ret) {
		dma_free_coherent(qcom_tzmem_dev, area->size,
				  area->vaddr, area->paddr);
		return ret;
	}

	scoped_guard(spinlock_irqsave, &pool->lock)
		list_add_tail(&area->list, &pool->areas);

	area = NULL;
	return 0;
}

/**
 * qcom_tzmem_pool_new() - Create a new TZ memory pool.
 * @config: Pool configuration.
 *
 * Create a new pool of memory suitable for sharing with the TrustZone.
 *
 * Must not be used in atomic context.
 *
 * Return: New memory pool address or ERR_PTR() on error.
 */
struct qcom_tzmem_pool *
qcom_tzmem_pool_new(const struct qcom_tzmem_pool_config *config)
{
	int ret = -ENOMEM;

	might_sleep();

	switch (config->policy) {
	case QCOM_TZMEM_POLICY_STATIC:
		if (!config->initial_size)
			return ERR_PTR(-EINVAL);
		break;
	case QCOM_TZMEM_POLICY_MULTIPLIER:
		if (!config->increment)
			return ERR_PTR(-EINVAL);
		break;
	case QCOM_TZMEM_POLICY_ON_DEMAND:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	struct qcom_tzmem_pool *pool __free(kfree) = kzalloc(sizeof(*pool),
							     GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->genpool = gen_pool_create(PAGE_SHIFT, -1);
	if (!pool->genpool)
		return ERR_PTR(-ENOMEM);

	gen_pool_set_algo(pool->genpool, gen_pool_best_fit, NULL);

	pool->policy = config->policy;
	pool->increment = config->increment;
	pool->max_size = config->max_size;
	INIT_LIST_HEAD(&pool->areas);
	spin_lock_init(&pool->lock);

	if (config->initial_size) {
		ret = qcom_tzmem_pool_add_memory(pool, config->initial_size,
						 GFP_KERNEL);
		if (ret) {
			gen_pool_destroy(pool->genpool);
			return ERR_PTR(ret);
		}
	}

	return_ptr(pool);
}
EXPORT_SYMBOL_GPL(qcom_tzmem_pool_new);

/**
 * qcom_tzmem_pool_free() - Destroy a TZ memory pool and free all resources.
 * @pool: Memory pool to free.
 *
 * Must not be called if any of the allocated chunks has not been freed.
 * Must not be used in atomic context.
 */
void qcom_tzmem_pool_free(struct qcom_tzmem_pool *pool)
{
	struct qcom_tzmem_area *area, *next;
	struct qcom_tzmem_chunk *chunk;
	struct radix_tree_iter iter;
	bool non_empty = false;
	void __rcu **slot;

	might_sleep();

	if (!pool)
		return;

	scoped_guard(spinlock_irqsave, &qcom_tzmem_chunks_lock) {
		radix_tree_for_each_slot(slot, &qcom_tzmem_chunks, &iter, 0) {
			chunk = radix_tree_deref_slot_protected(slot,
						&qcom_tzmem_chunks_lock);

			if (chunk->owner == pool)
				non_empty = true;
		}
	}

	WARN(non_empty, "Freeing TZ memory pool with memory still allocated");

	list_for_each_entry_safe(area, next, &pool->areas, list) {
		list_del(&area->list);
		qcom_tzmem_cleanup_area(area);
		dma_free_coherent(qcom_tzmem_dev, area->size,
				  area->vaddr, area->paddr);
		kfree(area);
	}

	gen_pool_destroy(pool->genpool);
	kfree(pool);
}
EXPORT_SYMBOL_GPL(qcom_tzmem_pool_free);

static void devm_qcom_tzmem_pool_free(void *data)
{
	struct qcom_tzmem_pool *pool = data;

	qcom_tzmem_pool_free(pool);
}

/**
 * devm_qcom_tzmem_pool_new() - Managed variant of qcom_tzmem_pool_new().
 * @dev: Device managing this resource.
 * @config: Pool configuration.
 *
 * Must not be used in atomic context.
 *
 * Return: Address of the managed pool or ERR_PTR() on failure.
 */
struct qcom_tzmem_pool *
devm_qcom_tzmem_pool_new(struct device *dev,
			 const struct qcom_tzmem_pool_config *config)
{
	struct qcom_tzmem_pool *pool;
	int ret;

	pool = qcom_tzmem_pool_new(config);
	if (IS_ERR(pool))
		return pool;

	ret = devm_add_action_or_reset(dev, devm_qcom_tzmem_pool_free, pool);
	if (ret)
		return ERR_PTR(ret);

	return pool;
}
EXPORT_SYMBOL_GPL(devm_qcom_tzmem_pool_new);

static bool qcom_tzmem_try_grow_pool(struct qcom_tzmem_pool *pool,
				     size_t requested, gfp_t gfp)
{
	size_t current_size = gen_pool_size(pool->genpool);

	if (pool->max_size && (current_size + requested) > pool->max_size)
		return false;

	switch (pool->policy) {
	case QCOM_TZMEM_POLICY_STATIC:
		return false;
	case QCOM_TZMEM_POLICY_MULTIPLIER:
		requested = current_size * pool->increment;
		break;
	case QCOM_TZMEM_POLICY_ON_DEMAND:
		break;
	}

	return !qcom_tzmem_pool_add_memory(pool, requested, gfp);
}

/**
 * qcom_tzmem_alloc() - Allocate a memory chunk suitable for sharing with TZ.
 * @pool: TZ memory pool from which to allocate memory.
 * @size: Number of bytes to allocate.
 * @gfp: GFP flags.
 *
 * Can be used in any context.
 *
 * Return:
 * Address of the allocated buffer or NULL if no more memory can be allocated.
 * The buffer must be released using qcom_tzmem_free().
 */
void *qcom_tzmem_alloc(struct qcom_tzmem_pool *pool, size_t size, gfp_t gfp)
{
	unsigned long vaddr;
	int ret;

	if (!size)
		return NULL;

	size = PAGE_ALIGN(size);

	struct qcom_tzmem_chunk *chunk __free(kfree) = kzalloc(sizeof(*chunk),
							       gfp);
	if (!chunk)
		return NULL;

again:
	vaddr = gen_pool_alloc(pool->genpool, size);
	if (!vaddr) {
		if (qcom_tzmem_try_grow_pool(pool, size, gfp))
			goto again;

		return NULL;
	}

	chunk->paddr = gen_pool_virt_to_phys(pool->genpool, vaddr);
	chunk->size = size;
	chunk->owner = pool;

	scoped_guard(spinlock_irqsave, &qcom_tzmem_chunks_lock) {
		ret = radix_tree_insert(&qcom_tzmem_chunks, vaddr, chunk);
		if (ret) {
			gen_pool_free(pool->genpool, vaddr, size);
			return NULL;
		}

		chunk = NULL;
	}

	return (void *)vaddr;
}
EXPORT_SYMBOL_GPL(qcom_tzmem_alloc);

/**
 * qcom_tzmem_free() - Release a buffer allocated from a TZ memory pool.
 * @vaddr: Virtual address of the buffer.
 *
 * Can be used in any context.
 */
void qcom_tzmem_free(void *vaddr)
{
	struct qcom_tzmem_chunk *chunk;

	scoped_guard(spinlock_irqsave, &qcom_tzmem_chunks_lock)
		chunk = radix_tree_delete_item(&qcom_tzmem_chunks,
					       (unsigned long)vaddr, NULL);

	if (!chunk) {
		WARN(1, "Virtual address %p not owned by TZ memory allocator",
		     vaddr);
		return;
	}

	scoped_guard(spinlock_irqsave, &chunk->owner->lock)
		gen_pool_free(chunk->owner->genpool, (unsigned long)vaddr,
			      chunk->size);
	kfree(chunk);
}
EXPORT_SYMBOL_GPL(qcom_tzmem_free);

/**
 * qcom_tzmem_to_phys() - Map the virtual address of a TZ buffer to physical.
 * @vaddr: Virtual address of the buffer allocated from a TZ memory pool.
 *
 * Can be used in any context. The address must have been returned by a call
 * to qcom_tzmem_alloc().
 *
 * Returns: Physical address of the buffer.
 */
phys_addr_t qcom_tzmem_to_phys(void *vaddr)
{
	struct qcom_tzmem_chunk *chunk;

	guard(spinlock_irqsave)(&qcom_tzmem_chunks_lock);

	chunk = radix_tree_lookup(&qcom_tzmem_chunks, (unsigned long)vaddr);
	if (!chunk)
		return 0;

	return chunk->paddr;
}
EXPORT_SYMBOL_GPL(qcom_tzmem_to_phys);

int qcom_tzmem_enable(struct device *dev)
{
	if (qcom_tzmem_dev)
		return -EBUSY;

	qcom_tzmem_dev = dev;

	return qcom_tzmem_init();
}
EXPORT_SYMBOL_GPL(qcom_tzmem_enable);

MODULE_DESCRIPTION("TrustZone memory allocator for Qualcomm firmware drivers");
MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_LICENSE("GPL");
