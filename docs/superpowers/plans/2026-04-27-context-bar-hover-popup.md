# Context Bar Hover Popup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a multi-line popup over the AI chat context bar on hover, displaying the current input/output token breakdown — using actual API-reported numbers when available, marked estimates otherwise.

**Architecture:** Add a pure formatter function `ai_format_context_tooltip()` to `src/core/ai_prompt.c` (testable natively). Register the existing `d->hContextBar` as a tool on the existing `d->hTooltip` tooltip control with `LPSTR_TEXTCALLBACK`, and handle `TTN_GETDISPINFOA` in the chat panel's `WM_NOTIFY` branch to fill the text from live `AiChatData` state on each hover.

**Tech Stack:** C11, MinGW (Win32 cross-compile), Win32 common controls (PROGRESS_CLASS, TOOLTIPS_CLASS), custom test framework (`tests/test_framework.h`).

**Spec:** `docs/superpowers/specs/2026-04-27-context-bar-hover-popup-design.md`

**Conventions in this codebase (see `nutshell/CLAUDE.md`):**
- Always `make clean && make release` (and bump version) before producing a Windows binary.
- Always `make test` after changes.
- Pure logic that needs tests goes in `src/core/`, not `src/ui/` (UI files are excluded from test builds).
- The native test build is `gcc` with `-D_TEST -Wno-unused-function`; the Win32 build is `x86_64-w64-mingw32-gcc` with `-Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion`. Code must compile clean under both.

---

## File Structure

| File | Role |
|---|---|
| `src/core/ai_prompt.h` | Add declaration for `ai_format_context_tooltip()`. |
| `src/core/ai_prompt.c` | Implement `ai_format_context_tooltip()` plus a small static helper `format_int_with_commas()`. |
| `src/ui/ai_chat.c` | Add `tooltip_buf[512]` field to `AiChatData`; register the context bar as a callback-text tool on `d->hTooltip`; handle `TTN_GETDISPINFOA` in `WM_NOTIFY`. |
| `tests/test_context_tooltip.c` | New: unit tests for `ai_format_context_tooltip()`. |
| `tests/runner.c` | Declare and call the new test functions. |
| `src/ui/resource.h`, `README.md` | Version bump (mandatory before each Windows build). |

`Makefile` does not need editing — `TEST_SRCS` globs all `.c` under `src/core/` plus `tests/*.c`, so adding the new test file is automatic. (Verified: `TEST_SRCS = $(APP_SRCS) $(TEST_IMPL_SRCS)` where `APP_SRCS` excludes `NON_TEST_SRCS = src/main.c $(wildcard src/ui/*.c)` etc.)

---

## Task 1: Pure formatter — actual-data path

**Files:**
- Modify: `src/core/ai_prompt.h` (add declaration after `ai_format_context_label` near line 219)
- Modify: `src/core/ai_prompt.c` (add implementation below `ai_format_context_label` near line 239)
- Create: `tests/test_context_tooltip.c`
- Modify: `tests/runner.c` (declare prototype + add to test runner)

This task introduces the formatter and the actual-data path only. Estimate path, unknown-limit path, and NULL-model handling come in Task 2.

- [ ] **Step 1: Add the prototype to the header**

In `src/core/ai_prompt.h`, append after the `ai_format_context_label` block (after the line that currently reads `int ai_format_context_label(int tokens, int limit, char *buf, size_t buf_size);`):

```c
/* Build a multi-line tooltip string describing context window usage.
 *
 * Behaviour:
 *  - If actual_in + actual_out > 0: shows separate "Input tokens" and
 *    "Output tokens" lines plus a "Total" line.
 *  - Otherwise: shows a single "Total (estimated)" line using
 *    estimated_total, prefixed with '~'.
 *  - If context_limit <= 0: omits the "/ <limit> (<pct>%)" tail and
 *    appends a "Limit: unknown" line.
 *  - If model_name is NULL or empty: the "Model:" line is omitted.
 *
 * Numbers >= 1000 are formatted with thousands separators (commas).
 *
 * Returns the number of bytes written (excluding NUL), or 0 if buf is
 * NULL or buf_size == 0. Output is always NUL-terminated when buf_size
 * >= 1.
 */
int ai_format_context_tooltip(int actual_in, int actual_out,
                              int estimated_total,
                              int context_limit,
                              const char *model_name,
                              char *buf, size_t buf_size);
```

