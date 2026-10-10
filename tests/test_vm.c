#include "test_harness.h"

#include "oslab/vm.h"

#include <errno.h>
#include <stdio.h>

static void test_textbook(void)
{
    size_t nrefs = 0;
    const int *refs = os_vm_textbook_refs(&nrefs);
    CHECK_EQ_LONG(nrefs, 20);

    os_vm_result_t fifo;
    os_vm_result_t lru;
    CHECK(os_vm_simulate(OS_REPLACE_FIFO, OS_VM_TEXTBOOK_FRAMES, refs, nrefs, &fifo) == 0);
    CHECK(os_vm_simulate(OS_REPLACE_LRU, OS_VM_TEXTBOOK_FRAMES, refs, nrefs, &lru) == 0);
    CHECK_EQ_LONG(fifo.faults, OS_VM_TEXTBOOK_FIFO_FAULTS);
    CHECK_EQ_LONG(fifo.hits, nrefs - OS_VM_TEXTBOOK_FIFO_FAULTS);
    CHECK_EQ_LONG(lru.faults, OS_VM_TEXTBOOK_LRU_FAULTS);
    CHECK_EQ_LONG(lru.hits, nrefs - OS_VM_TEXTBOOK_LRU_FAULTS);
    CHECK(lru.faults < fifo.faults);
}

static void test_hand_trace_and_belady(void)
{
    const int refs[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    os_vm_result_t fifo3;
    os_vm_result_t fifo4;
    os_vm_result_t lru3;
    CHECK(os_vm_simulate(OS_REPLACE_FIFO, 3, refs, 12, &fifo3) == 0);
    CHECK(os_vm_simulate(OS_REPLACE_FIFO, 4, refs, 12, &fifo4) == 0);
    CHECK(os_vm_simulate(OS_REPLACE_LRU, 3, refs, 12, &lru3) == 0);
    CHECK_EQ_LONG(fifo3.faults, 9);
    CHECK_EQ_LONG(fifo3.hits, 3);
    CHECK_EQ_LONG(fifo4.faults, 10);
    CHECK_EQ_LONG(lru3.faults, 10);
    CHECK_EQ_LONG(lru3.hits, 2);
    /* Belady's anomaly: FIFO with more frames faults more often on this string. */
    CHECK(fifo4.faults > fifo3.faults);
}

static void test_repeat_and_empty(void)
{
    const int refs[] = {1, 1, 2, 2};
    os_vm_result_t result;
    CHECK(os_vm_simulate(OS_REPLACE_FIFO, 1, refs, 4, &result) == 0);
    CHECK_EQ_LONG(result.faults, 2);
    CHECK_EQ_LONG(result.hits, 2);

    CHECK(os_vm_simulate(OS_REPLACE_LRU, 1, refs, 0, &result) == 0);
    CHECK_EQ_LONG(result.faults, 0);
    CHECK_EQ_LONG(result.hits, 0);
}

static void test_invalid(void)
{
    const int refs[] = {1, -1};
    os_vm_result_t result;
    errno = 0;
    CHECK(os_vm_simulate(OS_REPLACE_FIFO, 0, refs, 1, &result) == -1);
    CHECK_EQ_LONG(errno, EINVAL);

    errno = 0;
    CHECK(os_vm_simulate(OS_REPLACE_LRU, 2, refs, 2, &result) == -1);
    CHECK_EQ_LONG(errno, EINVAL);

    errno = 0;
    CHECK(os_vm_simulate((os_replace_policy_t)0, 1, refs, 1, &result) == -1);
    CHECK_EQ_LONG(errno, EINVAL);
}

int main(void)
{
    test_textbook();
    test_hand_trace_and_belady();
    test_repeat_and_empty();
    test_invalid();
    if (g_checks_failed != 0) {
        fprintf(stderr, "%d virtual-memory checks failed\n", g_checks_failed);
        return 1;
    }
    printf("virtual-memory checks passed\n");
    return 0;
}
