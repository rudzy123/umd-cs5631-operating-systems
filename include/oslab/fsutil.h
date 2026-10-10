#ifndef OSLAB_FSUTIL_H
#define OSLAB_FSUTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

/*
 * Userspace file utilities. Nothing here unlinks, renames, or writes a path
 * the caller did not pass as a copy destination. Directory walks use lstat /
 * fstatat(AT_SYMLINK_NOFOLLOW) and do not descend through symbolic links.
 */

typedef struct os_file_info {
    off_t size;
    mode_t mode;
    nlink_t nlink;
    uid_t uid;
    gid_t gid;
    time_t mtime;
    bool is_regular;
    bool is_directory;
    bool is_symlink;
} os_file_info_t;

/* lstat(2). Follows nothing. */
int os_fs_inspect(const char *path, os_file_info_t *info);

/*
 * Copy a regular file. chunk_size is the read/write buffer size (1 selects
 * unbuffered byte I/O; 0 selects a 8192-byte buffer).
 *
 * The bytes land in a temporary file in the destination directory, are
 * fsync'd, and then rename'd onto dst. A crash before rename leaves dst
 * unchanged. Copying a file onto itself (same device and inode) fails.
 */
int os_fs_copy_file(const char *src, const char *dst, size_t chunk_size);

typedef int (*os_fs_visit_fn)(const char *path, const os_file_info_t *info, void *ctx);

/*
 * Visit dir, then its descendants. The callback return value is propagated
 * and stops the walk. Symlinks are reported and not entered.
 */
int os_fs_traverse(const char *dir, os_fs_visit_fn visit, void *ctx);

#endif