- [ ] **Step 2: Create the new test file with one failing test for the actual-data path**

Create `tests/test_context_tooltip.c`:

```c
/* tests/test_context_tooltip.c — Tests for ai_format_context_tooltip */
#include "test_framework.h"
#include "ai_prompt.h"
#include <string.h>

int test_context_tooltip_actual_basic(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        18432, 4128,        /* actual in/out */
        0,                  /* estimate (unused when actuals present) */
        200000,             /* limit */
        "deepseek-chat",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Context usage") != NULL);
    ASSERT_TRUE(strstr(buf, "Input tokens:")  != NULL);
    ASSERT_TRUE(strstr(buf, "18,432")         != NULL);
    ASSERT_TRUE(strstr(buf, "Output tokens:") != NULL);
    ASSERT_TRUE(strstr(buf, "4,128")          != NULL);
    ASSERT_TRUE(strstr(buf, "Total:")         != NULL);
    ASSERT_TRUE(strstr(buf, "22,560")         != NULL);
    ASSERT_TRUE(strstr(buf, "200,000")        != NULL);
    ASSERT_TRUE(strstr(buf, "(11%)")          != NULL);
    ASSERT_TRUE(strstr(buf, "Model:")         != NULL);
    ASSERT_TRUE(strstr(buf, "deepseek-chat")  != NULL);
    ASSERT_TRUE(strstr(buf, "(estimated)")    == NULL);
    TEST_END();
}
```

- [ ] **Step 3: Wire the test into the runner**

In `tests/runner.c`:

(a) Add a prototype block right after the existing `test_context_bar.c` prototype block (after the `int test_context_label_small_buf(void);` line near 1318):

```c
/* test_context_tooltip.c */
int test_context_tooltip_actual_basic(void);
```

(b) Add the call inside the "--- Context Bar ---" section, immediately after the `failed += test_context_label_small_buf();` line near 2774:

```c
    failed += test_context_tooltip_actual_basic();
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `make test 2>&1 | tail -30`

Expected: build failure with `undefined reference to 'ai_format_context_tooltip'` (the function isn't implemented yet). This confirms the test is wired up and the symbol is missing.

- [ ] **Step 5: Implement the actual-data path**

In `src/core/ai_prompt.c`, append after the `ai_format_context_label` function (after the closing `}` near line 239):

```c
/* Internal helper: format a non-negative int with thousands separators
 * (e.g. 18432 -> "18,432"). Writes into buf of size buf_size and
 * NUL-terminates. Returns bytes written (excluding NUL). */
static int format_int_with_commas(int value, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    if (value < 0) value = 0;

    char tmp[16];
    int tmp_len = snprintf(tmp, sizeof(tmp), "%d", value);
    if (tmp_len < 0) tmp_len = 0;

    /* Walk digits from the right, inserting commas every 3. */
    int out_len = 0;
    int digits_since_comma = 0;
    char rev[24];
    for (int i = tmp_len - 1; i >= 0; i--) {
        if (digits_since_comma == 3) {
            rev[out_len++] = ',';
            digits_since_comma = 0;
        }
        rev[out_len++] = tmp[i];
        digits_since_comma++;
    }

    /* Reverse into buf. */
    int written = 0;
    for (int i = out_len - 1; i >= 0 && (size_t)written < buf_size - 1; i--)
        buf[written++] = rev[i];
    buf[written] = '\0';
    return written;
}

