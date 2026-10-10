# Processes and threads

`process_demo` and `thread_demo` are the runnable versions of this note.

## What a process owns

A Linux process is the unit of resource ownership. It has:

- a virtual address space
- a file-descriptor table
- a process id (`pid`)
- signal dispositions and a set of pending signals
- credentials (uid, gid) and a current working directory

`fork` duplicates the caller. The child is a new process with its own pid and its own address space. Open file descriptions are shared in the sense that the file offset is shared across the duplicated descriptors: both processes initially refer to the same open-file description. Memory writes after `fork` are private to each process (copy-on-write in the kernel).

The child in the first demonstration calls `_exit(42)`. `_exit` skips stdio flush, which is the right call in a child that has not exec'd and may have copied its parent's buffers. The parent uses `waitpid` and reads the status with `WIFEXITED` and `WEXITSTATUS`.

## exec and wait

`exec` replaces the process image. The pid stays the same. Open descriptors remain open unless they were marked close-on-exec. The second demonstration `fork`s and `execl`s `true`. The parent reaps it with `wait`, which waits for any child. The lab keeps only one unreaped child at a time, so `wait` and `waitpid` observe the same process.

`WNOHANG` makes `waitpid` return immediately when no child has exited. The demos use a blocking wait so they observe the real status, and a watchdog so a stuck child cannot block the run forever.

## The anonymous pipe

`pipe` returns two descriptors: index 0 reads, index 1 writes. The third demonstration forks, points the child's standard output at the write end with `dup2`, closes the unused ends, and `exec`s `echo`. The parent closes its write end and reads until EOF. EOF arrives when every write end is closed, which is why both the parent and the child must close the end they do not use.

Pipe capacity is finite (`PIPE_BUF` is the atomic-write limit, 4096 bytes on Linux). This lab writes one short line, well under that limit.

## What a thread owns

A POSIX thread, on Linux implemented by NPTL, is a schedulable stack inside an existing process. Threads created with `pthread_create` share:

- the address space, including the heap and global variables
- the file-descriptor table
- the process id

Each thread has its own:

- stack and register state
- thread id (`pthread_t`, and a kernel tid)
- errno

Linux creates both processes and threads with `clone`. A thread is a `clone` that sets `CLONE_VM`, `CLONE_FILES`, and `CLONE_THREAD`. `fork` is the variant that does not share the address space. This lab calls the POSIX wrappers and does not issue `clone` directly.

## Context switch and mode switch

These are different events.

A **mode switch** happens when the running task enters or leaves the kernel: a system call, an interrupt, or an exception. The same task continues. User code runs in ring 3 and cannot change page tables, mask interrupts, or talk to devices. The kernel runs in ring 0 on a kernel stack.

A **context switch** saves one task's registers and stack pointer and restores another's. If the two tasks belong to different processes, the kernel also switches address spaces. On x86-64 that means a new page-table root, and translations cached in the TLB for the old address space must not be reused. A switch between threads of one process keeps the same address space.

`pthread_create` / `pthread_join` in `thread_demo` is where you can see the cost of extra schedulable contexts without a new address space. The mutex section of that program is about sharing, which is the next note.

## What to run

```bash
./process_demo
./thread_demo
```

Record the exit status, the pipe payload, and the protected counter in `docs/learning-log.md`.
