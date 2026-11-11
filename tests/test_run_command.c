#include "test_framework.h"
#include "run_command.h"
#include "ai_prompt.h"
#include "term.h"
#include "term_extract.h"

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

/* ---- The cap, the cut and the capture (spec decisions 10 and 11) ---- */

int test_runcmd_cap_bytes(void)
{
    TEST_BEGIN();
    ASSERT_EQ((int)runcmd_cap_bytes(1), 16384);
    ASSERT_EQ((int)runcmd_cap_bytes(2), 16384);
    ASSERT_EQ((int)runcmd_cap_bytes(3), 16384);   /* 65536 / 3 = 21845, capped */
    ASSERT_EQ((int)runcmd_cap_bytes(4), 16384);
    ASSERT_EQ((int)runcmd_cap_bytes(5), 13107);
    ASSERT_EQ((int)runcmd_cap_bytes(8), 8192);
    ASSERT_EQ((int)runcmd_cap_bytes(16), 4096);
    ASSERT_EQ((int)runcmd_cap_bytes(0), 16384);
    ASSERT_EQ((int)runcmd_cap_bytes(-4), 16384);
    ASSERT_TRUE(runcmd_cap_bytes(1000000) >= 1);  /* never 0 */
    TEST_END();
}

int test_runcmd_cut_uncut(void)
{
    TEST_BEGIN();
    char text[101];
    memset(text, 'x', 100);
    text[100] = '\0';
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    runcmd_cut(text, 100, 4096, &c);
    ASSERT_NOT_NULL(c.head);
    ASSERT_EQ((int)c.head_len, 100);
    ASSERT_STR_EQ(c.head, text);
    ASSERT_NULL(c.tail);
    ASSERT_EQ((int)c.tail_len, 0);
    ASSERT_EQ((int)c.omitted_bytes, 0);

    /* exactly the cap is still uncut; one more is cut (the cut replaces) */
    runcmd_cut(text, 100, 100, &c);
    ASSERT_EQ((int)c.head_len, 100);
    ASSERT_NULL(c.tail);
    runcmd_cut(text, 100, 99, &c);
    ASSERT_NOT_NULL(c.tail);
    ASSERT_EQ((int)(c.head_len + c.tail_len + c.omitted_bytes), 100);
    free(c.head);
    free(c.tail);
    TEST_END();
}

int test_runcmd_cut_head_tail(void)
{
    TEST_BEGIN();
    char *text = (char *)malloc(20001);
    ASSERT_NOT_NULL(text);
    memset(text, 'a', 20000);
    for (int i = 0; i < 3072; i++) text[i] = 'h';
    for (int i = 20000 - 1024; i < 20000; i++) text[i] = 't';
    text[20000] = '\0';
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    runcmd_cut(text, 20000, 4096, &c);
    ASSERT_EQ((int)c.head_len, 3072);
    ASSERT_EQ((int)c.tail_len, 1024);
    ASSERT_EQ((int)c.omitted_bytes, 15904);
    ASSERT_EQ((int)strlen(c.head), 3072);
    ASSERT_EQ((int)strlen(c.tail), 1024);
    ASSERT_TRUE(c.head[0] == 'h' && c.head[3071] == 'h');
    ASSERT_TRUE(c.tail[0] == 't' && c.tail[1023] == 't');
    ASSERT_EQ((int)c.flags, 0);                    /* the cut sets no flag */
    free(c.head);
    free(c.tail);
    free(text);
    TEST_END();
}

