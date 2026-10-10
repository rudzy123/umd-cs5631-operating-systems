#ifndef OSLAB_TEST_HARNESS_H
#define OSLAB_TEST_HARNESS_H

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_checks_failed = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            g_checks_failed++;                                                      \
        }                                                                           \
    } while (0)

#define CHECK_EQ_LONG(actual, expected)                                                                 \
    do {                                                                                                \
        long long _actual = (long long)(actual);                                                       \
        long long _expected = (long long)(expected);                                                    \
        if (_actual != _expected) {                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #actual,       \
                    _actual, #expected, _expected);                                                     \
            g_checks_failed++;                                                                          \
        }                                                                                               \
    } while (0)

static inline void test_on_alarm(int signo)
{
    (void)signo;
    const char message[] = "FAIL: concurrent test watchdog expired\n";
    ssize_t ignored = write(STDERR_FILENO, message, sizeof message - 1);
    (void)ignored;
    _exit(124);
}

static inline void test_arm_watchdog(unsigned seconds)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = test_on_alarm;
    sigemptyset(&action.sa_mask);
    sigaction(SIGALRM, &action, NULL);
    alarm(seconds);
}

static inline void test_disarm_watchdog(void)
{
    alarm(0);
}

#endif
