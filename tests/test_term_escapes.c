#include "test_framework.h"
#include "term.h"
#include "term_extract.h"
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
    /* xterm resets the margins on every resize: after one on the alternate
     * screen the primary's region is the whole new screen. */
    Terminal *t = term_init(10, 10, 20);
    feed(t, "\x1B[3;7r\x1B[?1049h");
    ASSERT_TRUE(term_resize(t, 8, 10));
    feed(t, "\x1B[?1049l");
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 7);
    term_free(t);
    /* shrunk below the region */
    t = term_init(10, 10, 20);
    feed(t, "\x1B[3;9r\x1B[?1049h");
    ASSERT_TRUE(term_resize(t, 6, 10));
    feed(t, "\x1B[?1049l");
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 5);
    term_free(t);
    /* grown */
    t = term_init(10, 10, 20);
    feed(t, "\x1B[3;9r\x1B[?1049h");
    ASSERT_TRUE(term_resize(t, 14, 10));
    feed(t, "\x1B[?1049l");
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 13);
    term_free(t);
    TEST_END();
}

/* The everyday path: open vim or less, grow the window, quit. */
int test_term_alt_screen_grow_then_scrolls(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 10, 50);
    feed(t, "\x1B[?1049h");
    ASSERT_TRUE(term_resize(t, 14, 10));
    feed(t, "\x1B[?1049l");
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 13);
    int before = t->lines_count;
    for (int i = 0; i < 30; i++) feed(t, "line\r\n");
    ASSERT_TRUE(t->lines_count > before);   /* scrollback grew */
    ASSERT_EQ(t->cursor.row, 13);
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

/* ---- review follow-ups: pending wrap, wrapped flag, wide tab stops ---- */

static bool stop_at(const Terminal *t, int col) {
    return (t->tab_stops[col / 8] >> (col % 8)) & 1;
}

int test_term_hts_in_pending_wrap(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 0);
    feed(t, "\x1B[1;1HXXXXXXXXXX");        /* pending wrap: col == cols */
    ASSERT_EQ(t->cursor.col, 10);
    ASSERT_TRUE(!stop_at(t, 9));
    feed(t, "\x1BH");                       /* HTS acts on the last column */
    ASSERT_TRUE(stop_at(t, 9));
    feed(t, "\x1B[1;1H\x1B[2I");
    ASSERT_EQ(t->cursor.col, 9);
    term_free(t);
    TEST_END();
}

int test_term_tbc_in_pending_wrap(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 0);
    feed(t, "\x1B[1;10H\x1BH");            /* stop at col 9 */
    ASSERT_TRUE(stop_at(t, 9));
    feed(t, "\x1B[1;1HXXXXXXXXXX");
    ASSERT_EQ(t->cursor.col, 10);
    feed(t, "\x1B[0g");                     /* clears the last column's stop */
    ASSERT_TRUE(!stop_at(t, 9));
    term_free(t);
    TEST_END();
}

int test_term_wrap_flag_below_region(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 5, 20);
    feed(t, "\x1B[2;5r\x1B[10;1HXXXXXY");  /* wrap stays on the last row */
    ASSERT_EQ(t->cursor.row, 9);
    ASSERT_TRUE(!t->lines[term_screen_to_phys(t, 9)]->wrapped);
    ASSERT_TRUE(!t->lines[term_screen_to_phys(t, 8)]->wrapped);
    term_free(t);
    TEST_END();
}

int test_term_wrap_flag_region_scroll(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 10, 20);
    feed(t, "\x1B[2;5r\x1B[5;1HXXXXXXXXXXY");
    ASSERT_EQ(t->cursor.row, 4);
    /* the new line, which the region scroll left on the bottom row */
    ASSERT_TRUE(t->lines[term_screen_to_phys(t, 4)]->wrapped);
    ASSERT_TRUE(!t->lines[term_screen_to_phys(t, 3)]->wrapped);
    term_free(t);
    TEST_END();
}