int ai_format_context_tooltip(int actual_in, int actual_out,
                              int estimated_total,
                              int context_limit,
                              const char *model_name,
                              char *buf, size_t buf_size)
{
    (void)estimated_total; /* used in Task 2 */
    if (!buf || buf_size == 0) return 0;
    buf[0] = '\0';

    int actual_total = actual_in + actual_out;
    int has_actual = (actual_total > 0);

    /* This task only handles the actual-data path with a known limit
     * and a non-NULL model. Other paths are added in Task 2. */
    if (!has_actual || context_limit <= 0 || !model_name || !model_name[0])
        return 0;

    char in_str[24], out_str[24], tot_str[24], lim_str[24];
    format_int_with_commas(actual_in,   in_str,  sizeof(in_str));
    format_int_with_commas(actual_out,  out_str, sizeof(out_str));
    format_int_with_commas(actual_total, tot_str, sizeof(tot_str));
    format_int_with_commas(context_limit, lim_str, sizeof(lim_str));

    int pct = (actual_total * 100) / context_limit;
    if (pct > 100) pct = 100;

    return snprintf(buf, buf_size,
        "Context usage\r\n"
        "\r\n"
        "Input tokens:   %s\r\n"
        "Output tokens:  %s\r\n"
        "Total:          %s / %s (%d%%)\r\n"
        "\r\n"
        "Model:          %s",
        in_str, out_str, tot_str, lim_str, pct, model_name);
}
```

(Note: `\r\n` is used because Win32 tooltips render newlines as `\r\n`. On the Linux test path, `strstr` substring checks still match.)

- [ ] **Step 6: Run the test to verify it passes**

Run: `make test 2>&1 | tail -10`

Expected: a "PASS" line for `test_context_tooltip_actual_basic` and the overall `Total: ... failed: 0` summary line.

- [ ] **Step 7: Commit**

```bash
git add src/core/ai_prompt.h src/core/ai_prompt.c \
        tests/test_context_tooltip.c tests/runner.c
git commit -m "feat(core): ai_format_context_tooltip — actual-data path"
```

---

## Task 2: Estimate, unknown-limit, and NULL-model branches

**Files:**
- Modify: `src/core/ai_prompt.c` (extend `ai_format_context_tooltip`)
- Modify: `tests/test_context_tooltip.c` (add tests)
- Modify: `tests/runner.c` (add prototypes + run calls)

- [ ] **Step 1: Add failing tests for the remaining branches**

Append to `tests/test_context_tooltip.c`:

```c
int test_context_tooltip_estimate_marks_estimated(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        0, 0,          /* no actuals */
        12345,         /* estimate */
        64000,
        "deepseek-chat",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Total (estimated)") != NULL);
    ASSERT_TRUE(strstr(buf, "~12,345") != NULL);
    ASSERT_TRUE(strstr(buf, "64,000")  != NULL);
    ASSERT_TRUE(strstr(buf, "(19%)")   != NULL);
    /* Estimate path must NOT show the input/output split */
    ASSERT_TRUE(strstr(buf, "Input tokens:")  == NULL);
    ASSERT_TRUE(strstr(buf, "Output tokens:") == NULL);
    TEST_END();
}

int test_context_tooltip_unknown_limit_actual(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        18432, 4128,
        0,
        0,             /* unknown limit */
        "custom-model",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Limit: unknown") != NULL);
    /* No percentage when limit is unknown. */
    ASSERT_TRUE(strstr(buf, "%") == NULL);
    /* Total still appears, just without "/ <limit>". */
    ASSERT_TRUE(strstr(buf, "Total:") != NULL);
    ASSERT_TRUE(strstr(buf, "22,560") != NULL);
    TEST_END();
}

int test_context_tooltip_unknown_limit_estimate(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        0, 0, 5000,
        0,
        "custom-model",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Total (estimated)") != NULL);
    ASSERT_TRUE(strstr(buf, "~5,000")        != NULL);
    ASSERT_TRUE(strstr(buf, "Limit: unknown") != NULL);
    ASSERT_TRUE(strstr(buf, "%") == NULL);
    TEST_END();
}

int test_context_tooltip_null_model_omits_line(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        100, 50, 0, 64000, NULL, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Model:") == NULL);
    ASSERT_TRUE(strstr(buf, "Total:") != NULL);
    TEST_END();
}

int test_context_tooltip_empty_model_omits_line(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        100, 50, 0, 64000, "", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Model:") == NULL);
    TEST_END();
}

