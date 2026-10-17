/* render.stuck_dirty: the counter behind the WARN for a row left dirty by paint after paint. */
#include "test_framework.h"
#include "render_stuck.h"

static int feed(RenderStuck *st, unsigned sid, int rows_dirty, int n)
{
    int fired = 0;
    for (int i = 0; i < n; i++)
        fired += render_stuck_update(st, sid, rows_dirty, NULL);
    return fired;
}

int test_render_stuck_threshold_is_a_named_constant(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(RENDER_STUCK_DIRTY_PAINTS >= 2);
    ASSERT_TRUE(RENDER_STUCK_DIRTY_PAINTS <= 600);
    TEST_END();
}

int test_render_stuck_does_not_fire_below_the_threshold(void)
{
    TEST_BEGIN();
    RenderStuck st;
    render_stuck_init(&st);
    ASSERT_EQ(feed(&st, 1, 3, RENDER_STUCK_DIRTY_PAINTS - 1), 0);
    TEST_END();
}

int test_render_stuck_fires_once_at_the_threshold_with_the_streak_length(void)
{
    TEST_BEGIN();
    RenderStuck st;
    render_stuck_init(&st);
    ASSERT_EQ(feed(&st, 1, 2, RENDER_STUCK_DIRTY_PAINTS - 1), 0);
    int paints = 0;
    ASSERT_EQ(render_stuck_update(&st, 1, 2, &paints), 1);
    ASSERT_EQ(paints, RENDER_STUCK_DIRTY_PAINTS);
    TEST_END();
}

int test_render_stuck_stays_quiet_for_the_rest_of_the_episode(void)
{
    TEST_BEGIN();
    RenderStuck st;
    render_stuck_init(&st);
    ASSERT_EQ(feed(&st, 1, 5, RENDER_STUCK_DIRTY_PAINTS), 1);
    ASSERT_EQ(feed(&st, 1, 5, 22000), 0);       /* the 22,000 no-op paints of 2026-10-14 */
    TEST_END();
}

int test_render_stuck_a_clean_paint_ends_the_episode_and_rearms(void)
{
    TEST_BEGIN();
    RenderStuck st;
    render_stuck_init(&st);
    ASSERT_EQ(feed(&st, 1, 1, RENDER_STUCK_DIRTY_PAINTS), 1);
    ASSERT_EQ(render_stuck_update(&st, 1, 0, NULL), 0);          /* clean */
    ASSERT_EQ(feed(&st, 1, 1, RENDER_STUCK_DIRTY_PAINTS - 1), 0);
    ASSERT_EQ(render_stuck_update(&st, 1, 1, NULL), 1);          /* a second episode */
    TEST_END();
}

int test_render_stuck_a_clean_paint_before_the_threshold_resets_the_count(void)
{
    TEST_BEGIN();
    RenderStuck st;
    render_stuck_init(&st);
    ASSERT_EQ(feed(&st, 1, 4, RENDER_STUCK_DIRTY_PAINTS - 1), 0);
    ASSERT_EQ(render_stuck_update(&st, 1, 0, NULL), 0);
    ASSERT_EQ(feed(&st, 1, 4, RENDER_STUCK_DIRTY_PAINTS - 1), 0);   /* would fire if the count had survived */
    TEST_END();
}

int test_render_stuck_switching_session_starts_a_new_episode(void)
{
    TEST_BEGIN();
    RenderStuck st;
    render_stuck_init(&st);
    ASSERT_EQ(feed(&st, 1, 2, RENDER_STUCK_DIRTY_PAINTS - 1), 0);
    ASSERT_EQ(render_stuck_update(&st, 2, 2, NULL), 0);          /* streak restarts at 1 for s2 */
    ASSERT_EQ(feed(&st, 2, 2, RENDER_STUCK_DIRTY_PAINTS - 2), 0);
    ASSERT_EQ(render_stuck_update(&st, 2, 2, NULL), 1);
    /* and back: s1 is a fresh episode too */
    ASSERT_EQ(render_stuck_update(&st, 1, 2, NULL), 0);
    TEST_END();
}

int test_render_stuck_the_number_of_dirty_rows_does_not_matter_only_that_one_is(void)
{
    TEST_BEGIN();
    RenderStuck st;
    render_stuck_init(&st);
    int fired = 0;
    for (int i = 0; i < RENDER_STUCK_DIRTY_PAINTS; i++)
        fired += render_stuck_update(&st, 7, (i % 2) ? 1 : 70, NULL);
    ASSERT_EQ(fired, 1);
    TEST_END();
}

int test_render_stuck_null_state_is_safe(void)
{
    TEST_BEGIN();
    ASSERT_EQ(render_stuck_update(NULL, 1, 1, NULL), 0);
    render_stuck_init(NULL);
    RenderStuck st;
    render_stuck_init(&st);
    ASSERT_EQ(render_stuck_update(&st, 0, 0, NULL), 0);          /* sid 0 = no session, clean */
    TEST_END();
}