int test_term_tab_past_2048_columns(void) {
    TEST_BEGIN();
    Terminal *t = term_init(3, 2100, 0);
    feed(t, "\x1B[1;2040H");               /* col 2039 */
    feed(t, "\t");  ASSERT_EQ(t->cursor.col, 2040);
    feed(t, "\t");  ASSERT_EQ(t->cursor.col, 2048);   /* past the stored stops */
    feed(t, "\t");  ASSERT_EQ(t->cursor.col, 2056);
    feed(t, "\x1B[1;2045H\x1B[I");
    ASSERT_EQ(t->cursor.col, 2048);
    feed(t, "\x1B[2I");
    ASSERT_EQ(t->cursor.col, 2064);
    feed(t, "\x1B[1;2050H\x1B[I");
    ASSERT_EQ(t->cursor.col, 2056);
    feed(t, "\x1B[1;2090H\x1B[3I");
    ASSERT_EQ(t->cursor.col, 2099);        /* last column */
    term_free(t);
    TEST_END();
}

/* ---- RIS (ESC c) is a full reset ---- */

static bool attr_is_default(const TermAttr *a) {
    return a->flags == 0 && a->fg_mode == COLOR_DEFAULT
        && a->bg_mode == COLOR_DEFAULT && a->fg == 0 && a->bg == 0;
}

int test_term_ris_full_reset(void) {
    TEST_BEGIN();
    Terminal *t = term_init(6, 20, 50);
    /* Coloured text over the screen and into the scrollback. */
    feed(t, "\x1B[31;44;1m");
    for (int i = 0; i < 12; i++) feed(t, "line text\r\n");
    int before_count = t->lines_count;
    uint32_t sb_first = t->lines[t->lines_start]->cells[0].codepoint;
    ASSERT_TRUE(before_count > t->rows);
    ASSERT_EQ(sb_first, (uint32_t)'l');
    /* Region, modes, title, a saved cursor away from home. */
    feed(t, "\x1B[2;4r");
    feed(t, "\x1B[?1h\x1B[?25l\x1B[4h\x1B[?2004h");
    feed(t, "\x1B]0;keep\x07");
    feed(t, "\x1B[3;7H\x1B" "7");
    feed(t, "\x1B[1;20Hx");                      /* a pending wrap: col == cols */
    uint32_t erase_before = t->erase_seq;
    feed(t, "\x1B[?1049h\x1B[2;3Halt screen text");
    ASSERT_TRUE(t->alt_screen_active);

    feed(t, "\x1B" "c");

    ASSERT_TRUE(!t->alt_screen_active);
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 0);
    ASSERT_TRUE(t->cursor.visible);
    ASSERT_EQ(t->saved_cursor.row, 0);
    ASSERT_EQ(t->saved_cursor.col, 0);
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, t->rows - 1);
    ASSERT_TRUE(attr_is_default(&t->current_attr));
    ASSERT_TRUE(!t->app_cursor_keys);
    ASSERT_TRUE(!t->insert_mode);
    ASSERT_TRUE(!t->bracketed_paste_mode);
    ASSERT_TRUE(t->full_redraw_needed);
    ASSERT_EQ((int)t->erase_seq, (int)erase_before + 1);
    ASSERT_STR_EQ(t->title, "keep");             /* xterm keeps the title */
    for (int r = 0; r < t->rows; r++) {
        TermRow *row = t->lines[term_screen_to_phys(t, r)];
        ASSERT_EQ(row->len, 0);
        ASSERT_TRUE(row->dirty);
        ASSERT_TRUE(!row->wrapped);
        for (int c = 0; c < t->cols; c++) {
            ASSERT_EQ(row->cells[c].codepoint, 0u);
            ASSERT_TRUE(attr_is_default(&row->cells[c].attr));
        }
    }
    /* The scrollback is as it was. */
    ASSERT_EQ(t->lines_count, before_count);
    ASSERT_EQ(t->lines[t->lines_start]->cells[0].codepoint, sb_first);
    /* The pending wrap is gone: text lands at the home position. */
    feed(t, "Z");
    ASSERT_EQ(cp(t, 0, 0), (uint32_t)'Z');
    ASSERT_EQ(t->cursor.col, 1);
    /* Insert mode is off: typing over a cell replaces it. */
    feed(t, "\x1B[1;1HQ");
    ASSERT_EQ(cp(t, 0, 0), (uint32_t)'Q');
    ASSERT_EQ(cp(t, 0, 1), 0u);
    term_free(t);
    TEST_END();
}

