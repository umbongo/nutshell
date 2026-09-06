# AI Tooling — Specification & Architecture Plan

**Author:** Opus (spec/plan) + Sonnet (implementation)
**Branch:** `ai_tooling`
**Date:** 2026-04-09
**Status:** Implemented (ai_tools.c, ai_agentic.c, web search + web fetch tools, 2026-04). The `[EXEC]` marker path still coexists with native tool use; retiring it is a separate piece of work. Historical spec — moved from the repo root 2026-09-07.

---

## 1. Problem Statement

Nutshell's AI assistant currently uses a bespoke `[EXEC]...[/EXEC]` marker system for command execution on remote SSH sessions. This works well for terminal commands but cannot be extended to other capabilities (web search, file retrieval, calculations, etc.) without fragile string-parsing hacks.

The AI needs a **formal tool-use framework** that:
- Starts with **web search** as the first tool
- Scales to additional tools without protocol changes
- Uses **native API tool-calling** (Anthropic `tool_use` content blocks / OpenAI `tool_calls`) instead of marker parsing
- Preserves the existing `[EXEC]` command system during migration
- Runs tools **locally inside Nutshell** (the Windows client), not server-side

---

## 2. Research Summary

### 2.1 Current Architecture

| Component | Location | Role |
|---|---|---|
| `ai_prompt.c/h` | `src/core/` | Conversation management, request body building, stream chunk parsing |
| `ai_http.h` | `src/core/` | HTTP interface (generic headers, streaming) |
| `ai_http_win.c` | `src/ui/` | WinHTTP implementation |
| `ai_chat.c` | `src/ui/` | UI panel, streaming thread, command approval queue |
| `cmd_classify.c/h` | `src/core/` | Command safety classification |
| `chat_approval.c/h` | `src/core/` | Approval queue for `[EXEC]` commands |

**Key observations:**
- System prompt tells AI to wrap commands in `[EXEC]...[/EXEC]` markers
- `ai_extract_commands()` parses markers from response text
- `ai_parse_stream_chunk()` handles both Anthropic and OpenAI SSE formats
- Request body is built with manual `snprintf` JSON construction
- No existing concept of "tools" — all tool-like behaviour is string-parsed

### 2.2 Native API Tool Use Formats

**Anthropic Messages API:**
```json
// Request: tool definitions
{ "tools": [{ "name": "web_search", "description": "...", "input_schema": {...} }] }

// Response: tool_use content block
{ "type": "tool_use", "id": "toolu_01...", "name": "web_search", "input": {"query": "..."} }

// Client sends back: tool_result
{ "type": "tool_result", "tool_use_id": "toolu_01...", "content": "..." }

// Streaming: input_json_delta events build up partial JSON for tool input
// stop_reason: "tool_use" when model wants to call a tool
```

**OpenAI-compatible (used by DeepSeek, Moonshot, Gemini, OpenAI):**
```json
// Request: tool definitions in "tools" array with "function" wrappers
{ "tools": [{ "type": "function", "function": { "name": "web_search", "description": "...", "parameters": {...} } }] }

// Response: tool_calls in assistant message
{ "tool_calls": [{ "id": "call_...", "type": "function", "function": { "name": "web_search", "arguments": "{...}" } }] }

// Client sends back: role "tool" message
{ "role": "tool", "tool_call_id": "call_...", "content": "..." }
```

### 2.3 Build vs Buy: Web Search

| Option | Pros | Cons |
|---|---|---|
| **DuckDuckGo API** (`api.duckduckgo.com`) | Free, no API key, structured JSON, stable endpoint | Limited to instant answers; no full web results for all queries |
| **DuckDuckGo HTML** (`html.duckduckgo.com`) | Free, no API key, full web results | HTML scraping — fragile if DDG changes undocumented layout |
| **Tavily API** | Purpose-built for AI agents, clean JSON results, fast | Paid ($0.001/query), acquired by Nebius, external dependency |
| **SearXNG (self-hosted)** | Free, no limits, privacy-focused, multiple engines | User must self-host, more setup |
| **Anthropic server-side web_search** | Zero client code, Anthropic handles it | Anthropic-only, no control, uses server tool format |

**Decision: DuckDuckGo API as primary, HTML scraping as fallback, with pluggable provider.**

Rationale:
- `api.duckduckgo.com` as **primary**: stable structured JSON, no scraping fragility — but limited to instant answers (many general queries return empty)
- `html.duckduckgo.com` as **fallback**: full web results when the API returns nothing, accepts the scraping fragility risk only when needed
- Free, no API key required — zero friction for users out of the box
- "Custom" option allows power users to point at SearXNG, Tavily, Serper, etc.
- Client-side execution means tools work with ALL AI providers (Anthropic, OpenAI, etc.)
- We already have WinHTTP — adding HTTP GET + HTML parsing is minimal new code
- **Degradation path**: API empty → HTML scraping attempted → if HTML also fails (layout change, CAPTCHA) → user gets "no results found" with a clear message

### 2.4 Web Fetch

The AI also needs the ability to **fetch arbitrary URLs** and return their content. This is distinct from web search:

| Concern | Decision |
|---|---|
| **Use case** | AI retrieves a specific page the user or search results reference |
| **Implementation** | WinHTTP GET request, strip HTML tags, return plain text (dynamic buffer, max 1MB) |
| **Safety** | Gated by a settings toggle (`ai_web_fetch_enabled`). Default: **disabled**. User must explicitly permit. No SSRF filtering — internal endpoints are permitted by design (user controls enablement). |
| **Tool safety level** | `TOOL_SAFE` when enabled (read-only, no side effects) |
| **Content handling** | Strip HTML to plain text, dynamic buffer up to 1MB, truncate with user warning if exceeded |

### 2.5 Best Practices Applied

1. **Plugin architecture** — tools are registered structs with name, schema, and execute callback
2. **Provider-agnostic** — tool definitions are translated to each provider's format at request-build time
3. **Agentic loop** — when `stop_reason` is `tool_use`, execute tools locally and send results back automatically
4. **Safety classification** — tools go through the existing approval framework (extend beyond `[EXEC]`)
5. **Progressive disclosure** — start with 2 tools, architecture supports N tools with no protocol changes
6. **Rate limiting** — max 3 search tool invocations per user message, max 5 loop iterations per user message

---

## 3. Architecture

### 3.1 Component Diagram

```
                         ┌──────────────────────────────────┐
                         │         ai_chat.c (UI)            │
                         │  streaming thread, agentic loop,  │
                         │  approval, tool registry owner    │
                         └──────────┬───────────────────────┘
                                    │
                         ┌──────────▼───────────────────────┐
                         │       ai_prompt.c (core)          │
                         │  request body (with tool defs),   │
                         │  stream parse, tool msg format    │
                         └──────────┬───────────────────────┘
                                    │
              ┌─────────────────────┼─────────────────────┐
              │                     │                     │
   ┌──────────▼────────┐           │            ┌────────▼────────┐
   │  ai_tools.c (NEW) │           │            │  ai_http.h      │
   │  tool registry     │           │            │  HTTP transport  │
   │  parse/serialize   │           │            │  (WinHTTP impl)  │
   └──────────┬────────┘           │            └─────────────────┘
              │                     │
   ┌──────────▼─────────────────────▼─────────┐
   │          Tool Implementations             │
   │  ┌───────────┐  ┌───────────┐ ┌────────┐ │
   │  │web_search │  │ web_fetch │ │(future)│ │
   │  │  (DDG)    │  │           │ │ (exec) │ │
   │  └─────┬─────┘  └─────┬─────┘ └────────┘ │
   │        └───────┬───────┘                  │
   │         ┌──────▼──────┐                   │
   │         │ html_util.c │                   │
   │         │ shared HTML │                   │
   │         │  utilities  │                   │
   │         └─────────────┘                   │
   └───────────────────────────────────────────┘
```

**Note:** The agentic loop lives in `ai_chat.c` (not a separate file) because it is
tightly coupled to the streaming thread, UI message posting, session state, and
the approval queue — all of which are already in `ai_chat.c`.

