/* tests/test_ai_debug_tool.c -- the AI Assist tool `read_debug_log`: its
 * registration (only while debugging is on), the per-stream context and its
 * rebinding, execution on the worker (call limit, bad arguments, a real read),
 * the chat-row helpers, and the system-prompt note. The query engine itself is
 * tested in test_dlog_query.c / test_dlog_live.c. */
#include "test_framework.h"
#include "fake_dlog.h"
#include "ai_tools.h"
#include "ai_stream.h"
#include "ai_prompt.h"
#include "dlog.h"
#include "dlog_query.h"
#include "json_validate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int begin(void)
{
    fk_reset();
    return fk_start(0, 0);
}

static void end(void)
{
    dlog_shutdown();
    fk_free();
}

static void apply(int enabled)
{
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = enabled;
    for (int i = 0; i < DLOG_CAT_COUNT; i++) c.levels[i] = DLOG_LVL_INFO;
    c.recorder = 1;
    c.recorder_level = DLOG_LVL_TRACE;
    dlog_apply(&c);
}

static int has(const char *h, const char *n) { return strstr(h, n) != NULL; }

/* Run the tool once; returns its rc and the text (caller frees *out). */
static int run_tool(DebugLogToolContext *ctx, const char *json, char **out)
{
    size_t len = 0;
    int trunc = 0;
    *out = NULL;
    return ai_debug_tool_execute(json, ctx, NULL, out, &len, &trunc);
}

/* ---- registration ------------------------------------------------------ */

int test_ai_debug_tool_register_fills_the_definition(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    DebugLogToolContext ctx;
    ai_tools_init(&reg);
    ai_debug_tool_ctx_init(&ctx);
    ASSERT_EQ(ai_debug_tool_register(&reg, &ctx), 0);
    ASSERT_EQ(reg.count, 1);
    const AiToolDef *t = ai_tools_find(&reg, "read_debug_log");
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_EQ((int)t->safety, (int)TOOL_SAFE);
        ASSERT_TRUE(t->execute == ai_debug_tool_execute);
        ASSERT_TRUE(t->tool_data == &ctx);
        /* Neither field was cut by the fixed-size buffers. */
        ASSERT_STR_EQ(t->description, dlog_tool_description());
        ASSERT_STR_EQ(t->input_schema_json, dlog_tool_schema());
        char err[128] = "";
        ASSERT_TRUE(json_validate(t->input_schema_json, strlen(t->input_schema_json),
                                  err, sizeof(err)));
    }
    ASSERT_EQ(ai_tools_validate_schemas(&reg), 0);
    TEST_END();
}

int test_ai_debug_tool_registered_only_while_debugging(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    DebugLogToolContext ctx;
    ai_debug_tool_ctx_init(&ctx);

    /* Logger not started: debugging is off. */
    ai_tools_init(&reg);
    ASSERT_EQ(ai_debug_tool_register_if_enabled(&reg, &ctx), 0);
    ASSERT_EQ(reg.count, 0);

    ASSERT_TRUE(begin());
    apply(0);
    ai_tools_init(&reg);
    ASSERT_EQ(ai_debug_tool_register_if_enabled(&reg, &ctx), 0);
    ASSERT_EQ(reg.count, 0);
    ASSERT_NULL(ai_tools_find(&reg, "read_debug_log"));

    apply(1);
    ai_tools_init(&reg);
    ASSERT_EQ(ai_debug_tool_register_if_enabled(&reg, &ctx), 1);
    ASSERT_EQ(reg.count, 1);
    ASSERT_NOT_NULL(ai_tools_find(&reg, "read_debug_log"));

    apply(0);                     /* switched off again before the next launch */
    ai_tools_init(&reg);
    ASSERT_EQ(ai_debug_tool_register_if_enabled(&reg, &ctx), 0);
    ASSERT_EQ(reg.count, 0);
    end();
    TEST_END();
}

