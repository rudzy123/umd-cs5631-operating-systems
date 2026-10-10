#ifndef OSLAB_DEMO_UTIL_H
#define OSLAB_DEMO_UTIL_H

#include <signal.h>
#include <string.h>
#include <unistd.h>

/*
 * Every demonstration arms this watchdog before it waits on another thread
 * or child. The handler uses only async-signal-safe calls and exits, so a
 * logic bug cannot block CI forever.
 */

static void oslab_on_watchdog(int signo)
{
    (void)signo;
    const char message[] = "oslab: watchdog expired; exiting so the run cannot hang\n";
    ssize_t ignored = write(STDERR_FILENO, message, sizeof message - 1);
    (void)ignored;
    _exit(124);
}

static void oslab_watchdog_arm(unsigned seconds)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = oslab_on_watchdog;
    sigemptyset(&action.sa_mask);
    sigaction(SIGALRM, &action, NULL);
    alarm(seconds);
}

static void oslab_watchdog_disarm(void)
{
    alarm(0);
}

#endif
