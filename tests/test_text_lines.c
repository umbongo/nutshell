#include "test_framework.h"
#include "text_lines.h"
#include <string.h>

/* text_lf_to_crlf: a multiline EDIT only breaks lines on CRLF, so text that
 * arrives with bare LF (the transcript copy) is normalised before it is
 * pasted into the AI message box. */

int test_text_lf_empty(void)
{
    TEST_BEGIN();
    char out[8] = "xxxxxxx";
    ASSERT_EQ(text_lf_to_crlf("", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "");
    TEST_END();
}

int test_text_lf_no_newlines(void)
{
    TEST_BEGIN();
    char out[16];
    ASSERT_EQ(text_lf_to_crlf("hello", out, sizeof(out)), 5);
    ASSERT_STR_EQ(out, "hello");
    TEST_END();
}

int test_text_lf_bare_lf(void)
{
    TEST_BEGIN();
    char out[32];
    ASSERT_EQ(text_lf_to_crlf("a\nb\n\nc", out, sizeof(out)), 9);
    ASSERT_STR_EQ(out, "a\r\nb\r\n\r\nc");
    ASSERT_EQ(text_lf_to_crlf("\n", out, sizeof(out)), 2);
    ASSERT_STR_EQ(out, "\r\n");
    TEST_END();
}

int test_text_lf_existing_crlf_unchanged(void)
{
    TEST_BEGIN();
    char out[32];
    ASSERT_EQ(text_lf_to_crlf("a\r\nb\r\n", out, sizeof(out)), 6);
    ASSERT_STR_EQ(out, "a\r\nb\r\n");
    TEST_END();
}

int test_text_lf_mixed(void)
{
    TEST_BEGIN();
    char out[32];
    ASSERT_EQ(text_lf_to_crlf("a\r\nb\nc\r\n\nd", out, sizeof(out)), 12);
    ASSERT_STR_EQ(out, "a\r\nb\r\nc\r\n\r\nd");
    TEST_END();
}

int test_text_lf_lone_cr_unchanged(void)
{
    TEST_BEGIN();
    char out[32];
    ASSERT_EQ(text_lf_to_crlf("a\rb\r", out, sizeof(out)), 4);
    ASSERT_STR_EQ(out, "a\rb\r");
    /* CR then LF split by nothing is CRLF; CR CR LF -> CR CRLF */
    ASSERT_EQ(text_lf_to_crlf("a\r\r\nb", out, sizeof(out)), 5);
    ASSERT_STR_EQ(out, "a\r\r\nb");
    TEST_END();
}

int test_text_lf_size_query(void)
{
    TEST_BEGIN();
    ASSERT_EQ(text_lf_to_crlf("a\nb\nc", NULL, 0), 7);
    ASSERT_EQ(text_lf_to_crlf("a\r\nb", NULL, 0), 4);
    ASSERT_EQ(text_lf_to_crlf("", NULL, 0), 0);
    /* out_sz == 0 with a buffer must not be written */
    char out[4] = "zzz";
    ASSERT_EQ(text_lf_to_crlf("a\nb", out, 0), 4);
    ASSERT_STR_EQ(out, "zzz");
    TEST_END();
}

int test_text_lf_truncation_never_splits_crlf(void)
{
    TEST_BEGIN();
    char out[8];
    /* full result "ab\r\ncd" (6); room for 3 chars + NUL: the pair does not
     * fit, so "ab" only */
    memset(out, 'x', sizeof(out));
    ASSERT_EQ(text_lf_to_crlf("ab\ncd", out, 4), 6);
    ASSERT_STR_EQ(out, "ab");
    /* room for exactly the pair */
    memset(out, 'x', sizeof(out));
    ASSERT_EQ(text_lf_to_crlf("ab\ncd", out, 5), 6);
    ASSERT_STR_EQ(out, "ab\r\n");
    /* existing CRLF is a pair too */
    memset(out, 'x', sizeof(out));
    ASSERT_EQ(text_lf_to_crlf("ab\r\ncd", out, 4), 6);
    ASSERT_STR_EQ(out, "ab");
    /* out_sz == 1 only holds the NUL */
    memset(out, 'x', sizeof(out));
    ASSERT_EQ(text_lf_to_crlf("abc", out, 1), 3);
    ASSERT_STR_EQ(out, "");
    /* plain truncation */
    memset(out, 'x', sizeof(out));
    ASSERT_EQ(text_lf_to_crlf("abcdef", out, 4), 6);
    ASSERT_STR_EQ(out, "abc");
    TEST_END();
}

int test_text_lf_null_input(void)
{
    TEST_BEGIN();
    char out[4] = "zzz";
    ASSERT_EQ(text_lf_to_crlf(NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "");
    ASSERT_EQ(text_lf_to_crlf(NULL, NULL, 0), 0);
    TEST_END();
}
