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

/* ---- utf8_encode -------------------------------------------------------- */

int test_utf8_encode_ascii(void)
{
    TEST_BEGIN();
    char buf[4];
    ASSERT_EQ(utf8_encode(0x41, buf), 1);        /* 'A' */
    ASSERT_EQ(buf[0], 'A');
    ASSERT_EQ(utf8_encode(0x00, buf), 1);         /* NUL */
    ASSERT_EQ(buf[0], '\0');
    ASSERT_EQ(utf8_encode(0x7F, buf), 1);         /* DEL — last 1-byte */
    ASSERT_EQ((unsigned char)buf[0], 0x7F);
    TEST_END();
}

int test_utf8_encode_2byte(void)
{
    TEST_BEGIN();
    char buf[4];
    /* U+00A9 = copyright sign: C2 A9 */
    ASSERT_EQ(utf8_encode(0xA9, buf), 2);
    ASSERT_EQ((unsigned char)buf[0], 0xC2);
    ASSERT_EQ((unsigned char)buf[1], 0xA9);
    /* U+07FF — last 2-byte codepoint */
    ASSERT_EQ(utf8_encode(0x7FF, buf), 2);
    ASSERT_EQ((unsigned char)buf[0], 0xDF);
    ASSERT_EQ((unsigned char)buf[1], 0xBF);
    /* U+0080 — first 2-byte codepoint */
    ASSERT_EQ(utf8_encode(0x80, buf), 2);
    ASSERT_EQ((unsigned char)buf[0], 0xC2);
    ASSERT_EQ((unsigned char)buf[1], 0x80);
    TEST_END();
}

int test_utf8_encode_3byte(void)
{
    TEST_BEGIN();
    char buf[4];
    /* U+4E16 = 世 : E4 B8 96 */
    ASSERT_EQ(utf8_encode(0x4E16, buf), 3);
    ASSERT_EQ((unsigned char)buf[0], 0xE4);
    ASSERT_EQ((unsigned char)buf[1], 0xB8);
    ASSERT_EQ((unsigned char)buf[2], 0x96);
    /* U+0800 — first 3-byte codepoint */
    ASSERT_EQ(utf8_encode(0x800, buf), 3);
    ASSERT_EQ((unsigned char)buf[0], 0xE0);
    ASSERT_EQ((unsigned char)buf[1], 0xA0);
    ASSERT_EQ((unsigned char)buf[2], 0x80);
    /* U+FFFF — last 3-byte codepoint */
    ASSERT_EQ(utf8_encode(0xFFFF, buf), 3);
    ASSERT_EQ((unsigned char)buf[0], 0xEF);
    ASSERT_EQ((unsigned char)buf[1], 0xBF);
    ASSERT_EQ((unsigned char)buf[2], 0xBF);
    TEST_END();
}

int test_utf8_encode_4byte(void)
{
    TEST_BEGIN();
    char buf[4];
    /* U+1F600 = 😀 : F0 9F 98 80 */
    ASSERT_EQ(utf8_encode(0x1F600, buf), 4);
    ASSERT_EQ((unsigned char)buf[0], 0xF0);
    ASSERT_EQ((unsigned char)buf[1], 0x9F);
    ASSERT_EQ((unsigned char)buf[2], 0x98);
    ASSERT_EQ((unsigned char)buf[3], 0x80);
    /* U+10000 — first 4-byte codepoint */
    ASSERT_EQ(utf8_encode(0x10000, buf), 4);
    ASSERT_EQ((unsigned char)buf[0], 0xF0);
    ASSERT_EQ((unsigned char)buf[1], 0x90);
    ASSERT_EQ((unsigned char)buf[2], 0x80);
    ASSERT_EQ((unsigned char)buf[3], 0x80);
    /* U+10FFFF — last valid codepoint */
    ASSERT_EQ(utf8_encode(0x10FFFF, buf), 4);
    ASSERT_EQ((unsigned char)buf[0], 0xF4);
    ASSERT_EQ((unsigned char)buf[1], 0x8F);
    ASSERT_EQ((unsigned char)buf[2], 0xBF);
    ASSERT_EQ((unsigned char)buf[3], 0xBF);
    TEST_END();
}

int test_utf8_encode_out_of_range(void)
{
    TEST_BEGIN();
    char buf[4];
    /* U+110000 — first invalid codepoint */
    ASSERT_EQ(utf8_encode(0x110000, buf), 0);
    /* Large value */
    ASSERT_EQ(utf8_encode(0xFFFFFFFF, buf), 0);
    TEST_END();
}

/* ── UTF-8 model label formatting tests ──────────────────────────── */

int test_model_label_middle_dot_utf8(void)
{
    TEST_BEGIN();
    /* The model label uses "\xC2\xB7" which is U+00B7 MIDDLE DOT in UTF-8.
     * Verify the snprintf format produces correct UTF-8 bytes. */
    char label[80];
    snprintf(label, sizeof(label), " \xC2\xB7 %s", "deepseek-chat");
    /* Byte 1 (space), Byte 2-3 (middle dot), Byte 4 (space) */
    ASSERT_EQ((unsigned char)label[0], ' ');
    ASSERT_EQ((unsigned char)label[1], 0xC2);
    ASSERT_EQ((unsigned char)label[2], 0xB7);
    ASSERT_EQ((unsigned char)label[3], ' ');
    /* Model name follows */
    ASSERT_STR_EQ(label + 4, "deepseek-chat");
    TEST_END();
}

int test_model_label_middle_dot_not_split(void)
{
    TEST_BEGIN();
    /* Ensure the middle dot is exactly 2 bytes (not 1 byte 0xB7 which would
     * indicate Latin-1 encoding, nor the "Â·" artifact from double-encoding). */
    char label[80];
    snprintf(label, sizeof(label), " \xC2\xB7 %s", "gpt-4");
    /* Count bytes between the spaces: must be exactly 2 (0xC2, 0xB7) */
    ASSERT_EQ((unsigned char)label[1], 0xC2);
    ASSERT_EQ((unsigned char)label[2], 0xB7);
    /* Next byte must be space, not another high byte */
    ASSERT_EQ((unsigned char)label[3], ' ');
    TEST_END();
}

int test_model_label_utf8_encode_middle_dot(void)
{
    TEST_BEGIN();
    /* Verify utf8_encode produces the same bytes as the literal */
    char buf[4];
    int len = utf8_encode(0xB7, buf);  /* U+00B7 MIDDLE DOT */
    ASSERT_EQ(len, 2);
    ASSERT_EQ((unsigned char)buf[0], 0xC2);
    ASSERT_EQ((unsigned char)buf[1], 0xB7);
    TEST_END();
}

int test_model_label_truncation(void)
{
    TEST_BEGIN();
    /* A very long model name should be truncated but still have valid UTF-8 prefix */
    char label[20];  /* small buffer */
    snprintf(label, sizeof(label), " \xC2\xB7 %s",
             "very-long-model-name-that-exceeds-buffer");
    /* Middle dot must still be intact */
    ASSERT_EQ((unsigned char)label[1], 0xC2);
    ASSERT_EQ((unsigned char)label[2], 0xB7);
    /* Must be null-terminated */
    ASSERT_EQ(label[19], '\0');
    TEST_END();
}
