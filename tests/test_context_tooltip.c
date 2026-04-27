/* tests/test_context_tooltip.c — Tests for ai_format_context_tooltip */
#include "test_framework.h"
#include "ai_prompt.h"
#include <string.h>

int test_context_tooltip_actual_basic(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        18432, 4128,        /* actual in/out */
        0,                  /* estimate (unused when actuals present) */
        200000,             /* limit */
        "deepseek-chat",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Context usage") != NULL);
    ASSERT_TRUE(strstr(buf, "Input tokens:")  != NULL);
    ASSERT_TRUE(strstr(buf, "18,432")         != NULL);
    ASSERT_TRUE(strstr(buf, "Output tokens:") != NULL);
    ASSERT_TRUE(strstr(buf, "4,128")          != NULL);
    ASSERT_TRUE(strstr(buf, "Total:")         != NULL);
    ASSERT_TRUE(strstr(buf, "22,560")         != NULL);
    ASSERT_TRUE(strstr(buf, "200,000")        != NULL);
    ASSERT_TRUE(strstr(buf, "(11%)")          != NULL);
    ASSERT_TRUE(strstr(buf, "Model:")         != NULL);
    ASSERT_TRUE(strstr(buf, "deepseek-chat")  != NULL);
    ASSERT_TRUE(strstr(buf, "(estimated)")    == NULL);
    TEST_END();
}

int test_context_tooltip_estimate_marks_estimated(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        0, 0,          /* no actuals */
        12345,         /* estimate */
        64000,
        "deepseek-chat",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Total (estimated)") != NULL);
    ASSERT_TRUE(strstr(buf, "~12,345") != NULL);
    ASSERT_TRUE(strstr(buf, "64,000")  != NULL);
    ASSERT_TRUE(strstr(buf, "(19%)")   != NULL);
    /* Estimate path must NOT show the input/output split */
    ASSERT_TRUE(strstr(buf, "Input tokens:")  == NULL);
    ASSERT_TRUE(strstr(buf, "Output tokens:") == NULL);
    TEST_END();
}

int test_context_tooltip_unknown_limit_actual(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        18432, 4128,
        0,
        0,             /* unknown limit */
        "custom-model",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Limit: unknown") != NULL);
    /* No percentage when limit is unknown. */
    ASSERT_TRUE(strstr(buf, "%") == NULL);
    /* Total still appears, just without "/ <limit>". */
    ASSERT_TRUE(strstr(buf, "Total:") != NULL);
    ASSERT_TRUE(strstr(buf, "22,560") != NULL);
    TEST_END();
}

int test_context_tooltip_unknown_limit_estimate(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        0, 0, 5000,
        0,
        "custom-model",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Total (estimated)") != NULL);
    ASSERT_TRUE(strstr(buf, "~5,000")        != NULL);
    ASSERT_TRUE(strstr(buf, "Limit: unknown") != NULL);
    ASSERT_TRUE(strstr(buf, "%") == NULL);
    TEST_END();
}

int test_context_tooltip_null_model_omits_line(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        100, 50, 0, 64000, NULL, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Model:") == NULL);
    ASSERT_TRUE(strstr(buf, "Total:") != NULL);
    TEST_END();
}

int test_context_tooltip_empty_model_omits_line(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        100, 50, 0, 64000, "", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Model:") == NULL);
    TEST_END();
}

int test_context_tooltip_zero_actuals_zero_estimate(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        0, 0, 0, 64000, "deepseek-chat", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Total (estimated)") != NULL);
    ASSERT_TRUE(strstr(buf, "~0")    != NULL);
    ASSERT_TRUE(strstr(buf, "(0%)")  != NULL);
    TEST_END();
}

int test_context_tooltip_large_numbers(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        1234567, 7654321, 0,
        9999999,
        "big-model",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "1,234,567") != NULL);
    ASSERT_TRUE(strstr(buf, "7,654,321") != NULL);
    ASSERT_TRUE(strstr(buf, "8,888,888") != NULL);
    ASSERT_TRUE(strstr(buf, "9,999,999") != NULL);
    TEST_END();
}

int test_context_tooltip_small_buf_safe(void) {
    TEST_BEGIN();
    char buf[16];
    int n = ai_format_context_tooltip(
        18432, 4128, 0, 200000, "deepseek-chat",
        buf, sizeof(buf));
    /* Must not crash, must NUL-terminate, must report bytes
     * snprintf would have written (>=0). */
    ASSERT_TRUE(n >= 0);
    ASSERT_EQ(buf[sizeof(buf) - 1], '\0');
    TEST_END();
}

int test_context_tooltip_null_buf_returns_zero(void) {
    TEST_BEGIN();
    int n = ai_format_context_tooltip(
        100, 50, 0, 64000, "m", NULL, 100);
    ASSERT_EQ(n, 0);
    TEST_END();
}

int test_context_tooltip_zero_size_returns_zero(void) {
    TEST_BEGIN();
    char buf[16];
    int n = ai_format_context_tooltip(
        100, 50, 0, 64000, "m", buf, 0);
    ASSERT_EQ(n, 0);
    TEST_END();
}
