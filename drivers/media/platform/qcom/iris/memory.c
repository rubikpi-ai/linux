// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "memory.h"
#include "iris_common.h"
#include "iris_instance.h"

void *iris_get_buffer_from_pool(struct iris_inst *inst)
{
	struct iris_mem_pool_header *hdr = NULL;
	struct iris_mem_pool *pool;

	pool = inst->mem_pool;

	if (!list_empty(&pool->free_hdr_list)) {
		hdr = list_first_entry(&pool->free_hdr_list, struct iris_mem_pool_header, list);
		list_move_tail(&hdr->list, &pool->busy_hdr_list);

		memset((char *)hdr->buf, 0, pool->size);
		/* Catch double-free request */
		hdr->busy = true;

		return hdr->buf;
	}

	hdr = kzalloc(pool->size + sizeof(*hdr), GFP_KERNEL);
	if (!hdr)
		return NULL;

	INIT_LIST_HEAD(&hdr->list);
	hdr->busy = true;
	hdr->buf = (void *)(hdr + 1);
	list_add_tail(&hdr->list, &pool->busy_hdr_list);

	return hdr->buf;
}

void iris_return_buffer_to_pool(struct iris_inst *inst, void *vidc_buf)
{
	struct iris_mem_pool_header *hdr;
	struct iris_mem_pool *pool;

	if (!vidc_buf)
		return;

	hdr = (struct iris_mem_pool_header *)vidc_buf - 1;

	if (hdr->buf != vidc_buf)
		return;

	pool = inst->mem_pool;

	/* Catch double-free request */
	if (!hdr->busy)
		return;
	hdr->busy = false;

	list_move_tail(&hdr->list, &pool->free_hdr_list);
}

int iris_mem_pool_init(struct iris_inst *inst)
{
	inst->mem_pool = devm_kzalloc(inst->core->dev, sizeof(struct iris_mem_pool),
				      GFP_KERNEL);

	if (!inst->mem_pool)
		return -ENOMEM;

	inst->mem_pool->size = sizeof(struct iris_buffer);
	INIT_LIST_HEAD(&inst->mem_pool->free_hdr_list);
	INIT_LIST_HEAD(&inst->mem_pool->busy_hdr_list);

	return 0;
}

void iris_mem_pool_deinit(struct iris_inst *inst)
{
	struct iris_mem_pool_header *hdr, *dummy;
	struct iris_mem_pool *pool;

	pool = inst->mem_pool;

	list_for_each_entry_safe(hdr, dummy, &pool->free_hdr_list, list) {
		list_del(&hdr->list);
		kfree(hdr);
	}

	list_for_each_entry_safe(hdr, dummy, &pool->busy_hdr_list, list) {
		list_del(&hdr->list);
		kfree(hdr);
	}
}
