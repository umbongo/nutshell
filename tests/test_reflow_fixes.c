/* tests/test_reflow_fixes.c -- the two reflow fixes every session gets
 * (docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md
 * section 2), and the identity property they buy: a resize sequence that
 * ends at its starting size, with no overflow in between, leaves the model
 * unchanged.
 *
 * The spec states the identity for top mode; top mode is not built yet
 * (it is the next plan), so these cases use states bottom mode can also
 * reverse: no top padding (no grow while the screen has blank rows below
 * the cursor) and nothing pushed past the scrollback capacity. The top-mode
 * versions join this file with section 1.
 *
 * Native, `make test`; term_resize() is called directly. */

#include "test_framework.h"
#include "term.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ---- helpers ------------------------------------------------------------ */

static const TermRow *screen_row(const Terminal *t, int r)
{
    return t->lines[term_screen_to_phys(t, r)];
}

/* A row's written cells as text (NUL cells as spaces), up to row->len. */
static void row_text(const TermRow *row, int cols, char *out, size_t out_size)
{
    size_t n = 0;
    int len = row->len < cols ? row->len : cols;
    for (int c = 0; c < len && n + 1 < out_size; c++) {
        uint32_t cp = row->cells[c].codepoint;
        out[n++] = (cp >= 0x20u && cp < 0x7Fu) ? (char)cp : ' ';
    }
    out[n] = '\0';
}

static void screen_text_at(const Terminal *t, int r, char *out, size_t out_size)
{
    row_text(screen_row(t, r), t->cols, out, out_size);
}

static void feed(Terminal *t, const char *s)
{
    term_process(t, s, strlen(s));
}

/* ---- exact width ---------------------------------------------------------- */

/* A 20-character line on a 20-column terminal, made 10 rows taller: the
 * line keeps one row and no row gains a wrapped flag. */
int test_reflow_exact_width_height_grow_adds_no_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "ABCDEFGHIJKLMNOPQRST\r\n$ ");
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 2);

    term_resize(t, 20, 20);

    char text[64];
    ASSERT_EQ(t->cursor.row, 19);
    ASSERT_EQ(t->cursor.col, 2);
    screen_text_at(t, 18, text, sizeof(text));
    ASSERT_STR_EQ(text, "ABCDEFGHIJKLMNOPQRST");
    screen_text_at(t, 19, text, sizeof(text));
    ASSERT_STR_EQ(text, "$ ");
    int wrapped_rows = 0;
    for (int r = 0; r < t->rows; r++)
        if (screen_row(t, r)->wrapped) wrapped_rows++;
    ASSERT_EQ(wrapped_rows, 0);

    term_free(t);
    TEST_END();
}

/* The same line narrowed to 10 columns is two rows, and widened back to 20
 * it is one row again, with the prompt directly below it. */
int test_reflow_exact_width_narrow_and_back_is_one_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "ABCDEFGHIJKLMNOPQRST\r\n$ ");

    char text[64];
    term_resize(t, 10, 10);
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "ABCDEFGHIJ");
    screen_text_at(t, 1, text, sizeof(text));
    ASSERT_STR_EQ(text, "KLMNOPQRST");
    ASSERT_TRUE(screen_row(t, 1)->wrapped);
    screen_text_at(t, 2, text, sizeof(text));
    ASSERT_STR_EQ(text, "$ ");
    ASSERT_FALSE(screen_row(t, 2)->wrapped);
    ASSERT_EQ(t->cursor.row, 2);
    ASSERT_EQ(t->cursor.col, 2);

    term_resize(t, 10, 20);
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "ABCDEFGHIJKLMNOPQRST");
    ASSERT_FALSE(screen_row(t, 0)->wrapped);
    screen_text_at(t, 1, text, sizeof(text));
    ASSERT_STR_EQ(text, "$ ");
    ASSERT_FALSE(screen_row(t, 1)->wrapped);
    ASSERT_EQ(screen_row(t, 2)->len, 0);
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 2);

    term_free(t);
    TEST_END();
}

/* Negative: one character more than the width still wraps to two rows. */
int test_reflow_line_one_past_width_still_wraps(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "ABCDEFGHIJKLMNOPQRSTU\r\n$ ");

    term_resize(t, 12, 20);

    char text[64];
    screen_text_at(t, 9, text, sizeof(text));
    ASSERT_STR_EQ(text, "ABCDEFGHIJKLMNOPQRST");
    ASSERT_FALSE(screen_row(t, 9)->wrapped);
    screen_text_at(t, 10, text, sizeof(text));
    ASSERT_STR_EQ(text, "U");
    ASSERT_TRUE(screen_row(t, 10)->wrapped);
    screen_text_at(t, 11, text, sizeof(text));
    ASSERT_STR_EQ(text, "$ ");
    ASSERT_EQ(t->cursor.row, 11);
    ASSERT_EQ(t->cursor.col, 2);

    term_free(t);
    TEST_END();
}

