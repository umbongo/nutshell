#include "test_framework.h"
#include "term.h"
#include <stdlib.h>
#include <string.h>

/* Helper to get a cell from the screen (0-based row/col) */
static TermCell sr_get_cell(Terminal *term, int row, int col) {
    int top_logical = (term->lines_count >= term->rows)
                    ? (term->lines_count - term->rows) : 0;
    int logical_idx = top_logical + row;
    int physical_idx = (term->lines_start + logical_idx) % term->lines_capacity;
    return term->lines[physical_idx]->cells[col];
}

/* Helper: write a character at every column of a given screen row */
static void fill_row_char(Terminal *term, int row, char ch) {
    int top_logical = (term->lines_count >= term->rows)
                    ? (term->lines_count - term->rows) : 0;
    int logical_idx = top_logical + row;
    int physical_idx = (term->lines_start + logical_idx) % term->lines_capacity;
    TermRow *r = term->lines[physical_idx];
    for (int c = 0; c < term->cols; c++) {
        r->cells[c].codepoint = (uint32_t)ch;
    }
    r->len = term->cols;
}

/* ---- Scroll region init -------------------------------------------------- */

int test_sr_init_defaults(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 23);
    term_free(t);
    TEST_END();
}

int test_sr_resize_resets(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Set a custom scroll region via CSI r */
    term_process(t, "\x1B[5;20r", 7);
    ASSERT_EQ(t->scroll_top, 4);
    ASSERT_EQ(t->scroll_bot, 19);
    /* Resize should reset to full screen */
    term_resize(t, 30, 80);
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 29);
    term_free(t);
    TEST_END();
}

/* ---- DECSTBM (CSI r) ----------------------------------------------------- */

int test_sr_decstbm_set(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* ESC[5;20r  → scroll_top=4, scroll_bot=19 (1-based → 0-based) */
    term_process(t, "\x1B[5;20r", 7);
    ASSERT_EQ(t->scroll_top, 4);
    ASSERT_EQ(t->scroll_bot, 19);
    term_free(t);
    TEST_END();
}

int test_sr_decstbm_reset(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "\x1B[5;20r", 7);
    /* ESC[r with no params resets to full screen */
    term_process(t, "\x1B[r", 3);
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 23);
    term_free(t);
    TEST_END();
}

int test_sr_decstbm_homes_cursor(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "Hello", 5);
    ASSERT_EQ(t->cursor.col, 5);
    ASSERT_EQ(t->cursor.row, 0);
    /* Setting scroll region moves cursor to home */
    term_process(t, "\x1B[5;20r", 7);
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 0);
    term_free(t);
    TEST_END();
}

/* ---- term_scroll_up ------------------------------------------------------ */

int test_sr_scroll_up_region(void) {
    TEST_BEGIN();
    /* 6-row terminal, scroll region rows 1-4, rows 0 and 5 should be untouched */
    Terminal *t = term_init(6, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');
    fill_row_char(t, 4, 'E');
    fill_row_char(t, 5, 'F');

    term_scroll_up(t, 1, 4, 1);

    /* Row 0 untouched */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    /* Region shifted up: row1←C, row2←D, row3←E, row4←cleared */
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'D');
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 'E');
    ASSERT_EQ(sr_get_cell(t, 4, 0).codepoint, 0); /* cleared */
    /* Row 5 untouched */
    ASSERT_EQ(sr_get_cell(t, 5, 0).codepoint, 'F');

    term_free(t);
    TEST_END();
}

int test_sr_scroll_up_full_screen(void) {
    TEST_BEGIN();
    /* Full-screen scroll should still work (delegates to term_scroll for scrollback) */
    Terminal *t = term_init(4, 10, 10);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');

    term_scroll_up(t, 0, 3, 1);

    /* Row 0 ← B, Row 1 ← C, Row 2 ← D, Row 3 ← cleared */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'B');
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'D');
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 0);

    term_free(t);
    TEST_END();
}

/* ---- term_scroll_down ---------------------------------------------------- */

