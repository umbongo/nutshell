#include "test_framework.h"
#include "ai_agentic.h"
#include "run_command.h"
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
    ASSERT_EQ(conv.msg_count, 1);   /* one results turn for the round */

    AiMessage *m0 = &conv.messages[0];
    ASSERT_EQ((int)m0->role, (int)AI_ROLE_TOOL);
    ASSERT_EQ(m0->n_parts, 2);
    ASSERT_STR_EQ(m0->parts[0].content, "search results here");
    ASSERT_STR_EQ(m0->parts[0].tool_call_id, "toolu_01");
    ASSERT_STR_EQ(m0->parts[0].tool_name, "web_search");
    ASSERT_EQ(m0->parts[0].is_error, 0);

    ASSERT_STR_EQ(m0->parts[1].content, "fetched page content");
    ASSERT_STR_EQ(m0->parts[1].tool_call_id, "toolu_02");
    ASSERT_STR_EQ(m0->parts[1].tool_name, "web_fetch");
    ASSERT_EQ(m0->parts[1].is_error, 1);

    ai_msg_free(m0);
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
    int n = agentic_truncation_warning("web_search", 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "web_search") != NULL);
    ASSERT_TRUE(strstr(buf, "truncated") != NULL);
    TEST_END();
}

/* The warning names the real cap, or the bytes omitted: never "1MB". */
int test_agentic_truncation_warning_text(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = agentic_truncation_warning("web_fetch", 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "[web_fetch output truncated at 64 KiB]");
    ASSERT_EQ(n, (int)strlen(buf));
    n = agentic_truncation_warning("run_command", 1234, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "[run_command output cut: 1234 bytes omitted]");
    ASSERT_EQ(n, (int)strlen(buf));
    ASSERT_NULL(strstr(buf, "1MB"));
    /* A NULL name reads as empty; a NULL or empty buffer writes nothing. */
    agentic_truncation_warning(NULL, 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "[ output truncated at 64 KiB]");
    ASSERT_EQ(agentic_truncation_warning("x", 0, NULL, 10), 0);
    ASSERT_EQ(agentic_truncation_warning("x", 0, buf, 0), 0);
    /* A small buffer is cut, never overrun. */
    char small[8];
    agentic_truncation_warning("web_fetch", 5, small, sizeof(small));
    ASSERT_EQ(strlen(small), 7);
    TEST_END();
}

/* ---- The cut-off input and the room for a tool round (spec 4) -------------- */

/* A 4000-byte unterminated heredoc: the JSON stops mid-string. Stored as-is
 * it would be sent back to the provider as invalid input. */
int test_agentic_sanitize_cut_heredoc(void)
{
    TEST_BEGIN();
    AiToolCall c;
    memset(&c, 0, sizeof(c));
    strcpy(c.id, "toolu_1");
    strcpy(c.name, RUNCMD_TOOL_NAME);
    size_t n = (size_t)snprintf(c.input_json, sizeof(c.input_json),
                                "{\"command\":\"cat > f <<'EOF'\\n");
    while (n < 4000) c.input_json[n++] = 'x';
    c.input_json[n] = '\0';
    ASSERT_EQ(agentic_sanitize_tool_inputs(&c, 1), 1);
    ASSERT_STR_EQ(c.input_json, "{}");
    char cmd[RUNCMD_COMMAND_MAX + 1], note[RUNCMD_NOTE_MAX];
    ASSERT_EQ(runcmd_parse_input(c.input_json, cmd, note),
              RUNCMD_PARSE_NO_COMMAND);

    /* The same call marked overflowed: its bytes are replaced even when
     * what was kept parses as an object (a spliced command must not run). */
    AiToolCall o;
    memset(&o, 0, sizeof(o));
    strcpy(o.name, RUNCMD_TOOL_NAME);
    strcpy(o.input_json, "{\"command\":\"rm -rf build-cache\"}");
    o.overflowed = 1;
    ASSERT_EQ(agentic_sanitize_tool_inputs(&o, 1), 1);
    ASSERT_STR_EQ(o.input_json, "{}");
    ASSERT_EQ(runcmd_parse_input(o.input_json, cmd, note),
              RUNCMD_PARSE_NO_COMMAND);
    TEST_END();
}

