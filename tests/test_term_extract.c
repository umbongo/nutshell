#include "test_framework.h"
#include "term_extract.h"
#include "term.h"
#include "ai_prompt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int test_extract_empty_term(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    char buf[1024];
    size_t n = term_extract_visible(t, buf, sizeof(buf));
    /* Empty terminal: all rows are blank, nothing to extract */
    ASSERT_EQ((int)n, 0);
    ASSERT_STR_EQ(buf, "");
    term_free(t);
    TEST_END();
}

int test_extract_single_line(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "hello", 5);
    char buf[1024];
    size_t n = term_extract_visible(t, buf, sizeof(buf));
    /* Only first row has content; remaining 23 blank rows add nothing */
    ASSERT_TRUE(n > 0);
    /* First line should be "hello" */
    ASSERT_TRUE(strncmp(buf, "hello", 5) == 0);
    term_free(t);
    TEST_END();
}

int test_extract_multi_line(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    term_process(t, "line1\r\nline2\r\nline3", 19);
    char buf[1024];
    size_t n = term_extract_visible(t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* Should contain "line1\nline2\nline3" */
    ASSERT_TRUE(strstr(buf, "line1\nline2\nline3") != NULL);
    term_free(t);
    TEST_END();
}

int test_extract_trims_trailing_spaces(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 100);
    term_process(t, "abc", 3);
    char buf[1024];
    term_extract_visible(t, buf, sizeof(buf));
    /* First line should be exactly "abc", not "abc   ..." */
    char *nl = strchr(buf, '\n');
    int first_line_len = nl ? (int)(nl - buf) : (int)strlen(buf);
    ASSERT_EQ(first_line_len, 3);
    term_free(t);
    TEST_END();
}

int test_extract_utf8_codepoint(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 100);
    /* Euro sign: U+20AC = E2 82 AC in UTF-8 */
    term_process(t, "\xE2\x82\xAC", 3);
    char buf[1024];
    size_t n = term_extract_visible(t, buf, sizeof(buf));
    ASSERT_TRUE(n >= 3);
    ASSERT_TRUE((unsigned char)buf[0] == 0xE2);
    ASSERT_TRUE((unsigned char)buf[1] == 0x82);
    ASSERT_TRUE((unsigned char)buf[2] == 0xAC);
    term_free(t);
    TEST_END();
}

/* Hardening: a cell holding a raw control byte must extract as a space,
 * never the control byte itself -- term_extract_visible() feeds the AI
 * context, so a stray control character here would reach the model raw. */
int test_extract_control_cell_becomes_space(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 100);
    term_process(t, "A\x7F" "B", 3);   /* DEL lands in the middle cell */
    char buf[1024];
    size_t n = term_extract_visible(t, buf, sizeof(buf));
    ASSERT_EQ((int)n, 3);
    ASSERT_STR_EQ(buf, "A B");
    term_free(t);
    TEST_END();
}

/* Same, for a C1 control reached via its UTF-8 encoding. */
int test_extract_c1_control_becomes_space(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 100);
    term_process(t, "A\xC2\x80" "B", 4);   /* U+0080 lands in the middle cell */
    char buf[1024];
    size_t n = term_extract_visible(t, buf, sizeof(buf));
    ASSERT_EQ((int)n, 3);
    ASSERT_STR_EQ(buf, "A B");
    term_free(t);
    TEST_END();
}

int test_extract_buf_too_small(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    term_process(t, "hello world", 11);
    char buf[6]; /* only room for "hello" + NUL */
    size_t n = term_extract_visible(t, buf, sizeof(buf));
    ASSERT_TRUE(n <= 5);
    ASSERT_TRUE(buf[n] == '\0'); /* always NUL-terminated */
    term_free(t);
    TEST_END();
}

int test_extract_null_safety(void) {
    TEST_BEGIN();
    char buf[64];
    ASSERT_EQ((int)term_extract_visible(NULL, buf, sizeof(buf)), 0);
    Terminal *t = term_init(5, 10, 100);
    ASSERT_EQ((int)term_extract_visible(t, NULL, 64), 0);
    ASSERT_EQ((int)term_extract_visible(t, buf, 0), 0);
    term_free(t);
    TEST_END();
}

