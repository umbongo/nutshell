# AI Chat Markdown — Tables + Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Loosen markdown table detection to accept leading whitespace, and add a Settings checkbox to enable/disable markdown rendering in the AI chat.

**Architecture:** Two independent edits. (1) Skip leading whitespace before the `|` check in `md_classify_line` and `md_is_table_separator` ([src/ui/markdown.h](../../../src/ui/markdown.h)). (2) Add a new `markdown_render_enabled` field to `Settings`, plumb it through the JSON loader, the Settings dialog, and `ChatListView` via a setter that gates the four `md_render_text`/`md_measure_text` call sites with a `draw_text_utf8` plain-text fallback.

**Tech Stack:** C, MinGW cross-compile to Win32 + native gcc tests, GDI, JSON, the existing `markdown.h` parser.

**Spec:** [docs/superpowers/specs/2026-04-28-md-tables-and-toggle-design.md](../specs/2026-04-28-md-tables-and-toggle-design.md)

---

## Context

Two visible problems remain in the AI chat after the layout fix landed at commit `e3944c4` (markdown layout fix plan):
1. AI sometimes emits tables with leading whitespace, so `md_classify_line` doesn't flag them as `MD_LINE_TABLE` and they render as wrapped prose.
2. No way for the user to disable markdown rendering when they prefer plain text or hit a rendering bug.

This plan fixes both.

## File Map

- **Modify** [src/ui/markdown.h](../../../src/ui/markdown.h) — loosen the table check and `md_is_table_separator`.
- **Modify** [src/config/config.h](../../../src/config/config.h) — add `markdown_render_enabled` field.
- **Modify** [src/config/loader.c](../../../src/config/loader.c) — default value, JSON read/write.
- **Modify** [src/ui/settings.c](../../../src/ui/settings.c) — Settings dialog checkbox + tooltip + IDC_*.
- **Modify** [src/ui/chat_listview.h](../../../src/ui/chat_listview.h) — declare `chat_listview_set_render_markdown` setter and add an `int render_markdown` field on the `ChatListView` struct.
- **Modify** [src/ui/chat_listview.c](../../../src/ui/chat_listview.c) — initialise the flag (default 1), implement the setter, gate the four call sites with plain-text fallback.
- **Modify** [src/ui/window.c](../../../src/ui/window.c) — wire `g_config.settings.markdown_render_enabled` into `chat_listview_set_render_markdown` at init and after Settings-OK.
- **Modify** [tests/test_markdown_render.c](../../../tests/test_markdown_render.c) — new table-detection tests.
- **Modify** [tests/test_settings.c](../../../tests/test_settings.c) — new default + JSON roundtrip tests.
- **Modify** [tests/runner.c](../../../tests/runner.c) — register new tests.
- **Modify** [src/ui/resource.h](../../../src/ui/resource.h), [README.md](../../../README.md) — version bump.

## Out of Scope

Documented in the spec: real grid table rendering, loosening other block-marker classifiers, DPI scaling of `MD_*` constants, per-message toggle.

---

## Task 1: Loosen `md_classify_line` table detection (TDD, portable)

**Files:**
- Modify: [src/ui/markdown.h](../../../src/ui/markdown.h) — `md_classify_line` (table case, around line 118), `md_is_table_separator` (around line 296).
- Modify: [tests/test_markdown_render.c](../../../tests/test_markdown_render.c) — add tests.
- Modify: [tests/runner.c](../../../tests/runner.c) — register tests.

- [ ] **Step 1.1: Write failing tests**

Append to [tests/test_markdown_render.c](../../../tests/test_markdown_render.c) (before EOF):