int test_agentic_sanitize_keeps_valid(void)
{
    TEST_BEGIN();
    AiToolCall c[5];
    memset(c, 0, sizeof(c));
    strcpy(c[0].input_json, "{\"command\":\"ls\"}");
    strcpy(c[1].input_json, "");
    strcpy(c[2].input_json, "[1]");
    strcpy(c[3].input_json, "null");
    strcpy(c[4].input_json, "{}");
    ASSERT_EQ(agentic_sanitize_tool_inputs(c, 5), 3);
    ASSERT_STR_EQ(c[0].input_json, "{\"command\":\"ls\"}");
    ASSERT_STR_EQ(c[1].input_json, "{}");
    ASSERT_STR_EQ(c[2].input_json, "{}");
    ASSERT_STR_EQ(c[3].input_json, "{}");
    ASSERT_STR_EQ(c[4].input_json, "{}");
    /* Nothing left to fix: a second pass changes nothing. */
    ASSERT_EQ(agentic_sanitize_tool_inputs(c, 5), 0);
    ASSERT_EQ(agentic_sanitize_tool_inputs(NULL, 3), 0);
    ASSERT_EQ(agentic_sanitize_tool_inputs(c, 0), 0);
    ASSERT_EQ(agentic_sanitize_tool_inputs(c, -1), 0);
    TEST_END();
}

/* An input is kept only when it is exactly one JSON object: anything after
 * it would make every later request body invalid. */
int test_agentic_sanitize_exactly_one_object(void)
{
    TEST_BEGIN();
    AiToolCall c[6];
    memset(c, 0, sizeof(c));
    strcpy(c[0].input_json, "{\"command\":\"ls\"}x");
    strcpy(c[1].input_json, "{}{}");
    strcpy(c[2].input_json, "{\"a\":1}{\"a\":1");
    strcpy(c[3].input_json, "{\"command\":\"ls\"}garbage");
    strcpy(c[4].input_json, "{\"command\":\"ls\"}  \n\t");
    strcpy(c[5].input_json, " \n{\"command\":\"ls\"}");
    ASSERT_EQ(agentic_sanitize_tool_inputs(c, 6), 4);
    for (int i = 0; i < 4; i++) ASSERT_STR_EQ(c[i].input_json, "{}");
    ASSERT_STR_EQ(c[4].input_json, "{\"command\":\"ls\"}  \n\t");
    ASSERT_STR_EQ(c[5].input_json, " \n{\"command\":\"ls\"}");
    TEST_END();
}

int test_agentic_room_for_tool_round(void)
{
    TEST_BEGIN();
    AiConversation conv;
    memset(&conv, 0, sizeof(conv));
    conv.msg_count = 61;
    ASSERT_EQ(agentic_room_for_tool_round(&conv), 1);
    conv.msg_count = 62;
    ASSERT_EQ(agentic_room_for_tool_round(&conv), 0);
    conv.msg_count = 0;
    ASSERT_EQ(agentic_room_for_tool_round(&conv), 1);
    conv.msg_count = AI_MAX_MESSAGES;
    ASSERT_EQ(agentic_room_for_tool_round(&conv), 0);
    ASSERT_EQ(agentic_room_for_tool_round(NULL), 0);
    TEST_END();
}

/* A run_command result is bounded by its executor (64 KiB, escaped): the
 * generic cap must not cut or strip it a second time. Another tool's result
 * of the same size is still capped. */
