#include "test_framework.h"
#include "string_utils.h"
#include <stdlib.h>
#include <string.h>

int test_str_dup_basic(void)
{
    TEST_BEGIN();
    char *s = str_dup("hello");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hello");
    free(s);
    TEST_END();
}

int test_str_dup_empty(void)
{
    TEST_BEGIN();
    char *s = str_dup("");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "");
    free(s);
    TEST_END();
}

int test_str_dup_null(void)
{
    TEST_BEGIN();
    char *s = str_dup(NULL);
    ASSERT_NULL(s);
    TEST_END();
}

int test_str_cat_basic(void)
{
    TEST_BEGIN();
    char buf[16] = "hello";
    str_cat(buf, sizeof(buf), " world");
    ASSERT_STR_EQ(buf, "hello world");
    TEST_END();
}

int test_str_cat_truncates(void)
{
    TEST_BEGIN();
    char buf[8] = "abc";
    str_cat(buf, sizeof(buf), "defghijklmnop");  /* truncated to fit */
    ASSERT_EQ(buf[7], '\0');                      /* always null-terminated */
    ASSERT_EQ((int)strlen(buf), 7);
    TEST_END();
}

int test_str_cat_full_buffer(void)
{
    TEST_BEGIN();
    char buf[4] = "abc";  /* buf is already full (a,b,c,\0) */
    str_cat(buf, sizeof(buf), "XYZ");
    ASSERT_STR_EQ(buf, "abc");  /* unchanged */
    TEST_END();
}

int test_str_cat_null_src(void)
{
    TEST_BEGIN();
    char buf[8] = "hello";
    str_cat(buf, sizeof(buf), NULL);
    ASSERT_STR_EQ(buf, "hello");  /* unchanged */
    TEST_END();
}

int test_str_trim_both_ends(void)
{
    TEST_BEGIN();
    char s[] = "  hello  ";
    str_trim(s);
    ASSERT_STR_EQ(s, "hello");
    TEST_END();
}

int test_str_trim_leading(void)
{
    TEST_BEGIN();
    char s[] = "   world";
    str_trim(s);
    ASSERT_STR_EQ(s, "world");
    TEST_END();
}

int test_str_trim_trailing(void)
{
    TEST_BEGIN();
    char s[] = "world   ";
    str_trim(s);
    ASSERT_STR_EQ(s, "world");
    TEST_END();
}

int test_str_trim_no_whitespace(void)
{
    TEST_BEGIN();
    char s[] = "hello";
    str_trim(s);
    ASSERT_STR_EQ(s, "hello");
    TEST_END();
}

int test_str_trim_all_whitespace(void)
{
    TEST_BEGIN();
    char s[] = "   ";
    str_trim(s);
    ASSERT_STR_EQ(s, "");
    TEST_END();
}

int test_str_trim_empty(void)
{
    TEST_BEGIN();
    char s[] = "";
    str_trim(s);
    ASSERT_STR_EQ(s, "");
    TEST_END();
}

int test_str_starts_with_yes(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(str_starts_with("hello world", "hello"));
    ASSERT_TRUE(str_starts_with("hello", "hello"));
    ASSERT_TRUE(str_starts_with("hello", ""));
    TEST_END();
}

int test_str_starts_with_no(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(str_starts_with("hello world", "world"));
    ASSERT_FALSE(str_starts_with("", "x"));
    TEST_END();
}

int test_str_starts_with_null(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(str_starts_with(NULL, "x"));
    ASSERT_FALSE(str_starts_with("hello", NULL));
    TEST_END();
}

int test_str_ends_with_yes(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(str_ends_with("hello world", "world"));
    ASSERT_TRUE(str_ends_with("hello", "hello"));
    ASSERT_TRUE(str_ends_with("hello", ""));
    TEST_END();
}

int test_str_ends_with_no(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(str_ends_with("hello world", "hello"));
    ASSERT_FALSE(str_ends_with("", "x"));
    TEST_END();
}

int test_str_ends_with_null(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(str_ends_with(NULL, "x"));
    ASSERT_FALSE(str_ends_with("hello", NULL));
    TEST_END();
}

/* ---- ansi_strip --------------------------------------------------------- */

int test_ansi_strip_plain(void)
{
    TEST_BEGIN();
    char dst[64];
    size_t n = ansi_strip(dst, sizeof(dst), "hello world", 11u);
    ASSERT_STR_EQ(dst, "hello world");
    ASSERT_EQ((int)n, 11);
    TEST_END();
}

int test_ansi_strip_sgr_reset(void)
{
    TEST_BEGIN();
    char dst[64];
    /* ESC[0m should be stripped completely */
    const char *src = "abc\x1b[0mdef";
    size_t n = ansi_strip(dst, sizeof(dst), src, strlen(src));
    ASSERT_STR_EQ(dst, "abcdef");
    ASSERT_EQ((int)n, 6);
    TEST_END();
}

int test_ansi_strip_colour(void)
{
    TEST_BEGIN();
    char dst[64];
    /* ESC[31m = red fg */
    const char *src = "\x1b[31mRed\x1b[0m text";
    size_t n = ansi_strip(dst, sizeof(dst), src, strlen(src));
    ASSERT_STR_EQ(dst, "Red text");
    ASSERT_EQ((int)n, 8);
    TEST_END();
}

int test_ansi_strip_osc_title(void)
{
    TEST_BEGIN();
    char dst[64];
    /* OSC 0 ; title BEL */
    const char *src = "\x1b]0;My Title\x07" "after";
    size_t n = ansi_strip(dst, sizeof(dst), src, strlen(src));
    ASSERT_STR_EQ(dst, "after");
    ASSERT_EQ((int)n, 5);
    TEST_END();
}

int test_ansi_strip_cr_removed(void)
{
    TEST_BEGIN();
    char dst[64];
    const char *src = "line1\r\nline2";
    size_t n = ansi_strip(dst, sizeof(dst), src, strlen(src));
    ASSERT_STR_EQ(dst, "line1\nline2");
    ASSERT_EQ((int)n, 11);
    TEST_END();
}

int test_ansi_strip_empty(void)
{
    TEST_BEGIN();
    char dst[64];
    size_t n = ansi_strip(dst, sizeof(dst), "", 0u);
    ASSERT_STR_EQ(dst, "");
    ASSERT_EQ((int)n, 0);
    TEST_END();
}

int test_ansi_strip_null_dst(void)
{
    TEST_BEGIN();
    /* NULL dst — must not crash, must return 0 */
    size_t n = ansi_strip(NULL, 64u, "text", 4u);
    ASSERT_EQ((int)n, 0);
    TEST_END();
}
