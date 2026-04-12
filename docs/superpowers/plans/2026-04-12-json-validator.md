# JSON Validator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a JSON validator that checks outgoing AI provider messages for syntax correctness and proper string escaping, and consolidate two duplicate escape functions into one shared implementation.

**Architecture:** New `src/core/json_validate.c/.h` module with a single-pass state machine validator and a consolidated `json_escape_string()` function. The validator is called after every JSON body build in `ai_chat.c`; on failure it blocks the HTTP send and surfaces an error to the user.

**Tech Stack:** C11, custom test framework (`test_framework.h`), native gcc test build.

---

### Task 1: Consolidated Escape Function — Tests

**Files:**
- Create: `tests/test_json_validate.c`
- Modify: `tests/runner.c` (add forward declarations and calls)

- [ ] **Step 1: Create test file with escape function tests**

Create `tests/test_json_validate.c`:

```c
#include "test_framework.h"
#include "json_validate.h"
#include <string.h>

/* ---- json_escape_string tests ---- */

int test_escape_simple_string(void) {
    TEST_BEGIN();
    char buf[256];
    size_t pos = json_escape_string("hello", 5, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"hello\"");
    TEST_END();
}

int test_escape_special_chars(void) {
    TEST_BEGIN();
    char buf[256];
    size_t pos = json_escape_string("a\"b\\c\nd\re\tf", 11, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"a\\\"b\\\\c\\nd\\re\\tf\"");
    TEST_END();
}

int test_escape_control_chars(void) {
    TEST_BEGIN();
    char buf[256];
    /* 0x01 should become \u0001 */
    const char input[] = "a\x01" "b";
    size_t pos = json_escape_string(input, 3, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"a\\u0001b\"");
    TEST_END();
}

int test_escape_no_quotes(void) {
    TEST_BEGIN();
    char buf[256];
    size_t pos = json_escape_string("hello", 5, buf, sizeof(buf), 0, 0);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "hello");
    TEST_END();
}

int test_escape_with_offset(void) {
    TEST_BEGIN();
    char buf[256];
    memcpy(buf, "{\"k\":", 5);
    size_t pos = json_escape_string("val", 3, buf, sizeof(buf), 5, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "{\"k\":\"val\"");
    TEST_END();
}

int test_escape_overflow(void) {
    TEST_BEGIN();
    char buf[4]; /* too small for "hello" with quotes */
    size_t pos = json_escape_string("hello", 5, buf, sizeof(buf), 0, 1);
    ASSERT_EQ((int)pos, 0);
    TEST_END();
}

int test_escape_empty_string(void) {
    TEST_BEGIN();
    char buf[256];
    size_t pos = json_escape_string("", 0, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"\"");
    TEST_END();
}

int test_escape_forward_slash(void) {
    TEST_BEGIN();
    char buf[256];
    /* Forward slash is valid unescaped in JSON — should pass through as-is */
    size_t pos = json_escape_string("a/b", 3, buf, sizeof(buf), 0, 1);
    ASSERT_TRUE(pos > 0);
    buf[pos] = '\0';
    ASSERT_STR_EQ(buf, "\"a/b\"");
    TEST_END();
}
```

- [ ] **Step 2: Add forward declarations and calls to runner.c**

In `tests/runner.c`, add forward declarations near the top (after the last test block):

```c
/* test_json_validate.c */
int test_escape_simple_string(void);
int test_escape_special_chars(void);
int test_escape_control_chars(void);
int test_escape_no_quotes(void);
int test_escape_with_offset(void);
int test_escape_overflow(void);
int test_escape_empty_string(void);
int test_escape_forward_slash(void);
```

And add the calls at the end of `main()`, before the final `printf`:

```c
    printf("\n--- JSON Escape ---\n");
    failed += test_escape_simple_string();
    failed += test_escape_special_chars();
    failed += test_escape_control_chars();
    failed += test_escape_no_quotes();
    failed += test_escape_with_offset();
    failed += test_escape_overflow();
    failed += test_escape_empty_string();
    failed += test_escape_forward_slash();
```

