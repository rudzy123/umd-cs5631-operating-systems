#include "oslab/deadlock.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

static int find_node(const int *nodes, size_t nnodes, int id)
{
    for (size_t i = 0; i < nnodes; i++) {
        if (nodes[i] == id) {
            return (int)i;
        }
    }
    return -1;
}

static bool dfs_cycle(size_t u, const size_t *adj_off, const size_t *adj, const size_t *adj_count,
                      unsigned char *color)
{
    color[u] = 1;
    size_t begin = adj_off[u];
    size_t end = begin + adj_count[u];
    for (size_t e = begin; e < end; e++) {
        size_t v = adj[e];
        if (color[v] == 1) {
            return true;
        }
        if (color[v] == 0 && dfs_cycle(v, adj_off, adj, adj_count, color)) {
            return true;
        }
    }
    color[u] = 2;
    return false;
}

static int graph_has_cycle(const os_lock_sequence_t *sequences, size_t n, bool *cycle_out)
{
    *cycle_out = false;
    if (n == 0) {
        return 0;
    }
    if (n > SIZE_MAX / 2) {
        errno = ENOMEM;
        return -1;
    }

    int *nodes = calloc(n * 2, sizeof(*nodes));
    size_t *edge_from = calloc(n, sizeof(*edge_from));
    size_t *edge_to = calloc(n, sizeof(*edge_to));
    if (nodes == NULL || edge_from == NULL || edge_to == NULL) {
        free(nodes);
        free(edge_from);
        free(edge_to);
        errno = ENOMEM;
        return -1;
    }

    size_t nnodes = 0;
    size_t nedges = 0;
    for (size_t i = 0; i < n; i++) {
        if (sequences[i].first_lock == sequences[i].second_lock) {
            continue;
        }
        int ids[2] = {sequences[i].first_lock, sequences[i].second_lock};
        size_t mapped[2] = {0, 0};
        for (int k = 0; k < 2; k++) {
            int found = find_node(nodes, nnodes, ids[k]);
            if (found < 0) {
                nodes[nnodes] = ids[k];
                mapped[k] = nnodes;
                nnodes++;
            } else {
                mapped[k] = (size_t)found;
            }
        }
        edge_from[nedges] = mapped[0];
        edge_to[nedges] = mapped[1];
        nedges++;
    }

    bool cycle = false;
    int rc = 0;
    if (nnodes > 0 && nedges > 0) {
        size_t *adj_count = calloc(nnodes, sizeof(*adj_count));
        size_t *adj_off = calloc(nnodes, sizeof(*adj_off));
        size_t *adj = calloc(nedges, sizeof(*adj));
        size_t *cursor = calloc(nnodes, sizeof(*cursor));
        unsigned char *color = calloc(nnodes, sizeof(*color));
        if (adj_count == NULL || adj_off == NULL || adj == NULL || cursor == NULL || color == NULL) {
            errno = ENOMEM;
            rc = -1;
        } else {
            for (size_t e = 0; e < nedges; e++) {
                adj_count[edge_from[e]]++;
            }
            size_t running = 0;
            for (size_t i = 0; i < nnodes; i++) {
                adj_off[i] = running;
                cursor[i] = running;
                running += adj_count[i];
            }
            for (size_t e = 0; e < nedges; e++) {
                size_t u = edge_from[e];
                adj[cursor[u]++] = edge_to[e];
            }
            for (size_t i = 0; i < nnodes; i++) {
                if (color[i] == 0 && dfs_cycle(i, adj_off, adj, adj_count, color)) {
                    cycle = true;
                    break;
                }
            }
        }
        free(adj_count);
        free(adj_off);
        free(adj);
        free(cursor);
        free(color);
    }

    free(nodes);
    free(edge_from);
    free(edge_to);
    if (rc == 0) {
        *cycle_out = cycle;
    }
    return rc;
}

int os_coffman_analyze(const os_lock_sequence_t *sequences, size_t n, os_coffman_report_t *out)
{
    if (out == NULL || (sequences == NULL && n > 0)) {
        errno = EINVAL;
        return -1;
    }

    out->mutual_exclusion = false;
    out->hold_and_wait = false;
    out->no_preemption = false;
    out->circular_wait = false;
    out->deadlock_possible = false;
    if (n == 0) {
        return 0;
    }

    /* This lab models pthread mutexes: exclusive and non-preemptible. */
    out->mutual_exclusion = true;
    out->no_preemption = true;

    for (size_t i = 0; i < n; i++) {
        if (sequences[i].first_lock != sequences[i].second_lock) {
            out->hold_and_wait = true;
            break;
        }
    }

    bool cycle = false;
    if (graph_has_cycle(sequences, n, &cycle) != 0) {
        return -1;
    }
    out->circular_wait = cycle;
    out->deadlock_possible = out->mutual_exclusion && out->hold_and_wait && out->no_preemption &&
                             out->circular_wait;
    return 0;
}

typedef struct ordered_worker_arg {
    pthread_mutex_t *first;
    pthread_mutex_t *second;
    int iterations;
    int *counter;
    int error;
} ordered_worker_arg_t;

static void *ordered_worker(void *arg)
{
    ordered_worker_arg_t *worker = arg;
    for (int i = 0; i < worker->iterations; i++) {
        int rc = pthread_mutex_lock(worker->first);
        if (rc != 0) {
            worker->error = rc;
            return NULL;
        }
        rc = pthread_mutex_lock(worker->second);
        if (rc != 0) {
            pthread_mutex_unlock(worker->first);
            worker->error = rc;
            return NULL;
        }
        (*worker->counter)++;
        pthread_mutex_unlock(worker->second);
        pthread_mutex_unlock(worker->first);
    }
    return NULL;
}

int os_run_ordered_workers(int iterations, int *counter_out)
{
    if (counter_out == NULL || iterations < 0) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_t lock_a = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t lock_b = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t *first = &lock_a;
    pthread_mutex_t *second = &lock_b;
    if ((uintptr_t)&lock_b < (uintptr_t)&lock_a) {
        first = &lock_b;
        second = &lock_a;
    }

    int counter = 0;
    ordered_worker_arg_t left = {.first = first, .second = second, .iterations = iterations, .counter = &counter, .error = 0};
    ordered_worker_arg_t right = {.first = first, .second = second, .iterations = iterations, .counter = &counter, .error = 0};

    pthread_t threads[2];
    int rc = pthread_create(&threads[0], NULL, ordered_worker, &left);
    if (rc != 0) {
        pthread_mutex_destroy(&lock_a);
        pthread_mutex_destroy(&lock_b);
        errno = rc;
        return -1;
    }
    rc = pthread_create(&threads[1], NULL, ordered_worker, &right);
    if (rc != 0) {
        pthread_join(threads[0], NULL);
        pthread_mutex_destroy(&lock_a);
        pthread_mutex_destroy(&lock_b);
        errno = rc;
        return -1;
    }

    rc = pthread_join(threads[0], NULL);
    int rc2 = pthread_join(threads[1], NULL);
    pthread_mutex_destroy(&lock_a);
    pthread_mutex_destroy(&lock_b);

    if (rc != 0 || rc2 != 0 || left.error != 0 || right.error != 0) {
        errno = rc != 0 ? rc : (rc2 != 0 ? rc2 : left.error != 0 ? left.error : right.error);
        return -1;
    }

    *counter_out = counter;
    return 0;
}
