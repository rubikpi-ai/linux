// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/elf.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/pstore_ram.h>
#include <linux/soc/qcom/smem.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <soc/qcom/qcom_minidump.h>

#include "qcom_minidump_internal.h"

/**
 * struct minidump_ss_data - Minidump subsystem private data
 *
 * @md_ss_toc: Application Subsystem TOC pointer
 * @md_regions: Application Subsystem region base pointer
 */
struct minidump_ss_data {
	struct minidump_subsystem *md_ss_toc;
	struct minidump_region *md_regions;
};

/**
 * struct minidump_elfhdr - Minidump table elf header
 *
 * @ehdr: elf main header
 * @shdr: Section header
 * @phdr: Program header
 * @elf_offset: Section offset in elf
 * @strtable_idx: String table current index position
 */
struct minidump_elfhdr {
	struct elfhdr *ehdr;
	struct elf_shdr *shdr;
	struct elf_phdr *phdr;

	size_t elf_offset;
	size_t strtable_idx;
};

/**
 * struct minidump - Minidump driver data information
 *
 * @apss_data: APSS driver data
 * @elf: Minidump elf header
 * @work: Minidump work for any required execution in process context.
 * @dev: Minidump backend device
 * @md_lock: Lock to protect access to APSS minidump table
 * @nb_cookie: Save the cookie, will be used for unregistering the callback.
 */
struct minidump {
	struct minidump_ss_data *apss_data;
	struct minidump_elfhdr elf;
	struct work_struct work;
	struct device *dev;
	struct mutex md_lock;
	void *nb_cookie;
};

struct md_region_list {
	struct qcom_minidump_region md_region;
	struct list_head list;
};

/*
 * In some of the Old Qualcomm devices, boot firmware statically allocates 300
 * as total number of supported region (including all co-processors) in
 * minidump table out of which linux was using 201. In future, this limitation
 * from boot firmware might get removed by allocating the region dynamically.
 * So, keep it compatible with older devices, we can keep the current limit for
 * Linux to 201.
 */
#define MAX_NUM_ENTRIES	  201
#define MAX_STRTBL_SIZE	  (MAX_NUM_ENTRIES * MAX_REGION_NAME_LENGTH)

static LIST_HEAD(apss_md_rlist);
static struct elf_shdr *elf_shdr_entry_addr(struct elfhdr *ehdr, int idx)
{
	struct elf_shdr *eshdr = (struct elf_shdr *)((size_t)ehdr + ehdr->e_shoff);

	return &eshdr[idx];
}

static struct elf_phdr *elf_phdr_entry_addr(struct elfhdr *ehdr, int idx)
{
	struct elf_phdr *ephdr = (struct elf_phdr *)((size_t)ehdr + ehdr->e_phoff);

	return &ephdr[idx];
}

static char *elf_str_table_start(struct elfhdr *ehdr)
{
	struct elf_shdr *eshdr;

	if (ehdr->e_shstrndx == SHN_UNDEF)
		return NULL;

	eshdr = elf_shdr_entry_addr(ehdr, ehdr->e_shstrndx);

	return (char *)ehdr + eshdr->sh_offset;
}

static char *elf_lookup_string(struct minidump *md, struct elfhdr *ehdr, int offset)
{
	char *strtab = elf_str_table_start(ehdr);

	if (!strtab || (md->elf.strtable_idx < offset))
		return NULL;

	return strtab + offset;
}

static unsigned int append_str_to_strtable(struct minidump *md, const char *name)
{
	char *strtab = elf_str_table_start(md->elf.ehdr);
	unsigned int old_idx = md->elf.strtable_idx;
	unsigned int ret;

	if (!strtab || !name)
		return 0;

	ret = old_idx;
	old_idx += strscpy((strtab + old_idx), name, MAX_REGION_NAME_LENGTH);
	md->elf.strtable_idx = old_idx + 1;

	return ret;
}