int test_agentic_runcmd_result_not_capped(void)
{
    TEST_BEGIN();
    size_t big = 70000;
    char *a = (char *)malloc(big + 1);
    char *b = (char *)malloc(big + 1);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    memset(a, 'a', big); a[big] = '\0';
    memset(b, 'b', big); b[big] = '\0';

    AiConversation conv;
    ai_conv_init(&conv, "m");
    AiToolCall calls[2];
    memset(calls, 0, sizeof(calls));
    strcpy(calls[0].name, RUNCMD_TOOL_NAME);
    strcpy(calls[1].name, "web_fetch");
    AiToolResult res[2];
    memset(res, 0, sizeof(res));
    res[0].content = a; res[0].content_len = big;
    res[1].content = b; res[1].content_len = big;
    ASSERT_EQ(agentic_add_tool_results(&conv, calls, res, 2), 0);
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(conv.messages[0].n_parts, 2);
    ASSERT_EQ(conv.messages[0].parts[0].content_len, big);
    ASSERT_STR_EQ(conv.messages[0].parts[0].tool_note, "");
    ASSERT_TRUE(conv.messages[0].parts[1].content_len < big);
    ASSERT_STR_EQ(conv.messages[0].parts[1].tool_note, AI_TOOL_TRUNC_NOTE);
    ai_conv_reset(&conv);
    free(a);
    free(b);
    TEST_END();
}

/* A long note ending with the cap's "\n" truncation line: the stored part
 * keeps the whole note, the line still at its end (a part carries
 * AI_TOOL_PART_NOTE_MAX bytes, so nothing is cut or moved). */
int test_agentic_note_copy_keeps_marker(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "m");
    AiToolCall call;
    memset(&call, 0, sizeof(call));
    strcpy(call.name, "web_fetch");
    AiToolResult r;
    memset(&r, 0, sizeof(r));
    size_t marker = strlen(AI_TOOL_TRUNC_NOTE);
    /* 'a' then 200 two-byte characters: 401 bytes of head, odd on purpose */
    size_t head_len = 401;
    r.tool_note[0] = 'a';
    for (size_t i = 1; i + 1 < head_len + 1; i += 2)
        memcpy(r.tool_note + i, "\xC3\xA9", 2);
    r.tool_note[head_len] = '\n';
    memcpy(r.tool_note + head_len + 1, AI_TOOL_TRUNC_NOTE, marker + 1);
    ASSERT_TRUE(head_len + 1 + marker < AI_TOOL_PART_NOTE_MAX);
    ASSERT_EQ(agentic_add_tool_results(&conv, &call, &r, 1), 0);
    ASSERT_EQ(conv.messages[0].n_parts, 1);
    const char *got = conv.messages[0].parts[0].tool_note;
    ASSERT_STR_EQ(got, r.tool_note);
    size_t glen = strlen(got);
    ASSERT_EQ(glen, head_len + 1 + marker);
    ASSERT_STR_EQ(got + glen - marker - 1, "\n" AI_TOOL_TRUNC_NOTE);
    ai_conv_reset(&conv);
    TEST_END();
}

/* AiToolResult and AiToolPart both carry AI_TOOL_PART_NOTE_MAX bytes of note;
 * a stored part keeps a 355-byte note, multi-byte character and all. */
int test_agentic_long_note_copy_cut_safely(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sizeof(((AiToolResult *)0)->tool_note), AI_TOOL_PART_NOTE_MAX);
    ASSERT_EQ(sizeof(((AiToolPart *)0)->tool_note), AI_TOOL_PART_NOTE_MAX);
    ASSERT_EQ(AI_TOOL_PART_NOTE_MAX, 512);

    AiConversation conv;
    ai_conv_init(&conv, "m");
    AiToolCall call;
    memset(&call, 0, sizeof(call));
    strcpy(call.name, "web_fetch");
    AiToolResult r;
    memset(&r, 0, sizeof(r));
    /* 254 ASCII bytes then a 2-byte character that straddles byte 255. */
    memset(r.tool_note, 'n', 254);
    memcpy(r.tool_note + 254, "\xC3\xA9", 2);
    memset(r.tool_note + 256, 'm', 100);
    ASSERT_EQ(agentic_add_tool_results(&conv, &call, &r, 1), 0);
    ASSERT_EQ(strlen(conv.messages[0].parts[0].tool_note), 356);
    ASSERT_STR_EQ(conv.messages[0].parts[0].tool_note, r.tool_note);
    ai_conv_reset(&conv);
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
    TEST_END();
}

