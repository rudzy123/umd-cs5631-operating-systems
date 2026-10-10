# Synchronization

Shared mutable state needs a protocol. `thread_demo` and `deadlock_demo` are the runnable versions of this note. The bounded buffer and the Coffman model live in `src/bounded_buffer.c` and `src/deadlock.c`.

## Race condition

A race is a situation in which the result depends on the order of unsynchronized accesses, and at least one access is a write.

The unsafe counter is:

```c
(*counter)++;
```

That increment is a load, a modify, and a store. Two threads can both load the same value and both store `old + 1`, so one update disappears. `thread_demo --unsafe` runs this on purpose and then still runs the correct counter. CI never passes `--unsafe`. Under ThreadSanitizer the racy path aborts, which is the sanitizer doing its job.

The default path holds a `pthread_mutex_t` across the increment. A mutex provides mutual exclusion and a happens-before edge: everything done before `unlock` is visible to the thread that later `lock`s the same mutex.

## Mutex and condition variable

A mutex answers "who may touch this state." A condition variable answers "the state is not ready; wake me when it might be."

`os_bb_put` and `os_bb_take` share one mutex and use two condition variables, `not_full` and `not_empty`. The wait is always in a loop:

```c
while (buffer->count == buffer->capacity) {
    pthread_cond_wait(&buffer->not_full, &buffer->mu);
}
```

`pthread_cond_wait` atomically releases the mutex and blocks. On wakeup it reacquires the mutex. The `while` is required because wakeups can be spurious and because another thread may have consumed the last free slot before this thread runs. The signal is sent while the mutex is still held, after the slot or the item is published.

The timed variants use `pthread_cond_timedwait` with an absolute `CLOCK_REALTIME` deadline. On `ETIMEDOUT` they recheck the predicate so a producer that won the race is not reported as a timeout. Tests use those deadlines, and they also arm `SIGALRM`, so a missed wakeup fails the test instead of stalling it.

Invariants the tests check:

- occupancy never exceeds capacity
- a full buffer rejects a timed put
- an empty buffer rejects a timed take
- items leave in FIFO order
- two producers' sequences stay ordered inside the merged output

## The four necessary conditions

Coffman, Elphick, and Shoshani (1971) named four conditions that are all required for deadlock:

| Condition | In this lab |
| --- | --- |
| Mutual exclusion | Each lock id is a pthread mutex. |
| Hold and wait | A thread locks one mutex, then requests another. |
| No preemption | A mutex cannot be taken away from its holder. |
| Circular wait | The "holds, then wants" graph contains a cycle. |

`os_coffman_analyze` sets the first and third from the mutex model, derives hold-and-wait from any sequence whose two lock ids differ, and derives circular wait by a depth-first search on that graph. `deadlock_possible` is true only when all four are true. A cycle of three locks is detected, not only a pair of opposite edges.

## Prevention by lock ordering

The practical prevention used here removes circular wait. Every thread locks the lower mutex address first:

```c
if ((uintptr_t)&lock_b < (uintptr_t)&lock_a) {
    first = &lock_b;
    second = &lock_a;
}
```

Both workers in `os_run_ordered_workers` use that order. The acquisition graph has edges only from the lower lock to the higher lock, so it is acyclic. The test expects `iterations * 2` increments and is guarded by a five-second alarm.

## A bounded demonstration of inversion

`deadlock_demo --unsafe-inversion` builds the opposite order on real mutexes:

1. Thread 1 locks A. Thread 2 locks B.
2. A barrier makes both wait until both locks are held.
3. Each thread calls `pthread_mutex_timedlock` on the other mutex with a 200 ms deadline.
4. A second barrier runs before either thread unlocks, so the peer cannot slip in after the first timeout.

Both calls return `ETIMEDOUT`. Each thread then unlocks the mutex it holds. The process watchdog is a second bound. This shows that the circular-wait condition was met and that the process still terminates.

ThreadSanitizer reports a lock-order inversion even when the wait times out. The TSan CI job does not run this flag. AddressSanitizer does not track lock order, so the ASan job does run it, with a ctest timeout of 10 seconds.

Other prevention and avoidance ideas, stated only as design choices: acquire every lock a thread will need in one step (limits hold-and-wait), or allow the kernel to revoke a lock (preemption). This lab implements ordering, not a banker's algorithm and not lock revocation.
