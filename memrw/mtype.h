/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/memrw/mtype.h
 *
 * Copyright (C) 2026 Kaidevon
 *
 */
#ifndef _MTYPE_H
#define _MTYPE_H

#include <linux/mm.h>

static inline bool is_devmem(phys_addr_t paddr)
{
	unsigned long pfn = __phys_to_pfn(paddr);

	return !pfn_valid(pfn) || PageReserved(pfn_to_page(pfn));
}

#endif /* _MTYPE_H */