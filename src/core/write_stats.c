#include "write_stats.h"

void write_stats_init(WriteStats *w)
{
    if (!w) return;
    w->last_rc = 0;
    w->fail_count = 0;
    w->last_write_tick = 0;
}

int write_is_failure(int rc, size_t want)
{
    if (rc < 0) return 1;
    return (size_t)rc < want;
}

int write_stats_note(WriteStats *w, int rc, size_t want, unsigned long now_tick)
{
    if (!w) return 0;
    int bad = write_is_failure(rc, want);
    w->last_rc = rc;
    w->last_write_tick = now_tick;
    if (bad) w->fail_count++;
    return bad;
}

void write_agg_init(WriteAgg *a)
{
    if (!a) return;
    a->rc = 1;
    a->bad = 0;
}

void write_agg_add(WriteAgg *a, int rc, size_t want)
{
    if (!a) return;
    if (write_is_failure(rc, want)) {
        if (a->bad == 0) a->rc = rc;
        a->bad++;
    } else if (a->bad == 0) {
        a->rc = rc;
    }
}

int write_stats_note_agg(WriteStats *w, const WriteAgg *a, unsigned long now_tick)
{
    if (!w || !a) return 0;
    w->last_rc = a->rc;
    w->last_write_tick = now_tick;
    if (a->bad) { w->fail_count++; return 1; }
    return 0;
}

long write_ms_since(unsigned long now, unsigned long then)
{
    if (then == 0ul) return -1L;
    return (long)((now - then) & 0xFFFFFFFFul);
}
