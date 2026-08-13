/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scalpel/hide/hide_inode.h
 *
 * Copyright (C) 2026 Kaidevon
 *
 */
#ifndef _HIDE_INODE_H
#define _HIDE_INODE_H

#include <linux/fs.h>

#define MAX_HIDDEN_INODES 128

struct hidden_inode {
	unsigned long ino;
	dev_t dev;
	int valid;
};

int hide_inode(struct inode *inode);

int init_hiding(const char *dev_path);
void exit_hiding(void);

#endif /* _HIDE_INODE_H */