static int dummy_exec(const char *in, void *data, volatile int *cancel,
                      char **out, size_t *out_len, int *trunc)
{
    (void)in; (void)data; (void)cancel; (void)out; (void)out_len; (void)trunc;
    return 0;
}

int test_ai_debug_tool_registration_keeps_other_tools(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    DebugLogToolContext ctx;
    ai_tools_init(&reg);
    ai_debug_tool_ctx_init(&ctx);
    AiToolDef other;
    memset(&other, 0, sizeof(other));
    strcpy(other.name, "web_search");
    strcpy(other.input_schema_json, "{\"type\":\"object\"}");
    other.execute = dummy_exec;
    ASSERT_EQ(ai_tools_register(&reg, &other), 0);
    ASSERT_EQ(ai_debug_tool_register(&reg, &ctx), 0);
    ASSERT_EQ(reg.count, 2);
    ASSERT_NOT_NULL(ai_tools_find(&reg, "web_search"));
    ASSERT_NOT_NULL(ai_tools_find(&reg, "read_debug_log"));
    TEST_END();
}

/* ---- the stream's own copy ---------------------------------------------- */

int test_ai_debug_tool_stream_copy_rebinds_context(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    DebugLogToolContext ctx;
    ai_tools_init(&reg);
    ai_debug_tool_ctx_init(&ctx);
    ASSERT_EQ(ai_debug_tool_register(&reg, &ctx), 0);

    AiStream *s = ai_stream_new();
    ASSERT_NOT_NULL(s);
    if (!s) { TEST_END(); }
    ai_stream_copy_tools(s, &reg, 3u, NULL, NULL, &ctx);
    ASSERT_EQ(s->tools.count, 1);
    ASSERT_TRUE(s->tools.tools[0].tool_data == &s->debug_log);
    ASSERT_EQ(s->debug_log.calls, 0);

    /* The stream counts on its own copy; the panel's context is untouched,
     * so the next message (a new stream) starts again from zero. */
    char *out = NULL;
    (void)run_tool((DebugLogToolContext *)s->tools.tools[0].tool_data, "{\"around_issue\":\"x\"}", &out);
    free(out);
    ASSERT_EQ(s->debug_log.calls, 1);
    ASSERT_EQ(ctx.calls, 0);

    /* Without a context the pointer is left alone. */
    AiStream *s2 = ai_stream_new();
    if (s2) {
        ai_stream_copy_tools(s2, &reg, 3u, NULL, NULL, NULL);
        ASSERT_TRUE(s2->tools.tools[0].tool_data == &ctx);
        ai_stream_discard(s2);
    }
    ai_stream_discard(s);
    TEST_END();
}

/* ---- execution ---------------------------------------------------------- */

int test_ai_debug_tool_execute_reads_the_log(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1);
    DLOG(DLOG_RENDER, DLOG_INFO, 2, "paint", "rows=%d", 24);
    DebugLogToolContext ctx;
    ai_debug_tool_ctx_init(&ctx);
    char *out = NULL;
    int rc = run_tool(&ctx, "{\"category\":\"render\"}", &out);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(out);
    if (out) {
        ASSERT_TRUE(out[0] == '#');
        ASSERT_TRUE(has(out, "paint rows=24"));
        free(out);
    }
    ASSERT_EQ(ctx.calls, 1);
    end();
    TEST_END();
}

int test_ai_debug_tool_execute_empty_and_missing_input_use_defaults(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1);
    DebugLogToolContext ctx;
    ai_debug_tool_ctx_init(&ctx);
    char *out = NULL;
    ASSERT_EQ(run_tool(&ctx, "{}", &out), 0);
    ASSERT_TRUE(out && out[0] == '#');
    free(out);
    ASSERT_EQ(run_tool(&ctx, "", &out), 0);
    ASSERT_TRUE(out && out[0] == '#');
    free(out);
    ASSERT_EQ(run_tool(&ctx, NULL, &out), 0);
    ASSERT_TRUE(out && out[0] == '#');
    free(out);
    ASSERT_EQ(ctx.calls, 3);
    end();
    TEST_END();
}

