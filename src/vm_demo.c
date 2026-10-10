#include "demo_util.h"

#include "oslab/vm.h"

#include <stdio.h>
#include <stdlib.h>

static void print_trace(size_t step, int page, bool fault, const int *frames, size_t frame_count, void *ctx)
{
    (void)ctx;
    printf("%4zu %6d  %-5s ", step, page, fault ? "FAULT" : "hit");
    for (size_t i = 0; i < frame_count; i++) {
        if (frames[i] < 0) {
            printf(" .");
        } else {
            printf(" %d", frames[i]);
        }
    }
    printf("\n");
}

static int run_policy(os_replace_policy_t policy, const char *name, size_t frames, const int *refs, size_t nrefs,
                      bool trace)
{
    os_vm_result_t result;
    printf("== %s, %zu frames ==\n", name, frames);
    if (trace) {
        printf("%4s %6s  %-5s %s\n", "step", "page", "event", "frames");
    }
    int rc = os_vm_simulate_trace(policy, frames, refs, nrefs, &result, trace ? print_trace : NULL, NULL);
    if (rc != 0) {
        perror(name);
        return -1;
    }
    printf("page faults %zu, hits %zu\n\n", result.faults, result.hits);
    return 0;
}

int main(void)
{
    oslab_watchdog_arm(5);
    size_t nrefs = 0;
    const int *refs = os_vm_textbook_refs(&nrefs);
    printf("Reference string (%zu pages), fixed frame count %u\n\n", nrefs, OS_VM_TEXTBOOK_FRAMES);

    int rc = run_policy(OS_REPLACE_FIFO, "FIFO", OS_VM_TEXTBOOK_FRAMES, refs, nrefs, true);
    if (rc == 0) {
        rc = run_policy(OS_REPLACE_LRU, "LRU", OS_VM_TEXTBOOK_FRAMES, refs, nrefs, true);
    }
    oslab_watchdog_disarm();
    return rc == 0 ? 0 : 1;
}
