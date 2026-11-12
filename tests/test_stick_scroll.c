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

/* --- stick_scroll_anchor tests --- */

int test_stick_scroll_anchor_pinned_unchanged(void) {
    TEST_BEGIN();
    ASSERT_EQ(stick_scroll_anchor(1, 500, 400, 300, 80), 500);
    ASSERT_EQ(stick_scroll_anchor(1, 0, 0, 0, 0), 0);
    TEST_END();
}

int test_stick_scroll_anchor_follows_item_top(void) {
    TEST_BEGIN();
    /* 30 px into an item that moved up 100 px: still 30 px into it */
    ASSERT_EQ(stick_scroll_anchor(0, 430, 400, 300, 80), 330);
    /* content above grew: the item moved down, the view follows */
    ASSERT_EQ(stick_scroll_anchor(0, 430, 400, 450, 80), 480);
    /* nothing moved */
    ASSERT_EQ(stick_scroll_anchor(0, 430, 400, 400, 80), 430);
    TEST_END();
}

int test_stick_scroll_anchor_offset_clamped_to_new_height(void) {
    TEST_BEGIN();
    /* 300 px into an item that is now only 50 px tall: its last pixel */
    ASSERT_EQ(stick_scroll_anchor(0, 400, 100, 100, 50), 149);
    /* a zero-height item: offset 0 */
    ASSERT_EQ(stick_scroll_anchor(0, 400, 100, 100, 0), 100);
    TEST_END();
}

int test_stick_scroll_anchor_never_negative(void) {
    TEST_BEGIN();
    /* 40 px above the item (its gap) and the item moved to the very top */
    ASSERT_EQ(stick_scroll_anchor(0, 10, 50, 5, 80), 0);
    ASSERT_EQ(stick_scroll_anchor(0, 0, 0, 0, 80), 0);
    TEST_END();
}

/* --- stick_scroll_pick_anchor tests --- */

int test_stick_scroll_pick_anchor_plain_item(void) {
    TEST_BEGIN();
    /* gap 10: tops 10, 60 (h 40), 110 (h 100); scroll_y 70 is 10 px into #1 */
    int h[3]  = { 40, 40, 100 };
    int rd[3] = { -1, -1, -1 };
    int off = -99;
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd, 3, 10, 70, &off), 1);
    ASSERT_EQ(off, 10);
    /* in the gap above the first item: that item, negative offset */
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd, 3, 10, 0, &off), 0);
    ASSERT_EQ(off, -10);
    /* a zero-height item is skipped, no gap added for it */
    int h2[3] = { 40, 0, 100 };
    ASSERT_EQ(stick_scroll_pick_anchor(h2, rd, 3, 10, 70, &off), 2);
    ASSERT_EQ(off, 10);
    TEST_END();
}

int test_stick_scroll_pick_anchor_hidden_redirects_to_head_offset_zero(void) {
    TEST_BEGIN();
    /* #0 is a fold head, #1 and #2 were its rows (old heights still set) and
     * are hidden now; scroll_y (85) is in the gap above #2: anchor on #0 at offset 0 */
    int h[4]  = { 30, 30, 30, 50 };
    int rd[4] = { -1, 0, 0, -1 };
    int off = -99;
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd, 4, 10, 85, &off), 0);
    ASSERT_EQ(off, 0);
    /* the first row below the hidden group is a plain item again */
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd, 4, 10, 135, &off), 3);
    ASSERT_EQ(off, 135 - 130);
    TEST_END();
}

int test_stick_scroll_pick_anchor_head_with_changed_fold_offset_zero(void) {
    TEST_BEGIN();
    /* #1 is a tall head (old height 200) that folded in this refresh:
     * redirect[1] == 1 marks "this row itself changed fold". scroll_y 150
     * sits 90 px into it; the fold row's top goes to the top of the view. */
    int h[3]  = { 40, 200, 40 };
    int rd[3] = { -1, 1, -1 };
    int off = -99;
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd, 3, 10, 150, &off), 1);
    ASSERT_EQ(off, 0);
    /* the same head whose fold did not change keeps its offset */
    int rd2[3] = { -1, -1, -1 };
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd2, 3, 10, 150, &off), 1);
    ASSERT_EQ(off, 90);
    /* a changed head elsewhere does not affect an unrelated anchor */
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd, 3, 10, 30, &off), 0);
    ASSERT_EQ(off, 30 - 10);
    TEST_END();
}

int test_stick_scroll_pick_anchor_nothing_visible(void) {
    TEST_BEGIN();
    int h[2]  = { 40, 40 };
    int rd[2] = { -1, -1 };
    int off = -99;
    /* scrolled past the end */
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd, 2, 10, 500, &off), -1);
    /* empty, all hidden, bad arguments */
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd, 0, 10, 0, &off), -1);
    int z[2] = { 0, 0 };
    ASSERT_EQ(stick_scroll_pick_anchor(z, rd, 2, 10, 0, &off), -1);
    ASSERT_EQ(stick_scroll_pick_anchor(NULL, rd, 2, 10, 0, &off), -1);
    ASSERT_EQ(stick_scroll_pick_anchor(h, NULL, 2, 10, 0, &off), -1);
    ASSERT_EQ(stick_scroll_pick_anchor(h, rd, 2, 10, 0, NULL), -1);
    TEST_END();
}

/* --- stick_scroll_hold_row tests --- */

int test_stick_scroll_hold_row_keeps_screen_position(void) {
    TEST_BEGIN();
    /* the row was 120 px below the top of the view; now its top is 900 */
    ASSERT_EQ(stick_scroll_hold_row(120, 900, 5000), 780);
    /* content above it shrank: it moved up, the view follows */
    ASSERT_EQ(stick_scroll_hold_row(120, 300, 5000), 180);
    TEST_END();
}

int test_stick_scroll_hold_row_clamps(void) {
    TEST_BEGIN();
    /* would scroll above the top */
    ASSERT_EQ(stick_scroll_hold_row(200, 100, 5000), 0);
    /* would scroll past the bottom (content shrank below the row) */
    ASSERT_EQ(stick_scroll_hold_row(10, 900, 400), 400);
    /* content fits the viewport */
    ASSERT_EQ(stick_scroll_hold_row(10, 900, -50), 0);
    TEST_END();
}
