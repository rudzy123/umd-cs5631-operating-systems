#ifndef OSLAB_DEADLOCK_H
#define OSLAB_DEADLOCK_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Coffman conditions for a small mutex model, plus a live lock-ordering demo.
 *
 * Each sequence says "this thread locks first_lock, then second_lock".
 * The model treats every lock id as a non-preemptible mutex.
 *
 *   mutual exclusion — lock ids name mutexes
 *   hold and wait    — some thread requests a second lock while holding one
 *   no preemption    — a mutex cannot be taken away
 *   circular wait    — the holds-then-wants graph contains a cycle
 *
 * deadlock_possible is true only when all four are true. Lock ordering removes
 * circular wait: every thread locks the lower id first.
 */

typedef struct os_lock_sequence {
    int thread_id;
    int first_lock;
    int second_lock;
} os_lock_sequence_t;

typedef struct os_coffman_report {
    bool mutual_exclusion;
    bool hold_and_wait;
    bool no_preemption;
    bool circular_wait;
    bool deadlock_possible;
} os_coffman_report_t;

int os_coffman_analyze(const os_lock_sequence_t *sequences, size_t n,
                       os_coffman_report_t *out);

/*
 * Two threads each increment a shared counter `iterations` times while holding
 * two mutexes. Both threads lock the lower address first, so the acquisition
 * graph has no cycle. Returns 0 and writes iterations * 2 to *counter_out.
 */
int os_run_ordered_workers(int iterations, int *counter_out);

#endif