int test_sr_scroll_down_region(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');
    fill_row_char(t, 4, 'E');
    fill_row_char(t, 5, 'F');

    term_scroll_down(t, 1, 4, 1);

    /* Row 0 untouched */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    /* Region shifted down: row1←cleared, row2←B, row3←C, row4←D */
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 0); /* cleared */
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'B');
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 4, 0).codepoint, 'D');
    /* Row 5 untouched */
    ASSERT_EQ(sr_get_cell(t, 5, 0).codepoint, 'F');

    term_free(t);
    TEST_END();
}

/* ---- Newline at scroll_bot ----------------------------------------------- */

int test_sr_newline_at_scroll_bot(void) {
    TEST_BEGIN();
    /* 6-row terminal, scroll region rows 1-4 */
    Terminal *t = term_init(6, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');
    fill_row_char(t, 4, 'E');
    fill_row_char(t, 5, 'F');

    t->scroll_top = 1;
    t->scroll_bot = 4;
    t->cursor.row = 4; /* at scroll_bot */

    /* LF should scroll the region, not the whole screen */
    term_process(t, "\n", 1);

    /* Cursor stays at scroll_bot */
    ASSERT_EQ(t->cursor.row, 4);
    /* Row 0 untouched */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    /* Region scrolled: row1←C, row2←D, row3←E, row4←cleared */
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'D');
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 'E');
    ASSERT_EQ(sr_get_cell(t, 4, 0).codepoint, 0);
    /* Row 5 untouched */
    ASSERT_EQ(sr_get_cell(t, 5, 0).codepoint, 'F');

    term_free(t);
    TEST_END();
}

int test_sr_newline_inside_region(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    t->scroll_top = 1;
    t->scroll_bot = 4;
    t->cursor.row = 2; /* inside region, not at bottom */

    term_process(t, "\n", 1);

    /* Cursor should just move down */
    ASSERT_EQ(t->cursor.row, 3);

    term_free(t);
    TEST_END();
}

/* ---- Reverse Index (ESC M) ----------------------------------------------- */

int test_sr_reverse_index_at_top(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');
    fill_row_char(t, 4, 'E');
    fill_row_char(t, 5, 'F');

    t->scroll_top = 1;
    t->scroll_bot = 4;
    t->cursor.row = 1; /* at scroll_top */

    /* ESC M: reverse index at top of scroll region → scroll down */
    term_process(t, "\x1BM", 2);

    /* Cursor stays at scroll_top */
    ASSERT_EQ(t->cursor.row, 1);
    /* Row 0 untouched */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    /* Region scrolled down: row1←cleared, row2←B, row3←C, row4←D */
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 0);
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'B');
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 4, 0).codepoint, 'D');
    /* Row 5 untouched */
    ASSERT_EQ(sr_get_cell(t, 5, 0).codepoint, 'F');

    term_free(t);
    TEST_END();
}

int test_sr_reverse_index_inside(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    t->scroll_top = 1;
    t->scroll_bot = 4;
    t->cursor.row = 3; /* inside region, not at top */

    term_process(t, "\x1BM", 2);

    /* Cursor moves up */
    ASSERT_EQ(t->cursor.row, 2);

    term_free(t);
    TEST_END();
}

/* ---- Insert Lines (CSI L) ------------------------------------------------ */

int test_sr_insert_lines(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');
    fill_row_char(t, 4, 'E');
    fill_row_char(t, 5, 'F');

    t->scroll_top = 0;
    t->scroll_bot = 5;
    t->cursor.row = 2; /* insert at row 2 */

    /* CSI 1 L — insert 1 line */
    term_process(t, "\x1B[1L", 4);

    /* Rows 0-1 untouched */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'B');
    /* Row 2 is now blank (inserted) */
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 0);
    /* Original C,D,E shifted down */
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 4, 0).codepoint, 'D');
    ASSERT_EQ(sr_get_cell(t, 5, 0).codepoint, 'E');
    /* F fell off the bottom */

    term_free(t);
    TEST_END();
}

/* ---- Delete Lines (CSI M) ------------------------------------------------ */

