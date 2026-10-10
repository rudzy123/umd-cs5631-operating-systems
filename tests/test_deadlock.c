#include "test_harness.h"

#include "oslab/deadlock.h"

#include <stdio.h>

static void expect_conditions(const os_lock_sequence_t *sequences, size_t n, bool exclusion, bool hold_and_wait,
                              bool no_preemption, bool circular, bool deadlock)
{
    os_coffman_report_t report;
    CHECK(os_coffman_analyze(sequences, n, &report) == 0);
    CHECK(report.mutual_exclusion == exclusion);
    CHECK(report.hold_and_wait == hold_and_wait);
    CHECK(report.no_preemption == no_preemption);
    CHECK(report.circular_wait == circular);
    CHECK(report.deadlock_possible == deadlock);
}

static void test_coffman(void)
{
    expect_conditions(NULL, 0, false, false, false, false, false);

    const os_lock_sequence_t single[] = {
        {.thread_id = 1, .first_lock = 0, .second_lock = 0},
    };
    expect_conditions(single, 1, true, false, true, false, false);

    const os_lock_sequence_t ordered[] = {
        {.thread_id = 1, .first_lock = 0, .second_lock = 1},
        {.thread_id = 2, .first_lock = 0, .second_lock = 1},
    };
    expect_conditions(ordered, 2, true, true, true, false, false);

    const os_lock_sequence_t inverted[] = {
        {.thread_id = 1, .first_lock = 0, .second_lock = 1},
        {.thread_id = 2, .first_lock = 1, .second_lock = 0},
    };
    expect_conditions(inverted, 2, true, true, true, true, true);

    const os_lock_sequence_t triangle[] = {
        {.thread_id = 1, .first_lock = 0, .second_lock = 1},
        {.thread_id = 2, .first_lock = 1, .second_lock = 2},
        {.thread_id = 3, .first_lock = 2, .second_lock = 0},
    };
    expect_conditions(triangle, 3, true, true, true, true, true);

    const os_lock_sequence_t disjoint[] = {
        {.thread_id = 1, .first_lock = 0, .second_lock = 1},
        {.thread_id = 2, .first_lock = 2, .second_lock = 3},
    };
    expect_conditions(disjoint, 2, true, true, true, false, false);
}

static void test_ordered_workers(void)
{
    test_arm_watchdog(5);
    int counter = 0;
    CHECK(os_run_ordered_workers(1000, &counter) == 0);
    CHECK_EQ_LONG(counter, 2000);
    test_disarm_watchdog();
}

int main(void)
{
    test_coffman();
    test_ordered_workers();
    if (g_checks_failed != 0) {
        fprintf(stderr, "%d deadlock checks failed\n", g_checks_failed);
        return 1;
    }
    printf("deadlock checks passed\n");
    return 0;
}
