# Scalpel

Scalpel is a Linux kernel module designed for stealth file hiding and direct process/device memory access. It provides a character device (`/dev/scalpel`) through which user‑space tools can hide files from directory listings and perform low‑level memory read/write operations.

> **WARNING**: This module manipulates kernel internals and can easily crash your system, corrupt data, or be abused for malicious purposes. For research and educational testing only. The author assumes no responsibility for any damage caused.

---

## Features

- **File hiding**: Write an absolute path to `/dev/scalpel` to hide the corresponding inode from non‑root users. The file disappears from directory listings and path lookups **only on memory‑backed filesystems (tmpfs, devtmpfs)**.
- **Virtual memory read/write**: Read or write another process's virtual memory using `pid` and virtual address.
- **Device physical memory read/write**: Read or write physical memory ranges that belong to reserved/device memory (e.g., MMIO).
- **Device memory detection**: Check whether a given virtual address maps to device memory (reserved or non‑RAM).
- **Module self‑hiding**: Renames the module's sysfs directory and the misc device's sysfs entry to random names at load time to make detection harder.

---

## Supported Kernels & Tested Devices

| Android Version | Kernel Version | Status |
|-----------------|----------------|--------|
| Android 12      | 5.10           | Tested (GKI) |
| Android 13      | 5.10           | Tested (GKI) |
| Android 13      | 5.15           | Tested (GKI) |
| Android 14      | 6.1            | Tested (GKI) |
| Android 15      | 6.6            | Tested (GKI) |
| Android 16      | 6.12           | Tested (GKI) |

The above kernel versions have been verified to build and load successfully. However, **file hiding functionality is only guaranteed on memory‑backed filesystems** such as `tmpfs` or `devtmpfs`. It is **not supported** on disk filesystems (ext4, f2fs, etc.) in this release.

---

## Build

Requirements:
- Linux kernel headers matching your running kernel
- GCC and make
- Kernel configuration with kprobes and kallsyms support (usually enabled by default)

```bash
cd scalpel
make
```

The module object file will be generated as `scalpel.ko`.

---

## Installation

Load the module with `insmod` (requires root):

```bash
sudo insmod scalpel.ko
```

Check kernel messages for the device node name (it may be randomly renamed during initialization):

```bash
dmesg | tail
```

If the device node is renamed, you will see something like:

```
scalpel: misc device sysfs dir renamed to xxxxxxxxx
```

**Important**: The module also hides the `/dev/scalpel` node itself from non‑root users (`uid != 0`) at load time. This means that after loading, the device node may become inaccessible to non‑root users.

To remove the module:

```bash
sudo rmmod scalpel
```

---

## Usage

### Hiding a File

1. Open the scalpel device node (the real path after loading, if accessible).
2. Write the absolute path of the file/directory you want to hide.
3. Close the device.

Example using `echo`:

```bash
echo -n "/home/user/secret.txt" > /dev/scalpel
```

The file becomes invisible to non‑root users **only if it resides on a memory‑backed filesystem**. To unhide, simply unload the module (all hidden files reappear).

**Note**: Hidden files should not be modified, renamed, or deleted while hidden; the internal VFS state is manipulated and such operations may fail or cause instability. This is especially true for the current implementation, which does not handle all VFS operations.

---

## IOCTL Interface

The device supports several `ioctl` commands for memory operations. All commands use the magic number `'M'`.

### Structures

Defined in `memrw/memrw.h`:

```c
struct devmem_read_arg {
    __kernel_pid_t pid;
    unsigned long vaddr;
    void __user *buf;
    __kernel_size_t size;
};

struct devmem_write_arg {
    __kernel_pid_t pid;
    unsigned long vaddr;
    const void __user *buf;
    __kernel_size_t size;
};

struct vmem_read_arg {
    __kernel_pid_t pid;
    unsigned long vaddr;
    void __user *buf;
    __kernel_size_t size;
};

struct vmem_write_arg {
    __kernel_pid_t pid;
    unsigned long vaddr;
    const void __user *buf;
    __kernel_size_t size;
};

struct is_devmem_arg {
    __kernel_pid_t pid;
    unsigned long vaddr;
    __u8 __user *result;
    __kernel_size_t size;
};
```

### Commands

| Command | Description |
|---------|-------------|
| `MEMRW_IOC_DEVMEM_READ` | Read from device physical memory. The `vaddr` is translated to a physical address using the target process's page tables. |
| `MEMRW_IOC_DEVMEM_WRITE` | Write to device physical memory. |
| `MEMRW_IOC_VMEM_READ` | Read from a process's virtual memory. |
| `MEMRW_IOC_VMEM_WRITE` | Write to a process's virtual memory. |
| `MEMRW_IOC_IS_DEVMEM` | Check if a virtual address maps to device memory. Writes `1` or `0` to `result`. |

Example usage from a C program:

```c
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include "memrw/memrw.h"

int fd = open("/dev/scalpel", O_RDWR);
struct vmem_read_arg arg;
arg.pid = 1234;
arg.vaddr = 0x7ffd00000000;
arg.buf = buffer;
arg.size = 4096;
ioctl(fd, MEMRW_IOC_VMEM_READ, &arg);
close(fd);
```

---

## How It Works (Brief)

- **File hiding**: The module maintains a list of hidden inode numbers. It registers kretprobes on `__d_lookup` and `__d_lookup_rcu` to intercept dentry lookups. If the dentry's inode is hidden and the caller is not root, the lookup returns NULL. Additionally, the dentry is physically unlinked from its parent's `d_subdirs` list to ensure it does not appear in directory enumeration **on memory‑backed filesystems**. This approach does **not** work for disk filesystems because directory enumeration reads directly from disk, not from the dcache.
- **Memory access**:
  - Virtual memory: `vmem_read`/`vmem_write` walk the target process's page tables using `follow_pte` (or `get_user_pages_remote` on kernel 6.12+) and copy data page by page.
  - Device memory: The virtual address is first translated to a physical address using the target process's page tables. If the physical address is considered device memory (via `pfn_valid` and `PageReserved`), `ioremap` is used to map it and perform I/O memory copy.

---

## Known Limitations / Warnings

- **Instability**: This module directly modifies kernel VFS internals and can lead to kernel panics, use‑after‑free, or deadlocks under certain conditions.
- **Hidden file restrictions**: Do not rename, unlink, or modify hidden files while the module is loaded. The VFS state is inconsistent and such operations can cause crashes or undefined behaviour. This is especially true for the current implementation, which does not handle all VFS operations.
- **Root user bypass**: The file hiding mechanism intentionally does **not** hide files from the root user (`uid 0`). This is by design in the current code.
- **Device node rename/hiding**: The module renames its own sysfs entries to random strings at load time. Additionally, it hides `/dev/scalpel` itself for `uid != 0`. This may make the device node inaccessible to non‑root users after loading.
- **Kernel symbol dependency**: The module relies on `kallsyms_lookup_name` (obtained via kprobe) and `follow_pte` being available. It may not work on kernels where these symbols are restricted or changed (e.g., 6.12+ uses a different path for page table walking).
- **No multi‑core safety**: Some hide/unhide operations may not be fully safe under concurrent access. Use with caution.

---

## License

This project is licensed under the GNU General Public License v2.0. See the `SPDX-License-Identifier` headers in the source files.

---

## Contact

- **Author**: Kaidevon
- **Email**: [kaidevonmail@gmail.com](mailto:kaidevonmail@gmail.com)

---

*Version: v0.0.1*