int test_sr_delete_lines(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');
    fill_row_char(t, 4, 'E');
    fill_row_char(t, 5, 'F');

    t->scroll_top = 0;
    t->scroll_bot = 5;
    t->cursor.row = 2; /* delete at row 2 */

    /* CSI 1 M — delete 1 line */
    term_process(t, "\x1B[1M", 4);

    /* Rows 0-1 untouched */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'B');
    /* Row 2 ← D (C deleted), row 3 ← E, row 4 ← F */
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'D');
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 'E');
    ASSERT_EQ(sr_get_cell(t, 4, 0).codepoint, 'F');
    /* Row 5 cleared (vacated) */
    ASSERT_EQ(sr_get_cell(t, 5, 0).codepoint, 0);

    term_free(t);
    TEST_END();
}

/* ---- Scroll Up/Down (CSI S / CSI T) -------------------------------------- */

int test_sr_csi_scroll_up(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');

    t->scroll_top = 0;
    t->scroll_bot = 3;

    /* CSI 1 S — scroll up 1 */
    term_process(t, "\x1B[1S", 4);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'B');
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'D');
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 0);

    term_free(t);
    TEST_END();
}

int test_sr_csi_scroll_down(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');

    t->scroll_top = 0;
    t->scroll_bot = 3;

    /* CSI 1 T — scroll down 1 */
    term_process(t, "\x1B[1T", 4);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 0);
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'A');
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'B');
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 'C');

    term_free(t);
    TEST_END();
}

/* ---- Insert/Delete Characters (CSI @ / CSI P) ---------------------------- */

int test_sr_insert_chars(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    /* Write "ABCDE" at row 0 */
    term_process(t, "ABCDE", 5);
    t->cursor.row = 0;
    t->cursor.col = 2; /* position at 'C' */

    /* CSI 2 @ — insert 2 chars at col 2 */
    term_process(t, "\x1B[2@", 4);

    /* A B _ _ C D E _ _ _ */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    ASSERT_EQ(sr_get_cell(t, 0, 1).codepoint, 'B');
    ASSERT_EQ(sr_get_cell(t, 0, 2).codepoint, 0); /* inserted blank */
    ASSERT_EQ(sr_get_cell(t, 0, 3).codepoint, 0); /* inserted blank */
    ASSERT_EQ(sr_get_cell(t, 0, 4).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 0, 5).codepoint, 'D');
    ASSERT_EQ(sr_get_cell(t, 0, 6).codepoint, 'E');

    term_free(t);
    TEST_END();
}

int test_sr_delete_chars(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    /* Write "ABCDE" at row 0 */
    term_process(t, "ABCDE", 5);
    t->cursor.row = 0;
    t->cursor.col = 1; /* position at 'B' */

    /* CSI 2 P — delete 2 chars at col 1 */
    term_process(t, "\x1B[2P", 4);

    /* A D E _ _ _ _ _ _ _ */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    ASSERT_EQ(sr_get_cell(t, 0, 1).codepoint, 'D');
    ASSERT_EQ(sr_get_cell(t, 0, 2).codepoint, 'E');
    ASSERT_EQ(sr_get_cell(t, 0, 3).codepoint, 0); /* cleared */

    term_free(t);
    TEST_END();
}

/* ---- ED mode 0 / mode 1 ------------------------------------------------- */

int test_sr_ed_mode0(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');

    t->cursor.row = 1;
    t->cursor.col = 5;

    /* CSI 0 J — clear from cursor to end of screen */
    term_process(t, "\x1B[0J", 4);

    /* Row 0 untouched */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    /* Row 1: cols 0-4 untouched, cols 5-9 cleared */
    ASSERT_EQ(sr_get_cell(t, 1, 4).codepoint, 'B');
    ASSERT_EQ(sr_get_cell(t, 1, 5).codepoint, 0);
    ASSERT_EQ(sr_get_cell(t, 1, 9).codepoint, 0);
    /* Rows 2-3 fully cleared */
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 0);
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 0);

    term_free(t);
    TEST_END();
}

