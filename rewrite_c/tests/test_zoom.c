#include "test_framework.h"
#include "zoom.h"

/* =========================================================================
 * zoom_font_fits — positive tests
 * ========================================================================= */

/* Both dimensions divide evenly: perfect fit. */
int test_zoom_exact_fit(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(640, 384, 8, 16), 1);  /* 80×24 */
    TEST_END();
}

/* Odd cell sizes that still divide evenly. */
int test_zoom_odd_cell_sizes(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(639, 432, 9, 18), 1);  /* 71×24 */
    TEST_END();
}

/* 1×1 cells always fit. */
int test_zoom_unit_cells(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(1024, 768, 1, 1), 1);
    ASSERT_EQ(zoom_font_fits(1, 1, 1, 1), 1);
    TEST_END();
}

/* Large exact multiples. */
int test_zoom_large_window(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(1920, 1056, 10, 22), 1); /* 192×48 */
    TEST_END();
}

/* =========================================================================
 * zoom_font_fits — negative tests
 * ========================================================================= */

/* 1-pixel horizontal gutter. */
int test_zoom_hgutter(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(641, 384, 8, 16), 0);
    TEST_END();
}

/* 1-pixel vertical gutter. */
int test_zoom_vgutter(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(640, 385, 8, 16), 0);
    TEST_END();
}

/* Gutter on both axes. */
int test_zoom_both_gutters(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(641, 385, 8, 16), 0);
    TEST_END();
}

/* Zero char_w: guard against divide-by-zero, returns 0. */
int test_zoom_zero_char_w(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(640, 384, 0, 16), 0);
    TEST_END();
}

/* Zero char_h: guard against divide-by-zero, returns 0. */
int test_zoom_zero_char_h(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(640, 384, 8, 0), 0);
    TEST_END();
}

/* Zero client width. */
int test_zoom_zero_client_w(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(0, 384, 8, 16), 1); /* 0 % 8 == 0 */
    TEST_END();
}

/* Zero term height. */
int test_zoom_zero_term_h(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_font_fits(640, 0, 8, 16), 1); /* 0 % 16 == 0 */
    TEST_END();
}
