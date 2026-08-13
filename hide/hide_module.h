/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/hide/hide_module.h
 *
 * Copyright (C) 2026 Kaidevon
 *
 */
#ifndef _HIDE_MODULE_H
#define _HIDE_MODULE_H

#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/module.h>

int hide_module_sysfs_name(struct module *mod);
int hide_misc_device_sysfs(struct miscdevice *misc);

#endif /* _HIDE_MODULE_H */