int test_sr_ed_mode1(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');

    t->cursor.row = 2;
    t->cursor.col = 3;

    /* CSI 1 J — clear from start of screen to cursor */
    term_process(t, "\x1B[1J", 4);

    /* Rows 0-1 fully cleared */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 0);
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 0);
    /* Row 2: cols 0-3 cleared, cols 4-9 untouched */
    ASSERT_EQ(sr_get_cell(t, 2, 3).codepoint, 0);
    ASSERT_EQ(sr_get_cell(t, 2, 4).codepoint, 'C');
    /* Row 3 untouched */
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 'D');

    term_free(t);
    TEST_END();
}

/* ---- Alt screen resets scroll region ------------------------------------- */

int test_sr_alt_screen_resets(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    t->scroll_top = 5;
    t->scroll_bot = 20;

    /* Enter alt screen should reset scroll region */
    term_process(t, "\x1B[?1049h", 8);
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 23);

    /* Set region on alt screen */
    term_process(t, "\x1B[2;10r", 7);
    ASSERT_EQ(t->scroll_top, 1);
    ASSERT_EQ(t->scroll_bot, 9);

    /* Exit alt screen should reset scroll region */
    term_process(t, "\x1B[?1049l", 8);
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 23);

    term_free(t);
    TEST_END();
}

/* ---- Nano-like simulation ------------------------------------------------ */

int test_sr_nano_simulation(void) {
    TEST_BEGIN();
    /*
     * Simulate nano's typical scroll pattern:
     * 1. Set scroll region to rows 2-21 (leaving status lines at top and bottom)
     * 2. Position cursor at bottom of region
     * 3. Scroll up (nano uses CSI S or LF at region bottom)
     * 4. Write new content on the vacated line
     */
    Terminal *t = term_init(24, 40, 0);

    /* Write something on the status line (row 0) */
    term_process(t, "  GNU nano 7.2", 14);

    /* Set scroll region: rows 3-22 (1-based) → 2-21 (0-based) */
    term_process(t, "\x1B[3;22r", 7);
    ASSERT_EQ(t->scroll_top, 2);
    ASSERT_EQ(t->scroll_bot, 21);

    /* Move cursor to row 21 (scroll_bot), write content */
    term_process(t, "\x1B[22;1H", 7); /* row 22 1-based = row 21 0-based */
    ASSERT_EQ(t->cursor.row, 21);
    term_process(t, "last line of content", 20);

    /* Scroll content region up by 1 (CSI S) */
    term_process(t, "\x1B[1S", 4);

    /* Row 21 should now be blank (vacated by scroll) */
    ASSERT_EQ(sr_get_cell(t, 21, 0).codepoint, 0);

    /* Status line (row 0) should be untouched */
    ASSERT_EQ(sr_get_cell(t, 0, 2).codepoint, 'G');
    ASSERT_EQ(sr_get_cell(t, 0, 3).codepoint, 'N');

    term_free(t);
    TEST_END();
}

/* ---- Edge cases ---------------------------------------------------------- */

int test_sr_scroll_up_multiple(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');
    fill_row_char(t, 4, 'E');
    fill_row_char(t, 5, 'F');

    /* Scroll region [1..4] up by 2 */
    term_scroll_up(t, 1, 4, 2);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A'); /* untouched */
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'D'); /* was row 3 */
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'E'); /* was row 4 */
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 0);   /* cleared */
    ASSERT_EQ(sr_get_cell(t, 4, 0).codepoint, 0);   /* cleared */
    ASSERT_EQ(sr_get_cell(t, 5, 0).codepoint, 'F'); /* untouched */

    term_free(t);
    TEST_END();
}

int test_sr_scroll_down_multiple(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');
    fill_row_char(t, 4, 'E');
    fill_row_char(t, 5, 'F');

    /* Scroll region [1..4] down by 2 */
    term_scroll_down(t, 1, 4, 2);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A'); /* untouched */
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 0);   /* cleared */
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 0);   /* cleared */
    ASSERT_EQ(sr_get_cell(t, 3, 0).codepoint, 'B'); /* was row 1 */
    ASSERT_EQ(sr_get_cell(t, 4, 0).codepoint, 'C'); /* was row 2 */
    ASSERT_EQ(sr_get_cell(t, 5, 0).codepoint, 'F'); /* untouched */

    term_free(t);
    TEST_END();
}

