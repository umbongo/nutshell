#include "test_framework.h"
#include "selection.h"
#include "term.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Terminal selection, PuTTY style (spec
 * docs/superpowers/specs/2026-10-20-putty-selection-design.md section 3.1). */

/* ---- helpers ------------------------------------------------------------ */

static void feed(Terminal *t, const char *text)
{
    term_process(t, text, strlen(text));
}

/* "L00", "\r\nL01", ...: line i of the batch reads "L%02d" of first + i. */
static void feed_lines(Terminal *t, int first, int count)
{
    char buf[32];
    for (int i = 0; i < count; i++) {
        int n = snprintf(buf, sizeof buf, "%sL%02d", i ? "\r\n" : "", first + i);
        term_process(t, buf, (size_t)n);
    }
}

/* A drag from screen cell (r0,c0) to (r1,c1), well past the drag threshold,
 * then released. Returns what selection_finish() returns. */
static bool drag(Selection *s, const Terminal *t, int r0, int c0, int r1, int c1)
{
    selection_begin(s, t, r0, c0, 1, 0, 0);
    selection_extend(s, t, r1, c1, 100, 100, 4, 4);
    return selection_finish(s, t);
}

/* selection_copy_text() as a string the caller frees. */
static char *copy_str(const Selection *s, const Terminal *t)
{
    size_t n = 0;
    return selection_copy_text(s, t, &n);
}

/* ---- pixel_to_cell ------------------------------------------------------ */

int test_sel_pixel_to_cell_basic(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(40, 64, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 2);
    ASSERT_EQ(col, 5);
    TEST_END();
}

int test_sel_pixel_to_cell_origin(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(0, 32, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

int test_sel_pixel_to_cell_clamp_negative(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(0, 10, 8, 16, 32, 24, 80, &row, &col);   /* in the tab strip */
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

int test_sel_pixel_to_cell_clamp_max(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(800, 500, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 23);
    ASSERT_EQ(col, 79);
    TEST_END();
}

int test_sel_pixel_to_cell_mid_cell(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(12, 44, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 1);
    TEST_END();
}

/* A captured drag reports negative client coordinates above or left of the
 * window (GET_X/Y_LPARAM, signed): they clamp, never wrap. */
int test_sel_pixel_to_cell_negative_x_and_y(void)
{
    TEST_BEGIN();
    int row = -1, col = -1;
    selection_pixel_to_cell(-30, -40, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

/* Review Focus 5: no font metrics yet. */
int test_sel_pixel_to_cell_zero_cell_size(void)
{
    TEST_BEGIN();
    int row = -1, col = -1;
    selection_pixel_to_cell(100, 100, 0, 0, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

/* ---- autoscroll_step ---------------------------------------------------- */

int test_sel_autoscroll_step_edges(void)
{
    TEST_BEGIN();
    /* Terminal area y in [32, 416): 24 rows of 16 px. */
    ASSERT_EQ(selection_autoscroll_step(32, 32, 416, 16), 0);
    ASSERT_EQ(selection_autoscroll_step(200, 32, 416, 16), 0);
    ASSERT_EQ(selection_autoscroll_step(415, 32, 416, 16), 0);
    ASSERT_EQ(selection_autoscroll_step(31, 32, 416, 16), -1);       /* one pixel above */
    ASSERT_EQ(selection_autoscroll_step(0, 32, 416, 16), -3);        /* two cell heights above */
    ASSERT_EQ(selection_autoscroll_step(416, 32, 416, 16), 1);       /* one pixel below */
    ASSERT_EQ(selection_autoscroll_step(416 + 15, 32, 416, 16), 2);  /* one cell height below */
    TEST_END();
}

/* Review Focus 5: a zero cell height must not divide by zero. */
int test_sel_autoscroll_step_zero_char_height(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(selection_autoscroll_step(10, 32, 416, 0) < 0);
    ASSERT_TRUE(selection_autoscroll_step(500, 32, 416, 0) > 0);
    ASSERT_EQ(selection_autoscroll_step(100, 32, 416, 0), 0);
    TEST_END();
}

/* ---- begin / extend / finish -------------------------------------------- */

int test_sel_click_is_not_a_selection(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    selection_begin(&s, t, 0, 3, 1, 27, 40);
    ASSERT_FALSE(selection_finish(&s, t));
    ASSERT_FALSE(s.valid);
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_move_under_threshold_is_not_a_selection(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    selection_begin(&s, t, 0, 3, 1, 31, 40);
    selection_extend(&s, t, 0, 4, 33, 40, 4, 4);   /* into the next cell, 2 px < 4 */
    ASSERT_FALSE(s.valid);
    ASSERT_FALSE(selection_finish(&s, t));
    term_free(t);
    TEST_END();
}

int test_sel_move_over_threshold_selects(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    selection_begin(&s, t, 0, 3, 1, 31, 40);
    selection_extend(&s, t, 0, 4, 35, 40, 4, 4);   /* 4 px: a drag */
    ASSERT_TRUE(s.valid);
    ASSERT_TRUE(selection_finish(&s, t));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "DE");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_backward_drag_normalises(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 7, 0, 2));
    ASSERT_EQ(s.start.col, 2);
    ASSERT_EQ(s.end.col, 7);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "CDEFGH");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_press_clears_previous_selection(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 0, 5));
    selection_begin(&s, t, 1, 0, 1, 0, 0);
    ASSERT_FALSE(s.valid);
    ASSERT_TRUE(s.active);
    term_free(t);
    TEST_END();
}

int test_sel_extend_after_finish_is_ignored(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 1, 0, 3));
    selection_extend(&s, t, 0, 8, 200, 200, 4, 4);
    ASSERT_EQ(s.end.col, 3);
    term_free(t);
    TEST_END();
}

/* Points are absolute line numbers: a drag while scrolled back names the
 * rows the view shows, and the copy reads exactly those (defect 2). */
int test_sel_points_are_buffer_absolute(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    feed_lines(t, 0, 12);                         /* L00..L11 at logical 0..11 */
    t->scrollback_offset = 3;                     /* the view shows L04..L08 */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    ASSERT_EQ(s.start.line, t->lines_recycled + (uint64_t)term_view_to_logical(t, 0));
    ASSERT_EQ(s.start.line, (uint64_t)4);
    ASSERT_EQ(s.end.line, (uint64_t)5);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "L04\r\nL05");
    free(txt);
    term_free(t);
    TEST_END();
}

/* ---- copy_text ---------------------------------------------------------- */

int test_sel_copy_wrapped_rows_are_one_line(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "ABCDEFGHIJKLMNOPQRSTUVWXY");            /* rows 0-2, rows 1-2 wrapped */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 2, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "ABCDEFGHIJKLMNOPQRSTUVWXY");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_keeps_space_at_wrap_boundary(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "123456foo bar");                         /* "123456foo " | "bar" */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 6, 1, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "foo bar");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_crlf_between_lines_none_at_end(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "one\r\ntwo\r\nthree");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 2, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "one\r\ntwo\r\nthree");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_trims_trailing_blanks_per_line(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "ab   \r\ncd");                           /* written spaces, then empty cells */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "ab\r\ncd");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_drops_trailing_empty_rows(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "top\r\n\r\n\r\n");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 3, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "top");
    free(txt);
    term_free(t);
    TEST_END();
}

