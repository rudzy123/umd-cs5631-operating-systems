#include "demo_util.h"

#include "oslab/deadlock.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_report(const char *title, const os_coffman_report_t *report)
{
    printf("== %s ==\n", title);
    printf("mutual exclusion : %s\n", report->mutual_exclusion ? "present" : "absent");
    printf("hold and wait    : %s\n", report->hold_and_wait ? "present" : "absent");
    printf("no preemption    : %s\n", report->no_preemption ? "present" : "absent");
    printf("circular wait    : %s\n", report->circular_wait ? "present" : "absent");
    printf("deadlock possible: %s\n\n", report->deadlock_possible ? "yes" : "no");
}

static int analyze(const char *title, const os_lock_sequence_t *sequences, size_t n, bool expect_deadlock)
{
    os_coffman_report_t report;
    if (os_coffman_analyze(sequences, n, &report) != 0) {
        perror("os_coffman_analyze");
        return -1;
    }
    print_report(title, &report);
    if (report.deadlock_possible != expect_deadlock) {
        fprintf(stderr, "unexpected deadlock conclusion for %s\n", title);
        return -1;
    }
    return 0;
}

static int run_prevention(void)
{
    printf("== lock ordering prevention ==\n");
    printf("Both threads lock the lower mutex address first, then the higher one.\n");
    int counter = 0;
    if (os_run_ordered_workers(1000, &counter) != 0) {
        perror("os_run_ordered_workers");
        return -1;
    }
    if (counter != 2000) {
        fprintf(stderr, "expected counter 2000, got %d\n", counter);
        return -1;
    }
    printf("two threads completed 2000 critical sections without circular wait\n\n");
    return 0;
}

typedef struct inversion_arg {
    pthread_mutex_t *held;
    pthread_mutex_t *wanted;
    pthread_barrier_t *barrier;
    int timedlock_result;
} inversion_arg_t;

static void *inversion_worker(void *arg)
{
    inversion_arg_t *worker = arg;
    int rc = pthread_mutex_lock(worker->held);
    if (rc != 0) {
        worker->timedlock_result = rc;
        return NULL;
    }
    pthread_barrier_wait(worker->barrier);

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 200000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    rc = pthread_mutex_timedlock(worker->wanted, &deadline);
    worker->timedlock_result = rc;
    /*
     * Stay on the held mutex until the peer has also finished its timed lock.
     * Releasing earlier lets the peer acquire the mutex and hide the inversion.
     */
    pthread_barrier_wait(worker->barrier);
    pthread_mutex_unlock(worker->held);
    if (rc == 0) {
        pthread_mutex_unlock(worker->wanted);
    }
    return NULL;
}

/*
 * Opposite lock order would wait forever. timedlock bounds the wait, and the
 * process watchdog is a second backstop. CI does not pass this flag when
 * ThreadSanitizer is enabled, because TSan reports the inversion itself.
 */
static int run_unsafe_inversion(void)
{
    printf("== bounded lock inversion (explicit flag) ==\n");
    printf("Each thread holds one mutex and timed-locks the other for 200 ms.\n");

    pthread_mutex_t lock_a = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t lock_b = PTHREAD_MUTEX_INITIALIZER;
    pthread_barrier_t barrier;
    int rc = pthread_barrier_init(&barrier, NULL, 2);
    if (rc != 0) {
        errno = rc;
        perror("pthread_barrier_init");
        return -1;
    }

    inversion_arg_t left = {.held = &lock_a, .wanted = &lock_b, .barrier = &barrier, .timedlock_result = -1};
    inversion_arg_t right = {.held = &lock_b, .wanted = &lock_a, .barrier = &barrier, .timedlock_result = -1};
    pthread_t threads[2];

    rc = pthread_create(&threads[0], NULL, inversion_worker, &left);
    if (rc != 0) {
        pthread_barrier_destroy(&barrier);
        errno = rc;
        perror("pthread_create");
        return -1;
    }
    rc = pthread_create(&threads[1], NULL, inversion_worker, &right);
    if (rc != 0) {
        pthread_join(threads[0], NULL);
        pthread_barrier_destroy(&barrier);
        errno = rc;
        perror("pthread_create");
        return -1;
    }

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    pthread_barrier_destroy(&barrier);
    pthread_mutex_destroy(&lock_a);
    pthread_mutex_destroy(&lock_b);

    if (left.timedlock_result != ETIMEDOUT || right.timedlock_result != ETIMEDOUT) {
        fprintf(stderr, "expected both timed locks to return ETIMEDOUT (%d, %d)\n", left.timedlock_result,
                right.timedlock_result);
        return -1;
    }

    printf("both waits timed out and both mutexes were released\n");
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [--unsafe-inversion]\n", argv0);
}

int main(int argc, char **argv)
{
    bool unsafe = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--unsafe-inversion") == 0) {
            unsafe = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    oslab_watchdog_arm(10);

    const os_lock_sequence_t inverted[] = {
        {.thread_id = 1, .first_lock = 0, .second_lock = 1},
        {.thread_id = 2, .first_lock = 1, .second_lock = 0},
    };
    const os_lock_sequence_t ordered[] = {
        {.thread_id = 1, .first_lock = 0, .second_lock = 1},
        {.thread_id = 2, .first_lock = 0, .second_lock = 1},
    };

    int rc = analyze("opposite acquisition order", inverted, 2, true);
    if (rc == 0) {
        rc = analyze("identical acquisition order", ordered, 2, false);
    }
    if (rc == 0) {
        rc = run_prevention();
    }
    if (rc == 0 && unsafe) {
        rc = run_unsafe_inversion();
    }

    oslab_watchdog_disarm();
    if (rc == 0) {
        printf("deadlock study completed\n");
    }
    return rc == 0 ? 0 : 1;
}
