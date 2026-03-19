/* test_cursor_scroll.c
 * Tests for cursor visibility during terminal scrollback.
 *
 * The renderer hides the cursor when scrollback_offset > 0 because the
 * cursor is on the active screen which is below the visible viewport.
 * These tests verify the logic that decides whether to draw the cursor,
 * mirroring the conditions in renderer.c resolve_cell() and renderer_draw().
 */

#include "test_framework.h"
#include <stdbool.h>

/* Mirror the cursor-visibility formula from renderer.c:
 *   show = cursor.visible && scrollback_offset == 0
 * Returns the effective cursor row (-1 if hidden). */
static int cursor_effective_row(bool visible, int scrollback_offset, int row)
{
    return (visible && scrollback_offset == 0) ? row : -1;
}

static int cursor_effective_col(bool visible, int scrollback_offset, int col)
{
    return (visible && scrollback_offset == 0) ? col : -1;
}

/* Mirror the per-cell reverse check from resolve_cell():
 *   is_cursor = cursor.visible && scrollback_offset == 0
 *               && cursor.row == row_idx && cursor.col == col_idx */
static bool cell_is_cursor(bool visible, int scrollback_offset,
                           int cursor_row, int cursor_col,
                           int row_idx, int col_idx)
{
    return visible && scrollback_offset == 0
           && cursor_row == row_idx && cursor_col == col_idx;
}

/* ---- cursor hidden when scrolled back ----------------------------------- */

int test_cursor_visible_at_bottom(void)
{
    TEST_BEGIN();
    /* scrollback_offset == 0: cursor should be visible at its position */
    ASSERT_EQ(cursor_effective_row(true, 0, 5), 5);
    ASSERT_EQ(cursor_effective_col(true, 0, 10), 10);
    TEST_END();
}

int test_cursor_hidden_when_scrolled(void)
{
    TEST_BEGIN();
    /* scrollback_offset > 0: cursor should be hidden */
    ASSERT_EQ(cursor_effective_row(true, 1, 5), -1);
    ASSERT_EQ(cursor_effective_col(true, 1, 10), -1);
    TEST_END();
}

int test_cursor_hidden_when_scrolled_far(void)
{
    TEST_BEGIN();
    /* Large scrollback_offset: still hidden */
    ASSERT_EQ(cursor_effective_row(true, 100, 5), -1);
    ASSERT_EQ(cursor_effective_col(true, 100, 10), -1);
    TEST_END();
}

int test_cursor_hidden_when_invisible(void)
{
    TEST_BEGIN();
    /* cursor.visible == false: hidden regardless of scroll */
    ASSERT_EQ(cursor_effective_row(false, 0, 5), -1);
    ASSERT_EQ(cursor_effective_col(false, 0, 10), -1);
    TEST_END();
}

int test_cursor_hidden_invisible_and_scrolled(void)
{
    TEST_BEGIN();
    /* Both invisible and scrolled */
    ASSERT_EQ(cursor_effective_row(false, 3, 5), -1);
    ASSERT_EQ(cursor_effective_col(false, 3, 10), -1);
    TEST_END();
}

/* ---- cell cursor check (resolve_cell reverse logic) --------------------- */

int test_cell_is_cursor_at_position(void)
{
    TEST_BEGIN();
    /* At bottom, matching position: is cursor */
    ASSERT_TRUE(cell_is_cursor(true, 0, 5, 10, 5, 10));
    TEST_END();
}

int test_cell_not_cursor_wrong_row(void)
{
    TEST_BEGIN();
    /* Wrong row */
    ASSERT_FALSE(cell_is_cursor(true, 0, 5, 10, 6, 10));
    TEST_END();
}

int test_cell_not_cursor_wrong_col(void)
{
    TEST_BEGIN();
    /* Wrong column */
    ASSERT_FALSE(cell_is_cursor(true, 0, 5, 10, 5, 11));
    TEST_END();
}

int test_cell_not_cursor_when_scrolled(void)
{
    TEST_BEGIN();
    /* Scrolled back: even at matching position, not cursor */
    ASSERT_FALSE(cell_is_cursor(true, 1, 5, 10, 5, 10));
    TEST_END();
}

int test_cell_not_cursor_when_invisible(void)
{
    TEST_BEGIN();
    /* Cursor not visible */
    ASSERT_FALSE(cell_is_cursor(false, 0, 5, 10, 5, 10));
    TEST_END();
}

int test_cell_not_cursor_scrolled_and_invisible(void)
{
    TEST_BEGIN();
    /* Both scrolled and invisible */
    ASSERT_FALSE(cell_is_cursor(false, 2, 5, 10, 5, 10));
    TEST_END();
}

int test_cursor_row_zero(void)
{
    TEST_BEGIN();
    /* Cursor at row 0, col 0 — should work at bottom */
    ASSERT_EQ(cursor_effective_row(true, 0, 0), 0);
    ASSERT_EQ(cursor_effective_col(true, 0, 0), 0);
    ASSERT_TRUE(cell_is_cursor(true, 0, 0, 0, 0, 0));
    /* But not when scrolled */
    ASSERT_FALSE(cell_is_cursor(true, 1, 0, 0, 0, 0));
    TEST_END();
}