/* No size limit (defect 7): 3,000 lines of 200 characters, whole. */
/* The sparse-buffer state right after a resize (lines_count < rows, see
 * term_logical_rows() in src/term/term.h): rows are on screen and
 * selectable yet sit past lines_count. A selection over the visible text
 * must still copy exactly that text, not blank lines. The resize no
 * longer leaves the state by itself (term_reflow_buffer() keeps
 * lines_count >= rows), so it is forced here to pin the consumer's
 * tolerance (the old test_sel_extract_sparse_after_resize_shrink). */
int test_sel_copy_sparse_buffer_after_resize_shrink(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(50, 76, 3000);
    term_resize(t, 29, 76);
    t->lines_count = 1;                           /* forced: sparse, lines_count < rows */
    ASSERT_TRUE(t->lines_count < t->rows);

    feed(t, "line one\r\nline two");
    ASSERT_TRUE(t->cursor.row >= t->lines_count);
    ASSERT_TRUE(term_view_to_logical(t, 1) >= t->lines_count);

    Selection s;
    ASSERT_TRUE(drag(&s, t, 1, 0, 1, 9));         /* the sparse row alone */
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "line two");
    free(txt);

    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));         /* both rows */
    txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "line one\r\nline two");
    free(txt);

    term_free(t);
    TEST_END();
}

int test_sel_copy_3000_rows_whole(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 200, 3000);
    char line[202];
    for (int i = 0; i < 3000; i++) {
        memset(line, 'a' + i % 26, 200);
        line[200] = '\r';
        line[201] = '\n';
        term_process(t, line, sizeof line);
    }
    Selection s;
    selection_select_all(&s, t);
    ASSERT_TRUE(s.valid);
    size_t n = 0;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)(3000 * 200 + 2999 * 2));
    ASSERT_EQ(strlen(txt), n);
    ASSERT_EQ(txt[0], 'a');
    ASSERT_EQ(txt[199], 'a');
    ASSERT_EQ(txt[200], '\r');
    ASSERT_EQ(txt[201], '\n');
    ASSERT_EQ(txt[n - 201], '\n');
    ASSERT_EQ(txt[n - 1], 'a' + 2999 % 26);
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_empty_selection_is_empty_string(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "text");
    Selection s;
    selection_clear(&s);
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    txt = selection_copy_text(NULL, t, &n);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    txt = selection_copy_text(&s, NULL, NULL);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    term_free(t);
    TEST_END();
}

/* Review Focus 4: a selection of blanks copies nothing, so the window leaves
 * the clipboard alone. */
int test_sel_copy_blank_only_is_empty(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "text");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 2, 0, 2, 9));
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    term_free(t);
    TEST_END();
}

/* Hardening (kept from the old extractor's tests): DEL and C1 controls in
 * cells copy as spaces, never as the control byte. */
int test_sel_copy_controls_become_spaces(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "A\x7F" "B");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 0, 2));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "A B");
    free(txt);
    term_free(t);

    t = term_init(4, 10, 0);
    feed(t, "A\xC2\x80" "B");
    ASSERT_TRUE(drag(&s, t, 0, 0, 0, 2));
    txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "A B");
    free(txt);
    term_free(t);
    TEST_END();
}

/* ---- select_all, contains, in_view -------------------------------------- */

int test_sel_select_all_from_oldest_to_last_nonempty(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 3);            /* capacity 8 */
    feed_lines(t, 0, 12);                         /* L00..L03 recycled */
    ASSERT_EQ(t->lines_recycled, (uint64_t)4);
    Selection s;
    selection_select_all(&s, t);
    ASSERT_TRUE(s.valid);
    ASSERT_FALSE(s.active);
    ASSERT_EQ(s.start.line, (uint64_t)4);
    ASSERT_EQ(s.start.col, 0);
    ASSERT_EQ(s.end.line, (uint64_t)11);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "L04\r\nL05\r\nL06\r\nL07\r\nL08\r\nL09\r\nL10\r\nL11");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_select_all_empty_terminal_is_not_valid(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 3);
    Selection s;
    selection_select_all(&s, t);
    ASSERT_FALSE(s.valid);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_contains_and_in_view_follow_the_view(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    feed_lines(t, 0, 12);                         /* live view L07..L11 */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 2));         /* L07 col 0 .. L08 col 2 */
    ASSERT_TRUE(selection_contains(&s, t, 0, 0));
    ASSERT_TRUE(selection_contains(&s, t, 1, 2));
    ASSERT_FALSE(selection_contains(&s, t, 1, 3));
    ASSERT_FALSE(selection_contains(&s, t, 2, 0));
    t->scrollback_offset = 2;                     /* view L05..L09: L07 on row 2 */
    ASSERT_TRUE(selection_contains(&s, t, 2, 0));
    ASSERT_FALSE(selection_contains(&s, t, 0, 0));
    ASSERT_TRUE(selection_in_view(&s, t));
    t->scrollback_offset = 7;                     /* view L00..L04 */
    ASSERT_FALSE(selection_in_view(&s, t));
    ASSERT_FALSE(selection_contains(&s, t, 0, 0));
    term_free(t);
    TEST_END();
}

/* ---- words and lines (spec section 1) ----------------------------------- */

/* A double- or triple-click at a screen cell, released where it was pressed. */
static bool click_n(Selection *s, const Terminal *t, int row, int col, int clicks)
{
    selection_begin(s, t, row, col, clicks, 0, 0);
    selection_extend(s, t, row, col, 0, 0, 4, 4);
    return selection_finish(s, t);
}

