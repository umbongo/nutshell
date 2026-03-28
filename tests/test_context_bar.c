/* tests/test_context_bar.c — Tests for context bar logic */
#include "test_framework.h"
#include "ai_prompt.h"
#include <string.h>

/* --- ai_context_estimate_tokens --- */

int test_context_tokens_empty_conv(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ASSERT_EQ(ai_context_estimate_tokens(&conv), 0);
    TEST_END();
}

int test_context_tokens_null(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_context_estimate_tokens(NULL), 0);
    TEST_END();
}

int test_context_tokens_single_message(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    /* 40 chars / 4 = 10 tokens */
    ai_conv_add(&conv, AI_ROLE_USER, "1234567890123456789012345678901234567890");
    ASSERT_EQ(ai_context_estimate_tokens(&conv), 10);
    TEST_END();
}

int test_context_tokens_multiple_messages(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "12345678");  /* 8 chars */
    ai_conv_add(&conv, AI_ROLE_USER, "12345678");     /* 8 chars */
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "12345678"); /* 8 chars */
    /* 24 chars / 4 = 6 tokens */
    ASSERT_EQ(ai_context_estimate_tokens(&conv), 6);
    TEST_END();
}

int test_context_tokens_rounding_down(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_USER, "123"); /* 3 chars / 4 = 0 */
    ASSERT_EQ(ai_context_estimate_tokens(&conv), 0);
    TEST_END();
}

/* --- ai_format_context_label --- */

int test_context_label_zero_tokens(void) {
    TEST_BEGIN();
    char buf[64];
    ai_format_context_label(0, 64000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Context: 0 / 64k (0%)");
    TEST_END();
}

int test_context_label_small_tokens(void) {
    TEST_BEGIN();
    char buf[64];
    ai_format_context_label(500, 64000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Context: 500 / 64k (0%)");
    TEST_END();
}

int test_context_label_kilo_tokens(void) {
    TEST_BEGIN();
    char buf[64];
    ai_format_context_label(2200, 64000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Context: 2.2k / 64k (3%)");
    TEST_END();
}

int test_context_label_large_limit(void) {
    TEST_BEGIN();
    char buf[64];
    ai_format_context_label(50000, 200000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Context: 50.0k / 200k (25%)");
    TEST_END();
}

int test_context_label_full(void) {
    TEST_BEGIN();
    char buf[64];
    ai_format_context_label(64000, 64000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Context: 64.0k / 64k (100%)");
    TEST_END();
}

int test_context_label_over_limit(void) {
    TEST_BEGIN();
    char buf[64];
    /* Over limit clamps to 100% */
    ai_format_context_label(70000, 64000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Context: 70.0k / 64k (100%)");
    TEST_END();
}

int test_context_label_small_limit(void) {
    TEST_BEGIN();
    char buf[64];
    ai_format_context_label(50, 500, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Context: 50 / 500 (10%)");
    TEST_END();
}

int test_context_label_exact_1k(void) {
    TEST_BEGIN();
    char buf[64];
    ai_format_context_label(1000, 128000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Context: 1.0k / 128k (0%)");
    TEST_END();
}

int test_context_label_na(void) {
    TEST_BEGIN();
    char buf[64];
    /* limit=0 should produce N/A */
    ai_format_context_label(100, 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Context: N/A");
    TEST_END();
}

int test_context_label_small_buf(void) {
    TEST_BEGIN();
    char buf[10];
    ai_format_context_label(2200, 64000, buf, sizeof(buf));
    /* Should truncate safely without overflow */
    ASSERT_EQ((int)strlen(buf) < 10, 1);
    TEST_END();
}
