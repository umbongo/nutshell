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

/* ---- UI font zoom tests -----------------------------------------------
 * APP_FONT_UI_SIZE is 10 as of the design-system foundation work (body text
 * moved from 9pt to 10pt), which now sits exactly on a table entry. These
 * tests exercise app_font_zoom()'s off-table snapping directly with a
 * literal 9 (the size AI chat used to start at) so the coverage for
 * "starting size not in the table" survives that change. */

int test_app_font_zoom_from_off_table_size_up(void) {
    TEST_BEGIN();
    /* 9 sits between 8 and 10. Zooming up from 9 should go to 10 (next
     * size above). */
    int result = app_font_zoom(9, 1);
    ASSERT_EQ(result, 10);
    TEST_END();
}

int test_app_font_zoom_from_off_table_size_down(void) {
    TEST_BEGIN();
    /* Zooming down from 9 should snap to 8 (the floor value, which is
     * already smaller than 9). */
    int result = app_font_zoom(9, -1);
    ASSERT_EQ(result, 8);
    TEST_END();
}

int test_app_font_zoom_ui_size_is_in_table(void) {
    TEST_BEGIN();
    /* APP_FONT_UI_SIZE (10) lands exactly on a table entry, so zooming
     * from it behaves like any other in-table size. */
    ASSERT_EQ(app_font_zoom(APP_FONT_UI_SIZE, 1), 12);
    ASSERT_EQ(app_font_zoom(APP_FONT_UI_SIZE, -1), 8);
    TEST_END();
}

int test_app_font_zoom_full_range_up(void) {
    TEST_BEGIN();
    /* Starting from an off-table size (9), zoom up repeatedly to max */
    int sz = 9;
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
    int sz = 9;  /* not in table */
    sz = app_font_zoom(sz, 1);  ASSERT_EQ(sz, 10); /* now in table */
    sz = app_font_zoom(sz, -1); ASSERT_EQ(sz, 8);  /* still in table */
    sz = app_font_zoom(sz, 1);  ASSERT_EQ(sz, 10); /* back to 10 */
    sz = app_font_zoom(sz, 1);  ASSERT_EQ(sz, 12);
    sz = app_font_zoom(sz, -1); ASSERT_EQ(sz, 10); /* exact roundtrip */
    TEST_END();
}

int test_app_font_zoom_full_range_down(void) {
    TEST_BEGIN();
    /* Starting from an off-table size (9), zoom down repeatedly to min */
    int sz = 9;
    sz = app_font_zoom(sz, -1);  ASSERT_EQ(sz, 8);
    sz = app_font_zoom(sz, -1);  ASSERT_EQ(sz, 6);
    sz = app_font_zoom(sz, -1);  ASSERT_EQ(sz, 6); /* clamped */
    TEST_END();
}

/* ---- app_font_px_height: a point size at a DPI (CreateFont's negative
 * character height), the arithmetic the AI panel redoes on a monitor move ---- */

int test_app_font_px_height_known_values(void) {
    TEST_BEGIN();
    /* MulDiv(pt, dpi, 72), negated: rounded to the nearest pixel. */
    ASSERT_EQ(app_font_px_height(10, 96), -13);    /* 13.33 */
    ASSERT_EQ(app_font_px_height(8, 96), -11);     /* 10.67 */
    ASSERT_EQ(app_font_px_height(10, 120), -17);   /* 16.67 */
    ASSERT_EQ(app_font_px_height(14, 144), -28);   /* exact */
    ASSERT_EQ(app_font_px_height(20, 192), -53);   /* 53.33 */
    ASSERT_EQ(app_font_px_height(6, 96), -8);      /* exact */
    ASSERT_EQ(app_font_px_height(9, 100), -13);    /* 12.5 rounds up */
    TEST_END();
}

int test_app_font_px_height_bad_input(void) {
    TEST_BEGIN();
    ASSERT_EQ(app_font_px_height(0, 96), 0);
    ASSERT_EQ(app_font_px_height(-4, 96), 0);
    ASSERT_EQ(app_font_px_height(10, 0), app_font_px_height(10, 96));   /* dpi <= 0 is 96 */
    ASSERT_EQ(app_font_px_height(10, -5), app_font_px_height(10, 96));
    TEST_END();
}

int test_app_font_px_height_grows_with_dpi_and_zoom(void) {
    TEST_BEGIN();
    static const int dpis[] = { 96, 120, 144, 192 };
    for (int s = 0; s < APP_FONT_NUM_SIZES; s++) {
        int prev = 0;
        for (int i = 0; i < 4; i++) {
            int h = app_font_px_height(k_app_font_sizes[s], dpis[i]);
            ASSERT_TRUE(h < 0);                 /* character height: negative */
            ASSERT_TRUE(h <= prev);             /* never smaller at a higher DPI */
            prev = h;
        }
    }
    /* The user's zoom survives a DPI change: the same point size on both
     * monitors, so the zoom step keeps ordering the heights. */
    for (int i = 0; i < 4; i++)
        for (int s = 1; s < APP_FONT_NUM_SIZES; s++)
            ASSERT_TRUE(app_font_px_height(k_app_font_sizes[s], dpis[i]) <
                        app_font_px_height(k_app_font_sizes[s - 1], dpis[i]));
    TEST_END();
}