int test_runcmd_cut_multibyte(void)
{
    TEST_BEGIN();
    /* "a" then 7000 euro signs (3 bytes each): both cut points fall inside a
     * character. */
    size_t n = 7000, len = 1 + 3 * n;
    char *text = (char *)malloc(len + 1);
    ASSERT_NOT_NULL(text);
    text[0] = 'a';
    for (size_t i = 0; i < n; i++) memcpy(text + 1 + 3 * i, "\xE2\x82\xAC", 3);
    text[len] = '\0';
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    runcmd_cut(text, len, 4096, &c);
    ASSERT_NOT_NULL(c.head);
    ASSERT_NOT_NULL(c.tail);
    ASSERT_TRUE(c.head_len < 3072 && c.head_len > 3072 - 4);   /* stepped back */
    ASSERT_TRUE(c.tail_len < 1024 && c.tail_len > 1024 - 4);   /* stepped forward */
    ASSERT_TRUE(utf8_whole(c.head));
    ASSERT_TRUE(utf8_whole(c.tail));
    ASSERT_EQ((int)(c.head_len + c.tail_len + c.omitted_bytes), (int)len);
    ASSERT_EQ((int)strlen(c.head), (int)c.head_len);
    ASSERT_EQ((int)strlen(c.tail), (int)c.tail_len);
    free(c.head);
    free(c.tail);

    /* a four-byte character at both cut points, cap 100: 75 / 25 */
    char four[401];
    for (int i = 0; i < 100; i++) memcpy(four + 4 * i, "\xF0\x9F\x98\x80", 4);
    four[400] = '\0';
    memset(&c, 0, sizeof c);
    runcmd_cut(four, 400, 100, &c);
    ASSERT_TRUE(utf8_whole(c.head) && utf8_whole(c.tail));
    ASSERT_EQ((int)c.head_len, 72);
    ASSERT_EQ((int)c.tail_len, 24);
    ASSERT_EQ((int)(c.head_len + c.tail_len + c.omitted_bytes), 400);
    free(c.head);
    free(c.tail);
    free(text);
    TEST_END();
}

int test_runcmd_cut_replaces_and_edges(void)
{
    TEST_BEGIN();
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    char big[501];
    memset(big, 'b', 500);
    big[500] = '\0';
    runcmd_cut(big, 500, 100, &c);                 /* head and tail, heap */
    ASSERT_NOT_NULL(c.tail);
    runcmd_cut("short", 5, 100, &c);               /* replaces; SAN checks the frees */
    ASSERT_STR_EQ(c.head, "short");
    ASSERT_NULL(c.tail);
    ASSERT_EQ((int)c.tail_len, 0);
    ASSERT_EQ((int)c.omitted_bytes, 0);
    runcmd_cut("", 0, 100, &c);                    /* empty text: no excerpt */
    ASSERT_NULL(c.head);
    ASSERT_EQ((int)c.head_len, 0);
    runcmd_cut(NULL, 7, 100, &c);                  /* NULL text is empty text */
    ASSERT_NULL(c.head);
    ASSERT_EQ((int)c.omitted_bytes, 0);
    runcmd_cut("x", 1, 100, NULL);                 /* NULL capture: nothing */

    /* a cap of 0 omits it all, with no excerpt */
    runcmd_cut("abc", 3, 0, &c);
    ASSERT_NULL(c.head);
    ASSERT_NULL(c.tail);
    ASSERT_EQ((int)c.omitted_bytes, 3);
    /* a tiny cap: tail 0, head the cap's three quarters */
    runcmd_cut("abcdefgh", 8, 3, &c);
    ASSERT_EQ((int)(c.head_len + c.tail_len + c.omitted_bytes), 8);
    free(c.head);
    free(c.tail);
    TEST_END();
}

