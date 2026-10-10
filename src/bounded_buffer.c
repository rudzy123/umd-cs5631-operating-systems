#include "oslab/bounded_buffer.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

struct os_bounded_buffer {
    int *slots;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mu;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

int os_bb_create(os_bounded_buffer_t **out, size_t capacity)
{
    if (out == NULL || capacity == 0) {
        errno = EINVAL;
        return -1;
    }

    os_bounded_buffer_t *buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }
    buffer->slots = calloc(capacity, sizeof(*buffer->slots));
    if (buffer->slots == NULL) {
        free(buffer);
        errno = ENOMEM;
        return -1;
    }
    buffer->capacity = capacity;

    int rc = pthread_mutex_init(&buffer->mu, NULL);
    if (rc != 0) {
        free(buffer->slots);
        free(buffer);
        errno = rc;
        return -1;
    }
    rc = pthread_cond_init(&buffer->not_empty, NULL);
    if (rc != 0) {
        pthread_mutex_destroy(&buffer->mu);
        free(buffer->slots);
        free(buffer);
        errno = rc;
        return -1;
    }
    rc = pthread_cond_init(&buffer->not_full, NULL);
    if (rc != 0) {
        pthread_cond_destroy(&buffer->not_empty);
        pthread_mutex_destroy(&buffer->mu);
        free(buffer->slots);
        free(buffer);
        errno = rc;
        return -1;
    }

    *out = buffer;
    return 0;
}

void os_bb_destroy(os_bounded_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }
    pthread_cond_destroy(&buffer->not_full);
    pthread_cond_destroy(&buffer->not_empty);
    pthread_mutex_destroy(&buffer->mu);
    free(buffer->slots);
    free(buffer);
}

static int put_locked(os_bounded_buffer_t *buffer, int item, const struct timespec *deadline)
{
    while (buffer->count == buffer->capacity) {
        int rc;
        if (deadline == NULL) {
            rc = pthread_cond_wait(&buffer->not_full, &buffer->mu);
        } else {
            rc = pthread_cond_timedwait(&buffer->not_full, &buffer->mu, deadline);
        }
        if (rc == ETIMEDOUT) {
            /* The predicate can become true as the wait expires. Honor that. */
            if (buffer->count < buffer->capacity) {
                break;
            }
            errno = ETIMEDOUT;
            return -1;
        }
        if (rc != 0) {
            errno = rc;
            return -1;
        }
    }

    buffer->slots[buffer->tail] = item;
    buffer->tail = (buffer->tail + 1) % buffer->capacity;
    buffer->count++;
    assert(buffer->count <= buffer->capacity);
    pthread_cond_signal(&buffer->not_empty);
    return 0;
}

static int take_locked(os_bounded_buffer_t *buffer, int *item, const struct timespec *deadline)
{
    while (buffer->count == 0) {
        int rc;
        if (deadline == NULL) {
            rc = pthread_cond_wait(&buffer->not_empty, &buffer->mu);
        } else {
            rc = pthread_cond_timedwait(&buffer->not_empty, &buffer->mu, deadline);
        }
        if (rc == ETIMEDOUT) {
            if (buffer->count > 0) {
                break;
            }
            errno = ETIMEDOUT;
            return -1;
        }
        if (rc != 0) {
            errno = rc;
            return -1;
        }
    }

    *item = buffer->slots[buffer->head];
    buffer->head = (buffer->head + 1) % buffer->capacity;
    buffer->count--;
    assert(buffer->count <= buffer->capacity);
    pthread_cond_signal(&buffer->not_full);
    return 0;
}

static int lock_buffer(os_bounded_buffer_t *buffer)
{
    int rc = pthread_mutex_lock(&buffer->mu);
    if (rc != 0) {
        errno = rc;
        return -1;
    }
    return 0;
}

static int unlock_buffer(os_bounded_buffer_t *buffer, int op_rc)
{
    int rc = pthread_mutex_unlock(&buffer->mu);
    if (rc != 0) {
        errno = rc;
        return -1;
    }
    return op_rc;
}

int os_bb_put(os_bounded_buffer_t *buffer, int item)
{
    if (buffer == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (lock_buffer(buffer) != 0) {
        return -1;
    }
    return unlock_buffer(buffer, put_locked(buffer, item, NULL));
}

int os_bb_take(os_bounded_buffer_t *buffer, int *item)
{
    if (buffer == NULL || item == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (lock_buffer(buffer) != 0) {
        return -1;
    }
    return unlock_buffer(buffer, take_locked(buffer, item, NULL));
}

int os_bb_put_until(os_bounded_buffer_t *buffer, int item, const struct timespec *absolute_deadline)
{
    if (buffer == NULL || absolute_deadline == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (lock_buffer(buffer) != 0) {
        return -1;
    }
    return unlock_buffer(buffer, put_locked(buffer, item, absolute_deadline));
}

int os_bb_take_until(os_bounded_buffer_t *buffer, int *item, const struct timespec *absolute_deadline)
{
    if (buffer == NULL || item == NULL || absolute_deadline == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (lock_buffer(buffer) != 0) {
        return -1;
    }
    return unlock_buffer(buffer, take_locked(buffer, item, absolute_deadline));
}

int os_bb_count(os_bounded_buffer_t *buffer, size_t *count)
{
    if (buffer == NULL || count == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (lock_buffer(buffer) != 0) {
        return -1;
    }
    *count = buffer->count;
    return unlock_buffer(buffer, 0);
}
