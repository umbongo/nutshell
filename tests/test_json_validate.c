#include "test_framework.h"
#include "json_validate.h"
#include <string.h>

/* ---- json_escape_string tests ---- */

int test_escape_simple_string(void) {
    TEST_BEGIN();
    char buf[256];
    size_t pos = json_escape_string("hello", 5, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"hello\"");
    TEST_END();
}

int test_escape_special_chars(void) {
    TEST_BEGIN();
    char buf[256];
    size_t pos = json_escape_string("a\"b\\c\nd\re\tf", 11, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"a\\\"b\\\\c\\nd\\re\\tf\"");
    TEST_END();
}

int test_escape_control_chars(void) {
    TEST_BEGIN();
    char buf[256];
    /* 0x01 should become \u0001 */
    const char input[] = "a\x01" "b";
    size_t pos = json_escape_string(input, 3, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"a\\u0001b\"");
    TEST_END();
}

int test_escape_no_quotes(void) {
    TEST_BEGIN();
    char buf[256];
    size_t pos = json_escape_string("hello", 5, buf, sizeof(buf), 0, 0);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "hello");
    TEST_END();
}

int test_escape_with_offset(void) {
    TEST_BEGIN();
    char buf[256];
    memcpy(buf, "{\"k\":", 5);
    size_t pos = json_escape_string("val", 3, buf, sizeof(buf), 5, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "{\"k\":\"val\"");
    TEST_END();
}

int test_escape_overflow(void) {
    TEST_BEGIN();
    char buf[4]; /* too small for "hello" with quotes */
    size_t pos = json_escape_string("hello", 5, buf, sizeof(buf), 0, 1);
    ASSERT_EQ((int)pos, 0);
    TEST_END();
}

int test_escape_empty_string(void) {
    TEST_BEGIN();
    char buf[256];
    size_t pos = json_escape_string("", 0, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"\"");
    TEST_END();
}

int test_escape_forward_slash(void) {
    TEST_BEGIN();
    char buf[256];
    /* Forward slash is valid unescaped in JSON — should pass through as-is */
    size_t pos = json_escape_string("a/b", 3, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"a/b\"");
    TEST_END();
}