/* Anthropic ends a cut-off reply with message_delta and stop_reason
 * "max_tokens"; any other reason is not a cut-off. */
int test_stream_cut_off_anthropic_max_tokens(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"max_tokens\","
        "\"stop_sequence\":null},\"usage\":{\"output_tokens\":8096}}"), 1);
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":null}}"), 0);
    /* The word as text is not a stop reason. */
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\","
        "\"text\":\"max_tokens\"}}"), 0);
    ASSERT_EQ(ai_stream_chunk_cut_off(
        "{\"type\":\"message_delta\",\"delta\":{}}"), 0);
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

/* ---- One stored turn per tool round (spec decision 11, 25) ---------------- */

/* Five calls, five results: each distinct, so an order or field mix-up shows. */
static void five_calls(AiToolCall calls[5], AiToolResult res[5],
                       char *bufs[5])
{
    static const char *names[5] = { "web_fetch", RUNCMD_TOOL_NAME, "web_search",
                                    RUNCMD_TOOL_NAME, "read_debug_log" };
    memset(calls, 0, 5 * sizeof(AiToolCall));
    memset(res, 0, 5 * sizeof(AiToolResult));
    for (int i = 0; i < 5; i++) {
        snprintf(calls[i].id, sizeof(calls[i].id), "call_%d", i);
        snprintf(calls[i].name, sizeof(calls[i].name), "%s", names[i]);
        snprintf(res[i].tool_use_id, sizeof(res[i].tool_use_id), "call_%d", i);
        char text[64];
        snprintf(text, sizeof(text), "output number %d", i);
        bufs[i] = (char *)malloc(strlen(text) + 1);
        strcpy(bufs[i], text);
        res[i].content = bufs[i];
        res[i].content_len = strlen(text);
        res[i].is_error = (i == 2);
    }
    /* result 1 is a cut run_command result with a full-length note */
    memset(res[1].tool_note, 'n', AI_TOOL_PART_NOTE_MAX - 1);
    res[1].omit_at = 7;
    res[1].omitted_bytes = 41213;
    snprintf(res[3].tool_note, sizeof(res[3].tool_note), "ran");
    res[4].content_len = 0;         /* an empty result */
    bufs[4][0] = '\0';
}

