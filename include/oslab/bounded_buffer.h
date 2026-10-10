#ifndef OSLAB_BOUNDED_BUFFER_H
#define OSLAB_BOUNDED_BUFFER_H

#include <stddef.h>
#include <time.h>

/*
 * Bounded FIFO buffer protected by one mutex and two condition variables.
 *
 * put waits until count < capacity. take waits until count > 0.
 * Waits recheck the predicate after wakeup so spurious wakeups are harmless.
 *
 * The timed calls use an absolute CLOCK_REALTIME deadline. On timeout they
 * return -1 and set errno to ETIMEDOUT, after rechecking the predicate so a
 * concurrent producer or consumer that won the race is not reported as a timeout.
 *
 * Destroy the buffer only after every producer and consumer has returned.
 */

typedef struct os_bounded_buffer os_bounded_buffer_t;

int os_bb_create(os_bounded_buffer_t **out, size_t capacity);
void os_bb_destroy(os_bounded_buffer_t *buffer);

int os_bb_put(os_bounded_buffer_t *buffer, int item);
int os_bb_take(os_bounded_buffer_t *buffer, int *item);

int os_bb_put_until(os_bounded_buffer_t *buffer, int item,
                    const struct timespec *absolute_deadline);
int os_bb_take_until(os_bounded_buffer_t *buffer, int *item,
                     const struct timespec *absolute_deadline);

/* Snapshot of the current occupancy. Safe to call concurrently with put/take. */
int os_bb_count(os_bounded_buffer_t *buffer, size_t *count);

#endif
