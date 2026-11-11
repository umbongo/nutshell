#include "test_framework.h"
#include "ai_tools.h"
#include "ai_agentic.h"
#include "ai_prompt.h"
#include "chat_approval.h"
#include "json_parser.h"
#include "run_command.h"
#include <string.h>
#include <stdlib.h>

/* ---- Mock callbacks ------------------------------------------------------ */

static int mock_execute(const char *input_json, void *tool_data,
                        volatile int *cancel_flag,
                        char **result_buf, size_t *result_len, int *was_truncated)
{
    (void)cancel_flag;
    (void)tool_data;
    (void)input_json;
    const char *result = "mock result";
    *result_buf = malloc(strlen(result) + 1);
    if (!*result_buf) return -1;
    strcpy(*result_buf, result);
    *result_len = strlen(result);
    *was_truncated = 0;
    return 0;
}

static int mock_execute_error(const char *input_json, void *tool_data,
                               volatile int *cancel_flag,
                               char **result_buf, size_t *result_len,
                               int *was_truncated)
{
    (void)cancel_flag;
    (void)tool_data;
    (void)input_json;
    const char *result = "something went wrong";
    *result_buf = malloc(strlen(result) + 1);
    if (!*result_buf) return -1;
    strcpy(*result_buf, result);
    *result_len = strlen(result);
    *was_truncated = 0;
    return -1;
}

static int mock_execute_truncated(const char *input_json, void *tool_data,
                                   volatile int *cancel_flag,
                                   char **result_buf, size_t *result_len,
                                   int *was_truncated)
{
    (void)cancel_flag;
    (void)tool_data;
    (void)input_json;
    const char *result = "truncated result";
    *result_buf = malloc(strlen(result) + 1);
    if (!*result_buf) return -1;
    strcpy(*result_buf, result);
    *result_len = strlen(result);
    *was_truncated = 1;
    return 0;
}

/* Captures the tool_data pointer for verification */
static void *g_received_tool_data = NULL;
static int mock_execute_captures_data(const char *input_json, void *tool_data,
                                       volatile int *cancel_flag,
                                       char **result_buf, size_t *result_len,
                                       int *was_truncated)
{
    (void)cancel_flag;
    (void)input_json;
    g_received_tool_data = tool_data;
    const char *result = "ok";
    *result_buf = malloc(strlen(result) + 1);
    if (!*result_buf) return -1;
    strcpy(*result_buf, result);
    *result_len = strlen(result);
    *was_truncated = 0;
    return 0;
}

/* ---- Helper: build a minimal tool def ------------------------------------ */

static AiToolDef make_tool(const char *name, const char *desc,
                            const char *schema)
{
    AiToolDef t;
    memset(&t, 0, sizeof(t));
    snprintf(t.name,             sizeof(t.name),             "%s", name);
    snprintf(t.description,      sizeof(t.description),      "%s", desc);
    snprintf(t.input_schema_json, sizeof(t.input_schema_json), "%s", schema);
    t.safety  = TOOL_SAFE;
    t.execute = mock_execute;
    t.tool_data = NULL;
    return t;
}

static const char *SIMPLE_SCHEMA =
    "{\"type\":\"object\","
    "\"properties\":{\"query\":{\"type\":\"string\","
    "\"description\":\"The search query\"}},"
    "\"required\":[\"query\"]}";

/* ======================================================================== */
/* Registry tests                                                             */
/* ======================================================================== */

int test_ai_tools_init(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);
    ASSERT_EQ(reg.count, 0);
    TEST_END();
}

int test_ai_tools_register(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("web_search", "Search the web", SIMPLE_SCHEMA);
    int rc = ai_tools_register(&reg, &t);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(reg.count, 1);

    const AiToolDef *found = ai_tools_find(&reg, "web_search");
    ASSERT_NOT_NULL(found);
    ASSERT_STR_EQ(found->name, "web_search");
    TEST_END();
}

int test_ai_tools_register_max(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    /* Fill up to AI_TOOL_MAX */
    for (int i = 0; i < AI_TOOL_MAX; i++) {
        char name[AI_TOOL_NAME_MAX];
        snprintf(name, sizeof(name), "tool_%d", i);
        AiToolDef t = make_tool(name, "desc", SIMPLE_SCHEMA);
        int rc = ai_tools_register(&reg, &t);
        ASSERT_EQ(rc, 0);
    }
    ASSERT_EQ(reg.count, AI_TOOL_MAX);

    /* Next registration must fail */
    AiToolDef extra = make_tool("extra_tool", "desc", SIMPLE_SCHEMA);
    int rc = ai_tools_register(&reg, &extra);
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(reg.count, AI_TOOL_MAX);
    TEST_END();
}

int test_ai_tools_find_not_found(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    const AiToolDef *found = ai_tools_find(&reg, "no_such_tool");
    ASSERT_NULL(found);
    TEST_END();
}

int test_ai_tools_find_by_name(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t1 = make_tool("alpha", "First",  SIMPLE_SCHEMA);
    AiToolDef t2 = make_tool("beta",  "Second", SIMPLE_SCHEMA);
    ai_tools_register(&reg, &t1);
    ai_tools_register(&reg, &t2);

    const AiToolDef *fa = ai_tools_find(&reg, "alpha");
    const AiToolDef *fb = ai_tools_find(&reg, "beta");
    const AiToolDef *fn = ai_tools_find(&reg, "gamma");

    ASSERT_NOT_NULL(fa);
    ASSERT_STR_EQ(fa->name, "alpha");
    ASSERT_NOT_NULL(fb);
    ASSERT_STR_EQ(fb->name, "beta");
    ASSERT_NULL(fn);
    TEST_END();
}

/* ======================================================================== */
/* Anthropic serialization tests                                              */
/* ======================================================================== */

int test_ai_tools_serialize_anthropic_empty(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    char buf[256];
    int n = ai_tools_serialize_anthropic(&reg, buf, sizeof(buf));
    ASSERT_TRUE(n >= 0);
    ASSERT_STR_EQ(buf, "[]");
    TEST_END();
}

int test_ai_tools_serialize_anthropic_one(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("web_search", "Search the web", SIMPLE_SCHEMA);
    ai_tools_register(&reg, &t);

    char buf[4096];
    int n = ai_tools_serialize_anthropic(&reg, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);

    /* Verify the key structural elements are present */
    ASSERT_NOT_NULL(strstr(buf, "\"name\":\"web_search\""));
    ASSERT_NOT_NULL(strstr(buf, "\"description\":\"Search the web\""));
    ASSERT_NOT_NULL(strstr(buf, "\"input_schema\":"));
    /* Schema embedded verbatim */
    ASSERT_NOT_NULL(strstr(buf, "\"type\":\"object\""));
    ASSERT_NOT_NULL(strstr(buf, "\"query\""));
    TEST_END();
}

int test_ai_tools_serialize_anthropic_overflow(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("web_search", "Search the web", SIMPLE_SCHEMA);
    ai_tools_register(&reg, &t);

    char tiny[4];
    int n = ai_tools_serialize_anthropic(&reg, tiny, sizeof(tiny));
    ASSERT_EQ(n, -1);
    TEST_END();
}

/* ======================================================================== */
/* OpenAI serialization tests                                                 */
/* ======================================================================== */

int test_ai_tools_serialize_openai_empty(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    char buf[256];
    int n = ai_tools_serialize_openai(&reg, buf, sizeof(buf));
    ASSERT_TRUE(n >= 0);
    ASSERT_STR_EQ(buf, "[]");
    TEST_END();
}

int test_ai_tools_serialize_openai_one(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("web_search", "Search the web", SIMPLE_SCHEMA);
    ai_tools_register(&reg, &t);

    char buf[4096];
    int n = ai_tools_serialize_openai(&reg, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);

    /* Verify OpenAI function wrapper */
    ASSERT_NOT_NULL(strstr(buf, "\"type\":\"function\""));
    ASSERT_NOT_NULL(strstr(buf, "\"function\":"));
    ASSERT_NOT_NULL(strstr(buf, "\"name\":\"web_search\""));
    ASSERT_NOT_NULL(strstr(buf, "\"description\":\"Search the web\""));
    ASSERT_NOT_NULL(strstr(buf, "\"parameters\":"));
    TEST_END();
}

int test_ai_tools_serialize_openai_overflow(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("web_search", "Search the web", SIMPLE_SCHEMA);
    ai_tools_register(&reg, &t);

    char tiny[4];
    int n = ai_tools_serialize_openai(&reg, tiny, sizeof(tiny));
    ASSERT_EQ(n, -1);
    TEST_END();
}

/* ======================================================================== */
/* Anthropic parsing tests                                                    */
/* ======================================================================== */

int test_ai_parse_tool_calls_anthropic_basic(void)
{
    TEST_BEGIN();
    /* A minimal Anthropic response with one tool_use block */
    const char *resp =
        "{"
        "  \"id\": \"msg_01\","
        "  \"type\": \"message\","
        "  \"content\": ["
        "    {"
        "      \"type\": \"tool_use\","
        "      \"id\": \"toolu_01abc\","
        "      \"name\": \"web_search\","
        "      \"input\": {\"query\": \"nutshell ssh client\"}"
        "    }"
        "  ]"
        "}";

    AiToolCall calls[8];
    int n = ai_parse_tool_calls_anthropic(resp, calls, 8);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(calls[0].id,   "toolu_01abc");
    ASSERT_STR_EQ(calls[0].name, "web_search");
    /* input_json should contain "query" and "nutshell ssh client" */
    ASSERT_NOT_NULL(strstr(calls[0].input_json, "query"));
    ASSERT_NOT_NULL(strstr(calls[0].input_json, "nutshell ssh client"));
    TEST_END();
}

int test_ai_parse_tool_calls_anthropic_multiple(void)
{
    TEST_BEGIN();
    const char *resp =
        "{"
        "  \"content\": ["
        "    {\"type\": \"text\", \"text\": \"Let me search.\"},"
        "    {"
        "      \"type\": \"tool_use\","
        "      \"id\": \"toolu_01\","
        "      \"name\": \"web_search\","
        "      \"input\": {\"query\": \"first query\"}"
        "    },"
        "    {"
        "      \"type\": \"tool_use\","
        "      \"id\": \"toolu_02\","
        "      \"name\": \"web_search\","
        "      \"input\": {\"query\": \"second query\"}"
        "    }"
        "  ]"
        "}";

    AiToolCall calls[8];
    int n = ai_parse_tool_calls_anthropic(resp, calls, 8);
    ASSERT_EQ(n, 2);
    ASSERT_STR_EQ(calls[0].id, "toolu_01");
    ASSERT_STR_EQ(calls[1].id, "toolu_02");
    ASSERT_NOT_NULL(strstr(calls[0].input_json, "first query"));
    ASSERT_NOT_NULL(strstr(calls[1].input_json, "second query"));
    TEST_END();
}

int test_ai_parse_tool_calls_anthropic_no_tools(void)
{
    TEST_BEGIN();
    const char *resp =
        "{"
        "  \"content\": ["
        "    {\"type\": \"text\", \"text\": \"Hello, world!\"}"
        "  ]"
        "}";

    AiToolCall calls[8];
    int n = ai_parse_tool_calls_anthropic(resp, calls, 8);
    ASSERT_EQ(n, 0);
    TEST_END();
}

int test_ai_parse_tool_calls_anthropic_invalid(void)
{
    TEST_BEGIN();
    AiToolCall calls[8];
    int n = ai_parse_tool_calls_anthropic("{not valid json {{{}",
                                           calls, 8);
    ASSERT_EQ(n, -1);
    TEST_END();
}

/* ======================================================================== */
/* OpenAI parsing tests                                                       */
/* ======================================================================== */

