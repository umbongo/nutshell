#include "test_framework.h"
#include "term_extract.h"
#include "term.h"
#include <string.h>

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