/* ---- EL and ED keep TermRow.len at the row's content width ----
 * len is the highest column written, plus one (never counting a cell erased
 * since): term_put_char raises it, a whole-row erase sets it to 0. An erase
 * with a background colour leaves coloured blanks, which are not content. */

static int row_len(Terminal *t, int row) {
    return t->lines[term_screen_to_phys(t, row)]->len;
}

int test_term_el_updates_len(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    /* EL 0 with content before, at and after the cursor */
    feed(t, "\x1B[1;1Habcdefgh\x1B[1;4H\x1B[K");
    ASSERT_EQ(row_len(t, 0), 3);
    feed(t, "\x1B[1;1H\x1B[K");                    /* at the start: all gone */
    ASSERT_EQ(row_len(t, 0), 0);
    feed(t, "\x1B[2;1Habc\x1B[2;8H\x1B[K");        /* cursor past the content */
    ASSERT_EQ(row_len(t, 1), 3);
    /* EL 0 over a gap of never-written cells does not leave len on a blank */
    feed(t, "\x1B[3;1Ha\x1B[3;5Hb\x1B[3;4H\x1B[K");
    ASSERT_EQ(row_len(t, 2), 1);

    /* EL 1: content before, at and after the cursor */
    feed(t, "\x1B[4;1Habcdefgh\x1B[4;4H\x1B[1K");
    ASSERT_EQ(row_len(t, 3), 8);                   /* content after remains */
    feed(t, "\x1B[4;7H\x1B[1K");                   /* up to the cursor, h (col 8) stays */
    ASSERT_EQ(row_len(t, 3), 8);
    feed(t, "\x1B[4;8H\x1B[1K");                   /* now h is erased too */
    ASSERT_EQ(row_len(t, 3), 0);
    feed(t, "\x1B[5;1Habc\x1B[5;9H\x1B[1K");       /* erase past the content */
    ASSERT_EQ(row_len(t, 4), 0);

    /* EL 2 */
    feed(t, "\x1B[1;1Hhello\x1B[2K");
    ASSERT_EQ(row_len(t, 0), 0);

    /* the pending-wrap state (cursor.col == cols) */
    feed(t, "\x1B[2J\x1B[1;1HXXXXXXXXXX\x1B[K");   /* EL 0 acts on the last column */
    ASSERT_EQ(row_len(t, 0), 9);
    term_free(t);
    TEST_END();
}

int test_term_ed_updates_len(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 20);
    /* ED 0 from the middle of row 1: that row is cut, the rows below are empty */
    feed(t, "\x1B[1;1Haaaaaaaa\x1B[2;1Hbbbbbbbb\x1B[3;1Hcccccccc\x1B[2;4H\x1B[J");
    ASSERT_EQ(row_len(t, 0), 8);
    ASSERT_EQ(row_len(t, 1), 3);
    ASSERT_EQ(row_len(t, 2), 0);
    /* ED 0 from column 0 */
    feed(t, "\x1B[2;1H\x1B[J");
    ASSERT_EQ(row_len(t, 1), 0);

    /* ED 1: the rows above are empty, the cursor row loses its start */
    feed(t, "\x1B[1;1Haaaaaaaa\x1B[2;1Hbbbbbbbb\x1B[3;1Hcccccccc\x1B[3;4H\x1B[1J");
    ASSERT_EQ(row_len(t, 0), 0);
    ASSERT_EQ(row_len(t, 1), 0);
    ASSERT_EQ(row_len(t, 2), 8);                   /* content past the cursor stays */
    feed(t, "\x1B[3;10H\x1B[1J");                  /* the whole row */
    ASSERT_EQ(row_len(t, 2), 0);

    /* ED 2, ED 0 in the pending-wrap state touches nothing on its own row */
    feed(t, "\x1B[1;1Hxxxxxxxxxx\x1B[J");
    ASSERT_EQ(row_len(t, 0), 10);
    feed(t, "\x1B[2J");
    for (int r = 0; r < 4; r++) ASSERT_EQ(row_len(t, r), 0);
    term_free(t);
    TEST_END();
}