int test_ai_parse_tool_calls_openai_basic(void)
{
    TEST_BEGIN();
    const char *resp =
        "{"
        "  \"choices\": [{"
        "    \"message\": {"
        "      \"role\": \"assistant\","
        "      \"tool_calls\": [{"
        "        \"id\": \"call_abc123\","
        "        \"type\": \"function\","
        "        \"function\": {"
        "          \"name\": \"web_search\","
        "          \"arguments\": \"{\\\"query\\\":\\\"nutshell ssh\\\"}\""
        "        }"
        "      }]"
        "    }"
        "  }]"
        "}";

    AiToolCall calls[8];
    int n = ai_parse_tool_calls_openai(resp, calls, 8);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(calls[0].id,   "call_abc123");
    ASSERT_STR_EQ(calls[0].name, "web_search");
    ASSERT_NOT_NULL(strstr(calls[0].input_json, "nutshell ssh"));
    TEST_END();
}

int test_ai_parse_tool_calls_openai_multiple(void)
{
    TEST_BEGIN();
    const char *resp =
        "{"
        "  \"choices\": [{"
        "    \"message\": {"
        "      \"tool_calls\": ["
        "        {"
        "          \"id\": \"call_001\","
        "          \"function\": {"
        "            \"name\": \"web_search\","
        "            \"arguments\": \"{\\\"query\\\":\\\"first\\\"}\""
        "          }"
        "        },"
        "        {"
        "          \"id\": \"call_002\","
        "          \"function\": {"
        "            \"name\": \"web_search\","
        "            \"arguments\": \"{\\\"query\\\":\\\"second\\\"}\""
        "          }"
        "        }"
        "      ]"
        "    }"
        "  }]"
        "}";

    AiToolCall calls[8];
    int n = ai_parse_tool_calls_openai(resp, calls, 8);
    ASSERT_EQ(n, 2);
    ASSERT_STR_EQ(calls[0].id, "call_001");
    ASSERT_STR_EQ(calls[1].id, "call_002");
    TEST_END();
}

int test_ai_parse_tool_calls_openai_no_tools(void)
{
    TEST_BEGIN();
    /* Response with no tool_calls in message */
    const char *resp =
        "{"
        "  \"choices\": [{"
        "    \"message\": {"
        "      \"role\": \"assistant\","
        "      \"content\": \"Hello!\""
        "    }"
        "  }]"
        "}";

    AiToolCall calls[8];
    int n = ai_parse_tool_calls_openai(resp, calls, 8);
    ASSERT_EQ(n, 0);
    TEST_END();
}

int test_ai_parse_tool_calls_openai_invalid(void)
{
    TEST_BEGIN();
    AiToolCall calls[8];
    int n = ai_parse_tool_calls_openai("not json at all !!!",
                                        calls, 8);
    ASSERT_EQ(n, -1);
    TEST_END();
}

/* ======================================================================== */
/* Execution tests                                                            */
/* ======================================================================== */

int test_ai_tool_execute_basic(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("web_search", "Search", SIMPLE_SCHEMA);
    t.execute = mock_execute;
    ai_tools_register(&reg, &t);

    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id,         sizeof(call.id),         "toolu_01");
    snprintf(call.name,       sizeof(call.name),       "web_search");
    snprintf(call.input_json, sizeof(call.input_json), "{\"query\":\"test\"}");

    AiToolResult result;
    int rc = ai_tool_execute(&reg, &call, NULL, &result);

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(result.is_error, 0);
    ASSERT_NOT_NULL(result.content);
    ASSERT_STR_EQ(result.content, "mock result");
    ASSERT_STR_EQ(result.tool_use_id, "toolu_01");

    free(result.content);
    TEST_END();
}

int test_ai_tool_execute_not_found(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id,   sizeof(call.id),   "toolu_xx");
    snprintf(call.name, sizeof(call.name), "no_such_tool");

    AiToolResult result;
    int rc = ai_tool_execute(&reg, &call, NULL, &result);

    ASSERT_EQ(rc, -1);
    ASSERT_EQ(result.is_error, 1);
    /* Nutshell's own text: in tool_note, not content */
    ASSERT_NULL(result.content);
    ASSERT_EQ(result.content_len, 0);
    ASSERT_NOT_NULL(strstr(result.tool_note, "Unknown tool"));
    ASSERT_NOT_NULL(strstr(result.tool_note, "no_such_tool"));

    TEST_END();
}

int test_ai_tool_execute_tool_data(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    int sentinel = 42;
    AiToolDef t = make_tool("probe", "probe tool", SIMPLE_SCHEMA);
    t.execute   = mock_execute_captures_data;
    t.tool_data = &sentinel;
    ai_tools_register(&reg, &t);

    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id,   sizeof(call.id),   "toolu_probe");
    snprintf(call.name, sizeof(call.name), "probe");

    g_received_tool_data = NULL;

    AiToolResult result;
    int rc = ai_tool_execute(&reg, &call, NULL, &result);

    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(g_received_tool_data == (void *)&sentinel);

    free(result.content);
    TEST_END();
}

int test_ai_tool_execute_truncated(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("big_tool", "Big output", SIMPLE_SCHEMA);
    t.execute = mock_execute_truncated;
    ai_tools_register(&reg, &t);

    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id,   sizeof(call.id),   "toolu_big");
    snprintf(call.name, sizeof(call.name), "big_tool");

    AiToolResult result;
    int rc = ai_tool_execute(&reg, &call, NULL, &result);

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(result.is_error, 0);
    ASSERT_EQ(result.was_truncated, 1);

    free(result.content);
    TEST_END();
}

/* ======================================================================== */
/* Result serialization tests                                                 */
/* ======================================================================== */