- [ ] **Step 3: Create minimal header to make tests compile**

Create `src/core/json_validate.h`:

```c
#ifndef NUTSHELL_JSON_VALIDATE_H
#define NUTSHELL_JSON_VALIDATE_H

#include <stddef.h>

/* Write a JSON-escaped string into buf at position pos.
 * If add_quotes is non-zero, wraps the output in double quotes.
 * s_len is the byte length of s (does not rely on NUL terminator).
 * Returns new position, or 0 on overflow. */
size_t json_escape_string(const char *s, size_t s_len, char *buf,
                          size_t buf_size, size_t pos, int add_quotes);

/* Validate a JSON buffer for syntax correctness and proper string escaping.
 * Returns 1 if valid, 0 if invalid.
 * On failure, writes a human-readable error to error (includes byte offset). */
int json_validate(const char *buf, size_t len, char *error, size_t error_size);

#endif
```

- [ ] **Step 4: Run tests — verify they fail**

Run: `make test`

Expected: Linker error — `json_escape_string` undefined. Tests fail because no implementation exists yet.

- [ ] **Step 5: Commit**

```bash
git add tests/test_json_validate.c src/core/json_validate.h tests/runner.c
git commit -m "test: add failing tests for json_escape_string"
```

---

### Task 2: Consolidated Escape Function — Implementation

**Files:**
- Create: `src/core/json_validate.c`

- [ ] **Step 1: Implement json_escape_string**

Create `src/core/json_validate.c`:

```c
#include "json_validate.h"
#include <string.h>
#include <stdio.h>

/* ---- Consolidated JSON escape function ----------------------------------- */

size_t json_escape_string(const char *s, size_t s_len, char *buf,
                          size_t buf_size, size_t pos, int add_quotes)
{
    if (!s || !buf || pos >= buf_size) return 0;

    if (add_quotes) {
        if (pos + 1 >= buf_size) return 0;
        buf[pos++] = '"';
    }

    for (size_t i = 0; i < s_len; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *esc = NULL;
        char u_esc[7];

        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if (c < 0x20) {
                    snprintf(u_esc, sizeof(u_esc), "\\u%04x", (unsigned)c);
                    esc = u_esc;
                }
                break;
        }

        if (esc) {
            size_t len = strlen(esc);
            if (pos + len >= buf_size) return 0;
            memcpy(buf + pos, esc, len);
            pos += len;
        } else {
            if (pos + 1 >= buf_size) return 0;
            buf[pos++] = (char)c;
        }
    }

    if (add_quotes) {
        if (pos + 1 >= buf_size) return 0;
        buf[pos++] = '"';
    }

    return pos;
}
```

- [ ] **Step 2: Run tests — verify they pass**

Run: `make test`

Expected: All 8 `JSON Escape` tests PASS. All other tests still PASS.

- [ ] **Step 3: Commit**

```bash
git add src/core/json_validate.c
git commit -m "feat: implement consolidated json_escape_string"
```

---

### Task 3: JSON Validator — Tests

**Files:**
- Modify: `tests/test_json_validate.c`
- Modify: `tests/runner.c`

- [ ] **Step 1: Add validator tests to test_json_validate.c**

Append to `tests/test_json_validate.c`:

```c
/* ---- json_validate tests ---- */

int test_validate_simple_object(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"key\":\"value\"}";
    ASSERT_TRUE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_nested_object(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"a\":{\"b\":[1,2,3]},\"c\":true}";
    ASSERT_TRUE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_string_escapes(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"msg\":\"hello\\nworld\\t\\\"quoted\\\"\"}";
    ASSERT_TRUE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_unicode_escape(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"c\":\"\\u0041\"}";
    ASSERT_TRUE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_all_value_types(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"s\":\"str\",\"n\":42,\"f\":3.14,\"t\":true,\"fa\":false,\"nu\":null}";
    ASSERT_TRUE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_raw_newline_in_string(void) {
    TEST_BEGIN();
    char err[256] = "";
    /* Raw newline inside a JSON string — invalid */
    const char json[] = "{\"msg\":\"hello\nworld\"}";
    ASSERT_FALSE(json_validate(json, sizeof(json) - 1, err, sizeof(err)));
    ASSERT_TRUE(strstr(err, "control character") != NULL);
    TEST_END();
}

int test_validate_raw_tab_in_string(void) {
    TEST_BEGIN();
    char err[256] = "";
    /* Raw tab inside a JSON string — invalid */
    const char json[] = "{\"msg\":\"hello\tworld\"}";
    ASSERT_FALSE(json_validate(json, sizeof(json) - 1, err, sizeof(err)));
    ASSERT_TRUE(strstr(err, "control character") != NULL);
    TEST_END();
}

int test_validate_bad_escape_sequence(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"msg\":\"hello\\xworld\"}";
    ASSERT_FALSE(json_validate(json, strlen(json), err, sizeof(err)));
    ASSERT_TRUE(strstr(err, "escape") != NULL);
    TEST_END();
}

int test_validate_truncated_unicode(void) {
    TEST_BEGIN();
    char err[256] = "";
    /* \u00 — only 2 hex digits instead of 4 */
    const char *json = "{\"c\":\"\\u00\"}";
    ASSERT_FALSE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_unmatched_brace(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"key\":\"value\"";
    ASSERT_FALSE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_unmatched_bracket(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "[1,2,3";
    ASSERT_FALSE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_trailing_comma_object(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"a\":1,}";
    ASSERT_FALSE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_trailing_comma_array(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "[1,2,]";
    ASSERT_FALSE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_empty_input(void) {
    TEST_BEGIN();
    char err[256] = "";
    ASSERT_FALSE(json_validate("", 0, err, sizeof(err)));
    TEST_END();
}

int test_validate_null_input(void) {
    TEST_BEGIN();
    char err[256] = "";
    ASSERT_FALSE(json_validate(NULL, 0, err, sizeof(err)));
    TEST_END();
}

int test_validate_mismatched_brackets(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"a\":[}";
    ASSERT_FALSE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_negative_number(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "{\"n\":-42}";
    ASSERT_TRUE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}

int test_validate_array_of_objects(void) {
    TEST_BEGIN();
    char err[256] = "";
    const char *json = "[{\"a\":1},{\"b\":2}]";
    ASSERT_TRUE(json_validate(json, strlen(json), err, sizeof(err)));
    TEST_END();
}
```

- [ ] **Step 2: Add forward declarations and calls to runner.c**

Add forward declarations:

```c
int test_validate_simple_object(void);
int test_validate_nested_object(void);
int test_validate_string_escapes(void);
int test_validate_unicode_escape(void);
int test_validate_all_value_types(void);
int test_validate_raw_newline_in_string(void);
int test_validate_raw_tab_in_string(void);
int test_validate_bad_escape_sequence(void);
int test_validate_truncated_unicode(void);
int test_validate_unmatched_brace(void);
int test_validate_unmatched_bracket(void);
int test_validate_trailing_comma_object(void);
int test_validate_trailing_comma_array(void);
int test_validate_empty_input(void);
int test_validate_null_input(void);
int test_validate_mismatched_brackets(void);
int test_validate_negative_number(void);
int test_validate_array_of_objects(void);
```

Add calls:

```c
    printf("\n--- JSON Validator ---\n");
    failed += test_validate_simple_object();
    failed += test_validate_nested_object();
    failed += test_validate_string_escapes();
    failed += test_validate_unicode_escape();
    failed += test_validate_all_value_types();
    failed += test_validate_raw_newline_in_string();
    failed += test_validate_raw_tab_in_string();
    failed += test_validate_bad_escape_sequence();
    failed += test_validate_truncated_unicode();
    failed += test_validate_unmatched_brace();
    failed += test_validate_unmatched_bracket();
    failed += test_validate_trailing_comma_object();
    failed += test_validate_trailing_comma_array();
    failed += test_validate_empty_input();
    failed += test_validate_null_input();
    failed += test_validate_mismatched_brackets();
    failed += test_validate_negative_number();
    failed += test_validate_array_of_objects();
```