static int qcom_md_clear_elfheader(struct minidump *md,
				   const struct qcom_minidump_region *region)
{
	struct elfhdr *ehdr = md->elf.ehdr;
	struct elf_shdr *shdr;
	struct elf_shdr *tmp_shdr;
	struct elf_phdr *phdr;
	struct elf_phdr *tmp_phdr;
	unsigned int phidx;
	unsigned int shidx;
	unsigned int len;
	unsigned int i;
	char *shname;

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = elf_phdr_entry_addr(ehdr, i);
		if (phdr->p_paddr == region->phys_addr &&
		    phdr->p_memsz == region->size)
			break;
	}

	if (i == ehdr->e_phnum) {
		dev_err(md->dev, "Cannot find program header entry in elf\n");
		return -EINVAL;
	}

	phidx = i;
	for (i = 0; i < ehdr->e_shnum; i++) {
		shdr = elf_shdr_entry_addr(ehdr, i);
		shname = elf_lookup_string(md, ehdr, shdr->sh_name);
		if (shname && !strcmp(shname, region->name) &&
		    shdr->sh_addr == (elf_addr_t)region->virt_addr &&
		    shdr->sh_size == region->size)
			break;
	}

	if (i == ehdr->e_shnum) {
		dev_err(md->dev, "Cannot find section header entry in elf\n");
		return -EINVAL;
	}

	shidx = i;
	if (shdr->sh_offset != phdr->p_offset) {
		dev_err(md->dev, "Invalid entry details for region: %s\n", region->name);
		return -EINVAL;
	}

	/* Clear name in string table */
	len = strlen(shname) + 1;
	memmove(shname, shname + len, md->elf.strtable_idx - shdr->sh_name - len);
	md->elf.strtable_idx -= len;

	/* Clear program header */
	tmp_phdr = elf_phdr_entry_addr(ehdr, phidx);
	for (i = phidx; i < ehdr->e_phnum - 1; i++) {
		tmp_phdr = elf_phdr_entry_addr(ehdr, i + 1);
		phdr = elf_phdr_entry_addr(ehdr, i);
		memcpy(phdr, tmp_phdr, sizeof(*phdr));
		phdr->p_offset = phdr->p_offset - region->size;
	}
	memset(tmp_phdr, 0, sizeof(*tmp_phdr));
	ehdr->e_phnum--;

	/* Clear section header */
	tmp_shdr = elf_shdr_entry_addr(ehdr, shidx);
	for (i = shidx; i < ehdr->e_shnum - 1; i++) {
		tmp_shdr = elf_shdr_entry_addr(ehdr, i + 1);
		shdr = elf_shdr_entry_addr(ehdr, i);
		memcpy(shdr, tmp_shdr, sizeof(*shdr));
		shdr->sh_offset -= region->size;
		shdr->sh_name -= len;
	}

	memset(tmp_shdr, 0, sizeof(*tmp_shdr));
	ehdr->e_shnum--;
	md->elf.elf_offset -= region->size;

	return 0;
}

static void qcom_md_update_elfheader(struct minidump *md,
				     const struct qcom_minidump_region *region)
{
	struct elfhdr *ehdr = md->elf.ehdr;
	struct elf_shdr *shdr;
	struct elf_phdr *phdr;

	shdr = elf_shdr_entry_addr(ehdr, ehdr->e_shnum++);
	phdr = elf_phdr_entry_addr(ehdr, ehdr->e_phnum++);

	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_name = append_str_to_strtable(md, region->name);
	shdr->sh_addr = (elf_addr_t)region->virt_addr;
	shdr->sh_size = region->size;
	shdr->sh_flags = SHF_WRITE;
	shdr->sh_offset = md->elf.elf_offset;
	shdr->sh_entsize = 0;

	phdr->p_type = PT_LOAD;
	phdr->p_offset = md->elf.elf_offset;
	phdr->p_vaddr = (elf_addr_t)region->virt_addr;
	phdr->p_paddr = region->phys_addr;
	phdr->p_filesz = phdr->p_memsz = region->size;
	phdr->p_flags = PF_R | PF_W;
	md->elf.elf_offset += shdr->sh_size;
}

static void qcom_md_add_region(struct minidump_ss_data *mdss_data,
			       const struct qcom_minidump_region *region)
{
	struct minidump_subsystem *mdss_toc = mdss_data->md_ss_toc;
	struct minidump_region *mdr;
	unsigned int region_cnt;

	region_cnt = le32_to_cpu(mdss_toc->region_count);
	mdr = &mdss_data->md_regions[region_cnt];
	strscpy(mdr->name, region->name, sizeof(mdr->name));
	mdr->address = cpu_to_le64(region->phys_addr);
	mdr->size = cpu_to_le64(region->size);
	mdr->valid = cpu_to_le32(MINIDUMP_REGION_VALID);
	region_cnt++;
	mdss_toc->region_count = cpu_to_le32(region_cnt);
}

static int qcom_md_get_region_index(struct minidump_ss_data *mdss_data,
				    const struct qcom_minidump_region *region)
{
	struct minidump_subsystem *mdss_toc = mdss_data->md_ss_toc;
	struct minidump_region *mdr;
	unsigned int i;
	unsigned int count;

	count = le32_to_cpu(mdss_toc->region_count);
	for (i = 0; i < count; i++) {
		mdr = &mdss_data->md_regions[i];
		if (!strcmp(mdr->name, region->name))
			return i;
	}

	return -ENOENT;
}

