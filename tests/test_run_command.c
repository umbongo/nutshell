#include "test_framework.h"
#include "run_command.h"
#include "ai_prompt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A valid UTF-8 string with no partial sequence at its end. */
static int utf8_whole(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n = (*p < 0x80u) ? 1 : ((*p & 0xE0u) == 0xC0u) ? 2
              : ((*p & 0xF0u) == 0xE0u) ? 3 : ((*p & 0xF8u) == 0xF0u) ? 4 : 0;
        if (n == 0) return 0;
        for (int i = 1; i < n; i++)
            if ((p[i] & 0xC0u) != 0x80u) return 0;
        p += n;
    }
    return 1;
}

int test_runcmd_parse_ok(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];

    ASSERT_EQ(runcmd_parse_input("{\"command\":\"ls -la\",\"note\":\"list\"}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "ls -la");
    ASSERT_STR_EQ(note, "list");

    /* no note: note is "" */
    memset(note, 'x', sizeof note);
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"pwd\"}", cmd, note), RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "pwd");
    ASSERT_STR_EQ(note, "");

    /* a note that is not a string: "" and still OK */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"pwd\",\"note\":5}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(note, "");

    /* nothing is trimmed: the command is the string as sent */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"  ls \"}", cmd, note), RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "  ls ");

    /* escaped quotes and non-ASCII text survive */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"echo \\\"h\xC3\xA9\\\"\"}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "echo \"h\xC3\xA9\"");
    TEST_END();
}

int test_runcmd_parse_no_command(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    const char *bad[] = {
        NULL, "", "{}", "{\"command\":\"\"}", "{\"command\":\"   \"}",
        "{\"command\":\" \\t \"}", "{\"command\":5}", "{\"command\":null}",
        "{\"command\":[\"ls\"]}", "[1]", "\"ls\"", "{\"command\":\"ls",
        "{\"note\":\"only a note\"}"
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memset(cmd, 'x', sizeof cmd);
        memset(note, 'x', sizeof note);
        RunCmdParse p = runcmd_parse_input(bad[i], cmd, note);
        if (p != RUNCMD_PARSE_NO_COMMAND) {
            printf("  case %zu: got %d\n", i, (int)p);
            _tf_local_fail = 1;
        }
        /* both outputs are defined on a failure */
        ASSERT_STR_EQ(cmd, "");
        ASSERT_STR_EQ(note, "");
    }
    TEST_END();
}

int test_runcmd_parse_length(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    static char json[RUNCMD_COMMAND_MAX + 64];

    memcpy(json, "{\"command\":\"", 12);
    memset(json + 12, 'a', RUNCMD_COMMAND_MAX);
    memcpy(json + 12 + RUNCMD_COMMAND_MAX, "\"}", 3);
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_OK);
    ASSERT_EQ(strlen(cmd), (size_t)RUNCMD_COMMAND_MAX);

    memset(json + 12, 'a', RUNCMD_COMMAND_MAX + 1);
    memcpy(json + 12 + RUNCMD_COMMAND_MAX + 1, "\"}", 3);
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_TOO_LONG);
    ASSERT_STR_EQ(cmd, "");

    /* too long wins over unsafe: the model is told to split it first */
    memset(json + 12, 'a', RUNCMD_COMMAND_MAX + 2);
    memcpy(json + 12 + RUNCMD_COMMAND_MAX + 2, "\"}", 3);
    json[12] = '\\'; json[13] = 'n';   /* 1 newline + 1024 more: 1025 bytes */
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_TOO_LONG);
    TEST_END();
}

int test_runcmd_parse_unsafe(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    const char *bad[] = {
        "{\"command\":\"ls\\nrm x\"}",
        "{\"command\":\"a\\tb\"}",
        "{\"command\":\"a\\rb\"}",
        "{\"command\":\"a\\u001b[0m\"}",
        "{\"command\":\"a\\u007fb\"}",
        "{\"command\":\"a\\u0085b\"}",
        "{\"command\":\"a\\u202eb\"}",
        "{\"command\":\"a\\u2066b\"}",
        "{\"command\":\"a\xE2\x80\xAEz\"}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        RunCmdParse p = runcmd_parse_input(bad[i], cmd, note);
        if (p != RUNCMD_PARSE_UNSAFE) {
            printf("  case %zu: got %d\n", i, (int)p);
            _tf_local_fail = 1;
        }
        ASSERT_STR_EQ(cmd, "");
    }
    /* ordinary non-ASCII text is not unsafe */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"echo \xE2\x82\xAC\"}", cmd, note),
              RUNCMD_PARSE_OK);
    TEST_END();
}