int test_term_erase_with_background_len(void) {
    TEST_BEGIN();
    Terminal *t = term_init(3, 10, 20);
    /* Erased cells take the current background but are not content. */
    feed(t, "\x1B[1;1Habcdef\x1B[44m\x1B[1;3H\x1B[K");
    ASSERT_EQ(row_len(t, 0), 2);
    ASSERT_EQ(t->lines[term_screen_to_phys(t, 0)]->cells[5].attr.bg_mode, (uint8_t)COLOR_ANSI16);
    feed(t, "\x1B[2;1Habc\x1B[2K");
    ASSERT_EQ(row_len(t, 1), 0);
    /* a written space is content, an erased cell is not */
    feed(t, "\x1B[0m\x1B[3;1Ha  \x1B[3;3H\x1B[K");
    ASSERT_EQ(row_len(t, 2), 2);
    term_free(t);
    TEST_END();
}

int test_term_el_extract_and_reflow(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    feed(t, "abcdefgh\x1B[1;3H\x1B[K\r\nnext");
    char buf[128];
    size_t n = term_extract_visible(t, buf, sizeof buf);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strncmp(buf, "ab\nnext", 7) == 0);

    /* Narrower than the old content, wider than the new: the row must not
     * wrap onto a second row for text that was erased. */
    ASSERT_TRUE(term_resize(t, 5, 4));
    ASSERT_EQ(row_len(t, 0), 2);
    TermRow *r1 = t->lines[term_screen_to_phys(t, 1)];
    ASSERT_TRUE(!r1->wrapped);
    ASSERT_EQ(r1->cells[0].codepoint, (uint32_t)'n');
    n = term_extract_visible(t, buf, sizeof buf);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strncmp(buf, "ab\nnext", 7) == 0);
    term_free(t);
    TEST_END();
}

/* The screen as text: every row's cells (blank as '.') and its wrapped flag. */
static void screen_snapshot(Terminal *t, char *out, size_t cap) {
    size_t n = 0;
    out[0] = '\0';
    for (int r = 0; r < t->rows && n + (size_t)t->cols + 8 < cap; r++) {
        TermRow *row = t->lines[term_screen_to_phys(t, r)];
        for (int c = 0; c < t->cols; c++) {
            uint32_t v = row->cells[c].codepoint;
            out[n++] = v == 0 ? '.' : (char)v;
        }
        out[n++] = row->wrapped ? '~' : '|';
        out[n++] = '\n';
    }
    out[n] = '\0';
}

/* Feed `input`, resize to 14 columns and back to 20: the screen is unchanged. */
static int round_trip_keeps_screen(const char *input) {
    Terminal *t = term_init(6, 20, 50);
    feed(t, input);
    char before[1024], after[1024];
    screen_snapshot(t, before, sizeof before);
    int ok = term_resize(t, 6, 14) && term_resize(t, 6, 20);
    screen_snapshot(t, after, sizeof after);
    if (!ok || strcmp(before, after) != 0)
        fprintf(stderr, "round trip changed the screen for input\nbefore:\n%s\nafter:\n%s\n",
                before, after);
    term_free(t);
    return ok && strcmp(before, after) == 0;
}

int test_term_erase_keeps_continuation_on_resize(void) {
    TEST_BEGIN();
    ASSERT_TRUE(round_trip_keeps_screen(
        "old content long line\r\nold2 long long\r\nold3\x1B[1;1Hrow one\x1B[K"
        "\x1B[2;1Hrow two two\x1B[K"));
    ASSERT_TRUE(round_trip_keeps_screen(
        "$ abcdefghijklmnopqrstuvwxyz0123456789\x1B[1;4H\x1B[K"));
    ASSERT_TRUE(round_trip_keeps_screen("abcdefghijklmnopqrst\x1B[2KX"));
    /* EL 1 up to the last column of a wrapped row */
    ASSERT_TRUE(round_trip_keeps_screen(
        "abcdefghijklmnopqrstuvwxyz\x1B[1;20H\x1B[1K"));
    TEST_END();
}

int test_term_extract_wrapped_pair_after_erase(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 20);
    feed(t, "abcdefghijklmnop\x1B[1;4H\x1B[K");      /* row 1 wraps onto row 0, then is cut */
    /* The echo test joins a wrapped pair; row 0 is blank from column 3 on,
     * so "abcklmnop" is not what the screen holds. */
    size_t n = 0;
    char *s = term_extract_abs_range_dup(t, 0, 2, "abcklmnop", &n, NULL);
    ASSERT_NOT_NULL(s);
    ASSERT_TRUE(strstr(s, "klmnop") != NULL);        /* the row was not taken as the echo */
    free(s);
    term_free(t);
    TEST_END();
}

