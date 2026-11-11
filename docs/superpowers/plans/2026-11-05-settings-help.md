# Settings Help on Hover Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every control and label in the Settings dialog shows a themed, detailed help tip on hover. In the same PR, the dialog opens tall enough for AI Assistant > Behaviour, Web Access puts Permit Web Fetch first, a second Settings request brings the open dialog forward, and Scrollback Lines takes effect for new tabs, with the terminal allocating scrollback rows only as lines are kept.

**Architecture:** The help texts, labels, defaults and ranges live in a portable table (`src/core/settings_help.{c,h}`), and the native suite tests it against the real defaults and limits. A small Win32 helper (`src/ui/ns_tip.{c,h}`) creates the stock comctl32 tooltip, classic-rendered in the dialog's theme tokens and font. `settings.c` creates every page control through `page_ctrl()`, which needs a help id, and one loop attaches the tools. The pure rules move to `src/core`: search providers, the keep-the-old-value Save rule, the scrollback lookup and the initial window placement. Each has native tests. In `src/term/buffer.c` the ring of row slots keeps its size and indexing, but only the occupied segment holds rows: `term_init()` allocates the screen's rows, `term_scroll()` allocates one row each time the ring grows, and reflow allocates only the rows it keeps. The runtime allocations are fallible and go through a replaceable allocator (`term_set_calloc()`), so the native suite injects failures and checks that growth recycles the oldest row, a resize leaves the terminal unchanged, and entering the alternate screen stays on the primary.

**Tech Stack:** C11, Win32 (comctl32 v6 tooltips, MinGW cross-compile), native gcc test suite (`tests/runner.c`), Wine wintest (`tests/win_runner.c`), PowerShell integration harness.

**Spec:** `docs/superpowers/specs/2026-11-05-settings-help-design.md`. The orchestrator commits the spec, and this plan as `docs/superpowers/plans/2026-11-05-settings-help.md`, on the branch before Task 1. Every implementer reads the spec sections their task names.

## Global Constraints

