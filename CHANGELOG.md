# Changelog

## [v0.0.1] - 2026-08-16

### Added
- Support for GKI kernel versions on Android 12/13/14/15/16.

### Changed
- Renamed `scalpel.c` to `scalpel_main.c` and fixed `kern_path` and `path_put` symbol resolution issues.
- Updated `memrw.h` as a UAPI header, removing `__user` annotations.
- Updated `mtype.h` with `#endif` comments.
- Updated `vmem_rw.c` to handle the removal of `follow_pte` in Linux 6.12+, switching to `get_user_pages_remote`.

### Fixed
- Fixed symbol resolution issues for `kern_path` and `path_put` in `hide_inode.c`.
- Fixed dentry alias traversal errors caused by `CONFIG_DCACHE_WORD_ACCESS` (may still exist on some versions but adapted to the target kernel's hlist structure).

### Known Issues
- File hiding currently only supports memory-backed filesystems (tmpfs/devtmpfs); disk filesystems (ext4/f2fs) are not supported.
- The root user is not affected by the hiding mechanism, except that hidden files do not appear in directory listings (e.g., `ls`).
- The module is not hidden from `/proc/modules` (i.e., it still appears in `lsmod` output).
- At load time, the module hides the `/dev/scalpel` node for non-root users, preventing them from using it for subsequent dynamic hiding. This is by design.