int test_sel_char_class(void)
{
    TEST_BEGIN();
    ASSERT_EQ(selection_char_class(0), 0);
    ASSERT_EQ(selection_char_class(' '), 0);
    ASSERT_EQ(selection_char_class(0x07), 0);                 /* a control codepoint */
    const char *word = "azAZ09_-./~:@";
    for (const char *p = word; *p; p++)
        ASSERT_EQ(selection_char_class((uint32_t)(unsigned char)*p), 1);
    ASSERT_EQ(selection_char_class(0xE9), 1);                 /* é: Review Focus 1 */
    ASSERT_EQ(selection_char_class(0x0436), 1);               /* ж */
    ASSERT_EQ(selection_char_class(','), 2 + ',');
    ASSERT_EQ(selection_char_class(0xD7), 2 + 0xD7);          /* × */
    ASSERT_EQ(selection_char_class(0x65E5), 2 + 0x65E5);      /* 日 */
    TEST_END();
}

int test_sel_word_path(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 40, 10);
    feed(t, "ls /usr/local/bin now");
    Selection s;
    selection_begin(&s, t, 0, 7, 2, 0, 0);
    ASSERT_TRUE(s.valid);                                     /* highlighted on the press */
    ASSERT_TRUE(selection_finish(&s, t));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "/usr/local/bin");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_stops_at_comma(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "foo,bar");
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 1, 2));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "foo");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_run_of_spaces(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "a    b");
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 2, 2));
    ASSERT_EQ(s.start.col, 1);
    ASSERT_EQ(s.end.col, 4);
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);                                  /* Review Focus 4 */
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_cjk_cell(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");            /* 日本語, one cell each */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 1, 2));
    ASSERT_EQ(s.start.col, 1);
    ASSERT_EQ(s.end.col, 1);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "\xE6\x9C\xAC");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_follows_wrap(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "go http://ex.com/a/b ok");                       /* the URL wraps onto row 1 */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 1, 2, 2));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "http://ex.com/a/b");
    free(txt);
    term_free(t);
    TEST_END();
}

/* Review Focus 1: an accented word is one word. */
int test_sel_word_accented_latin(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "un caf\xC3\xA9 noir");
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 5, 2));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "caf\xC3\xA9");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_line_middle_of_three_rows(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "$ abcdefghijklmnopqrstuvwxyz\r\nnext");             /* 28 chars on rows 0-2 */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 1, 4, 3));
    ASSERT_EQ(s.start.line, (uint64_t)0);
    ASSERT_EQ(s.start.col, 0);
    ASSERT_EQ(s.end.line, (uint64_t)2);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "$ abcdefghijklmnopqrstuvwxyz");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_line_head_recycled_stops_at_oldest(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(3, 10, 1);                        /* capacity 4 */
    /* One 45-character line over five rows: the fifth recycles the head. */
    feed(t, "0123456789abcdefghijABCDEFGHIJklmnopqrstKLMNO");
    ASSERT_EQ(t->lines_recycled, (uint64_t)1);
    t->scrollback_offset = 1;                                 /* row 0 shows logical 0 */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 3, 3));
    ASSERT_EQ(s.start.line, t->lines_recycled);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "abcdefghijABCDEFGHIJklmnopqrstKLMNO");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_drag_backwards_whole_words(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "alpha beta gamma");
    Selection s;
    selection_begin(&s, t, 0, 13, 2, 0, 0);                   /* in "gamma" */
    selection_extend(&s, t, 0, 7, 0, 0, 4, 4);                /* into "beta" */
    ASSERT_TRUE(selection_finish(&s, t));
    ASSERT_EQ(s.start.col, 6);
    ASSERT_EQ(s.end.col, 15);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "beta gamma");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_line_drag_extends_by_lines(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "one\r\ntwo\r\nthree");
    Selection s;
    selection_begin(&s, t, 2, 1, 3, 0, 0);
    selection_extend(&s, t, 0, 2, 0, 0, 4, 4);
    ASSERT_TRUE(selection_finish(&s, t));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "one\r\ntwo\r\nthree");
    free(txt);
    term_free(t);
    TEST_END();
}

/* ---- staleness and recycling (spec section 2.3) ------------------------- */

int test_sel_write_after_finish_makes_row_stale(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "one\r\ntwo\r\nthree");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    feed(t, "\x1b[2;1HX");                        /* write into "two" */
    ASSERT_TRUE(selection_contains(&s, t, 0, 0)); /* untouched row: still highlighted */
    ASSERT_FALSE(selection_contains(&s, t, 1, 1));/* written row: not */
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    ASSERT_TRUE(selection_sync(&s, t));           /* the clear */
    ASSERT_FALSE(s.valid);
    term_free(t);
    TEST_END();
}

int test_sel_write_during_drag_does_not_clear(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "one\r\ntwo\r\nthree");
    Selection s;
    selection_begin(&s, t, 0, 0, 1, 0, 0);
    selection_extend(&s, t, 1, 9, 100, 100, 4, 4);
    feed(t, "\x1b[2;1HX");                        /* streaming output under the drag */
    ASSERT_FALSE(selection_sync(&s, t));
    ASSERT_TRUE(s.valid);
    ASSERT_TRUE(selection_contains(&s, t, 1, 0));
    ASSERT_TRUE(selection_finish(&s, t));         /* takes the stamps */
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "one\r\nXwo");             /* what is on screen at release */
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_epoch_change_clears_and_ends_drag(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "one\r\ntwo");
    Selection s;
    selection_begin(&s, t, 0, 0, 1, 0, 0);
    selection_extend(&s, t, 1, 2, 100, 100, 4, 4);
    term_resize(t, 6, 10);                        /* reflow: a new epoch */
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_FALSE(s.active);
    ASSERT_FALSE(s.valid);

    ASSERT_TRUE(drag(&s, t, 0, 0, 0, 2));
    term_alt_screen_enter(t);                     /* vim, less: a new epoch */
    ASSERT_FALSE(selection_contains(&s, t, 0, 0));
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_FALSE(s.valid);
    term_free(t);
    TEST_END();
}

/* The row at the recycle point is trimmed away, not treated as touched: the
 * recycled TermRow is stamped, but under its new, newest line number. */
