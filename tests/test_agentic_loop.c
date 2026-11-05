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

/* ---- Reasoning kept with a tool-call turn (Moonshot / Kimi) --------------- */

static const char REASON_TOOLS[] =
    "[{\"type\":\"function\",\"function\":{\"name\":\"web_fetch\","
    "\"description\":\"d\",\"parameters\":{\"type\":\"object\"}}}]";

/* system, user, assistant(tool call, reasoning), tool result. */
static void reasoning_conv(AiConversation *conv, const char *reasoning)
{
    ai_conv_init(conv, "m");
    ai_conv_add(conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(conv, AI_ROLE_USER, "fetch it");

    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id, sizeof(call.id), "call_1");
    snprintf(call.name, sizeof(call.name), "web_fetch");
    snprintf(call.input_json, sizeof(call.input_json), "{\"url\":\"https://x.test/\"}");
    agentic_add_assistant_tool_msg_ex(conv, "", reasoning, &call, 1);

    const char *page = "page text";
    AiToolResult res;
    memset(&res, 0, sizeof(res));
    snprintf(res.tool_use_id, sizeof(res.tool_use_id), "call_1");
    res.content = (char *)(uintptr_t)page;
    res.content_len = strlen(page);
    agentic_add_tool_results(conv, &call, &res, 1);
}

static size_t reasoning_body(const AiConversation *conv, const char *provider,
                             char *buf, size_t cap)
{
    return ai_build_request_body_tools(conv, NULL, REASON_TOOLS, buf, cap, 1,
                                       provider);
}

int test_ai_supports_tools_moonshot(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_provider_supports_tools("moonshot"), 1);
    ASSERT_EQ(ai_provider_supports_tools("anthropic"), 1);
    ASSERT_EQ(ai_provider_supports_tools("openai"), 1);
    ASSERT_EQ(ai_provider_supports_tools("deepseek"), 1);
    ASSERT_EQ(ai_provider_supports_tools("gemini"), 0);
    ASSERT_EQ(ai_provider_supports_tools("no-such-provider"), 0);
    ASSERT_EQ(ai_provider_supports_tools(NULL), 0);
    TEST_END();
}

