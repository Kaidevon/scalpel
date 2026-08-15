/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * include/uapi/scalpel/memrw.h
 *
 * Copyright (C) 2026 Kaidevon
 *
 * Scalpel memory read/write UAPI header.
 */
#ifndef _UAPI_MEMRW_H
#define _UAPI_MEMRW_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define MEMRW_IOC_MAGIC 'M'

struct devmem_read_arg {
	__kernel_pid_t pid;
	unsigned long vaddr;
	void *buf;
	__kernel_size_t size;
};

struct devmem_write_arg {
	__kernel_pid_t pid;
	unsigned long vaddr;
	const void *buf;
	__kernel_size_t size;
};

struct vmem_read_arg {
	__kernel_pid_t pid;
	unsigned long vaddr;
	void *buf;
	__kernel_size_t size;
};

struct vmem_write_arg {
	__kernel_pid_t pid;
	unsigned long vaddr;
	const void *buf;
	__kernel_size_t size;
};

struct is_devmem_arg {
	__kernel_pid_t pid;
	unsigned long vaddr;
	__u8 *result;
	__kernel_size_t size;
};

#define MEMRW_IOC_DEVMEM_READ _IOWR(MEMRW_IOC_MAGIC, 1, struct devmem_read_arg)
#define MEMRW_IOC_DEVMEM_WRITE _IOW(MEMRW_IOC_MAGIC, 2, struct devmem_write_arg)
#define MEMRW_IOC_VMEM_READ _IOWR(MEMRW_IOC_MAGIC, 3, struct vmem_read_arg)
#define MEMRW_IOC_VMEM_WRITE _IOW(MEMRW_IOC_MAGIC, 4, struct vmem_write_arg)
#define MEMRW_IOC_IS_DEVMEM _IOWR(MEMRW_IOC_MAGIC, 5, struct is_devmem_arg)

#ifdef __KERNEL__
int memrw_ioctl(unsigned int cmd, unsigned long arg);
int memrw_init(void);
#endif

#endif /* _UAPI_MEMRW_H */