int test_ai_debug_tool_execute_invalid_input_is_a_short_error(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1);
    DebugLogToolContext ctx;
    ai_debug_tool_ctx_init(&ctx);
    const char *bad[] = {
        "{not json",
        "[1,2]",
        "{\"since_minutes\":\"soon\"}",
        "{\"category\":\"video\"}",
        "{\"min_level\":7}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char *out = NULL;
        int rc = run_tool(&ctx, bad[i], &out);
        ASSERT_TRUE(rc != 0);
        ASSERT_NOT_NULL(out);
        if (out) {
            ASSERT_TRUE(strlen(out) > 0 && strlen(out) < 300);
            ASSERT_TRUE(out[0] != '#');            /* not a log read */
            free(out);
        }
    }
    end();
    TEST_END();
}

int test_ai_debug_tool_fifth_call_hits_the_limit(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1);
    DebugLogToolContext ctx;
    ai_debug_tool_ctx_init(&ctx);
    for (int i = 0; i < DLOG_TOOL_MAX_CALLS_PER_MSG; i++) {
        char *out = NULL;
        ASSERT_EQ(run_tool(&ctx, "{\"summary\":true}", &out), 0);
        ASSERT_TRUE(out && out[0] == '#');
        free(out);
    }
    char *out = NULL;
    int rc = run_tool(&ctx, "{\"summary\":true}", &out);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(out);
    if (out) {
        ASSERT_STR_EQ(out, "call limit reached for this message");
        free(out);
    }
    /* It stays refused, and the counter does not run away. */
    out = NULL;
    ASSERT_TRUE(run_tool(&ctx, "{}", &out) != 0);
    free(out);
    ASSERT_TRUE(ctx.calls <= DLOG_TOOL_MAX_CALLS_PER_MSG + 2);

    /* A fresh context (the next user message) can read again. */
    DebugLogToolContext next;
    ai_debug_tool_ctx_init(&next);
    out = NULL;
    ASSERT_EQ(run_tool(&next, "{}", &out), 0);
    free(out);
    end();
    TEST_END();
}

int test_ai_debug_tool_bad_calls_count_toward_the_limit(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1);
    DebugLogToolContext ctx;
    ai_debug_tool_ctx_init(&ctx);
    for (int i = 0; i < DLOG_TOOL_MAX_CALLS_PER_MSG; i++) {
        char *out = NULL;
        (void)run_tool(&ctx, "{not json", &out);
        free(out);
    }
    char *out = NULL;
    ASSERT_TRUE(run_tool(&ctx, "{}", &out) != 0);
    ASSERT_TRUE(out && has(out, "call limit reached"));
    free(out);
    end();
    TEST_END();
}

int test_ai_debug_tool_execute_null_arguments_are_safe(void)
{
    TEST_BEGIN();
    size_t len = 0;
    int trunc = 0;
    char *out = NULL;
    DebugLogToolContext ctx;
    ai_debug_tool_ctx_init(&ctx);
    ASSERT_TRUE(ai_debug_tool_execute("{}", &ctx, NULL, NULL, &len, &trunc) != 0);
    ASSERT_TRUE(ai_debug_tool_execute("{}", &ctx, NULL, &out, NULL, &trunc) != 0);
    ASSERT_TRUE(ai_debug_tool_execute("{}", &ctx, NULL, &out, &len, NULL) != 0);
    /* No context at all: a short error, never a crash. */
    ASSERT_TRUE(ai_debug_tool_execute("{}", NULL, NULL, &out, &len, &trunc) != 0);
    ASSERT_NOT_NULL(out);
    free(out);
    TEST_END();
}

