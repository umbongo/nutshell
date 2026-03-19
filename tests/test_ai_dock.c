#include "test_framework.h"
#include "ai_dock.h"

/* ---- Can-open guard ---- */

int test_dock_can_open_no_session(void) {
    TEST_BEGIN();
    ASSERT_TRUE(!ai_dock_can_open(0, 0));
    TEST_END();
}

int test_dock_can_open_no_channel(void) {
    TEST_BEGIN();
    ASSERT_TRUE(!ai_dock_can_open(1, 0));
    TEST_END();
}

int test_dock_can_open_connected(void) {
    TEST_BEGIN();
    ASSERT_TRUE(ai_dock_can_open(1, 1));
    TEST_END();
}

/* ---- Panel width calculation ---- */

int test_dock_pct_to_px_default(void) {
    TEST_BEGIN();
    /* 25% of 1200px = 300px */
    int w = ai_dock_pct_to_px(1200, 25, AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
    ASSERT_EQ(w, 300);
    TEST_END();
}

int test_dock_pct_to_px_clamp_min(void) {
    TEST_BEGIN();
    /* Requesting 5% should clamp to 10% */
    int w = ai_dock_pct_to_px(1000, 5, AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
    ASSERT_EQ(w, 100);
    TEST_END();
}

int test_dock_pct_to_px_clamp_max(void) {
    TEST_BEGIN();
    /* Requesting 90% should clamp to 75% */
    int w = ai_dock_pct_to_px(1000, 90, AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
    ASSERT_EQ(w, 750);
    TEST_END();
}

int test_dock_pct_to_px_zero_client(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_dock_pct_to_px(0, 25, 10, 75), 0);
    TEST_END();
}

/* ---- Clamp width ---- */

int test_dock_clamp_width_within_range(void) {
    TEST_BEGIN();
    int w = ai_dock_clamp_width(400, 1000, AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
    ASSERT_EQ(w, 400);
    TEST_END();
}

int test_dock_clamp_width_too_small(void) {
    TEST_BEGIN();
    /* 50px on 1000px window, min 10% = 100px */
    int w = ai_dock_clamp_width(50, 1000, AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
    ASSERT_EQ(w, 100);
    TEST_END();
}

int test_dock_clamp_width_too_large(void) {
    TEST_BEGIN();
    /* 900px on 1000px window, max 75% = 750px */
    int w = ai_dock_clamp_width(900, 1000, AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
    ASSERT_EQ(w, 750);
    TEST_END();
}

/* ---- Terminal width ---- */

int test_dock_terminal_width_no_panel(void) {
    TEST_BEGIN();
    /* No panel: 1200 - 0 - 14 - 6 = 1180 */
    int w = ai_dock_terminal_width(1200, 0, 14, 6);
    ASSERT_EQ(w, 1180);
    TEST_END();
}

int test_dock_terminal_width_with_panel(void) {
    TEST_BEGIN();
    /* With 300px panel: 1200 - 300 - 14 - 6 = 880 */
    int w = ai_dock_terminal_width(1200, 300, 14, 6);
    ASSERT_EQ(w, 880);
    TEST_END();
}

int test_dock_terminal_width_minimum(void) {
    TEST_BEGIN();
    /* Very small: should return 1 (minimum) */
    int w = ai_dock_terminal_width(100, 90, 14, 6);
    ASSERT_EQ(w, 1);
    TEST_END();
}

/* ---- Ease-in curve ---- */

int test_dock_ease_in_zero(void) {
    TEST_BEGIN();
    ASSERT_TRUE(ai_dock_ease_in(0.0) == 0.0);
    TEST_END();
}

int test_dock_ease_in_one(void) {
    TEST_BEGIN();
    ASSERT_TRUE(ai_dock_ease_in(1.0) == 1.0);
    TEST_END();
}

int test_dock_ease_in_half(void) {
    TEST_BEGIN();
    /* At t=0.5, ease_in = 0.25 (slow start) */
    double e = ai_dock_ease_in(0.5);
    ASSERT_TRUE(e > 0.24 && e < 0.26);
    TEST_END();
}

int test_dock_ease_in_clamp(void) {
    TEST_BEGIN();
    /* Out of range should clamp */
    ASSERT_TRUE(ai_dock_ease_in(-0.5) == 0.0);
    ASSERT_TRUE(ai_dock_ease_in(1.5) == 1.0);
    TEST_END();
}

/* ---- Animation width (legacy open-only) ---- */

int test_dock_anim_width_start(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_dock_anim_width(300, 0.0), 0);
    TEST_END();
}

int test_dock_anim_width_end(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_dock_anim_width(300, 1.0), 300);
    TEST_END();
}

int test_dock_anim_width_mid(void) {
    TEST_BEGIN();
    /* At t=0.5, ease=0.25, so 300 * 0.25 = 75 */
    ASSERT_EQ(ai_dock_anim_width(300, 0.5), 75);
    TEST_END();
}

/* ---- Eased lerp (open + close animation) ---- */

int test_dock_lerp_open_start(void) {
    TEST_BEGIN();
    /* Open: from=0 to=300 at t=0.0 -> 0 */
    ASSERT_EQ(ai_dock_anim_lerp(0, 300, 0.0), 0);
    TEST_END();
}

int test_dock_lerp_open_end(void) {
    TEST_BEGIN();
    /* Open: from=0 to=300 at t=1.0 -> 300 */
    ASSERT_EQ(ai_dock_anim_lerp(0, 300, 1.0), 300);
    TEST_END();
}

int test_dock_lerp_open_mid(void) {
    TEST_BEGIN();
    /* Open: from=0 to=300 at t=0.5, ease=0.25 -> 75 */
    ASSERT_EQ(ai_dock_anim_lerp(0, 300, 0.5), 75);
    TEST_END();
}

int test_dock_lerp_close_start(void) {
    TEST_BEGIN();
    /* Close: from=300 to=0 at t=0.0 -> 300 */
    ASSERT_EQ(ai_dock_anim_lerp(300, 0, 0.0), 300);
    TEST_END();
}

int test_dock_lerp_close_end(void) {
    TEST_BEGIN();
    /* Close: from=300 to=0 at t=1.0 -> 0 */
    ASSERT_EQ(ai_dock_anim_lerp(300, 0, 1.0), 0);
    TEST_END();
}

int test_dock_lerp_close_mid(void) {
    TEST_BEGIN();
    /* Close: from=300 to=0 at t=0.5, ease=0.25 -> 300 + (0-300)*0.25 = 225 */
    ASSERT_EQ(ai_dock_anim_lerp(300, 0, 0.5), 225);
    TEST_END();
}

int test_dock_lerp_close_quarter(void) {
    TEST_BEGIN();
    /* Close: from=400 to=0 at t=1.0 -> 0 */
    ASSERT_EQ(ai_dock_anim_lerp(400, 0, 1.0), 0);
    /* Close: at t=0.0 -> still at 400 */
    ASSERT_EQ(ai_dock_anim_lerp(400, 0, 0.0), 400);
    TEST_END();
}

int test_dock_lerp_resize(void) {
    TEST_BEGIN();
    /* Resize from 200 to 400 at t=1.0 -> 400 */
    ASSERT_EQ(ai_dock_anim_lerp(200, 400, 1.0), 400);
    /* At t=0.5, ease=0.25 -> 200 + (400-200)*0.25 = 250 */
    ASSERT_EQ(ai_dock_anim_lerp(200, 400, 0.5), 250);
    TEST_END();
}

/* ---- Splitter hit test ---- */

int test_dock_splitter_hit_inside(void) {
    TEST_BEGIN();
    /* Splitter at x=900, width=4, half=2, mouse at 899 */
    ASSERT_TRUE(ai_dock_splitter_hit(899, 900, 4, 50, 32));
    ASSERT_TRUE(ai_dock_splitter_hit(900, 900, 4, 50, 32));
    ASSERT_TRUE(ai_dock_splitter_hit(901, 900, 4, 50, 32));
    TEST_END();
}

int test_dock_splitter_hit_outside(void) {
    TEST_BEGIN();
    ASSERT_TRUE(!ai_dock_splitter_hit(895, 900, 4, 50, 32));
    ASSERT_TRUE(!ai_dock_splitter_hit(905, 900, 4, 50, 32));
    TEST_END();
}

int test_dock_splitter_hit_above_tabs(void) {
    TEST_BEGIN();
    /* Mouse in tab bar area (y < top_y) should not hit */
    ASSERT_TRUE(!ai_dock_splitter_hit(900, 900, 4, 10, 32));
    TEST_END();
}

/* ---- Chat layout clamping ---- */

int test_dock_chat_layout_normal(void) {
    TEST_BEGIN();
    int dw, dh;
    /* Normal case: 400w, 600h, top_y=32, input_h=46, approve=0, margin=5, sb=14 */
    ai_dock_chat_layout(400, 600, 32, 46, 0, 5, 14, &dw, &dh);
    /* dw = 400 - 10 - 14 = 376, dh = 600 - 46 - 0 - 32 - 10 = 512 */
    ASSERT_EQ(dw, 376);
    ASSERT_EQ(dh, 512);
    TEST_END();
}

int test_dock_chat_layout_tiny_window(void) {
    TEST_BEGIN();
    int dw, dh;
    /* Tiny window (1x1 during animation): dimensions should clamp to 1 */
    ai_dock_chat_layout(1, 1, 32, 46, 0, 5, 14, &dw, &dh);
    ASSERT_EQ(dw, 1);
    ASSERT_EQ(dh, 1);
    TEST_END();
}

int test_dock_chat_layout_with_approval(void) {
    TEST_BEGIN();
    int dw, dh;
    /* With approval bar: dh shrinks by approve_h */
    ai_dock_chat_layout(400, 600, 32, 46, 28, 5, 14, &dw, &dh);
    /* dh = 600 - 46 - 28 - 32 - 10 = 484 */
    ASSERT_EQ(dw, 376);
    ASSERT_EQ(dh, 484);
    TEST_END();
}

/* ---- Initial terminal columns ---- */

int test_dock_initial_cols_no_panel(void) {
    TEST_BEGIN();
    /* No AI panel: (1200 - 0 - 14 - 6) / 10 = 118 */
    int cols = ai_dock_initial_term_cols(1200, 10, 0, 0, 14, 6);
    ASSERT_EQ(cols, 118);
    TEST_END();
}

int test_dock_initial_cols_with_panel(void) {
    TEST_BEGIN();
    /* With 300px AI panel: (1200 - 300 - 14 - 6) / 10 = 88 */
    int cols = ai_dock_initial_term_cols(1200, 10, 1, 300, 14, 6);
    ASSERT_EQ(cols, 88);
    TEST_END();
}

int test_dock_initial_cols_zero_char_width(void) {
    TEST_BEGIN();
    /* char_w=0 should return fallback of 80 */
    int cols = ai_dock_initial_term_cols(1200, 0, 0, 0, 14, 6);
    ASSERT_EQ(cols, 80);
    TEST_END();
}
