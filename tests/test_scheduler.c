#include "test_harness.h"

#include "oslab/scheduler.h"

#include <errno.h>
#include <stdio.h>

static void expect_metrics(const os_job_metrics_t *job, int id, int start, int finish, int waiting, int turnaround,
                           int response)
{
    CHECK_EQ_LONG(job->id, id);
    CHECK_EQ_LONG(job->start_time, start);
    CHECK_EQ_LONG(job->completion_time, finish);
    CHECK_EQ_LONG(job->waiting_time, waiting);
    CHECK_EQ_LONG(job->turnaround_time, turnaround);
    CHECK_EQ_LONG(job->response_time, response);
}

static void test_fcfs_convoy(void)
{
    const os_job_t jobs[] = {
        {.id = 1, .arrival = 0, .burst = 24},
        {.id = 2, .arrival = 0, .burst = 3},
        {.id = 3, .arrival = 0, .burst = 3},
    };
    os_schedule_t schedule;
    CHECK(os_schedule(OS_SCHED_FCFS, jobs, 3, 0, &schedule) == 0);
    expect_metrics(&schedule.jobs[0], 1, 0, 24, 0, 24, 0);
    expect_metrics(&schedule.jobs[1], 2, 24, 27, 24, 27, 24);
    expect_metrics(&schedule.jobs[2], 3, 27, 30, 27, 30, 27);
    CHECK_EQ_LONG(schedule.total_waiting, 51);
    os_schedule_free(&schedule);
}

static void test_sjf_same_arrivals(void)
{
    const os_job_t jobs[] = {
        {.id = 1, .arrival = 0, .burst = 24},
        {.id = 2, .arrival = 0, .burst = 3},
        {.id = 3, .arrival = 0, .burst = 3},
    };
    os_schedule_t schedule;
    CHECK(os_schedule(OS_SCHED_SJF, jobs, 3, 0, &schedule) == 0);
    expect_metrics(&schedule.jobs[0], 1, 6, 30, 6, 30, 6);
    expect_metrics(&schedule.jobs[1], 2, 0, 3, 0, 3, 0);
    expect_metrics(&schedule.jobs[2], 3, 3, 6, 3, 6, 3);
    CHECK_EQ_LONG(schedule.total_waiting, 9);
    os_schedule_free(&schedule);
}

static void test_sjf_does_not_preempt(void)
{
    const os_job_t jobs[] = {
        {.id = 1, .arrival = 0, .burst = 7},
        {.id = 2, .arrival = 2, .burst = 4},
        {.id = 3, .arrival = 4, .burst = 1},
        {.id = 4, .arrival = 5, .burst = 4},
    };
    os_schedule_t schedule;
    CHECK(os_schedule(OS_SCHED_SJF, jobs, 4, 0, &schedule) == 0);
    expect_metrics(&schedule.jobs[0], 1, 0, 7, 0, 7, 0);
    expect_metrics(&schedule.jobs[1], 2, 8, 12, 6, 10, 6);
    expect_metrics(&schedule.jobs[2], 3, 7, 8, 3, 4, 3);
    expect_metrics(&schedule.jobs[3], 4, 12, 16, 7, 11, 7);
    os_schedule_free(&schedule);
}

static void test_rr_textbook(void)
{
    const os_job_t jobs[] = {
        {.id = 1, .arrival = 0, .burst = 24},
        {.id = 2, .arrival = 0, .burst = 3},
        {.id = 3, .arrival = 0, .burst = 3},
    };
    os_schedule_t schedule;
    CHECK(os_schedule(OS_SCHED_RR, jobs, 3, 4, &schedule) == 0);
    expect_metrics(&schedule.jobs[0], 1, 0, 30, 6, 30, 0);
    expect_metrics(&schedule.jobs[1], 2, 4, 7, 4, 7, 4);
    expect_metrics(&schedule.jobs[2], 3, 7, 10, 7, 10, 7);
    /* Response time ignores later preemption; waiting time includes it. */
    CHECK(schedule.jobs[0].waiting_time > schedule.jobs[0].response_time);
    os_schedule_free(&schedule);
}

static void test_rr_orders_mid_quantum_arrivals(void)
{
    const os_job_t jobs[] = {
        {.id = 1, .arrival = 0, .burst = 6},
        {.id = 2, .arrival = 4, .burst = 1},
        {.id = 3, .arrival = 1, .burst = 1},
    };
    os_schedule_t schedule;
    CHECK(os_schedule(OS_SCHED_RR, jobs, 3, 5, &schedule) == 0);
    expect_metrics(&schedule.jobs[0], 1, 0, 8, 2, 8, 0);
    expect_metrics(&schedule.jobs[1], 2, 6, 7, 2, 3, 2);
    expect_metrics(&schedule.jobs[2], 3, 5, 6, 4, 5, 4);
    os_schedule_free(&schedule);
}

static void test_fcfs_idle_gap(void)
{
    const os_job_t jobs[] = {
        {.id = 1, .arrival = 0, .burst = 2},
        {.id = 2, .arrival = 5, .burst = 3},
    };
    os_schedule_t schedule;
    CHECK(os_schedule(OS_SCHED_FCFS, jobs, 2, 0, &schedule) == 0);
    expect_metrics(&schedule.jobs[0], 1, 0, 2, 0, 2, 0);
    expect_metrics(&schedule.jobs[1], 2, 5, 8, 0, 3, 0);
    os_schedule_free(&schedule);
}

static void test_invalid_and_empty(void)
{
    os_schedule_t schedule;
    CHECK(os_schedule(OS_SCHED_FCFS, NULL, 0, 0, &schedule) == 0);
    CHECK_EQ_LONG(schedule.n, 0);
    os_schedule_free(&schedule);

    const os_job_t bad_burst[] = {{.id = 1, .arrival = 0, .burst = 0}};
    errno = 0;
    CHECK(os_schedule(OS_SCHED_FCFS, bad_burst, 1, 0, &schedule) == -1);
    CHECK_EQ_LONG(errno, EINVAL);

    const os_job_t one[] = {{.id = 1, .arrival = 0, .burst = 2}};
    errno = 0;
    CHECK(os_schedule(OS_SCHED_RR, one, 1, 0, &schedule) == -1);
    CHECK_EQ_LONG(errno, EINVAL);

    CHECK(os_schedule(OS_SCHED_FCFS, NULL, 1, 0, &schedule) == -1);
}

int main(void)
{
    test_fcfs_convoy();
    test_sjf_same_arrivals();
    test_sjf_does_not_preempt();
    test_rr_textbook();
    test_rr_orders_mid_quantum_arrivals();
    test_fcfs_idle_gap();
    test_invalid_and_empty();
    if (g_checks_failed != 0) {
        fprintf(stderr, "%d scheduler checks failed\n", g_checks_failed);
        return 1;
    }
    printf("scheduler checks passed\n");
    return 0;
}
