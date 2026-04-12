/* tests/test_token_usage.c — Tests for actual API token count extraction */
#include "test_framework.h"
#include "ai_prompt.h"
#include <string.h>

/* --- Anthropic message_start: contains input_tokens --- */

int test_token_anthropic_message_start_input_tokens(void) {
    TEST_BEGIN();
    /* Anthropic message_start carries input token count */
    const char *json = "{\"type\":\"message_start\","
                       "\"message\":{\"id\":\"msg_01\","
                       "\"usage\":{\"input_tokens\":1234,\"output_tokens\":0}}}";
    char content[256] = "";
    char thinking[256] = "";
    int input_tokens = 0, output_tokens = 0;

    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking),
                                   &input_tokens, &output_tokens);

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(input_tokens, 1234);
    ASSERT_EQ(output_tokens, 0);
    TEST_END();
}

/* --- Anthropic message_delta: contains output_tokens --- */

int test_token_anthropic_message_delta_output_tokens(void) {
    TEST_BEGIN();
    /* Anthropic message_delta carries output token count */
    const char *json = "{\"type\":\"message_delta\","
                       "\"usage\":{\"output_tokens\":42}}";
    char content[256] = "";
    char thinking[256] = "";
    int input_tokens = 0, output_tokens = 0;

    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking),
                                   &input_tokens, &output_tokens);

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(input_tokens, 0);
    ASSERT_EQ(output_tokens, 42);
    TEST_END();
}

/* --- OpenAI chunk with top-level usage field --- */

int test_token_openai_usage_field(void) {
    TEST_BEGIN();
    /* OpenAI last chunk may include top-level usage */
    const char *json = "{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],"
                       "\"usage\":{\"prompt_tokens\":500,\"completion_tokens\":75,"
                       "\"total_tokens\":575}}";
    char content[256] = "";
    char thinking[256] = "";
    int input_tokens = 0, output_tokens = 0;

    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking),
                                   &input_tokens, &output_tokens);

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(input_tokens, 500);
    ASSERT_EQ(output_tokens, 75);
    TEST_END();
}

/* --- Non-usage Anthropic events return 0 for both token counts --- */

int test_token_non_usage_event_returns_zero(void) {
    TEST_BEGIN();
    /* content_block_delta carries text, no token counts */
    const char *json = "{\"type\":\"content_block_delta\","
                       "\"delta\":{\"type\":\"text_delta\",\"text\":\"hello\"}}";
    char content[256] = "";
    char thinking[256] = "";
    int input_tokens = -1, output_tokens = -1;

    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking),
                                   &input_tokens, &output_tokens);

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(input_tokens, 0);
    ASSERT_EQ(output_tokens, 0);
    ASSERT_STR_EQ(content, "hello");
    TEST_END();
}

/* --- OpenAI chunk without usage returns 0 for token counts --- */

int test_token_openai_no_usage_returns_zero(void) {
    TEST_BEGIN();
    const char *json = "{\"choices\":[{\"delta\":{\"content\":\"world\"}}]}";
    char content[256] = "";
    char thinking[256] = "";
    int input_tokens = -1, output_tokens = -1;

    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking),
                                   &input_tokens, &output_tokens);

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(input_tokens, 0);
    ASSERT_EQ(output_tokens, 0);
    ASSERT_STR_EQ(content, "world");
    TEST_END();
}

/* --- NULL safety: NULL token output pointers are safe --- */

int test_token_null_output_pointers_safe(void) {
    TEST_BEGIN();
    const char *json = "{\"type\":\"message_start\","
                       "\"message\":{\"usage\":{\"input_tokens\":100}}}";

    /* Pass NULL for token out pointers — should not crash */
    int rc = ai_parse_stream_chunk(json, NULL, 0, NULL, 0, NULL, NULL);
    ASSERT_EQ(rc, 0);
    TEST_END();
}

/* --- NULL json input still returns error --- */

int test_token_null_json_returns_error(void) {
    TEST_BEGIN();
    int input_tokens = 99, output_tokens = 99;
    int rc = ai_parse_stream_chunk(NULL, NULL, 0, NULL, 0,
                                   &input_tokens, &output_tokens);
    ASSERT_EQ(rc, -1);
    TEST_END();
}

/* --- message_start with missing usage does not crash --- */

int test_token_message_start_no_usage_field(void) {
    TEST_BEGIN();
    const char *json = "{\"type\":\"message_start\",\"message\":{\"id\":\"x\"}}";
    char content[256] = "";
    int input_tokens = 0, output_tokens = 0;

    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   NULL, 0,
                                   &input_tokens, &output_tokens);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(input_tokens, 0);
    ASSERT_EQ(output_tokens, 0);
    TEST_END();
}