int test_ai_tool_result_anthropic_basic(void)
{
    TEST_BEGIN();
    AiToolResult result;
    memset(&result, 0, sizeof(result));
    snprintf(result.tool_use_id, sizeof(result.tool_use_id), "toolu_01abc");
    const char *text = "search results here";
    result.content     = (char *)text;
    result.content_len = strlen(text);
    result.is_error    = 0;
    result.was_truncated = 0;

    char buf[1024];
    int n = ai_tool_result_anthropic(&result, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_NOT_NULL(strstr(buf, "\"type\":\"tool_result\""));
    ASSERT_NOT_NULL(strstr(buf, "\"tool_use_id\":\"toolu_01abc\""));
    ASSERT_NOT_NULL(strstr(buf, "\"content\":\"search results here\""));
    /* No is_error field when false */
    ASSERT_NULL(strstr(buf, "is_error"));
    TEST_END();
}

int test_ai_tool_result_anthropic_error(void)
{
    TEST_BEGIN();
    AiToolResult result;
    memset(&result, 0, sizeof(result));
    snprintf(result.tool_use_id, sizeof(result.tool_use_id), "toolu_err");
    const char *text = "failed to fetch";
    result.content     = (char *)text;
    result.content_len = strlen(text);
    result.is_error    = 1;

    char buf[1024];
    int n = ai_tool_result_anthropic(&result, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_NOT_NULL(strstr(buf, "\"is_error\":true"));
    ASSERT_NOT_NULL(strstr(buf, "\"tool_use_id\":\"toolu_err\""));
    TEST_END();
}

int test_ai_tool_result_openai_basic(void)
{
    TEST_BEGIN();
    AiToolResult result;
    memset(&result, 0, sizeof(result));
    snprintf(result.tool_use_id, sizeof(result.tool_use_id), "call_abc");
    const char *text = "the answer";
    result.content     = (char *)text;
    result.content_len = strlen(text);
    result.is_error    = 0;

    char buf[1024];
    int n = ai_tool_result_openai(&result, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_NOT_NULL(strstr(buf, "\"role\":\"tool\""));
    ASSERT_NOT_NULL(strstr(buf, "\"tool_call_id\":\"call_abc\""));
    ASSERT_NOT_NULL(strstr(buf, "\"content\":\"the answer\""));
    TEST_END();
}

int test_ai_tool_result_openai_error(void)
{
    TEST_BEGIN();
    /* OpenAI tool results have no is_error — content carries the error message */
    AiToolResult result;
    memset(&result, 0, sizeof(result));
    snprintf(result.tool_use_id, sizeof(result.tool_use_id), "call_fail");
    const char *text = "Error: timeout";
    result.content     = (char *)text;
    result.content_len = strlen(text);
    result.is_error    = 1;

    char buf[1024];
    int n = ai_tool_result_openai(&result, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_NOT_NULL(strstr(buf, "\"role\":\"tool\""));
    ASSERT_NOT_NULL(strstr(buf, "\"tool_call_id\":\"call_fail\""));
    ASSERT_NOT_NULL(strstr(buf, "\"content\":\"Error: timeout\""));
    TEST_END();
}

/* ======================================================================== */
/* Schema validation tests                                                    */
/* ======================================================================== */

int test_ai_tools_validate_schemas_valid(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("web_search", "Search", SIMPLE_SCHEMA);
    ai_tools_register(&reg, &t);

    int rc = ai_tools_validate_schemas(&reg);
    ASSERT_EQ(rc, 0);
    TEST_END();
}

int test_ai_tools_validate_schemas_invalid(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("bad_tool", "Bad schema", "{not valid json}");
    ai_tools_register(&reg, &t);

    int rc = ai_tools_validate_schemas(&reg);
    ASSERT_EQ(rc, -1);
    TEST_END();
}

/* ======================================================================== */
/* Input buffer boundary tests                                                */
/* ======================================================================== */

int test_ai_tool_input_max_boundary(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);

    AiToolDef t = make_tool("web_search", "Search", SIMPLE_SCHEMA);
    ai_tools_register(&reg, &t);

    /* Build an input_json string that is exactly AI_TOOL_INPUT_MAX-1 chars
     * (including NUL at end). We need valid JSON that fills the space. */
    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id,   sizeof(call.id),   "toolu_boundary");
    snprintf(call.name, sizeof(call.name), "web_search");

    /* Fill with a long query value: {"query":"AAA...A"} */
    /* Prefix: {"query":" = 10 chars, suffix: "} = 2 chars, total overhead 12 */
    const char prefix[] = "{\"query\":\"";
    const char suffix[] = "\"}";
    size_t overhead = strlen(prefix) + strlen(suffix);
    size_t fill_len = (size_t)(AI_TOOL_INPUT_MAX - 1) - overhead;

    /* Clamp if formula overflows (shouldn't, but be safe) */
    if (fill_len >= (size_t)(AI_TOOL_INPUT_MAX)) {
        fill_len = AI_TOOL_INPUT_MAX - overhead - 1;
    }

    snprintf(call.input_json, sizeof(call.input_json), "%s", prefix);
    size_t pos = strlen(call.input_json);
    for (size_t i = 0; i < fill_len && pos + 1 < sizeof(call.input_json); i++) {
        call.input_json[pos++] = 'A';
    }
    /* Make sure we have room for the suffix */
    if (pos + strlen(suffix) < sizeof(call.input_json)) {
        memcpy(call.input_json + pos, suffix, strlen(suffix) + 1);
    }

    /* Should be parseable JSON */
    JsonNode *parsed = json_parse(call.input_json);
    ASSERT_NOT_NULL(parsed);
    if (parsed) json_free(parsed);

    /* Executing the call should succeed */
    AiToolResult result;
    int rc = ai_tool_execute(&reg, &call, NULL, &result);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(result.is_error, 0);

    free(result.content);
    TEST_END();
}

/* ======================================================================== */
/* Anthropic streaming tests                                                  */
/* ======================================================================== */

int test_ai_stream_anthropic_tool_use_block(void)
{
    TEST_BEGIN();
    AiToolStreamState state;
    ai_tools_stream_init(&state);

    /* content_block_start: tool_use */
    const char *start_chunk =
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"tool_use\","
        "\"id\":\"toolu_01\",\"name\":\"web_search\"}}";
    int rc = ai_parse_tool_stream_anthropic(start_chunk, &state);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ((int)state.current_block_type, (int)BLOCK_TOOL_USE);
    ASSERT_EQ(state.tool_call_active, 1);
    ASSERT_STR_EQ(state.active_tool_call.id,   "toolu_01");
    ASSERT_STR_EQ(state.active_tool_call.name, "web_search");

    /* content_block_delta: input_json_delta */
    const char *delta_chunk =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"input_json_delta\","
        "\"partial_json\":\"{\\\"query\\\":\\\"test\\\"}\"}}";
    rc = ai_parse_tool_stream_anthropic(delta_chunk, &state);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(strstr(state.active_tool_call.input_json, "query"));

    /* content_block_stop */
    const char *stop_chunk =
        "{\"type\":\"content_block_stop\",\"index\":0}";
    rc = ai_parse_tool_stream_anthropic(stop_chunk, &state);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(state.pending_tool_count, 1);
    ASSERT_NOT_NULL(state.pending_tool_calls);
    ASSERT_STR_EQ(state.pending_tool_calls[0].id,   "toolu_01");
    ASSERT_STR_EQ(state.pending_tool_calls[0].name, "web_search");
    ASSERT_NOT_NULL(strstr(state.pending_tool_calls[0].input_json, "query"));
    ASSERT_EQ(state.tool_call_active, 0);
    ASSERT_EQ((int)state.current_block_type, (int)BLOCK_NONE);

    /* message_delta: stop_reason tool_use => returns 1 */
    const char *msg_delta_chunk =
        "{\"type\":\"message_delta\","
        "\"delta\":{\"stop_reason\":\"tool_use\",\"stop_sequence\":null},"
        "\"usage\":{\"output_tokens\":20}}";
    rc = ai_parse_tool_stream_anthropic(msg_delta_chunk, &state);
    ASSERT_EQ(rc, 1);

    ai_tools_stream_reset(&state);
    TEST_END();
}

int test_ai_stream_anthropic_text_block(void)
{
    TEST_BEGIN();
    AiToolStreamState state;
    ai_tools_stream_init(&state);

    /* content_block_start: text */
    const char *start_chunk =
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"text\",\"text\":\"\"}}";
    int rc = ai_parse_tool_stream_anthropic(start_chunk, &state);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ((int)state.current_block_type, (int)BLOCK_TEXT);

    /* content_block_delta: text_delta => 0 (caller handles display) */
    const char *delta_chunk =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello!\"}}";
    rc = ai_parse_tool_stream_anthropic(delta_chunk, &state);
    ASSERT_EQ(rc, 0);

    /* content_block_stop => BLOCK_NONE */
    const char *stop_chunk =
        "{\"type\":\"content_block_stop\",\"index\":0}";
    rc = ai_parse_tool_stream_anthropic(stop_chunk, &state);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ((int)state.current_block_type, (int)BLOCK_NONE);
    ASSERT_EQ(state.pending_tool_count, 0);

    /* message_delta: stop_reason end_turn => returns 2 */
    const char *msg_delta_chunk =
        "{\"type\":\"message_delta\","
        "\"delta\":{\"stop_reason\":\"end_turn\",\"stop_sequence\":null}}";
    rc = ai_parse_tool_stream_anthropic(msg_delta_chunk, &state);
    ASSERT_EQ(rc, 2);

    ai_tools_stream_reset(&state);
    TEST_END();
}

int test_ai_stream_anthropic_mixed_blocks(void)
{
    TEST_BEGIN();
    AiToolStreamState state;
    ai_tools_stream_init(&state);

    /* Text block */
    const char *text_start =
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"text\",\"text\":\"\"}}";
    ai_parse_tool_stream_anthropic(text_start, &state);

    const char *text_delta =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"Searching...\"}}";
    ai_parse_tool_stream_anthropic(text_delta, &state);

    const char *text_stop =
        "{\"type\":\"content_block_stop\",\"index\":0}";
    ai_parse_tool_stream_anthropic(text_stop, &state);

    ASSERT_EQ(state.pending_tool_count, 0);

    /* tool_use block */
    const char *tool_start =
        "{\"type\":\"content_block_start\",\"index\":1,"
        "\"content_block\":{\"type\":\"tool_use\","
        "\"id\":\"toolu_mix\",\"name\":\"web_search\"}}";
    ai_parse_tool_stream_anthropic(tool_start, &state);

    const char *tool_delta =
        "{\"type\":\"content_block_delta\",\"index\":1,"
        "\"delta\":{\"type\":\"input_json_delta\","
        "\"partial_json\":\"{\\\"query\\\":\\\"mixed\\\"}\"}}";
    ai_parse_tool_stream_anthropic(tool_delta, &state);

    const char *tool_stop =
        "{\"type\":\"content_block_stop\",\"index\":1}";
    ai_parse_tool_stream_anthropic(tool_stop, &state);

    ASSERT_EQ(state.pending_tool_count, 1);
    ASSERT_NOT_NULL(state.pending_tool_calls);
    ASSERT_STR_EQ(state.pending_tool_calls[0].id,   "toolu_mix");
    ASSERT_STR_EQ(state.pending_tool_calls[0].name, "web_search");
    ASSERT_NOT_NULL(strstr(state.pending_tool_calls[0].input_json, "mixed"));

    ai_tools_stream_reset(&state);
    TEST_END();
}

/* ======================================================================== */
/* OpenAI streaming tests                                                     */
/* ======================================================================== */

int test_ai_stream_openai_tool_call(void)
{
    TEST_BEGIN();
    AiToolStreamState state;
    ai_tools_stream_init(&state);

    /* First chunk: id and function.name */
    const char *chunk1 =
        "{\"choices\":[{\"index\":0,"
        "\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_01\","
        "\"type\":\"function\","
        "\"function\":{\"name\":\"web_search\",\"arguments\":\"\"}}]}}]}";
    int rc = ai_parse_tool_stream_openai(chunk1, &state);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(state.pending_tool_count, 1);
    ASSERT_STR_EQ(state.pending_tool_calls[0].id,   "call_01");
    ASSERT_STR_EQ(state.pending_tool_calls[0].name, "web_search");

    /* Second chunk: append arguments */
    const char *chunk2 =
        "{\"choices\":[{\"index\":0,"
        "\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"function\":{\"arguments\":\"{\\\"query\\\":\\\"test\\\"}\"}}]}}]}";
    rc = ai_parse_tool_stream_openai(chunk2, &state);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(strstr(state.pending_tool_calls[0].input_json, "query"));

    /* Finish chunk: finish_reason tool_calls => returns 1 */
    const char *finish_chunk =
        "{\"choices\":[{\"index\":0,\"delta\":{},"
        "\"finish_reason\":\"tool_calls\"}]}";
    rc = ai_parse_tool_stream_openai(finish_chunk, &state);
    ASSERT_EQ(rc, 1);

    ASSERT_EQ(state.pending_tool_count, 1);
    ASSERT_STR_EQ(state.pending_tool_calls[0].id,   "call_01");
    ASSERT_STR_EQ(state.pending_tool_calls[0].name, "web_search");
    ASSERT_NOT_NULL(strstr(state.pending_tool_calls[0].input_json, "query"));

    ai_tools_stream_reset(&state);
    TEST_END();
}

int test_ai_stream_openai_no_tools(void)
{
    TEST_BEGIN();
    AiToolStreamState state;
    ai_tools_stream_init(&state);

    /* Regular content delta — no tool_calls */
    const char *chunk1 =
        "{\"choices\":[{\"index\":0,"
        "\"delta\":{\"content\":\"Hello there!\"}}]}";
    int rc = ai_parse_tool_stream_openai(chunk1, &state);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(state.pending_tool_count, 0);

    /* Finish chunk: finish_reason stop => returns 2 */
    const char *finish_chunk =
        "{\"choices\":[{\"index\":0,\"delta\":{},"
        "\"finish_reason\":\"stop\"}]}";
    rc = ai_parse_tool_stream_openai(finish_chunk, &state);
    ASSERT_EQ(rc, 2);
    ASSERT_EQ(state.pending_tool_count, 0);

    ai_tools_stream_reset(&state);
    TEST_END();
}

/* ======================================================================== */
/* OpenAI-format stream: finish reasons, missing index, in-stream errors      */
/* (Kimi trace, Task 17). Chunks are the providers' real SSE shapes.          */
/* ======================================================================== */

#define SSE_CALL_FIRST \
    "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0," \
    "\"id\":\"call_9\",\"type\":\"function\"," \
    "\"function\":{\"name\":\"web_fetch\",\"arguments\":\"\"}}]},\"finish_reason\":null}]}"
#define SSE_CALL_ARGS \
    "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0," \
    "\"function\":{\"arguments\":\"{\\\"url\\\":\\\"https://example.org/\\\"}\"}}]}," \
    "\"finish_reason\":null}]}"

/* One chunk through the extended parser. */
static int feed_ex(AiToolStreamState *st, const char *json,
                   char *text, size_t tsz)
{
    char think[64] = "";
    text[0] = '\0';
    return ai_parse_stream_chunk_ex(json, text, tsz, think, sizeof(think),
                                    st, "moonshot", NULL, NULL);
}

int test_ai_stream_openai_finish_stop_with_pending_call(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    ASSERT_EQ(feed_ex(&st, SSE_CALL_FIRST, text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st, SSE_CALL_ARGS, text, sizeof(text)), 0);
    int rc = feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}",
        text, sizeof(text));
    ASSERT_EQ(rc, 1);                       /* a stop, as before */
    ASSERT_EQ(st.pending_tool_count, 1);    /* the call is still pending */
    ASSERT_STR_EQ(st.finish, "stop");
    ASSERT_STR_EQ(st.pending_tool_calls[0].name, "web_fetch");
    /* The loop runs although tool_stop (rc == 2) never came. */
    ASSERT_EQ(ai_tool_round_should_run("moonshot", 0, st.pending_tool_count, 0), 1);
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_stream_openai_no_finish_reason_with_pending_call(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    ASSERT_EQ(feed_ex(&st, SSE_CALL_FIRST, text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st, SSE_CALL_ARGS, text, sizeof(text)), 0);
    /* The stream ends at [DONE] with no finish_reason at all. */
    (void)feed_ex(&st, "[DONE]", text, sizeof(text));   /* return value not pinned */
    ASSERT_EQ(st.pending_tool_count, 1);
    ASSERT_STR_EQ(st.finish, "");
    ASSERT_EQ(ai_tool_round_should_run("moonshot", 0, st.pending_tool_count, 0), 1);
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_stream_openai_whole_call_in_finish_chunk(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    int rc = feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"id\":\"call_7\",\"type\":\"function\",\"function\":{\"name\":"
        "\"web_fetch\",\"arguments\":\"{\\\"url\\\":\\\"https://example.org/\\\"}\"}}]},"
        "\"finish_reason\":\"tool_calls\"}]}", text, sizeof(text));
    ASSERT_EQ(rc, 2);
    ASSERT_EQ(st.pending_tool_count, 1);
    ASSERT_STR_EQ(st.pending_tool_calls[0].id, "call_7");
    ASSERT_STR_EQ(st.pending_tool_calls[0].name, "web_fetch");
    ASSERT_NOT_NULL(strstr(st.pending_tool_calls[0].input_json, "example.org"));
    ASSERT_STR_EQ(st.finish, "tool_calls");
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_stream_openai_tool_delta_without_index(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"id\":\"call_3\",\"type\":\"function\",\"function\":{\"name\":"
        "\"web_fetch\",\"arguments\":\"{\\\"url\\\":\"}}]},\"finish_reason\":null}]}",
        text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 1);           /* index 0, one call */
    ASSERT_EQ(st.index_defaulted, 1);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"function\":{\"arguments\":\"\\\"https://example.org/\\\"}\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 1);           /* still one call */
    ASSERT_EQ(st.index_defaulted, 2);
    ASSERT_STR_EQ(st.pending_tool_calls[0].name, "web_fetch");
    ASSERT_STR_EQ(st.pending_tool_calls[0].input_json,
                  "{\"url\":\"https://example.org/\"}");
    ai_tools_stream_reset(&st);

    /* An explicit index never counts as defaulted. */
    ai_tools_stream_init(&st);
    ASSERT_EQ(feed_ex(&st, SSE_CALL_FIRST, text, sizeof(text)), 0);
    ASSERT_EQ(st.index_defaulted, 0);
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_stream_openai_index_less_calls_split_on_id(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    /* Call a: id and name first, arguments over two chunks (no index, no id). */
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"a\","
        "\"type\":\"function\",\"function\":{\"name\":\"web_fetch\","
        "\"arguments\":\"{\\\"url\\\":\"}}]},\"finish_reason\":null}]}",
        text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"function\":{\"arguments\":\"\\\"https://a.example/\\\"}\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 1);
    /* Call b: a different id starts a new call, arguments over two chunks. */
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"b\","
        "\"type\":\"function\",\"function\":{\"name\":\"web_search\","
        "\"arguments\":\"{\\\"query\\\":\"}}]},\"finish_reason\":null}]}",
        text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 2);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"function\":{\"arguments\":\"\\\"cats\\\"}\"}}]}," 
        "\"finish_reason\":\"stop\"}]}", text, sizeof(text)), 1);
    ASSERT_EQ(st.pending_tool_count, 2);
    ASSERT_STR_EQ(st.pending_tool_calls[0].id, "a");
    ASSERT_STR_EQ(st.pending_tool_calls[0].name, "web_fetch");
    ASSERT_STR_EQ(st.pending_tool_calls[0].input_json,
                  "{\"url\":\"https://a.example/\"}");
    ASSERT_STR_EQ(st.pending_tool_calls[1].id, "b");
    ASSERT_STR_EQ(st.pending_tool_calls[1].name, "web_search");
    ASSERT_STR_EQ(st.pending_tool_calls[1].input_json, "{\"query\":\"cats\"}");
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_stream_openai_index_less_two_calls_in_one_delta(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"id\":\"a\",\"function\":{\"name\":\"web_fetch\",\"arguments\":\"{\\\"url\\\":\\\"https://a.example/\\\"}\"}},"
        "{\"id\":\"b\",\"function\":{\"name\":\"web_search\",\"arguments\":\"{\\\"query\\\":\\\"x\\\"}\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 2);
    ASSERT_STR_EQ(st.pending_tool_calls[0].name, "web_fetch");
    ASSERT_STR_EQ(st.pending_tool_calls[0].input_json,
                  "{\"url\":\"https://a.example/\"}");
    ASSERT_STR_EQ(st.pending_tool_calls[1].name, "web_search");
    ASSERT_STR_EQ(st.pending_tool_calls[1].input_json, "{\"query\":\"x\"}");
    ASSERT_EQ(st.index_defaulted, 2);
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_stream_openai_index_less_id_only_on_first_chunk_stays_one_call(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"k1\","
        "\"function\":{\"name\":\"web_fetch\",\"arguments\":\"{\\\"u\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"function\":{\"arguments\":\"rl\\\":\"}}]},\"finish_reason\":null}]}",
        text, sizeof(text)), 0);
    /* The same id repeated on a later chunk is the same call. */
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"k1\","
        "\"function\":{\"arguments\":\"\\\"x\\\"}\"}}]},\"finish_reason\":null}]}",
        text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 1);
    ASSERT_STR_EQ(st.pending_tool_calls[0].input_json, "{\"url\":\"x\"}");
    ai_tools_stream_reset(&st);

    /* A call that has a name but never got an id is closed by a delta that
     * brings one: that is a second call. */
    ai_tools_stream_init(&st);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"function\":{\"name\":\"web_fetch\",\"arguments\":\"{}\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"n\","
        "\"function\":{\"name\":\"web_search\",\"arguments\":\"{}\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 2);
    ASSERT_STR_EQ(st.pending_tool_calls[1].name, "web_search");
    ai_tools_stream_reset(&st);
    TEST_END();
}

