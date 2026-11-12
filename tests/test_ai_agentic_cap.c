#include "test_framework.h"
#include "ai_agentic.h"
#include "json_validate.h"
#include <stdlib.h>
#include <string.h>

/* The JSON-escaped size of a buffer, by json_escape_string itself. */
static size_t escaped_size(const char *s, size_t len)
{
    size_t cap = len * 6 + 16;
    char *tmp = (char *)malloc(cap);
    if (!tmp) return (size_t)-1;
    size_t end = len ? json_escape_string(s, len, tmp, cap, 0, 0) : 0;
    free(tmp);
    return end;
}

static void result_init(AiToolResult *r, const char *fill, size_t n)
{
    memset(r, 0, sizeof(*r));
    r->content = (char *)malloc(n + 1);
    if (!r->content) return;
    memcpy(r->content, fill, n);
    r->content[n] = '\0';
    r->content_len = n;
}

static char *repeat_byte(char c, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memset(p, c, n);
    p[n] = '\0';
    return p;
}

int test_cap_all_quotes(void)
{
    TEST_BEGIN();
    char *q = repeat_byte('"', 70000);
    ASSERT_NOT_NULL(q);
    AiToolResult r;
    result_init(&r, q, 70000);
    ASSERT_EQ(agentic_cap_tool_result(&r), 1);
    size_t esc = escaped_size(r.content, r.content_len);
    ASSERT_TRUE(esc <= AI_TOOL_RESULT_ESCAPED_MAX);
    ASSERT_TRUE(esc > AI_TOOL_RESULT_ESCAPED_MAX - 2);
    ASSERT_EQ(strlen(r.content), r.content_len);
    ASSERT_STR_EQ(r.tool_note, AI_TOOL_TRUNC_NOTE);
    free(r.content);
    free(q);
    TEST_END();
}

int test_cap_control_bytes_stripped(void)
{
    TEST_BEGIN();
    char *c = repeat_byte('\x01', 100000);
    ASSERT_NOT_NULL(c);
    AiToolResult r;
    result_init(&r, c, 100000);
    ASSERT_EQ(agentic_cap_tool_result(&r), 0);
    ASSERT_EQ(r.content_len, 0);
    ASSERT_STR_EQ(r.tool_note, "");
    free(r.content);
    free(c);

    const char *mixed = "a\tb\nc\rd\x7f";
    result_init(&r, mixed, strlen(mixed));
    ASSERT_EQ(agentic_cap_tool_result(&r), 0);
    ASSERT_STR_EQ(r.content, "a\tb\ncd");
    ASSERT_EQ(r.content_len, 6);
    free(r.content);
    TEST_END();
}

int test_cap_utf8_boundary(void)
{
    TEST_BEGIN();
    size_t n = 65535;
    char *buf = (char *)malloc(n + 4);
    ASSERT_NOT_NULL(buf);
    memset(buf, 'a', n);
    memcpy(buf + n, "\xE2\x82\xAC", 3);
    AiToolResult r;
    result_init(&r, buf, n + 3);
    ASSERT_EQ(agentic_cap_tool_result(&r), 1);
    ASSERT_EQ(r.content_len, 65535);
    ASSERT_EQ(r.content[r.content_len - 1], 'a');
    ASSERT_EQ(r.content[r.content_len], '\0');
    free(r.content);
    free(buf);
    TEST_END();
}

int test_cap_under_limit_untouched(void)
{
    TEST_BEGIN();
    AiToolResult r;
    result_init(&r, "hello", 5);
    ASSERT_EQ(agentic_cap_tool_result(&r), 0);
    ASSERT_STR_EQ(r.content, "hello");
    ASSERT_EQ(r.content_len, 5);
    ASSERT_STR_EQ(r.tool_note, "");
    free(r.content);
    TEST_END();
}

int test_cap_null_content_is_noop(void)
{
    TEST_BEGIN();
    AiToolResult r;
    memset(&r, 0, sizeof(r));
    snprintf(r.tool_note, sizeof(r.tool_note), "kept");
    ASSERT_EQ(agentic_cap_tool_result(&r), 0);
    ASSERT_NULL(r.content);
    ASSERT_STR_EQ(r.tool_note, "kept");
    ASSERT_EQ(agentic_cap_tool_result(NULL), 0);
    TEST_END();
}