int test_extract_last_n_basic(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    term_process(t, "aaa\r\nbbb\r\nccc\r\nddd\r\neee", 23);
    char buf[1024];
    size_t n = term_extract_last_n(t, 2, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* Last 2 visible rows: "ddd" and "eee" */
    ASSERT_TRUE(strstr(buf, "ddd\neee") != NULL);
    term_free(t);
    TEST_END();
}

/* Regression (2026-09-25): in the sparse-buffer state right after a
 * resize (lines_count < rows -- see term_screen_to_phys()'s comment in
 * src/term/term.h), the newest output can sit in a screen row past
 * lines_count. term_extract_last_n() -- what feeds the AI model's view of
 * the terminal -- used to scan only [0, lines_count) for "the last row
 * with content", so it could miss that output entirely and hand the
 * model a stale or empty screen in a freshly resized tab. Same
 * 50-rows-shrunk-to-29 reproduction as test_term_at_prompt_sparse_after_
 * resize_shrink (test_term.c). The sparse state is now forced manually
 * (lines_count = 1) because term_reflow_buffer() keeps lines_count >= rows
 * since the fix; the consumer tolerance is still worth pinning. */
int test_extract_last_n_sparse_after_resize_shrink(void) {
    TEST_BEGIN();
    Terminal *t = term_init(50, 76, 3000);
    term_resize(t, 29, 76);
    t->lines_count = 1; /* forced: the resize no longer leaves it sparse */
    ASSERT_EQ(t->lines_count, 1);

    static const char out[] = "line one\r\nline two\r\nPS C:\\Users\\thoma> ";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_TRUE(t->cursor.row >= t->lines_count);

    char buf[1024];
    size_t n = term_extract_last_n(t, 50, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "line one") != NULL);
    ASSERT_TRUE(strstr(buf, "line two") != NULL);
    ASSERT_TRUE(strstr(buf, "PS C:\\Users\\thoma>") != NULL);

    term_free(t);
    TEST_END();
}

int test_extract_last_n_with_scrollback(void) {
    TEST_BEGIN();
    /* 3 rows, 5 scrollback. Write 6 lines to push into scrollback. */
    Terminal *t = term_init(3, 10, 5);
    term_process(t, "L1\r\nL2\r\nL3\r\nL4\r\nL5\r\nL6", 22);
    char buf[1024];
    /* Last 5 lines should include scrollback content */
    size_t n = term_extract_last_n(t, 5, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "L2") != NULL);
    ASSERT_TRUE(strstr(buf, "L6") != NULL);
    term_free(t);
    TEST_END();
}

int test_extract_last_n_exceeds_total(void) {
    TEST_BEGIN();
    Terminal *t = term_init(3, 10, 5);
    term_process(t, "A\r\nB", 4);
    char buf[1024];
    /* Ask for 100 rows but only ~3 exist */
    size_t n = term_extract_last_n(t, 100, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "A") != NULL);
    ASSERT_TRUE(strstr(buf, "B") != NULL);
    term_free(t);
    TEST_END();
}

int test_extract_last_n_zero(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    term_process(t, "hello", 5);
    char buf[64];
    ASSERT_EQ((int)term_extract_last_n(t, 0, buf, sizeof(buf)), 0);
    ASSERT_EQ((int)term_extract_last_n(t, -1, buf, sizeof(buf)), 0);
    term_free(t);
    TEST_END();
}

int test_extract_last_n_ignores_trailing_blank_rows(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "line1\r\nline2\r\nline3", 19);
    char buf[1024];

    size_t n2 = term_extract_last_n(t, 2, buf, sizeof(buf));
    buf[n2] = '\0';
    ASSERT_STR_EQ(buf, "line2\nline3");

    size_t n1 = term_extract_last_n(t, 1, buf, sizeof(buf));
    buf[n1] = '\0';
    ASSERT_STR_EQ(buf, "line3");

    size_t n10 = term_extract_last_n(t, 10, buf, sizeof(buf));
    buf[n10] = '\0';
    ASSERT_STR_EQ(buf, "line1\nline2\nline3");

    term_free(t);
    TEST_END();
}

int test_extract_last_n_all_blank(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    char buf[1024];
    size_t n = term_extract_last_n(t, 5, buf, sizeof(buf));
    ASSERT_EQ((int)n, 0);
    ASSERT_STR_EQ(buf, "");
    term_free(t);
    TEST_END();
}

