#include "test_harness.h"

#include "oslab/fsutil.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct visit_tally {
    int count;
    int symlinks;
    int a_txt;
} visit_tally_t;

static int on_visit(const char *path, const os_file_info_t *info, void *ctx)
{
    visit_tally_t *tally = ctx;
    tally->count++;
    if (info->is_symlink) {
        tally->symlinks++;
    }
    if (strstr(path, "/a.txt") != NULL) {
        tally->a_txt++;
    }
    return 0;
}

static int write_pattern(const char *path, size_t nbytes)
{
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) {
        return -1;
    }
    for (size_t i = 0; i < nbytes; i++) {
        unsigned char byte = (unsigned char)((i * 17u) + 3u);
        ssize_t wrote = write(fd, &byte, 1);
        if (wrote != 1) {
            close(fd);
            return -1;
        }
    }
    if (fchmod(fd, 0640) != 0) {
        close(fd);
        return -1;
    }
    if (close(fd) != 0) {
        return -1;
    }
    return 0;
}

static int files_match(const char *left_path, const char *right_path)
{
    FILE *left = fopen(left_path, "rb");
    FILE *right = fopen(right_path, "rb");
    if (left == NULL || right == NULL) {
        if (left != NULL) {
            fclose(left);
        }
        if (right != NULL) {
            fclose(right);
        }
        return -1;
    }

    int rc = 0;
    for (;;) {
        int a = fgetc(left);
        int b = fgetc(right);
        if (a != b) {
            rc = -1;
            break;
        }
        if (a == EOF) {
            break;
        }
    }
    fclose(left);
    fclose(right);
    return rc;
}

static void test_copy_and_walk(void)
{
    char root[] = "/tmp/oslab-fs-XXXXXX";
    if (mkdtemp(root) == NULL) {
        perror("mkdtemp");
        g_checks_failed++;
        return;
    }

    char src[256];
    char byte_copy[256];
    char buffered_copy[256];
    char sub[256];
    char nested[256];
    char sibling[256];
    char linkpath[256];
    CHECK(snprintf(src, sizeof src, "%s/source.bin", root) < (int)sizeof src);
    CHECK(snprintf(byte_copy, sizeof byte_copy, "%s/byte.bin", root) < (int)sizeof byte_copy);
    CHECK(snprintf(buffered_copy, sizeof buffered_copy, "%s/buffered.bin", root) < (int)sizeof buffered_copy);
    CHECK(snprintf(sub, sizeof sub, "%s/sub", root) < (int)sizeof sub);
    CHECK(snprintf(nested, sizeof nested, "%s/sub/a.txt", root) < (int)sizeof nested);
    CHECK(snprintf(sibling, sizeof sibling, "%s/b.txt", root) < (int)sizeof sibling);
    CHECK(snprintf(linkpath, sizeof linkpath, "%s/link", root) < (int)sizeof linkpath);

    const size_t nbytes = 5000;
    CHECK(write_pattern(src, nbytes) == 0);
    CHECK(os_fs_copy_file(src, byte_copy, 1) == 0);
    CHECK(os_fs_copy_file(src, buffered_copy, 8192) == 0);
    CHECK(files_match(src, byte_copy) == 0);
    CHECK(files_match(src, buffered_copy) == 0);

    os_file_info_t info;
    CHECK(os_fs_inspect(buffered_copy, &info) == 0);
    CHECK(info.is_regular);
    CHECK(!info.is_directory);
    CHECK(!info.is_symlink);
    CHECK_EQ_LONG(info.size, (long long)nbytes);
    CHECK_EQ_LONG(info.mode & 0777, 0640);

    errno = 0;
    CHECK(os_fs_copy_file(src, src, 8192) == -1);
    CHECK_EQ_LONG(errno, EINVAL);

    CHECK(mkdir(sub, 0700) == 0);
    CHECK(write_pattern(nested, 4) == 0);
    CHECK(write_pattern(sibling, 8) == 0);
    CHECK(symlink("sub", linkpath) == 0);

    errno = 0;
    CHECK(os_fs_copy_file(sub, byte_copy, 8192) == -1);
    CHECK_EQ_LONG(errno, EINVAL);

    os_file_info_t before;
    CHECK(os_fs_inspect(sibling, &before) == 0);
    visit_tally_t tally = {.count = 0, .symlinks = 0, .a_txt = 0};
    CHECK(os_fs_traverse(root, on_visit, &tally) == 0);
    os_file_info_t after;
    CHECK(os_fs_inspect(sibling, &after) == 0);
    CHECK_EQ_LONG(before.mtime, after.mtime);
    CHECK_EQ_LONG(tally.count, 8);
    CHECK_EQ_LONG(tally.symlinks, 1);
    CHECK_EQ_LONG(tally.a_txt, 1);

    errno = 0;
    CHECK(os_fs_traverse(sibling, on_visit, &tally) == -1);
    CHECK_EQ_LONG(errno, ENOTDIR);

    unlink(nested);
    unlink(sibling);
    unlink(linkpath);
    unlink(src);
    unlink(byte_copy);
    unlink(buffered_copy);
    rmdir(sub);
    rmdir(root);
}

int main(void)
{
    test_copy_and_walk();
    if (g_checks_failed != 0) {
        fprintf(stderr, "%d file-system checks failed\n", g_checks_failed);
        return 1;
    }
    printf("file-system checks passed\n");
    return 0;
}