- [ ] **Step 3: Run tests — verify validator tests fail**

Run: `make test`

Expected: Linker error or test failures — `json_validate` is declared but not yet implemented.

- [ ] **Step 4: Commit**

```bash
git add tests/test_json_validate.c tests/runner.c
git commit -m "test: add failing tests for json_validate"
```

---

### Task 4: JSON Validator — Implementation

**Files:**
- Modify: `src/core/json_validate.c`

- [ ] **Step 1: Implement json_validate**

Append to `src/core/json_validate.c`, after the `json_escape_string` function:

```c
/* ---- JSON validator (single-pass state machine) -------------------------- */

#define VALIDATE_MAX_DEPTH 128

int json_validate(const char *buf, size_t len, char *error, size_t error_size)
{
    if (!buf || len == 0) {
        if (error && error_size > 0)
            snprintf(error, error_size, "Empty or null JSON input");
        return 0;
    }

    /* Nesting stack: '{' or '[' */
    char stack[VALIDATE_MAX_DEPTH];
    int depth = 0;

    int in_string = 0;
    int escape_next = 0;
    int unicode_remaining = 0;  /* counts down hex digits expected after \u */

    /* Context tracking for trailing-comma detection */
    /* after_comma[d] = 1 if the last significant token at depth d was a comma */
    char after_comma[VALIDATE_MAX_DEPTH];
    memset(after_comma, 0, sizeof(after_comma));

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];

        /* Inside a \uXXXX escape — expect hex digits */
        if (unicode_remaining > 0) {
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F')) {
                unicode_remaining--;
                continue;
            }
            if (error && error_size > 0)
                snprintf(error, error_size,
                         "Invalid unicode escape at byte %zu", i);
            return 0;
        }

        /* Inside an escape sequence (char after \) */
        if (escape_next) {
            escape_next = 0;
            switch (c) {
                case '"': case '\\': case '/': case 'b':
                case 'f': case 'n': case 'r': case 't':
                    continue;
                case 'u':
                    unicode_remaining = 4;
                    continue;
                default:
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Invalid escape sequence '\\%c' at byte %zu",
                                 (char)c, i);
                    return 0;
            }
        }

        /* Inside a string literal */
        if (in_string) {
            if (c == '\\') {
                escape_next = 1;
                continue;
            }
            if (c == '"') {
                in_string = 0;
                continue;
            }
            if (c < 0x20) {
                if (error && error_size > 0)
                    snprintf(error, error_size,
                             "Unescaped control character 0x%02x at byte %zu",
                             (unsigned)c, i);
                return 0;
            }
            continue;
        }

        /* Outside strings — skip whitespace */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;

        /* Structural characters */
        switch (c) {
            case '"':
                in_string = 1;
                if (depth > 0) after_comma[depth] = 0;
                continue;

            case '{': case '[':
                if (depth >= VALIDATE_MAX_DEPTH) {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Nesting too deep at byte %zu", i);
                    return 0;
                }
                stack[depth] = (char)c;
                depth++;
                if (depth < VALIDATE_MAX_DEPTH)
                    after_comma[depth] = 0;
                continue;

            case '}':
                if (depth == 0 || stack[depth - 1] != '{') {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Unexpected '}' at byte %zu", i);
                    return 0;
                }
                if (after_comma[depth]) {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Trailing comma before '}' at byte %zu", i);
                    return 0;
                }
                depth--;
                if (depth > 0) after_comma[depth] = 0;
                continue;

            case ']':
                if (depth == 0 || stack[depth - 1] != '[') {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Unexpected ']' at byte %zu", i);
                    return 0;
                }
                if (after_comma[depth]) {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Trailing comma before ']' at byte %zu", i);
                    return 0;
                }
                depth--;
                if (depth > 0) after_comma[depth] = 0;
                continue;

            case ',':
                if (depth > 0)
                    after_comma[depth] = 1;
                continue;

            case ':':
                continue;

            default:
                /* Numbers, true, false, null — skip their characters */
                if (c == 't' || c == 'f' || c == 'n') {
                    /* Consume literal: true, false, null */
                    const char *lit = NULL;
                    size_t lit_len = 0;
                    if (c == 't') { lit = "true"; lit_len = 4; }
                    else if (c == 'f') { lit = "false"; lit_len = 5; }
                    else { lit = "null"; lit_len = 4; }

                    if (i + lit_len > len ||
                        memcmp(buf + i, lit, lit_len) != 0) {
                        if (error && error_size > 0)
                            snprintf(error, error_size,
                                     "Invalid literal at byte %zu", i);
                        return 0;
                    }
                    i += lit_len - 1;
                    if (depth > 0) after_comma[depth] = 0;
                    continue;
                }
                if (c == '-' || (c >= '0' && c <= '9')) {
                    /* Consume number */
                    while (i + 1 < len) {
                        unsigned char nc = (unsigned char)buf[i + 1];
                        if ((nc >= '0' && nc <= '9') || nc == '.' ||
                            nc == 'e' || nc == 'E' || nc == '+' || nc == '-')
                            i++;
                        else
                            break;
                    }
                    if (depth > 0) after_comma[depth] = 0;
                    continue;
                }
                if (error && error_size > 0)
                    snprintf(error, error_size,
                             "Unexpected character '%c' at byte %zu",
                             (char)c, i);
                return 0;
        }
    }

    /* Check we're not still inside a string */
    if (in_string) {
        if (error && error_size > 0)
            snprintf(error, error_size, "Unterminated string at end of input");
        return 0;
    }

    /* Check nesting closed */
    if (depth != 0) {
        if (error && error_size > 0)
            snprintf(error, error_size,
                     "Unclosed '%c' — depth %d at end of input",
                     stack[depth - 1], depth);
        return 0;
    }

    return 1;
}
```

