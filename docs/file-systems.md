# Files, descriptors, and buffering

`fs_demo` is the command-line front end. `os_fs_inspect`, `os_fs_copy_file`, and `os_fs_traverse` are the tested operations. None of them require root, and the walk never writes.

## Name, descriptor, open-file description, inode

A directory maps a name to an inode number. The inode stores the metadata `stat` reports: mode, link count, owner, group, size, and timestamps. The bytes of a regular file live in blocks reached from that inode.

`open` allocates a file descriptor, a small integer index in the process file-descriptor table. That entry points at an open-file description, which holds the offset and the access mode. `dup` and `fork` create another descriptor to the same open-file description, so they share the offset. Two separate `open` calls create two descriptions and two offsets, even when both names resolve to the same inode.

`os_fs_inspect` calls `lstat`. It reports the symlink itself when the path is a symlink. `stat` would follow the link. The demo prints the file type, the permission bits, the size, the link count, the owner, and the modification time.

## Buffering

There are three buffers in the path of a typical write:

1. The stdio buffer inside the process (`FILE`, often 8 KiB, flushed by `fflush`, a full buffer, or exit).
2. The buffer you pass to `read` and `write`.
3. The kernel page cache, which holds file pages until writeback.

`os_fs_copy_file` uses `read` and `write`, so it skips the stdio layer. The `chunk_size` argument is the size of the userspace buffer. `1` issues a system call per byte. `8192` (the default when the size is 0) moves 8 KiB per call. The bytes that land on disk are the same; the tests copy a 5000-byte pattern both ways and compare them. `fs_demo compare` times the two chunk sizes on a file you choose. The page cache still absorbs the writes until `fsync`.

## A copy that does not tear the destination

`os_fs_copy_file` refuses a non-regular source and refuses to copy a file onto the same device and inode.

The write path is:

1. `mkstemp` in the destination's directory, so the temporary file is on the same filesystem as the final name.
2. `read` / `write` in the requested chunk size, retrying `EINTR`.
3. `fchmod` to the source permission bits, without copying setuid or setgid.
4. `fsync` on the temporary file, so the bytes are stable before the name changes.
5. `rename` onto the destination. On the same filesystem, `rename` replaces the destination name atomically.
6. `fsync` on the parent directory so the directory entry itself is stable. Filesystems that reject directory `fsync` with `EINVAL`, `EPERM`, or `EROFS` are ignored; the file `fsync` has already completed.

If a step fails before `rename`, the temporary name is unlinked and the destination is left as it was. A crash before `rename` loses the copy, not the previous destination.

## A walk that does not modify the tree

`os_fs_traverse` `lstat`s the starting directory, then opens it with `O_DIRECTORY` and walks with `openat` / `fstatat(..., AT_SYMLINK_NOFOLLOW)`. `.` and `..` are skipped. A symlink is reported to the callback and is not opened, which prevents both leaving the tree through a link and looping on a symlink cycle. Descent stops at depth 64 with `ELOOP` as a backstop. The callback's non-zero return stops the walk.

The file test builds a temporary directory, a nested file, a sibling file, and a symlink to the nested directory. It expects the nested file to be visited once, the symlink to be visited once, and the sibling's `mtime` to be unchanged. That is the contract: inspection and traversal are not writers.

```bash
./fs_demo stat /tmp
./fs_demo walk .
./fs_demo copy source.bin /tmp/source-copy.bin 8192
./fs_demo compare source.bin /tmp
```

`walk .` prints the repository. It does not delete or rewrite anything it prints.