int test_sel_recycle_trims_start_without_touching(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 2);            /* capacity 6 */
    feed_lines(t, 0, 6);                          /* full: L00..L05 */
    t->scrollback_offset = 2;                     /* view L00..L03 */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 2, 9));         /* L00..L02 */
    feed(t, "\r\nL06");                           /* recycles L00 */
    ASSERT_EQ(t->lines_recycled, (uint64_t)1);
    ASSERT_TRUE(selection_sync(&s, t));           /* trimmed */
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.start.line, (uint64_t)1);
    ASSERT_EQ(s.start.col, 0);
    ASSERT_TRUE(s.anchor.line >= t->lines_recycled);
    ASSERT_TRUE(selection_contains(&s, t, 0, 0)); /* view row 0 is L01 now */
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "L01\r\nL02");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_recycled_anchor_cannot_return(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 2);
    feed_lines(t, 0, 6);
    t->scrollback_offset = 2;
    Selection s;
    selection_begin(&s, t, 0, 0, 1, 0, 0);        /* anchor on L00 */
    selection_extend(&s, t, 2, 9, 100, 100, 4, 4);
    feed(t, "\r\nL06");                           /* L00 recycled mid-drag */
    selection_extend(&s, t, 1, 0, 100, 120, 4, 4);/* no sync in between */
    ASSERT_TRUE(s.anchor.line >= t->lines_recycled);
    ASSERT_TRUE(s.start.line >= t->lines_recycled);
    term_free(t);
    TEST_END();
}

int test_sel_wholly_recycled_clears(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 2);
    feed_lines(t, 0, 6);
    t->scrollback_offset = 2;
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));         /* L00..L01 */
    feed(t, "\r\nL06\r\nL07");                    /* both recycled */
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_FALSE(s.valid);
    term_free(t);
    TEST_END();
}

int test_sel_sync_ignores_writes_elsewhere(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "one\r\ntwo");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    ASSERT_FALSE(selection_sync(&s, t));          /* nothing written */
    feed(t, "\x1b[4;1HZ");                        /* a row outside the selection */
    ASSERT_FALSE(selection_sync(&s, t));
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.gen, t->write_gen);               /* stamp moved up: next sync is cheap */
    term_free(t);
    TEST_END();
}

/* write_gen is 64 bits. Crossing 2^32, the old 32-bit wrap point, must not
 * make an old row look new, nor hide a write into the selection. */
int test_sel_stamps_survive_counter_wrap(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    t->write_gen = 0xFFFFFFF0u;
    feed(t, "one\r\ntwo");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    for (int i = 0; i < 40; i++) feed(t, "\x1b[4;1HZ");  /* elsewhere; crosses 2^32 */
    ASSERT_TRUE(t->write_gen > 0xFFFFFFFFull);
    ASSERT_FALSE(selection_sync(&s, t));
    ASSERT_TRUE(s.valid);
    ASSERT_TRUE(selection_contains(&s, t, 0, 0));
    feed(t, "\x1b[1;1HX");                        /* into the selection */
    ASSERT_FALSE(selection_contains(&s, t, 0, 0));
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_FALSE(s.valid);
    term_free(t);
    TEST_END();
}

/* Regression: rows made by reflow carry stamp 0. With a 32-bit counter past
 * 2^31 every such row compared as newer than any selection, so a fresh
 * selection was stale at once. */
int test_sel_reflowed_rows_not_stale_at_high_counter(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "one\r\ntwo\r\nthree");
    t->write_gen = 0x80000010u;
    term_resize(t, 6, 10);                        /* reflow: rows with gen 0, text now at rows 3-5 */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 3, 0, 4, 9));
    ASSERT_TRUE(selection_contains(&s, t, 3, 0));
    ASSERT_TRUE(selection_contains(&s, t, 4, 0));
    feed(t, "\x1b[1;1HZ");                        /* a write elsewhere */
    ASSERT_FALSE(selection_sync(&s, t));
    ASSERT_TRUE(s.valid);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "one\r\ntwo");
    free(txt);
    term_free(t);
    TEST_END();
}

/* ---- extending a selection: Shift+click, middle-click ------------------- */

/* The extend press and its release, at screen cell (row, col). */
static bool extend_click(Selection *s, const Terminal *t, int row, int col)
{
    selection_extend_from(s, t, row, col, 50, 50);
    return selection_finish(s, t);
}

int test_sel_extend_from_after_end_moves_end(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 30, 10);
    feed(t, "0123456789abcdefghij");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 2, 0, 5));
    selection_extend_from(&s, t, 0, 12, 50, 50);
    ASSERT_TRUE(s.active);
    ASSERT_EQ(s.press_x, 50);
    ASSERT_EQ(s.last_y, 50);
    ASSERT_TRUE(selection_finish(&s, t));
    ASSERT_EQ(s.start.col, 2);
    ASSERT_EQ(s.end.col, 12);
    ASSERT_TRUE(s.mode == SEL_STREAM);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "23456789abc");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_extend_from_before_start_moves_start(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 30, 10);
    feed(t, "0123456789abcdefghij");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 8, 0, 12));
    ASSERT_TRUE(extend_click(&s, t, 0, 3));
    ASSERT_EQ(s.start.col, 3);
    ASSERT_EQ(s.end.col, 12);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "3456789abc");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_extend_from_inside_moves_nearer_end(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 30, 10);
    feed(t, "0123456789abcdefghij");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 2, 0, 12));              /* middle is 7 */
    ASSERT_TRUE(extend_click(&s, t, 0, 5));             /* nearer the start */
    ASSERT_EQ(s.start.col, 5);
    ASSERT_EQ(s.end.col, 12);
    ASSERT_TRUE(drag(&s, t, 0, 2, 0, 12));
    ASSERT_TRUE(extend_click(&s, t, 0, 9));             /* nearer the end */
    ASSERT_EQ(s.start.col, 2);
    ASSERT_EQ(s.end.col, 9);
    ASSERT_TRUE(drag(&s, t, 0, 2, 0, 12));
    ASSERT_TRUE(extend_click(&s, t, 0, 7));             /* a tie goes to the end */
    ASSERT_EQ(s.start.col, 2);
    ASSERT_EQ(s.end.col, 7);
    term_free(t);
    TEST_END();
}

/* Nearer is judged by line first: a click nearer the end by line moves the end
 * even when it is nearer the start by column. */
