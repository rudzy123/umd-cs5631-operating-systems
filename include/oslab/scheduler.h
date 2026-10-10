#ifndef OSLAB_SCHEDULER_H
#define OSLAB_SCHEDULER_H

#include <stddef.h>

/*
 * Discrete-time CPU scheduling for a single processor.
 *
 * Time is an integer tick. A job that arrives at tick t becomes runnable at t.
 * Metrics, for each job:
 *   turnaround = completion - arrival
 *   waiting    = turnaround - burst
 *   response   = first time the job is on the CPU - arrival
 *
 * Tie breaks are deterministic:
 *   FCFS  — earlier arrival, then earlier input index
 *   SJF   — non-preemptive; smallest burst, then earlier arrival, then input index
 *   RR    — FIFO ready queue. Jobs that become runnable at the same tick enter
 *           in arrival order, with input index breaking ties. A job preempted
 *           at the end of its quantum is placed behind those arrivals.
 *
 * quantum is used only by RR and must be > 0 for that policy. Other policies
 * ignore it.
 */

typedef enum os_sched_policy {
    OS_SCHED_FCFS = 1,
    OS_SCHED_SJF = 2,
    OS_SCHED_RR = 3
} os_sched_policy_t;

typedef struct os_job {
    int id;
    int arrival;
    int burst;
} os_job_t;

typedef struct os_job_metrics {
    int id;
    int start_time;
    int completion_time;
    int waiting_time;
    int turnaround_time;
    int response_time;
} os_job_metrics_t;

typedef struct os_schedule {
    os_job_metrics_t *jobs; /* input order; NULL when n == 0 */
    size_t n;
    long total_waiting;
    long total_turnaround;
    long total_response;
} os_schedule_t;

/* Returns 0, or -1 with errno set to EINVAL, ERANGE, or ENOMEM. */
int os_schedule(os_sched_policy_t policy, const os_job_t *jobs, size_t n,
                int quantum, os_schedule_t *out);

void os_schedule_free(os_schedule_t *schedule);

#endif