int test_runcmd_parse_note_cut(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    static char json[1024];

    /* 300 two-byte characters: cut to 255 bytes on a boundary, so 254 */
    strcpy(json, "{\"command\":\"ls\",\"note\":\"");
    size_t at = strlen(json);
    for (int i = 0; i < 300; i++) { json[at++] = (char)0xC3; json[at++] = (char)0xA9; }
    strcpy(json + at, "\"}");
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_OK);
    ASSERT_TRUE(strlen(note) <= 255);
    ASSERT_EQ(strlen(note), (size_t)254);
    ASSERT_TRUE(utf8_whole(note));

    /* three-byte characters: 255 is a multiple of 3, so exactly 255 */
    strcpy(json, "{\"command\":\"ls\",\"note\":\"");
    at = strlen(json);
    for (int i = 0; i < 200; i++) {
        json[at++] = (char)0xE2; json[at++] = (char)0x82; json[at++] = (char)0xAC;
    }
    strcpy(json + at, "\"}");
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_OK);
    ASSERT_EQ(strlen(note), (size_t)255);
    ASSERT_TRUE(utf8_whole(note));

    /* four-byte characters: 63 of them fit (252 bytes) */
    strcpy(json, "{\"command\":\"ls\",\"note\":\"");
    at = strlen(json);
    for (int i = 0; i < 100; i++) {
        json[at++] = (char)0xF0; json[at++] = (char)0x9F;
        json[at++] = (char)0x98; json[at++] = (char)0x80;
    }
    strcpy(json + at, "\"}");
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_OK);
    ASSERT_EQ(strlen(note), (size_t)252);
    ASSERT_TRUE(utf8_whole(note));

    /* an unsafe note is dropped, the command still runs */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"ls\",\"note\":\"a\\nb\"}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "ls");
    ASSERT_STR_EQ(note, "");
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"ls\",\"note\":\"a\\u202eb\"}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(note, "");
    TEST_END();
}

int test_runcmd_parse_error_texts(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(runcmd_parse_error(RUNCMD_PARSE_OK), "");
    ASSERT_STR_EQ(runcmd_parse_error(RUNCMD_PARSE_NO_COMMAND),
                  "run_command needs a \"command\" string");
    ASSERT_STR_EQ(runcmd_parse_error(RUNCMD_PARSE_TOO_LONG),
                  "command longer than 1023 bytes: break the work into smaller steps");
    ASSERT_STR_EQ(runcmd_parse_error(RUNCMD_PARSE_UNSAFE),
                  "command contains control or bidirectional formatting characters: "
                  "send one single-line command");
    TEST_END();
}