int test_ai_debug_tool_cancelled_before_reading(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1);
    DebugLogToolContext ctx;
    ai_debug_tool_ctx_init(&ctx);
    volatile int cancel = 1;
    char *out = NULL;
    size_t len = 0;
    int trunc = 0;
    int rc = ai_debug_tool_execute("{}", &ctx, &cancel, &out, &len, &trunc);
    ASSERT_TRUE(rc != 0);
    ASSERT_NOT_NULL(out);
    free(out);
    end();
    TEST_END();
}

int test_ai_debug_tool_runs_through_ai_tool_execute(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1);
    AiToolRegistry reg;
    DebugLogToolContext ctx;
    ai_tools_init(&reg);
    ai_debug_tool_ctx_init(&ctx);
    ASSERT_EQ(ai_debug_tool_register_if_enabled(&reg, &ctx), 1);
    AiToolCall call;
    memset(&call, 0, sizeof(call));
    strcpy(call.id, "toolu_1");
    strcpy(call.name, "read_debug_log");
    strcpy(call.input_json, "{\"summary\":true}");
    AiToolResult res;
    ASSERT_EQ(ai_tool_execute(&reg, &call, NULL, &res), 0);
    ASSERT_EQ(res.is_error, 0);
    ASSERT_TRUE(res.content && res.content[0] == '#');
    free(res.content);
    end();
    TEST_END();
}

/* ---- chat rows ------------------------------------------------------------ */

int test_ai_debug_tool_describe_for_the_using_tool_row(void)
{
    TEST_BEGIN();
    char d[200];
    ASSERT_EQ(ai_debug_tool_describe("{}", d, sizeof(d)), 1);
    ASSERT_STR_EQ(d, "last 30 min");
    ASSERT_EQ(ai_debug_tool_describe("", d, sizeof(d)), 1);
    ASSERT_STR_EQ(d, "last 30 min");
    ASSERT_EQ(ai_debug_tool_describe(NULL, d, sizeof(d)), 1);
    ASSERT_STR_EQ(d, "last 30 min");
    ASSERT_EQ(ai_debug_tool_describe("{\"category\":\"render\",\"min_level\":\"debug\",\"around_issue\":3}",
                                     d, sizeof(d)), 1);
    ASSERT_STR_EQ(d, "render, min debug, around issue 3");
    ASSERT_EQ(ai_debug_tool_describe("{\"summary\":true,\"since_minutes\":90}", d, sizeof(d)), 1);
    ASSERT_STR_EQ(d, "summary, last 90 min");
    /* A call the engine would refuse still gets a row; it says so. */
    ASSERT_EQ(ai_debug_tool_describe("{not json", d, sizeof(d)), 0);
    ASSERT_TRUE(strlen(d) > 0);
    /* Small buffers are cut, never overrun. */
    char tiny[8];
    ASSERT_EQ(ai_debug_tool_describe("{\"category\":\"render\",\"around_issue\":3}", tiny, sizeof(tiny)), 1);
    ASSERT_TRUE(strlen(tiny) < sizeof(tiny));
    ASSERT_EQ(ai_debug_tool_describe("{}", NULL, 0), 0);   /* must not crash */
    TEST_END();
}