### 3.2 Core Data Structures

```c
/* src/core/ai_tools.h */

#define AI_TOOL_NAME_MAX       64
#define AI_TOOL_MAX            16
#define AI_TOOL_ID_MAX         64
#define AI_TOOL_INPUT_MAX      4096
#define AI_TOOL_RESULT_MAX     (1024 * 1024)  /* 1MB dynamic buffer cap */
#define AI_TOOL_DESC_MAX       512   /* tool description max — single #define for easy future change */
#define AI_TOOL_SCHEMA_MAX     2048  /* input schema JSON max — single #define for easy future change */

#define AI_TOOL_LOOP_MAX       5     /* max agentic loop iterations per user message */
#define AI_TOOL_SEARCH_MAX     3     /* max search tool invocations per user message */

/* Tool safety levels — mirrors cmd_classify */
typedef enum {
    TOOL_SAFE,       /* read-only, no side effects */
    TOOL_WRITE,      /* modifies external state */
    TOOL_CRITICAL    /* potentially dangerous */
} ToolSafety;

/* A registered tool definition */
typedef struct {
    char name[AI_TOOL_NAME_MAX];
    char description[AI_TOOL_DESC_MAX];
    char input_schema_json[AI_TOOL_SCHEMA_MAX];
    ToolSafety safety;

    /*
     * Execute callback. Called on background thread.
     * input_json: the parsed input object as a JSON string
     * result_buf: pointer to heap-allocated result buffer (callee sets *result_buf)
     * result_len: set by callee to actual length of result
     * Returns: 0 on success, -1 on error (write error message to *result_buf)
     *
     * The callee allocates *result_buf with malloc(). The caller frees it.
     * If the result exceeds AI_TOOL_RESULT_MAX, the callee MUST:
     *   1. Truncate to AI_TOOL_RESULT_MAX
     *   2. Set *was_truncated = 1
     * The agentic loop will surface a warning to the user when was_truncated is set.
     *
     * tool_data is stored per-tool (below) and passed automatically by
     * ai_tool_execute() — callers never need to look up the right context.
     */
    int (*execute)(const char *input_json, void *tool_data,
                   volatile int *cancel_flag,
                   char **result_buf, size_t *result_len, int *was_truncated);

    /* Tool-specific context — set at registration time, passed to execute().
     * Each tool defines its own context struct (e.g., WebSearchContext).
     * Owned by the caller (AiChatData); the registry stores the pointer only. */
    void *tool_data;
} AiToolDef;

/* A tool call parsed from an API response */
typedef struct {
    char id[AI_TOOL_ID_MAX];        /* "toolu_01..." or "call_..." */
    char name[AI_TOOL_NAME_MAX];
    char input_json[AI_TOOL_INPUT_MAX];
} AiToolCall;

/* A tool result to send back */
typedef struct {
    char tool_use_id[AI_TOOL_ID_MAX];
    char *content;       /* heap-allocated, caller frees */
    size_t content_len;
    int is_error;
    int was_truncated;   /* 1 if result was truncated to AI_TOOL_RESULT_MAX */
} AiToolResult;

/* Tool registry */
typedef struct {
    AiToolDef tools[AI_TOOL_MAX];
    int count;
} AiToolRegistry;
```

**Tool-specific context** is passed via `void *tool_data` in the execute callback. Each tool defines its own context struct:

```c
/* Web search context — passed as tool_data to tool_web_search_execute */
typedef struct {
    char search_provider[64];   /* "none", "duckduckgo-api", "duckduckgo-html", "custom" */
    char search_url[256];       /* custom search endpoint */
    int  max_search_results;    /* max results to return (default 7) */
} WebSearchContext;

/* Web fetch context — passed as tool_data to tool_web_fetch_execute */
typedef struct {
    int timeout_ms;             /* request timeout (default 10000) */
} WebFetchContext;
```

### 3.2.1 AiMessage Extension for Tool Messages

The existing `AiMessage` struct must carry tool call IDs and tool names so
`ai_build_request_body_ex()` can emit the correct format for each provider.

```c
/* Additions to ai_prompt.h */

typedef enum {
    AI_ROLE_SYSTEM,
    AI_ROLE_USER,
    AI_ROLE_ASSISTANT,
    AI_ROLE_TOOL          /* NEW — tool result message */
} AiRole;

typedef struct {
    AiRole role;

    /* Message content — inline buffer for normal messages, overflow for large content.
     * Normal text messages (user, assistant, system) use the inline buffer only.
     * Tool result messages may overflow into a heap-allocated buffer for large results.
     *
     * Readers use ai_msg_content() which returns the correct pointer transparently.
     * Writers use ai_msg_set_content() which handles allocation/overflow logic. */
    char content[AI_MSG_MAX];       /* inline buffer — fits most messages */
    char *content_overflow;         /* heap-allocated when content > AI_MSG_MAX, else NULL */
    size_t content_len;             /* actual content length (regardless of which buffer) */

    AiAttachment *attachment;

    /* Tool fields — zero/empty when unused */
    char tool_call_id[64];    /* set on ROLE_TOOL: the id this result is for */
    char tool_name[64];       /* set on ROLE_TOOL: tool name (for display) */
    int  is_tool_error;       /* set on ROLE_TOOL: 1 if this is an error result */

    /* For ROLE_ASSISTANT messages that contain tool_use blocks:
     * Dynamically allocated — NULL and n_tool_calls == 0 for normal text messages.
     * Caller allocates with malloc(n * sizeof(AiToolCall)), frees when message is
     * removed from conversation. */
    AiToolCall *tool_calls;   /* heap-allocated array, NULL when unused */
    int n_tool_calls;         /* 0 means normal text message */
} AiMessage;
```

**Content access helpers:**

```c
/* Returns pointer to message content (inline or overflow buffer, transparently) */
const char *ai_msg_content(const AiMessage *msg);

/* Sets message content. If len <= AI_MSG_MAX-1, copies to inline buffer.
 * If len > AI_MSG_MAX-1, allocates content_overflow via malloc.
 * Frees any prior overflow. Returns 0 on success, -1 on allocation failure. */
int ai_msg_set_content(AiMessage *msg, const char *data, size_t len);

/* Frees dynamic resources (content_overflow + tool_calls). Must be called
 * when a message is removed from the conversation. */
void ai_msg_free(AiMessage *msg);
```

**Memory management**: For most messages (user input, assistant text), content
fits in the inline `char content[AI_MSG_MAX]` buffer and no heap allocation
occurs — identical overhead to the current code. Only tool result messages that
exceed `AI_MSG_MAX` trigger a `malloc` into `content_overflow`. This keeps the
common path allocation-free while supporting large tool results (up to
`AI_TOOL_RESULT_MAX` / 1MB).

`ai_msg_free()` replaces the narrower `ai_msg_free()` — it frees
both `content_overflow` and `tool_calls` in one call. When a message is removed
from the conversation (context window overflow, conversation clear),
`ai_msg_free()` is the single cleanup point.

**How `ai_build_request_body_ex()` uses these fields:**

- **Anthropic format**: When `n_tool_calls > 0` on an assistant message, emit
  `content` as a `text` block followed by `tool_use` blocks. When role is
  `AI_ROLE_TOOL`, emit a `tool_result` content block inside a `user` message.
  **Grouping rule**: consecutive `AI_ROLE_TOOL` messages in the conversation
  array MUST be coalesced into a single `user` message containing multiple
  `tool_result` content blocks. The serializer scans ahead when it encounters
  the first `AI_ROLE_TOOL` and collects all adjacent `AI_ROLE_TOOL` messages
  into one `user` message, then advances the loop index past them. This is
  required because Anthropic rejects consecutive `user` messages and requires
  all tool results for a given turn in a single message.
- **OpenAI format**: When `n_tool_calls > 0`, emit `tool_calls` array on the
  assistant message. When role is `AI_ROLE_TOOL`, emit a `role: "tool"` message
  with `tool_call_id`. No grouping needed — OpenAI accepts separate `tool`
  messages.