int test_runcmd_describe(void)
{
    TEST_BEGIN();
    char out[64];

    ASSERT_EQ(runcmd_describe("{\"command\":\"uptime\"}", out, sizeof out), 1);
    ASSERT_STR_EQ(out, "uptime");

    memset(out, 'x', sizeof out);
    ASSERT_EQ(runcmd_describe("{}", out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");

    memset(out, 'x', sizeof out);
    ASSERT_EQ(runcmd_describe("{\"command\":\"a\\nb\"}", out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");

    ASSERT_EQ(runcmd_describe(NULL, out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");

    /* a small buffer: cut on a UTF-8 boundary, still terminated */
    char small[5];
    ASSERT_EQ(runcmd_describe("{\"command\":\"ab\xC3\xA9\xC3\xA9\"}", small, sizeof small), 1);
    ASSERT_STR_EQ(small, "ab\xC3\xA9");

    /* no buffer: nothing written, no crash */
    ASSERT_EQ(runcmd_describe("{\"command\":\"ls\"}", NULL, 0), 1);
    TEST_END();
}

int test_runcmd_auto_default_table(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_runcmd_auto_default("anthropic"), 1);
    ASSERT_EQ(ai_runcmd_auto_default("openai"), 1);
    ASSERT_EQ(ai_runcmd_auto_default("moonshot"), 1);
    ASSERT_EQ(ai_runcmd_auto_default("deepseek"), 0);
    ASSERT_EQ(ai_runcmd_auto_default("gemini"), 0);
    ASSERT_EQ(ai_runcmd_auto_default("custom"), 0);
    ASSERT_EQ(ai_runcmd_auto_default("other"), 0);
    ASSERT_EQ(ai_runcmd_auto_default(NULL), 0);
    TEST_END();
}

int test_provider_runcmd_modes(void)
{
    TEST_BEGIN();
    const char *all[] = { "anthropic", "openai", "deepseek", "moonshot", "gemini", "custom" };
    const int web_tools[] = { 1, 1, 1, 1, 0, 0 };   /* ai_provider_tools, AUTO */

    ASSERT_EQ((int)AI_RUNCMD_AUTO, 0);
    ASSERT_EQ((int)AI_RUNCMD_ON, 1);
    ASSERT_EQ((int)AI_RUNCMD_OFF, 2);

    for (int i = 0; i < 6; i++) {
        const char *p = all[i];
        /* Off: no run_command anywhere, the web tools unchanged */
        ASSERT_EQ(ai_provider_runcmd(p, AI_RUNCMD_OFF), 0);
        ASSERT_EQ(ai_provider_tools(p, AI_RUNCMD_OFF), web_tools[i]);
        /* On: every provider */
        ASSERT_EQ(ai_provider_runcmd(p, AI_RUNCMD_ON), 1);
        ASSERT_EQ(ai_provider_tools(p, AI_RUNCMD_ON), 1);
        /* Automatic: the table; web tools as today */
        ASSERT_EQ(ai_provider_runcmd(p, AI_RUNCMD_AUTO), ai_runcmd_auto_default(p));
        ASSERT_EQ(ai_provider_tools(p, AI_RUNCMD_AUTO), web_tools[i]);
        ASSERT_EQ(ai_provider_supports_tools(p), web_tools[i]);
    }

    /* unknown providers and NULL never get tools, whatever the mode */
    for (int m = 0; m <= 2; m++) {
        ASSERT_EQ(ai_provider_runcmd("other", m), 0);
        ASSERT_EQ(ai_provider_runcmd(NULL, m), 0);
        ASSERT_EQ(ai_provider_tools("other", m), 0);
        ASSERT_EQ(ai_provider_tools(NULL, m), 0);
    }
    ASSERT_EQ(ai_provider_supports_tools("other"), 0);
    ASSERT_EQ(ai_provider_supports_tools(NULL), 0);
    TEST_END();
}

int test_runcmd_reject_hint(void)
{
    TEST_BEGIN();
    ASSERT_EQ(runcmd_reject_hint_applies(400, "tools is not supported"), 1);
    ASSERT_EQ(runcmd_reject_hint_applies(422, "Function calling is not enabled"), 1);
    ASSERT_EQ(runcmd_reject_hint_applies(400, "reasoning_content must be passed back"), 1);
    ASSERT_EQ(runcmd_reject_hint_applies(400, "Unknown parameter: TOOLS"), 1);
    ASSERT_EQ(runcmd_reject_hint_applies(400, "REASONING_CONTENT missing"), 1);

    ASSERT_EQ(runcmd_reject_hint_applies(401, "tools"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(402, "tools"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(429, "tools"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(500, "tools"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(400, "maximum context length is 8192 tokens"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(400, ""), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(400, NULL), 0);
    /* in-stream error objects have no status */
    ASSERT_EQ(runcmd_reject_hint_applies(0, "overloaded"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(0, "tools"), 0);

    ASSERT_STR_EQ(runcmd_reject_hint(),
        "[If this provider does not accept Nutshell's tool calls, set Settings > "
        "AI Assistant > Provider > Commands as tool calls to Off]");
    TEST_END();
}

/* ---- the planner (spec decisions 4 to 6) ---- */

static void set_call(AiToolCall *c, const char *id, const char *name, const char *input)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->id, sizeof(c->id), "%s", id);
    snprintf(c->name, sizeof(c->name), "%s", name);
    snprintf(c->input_json, sizeof(c->input_json), "%s", input);
}

static void set_cmd_call(AiToolCall *c, int i)
{
    char id[32], in[64];
    snprintf(id, sizeof(id), "call_%d", i);
    snprintf(in, sizeof(in), "{\"command\":\"echo %d\"}", i);
    set_call(c, id, RUNCMD_TOOL_NAME, in);
}

static int g_plan_alloc_calls, g_plan_alloc_fail_at;
static void *plan_failing_alloc(size_t n)
{
    g_plan_alloc_calls++;
    if (g_plan_alloc_calls == g_plan_alloc_fail_at) return NULL;
    return malloc(n);
}

int test_runcmd_plan_order(void)
{
    TEST_BEGIN();
    AiToolCall *c = calloc(4, sizeof(*c));
    ASSERT_NOT_NULL(c);
    set_call(&c[0], "id0", RUNCMD_TOOL_NAME, "{\"command\":\"ls\",\"note\":\"list\"}");
    set_call(&c[1], "id1", "web_fetch", "{\"url\":\"https://example.com\"}");
    set_call(&c[2], "id2", RUNCMD_TOOL_NAME, "{}");
    set_call(&c[3], "id3", RUNCMD_TOOL_NAME, "{\"command\":\"a\\nb\"}");

    RunCmdReply *r = runcmd_plan(c, 4, 1);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->n_calls, 4);
    ASSERT_EQ(r->n_slot, 1);
    ASSERT_EQ(r->calls[0].kind, RUNCMD_KIND_SLOT);
    ASSERT_EQ(r->calls[1].kind, RUNCMD_KIND_OTHER_TOOL);
    ASSERT_EQ(r->calls[2].kind, RUNCMD_KIND_INVALID);
    ASSERT_EQ(r->calls[2].parse, RUNCMD_PARSE_NO_COMMAND);
    ASSERT_EQ(r->calls[3].kind, RUNCMD_KIND_INVALID);
    ASSERT_EQ(r->calls[3].parse, RUNCMD_PARSE_UNSAFE);
    ASSERT_STR_EQ(r->calls[0].tool_use_id, "id0");
    ASSERT_STR_EQ(r->calls[1].tool_use_id, "id1");
    ASSERT_STR_EQ(r->calls[2].tool_use_id, "id2");
    ASSERT_STR_EQ(r->calls[3].tool_use_id, "id3");
    ASSERT_STR_EQ(r->calls[1].tool_name, "web_fetch");
    ASSERT_STR_EQ(r->calls[0].tool_name, RUNCMD_TOOL_NAME);
    ASSERT_STR_EQ(r->calls[0].command, "ls");
    ASSERT_STR_EQ(r->calls[0].note, "list");
    ASSERT_STR_EQ(r->calls[2].command, "");
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(r->calls[i].entry, -1);
        ASSERT_EQ(r->calls[i].fetch_off, 0);
        ASSERT_EQ(r->calls[i].cap.started, 0);
        ASSERT_EQ(r->calls[i].cap.captured, 0);
        ASSERT_NULL(r->calls[i].cap.head);
        ASSERT_NULL(r->calls[i].cap.tail);
    }
    runcmd_reply_free(r);

    r = runcmd_plan(c, 4, 0);
    ASSERT_NOT_NULL(r);
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(r->calls[i].fetch_off, i == 1 ? 1 : 0);
    runcmd_reply_free(r);
    free(c);
    TEST_END();
}

int test_runcmd_plan_over_cap(void)
{
    TEST_BEGIN();
    AiToolCall *c = calloc(17, sizeof(*c));
    ASSERT_NOT_NULL(c);
    for (int i = 0; i < 17; i++) set_cmd_call(&c[i], i);
    RunCmdReply *r = runcmd_plan(c, 17, 1);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->n_calls, 17);
    ASSERT_EQ(r->n_slot, 16);
    for (int i = 0; i < 16; i++) ASSERT_EQ(r->calls[i].kind, RUNCMD_KIND_SLOT);
    ASSERT_EQ(r->calls[16].kind, RUNCMD_KIND_OVER_CAP);
    ASSERT_EQ(r->calls[16].parse, RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(r->calls[16].command, "echo 16");
    runcmd_reply_free(r);

    /* the streamed list can reach AI_TOOL_STREAM_CALLS_MAX */
    free(c);
    c = calloc(AI_TOOL_STREAM_CALLS_MAX, sizeof(*c));
    ASSERT_NOT_NULL(c);
    for (int i = 0; i < AI_TOOL_STREAM_CALLS_MAX; i++) set_cmd_call(&c[i], i);
    r = runcmd_plan(c, AI_TOOL_STREAM_CALLS_MAX, 1);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->n_slot, RUNCMD_SLOT_MAX);
    ASSERT_EQ(r->calls[AI_TOOL_STREAM_CALLS_MAX - 1].kind, RUNCMD_KIND_OVER_CAP);
    runcmd_reply_free(r);
    free(c);
    TEST_END();
}

int test_runcmd_plan_mixed_cap(void)
{
    TEST_BEGIN();
    AiToolCall *c = calloc(19, sizeof(*c));
    ASSERT_NOT_NULL(c);
    for (int i = 0; i < 3; i++) set_call(&c[i], "bad", RUNCMD_TOOL_NAME, "{}");
    for (int i = 3; i < 19; i++) set_cmd_call(&c[i], i);
    RunCmdReply *r = runcmd_plan(c, 19, 1);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->n_slot, 16);
    for (int i = 0; i < 3; i++) ASSERT_EQ(r->calls[i].kind, RUNCMD_KIND_INVALID);
    for (int i = 3; i < 19; i++) ASSERT_EQ(r->calls[i].kind, RUNCMD_KIND_SLOT);
    runcmd_reply_free(r);
    free(c);
    TEST_END();
}

int test_runcmd_plan_edges(void)
{
    TEST_BEGIN();
    AiToolCall c;
    set_cmd_call(&c, 1);
    ASSERT_NULL(runcmd_plan(NULL, 1, 1));
    ASSERT_NULL(runcmd_plan(&c, 0, 1));
    ASSERT_NULL(runcmd_plan(&c, -3, 1));
    runcmd_reply_free(NULL);                       /* NULL-safe */

    /* a stream that cut the input to {} is INVALID, not a slot */
    set_call(&c, "x", RUNCMD_TOOL_NAME, "{}");
    c.overflowed = 1;
    RunCmdReply *r = runcmd_plan(&c, 1, 1);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->calls[0].kind, RUNCMD_KIND_INVALID);
    ASSERT_EQ(r->n_slot, 0);
    runcmd_reply_free(r);
    TEST_END();
}

int test_runcmd_plan_oom_returns_null(void)
{
    TEST_BEGIN();
    AiToolCall c;
    set_cmd_call(&c, 1);
    for (int at = 1; at <= 2; at++) {
        runcmd_set_alloc_for_test(plan_failing_alloc);
        g_plan_alloc_calls = 0;
        g_plan_alloc_fail_at = at;
        ASSERT_NULL(runcmd_plan(&c, 1, 1));
    }
    runcmd_set_alloc_for_test(NULL);
    RunCmdReply *r = runcmd_plan(&c, 1, 1);
    ASSERT_NOT_NULL(r);
    runcmd_reply_free(r);
    TEST_END();
}

int test_runcmd_call_for_entry(void)
{
    TEST_BEGIN();
    AiToolCall *c = calloc(3, sizeof(*c));
    ASSERT_NOT_NULL(c);
    for (int i = 0; i < 3; i++) set_cmd_call(&c[i], i);
    RunCmdReply *r = runcmd_plan(c, 3, 1);
    ASSERT_NOT_NULL(r);
    r->calls[2].entry = 5;
    ASSERT_TRUE(runcmd_call_for_entry(r, 5) == &r->calls[2]);
    ASSERT_NULL(runcmd_call_for_entry(r, -1));     /* unqueued calls are -1 */
    ASSERT_NULL(runcmd_call_for_entry(r, 4));
    ASSERT_NULL(runcmd_call_for_entry(NULL, 5));
    runcmd_reply_free(r);
    free(c);
    TEST_END();
}

int test_runcmd_reply_has_call(void)
{
    TEST_BEGIN();
    AiToolCall c[2];
    set_call(&c[0], "a", "web_search", "{\"query\":\"x\"}");
    ASSERT_EQ(runcmd_reply_has_call(c, 1), 0);
    set_cmd_call(&c[1], 1);
    ASSERT_EQ(runcmd_reply_has_call(c, 2), 1);
    ASSERT_EQ(runcmd_reply_has_call(c, 0), 0);
    ASSERT_EQ(runcmd_reply_has_call(NULL, 2), 0);
    TEST_END();
}