int test_runcmd_cut_oom(void)
{
    TEST_BEGIN();
    char *text = (char *)malloc(20001);
    ASSERT_NOT_NULL(text);
    memset(text, 'z', 20000);
    text[20000] = '\0';
    /* the cut allocates head then tail (uncut: one block); fail each in turn */
    for (int at = 1; at <= 2; at++) {
        RunCmdCapture c;
        memset(&c, 0, sizeof c);
        runcmd_cut("old", 3, 100, &c);             /* an excerpt to replace */
        runcmd_set_alloc_for_test(plan_failing_alloc);
        g_plan_alloc_calls = 0;
        g_plan_alloc_fail_at = at;
        runcmd_cut(text, 20000, 4096, &c);
        runcmd_set_alloc_for_test(NULL);
        ASSERT_NULL(c.head);                       /* well-formed: no excerpt... */
        ASSERT_NULL(c.tail);
        ASSERT_EQ((int)c.head_len, 0);
        ASSERT_EQ((int)c.tail_len, 0);
        ASSERT_EQ((int)c.omitted_bytes, 20000);    /* ...and the size says dropped */
        ASSERT_EQ((int)c.flags, 0);
    }
    RunCmdCapture u;
    memset(&u, 0, sizeof u);
    runcmd_set_alloc_for_test(plan_failing_alloc);
    g_plan_alloc_calls = 0;
    g_plan_alloc_fail_at = 1;
    runcmd_cut(text, 100, 4096, &u);               /* the uncut path */
    runcmd_set_alloc_for_test(NULL);
    ASSERT_NULL(u.head);
    ASSERT_EQ((int)u.omitted_bytes, 100);
    free(text);
    TEST_END();
}

/* Feed text to a terminal. */
static void rc_feed(Terminal *t, const char *s)
{
    term_process(t, s, strlen(s));
}

static void cap_free(RunCmdCapture *c)
{
    free(c->head);
    free(c->tail);
    c->head = c->tail = NULL;
}

int test_runcmd_capture_normal(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    ASSERT_EQ(c.started, 1);
    ASSERT_EQ((int)c.start_line, 0);
    ASSERT_EQ(c.start_alt, 0);
    rc_feed(t, "ls\r\nout1\r\nout2\r\n$ ");
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 1);
    ASSERT_EQ(c.captured, 1);
    ASSERT_NOT_NULL(c.head);
    ASSERT_STR_EQ(c.head, "out1\nout2");
    ASSERT_EQ((int)c.head_len, 9);
    ASSERT_NULL(c.tail);
    ASSERT_EQ((int)c.omitted_bytes, 0);
    ASSERT_EQ((int)c.flags, 0);
    cap_free(&c);
    term_free(t);
    TEST_END();
}

int test_runcmd_capture_once(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    rc_feed(t, "ls\r\nout1\r\n$ ");
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 1);
    char *first = c.head;
    ASSERT_STR_EQ(first, "out1");
    rc_feed(t, "typed\r\nmore output\r\n$ ");     /* keystrokes move the cursor */
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 0);
    ASSERT_TRUE(c.head == first);
    ASSERT_STR_EQ(c.head, "out1");
    ASSERT_EQ((int)c.flags, 0);
    cap_free(&c);
    term_free(t);
    TEST_END();
}

int test_runcmd_capture_reflowed(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    rc_feed(t, "ls\r\nout1\r\nout2\r\n$ ");
    ASSERT_TRUE(term_resize(t, 10, 30));
    ASSERT_TRUE(t->reflow_seq != c.start_reflow_seq);
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 1);
    ASSERT_EQ((int)c.flags, (int)RUNCMD_CAP_SCREEN);
    size_t want_len = 0;
    char *want = term_extract_last_n_dup(t, t->rows, &want_len);
    ASSERT_NOT_NULL(want);
    ASSERT_NOT_NULL(c.head);
    ASSERT_STR_EQ(c.head, want);                   /* the screen rows, echo kept */
    ASSERT_TRUE(strstr(c.head, "out1") != NULL);
    free(want);
    cap_free(&c);
    term_free(t);
    TEST_END();
}

