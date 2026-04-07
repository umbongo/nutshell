/* tests/test_base64.c */
#include "test_framework.h"
#include "base64.h"
#include <string.h>

int test_base64_empty(void)
{
    TEST_BEGIN();
    char out[8];
    size_t n = base64_encode((const unsigned char *)"", 0, out, sizeof(out));
    ASSERT_EQ((int)n, 0);
    TEST_END();
}

int test_base64_hello(void)
{
    TEST_BEGIN();
    char out[16];
    size_t n = base64_encode((const unsigned char *)"Hello", 5, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "SGVsbG8=");
    TEST_END();
}

int test_base64_padding_1byte(void)
{
    TEST_BEGIN();
    char out[16];
    /* 1 byte -> 4 chars with == padding */
    size_t n = base64_encode((const unsigned char *)"A", 1, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "QQ==");
    TEST_END();
}

int test_base64_padding_2bytes(void)
{
    TEST_BEGIN();
    char out[16];
    /* 2 bytes -> 4 chars with = padding */
    size_t n = base64_encode((const unsigned char *)"AB", 2, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "QUI=");
    TEST_END();
}

int test_base64_padding_3bytes(void)
{
    TEST_BEGIN();
    char out[16];
    /* 3 bytes -> 4 chars, no padding */
    size_t n = base64_encode((const unsigned char *)"ABC", 3, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "QUJD");
    TEST_END();
}

int test_base64_small_buffer(void)
{
    TEST_BEGIN();
    char out[4]; /* needs 8 for "Hello" */
    size_t n = base64_encode((const unsigned char *)"Hello", 5, out, sizeof(out));
    ASSERT_EQ((int)n, 0);
    TEST_END();
}

int test_base64_binary(void)
{
    TEST_BEGIN();
    unsigned char bin[] = {0x00, 0xFF, 0x80, 0x01};
    char out[16];
    size_t n = base64_encode(bin, 4, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "AP+AAQ==");
    TEST_END();
}