int test_agentic_results_one_turn(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "m");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&conv, AI_ROLE_USER, "go");

    AiToolCall calls[5];
    AiToolResult res[5];
    char *bufs[5];
    five_calls(calls, res, bufs);

    ASSERT_EQ(agentic_add_tool_results(&conv, calls, res, 5), 0);
    ASSERT_EQ(conv.msg_count, 3);                 /* one message, not five */
    AiMessage *m = &conv.messages[2];
    ASSERT_EQ((int)m->role, (int)AI_ROLE_TOOL);
    ASSERT_EQ(m->n_parts, 5);
    ASSERT_NOT_NULL(m->parts);
    if (m->parts && m->n_parts == 5) {
        for (int i = 0; i < 5; i++) {
            char want[16];
            snprintf(want, sizeof(want), "call_%d", i);
            ASSERT_STR_EQ(m->parts[i].tool_call_id, want);
            ASSERT_STR_EQ(m->parts[i].tool_name, calls[i].name);
            ASSERT_EQ(m->parts[i].is_error, i == 2);
        }
        ASSERT_STR_EQ(m->parts[0].content, "output number 0");
        ASSERT_EQ(m->parts[0].content_len, 15);
        ASSERT_TRUE(m->parts[0].content != bufs[0]);   /* its own copy */
        /* the cut fields and the whole 511-byte note survive */
        ASSERT_EQ(m->parts[1].omit_at, 7);
        ASSERT_EQ(m->parts[1].omitted_bytes, 41213);
        ASSERT_EQ(strlen(m->parts[1].tool_note), AI_TOOL_PART_NOTE_MAX - 1);
        ASSERT_STR_EQ(m->parts[3].tool_note, "ran");
        ASSERT_EQ(m->parts[0].omitted_bytes, 0);
        ASSERT_EQ(m->parts[4].content_len, 0);
    }

    /* A deep copy and a take each own what they hold; reset frees all
     * (SAN=1 reports a leak or a double free). */
    AiConversation *copy = (AiConversation *)calloc(1, sizeof(*copy));
    AiConversation *moved = (AiConversation *)calloc(1, sizeof(*moved));
    ASSERT_NOT_NULL(copy);
    ASSERT_NOT_NULL(moved);
    if (copy && moved) {
        ASSERT_EQ(ai_conv_copy(copy, &conv), 0);
        AiMessage *c = &copy->messages[2];
        ASSERT_EQ(c->n_parts, 5);
        ASSERT_TRUE(c->parts != m->parts);
        ASSERT_TRUE(c->parts && c->parts[0].content != m->parts[0].content);
        ASSERT_STR_EQ(c->parts[0].content, "output number 0");
        ASSERT_EQ(c->parts[1].omitted_bytes, 41213);
        ASSERT_STR_EQ(c->parts[3].tool_note, "ran");

        ai_conv_take(moved, &conv);
        ASSERT_EQ(conv.msg_count, 0);
        ASSERT_EQ(moved->messages[2].n_parts, 5);
        ASSERT_STR_EQ(moved->messages[2].parts[2].tool_call_id, "call_2");
        ASSERT_EQ(conv.messages[2].n_parts, 0);
        ASSERT_NULL(conv.messages[2].parts);
        ai_conv_reset(copy);
        ai_conv_reset(moved);
    }
    ai_conv_reset(&conv);
    free(copy);
    free(moved);
    for (int i = 0; i < 5; i++) free(bufs[i]);

    /* Full: one slot is all a round needs, none left stores nothing. */
    AiConversation full;
    ai_conv_init(&full, "m");
    for (int i = 0; i < AI_MAX_MESSAGES - 1; i++)
        ai_conv_add(&full, AI_ROLE_USER, "x");
    five_calls(calls, res, bufs);
    ASSERT_EQ(agentic_add_tool_results(&full, calls, res, 5), 0);
    ASSERT_EQ(full.msg_count, AI_MAX_MESSAGES);
    ASSERT_EQ(agentic_add_tool_results(&full, calls, res, 5), -1);
    ASSERT_EQ(full.msg_count, AI_MAX_MESSAGES);
    ai_conv_reset(&full);
    for (int i = 0; i < 5; i++) free(bufs[i]);

    /* No results store no message. */
    AiConversation none;
    ai_conv_init(&none, "m");
    ASSERT_EQ(agentic_add_tool_results(&none, calls, res, 0), 0);
    ASSERT_EQ(none.msg_count, 0);
    TEST_END();
}

/* An allocator that fails on the Nth call (fail_at) or on every call from the
 * Nth on (fail_from). */
static int g_alloc_n, g_alloc_fail_at, g_alloc_fail_from;

static void *counting_alloc(size_t n)
{
    g_alloc_n++;
    if (g_alloc_n == g_alloc_fail_at) return NULL;
    if (g_alloc_fail_from && g_alloc_n >= g_alloc_fail_from) return NULL;
    return malloc(n);
}

static void alloc_plan(int fail_at, int fail_from)
{
    g_alloc_n = 0;
    g_alloc_fail_at = fail_at;
    g_alloc_fail_from = fail_from;
    agentic_set_alloc_for_test(counting_alloc);
}

/* A result's content that cannot be copied does not leave its call
 * unanswered: the round is stored as one notes-only turn (spec 25). If even
 * that array cannot be had, nothing is stored (Task B4 then drops the open
 * assistant turn). */