- [ ] **Step 2: Run tests — verify all pass**

Run: `make test`

Expected: All `JSON Escape` and `JSON Validator` tests PASS. All other tests still PASS.

- [ ] **Step 3: Commit**

```bash
git add src/core/json_validate.c
git commit -m "feat: implement json_validate state machine"
```

---

### Task 5: Replace Escape Functions in ai_prompt.c

**Files:**
- Modify: `src/core/ai_prompt.c`

- [ ] **Step 1: Add include and replace calls**

In `src/core/ai_prompt.c`:

1. Add `#include "json_validate.h"` near the other includes at the top.
2. Delete the static `json_escape_str` function (lines 258–299).
3. Find-and-replace all calls from `json_escape_str(s, buf, buf_size, pos)` to `json_escape_string(s, strlen(s), buf, buf_size, pos, 1)`.

The calls in `ai_prompt.c` all pass NUL-terminated strings and expect surrounding quotes, so `add_quotes = 1` and `s_len = strlen(s)`.

Affected call sites (search for `json_escape_str(` in `ai_prompt.c`):
- Line 328: `json_escape_str(conv->model, ...)`
- Line 354: `json_escape_str(ai_msg_content(&conv->messages[0]), ...)`
- All other occurrences in `ai_build_request_body_ex()` and surrounding code

Each becomes: `json_escape_string(str, strlen(str), buf, buf_size, pos, 1)`

- [ ] **Step 2: Run tests — verify all pass**

Run: `make test`

Expected: All existing `ai_prompt` tests still PASS (especially `test_ai_build_body_escapes`). All `JSON Escape` and `JSON Validator` tests PASS.

- [ ] **Step 3: Commit**

```bash
git add src/core/ai_prompt.c
git commit -m "refactor: use consolidated json_escape_string in ai_prompt.c"
```

---

