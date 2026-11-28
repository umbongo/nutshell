#include "paint_poll.h"

PaintPollAction paint_poll_decide(int is_active, int got_output,
                                  int has_dirty_rows, int critical,
                                  unsigned long since_paint_ms,
                                  unsigned long cooldown_ms)
{
    if (!is_active) return PAINT_POLL_NONE;
    if (!got_output && !has_dirty_rows) return PAINT_POLL_NONE;
    if (critical || since_paint_ms >= cooldown_ms) return PAINT_POLL_INVALIDATE;
    return PAINT_POLL_COOLDOWN_SKIP;
}
