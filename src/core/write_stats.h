#ifndef NUTSHELL_WRITE_STATS_H
#define NUTSHELL_WRITE_STATS_H

#include <stddef.h>

/*
 * Bookkeeping for the user's keystroke and paste writes to a session's
 * transport (issue.term: last_write_rc, write_fails, since_key_write_ms; the
 * term.write_fail WARN). Pure: ticks are passed in as unsigned long
 * (GetTickCount() milliseconds; 0 means "never").
 */

typedef struct {
    long          last_rc;          /* return code of the last write call */
    unsigned      fail_count;       /* write CALLS that failed or were short */
    unsigned long last_write_tick;  /* tick of the last write call; 0 = never */
} WriteStats;

void write_stats_init(WriteStats *w);

/* 1 when a write returning rc for `want` bytes failed (rc < 0) or was short
 * (rc < want). */
int write_is_failure(int rc, size_t want);

/* Record one write call. Returns 1 if it failed (the caller logs the WARN). */
int write_stats_note(WriteStats *w, int rc, size_t want, unsigned long now_tick);

/* A paste is many one-byte writes; aggregate them so a whole call counts as at
 * most one failure. rc is the first failing return code, else the last one. */
typedef struct {
    int      rc;
    unsigned bad;
} WriteAgg;

void write_agg_init(WriteAgg *a);                       /* rc = 1, bad = 0 */
void write_agg_add(WriteAgg *a, int rc, size_t want);
/* Record the aggregate as one call: failed when any byte write was. Returns 1
 * if it failed. */
int  write_stats_note_agg(WriteStats *w, const WriteAgg *a, unsigned long now_tick);

/* Milliseconds from `then` to `now`, wrap-safe; -1 when then == 0 (never). */
long write_ms_since(unsigned long now, unsigned long then);

#endif
