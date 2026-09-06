#include "test_framework.h"
#include "ns_motion.h"
#include <limits.h>

/* ===========================================================================
 * ns_motion tests (Design-System Foundation, task 8) -- see
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 4 ("Motion tokens").
 * ===========================================================================
 */

int test_ns_ease_endpoints(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ns_ease(0.0) == 0.0);
    ASSERT_TRUE(ns_ease(1.0) == 1.0);
    TEST_END();
}

int test_ns_ease_monotonic_over_unit_interval(void)
{
    TEST_BEGIN();
    double prev = ns_ease(0.0);
    for (int i = 1; i <= 20; i++) {
        double t = (double)i * 0.05;
        double v = ns_ease(t);
        ASSERT_TRUE(v >= prev);
        prev = v;
    }
    TEST_END();
}

int test_ns_ease_clamps_outside_unit_interval(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ns_ease(-0.5) == 0.0);
    ASSERT_TRUE(ns_ease(1.5) == 1.0);
    TEST_END();
}

int test_ns_anim_progress_increases_with_time(void)
{
    TEST_BEGIN();
    NsAnim a;
    ns_anim_start(&a, 1000UL, 200);
    NsAnimStep s0 = ns_anim_progress(&a, 1000UL, 0);
    NsAnimStep s1 = ns_anim_progress(&a, 1050UL, 0);
    NsAnimStep s2 = ns_anim_progress(&a, 1150UL, 0);
    ASSERT_TRUE(s0.t == 0.0);
    ASSERT_TRUE(s1.t > s0.t);
    ASSERT_TRUE(s2.t > s1.t);
    TEST_END();
}

int test_ns_anim_progress_clamps_at_one(void)
{
    TEST_BEGIN();
    NsAnim a;
    ns_anim_start(&a, 1000UL, 200);
    NsAnimStep s = ns_anim_progress(&a, 5000UL, 0);
    ASSERT_TRUE(s.t == 1.0);
    TEST_END();
}

int test_ns_anim_progress_done_fires_exactly_once(void)
{
    TEST_BEGIN();
    NsAnim a;
    ns_anim_start(&a, 1000UL, 200);
    NsAnimStep s1 = ns_anim_progress(&a, 1200UL, 0); /* exactly at the end */
    NsAnimStep s2 = ns_anim_progress(&a, 1300UL, 0); /* still past the end */
    ASSERT_TRUE(s1.done == 1);
    ASSERT_TRUE(s2.done == 0);
    ASSERT_TRUE(s2.t == 1.0);
    TEST_END();
}

int test_ns_anim_progress_reduced_motion_snaps_on_first_call(void)
{
    TEST_BEGIN();
    NsAnim a;
    ns_anim_start(&a, 1000UL, 200);
    NsAnimStep s1 = ns_anim_progress(&a, 1000UL, 1);
    NsAnimStep s2 = ns_anim_progress(&a, 1010UL, 1);
    ASSERT_TRUE(s1.t == 1.0);
    ASSERT_TRUE(s1.done == 1);
    ASSERT_TRUE(s2.t == 1.0);
    ASSERT_TRUE(s2.done == 0);
    TEST_END();
}

int test_ns_anim_list_add_and_get(void)
{
    TEST_BEGIN();
    NsAnimList l;
    l.count = 0;
    int idx = ns_anim_list_add(&l, 7, 1000UL, 200);
    ASSERT_TRUE(idx >= 0);
    NsAnimStep out;
    int found = ns_anim_list_get(&l, 7, 1000UL, 0, &out);
    ASSERT_TRUE(found == 1);
    ASSERT_TRUE(out.t == 0.0);
    int missing = ns_anim_list_get(&l, 99, 1000UL, 0, &out);
    ASSERT_TRUE(missing == 0);
    ASSERT_TRUE(out.t == 1.0);
    TEST_END();
}

int test_ns_anim_list_add_replaces_existing_id(void)
{
    TEST_BEGIN();
    NsAnimList l;
    l.count = 0;
    ns_anim_list_add(&l, 1, 1000UL, 200);
    /* Advance partway, then restart the same id -- should not accumulate
     * a second entry, and progress should measure from the new start. */
    ns_anim_list_add(&l, 1, 1050UL, 200);
    ASSERT_EQ(ns_anim_list_active(&l), 1);
    NsAnimStep out;
    ns_anim_list_get(&l, 1, 1050UL, 0, &out);
    ASSERT_TRUE(out.t == 0.0);
    TEST_END();
}

int test_ns_anim_list_add_full_returns_minus_one(void)
{
    TEST_BEGIN();
    NsAnimList l;
    l.count = 0;
    for (int i = 0; i < NS_ANIM_LIST_MAX; i++) {
        int idx = ns_anim_list_add(&l, i, 1000UL, 200);
        ASSERT_TRUE(idx >= 0);
    }
    int overflow = ns_anim_list_add(&l, 999, 1000UL, 200);
    ASSERT_EQ(overflow, -1);
    ASSERT_EQ(ns_anim_list_active(&l), NS_ANIM_LIST_MAX);
    TEST_END();
}

int test_ns_anim_list_step_removes_finished_keeps_active(void)
{
    TEST_BEGIN();
    NsAnimList l;
    l.count = 0;
    ns_anim_list_add(&l, 1, 1000UL, 100);  /* finishes at tick 1100 */
    ns_anim_list_add(&l, 2, 1000UL, 1000); /* still running at tick 1100 */
    int active = ns_anim_list_step(&l, 1100UL, 0);
    ASSERT_EQ(active, 1);
    NsAnimStep out;
    ASSERT_EQ(ns_anim_list_get(&l, 1, 1100UL, 0, &out), 0); /* removed */
    ASSERT_EQ(ns_anim_list_get(&l, 2, 1100UL, 0, &out), 1); /* still tracked */
    TEST_END();
}

int test_ns_anim_list_step_reduced_motion_finishes_immediately(void)
{
    TEST_BEGIN();
    NsAnimList l;
    l.count = 0;
    ns_anim_list_add(&l, 1, 1000UL, 200);
    int active = ns_anim_list_step(&l, 1000UL, 1);
    ASSERT_EQ(active, 0);
    ASSERT_EQ(ns_anim_list_active(&l), 0);
    TEST_END();
}

int test_ns_anim_progress_tick_wraparound_near_ulong_max(void)
{
    TEST_BEGIN();
    NsAnim a;
    unsigned long near_max = (unsigned long)(ULONG_MAX - 50UL);
    ns_anim_start(&a, near_max, 200);
    /* now_tick has wrapped around past ULONG_MAX back to 50 -- 100ms
     * really elapsed (50 to reach the wrap, 50 more past it). */
    unsigned long wrapped_now = 50UL;
    NsAnimStep s = ns_anim_progress(&a, wrapped_now, 0);
    ASSERT_TRUE(s.t > 0.0);
    ASSERT_TRUE(s.t < 1.0);
    TEST_END();
}
