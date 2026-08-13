/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/ksym_get.c
 *
 * Copyright (C) 2026 Kaidevon
 *
 */
#include "ksym_get.h"
#include <linux/string.h>

__attribute__((no_sanitize("cfi"))) unsigned long ksym_get(const char *name)
{
	unsigned long (*fn)(const char *) = NULL;
	struct kprobe kp;

	memset(&kp, 0, sizeof(kp));
	kp.symbol_name = "kallsyms_lookup_name";
	if (register_kprobe(&kp) < 0)
		return 0;
	fn = (void *)kp.addr;
	unregister_kprobe(&kp);
	if (!fn)
		return 0;
	return fn(name);
}