int test_sr_decstbm_invalid(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* top == bot is invalid → should reset to full screen */
    term_process(t, "\x1B[5;5r", 6);
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 23);
    /* top > bot is invalid → should reset to full screen */
    term_process(t, "\x1B[10;5r", 7);
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 23);
    term_free(t);
    TEST_END();
}

/* ---- ESC 7/8 — DECSC / DECRC -------------------------------------------- */

int test_sr_save_restore_cursor(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* Move cursor to row 5, col 10 */
    term_process(t, "\x1B[6;11H", 7); /* 1-based */
    ASSERT_EQ(t->cursor.row, 5);
    ASSERT_EQ(t->cursor.col, 10);

    /* ESC 7 — save cursor */
    term_process(t, "\x1B" "7", 2);

    /* Move cursor elsewhere */
    term_process(t, "\x1B[1;1H", 6);
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 0);

    /* ESC 8 — restore cursor */
    term_process(t, "\x1B" "8", 2);
    ASSERT_EQ(t->cursor.row, 5);
    ASSERT_EQ(t->cursor.col, 10);

    term_free(t);
    TEST_END();
}

int test_sr_save_restore_shares_slot(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* ESC 7 saves at row 3, col 7 */
    term_process(t, "\x1B[4;8H", 6);
    term_process(t, "\x1B" "7", 2);

    /* CSI u restores the same slot */
    term_process(t, "\x1B[1;1H", 6);
    term_process(t, "\x1B[u", 3);
    ASSERT_EQ(t->cursor.row, 3);
    ASSERT_EQ(t->cursor.col, 7);

    /* CSI s saves, ESC 8 restores the same slot */
    term_process(t, "\x1B[10;20H", 8);
    term_process(t, "\x1B[s", 3);
    term_process(t, "\x1B[1;1H", 6);
    term_process(t, "\x1B" "8", 2);
    ASSERT_EQ(t->cursor.row, 9);
    ASSERT_EQ(t->cursor.col, 19);

    term_free(t);
    TEST_END();
}

int test_sr_scroll_up_with_save_restore(void) {
    TEST_BEGIN();
    /*
     * Simulate ncurses scroll-reverse pattern:
     * 1. ESC 7 (save cursor at editing position)
     * 2. CUP to top of scroll region
     * 3. ESC M (reverse index — scroll content down)
     * 4. Write new content at top
     * 5. ESC 8 (restore cursor to editing position)
     */
    Terminal *t = term_init(24, 40, 0);

    /* Enter alt screen */
    term_process(t, "\x1B[?1049h", 8);

    /* Set scroll region: rows 2-22 (1-based) → 1-21 (0-based) */
    term_process(t, "\x1B[2;22r", 7);

    /* Fill rows with identifiable content */
    for (int r = 1; r <= 21; r++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "\x1B[%d;1H" "Line%02d", r + 1, r);
        term_process(t, buf, (size_t)len);
    }

    /* Position cursor at row 10 col 5 (editing position) */
    term_process(t, "\x1B[11;6H", 7);
    ASSERT_EQ(t->cursor.row, 10);
    ASSERT_EQ(t->cursor.col, 5);

    /* ESC 7 — save cursor */
    term_process(t, "\x1B" "7", 2);

    /* Move to top of scroll region */
    term_process(t, "\x1B[2;1H", 6);
    ASSERT_EQ(t->cursor.row, 1);

    /* ESC M — reverse index at scroll_top → scroll content down */
    term_process(t, "\x1BM", 2);
    ASSERT_EQ(t->cursor.row, 1);

    /* Row 1 should now be blank (new line inserted) */
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 0);
    /* Row 2 should have what was Line01 */
    ASSERT_EQ(sr_get_cell(t, 2, 0).codepoint, 'L');

    /* Write new content at the blank top line */
    term_process(t, "NewTop", 6);

    /* ESC 8 — restore cursor to editing position */
    term_process(t, "\x1B" "8", 2);
    ASSERT_EQ(t->cursor.row, 10);
    ASSERT_EQ(t->cursor.col, 5);

    /* Verify row 1 has the new content */
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'N');
    ASSERT_EQ(sr_get_cell(t, 1, 5).codepoint, 'p');

    term_free(t);
    TEST_END();
}