static int qcom_md_region_unregister(struct minidump *md,
				     const struct qcom_minidump_region *region)
{
	struct minidump_ss_data *mdss_data = md->apss_data;
	struct minidump_subsystem *mdss_toc = mdss_data->md_ss_toc;
	struct minidump_region *mdr;
	unsigned int region_cnt;
	unsigned int idx;
	int ret;

	ret = qcom_md_get_region_index(mdss_data, region);
	if (ret < 0) {
		dev_err(md->dev, "%s region is not present\n", region->name);
		return ret;
	}

	idx = ret;
	mdr = &mdss_data->md_regions[0];
	region_cnt = le32_to_cpu(mdss_toc->region_count);
	/*
	 * Left shift all the regions exist after this removed region
	 * index by 1 to fill the gap and zero out the last region
	 * present at the end.
	 */
	memmove(&mdr[idx], &mdr[idx + 1], (region_cnt - idx - 1) * sizeof(*mdr));
	memset(&mdr[region_cnt - 1], 0, sizeof(*mdr));
	region_cnt--;
	mdss_toc->region_count = cpu_to_le32(region_cnt);

	return 0;
}

static int qcom_md_region_register(struct minidump *md,
				   const struct qcom_minidump_region *region)
{
	struct minidump_ss_data *mdss_data = md->apss_data;
	struct minidump_subsystem *mdss_toc = mdss_data->md_ss_toc;
	unsigned int num_region;
	int ret;

	ret = qcom_md_get_region_index(mdss_data, region);
	if (ret >= 0) {
		dev_info(md->dev, "%s region is already registered\n", region->name);
		return -EEXIST;
	}

	/* Check if there is a room for a new entry */
	num_region = le32_to_cpu(mdss_toc->region_count);
	if (num_region >= MAX_NUM_ENTRIES) {
		dev_err(md->dev, "maximum region limit %u reached\n", num_region);
		return -ENOSPC;
	}

	qcom_md_add_region(mdss_data, region);

	return 0;
}

static bool qcom_minidump_valid_region(const struct qcom_minidump_region *region)
{
	return region &&
		strnlen(region->name, MAX_NAME_LENGTH) < MAX_NAME_LENGTH &&
		region->virt_addr &&
		region->size &&
		IS_ALIGNED(region->size, 4);
}

/**
 * qcom_minidump_region_register() - Register region in APSS Minidump table.
 * @region: minidump region.
 *
 * Return: On success, it returns 0 and negative error value on failure.
 */
static int qcom_minidump_region_register(const struct qcom_minidump_region *region)
{
	struct platform_device *pdev;
	struct minidump *md;
	int ret;

	pdev = qcom_minidump_platform_device();
	if (!pdev)
		return -EINVAL;

	if (!qcom_minidump_valid_region(region))
		return -EINVAL;

	md = platform_get_drvdata(pdev);
	mutex_lock(&md->md_lock);
	ret = qcom_md_region_register(md, region);
	if (ret)
		goto unlock;

	qcom_md_update_elfheader(md, region);
unlock:
	mutex_unlock(&md->md_lock);
	return ret;
}

/**
 * qcom_minidump_region_unregister() - Unregister region from APSS Minidump table.
 * @region: minidump region.
 *
 * Return: On success, it returns 0 and negative error value on failure.
 */
static int qcom_minidump_region_unregister(const struct qcom_minidump_region *region)
{
	struct platform_device *pdev;
	struct minidump *md;
	int ret;

	pdev = qcom_minidump_platform_device();
	if (!pdev)
		return -EINVAL;

	if (!qcom_minidump_valid_region(region))
		return -EINVAL;

	md = platform_get_drvdata(pdev);
	mutex_lock(&md->md_lock);
	ret = qcom_md_region_unregister(md, region);
	if (ret)
		goto unlock;

	ret = qcom_md_clear_elfheader(md, region);
unlock:
	mutex_unlock(&md->md_lock);
	return ret;
}

