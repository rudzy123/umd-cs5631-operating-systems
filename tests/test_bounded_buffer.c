#include "test_harness.h"

#include "oslab/bounded_buffer.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void deadline_after_ms(struct timespec *deadline, long milliseconds)
{
    clock_gettime(CLOCK_REALTIME, deadline);
    deadline->tv_sec += milliseconds / 1000;
    deadline->tv_nsec += (milliseconds % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= 1000000000L;
    }
}

static void test_fifo_and_timeouts(void)
{
    os_bounded_buffer_t *buffer = NULL;
    CHECK(os_bb_create(&buffer, 3) == 0);

    CHECK(os_bb_put(buffer, 10) == 0);
    CHECK(os_bb_put(buffer, 20) == 0);
    CHECK(os_bb_put(buffer, 30) == 0);

    size_t count = 0;
    CHECK(os_bb_count(buffer, &count) == 0);
    CHECK_EQ_LONG(count, 3);

    struct timespec deadline;
    deadline_after_ms(&deadline, 50);
    errno = 0;
    CHECK(os_bb_put_until(buffer, 40, &deadline) == -1);
    CHECK_EQ_LONG(errno, ETIMEDOUT);
    CHECK(os_bb_count(buffer, &count) == 0);
    CHECK_EQ_LONG(count, 3);

    int value = 0;
    CHECK(os_bb_take(buffer, &value) == 0);
    CHECK_EQ_LONG(value, 10);
    CHECK(os_bb_put(buffer, 40) == 0);

    CHECK(os_bb_take(buffer, &value) == 0);
    CHECK_EQ_LONG(value, 20);
    CHECK(os_bb_take(buffer, &value) == 0);
    CHECK_EQ_LONG(value, 30);
    CHECK(os_bb_take(buffer, &value) == 0);
    CHECK_EQ_LONG(value, 40);

    deadline_after_ms(&deadline, 50);
    errno = 0;
    CHECK(os_bb_take_until(buffer, &value, &deadline) == -1);
    CHECK_EQ_LONG(errno, ETIMEDOUT);
    CHECK(os_bb_count(buffer, &count) == 0);
    CHECK_EQ_LONG(count, 0);

    os_bb_destroy(buffer);

    errno = 0;
    CHECK(os_bb_create(&buffer, 0) == -1);
    CHECK_EQ_LONG(errno, EINVAL);
}

enum { k_per_producer = 200, k_base_b = 10000, k_total = k_per_producer * 2 };

typedef struct pc_producer {
    os_bounded_buffer_t *buffer;
    int first;
    int count;
    int error;
} pc_producer_t;

typedef struct pc_consumer {
    os_bounded_buffer_t *buffer;
    int *items;
    int take_count;
    int error;
} pc_consumer_t;

static void *producer_main(void *arg)
{
    pc_producer_t *worker = arg;
    for (int i = 0; i < worker->count; i++) {
        if (os_bb_put(worker->buffer, worker->first + i) != 0) {
            worker->error = errno;
            return NULL;
        }
    }
    return NULL;
}

static void *consumer_main(void *arg)
{
    pc_consumer_t *worker = arg;
    for (int i = 0; i < worker->take_count; i++) {
        if (os_bb_take(worker->buffer, &worker->items[i]) != 0) {
            worker->error = errno;
            return NULL;
        }
    }
    return NULL;
}

static int compare_ints(const void *left, const void *right)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    return (a > b) - (a < b);
}

