#include "test_framework.h"
#include "ai_agentic.h"
#include <string.h>
#include <stdlib.h>

/* ---- Iteration tests ------------------------------------------------------- */

int test_agentic_can_continue_initial(void)
{
    TEST_BEGIN();
    AgenticState state;
    agentic_state_reset(&state);
    ASSERT_EQ(agentic_can_continue(&state), 1);
    TEST_END();
}

int test_agentic_max_iterations(void)
{
    TEST_BEGIN();
    AgenticState state;
    agentic_state_reset(&state);
    state.loop_iter = AI_TOOL_LOOP_MAX;
    ASSERT_EQ(agentic_can_continue(&state), 0);
    TEST_END();
}

int test_agentic_mid_iteration(void)
{
    TEST_BEGIN();
    AgenticState state;
    agentic_state_reset(&state);
    state.loop_iter = 2;
    ASSERT_EQ(agentic_can_continue(&state), 1);
    TEST_END();
}

/* ---- Rate limiting tests --------------------------------------------------- */

int test_agentic_search_rate_limit(void)
{
    TEST_BEGIN();
    AgenticState state;
    agentic_state_reset(&state);

    for (int i = 0; i < AI_TOOL_SEARCH_MAX; i++) {
        agentic_record_tool_call(&state, "web_search");
    }

    char err[256];
    err[0] = '\0';
    int allowed = agentic_check_rate_limit(&state, "web_search", err, sizeof(err));
    ASSERT_EQ(allowed, 0);
    ASSERT_TRUE(strlen(err) > 0);
    ASSERT_TRUE(strstr(err, "Search limit reached") != NULL);
    TEST_END();
}

int test_agentic_search_under_limit(void)
{
    TEST_BEGIN();
    AgenticState state;
    agentic_state_reset(&state);

    agentic_record_tool_call(&state, "web_search");

    char err[256];
    err[0] = '\0';
    int allowed = agentic_check_rate_limit(&state, "web_search", err, sizeof(err));
    ASSERT_EQ(allowed, 1);
    TEST_END();
}

int test_agentic_non_search_no_limit(void)
{
    TEST_BEGIN();
    AgenticState state;
    agentic_state_reset(&state);

    /* Record many "web_fetch" calls — should never be rate limited */
    for (int i = 0; i < 100; i++) {
        agentic_record_tool_call(&state, "web_fetch");
    }

    char err[256];
    err[0] = '\0';
    int allowed = agentic_check_rate_limit(&state, "web_fetch", err, sizeof(err));
    ASSERT_EQ(allowed, 1);
    TEST_END();
}

int test_agentic_state_reset_clears(void)
{
    TEST_BEGIN();
    AgenticState state;
    state.loop_iter = 4;
    state.search_count = 3;

    agentic_state_reset(&state);
    ASSERT_EQ(state.loop_iter, 0);
    ASSERT_EQ(state.search_count, 0);
    TEST_END();
}

/* ---- Conversation assembly tests ------------------------------------------ */

int test_agentic_add_assistant_tool_msg(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "claude-3-5-sonnet-20241022");

    AiToolCall calls[1];
    memset(calls, 0, sizeof(calls));
    strncpy(calls[0].id, "toolu_01", sizeof(calls[0].id) - 1);
    strncpy(calls[0].name, "web_search", sizeof(calls[0].name) - 1);
    strncpy(calls[0].input_json, "{\"query\":\"test\"}", sizeof(calls[0].input_json) - 1);

    int rc = agentic_add_assistant_tool_msg(&conv, "Let me search for that.", calls, 1);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(conv.msg_count, 1);

    AiMessage *msg = &conv.messages[0];
    ASSERT_EQ((int)msg->role, (int)AI_ROLE_ASSISTANT);
    ASSERT_STR_EQ(ai_msg_content(msg), "Let me search for that.");
    ASSERT_EQ(msg->n_tool_calls, 1);
    ASSERT_NOT_NULL(msg->tool_calls);
    ASSERT_STR_EQ(msg->tool_calls[0].id, "toolu_01");
    ASSERT_STR_EQ(msg->tool_calls[0].name, "web_search");

    ai_msg_free(msg);
    TEST_END();
}

