# Keyboard Shortcuts Cheat Sheet Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Help > Keyboard Shortcuts... opens a modal, themed, custom-painted sheet listing every Nutshell shortcut in seven sections, drawn from one portable table that also supplies every menu accelerator label and README's generated "Keyboard Shortcuts" block.

**Architecture:** A pure table (`src/core/shortcuts.c`) is the single source of truth: the sheet paints it, `create_app_menu()` takes accelerator text from it, and a native test compares README's block with `shortcuts_readme_block()` byte for byte. A pure layout module (`src/core/cheatsheet_layout.c`) decides one or two columns, the scroll viewport, the scrollbar and Close-button rectangles and where the window goes; the Win32 window (`src/ui/cheatsheet_dlg.c`) only measures text, paints and runs the modal loop.

**Tech Stack:** C11, Win32/GDI cross-compiled with `x86_64-w64-mingw32-gcc` (`-std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion`), native tests with plain `gcc` through `make test` (`tests/test_framework.h`, `tests/runner.c`), the design-system modules (`ns_scale`, `ns_type`, `ns_font`, `ns_tokens`, `ns_draw`, `themed_button.h`, `custom_scrollbar.h`), PowerShell 5.1 integration harness (`tests/integration/`), GitHub Actions (`.github/workflows/checks.yml`).

**Spec:** `docs/superpowers/specs/2026-10-20-keyboard-cheat-sheet-design.md` (APPROVED 2026-10-20; section 4 defaults taken: the full list, scrolling when it must; no chord for the sheet; the Mouse section stays). The rows marked † there describe behaviour from `docs/superpowers/specs/2026-10-20-putty-selection-design.md` sections 1 and 6 (Ctrl+C always interrupts; Select All copies; drag copies on release; Confirm Paste proceeds on Enter). **The selection plan (`docs/superpowers/plans/2026-10-20-putty-selection.md`) runs before this one on the same branch `feat/putty-selection`**, so those behaviours exist when this plan runs; the table rows are written as the spec lists them.

## Global Constraints

Copied from the spec; every task's requirements include these.

- **Alternatives share a row; menu rows hold one chord** (spec as amended in 38d8722). "Alternatives that share an action sit in one row, comma separated (`Ctrl+V, Ctrl+Shift+V`); a row that feeds a menu label is one chord. A row that names a menu item shows the menu path in the chord column. A chord that is a US layout name (`Ctrl+=`) carries "US layout" in its condition." Chord grammar: "keys joined by `+`, written as the key is named on a US layout (`Ctrl+=`, `Ctrl+-`, `Shift+PgUp`); alternatives that share an action are listed in one row separated by `, ` (`Ctrl+V, Ctrl+Shift+V`; `PgUp, PgDn, Home, End, Up, Down`), and a row with a non-zero `menu_id` holds exactly one chord; no placeholders other than `key`, `letter` and `Arrows`". Rows are merged only when action and condition are identical; never ` / `.
- **Menu labels from the table.** "the menu builder takes its accelerator strings from it, so a menu label cannot say something the sheet does not". "Every item's accelerator text comes from `shortcuts_menu_accel(id)` instead of a literal after `\t`".
- **README block verbatim.** "the README's table is a generated block the native test compares verbatim"; markers `<!-- shortcuts:begin -->` and `<!-- shortcuts:end -->`; "LF line endings"; the test "tolerates CRLF, and asserts the text between the markers equals `shortcuts_readme_block()` byte for byte (after CRLF folding)."
- **Help-menu only.** "Help-menu only: a chord of its own would be dead while the AI panel has focus and would name a different key on German and French layouts". No chord is added for the sheet; which chords exist does not change ("no Ctrl+Tab, no Ctrl+0, no approve or deny keys").
- **Columns.** "in two columns, 1 to 4 left and 5 to 7 right"; "Two columns, sections 1 to 4 left and 5 to 7 right, when two columns plus margins fit the work-area width; else one column in section order."
- **Scrollbar only when too tall.** "with the themed scrollbar only when the page is still taller than the work area"; "the Close button stays outside the scrolled area."
- **No chip, no ellipsis**; conditions on their own line, `FONT_CAPTION`, `text_dim`; the table is written so an action fits `ns_scale(360, dpi)`.
- **CI.** "When the file is absent the test fails if the `CI` environment variable is set and skips with a printed note otherwise (`checks.yml:106` sets `CI: true`)."
- **Modality** follows the passphrase prompt (`window.c:940-986`), not About: `was_disabled` for the main window and a floating AI panel, focus restored, `SetForegroundWindow(owner)` before the destroy, a `WM_QUIT` seen in the loop re-posted. A second `IDM_HELP_SHORTCUTS` while open is ignored.
- **Design-system gate** (CLAUDE.md "Design system rules", enforced by `tests/test_ui_tokens.c`): no `RGB(` in `src/ui/*.c`, no `MulDiv(..., 96)`, no `CreateFont`; colours from `ns_tokens()` converted with `theme_cr()`; fonts from `ns_font(role, dpi)`; sizes from `ns_type.h` through `ns_scale(px, dpi)`. `WM_DPICHANGED` re-fetches fonts through `ns_font` **without** `ns_font_flush()`.
- **Builds.** Portable logic in `src/core/` (native-tested); `src/ui/*.c` is excluded from native tests (`NON_TEST_SRCS`). Code compiles clean under MinGW and native gcc with `-Werror`. Any `make` that produces a binary needs a version bump first: that happens once, in Task 9. The `-c -o /dev/null` compile checks below produce no binary.
- **Commits.** Stage new files by path and read every `??` line of `git status --short`; commit with the repo's Models line and trailer (CLAUDE.md "Git commits"). Every edit to `.claude/memory/MEMORY.md` gets a dated `.claude/CHANGELOG.md` entry in the same commit. Never push to or merge `main` from here.

## Review Focus

The five input classes the spec implies but does not test, most likely to bite first; each is pinned to a test in the owning task.

1. **README.md arrives with CRLF line endings (no `.gitattributes`; 89 CRLF files) or a hand edit doubles, drops or swaps a marker.** Expected: CRLF matches like LF; a doubled, missing or swapped marker fails with a message naming the markers, never a crash or a silent pass. Pinned: Task 2, `test_shortcuts_readme_check_handles_crlf_and_bad_markers`.
2. **A table field containing `|`, `*`, a backtick, a tab or newline, or a leading or trailing space.** Expected: impossible to commit, because it would split or restyle a README table cell and misalign the sheet. Pinned: Task 1, `test_shortcuts_fields_are_markdown_safe`.
3. **Small or short work areas: a 1366x768 laptop at 125 to 150 percent, a 720p screen, a work area shorter than the button row.** Expected: one column, the scrollbar, the Close button inside the client, nothing negative, the window no wider than the work area. Pinned: Task 3, `test_cheatsheet_layout_scrolls_on_1366x768_at_120` and `test_cheatsheet_layout_tiny_work_area_stays_sane`.
4. **Owner window hanging off an edge, on a second monitor at negative coordinates, or smaller than the sheet.** Expected: the sheet is centred on the owner and then pulled entirely inside that monitor's work area. Pinned: Task 3, `test_cheatsheet_place_centres_then_keeps_to_the_work_area`.
5. **Help > Keyboard Shortcuts issued again while the sheet is open (a posted command, the demo message) and keys posted to the sheet.** Expected: still one sheet; Esc closes it; the main window is enabled again afterwards. Owning code: Task 4's `s_open` guard and `was_disabled` handling; Win32 is not native-testable, so the pin is Task 8's `help_shortcuts_sheet_opens_and_closes`. (The `WM_QUIT` re-post is covered by review only.)

## Spec conflicts and interpretations (read before Task 1)

None of these changes the spec; each is how this plan executes it, and the first needs the maintainer's eye.

1. **Fit at 96 DPI (CONFLICT, still, after the 38d8722 amendment).** Spec section 1 and the amended 2.1 say "about 46 rows ... one page at 96 and 120 DPI on a 1080p screen", and 3.1 expects no scrollbar at 1920 x 1040, 96 DPI. With alternatives joined (only rows whose action and condition are identical) the table is **50 rows** (5, 5, 7, 8 | 10, 4, 11). With the spec's own spacing (`SP_XS` between rows, conditions on their own line, `FONT_HEADING` headings with a rule, `SP_XL` between sections, `SP_LG` padding, the Close row) the taller column is now the left one (sections 1-4, 25 rows, 11 condition lines): a **962 px page plus the 52 px Close row = 1,014 px against 1,001 px available** (1,040 minus the 39 px frame estimate), so by the estimates it still scrolls at 96 DPI, by about 13 px; at 120 DPI it needs about 1,270 px and scrolls. The real Inter metrics may differ by that much either way. The approved default ("the full list, scrolling when it must", 4.1) governs: Task 3 prints the 1080p/96 result instead of asserting "no scrollbar", and pins "scrollbar only when too tall" with a 1920 x 1300 case. To make it one page at 96 DPI, any one of these gains the 13 px: drop `SP_XS` between rows to 0 in section-internal rows (saves 84 px on the left), shorten one condition so it no longer needs its own line in sections 1-4 (16 px each), or cut rows (4.1). Then flip the reported line to `ASSERT_EQ(l.scrolls, 0)`. Two columns stop fitting a 1080p work area at 147 DPI.
2. **`--ui-demo=all` cannot include the sheet.** Spec 2.4 says the `shortcuts` state makes "`--ui-demo=all` and the visual check in CLAUDE.md include it". `all` is the union of AI-panel states and, like `settings-debug`, never opens a modal window (a modal would block the demo). Task 5 adds `shortcuts` as its own state, not part of `all`; Task 6 names it in CLAUDE.md's visual-check line.
3. **Public entry.** Spec 2.3 names `cheatsheet_show(HWND parent)`, but the floating AI panel handle and the active `ThemeColors` are `window.c` statics. Task 4 exposes `cheatsheet_show(HWND parent, HWND floating_panel, const ThemeColors *theme)`; `window.c`'s `show_shortcuts(HWND)` is the one-argument entry the spec describes.
4. `ThemeTokens.bg_primary` is a `ThemeSurface`; the page colour is `ns_tokens()->bg_primary.base`.
5. "every `menu_id` is non-zero at most once per chord" is read as: each non-zero `menu_id` appears on exactly one row, `SC_MENU_ACCEL` is set exactly when `menu_id` is non-zero, and (amendment) such a row's chord has no `, `. The duplicate rule ("no row repeats another's chord and condition within a section") is applied per alternative: no single chord appears twice with the same condition in a section, alone or inside a joined row. The Mouse rows "Double-click" and "Double-click in the AI panel" (and the triple-click pair) share action and condition but are left as separate rows: joining them would make a 42-character chord, widen the chord column for every row and pull the two-column threshold from 147 to 132 DPI; the amendment permits joining, it does not require it.
6. "no rectangle overlaps another or leaves the client area": scrolled content is in page coordinates and necessarily runs past the client when it scrolls, so Task 3 checks page items against the page and the fixed parts (view, scrollbar, Close) against the client.
7. `IDM_FILE_CLOSE_TAB` and `IDM_HELP_SHORTCUTS` go into `resource.h` in Task 1, not with the wiring, because the table refers to them.
8. README ":246-251" is read as the AI panel's "Selecting and Copying" subsection; ":118-157" as "Text Selection and Clipboard", "Keyboard" and "Zoom".
9. The User Guide's SPECIAL KEYS block: its key list becomes the one pointer line; the heading and the Edit > Send Key paragraph stay (that paragraph explains a menu, not a shortcut list).

## File structure

| File | Responsibility | Task |
|---|---|---|
| `src/core/shortcuts.h`, `src/core/shortcuts.c` (new) | The table, section list, menu-accelerator lookup, README block generator | 1 |
| `src/ui/resource.h` | `IDM_FILE_CLOSE_TAB 2008`, `IDM_HELP_SHORTCUTS 2033` | 1 |
| `tests/test_shortcuts.c` (new) | Table rules; README block shape; README verbatim check | 1, 2 |
| `README.md` | Generated block between markers; prose cut to point at it; directory tree | 2, 6 |
| `src/core/cheatsheet_layout.h`, `.c` (new) | Pure layout, scroll clamp, window placement | 3 |
| `tests/test_cheatsheet_layout.c` (new) | Layout tests with fixed per-character estimates | 3 |
| `src/ui/cheatsheet_dlg.h`, `.c` (new) | The window: measure, paint, scroll, modal loop, DPI change | 4 |
| `src/ui/window.c` | Menu items and accelerators from the table, `WM_COMMAND` cases, demo hook | 5 |
| `src/core/ui_demo.c`, `.h`, `tests/test_ui_demo.c` | `shortcuts` demo state | 5 |
| `src/ui/help_guide.c` | Shortcut lists replaced by a pointer; AES text becomes DPAPI | 6 |
| `CLAUDE.md`, `docs/superpowers/specs/2026-09-09-integration-coverage.md`, `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` | Visual-check line; Ctrl+0/Ctrl+Tab notes; notes correction and Open items | 6 |
| `.github/workflows/checks.yml`, `CLAUDE.md`, `.claude/memory/MEMORY.md`, `.claude/CHANGELOG.md` | README.md joins the native-suite path filter | 7 |
| `tests/integration/NutshellIT.psm1`, `tests/integration/cases/50-window.ps1`, `tests/integration/cases/30-ui-demo.ps1`, `tests/integration/README.md` | Dialog class, P/Invokes, menu id lists, three new cases | 8 |
| `src/ui/resource.h`, `README.md`, `build/win/nutshell.exe` | Version bump and build (once, after both plans) | 9 |

`tests/runner.c` gains declarations and calls in Tasks 1, 2, 3 and 5. The Makefile needs no change: `SRCS` is a wildcard over `src/core` and `src/ui`, `NON_TEST_SRCS` already excludes every `src/ui/*.c`, and `TEST_IMPL_SRCS` picks up every `tests/*.c`. Every new `src/core` file includes `resource.h` through `-Isrc/ui`, which both build paths already pass.

---

### Task 1: The shortcuts table

**Files:**
- Create: `src/core/shortcuts.h`, `src/core/shortcuts.c`
- Create: `tests/test_shortcuts.c`
- Modify: `src/ui/resource.h` (the menu-id block, lines 13-31)
- Modify: `tests/runner.c` (declaration after `int test_about_layout_null_out_is_safe(void);`, call after `failed += test_about_layout_null_out_is_safe();`)

**Interfaces:**
- Consumes: `IDM_*` from `src/ui/resource.h`.
- Produces (later tasks rely on these exact names):
  - `typedef struct { const char *section; const char *chord; const char *action; const char *condition; unsigned flags; int menu_id; } Shortcut;`
  - flags `SC_MENU_PATH` (1), `SC_SELECTION_SPEC` (2), `SC_MENU_ACCEL` (4)
  - `const Shortcut *shortcuts_table(size_t *count);` (50 rows, display order, never NULL)
  - `size_t shortcuts_sections(const char **out, size_t cap);` (7)
  - `const char *shortcuts_menu_accel(int menu_id);` ("" when none, never NULL)
  - `size_t shortcuts_readme_block(char *buf, size_t cap);` (snprintf semantics)
  - macros `SHORTCUTS_README_BEGIN` `"<!-- shortcuts:begin -->"`, `SHORTCUTS_README_END` `"<!-- shortcuts:end -->"`
  - `IDM_FILE_CLOSE_TAB 2008`, `IDM_HELP_SHORTCUTS 2033` in `resource.h`

- [ ] **Step 1: Add the two menu ids**

In `src/ui/resource.h`, replace

```c
#define IDM_FILE_SAVE_AI      2007
```

with

```c
#define IDM_FILE_SAVE_AI      2007
#define IDM_FILE_CLOSE_TAB    2008  /* File > Close Tab (Ctrl+Shift+W) */
```

and replace

```c
#define IDM_HELP_LOG_FOLDER   2032  /* Help > Open Debug Log Folder */
```

with

```c
#define IDM_HELP_LOG_FOLDER   2032  /* Help > Open Debug Log Folder */
#define IDM_HELP_SHORTCUTS    2033  /* Help > Keyboard Shortcuts... (the cheat sheet) */
```

Check first that neither number is taken (the selection plan may have added ids): `grep -nE '\b(2008|2033)\b' src/ui/resource.h` must print nothing before the edit.

- [ ] **Step 2: Write the failing tests**

Create `tests/test_shortcuts.c`:

```c
#include "test_framework.h"
#include "shortcuts.h"
#include "resource.h"
#include <stdlib.h>
#include <string.h>

/* The seven sections, in the order the sheet and README show them (spec
 * section 1). */
static const char *const WANT_SECTIONS[] = {
    "Sessions and tabs",
    "Copy, paste and selection",
    "Scrolling and zoom",
    "Keys the terminal sends to the shell",
    "AI Assist panel",
    "Window and help",
    "Mouse"
};
#define WANT_N (sizeof(WANT_SECTIONS) / sizeof(WANT_SECTIONS[0]))

int test_shortcuts_every_row_has_section_chord_action(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    ASSERT_NOT_NULL(t);
    ASSERT_TRUE(n >= 40u);
    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(t[i].section && t[i].section[0]);
        ASSERT_TRUE(t[i].chord && t[i].chord[0]);
        ASSERT_TRUE(t[i].action && t[i].action[0]);
        ASSERT_NOT_NULL(t[i].condition);           /* "" when none, never NULL */
    }
    TEST_END();
}

int test_shortcuts_sections_contiguous_and_in_fixed_order(void)
{
    TEST_BEGIN();
    const char *got[16];
    size_t ns = shortcuts_sections(got, 16u);
    ASSERT_EQ(ns, WANT_N);
    for (size_t i = 0; i < ns && i < WANT_N; i++)
        ASSERT_STR_EQ(got[i], WANT_SECTIONS[i]);

    /* Contiguous: walking the table, the section index never goes back. */
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    size_t at = 0;
    for (size_t i = 0; i < n; i++) {
        while (at < WANT_N && strcmp(t[i].section, WANT_SECTIONS[at]) != 0) at++;
        ASSERT_TRUE(at < WANT_N);                  /* a section out of order or unknown */
    }
    /* A cap smaller than the count still reports the full count. */
    ASSERT_EQ(shortcuts_sections(NULL, 0u), WANT_N);
    TEST_END();
}

/* The i-th ", "-separated alternative of `chord` into out (cap bytes);
 * 0 when there is no i-th one. */
static int chord_alt(const char *chord, int idx, char *out, size_t cap)
{
    const char *p = chord;
    for (int k = 0; k < idx; k++) {
        p = strstr(p, ", ");
        if (!p) return 0;
        p += 2;
    }
    const char *end = strstr(p, ", ");
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= cap) len = cap - 1u;
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

/* No single chord appears twice with the same condition in one section,
 * whether it stands alone or is one of a row's alternatives. */
int test_shortcuts_no_chord_and_condition_repeat_within_a_section(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    char a[64], b[64];
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i; j < n; j++) {
            if (strcmp(t[i].section, t[j].section) != 0) continue;
            if (strcmp(t[i].condition, t[j].condition) != 0) continue;
            for (int x = 0; chord_alt(t[i].chord, x, a, sizeof(a)); x++) {
                for (int y = 0; chord_alt(t[j].chord, y, b, sizeof(b)); y++) {
                    if (i == j && x == y) continue;
                    if (strcmp(a, b) == 0) {
                        printf("  duplicate: [%s] %s \"%s\"\n",
                               t[i].section, a, t[i].condition);
                        ASSERT_TRUE(0);
                    }
                }
            }
        }
    }
    TEST_END();
}

/* Chord grammar: alternatives only as ", " between non-empty chords; never
 * " / "; a menu-label row holds exactly one chord; a menu path is flagged
 * as one. */
int test_shortcuts_chord_grammar(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (size_t i = 0; i < n; i++) {
        const char *c = t[i].chord;
        ASSERT_TRUE(strstr(c, " / ") == NULL);
        ASSERT_TRUE(strstr(c, ",,") == NULL);
        for (const char *p = strchr(c, ','); p; p = strchr(p + 1, ','))
            ASSERT_TRUE(p[1] == ' ' && p[2] != '\0' && p[2] != ' ' && p != c);
        if (t[i].menu_id != 0) ASSERT_TRUE(strchr(c, ',') == NULL);
        int is_path = strstr(c, " > ") != NULL;
        ASSERT_EQ(is_path, (t[i].flags & SC_MENU_PATH) ? 1 : 0);
    }
    TEST_END();
}

/* No field may break a markdown table row: no '|', no newline, no
 * leading or trailing space. */
int test_shortcuts_fields_are_markdown_safe(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (size_t i = 0; i < n; i++) {
        const char *f[4] = { t[i].section, t[i].chord, t[i].action, t[i].condition };
        for (int k = 0; k < 4; k++) {
            size_t len = strlen(f[k]);
            ASSERT_TRUE(strpbrk(f[k], "|\r\n\t*`") == NULL);
            if (len > 0) {
                ASSERT_TRUE(f[k][0] != ' ');
                ASSERT_TRUE(f[k][len - 1] != ' ');
            }
        }
    }
    TEST_END();
}