int test_extract_last_n_utf8_fits_context_buffer(void) {
    TEST_BEGIN();
    /* rows > number of lines written: term_extract_last_n must anchor at
     * the last non-blank row, not at lines_count (which is fixed at rows
     * for a screen that has never scrolled). */
    Terminal *t = term_init(5, 20, 100);

    /* One row: 20 copies of U+2500 BOX DRAWINGS LIGHT HORIZONTAL ("─"),
     * UTF-8 encoded as 0xE2 0x94 0x80 (3 bytes each). */
    char line[61];
    int p = 0;
    for (int i = 0; i < 20; i++) {
        line[p++] = (char)0xE2;
        line[p++] = (char)0x94;
        line[p++] = (char)0x80;
    }
    line[p] = '\0'; /* p == 60 */

    char input[200];
    int ip = 0;
    memcpy(input + ip, line, 60); ip += 60;
    input[ip++] = '\r'; input[ip++] = '\n';
    memcpy(input + ip, line, 60); ip += 60;
    input[ip++] = '\r'; input[ip++] = '\n';
    memcpy(input + ip, line, 60); ip += 60;
    /* no trailing newline after the third line */

    term_process(t, input, (size_t)ip);

    size_t bufsize = ai_context_buf_size(3, 20);
    char *buf = (char *)malloc(bufsize);
    ASSERT_NOT_NULL(buf);

    size_t n = term_extract_last_n(t, 3, buf, bufsize);

    int nl_count = 0;
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '\n') nl_count++;
    ASSERT_EQ(nl_count, 2);
    ASSERT_EQ((int)n, 3 * 20 * 3 + 2);
    ASSERT_TRUE(n >= 3);
    ASSERT_TRUE((unsigned char)buf[n - 3] == 0xE2);
    ASSERT_TRUE((unsigned char)buf[n - 2] == 0x94);
    ASSERT_TRUE((unsigned char)buf[n - 1] == 0x80);

    free(buf);
    term_free(t);
    TEST_END();
}

int test_extract_buf_too_small_keeps_newest(void) {
    TEST_BEGIN();
    /* rows > number of lines written: must still anchor at the last
     * non-blank row. */
    Terminal *t = term_init(5, 10, 100);
    term_process(t, "aaaa\r\nbbbb\r\ncccc", 16);

    char buf[10]; /* room for "bbbb\ncccc" = 9 chars + NUL */
    term_extract_last_n(t, 3, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "bbbb\ncccc");

    char buf2[5]; /* room for "cccc" + NUL */
    term_extract_last_n(t, 3, buf2, sizeof(buf2));
    ASSERT_STR_EQ(buf2, "cccc");

    term_free(t);
    TEST_END();
}

/* term_extract_last_n_dup(): sized to the content, so a row wider than any
 * fixed buffer keeps its end -- where a prompt's closing '$'/'#'/'>' is
 * (M6, 2026-09-24: the contradiction check read 4 rows into 256 bytes and a
 * wide prompt row lost its end). */
int test_extract_last_n_dup_wide_row_keeps_prompt(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 400, 100);
    char line[400];
    memset(line, 'x', 300);
    memcpy(line + 300, " tom@webhost:~$", 15);
    term_process(t, "some output\r\n", 13);
    term_process(t, line, 315);

    size_t len = 0;
    char *row = term_extract_last_n_dup(t, 1, &len);
    ASSERT_NOT_NULL(row);
    if (row) {
        ASSERT_EQ((int)len, 315);
        ASSERT_EQ((int)strlen(row), 315);
        ASSERT_EQ(row[len - 1], '$');
        ASSERT_TRUE(strchr(row, '\n') == NULL);   /* one row only */
    }
    free(row);

    /* The fixed-buffer path this replaces cuts the same row short. */
    char small[256];
    size_t n = term_extract_last_n(t, 4, small, sizeof small);
    ASSERT_TRUE(n < 315);
    ASSERT_TRUE(small[n - 1] != '$');

    term_free(t);
    TEST_END();
}

/* The 40-row scan: wide rows of multi-byte text that would overflow a 4 KB
 * buffer come back whole, oldest row first, newest row last. */
