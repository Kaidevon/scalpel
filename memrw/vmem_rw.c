/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/memrw/vmem_rw.c
 *
 * Copyright (C) 2026 Kaidevon
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/pid.h>
#include <linux/uaccess.h>
#include <linux/pgtable.h>
#include <linux/spinlock.h>
#include <linux/mmap_lock.h>
#include <linux/stddef.h>

#include "memrw/vmem_rw.h"
#include "memrw/mtype.h"
#include "ksym_get.h"

typedef int (*follow_pte_t)(struct mm_struct *mm, unsigned long address,
			    pte_t **ptepp, spinlock_t **ptlp);

static follow_pte_t follow_pte_fn;

static void init_follow_pte_fn(void)
{
	if (!follow_pte_fn)
		follow_pte_fn = (follow_pte_t)ksym_get("follow_pte");
}

__attribute__((no_sanitize("cfi"))) static int
call_follow_pte(struct mm_struct *mm, unsigned long address, pte_t **ptepp,
		spinlock_t **ptlp)
{
	return follow_pte_fn(mm, address, ptepp, ptlp);
}

static struct mm_struct *get_mm_from_pid(pid_t pid)
{
	struct pid *pid_struct;
	struct task_struct *task;
	struct mm_struct *mm;

	pid_struct = find_get_pid(pid);
	if (!pid_struct)
		return NULL;

	task = get_pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);
	if (!task)
		return NULL;

	mm = get_task_mm(task);
	put_task_struct(task);
	return mm;
}

int vmem_va_to_pa(struct mm_struct *mm, unsigned long vaddr, phys_addr_t *pa,
		  bool *is_dev)
{
	pte_t *pte;
	spinlock_t *ptl;
	int ret = 0;
	unsigned long pfn;
	unsigned long offset;

	if (!mm || !pa)
		return -EINVAL;

	init_follow_pte_fn();
	if (!follow_pte_fn)
		return -ENOENT;

	mmap_read_lock(mm);
	ret = call_follow_pte(mm, vaddr, &pte, &ptl);
	if (ret == 0) {
		if (pte_present(*pte)) {
			pfn = pte_pfn(*pte);
			offset = vaddr & ~PAGE_MASK;
			*pa = PFN_PHYS(pfn) + offset;
			if (is_dev)
				*is_dev = is_devmem(*pa);
			ret = 0;
		} else {
			ret = -EFAULT;
		}
		spin_unlock(ptl);
	}
	mmap_read_unlock(mm);
	return ret;
}
EXPORT_SYMBOL(vmem_va_to_pa);

int vmem_read(pid_t pid, unsigned long vaddr, void *buf, size_t size)
{
	struct mm_struct *mm;
	struct page *page;
	pte_t *pte;
	spinlock_t *ptl;
	void *kaddr;
	int ret;
	size_t total = 0;
	unsigned long offset;
	size_t to_copy;

	if (!buf || size == 0)
		return -EINVAL;
	if (size > INT_MAX)
		return -EINVAL;

	mm = get_mm_from_pid(pid);
	if (!mm)
		return -ESRCH;

	offset = vaddr & ~PAGE_MASK;
	while (total < size) {
		unsigned long uvaddr = vaddr + total;

		mmap_read_lock(mm);
		ret = call_follow_pte(mm, uvaddr, &pte, &ptl);
		if (ret != 0) {
			mmap_read_unlock(mm);
			mmput(mm);
			return total ? (int)total : ret;
		}

		if (!pte_present(*pte)) {
			spin_unlock(ptl);
			mmap_read_unlock(mm);
			mmput(mm);
			return total ? (int)total : -EFAULT;
		}

		page = pfn_to_page(pte_pfn(*pte));
		get_page(page);
		spin_unlock(ptl);
		mmap_read_unlock(mm);

		kaddr = kmap(page);
		to_copy = min_t(size_t, PAGE_SIZE - offset, size - total);
		memcpy((char *)buf + total, kaddr + offset, to_copy);
		kunmap(page);
		put_page(page);

		total += to_copy;
		offset = 0;
	}

	mmput(mm);
	return (int)total;
}
EXPORT_SYMBOL(vmem_read);

int vmem_write(pid_t pid, unsigned long vaddr, const void *buf, size_t size)
{
	struct mm_struct *mm;
	struct page *page;
	pte_t *pte;
	spinlock_t *ptl;
	void *kaddr;
	int ret;
	size_t total = 0;
	unsigned long offset;
	size_t to_copy;

	if (!buf || size == 0)
		return -EINVAL;
	if (size > INT_MAX)
		return -EINVAL;

	mm = get_mm_from_pid(pid);
	if (!mm)
		return -ESRCH;

	offset = vaddr & ~PAGE_MASK;
	while (total < size) {
		unsigned long uvaddr = vaddr + total;

		mmap_read_lock(mm);
		ret = call_follow_pte(mm, uvaddr, &pte, &ptl);
		if (ret != 0) {
			mmap_read_unlock(mm);
			mmput(mm);
			return ret;
		}

		if (!pte_present(*pte) || !pte_write(*pte)) {
			spin_unlock(ptl);
			mmap_read_unlock(mm);
			mmput(mm);
			return -EFAULT;
		}

		page = pfn_to_page(pte_pfn(*pte));
		get_page(page);
		spin_unlock(ptl);
		mmap_read_unlock(mm);

		kaddr = kmap(page);
		to_copy = min_t(size_t, PAGE_SIZE - offset, size - total);
		memcpy(kaddr + offset, (const char *)buf + total, to_copy);
		kunmap(page);
		set_page_dirty_lock(page);
		put_page(page);

		total += to_copy;
		offset = 0;
	}

	mmput(mm);
	return 0;
}
EXPORT_SYMBOL(vmem_write);

int vmem_init(void)
{
	init_follow_pte_fn();
	if (!follow_pte_fn)
		return -ENOENT;
	return 0;
}
EXPORT_SYMBOL(vmem_init);