int test_ai_debug_tool_summary_line_is_the_first_header(void)
{
    TEST_BEGIN();
    char s[128];
    const char *res = "# 2341 matching lines, showing last 200; files: a.log\n"
                      "13:35:10 INFO render s1 ui paint rows=24\n";
    ai_debug_tool_summary_line(res, strlen(res), s, sizeof(s));
    ASSERT_STR_EQ(s, "# 2341 matching lines, showing last 200; files: a.log");

    /* CRLF and a header with no line break. */
    ai_debug_tool_summary_line("# short\r\nrest", 13, s, sizeof(s));
    ASSERT_STR_EQ(s, "# short");
    ai_debug_tool_summary_line("# only", 6, s, sizeof(s));
    ASSERT_STR_EQ(s, "# only");

    /* No header: fall back to the first line, never the log lines wholesale. */
    ai_debug_tool_summary_line("plain text\nmore", 15, s, sizeof(s));
    ASSERT_STR_EQ(s, "plain text");

    /* Empty and NULL. */
    ai_debug_tool_summary_line("", 0, s, sizeof(s));
    ASSERT_STR_EQ(s, "");
    ai_debug_tool_summary_line(NULL, 0, s, sizeof(s));
    ASSERT_STR_EQ(s, "");

    /* Cut to the buffer. */
    char small[10];
    ai_debug_tool_summary_line(res, strlen(res), small, sizeof(small));
    ASSERT_EQ(strlen(small), sizeof(small) - 1);
    ASSERT_TRUE(strncmp(small, "# 2341 ma", 9) == 0);
    TEST_END();
}

int test_ai_debug_tool_summary_line_of_a_real_result(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1);
    DebugLogToolContext ctx;
    ai_debug_tool_ctx_init(&ctx);
    char *out = NULL;
    ASSERT_EQ(run_tool(&ctx, "{\"summary\":true}", &out), 0);
    ASSERT_NOT_NULL(out);
    if (out) {
        char s[200];
        ai_debug_tool_summary_line(out, strlen(out), s, sizeof(s));
        ASSERT_TRUE(s[0] == '#');
        ASSERT_FALSE(has(s, "\n"));
        free(out);
    }
    end();
    TEST_END();
}

/* ---- the system prompt -------------------------------------------------------- */

int test_ai_debug_tool_prompt_note_text(void)
{
    TEST_BEGIN();
    const char *n = ai_debug_tool_prompt_note();
    ASSERT_NOT_NULL(n);
    ASSERT_TRUE(has(n, "read_debug_log"));
    ASSERT_TRUE(has(n, "glitch"));
    ASSERT_TRUE(has(n, "hang"));
    ASSERT_TRUE(has(n, "marked an issue"));
    ASSERT_TRUE(has(n, "at most"));
    ASSERT_TRUE(strlen(n) < 600);
    TEST_END();
}

int test_ai_debug_tool_system_prompt_carries_the_note_only_when_asked(void)
{
    TEST_BEGIN();
    char base[8192], with[8192], off[8192];
    ai_build_system_prompt(base, sizeof(base), "$ ls", NULL, NULL, SESSION_SSH, NULL);
    ai_build_system_prompt_ex(off, sizeof(off), "$ ls", NULL, NULL, SESSION_SSH, NULL, 0);
    ai_build_system_prompt_ex(with, sizeof(with), "$ ls", NULL, NULL, SESSION_SSH, NULL, 1);
    ASSERT_STR_EQ(base, off);                                /* byte for byte */
    ASSERT_FALSE(has(off, "read_debug_log"));
    ASSERT_TRUE(has(with, ai_debug_tool_prompt_note()));
    ASSERT_TRUE(strlen(with) > strlen(off));
    /* The terminal block is still last, the note sits before it. */
    const char *note = strstr(with, "read_debug_log");
    const char *term = strstr(with, "Current terminal output:");
    ASSERT_TRUE(note && term && note < term);

    char local[8192];
    ai_build_system_prompt_ex(local, sizeof(local), NULL, NULL, NULL, SESSION_LOCAL, "PowerShell", 1);
    ASSERT_TRUE(has(local, ai_debug_tool_prompt_note()));
    ASSERT_TRUE(has(local, "Write PowerShell syntax"));

    /* A tiny buffer is cut, never overrun. */
    char tiny[64];
    ai_build_system_prompt_ex(tiny, sizeof(tiny), "$ ls", NULL, NULL, SESSION_SSH, NULL, 1);
    ASSERT_TRUE(strlen(tiny) < sizeof(tiny));
    TEST_END();
}