- **Backward compatibility**: When `n_tool_calls == 0` and role is not
  `AI_ROLE_TOOL`, behaviour is identical to today — no tool fields emitted.

**Why grouping is a serializer responsibility, not a data model change:**
The internal conversation stores one `AiMessage` per tool result because it
simplifies insertion, deletion, display, and export — each tool result is an
independent item in the ChatListView. Merging at the data model level would
require compound messages with variable-length arrays of tool results, adding
complexity to every consumer. Instead, only `ai_build_request_body_ex()` handles
the Anthropic grouping rule, keeping it in one place.

### 3.2.2 AiSessionState Extension for Tool State

The per-session state needs to track agentic loop progress and streaming block type:

```c
/* Stream content block type — tracks what kind of block we're inside */
typedef enum {
    BLOCK_NONE,       /* not inside any content block */
    BLOCK_TEXT,       /* inside a text content block — deltas go to display */
    BLOCK_TOOL_USE   /* inside a tool_use block — deltas go to accumulation buffer */
} AiStreamBlockType;

/* Additions to AiSessionState in ai_prompt.h */
typedef struct {
    /* ... existing fields ... */

    /* Tool streaming state */
    AiStreamBlockType current_block_type;  /* what kind of content block we're in */
    AiToolCall active_tool_call;   /* tool call being accumulated from stream deltas */
    int        tool_call_active;   /* 1 while accumulating a tool_use block */
    int        tool_loop_iter;     /* current agentic loop iteration (0-based) */
    int        tool_search_count;  /* search invocations this user message (reset per user msg) */
    volatile int cancel_requested; /* set to 1 from UI thread to request cancellation */

    /* Accumulated tool calls for current response (multiple tool_use blocks) */
    AiToolCall *pending_tool_calls;  /* heap array, grown as tool_use blocks complete */
    int         pending_tool_count;
    int         pending_tool_cap;
} AiSessionState;
```

**Stream parsing state machine:**

For **Anthropic** streaming:
1. `content_block_start` with `type: "text"` → set `current_block_type = BLOCK_TEXT`
2. `content_block_start` with `type: "tool_use"` → set `current_block_type = BLOCK_TOOL_USE`, capture `id` and `name` from the event into `active_tool_call`
3. `content_block_delta` with `text_delta` → if `BLOCK_TEXT`, route to display
4. `content_block_delta` with `input_json_delta` → if `BLOCK_TOOL_USE`, append to `active_tool_call.input_json` accumulation buffer
5. `content_block_stop` → if `BLOCK_TOOL_USE`, parse accumulated `input_json` and add to `pending_tool_calls`; reset `current_block_type = BLOCK_NONE`
6. `message_delta` with `stop_reason: "tool_use"` → trigger tool execution phase

For **OpenAI** streaming:
1. `delta.tool_calls[].index` identifies the tool call slot
2. First chunk per index carries `id` and `function.name` → allocate slot in `pending_tool_calls`
3. Subsequent chunks for same index append to `function.arguments` accumulation buffer
4. `finish_reason: "tool_calls"` → parse all accumulated slots, trigger tool execution phase

**Stream error handling:**
On any stream error (timeout, network drop, malformed JSON) during tool call accumulation:
1. Discard **all** partial tool calls (`pending_tool_calls` freed, count reset)
2. Reset parser state (`current_block_type = BLOCK_NONE`, `tool_call_active = 0`)
3. Surface error to user via UI
4. **Never** execute a tool from partial data

### 3.2.3 Tool Registry Ownership

The `AiToolRegistry` is owned by the `AiChatData` struct in `ai_chat.c`:

```c
/* Addition to AiChatData in ai_chat.c */
typedef struct {
    /* ... existing fields ... */
    AiToolRegistry tool_registry;    /* populated on init / settings change */
    WebSearchContext search_ctx;     /* tool-specific context for web_search */
    WebFetchContext  fetch_ctx;      /* tool-specific context for web_fetch */
} AiChatData;
```

When settings change (or on init), the chat window calls `ai_tools_init()` then
conditionally registers `web_search` (if `ai_search_provider` != "none") and
`web_fetch` (if `ai_web_fetch_enabled`). The tool-specific context structs are
populated from `Settings` and their addresses are stored in each `AiToolDef.tool_data`
at registration time. `ai_tool_execute()` reads `tool_data` from the registered
definition — callers never need to look up or route the correct context.

### 3.3 Key Functions

```c
/* ai_tools.h — Registry */
void ai_tools_init(AiToolRegistry *reg);
int  ai_tools_register(AiToolRegistry *reg, const AiToolDef *tool);
const AiToolDef *ai_tools_find(const AiToolRegistry *reg, const char *name);

/* ai_tools.h — Serialization (provider-agnostic -> provider-specific) */
int  ai_tools_serialize_anthropic(const AiToolRegistry *reg, char *buf, size_t max);
int  ai_tools_serialize_openai(const AiToolRegistry *reg, char *buf, size_t max);

/* ai_tools.h — Parse tool calls from response */
int  ai_parse_tool_calls_anthropic(const char *response_json, AiToolCall *calls, int max_calls);
int  ai_parse_tool_calls_openai(const char *response_json, AiToolCall *calls, int max_calls);

/* ai_tools.h — Parse tool calls from streaming (accumulate deltas) */
int  ai_parse_tool_stream_anthropic(const char *chunk, AiSessionState *state);
int  ai_parse_tool_stream_openai(const char *chunk, AiSessionState *state);

/* ai_tools.h — Execute a tool call (tool_data is read from the registered AiToolDef) */
int  ai_tool_execute(const AiToolRegistry *reg, const AiToolCall *call,
                     AiToolResult *result);

/* ai_tools.h — Build tool_result messages */
int  ai_tool_result_anthropic(const AiToolResult *result, char *buf, size_t max);
int  ai_tool_result_openai(const AiToolResult *result, char *buf, size_t max);

/* ai_tools.h — Schema validation (used in tests) */
int  ai_tools_validate_schemas(const AiToolRegistry *reg);

/* ai_tools.h — Message helpers */
void ai_msg_free(AiMessage *msg);

/* ai_tool_web_search.c — Web search tool implementation */
int  tool_web_search_execute(const char *input_json, void *tool_data,
                             volatile int *cancel_flag,
                             char **result_buf, size_t *result_len,
                             int *was_truncated);

/* ai_tool_web_fetch.c — Web fetch tool implementation */
int  tool_web_fetch_execute(const char *input_json, void *tool_data,
                            volatile int *cancel_flag,
                            char **result_buf, size_t *result_len,
                            int *was_truncated);
```

### 3.4 Agentic Loop

The key architectural change is an **agentic loop** in the streaming thread:

```
1. Build request with tool definitions + conversation
2. Send streaming request to API
3. Accumulate response (text + tool_use blocks via stream state machine)
4. If stop_reason == "tool_use":
   a. Parse completed tool calls from pending_tool_calls
   b. For each tool call:
      - Check safety level against approval settings
      - If approval needed: post to UI thread, wait for approval
      - Check rate limits (search_count < AI_TOOL_SEARCH_MAX for web_search)
      - Execute tool (background thread) — callee allocates result buffer
      - If was_truncated: post warning to UI ("Tool result truncated to 1MB")
      - Collect result, free result buffer after adding to conversation
   c. Add assistant message (with tool_use) to conversation
   d. Add user message (with tool_results) to conversation
   e. Increment tool_loop_iter
   f. If tool_loop_iter >= AI_TOOL_LOOP_MAX: post warning, stop
   g. GOTO 1 (send next request with updated conversation)
5. If stop_reason == "end_turn" or "stop":
   a. Add assistant message to conversation
   b. Post final response to UI
   c. Reset tool_loop_iter and tool_search_count
   d. Done
```

**Rate limiting:** The agentic loop tracks `tool_search_count` per user message.
When a `web_search` tool call would exceed `AI_TOOL_SEARCH_MAX` (3), the tool
returns an error result: `"Search limit reached (3 per message). Please ask the
user to send a new message if more searches are needed."` This prevents runaway
search loops while still allowing the model to use remaining tool calls.

