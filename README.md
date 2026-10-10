# CS 5631 Operating Systems Laboratory

A Linux userspace laboratory for process control, threads, synchronization, scheduling metrics, page replacement, and file I/O. The programs are ordinary processes. They use C17, POSIX, and pthreads. They do not load kernel modules and they do not need root.

The simulators are there to make the metrics measurable. Linux itself schedules with CFS, manages memory with a multi-level page table and the TLB, and caches file data in the page cache. Each section below names that mechanism and points at the program that isolates one piece of it.

## Build, test, and run

Requirements: GCC or Clang with C17, CMake 3.16 or newer, pthreads, and, for the memory check, Valgrind.

AddressSanitizer and UndefinedBehaviorSanitizer (the configuration CI uses):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DOSLAB_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure --timeout 20
```

Valgrind cannot interpret a sanitizer build. Configure a second tree with the sanitizers off:

```bash
cmake -S . -B build-valgrind -DCMAKE_BUILD_TYPE=Debug
cmake --build build-valgrind --parallel
valgrind --leak-check=full --error-exitcode=1 ./build-valgrind/test_scheduler
```

ThreadSanitizer is a third tree. It cannot be combined with AddressSanitizer.

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DOSLAB_TSAN=ON
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure --timeout 30
```

Binaries are `process_demo`, `thread_demo`, `scheduler_demo`, `deadlock_demo`, `vm_demo`, and `fs_demo`.

| Program | What it shows |
| --- | --- |
| `process_demo` | `fork`, `exec`, `wait`, `waitpid`, exit status, anonymous pipe |
| `thread_demo` | `pthread_create` / `join`, mutex counter, bounded buffer |
| `scheduler_demo` | FCFS, SJF, round robin, and the three time metrics |
| `deadlock_demo` | Coffman conditions and lock-order prevention |
| `vm_demo` | FIFO and LRU fault counts on a fixed frame count |
| `fs_demo` | `stat` metadata, buffered and unbuffered copy, read-only walk |

Two flags exist for demonstrations that are unsafe if they run unbounded or unsynchronized. CI does not pass `--unsafe`. The lock-inversion flag is limited to builds without ThreadSanitizer.

```bash
./thread_demo --unsafe                  # data race on a counter; still joins
./deadlock_demo --unsafe-inversion      # opposite lock order, 200 ms timed locks
```

Every demo arms a `SIGALRM` watchdog. Concurrent tests do the same, and ctest sets a per-test timeout. A missed wakeup or a stuck child fails the run.

## Process and thread

A process owns an address space, a file-descriptor table, signal dispositions, and credentials. `fork` creates a second process. The child starts by returning 0 from `fork` in a copy of the parent's address space; writes after the call are private. `exec` replaces that image and keeps the pid. `waitpid` collects the exit status so the child does not remain a zombie. `process_demo` checks a raw `_exit(42)`, an `exec` of `true`, and a pipe whose write end is the standard output of `echo`.

A thread is a schedulable stack inside a process. Threads from `pthread_create` share the address space, the heap, and the file-descriptor table. Each has its own stack, registers, and errno. On Linux both operations are `clone`: a thread sets `CLONE_VM`, `CLONE_FILES`, and `CLONE_THREAD`, and `fork` does not share the address space. Sharing is why the counter in `thread_demo` needs a mutex, and why a bug in one thread can corrupt the whole process.

## Context switch, user mode, and kernel mode

User code runs in user mode (ring 3 on x86-64). It cannot install page tables, mask interrupts, or touch device registers. A system call, interrupt, or exception switches the CPU into kernel mode (ring 0) on a kernel stack and then returns to the same task. That is a mode switch.

A context switch saves one task's registers and stack and restores another task. Switching to a thread in the same process keeps the address space. Switching to another process installs that process's page-table root, and TLB entries that belong to the old address space must not be reused. The scheduling simulator counts the decisions a scheduler would make; it does not perform the register save itself.

## Synchronization and race conditions

A race is an outcome that depends on unsynchronized accesses when at least one access writes. `(*counter)++` is a load, an add, and a store. Two threads can load the same value and each store `old + 1`, and one update is lost. `thread_demo --unsafe` shows that. The default path holds a pthread mutex across the increment. Unlock happens-before the next lock of that same mutex, so the update is both exclusive and visible.

