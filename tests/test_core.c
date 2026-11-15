#include "instmon.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void stat_parsing(void)
{
    /* Fields 4..24, including signed tty/group fields we do not consume. */
    char text[] = "42 (a tricky ) name) S 1 2 3 0 -1 0 0 0 0 0 100 50 0 0 20 -5 3 0 12345 8192 2";
    Process p = {0};
    assert(parse_stat(text, &p, 4096) == 0);
    assert(!strcmp(p.name, "a tricky ) name"));
    assert(p.cpu_ticks == 150 && p.start_ticks == 12345);
    assert(p.rss == 8192 && p.virtual_bytes == 8192);
    assert(p.nice == -5 && p.threads == 3 && p.parent == 1);
    char malformed[] = "42 (broken) S 1";
    assert(parse_stat(malformed, &p, 4096) == -1);
    char overflow[] = "42 (x) S 1 2 3 0 -1 0 0 0 0 0 18446744073709551615 1 0 0 20 0 1 0 2 3 1";
    assert(parse_stat(overflow, &p, 4096) == -1);
}

static void rates_and_identity(void)
{
    Process old = {.pid=42, .start_ticks=10, .sampled_at=1, .cpu_ticks=100,
                   .read_bytes=100, .write_bytes=200, .io_status=VALUE_OK};
    Process next = old;
    next.sampled_at=3; next.cpu_ticks=400; next.read_bytes=300;
    Snapshot a = {.items=&old, .count=1, .clock_ticks=100};
    Snapshot b = {.items=&next, .count=1, .clock_ticks=100};
    snapshot_rates(&b, &a);
    assert(next.rates_valid && fabs(next.cpu_percent-150) < .001);
    assert(fabs(next.read_rate-100) < .001);
    next.rates_valid=false; next.start_ticks=11;
    snapshot_rates(&b, &a); assert(!next.rates_valid);
    next.start_ticks=10; next.cpu_ticks=1;
    snapshot_rates(&b, &a); assert(!next.rates_valid);
}

int main(void)
{
    stat_parsing(); rates_and_identity();
    char hostile[] = "hello\033[31m\n";
    sanitize(hostile); assert(!strchr(hostile, '\033') && !strchr(hostile, '\n'));
    puts("Core tests passed");
    return 0;
}
