#include "test_framework.h"
#include "connect_anim.h"
#include <string.h>

/* =========================================================================
 * connect_anim_dots — positive tests
 * ========================================================================= */

/* No time has elapsed: no dots yet. */
int test_anim_dots_zero_elapsed(void)
{
    TEST_BEGIN();
    ASSERT_EQ(connect_anim_dots(0, 500, 10), 0);
    TEST_END();
}

/* One millisecond short of the first interval: still zero dots. */
int test_anim_dots_just_under_interval(void)
{
    TEST_BEGIN();
    ASSERT_EQ(connect_anim_dots(499, 500, 10), 0);
    TEST_END();
}

/* Exactly one interval elapsed: exactly one dot. */
int test_anim_dots_exactly_one_interval(void)
{
    TEST_BEGIN();
    ASSERT_EQ(connect_anim_dots(500, 500, 10), 1);
    TEST_END();
}

/* Multiple complete intervals produce the matching dot count. */
int test_anim_dots_multiple_intervals(void)
{
    TEST_BEGIN();
    ASSERT_EQ(connect_anim_dots(1000, 500, 20), 2);
    ASSERT_EQ(connect_anim_dots(1500, 500, 20), 3);
    ASSERT_EQ(connect_anim_dots(5000, 500, 20), 10);
    TEST_END();
}

/* Clamped to max_dots regardless of elapsed time. */
int test_anim_dots_clamped_to_max(void)
{
    TEST_BEGIN();
    ASSERT_EQ(connect_anim_dots(10000,  500, 6), 6);
    ASSERT_EQ(connect_anim_dots(999999, 500, 3), 3);
    TEST_END();
}

/* Large interval: no dot until a full interval has elapsed. */
int test_anim_dots_large_interval(void)
{
    TEST_BEGIN();
    ASSERT_EQ(connect_anim_dots(999,  1000, 10), 0);
    ASSERT_EQ(connect_anim_dots(1000, 1000, 10), 1);
    TEST_END();
}

/* =========================================================================
 * connect_anim_dots — negative / edge-case tests
 * ========================================================================= */

/* Zero interval: avoid divide-by-zero, return max_dots immediately. */
int test_anim_dots_zero_interval(void)
{
    TEST_BEGIN();
    ASSERT_EQ(connect_anim_dots(1000, 0, 5), 5);
    ASSERT_EQ(connect_anim_dots(0,    0, 0), 0);
    TEST_END();
}

/* max_dots = 0 always returns 0 regardless of elapsed time. */
int test_anim_dots_max_zero(void)
{
    TEST_BEGIN();
    ASSERT_EQ(connect_anim_dots(5000, 500, 0), 0);
    TEST_END();
}

/* =========================================================================
 * connect_anim_text — positive tests
 * ========================================================================= */

/* Zero dots: output is exactly "Connecting". */
int test_anim_text_zero_dots(void)
{
    TEST_BEGIN();
    char buf[64];
    int n = connect_anim_text(0, buf, sizeof(buf));
    ASSERT_EQ(n, 10);
    ASSERT_TRUE(strcmp(buf, "Connecting") == 0);
    TEST_END();
}

/* One dot appended. */
int test_anim_text_one_dot(void)
{
    TEST_BEGIN();
    char buf[64];
    int n = connect_anim_text(1, buf, sizeof(buf));
    ASSERT_EQ(n, 11);
    ASSERT_TRUE(strcmp(buf, "Connecting.") == 0);
    TEST_END();
}

/* Three dots appended. */
int test_anim_text_three_dots(void)
{
    TEST_BEGIN();
    char buf[64];
    int n = connect_anim_text(3, buf, sizeof(buf));
    ASSERT_EQ(n, 13);
    ASSERT_TRUE(strcmp(buf, "Connecting...") == 0);
    TEST_END();
}

/* Buffer exactly fits "Connecting" with no room for dots. */
int test_anim_text_buf_just_fits_prefix(void)
{
    TEST_BEGIN();
    char buf[11]; /* "Connecting" + '\0' */
    int n = connect_anim_text(0, buf, sizeof(buf));
    ASSERT_EQ(n, 10);
    ASSERT_TRUE(strcmp(buf, "Connecting") == 0);
    TEST_END();
}

/* Buffer fits prefix plus one dot only; remaining dots are truncated. */
int test_anim_text_dots_truncated_by_buf(void)
{
    TEST_BEGIN();
    char buf[12]; /* "Connecting." + '\0' */
    int n = connect_anim_text(5, buf, sizeof(buf));
    ASSERT_EQ(n, 11);
    ASSERT_TRUE(strcmp(buf, "Connecting.") == 0);
    TEST_END();
}

/* =========================================================================
 * connect_anim_text — negative / edge-case tests
 * ========================================================================= */

/* NULL buffer: return 0 without crashing. */
int test_anim_text_null_buf(void)
{
    TEST_BEGIN();
    int n = connect_anim_text(3, NULL, 64);
    ASSERT_EQ(n, 0);
    TEST_END();
}

/* Zero-size buffer: return 0 without writing. */
int test_anim_text_zero_size(void)
{
    TEST_BEGIN();
    char buf[8];
    buf[0] = 'X';
    int n = connect_anim_text(3, buf, 0);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(buf[0], 'X'); /* untouched */
    TEST_END();
}

/* Buffer smaller than prefix: prefix is truncated, always null-terminated. */
int test_anim_text_small_buf_truncates_prefix(void)
{
    TEST_BEGIN();
    char buf[5]; /* fits "Conn\0" only */
    int n = connect_anim_text(0, buf, sizeof(buf));
    ASSERT_EQ(n, 4);
    ASSERT_TRUE(strncmp(buf, "Conn", 4) == 0);
    ASSERT_EQ(buf[4], '\0');
    TEST_END();
}

/* Single-byte buffer: only the null terminator fits. */
int test_anim_text_single_byte_buf(void)
{
    TEST_BEGIN();
    char buf[1];
    int n = connect_anim_text(0, buf, sizeof(buf));
    ASSERT_EQ(n, 0);
    ASSERT_EQ(buf[0], '\0');
    TEST_END();
}