/* One consumer records take order, so each producer's subsequence must be FIFO. */
static void test_concurrent_fifo(void)
{
    test_arm_watchdog(5);

    os_bounded_buffer_t *buffer = NULL;
    CHECK(os_bb_create(&buffer, 8) == 0);
    int *items = calloc(k_total, sizeof(*items));
    CHECK(items != NULL);
    if (buffer == NULL || items == NULL) {
        test_disarm_watchdog();
        os_bb_destroy(buffer);
        free(items);
        return;
    }

    pc_producer_t producers[2] = {
        {.buffer = buffer, .first = 0, .count = k_per_producer, .error = 0},
        {.buffer = buffer, .first = k_base_b, .count = k_per_producer, .error = 0},
    };
    pc_consumer_t consumer = {.buffer = buffer, .items = items, .take_count = k_total, .error = 0};

    pthread_t threads[3];
    CHECK(pthread_create(&threads[0], NULL, producer_main, &producers[0]) == 0);
    CHECK(pthread_create(&threads[1], NULL, producer_main, &producers[1]) == 0);
    CHECK(pthread_create(&threads[2], NULL, consumer_main, &consumer) == 0);
    for (int i = 0; i < 3; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    CHECK_EQ_LONG(producers[0].error, 0);
    CHECK_EQ_LONG(producers[1].error, 0);
    CHECK_EQ_LONG(consumer.error, 0);

    int last_a = -1;
    int last_b = k_base_b - 1;
    int count_a = 0;
    int count_b = 0;
    for (int i = 0; i < k_total; i++) {
        int value = items[i];
        if (value < k_base_b) {
            CHECK_EQ_LONG(value, last_a + 1);
            last_a = value;
            count_a++;
        } else {
            CHECK_EQ_LONG(value, last_b + 1);
            last_b = value;
            count_b++;
        }
    }
    CHECK_EQ_LONG(count_a, k_per_producer);
    CHECK_EQ_LONG(count_b, k_per_producer);

    size_t occupancy = 99;
    CHECK(os_bb_count(buffer, &occupancy) == 0);
    CHECK_EQ_LONG(occupancy, 0);

    free(items);
    os_bb_destroy(buffer);
    test_disarm_watchdog();
}

/* Two consumers each keep their own take arrays. The combined multiset must be exact. */
static void test_concurrent_multiset(void)
{
    test_arm_watchdog(5);

    os_bounded_buffer_t *buffer = NULL;
    CHECK(os_bb_create(&buffer, 8) == 0);
    int *first = calloc(k_per_producer, sizeof(*first));
    int *second = calloc(k_per_producer, sizeof(*second));
    CHECK(first != NULL && second != NULL);
    if (buffer == NULL || first == NULL || second == NULL) {
        test_disarm_watchdog();
        os_bb_destroy(buffer);
        free(first);
        free(second);
        return;
    }

    pc_producer_t producers[2] = {
        {.buffer = buffer, .first = 0, .count = k_per_producer, .error = 0},
        {.buffer = buffer, .first = k_base_b, .count = k_per_producer, .error = 0},
    };
    pc_consumer_t consumers[2] = {
        {.buffer = buffer, .items = first, .take_count = k_per_producer, .error = 0},
        {.buffer = buffer, .items = second, .take_count = k_per_producer, .error = 0},
    };

    pthread_t threads[4];
    CHECK(pthread_create(&threads[0], NULL, producer_main, &producers[0]) == 0);
    CHECK(pthread_create(&threads[1], NULL, producer_main, &producers[1]) == 0);
    CHECK(pthread_create(&threads[2], NULL, consumer_main, &consumers[0]) == 0);
    CHECK(pthread_create(&threads[3], NULL, consumer_main, &consumers[1]) == 0);
    for (int i = 0; i < 4; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    CHECK_EQ_LONG(producers[0].error, 0);
    CHECK_EQ_LONG(producers[1].error, 0);
    CHECK_EQ_LONG(consumers[0].error, 0);
    CHECK_EQ_LONG(consumers[1].error, 0);

    int *all = calloc(k_total, sizeof(*all));
    CHECK(all != NULL);
    if (all != NULL) {
        memcpy(all, first, (size_t)k_per_producer * sizeof(*all));
        memcpy(all + k_per_producer, second, (size_t)k_per_producer * sizeof(*all));
        qsort(all, k_total, sizeof(*all), compare_ints);
        for (int i = 0; i < k_per_producer; i++) {
            CHECK_EQ_LONG(all[i], i);
            CHECK_EQ_LONG(all[k_per_producer + i], k_base_b + i);
        }
    }

    size_t occupancy = 99;
    CHECK(os_bb_count(buffer, &occupancy) == 0);
    CHECK_EQ_LONG(occupancy, 0);

    free(all);
    free(first);
    free(second);
    os_bb_destroy(buffer);
    test_disarm_watchdog();
}

int main(void)
{
    test_fifo_and_timeouts();
    test_concurrent_fifo();
    test_concurrent_multiset();
    if (g_checks_failed != 0) {
        fprintf(stderr, "%d bounded-buffer checks failed\n", g_checks_failed);
        return 1;
    }
    printf("bounded-buffer checks passed\n");
    return 0;
}
