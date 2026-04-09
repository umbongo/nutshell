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
