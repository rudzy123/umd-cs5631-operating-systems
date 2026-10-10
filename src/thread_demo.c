#include "demo_util.h"

#include "oslab/bounded_buffer.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    k_threads = 4,
    k_iterations = 5000,
    k_unsafe_iterations = 200000,
    k_items = 40,
    k_capacity = 4
};

typedef struct counter_arg {
    int *counter;
    pthread_mutex_t *mutex;
    int iterations;
    bool use_mutex;
} counter_arg_t;

static void *counter_worker(void *arg)
{
    counter_arg_t *worker = arg;
    for (int i = 0; i < worker->iterations; i++) {
        if (worker->use_mutex) {
            pthread_mutex_lock(worker->mutex);
        }
        (*worker->counter)++;
        if (worker->use_mutex) {
            pthread_mutex_unlock(worker->mutex);
        }
    }
    return NULL;
}

static int run_counter(bool use_mutex, int iterations)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    int counter = 0;
    pthread_t threads[k_threads];
    counter_arg_t args[k_threads];
    const int expected = k_threads * iterations;

    printf("== %s shared counter (%d threads x %d) ==\n",
           use_mutex ? "mutex-protected" : "UNSAFE unsynchronized", k_threads, iterations);
    if (!use_mutex) {
        printf("This path is a data race. The total may be short of %d.\n", expected);
        printf("It is not part of CI. Run it only with --unsafe.\n");
    }

    for (int i = 0; i < k_threads; i++) {
        args[i].counter = &counter;
        args[i].mutex = &mutex;
        args[i].iterations = iterations;
        args[i].use_mutex = use_mutex;
        int rc = pthread_create(&threads[i], NULL, counter_worker, &args[i]);
        if (rc != 0) {
            errno = rc;
            perror("pthread_create");
            return -1;
        }
    }
    for (int i = 0; i < k_threads; i++) {
        int rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            errno = rc;
            perror("pthread_join");
            return -1;
        }
    }
    pthread_mutex_destroy(&mutex);

    printf("counter = %d, expected = %d\n", counter, expected);
    if (use_mutex && counter != expected) {
        fprintf(stderr, "mutex counter lost updates\n");
        return -1;
    }
    return 0;
}

typedef struct producer_arg {
    os_bounded_buffer_t *buffer;
    int count;
    int error;
} producer_arg_t;

typedef struct consumer_arg {
    os_bounded_buffer_t *buffer;
    int count;
    int *out;
    int error;
} consumer_arg_t;

static void *producer(void *arg)
{
    producer_arg_t *worker = arg;
    for (int i = 0; i < worker->count; i++) {
        if (os_bb_put(worker->buffer, i) != 0) {
            worker->error = errno;
            return NULL;
        }
    }
    return NULL;
}

static void *consumer(void *arg)
{
    consumer_arg_t *worker = arg;
    for (int i = 0; i < worker->count; i++) {
        if (os_bb_take(worker->buffer, &worker->out[i]) != 0) {
            worker->error = errno;
            return NULL;
        }
    }
    return NULL;
}

static int run_producer_consumer(void)
{
    printf("== producer-consumer bounded buffer (capacity %d, %d items) ==\n", k_capacity, k_items);

    os_bounded_buffer_t *buffer = NULL;
    if (os_bb_create(&buffer, k_capacity) != 0) {
        perror("os_bb_create");
        return -1;
    }

    int *received = calloc((size_t)k_items, sizeof(*received));
    if (received == NULL) {
        os_bb_destroy(buffer);
        perror("calloc");
        return -1;
    }

    producer_arg_t producer_args = {.buffer = buffer, .count = k_items, .error = 0};
    consumer_arg_t consumer_args = {.buffer = buffer, .count = k_items, .out = received, .error = 0};
    pthread_t producer_thread;
    pthread_t consumer_thread;

    int rc = pthread_create(&producer_thread, NULL, producer, &producer_args);
    if (rc != 0) {
        free(received);
        os_bb_destroy(buffer);
        errno = rc;
        perror("pthread_create");
        return -1;
    }
    rc = pthread_create(&consumer_thread, NULL, consumer, &consumer_args);
    if (rc != 0) {
        pthread_join(producer_thread, NULL);
        free(received);
        os_bb_destroy(buffer);
        errno = rc;
        perror("pthread_create");
        return -1;
    }

    int join_rc = pthread_join(producer_thread, NULL);
    int join_rc2 = pthread_join(consumer_thread, NULL);
    if (join_rc != 0 || join_rc2 != 0 || producer_args.error != 0 || consumer_args.error != 0) {
        fprintf(stderr, "producer-consumer failed\n");
        free(received);
        os_bb_destroy(buffer);
        return -1;
    }

    int mismatches = 0;
    printf("consumed:");
    for (int i = 0; i < k_items; i++) {
        printf(" %d", received[i]);
        if (received[i] != i) {
            mismatches++;
        }
    }
    printf("\n");

    size_t occupancy = 1;
    os_bb_count(buffer, &occupancy);
    free(received);
    os_bb_destroy(buffer);

    if (mismatches != 0 || occupancy != 0) {
        fprintf(stderr, "buffer did not preserve FIFO order\n");
        return -1;
    }
    printf("FIFO order preserved and buffer is empty\n");
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [--unsafe]\n", argv0);
    fprintf(stderr, "  default    mutex counter and producer-consumer\n");
    fprintf(stderr, "  --unsafe   also run an unsynchronized counter (data race)\n");
}

int main(int argc, char **argv)
{
    bool unsafe = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--unsafe") == 0) {
            unsafe = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    oslab_watchdog_arm(20);
    int rc = 0;
    if (unsafe) {
        rc = run_counter(false, k_unsafe_iterations);
    }
    if (rc == 0) {
        rc = run_counter(true, k_iterations);
    }
    if (rc == 0) {
        rc = run_producer_consumer();
    }
    oslab_watchdog_disarm();
    if (rc == 0) {
        printf("thread demonstration completed\n");
    }
    return rc == 0 ? 0 : 1;
}