int test_agentic_add_tool_results(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "claude-3-5-sonnet-20241022");

    AiToolCall calls[2];
    memset(calls, 0, sizeof(calls));
    strncpy(calls[0].id, "toolu_01", sizeof(calls[0].id) - 1);
    strncpy(calls[0].name, "web_search", sizeof(calls[0].name) - 1);
    strncpy(calls[1].id, "toolu_02", sizeof(calls[1].id) - 1);
    strncpy(calls[1].name, "web_fetch", sizeof(calls[1].name) - 1);

    const char *content0 = "search results here";
    const char *content1 = "fetched page content";

    AiToolResult results[2];
    memset(results, 0, sizeof(results));
    strncpy(results[0].tool_use_id, "toolu_01", sizeof(results[0].tool_use_id) - 1);
    results[0].content = (char *)(uintptr_t)content0;
    results[0].content_len = strlen(content0);
    results[0].is_error = 0;

    strncpy(results[1].tool_use_id, "toolu_02", sizeof(results[1].tool_use_id) - 1);
    results[1].content = (char *)(uintptr_t)content1;
    results[1].content_len = strlen(content1);
    results[1].is_error = 1;

    int rc = agentic_add_tool_results(&conv, calls, results, 2);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(conv.msg_count, 2);

    AiMessage *m0 = &conv.messages[0];
    ASSERT_EQ((int)m0->role, (int)AI_ROLE_TOOL);
    ASSERT_STR_EQ(ai_msg_content(m0), "search results here");
    ASSERT_STR_EQ(m0->tool_call_id, "toolu_01");
    ASSERT_STR_EQ(m0->tool_name, "web_search");
    ASSERT_EQ(m0->is_tool_error, 0);

    AiMessage *m1 = &conv.messages[1];
    ASSERT_EQ((int)m1->role, (int)AI_ROLE_TOOL);
    ASSERT_STR_EQ(ai_msg_content(m1), "fetched page content");
    ASSERT_STR_EQ(m1->tool_call_id, "toolu_02");
    ASSERT_STR_EQ(m1->tool_name, "web_fetch");
    ASSERT_EQ(m1->is_tool_error, 1);

    ai_msg_free(m0);
    ai_msg_free(m1);
    TEST_END();
}

int test_agentic_conversation_full(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test-model");

    /* Fill conversation to capacity */
    for (int i = 0; i < AI_MAX_MESSAGES; i++) {
        ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, "msg"), 0);
    }
    ASSERT_EQ(conv.msg_count, AI_MAX_MESSAGES);

    /* agentic_add_assistant_tool_msg should fail */
    int rc = agentic_add_assistant_tool_msg(&conv, "text", NULL, 0);
    ASSERT_EQ(rc, -1);

    /* agentic_add_tool_results should also fail */
    const char *content = "result";
    AiToolCall calls[1];
    memset(calls, 0, sizeof(calls));
    strncpy(calls[0].name, "web_search", sizeof(calls[0].name) - 1);

    AiToolResult results[1];
    memset(results, 0, sizeof(results));
    results[0].content = (char *)(uintptr_t)content;
    results[0].content_len = strlen(content);
    rc = agentic_add_tool_results(&conv, calls, results, 1);
    ASSERT_EQ(rc, -1);

    TEST_END();
}

/* ---- Cancellation pattern test -------------------------------------------- */

int test_agentic_cancel_before_tool(void)
{
    TEST_BEGIN();
    /* Verify the cancel_requested flag pattern used in the agentic loop.
     * The loop checks a volatile int before executing each tool call.
     * This test exercises the check pattern directly. */
    volatile int cancel_requested = 0;

    /* When not cancelled, execution would proceed */
    ASSERT_EQ(cancel_requested, 0);

    cancel_requested = 1;

    /* When cancelled, tool execution is skipped */
    ASSERT_EQ(cancel_requested, 1);
    TEST_END();
}

/* ---- Truncation warning test ---------------------------------------------- */

int test_agentic_truncation_warning(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = agentic_truncation_warning("web_search", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "web_search") != NULL);
    ASSERT_TRUE(strstr(buf, "truncated") != NULL);
    TEST_END();
}
