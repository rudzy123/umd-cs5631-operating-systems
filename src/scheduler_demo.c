#include "demo_util.h"

#include "oslab/scheduler.h"

#include <stdio.h>
#include <stdlib.h>

static const os_job_t k_jobs[] = {
    {.id = 1, .arrival = 0, .burst = 8},
    {.id = 2, .arrival = 1, .burst = 4},
    {.id = 3, .arrival = 2, .burst = 9},
    {.id = 4, .arrival = 3, .burst = 5},
};

enum { k_quantum = 4 };

static void print_schedule(const char *name, const os_schedule_t *schedule)
{
    printf("== %s ==\n", name);
    printf("%4s %8s %8s %8s %10s %10s\n", "id", "start", "finish", "wait", "turnaround", "response");
    for (size_t i = 0; i < schedule->n; i++) {
        const os_job_metrics_t *job = &schedule->jobs[i];
        printf("%4d %8d %8d %8d %10d %10d\n", job->id, job->start_time, job->completion_time,
               job->waiting_time, job->turnaround_time, job->response_time);
    }
    if (schedule->n > 0) {
        double n = (double)schedule->n;
        printf("average waiting %.2f  turnaround %.2f  response %.2f\n",
               (double)schedule->total_waiting / n, (double)schedule->total_turnaround / n,
               (double)schedule->total_response / n);
    }
    printf("\n");
}

static int run_one(os_sched_policy_t policy, const char *name, int quantum)
{
    os_schedule_t schedule;
    if (os_schedule(policy, k_jobs, sizeof k_jobs / sizeof k_jobs[0], quantum, &schedule) != 0) {
        perror(name);
        return -1;
    }
    print_schedule(name, &schedule);
    os_schedule_free(&schedule);
    return 0;
}

int main(void)
{
    oslab_watchdog_arm(5);
    printf("Single CPU, jobs (id, arrival, burst): (1,0,8) (2,1,4) (3,2,9) (4,3,5)\n");
    printf("Round robin quantum = %d\n\n", k_quantum);

    int rc = run_one(OS_SCHED_FCFS, "first come, first served", 0);
    if (rc == 0) {
        rc = run_one(OS_SCHED_SJF, "shortest job first (non-preemptive)", 0);
    }
    if (rc == 0) {
        rc = run_one(OS_SCHED_RR, "round robin", k_quantum);
    }
    oslab_watchdog_disarm();
    return rc == 0 ? 0 : 1;
}