The producer-consumer buffer adds condition variables. `put` waits on `not_full` while `count == capacity`. `take` waits on `not_empty` while `count == 0`. The wait sits in a `while` loop because a wakeup can be spurious and because another thread can take the last slot before the waiter runs. `pthread_cond_wait` releases the mutex while the thread sleeps and reacquires it before returning. Tests check FIFO order, the capacity bound, and timed failure on a full or empty buffer.

## Deadlock

Deadlock requires all four Coffman conditions:

1. Mutual exclusion — the resource is a mutex.
2. Hold and wait — a thread holds one mutex and requests another.
3. No preemption — the holder cannot be forced to unlock.
4. Circular wait — the "holds, then wants" graph has a cycle.

`deadlock_demo` evaluates both an opposite-order pair (all four present) and a single global order (circular wait absent). Prevention in the live program is the global order: both threads lock the lower mutex address first, then the higher one. The inversion flag uses `pthread_mutex_timedlock` so the circular wait is observable and the process still exits. Details and the graph walk are in [docs/synchronization.md](docs/synchronization.md).

## Scheduling tradeoffs

The simulator is one CPU and integer ticks. For arrival `A`, burst `B`, first dispatch `S`, and completion `C`:

- turnaround = `C - A`
- waiting = `C - A - B`
- response = `S - A`

FCFS is simple and convoys short jobs behind a long one. Non-preemptive SJF lowers average waiting time when the burst is known, and a long job can starve. Round robin bounds how long a ready job waits for the CPU; a tiny quantum spends the processor switching, and a huge quantum becomes FCFS. Response time records the first dispatch, so later preemption increases waiting time and leaves response time unchanged. Worked tables are in [docs/scheduling.md](docs/scheduling.md).

Linux CFS is not these three algorithms. It accounts virtual runtime so runnable tasks get a fair share. The lab still uses FCFS, SJF, and round robin because their metrics are the ones you can compute by hand and test exactly.

## Virtual addresses, pages, frames, and the TLB

A virtual address is a virtual page number plus an offset. With 4 KiB pages the offset is 12 bits. The page table maps a virtual page to a physical frame, or records that the page is not resident. The TLB caches recent translations. A TLB hit does not walk the page table. A TLB miss walks it. A page fault is the further case in which the page is not resident or the access is illegal; the kernel then fills a frame and retries the instruction.

`vm_demo` does not model offsets or the TLB. It answers a smaller question: given a reference string and a fixed frame count, how many references miss in the resident set under FIFO and under LRU? FIFO replaces the oldest load and can fault more often with more frames (Belady's anomaly; the tests include that string). LRU replaces the page used furthest in the past. On the textbook string of 20 references and 3 frames, FIFO faults 15 times and LRU faults 12. See [docs/virtual-memory.md](docs/virtual-memory.md).

## Files, descriptors, metadata, and buffering

A directory entry is a name for an inode. The inode holds the metadata: mode, link count, owner, size, timestamps. A file descriptor is an index in the process table and points at an open-file description, which holds the offset. `dup` and inheritance across `fork` share that offset. A second `open` does not.

`fs_demo stat` uses `lstat`, so a symlink is described as a symlink. `fs_demo walk` visits a directory with `openat` and `fstatat(AT_SYMLINK_NOFOLLOW)` and does not descend into symlinks or change any inode it reports.

A copy passes through a userspace buffer and the kernel page cache. `fs_demo compare` copies once with 8192-byte `read`/`write` calls and once with 1-byte calls. The byte strings match; the time differs because the syscall rate differs. The safe copy writes a temporary file in the destination directory, `fsync`s it, and `rename`s it into place, so a crash before `rename` leaves the previous destination intact. The protocol is in [docs/file-systems.md](docs/file-systems.md).

## Layout

```text
include/oslab/          public headers for the five libraries
src/                    libraries and the six programs
tests/                  scheduler, pager, buffer, file copy, deadlock
docs/                   notes and a learning log
.github/workflows/ci.yml
```

CI builds with AddressSanitizer and UndefinedBehaviorSanitizer and runs ctest. A second job runs Valgrind memcheck on an unsanitized build. A third job runs ThreadSanitizer with `continue-on-error`, so a TSan-only finding does not hide the sanitizer and test results. The TSan job skips `--unsafe-inversion`.

Deeper notes:

- [Processes and threads](docs/processes-threads.md)
- [Synchronization](docs/synchronization.md)
- [Scheduling](docs/scheduling.md)
- [Virtual memory](docs/virtual-memory.md)
- [Files](docs/file-systems.md)
- [Learning log](docs/learning-log.md)

## License

MIT. See [LICENSE](LICENSE).
