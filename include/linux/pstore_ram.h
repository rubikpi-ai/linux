/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2010 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright (C) 2011 Kees Cook <keescook@chromium.org>
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef __LINUX_PSTORE_RAM_H__
#define __LINUX_PSTORE_RAM_H__

#include <linux/pstore.h>

struct persistent_ram_ecc_info {
	int block_size;
	int ecc_size;
	int symsize;
	int poly;
	uint16_t *par;
};

/*
 * Ramoops platform data
 * @mem_size	memory size for ramoops
 * @mem_address	physical memory address to contain ramoops
 */

#define RAMOOPS_FLAG_FTRACE_PER_CPU	BIT(0)

struct ramoops_platform_data {
	unsigned long	mem_size;
	phys_addr_t	mem_address;
	unsigned int	mem_type;
	unsigned long	record_size;
	unsigned long	console_size;
	unsigned long	ftrace_size;
	unsigned long	pmsg_size;
	int		max_reason;
	u32		flags;
	struct persistent_ram_ecc_info ecc_info;
};

#if IS_ENABLED(CONFIG_PSTORE_RAM)
void *register_ramoops_info_notifier(int (*fn)(const char *name, int id,
				     void *vaddr, phys_addr_t paddr,
				     size_t size));
void unregister_ramoops_info_notifier(void *nb_cookie);
#else
static inline void *
register_ramoops_info_notifier(int (*fn)(const char *name, int id, void *vaddr,
			       phys_addr_t paddr, size_t size))
{
	return NULL;
}
static inline void unregister_ramoops_info_notifier(void *nb_cookie) { }
#endif	/* CONFIG_PSTORE_RAM */

#endif