int test_sel_extend_from_inside_nearer_by_line(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(6, 30, 10);
    feed(t, "aaaaaaaaaa\r\nbbbbbbbbbb\r\ncccccccccc\r\ndddddddddd");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 8, 3, 1));
    ASSERT_TRUE(extend_click(&s, t, 2, 8));             /* line 2: one from the end, two from the start;
                                                         * col 8 is the start's column, 7 from the end's */
    ASSERT_EQ(s.start.line, (uint64_t)0);
    ASSERT_EQ(s.start.col, 8);
    ASSERT_EQ(s.end.line, (uint64_t)2);
    ASSERT_EQ(s.end.col, 8);
    term_free(t);
    TEST_END();
}

int test_sel_extend_from_word_mode_by_whole_words(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 30, 10);
    feed(t, "alpha beta gamma delta");
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 7, 2));               /* "beta" */
    ASSERT_TRUE(extend_click(&s, t, 0, 13));            /* into "gamma" */
    ASSERT_TRUE(s.mode == SEL_WORD);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "beta gamma");
    free(txt);
    ASSERT_TRUE(extend_click(&s, t, 0, 1));             /* before: into "alpha" */
    txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "alpha beta gamma");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_extend_from_line_mode_by_whole_lines(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 10);
    feed(t, "one\r\ntwo\r\nthree\r\nfour");
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 1, 1, 3));               /* "two" */
    ASSERT_TRUE(extend_click(&s, t, 2, 2));
    ASSERT_TRUE(s.mode == SEL_LINE);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "two\r\nthree");
    free(txt);
    ASSERT_TRUE(extend_click(&s, t, 0, 1));
    txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "one\r\ntwo\r\nthree");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_extend_from_without_selection_is_a_click(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 30, 10);
    feed(t, "0123456789abcdefghij");
    Selection s;
    selection_clear(&s);
    selection_extend_from(&s, t, 0, 4, 10, 10);
    ASSERT_FALSE(s.valid);
    ASSERT_TRUE(s.active);
    ASSERT_TRUE(s.mode == SEL_STREAM);
    selection_extend(&s, t, 0, 9, 100, 100, 4, 4);     /* a drag selects from it */
    ASSERT_TRUE(selection_finish(&s, t));
    ASSERT_EQ(s.start.col, 4);
    ASSERT_EQ(s.end.col, 9);
    term_free(t);
    TEST_END();
}

int test_sel_extend_from_after_epoch_change_starts_fresh(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 30, 10);
    feed(t, "0123456789abcdefghij");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 2, 0, 5));
    term_resize(t, 4, 20);
    ASSERT_TRUE(s.epoch != t->epoch);
    selection_extend_from(&s, t, 0, 9, 10, 10);
    ASSERT_FALSE(s.valid);                              /* a single click, not an extension */
    ASSERT_TRUE(s.active);
    ASSERT_EQ(s.epoch, t->epoch);
    ASSERT_EQ(s.anchor.col, 9);
    term_free(t);
    TEST_END();
}

/* After the extend press the drag is live: the end that stayed is still the
 * anchor, so dragging back across it flips the selection. */
int test_sel_extend_from_keeps_anchor_for_further_drag(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 30, 10);
    feed(t, "0123456789abcdefghij");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 5, 0, 8));
    selection_extend_from(&s, t, 0, 12, 50, 50);        /* moves the end; start 5 stays */
    ASSERT_EQ(s.anchor.col, 5);
    ASSERT_EQ(s.anchor_end.col, 5);
    selection_extend(&s, t, 0, 2, 60, 50, 4, 4);        /* drag left past the anchor */
    ASSERT_TRUE(selection_finish(&s, t));
    ASSERT_EQ(s.start.col, 2);
    ASSERT_EQ(s.end.col, 5);
    selection_extend_from(&s, t, 0, 1, 50, 50);         /* start moves, end 5 stays */
    ASSERT_EQ(s.anchor.col, 5);
    selection_extend(&s, t, 0, 9, 60, 50, 4, 4);
    ASSERT_TRUE(selection_finish(&s, t));
    ASSERT_EQ(s.start.col, 5);
    ASSERT_EQ(s.end.col, 9);
    term_free(t);
    TEST_END();
}

/* A selection whose start line was recycled after the drag: selection_sync
 * trims it to the oldest held line, and an extend from there moves only the
 * end. */
int test_sel_extend_from_after_start_recycled(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 2);                  /* capacity 6 */
    feed_lines(t, 0, 6);                                /* full: L00..L05 */
    t->scrollback_offset = 2;                           /* view L00..L03 */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 1, 0, 2, 9));               /* L01..L02 */
    feed(t, "\r\nL06\r\nL07");                          /* recycles L00 and L01 */
    ASSERT_EQ(t->lines_recycled, (uint64_t)2);
    t->scrollback_offset = 0;                           /* view L04..L07 */
    ASSERT_TRUE(selection_sync(&s, t));                 /* start trimmed to line 2 */
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.start.line, (uint64_t)2);
    ASSERT_TRUE(extend_click(&s, t, 3, 2));             /* L07, col 2: after the end */
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.start.line, (uint64_t)2);
    ASSERT_EQ(s.start.col, 0);
    ASSERT_EQ(s.end.line, (uint64_t)7);
    ASSERT_EQ(s.end.col, 2);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "L02\r\nL03\r\nL04\r\nL05\r\nL06\r\nL07");
    free(txt);
    term_free(t);
    TEST_END();
}

/* ---- block (Alt+drag) selection ----------------------------------------- */

/* An Alt drag from screen cell (r0,c0) to (r1,c1), released. */
static bool bdrag(Selection *s, const Terminal *t, int r0, int c0, int r1, int c1)
{
    selection_begin_block(s, t, r0, c0, 0, 0);
    selection_extend(s, t, r1, c1, 100, 100, 4, 4);
    return selection_finish(s, t);
}

/* Four rows "ABCDEFGH" style: row i holds the letters from 'a'+i*... */
static Terminal *block_term(void)
{
    Terminal *t = term_init(4, 10, 0);
    feed(t, "0123456789\r\nabcdefghij\r\nKLMNOPQRST\r\nuvwxyzABCD");
    return t;
}

static bool block_copy_is(const Selection *s, const Terminal *t, const char *want)
{
    char *txt = copy_str(s, t);
    bool ok = strcmp(txt, want) == 0;
    if (!ok) printf("  block copy: got \"%s\" want \"%s\"\n", txt, want);
    free(txt);
    return ok;
}

