#ifndef NUTSHELL_CORE_PAINT_POLL_H
#define NUTSHELL_CORE_PAINT_POLL_H

/*
 * The WM_TIMER poll's decision, per session and per tick: should this
 * session's output or dirty rows invalidate the window now?
 *
 * Only the active session's terminal is ever painted, so a background
 * session's dirty rows are never cleared by a paint. If they counted, a
 * connected background tab with one undrawn row would invalidate the
 * window every cooldown for as long as it lived. They do not count: a
 * background session never asks for a paint (it gets a full redraw through
 * on_tab_select() when it becomes active).
 *
 * The cooldown is per session: `since_paint_ms` is the time since this
 * session's own last poll paint, so one session's paints (or skips) can
 * never delay another's.
 */

typedef enum {
    PAINT_POLL_NONE = 0,       /* nothing to paint, or not on screen */
    PAINT_POLL_INVALIDATE,     /* invalidate the terminal now */
    PAINT_POLL_COOLDOWN_SKIP   /* wanted a paint, inside the cooldown */
} PaintPollAction;

/*
 *   is_active       the session whose terminal is on screen
 *   got_output      the transport poll returned data this tick
 *   has_dirty_rows  the terminal has dirty rows
 *   critical        the active session needs a full redraw (bypasses the
 *                   cooldown)
 *   since_paint_ms  milliseconds since this session's last poll paint
 *   cooldown_ms     the minimum gap between poll paints
 */
PaintPollAction paint_poll_decide(int is_active, int got_output,
                                  int has_dirty_rows, int critical,
                                  unsigned long since_paint_ms,
                                  unsigned long cooldown_ms);

#endif
