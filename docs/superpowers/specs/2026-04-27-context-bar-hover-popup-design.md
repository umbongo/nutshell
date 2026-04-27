# Context Bar Hover Popup — Design

**Date:** 2026-04-27
**Status:** Draft
**Area:** AI chat panel UI

## Goal

When the user hovers the context-usage progress bar at the top of the AI
chat panel, show a popup with a breakdown of the token data behind the
bar. The popup must always prefer real API-reported numbers and clearly
mark estimated numbers when no real data is available yet.

## Background

The context bar (`d->hContextBar` in `src/ui/ai_chat.c`) is a Win32
progress bar control that visualises how full the model's context window
is. It already shows a compact label ("23k / 200k - 11%") drawn by a
subclass paint handler. The state behind the bar lives on `AiChatData`:

- `d->actual_input_tokens` / `d->actual_output_tokens` — last known
  input/output token counts reported by the API; both are `0` until
  the first response returns.
- `d->context_limit` — model context window in tokens; `0` if unknown.
- `d->conv` — local conversation, used for estimation via
  `ai_context_estimate_tokens(&d->conv)` when actual data is missing.
- Model name is stored on the conversation/profile and is already used
  to derive `context_limit` via `ai_model_context_limit()`.

`ai_chat.c` already owns a Win32 tooltip control `d->hTooltip` and a
helper `add_tooltip()` at line 1500 that registers static-text tools
against child controls. `settings.c` registers a similar tooltip control
and uses `TTM_SETMAXTIPWIDTH` to allow multi-line tips.

## Requirements

1. Hovering the context bar shows a popup with the token breakdown.
2. The popup shows actual API-reported tokens when available.
3. When actual data is unavailable, the popup falls back to an estimate
   and explicitly marks it (e.g. `(estimated)`).
4. The popup reflects the current state every time it appears — it
   must not show stale data after a response has updated the counters.
5. When the model's context limit is unknown, the popup still shows
   what numbers it has and notes the limit is unknown.
6. The popup must not interfere with the existing busy/marquee state
   of the bar.

## Approach

Use the existing Win32 tooltip control `d->hTooltip` and register the
context bar as a tool with **dynamic text**: register with
`LPSTR_TEXTCALLBACK` and provide content via `TTN_GETDISPINFOA` in the
chat panel's `WM_NOTIFY` handler. This keeps the popup native, themed
by the OS, and consistent with the other tooltips in the chat panel
(no custom popup window, no extra paint code).

### Content

The popup content is built each time the tooltip is about to show.

**When actual data is available** (`actual_input_tokens +
actual_output_tokens > 0`):

```
Context usage

Input tokens:   18,432
Output tokens:   4,128
Total:          22,560 / 200,000 (11%)

Model:          deepseek-chat
```

**When only an estimate is available**:

```
Context usage

Total (estimated):  ~22,560 / 200,000 (11%)

Model:              deepseek-chat
```

**When the limit is unknown** (`context_limit <= 0`): omit the
denominator and percent, replace with `Limit: unknown`.

Numbers are formatted with thousands separators (commas). The `~`
prefix on the estimate total reinforces the `(estimated)` marker.

### Components

1. **`ai_format_context_tooltip()`** — new pure function in
   `src/core/ai_prompt.{h,c}`, sibling of `ai_format_context_label()`.
   Signature:
   ```c
   int ai_format_context_tooltip(
       int actual_in, int actual_out,
       int estimated_total,    /* used only when actual_in+actual_out == 0 */
       int context_limit,      /* 0 = unknown */
       const char *model_name, /* may be NULL */
       char *buf, size_t buf_size);
   ```
   Returns the number of bytes written (excluding NUL). Builds the
   multi-line text described above. Lives in `src/core/` so it can be
   unit-tested natively per the project rules in `CLAUDE.md`.