/* Ruling R51 (review of the fix wave, 2026-11-11): with no "index" at all, a
 * chunk carrying a new non-empty id that differs from the open slot's opens
 * the next slot, whatever else the chunk holds; two parallel calls a and b
 * streamed in pieces both survive whole with their own arguments. The id is
 * repeated on some of a's chunks and on none of b's, and b's id comes in a
 * chunk of its own before its name. */
int test_ai_stream_openai_index_less_parallel_calls_survive(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"a\","
        "\"type\":\"function\",\"function\":{\"name\":\"web_fetch\","
        "\"arguments\":\"\"}}]},\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"a\","
        "\"function\":{\"arguments\":\"{\\\"url\\\":\"}}]},\"finish_reason\":null}]}",
        text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"function\":{\"arguments\":\"\\\"https://a.example/\\\"}\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    /* b's id alone, then its name, then its arguments in two pieces */
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"b\"}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 2);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"function\":{\"name\":\"web_search\",\"arguments\":\"{\\\"query\\\":\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"function\":{\"arguments\":\"\\\"cats\\\"}\"}}]},\"finish_reason\":null}]}",
        text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 2);
    ASSERT_EQ(st.index_defaulted, 6);
    ASSERT_STR_EQ(st.pending_tool_calls[0].id, "a");
    ASSERT_STR_EQ(st.pending_tool_calls[0].name, "web_fetch");
    ASSERT_STR_EQ(st.pending_tool_calls[0].input_json,
                  "{\"url\":\"https://a.example/\"}");
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 0);
    ASSERT_STR_EQ(st.pending_tool_calls[1].id, "b");
    ASSERT_STR_EQ(st.pending_tool_calls[1].name, "web_search");
    ASSERT_STR_EQ(st.pending_tool_calls[1].input_json, "{\"query\":\"cats\"}");
    ASSERT_EQ(st.pending_tool_calls[1].overflowed, 0);
    ai_tools_stream_reset(&st);

    /* only an explicit index reused with a different id overflows */
    ai_tools_stream_init(&st);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"index\":0,\"id\":\"a\",\"function\":{\"name\":\"web_fetch\",\"arguments\":\"{}\"}},"
        "{\"index\":0,\"id\":\"b\",\"function\":{\"name\":\"web_search\",\"arguments\":\"{}\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 1);
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 1);
    ASSERT_EQ(st.index_defaulted, 0);
    ai_tools_stream_reset(&st);
    TEST_END();
}

/* A tool input whose object holds a key with a \u0000 escape (review of the
 * fix wave, 2026-11-11): the call is kept, the key is written back whole and
 * never read as "command", so the real "command" member is the one that runs. */
int test_ai_parse_tool_calls_anthropic_nul_key(void)
{
    TEST_BEGIN();
    const char *resp =
        "{\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":"
        "\"run_command\",\"input\":{\"command\\u0000x\":\"evil\","
        "\"command\":\"ls\"}}]}";
    AiToolCall calls[2];
    memset(calls, 0, sizeof(calls));
    ASSERT_EQ(ai_parse_tool_calls_anthropic(resp, calls, 2), 1);
    ASSERT_STR_EQ(calls[0].input_json,
                  "{\"command\\u0000x\":\"evil\",\"command\":\"ls\"}");
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    ASSERT_EQ(runcmd_parse_input(calls[0].input_json, cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "ls");
    TEST_END();
}

int test_ai_stream_openai_indexed_parallel_calls_unchanged(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"index\":0,\"id\":\"a\",\"function\":{\"name\":\"web_fetch\",\"arguments\":\"{\\\"url\\\":\"}},"
        "{\"index\":1,\"id\":\"b\",\"function\":{\"name\":\"web_search\",\"arguments\":\"{\\\"query\\\":\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"index\":1,\"function\":{\"arguments\":\"\\\"q\\\"}\"}},"
        "{\"index\":0,\"function\":{\"arguments\":\"\\\"https://a.example/\\\"}\"}}]},"
        "\"finish_reason\":\"tool_calls\"}]}", text, sizeof(text)), 2);
    ASSERT_EQ(st.pending_tool_count, 2);
    ASSERT_EQ(st.index_defaulted, 0);
    ASSERT_STR_EQ(st.pending_tool_calls[0].input_json, "{\"url\":\"https://a.example/\"}");
    ASSERT_STR_EQ(st.pending_tool_calls[1].input_json, "{\"query\":\"q\"}");
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_stream_openai_text_kept_with_finish_reason(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    int rc = feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\" Following it now.\"},"
        "\"finish_reason\":\"stop\"}]}", text, sizeof(text));
    ASSERT_EQ(rc, 1);
    ASSERT_STR_EQ(text, " Following it now.");
    ASSERT_STR_EQ(st.finish, "stop");

    ai_tools_stream_reset(&st);
    ai_tools_stream_init(&st);
    rc = feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Fetching.\","
        "\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"type\":\"function\","
        "\"function\":{\"name\":\"web_fetch\",\"arguments\":\"{}\"}}]},"
        "\"finish_reason\":\"tool_calls\"}]}", text, sizeof(text));
    ASSERT_EQ(rc, 2);
    ASSERT_STR_EQ(text, "Fetching.");
    ASSERT_EQ(st.pending_tool_count, 1);
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_stream_openai_finish_tool_calls_path_unchanged(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    ASSERT_EQ(feed_ex(&st, SSE_CALL_FIRST, text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st, SSE_CALL_ARGS, text, sizeof(text)), 0);
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"tool_calls\"}]}",
        text, sizeof(text)), 2);
    ASSERT_EQ(st.pending_tool_count, 1);
    ASSERT_STR_EQ(st.finish, "tool_calls");
    ASSERT_EQ(ai_tool_round_should_run("moonshot", 1, 1, 0), 1);
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_stream_openai_plain_reply_stop_ends_normally(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hi\"},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_STR_EQ(text, "Hi");
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}",
        text, sizeof(text)), 1);
    ASSERT_EQ(st.pending_tool_count, 0);
    ASSERT_EQ(ai_tool_round_should_run("moonshot", 0, st.pending_tool_count, 0), 0);
    ai_tools_stream_reset(&st);
    TEST_END();
}

/* ---- Sticky overflow: a refused fragment poisons its call ---------------- */

/* An OpenAI chunk carrying one fragment of tool call `index`; `args` is
 * already JSON-escaped for the arguments string. id/name only when non-NULL. */
static void oa_frag(char *out, size_t sz, int index, const char *id,
                    const char *name, const char *args)
{
    char head[160] = "";
    if (id && name)
        snprintf(head, sizeof head, "\"id\":\"%s\",\"type\":\"function\",", id);
    snprintf(out, sz,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":%d,%s"
        "\"function\":{%s%s%s\"arguments\":\"%s\"}}]},\"finish_reason\":null}]}",
        index, head,
        name ? "\"name\":\"" : "", name ? name : "", name ? "\"," : "", args);
}

/* The fragment that overflows: past AI_TOOL_INPUT_MAX on its own. */
static void fill_overflow(char *buf, size_t n)
{
    memset(buf, 'a', n);
    buf[n] = '\0';
}

