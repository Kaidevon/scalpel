/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/memrw/devmem_rw.h
 *
 * Copyright (C) 2026 Kaidevon
 *
 */
#ifndef _DEVMEM_RW_H
#define _DEVMEM_RW_H

#include <linux/io.h>
#include <linux/errno.h>
#include "mtype.h"

static inline int devmem_read(phys_addr_t paddr, void *buf, size_t size)
{
	void __iomem *ioaddr;

	if (!buf || size == 0)
		return -EINVAL;
	if (!is_devmem(paddr))
		return -EINVAL;

	ioaddr = ioremap(paddr, size);
	if (!ioaddr)
		return -ENOMEM;

	memcpy_fromio(buf, ioaddr, size);
	iounmap(ioaddr);
	return 0;
}

static inline int devmem_write(phys_addr_t paddr, const void *buf, size_t size)
{
	void __iomem *ioaddr;

	if (!buf || size == 0)
		return -EINVAL;
	if (!is_devmem(paddr))
		return -EINVAL;

	ioaddr = ioremap(paddr, size);
	if (!ioaddr)
		return -ENOMEM;

	memcpy_toio(ioaddr, buf, size);
	iounmap(ioaddr);
	return 0;
}

#endif