2. **Tooltip registration** in `ai_chat.c` where `d->hContextBar` is
   created. Register the bar as a tool on `d->hTooltip` with
   `lpszText = LPSTR_TEXTCALLBACK` and flags `TTF_SUBCLASS |
   TTF_IDISHWND`. Ensure `TTM_SETMAXTIPWIDTH` is set on `d->hTooltip`
   (with a width like 400 px) so newline-bearing text wraps correctly.

3. **`WM_NOTIFY` handler** in the chat panel's WndProc. Catch
   `TTN_GETDISPINFOA` (`NMHDR.code`). When `nmtdi->hdr.idFrom ==
   (UINT_PTR)d->hContextBar` (because `TTF_IDISHWND` makes the tool ID
   the HWND), build the tip text into a buffer owned by `AiChatData`
   (`char tooltip_buf[512]`) by calling `ai_format_context_tooltip()`
   with the live values from `d`, then set
   `nmtdi->lpszText = d->tooltip_buf`.

### Data flow

```
  hover                Win32             WndProc
  context bar  --->   tooltip ctl  --->  WM_NOTIFY/TTN_GETDISPINFOA
                                              |
                                              v
                                  ai_format_context_tooltip(
                                    d->actual_input_tokens,
                                    d->actual_output_tokens,
                                    ai_context_estimate_tokens(&d->conv),
                                    d->context_limit,
                                    model_name,
                                    d->tooltip_buf, sizeof d->tooltip_buf)
                                              |
                                              v
                                  nmtdi->lpszText = d->tooltip_buf
                                              |
                                              v
                                          tooltip shows
```

The estimate is computed unconditionally on each hover; it is cheap
(a count over conversation messages) and only displayed if the actual
totals are zero, but always passing it keeps the formatter pure and
testable.

### Edge cases

- `actual_in + actual_out == 0` and conversation is empty → estimate
  is `0`; show `Total (estimated): ~0 / <limit> (0%)` (or `Limit:
  unknown` form). Acceptable.
- Bar is in marquee/busy state → tooltip still shows the last known
  numbers. No special handling needed; the data on `d` is correct.
- Model name unknown / NULL → omit the `Model:` line.
- Buffer overflow → `snprintf` into the fixed buffer; truncate cleanly.

## Testing

Add `tests/test_context_tooltip.c` exercising
`ai_format_context_tooltip()`:

- Actual data path with all fields populated.
- Estimate path: zero actuals, non-zero estimate, `(estimated)` marker
  present, `~` prefix present.
- Unknown limit (`context_limit = 0`): no percent, `Limit: unknown`
  line present.
- NULL model name: `Model:` line absent.
- Comma formatting on large numbers (e.g. 1,234,567).
- Small buffer truncation does not crash and returns a valid C string.

Register the new test functions in `tests/runner.c`.

Manual verification on Windows build:

- Hover bar before any AI call → tooltip shows estimated total with
  `(estimated)`.
- Send a message, wait for response → tooltip switches to the actual
  input/output split.
- Switch to a custom model with unknown limit → tooltip shows
  `Limit: unknown`.
- Hover while marquee/busy is active → tooltip still appears with the
  most recent counts.

## Files touched

- `src/core/ai_prompt.h` — declare `ai_format_context_tooltip()`.
- `src/core/ai_prompt.c` — implement `ai_format_context_tooltip()` and
  a small comma-formatting helper.
- `src/ui/ai_chat.c`:
  - add `char tooltip_buf[512]` to `AiChatData`,
  - register the bar as a callback-text tool on `d->hTooltip` and set
    `TTM_SETMAXTIPWIDTH`,
  - handle `TTN_GETDISPINFOA` in the chat panel's `WM_NOTIFY` branch.
- `tests/test_context_tooltip.c` — new file.
- `tests/runner.c` — declare and call the new test functions.
- `Makefile` — add `tests/test_context_tooltip.c` to test sources if
  the test list is enumerated explicitly.

## Out of scope

- Custom-themed popup window. The native Win32 tooltip is sufficient
  for now; a themed bubble can be revisited if visual consistency
  becomes a concern.
- Persisting historical token usage or showing a per-message
  breakdown.
- Click behaviour on the bar (this spec is hover-only).
