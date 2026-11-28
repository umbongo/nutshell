#include "test_framework.h"
#include "paint_poll.h"

#define CD 16ul

/* Active session, output arrived, cooldown over: paint. */
int test_paint_poll_active_with_output(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paint_poll_decide(1, 1, 0, 0, 100, CD), PAINT_POLL_INVALIDATE);
    TEST_END();
}

/* Active session whose transport just closed: no output, rows dirty from the
 * "[Connection Closed]" banner, so the last paint still happens. */
int test_paint_poll_active_disconnected_dirty(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paint_poll_decide(1, 0, 1, 0, 100, CD), PAINT_POLL_INVALIDATE);
    TEST_END();
}

/* Active, nothing arrived, nothing dirty: nothing to do. */
int test_paint_poll_active_idle(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paint_poll_decide(1, 0, 0, 0, 100, CD), PAINT_POLL_NONE);
    TEST_END();
}

/* A background connected session with dirty rows and fresh output never
 * asks for a paint, whatever the cooldown says. */
int test_paint_poll_background_connected_dirty(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paint_poll_decide(0, 0, 1, 0, 100000, CD), PAINT_POLL_NONE);
    ASSERT_EQ(paint_poll_decide(0, 1, 1, 0, 100000, CD), PAINT_POLL_NONE);
    ASSERT_EQ(paint_poll_decide(0, 1, 1, 0, 0, CD), PAINT_POLL_NONE);
    TEST_END();
}

/* A background session that just disconnected (rows dirty from the banner,
 * no output) stays quiet, tick after tick. */
int test_paint_poll_background_disconnected_dirty(void)
{
    TEST_BEGIN();
    for (unsigned long t = 0; t < 1000; t += 16)
        ASSERT_EQ(paint_poll_decide(0, 0, 1, 0, t, CD), PAINT_POLL_NONE);
    TEST_END();
}

/* A background session cannot claim a critical full redraw. */
int test_paint_poll_background_critical_ignored(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paint_poll_decide(0, 1, 1, 1, 0, CD), PAINT_POLL_NONE);
    TEST_END();
}

/* Cooldown edges for the active session. */
int test_paint_poll_cooldown_edges(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paint_poll_decide(1, 1, 1, 0, 0, CD), PAINT_POLL_COOLDOWN_SKIP);
    ASSERT_EQ(paint_poll_decide(1, 1, 1, 0, CD - 1, CD), PAINT_POLL_COOLDOWN_SKIP);
    ASSERT_EQ(paint_poll_decide(1, 1, 1, 0, CD, CD), PAINT_POLL_INVALIDATE);
    ASSERT_EQ(paint_poll_decide(1, 0, 1, 0, CD + 1, CD), PAINT_POLL_INVALIDATE);
    /* Nothing to paint is NONE even inside the cooldown. */
    ASSERT_EQ(paint_poll_decide(1, 0, 0, 0, 0, CD), PAINT_POLL_NONE);
    TEST_END();
}

/* A critical redraw bypasses the cooldown. */
int test_paint_poll_critical_bypasses_cooldown(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paint_poll_decide(1, 0, 1, 1, 0, CD), PAINT_POLL_INVALIDATE);
    ASSERT_EQ(paint_poll_decide(1, 1, 0, 1, 3, CD), PAINT_POLL_INVALIDATE);
    TEST_END();
}

/* The decision depends only on the session's own since-paint time, so one
 * session inside its cooldown never holds back another that is outside its
 * own. */
int test_paint_poll_independent_cooldowns(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paint_poll_decide(1, 1, 1, 0, 2, CD), PAINT_POLL_COOLDOWN_SKIP);
    ASSERT_EQ(paint_poll_decide(1, 1, 1, 0, 500, CD), PAINT_POLL_INVALIDATE);
    TEST_END();
}
