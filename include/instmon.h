#ifndef INSTMON_H
#define INSTMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Buffers are deliberately bounded: procfs contains text controlled by other
 * processes. A truncated command is acceptable; an unbounded allocation is not. */
#define TEXT_CAP 4096
#define PROCESS_CAP 131072

typedef enum { VALUE_OK, VALUE_DENIED, VALUE_MISSING, VALUE_ERROR } ValueStatus;
typedef struct {
    pid_t pid, parent;
    uid_t uid;
    uint64_t start_ticks, cpu_ticks, rss, virtual_bytes, threads;
    uint64_t read_bytes, write_bytes;
    long priority, nice;
    char name[256], command[TEXT_CAP], owner[64], state;
    double sampled_at, cpu_percent, read_rate, write_rate, age;
    bool rates_valid;
    ValueStatus io_status;
} Process;

typedef struct {
    Process *items; /* The snapshot owns this array; snapshot_free releases it. */
    size_t count;
    uint64_t memory_total, memory_available;
    double uptime, collected_at, duration;
    long clock_ticks, page_size, cpus;
} Snapshot;

typedef struct {
    char executable[TEXT_CAP], directory[TEXT_CAP];
    char status[TEXT_CAP], limits[TEXT_CAP], memory[TEXT_CAP], files[32768];
    bool identity_valid;
} Details;

typedef enum { SORT_RAM, SORT_CPU, SORT_NAME, SORT_PID } SortKey;
typedef struct {
    bool list, inspect, json, reverse, interval_set, user_set, sysmon;
    pid_t pid;
    uid_t uid;
    double interval;
    SortKey sort;
    const char *filter;
} Options;

/* Parsing does not initialize the terminal. Return 1 for help/version, -1 for
 * invalid arguments, and 0 when options are ready for execution. */
int options_parse(int argc, char **argv, Options *options);
double monotonic_seconds(void);
void sanitize(char *text);
const char *value_status(ValueStatus status);
int parse_stat(char *text, Process *process, long page_size);
int snapshot_collect(Snapshot *snapshot);
void snapshot_free(Snapshot *snapshot);
void snapshot_rates(Snapshot *current, const Snapshot *previous);
void snapshot_query(Snapshot *snapshot, const Options *options);
const Process *snapshot_find(const Snapshot *snapshot, pid_t pid, uint64_t start);
void details_collect(const Process *process, Details *details);
/* A successful request means SIGTERM was delivered, not that the process exited.
 * A pidfd pins the target lifetime; no signal-by-numeric-PID fallback is used. */
typedef enum {
    TERM_SENT, TERM_EXITED, TERM_CHANGED, TERM_DENIED, TERM_UNSUPPORTED, TERM_ERROR, TERM_INVALID
} TerminateResult;
TerminateResult process_terminate(const Process *process);
const char *termination_message(TerminateResult result);
void output_snapshot(const Snapshot *snapshot, const Options *options);
int ui_run(const Options *options);

#endif