/* A cursor on a continuation row that has no cells of its own (a stale
 * wrapped flag left by ED 0), after a row that ends exactly at the width,
 * keeps its row: the exact-width fix must not pull it up onto the end of
 * the row above. */
int test_reflow_cursor_on_empty_continuation_keeps_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "AAAAAAAAAAAAAAAAAAAAB");     /* 20 A, then B auto-wraps   */
    ASSERT_TRUE(screen_row(t, 1)->wrapped);
    feed(t, "\x1b[1;20H\x1b[J");          /* ED 0: row 1 keeps wrapped */
    feed(t, "A");                          /* row 0 full again          */
    feed(t, "\x1b[2;1H");                  /* cursor to row 1, col 0    */
    ASSERT_TRUE(screen_row(t, 1)->wrapped);
    ASSERT_EQ(screen_row(t, 1)->len, 0);

    term_resize(t, 8, 20);

    char text[64];
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "AAAAAAAAAAAAAAAAAAAA");
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 0);

    term_free(t);
    TEST_END();
}

/* ---- the deferred-wrap column --------------------------------------------- */

/* The pending wrap survives a resize. A cursor right after a full row sits
 * at col == cols with the wrap still owed. Narrowing the terminal so the
 * row's content ends exactly at the new width keeps the cursor on that row
 * at col == new_cols (no blank wrapped row is opened for it), so the next
 * character still wraps to the row below instead of overwriting the last
 * cell. Spec section 2: a resize sequence that ends where it started
 * leaves the model unchanged. */
int test_reflow_deferred_wrap_cursor_keeps_pending_wrap(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 10, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "0123456789");
    ASSERT_EQ(t->cursor.col, 10);

    term_resize(t, 8, 10);

    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 10);
    ASSERT_EQ(screen_row(t, 1)->len, 0);
    ASSERT_FALSE(screen_row(t, 1)->wrapped);

    feed(t, "X");

    char text[64];
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "0123456789");
    screen_text_at(t, 1, text, sizeof(text));
    ASSERT_STR_EQ(text, "X");
    ASSERT_TRUE(screen_row(t, 1)->wrapped);
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 1);

    term_free(t);
    TEST_END();
}

/* The cursor need not have been in the old deferred-wrap column: typed at
 * col 10 of 20, exactly at the content's end, then narrowed to 10, it also
 * keeps col == new_cols, and the next character wraps instead of
 * overwriting the '9'. */
int test_reflow_cursor_at_content_end_keeps_pending_wrap_when_narrowed(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "0123456789");
    ASSERT_EQ(t->cursor.col, 10);

    term_resize(t, 10, 10);

    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 10);
    ASSERT_EQ(screen_row(t, 1)->len, 0);
    ASSERT_FALSE(screen_row(t, 1)->wrapped);
    feed(t, "X");
    char text[64];
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "0123456789");
    screen_text_at(t, 1, text, sizeof(text));
    ASSERT_STR_EQ(text, "X");
    ASSERT_TRUE(screen_row(t, 1)->wrapped);
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 1);

    term_free(t);
    TEST_END();
}

/* A cursor past the content (here 5 columns past it, after `ESC [ 5 C`) on a
 * row whose content ends exactly at the new width must not be clamped onto
 * the last content cell: it keeps the pending wrap (col == new_cols), the
 * distance is lost, and the next character wraps with nothing overwritten. */
int test_reflow_cursor_past_content_at_exact_width_does_not_overwrite(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "0123456789\x1b[5C");
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 15);

    term_resize(t, 10, 10);

    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 10);
    ASSERT_EQ(screen_row(t, 1)->len, 0);
    feed(t, "X");
    char text[64];
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "0123456789");
    screen_text_at(t, 1, text, sizeof(text));
    ASSERT_STR_EQ(text, "X");
    ASSERT_TRUE(screen_row(t, 1)->wrapped);

    term_free(t);
    TEST_END();
}

/* Negative: widened, the row no longer ends at the new width, so the
 * cursor's column is simply inside the row (10 of 15), no pending wrap. */
int test_reflow_deferred_wrap_cursor_widened_is_inside_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 10, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "0123456789");
    ASSERT_EQ(t->cursor.col, 10);

    term_resize(t, 10, 15);

    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 10);
    feed(t, "X");
    char text[64];
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "0123456789X");

    term_free(t);
    TEST_END();
}
