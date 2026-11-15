/* Small collector harness: tests supply a synthetic Linux interface tree.
 * Keeping assertions in Python makes hardware edge cases easy to describe. */
#include "sysmon.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    assert(argc >= 2);
    Sysmon monitor;
    sysmon_init(&monitor, argv[1]);
    double now = monotonic_seconds();
    sysmon_refresh(&monitor, now);
    double cached_at = monitor.refreshed_at;
    sysmon_refresh(&monitor, now + 1);
    assert(monitor.refreshed_at == cached_at);
    if (argc == 2) {
        double deadline = now + 4;
        while (monitor.child && monotonic_seconds() < deadline) {
            sysmon_poll(&monitor, monotonic_seconds());
            usleep(10000);
        }
        assert(!monitor.child);
    }
    Snapshot snapshot = {.memory_total = 8ULL * 1073741824, .memory_available = 2ULL * 1073741824, .uptime = 90060};
    char text[16384];
    sysmon_format(&monitor, &snapshot, text, sizeof text);
    puts(text);
    sysmon_destroy(&monitor);
    /* The collector must reap its own child on completion and early shutdown. */
    errno = 0;
    assert(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    return 0;
}