- Branch `feat/settings-help`, created from `main` at `d73eeec` (v1.2.74, which merged PR #91, `feat/trust-boundary-tool-gate`) or later. Work on `automaton` in a worktree under `~/nutshell-wt/`: MinGW and Wine are needed from Task 3 on.
- **Line numbers in this plan are hints only.** Find each control by its `IDC_*` id and on-screen label, and re-find every line before you edit. `main` may have moved code after this plan was written.
- **The help texts are not repeated in this plan.** Copy each one byte for byte from the spec's "The help texts" section, under that control's `####` heading. Each control's metadata line there gives its id, kind, config key, default and range. In C, escape `\` as `\\` and `"` as `\"`. Then check that the string still reads exactly as the spec does.
- **A table label is the text the dialog shows when you build it.** The spec's headings match the on-screen labels on `main` at `d73eeec`, including `"Tool approval timeout (seconds):"` (since `9e2f1b3`). If a label differs when you build, the on-screen label wins and the spec is corrected. Never change on-screen wording to match the spec.
- ASCII only in the table: `settings.c` uses `TOOLINFOA`. The refresh button's glyph `L"\x21BB"` is not a label, and its table label is `"Refresh models"`.
- `-Werror` under native gcc and under `x86_64-w64-mingw32-gcc`, with `-Wshadow -Wconversion -Wformat=2 -Wpedantic`. Cast explicitly between `int`, `unsigned`, `size_t`, `LPARAM` and `WPARAM`.
- Design-system gate (`tests/test_ui_tokens.c`): `src/ui` has no `RGB(` literal, no `MulDiv(x, dpi, 96)` and no `CreateFont`. Use `ns_scale()`, `ns_font()`, `SP_*` from `ns_type.h`, and `theme_cr()` on token values.
- Portable logic goes in `src/core/`. `src/ui/*.c` is never built natively. Native tests are declared and called in `tests/runner.c`. Win32 tests are declared and called in `tests/win_runner.c`, and their files go in `WIN_ONLY_TESTS`.
- Line endings: `Makefile`, `src/config/loader.c` and `src/term/buffer.c` are **CRLF** in the index (`git ls-files --eol` on `main` at `d73eeec`). Every other existing file this plan touches is LF, and new files are LF. Run `git ls-files --eol <file>` before editing any file not listed here, and keep each file's endings. Before each review run `git diff --stat --ignore-cr-at-eol` and compare it with `git diff --stat`: if a file shows far more lines in the plain stat, its line endings flipped and must be restored.
- No `git stash`. Stage by path and read every `??` line of `git status --short` before committing. Never use `git add -u` or `git add -A`.
- The version is bumped only in Task 10. No earlier task touches `resource.h`, `README.md`'s `**Version**:` line or `build/win/nutshell.exe`. If a `make release` fails after `make clean`, run `git restore build/win/nutshell.exe`.
- Do not edit README's `<!-- shortcuts:begin -->`...`<!-- shortcuts:end -->` block.
- Every commit message ends with this block, verbatim:

```
Models: claude-opus-5-5 (orchestration, review); claude-sonnet-5-5 (implementation, task review); claude-fable-5-2 (rulings)

Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg
```

- Verification commands, run from the repository root: `make -j test`, `make test SAN=1`, `make lint-gate`, `xvfb-run -a make wintest` (Task 3 on), the Windows compile check below (Tasks 1 to 9), and, in Task 10 only, `make clean && make release` and `bash .github/scripts/check-version.sh origin/main`.
- **No `make release` and no `make clean` before Task 10** (CLAUDE.md: every build of the exe gets a version bump, and `make clean` deletes the tracked `build/win/nutshell.exe`). To check that a task's `src/` files compile clean for Windows, use **the Windows compile check**: `make -B` followed by each touched `.c` file with `.c` replaced by `.o`, for example `make -B src/term/buffer.o src/ui/window.o`, then `rm -f` the same `.o` paths. The Makefile's own pattern rule (`%.o: %.c`, `$(CC) $(CFLAGS) -c $< -o $@`) compiles each one with `x86_64-w64-mingw32-gcc` and the release flags (`-Werror`, the full warning set, the vcpkg include path). It links nothing, writes no exe and runs no `make clean`; the `.o` files are gitignored. `-B` makes it recompile even when only a header changed, since the Makefile tracks no header dependencies. Each task names its exact command.

## Review Focus

1. **A typed number above `INT_MAX` but within `UINT_MAX`** (for example `3000000000` in Tool approval timeout). `GetDlgItemInt` accepts it, and the stored value must stay. Task 1 tests this.
2. **A stored search provider in a different case, or unknown.** `"DuckDuckGo-API"` must select DuckDuckGo (API). `"bing"`, `""` or `NULL` must give index -1, and the dialog must then fall back to row 0 as it does today. Task 1 tests this.
3. **An owner monitor whose work area has a negative origin** (a second monitor left of or above the primary). The window must open inside that work area, not at (0,0). Task 6 tests this.
4. **A disabled combo's rect tool while its row is hidden or the page is scrolled.** The rect must be empty while the row is hidden and follow the control after a scroll, so no tip appears over another page's control. Task 3's wintest pins `ns_tip_move_rect(..., NULL)` as an empty rect. Task 4 calls it from `relayout_page()`, which every page switch and scroll runs through, and the Task 4 reviewer checks that call.
5. **A help id out of range** (`SH_NONE`, `SH_COUNT`, -5). `settings_help_get()` returns NULL, and `page_ctrl(..., SH_NONE)` records no tool. Task 2 tests the first. Task 4's source scan and the harness's exact count of 82 pin the second.
6. **A reader reaching a NULL slot.** Only the live part of the ring (`term_logical_rows()` slots from `lines_start`) holds rows in the running app. Every Task 7 test checks `ring_ok()` after each step, the fuzz target traps on a violation on either ring, and the Task 7 reviewer checks every `lines[...]` dereference in `buffer.c` and `selection.c` (the reflow's trailing-row strip, `sel_row_blank()`, `term_scroll()`'s dirty loop and the region scrolls in particular) against the invariant and the three new NULL guards.
9. **The sparse state seven existing tests force** (`lines_count < rows` set by hand). Growth must reuse a slot that already holds a row, and `term_resize()` and `term_free()` must free every non-NULL slot of the whole array. Those seven tests passing unchanged under `make test SAN=1`, with no leak report, is the check.
7. **An allocation failing halfway through a reflow.** `term_resize()` must leave rows, cols, both rings, the cursors, `epoch` and `lines_recycled` exactly as they were and leak nothing, including on the alternate screen when the primary's reflow is the one that fails. Task 7 tests each failing call position under `make test SAN=1`.
8. **`window.c` acting on a failed resize.** Both `term_resize()` callers must skip `term_mark_all_dirty()`, `dispbuf_resize()` and `io.resize()` when it returns false, so the remote end is never told a size the terminal does not have. Not built natively: the Task 7 reviewer checks the two sites.

---

### Task 1: Search providers and the Save rule in `src/core`

The help test (Task 2) reads `search_provider_*`, so this task comes first.

**Files:**
- Create: `src/core/search_providers.h`, `src/core/search_providers.c`
- Create: `src/core/settings_entry.h`, `src/core/settings_entry.c`
- Create: `tests/test_settings_entry.c`
- Modify: `tests/runner.c` (declarations and calls)

**Interfaces:**
- Produces:
  - `int search_provider_count(void);` returns 4.
  - `const char *search_provider_label(int i);` and `const char *search_provider_value(int i);` return NULL out of range. The rows are, in order: None/`none`, DuckDuckGo (API)/`duckduckgo-api`, DuckDuckGo (HTML)/`duckduckgo-html`, Custom/`custom`.
  - `int search_provider_index(const char *value);` matches ASCII case-insensitively and returns -1 for NULL, `""` or an unknown value. Use a local tolower loop, not `_stricmp`.
  - `int settings_entry_or_keep(int parsed_ok, unsigned v, int lo, int hi, int zero_ok, int current);` returns `(int)v` when `parsed_ok` is true and v is in `lo..hi`, or when v is 0 and `zero_ok` is set. Otherwise it returns `current`. Compare in `unsigned` after checking `lo >= 0`, so no cast can overflow.

- [ ] **Step 1: Write the failing tests** in `tests/test_settings_entry.c`:

```c
int test_search_providers_table(void) {
    TEST_BEGIN();
    ASSERT_EQ(search_provider_count(), 4);
    ASSERT_STR_EQ(search_provider_label(1), "DuckDuckGo (API)");
    ASSERT_STR_EQ(search_provider_value(2), "duckduckgo-html");
    ASSERT_NULL(search_provider_label(4)); ASSERT_NULL(search_provider_value(-1));
    for (int i = 0; i < 4; i++) ASSERT_EQ(search_provider_index(search_provider_value(i)), i);
    TEST_END();
}
int test_search_provider_index_case_and_unknown(void) {   /* Review Focus 2 */
    TEST_BEGIN();
    ASSERT_EQ(search_provider_index("DuckDuckGo-API"), 1);
    ASSERT_EQ(search_provider_index("CUSTOM"), 3);
    ASSERT_EQ(search_provider_index("bing"), -1);
    ASSERT_EQ(search_provider_index(""), -1);
    ASSERT_EQ(search_provider_index(NULL), -1);
    TEST_END();
}
int test_settings_entry_or_keep_range(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_entry_or_keep(1, 1u, 1, 20, 0, 7), 1);
    ASSERT_EQ(settings_entry_or_keep(1, 20u, 1, 20, 0, 7), 20);
    ASSERT_EQ(settings_entry_or_keep(1, 9u, 1, 20, 0, 7), 9);
    ASSERT_EQ(settings_entry_or_keep(1, 0u, 1, 20, 0, 7), 7);
    ASSERT_EQ(settings_entry_or_keep(1, 21u, 1, 20, 0, 7), 7);
    ASSERT_EQ(settings_entry_or_keep(0, 9u, 1, 20, 0, 7), 7);
    TEST_END();
}
int test_settings_entry_or_keep_zero_ok(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_entry_or_keep(1, 0u, 15, 3600, 1, 60), 0);
    ASSERT_EQ(settings_entry_or_keep(1, 1u, 15, 3600, 1, 60), 60);
    ASSERT_EQ(settings_entry_or_keep(1, 3601u, 15, 3600, 1, 60), 60);
    TEST_END();
}
int test_settings_entry_or_keep_huge_unsigned(void) {     /* Review Focus 1 */
    TEST_BEGIN();
    ASSERT_EQ(settings_entry_or_keep(1, 3000000000u, 15, 3600, 1, 60), 60);
    ASSERT_EQ(settings_entry_or_keep(1, 4294967295u, 0, 10080, 1, 5), 5);
    TEST_END();
}
```

Declare all five in `tests/runner.c` under a `/* test_settings_entry.c */` comment and call them next to the `test_settings_layout.c` calls.

- [ ] **Step 2: Run it and see it fail.** Run `make -j test`. Expected: the build fails on the undeclared `search_provider_count` and `settings_entry_or_keep`.
- [ ] **Step 3: Implement both modules** with the signatures above. No Win32 and no `config.h` includes yet; Task 8 adds those.
- [ ] **Step 4: Run it and see it pass.** Run `make -j test`, then `make test SAN=1`. Expected: `Failed: 0`, and the five new tests are listed. Then the Windows compile check: `make -B src/core/search_providers.o src/core/settings_entry.o && rm -f src/core/search_providers.o src/core/settings_entry.o`.
- [ ] **Step 5: Commit**

```bash
git add src/core/search_providers.h src/core/search_providers.c src/core/settings_entry.h src/core/settings_entry.c tests/test_settings_entry.c tests/runner.c
git commit   # "feat(settings): search providers and the keep-the-old-value rule in core" + trailer
```

---

### Task 2: The help table (`src/core/settings_help.{c,h}`) and native tests 1-5

**Files:**
- Create: `src/core/settings_help.h`, `src/core/settings_help.c`
- Create: `tests/test_settings_help.c`
- Modify: `tests/runner.c`

**Interfaces:**
- Consumes: `search_provider_label()` and `search_provider_index()` (Task 1); `settings_nav_at()` and `SETTINGS_PAGE_*` (`settings_layout.h`); `settings_debug_cat_label()` and `settings_debug_level_label()` (`settings_debug.h`); `cmd_policy_stop_label()`, `cmd_policy_default()` and `cmd_policy_auto_on()` (`cmd_policy.h`); `ai_provider_model()` (`ai_prompt.h`); `k_app_font_sizes[]` (`app_font.h`); `config_default_settings()` (`config.h`).
- Produces: exactly the spec's section 2 block. That is the `SettingsHelpId` enum, with the Web Access group ordered `SH_AI_WEB_FETCH, SH_AI_SEARCH, SH_AI_SEARCH_URL, SH_AI_MAX_RESULTS`, plus `SettingsHelpKind`, `SettingsHelp`, `SETTINGS_HELP_TEXT_MAX` (700), `settings_help_get(int id)` and `settings_help_table(size_t *count)`. The flags are `#define SH_F_ZERO_MEANS_NEVER 1u`, `SH_F_THOUSANDS 2u` and `SH_F_KEEP_OUT_OF_RANGE 4u`. The table is built with a macro that writes `#id` into `name`.

**Table values the spec fixes.** Copy `label`, `title`, `text` and `default_text` from "The help texts". Set `title` only on `SH_DBG_RECORDER` (`"Keep recent detail for issue marks"`). Every `text` is the heading (the `title`, or else the `label`), then `"\n\n"`, the body, and for a setting `"\nDefault: " default_text "."`. Use these ranges and flags; every other entry has 0, 0 and no flags:

| id | min, max | flags |
|---|---|---|
| `SH_SCROLLBACK` | 100, 50000 | THOUSANDS |
| `SH_PASTE_DELAY` | 0, 5000 | THOUSANDS |
| `SH_SSH_IDLE` | 0, 10080 | THOUSANDS, ZERO_MEANS_NEVER, KEEP_OUT_OF_RANGE |
| `SH_AI_CONTEXT_LINES` | 1, 50000 | THOUSANDS |
| `SH_AI_TOOL_TIMEOUT` | 15, 3600 | THOUSANDS, ZERO_MEANS_NEVER, KEEP_OUT_OF_RANGE |
| `SH_AI_TOOL_ROUNDS` | 1, 20 | KEEP_OUT_OF_RANGE |
| `SH_AI_MAX_RESULTS` | 1, 20 | KEEP_OUT_OF_RANGE |
| `SH_DBG_MAX_FILE` | 1, 100 | none |
| `SH_DBG_MAX_TOTAL` | 5, 5000 | THOUSANDS |

The `SH_DBG_LEVEL_*` labels are `settings_debug_cat_label(cat)` followed by `":"`, written as literals. `SH_AI_REFRESH` uses `"Refresh models"` and `SH_DBG_USAGE` uses `"Log folder usage"`. Copy the Scrollback text as the spec now gives it (item C wording). It is accurate once Tasks 7 and 8 land.

- [ ] **Step 1: Write the failing tests** in `tests/test_settings_help.c`. Use local helpers `fmt_thousands(int v, char *buf, size_t n)` (which writes `"10,080"`) and `body_of(const SettingsHelp *e)`, which returns the text after the first `"\n\n"`, up to the `"\nDefault: "` if there is one.

  - `test_settings_help_table_matches_enum` (test 1): build a local array with `#define N(x) { x, #x }` for all 46 enumerators, then assert:

```c
size_t n; const SettingsHelp *t = settings_help_table(&n);
ASSERT_EQ(n, (size_t)SH_COUNT); ASSERT_EQ(SH_COUNT, 46);
for (i) { ASSERT_EQ(t[i].id, (SettingsHelpId)i); ASSERT_STR_EQ(t[i].name, k_names[i].name);
          ASSERT_EQ(k_names[i].id, (SettingsHelpId)i); }
```

  - `test_settings_help_get_out_of_range` (Review Focus 5): `ASSERT_NULL(settings_help_get(SH_NONE))`, then the same for `SH_COUNT` and `-5`. Also `settings_help_get(SH_DBG_CURRENT) == &t[SH_DBG_CURRENT]`.
  - `test_settings_help_labels_and_pages` (test 1): every label is non-empty and unique within its page. Each page from `SETTINGS_PAGE_APPEARANCE` to `SETTINGS_PAGE_DEBUG` has at least one entry, and `SETTINGS_PAGE_ABOUT` has none. The entries' pages map, through `settings_nav_index_of_page()`, to nav indices that never decrease.
  - `test_settings_help_text_shape` (test 2): for each entry, the first line equals `title ? title : label` and is followed by `"\n\n"`. Also assert `strlen(text) <= SETTINGS_HELP_TEXT_MAX`, that the body has at least 100 bytes, and that every byte is in `0x20..0x7E` or is `'\n'`. There is no `"  "`, no `" \n"`, and the text ends with `'.'`. A `SH_KIND_SETTING` body has 2 to 6 sentences, counted as `count(". ") + 1`. Actions and info entries have `default_text == NULL` and no `"\nDefault: "`.
  - `test_settings_help_defaults_match_config` (test 3): `Settings s; config_default_settings(&s);`. For every setting, the text ends with `"\nDefault: " default_text "."`, and `default_text` starts with the rendered default, followed by `'\0'` or `" ("`. The rendering is per id:
    - strings as they are, with `"empty"` for `""`: `colour_scheme`, `font`, `ai_font`, `log_dir`, `log_format`, `auto_connect_session`, `ai_provider`, `ai_api_key`, `ai_custom_model`, `ai_custom_url`, `ai_system_notes`, `ai_search_url` and `debug_log_dir`;
    - `fmt_thousands` for the THOUSANDS ids, and `%d` for `font_size`, `ai_tool_auto_rounds`, `ai_max_search_results` and `debug_max_file_mb`;
    - `"on"` or `"off"` for `paste_confirm`, `open_session_manager_at_start`, `auto_connect`, `markdown_render_enabled`, `cmd_policy_auto_on(s.ai_policy_default)`, `ai_web_fetch_enabled`, `debug_enabled`, `debug_screenshot` and `debug_include_content`;
    - `cmd_policy_stop_label(s.ai_policy_default.allowed)` for Allowed, and `search_provider_label(search_provider_index(s.ai_search_provider))` for Search Engine;
    - `settings_debug_level_label(s.debug_levels[cat])` for the six level ids, through a local `k_level_cat[]`;
    - for `SH_DBG_RECORDER`, `"%s, at %s"` from `debug_recorder` and `settings_debug_level_label(debug_recorder_level)`.
  - `test_settings_help_model_names_default` (test 3): `strstr(settings_help_get(SH_AI_MODEL)->text, ai_provider_model("anthropic")) != NULL`.
  - `test_settings_help_ranges_in_text` (test 4): for every entry with `max > 0`, the body contains `"<min> to <max>"`, with both numbers through `fmt_thousands` when `SH_F_THOUSANDS` is set. Pin the constants: `SH_AI_TOOL_TIMEOUT` is `AI_TOOL_TIMEOUT_SEC_MIN..MAX`, `SH_AI_TOOL_ROUNDS` is `AI_TOOL_ROUNDS_MIN..MAX`, `SH_AI_CONTEXT_LINES` max is `AI_CONTEXT_LINES_MAX`, and `SH_DBG_MAX_FILE`/`SH_DBG_MAX_TOTAL` are the `SETTINGS_DEBUG_*_MB_*` values.
  - `test_settings_help_keep_entries` (test 4): exactly `SH_SSH_IDLE`, `SH_AI_TOOL_TIMEOUT`, `SH_AI_TOOL_ROUNDS` and `SH_AI_MAX_RESULTS` carry `SH_F_KEEP_OUT_OF_RANGE`, and each body contains `"not saved"`. For each of these entries, `settings_entry_or_keep()` with the entry's `min`/`max`, and with `zero_ok = (flags & SH_F_ZERO_MEANS_NEVER) != 0`:
    - returns `min`, `max` and `(min+max)/2` unchanged;
    - returns `current` for `max + 1`, for `min - 1` when `min > 0`, and when `parsed_ok` is 0.
  - `test_settings_help_font_sizes_and_notes` (test 4): build `"6, 8, 10, 12, 14, 16, 18 or 20"` from `k_app_font_sizes[]` (`", "` between values and `" or "` before the last) and assert that `SH_FONT_SIZE`'s body contains it. `SH_AI_SYSTEM_NOTES`'s body contains `fmt_thousands((int)AI_NOTES_MAX - 1)` (`"2,559"`).
  - `test_settings_help_labels_match_sources` (test 5): for each k, `SH_DBG_LEVEL_RENDER`..`SH_DBG_LEVEL_GENERAL` (through `k_level_cat[]`) has the label `settings_debug_cat_label(k)` plus `":"`. `SH_AI_POLICY_ALLOWED`'s text contains every `cmd_policy_stop_label(i)` for `i` in `0..POLICY_STOP_COUNT-1`. `SH_AI_SEARCH`'s text contains every `search_provider_label(i)`. Each level text contains every `settings_debug_level_label(l)` for `l` in `0..DLOG_LVL_COUNT-1`.

  Declare and call all nine in `tests/runner.c`.

- [ ] **Step 2: Run it and see it fail.** Run `make -j test`. Expected: the build fails on the missing `settings_help.h`.
- [ ] **Step 3: Implement `settings_help.{c,h}`.** Copy the texts per Global Constraints. The table is one `static const SettingsHelp k_help[SH_COUNT]` in enum order, and `settings_help_table()` sets `*count` when the pointer is not NULL. No Win32.
- [ ] **Step 4: Run it and see it pass.** Run `make -j test` and `make test SAN=1`: 0 failed. Then `make -B src/core/settings_help.o && rm -f src/core/settings_help.o`. When a text assertion fails, fix the transcription against the spec. If the spec text itself breaks a rule, stop and report the line. Never reword it.
- [ ] **Step 5: Commit**

```bash
git add src/core/settings_help.h src/core/settings_help.c tests/test_settings_help.c tests/runner.c
git commit   # "feat(settings): help table for every Settings control" + trailer
```

---

### Task 3: `ns_tip`, the themed tooltip helper, with its wintest

Read the spec first: section 1, "Mechanism", and the wintest paragraph in section 4.

**Files:**
- Create: `src/ui/ns_tip.h`, `src/ui/ns_tip.c`
- Create: `tests/test_ns_tip_win.c`
- Modify: `tests/win_runner.c`, `Makefile` (CRLF)

**Interfaces:**
- Produces (all `#ifdef _WIN32`, ANSI):
  - `HWND ns_tip_create(HWND owner, int dpi, HFONT font, const ThemeTokens *tokens);` creates the tip with `WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP` and `WS_EX_TOPMOST`, without `TTS_BALLOON`. It then calls `ns_control_classic(tip)`. Colours: `TTM_SETTIPBKCOLOR theme_cr(tokens->raised.base)` and `TTM_SETTIPTEXTCOLOR theme_cr(tokens->text_main)`, both skipped when `tokens` is NULL. `WM_SETFONT` is skipped when `font` is NULL. Width: `TTM_SETMAXTIPWIDTH ns_scale(360, dpi)`. Margins: `TTM_SETMARGIN` of `ns_scale(SP_XS, dpi)` on all four sides. Delays: `TTDT_INITIAL` 500, `TTDT_RESHOW` 100, `TTDT_AUTOPOP` 32767. It returns NULL on failure and never deletes the font.
  - `int ns_tip_add(HWND tip, HWND tool, const char *text);` adds a `TTF_IDISHWND | TTF_SUBCLASS` tool, with `hwnd = GetParent(tool)` and `uId = (UINT_PTR)tool`. It returns non-zero on success and changes no window style.
  - `int ns_tip_add_combo(HWND tip, HWND combo, const char *text);` adds the `GetComboBoxInfo` `hwndItem` edit as a tool. It returns 0 when the combo has no item edit.
  - `int ns_tip_add_rect(HWND tip, HWND host, UINT_PTR id, const RECT *rc, const char *text);` adds a `TTF_SUBCLASS` tool with no `TTF_IDISHWND`. A NULL `rc` means an empty rect.
  - `void ns_tip_move_rect(HWND tip, HWND host, UINT_PTR id, const RECT *rc);` sends `TTM_NEWTOOLRECT`. A NULL `rc` means `{0,0,0,0}`.
  - Every `TOOLINFOA` has `cbSize = sizeof(TOOLINFOA)`. Includes: `ns_draw.h`, `ns_scale.h`, `ns_type.h`, `ui_theme.h`, and `themed_button.h` for `theme_cr()`, as `settings.c` already does.

- [ ] **Step 1: Write the failing wintest** in `tests/test_ns_tip_win.c`. The fixture is a top-level `"STATIC"` window, hidden, 400x300. It holds:
  - a static `"Label:"` with `SS_NOTIFY` at (10,10,100,20);
  - an edit;
  - a button;
  - a `CBS_DROPDOWNLIST` combo, disabled with `EnableWindow(FALSE)`;
  - a `CBS_DROPDOWN` combo.

  Short texts `"a"`, `"bb"`, `"ccc"`, `"dddd"`, `"eeeee"`, `"ffffff"` and `"g"`. Tokens come from `ui_theme_resolve(ui_theme_get(0), &tok)` and the font from `ns_font(FONT_BODY, 192)`. Use a helper `get_text(tip, hwnd, uId, buf, 64)` that fills a `TOOLINFOA` with `lpszText = buf` and sends `TTM_GETTEXTA` with `wParam = 64`.
  - `test_ns_tip_win_create_styles`:

```c
LONG st = GetWindowLongA(tip, GWL_STYLE);
ASSERT_TRUE(st & TTS_NOPREFIX); ASSERT_TRUE(st & TTS_ALWAYSTIP); ASSERT_FALSE(st & TTS_BALLOON);
ASSERT_EQ((int)SendMessage(tip, TTM_GETMAXTIPWIDTH, 0, 0), ns_scale(360, 192));
ASSERT_EQ((int)SendMessage(tip, TTM_GETDELAYTIME, TTDT_AUTOPOP, 0), 32767);
```

  - `test_ns_tip_win_notify_static_hits_client`: `SendMessage(lbl, WM_NCHITTEST, 0, MAKELPARAM(screen centre of lbl))` returns `HTCLIENT`.
  - `test_ns_tip_win_add_keeps_styles`: record `GWL_STYLE` and `GWL_EXSTYLE` for all five controls, call `ns_tip_add` on each, and assert all ten values are unchanged.
  - `test_ns_tip_win_count_and_text`: after the five `ns_tip_add` calls, one `ns_tip_add_combo(dropdown)` and one `ns_tip_add_rect(fixture, 7001, &r, "g")`, `TTM_GETTOOLCOUNT` is 7. Each window tool's text reads back as given. The combo's edit tool is found with `hwnd = dropdown`, `uId = edit` and reads `"ffffff"`. `ns_tip_add_combo` on the `CBS_DROPDOWNLIST` combo returns 0.
  - `test_ns_tip_win_rect_tool_moves` (Review Focus 4): add the rect tool `{5,5,50,25}` with `uId` 7001 on the fixture. `ns_tip_move_rect` to `{20,30,120,52}` gives exactly that rect from `TTM_GETTOOLINFOA`. `ns_tip_move_rect(..., NULL)` gives `{0,0,0,0}`.

  In `tests/win_runner.c`, declare the five tests. Add a helper that builds an activation context from `"src\\ui\\nutshell.manifest"` (`ACTCTXA`, `CreateActCtxA` and `ActivateActCtx`). Call the five tests between the activate and `DeactivateActCtx`/`ReleaseActCtx`, after `InitCommonControls()`. If `CreateActCtxA` returns `INVALID_HANDLE_VALUE`, print `[FAIL] ns_tip: no activation context from src\ui\nutshell.manifest`, add 1 to `failed` and `_tf_failed`, and skip the five tests. In `Makefile`, add `tests/test_ns_tip_win.c` to `WIN_ONLY_TESTS`, and add `src/ui/ns_tip.c tests/test_ns_tip_win.c` to `WIN_TEST_SRCS`. Keep CRLF.

- [ ] **Step 2: Run it and see it fail.** Run `xvfb-run -a make wintest`. Expected: the link fails on the undefined `ns_tip_create`.
- [ ] **Step 3: Implement `ns_tip.{c,h}`** as specified in Interfaces.
- [ ] **Step 4: Run it and see it pass.**
  - Run `xvfb-run -a make wintest`: 0 failed, and the five `ns_tip` tests are listed. If Wine says no driver could be loaded, run `pgrep -a wine` first, and use `wineserver -k` only when no other session's run shows.
  - Run `make -j test`: the design-system gate is still green with the new file.
  - Run `make -B src/ui/ns_tip.o && rm -f src/ui/ns_tip.o`: `ns_tip.c` compiles with the release flags, not only the wintest's.
  - Run `git diff --stat --ignore-cr-at-eol Makefile` and compare it with `git diff --stat Makefile`.
- [ ] **Step 5: Commit**

```bash
git add src/ui/ns_tip.h src/ui/ns_tip.c tests/test_ns_tip_win.c tests/win_runner.c Makefile
git commit   # "feat(ui): ns_tip, a tooltip in the theme's colours and font" + trailer
```

---

### Task 4: Wire every Settings control through `page_ctrl()` with a help id

Read the spec first: section 1, "Where it attaches", and section 2, the bullets on labels, helper, search providers and the Save rule.

**Files:**
- Modify: `src/ui/settings.c` (LF)
- Modify: `tests/test_settings_help.c`, `tests/runner.c` (test 6)

**Interfaces:**
- Consumes: Task 1 (`search_provider_*` and `settings_entry_or_keep`), Task 2 (`settings_help_get` and the `SH_*` ids), Task 3 (`ns_tip_*`).
- Produces, internal to `settings.c`:
  - `static HWND page_ctrl(SettingsDlgData *d, const char *cls, const char *text, DWORD style, int ctrl_id, SettingsHelpId help);` creates a `WS_CHILD | WS_VISIBLE | style` child of `d->hPage`. It ORs in `SS_NOTIFY` when `cls` is `"STATIC"` and records `{hwnd, help}` in `d->tips[]` unless `help == SH_NONE`.
  - `static HWND page_ctrl_w(...)` takes the same parameters with wide `cls` and `text`, for the refresh glyph only. Both record through one `static void page_track(SettingsDlgData *d, HWND h, SettingsHelpId help)`.
  - `static HWND make_hint(SettingsDlgData *d, const char *text)` has no id. It is used for the page title, the SSH hint, the context hint, the two Behaviour notes, the URL warning, the Debugging dim statics (keep their `hDbgDim[]` registration) and the About statics.
  - New fields: `typedef struct { HWND hwnd; SettingsHelpId help; } SettingsTip;`, then `SettingsTip tips[96]; int n_tips; HWND hTip;`, which replaces `hTooltip`.

- [ ] **Step 1: Write the failing test** `test_settings_help_every_id_in_settings_c` (test 6) in `tests/test_settings_help.c`:
  - Read `"src/ui/settings.c"` with a local `read_whole()`, copied from `tests/test_shortcuts.c`.
  - When the file is NULL: if `getenv("CI")` is non-empty, print that and `ASSERT_TRUE(0)`; otherwise print a skip line.
  - For every table entry, assert that `e->name` occurs as a whole identifier, meaning the character after the match is not `[A-Za-z0-9_]`. Otherwise `SH_AI_SEARCH` would match inside `SH_AI_SEARCH_URL`.
  - Declare and call it in `tests/runner.c`.
- [ ] **Step 2: Run it and see it fail.** Run `make -j test`. Expected: FAIL, naming `SH_COLOUR_SCHEME` and the other ids that are missing.
- [ ] **Step 3: Rewrite the control creation in `settings.c`:**
  - `make_label2`, `make_edit2`, `make_combo2` and `make_check2` become wrappers that take `(d, ..., SettingsHelpId help)` and call `page_ctrl`. `make_check2` keeps `ns_control_classic`. `make_edit2` keeps `EM_SETMARGINS`.
  - Every label and tickbox text is `settings_help_get(id)->label`.
  - The raw `CreateWindow` sites become `page_ctrl` calls with their ids: the System Instructions edit (`SH_AI_SYSTEM_NOTES`), Browse (`SH_DBG_BROWSE`), Open (`SH_DBG_OPEN`), the usage static (`SH_DBG_USAGE`, still registered dim), and the two Behaviour row label statics (`SH_AI_TOOL_TIMEOUT` and `SH_AI_TOOL_ROUNDS`, with their number boxes carrying the same ids). The refresh button becomes `page_ctrl_w` with `SH_AI_REFRESH`.
  - The Debugging level rows use `static const SettingsHelpId k_dbg_level_help[DLOG_CAT_COUNT] = { SH_DBG_LEVEL_RENDER, SH_DBG_LEVEL_TERM, SH_DBG_LEVEL_AI, SH_DBG_LEVEL_SSH, SH_DBG_LEVEL_SHELL, SH_DBG_LEVEL_GENERAL };`. The recorder tickbox and its level combo both carry `SH_DBG_RECORDER`. `SH_DBG_CURRENT` goes on both its label and its edit.
  - Delete `k_search_providers[]`, `NUM_SEARCH_PROVIDERS`, `k_tooltips[]`, `add_tooltip()` and the old tooltip creation. The search combo fills from `search_provider_label(i)`. The selection is `search_provider_index(cfg value)`, with -1 meaning row 0. The custom check and the Save handler read `search_provider_value(sel)`.
  - In the Save handler, the four inline range tests become `s->x = settings_entry_or_keep(ok, v, h->min, h->max, (h->flags & SH_F_ZERO_MEANS_NEVER) != 0, s->x)`, with `h = settings_help_get(SH_SSH_IDLE / SH_AI_TOOL_TIMEOUT / SH_AI_TOOL_ROUNDS / SH_AI_MAX_RESULTS)`.
  - Create the tip in `WM_CREATE` **after the theme block**: `ThemeTokens tok; ui_theme_resolve(nd->theme, &tok); nd->hTip = ns_tip_create(hwnd, nd->dpi, nd->hDlgFont, &tok);`. Then run one attach loop over `tips[]`: `ns_tip_add` with `settings_help_get(help)->text`, plus `ns_tip_add_combo` for the `SH_AI_MODEL` combo. Add rect tools on `nd->hPage` for `IDC_AUTOCONNECT_COMBO` (`SH_AUTOCONNECT_SESSION`) and `IDC_DBG_RECORDER_LEVEL` (`SH_DBG_RECORDER`), each with a NULL rect.
  - At the end of `relayout_page()`, for each of those two ids:
    - when the control `IsWindowVisible`, take its `GetWindowRect`, `MapWindowPoints(NULL, d->hPage, ...)` it, and pass the result to `ns_tip_move_rect`;
    - otherwise pass NULL;
    - skip the whole step when `d->hTip` is NULL.
  - `WM_DESTROY` destroys `d->hTip`.
  - Label statics keep control id 0. Their `STN_CLICKED` is forwarded to the dialog's `WM_COMMAND` and matches no case. Confirm that no `case 0:` exists.
- [ ] **Step 4: Verify.**
  - Run `make -j test`: test 6 passes and the gate is green.
  - Run `make lint-gate`: exit 0.
  - Run `make -B src/ui/settings.o && rm -f src/ui/settings.o`. Expected: a clean `-Werror` compile.
  - Count the tools by reading the code against the spec's Count table: 79 `tips[]` records, plus the combo edit, plus 2 rect tools, makes 82.
- [ ] **Step 5: Commit**

```bash
git add src/ui/settings.c tests/test_settings_help.c tests/runner.c
git commit   # "feat(settings): hover help on every control and label" + trailer
```

---

### Task 5: Re-entry guard for the Settings dialog

Read the spec first: section 1, "Re-entry guard". `src/ui` is not built natively, so the harness case in Task 9 and checklist item 4 test this task.

**Files:**
- Modify: `src/ui/settings.c`, `src/ui/settings_dlg.h`, `src/ui/window.c` (all LF)

**Interfaces:**
- Produces:
  - `int settings_dlg_show(HWND parent, Config *cfg, const char *config_path, int initial_page);` returns 1 when it ran a dialog, and 0 when it ran none (already open, `cfg` NULL, or creation failed). Update the header comment.
  - `settings_dlg_show_demo_debug` stays `void`.

- [ ] **Step 1: Add the guard.** `settings_dlg_run()` returns `int` and holds `static HWND s_open`. If `s_open && IsWindow(s_open)`, it calls `SetForegroundWindow(s_open)` and returns 0 before allocating anything. It sets `s_open = hwnd` after `CreateWindowEx` succeeds and clears it to NULL after the loop ends.
- [ ] **Step 2: Wire `window.c`.** `on_settings_clicked_page()` returns at once when `settings_dlg_show(...)` is 0, before `dlog_configure` and the theme refresh.
- [ ] **Step 3: Verify.** Run `make -j test`, then `make -B src/ui/settings.o src/ui/window.o && rm -f src/ui/settings.o src/ui/window.o`. Both compile clean.
- [ ] **Step 4: Commit**

```bash
git add src/ui/settings.c src/ui/settings_dlg.h src/ui/window.c
git commit   # "fix(settings): a second Settings request brings the open one forward" + trailer
```

---

### Task 6: Initial size and position, and Web Access order

Read the spec first: "Layout and behaviour changes", items A and B.

**Files:**
- Modify: `src/core/settings_layout.h`, `src/core/settings_layout.c` (LF)
- Modify: `tests/test_settings_layout.c`, `tests/runner.c`
- Modify: `src/ui/settings.c`

**Interfaces:**
- Produces `void settings_initial_placement(const SettingsMetrics *m, int target_rows, int work_x, int work_y, int work_w, int work_h, int nc_w, int nc_h, int *client_w, int *client_h, int *win_x, int *win_y);`. It applies the spec's rule (`target_h`, `avail_h`, `client_h`, `client_w`, with the margin `ns_scale(SP_XL, m->dpi)`). Placement works one axis at a time on the window size (client plus nc):
  - centre it in the work area;
  - clamp it so its far edge is at most the work area's far edge;
  - then clamp it so its near edge is at least the work area's near edge. Top-left wins.

- [ ] **Step 1: Write the failing tests** in `tests/test_settings_layout.c` (test 7). Use `SettingsMetrics m; settings_metrics_init(&m, 96);` unless a case says otherwise.
  Behaviour has 22 rows on `main` at `d73eeec` (the title, plus 1, 4, 6, 1, 1, 1, then 1 + 2 and 1 + 3 for the two number rows and their notes), so 22 is the count every case uses unless it says otherwise. Non-client is 16x39 unless stated.
  - `test_sl_initial_1080p_fits_behaviour`: 22 rows, work area (0,0,1920,1032). Expect `cw == 760`, `ch == 744`, `wx == (1920 - 776) / 2` (572) and `wy == (1032 - 783) / 2` (124).
  - `test_sl_initial_scroll_expectations`: `settings_page_height(22, &m) == 688` and `settings_page_height(21, &m) == 658`; with that `ch`, `settings_scroll_max(ch - m.btnbar_h, settings_page_height(22, &m)) == 0` (Behaviour does not scroll) and `settings_scroll_max(688, settings_page_height(21, &m)) == 0` (Debugging, ticked, fits).
  - `test_sl_initial_follows_row_count`: 17 rows on (0,0,1920,1032) gives `ch == 594`, so the height comes from the count passed in.
  - `test_sl_initial_laptop_height`: 22 rows on (0,0,1366,728) gives `ch == 641` (the cap, 728 - 39 - 48), and `settings_scroll_max(641 - m.btnbar_h, 688) == 103`.
  - `test_sl_initial_tiny_work_area`: 22 rows on (0,0,640,400) gives `cw == m.min_w` (620), `ch == m.min_h`, `m.min_h == 420`, `wx == (640 - (m.min_w + 16)) / 2` (2) and `wy == 0`: the width fits, so x stays centred, and only y is pinned to the top.
  - `test_sl_initial_narrow_clamps_width`: on (0,0,700,1032), `cw == 700 - 16 - 48` (636).
  - `test_sl_initial_144dpi_capped`: at DPI 144, 22 rows on (0,0,1920,1008) with nc 24x57 gives `ch == 879`, and the window lies inside the work area.
  - `test_sl_initial_negative_origin` (Review Focus 3): 22 rows on (-1920,-200,1920,1032). The window satisfies `wx >= -1920`, `wx + cw + 16 <= 0`, `wy >= -200` and `wy + ch + 39 <= 832`.

  Declare and call all eight in `tests/runner.c`.
- [ ] **Step 2: Run it and see it fail.** Run `make -j test`. Expected: the build fails on the undeclared `settings_initial_placement`.
- [ ] **Step 3: Implement it** in `settings_layout.c`.
- [ ] **Step 4: Wire `settings_dlg_run()`.**
  - Create the window **without** `WS_VISIBLE`, at the owner monitor's `rcWork` top-left (`MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST)`, `GetMonitorInfo`), using today's provisional size. This puts the window on the owner's monitor, so `d->dpi` is that monitor's DPI.
  - Then use `d->dpi` and `settings_metrics_init`. Sum `target_rows = 1 + Σ rows` of the `d->ctrls[]` entries on `SETTINGS_PAGE_AI_BEHAVIOUR`, counting the `rows` field whether or not the entry is visible.
  - Get `nc_w`/`nc_h` by measuring the hidden window (`GetWindowRect` minus `GetClientRect`), so the frame is the one for the monitor's DPI; `AdjustWindowRect` on the style is only the fallback.
  - Call `settings_initial_placement`, then `SetWindowPos` the window to the result and `ShowWindow(hwnd, SW_SHOW)`.
  - Remove `CW_USEDEFAULT` and the fixed 760x560.
  - Hold `d` before the message loop: `WM_DESTROY` frees it.
- [ ] **Step 5: Reorder Web Access (item B).** Move the `IDC_AI_WEB_FETCH` block above the `IDC_AI_SEARCH_COMBO` block. Search URL stays directly under Search Engine.
- [ ] **Step 6: Verify.** Run `make -j test` (test 7 passes), then `make -B src/core/settings_layout.o src/ui/settings.o && rm -f src/core/settings_layout.o src/ui/settings.o`, then `make lint-gate`.
- [ ] **Step 7: Commit**

```bash
git add src/core/settings_layout.h src/core/settings_layout.c tests/test_settings_layout.c tests/runner.c src/ui/settings.c
git commit   # "feat(settings): open tall enough for Behaviour; Permit Web Fetch first" + trailer
```

**Execution note:** the spec and these tests use 22 rows, the count on `main` at `d73eeec`. If the Behaviour page has changed when you build, the dialog stays right (it sums the rows it builds), but update `test_sl_initial_1080p_fits_behaviour` and the spec's section A to the new count. Report the measured count and client height in the PR body next to checklist item 9.

---

### Task 7: Lazy scrollback rows in `src/term`

Read the spec first: "Layout and behaviour changes", item C, from "Memory today" to the end of its "Tests" (test 9). This task comes before the wiring (Task 8), so the new 10,000-line default never costs a full buffer at tab open.

**Files:**
- Modify: `src/term/buffer.c` (**CRLF**), `src/term/term.h` (LF)
- Create: `tests/test_term_scrollback.c` (LF)
- Modify: `src/core/selection.c` (LF; `sel_row_blank()` only)
- Modify: `tests/runner.c`, `tests/fuzz/fuzz_term.c` (LF)
- Create: `tests/fuzz/corpus/term/31-ring-wrap`, `32-resize`, `33-alloc-fail` (binary seeds, no line-ending attribute, like the existing ones)
- Modify: `src/ui/window.c` (LF; the two `term_resize()` callers only)

**Interfaces:**
- Produces, in `term.h`:
  - `typedef void *(*TermCallocFn)(size_t nmemb, size_t size);` and `void term_set_calloc(TermCallocFn fn);`. One static pointer in `buffer.c`; NULL restores `calloc`. Tests only: no product code calls it.
  - `bool term_resize(Terminal *term, int rows, int cols);` (was `void`). It returns false only when an allocation failed, and then the `Terminal` is unchanged. A NULL `term`, `rows` or `cols` of 0 or less, and the same size return true, as there is nothing to do.
  - `bool grow_failing;` and `bool resize_failing;` in `Terminal`, next to `full_redraw_needed`; `term_init()` sets both false.
- Internal to `buffer.c`:
  - `static TermRow *term_row_try_alloc(int cols, TermAttr attr);` makes two calls through the allocator, `(1, sizeof(TermRow))` then `((size_t)cols, sizeof(TermCell))`. If the second fails it frees the first. It fills the row as `term_row_fill()` does and returns NULL on failure.
  - `term_row_alloc()` keeps `xmalloc`/`xcalloc` and is used by `term_init()` only.
  - `term_reflow_buffer()` returns `bool`, reads the old ring without freeing it, and writes its out parameters only on success. Its counting pass is unchanged. It then allocates the slot array through the allocator and `max(needed, new_rows)` rows with `term_row_try_alloc()`, where `needed` is pass 0's `total - dropped`. On any failure it frees what it got and returns false.

- [ ] **Step 1: Write the failing tests** in `tests/test_term_scrollback.c`. The helpers:

```c
#include "test_framework.h"
#include "term.h"
#include "selection.h"
#include <stdlib.h>
#include <string.h>

static int g_calls, g_fail_at;   /* g_fail_at: 0 never fails, -1 always fails, n > 0 fails the n-th call */
static void *fake_calloc(size_t n, size_t sz)
{
    g_calls++;
    if (g_fail_at == -1 || g_calls == g_fail_at) return NULL;
    return calloc(n, sz);
}
static void hook(int fail_at) { g_calls = 0; g_fail_at = fail_at; term_set_calloc(fake_calloc); }
static void unhook(void) { term_set_calloc(NULL); }

/* Exactly the live part [start, start + live) mod cap holds rows, where live is
 * term_logical_rows(): lines_count, or rows in a sparse state a test forced. */
static int seg_ok(TermRow *const *lines, int cap, int start, int count)
{
    for (int i = 0; i < cap; i++) {
        int in = ((i - start + cap) % cap) < count;
        if (in != (lines[i] != NULL)) return 0;
    }
    return 1;
}
static int ring_ok(const Terminal *t)
{
    if (!seg_ok(t->lines, t->lines_capacity, t->lines_start, term_logical_rows(t))) return 0;
    if (t->alt_screen_active &&
        !seg_ok(t->primary_lines, t->primary_lines_capacity,
                t->primary_lines_start,
                t->primary_lines_count > t->rows ? t->primary_lines_count : t->rows)) return 0;
    return 1;
}
static int rows_held(const Terminal *t)
{
    int n = 0;
    for (int i = 0; i < t->lines_capacity; i++) n += t->lines[i] != NULL;
    return n;
}
static void feed(Terminal *t, const char *s) { term_process(t, s, strlen(s)); }
static char *all_text(const Terminal *t)
{
    Selection s; size_t n = 0;
    selection_select_all(&s, t);
    return selection_copy_text(&s, t, &n);
}
```

  Two of the tests in full:

```c
int test_term_sb_grows_one_row_per_line(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "\n\n\n");                      /* cursor reaches the bottom row */
    ASSERT_EQ(rows_held(t), 4); ASSERT_TRUE(ring_ok(t));
    for (int k = 1; k <= 10; k++) {
        feed(t, "\n");
        ASSERT_EQ(t->lines_count, 4 + k);
        ASSERT_EQ(rows_held(t), 4 + k);
        ASSERT_TRUE(ring_ok(t));
    }
    term_free(t);
    TEST_END();
}

int test_term_sb_grow_alloc_failure_recycles(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "A\r\nB\r\nC\r\nD");
    hook(-1);
    for (int k = 0; k < 10; k++) { char line[8]; snprintf(line, sizeof line, "\r\nX%d", k); feed(t, line); }
    ASSERT_EQ(t->lines_count, 4); ASSERT_EQ(rows_held(t), 4);
    ASSERT_EQ(t->lines_recycled, (uint64_t)10);
    ASSERT_TRUE(t->grow_failing); ASSERT_TRUE(ring_ok(t));
    char *txt = all_text(t);
    ASSERT_STR_EQ(txt, "X6\r\nX7\r\nX8\r\nX9"); free(txt);   /* selection_copy_text joins with CRLF */
    unhook();
    feed(t, "\r\n");
    ASSERT_EQ(t->lines_count, 5); ASSERT_FALSE(t->grow_failing); ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}
```

  The rest, each ending with `ring_ok(t)` true, `unhook()` and `term_free()`:
  - `test_term_sb_fresh_tab_rows_only`: `term_init(50, 200, 10000)` gives `rows_held == 50` and `lines_capacity == 10050`; with 50000, `rows_held == 50` and `lines_capacity == 50050`. `rows_held` is the behavioural check. The test also asserts `lines_capacity * sizeof(TermRow *) + 50 * (sizeof(TermRow) + 200 * sizeof(TermCell))` is 282000 and 602000, with a comment saying these two asserts document the arithmetic behind the spec's memory figures and test no behaviour.
  - `test_term_sb_full_ring_recycles_without_alloc`: `term_init(4, 10, 6)`, then `L01` to `L30`, each followed by `"\r\n"`. Expect `lines_count == 10`, `rows_held == 10`, `lines_recycled == 21`, and the oldest row (`lines[lines_start]`) starting `"L22"`. Then `hook(0)`, ten more lines, and `g_calls == 0`.
  - `test_term_sb_resize_partly_filled`: `term_init(5, 10, 100)`, twelve lines of 10 characters and one of 25, then `before = all_text(t)`. Resize in turn to 5x20, 5x4, 8x10 and 3x10. After each, `term_resize` returned true, `rows_held == lines_count`, `lines_count <= lines_capacity`, and `all_text(t)` equals `before`. Then the padding edge: `term_init(3, 10, 1)` holding two short lines, resized to 6x10, where the padding slots overlap the spare rows; `ring_ok` holds, and the text is the old text under the four blank rows the padding prepends (growing pads above the content), so the test expects exactly that.
  - `test_term_sb_resize_alt_screen_partly_filled`: as above, then `"\x1b[?1049h"`, resize to 5x20, `"\x1b[?1049l"`. Expect `ring_ok` throughout and the primary's `all_text` equal to `before`.
  - `test_term_sb_grow_cells_failure_no_leak`: a full screen, then `hook(2)` (the cells block of the first new row fails), one line feed: `lines_count` unchanged, `grow_failing` true. `make test SAN=1` reports no leak.
  - `test_term_sb_resize_alloc_failure_unchanged`: a local `make_partly()` builds the partly filled terminal above. Build one, resize it to 5x4 under `hook(0)` to learn `N = g_calls`, and free it. Then for each `f` in 1, 2, N / 2 and N, build a fresh one, record rows, cols, `lines_count`, `lines_start`, `lines_recycled`, `epoch`, the cursor and `all_text`, then `hook(f)` and `term_resize(t, 5, 4)`. Expect false, every recorded value unchanged and `ring_ok`. Also: `resize_failing` is true after the first failure, still true after a second failing resize, and false after the next resize that succeeds.
  - `test_term_sb_resize_alt_alloc_failure_unchanged`: `make_partly()`, then `"\x1b[?1049h"`. Learn `N` from a successful resize of an identical terminal, then fail call `N`, the last one: the active (alternate) ring is reflowed first, so the last call belongs to the primary's reflow. Expect false, both rings, both cursors and `epoch` unchanged, and `alt_screen_active` still true.
  - `test_term_sb_alt_enter_alloc_failure_stays_primary`: `hook(-1)`, `"\x1b[?1049h"`, then `"hello"`: `alt_screen_active` false and `"hello"` on the primary's cursor row. `"\x1b[?1049l"` changes nothing.
  - `test_term_sb_delete_lines_at_top_grows`: `term_init(4, 10, 100)`, `"A\r\nB\r\nC\r\nD"`, then `"\x1b[H\x1b[2M"` (`CSI M` on row 0, the region the whole screen, reaches `term_scroll()` through `term_scroll_up(0, 3, 2)`): `lines_count == 6`, `rows_held == 6`, logical rows 0 and 1 start `'A'` and `'B'`.
  - `test_term_sb_wrapped_live_part_resizes`: `term_init(5, 10, 100)` (capacity 105), `"A\r\nB\r\nC"`, then `term_resize(t, 10, 10)`. The reflow needs 3 rows, pads 7 and leaves `lines_capacity == 110`, `lines_start == 103`, `lines_count == 10`, slots 103..109 and 0..2 non-NULL and slots 3..102 NULL (assert a few explicitly, then `ring_ok`). Feed 20 `"\n"` (the cursor is on the bottom row): `lines_count == 30`, slots 3..22 now non-NULL, 23..102 still NULL. Then `term_resize(t, 5, 4)`: `lines_capacity == 105`, `lines_start == 0`, `lines_count == 30`, `ring_ok`, logical rows 7, 8 and 9 start `'A'`, `'B'` and `'C'`, and `cursor.row == 4`.
  - `test_term_sb_grow_failure_with_history_then_resize`: `term_init(4, 10, 100)`, `"L0\r\nL1\r\n...\r\nL9"` (`lines_count == 10`, `lines_start == 0`). `hook(-1)`, then `"\r\nM0"` to `"\r\nM4"`: `lines_start == 5`, slots 0..4 NULL, `lines_recycled == 5`, logical rows 0..9 read `L5`..`L9`, `M0`..`M4`. `unhook()`, `term_resize(t, 4, 20)` returns true: `lines_start == 0`, `lines_count == 10`, the same ten rows in order, `lines_recycled == 5`, `ring_ok`.
  - `test_term_sb_grow_failure_scrolled_back`: the same `L0`..`L9`, `t->scrollback_offset = 3`; the row at `term_view_to_logical(t, 0)` reads `L3`. `hook(-1)`, `"\r\nM0\r\nM1"`: `scrollback_offset == 5`, and the row at `term_view_to_logical(t, 0)` still reads `L3`.
  - `test_term_sb_grow_failure_selection_matches_full_ring`: terminal `a = term_init(4, 10, 100)` and terminal `b = term_init(4, 10, 6)` (capacity 10), both fed `L0`..`L9`, so `b` is exactly full. `selection_select_all()` on each. `hook(-1)` for the next line on `a` only (`unhook()` before feeding `b`), and `"\r\nM0"` to both. Then `selection_sync()` returns the same on both, `selection_copy_text()` gives the same string, and `selection_contains()` agrees for every screen cell.
  - `test_term_sb_guard_select_all_null_row`: `term_init(4, 10, 100)`, `"A\r\nB"`; save the row at `term_screen_to_phys(t, 3)` and set that slot NULL (a constructed state: unreachable through the API). `selection_select_all()` does not crash and ends at absolute line 1 (`B`). Put the row back before `term_free()`.
  - `test_term_sb_guard_scroll_dirty_null_row`: `"A\r\nB\r\nC\r\nD"`, set screen row 1's slot NULL, then `"\n"` on the bottom row: no crash, `lines_count == 5`. Put the row back in the slot that is NULL.
  - `test_term_sb_guard_region_scroll_null_row`: `"A\r\nB\r\nC\r\nD"`, set screen row 1's slot NULL, then `"\x1b[2;3r\x1b[1S\x1b[1T"` (a region scroll up and down over rows 2 and 3): no crash, exactly one screen slot is NULL afterwards; put the row back there.
  - `test_term_sb_clear_keeps_rows`: a partly filled buffer, `hook(0)`, then `"\x1b[2J"` and `"\x1b[3J"`: `lines_count` and `rows_held` unchanged, `g_calls == 0`.
  - `test_term_sb_limit_100`: `term_init(24, 80, 100)` and 300 lines: `rows_held == 124 == lines_capacity` and `term_max_scroll_offset(t) == 100`.
  - `test_term_sb_limit_50000`: `term_init(24, 2, 50000)`: `rows_held == 24`. Then 50,100 lines of `"x\r\n"` sent in one buffer: `rows_held == 50024 == lines_capacity` and `term_max_scroll_offset(t) == 50000`. `hook(0)` and 100 more lines: `g_calls == 0`.
  - `test_term_sb_free_all_states`: `term_free()` on a fresh, a partly filled, a full and an alternate-screen terminal (with a partly filled primary). Its check is LeakSanitizer under `make test SAN=1`.

  Declare and call all twenty-two in `tests/runner.c` under a `/* test_term_scrollback.c */` comment, next to the `test_reflow_fixes.c` calls.

- [ ] **Step 2: Run it and see it fail.** Run `make -j test`. Expected: the build fails on the undeclared `term_set_calloc` and the missing `grow_failing` field. (The three guard tests would crash rather than fail; that is expected until Step 3.)
- [ ] **Step 3: Implement the design in `buffer.c`** (keep CRLF):
  - The allocator: `static TermCallocFn s_calloc;`, used as `(s_calloc ? s_calloc : calloc)(n, size)` by every fallible allocation, and `term_set_calloc()` setting it. Run `make lint-gate` in this task (Step 6): if cppcheck complains about that conditional call, use a plain wrapper, `static void *term_calloc(size_t n, size_t size) { return s_calloc ? s_calloc(n, size) : calloc(n, size); }`, instead.
  - `term_init()`: zero the slot array with `xcalloc`, allocate rows for slots `0 .. rows - 1` only, and set `grow_failing = false`.
  - `term_scroll()`, the not-full branch: allocate only when `lines[new_idx]` is NULL. If it already holds a row (the sparse state seven existing tests force), reuse it exactly as today and allocate nothing. If it is NULL, call `term_row_try_alloc(term->cols, term->current_attr)`; on success, store it and continue as today, clearing `grow_failing`. On failure, take `lines[lines_start]`, refill it, store it at `new_idx`, set `lines[lines_start] = NULL`, advance `lines_start`, raise `lines_recycled`, stamp the row, and leave `lines_count`. When `grow_failing` was false, set it and write `DLOG(DLOG_TERM, DLOG_WARN, term->log_id, "term.scrollback.alloc_failed", "lines=%d capacity=%d", ...)`. The smart-scrolling code after the branch is unchanged; the dirty-row loop after it skips a NULL slot.
  - `term_scroll_up()` and `term_scroll_down()`: in the save, refill and stamp loops, a NULL row is moved like any other pointer but not refilled, marked or stamped.
  - `term_reflow_buffer()`: as in Interfaces. Pass 1 writes into the pre-allocated rows. The trailing strip reads `!new_lines[i] || new_lines[i]->len == 0`. In the padding step, before today's `term_row_fill()` of each padding slot, move a spare row into that slot if it is NULL, taking spares from the slots in `[new_count, allocated)` that are not themselves padding slots (with a small `max_scrollback` the two ranges overlap; the count still works out, because at least `new_rows` rows were allocated and the segment needs exactly `new_rows`). After the `new_count = new_rows` floor, free every row outside the new segment. On the alternate screen (`max_scrollback` 0) the padding slots are already allocated, so nothing moves and nothing is freed.
  - `term_resize()`: reflow the active ring into locals. On the alternate screen, also reflow the saved primary into locals. If either failed, free the new rows and slot arrays it got, set `resize_failing`, write `DLOG(..., DLOG_WARN, ..., "term.resize.alloc_failed", "rows=%d cols=%d alt=%d", ...)` only when `resize_failing` was false (once per failing streak, not on every `WM_SIZE` of a drag), and return false with nothing else assigned. Otherwise free the old ring or rings by walking every slot of the whole array and freeing each non-NULL one (a test may have left rows outside the live part), assign the locals, clear `resize_failing`, and run today's epoch, scroll-region, offset clamp and log. Return true. No retry timer.
  - `term_free()`: keeps freeing every non-NULL slot of the whole array of both rings, as today.
  - `term_alt_screen_enter()`: allocate the slot array and the `rows` rows through the allocator before saving anything. On failure free them, write `"term.alt_screen.alloc_failed"` at `DLOG_WARN`, and return with every field unchanged.
  - In `src/core/selection.c`, `sel_row_blank()` returns true for a NULL row.
  - Correct the comments that say every slot is allocated up front: `term_cursor_row_text()` in `buffer.c`, and `term_screen_to_phys()` and `term_logical_rows()` in `term.h`. They now say the live part always covers the screen.
- [ ] **Step 4: The fuzz target and its seeds** (spec item C, "The fuzz target"). In `tests/fuzz/fuzz_term.c`:
  - `term_init(4, 10, 6)`: capacity 10, so seven line feeds past the bottom row fill the ring; 4 rows and 10 columns put scroll regions, `CSI M` on row 0, auto-wrap and reflow within a few bytes.
  - Scan the input. `0xFF r c` (with two bytes after it): feed the pending data run to `term_process()`, then `term_resize(t, 1 + r % 8, 1 + c % 16)`. `0xFE k` (with one byte after it): feed the pending run, then arm a counting allocator, installed with `term_set_calloc()`, so that allocation number `1 + k % 4` from now on returns NULL once and the allocator then disarms. Any other byte, and a command byte with too few bytes after it, is data.
  - After every `term_process()` run and every resize, check `seg_ok()` (copied into the target, which links only the product sources) on the active ring with `term_logical_rows(t)` and, while `alt_screen_active`, on the saved primary; `__builtin_trap()` on a mismatch. Call `term_set_calloc(NULL)` before `term_free()` and return.
  - The seeds, hand-written (never crash outputs), created with `printf` into `tests/fuzz/corpus/term/`:
    - `31-ring-wrap`: `L01\r\n` through `L30\r\n`.
    - `32-resize`: `A\r\nB\r\nC`, `\xff\x07\x09` (8 x 10, the padding path), twenty `\n`, `\xff\x04\x03` (5 x 4, the wrapped ring), `\x1b[?1049h`, `\xff\x01\x0f` (2 x 16 on the alternate screen), `\x1b[?1049l`.
    - `33-alloc-fail`: eight `x\r\n`, `\xfe\x00` then `y\r\n` (a failing grow), `\xfe\x01` then `\xff\x07\x0f` (a resize to 8 x 16 whose second allocation fails), `\xfe\x00` then `\x1b[?1049h` and `z` (a failing alternate-screen entry).
  - If clang is installed (`command -v clang`), run `make fuzz && make fuzz-run`: every target reports ok. Otherwise the pull-request `Fuzz` job runs it; say so in the PR body.
- [ ] **Step 5: The two `window.c` callers.** In the grid sync (the `grid.sync` log site) and in `WM_SIZE`, wrap `term_mark_all_dirty()`, `dispbuf_resize()` and `io.resize()` in `if (term_resize(...)) { ... }`. The `InvalidateRect()` and `update_scrollbar()` calls after them stay unconditional.
- [ ] **Step 6: Verify.**
  - Run `make -j test` and `make test SAN=1`: 0 failed, the twenty-two new tests listed, every existing terminal test unchanged and passing, and no LeakSanitizer report.
  - Run `make lint-gate`: exit 0 (see the allocator note in Step 3).
  - The Windows compile check: `make -B src/term/buffer.o src/core/selection.o src/ui/window.o && rm -f src/term/buffer.o src/core/selection.o src/ui/window.o`. Expected: a clean `-Werror` compile of `window.c` with the new `bool` return. No `make release`, no `make clean`.
  - Run `git diff --stat --ignore-cr-at-eol src/term/buffer.c` and compare it with the plain stat.
- [ ] **Step 7: Commit**

```bash
git add src/term/buffer.c src/term/term.h src/core/selection.c tests/test_term_scrollback.c tests/runner.c tests/fuzz/fuzz_term.c tests/fuzz/corpus/term/31-ring-wrap tests/fuzz/corpus/term/32-resize tests/fuzz/corpus/term/33-alloc-fail src/ui/window.c
git commit   # "feat(terminal): allocate scrollback rows as lines are kept" + trailer
```

---

### Task 8: Scrollback Lines takes effect for new tabs

Read the spec first: "Layout and behaviour changes", item C, "The pure lookup". Task 7 must be committed first.

**Files:**
- Modify: `src/core/settings_entry.h`, `src/core/settings_entry.c`
- Modify: `src/config/loader.c` (**CRLF**)
- Modify: `src/ui/window.c`
- Modify: `tests/test_settings_entry.c`, `tests/runner.c`
- Modify: `README.md` (the Terminal section's Scrollback lines entry only)

**Interfaces:**
- Produces:
  - `#define SETTINGS_SCROLLBACK_MIN 100`, `SETTINGS_SCROLLBACK_MAX 50000` and `SETTINGS_SCROLLBACK_DEFAULT 10000`.
  - `int settings_scrollback_for_new_tab(const Settings *s);` returns the default for NULL, and otherwise `scrollback_lines` clamped to `MIN..MAX`. `settings_entry.h` includes `config.h` for `Settings`.

- [ ] **Step 1: Write the failing tests** (test 8):

```c
int test_settings_scrollback_for_new_tab(void) {
    TEST_BEGIN();
    Settings s; config_default_settings(&s);
    ASSERT_EQ(settings_scrollback_for_new_tab(NULL), 10000);
    ASSERT_EQ(settings_scrollback_for_new_tab(&s), 10000);
    int in[]  = { 99, 100, 50000, 50001, -5 };
    int out[] = { 100, 100, 50000, 50000, 100 };
    for (int i = 0; i < 5; i++) { s.scrollback_lines = in[i];
        ASSERT_EQ(settings_scrollback_for_new_tab(&s), out[i]); }
    TEST_END();
}
int test_settings_scrollback_agrees_with_validate(void) {
    TEST_BEGIN();
    int in[] = { 99, 50001 };
    for (int i = 0; i < 2; i++) { Settings s; config_default_settings(&s);
        s.scrollback_lines = in[i]; int want = settings_scrollback_for_new_tab(&s);
        settings_validate(&s); ASSERT_EQ(s.scrollback_lines, want); }
    TEST_END();
}
```

  Declare and call both in `tests/runner.c`.
- [ ] **Step 2: Run it and see it fail.** Run `make -j test`. Expected: the build fails on the undeclared `settings_scrollback_for_new_tab`.
- [ ] **Step 3: Implement it.**
  - Write the helper.
  - In `loader.c`, `config_default_settings()` uses `SETTINGS_SCROLLBACK_DEFAULT`, and `settings_validate()` uses `MIN` and `MAX` instead of 10000, 100 and 50000. Keep CRLF.
  - In `window.c`, `create_session()` calls `term_init(rows, cols, settings_scrollback_for_new_tab(g_config ? &g_config->settings : NULL))`, guarding `g_config` as `window.c`'s other settings reads do. It is the only `term_init` caller outside `src/term`; check that with `grep -rn "term_init(" src`. Include `settings_entry.h`.
  - In README's General > Terminal, the Scrollback lines entry says what the tip says: 100 to 50,000, default 10,000, applies to tabs opened after Save, and memory is used only as lines are kept (about 40 MB for a full 10,000-line buffer on a 200-column tab, 200 MB at 50,000, and briefly about as much again during a resize).
- [ ] **Step 4: Verify.**
  - Run `make -j test` and `make test SAN=1`: 0 failed. Test 3 still finds `"10,000"`.
  - Run `make -B src/ui/window.o src/config/loader.o src/core/settings_entry.o && rm -f src/ui/window.o src/config/loader.o src/core/settings_entry.o`. Expected: a clean `-Werror` compile.
  - Run `git diff --stat --ignore-cr-at-eol src/config/loader.c` and compare it with the plain stat.
- [ ] **Step 5: Commit**

```bash
git add src/core/settings_entry.h src/core/settings_entry.c src/config/loader.c src/ui/window.c tests/test_settings_entry.c tests/runner.c README.md
git commit   # "feat(terminal): new tabs use the Scrollback Lines setting" + trailer
```

---

### Task 9: README corrections and the harness case

**Files:**
- Modify: `README.md` (the Settings section only)
- Modify: `tests/integration/cases/50-window.ps1`, `tests/integration/README.md`

- [ ] **Step 1: README hand corrections** (spec section 2, README bullet):
  - In the group table, remove Debugging from General and add a row `| | Debugging |` above About. Move the Logging section's **Debugging** bullet under a new `#### Debugging` heading, with its link kept.
  - Paste delay gets `(default: 350)`.
  - The AI assist font entry says when it takes effect, with the same wording as the tip (after Undock/Dock or a restart).
  - Reorder the `#### AI Assistant > Web Access` list to Permit web fetch, Search engine, Search URL, Max results, with no wording changes.
- [ ] **Step 2: Write the harness case.** After the `help_shortcuts_sheet_*` cases, add `Invoke-Case -Name "settings_help_tool_count" -Settings @{} -ExtraArgs @("-nc") -Body { ... }`:
  - `$dlg = Open-NutshellSettings -Session $s`.
  - Enumerate the hidden tips. `Get-NutshellWindows` lists visible windows only, so do not use it here. Loop `[NutshellNative]::FindWindowEx([IntPtr]::Zero, $after, "tooltips_class32", [IntPtr]::Zero)` until it returns Zero, and keep each handle whose `GetWindowThreadProcessId` pid is `$s.Process.Id` and whose `[NutshellNative]::GetParent($h)` is `$dlg`.
  - For each kept handle, read the count with `[NutshellNative]::SendMsg($h, 0x040D, [IntPtr]::Zero, [IntPtr]::Zero)` (`TTM_GETTOOLCOUNT`). Assert that the sorted counts are exactly `1, 82`. On failure, say which counts were seen.
  - Post `IDM_EDIT_SETTINGS` again (`[NutshellNative]::PostMessage($s.Main, $script:WM_COMMAND, [IntPtr]2013, [IntPtr]::Zero)`) and sleep 800 ms. Assert that `Get-NutshellWindows` has exactly one `Nutshell_Settings` row and that `[NutshellNative]::IsWindow($dlg)` is true.
  - `Close-NutshellDialog -Dialog $dlg -Button Cancel`. Return a one-line detail string.
  - No module (`NutshellIT.psm1`) change.
- [ ] **Step 3: List the case** in `tests/integration/README.md`'s case table as a `cases\50-window.ps1` row, in the style of the `help_shortcuts_sheet_*` rows.
- [ ] **Step 4: Verify.**
  - Run `make -j test`: README is read by `test_shortcuts.c`, so its block must still match.
  - Run `git diff README.md`: only Settings-section lines have changed.
  - `pwsh` is not installed on `automaton`: the maintainer parses and runs the case with `Run-Integration.ps1 -Tier gate` on the Windows box. Say so in the PR body.
- [ ] **Step 5: Commit**

```bash
git add README.md tests/integration/cases/50-window.ps1 tests/integration/README.md
git commit   # "docs(settings): README corrections; harness case for the help tips" + trailer
```

---

### Task 10: Version bump, build and full verification

**Files:**
- Modify: `src/ui/resource.h`, `README.md` (`**Version**:` line), `build/win/nutshell.exe`

- [ ] **Step 1: Pick the version.** Run `git fetch origin main`. Take the greater of `origin/main`'s and the branch's `APP_VERSION` and add 1 to the patch number. After `.99` comes `x.(y+1).0`. Set `APP_VERSION` and `APP_VERSION_BINARY` in `resource.h` and README's `**Version**:` line to the result.
- [ ] **Step 2: Build and test.** Run each of these in turn and stop at the first failure:
  - `make clean && make release`
  - `make -j test`
  - `make test SAN=1`
  - `make lint-gate`
  - `xvfb-run -a make wintest`
  - `bash .github/scripts/check-version.sh origin/main`

  Expected: all green, a native count of 0 failed with the Task 7 tests listed, and the wintest listing the five `ns_tip` tests.
- [ ] **Step 3: Check line endings.** `git diff --stat origin/main...HEAD` and the same with `--ignore-cr-at-eol` agree for `Makefile`, `src/config/loader.c` and `src/term/buffer.c`, and `git ls-files --eol` shows every touched file with the endings it had on `main`.
- [ ] **Step 4: Commit**

```bash
git add src/ui/resource.h README.md build/win/nutshell.exe
git commit   # "build: vX.Y.Z" + trailer
```

  The PR body carries:
  - the Windows checklist (spec section 4, items 1-12), with Behaviour's measured row count and client height beside item 9 and the Task Manager figures beside item 12;
  - the `-Tier gate` request for the maintainer;
  - the open scope question (the profile editor);
  - the Models line.

---

## Self-review

- **Spec coverage:**
  - Mechanism and tool set: Tasks 3-4. The texts and table: Task 2. Search providers and the Save rule: Task 1.
  - DPI and themes: Task 3 takes `d->dpi` and `d->theme`'s tokens in Task 4.
  - Tests 1-5: Task 2. Test 6: Task 4. Test 7: Task 6. Test 9, the three NULL guards, the fuzz target and its seeds: Task 7. Test 8: Task 8. The wintest: Task 3.
  - The harness case: Task 9. The re-entry guard: Task 5. Layout items A and B: Task 6. Item C: Task 7 (lazy rows, the resize callers) and Task 8 (the setting). README: Tasks 8 and 9. The build: Task 10.
  - Non-goals and follow-ups get no task, by design.
- **Type consistency:**
  - `SettingsHelpId` and the `SH_*` ids are used unchanged in Tasks 2, 4 and 5.
  - `settings_entry_or_keep` has the same signature in Tasks 1, 2 and 4.
  - `ns_tip_*` has the same signatures in Tasks 3 and 4.
  - `settings_initial_placement` takes `target_rows`, matching the revised spec, and Task 6's cases use the spec's 22 rows.
  - `term_resize()` returns `bool` in Task 7, and both `window.c` callers test it. `term_set_calloc()`, `grow_failing` and `resize_failing` are named the same in the spec, Task 7's Interfaces and its tests.
- **Proportion:** the help texts are referenced by section, not copied. Code appears only for tests and the fixed values.
