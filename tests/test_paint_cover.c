#include "test_framework.h"
#include "paint_cover.h"

/* paint_covers_rect: renderer_draw may clear a row's dirty flag only when
 * the paint rectangle contained the row's whole rectangle. The real bug
 * (2026-10-14): a 14px right-edge sliver paint (2891..2905) over content
 * ending at 2891 cleared every row's dirty flag while painting nothing,
 * stranding stale content on screen. Row geometry below: a 23px row at
 * y=48 (the tab strip height) unless stated. */

#define ROW_TOP 48L
#define ROW_BOT 71L

int test_paint_cover_full_rect_covers(void)
{
    TEST_BEGIN();
    /* Whole terminal invalidated: content 0..2891 in a 0..2905 client. */
    ASSERT_EQ(paint_covers_rect(0, 48, 2905, 1743, 0, ROW_TOP, 2891, ROW_BOT), 1);
    TEST_END();
}

int test_paint_cover_exact_bounds_cover(void)
{
    TEST_BEGIN();
    /* Paint equals the row rectangle exactly (right/bottom exclusive). */
    ASSERT_EQ(paint_covers_rect(0, ROW_TOP, 2891, ROW_BOT,
                                0, ROW_TOP, 2891, ROW_BOT), 1);
    TEST_END();
}

int test_paint_cover_right_sliver_does_not_cover(void)
{
    TEST_BEGIN();
    /* The actual failing case: right-edge gutter only. */
    ASSERT_EQ(paint_covers_rect(2891, 48, 2905, 1743, 0, ROW_TOP, 2891, ROW_BOT), 0);
    TEST_END();
}

int test_paint_cover_left_margin_still_covers(void)
{
    TEST_BEGIN();
    /* Content starts at a left margin; a full-client paint still covers. */
    ASSERT_EQ(paint_covers_rect(0, 48, 3000, 1743, 5, ROW_TOP, 2896, ROW_BOT), 1);
    TEST_END();
}

int test_paint_cover_missing_left_column_does_not_cover(void)
{
    TEST_BEGIN();
    /* Paint starts past the content's left edge: first column unpainted. */
    ASSERT_EQ(paint_covers_rect(10, 48, 3000, 1743, 5, ROW_TOP, 2896, ROW_BOT), 0);
    TEST_END();
}

int test_paint_cover_missing_right_column_does_not_cover(void)
{
    TEST_BEGIN();
    /* Paint stops one pixel short of the last column. */
    ASSERT_EQ(paint_covers_rect(0, 48, 2890, 1743, 0, ROW_TOP, 2891, ROW_BOT), 0);
    TEST_END();
}

int test_paint_cover_middle_band_does_not_cover(void)
{
    TEST_BEGIN();
    /* A horizontal band in the middle misses both edges. */
    ASSERT_EQ(paint_covers_rect(1000, 48, 1500, 1743, 0, ROW_TOP, 2891, ROW_BOT), 0);
    TEST_END();
}

int test_paint_cover_vertical_partial_row_does_not_cover(void)
{
    TEST_BEGIN();
    /* Full width, but the paint ends in the middle of the row (the
     * original bug turned 90 degrees): the row must stay dirty. */
    ASSERT_EQ(paint_covers_rect(0, 48, 2905, 60, 0, ROW_TOP, 2891, ROW_BOT), 0);
    /* ...and one that starts mid-row. */
    ASSERT_EQ(paint_covers_rect(0, 60, 2905, 1743, 0, ROW_TOP, 2891, ROW_BOT), 0);
    TEST_END();
}

int test_paint_cover_edge_touching_row_boundary(void)
{
    TEST_BEGIN();
    /* Paint bottom exactly at the row's exclusive bottom: covered. Paint
     * bottom one pixel less: not covered. Half-open on both sides. */
    ASSERT_EQ(paint_covers_rect(0, 48, 2905, ROW_BOT, 0, ROW_TOP, 2891, ROW_BOT), 1);
    ASSERT_EQ(paint_covers_rect(0, 48, 2905, ROW_BOT - 1, 0, ROW_TOP, 2891, ROW_BOT), 0);
    /* Paint top exactly at the row's top: covered. One pixel below: not. */
    ASSERT_EQ(paint_covers_rect(0, ROW_TOP, 2905, 1743, 0, ROW_TOP, 2891, ROW_BOT), 1);
    ASSERT_EQ(paint_covers_rect(0, ROW_TOP + 1, 2905, 1743, 0, ROW_TOP, 2891, ROW_BOT), 0);
    TEST_END();
}

int test_paint_cover_row_above_or_below_paint_does_not_cover(void)
{
    TEST_BEGIN();
    /* A full-width paint of rows 0-9 does not cover row 40. */
    ASSERT_EQ(paint_covers_rect(0, 48, 2905, 48 + 10 * 23,
                                0, 48 + 40 * 23, 2891, 48 + 41 * 23), 0);
    TEST_END();
}

int test_paint_cover_degenerate_rect_is_covered(void)
{
    TEST_BEGIN();
    /* Zero-width or zero-height row: nothing to miss, never pin dirty. */
    ASSERT_EQ(paint_covers_rect(100, 100, 100, 100, 0, 48, 0, 71), 1);
    ASSERT_EQ(paint_covers_rect(0, 0, 0, 0, 0, 48, 2891, 48), 1);
    ASSERT_EQ(paint_covers_rect(0, 0, 0, 0, 50, 48, 10, 71), 1);
    TEST_END();
}