/* ---- ESC E — NEL (Next Line) --------------------------------------------- */

int test_sr_next_line(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    t->cursor.row = 2;
    t->cursor.col = 5;

    /* ESC E = CR + LF */
    term_process(t, "\x1B" "E", 2);
    ASSERT_EQ(t->cursor.row, 3);
    ASSERT_EQ(t->cursor.col, 0);

    term_free(t);
    TEST_END();
}

int test_sr_next_line_at_scroll_bot(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 10, 0);
    fill_row_char(t, 0, 'A');
    fill_row_char(t, 1, 'B');
    fill_row_char(t, 2, 'C');
    fill_row_char(t, 3, 'D');
    fill_row_char(t, 4, 'E');
    fill_row_char(t, 5, 'F');

    t->scroll_top = 1;
    t->scroll_bot = 4;
    t->cursor.row = 4;
    t->cursor.col = 5;

    /* ESC E at scroll_bot: col→0, scroll region up */
    term_process(t, "\x1B" "E", 2);
    ASSERT_EQ(t->cursor.row, 4);
    ASSERT_EQ(t->cursor.col, 0);
    /* Row 0 untouched */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    /* Region scrolled up */
    ASSERT_EQ(sr_get_cell(t, 1, 0).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 4, 0).codepoint, 0); /* cleared */
    /* Row 5 untouched */
    ASSERT_EQ(sr_get_cell(t, 5, 0).codepoint, 'F');

    term_free(t);
    TEST_END();
}

/* ---- CSI X — ECH (Erase Characters) ------------------------------------- */

int test_sr_erase_chars(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_process(t, "ABCDEFGHIJ", 10);
    t->cursor.row = 0;
    t->cursor.col = 3;

    /* CSI 4 X — erase 4 chars from cursor, cursor stays */
    term_process(t, "\x1B[4X", 4);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    ASSERT_EQ(sr_get_cell(t, 0, 2).codepoint, 'C');
    ASSERT_EQ(sr_get_cell(t, 0, 3).codepoint, 0); /* erased */
    ASSERT_EQ(sr_get_cell(t, 0, 6).codepoint, 0); /* erased */
    ASSERT_EQ(sr_get_cell(t, 0, 7).codepoint, 'H'); /* not erased */
    /* Cursor didn't move */
    ASSERT_EQ(t->cursor.col, 3);

    term_free(t);
    TEST_END();
}

/* ---- ICH/DCH edge cases ------------------------------------------------- */

int test_sr_ich_at_line_end(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_process(t, "ABCDEFGHIJ", 10);
    t->cursor.row = 0;
    t->cursor.col = 8; /* near end */

    /* CSI 5 @ — insert 5 chars near end, everything past cursor cleared */
    term_process(t, "\x1B[5@", 4);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    ASSERT_EQ(sr_get_cell(t, 0, 7).codepoint, 'H');
    ASSERT_EQ(sr_get_cell(t, 0, 8).codepoint, 0); /* inserted blank */
    ASSERT_EQ(sr_get_cell(t, 0, 9).codepoint, 0); /* inserted blank */
    /* I and J pushed off screen */

    term_free(t);
    TEST_END();
}

int test_sr_dch_at_line_end(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_process(t, "ABCDEFGHIJ", 10);
    t->cursor.row = 0;
    t->cursor.col = 7; /* at 'H' */

    /* CSI 5 P — delete 5 chars from col 7, more than remaining */
    term_process(t, "\x1B[5P", 4);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'A');
    ASSERT_EQ(sr_get_cell(t, 0, 6).codepoint, 'G');
    /* Cols 7-9 should be cleared (nothing to shift in) */
    ASSERT_EQ(sr_get_cell(t, 0, 7).codepoint, 0);
    ASSERT_EQ(sr_get_cell(t, 0, 8).codepoint, 0);
    ASSERT_EQ(sr_get_cell(t, 0, 9).codepoint, 0);

    term_free(t);
    TEST_END();
}

