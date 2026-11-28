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

/* ---- F2: scroll region across the alternate screen ---- */

int test_term_alt_screen_keeps_region(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 10, 20);
    feed(t, "\x1B[3;7r");
    feed(t, "\x1B[?1049h");
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 9);
    feed(t, "\x1B[?1049l");
    ASSERT_EQ(t->scroll_top, 2);
    ASSERT_EQ(t->scroll_bot, 6);
    term_free(t);
    TEST_END();
}

int test_term_alt_screen_region_resize(void) {
    TEST_BEGIN();
    /* fits after the resize: kept */
    Terminal *t = term_init(10, 10, 20);
    feed(t, "\x1B[3;7r\x1B[?1049h");
    ASSERT_TRUE(term_resize(t, 8, 10));
    feed(t, "\x1B[?1049l");
    ASSERT_EQ(t->scroll_top, 2);
    ASSERT_EQ(t->scroll_bot, 6);
    term_free(t);
    /* no longer fits: full screen */
    t = term_init(10, 10, 20);
    feed(t, "\x1B[3;9r\x1B[?1049h");
    ASSERT_TRUE(term_resize(t, 6, 10));
    feed(t, "\x1B[?1049l");
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 5);
    term_free(t);
    /* grown: kept */
    t = term_init(10, 10, 20);
    feed(t, "\x1B[3;9r\x1B[?1049h");
    ASSERT_TRUE(term_resize(t, 14, 10));
    feed(t, "\x1B[?1049l");
    ASSERT_EQ(t->scroll_top, 2);
    ASSERT_EQ(t->scroll_bot, 8);
    term_free(t);
    TEST_END();
}

int test_term_alt_screen_region_repeat(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 10, 20);
    feed(t, "\x1B[3;7r");
    feed(t, "\x1B[?1049h\x1B[2;4r\x1B[?1049h");   /* second enter ignored */
    feed(t, "\x1B[?1049l\x1B[?1049l");            /* second leave ignored */
    ASSERT_EQ(t->scroll_top, 2);
    ASSERT_EQ(t->scroll_bot, 6);
    term_free(t);
    TEST_END();
}

/* ---- F3: settable tab stops ---- */

int test_term_tab_defaults(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 0);
    feed(t, "\t");  ASSERT_EQ(t->cursor.col, 8);
    feed(t, "\t");  ASSERT_EQ(t->cursor.col, 16);
    feed(t, "\t");  ASSERT_EQ(t->cursor.col, 19);   /* no stop left: last column */
    feed(t, "\x1B[1;1H\x1B[3I");  ASSERT_EQ(t->cursor.col, 19);
    feed(t, "\x1B[1;1H\x1B[2I");  ASSERT_EQ(t->cursor.col, 16);
    feed(t, "\x1B[I");            ASSERT_EQ(t->cursor.col, 19);
    feed(t, "\x1B[Z");            ASSERT_EQ(t->cursor.col, 16);
    feed(t, "\x1B[2Z");           ASSERT_EQ(t->cursor.col, 0);
    feed(t, "\x1B[Z");            ASSERT_EQ(t->cursor.col, 0);   /* CBT at column 0 */
    term_free(t);
    TEST_END();
}

int test_term_tab_hts_and_tbc(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 40, 0);
    feed(t, "\x1B[1;4H\x1BH");                       /* HTS at col 3 */
    feed(t, "\x1B[1;1H\t");   ASSERT_EQ(t->cursor.col, 3);
    feed(t, "\t");            ASSERT_EQ(t->cursor.col, 8);
    feed(t, "\x1B[Z");        ASSERT_EQ(t->cursor.col, 3);
    feed(t, "\x1B[g");                               /* clear at cursor (col 3) */
    feed(t, "\x1B[1;1H\t");   ASSERT_EQ(t->cursor.col, 8);
    feed(t, "\x1B[0g");                              /* clear col 8 */
    feed(t, "\x1B[1;1H\t");   ASSERT_EQ(t->cursor.col, 16);
    feed(t, "\x1B[2g\x1B[1;1H\t");                   /* other parameters ignored */
    ASSERT_EQ(t->cursor.col, 16);
    feed(t, "\x1B[1;9H\x1BH");                       /* put col 8 back */
    feed(t, "\x1B[1;1H\t");   ASSERT_EQ(t->cursor.col, 8);
    term_free(t);
    TEST_END();
}

int test_term_tab_clear_all(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 40, 0);
    feed(t, "\x1B[3g");
    feed(t, "\t");            ASSERT_EQ(t->cursor.col, 39);   /* last column */
    feed(t, "\x1B[1;1H\x1B[5I");  ASSERT_EQ(t->cursor.col, 39);
    feed(t, "\x1B[Z");        ASSERT_EQ(t->cursor.col, 0);
    /* a stop at the last column */
    feed(t, "\x1B[1;40H\x1BH\x1B[1;1H\t");
    ASSERT_EQ(t->cursor.col, 39);
    feed(t, "\x1B[1;1H\x1B[1;20H\x1BH\x1B[1;1H\t");
    ASSERT_EQ(t->cursor.col, 19);
    term_free(t);
    TEST_END();
}

int test_term_tab_cht_count_beyond_stops(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 30, 0);
    feed(t, "\x1B[65535I");
    ASSERT_EQ(t->cursor.col, 29);
    feed(t, "\x1B[65535Z");
    ASSERT_EQ(t->cursor.col, 0);
    term_free(t);
    TEST_END();
}

int test_term_tab_reset_resize_alt(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 0);
    feed(t, "\x1B[3g\x1B[1;4H\x1BH");               /* only a stop at col 3 */
    feed(t, "\x1B[?1049h\x1B[1;1H\t");               /* alt screen shares them */
    ASSERT_EQ(t->cursor.col, 3);
    feed(t, "\x1B[1;6H\x1BH\x1B[?1049l");            /* ...and set ones persist */
    feed(t, "\x1B[1;1H\x1B[2I");
    ASSERT_EQ(t->cursor.col, 5);
    /* resize keeps the stops, new columns get every 8 */
    ASSERT_TRUE(term_resize(t, 5, 40));
    feed(t, "\x1B[1;1H\t");   ASSERT_EQ(t->cursor.col, 3);
    feed(t, "\x1B[1;10H\t");  ASSERT_EQ(t->cursor.col, 24);   /* 16 was cleared; 24 is new */
    feed(t, "\x1B[1;1H\x1B[3I");  ASSERT_EQ(t->cursor.col, 24);
    /* ESC c restores the defaults */
    feed(t, "\x1B" "c");
    feed(t, "\x1B[1;1H\t");   ASSERT_EQ(t->cursor.col, 8);
    term_free(t);
    TEST_END();
}