```c
/* ---- table-line detection: leading whitespace ---- */

int test_md_classify_table_with_leading_spaces(void) {
    TEST_BEGIN();
    /* Two leading spaces before | should still classify as TABLE. */
    MdLineInfo info = md_classify_line("  | A | B |", 0);
    ASSERT_EQ(info.type, MD_LINE_TABLE);
    TEST_END();
}

int test_md_classify_table_with_leading_tab(void) {
    TEST_BEGIN();
    /* Tab before | should still classify as TABLE. */
    MdLineInfo info = md_classify_line("\t| A | B |", 0);
    ASSERT_EQ(info.type, MD_LINE_TABLE);
    TEST_END();
}

int test_md_classify_table_indented_in_code_block(void) {
    TEST_BEGIN();
    /* Inside code block, even an indented |…| stays as CODE. */
    MdLineInfo info = md_classify_line("  | col |", 1);
    ASSERT_EQ(info.type, MD_LINE_CODE);
    TEST_END();
}

int test_md_table_sep_with_leading_spaces(void) {
    TEST_BEGIN();
    ASSERT_TRUE(md_is_table_separator("  |---|"));
    ASSERT_TRUE(md_is_table_separator("\t|:--:|"));
    TEST_END();
}

int test_md_table_sep_leading_spaces_not_separator(void) {
    TEST_BEGIN();
    /* Leading spaces with text after | is still NOT a separator. */
    ASSERT_FALSE(md_is_table_separator("  | text |"));
    TEST_END();
}
```

Append forward declarations and registrations to [tests/runner.c](../../../tests/runner.c) — find the existing `test_md_classify_*` and `test_md_table_sep_*` blocks and add immediately after them. Use the codebase's `failed += test_fn();` pattern (NOT a `RUN_TEST` macro — it doesn't exist):

```c
    failed += test_md_classify_table_with_leading_spaces();
    failed += test_md_classify_table_with_leading_tab();
    failed += test_md_classify_table_indented_in_code_block();
    failed += test_md_table_sep_with_leading_spaces();
    failed += test_md_table_sep_leading_spaces_not_separator();
```

Also add forward declarations near the top of `runner.c` next to the other `test_md_*` declarations:

```c
int test_md_classify_table_with_leading_spaces(void);
int test_md_classify_table_with_leading_tab(void);
int test_md_classify_table_indented_in_code_block(void);
int test_md_table_sep_with_leading_spaces(void);
int test_md_table_sep_leading_spaces_not_separator(void);
```

- [ ] **Step 1.2: Run tests to verify they fail**

```bash
cd /home/thomas/nutshell && make test 2>&1 | tail -30
```
Expected: `test_md_classify_table_with_leading_spaces`, `_tab`, `test_md_table_sep_with_leading_spaces` all FAIL (existing checks reject leading whitespace). `test_md_classify_table_indented_in_code_block` and `test_md_table_sep_leading_spaces_not_separator` may pass or fail; that's fine — they confirm the negative cases stay correct after the fix.

- [ ] **Step 1.3: Loosen `md_classify_line` table check**

In [src/ui/markdown.h](../../../src/ui/markdown.h), replace the existing table block in `md_classify_line` (currently around line 118):

```c
    /* Table: starts with | */
    if (line[0] == '|') {
        info.type = MD_LINE_TABLE;
        return info;
    }
```

with:

```c
    /* Table: first non-whitespace character is |. Allow up to a few leading
     * spaces or tabs so AI-emitted tables with light indentation still render
     * as a table block instead of falling through to PARAGRAPH. */
    {
        int ti = 0;
        while (line[ti] == ' ' || line[ti] == '\t') ti++;
        if (line[ti] == '|') {
            info.type = MD_LINE_TABLE;
            return info;
        }
    }
```

- [ ] **Step 1.4: Loosen `md_is_table_separator`**

In [src/ui/markdown.h](../../../src/ui/markdown.h), replace `md_is_table_separator` (currently around line 296):

```c
static inline int md_is_table_separator(const char *line)
{
    if (!line || line[0] != '|') return 0;
    for (int i = 1; line[i]; i++) {
        if (line[i] != '-' && line[i] != '|' && line[i] != ' ' && line[i] != ':')
            return 0;
    }
    return 1;
}
```

