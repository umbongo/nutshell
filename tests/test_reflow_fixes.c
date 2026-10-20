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

/* Every logical row the model holds -- scrollback and screen -- with its
 * text, len and wrapped flag, plus the geometry and the cursor. */
#define SNAP_MAX_ROWS 256
#define SNAP_MAX_COLS 64

typedef struct {
    int rows, cols, lines_count;
    int cursor_row, cursor_col, scrollback_offset;
    int len[SNAP_MAX_ROWS];
    bool wrapped[SNAP_MAX_ROWS];
    char text[SNAP_MAX_ROWS][SNAP_MAX_COLS + 1];
} ModelSnap;

static void snap(const Terminal *t, ModelSnap *s)
{
    memset(s, 0, sizeof(*s));
    s->rows = t->rows;
    s->cols = t->cols;
    s->lines_count = t->lines_count;
    s->cursor_row = t->cursor.row;
    s->cursor_col = t->cursor.col;
    s->scrollback_offset = t->scrollback_offset;
    for (int i = 0; i < t->lines_count && i < SNAP_MAX_ROWS; i++) {
        const TermRow *row = t->lines[(t->lines_start + i) % t->lines_capacity];
        s->len[i] = row->len;
        s->wrapped[i] = row->wrapped;
        row_text(row, t->cols < SNAP_MAX_COLS ? t->cols : SNAP_MAX_COLS,
                 s->text[i], sizeof(s->text[i]));
    }
}

/* 1 when equal; otherwise prints the first difference and returns 0. */
static int snap_equal(const ModelSnap *a, const ModelSnap *b)
{
    if (a->rows != b->rows || a->cols != b->cols ||
        a->lines_count != b->lines_count) {
        printf("  geometry %dx%d lines=%d vs %dx%d lines=%d\n",
               a->rows, a->cols, a->lines_count,
               b->rows, b->cols, b->lines_count);
        return 0;
    }
    if (a->cursor_row != b->cursor_row || a->cursor_col != b->cursor_col) {
        printf("  cursor %d,%d vs %d,%d\n", a->cursor_row, a->cursor_col,
               b->cursor_row, b->cursor_col);
        return 0;
    }
    if (a->scrollback_offset != b->scrollback_offset) {
        printf("  scrollback_offset %d vs %d\n", a->scrollback_offset,
               b->scrollback_offset);
        return 0;
    }
    for (int i = 0; i < a->lines_count && i < SNAP_MAX_ROWS; i++) {
        if (a->len[i] != b->len[i] || a->wrapped[i] != b->wrapped[i] ||
            strcmp(a->text[i], b->text[i]) != 0) {
            printf("  line %d: len=%d wrapped=%d \"%s\" vs len=%d wrapped=%d \"%s\"\n",
                   i, a->len[i], (int)a->wrapped[i], a->text[i],
                   b->len[i], (int)b->wrapped[i], b->text[i]);
            return 0;
        }
    }
    return 1;
}

/* 29 numbered lines, every third exactly 20 wide, then a PowerShell-style
 * prompt with the cursor one cell past it on the last row of a 10x20
 * screen: 20 rows of scrollback above. */
static void fill_history_with_prompt(Terminal *t)
{
    char line[32];
    for (int i = 1; i <= 29; i++) {
        if (i % 3 == 0) {
            (void)snprintf(line, sizeof(line), "L%02d=================\r\n", i);
        } else {
            (void)snprintf(line, sizeof(line), "L%02d\r\n", i);
        }
        feed(t, line);
    }
    feed(t, "PS C:\\>");
    feed(t, "\x1b[10;9H");
}

/* ---- cursor past the content ---------------------------------------------- */

/* PS C:\> is 7 cells; the cursor one cell further, at column 8, keeps
 * column 8 across height changes either way. */
int test_reflow_cursor_past_content_keeps_column(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "PS C:\\>\x1b[1;9H");
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 8);

    term_resize(t, 8, 20);
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 8);

    term_resize(t, 12, 20);
    ASSERT_EQ(t->cursor.col, 8);
    char text[64];
    screen_text_at(t, t->cursor.row, text, sizeof(text));
    ASSERT_STR_EQ(text, "PS C:\\>");

    term_free(t);
    TEST_END();
}

/* Narrowed so the prompt wraps, the cursor keeps its distance on the
 * continuation row; narrowed so it no longer fits, it is clamped to the
 * last column. */
int test_reflow_cursor_past_content_rewraps_and_clamps(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "PS C:\\>\x1b[1;9H");

    term_resize(t, 10, 6);               /* "PS C:\" + ">" wrapped     */
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 2);

    term_free(t);

    t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "PS C:\\>\x1b[1;9H");
    term_resize(t, 10, 8);               /* column 8 does not exist    */
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 7);

    term_free(t);
    TEST_END();
}

/* ---- identity --------------------------------------------------------------- */

/* Narrow and widen back: exact-width, wrapping and short lines and a
 * prompt with the cursor past it come back exactly as they were. */
int test_reflow_width_round_trip_is_identity(void)
{
    TEST_BEGIN();
    static ModelSnap before, after;
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "ABCDEFGHIJKLMNOPQRST\r\nhello\r\n"
            "123456789012345678901234567\r\nPS C:\\>\x1b[5;9H");
    ASSERT_EQ(t->cursor.row, 4);
    ASSERT_EQ(t->cursor.col, 8);
    snap(t, &before);

    term_resize(t, 10, 12);
    term_resize(t, 10, 20);

    snap(t, &after);
    ASSERT_TRUE(snap_equal(&before, &after));

    term_free(t);
    TEST_END();
}

/* Taller and back with history above and the cursor on the last row. */
int test_reflow_height_round_trip_with_history_is_identity(void)
{
    TEST_BEGIN();
    static ModelSnap before, after;
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    fill_history_with_prompt(t);
    ASSERT_EQ(t->lines_count, 30);
    ASSERT_EQ(t->cursor.row, 9);
    ASSERT_EQ(t->cursor.col, 8);
    snap(t, &before);

    term_resize(t, 20, 20);
    ASSERT_EQ(t->cursor.row, 19);
    ASSERT_EQ(t->cursor.col, 8);
    term_resize(t, 10, 20);

    snap(t, &after);
    ASSERT_TRUE(snap_equal(&before, &after));

    term_free(t);
    TEST_END();
}

/* A drag's worth of sizes, height and width both, ending where it began. */
int test_reflow_mixed_sequence_round_trip_is_identity(void)
{
    TEST_BEGIN();
    static ModelSnap before, after;
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    fill_history_with_prompt(t);
    snap(t, &before);

    term_resize(t, 15, 20);
    term_resize(t, 15, 12);
    term_resize(t, 10, 12);
    term_resize(t, 10, 20);

    snap(t, &after);
    ASSERT_TRUE(snap_equal(&before, &after));

    term_free(t);
    TEST_END();
}

/* A cursor past the content by d > 0, whose content ends well before the
 * last column, is clamped to new_cols - 1 on a blank cell: typing there
 * overwrites nothing. */
int test_reflow_cursor_past_content_clamps_onto_blank_cell(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "abc\x1b[1;16H");
    ASSERT_EQ(t->cursor.col, 15);

    term_resize(t, 10, 10);

    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 9);
    feed(t, "X");
    char text[64];
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "abc      X");

    term_free(t);
    TEST_END();
}
