#include "demo_util.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * POSIX process lifecycle, one child at a time:
 *   1. fork, _exit, waitpid, and decode the exit status
 *   2. fork, exec /bin/true, and wait
 *   3. an anonymous pipe from a child that execs /bin/echo
 *
 * Children cancel the parent's watchdog timer. fork does not need the timer,
 * and exec would otherwise keep it.
 */

static void child_clear_watchdog(void)
{
    alarm(0);
}

static int fail(const char *what)
{
    perror(what);
    return -1;
}

static const char *first_executable(const char *const *candidates)
{
    for (size_t i = 0; candidates[i] != NULL; i++) {
        if (access(candidates[i], X_OK) == 0) {
            return candidates[i];
        }
    }
    return NULL;
}

static int demo_exit_status(void)
{
    printf("== fork / waitpid / exit status ==\n");
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        return fail("fork");
    }
    if (pid == 0) {
        child_clear_watchdog();
        _exit(42);
    }

    int status = 0;
    pid_t reaped = waitpid(pid, &status, 0);
    if (reaped < 0) {
        return fail("waitpid");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
        fprintf(stderr, "expected exit status 42\n");
        return -1;
    }

    printf("child %ld terminated normally with status %d\n", (long)reaped, WEXITSTATUS(status));
    return 0;
}

static int demo_exec(void)
{
    printf("== fork / exec / wait ==\n");
    fflush(stdout);

    const char *const candidates[] = {"/bin/true", "/usr/bin/true", NULL};
    const char *program = first_executable(candidates);
    if (program == NULL) {
        fprintf(stderr, "could not find true(1)\n");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return fail("fork");
    }
    if (pid == 0) {
        child_clear_watchdog();
        execl(program, "true", (char *)NULL);
        _exit(127);
    }

    int status = 0;
    pid_t reaped = wait(&status);
    if (reaped < 0) {
        return fail("wait");
    }
    if (reaped != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "exec of %s did not exit 0\n", program);
        return -1;
    }

    printf("child %ld exec'd %s and wait() reported status %d\n", (long)reaped, program, WEXITSTATUS(status));
    return 0;
}

static int read_all(int fd, char *buffer, size_t capacity, size_t *out_len)
{
    size_t got = 0;
    while (got + 1 < capacity) {
        ssize_t nread = read(fd, buffer + got, capacity - 1 - got);
        if (nread == 0) {
            break;
        }
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        got += (size_t)nread;
    }
    buffer[got] = '\0';
    *out_len = got;
    return 0;
}

static int demo_pipe_exec(void)
{
    printf("== anonymous pipe / fork / exec ==\n");
    fflush(stdout);

    const char *const candidates[] = {"/bin/echo", "/usr/bin/echo", NULL};
    const char *program = first_executable(candidates);
    if (program == NULL) {
        fprintf(stderr, "could not find echo(1)\n");
        return -1;
    }

    int fds[2];
    if (pipe(fds) != 0) {
        return fail("pipe");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return fail("fork");
    }
    if (pid == 0) {
        child_clear_watchdog();
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(fds[1]);
        execl(program, "echo", "hello-from-child", (char *)NULL);
        _exit(127);
    }

    close(fds[1]);
    char message[64];
    size_t length = 0;
    int rc = read_all(fds[0], message, sizeof message, &length);
    close(fds[0]);
    if (rc != 0) {
        return fail("read");
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return fail("waitpid");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "echo child failed\n");
        return -1;
    }
    if (strcmp(message, "hello-from-child\n") != 0) {
        fprintf(stderr, "unexpected pipe payload: %s\n", message);
        return -1;
    }

    printf("parent read %zu bytes from the pipe: %s", length, message);
    return 0;
}

int main(void)
{
    oslab_watchdog_arm(10);

    int rc = demo_exit_status();
    if (rc == 0) {
        rc = demo_exec();
    }
    if (rc == 0) {
        rc = demo_pipe_exec();
    }

    oslab_watchdog_disarm();
    if (rc == 0) {
        printf("process demonstration completed\n");
    }
    return rc == 0 ? 0 : 1;
}