with:

```c
static inline int md_is_table_separator(const char *line)
{
    if (!line) return 0;
    int i = 0;
    while (line[i] == ' ' || line[i] == '\t') i++;
    if (line[i] != '|') return 0;
    for (i = i + 1; line[i]; i++) {
        if (line[i] != '-' && line[i] != '|' && line[i] != ' ' && line[i] != ':')
            return 0;
    }
    return 1;
}
```

- [ ] **Step 1.5: Run tests to verify they pass**

```bash
cd /home/thomas/nutshell && make test 2>&1 | tail -10
```
Expected: full suite green; the five new tests pass; pre-existing `test_md_classify_table_line`, `test_md_classify_table_in_code_block`, `test_md_table_sep_*` still pass.

- [ ] **Step 1.6: Commit**

```bash
cd /home/thomas/nutshell
git add src/ui/markdown.h tests/test_markdown_render.c tests/runner.c
git commit -m "fix(md): classify indented |---|/| col | as MD_LINE_TABLE"
```

---

## Task 2: Add `markdown_render_enabled` to Settings (TDD, portable)

**Files:**
- Modify: [src/config/config.h](../../../src/config/config.h) — add field after `ai_web_fetch_enabled`.
- Modify: [src/config/loader.c](../../../src/config/loader.c) — default, JSON read, JSON write.
- Modify: [tests/test_settings.c](../../../tests/test_settings.c) — default + JSON roundtrip tests.
- Modify: [tests/runner.c](../../../tests/runner.c) — register tests.

- [ ] **Step 2.1: Write failing tests**

Append to [tests/test_settings.c](../../../tests/test_settings.c) (find an appropriate place — likely near other settings tests, before EOF):

```c
/* ---- markdown_render_enabled default + JSON roundtrip ---- */

int test_settings_markdown_render_default(void) {
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    /* Default: markdown rendering is ON. */
    ASSERT_EQ(s.markdown_render_enabled, 1);
    TEST_END();
}

int test_settings_markdown_render_roundtrip_off(void) {
    TEST_BEGIN();
    /* Write a config with the flag explicitly false; verify it loads as 0. */
    const char *path = "/tmp/nutshell_test_md_off.config";
    FILE *f = fopen(path, "w");
    ASSERT_NOT_NULL(f);
    fputs("{ \"settings\": { \"markdown_render_enabled\": false } }", f);
    fclose(f);
    Config *cfg = config_load(path);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.markdown_render_enabled, 0);
    config_free(cfg);
    remove(path);
    TEST_END();
}

int test_settings_markdown_render_roundtrip_on(void) {
    TEST_BEGIN();
    /* Explicitly true → loads as 1. */
    const char *path = "/tmp/nutshell_test_md_on.config";
    FILE *f = fopen(path, "w");
    ASSERT_NOT_NULL(f);
    fputs("{ \"settings\": { \"markdown_render_enabled\": true } }", f);
    fclose(f);
    Config *cfg = config_load(path);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.markdown_render_enabled, 1);
    config_free(cfg);
    remove(path);
    TEST_END();
}

int test_settings_markdown_render_missing_field(void) {
    TEST_BEGIN();
    /* Missing field → falls back to default (1). */
    const char *path = "/tmp/nutshell_test_md_missing.config";
    FILE *f = fopen(path, "w");
    ASSERT_NOT_NULL(f);
    fputs("{ \"settings\": {} }", f);
    fclose(f);
    Config *cfg = config_load(path);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.markdown_render_enabled, 1);
    config_free(cfg);
    remove(path);
    TEST_END();
}
```

Add forward declarations and registrations to [tests/runner.c](../../../tests/runner.c) (next to existing `test_settings_*`):

```c
int test_settings_markdown_render_default(void);
int test_settings_markdown_render_roundtrip_off(void);
int test_settings_markdown_render_roundtrip_on(void);
int test_settings_markdown_render_missing_field(void);
```

