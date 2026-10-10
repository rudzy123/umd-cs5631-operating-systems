# Virtual memory

`vm_demo` replays a reference string through a fixed number of frames. `os_vm_simulate` is the function under test.

## Addresses, pages, frames

A virtual address splits into a virtual page number and a page offset. On x86-64 with 4 KiB pages, the offset is the low 12 bits and the page number is the rest of the canonical address. A page is the virtual unit. A frame is a page-sized slot of physical memory.

The page table maps a virtual page to a frame, or records that the page is not resident. The processor caches recent mappings in the TLB. A TLB hit uses the cached frame number and does not walk the page table. A TLB miss walks the page table. That walk is not the same thing as a page fault. A page fault is the case where the page table itself says the page is not resident (or the access violates permissions). The kernel then allocates a frame, fills it from disk or from a zero page, installs the translation, and retries the instruction.

A context switch to another process installs a different page-table root. TLB entries tagged with the old address space must be ignored. Linux and modern x86-64 use process-context identifiers so some of those entries can remain.

This simulator does not model offsets, multi-level page tables, or the TLB. A "reference" is a page number. A "frame" is a resident slot. A fault here means the page was not among the resident pages and had to be loaded. Hits and faults are counted. Empty frames start unused, and a negative page number is rejected so that the empty marker `-1` cannot collide with a real page.

## FIFO

FIFO replaces the page that was loaded earliest. A hit does not change that order. With 3 frames and the Silberschatz reference string

```text
7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1
```

FIFO records 15 faults and 5 hits. `OS_VM_TEXTBOOK_FIFO_FAULTS` is that number.

FIFO can exhibit Belady's anomaly: more frames can produce more faults. On

```text
1 2 3 4 1 2 5 1 2 3 4 5
```

the tests expect 9 faults with 3 frames and 10 faults with 4 frames.

## LRU

LRU replaces the resident page whose most recent reference is the oldest. A hit refreshes that timestamp. Equal timestamps, which do not arise for distinct references in this clock, would break toward the lowest frame index.

On the same textbook string and 3 frames, LRU records 12 faults and 8 hits. The recent stack stays warmer than FIFO's load order, so pages such as 0 and 2 survive longer in the second half of the string.

LRU needs a timestamp or a stack update on every reference. Hardware usually approximates it with reference bits and a clock algorithm. The simulator uses exact timestamps because the reference string is already in hand.

## What a fault costs

In a real kernel a fault may allocate a frame, evict a dirty page (write it out), read the new page, and update the page table. The simulator counts the decision to evict. It does not model dirty bits, writeback, or disk latency. Use the fault counts to compare policies on the same string and the same frame count. Changing only one of those inputs is the experiment worth recording in the learning log.
