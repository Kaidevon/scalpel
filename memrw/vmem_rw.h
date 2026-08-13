/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/memrw/vmem_rw.h
 *
 * Copyright (C) 2026 Kaidevon
 */
#ifndef _VMEM_RW_H
#define _VMEM_RW_H

#include <linux/types.h>
#include <linux/mm_types.h>

int vmem_va_to_pa(struct mm_struct *mm, unsigned long vaddr, phys_addr_t *pa,
		  bool *is_dev);
int vmem_read(pid_t pid, unsigned long vaddr, void *buf, size_t size);
int vmem_write(pid_t pid, unsigned long vaddr, const void *buf, size_t size);
int vmem_init(void);

#endif /* _VMEM_RW_H */