int test_extract_last_n_dup_many_wide_rows_whole(void) {
    TEST_BEGIN();
    Terminal *t = term_init(50, 200, 200);
    /* 40 rows of 200 box-drawing cells (3 bytes each) = 24,000 bytes. */
    for (int r = 0; r < 40; r++) {
        for (int c = 0; c < 200; c++)
            term_process(t, "\xe2\x94\x80", 3);
        if (r < 39) term_process(t, "\r\n", 2);
    }
    size_t len = 0;
    char *buf = term_extract_last_n_dup(t, 40, &len);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ((int)len, 40 * 600 + 39);
    if (buf) ASSERT_EQ((int)strlen(buf), (int)len);
    free(buf);
    term_free(t);
    TEST_END();
}

/* Nothing to extract, or bad arguments: NULL with a zero length. */
int test_extract_last_n_dup_empty_and_null(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 10);
    size_t len = 99;
    ASSERT_NULL(term_extract_last_n_dup(t, 3, &len));
    ASSERT_EQ((int)len, 0);
    len = 99;
    ASSERT_NULL(term_extract_last_n_dup(NULL, 3, &len));
    ASSERT_EQ((int)len, 0);
    term_process(t, "abc", 3);
    ASSERT_NULL(term_extract_last_n_dup(t, 0, &len));
    char *s = term_extract_last_n_dup(t, 3, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "abc");
    free(s);
    term_free(t);
    TEST_END();
}

/* ---- Absolute-line capture (spec 2026-11-05-run-command-tool, decision 10) ---- */

static void feed_str(Terminal *t, const char *s)
{
    term_process(t, s, strlen(s));
}

/* Feed "L0".."L<n-1>", one per line, no trailing newline. */
static void feed_numbered(Terminal *t, int n)
{
    char b[32];
    for (int i = 0; i < n; i++) {
        int k = snprintf(b, sizeof b, i < n - 1 ? "L%d\r\n" : "L%d", i);
        term_process(t, b, (size_t)k);
    }
}

/* The cursor's absolute line is lines_recycled plus its logical index, and
 * scrolling the view back does not move it. */
int test_term_abs_cursor_line(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    ASSERT_EQ((int)term_abs_cursor_line(NULL), 0);
    ASSERT_EQ((int)term_abs_cursor_line(t), 0);
    feed_numbered(t, 12);
    ASSERT_EQ(t->lines_count, 12);
    int top = t->lines_count - t->rows;
    ASSERT_EQ((int)term_abs_cursor_line(t),
              (int)t->lines_recycled + top + t->cursor.row);
    ASSERT_EQ((int)term_abs_cursor_line(t), 11);
    t->scrollback_offset = 3;
    ASSERT_EQ((int)term_abs_cursor_line(t), 11);
    t->scrollback_offset = 0;

    /* With rows recycled the number keeps counting from the first line ever. */
    Terminal *u = term_init(5, 20, 10);
    feed_numbered(u, 100);
    ASSERT_TRUE(u->lines_recycled > 0);
    ASSERT_EQ((int)term_abs_cursor_line(u), 99);
    term_free(u);
    term_free(t);
    TEST_END();
}

int test_term_abs_range_across_scrollback(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    feed_numbered(t, 12);
    size_t len = 99;
    unsigned flags = 99;
    char *s = term_extract_abs_range_dup(t, 2, 10, NULL, &len, &flags);
    ASSERT_NOT_NULL(s);
    if (s) {
        ASSERT_STR_EQ(s, "L2\nL3\nL4\nL5\nL6\nL7\nL8\nL9");
        ASSERT_EQ((int)len, (int)strlen(s));
    }
    ASSERT_EQ((int)flags, 0);
    free(s);
    /* An end past the buffer is clamped to it. */
    s = term_extract_abs_range_dup(t, 10, 500, NULL, &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "L10\nL11");
    free(s);
    /* NULL terminal, NULL out pointers. */
    ASSERT_NULL(term_extract_abs_range_dup(NULL, 0, 5, NULL, &len, &flags));
    ASSERT_EQ((int)len, 0);
    s = term_extract_abs_range_dup(t, 0, 1, NULL, NULL, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "L0");
    free(s);
    term_free(t);
    TEST_END();
}

