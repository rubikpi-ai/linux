/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_SMEM_H__
#define __QCOM_SMEM_H__

#define QCOM_SMEM_HOST_ANY -1

bool qcom_smem_is_available(void);
int qcom_smem_alloc(unsigned host, unsigned item, size_t size);
void *qcom_smem_get(unsigned host, unsigned item, size_t *size);

int qcom_smem_get_free_space(unsigned host);

phys_addr_t qcom_smem_virt_to_phys(void *p);

int qcom_smem_get_soc_id(u32 *id);

int qcom_smem_bust_hwspin_lock_by_host(unsigned int host);

void *qcom_minidump_platform_device(void);

#endif
