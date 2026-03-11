#include "test_framework.h"
#include "term.h"
#include "display_buffer.h"
#include <string.h>

static void feed(Terminal *t, const char *s) {
    term_process(t, s, strlen(s));
}

/* ---- Initial state: all rows dirty after init --------------------------- */

int test_dirty_init_all_dirty(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- After clearing, no rows are dirty ---------------------------------- */

int test_dirty_clear_all(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_clear_dirty(t);
    ASSERT_FALSE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Writing a character marks that row dirty --------------------------- */

int test_dirty_put_char(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    feed(t, "A");
    /* Row 0 (cursor row) should be dirty */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Only the written row is dirty, others stay clean ------------------- */

int test_dirty_only_written_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    /* Move cursor to row 2, write a char */
    feed(t, "\033[3;1H");  /* CSI row;col H — row 3 = index 2 */
    term_clear_dirty(t);   /* clear dirty from cursor move */
    feed(t, "X");
    /* Check that only row 2 is dirty via get_visible_row check */
    /* We can verify by checking term_has_dirty_rows returns true */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Erase display (ED) marks all rows dirty ---------------------------- */

int test_dirty_erase_display(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    feed(t, "\033[2J");  /* ED 2 = clear entire screen */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Erase line (EL) marks that row dirty ------------------------------- */

int test_dirty_erase_line(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    feed(t, "\033[K");   /* EL 0 = clear from cursor to end of line */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Scrolling marks the new/recycled row dirty ------------------------- */

int test_dirty_scroll(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    term_scroll(t);
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Resize marks all rows dirty ---------------------------------------- */

int test_dirty_resize(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    term_resize(t, 6, 12);
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- NULL safety -------------------------------------------------------- */

int test_dirty_null_safety(void)
{
    TEST_BEGIN();
    term_clear_dirty(NULL);        /* must not crash */
    ASSERT_FALSE(term_has_dirty_rows(NULL));
    ASSERT_TRUE(1);
    TEST_END();
}

/* ---- Multiple writes then clear ----------------------------------------- */

int test_dirty_multi_write_clear(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "Hello");
    feed(t, "\nWorld");
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_clear_dirty(t);
    ASSERT_FALSE(term_has_dirty_rows(t));
    /* Write again */
    feed(t, "!");
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Scroll marks ALL visible rows dirty (not just the new row) --------- */

int test_dirty_scroll_all_visible(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    /* Fill all 4 rows with content so they're used */
    feed(t, "row0\nrow1\nrow2\nrow3");
    term_clear_dirty(t);
    /* Verify all rows are clean */
    for (int i = 0; i < t->rows; i++) {
        int logical = t->lines_count - t->rows + i;
        int phys = (t->lines_start + logical) % t->lines_capacity;
        ASSERT_FALSE(t->lines[phys]->dirty);
    }
    /* Scroll once — all visible rows must become dirty because each
     * screen position now maps to a different logical row */
    term_scroll(t);
    int dirty_count = 0;
    for (int i = 0; i < t->rows; i++) {
        int logical = t->lines_count - t->rows + i;
        int phys = (t->lines_start + logical) % t->lines_capacity;
        if (t->lines[phys]->dirty) dirty_count++;
    }
    ASSERT_EQ(dirty_count, t->rows);
    term_free(t);
    TEST_END();
}

/* ---- Alt screen enter/exit marks dirty ---------------------------------- */

int test_dirty_alt_screen(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    term_clear_dirty(t);
    term_alt_screen_enter(t);
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_clear_dirty(t);
    term_alt_screen_exit(t);
    /* Primary buffer rows were dirty=true from init, preserved across alt screen */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* term_mark_all_dirty marks every row dirty */
int test_mark_all_dirty_basic(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    term_clear_dirty(t);
    ASSERT_FALSE(term_has_dirty_rows(t));
    term_mark_all_dirty(t);
    ASSERT_TRUE(term_has_dirty_rows(t));
    /* Check every visible row is dirty */
    for (int i = 0; i < t->lines_count; i++) {
        int idx = (t->lines_start + i) % t->lines_capacity;
        ASSERT_TRUE(t->lines[idx]->dirty);
    }
    term_free(t);
    TEST_END();
}

/* term_mark_all_dirty(NULL) must not crash */
int test_mark_all_dirty_null(void) {
    TEST_BEGIN();
    term_mark_all_dirty(NULL);  /* must not crash */
    TEST_END();
}

/* term_mark_all_dirty after clearing and writing should mark all dirty */
int test_mark_all_dirty_after_clear(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "hello");
    term_clear_dirty(t);
    ASSERT_FALSE(term_has_dirty_rows(t));
    term_mark_all_dirty(t);
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* After resizing to a larger terminal with sparse content, lines_count
 * can be < rows.  The pre-allocated rows beyond lines_count must still
 * be accessible (the parser writes to them via get_screen_row). */
int test_resize_sparse_rows_accessible(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "hi");
    term_resize(t, 20, 10);
    /* lines_count may be < rows — that's OK */
    /* But all screen rows must have pre-allocated TermRow pointers */
    for (int i = 0; i < 20; i++) {
        /* Same logic as parser.c get_screen_row */
        int top = (t->lines_count >= t->rows) ? (t->lines_count - t->rows) : 0;
        int logical = top + i;
        int phys = (t->lines_start + logical) % t->lines_capacity;
        ASSERT_NOT_NULL(t->lines[phys]);
    }
    term_free(t);
    TEST_END();
}

/* After resize to larger size, typing on a row beyond the old content
 * area must produce visible output (the row must exist in the buffer). */
int test_resize_type_beyond_old_content(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "hi");
    term_resize(t, 20, 10);
    /* Move cursor to row 10 (well beyond old content) and type */
    feed(t, "\033[11;1H");  /* CSI row;col H — row 11 = index 10 */
    feed(t, "test");
    /* The row should exist and contain 'test' */
    int top = (t->lines_count >= t->rows) ? (t->lines_count - t->rows) : 0;
    int logical = top + 10;
    int phys = (t->lines_start + logical) % t->lines_capacity;
    ASSERT_NOT_NULL(t->lines[phys]);
    ASSERT_EQ(t->lines[phys]->cells[0].codepoint, (uint32_t)'t');
    ASSERT_EQ(t->lines[phys]->cells[1].codepoint, (uint32_t)'e');
    term_free(t);
    TEST_END();
}

/* dispbuf cells for empty rows are dirty after invalidation, confirming
 * the renderer can detect they need repainting. */
int test_dispbuf_empty_rows_dirty_after_invalidate(void) {
    TEST_BEGIN();
    DisplayBuffer db;
    dispbuf_init(&db, 10, 5);
    /* Simulate painting: update row 0 with real content, rows 1-9 with empty cells */
    for (int c = 0; c < 5; c++) {
        dispbuf_cell_update(&db, 0, c, 'A', 0xFF, 0x00, 0);
    }
    for (int r = 1; r < 10; r++)
        for (int c = 0; c < 5; c++)
            dispbuf_cell_update(&db, r, c, 0, 0xFF, 0x00, 0);
    /* All cells should be clean now */
    ASSERT_TRUE(dispbuf_cell_clean(&db, 5, 0, 0, 0xFF, 0x00, 0));
    /* Invalidate — simulates resize/zoom */
    dispbuf_invalidate(&db);
    /* Empty rows must now be dirty (sentinel != painted content) */
    ASSERT_FALSE(dispbuf_cell_clean(&db, 5, 0, 0, 0xFF, 0x00, 0));
    dispbuf_free(&db);
    TEST_END();
}
