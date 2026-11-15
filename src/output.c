#include "instmon.h"
#include <inttypes.h>
#include <stdio.h>

/* A small streaming writer avoids another dependency. Every string, including
 * file contents, passes through this function; JSON never receives raw text. */
static void json_string(const char *text)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
        else if (*p < 32 || *p >= 127) printf("\\u%04x", (unsigned)*p);
        else putchar(*p);
    }
    putchar('"');
}

static void json_details(const Details *d)
{
    printf(",\"details\":{\"identity_valid\":%s", d->identity_valid ? "true" : "false");
    if (d->identity_valid) {
        const char *keys[] = {"executable", "directory", "status", "limits", "memory", "files"};
        const char *values[] = {d->executable, d->directory, d->status, d->limits, d->memory, d->files};
        for (size_t i = 0; i < 6; ++i) { printf(",\"%s\":", keys[i]); json_string(values[i]); }
    }
    putchar('}');
}

void output_snapshot(const Snapshot *s, const Options *o)
{
    if (o->json) {
        printf("{\"schema_version\":1,\"sampled_at_unix\":%.0f,\"collection_seconds\":%.6f,"
               "\"units\":{\"memory\":\"bytes\",\"rates\":\"bytes/second\",\"cpu\":\"percent of one logical CPU\"},"
               "\"memory_total_bytes\":%" PRIu64 ",\"processes\":[", s->collected_at, s->duration, s->memory_total);
    } else {
        printf("Host RAM: %.1f MiB total, %.1f MiB available | %zu processes | collection %.3fs\n",
               (double)s->memory_total / 1048576, (double)s->memory_available / 1048576, s->count, s->duration);
        puts("    PID OWNER            STATE    CPU%    RAM MiB   RAM% THREADS  AGE(s) NAME");
    }
    for (size_t i = 0; i < s->count; ++i) {
        const Process *p = &s->items[i];
        if (o->json) {
            if (i) putchar(',');
            printf("{\"pid\":%ld,\"parent_pid\":%ld,\"start_ticks\":%" PRIu64 ",\"uid\":%lu,\"name\":",
                   (long)p->pid, (long)p->parent, p->start_ticks, (unsigned long)p->uid);
            json_string(p->name); printf(",\"command\":"); json_string(p->command);
            printf(",\"state\":\"%c\",\"rss_bytes\":%" PRIu64 ",\"virtual_bytes\":%" PRIu64
                   ",\"threads\":%" PRIu64 ",\"age_seconds\":%.2f,\"cpu_percent\":",
                   p->state, p->rss, p->virtual_bytes, p->threads, p->age);
            if (p->rates_valid) printf("%.2f", p->cpu_percent); else printf("null");
            printf(",\"cpu_status\":\"%s\",\"io_status\":", p->rates_valid ? "available" : "warming up");
            json_string(value_status(p->io_status));
            printf(",\"read_bytes\":");
            if (p->io_status == VALUE_OK) printf("%" PRIu64, p->read_bytes); else printf("null");
            printf(",\"write_bytes\":");
            if (p->io_status == VALUE_OK) printf("%" PRIu64, p->write_bytes); else printf("null");
            if (o->inspect) { Details d; details_collect(p, &d); json_details(&d); }
            putchar('}');
        } else {
            char cpu[32] = "warming";
            if (p->rates_valid) snprintf(cpu, sizeof cpu, "%.1f", p->cpu_percent);
            printf("%7ld %-16.16s %c %11s %10.2f %6.2f %7" PRIu64 " %7.0f %s\n",
                   (long)p->pid, p->owner, p->state, cpu, (double)p->rss / 1048576,
                   (double)p->rss / (double)s->memory_total * 100, p->threads, p->age, p->name);
            if (o->inspect) {
                Details d; details_collect(p, &d);
                if (!d.identity_valid) { puts("Process exited or identity changed during inspection."); continue; }
                printf("\nCommand: %s\nExecutable: %s\nDirectory: %s\nParent PID: %ld\n"
                       "Virtual memory: %.2f MiB\nPriority: %ld  Nice: %ld\nI/O: %s\n",
                       p->command, d.executable, d.directory, (long)p->parent,
                       (double)p->virtual_bytes / 1048576, p->priority, p->nice, value_status(p->io_status));
                if (p->io_status == VALUE_OK) printf("Storage read: %" PRIu64 " bytes; write: %" PRIu64 " bytes\n", p->read_bytes, p->write_bytes);
                printf("\nStatus\n%s\nMemory (PSS when accessible)\n%s\nLimits\n%s\nDescriptors\n%s\n",
                       d.status, d.memory, d.limits, d.files);
            }
        }
    }
    if (o->json) puts("]}");
}