int test_term_abs_range_echo_dropped(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    feed_str(t, "$ ls\r\nout1\r\nout2\r\n");
    size_t len = 0;
    char *s = term_extract_abs_range_dup(t, 0, term_abs_cursor_line(t) + 1,
                                         "ls", &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) {
        ASSERT_STR_EQ(s, "out1\nout2");
        ASSERT_EQ((int)len, 9);
    }
    free(s);
    /* The same range without an echo keeps the prompt row. */
    s = term_extract_abs_range_dup(t, 0, term_abs_cursor_line(t) + 1, NULL, &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "$ ls\nout1\nout2");
    free(s);
    /* An empty echo drops nothing. */
    s = term_extract_abs_range_dup(t, 0, term_abs_cursor_line(t) + 1, "", &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "$ ls\nout1\nout2");
    free(s);
    term_free(t);
    TEST_END();
}

/* A 29-byte command on a 20-column screen wraps; both rows are the echo. */
int test_term_abs_range_echo_wrapped(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    const char *cmd = "echo aaaaaaaaaaaaaaaaaaaaaaaa";   /* 29 chars */
    char line[80];
    int n = snprintf(line, sizeof line, "$ %s\r\nresult\r\n", cmd);
    term_process(t, line, (size_t)n);
    ASSERT_TRUE(strlen(cmd) + 2 > 20);                    /* it did wrap */
    size_t len = 0;
    char *s = term_extract_abs_range_dup(t, 0, term_abs_cursor_line(t) + 1,
                                         cmd, &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "result");
    free(s);
    /* A space at the wrap boundary does not break the match. */
    Terminal *u = term_init(5, 20, 50);
    const char *cmd2 = "ab cd ef gh ij kl mn op";         /* "$ " + 18 chars ends in a space on row 0 */
    n = snprintf(line, sizeof line, "$ %s\r\nok\r\n", cmd2);
    term_process(u, line, (size_t)n);
    s = term_extract_abs_range_dup(u, 0, term_abs_cursor_line(u) + 1, cmd2, &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "ok");
    free(s);
    term_free(u);
    term_free(t);
    TEST_END();
}

int test_term_abs_range_echo_kept(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    feed_str(t, "$ ls\r\nout1\r\n");
    size_t len = 0;
    char *s = term_extract_abs_range_dup(t, 0, term_abs_cursor_line(t) + 1,
                                         "cat", &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "$ ls\nout1");
    free(s);
    /* A range that starts on a continuation row does not start a logical
     * line: nothing is dropped even if the row contains the echo. */
    Terminal *u = term_init(5, 20, 50);
    feed_str(u, "aaaaaaaaaaaaaaaaaaaaXYZ\r\nnext");
    s = term_extract_abs_range_dup(u, 1, 3, "XYZ", &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "XYZ\nnext");
    free(s);
    term_free(u);
    term_free(t);
    TEST_END();
}

int test_term_abs_range_trailing_blank(void) {
    TEST_BEGIN();
    Terminal *t = term_init(8, 20, 50);
    feed_str(t, "a\r\n\r\nb\r\n   \r\n");
    size_t len = 0;
    char *s = term_extract_abs_range_dup(t, 0, 8, NULL, &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) {
        ASSERT_STR_EQ(s, "a\n\nb");          /* the inner blank row stays */
        ASSERT_EQ((int)len, 4);
    }
    free(s);
    /* Only blank rows: empty text, zero length. */
    s = term_extract_abs_range_dup(t, 3, 8, NULL, &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "");
    ASSERT_EQ((int)len, 0);
    free(s);
    term_free(t);
    TEST_END();
}

int test_term_abs_range_start_recycled(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    feed_numbered(t, 200);
    ASSERT_TRUE(t->lines_recycled > 0);
    size_t len = 0;
    unsigned flags = 0;
    char *s = term_extract_abs_range_dup(t, 0, 200, NULL, &len, &flags);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ((int)flags, (int)TERM_RANGE_START_RECYCLED);
    char want[16];
    snprintf(want, sizeof want, "L%d\n", (int)t->lines_recycled);
    if (s) ASSERT_TRUE(strncmp(s, want, strlen(want)) == 0);
    if (s) ASSERT_TRUE(strstr(s, "L199") != NULL);
    free(s);
    /* A range that starts at lines_recycled does not set the flag. */
    flags = 7;
    s = term_extract_abs_range_dup(t, t->lines_recycled, 200, NULL, &len, &flags);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ((int)flags, 0);
    free(s);
    /* A range wholly in the recycled part is empty, flag still set. */
    s = term_extract_abs_range_dup(t, 0, t->lines_recycled, NULL, &len, &flags);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "");
    ASSERT_EQ((int)flags, (int)TERM_RANGE_START_RECYCLED);
    free(s);
    term_free(t);
    TEST_END();
}

