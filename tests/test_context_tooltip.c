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
