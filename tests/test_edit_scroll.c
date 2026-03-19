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

/* --- edit_scroll_wheel_accum tests ---
 *
 * High-precision mice and trackpads send sub-notch wheel deltas (e.g. 30, 40)
 * which the old function silently drops via integer division truncation.
 * The accumulator version collects partial deltas across events and scrolls
 * once a full notch threshold is crossed. */

int test_wheel_accum_full_notch(void) {
    TEST_BEGIN();
    int accum = 0;
    /* Full notch in one event -> scrolls immediately, accumulator resets */
    ASSERT_EQ(edit_scroll_wheel_accum(120, 120, 3, &accum), -3);
    ASSERT_EQ(accum, 0);
    /* Full notch down */
    ASSERT_EQ(edit_scroll_wheel_accum(-120, 120, 3, &accum), 3);
    ASSERT_EQ(accum, 0);
    TEST_END();
}

int test_wheel_accum_partial_events(void) {
    TEST_BEGIN();
    int accum = 0;
    /* Four partial events of +30 each = 120 total = one notch up */
    ASSERT_EQ(edit_scroll_wheel_accum(30, 120, 3, &accum), 0);
    ASSERT_EQ(accum, 30);
    ASSERT_EQ(edit_scroll_wheel_accum(30, 120, 3, &accum), 0);
    ASSERT_EQ(accum, 60);
    ASSERT_EQ(edit_scroll_wheel_accum(30, 120, 3, &accum), 0);
    ASSERT_EQ(accum, 90);
    /* Fourth event crosses threshold */
    ASSERT_EQ(edit_scroll_wheel_accum(30, 120, 3, &accum), -3);
    ASSERT_EQ(accum, 0);
    TEST_END();
}

int test_wheel_accum_partial_down(void) {
    TEST_BEGIN();
    int accum = 0;
    /* Two partial events of -60 each = -120 total = one notch down */
    ASSERT_EQ(edit_scroll_wheel_accum(-60, 120, 3, &accum), 0);
    ASSERT_EQ(accum, -60);
    ASSERT_EQ(edit_scroll_wheel_accum(-60, 120, 3, &accum), 3);
    ASSERT_EQ(accum, 0);
    TEST_END();
}

int test_wheel_accum_overflow_carries(void) {
    TEST_BEGIN();
    int accum = 0;
    /* 150 = one full notch (120) + 30 remainder */
    ASSERT_EQ(edit_scroll_wheel_accum(150, 120, 3, &accum), -3);
    ASSERT_EQ(accum, 30);
    /* Next 90 + carry 30 = 120 -> another notch */
    ASSERT_EQ(edit_scroll_wheel_accum(90, 120, 3, &accum), -3);
    ASSERT_EQ(accum, 0);
    TEST_END();
}

int test_wheel_accum_direction_change_resets(void) {
    TEST_BEGIN();
    int accum = 0;
    /* Accumulate up, then change direction */
    ASSERT_EQ(edit_scroll_wheel_accum(60, 120, 3, &accum), 0);
    ASSERT_EQ(accum, 60);
    /* Direction reversal: reset accumulator, start fresh */
    ASSERT_EQ(edit_scroll_wheel_accum(-30, 120, 3, &accum), 0);
    ASSERT_EQ(accum, -30);
    TEST_END();
}

int test_wheel_accum_two_notches(void) {
    TEST_BEGIN();
    int accum = 0;
    /* 240 = two notches up -> -6 lines */
    ASSERT_EQ(edit_scroll_wheel_accum(240, 120, 3, &accum), -6);
    ASSERT_EQ(accum, 0);
    TEST_END();
}

int test_wheel_accum_zero_notch_size(void) {
    TEST_BEGIN();
    int accum = 0;
    ASSERT_EQ(edit_scroll_wheel_accum(120, 0, 3, &accum), 0);
    TEST_END();
}

int test_wheel_accum_null_accumulator(void) {
    TEST_BEGIN();
    /* Should not crash with NULL accumulator */
    ASSERT_EQ(edit_scroll_wheel_accum(120, 120, 3, NULL), 0);
    TEST_END();
}

/* --- edit_scroll_needed tests --- */

int test_scroll_needed_overflow(void) {
    TEST_BEGIN();
    /* 50 lines, 200px edit, 16px line = 12 visible -> scrollbar needed */
    ASSERT_EQ(edit_scroll_needed(50, 200, 16), 1);
    TEST_END();
}

int test_scroll_needed_exact_fit(void) {
    TEST_BEGIN();
    /* 10 lines, 160px edit, 16px line = exactly 10 visible -> not needed */
    ASSERT_EQ(edit_scroll_needed(10, 160, 16), 0);
    TEST_END();
}

int test_scroll_needed_fewer_lines(void) {
    TEST_BEGIN();
    /* 5 lines, 200px edit, 16px line = 12 visible -> not needed */
    ASSERT_EQ(edit_scroll_needed(5, 200, 16), 0);
    TEST_END();
}

int test_scroll_needed_one_over(void) {
    TEST_BEGIN();
    /* 11 lines, 160px edit, 16px line = 10 visible -> needed */
    ASSERT_EQ(edit_scroll_needed(11, 160, 16), 1);
    TEST_END();
}

int test_scroll_needed_single_line(void) {
    TEST_BEGIN();
    /* 1 line, 200px edit -> not needed */
    ASSERT_EQ(edit_scroll_needed(1, 200, 16), 0);
    TEST_END();
}

int test_scroll_needed_zero_lines(void) {
    TEST_BEGIN();
    /* 0 lines -> not needed */
    ASSERT_EQ(edit_scroll_needed(0, 200, 16), 0);
    TEST_END();
}

int test_scroll_needed_zero_height(void) {
    TEST_BEGIN();
    /* Zero edit height: degenerate -> needed (can't show anything) */
    ASSERT_EQ(edit_scroll_needed(5, 0, 16), 1);
    TEST_END();
}

int test_scroll_needed_zero_line_height(void) {
    TEST_BEGIN();
    /* Zero line height: degenerate -> needed (can't compute visible) */
    ASSERT_EQ(edit_scroll_needed(5, 200, 0), 1);
    TEST_END();
}

int test_scroll_needed_empty_zero_height(void) {
    TEST_BEGIN();
    /* Zero lines and zero height -> not needed */
    ASSERT_EQ(edit_scroll_needed(0, 0, 16), 0);
    TEST_END();
}