int test_runcmd_capture_alt_round_trip(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    rc_feed(t, "less f\r\nout1\r\n");
    rc_feed(t, "\x1b[?1049h");
    ASSERT_TRUE(t->alt_screen_active);
    rc_feed(t, "pager text");
    rc_feed(t, "\x1b[?1049l");
    ASSERT_TRUE(!t->alt_screen_active);
    rc_feed(t, "out2\r\n$ ");
    ASSERT_EQ(runcmd_capture_take(&c, t, "less f", 4096), 1);
    ASSERT_EQ((int)c.flags, 0);                    /* the primary range, no flag */
    ASSERT_NOT_NULL(c.head);
    ASSERT_STR_EQ(c.head, "out1\nout2");
    cap_free(&c);
    term_free(t);
    TEST_END();
}

int test_runcmd_capture_inside_alt(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "\x1b[?1049h");                     /* tmux: already on the alternate screen */
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    ASSERT_EQ(c.start_alt, 1);
    rc_feed(t, "ls\r\nout\r\n$ ");
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 1);
    ASSERT_EQ((int)c.flags, (int)RUNCMD_CAP_SCREEN);
    ASSERT_NOT_NULL(c.head);
    ASSERT_TRUE(strstr(c.head, "out") != NULL);
    ASSERT_TRUE(strstr(c.head, "$ ls") != NULL);   /* the visible rows as they are */
    cap_free(&c);

    /* and the alternate screen turned on during the command */
    RunCmdCapture d;
    memset(&d, 0, sizeof d);
    Terminal *u = term_init(10, 40, 100);
    rc_feed(u, "$ ");
    runcmd_capture_start(&d, u);
    ASSERT_EQ(d.start_alt, 0);
    rc_feed(u, "vi\r\n\x1b[?1049hfile text");
    ASSERT_EQ(runcmd_capture_take(&d, u, "vi", 4096), 1);
    ASSERT_EQ((int)d.flags, (int)RUNCMD_CAP_SCREEN);
    ASSERT_TRUE(d.head && strstr(d.head, "file text") != NULL);
    cap_free(&d);
    term_free(u);
    term_free(t);
    TEST_END();
}

int test_runcmd_capture_region_scroll(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(6, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    rc_feed(t, "\x1b[1;3r\x1b[3;1H");              /* DECSTBM rows 1-3, cursor to row 3 */
    rc_feed(t, "a\r\nb\r\nc\r\nd\r\n");            /* scrolls the region in place */
    ASSERT_TRUE(t->region_scroll_seq != c.start_region_seq);
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 1);
    ASSERT_EQ((int)c.flags, (int)RUNCMD_CAP_SCREEN);
    ASSERT_NOT_NULL(c.head);
    ASSERT_TRUE(strstr(c.head, "d") != NULL);
    cap_free(&c);
    term_free(t);
    TEST_END();
}

int test_runcmd_capture_cursor_up(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(8, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "L0\r\nL1\r\n$ ");
    runcmd_capture_start(&c, t);
    ASSERT_EQ((int)c.start_line, 2);
    rc_feed(t, "clear; ls\r\n\x1b[2J\x1b[Hout");   /* clear, then the listing from row 0 */
    ASSERT_TRUE(term_abs_cursor_line(t) <= c.start_line);   /* end <= start */
    ASSERT_EQ(runcmd_capture_take(&c, t, "clear; ls", 4096), 1);
    ASSERT_EQ((int)c.flags, (int)(RUNCMD_CAP_SCREEN | RUNCMD_CAP_REDRAWN));
    ASSERT_NOT_NULL(c.head);
    ASSERT_TRUE(strstr(c.head, "out") != NULL);
    cap_free(&c);
    term_free(t);

    /* the same on a blank screen: SCREEN and EMPTY, no REDRAWN (nothing there) */
    Terminal *u = term_init(8, 40, 100);
    RunCmdCapture d;
    memset(&d, 0, sizeof d);
    rc_feed(u, "L0\r\nL1\r\n$ ");
    runcmd_capture_start(&d, u);
    rc_feed(u, "\x1b[2J\x1b[H");
    ASSERT_EQ(runcmd_capture_take(&d, u, "cls", 4096), 1);
    ASSERT_EQ((int)d.flags, (int)(RUNCMD_CAP_SCREEN | RUNCMD_CAP_EMPTY));
    ASSERT_NULL(d.head);
    cap_free(&d);
    term_free(u);
    TEST_END();
}