static int qcom_md_add_elfheader(struct minidump *md)
{
	struct qcom_minidump_region elfregion;
	struct elfhdr *ehdr;
	struct elf_shdr *shdr;
	struct elf_phdr *phdr;
	unsigned int  elfh_size;
	unsigned int strtbl_off;
	unsigned int phdr_off;

	/*
	 * Header buffer contains:
	 * ELF header, (MAX_NUM_ENTRIES + 2) of Section and Program ELF headers,
	 * where, 2 additional entries, one for empty header, one for string table.
	 */
	elfh_size = sizeof(*ehdr);
	elfh_size += MAX_STRTBL_SIZE;
	elfh_size += ((sizeof(*shdr) + sizeof(*phdr)) * (MAX_NUM_ENTRIES + 4));
	elfh_size = ALIGN(elfh_size, 4);

	md->elf.ehdr = devm_kzalloc(md->dev, elfh_size, GFP_KERNEL);
	if (!md->elf.ehdr)
		return -ENOMEM;

	ehdr = md->elf.ehdr;
	/* Assign Section/Program headers offset */
	md->elf.shdr = shdr = (struct elf_shdr *)(ehdr + 1);
	md->elf.phdr = phdr = (struct elf_phdr *)(shdr + MAX_NUM_ENTRIES);
	phdr_off = sizeof(*ehdr) + (sizeof(*shdr) * MAX_NUM_ENTRIES);

	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELF_CLASS;
	ehdr->e_ident[EI_DATA] = ELF_DATA;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELF_OSABI;
	ehdr->e_type = ET_CORE;
	ehdr->e_machine  = ELF_ARCH;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_ehsize = sizeof(*ehdr);
	ehdr->e_phoff = phdr_off;
	ehdr->e_phentsize = sizeof(*phdr);
	ehdr->e_shoff = sizeof(*ehdr);
	ehdr->e_shentsize = sizeof(*shdr);
	ehdr->e_shstrndx = 1;

	md->elf.elf_offset = elfh_size;
	/*
	 * The zeroth index of the section header is reserved and is rarely used.
	 * Set the section header as null (SHN_UNDEF) and move to the next one.
	 * 2nd Section is String table.
	 */
	md->elf.strtable_idx = 1;
	strtbl_off = sizeof(*ehdr) + ((sizeof(*phdr) + sizeof(*shdr)) * MAX_NUM_ENTRIES);
	shdr++;
	ehdr->e_shnum++;
	shdr->sh_type = SHT_STRTAB;
	shdr->sh_offset = (elf_addr_t)strtbl_off;
	shdr->sh_size = MAX_STRTBL_SIZE;
	shdr->sh_entsize = 0;
	shdr->sh_flags = 0;
	shdr->sh_name = append_str_to_strtable(md, "STR_TBL");
	shdr++;
	ehdr->e_shnum++;

	/* Register ELF header as first region */
	strscpy(elfregion.name, "KELF_HEADER", sizeof(elfregion.name));
	elfregion.virt_addr = md->elf.ehdr;
	elfregion.phys_addr = virt_to_phys(md->elf.ehdr);
	elfregion.size = elfh_size;

	return qcom_md_region_register(md, &elfregion);
}

static int qcom_apss_md_table_init(struct minidump *md,
				   struct minidump_subsystem *mdss_toc)
{
	struct minidump_ss_data *mdss_data;

	mdss_data = devm_kzalloc(md->dev, sizeof(*mdss_data), GFP_KERNEL);
	if (!mdss_data)
		return -ENOMEM;

	mdss_data->md_ss_toc = mdss_toc;
	mdss_data->md_regions = devm_kcalloc(md->dev, MAX_NUM_ENTRIES,
					     sizeof(*mdss_data->md_regions),
					     GFP_KERNEL);
	if (!mdss_data->md_regions)
		return -ENOMEM;

	mdss_toc = mdss_data->md_ss_toc;
	mdss_toc->regions_baseptr = cpu_to_le64(virt_to_phys(mdss_data->md_regions));
	mdss_toc->enabled = cpu_to_le32(MINIDUMP_SS_ENABLED);
	mdss_toc->status = cpu_to_le32(1);
	mdss_toc->region_count = cpu_to_le32(0);

	/* Tell bootloader not to encrypt the regions of this subsystem */
	mdss_toc->encryption_status = cpu_to_le32(MINIDUMP_SS_ENCR_DONE);
	mdss_toc->encryption_required = cpu_to_le32(MINIDUMP_SS_ENCR_NOTREQ);

	md->apss_data = mdss_data;

	return 0;
}

static int register_ramoops_region(const char *name, int id, void *vaddr,
				   phys_addr_t paddr, size_t size)
{
	struct qcom_minidump_region *md_region;
	struct md_region_list *mdr_list;
	int ret;

	mdr_list = kzalloc(sizeof(*mdr_list), GFP_KERNEL);
	if (!mdr_list)
		return -ENOMEM;

	md_region = &mdr_list->md_region;
	scnprintf(md_region->name, sizeof(md_region->name), "K%s%d", name, id);
	md_region->virt_addr = vaddr;
	md_region->phys_addr = paddr;
	md_region->size = size;
	ret = qcom_minidump_region_register(md_region);
	if (ret < 0) {
		pr_err("failed to register region in minidump: err: %d\n", ret);
		return ret;
	}

	list_add(&mdr_list->list, &apss_md_rlist);

	return 0;
}

