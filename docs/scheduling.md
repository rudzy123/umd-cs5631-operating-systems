# Scheduling

`scheduler_demo` prints one workload under three policies. `os_schedule` in `src/scheduler.c` is the code the tests call. The simulator is a single CPU and integer ticks. It is a teaching model of the metrics, not a model of the Linux scheduler.

## Metrics

For a job with arrival time `A`, burst `B`, first time on the CPU `S`, and completion `C`:

- turnaround time = `C - A` (submission to completion)
- waiting time = `C - A - B` (ready, not running)
- response time = `S - A` (submission to first dispatch)

On a non-preemptive policy, waiting time and response time are equal: the job waits once, then runs to completion. Under round robin they diverge, because later preemption adds waiting time and does not change `S`. The textbook round-robin test checks that gap on the long job.

## First come, first served

FCFS runs the earliest arrival to completion. Equal arrivals keep input order. If the CPU goes idle, time jumps to the next arrival.

The convoy effect is the cost. With arrivals at 0 and bursts 24, 3, 3:

| id | start | finish | waiting | turnaround | response |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 0 | 24 | 0 | 24 | 0 |
| 2 | 24 | 27 | 24 | 27 | 24 |
| 3 | 27 | 30 | 27 | 30 | 27 |

Average waiting time is 17. Two short jobs sit behind one long job.

## Shortest job first

Non-preemptive SJF, when the CPU is free, selects the arrived job with the smallest burst. Ties break by earlier arrival, then by input index. A job that has started is not preempted when a shorter job arrives.

Same three jobs: the two bursts of 3 run first (input order between them), then the burst of 24. Average waiting time is 3. That is the attraction of SJF, and it depends on knowing the burst before execution. A long job can starve if shorter work keeps arriving. This simulator does not estimate bursts; the caller supplies them.

Worked non-preemptive case used by the tests:

| id | arrival | burst | start | finish | waiting | response |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 0 | 7 | 0 | 7 | 0 | 0 |
| 3 | 4 | 1 | 7 | 8 | 3 | 3 |
| 2 | 2 | 4 | 8 | 12 | 6 | 6 |
| 4 | 5 | 4 | 12 | 16 | 7 | 7 |

Job 3 arrives at time 4 with a burst of 1 and still waits until time 7, because job 1 already holds the CPU.

## Round robin

The ready queue is FIFO. The running job receives `min(quantum, remaining)` ticks. Jobs that become runnable during that slice are inserted at the end of the slice in arrival order, input index breaking ties, and a preempted job is inserted after them.

Quantum 4 on the 24/3/3 workload:

| id | start | finish | waiting | turnaround | response |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 0 | 30 | 6 | 30 | 0 |
| 2 | 4 | 7 | 4 | 7 | 4 |
| 3 | 7 | 10 | 7 | 10 | 7 |

Job 1's response time is 0 and its waiting time is 6. A smaller quantum improves responsiveness and increases the number of context switches. A quantum large enough that every job finishes in one slice behaves as FCFS.

The demo workload is `(id, arrival, burst) = (1,0,8), (2,1,4), (3,2,9), (4,3,5)` with quantum 4. FCFS average waiting time is 8.75. SJF average waiting time is 7.75. Round robin average waiting time is 11.75 on this set: fairness for the later arrivals costs the short job less than FCFS, and the average is not always the winner. Run `scheduler_demo` and compare the three tables.

## Tradeoff

| Policy | Strength | Cost |
| --- | --- | --- |
| FCFS | Trivial, no burst estimate | Convoy effect |
| SJF | Strong average waiting time when bursts are known | Needs burst knowledge; can starve long jobs |
| RR | Bounds the wait before a ready job runs | Quantum too small spends the CPU switching |

Linux CFS is a different design. It tracks virtual runtime and tries to give each runnable task a fair share of the CPU, with sleeper fairness for interactive tasks. The three algorithms above are still the right tools for learning waiting, turnaround, and response time.
