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
