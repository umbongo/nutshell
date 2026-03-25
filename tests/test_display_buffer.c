#include "test_framework.h"
#include "display_buffer.h"
#include <string.h>

/* ---- Init tests --------------------------------------------------------- */

int test_dispbuf_init_zeroed(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 4, 10);
    ASSERT_EQ(db.rows, 4);
    ASSERT_EQ(db.cols, 10);
    ASSERT_NOT_NULL(db.cells);
    /* All cells should have sentinel codepoint (dirty) after init */
    for (int i = 0; i < 4 * 10; i++) {
        ASSERT_EQ(db.cells[i].codepoint, 0xFFFFFFFFu);
    }
    dispbuf_free(&db);
    TEST_END();
}

int test_dispbuf_resize_clears(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 2, 5);
    /* Write something */
    dispbuf_cell_update(&db, 0, 0, 'A', 0xFF, 0x00, 1);
    /* Resize clears everything */
    dispbuf_resize(&db, 3, 8);
    ASSERT_EQ(db.rows, 3);
    ASSERT_EQ(db.cols, 8);
    ASSERT_EQ(db.cells[0].codepoint, 0xFFFFFFFFu);
    dispbuf_free(&db);
    TEST_END();
}

/* ---- Comparison tests --------------------------------------------------- */

int test_dispbuf_cell_match(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 1, 1);
    dispbuf_cell_update(&db, 0, 0, 'X', 0xAA, 0xBB, 2);
    ASSERT_TRUE(dispbuf_cell_clean(&db, 0, 0, 'X', 0xAA, 0xBB, 2));
    dispbuf_free(&db);
    TEST_END();
}

int test_dispbuf_cell_codepoint_diff(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 1, 1);
    dispbuf_cell_update(&db, 0, 0, 'A', 0xAA, 0xBB, 0);
    ASSERT_FALSE(dispbuf_cell_clean(&db, 0, 0, 'B', 0xAA, 0xBB, 0));
    dispbuf_free(&db);
    TEST_END();
}

int test_dispbuf_cell_fg_diff(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 1, 1);
    dispbuf_cell_update(&db, 0, 0, 'A', 0xFF, 0x00, 0);
    ASSERT_FALSE(dispbuf_cell_clean(&db, 0, 0, 'A', 0xFE, 0x00, 0));
    dispbuf_free(&db);
    TEST_END();
}

int test_dispbuf_cell_bg_diff(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 1, 1);
    dispbuf_cell_update(&db, 0, 0, 'A', 0xFF, 0x11, 0);
    ASSERT_FALSE(dispbuf_cell_clean(&db, 0, 0, 'A', 0xFF, 0x22, 0));
    dispbuf_free(&db);
    TEST_END();
}

int test_dispbuf_invalidate_all(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 2, 3);
    /* Update some cells to real content */
    dispbuf_cell_update(&db, 0, 0, 'A', 0, 0, 0);
    dispbuf_cell_update(&db, 1, 2, ' ', 0, 0, 0);
    /* Invalidate — every cell should compare as dirty */
    dispbuf_invalidate(&db);
    ASSERT_FALSE(dispbuf_cell_clean(&db, 0, 0, 'A', 0, 0, 0));
    ASSERT_FALSE(dispbuf_cell_clean(&db, 1, 2, ' ', 0, 0, 0));
    /* Even a zero cell won't match the sentinel */
    ASSERT_FALSE(dispbuf_cell_clean(&db, 0, 1, 0, 0, 0, 0));
    dispbuf_free(&db);
    TEST_END();
}

int test_dispbuf_free_null(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    memset(&db, 0, sizeof(db));
    dispbuf_free(&db);  /* should not crash */
    ASSERT_NULL(db.cells);
    TEST_END();
}

/* After init, zero-codepoint cells must still be dirty (not match empty cells)
 * so the renderer paints the background colour on every cell at least once. */
int test_dispbuf_init_all_dirty(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 2, 3);
    /* An empty terminal cell has codepoint=0, fg=0, bg=0, flags=0.
     * This must NOT match the initial display buffer state. */
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++)
            ASSERT_FALSE(dispbuf_cell_clean(&db, r, c, 0, 0, 0, 0));
    dispbuf_free(&db);
    TEST_END();
}

/* After resize, all cells must be dirty (same rationale as init). */
int test_dispbuf_resize_all_dirty(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 2, 3);
    dispbuf_cell_update(&db, 0, 0, 'A', 0xFF, 0x00, 1);
    dispbuf_resize(&db, 4, 5);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 5; c++)
            ASSERT_FALSE(dispbuf_cell_clean(&db, r, c, 0, 0, 0, 0));
    dispbuf_free(&db);
    TEST_END();
}

/* Simulate window maximize: display buffer dimensions must match new size
 * immediately after resize to prevent rendering artifacts (gaps, overwrites).
 * This tests the fix for the issue where maximizing before running commands
 * caused display corruption. */
int test_dispbuf_resize_dimensions_sync(void) {
    TEST_BEGIN();
    DisplayBuffer db;

    /* Start with small window dimensions (e.g., 24 rows × 80 cols) */
    dispbuf_init(&db, 24, 80);
    ASSERT_EQ(db.rows, 24);
    ASSERT_EQ(db.cols, 80);

    /* Populate some cells to simulate rendered content */
    dispbuf_cell_update(&db, 0, 0, 'l', 0xFFFFFF, 0x000000, 0);
    dispbuf_cell_update(&db, 0, 1, 's', 0xFFFFFF, 0x000000, 0);
    ASSERT_TRUE(dispbuf_cell_clean(&db, 0, 0, 'l', 0xFFFFFF, 0x000000, 0));

    /* Maximize window: resize to larger dimensions (e.g., 40 rows × 120 cols)
     * This simulates WM_SIZE handler calling dispbuf_resize() */
    dispbuf_resize(&db, 40, 120);

    /* Display buffer MUST have new dimensions immediately */
    ASSERT_EQ(db.rows, 40);
    ASSERT_EQ(db.cols, 120);

    /* All cells must be marked dirty (including the new cells) */
    for (int r = 0; r < 40; r++) {
        for (int c = 0; c < 120; c++) {
            ASSERT_FALSE(dispbuf_cell_clean(&db, r, c, 0, 0, 0, 0));
        }
    }

    /* Old content should not persist */
    ASSERT_FALSE(dispbuf_cell_clean(&db, 0, 0, 'l', 0xFFFFFF, 0x000000, 0));

    dispbuf_free(&db);
    TEST_END();
}

/* Test shrinking window (un-maximize) also updates dimensions correctly */
int test_dispbuf_resize_shrink_dimensions(void) {
    TEST_BEGIN();
    DisplayBuffer db;

    /* Start with large dimensions */
    dispbuf_init(&db, 50, 150);
    ASSERT_EQ(db.rows, 50);
    ASSERT_EQ(db.cols, 150);

    /* Shrink to smaller size */
    dispbuf_resize(&db, 20, 60);

    /* Dimensions must match the smaller size */
    ASSERT_EQ(db.rows, 20);
    ASSERT_EQ(db.cols, 60);

    /* All cells in the new smaller buffer must be dirty */
    for (int r = 0; r < 20; r++) {
        for (int c = 0; c < 60; c++) {
            ASSERT_FALSE(dispbuf_cell_clean(&db, r, c, 0, 0, 0, 0));
        }
    }

    dispbuf_free(&db);
    TEST_END();
}