int test_context_tooltip_zero_actuals_zero_estimate(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        0, 0, 0, 64000, "deepseek-chat", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Total (estimated)") != NULL);
    ASSERT_TRUE(strstr(buf, "~0")    != NULL);
    ASSERT_TRUE(strstr(buf, "(0%)")  != NULL);
    TEST_END();
}

int test_context_tooltip_large_numbers(void) {
    TEST_BEGIN();
    char buf[512];
    int n = ai_format_context_tooltip(
        1234567, 7654321, 0,
        9999999,
        "big-model",
        buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "1,234,567") != NULL);
    ASSERT_TRUE(strstr(buf, "7,654,321") != NULL);
    ASSERT_TRUE(strstr(buf, "8,888,888") != NULL);
    ASSERT_TRUE(strstr(buf, "9,999,999") != NULL);
    TEST_END();
}

int test_context_tooltip_small_buf_safe(void) {
    TEST_BEGIN();
    char buf[16];
    int n = ai_format_context_tooltip(
        18432, 4128, 0, 200000, "deepseek-chat",
        buf, sizeof(buf));
    /* Must not crash, must NUL-terminate, must report bytes
     * snprintf would have written (>=0). */
    ASSERT_TRUE(n >= 0);
    ASSERT_EQ(buf[sizeof(buf) - 1], '\0');
    TEST_END();
}

int test_context_tooltip_null_buf_returns_zero(void) {
    TEST_BEGIN();
    int n = ai_format_context_tooltip(
        100, 50, 0, 64000, "m", NULL, 100);
    ASSERT_EQ(n, 0);
    TEST_END();
}

int test_context_tooltip_zero_size_returns_zero(void) {
    TEST_BEGIN();
    char buf[16];
    int n = ai_format_context_tooltip(
        100, 50, 0, 64000, "m", buf, 0);
    ASSERT_EQ(n, 0);
    TEST_END();
}
```

- [ ] **Step 2: Wire the new tests into the runner**

In `tests/runner.c`, extend the `/* test_context_tooltip.c */` prototype block (added in Task 1):

```c
/* test_context_tooltip.c */
int test_context_tooltip_actual_basic(void);
int test_context_tooltip_estimate_marks_estimated(void);
int test_context_tooltip_unknown_limit_actual(void);
int test_context_tooltip_unknown_limit_estimate(void);
int test_context_tooltip_null_model_omits_line(void);
int test_context_tooltip_empty_model_omits_line(void);
int test_context_tooltip_zero_actuals_zero_estimate(void);
int test_context_tooltip_large_numbers(void);
int test_context_tooltip_small_buf_safe(void);
int test_context_tooltip_null_buf_returns_zero(void);
int test_context_tooltip_zero_size_returns_zero(void);
```

And extend the corresponding run block in the "--- Context Bar ---" section:

```c
    failed += test_context_tooltip_actual_basic();
    failed += test_context_tooltip_estimate_marks_estimated();
    failed += test_context_tooltip_unknown_limit_actual();
    failed += test_context_tooltip_unknown_limit_estimate();
    failed += test_context_tooltip_null_model_omits_line();
    failed += test_context_tooltip_empty_model_omits_line();
    failed += test_context_tooltip_zero_actuals_zero_estimate();
    failed += test_context_tooltip_large_numbers();
    failed += test_context_tooltip_small_buf_safe();
    failed += test_context_tooltip_null_buf_returns_zero();
    failed += test_context_tooltip_zero_size_returns_zero();