int test_sel_block_down_right(void)
{
    TEST_BEGIN();
    Terminal *t = block_term();
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 2, 2, 4));
    ASSERT_TRUE(s.block);
    ASSERT_TRUE(block_copy_is(&s, t, "234\r\ncde\r\nMNO"));
    ASSERT_TRUE(selection_contains(&s, t, 1, 3));
    ASSERT_TRUE(!selection_contains(&s, t, 1, 5));     /* a stream selection would hold it */
    ASSERT_TRUE(!selection_contains(&s, t, 1, 1));
    ASSERT_TRUE(!selection_contains(&s, t, 3, 3));
    term_free(t);
    TEST_END();
}

int test_sel_block_all_four_directions(void)
{
    TEST_BEGIN();
    Terminal *t = block_term();
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 2, 4, 0, 2));             /* up and left */
    ASSERT_TRUE(block_copy_is(&s, t, "234\r\ncde\r\nMNO"));
    ASSERT_TRUE(bdrag(&s, t, 0, 4, 2, 2));             /* down and left */
    ASSERT_TRUE(block_copy_is(&s, t, "234\r\ncde\r\nMNO"));
    ASSERT_TRUE(bdrag(&s, t, 2, 2, 0, 4));             /* up and right */
    ASSERT_TRUE(block_copy_is(&s, t, "234\r\ncde\r\nMNO"));
    term_free(t);
    TEST_END();
}

int test_sel_block_one_row_and_one_column(void)
{
    TEST_BEGIN();
    Terminal *t = block_term();
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 1, 2, 1, 5));             /* one row */
    ASSERT_TRUE(block_copy_is(&s, t, "cdef"));
    ASSERT_TRUE(bdrag(&s, t, 0, 3, 3, 3));             /* one column, several rows */
    ASSERT_TRUE(block_copy_is(&s, t, "3\r\nd\r\nN\r\nx"));
    term_free(t);
    TEST_END();
}

/* A press and release without movement is no selection, block or not. */
int test_sel_block_click_selects_nothing(void)
{
    TEST_BEGIN();
    Terminal *t = block_term();
    Selection s;
    selection_begin_block(&s, t, 1, 3, 0, 0);
    selection_extend(&s, t, 1, 3, 1, 1, 4, 4);         /* under the drag threshold */
    ASSERT_TRUE(!selection_finish(&s, t));
    ASSERT_TRUE(block_copy_is(&s, t, ""));
    term_free(t);
    TEST_END();
}

/* Rows shorter than the left edge give an empty line; trailing blanks go. */
int test_sel_block_short_rows(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "0123456789\r\nab\r\n\r\nabcdef  ");
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 4, 3, 7));
    ASSERT_TRUE(block_copy_is(&s, t, "4567\r\n\r\n\r\nef"));
    ASSERT_TRUE(bdrag(&s, t, 0, 1, 3, 8));             /* "ab" row reaches into col 1 only */
    ASSERT_TRUE(block_copy_is(&s, t, "12345678\r\nb\r\n\r\nbcdef"));
    term_free(t);
    TEST_END();
}

/* Empty lines at the bottom are dropped, as in the stream copy. */
int test_sel_block_trailing_empty_lines_dropped(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "0123456789\r\nab");
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 5, 3, 8));
    ASSERT_TRUE(block_copy_is(&s, t, "5678"));
    ASSERT_TRUE(bdrag(&s, t, 1, 5, 3, 8));             /* only blanks */
    ASSERT_TRUE(block_copy_is(&s, t, ""));
    term_free(t);
    TEST_END();
}

/* Cells hold one codepoint each: a wide character is one cell and is copied
 * whole or not at all. A combining mark at the left edge (its base outside)
 * is left out; one right after the right edge goes with its base. */
int test_sel_block_wide_and_combining_edges(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(3, 10, 0);
    feed(t, "a\xe6\x97\xa5" "b\xe6\x9c\xac" "c\r\ne\xcc\x81" "xyz");
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 1, 0, 3));             /* wide, b, wide */
    ASSERT_TRUE(block_copy_is(&s, t, "\xe6\x97\xa5" "b\xe6\x9c\xac"));
    /* Row 1 is e, U+0301 (own cell), x, y, z. */
    ASSERT_TRUE(bdrag(&s, t, 1, 0, 1, 0));             /* base alone: its mark follows */
    ASSERT_TRUE(block_copy_is(&s, t, "e\xcc\x81"));
    ASSERT_TRUE(bdrag(&s, t, 1, 1, 1, 3));             /* mark at the left edge: left out */
    ASSERT_TRUE(block_copy_is(&s, t, "xy"));
    term_free(t);
    TEST_END();
}

/* Insert-character moves cells right without raising the row's len: the
 * block copy must see what the highlight shows, as the stream copy does. */
int test_sel_block_copy_past_row_len(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(2, 10, 0);
    feed(t, "abc\r\x1b[5@");                           /* five blanks, then abc; len stays 3 */
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 4, 0, 9));
    ASSERT_TRUE(block_copy_is(&s, t, " abc"));
    /* After delete-character the row can be longer than len says, too. */
    feed(t, "\x1b[2;1Hwxyz123456\x1b[2;1H\x1b[2P");     /* "yz123456" shifted left */
    ASSERT_TRUE(bdrag(&s, t, 1, 0, 1, 7));
    ASSERT_TRUE(block_copy_is(&s, t, "yz123456"));
    term_free(t);
    TEST_END();
}

/* Alt starts a block; Ctrl with it (Ctrl+Alt, or AltGr, which Windows reports
 * as Ctrl+Alt) does not. */
int test_sel_block_modifiers(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(selection_block_modifiers(true, false));
    ASSERT_TRUE(!selection_block_modifiers(false, false));
    ASSERT_TRUE(!selection_block_modifiers(true, true));      /* AltGr, Ctrl+Alt */
    ASSERT_TRUE(!selection_block_modifiers(false, true));
    TEST_END();
}

/* Alt+double-click is no longer a word selection: an Alt press always starts
 * a block, whatever the click counter says, and a plain press keeps its count. */