int test_runcmd_capture_scrolled_out(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 40, 50);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    rc_feed(t, "seq 200\r\n");
    char b[32];
    for (int i = 0; i < 200; i++) {
        int k = snprintf(b, sizeof b, "o%d\r\n", i);
        term_process(t, b, (size_t)k);
    }
    rc_feed(t, "$ ");
    ASSERT_TRUE(t->lines_recycled > 0);
    ASSERT_EQ(runcmd_capture_take(&c, t, "seq 200", 4096), 1);
    ASSERT_EQ((int)c.flags, (int)RUNCMD_CAP_SCROLLED_OUT);
    ASSERT_NOT_NULL(c.head);
    ASSERT_TRUE(strstr(c.head, "o199") != NULL);
    ASSERT_TRUE(strstr(c.head, "o0\n") == NULL);   /* the start is gone */
    cap_free(&c);
    term_free(t);
    TEST_END();
}

int test_runcmd_capture_empty(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    rc_feed(t, "true\r\n$ ");                      /* the echo, no output */
    ASSERT_EQ(runcmd_capture_take(&c, t, "true", 4096), 1);
    ASSERT_EQ((int)c.flags, (int)RUNCMD_CAP_EMPTY);
    ASSERT_NULL(c.head);
    ASSERT_NULL(c.tail);
    ASSERT_EQ((int)c.omitted_bytes, 0);
    term_free(t);

    /* blank rows after the echo are trimmed: still EMPTY */
    Terminal *u = term_init(10, 40, 100);
    RunCmdCapture d;
    memset(&d, 0, sizeof d);
    rc_feed(u, "$ ");
    runcmd_capture_start(&d, u);
    rc_feed(u, "echo\r\n\r\n\r\n$ ");
    ASSERT_EQ(runcmd_capture_take(&d, u, "echo", 4096), 1);
    ASSERT_EQ((int)d.flags, (int)RUNCMD_CAP_EMPTY);
    ASSERT_NULL(d.head);
    term_free(u);
    TEST_END();
}

/* The same session with the view scrolled back at start and take. */
static char *scrolled_session(int offset)
{
    Terminal *t = term_init(5, 40, 50);
    char b[16];
    for (int i = 0; i < 12; i++) {
        int k = snprintf(b, sizeof b, "L%d\r\n", i);
        term_process(t, b, (size_t)k);
    }
    rc_feed(t, "$ ");
    t->scrollback_offset = offset;
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    runcmd_capture_start(&c, t);
    rc_feed(t, "ls\r\nout1\r\nout2\r\n$ ");
    t->scrollback_offset = offset;
    (void)runcmd_capture_take(&c, t, "ls", 4096);
    char *r = c.head ? strdup(c.head) : NULL;
    unsigned f = c.flags;
    cap_free(&c);
    term_free(t);
    if (f != 0u) { free(r); return NULL; }
    return r;
}

int test_runcmd_capture_scrolled_view(void)
{
    TEST_BEGIN();
    char *at0 = scrolled_session(0);
    char *at3 = scrolled_session(3);
    ASSERT_NOT_NULL(at0);
    ASSERT_NOT_NULL(at3);
    ASSERT_STR_EQ(at0, "out1\nout2");
    ASSERT_STR_EQ(at3, at0);
    free(at0);
    free(at3);
    TEST_END();
}