int test_term_abs_range_empty(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    feed_numbered(t, 4);
    size_t len = 99;
    char *s = term_extract_abs_range_dup(t, 2, 2, NULL, &len, NULL);
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "");
    ASSERT_EQ((int)len, 0);
    free(s);
    len = 99;
    s = term_extract_abs_range_dup(t, 3, 1, NULL, &len, NULL);   /* inverted */
    ASSERT_NOT_NULL(s);
    if (s) ASSERT_STR_EQ(s, "");
    ASSERT_EQ((int)len, 0);
    free(s);
    term_free(t);
    TEST_END();
}

int test_term_reflow_seq(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    ASSERT_EQ((int)t->reflow_seq, 0);
    feed_numbered(t, 12);
    ASSERT_TRUE(term_resize(t, 5, 30));
    ASSERT_EQ((int)t->reflow_seq, 1);
    ASSERT_TRUE(term_resize(t, 5, 30));              /* same size: nothing to do */
    ASSERT_EQ((int)t->reflow_seq, 1);
    ASSERT_TRUE(term_resize(t, 6, 30));
    ASSERT_EQ((int)t->reflow_seq, 2);

    /* An alternate-screen round trip leaves it, and the old rows keep their
     * absolute line numbers. */
    uint64_t line = term_abs_cursor_line(t);
    size_t len = 0;
    char *before = term_extract_abs_range_dup(t, 0, line + 1, NULL, &len, NULL);
    ASSERT_NOT_NULL(before);
    feed_str(t, "\x1b[?1049h");
    ASSERT_TRUE(t->alt_screen_active);
    ASSERT_EQ((int)t->reflow_seq, 2);
    feed_str(t, "\x1b[?1049l");
    ASSERT_FALSE(t->alt_screen_active);
    ASSERT_EQ((int)t->reflow_seq, 2);
    ASSERT_EQ((int)term_abs_cursor_line(t), (int)line);
    char *after = term_extract_abs_range_dup(t, 0, line + 1, NULL, &len, NULL);
    ASSERT_NOT_NULL(after);
    if (before && after) ASSERT_STR_EQ(after, before);
    free(before);
    free(after);
    term_free(t);
    TEST_END();
}

int test_term_region_scroll_seq(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    ASSERT_EQ((int)t->region_scroll_seq, 0);

    /* A full-screen scroll on the primary screen recycles into scrollback. */
    for (int i = 0; i < 10; i++) feed_str(t, "x\r\n");
    ASSERT_EQ((int)t->region_scroll_seq, 0);

    /* A scroll region: a newline on its last row swaps rows in place. */
    feed_str(t, "\x1b[2;4r");
    feed_str(t, "\x1b[4;1H");
    uint32_t before = t->region_scroll_seq;
    feed_str(t, "\n");
    ASSERT_TRUE(t->region_scroll_seq > before);

    /* Reverse index at the region's top row scrolls down in place. */
    feed_str(t, "\x1b[2;1H");
    before = t->region_scroll_seq;
    feed_str(t, "\x1bM");
    ASSERT_TRUE(t->region_scroll_seq > before);

    /* Insert line moves rows in place too. */
    before = t->region_scroll_seq;
    feed_str(t, "\x1b[L");
    ASSERT_TRUE(t->region_scroll_seq > before);

    /* Back to the full screen: no more bumps. */
    feed_str(t, "\x1b[r");
    before = t->region_scroll_seq;
    for (int i = 0; i < 10; i++) feed_str(t, "y\r\n");
    ASSERT_EQ((int)t->region_scroll_seq, (int)before);

    /* Entering and leaving the alternate screen does not bump it, and nor
     * does scrolling on it (its rows are its own; the capture reads the
     * alt-screen flag). Region scrolls and reverse index on it stay uncounted. */
    feed_str(t, "\x1b[?1049h");
    ASSERT_EQ((int)t->region_scroll_seq, (int)before);
    for (int i = 0; i < 10; i++) feed_str(t, "z\r\n");
    ASSERT_EQ((int)t->region_scroll_seq, (int)before);
    feed_str(t, "\x1b[2;4r\x1b[4;1H\n\x1b[2;1H\x1bM\x1b[L\x1b[r");
    ASSERT_EQ((int)t->region_scroll_seq, (int)before);
    feed_str(t, "\x1b[?1049l");
    ASSERT_EQ((int)t->region_scroll_seq, (int)before);
    feed_str(t, "\x1b[2;4r\x1b[4;1H\n\x1b[r");   /* back on the primary: counted again */
    ASSERT_TRUE(t->region_scroll_seq > before);
    before = t->region_scroll_seq;
    ASSERT_EQ((int)t->region_scroll_seq, (int)before);

    /* A resize does not bump it. */
    ASSERT_TRUE(term_resize(t, 5, 30));
    ASSERT_EQ((int)t->region_scroll_seq, (int)before);
    term_free(t);
    TEST_END();
}