int test_shortcuts_menu_ids_unique_and_flagged(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    size_t with_id = 0;
    for (size_t i = 0; i < n; i++) {
        ASSERT_EQ(t[i].menu_id != 0 ? 1 : 0, (t[i].flags & SC_MENU_ACCEL) ? 1 : 0);
        if (t[i].menu_id == 0) continue;
        with_id++;
        for (size_t j = i + 1; j < n; j++)
            ASSERT_TRUE(t[j].menu_id != t[i].menu_id);
    }
    ASSERT_EQ(with_id, (size_t)7);
    TEST_END();
}

int test_shortcuts_menu_accel_for_the_seven_menu_ids(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_FILE_NEW_SESSION), "Ctrl+Shift+T");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_FILE_CLOSE_TAB),   "Ctrl+Shift+W");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_EDIT_COPY),        "Ctrl+Shift+C");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_EDIT_PASTE),       "Ctrl+V");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_VIEW_AI_CHAT),     "Ctrl+Shift+Space");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_VIEW_FULLSCREEN),  "F11");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_HELP_MARK_ISSUE),  "Ctrl+Shift+M");
    /* Items with no accelerator, an unknown id and 0 all give "". */
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_FILE_EXIT),        "");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_EDIT_SELECT_ALL),  "");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_HELP_SHORTCUTS),   "");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_SENDKEY_CTRL_V),   "");
    ASSERT_STR_EQ(shortcuts_menu_accel(0),                    "");
    ASSERT_STR_EQ(shortcuts_menu_accel(-1),                   "");
    TEST_END();
}

/* Each accelerator fits MenuItemData.accel (char[32] in window.c). */
int test_shortcuts_menu_accels_fit_the_menu_buffer(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (size_t i = 0; i < n; i++)
        if (t[i].menu_id != 0) ASSERT_TRUE(strlen(t[i].chord) < 32u);
    TEST_END();
}

int test_shortcuts_readme_block_shape(void)
{
    TEST_BEGIN();
    size_t need = shortcuts_readme_block(NULL, 0u);
    ASSERT_TRUE(need > 1000u);
    char *buf = (char *)malloc(need + 1u);
    ASSERT_NOT_NULL(buf);
    if (buf) {
        ASSERT_EQ(shortcuts_readme_block(buf, need + 1u), need);
        ASSERT_EQ(strlen(buf), need);
        static const char HEAD[] =
            "\n\n#### Sessions and tabs\n\n| Shortcut | Action | Note |\n| --- | --- | --- |\n";
        ASSERT_TRUE(strncmp(buf, HEAD, sizeof(HEAD) - 1u) == 0);
        ASSERT_TRUE(strstr(buf, "| **Ctrl+Shift+T** | New session (opens the Session Manager) | from the terminal, not the AI panel |\n") != NULL);
        ASSERT_TRUE(strstr(buf, "| **Alt+F4** | Exit | |\n") != NULL);
        ASSERT_TRUE(strchr(buf, '\r') == NULL);
        ASSERT_TRUE(need >= 2u && buf[need - 1] == '\n' && buf[need - 2] == '\n');
        free(buf);
    }
    TEST_END();
}

/* A short buffer gets a NUL-terminated prefix and the full length back. */
int test_shortcuts_readme_block_truncates_safely(void)
{
    TEST_BEGIN();
    size_t need = shortcuts_readme_block(NULL, 0u);
    char small[16];
    memset(small, 'x', sizeof(small));
    ASSERT_EQ(shortcuts_readme_block(small, sizeof(small)), need);
    ASSERT_EQ(strlen(small), sizeof(small) - 1u);
    char one[1] = { 'x' };
    ASSERT_EQ(shortcuts_readme_block(one, 1u), need);
    ASSERT_EQ(one[0], '\0');
    TEST_END();
}
```

Register them in `tests/runner.c`. After the line `int test_about_layout_null_out_is_safe(void);` add:

```c
/* test_shortcuts.c */
int test_shortcuts_every_row_has_section_chord_action(void);
int test_shortcuts_sections_contiguous_and_in_fixed_order(void);
int test_shortcuts_no_chord_and_condition_repeat_within_a_section(void);
int test_shortcuts_chord_grammar(void);
int test_shortcuts_fields_are_markdown_safe(void);
int test_shortcuts_menu_ids_unique_and_flagged(void);
int test_shortcuts_menu_accel_for_the_seven_menu_ids(void);
int test_shortcuts_menu_accels_fit_the_menu_buffer(void);
int test_shortcuts_readme_block_shape(void);
int test_shortcuts_readme_block_truncates_safely(void);
```

After the line `    failed += test_about_layout_null_out_is_safe();` add:

```c
    failed += test_shortcuts_every_row_has_section_chord_action();
    failed += test_shortcuts_sections_contiguous_and_in_fixed_order();
    failed += test_shortcuts_no_chord_and_condition_repeat_within_a_section();
    failed += test_shortcuts_chord_grammar();
    failed += test_shortcuts_fields_are_markdown_safe();
    failed += test_shortcuts_menu_ids_unique_and_flagged();
    failed += test_shortcuts_menu_accel_for_the_seven_menu_ids();
    failed += test_shortcuts_menu_accels_fit_the_menu_buffer();
    failed += test_shortcuts_readme_block_shape();
    failed += test_shortcuts_readme_block_truncates_safely();
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `make test 2>&1 | grep -E 'shortcuts|error' | head`
Expected: the build fails with `fatal error: shortcuts.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `src/core/shortcuts.h`:

```c
#ifndef NUTSHELL_SHORTCUTS_H
#define NUTSHELL_SHORTCUTS_H

/*
 * shortcuts — the one table of every keyboard and mouse shortcut Nutshell
 * has (spec docs/superpowers/specs/2026-10-20-keyboard-cheat-sheet-design.md
 * section 2.1). Portable, no windows.h: the cheat sheet (src/ui/
 * cheatsheet_dlg.c) draws from it, the menu builder (create_app_menu() in
 * src/ui/window.c) takes every accelerator label from it, and README.md's
 * "Keyboard Shortcuts" block is generated from it and compared verbatim by
 * tests/test_shortcuts.c.
 *
 * A chord is a key combination written as the key is named on a US layout
 * ("Ctrl+=", "Shift+PgUp"). Alternatives that share an action and condition
 * sit in one row, separated by ", " ("Ctrl+Shift+V, Shift+Insert"); a row
 * with a non-zero menu_id holds exactly one chord, because it is that menu
 * item's label. The only placeholders are the literal words "key",
 * "letter" and "Arrows". A row flagged SC_MENU_PATH names a menu item
 * ("Edit > Select All") in the chord column instead.
 */

#include <stddef.h>

enum {
    SC_MENU_PATH      = 1u << 0, /* chord is a menu path, not keys */
    SC_SELECTION_SPEC = 1u << 1, /* true once the PuTTY-selection spec landed */
    SC_MENU_ACCEL     = 1u << 2  /* menu_id's label shows this chord */
};

typedef struct {
    const char *section;    /* "Sessions and tabs", ... */
    const char *chord;      /* "Ctrl+Shift+T", "Esc, Enter", or a menu path "Edit > Select All" */
    const char *action;     /* "New session (opens the Session Manager)" */
    const char *condition;  /* "" or "on the alternate screen (vim, less): sent to the program" */
    unsigned    flags;      /* SC_* */
    int         menu_id;    /* IDM_* whose menu label carries this chord, or 0 */
} Shortcut;

/* The rows in display order; sections are contiguous. Never NULL. */
const Shortcut *shortcuts_table(size_t *count);

/* The distinct section names in display order. Writes at most `cap` of
 * them to `out` (which may be NULL when cap is 0) and returns how many
 * there are. */
size_t shortcuts_sections(const char **out, size_t cap);

/* The chord the menu item `menu_id` shows as its accelerator, or "" when
 * no row names that id. Never NULL. */
const char *shortcuts_menu_accel(int menu_id);

/* The README.md block that sits between SHORTCUTS_README_BEGIN and
 * SHORTCUTS_README_END: one GitHub-flavoured markdown table per section,
 * LF line endings. Writes at most cap-1 bytes plus a NUL (snprintf
 * semantics; buf may be NULL when cap is 0) and returns the full length,
 * not counting the NUL. */
size_t shortcuts_readme_block(char *buf, size_t cap);

#define SHORTCUTS_README_BEGIN "<!-- shortcuts:begin -->"
#define SHORTCUTS_README_END   "<!-- shortcuts:end -->"

#endif /* NUTSHELL_SHORTCUTS_H */
```

- [ ] **Step 5: Write the table**

Create `src/core/shortcuts.c`. Every chord of spec section 1, in the spec's order, 50 rows: alternatives whose action and condition are identical share a row, `, `-separated (`Ctrl+Shift+V, Shift+Insert`; `Ctrl+PgUp, Ctrl+PgDn`; `Ctrl+Arrows, Ctrl+Home, Ctrl+End`; `F10, Shift+F10`; `PgUp, PgDn, Home, End, Up, Down`; `Esc, Enter`); the seven menu-accelerator rows stay single chords and carry `SC_MENU_ACCEL` and their `IDM_*`; † rows carry `SC_SELECTION_SPEC`. Each action is at most 51 characters (Task 3 measures it against `ns_scale(360, dpi)`).

```c
#include "shortcuts.h"
#include "resource.h"   /* IDM_* -- -Isrc/ui is on both build paths */
#include <string.h>

#define S1 "Sessions and tabs"
#define S2 "Copy, paste and selection"
#define S3 "Scrolling and zoom"
#define S4 "Keys the terminal sends to the shell"
#define S5 "AI Assist panel"
#define S6 "Window and help"
#define S7 "Mouse"

#define ALT_SCREEN "on the alternate screen (vim, less): sent to the program"
#define NO_SESSION "with no live session: the menu"

static const Shortcut TABLE[] = {
    /* 1. Sessions and tabs */
    { S1, "Ctrl+Shift+T", "New session (opens the Session Manager)",
      "from the terminal, not the AI panel", SC_MENU_ACCEL, IDM_FILE_NEW_SESSION },
    { S1, "Ctrl+Shift+W", "Close the active tab",
      "from the terminal", SC_MENU_ACCEL, IDM_FILE_CLOSE_TAB },
    { S1, "Alt+F4",       "Exit", "", 0u, 0 },
    { S1, "Alt",          "Open the menu bar", "", 0u, 0 },
    { S1, "Alt+Space",    "Open the system menu", "", 0u, 0 },

    /* 2. Copy, paste and selection */
    { S2, "Ctrl+Shift+C", "Copy the selection", "",
      SC_SELECTION_SPEC | SC_MENU_ACCEL, IDM_EDIT_COPY },
    { S2, "Ctrl+C",       "Interrupt (^C); never copies", "",
      SC_SELECTION_SPEC, 0 },
    { S2, "Ctrl+V",       "Paste",
      "a confirmation first when Settings says so; Enter confirms",
      SC_SELECTION_SPEC | SC_MENU_ACCEL, IDM_EDIT_PASTE },
    { S2, "Ctrl+Shift+V, Shift+Insert", "Paste", "", 0u, 0 },
    { S2, "Edit > Select All", "Select everything held and copy it",
      "Ctrl+A goes to the shell", SC_MENU_PATH | SC_SELECTION_SPEC, 0 },

    /* 3. Scrolling and zoom */
    { S3, "Shift+PgUp", "History up a page", "", 0u, 0 },
    { S3, "Shift+PgDn", "History down a page", "", 0u, 0 },
    { S3, "PgUp",       "History up a page", ALT_SCREEN, 0u, 0 },
    { S3, "PgDn",       "History down a page", ALT_SCREEN, 0u, 0 },
    { S3, "Ctrl+PgUp, Ctrl+PgDn", "Sent to the program, always", "", 0u, 0 },
    { S3, "Ctrl+=",     "Zoom the terminal font in", "US layout", 0u, 0 },
    { S3, "Ctrl+-",     "Zoom the terminal font out", "US layout", 0u, 0 },

    /* 4. Keys the terminal sends to the shell */
    { S4, "Ctrl+Space",      "NUL (0x00)", "", 0u, 0 },
    { S4, "Shift+Tab",       "Back-tab", "", 0u, 0 },
    { S4, "Ctrl+Arrows, Ctrl+Home, Ctrl+End", "Sent with the modifier", "", 0u, 0 },
    { S4, "Alt+key",         "ESC, then the key", NO_SESSION, 0u, 0 },
    { S4, "Ctrl+Alt+letter", "ESC, then the control byte",
      "unless the layout makes it a character", 0u, 0 },
    { S4, "F10, Shift+F10",  "Sent to the shell, not the menu", NO_SESSION, 0u, 0 },
    { S4, "Backspace",       "BS over SSH, DEL in a local shell", "", 0u, 0 },
    { S4, "Edit > Send Key", "Send the next key raw, or with Alt", "",
      SC_MENU_PATH, 0 },

    /* 5. AI Assist panel */
    { S5, "Ctrl+Shift+Space", "Show or hide the panel",
      "from the terminal, not from inside the panel", SC_MENU_ACCEL, IDM_VIEW_AI_CHAT },
    { S5, "Enter",       "Send",
      "while a reply streams or commands run: stop them", 0u, 0 },
    { S5, "Shift+Enter", "New line", "", 0u, 0 },
    { S5, "Ctrl+V",      "Paste text, or attach an image from the clipboard", "", 0u, 0 },
    { S5, "Ctrl+=",      "Zoom the chat in", "US layout", 0u, 0 },
    { S5, "Ctrl+-",      "Zoom the chat out", "US layout", 0u, 0 },
    { S5, "Ctrl+C",      "Copy the selected messages", "conversation list focused", 0u, 0 },
    { S5, "Ctrl+A",      "Select all messages", "list focused", 0u, 0 },
    { S5, "Esc",         "Clear the selection", "list focused", 0u, 0 },
    { S5, "PgUp, PgDn, Home, End, Up, Down", "Scroll the conversation",
      "list focused", 0u, 0 },

    /* 6. Window and help */
    { S6, "F11",          "Fullscreen", "from the terminal",
      SC_MENU_ACCEL, IDM_VIEW_FULLSCREEN },
    { S6, "Ctrl+Shift+M", "Mark an issue in the debug log", "from the terminal",
      SC_MENU_ACCEL, IDM_HELP_MARK_ISSUE },
    { S6, "Esc, Enter",   "Close a dialog", "most dialogs", 0u, 0 },
    { S6, "Help > Keyboard Shortcuts", "This sheet", "", SC_MENU_PATH, 0 },

    /* 7. Mouse */
    { S7, "Drag",               "Select and copy", "", SC_SELECTION_SPEC, 0 },
    { S7, "Double-click",       "Select a word", "", SC_SELECTION_SPEC, 0 },
    { S7, "Triple-click",       "Select a line", "", SC_SELECTION_SPEC, 0 },
    { S7, "Drag past the edge", "Scroll while selecting", "", SC_SELECTION_SPEC, 0 },
    { S7, "Right-click",        "Paste", "", 0u, 0 },
    { S7, "Wheel",              "Scroll three lines", "", 0u, 0 },
    { S7, "Ctrl+Wheel",         "Zoom the terminal font", "", 0u, 0 },
    { S7, "Drag in the AI panel", "Select messages",
      "nothing copied until Ctrl+C", 0u, 0 },
    { S7, "Double-click in the AI panel", "Select a word", "", 0u, 0 },
    { S7, "Triple-click in the AI panel", "Select a line", "", 0u, 0 },
    { S7, "Right-click in the AI panel", "Copy, Select all, Paste into message",
      "", 0u, 0 },
};

#define TABLE_N (sizeof(TABLE) / sizeof(TABLE[0]))

const Shortcut *shortcuts_table(size_t *count)
{
    if (count) *count = TABLE_N;
    return TABLE;
}

size_t shortcuts_sections(const char **out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < TABLE_N; i++) {
        if (i > 0 && strcmp(TABLE[i].section, TABLE[i - 1].section) == 0)
            continue;
        if (out && n < cap) out[n] = TABLE[i].section;
        n++;
    }
    return n;
}

const char *shortcuts_menu_accel(int menu_id)
{
    if (menu_id == 0) return "";
    for (size_t i = 0; i < TABLE_N; i++) {
        if (TABLE[i].menu_id == menu_id) return TABLE[i].chord;
    }
    return "";
}

/* snprintf-style appender: counts every byte, writes what fits. */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} Out;

static void put(Out *o, const char *s)
{
    size_t n = strlen(s);
    if (o->buf && o->cap > 0 && o->len < o->cap - 1) {
        size_t room = o->cap - 1 - o->len;
        size_t k = n < room ? n : room;
        memcpy(o->buf + o->len, s, k);
        o->buf[o->len + k] = '\0';
    }
    o->len += n;
}