int test_tool_stream_overflow_splice_refused(void)
{
    TEST_BEGIN();
    static char big[4300];
    static char chunk[4800];
    AiToolStreamState st;
    ai_tools_stream_init(&st);

    oa_frag(chunk, sizeof chunk, 0, "call_1", "run_command", "{\\\"note\\\":\\\"");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 0);

    /* 4200 bytes: refused, past AI_TOOL_INPUT_MAX */
    fill_overflow(big, 4200);
    oa_frag(chunk, sizeof chunk, 0, NULL, NULL, big);
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 1);

    /* the tail would fit on its own: it must not be appended */
    oa_frag(chunk, sizeof chunk, 0, NULL, NULL,
            "\\\",\\\"command\\\":\\\"rm -rf /home/user/build-cache\\\"}");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 1);
    ASSERT_TRUE(strstr(st.pending_tool_calls[0].input_json, "rm -rf") == NULL);
    ASSERT_TRUE(strstr(st.pending_tool_calls[0].input_json, "command") == NULL);

    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_tool_stream_overflow_keeps_rest_of_chunk(void)
{
    TEST_BEGIN();
    static char big[4300];
    static char chunk[4900];
    AiToolStreamState st;
    ai_tools_stream_init(&st);

    oa_frag(chunk, sizeof chunk, 0, "call_1", "run_command", "{\\\"note\\\":\\\"");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    oa_frag(chunk, sizeof chunk, 1, "call_2", "web_search", "");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);

    /* one chunk: call 0 overflows, call 1 completes, then the finish */
    fill_overflow(big, 4200);
    snprintf(chunk, sizeof chunk,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"index\":0,\"function\":{\"arguments\":\"%s\"}},"
        "{\"index\":1,\"function\":{\"arguments\":\"{\\\"query\\\":\\\"q\\\"}\"}}"
        "]},\"finish_reason\":\"tool_calls\"}]}", big);
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 1);

    ASSERT_EQ(st.pending_tool_count, 2);
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 1);
    ASSERT_EQ(st.pending_tool_calls[1].overflowed, 0);
    ASSERT_STR_EQ(st.pending_tool_calls[1].input_json, "{\"query\":\"q\"}");
    ASSERT_STR_EQ(st.finish, "tool_calls");

    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_tool_stream_index_bounded(void)
{
    TEST_BEGIN();
    static char chunk[1024];
    AiToolStreamState st;
    ai_tools_stream_init(&st);

    /* one chunk: a huge index, a valid call 0, then the finish */
    snprintf(chunk, sizeof chunk,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"index\":500000,\"id\":\"bad\",\"function\":{\"name\":\"run_command\",\"arguments\":\"{}\"}},"
        "{\"index\":1e300,\"function\":{\"arguments\":\"{}\"}},"
        "{\"index\":0,\"id\":\"call_1\",\"function\":{\"name\":\"run_command\",\"arguments\":\"{\\\"command\\\":\\\"ls\\\"}\"}}"
        "]},\"finish_reason\":\"tool_calls\"}]}");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 1);
    ASSERT_EQ(st.pending_tool_count, 1);
    ASSERT_TRUE(st.pending_tool_cap <= AI_TOOL_STREAM_CALLS_MAX);
    ASSERT_STR_EQ(st.pending_tool_calls[0].id, "call_1");
    ASSERT_STR_EQ(st.pending_tool_calls[0].input_json, "{\"command\":\"ls\"}");
    ASSERT_STR_EQ(st.finish, "tool_calls");
    ai_tools_stream_reset(&st);

    /* the boundary: 63 accepted, 64 refused */
    ai_tools_stream_init(&st);
    oa_frag(chunk, sizeof chunk, AI_TOOL_STREAM_CALLS_MAX - 1, "c63", "run_command", "{}");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    ASSERT_EQ(st.pending_tool_count, AI_TOOL_STREAM_CALLS_MAX);
    ASSERT_STR_EQ(st.pending_tool_calls[AI_TOOL_STREAM_CALLS_MAX - 1].id, "c63");
    oa_frag(chunk, sizeof chunk, AI_TOOL_STREAM_CALLS_MAX, "c64", "run_command", "{}");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    ASSERT_EQ(st.pending_tool_count, AI_TOOL_STREAM_CALLS_MAX);
    ai_tools_stream_reset(&st);

    /* index-less deltas with a new id each: the split path stops at the bound */
    ai_tools_stream_init(&st);
    for (int i = 0; i < AI_TOOL_STREAM_CALLS_MAX + 6; i++) {
        snprintf(chunk, sizeof chunk,
            "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"id\":\"i%d\","
            "\"function\":{\"name\":\"run_command\",\"arguments\":\"{}\"}}]}}]}", i);
        ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    }
    ASSERT_EQ(st.pending_tool_count, AI_TOOL_STREAM_CALLS_MAX);
    ASSERT_STR_EQ(st.pending_tool_calls[AI_TOOL_STREAM_CALLS_MAX - 1].id, "i63");
    /* the refused 65th call's id-less fragments must not splice onto call 63 */
    ASSERT_EQ(st.pending_tool_calls[AI_TOOL_STREAM_CALLS_MAX - 1].overflowed, 1);
    snprintf(chunk, sizeof chunk,
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
        "\"function\":{\"arguments\":\"more\"}}]}}]}");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    ASSERT_STR_EQ(st.pending_tool_calls[AI_TOOL_STREAM_CALLS_MAX - 1].input_json, "{}");
    ai_tools_stream_reset(&st);
    TEST_END();
}

/* review of B1-B11, 2026-11-11: a second call arriving on an index whose slot
 * already holds a different non-empty id must not be spliced onto it (call
 * B's name with call A's arguments): the slot is overflowed, so its stored
 * input is "{}"; the rest of the chunk is still processed. */
int test_tool_stream_index_reuse_overflows(void)
{
    TEST_BEGIN();
    static char chunk[1024];
    AiToolStreamState st;
    ai_tools_stream_init(&st);

    oa_frag(chunk, sizeof chunk, 0, "call_A", "run_command",
            "{\\\"command\\\":\\\"ls\\\"}");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 0);

    /* the same id again on the same index (a resend) is not a new call */
    oa_frag(chunk, sizeof chunk, 0, "call_A", "run_command", "");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 0);

    /* one chunk: index 0 is reused by call_B, index 1 is a fresh call_C, and
     * the finish follows */
    snprintf(chunk, sizeof chunk,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"index\":0,\"id\":\"call_B\",\"type\":\"function\",\"function\":"
        "{\"name\":\"web_search\",\"arguments\":\"{\\\"query\\\":\\\"q\\\"}\"}},"
        "{\"index\":1,\"id\":\"call_C\",\"type\":\"function\",\"function\":"
        "{\"name\":\"web_fetch\",\"arguments\":\"{\\\"url\\\":\\\"https://example.org/\\\"}\"}}"
        "]},\"finish_reason\":\"tool_calls\"}]}");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 1);
    ASSERT_EQ(st.pending_tool_count, 2);
    ASSERT_STR_EQ(st.pending_tool_calls[0].id, "call_A");
    ASSERT_STR_EQ(st.pending_tool_calls[0].name, "run_command");
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 1);
    ASSERT_STR_EQ(st.pending_tool_calls[1].id, "call_C");
    ASSERT_STR_EQ(st.pending_tool_calls[1].name, "web_fetch");
    ASSERT_EQ(st.pending_tool_calls[1].overflowed, 0);
    ASSERT_STR_EQ(st.pending_tool_calls[1].input_json,
                  "{\"url\":\"https://example.org/\"}");
    ASSERT_STR_EQ(st.finish, "tool_calls");

    /* sanitising turns the poisoned slot into "{}", the other is kept */
    ASSERT_EQ(agentic_sanitize_tool_inputs(st.pending_tool_calls, 2), 1);
    ASSERT_STR_EQ(st.pending_tool_calls[0].input_json, "{}");
    ASSERT_STR_EQ(st.pending_tool_calls[1].input_json,
                  "{\"url\":\"https://example.org/\"}");
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_tool_stream_anthropic_blocks_bounded(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    for (int i = 0; i < AI_TOOL_STREAM_CALLS_MAX + 3; i++) {
        ASSERT_EQ(ai_parse_tool_stream_anthropic(
            "{\"type\":\"content_block_start\",\"content_block\":"
            "{\"type\":\"tool_use\",\"id\":\"t\",\"name\":\"run_command\"}}", &st), 0);
        ASSERT_EQ(ai_parse_tool_stream_anthropic(
            "{\"type\":\"content_block_stop\"}", &st), 0);
    }
    ASSERT_EQ(st.pending_tool_count, AI_TOOL_STREAM_CALLS_MAX);
    ASSERT_EQ(ai_parse_tool_stream_anthropic(
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}", &st), 1);
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_tool_stream_anthropic_overflow_sticky(void)
{
    TEST_BEGIN();
    static char big[4300];
    static char chunk[4900];
    AiToolStreamState st;
    ai_tools_stream_init(&st);

    ASSERT_EQ(ai_parse_tool_stream_anthropic(
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_1\","
        "\"name\":\"run_command\"}}", &st), 0);
    ASSERT_EQ(ai_parse_tool_stream_anthropic(
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"input_json_delta\","
        "\"partial_json\":\"{\\\"note\\\":\\\"\"}}", &st), 0);

    fill_overflow(big, 4200);
    snprintf(chunk, sizeof chunk,
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"%s\"}}", big);
    ASSERT_EQ(ai_parse_tool_stream_anthropic(chunk, &st), 0);
    ASSERT_EQ(st.active_tool_call.overflowed, 1);

    ASSERT_EQ(ai_parse_tool_stream_anthropic(
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"input_json_delta\","
        "\"partial_json\":\"\\\",\\\"command\\\":\\\"rm -rf /home/user/build-cache\\\"}\"}}",
        &st), 0);
    ASSERT_TRUE(strstr(st.active_tool_call.input_json, "rm -rf") == NULL);

    /* the block still commits, flagged, and the stop reason is still read */
    ASSERT_EQ(ai_parse_tool_stream_anthropic(
        "{\"type\":\"content_block_stop\",\"index\":0}", &st), 0);
    ASSERT_EQ(st.pending_tool_count, 1);
    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 1);
    ASSERT_TRUE(strstr(st.pending_tool_calls[0].input_json, "rm -rf") == NULL);
    ASSERT_EQ(ai_parse_tool_stream_anthropic(
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}",
        &st), 1);

    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_tool_stream_web_fetch_url_not_spliced(void)
{
    TEST_BEGIN();
    static char big[4300];
    static char chunk[4800];
    AiToolStreamState st;
    ai_tools_stream_init(&st);

    oa_frag(chunk, sizeof chunk, 0, "call_1", "web_fetch", "{\\\"note\\\":\\\"");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    fill_overflow(big, 4200);
    oa_frag(chunk, sizeof chunk, 0, NULL, NULL, big);
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);
    oa_frag(chunk, sizeof chunk, 0, NULL, NULL,
            "\\\",\\\"url\\\":\\\"https://evil.example/x\\\"}");
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &st), 0);

    ASSERT_EQ(st.pending_tool_calls[0].overflowed, 1);
    ASSERT_TRUE(strstr(st.pending_tool_calls[0].input_json, "evil.example") == NULL);
    ASSERT_TRUE(strstr(st.pending_tool_calls[0].input_json, "url") == NULL);

    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_tool_round_should_run_rules(void)
{
    TEST_BEGIN();
    /* OpenAI-format providers: pending calls decide, not the finish reason. */
    ASSERT_EQ(ai_tool_round_should_run("openai", 0, 2, 0), 1);
    ASSERT_EQ(ai_tool_round_should_run("deepseek", 1, 1, 0), 1);
    ASSERT_EQ(ai_tool_round_should_run("moonshot", 0, 0, 0), 0);
    ASSERT_EQ(ai_tool_round_should_run("moonshot", 1, 0, 0), 0);
    ASSERT_EQ(ai_tool_round_should_run(NULL, 0, 1, 0), 1);
    /* finish_reason "length": the arguments are cut off, never run them. */
    ASSERT_EQ(ai_tool_round_should_run("moonshot", 0, 1, 1), 0);
    /* Anthropic keeps its tool_use stop requirement. */
    ASSERT_EQ(ai_tool_round_should_run("anthropic", 0, 1, 0), 0);
    ASSERT_EQ(ai_tool_round_should_run("anthropic", 1, 1, 0), 1);
    ASSERT_EQ(ai_tool_round_should_run("anthropic", 1, 0, 0), 0);
    TEST_END();
}

