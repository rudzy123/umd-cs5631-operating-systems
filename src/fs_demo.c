#include "demo_util.h"

#include "oslab/fsutil.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_info(const char *path, const os_file_info_t *info)
{
    char kind = '?';
    if (info->is_symlink) {
        kind = 'l';
    } else if (info->is_directory) {
        kind = 'd';
    } else if (info->is_regular) {
        kind = '-';
    }

    char when[64];
    struct tm broken;
    if (localtime_r(&info->mtime, &broken) == NULL) {
        snprintf(when, sizeof when, "-");
    } else {
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &broken);
    }

    printf("%c %06o %8lld %s %s\n", kind, (unsigned)(info->mode & 0777), (long long)info->size, when, path);
}

static int visit_print(const char *path, const os_file_info_t *info, void *ctx)
{
    (void)ctx;
    print_info(path, info);
    return 0;
}

static int cmd_stat(const char *path)
{
    os_file_info_t info;
    if (os_fs_inspect(path, &info) != 0) {
        perror(path);
        return -1;
    }
    printf("path: %s\n", path);
    printf("mode: %06o\n", (unsigned)(info.mode & 07777));
    printf("size: %lld\n", (long long)info.size);
    printf("links: %llu\n", (unsigned long long)info.nlink);
    printf("uid: %llu gid: %llu\n", (unsigned long long)info.uid, (unsigned long long)info.gid);
    printf("regular: %s directory: %s symlink: %s\n", info.is_regular ? "yes" : "no",
           info.is_directory ? "yes" : "no", info.is_symlink ? "yes" : "no");
    return 0;
}

static int cmd_copy(const char *src, const char *dst, size_t chunk)
{
    if (os_fs_copy_file(src, dst, chunk) != 0) {
        perror("copy");
        return -1;
    }
    printf("copied %s -> %s with %zu-byte reads\n", src, dst, chunk == 0 ? (size_t)8192 : chunk);
    return 0;
}

static int cmd_walk(const char *dir)
{
    printf("read-only walk of %s\n", dir);
    if (os_fs_traverse(dir, visit_print, NULL) != 0) {
        perror(dir);
        return -1;
    }
    return 0;
}

static double elapsed_ms(const struct timespec *start, const struct timespec *end)
{
    long seconds = (long)(end->tv_sec - start->tv_sec);
    long nanos = end->tv_nsec - start->tv_nsec;
    return (double)seconds * 1000.0 + (double)nanos / 1.0e6;
}

static int cmd_compare(const char *src, const char *dir)
{
    char buffered[512];
    char unbuffered[512];
    int n = snprintf(buffered, sizeof buffered, "%s/buffered-copy", dir);
    int n2 = snprintf(unbuffered, sizeof unbuffered, "%s/unbuffered-copy", dir);
    if (n < 0 || n2 < 0 || (size_t)n >= sizeof buffered || (size_t)n2 >= sizeof unbuffered) {
        fprintf(stderr, "destination directory path is too long\n");
        return -1;
    }

    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (os_fs_copy_file(src, buffered, 8192) != 0) {
        perror("buffered copy");
        return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double buffered_ms = elapsed_ms(&start, &end);

    clock_gettime(CLOCK_MONOTONIC, &start);
    if (os_fs_copy_file(src, unbuffered, 1) != 0) {
        perror("unbuffered copy");
        return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double unbuffered_ms = elapsed_ms(&start, &end);

    os_file_info_t left;
    os_file_info_t right;
    if (os_fs_inspect(buffered, &left) != 0 || os_fs_inspect(unbuffered, &right) != 0) {
        perror("stat copy");
        return -1;
    }
    printf("buffered 8192-byte copy: %.3f ms (%lld bytes)\n", buffered_ms, (long long)left.size);
    printf("unbuffered 1-byte copy:  %.3f ms (%lld bytes)\n", unbuffered_ms, (long long)right.size);
    if (left.size != right.size) {
        fprintf(stderr, "copy sizes differ\n");
        return -1;
    }
    printf("both copies have the same length; user-space buffering changes syscall count, not file bytes\n");
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s stat <path>\n", argv0);
    fprintf(stderr, "  %s copy <src> <dst> [chunk]\n", argv0);
    fprintf(stderr, "  %s walk <directory>\n", argv0);
    fprintf(stderr, "  %s compare <src> <dest-dir>\n", argv0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    oslab_watchdog_arm(30);
    int rc = -1;
    if (strcmp(argv[1], "stat") == 0 && argc == 3) {
        rc = cmd_stat(argv[2]);
    } else if (strcmp(argv[1], "copy") == 0 && (argc == 4 || argc == 5)) {
        size_t chunk = 0;
        if (argc == 5) {
            chunk = (size_t)strtoul(argv[4], NULL, 10);
        }
        rc = cmd_copy(argv[2], argv[3], chunk);
    } else if (strcmp(argv[1], "walk") == 0 && argc == 3) {
        rc = cmd_walk(argv[2]);
    } else if (strcmp(argv[1], "compare") == 0 && argc == 4) {
        rc = cmd_compare(argv[2], argv[3]);
    } else if (strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        oslab_watchdog_disarm();
        return 0;
    } else {
        usage(argv[0]);
        oslab_watchdog_disarm();
        return 2;
    }

    oslab_watchdog_disarm();
    return rc == 0 ? 0 : 1;
}