```

- [ ] **Step 3: Run tests to verify the new ones fail**

Run: `make test 2>&1 | tail -20`

Expected: many of the new tests fail (the function still returns 0 on the un-implemented branches and produces no output).

- [ ] **Step 4: Replace the implementation with the full version**

In `src/core/ai_prompt.c`, replace the body of `ai_format_context_tooltip` (the function added in Task 1, **not** `format_int_with_commas`) with:

```c
int ai_format_context_tooltip(int actual_in, int actual_out,
                              int estimated_total,
                              int context_limit,
                              const char *model_name,
                              char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    buf[0] = '\0';

    int actual_total = actual_in + actual_out;
    int has_actual   = (actual_total > 0);
    int has_limit    = (context_limit > 0);
    int has_model    = (model_name && model_name[0]);

    char tot_str[24], lim_str[24];
    int  display_total = has_actual ? actual_total : estimated_total;
    if (display_total < 0) display_total = 0;
    format_int_with_commas(display_total, tot_str, sizeof(tot_str));
    if (has_limit)
        format_int_with_commas(context_limit, lim_str, sizeof(lim_str));

    int pct = 0;
    if (has_limit) {
        pct = (display_total * 100) / context_limit;
        if (pct > 100) pct = 100;
    }

    /* Build into a working buffer to keep the snprintf calls simple. */
    char tmp[512];
    int  off = 0;
    int  rem = (int)sizeof(tmp);

    int w = snprintf(tmp + off, (size_t)rem, "Context usage\r\n\r\n");
    if (w < 0) w = 0;
    off += w; rem -= w; if (rem < 0) rem = 0;

    if (has_actual) {
        char in_str[24], out_str[24];
        format_int_with_commas(actual_in,  in_str,  sizeof(in_str));
        format_int_with_commas(actual_out, out_str, sizeof(out_str));
        w = snprintf(tmp + off, (size_t)rem,
            "Input tokens:   %s\r\n"
            "Output tokens:  %s\r\n",
            in_str, out_str);
        if (w < 0) w = 0;
        off += w; rem -= w; if (rem < 0) rem = 0;

        if (has_limit) {
            w = snprintf(tmp + off, (size_t)rem,
                "Total:          %s / %s (%d%%)\r\n",
                tot_str, lim_str, pct);
        } else {
            w = snprintf(tmp + off, (size_t)rem,
                "Total:          %s\r\n"
                "Limit: unknown\r\n",
                tot_str);
        }
    } else {
        if (has_limit) {
            w = snprintf(tmp + off, (size_t)rem,
                "Total (estimated):  ~%s / %s (%d%%)\r\n",
                tot_str, lim_str, pct);
        } else {
            w = snprintf(tmp + off, (size_t)rem,
                "Total (estimated):  ~%s\r\n"
                "Limit: unknown\r\n",
                tot_str);
        }
    }
    if (w < 0) w = 0;
    off += w; rem -= w; if (rem < 0) rem = 0;

    if (has_model) {
        w = snprintf(tmp + off, (size_t)rem,
            "\r\nModel:          %s", model_name);
        if (w < 0) w = 0;
        off += w; rem -= w; if (rem < 0) rem = 0;
    }

    /* Copy into the caller's buffer with truncation. */
    size_t copy_len = (size_t)off;
    if (copy_len > buf_size - 1) copy_len = buf_size - 1;
    memcpy(buf, tmp, copy_len);
    buf[copy_len] = '\0';
    return (int)copy_len;
}
```

- [ ] **Step 5: Run the tests**

Run: `make test 2>&1 | tail -25`

Expected: all `test_context_tooltip_*` tests pass and `failed: 0` in the summary.

- [ ] **Step 6: Commit**

```bash
git add src/core/ai_prompt.c tests/test_context_tooltip.c tests/runner.c
git commit -m "feat(core): ai_format_context_tooltip — estimate/unknown/null-model paths"
```

---

## Task 3: Add the `tooltip_buf` field to `AiChatData`

**Files:**
- Modify: `src/ui/ai_chat.c` (struct definition near line 219)

This is a tiny prep task so the next task can reference the field cleanly. No behaviour change.

- [ ] **Step 1: Add the buffer field**

In `src/ui/ai_chat.c`, find the lines (near 218-219):

```c
    int  actual_input_tokens;  /* last known input tokens from API (0 if unavailable) */
    int  actual_output_tokens; /* last known output tokens from API (0 if unavailable) */
```

Immediately after them, add:

```c
    /* Buffer for the context-bar hover tooltip text. Populated on
     * each TTN_GETDISPINFO callback so the tip always reflects the
     * latest token state. */
    char tooltip_buf[512];