### Task 6: Replace Escape Functions in ai_tools.c

**Files:**
- Modify: `src/core/ai_tools.c`

- [ ] **Step 1: Add include and replace calls**

In `src/core/ai_tools.c`:

1. Add `#include "json_validate.h"` near the other includes at the top.
2. Delete the static `json_escape_str` function (lines 11–49).
3. Replace all calls. The `ai_tools.c` version does NOT add quotes, so `add_quotes = 0`. The old signature was `json_escape_str(src, src_len, buf + pos, max - pos)` returning bytes written. The new one is `json_escape_string(src, src_len, buf, max, pos, 0)` returning new absolute position.

Each call site needs adjustment. For example, old pattern:
```c
size_t escaped = json_escape_str(t->name, strlen(t->name), buf + pos, max - pos);
if (escaped == 0) return 0;
pos += escaped;
```
New pattern:
```c
size_t new_pos = json_escape_string(t->name, strlen(t->name), buf, max, pos, 0);
if (new_pos == 0) return 0;
pos = new_pos;
```

Apply this to all call sites found by searching `json_escape_str(` in `ai_tools.c` (approximately 10 call sites at lines 78, 168, 249, 256, 292, 299, 488, 497, 524, 533).

- [ ] **Step 2: Run tests — verify all pass**

Run: `make test`

Expected: All existing `ai_tools` tests still PASS. All other tests still PASS.

- [ ] **Step 3: Commit**

```bash
git add src/core/ai_tools.c
git commit -m "refactor: use consolidated json_escape_string in ai_tools.c"
```

---

### Task 7: Integration — Validate Before Sending

**Files:**
- Modify: `src/ui/ai_chat.c`

Note: `ai_chat.c` is in `src/ui/` and excluded from the test build (`NON_TEST_SRCS`). This task is integration-only; the validator and escape function are already fully tested.

- [ ] **Step 1: Add include**

Add `#include "json_validate.h"` near the other includes in `ai_chat.c`.

- [ ] **Step 2: Add validation after body-build at line ~980 (initial build with tools)**

After:
```c
arg->body_len = ai_build_request_body_tools(&d->conv, att, ...);
```

Add:
```c
{
    char json_err[256];
    if (arg->body_len == 0 ||
        !json_validate(arg->body, arg->body_len, json_err, sizeof(json_err))) {
        /* Surface error to user and abort send */
        ai_chat_append_status(d, json_err[0] ? json_err : "JSON build failed");
        free(arg);
        d->busy = 0;
        return;
    }
}
```

- [ ] **Step 3: Add validation after body-build at line ~986 (initial build without tools)**

Same pattern after the `ai_build_request_body_ex()` call in the `else` branch.

- [ ] **Step 4: Add validation after body-build at line ~706 (agentic loop rebuild)**

Same pattern after the rebuild call inside the agentic loop. On failure, set `loop_done = 1` and post a status message instead of continuing.

- [ ] **Step 5: Verify cross-compile succeeds**

Run: `make clean && make release`

(Remember to bump version in `resource.h` and `README.md` first per project rules.)

Expected: Clean compile with no warnings under `-Werror`.

- [ ] **Step 6: Run tests**

Run: `make test`

Expected: All tests pass (ai_chat.c is excluded from the test build, so no new test impact).

- [ ] **Step 7: Commit**

```bash
git add src/ui/ai_chat.c src/ui/resource.h README.md
git commit -m "feat: validate JSON before sending to AI provider"
```

---

### Task 8: Update todo.md

**Files:**
- Modify: `todo.md`

- [ ] **Step 1: Mark the todo as done**

Change:
```
- [ ] Use a validator to ensure that all special characters in JSON files are escaped. Use it on all JSON messages to the AI provider.
```
To:
```
- [x] Use a validator to ensure that all special characters in JSON files are escaped. Use it on all JSON messages to the AI provider.
```

- [ ] **Step 2: Commit**

```bash
git add todo.md
git commit -m "docs: mark JSON validator todo as done"
```