size_t shortcuts_readme_block(char *buf, size_t cap)
{
    Out o = { buf, cap, 0 };
    if (buf && cap > 0) buf[0] = '\0';

    put(&o, "\n");
    for (size_t i = 0; i < TABLE_N; i++) {
        const Shortcut *r = &TABLE[i];
        if (i == 0 || strcmp(r->section, TABLE[i - 1].section) != 0) {
            put(&o, "\n#### ");
            put(&o, r->section);
            put(&o, "\n\n| Shortcut | Action | Note |\n| --- | --- | --- |\n");
        }
        put(&o, "| **");
        put(&o, r->chord);
        put(&o, "** | ");
        put(&o, r->action);
        put(&o, " |");
        if (r->condition[0]) {
            put(&o, " ");
            put(&o, r->condition);
            put(&o, " |");
        } else {
            put(&o, " |");
        }
        put(&o, "\n");
    }
    put(&o, "\n");
    return o.len;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `make test 2>&1 | grep -E '^\[(PASS|FAIL)\] test_shortcuts|Tests Run'`
Expected: ten `[PASS] test_shortcuts_...` lines, and `Tests Run: N, Failed: 0`.

Also compile the two new files the way the Windows build will (no binary is produced):

```bash
for f in src/core/shortcuts.c; do
  x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow \
    -Wformat=2 -Wconversion -Os -Isrc -Isrc/core -Isrc/ui -c "$f" -o /dev/null && echo "$f OK"
done
```

Expected: `src/core/shortcuts.c OK`.

- [ ] **Step 7: Commit**

```bash
git add src/core/shortcuts.h src/core/shortcuts.c tests/test_shortcuts.c src/ui/resource.h tests/runner.c
git status --short    # read every ?? line: nothing of this task may be left untracked
git commit
```

Message: `feat(shortcuts): one table of every shortcut, with menu ids and a README generator`, body naming spec section 2.1; commit with the repo's Models line and trailer.

---

### Task 2: README's generated block and its verbatim test

**Files:**
- Modify: `tests/test_shortcuts.c` (append)
- Modify: `tests/runner.c` (after the Task 1 lines)
- Modify: `README.md`: "#### Text Selection and Clipboard", "#### Keyboard" and "### Zoom" (main's lines 118-150), the AI panel's "#### Selecting and Copying" (246-253), "### Keyboard Shortcuts" (349-372)

**Interfaces:**
- Consumes: `shortcuts_readme_block()`, `SHORTCUTS_README_BEGIN`, `SHORTCUTS_README_END` (Task 1).
- Produces: README.md holding exactly one `<!-- shortcuts:begin -->` ... `<!-- shortcuts:end -->` pair whose content is the generated block; `test_shortcuts_readme_matches_table`, `test_shortcuts_readme_check_handles_crlf_and_bad_markers`. Task 7 relies on this test existing (it is why README.md joins the CI path filter).

The block `shortcuts_readme_block()` emits is, exactly (it starts with the newline that ends the begin-marker line and ends with an empty line before the end marker; LF throughout):

```markdown


#### Sessions and tabs

| Shortcut | Action | Note |
| --- | --- | --- |
| **Ctrl+Shift+T** | New session (opens the Session Manager) | from the terminal, not the AI panel |
| **Ctrl+Shift+W** | Close the active tab | from the terminal |
| **Alt+F4** | Exit | |
| **Alt** | Open the menu bar | |
| **Alt+Space** | Open the system menu | |

#### Copy, paste and selection

| Shortcut | Action | Note |
| --- | --- | --- |
| **Ctrl+Shift+C** | Copy the selection | |
| **Ctrl+C** | Interrupt (^C); never copies | |
| **Ctrl+V** | Paste | a confirmation first when Settings says so; Enter confirms |
| **Ctrl+Shift+V, Shift+Insert** | Paste | |
| **Edit > Select All** | Select everything held and copy it | Ctrl+A goes to the shell |

#### Scrolling and zoom

| Shortcut | Action | Note |
| --- | --- | --- |
| **Shift+PgUp** | History up a page | |
| **Shift+PgDn** | History down a page | |
| **PgUp** | History up a page | on the alternate screen (vim, less): sent to the program |
| **PgDn** | History down a page | on the alternate screen (vim, less): sent to the program |
| **Ctrl+PgUp, Ctrl+PgDn** | Sent to the program, always | |
| **Ctrl+=** | Zoom the terminal font in | US layout |
| **Ctrl+-** | Zoom the terminal font out | US layout |

#### Keys the terminal sends to the shell

| Shortcut | Action | Note |
| --- | --- | --- |
| **Ctrl+Space** | NUL (0x00) | |
| **Shift+Tab** | Back-tab | |
| **Ctrl+Arrows, Ctrl+Home, Ctrl+End** | Sent with the modifier | |
| **Alt+key** | ESC, then the key | with no live session: the menu |
| **Ctrl+Alt+letter** | ESC, then the control byte | unless the layout makes it a character |
| **F10, Shift+F10** | Sent to the shell, not the menu | with no live session: the menu |
| **Backspace** | BS over SSH, DEL in a local shell | |
| **Edit > Send Key** | Send the next key raw, or with Alt | |

#### AI Assist panel

| Shortcut | Action | Note |
| --- | --- | --- |
| **Ctrl+Shift+Space** | Show or hide the panel | from the terminal, not from inside the panel |
| **Enter** | Send | while a reply streams or commands run: stop them |
| **Shift+Enter** | New line | |
| **Ctrl+V** | Paste text, or attach an image from the clipboard | |
| **Ctrl+=** | Zoom the chat in | US layout |
| **Ctrl+-** | Zoom the chat out | US layout |
| **Ctrl+C** | Copy the selected messages | conversation list focused |
| **Ctrl+A** | Select all messages | list focused |
| **Esc** | Clear the selection | list focused |
| **PgUp, PgDn, Home, End, Up, Down** | Scroll the conversation | list focused |

#### Window and help

| Shortcut | Action | Note |
| --- | --- | --- |
| **F11** | Fullscreen | from the terminal |
| **Ctrl+Shift+M** | Mark an issue in the debug log | from the terminal |
| **Esc, Enter** | Close a dialog | most dialogs |
| **Help > Keyboard Shortcuts** | This sheet | |

#### Mouse

| Shortcut | Action | Note |
| --- | --- | --- |
| **Drag** | Select and copy | |
| **Double-click** | Select a word | |
| **Triple-click** | Select a line | |
| **Drag past the edge** | Scroll while selecting | |
| **Right-click** | Paste | |
| **Wheel** | Scroll three lines | |
| **Ctrl+Wheel** | Zoom the terminal font | |
| **Drag in the AI panel** | Select messages | nothing copied until Ctrl+C |
| **Double-click in the AI panel** | Select a word | |
| **Triple-click in the AI panel** | Select a line | |
| **Right-click in the AI panel** | Copy, Select all, Paste into message | |
```

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_shortcuts.c`:

```c
/* ---- README.md's generated block (Task 2) -------------------------------- */

/* Copy of `text` with every CRLF folded to LF (the repository has no
 * .gitattributes, so a Windows checkout can hand README.md over as CRLF).
 * Caller frees; NULL on allocation failure. */
static char *fold_crlf(const char *text)
{
    size_t len = strlen(text);
    char *out = (char *)malloc(len + 1u);
    if (!out) return NULL;
    size_t w = 0;
    for (size_t r = 0; r < len; r++) {
        if (text[r] == '\r' && text[r + 1u] == '\n') continue;
        out[w++] = text[r];
    }
    out[w] = '\0';
    return out;
}

static size_t count_occurrences(const char *hay, const char *needle)
{
    size_t n = 0;
    size_t nl = strlen(needle);
    for (const char *p = strstr(hay, needle); p; p = strstr(p + nl, needle)) n++;
    return n;
}

/* 1 when `readme` (LF or CRLF) holds exactly one begin and one end marker,
 * in that order, and the text between them is shortcuts_readme_block()
 * byte for byte. Prints where it differs otherwise. */
static int readme_block_matches(const char *readme)
{
    char *text = fold_crlf(readme);
    if (!text) return 0;
    int ok = 0;
    const char *b = strstr(text, SHORTCUTS_README_BEGIN);
    const char *e = strstr(text, SHORTCUTS_README_END);
    if (count_occurrences(text, SHORTCUTS_README_BEGIN) != 1u ||
        count_occurrences(text, SHORTCUTS_README_END) != 1u || !b || !e || e < b) {
        printf("  README needs exactly one %s followed by one %s\n",
               SHORTCUTS_README_BEGIN, SHORTCUTS_README_END);
    } else {
        b += strlen(SHORTCUTS_README_BEGIN);
        size_t have = (size_t)(e - b);
        size_t need = shortcuts_readme_block(NULL, 0u);
        char *want = (char *)malloc(need + 1u);
        if (want) {
            shortcuts_readme_block(want, need + 1u);
            ok = have == need && memcmp(b, want, need) == 0;
            if (!ok) {
                size_t i = 0;
                while (i < have && i < need && b[i] == want[i]) i++;
                printf("  README block differs from shortcuts_readme_block() at byte %u\n"
                       "  README: \"%.60s\"\n  table:  \"%.60s\"\n"
                       "  regenerate it: see the recipe above"
                       " test_shortcuts_readme_matches_table\n",
                       (unsigned)i, b + i, want + i);
            }
            free(want);
        }
    }
    free(text);
    return ok;
}

/* Whole file, as bytes; NULL when it cannot be read. */
static char *read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1u, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* README.md's "Keyboard Shortcuts" block is generated from the table and
 * must match it byte for byte. make test runs from the repository root;
 * run from anywhere else README.md is not there and the check skips --
 * except under CI (checks.yml sets CI: true), where a missing README means
 * the job is misconfigured, so it fails.
 *
 * To regenerate the block after changing src/core/shortcuts.c, from the
 * repository root:
 *
 *   gen=$(mktemp -d)
 *   printf '%s\n' '#include <stdio.h>' '#include <stdlib.h>' \
 *     '#include "shortcuts.h"' 'int main(void) {' \
 *     '  size_t n = shortcuts_readme_block(NULL, 0u);' \
 *     '  char *b = malloc(n + 1u); if (!b) return 1;' \
 *     '  shortcuts_readme_block(b, n + 1u); fputs(b, stdout); free(b); return 0; }' \
 *     > "$gen/gen.c"
 *   gcc -std=c11 -Isrc/core -Isrc/ui "$gen/gen.c" src/core/shortcuts.c -o "$gen/gen"
 *   "$gen/gen" > "$gen/block.md"
 *   python3 -c 'import sys; b, e = "<!-- shortcuts:begin -->", "<!-- shortcuts:end -->"
 *   blk = open(sys.argv[1], newline="").read(); s = open("README.md", newline="").read()
 *   nl = "\r\n" if "\r\n" in s else "\n"; i = s.index(b) + len(b); j = s.index(e)
 *   open("README.md", "w", newline="").write(s[:i] + blk.replace("\n", nl) + s[j:])' "$gen/block.md"
 */
int test_shortcuts_readme_matches_table(void)
{
    TEST_BEGIN();
    char *readme = read_whole("README.md");
    if (!readme) {
        const char *ci = getenv("CI");
        if (ci && ci[0]) {
            printf("  README.md not readable from the working directory under CI\n");
            ASSERT_TRUE(0);
        } else {
            printf("  [shortcuts] README.md not readable from cwd -- skipping\n");
        }
        TEST_END();
    }
    ASSERT_TRUE(readme_block_matches(readme));
    free(readme);
    TEST_END();
}

/* The comparison itself, on synthetic READMEs: LF and CRLF both match; a
 * one-byte change, a doubled or missing marker, or the markers swapped
 * do not. */
int test_shortcuts_readme_check_handles_crlf_and_bad_markers(void)
{
    TEST_BEGIN();
    size_t need = shortcuts_readme_block(NULL, 0u);
    size_t cap = 2u * (need + 256u);
    char *block = (char *)malloc(need + 1u);
    char *doc = (char *)malloc(cap);
    char *crlf = (char *)malloc(cap);
    ASSERT_TRUE(block && doc && crlf);
    if (block && doc && crlf) {
        shortcuts_readme_block(block, need + 1u);

        snprintf(doc, cap, "intro\n%s%s%s\nafter\n",
                 SHORTCUTS_README_BEGIN, block, SHORTCUTS_README_END);
        ASSERT_TRUE(readme_block_matches(doc));

        size_t w = 0;
        for (size_t r = 0; doc[r] && w + 2u < cap; r++) {
            if (doc[r] == '\n') crlf[w++] = '\r';
            crlf[w++] = doc[r];
        }
        crlf[w] = '\0';
        ASSERT_TRUE(readme_block_matches(crlf));

        char *hit = strstr(doc, "Ctrl+Shift+T");          /* one byte off */
        if (hit) { hit[0] = 'c'; ASSERT_FALSE(readme_block_matches(doc)); hit[0] = 'C'; }

        snprintf(doc, cap, "%s\n%s%s%s\n", SHORTCUTS_README_BEGIN,
                 SHORTCUTS_README_BEGIN, block, SHORTCUTS_README_END);
        ASSERT_FALSE(readme_block_matches(doc));          /* begin twice */

        snprintf(doc, cap, "%s%s\n", SHORTCUTS_README_BEGIN, block);
        ASSERT_FALSE(readme_block_matches(doc));          /* no end */

        snprintf(doc, cap, "%s%s%s\n", SHORTCUTS_README_END, block,
                 SHORTCUTS_README_BEGIN);
        ASSERT_FALSE(readme_block_matches(doc));          /* swapped */

        ASSERT_FALSE(readme_block_matches(""));
    }
    free(block);
    free(doc);
    free(crlf);
    TEST_END();
}
```

In `tests/runner.c`, after `int test_shortcuts_readme_block_truncates_safely(void);` add

```c
int test_shortcuts_readme_matches_table(void);
int test_shortcuts_readme_check_handles_crlf_and_bad_markers(void);
```

and after `    failed += test_shortcuts_readme_block_truncates_safely();` add

```c
    failed += test_shortcuts_readme_matches_table();
    failed += test_shortcuts_readme_check_handles_crlf_and_bad_markers();
```

- [ ] **Step 2: Run the tests to verify the README check fails**

Run: `make test 2>&1 | grep -E -A2 'test_shortcuts_readme'`
Expected: `test_shortcuts_readme_matches_table` prints `README needs exactly one <!-- shortcuts:begin --> followed by one <!-- shortcuts:end -->` and `[FAIL]`; `test_shortcuts_readme_check_handles_crlf_and_bad_markers` prints its expected diagnostics and `[PASS]`.

- [ ] **Step 3: Replace README's shortcut table with the markers**

In `README.md`, replace everything from the line `### Keyboard Shortcuts` up to, not including, the blank line before `---` that precedes `## Local shell` (main's lines 349-372: the two-column table and the paragraph starting "See [Keyboard](#keyboard) above") with:

```markdown
### Keyboard Shortcuts

The same list is in the app under **Help > Keyboard Shortcuts**. The tables
below are generated from `src/core/shortcuts.c`, and `make test` fails if the
two disagree: change the table, then regenerate this block (the recipe is in
`tests/test_shortcuts.c`).

<!-- shortcuts:begin -->
<!-- shortcuts:end -->
```

Run: `make test 2>&1 | grep -E -A2 'test_shortcuts_readme_matches_table'`
Expected: `README block differs from shortcuts_readme_block() at byte 1` and `[FAIL]`.

- [ ] **Step 4: Generate the block into README**

From the repository root (the same recipe the test file's comment carries):

```bash
gen=$(mktemp -d)
printf '%s\n' '#include <stdio.h>' '#include <stdlib.h>' \
  '#include "shortcuts.h"' 'int main(void) {' \
  '  size_t n = shortcuts_readme_block(NULL, 0u);' \
  '  char *b = malloc(n + 1u); if (!b) return 1;' \
  '  shortcuts_readme_block(b, n + 1u); fputs(b, stdout); free(b); return 0; }' \
  > "$gen/gen.c"
gcc -std=c11 -Isrc/core -Isrc/ui "$gen/gen.c" src/core/shortcuts.c -o "$gen/gen"
"$gen/gen" > "$gen/block.md"
python3 -c 'import sys; b, e = "<!-- shortcuts:begin -->", "<!-- shortcuts:end -->"
blk = open(sys.argv[1], newline="").read(); s = open("README.md", newline="").read()
nl = "\r\n" if "\r\n" in s else "\n"; i = s.index(b) + len(b); j = s.index(e)
open("README.md", "w", newline="").write(s[:i] + blk.replace("\n", nl) + s[j:])' "$gen/block.md"
git diff --stat README.md
```

Expected: README gains the seven `####` tables shown above, between the markers.

- [ ] **Step 5: Cut the prose that repeated the table**

Each edit below removes chord lists that the generated block now owns, keeps behaviour that is not a chord, and points at the block. The selection plan ran first and may have rewritten "Text Selection and Clipboard": apply the stated rule to whatever is there; the replacement shown is what the rule gives on main's text.

(a) `#### Text Selection and Clipboard`. Rule: delete every bullet that starts with a chord in bold (`- **Ctrl+`, `- **Shift+`), keep every other bullet, and end the subsection with the pointer line. On main's text the subsection becomes:

```markdown
#### Text Selection and Clipboard

- **Click and drag** on the terminal to select text
- **Confirm before pasting** (Settings > Terminal, on by default) shows a preview dialog before every paste, so you can review what's about to be sent
- **Paste delay**: an optional inter-line delay (0 to 5000 ms) can be set in Settings for servers that need time between lines

Every copy, paste and selection chord is in [Keyboard Shortcuts](#keyboard-shortcuts).
```

After the edit, `grep -n 'Ctrl+C\*\* copies' README.md` prints nothing.

(b) Replace everything from `#### Keyboard` up to, not including, `### Zoom` with:

```markdown
#### Keyboard

Most keys reach the shell the way xterm sends them, so remote-side editing and program shortcuts work as expected: Alt+key arrives as Esc then the key (xterm's `metaSendsEscape`), and the arrows, Home, End, Insert, Delete, Page Up/Down and the F-keys carry their modifiers in xterm's form. Nutshell keeps only a few chords for itself, all with Ctrl+Shift or F11, so plain Ctrl+T, Ctrl+W and Ctrl+Space reach the shell; with **AltGr** held none of the Ctrl shortcuts fire. **Edit > Send Key** sends a key Nutshell or Windows would otherwise keep, and has two one-shot items for the next keystroke: **Send Next Key Raw** and **Send Next Key with Alt**. When the AI panel's input box has focus, keys go to the panel; click the terminal to return focus. Every chord is listed in [Keyboard Shortcuts](#keyboard-shortcuts), and in the app under **Help > Keyboard Shortcuts**.

```

(c) Replace everything from `### Zoom` up to, not including, `### Settings` with:

```markdown
### Zoom

Zoom the terminal font in discrete steps (6, 8, 10, 12, 14, 16, 18, 20 pt) with the zoom chords or Ctrl+mouse wheel listed in [Keyboard Shortcuts](#keyboard-shortcuts). Zoom changes trigger an automatic PTY resize so the remote shell adapts to the new column/row count.

```

(d) Replace everything from `#### Selecting and Copying` up to, not including, `#### Command Execution` with:

```markdown
#### Selecting and Copying

Drag anywhere in the thread to select characters -- your messages, replies, the open Thinking text, commands, status lines -- across messages if you like; dragging past the top or bottom edge scrolls. Double-click selects a word (in a code block, the command token under the pointer) and triple-click the whole line. Nothing is copied just by letting go of the mouse: copying puts the selection on the clipboard as it reads on screen -- no markdown markers, list items with their bullet or number when the selection includes it, table cells separated by tabs, commands without their `[EXEC]` markers. The copy icons on code blocks and command rows still copy their raw text in one click. The panel's chords are in [Keyboard Shortcuts](#keyboard-shortcuts).

```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `make test 2>&1 | grep -E '^\[(PASS|FAIL)\] test_shortcuts|Tests Run'`
Expected: twelve `[PASS] test_shortcuts_...` lines, `Failed: 0`.

Run: `grep -c 'shortcuts:begin' README.md; grep -c 'shortcuts:end' README.md`
Expected: `1` and `1`.

Then check what the suite does from another directory, and under CI (`$OLDPWD` is the repository root inside each subshell):

```bash
(cd "$(mktemp -d)" && "$OLDPWD/build/test_runner" | grep -A1 'readme_matches_table' | head -2)
(cd "$(mktemp -d)" && CI=true "$OLDPWD/build/test_runner" | grep -A1 'readme_matches_table' | head -2)
```

Expected: the first prints `[shortcuts] README.md not readable from cwd -- skipping`; the second prints `README.md not readable from the working directory under CI` (and that test fails, which is the point).

- [ ] **Step 7: Commit**

```bash
git add tests/test_shortcuts.c tests/runner.c README.md
git status --short
git commit
```

Message: `docs(readme): generate the shortcuts block from the table and test it verbatim`; commit with the repo's Models line and trailer.

---

### Task 3: The pure layout

**Files:**
- Create: `src/core/cheatsheet_layout.h`, `src/core/cheatsheet_layout.c`
- Create: `tests/test_cheatsheet_layout.c`
- Modify: `tests/runner.c` (after the Task 2 lines)

**Interfaces:**
- Consumes: `shortcuts_table()`, `shortcuts_sections()` (Task 1; the tests measure the real table); `ns_scale()` (`src/core/ns_scale.h`); `SP_*`, `SZ_*`, `STROKE_HAIRLINE` (`src/core/ns_type.h`).
- Produces (Task 4 relies on these exact names):
  - `typedef struct { int x, y, w, h; } CsRect;`
  - `enum { CS_MAX_SECTIONS = 8, CS_MAX_ROWS = 96, CS_LEFT_SECTIONS = 4 };`
  - `CsInput` { `dpi`, `work_w`, `work_h`, `nc_w`, `nc_h`, `n_sections`, `section_rows[8]`, `has_cond[96]` (unsigned char), `chord_w`, `text_w`, `heading_w`, `body_h`, `caption_h`, `heading_h`, `scrollbar_w` }
  - `CsLayout` { `columns`, `col_w`, `client_w`, `client_h`, `page_h`, `scrolls`, `view`, `scrollbar`, `button`, `n_sections`, `n_rows`, `heading_col[]`, `heading[]`, `rule[]`, `row_col[]`, `row[]`, `chord[]`, `action[]`, `cond[]` }
  - `void cheatsheet_layout_compute(CsLayout *out, const CsInput *in);`
  - `int cheatsheet_scroll_clamp(int offset, int page_h, int view_h);`
  - `void cheatsheet_place(int owner_x, int owner_y, int owner_w, int owner_h, int win_w, int win_h, int work_x, int work_y, int work_w, int work_h, int *out_x, int *out_y);`

Geometry, all through `ns_scale(px, dpi)` except the 1 px rule (`STROKE_HAIRLINE`, which ns_type.h keeps unscaled): `SP_LG` page padding and chord-to-text gutter; `SP_XL` between sections and between the two columns; `SP_XS` between rows and from a heading to its rule; `SP_SM` from the rule to the first row and from the view to the Close button; Close is `SZ_BTN_MIN_W` x `SZ_CTRL_H`, centred, with `SP_LG` below it. Column width = max(chord + gutter + text, heading). Two columns when `2 * pad + 2 * col_w + col_gap + scrollbar_w` fits `work_w - nc_w` (the scrollbar's width is always held back, so the column choice never depends on scrolling). Scrolls when `page_h + button row` exceeds `work_h - nc_h`; then the view takes the rest and the scrollbar sits at its right edge, outside the view.

The native estimates (fixed, stated in the test): 7 px a character in `FONT_BODY`, 6 in `FONT_CAPTION`, 10 in `FONT_HEADING`, at 96 DPI; line heights 18 / 16 / 24 (the ramp's `px x line_height` at 96 DPI); frame 16 x 39; scrollbar `CSB_WIDTH` 14. With them the real 50-row table needs a 962 px page plus the 52 px Close row at 96 DPI, 13 px more than a 1920 x 1040 work area leaves, so it scrolls there (see "Spec conflicts" 1); two columns stop fitting a 1080p work area at 147 DPI.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_cheatsheet_layout.c`:

```c
#include "test_framework.h"
#include "cheatsheet_layout.h"
#include "shortcuts.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

/* Natively there are no fonts to measure, so text widths are FIXED
 * PER-CHARACTER ESTIMATES of the Inter UI face at 96 DPI, scaled with
 * ns_scale(): 7 px a character in FONT_BODY (13 px), 6 in FONT_CAPTION
 * (12 px), 10 in FONT_HEADING (19 px semibold). Line heights are the type
 * ramp's at 96 DPI (ns_type.c: px x line_height, rounded): body 18,
 * caption 16, heading 24. The frame estimate is a WS_CAPTION popup with
 * WS_EX_DLGMODALFRAME: 16 px wide, 39 px tall at 96 DPI. The scrollbar
 * is CSB_WIDTH (14) scaled. The real window measures instead. */
#define EST_BODY_CHAR_W_96    7
#define EST_CAPTION_CHAR_W_96 6
#define EST_HEADING_CHAR_W_96 10
#define EST_BODY_LINE_96      18
#define EST_CAPTION_LINE_96   16
#define EST_HEADING_LINE_96   24
#define EST_NC_W_96           16
#define EST_NC_H_96           39
#define EST_SCROLLBAR_W_96    14

static int est_w(const char *s, int char_w_96, int dpi)
{
    return ns_scale((int)strlen(s) * char_w_96, dpi);
}

/* The real table, measured with the estimates, in a work_w x work_h area. */
static void fill_input(CsInput *in, int dpi, int work_w, int work_h)
{
    memset(in, 0, sizeof(*in));
    in->dpi = dpi;
    in->work_w = work_w;
    in->work_h = work_h;
    in->nc_w = ns_scale(EST_NC_W_96, dpi);
    in->nc_h = ns_scale(EST_NC_H_96, dpi);
    in->body_h = ns_scale(EST_BODY_LINE_96, dpi);
    in->caption_h = ns_scale(EST_CAPTION_LINE_96, dpi);
    in->heading_h = ns_scale(EST_HEADING_LINE_96, dpi);
    in->scrollbar_w = ns_scale(EST_SCROLLBAR_W_96, dpi);

    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    int s = -1;
    for (size_t i = 0; i < n && i < CS_MAX_ROWS; i++) {
        if (i == 0 || strcmp(t[i].section, t[i - 1].section) != 0) {
            s++;
            int hw = est_w(t[i].section, EST_HEADING_CHAR_W_96, dpi);
            if (hw > in->heading_w) in->heading_w = hw;
        }
        if (s >= CS_MAX_SECTIONS) break;
        in->section_rows[s]++;
        in->has_cond[i] = t[i].condition[0] ? 1 : 0;
        int cw = est_w(t[i].chord, EST_BODY_CHAR_W_96, dpi);
        int aw = est_w(t[i].action, EST_BODY_CHAR_W_96, dpi);
        int dw = est_w(t[i].condition, EST_CAPTION_CHAR_W_96, dpi);
        if (cw > in->chord_w) in->chord_w = cw;
        if (aw > in->text_w) in->text_w = aw;
        if (dw > in->text_w) in->text_w = dw;
    }
    in->n_sections = s + 1;
}

static int inside(CsRect r, int w, int h)
{
    return r.x >= 0 && r.y >= 0 && r.w >= 0 && r.h >= 0 &&
           r.x + r.w <= w && r.y + r.h <= h;
}

static int contains(CsRect outer, CsRect r)
{
    return r.x >= outer.x && r.y >= outer.y &&
           r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}

static int overlap(CsRect a, CsRect b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return 0;
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

/* Every check that holds for any layout: page items inside the page and
 * apart from each other, row parts inside their row, the fixed parts
 * inside the client area and apart. Returns the number of failures. */
static int check_geometry(const CsLayout *l)
{
    int bad = 0;
    int page_w = 0;
    for (int s = 0; s < l->n_sections; s++) {
        int right = l->heading[s].x + l->heading[s].w;
        if (right > page_w) page_w = right;
    }
    page_w += 1; /* exclusive bound */
    CsRect items[2 * CS_MAX_SECTIONS + CS_MAX_ROWS];
    int ni = 0;
    for (int s = 0; s < l->n_sections; s++) {
        items[ni++] = l->heading[s];
        items[ni++] = l->rule[s];
    }
    for (int i = 0; i < l->n_rows; i++) {
        items[ni++] = l->row[i];
        if (!contains(l->row[i], l->chord[i]))  bad++;
        if (!contains(l->row[i], l->action[i])) bad++;
        if (!contains(l->row[i], l->cond[i]))   bad++;
        if (overlap(l->chord[i], l->action[i])) bad++;
        if (overlap(l->action[i], l->cond[i]))  bad++;
    }
    for (int a = 0; a < ni; a++) {
        if (!inside(items[a], page_w, l->page_h)) bad++;
        for (int b = a + 1; b < ni; b++)
            if (overlap(items[a], items[b])) bad++;
    }
    if (!inside(l->view, l->client_w, l->client_h))   bad++;
    if (!inside(l->button, l->client_w, l->client_h)) bad++;
    if (overlap(l->view, l->button)) bad++;
    if (l->scrolls) {
        if (!inside(l->scrollbar, l->client_w, l->client_h)) bad++;
        if (overlap(l->scrollbar, l->view))   bad++;
        if (overlap(l->scrollbar, l->button)) bad++;
        if (l->view.h >= l->page_h) bad++;
    } else {
        if (l->scrollbar.w != 0 || l->scrollbar.h != 0) bad++;
        if (l->view.h != l->page_h) bad++;
    }
    return bad;
}

/* The sheet has room for every row and section of the table. */
int test_cheatsheet_layout_capacity_holds_the_table(void)
{
    TEST_BEGIN();
    size_t n = 0;
    (void)shortcuts_table(&n);
    ASSERT_TRUE(n <= (size_t)CS_MAX_ROWS);
    ASSERT_TRUE(shortcuts_sections(NULL, 0u) <= (size_t)CS_MAX_SECTIONS);
    TEST_END();
}

int test_cheatsheet_layout_two_columns_split_1_4_and_5_7_on_1080p_at_96(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 96, 1920, 1040);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.n_sections, 7);
    ASSERT_EQ(l.columns, 2);
    for (int s = 0; s < 7; s++) ASSERT_EQ(l.heading_col[s], s < 4 ? 0 : 1);
    int i = 0;
    for (int s = 0; s < l.n_sections; s++)
        for (int r = 0; r < in.section_rows[s]; r++, i++)
            ASSERT_EQ(l.row_col[i], s < 4 ? 0 : 1);
    ASSERT_EQ(i, l.n_rows);
    ASSERT_EQ(check_geometry(&l), 0);
    /* Spec 3.1 expects no scrollbar here. With alternatives joined the
     * table is 50 rows; by these estimates the taller column (sections 1-4)
     * needs a 962 px page plus the 52 px Close row, 1,014 px, against 1,001
     * available, so it scrolls by about 13 px. The approved default ("the
     * full list, scrolling when it must", spec section 4.1) governs.
     * Reported, not asserted -- see the plan's "Spec conflicts" 1. */
    printf("  [cheatsheet] 1920x1040 @96: page %d px, client %dx%d, %s\n",
           l.page_h, l.client_w, l.client_h, l.scrolls ? "scrolls" : "one page");
    TEST_END();
}

/* The scrollbar appears only when the page is taller than the work area:
 * the same table on a work area with room to spare is one page. */
int test_cheatsheet_layout_no_scrollbar_when_the_page_fits(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 96, 1920, 1300);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.columns, 2);
    ASSERT_EQ(l.scrolls, 0);
    ASSERT_EQ(l.scrollbar.w, 0);
    ASSERT_EQ(l.view.h, l.page_h);
    ASSERT_TRUE(l.client_h <= 1300 - in.nc_h);
    ASSERT_EQ(check_geometry(&l), 0);
    TEST_END();
}

int test_cheatsheet_layout_scrolls_on_1366x768_at_120(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 120, 1366, 718);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.scrolls, 1);
    ASSERT_TRUE(l.client_h <= 718 - in.nc_h);
    ASSERT_TRUE(l.client_w <= 1366 - in.nc_w);
    ASSERT_TRUE(inside(l.button, l.client_w, l.client_h));
    ASSERT_TRUE(inside(l.scrollbar, l.client_w, l.client_h));
    ASSERT_TRUE(l.scrollbar.w > 0 && l.scrollbar.h == l.view.h);
    ASSERT_TRUE(l.button.y >= l.view.y + l.view.h);   /* Close stays outside the scroll */
    ASSERT_EQ(check_geometry(&l), 0);
    TEST_END();
}

int test_cheatsheet_layout_one_column_and_scrolls_at_192_on_1080p(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 192, 1920, 984);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.columns, 1);
    ASSERT_EQ(l.scrolls, 1);
    for (int s = 0; s < l.n_sections; s++) ASSERT_EQ(l.heading_col[s], 0);
    for (int s = 1; s < l.n_sections; s++)               /* section order kept */
        ASSERT_TRUE(l.heading[s].y > l.heading[s - 1].y);
    ASSERT_EQ(check_geometry(&l), 0);
    TEST_END();
}

/* With room for everything, 192 DPI is twice 96 DPI to within rounding. */
int test_cheatsheet_layout_192_is_twice_96(void)
{
    TEST_BEGIN();
    CsInput a, b;
    CsLayout la, lb;
    fill_input(&a, 96, 8000, 8000);
    fill_input(&b, 192, 8000, 8000);
    cheatsheet_layout_compute(&la, &a);
    cheatsheet_layout_compute(&lb, &b);
    ASSERT_EQ(la.columns, 2);
    ASSERT_EQ(lb.columns, 2);
    ASSERT_EQ(la.scrolls, 0);
    ASSERT_EQ(lb.scrolls, 0);
    /* Tolerance: 2 px of rounding, plus 1 px per section in the height,
     * because each section's rule is STROKE_HAIRLINE, which ns_type.h
     * keeps unscaled at every DPI. */
    int dw = lb.client_w - 2 * la.client_w;
    int dh = lb.client_h - 2 * la.client_h;
    ASSERT_TRUE(dw >= -2 && dw <= 2);
    ASSERT_TRUE(dh >= -2 - CS_MAX_SECTIONS && dh <= 2 + CS_MAX_SECTIONS);
    ASSERT_EQ(check_geometry(&la), 0);
    ASSERT_EQ(check_geometry(&lb), 0);
    TEST_END();
}

/* The table is written so every action fits 360 px at 96 DPI (spec
 * section 1), measured with the body estimate. */
int test_cheatsheet_layout_every_action_fits_360(void)
{
    TEST_BEGIN();
    static const int dpis[] = { 96, 120, 144, 168, 192 };
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (size_t d = 0; d < sizeof(dpis) / sizeof(dpis[0]); d++) {
        for (size_t i = 0; i < n; i++) {
            int w = est_w(t[i].action, EST_BODY_CHAR_W_96, dpis[d]);
            if (w > ns_scale(360, dpis[d])) {
                printf("  too wide at %d DPI: \"%s\" (%d px)\n", dpis[d], t[i].action, w);
                ASSERT_TRUE(0);
            }
        }
    }
    TEST_END();
}

/* Not an assertion about a number the spec fixes: it reports where two
 * columns stop fitting a 1920 x 1040 work area, so a change to the table
 * that moves the threshold shows up in the test log. */
int test_cheatsheet_layout_reports_where_two_columns_stop_fitting_1080p(void)
{
    TEST_BEGIN();
    int first_one_col = 0;
    for (int dpi = 96; dpi <= 288 && !first_one_col; dpi++) {
        CsInput in;
        CsLayout l;
        fill_input(&in, dpi, 1920, 1040);
        cheatsheet_layout_compute(&l, &in);
        if (l.columns == 1) first_one_col = dpi;
    }
    printf("  [cheatsheet] two columns stop fitting 1920x1040 at %d DPI\n", first_one_col);
    ASSERT_TRUE(first_one_col > 120);      /* 96 and 120 DPI keep two columns */
    TEST_END();
}

/* A work area shorter than the button row still gives a usable window:
 * one line of view, the button inside the client, nothing negative. */
int test_cheatsheet_layout_tiny_work_area_stays_sane(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 192, 640, 120);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.columns, 1);
    ASSERT_EQ(l.scrolls, 1);
    ASSERT_TRUE(l.view.h >= in.body_h);
    ASSERT_TRUE(l.client_w > 0 && l.client_h > 0);
    ASSERT_TRUE(l.client_w <= 640 - in.nc_w);
    ASSERT_TRUE(inside(l.button, l.client_w, l.client_h));
    ASSERT_TRUE(l.button.w > 0 && l.button.h > 0);
    TEST_END();
}

int test_cheatsheet_layout_degenerate_inputs(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    cheatsheet_layout_compute(NULL, NULL);          /* no crash */
    memset(&in, 0, sizeof(in));
    cheatsheet_layout_compute(NULL, &in);
    cheatsheet_layout_compute(&l, &in);             /* nothing at all */
    ASSERT_EQ(l.n_rows, 0);
    ASSERT_TRUE(l.client_w > 0);
    ASSERT_TRUE(l.client_h > 0);
    in.dpi = -5;                                     /* <= 0 is 96 */
    in.n_sections = 99;                              /* clamped */
    in.section_rows[0] = 1000;                       /* clamped to CS_MAX_ROWS */
    in.body_h = -3;                                  /* clamped to 0 */
    in.work_w = 1920; in.work_h = 1040;
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.n_sections, CS_MAX_SECTIONS);
    ASSERT_EQ(l.n_rows, CS_MAX_ROWS);
    ASSERT_TRUE(l.view.h >= 1);
    TEST_END();
}

int test_cheatsheet_scroll_clamp_both_ends(void)
{
    TEST_BEGIN();
    ASSERT_EQ(cheatsheet_scroll_clamp(-10, 1000, 400), 0);
    ASSERT_EQ(cheatsheet_scroll_clamp(0, 1000, 400), 0);
    ASSERT_EQ(cheatsheet_scroll_clamp(250, 1000, 400), 250);
    ASSERT_EQ(cheatsheet_scroll_clamp(600, 1000, 400), 600);
    ASSERT_EQ(cheatsheet_scroll_clamp(601, 1000, 400), 600);
    ASSERT_EQ(cheatsheet_scroll_clamp(99999, 1000, 400), 600);
    ASSERT_EQ(cheatsheet_scroll_clamp(50, 300, 400), 0);   /* page shorter than view */
    ASSERT_EQ(cheatsheet_scroll_clamp(50, 400, 400), 0);
    TEST_END();
}

int test_cheatsheet_place_centres_then_keeps_to_the_work_area(void)
{
    TEST_BEGIN();
    int x = 0, y = 0;
    /* Centred on an owner well inside the work area. */
    cheatsheet_place(100, 100, 1000, 800, 400, 300, 0, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, 400);
    ASSERT_EQ(y, 350);
    /* Owner hanging off the right and bottom edges: pulled back in. */
    cheatsheet_place(1700, 900, 800, 600, 600, 500, 0, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, 1920 - 600);
    ASSERT_EQ(y, 1040 - 500);
    /* Owner on a second monitor left of the primary (negative x). */
    cheatsheet_place(-1900, 50, 800, 600, 600, 500, -1920, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, -1900 + 100);
    ASSERT_EQ(y, 100);
    /* Owner off the top-left: pulled to the work area's corner. */
    cheatsheet_place(-500, -400, 600, 400, 600, 500, 0, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, 0);
    ASSERT_EQ(y, 0);
    /* Window bigger than the work area: its top-left corner wins. */
    cheatsheet_place(0, 0, 800, 600, 3000, 2000, 0, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, 0);
    ASSERT_EQ(y, 0);
    /* NULL outputs are allowed. */
    cheatsheet_place(0, 0, 10, 10, 5, 5, 0, 0, 100, 100, NULL, NULL);
    TEST_END();
}
```

In `tests/runner.c`, after `int test_shortcuts_readme_check_handles_crlf_and_bad_markers(void);` add

```c
/* test_cheatsheet_layout.c */
int test_cheatsheet_layout_capacity_holds_the_table(void);
int test_cheatsheet_layout_two_columns_split_1_4_and_5_7_on_1080p_at_96(void);
int test_cheatsheet_layout_no_scrollbar_when_the_page_fits(void);
int test_cheatsheet_layout_scrolls_on_1366x768_at_120(void);
int test_cheatsheet_layout_one_column_and_scrolls_at_192_on_1080p(void);
int test_cheatsheet_layout_192_is_twice_96(void);
int test_cheatsheet_layout_every_action_fits_360(void);
int test_cheatsheet_layout_reports_where_two_columns_stop_fitting_1080p(void);
int test_cheatsheet_layout_tiny_work_area_stays_sane(void);
int test_cheatsheet_layout_degenerate_inputs(void);
int test_cheatsheet_scroll_clamp_both_ends(void);
int test_cheatsheet_place_centres_then_keeps_to_the_work_area(void);
```

and after `    failed += test_shortcuts_readme_check_handles_crlf_and_bad_markers();` add

```c
    failed += test_cheatsheet_layout_capacity_holds_the_table();
    failed += test_cheatsheet_layout_two_columns_split_1_4_and_5_7_on_1080p_at_96();
    failed += test_cheatsheet_layout_no_scrollbar_when_the_page_fits();
    failed += test_cheatsheet_layout_scrolls_on_1366x768_at_120();
    failed += test_cheatsheet_layout_one_column_and_scrolls_at_192_on_1080p();
    failed += test_cheatsheet_layout_192_is_twice_96();
    failed += test_cheatsheet_layout_every_action_fits_360();
    failed += test_cheatsheet_layout_reports_where_two_columns_stop_fitting_1080p();
    failed += test_cheatsheet_layout_tiny_work_area_stays_sane();
    failed += test_cheatsheet_layout_degenerate_inputs();
    failed += test_cheatsheet_scroll_clamp_both_ends();
    failed += test_cheatsheet_place_centres_then_keeps_to_the_work_area();
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make test 2>&1 | grep -E 'cheatsheet_layout|error' | head -3`
Expected: the build fails with `fatal error: cheatsheet_layout.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/core/cheatsheet_layout.h`:

```c
#ifndef NUTSHELL_CHEATSHEET_LAYOUT_H
#define NUTSHELL_CHEATSHEET_LAYOUT_H

/*
 * cheatsheet_layout — portable (Win32-free) layout math for the keyboard
 * shortcuts sheet (spec docs/superpowers/specs/2026-10-20-keyboard-cheat-
 * sheet-design.md section 2.2). Pure C, no windows.h: compiled into both
 * the MinGW build and the native tests (tests/test_cheatsheet_layout.c).
 *
 * The Win32 side (src/ui/cheatsheet_dlg.c) measures the text and the line
 * heights with the real fonts and passes them in; the native test passes
 * fixed per-character estimates instead.
 *
 * Coordinates: everything that scrolls (headings, rules, rows) is in PAGE
 * coordinates, origin at the top-left of the scrolled content; the painter
 * subtracts the scroll offset and adds view.y. view, scrollbar and button
 * are in CLIENT coordinates and never scroll.
 */

enum {
    CS_MAX_SECTIONS = 8,
    CS_MAX_ROWS     = 96,
    CS_LEFT_SECTIONS = 4   /* in two columns: sections 1-4 left, 5-7 right */
};

typedef struct { int x, y, w, h; } CsRect;

typedef struct {
    int dpi;               /* <= 0 is treated as 96 */
    int work_w, work_h;    /* monitor work area, px */
    int nc_w, nc_h;        /* what the frame and caption add to the client size */
    int n_sections;        /* <= CS_MAX_SECTIONS */
    int section_rows[CS_MAX_SECTIONS];  /* rows per section, display order */
    unsigned char has_cond[CS_MAX_ROWS]; /* per row, display order: 1 = condition line */
    int chord_w;           /* widest chord, px */
    int text_w;            /* widest action or condition, px */
    int heading_w;         /* widest section heading, px */
    int body_h;            /* FONT_BODY line height, px */
    int caption_h;         /* FONT_CAPTION line height, px */
    int heading_h;         /* FONT_HEADING line height, px */
    int scrollbar_w;       /* the themed scrollbar's width (CSB_WIDTH), px */
} CsInput;

typedef struct {
    int columns;           /* 1 or 2 */
    int col_w;             /* one column's width */
    int client_w, client_h;
    int page_h;            /* full height of the scrolled content */
    int scrolls;           /* 1 when page_h > view.h */
    CsRect view;           /* the scrolled viewport, client coords */
    CsRect scrollbar;      /* client coords; all zero when !scrolls */
    CsRect button;         /* Close, client coords, below the view */
    int n_sections, n_rows;
    int    heading_col[CS_MAX_SECTIONS];
    CsRect heading[CS_MAX_SECTIONS];   /* page coords */
    CsRect rule[CS_MAX_SECTIONS];      /* border-colour line under the heading */
    int    row_col[CS_MAX_ROWS];
    CsRect row[CS_MAX_ROWS];           /* the whole row, both lines */
    CsRect chord[CS_MAX_ROWS];
    CsRect action[CS_MAX_ROWS];
    CsRect cond[CS_MAX_ROWS];          /* h == 0 when the row has none */
} CsLayout;

/* Lay the sheet out. Negative sizes are clamped to 0; n_sections and the
 * row counts are clamped to the CS_MAX_* capacities. `out` may be NULL. */
void cheatsheet_layout_compute(CsLayout *out, const CsInput *in);

/* Clamp a scroll offset to [0, max(0, page_h - view_h)]. */
int cheatsheet_scroll_clamp(int offset, int page_h, int view_h);

/* Where a win_w x win_h window goes: centred on the owner's rectangle,
 * then moved to lie inside the work area (top-left wins when the window is
 * larger than the work area). */
void cheatsheet_place(int owner_x, int owner_y, int owner_w, int owner_h,
                      int win_w, int win_h,
                      int work_x, int work_y, int work_w, int work_h,
                      int *out_x, int *out_y);

#endif /* NUTSHELL_CHEATSHEET_LAYOUT_H */
```

- [ ] **Step 4: Write the layout**

Create `src/core/cheatsheet_layout.c`:

```c
#include "cheatsheet_layout.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

static int clamp0(int v) { return v < 0 ? 0 : v; }
static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

int cheatsheet_scroll_clamp(int offset, int page_h, int view_h)
{
    int max_off = page_h - view_h;
    if (max_off < 0) max_off = 0;
    if (offset < 0) return 0;
    return offset > max_off ? max_off : offset;
}

void cheatsheet_place(int owner_x, int owner_y, int owner_w, int owner_h,
                      int win_w, int win_h,
                      int work_x, int work_y, int work_w, int work_h,
                      int *out_x, int *out_y)
{
    int x = owner_x + (owner_w - win_w) / 2;
    int y = owner_y + (owner_h - win_h) / 2;
    if (x + win_w > work_x + work_w) x = work_x + work_w - win_w;
    if (y + win_h > work_y + work_h) y = work_y + work_h - win_h;
    if (x < work_x) x = work_x;
    if (y < work_y) y = work_y;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

void cheatsheet_layout_compute(CsLayout *out, const CsInput *in_raw)
{
    if (!out || !in_raw) return;
    memset(out, 0, sizeof(*out));

    CsInput in = *in_raw;
    int dpi = in.dpi > 0 ? in.dpi : 96;
    in.chord_w = clamp0(in.chord_w);
    in.text_w = clamp0(in.text_w);
    in.heading_w = clamp0(in.heading_w);
    in.body_h = clamp0(in.body_h);
    in.caption_h = clamp0(in.caption_h);
    in.heading_h = clamp0(in.heading_h);
    in.scrollbar_w = clamp0(in.scrollbar_w);
    in.work_w = clamp0(in.work_w);
    in.work_h = clamp0(in.work_h);
    in.nc_w = clamp0(in.nc_w);
    in.nc_h = clamp0(in.nc_h);
    int ns = imin(imax(in.n_sections, 0), CS_MAX_SECTIONS);

    /* Row counts, clamped so the running total never passes CS_MAX_ROWS. */
    int rows_of[CS_MAX_SECTIONS];
    int first_of[CS_MAX_SECTIONS];
    int total = 0;
    for (int s = 0; s < ns; s++) {
        int r = clamp0(in.section_rows[s]);
        if (total + r > CS_MAX_ROWS) r = CS_MAX_ROWS - total;
        first_of[s] = total;
        rows_of[s] = r;
        total += r;
    }
    out->n_sections = ns;
    out->n_rows = total;

    const int pad        = ns_scale(SP_LG, dpi);   /* page padding */
    const int sec_gap    = ns_scale(SP_XL, dpi);   /* between sections */
    const int col_gap    = ns_scale(SP_XL, dpi);   /* between the two columns */
    const int row_gap    = ns_scale(SP_XS, dpi);   /* between rows */
    const int gutter     = ns_scale(SP_LG, dpi);   /* chord column -> text column */
    const int rule_gap   = ns_scale(SP_XS, dpi);   /* heading text -> rule */
    const int rule_h     = STROKE_HAIRLINE;        /* strokes are not scaled */
    const int after_rule = ns_scale(SP_SM, dpi);   /* rule -> first row */
    const int btn_w      = ns_scale(SZ_BTN_MIN_W, dpi);
    const int btn_h      = ns_scale(SZ_CTRL_H, dpi);
    const int btn_top    = ns_scale(SP_SM, dpi);   /* view -> button */
    const int btn_area   = btn_top + btn_h + pad;  /* below the view */

    const int col_w = imax(in.chord_w + gutter + in.text_w, in.heading_w);
    out->col_w = col_w;

    const int avail_w = clamp0(in.work_w - in.nc_w);
    const int avail_h = clamp0(in.work_h - in.nc_h);

    /* Two columns when they fit beside each other with the scrollbar's
     * width held back, so the choice never depends on whether it scrolls. */
    const int two_w = 2 * pad + 2 * col_w + col_gap + in.scrollbar_w;
    const int cols = (ns > CS_LEFT_SECTIONS && two_w <= avail_w) ? 2 : 1;
    out->columns = cols;

    /* Place every section in page coordinates. */
    int col_y[2] = { pad, pad };
    int placed[2] = { 0, 0 };
    for (int s = 0; s < ns; s++) {
        int c = (cols == 2 && s >= CS_LEFT_SECTIONS) ? 1 : 0;
        int x = pad + c * (col_w + col_gap);
        int y = col_y[c];
        if (placed[c]) y += sec_gap;            /* not before a column's first */
        placed[c] = 1;

        out->heading_col[s] = c;
        out->heading[s] = (CsRect){ x, y, col_w, in.heading_h };
        y += in.heading_h + rule_gap;
        out->rule[s] = (CsRect){ x, y, col_w, rule_h };
        y += rule_h + after_rule;

        for (int r = 0; r < rows_of[s]; r++) {
            int i = first_of[s] + r;
            int cond_h = in.has_cond[i] ? in.caption_h : 0;
            int text_x = x + in.chord_w + gutter;
            int text_w = col_w - in.chord_w - gutter;
            out->row_col[i] = c;
            out->row[i]    = (CsRect){ x, y, col_w, in.body_h + cond_h };
            out->chord[i]  = (CsRect){ x, y, in.chord_w, in.body_h };
            out->action[i] = (CsRect){ text_x, y, text_w, in.body_h };
            out->cond[i]   = (CsRect){ text_x, y + in.body_h, text_w, cond_h };
            y += in.body_h + cond_h;
            if (r + 1 < rows_of[s]) y += row_gap;
        }
        col_y[c] = y;
    }

    const int content_bottom = imax(col_y[0], col_y[1]);
    out->page_h = content_bottom + pad;

    int page_w = 2 * pad + cols * col_w + (cols - 1) * col_gap;
    int scrolls = out->page_h + btn_area > avail_h;
    int view_h = scrolls ? avail_h - btn_area : out->page_h;
    /* A work area too short for even one line still gets one line. */
    if (view_h < in.body_h) view_h = in.body_h;
    if (view_h < 1) view_h = 1;
    if (view_h >= out->page_h) { view_h = out->page_h; scrolls = 0; }
    out->scrolls = scrolls;

    int client_w = page_w + (scrolls ? in.scrollbar_w : 0);
    /* A work area narrower than one column: the window keeps to the work
     * area and the text column is clipped -- no horizontal scrolling, no
     * ellipsis (spec section 1). */
    if (avail_w > 0 && client_w > avail_w) client_w = avail_w;
    if (client_w < btn_w + 2 * pad) client_w = btn_w + 2 * pad;
    out->client_w = client_w;
    out->client_h = view_h + btn_area;

    int view_w = client_w - (scrolls ? in.scrollbar_w : 0);
    out->view = (CsRect){ 0, 0, view_w, view_h };
    if (scrolls)
        out->scrollbar = (CsRect){ view_w, 0, in.scrollbar_w, view_h };
    out->button = (CsRect){ (client_w - btn_w) / 2, view_h + btn_top, btn_w, btn_h };
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `make test 2>&1 | grep -E '^\[(PASS|FAIL)\] test_cheatsheet|\[cheatsheet\]|Tests Run'`
Expected: twelve `[PASS] test_cheatsheet_...` lines; the two report lines
`[cheatsheet] 1920x1040 @96: page 962 px, client 1246x1001, scrolls` and
`[cheatsheet] two columns stop fitting 1920x1040 at 147 DPI`; `Failed: 0`.

Compile it the way the Windows build will:

```bash
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow \
  -Wformat=2 -Wconversion -Os -Isrc -Isrc/core -Isrc/ui \
  -c src/core/cheatsheet_layout.c -o /dev/null && echo OK
```

Expected: `OK`.

- [ ] **Step 6: Commit**

```bash
git add src/core/cheatsheet_layout.h src/core/cheatsheet_layout.c tests/test_cheatsheet_layout.c tests/runner.c
git status --short
git commit
```

Message: `feat(cheatsheet): pure layout for the shortcuts sheet (columns, scroll, placement)`; commit with the repo's Models line and trailer.

---

### Task 4: The sheet window

**Files:**
- Create: `src/ui/cheatsheet_dlg.h`, `src/ui/cheatsheet_dlg.c`

**Interfaces:**
- Consumes: Task 1 (`shortcuts_table`, `shortcuts_sections`); Task 3 (`CsInput`, `CsLayout`, `cheatsheet_layout_compute`, `cheatsheet_scroll_clamp`, `cheatsheet_place`, `CS_MAX_*`); `ns_font(role, dpi)` (`src/ui/ns_font.h`); `ns_tokens()` (`src/ui/ns_tokens.h`); `theme_cr()`, `draw_themed_button()`, `themed_apply_title_bar()`, `themed_apply_borders()` (`src/ui/themed_button.h`); `csb_register()`, `csb_create()`, `csb_set_range()`, `csb_set_pos()`, `csb_get_trackpos()`, `CSB_WIDTH` (`src/ui/custom_scrollbar.h`); `ns_draw_separator()` (`src/ui/ns_draw.h`); `get_window_dpi()` (`src/ui/dpi_util.h`); `ui_theme_get()` (`src/core/ui_theme.h`).
- Produces: `void cheatsheet_show(HWND parent, HWND floating_panel, const ThemeColors *theme);` and the window class `Nutshell_Shortcuts`, title `Keyboard Shortcuts`, Close button control id `IDOK` (Task 5 calls the function; Task 8 finds the class and title).

Behaviour, from spec 2.3: custom-painted, double-buffered; colours only from `ns_tokens()` (`bg_primary.base` page, `text_main` headings, chords and actions, `text_dim` conditions, `border` rules) through `theme_cr()`; fonts only from `ns_font()`; one owner-draw Close button by `draw_themed_button`; the themed scrollbar only when the layout scrolls (wheel, `WM_VSCROLL`, PgUp/PgDn, Up/Down, Home/End); Esc, Enter, Close and the caption's X close it; `WM_DPICHANGED` re-fetches fonts without `ns_font_flush()`, re-measures, re-lays-out and re-clamps to the new monitor (as `cmd_detail_dlg.c:293-307`); modality as the passphrase prompt (`window.c:940-986`). Text a screen reader cannot see is a stated trade-off (the README carries the same table as text).

There is no native test for this file (`src/ui` is excluded from `make test`); its checks are the MinGW `-Werror` compile below, the design-system gate in `make test` (`test_ui_tokens_colour_gate`, `test_ui_tokens_scale_gate` scan every `src/ui/*.c`), Task 8's harness cases and Task 9's `--ui-demo=shortcuts` look.

- [ ] **Step 1: Write the header**

Create `src/ui/cheatsheet_dlg.h`:

```c
#ifndef NUTSHELL_CHEATSHEET_DLG_H
#define NUTSHELL_CHEATSHEET_DLG_H

#ifdef _WIN32
#include <windows.h>
#include "ui_theme.h"

/*
 * Help > Keyboard Shortcuts: the modal, custom-painted shortcuts sheet
 * (spec docs/superpowers/specs/2026-10-20-keyboard-cheat-sheet-design.md
 * section 2.3). Draws src/core/shortcuts.c's table in the layout
 * src/core/cheatsheet_layout.c computes.
 *
 *   parent         -- the main window: owner, disabled while the sheet is
 *                     up, re-enabled only if this call disabled it.
 *   floating_panel -- the undocked AI Assist window, or NULL; disabled and
 *                     re-enabled by the same rule.
 *   theme          -- the active ThemeColors (title bar, Close button,
 *                     scrollbar); NULL means the first built-in theme.
 *
 * Returns when the sheet closes (Esc, Enter, Close, the caption's X). A call
 * made while a sheet is already open returns at once and opens nothing. A
 * WM_QUIT that arrives while it is open is re-posted after it closes.
 */
void cheatsheet_show(HWND parent, HWND floating_panel, const ThemeColors *theme);

#endif /* _WIN32 */
#endif /* NUTSHELL_CHEATSHEET_DLG_H */
```

- [ ] **Step 2: Write the window**

Create `src/ui/cheatsheet_dlg.c`:

```c
#include "cheatsheet_dlg.h"

#ifdef _WIN32
#include "cheatsheet_layout.h"
#include "shortcuts.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "ns_draw.h"
#include "ns_tokens.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "dpi_util.h"
#include <stdlib.h>
#include <string.h>

#define CHEAT_CLASS       "Nutshell_Shortcuts"
#define CHEAT_TITLE       "Keyboard Shortcuts"
#define CHEAT_STYLE       (WS_POPUP | WS_CAPTION | WS_SYSMENU)
#define CHEAT_EXSTYLE     WS_EX_DLGMODALFRAME

typedef struct {
    const ThemeColors *theme;
    HWND     hClose;       /* owner-draw Close button, id IDOK */
    HWND     hScroll;      /* themed scrollbar; NULL until the page first scrolls */
    int      dpi;
    int      scroll;       /* px into the page */
    int      closing;      /* set by Close/Esc/Enter/WM_CLOSE; ends the loop */
    CsInput  in;
    CsLayout lay;
} CheatData;

static int s_open = 0;     /* a second Help > Keyboard Shortcuts is ignored */

/* One line of `role` text: the ramp's pixel size times its line height. */
static int cheat_line_h(NsFontRole role, int dpi)
{
    const NsFontSpec *spec = ns_type_font(role);
    double px = (double)ns_type_font_px(role, dpi, 0) * spec->line_height;
    return (int)(px + 0.5);
}

static int cheat_text_w(HDC hdc, HFONT font, const char *s)
{
    if (!s || !s[0]) return 0;
    HGDIOBJ old = font ? SelectObject(hdc, (HGDIOBJ)font) : NULL;
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32A(hdc, s, (int)strlen(s), &sz);
    if (old) SelectObject(hdc, old);
    return (int)sz.cx;
}

/* Fill d->in from the table, measured with the real fonts at d->dpi, for a
 * work area of work_w x work_h. The frame size comes from the live window. */
static void cheat_measure(HWND hwnd, CheatData *d, int work_w, int work_h)
{
    CsInput *in = &d->in;
    memset(in, 0, sizeof(*in));
    in->dpi = d->dpi;
    in->work_w = work_w;
    in->work_h = work_h;

    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    in->nc_w = (wr.right - wr.left) - cr.right;
    in->nc_h = (wr.bottom - wr.top) - cr.bottom;

    in->body_h      = cheat_line_h(FONT_BODY, d->dpi);
    in->caption_h   = cheat_line_h(FONT_CAPTION, d->dpi);
    in->heading_h   = cheat_line_h(FONT_HEADING, d->dpi);
    in->scrollbar_w = ns_scale(CSB_WIDTH, d->dpi);

    HFONT fb = ns_font(FONT_BODY, d->dpi);
    HFONT fc = ns_font(FONT_CAPTION, d->dpi);
    HFONT fh = ns_font(FONT_HEADING, d->dpi);
    HDC hdc = GetDC(hwnd);
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    int s = -1;
    for (size_t i = 0; i < n && i < CS_MAX_ROWS; i++) {
        if (i == 0 || strcmp(t[i].section, t[i - 1].section) != 0) {
            if (s + 1 >= CS_MAX_SECTIONS) break;
            s++;
            int hw = cheat_text_w(hdc, fh, t[i].section);
            if (hw > in->heading_w) in->heading_w = hw;
        }
        in->section_rows[s]++;
        in->has_cond[i] = t[i].condition[0] ? 1 : 0;
        int cw = cheat_text_w(hdc, fb, t[i].chord);
        int aw = cheat_text_w(hdc, fb, t[i].action);
        int dw = cheat_text_w(hdc, fc, t[i].condition);
        if (cw > in->chord_w) in->chord_w = cw;
        if (aw > in->text_w)  in->text_w = aw;
        if (dw > in->text_w)  in->text_w = dw;
    }
    ReleaseDC(hwnd, hdc);
    in->n_sections = s + 1;
}

/* Close button and scrollbar where the layout puts them. */
static void cheat_place_children(HWND hwnd, CheatData *d)
{
    const CsLayout *l = &d->lay;
    if (d->hClose)
        MoveWindow(d->hClose, l->button.x, l->button.y, l->button.w, l->button.h, TRUE);
    if (l->scrolls) {
        if (!d->hScroll) {
            csb_register(GetModuleHandle(NULL));
            d->hScroll = csb_create(hwnd, l->scrollbar.x, l->scrollbar.y,
                                    l->scrollbar.w, l->scrollbar.h,
                                    d->theme, GetModuleHandle(NULL));
        } else {
            MoveWindow(d->hScroll, l->scrollbar.x, l->scrollbar.y,
                       l->scrollbar.w, l->scrollbar.h, TRUE);
            ShowWindow(d->hScroll, SW_SHOWNOACTIVATE);
        }
        if (d->hScroll) {
            csb_set_range(d->hScroll, 0, l->page_h > 0 ? l->page_h - 1 : 0, l->view.h);
            csb_set_pos(d->hScroll, d->scroll);
        }
    } else if (d->hScroll) {
        ShowWindow(d->hScroll, SW_HIDE);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

/* Measure, lay out, size and place the window: centred on `anchor` (the
 * owner's rectangle, or the rectangle WM_DPICHANGED suggests), inside the
 * work area of the monitor `anchor` is on. */
static void cheat_fit(HWND hwnd, CheatData *d, const RECT *anchor)
{
    RECT work = *anchor;
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(MonitorFromRect(anchor, MONITOR_DEFAULTTONEAREST), &mi))
        work = mi.rcWork;

    cheat_measure(hwnd, d, work.right - work.left, work.bottom - work.top);
    cheatsheet_layout_compute(&d->lay, &d->in);

    int win_w = d->lay.client_w + d->in.nc_w;
    int win_h = d->lay.client_h + d->in.nc_h;
    int x = 0, y = 0;
    cheatsheet_place(anchor->left, anchor->top,
                     anchor->right - anchor->left, anchor->bottom - anchor->top,
                     win_w, win_h, work.left, work.top,
                     work.right - work.left, work.bottom - work.top, &x, &y);
    SetWindowPos(hwnd, NULL, x, y, win_w, win_h, SWP_NOZORDER | SWP_NOACTIVATE);

    /* The frame can differ by a pixel or two from the one measured before
     * the move (per-monitor DPI); trim so the client is exactly the layout. */
    RECT cr;
    GetClientRect(hwnd, &cr);
    int dw = d->lay.client_w - cr.right;
    int dh = d->lay.client_h - cr.bottom;
    if (dw != 0 || dh != 0)
        SetWindowPos(hwnd, NULL, 0, 0, win_w + dw, win_h + dh,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    d->scroll = cheatsheet_scroll_clamp(d->scroll, d->lay.page_h, d->lay.view.h);
    cheat_place_children(hwnd, d);
}

static void cheat_scroll_to(HWND hwnd, CheatData *d, int offset)
{
    int s = cheatsheet_scroll_clamp(offset, d->lay.page_h, d->lay.view.h);
    if (s == d->scroll) return;
    d->scroll = s;
    if (d->hScroll) csb_set_pos(d->hScroll, s);
    RECT v = { d->lay.view.x, d->lay.view.y,
               d->lay.view.x + d->lay.view.w, d->lay.view.y + d->lay.view.h };
    InvalidateRect(hwnd, &v, FALSE);
}

static int cheat_line_step(const CheatData *d)
{
    return d->in.body_h + ns_scale(SP_XS, d->dpi);
}

static int cheat_page_step(const CheatData *d)
{
    int step = d->lay.view.h - cheat_line_step(d);
    return step > 0 ? step : cheat_line_step(d);
}

/* Keys the sheet handles itself. 1 when the key was consumed. */
static int cheat_key(HWND hwnd, CheatData *d, WPARAM vk)
{
    switch (vk) {
    case VK_ESCAPE:
    case VK_RETURN: d->closing = 1; return 1;
    case VK_PRIOR:  cheat_scroll_to(hwnd, d, d->scroll - cheat_page_step(d)); return 1;
    case VK_NEXT:   cheat_scroll_to(hwnd, d, d->scroll + cheat_page_step(d)); return 1;
    case VK_UP:     cheat_scroll_to(hwnd, d, d->scroll - cheat_line_step(d)); return 1;
    case VK_DOWN:   cheat_scroll_to(hwnd, d, d->scroll + cheat_line_step(d)); return 1;
    case VK_HOME:   cheat_scroll_to(hwnd, d, 0); return 1;
    case VK_END:    cheat_scroll_to(hwnd, d, d->lay.page_h); return 1;
    default:        return 0;
    }
}

static void cheat_text(HDC hdc, CsRect r, int dy, NsFontRole role, int dpi,
                       unsigned int colour, const char *s)
{
    if (!s || !s[0] || r.w <= 0 || r.h <= 0) return;
    RECT rc = { r.x, r.y + dy, r.x + r.w, r.y + dy + r.h };
    HFONT f = ns_font(role, dpi);
    HGDIOBJ old = f ? SelectObject(hdc, (HGDIOBJ)f) : NULL;
    SetTextColor(hdc, theme_cr(colour));
    DrawTextA(hdc, s, -1, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
}

static void cheat_paint(HDC hdc, const RECT *client, const CheatData *d)
{
    const ThemeTokens *tk = ns_tokens();
    const CsLayout *l = &d->lay;

    HBRUSH bg = CreateSolidBrush(theme_cr(tk->bg_primary.base));
    FillRect(hdc, client, bg);
    DeleteObject(bg);

    HRGN clip = CreateRectRgn(l->view.x, l->view.y,
                              l->view.x + l->view.w, l->view.y + l->view.h);
    SelectClipRgn(hdc, clip);
    SetBkMode(hdc, TRANSPARENT);

    const int dy = l->view.y - d->scroll;
    const int top = d->scroll;
    const int bottom = d->scroll + l->view.h;

    const char *names[CS_MAX_SECTIONS];
    size_t ns = shortcuts_sections(names, CS_MAX_SECTIONS);
    for (int s = 0; s < l->n_sections && (size_t)s < ns; s++) {
        cheat_text(hdc, l->heading[s], dy, FONT_HEADING, d->dpi, tk->text_main, names[s]);
        ns_draw_separator(hdc, l->rule[s].x, l->rule[s].x + l->rule[s].w,
                          l->rule[s].y + dy, theme_cr(tk->border));
    }

    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (int i = 0; i < l->n_rows && (size_t)i < n; i++) {
        const CsRect *row = &l->row[i];
        if (row->y + row->h < top || row->y > bottom) continue;
        cheat_text(hdc, l->chord[i],  dy, FONT_BODY,    d->dpi, tk->text_main, t[i].chord);
        cheat_text(hdc, l->action[i], dy, FONT_BODY,    d->dpi, tk->text_main, t[i].action);
        cheat_text(hdc, l->cond[i],   dy, FONT_CAPTION, d->dpi, tk->text_dim,  t[i].condition);
    }

    SelectClipRgn(hdc, NULL);
    DeleteObject(clip);
}

static LRESULT CALLBACK CheatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CheatData *d = (CheatData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        d = (CheatData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        d->dpi = get_window_dpi(hwnd);
        d->hClose = CreateWindowA("BUTTON", "Close",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                  0, 0, 10, 10, hwnd, (HMENU)(UINT_PTR)IDOK,
                                  GetModuleHandle(NULL), NULL);
        if (d->hClose) {
            HFONT font = ns_font(FONT_BODY, d->dpi);
            if (font) SendMessage(d->hClose, WM_SETFONT, (WPARAM)font, TRUE);
        }
        themed_apply_title_bar(hwnd, d->theme);
        themed_apply_borders(hwnd, d->theme);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (d && rc.right > 0 && rc.bottom > 0) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, (HGDIOBJ)bmp);
            cheat_paint(mem, &rc, d);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (d && dis && dis->CtlID == IDOK) {
            draw_themed_button(dis, d->theme, 1);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        if (d && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
            d->closing = 1;
            return 0;
        }
        break;

    case WM_CLOSE:                       /* the caption's X: the loop tears down */
        if (d) d->closing = 1;
        return 0;

    case WM_KEYDOWN:
        if (d && cheat_key(hwnd, d, wParam)) return 0;
        break;

    case WM_MOUSEWHEEL:
        if (d && d->lay.scrolls) {
            int lines = 3 * GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            cheat_scroll_to(hwnd, d, d->scroll - lines * cheat_line_step(d));
            return 0;
        }
        break;

    case WM_VSCROLL:
        if (d && d->hScroll && (HWND)lParam == d->hScroll) {
            switch (LOWORD(wParam)) {
            case SB_LINEUP:        cheat_scroll_to(hwnd, d, d->scroll - cheat_line_step(d)); break;
            case SB_LINEDOWN:      cheat_scroll_to(hwnd, d, d->scroll + cheat_line_step(d)); break;
            case SB_PAGEUP:        cheat_scroll_to(hwnd, d, d->scroll - cheat_page_step(d)); break;
            case SB_PAGEDOWN:      cheat_scroll_to(hwnd, d, d->scroll + cheat_page_step(d)); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: cheat_scroll_to(hwnd, d, csb_get_trackpos(d->hScroll)); break;
            case SB_TOP:           cheat_scroll_to(hwnd, d, 0); break;
            case SB_BOTTOM:        cheat_scroll_to(hwnd, d, d->lay.page_h); break;
            default: break;
            }
            return 0;
        }
        break;

    case WM_DPICHANGED:
        if (d) {
            /* Fonts come straight from the shared cache at the new DPI;
             * ns_font_flush() would pull the main window's fonts out from
             * under it, so it is never called here (as cmd_detail_dlg.c). */
            d->dpi = (int)HIWORD(wParam);
            HFONT font = ns_font(FONT_BODY, d->dpi);
            if (d->hClose && font) SendMessage(d->hClose, WM_SETFONT, (WPARAM)font, TRUE);
            cheat_fit(hwnd, d, (const RECT *)lParam);
            return 0;
        }
        break;

    case WM_NCDESTROY:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        break;

    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void cheatsheet_show(HWND parent, HWND floating_panel, const ThemeColors *theme)
{
    if (s_open) return;
    if (!theme) theme = ui_theme_get(0);

    static int registered = 0;
    if (!registered) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = CheatWndProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = CHEAT_CLASS;
        RegisterClassExA(&wc);
        registered = 1;
    }

    RECT anchor = { 0, 0, 0, 0 };
    if (!parent || !GetWindowRect(parent, &anchor))
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &anchor, 0);

    CheatData d;
    memset(&d, 0, sizeof(d));
    d.theme = theme;

    HWND prev_focus = GetFocus();
    /* Created on the owner's monitor so its DPI is the owner's; cheat_fit()
     * then gives it its real size and position. */
    HWND dlg = CreateWindowExA(CHEAT_EXSTYLE, CHEAT_CLASS, CHEAT_TITLE, CHEAT_STYLE,
                               anchor.left, anchor.top, 400, 300,
                               parent, NULL, GetModuleHandle(NULL), &d);
    if (!dlg) return;
    s_open = 1;
    cheat_fit(dlg, &d, &anchor);

    /* was_disabled (window.c's passphrase prompt): re-enable each window
     * only if this call is the one that disabled it, so a sheet opened
     * inside another modal never re-enables the main window under it. */
    BOOL parent_was_disabled = parent ? EnableWindow(parent, FALSE) : TRUE;
    BOOL panel_was_disabled = TRUE;
    if (floating_panel && IsWindow(floating_panel))
        panel_was_disabled = EnableWindow(floating_panel, FALSE);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    if (d.hClose) SetFocus(d.hClose);

    MSG m;
    memset(&m, 0, sizeof(m));
    int gm = 1;
    while (IsWindow(dlg) && !d.closing && (gm = GetMessage(&m, NULL, 0, 0)) > 0) {
        /* Keys reach the focused Close button, not the sheet, so they are
         * taken here: IsDialogMessage never sees an owner-draw default. */
        if (m.message == WM_KEYDOWN && (m.hwnd == dlg || IsChild(dlg, m.hwnd)) &&
            cheat_key(dlg, &d, m.wParam))
            continue;
        if (!IsDialogMessage(dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }

    if (parent && !parent_was_disabled) EnableWindow(parent, TRUE);
    if (floating_panel && !panel_was_disabled && IsWindow(floating_panel))
        EnableWindow(floating_panel, TRUE);
    if (parent) SetForegroundWindow(parent);   /* before the destroy, or Windows activates another app */
    if (IsWindow(dlg)) DestroyWindow(dlg);
    if (prev_focus && IsWindow(prev_focus)) SetFocus(prev_focus);
    s_open = 0;

    /* A real WM_QUIT (gm == 0) belongs to ui_run()'s loop; -1 is an error
     * with no quit in it and must not be forwarded. */
    if (gm == 0) PostQuitMessage((int)m.wParam);
}

#endif /* _WIN32 */
```

- [ ] **Step 3: Compile it the way the Windows build will**

```bash
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow \
  -Wformat=2 -Wconversion -Os -Isrc -Isrc/core -Isrc/config -Isrc/crypto \
  -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui \
  -c src/ui/cheatsheet_dlg.c -o /dev/null && echo OK
```

Expected: `OK`. (On the MSYS2 box drop the vcpkg `-I`; the pacman headers are on gcc's default path.)

- [ ] **Step 4: Run the design-system gate and the suite**

Run: `make test 2>&1 | grep -E 'colour gate\] cheatsheet|ui_tokens_(colour|scale)_gate|Tests Run'`
Expected: `[PASS] test_ui_tokens_colour_gate`, `[PASS] test_ui_tokens_scale_gate`, `Failed: 0`. (`cheatsheet_dlg.c` has no `RGB(`, `MulDiv(` or `CreateFont`: `grep -nE 'RGB\(|MulDiv\(|CreateFont' src/ui/cheatsheet_dlg.c` prints nothing.)

- [ ] **Step 5: Commit**

```bash
git add src/ui/cheatsheet_dlg.h src/ui/cheatsheet_dlg.c
git status --short
git commit
```

Message: `feat(cheatsheet): the keyboard shortcuts sheet window`; commit with the repo's Models line and trailer.

---

### Task 5: Wiring: menu, commands and the demo state

**Files:**
- Modify: `tests/test_ui_demo.c` (`test_ui_demo_states_list_ends_in_all` near line 35; new test after `test_ui_demo_settings_debug_is_an_empty_state` near line 467)
- Modify: `src/core/ui_demo.c` (comment lines 8-15, `STATE_NAMES` lines 17-21, `ui_demo_build` near line 602)
- Modify: `src/core/ui_demo.h` (the state comment, ending near line 46)
- Modify: `tests/runner.c` (declaration and call next to `test_ui_demo_settings_debug_is_an_empty_state`)
- Modify: `src/ui/window.c`: includes (line 21), `WM_OPEN_DEMO_SETTINGS` (line 92), `menu_add_item` (2580-2601), `create_app_menu` (2652-2700), after `show_about_dialog` (ends near 2977), `create_demo_session` (near 1982), `WM_COMMAND` (near 3963 and 4062), the `WM_OPEN_DEMO_SETTINGS` handler (near 4459)

**Interfaces:**
- Consumes: `cheatsheet_show()` (Task 4), `shortcuts_menu_accel()` (Task 1), `IDM_FILE_CLOSE_TAB`, `IDM_HELP_SHORTCUTS` (Task 1), `close_active_tab()` (`window.c:3158`).
- Produces: `--ui-demo=shortcuts` (Task 8's gallery case launches it); `WM_COMMAND` 2033 opens the sheet and 2008 closes the active tab (Task 8 posts 2033); the File menu ids `2001, 2008, 0, 2002, ...` and Help ids `2033, 2029, 0, 2031, 2032, 0, 2030` (Task 8 asserts them).

- [ ] **Step 1: Write the failing demo-state tests**

In `tests/test_ui_demo.c`, replace the comment and body of `test_ui_demo_states_list_ends_in_all` (lines 30-51) with the version below. If the selection plan added a state, `count` is one more than the number the test reads now, and `"shortcuts"` still goes immediately before `"all"`: keep the shape, adjust the numbers.

```c
/* "all" stays last and still means the union of the panel states. "local"
 * (v1.2.1, spec 2026-09-22-local-shell-design.md section 5) only swaps the
 * terminal transcript; "thinking" is "chat" with a longer reasoning block;
 * "cmddetail", "settings-debug", "policy-auto", "policy-compact" and "ansi"
 * reuse or swap content too, and "shortcuts" (2026-10-20) opens the modal
 * keyboard shortcuts sheet. None of those is part of "all". */
int test_ui_demo_states_list_ends_in_all(void)
{
    TEST_BEGIN();
    int count = 0;
    const char *const *states = ui_demo_states(&count);
    ASSERT_NOT_NULL(states);
    ASSERT_EQ(count, 18);
    ASSERT_STR_EQ(states[count - 1], "all");
    ASSERT_STR_EQ(states[count - 2], "shortcuts");
    ASSERT_STR_EQ(states[count - 3], "policy-compact");
    ASSERT_STR_EQ(states[count - 4], "policy-auto");
    ASSERT_STR_EQ(states[count - 5], "settings-debug");
    ASSERT_STR_EQ(states[count - 6], "cmddetail");
    ASSERT_STR_EQ(states[count - 7], "thinking");
    ASSERT_STR_EQ(states[count - 8], "local");
    ASSERT_STR_EQ(states[count - 9], "ansi");
    TEST_END();
}
```

After the closing brace of `test_ui_demo_settings_debug_is_an_empty_state` add:

```c
/* "shortcuts" is a real state that builds an empty conversation (the win32
 * side opens Help > Keyboard Shortcuts over it) and is not part of "all". */
int test_ui_demo_shortcuts_is_an_empty_state(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char term_buf[8192];
    ASSERT_TRUE(ui_demo_state_valid("shortcuts"));
    ASSERT_EQ(ui_demo_build("shortcuts", &conv, &approval, &approval2,
                            term_buf, sizeof(term_buf)), 0);
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ(approval2.count, 0);
    ASSERT_TRUE(!ui_demo_scrolls_to_top("shortcuts"));
    ASSERT_TRUE(!ui_demo_state_valid("shortcut"));
    ASSERT_TRUE(!ui_demo_state_valid("Shortcuts"));
    TEST_END();
}
```

In `tests/runner.c` add `int test_ui_demo_shortcuts_is_an_empty_state(void);` after the declaration of `test_ui_demo_settings_debug_is_an_empty_state`, and `    failed += test_ui_demo_shortcuts_is_an_empty_state();` after `    failed += test_ui_demo_settings_debug_is_an_empty_state();`.

- [ ] **Step 2: Run them to verify they fail**

Run: `make test 2>&1 | grep -E -A2 'test_ui_demo_(states_list_ends_in_all|shortcuts_is)'`
Expected: `Assertion failed: count == 18 (17 != 18)` and `Assertion failed: ui_demo_state_valid("shortcuts")`, both `[FAIL]`.

- [ ] **Step 3: Add the state**

In `src/core/ui_demo.c`, replace

```c
 * always last and always means "union of the panel states" (not "ansi",
 * "local", "thinking", "cmddetail", "settings-debug", "policy-auto" or
 * "policy-compact", which reuse or swap content). "ansi" sits before
 * "local" so the names tests/test_ui_demo.c anchors from the end of the
 * list keep their positions.
```

with

```c
 * always last and always means "union of the panel states" (not "ansi",
 * "local", "thinking", "cmddetail", "settings-debug", "policy-auto",
 * "policy-compact" or "shortcuts", which reuse or swap content or open a
 * modal window). "ansi" sits before "local" so the names
 * tests/test_ui_demo.c anchors from the end of the list keep their
 * positions; "shortcuts" sits immediately before "all".
```

and in `STATE_NAMES` replace `"settings-debug", "policy-auto", "policy-compact", "all"` with `"settings-debug", "policy-auto", "policy-compact", "shortcuts", "all"`.

In `ui_demo_build`, after the comment block that ends `Settings window is modal and is not an AI panel state. */`, add:

```c
    /* "shortcuts" (2026-10-20) adds nothing either: the win32 side opens the
     * keyboard shortcuts sheet (Help > Keyboard Shortcuts) over the empty
     * demo session. Not part of "all": the sheet is modal. */
```

In `src/core/ui_demo.h`, replace

```c
 * white) and docks the panel at ui_demo_panel_width_px(); the conversation
 * is the system message only. Not part of "all". */
```

with

```c
 * white) and docks the panel at ui_demo_panel_width_px(); the conversation
 * is the system message only. Not part of "all".
 *
 * "shortcuts" (2026-10-20, keyboard cheat sheet) builds an empty
 * conversation; the win32 side opens Help > Keyboard Shortcuts over it.
 * Not part of "all": the sheet is modal, like Settings in "settings-debug". */
```

- [ ] **Step 4: Run the demo tests to verify they pass**

Run: `make test 2>&1 | grep -E '^\[(PASS|FAIL)\] test_ui_demo|Tests Run'`
Expected: every `test_ui_demo_*` line `[PASS]`, `Failed: 0`.

- [ ] **Step 5: Wire the window**

In `src/ui/window.c`:

(a) Replace `#include "help_guide.h"` with

```c
#include "help_guide.h"
#include "cheatsheet_dlg.h"
#include "shortcuts.h"
```

(b) After `#define WM_OPEN_DEMO_SETTINGS   (WM_USER + 5)` add the line below. First run `grep -n 'WM_USER + ' src/ui/window.c`; if `WM_USER + 6` is taken (the selection plan may have added a message), use the next free number.

```c
/* --ui-demo=shortcuts: posted once the demo session exists, to open the
 * keyboard shortcuts sheet over it. */
#define WM_OPEN_DEMO_SHORTCUTS  (WM_USER + 6)
```

(c) Replace the whole of `menu_add_item` (the comment line `/* Helper: add an owner-draw item to a menu */` through its closing brace) with:

```c
/* Helper: add an owner-draw item to a menu. The accelerator text is the
 * shortcuts table's (src/core/shortcuts.c), never part of the label, so a
 * menu cannot name a chord the cheat sheet and README do not. */
static void menu_add_item(HMENU menu, UINT id, const char *label)
{
    MenuItemData *mi = alloc_menu_item();
    mi->id   = id;
    mi->hSub = NULL;
    mi->is_separator = 0;
    mi->nested = 0;
    (void)snprintf(mi->text, sizeof(mi->text), "%s", label);
    (void)snprintf(mi->accel, sizeof(mi->accel), "%s",
                   shortcuts_menu_accel((int)id));
    AppendMenu(menu, MF_OWNERDRAW, id, (LPCSTR)mi);
}
```

(d) In `create_app_menu`, drop every literal accelerator and add the two items. Each line on the left becomes the line(s) on the right; if the selection plan changed an accelerator after `\t` (for example `"Copy\tCtrl+Shift+C"`), the label still becomes the text before `\t`:

| main | becomes |
|---|---|
| `menu_add_item(hFile, IDM_FILE_NEW_SESSION, "New Session\tCtrl+Shift+T");` | `menu_add_item(hFile, IDM_FILE_NEW_SESSION, "New Session");` then `menu_add_item(hFile, IDM_FILE_CLOSE_TAB, "Close Tab");` |
| `menu_add_item(hEdit, IDM_EDIT_COPY, "Copy\tCtrl+C");` | `menu_add_item(hEdit, IDM_EDIT_COPY, "Copy");` |
| `menu_add_item(hEdit, IDM_EDIT_PASTE, "Paste\tCtrl+V");` | `menu_add_item(hEdit, IDM_EDIT_PASTE, "Paste");` |
| `menu_add_item(hView, IDM_VIEW_AI_CHAT, "AI Assist\tCtrl+Shift+Space");` | `menu_add_item(hView, IDM_VIEW_AI_CHAT, "AI Assist");` |
| `menu_add_item(hView, IDM_VIEW_FULLSCREEN, "Fullscreen\tF11");` | `menu_add_item(hView, IDM_VIEW_FULLSCREEN, "Fullscreen");` |
| `menu_add_item(hHelp, IDM_HELP_GUIDE, "User Guide");` (first Help item) | `menu_add_item(hHelp, IDM_HELP_SHORTCUTS, "Keyboard Shortcuts...");` then `menu_add_item(hHelp, IDM_HELP_GUIDE, "User Guide");` |
| `menu_add_item(hHelp, IDM_HELP_MARK_ISSUE, "Mark Issue\tCtrl+Shift+M");` | `menu_add_item(hHelp, IDM_HELP_MARK_ISSUE, "Mark Issue");` |

The pool holds 64 items (`MAX_MENU_ITEMS`); the menu uses 55 after this change.

(e) Immediately before the line `/* ---- Keyboard: what each key sends (special-keys spec, 2026-09-23) -------` (just after `show_about_dialog`), add:

```c
/* Help > Keyboard Shortcuts (and --ui-demo=shortcuts): the modal sheet,
 * owned by the main window, with the floating AI panel disabled too. */
static void show_shortcuts(HWND hwnd)
{
    HWND floating = (!g_ai_docked && g_hwndAiChat && IsWindow(g_hwndAiChat))
                        ? g_hwndAiChat : NULL;
    cheatsheet_show(hwnd, floating, g_theme);
}
```

(f) In `create_demo_session`, replace

```c
    if (strcmp(state, "settings-debug") == 0)
        PostMessage(hwnd, WM_OPEN_DEMO_SETTINGS, 0, 0);
```

with

```c
    if (strcmp(state, "settings-debug") == 0)
        PostMessage(hwnd, WM_OPEN_DEMO_SETTINGS, 0, 0);
    /* --ui-demo=shortcuts: the keyboard shortcuts sheet over the demo
     * session, posted for the same reason. */
    if (strcmp(state, "shortcuts") == 0)
        PostMessage(hwnd, WM_OPEN_DEMO_SHORTCUTS, 0, 0);
```

(g) In the main window's `WM_COMMAND` switch, after

```c
                case IDM_FILE_NEW_SESSION:
                    on_tab_new();
                    return 0;
```

add

```c
                case IDM_FILE_CLOSE_TAB:
                    close_active_tab();
                    return 0;
```

and before `                case IDM_HELP_GUIDE:` add

```c
                case IDM_HELP_SHORTCUTS:
                    show_shortcuts(hwnd);
                    return 0;
```

(h) After the handler

```c
        case WM_OPEN_DEMO_SETTINGS:
            settings_dlg_show_demo_debug(hwnd, g_config);
            return 0;
```

add

```c

        case WM_OPEN_DEMO_SHORTCUTS:
            show_shortcuts(hwnd);
            return 0;
```

Check that no literal accelerator is left: `grep -n 'menu_add_item(.*\\t' src/ui/window.c` prints nothing.

- [ ] **Step 6: Compile window.c the way the Windows build will**

```bash
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow \
  -Wformat=2 -Wconversion -Os -Isrc -Isrc/core -Isrc/config -Isrc/crypto \
  -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui \
  -c src/ui/window.c -o /dev/null && echo OK
```

Expected: `OK`.

- [ ] **Step 7: Run the whole suite**

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`.

- [ ] **Step 8: Commit**

```bash
git add src/ui/window.c src/core/ui_demo.c src/core/ui_demo.h tests/test_ui_demo.c tests/runner.c
git status --short
git commit
```

Message: `feat(menu): Help > Keyboard Shortcuts, File > Close Tab, accelerators from the table`; commit with the repo's Models line and trailer.

---

### Task 6: User Guide and documentation

**Files:**
- Modify: `src/ui/help_guide.c` (part 2 lines 127-142, part 5 lines 327-328, part 6 lines 342 and 352-375; part 1 lines 99-102 only if still stale)
- Modify: `README.md` (directory tree, near lines 481, 505 and 541)
- Modify: `CLAUDE.md` ("Visual sanity check", lines 150-154)
- Modify: `docs/superpowers/specs/2026-09-09-integration-coverage.md` (rows 34 and 38, lines 78 and 87)
- Modify: `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` (lines 47-48; the end of the "Open, in priority order" list)

**Interfaces:**
- Consumes: the menu item "Help > Keyboard Shortcuts" (Task 5) that the new text points to; the file names from Tasks 1, 3 and 4.
- Produces: nothing code depends on.

- [ ] **Step 1: Replace the User Guide's two shortcut lists with a pointer**

In `src/ui/help_guide.c`, `GUIDE_PART_2`: replace the key list from `"Most keys reach the shell the way xterm sends them:\r\n"` through `"    see KEYBOARD SHORTCUTS below for what moved to Ctrl+Shift\r\n"` (16 lines, main's 127-142) with the single line

```c
"See Help > Keyboard Shortcuts for the full list.\r\n"
```

so the part starts:

```c
static const char GUIDE_PART_2[] =
"SPECIAL KEYS\r\n"
"------------\r\n"
"\r\n"
"See Help > Keyboard Shortcuts for the full list.\r\n"
"\r\n"
"Edit > Send Key sends a key Nutshell or Windows would\r\n"
```

In `GUIDE_PART_6`: replace the KEYBOARD SHORTCUTS section's list and its closing note, from `"  Ctrl+Shift+T     New session / Session Manager\r\n"` through `"or Windows would otherwise keep.\r\n"`, with the same single line, so the section reads:

```c
"KEYBOARD SHORTCUTS\r\n"
"------------------\r\n"
"\r\n"
"See Help > Keyboard Shortcuts for the full list.\r\n"
"\r\n"
"\r\n"
"CONFIGURATION FILE\r\n"
```

- [ ] **Step 2: Correct the AES-256-GCM text to DPAPI**

In `GUIDE_PART_5` replace

```c
"  Enter your password in the Session Manager. Passwords are\r\n"
"  encrypted at rest using AES-256-GCM in the config file.\r\n"
```

with

```c
"  Enter your password in the Session Manager. Passwords are\r\n"
"  encrypted at rest in the config file with Windows DPAPI,\r\n"
"  tied to your Windows user and this PC.\r\n"
```

In `GUIDE_PART_6` replace

```c
"  - Passwords and API keys are encrypted at rest (AES-256-GCM)\r\n"
```

with

```c
"  - Passwords and API keys are encrypted at rest with Windows\r\n"
"    DPAPI, tied to your Windows user and this PC\r\n"
```

- [ ] **Step 3: Part 1, only if the selection plan left it stale**

Run: `grep -n 'Ctrl+C copies the selection' src/ui/help_guide.c`. If it prints a line (part 1's COPY & PASTE block still describes the old Ctrl+C), replace these four lines

```c
"  - Select text: Click and drag in the terminal\r\n"
"  - Copy:        Ctrl+C copies the selection and clears it;\r\n"
"                 with no selection, Ctrl+C sends SIGINT as usual\r\n"
"  - Copy always: Ctrl+Shift+C (never sent to the shell)\r\n"
```

with

```c
"  - Select text: drag in the terminal; it is copied when you\r\n"
"                 let go. Double-click a word, triple-click a line\r\n"
"  - Copy again:  Ctrl+Shift+C. Ctrl+C always interrupts\r\n"
```

If it prints nothing, leave part 1 alone.

- [ ] **Step 4: Compile the guide**

```bash
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow \
  -Wformat=2 -Wconversion -Os -Isrc -Isrc/core -Isrc/config -Isrc/crypto \
  -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui \
  -c src/ui/help_guide.c -o /dev/null && echo OK
grep -c 'See Help > Keyboard Shortcuts for the full list' src/ui/help_guide.c
grep -n 'AES-256-GCM\|KEYBOARD SHORTCUTS below' src/ui/help_guide.c
```

Expected: `OK`; `2`; the last grep prints nothing. (Each part must stay under the 4,095-character literal limit `-Wpedantic` enforces; these edits shrink parts 2 and 6 and grow part 5 by about 40 characters, from 2,694.)

- [ ] **Step 5: README directory tree**

In README's `## Directory Structure` tree add three lines, keeping the column alignment of their neighbours:

after `│   │   ├── chat_thinking.c/.h         #   Thinking/reasoning display state`:

```text
│   │   ├── cheatsheet_layout.c/.h     #   Keyboard shortcuts sheet layout (columns, scroll, placement; pure)
```

after `│   │   ├── shell_prompt.c/.h          #   Detects a shell waiting at a prompt (gates one-at-a-time dispatch)`:

```text
│   │   ├── shortcuts.c/.h             #   Every shortcut, once: the sheet, the menu accelerators and README's block
```

after `│       ├── help_guide.c/.h           #   Help guide dialog`:

```text
│       ├── cheatsheet_dlg.c/.h       #   Help > Keyboard Shortcuts sheet
```

Run: `make test 2>&1 | grep -E 'readme_matches_table|Tests Run'`
Expected: `[PASS] test_shortcuts_readme_matches_table` (the tree is outside the markers), `Failed: 0`.

- [ ] **Step 6: CLAUDE.md's visual check names the new state**

In `CLAUDE.md`, replace

```markdown
  running the integration suite's `ui_gallery` case, which screenshots the
  same states across all four themes into `tests\integration\artifacts\gallery\`.
```

with

```markdown
  running the integration suite's `ui_gallery` case, which screenshots the
  same states across all four themes into `tests\integration\artifacts\gallery\`.
  `--ui-demo=shortcuts` (not part of `all`: the sheet is modal) opens
  Help > Keyboard Shortcuts over the demo window; the harness case
  `help_shortcuts_sheet_gallery` captures it on all four themes.
```

- [ ] **Step 7: The coverage spec's chords that do not exist**

In `docs/superpowers/specs/2026-09-09-integration-coverage.md`, row 34: replace the last cell `TERM-4: zoom in twice, \`tput cols\` drops, zoom reset restores |` with

```markdown
TERM-4: zoom in twice, `tput cols` drops, zoom reset restores. Note (2026-10-20): Ctrl+0 does not exist; there is no zoom-reset chord (Help > Keyboard Shortcuts lists every chord) |
```

Row 38: replace its last cell `TABS-1: open two, switch both ways, close the active one, the other becomes active, close-last behaviour |` with

```markdown
TABS-1: open two, switch both ways, close the active one, the other becomes active, close-last behaviour. Note (2026-10-20): Ctrl+Tab does not exist and plain Ctrl+W goes to the shell; tabs switch by click and close with × or Ctrl+Shift+W (File > Close Tab) |
```

- [ ] **Step 8: The notes document**

In `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` replace

```markdown
6. **Plain chrome** — classic text menu bar above the tab strip; shortcuts (Ctrl+W,
   Ctrl+=/-, PgUp/PgDn, Shift+Insert) never shown in the UI; no accelerator table.
```

with

```markdown
6. **Plain chrome** — classic text menu bar above the tab strip; shortcuts (Ctrl+W,
   Ctrl+=/-, PgUp/PgDn, Shift+Insert) never shown in the UI; no accelerator table.
   (Corrected 2026-10-20: Help > Keyboard Shortcuts now shows every chord, from the
   one table in `src/core/shortcuts.c` that also labels the menus; there is still no
   Win32 accelerator table, and plain Ctrl+W goes to the shell.)
```

At the end of the "Open, in priority order:" list (after its last `- [ ]` item, before the blank line that precedes `Process (CLAUDE.md):`), add:

```markdown
- [ ] Chords asked about that do not exist: Ctrl+Tab (switch tabs), Ctrl+0 (zoom reset),
      approve/deny keys in the AI panel. The cheat-sheet spec (2026-10-20, "Not changed")
      left them out; decide whether any is wanted, then add it to `src/core/shortcuts.c`.
- [ ] Main-window chords (Ctrl+Shift+T/W/M, F11, Ctrl+Shift+Space) fire only while the main
      window has focus; from the AI panel's input they are dead. The cheat sheet states the
      condition per row ("from the terminal"); forwarding them is a separate change.
```

- [ ] **Step 9: Commit**

```bash
git add src/ui/help_guide.c README.md CLAUDE.md docs/superpowers/specs/2026-09-09-integration-coverage.md docs/superpowers/specs/2026-09-06-ui-redesign-notes.md
git status --short
git commit
```

Message: `docs: point the User Guide at the shortcuts sheet; DPAPI text; coverage and notes corrections`; commit with the repo's Models line and trailer.

---

### Task 7: README.md joins the native suite's path filter (a gate change)

This changes what the merge gate runs, so it is its own commit and gets its own line in the pull request body (spec 2.4).

**Files:**
- Modify: `.github/workflows/checks.yml` (comment lines 12-16, comment lines 55-60, the regex on line 71)
- Modify: `CLAUDE.md` ("Branches, pull requests and the merge gate", lines 192-198)
- Modify: `.claude/memory/MEMORY.md` (line 8 "Last updated", "The merge gate, precisely" first bullet, lines 95-103)
- Modify: `.claude/CHANGELOG.md` (new entry at the top, above `## 2026-10-19 — retrospective (11 merges)`)

**Interfaces:**
- Consumes: `test_shortcuts_readme_matches_table` (Task 2), which makes a README-only change able to fail `make test`.
- Produces: `Native tests` runs on a pull request that changes `README.md`.

- [ ] **Step 1: Show the filter misses README today (the failing check)**

```bash
re=$(sed -n "s/.*grep -qE '\(.*\)'; then/\1/p" .github/workflows/checks.yml)
echo "$re"
printf 'README.md\n' | grep -qE "$re" && echo code || echo skipped
```

Expected: the regex without `README`, then `skipped`.

- [ ] **Step 2: Add README.md to the filter and its comments**

In `.github/workflows/checks.yml` replace the regex on line 71

```yaml
          if printf '%s\n' "$changed" | grep -qE '^(src/|tests/|build-support/|Makefile$|nutshell\.rc$|\.github/workflows/checks\.yml$)'; then
```

with

```yaml
          if printf '%s\n' "$changed" | grep -qE '^(src/|tests/|build-support/|Makefile$|nutshell\.rc$|README\.md$|\.github/workflows/checks\.yml$)'; then
```

Replace the header comment lines

```yaml
#                      it came from the committed sources. Since 2026-10-13 a
#                      pull request that changes no code (docs/, .claude/,
#                      README, ...) skips the compile and the suite but still
#                      reports green -- see the "scope" step for why the
#                      trigger has no paths filter.
```

with

```yaml
#                      it came from the committed sources. Since 2026-10-13 a
#                      pull request that changes no code (docs/, .claude/,
#                      ...) skips the compile and the suite but still
#                      reports green -- see the "scope" step for why the
#                      trigger has no paths filter. README.md counts as code
#                      since 2026-10-20: the suite compares its generated
#                      shortcuts block with src/core/shortcuts.c.
```

Replace the scope step's comment lines

```yaml
          # A docs-only pull request has nothing to compile, so the three
          # steps below are skipped when nothing under src/, tests/,
          # build-support/, the Makefile, nutshell.rc or this workflow
          # changed. tests/ counts as code (a test-only change still has to
          # compile the runner) and so does this file (the gate must test
          # itself). The skip lives here, inside the job, and NOT as a
```

with

```yaml
          # A docs-only pull request has nothing to compile, so the three
          # steps below are skipped when nothing under src/, tests/,
          # build-support/, the Makefile, nutshell.rc, README.md or this
          # workflow changed. tests/ counts as code (a test-only change still
          # has to compile the runner), so does README.md (since 2026-10-20
          # tests/test_shortcuts.c compares its "Keyboard Shortcuts" block
          # with src/core/shortcuts.c byte for byte) and so does this file
          # (the gate must test itself). The skip lives here, inside the job,
          # and NOT as a
```

The next comment line (`# paths-ignore on the trigger: this job is a required status`) continues the sentence unchanged.

- [ ] **Step 3: Run the check again**

```bash
re=$(sed -n "s/.*grep -qE '\(.*\)'; then/\1/p" .github/workflows/checks.yml)
for f in README.md docs/x.md .claude/memory/MEMORY.md src/core/shortcuts.c tests/runner.c Makefile nutshell.rc .github/workflows/checks.yml README.md.bak; do
  printf '%s\n' "$f" | grep -qE "$re" && echo "code    $f" || echo "skipped $f"
done
python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/checks.yml')); print('yaml ok')" 2>/dev/null || echo "(no PyYAML here: read the diff instead)"
```

Expected: `code` for README.md, src/core/shortcuts.c, tests/runner.c, Makefile, nutshell.rc and checks.yml; `skipped` for docs/x.md, .claude/memory/MEMORY.md and README.md.bak; `yaml ok` or the note.

- [ ] **Step 4: CLAUDE.md's gate section**

Replace

```markdown
committed files. On a pull request that changes nothing under `src/`,
`tests/`, `build-support/`, the Makefile, `nutshell.rc` or the workflow
itself (docs, `.claude/`, README) it skips the compile and the suite but
still reports green, so the ruleset is satisfied; the skip is a step
condition inside the job, never a `paths` filter on the trigger, because a
required check that never reports blocks the pull request forever.
```

with

```markdown
committed files. On a pull request that changes nothing under `src/`,
`tests/`, `build-support/`, the Makefile, `nutshell.rc`, `README.md` or the
workflow itself (docs, `.claude/`) it skips the compile and the suite but
still reports green, so the ruleset is satisfied; the skip is a step
condition inside the job, never a `paths` filter on the trigger, because a
required check that never reports blocks the pull request forever.
`README.md` counts as code because `tests/test_shortcuts.c` compares its
generated "Keyboard Shortcuts" block with `src/core/shortcuts.c` byte for
byte (since 2026-10-20).
```

- [ ] **Step 5: Project memory and its changelog entry**

In `.claude/memory/MEMORY.md`, replace `Last updated: 2026-10-19 (retrospective).` with `Last updated: 2026-10-20 (README joins the native-suite path filter).`, and in "## The merge gate, precisely", after the first bullet (the one ending `be strictly greater than the base's and the exe to be in the diff.`), add:

```markdown
- `Native tests` skips its compile and suite (still reporting green) when
  a pull request changes nothing under `src/`, `tests/`, `build-support/`,
  the Makefile, `nutshell.rc`, `README.md` or `checks.yml`. `README.md`
  joined the list on 2026-10-20: `tests/test_shortcuts.c` compares its
  generated block between `<!-- shortcuts:begin -->` and
  `<!-- shortcuts:end -->` with `src/core/shortcuts.c` byte for byte, and
  fails rather than skips under `CI` when README.md cannot be read. The
  regeneration recipe is in that test file.
```

In `.claude/CHANGELOG.md`, insert above `## 2026-10-19 — retrospective (11 merges)`:

```markdown
## 2026-10-20 — README.md joins the native suite's path filter

- **`memory/MEMORY.md`, "The merge gate, precisely".** `Native tests` now
  compiles and runs the suite when a pull request changes `README.md`, as
  well as `src/`, `tests/`, `build-support/`, the Makefile, `nutshell.rc`
  and `checks.yml`: `tests/test_shortcuts.c` compares README's generated
  "Keyboard Shortcuts" block with `src/core/shortcuts.c` byte for byte, so
  a README-only edit can now fail the suite. The same change is in
  `.github/workflows/checks.yml` (the `scope` step and its comments) and in
  CLAUDE.md's gate section. Spec:
  `docs/superpowers/specs/2026-10-20-keyboard-cheat-sheet-design.md`
  section 2.4. "Last updated" moved to 2026-10-20.

```

End the entry with one more line: the exact `Models:` line you put in this commit's message (the other entries carry theirs the same way).

- [ ] **Step 6: Commit**

```bash
git add .github/workflows/checks.yml CLAUDE.md .claude/memory/MEMORY.md .claude/CHANGELOG.md
git status --short
git commit
```

Message: `ci: run the native suite when README.md changes (its shortcuts block is tested)`; commit with the repo's Models line and trailer. For the pull request body, carry this line: "Gate change: `Native tests` now runs on README.md changes, because `tests/test_shortcuts.c` compares README's generated shortcuts block with the table."

---

### Task 8: Harness cases

The integration harness is a manual tool run on the Windows box (CLAUDE.md "Integration Harness"); these cases are written here and run after Task 9's build. Every case launches and stops its own scratch copy of the exe; none needs the foreground.

**Files:**
- Modify: `tests/integration/NutshellIT.psm1` (P/Invoke block after line 93; `$script:DIALOG_CLASSES` and its comment, lines 501-505)
- Modify: `tests/integration/cases/50-window.ps1` (header lines 1-2; `menus_open_and_list_items` lines 466-470 and 495; three new cases at the end of the file)
- Modify: `tests/integration/cases/30-ui-demo.ps1` (the `$galleryStates` comment, lines 16-23)
- Modify: `tests/integration/README.md` (the `cases\50-window.ps1` row, line 40)

**Interfaces:**
- Consumes: class `Nutshell_Shortcuts`, title `Keyboard Shortcuts`, `WM_COMMAND` 2033 (Tasks 4-5); `--ui-demo=shortcuts` (Task 5); scrollbar class `Nutshell_CustomScrollbar` (`custom_scrollbar.h`); helpers `Wait-NutshellDialog`, `Save-NutshellScreenshot -Hwnd`, `Get-NutshellWindows`, `Get-NutshellWorkArea`, `Invoke-Case`, `Assert-True`, `Test-NutshellCaptureNonBlank`, `ConvertTo-NutshellFileToken`.
- Produces: cases `help_shortcuts_sheet_opens_and_closes`, `help_shortcuts_sheet_fits_the_screen`, `help_shortcuts_sheet_gallery` (gate tier).

- [ ] **Step 1: Teach the module the new dialog class and two P/Invokes**

In `tests/integration/NutshellIT.psm1`, after

```csharp
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
```

add

```csharp
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
```

Replace

```powershell
# real dialogs (class #32770); Settings, the paste preview, the passphrase
# prompt and About are hand-built popup windows with their own classes.
$script:DIALOG_CLASSES = @("#32770", "NutshellPassDlg", "Nutshell_Settings", "Nutshell_PastePreview", "Nutshell_About")
```

with

```powershell
# real dialogs (class #32770); Settings, the paste preview, the passphrase
# prompt, About and the keyboard shortcuts sheet are hand-built popup windows
# with their own classes.
$script:DIALOG_CLASSES = @("#32770", "NutshellPassDlg", "Nutshell_Settings", "Nutshell_PastePreview", "Nutshell_About", "Nutshell_Shortcuts")
```

- [ ] **Step 2: The menu case's id lists and detail string**

In `tests/integration/cases/50-window.ps1`, `menus_open_and_list_items`: replace

```powershell
            0 = @(2001, 0, 2002, 2003, 0, 2004, 2005, 0, 2007, 0, 2006)  # File
```

with

```powershell
            0 = @(2001, 2008, 0, 2002, 2003, 0, 2004, 2005, 0, 2007, 0, 2006)  # File: new session, close tab, ...
```

replace

```powershell
            3 = @(2029, 0, 2031, 2032, 0, 2030)                           # Help: guide, mark issue, log folder, about
```

with

```powershell
            3 = @(2033, 2029, 0, 2031, 2032, 0, 2030)                     # Help: shortcuts, guide, mark issue, log folder, about
```

and in its `$detail` string replace `File 11 items, Edit 6 (incl. Send Key popup), View 3, Help 3;` with `File 12 items, Edit 6 (incl. Send Key popup), View 3, Help 7;`.

Change the file's first two lines to:

```powershell
# 50-window.ps1 -- launch, window, shutdown, menu (integration-coverage.md section 1):
# LAUNCH-1..3, RESIZE-1, WINDOW-1..2, CLOSE-1, MENU-1, and HELP-1 (the keyboard shortcuts sheet).
```

- [ ] **Step 3: The three cases**

Append to `tests/integration/cases/50-window.ps1`:

```powershell
# ---- HELP-1: Help > Keyboard Shortcuts (cheat-sheet spec section 3.2) ----------
# The sheet is custom-painted (class Nutshell_Shortcuts, title "Keyboard
# Shortcuts"), so it is captured by its own handle and keys are posted to that
# handle. -nc: no SSH host needed. 2033 is IDM_HELP_SHORTCUTS (resource.h).
$IDM_HELP_SHORTCUTS = 2033
$WM_KEYDOWN_SHEET   = 0x0100
$VK_ESCAPE_SHEET    = 0x1B

function Open-NutshellShortcutsSheet {
    <# Post Help > Keyboard Shortcuts to the main window and return the sheet's
       HWND; throws if no sheet appears. #>
    param([Parameter(Mandatory)] $Session)
    [NutshellNative]::PostMessage($Session.Main, $WM_COMMAND, [IntPtr]$IDM_HELP_SHORTCUTS, [IntPtr]::Zero) | Out-Null
    $dlg = Wait-NutshellDialog -Session $Session -Title "^Keyboard Shortcuts$" -TimeoutSec 10
    Assert-True ($dlg -ne [IntPtr]::Zero) "Help > Keyboard Shortcuts opened no Nutshell_Shortcuts window"
    Start-Sleep -Milliseconds 400   # let the first paint land before a capture
    return $dlg
}

function Get-NutshellShortcutsSheets {
    param([Parameter(Mandatory)] $Session)
    return @(Get-NutshellWindows -Session $Session | Where-Object { ($_ -split "`t")[1] -eq "Nutshell_Shortcuts" })
}

Invoke-Case -Name "help_shortcuts_sheet_opens_and_closes" -Settings @{} -ExtraArgs @("-nc") -Body {
    param($s)
    $dlg = Open-NutshellShortcutsSheet -Session $s
    $path = Join-Path $Artifacts "help_shortcuts_sheet.png"
    Save-NutshellScreenshot -Session $s -Path $path -Hwnd ([long]$dlg) | Out-Null
    Assert-True (Test-NutshellCaptureNonBlank -Path $path) "the sheet's capture looks blank: $path"
    Assert-True (-not [NutshellNative]::IsWindowEnabled($s.Main)) "the main window stayed enabled under the modal sheet"

    # A second Help > Keyboard Shortcuts while it is open opens nothing more.
    [NutshellNative]::PostMessage($s.Main, $WM_COMMAND, [IntPtr]$IDM_HELP_SHORTCUTS, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 800
    $n = (Get-NutshellShortcutsSheets -Session $s).Count
    Assert-True ($n -eq 1) "a second Help > Keyboard Shortcuts left $n sheets open, expected 1"

    # Esc posted to the sheet's own handle closes it.
    [NutshellNative]::PostMessage($dlg, $WM_KEYDOWN_SHEET, [IntPtr]$VK_ESCAPE_SHEET, [IntPtr]::Zero) | Out-Null
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline -and [NutshellNative]::IsWindow($dlg)) { Start-Sleep -Milliseconds 100 }
    Assert-True (-not [NutshellNative]::IsWindow($dlg)) "Esc posted to the sheet did not close it"
    Assert-True ([NutshellNative]::IsWindowEnabled($s.Main)) "the main window is still disabled after the sheet closed"
    "opened by WM_COMMAND 2033, captured by handle (non-blank), main window disabled while open; a second open ignored; Esc closed it; main window enabled again"
}

Invoke-Case -Name "help_shortcuts_sheet_fits_the_screen" -Settings @{} -ExtraArgs @("-nc") -Body {
    param($s)
    $dlg = Open-NutshellShortcutsSheet -Session $s
    $r = New-Object NutshellNative+RECT
    [NutshellNative]::GetWindowRect($dlg, [ref]$r) | Out-Null
    $wa = Get-NutshellWorkArea
    $dpi = [int][NutshellNative]::GetDpiForWindow($dlg)
    $inside = ($r.L -ge $wa.X) -and ($r.T -ge $wa.Y) -and ($r.R -le ($wa.X + $wa.Width)) -and ($r.B -le ($wa.Y + $wa.Height))
    Assert-True $inside ("sheet {0},{1}-{2},{3} leaves the work area {4},{5} {6}x{7} at {8} DPI" -f $r.L, $r.T, $r.R, $r.B, $wa.X, $wa.Y, $wa.Width, $wa.Height, $dpi)
    $bars = @([NutshellNative]::ListChildren($dlg) | Where-Object { ($_ -split "`t")[2] -eq "Nutshell_CustomScrollbar" })
    $how = if ($bars.Count -gt 0) { "scrolls" } else { "one page" }
    [NutshellNative]::PostMessage($dlg, $WM_KEYDOWN_SHEET, [IntPtr]$VK_ESCAPE_SHEET, [IntPtr]::Zero) | Out-Null
    "sheet {0}x{1} inside the {2}x{3} work area at {4} DPI; {5}" -f ($r.R - $r.L), ($r.B - $r.T), $wa.Width, $wa.Height, $dpi, $how
}

# The sheet on all four themes, for the eye: --ui-demo=shortcuts opens it over
# the demo window, so no host and no config theme edit is needed. Captured by
# handle into artifacts\gallery\<Theme>-shortcuts.png beside ui_gallery's.
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "help_shortcuts_sheet_gallery")) {
    $name = "help_shortcuts_sheet_gallery"
    Write-Host ("[RUN ] " + $name)
    $galleryDir = Join-Path $Artifacts "gallery"
    New-Item -ItemType Directory -Force $galleryDir | Out-Null
    $themes = @("Onyx Synapse", "Onyx Light", "Sage & Sand", "Moss & Mist")
    $ok = $true
    $notes = New-Object System.Collections.ArrayList
    foreach ($theme in $themes) {
        $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
        $session = $null
        try {
            $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=shortcuts", "--theme", $theme)
            $dlg = Wait-NutshellDialog -Session $session -Title "^Keyboard Shortcuts$" -TimeoutSec 10
            Assert-True ($dlg -ne [IntPtr]::Zero) "--ui-demo=shortcuts opened no sheet"
            Start-Sleep -Milliseconds 400
            $path = Join-Path $galleryDir ("{0}-shortcuts.png" -f (ConvertTo-NutshellFileToken $theme))
            Save-NutshellScreenshot -Session $session -Path $path -Hwnd ([long]$dlg) | Out-Null
            Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank: $path"
            [void]$notes.Add("${theme}: ok")
        } catch {
            $ok = $false
            [void]$notes.Add("${theme}: " + $_.Exception.Message)
        } finally {
            if ($session) { Stop-Nutshell -Session $session }
            for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
                Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
                if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
            }
        }
    }
    $detail = "sheet captured by handle on 4 themes into artifacts\gallery\<Theme>-shortcuts.png -- " + ($notes -join "; ")
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}
```

- [ ] **Step 4: The gallery comment and the harness README**

In `tests/integration/cases/30-ui-demo.ps1`, replace

```powershell
# $galleryStates is a hand-kept copy of src/core/ui_demo.c's STATE_NAMES,
# minus "local" (tab-chrome-only, covered by its own case,
# 80-local-shell.ps1) -- there is no cheap way for a PowerShell script to
```

with

```powershell
# $galleryStates is a hand-kept copy of src/core/ui_demo.c's STATE_NAMES,
# minus "local" (tab-chrome-only, covered by its own case,
# 80-local-shell.ps1) and "shortcuts" (the modal sheet; this case captures
# only the main window, so 50-window.ps1's help_shortcuts_sheet_gallery
# captures the sheet by its handle) -- there is no cheap way for a PowerShell script to
```

In `tests/integration/README.md`, replace the row

```markdown
| `cases\50-window.ps1` | launch/window/shutdown/menu (`LAUNCH-*`, `RESIZE-1`, `WINDOW-*`, `CLOSE-1`, `MENU-1`) |
```

with

```markdown
| `cases\50-window.ps1` | launch/window/shutdown/menu (`LAUNCH-*`, `RESIZE-1`, `WINDOW-*`, `CLOSE-1`, `MENU-1`) and the keyboard shortcuts sheet (`help_shortcuts_sheet_opens_and_closes`, `help_shortcuts_sheet_fits_the_screen`, `help_shortcuts_sheet_gallery`) |
```

- [ ] **Step 5: Parse-check the scripts (Windows box, PowerShell 5.1)**

```powershell
foreach ($f in "tests\integration\NutshellIT.psm1", "tests\integration\cases\50-window.ps1", "tests\integration\cases\30-ui-demo.ps1") {
    $errs = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $f), [ref]$null, [ref]$errs)
    "{0}: {1} parse error(s)" -f $f, $errs.Count
}
```

Expected: `0 parse error(s)` for each. (No PowerShell on the Linux box: run this on Windows, or leave it to Step 6.)

- [ ] **Step 6: Run the cases (after Task 9's build, Windows box)**

```powershell
.\tests\integration\Run-Integration.ps1 -Tier gate -Only menus_open_and_list_items,help_shortcuts_sheet_opens_and_closes,help_shortcuts_sheet_fits_the_screen,help_shortcuts_sheet_gallery
```

Expected: four `[PASS]` lines; `help_shortcuts_sheet_fits_the_screen`'s detail records the DPI and whether the sheet scrolled; the five PNGs (`help_shortcuts_sheet.png`, `gallery\<Theme>-shortcuts.png`) show the sheet, two columns at 96-120 DPI on a 1080p screen. Look at them.

- [ ] **Step 7: Commit**

```bash
git add tests/integration/NutshellIT.psm1 tests/integration/cases/50-window.ps1 tests/integration/cases/30-ui-demo.ps1 tests/integration/README.md
git status --short
git commit
```

Message: `test(harness): keyboard shortcuts sheet cases; menu id lists with Close Tab and Keyboard Shortcuts`; commit with the repo's Models line and trailer.

---

### Task 9: Bump, build, verify (SKIPPABLE)

**Skip this task when executing this plan inside the two-plan run:** the orchestrator runs it once, after both this plan and the selection plan, so the branch gets one version and one committed exe. Run it only when this plan is executed on its own. It needs the MinGW cross-compiler and vcpkg (the Linux box `automaton`) or MSYS2 (`mingw32-make` there); a cloud session cannot do it (CLAUDE.md, MEMORY.md "Working from a cloud (Linux) session").

**Files:**
- Modify: `src/ui/resource.h` (`APP_VERSION`, `APP_VERSION_BINARY`, lines 4-5)
- Modify: `README.md` (line 5, `**Version**:`)
- Modify: `build/win/nutshell.exe` (rebuilt and committed)

**Interfaces:**
- Consumes: every earlier task.
- Produces: the committed exe whose FileVersion equals `APP_VERSION`, which the `Version bump` check reads.

- [ ] **Step 1: Compute the new version**

N is one patch above the larger of `origin/main`'s version and the branch's last build; the patch never passes 99 (after `1.x.99` comes `1.(x+1).0`).

```bash
git fetch origin main
main_v=$(git show origin/main:src/ui/resource.h | sed -n 's/^#define APP_VERSION *"\(.*\)"/\1/p')
branch_v=$(sed -n 's/^#define APP_VERSION *"\(.*\)"/\1/p' src/ui/resource.h)
next=$(python3 - "$main_v" "$branch_v" <<'PY'
import sys
a, b, c = max(tuple(int(x) for x in v.split('.')) for v in sys.argv[1:])
c += 1
if c > 99:
    b, c = b + 1, 0
print(f"{a}.{b}.{c}")
PY
)
echo "main $main_v, branch $branch_v -> $next"
```

Expected: one line naming the three versions; `$next` is strictly greater than both.

- [ ] **Step 2: Write it in all three places**

```bash
sed -i "s/^#define APP_VERSION        \".*\"/#define APP_VERSION        \"$next\"/" src/ui/resource.h
sed -i "s/^#define APP_VERSION_BINARY  .*/#define APP_VERSION_BINARY  ${next//./,},0/" src/ui/resource.h
sed -i "s/^\*\*Version\*\*: v[0-9.]*/**Version**: v$next/" README.md
sed -n 4,5p src/ui/resource.h; sed -n 5p README.md
```

Expected: `#define APP_VERSION        "<next>"`, `#define APP_VERSION_BINARY  <a>,<b>,<c>,0`, `**Version**: v<next> \`.

- [ ] **Step 3: Build**

```bash
make clean && make release
strings -el build/win/nutshell.exe | grep 1.2.
```

(MSYS2: `mingw32-make clean && mingw32-make release`.) Expected: the build ends without an error, and `strings` prints the new version (FileVersion and ProductVersion); once the minor has moved past 2, use `grep -F "$next"` instead.

- [ ] **Step 4: Verify**

```bash
make test 2>&1 | tail -1
.github/scripts/check-version.sh origin/main
```

Expected: `Tests Run: N, Failed: 0`; the script reports the version bumped, README current, and the exe's version equal to `APP_VERSION`, and exits 0.

Then the eye check on Windows: `nutshell.exe --ui-demo=shortcuts --theme "Onyx Light"` (and one dark theme) shows the sheet over the demo window: seven sections, 1-4 left and 5-7 right at 96-120 DPI, conditions in the dim caption font on their own line, the scrollbar only when the page is taller than the screen, Close below the scrolled area; Esc, Enter and Close close it; Help menu shows "Keyboard Shortcuts..." first; File shows "Close Tab  Ctrl+Shift+W"; Edit > Copy shows `Ctrl+Shift+C`. Drag the sheet to a monitor at another DPI: it re-lays-out and stays on that monitor. Then run Task 8 Step 6.

- [ ] **Step 5: Commit**

```bash
git add src/ui/resource.h README.md build/win/nutshell.exe
git status --short
git commit
```

Message: `build: v<next>` (the computed number); commit with the repo's Models line and trailer. Do not open, ready or merge a pull request from a cloud session (CLAUDE.md "Branches, pull requests and the merge gate").

---

## Self-review (writing-plans checklist, done while writing)

**1. Spec coverage.**

| Spec | Task |
|---|---|
| 1 Behaviour: Help menu item, modal, themed, custom-painted; two columns or one; scrollbar only when too tall; Esc, Enter, Close | 3, 4, 5 |
| 1 Sections and their order; 1-4 left, 5-7 right | 1 (order), 3 (split) |
| 1 Row anatomy: chord, action, condition line in caption/`text_dim`; alternatives joined with `, `, menu rows single; menu path rows; "US layout" | 1, 4 |
| 1 Contents, row by row, with † rows | 1 (50 rows, `SC_SELECTION_SPEC`) |
| 1 One source of truth: sheet, menu labels, README block, guide pointer | 1, 2, 4, 5, 6 |
| 2.1 Table API and README markers; chord grammar as amended in 38d8722 (`test_shortcuts_chord_grammar`) | 1, 2 |
| 2.2 Layout inputs/outputs, rules, spacing, scroll, centring and clamping | 3 |
| 2.3 Window: class, colours, fonts, Close, modality, `WM_QUIT`, `WM_DPICHANGED`, second open ignored | 4 |
| 2.4 `resource.h` ids | 1 |
| 2.4 `create_app_menu`, accelerators from the table, `WM_COMMAND` cases | 5 |
| 2.4 `ui_demo` state, `test_ui_demo`, comment at `ui_demo.c:12`, gallery list untouched | 5, 8 (comment only) |
| 2.4 `help_guide.c` edits, AES text | 6 |
| 2.4 README block, prose cut, ":121 and :355 lose Ctrl+C copies" | 2 |
| 2.4 CI path filter, workflow comments, CLAUDE.md, MEMORY.md, CHANGELOG, PR-body line | 7 |
| 2.4 coverage spec Ctrl+0/Ctrl+Tab notes; notes document correction; open-list items | 6 |
| 2.4 harness id lists, detail strings, `tests/integration/README.md` | 8 |
| 3.1 `test_shortcuts.c` (every rule listed, README verbatim, CRLF, CI) | 1, 2 |
| 3.1 `test_cheatsheet_layout.c` (columns, scrolls, one column at 192, no overlap, 2x within rounding, action width, first DPI printed) and `cheatsheet_scroll_clamp` | 3 (the 96-DPI "no scrollbar" expectation is reported, see "Spec conflicts" 1) |
| 3.2 the three harness cases and `DIALOG_CLASSES` | 8 |
| 4 approved defaults: full list with scrolling; no chord; Mouse stays | 1, 3 |

No requirement is without a task; the deviations are listed in "Spec conflicts and interpretations".

**2. Placeholder scan.** Every code step carries the code; every command has its expected output. The only values computed at execution time are the version number (Task 9 Step 1 computes it) and the `Models:` line, which CLAUDE.md defines per commit.

**3. Type and name consistency.** `Shortcut`, `SC_*`, `shortcuts_table/sections/menu_accel/readme_block`, `SHORTCUTS_README_BEGIN/END` (Task 1) are used unchanged in Tasks 2-5. `CsRect`, `CsInput`, `CsLayout`, `CS_MAX_SECTIONS`, `CS_MAX_ROWS`, `CS_LEFT_SECTIONS`, `cheatsheet_layout_compute`, `cheatsheet_scroll_clamp`, `cheatsheet_place` (Task 3) match Task 4. `cheatsheet_show(HWND, HWND, const ThemeColors *)` (Task 4) matches Task 5's `show_shortcuts`. `IDM_FILE_CLOSE_TAB` 2008 and `IDM_HELP_SHORTCUTS` 2033 (Task 1) match Tasks 5 and 8. Class `Nutshell_Shortcuts`, title `Keyboard Shortcuts` (Task 4) match Task 8's lookups. The core files, tests and `cheatsheet_dlg.c` and `window.c` edits were compiled while writing this plan with the flags above (native gcc and `x86_64-w64-mingw32-gcc`, `-Werror`), and the native tests ran green against them.

**4. Review Focus.** Five lines above, each with its test in the owning task (1: Task 2; 2: Task 1; 3: Task 3; 4: Task 3; 5: Task 8 for Task 4's code). Also covered without a Review Focus line: the menu accelerator buffer (`test_shortcuts_menu_accels_fit_the_menu_buffer`), the table outgrowing the layout's capacity (`test_cheatsheet_layout_capacity_holds_the_table`), README block truncation (`test_shortcuts_readme_block_truncates_safely`), degenerate layout inputs (`test_cheatsheet_layout_degenerate_inputs`).
