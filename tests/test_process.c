/* Test real SIGTERM delivery only to disposable children. Link-time wrapping
 * injects kernel failures without requiring root or targeting system processes. */
#include "instmon.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int open_error, send_error, sends;
long __real_syscall(long number, ...);
long __wrap_syscall(long number, ...)
{
    va_list args;
    va_start(args, number);
    long result;
    if (number == SYS_pidfd_open) {
        pid_t pid = va_arg(args, pid_t);
        unsigned flags = va_arg(args, unsigned);
        if (open_error) { errno = open_error; result = -1; }
        else result = __real_syscall(number, pid, flags);
    } else {
        assert(number == SYS_pidfd_send_signal);
        int fd = va_arg(args, int), signal_number = va_arg(args, int);
        void *info = va_arg(args, void *);
        unsigned flags = va_arg(args, unsigned);
        assert(signal_number == SIGTERM && !info && flags == 0);
        ++sends;
        if (send_error) { errno = send_error; result = -1; }
        else result = __real_syscall(number, fd, signal_number, info, flags);
    }
    va_end(args);
    return result;
}

static Process identity(pid_t pid)
{
    Snapshot s;
    assert(!snapshot_collect(&s));
    Process found = {0};
    for (size_t i = 0; i < s.count; ++i) if (s.items[i].pid == pid) found = s.items[i];
    snapshot_free(&s);
    assert(found.pid == pid);
    return found;
}

int main(void)
{
    pid_t child = fork();
    assert(child >= 0);
    if (!child) { for (;;) pause(); }
    Process target = identity(child), changed = target;
    ++changed.start_ticks;
    assert(process_terminate(&changed) == TERM_CHANGED && sends == 0);
    Process invalid = {.pid = -1};
    assert(process_terminate(&invalid) == TERM_INVALID && sends == 0);
    open_error = ENOSYS;
    assert(process_terminate(&target) == TERM_UNSUPPORTED && sends == 0);
    open_error = EPERM;
    assert(process_terminate(&target) == TERM_DENIED && sends == 0);
    open_error = EMFILE;
    assert(process_terminate(&target) == TERM_ERROR && sends == 0);
    open_error = 0; send_error = EPERM;
    assert(process_terminate(&target) == TERM_DENIED);
    assert(!kill(child, 0)); /* Failed requests must leave the child alive. */
    send_error = ENOSYS;
    assert(process_terminate(&target) == TERM_UNSUPPORTED);
    send_error = ESRCH;
    assert(process_terminate(&target) == TERM_EXITED);
    send_error = 0;
    assert(process_terminate(&target) == TERM_SENT);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    assert(process_terminate(&target) == TERM_EXITED);
    puts("Process identity, SIGTERM, permission and kernel-failure tests passed");
    return 0;
}
