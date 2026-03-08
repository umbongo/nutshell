#include "test_framework.h"
#include "selection.h"
#include "term.h"
#include <string.h>

/* ---- pixel_to_cell tests ------------------------------------------------ */

int test_sel_pixel_to_cell_basic(void)
{
    TEST_BEGIN();
    int row, col;
    /* Pixel (40, 64) with char 8x16, y_offset=32 → col=5, row=2 */
    selection_pixel_to_cell(40, 64, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 2);
    ASSERT_EQ(col, 5);
    TEST_END();
}

int test_sel_pixel_to_cell_origin(void)
{
    TEST_BEGIN();
    int row, col;
    /* Top-left of terminal area */
    selection_pixel_to_cell(0, 32, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

int test_sel_pixel_to_cell_clamp_negative(void)
{
    TEST_BEGIN();
    int row, col;
    /* Click above terminal area (in tab strip) */
    selection_pixel_to_cell(0, 10, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

int test_sel_pixel_to_cell_clamp_max(void)
{
    TEST_BEGIN();
    int row, col;
    /* Click beyond terminal bounds */
    selection_pixel_to_cell(800, 500, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 23);
    ASSERT_EQ(col, 79);
    TEST_END();
}

int test_sel_pixel_to_cell_mid_cell(void)
{
    TEST_BEGIN();
    int row, col;
    /* Click in the middle of a cell: pixel (12, 44) with char 8x16, y_offset=32
     * col = 12/8 = 1, row = (44-32)/16 = 0 */
    selection_pixel_to_cell(12, 44, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 1);
    TEST_END();
}

/* ---- normalise tests ---------------------------------------------------- */

int test_sel_normalise_forward(void)
{
    TEST_BEGIN();
    Selection sel = { .start_row=1, .start_col=5, .end_row=3, .end_col=10 };
    int r0, c0, r1, c1;
    selection_normalise(&sel, &r0, &c0, &r1, &c1);
    ASSERT_EQ(r0, 1); ASSERT_EQ(c0, 5);
    ASSERT_EQ(r1, 3); ASSERT_EQ(c1, 10);
    TEST_END();
}

int test_sel_normalise_backward(void)
{
    TEST_BEGIN();
    /* Dragged upward: end before start */
    Selection sel = { .start_row=3, .start_col=10, .end_row=1, .end_col=5 };
    int r0, c0, r1, c1;
    selection_normalise(&sel, &r0, &c0, &r1, &c1);
    ASSERT_EQ(r0, 1); ASSERT_EQ(c0, 5);
    ASSERT_EQ(r1, 3); ASSERT_EQ(c1, 10);
    TEST_END();
}

int test_sel_normalise_same_row_backward(void)
{
    TEST_BEGIN();
    Selection sel = { .start_row=2, .start_col=15, .end_row=2, .end_col=3 };
    int r0, c0, r1, c1;
    selection_normalise(&sel, &r0, &c0, &r1, &c1);
    ASSERT_EQ(r0, 2); ASSERT_EQ(c0, 3);
    ASSERT_EQ(r1, 2); ASSERT_EQ(c1, 15);
    TEST_END();
}

/* ---- extract text tests ------------------------------------------------- */

static void feed(Terminal *t, const char *s) {
    term_process(t, s, strlen(s));
}

int test_sel_extract_single_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "Hello World");  /* wraps: "Hello Worl" on row 0, "d" on row 1 */
    /* Select "llo W" from row 0 cols 2-6 */
    Selection sel = { .start_row=0, .start_col=2, .end_row=0, .end_col=6 };
    char buf[64];
    size_t n = selection_extract_text(&sel, t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(strcmp(buf, "llo W"), 0);
    term_free(t);
    TEST_END();
}

int test_sel_extract_multi_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 0);
    feed(t, "AAAA\r\nBBBB\r\nCCCC");
    /* row 0: AAAA, row 1: BBBB, row 2: CCCC */
    /* Select from row 0 col 2 to row 2 col 2 → "AA\nBBBB\nCCC" */
    Selection sel = { .start_row=0, .start_col=2, .end_row=2, .end_col=2 };
    char buf[64];
    size_t n = selection_extract_text(&sel, t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(strcmp(buf, "AA\nBBBB\nCCC"), 0);
    term_free(t);
    TEST_END();
}

int test_sel_extract_empty(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    /* Same start and end on an empty cell → empty string (trailing space trim) */
    Selection sel = { .start_row=0, .start_col=3, .end_row=0, .end_col=3 };
    char buf[64];
    size_t n = selection_extract_text(&sel, t, buf, sizeof(buf));
    ASSERT_EQ(n, 0);
    ASSERT_EQ(buf[0], '\0');
    term_free(t);
    TEST_END();
}

int test_sel_extract_null_safety(void)
{
    TEST_BEGIN();
    Selection sel = { .start_row=0, .start_col=0, .end_row=0, .end_col=5 };
    char buf[64];
    ASSERT_EQ(selection_extract_text(NULL, NULL, buf, sizeof(buf)), 0);
    ASSERT_EQ(selection_extract_text(&sel, NULL, buf, sizeof(buf)), 0);
    ASSERT_EQ(selection_extract_text(&sel, NULL, NULL, 0), 0);
    TEST_END();
}

int test_sel_extract_trims_trailing_spaces(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "Hi");  /* row 0: "Hi" + 8 empty cells */
    /* Select entire row 0 (cols 0-9) → should get "Hi" not "Hi        " */
    Selection sel = { .start_row=0, .start_col=0, .end_row=0, .end_col=9 };
    char buf[64];
    selection_extract_text(&sel, t, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "Hi"), 0);
    term_free(t);
    TEST_END();
}

int test_sel_extract_backward_selection(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    /* Backward drag: end before start on same row */
    Selection sel = { .start_row=0, .start_col=7, .end_row=0, .end_col=2 };
    char buf[64];
    selection_extract_text(&sel, t, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "CDEFGH"), 0);
    term_free(t);
    TEST_END();
}

/* Regression: selecting exactly one character must not return empty.
 * Bug: start==end was treated as "no selection" instead of single char. */
int test_sel_extract_single_char(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    /* Click and release on same cell → should select that one character */
    Selection sel = { .start_row=0, .start_col=3, .end_row=0, .end_col=3 };
    char buf[64];
    size_t n = selection_extract_text(&sel, t, buf, sizeof(buf));
    ASSERT_EQ(n, 1);
    ASSERT_EQ(buf[0], 'D');
    term_free(t);
    TEST_END();
}

/* Verify that the last character of a full-row selection is included. */
int test_sel_extract_includes_last_char(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 0);
    feed(t, "thomas@tompi:~$");
    /* Select entire prompt: col 0 to col 14 inclusive */
    Selection sel = { .start_row=0, .start_col=0, .end_row=0, .end_col=14 };
    char buf[64];
    selection_extract_text(&sel, t, buf, sizeof(buf));
    ASSERT_EQ(strcmp(buf, "thomas@tompi:~$"), 0);
    term_free(t);
    TEST_END();
}