static void register_ramoops_minidump_cb(struct work_struct *work)
{
	struct minidump *md = container_of(work, struct minidump, work);

	md->nb_cookie = register_ramoops_info_notifier(register_ramoops_region);
	if (IS_ERR(md->nb_cookie)) {
		dev_err(md->dev, "Fail to register ramoops info notifier\n");
		md->nb_cookie = NULL;
	}
}

static void qcom_ramoops_minidump_unregister(void)
{
	struct md_region_list *mdr_list;
	struct md_region_list *tmp;

	list_for_each_entry_safe(mdr_list, tmp, &apss_md_rlist, list) {
		struct qcom_minidump_region *region;

		region = &mdr_list->md_region;
		qcom_minidump_region_unregister(region);
		list_del(&mdr_list->list);
	}
}

static void qcom_apss_md_table_exit(struct minidump_ss_data *mdss_data)
{
	memset(mdss_data->md_ss_toc, cpu_to_le32(0), sizeof(*mdss_data->md_ss_toc));
}

static int qcom_apss_minidump_probe(struct platform_device *pdev)
{
	struct minidump_global_toc *mdgtoc;
	struct minidump *md;
	size_t size;
	int ret;

	md = devm_kzalloc(&pdev->dev, sizeof(*md), GFP_KERNEL);
	if (!md)
		return -ENOMEM;

	md->dev = &pdev->dev;
	mdgtoc = qcom_smem_get(QCOM_SMEM_HOST_ANY, SBL_MINIDUMP_SMEM_ID, &size);
	if (IS_ERR(mdgtoc)) {
		ret = PTR_ERR(mdgtoc);
		return dev_err_probe(md->dev, ret,
				     "Couldn't find minidump smem item\n");
	}

	if (size < sizeof(*mdgtoc) || !mdgtoc->status) {
		ret = -EINVAL;
		return dev_err_probe(md->dev, ret,
				     "minidump table is not initialized\n");
	}

	mutex_init(&md->md_lock);
	ret = qcom_apss_md_table_init(md, &mdgtoc->subsystems[MINIDUMP_APSS_DESC]);
	if (ret)
		return dev_err_probe(md->dev, ret,
				     "apss minidump initialization failed\n");

	/* First entry would be ELF header */
	ret = qcom_md_add_elfheader(md);
	if (ret) {
		qcom_apss_md_table_exit(md->apss_data);
		return dev_err_probe(md->dev, ret, "Failed to add elf header\n");
	}

	platform_set_drvdata(pdev, md);

	/*
	 * Use separate context for registering ramoops region via workqueue
	 * as minidump probe can get called in same context of platform device
	 * register call from smem driver and further call to qcom_minidump_platform_device()
	 * can return -EPROBE_DEFER as __smem->minidump is not yet initialized because
	 * of same context and it can only initialized after return from probe.
	 *
	 * qcom_apss_minidump_probe()
	 *   register_ramoops_minidump_cb()
	 *     register_ramoops_region()
	 *       qcom_minidump_region_register()
	 *         qcom_minidump_platform_device()
	 */
	INIT_WORK(&md->work, register_ramoops_minidump_cb);
	schedule_work(&md->work);

	return ret;
}

static void qcom_apss_minidump_remove(struct platform_device *pdev)
{
	struct minidump *md = platform_get_drvdata(pdev);

	flush_work(&md->work);
	qcom_ramoops_minidump_unregister();
	if (md->nb_cookie)
		unregister_ramoops_info_notifier(md->nb_cookie);

	qcom_apss_md_table_exit(md->apss_data);
}

static const struct platform_device_id qcom_minidump_id_table[] = {
	{ .name = "qcom_minidump_smem" },
	{}
};
MODULE_DEVICE_TABLE(platform, qcom_minidump_id_table);

static struct platform_driver qcom_minidump_driver = {
	.probe = qcom_apss_minidump_probe,
	.remove_new = qcom_apss_minidump_remove,
	.driver  = {
		.name = "qcom_minidump_smem",
	},
	.id_table = qcom_minidump_id_table,
};

module_platform_driver(qcom_minidump_driver);

MODULE_DESCRIPTION("Qualcomm APSS minidump driver");
MODULE_LICENSE("GPL");
