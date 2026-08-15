/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/memrw/vmem_rw.c
 *
 * Copyright (C) 2026 Kaidevon
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/pid.h>
#include <linux/uaccess.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0)
/* Linux 5.5+ moved page table definitions to linux/pgtable.h */
#include <linux/pgtable.h>
#else
#include <asm/pgtable.h>
#endif
#include <linux/spinlock.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
/* Linux 5.8+ introduced mmap_lock.h for the mmap lock API */
#include <linux/mmap_lock.h>
#else
/* Older kernels use mm->mmap_sem directly */
#include <linux/mm.h>
#include <linux/rwsem.h>
#endif
#include <linux/stddef.h>

#include "memrw/vmem_rw.h"
#include "memrw/mtype.h"
#include "ksym_get.h"

/*
 * In Linux 6.12 and later, follow_pte is no longer exported,
 * so we cannot obtain it via ksym_get. The new branch uses
 * get_user_pages_remote, which handles mmap locking internally
 * and supports FOLL_NOFAULT to avoid triggering page faults,
 * mimicking the old follow_pte behavior that only walks existing
 * page tables.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
/* Only defined for kernels older than 6.12, where follow_pte is available */
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
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0) */

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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	/* New kernel branch: use get_user_pages_remote */
	struct page *page;
	long gup_ret;
#else
	/* Old kernel branch: use follow_pte via kallsyms */
	pte_t *pte;
	spinlock_t *ptl;
	int ret = 0;
#endif
	unsigned long pfn;
	unsigned long offset;

	if (!mm || !pa)
		return -EINVAL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	/*
	 * FOLL_NOFAULT prevents faulting in non-resident pages,
	 * matching the old follow_pte behavior of only walking
	 * existing page table entries.
	 */
	gup_ret = get_user_pages_remote(mm, vaddr, 1,
					FOLL_GET | FOLL_NOFAULT, &page, NULL);
	if (gup_ret != 1) {
		if (gup_ret >= 0)
			gup_ret = -EFAULT;
		return (int)gup_ret;
	}

	pfn = page_to_pfn(page);
	offset = vaddr & ~PAGE_MASK;
	*pa = PFN_PHYS(pfn) + offset;
	if (is_dev)
		*is_dev = is_devmem(*pa);
	put_page(page);
	return 0;
#else
	/* Old kernel branch */
	init_follow_pte_fn();
	if (!follow_pte_fn)
		return -ENOENT;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	/* mmap_read_lock is available from 5.8+ */
	mmap_read_lock(mm);
#else
	down_read(&mm->mmap_sem);
#endif
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	mmap_read_unlock(mm);
#else
	up_read(&mm->mmap_sem);
#endif
	return ret;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0) */
}
EXPORT_SYMBOL(vmem_va_to_pa);

