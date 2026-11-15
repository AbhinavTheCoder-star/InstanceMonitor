#include "instmon.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const Process *snapshot_find(const Snapshot *s, pid_t pid, uint64_t start)
{
    for (size_t i = 0; i < s->count; ++i)
        if (s->items[i].pid == pid && s->items[i].start_ticks == start) return &s->items[i];
    return NULL;
}

/* Rates need two observations of the same lifetime. A missing baseline is not
 * zero activity; it remains explicitly unavailable until the next refresh. */
void snapshot_rates(Snapshot *current, const Snapshot *previous)
{
    for (size_t i = 0; i < current->count; ++i) {
        Process *p = &current->items[i];
        const Process *old = snapshot_find(previous, p->pid, p->start_ticks);
        if (!old) continue;
        double elapsed = p->sampled_at - old->sampled_at;
        if (elapsed <= 0 || p->cpu_ticks < old->cpu_ticks) continue;
        p->cpu_percent = (double)(p->cpu_ticks - old->cpu_ticks) / (double)current->clock_ticks / elapsed * 100;
        p->rates_valid = true;
        p->read_rate = p->write_rate = -1;
        if (p->io_status == VALUE_OK && old->io_status == VALUE_OK &&
            p->read_bytes >= old->read_bytes && p->write_bytes >= old->write_bytes) {
            p->read_rate = (double)(p->read_bytes - old->read_bytes) / elapsed;
            p->write_rate = (double)(p->write_bytes - old->write_bytes) / elapsed;
        }
    }
}

/* qsort has no portable context argument. These values are private to this
 * single-threaded module and are set immediately before sorting. */
static SortKey sort_key;
static bool reversed;
static int compare_process(const void *left, const void *right)
{
    const Process *a = left, *b = right;
    int result = 0;
    if (sort_key == SORT_RAM) result = (b->rss > a->rss) - (b->rss < a->rss);
    if (sort_key == SORT_CPU) result = (b->cpu_percent > a->cpu_percent) - (b->cpu_percent < a->cpu_percent);
    if (sort_key == SORT_NAME) result = strcasecmp(a->name, b->name);
    if (sort_key == SORT_PID || result == 0) result = (a->pid > b->pid) - (a->pid < b->pid);
    return reversed ? -result : result;
}

void snapshot_query(Snapshot *s, const Options *o)
{
    size_t kept = 0;
    for (size_t i = 0; i < s->count; ++i) {
        Process *p = &s->items[i];
        if (o->inspect && p->pid != o->pid) continue;
        if (o->user_set && p->uid != o->uid) continue;
        if (o->filter[0] && !strcasestr(p->name, o->filter) && !strcasestr(p->command, o->filter)) continue;
        s->items[kept++] = *p;
    }
    s->count = kept; sort_key = o->sort; reversed = o->reverse;
    if (s->count > 1) qsort(s->items, s->count, sizeof *s->items, compare_process);
}