int test_ai_stream_anthropic_finish_recorded_behaviour_unchanged(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    ASSERT_EQ(ai_parse_tool_stream_anthropic(
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}",
        &st), 1);
    ASSERT_STR_EQ(st.finish, "tool_use");
    ASSERT_EQ(ai_parse_tool_stream_anthropic(
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}",
        &st), 2);
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_tools_stream_compact_drops_index_gaps(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    /* A single call at index 5: slots 0..4 are zeroed holes. */
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":5,"
        "\"id\":\"call_5\",\"type\":\"function\",\"function\":{\"name\":"
        "\"web_fetch\",\"arguments\":\"{}\"}}]},\"finish_reason\":\"stop\"}]}",
        text, sizeof(text)), 1);
    ASSERT_EQ(st.pending_tool_count, 6);
    ASSERT_EQ(ai_tools_stream_compact(&st), 5);
    ASSERT_EQ(st.pending_tool_count, 1);
    ASSERT_STR_EQ(st.pending_tool_calls[0].id, "call_5");
    ASSERT_STR_EQ(st.pending_tool_calls[0].name, "web_fetch");
    ASSERT_STR_EQ(st.pending_tool_calls[0].input_json, "{}");
    ASSERT_EQ(ai_tools_stream_compact(&st), 0);   /* idempotent */
    ai_tools_stream_reset(&st);
    TEST_END();
}

int test_ai_tools_stream_compact_keeps_order_and_only_holes(void)
{
    TEST_BEGIN();
    AiToolStreamState st;
    ai_tools_stream_init(&st);
    char text[256];
    /* Indices 0, 2 and 3 present, 1 missing; a call with a name but no id
     * (and one with an id but no name) is not a hole. */
    ASSERT_EQ(feed_ex(&st,
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"index\":0,\"id\":\"a\",\"function\":{\"name\":\"web_fetch\",\"arguments\":\"1\"}},"
        "{\"index\":2,\"function\":{\"name\":\"web_search\",\"arguments\":\"2\"}},"
        "{\"index\":3,\"id\":\"d\",\"function\":{\"arguments\":\"3\"}}]},"
        "\"finish_reason\":null}]}", text, sizeof(text)), 0);
    ASSERT_EQ(st.pending_tool_count, 4);
    ASSERT_EQ(ai_tools_stream_compact(&st), 1);
    ASSERT_EQ(st.pending_tool_count, 3);
    ASSERT_STR_EQ(st.pending_tool_calls[0].id, "a");
    ASSERT_STR_EQ(st.pending_tool_calls[1].name, "web_search");
    ASSERT_STR_EQ(st.pending_tool_calls[1].input_json, "2");
    ASSERT_STR_EQ(st.pending_tool_calls[2].id, "d");
    ASSERT_STR_EQ(st.pending_tool_calls[2].input_json, "3");
    ai_tools_stream_reset(&st);

    /* Nothing pending, NULL, no array: all 0. */
    ai_tools_stream_init(&st);
    ASSERT_EQ(ai_tools_stream_compact(&st), 0);
    ASSERT_EQ(ai_tools_stream_compact(NULL), 0);
    TEST_END();
}

int test_ai_stream_chunk_error_object_message(void)
{
    TEST_BEGIN();
    char msg[AI_STREAM_ERROR_MSG_MAX + 1];
    int rc = ai_stream_chunk_error("moonshot",
        "{\"error\":{\"message\":\"Your account is suspended\",\"type\":"
        "\"insufficient_quota\"}}", msg, sizeof(msg));
    ASSERT_EQ(rc, 1);
    ASSERT_STR_EQ(msg, "Your account is suspended");
    /* Without a message, the type names it. */
    rc = ai_stream_chunk_error("openai",
        "{\"error\":{\"type\":\"server_error\"}}", msg, sizeof(msg));
    ASSERT_EQ(rc, 1);
    ASSERT_STR_EQ(msg, "server_error");
    /* No message, no type. */
    rc = ai_stream_chunk_error("openai", "{\"error\":{}}", msg, sizeof(msg));
    ASSERT_EQ(rc, 1);
    ASSERT_STR_EQ(msg, "unknown error");
    TEST_END();
}

int test_ai_stream_chunk_error_string_form(void)
{
    TEST_BEGIN();
    char msg[AI_STREAM_ERROR_MSG_MAX + 1];
    int rc = ai_stream_chunk_error("deepseek",
        "{\"error\":\"rate limit exceeded\"}", msg, sizeof(msg));
    ASSERT_EQ(rc, 1);
    ASSERT_STR_EQ(msg, "rate limit exceeded");
    TEST_END();
}

int test_ai_stream_chunk_error_not_an_error(void)
{
    TEST_BEGIN();
    char msg[AI_STREAM_ERROR_MSG_MAX + 1] = "keep";
    ASSERT_EQ(ai_stream_chunk_error("moonshot",
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"error\"}}]}",
        msg, sizeof(msg)), 0);
    ASSERT_EQ(ai_stream_chunk_error("moonshot", "{\"error\":null}",
                                    msg, sizeof(msg)), 0);
    ASSERT_EQ(ai_stream_chunk_error("moonshot", "[DONE]", msg, sizeof(msg)), 0);
    ASSERT_EQ(ai_stream_chunk_error("moonshot", "", msg, sizeof(msg)), 0);
    ASSERT_EQ(ai_stream_chunk_error("moonshot", NULL, msg, sizeof(msg)), 0);
    /* Anthropic's own error events are not this path's business. */
    ASSERT_EQ(ai_stream_chunk_error("anthropic",
        "{\"type\":\"error\",\"error\":{\"message\":\"overloaded\"}}",
        msg, sizeof(msg)), 0);
    /* NULL out buffer is allowed. */
    ASSERT_EQ(ai_stream_chunk_error("moonshot", "{\"error\":{\"message\":\"x\"}}",
                                    NULL, 0), 1);
    TEST_END();
}

int test_ai_stream_chunk_error_sanitised_and_capped(void)
{
    TEST_BEGIN();
    char msg[AI_STREAM_ERROR_MSG_MAX + 1];
    /* ESC, BEL, newline, a bidi override (U+202E), a C1 control (U+0085). */
    int rc = ai_stream_chunk_error("moonshot",
        "{\"error\":{\"message\":\"bad\\u001b[31m key\\u0007\\nnow\\u202eevil\\u0085!\"}}",
        msg, sizeof(msg));
    ASSERT_EQ(rc, 1);
    ASSERT_STR_EQ(msg, "bad[31m key nowevil!");
    for (const char *p = msg; *p; p++) {
        ASSERT_TRUE((unsigned char)*p >= 0x20 && *p != 0x7F);
    }

    /* U+061C ARABIC LETTER MARK is dropped; U+2028/U+2029 become a space. */
    rc = ai_stream_chunk_error("moonshot",
        "{\"error\":{\"message\":\"a\\u061cb\\u2028c\\u2029d\"}}", msg, sizeof(msg));
    ASSERT_EQ(rc, 1);
    ASSERT_STR_EQ(msg, "ab c d");

    /* A long message is cut to AI_STREAM_ERROR_MSG_MAX bytes. */
    char json[1024];
    size_t n = (size_t)snprintf(json, sizeof(json), "{\"error\":{\"message\":\"");
    for (int i = 0; i < 600; i++) json[n++] = 'a';
    n += (size_t)snprintf(json + n, sizeof(json) - n, "\"}}");
    rc = ai_stream_chunk_error("moonshot", json, msg, sizeof(msg));
    ASSERT_EQ(rc, 1);
    ASSERT_EQ(strlen(msg), (size_t)AI_STREAM_ERROR_MSG_MAX);

    /* The cut never splits a multi-byte character. */
    n = (size_t)snprintf(json, sizeof(json), "{\"error\":{\"message\":\"");
    for (int i = 0; i < 199; i++) json[n++] = 'a';
    n += (size_t)snprintf(json + n, sizeof(json) - n, "\xc3\xa9\xc3\xa9\"}}");
    rc = ai_stream_chunk_error("moonshot", json, msg, sizeof(msg));
    ASSERT_EQ(rc, 1);
    ASSERT_EQ(strlen(msg), (size_t)199);          /* e-acute needs 2 bytes */

    /* A small buffer is honoured. */
    char small[8];
    rc = ai_stream_chunk_error("moonshot",
        "{\"error\":{\"message\":\"abcdefghijklmnop\"}}", small, sizeof(small));
    ASSERT_EQ(rc, 1);
    ASSERT_EQ(strlen(small), (size_t)7);
    TEST_END();
}

/* ======================================================================== */
/* Streaming error handling tests                                             */
/* ======================================================================== */

int test_ai_stream_error_reset(void)
{
    TEST_BEGIN();
    AiToolStreamState state;
    ai_tools_stream_init(&state);

    /* Start accumulating a tool_use block */
    const char *start_chunk =
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"tool_use\","
        "\"id\":\"toolu_partial\",\"name\":\"web_search\"}}";
    ai_parse_tool_stream_anthropic(start_chunk, &state);

    /* Partial delta */
    const char *delta_chunk =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"input_json_delta\","
        "\"partial_json\":\"{\\\"q\\\":\\\"partial\"}}";
    ai_parse_tool_stream_anthropic(delta_chunk, &state);

    ASSERT_EQ(state.tool_call_active, 1);
    ASSERT_EQ((int)state.current_block_type, (int)BLOCK_TOOL_USE);

    /* Reset — should discard everything */
    ai_tools_stream_reset(&state);

    ASSERT_EQ(state.pending_tool_count, 0);
    ASSERT_NULL(state.pending_tool_calls);
    ASSERT_EQ((int)state.current_block_type, (int)BLOCK_NONE);
    ASSERT_EQ(state.tool_call_active, 0);

    TEST_END();
}

int test_ai_stream_anthropic_invalid_json(void)
{
    TEST_BEGIN();
    AiToolStreamState state;
    ai_tools_stream_init(&state);

    int rc = ai_parse_tool_stream_anthropic("not valid json {{{{", &state);
    ASSERT_EQ(rc, -1);

    ai_tools_stream_reset(&state);
    TEST_END();
}

/* ---- Error text goes to tool_note, except web_search's -------------------- */

/* tool_data is the text to return; the call fails (-1) with it. */
static int mock_execute_fail_with_data(const char *input_json, void *tool_data,
                                       volatile int *cancel_flag,
                                       char **result_buf, size_t *result_len,
                                       int *was_truncated)
{
    (void)cancel_flag;
    (void)input_json;
    const char *text = (const char *)tool_data;
    *result_buf = malloc(strlen(text) + 1);
    if (!*result_buf) return -1;
    strcpy(*result_buf, text);
    *result_len = strlen(text);
    *was_truncated = 0;
    return -1;
}