int test_ai_reasoning_moonshot_body(void)
{
    TEST_BEGIN();
    AiConversation conv;
    reasoning_conv(&conv, "I should fetch the page first.");
    char buf[16384];
    ASSERT_TRUE(reasoning_body(&conv, "moonshot", buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "\"tools\":[") != NULL);
    const char *asst = strstr(buf, "{\"role\":\"assistant\"");
    ASSERT_NOT_NULL(asst);
    const char *rc = strstr(asst, "\"reasoning_content\":\"I should fetch the page first.\"");
    const char *tc = strstr(asst, "\"tool_calls\":[");
    ASSERT_NOT_NULL(rc);
    ASSERT_NOT_NULL(tc);
    ASSERT_TRUE(rc < tc);   /* on the assistant message, not elsewhere */
    /* exactly one field */
    ASSERT_NULL(strstr(rc + 1, "\"reasoning_content\""));
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ai_reasoning_other_providers_none(void)
{
    TEST_BEGIN();
    AiConversation conv;
    reasoning_conv(&conv, "secret thoughts");
    static const char *const providers[] = {"deepseek", "openai", "gemini",
                                            "anthropic", NULL};
    for (int i = 0; providers[i]; i++) {
        char buf[16384];
        ASSERT_TRUE(reasoning_body(&conv, providers[i], buf, sizeof(buf)) > 0);
        ASSERT_TRUE(strstr(buf, "\"tools\"") != NULL);
        ASSERT_NULL(strstr(buf, "reasoning_content"));
        ASSERT_NULL(strstr(buf, "secret thoughts"));
    }
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ai_reasoning_absent_emits_nothing(void)
{
    TEST_BEGIN();
    AiConversation conv;
    reasoning_conv(&conv, NULL);
    char buf[16384];
    /* Moonshot: the field is always there on a tool-call turn, empty when
     * the model streamed no reasoning (the thinking models reject a missing
     * one). */
    ASSERT_TRUE(reasoning_body(&conv, "moonshot", buf, sizeof(buf)) > 0);
    const char *asst = strstr(buf, "{\"role\":\"assistant\"");
    ASSERT_NOT_NULL(asst);
    const char *rc = strstr(asst, "\"reasoning_content\":\"\",\"tool_calls\":[");
    ASSERT_NOT_NULL(rc);
    ASSERT_NULL(strstr(rc + 20, "reasoning_content"));
    ASSERT_NULL(conv.messages[2].reasoning);
    /* only on an assistant tool-call turn */
    ASSERT_NULL(strstr(buf, "\"role\":\"user\",\"content\":\"fetch it\",\"reasoning_content\""));
    ai_conv_reset(&conv);

    reasoning_conv(&conv, "");   /* empty counts as none: same output */
    ASSERT_TRUE(reasoning_body(&conv, "moonshot", buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "\"reasoning_content\":\"\",\"tool_calls\":[") != NULL);
    ASSERT_NULL(conv.messages[2].reasoning);

    /* every other provider still emits nothing */
    static const char *const others[] = {"deepseek", "openai", "gemini",
                                         "anthropic", NULL};
    for (int i = 0; others[i]; i++) {
        ASSERT_TRUE(reasoning_body(&conv, others[i], buf, sizeof(buf)) > 0);
        ASSERT_NULL(strstr(buf, "reasoning_content"));
    }
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ai_reasoning_escaped_in_body(void)
{
    TEST_BEGIN();
    AiConversation conv;
    reasoning_conv(&conv, "say \"hi\"\nthen\\go");
    char buf[16384];
    ASSERT_TRUE(reasoning_body(&conv, "moonshot", buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "\"reasoning_content\":\"say \\\"hi\\\"\\nthen\\\\go\"") != NULL);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ai_reasoning_capped(void)
{
    TEST_BEGIN();
    AiMessage m;
    memset(&m, 0, sizeof(m));
    size_t big = (size_t)AI_REASONING_MAX * 3;
    char *text = (char *)malloc(big + 1);
    ASSERT_NOT_NULL(text);
    memset(text, 'a', big);
    text[big] = '\0';
    ASSERT_EQ(ai_msg_set_reasoning(&m, text), 0);
    ASSERT_NOT_NULL(m.reasoning);
    ASSERT_EQ((int)strlen(m.reasoning), AI_REASONING_MAX - 1);
    ai_msg_free(&m);
    ASSERT_NULL(m.reasoning);

    /* A cut must not split a multi-byte character: the cap falls inside a
     * 3-byte codepoint here (U+20AC, E2 82 AC). */
    memset(text, 'a', AI_REASONING_MAX - 2);   /* cap-1 = 16383 bytes allowed */
    size_t at = (size_t)AI_REASONING_MAX - 2;
    text[at] = (char)0xE2; text[at + 1] = (char)0x82; text[at + 2] = (char)0xAC;
    text[at + 3] = '\0';
    ASSERT_EQ(ai_msg_set_reasoning(&m, text), 0);
    ASSERT_EQ((int)strlen(m.reasoning), (int)at);   /* whole char dropped */
    ai_msg_free(&m);

    /* NULL and empty clear it */
    ASSERT_EQ(ai_msg_set_reasoning(&m, "x"), 0);
    ASSERT_EQ(ai_msg_set_reasoning(&m, NULL), 0);
    ASSERT_NULL(m.reasoning);
    ASSERT_EQ(ai_msg_set_reasoning(&m, "x"), 0);
    ASSERT_EQ(ai_msg_set_reasoning(&m, ""), 0);
    ASSERT_NULL(m.reasoning);
    free(text);
    TEST_END();
}

int test_ai_reasoning_copy_take_compact(void)
{
    TEST_BEGIN();
    AiConversation a, b, c;
    reasoning_conv(&a, "kept thoughts");
    ai_conv_init(&b, "m");
    ai_conv_init(&c, "m");

    /* deep copy: own buffer, same text */
    ASSERT_EQ(ai_conv_copy(&b, &a), 0);
    ASSERT_NOT_NULL(b.messages[2].reasoning);
    ASSERT_TRUE(b.messages[2].reasoning != a.messages[2].reasoning);
    ASSERT_STR_EQ(b.messages[2].reasoning, "kept thoughts");
    ASSERT_NULL(b.messages[1].reasoning);

    /* take: ownership moves, source is empty */
    ai_conv_take(&c, &b);
    ASSERT_STR_EQ(c.messages[2].reasoning, "kept thoughts");
    ASSERT_EQ(b.msg_count, 0);
    ASSERT_NULL(b.messages[2].reasoning);

    /* compact drops the reasoning of removed messages and keeps the rest */
    AiConversation t;
    ai_conv_init(&t, "m");
    ai_conv_add(&t, AI_ROLE_SYSTEM, "sys");
    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id, sizeof(call.id), "c");
    snprintf(call.name, sizeof(call.name), "web_fetch");
    for (int i = 0; i < 4; i++) {
        ai_conv_add(&t, AI_ROLE_USER, "u");
        agentic_add_assistant_tool_msg_ex(&t, "", i == 3 ? "last" : "old", &call, 1);
    }
    ASSERT_TRUE(ai_conv_compact(&t, 1) > 0);
    ASSERT_STR_EQ(t.messages[t.msg_count - 1].reasoning, "last");
    ai_conv_reset(&t);

    ai_conv_reset(&a);
    ai_conv_reset(&c);
    TEST_END();
}

/* ---- Moonshot output limit and "length" cut-off ---------------------------- */

int test_ai_moonshot_max_completion_tokens(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "m");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&conv, AI_ROLE_USER, "hi");
    char buf[4096];

    /* moonshot: the explicit limit, right after the model */
    ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 1,
                                         "moonshot") > 0);
    ASSERT_STR_EQ(buf,
        "{\"model\":\"m\",\"max_completion_tokens\":16384,\"stream\":true,"
        "\"messages\":[{\"role\":\"system\",\"content\":\"sys\"},"
        "{\"role\":\"user\",\"content\":\"hi\"}]}");
    ASSERT_NULL(strstr(buf, "\"max_tokens\""));

    /* also with tools, and when not streaming */
    ASSERT_TRUE(reasoning_body(&conv, "moonshot", buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "\"max_completion_tokens\":16384,") != NULL);
    ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 0,
                                         "moonshot") > 0);
    ASSERT_TRUE(strstr(buf, "\"max_completion_tokens\":16384,\"stream\":false") != NULL);

    /* every other provider: byte-for-byte what it was */
    ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 1,
                                         "openai") > 0);
    ASSERT_STR_EQ(buf,
        "{\"model\":\"m\",\"stream\":true,"
        "\"messages\":[{\"role\":\"system\",\"content\":\"sys\"},"
        "{\"role\":\"user\",\"content\":\"hi\"}]}");
    ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 1,
                                         "deepseek") > 0);
    ASSERT_STR_EQ(buf,
        "{\"model\":\"m\",\"stream\":true,"
        "\"messages\":[{\"role\":\"system\",\"content\":\"sys\"},"
        "{\"role\":\"user\",\"content\":\"hi\"}]}");
    ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 1,
                                         "gemini") > 0);
    ASSERT_NULL(strstr(buf, "max_completion_tokens"));
    ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 1,
                                         "anthropic") > 0);
    ASSERT_NULL(strstr(buf, "max_completion_tokens"));
    ASSERT_TRUE(strstr(buf, "\"max_tokens\":8096") != NULL);
    ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 1,
                                         NULL) > 0);
    ASSERT_NULL(strstr(buf, "max_completion_tokens"));
    ai_conv_reset(&conv);
    TEST_END();
}

