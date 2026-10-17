/* src/core/render_stuck.h -- "a row stayed dirty across many paints".
 *
 * The 2026-10-14 background tab drove 22,000 no-op paints: every WM_PAINT
 * left a row dirty, so the 60 fps invalidate loop never stopped. This is the
 * counter behind the render.stuck_dirty WARN (spec 2026-10-17-debug-logging-
 * design.md, section 2) that makes the next occurrence announce itself.
 *
 * Pure: renderer_draw() (src/ui/renderer.c) feeds it once per paint with how
 * many visible rows its paint left dirty; it says when to log. One line per
 * episode: it fires on the paint that makes the streak reach
 * RENDER_STUCK_DIRTY_PAINTS, stays quiet while the streak goes on, and
 * rearms after a paint that leaves nothing dirty (or a different session).
 */
#ifndef NUTSHELL_CORE_RENDER_STUCK_H
#define NUTSHELL_CORE_RENDER_STUCK_H

/* Consecutive paints that leave a row dirty before render.stuck_dirty is
 * written. Around half a second of a 60 fps paint loop. */
#define RENDER_STUCK_DIRTY_PAINTS 30

typedef struct {
    unsigned sid;       /* the session being painted, 0 = none yet            */
    int      streak;    /* consecutive paints that left at least one row dirty */
    int      fired;     /* this episode has already been reported              */
} RenderStuck;

void render_stuck_init(RenderStuck *st);

/* One paint of session `sid` that left `rows_dirty` visible rows dirty.
 * Returns 1 exactly once per episode, on the paint where the streak reaches
 * RENDER_STUCK_DIRTY_PAINTS; *paints_out (optional) is then the streak
 * length. A paint with no dirty row left, or one for another session, ends
 * the episode. NULL st returns 0. */
int render_stuck_update(RenderStuck *st, unsigned sid, int rows_dirty,
                        int *paints_out);

#endif /* NUTSHELL_CORE_RENDER_STUCK_H */