int vmem_read(pid_t pid, unsigned long vaddr, void *buf, size_t size)
{
	struct mm_struct *mm;
	struct page *page;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	/* New kernel branch */
	long gup_ret;
#else
	/* Old kernel branch */
	pte_t *pte;
	spinlock_t *ptl;
	int ret;
#endif
	void *kaddr;
	size_t total = 0;
	unsigned long offset;
	size_t to_copy;
	unsigned long uvaddr;

	if (!buf || size == 0)
		return -EINVAL;
	if (size > INT_MAX)
		return -EINVAL;

	mm = get_mm_from_pid(pid);
	if (!mm)
		return -ESRCH;

	offset = vaddr & ~PAGE_MASK;
	while (total < size) {
		uvaddr = vaddr + total;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		/*
		 * FOLL_NOFAULT ensures that non-resident pages are not
		 * faulted in, keeping the same behavior as the old
		 * follow_pte path: only existing pages are read.
		 */
		gup_ret = get_user_pages_remote(mm, uvaddr, 1,
						FOLL_GET | FOLL_NOFAULT,
						&page, NULL);
		if (gup_ret != 1) {
			if (gup_ret >= 0)
				gup_ret = -EFAULT;
			mmput(mm);
			return total ? (int)total : (int)gup_ret;
		}
#else
		/* Old kernel branch */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
		mmap_read_lock(mm);
#else
		down_read(&mm->mmap_sem);
#endif
		ret = call_follow_pte(mm, uvaddr, &pte, &ptl);
		if (ret != 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
			mmap_read_unlock(mm);
#else
			up_read(&mm->mmap_sem);
#endif
			mmput(mm);
			return total ? (int)total : ret;
		}

		if (!pte_present(*pte)) {
			spin_unlock(ptl);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
			mmap_read_unlock(mm);
#else
			up_read(&mm->mmap_sem);
#endif
			mmput(mm);
			return total ? (int)total : -EFAULT;
		}

		page = pfn_to_page(pte_pfn(*pte));
		get_page(page);
		spin_unlock(ptl);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
		mmap_read_unlock(mm);
#else
		up_read(&mm->mmap_sem);
#endif
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0) */

		/*
		 * Use kmap_local_page on kernels >= 6.12 for a more
		 * efficient temporary mapping. Older branches keep kmap
		 * to preserve existing behavior.
		 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		kaddr = kmap_local_page(page);
#else
		kaddr = kmap(page);
#endif
		to_copy = min_t(size_t, PAGE_SIZE - offset, size - total);
		memcpy((char *)buf + total, kaddr + offset, to_copy);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		kunmap_local(kaddr);
#else
		kunmap(page);
#endif
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	/* New kernel branch */
	long gup_ret;
#else
	/* Old kernel branch */
	pte_t *pte;
	spinlock_t *ptl;
	int ret;
#endif
	void *kaddr;
	size_t total = 0;
	unsigned long offset;
	size_t to_copy;
	unsigned long uvaddr;

	if (!buf || size == 0)
		return -EINVAL;
	if (size > INT_MAX)
		return -EINVAL;

	mm = get_mm_from_pid(pid);
	if (!mm)
		return -ESRCH;

	offset = vaddr & ~PAGE_MASK;
	while (total < size) {
		uvaddr = vaddr + total;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		/*
		 * For writing we need FOLL_WRITE. Combined with
		 * FOLL_NOFAULT, we only write to already resident,
		 * writable pages, avoiding COW and matching the old
		 * follow_pte check for pte_write.
		 */
		gup_ret = get_user_pages_remote(mm, uvaddr, 1,
						FOLL_WRITE | FOLL_GET |
						FOLL_NOFAULT,
						&page, NULL);
		if (gup_ret != 1) {
			if (gup_ret >= 0)
				gup_ret = -EFAULT;
			mmput(mm);
			return (int)gup_ret;
		}
#else
		/* Old kernel branch */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
		mmap_read_lock(mm);
#else
		down_read(&mm->mmap_sem);
#endif
		ret = call_follow_pte(mm, uvaddr, &pte, &ptl);
		if (ret != 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
			mmap_read_unlock(mm);
#else
			up_read(&mm->mmap_sem);
#endif
			mmput(mm);
			return ret;
		}

		if (!pte_present(*pte) || !pte_write(*pte)) {
			spin_unlock(ptl);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
			mmap_read_unlock(mm);
#else
			up_read(&mm->mmap_sem);
#endif
			mmput(mm);
			return -EFAULT;
		}

		page = pfn_to_page(pte_pfn(*pte));
		get_page(page);
		spin_unlock(ptl);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
		mmap_read_unlock(mm);
#else
		up_read(&mm->mmap_sem);
#endif
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0) */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		kaddr = kmap_local_page(page);
#else
		kaddr = kmap(page);
#endif
		to_copy = min_t(size_t, PAGE_SIZE - offset, size - total);
		memcpy(kaddr + offset, (const char *)buf + total, to_copy);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		kunmap_local(kaddr);
#else
		kunmap(page);
#endif
		set_page_dirty_lock(page);
		put_page(page);

		total += to_copy;
		offset = 0;
	}

	mmput(mm);
	/* Historical behavior: successful write returns 0, not byte count */
	return 0;
}
EXPORT_SYMBOL(vmem_write);

int vmem_init(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	/*
	 * For 6.12+ there is no need to dynamically resolve follow_pte
	 * because get_user_pages_remote provides the required functionality.
	 */
	return 0;
#else
	init_follow_pte_fn();
	if (!follow_pte_fn)
		return -ENOENT;
	return 0;
#endif
}
EXPORT_SYMBOL(vmem_init);