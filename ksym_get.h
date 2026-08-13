/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/ksym_get.h
 *
 * Copyright (C) 2026 Kaidevon
 *
 */
#ifndef KSYM_GET_H
#define KSYM_GET_H

#include <linux/kprobes.h>

unsigned long ksym_get(const char *name);

#endif /* KSYM_GET_H */