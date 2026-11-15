#include "instmon.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

double monotonic_seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

void sanitize(char *text)
{
    /* Only printable ASCII is rendered in v0.1. This also prevents terminal
     * escapes and invalid byte sequences from process-controlled names. */
    for (unsigned char *p = (unsigned char *)text; *p; ++p)
        if (*p < 32 || *p >= 127) *p = '?';
}

const char *value_status(ValueStatus status)
{
    static const char *names[] = {"available", "permission denied", "unavailable", "read error"};
    return names[status];
}

/* Always NUL-terminate, even on failure. The caller chooses the read budget.
 * Multi-line detail files preserve newlines but remove other control bytes. */
static ValueStatus read_text(const char *path, char *out, size_t capacity, bool command)
{
    out[0] = '\0';
    FILE *file = fopen(path, "rb");
    if (!file) return errno == EACCES || errno == EPERM ? VALUE_DENIED :
                      errno == ENOENT || errno == ESRCH ? VALUE_MISSING : VALUE_ERROR;
    size_t count = fread(out, 1, capacity - 1, file);
    bool failed = ferror(file);
    int extra = fgetc(file);
    fclose(file);
    for (size_t i = 0; i < count; ++i) {
        unsigned char ch = (unsigned char)out[i];
        if (command && ch == 0) out[i] = ' ';
        else if ((ch < 32 && ch != '\n' && ch != '\t') || ch >= 127) out[i] = '?';
    }
    out[count] = '\0';
    if (extra != EOF && capacity > 20) memcpy(out + capacity - 16, "\n[truncated]\n", 14);
    return failed ? VALUE_ERROR : VALUE_OK;
}

/* stat's parenthesized command can contain spaces and ')' characters. The
 * final ')' delimits it; fields after that point are numeric except state. */
int parse_stat(char *text, Process *p, long page_size)
{
    char *open = strchr(text, '('), *close = strrchr(text, ')');
    if (!open || !close || close <= open || page_size <= 0) return -1;
    size_t length = (size_t)(close - open - 1);
    if (length >= sizeof p->name) length = sizeof p->name - 1;
    memcpy(p->name, open + 1, length); p->name[length] = 0; sanitize(p->name);
    char *save = NULL;
    char *token = strtok_r(close + 1, " \n", &save);
    for (int field = 3; field <= 24; ++field) {
        if (!token) return -1;
        if (field == 3) {
            if (strlen(token) != 1 || !strchr("RSDZTtXxKWPI", token[0])) return -1;
            p->state = token[0];
        }
        else {
            char *end;
            errno = 0;
            /* Some irrelevant stat fields are signed. Parse only the fields
             * we consume, choosing signedness according to procfs semantics. */
            if (field == 18 || field == 19 || field == 24) {
                long long value = strtoll(token, &end, 10);
                if (errno || *end) return -1;
                if (field == 18) p->priority = (long)value;
                if (field == 19) p->nice = (long)value;
                if (field == 24) {
                    if (value < 0 || (uint64_t)value > UINT64_MAX / (uint64_t)page_size) return -1;
                    p->rss = (uint64_t)value * (uint64_t)page_size;
                }
            } else if (field == 4 || field == 14 || field == 15 || field == 20 || field == 22 || field == 23) {
                if (*token == '-') return -1;
                unsigned long long value = strtoull(token, &end, 10);
                if (errno || *end) return -1;
                if (field == 4) { if (value > INT_MAX) return -1; p->parent = (pid_t)value; }
                if (field == 14) p->cpu_ticks = value;
                if (field == 15) { if (UINT64_MAX - p->cpu_ticks < value) return -1; p->cpu_ticks += value; }
                if (field == 20) p->threads = value;
                if (field == 22) p->start_ticks = value;
                if (field == 23) p->virtual_bytes = value;
            }
        }
        token = strtok_r(NULL, " \n", &save);
    }
    return 0;
}

static int read_stat(pid_t pid, Process *p, long page_size)
{
    char path[128], text[TEXT_CAP];
    snprintf(path, sizeof path, "/proc/%ld/stat", (long)pid);
    if (read_text(path, text, sizeof text, false) != VALUE_OK) return -1;
    return parse_stat(text, p, page_size);
}

static uint64_t keyed_value(const char *text, const char *key)
{
    const char *line = text;
    size_t length = strlen(key);
    while (*line) {
        if (!strncmp(line, key, length)) {
            char *end;
            errno = 0;
            unsigned long long value = strtoull(line + length, &end, 10);
            return errno || end == line + length ? 0 : value;
        }
        line = strchr(line, '\n');
        if (!line) break;
        ++line;
    }
    return 0;
}

/* A PID can disappear or be reused between any two reads. Validate start time
 * again after metadata collection; discard inconsistent records entirely. */
