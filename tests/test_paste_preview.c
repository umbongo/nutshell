#include "test_framework.h"
#include "paste_preview.h"
#include <string.h>
#include <stdlib.h>

/* ---- paste_format_lines ------------------------------------------------- */

int test_paste_format_lines_single(void)
{
    TEST_BEGIN();
    int count = 0;
    char **lines = paste_format_lines("ls -la", &count);
    ASSERT_EQ(count, 1);
    ASSERT_NOT_NULL(lines);
    ASSERT_STR_EQ(lines[0], "ls -la");
    paste_line_free(lines, count);
    TEST_END();
}

int test_paste_format_lines_multi(void)
{
    TEST_BEGIN();
    int count = 0;
    char **lines = paste_format_lines("ls\ncd /tmp\nexit\n", &count);
    ASSERT_EQ(count, 3);
    ASSERT_NOT_NULL(lines);
    ASSERT_STR_EQ(lines[0], "ls");
    ASSERT_STR_EQ(lines[1], "cd /tmp");
    ASSERT_STR_EQ(lines[2], "exit");
    paste_line_free(lines, count);
    TEST_END();
}

int test_paste_format_lines_crlf(void)
{
    TEST_BEGIN();
    int count = 0;
    char **lines = paste_format_lines("a\r\nb\r\n", &count);
    ASSERT_EQ(count, 2);
    ASSERT_NOT_NULL(lines);
    ASSERT_STR_EQ(lines[0], "a");
    ASSERT_STR_EQ(lines[1], "b");
    paste_line_free(lines, count);
    TEST_END();
}

int test_paste_format_lines_trailing_newline(void)
{
    TEST_BEGIN();
    int count = 0;
    char **lines = paste_format_lines("foo\n", &count);
    ASSERT_EQ(count, 1);
    ASSERT_NOT_NULL(lines);
    ASSERT_STR_EQ(lines[0], "foo");
    paste_line_free(lines, count);
    TEST_END();
}

int test_paste_format_lines_no_trailing_newline(void)
{
    TEST_BEGIN();
    int count = 0;
    char **lines = paste_format_lines("foo", &count);
    ASSERT_EQ(count, 1);
    ASSERT_NOT_NULL(lines);
    ASSERT_STR_EQ(lines[0], "foo");
    paste_line_free(lines, count);
    TEST_END();
}

int test_paste_format_lines_empty(void)
{
    TEST_BEGIN();
    int count = 99;
    char **lines = paste_format_lines("", &count);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(lines);
    TEST_END();
}

int test_paste_format_lines_null(void)
{
    TEST_BEGIN();
    int count = 99;
    char **lines = paste_format_lines(NULL, &count);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(lines);
    TEST_END();
}

int test_paste_format_lines_blank_lines(void)
{
    TEST_BEGIN();
    int count = 0;
    char **lines = paste_format_lines("a\n\nb\n", &count);
    ASSERT_EQ(count, 3);
    ASSERT_NOT_NULL(lines);
    ASSERT_STR_EQ(lines[0], "a");
    ASSERT_STR_EQ(lines[1], "");
    ASSERT_STR_EQ(lines[2], "b");
    paste_line_free(lines, count);
    TEST_END();
}

int test_paste_format_lines_long_line(void)
{
    TEST_BEGIN();
    /* Build a 500-char line */
    char big[501];
    memset(big, 'X', 500);
    big[500] = '\0';

    int count = 0;
    char **lines = paste_format_lines(big, &count);
    ASSERT_EQ(count, 1);
    ASSERT_NOT_NULL(lines);
    ASSERT_EQ((int)strlen(lines[0]), 500);
    paste_line_free(lines, count);
    TEST_END();
}

/* ---- paste_line_free ---------------------------------------------------- */

int test_paste_line_free_null(void)
{
    TEST_BEGIN();
    paste_line_free(NULL, 0);  /* must not crash */
    paste_line_free(NULL, 5);  /* must not crash */
    ASSERT_TRUE(1);
    TEST_END();
}

/* ---- paste_build_summary ------------------------------------------------ */

int test_paste_build_summary_single(void)
{
    TEST_BEGIN();
    char buf[128];
    paste_build_summary(1, 42, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Paste 1 line (42 chars)?");
    TEST_END();
}

int test_paste_build_summary_multi(void)
{
    TEST_BEGIN();
    char buf[128];
    paste_build_summary(5, 200, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Paste 5 lines (200 chars)?");
    TEST_END();
}

int test_paste_build_summary_null_buf(void)
{
    TEST_BEGIN();
    paste_build_summary(1, 10, NULL, 0);  /* must not crash */
    ASSERT_TRUE(1);
    TEST_END();
}

int test_paste_build_summary_small_buf(void)
{
    TEST_BEGIN();
    char buf[10];
    paste_build_summary(5, 200, buf, sizeof(buf));
    /* Should be truncated but not overflow */
    ASSERT_TRUE(strlen(buf) < 10);
    TEST_END();
}
