#ifndef NUTSHELL_AI_TOOLS_H
#define NUTSHELL_AI_TOOLS_H

#include <stddef.h>

#define AI_TOOL_NAME_MAX       64
#define AI_TOOL_MAX            16
#define AI_TOOL_ID_MAX         64
#define AI_TOOL_INPUT_MAX      4096
#define AI_TOOL_RESULT_MAX     (1024 * 1024)  /* 1MB dynamic buffer cap */
#define AI_TOOL_DESC_MAX       512
#define AI_TOOL_SCHEMA_MAX     2048

#define AI_TOOL_LOOP_MAX       5
#define AI_TOOL_SEARCH_MAX     3

#define AI_TOOL_USER_AGENT     "Nutshell/1.0 (Windows NT 10.0; Win64; x64)"

typedef enum {
    TOOL_SAFE,
    TOOL_WRITE,
    TOOL_CRITICAL
} ToolSafety;

typedef struct {
    char name[AI_TOOL_NAME_MAX];
    char description[AI_TOOL_DESC_MAX];
    char input_schema_json[AI_TOOL_SCHEMA_MAX];
    ToolSafety safety;
    int (*execute)(const char *input_json, void *tool_data,
                   volatile int *cancel_flag,
                   char **result_buf, size_t *result_len, int *was_truncated);
    void *tool_data;
} AiToolDef;

typedef struct {
    char id[AI_TOOL_ID_MAX];
    char name[AI_TOOL_NAME_MAX];
    char input_json[AI_TOOL_INPUT_MAX];
} AiToolCall;

typedef struct {
    char tool_use_id[AI_TOOL_ID_MAX];
    char *content;       /* heap-allocated, caller frees */
    size_t content_len;
    int is_error;
    int was_truncated;
} AiToolResult;

typedef struct {
    AiToolDef tools[AI_TOOL_MAX];
    int count;
} AiToolRegistry;

/* Registry */
void ai_tools_init(AiToolRegistry *reg);
int  ai_tools_register(AiToolRegistry *reg, const AiToolDef *tool);
const AiToolDef *ai_tools_find(const AiToolRegistry *reg, const char *name);

/* Serialization */
int  ai_tools_serialize_anthropic(const AiToolRegistry *reg, char *buf, size_t max);
int  ai_tools_serialize_openai(const AiToolRegistry *reg, char *buf, size_t max);

/* Parse tool calls from non-streaming response */
int  ai_parse_tool_calls_anthropic(const char *response_json, AiToolCall *calls, int max_calls);
int  ai_parse_tool_calls_openai(const char *response_json, AiToolCall *calls, int max_calls);

/* Execute a tool call */
int  ai_tool_execute(const AiToolRegistry *reg, const AiToolCall *call,
                     volatile int *cancel_flag, AiToolResult *result);

/* Build tool_result messages */
int  ai_tool_result_anthropic(const AiToolResult *result, char *buf, size_t max);
int  ai_tool_result_openai(const AiToolResult *result, char *buf, size_t max);

/* Schema validation */
int  ai_tools_validate_schemas(const AiToolRegistry *reg);

/* ---- read_debug_log (the debug log, for AI Assist) ------------------------ */

/* Registered only while "Enable debugging" is on at the moment a reply is
 * launched (spec 2026-10-17-debug-logging-design.md section 8). The query
 * engine, schema and limits live in dlog_query.h; this is the wiring.
 *
 * Threads: the context is owned by one AiStream (ai_stream_copy_tools()
 * rebinds the registry's pointer to the stream's own copy before the thread
 * starts), so the stream thread never reaches the panel. The counter is
 * per stream, which is per user message. */
typedef struct {
    int calls;                  /* read_debug_log calls so far in this message */
} DebugLogToolContext;

#define AI_DEBUG_TOOL_LIMIT_MSG "call limit reached for this message"

void ai_debug_tool_ctx_init(DebugLogToolContext *ctx);

/* Add the tool (TOOL_SAFE, tool_data = ctx). 0 on success, -1 if the
 * registry is full or an argument is NULL. */
int  ai_debug_tool_register(AiToolRegistry *reg, DebugLogToolContext *ctx);

/* The same, but only while dlog_enabled(). Returns 1 when registered. */
int  ai_debug_tool_register_if_enabled(AiToolRegistry *reg, DebugLogToolContext *ctx);

/* The tool body (worker thread). Counts the call, refuses the one past
 * DLOG_TOOL_MAX_CALLS_PER_MSG, parses the model's arguments, flushes the log
 * and returns the query result. A refusal returns non-zero with a short text
 * in *result_buf. The log is never copied anywhere but the result. */
int  ai_debug_tool_execute(const char *input_json, void *tool_data,
                           volatile int *cancel_flag,
                           char **result_buf, size_t *result_len,
                           int *was_truncated);

/* What the "using tool:" row shows after the name: the query in words
 * ("render, min debug, around issue 3"). Returns 1, or 0 when the arguments
 * are refused (out then says so). */
int  ai_debug_tool_describe(const char *input_json, char *out, size_t cap);

/* The result row: the first line of a result (its `# ...` header), cut to
 * cap - 1 bytes, with no line break. Empty for NULL or an empty result. */
void ai_debug_tool_summary_line(const char *result, size_t len, char *out, size_t cap);

/* The sentences added to the system prompt while the tool is registered. */
const char *ai_debug_tool_prompt_note(void);

/* ---- Streaming tool detection -------------------------------------------- */

/* Stream content block type — tracks what kind of block we're inside */
typedef enum {
    BLOCK_NONE,       /* not inside any content block */
    BLOCK_TEXT,       /* inside a text content block — deltas go to display */
    BLOCK_TOOL_USE    /* inside a tool_use block — deltas go to accumulation buffer */
} AiStreamBlockType;

/* Streaming tool accumulation state */
typedef struct {
    AiStreamBlockType current_block_type;
    AiToolCall active_tool_call;   /* tool call being accumulated from stream deltas */
    int        tool_call_active;   /* 1 while accumulating a tool_use block */

    /* Accumulated tool calls for current response */
    AiToolCall *pending_tool_calls;  /* heap array, grown as tool_use blocks complete */
    int         pending_tool_count;
    int         pending_tool_cap;
} AiToolStreamState;

/* Streaming tool detection — Anthropic format */
int ai_parse_tool_stream_anthropic(const char *chunk_json, AiToolStreamState *state);

/* Streaming tool detection — OpenAI format */
int ai_parse_tool_stream_openai(const char *chunk_json, AiToolStreamState *state);

/* Reset/discard all partial streaming state (on error or cancellation) */
void ai_tools_stream_reset(AiToolStreamState *state);

/* Initialize streaming state */
void ai_tools_stream_init(AiToolStreamState *state);

#endif /* NUTSHELL_AI_TOOLS_H */