int test_sel_block_alt_double_click_is_not_a_word(void)
{
    TEST_BEGIN();
    ASSERT_EQ(selection_press_clicks(true, 1), 1);
    ASSERT_EQ(selection_press_clicks(true, 2), 1);
    ASSERT_EQ(selection_press_clicks(true, 3), 1);
    ASSERT_EQ(selection_press_clicks(false, 2), 2);
    ASSERT_EQ(selection_press_clicks(false, 3), 3);
    /* Two quick Alt drags at one spot: the second begins a block again. */
    Terminal *t = block_term();
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 2, 1, 4));
    selection_begin_block(&s, t, 0, 2, 0, 0);
    selection_extend(&s, t, 1, 4, 100, 100, 4, 4);
    ASSERT_TRUE(selection_finish(&s, t));
    ASSERT_TRUE(s.block);
    ASSERT_TRUE(block_copy_is(&s, t, "234\r\ncde"));
    term_free(t);
    TEST_END();
}

/* Across the scrollback and the screen. */
int test_sel_block_across_scrollback(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 20);
    feed_lines(t, 0, 12);                               /* L00..L11, view L08..L11 */
    t->scrollback_offset = 6;                           /* view L02..L05 */
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 1, 1, 3, 2));              /* L03..L05, cols 1-2 */
    t->scrollback_offset = 0;
    (void)selection_sync(&s, t);
    ASSERT_TRUE(s.valid);
    ASSERT_TRUE(block_copy_is(&s, t, "03\r\n04\r\n05"));
    ASSERT_TRUE(!selection_in_view(&s, t));
    t->scrollback_offset = 6;
    ASSERT_TRUE(selection_in_view(&s, t));
    ASSERT_TRUE(selection_contains(&s, t, 1, 2));
    ASSERT_TRUE(!selection_contains(&s, t, 1, 0));
    term_free(t);
    TEST_END();
}

/* Shift+click on a block keeps it a block: the nearest corner moves. */
int test_sel_block_extend_keeps_block(void)
{
    TEST_BEGIN();
    Terminal *t = block_term();
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 2, 1, 4));              /* corners (0,2) and (1,4) */
    selection_extend_from(&s, t, 3, 6, 0, 0);           /* past the end: bottom-right moves */
    ASSERT_TRUE(s.block);
    ASSERT_TRUE(block_copy_is(&s, t, "23456\r\ncdefg\r\nMNOPQ\r\nwxyzA"));
    selection_extend(&s, t, 3, 7, 100, 100, 4, 4);      /* the drag is live: the anchor is (0,2) */
    ASSERT_TRUE(block_copy_is(&s, t, "234567\r\ncdefgh\r\nMNOPQR\r\nwxyzAB"));
    ASSERT_TRUE(selection_finish(&s, t));
    selection_extend_from(&s, t, 0, 0, 0, 0);           /* before the start: top-left moves */
    ASSERT_TRUE(s.block);
    ASSERT_TRUE(block_copy_is(&s, t, "01234567\r\nabcdefgh\r\nKLMNOPQR\r\nuvwxyzAB"));
    term_free(t);
    TEST_END();
}

/* A plain click starts a stream selection again. */
int test_sel_block_cleared_by_plain_click(void)
{
    TEST_BEGIN();
    Terminal *t = block_term();
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 2, 2, 4));
    ASSERT_TRUE(s.block);
    selection_begin(&s, t, 1, 3, 1, 0, 0);
    ASSERT_TRUE(!s.block);
    ASSERT_TRUE(!s.valid);
    selection_extend(&s, t, 2, 4, 100, 100, 4, 4);
    ASSERT_TRUE(selection_finish(&s, t));
    ASSERT_TRUE(block_copy_is(&s, t, "defghij\r\nKLMNO"));
    term_free(t);
    TEST_END();
}

/* Output that overwrites the rectangle clears it, as for a stream. */
int test_sel_block_output_into_rectangle_clears(void)
{
    TEST_BEGIN();
    Terminal *t = block_term();
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 2, 1, 4));
    feed(t, "\x1b[2;1Hzz");
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_TRUE(!s.valid);
    term_free(t);
    TEST_END();
}

/* A recycled start trims the rows but not the columns. */
int test_sel_block_recycle_keeps_columns(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 2);                  /* capacity 6 */
    feed_lines(t, 0, 6);
    t->scrollback_offset = 2;
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 1, 2, 2));              /* L00..L02, cols 1-2 */
    feed(t, "\r\nL06");                                 /* recycles L00 */
    t->scrollback_offset = 0;
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.start.col, 1);
    ASSERT_EQ(s.end.col, 2);
    ASSERT_TRUE(block_copy_is(&s, t, "01\r\n02"));
    term_free(t);
    TEST_END();
}

int test_sel_block_select_all_is_not_a_block(void)
{
    TEST_BEGIN();
    Terminal *t = block_term();
    Selection s;
    ASSERT_TRUE(bdrag(&s, t, 0, 2, 1, 4));
    selection_select_all(&s, t);
    ASSERT_TRUE(!s.block);
    term_free(t);
    TEST_END();
}

/* ---- the lone-Alt-tap decision ------------------------------------------ */

int test_alt_tap_alone_opens_menu(void)
{
    TEST_BEGIN();
    AltTap a;
    alt_tap_reset(&a);
    alt_tap_key_down(&a, false);
    ASSERT_TRUE(!alt_tap_key_up(&a));                   /* not swallowed: the menu opens */
    TEST_END();
}

int test_alt_tap_mouse_press_swallows_release(void)
{
    TEST_BEGIN();
    AltTap a;
    alt_tap_reset(&a);
    alt_tap_key_down(&a, false);
    alt_tap_key_down(&a, true);                         /* auto-repeat */
    alt_tap_mouse_press(&a);
    alt_tap_key_down(&a, true);                         /* repeat after the press */
    ASSERT_TRUE(alt_tap_key_up(&a));
    /* The next tap is a fresh one. */
    alt_tap_key_down(&a, false);
    ASSERT_TRUE(!alt_tap_key_up(&a));
    TEST_END();
}

int test_alt_tap_focus_loss_forgets(void)
{
    TEST_BEGIN();
    AltTap a;
    alt_tap_reset(&a);
    alt_tap_key_down(&a, false);
    alt_tap_mouse_press(&a);
    alt_tap_reset(&a);                                  /* the key-up went elsewhere */
    alt_tap_key_down(&a, false);
    ASSERT_TRUE(!alt_tap_key_up(&a));
    TEST_END();
}

