#include "test_framework.h"
#include "term.h"
#include <stdlib.h>
#include <string.h>

/* Terminal escape fixes: auto-wrap in a scroll region, the scroll region
 * across the alternate screen, settable tab stops. */

static void feed(Terminal *t, const char *s) { term_process(t, s, strlen(s)); }

static uint32_t cp(Terminal *t, int row, int col) {
    return t->lines[term_screen_to_phys(t, row)]->cells[col].codepoint;
}

/* Put a letter in column 0 of every row: row r holds 'A' + r. */
static void label_rows(Terminal *t, int rows) {
    for (int r = 0; r < rows; r++) {
        char b[32];
        snprintf(b, sizeof b, "\x1B[%d;1H%c", r + 1, 'A' + r);
        feed(t, b);
    }
}

/* ---- F1: auto-wrap and the scroll region ---- */

int test_term_wrap_scrolls_region_only(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 10, 20);
    label_rows(t, 10);
    feed(t, "\x1B[2;5r");              /* region rows 2-5 (1-based) = 1..4 */
    feed(t, "\x1B[5;1H");              /* on the region's bottom row */
    feed(t, "XXXXXXXXXXY");            /* fill the row, then wrap */
    ASSERT_EQ(t->cursor.row, 4);
    ASSERT_EQ(t->cursor.col, 1);
    ASSERT_EQ(cp(t, 4, 0), (uint32_t)'Y');
    ASSERT_EQ(cp(t, 3, 0), (uint32_t)'X');  /* old row 4 (with the Xs) moved up */
    ASSERT_EQ(cp(t, 1, 0), (uint32_t)'C');  /* old row 2 moved up to 1 */
    ASSERT_EQ(cp(t, 0, 0), (uint32_t)'A');
    ASSERT_EQ(cp(t, 5, 0), (uint32_t)'F');
    ASSERT_EQ(cp(t, 9, 0), (uint32_t)'J');
    ASSERT_EQ(t->lines_count, 10);          /* no scrollback gained */
    term_free(t);
    TEST_END();
}

int test_term_wrap_full_screen_unchanged(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 5, 20);
    feed(t, "\x1B[4;1HXXXXXY");
    ASSERT_EQ(t->cursor.row, 3);
    ASSERT_EQ(t->cursor.col, 1);
    ASSERT_EQ(t->lines_count, 5);           /* one row went to scrollback */
    ASSERT_EQ(cp(t, 3, 0), (uint32_t)'Y');
    ASSERT_EQ(cp(t, 2, 0), (uint32_t)'X');
    ASSERT_TRUE(t->lines[term_screen_to_phys(t, 3)]->wrapped);
    term_free(t);
    TEST_END();
}

int test_term_wrap_below_region_stays(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 5, 20);
    label_rows(t, 10);
    feed(t, "\x1B[2;5r");
    feed(t, "\x1B[10;1HXXXXXY");          /* last screen row, below region */
    ASSERT_EQ(t->cursor.row, 9);
    ASSERT_EQ(t->lines_count, 10);
    ASSERT_EQ(cp(t, 9, 0), (uint32_t)'Y');
    ASSERT_EQ(cp(t, 8, 0), (uint32_t)'I');  /* nothing scrolled */
    ASSERT_EQ(cp(t, 1, 0), (uint32_t)'B');
    term_free(t);
    TEST_END();
}

int test_term_wrap_inside_region_moves_down(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 5, 20);
    label_rows(t, 10);
    feed(t, "\x1B[2;5r\x1B[3;1HXXXXXY");
    ASSERT_EQ(t->cursor.row, 3);
    ASSERT_EQ(cp(t, 3, 0), (uint32_t)'Y');
    ASSERT_EQ(cp(t, 1, 0), (uint32_t)'B');  /* no scroll */
    term_free(t);
    TEST_END();
}
