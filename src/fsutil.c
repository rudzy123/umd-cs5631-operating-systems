#include "oslab/fsutil.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void info_from_stat(const struct stat *st, os_file_info_t *info)
{
    info->size = st->st_size;
    info->mode = st->st_mode;
    info->nlink = st->st_nlink;
    info->uid = st->st_uid;
    info->gid = st->st_gid;
    info->mtime = st->st_mtime;
    info->is_regular = S_ISREG(st->st_mode);
    info->is_directory = S_ISDIR(st->st_mode);
    info->is_symlink = S_ISLNK(st->st_mode);
}

int os_fs_inspect(const char *path, os_file_info_t *info)
{
    if (path == NULL || info == NULL) {
        errno = EINVAL;
        return -1;
    }
    struct stat st;
    if (lstat(path, &st) != 0) {
        return -1;
    }
    info_from_stat(&st, info);
    return 0;
}

static int copy_loop(int srcfd, int dstfd, size_t chunk)
{
    char *buffer = malloc(chunk);
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    for (;;) {
        ssize_t nread = read(srcfd, buffer, chunk);
        if (nread == 0) {
            break;
        }
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buffer);
            return -1;
        }
        size_t off = 0;
        while (off < (size_t)nread) {
            ssize_t nwrite = write(dstfd, buffer + off, (size_t)nread - off);
            if (nwrite < 0) {
                if (errno == EINTR) {
                    continue;
                }
                free(buffer);
                return -1;
            }
            off += (size_t)nwrite;
        }
    }

    free(buffer);
    return 0;
}

static int parent_directory(const char *path, char *dir, size_t dir_size)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        if (dir_size < 2) {
            errno = ENAMETOOLONG;
            return -1;
        }
        dir[0] = '.';
        dir[1] = '\0';
        return 0;
    }
    size_t len = (size_t)(slash - path);
    if (len == 0) {
        len = 1; /* "/" */
    }
    if (len + 1 > dir_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    return 0;
}

int os_fs_copy_file(const char *src, const char *dst, size_t chunk_size)
{
    if (src == NULL || dst == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (chunk_size == 0) {
        chunk_size = 8192;
    }

    int srcfd = open(src, O_RDONLY | O_CLOEXEC);
    if (srcfd < 0) {
        return -1;
    }

    struct stat src_st;
    if (fstat(srcfd, &src_st) != 0) {
        close(srcfd);
        return -1;
    }
    if (!S_ISREG(src_st.st_mode)) {
        close(srcfd);
        errno = EINVAL;
        return -1;
    }

    struct stat dst_st;
    if (stat(dst, &dst_st) == 0 && src_st.st_dev == dst_st.st_dev && src_st.st_ino == dst_st.st_ino) {
        close(srcfd);
        errno = EINVAL;
        return -1;
    }

    char dir[PATH_MAX];
    if (parent_directory(dst, dir, sizeof(dir)) != 0) {
        close(srcfd);
        return -1;
    }

    char tmp_path[PATH_MAX];
    int written = snprintf(tmp_path, sizeof(tmp_path), "%s/.oslab-copy-XXXXXX", dir);
    if (written < 0 || (size_t)written >= sizeof(tmp_path)) {
        close(srcfd);
        errno = ENAMETOOLONG;
        return -1;
    }

    int tmpfd = mkstemp(tmp_path);
    if (tmpfd < 0) {
        close(srcfd);
        return -1;
    }

    int rc = copy_loop(srcfd, tmpfd, chunk_size);
    if (rc == 0 && fchmod(tmpfd, src_st.st_mode & 0777) != 0) {
        rc = -1;
    }
    if (rc == 0 && fsync(tmpfd) != 0) {
        rc = -1;
    }
    if (close(tmpfd) != 0) {
        rc = -1;
    }
    tmpfd = -1;
    if (close(srcfd) != 0) {
        rc = -1;
    }
    srcfd = -1;

    if (rc == 0 && rename(tmp_path, dst) != 0) {
        rc = -1;
    }
    if (rc == 0) {
        int parent_fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (parent_fd >= 0) {
            if (fsync(parent_fd) != 0 && errno != EINVAL && errno != EPERM && errno != EROFS) {
                rc = -1;
            }
            close(parent_fd);
        }
    }
    if (rc != 0) {
        unlink(tmp_path);
    }
    return rc;
}

static int join_path(const char *parent, const char *name, char *out, size_t out_size)
{
    int written;
    if (parent[0] == '/' && parent[1] == '\0') {
        written = snprintf(out, out_size, "/%s", name);
    } else {
        written = snprintf(out, out_size, "%s/%s", parent, name);
    }
    if (written < 0 || (size_t)written >= out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int walk_directory(int fd, const char *path, os_fs_visit_fn visit, void *ctx, int depth)
{
    if (depth > 64) {
        close(fd);
        errno = ELOOP;
        return -1;
    }

    DIR *directory = fdopendir(fd);
    if (directory == NULL) {
        close(fd);
        return -1;
    }

    int rc = 0;
    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            errno = 0;
            continue;
        }

        struct stat st;
        if (fstatat(dirfd(directory), entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            rc = -1;
            break;
        }

        char child[PATH_MAX];
        if (join_path(path, entry->d_name, child, sizeof(child)) != 0) {
            rc = -1;
            break;
        }

        os_file_info_t info;
        info_from_stat(&st, &info);
        rc = visit(child, &info, ctx);
        if (rc != 0) {
            break;
        }

        if (S_ISDIR(st.st_mode)) {
            int child_fd = openat(dirfd(directory), entry->d_name,
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (child_fd < 0) {
                rc = -1;
                break;
            }
            rc = walk_directory(child_fd, child, visit, ctx, depth + 1);
            if (rc != 0) {
                break;
            }
        }
        errno = 0;
    }
    if (rc == 0 && errno != 0) {
        rc = -1;
    }

    closedir(directory);
    return rc;
}

int os_fs_traverse(const char *dir, os_fs_visit_fn visit, void *ctx)
{
    if (dir == NULL || visit == NULL) {
        errno = EINVAL;
        return -1;
    }

    os_file_info_t info;
    if (os_fs_inspect(dir, &info) != 0) {
        return -1;
    }
    if (!info.is_directory) {
        errno = ENOTDIR;
        return -1;
    }

    int rc = visit(dir, &info, ctx);
    if (rc != 0) {
        return rc;
    }

    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    return walk_directory(fd, dir, visit, ctx, 0);
}