int test_term_erase_seq(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 20, 50);
    ASSERT_EQ((int)t->erase_seq, 0);
    feed_str(t, "abc\x1b[2J");
    ASSERT_EQ((int)t->erase_seq, 1);
    feed_str(t, "\x1b[J\x1b[0J\x1b[1J\x1b[K\x1b[2K");   /* the other erases: not counted */
    ASSERT_EQ((int)t->erase_seq, 1);
    feed_str(t, "\x1b[3J\x1b" "c");                         /* ED 3 and RIS are not handled: nothing */
    ASSERT_EQ((int)t->erase_seq, 1);
    feed_str(t, "\x1b[2J\x1b[2J");
    ASSERT_EQ((int)t->erase_seq, 3);

    /* on the alternate screen it is not counted; back on the primary it is */
    feed_str(t, "\x1b[?1049h\x1b[2J");
    ASSERT_EQ((int)t->erase_seq, 3);
    feed_str(t, "\x1b[?1049l\x1b[2J");
    ASSERT_EQ((int)t->erase_seq, 4);

    /* a resize does not touch it */
    ASSERT_TRUE(term_resize(t, 6, 25));
    ASSERT_EQ((int)t->erase_seq, 4);
    term_free(t);
    TEST_END();
}

int test_term_cursor_low_line(void) {
    TEST_BEGIN();
    Terminal *t = term_init(12, 20, 50);
    ASSERT_EQ((int)t->cursor_low_line, 0);
    feed_str(t, "a\r\nb\r\nc\r\nd\r\n");          /* cursor on line 4 */
    ASSERT_EQ((int)term_abs_cursor_line(t), 4);
    ASSERT_EQ((int)t->cursor_low_line, 0);                /* never above the start: stays */
    term_reset_cursor_low(t);
    ASSERT_EQ((int)t->cursor_low_line, 4);
    feed_str(t, "e\r\nf\r\n");                          /* downward moves leave it */
    ASSERT_EQ((int)t->cursor_low_line, 4);

    feed_str(t, "\x1b[A");                               /* CUU: 6 -> 5 */
    ASSERT_EQ((int)t->cursor_low_line, 4);
    feed_str(t, "\x1b[2A");                              /* CUU 2: line 3 */
    ASSERT_EQ((int)t->cursor_low_line, 3);
    feed_str(t, "\x1b[3;1H");                            /* CUP row 3: line 2 */
    ASSERT_EQ((int)t->cursor_low_line, 2);
    feed_str(t, "\x1b[5;1H");                            /* back down: the mark stays */
    ASSERT_EQ((int)t->cursor_low_line, 2);
    feed_str(t, "\x1b[2d");                              /* VPA row 2: line 1 */
    ASSERT_EQ((int)t->cursor_low_line, 1);
    feed_str(t, "\x1b[5;1H\x1b[3F");                    /* CPL 3 from row 5: line 1 again */
    ASSERT_EQ((int)t->cursor_low_line, 1);
    feed_str(t, "\x1b[5;1H\x1b[4F");                    /* CPL 4: line 0 */
    ASSERT_EQ((int)t->cursor_low_line, 0);

    /* RI, DECRC and CSI u (restore) count too */
    feed_str(t, "\x1b[2;1H\x1b" "7\x1b[6;1H");             /* save on row 2 (line 1), go down */
    term_reset_cursor_low(t);
    uint64_t base = t->cursor_low_line;                      /* the bottom row */
    feed_str(t, "\x1b" "8");                                /* DECRC: back up to row 2 */
    ASSERT_TRUE(t->cursor_low_line < base);
    ASSERT_TRUE(t->cursor_low_line == term_abs_cursor_line(t));
    feed_str(t, "\x1b[2;1H\x1b[s\x1b[6;1H");
    term_reset_cursor_low(t);
    base = t->cursor_low_line;
    feed_str(t, "\x1b[u");                                  /* CSI u */
    ASSERT_TRUE(t->cursor_low_line < base);
    feed_str(t, "\x1b[6;1H");
    term_reset_cursor_low(t);
    base = t->cursor_low_line;
    feed_str(t, "\x1bM\x1bM");                             /* RI twice: two lines up */
    ASSERT_TRUE(t->cursor_low_line == base - 2);

    /* the alternate screen's own moves are not counted; its exit may lower it */
    term_reset_cursor_low(t);
    uint64_t low = t->cursor_low_line;
    feed_str(t, "\x1b[?1049h\x1b[1;1H\x1b[A");
    ASSERT_TRUE(t->cursor_low_line == low);
    feed_str(t, "\x1b[?1049l");
    ASSERT_TRUE(t->cursor_low_line <= term_abs_cursor_line(t));

    /* a committed resize sets it to 0: the numbering changed */
    term_reset_cursor_low(t);
    ASSERT_TRUE(term_resize(t, 6, 30));
    ASSERT_EQ((int)t->cursor_low_line, 0);

    /* NULL-safe */
    term_reset_cursor_low(NULL);
    term_free(t);
    TEST_END();
}

