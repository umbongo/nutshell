#include "test_framework.h"
#include "ns_hover.h"

/* ===========================================================================
 * ns_hover tests (Design-System Foundation, task 6) -- see
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 3 ("Hover tracker").
 * ===========================================================================
 */

int test_ns_hover_init_sets_both_ids_to_minus_one(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    ASSERT_EQ(h.hot_id, -1);
    ASSERT_EQ(h.pressed_id, -1);
    TEST_END();
}

int test_ns_hover_enter_reports_change_from_none(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    NsHoverChange c = ns_hover_move(&h, 5);
    ASSERT_TRUE(c.changed);
    ASSERT_EQ(c.old_id, -1);
    ASSERT_EQ(c.new_id, 5);
    ASSERT_EQ(h.hot_id, 5);
    TEST_END();
}

int test_ns_hover_move_within_same_element_unchanged(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    ns_hover_move(&h, 5);
    NsHoverChange c = ns_hover_move(&h, 5);
    ASSERT_TRUE(!c.changed);
    ASSERT_EQ(c.old_id, 5);
    ASSERT_EQ(c.new_id, 5);
    ASSERT_EQ(h.hot_id, 5);
    TEST_END();
}

int test_ns_hover_move_between_elements_reports_both_ids(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    ns_hover_move(&h, 5);
    NsHoverChange c = ns_hover_move(&h, 9);
    ASSERT_TRUE(c.changed);
    ASSERT_EQ(c.old_id, 5);
    ASSERT_EQ(c.new_id, 9);
    ASSERT_EQ(h.hot_id, 9);
    TEST_END();
}

int test_ns_hover_leave_reports_change_to_none(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    ns_hover_move(&h, 5);
    NsHoverChange c = ns_hover_leave(&h);
    ASSERT_TRUE(c.changed);
    ASSERT_EQ(c.old_id, 5);
    ASSERT_EQ(c.new_id, -1);
    ASSERT_EQ(h.hot_id, -1);
    TEST_END();
}

int test_ns_hover_leave_when_already_idle_unchanged(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    NsHoverChange c = ns_hover_leave(&h);
    ASSERT_TRUE(!c.changed);
    ASSERT_EQ(c.old_id, -1);
    ASSERT_EQ(c.new_id, -1);
    TEST_END();
}

int test_ns_hover_press_sets_pressed_id(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    ns_hover_move(&h, 5);
    NsHoverChange c = ns_hover_press(&h, 5);
    ASSERT_TRUE(c.changed);
    ASSERT_EQ(c.old_id, -1);
    ASSERT_EQ(c.new_id, 5);
    ASSERT_EQ(h.pressed_id, 5);
    /* hot id is untouched by press */
    ASSERT_EQ(h.hot_id, 5);
    TEST_END();
}

int test_ns_hover_release_clears_pressed_id(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    ns_hover_press(&h, 5);
    NsHoverChange c = ns_hover_release(&h);
    ASSERT_TRUE(c.changed);
    ASSERT_EQ(c.old_id, 5);
    ASSERT_EQ(c.new_id, -1);
    ASSERT_EQ(h.pressed_id, -1);
    TEST_END();
}

int test_ns_hover_release_when_nothing_pressed_unchanged(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    NsHoverChange c = ns_hover_release(&h);
    ASSERT_TRUE(!c.changed);
    ASSERT_EQ(c.old_id, -1);
    ASSERT_EQ(c.new_id, -1);
    TEST_END();
}

int test_ns_hover_state_for_rest_hover_pressed(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);

    /* Nothing hot or pressed: everything reads rest (0). */
    ASSERT_EQ(ns_hover_state_for(&h, 5), 0);
    ASSERT_EQ(ns_hover_state_for(&h, 9), 0);

    /* 5 is hot: 5 reads hover (1), 9 still rest. */
    ns_hover_move(&h, 5);
    ASSERT_EQ(ns_hover_state_for(&h, 5), 1);
    ASSERT_EQ(ns_hover_state_for(&h, 9), 0);

    /* 5 is pressed too: 5 reads pressed (2), takes priority over hover. */
    ns_hover_press(&h, 5);
    ASSERT_EQ(ns_hover_state_for(&h, 5), 2);

    /* Release: 5 falls back to hover since it is still hot. */
    ns_hover_release(&h);
    ASSERT_EQ(ns_hover_state_for(&h, 5), 1);

    /* Leave: 5 falls back to rest. */
    ns_hover_leave(&h);
    ASSERT_EQ(ns_hover_state_for(&h, 5), 0);
    TEST_END();
}

int test_ns_hover_state_for_negative_id_always_rest(void)
{
    TEST_BEGIN();
    NsHover h;
    ns_hover_init(&h);
    ns_hover_move(&h, 5);
    ns_hover_press(&h, 5);
    /* -1 ("nothing") never reads as hovered/pressed even though both ids
     * could theoretically be -1 (e.g. right after init). */
    ASSERT_EQ(ns_hover_state_for(&h, -1), 0);
    NsHover idle;
    ns_hover_init(&idle);
    ASSERT_EQ(ns_hover_state_for(&idle, -1), 0);
    TEST_END();
}

int test_ns_hover_state_for_null_hover_is_rest(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ns_hover_state_for(NULL, 5), 0);
    TEST_END();
}
