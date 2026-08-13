/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/hide/hide_module.c
 *
 * Copyright (C) 2026 Kaidevon
 *
 */
#include "hide_module.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/random.h>
#include <linux/err.h>
#include "ksym_get.h"

typedef int (*kobject_rename_t)(struct kobject *kobj, const char *new_name);
typedef int (*device_rename_t)(struct device *dev, const char *new_name);

static kobject_rename_t kobj_rename_fn;
static device_rename_t dev_rename_fn;

static void init_rename_fns(void)
{
	if (!kobj_rename_fn)
		kobj_rename_fn = (kobject_rename_t)ksym_get("kobject_rename");
	if (!dev_rename_fn)
		dev_rename_fn = (device_rename_t)ksym_get("device_rename");
}

__attribute__((no_sanitize("cfi"))) static int
call_kobject_rename(struct kobject *kobj, const char *name)
{
	return kobj_rename_fn(kobj, name);
}

__attribute__((no_sanitize("cfi"))) static int
call_device_rename(struct device *dev, const char *name)
{
	return dev_rename_fn(dev, name);
}

static void generate_random_name(char *buf, size_t size)
{
	unsigned char rand;
	size_t i;

	for (i = 0; i < size - 1; i++) {
		get_random_bytes(&rand, 1);
		buf[i] = 'a' + (rand % 26);
	}
	buf[size - 1] = '\0';
}

int hide_module_sysfs_name(struct module *mod)
{
	char rand_name[32];
	int ret;

	if (!mod) {
		pr_err("hide_module: mod is NULL\n");
		return -EINVAL;
	}

	init_rename_fns();
	if (!kobj_rename_fn) {
		pr_err("hide_module: kobject_rename not found\n");
		return -EFAULT;
	}

	generate_random_name(rand_name, sizeof(rand_name));
	ret = call_kobject_rename(&mod->mkobj.kobj, rand_name);
	if (ret)
		pr_err("hide_module: kobject_rename failed %d\n", ret);
	else
		pr_info("hide_module: module sysfs dir renamed to %s\n",
			rand_name);
	return ret;
}

int hide_misc_device_sysfs(struct miscdevice *misc)
{
	char rand_name[32];
	int ret;

	if (!misc->this_device) {
		pr_err("hide_module: misc device not yet registered\n");
		return -ENODEV;
	}
	init_rename_fns();
	if (!dev_rename_fn) {
		pr_err("hide_module: device_rename not found\n");
		return -EFAULT;
	}
	generate_random_name(rand_name, sizeof(rand_name));
	ret = call_device_rename(misc->this_device, rand_name);
	if (ret)
		pr_err("hide_module: device_rename failed %d\n", ret);
	else
		pr_info("hide_module: misc device sysfs dir renamed to %s\n",
			rand_name);
	return ret;
}