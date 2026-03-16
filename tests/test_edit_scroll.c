#include "test_framework.h"
#include "edit_scroll.h"

/* --- edit_scroll_visible_lines tests --- */

int test_edit_scroll_visible_basic(void) {
    TEST_BEGIN();
    /* 200px edit, 16px line height -> 12 visible lines */
    ASSERT_EQ(edit_scroll_visible_lines(200, 16), 12);
    /* 160px edit, 16px line height -> exactly 10 */
    ASSERT_EQ(edit_scroll_visible_lines(160, 16), 10);
    TEST_END();
}

int test_edit_scroll_visible_rounding(void) {
    TEST_BEGIN();
    /* 170px / 16px = 10.625 -> 10 (floor) */
    ASSERT_EQ(edit_scroll_visible_lines(170, 16), 10);
    /* 175px / 16px = 10.9375 -> 10 (floor) */
    ASSERT_EQ(edit_scroll_visible_lines(175, 16), 10);
    TEST_END();
}

int test_edit_scroll_visible_zero(void) {
    TEST_BEGIN();
    ASSERT_EQ(edit_scroll_visible_lines(0, 16), 0);
    ASSERT_EQ(edit_scroll_visible_lines(200, 0), 0);
    ASSERT_EQ(edit_scroll_visible_lines(0, 0), 0);
    TEST_END();
}

int test_edit_scroll_visible_small(void) {
    TEST_BEGIN();
    /* Edit smaller than one line */
    ASSERT_EQ(edit_scroll_visible_lines(10, 16), 0);
    /* Exactly one line */
    ASSERT_EQ(edit_scroll_visible_lines(16, 16), 1);
    TEST_END();
}

/* --- edit_scroll_params tests --- */

int test_edit_scroll_params_basic(void) {
    TEST_BEGIN();
    int nMin, nMax, nPage, nPos;
    /* 50 lines total, first visible=10, 200px edit, 16px lines -> 12 visible */
    edit_scroll_params(50, 10, 200, 16, &nMin, &nMax, &nPage, &nPos);
    ASSERT_EQ(nMin, 0);
    ASSERT_EQ(nMax, 49);
    ASSERT_EQ(nPage, 12);
    ASSERT_EQ(nPos, 10);
    TEST_END();
}

int test_edit_scroll_params_no_scroll(void) {
    TEST_BEGIN();
    int nMin, nMax, nPage, nPos;
    /* 5 lines total, 200px edit, 16px lines -> 12 visible; no scrolling needed */
    edit_scroll_params(5, 0, 200, 16, &nMin, &nMax, &nPage, &nPos);
    ASSERT_EQ(nMin, 0);
    ASSERT_EQ(nMax, 4);
    ASSERT_EQ(nPage, 12);
    ASSERT_EQ(nPos, 0);
    TEST_END();
}

int test_edit_scroll_params_zero_lines(void) {
    TEST_BEGIN();
    int nMin, nMax, nPage, nPos;
    edit_scroll_params(0, 0, 200, 16, &nMin, &nMax, &nPage, &nPos);
    ASSERT_EQ(nMin, 0);
    ASSERT_EQ(nMax, 0);
    ASSERT_EQ(nPage, 12);
    ASSERT_EQ(nPos, 0);
    TEST_END();
}

int test_edit_scroll_params_single_line(void) {
    TEST_BEGIN();
    int nMin, nMax, nPage, nPos;
    edit_scroll_params(1, 0, 200, 16, &nMin, &nMax, &nPage, &nPos);
    ASSERT_EQ(nMin, 0);
    ASSERT_EQ(nMax, 0);
    ASSERT_EQ(nPage, 12);
    ASSERT_EQ(nPos, 0);
    TEST_END();
}

/* --- edit_scroll_line_delta tests --- */

int test_edit_scroll_delta_forward(void) {
    TEST_BEGIN();
    /* Scroll from line 5 to line 10 = +5 */
    ASSERT_EQ(edit_scroll_line_delta(10, 5), 5);
    TEST_END();
}

int test_edit_scroll_delta_backward(void) {
    TEST_BEGIN();
    /* Scroll from line 10 to line 3 = -7 */
    ASSERT_EQ(edit_scroll_line_delta(3, 10), -7);
    TEST_END();
}

int test_edit_scroll_delta_none(void) {
    TEST_BEGIN();
    ASSERT_EQ(edit_scroll_line_delta(5, 5), 0);
    TEST_END();
}

int test_edit_scroll_delta_from_zero(void) {
    TEST_BEGIN();
    ASSERT_EQ(edit_scroll_line_delta(20, 0), 20);
    TEST_END();
}

int test_edit_scroll_delta_to_zero(void) {
    TEST_BEGIN();
    ASSERT_EQ(edit_scroll_line_delta(0, 15), -15);
    TEST_END();
}

/* --- edit_scroll_wheel_delta tests --- */

int test_wheel_scroll_up_one_notch(void) {
    TEST_BEGIN();
    /* Wheel up (+120) at 3 lines/notch -> EM_LINESCROLL delta = -3 */
    ASSERT_EQ(edit_scroll_wheel_delta(120, 120, 3), -3);
    TEST_END();
}

int test_wheel_scroll_down_one_notch(void) {
    TEST_BEGIN();
    /* Wheel down (-120) at 3 lines/notch -> EM_LINESCROLL delta = +3 */
    ASSERT_EQ(edit_scroll_wheel_delta(-120, 120, 3), 3);
    TEST_END();
}

int test_wheel_scroll_two_notches(void) {
    TEST_BEGIN();
    /* Two notches up (+240) -> -6 */
    ASSERT_EQ(edit_scroll_wheel_delta(240, 120, 3), -6);
    /* Two notches down (-240) -> +6 */
    ASSERT_EQ(edit_scroll_wheel_delta(-240, 120, 3), 6);
    TEST_END();
}

int test_wheel_scroll_custom_speed(void) {
    TEST_BEGIN();
    /* 5 lines per notch */
    ASSERT_EQ(edit_scroll_wheel_delta(120, 120, 5), -5);
    ASSERT_EQ(edit_scroll_wheel_delta(-120, 120, 5), 5);
    /* 1 line per notch */
    ASSERT_EQ(edit_scroll_wheel_delta(120, 120, 1), -1);
    TEST_END();
}

int test_wheel_scroll_zero_delta(void) {
    TEST_BEGIN();
    /* No wheel movement -> no scroll */
    ASSERT_EQ(edit_scroll_wheel_delta(0, 120, 3), 0);
    TEST_END();
}

int test_wheel_scroll_zero_notch_size(void) {
    TEST_BEGIN();
    /* Guard against division by zero */
    ASSERT_EQ(edit_scroll_wheel_delta(120, 0, 3), 0);
    TEST_END();
}

int test_wheel_scroll_partial_notch(void) {
    TEST_BEGIN();
    /* Less than one notch (high-res trackpad) -> truncates to 0 */
    ASSERT_EQ(edit_scroll_wheel_delta(60, 120, 3), 0);
    ASSERT_EQ(edit_scroll_wheel_delta(-60, 120, 3), 0);
    TEST_END();
}
