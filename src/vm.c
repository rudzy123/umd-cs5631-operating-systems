#include "oslab/vm.h"

#include <errno.h>
#include <stdlib.h>

static const int k_textbook[] = {
    7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1
};

const int *os_vm_textbook_refs(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(k_textbook) / sizeof(k_textbook[0]);
    }
    return k_textbook;
}

static int find_page(const int *frames, size_t frame_count, int page)
{
    for (size_t i = 0; i < frame_count; i++) {
        if (frames[i] == page) {
            return (int)i;
        }
    }
    return -1;
}

static int lru_victim(const long *last_used, size_t frame_count)
{
    size_t victim = 0;
    for (size_t i = 1; i < frame_count; i++) {
        if (last_used[i] < last_used[victim]) {
            victim = i;
        }
    }
    return (int)victim;
}

int os_vm_simulate_trace(os_replace_policy_t policy, size_t frame_count, const int *refs, size_t nrefs,
                         os_vm_result_t *out, os_vm_trace_fn trace, void *ctx)
{
    if (out == NULL || (refs == NULL && nrefs > 0) || frame_count == 0) {
        errno = EINVAL;
        return -1;
    }
    if (policy != OS_REPLACE_FIFO && policy != OS_REPLACE_LRU) {
        errno = EINVAL;
        return -1;
    }
    for (size_t i = 0; i < nrefs; i++) {
        if (refs[i] < 0) {
            errno = EINVAL;
            return -1;
        }
    }

    int *frames = malloc(frame_count * sizeof(*frames));
    long *last_used = malloc(frame_count * sizeof(*last_used));
    if (frames == NULL || last_used == NULL) {
        free(frames);
        free(last_used);
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < frame_count; i++) {
        frames[i] = -1;
        last_used[i] = 0;
    }

    size_t filled = 0;
    size_t fifo_hand = 0;
    size_t faults = 0;
    size_t hits = 0;
    long tick = 0;

    for (size_t step = 0; step < nrefs; step++) {
        int page = refs[step];
        int resident = find_page(frames, frame_count, page);
        bool fault = resident < 0;
        if (!fault) {
            hits++;
            if (policy == OS_REPLACE_LRU) {
                tick++;
                last_used[resident] = tick;
            }
        } else {
            faults++;
            size_t slot = 0;
            if (filled < frame_count) {
                slot = filled;
                filled++;
            } else if (policy == OS_REPLACE_FIFO) {
                slot = fifo_hand;
                fifo_hand = (fifo_hand + 1) % frame_count;
            } else {
                slot = (size_t)lru_victim(last_used, frame_count);
            }
            frames[slot] = page;
            if (policy == OS_REPLACE_LRU) {
                tick++;
                last_used[slot] = tick;
            }
        }

        if (trace != NULL) {
            trace(step + 1, page, fault, frames, frame_count, ctx);
        }
    }

    out->faults = faults;
    out->hits = hits;
    free(frames);
    free(last_used);
    return 0;
}

int os_vm_simulate(os_replace_policy_t policy, size_t frame_count, const int *refs, size_t nrefs,
                   os_vm_result_t *out)
{
    return os_vm_simulate_trace(policy, frame_count, refs, nrefs, out, NULL, NULL);
}
