#include "test_framework.h"
#include "app_font.h"
#include <string.h>

/* ---- Size table tests ---------------------------------------------------- */

int test_app_font_sizes_count(void) {
    TEST_BEGIN();
    ASSERT_EQ(APP_FONT_NUM_SIZES, 8);
    TEST_END();
}

int test_app_font_sizes_ascending(void) {
    TEST_BEGIN();
    for (int i = 1; i < APP_FONT_NUM_SIZES; i++) {
        ASSERT_TRUE(k_app_font_sizes[i] > k_app_font_sizes[i - 1]);
    }
    TEST_END();
}

/* ---- Snap tests ---------------------------------------------------------- */

int test_app_font_snap_exact(void) {
    TEST_BEGIN();
    ASSERT_EQ(app_font_snap_size(6),  6);
    ASSERT_EQ(app_font_snap_size(10), 10);
    ASSERT_EQ(app_font_snap_size(20), 20);
    TEST_END();
}

int test_app_font_snap_nearest(void) {
    TEST_BEGIN();
    ASSERT_EQ(app_font_snap_size(9),  8);   /* 9 is closer to 8 than 10 */
    ASSERT_EQ(app_font_snap_size(11), 10);  /* 11 is closer to 10 than 12 */
    ASSERT_EQ(app_font_snap_size(13), 12);  /* 13 is closer to 12 than 14 */
    ASSERT_EQ(app_font_snap_size(15), 14);  /* 15 is closer to 14 than 16 */
    TEST_END();
}

int test_app_font_snap_boundary(void) {
    TEST_BEGIN();
    ASSERT_EQ(app_font_snap_size(0),  6);   /* below min snaps to 6 */
    ASSERT_EQ(app_font_snap_size(1),  6);
    ASSERT_EQ(app_font_snap_size(99), 20);  /* above max snaps to 20 */
    ASSERT_EQ(app_font_snap_size(-5), 6);   /* negative snaps to 6 */
    TEST_END();
}

/* ---- Zoom tests ---------------------------------------------------------- */

int test_app_font_zoom_up(void) {
    TEST_BEGIN();
    ASSERT_EQ(app_font_zoom(10, 1), 12);
    ASSERT_EQ(app_font_zoom(6,  1), 8);
    ASSERT_EQ(app_font_zoom(18, 1), 20);
    TEST_END();
}

int test_app_font_zoom_down(void) {
    TEST_BEGIN();
    ASSERT_EQ(app_font_zoom(10, -1), 8);
    ASSERT_EQ(app_font_zoom(20, -1), 18);
    ASSERT_EQ(app_font_zoom(8,  -1), 6);
    TEST_END();
}

int test_app_font_zoom_at_min(void) {
    TEST_BEGIN();
    ASSERT_EQ(app_font_zoom(6, -1), 6);  /* can't go below min */
    TEST_END();
}

int test_app_font_zoom_at_max(void) {
    TEST_BEGIN();
    ASSERT_EQ(app_font_zoom(20, 1), 20);  /* can't go above max */
    TEST_END();
}

/* ---- UI font zoom tests (AI chat starts at 9pt, not in the table) -------- */

int test_app_font_zoom_from_ui_size_up(void) {
    TEST_BEGIN();
    /* APP_FONT_UI_SIZE is 9, which sits between 8 and 10.
     * Zooming up from 9 should go to 10 (next size above). */
    int result = app_font_zoom(APP_FONT_UI_SIZE, 1);
    ASSERT_EQ(result, 10);
    TEST_END();
}

int test_app_font_zoom_from_ui_size_down(void) {
    TEST_BEGIN();
    /* Zooming down from 9 should snap to 8 (the floor value, which is
     * already smaller than 9). */
    int result = app_font_zoom(APP_FONT_UI_SIZE, -1);
    ASSERT_EQ(result, 8);
    TEST_END();
}

int test_app_font_zoom_ui_full_range_up(void) {
    TEST_BEGIN();
    /* Starting from UI size (9), zoom up repeatedly to max */
    int sz = APP_FONT_UI_SIZE;
    sz = app_font_zoom(sz, 1);   ASSERT_EQ(sz, 10);
    sz = app_font_zoom(sz, 1);   ASSERT_EQ(sz, 12);
    sz = app_font_zoom(sz, 1);   ASSERT_EQ(sz, 14);
    sz = app_font_zoom(sz, 1);   ASSERT_EQ(sz, 16);
    sz = app_font_zoom(sz, 1);   ASSERT_EQ(sz, 18);
    sz = app_font_zoom(sz, 1);   ASSERT_EQ(sz, 20);
    sz = app_font_zoom(sz, 1);   ASSERT_EQ(sz, 20); /* clamped */
    TEST_END();
}

int test_app_font_zoom_preserves_size_after_roundtrip(void) {
    TEST_BEGIN();
    /* Zoom up then back down should return to a table value, not lose state.
     * This simulates the AI chat zoom: the size must always land on a valid
     * table entry so that rebuild_display can use it as a base. */
    int sz = APP_FONT_UI_SIZE;  /* 9, not in table */
    sz = app_font_zoom(sz, 1);  ASSERT_EQ(sz, 10); /* now in table */
    sz = app_font_zoom(sz, -1); ASSERT_EQ(sz, 8);  /* still in table */
    sz = app_font_zoom(sz, 1);  ASSERT_EQ(sz, 10); /* back to 10 */
    sz = app_font_zoom(sz, 1);  ASSERT_EQ(sz, 12);
    sz = app_font_zoom(sz, -1); ASSERT_EQ(sz, 10); /* exact roundtrip */
    TEST_END();
}

int test_app_font_zoom_ui_full_range_down(void) {
    TEST_BEGIN();
    /* Starting from UI size (9), zoom down repeatedly to min */
    int sz = APP_FONT_UI_SIZE;
    sz = app_font_zoom(sz, -1);  ASSERT_EQ(sz, 8);
    sz = app_font_zoom(sz, -1);  ASSERT_EQ(sz, 6);
    sz = app_font_zoom(sz, -1);  ASSERT_EQ(sz, 6); /* clamped */
    TEST_END();
}