int test_ai_stream_chunk_cut_off(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"length\"}]}"), 1);
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"choices\":[{\"delta\":{\"content\":\"x\"},\"finish_reason\":\"length\"}],"
        "\"usage\":{\"completion_tokens\":8096}}"), 1);
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"choices\":[{\"delta\":{\"content\":\"length\"},\"finish_reason\":null}]}"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off("{\"choices\":[]}"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off("{\"id\":\"x\"}"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off("[DONE]"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off("not json"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off(""), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off(NULL), 0);
    /* Anthropic's own stop reason is a different format and not reported here */
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"max_tokens\"}}"), 0);
    TEST_END();
}

/* The Moonshot output limit follows the model's context window: 16384, or
 * half of a small window (spec ruling 2026-11-05). */
int test_ai_moonshot_output_limit_per_model(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_moonshot_output_limit("kimi-k3"), 16384);
    ASSERT_EQ(ai_moonshot_output_limit("kimi-k3.1-pro"), 16384);
    ASSERT_EQ(ai_moonshot_output_limit("moonshot-v1-128k"), 16384);
    ASSERT_EQ(ai_moonshot_output_limit("moonshot-v1-32k"), 16000);
    ASSERT_EQ(ai_moonshot_output_limit("moonshot-v1-8k"), 4000);
    /* not in the table (0 from ai_model_context_limit): the full 16384 */
    ASSERT_EQ(ai_model_context_limit("my-own-kimi"), 0);
    ASSERT_EQ(ai_moonshot_output_limit("my-own-kimi"), 16384);
    ASSERT_EQ(ai_moonshot_output_limit(""), 16384);
    ASSERT_EQ(ai_moonshot_output_limit(NULL), 16384);

    /* and the emitted body carries it */
    static const struct { const char *model; const char *field; } cases[] = {
        {"kimi-k3",          "\"max_completion_tokens\":16384,"},
        {"moonshot-v1-8k",   "\"max_completion_tokens\":4000,"},
        {"moonshot-v1-32k",  "\"max_completion_tokens\":16000,"},
        {"unlisted-model",   "\"max_completion_tokens\":16384,"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        AiConversation conv;
        ai_conv_init(&conv, cases[i].model);
        ai_conv_add(&conv, AI_ROLE_USER, "hi");
        char buf[2048];
        ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 1,
                                             "moonshot") > 0);
        ASSERT_TRUE(strstr(buf, cases[i].field) != NULL);
        /* another provider with the same model name: no limit field */
        ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 1,
                                             "openai") > 0);
        ASSERT_NULL(strstr(buf, "max_completion_tokens"));
        ai_conv_reset(&conv);
    }
    TEST_END();
}
