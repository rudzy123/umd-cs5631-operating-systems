#include "oslab/scheduler.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>

static void finalize_metrics(const os_job_t *jobs, os_job_metrics_t *metrics, size_t n,
                             os_schedule_t *out)
{
    out->total_waiting = 0;
    out->total_turnaround = 0;
    out->total_response = 0;

    for (size_t i = 0; i < n; i++) {
        metrics[i].id = jobs[i].id;
        metrics[i].turnaround_time = metrics[i].completion_time - jobs[i].arrival;
        metrics[i].waiting_time = metrics[i].turnaround_time - jobs[i].burst;
        metrics[i].response_time = metrics[i].start_time - jobs[i].arrival;
        out->total_waiting += metrics[i].waiting_time;
        out->total_turnaround += metrics[i].turnaround_time;
        out->total_response += metrics[i].response_time;
    }
}

static int add_burst(int now, int burst, int *next)
{
    if (burst > INT_MAX - now) {
        errno = ERANGE;
        return -1;
    }
    *next = now + burst;
    return 0;
}

static int run_fcfs(const os_job_t *jobs, size_t n, os_job_metrics_t *metrics)
{
    size_t *order = calloc(n, sizeof(*order));
    if (order == NULL) {
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        order[i] = i;
        metrics[i].start_time = -1;
    }

    for (size_t i = 1; i < n; i++) {
        size_t key = order[i];
        size_t j = i;
        while (j > 0) {
            size_t prev = order[j - 1];
            bool earlier = jobs[key].arrival < jobs[prev].arrival ||
                           (jobs[key].arrival == jobs[prev].arrival && key < prev);
            if (!earlier) {
                break;
            }
            order[j] = prev;
            j--;
        }
        order[j] = key;
    }

    int now = 0;
    for (size_t k = 0; k < n; k++) {
        size_t i = order[k];
        if (now < jobs[i].arrival) {
            now = jobs[i].arrival;
        }
        metrics[i].start_time = now;
        int completion = 0;
        if (add_burst(now, jobs[i].burst, &completion) != 0) {
            free(order);
            return -1;
        }
        metrics[i].completion_time = completion;
        now = completion;
    }

    free(order);
    return 0;
}

static int run_sjf(const os_job_t *jobs, size_t n, os_job_metrics_t *metrics)
{
    bool *finished = calloc(n, sizeof(*finished));
    if (finished == NULL) {
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        metrics[i].start_time = -1;
    }

    int now = 0;
    size_t done = 0;
    while (done < n) {
        int best = -1;
        for (size_t i = 0; i < n; i++) {
            if (finished[i] || jobs[i].arrival > now) {
                continue;
            }
            if (best < 0) {
                best = (int)i;
                continue;
            }
            size_t b = (size_t)best;
            bool shorter = jobs[i].burst < jobs[b].burst;
            bool tie_arrival = jobs[i].burst == jobs[b].burst && jobs[i].arrival < jobs[b].arrival;
            bool tie_index = jobs[i].burst == jobs[b].burst && jobs[i].arrival == jobs[b].arrival && i < b;
            if (shorter || tie_arrival || tie_index) {
                best = (int)i;
            }
        }

        if (best < 0) {
            int next_arrival = INT_MAX;
            for (size_t i = 0; i < n; i++) {
                if (!finished[i] && jobs[i].arrival < next_arrival) {
                    next_arrival = jobs[i].arrival;
                }
            }
            if (next_arrival == INT_MAX) {
                free(finished);
                errno = EINVAL;
                return -1;
            }
            now = next_arrival;
            continue;
        }

        size_t i = (size_t)best;
        metrics[i].start_time = now;
        int completion = 0;
        if (add_burst(now, jobs[i].burst, &completion) != 0) {
            free(finished);
            return -1;
        }
        metrics[i].completion_time = completion;
        now = completion;
        finished[i] = true;
        done++;
    }

    free(finished);
    return 0;
}

static void rr_enqueue(size_t *queue, size_t cap, size_t *tail, size_t *count, size_t job)
{
    queue[*tail] = job;
    *tail = (*tail + 1) % cap;
    (*count)++;
}

