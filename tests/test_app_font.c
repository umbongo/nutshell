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