static int run_failing_tool(const char *name, const char *text, AiToolResult *out)
{
    AiToolRegistry reg;
    ai_tools_init(&reg);
    AiToolDef t = make_tool(name, "d", SIMPLE_SCHEMA);
    t.execute = mock_execute_fail_with_data;
    t.tool_data = (void *)text;
    ai_tools_register(&reg, &t);
    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id, sizeof(call.id), "toolu_e");
    snprintf(call.name, sizeof(call.name), "%s", name);
    return ai_tool_execute(&reg, &call, NULL, out);
}

int test_tool_execute_error_goes_to_note(void)
{
    TEST_BEGIN();
    AiToolResult r;
    ASSERT_EQ(run_failing_tool("web_fetch", "Refused: x", &r), -1);
    ASSERT_NULL(r.content);
    ASSERT_EQ(r.content_len, 0);
    ASSERT_STR_EQ(r.tool_note, "Refused: x");
    ASSERT_EQ(r.is_error, 1);

    ASSERT_EQ(run_failing_tool("read_debug_log", "call limit reached", &r), -1);
    ASSERT_NULL(r.content);
    ASSERT_STR_EQ(r.tool_note, "call limit reached");

    /* unknown tool name */
    AiToolRegistry reg;
    ai_tools_init(&reg);
    AiToolCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.id, sizeof(call.id), "toolu_u");
    snprintf(call.name, sizeof(call.name), "nope");
    ASSERT_EQ(ai_tool_execute(&reg, &call, NULL, &r), -1);
    ASSERT_NULL(r.content);
    ASSERT_TRUE(r.tool_note[0] != '\0');
    ASSERT_EQ(r.is_error, 1);

    /* a long error is cut to 255 bytes on a UTF-8 boundary */
    char longtxt[400];
    memset(longtxt, 'x', sizeof(longtxt));
    longtxt[sizeof(longtxt) - 1] = '\0';
    /* a 3-byte euro sign straddling byte 255: bytes 253..255 */
    memcpy(longtxt + 253, "\xE2\x82\xAC", 3);
    ASSERT_EQ(run_failing_tool("web_fetch", longtxt, &r), -1);
    ASSERT_EQ(strlen(r.tool_note), 253);
    TEST_END();
}

int test_tool_execute_web_search_error_stays_content(void)
{
    TEST_BEGIN();
    AiToolResult r;
    ASSERT_EQ(run_failing_tool("web_search", "HTTP 500: body", &r), -1);
    ASSERT_NOT_NULL(r.content);
    if (r.content) ASSERT_STR_EQ(r.content, "HTTP 500: body");
    ASSERT_EQ(r.content_len, 14);
    ASSERT_STR_EQ(r.tool_note, "");
    ASSERT_EQ(r.is_error, 1);
    free(r.content);
    TEST_END();
}

/* ---- ai_tool_card_text ------------------------------------------------- */

int test_ai_tool_card_text_web_fetch(void)
{
    TEST_BEGIN();
    AiToolCall call;
    char row[256], detail[1024];

    memset(&call, 0, sizeof call);
    snprintf(call.id, sizeof call.id, "toolu_1");
    snprintf(call.name, sizeof call.name, "web_fetch");
    snprintf(call.input_json, sizeof call.input_json,
             "{\"url\":\"https://u@a.test/x?q=1\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(row, "web_fetch: https://a.test/x?\xE2\x80\xA6");
    ASSERT_STR_EQ(detail, "https://a.test/x?q=1");

    /* no query: no mark */
    snprintf(call.input_json, sizeof call.input_json, "{\"url\":\"https://a.test/x\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(row, "web_fetch: https://a.test/x");
    ASSERT_STR_EQ(detail, "https://a.test/x");

    /* a control (JSON \u001b) in the URL: removed from the row, escaped in the detail */
    snprintf(call.input_json, sizeof call.input_json,
             "{\"url\":\"https://a.test/a\\u001bb\\u202ec\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(row, "web_fetch: https://a.test/ab\xEF\xBF\xBD" "c");
    ASSERT_STR_EQ(detail, "https://a.test/a\\x1Bb\\u202Ec");

    /* invisible characters (JSON \u escapes and raw bytes): never raw on the
     * card, U+FFFD in the row, an escape in Details */
    snprintf(call.input_json, sizeof call.input_json,
             "{\"url\":\"https://a.test/p\\u200ba\\ufeffb\\u00adc\\ud800\\udc00d\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_TRUE(strstr(row, "\xE2\x80\x8B") == NULL);
    ASSERT_TRUE(strstr(detail, "\xE2\x80\x8B") == NULL);
    ASSERT_TRUE(strstr(row, "\xEF\xBB\xBF") == NULL);
    ASSERT_TRUE(strstr(detail, "\xEF\xBB\xBF") == NULL);
    ASSERT_TRUE(strstr(row, "\xC2\xAD") == NULL);
    ASSERT_TRUE(strstr(detail, "\xC2\xAD") == NULL);
    ASSERT_TRUE(strstr(row, "\xEF\xBF\xBD") != NULL);
    snprintf(call.input_json, sizeof call.input_json,
             "{\"url\":\"https://a.test/p\xE2\x80\x8B" "a\xEF\xBB\xBF" "b\xC2\xAD" "c\xF3\xA0\x81\x81" "d\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(row, "web_fetch: https://a.test/p\xEF\xBF\xBD" "a\xEF\xBF\xBD" "b\xEF\xBF\xBD" "c\xEF\xBF\xBD" "d");
    ASSERT_STR_EQ(detail, "https://a.test/p\\u200Ba\\uFEFFb\\u00ADc\\U000E0041d");
    /* an invalid UTF-8 byte: U+FFFD in the row, \xNN in Details */
    snprintf(call.input_json, sizeof call.input_json, "{\"url\":\"https://a.test/a\xFF" "b\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(row, "web_fetch: https://a.test/a\xEF\xBF\xBD" "b");
    ASSERT_STR_EQ(detail, "https://a.test/a\\xFFb");

    /* a schemeless URL quoting "://" in its path still loses its userinfo */
    snprintf(call.input_json, sizeof call.input_json,
             "{\"url\":\"user:pw@host/p://x\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(row, "web_fetch: host/p://x");
    ASSERT_STR_EQ(detail, "host/p://x");

    /* the query never reaches the row, even a long one */
    snprintf(call.input_json, sizeof call.input_json,
             "{\"url\":\"https://a.test/?token=hunter2&x=1\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_TRUE(strstr(row, "hunter2") == NULL);
    ASSERT_TRUE(strstr(detail, "hunter2") != NULL);

    /* missing, empty or non-string url, malformed JSON */
    snprintf(call.input_json, sizeof call.input_json, "{}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), -1);
    ASSERT_STR_EQ(row, "web_fetch: (no url)");
    ASSERT_STR_EQ(detail, "");
    snprintf(call.input_json, sizeof call.input_json, "{\"url\":\"\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), -1);
    ASSERT_STR_EQ(row, "web_fetch: (no url)");
    snprintf(call.input_json, sizeof call.input_json, "{\"url\":42}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), -1);
    ASSERT_STR_EQ(row, "web_fetch: (no url)");
    snprintf(call.input_json, sizeof call.input_json, "not json");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), -1);
    ASSERT_STR_EQ(row, "web_fetch: (no url)");

    /* NULL call; NULL or zero-size buffers */
    ASSERT_EQ(ai_tool_card_text(NULL, row, sizeof row, detail, sizeof detail), -1);
    ASSERT_STR_EQ(row, "");
    ASSERT_STR_EQ(detail, "");
    snprintf(call.input_json, sizeof call.input_json, "{\"url\":\"https://a.test/x\"}");
    ASSERT_EQ(ai_tool_card_text(&call, NULL, 0, detail, sizeof detail), 0);
    ASSERT_STR_EQ(detail, "https://a.test/x");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, NULL, 0), 0);
    ASSERT_STR_EQ(row, "web_fetch: https://a.test/x");

    /* a small row buffer: NUL-terminated */
    memset(row, 'x', sizeof row);
    ai_tool_card_text(&call, row, 8, detail, sizeof detail);
    ASSERT_TRUE(strlen(row) < 8);
    TEST_END();
}

int test_ai_tool_card_text_other_tool(void)
{
    TEST_BEGIN();
    AiToolCall call;
    char row[128], detail[1024];

    memset(&call, 0, sizeof call);
    snprintf(call.name, sizeof call.name, "custom");
    snprintf(call.input_json, sizeof call.input_json, "{\"a\":\"\\u0001\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(row, "custom");
    ASSERT_STR_EQ(detail, "{\"a\":\"\\u0001\"}");

    /* a raw control byte in the input is escaped */
    snprintf(call.input_json, sizeof call.input_json, "{\"a\":\"x\x1B[2Jy\xE2\x80\xAE\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(detail, "{\"a\":\"x\\x1B[2Jy\\u202E\"}");

    /* the same rule as a URL: invisible, tag and invalid bytes are escaped */
    snprintf(call.input_json, sizeof call.input_json,
             "{\"a\":\"p\xE2\x80\x8B\xEF\xBB\xBF\xC2\xAD\xF3\xA0\x81\x81\xFF\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(detail, "{\"a\":\"p\\u200B\\uFEFF\\u00AD\\U000E0041\\xFF\"}");

    /* a hostile tool name never puts a control or bidi byte in the row */
    snprintf(call.name, sizeof call.name, "ev\x1B[2Jil\xE2\x80\xAE" "x\n");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(row, "ev[2Jilx");

    /* a tool named like web_fetch but longer is another tool */
    snprintf(call.name, sizeof call.name, "web_fetch2");
    snprintf(call.input_json, sizeof call.input_json, "{\"url\":\"https://a.test/?q=1\"}");
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(row, "web_fetch2");

    /* a long input is cut with the ellipsis at the detail's capacity */
    snprintf(call.name, sizeof call.name, "custom");
    {
        char *p = call.input_json;
        size_t i;
        p[0] = '{';
        for (i = 1; i < 3000; i++) p[i] = 'z';
        p[3000] = '\0';
        char d[100];
        ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, d, sizeof d), 0);
        ASSERT_EQ(strlen(d), (size_t)99);
        ASSERT_STR_EQ(d + 96, "\xE2\x80\xA6");
    }

    /* an empty input */
    call.input_json[0] = '\0';
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    ASSERT_STR_EQ(detail, "");
    TEST_END();
}

/* The UI sizes a card's Details at CMD_NOTE_MAX (stream_post_tool_decide), so
 * the escape-aware cut in ai_tool_card_text fires and chat_approval_add_tool
 * copies the result whole: its own cut (UTF-8 continuation bytes only) must
 * never see an over-long detail and split a \UXXXXXXXX escape. */
int test_ai_tool_card_text_detail_fits_approval_note(void)
{
    TEST_BEGIN();
    AiToolCall call;
    char row[256], detail[CMD_NOTE_MAX];
    char url[600] = "https://a.test/";
    size_t i, n = strlen(url);

    for (i = 0; i < 40; i++) {                 /* 40 x U+E0041 (F3 A0 81 81) */
        memcpy(url + n, "\xF3\xA0\x81\x81", 4);
        n += 4;
    }
    url[n] = '\0';
    memset(&call, 0, sizeof call);
    snprintf(call.id, sizeof call.id, "toolu_1");
    snprintf(call.name, sizeof call.name, "web_fetch");
    snprintf(call.input_json, sizeof call.input_json, "{\"url\":\"%s\"}", url);

    /* at the real cap: whole escapes, then the ellipsis */
    ASSERT_EQ(ai_tool_card_text(&call, row, sizeof row, detail, sizeof detail), 0);
    {
        size_t len = strlen(detail);
        ASSERT_TRUE(len < (size_t)CMD_NOTE_MAX);
        ASSERT_TRUE(len > 13);
        ASSERT_STR_EQ(detail + len - 3, "\xE2\x80\xA6");
        ASSERT_TRUE(memcmp(detail + len - 13, "\\U000E0041", 10) == 0);
    }

    /* chat_approval_add_tool keeps it byte for byte: no second cut */
    {
        ApprovalQueue q;
        chat_approval_init(&q);
        ASSERT_EQ(chat_approval_add_tool(&q, row, detail, CMD_UNKNOWN), 0);
        ASSERT_STR_EQ(q.entries[0].note, detail);
    }
    TEST_END();
}

int test_runcmd_serialized_both_formats(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    static char buf[4096];
    ai_tools_init(&reg);
    ASSERT_EQ(runcmd_register(&reg), 0);
    ASSERT_EQ(reg.count, 1);
    ASSERT_EQ(ai_tools_validate_schemas(&reg), 0);

    const AiToolDef *t = ai_tools_find(&reg, RUNCMD_TOOL_NAME);
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_EQ((int)t->safety, (int)TOOL_CRITICAL);
        ASSERT_STR_EQ(t->input_schema_json,
            "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\","
            "\"description\":\"one single-line command, at most 1023 bytes\"},"
            "\"note\":{\"type\":\"string\",\"description\":\"one line for the user: "
            "what the command does and why\"}},\"required\":[\"command\"]}");
    }

    ASSERT_TRUE(ai_tools_serialize_anthropic(&reg, buf, sizeof buf) > 0);
    ASSERT_NOT_NULL(strstr(buf, "\"name\":\"run_command\""));
    ASSERT_NOT_NULL(strstr(buf, "\"input_schema\""));

    ASSERT_TRUE(ai_tools_serialize_openai(&reg, buf, sizeof buf) > 0);
    ASSERT_NOT_NULL(strstr(buf, "\"type\":\"function\""));
    ASSERT_NOT_NULL(strstr(buf, "\"name\":\"run_command\""));
    ASSERT_NOT_NULL(strstr(buf, "\"required\":[\"command\"]"));

    ASSERT_EQ(runcmd_register(NULL), -1);
    TEST_END();
}

int test_runcmd_stub_is_error(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);
    ASSERT_EQ(runcmd_register(&reg), 0);

    AiToolCall call;
    memset(&call, 0, sizeof call);
    snprintf(call.id, sizeof call.id, "toolu_1");
    snprintf(call.name, sizeof call.name, "%s", RUNCMD_TOOL_NAME);
    snprintf(call.input_json, sizeof call.input_json, "{\"command\":\"ls\"}");

    AiToolResult r;
    int rc = ai_tool_execute(&reg, &call, NULL, &r);
    ASSERT_TRUE(rc != 0);
    ASSERT_EQ(r.is_error, 1);
    ASSERT_NOT_NULL(strstr(r.tool_note, "handled by the panel"));
    ASSERT_NULL(r.content);   /* Nutshell's own text: note, not data */
    free(r.content);
    TEST_END();
}

/* ======================================================================== */
/* Gemini thought signatures (OpenAI-compatible stream)                       */
/* ======================================================================== */

int test_ai_stream_gemini_signature_captured(void)
{
    TEST_BEGIN();
    AiToolStreamState state;
    ai_tools_stream_init(&state);
    ASSERT_NULL(state.sig);

    /* no "index" (Gemini leaves it out), the signature on the call itself */
    const char *chunk =
        "{\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\","
        "\"tool_calls\":[{\"id\":\"function-call-1\",\"type\":\"function\","
        "\"function\":{\"name\":\"web_search\",\"arguments\":\"{\\\"query\\\":\\\"x\\\"}\"},"
        "\"extra_content\":{\"google\":{\"thought_signature\":\"c2ln\"}}}]}}]}";
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &state), 0);
    ASSERT_EQ(state.pending_tool_count, 1);
    ASSERT_STR_EQ(state.pending_tool_calls[0].name, "web_search");
    ASSERT_NOT_NULL(state.sig);
    ASSERT_STR_EQ(state.sig, "c2ln");
    ASSERT_EQ((int)state.sig_len, 4);

    /* a later chunk of the same response, with another signature: ignored */
    const char *later =
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"function\":{\"arguments\":\"\"},"
        "\"extra_content\":{\"google\":{\"thought_signature\":\"b3RoZXI=\"}}}]}}]}";
    ASSERT_EQ(ai_parse_tool_stream_openai(later, &state), 0);
    ASSERT_STR_EQ(state.sig, "c2ln");

    /* the reset frees it */
    ai_tools_stream_reset(&state);
    ASSERT_NULL(state.sig);
    ASSERT_EQ((int)state.sig_len, 0);
    ASSERT_EQ((int)state.sig_cap, 0);
    TEST_END();
}

int test_ai_stream_gemini_parallel_first_only(void)
{
    TEST_BEGIN();
    AiToolStreamState state;
    ai_tools_stream_init(&state);

    /* two index-less calls in one chunk: the first carries the signature; a
     * signature on the second (which Gemini does not send) is not taken */
    const char *chunk =
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"id\":\"a\",\"type\":\"function\","
        "\"function\":{\"name\":\"web_search\",\"arguments\":\"{}\"},"
        "\"extra_content\":{\"google\":{\"thought_signature\":\"Zmlyc3Q=\"}}},"
        "{\"id\":\"b\",\"type\":\"function\","
        "\"function\":{\"name\":\"web_fetch\",\"arguments\":\"{}\"},"
        "\"extra_content\":{\"google\":{\"thought_signature\":\"c2Vjb25k\"}}}]}}]}";
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &state), 0);
    ASSERT_EQ(state.pending_tool_count, 2);
    ASSERT_STR_EQ(state.pending_tool_calls[0].id, "a");
    ASSERT_STR_EQ(state.pending_tool_calls[1].id, "b");
    ASSERT_STR_EQ(state.sig, "Zmlyc3Q=");

    /* a second call without one (the usual case) leaves the first's alone */
    ai_tools_stream_reset(&state);
    const char *usual =
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"id\":\"a\",\"type\":\"function\","
        "\"function\":{\"name\":\"web_search\",\"arguments\":\"{}\"},"
        "\"extra_content\":{\"google\":{\"thought_signature\":\"Zmlyc3Q=\"}}},"
        "{\"id\":\"b\",\"type\":\"function\","
        "\"function\":{\"name\":\"web_fetch\",\"arguments\":\"{}\"}}]}}]}";
    ASSERT_EQ(ai_parse_tool_stream_openai(usual, &state), 0);
    ASSERT_EQ(state.pending_tool_count, 2);
    ASSERT_STR_EQ(state.sig, "Zmlyc3Q=");

    /* no signature anywhere: none */
    ai_tools_stream_reset(&state);
    const char *none =
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"id\":\"a\",\"type\":\"function\","
        "\"function\":{\"name\":\"web_search\",\"arguments\":\"{}\"}}]}}]}";
    ASSERT_EQ(ai_parse_tool_stream_openai(none, &state), 0);
    ASSERT_NULL(state.sig);
    ASSERT_EQ((int)state.sig_len, 0);
    ai_tools_stream_reset(&state);
    TEST_END();
}

int test_ai_stream_gemini_long_signature_kept(void)
{
    TEST_BEGIN();
    enum { SIG_LEN = 20 * 1024 };
    char *sig = (char *)malloc(SIG_LEN + 1);
    ASSERT_NOT_NULL(sig);
    for (int i = 0; i < SIG_LEN; i++) sig[i] = (char)('A' + i % 26);
    sig[SIG_LEN] = '\0';

    const char *head =
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"a\","
        "\"type\":\"function\",\"function\":{\"name\":\"web_search\",\"arguments\":\"{}\"},"
        "\"extra_content\":{\"google\":{\"thought_signature\":\"";
    const char *tail = "\"}}}]}}]}";
    char *chunk = (char *)malloc(strlen(head) + SIG_LEN + strlen(tail) + 1);
    ASSERT_NOT_NULL(chunk);
    sprintf(chunk, "%s%s%s", head, sig, tail);

    AiToolStreamState state;
    ai_tools_stream_init(&state);
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &state), 0);
    ASSERT_EQ(state.pending_tool_count, 1);
    ASSERT_NOT_NULL(state.sig);
    ASSERT_EQ((int)state.sig_len, SIG_LEN);
    ASSERT_TRUE(state.sig_cap > state.sig_len);
    ASSERT_TRUE(strcmp(state.sig, sig) == 0);

    ai_tools_stream_reset(&state);
    free(chunk);
    free(sig);
    TEST_END();
}