int test_cap_appends_to_existing_note(void)
{
    TEST_BEGIN();
    char *a = repeat_byte('a', 70000);
    ASSERT_NOT_NULL(a);
    AiToolResult r;
    result_init(&r, a, 70000);
    snprintf(r.tool_note, sizeof(r.tool_note), "N");
    ASSERT_EQ(agentic_cap_tool_result(&r), 1);
    ASSERT_STR_EQ(r.tool_note, "N\n" AI_TOOL_TRUNC_NOTE);
    ASSERT_EQ(r.content_len, AI_TOOL_RESULT_ESCAPED_MAX);
    free(r.content);
    free(a);
    TEST_END();
}

int test_cap_exact_limit_not_cut(void)
{
    TEST_BEGIN();
    char *a = repeat_byte('a', AI_TOOL_RESULT_ESCAPED_MAX);
    ASSERT_NOT_NULL(a);
    AiToolResult r;
    result_init(&r, a, AI_TOOL_RESULT_ESCAPED_MAX);
    ASSERT_EQ(agentic_cap_tool_result(&r), 0);
    ASSERT_EQ(r.content_len, AI_TOOL_RESULT_ESCAPED_MAX);
    ASSERT_STR_EQ(r.tool_note, "");
    free(r.content);
    free(a);
    TEST_END();
}

int test_cap_full_note_drops_marker_safely(void)
{
    TEST_BEGIN();
    char *a = repeat_byte('a', 70000);
    ASSERT_NOT_NULL(a);
    AiToolResult r;
    result_init(&r, a, 70000);
    memset(r.tool_note, 'n', sizeof(r.tool_note) - 1);
    r.tool_note[sizeof(r.tool_note) - 1] = '\0';
    ASSERT_EQ(agentic_cap_tool_result(&r), 1);
    ASSERT_EQ(strlen(r.tool_note), sizeof(r.tool_note) - 1);
    free(r.content);
    free(a);
    TEST_END();
}

int test_json_escaped_len_matches_escape_string(void)
{
    TEST_BEGIN();
    for (int c = 0; c < 256; c++) {
        char s = (char)c;
        char out[16];
        size_t end = json_escape_string(&s, 1, out, sizeof(out), 0, 0);
        ASSERT_EQ(json_escaped_byte_len((unsigned char)c), end);
    }
    TEST_END();
}

int test_add_tool_results_stores_note_and_caps(void)
{
    TEST_BEGIN();
    AiConversation conv;
    memset(&conv, 0, sizeof(conv));
    char *a = repeat_byte('a', 70000);
    ASSERT_NOT_NULL(a);
    AiToolResult r[2];
    result_init(&r[0], a, 70000);
    snprintf(r[0].tool_use_id, sizeof(r[0].tool_use_id), "toolu_1");
    memset(&r[1], 0, sizeof(r[1]));
    snprintf(r[1].tool_use_id, sizeof(r[1].tool_use_id), "toolu_2");
    snprintf(r[1].tool_note, sizeof(r[1].tool_note), "The user did not approve this tool call.");
    r[1].is_error = 1;
    AiToolCall calls[2];
    memset(calls, 0, sizeof(calls));
    snprintf(calls[0].name, sizeof(calls[0].name), "web_fetch");
    snprintf(calls[1].name, sizeof(calls[1].name), "web_fetch");

    ASSERT_EQ(agentic_add_tool_results(&conv, calls, r, 2), 0);
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(conv.messages[0].n_parts, 2);
    ASSERT_TRUE(conv.messages[0].parts[0].content_len <= AI_TOOL_RESULT_ESCAPED_MAX);
    ASSERT_STR_EQ(conv.messages[0].parts[0].tool_note, AI_TOOL_TRUNC_NOTE);
    ASSERT_STR_EQ(conv.messages[0].parts[1].tool_note, "The user did not approve this tool call.");
    ASSERT_EQ(conv.messages[0].parts[1].content_len, 0);
    /* the caller's originals are untouched */
    ASSERT_EQ(r[0].content_len, 70000);
    ASSERT_STR_EQ(r[0].tool_note, "");

    ai_conv_reset(&conv);
    free(r[0].content);
    free(a);
    TEST_END();
}
