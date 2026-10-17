/* src/core/render_stuck.c -- see render_stuck.h. */
#include "render_stuck.h"

#include <string.h>

void render_stuck_init(RenderStuck *st)
{
    if (st) memset(st, 0, sizeof(*st));
}

static int stuck_step(RenderStuck *st, unsigned sid, int rows_dirty, int threshold,
                      int *streak_out)
{
    if (!st) return 0;

    if (sid != st->sid) {          /* another session: a new episode */
        st->sid = sid;
        st->streak = 0;
        st->fired = 0;
    }
    if (rows_dirty <= 0) {         /* a clean paint ends the episode */
        st->streak = 0;
        st->fired = 0;
        return 0;
    }

    if (st->streak < 0x3FFFFFFF) st->streak++;
    if (!st->fired && st->streak >= threshold) {
        st->fired = 1;
        if (streak_out) *streak_out = st->streak;
        return 1;
    }
    return 0;
}

int render_stuck_update(RenderStuck *st, unsigned sid, int rows_dirty,
                        int *paints_out)
{
    return stuck_step(st, sid, rows_dirty, RENDER_STUCK_DIRTY_PAINTS, paints_out);
}

int render_stuck_poll(RenderStuck *st, unsigned sid, int rows_dirty, int is_active,
                      int got_bytes, int *ticks_out)
{
    if (!st) return 0;
    if (is_active) rows_dirty = 0;      /* the paint path owns the active session */
    if (got_bytes) rows_dirty = 0;      /* output is arriving: busy, not stuck */
    return stuck_step(st, sid, rows_dirty, RENDER_STUCK_POLL_TICKS, ticks_out);
}