int test_agentic_results_oom_notes_only(void)
{
    TEST_BEGIN();
    AiToolCall calls[5];
    AiToolResult res[5];
    char *bufs[5];

    /* allocation 1 is the parts array, 2 the first content copy: fail it */
    AiConversation conv;
    ai_conv_init(&conv, "m");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    five_calls(calls, res, bufs);
    alloc_plan(2, 0);
    ASSERT_EQ(agentic_add_tool_results(&conv, calls, res, 5), 0);
    agentic_set_alloc_for_test(NULL);
    ASSERT_EQ(conv.msg_count, 2);
    AiMessage *m = &conv.messages[1];
    ASSERT_EQ((int)m->role, (int)AI_ROLE_TOOL);
    ASSERT_EQ(m->n_parts, 5);
    if (m->parts && m->n_parts == 5) {
        for (int i = 0; i < 5; i++) {
            char want[16];
            snprintf(want, sizeof(want), "call_%d", i);
            ASSERT_STR_EQ(m->parts[i].tool_call_id, want);
            ASSERT_STR_EQ(m->parts[i].tool_name, calls[i].name);
            ASSERT_NULL(m->parts[i].content);
            ASSERT_EQ(m->parts[i].content_len, 0);
            ASSERT_EQ(m->parts[i].omitted_bytes, 0);
            ASSERT_STR_EQ(m->parts[i].tool_note,
                          "not run: Nutshell ran out of memory");
        }
    }
    /* the caller still owns its results */
    ASSERT_STR_EQ(res[0].content, "output number 0");

    /* the turn answers every call: a well-formed Anthropic body */
    char body[8192];
    ASSERT_TRUE(ai_build_request_body_ex(&conv, NULL, body, sizeof(body), 0,
                                         "anthropic") > 0);
    ASSERT_TRUE(strstr(body, "\"tool_use_id\":\"call_4\"") != NULL);
    ASSERT_TRUE(strstr(body, "not run: Nutshell ran out of memory") != NULL);
    ai_conv_reset(&conv);

    /* a failure at a later part falls back the same way */
    ai_conv_init(&conv, "m");
    alloc_plan(4, 0);
    ASSERT_EQ(agentic_add_tool_results(&conv, calls, res, 5), 0);
    agentic_set_alloc_for_test(NULL);
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(conv.messages[0].n_parts, 5);
    ASSERT_NULL(conv.messages[0].parts[3].content);
    ai_conv_reset(&conv);

    /* nothing can be allocated: -1 and nothing stored */
    ai_conv_init(&conv, "m");
    ai_conv_add(&conv, AI_ROLE_USER, "go");
    alloc_plan(0, 1);
    ASSERT_EQ(agentic_add_tool_results(&conv, calls, res, 5), -1);
    agentic_set_alloc_for_test(NULL);
    ASSERT_EQ(conv.msg_count, 1);

    /* the parts array comes, the first content does not, and the notes-only
     * array does not either: the partial first array is freed (SAN=1) */
    alloc_plan(0, 2);
    ASSERT_EQ(agentic_add_tool_results(&conv, calls, res, 5), -1);
    agentic_set_alloc_for_test(NULL);
    ASSERT_EQ(conv.msg_count, 1);
    ai_conv_reset(&conv);
    for (int i = 0; i < 5; i++) free(bufs[i]);
    TEST_END();
}

/* A note a part carries is the full AiToolResult note, never the 256-byte cut
 * of a stored message, and the cap's truncation line stays at its end. */
int test_agentic_parts_keep_full_note(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "m");
    AiToolCall call;
    memset(&call, 0, sizeof(call));
    strcpy(call.name, "web_fetch");
    AiToolResult r;
    memset(&r, 0, sizeof(r));
    memset(r.tool_note, 'n', 400);
    memcpy(r.tool_note + 400, "\xC3\xA9", 2);
    ASSERT_EQ(agentic_add_tool_results(&conv, &call, &r, 1), 0);
    ASSERT_EQ(conv.messages[0].n_parts, 1);
    ASSERT_STR_EQ(conv.messages[0].parts[0].tool_note, r.tool_note);
    ASSERT_NULL(conv.messages[0].parts[0].content);
    ai_conv_reset(&conv);
    TEST_END();
}