/* A mouse press with Alt held before the window saw the key-down. */
int test_alt_tap_press_without_seen_key_down(void)
{
    TEST_BEGIN();
    AltTap a;
    alt_tap_reset(&a);
    alt_tap_mouse_press(&a);
    ASSERT_TRUE(alt_tap_key_up(&a));
    ASSERT_TRUE(!alt_tap_key_up(&a));                   /* consumed once */
    TEST_END();
}

/* ---- click cycling ------------------------------------------------------ */

int test_sel_click_count_cycles_one_two_three(void)
{
    TEST_BEGIN();
    ASSERT_EQ(selection_click_count(0, true), 1);
    ASSERT_EQ(selection_click_count(1, true), 2);
    ASSERT_EQ(selection_click_count(2, true), 3);
    ASSERT_EQ(selection_click_count(3, true), 1);             /* a fourth click starts over */
    ASSERT_EQ(selection_click_count(2, false), 1);
    ASSERT_EQ(selection_click_count(3, false), 1);
    ASSERT_EQ(selection_click_count(1, false), 1);
    int c = 0;
    for (int i = 0; i < 7; i++) c = selection_click_count(c, true);
    ASSERT_EQ(c, 1);                                          /* 1,2,3,1,2,3,1 */
    TEST_END();
}

/* ---- combining marks join the word before them -------------------------- */

int test_sel_is_combining_ranges(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(selection_is_combining(0x0300));
    ASSERT_TRUE(selection_is_combining(0x0301));
    ASSERT_TRUE(selection_is_combining(0x036F));
    ASSERT_FALSE(selection_is_combining(0x02FF));
    ASSERT_FALSE(selection_is_combining(0x0370));
    ASSERT_TRUE(selection_is_combining(0x1AB0));
    ASSERT_TRUE(selection_is_combining(0x1AFF));
    ASSERT_TRUE(selection_is_combining(0x1DC0));
    ASSERT_TRUE(selection_is_combining(0x1DFF));
    ASSERT_TRUE(selection_is_combining(0x20D0));
    ASSERT_TRUE(selection_is_combining(0x20FF));
    ASSERT_TRUE(selection_is_combining(0xFE20));
    ASSERT_TRUE(selection_is_combining(0xFE2F));
    ASSERT_FALSE(selection_is_combining(0xFE30));
    ASSERT_FALSE(selection_is_combining('e'));
    ASSERT_FALSE(selection_is_combining(0));
    ASSERT_EQ(selection_char_class(0x0301), 2 + 0x0301);      /* alone, its own class */
    TEST_END();
}

int test_sel_word_combining_mark_joins_base(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "cafe\xCC\x81 noir");                            /* e + U+0301 in cells 3 and 4 */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 3, 2));                     /* the e */
    ASSERT_EQ(s.start.col, 0);
    ASSERT_EQ(s.end.col, 4);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "cafe\xCC\x81");
    free(txt);
    ASSERT_TRUE(click_n(&s, t, 0, 4, 2));                     /* the mark itself */
    txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "cafe\xCC\x81");
    free(txt);
    ASSERT_TRUE(click_n(&s, t, 0, 0, 2));                     /* the c */
    txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "cafe\xCC\x81");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_combining_does_not_disturb_other_words(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "cafe\xCC\x81 noir");
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 7, 2));
    ASSERT_EQ(s.start.col, 6);
    ASSERT_EQ(s.end.col, 9);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "noir");
    free(txt);
    term_free(t);
    TEST_END();
}

/* A mark after a blank has nothing to combine with: it neither extends the
 * blank's run nor pulls the next word back over it. */
int test_sel_word_combining_after_space_does_not_join_space(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, " \xCC\x81x");                                    /* space, mark, x */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 0, 2));                     /* the space */
    ASSERT_EQ(s.start.col, 0);
    ASSERT_EQ(s.end.col, 0);
    ASSERT_TRUE(click_n(&s, t, 0, 2, 2));                     /* the x */
    ASSERT_EQ(s.start.col, 2);
    ASSERT_EQ(s.end.col, 2);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "x");
    free(txt);
    term_free(t);
    TEST_END();
}

/* A blank run takes in no mark from either side: the mark is a class of its
 * own, so a space on each side of it is a run of one. */
int test_sel_word_blank_run_takes_no_mark_either_side(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, " \xCC\x81 x");                                 /* space, mark, space, x */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 0, 2));
    ASSERT_EQ(s.start.col, 0);
    ASSERT_EQ(s.end.col, 0);
    ASSERT_TRUE(click_n(&s, t, 0, 2, 2));
    ASSERT_EQ(s.start.col, 2);
    ASSERT_EQ(s.end.col, 2);
    term_free(t);
    TEST_END();
}

/* Ctrl+Alt (or AltGr): the Alt key-down and key-up are not system messages,
 * so only a non-system key-up reset tells the state. After it a lone tap
 * opens the menu again. */
int test_alt_tap_reset_after_non_system_key_up(void)
{
    TEST_BEGIN();
    AltTap a;
    alt_tap_reset(&a);
    alt_tap_key_down(&a, false);
    alt_tap_mouse_press(&a);                            /* Ctrl+Alt+click */
    alt_tap_reset(&a);                                  /* WM_KEYUP of VK_MENU */
    alt_tap_key_down(&a, false);
    ASSERT_TRUE(!alt_tap_key_up(&a));
    TEST_END();
}

/* A press whose Alt key-up never came: the next fresh Alt key-down (repeat
 * bit clear) starts a new tap, whatever the state recorded. */
int test_alt_tap_stale_press_does_not_swallow_next_tap(void)
{
    TEST_BEGIN();
    AltTap a;
    alt_tap_reset(&a);
    alt_tap_mouse_press(&a);                            /* no key-down was recorded */
    alt_tap_key_down(&a, false);
    ASSERT_TRUE(!alt_tap_key_up(&a));
    TEST_END();
}

/* Alt already recorded down: a press swallows its release; a repeat does not
 * clear that. */
int test_alt_tap_press_with_alt_already_down(void)
{
    TEST_BEGIN();
    AltTap a;
    alt_tap_reset(&a);
    alt_tap_key_down(&a, false);
    alt_tap_key_down(&a, true);
    alt_tap_mouse_press(&a);
    alt_tap_key_down(&a, true);
    ASSERT_TRUE(alt_tap_key_up(&a));
    TEST_END();
}