```

- [ ] **Step 2: Verify the Windows build still compiles**

Run: `make clean && make release 2>&1 | tail -20`

Note: per `CLAUDE.md`, every Windows build requires a version bump first. Do that:

(a) Open `src/ui/resource.h` and bump the patch version. Find the lines with `APP_VERSION "1.0.X"` and `APP_VERSION_BINARY 1,0,X,0` and increment X by 1.

(b) Open `README.md` and update the `**Version**:` line to match.

(c) Then run: `make clean && make release 2>&1 | tail -20`

Expected: build succeeds, no warnings.

- [ ] **Step 3: Verify native tests still pass**

Run: `make test 2>&1 | tail -10`

Expected: `failed: 0`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/ai_chat.c src/ui/resource.h README.md
git commit -m "feat(ui): add tooltip_buf field to AiChatData"
```

---

## Task 4: Register the context bar as a callback-text tooltip

**Files:**
- Modify: `src/ui/ai_chat.c` (tooltip creation block near line 2042-2069)

- [ ] **Step 1: Bump max tip width and register the bar**

In `src/ui/ai_chat.c`, find this block (around line 2045-2046):

```c
        if (nd->hTooltip) {
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, 300);
```

Change `300` to `400` to allow wider multi-line tooltip rendering:

```c
        if (nd->hTooltip) {
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, 400);
```

Then, immediately before the closing `}` of this `if (nd->hTooltip)` block (currently right after the `add_tooltip(... "Send\nSend your message to the AI...");` call near line 2068), add:

```c
            /* Register the context bar as a callback-text tool. The
             * actual text is built on demand in the WM_NOTIFY handler
             * for TTN_GETDISPINFOA, so it always reflects current
             * token state without us having to push updates. */
            if (nd->hContextBar) {
                TOOLINFO ti;
                memset(&ti, 0, sizeof(ti));
                ti.cbSize   = sizeof(ti);
                ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
                ti.hwnd     = hwnd;
                ti.uId      = (UINT_PTR)nd->hContextBar;
                ti.lpszText = LPSTR_TEXTCALLBACK;
                SendMessage(nd->hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
            }
```

- [ ] **Step 2: Build for Windows to confirm registration compiles**

Bump the patch version in `src/ui/resource.h` and `README.md` again (every Windows build), then:

Run: `make clean && make release 2>&1 | tail -20`

Expected: build succeeds, no warnings under `-Werror -Wpedantic -Wshadow -Wconversion`.

- [ ] **Step 3: Run the native tests to confirm nothing regressed**

Run: `make test 2>&1 | tail -10`

Expected: `failed: 0`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/ai_chat.c src/ui/resource.h README.md
git commit -m "feat(ui): register context bar as callback-text tooltip tool"
```

---

## Task 5: Handle `TTN_GETDISPINFOA` in `WM_NOTIFY`

**Files:**
- Modify: `src/ui/ai_chat.c` (WM_NOTIFY case near line 2112-2117)

- [ ] **Step 1: Replace the WM_NOTIFY case**

In `src/ui/ai_chat.c`, find this block (line 2112-2117):

```c
    case WM_NOTIFY: {
        /* EN_LINK handling removed — ChatListView handles thinking toggle
         * inline via its own click handling in the list view WndProc. */
        (void)lParam;
        break;
    }
```

Replace it with:

```c
    case WM_NOTIFY: {
        /* EN_LINK handling removed — ChatListView handles thinking toggle
         * inline via its own click handling in the list view WndProc. */
        NMHDR *hdr = (NMHDR *)lParam;
        if (d && hdr && hdr->code == TTN_GETDISPINFOA &&
            d->hContextBar &&
            hdr->idFrom == (UINT_PTR)d->hContextBar) {
            NMTTDISPINFOA *nm = (NMTTDISPINFOA *)lParam;
            int actual = d->actual_input_tokens + d->actual_output_tokens;
            int est = (actual > 0) ? 0
                                   : ai_context_estimate_tokens(&d->conv);
            ai_format_context_tooltip(
                d->actual_input_tokens,
                d->actual_output_tokens,
                est,
                d->context_limit,
                d->conv.model,
                d->tooltip_buf, sizeof(d->tooltip_buf));
            nm->lpszText = d->tooltip_buf;
            return 0;
        }
        break;
    }