int test_runcmd_capture_not_started(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ls\r\nout\r\n$ ");
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 0);   /* before start */
    ASSERT_EQ(c.captured, 0);
    ASSERT_NULL(c.head);
    ASSERT_EQ(c.flags, 0u);

    runcmd_capture_start(&c, NULL);                         /* no terminal: not started */
    ASSERT_EQ(c.started, 0);
    runcmd_capture_start(NULL, t);                          /* NULL-safe */
    runcmd_capture_start(&c, t);
    ASSERT_EQ(c.started, 1);
    ASSERT_EQ(runcmd_capture_take(&c, NULL, "ls", 4096), 0);   /* no terminal */
    ASSERT_EQ(c.captured, 0);
    ASSERT_EQ(runcmd_capture_take(NULL, t, "ls", 4096), 0);
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 1);      /* and it still can */
    ASSERT_EQ(c.captured, 1);
    cap_free(&c);
    term_free(t);
    TEST_END();
}

/* A cell never yields a control byte, so the capture needs no strip of its
 * own: term_extract writes C0, DEL and C1 cells as spaces. */
int test_runcmd_capture_control_cells(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(6, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    rc_feed(t, "ls\r\nabcdefgh\r\n$ ");
    /* write control codepoints straight into the output row's cells */
    int logical = 1;
    TermRow *row = t->lines[(t->lines_start + logical) % t->lines_capacity];
    ASSERT_NOT_NULL(row);
    row->cells[1].codepoint = 0x01u;
    row->cells[2].codepoint = 0x1Bu;
    row->cells[3].codepoint = 0x7Fu;
    row->cells[4].codepoint = 0x85u;
    row->cells[5].codepoint = 0x9Fu;
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 1);
    ASSERT_NOT_NULL(c.head);
    ASSERT_STR_EQ(c.head, "a     gh");
    for (const unsigned char *p = (const unsigned char *)c.head; *p; p++)
        ASSERT_TRUE(*p == '\n' || (*p >= 0x20 && *p != 0x7F));
    cap_free(&c);
    term_free(t);
    TEST_END();
}

int test_runcmd_capture_cut_applied(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 200, 5000);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    rc_feed(t, "big\r\n");
    char b[32];
    size_t total = 0;
    for (int i = 0; i < 400; i++) {
        int k = snprintf(b, sizeof b, "line %04d\r\n", i);
        term_process(t, b, (size_t)k);
        total += (size_t)k - 1;                    /* the \n between rows, the \r is not stored */
    }
    rc_feed(t, "$ ");
    ASSERT_EQ(runcmd_capture_take(&c, t, "big", 1000), 1);
    ASSERT_EQ((int)c.flags, 0);
    ASSERT_NOT_NULL(c.head);
    ASSERT_NOT_NULL(c.tail);
    ASSERT_EQ((int)c.head_len, 750);
    ASSERT_EQ((int)c.tail_len, 250);
    ASSERT_TRUE(c.omitted_bytes > 0);
    ASSERT_TRUE(strncmp(c.head, "line 0000\n", 10) == 0);
    ASSERT_TRUE(strstr(c.tail, "line 0399") != NULL);
    ASSERT_EQ((int)(c.head_len + c.tail_len + c.omitted_bytes), (int)(total - 1));
    cap_free(&c);
    term_free(t);
    TEST_END();
}

/* Spec 10: "less then q keeps the output before it". Scrolling the alternate
 * screen is not a region scroll of the primary ring. */
int test_runcmd_capture_alt_scroll_keeps_range(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "$ ");
    runcmd_capture_start(&c, t);
    rc_feed(t, "less f\r\nout1\r\n");
    rc_feed(t, "\x1b[?1049h");
    char b[16];
    for (int i = 0; i < 25; i++) {                 /* scrolls the alternate screen */
        int k = snprintf(b, sizeof b, "pg%d\r\n", i);
        term_process(t, b, (size_t)k);
    }
    rc_feed(t, "\x1b[?1049l");
    rc_feed(t, "out2\r\n$ ");
    ASSERT_EQ((int)t->region_scroll_seq, (int)c.start_region_seq);
    ASSERT_EQ(runcmd_capture_take(&c, t, "less f", 4096), 1);
    ASSERT_EQ((int)c.flags, 0);
    ASSERT_NOT_NULL(c.head);
    ASSERT_STR_EQ(c.head, "out1\nout2");
    cap_free(&c);
    term_free(t);
    TEST_END();
}