/* ---- Horizontal scroll simulation --------------------------------------- */

int test_sr_horizontal_nav_simulation(void) {
    TEST_BEGIN();
    /*
     * Simulate nano horizontal scroll: move cursor right past visible area,
     * nano redraws using CUP + EL + text output
     */
    Terminal *t = term_init(4, 10, 0);

    /* Write initial content */
    term_process(t, "0123456789", 10);

    /* nano redraws the line with shifted content:
     * CUP to row 1, col 1; EL (clear to end); write new visible text */
    term_process(t, "\x1B[1;1H", 6);  /* cursor to row 0 */
    term_process(t, "\x1B[K", 3);     /* clear to end of line */
    term_process(t, "3456789ABC", 10); /* write shifted content */

    /* Verify */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, '3');
    ASSERT_EQ(sr_get_cell(t, 0, 9).codepoint, 'C');

    term_free(t);
    TEST_END();
}

/* ---- SCS (Select Character Set) ----------------------------------------- */

int test_scs_esc_paren_b_no_leak(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);

    /* ESC ( B — select ASCII for G0; must not print "B" */
    term_process(t, "\x1B" "(B", 3);

    /* Screen should be blank — codepoint 0 at (0,0) */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 0u);
    /* Cursor should still be at origin */
    ASSERT_EQ(t->cursor.col, 0);
    ASSERT_EQ(t->cursor.row, 0);

    term_free(t);
    TEST_END();
}

int test_scs_esc_rparen_0_no_leak(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);

    /* ESC ) 0 — select line-drawing for G1; must not print "0" */
    term_process(t, "\x1B" ")0", 3);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 0u);
    ASSERT_EQ(t->cursor.col, 0);

    term_free(t);
    TEST_END();
}

int test_scs_esc_hash_no_leak(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);

    /* ESC # 8 — DECALN (fill screen with 'E'); we don't implement it,
     * but must not print "8" as a literal character. */
    term_process(t, "\x1B" "#8", 3);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 0u);
    ASSERT_EQ(t->cursor.col, 0);

    term_free(t);
    TEST_END();
}

int test_scs_cursor_unchanged(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);

    /* Move cursor to (1, 5) then send SCS sequences */
    term_process(t, "\x1B[2;6H", 6);  /* CUP to row 1, col 5 */
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 5);

    term_process(t, "\x1B" "(B", 3);
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 5);

    term_process(t, "\x1B" ")0", 3);
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 5);

    term_free(t);
    TEST_END();
}

int test_scs_text_after_scs(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);

    /* SCS followed by normal text — text should appear starting at col 0 */
    term_process(t, "\x1B" "(B" "Hello", 8);

    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, 'H');
    ASSERT_EQ(sr_get_cell(t, 0, 4).codepoint, 'o');
    ASSERT_EQ(t->cursor.col, 5);

    term_free(t);
    TEST_END();
}

int test_scs_nano_statusbar_sim(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 40, 100);

    /* Simulate what nano sends for the status bar:
     * ESC(B ESC[m ESC[7m ^G ESC[27m Help
     * The ESC(B resets charset, ESC[m resets attrs, ESC[7m enables reverse,
     * ESC[27m disables reverse. No "B" should appear. */
    term_process(t, "\x1B" "(B" "\x1B[m" "\x1B[7m" "^G" "\x1B[27m" " Help", 22);

    /* "^G Help" should start at col 0 */
    ASSERT_EQ(sr_get_cell(t, 0, 0).codepoint, '^');
    ASSERT_EQ(sr_get_cell(t, 0, 1).codepoint, 'G');
    ASSERT_EQ(sr_get_cell(t, 0, 2).codepoint, ' ');
    ASSERT_EQ(sr_get_cell(t, 0, 3).codepoint, 'H');
    ASSERT_EQ(sr_get_cell(t, 0, 4).codepoint, 'e');
    ASSERT_EQ(sr_get_cell(t, 0, 5).codepoint, 'l');
    ASSERT_EQ(sr_get_cell(t, 0, 6).codepoint, 'p');
    ASSERT_EQ(t->cursor.col, 7);

    term_free(t);
    TEST_END();
}