/* Visible text of a 6x20 terminal fed `input`, resized to 14 columns. */
static void narrowed_text(const char *input, char *out, size_t cap) {
    Terminal *t = term_init(6, 20, 50);
    feed(t, input);
    term_resize(t, 6, 14);
    term_extract_visible(t, out, cap);
    term_free(t);
}

int test_term_ech_dch_match_el_on_resize(void) {
    TEST_BEGIN();
    char want[256], got[256];
    narrowed_text("abcdefghijklmnopqrs\x1B[1G\x1B[2K\r\nnext", want, sizeof want);
    ASSERT_STR_EQ(want, "\nnext");
    narrowed_text("abcdefghijklmnopqrs\x1B[1G\x1B[19X\r\nnext", got, sizeof got);
    ASSERT_STR_EQ(got, want);
    narrowed_text("abcdefghijklmnopqrs\x1B[1G\x1B[99X\r\nnext", got, sizeof got);
    ASSERT_STR_EQ(got, want);
    narrowed_text("abcdefghijklmnopqrs\x1B[1G\x1B[19P\r\nnext", got, sizeof got);
    ASSERT_STR_EQ(got, want);
    narrowed_text("abcdefghijklmnopqrs\x1B[1G\x1B[99P\r\nnext", got, sizeof got);
    ASSERT_STR_EQ(got, want);

    Terminal *t = term_init(3, 10, 20);
    /* ECH reaching len cuts it, past erased cells; a middle erase does not */
    feed(t, "abcdef\x1B[3G\x1B[99X");
    ASSERT_EQ(row_len(t, 0), 2);
    feed(t, "\x1B[2;1Habcdef\x1B[2G\x1B[2X");
    ASSERT_EQ(row_len(t, 1), 6);
    feed(t, "\x1B[3;1Habcdef\x1B[5G\x1B[2X");     /* reaches len exactly */
    ASSERT_EQ(row_len(t, 2), 4);
    /* DCH: len drops by the count, never below the cursor column's content */
    feed(t, "\x1B[2J\x1B[1;1Habcdef\x1B[2G\x1B[2P");
    ASSERT_EQ(row_len(t, 0), 4);
    feed(t, "\x1B[2;1Habcdef\x1B[3G\x1B[99P");
    ASSERT_EQ(row_len(t, 1), 2);
    feed(t, "\x1B[3;1Habcdef\x1B[9G\x1B[2P");      /* cursor past the content */
    ASSERT_EQ(row_len(t, 2), 6);
    term_free(t);
    TEST_END();
}

int test_term_ich_raises_len(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 20);
    feed(t, "hello world\x1B[1G\x1B[5@");
    ASSERT_EQ(row_len(t, 0), 16);
    char buf[128];
    term_extract_visible(t, buf, sizeof buf);
    ASSERT_TRUE(strstr(buf, "hello world") != NULL);
    ASSERT_EQ(strlen(buf), (size_t)16);
    /* cells pushed past the right edge are lost: len stops at cols */
    feed(t, "\x1B[2;1Habcdefghijklmnopqrs\x1B[1G\x1B[9@");
    ASSERT_EQ(row_len(t, 1), 20);
    /* inserting at or past the content adds only blanks */
    feed(t, "\x1B[3;1Hab\x1B[5G\x1B[3@");
    ASSERT_EQ(row_len(t, 2), 2);
    /* the head erase leaves the text still on screen counted */
    feed(t, "\x1B[2J\x1B[1;1Hhello world\x1B[1G\x1B[5@\x1B[14G\x1B[1K");
    ASSERT_EQ(row_len(t, 0), 16);
    term_free(t);

    /* reflow after an insert keeps the shifted text */
    t = term_init(4, 20, 20);
    feed(t, "hello world\x1B[1G\x1B[5@");
    ASSERT_TRUE(term_resize(t, 4, 8));
    term_extract_visible(t, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "     hel\nlo world");
    term_free(t);
    TEST_END();
}