```c
    failed += test_settings_markdown_render_default();
    failed += test_settings_markdown_render_roundtrip_off();
    failed += test_settings_markdown_render_roundtrip_on();
    failed += test_settings_markdown_render_missing_field();
```

If `test_settings.c` doesn't already include `<stdio.h>` and `<stdlib.h>`, ensure they're present so `fopen`, `fputs`, `fclose`, `remove` are available. Check the existing `Config *config_load(const char *path)` and `void config_free(Config *cfg)` signatures by reading the top of `src/config/config.h` and `src/config/loader.c` — adapt the test if the actual API differs (e.g. `config_load` may return `int` and take `Config*` — read the file, don't guess).

- [ ] **Step 2.2: Verify tests fail**

```bash
cd /home/thomas/nutshell && make test 2>&1 | tail -30
```
Expected: compilation error — `'Settings' has no member named 'markdown_render_enabled'`.

- [ ] **Step 2.3: Add field to Settings struct**

In [src/config/config.h](../../../src/config/config.h), add the new field at the end of the `Settings` struct (just before the closing `};`). Use the same style as `ai_web_fetch_enabled` at line 39:

```c
    int markdown_render_enabled;     /* render AI replies as markdown */
```

- [ ] **Step 2.4: Default + JSON read + JSON write**

In [src/config/loader.c](../../../src/config/loader.c):

(a) In `config_default_settings()` (around line 131 — find the line `s->ai_web_fetch_enabled = 0;`), add immediately after:

```c
    s->markdown_render_enabled = 1;
```

(b) In the JSON read block (around line 273-274 — find `s->ai_web_fetch_enabled = json_obj_bool(jset, "ai_web_fetch_enabled", ...);`), add immediately after:

```c
    s->markdown_render_enabled = json_obj_bool(jset, "markdown_render_enabled",
                                               s->markdown_render_enabled);
```

(c) In the JSON write block (around line 419-420 — find `fprintf(f, "    \"ai_web_fetch_enabled\": %s,\n", ...);`), add immediately after:

```c
    fprintf(f, "    \"markdown_render_enabled\": %s,\n",
            s->markdown_render_enabled ? "true" : "false");
```

Watch the trailing comma on the previous JSON line — the config writer produces a sequence of fields; the last field must NOT have a trailing comma. Read the surrounding lines to see whether your new line should end in `,\n` or `\n` (it should end in `,\n` if other fields follow it; if it's the very last field, drop the comma and shift the trailing comma onto the previous line). Inspect the actual output by running `make test` after wiring and confirm valid JSON.

- [ ] **Step 2.5: Run tests to verify they pass**

```bash
cd /home/thomas/nutshell && make test 2>&1 | tail -10
```
Expected: full suite green; the four new tests pass.

- [ ] **Step 2.6: Commit**

```bash
cd /home/thomas/nutshell
git add src/config/config.h src/config/loader.c tests/test_settings.c tests/runner.c
git commit -m "feat(config): add markdown_render_enabled setting"
```

---

## Task 3: Settings dialog checkbox

**Files:**
- Modify: [src/ui/settings.c](../../../src/ui/settings.c) — IDC_*, tooltip table entry, BS_AUTOCHECKBOX creation, IsDlgButtonChecked read on OK. Win32-only; no native test possible. Verification is build cleanliness + manual visual check (Task 6).

- [ ] **Step 3.1: Inspect current AI section layout**

```bash
grep -n "IDC_AI_WEB_FETCH\|Permit Web Fetch\|ai_web_fetch_enabled" /home/thomas/nutshell/src/ui/settings.c
```

Expected hits at approximately: control-ID definition, tooltip table entry (~line 322), checkbox creation (~line 659), OK read (~line 1110). Use these as templates.

- [ ] **Step 3.2: Add IDC for the new checkbox**

In [src/ui/settings.c](../../../src/ui/settings.c), find where `IDC_AI_WEB_FETCH` is defined (likely a `#define` near the top of the file, or inline in a table). Add `IDC_AI_MD_RENDER` immediately after, with a unique value (one greater than `IDC_AI_WEB_FETCH`'s value, OR following whatever pattern the file uses — read the file).

Example:
```c
#define IDC_AI_MD_RENDER  XXXX  /* exact number depends on existing IDC range */
```

- [ ] **Step 3.3: Add tooltip entry**

Find the tooltip table near line 322 — it contains entries like:

```c
{ IDC_AI_WEB_FETCH,
  "Allow the AI to fetch arbitrary URLs as a tool call." },
```

Add a new entry immediately after:

```c
{ IDC_AI_MD_RENDER,
  "Render AI replies as formatted markdown. Turn off to see raw text." },
```

- [ ] **Step 3.4: Create the checkbox in WM_CREATE**

Find the existing "Permit Web Fetch" checkbox creation (~line 659), which looks like:

```c
HWND hFetch = CreateWindow("BUTTON", "Permit Web Fetch",
    WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
    ex, y, ew, fetch_h, hwnd, (HMENU)IDC_AI_WEB_FETCH, NULL, NULL);
SendMessage(hFetch, BM_SETCHECK,
            nd->cfg->settings.ai_web_fetch_enabled
                ? BST_CHECKED : BST_UNCHECKED, 0);
y += rh;
```

Immediately after the `y += rh;` (which advances the layout cursor for the next row), add:

```c
HWND hMdRender = CreateWindow("BUTTON", "Render AI markdown",
    WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
    ex, y, ew, fetch_h, hwnd, (HMENU)IDC_AI_MD_RENDER, NULL, NULL);
SendMessage(hMdRender, BM_SETCHECK,
            nd->cfg->settings.markdown_render_enabled
                ? BST_CHECKED : BST_UNCHECKED, 0);
y += rh;
```

If the dialog has a fixed total height (the recent commit `7c1d658` mentions tightening dialog height) and the new row would overflow, also update the dialog's height calculation. Search for `dlg_h` or similar in the same file. If a height bump is needed, add `+ rh` to that calculation.

- [ ] **Step 3.5: Read the checkbox on OK**

Find the OK handler (~line 1110), which looks like:

```c
s->ai_web_fetch_enabled = (IsDlgButtonChecked(hwnd, IDC_AI_WEB_FETCH)
                            == BST_CHECKED) ? 1 : 0;
```

Add immediately after:

```c
s->markdown_render_enabled = (IsDlgButtonChecked(hwnd, IDC_AI_MD_RENDER)
                               == BST_CHECKED) ? 1 : 0;
```

- [ ] **Step 3.6: Build the cross-compile**

```bash
cd /home/thomas/nutshell
make clean && make release 2>&1 | tail -20
```
Expected: build succeeds, no `-Werror` / `-Wshadow` / `-Wconversion` warnings, binary produced.

- [ ] **Step 3.7: Commit**

```bash
git add src/ui/settings.c
git commit -m "feat(ui): Settings checkbox to toggle AI markdown rendering"
```

---

## Task 4: Wire toggle into ChatListView with plain-text fallback

**Files:**
- Modify: [src/ui/chat_listview.h](../../../src/ui/chat_listview.h) — add `int render_markdown` to `ChatListView` struct, declare `chat_listview_set_render_markdown(HWND, int)`.
- Modify: [src/ui/chat_listview.c](../../../src/ui/chat_listview.c) — initialise to 1 in create, implement setter, gate the four `md_*` call sites.
- Modify: [src/ui/window.c](../../../src/ui/window.c) — call the setter at chat-listview create time and after Settings-OK.

- [ ] **Step 4.1: Declare setter and field**

In [src/ui/chat_listview.h](../../../src/ui/chat_listview.h), add a new int field to the `ChatListView` struct (find the struct around lines 10-62; place near the end alongside `theme` or other config-like fields):

```c
    int render_markdown;        /* 1 = markdown render on, 0 = plain text */
```

Add a public setter declaration near the bottom of the header (next to other `chat_listview_*` declarations):

```c
/* Toggle markdown rendering. Triggers a redraw. Default after create: 1. */
void chat_listview_set_render_markdown(HWND hwnd, int enabled);
```

- [ ] **Step 4.2: Initialise field in create**

In [src/ui/chat_listview.c](../../../src/ui/chat_listview.c), find `chat_listview_create` (or wherever the `ChatListView` struct is allocated and initialised). Add:

```c
    lv->render_markdown = 1;
```

immediately alongside the other field initialisations.

- [ ] **Step 4.3: Implement setter**

In [src/ui/chat_listview.c](../../../src/ui/chat_listview.c), add this function near the other public APIs (look for functions named `chat_listview_set_*` and place beside them):

```c
void chat_listview_set_render_markdown(HWND hwnd, int enabled)
{
    ChatListView *lv = (ChatListView *)GetWindowLongPtr(hwnd, 0);
    if (!lv) return;
    int v = enabled ? 1 : 0;
    if (lv->render_markdown == v) return;   /* no-op */
    lv->render_markdown = v;
    /* Layout heights change with the new mode — recalc and redraw. */
    chat_listview_invalidate(hwnd);
}
```

If the existing pattern uses a different "get the LV from HWND" helper or if the `ChatListView` is stored differently, mirror what the existing setters do (e.g. `chat_listview_set_pulse`, `chat_listview_set_activity`).

- [ ] **Step 4.4: Gate the measure call**

In [src/ui/chat_listview.c](../../../src/ui/chat_listview.c), find the existing measurement at line 731:

```c
        int h = md_measure_text(hdc, measure_text, text_w,
                                lv->hFont, lv->hMonoFont, lv->hBoldFont,
                                lv->theme);
```

Replace with:

```c
        int h;
        if (lv->render_markdown) {
            h = md_measure_text(hdc, measure_text, text_w,
                                lv->hFont, lv->hMonoFont, lv->hBoldFont,
                                lv->theme);
        } else {
            HGDIOBJ tf = SelectObject(hdc, lv->hFont ? lv->hFont
                                       : GetStockObject(DEFAULT_GUI_FONT));
            RECT mrc;
            SetRect(&mrc, 0, 0, text_w, 0);
            draw_text_utf8(hdc, measure_text, &mrc,
                           DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            h = mrc.bottom - mrc.top;
            SelectObject(hdc, tf);
        }
```

- [ ] **Step 4.5: Gate the three render calls**

In [src/ui/chat_listview.c](../../../src/ui/chat_listview.c) `draw_ai_text_with_exec`, all three `md_render_text` call sites need the same gate. Define a small static helper at the top of the function (or just before it) so the three sites stay readable:

```c
/* Render `text` as markdown if enabled, else as plain UTF-8 word-wrapped
 * paragraph in lv->hFont. Returns height consumed (pixels). */
static int draw_ai_segment(ChatListView *lv, HDC hdc, const char *text,
                           int x, int y, int width)
{
    if (lv->render_markdown) {
        return md_render_text(hdc, text, x, y, width,
                              lv->hFont, lv->hMonoFont, lv->hBoldFont,
                              lv->theme);
    }
    HGDIOBJ tf = SelectObject(hdc, lv->hFont ? lv->hFont
                              : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(hdc, RGB_FROM_THEME(lv->theme->text_main));
    RECT mrc;
    SetRect(&mrc, x, y, x + width, 0);
    draw_text_utf8(hdc, text, &mrc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int h = mrc.bottom - mrc.top;
    SetRect(&mrc, x, y, x + width, y + h);
    draw_text_utf8(hdc, text, &mrc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, tf);
    return h;
}
```

If `RGB_FROM_THEME` isn't already defined in `chat_listview.c`, search for it — it might live in a header (`ui_theme.h`) or be redefined locally; if neither, copy the definition from [src/ui/md_render.c:17](../../../src/ui/md_render.c#L17):

```c
#define RGB_FROM_THEME(c) \
    RGB(((c) >> 16) & 0xFF, ((c) >> 8) & 0xFF, (c) & 0xFF)
```

Then replace each of the three `md_render_text` calls in `draw_ai_text_with_exec` (lines 923, 938, 960) with calls to `draw_ai_segment`. For example, line 923's call:

```c
        md_render_text(hdc, text, rc->left, rc->top, rc->right - rc->left,
                       lv->hFont, lv->hMonoFont, lv->hBoldFont, lv->theme);
```

becomes:

```c
        draw_ai_segment(lv, hdc, text, rc->left, rc->top,
                        rc->right - rc->left);
```

Line 938's call uses `pos`, `y`, returns `h`:

```c
                int h = draw_ai_segment(lv, hdc, pos, rc->left, y,
                                        rc->right - rc->left);
                y += h;
```

Line 960's call uses `seg`, `y`:

```c
                    int h = draw_ai_segment(lv, hdc, seg, rc->left, y,
                                            rc->right - rc->left);
                    y += h;
```

Read the surrounding context for each site and preserve any other state (pen/font selection, etc.) the existing code did beyond the three `md_render_text` lines. The `[EXEC]`-marker rendering paths (purple text in monospace) are NOT touched — they live alongside the markdown calls and continue to work.

- [ ] **Step 4.6: Wire the setter from window.c**

In [src/ui/window.c](../../../src/ui/window.c), find where `chat_listview_create` (or whatever creates the chat list view HWND) is called. Immediately after, add:

```c
    chat_listview_set_render_markdown(g_chat_list_hwnd,
                                      g_config.settings.markdown_render_enabled);
```

Adjust variable names to match the actual codebase (the chat HWND may live on the AiChatData struct, not `g_chat_list_hwnd` — search for `chat_listview_create(` and follow the returned/stored HWND).

Also find the place where Settings → OK is applied (it should already invalidate the chat list view for theme changes — search for `chat_listview_invalidate` calls). Add the setter call there too:

```c
    chat_listview_set_render_markdown(<chat_hwnd>,
                                      g_config.settings.markdown_render_enabled);
```

- [ ] **Step 4.7: Cross-compile**

```bash
cd /home/thomas/nutshell
make clean && make release 2>&1 | tail -20
make test 2>&1 | tail -5
```
Expected: clean build under strict flags; native tests still pass (1384+ from prior plan + 5 new from Task 1 + 4 new from Task 2 = 1393).

If you hit `-Wshadow` warnings — likely from naming the helper's locals (e.g. `h`) when the calling scope already has `h`. Rename to `seg_h` if needed.

If you hit `-Wunused-parameter` on `draw_ai_segment` — won't happen because all five params are used.

- [ ] **Step 4.8: Commit**

```bash
git add src/ui/chat_listview.h src/ui/chat_listview.c src/ui/window.c
git commit -m "feat(ui): gate AI chat markdown rendering on user toggle"
```

---

## Task 5: Version bump, build, manual visual verification

**Files:**
- Modify: [src/ui/resource.h](../../../src/ui/resource.h)
- Modify: [README.md](../../../README.md)

- [ ] **Step 5.1: Inspect current version**

```bash
grep -E '^#define APP_VERSION' /home/thomas/nutshell/src/ui/resource.h
grep -nE '^\*\*Version\*\*' /home/thomas/nutshell/README.md
```

- [ ] **Step 5.2: Bump patch version**

Increment current patch (likely `1.0.64` → `1.0.65`) in BOTH files. Update `APP_VERSION "1.0.65"` and `APP_VERSION_BINARY 1,0,65,0` in `resource.h`, and the `**Version**: v1.0.65 \` line in `README.md`. `nutshell.rc` inherits via `#include` so no edit needed.

- [ ] **Step 5.3: Final clean build**

```bash
cd /home/thomas/nutshell
make clean && make release 2>&1 | tail -10
```
Expected: success; `build/win/nutshell.exe` is the new artifact.

- [ ] **Step 5.4: Manual visual verification**

Install the new binary on Windows and verify:

1. **Indented table renders as monospace block.** Ask the AI for a directory listing. Confirm the table aligns in monospace and does NOT show each backtick cell as inline-code grey-background fragments.
2. **Markdown toggle visible.** Open Settings → AI Assist section. The new "Render AI markdown" checkbox is present and tooltips on hover.
3. **Default is ON.** Fresh launch shows the box checked.
4. **Toggle OFF → text becomes plain.** Uncheck → OK → trigger an AI response. The new response and previously-rendered ones repaint as plain wrapped text in the regular font; no bold/italic/code spans, no headings styled, no colour for code blocks. `[EXEC]` markers still appear in purple monospace.
5. **Toggle ON → markdown returns.** Re-check → OK → previously-rendered responses repaint as markdown again.
6. **Persistence.** Toggle OFF, close the app, reopen. Setting stays OFF. Toggle ON, close, reopen. Setting stays ON.
7. **Regression sweep.** With markdown ON: bold/italic/headings/lists/blockquotes/code blocks still render correctly (matches the prior plan's checks).

- [ ] **Step 5.5: Commit version bump**

```bash
git add src/ui/resource.h README.md
git commit -m "chore: bump version to 1.0.65"
```

---

## Verification Summary

- **Automated tests** (Linux native): `make test` exercises all parser changes (Task 1) and JSON roundtrip (Task 2).
- **Build sanity** (Win cross): `make clean && make release` clean under `-Werror -Wshadow -Wconversion -Wpedantic -Wformat=2`.
- **Visual regression** (Win binary): the manual checks in Step 5.4 cover both the table fix and the toggle.

## Critical Files Reference

- [src/ui/markdown.h](../../../src/ui/markdown.h) — parser, table-detection edits.
- [src/config/config.h](../../../src/config/config.h) — Settings struct.
- [src/config/loader.c](../../../src/config/loader.c) — defaults + JSON I/O.
- [src/ui/settings.c](../../../src/ui/settings.c) — Settings dialog UI.
- [src/ui/chat_listview.h](../../../src/ui/chat_listview.h), [src/ui/chat_listview.c](../../../src/ui/chat_listview.c) — toggle wiring + plain-text fallback.
- [src/ui/window.c](../../../src/ui/window.c) — call the setter at create + Settings-OK.
- [src/ui/md_render.c](../../../src/ui/md_render.c) — NOT modified; the layouter from the prior plan keeps working.
- [tests/test_markdown_render.c](../../../tests/test_markdown_render.c), [tests/test_settings.c](../../../tests/test_settings.c), [tests/runner.c](../../../tests/runner.c) — new tests + registration.

## Existing Code Reused

- [`md_classify_line`](../../../src/ui/markdown.h) and [`md_is_table_separator`](../../../src/ui/markdown.h) — extended in place; signatures unchanged so the Win32 renderer needs no edits.
- [`config_default_settings`](../../../src/config/loader.c), [`json_obj_bool`](../../../src/core/json.c) — reused exactly as the existing `ai_web_fetch_enabled` field uses them.
- [`draw_text_utf8`](../../../src/ui/chat_listview.c#L188) — existing UTF-8 wrapper around `DrawTextW`, reused for the plain-text fallback.
- The Settings dialog AI Assist section, layout cursor `y`, tooltip table, and IDC_AI_WEB_FETCH — followed as a template by the new IDC_AI_MD_RENDER checkbox.
- [`chat_listview_invalidate`](../../../src/ui/chat_listview.c) — reused in the new setter to trigger a repaint.