static int collect_process(pid_t pid, Process *p, const Snapshot *s)
{
    memset(p, 0, sizeof *p); p->pid = pid;
    if (read_stat(pid, p, s->page_size)) return -1;
    char path[128], text[TEXT_CAP];
    snprintf(path, sizeof path, "/proc/%ld", (long)pid);
    struct stat metadata;
    if (stat(path, &metadata)) return -1;
    p->uid = metadata.st_uid;
    struct passwd *owner = getpwuid(p->uid);
    if (owner) snprintf(p->owner, sizeof p->owner, "%s", owner->pw_name);
    else snprintf(p->owner, sizeof p->owner, "%lu", (unsigned long)p->uid);
    sanitize(p->owner);
    snprintf(path, sizeof path, "/proc/%ld/cmdline", (long)pid);
    read_text(path, p->command, sizeof p->command, true); sanitize(p->command);
    if (!p->command[0]) snprintf(p->command, sizeof p->command, "[%s]", p->name);
    snprintf(path, sizeof path, "/proc/%ld/io", (long)pid);
    p->io_status = read_text(path, text, sizeof text, false);
    p->read_bytes = keyed_value(text, "read_bytes:");
    p->write_bytes = keyed_value(text, "write_bytes:");
    p->sampled_at = monotonic_seconds();
    p->age = s->uptime - (double)p->start_ticks / (double)s->clock_ticks;
    if (p->age < 0) p->age = 0;
    Process verify = {0};
    return read_stat(pid, &verify, s->page_size) || verify.start_ticks != p->start_ticks ? -1 : 0;
}

int snapshot_collect(Snapshot *s)
{
    *s = (Snapshot){0};
    double beginning = monotonic_seconds();
    s->clock_ticks = sysconf(_SC_CLK_TCK); s->page_size = sysconf(_SC_PAGESIZE);
    s->cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (s->clock_ticks <= 0 || s->page_size <= 0) { errno = EINVAL; return -1; }
    char text[TEXT_CAP];
    if (read_text("/proc/meminfo", text, sizeof text, false) != VALUE_OK) return -1;
    s->memory_total = keyed_value(text, "MemTotal:") * 1024;
    s->memory_available = keyed_value(text, "MemAvailable:") * 1024;
    if (!s->memory_total) { errno = EINVAL; return -1; }
    if (read_text("/proc/uptime", text, sizeof text, false) != VALUE_OK || sscanf(text, "%lf", &s->uptime) != 1) return -1;
    DIR *directory = opendir("/proc");
    if (!directory) return -1;
    size_t capacity = 256;
    s->items = calloc(capacity, sizeof *s->items);
    if (!s->items) { closedir(directory); return -1; }
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        char *end;
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        errno = 0;
        long pid = strtol(entry->d_name, &end, 10);
        if (errno || *end || pid <= 0 || pid > INT_MAX) continue;
        if (s->count == PROCESS_CAP) { closedir(directory); snapshot_free(s); errno = EOVERFLOW; return -1; }
        if (s->count == capacity) {
            capacity *= 2;
            Process *grown = realloc(s->items, capacity * sizeof *grown);
            if (!grown) { closedir(directory); snapshot_free(s); return -1; }
            s->items = grown;
        }
        if (!collect_process((pid_t)pid, &s->items[s->count], s)) ++s->count;
    }
    closedir(directory);
    s->collected_at = (double)time(NULL);
    s->duration = monotonic_seconds() - beginning;
    return 0;
}

void snapshot_free(Snapshot *s) { free(s->items); *s = (Snapshot){0}; }

static void detail_file(pid_t pid, const char *name, char *out, size_t capacity)
{
    char path[128];
    snprintf(path, sizeof path, "/proc/%ld/%s", (long)pid, name);
    ValueStatus status = read_text(path, out, capacity, false);
    if (status != VALUE_OK) snprintf(out, capacity, "[%s]", value_status(status));
}

static void detail_link(pid_t pid, const char *name, char *out, size_t capacity)
{
    char path[128];
    snprintf(path, sizeof path, "/proc/%ld/%s", (long)pid, name);
    ssize_t count = readlink(path, out, capacity - 1);
    if (count < 0) snprintf(out, capacity, "[%s]", errno == EACCES ? "permission denied" : "unavailable");
    else out[count] = 0;
    sanitize(out);
}

/* Details are deliberately lazy and bounded. We never open target application
 * files: descriptor inspection reads symlink targets only. The identity guard
 * prevents callers from displaying a mixture of two processes after PID reuse. */
void details_collect(const Process *p, Details *d)
{
    memset(d, 0, sizeof *d);
    Process check = {0};
    long page_size = sysconf(_SC_PAGESIZE);
    if (read_stat(p->pid, &check, page_size) || check.start_ticks != p->start_ticks) return;
    detail_link(p->pid, "exe", d->executable, sizeof d->executable);
    detail_link(p->pid, "cwd", d->directory, sizeof d->directory);
    detail_file(p->pid, "status", d->status, sizeof d->status);
    detail_file(p->pid, "limits", d->limits, sizeof d->limits);
    detail_file(p->pid, "smaps_rollup", d->memory, sizeof d->memory);
    char path[128];
    snprintf(path, sizeof path, "/proc/%ld/fd", (long)p->pid);
    DIR *directory = opendir(path);
    if (!directory) snprintf(d->files, sizeof d->files, "[%s]", errno == EACCES ? "permission denied" : "unavailable");
    else {
        size_t used = 0; unsigned count = 0;
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (entry->d_name[0] == '.') continue;
            if (++count > 256 || sizeof d->files - used < TEXT_CAP + 300) {
                snprintf(d->files + used, sizeof d->files - used, "[truncated: descriptor budget]\n"); break;
            }
            char target[TEXT_CAP], linkname[300];
            snprintf(linkname, sizeof linkname, "fd/%s", entry->d_name);
            detail_link(p->pid, linkname, target, sizeof target);
            int written = snprintf(d->files + used, sizeof d->files - used, "%s -> %s\n", entry->d_name, target);
            if (written > 0) used += (size_t)written;
        }
        closedir(directory);
    }
    d->identity_valid = !read_stat(p->pid, &check, page_size) && check.start_ticks == p->start_ticks;
    /* Callers must discard all detail fields when identity_valid is false. */
}
