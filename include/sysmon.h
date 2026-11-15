#ifndef SYSMON_H
#define SYSMON_H

#include "instmon.h"

/* No heap allocations or threads: static hardware text is cached for the
 * session, while inexpensive diagnostics are refreshed only in the Sysmon view.
 * root is normally ""; tests supply a private proc/sys fixture tree. */
typedef struct {
    char root[1024];
    char os[256], kernel[256], hostname[256], cpu[256], topology[128];
    char gpu[1024], storage[1024], temperature[128], disk[256], load[128];
    char failed[4096], service_output[4096];
    double refreshed_at, services_at, child_started;
    pid_t child;
    int pipe_fd;
    size_t output_length;
    bool initialized, output_truncated, timed_out;
} Sysmon;

void sysmon_init(Sysmon *monitor, const char *root);
void sysmon_refresh(Sysmon *monitor, double now);
/* Nonblocking polling belongs to the existing TUI loop, not a new event loop. */
void sysmon_poll(Sysmon *monitor, double now);
void sysmon_format(const Sysmon *monitor, const Snapshot *snapshot, char *out, size_t capacity);
void sysmon_destroy(Sysmon *monitor);

#endif