/* The alternate screen on at the start only: still SCREEN (spec 10). */
int test_runcmd_capture_alt_at_start_only(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 100);
    RunCmdCapture c;
    memset(&c, 0, sizeof c);
    rc_feed(t, "P1\r\n");
    rc_feed(t, "\x1b[?1049h$ ");
    runcmd_capture_start(&c, t);
    ASSERT_EQ(c.start_alt, 1);
    rc_feed(t, "ls\r\nout\r\n\x1b[?1049l");
    ASSERT_TRUE(!t->alt_screen_active);
    ASSERT_EQ(runcmd_capture_take(&c, t, "ls", 4096), 1);
    ASSERT_EQ((int)c.flags, (int)RUNCMD_CAP_SCREEN);
    ASSERT_TRUE(c.head && strstr(c.head, "P1") != NULL);
    cap_free(&c);
    term_free(t);
    TEST_END();
}

/* An allocation failure while taking is "dropped", never "no output". */
int test_runcmd_capture_take_oom(void)
{
    TEST_BEGIN();
    for (int at = 1; at <= 2; at++) {
        Terminal *t = term_init(10, 200, 5000);
        RunCmdCapture c;
        memset(&c, 0, sizeof c);
        rc_feed(t, "$ ");
        runcmd_capture_start(&c, t);
        rc_feed(t, "big\r\n");
        char b[32];
        for (int i = 0; i < 400; i++) {
            int k = snprintf(b, sizeof b, "line %04d\r\n", i);
            term_process(t, b, (size_t)k);
        }
        rc_feed(t, "$ ");
        runcmd_set_alloc_for_test(plan_failing_alloc);   /* the cut's head, then its tail */
        g_plan_alloc_calls = 0;
        g_plan_alloc_fail_at = at;
        ASSERT_EQ(runcmd_capture_take(&c, t, "big", 1000), 1);
        runcmd_set_alloc_for_test(NULL);
        ASSERT_EQ(c.captured, 1);
        ASSERT_NULL(c.head);
        ASSERT_NULL(c.tail);
        ASSERT_EQ((int)c.omitted_bytes, 3999);
        ASSERT_EQ((int)(c.flags & RUNCMD_CAP_EMPTY), 0);
        ASSERT_EQ((int)c.flags, 0);
        term_free(t);
    }
    TEST_END();
}

/* The take's OOM test for the screen reader rests on this: a one-row probe
 * writes a byte exactly when term_extract_last_n_dup finds content. */
int test_runcmd_capture_probe_matches_dup(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(6, 20, 50);
    char probe[8];
    size_t len = 0;
    char *s = term_extract_last_n_dup(t, t->rows, &len);        /* blank */
    ASSERT_NULL(s);
    ASSERT_EQ((int)term_extract_last_n(t, t->rows, probe, sizeof probe), 0);
    rc_feed(t, "\xF0\x9F\x98\x80");                             /* one 4-byte glyph */
    s = term_extract_last_n_dup(t, t->rows, &len);
    ASSERT_NOT_NULL(s);
    ASSERT_TRUE(term_extract_last_n(t, t->rows, probe, sizeof probe) > 0);
    free(s);
    rc_feed(t, "\r\n\r\n \x1b[1;31m\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC");
    s = term_extract_last_n_dup(t, t->rows, &len);
    ASSERT_NOT_NULL(s);
    ASSERT_TRUE(term_extract_last_n(t, t->rows, probe, sizeof probe) > 0);
    free(s);
    rc_feed(t, "\x1b[2J");                                      /* cleared: blank again */
    s = term_extract_last_n_dup(t, t->rows, &len);
    ASSERT_EQ(s == NULL, term_extract_last_n(t, t->rows, probe, sizeof probe) == 0);
    free(s);
    term_free(t);
    TEST_END();
}
