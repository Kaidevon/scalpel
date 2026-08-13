/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/memrw/memrw.c
 *
 * Copyright (C) 2026 Kaidevon
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <linux/pid.h>
#include "memrw/memrw.h"
#include "memrw/mtype.h"
#include "memrw/devmem_rw.h"
#include "memrw/vmem_rw.h"
#include "ksym_get.h"

static int resolve_user_pa(pid_t pid, unsigned long vaddr, phys_addr_t *pa)
{
	struct pid *pid_struct;
	struct task_struct *task;
	struct mm_struct *mm;
	int ret;
	bool is_dev;

	pid_struct = find_get_pid(pid);
	if (!pid_struct)
		return -ESRCH;

	task = get_pid_task(pid_struct, PIDTYPE_PID);

	put_pid(pid_struct);

	if (!task)
		return -ESRCH;

	mm = get_task_mm(task);
	put_task_struct(task);

	if (!mm)
		return -EINVAL;

	ret = vmem_va_to_pa(mm, vaddr, pa, &is_dev);

	mmput(mm);

	return ret;
}

static int handle_devmem_read(struct devmem_read_arg *arg)
{
	phys_addr_t pa;
	void *kbuf;
	int ret;

	if (!arg->buf || arg->size == 0)
		return -EINVAL;

	ret = resolve_user_pa(arg->pid, arg->vaddr, &pa);
	if (ret)
		return ret;
	if (!is_devmem(pa))
		return -EINVAL;

	kbuf = kmalloc(arg->size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = devmem_read(pa, kbuf, arg->size);
	if (ret == 0 && copy_to_user(arg->buf, kbuf, arg->size))
		ret = -EFAULT;

	kfree(kbuf);
	return ret;
}

static int handle_devmem_write(struct devmem_write_arg *arg)
{
	phys_addr_t pa;
	void *kbuf;
	int ret;

	if (!arg->buf || arg->size == 0)
		return -EINVAL;

	ret = resolve_user_pa(arg->pid, arg->vaddr, &pa);
	if (ret)
		return ret;
	if (!is_devmem(pa))
		return -EINVAL;

	kbuf = kmalloc(arg->size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, arg->buf, arg->size)) {
		kfree(kbuf);
		return -EFAULT;
	}
	ret = devmem_write(pa, kbuf, arg->size);
	kfree(kbuf);
	return ret;
}

static int handle_vmem_read(struct vmem_read_arg *arg)
{
	void *kbuf;
	int ret;

	if (!arg->buf || arg->size == 0)
		return -EINVAL;

	kbuf = kmalloc(arg->size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = vmem_read(arg->pid, arg->vaddr, kbuf, arg->size);
	if (ret > 0 && copy_to_user(arg->buf, kbuf, ret))
		ret = -EFAULT;

	kfree(kbuf);
	return ret;
}

static int handle_vmem_write(struct vmem_write_arg *arg)
{
	void *kbuf;
	int ret;

	if (!arg->buf || arg->size == 0)
		return -EINVAL;

	kbuf = kmalloc(arg->size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, arg->buf, arg->size)) {
		kfree(kbuf);
		return -EFAULT;
	}
	ret = vmem_write(arg->pid, arg->vaddr, kbuf, arg->size);
	kfree(kbuf);
	return ret;
}

static int handle_is_devmem(struct is_devmem_arg *arg)
{
	phys_addr_t pa;
	__u8 result;
	int ret;

	if (!arg->result)
		return -EINVAL;

	ret = resolve_user_pa(arg->pid, arg->vaddr, &pa);
	if (ret)
		return ret;

	result = is_devmem(pa) ? 1 : 0;
	if (copy_to_user(arg->result, &result, sizeof(result)))
		return -EFAULT;

	return 0;
}

int memrw_ioctl(unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case MEMRW_IOC_DEVMEM_READ: {
		struct devmem_read_arg karg;

		if (copy_from_user(&karg, (void __user *)arg, sizeof(karg)))
			return -EFAULT;
		return handle_devmem_read(&karg);
	}
	case MEMRW_IOC_DEVMEM_WRITE: {
		struct devmem_write_arg karg;

		if (copy_from_user(&karg, (void __user *)arg, sizeof(karg)))
			return -EFAULT;
		return handle_devmem_write(&karg);
	}
	case MEMRW_IOC_VMEM_READ: {
		struct vmem_read_arg karg;

		if (copy_from_user(&karg, (void __user *)arg, sizeof(karg)))
			return -EFAULT;
		return handle_vmem_read(&karg);
	}
	case MEMRW_IOC_VMEM_WRITE: {
		struct vmem_write_arg karg;

		if (copy_from_user(&karg, (void __user *)arg, sizeof(karg)))
			return -EFAULT;
		return handle_vmem_write(&karg);
	}
	case MEMRW_IOC_IS_DEVMEM: {
		struct is_devmem_arg karg;

		if (copy_from_user(&karg, (void __user *)arg, sizeof(karg)))
			return -EFAULT;
		return handle_is_devmem(&karg);
	}
	default:
		return -ENOTTY;
	}
}
EXPORT_SYMBOL(memrw_ioctl);

int memrw_init(void)
{
	return vmem_init();
}
EXPORT_SYMBOL(memrw_init);