**Cancellation:** The UI thread sets `state->cancel_requested = 1` when the user
presses abort. The agentic loop checks this flag at three points:
1. **Before each tool execution** — skip remaining tools, discard pending calls
2. **Before each loop iteration** (GOTO 1) — exit loop immediately
3. **During HTTP requests** — tool implementations receive a pointer to
   `cancel_requested` and pass it to WinHTTP via `WinHttpSetOption` with a
   callback that calls `WinHttpCloseHandle()` when the flag is set, aborting
   in-flight requests. This prevents a 10-second fetch from blocking abort.

On cancellation, any partial results are discarded. The conversation retains
messages up to the last complete assistant turn (no partial tool_use/tool_result
pairs are left in the history).

### 3.5 Web Search Tool

```c
/* Tool definition */
{
    .name = "web_search",
    .description = "Search the web for current information. Use this when the user "
                   "asks about recent events, current data, or anything that requires "
                   "up-to-date information beyond your training data. Returns relevant "
                   "search results with titles, URLs, and snippets.",
    .input_schema_json = "{"
        "\"type\":\"object\","
        "\"properties\":{"
            "\"query\":{\"type\":\"string\",\"description\":\"The search query\"}"
        "},"
        "\"required\":[\"query\"]"
    "}",
    .safety = TOOL_SAFE,
    .execute = tool_web_search_execute
}
```

**User-Agent:** All HTTP requests from tool implementations use a generic
User-Agent header identifying the application:

```
Nutshell/1.0 (Windows NT 10.0; Win64; x64)
```

This is set as a constant `AI_TOOL_USER_AGENT` in `ai_tools.h`.

**Implementation:**
- Uses WinHTTP (already available) to perform search requests
- **Primary method** (`api.duckduckgo.com`): HTTP GET to `https://api.duckduckgo.com/?q=<query>&format=json&no_html=1`, parse JSON Instant Answer response. Stable endpoint, structured data, but limited to instant answers.
- **Fallback method** (`html.duckduckgo.com`): HTTP POST to `https://html.duckduckgo.com/html/` with body `q=<url-encoded-query>`, parse HTML response. Full web results but fragile scraping.
- **Search flow**: Try API first. If API returns `Type: ""` (no instant answer), automatically fall back to HTML scraping. If HTML also fails (HTTP error, CAPTCHA, zero results), return a clear "no results found" message. If settings select `html.duckduckgo.com` explicitly, use HTML only. If `custom`, POST/GET to user-configured URL.
- Config fields: `ai_search_provider`, `ai_search_url`, `ai_max_search_results`
- No API key required for DuckDuckGo options
- Returns: formatted text with title, URL, and snippet for each result
- Max results: controlled by `ctx->max_search_results` (default 7)
- **Ad filtering**: skip results where the container class contains `result--ad`

#### 3.5.1 DDG Instant Answer API Response Format (Primary)

```
Request:  GET https://api.duckduckgo.com/?q=<query>&format=json&no_html=1

Response (JSON):
  {
    "Type": "A"|"D"|"",        // A=article, D=disambiguation, ""=nothing
    "Heading": "Topic Name",
    "Abstract": "Summary text...",
    "AbstractURL": "https://en.wikipedia.org/wiki/...",
    "AbstractSource": "Wikipedia",
    "RelatedTopics": [
      // Flat entry:
      { "Text": "Description...", "FirstURL": "https://duckduckgo.com/Topic" },
      // Categorized group:
      { "Name": "Category", "Topics": [{ "Text": "...", "FirstURL": "..." }] }
    ],
    "Results": [
      { "Text": "Official site", "FirstURL": "https://example.com/" }
    ]
  }
```

