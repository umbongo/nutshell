#include "test_framework.h"
#include "stick_scroll.h"

/* --- stick_scroll_after_user tests --- */

int test_stick_scroll_after_user_scrolled_up(void) {
    TEST_BEGIN();
    /* Well short of the bottom -> released */
    ASSERT_EQ(stick_scroll_after_user(0, 500), 0);
    ASSERT_EQ(stick_scroll_after_user(100, 500), 0);
    TEST_END();
}

int test_stick_scroll_after_user_exactly_at_bottom(void) {
    TEST_BEGIN();
    ASSERT_EQ(stick_scroll_after_user(500, 500), 1);
    TEST_END();
}

int test_stick_scroll_after_user_past_bottom(void) {
    TEST_BEGIN();
    /* Shouldn't normally happen (caller clamps first), but stick logic
     * treats "beyond max" as still stuck. */
    ASSERT_EQ(stick_scroll_after_user(600, 500), 1);
    TEST_END();
}

int test_stick_scroll_after_user_max_scroll_zero_or_negative(void) {
    TEST_BEGIN();
    /* Content shorter than the viewport -> always "at bottom" */
    ASSERT_EQ(stick_scroll_after_user(0, 0), 1);
    ASSERT_EQ(stick_scroll_after_user(0, -10), 1);
    ASSERT_EQ(stick_scroll_after_user(50, -10), 1);
    TEST_END();
}

int test_stick_scroll_after_user_negative_scroll_y(void) {
    TEST_BEGIN();
    /* Negative position (shouldn't happen post-clamp) is still short of
     * a positive max -> released. */
    ASSERT_EQ(stick_scroll_after_user(-5, 500), 0);
    TEST_END();
}

/* --- stick_scroll_on_layout tests --- */

int test_stick_scroll_on_layout_stuck_content_grows(void) {
    TEST_BEGIN();
    /* Stuck, list grew (new max) -> jump to the new bottom */
    ASSERT_EQ(stick_scroll_on_layout(1, 500, 500), 500);
    ASSERT_EQ(stick_scroll_on_layout(1, 500, 800), 800);
    TEST_END();
}

int test_stick_scroll_on_layout_not_stuck_content_grows_unchanged(void) {
    TEST_BEGIN();
    /* Not stuck, list grew but current position still valid -> unchanged */
    ASSERT_EQ(stick_scroll_on_layout(0, 200, 800), 200);
    TEST_END();
}

int test_stick_scroll_on_layout_not_stuck_content_shrinks_clamped(void) {
    TEST_BEGIN();
    /* Not stuck, list shrank below the current position -> clamped down */
    ASSERT_EQ(stick_scroll_on_layout(0, 700, 400), 400);
    TEST_END();
}

int test_stick_scroll_on_layout_negative_scroll_y_clamped(void) {
    TEST_BEGIN();
    ASSERT_EQ(stick_scroll_on_layout(0, -20, 500), 0);
    TEST_END();
}

int test_stick_scroll_on_layout_max_scroll_zero_or_negative(void) {
    TEST_BEGIN();
    /* Content fits viewport: stuck or not, position collapses to 0 */
    ASSERT_EQ(stick_scroll_on_layout(1, 0, 0), 0);
    ASSERT_EQ(stick_scroll_on_layout(0, 50, -10), 0);
    TEST_END();
}

int test_stick_scroll_on_layout_stuck_content_shrinks(void) {
    TEST_BEGIN();
    /* Stuck: always tracks the new max regardless of old position */
    ASSERT_EQ(stick_scroll_on_layout(1, 700, 400), 400);
    TEST_END();
}