static void rr_admit(const os_job_t *jobs, size_t n, bool *admitted, int low_exclusive, int high_inclusive,
                     size_t *queue, size_t *tail, size_t *count, size_t *scratch)
{
    size_t npick = 0;
    for (size_t i = 0; i < n; i++) {
        if (admitted[i]) {
            continue;
        }
        if (jobs[i].arrival > low_exclusive && jobs[i].arrival <= high_inclusive) {
            scratch[npick++] = i;
        }
    }

    for (size_t i = 1; i < npick; i++) {
        size_t key = scratch[i];
        size_t j = i;
        while (j > 0) {
            size_t prev = scratch[j - 1];
            bool earlier = jobs[key].arrival < jobs[prev].arrival ||
                           (jobs[key].arrival == jobs[prev].arrival && key < prev);
            if (!earlier) {
                break;
            }
            scratch[j] = prev;
            j--;
        }
        scratch[j] = key;
    }

    for (size_t p = 0; p < npick; p++) {
        rr_enqueue(queue, n, tail, count, scratch[p]);
        admitted[scratch[p]] = true;
    }
}

static int run_rr(const os_job_t *jobs, size_t n, int quantum, os_job_metrics_t *metrics)
{
    int *remaining = calloc(n, sizeof(*remaining));
    bool *admitted = calloc(n, sizeof(*admitted));
    size_t *queue = calloc(n, sizeof(*queue));
    size_t *scratch = calloc(n, sizeof(*scratch));
    if (remaining == NULL || admitted == NULL || queue == NULL || scratch == NULL) {
        free(remaining);
        free(admitted);
        free(queue);
        free(scratch);
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        remaining[i] = jobs[i].burst;
        metrics[i].start_time = -1;
    }

    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;
    int now = 0;
    size_t done = 0;
    int rc = 0;

    while (done < n) {
        rr_admit(jobs, n, admitted, INT_MIN, now, queue, &tail, &count, scratch);
        if (count == 0) {
            int next_arrival = INT_MAX;
            for (size_t i = 0; i < n; i++) {
                if (!admitted[i] && jobs[i].arrival < next_arrival) {
                    next_arrival = jobs[i].arrival;
                }
            }
            if (next_arrival == INT_MAX) {
                rc = -1;
                errno = EINVAL;
                break;
            }
            now = next_arrival;
            continue;
        }

        size_t i = queue[head];
        head = (head + 1) % n;
        count--;

        if (metrics[i].start_time < 0) {
            metrics[i].start_time = now;
        }

        int slice = remaining[i] < quantum ? remaining[i] : quantum;
        int started = now;
        int completion = 0;
        if (add_burst(now, slice, &completion) != 0) {
            rc = -1;
            break;
        }
        now = completion;
        remaining[i] -= slice;

        /* Arrivals during (started, now] join the queue before a preempted job. */
        rr_admit(jobs, n, admitted, started, now, queue, &tail, &count, scratch);

        if (remaining[i] == 0) {
            metrics[i].completion_time = now;
            done++;
        } else {
            rr_enqueue(queue, n, &tail, &count, i);
        }
    }

    free(remaining);
    free(admitted);
    free(queue);
    free(scratch);
    return rc;
}

int os_schedule(os_sched_policy_t policy, const os_job_t *jobs, size_t n, int quantum, os_schedule_t *out)
{
    if (out == NULL || (jobs == NULL && n > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (policy != OS_SCHED_FCFS && policy != OS_SCHED_SJF && policy != OS_SCHED_RR) {
        errno = EINVAL;
        return -1;
    }
    if (policy == OS_SCHED_RR && quantum <= 0) {
        errno = EINVAL;
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        if (jobs[i].arrival < 0 || jobs[i].burst <= 0) {
            errno = EINVAL;
            return -1;
        }
    }

    out->jobs = NULL;
    out->n = 0;
    out->total_waiting = 0;
    out->total_turnaround = 0;
    out->total_response = 0;
    if (n == 0) {
        return 0;
    }

    os_job_metrics_t *metrics = calloc(n, sizeof(*metrics));
    if (metrics == NULL) {
        errno = ENOMEM;
        return -1;
    }

    int rc = -1;
    switch (policy) {
    case OS_SCHED_FCFS:
        rc = run_fcfs(jobs, n, metrics);
        break;
    case OS_SCHED_SJF:
        rc = run_sjf(jobs, n, metrics);
        break;
    case OS_SCHED_RR:
        rc = run_rr(jobs, n, quantum, metrics);
        break;
    }

    if (rc != 0) {
        free(metrics);
        return -1;
    }

    out->jobs = metrics;
    out->n = n;
    finalize_metrics(jobs, metrics, n, out);
    return 0;
}

void os_schedule_free(os_schedule_t *schedule)
{
    if (schedule == NULL) {
        return;
    }
    free(schedule->jobs);
    schedule->jobs = NULL;
    schedule->n = 0;
    schedule->total_waiting = 0;
    schedule->total_turnaround = 0;
    schedule->total_response = 0;
}