**Parsing strategy:**
1. Check `Type`: if `""` (empty), return failure code to trigger HTML fallback (not an error — the API simply doesn't cover all queries)
2. If `Type` is `"A"` (article): return `Heading` + `AbstractText` + `AbstractURL` as the primary result, then `RelatedTopics` flat entries as supplementary
3. If `Type` is `"D"` (disambiguation): return `RelatedTopics` entries (flat entries only; skip categorized groups for simplicity)
4. For each `RelatedTopics` flat entry: `Text` is the description, `FirstURL` is the DDG internal URL (e.g. `https://duckduckgo.com/Secure_Shell`)
5. Cap at `max_search_results` entries
6. **Limitation**: This API is an instant-answer service, not a full search engine. Many general queries return `Type: ""` — this is why it cascades to HTML scraping.

#### 3.5.2 DDG HTML Lite Response Format (Fallback)

The HTML lite endpoint uses **POST** (not GET). Key structure:

```
Request:  POST https://html.duckduckgo.com/html/
          Content-Type: application/x-www-form-urlencoded
          Body: q=linux+ssh+terminal

Response structure:
  <div class="serp__results">
    <div id="links" class="results">
      <!-- Ad results (skip these): -->
      <div class="result results_links results_links_deep result--ad ">
        ...
      </div>

      <!-- Organic results (parse these): -->
      <div class="result results_links results_links_deep web-result ">
        <div class="links_main links_deep result__body">
          <h2 class="result__title">
            <a class="result__a" href="//duckduckgo.com/l/?uddg=ENCODED_URL&amp;rut=HASH">
              Title Text
            </a>
          </h2>
          <div class="result__extras">
            <a class="result__url" href="...">www.example.com/path</a>
          </div>
          <a class="result__snippet" href="...">
            Snippet with <b>keyword</b> highlights...
          </a>
        </div>
      </div>
      ...
    </div>
  </div>
```

**Parsing strategy:**
1. Scan for `class="` containing `web-result` → organic result (parse it)
2. Scan for `class="` containing `result--ad` → ad (skip it)
3. Inside each organic result:
   - Find `class="result__a"` → extract inner text as **title**
   - From same `<a>` `href`, extract `uddg=` parameter → URL-decode for **real URL**. Apply a second decode pass if the result still contains `%`-encoded sequences (DDG double-encodes some URLs).
   - Find `class="result__snippet"` → strip `<b>` tags for **snippet text**
4. Detect CAPTCHA: presence of `anomaly-modal` class → fail, trigger "no results" message
5. Detect zero results: no `web-result` divs found
6. HTML entities: decode `&amp;`, `&quot;`, `&#x27;`, `&lt;`, `&gt;`
7. Stop after `max_search_results` organic results collected

### 3.6 Web Fetch Tool

```c
/* Tool definition */
{
    .name = "web_fetch",
    .description = "Fetch the contents of a specific web page URL. Use this to retrieve "
                   "detailed information from a URL found via web search or provided by "
                   "the user. Returns the page content as plain text.",
    .input_schema_json = "{"
        "\"type\":\"object\","
        "\"properties\":{"
            "\"url\":{\"type\":\"string\",\"description\":\"The URL to fetch\"}"
        "},"
        "\"required\":[\"url\"]"
    "}",
    .safety = TOOL_SAFE,
    .execute = tool_web_fetch_execute
}
```

**Implementation:**
- Uses WinHTTP GET to fetch the target URL
- Strips HTML tags, scripts, and styles to extract plain text content
- Dynamic result buffer: allocates as needed, caps at `AI_TOOL_RESULT_MAX` (1MB) with truncation warning
- **Gated by settings**: only registered in tool registry when `ai_web_fetch_enabled` is true
- **No SSRF filtering**: internal network endpoints are permitted — the user controls enablement via the settings toggle
- Handles redirects (WinHTTP default behaviour)
- Uses `AI_TOOL_USER_AGENT` header
- Timeout: 10 seconds to prevent blocking on slow/hanging servers

### 3.6.1 Shared HTML Utilities (`html_util.h/c`)

Both `web_search` (DDG HTML scraping) and `web_fetch` (HTML-to-text) need HTML
processing. To avoid duplicating this error-prone C string parsing, shared
utilities live in `src/core/html_util.h/c`:

```c
/* src/core/html_util.h */

/* Strip all HTML tags from input, writing plain text to out_buf.
 * Removes <script> and <style> blocks entirely (including content).
 * Self-closing tags handled. Preserves text content between tags.
 * Returns bytes written (excluding NUL). */
size_t html_strip_tags(const char *html, size_t html_len,
                       char *out_buf, size_t out_max);

/* Decode HTML entities in-place: &amp; &quot; &#x27; &lt; &gt; &#NNN; &#xHHHH;
 * Returns new length after decoding. */
size_t html_decode_entities(char *buf, size_t len);

/* URL-decode a percent-encoded string in-place: %20 → space, %2F → /, etc.
 * Applies a second decode pass if the result still contains %-sequences
 * (handles DDG double-encoding). Returns new length. */
size_t html_url_decode(char *buf, size_t len);

/* Strip specific tags by name (e.g., "b", "em") while keeping their text content.
 * Used by DDG snippet parsing to remove <b> highlight tags.
 * Modifies buf in-place. Returns new length. */
size_t html_strip_tag_by_name(char *buf, size_t len, const char *tag_name);

/* Extract text content between the first occurrence of a tag with the given
 * class attribute value. Returns pointer into html (not a copy), sets *out_len.
 * Returns NULL if not found. */
const char *html_find_by_class(const char *html, size_t html_len,
                               const char *class_value, size_t *out_len);
```

**Design principles:**
- All functions operate on explicit lengths — no reliance on NUL-termination of
  untrusted HTML input (prevents over-read on malformed/truncated content)
- In-place modification where possible to minimise allocation
- `html_strip_tags()` is the most complex: uses a simple state machine
  (TEXT → TAG → TEXT) with special states for `<script>` and `<style>` blocks
- No heap allocation inside these functions — caller provides the buffer

**Consumers:**
- `ai_tool_web_search.c` uses `html_find_by_class()`, `html_decode_entities()`,
  `html_url_decode()`, and `html_strip_tag_by_name()` for DDG HTML parsing
- `ai_tool_web_fetch.c` uses `html_strip_tags()` and `html_decode_entities()`
  for full-page HTML-to-text conversion

### 3.7 Config Changes

```c
/* Additions to Settings struct in config.h */
char ai_search_provider[64];     /* "none", "duckduckgo-api", "duckduckgo-html", "custom" */
char ai_search_url[256];         /* for custom search endpoint only */
int  ai_max_search_results;      /* 1-20, default 7 */
int  ai_web_fetch_enabled;       /* 0 = disabled (default), 1 = enabled */
```

**Notes:**
- No `ai_search_api_key` needed — DuckDuckGo requires no authentication
- `ai_search_provider` defaults to `"duckduckgo-api"` (works out of the box, no API key needed)
- `ai_max_search_results` defaults to `7`, clamped to `1-20` in `settings_validate()`
- `ai_web_fetch_enabled` defaults to `0` (disabled) — user must explicitly permit
- Custom search URL is only visible/used when provider is "custom"

### 3.8 Settings UI Changes

**Search Engine dropdown** (CBS_DROPDOWNLIST):
- Items: `None`, `DuckDuckGo (API)`, `DuckDuckGo (HTML)`, `Custom`
- When `Custom` is selected, show the "Search URL" edit box (same show/hide pattern as AI Base URL for custom provider)
- Default selection: `DuckDuckGo (API)`

**Max Search Results** (ES_NUMBER edit box):
- Label: `Max search results`
- Numeric input, range 1-20, default 7
- Visible when search engine is not `None`

**Web Fetch checkbox** (BS_AUTOCHECKBOX):
- Label: `Permit web fetch`
- Checked = AI can fetch arbitrary URLs; unchecked = tool not registered
- Default: unchecked

**Layout**: These controls go in the AI section of the settings dialog, below the existing AI fields.

### 3.9 System Prompt Update

When tools are registered, the system prompt is extended with a tools section:

```
You have access to the following tools that will be called automatically via the API tool-use mechanism:

- web_search: Search the web for current information. Use this for recent events, current data, or anything beyond your training data. The search results will be returned to you automatically.
- web_fetch: Fetch the contents of a specific URL. Use this to read pages from search results or URLs the user provides.

You do NOT need to use [EXEC] markers for these tools. Simply request them via the tool-use API and the results will be provided.

Continue to use [EXEC]...[/EXEC] markers for SSH terminal commands as before.
```

This is appended conditionally — only when `tool_registry.count > 0` **and** the
current provider supports tool use. The specific tool names listed are generated
from the registry, not hard-coded in the prompt.

### 3.9.1 Provider Tool Support Detection

Not all providers/models support native tool use. Before including tool
definitions in a request, `ai_build_request_body_ex()` checks whether the
current provider supports them:

```c
/* Returns 1 if the provider supports native tool use, 0 otherwise.
 * Known support: Anthropic (all models), OpenAI (gpt-4+), DeepSeek (chat models).
 * Unknown providers: assume no support (safe default). */
int ai_provider_supports_tools(const Settings *settings);
```

**When tools are registered but the provider does not support them:**

1. **On first request of the session** (or after provider change): post a
   one-time notification to the ChatListView:
   `"Note: <model_name> does not support tool use. Web search and other tools are unavailable for this model. Cancel the request if you'd like to switch models, or continue without tools."`
   This uses `CHAT_ITEM_STATUS` — the same visual treatment as connection status.
2. **Tool definitions are omitted** from the request body — no `tools` array.
3. **System prompt tool section is omitted** — the model never sees mention of
   tools, so it won't attempt to call them via text output.
4. **The request proceeds normally** without tools. `[EXEC]` commands still work.
5. **The notification is per-session, not per-message** — once shown, it does
   not repeat unless the user changes the provider/model in settings.

### 3.10 Tool Results in Conversation Export/Copy

When the user copies or exports the chat conversation, tool interactions are included
in a readable format:

```
[Tool: web_search] query: "linux ssh terminal"
[Result] 1. PuTTY - https://www.putty.org/ - PuTTY is a free SSH client...
         2. OpenSSH - https://www.openssh.com/ - ...

[Tool: web_fetch] url: "https://www.putty.org/"
[Result] PuTTY is a free implementation of SSH and Telnet...
```

Tool call and result items in the `ChatListView` use `CHAT_ITEM_TOOL_CALL` and
`CHAT_ITEM_TOOL_RESULT` types. The copy/export logic formats these with the
`[Tool: name]` and `[Result]` prefixes.

### 3.11 Migration Strategy: [EXEC] Coexistence

The `[EXEC]` system is NOT replaced — it runs **alongside** formal tool use:

1. **Phase 1 (this branch):** Add tool framework + web search + web fetch. `[EXEC]` commands continue to work as-is for SSH command execution. The system prompt still instructs `[EXEC]` for terminal commands.
2. **Phase 2 (future):** Add a formal `execute_command` tool that replaces `[EXEC]`. The tool sends commands through the same SSH channel + approval queue. Remove `[EXEC]` marker parsing.
3. **Phase 3 (future):** Additional tools (file_read, calculator, etc.)

This means Phase 1 has **zero risk** to existing functionality.

---

## 4. Implementation Plan

### Phase 1A: Config Changes & Core Tool Framework (TDD)

| Step | File(s) | Description |
|---|---|---|
| 1 | `src/config/config.h` | Add `ai_search_provider`, `ai_search_url`, `ai_max_search_results`, `ai_web_fetch_enabled` fields to `Settings` |
| 2 | `src/config/loader.c` | Load/save new config fields, defaults in `config_default_settings()`, validation in `settings_validate()` |
| 3 | `tests/test_ai_tools.c`, `tests/runner.c` | Write tests for tool registry (init, register, find, overflow) |
| 4 | `src/core/ai_tools.h` | Define structs and function prototypes |
| 5 | `src/core/ai_tools.c` | Implement registry (init, register, find) |
| 6 | `tests/test_ai_tools.c` | Tests for Anthropic tool serialization |
| 7 | `src/core/ai_tools.c` | Implement `ai_tools_serialize_anthropic()` |
| 8 | `tests/test_ai_tools.c` | Tests for OpenAI tool serialization |
| 9 | `src/core/ai_tools.c` | Implement `ai_tools_serialize_openai()` |
| 10 | `tests/test_ai_tools.c` | Tests for parsing tool_use from Anthropic response |
| 11 | `src/core/ai_tools.c` | Implement `ai_parse_tool_calls_anthropic()` |
| 12 | `tests/test_ai_tools.c` | Tests for parsing tool_calls from OpenAI response |
| 13 | `src/core/ai_tools.c` | Implement `ai_parse_tool_calls_openai()` |
| 14 | `tests/test_ai_tools.c` | Tests for tool execution dispatch (mock execute callback, verify `tool_data` passthrough, dynamic result buffer allocation/free) |
| 15 | `src/core/ai_tools.c` | Implement `ai_tool_execute()` |
| 16 | `tests/test_ai_tools.c` | Tests for tool_result serialization (both formats) |
| 17 | `src/core/ai_tools.c` | Implement result serialization |
| 18 | `tests/test_ai_tools.c` | **Schema validation tests**: register all tools, call `ai_tools_validate_schemas()` — verifies each tool's `input_schema_json` parses as valid JSON via `json_parse()` |
| 19 | `src/core/ai_tools.c` | Implement `ai_tools_validate_schemas()` |
| 19b | `tests/test_ai_tools.c` | **Input buffer boundary tests**: for each registered tool, construct a maximally-sized valid `input_json` (e.g., `{"query":"<3900+ chars>"}`) at and beyond `AI_TOOL_INPUT_MAX`. Verify: (a) input at limit-1 executes successfully, (b) input at exactly `AI_TOOL_INPUT_MAX` is handled without overflow, (c) streaming accumulation that would exceed the buffer is detected and rejected before execution. Run against every tool in the registry via a loop. |

### Phase 1B: Streaming Tool Detection

| Step | File(s) | Description |
|---|---|---|
| 20 | `tests/test_ai_tools.c` | Tests for Anthropic streaming: `content_block_start` with `type: "tool_use"` sets `BLOCK_TOOL_USE` and captures id/name; `input_json_delta` accumulates; `content_block_stop` triggers parse; `content_block_start` with `type: "text"` sets `BLOCK_TEXT`; verify text deltas route to display and tool deltas route to accumulation |
| 21 | `src/core/ai_tools.c` | Implement `ai_parse_tool_stream_anthropic()` using `AiStreamBlockType` state machine |
| 22 | `tests/test_ai_tools.c` | Tests for OpenAI streaming: `delta.tool_calls[].index` slot allocation; first chunk per index carries `id` and `function.name`; subsequent chunks append to `function.arguments`; `finish_reason: "tool_calls"` triggers parse of all accumulated slots |
| 23 | `src/core/ai_tools.c` | Implement `ai_parse_tool_stream_openai()` with index-based slot tracking |
| 24 | `tests/test_ai_tools.c` | Tests for stream error handling: simulate mid-accumulation error → verify all partial tool calls discarded, parser state reset to `BLOCK_NONE`, no tool execution attempted |
| 25 | `src/core/ai_tools.c` | Implement `ai_tools_stream_reset()` — discard partial state, free pending calls |

### Phase 1C: AiMessage Extension & Request Body Integration

| Step | File(s) | Description |
|---|---|---|
| 26 | `src/core/ai_prompt.h` | Add `AI_ROLE_TOOL` to `AiRole`, add `tool_call_id`, `tool_name`, `is_tool_error`, `tool_calls` (pointer), `n_tool_calls` fields to `AiMessage`; add `AiStreamBlockType` enum and tool streaming fields to `AiSessionState` |
| 27 | `src/core/ai_prompt.c` | Implement `ai_msg_set_content()`, `ai_msg_content()`, `ai_msg_free()` |
| 27b | `tests/test_ai_prompt.c` | **Content overflow tests**: (a) content shorter than `AI_MSG_MAX` uses inline buffer, `content_overflow` is NULL; (b) content exceeding `AI_MSG_MAX` allocates overflow, `ai_msg_content()` returns overflow pointer; (c) `ai_msg_free()` frees overflow and tool_calls; (d) calling `ai_msg_set_content()` twice frees prior overflow before re-allocating; (e) `ai_msg_set_content()` returns -1 on malloc failure (test with very large size) |
| 28 | `tests/test_ai_prompt.c` | Tests for `AI_ROLE_TOOL` message serialization — **Anthropic**: `tool_result` content block inside `user` message with `tool_use_id` and `content`; **OpenAI**: `role: "tool"` message with `tool_call_id` |
| 29 | `tests/test_ai_prompt.c` | Tests for assistant message with `tool_use` blocks — **Anthropic**: `content` array with `text` block + `tool_use` blocks (each with `id`, `name`, `input`); **OpenAI**: `tool_calls` array with `function` objects |
| 30 | `tests/test_ai_prompt.c` | Tests for full round-trip conversation body: user msg → assistant msg with tool_use → tool_result msg → assistant final response. Verify correct message ordering and nesting for both Anthropic and OpenAI formats |
| 30b | `tests/test_ai_prompt.c` | **Anthropic tool_result grouping tests**: (a) two consecutive `AI_ROLE_TOOL` messages are coalesced into one `user` message with two `tool_result` content blocks; (b) single `AI_ROLE_TOOL` produces one `user` message with one `tool_result`; (c) `AI_ROLE_TOOL` messages separated by another role are NOT grouped; (d) verify OpenAI format emits separate `tool` messages (no grouping). These tests catch Anthropic's rejection of consecutive `user` messages. |
| 31 | `src/core/ai_prompt.c` | Modify `ai_build_request_body_ex()` to handle `AI_ROLE_TOOL` and `n_tool_calls > 0` messages for both providers |
| 32 | `tests/test_ai_prompt.c` | Tests for request body with tool definitions included — verify `tools` array appears at top level (Anthropic format) and with `function` wrapper (OpenAI format); verify tool definitions match registry contents |
| 33 | `src/core/ai_prompt.c` | Modify `ai_build_request_body_ex()` to accept and include tool definitions from registry |

### Phase 1D: Shared HTML Utilities & Web Search Tool

| Step | File(s) | Description |
|---|---|---|
| 33b | `tests/test_html_util.c` | Tests for `html_strip_tags()`: basic tags, nested tags, self-closing, `<script>`/`<style>` block removal, empty input, unclosed tags, tags spanning buffer boundary |
| 33c | `tests/test_html_util.c` | Tests for `html_decode_entities()`: named entities (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&#x27;`), numeric decimal (`&#123;`), numeric hex (`&#x1F4A9;`), malformed/unterminated entities, consecutive entities |
| 33d | `tests/test_html_util.c` | Tests for `html_url_decode()`: standard percent-encoding, `+` as space, double-encoded URLs (DDG `uddg` parameter), already-decoded passthrough, malformed `%` sequences |
| 33e | `tests/test_html_util.c` | Tests for `html_strip_tag_by_name()` and `html_find_by_class()`: partial class matches, multiple classes on element, class not found, nested elements with same class |
| 33f | `src/core/html_util.h/c` | Implement all shared HTML utility functions |
| 34 | `tests/test_tool_web_search.c` | Tests for DDG API JSON parsing: Type "A" (article), Type "D" (disambiguation), Type "" (empty → returns failure code for fallback), RelatedTopics flat vs categorized |
| 35 | `tests/test_tool_web_search.c` | Tests for DDG HTML parsing: organic results, ad filtering, URL decoding of `uddg` param (single-encoded and double-encoded URLs), HTML entity decoding, `<b>` tag stripping in snippets |
| 36 | `tests/test_tool_web_search.c` | Tests for DDG HTML edge cases: CAPTCHA detection (`anomaly-modal`), zero results (no `web-result` divs), malformed HTML |
| 37 | `tests/test_tool_web_search.c` | Tests for primary→fallback cascade: API returns `Type: ""` → HTML called; both fail → clear "no results" message |
| 38 | `src/core/ai_tool_web_search.h/c` | Implement DDG API JSON parser: handle Type A/D/"", extract from Abstract/RelatedTopics/Results |
| 39 | `src/core/ai_tool_web_search.c` | Implement DDG HTML parser using `html_util` functions: `html_find_by_class()` for result divs, `html_url_decode()` for `uddg` URLs, `html_decode_entities()` + `html_strip_tag_by_name()` for snippets |
| 40 | `src/core/ai_tool_web_search.c` | Implement `tool_web_search_execute()` with primary(API)/fallback(HTML) logic, dynamic result buffer, `was_truncated` support, respecting `WebSearchContext` fields |

### Phase 1E: Web Fetch Tool

| Step | File(s) | Description |
|---|---|---|
| 41 | `tests/test_tool_web_fetch.c` | Tests for web_fetch HTML-to-text pipeline: `html_strip_tags()` + `html_decode_entities()` + whitespace normalization (these use shared `html_util` functions, tested in 33b-33e; these tests verify the web_fetch-specific integration: chaining, buffer sizing, whitespace collapse) |
| 42 | `tests/test_tool_web_fetch.c` | Tests for dynamic buffer allocation, truncation at `AI_TOOL_RESULT_MAX` (1MB) with `was_truncated` flag set |
| 43 | `src/core/ai_tool_web_fetch.h/c` | Implement HTML-to-text pipeline using `html_util` functions + whitespace normalisation pass |
| 44 | `src/core/ai_tool_web_fetch.c` | Implement `tool_web_fetch_execute()` — WinHTTP GET, strip via html_util, dynamic buffer, truncate with warning flag |

### Phase 1F: Agentic Loop & UI Integration

| Step | File(s) | Description |
|---|---|---|
| 45 | `tests/test_agentic_loop.c` | Tests for agentic loop core logic (extracted as testable function): iteration counting, stop-reason branching (`tool_use` → continue, `end_turn` → stop), conversation assembly with interleaved tool_use/tool_result messages, max iteration enforcement (`AI_TOOL_LOOP_MAX`) |
| 46 | `tests/test_agentic_loop.c` | Tests for rate limiting: search count tracking, 4th search returns error result, count resets on new user message; verify non-search tools are not rate-limited |
| 46b | `tests/test_agentic_loop.c` | Tests for cancellation: set `cancel_requested = 1` before tool execution → verify remaining tools skipped, partial results discarded, no incomplete tool_use/tool_result pairs in conversation; set cancel mid-loop → verify loop exits cleanly |
| 47 | `tests/test_agentic_loop.c` | Tests for truncation warning: mock tool returns `was_truncated = 1` → verify warning message generated |
| 48 | `src/core/ai_agentic.h/c` | Extract testable agentic loop core: iteration logic, rate limiting, conversation assembly, truncation warning generation. Pure logic — no UI dependencies. |
| 49 | `src/core/ai_prompt.c` | Update stream chunk parser to route through `AiStreamBlockType` state machine, call `ai_parse_tool_stream_anthropic/openai` for tool blocks |
| 50 | `src/ui/ai_chat.c` | Add `AiToolRegistry` to `AiChatData`, populate on init and settings change |
| 51 | `src/ui/ai_chat.c` | Modify streaming thread to use `ai_agentic.c` core logic + UI integration (approval, display, error handling) |
| 52 | `src/ui/ai_chat.c` | Add `CHAT_ITEM_TOOL_CALL` and `CHAT_ITEM_TOOL_RESULT` items to ChatListView (show `using tool: <provider> - <name>`) |
| 53 | `src/ui/ai_chat.c` | Add tool-result display (collapsible, like thinking blocks); include tool results in copy/export with `[Tool: name]` / `[Result]` formatting |
| 54 | `src/ui/ai_chat.c` | Update system prompt conditionally when `tool_registry.count > 0` **and** `ai_provider_supports_tools()` returns true — list registered tool names dynamically |
| 54b | `src/core/ai_prompt.c` | Implement `ai_provider_supports_tools()` — check provider/model against known support list; return 0 for unknown providers (safe default) |
| 54c | `src/ui/ai_chat.c` | When tools are registered but provider doesn't support them: post one-time `CHAT_ITEM_STATUS` notification naming the model and explaining tools are unavailable; proceed without tools; don't repeat notification until provider changes |
| 55 | `src/ui/settings.c` | Add search engine dropdown (None / DuckDuckGo (API) / DuckDuckGo (HTML) / Custom) |
| 56 | `src/ui/settings.c` | Add custom search URL edit box (visible only when "Custom" selected) |
| 57 | `src/ui/settings.c` | Add max search results numeric edit box (visible when search engine not None) |
| 58 | `src/ui/settings.c` | Add "Permit web fetch" checkbox |

### Phase 1G: Build & Verify

| Step | File(s) | Description |
|---|---|---|
| 59 | `Makefile` | Add new source files to build |
| 60 | — | `make clean && make release`, `make test` |
| 61 | — | Manual testing with Anthropic + OpenAI providers |

---

## 5. File Manifest (New & Modified Files)

**New files:**

| File | Layer | Purpose |
|---|---|---|
| `src/core/ai_tools.h` | Core | Tool structs (`AiToolDef`, `AiToolCall`, `AiToolResult`), registry, serialization prototypes, `AI_TOOL_USER_AGENT` constant |
| `src/core/ai_tools.c` | Core | Tool registry, Anthropic/OpenAI serialization, parsing, execution dispatch, schema validation, stream state machine |
| `src/core/ai_tool_web_search.h` | Core | Web search tool header, `WebSearchContext` struct |
| `src/core/ai_tool_web_search.c` | Core | DDG API JSON parser, DDG HTML scraper (via html_util), primary/fallback logic, ad filtering |
| `src/core/html_util.h` | Core | Shared HTML utilities: tag stripping, entity decoding, URL decoding, class-based element finding |
| `src/core/html_util.c` | Core | Implementation of shared HTML parsing functions used by both web_search and web_fetch |
| `src/core/ai_tool_web_fetch.h` | Core | Web fetch tool header, `WebFetchContext` struct |
| `src/core/ai_tool_web_fetch.c` | Core | URL fetching, HTML-to-text (via html_util), dynamic buffer, content truncation |
| `src/core/ai_agentic.h` | Core | Agentic loop core logic header |
| `src/core/ai_agentic.c` | Core | Testable agentic loop: iteration control, rate limiting, conversation assembly |
| `tests/test_ai_tools.c` | Tests | Tool framework tests (registry, serialization, parsing, execution, schema validation, streaming state machine) |
| `tests/test_html_util.c` | Tests | Shared HTML utility tests: tag stripping, entity decoding, URL decoding (single + double), class-based element finding, malformed input handling |
| `tests/test_tool_web_search.c` | Tests | DDG API parsing, DDG HTML parsing, ad filtering, fallback cascade |
| `tests/test_tool_web_fetch.c` | Tests | HTML stripping, dynamic buffer truncation, entity decoding, edge cases |
| `tests/test_agentic_loop.c` | Tests | Agentic loop iteration logic, rate limiting, conversation assembly, truncation warnings |

**Modified files:**

| File | Changes |
|---|---|
| `src/core/ai_prompt.h` | Add `AI_ROLE_TOOL` to `AiRole`; add tool fields + `content_overflow`/`content_len` to `AiMessage` (dynamic `tool_calls` pointer); add `AiStreamBlockType` enum and tool streaming fields to `AiSessionState` |
| `src/core/ai_prompt.c` | Extend `ai_build_request_body_ex()` for tool defs + tool messages + Anthropic tool_result grouping + provider tool support check; update stream parser for `AiStreamBlockType` state machine; add `ai_msg_set_content()`, `ai_msg_content()`, `ai_msg_free()` |
| `src/config/config.h` | Add `ai_search_provider`, `ai_search_url`, `ai_max_search_results`, `ai_web_fetch_enabled` to `Settings` |
| `src/config/loader.c` | Load/save/default/validate new config fields |
| `src/ui/ai_chat.c` | Add `AiToolRegistry` + tool contexts to `AiChatData`; agentic loop in streaming thread using `ai_agentic.c` core; `CHAT_ITEM_TOOL_CALL`/`CHAT_ITEM_TOOL_RESULT` items; tool results in copy/export; conditional system prompt |
| `src/ui/settings.c` | Search engine dropdown, max results input, custom URL field, web fetch checkbox |
| `tests/runner.c` | Register new test functions from all 5 test files |
| `Makefile` | Add new `.c` files to build |

---

## 6. Risk Assessment

| Risk | Mitigation |
|---|---|
| Runaway tool loops | Max `AI_TOOL_LOOP_MAX` (5) iterations per user message, abort button works |
| Runaway search calls | Max `AI_TOOL_SEARCH_MAX` (3) search invocations per user message; enforced in agentic core logic |
| Tool execution blocks UI | Tools run on background thread (same as current streaming) |
| Abort blocked by in-flight HTTP | `cancel_requested` flag checked before each tool; WinHTTP handle closed from UI thread to abort pending requests; no stale tool_use/tool_result pairs left in conversation |
| DDG API returns empty for most queries | Automatic fallback to HTML scraping; clear "no results" message if both fail |
| DDG HTML layout changes break scraping | HTML scraping is fallback only (API is primary); parser is simple (anchor + snippet extraction); degradation is graceful |
| DDG rate limiting | Max 3 searches per user message, generic User-Agent header |
| Web fetch on huge URLs | Dynamic buffer with 1MB cap; `was_truncated` flag surfaces warning to user |
| Web fetch privacy concern | Disabled by default — user must explicitly enable in settings |
| Tool result too large for context | Dynamic buffer up to 1MB; truncation with user-visible warning when exceeded |
| Provider doesn't support tools | `ai_provider_supports_tools()` check; one-time notification to user naming the model; system prompt tool section omitted; request proceeds without tools |
| HTML parsing of untrusted input | Shared `html_util` functions with explicit length parameters (no NUL-termination reliance); no heap allocation inside parsers; tested with malformed/adversarial input in `test_html_util.c` |
| Breaking existing `[EXEC]` | Phase 1 runs alongside `[EXEC]`, zero changes to marker system |
| Streaming tool detection complexity | `AiStreamBlockType` state machine with explicit block start/stop transitions; stream errors discard all partial state |
| Partial tool data execution | Stream error handler discards all partial tool calls; never executes from partial data |
| Malformed tool schema JSON | `ai_tools_validate_schemas()` test catches invalid JSON at test time, not runtime |

---

## 7. Future Tools (Phase 2+)

| Tool | Safety | Description |
|---|---|---|
| `execute_command` | WRITE/CRITICAL | Replace `[EXEC]` with formal tool (same SSH channel + approval) |
| `read_file` | SAFE | Read file contents from remote via SSH/SFTP |
| `write_file` | WRITE | Write file contents to remote via SSH/SFTP |
| `calculator` | SAFE | Evaluate mathematical expressions |

---

## 8. Decisions & Open Questions

### Resolved

1. **Search backend**: **DECIDED** — DuckDuckGo. `duckduckgo-api` as primary (Instant Answer JSON, stable), `duckduckgo-html` as fallback (HTML scraping for full results when API returns empty). Free, no API key. User selects via settings dropdown. Default: `duckduckgo-api`. Config values are logical identifiers (`duckduckgo-api`, `duckduckgo-html`, `custom`) decoupled from endpoint URLs — the implementation maps identifiers to URLs internally. Max results configurable (default 7). Ads filtered by `result--ad` class.
2. **Tool approval UX**: **DECIDED** — Safe tools (web_search, web_fetch) auto-execute without approval. Only WRITE/CRITICAL tools require approval.
3. **Tool execution UX**: **DECIDED** — Distinct visual indicator from thinking. Show a status line: `using tool: duckduckgo - web_search` or `using tool: web_fetch`. This is NOT the thinking indicator — it gets its own visual treatment in the ChatListView, similar to `CHAT_ITEM_STATUS`.
4. **Context budget**: **DECIDED** — Dynamic result buffers up to 1MB. Truncation with `was_truncated` flag triggers a user-visible warning ("Tool result truncated to 1MB"). Tool callbacks are responsible for returning clean, usable text within the cap.
5. **Web fetch gating**: **DECIDED** — Disabled by default. User must enable via "Permit web fetch" checkbox in settings. When disabled, the tool is not registered in the registry at all. No SSRF filtering — internal endpoints are permitted by design.
6. **Tool result buffer**: **DECIDED** — Dynamic allocation (malloc in tool callback, freed by caller). Max 1MB (`AI_TOOL_RESULT_MAX`). Truncation sets `was_truncated` flag, agentic loop posts warning to UI.
7. **Tool context passing**: **DECIDED** — `void *tool_data` in execute callback. Each tool defines its own context struct (`WebSearchContext`, `WebFetchContext`). No monolithic context struct.
8. **User-Agent**: **DECIDED** — Generic application identifier: `Nutshell/1.0 (Windows NT 10.0; Win64; x64)`. Defined as `AI_TOOL_USER_AGENT` constant. Honest identification; if specific services block this, we can revisit per-service headers.
9. **Rate limiting**: **DECIDED** — Max 3 search tool invocations per user message (`AI_TOOL_SEARCH_MAX`). Max 5 loop iterations (`AI_TOOL_LOOP_MAX`). Enforced in agentic core logic with error result on exceed.
10. **Stream parsing**: **DECIDED** — `AiStreamBlockType` enum (`BLOCK_NONE`, `BLOCK_TEXT`, `BLOCK_TOOL_USE`). Anthropic: `content_block_start` sets type, deltas route by type, `content_block_stop` finalises. OpenAI: `delta.tool_calls[].index` slot allocation, `finish_reason: "tool_calls"` triggers parse. Stream errors discard all partial state.
11. **Tool results in export**: **DECIDED** — Visible in conversation copy/export. Formatted as `[Tool: name]` and `[Result]` prefixes.
12. **AiMessage tool_calls**: **DECIDED** — Dynamically allocated pointer (`AiToolCall *tool_calls`), NULL when unused. Freed via `ai_msg_free()` when message is removed from conversation.
13. **Tool context dispatch**: **DECIDED** — `void *tool_data` stored inside `AiToolDef` at registration time. `ai_tool_execute()` reads it from the registered definition — no manual routing or lookup by callers.
14. **AiMessage content storage**: **DECIDED** — Inline `char content[AI_MSG_MAX]` for normal messages (zero allocation overhead), with `char *content_overflow` for large tool results. Access via `ai_msg_content()` / `ai_msg_set_content()` helpers. `ai_msg_free()` is the single cleanup point for both overflow and tool_calls.
15. **Anthropic tool_result grouping**: **DECIDED** — Serializer responsibility. Internal conversation stores one `AiMessage` per tool result (simple for display, export, deletion). `ai_build_request_body_ex()` coalesces consecutive `AI_ROLE_TOOL` messages into a single Anthropic `user` message with multiple `tool_result` blocks. OpenAI emits separate `tool` messages (no grouping needed).
16. **Provider tool support**: **DECIDED** — `ai_provider_supports_tools()` checks provider/model against known support. When tools are registered but unsupported: one-time `CHAT_ITEM_STATUS` notification naming the model, tool definitions and system prompt tool section omitted, request proceeds without tools. `[EXEC]` still works.
17. **Shared HTML utilities**: **DECIDED** — `html_util.h/c` provides `html_strip_tags()`, `html_decode_entities()`, `html_url_decode()`, `html_strip_tag_by_name()`, `html_find_by_class()`. Used by both web_search (DDG HTML scraping) and web_fetch (HTML-to-text). Explicit length parameters, no heap allocation inside, no NUL-termination reliance on untrusted input.
18. **Cancellation**: **DECIDED** — `volatile int cancel_requested` flag in `AiSessionState`, set from UI thread. Checked before each tool execution, before each loop iteration, and passed to WinHTTP for in-flight request abort. Partial results discarded; no incomplete tool_use/tool_result pairs left in conversation.

### Open — Resolve Before Implementation

(None — all major decisions resolved.)
