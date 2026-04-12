# JSON Validator Design

**Date:** 2026-04-12
**Status:** Approved

## Problem

JSON request bodies sent to AI providers are built manually via `snprintf`. There is no validation step — if the builder produces malformed JSON or misses escaping a special character, the raw broken payload goes to the API. Two duplicate `json_escape_str()` functions exist (`ai_prompt.c` and `ai_tools.c`) with slightly different signatures, increasing the surface area for escaping bugs.

## Goals

1. Validate all outgoing JSON messages before sending to AI providers
2. Catch both syntax errors (malformed structure) and escaping errors (unescaped control chars / special chars in strings)
3. On failure: refuse to send, surface the error to the user
4. Consolidate the two duplicate escape functions into a single shared implementation

## Design

### New Module: `json_validate.c` / `json_validate.h`

Location: `src/core/`

#### Public API

```c
/* Validate a JSON buffer for syntax correctness and proper string escaping.
 * Returns 1 if valid, 0 if invalid.
 * On failure, writes a human-readable error to `error` (includes byte offset). */
int json_validate(const char *buf, size_t len, char *error, size_t error_size);

/* Consolidated JSON string escape function.
 * Writes the escaped form of `s` into `buf` at position `pos`.
 * If add_quotes is non-zero, wraps the output in double quotes.
 * Returns new position, or 0 on overflow. */
size_t json_escape_string(const char *s, size_t s_len, char *buf,
                          size_t buf_size, size_t pos, int add_quotes);
```

### Validator Implementation

Single-pass state machine with no heap allocation. Tracks:

- **Nesting stack** (fixed-size, e.g. 128 depth) to verify matched `{}` and `[]`
- **Current state**: outside-value, in-string, in-escape, in-number, in-literal (`true`/`false`/`null`)

Inside string literals, the validator checks:
- No raw control characters (bytes < 0x20) appear unescaped
- Every `\` is followed by one of: `"`, `\\`, `/`, `b`, `f`, `n`, `r`, `t`, or `uXXXX` (4 hex digits)
- No unescaped `"` (which would break string boundaries)

Outside strings, the validator checks:
- Braces and brackets are balanced and properly nested
- Commas and colons appear only in valid positions
- No trailing commas before `}` or `]`
- Buffer ends with a complete JSON structure (nesting depth returns to 0)

### Error Reporting

Error string format: `"<description> at byte <offset>"`.

Examples:
- `"Unescaped control character 0x0a at byte 1423"`
- `"Invalid escape sequence '\\x' at byte 302"`
- `"Unexpected end of JSON at byte 8192"`
- `"Unmatched '{' at byte 0"`

### Consolidated Escape Function

The `json_escape_string()` function replaces:
- `static json_escape_str()` in `ai_prompt.c` (adds quotes, takes NUL-terminated string)
- `static json_escape_str()` in `ai_tools.c` (no quotes, takes explicit length)

The consolidated version takes both `s_len` (explicit length) and `add_quotes` (flag), unifying both use cases. Callers that pass NUL-terminated strings use `strlen(s)` for `s_len`.

Escapes handled: `"`, `\`, `\n`, `\r`, `\t`, `/` (optional but safe), and all control characters < 0x20 as `\uXXXX`.

### Integration Points

Three call sites in `ai_chat.c` where JSON bodies are built:

1. **Line ~980** — initial build with tools (`ai_build_request_body_tools`)
2. **Line ~986** — initial build without tools (`ai_build_request_body_ex`)
3. **Line ~706** — rebuild during agentic tool loop (`ai_build_request_body_tools`)

At each site, after the build call:
```c
char json_err[256];
if (!json_validate(arg->body, arg->body_len, json_err, sizeof(json_err))) {
    /* Surface error to user via chat status, skip HTTP send */
    post_tool_msg(arg->hwnd, CHAT_ITEM_STATUS, json_err);
    /* ... handle error path ... */
}
```

### File Changes Summary

| File | Change |
|------|--------|
| `src/core/json_validate.h` | New — public API declarations |
| `src/core/json_validate.c` | New — validator + consolidated escape function |
| `src/core/ai_prompt.c` | Remove static `json_escape_str()`, use `json_escape_string()` from `json_validate.h` |
| `src/core/ai_tools.c` | Remove static `json_escape_str()`, use `json_escape_string()` from `json_validate.h` |
| `src/ui/ai_chat.c` | Add `json_validate()` calls after each body-build site |
| `tests/test_json_validate.c` | New — tests for validator and escape function |
| `tests/runner.c` | Register new test functions |
| `Makefile` | Add `json_validate.c` to both build targets |

### What This Does NOT Change

- The JSON builder logic itself (`ai_build_request_body_ex`) — it stays as-is
- The JSON parser (`json_parser.c`) — that handles *incoming* JSON
- The HTTP layer — validator sits between builder and sender
