# Learning log

Fill this in after you run the programs. Keep the command, the number you observed, and one sentence on the mechanism.

## Processes

Command:

```bash
./process_demo
```

- Child exit status:
- Program passed to `exec`:
- Bytes read from the pipe:
- Which descriptor did the child close before `exec`, and why does the parent then see EOF?

## Threads

Command:

```bash
./thread_demo
```

- Mutex counter and the expected product of threads and iterations:
- First and last values consumed from the bounded buffer:

Optional, outside CI:

```bash
./thread_demo --unsafe
```

- Unsafe counter:
- Which of the load / modify / store steps collided?

## Scheduling

Command:

```bash
./scheduler_demo
```

Record average waiting, turnaround, and response for the printed workload.

| Policy | Average waiting | Average turnaround | Average response |
| --- | --- | --- | --- |
| FCFS |  |  |  |
| SJF |  |  |  |
| RR (quantum 4) |  |  |  |

- Which policy had the worst convoy effect on this input?
- For the round-robin long job, which metric moved when the job was preempted, waiting or response?

## Deadlock

Command:

```bash
./deadlock_demo
```

- Which Coffman condition is present for opposite lock order and absent for identical order?
- Ordered-worker counter:

Optional, outside the ThreadSanitizer build:

```bash
./deadlock_demo --unsafe-inversion
```

- Return code of both timed locks:
- Did the process exit on its own?

## Virtual memory

Command:

```bash
./vm_demo
```

| Policy | Frames | Faults | Hits |
| --- | ---: | ---: | ---: |
| FIFO | 3 |  |  |
| LRU | 3 |  |  |

- Name one page FIFO evicts and LRU keeps in the textbook string.
- What does a fault mean in this simulator, and what extra work does a fault mean in the kernel?

## Files

```bash
./fs_demo stat /tmp
./fs_demo compare <a-file-you-choose> /tmp
```

- `/tmp` file type and mode:
- Buffered copy time:
- One-byte copy time:
- Same byte count for both copies? 

- Which `fsync` protects the file bytes, and which one protects the directory entry?
