#include "test_framework.h"
#include "ai_tools.h"
#include "json_parser.h"
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
    ASSERT_NOT_NULL(result.content);
    ASSERT_NOT_NULL(strstr(result.content, "Unknown tool"));
    ASSERT_NOT_NULL(strstr(result.content, "no_such_tool"));

    free(result.content);
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