int test_term_cursor_low_scrolled(void) {
    /* On a terminal that has scrolled the mark is an absolute line: it counts
     * the recycled rows, as term_abs_cursor_line() does. */
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 50);
    for (int i = 0; i < 100; i++) feed_str(t, "x\r\n");
    ASSERT_TRUE(t->lines_recycled > 0);
    term_reset_cursor_low(t);
    uint64_t line = term_abs_cursor_line(t);
    ASSERT_EQ((int)t->cursor_low_line, (int)line);
    feed_str(t, "\x1b[H");                                /* the top of the screen */
    ASSERT_TRUE(t->cursor_low_line < line);
    ASSERT_TRUE(t->cursor_low_line == term_abs_cursor_line(t));
    ASSERT_EQ(t->cursor.row, 0);
    term_free(t);
    TEST_END();
}

int test_term_extract_cursor_prefix(void) {
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 50);
    char out[64];

    /* nothing written: empty */
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");

    /* text up to the cursor, the trailing space kept */
    feed_str(t, "user@h:~$ ");
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, sizeof out), 10);
    ASSERT_STR_EQ(out, "user@h:~$ ");

    /* text right of the cursor is not read */
    feed_str(t, "ls\x1b[2D");
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, sizeof out), 10);
    ASSERT_STR_EQ(out, "user@h:~$ ");

    /* a cursor past the written cells: those are spaces */
    feed_str(t, "\r\nab\x1b[3C");
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, sizeof out), 5);
    ASSERT_STR_EQ(out, "ab   ");

    /* count only */
    ASSERT_EQ((int)term_extract_cursor_prefix(t, NULL, 0), 5);
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, 0), 5);

    /* too small: the whole characters that fit, the full length returned */
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, 3), 5);
    ASSERT_STR_EQ(out, "ab");

    /* a multi-byte character is never cut: "é" is two bytes */
    feed_str(t, "\r\n\xc3\xa9\xc3\xa9\xc3\xa9");
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, sizeof out), 6);
    ASSERT_STR_EQ(out, "\xc3\xa9\xc3\xa9\xc3\xa9");
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, 4), 6);   /* room for one: 2 bytes + NUL fits, 2 more do not */
    ASSERT_STR_EQ(out, "\xc3\xa9");
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, 3), 6);
    ASSERT_STR_EQ(out, "\xc3\xa9");
    ASSERT_EQ((int)term_extract_cursor_prefix(t, out, 2), 6);   /* not even one fits */
    ASSERT_STR_EQ(out, "");

    /* NULL terminal */
    ASSERT_EQ((int)term_extract_cursor_prefix(NULL, out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");
    term_free(t);
    TEST_END();
}