```

- [ ] **Step 2: Bump version and build for Windows**

Bump the patch version in `src/ui/resource.h` and `README.md`, then:

Run: `make clean && make release 2>&1 | tail -20`

Expected: build succeeds, no warnings.

If the build fails with `'TTN_GETDISPINFOA' undeclared` or `NMTTDISPINFOA` issues, ensure `<commctrl.h>` is reachable from `ai_chat.c`. It already is via the existing tooltip code in this file (the `add_tooltip` helper uses `TOOLINFO` and `TTM_ADDTOOL`), so this is just a sanity check.

- [ ] **Step 3: Run the native tests**

Run: `make test 2>&1 | tail -10`

Expected: `failed: 0`. (UI files are excluded from tests; the underlying formatter is exercised by Tasks 1-2.)

- [ ] **Step 4: Manual verification on Windows**

Copy the built `build/win/nutshell.exe` to a Windows host (or run via Wine). Then:

1. Launch nutshell, open the AI Assist panel.
2. Hover the context bar at the top. Before sending any message, expect the tooltip to show:
   - "Context usage"
   - "Total (estimated): ~<n>" with `(estimated)` marker
   - the configured limit and percentage (or "Limit: unknown" for an unknown model)
   - "Model: <name>"
3. Send a message. After the response arrives, hover again. Expect:
   - "Input tokens:" and "Output tokens:" lines with the API-reported numbers
   - "Total:" line with sum and percentage
   - No "(estimated)" marker
4. Hover during a busy/marquee state — tooltip should still render with the most recent counts.
5. Switch to a custom model with no entry in `ai_model_context_limit()` and confirm the "Limit: unknown" line appears.

Document each scenario with pass/fail in the commit message body.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ai_chat.c src/ui/resource.h README.md
git commit -m "feat(ui): show token-breakdown popup on context bar hover

Hovering the AI chat context bar now shows a multi-line tooltip with
the input/output/total token counts. Falls back to an estimate marked
'(estimated)' when no API-reported usage is available yet. Handles
unknown context limits and missing model names.

Manual verification on Windows: pre-send estimate, post-send actual,
busy/marquee, unknown limit — all OK."
```

---

## Self-Review

**Spec coverage check (against `docs/superpowers/specs/2026-04-27-context-bar-hover-popup-design.md`):**

- Requirement 1 (popup on hover): Task 4 (registration) + Task 5 (callback handler).
- Requirement 2 (actual data when available): Task 1 (actual-data path) + Task 2 (full impl) + Task 5 (live data wired in).
- Requirement 3 (estimate marked): Task 2 (`(estimated)` and `~` prefix), tested in `test_context_tooltip_estimate_marks_estimated`.
- Requirement 4 (always reflects current state): Task 5 uses `LPSTR_TEXTCALLBACK` so content is rebuilt every hover.
- Requirement 5 (unknown limit): Task 2, tested in `test_context_tooltip_unknown_limit_actual` / `_estimate`.
- Requirement 6 (no marquee interference): Task 5 reads `d->actual_*` and `d->conv` directly; no interaction with `PBM_*` or marquee state. Manual step in Task 5 verifies.

**Type consistency:**
- `ai_format_context_tooltip` signature is identical in header (Task 1 Step 1), in test calls (Tasks 1-2), and in the WM_NOTIFY handler (Task 5).
- `d->tooltip_buf` is declared as `char[512]` in Task 3 and used as `d->tooltip_buf, sizeof(d->tooltip_buf)` in Task 5 — sizes never hardcoded inconsistently.
- `d->conv.model` field name verified against `src/core/ai_prompt.h:45` (`char model[64];`).
- `TTN_GETDISPINFOA` paired with `NMTTDISPINFOA` (ANSI versions, consistent with the rest of the codebase that uses ANSI Win32 APIs throughout).

**Placeholder scan:** none. Every code step shows complete code; every command step shows the exact command and expected output.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-27-context-bar-hover-popup.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