int test_ai_stream_gemini_long_signature_escapes(void)
{
    TEST_BEGIN();
    /* a signature past the JSON parser's string limit, with base64 slashes
     * escaped as \/ and a + plus: decoded, and the rest of the chunk
     * (the call, a second call) still read */
    enum { RAW = 12000 };
    char *esc = (char *)malloc(RAW * 2 + 64);
    char *want = (char *)malloc(RAW + 8);
    ASSERT_NOT_NULL(esc);
    ASSERT_NOT_NULL(want);
    size_t e = 0, w = 0;
    for (int i = 0; i < RAW; i++) {
        if (i % 50 == 7)       { memcpy(esc + e, "\\/", 2); e += 2; want[w++] = '/'; }
        else if (i % 50 == 9)  { memcpy(esc + e, "\\u002b", 6); e += 6; want[w++] = '+'; }
        else                   { esc[e++] = want[w++] = (char)('a' + i % 26); }
    }
    esc[e] = '\0';
    want[w] = '\0';

    const char *head =
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
        "{\"id\":\"a\",\"type\":\"function\","
        "\"function\":{\"name\":\"web_search\",\"arguments\":\"{}\"},"
        "\"extra_content\":{\"google\":{\"thought_signature\":\"";
    const char *tail =
        "\"}}},{\"id\":\"b\",\"type\":\"function\","
        "\"function\":{\"name\":\"web_fetch\",\"arguments\":\"{}\"}}]}}]}";
    char *chunk = (char *)malloc(strlen(head) + e + strlen(tail) + 1);
    ASSERT_NOT_NULL(chunk);
    sprintf(chunk, "%s%s%s", head, esc, tail);

    AiToolStreamState state;
    ai_tools_stream_init(&state);
    ASSERT_EQ(ai_parse_tool_stream_openai(chunk, &state), 0);
    ASSERT_EQ(state.pending_tool_count, 2);
    ASSERT_STR_EQ(state.pending_tool_calls[1].name, "web_fetch");
    ASSERT_NOT_NULL(state.sig);
    ASSERT_EQ((int)state.sig_len, (int)w);
    ASSERT_TRUE(strcmp(state.sig, want) == 0);
    ai_tools_stream_reset(&state);

    /* a long string that is not a signature, or an escape it cannot decode,
     * is still a parse failure */
    char *bad = (char *)malloc(strlen(head) + 20000 + strlen(tail) + 8);
    ASSERT_NOT_NULL(bad);
    size_t b = (size_t)sprintf(bad, "%s", head);
    memset(bad + b, 'q', 9000);
    b += 9000;
    memcpy(bad + b, "\\ud83d", 6);
    b += 6;
    sprintf(bad + b, "%s", tail);
    ai_tools_stream_init(&state);
    ASSERT_EQ(ai_parse_tool_stream_openai(bad, &state), -1);
    ASSERT_NULL(state.sig);
    ai_tools_stream_reset(&state);

    free(bad);
    free(chunk);
    free(esc);
    free(want);
    TEST_END();
}
