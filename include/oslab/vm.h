#ifndef OSLAB_VM_H
#define OSLAB_VM_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Fixed-frame page replacement over a reference string.
 *
 * A reference is a page number (>= 0). The simulator has frame_count frames,
 * initially empty. A reference to a page that is not resident is a page fault
 * and loads that page, replacing a resident page when every frame is full.
 *
 *   FIFO — replace the page that was loaded earliest
 *   LRU  — replace the page whose most recent reference is the oldest
 *          (equal timestamps break toward the lowest frame index)
 *
 * This models resident-set membership. It does not model page-table entries,
 * offsets, or the TLB; those are covered in docs/virtual-memory.md.
 */

typedef enum os_replace_policy {
    OS_REPLACE_FIFO = 1,
    OS_REPLACE_LRU = 2
} os_replace_policy_t;

typedef struct os_vm_result {
    size_t faults;
    size_t hits;
} os_vm_result_t;

typedef void (*os_vm_trace_fn)(size_t step, int page, bool fault,
                               const int *frames, size_t frame_count, void *ctx);

/* Textbook string from Silberschatz et al., 3 frames: FIFO 15 faults, LRU 12. */
#define OS_VM_TEXTBOOK_FRAMES 3u
#define OS_VM_TEXTBOOK_FIFO_FAULTS 15u
#define OS_VM_TEXTBOOK_LRU_FAULTS 12u

const int *os_vm_textbook_refs(size_t *count);

/* frame_count must be > 0. Page numbers must be >= 0. Empty strings succeed. */
int os_vm_simulate(os_replace_policy_t policy, size_t frame_count,
                   const int *refs, size_t nrefs, os_vm_result_t *out);

int os_vm_simulate_trace(os_replace_policy_t policy, size_t frame_count,
                         const int *refs, size_t nrefs, os_vm_result_t *out,
                         os_vm_trace_fn trace, void *ctx);

#endif
