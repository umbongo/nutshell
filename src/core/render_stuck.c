/* src/core/render_stuck.c -- see render_stuck.h. */
#include "render_stuck.h"

#include <string.h>

void render_stuck_init(RenderStuck *st)
{
    if (st) memset(st, 0, sizeof(*st));
}

int render_stuck_update(RenderStuck *st, unsigned sid, int rows_dirty,
                        int *paints_out)
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
    if (!st->fired && st->streak >= RENDER_STUCK_DIRTY_PAINTS) {
        st->fired = 1;
        if (paints_out) *paints_out = st->streak;
        return 1;
    }
    return 0;
}
