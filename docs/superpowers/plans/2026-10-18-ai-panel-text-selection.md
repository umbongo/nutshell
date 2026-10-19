# Character Selection in the AI Assist Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Let the user select any characters in the AI Assist panel by mouse, across messages, and copy exactly the text they see with Ctrl+C, Edit > Copy or the context menu.

**Architecture:** Text and geometry are split. The visible text of a byte range is a pure function of the source, computed in `src/core/chat_copy.c` by walking the item as the painter does (the same `[EXEC]` split, now `chat_exec_split()` in core, the same line classifier, inline parser, list numbering and table columns) and tested natively against a model of the painter. Geometry is recorded by the paint pass only: `md_render.c` appends one `TextRun` per drawn word (rect, byte range, cumulative widths from one `GetTextExtentExPointW`) to the item's `TextRuns`, every item type goes through that layouter (`MD_MODE_PLAIN` for user, status, Thinking and command text), and the list view hit-tests the runs and keeps the selection as (message id, part, byte offset) pairs.

**Tech Stack:** C11, Win32 GDI (MinGW cross-compile), native gcc test runner (`tests/test_framework.h`), `make wintest` (MinGW binary run on Windows, or under Wine with `xvfb-run` on Linux), PowerShell integration harness (`tests/integration/`).

**Spec:** [docs/superpowers/specs/2026-10-18-ai-panel-text-selection-design.md](../specs/2026-10-18-ai-panel-text-selection-design.md)

---

## Global Constraints

- **Version bump before every build of the exe:** raise the patch in `src/ui/resource.h` (`APP_VERSION "1.2.36"` -> `"1.2.37"`, `APP_VERSION_BINARY 1,2,36,0` -> `1,2,37,0`) and README's `**Version**: v1.2.36` line to match, before any `make release`; after `1.x.99` comes `1.(x+1).0`. Done once, in Task 16 -- no other task builds the exe (they compile-check with `-fsyntax-only`, which produces no binary). If `main` has moved past 1.2.36 when Task 16 runs, bump past `main`'s version.
- **Build and test:** `make clean && make release` (only in Task 16), then `make test`. `make test` must pass at the end of every task (native baseline before this plan: 2,957 tests on automaton; 3,007 after Task 11, unchanged after that).
- **Warnings:** code compiles clean with `-Werror -Wshadow -Wconversion` (plus `-Wall -Wextra -Wpedantic -Wformat=2`) under both native gcc and `x86_64-w64-mingw32-gcc`. The native `make test` build does *not* use those flags, so every task runs the strict check itself:
  - core: `gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only <files>` and the same with `x86_64-w64-mingw32-gcc`;
  - src/ui: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only <files>`.
- **Design-system gate** (`tests/test_ui_tokens.c`, exact allow-list): no `RGB(` with literal numbers, no `MulDiv(x, dpi, 96)`, no `CreateFont` and no local scale macro in `src/ui`; colours from `ns_tokens()` (a `const ThemeTokens *`) or the theme passed in, blends with `rgb_alpha()` (`src/ui/ns_draw.h`), DPI with `ns_scale(px, dpi)`, fonts with `ns_font()`, sizes from the `SP_*`/`SZ_*` grid in `ns_type.h`, hover via `ns_hover`. The highlight is `rgb_alpha(<hue>, <surface>, 0.30f)`; nothing here adds an `RGB(` literal (the existing `RGB_FROM_THEME` macro unpacks a packed colour, which the gate allows).
- **Portable logic in `src/core`:** `src/ui/*.c` is excluded from `make test` (`NON_TEST_SRCS`), so everything testable -- splitting, extraction, hit-testing, boundary arithmetic, CR LF handling -- lives in `src/core` and is tested natively; `src/ui` only paints and dispatches.
- **Test files:** a test that writes a file builds its path from `TEST_TMP_DIR`, never `/tmp/` (none in this plan writes one). A Win32-only test file goes in `WIN_ONLY_TESTS` and `WIN_TEST_SRCS`, never in the native build.
- **Stack:** Windows threads have 1 MB. Large structs go on the heap (`AiConversation` in tests is `calloc`'d; `FakePaint` too); the renderer's per-run boundary buffers are 3 x 257 ints on the stack with a heap fallback.
- **`ASSERT_EQ`/`ASSERT_TRUE` evaluate their argument twice** (`tests/test_framework.h`): store a call with side effects in a variable first.
- **Line endings:** `Makefile`, `tests/runner.c`, `src/ui/ai_chat.h`, `src/core/ui_theme.h` and `src/core/ui_theme.c` are CRLF in the index; every other file this plan touches is LF. Edit with the Edit tool (it keeps them); never rewrite a whole file with a script.
- **Compiling `src/ui` without Windows:** automaton has a full MinGW toolchain (verified 2026-10-18: `x86_64-w64-mingw32-gcc`, the vcpkg static libs, `upx`, `wine`, `xvfb-run`), so src/ui tasks compile-check with the MinGW command above and run `make wintest` as `wineserver -k; xvfb-run -a make wintest` (without a display, Wine cannot create windows and the list-view cases fail with "could not create the windows"). Where no MinGW toolchain exists, an implementer relies on `make test` plus careful reading of the src/ui diff; either way the maintainer builds the exe and runs `mingw32-make wintest` on the Windows box before the PR leaves draft.
- **Commits:** stage new files by path and read every `??` line of `git status --short` before committing (`git add -u` skips new files). Subject in the imperative, `type(scope): ...`, with the version in parentheses only on commits that carry a build (Task 16). Body in the maintainer's voice, then a `Models:` line naming every model that touched the change with its full id and role, then the `Co-Authored-By:` trailer the implementer's tooling supplies. The commands below show the plan author and a Sonnet implementer (`Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)`, `Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>`); a different implementer puts its own id and trailer there. Work in `/home/thomas/nutshell-wt/text-selection` on `feat/ai-panel-text-selection`; never push to `main`; a cloud session stops at the pushed branch.

## Review Focus

Failure modes the spec implies that no task's tests exercised as first written, most likely to bite a user first. Each was confirmed on a scratch prototype of this plan (2026-10-18) and the test now sits in the owning task.

1. **Double-clicking a word in a list item copies the bullet** (`- partition:`): the spec copies a list marker "when the range covers any byte of the line". Found by the wintest mouse case. The marker is now copied, and highlighted, only when the range covers a byte of the marker itself; the synthetic run spans the marker's bytes. Tests: `test_chat_copy_bullet_marker_follows_its_bytes` and `test_chat_copy_ordered_list_copies_the_displayed_number` (Task 5), the double- and triple-click checks in `test_chat_select_win_mouse_and_keys` (Task 12).
2. **The highlight is invisible on the user bubble:** the spec fills with `rgb_alpha(focus, bubble, 30%)`, but the bubble is filled with the accent the focus token comes from -- the step is 0 in Onyx Synapse and Sage & Sand. The bubble uses its own text colour as the hue. Test: `test_sel_highlight_visible_on_every_surface` (Task 8, native), every surface in every theme at least 40 channel-steps from what it sits on.
3. **Turning markdown rendering off leaves every item's old height:** `chat_listview_set_render_markdown()` never marks items dirty and `recalc_layout()` only remeasures dirty ones, so runs and hit-tests land on stale rects (measured 373 vs painted 364 px). Fixed in Task 8. Test: the `markdown = 0` configuration of `test_chat_select_win_runs_match_paint` (Task 12), which toggles the setting after the first layout.
4. **Leading whitespace is copied but never painted:** `render_inline_spans` drops whitespace at the start of a line's content (`-  x`, an indented paragraph, `**  a**`), and the spec's "nothing is trimmed" would copy it, so the copy differs from the highlight. The copier skips it exactly as the painter does (markdown only; `MD_MODE_PLAIN` keeps it in both). Tests: `test_chat_copy_leading_whitespace_is_not_copied` (Task 5) and the fourth fixture of `test_painted_words_equal_copied_text` (Task 6).
5. **A table whose alignment row has fewer columns than its header:** the painter draws `md_table_alignments()`'s column count, the spec's copier drops only columns past `MD_TABLE_MAX_COLS`, so the copy would carry columns the panel never drew. The copier takes the painter's column count per table block. Test: `test_chat_copy_table_columns_follow_the_alignment_row` (Task 5).

## Prototype note

Every task below was applied to a scratch copy of this branch before the plan was written: the core code compiles under the strict flags with both compilers, `make test` passes at each step (3,007 tests at the end), the whole tree passes the MinGW `-fsyntax-only` sweep and links into an exe, and `make wintest` passes all thirteen cases under Wine and Xvfb. The `--ui-demo=chat` screenshot was taken in the four themes from that build. Code blocks here are that code.

## File Map

- Move: `src/ui/markdown.h` -> `src/core/markdown.h`.
- Create (core): `src/core/chat_exec.h/.c`, `src/core/text_runs.h/.c`, `src/core/chat_copy.h/.c`.
- Create (tests): `tests/test_chat_exec.c`, `tests/test_text_runs.c`, `tests/test_chat_copy.c`, `tests/fake_md_painter.h/.c`, `tests/test_chat_copy_paint.c`, `tests/test_sel_contrast.c`, `tests/test_md_render_win.c`, `tests/test_chat_select_win.c`, `tests/integration/cases/95-ai-select.ps1`.
- Modify (core): `src/core/md_blocks.c/.h` (include path), `src/core/md_table.h` (table limits), `src/core/chat_msg.h/.c` (runs on the item, `chat_msg_find`, `chat_strip_exec_markers` removed), `src/core/text_lines.h/.c` (`text_crlf_to_lf`), `src/core/ui_theme.h/.c` (`ui_theme_sel_hue`), `src/core/ui_demo.h/.c`.
- Modify (ui): `src/ui/md_render.h/.c`, `src/ui/chat_listview.h/.c`, `src/ui/ai_chat.h/.c`, `src/ui/window.c` (`do_copy`), `src/ui/resource.h` (Task 16).
- Modify (tests/build/docs): `tests/runner.c`, `tests/test_markdown_render.c`, `tests/test_chat_msg.c`, `tests/test_text_lines.c`, `tests/test_ui_demo.c`, `tests/win_runner.c`, `Makefile`, `tests/integration/NutshellIT.psm1`, `tests/integration/README.md`, `README.md`, `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`, `build/win/nutshell.exe` (Task 16).

Registration pattern used throughout: every test function is declared and called in `tests/runner.c` with `int test_x(void);` near the other declarations and `failed += test_x();` in `main` (there is no `RUN_TEST` macro). New test files register their block immediately **before** the line `/* test_md_blocks.c */` (declarations) and immediately **before** `    failed += test_md_blocks_none();` (calls).

---

### Task 1: Move `markdown.h` to `src/core`

The parser is header-only and Win32-free; the copier in core needs it, and `src/core/md_blocks.c` already reaches into `../ui/` for it.

**Files:**
- Move: `src/ui/markdown.h` -> `src/core/markdown.h` (content unchanged)
- Modify: `src/core/md_blocks.c:2` (include), `src/core/md_blocks.h:7` (comment)
- Test: `tests/test_markdown_render.c:10` (include)

**Interfaces:**
- Consumes: nothing.
- Produces: `src/core/markdown.h` with `md_classify_line`, `md_parse_inline`, `md_next_word`, `md_is_table_separator`, `MdLineInfo`, `MdSpan`, `MD_MAX_SPANS` unchanged. `#include "markdown.h"` resolves through `-Isrc/core` in every build (`CFLAGS`, `TEST_CFLAGS`, `WIN_TEST_CFLAGS` all carry it); `src/ui/md_render.c`, `src/ui/ai_chat.c`, `src/core/md_table.c` and `tests/test_ai_chat.c` need no edit.

- [ ] **Step 1.1: Point the test at the new path (the failing test)**

In `tests/test_markdown_render.c` replace line 10:

```c
#include "../src/ui/markdown.h"
```
with

```c
#include "../src/core/markdown.h"
```
- [ ] **Step 1.2: Run it and see it fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make test 2>&1 | grep -m1 "markdown.h"
```
Expected: `tests/test_markdown_render.c:10:10: fatal error: ../src/core/markdown.h: No such file or directory`.

- [ ] **Step 1.3: Move the header and fix the one relative include**

```bash
cd /home/thomas/nutshell-wt/text-selection && git mv src/ui/markdown.h src/core/markdown.h
```
In `src/core/md_blocks.c` replace line 2:

```c
#include "../ui/markdown.h"   /* md_classify_line: header-only, Win32-free */
```
with

```c
#include "markdown.h"   /* md_classify_line: header-only, Win32-free */
```
In `src/core/md_blocks.h` replace, in the comment on line 7,

```c
 * (md_classify_line in src/ui/markdown.h) so it recognises exactly the fences
```
with

```c
 * (md_classify_line in src/core/markdown.h) so it recognises exactly the fences
```
- [ ] **Step 1.4: Run the tests and the include sweep**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | tail -1
grep -rn "ui/markdown.h" src tests Makefile
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/ui/md_render.c src/ui/ai_chat.c src/core/md_table.c src/core/md_blocks.c
```
Expected: `Tests Run: 2957, Failed: 0`; the grep prints nothing; the MinGW check prints nothing.

- [ ] **Step 1.5: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/core/markdown.h src/core/md_blocks.c src/core/md_blocks.h tests/test_markdown_render.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
refactor(core): move the markdown parser to src/core

markdown.h is header-only and Win32-free, and the selection copier in
src/core needs it. md_blocks.c no longer reaches into ../ui for it.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 2: `chat_exec_split()` -- the `[EXEC]` split in one place

Extract the segment splitter that `draw_ai_text_with_exec()` and `measure_ai_text_with_exec()` each carry, so the painter and the copier cut a reply at the same bytes (spec section 2).

**Files:**
- Create: `src/core/chat_exec.h`, `src/core/chat_exec.c`
- Modify: `src/ui/chat_listview.c` -- includes (~line 32), `measure_ai_text_with_exec()` (~971-1041), `draw_ai_text_with_exec()` (~1373-1486)
- Test: `tests/test_chat_exec.c` (new), `tests/runner.c`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `typedef enum { CHAT_SEG_PROSE = 0, CHAT_SEG_EXEC = 1 } ChatSegKind;`
  - `typedef struct { ChatSegKind kind; int start; int end; } ChatSeg;`
  - `int chat_exec_split(const char *text, int len, int *pos, ChatSeg *out);` -- iterator: `*pos = 0`, call until 0; prose before a marker loses its trailing `\n`/`\r` and is skipped when empty; a command excludes its markers; an unterminated `[EXEC]` is one command segment from the marker (included) to the end; the last prose is untrimmed.

- [ ] **Step 2.1: Write the failing tests**

Create `tests/test_chat_exec.c`:

```c
/* tests/test_chat_exec.c -- chat_exec_split(): the [EXEC] segment split the
 * chat painter and the copier share (spec 2026-10-18 section 2). */
#include "test_framework.h"
#include "chat_exec.h"
#include <string.h>

/* Collect every segment of `t` into segs (at most max); returns the count. */
static int split_all(const char *t, ChatSeg *segs, int max)
{
    int pos = 0, n = 0;
    ChatSeg s;
    while (chat_exec_split(t, (int)strlen(t), &pos, &s)) {
        if (n < max) segs[n] = s;
        n++;
        if (n > 64) break;   /* a stuck iterator fails the test, not the run */
    }
    return n;
}

static int seg_is(const char *t, ChatSeg s, ChatSegKind kind, const char *want)
{
    int n = (int)strlen(want);
    return s.kind == kind && s.end - s.start == n &&
           memcmp(t + s.start, want, (size_t)n) == 0;
}

int test_chat_exec_split_no_marker_is_one_prose_segment(void)
{
    TEST_BEGIN();
    const char *t = "Hello\n\n**world**\n";
    ChatSeg s[4];
    ASSERT_EQ(split_all(t, s, 4), 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "Hello\n\n**world**\n"));
    TEST_END();
}

int test_chat_exec_split_command_between_prose(void)
{
    TEST_BEGIN();
    const char *t = "Run this:\n\n[EXEC]ls -la[/EXEC]\nDone.";
    ChatSeg s[4];
    ASSERT_EQ(split_all(t, s, 4), 3);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "Run this:"));
    ASSERT_TRUE(seg_is(t, s[1], CHAT_SEG_EXEC, "ls -la"));
    ASSERT_TRUE(seg_is(t, s[2], CHAT_SEG_PROSE, "\nDone."));
    TEST_END();
}

int test_chat_exec_split_crlf_before_marker_is_trimmed(void)
{
    TEST_BEGIN();
    const char *t = "a\r\n\r\n[EXEC]x[/EXEC]";
    ChatSeg s[4];
    ASSERT_EQ(split_all(t, s, 4), 2);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "a"));
    ASSERT_TRUE(seg_is(t, s[1], CHAT_SEG_EXEC, "x"));
    TEST_END();
}

int test_chat_exec_split_only_newlines_before_marker_give_no_prose(void)
{
    TEST_BEGIN();
    const char *t = "\n\n[EXEC]pwd[/EXEC]";
    ChatSeg s[4];
    ASSERT_EQ(split_all(t, s, 4), 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_EXEC, "pwd"));
    TEST_END();
}

int test_chat_exec_split_unterminated_keeps_the_marker(void)
{
    TEST_BEGIN();
    const char *t = "Try [EXEC]uname -a";
    ChatSeg s[4];
    ASSERT_EQ(split_all(t, s, 4), 2);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "Try "));
    ASSERT_TRUE(seg_is(t, s[1], CHAT_SEG_EXEC, "[EXEC]uname -a"));
    TEST_END();
}

int test_chat_exec_split_empty_and_adjacent_commands(void)
{
    TEST_BEGIN();
    const char *t = "[EXEC][/EXEC][EXEC]a[/EXEC][EXEC]b[/EXEC]";
    ChatSeg s[4];
    ASSERT_EQ(split_all(t, s, 4), 3);
    ASSERT_EQ(s[0].kind, CHAT_SEG_EXEC);
    ASSERT_EQ(s[0].start, s[0].end);
    ASSERT_TRUE(seg_is(t, s[1], CHAT_SEG_EXEC, "a"));
    ASSERT_TRUE(seg_is(t, s[2], CHAT_SEG_EXEC, "b"));
    TEST_END();
}

int test_chat_exec_split_stray_close_is_prose(void)
{
    TEST_BEGIN();
    const char *t = "x [/EXEC] y";
    ChatSeg s[4];
    ASSERT_EQ(split_all(t, s, 4), 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "x [/EXEC] y"));
    TEST_END();
}

int test_chat_exec_split_respects_len_and_null(void)
{
    TEST_BEGIN();
    const char *t = "ab[EXEC]cd[/EXEC]";
    int pos = 0;
    ChatSeg s;
    /* len 4 cuts the marker short: the whole prefix is prose */
    ASSERT_EQ(chat_exec_split(t, 4, &pos, &s), 1);
    ASSERT_EQ(s.kind, CHAT_SEG_PROSE);
    ASSERT_EQ(s.start, 0);
    ASSERT_EQ(s.end, 4);
    ASSERT_EQ(chat_exec_split(t, 4, &pos, &s), 0);
    pos = 0;
    ASSERT_EQ(chat_exec_split(NULL, 4, &pos, &s), 0);
    ASSERT_EQ(chat_exec_split(t, 0, &pos, &s), 0);
    ASSERT_EQ(chat_exec_split(t, 4, NULL, &s), 0);
    TEST_END();
}
```
In `tests/runner.c` insert immediately before the line `/* test_md_blocks.c */`:

```c
/* test_chat_exec.c */
int test_chat_exec_split_no_marker_is_one_prose_segment(void);
int test_chat_exec_split_command_between_prose(void);
int test_chat_exec_split_crlf_before_marker_is_trimmed(void);
int test_chat_exec_split_only_newlines_before_marker_give_no_prose(void);
int test_chat_exec_split_unterminated_keeps_the_marker(void);
int test_chat_exec_split_empty_and_adjacent_commands(void);
int test_chat_exec_split_stray_close_is_prose(void);
int test_chat_exec_split_respects_len_and_null(void);

```
and immediately before the line `    failed += test_md_blocks_none();`:

```c
    failed += test_chat_exec_split_no_marker_is_one_prose_segment();
    failed += test_chat_exec_split_command_between_prose();
    failed += test_chat_exec_split_crlf_before_marker_is_trimmed();
    failed += test_chat_exec_split_only_newlines_before_marker_give_no_prose();
    failed += test_chat_exec_split_unterminated_keeps_the_marker();
    failed += test_chat_exec_split_empty_and_adjacent_commands();
    failed += test_chat_exec_split_stray_close_is_prose();
    failed += test_chat_exec_split_respects_len_and_null();

```
- [ ] **Step 2.2: Run them and see them fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make test 2>&1 | grep -m1 "chat_exec.h"
```
Expected: `fatal error: chat_exec.h: No such file or directory` (from `tests/test_chat_exec.c`).

- [ ] **Step 2.3: Implement**

Create `src/core/chat_exec.h`:

```c
/* src/core/chat_exec.h */
#ifndef NUTSHELL_CHAT_EXEC_H
#define NUTSHELL_CHAT_EXEC_H

/*
 * The [EXEC] segment split of an AI reply, cut exactly where the chat panel
 * cuts it when it paints (src/ui/chat_listview.c draw_ai_text_with_exec and
 * its measuring twin), so the copier (chat_copy.c) and the painter agree on
 * every byte. In order:
 *
 *   - the prose before an "[EXEC]" marker, its trailing '\n'/'\r' bytes
 *     dropped, and no segment at all when nothing is left;
 *   - the command between "[EXEC]" and "[/EXEC]", markers excluded (an
 *     empty command is a segment with start == end, which paints nothing);
 *   - an "[EXEC]" with no "[/EXEC]" after it: everything from the marker
 *     itself to the end, the marker included, as one command segment;
 *   - the prose after the last "[/EXEC]", untrimmed (it usually starts with
 *     the '\n' that followed the marker, which paints as a blank line).
 *
 * A stray "[/EXEC]" with no "[EXEC]" before it is prose. Portable, no
 * windows.h. Spec: docs/superpowers/specs/
 * 2026-10-18-ai-panel-text-selection-design.md section 2.
 */

typedef enum { CHAT_SEG_PROSE = 0, CHAT_SEG_EXEC = 1 } ChatSegKind;

typedef struct {
    ChatSegKind kind;
    int start;   /* byte offset into the text */
    int end;     /* exclusive */
} ChatSeg;

/* Iterator. Set *pos = 0 and call until it returns 0; every call that
 * returns 1 writes the next segment to *out. `len` bounds the walk (the text
 * need not be NUL-terminated at len). NULL text/pos/out or len <= 0 yield no
 * segment. */
int chat_exec_split(const char *text, int len, int *pos, ChatSeg *out);

#endif /* NUTSHELL_CHAT_EXEC_H */
```
Create `src/core/chat_exec.c`:

```c
/* src/core/chat_exec.c -- see chat_exec.h. */
#include "chat_exec.h"
#include <string.h>

static const char EXEC_OPEN[]  = "[EXEC]";
static const char EXEC_CLOSE[] = "[/EXEC]";
#define EXEC_OPEN_LEN  ((int)sizeof(EXEC_OPEN) - 1)
#define EXEC_CLOSE_LEN ((int)sizeof(EXEC_CLOSE) - 1)

/* First occurrence of marker m (mlen bytes) in text[from, len), or -1. */
static int find_marker(const char *text, int from, int len,
                       const char *m, int mlen)
{
    for (int i = from; i + mlen <= len; i++)
        if (text[i] == m[0] && memcmp(text + i, m, (size_t)mlen) == 0)
            return i;
    return -1;
}

int chat_exec_split(const char *text, int len, int *pos, ChatSeg *out)
{
    if (!text || !pos || !out || len <= 0) return 0;
    for (;;) {
        int p = *pos;
        if (p < 0 || p >= len) return 0;

        /* At a marker: the command segment. */
        if (p + EXEC_OPEN_LEN <= len &&
            memcmp(text + p, EXEC_OPEN, (size_t)EXEC_OPEN_LEN) == 0) {
            int cmd = p + EXEC_OPEN_LEN;
            int close = find_marker(text, cmd, len, EXEC_CLOSE, EXEC_CLOSE_LEN);
            out->kind = CHAT_SEG_EXEC;
            if (close < 0) {          /* unterminated: marker and all */
                out->start = p;
                out->end = len;
                *pos = len;
            } else {
                out->start = cmd;
                out->end = close;
                *pos = close + EXEC_CLOSE_LEN;
            }
            return 1;
        }

        int open = find_marker(text, p, len, EXEC_OPEN, EXEC_OPEN_LEN);
        if (open < 0) {               /* the rest is prose, untrimmed */
            out->kind = CHAT_SEG_PROSE;
            out->start = p;
            out->end = len;
            *pos = len;
            return 1;
        }

        /* Prose before the marker, trailing line ends dropped. */
        int trim = open;
        while (trim > p && (text[trim - 1] == '\n' || text[trim - 1] == '\r'))
            trim--;
        *pos = open;
        if (trim > p) {
            out->kind = CHAT_SEG_PROSE;
            out->start = p;
            out->end = trim;
            return 1;
        }
        /* nothing left before the marker: loop to emit the command */
    }
}
```
- [ ] **Step 2.4: Switch the painter and its measuring twin to it**

In `src/ui/chat_listview.c`, after `#include "stick_scroll.h"` add:

```c
#include "chat_exec.h"
```
Replace the whole comment block and function `measure_ai_text_with_exec()` -- from the line `/* Height-only twin of draw_ai_text_with_exec() (near paint_ai_item): walks` to the closing `}` before `/* ── Measure a single item` -- with:

```c
/* Height-only twin of draw_ai_text_with_exec() (near paint_ai_item): walks
 * the same chat_exec_split() segments -- prose through measure_ai_segment()
 * (markdown or plain, matching draw_ai_segment's own font choice), commands
 * through the mono font with DT_CALCRECT (matching draw_ai_text_with_exec's
 * mono-font paint) -- so the two can never disagree about an EXEC-bearing
 * message's height. */
static int measure_ai_text_with_exec(ChatListView *lv, HDC hdc,
                                     const char *text, int width)
{
    HFONT mono_font = lv->hMonoFont ? lv->hMonoFont
                                     : (HFONT)GetStockObject(ANSI_FIXED_FONT);
    int len = (int)strlen(text);
    int pos = 0, total_h = 0;
    ChatSeg seg;
    while (chat_exec_split(text, len, &pos, &seg)) {
        int n = seg.end - seg.start;
        if (n <= 0) continue;
        char *s = (char *)malloc((size_t)n + 1);
        if (!s) continue;
        memcpy(s, text + seg.start, (size_t)n);
        s[n] = '\0';
        if (seg.kind == CHAT_SEG_PROSE) {
            total_h += measure_ai_segment(lv, hdc, s, width);
        } else {
            RECT rc;
            SetRect(&rc, 0, 0, width, 0);
            HGDIOBJ old = SelectObject(hdc, mono_font);
            draw_text_utf8(hdc, s, &rc,
                           DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hdc, old);
            total_h += rc.bottom - rc.top;
        }
        free(s);
    }
    return total_h;
}
```
Replace the comment block and function `draw_ai_text_with_exec()` -- from `/* Draw AI text with [EXEC]...[/EXEC] segments highlighted in purple.` to the closing `}` before `static int paint_ai_item(` -- with:

```c
/* Draw AI text with its [EXEC] commands in the info hue and mono font: the
 * chat_exec_split() segments one under the other. Returns the total height
 * painted (each command is laid out with DT_CALCRECT first and painted
 * into an exact-fit rect, so a stale item bottom never truncates it -- see
 * measure_ai_text_with_exec()'s comment), so callers can find out when this
 * grew taller than measure_item() predicted. */
static int draw_ai_text_with_exec(ChatListView *lv, HDC hdc,
                                   const char *text, RECT *rc, int item_id)
{
    int blk = 0;   /* code-block number within this item, across segments */
    COLORREF exec_clr = RGB_FROM_THEME(ns_tokens()->info.base);
    HFONT mono_font = lv->hMonoFont ? lv->hMonoFont
                                     : (HFONT)GetStockObject(ANSI_FIXED_FONT);
    int len = (int)strlen(text);
    int y = rc->top, pos = 0;
    ChatSeg seg;

    while (chat_exec_split(text, len, &pos, &seg)) {
        int n = seg.end - seg.start;
        if (n <= 0) continue;
        char *s = (char *)malloc((size_t)n + 1);
        if (!s) continue;
        memcpy(s, text + seg.start, (size_t)n);
        s[n] = '\0';
        if (seg.kind == CHAT_SEG_PROSE) {
            y += draw_ai_segment(lv, hdc, s, rc->left, y,
                                 rc->right - rc->left, item_id, &blk);
        } else {
            RECT seg_rc;
            HGDIOBJ old_mf = SelectObject(hdc, mono_font);
            SetRect(&seg_rc, rc->left, y, rc->right, 0);
            draw_text_utf8(hdc, s, &seg_rc,
                           DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            int h = seg_rc.bottom - seg_rc.top;
            SetRect(&seg_rc, rc->left, y, rc->right, y + h);
            SetTextColor(hdc, exec_clr);
            draw_text_utf8(hdc, s, &seg_rc, DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hdc, old_mf);
            y += h;
        }
        free(s);
    }
    return y - rc->top;
}
```
Both keep their signatures and every height they returned: the fast path (no marker) is the single prose segment, an empty command still paints nothing, an unterminated command still paints from its marker.

- [ ] **Step 2.5: Run the tests and the strict checks**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | grep -E "chat_exec|Tests Run" | grep -v RUN
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/chat_exec.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/chat_exec.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/ui/chat_listview.c
```
Expected: eight `[PASS] test_chat_exec_split_*` lines and `Tests Run: 2965, Failed: 0`; the three compiler checks print nothing.

- [ ] **Step 2.6: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/core/chat_exec.h src/core/chat_exec.c tests/test_chat_exec.c tests/runner.c src/ui/chat_listview.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
feat(core): split AI replies at [EXEC] in one place

chat_exec_split() is the segment walk draw_ai_text_with_exec() and its
measuring twin each carried. The selection copier will walk the same
segments, so painter and copier cut a reply at the same bytes.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 3: `text_runs` -- run geometry, boundaries and the hit-test

The portable half of sections 3, 4 and 6: the `TextRun`/`TextRuns` the paint pass fills, UTF-16-to-UTF-8 boundary conversion, the hit-test, the selected-pixels-of-a-run arithmetic for the highlight, and the word/line helpers for double- and triple-click.

**Files:**
- Create: `src/core/text_runs.h`, `src/core/text_runs.c`
- Test: `tests/test_text_runs.c` (new), `tests/runner.c`

**Interfaces:**
- Consumes: nothing.
- Produces (all in `text_runs.h`):
  - `#define TR_PART_THINKING 0`, `#define TR_PART_MAIN 1` (reading order: the Thinking body is above the text), `#define TR_FLAG_CODE 1`.
  - `TextRun { int x, y, w, h; int start, end; int part; int line; int first_cum; int n_cum; int flags; char synthetic[8]; }` -- the spec's struct plus `flags`.
  - `TextRuns { TextRun *v; int n, cap; int *cum; int *cum_byte; int cum_n, cum_cap; int line_count; }` -- the spec's struct plus `cum_byte`, the byte offset at each boundary: a pure hit-test needs it to turn a boundary into a byte offset without the text.
  - `void text_runs_init(TextRuns *r); void text_runs_reset(TextRuns *r); void text_runs_free(TextRuns *r);`
  - `int text_runs_append(TextRuns *r, const TextRun *proto, const int *cum, const int *cum_byte, int n_cum);` -> index or -1.
  - `int text_runs_cum_from_utf16(const char *s, int len, const int *dx16, int n16, int *cum, int *cum_byte, int cap);` -> boundaries written or -1.
  - `int text_runs_utf8_prefix_for_utf16(const char *s, int len, int n16);`
  - `int text_runs_span_px(int run_start, int run_end, const int *cum, const int *cum_byte, int n_cum, int sel_start, int sel_end, int *x0, int *x1);`
  - `int text_runs_hit(const TextRuns *r, int x, int y, int *part_out);` -> byte offset or -1.
  - `int text_runs_run_at(const TextRuns *r, int x, int y);`
  - `void text_runs_word(const char *text, int text_len, const TextRun *run, int off, int *s, int *e);`
  - `void text_line_at(const char *text, int len, int off, int *s, int *e);`
  - `int text_next_line(const char *s, int len, int *pos, int *ls, int *le);` -- the MD_PLAIN line iterator (CR LF ends a line, no empty line after a final `\n`).

- [ ] **Step 3.1: Write the failing tests**

Create `tests/test_text_runs.c`:

```c
/* tests/test_text_runs.c -- the run geometry recorded by the chat panel's
 * paint pass and the hit test over it (spec 2026-10-18 sections 3, 4, 6). */
#include "test_framework.h"
#include "text_runs.h"
#include <string.h>

/* Append the run for text[start, end) at (x, y), every UTF-16 unit 6 px
 * wide, through text_runs_cum_from_utf16 exactly as md_render.c does. */
static int add_run(TextRuns *r, const char *text, int start, int end,
                   int x, int y, int part, int flags)
{
    int dx16[64], cum[65], cb[65];
    for (int i = 0; i < 64; i++) dx16[i] = 6 * (i + 1);
    int n = text_runs_cum_from_utf16(text + start, end - start, dx16, 64,
                                     cum, cb, 65);
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.x = x; t.y = y; t.w = cum[n - 1]; t.h = 10;
    t.start = start; t.end = end; t.part = part; t.flags = flags;
    return text_runs_append(r, &t, cum, cb, n);
}

static int add_synth(TextRuns *r, const char *mark, int start, int end,
                     int x, int y, int w, int part)
{
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.x = x; t.y = y; t.w = w; t.h = 10;
    t.start = start; t.end = end; t.part = part;
    strcpy(t.synthetic, mark);
    return text_runs_append(r, &t, NULL, NULL, 0);
}

/* "Hello w\xC3\xB6rld\n- item": line A = "Hello " [0,6) at x 10 and
 * "w\xC3\xB6rld" [6,12) at x 46; line B = the bullet over its marker bytes
 * [13,15) at x 10 (16 px wide) and "item" [15,19) at x 26. */
static const char FIX[] = "Hello w\xC3\xB6rld\n- item";

static void build_fixture(TextRuns *r)
{
    text_runs_init(r);
    add_run(r, FIX, 0, 6, 10, 0, TR_PART_MAIN, 0);
    add_run(r, FIX, 6, 12, 46, 0, TR_PART_MAIN, 0);
    add_synth(r, "- ", 13, 15, 10, 14, 16, TR_PART_MAIN);
    add_run(r, FIX, 15, 19, 26, 14, TR_PART_MAIN, 0);
}

int test_text_runs_cum_ascii_multibyte_and_surrogates(void)
{
    TEST_BEGIN();
    int dx16[8] = { 6, 12, 18, 24, 30, 36, 42, 48 };
    int cum[8], cb[8];
    /* "a" + U+1F600 (4 bytes, 2 units) + "b" */
    const char *s = "a\xF0\x9F\x98\x80" "b";
    int n = text_runs_cum_from_utf16(s, 6, dx16, 4, cum, cb, 8);
    ASSERT_EQ(n, 4);
    ASSERT_EQ(cum[0], 0);  ASSERT_EQ(cb[0], 0);
    ASSERT_EQ(cum[1], 6);  ASSERT_EQ(cb[1], 1);
    ASSERT_EQ(cum[2], 18); ASSERT_EQ(cb[2], 5);
    ASSERT_EQ(cum[3], 24); ASSERT_EQ(cb[3], 6);
    /* "\xC3\xA9" is one character, one unit */
    n = text_runs_cum_from_utf16("\xC3\xA9x", 3, dx16, 2, cum, cb, 8);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(cb[1], 2);   ASSERT_EQ(cum[1], 6);
    ASSERT_EQ(cb[2], 3);   ASSERT_EQ(cum[2], 12);
    TEST_END();
}

int test_text_runs_cum_invalid_bytes_and_limits(void)
{
    TEST_BEGIN();
    int dx16[4] = { 5, 10, 15, 20 };
    int cum[4], cb[4];
    /* cp1252 "\xE9" then "x": an invalid lead is one character */
    int n = text_runs_cum_from_utf16("\xE9x", 2, dx16, 2, cum, cb, 4);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(cb[1], 1);  ASSERT_EQ(cum[1], 5);
    ASSERT_EQ(cb[2], 2);  ASSERT_EQ(cum[2], 10);
    /* fewer units reported than characters: the last width repeats */
    n = text_runs_cum_from_utf16("abc", 3, dx16, 1, cum, cb, 4);
    ASSERT_EQ(n, 4);
    ASSERT_EQ(cum[3], 5);
    /* cap too small */
    ASSERT_EQ(text_runs_cum_from_utf16("abcd", 4, dx16, 4, cum, cb, 4), -1);
    ASSERT_EQ(text_runs_cum_from_utf16(NULL, 1, dx16, 1, cum, cb, 4), -1);
    TEST_END();
}

int test_text_runs_utf8_prefix_never_splits_a_pair(void)
{
    TEST_BEGIN();
    const char *s = "a\xF0\x9F\x98\x80" "b";
    ASSERT_EQ(text_runs_utf8_prefix_for_utf16(s, 6, 1), 1);
    ASSERT_EQ(text_runs_utf8_prefix_for_utf16(s, 6, 2), 1);
    ASSERT_EQ(text_runs_utf8_prefix_for_utf16(s, 6, 3), 5);
    ASSERT_EQ(text_runs_utf8_prefix_for_utf16(s, 6, 9), 6);
    ASSERT_EQ(text_runs_utf8_prefix_for_utf16(s, 6, 0), 0);
    TEST_END();
}

int test_text_runs_append_assigns_lines_and_boundaries(void)
{
    TEST_BEGIN();
    TextRuns r;
    build_fixture(&r);
    ASSERT_EQ(r.n, 4);
    ASSERT_EQ(r.line_count, 2);
    ASSERT_EQ(r.v[0].line, 0);
    ASSERT_EQ(r.v[1].line, 0);
    ASSERT_EQ(r.v[2].line, 1);
    ASSERT_EQ(r.v[3].line, 1);
    ASSERT_EQ(r.v[0].n_cum, 7);          /* "Hello " */
    ASSERT_EQ(r.v[1].n_cum, 6);          /* w o-umlaut r l d */
    ASSERT_EQ(r.v[1].first_cum, 7);
    ASSERT_EQ(r.v[2].n_cum, 0);
    ASSERT_EQ(r.v[1].w, 30);
    text_runs_reset(&r);
    ASSERT_EQ(r.n, 0);
    ASSERT_EQ(r.cum_n, 0);
    ASSERT_EQ(r.line_count, 0);
    text_runs_free(&r);
    ASSERT_NULL(r.v);
    TEST_END();
}

int test_text_runs_hit_inside_a_run_takes_the_nearest_boundary(void)
{
    TEST_BEGIN();
    TextRuns r;
    build_fixture(&r);
    int part = -1;
    ASSERT_EQ(text_runs_hit(&r, 10, 5, &part), 0);
    ASSERT_EQ(part, TR_PART_MAIN);
    ASSERT_EQ(text_runs_hit(&r, 18, 5, NULL), 1);    /* 8 px in: boundary 6 */
    ASSERT_EQ(text_runs_hit(&r, 20, 5, NULL), 2);    /* 10 px in: boundary 12 */
    /* x 58 is 12 px into "w\xC3\xB6rld": boundary 2 = after the umlaut, byte 9 */
    ASSERT_EQ(text_runs_hit(&r, 58, 5, NULL), 9);
    /* never inside the two-byte character */
    ASSERT_TRUE(text_runs_hit(&r, 55, 5, NULL) != 8);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_left_right_and_between(void)
{
    TEST_BEGIN();
    TextRuns r;
    build_fixture(&r);
    ASSERT_EQ(text_runs_hit(&r, 0, 5, NULL), 0);      /* left of the line */
    ASSERT_EQ(text_runs_hit(&r, 500, 5, NULL), 12);   /* right of the line */
    /* a gap between runs: move run 1 right by 20 px */
    r.v[1].x = 66;
    ASSERT_EQ(text_runs_hit(&r, 50, 5, NULL), 6);     /* end of the left run */
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_above_below_and_between_lines(void)
{
    TEST_BEGIN();
    TextRuns r;
    build_fixture(&r);
    ASSERT_EQ(text_runs_hit(&r, 40, -30, NULL), 0);   /* above everything */
    ASSERT_EQ(text_runs_hit(&r, 40, 400, NULL), 19);  /* below everything */
    /* y 12 is in the 4 px gap under line A: the line above */
    ASSERT_EQ(text_runs_hit(&r, 500, 12, NULL), 12);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_synthetic_maps_to_the_next_real_run(void)
{
    TEST_BEGIN();
    TextRuns r;
    build_fixture(&r);
    ASSERT_EQ(text_runs_hit(&r, 12, 18, NULL), 15);   /* on the bullet */
    ASSERT_EQ(text_runs_hit(&r, 0, 18, NULL), 15);    /* left of the bullet */
    /* a bullet alone on its line resolves to its own end */
    TextRuns lone;
    text_runs_init(&lone);
    add_synth(&lone, "3. ", 0, 3, 10, 0, 16, TR_PART_MAIN);
    ASSERT_EQ(text_runs_hit(&lone, 12, 5, NULL), 3);
    text_runs_free(&lone);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_reports_the_thinking_part(void)
{
    TEST_BEGIN();
    /* Thinking body runs above the reply text, painted first. */
    static const char THINK[] = "plan it";
    static const char MAIN[] = "done";
    TextRuns r;
    text_runs_init(&r);
    add_run(&r, THINK, 0, 5, 10, 0, TR_PART_THINKING, 0);
    add_run(&r, THINK, 5, 7, 40, 0, TR_PART_THINKING, 0);
    add_run(&r, MAIN, 0, 4, 10, 30, TR_PART_MAIN, 0);
    int part = -1;
    ASSERT_EQ(text_runs_hit(&r, 22, 5, &part), 2);
    ASSERT_EQ(part, TR_PART_THINKING);
    ASSERT_EQ(text_runs_hit(&r, 16, 33, &part), 1);
    ASSERT_EQ(part, TR_PART_MAIN);
    /* the gap between the box and the reply belongs to the box's last line */
    ASSERT_EQ(text_runs_hit(&r, 500, 20, &part), 7);
    ASSERT_EQ(part, TR_PART_THINKING);
    /* runs on one y but different parts never form one line */
    ASSERT_EQ(r.v[2].line, 1);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_table_cells_share_a_line_by_y(void)
{
    TEST_BEGIN();
    /* cell 1 wraps to a second line, cell 2 sits back on the row's top y */
    static const char ROW[] = "| aa bb | cc |";
    TextRuns r;
    text_runs_init(&r);
    add_run(&r, ROW, 2, 5, 10, 0, TR_PART_MAIN, 0);    /* "aa " */
    add_run(&r, ROW, 5, 7, 10, 12, TR_PART_MAIN, 0);   /* "bb" (wrapped) */
    add_run(&r, ROW, 10, 12, 60, 0, TR_PART_MAIN, 0);  /* "cc" */
    ASSERT_EQ(text_runs_hit(&r, 64, 5, NULL), 11);     /* inside "cc" */
    ASSERT_EQ(text_runs_hit(&r, 40, 5, NULL), 5);      /* between cells */
    ASSERT_EQ(text_runs_hit(&r, 500, 5, NULL), 12);    /* right of the row */
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_empty_is_minus_one(void)
{
    TEST_BEGIN();
    TextRuns r;
    text_runs_init(&r);
    ASSERT_EQ(text_runs_hit(&r, 1, 1, NULL), -1);
    ASSERT_EQ(text_runs_hit(NULL, 1, 1, NULL), -1);
    ASSERT_EQ(text_runs_run_at(&r, 1, 1), -1);
    TEST_END();
}

int test_text_runs_span_px_partial_full_and_none(void)
{
    TEST_BEGIN();
    TextRuns r;
    build_fixture(&r);
    const TextRun *t = &r.v[1];   /* "w\xC3\xB6rld" [6,12) */
    const int *c = r.cum + t->first_cum, *b = r.cum_byte + t->first_cum;
    int x0 = -1, x1 = -1;
    ASSERT_TRUE(text_runs_span_px(t->start, t->end, c, b, t->n_cum, 0, 99, &x0, &x1));
    ASSERT_EQ(x0, 0);  ASSERT_EQ(x1, 30);
    /* from the umlaut (byte 7) to after "r" (byte 10) */
    ASSERT_TRUE(text_runs_span_px(t->start, t->end, c, b, t->n_cum, 7, 10, &x0, &x1));
    ASSERT_EQ(x0, 6);  ASSERT_EQ(x1, 18);
    /* a range that starts inside the umlaut takes the next boundary */
    ASSERT_TRUE(text_runs_span_px(t->start, t->end, c, b, t->n_cum, 8, 12, &x0, &x1));
    ASSERT_EQ(x0, 12); ASSERT_EQ(x1, 30);
    ASSERT_FALSE(text_runs_span_px(t->start, t->end, c, b, t->n_cum, 0, 6, &x0, &x1));
    ASSERT_FALSE(text_runs_span_px(t->start, t->end, c, b, t->n_cum, 12, 20, &x0, &x1));
    ASSERT_FALSE(text_runs_span_px(t->start, t->end, c, b, t->n_cum, 9, 9, &x0, &x1));
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_run_at_finds_the_rect(void)
{
    TEST_BEGIN();
    TextRuns r;
    build_fixture(&r);
    ASSERT_EQ(text_runs_run_at(&r, 50, 3), 1);
    ASSERT_EQ(text_runs_run_at(&r, 12, 15), 2);
    ASSERT_EQ(text_runs_run_at(&r, 300, 3), -1);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_word_prose_code_and_synthetic(void)
{
    TEST_BEGIN();
    static const char T[] = "make  all && cd /tmp";
    TextRun prose, code, syn;
    memset(&prose, 0, sizeof(prose));
    prose.start = 0; prose.end = 6;        /* "make  " */
    int s = -1, e = -1;
    text_runs_word(T, (int)strlen(T), &prose, 2, &s, &e);
    ASSERT_EQ(s, 0); ASSERT_EQ(e, 4);
    memset(&code, 0, sizeof(code));
    code.start = 0; code.end = (int)strlen(T); code.flags = TR_FLAG_CODE;
    text_runs_word(T, (int)strlen(T), &code, 10, &s, &e);   /* on "&&" */
    ASSERT_EQ(s, 10); ASSERT_EQ(e, 12);
    text_runs_word(T, (int)strlen(T), &code, 12, &s, &e);   /* just after it */
    ASSERT_EQ(s, 10); ASSERT_EQ(e, 12);
    text_runs_word(T, (int)strlen(T), &code, 5, &s, &e);    /* between spaces */
    ASSERT_EQ(s, 5); ASSERT_EQ(e, 5);
    memset(&syn, 0, sizeof(syn));
    syn.start = 0; syn.end = 6; strcpy(syn.synthetic, "- ");
    text_runs_word(T, (int)strlen(T), &syn, 2, &s, &e);
    ASSERT_EQ(s, e);
    TEST_END();
}

int test_text_line_at_and_next_line(void)
{
    TEST_BEGIN();
    static const char T[] = "one\ntwo\r\n\nfour";
    int s = -1, e = -1;
    text_line_at(T, (int)strlen(T), 5, &s, &e);
    ASSERT_EQ(s, 4); ASSERT_EQ(e, 8);          /* "two\r": the bytes as stored */
    text_line_at(T, (int)strlen(T), 9, &s, &e);
    ASSERT_EQ(s, 9); ASSERT_EQ(e, 9);          /* the empty line */
    text_line_at(T, (int)strlen(T), 99, &s, &e);
    ASSERT_EQ(s, 10); ASSERT_EQ(e, 14);
    int pos = 0, ls, le, n = 0;
    int want[4][2] = { { 0, 3 }, { 4, 7 }, { 9, 9 }, { 10, 14 } };
    while (text_next_line(T, (int)strlen(T), &pos, &ls, &le)) {
        ASSERT_TRUE(n < 4);
        if (n < 4) { ASSERT_EQ(ls, want[n][0]); ASSERT_EQ(le, want[n][1]); }
        n++;
    }
    ASSERT_EQ(n, 4);
    pos = 0; n = 0;
    while (text_next_line("a\n", 2, &pos, &ls, &le)) n++;
    ASSERT_EQ(n, 1);                           /* no line after a final '\n' */
    pos = 0;
    ASSERT_TRUE(text_next_line("x\ry", 3, &pos, &ls, &le));
    ASSERT_EQ(le, 3);                          /* a lone CR is text */
    TEST_END();
}
```
In `tests/runner.c` insert immediately before the line `/* test_md_blocks.c */`:

```c
/* test_text_runs.c */
int test_text_runs_cum_ascii_multibyte_and_surrogates(void);
int test_text_runs_cum_invalid_bytes_and_limits(void);
int test_text_runs_utf8_prefix_never_splits_a_pair(void);
int test_text_runs_append_assigns_lines_and_boundaries(void);
int test_text_runs_hit_inside_a_run_takes_the_nearest_boundary(void);
int test_text_runs_hit_left_right_and_between(void);
int test_text_runs_hit_above_below_and_between_lines(void);
int test_text_runs_hit_synthetic_maps_to_the_next_real_run(void);
int test_text_runs_hit_reports_the_thinking_part(void);
int test_text_runs_hit_table_cells_share_a_line_by_y(void);
int test_text_runs_hit_empty_is_minus_one(void);
int test_text_runs_span_px_partial_full_and_none(void);
int test_text_runs_run_at_finds_the_rect(void);
int test_text_runs_word_prose_code_and_synthetic(void);
int test_text_line_at_and_next_line(void);

```
and immediately before the line `    failed += test_md_blocks_none();`:

```c
    failed += test_text_runs_cum_ascii_multibyte_and_surrogates();
    failed += test_text_runs_cum_invalid_bytes_and_limits();
    failed += test_text_runs_utf8_prefix_never_splits_a_pair();
    failed += test_text_runs_append_assigns_lines_and_boundaries();
    failed += test_text_runs_hit_inside_a_run_takes_the_nearest_boundary();
    failed += test_text_runs_hit_left_right_and_between();
    failed += test_text_runs_hit_above_below_and_between_lines();
    failed += test_text_runs_hit_synthetic_maps_to_the_next_real_run();
    failed += test_text_runs_hit_reports_the_thinking_part();
    failed += test_text_runs_hit_table_cells_share_a_line_by_y();
    failed += test_text_runs_hit_empty_is_minus_one();
    failed += test_text_runs_span_px_partial_full_and_none();
    failed += test_text_runs_run_at_finds_the_rect();
    failed += test_text_runs_word_prose_code_and_synthetic();
    failed += test_text_line_at_and_next_line();

```
- [ ] **Step 3.2: Run them and see them fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make test 2>&1 | grep -m1 "text_runs.h"
```
Expected: `fatal error: text_runs.h: No such file or directory` (from `tests/test_text_runs.c`).

- [ ] **Step 3.3: Implement**

Create `src/core/text_runs.h`:

```c
/* src/core/text_runs.h */
#ifndef NUTSHELL_TEXT_RUNS_H
#define NUTSHELL_TEXT_RUNS_H

/*
 * The geometry the chat panel's paint pass records for character selection
 * (spec docs/superpowers/specs/2026-10-18-ai-panel-text-selection-design.md
 * sections 3, 4 and 6). Every word the renderer draws is appended as a run:
 * its rect in list-view client pixels, its byte range into the source text
 * of one part of one message, and the cumulative pixel width at each UTF-8
 * character boundary inside it. The mouse is hit-tested against these by
 * lookup; nothing here measures text. Portable, no windows.h.
 */

/* Parts of a message. Ordered as they are painted (the Thinking body sits
 * above the reply text), so (message id, part, offset) compares in reading
 * order. */
#define TR_PART_THINKING 0
#define TR_PART_MAIN     1

/* Run flags. */
#define TR_FLAG_CODE 1   /* a fenced code line: double-click takes a token */

typedef struct {
    int x, y, w, h;      /* list-local pixels, as painted this frame */
    int start, end;      /* byte range into the part's source text, end
                          * exclusive; a synthetic run spans the source bytes
                          * of its marker ("- ", "12. "), which is what the
                          * copy carries when the range covers them */
    int part;            /* TR_PART_MAIN or TR_PART_THINKING */
    int line;            /* visual line: runs with the same y and part share it */
    int first_cum;       /* index into cum[]/cum_byte[] of the first boundary */
    int n_cum;           /* boundaries: UTF-8 characters + 1; 0 for synthetic */
    int flags;           /* TR_FLAG_* */
    char synthetic[8];   /* "- " or "3. " for a bullet or number, else "" */
} TextRun;

typedef struct {
    TextRun *v; int n, cap;
    int *cum;        /* cumulative pixel width at each boundary (0 first) */
    int *cum_byte;   /* byte offset from the run's start at each boundary */
    int cum_n, cum_cap;
    int line_count;
} TextRuns;

void text_runs_init(TextRuns *r);
/* Forget every run, keep the memory (called at the start of every paint). */
void text_runs_reset(TextRuns *r);
void text_runs_free(TextRuns *r);

/* Append one run. x, y, w, h, start, end, part, flags and synthetic come
 * from *proto; line, first_cum and n_cum are assigned here. `cum` and
 * `cum_byte` hold n_cum boundaries (none for a synthetic run). A run with
 * the same y and part as the previous one continues its visual line; any
 * other run starts a new line. Returns the run's index, or -1 when memory
 * runs out (nothing is appended then). */
int text_runs_append(TextRuns *r, const TextRun *proto,
                     const int *cum, const int *cum_byte, int n_cum);

/* Boundaries of the UTF-8 string s[0, len) from the cumulative extents
 * GetTextExtentExPointW reports for its UTF-16 form (dx16[i] is the width
 * of the first i + 1 UTF-16 units, n16 of them). Writes one boundary per
 * character plus the leading 0: cum[k] is the width and cum_byte[k] the
 * byte offset after k characters. A four-byte sequence is one character and
 * two UTF-16 units (a surrogate pair); an invalid byte is one character and
 * one unit, as MultiByteToWideChar replaces it with U+FFFD. Units past n16
 * take the last width. Returns the number written, or -1 when it would pass
 * `cap` or an argument is NULL. */
int text_runs_cum_from_utf16(const char *s, int len, const int *dx16, int n16,
                             int *cum, int *cum_byte, int cap);

/* Bytes of s[0, len) that make up the first n16 UTF-16 units, whole
 * characters only (a surrogate pair is never split). */
int text_runs_utf8_prefix_for_utf16(const char *s, int len, int n16);

/* The part of a run's pixels that lies inside the selection [sel_start,
 * sel_end): *x0 and *x1 relative to the run's x, at character boundaries.
 * Returns 1 when that part is non-empty, 0 otherwise (also for n_cum < 2). */
int text_runs_span_px(int run_start, int run_end,
                      const int *cum, const int *cum_byte, int n_cum,
                      int sel_start, int sel_end, int *x0, int *x1);

/* Spec section 4. The byte offset in the source of the part the point falls
 * in, the part written to *part_out (may be NULL); -1 when there are no runs.
 *  - The visual line is the first run whose [y, y + h) holds the point, and
 *    every run with that y and part; above every run the first run's start,
 *    below every run the last run's end; in a gap between lines the line
 *    above.
 *  - On the line: left of the first run its start, right of the last run
 *    its end, between two runs the end of the left one, inside a run the
 *    nearest character boundary.
 *  - A synthetic run (bullet, number) gives the start of the next real run
 *    on its line, or its own end when there is none. */
int text_runs_hit(const TextRuns *r, int x, int y, int *part_out);

/* Index of the run whose rect holds the point, or -1. */
int text_runs_run_at(const TextRuns *r, int x, int y);

/* Double-click: the word at byte `off` of `run` in text[0, text_len). A
 * prose run gives its own bytes less trailing spaces and tabs; a code run
 * (TR_FLAG_CODE) gives the whitespace-delimited token holding `off` (or
 * ending at it); a synthetic run gives nothing (*s == *e). */
void text_runs_word(const char *text, int text_len, const TextRun *run,
                    int off, int *s, int *e);

/* Triple-click: the source line holding byte `off` of text[0, len), as
 * [*s, *e) without its '\n'. */
void text_line_at(const char *text, int len, int off, int *s, int *e);

/* Line iterator for the plain (MD_PLAIN) layout: set *pos = 0 and call
 * until it returns 0. Each line is [*ls, *le), its '\n' excluded and also a
 * '\r' right before that '\n' (a CR LF ends a line; a lone '\r' elsewhere
 * is text). A text ending in '\n' has no empty line after it. */
int text_next_line(const char *s, int len, int *pos, int *ls, int *le);

#endif /* NUTSHELL_TEXT_RUNS_H */
```
Create `src/core/text_runs.c`:

```c
/* src/core/text_runs.c -- see text_runs.h. */
#include "text_runs.h"
#include <stdlib.h>
#include <string.h>

void text_runs_init(TextRuns *r)
{
    if (r) memset(r, 0, sizeof(*r));
}

void text_runs_reset(TextRuns *r)
{
    if (!r) return;
    r->n = 0;
    r->cum_n = 0;
    r->line_count = 0;
}

void text_runs_free(TextRuns *r)
{
    if (!r) return;
    free(r->v);
    free(r->cum);
    free(r->cum_byte);
    memset(r, 0, sizeof(*r));
}

/* Grow r->v to hold at least `need` runs. */
static int grow_runs(TextRuns *r, int need)
{
    if (need <= r->cap) return 1;
    int cap = r->cap ? r->cap * 2 : 64;
    while (cap < need) cap *= 2;
    TextRun *v = (TextRun *)realloc(r->v, (size_t)cap * sizeof(TextRun));
    if (!v) return 0;
    r->v = v;
    r->cap = cap;
    return 1;
}

/* Grow the two boundary arrays together to hold at least `need`. */
static int grow_cum(TextRuns *r, int need)
{
    if (need <= r->cum_cap) return 1;
    int cap = r->cum_cap ? r->cum_cap * 2 : 256;
    while (cap < need) cap *= 2;
    int *c = (int *)realloc(r->cum, (size_t)cap * sizeof(int));
    if (!c) return 0;
    r->cum = c;
    int *b = (int *)realloc(r->cum_byte, (size_t)cap * sizeof(int));
    if (!b) return 0;   /* r->cum is larger than needed; harmless */
    r->cum_byte = b;
    r->cum_cap = cap;
    return 1;
}

int text_runs_append(TextRuns *r, const TextRun *proto,
                     const int *cum, const int *cum_byte, int n_cum)
{
    if (!r || !proto || n_cum < 0) return -1;
    if (n_cum > 0 && (!cum || !cum_byte)) return -1;
    if (!grow_runs(r, r->n + 1)) return -1;
    if (n_cum > 0 && !grow_cum(r, r->cum_n + n_cum)) return -1;

    TextRun *t = &r->v[r->n];
    *t = *proto;
    t->synthetic[sizeof(t->synthetic) - 1] = '\0';
    if (n_cum > 0) {
        memcpy(r->cum + r->cum_n, cum, (size_t)n_cum * sizeof(int));
        memcpy(r->cum_byte + r->cum_n, cum_byte, (size_t)n_cum * sizeof(int));
    }
    t->first_cum = r->cum_n;
    t->n_cum = n_cum;
    r->cum_n += n_cum;

    if (r->n > 0 && r->v[r->n - 1].y == t->y && r->v[r->n - 1].part == t->part)
        t->line = r->v[r->n - 1].line;
    else
        t->line = r->line_count++;
    return r->n++;
}

/* Length of the UTF-8 character starting at s[i] (i < len): 1 for ASCII,
 * an invalid byte or a sequence cut short, else 2 to 4. */
static int utf8_char_len(const char *s, int i, int len)
{
    unsigned char c = (unsigned char)s[i];
    int n;
    if (c < 0x80) return 1;
    else if ((c & 0xE0) == 0xC0) n = 2;
    else if ((c & 0xF0) == 0xE0) n = 3;
    else if ((c & 0xF8) == 0xF0) n = 4;
    else return 1;
    if (i + n > len) return 1;
    for (int k = 1; k < n; k++)
        if (((unsigned char)s[i + k] & 0xC0) != 0x80) return 1;
    return n;
}

int text_runs_cum_from_utf16(const char *s, int len, const int *dx16, int n16,
                             int *cum, int *cum_byte, int cap)
{
    if (!s || !cum || !cum_byte || cap < 1 || len < 0) return -1;
    if (n16 > 0 && !dx16) return -1;
    int k = 0, i = 0, u = 0;
    cum[k] = 0;
    cum_byte[k] = 0;
    k++;
    while (i < len) {
        int cl = utf8_char_len(s, i, len);
        i += cl;
        u += (cl == 4) ? 2 : 1;
        if (k >= cap) return -1;
        int ui = u < n16 ? u : n16;
        cum[k] = ui > 0 ? dx16[ui - 1] : 0;
        cum_byte[k] = i;
        k++;
    }
    return k;
}

int text_runs_utf8_prefix_for_utf16(const char *s, int len, int n16)
{
    if (!s || len <= 0 || n16 <= 0) return 0;
    int i = 0, u = 0;
    while (i < len) {
        int cl = utf8_char_len(s, i, len);
        int units = (cl == 4) ? 2 : 1;
        if (u + units > n16) break;
        u += units;
        i += cl;
    }
    return i;
}

int text_runs_span_px(int run_start, int run_end,
                      const int *cum, const int *cum_byte, int n_cum,
                      int sel_start, int sel_end, int *x0, int *x1)
{
    if (!cum || !cum_byte || !x0 || !x1 || n_cum < 2) return 0;
    if (sel_start >= sel_end || sel_end <= run_start || sel_start >= run_end)
        return 0;
    int lo = (sel_start > run_start ? sel_start : run_start) - run_start;
    int hi = (sel_end < run_end ? sel_end : run_end) - run_start;
    int a = -1, b = -1;
    for (int k = 0; k < n_cum; k++) {
        if (a < 0 && cum_byte[k] >= lo) a = k;
        if (cum_byte[k] <= hi) b = k;
    }
    if (a < 0 || b < 0 || cum[b] <= cum[a]) return 0;
    *x0 = cum[a];
    *x1 = cum[b];
    return 1;
}

/* Offset a synthetic run resolves to: the start of the next real run on its
 * line (smallest x right of it), else its own end. */
static int synthetic_target(const TextRuns *r, int si)
{
    const TextRun *s = &r->v[si];
    int best = -1;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (t->line != s->line || t->synthetic[0] || t->x <= s->x) continue;
        if (best < 0 || t->x < r->v[best].x) best = i;
    }
    return best >= 0 ? r->v[best].start : s->end;
}

/* Offset for the left edge of run i: its start, or a synthetic's target. */
static int run_left_offset(const TextRuns *r, int i)
{
    return r->v[i].synthetic[0] ? synthetic_target(r, i) : r->v[i].start;
}

/* Nearest character boundary in run i to x. */
static int nearest_boundary(const TextRuns *r, int i, int x)
{
    const TextRun *t = &r->v[i];
    if (t->synthetic[0]) return synthetic_target(r, i);
    if (t->n_cum < 1) return t->start;
    int best = 0, best_d = -1;
    for (int k = 0; k < t->n_cum; k++) {
        int d = t->x + r->cum[t->first_cum + k] - x;
        if (d < 0) d = -d;
        if (best_d < 0 || d < best_d) { best = k; best_d = d; }
    }
    return t->start + r->cum_byte[t->first_cum + best];
}

int text_runs_hit(const TextRuns *r, int x, int y, int *part_out)
{
    if (!r || r->n <= 0) return -1;

    /* The visual line: a run that holds y, else the nearest one above. */
    int li = -1;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (y >= t->y && y < t->y + t->h) { li = i; break; }
    }
    if (li < 0) {
        int top = r->v[0].y, above = -1;
        for (int i = 0; i < r->n; i++) {
            const TextRun *t = &r->v[i];
            if (t->y < top) top = t->y;
            if (t->y + t->h <= y &&
                (above < 0 || t->y + t->h > r->v[above].y + r->v[above].h))
                above = i;
        }
        if (y < top) {
            if (part_out) *part_out = r->v[0].part;
            return run_left_offset(r, 0);
        }
        int below_all = 1;
        for (int i = 0; i < r->n; i++)
            if (r->v[i].y + r->v[i].h > y) { below_all = 0; break; }
        if (below_all || above < 0) {
            if (part_out) *part_out = r->v[r->n - 1].part;
            return r->v[r->n - 1].end;
        }
        li = above;
    }

    int ly = r->v[li].y, part = r->v[li].part;
    if (part_out) *part_out = part;

    /* Runs on that line: same y and part (table cells of one row share a y
     * but were appended as separate lines, so match on y, not line). */
    int left = -1, right = -1, left_of = -1;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (t->y != ly || t->part != part) continue;
        if (left < 0 || t->x < r->v[left].x) left = i;
        if (right < 0 || t->x > r->v[right].x) right = i;
        if (x >= t->x && x < t->x + t->w) return nearest_boundary(r, i, x);
        if (t->x + t->w <= x &&
            (left_of < 0 || t->x + t->w > r->v[left_of].x + r->v[left_of].w))
            left_of = i;
    }
    if (x < r->v[left].x) return run_left_offset(r, left);
    if (x >= r->v[right].x + r->v[right].w) return r->v[right].end;
    if (left_of >= 0) {
        if (r->v[left_of].synthetic[0]) return synthetic_target(r, left_of);
        return r->v[left_of].end;
    }
    return run_left_offset(r, left);
}

int text_runs_run_at(const TextRuns *r, int x, int y)
{
    if (!r) return -1;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h)
            return i;
    }
    return -1;
}

static int is_ws(char c) { return c == ' ' || c == '\t'; }

void text_runs_word(const char *text, int text_len, const TextRun *run,
                    int off, int *s, int *e)
{
    if (!s || !e) return;
    *s = *e = off;
    if (!text || !run || run->synthetic[0]) return;
    int rs = run->start, re = run->end;
    if (rs < 0) rs = 0;
    if (re > text_len) re = text_len;
    if (rs >= re) return;
    if (!(run->flags & TR_FLAG_CODE)) {
        while (re > rs && is_ws(text[re - 1])) re--;
        *s = rs;
        *e = re;
        return;
    }
    if (off < rs) off = rs;
    if (off > re) off = re;
    int a;
    if (off < re && !is_ws(text[off])) a = off;
    else if (off > rs && !is_ws(text[off - 1])) a = off - 1;
    else { *s = *e = off; return; }
    int b = a;
    while (a > rs && !is_ws(text[a - 1])) a--;
    while (b < re && !is_ws(text[b])) b++;
    *s = a;
    *e = b;
}

void text_line_at(const char *text, int len, int off, int *s, int *e)
{
    if (!s || !e) return;
    if (!text || len <= 0) { *s = *e = 0; return; }
    if (off < 0) off = 0;
    if (off > len) off = len;
    int a = off, b = off;
    while (a > 0 && text[a - 1] != '\n') a--;
    while (b < len && text[b] != '\n') b++;
    *s = a;
    *e = b;
}

int text_next_line(const char *s, int len, int *pos, int *ls, int *le)
{
    if (!s || !pos || !ls || !le || len <= 0) return 0;
    int p = *pos;
    if (p < 0 || p >= len) return 0;
    int q = p;
    while (q < len && s[q] != '\n') q++;
    int end = q;
    if (q < len && end > p && s[end - 1] == '\r') end--;
    *ls = p;
    *le = end;
    *pos = (q < len) ? q + 1 : q;
    return 1;
}
```
- [ ] **Step 3.4: Run the tests and the strict checks**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | grep -E "^\[FAIL|Assertion|Tests Run"
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/text_runs.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/text_runs.c
```
Expected: no `[FAIL]`, `Tests Run: 2980, Failed: 0`; both compilers print nothing.

- [ ] **Step 3.5: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/core/text_runs.h src/core/text_runs.c tests/test_text_runs.c tests/runner.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
feat(core): text runs and the hit-test for chat selection

The geometry half of character selection: the runs the paint pass
will record, cumulative widths turned from UTF-16 into UTF-8
boundaries, the hit-test of spec section 4, the selected pixels of a
run for the highlight, and the word and line helpers for double- and
triple-click. cum_byte sits beside cum[] so the hit-test turns a
boundary into a byte offset without the text.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 4: The item carries its runs; `chat_msg_find()`

`ChatMsgItem` gains the run sets the paint pass fills, the painted height the wintest compares, and the Thinking layout cache; the list gains a lookup by id, which is how every selection end is resolved (spec sections 3 and 5).

**Files:**
- Modify: `src/core/chat_msg.h` -- include (~line 7), `ChatMsgItem` (~lines 22-63), declaration after `chat_msg_count()` (~line 104)
- Modify: `src/core/chat_msg.c` -- `chat_msg_append()` (~17-66), `free_item()` (~68-92), new function after `chat_msg_count()` (~214)
- Test: `tests/test_chat_msg.c` (append), `tests/runner.c`

**Interfaces:**
- Consumes (Task 3): `TextRuns`, `text_runs_init()`, `text_runs_free()`, `text_runs_append()`, `TR_PART_MAIN`.
- Produces:
  - `ChatMsgItem` fields `TextRuns runs; TextRuns pinned_runs; int painted_height;` and, in `u.ai`, `int think_cache_len, think_cache_w, think_cache_line_h, think_cache_full_h, think_cache_text_w;` (`think_cache_len` starts at -1).
  - `ChatMsgItem *chat_msg_find(const ChatMsgList *list, int id);`

- [ ] **Step 4.1: Write the failing tests**

Append to `tests/test_chat_msg.c`:

```c
/* --- selection support: runs on the item, lookup by id (spec 2026-10-18) --- */

int test_chat_msg_find_by_id(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "b");
    ASSERT_TRUE(chat_msg_find(&list, a->id) == a);
    ASSERT_TRUE(chat_msg_find(&list, b->id) == b);
    int gone = a->id;
    chat_msg_remove(&list, a);
    ASSERT_NULL(chat_msg_find(&list, gone));
    ASSERT_NULL(chat_msg_find(NULL, 1));
    /* ids are never reused, even after a clear */
    int b_id = b->id;
    chat_msg_list_clear(&list);
    ChatMsgItem *c = chat_msg_append(&list, CHAT_ITEM_USER, "c");
    ASSERT_TRUE(c->id > b_id);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_item_starts_without_runs_and_frees_them(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *ai = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "hello");
    ASSERT_EQ(ai->runs.n, 0);
    ASSERT_EQ(ai->pinned_runs.n, 0);
    ASSERT_EQ(ai->painted_height, 0);
    ASSERT_EQ(ai->u.ai.think_cache_len, -1);
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.w = 10; t.h = 10; t.end = 5; t.part = TR_PART_MAIN;
    int cum[2] = { 0, 10 }, cb[2] = { 0, 5 };
    ASSERT_EQ(text_runs_append(&ai->runs, &t, cum, cb, 2), 0);
    ASSERT_EQ(text_runs_append(&ai->pinned_runs, &t, cum, cb, 2), 0);
    chat_msg_remove(&list, ai);      /* frees both run sets */
    ASSERT_EQ(list.count, 0);
    TEST_END();
}
```
In `tests/runner.c`, after the line `int test_utf8_cut_at_degenerate_args(void);` add:

```c
int test_chat_msg_find_by_id(void);
int test_chat_msg_item_starts_without_runs_and_frees_them(void);
```
and after the line `    failed += test_utf8_cut_at_degenerate_args();` add:

```c
    failed += test_chat_msg_find_by_id();
    failed += test_chat_msg_item_starts_without_runs_and_frees_them();
```
- [ ] **Step 4.2: Run them and see them fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make test 2>&1 | grep -m3 "error"
```
Expected: compile errors such as `implicit declaration of function 'chat_msg_find'` and `'ChatMsgItem' has no member named 'runs'`.

- [ ] **Step 4.3: Implement**

In `src/core/chat_msg.h`, after `#include "chat_approval.h"` add:

```c
#include "text_runs.h"
```
Replace

```c
    char *text;
    size_t text_len;

    union {
```
with

```c
    char *text;
    size_t text_len;

    /* Character selection (spec 2026-10-18 section 3): what the list view's
     * last paint drew of this item, reset for every item at the start of
     * each paint, so an item that was not painted has none. `runs` holds the
     * main text and the Thinking body (TextRun.part); `pinned_runs` the copy
     * of a user bubble pinned at the top of the list. painted_height is the
     * height that paint actually used (0 when not painted). */
    TextRuns runs;
    TextRuns pinned_runs;
    int painted_height;

    union {
```
In the `ai` struct, after the `thinking_user_set` member and its comment (the comment ends `change thinking_collapsed on this item again. */`), add:

```c
            /* The Thinking body's wrapped height, cached by the list view
             * per (text length, box width, line height) so a hover move
             * costs no layout; think_cache_len = -1 means nothing cached.
             * think_cache_text_w is the width the text was laid out at
             * (the box less its inset and, when it overflows, the
             * scrollbar), shared by measure and paint. */
            int think_cache_len;
            int think_cache_w;
            int think_cache_line_h;
            int think_cache_full_h;
            int think_cache_text_w;
```
Replace

```c
/* Get item count. */
int chat_msg_count(const ChatMsgList *list);
```
with

```c
/* Get item count. */
int chat_msg_count(const ChatMsgList *list);

/* The item with this id, or NULL (ids are unique and never reused). */
ChatMsgItem *chat_msg_find(const ChatMsgList *list, int id);
```
In `src/core/chat_msg.c`, `chat_msg_append()`: replace

```c
    item->text_len = len;

    /* Type-specific defaults */
```
with

```c
    item->text_len = len;
    text_runs_init(&item->runs);
    text_runs_init(&item->pinned_runs);
    item->painted_height = 0;

    /* Type-specific defaults */
```
and replace

```c
        item->u.ai.thinking_autoscroll = 1;
    } else if
```
with

```c
        item->u.ai.thinking_autoscroll = 1;
        item->u.ai.think_cache_len = -1;
    } else if
```
In `free_item()` replace

```c
static void free_item(ChatMsgItem *item)
{
    if (!item) return;
```
with

```c
static void free_item(ChatMsgItem *item)
{
    if (!item) return;
    text_runs_free(&item->runs);
    text_runs_free(&item->pinned_runs);
```
Insert immediately before `int chat_msg_batch_index(const ChatMsgList *list, const ChatMsgItem *item)`:

```c
ChatMsgItem *chat_msg_find(const ChatMsgList *list, int id)
{
    for (ChatMsgItem *it = list ? list->head : NULL; it; it = it->next)
        if (it->id == id) return it;
    return NULL;
}

```
- [ ] **Step 4.4: Run the tests and the strict checks**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | grep -E "^\[FAIL|Assertion|Tests Run"
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/chat_msg.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/chat_msg.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/ui/chat_listview.c src/ui/ai_chat.c
```
Expected: `Tests Run: 2982, Failed: 0`; the compilers print nothing.

- [ ] **Step 4.5: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/core/chat_msg.h src/core/chat_msg.c tests/test_chat_msg.c tests/runner.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
feat(core): chat items carry their paint runs; find an item by id

Each item keeps the runs the list view records when it paints it (and
a second set for the pinned copy of a user bubble), the height that
paint used, and a cache for the Thinking layout. chat_msg_find() is
how a selection end is resolved on every use.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 5: `chat_copy` -- the visible text of a selection

Section 2 in full: `chat_copy_visible()` walks a part's text as the painter does and writes what it shows; `chat_copy_selection()` joins the parts of a selection across items; `chat_sel_all()` is Ctrl+A. Includes the two corrections Review Focus 1, 4 and 5 call for (marker by its bytes, leading whitespace as the painter drops it, the painter's column count).

**Files:**
- Create: `src/core/chat_copy.h`, `src/core/chat_copy.c`
- Modify: `src/core/md_table.h` (table limits, ~line 10), `src/ui/md_render.c` (drop its own two defines, ~lines 55-56)
- Test: `tests/test_chat_copy.c` (new), `tests/runner.c`

**Interfaces:**
- Consumes: `chat_exec_split()`, `ChatSeg`, `CHAT_SEG_*` (Task 2); `TR_PART_*` (Task 3); `chat_msg_find()`, `ChatMsgList`, `ChatMsgItem` (Task 4); `md_classify_line()`, `md_parse_inline()`, `md_is_table_separator()` (`markdown.h`); `md_table_split_row()`, `md_table_alignments()` (`md_table.h`).
- Produces:
  - `#define MD_TABLE_MAX_ROWS 64`, `#define MD_TABLE_MAX_COLS 16` in `md_table.h` (moved from `md_render.c`).
  - `#define CHAT_COPY_MD 1`, `#define CHAT_COPY_EXEC 2`.
  - `int chat_copy_visible(const char *text, int len, int start, int end, int flags, char **out);` -- heap string in `*out`, returns its length, -1 on NULL argument or OOM. The spec's `markdown` argument is this `flags` word: an AI reply with markdown off still splits at `[EXEC]` (the painter does), which a single 0/1 flag cannot say.
  - `typedef struct { int msg_id; int part; int off; } ChatSelPos;`
  - `int chat_sel_cmp(ChatSelPos a, ChatSelPos b);`
  - `int chat_sel_part_range(ChatSelPos a, ChatSelPos b, int msg_id, int part, int part_len, int *s, int *e);`
  - `size_t chat_copy_selection(const ChatMsgList *list, ChatSelPos a, ChatSelPos b, int markdown, char **out);`
  - `int chat_sel_all(const ChatMsgList *list, ChatSelPos *a, ChatSelPos *b);`

Two rules here differ from the spec's wording, both so the copy equals the highlight (see Review Focus): a fence line and the table alignment row copy as nothing *including* their line break (the spec gives only the rule line and the empty line their break); and a `\n` is copied for every newline byte inside the range, which equals "after every source line but the last" except that a range ending at the start of a line keeps the break before it.

- [ ] **Step 5.1: Write the failing tests**

Create `tests/test_chat_copy.c`:

```c
/* tests/test_chat_copy.c -- the visible text of a selection (spec
 * 2026-10-18 section 2): chat_copy_visible() per line kind, the [EXEC]
 * segment walk, and the join across items. */
#include "test_framework.h"
#include "chat_copy.h"
#include "text_runs.h"
#include <stdlib.h>
#include <string.h>

#define MDX (CHAT_COPY_MD | CHAT_COPY_EXEC)

/* chat_copy_visible over [s, e) of t, compared with want. */
static int copy_is(const char *t, int s, int e, int flags, const char *want)
{
    char *out = NULL;
    int n = chat_copy_visible(t, (int)strlen(t), s, e, flags, &out);
    int ok = out && n == (int)strlen(want) && strcmp(out, want) == 0;
    if (!ok) printf("  got \"%s\" want \"%s\"\n", out ? out : "(null)", want);
    free(out);
    return ok;
}

/* Byte offset of the first occurrence of `needle` in t. */
static int at(const char *t, const char *needle)
{
    const char *p = strstr(t, needle);
    return p ? (int)(p - t) : -1;
}

static const char GOLD_SRC[] =
    "## Disk **Usage**\n"
    "Intro with `code` and *em*.\n"
    "\n"
    "- one\n"
    "- two\n"
    "1. a\n"
    "1. b\n"
    "\n"
    "> quote **bold**\n"
    "---\n"
    "| A | B \\| C |\n"
    "|---|---|\n"
    "| x | y |\n"
    "\n"
    "```sh\n"
    "cd a && make\n"
    "```\n"
    "Done.";

static const char GOLD_COPY[] =
    "Disk Usage\n"
    "Intro with code and em.\n"
    "\n"
    "- one\n"
    "- two\n"
    "1. a\n"
    "2. b\n"
    "\n"
    "quote bold\n"
    "\n"
    "A\tB \\| C\n"
    "x\ty\n"
    "\n"
    "cd a && make\n"
    "Done.";

int test_chat_copy_whole_fixture_matches_golden(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(copy_is(GOLD_SRC, 0, (int)strlen(GOLD_SRC), MDX, GOLD_COPY));
    TEST_END();
}

int test_chat_copy_prose_inside_one_line_drops_markers(void)
{
    TEST_BEGIN();
    const char *t = "Intro with `code` and **bold** text";
    /* from "with" to the end of "bold": the backticks and ** never copy */
    ASSERT_TRUE(copy_is(t, at(t, "with"), at(t, "** text"), MDX,
                        "with code and bold"));
    /* starting on a marker byte copies from the next visible byte */
    ASSERT_TRUE(copy_is(t, at(t, "`code"), at(t, "` and"), MDX, "code"));
    TEST_END();
}

int test_chat_copy_across_line_break_and_empty_line(void)
{
    TEST_BEGIN();
    const char *t = "p1\n\np2";
    ASSERT_TRUE(copy_is(t, 0, 6, MDX, "p1\n\np2"));
    ASSERT_TRUE(copy_is(t, 1, 5, MDX, "1\n\np"));
    ASSERT_TRUE(copy_is(t, 0, 2, MDX, "p1"));       /* ends before the '\n' */
    ASSERT_TRUE(copy_is(t, 0, 3, MDX, "p1\n"));
    TEST_END();
}

int test_chat_copy_bullet_marker_follows_its_bytes(void)
{
    TEST_BEGIN();
    const char *t = "a\n- b";
    ASSERT_TRUE(copy_is(t, 0, 2, MDX, "a\n"));      /* line 2 untouched */
    ASSERT_TRUE(copy_is(t, 0, 3, MDX, "a\n- "));    /* covers the marker */
    ASSERT_TRUE(copy_is(t, 3, 5, MDX, "- b"));      /* its last byte */
    ASSERT_TRUE(copy_is(t, 4, 5, MDX, "b"));        /* a word: no marker */
    ASSERT_TRUE(copy_is(t, 0, 5, MDX, "a\n- b"));
    TEST_END();
}

int test_chat_copy_ordered_list_copies_the_displayed_number(void)
{
    TEST_BEGIN();
    const char *t = "1. a\n1. b\n7. c";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "1. a\n2. b\n3. c"));
    /* item 2 alone, marker included, is numbered 2; a word in it alone
     * carries no number */
    ASSERT_TRUE(copy_is(t, at(t, "1. b"), at(t, "b") + 1, MDX, "2. b"));
    ASSERT_TRUE(copy_is(t, at(t, "b"), at(t, "b") + 1, MDX, "b"));
    /* a paragraph or blank line restarts the count, as the painter does */
    ASSERT_TRUE(copy_is("1. a\n\n1. b", 0, 10, MDX, "1. a\n\n1. b"));
    TEST_END();
}

int test_chat_copy_fence_lines_copy_nothing(void)
{
    TEST_BEGIN();
    const char *t = "x\n```\ny  z\n```\nw";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "x\ny  z\nw"));
    /* a range inside the code keeps its spaces verbatim */
    ASSERT_TRUE(copy_is(t, at(t, "  z"), at(t, "z") + 1, MDX, "  z"));
    TEST_END();
}

int test_chat_copy_heading_quote_and_rule(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(copy_is("## **Note**", 0, 11, MDX, "Note"));
    ASSERT_TRUE(copy_is("> said *so*", 0, 11, MDX, "said so"));
    ASSERT_TRUE(copy_is("a\n---\nb", 0, 7, MDX, "a\n\nb"));
    TEST_END();
}

int test_chat_copy_table_escaped_pipe_and_column_cap(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(copy_is("| a \\| b | c |", 0, 14, MDX, "a \\| b\tc"));
    /* 18 cells and no alignment row: the painter draws 16 */
    const char *wide =
        "|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|";
    ASSERT_TRUE(copy_is(wide, 0, (int)strlen(wide), MDX,
                        "1\t2\t3\t4\t5\t6\t7\t8\t9\t10\t11\t12\t13\t14\t15\t16"));
    TEST_END();
}

int test_chat_copy_table_columns_follow_the_alignment_row(void)
{
    TEST_BEGIN();
    /* Review Focus 5: two alignment cells -> the painter draws two columns,
     * header and body alike; a short body row draws an empty last cell. */
    const char *t = "| a | b | c |\n|---|---|\n| x | y | z |\n| q |";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "a\tb\nx\ty\nq\t"));
    TEST_END();
}

int test_chat_copy_exec_segment_and_state_reset(void)
{
    TEST_BEGIN();
    const char *t = "Run this:\n\n[EXEC]ls -la[/EXEC]\nDone.";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "Run this:\nls -la\n\nDone."));
    /* the fence opened before the command does not survive it: after the
     * segment the parser starts afresh, so the second ``` opens a new block */
    const char *r = "```bash\n[EXEC]ls[/EXEC]\n```\nDone.";
    ASSERT_TRUE(copy_is(r, 0, (int)strlen(r), MDX, "ls\n\nDone."));
    /* a range inside the command */
    ASSERT_TRUE(copy_is(t, at(t, "-la"), at(t, "[/EXEC]"), MDX, "-la"));
    TEST_END();
}

int test_chat_copy_ordered_list_restarts_after_exec(void)
{
    TEST_BEGIN();
    /* Review Focus 4: the segment reset restarts the numbering */
    const char *t = "1. a\n[EXEC]x[/EXEC]\n1. b";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "1. a\nx\n\n1. b"));
    TEST_END();
}

int test_chat_copy_leading_whitespace_is_not_copied(void)
{
    TEST_BEGIN();
    /* Review Focus 1: render_inline_spans drops whitespace at the start of
     * a line's content, so the copy does too (markdown only). */
    ASSERT_TRUE(copy_is("-  x\n   para **b**", 0, 18, MDX, "- x\npara b"));
    ASSERT_TRUE(copy_is("**  a**", 0, 7, MDX, "a"));
    ASSERT_TRUE(copy_is("   para", 0, 7, 0, "   para"));       /* plain keeps */
    TEST_END();
}

int test_chat_copy_plain_passes_bytes_through(void)
{
    TEST_BEGIN();
    const char *t = "**a**\n- b\n```";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), 0, t));
    /* markdown off on a reply: the [EXEC] split still applies */
    const char *r = "**x**\n[EXEC]ls[/EXEC]";
    ASSERT_TRUE(copy_is(r, 0, (int)strlen(r), CHAT_COPY_EXEC, "**x**\nls"));
    TEST_END();
}

int test_chat_copy_widens_a_cut_character(void)
{
    TEST_BEGIN();
    const char *t = "x\xC3\xA9y";        /* x e-acute y */
    ASSERT_TRUE(copy_is(t, 2, 3, 0, "\xC3\xA9"));
    ASSERT_TRUE(copy_is(t, 1, 2, 0, "\xC3\xA9"));
    ASSERT_TRUE(copy_is(t, 2, 4, MDX, "\xC3\xA9y"));
    TEST_END();
}

int test_chat_copy_empty_and_bad_arguments(void)
{
    TEST_BEGIN();
    char *out = (char *)1;
    ASSERT_EQ(chat_copy_visible("abc", 3, 2, 2, MDX, &out), 0);
    ASSERT_STR_EQ(out, "");
    free(out);
    ASSERT_EQ(chat_copy_visible(NULL, 3, 0, 3, MDX, &out), -1);
    ASSERT_NULL(out);
    ASSERT_EQ(chat_copy_visible("abc", 3, 0, 3, MDX, NULL), -1);
    /* a range wholly inside a fence line copies nothing */
    ASSERT_TRUE(copy_is("```\nx\n```", 0, 3, MDX, ""));
    TEST_END();
}

int test_chat_sel_cmp_and_part_range(void)
{
    TEST_BEGIN();
    ChatSelPos a = { 3, TR_PART_THINKING, 9 }, b = { 3, TR_PART_MAIN, 2 };
    ASSERT_TRUE(chat_sel_cmp(a, b) < 0);           /* Thinking reads first */
    ASSERT_TRUE(chat_sel_cmp(b, a) > 0);
    ChatSelPos c = { 4, TR_PART_THINKING, 0 };
    ASSERT_TRUE(chat_sel_cmp(b, c) < 0);
    ASSERT_EQ(chat_sel_cmp(a, a), 0);
    int s = -1, e = -1;
    ASSERT_TRUE(chat_sel_part_range(a, b, 3, TR_PART_THINKING, 20, &s, &e));
    ASSERT_EQ(s, 9); ASSERT_EQ(e, 20);
    ASSERT_TRUE(chat_sel_part_range(a, b, 3, TR_PART_MAIN, 50, &s, &e));
    ASSERT_EQ(s, 0); ASSERT_EQ(e, 2);
    ASSERT_FALSE(chat_sel_part_range(a, b, 4, TR_PART_MAIN, 50, &s, &e));
    ASSERT_FALSE(chat_sel_part_range(a, b, 2, TR_PART_MAIN, 50, &s, &e));
    /* an end past the part's length is clamped */
    ChatSelPos far = { 3, TR_PART_MAIN, 999 };
    ASSERT_TRUE(chat_sel_part_range(a, far, 3, TR_PART_MAIN, 50, &s, &e));
    ASSERT_EQ(e, 50);
    TEST_END();
}

/* user, AI reply with Thinking, a command card, a status line */
static void build_list(ChatMsgList *l, int thinking_open)
{
    chat_msg_list_init(l);
    chat_msg_append(l, CHAT_ITEM_USER, "hello world");
    ChatMsgItem *ai = chat_msg_append(l, CHAT_ITEM_AI_TEXT,
                                      "**Hi** there\n[EXEC]ls[/EXEC]");
    chat_msg_set_thinking(ai, "think");
    ai->u.ai.thinking_collapsed = !thinking_open;
    ChatMsgItem *cmd = chat_msg_append(l, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(cmd, "rm -rf /tmp/x", CMD_WRITE, 0);
    chat_msg_append(l, CHAT_ITEM_STATUS, "done **raw**");
}

int test_chat_copy_selection_joins_items_and_skips_cards(void)
{
    TEST_BEGIN();
    ChatMsgList l;
    build_list(&l, 0);
    int u = l.head->id, s = l.tail->id;
    ChatSelPos a = { u, TR_PART_MAIN, 6 }, b = { s, TR_PART_MAIN, 6 };
    char *out = NULL;
    size_t n = chat_copy_selection(&l, b, a, 1, &out);   /* either order */
    ASSERT_NOT_NULL(out);
    if (out) ASSERT_STR_EQ(out, "world\n\nHi there\nls\n\ndone *");
    ASSERT_EQ(n, out ? strlen(out) : 0);
    free(out);
    /* markdown off: the reply's markers stay, the [EXEC] split does not */
    n = chat_copy_selection(&l, a, b, 0, &out);
    if (out) ASSERT_STR_EQ(out, "world\n\n**Hi** there\nls\n\ndone *");
    free(out);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_copy_selection_takes_open_thinking_only(void)
{
    TEST_BEGIN();
    ChatMsgList l;
    build_list(&l, 1);
    int u = l.head->id, ai = l.head->next->id;
    ChatSelPos a = { u, TR_PART_MAIN, 6 }, b = { ai, TR_PART_MAIN, 2 };
    char *out = NULL;
    chat_copy_selection(&l, a, b, 1, &out);
    if (out) ASSERT_STR_EQ(out, "world\n\nthink");   /* "**" copies nothing */
    ASSERT_NOT_NULL(out);
    free(out);
    l.head->next->u.ai.thinking_collapsed = 1;
    ChatSelPos b2 = { ai, TR_PART_MAIN, 6 };
    chat_copy_selection(&l, a, b2, 1, &out);
    if (out) ASSERT_STR_EQ(out, "world\n\nHi");
    free(out);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_copy_selection_clamps_and_drops_vanished_ids(void)
{
    TEST_BEGIN();
    /* Review Focus 2: a reply that was replaced by a shorter text keeps a
     * selection whose end now lies past its length -- clamp, never read
     * past the text; an id that no longer resolves copies nothing. */
    ChatMsgList l;
    build_list(&l, 0);
    ChatMsgItem *ai = l.head->next;
    ChatSelPos a = { ai->id, TR_PART_MAIN, 0 }, b = { ai->id, TR_PART_MAIN, 500 };
    char *out = NULL;
    chat_msg_set_text(ai, "ok");
    size_t n = chat_copy_selection(&l, a, b, 1, &out);
    ASSERT_EQ(n, 2);
    if (out) ASSERT_STR_EQ(out, "ok");
    free(out);
    ChatSelPos gone = { 9999, TR_PART_MAIN, 0 };
    ASSERT_EQ(chat_copy_selection(&l, a, gone, 1, &out), 0);
    ASSERT_NULL(out);
    ASSERT_EQ(chat_copy_selection(&l, a, a, 1, &out), 0);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_sel_all_spans_text_items(void)
{
    TEST_BEGIN();
    ChatMsgList l;
    build_list(&l, 1);
    ChatSelPos a, b;
    ASSERT_TRUE(chat_sel_all(&l, &a, &b));
    ASSERT_EQ(a.msg_id, l.head->id);
    ASSERT_EQ(a.part, TR_PART_MAIN);
    ASSERT_EQ(a.off, 0);
    ASSERT_EQ(b.msg_id, l.tail->id);          /* the status line, not the card */
    ASSERT_EQ(b.off, (int)strlen("done **raw**"));
    char *out = NULL;
    chat_copy_selection(&l, a, b, 1, &out);
    if (out) ASSERT_STR_EQ(out,
        "hello world\n\nthink\n\nHi there\nls\n\ndone **raw**");
    free(out);
    chat_msg_list_clear(&l);
    ChatMsgList empty;
    chat_msg_list_init(&empty);
    ASSERT_FALSE(chat_sel_all(&empty, &a, &b));
    TEST_END();
}
```
In `tests/runner.c` insert immediately before the line `/* test_md_blocks.c */`:

```c
/* test_chat_copy.c */
int test_chat_copy_whole_fixture_matches_golden(void);
int test_chat_copy_prose_inside_one_line_drops_markers(void);
int test_chat_copy_across_line_break_and_empty_line(void);
int test_chat_copy_bullet_marker_follows_its_bytes(void);
int test_chat_copy_ordered_list_copies_the_displayed_number(void);
int test_chat_copy_fence_lines_copy_nothing(void);
int test_chat_copy_heading_quote_and_rule(void);
int test_chat_copy_table_escaped_pipe_and_column_cap(void);
int test_chat_copy_table_columns_follow_the_alignment_row(void);
int test_chat_copy_exec_segment_and_state_reset(void);
int test_chat_copy_ordered_list_restarts_after_exec(void);
int test_chat_copy_leading_whitespace_is_not_copied(void);
int test_chat_copy_plain_passes_bytes_through(void);
int test_chat_copy_widens_a_cut_character(void);
int test_chat_copy_empty_and_bad_arguments(void);
int test_chat_sel_cmp_and_part_range(void);
int test_chat_copy_selection_joins_items_and_skips_cards(void);
int test_chat_copy_selection_takes_open_thinking_only(void);
int test_chat_copy_selection_clamps_and_drops_vanished_ids(void);
int test_chat_sel_all_spans_text_items(void);

```
and immediately before the line `    failed += test_md_blocks_none();`:

```c
    failed += test_chat_copy_whole_fixture_matches_golden();
    failed += test_chat_copy_prose_inside_one_line_drops_markers();
    failed += test_chat_copy_across_line_break_and_empty_line();
    failed += test_chat_copy_bullet_marker_follows_its_bytes();
    failed += test_chat_copy_ordered_list_copies_the_displayed_number();
    failed += test_chat_copy_fence_lines_copy_nothing();
    failed += test_chat_copy_heading_quote_and_rule();
    failed += test_chat_copy_table_escaped_pipe_and_column_cap();
    failed += test_chat_copy_table_columns_follow_the_alignment_row();
    failed += test_chat_copy_exec_segment_and_state_reset();
    failed += test_chat_copy_ordered_list_restarts_after_exec();
    failed += test_chat_copy_leading_whitespace_is_not_copied();
    failed += test_chat_copy_plain_passes_bytes_through();
    failed += test_chat_copy_widens_a_cut_character();
    failed += test_chat_copy_empty_and_bad_arguments();
    failed += test_chat_sel_cmp_and_part_range();
    failed += test_chat_copy_selection_joins_items_and_skips_cards();
    failed += test_chat_copy_selection_takes_open_thinking_only();
    failed += test_chat_copy_selection_clamps_and_drops_vanished_ids();
    failed += test_chat_sel_all_spans_text_items();

```
- [ ] **Step 5.2: Run them and see them fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make test 2>&1 | grep -m1 "chat_copy.h"
```
Expected: `fatal error: chat_copy.h: No such file or directory` (from `tests/test_chat_copy.c`).

- [ ] **Step 5.3: Share the table limits**

In `src/core/md_table.h` replace

```c
typedef struct {
    const char *start;  /* Points into the caller's line buffer. */
```
with

```c
/* Table block limits, shared by the renderer (src/ui/md_render.c) and the
 * copier (src/core/chat_copy.c) so both draw and copy the same cells: a
 * block holds at most MD_TABLE_MAX_ROWS consecutive table lines (header,
 * alignment row and body; the next line starts a new block) and draws at
 * most MD_TABLE_MAX_COLS columns. */
#define MD_TABLE_MAX_ROWS 64
#define MD_TABLE_MAX_COLS 16

typedef struct {
    const char *start;  /* Points into the caller's line buffer. */
```
In `src/ui/md_render.c` delete these two lines (it includes `md_table.h` already):

```c
#define MD_TABLE_MAX_ROWS   64  /* header + separator + body rows, capped */
#define MD_TABLE_MAX_COLS   16
```
- [ ] **Step 5.4: Implement the copier**

Create `src/core/chat_copy.h`:

```c
/* src/core/chat_copy.h */
#ifndef NUTSHELL_CHAT_COPY_H
#define NUTSHELL_CHAT_COPY_H

/*
 * Visible-text extraction for the AI Assist panel's character selection
 * (spec docs/superpowers/specs/2026-10-18-ai-panel-text-selection-design.md
 * section 2). No geometry: every newline in the source is a hard break the
 * renderer never joins, so the visible text of a byte range is a pure
 * function of the source. This walks the text exactly as the painter does --
 * the same [EXEC] split (chat_exec_split), the same line classifier and
 * inline parser (markdown.h), the same list numbering and table columns
 * (md_table.h) -- and writes what the painter shows. Portable, no windows.h.
 */

#include <stddef.h>
#include "chat_msg.h"

/* Flags for chat_copy_visible(). */
#define CHAT_COPY_MD    1   /* prose is markdown (the render setting is on) */
#define CHAT_COPY_EXEC  2   /* the text is an AI reply: split at [EXEC] */

/* The visible text of text[start, end) (len bytes of text). A range edge
 * inside a multi-byte character is widened to take the whole character.
 *  - CHAT_COPY_EXEC: walk the segments of chat_exec_split(); a command
 *    segment copies its bytes; consecutive segments the range touches are
 *    joined with '\n' (they paint on separate lines). Without the flag the
 *    whole text is one prose segment.
 *  - CHAT_COPY_MD: per segment, the markdown state (fence, list number,
 *    table block) starts afresh, as md_render runs once per segment; per
 *    source line: prose, heading, list and quote content goes through
 *    md_parse_inline, markers never copied, whitespace at the start of the
 *    content dropped as the painter drops it; a list item gets "- " or its
 *    displayed number when the range covers a byte of its marker; fenced
 *    code lines copy verbatim, fence lines, rules and the table alignment
 *    row copy nothing; a table row copies its cells (md_table_split_row, as
 *    many columns as the painter draws) joined with '\t'.
 *    Without the flag a prose segment copies its bytes as they are.
 *  - A '\n' byte inside the range is copied as '\n' (between lines, and for
 *    the blank lines a range spans); nothing else is inserted.
 * Writes a heap, NUL-terminated string to *out (the caller frees it) and
 * returns its length; returns -1 with *out = NULL on a NULL argument or
 * when memory runs out. An empty range gives "" and 0. */
int chat_copy_visible(const char *text, int len, int start, int end,
                      int flags, char **out);

/* One end of a selection: a message (by its id), a part of it (TR_PART_*
 * in text_runs.h) and a byte offset into that part's source text. */
typedef struct { int msg_id; int part; int off; } ChatSelPos;

/* Reading order: message id (ids only grow along the list), then part
 * (the Thinking body before the reply text), then offset. <0, 0, >0. */
int chat_sel_cmp(ChatSelPos a, ChatSelPos b);

/* The part of the ordered selection [a, b) that lies in part `part` of
 * message `msg_id`, whose text is part_len bytes: [*s, *e). Offsets past
 * part_len are clamped. Returns 1 when non-empty, 0 otherwise. */
int chat_sel_part_range(ChatSelPos a, ChatSelPos b, int msg_id, int part,
                        int part_len, int *s, int *e);

/* The visible text of the selection between a and b (either order) across
 * the list: command items skipped, the Thinking body only while expanded,
 * a user bubble, status and tool line as their bytes, an AI reply through
 * chat_copy_visible(CHAT_COPY_EXEC | (markdown ? CHAT_COPY_MD : 0)); the
 * non-empty pieces joined with "\n\n". Writes a heap string to *out (caller
 * frees) and returns its length; returns 0 with *out = NULL when a or b no
 * longer names a message in the list, the selection is empty or memory runs
 * out. */
size_t chat_copy_selection(const ChatMsgList *list, ChatSelPos a,
                           ChatSelPos b, int markdown, char **out);

/* Select all: from offset 0 of the first item with text (its Thinking body
 * when that is expanded) to the end of the last item with text, command
 * items skipped. Returns 1 and fills *a and *b, or 0 when there is none. */
int chat_sel_all(const ChatMsgList *list, ChatSelPos *a, ChatSelPos *b);

#endif /* NUTSHELL_CHAT_COPY_H */
```
Create `src/core/chat_copy.c`:

```c
/* src/core/chat_copy.c -- see chat_copy.h. */
#include "chat_copy.h"
#include "chat_exec.h"
#include "markdown.h"
#include "md_table.h"
#include "text_runs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── growable output buffer ─────────────────────────────────────────── */

typedef struct { char *p; size_t n, cap; int oom; } CopyBuf;

static void cb_put(CopyBuf *b, const char *s, size_t n)
{
    if (b->oom || n == 0) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n + 1) cap *= 2;
        char *q = (char *)realloc(b->p, cap);
        if (!q) { b->oom = 1; return; }
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}

/* Copy text[a, b) intersected with [lo, hi). */
static void cb_range(CopyBuf *b, const char *text, int a, int e, int lo, int hi)
{
    if (a < lo) a = lo;
    if (e > hi) e = hi;
    if (e > a) cb_put(b, text + a, (size_t)(e - a));
}

static int is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }
static int is_ws(char c) { return c == ' ' || c == '\t'; }

/* ── one source line, NUL-terminated, for the classifier ─────────────── */

typedef struct { char *p; int cap; } LineBuf;

static const char *lb_set(LineBuf *lb, const char *s, int n)
{
    if (n + 1 > lb->cap) {
        int cap = lb->cap ? lb->cap : 256;
        while (cap < n + 1) cap *= 2;
        char *q = (char *)realloc(lb->p, (size_t)cap);
        if (!q) return NULL;
        lb->p = q;
        lb->cap = cap;
    }
    memcpy(lb->p, s, (size_t)n);
    lb->p[n] = '\0';
    return lb->p;
}

/* Span-parsed content of a line: line[cs, ce) is the content and `src`
 * the source offset of line[0]; copies what lies in [lo, hi) (source
 * offsets). Whitespace at the start of the content is skipped across spans,
 * as render_inline_spans drops it at the start of a line. */
static void emit_spans(CopyBuf *b, const char *line, int cs, int ce, int src,
                       int lo, int hi)
{
    if (ce <= cs) return;
    MdSpan spans[MD_MAX_SPANS];
    int n = md_parse_inline(line + cs, ce - cs, spans);
    int at_start = 1;
    for (int i = 0; i < n; i++) {
        int a = cs + spans[i].start, e = cs + spans[i].end;   /* in line */
        if (at_start) {
            while (a < e && is_ws(line[a])) a++;
            if (a < e) at_start = 0;
        }
        int ga = src + a, ge = src + e;                        /* in source */
        if (ga < lo) ga = lo;
        if (ge > hi) ge = hi;
        if (ge > ga) cb_put(b, line + (ga - src), (size_t)(ge - ga));
    }
}

/* ── table block, mirroring md_render_table_block ───────────────────── */

typedef struct { int ls, le; } RowSpan;

/* The rows of the block starting at line [ls, le) of seg_text[0, seg_end)
 * (offsets relative to the segment). Returns the count; *next is the
 * offset just past the last row's newline. */
static int table_rows(const char *t, int seg_end, int ls, int le,
                      RowSpan *rows, int *next, LineBuf *lb)
{
    int n = 0;
    rows[n].ls = ls;
    rows[n].le = le;
    n++;
    int p = (le < seg_end) ? le + 1 : le;
    while (p < seg_end && n < MD_TABLE_MAX_ROWS) {
        int q = p;
        while (q < seg_end && t[q] != '\n') q++;
        const char *line = lb_set(lb, t + p, q - p);
        if (!line || md_classify_line(line, 0).type != MD_LINE_TABLE) break;
        rows[n].ls = p;
        rows[n].le = q;
        n++;
        p = (q < seg_end) ? q + 1 : q;
    }
    *next = p;
    return n;
}

/* Emit one table row's cells 0..ncols-1 that the range touches, joined with
 * '\t'. `src` is the source offset of the segment start. */
static void emit_row(CopyBuf *b, const char *t, RowSpan row, int ncols,
                     int src, int lo, int hi, LineBuf *lb)
{
    const char *line = lb_set(lb, t + row.ls, row.le - row.ls);
    if (!line) return;
    MdTableCell cells[MD_TABLE_MAX_COLS];
    int count = md_table_split_row(line, cells, MD_TABLE_MAX_COLS);
    int first = -1, last = -1;
    int cs[MD_TABLE_MAX_COLS], ce[MD_TABLE_MAX_COLS];
    for (int j = 0; j < ncols; j++) {
        if (j < count) {
            cs[j] = src + row.ls + (int)(cells[j].start - line);
            ce[j] = cs[j] + cells[j].len;
        } else {
            cs[j] = ce[j] = src + row.le;     /* a missing cell: row end */
        }
        /* An empty or missing cell counts when the range reaches its
         * position (inclusive), so a whole row keeps every column. */
        int in = (cs[j] < ce[j]) ? (cs[j] < hi && ce[j] > lo)
                                 : (cs[j] >= lo && cs[j] <= hi);
        if (in) {
            if (first < 0) first = j;
            last = j;
        }
    }
    for (int j = first; j >= 0 && j <= last; j++) {
        if (j > first) cb_put(b, "\t", 1);
        if (j < count)
            emit_spans(b, line, (int)(cells[j].start - line),
                       (int)(cells[j].start - line) + cells[j].len,
                       src + row.ls, lo, hi);
    }
}

/* ── one prose segment ───────────────────────────────────────────────── */

/* Walk the markdown prose segment text[s0, s1) line by line, the state
 * reset, and copy what lies in [lo, hi). */
static void emit_markdown(CopyBuf *b, const char *text, int s0, int s1,
                          int lo, int hi, LineBuf *lb)
{
    const char *t = text + s0;      /* segment-relative */
    int seg_end = s1 - s0;
    int in_code = 0, olist = 0;
    int p = 0;
    while (p < seg_end) {
        int q = p;
        while (q < seg_end && t[q] != '\n') q++;
        int ls = s0 + p, le = s0 + q;           /* source offsets */
        const char *line = lb_set(lb, t + p, q - p);
        if (!line) { b->oom = 1; return; }
        MdLineInfo info = md_classify_line(line, in_code);

        if (info.type == MD_LINE_TABLE) {
            olist = 0;
            RowSpan rows[MD_TABLE_MAX_ROWS];
            int next = 0;
            int n = table_rows(t, seg_end, p, q, rows, &next, lb);
            int align[MD_TABLE_MAX_COLS];
            int ncols = 0;
            int has_sep = 0;
            if (n >= 2) {
                const char *sep = lb_set(lb, t + rows[1].ls, rows[1].le - rows[1].ls);
                if (sep && md_is_table_separator(sep)) {
                    has_sep = 1;
                    ncols = md_table_alignments(sep, align, MD_TABLE_MAX_COLS);
                }
            }
            const char *hdr = lb_set(lb, t + rows[0].ls, rows[0].le - rows[0].ls);
            MdTableCell hc[MD_TABLE_MAX_COLS];
            int header_n = hdr ? md_table_split_row(hdr, hc, MD_TABLE_MAX_COLS) : 0;
            if (ncols <= 0) ncols = header_n;
            if (ncols > MD_TABLE_MAX_COLS) ncols = MD_TABLE_MAX_COLS;
            if (ncols <= 0) ncols = 1;
            for (int r = 0; r < n; r++) {
                if (has_sep && r == 1) continue;   /* alignment row: nothing */
                emit_row(b, t, rows[r], ncols, s0, lo, hi, lb);
                int nl = s0 + rows[r].le;          /* the row's '\n', if any */
                if (rows[r].le < seg_end && nl >= lo && nl < hi)
                    cb_put(b, "\n", 1);
            }
            p = next;
            continue;
        }

        switch (info.type) {
        case MD_LINE_CODE_FENCE:
            in_code = !in_code;
            break;
        case MD_LINE_CODE:
            cb_range(b, text, ls, le, lo, hi);
            break;
        case MD_LINE_HEADING:
        case MD_LINE_BLOCKQUOTE:
            emit_spans(b, line, info.content_offset, q - p, ls, lo, hi);
            break;
        case MD_LINE_ULIST:
            /* the marker copies when the range covers a byte of it */
            if (ls < hi && ls + info.content_offset > lo) cb_put(b, "- ", 2);
            emit_spans(b, line, info.content_offset, q - p, ls, lo, hi);
            break;
        case MD_LINE_OLIST: {
            olist++;
            if (ls < hi && ls + info.content_offset > lo) {
                char num[16];
                int k = snprintf(num, sizeof(num), "%d. ", olist);
                if (k > 0) cb_put(b, num, (size_t)k);
            }
            emit_spans(b, line, info.content_offset, q - p, ls, lo, hi);
            break;
        }
        case MD_LINE_PARAGRAPH:
            olist = 0;
            emit_spans(b, line, 0, q - p, ls, lo, hi);
            break;
        case MD_LINE_EMPTY:
            olist = 0;
            break;
        case MD_LINE_HRULE:
        case MD_LINE_TABLE:
            break;
        }
        /* The line break, except after a fence line, which copies as
         * nothing at all (a rule and an empty line keep theirs). */
        if (info.type != MD_LINE_CODE_FENCE && q < seg_end && le >= lo && le < hi)
            cb_put(b, "\n", 1);
        p = (q < seg_end) ? q + 1 : q;
    }
}

int chat_copy_visible(const char *text, int len, int start, int end,
                      int flags, char **out)
{
    if (out) *out = NULL;
    if (!text || !out || len < 0) return -1;
    if (start < 0) start = 0;
    if (end > len) end = len;
    while (start > 0 && start < len && is_cont((unsigned char)text[start]))
        start--;
    while (end < len && end > 0 && is_cont((unsigned char)text[end]))
        end++;

    CopyBuf b = { NULL, 0, 0, 0 };
    LineBuf lb = { NULL, 0 };
    if (start < end) {
        int pos = 0, copied_any = 0, more;
        ChatSeg seg;
        if (flags & CHAT_COPY_EXEC) {
            more = chat_exec_split(text, len, &pos, &seg);
        } else {
            seg.kind = CHAT_SEG_PROSE;
            seg.start = 0;
            seg.end = len;
            more = 1;
        }
        while (more) {
            int touched = seg.start < seg.end &&
                          seg.start < end && seg.end > start;
            if (touched) {
                /* Segments paint on lines of their own: one '\n' between
                 * two that both copy something. */
                CopyBuf piece = { NULL, 0, 0, 0 };
                if (seg.kind == CHAT_SEG_PROSE && (flags & CHAT_COPY_MD))
                    emit_markdown(&piece, text, seg.start, seg.end, start, end,
                                  &lb);
                else
                    cb_range(&piece, text, seg.start, seg.end, start, end);
                if (piece.oom) b.oom = 1;
                if (piece.n > 0) {
                    if (copied_any) cb_put(&b, "\n", 1);
                    cb_put(&b, piece.p, piece.n);
                    copied_any = 1;
                }
                free(piece.p);
            }
            more = (flags & CHAT_COPY_EXEC)
                       ? chat_exec_split(text, len, &pos, &seg) : 0;
        }
    }
    free(lb.p);
    if (!b.p) {                      /* nothing copied: an empty string */
        b.p = (char *)malloc(1);
        if (b.p) b.p[0] = '\0';
    }
    if (b.oom || !b.p) { free(b.p); return -1; }
    *out = b.p;
    return (int)b.n;
}

/* ── across items ───────────────────────────────────────────────────── */

int chat_sel_cmp(ChatSelPos a, ChatSelPos b)
{
    if (a.msg_id != b.msg_id) return a.msg_id < b.msg_id ? -1 : 1;
    if (a.part != b.part) return a.part < b.part ? -1 : 1;
    if (a.off != b.off) return a.off < b.off ? -1 : 1;
    return 0;
}

int chat_sel_part_range(ChatSelPos a, ChatSelPos b, int msg_id, int part,
                        int part_len, int *s, int *e)
{
    if (!s || !e) return 0;
    *s = *e = 0;
    if (part_len < 0) return 0;
    ChatSelPos lo = { msg_id, part, 0 }, hi = { msg_id, part, part_len };
    if (chat_sel_cmp(b, lo) <= 0 || chat_sel_cmp(a, hi) >= 0) return 0;
    *s = (chat_sel_cmp(a, lo) > 0) ? a.off : 0;
    *e = (chat_sel_cmp(b, hi) < 0) ? b.off : part_len;
    if (*s < 0) *s = 0;
    if (*e > part_len) *e = part_len;
    return *e > *s;
}

static int thinking_shown(const ChatMsgItem *it)
{
    return it->type == CHAT_ITEM_AI_TEXT && it->u.ai.thinking_text &&
           it->u.ai.thinking_text[0] && !it->u.ai.thinking_collapsed;
}

/* Append the copy of one part's [s, e) as a "\n\n"-joined piece. */
static void add_piece(CopyBuf *b, const char *text, int len, int s, int e,
                      int flags)
{
    char *piece = NULL;
    int n = chat_copy_visible(text, len, s, e, flags, &piece);
    if (n < 0) { b->oom = 1; return; }
    if (n > 0) {
        if (b->n > 0) cb_put(b, "\n\n", 2);
        cb_put(b, piece, (size_t)n);
    }
    free(piece);
}

size_t chat_copy_selection(const ChatMsgList *list, ChatSelPos a,
                           ChatSelPos b, int markdown, char **out)
{
    if (out) *out = NULL;
    if (!list || !out) return 0;
    if (chat_sel_cmp(a, b) > 0) { ChatSelPos t = a; a = b; b = t; }
    if (chat_sel_cmp(a, b) == 0) return 0;
    const ChatMsgItem *first = chat_msg_find(list, a.msg_id);
    if (!first || !chat_msg_find(list, b.msg_id)) return 0;

    CopyBuf buf = { NULL, 0, 0, 0 };
    for (const ChatMsgItem *it = first; it; it = it->next) {
        if (it->id > b.msg_id) break;
        if (it->type == CHAT_ITEM_COMMAND) continue;
        int s, e;
        if (thinking_shown(it)) {
            const char *th = it->u.ai.thinking_text;
            int tl = (int)strlen(th);
            if (chat_sel_part_range(a, b, it->id, TR_PART_THINKING, tl, &s, &e))
                add_piece(&buf, th, tl, s, e, 0);
        }
        const char *tx = it->text ? it->text : "";
        int len = (int)it->text_len;
        if (chat_sel_part_range(a, b, it->id, TR_PART_MAIN, len, &s, &e)) {
            int flags = 0;
            if (it->type == CHAT_ITEM_AI_TEXT)
                flags = CHAT_COPY_EXEC | (markdown ? CHAT_COPY_MD : 0);
            add_piece(&buf, tx, len, s, e, flags);
        }
    }
    if (buf.oom || buf.n == 0) { free(buf.p); return 0; }
    *out = buf.p;
    return buf.n;
}

int chat_sel_all(const ChatMsgList *list, ChatSelPos *a, ChatSelPos *b)
{
    if (!list || !a || !b) return 0;
    const ChatMsgItem *first = NULL, *last = NULL;
    for (const ChatMsgItem *it = list->head; it; it = it->next) {
        if (it->type == CHAT_ITEM_COMMAND) continue;
        if (it->text_len == 0 && !thinking_shown(it)) continue;
        if (!first) first = it;
        last = it;
    }
    if (!first) return 0;
    a->msg_id = first->id;
    a->part = thinking_shown(first) ? TR_PART_THINKING : TR_PART_MAIN;
    a->off = 0;
    b->msg_id = last->id;
    if (last->text_len > 0) {
        b->part = TR_PART_MAIN;
        b->off = (int)last->text_len;
    } else {
        b->part = TR_PART_THINKING;
        b->off = (int)strlen(last->u.ai.thinking_text);
    }
    return chat_sel_cmp(*a, *b) < 0;
}
```
- [ ] **Step 5.5: Run the tests and the strict checks**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | grep -E "^\[FAIL|Assertion|got |Tests Run"
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/chat_copy.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/chat_copy.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/ui/md_render.c
```
Expected: no `[FAIL]`, `Tests Run: 3002, Failed: 0`; the compilers print nothing.

- [ ] **Step 5.6: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/core/chat_copy.h src/core/chat_copy.c src/core/md_table.h src/ui/md_render.c tests/test_chat_copy.c tests/runner.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
feat(core): copy the visible text of a chat selection

chat_copy_visible() walks a reply as the painter does -- the same
[EXEC] segments with the markdown state reset per segment, the same
line classifier, inline parser, list numbering and table columns --
and writes what the panel shows: no markers, "- " or the displayed
number for a list item whose marker the range covers, cells joined
with tabs, code verbatim. chat_copy_selection() joins items with a
blank line, skips command cards and takes the Thinking body only while
it is open. The table limits move to md_table.h so the renderer and
the copier share them.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 6: Painted words equal copied text (native)

The test that keeps sections 2 and 3 from diverging (spec section 7): a native model of the paint pass -- same segments, same line walk, same tokens, same base-offset arithmetic as `md_render.c` will use in Task 7 -- records runs; every run must carry the bytes of the word it was told to draw, and per source line the painted words must equal `chat_copy_visible()` over that line.

**Files:**
- Create: `tests/fake_md_painter.h`, `tests/fake_md_painter.c` (test support, compiled into the native runner like `tests/fake_dlog.c`)
- Test: `tests/test_chat_copy_paint.c` (new), `tests/runner.c`

**Interfaces:**
- Consumes: `chat_exec_split()` (Task 2); `TextRuns`, `text_runs_append()`, `text_next_line()`, `TR_FLAG_CODE`, `TR_PART_MAIN` (Task 3); `chat_copy_visible()`, `CHAT_COPY_*` (Task 5); `markdown.h`, `md_table.h`.
- Produces (tests only): `FakePaint`, `FakeLine`, `void fake_paint(const char *text, int len, int markdown, FakePaint *fp);`, `void fake_paint_free(FakePaint *fp);`, `int fake_line_text(const FakePaint *fp, const char *text, int i, char *out, int cap);`.

The model's rules, each one `md_render.c` follows after Task 7: a synthetic run spans its marker's bytes `[line start, line start + content_offset)`; a code line is one run; an empty line a zero-width run; table cells sit at `row offset + (cell.start - row text)`; prose tokens come from `md_next_word` over `md_parse_inline` spans with whitespace dropped at a line start; `MD_MODE_PLAIN` (commands, markdown off) keeps every token. Commands containing `\r` are not used as fixtures: `text_next_line()` hides a CR before LF from the painter while the copier copies bytes.

- [ ] **Step 6.1: Write the failing test**

Create `tests/test_chat_copy_paint.c`:

```c
/* tests/test_chat_copy_paint.c -- the two walkers must agree (spec
 * 2026-10-18 section 7): the runs a model of the paint pass records carry
 * the bytes of the words it drew (base-offset arithmetic at every level:
 * [EXEC] segment, line, content offset, table cell), and per source line
 * the painted words equal chat_copy_visible() over that line. */
#include "test_framework.h"
#include "fake_md_painter.h"
#include "chat_copy.h"
#include <stdlib.h>
#include <string.h>

static const char *const FIXTURES[] = {
    /* every line kind */
    "## Disk **Usage**\n"
    "Intro with `code` and *em*.\n"
    "\n"
    "- one\n"
    "- two\n"
    "1. a\n"
    "1. b\n"
    "\n"
    "> quote **bold**\n"
    "---\n"
    "| A | B \\| C |\n"
    "|---|---|\n"
    "| x | y |\n"
    "\n"
    "```sh\n"
    "cd a && make\n"
    "```\n"
    "Done.",
    /* [EXEC] segments between markdown, a heading, a list and a table */
    "Here:\n\n[EXEC]df -h /[/EXEC]\n## After **it**\n1. a\n1. b\n"
    "[EXEC]  indented cmd[/EXEC]\n- x `y` z\n| h1 | h2 |\n|:-|-:|\n"
    "| a \\| b | c |\n| only |\n\n> q *e*",
    /* the parser state resets across a segment */
    "```bash\n[EXEC]ls[/EXEC]\n```\nDone.",
    /* Review Focus 1: whitespace at the start of the content */
    "-  x\n   para **b**\n**  a**\n1.   n",
    /* more cells than the painter draws, a ragged block */
    "|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|\n| a | b | c |\n"
    "|---|---|\n| x | y | z |",
    /* multi-byte text */
    "Gr\xC3\xBC\xC3\x9F" "e **\xC3\xA9t\xC3\xA9**\n- \xC3\xBCn\xC3\xAF "
    "\xF0\x9F\x98\x80 done",
};
#define N_FIXTURES ((int)(sizeof(FIXTURES) / sizeof(FIXTURES[0])))

static FakePaint *paint(const char *t, int markdown)
{
    FakePaint *fp = (FakePaint *)calloc(1, sizeof(FakePaint));
    if (fp) fake_paint(t, (int)strlen(t), markdown, fp);
    return fp;
}

int test_paint_runs_carry_the_bytes_they_draw(void)
{
    TEST_BEGIN();
    for (int f = 0; f < N_FIXTURES; f++) {
        const char *t = FIXTURES[f];
        int len = (int)strlen(t);
        FakePaint *fp = paint(t, 1);
        ASSERT_NOT_NULL(fp);
        if (!fp) continue;
        ASSERT_EQ(fp->offset_errors, 0);
        for (int i = 0; i < fp->runs.n; i++) {
            const TextRun *r = &fp->runs.v[i];
            ASSERT_TRUE(r->start >= 0 && r->start <= r->end && r->end <= len);
            /* no run ever holds a marker byte sequence */
            if (!r->synthetic[0] && r->end > r->start) {
                ASSERT_TRUE(memchr(t + r->start, '\n',
                                   (size_t)(r->end - r->start)) == NULL);
            }
        }
        fake_paint_free(fp);
        free(fp);
    }
    TEST_END();
}

static int lines_agree(const char *t, int markdown)
{
    int ok = 1;
    int len = (int)strlen(t);
    int flags = CHAT_COPY_EXEC | (markdown ? CHAT_COPY_MD : 0);
    FakePaint *fp = paint(t, markdown);
    if (!fp) return 0;
    for (int i = 0; i < fp->n_lines; i++) {
        char painted[512];
        fake_line_text(fp, t, i, painted, (int)sizeof(painted));
        char *copied = NULL;
        chat_copy_visible(t, len, fp->lines[i].ls, fp->lines[i].le, flags,
                          &copied);
        if (!copied || strcmp(painted, copied) != 0) {
            printf("  line %d [%d,%d): painted \"%s\" copied \"%s\"\n", i,
                   fp->lines[i].ls, fp->lines[i].le, painted,
                   copied ? copied : "(null)");
            ok = 0;
        }
        free(copied);
    }
    fake_paint_free(fp);
    free(fp);
    return ok;
}

int test_painted_words_equal_copied_text(void)
{
    TEST_BEGIN();
    for (int f = 0; f < N_FIXTURES; f++)
        ASSERT_TRUE(lines_agree(FIXTURES[f], 1));
    TEST_END();
}

int test_painted_words_equal_copied_text_markdown_off(void)
{
    TEST_BEGIN();
    for (int f = 0; f < N_FIXTURES; f++)
        ASSERT_TRUE(lines_agree(FIXTURES[f], 0));
    TEST_END();
}

int test_paint_synthetic_runs_cover_their_marker(void)
{
    TEST_BEGIN();
    const char *t = "1. a\n1. b";
    FakePaint *fp = paint(t, 1);
    ASSERT_NOT_NULL(fp);
    if (fp) {
        ASSERT_STR_EQ(fp->runs.v[0].synthetic, "1. ");
        ASSERT_EQ(fp->runs.v[0].start, 0);
        ASSERT_EQ(fp->runs.v[0].end, 3);
        ASSERT_STR_EQ(fp->runs.v[2].synthetic, "2. ");
        ASSERT_EQ(fp->runs.v[2].start, 5);
        fake_paint_free(fp);
        free(fp);
    }
    TEST_END();
}
```
In `tests/runner.c` insert immediately before the line `/* test_md_blocks.c */`:

```c
/* test_chat_copy_paint.c */
int test_paint_runs_carry_the_bytes_they_draw(void);
int test_painted_words_equal_copied_text(void);
int test_painted_words_equal_copied_text_markdown_off(void);
int test_paint_synthetic_runs_cover_their_marker(void);

```
and immediately before the line `    failed += test_md_blocks_none();`:

```c
    failed += test_paint_runs_carry_the_bytes_they_draw();
    failed += test_painted_words_equal_copied_text();
    failed += test_painted_words_equal_copied_text_markdown_off();
    failed += test_paint_synthetic_runs_cover_their_marker();

```
- [ ] **Step 6.2: Run it and see it fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make test 2>&1 | grep -m1 "fake_md_painter.h"
```
Expected: `fatal error: fake_md_painter.h: No such file or directory`.

- [ ] **Step 6.3: Write the model of the paint pass**

Create `tests/fake_md_painter.h`:

```c
/* tests/fake_md_painter.h -- a native model of the chat panel's paint pass,
 * for the painted-equals-copied tests (spec 2026-10-18 section 7). */
#ifndef NUTSHELL_FAKE_MD_PAINTER_H
#define NUTSHELL_FAKE_MD_PAINTER_H

#include "text_runs.h"

#define FAKE_MAX_RUNS  1024
#define FAKE_MAX_LINES 256

/* One source line as the fake painter walked it. ncols > 0 for a table
 * row (its column count), -1 for a table alignment row, 0 otherwise. */
typedef struct { int ls, le; int first_run, n_runs; int ncols; } FakeLine;

typedef struct {
    TextRuns runs;
    int      cell_of[FAKE_MAX_RUNS];   /* table column of each run, else -1 */
    FakeLine lines[FAKE_MAX_LINES];
    int      n_lines;
    int      offset_errors;            /* runs whose bytes != the word drawn */
} FakePaint;

/* Walk an AI reply's text the way chat_listview + md_render paint it --
 * segments from chat_exec_split, md_render_core's line walk per segment
 * (state reset), render_inline_spans' tokens (md_next_word over
 * md_parse_inline spans, whitespace dropped at a line start), one run per
 * code line, a synthetic run per bullet or number, table cells at their
 * offset in the row, MD_PLAIN lines for commands and plain prose -- with no
 * wrapping (one visual line per source line), appending runs with the same
 * base-offset arithmetic md_render.c uses. `fp` must be zeroed first. */
void fake_paint(const char *text, int len, int markdown, FakePaint *fp);
void fake_paint_free(FakePaint *fp);

/* The painted text of fp->lines[i]: each run's synthetic text or bytes in
 * order; for a table row, column by column joined with '\t' (ncols
 * columns, empty ones included). Returns the length written to out. */
int fake_line_text(const FakePaint *fp, const char *text, int i,
                   char *out, int cap);

#endif
```
Create `tests/fake_md_painter.c`:

```c
/* tests/fake_md_painter.c -- see fake_md_painter.h. Keep in step with
 * src/ui/md_render.c: every rule here is one the real renderer follows. */
#include "fake_md_painter.h"
#include "chat_exec.h"
#include "markdown.h"
#include "md_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_line(FakePaint *fp, int ls, int le, int ncols)
{
    if (fp->n_lines >= FAKE_MAX_LINES) return;
    FakeLine *l = &fp->lines[fp->n_lines++];
    l->ls = ls; l->le = le; l->ncols = ncols;
    l->first_run = fp->runs.n;
    l->n_runs = 0;
}

/* Append one run for source bytes [start, end); `word` is the pointer the
 * renderer would have handed TextOutW (into its own line copy). */
static void add_run(FakePaint *fp, const char *text, int start, int end,
                    const char *word, int flags, const char *synth, int cell)
{
    if (fp->runs.n >= FAKE_MAX_RUNS) return;
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.x = fp->runs.n * 10;               /* left to right, never wrapping */
    t.y = fp->n_lines * 20;
    t.w = synth ? 10 : 6 * (end - start);
    t.h = 16;
    t.start = start; t.end = end;
    t.part = TR_PART_MAIN; t.flags = flags;
    if (synth) snprintf(t.synthetic, sizeof(t.synthetic), "%s", synth);
    else if (word && end > start &&
             memcmp(text + start, word, (size_t)(end - start)) != 0)
        fp->offset_errors++;
    int cum[2] = { 0, t.w }, cb[2] = { 0, end - start };
    int idx = text_runs_append(&fp->runs, &t, cum, cb, synth ? 0 : 2);
    if (idx >= 0) {
        fp->cell_of[idx] = cell;
        if (fp->n_lines > 0) fp->lines[fp->n_lines - 1].n_runs++;
    }
}

/* render_inline_spans: tokens of line[cs, ce); src = source offset of
 * line[0]. Whitespace tokens at the start of the line are dropped. */
static void tokens(FakePaint *fp, const char *text, const char *line,
                   int cs, int ce, int src, int cell)
{
    if (ce <= cs) return;
    const char *c = line + cs;
    MdSpan spans[MD_MAX_SPANS];
    int n = md_parse_inline(c, ce - cs, spans);
    int at_start = 1;
    for (int i = 0; i < n; i++) {
        int off = spans[i].start;
        while (off < spans[i].end) {
            int ws = 0, we = 0;
            if (!md_next_word(c, spans[i].end, off, &ws, &we)) break;
            int is_ws = (c[ws] == ' ' || c[ws] == '\t');
            if (!(is_ws && at_start)) {
                add_run(fp, text, src + cs + ws, src + cs + we, c + ws, 0,
                        NULL, cell);
                at_start = 0;
            }
            off = we;
        }
    }
}

/* MD_PLAIN: every token kept, leading whitespace included. */
static void plain(FakePaint *fp, const char *text, int s0, int s1)
{
    int pos = 0, ls, le;
    while (text_next_line(text + s0, s1 - s0, &pos, &ls, &le)) {
        add_line(fp, s0 + ls, s0 + le, 0);
        if (le == ls) {
            add_run(fp, text, s0 + ls, s0 + ls, NULL, 0, NULL, -1);
            continue;
        }
        const char *line = text + s0 + ls;
        int off = 0;
        while (off < le - ls) {
            int ws = 0, we = 0;
            if (!md_next_word(line, le - ls, off, &ws, &we)) break;
            add_run(fp, text, s0 + ls + ws, s0 + ls + we, line + ws, 0,
                    NULL, -1);
            off = we;
        }
    }
}

static char *dup_line(const char *s, int n)
{
    char *p = (char *)malloc((size_t)n + 1);
    if (!p) return NULL;
    memcpy(p, s, (size_t)n);
    p[n] = '\0';
    return p;
}

/* md_render_core over the segment text[s0, s1), with base s0. */
static void markdown_seg(FakePaint *fp, const char *text, int s0, int s1)
{
    int in_code = 0, olist = 0, p = s0;
    while (p < s1) {
        int q = p;
        while (q < s1 && text[q] != '\n') q++;
        char *line = dup_line(text + p, q - p);
        if (!line) return;
        MdLineInfo info = md_classify_line(line, in_code);

        if (info.type == MD_LINE_TABLE) {
            olist = 0;
            int rs[MD_TABLE_MAX_ROWS], re[MD_TABLE_MAX_ROWS], n = 0;
            rs[n] = p; re[n] = q; n++;
            int r = (q < s1) ? q + 1 : q;
            while (r < s1 && n < MD_TABLE_MAX_ROWS) {
                int e = r;
                while (e < s1 && text[e] != '\n') e++;
                char *l2 = dup_line(text + r, e - r);
                int is_t = l2 && md_classify_line(l2, 0).type == MD_LINE_TABLE;
                free(l2);
                if (!is_t) break;
                rs[n] = r; re[n] = e; n++;
                r = (e < s1) ? e + 1 : e;
            }
            char *h = dup_line(text + rs[0], re[0] - rs[0]);
            char *sep = n >= 2 ? dup_line(text + rs[1], re[1] - rs[1]) : NULL;
            int align[MD_TABLE_MAX_COLS];
            int has_sep = sep && md_is_table_separator(sep);
            int ncols = has_sep ? md_table_alignments(sep, align, MD_TABLE_MAX_COLS) : 0;
            MdTableCell hc[MD_TABLE_MAX_COLS];
            int header_n = h ? md_table_split_row(h, hc, MD_TABLE_MAX_COLS) : 0;
            if (ncols <= 0) ncols = header_n;
            if (ncols > MD_TABLE_MAX_COLS) ncols = MD_TABLE_MAX_COLS;
            if (ncols <= 0) ncols = 1;
            free(h);
            free(sep);
            for (int k = 0; k < n; k++) {
                if (has_sep && k == 1) { add_line(fp, rs[k], re[k], -1); continue; }
                add_line(fp, rs[k], re[k], ncols);
                char *row = dup_line(text + rs[k], re[k] - rs[k]);
                if (!row) continue;
                MdTableCell cells[MD_TABLE_MAX_COLS];
                int count = md_table_split_row(row, cells, MD_TABLE_MAX_COLS);
                for (int j = 0; j < ncols && j < count; j++) {
                    int coff = (int)(cells[j].start - row);
                    tokens(fp, text, row, coff, coff + cells[j].len, rs[k], j);
                }
                free(row);
            }
            free(line);
            p = r;
            continue;
        }

        add_line(fp, p, q, 0);
        switch (info.type) {
        case MD_LINE_CODE_FENCE:
            in_code = !in_code;
            break;
        case MD_LINE_CODE:
            add_run(fp, text, p, q, line, TR_FLAG_CODE, NULL, -1);
            break;
        case MD_LINE_HEADING:
        case MD_LINE_BLOCKQUOTE:
            tokens(fp, text, line, info.content_offset, q - p, p, -1);
            break;
        case MD_LINE_ULIST:
            add_run(fp, text, p, p + info.content_offset, NULL, 0, "- ", -1);
            tokens(fp, text, line, info.content_offset, q - p, p, -1);
            break;
        case MD_LINE_OLIST: {
            char num[16];
            snprintf(num, sizeof(num), "%d. ", ++olist);
            add_run(fp, text, p, p + info.content_offset, NULL, 0, num, -1);
            tokens(fp, text, line, info.content_offset, q - p, p, -1);
            break;
        }
        case MD_LINE_PARAGRAPH:
            olist = 0;
            tokens(fp, text, line, 0, q - p, p, -1);
            break;
        case MD_LINE_EMPTY:
            olist = 0;
            add_run(fp, text, p, p, NULL, 0, NULL, -1);
            break;
        case MD_LINE_HRULE:
        case MD_LINE_TABLE:
            break;
        }
        free(line);
        p = (q < s1) ? q + 1 : q;
    }
}

void fake_paint(const char *text, int len, int markdown, FakePaint *fp)
{
    int pos = 0;
    ChatSeg seg;
    while (chat_exec_split(text, len, &pos, &seg)) {
        if (seg.kind == CHAT_SEG_PROSE && markdown)
            markdown_seg(fp, text, seg.start, seg.end);
        else
            plain(fp, text, seg.start, seg.end);
    }
}

void fake_paint_free(FakePaint *fp)
{
    text_runs_free(&fp->runs);
}

static int put(char *out, int n, int cap, const char *s, int k)
{
    if (n + k >= cap) k = cap - 1 - n;
    if (k > 0) { memcpy(out + n, s, (size_t)k); n += k; }
    out[n] = '\0';
    return n;
}

int fake_line_text(const FakePaint *fp, const char *text, int i,
                   char *out, int cap)
{
    const FakeLine *l = &fp->lines[i];
    int n = 0;
    out[0] = '\0';
    int cols = l->ncols > 0 ? l->ncols : 1;
    for (int j = 0; j < cols; j++) {
        if (l->ncols > 0 && j > 0) n = put(out, n, cap, "\t", 1);
        for (int k = l->first_run; k < l->first_run + l->n_runs; k++) {
            const TextRun *t = &fp->runs.v[k];
            if (l->ncols > 0 && fp->cell_of[k] != j) continue;
            if (t->synthetic[0])
                n = put(out, n, cap, t->synthetic, (int)strlen(t->synthetic));
            else
                n = put(out, n, cap, text + t->start, t->end - t->start);
        }
    }
    return n;
}
```
- [ ] **Step 6.4: Run the tests**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | grep -E "^\[FAIL|Assertion|line [0-9]+ \[|Tests Run"
```
Expected: no `[FAIL]`, no `line N [a,b): painted ... copied ...` diagnostics, `Tests Run: 3006, Failed: 0`. A diagnostic names the fixture line where the walkers disagree: fix the copier (Task 5's code), never the expectation, unless the model departs from `md_render.c`.

- [ ] **Step 6.5: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add tests/fake_md_painter.h tests/fake_md_painter.c tests/test_chat_copy_paint.c tests/runner.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
test(core): painted words equal copied text

A native model of the chat paint pass records runs with the base-offset
arithmetic md_render.c uses -- [EXEC] segment, line, content offset,
table cell -- and every run must hold the bytes of the word it drew;
per source line, the painted words must equal chat_copy_visible().
This is the test that keeps the copier and the painter from drifting.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 7: `md_render` -- runs, highlight, `MD_MODE_PLAIN`, code lines on `ExtTextOutW`

The paint pass records what it draws (spec section 3) and draws the highlight under each run (section 6). `md_render_ex()` is the one entry point; the three existing functions become thin wrappers, so no caller changes in this task.

**Toolchain:** this task changes `src/ui`, which `make test` never compiles. Compile-check it with the MinGW `-fsyntax-only` command in its run step (automaton has the toolchain); where no MinGW toolchain exists, rely on `make test` and a careful read of the diff. `make clean && make release` happens once, in Task 16, after the version bump; the maintainer builds the exe and runs `mingw32-make wintest` on Windows before the PR leaves draft.

**Files:**
- Modify: `src/ui/md_render.h` (whole file)
- Modify: `src/ui/md_render.c` -- includes (~10-17), new helpers after `mdbuf_free()` (~102-108), `paint_word()` (~165-232), `render_inline_spans()` (~304-394), `md_render_table_block()` (~396-713, five edits), `md_render_core()` and the public API (~715-784)
- Modify: `Makefile` -- `WIN_ONLY_TESTS` (~141), `WIN_TEST_SRCS` (~174-183)
- Test: `tests/test_md_render_win.c` (new, `make wintest`), `tests/win_runner.c`

**Interfaces:**
- Consumes: `TextRuns`, `TextRun`, `text_runs_append()`, `text_runs_cum_from_utf16()`, `text_runs_utf8_prefix_for_utf16()`, `text_runs_span_px()`, `text_next_line()`, `TR_FLAG_CODE` (Task 3); `MD_TABLE_MAX_ROWS/COLS` (Task 5); `rgb_alpha()` (`ns_draw.h`).
- Produces (`md_render.h`):
  - `typedef struct { TextRuns *runs; int base; int part; int sel_start; int sel_end; COLORREF focus; COLORREF surface; int clip_top; int clip_bottom; } MdRunCtx;`
  - `#define MD_MODE_MARKDOWN 0`, `#define MD_MODE_PLAIN 1`
  - `int md_render_ex(HDC hdc, const char *text, int x, int y, int max_width, HFONT hFont, HFONT hMonoFont, HFONT hBoldFont, const ThemeColors *theme, int mode, int paint, COLORREF plain_clr, MdBlockOut *blocks, MdRunCtx *rc);`
  - `md_render_text()`, `md_render_text_blocks()`, `md_measure_text()` unchanged in signature and result.

What changes on screen: a code line is drawn with `ExtTextOutW(..., ETO_CLIPPED, ...)` instead of `DrawTextW`, so `cd a && make` shows both ampersands (DrawText without `DT_NOPREFIX` ate one); its height is still the mono font's `tmHeight`, as `DT_CALCRECT` gave for one line. `ExtTextOutW` with `ETO_CLIPPED` is the spec's "TextOutW clipped at the same text_w". Nothing else moves: the measure path is untouched apart from the parameters it threads through.

How runs are recorded: every drawn token is one `paint_word()` call; it measures once with `GetTextExtentExPointW` (size and per-UTF-16 extents together), converts the extents to UTF-8 boundaries, fills the selected part over the colour under it, draws, and appends the run at `src + byte offset`. `src` is threaded down: `rc->base` (the segment's offset) + the line's offset in the text + `content_offset` (headings, lists, quotes) or the cell's offset in its row (tables). A bullet or number is a synthetic run over its marker's bytes; an empty line or empty code line a zero-width run, so a click on it lands on that line. A line the selection continues past is filled from its last run to the content's right edge (not in table cells). `MD_MODE_PLAIN` walks lines with `text_next_line()` and keeps a source line's leading whitespace (until its first soft wrap) and breaks a word wider than the line at a character boundary (`plain_fit_bytes()`).

- [ ] **Step 7.1: Write the failing test**

Create `tests/test_md_render_win.c`:

```c
/* tests/test_md_render_win.c -- make wintest: the renderer behind character
 * selection on a memory DC, no window needed (spec docs/superpowers/specs/
 * 2026-10-18-ai-panel-text-selection-design.md sections 3 and 6). The
 * measure pass and the paint pass give the same height; a paint with a run
 * context records runs whose boundaries are monotonic and whose bytes are
 * the words drawn; a code line is one run with its "&&" intact; a bullet is
 * a synthetic run over its marker; MD_MODE_PLAIN breaks a word wider than
 * the line. */

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"
#include "md_render.h"
#include "text_runs.h"
#include "ui_theme.h"

static const char MD_FIX[] =
    "## Disk **Usage**\n"
    "Here's what I found on **web-01** with `du`:\n"
    "\n"
    "- Root partition: 42% used\n"
    "1. first\n"
    "| Mount | Used |\n"
    "|---|--:|\n"
    "| / | 42% |\n"
    "```bash\n"
    "cd /var/log && ls -la\n"
    "```\n"
    "Gr\xC3\xBC\xC3\x9F" "e \xF0\x9F\x98\x80 done.";

static const char PLAIN_FIX[] =
    "  indented line kept\n"
    "averyveryveryverylongwordthatcannotfitonthenarrowline end";

static HFONT mk(int weight, const char *face)
{
    return CreateFontA(-13, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, face);
}

static int runs_sane(const TextRuns *r, const char *text, int len)
{
    int bad = 0;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (t->start < 0 || t->end > len || t->start > t->end) bad++;
        for (int k = 1; k < t->n_cum; k++)
            if (r->cum[t->first_cum + k] < r->cum[t->first_cum + k - 1]) bad++;
        if (!t->synthetic[0] && t->end > t->start &&
            memchr(text + t->start, '\n', (size_t)(t->end - t->start)))
            bad++;
    }
    return bad;
}

int test_md_render_win_measure_equals_paint_and_runs(void)
{
    TEST_BEGIN();
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 800, 1200);
    HGDIOBJ oldb = SelectObject(dc, bmp);
    HFONT f = mk(FW_NORMAL, "Segoe UI"), m = mk(FW_NORMAL, "Consolas"),
          b = mk(FW_BOLD, "Segoe UI");
    const ThemeColors *th = ui_theme_get(0);

    for (int w = 160; w <= 640; w += 480) {
        TextRuns runs;
        text_runs_init(&runs);
        MdRunCtx rc;
        memset(&rc, 0, sizeof(rc));
        rc.runs = &runs;
        rc.part = TR_PART_MAIN;
        rc.sel_start = 0;
        rc.sel_end = (int)strlen(MD_FIX);
        int hm = md_render_ex(dc, MD_FIX, 0, 0, w, f, m, b, th,
                              MD_MODE_MARKDOWN, 0, 0, NULL, NULL);
        int hp = md_render_ex(dc, MD_FIX, 10, 20, w, f, m, b, th,
                              MD_MODE_MARKDOWN, 1, 0, NULL, &rc);
        ASSERT_EQ(hm, hp);
        ASSERT_TRUE(runs.n > 0);
        ASSERT_EQ(runs_sane(&runs, MD_FIX, (int)strlen(MD_FIX)), 0);
        int code = 0, bullet = 0;
        for (int i = 0; i < runs.n; i++) {
            const TextRun *t = &runs.v[i];
            if ((t->flags & TR_FLAG_CODE) &&
                memcmp(MD_FIX + t->start, "cd /var/log && ls -la", 21) == 0 &&
                t->end - t->start == 21)
                code = 1;
            if (strcmp(t->synthetic, "- ") == 0 &&
                memcmp(MD_FIX + t->start, "- ", 2) == 0 && t->end - t->start == 2)
                bullet = 1;
        }
        ASSERT_TRUE(code);
        ASSERT_TRUE(bullet);
        text_runs_free(&runs);

        TextRuns pr;
        text_runs_init(&pr);
        MdRunCtx prc;
        memset(&prc, 0, sizeof(prc));
        prc.runs = &pr;
        prc.part = TR_PART_MAIN;
        int pm = md_render_ex(dc, PLAIN_FIX, 0, 0, w, f, f, f, th,
                              MD_MODE_PLAIN, 0, 0, NULL, NULL);
        int pp = md_render_ex(dc, PLAIN_FIX, 0, 0, w, f, f, f, th,
                              MD_MODE_PLAIN, 1, 0, NULL, &prc);
        ASSERT_EQ(pm, pp);
        ASSERT_EQ(runs_sane(&pr, PLAIN_FIX, (int)strlen(PLAIN_FIX)), 0);
        /* the leading spaces are a run of their own, kept */
        ASSERT_TRUE(pr.n > 0 && pr.v[0].start == 0 && PLAIN_FIX[0] == ' ');
        /* no run is wider than the line */
        for (int i = 0; i < pr.n; i++) ASSERT_TRUE(pr.v[i].w <= w);
        text_runs_free(&pr);
    }

    SelectObject(dc, oldb);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    DeleteObject(f); DeleteObject(m); DeleteObject(b);
    TEST_END();
}

#endif /* _WIN32 */
```
In `tests/win_runner.c`, before the line `/* test_local_pty.c */` add:

```c
/* test_md_render_win.c */
int test_md_render_win_measure_equals_paint_and_runs(void);

```
and after the line `    failed += test_icons_render_at_multiple_sizes();` add:

```c
    failed += test_md_render_win_measure_equals_paint_and_runs();
```
In `Makefile` (CRLF: edit with the Edit tool) replace

```make
WIN_ONLY_TESTS = tests/test_icons.c tests/test_local_pty.c tests/win_runner.c
```
with

```make
WIN_ONLY_TESTS = tests/test_icons.c tests/test_local_pty.c tests/win_runner.c \
                 tests/test_md_render_win.c
```
and in `WIN_TEST_SRCS` replace

```make
                   src/core/dlog_types.c src/core/dlog_snapshot.c src/core/dlog_sys.c \
                   tests/test_icons.c tests/test_local_pty.c tests/win_runner.c
```
with

```make
                   src/core/dlog_types.c src/core/dlog_snapshot.c src/core/dlog_sys.c \
                   src/ui/md_render.c src/ui/ns_draw.c src/core/text_runs.c \
                   src/core/md_table.c src/core/theme.c src/core/ui_theme.c \
                   src/core/ns_scale.c src/core/ns_type.c src/core/cmd_policy.c \
                   tests/test_icons.c tests/test_local_pty.c \
                   tests/test_md_render_win.c tests/win_runner.c
```
- [ ] **Step 7.2: Run it and see it fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make wintest 2>&1 | grep -m3 -E "error"
```
Expected: compile errors in `tests/test_md_render_win.c` -- `unknown type name 'MdRunCtx'`, `implicit declaration of function 'md_render_ex'`. (`make test` is unaffected: the file is in `WIN_ONLY_TESTS`.)

- [ ] **Step 7.3: Replace `src/ui/md_render.h`**

```c
/* src/ui/md_render.h */
#ifndef NUTSHELL_MD_RENDER_H
#define NUTSHELL_MD_RENDER_H

#include <windows.h>
#include "ui_theme.h"
#include "ns_layout.h"   /* NsRect */
#include "text_runs.h"

/* Render markdown text into a GDI device context.
 * x, y: top-left position. max_width: available width for wrapping.
 * Returns the total height consumed (in pixels). */
int md_render_text(HDC hdc, const char *text, int x, int y, int max_width,
                   HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                   const ThemeColors *theme);

/* Where the fenced code blocks of a rendered text ended up, for a caller that
 * draws something on them (the copy glyph). The caller fills `rects`,
 * `max_rects` and `reserve_w`; the renderer sets `count`.
 *
 * One entry per opening fence, in the order md_code_blocks() reports them
 * (both walk the same md_classify_line classification), in the same
 * coordinates as the x/y passed to the renderer: the union of the block's
 * code-line backgrounds. A block with no body lines keeps its slot as an
 * all-zero rect. `count` is the number of fenced blocks met and may exceed
 * `max_rects` (only the first `max_rects` are written).
 *
 * `reserve_w` (px, >= 0) is kept free of text on the right of every code line
 * so the glyph never covers code; code lines never wrap, so it changes no
 * height. */
typedef struct {
    NsRect *rects;
    int     max_rects;
    int     reserve_w;
    int     count;      /* out */
} MdBlockOut;

/* md_render_text() that also reports the fenced code blocks (see above).
 * `blocks` may be NULL, which is exactly md_render_text(). */
int md_render_text_blocks(HDC hdc, const char *text, int x, int y,
                          int max_width,
                          HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                          const ThemeColors *theme, MdBlockOut *blocks);

/* Measure the height that markdown text would consume without painting.
 * Same parameters as md_render_text. */
int md_measure_text(HDC hdc, const char *text, int max_width,
                    HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                    const ThemeColors *theme);

/* Character selection (spec docs/superpowers/specs/
 * 2026-10-18-ai-panel-text-selection-design.md sections 3 and 6). Passed to
 * md_render_ex(): while painting, every word drawn is appended to `runs` as
 * a TextRun (rect, byte range, cumulative widths from one
 * GetTextExtentExPointW per run), and the part of it inside [sel_start,
 * sel_end) is filled with rgb_alpha(focus, <the colour under it>, 30%)
 * immediately before its text is drawn. Offsets are into the part's source:
 * text[0] is source offset `base`. */
typedef struct {
    TextRuns *runs;        /* append here; NULL records nothing */
    int       base;        /* source offset of text[0] in the part */
    int       part;        /* TR_PART_MAIN or TR_PART_THINKING */
    int       sel_start;   /* selection in the part's source bytes; */
    int       sel_end;     /*   sel_start >= sel_end: none */
    COLORREF  focus;       /* highlight hue: ns_tokens()->focus */
    COLORREF  surface;     /* the colour plain text sits on */
    int       clip_top;    /* record only runs meeting [clip_top, */
    int       clip_bottom; /*   clip_bottom); top >= bottom: no clip */
} MdRunCtx;

/* Layout modes for md_render_ex(). MD_MODE_PLAIN skips the parser: every
 * source line is a hard break, the text wraps word by word in hFont and
 * plain_clr, whitespace at the start of a source line is kept, and a word
 * wider than the line is broken at a character boundary. */
#define MD_MODE_MARKDOWN 0
#define MD_MODE_PLAIN    1

/* The one entry point behind the three above. paint = 0 measures (x and y
 * are ignored, nothing is drawn or recorded); paint = 1 draws, sets
 * TRANSPARENT background mode for the call and restores the text colour.
 * plain_clr is used by MD_MODE_PLAIN only. `blocks` and `rc` may be NULL.
 * Returns the height consumed, identical for paint = 0 and paint = 1 at the
 * same width. */
int md_render_ex(HDC hdc, const char *text, int x, int y, int max_width,
                 HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                 const ThemeColors *theme, int mode, int paint,
                 COLORREF plain_clr, MdBlockOut *blocks, MdRunCtx *rc);

#endif /* NUTSHELL_MD_RENDER_H */
```
- [ ] **Step 7.4: Add the helpers to `src/ui/md_render.c`**

Replace the include block

```c
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
```
with

```c
#include "theme.h"
#include "text_runs.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
```
Insert immediately before the line `/* ── Internal: measure a single word in a span's font ────────────────── */`:

```c
/* ── Character selection: runs and highlight (spec 2026-10-18 s.3, s.6) ── */

/* The highlight: the focus hue at 30% over the colour the run sits on. */
#define MD_SEL_ALPHA 0.30f

/* Boundaries of one run, from one GetTextExtentExPointW call (stack
 * buffers, heap past MD_CUM_STACK). n == 0 when they could not be had. */
#define MD_CUM_STACK 256
typedef struct {
    int  st_dx[MD_CUM_STACK];
    int  st_cum[MD_CUM_STACK + 1];
    int  st_cb[MD_CUM_STACK + 1];
    int *dx, *cum, *cb;
    int  n;
} MdCum;

static void mdcum_free(MdCum *c)
{
    if (c->dx && c->dx != c->st_dx) free(c->dx);
    if (c->cum && c->cum != c->st_cum) free(c->cum);
    if (c->cb && c->cb != c->st_cb) free(c->cb);
    c->dx = c->cum = c->cb = NULL;
    c->n = 0;
}

/* Measure `wb` (the UTF-16 form of utf8[0, byte_len)) in the font selected
 * into hdc: *sz gets the extent, c the UTF-8 boundaries. */
static void mdcum_measure(HDC hdc, const MdWBuf *wb, const char *utf8,
                          int byte_len, MdCum *c, SIZE *sz)
{
    int nb = byte_len + 1;
    c->n = 0;
    c->dx = wb->len <= MD_CUM_STACK
        ? c->st_dx : (int *)malloc((size_t)wb->len * sizeof(int));
    c->cum = nb <= MD_CUM_STACK + 1
        ? c->st_cum : (int *)malloc((size_t)nb * sizeof(int));
    c->cb = nb <= MD_CUM_STACK + 1
        ? c->st_cb : (int *)malloc((size_t)nb * sizeof(int));
    sz->cx = sz->cy = 0;
    if (!c->dx || !c->cum || !c->cb) {
        mdcum_free(c);
        GetTextExtentPoint32W(hdc, wb->ptr, wb->len, sz);
        return;
    }
    if (!GetTextExtentExPointW(hdc, wb->ptr, wb->len, 0, NULL, c->dx, sz))
        return;
    int n = text_runs_cum_from_utf16(utf8, byte_len, c->dx, wb->len,
                                     c->cum, c->cb, nb);
    c->n = n > 0 ? n : 0;
}

static void md_fill(HDC hdc, const RECT *r, COLORREF clr)
{
    if (r->right <= r->left || r->bottom <= r->top) return;
    HBRUSH br = CreateSolidBrush(clr);
    FillRect(hdc, r, br);
    DeleteObject(br);
}

static int md_has_sel(const MdRunCtx *rc)
{
    return rc && rc->sel_start < rc->sel_end;
}

/* The selected part of the run [s, e) drawn at (px, py), h tall, clipped
 * at max_right -- painted before the run's text. */
static void md_sel_fill(HDC hdc, const MdRunCtx *rc, int s, int e,
                        int px, int py, int h, int max_right,
                        const MdCum *cm, COLORREF under)
{
    if (!md_has_sel(rc) || cm->n < 2) return;
    int x0 = 0, x1 = 0;
    if (!text_runs_span_px(s, e, cm->cum, cm->cb, cm->n,
                           rc->sel_start, rc->sel_end, &x0, &x1))
        return;
    RECT r = { px + x0, py, px + x1, py + h };
    if (r.right > max_right) r.right = max_right;
    md_fill(hdc, &r, rgb_alpha(rc->focus, under, MD_SEL_ALPHA));
}

/* A bullet or number over its marker's source bytes [s, e): filled whole
 * when the selection covers a byte of the marker, as the copy then carries
 * it. */
static void md_synth_fill(HDC hdc, const MdRunCtx *rc, int s, int e,
                          int x, int y, int w, int h, COLORREF under)
{
    if (!md_has_sel(rc) || !(s < rc->sel_end && e > rc->sel_start)) return;
    RECT r = { x, y, x + w, y + h };
    md_fill(hdc, &r, rgb_alpha(rc->focus, under, MD_SEL_ALPHA));
}

/* The fill from the end of a line's text to the content's right edge when
 * the selection runs past that line. `strict` (a soft wrap at offset
 * `off`, where the next visual line starts) needs the selection to start
 * before it; a source line end (`off` is its '\n') only needs it to start
 * at or before it. */
static void md_eol_fill(HDC hdc, const MdRunCtx *rc, int off, int strict,
                        int x0, int y, int x1, int h, COLORREF under)
{
    if (!md_has_sel(rc) || x1 <= x0) return;
    int starts = strict ? rc->sel_start < off : rc->sel_start <= off;
    if (!starts || off >= rc->sel_end) return;
    RECT r = { x0, y, x1, y + h };
    md_fill(hdc, &r, rgb_alpha(rc->focus, under, MD_SEL_ALPHA));
}

/* Append a run when painting with a run list, and only when its rect meets
 * the clip band (the Thinking body is painted at full height under a clip). */
static void md_record(MdRunCtx *rc, int x, int y, int w, int h, int s, int e,
                      const MdCum *cm, int flags, const char *synth)
{
    if (!rc || !rc->runs) return;
    if (rc->clip_top < rc->clip_bottom &&
        (y + h <= rc->clip_top || y >= rc->clip_bottom))
        return;
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.x = x; t.y = y; t.w = w; t.h = h;
    t.start = s; t.end = e;
    t.part = rc->part;
    t.flags = flags;
    if (synth) {
        (void)snprintf(t.synthetic, sizeof(t.synthetic), "%s", synth);
        text_runs_append(rc->runs, &t, NULL, NULL, 0);
    } else if (cm && cm->n > 0) {
        text_runs_append(rc->runs, &t, cm->cum, cm->cb, cm->n);
    } else {
        int z[1] = { 0 };
        text_runs_append(rc->runs, &t, z, z, 1);
    }
}

/* A zero-width run for a line that draws no word (an empty line, an empty
 * code line), so a click on it lands on that line. */
static void md_record_empty(MdRunCtx *rc, int x, int y, int h, int off)
{
    md_record(rc, x, y, 0, h, off, off, NULL, 0, NULL);
}

/* MD_MODE_PLAIN: how many bytes of s[0, len) fit in `avail` px in the font
 * selected into hdc -- whole characters, never fewer than one. */
static int plain_fit_bytes(HDC hdc, const char *s, int len, int avail)
{
    MdWBuf wb;
    mdbuf_init(&wb, s, len);
    int fit = 0;
    SIZE sz = { 0, 0 };
    if (avail < 0) avail = 0;
    GetTextExtentExPointW(hdc, wb.ptr, wb.len, avail, &fit, NULL, &sz);
    mdbuf_free(&wb);
    int nb = text_runs_utf8_prefix_for_utf16(s, len, fit);
    if (nb <= 0) nb = text_runs_utf8_prefix_for_utf16(s, len, 1);
    if (nb <= 0) nb = text_runs_utf8_prefix_for_utf16(s, len, 2);
    if (nb <= 0) nb = 1;
    return nb;
}
```
- [ ] **Step 7.5: Replace `paint_word()`**

Replace everything from the line `/* ── Internal: paint a single word at (px, py) in a span's font ──────── */` up to (not including) `/* ── Table cell measurement (used for column fitting + alignment) ────── */` with:

```c
/* ── Internal: paint a single word at (px, py) in a span's font ──────── */

/* Paints text[byte_off, byte_off + byte_len) and, with a run context,
 * fills its selected part first (over `bg`, or over the inline-code
 * background for a code span) and records it as a run whose source offset
 * is `src`. One GetTextExtentExPointW gives both its size and its
 * boundaries. */
static void paint_word(HDC hdc, const char *text, int byte_off, int byte_len,
                       const MdSpan *span, int px, int py,
                       HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                       const ThemeColors *theme,
                       COLORREF default_clr, COLORREF code_clr,
                       MdRunCtx *rc, int src, COLORREF bg)
{
    if (byte_len <= 0) return;

    HFONT sel_font = hFont;
    HFONT created = NULL;
    /* Font selection per span type. KEEP IN SYNC with measure_word. */
    switch (span->type) {
    case MD_SPAN_BOLD:        sel_font = hBoldFont; break;
    case MD_SPAN_BOLD_ITALIC: {
        LOGFONT lf; GetObject(hBoldFont, sizeof(lf), &lf);
        lf.lfItalic = TRUE;
        created = CreateFontIndirect(&lf);
        sel_font = created ? created : hBoldFont;
        break;
    }
    case MD_SPAN_ITALIC: {
        LOGFONT lf; GetObject(hFont, sizeof(lf), &lf);
        lf.lfItalic = TRUE;
        created = CreateFontIndirect(&lf);
        sel_font = created ? created : hFont;
        break;
    }
    case MD_SPAN_CODE:        sel_font = hMonoFont; break;
    default:                  sel_font = hFont;     break;
    }

    HFONT old_font = (HFONT)SelectObject(hdc, sel_font);

    MdWBuf wb;
    mdbuf_init(&wb, text + byte_off, byte_len);

    SIZE sz = { 0, 0 };
    MdCum cm;
    mdcum_measure(hdc, &wb, text + byte_off, byte_len, &cm, &sz);

    /* Code background */
    COLORREF under = bg;
    if (span->type == MD_SPAN_CODE) {
        RECT cbg = { px - 1, py, px + sz.cx + 1, py + sz.cy };
        under = RGB_FROM_THEME(theme->chat.cmd_bg);
        HBRUSH br = CreateSolidBrush(under);
        FillRect(hdc, &cbg, br);
        DeleteObject(br);
    }

    /* Selection highlight: after every background, before the text. */
    md_sel_fill(hdc, rc, src, src + byte_len, px, py, sz.cy, INT_MAX, &cm,
                under);

    COLORREF clr = (span->type == MD_SPAN_CODE) ? code_clr : default_clr;
    SetTextColor(hdc, clr);

    TextOutW(hdc, px, py, wb.ptr, wb.len);

    if (span->type == MD_SPAN_STRIKETHROUGH) {
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        int mid_y = py + tm.tmHeight / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, default_clr);
        HPEN old_pen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, px, mid_y, NULL);
        LineTo(hdc, px + sz.cx, mid_y);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    md_record(rc, px, py, sz.cx, sz.cy, src, src + byte_len, &cm, 0, NULL);

    SelectObject(hdc, old_font);
    if (created) DeleteObject(created);
    mdcum_free(&cm);
    mdbuf_free(&wb);
}
```
- [ ] **Step 7.6: Replace `render_inline_spans()`**

Replace everything from `/* ── Word-by-word inline layouter ────────────────────────────────────── */` up to (not including) `/* ── Table block ──────────────────────────────────────────────────────── */` with:

```c
/* ── Word-by-word inline layouter ────────────────────────────────────── */

/* Lays out parsed spans on one or more visual lines starting at (x, y),
 * wrapping to column `x` when a token won't fit at `cur_x`. Returns the
 * total height consumed (including the last line's height).
 *
 * `src` is line[0]'s source offset and `bg` the colour under the text, for
 * the run context `rc` (may be NULL). `plain` (MD_MODE_PLAIN) takes the
 * whole line as one text span and applies the two plain-only rules: the
 * whitespace a source line starts with is kept (until the first soft wrap),
 * and a word wider than the whole line is broken at a character boundary.
 * `fill_right` lets the selection fill run to x + max_width at the end of a
 * visual line the selection continues past (not for table cells). */
static int render_inline_spans(HDC hdc, const char *line, int line_len,
                               int x, int y, int max_width,
                               HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                               const ThemeColors *theme, int paint,
                               COLORREF default_clr, COLORREF code_clr,
                               MdRunCtx *rc, int src, COLORREF bg,
                               int plain, int fill_right)
{
    /* Default line height (used for empty lines and as min line height). */
    int default_lh;
    {
        HFONT old = (HFONT)SelectObject(hdc, hFont);
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        default_lh = tm.tmHeight;
        SelectObject(hdc, old);
    }

    MdSpan spans[MD_MAX_SPANS];
    int span_count = 0;
    if (line_len > 0) {
        if (plain) {
            spans[0].type = MD_SPAN_TEXT;
            spans[0].start = 0;
            spans[0].end = line_len;
            span_count = 1;
        } else {
            span_count = md_parse_inline(line, line_len, spans);
        }
    }

    int max_right = x + max_width;
    int cur_x = x;
    int cur_y = y;
    int line_h = default_lh;
    int at_line_start = 1;   /* true if cur_x == x (drop leading whitespace) */
    int keep_ws = plain;     /* plain: keep the source line's leading spaces */
    int placed = 0;          /* tokens painted or measured on this call */

    for (int s = 0; s < span_count; s++) {
        const MdSpan *span = &spans[s];
        int span_end = span->end;
        int off = span->start;

        while (off < span_end) {
            int wstart = 0, wend = 0;
            if (!md_next_word(line, span_end, off, &wstart, &wend)) break;
            int w_byte_len = wend - wstart;
            int is_ws = (line[wstart] == ' ' || line[wstart] == '\t');

            int word_h = 0;
            int word_w = measure_word(hdc, line, wstart, w_byte_len,
                                      span, hFont, hMonoFont, hBoldFont,
                                      &word_h);

            /* Drop leading whitespace at the start of a wrapped line. */
            if (is_ws && at_line_start && !keep_ws) {
                off = wend;
                continue;
            }

            /* If this is whitespace that would push past max_right, just
             * end the line here (don't emit trailing whitespace before wrap). */
            if (is_ws && cur_x + word_w > max_right) {
                if (paint && fill_right)
                    md_eol_fill(hdc, rc, src + wend, 1, cur_x, cur_y,
                                max_right, line_h, bg);
                cur_y += line_h;
                cur_x = x;
                line_h = default_lh;
                at_line_start = 1;
                keep_ws = 0;
                off = wend;   /* consume the whitespace */
                continue;
            }

            /* Word doesn't fit on current line and we're not at line start —
             * wrap before placing it. */
            if (!is_ws && cur_x + word_w > max_right && !at_line_start) {
                if (paint && fill_right)
                    md_eol_fill(hdc, rc, src + wstart, 1, cur_x, cur_y,
                                max_right, line_h, bg);
                cur_y += line_h;
                cur_x = x;
                line_h = default_lh;
                at_line_start = 1;
                keep_ws = 0;
            }

            /* MD_MODE_PLAIN: a word wider than the whole line is broken at
             * a character boundary instead of overflowing. */
            if (plain && !is_ws) {
                int word_end = wend;
                while (word_end > wstart && (line[word_end - 1] == ' ' ||
                                             line[word_end - 1] == '\t'))
                    word_end--;
                int core_h = 0;
                int core_w = measure_word(hdc, line, wstart, word_end - wstart,
                                          span, hFont, hMonoFont, hBoldFont,
                                          &core_h);
                if (core_w > max_width) {
                    HFONT oldf = (HFONT)SelectObject(hdc, hFont);
                    int ps = wstart;
                    while (ps < wend) {
                        int nb = plain_fit_bytes(hdc, line + ps, wend - ps,
                                                 max_right - cur_x);
                        int ph = 0;
                        int pw = measure_word(hdc, line, ps, nb, span, hFont,
                                              hMonoFont, hBoldFont, &ph);
                        if (paint)
                            paint_word(hdc, line, ps, nb, span, cur_x, cur_y,
                                       hFont, hMonoFont, hBoldFont, theme,
                                       default_clr, code_clr, rc, src + ps, bg);
                        cur_x += pw;
                        if (ph > line_h) line_h = ph;
                        placed++;
                        ps += nb;
                        if (ps < wend) {
                            if (paint && fill_right)
                                md_eol_fill(hdc, rc, src + ps, 1, cur_x, cur_y,
                                            max_right, line_h, bg);
                            cur_y += line_h;
                            cur_x = x;
                            line_h = default_lh;
                        }
                    }
                    SelectObject(hdc, oldf);
                    at_line_start = 0;
                    keep_ws = 0;
                    off = wend;
                    continue;
                }
            }

            /* Place the token. */
            if (paint) {
                paint_word(hdc, line, wstart, w_byte_len, span,
                           cur_x, cur_y,
                           hFont, hMonoFont, hBoldFont, theme,
                           default_clr, code_clr, rc, src + wstart, bg);
            }
            cur_x += word_w;
            if (word_h > line_h) line_h = word_h;
            at_line_start = 0;
            placed++;
            off = wend;
        }
    }

    if (paint) {
        /* A line that drew no word still gets a run, so a click lands on
         * it; the selection fill continues past its end like any line. */
        if (placed == 0)
            md_record_empty(rc, x, y, default_lh, src + line_len);
        if (fill_right)
            md_eol_fill(hdc, rc, src + line_len, 0, cur_x, cur_y, max_right,
                        line_h, bg);
    }

    /* Account for the final line. */
    return (cur_y - y) + line_h;
}
```
- [ ] **Step 7.7: Thread runs through the table block**

Five edits inside `md_render_table_block()` and its row type. Replace

*the row type:* replace

```c
typedef struct {
    char *text;
    int   len;
} MdTableRow;
```
with

```c
typedef struct {
    char *text;
    int   len;
    int   src;    /* source offset of the row's first byte (runs) */
} MdTableRow;
```
*the function head:* replace

```c
 * own line loop can resume there. Returns the total height consumed. */
static int md_render_table_block(HDC hdc, const char *first_line,
                                 int first_line_len, const char **p_next,
                                 int x, int y, int max_width,
                                 HFONT hFont, HFONT hMonoFont,
                                 HFONT hBoldFont, const ThemeColors *theme,
                                 int paint,
                                 COLORREF structure_clr, COLORREF code_clr)
{
```
with

```c
 * own line loop can resume there. Returns the total height consumed.
 *
 * Runs: `first_src` is the first row's source offset; a following row's is
 * base + (its position - text0), text0 being the text the caller walks.
 * Each cell records its runs at the row's offset plus the cell's offset in
 * the row; header cells sit on the header fill, body cells on rc->surface. */
static int md_render_table_block(HDC hdc, const char *first_line,
                                 int first_line_len, const char **p_next,
                                 int x, int y, int max_width,
                                 HFONT hFont, HFONT hMonoFont,
                                 HFONT hBoldFont, const ThemeColors *theme,
                                 int paint,
                                 COLORREF structure_clr, COLORREF code_clr,
                                 MdRunCtx *rc, int first_src,
                                 const char *text0, int base)
{
```
*row 0 (`first_line`):* replace

```c
        rows[n_rows].text[first_line_len] = '\0';
        rows[n_rows].len = first_line_len;
        n_rows++;
```
with

```c
        rows[n_rows].text[first_line_len] = '\0';
        rows[n_rows].len = first_line_len;
        rows[n_rows].src = first_src;
        n_rows++;
```
*each following row:* replace

```c
                rows[n_rows].text[line_len] = '\0';
                rows[n_rows].len = line_len;
                n_rows++;
```
with

```c
                rows[n_rows].text[line_len] = '\0';
                rows[n_rows].len = line_len;
                rows[n_rows].src = base + (int)(p - text0);
                n_rows++;
```
*the header-height measure call:* replace

```c
            ? render_inline_spans(hdc, ctext, clen, 0, 0, content_w,
                                  hBoldFont, hMonoFont, hBoldFont, theme, 0,
                                  structure_clr, code_clr)
```
with

```c
            ? render_inline_spans(hdc, ctext, clen, 0, 0, content_w,
                                  hBoldFont, hMonoFont, hBoldFont, theme, 0,
                                  structure_clr, code_clr,
                                  NULL, 0, 0, 0, 0)
```
*the body-height measure call:* replace

```c
                ? render_inline_spans(hdc, ctext, clen, 0, 0, content_w,
                                      hFont, hMonoFont, hBoldFont, theme, 0,
                                      body_clr, code_clr)
```
with

```c
                ? render_inline_spans(hdc, ctext, clen, 0, 0, content_w,
                                      hFont, hMonoFont, hBoldFont, theme, 0,
                                      body_clr, code_clr,
                                      NULL, 0, 0, 0, 0)
```
*the header paint call:* replace

```c
                render_inline_spans(hdc, ctext, clen,
                                    col_x + MD_TABLE_PAD_H + off,
                                    row_y + MD_TABLE_PAD_V,
                                    content_w - off,
                                    hBoldFont, hMonoFont, hBoldFont,
                                    theme, 1, structure_clr, code_clr);
```
with

```c
                render_inline_spans(hdc, ctext, clen,
                                    col_x + MD_TABLE_PAD_H + off,
                                    row_y + MD_TABLE_PAD_V,
                                    content_w - off,
                                    hBoldFont, hMonoFont, hBoldFont,
                                    theme, 1, structure_clr, code_clr,
                                    rc, rows[0].src + (int)(ctext - rows[0].text),
                                    header_fill, 0, 0);
```
*the body paint call:* replace

```c
                    render_inline_spans(hdc, ctext, clen,
                                        col_x + MD_TABLE_PAD_H + off,
                                        row_y + MD_TABLE_PAD_V,
                                        content_w - off,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, 1, body_clr, code_clr);
```
with

```c
                    const MdTableRow *brow = &rows[body_start + r];
                    render_inline_spans(hdc, ctext, clen,
                                        col_x + MD_TABLE_PAD_H + off,
                                        row_y + MD_TABLE_PAD_V,
                                        content_w - off,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, 1, body_clr, code_clr,
                                        rc, brow->src + (int)(ctext - brow->text),
                                        rc ? rc->surface : panel_clr, 0, 0);
```
- [ ] **Step 7.8: Replace `md_render_core()` and the public API**

Replace everything from `/* ── Core: shared render/measure logic ───────────────────────────────── */` up to (not including) the final `#endif /* _WIN32 */` with:

```c
/* ── Core: shared render/measure logic ───────────────────────────────── */

/* MD_MODE_PLAIN: every source line ('\n', or CR LF) a hard break, laid out
 * by render_inline_spans' plain rules in hFont and `clr`. No spacing is
 * added between lines, as DrawText added none. */
static int md_render_plain(HDC hdc, const char *text, int x, int y,
                           int max_width, HFONT hFont,
                           const ThemeColors *theme, int paint, COLORREF clr,
                           MdRunCtx *rc)
{
    int len = (int)strlen(text);
    int base = rc ? rc->base : 0;
    COLORREF bg = rc ? rc->surface : 0;

    int cur_y = y, pos = 0, ls = 0, le = 0;
    while (text_next_line(text, len, &pos, &ls, &le)) {
        cur_y += render_inline_spans(hdc, text + ls, le - ls, x, cur_y,
                                     max_width, hFont, hFont, hFont, theme,
                                     paint, clr, clr, rc, base + ls, bg, 1, 1);
    }
    return cur_y - y;
}

static int md_render_core(HDC hdc, const char *text, int x, int y,
                          int max_width,
                          HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                          const ThemeColors *theme, int paint,
                          MdBlockOut *bo, MdRunCtx *rc)
{
    if (bo) bo->count = 0;
    if (!text || !*text || max_width <= 0)
        return 0;

    /* The two AI-reply intent hues (see md_role_clr): "structure" for
     * headings/list markers/blockquote bar/table header text, "code" for
     * inline code text/fenced-code left bar. Resolved once per render so
     * every line reuses the same (contrast-guarded) colour. */
    COLORREF structure_clr = md_role_clr(theme, theme->link);
    COLORREF code_clr = md_role_clr(theme, theme->info);
    COLORREF body_clr = RGB_FROM_THEME(theme->text_main);
    COLORREF dim_text_clr = RGB_FROM_THEME(theme->text_dim);
    COLORREF code_bg = RGB_FROM_THEME(theme->chat.cmd_bg);
    COLORREF surface = rc ? rc->surface : RGB_FROM_THEME(theme->bg_primary);
    int base = rc ? rc->base : 0;   /* source offset of text[0] */

    int cur_y = y;
    int in_code_block = 0;
    int cur_block = -1;  /* index of the fenced block being walked, or -1 */
    int olist_num = 0;   /* current ordered list number */

    /* Process line by line */
    const char *p = text;
    while (*p) {
        /* Find end of current line */
        const char *eol = p;
        while (*eol && *eol != '\n') eol++;
        int line_len = (int)(eol - p);
        int src = base + (int)(p - text);   /* this line's source offset */

        /* Make a NUL-terminated copy for md_classify_line */
        char stack_buf[512];
        char *line_buf;
        if (line_len < (int)sizeof(stack_buf)) {
            line_buf = stack_buf;
        } else {
            line_buf = (char *)malloc((size_t)line_len + 1);
            if (!line_buf) break;
        }
        memcpy(line_buf, p, (size_t)line_len);
        line_buf[line_len] = '\0';

        MdLineInfo info = md_classify_line(line_buf, in_code_block);

        /* A table is a run of consecutive MD_LINE_TABLE lines, handled as
         * one block (own column layout) rather than line-by-line like
         * every other case below -- so it's intercepted before the
         * switch, and consumes as many following lines as belong to it. */
        if (info.type == MD_LINE_TABLE) {
            olist_num = 0;
            const char *next_p = (*eol == '\n') ? eol + 1 : eol;
            int h = md_render_table_block(hdc, line_buf, line_len, &next_p,
                                          x, cur_y, max_width,
                                          hFont, hMonoFont, hBoldFont,
                                          theme, paint,
                                          structure_clr, code_clr,
                                          paint ? rc : NULL, src, text, base);
            cur_y += h + MD_LINE_SPACING;
            if (line_buf != stack_buf) free(line_buf);
            p = next_p;
            continue;
        }

        switch (info.type) {
        case MD_LINE_CODE_FENCE:
            in_code_block = !in_code_block;
            if (in_code_block) {
                /* Start of code block — add a small gap */
                cur_y += MD_CODE_PAD_V;
                /* One entry per fence opened, in the order md_code_blocks()
                 * reports them (both walk md_classify_line), so an empty
                 * body keeps its slot as a zero rect. */
                cur_block = -1;
                if (bo) {
                    int bi = bo->count++;
                    if (bi < bo->max_rects && bo->rects) {
                        bo->rects[bi].x = bo->rects[bi].y = 0;
                        bo->rects[bi].w = bo->rects[bi].h = 0;
                        cur_block = bi;
                    }
                }
            } else {
                /* End of code block */
                cur_y += MD_CODE_PAD_V;
                cur_block = -1;
            }
            break;

        case MD_LINE_CODE: {
            /* One code line: monospace on the code background, drawn with
             * ExtTextOutW clipped at text_w -- no prefix processing (so
             * "cd a && make" shows both ampersands) and one run per line. */
            MdWBuf wb;
            mdbuf_init(&wb, line_buf, line_len);

            HFONT old_font = (HFONT)SelectObject(hdc, hMonoFont);
            /* A caller that draws a copy glyph on the block asks for its
             * right-hand margin to stay free of text. Code lines never
             * wrap, so this cannot change a height. */
            int text_w = max_width;
            if (bo && bo->reserve_w > 0) {
                text_w = max_width - bo->reserve_w;
                if (text_w < 1) text_w = 1;
            }
            TEXTMETRIC tm;
            GetTextMetrics(hdc, &tm);
            int h = tm.tmHeight;

            RECT bg_rc;
            bg_rc.left   = x - MD_CODE_PAD_H;
            bg_rc.top    = cur_y;
            bg_rc.right  = x + max_width + MD_CODE_PAD_H;
            bg_rc.bottom = cur_y + h;

            /* Grow the block's rect to the union of its line backgrounds. */
            if (bo && cur_block >= 0 && cur_block < bo->max_rects) {
                NsRect *br = &bo->rects[cur_block];
                if (br->w == 0 && br->h == 0) {
                    br->x = bg_rc.left;
                    br->y = bg_rc.top;
                    br->w = bg_rc.right - bg_rc.left;
                    br->h = bg_rc.bottom - bg_rc.top;
                } else {
                    int bot = bg_rc.bottom;
                    if (br->y + br->h > bot) bot = br->y + br->h;
                    br->h = bot - br->y;
                }
            }

            if (paint) {
                /* Background rectangle */
                HBRUSH bg_br = CreateSolidBrush(code_bg);
                FillRect(hdc, &bg_rc, bg_br);
                DeleteObject(bg_br);

                /* Code hue left bar (fenced code block only -- inline
                 * code gets the hue on its text instead, see paint_word). */
                RECT bar_rc = bg_rc;
                bar_rc.right = bar_rc.left + MD_CODE_BAR_WIDTH;
                HBRUSH bar_br = CreateSolidBrush(code_clr);
                FillRect(hdc, &bar_rc, bar_br);
                DeleteObject(bar_br);

                if (line_len == 0) {
                    md_record_empty(rc, x, cur_y, h, src);
                    md_eol_fill(hdc, rc, src, 0, x, cur_y, x + text_w, h,
                                code_bg);
                } else {
                    MdCum cm;
                    SIZE sz;
                    mdcum_measure(hdc, &wb, line_buf, line_len, &cm, &sz);
                    md_sel_fill(hdc, rc, src, src + line_len, x, cur_y, h,
                                x + text_w, &cm, code_bg);
                    SetTextColor(hdc, body_clr);
                    RECT clip = { x, cur_y, x + text_w, cur_y + h };
                    ExtTextOutW(hdc, x, cur_y, ETO_CLIPPED, &clip,
                                wb.ptr, (UINT)wb.len, NULL);
                    int run_w = sz.cx < text_w ? sz.cx : text_w;
                    md_record(rc, x, cur_y, run_w, h, src, src + line_len,
                              &cm, TR_FLAG_CODE, NULL);
                    md_eol_fill(hdc, rc, src + line_len, 0, x + run_w, cur_y,
                                x + text_w, h, code_bg);
                    mdcum_free(&cm);
                }
            }

            cur_y += h;
            SelectObject(hdc, old_font);
            mdbuf_free(&wb);
            break;
        }

        case MD_LINE_HEADING: {
            cur_y += MD_HEADING_EXTRA_V;
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;

            /* Create a scaled bold font for headings:
             * h1 = 150%, h2 = 125%, h3 = 110% of base */
            LOGFONT lf;
            GetObject(hBoldFont, sizeof(lf), &lf);
            if (info.heading_level == 1)
                lf.lfHeight = (LONG)(lf.lfHeight * 150 / 100);
            else if (info.heading_level == 2)
                lf.lfHeight = (LONG)(lf.lfHeight * 125 / 100);
            else
                lf.lfHeight = (LONG)(lf.lfHeight * 110 / 100);

            HFONT heading_font = CreateFontIndirect(&lf);
            HFONT use_font = heading_font ? heading_font : hBoldFont;

            HFONT old_font = (HFONT)SelectObject(hdc, use_font);
            int h = render_inline_spans(hdc, content, content_len,
                                        x, cur_y, max_width,
                                        use_font, hMonoFont, use_font,
                                        theme, paint,
                                        structure_clr, code_clr,
                                        rc, src + info.content_offset,
                                        surface, 0, 1);
            cur_y += h + MD_HEADING_EXTRA_V;
            SelectObject(hdc, old_font);
            if (heading_font) DeleteObject(heading_font);
            break;
        }

        case MD_LINE_ULIST: {
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;

            /* Draw bullet */
            if (paint) {
                /* Bullet character; a synthetic run "- " over the whole
                 * source line, filled when the selection touches it. */
                static const wchar_t bullet[] = L"\x2022 ";
                HFONT old_font = (HFONT)SelectObject(hdc, hFont);
                TEXTMETRIC tm;
                GetTextMetrics(hdc, &tm);
                md_synth_fill(hdc, rc, src, src + info.content_offset, x,
                              cur_y, MD_LIST_INDENT, tm.tmHeight, surface);
                SetTextColor(hdc, structure_clr);
                RECT brc;
                brc.left   = x;
                brc.top    = cur_y;
                brc.right  = x + MD_LIST_INDENT;
                brc.bottom = cur_y + 1000;
                DrawTextW(hdc, bullet, 2, &brc, DT_LEFT | DT_TOP);
                md_record(rc, x, cur_y, MD_LIST_INDENT, tm.tmHeight, src,
                          src + info.content_offset, NULL, 0, "- ");
                SelectObject(hdc, old_font);
            }

            int h = render_inline_spans(hdc, content, content_len,
                                        x + MD_LIST_INDENT, cur_y,
                                        max_width - MD_LIST_INDENT,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint, body_clr, code_clr,
                                        rc, src + info.content_offset,
                                        surface, 0, 1);
            cur_y += h + MD_LINE_SPACING;
            break;
        }

        case MD_LINE_OLIST: {
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;
            olist_num++;

            /* Draw number prefix */
            if (paint) {
                char num_str[16];
                int num_len = snprintf(num_str, sizeof(num_str),
                                       "%d. ", olist_num);
                MdWBuf nb;
                mdbuf_init(&nb, num_str, num_len);
                HFONT old_font = (HFONT)SelectObject(hdc, hFont);
                TEXTMETRIC tm;
                GetTextMetrics(hdc, &tm);
                md_synth_fill(hdc, rc, src, src + info.content_offset, x,
                              cur_y, MD_LIST_INDENT, tm.tmHeight, surface);
                SetTextColor(hdc, structure_clr);
                RECT nrc;
                nrc.left   = x;
                nrc.top    = cur_y;
                nrc.right  = x + MD_LIST_INDENT;
                nrc.bottom = cur_y + 1000;
                DrawTextW(hdc, nb.ptr, nb.len, &nrc, DT_LEFT | DT_TOP);
                md_record(rc, x, cur_y, MD_LIST_INDENT, tm.tmHeight, src,
                          src + info.content_offset, NULL, 0, num_str);
                SelectObject(hdc, old_font);
                mdbuf_free(&nb);
            }

            int h = render_inline_spans(hdc, content, content_len,
                                        x + MD_LIST_INDENT, cur_y,
                                        max_width - MD_LIST_INDENT,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint, body_clr, code_clr,
                                        rc, src + info.content_offset,
                                        surface, 0, 1);
            cur_y += h + MD_LINE_SPACING;
            break;
        }

        case MD_LINE_TABLE:
            /* Unreachable: handled as a whole block, and control flow
             * `continue`s past this switch, before this line is ever
             * classified as MD_LINE_TABLE here. Kept only so this switch
             * stays exhaustive over MdLineType. */
            break;

        case MD_LINE_BLOCKQUOTE: {
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;

            int h = render_inline_spans(hdc, content, content_len,
                                        x + MD_BLOCKQUOTE_IND, cur_y,
                                        max_width - MD_BLOCKQUOTE_IND,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint, dim_text_clr, code_clr,
                                        rc, src + info.content_offset,
                                        surface, 0, 1);
            if (paint) {
                /* Draw left bar (structure hue) */
                RECT bar_rc;
                bar_rc.left   = x;
                bar_rc.top    = cur_y;
                bar_rc.right  = x + MD_BQ_BAR_WIDTH;
                bar_rc.bottom = cur_y + h;
                HBRUSH bar_br = CreateSolidBrush(structure_clr);
                FillRect(hdc, &bar_rc, bar_br);
                DeleteObject(bar_br);
            }
            cur_y += h + MD_LINE_SPACING;
            break;
        }

        case MD_LINE_HRULE: {
            cur_y += MD_PARA_SPACING;
            if (paint) {
                RECT hr_rc;
                hr_rc.left   = x;
                hr_rc.top    = cur_y;
                hr_rc.right  = x + max_width;
                hr_rc.bottom = cur_y + MD_HRULE_HEIGHT;
                HBRUSH hr_br = CreateSolidBrush(
                    RGB_FROM_THEME(theme->border));
                FillRect(hdc, &hr_rc, hr_br);
                DeleteObject(hr_br);
            }
            cur_y += MD_HRULE_HEIGHT + MD_PARA_SPACING;
            break;
        }

        case MD_LINE_EMPTY: {
            /* Reset ordered list counter on blank line */
            olist_num = 0;
            /* Real paragraph break: full font line height plus MD_LINE_SPACING.
             * MD_PARA_SPACING (6 px) made \n\n indistinguishable from \n. */
            HFONT old = (HFONT)SelectObject(hdc, hFont);
            TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
            SelectObject(hdc, old);
            if (paint) {
                md_record_empty(rc, x, cur_y, tm.tmHeight, src);
                md_eol_fill(hdc, rc, src, 0, x, cur_y, x + max_width,
                            tm.tmHeight, surface);
            }
            cur_y += tm.tmHeight + MD_LINE_SPACING;
            break;
        }

        case MD_LINE_PARAGRAPH: {
            /* Reset ordered list counter */
            olist_num = 0;
            int h = render_inline_spans(hdc, line_buf, line_len,
                                        x, cur_y, max_width,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint, body_clr, code_clr,
                                        rc, src, surface, 0, 1);
            cur_y += h + MD_LINE_SPACING;
            break;
        }
        }

        if (line_buf != stack_buf) free(line_buf);

        /* Advance past the newline */
        if (*eol == '\n')
            p = eol + 1;
        else
            p = eol;   /* end of string */
    }

    return cur_y - y;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int md_render_ex(HDC hdc, const char *text, int x, int y, int max_width,
                 HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                 const ThemeColors *theme, int mode, int paint,
                 COLORREF plain_clr, MdBlockOut *blocks, MdRunCtx *rc)
{
    if (!hdc || !text || !theme)
        return 0;
    if (!paint) {
        x = 0;
        y = 0;
        rc = NULL;
    }

    int old_bk = 0;
    COLORREF old_color = 0;
    if (paint) {
        old_bk = SetBkMode(hdc, TRANSPARENT);
        old_color = GetTextColor(hdc);
    }
    int result;
    if (mode == MD_MODE_PLAIN) {
        if (blocks) blocks->count = 0;
        result = (*text && max_width > 0)
            ? md_render_plain(hdc, text, x, y, max_width, hFont, theme,
                              paint, plain_clr, rc)
            : 0;
    } else {
        result = md_render_core(hdc, text, x, y, max_width,
                                hFont, hMonoFont, hBoldFont,
                                theme, paint, blocks, rc);
    }
    if (paint) {
        SetTextColor(hdc, old_color);
        SetBkMode(hdc, old_bk);
    }
    return result;
}

int md_render_text(HDC hdc, const char *text, int x, int y, int max_width,
                   HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                   const ThemeColors *theme)
{
    return md_render_ex(hdc, text, x, y, max_width, hFont, hMonoFont,
                        hBoldFont, theme, MD_MODE_MARKDOWN, 1, 0, NULL, NULL);
}

int md_measure_text(HDC hdc, const char *text, int max_width,
                    HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                    const ThemeColors *theme)
{
    return md_render_ex(hdc, text, 0, 0, max_width, hFont, hMonoFont,
                        hBoldFont, theme, MD_MODE_MARKDOWN, 0, 0, NULL, NULL);
}

int md_render_text_blocks(HDC hdc, const char *text, int x, int y,
                          int max_width,
                          HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                          const ThemeColors *theme, MdBlockOut *blocks)
{
    return md_render_ex(hdc, text, x, y, max_width, hFont, hMonoFont,
                        hBoldFont, theme, MD_MODE_MARKDOWN, 1, 0, blocks,
                        NULL);
}
```
- [ ] **Step 7.9: Run the tests**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | tail -1
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/ui/md_render.c src/ui/chat_listview.c src/ui/ai_chat.c
wineserver -k; xvfb-run -a make wintest 2>&1 | grep -E "md_render_win|^Tests Run"
```
Expected: `make test` unchanged (`Failed: 0`); the MinGW check prints nothing; `[PASS] test_md_render_win_measure_equals_paint_and_runs` and `Tests Run: 10, Failed: 0` (the icon and ConPTY cases plus this one; this case needs no display, so a plain `make wintest` passes it too). On Windows: `mingw32-make wintest`.

- [ ] **Step 7.10: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/ui/md_render.h src/ui/md_render.c Makefile tests/test_md_render_win.c tests/win_runner.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
feat(ui): md_render records runs and draws the selection highlight

md_render_ex() is the one entry point: with a run context every drawn
word becomes a run (rect, byte range, cumulative widths from one
GetTextExtentExPointW) and its selected part is filled over the colour
it sits on just before its text. MD_MODE_PLAIN lays out plain text on
the same layouter, keeping a line's leading spaces and breaking a word
wider than the line. Code lines move to ExtTextOutW, so "cd a && make"
no longer loses an ampersand to prefix processing.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 8: Chat list -- every item on the layouter, runs recorded by paint

Spec section 3 in the list view: user, status, tool, Thinking and command text move to `MD_MODE_PLAIN` with their measuring twins at the same width; `on_paint()` resets every item's runs and the items painted append theirs (each segment at its source offset, Thinking only inside its clip, the pinned bubble into its own set); the Thinking layout is cached; the highlight hue comes from `ui_theme_sel_hue()`. The old band selection still runs in this task (it is replaced in Task 9); the new anchor and focus exist but stay equal, so nothing highlights yet.

Fixes two existing measure/paint mismatches the runs would expose: the Thinking body was measured at the box width and painted at the box less its inset and scrollbar; and the hover, click and wheel hit-tests computed the Thinking box one `side_pad` wider than it is painted (`box_right = width - side_pad` instead of `width - 2 * side_pad`). Also fixes `chat_listview_set_render_markdown()`, which never marked items dirty, so turning markdown off kept every old height (Review Focus 3).

**Toolchain:** this task changes `src/ui`, which `make test` never compiles. Compile-check it with the MinGW `-fsyntax-only` command in its run step (automaton has the toolchain); where no MinGW toolchain exists, rely on `make test` and a careful read of the diff. `make clean && make release` happens once, in Task 16, after the version bump; the maintainer builds the exe and runs `mingw32-make wintest` on Windows before the PR leaves draft.

**Files:**
- Modify: `src/core/ui_theme.h` (end, CRLF), `src/core/ui_theme.c` (end, CRLF)
- Modify: `src/ui/chat_listview.h` -- include (~9), selection fields (~47-50)
- Modify: `src/ui/chat_listview.c` -- includes (~32), new helpers after `draw_text_utf8()` (~267), `build_thinking_layout()` (~339-399), `chat_listview_set_fonts()` (~590-594), `chat_listview_set_render_markdown()` (~677-686), `measure_ai_segment()`/`measure_ai_text_with_exec()` (~934-1041), `measure_item()` (~1046-1173), new helpers before "Paint routines" (~1220), `paint_user_item()` (~1227-1263), `draw_ai_segment()`/`draw_ai_text_with_exec()` (~1341-1486), `paint_ai_item()` (~1610-1681), `paint_status_item()` (~2165-2186), `on_paint()` (~2604-2757), four Thinking-box hit-tests (~3085, ~3282, ~3431, ~3834), `ChatListWndProc` `WM_PAINT` (~3590)
- Test: `tests/test_sel_contrast.c` (new), `tests/runner.c`

**Interfaces:**
- Consumes: `md_render_ex()`, `MdRunCtx`, `MD_MODE_*` (Task 7); `chat_exec_split()` (Task 2); `text_runs_reset()`, `TR_PART_*` (Task 3); `ChatMsgItem.runs/.pinned_runs/.painted_height/.u.ai.think_cache_*` (Task 4); `ChatSelPos`, `chat_sel_cmp()`, `chat_sel_part_range()` (Task 5).
- Produces:
  - `unsigned int ui_theme_sel_hue(const ThemeTokens *t, int on_user_bubble);` (core)
  - `ChatListView` fields `ChatSelPos sel_anchor; ChatSelPos sel_focus; int pinned_id;`
  - statics in `chat_listview.c` that Task 9 calls: `static int clv_sel_ordered(const ChatListView *lv, ChatSelPos *a, ChatSelPos *b);`, `static void clv_run_ctx(const ChatListView *lv, MdRunCtx *rc, const ChatMsgItem *item, int part, int part_len, TextRuns *runs, int base, COLORREF surface);`, `static void paint_frame(ChatListView *lv, HDC hdc);`, `static HFONT clv_font(const ChatListView *lv);`
  - `WM_PRINTCLIENT` paints into `(HDC)wParam` (the wintest's way in, Task 12).

- [ ] **Step 8.1: Write the failing test**

Create `tests/test_sel_contrast.c`:

```c
/* tests/test_sel_contrast.c -- the selection highlight shows on every
 * surface it is drawn on, in every theme (spec 2026-10-18 section 6): the
 * hue ui_theme_sel_hue() picks, blended at 30% over the panel, the code
 * background, the Thinking body, the table-header fill and the user bubble,
 * must land at least MIN_STEP channel-steps away from that surface. */
#include "test_framework.h"
#include "ui_theme.h"
#include <stdio.h>
#include <stdlib.h>

/* rgb_alpha() (src/ui/ns_draw.c) on packed 0x00RRGGBB: per channel
 * fg * a + bg * (1 - a), truncated. */
static unsigned int blend(unsigned int fg, unsigned int bg, float a)
{
    unsigned int out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float f = (float)((fg >> sh) & 0xFF), b = (float)((bg >> sh) & 0xFF);
        out |= ((unsigned int)(f * a + b * (1.0f - a)) & 0xFF) << sh;
    }
    return out;
}

/* Sum of the per-channel differences. */
static int dist(unsigned int x, unsigned int y)
{
    int d = 0;
    for (int sh = 16; sh >= 0; sh -= 8)
        d += abs((int)((x >> sh) & 0xFF) - (int)((y >> sh) & 0xFF));
    return d;
}

#define SEL_ALPHA 0.30f
#define MIN_STEP  40

int test_sel_highlight_visible_on_every_surface(void)
{
    TEST_BEGIN();
    for (int t = 0; t < NUM_UI_THEMES; t++) {
        const ThemeColors *c = ui_theme_get(t);
        ThemeTokens k;
        ui_theme_resolve(c, &k);
        unsigned int header = blend(c->text_dim, c->bg_primary, 0.12f);
        unsigned int surf[5] = { c->bg_primary, c->chat.cmd_bg,
                                 k.bg_secondary.base, header,
                                 c->chat.user_bubble };
        unsigned int hue[5] = { ui_theme_sel_hue(&k, 0), ui_theme_sel_hue(&k, 0),
                                ui_theme_sel_hue(&k, 0), ui_theme_sel_hue(&k, 0),
                                ui_theme_sel_hue(&k, 1) };
        static const char *const name[5] = { "panel", "code", "thinking",
                                             "table header", "user bubble" };
        for (int i = 0; i < 5; i++) {
            int d = dist(blend(hue[i], surf[i], SEL_ALPHA), surf[i]);
            if (d < MIN_STEP)
                printf("  %s: highlight on the %s is only %d from it\n",
                       c->name, name[i], d);
            ASSERT_TRUE(d >= MIN_STEP);
        }
    }
    TEST_END();
}

int test_sel_hue_is_focus_except_on_the_bubble(void)
{
    TEST_BEGIN();
    ThemeTokens k;
    ui_theme_resolve(ui_theme_get(0), &k);
    ASSERT_EQ(ui_theme_sel_hue(&k, 0), k.focus);
    ASSERT_EQ(ui_theme_sel_hue(&k, 1), k.chat.user_text);
    ASSERT_EQ(ui_theme_sel_hue(NULL, 0), 0);
    TEST_END();
}
```
In `tests/runner.c` insert immediately before the line `/* test_md_blocks.c */`:

```c
/* test_sel_contrast.c */
int test_sel_highlight_visible_on_every_surface(void);
int test_sel_hue_is_focus_except_on_the_bubble(void);

```
and immediately before the line `    failed += test_md_blocks_none();`:

```c
    failed += test_sel_highlight_visible_on_every_surface();
    failed += test_sel_hue_is_focus_except_on_the_bubble();

```
- [ ] **Step 8.2: Run it and see it fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make test 2>&1 | grep -m2 "ui_theme_sel_hue"
```
Expected: `implicit declaration of function 'ui_theme_sel_hue'` and an undefined reference to it.

- [ ] **Step 8.3: The highlight hue (core)**

In `src/core/ui_theme.h` (CRLF) replace

```c
void ui_theme_resolve(const ThemeColors *base, ThemeTokens *out);

#endif /* NUTSHELL_UI_THEME_H */
```
with

```c
void ui_theme_resolve(const ThemeColors *base, ThemeTokens *out);

/* The hue the chat selection highlight blends, at 30%, over the surface it
 * sits on (spec docs/superpowers/specs/2026-10-18-ai-panel-text-selection-
 * design.md section 6): the focus token -- except on the user bubble, which
 * is filled with the accent the focus token is drawn from, so a focus tint
 * would vanish there; the bubble's own text colour is the hue instead.
 * Packed 0x00RRGGBB like every token. */
unsigned int ui_theme_sel_hue(const ThemeTokens *t, int on_user_bubble);

#endif /* NUTSHELL_UI_THEME_H */
```
Append to `src/core/ui_theme.c` (CRLF):

```c

unsigned int ui_theme_sel_hue(const ThemeTokens *t, int on_user_bubble)
{
    if (!t) return 0;
    return on_user_bubble ? t->chat.user_text : t->focus;
}
```
Run `make test 2>&1 | grep -E "sel_|Tests Run"`: both `test_sel_*` pass.

- [ ] **Step 8.4: Header fields**

In `src/ui/chat_listview.h`, after `#include "ns_hover.h"` add:

```c
#include "chat_copy.h"
```
Replace

```c
    /* Text selection (content-space Y coordinates) */
    int sel_start_y;            /* Content Y where mouse-drag started */
    int sel_end_y;              /* Content Y where mouse-drag currently is */
    int sel_active;             /* 1 = mouse is held down, dragging */
    int sel_valid;              /* 1 = a selection exists to highlight/copy */
```
with

```c
    /* Text selection (content-space Y coordinates) */
    int sel_start_y;            /* Content Y where mouse-drag started */
    int sel_end_y;              /* Content Y where mouse-drag currently is */
    int sel_active;             /* 1 = mouse is held down, dragging */
    int sel_valid;              /* 1 = a selection exists to highlight/copy */

    /* Character selection (spec 2026-10-18 section 5): anchor and focus as
     * (message id, part, byte offset), resolved by id on every use. */
    ChatSelPos sel_anchor;
    ChatSelPos sel_focus;

    /* Id of the user item whose copy on_paint() pinned at the top of the
     * list this frame (its runs are in pinned_runs), 0 when none. */
    int pinned_id;
```
- [ ] **Step 8.5: Includes, fonts and shared widths**

In `src/ui/chat_listview.c`, after `#include "chat_exec.h"` (added in Task 2) add:

```c
#include "chat_copy.h"
#include "text_runs.h"
```
Insert immediately before the line `/* ── Safety tag colour ──────────────────────────────────────────────── */`:

```c
/* ── Fonts and text widths shared by measure and paint ─────────────── */

static HFONT clv_font(const ChatListView *lv)
{
    return lv->hFont ? lv->hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static HFONT clv_mono(const ChatListView *lv)
{
    return lv->hMonoFont ? lv->hMonoFont
                         : (HFONT)GetStockObject(ANSI_FIXED_FONT);
}

static HFONT clv_small(const ChatListView *lv)
{
    return lv->hSmallFont ? lv->hSmallFont
                          : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

/* Text widths at list width `width` (the client width). One function per
 * item kind, called by measure_item() and by the paint routine alike, so
 * the layouter always measures at the width it paints at. */
static int clv_user_text_w(const ChatListView *lv, int width)
{
    int text_w = (width * 3) / 4 - 2 * lv->user_pad_h;
    return text_w < 40 ? 40 : text_w;
}

static int clv_status_text_w(const ChatListView *lv, int width)
{
    int text_w = width - 4 * ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    return text_w < 40 ? 40 : text_w;
}

static int clv_ai_text_w(const ChatListView *lv, int width)
{
    int text_w = width - lv->ai_indent - 3 * ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    return text_w < 40 ? 40 : text_w;
}

/* MD_MODE_PLAIN height of `text` at `width` in `font`. */
static int clv_plain_height(const ChatListView *lv, HDC hdc, const char *text,
                            int width, HFONT font)
{
    return md_render_ex(hdc, text ? text : "", 0, 0, width, font, font, font,
                        lv->theme, MD_MODE_PLAIN, 0, 0, NULL, NULL);
}
```
- [ ] **Step 8.6: The cached Thinking layout at the paint width**

Replace the whole function `build_thinking_layout()` (from `static void build_thinking_layout(ChatListView *lv, HDC hdc, ChatMsgItem *item,` to its closing `}`; keep the comment block above it) with:

```c
static void build_thinking_layout(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                                  int box_left, int box_right, int content_top,
                                  ThinkingLayout *out, int *out_full_body_h)
{
    int dpi = CLV_DPI(lv);
    NsRect avail = { box_left, content_top, box_right - box_left, 0 };
    int expanded = !item->u.ai.thinking_collapsed;

    int line_h = ns_scale(20, dpi);  /* fallback if hdc is unavailable */
    if (hdc) {
        HGDIOBJ old = SelectObject(hdc, clv_font(lv));
        TEXTMETRICA tm;
        GetTextMetricsA(hdc, &tm);
        line_h = tm.tmHeight;
        SelectObject(hdc, old);
    }
    int max_body_h = ai_thinking_max_body_h(line_h);
    if (max_body_h < 1) max_body_h = 1;

    thinking_layout(avail, expanded, 0, max_body_h, dpi, out);

    int full_h = 0;
    if (expanded && out->body.w > 0) {
        /* The wrapped height, laid out by the plain layouter at the width
         * the body is painted at: the box less its SP_XS inset and, when
         * the text overflows the box, less the scrollbar too. Cached per
         * (text length, box width, line height) so hover and click
         * hit-tests cost no layout (spec 2026-10-18 section 3). */
        const char *tt = item->u.ai.thinking_text ? item->u.ai.thinking_text
                                                  : "";
        int tlen = (int)strlen(tt);
        if (item->u.ai.think_cache_len == tlen &&
            item->u.ai.think_cache_w == out->body.w &&
            item->u.ai.think_cache_line_h == line_h) {
            full_h = item->u.ai.think_cache_full_h;
        } else if (hdc) {
            int tw = out->body.w - ns_scale(SP_XS, dpi);
            if (tw < 1) tw = 1;
            full_h = clv_plain_height(lv, hdc, tt, tw, clv_font(lv));
            int sb_w = ns_scale(6, dpi);
            if (full_h > max_body_h && tw - sb_w >= 1) {
                tw -= sb_w;
                full_h = clv_plain_height(lv, hdc, tt, tw, clv_font(lv));
            }
            item->u.ai.think_cache_len = tlen;
            item->u.ai.think_cache_w = out->body.w;
            item->u.ai.think_cache_line_h = line_h;
            item->u.ai.think_cache_full_h = full_h;
            item->u.ai.think_cache_text_w = tw;
        }

        thinking_layout(avail, expanded, full_h, max_body_h, dpi, out);

        /* Smart scroll: settle this item's persisted scroll offset now that
         * the real content height (full_h) and visible box height
         * (out->body.h) are known -- reuses stick_scroll_on_layout()
         * (src/core/stick_scroll.c), the same "stuck vs released" logic the
         * outer chat list uses for its own stick-to-bottom behaviour. While
         * thinking_autoscroll (this item's "stuck" bit, updated by the
         * wheel handler below on every user scroll) is set, this snaps to
         * the new bottom on every call -- i.e. it keeps following freshly
         * streamed text; otherwise it just re-clamps the existing position
         * into range, so scrolling up stops following and stays put, and
         * collapsing/re-expanding (which resets scroll_y to 0 and
         * thinking_autoscroll to 1 -- see the click handler) starts back at
         * the bottom instead of an out-of-range offset. Idempotent, so
         * running it from every geometry recompute (measure, paint, hover,
         * hit-test) is harmless. */
        int vis_h = out->body.h;
        int max_scroll = (full_h > vis_h) ? full_h - vis_h : 0;
        item->u.ai.thinking_scroll_y = stick_scroll_on_layout(
            item->u.ai.thinking_autoscroll, item->u.ai.thinking_scroll_y,
            max_scroll);
    }
    if (out_full_body_h) *out_full_body_h = full_h;
}
```
In `chat_listview_set_fonts()`:

Replace

```c
    /* Fonts changed — mark all dirty for full remeasure */
    {
        ChatMsgItem *fi = lv->msg_list ? lv->msg_list->head : NULL;
        while (fi) { fi->dirty = 1; fi = fi->next; }
    }
```
with

```c
    /* Fonts changed — mark all dirty for full remeasure, and forget every
     * cached Thinking layout (laid out in the old font). */
    {
        ChatMsgItem *fi = lv->msg_list ? lv->msg_list->head : NULL;
        while (fi) {
            fi->dirty = 1;
            if (fi->type == CHAT_ITEM_AI_TEXT) fi->u.ai.think_cache_len = -1;
            fi = fi->next;
        }
    }
```
In `chat_listview_set_render_markdown()` (Review Focus 3):

Replace

```c
    lv->render_markdown = v;
    /* Layout heights change with the new mode — recalc and redraw. */
    chat_listview_invalidate(hwnd);
```
with

```c
    lv->render_markdown = v;
    /* Layout heights change with the new mode: every item must be measured
     * again (recalc_layout() only remeasures dirty items), then redraw. */
    for (ChatMsgItem *it = lv->msg_list ? lv->msg_list->head : NULL; it;
         it = it->next)
        it->dirty = 1;
    chat_listview_invalidate(hwnd);
```
The four Thinking-box hit-tests (`chatlv_thinking_row_hit()`, `chatlv_hover_rect_for_id()`, `on_lbuttondown()`, the `WM_MOUSEWHEEL` handler) must use the box `paint_ai_item()` paints, or the cache keyed on its width would be rebuilt on every hover. Replace both occurrences of

```c
    int box_right = (client_rc.right - client_rc.left) - side_pad;
```
with

```c
    int box_right = (client_rc.right - client_rc.left) - 2 * side_pad;
```
(the first is indented four spaces, the second eight -- keep each one's indentation); replace

```c
            int box_right = (client_rc2.right - client_rc2.left) - side_pad;
```
with

```c
            int box_right = (client_rc2.right - client_rc2.left) - 2 * side_pad;
```
and replace

```c
                    int box_right = (crc.right - crc.left) - side_pad2;
```
with

```c
                    int box_right = (crc.right - crc.left) - 2 * side_pad2;
```
- [ ] **Step 8.7: Measuring twins on the layouter**

Replace everything from `/* Height-only twin of draw_ai_segment() (defined below, near paint_ai_item):` up to (not including) `/* ── Measure a single item ──────────────────────────────────────────── */` with:

```c
/* Height-only twin of draw_ai_segment() (defined below, near paint_ai_item):
 * same layouter, same fonts, paint = 0. Kept as the single source of truth
 * for a prose segment's height so measure_item() and paint_ai_item() can
 * never drift apart. */
static int measure_ai_segment(ChatListView *lv, HDC hdc, const char *text,
                              int width)
{
    if (lv->render_markdown) {
        return md_measure_text(hdc, text, width,
                               lv->hFont, lv->hMonoFont, lv->hBoldFont,
                               lv->theme);
    }
    return clv_plain_height(lv, hdc, text, width, clv_font(lv));
}

/* Height-only twin of draw_ai_text_with_exec() (near paint_ai_item): walks
 * the same chat_exec_split() segments -- prose through measure_ai_segment(),
 * commands through the plain layouter in the mono font -- so the two can
 * never disagree about an [EXEC]-bearing message's height. */
static int measure_ai_text_with_exec(ChatListView *lv, HDC hdc,
                                     const char *text, int width)
{
    int len = (int)strlen(text);
    int pos = 0, total_h = 0;
    ChatSeg seg;
    while (chat_exec_split(text, len, &pos, &seg)) {
        int n = seg.end - seg.start;
        if (n <= 0) continue;
        char *s = (char *)malloc((size_t)n + 1);
        if (!s) continue;
        memcpy(s, text + seg.start, (size_t)n);
        s[n] = '\0';
        if (seg.kind == CHAT_SEG_PROSE)
            total_h += measure_ai_segment(lv, hdc, s, width);
        else
            total_h += clv_plain_height(lv, hdc, s, width, clv_mono(lv));
        free(s);
    }
    return total_h;
}
```
In `measure_item()`, replace the declarations

```c
    int side_pad = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    int text_w;
    RECT rc;
    HGDIOBJ old_font;
```
with

```c
    int text_w;
    HGDIOBJ old_font;
```
Replace the whole `case CHAT_ITEM_USER: { ... }` block (from `    case CHAT_ITEM_USER: {` to its closing `    }` before `    case CHAT_ITEM_AI_TEXT: {`) with:

```c
    case CHAT_ITEM_USER:
        /* User bubble: the plain layouter at clv_user_text_w(), the width
         * paint_user_item() lays the text out at. */
        return clv_plain_height(lv, hdc, item->text,
                                clv_user_text_w(lv, width), clv_font(lv))
               + 2 * lv->user_pad_v;
```
Replace

```c
    case CHAT_ITEM_AI_TEXT: {
        /* AI text: left-indented.  on_paint applies side_pad to item_rc,
         * and paint_ai_item subtracts another side_pad on the right, so
         * the effective text width is width - 2*side_pad - indent - side_pad. */
        text_w = width - lv->ai_indent - 3 * side_pad;
        if (text_w < 40) text_w = 40;
```
with

```c
    case CHAT_ITEM_AI_TEXT: {
        /* AI text: left-indented, at clv_ai_text_w() -- the width
         * paint_ai_item() lays it out at. */
        text_w = clv_ai_text_w(lv, width);
```
Replace the whole `case CHAT_ITEM_TOOL_CALL: case CHAT_ITEM_TOOL_RESULT: case CHAT_ITEM_STATUS: { ... }` block (up to, not including, `    default:`) with:

```c
    case CHAT_ITEM_TOOL_CALL:
    case CHAT_ITEM_TOOL_RESULT:
    case CHAT_ITEM_STATUS:
        /* The plain layouter in the small font at clv_status_text_w(), the
         * width paint_status_item() uses, plus 8 px of slack. */
        return clv_plain_height(lv, hdc, item->text,
                                clv_status_text_w(lv, width), clv_small(lv))
               + ns_scale(8, CLV_DPI(lv));
```
- [ ] **Step 8.8: The run context**

Insert immediately before the three-line banner whose middle line is ` *  Paint routines` (above `static int paint_user_item(`):

```c
/* ── Character selection: the selection in reading order ───────────── */

/* The current selection as an ordered pair (a before b). Returns 0 when
 * there is none or it is empty. */
static int clv_sel_ordered(const ChatListView *lv, ChatSelPos *a, ChatSelPos *b)
{
    if (!lv->sel_valid) return 0;
    *a = lv->sel_anchor;
    *b = lv->sel_focus;
    if (chat_sel_cmp(*a, *b) > 0) {
        ChatSelPos t = *a;
        *a = *b;
        *b = t;
    }
    return chat_sel_cmp(*a, *b) < 0;
}

/* The run context for painting part `part` (part_len bytes) of `item`:
 * runs go to `runs`, text[0] of what is painted is source offset `base`,
 * and the selection range is the part of the current selection that lies
 * in this part. `surface` is the colour the text sits on. */
static void clv_run_ctx(const ChatListView *lv, MdRunCtx *rc,
                        const ChatMsgItem *item, int part, int part_len,
                        TextRuns *runs, int base, COLORREF surface)
{
    memset(rc, 0, sizeof(*rc));
    rc->runs = runs;
    rc->base = base;
    rc->part = part;
    rc->focus = RGB_FROM_THEME(ui_theme_sel_hue(ns_tokens(), 0));
    rc->surface = surface;
    ChatSelPos a, b;
    int s = 0, e = 0;
    if (clv_sel_ordered(lv, &a, &b) &&
        chat_sel_part_range(a, b, item->id, part, part_len, &s, &e)) {
        rc->sel_start = s;
        rc->sel_end = e;
    }
}
```
- [ ] **Step 8.9: Paint every item through the layouter, recording runs**

Change the forward declaration

```c
static int      paint_user_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                                RECT *rc);
```
to

```c
static int      paint_user_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                                RECT *rc, int pinned);
```
Replace the whole function `paint_user_item()` with:

```c
/* Paint a user bubble. `pinned` paints the copy pinned at the top of the
 * list: its runs go to item->pinned_runs, so a hit on the pin resolves to
 * the text under the pointer (spec 2026-10-18 section 3). */
static int paint_user_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                           RECT *rc, int pinned)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    int cw = (rc->right - rc->left) + 2 * ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));

    /* Right-align the bubble */
    int bubble_w = (rc->right - rc->left);
    int max_w = (cw * 3) / 4;
    if (bubble_w > max_w) bubble_w = max_w;

    RECT bubble;
    bubble.right  = rc->right;
    bubble.left   = rc->right - bubble_w;
    bubble.top    = rc->top;
    bubble.bottom = rc->bottom;

    /* Draw bubble background */
    COLORREF bubble_clr = RGB_FROM_THEME(tc->user_bubble);
    ns_draw_round_fill(hdc, &bubble, ns_scale(R_CARD, CLV_DPI(lv)),
                       bubble_clr, 255);

    /* Text: the plain layouter at the width measure_item() used. */
    MdRunCtx urc;
    clv_run_ctx(lv, &urc, item, TR_PART_MAIN, (int)item->text_len,
                pinned ? &item->pinned_runs : &item->runs, 0, bubble_clr);
    /* The bubble is filled with the accent the focus hue is drawn from, so
     * the highlight takes the bubble's own text colour (ui_theme_sel_hue). */
    urc.focus = RGB_FROM_THEME(ui_theme_sel_hue(ns_tokens(), 1));
    HFONT f = clv_font(lv);
    int text_h = md_render_ex(hdc, item->text ? item->text : "",
                              bubble.left + lv->user_pad_h,
                              bubble.top + lv->user_pad_v,
                              clv_user_text_w(lv, cw), f, f, f, lv->theme,
                              MD_MODE_PLAIN, 1, RGB_FROM_THEME(tc->user_text),
                              NULL, &urc);
    if (!pinned) item->painted_height = text_h + 2 * lv->user_pad_v;

    return rc->bottom - rc->top;
}
```
Replace the functions `draw_ai_segment()` and `draw_ai_text_with_exec()` and the comment between them (everything from `static int draw_ai_segment(ChatListView *lv, HDC hdc, const char *text,` up to, not including, `static int paint_ai_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,`) with:

```c
static int draw_ai_segment(ChatListView *lv, HDC hdc, const char *text,
                           int x, int y, int width, int item_id,
                           int *blk_next, MdRunCtx *rc)
{
    if (lv->render_markdown) {
        int want = blk_next && item_id >= 0 && item_id < CLV_ITEM_ID_LIMIT;
        NsRect rects[CLV_CODE_BLOCKS_PER_ITEM];
        MdBlockOut bo = { rects, CLV_CODE_BLOCKS_PER_ITEM,
                          ns_scale(SZ_ICON, CLV_DPI(lv))
                              + ns_scale(SP_XS, CLV_DPI(lv)), 0 };
        int h = md_render_ex(hdc, text, x, y, width,
                             lv->hFont, lv->hMonoFont, lv->hBoldFont,
                             lv->theme, MD_MODE_MARKDOWN, 1, 0,
                             want ? &bo : NULL, rc);
        if (want && bo.count > 0)
            paint_code_copy_glyphs(lv, hdc, text, item_id, &bo, blk_next);
        return h;
    }
    HFONT f = clv_font(lv);
    return md_render_ex(hdc, text, x, y, width, f, f, f, lv->theme,
                        MD_MODE_PLAIN, 1,
                        RGB_FROM_THEME(lv->theme->text_main), NULL, rc);
}

/* Draw an AI reply's text: the chat_exec_split() segments one under the
 * other -- prose through draw_ai_segment(), each command in the mono font
 * and the info hue through the plain layouter. Every segment records its
 * runs into item->runs at its own source offset. Returns the height
 * painted. */
static int draw_ai_text_with_exec(ChatListView *lv, HDC hdc,
                                  ChatMsgItem *item, const RECT *rc)
{
    const char *text = item->text ? item->text : "";
    int len = (int)item->text_len;
    int blk = 0;   /* code-block number within this item, across segments */
    COLORREF exec_clr = RGB_FROM_THEME(ns_tokens()->info.base);
    COLORREF surface = RGB_FROM_THEME(lv->theme->bg_primary);
    HFONT mono = clv_mono(lv);
    int width = rc->right - rc->left;
    int y = rc->top, pos = 0;
    ChatSeg seg;

    while (chat_exec_split(text, len, &pos, &seg)) {
        int n = seg.end - seg.start;
        if (n <= 0) continue;
        char *s = (char *)malloc((size_t)n + 1);
        if (!s) continue;
        memcpy(s, text + seg.start, (size_t)n);
        s[n] = '\0';
        MdRunCtx mrc;
        clv_run_ctx(lv, &mrc, item, TR_PART_MAIN, len, &item->runs,
                    seg.start, surface);
        if (seg.kind == CHAT_SEG_PROSE)
            y += draw_ai_segment(lv, hdc, s, rc->left, y, width, item->id,
                                 &blk, &mrc);
        else
            y += md_render_ex(hdc, s, rc->left, y, width, mono, mono, mono,
                              lv->theme, MD_MODE_PLAIN, 1, exec_clr, NULL,
                              &mrc);
        free(s);
    }
    return y - rc->top;
}
```
In `paint_ai_item()`, the Thinking body:

Replace

```c
            SetTextColor(hdc, dim_clr);
            HGDIOBJ tf = SelectObject(hdc, lv->hFont ? lv->hFont
                                      : GetStockObject(DEFAULT_GUI_FONT));
            RECT text_body_rc = { clip_rc.left, clip_rc.top - eff_scroll,
                                  clip_rc.right,
                                  clip_rc.top - eff_scroll + full_h };
            draw_text_utf8(hdc, item->u.ai.thinking_text, &text_body_rc,
                           DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hdc, tf);
```
with

```c
            /* The body through the plain layouter at the width it was
             * measured at (build_thinking_layout's think_cache_text_w,
             * which is this clip's width); a run is recorded only where it
             * shows inside the clip. */
            const char *tt = item->u.ai.thinking_text;
            MdRunCtx trc;
            clv_run_ctx(lv, &trc, item, TR_PART_THINKING, (int)strlen(tt),
                        &item->runs, 0,
                        RGB_FROM_THEME(tok->bg_secondary.base));
            trc.clip_top = clip_rc.top;
            trc.clip_bottom = clip_rc.bottom;
            HFONT tfont = clv_font(lv);
            md_render_ex(hdc, tt, clip_rc.left, clip_rc.top - eff_scroll,
                         item->u.ai.think_cache_text_w, tfont, tfont, tfont,
                         lv->theme, MD_MODE_PLAIN, 1, dim_clr, NULL, &trc);
```
the main text:

Replace

```c
    RECT text_rc;
    text_rc.left   = rc->left + lv->ai_indent;
    text_rc.top    = content_top;
    text_rc.right  = rc->right - side_pad;
    text_rc.bottom = rc->bottom;
    int text_h = draw_ai_text_with_exec(lv, hdc, item->text, &text_rc,
                                       item->id);
```
with

```c
    RECT text_rc;
    text_rc.left   = rc->left + lv->ai_indent;
    text_rc.top    = content_top;
    text_rc.right  = text_rc.left
                     + clv_ai_text_w(lv, (rc->right - rc->left) + 2 * side_pad);
    text_rc.bottom = rc->bottom;
    int text_h = draw_ai_text_with_exec(lv, hdc, item, &text_rc);
```
and its painted height:

Replace

```c
    int painted_h = (content_top - rc->top) + text_h;
    return painted_h > (rc->bottom - rc->top) ? painted_h : (rc->bottom - rc->top);
```
with

```c
    int painted_h = (content_top - rc->top) + text_h;
    item->painted_height = painted_h;
    return painted_h > (rc->bottom - rc->top) ? painted_h : (rc->bottom - rc->top);
```
Replace the whole function `paint_status_item()` with:

```c
static int paint_status_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                             RECT *rc)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    int side_pad = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    int cw = (rc->right - rc->left) + 2 * side_pad;

    /* The plain layouter in the small font at the width measure_item()
     * used (clv_status_text_w), one side_pad in from the item rect. */
    MdRunCtx prc;
    clv_run_ctx(lv, &prc, item, TR_PART_MAIN, (int)item->text_len,
                &item->runs, 0, RGB_FROM_THEME(lv->theme->bg_primary));
    HFONT f = clv_small(lv);
    int text_h = md_render_ex(hdc, item->text ? item->text : "",
                              rc->left + side_pad, rc->top,
                              clv_status_text_w(lv, cw), f, f, f, lv->theme,
                              MD_MODE_PLAIN, 1, RGB_FROM_THEME(tc->status_text),
                              NULL, &prc);
    item->painted_height = text_h + ns_scale(8, CLV_DPI(lv));
    return rc->bottom - rc->top;
}
```
- [ ] **Step 8.10: `on_paint()` resets the runs; `WM_PRINTCLIENT`**

Replace

```c
static void on_paint(ChatListView *lv)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(lv->hwnd, &ps);

    /* Copy glyphs on code blocks are re-recorded by this paint. */
    clv_code_hits_clear(lv);
    lv->sticky_bottom = 0;
```
with

```c
/* Paint the whole list into `hdc` (WM_PAINT's, or the DC WM_PRINTCLIENT
 * hands over). */
static void paint_frame(ChatListView *lv, HDC hdc)
{
    /* Copy glyphs on code blocks are re-recorded by this paint. */
    clv_code_hits_clear(lv);
    lv->sticky_bottom = 0;

    /* Character selection: every item's runs are re-recorded by this paint
     * too. Reset all of them -- an item not painted this frame must have
     * none, so no rect from an older width or scroll position survives
     * (spec 2026-10-18 section 3). */
    lv->pinned_id = 0;
    for (ChatMsgItem *ri = lv->msg_list ? lv->msg_list->head : NULL; ri;
         ri = ri->next) {
        text_runs_reset(&ri->runs);
        text_runs_reset(&ri->pinned_runs);
        ri->painted_height = 0;
    }
```
In the empty-state early return of that function:

Replace

```c
        BitBlt(hdc, 0, 0, cw, ch, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_bmp);
        DeleteObject(bmp);
        DeleteDC(mem_dc);
        EndPaint(lv->hwnd, &ps);
        return;
    }
```
with

```c
        BitBlt(hdc, 0, 0, cw, ch, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_bmp);
        DeleteObject(bmp);
        DeleteDC(mem_dc);
        return;
    }
```
Replace

```c
                painted_h = paint_user_item(lv, mem_dc, item, &item_rc);
```
with

```c
                painted_h = paint_user_item(lv, mem_dc, item, &item_rc, 0);
```
Replace

```c
        paint_user_item(lv, mem_dc, last_user, &sticky_rc);
```
with

```c
        paint_user_item(lv, mem_dc, last_user, &sticky_rc, 1);
        lv->pinned_id = last_user->id;
```
and at its end:

Replace

```c
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem_dc);

    EndPaint(lv->hwnd, &ps);
}
```
with

```c
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem_dc);
}

static void on_paint(ChatListView *lv)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(lv->hwnd, &ps);
    paint_frame(lv, hdc);
    EndPaint(lv->hwnd, &ps);
}
```
In `ChatListWndProc`:

Replace

```c
    case WM_PAINT:
        on_paint(lv);
        return 0;
```
with

```c
    case WM_PAINT:
        on_paint(lv);
        return 0;

    case WM_PRINTCLIENT:
        /* Paint into the caller's DC: the make wintest harness renders a
         * hidden list view this way (no WM_PAINT reaches a hidden window). */
        paint_frame(lv, (HDC)wParam);
        return 0;
```
- [ ] **Step 8.11: Run the tests and the checks**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | grep -E "^\[FAIL|Tests Run"
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/ui_theme.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/core/ui_theme.c src/ui/chat_listview.c src/ui/ai_chat.c src/ui/window.c
wineserver -k; xvfb-run -a make wintest 2>&1 | grep -E "^\[FAIL|^Tests Run"
```
Expected: `Tests Run: 3008, Failed: 0`; the compilers print nothing; `make wintest` still `Tests Run: 10, Failed: 0`. Read the diff once more for the one rule the compiler cannot check: every width passed to the layouter in a paint routine is the `clv_*_text_w()` that `measure_item()` used for the same item (Task 12's wintest compares the heights).

- [ ] **Step 8.12: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/core/ui_theme.h src/core/ui_theme.c src/ui/chat_listview.h src/ui/chat_listview.c tests/test_sel_contrast.c tests/runner.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
feat(ui): every chat item on the layouter, runs recorded by paint

User, status, tool, Thinking and [EXEC] text now go through
md_render_ex() in MD_MODE_PLAIN, measured at the width they are painted
at; each paint resets every item's runs and records the words it draws,
segment by segment at their source offsets, the Thinking body only
inside its clip and the pinned bubble into its own set. The Thinking
layout is cached per text length, width and line height, and the
hover, click and wheel hit-tests use the box width paint uses.
Turning markdown off now remeasures every item. The highlight hue is
ui_theme_sel_hue(): the focus token, or the bubble's text colour on
the user bubble, where a focus tint was invisible.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 9: Chat list -- selection by id, mouse, keys, menu and copy

Spec section 5. The band selection goes: anchor and focus are (message id, part, offset) pairs resolved by id on every use; button-down hit-tests the runs, a drag past 4 px moves the focus (repainting first so the runs follow any scroll), button-up re-hits and copies nothing; double-click selects a word (a code token in a code line), a third click within the double-click time selects the source line; Ctrl+C, Ctrl+A, Escape and the context menu work on it; the copy is `chat_copy_selection()` into a heap buffer. `chat_strip_exec_markers()` goes with the band copy it served.

No native test can drive a window procedure. This task's behaviour is tested by `test_chat_select_win_mouse_and_keys` in Task 12 (drag, click-to-clear, double-click, triple-click, code token, Ctrl+A, Escape) and by the harness cases of Task 14; here the checks are the strict compile and `make test`.

**Toolchain:** this task changes `src/ui`, which `make test` never compiles. Compile-check it with the MinGW `-fsyntax-only` command in its run step (automaton has the toolchain); where no MinGW toolchain exists, rely on `make test` and a careful read of the diff. `make clean && make release` happens once, in Task 16, after the version bump; the maintainer builds the exe and runs `mingw32-make wintest` on Windows before the PR leaves draft.

**Files:**
- Modify: `src/ui/chat_listview.h` -- the selection fields (as left by Task 8), one declaration before `chat_listview_invalidate()`
- Modify: `src/ui/chat_listview.c` -- includes, forward declarations (~224), the selection block (`item_in_selection()` .. `sel_clear()`, ~419-519), `chat_listview_invalidate()` (new function before it), `paint_frame()` (selection check after the run reset; the `InvertRect` block), `WM_LBUTTONDOWN`, new `WM_LBUTTONDBLCLK`, `WM_MOUSEMOVE` (drag part), `WM_LBUTTONUP`, new `WM_CAPTURECHANGED`, `WM_RBUTTONDOWN`, `WM_MOUSEWHEEL` (end), `WM_KEYDOWN` (Ctrl+C)
- Modify: `src/core/chat_msg.h` (~187-191), `src/core/chat_msg.c` (~337-358)
- Test: `tests/test_chat_msg.c` (remove the four `chat_strip_exec_markers` tests, ~813-857), `tests/runner.c` (their four declarations and calls)

**Interfaces:**
- Consumes: `clv_sel_ordered()`, `clv_run_ctx()`, `clv_font()`, `paint_frame()`, `ChatListView.sel_anchor/sel_focus/pinned_id` (Task 8); `text_runs_hit()`, `text_runs_run_at()`, `text_runs_word()`, `text_line_at()` (Task 3); `chat_msg_find()` (Task 4); `chat_copy_selection()`, `chat_sel_all()`, `chat_sel_cmp()` (Task 5); `clip_copy_utf8()` (`clipboard.h`); `secure_zero()` (`secure_zero.h`).
- Produces:
  - `int chat_listview_copy_selection(HWND hwnd);` -- copies and returns 1 only when the list view has keyboard focus and a selection.
  - `ChatListView` fields `sel_active, sel_valid, sel_moved, sel_down_x, sel_down_y, sel_last_x, sel_last_y, sel_keep_on_up, dbl_armed, DWORD dbl_time, dbl_x, dbl_y` (the band's `sel_start_y`/`sel_end_y` are gone).
  - statics Task 13 reuses: `static const char *clv_part_text(const ChatMsgItem *it, int part, int *len);`, `static int clv_hit(ChatListView *lv, int mx, int my, ChatSelPos *out);`.
- Removes: `void chat_strip_exec_markers(char *s);`.

- [ ] **Step 9.1: Header**

In `src/ui/chat_listview.h`:

Replace

```c
    /* Text selection (content-space Y coordinates) */
    int sel_start_y;            /* Content Y where mouse-drag started */
    int sel_end_y;              /* Content Y where mouse-drag currently is */
    int sel_active;             /* 1 = mouse is held down, dragging */
    int sel_valid;              /* 1 = a selection exists to highlight/copy */

    /* Character selection (spec 2026-10-18 section 5): anchor and focus as
     * (message id, part, byte offset), resolved by id on every use. */
    ChatSelPos sel_anchor;
    ChatSelPos sel_focus;

```
with

```c
    /* Character selection (spec 2026-10-18 section 5): anchor and focus as
     * (message id, part, byte offset), resolved by id on every use; the
     * selection is dropped when either id no longer names an item. */
    ChatSelPos sel_anchor;
    ChatSelPos sel_focus;
    int sel_active;             /* 1 = the button is down, dragging */
    int sel_valid;              /* 1 = a non-empty selection exists */
    int sel_moved;              /* 1 = this drag passed the 4 px threshold */
    int sel_down_x, sel_down_y; /* where the button went down */
    int sel_last_x, sel_last_y; /* last drag point, re-hit after a wheel */
    int sel_keep_on_up;         /* a triple-click: keep it on button-up */
    int dbl_armed;              /* a handled double-click may become triple */
    DWORD dbl_time;             /* GetTickCount() of that double-click */
    int dbl_x, dbl_y;           /* where it was */

```
Replace

```c
/* Notify that the message list has changed. Triggers remeasure + repaint. */
```
with

```c
/* Edit > Copy: when this list view has keyboard focus and a selection,
 * copy the selection's visible text to the clipboard and return 1;
 * otherwise return 0 and touch nothing (the caller copies the terminal's
 * selection instead). */
int chat_listview_copy_selection(HWND hwnd);

/* Notify that the message list has changed. Triggers remeasure + repaint. */
```
- [ ] **Step 9.2: Includes and a forward declaration**

In `src/ui/chat_listview.c`, after `#include "text_runs.h"` add `#include "secure_zero.h"`, and replace

```c
#include <commctrl.h>
#include <stdio.h>
```
with

```c
#include <commctrl.h>
#include <limits.h>
#include <stdio.h>
```
Replace

```c
static int      chatlv_list_empty(const ChatListView *lv);
```
with

```c
static int      chatlv_list_empty(const ChatListView *lv);
static int      clv_sel_ordered(const ChatListView *lv, ChatSelPos *a,
                                ChatSelPos *b);
```
- [ ] **Step 9.3: Replace the band selection**

Replace everything from `/* ── Selection: check if an item overlaps the selection range ──────── */` up to (not including) the three-line banner whose middle line is ` *  Registration` with:

```c
/* ── Selection: hit-test the list (spec 2026-10-18 sections 4 and 5) ── */

/* The text of part `part` of `it` and its length. */
static const char *clv_part_text(const ChatMsgItem *it, int part, int *len)
{
    const char *t = it->text;
    if (part == TR_PART_THINKING && it->type == CHAT_ITEM_AI_TEXT)
        t = it->u.ai.thinking_text;
    if (!t) t = "";
    *len = (t == it->text) ? (int)it->text_len : (int)strlen(t);
    return t;
}

/* The first (end = 0) or last (end = 1) offset of an item that has runs. */
static ChatSelPos clv_item_edge(const ChatMsgItem *it, int end)
{
    ChatSelPos p = { it->id, TR_PART_MAIN, 0 };
    int part = TR_PART_MAIN;
    int off = end ? text_runs_hit(&it->runs, 0, INT_MAX / 2, &part)
                  : text_runs_hit(&it->runs, INT_MIN / 2, INT_MIN / 2, &part);
    p.part = part;
    p.off = off < 0 ? 0 : off;
    return p;
}

/* Map a client point to a selection end. The pinned copy of a user bubble
 * answers for the band it covers; otherwise the item whose band (its rect
 * and the gap below it, laid out as on_paint() lays them) holds the point
 * answers through its runs. An item with no runs there -- a command card,
 * or an item this frame did not paint -- resolves to the nearer edge of the
 * nearest item that has runs; below every item, the end of the last one.
 * Returns 0 when no painted item has text. */
static int clv_hit(ChatListView *lv, int mx, int my, ChatSelPos *out)
{
    if (!lv->msg_list) return 0;

    if (lv->pinned_id && my < lv->sticky_bottom) {
        ChatMsgItem *pu = chat_msg_find(lv->msg_list, lv->pinned_id);
        if (pu && pu->pinned_runs.n > 0) {
            int part = TR_PART_MAIN;
            int off = text_runs_hit(&pu->pinned_runs, mx, my, &part);
            if (off >= 0) {
                out->msg_id = pu->id;
                out->part = part;
                out->off = off;
                return 1;
            }
        }
    }

    ChatMsgItem *band = NULL, *last = NULL;
    int band_top = 0, band_bot = 0;
    int y = lv->msg_gap - lv->scroll_y;
    for (ChatMsgItem *it = lv->msg_list->head; it; it = it->next) {
        int h = it->measured_height;
        if (h == 0) continue;
        if (!band && my < y + h + lv->msg_gap) {
            band = it;
            band_top = y;
            band_bot = y + h + lv->msg_gap;
        }
        if (it->runs.n > 0) last = it;
        y += h + lv->msg_gap;
    }
    if (!last) return 0;
    if (!band) {
        *out = clv_item_edge(last, 1);
        return 1;
    }
    if (band->runs.n > 0) {
        int part = TR_PART_MAIN;
        int off = text_runs_hit(&band->runs, mx, my, &part);
        out->msg_id = band->id;
        out->part = part;
        out->off = off < 0 ? 0 : off;
        return 1;
    }
    ChatMsgItem *prev = band->prev, *next = band->next;
    while (prev && prev->runs.n == 0) prev = prev->prev;
    while (next && next->runs.n == 0) next = next->next;
    if (prev && (!next || my - band_top < band_bot - my))
        *out = clv_item_edge(prev, 1);
    else if (next)
        *out = clv_item_edge(next, 0);
    else
        *out = clv_item_edge(last, 1);
    return 1;
}

/* The run under the point and the item it belongs to -- the pinned copy
 * first, where it covers the list. NULL when the point is on no run. */
static ChatMsgItem *clv_run_at(ChatListView *lv, int mx, int my,
                               const TextRuns **runs, const TextRun **run)
{
    if (!lv->msg_list) return NULL;
    if (lv->pinned_id && my < lv->sticky_bottom) {
        ChatMsgItem *pu = chat_msg_find(lv->msg_list, lv->pinned_id);
        int ri = pu ? text_runs_run_at(&pu->pinned_runs, mx, my) : -1;
        if (ri < 0) return NULL;
        *runs = &pu->pinned_runs;
        *run = &pu->pinned_runs.v[ri];
        return pu;
    }
    for (ChatMsgItem *it = lv->msg_list->head; it; it = it->next) {
        int ri = text_runs_run_at(&it->runs, mx, my);
        if (ri >= 0) {
            *runs = &it->runs;
            *run = &it->runs.v[ri];
            return it;
        }
    }
    return NULL;
}

static void clv_set_sel(ChatListView *lv, int id, int part, int s, int e)
{
    lv->sel_anchor.msg_id = id;
    lv->sel_anchor.part = part;
    lv->sel_anchor.off = s;
    lv->sel_focus.msg_id = id;
    lv->sel_focus.part = part;
    lv->sel_focus.off = e;
    lv->sel_valid = e > s;
    lv->sel_active = 0;
    InvalidateRect(lv->hwnd, NULL, FALSE);
}

/* Double-click: the word under the point (a prose run less its trailing
 * blanks, or the whitespace-delimited token in a code line). Returns 1
 * when it selected something. */
static int clv_select_word(ChatListView *lv, int mx, int my)
{
    const TextRuns *runs = NULL;
    const TextRun *run = NULL;
    ChatMsgItem *it = clv_run_at(lv, mx, my, &runs, &run);
    if (!it || !run || run->synthetic[0]) return 0;
    int part = run->part;
    int off = text_runs_hit(runs, mx, my, &part);
    int len = 0;
    const char *text = clv_part_text(it, run->part, &len);
    int s = 0, e = 0;
    text_runs_word(text, len, run, off, &s, &e);
    if (e <= s) return 0;
    clv_set_sel(lv, it->id, run->part, s, e);
    return 1;
}

/* Triple-click: the source line under the point -- a paragraph, as every
 * source newline is a hard break. Returns 1 when it selected something. */
static int clv_select_line(ChatListView *lv, int mx, int my)
{
    ChatSelPos p;
    if (!clv_hit(lv, mx, my, &p)) return 0;
    ChatMsgItem *it = chat_msg_find(lv->msg_list, p.msg_id);
    if (!it) return 0;
    int len = 0;
    const char *text = clv_part_text(it, p.part, &len);
    int s = 0, e = 0;
    text_line_at(text, len, p.off, &s, &e);
    if (e <= s) return 0;
    clv_set_sel(lv, it->id, p.part, s, e);
    return 1;
}

/* Drag: the focus follows the point. The runs must be current, so any
 * paint a scroll, wheel, resize or stream left pending happens first. */
static void clv_drag_to(ChatListView *lv, int mx, int my)
{
    UpdateWindow(lv->hwnd);
    ChatSelPos p;
    if (clv_hit(lv, mx, my, &p)) lv->sel_focus = p;
    lv->sel_valid = chat_sel_cmp(lv->sel_anchor, lv->sel_focus) != 0;
    InvalidateRect(lv->hwnd, NULL, FALSE);
}

/* ── Selection: copy the visible text to the clipboard ──────────────── */

/* Ctrl+C, the context menu and Edit > Copy: the visible text of the
 * selection (chat_copy_selection), heap-sized, no cap. Returns 1 when the
 * clipboard now holds it. */
static int sel_copy_to_clipboard(ChatListView *lv)
{
    ChatSelPos a, b;
    if (!clv_sel_ordered(lv, &a, &b)) return 0;
    char *buf = NULL;
    size_t n = chat_copy_selection(lv->msg_list, a, b, lv->render_markdown,
                                   &buf);
    int ok = 0;
    if (buf && n > 0) ok = clip_copy_utf8(lv->hwnd, buf, n);
    if (buf) {
        secure_zero(buf, n);
        free(buf);
    }
    return ok;
}

/* ── Selection: select all items ──────────────────────────────────── */

static void sel_select_all(ChatListView *lv)
{
    ChatSelPos a, b;
    if (!chat_sel_all(lv->msg_list, &a, &b)) return;
    lv->sel_anchor = a;
    lv->sel_focus = b;
    lv->sel_valid = 1;
    lv->sel_active = 0;
    InvalidateRect(lv->hwnd, NULL, FALSE);
}

/* ── Selection: clear selection ───────────────────────────────────── */

static void sel_clear(ChatListView *lv)
{
    if (lv->sel_valid || lv->sel_active) {
        lv->sel_valid = 0;
        lv->sel_active = 0;
        InvalidateRect(lv->hwnd, NULL, FALSE);
    }
}
```
- [ ] **Step 9.4: Paint: drop vanished selections and the inverted band**

In `paint_frame()`, right after the run-reset loop added in Task 8 (its closing `}` follows `ri->painted_height = 0;`), insert:

```c
    /* A selection whose message is gone (session switch, stream cancel)
     * is dropped: ends are resolved by id on every use. */
    if ((lv->sel_valid || lv->sel_active) && lv->msg_list &&
        (!chat_msg_find(lv->msg_list, lv->sel_anchor.msg_id) ||
         !chat_msg_find(lv->msg_list, lv->sel_focus.msg_id))) {
        lv->sel_valid = 0;
        lv->sel_active = 0;
    }
```
Delete the whole block that starts `            /* Selection highlight: invert colours for items in selection */` and ends with the `}` closing `if (lv->sel_valid) {` (the `InvertRect` call is inside it); the highlight is now drawn under each run by `md_render`.

- [ ] **Step 9.5: The mouse**

Replace the whole `case WM_LBUTTONDOWN: { ... }` block (up to, not including, `    case WM_MOUSEMOVE: {`) with:

```c
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        /* Triple-click: a down soon after a handled double-click, at the
         * same point, selects the source line -- taken before the normal
         * path so nothing clears it, and no capture is taken. */
        if (lv->dbl_armed) {
            lv->dbl_armed = 0;
            int dx = mx - lv->dbl_x, dy = my - lv->dbl_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (GetTickCount() - lv->dbl_time <= GetDoubleClickTime() &&
                dx <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
                dy <= GetSystemMetrics(SM_CYDOUBLECLK) / 2) {
                SetFocus(hwnd);
                if (clv_select_line(lv, mx, my)) {
                    lv->sel_keep_on_up = 1;
                    return 0;
                }
            }
        }

        if (on_lbuttondown(lv, mx, my)) {
            sel_clear(lv);
            return 0;  /* A button/link was clicked */
        }
        /* Start a character selection: anchor = focus at the hit. */
        ChatSelPos p;
        if (!clv_hit(lv, mx, my, &p)) {
            sel_clear(lv);
            return 0;
        }
        lv->sel_anchor = p;
        lv->sel_focus = p;
        lv->sel_valid = 0;
        lv->sel_active = 1;
        lv->sel_moved = 0;
        lv->sel_down_x = lv->sel_last_x = mx;
        lv->sel_down_y = lv->sel_last_y = my;
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        /* Over a glyph, tickbox, button, link or the Thinking header it
         * does nothing, as the unhandled message did: the first click of
         * the pair already acted, and it is not forwarded, so nothing
         * toggles twice. */
        if ((chatlv_list_empty(lv) && lv->state_id >= 0) ||
            chatlv_code_hit_at(lv, mx, my) ||
            chatlv_hit_is_actionable(chatlv_hover_hit(lv, mx, my, NULL)))
            return 0;
        if (clv_select_word(lv, mx, my)) {
            lv->dbl_armed = 1;
            lv->dbl_time = GetTickCount();
            lv->dbl_x = mx;
            lv->dbl_y = my;
        }
        return 0;
    }
```
In `case WM_MOUSEMOVE:` replace the drag part -- from the line `        if (!lv->sel_active) break;` through the `return 0;` and `}` that close the case -- with:

```c
        if (!lv->sel_active) break;
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        lv->sel_last_x = mx;
        lv->sel_last_y = my;
        if (!lv->sel_moved) {
            int dx = mx - lv->sel_down_x, dy = my - lv->sel_down_y;
            if (dx >= -4 && dx <= 4 && dy >= -4 && dy <= 4)
                return 0;  /* under the 4 px threshold: still a click */
            lv->sel_moved = 1;
        }
        /* Auto-scroll when dragging past an edge; clv_drag_to() repaints
         * before it hit-tests, so the runs follow the scroll. */
        if (my < 0) {
            lv->scroll_y += my;  /* my is negative, scrolls up */
            clamp_scroll(lv);
            after_user_scroll(lv);
        } else if (my > lv->viewport_height) {
            lv->scroll_y += my - lv->viewport_height;
            clamp_scroll(lv);
            after_user_scroll(lv);
        }
        clv_drag_to(lv, mx, my);
        return 0;
    }
```
Replace the whole `case WM_LBUTTONUP: { ... }` block (up to, not including, `    case WM_RBUTTONDOWN: {`) with:

```c
    case WM_LBUTTONUP: {
        /* A triple-click took no capture and keeps what it selected. */
        if (lv->sel_keep_on_up) {
            lv->sel_keep_on_up = 0;
            return 0;
        }
        if (!lv->sel_active) {
            if (GetCapture() == hwnd) ReleaseCapture();
            break;
        }
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        if (lv->sel_moved) {
            clv_drag_to(lv, mx, my);      /* re-hit at the release point */
        } else {
            lv->sel_valid = 0;            /* a click: clears the selection */
            InvalidateRect(hwnd, NULL, FALSE);
        }
        lv->sel_active = 0;               /* before WM_CAPTURECHANGED */
        ReleaseCapture();
        /* Nothing is copied on release: Ctrl+C, Edit > Copy or the menu. */
        return 0;
    }

    case WM_CAPTURECHANGED:
        /* Capture taken away mid-drag (a menu, another window): end the
         * drag where it is. */
        lv->sel_active = 0;
        return 0;
```
In `case WM_RBUTTONDOWN:` replace

```c
        if (pick == CM_COPY) {
            if (lv->sel_valid) sel_copy_to_clipboard(lv);
        }
```
with

```c
        if (pick == CM_COPY) {
            sel_copy_to_clipboard(lv);
        }
```
At the end of `case WM_MOUSEWHEEL:` (wheel while dragging):

Replace

```c
        int old_pos = lv->scroll_y;
        lv->scroll_y -= (delta * scroll_amount) / WHEEL_DELTA;
        clamp_scroll(lv);

        if (lv->scroll_y != old_pos) {
            after_user_scroll(lv);
        }
        return 0;
```
with

```c
        int old_pos = lv->scroll_y;
        lv->scroll_y -= (delta * scroll_amount) / WHEEL_DELTA;
        clamp_scroll(lv);

        if (lv->scroll_y != old_pos) {
            after_user_scroll(lv);
            /* Wheel while dragging: the text moved under a still pointer,
             * so the focus follows to what is now under it. */
            if (lv->sel_active) {
                lv->sel_moved = 1;
                clv_drag_to(lv, lv->sel_last_x, lv->sel_last_y);
            }
        }
        return 0;
```
- [ ] **Step 9.6: The keys and the public copy**

In `case WM_KEYDOWN:`:

Replace

```c
        /* Ctrl+C: copy selection to clipboard */
        if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (lv->sel_valid)
                sel_copy_to_clipboard(lv);
            return 0;
        }
```
with

```c
        /* Ctrl+C: copy the visible text of the selection */
        if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            sel_copy_to_clipboard(lv);
            return 0;
        }
```
(Ctrl+A and Escape keep their code: `sel_select_all()` and `sel_clear()` now do the new thing.) Insert immediately before `void chat_listview_invalidate(HWND hwnd)`:

```c
int chat_listview_copy_selection(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv || GetFocus() != hwnd || !lv->sel_valid) return 0;
    sel_copy_to_clipboard(lv);
    return 1;
}

```
- [ ] **Step 9.7: Remove `chat_strip_exec_markers()`**

In `src/core/chat_msg.h` delete:

```c
/* Remove every "[EXEC]" and "[/EXEC]" marker from the NUL-terminated string
 * in place, keeping the text between them. Nothing else changes. NULL is a
 * no-op. Used on band-selection text before it goes to the clipboard. */
void chat_strip_exec_markers(char *s);

```
In `src/core/chat_msg.c` delete the whole function `void chat_strip_exec_markers(char *s)`. In `tests/test_chat_msg.c` delete everything from `/* --- chat_strip_exec_markers --- */` up to (not including) `/* --- utf8_cut_at --- */`. In `tests/runner.c` delete these eight lines:

```c
int test_chat_strip_exec_markers_basic(void);
int test_chat_strip_exec_markers_multiple_and_adjacent(void);
int test_chat_strip_exec_markers_unpaired(void);
int test_chat_strip_exec_markers_leaves_everything_else(void);
    failed += test_chat_strip_exec_markers_basic();
    failed += test_chat_strip_exec_markers_multiple_and_adjacent();
    failed += test_chat_strip_exec_markers_unpaired();
    failed += test_chat_strip_exec_markers_leaves_everything_else();
```
- [ ] **Step 9.8: Run the tests and the checks**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | grep -E "^\[FAIL|Tests Run"
grep -rn "chat_strip_exec_markers\|sel_start_y\|sel_end_y\|item_in_selection\|sel_extract_text\|InvertRect" src tests
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/chat_msg.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/core/chat_msg.c src/ui/chat_listview.c src/ui/ai_chat.c src/ui/window.c
```
Expected: `Tests Run: 3004, Failed: 0`; the grep prints nothing; the compilers print nothing.

- [ ] **Step 9.9: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/ui/chat_listview.h src/ui/chat_listview.c src/core/chat_msg.h src/core/chat_msg.c tests/test_chat_msg.c tests/runner.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
feat(ui): character selection in the AI panel by mouse and keys

The band selection goes. Anchor and focus are (message id, part, byte
offset), resolved by id on every use and dropped when an id vanishes.
A button-down hit-tests the painted runs; a drag past 4 px moves the
focus, repainting first so the runs follow an auto-scroll or a wheel;
the button-up re-hits and copies nothing. Double-click takes a word (a
token in a code line), a third click the source line. Ctrl+C, the menu
and Ctrl+A act on it, and the copy is the visible text from
chat_copy_selection() in a heap buffer with no cap.
chat_strip_exec_markers() goes with the band copy it served.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 10: Edit > Copy follows focus; the message box is read as UTF-8

`ai_chat_copy_selection()` with the focus check, `do_copy()` asking it first (before its no-session return), and the input read with `GetWindowTextW` + `WideCharToMultiByte(CP_UTF8)` with CR LF turned into LF before the text becomes an item or goes to the model (spec sections 2 and 5).

The input box is created with the ANSI `CreateWindow("EDIT", ...)`, so `GetWindowTextW` hands back its ANSI-code-page text converted to UTF-16: this fixes the byte-boundary problem the spec names (a cp1252 `é` stored as a lone 0xE9) but does not let characters outside the code page in. A Unicode input box is already an open item in the notes.

**Toolchain:** this task changes `src/ui`, which `make test` never compiles. Compile-check it with the MinGW `-fsyntax-only` command in its run step (automaton has the toolchain); where no MinGW toolchain exists, rely on `make test` and a careful read of the diff. `make clean && make release` happens once, in Task 16, after the version bump; the maintainer builds the exe and runs `mingw32-make wintest` on Windows before the PR leaves draft.

**Files:**
- Modify: `src/core/text_lines.h` (after `text_lf_to_crlf()`), `src/core/text_lines.c` (end)
- Modify: `src/ui/ai_chat.c` -- includes (~25), new helper before `send_user_message()` (~1633), its input read (~1642-1656) and end (~1757-1760), new function before `ai_chat_close()` (~5676)
- Modify: `src/ui/ai_chat.h` (CRLF) -- after `ai_chat_close()`
- Modify: `src/ui/window.c` -- `do_copy()` (~2717-2719)
- Test: `tests/test_text_lines.c` (append), `tests/runner.c`

**Interfaces:**
- Consumes: `chat_listview_copy_selection()` (Task 9); `secure_zero()`.
- Produces: `size_t text_crlf_to_lf(char *s);` (core), `int ai_chat_copy_selection(HWND hwnd);` (ui).

- [ ] **Step 10.1: Write the failing test**

Append to `tests/test_text_lines.c`:

```c
/* text_crlf_to_lf: the AI message box's CR LF become LF before the text is
 * a chat item (spec 2026-10-18 section 2). */
int test_text_crlf_to_lf_pairs_become_lf(void)
{
    TEST_BEGIN();
    char a[] = "one\r\ntwo\r\n";
    ASSERT_EQ(text_crlf_to_lf(a), 8);
    ASSERT_STR_EQ(a, "one\ntwo\n");
    char b[] = "x\ry\r\r\nz";
    ASSERT_EQ(text_crlf_to_lf(b), 6);
    ASSERT_STR_EQ(b, "x\ry\r\nz");
    char c[] = "";
    ASSERT_EQ(text_crlf_to_lf(c), 0);
    ASSERT_EQ(text_crlf_to_lf(NULL), 0);
    TEST_END();
}
```
In `tests/runner.c` after `int test_text_lf_null_input(void);` add `int test_text_crlf_to_lf_pairs_become_lf(void);`, and after `    failed += test_text_lf_null_input();` add `    failed += test_text_crlf_to_lf_pairs_become_lf();`.

- [ ] **Step 10.2: Run it and see it fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make test 2>&1 | grep -m1 "text_crlf_to_lf"
```
Expected: `implicit declaration of function 'text_crlf_to_lf'`.

- [ ] **Step 10.3: Implement the core helper**

In `src/core/text_lines.h`, after the line `size_t text_lf_to_crlf(const char *in, char *out, size_t out_sz);` add:

```c

/* The other way, in place: every "\r\n" in the NUL-terminated `s` becomes
 * "\n"; a lone '\r' is kept. Used on the AI message box's text, which a
 * multiline EDIT holds with CR LF, before it becomes a chat item (whose
 * renderer breaks lines on '\n' only). Returns the new length; NULL is
 * empty text. */
size_t text_crlf_to_lf(char *s);
```
Append to `src/core/text_lines.c`:

```c

size_t text_crlf_to_lf(char *s)
{
    if (!s) return 0;
    size_t w = 0;
    for (size_t r = 0; s[r]; r++) {
        if (s[r] == '\r' && s[r + 1] == '\n') continue;
        s[w++] = s[r];
    }
    s[w] = '\0';
    return w;
}
```
- [ ] **Step 10.4: The message box as UTF-8**

In `src/ui/ai_chat.c`, after `#include "text_lines.h"` add `#include "secure_zero.h"`. Insert immediately before `static void send_user_message(AiChatData *d)`:

```c
/* The message box's text as UTF-8 with every CR LF turned into LF (spec
 * 2026-10-18 section 2). Read as UTF-16 rather than with the ANSI
 * GetWindowText, so a user item never holds code-page bytes the selection's
 * UTF-8 boundary rules would misread. Heap (the caller frees it); NULL when
 * memory runs out. */
static char *input_text_utf8(HWND hInput)
{
    int wlen = GetWindowTextLengthW(hInput);
    if (wlen < 0) wlen = 0;
    size_t wbytes = ((size_t)wlen + 1) * sizeof(wchar_t);
    wchar_t *w = (wchar_t *)malloc(wbytes);
    if (!w) return NULL;
    int got = GetWindowTextW(hInput, w, wlen + 1);
    if (got < 0) got = 0;
    if (got > wlen) got = wlen;
    w[got] = L'\0';
    int need = WideCharToMultiByte(CP_UTF8, 0, w, got + 1, NULL, 0, NULL, NULL);
    char *u = need > 0 ? (char *)malloc((size_t)need) : NULL;
    if (u && WideCharToMultiByte(CP_UTF8, 0, w, got + 1, u, need,
                                 NULL, NULL) <= 0)
        u[0] = '\0';
    secure_zero(w, wbytes);
    free(w);
    if (u) text_crlf_to_lf(u);
    return u;
}

```
In `send_user_message()`:

Replace

```c
    char input[2048];
    GetWindowText(d->hInput, input, (int)sizeof(input));
```
with

```c
    char *input = input_text_utf8(d->hInput);
    if (!input) return;
```
Replace

```c
    if (input[0] == '\0') return;
```
with

```c
    if (input[0] == '\0') {
        free(input);
        return;
    }
```
and at the end of the function:

Replace

```c
    /* Clear pending attachment after thread is launched */
    ai_attachment_free(&d->pending_attachment);
}
```
with

```c
    /* Clear pending attachment after thread is launched */
    ai_attachment_free(&d->pending_attachment);

    secure_zero(input, strlen(input));
    free(input);
}
```
(The `[Image attached] ` prefix strip between them works on the heap buffer unchanged; `chat_msg_append()` and `ai_conv_add()` copy the text. The old 2,047-byte cut is gone.)

- [ ] **Step 10.5: Edit > Copy follows focus**

Insert immediately before `void ai_chat_close(HWND hwnd)` in `src/ui/ai_chat.c`:

```c
int ai_chat_copy_selection(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return 0;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d || !d->hChatList) return 0;
    return chat_listview_copy_selection(d->hChatList);
}

```
In `src/ui/ai_chat.h` (CRLF), after `void ai_chat_close(HWND hwnd);` add:

```c

/* Edit > Copy: when the panel's message list has keyboard focus and a
 * selection, copy the selection's visible text and return 1; otherwise
 * return 0 (copy the terminal's selection instead). */
int ai_chat_copy_selection(HWND hwnd);
```
In `src/ui/window.c`:

Replace

```c
static void do_copy(HWND hwnd)
{
    if (!g_active_session || !g_active_session->term) return;
```
with

```c
static void do_copy(HWND hwnd)
{
    /* The AI panel first, and before the no-session return so a panel with
     * no session still copies: it copies, and answers 1, only when its
     * message list has keyboard focus and a selection. */
    if (g_hwndAiChat && ai_chat_copy_selection(g_hwndAiChat)) return;
    if (!g_active_session || !g_active_session->term) return;
```
`copy_selection_and_clear()` (the terminal's Ctrl+C) calls `do_copy()` too, but only when the terminal has its own selection and therefore the focus, so the panel check answers 0 there.

- [ ] **Step 10.6: Run the tests and the checks**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | grep -E "crlf_to_lf|^\[FAIL|Tests Run" | grep -v RUN
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/text_lines.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/core/text_lines.c src/ui/ai_chat.c src/ui/window.c
```
Expected: `[PASS] test_text_crlf_to_lf_pairs_become_lf`, `Tests Run: 3005, Failed: 0`; the compilers print nothing.

- [ ] **Step 10.7: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/core/text_lines.h src/core/text_lines.c src/ui/ai_chat.c src/ui/ai_chat.h src/ui/window.c tests/test_text_lines.c tests/runner.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
feat(ai): Edit > Copy takes the panel selection; read the message box as UTF-8

Edit > Copy asks the AI panel first, before the no-session return, and
the panel copies only while its message list has keyboard focus and a
selection, so the terminal's copy is unchanged otherwise. The message
box is read with GetWindowTextW and converted to UTF-8 with CR LF made
LF, so a user bubble never holds code-page bytes the selection's
boundary rules would misread; the 2 KB cut goes with the stack buffer.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 11: `--ui-demo=chat` shows a selection

Spec section 6: the chat state's reply gains an `[EXEC]` command, its Thinking body is expanded, and a selection runs from the user bubble across the Thinking body, the heading, the command, the list, the table and the code block into the reply's last line. The maintainer reviews this screenshot in the four themes before the PR leaves draft, and `ui_gallery` captures it.

**Toolchain:** this task changes `src/ui`, which `make test` never compiles. Compile-check it with the MinGW `-fsyntax-only` command in its run step (automaton has the toolchain); where no MinGW toolchain exists, rely on `make test` and a careful read of the diff. `make clean && make release` happens once, in Task 16, after the version bump; the maintainer builds the exe and runs `mingw32-make wintest` on Windows before the PR leaves draft.

**Files:**
- Modify: `src/core/ui_demo.c` -- `CHAT_ASSISTANT_MSG` (~76-79), new function after `ui_demo_thinking_text()` (~110), the `ui_demo_scrolls_to_top()` comment (~40)
- Modify: `src/core/ui_demo.h` -- after `ui_demo_thinking_text_long()`
- Modify: `src/ui/chat_listview.h/.c` -- `chat_listview_set_selection()`
- Modify: `src/ui/ai_chat.c` -- `ai_chat_apply_demo_extras()` (~5634-5646 and the end, ~5660-5673)
- Test: `tests/test_ui_demo.c` (includes, append), `tests/runner.c`

**Interfaces:**
- Consumes: `chat_copy_selection()`, `ChatSelPos` (Task 5); `TR_PART_MAIN` (Task 3); `chat_msg_*`.
- Produces: `void ui_demo_chat_selection(int *user_off, int *reply_off);` (core); `void chat_listview_set_selection(HWND hwnd, ChatSelPos a, ChatSelPos b);` (ui). The golden string of `test_ui_demo_chat_drag_across_items_golden` is the one Task 14's `ai_panel_select_across_items` compares the clipboard with.

- [ ] **Step 11.1: Write the failing tests**

In `tests/test_ui_demo.c` replace

```c
#include "chat_msg.h"
#include <string.h>
```
with

```c
#include "chat_msg.h"
#include "chat_copy.h"
#include "text_runs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
```
and append:

```c
/* The "chat" state's selection (spec 2026-10-18 section 6), copied the way
 * the panel copies it: the user turn and the reply replayed as items like
 * chat_rebuild_display() does, the Thinking body expanded as
 * ai_chat_apply_demo_extras() leaves it. */
static void build_chat_items(ChatMsgList *l, AiConversation *conv)
{
    chat_msg_list_init(l);
    chat_msg_append(l, CHAT_ITEM_USER, ai_msg_content(&conv->messages[1]));
    ChatMsgItem *ai = chat_msg_append(l, CHAT_ITEM_AI_TEXT,
                                      ai_msg_content(&conv->messages[2]));
    chat_msg_set_thinking(ai, ui_demo_thinking_text());
    ai->u.ai.thinking_collapsed = 0;
}

int test_ui_demo_chat_selection_spans_every_kind(void)
{
    TEST_BEGIN();
    AiConversation *conv = (AiConversation *)calloc(1, sizeof(*conv));
    ASSERT_NOT_NULL(conv);
    if (!conv) { TEST_END(); }
    ASSERT_EQ(ui_demo_build("chat", conv, NULL, NULL, NULL, 0), 0);
    ChatMsgList l;
    build_chat_items(&l, conv);
    int uo = -1, ro = -1;
    ui_demo_chat_selection(&uo, &ro);
    ChatSelPos a = { l.head->id, TR_PART_MAIN, uo };
    ChatSelPos b = { l.tail->id, TR_PART_MAIN, ro };
    char *out = NULL;
    size_t n = chat_copy_selection(&l, a, b, 1, &out);
    ASSERT_TRUE(n > 0);
    ASSERT_NOT_NULL(out);
    if (out) {
        ASSERT_TRUE(strncmp(out, "disk usage on this box", 22) == 0);
        ASSERT_TRUE(strstr(out, ui_demo_thinking_text()) != NULL);
        ASSERT_TRUE(strstr(out, "I checked the mounts first:\ndf -h /\n") != NULL);
        ASSERT_TRUE(strstr(out, "- Root partition: 42% used") != NULL);
        ASSERT_TRUE(strstr(out, "Mount\tUsed\tAvail") != NULL);
        ASSERT_TRUE(strstr(out, "$ du -sh /var/log/*") != NULL);
        ASSERT_TRUE(strstr(out, "[EXEC]") == NULL);
        ASSERT_TRUE(strstr(out, "**") == NULL);
        ASSERT_TRUE(strstr(out, "```") == NULL);
        ASSERT_TRUE(n >= 7 && strcmp(out + n - 7, "Want me") == 0);
    }
    free(out);
    chat_msg_list_clear(&l);
    ai_conv_reset(conv);
    free(conv);
    TEST_END();
}

/* What the integration case ai_panel_select_across_items
 * (tests/integration/cases/95-ai-select.ps1) expects on the clipboard after
 * dragging from "disk" in the user bubble to the end of "found" in the
 * reply. Keep the two strings identical. */
int test_ui_demo_chat_drag_across_items_golden(void)
{
    TEST_BEGIN();
    AiConversation *conv = (AiConversation *)calloc(1, sizeof(*conv));
    ASSERT_NOT_NULL(conv);
    if (!conv) { TEST_END(); }
    ASSERT_EQ(ui_demo_build("chat", conv, NULL, NULL, NULL, 0), 0);
    ChatMsgList l;
    build_chat_items(&l, conv);
    const char *u = l.head->text, *r = l.tail->text;
    ChatSelPos a = { l.head->id, TR_PART_MAIN, (int)(strstr(u, "disk") - u) };
    ChatSelPos b = { l.tail->id, TR_PART_MAIN,
                     (int)(strstr(r, "found") - r) + 5 };
    char *out = NULL;
    chat_copy_selection(&l, a, b, 1, &out);
    char want[1024];
    snprintf(want, sizeof(want),
             "disk usage on this box and tell me what's eating space?\n\n"
             "%s\n\n"
             "Disk Usage Summary\n\nI checked the mounts first:\ndf -h /\n\n"
             "Here's what I found", ui_demo_thinking_text());
    ASSERT_NOT_NULL(out);
    if (out) ASSERT_STR_EQ(out, want);
    free(out);
    chat_msg_list_clear(&l);
    ai_conv_reset(conv);
    free(conv);
    TEST_END();
}
```
In `tests/runner.c`, after `int test_ui_demo_cmddetail_matches_approval(void);` add

```c
int test_ui_demo_chat_selection_spans_every_kind(void);
int test_ui_demo_chat_drag_across_items_golden(void);
```
and after `    failed += test_ui_demo_cmddetail_matches_approval();` add

```c
    failed += test_ui_demo_chat_selection_spans_every_kind();
    failed += test_ui_demo_chat_drag_across_items_golden();
```
- [ ] **Step 11.2: Run them and see them fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make test 2>&1 | grep -m1 "ui_demo_chat_selection"
```
Expected: `implicit declaration of function 'ui_demo_chat_selection'`.

- [ ] **Step 11.3: The canned reply and the selection (core)**

In `src/core/ui_demo.c`:

Replace

```c
static const char CHAT_ASSISTANT_MSG[] =
    "## Disk Usage Summary\n"
    "\n"
    "Here's what I found on **web-01**:\n"
```
with

```c
static const char CHAT_ASSISTANT_MSG[] =
    "## Disk Usage Summary\n"
    "\n"
    "I checked the mounts first:\n"
    "[EXEC]df -h /[/EXEC]\n"
    "Here's what I found on **web-01**:\n"
```
Replace

```c
const char *ui_demo_thinking_text(void)
{
    return CHAT_THINKING;
}
```
with

```c
const char *ui_demo_thinking_text(void)
{
    return CHAT_THINKING;
}

/* The selection "chat" shows (spec 2026-10-18 section 6): from "disk" in the
 * user turn to just after "Want me" in the reply's last line -- across the
 * bubble, the Thinking body, the heading, the [EXEC] command, the list, the
 * table and the code block. */
void ui_demo_chat_selection(int *user_off, int *reply_off)
{
    const char *u = strstr(CHAT_USER_MSG, "disk");
    const char *r = strstr(CHAT_ASSISTANT_MSG, "Want me");
    if (user_off) *user_off = u ? (int)(u - CHAT_USER_MSG) : 0;
    if (reply_off)
        *reply_off = r ? (int)(r - CHAT_ASSISTANT_MSG) + (int)strlen("Want me")
                       : 0;
}
```
Replace

```c
 * of scrolling past it. "chat"/"thinking" show it collapsed, "all" shows
 * it expanded (ai_chat_apply_demo_extras() forces that). Every other state
```
with

```c
 * of scrolling past it. "thinking" shows it collapsed, "chat" and "all"
 * show it expanded (ai_chat_apply_demo_extras() forces that). Every other state
```
In `src/core/ui_demo.h`, after `const char *ui_demo_thinking_text_long(void);` add:

```c

/* The character selection the "chat" state shows (spec 2026-10-18 section
 * 6), as byte offsets: *user_off into the user turn's text (the start of
 * "disk"), *reply_off into the assistant reply's text (just after "Want
 * me" in its last line). The win32 side resolves the two items and selects
 * from the first to the second; the reply's Thinking body, which "chat"
 * shows expanded, lies between them. */
void ui_demo_chat_selection(int *user_off, int *reply_off);
```
- [ ] **Step 11.4: Set it from the demo (ui)**

In `src/ui/chat_listview.h`, before the comment of `chat_listview_copy_selection()` (`/* Edit > Copy: when this list view has keyboard focus and a selection,`) add:

```c
/* Select from `a` to `b` (either order) and repaint -- the --ui-demo=chat
 * preset (spec 2026-10-18 section 6). */
void chat_listview_set_selection(HWND hwnd, ChatSelPos a, ChatSelPos b);

```
In `src/ui/chat_listview.c`, before `int chat_listview_copy_selection(HWND hwnd)` add:

```c
void chat_listview_set_selection(HWND hwnd, ChatSelPos a, ChatSelPos b)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->sel_anchor = a;
    lv->sel_focus = b;
    lv->sel_active = 0;
    lv->sel_valid = chat_sel_cmp(a, b) != 0;
    InvalidateRect(hwnd, NULL, FALSE);
}

```
In `src/ui/ai_chat.c`, `ai_chat_apply_demo_extras()`:

Replace

```c
    /* Gallery: "chat" shows the Thinking disclosure collapsed (the normal
     * post-reply state); "all" shows it expanded so the review set covers
     * both forms per the design spec's "Thought process" section. Setting
     * the item's own flag (rather than d->show_thinking) is the smallest
     * change -- chat_rebuild_display() always creates it collapsed. */
    if (state && strcmp(state, "all") == 0) {
```
with

```c
    /* Gallery: "chat" and "all" show the Thinking disclosure expanded --
     * "chat" because its selection crosses the Thinking body (spec
     * 2026-10-18 section 6) -- and "thinking" shows it collapsed, so the
     * review set still covers both forms per the design spec's "Thought
     * process" section. Setting the item's own flag (rather than
     * d->show_thinking) is the smallest change -- chat_rebuild_display()
     * always creates it collapsed. */
    if (state && (strcmp(state, "all") == 0 || strcmp(state, "chat") == 0)) {
```
and at the end of the function:

Replace

```c
        if (ui_demo_scrolls_to_top(state))
            chat_listview_scroll_to_top(d->hChatList);
    }
}
```
with

```c
        if (ui_demo_scrolls_to_top(state))
            chat_listview_scroll_to_top(d->hChatList);
    }

    /* "chat": a character selection from the user bubble, across the
     * Thinking body, into the reply's last line -- the screenshot the
     * maintainer reviews in the four themes (spec 2026-10-18 section 6). */
    if (state && strcmp(state, "chat") == 0 && d->hChatList) {
        ChatMsgItem *u = NULL, *ai = NULL;
        for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
            if (!u && it->type == CHAT_ITEM_USER) u = it;
            if (!ai && it->type == CHAT_ITEM_AI_TEXT) ai = it;
        }
        if (u && ai) {
            int uo = 0, ro = 0;
            ui_demo_chat_selection(&uo, &ro);
            ChatSelPos a = { u->id, TR_PART_MAIN, uo };
            ChatSelPos b = { ai->id, TR_PART_MAIN, ro };
            chat_listview_set_selection(d->hChatList, a, b);
        }
    }
}
```
- [ ] **Step 11.5: Run the tests and the checks**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | grep -E "ui_demo_chat|^\[FAIL|Tests Run" | grep -v RUN
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -fsyntax-only src/core/ui_demo.c
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/core/ui_demo.c src/ui/ai_chat.c src/ui/chat_listview.c
```
Expected: both `test_ui_demo_chat_*` pass, `Tests Run: 3007, Failed: 0`; the compilers print nothing. On a box that can build and run the exe, `nutshell.exe --ui-demo=chat --theme "<each theme>"` shows the bubble, the expanded Thinking body, the heading, the command and the reply highlighted (the build itself waits for Task 16).

- [ ] **Step 11.6: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/core/ui_demo.c src/core/ui_demo.h src/ui/chat_listview.h src/ui/chat_listview.c src/ui/ai_chat.c tests/test_ui_demo.c tests/runner.c
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
feat(demo): --ui-demo=chat shows a character selection

The chat state's reply gains an [EXEC] command, shows its Thinking body
open, and carries a selection from the user bubble through the
Thinking text, the heading, the command, the list, the table and the
code block into the last line, for review in the four themes. "thinking"
still shows the disclosure closed.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 12: `make wintest` -- the real list view, painted and clicked

What the native suite cannot see (spec section 7): a hidden chat list view over a fixture, painted into a memory DC through `WM_PRINTCLIENT` at two widths and two DPIs and with markdown off after a toggle; every run's centre hit-tests back into it, `cum[]` is monotonic, every item's measured height equals its painted height, and the painted words equal `chat_copy_visible()` with whitespace set aside (a soft wrap drops a blank the copy keeps). A second case drives the window procedure: drag, click-to-clear, double-click, triple-click, a code token, Ctrl+A, Escape.

**Toolchain:** this task changes `src/ui`, which `make test` never compiles. Compile-check it with the MinGW `-fsyntax-only` command in its run step (automaton has the toolchain); where no MinGW toolchain exists, rely on `make test` and a careful read of the diff. `make clean && make release` happens once, in Task 16, after the version bump; the maintainer builds the exe and runs `mingw32-make wintest` on Windows before the PR leaves draft.

**Files:**
- Create: `tests/test_chat_select_win.c`
- Modify: `Makefile` (CRLF) -- `WIN_ONLY_TESTS`, `WIN_TEST_SRCS`
- Modify: `tests/win_runner.c` -- declarations, calls

**Interfaces:**
- Consumes: everything in `chat_listview.h` (`chat_listview_register/create/set_fonts/set_render_markdown/invalidate`, `ChatListView` fields `sel_valid`, `sel_anchor`, `sel_focus`, `render_markdown`), `WM_PRINTCLIENT` (Task 8), the mouse and key handlers (Task 9), `chat_copy_visible/selection()`, `chat_sel_all()` (Task 5), `text_runs_hit()` (Task 3).
- Produces: `int test_chat_select_win_runs_match_paint(void);`, `int test_chat_select_win_mouse_and_keys(void);` and, for Task 13, the file-local helpers `view_open()`, `view_paint()`, `view_close()`, `fill_fixture()`, `find_word()`, `TestView`. The file defines `ns_tokens()` and `ns_reduced_motion()` (window.c's, which the harness does not link) and `ai_word_count()` (ai_prompt.c's, which would pull in the HTTP and tool stack).

- [ ] **Step 12.1: Write the tests**

Create `tests/test_chat_select_win.c`:

```c
/* tests/test_chat_select_win.c -- make wintest: the real GDI renderer and
 * chat list view behind character selection (spec docs/superpowers/specs/
 * 2026-10-18-ai-panel-text-selection-design.md section 7). A hidden list
 * view is filled with a fixture, painted into a memory DC through
 * WM_PRINTCLIENT at two widths and two DPIs, and its recorded runs checked:
 * cum[] monotonic, the centre of every run hit-tests back into it, every
 * item's measured height equals its painted height, and the painted words
 * equal chat_copy_visible() with whitespace set aside. */

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "chat_listview.h"
#include "chat_msg.h"
#include "chat_copy.h"
#include "text_runs.h"
#include "ui_theme.h"
#include "ns_reduced_motion.h"
#include "ai_prompt.h"

/* window.c owns these in the app; this harness links without it. */
static ThemeTokens g_win_tokens;
const ThemeTokens *ns_tokens(void) { return &g_win_tokens; }
int ns_reduced_motion(void) { return 1; }

/* ai_prompt.c drags in the HTTP and tool stack; the list view only needs
 * this for the Thinking summary's word count. */
int ai_word_count(const char *text)
{
    int n = 0, in = 0;
    for (; text && *text; text++) {
        int sp = (*text == ' ' || *text == '\t' || *text == '\n' ||
                  *text == '\r');
        if (!sp && !in) n++;
        in = !sp;
    }
    return n;
}

static const char FIX_USER[] =
    "Can you check disk usage?\n"
    "  indented line kept\n"
    "averyveryveryverylongwordthatcannotfitonanylineofthebubbleatall";

static const char FIX_THINK[] =
    "Plan: run df, then du on /var/log, then summarise.";

static const char FIX_AI[] =
    "## Disk **Usage**\n"
    "I checked the mounts first:\n"
    "[EXEC]df -h / && echo done[/EXEC]\n"
    "Here's what I found on **web-01** with `du`:\n"
    "\n"
    "- Root partition: 42% used\n"
    "1. first\n"
    "1. second, a longer item that wraps at the narrow width for sure\n"
    "\n"
    "> quoted *note*\n"
    "| Mount | Used |\n"
    "|---|--:|\n"
    "| / | 42% |\n"
    "\n"
    "```bash\n"
    "cd /var/log && ls -la | sort -rh | head -3\n"
    "```\n"
    "Gr\xC3\xBC\xC3\x9F" "e \xF0\x9F\x98\x80 done.";

static const char FIX_STATUS[] = "Stream cancelled by user.";

static LRESULT CALLBACK host_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static HFONT make_font(int pt, int dpi, int weight, const char *face)
{
    return CreateFontA(-MulDiv(pt, dpi, 72), 0, 0, 0, weight, 0, 0, 0,
                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

/* Strip spaces, tabs and line breaks. */
static void squeeze(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (; *in && n + 1 < cap; in++)
        if (*in != ' ' && *in != '\t' && *in != '\n' && *in != '\r')
            out[n++] = *in;
    out[n] = '\0';
}

/* The painted words of one part of an item, in paint order. */
static void painted_text(const ChatMsgItem *it, int part, const char *text,
                         char *out, size_t cap)
{
    size_t n = 0;
    out[0] = '\0';
    for (int i = 0; i < it->runs.n; i++) {
        const TextRun *r = &it->runs.v[i];
        if (r->part != part) continue;
        const char *s = r->synthetic[0] ? r->synthetic : text + r->start;
        size_t k = r->synthetic[0] ? strlen(r->synthetic)
                                   : (size_t)(r->end - r->start);
        if (n + k + 1 > cap) k = cap - 1 - n;
        memcpy(out + n, s, k);
        n += k;
        out[n] = '\0';
    }
}

static int check_item(const ChatMsgItem *it, int markdown)
{
    int bad = 0;
    if (it->measured_height <= 0 || it->type == CHAT_ITEM_COMMAND) return 0;
    if (it->painted_height != it->measured_height) {
        printf("  item %d: measured %d, painted %d\n", it->id,
               it->measured_height, it->painted_height);
        bad++;
    }
    if (it->runs.n == 0) {
        printf("  item %d recorded no runs\n", it->id);
        return bad + 1;
    }
    for (int i = 0; i < it->runs.n; i++) {
        const TextRun *r = &it->runs.v[i];
        for (int k = 1; k < r->n_cum; k++) {
            if (it->runs.cum[r->first_cum + k] < it->runs.cum[r->first_cum + k - 1] ||
                it->runs.cum_byte[r->first_cum + k] <= it->runs.cum_byte[r->first_cum + k - 1]) {
                printf("  item %d run %d: cum not monotonic\n", it->id, i);
                bad++;
                break;
            }
        }
        if (r->synthetic[0] || r->w <= 0) continue;
        int part = -1;
        int off = text_runs_hit(&it->runs, r->x + r->w / 2, r->y + r->h / 2,
                                &part);
        if (part != r->part || off < r->start || off > r->end) {
            printf("  item %d run %d [%d,%d): centre hit %d part %d\n",
                   it->id, i, r->start, r->end, off, part);
            bad++;
        }
    }
    /* painted words == copied text, whitespace aside */
    static char painted[8192], copied_sq[8192], painted_sq[8192];
    char *copied = NULL;
    int flags = (it->type == CHAT_ITEM_AI_TEXT)
        ? (CHAT_COPY_EXEC | (markdown ? CHAT_COPY_MD : 0)) : 0;
    chat_copy_visible(it->text, (int)it->text_len, 0, (int)it->text_len,
                      flags, &copied);
    painted_text(it, TR_PART_MAIN, it->text, painted, sizeof(painted));
    squeeze(painted, painted_sq, sizeof(painted_sq));
    squeeze(copied ? copied : "", copied_sq, sizeof(copied_sq));
    if (strcmp(painted_sq, copied_sq) != 0) {
        printf("  item %d: painted \"%s\"\n         copied  \"%s\"\n", it->id,
               painted_sq, copied_sq);
        bad++;
    }
    free(copied);
    if (it->type == CHAT_ITEM_AI_TEXT && it->u.ai.thinking_text) {
        painted_text(it, TR_PART_THINKING, it->u.ai.thinking_text, painted,
                     sizeof(painted));
        squeeze(painted, painted_sq, sizeof(painted_sq));
        squeeze(it->u.ai.thinking_text, copied_sq, sizeof(copied_sq));
        if (strcmp(painted_sq, copied_sq) != 0) {
            printf("  item %d Thinking: painted \"%s\"\n", it->id, painted_sq);
            bad++;
        }
    }
    return bad;
}

/* A hidden host window holding a chat list view over `list`, painted
 * once into a memory DC at `width` x CLV_TEST_H and `dpi`. */
#define CLV_TEST_H 3000
typedef struct {
    HWND host, lv;
    HFONT f, m, b, s;
} TestView;

static int view_open(TestView *tv, ChatMsgList *list, int width, int dpi,
                     int markdown)
{
    static int registered = 0;
    HINSTANCE inst = GetModuleHandleA(NULL);
    memset(tv, 0, sizeof(*tv));
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = host_proc;
        wc.hInstance = inst;
        wc.lpszClassName = "NsSelTestHost";
        RegisterClassA(&wc);
        chat_listview_register(inst);
        ui_theme_resolve(ui_theme_get(0), &g_win_tokens);
        registered = 1;
    }
    tv->host = CreateWindowA("NsSelTestHost", "", WS_POPUP, 0, 0, width,
                             CLV_TEST_H, NULL, NULL, inst, NULL);
    if (!tv->host) return 0;
    tv->lv = chat_listview_create(tv->host, 0, 0, width, CLV_TEST_H, list,
                                  ui_theme_get(0));
    if (!tv->lv) return 0;
    if (dpi != 96) {
        RECT r = { 0, 0, width, CLV_TEST_H };
        SendMessageA(tv->lv, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), (LPARAM)&r);
    }
    tv->f = make_font(10, dpi, FW_NORMAL, "Segoe UI");
    tv->m = make_font(10, dpi, FW_NORMAL, "Consolas");
    tv->b = make_font(10, dpi, FW_BOLD, "Segoe UI");
    tv->s = make_font(9, dpi, FW_NORMAL, "Segoe UI");
    chat_listview_set_fonts(tv->lv, tv->f, tv->m, tv->b, tv->s);
    chat_listview_set_render_markdown(tv->lv, markdown);
    chat_listview_invalidate(tv->lv);
    return 1;
}

/* Paint the list view into a memory DC (no WM_PAINT reaches a hidden
 * window); the paint records every item's runs. */
static void view_paint(TestView *tv, int width)
{
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, width, CLV_TEST_H);
    HGDIOBJ old = SelectObject(mem, bmp);
    SendMessageA(tv->lv, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void view_close(TestView *tv)
{
    if (tv->lv) DestroyWindow(tv->lv);
    if (tv->host) DestroyWindow(tv->host);
    if (tv->f) DeleteObject(tv->f);
    if (tv->m) DeleteObject(tv->m);
    if (tv->b) DeleteObject(tv->b);
    if (tv->s) DeleteObject(tv->s);
}

static void fill_fixture(ChatMsgList *list)
{
    chat_msg_list_init(list);
    chat_msg_append(list, CHAT_ITEM_USER, FIX_USER);
    ChatMsgItem *ai = chat_msg_append(list, CHAT_ITEM_AI_TEXT, FIX_AI);
    chat_msg_set_thinking(ai, FIX_THINK);
    ai->u.ai.thinking_collapsed = 0;
    ai->u.ai.thinking_complete = 1;
    chat_msg_append(list, CHAT_ITEM_STATUS, FIX_STATUS);
}

/* One configuration: list width, DPI, markdown on or off. */
static int run_config(int width, int dpi, int markdown)
{
    ChatMsgList list;
    fill_fixture(&list);
    TestView tv;
    int bad = 0;
    if (!view_open(&tv, &list, width, dpi, markdown)) {
        printf("  could not create the windows (error %lu)\n",
               (unsigned long)GetLastError());
        bad = 1;
    } else {
        view_paint(&tv, width);
        for (ChatMsgItem *it = list.head; it; it = it->next)
            bad += check_item(it, markdown);
    }
    view_close(&tv);
    chat_msg_list_clear(&list);
    if (bad) printf("  (width %d, dpi %d, markdown %d)\n", width, dpi, markdown);
    return bad;
}

int test_chat_select_win_runs_match_paint(void)
{
    TEST_BEGIN();
    static const int widths[2] = { 420, 760 };
    static const int dpis[2] = { 96, 144 };
    for (int w = 0; w < 2; w++)
        for (int d = 0; d < 2; d++) {
            int bad = run_config(widths[w], dpis[d], 1);
            ASSERT_EQ(bad, 0);
        }
    int plain_bad = run_config(420, 96, 0);
    ASSERT_EQ(plain_bad, 0);
    TEST_END();
}

/* The run of `it` (main part) whose text starts with `word`; *wx gets the
 * pixel width of the word itself (trailing blanks excluded). */
static const TextRun *find_word(const ChatMsgItem *it, const char *word,
                                int *wx)
{
    int n = (int)strlen(word);
    for (int i = 0; i < it->runs.n; i++) {
        const TextRun *r = &it->runs.v[i];
        if (r->part != TR_PART_MAIN || r->synthetic[0] || r->end - r->start < n)
            continue;
        if (memcmp(it->text + r->start, word, (size_t)n) != 0) continue;
        *wx = r->w;
        for (int k = 0; k < r->n_cum; k++)
            if (it->runs.cum_byte[r->first_cum + k] == n)
                *wx = it->runs.cum[r->first_cum + k];
        return r;
    }
    return NULL;
}

/* The selection's visible text, as Ctrl+C would copy it. */
static int sel_is(HWND lvh, const ChatMsgList *list, const char *want)
{
    ChatListView *lv = (ChatListView *)GetWindowLongPtrA(lvh, GWLP_USERDATA);
    char *out = NULL;
    if (lv && lv->sel_valid)
        chat_copy_selection(list, lv->sel_anchor, lv->sel_focus,
                            lv->render_markdown, &out);
    int ok = out && strcmp(out, want) == 0;
    if (!ok) printf("  selection \"%s\", want \"%s\"\n", out ? out : "(none)", want);
    free(out);
    return ok;
}

static LPARAM pt(int x, int y) { return MAKELPARAM(x, y); }

/* The mouse paths of section 5 through the real window procedure: a drag
 * across two words, a click that clears, a double-click on a word and on a
 * code token, a triple-click on a list line, Ctrl+A and Escape. */
int test_chat_select_win_mouse_and_keys(void)
{
    TEST_BEGIN();
    ChatMsgList list;
    fill_fixture(&list);
    TestView tv;
    int opened = view_open(&tv, &list, 760, 96, 1);
    ASSERT_TRUE(opened);
    if (opened) {
        view_paint(&tv, 760);
        const ChatMsgItem *ai = list.head->next;
        int w1 = 0, w2 = 0, w3 = 0, w4 = 0;
        const TextRun *a = find_word(ai, "mounts", &w1);
        const TextRun *b = find_word(ai, "first:", &w2);
        const TextRun *c = find_word(ai, "partition:", &w3);
        const TextRun *d = find_word(ai, "cd /var/log && ls", &w4);
        ASSERT_NOT_NULL(a); ASSERT_NOT_NULL(b); ASSERT_NOT_NULL(c);
        ASSERT_NOT_NULL(d);
        if (a && b && c && d) {
            int ay = a->y + a->h / 2, by = b->y + b->h / 2;
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(a->x + 1, ay));
            SendMessageA(tv.lv, WM_MOUSEMOVE, MK_LBUTTON, pt(b->x + w2 / 2, by));
            SendMessageA(tv.lv, WM_MOUSEMOVE, MK_LBUTTON, pt(b->x + w2 - 1, by));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(b->x + w2 - 1, by));
            ASSERT_TRUE(sel_is(tv.lv, &list, "mounts first:"));

            /* a click with no movement clears it */
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(a->x + 1, ay));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(a->x + 2, ay));
            ChatListView *lv = (ChatListView *)GetWindowLongPtrA(tv.lv, GWLP_USERDATA);
            ASSERT_TRUE(lv && !lv->sel_valid);

            /* double-click a word, then a third click on the same spot */
            int cy = c->y + c->h / 2;
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(c->x + 2, cy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(c->x + 2, cy));
            SendMessageA(tv.lv, WM_LBUTTONDBLCLK, MK_LBUTTON, pt(c->x + 2, cy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(c->x + 2, cy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "partition:"));
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(c->x + 2, cy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(c->x + 2, cy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "- Root partition: 42% used"));

            /* double-click inside a code line takes the token, not the line */
            int dy = d->y + d->h / 2;
            Sleep(GetDoubleClickTime() + 50);   /* no triple-click this time */
            SendMessageA(tv.lv, WM_LBUTTONDBLCLK, MK_LBUTTON, pt(d->x + w4 - 2, dy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(d->x + w4 - 2, dy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "ls"));

            /* Ctrl+A selects every item; Escape clears */
            BYTE keys[256], saved[256];
            GetKeyboardState(saved);
            memcpy(keys, saved, sizeof(keys));
            keys[VK_CONTROL] = 0x80;
            SetKeyboardState(keys);
            SendMessageA(tv.lv, WM_KEYDOWN, 'A', 0);
            SetKeyboardState(saved);
            char *all = NULL;
            ChatSelPos sa, sb;
            ASSERT_TRUE(chat_sel_all(&list, &sa, &sb));
            chat_copy_selection(&list, sa, sb, 1, &all);
            ASSERT_NOT_NULL(all);
            if (all) ASSERT_TRUE(sel_is(tv.lv, &list, all));
            free(all);
            SendMessageA(tv.lv, WM_KEYDOWN, VK_ESCAPE, 0);
            ASSERT_TRUE(lv && !lv->sel_valid);
        }
    }
    view_close(&tv);
    chat_msg_list_clear(&list);
    TEST_END();
}

#endif /* _WIN32 */
```
In `tests/win_runner.c`, before `/* test_local_pty.c */` add:

```c
/* test_chat_select_win.c */
int test_chat_select_win_runs_match_paint(void);
int test_chat_select_win_mouse_and_keys(void);

```
and after `    failed += test_md_render_win_measure_equals_paint_and_runs();` add:

```c
    failed += test_chat_select_win_runs_match_paint();
    failed += test_chat_select_win_mouse_and_keys();
```
In `Makefile` (CRLF) replace

```make
WIN_ONLY_TESTS = tests/test_icons.c tests/test_local_pty.c tests/win_runner.c \
                 tests/test_md_render_win.c
```
with

```make
WIN_ONLY_TESTS = tests/test_icons.c tests/test_local_pty.c tests/win_runner.c \
                 tests/test_md_render_win.c tests/test_chat_select_win.c
```
and in `WIN_TEST_SRCS` replace

```make
                   src/core/ns_scale.c src/core/ns_type.c src/core/cmd_policy.c \
                   tests/test_icons.c tests/test_local_pty.c \
                   tests/test_md_render_win.c tests/win_runner.c
```
with

```make
                   src/core/ns_scale.c src/core/ns_type.c src/core/cmd_policy.c \
                   src/ui/chat_listview.c src/ui/clipboard.c src/ui/ns_font.c \
                   src/core/chat_msg.c src/core/chat_copy.c src/core/chat_exec.c \
                   src/core/md_blocks.c src/core/ns_layout.c src/core/ns_hover.c \
                   src/core/stick_scroll.c src/core/ai_panel_layout.c \
                   src/core/ai_panel_states.c src/core/chat_activity.c \
                   tests/test_icons.c tests/test_local_pty.c \
                   tests/test_md_render_win.c tests/test_chat_select_win.c \
                   tests/win_runner.c
```
If the link reports another undefined reference, add the `src/core` file that defines it (`grep -ln "^[a-z].*\bNAME(" src/core/*.c`); never `window.c` or `ai_chat.c` -- define a window.c accessor in the test file as `ns_tokens()` is.

- [ ] **Step 12.2: Run them**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | tail -1
wineserver -k; xvfb-run -a make wintest 2>&1 | grep -E "chat_select|md_render_win|^  |^Tests Run" | grep -v "^\[RUN"
```
Expected: `make test` unchanged; `[PASS] test_md_render_win_measure_equals_paint_and_runs`, `[PASS] test_chat_select_win_runs_match_paint`, `[PASS] test_chat_select_win_mouse_and_keys`, `Tests Run: 12, Failed: 0`. A failure prints which item, run or configuration disagrees (`item N: measured A, painted B`, `run i [s,e): centre hit ...`, `painted "..." copied "..."`, `selection "...", want "..."`); fix the code it names, not the expectation. If Wine reports `no driver could be loaded`, a headless `wineserver` from an earlier run is still up: `wineserver -k` and run again. On Windows: `mingw32-make wintest`.

- [ ] **Step 12.3: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add tests/test_chat_select_win.c tests/win_runner.c Makefile
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
test(ui): wintest the chat list view behind character selection

A hidden list view over a fixture is painted through WM_PRINTCLIENT at
two widths and two DPIs, and with markdown turned off after the first
layout: every run's centre hits back into it, cum[] is monotonic, every
item paints exactly the height it measured, and the painted words equal
the copied text. A second case drives the window procedure: drag,
click to clear, double- and triple-click, a code token, Ctrl+A and
Escape.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 13: The `--ui-demo` word query and the harness helpers

The harness must find words without hardcoded coordinates (spec section 7): under `--ui-demo` the list answers `CLV_MSG_QUERY_WORD` with the rect where a word was painted, named by a global atom. The PowerShell side gains the helpers the cases of Task 14 use: find the list, ask for a word, drag with sent mouse messages, move focus, and post a chord to a given window.

**Toolchain:** this task changes `src/ui`, which `make test` never compiles. Compile-check it with the MinGW `-fsyntax-only` command in its run step (automaton has the toolchain); where no MinGW toolchain exists, rely on `make test` and a careful read of the diff. `make clean && make release` happens once, in Task 16, after the version bump; the maintainer builds the exe and runs `mingw32-make wintest` on Windows before the PR leaves draft.

**Files:**
- Modify: `src/ui/chat_listview.h` -- field after `pinned_id`; declarations before `chat_listview_set_selection()`
- Modify: `src/ui/chat_listview.c` -- `chat_listview_enable_query()` (before `chat_listview_set_selection()`), `clv_query_word()` (before the WndProc banner), `ChatListWndProc` (before `case WM_PRINTCLIENT:`)
- Modify: `src/ui/ai_chat.c` -- `ai_chat_apply_demo_extras()` (~5663)
- Modify: `tests/integration/NutshellIT.psm1` -- after `[NutshellNative]::SetProcessDPIAware() | Out-Null` (~260), `Send-NutshellChord` (~625-673), `Export-ModuleMember` (~1404)
- Test: `tests/test_chat_select_win.c` (append), `tests/win_runner.c`

**Interfaces:**
- Consumes: `clv_part_text()` (Task 9), `ChatMsgItem.runs` (Task 4, filled by Task 8), the wintest helpers `view_open/view_paint/view_close/fill_fixture/find_word` (Task 12).
- Produces:
  - `#define CLV_MSG_QUERY_WORD (WM_APP + 0x51)` -- wParam `LOWORD` = item position, `HIWORD` = 1 for the Thinking body; lParam = global atom; returns `x | y << 16 | w << 32 | h << 48` (16 bits each) or 0.
  - `void chat_listview_enable_query(HWND hwnd);`, field `int query_enabled;`
  - PowerShell: `Get-NutshellChatList -Session`, `Get-NutshellChatWordRect -Session -Word -Item [-Thinking]` -> `@{X;Y;W;H}` or `$null`, `Send-NutshellChatDrag -Session -FromX -FromY -ToX -ToY [-Steps 4] [-HoldMoves 0]`, `Set-NutshellFocus -Session -Hwnd`, and `Send-NutshellChord ... [-Hwnd]` (target window; default the main window as before).

- [ ] **Step 13.1: Write the failing test**

Append to `tests/test_chat_select_win.c`, before its final `#endif /* _WIN32 */`:

```c
/* CLV_MSG_QUERY_WORD, the integration harness's way to find a word: off
 * until enabled, then the rect of the run that begins with the word, the
 * width cut at the word's end; the Thinking body through HIWORD = 1. */
int test_chat_select_win_query_word(void)
{
    TEST_BEGIN();
    ChatMsgList list;
    fill_fixture(&list);
    TestView tv;
    int opened = view_open(&tv, &list, 760, 96, 1);
    ASSERT_TRUE(opened);
    if (opened) {
        view_paint(&tv, 760);
        ATOM atom = GlobalAddAtomA("mounts");
        LRESULT off = SendMessageA(tv.lv, CLV_MSG_QUERY_WORD, 1, (LPARAM)atom);
        ASSERT_EQ(off, 0);
        chat_listview_enable_query(tv.lv);
        LRESULT r = SendMessageA(tv.lv, CLV_MSG_QUERY_WORD, 1, (LPARAM)atom);
        int wx = 0;
        const TextRun *run = find_word(list.head->next, "mounts", &wx);
        ASSERT_NOT_NULL(run);
        if (run) {
            ULONG_PTR v = (ULONG_PTR)r;
            ASSERT_EQ((int)(v & 0xFFFF), run->x);
            ASSERT_EQ((int)((v >> 16) & 0xFFFF), run->y);
            ASSERT_EQ((int)((v >> 32) & 0xFFFF), wx);
            ASSERT_EQ((int)((v >> 48) & 0xFFFF), run->h);
            ASSERT_TRUE(wx < run->w);       /* "mounts " less its blank */
        }
        GlobalDeleteAtom(atom);
        ATOM think = GlobalAddAtomA("Plan:");
        LRESULT rt = SendMessageA(tv.lv, CLV_MSG_QUERY_WORD, MAKEWPARAM(1, 1),
                                  (LPARAM)think);
        ASSERT_TRUE(rt != 0);
        GlobalDeleteAtom(think);
        ATOM none = GlobalAddAtomA("zzzz");
        LRESULT rn = SendMessageA(tv.lv, CLV_MSG_QUERY_WORD, 1, (LPARAM)none);
        ASSERT_EQ(rn, 0);
        GlobalDeleteAtom(none);
    }
    view_close(&tv);
    chat_msg_list_clear(&list);
    TEST_END();
}
```
In `tests/win_runner.c` add `int test_chat_select_win_query_word(void);` after the declaration of `test_chat_select_win_mouse_and_keys`, and `    failed += test_chat_select_win_query_word();` after its call.

- [ ] **Step 13.2: Run it and see it fail**

```bash
cd /home/thomas/nutshell-wt/text-selection && make wintest 2>&1 | grep -m2 -E "CLV_MSG_QUERY_WORD|chat_listview_enable_query"
```
Expected: `'CLV_MSG_QUERY_WORD' undeclared` and `implicit declaration of function 'chat_listview_enable_query'`.

- [ ] **Step 13.3: Implement the query**

In `src/ui/chat_listview.h`:

Replace

```c
    /* Id of the user item whose copy on_paint() pinned at the top of the
     * list this frame (its runs are in pinned_runs), 0 when none. */
    int pinned_id;
```
with

```c
    /* Id of the user item whose copy on_paint() pinned at the top of the
     * list this frame (its runs are in pinned_runs), 0 when none. */
    int pinned_id;

    /* 1 under --ui-demo: answer CLV_MSG_QUERY_WORD (the integration
     * harness asks where a word was painted). */
    int query_enabled;
```
Replace

```c
/* Select from `a` to `b` (either order) and repaint -- the --ui-demo=chat
```
with

```c
/* --ui-demo only (see chat_listview_enable_query): where was a word
 * painted? wParam: LOWORD = the item's position in the list (0 = first),
 * HIWORD = 1 for its Thinking body, 0 for its text; lParam: a global atom
 * (GlobalAddAtom) naming the word, ASCII. Returns the rect, in list-view
 * client pixels, from the left edge of the first painted run that begins
 * with the word to the end of the word itself, packed as
 * x | y << 16 | w << 32 | h << 48 (16 bits each), or 0 when no painted run
 * begins with it, the rect lies above or left of the client area, or the
 * query is not enabled. Runs are recorded for the whole of every item a
 * paint touched, so a word below the visible area still answers (with
 * y >= the client height). */
#define CLV_MSG_QUERY_WORD (WM_APP + 0x51)

/* Answer CLV_MSG_QUERY_WORD from now on. Called by the --ui-demo setup only. */
void chat_listview_enable_query(HWND hwnd);

/* Select from `a` to `b` (either order) and repaint -- the --ui-demo=chat
```
In `src/ui/chat_listview.c`, before `void chat_listview_set_selection(HWND hwnd, ChatSelPos a, ChatSelPos b)` add:

```c
void chat_listview_enable_query(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (lv) lv->query_enabled = 1;
}

```
Insert immediately before the three-line banner whose middle line is ` *  WndProc`:

```c
/* CLV_MSG_QUERY_WORD (see chat_listview.h). */
static LRESULT clv_query_word(ChatListView *lv, WPARAM wParam, LPARAM lParam)
{
    char word[128];
    UINT n = GlobalGetAtomNameA((ATOM)lParam, word, (int)sizeof(word));
    if (n == 0 || !lv->msg_list) return 0;
    int ordinal = (int)LOWORD(wParam);
    int part = HIWORD(wParam) ? TR_PART_THINKING : TR_PART_MAIN;
    ChatMsgItem *it = lv->msg_list->head;
    for (int i = 0; it && i < ordinal; i++) it = it->next;
    if (!it) return 0;
    int tlen = 0;
    const char *text = clv_part_text(it, part, &tlen);
    for (int i = 0; i < it->runs.n; i++) {
        const TextRun *r = &it->runs.v[i];
        if (r->part != part || r->synthetic[0] || r->end > tlen ||
            r->end - r->start < (int)n)
            continue;
        if (memcmp(text + r->start, word, (size_t)n) != 0) continue;
        int w = r->w;
        for (int k = 0; k < r->n_cum; k++)
            if (it->runs.cum_byte[r->first_cum + k] == (int)n)
                w = it->runs.cum[r->first_cum + k];
        if (r->x < 0 || r->y < 0 || r->x > 0xFFFF || r->y > 0xFFFF) return 0;
        ULONG_PTR packed = (ULONG_PTR)(unsigned)(r->x & 0xFFFF)
                         | ((ULONG_PTR)(unsigned)(r->y & 0xFFFF) << 16)
                         | ((ULONG_PTR)(unsigned)(w & 0xFFFF) << 32)
                         | ((ULONG_PTR)(unsigned)(r->h & 0xFFFF) << 48);
        return (LRESULT)packed;
    }
    return 0;
}
```
In `ChatListWndProc`, before `    case WM_PRINTCLIENT:` add:

```c
    case CLV_MSG_QUERY_WORD:
        return lv->query_enabled ? clv_query_word(lv, wParam, lParam) : 0;

```
In `src/ui/ai_chat.c`, `ai_chat_apply_demo_extras()`:

Replace

```c
    relayout(d);
    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
```
with

```c
    relayout(d);
    if (d->hChatList) {
        /* The integration harness asks the list where words were painted
         * (CLV_MSG_QUERY_WORD) -- under --ui-demo only. */
        chat_listview_enable_query(d->hChatList);
        chat_listview_invalidate(d->hChatList);
```
- [ ] **Step 13.4: Run the tests**

```bash
cd /home/thomas/nutshell-wt/text-selection
make test 2>&1 | tail -1
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/ui/chat_listview.c src/ui/ai_chat.c
wineserver -k; xvfb-run -a make wintest 2>&1 | grep -E "chat_select|^Tests Run" | grep -v "^\[RUN"
```
Expected: `make test` unchanged; the three `test_chat_select_win_*` pass; `Tests Run: 13, Failed: 0`.

- [ ] **Step 13.5: The harness helpers**

In `tests/integration/NutshellIT.psm1`, after the line `[NutshellNative]::SetProcessDPIAware() | Out-Null` add:

```powershell
# Character selection in the AI panel (spec docs/superpowers/specs/
# 2026-10-18-ai-panel-text-selection-design.md section 7): global atoms name
# the word CLV_MSG_QUERY_WORD looks for, and SetFocus moves keyboard focus
# once this thread's input is attached to the app's UI thread.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class NutshellSelNative {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern ushort GlobalAddAtom(string name);
    [DllImport("kernel32.dll")] public static extern ushort GlobalDeleteAtom(ushort atom);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
}
"@

$script:CLV_MSG_QUERY_WORD = 0x8051   # WM_APP + 0x51, src/ui/chat_listview.h
$script:WM_MOUSEMOVE       = 0x0200

function Get-NutshellChatList {
    <# HWND of the AI panel's message list (class NutshellChatList), or Zero. #>
    param([Parameter(Mandatory)] $Session)
    $p = Get-NutshellAiPanel -Session $Session
    if ($p -eq [IntPtr]::Zero) { return [IntPtr]::Zero }
    return [NutshellNative]::FindWindowEx($p, [IntPtr]::Zero, "NutshellChatList", [IntPtr]::Zero)
}

function Get-NutshellChatWordRect {
    <# Where the chat list painted a word -- --ui-demo only. Asks the list with
       CLV_MSG_QUERY_WORD for the first painted run of item -Item (0 = the
       first item in the list; -Thinking for its Thinking body) that begins
       with -Word (ASCII). Returns @{X;Y;W;H} in the list's client pixels, W
       ending at the word's last character (its trailing blank excluded), or
       $null when no painted run begins with it. A word below the visible
       area still answers, with Y at or past the list's client height: a
       paint records the whole of every item it touches. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Word,
          [Parameter(Mandatory)] [int] $Item, [switch] $Thinking)
    $list = Get-NutshellChatList -Session $Session
    if ($list -eq [IntPtr]::Zero) { throw "AI panel message list not found" }
    $atom = [NutshellSelNative]::GlobalAddAtom($Word)
    if ($atom -eq 0) { throw "GlobalAddAtom failed for '$Word'" }
    try {
        $hi = if ($Thinking) { 1 } else { 0 }
        $wp = ($hi -shl 16) -bor ($Item -band 0xFFFF)
        $r = [NutshellNative]::SendMsg($list, $script:CLV_MSG_QUERY_WORD, [IntPtr]$wp, [IntPtr][int]$atom)
    } finally {
        [NutshellSelNative]::GlobalDeleteAtom($atom) | Out-Null
    }
    $v = $r.ToInt64()
    if ($v -eq 0) { return $null }
    return @{
        X = [int]($v -band 0xFFFF)
        Y = [int](($v -shr 16) -band 0xFFFF)
        W = [int](($v -shr 32) -band 0xFFFF)
        H = [int](($v -shr 48) -band 0xFFFF)
    }
}

function ConvertTo-NutshellPointLParam {
    <# MAKELPARAM(x, y) for a mouse message: two 16-bit halves, so a point
       above or left of the window (negative) survives GET_X/Y_LPARAM. #>
    param([int] $X, [int] $Y)
    return [IntPtr]((($Y -band 0xFFFF) -shl 16) -bor ($X -band 0xFFFF))
}

function Send-NutshellChatDrag {
    <# Drag in the chat list with sent mouse messages only -- no cursor, no
       foreground: a button-down at (FromX, FromY), -Steps moves with the
       button held to (ToX, ToY), -HoldMoves more moves at (ToX, ToY) (each
       past an edge auto-scrolls the list once), then the button-up there.
       Client pixels of the list (Get-NutshellChatWordRect gives them). Sent,
       not posted, so every message is handled -- repaint and hit-test
       included -- before the next one goes. Physical mouse movement over
       the window during the drag would be read as one more drag point, so
       run it on a locked or idle desktop, like every posted-input case. #>
    param([Parameter(Mandatory)] $Session, [int] $FromX, [int] $FromY,
          [int] $ToX, [int] $ToY, [int] $Steps = 4, [int] $HoldMoves = 0)
    $list = Get-NutshellChatList -Session $Session
    if ($list -eq [IntPtr]::Zero) { throw "AI panel message list not found" }
    $mk = [IntPtr]$script:MK_LBUTTON
    [NutshellNative]::SendMsg($list, $script:WM_LBUTTONDOWN, $mk,
        (ConvertTo-NutshellPointLParam -X $FromX -Y $FromY)) | Out-Null
    for ($i = 1; $i -le $Steps; $i++) {
        $x = $FromX + [int](($ToX - $FromX) * $i / $Steps)
        $y = $FromY + [int](($ToY - $FromY) * $i / $Steps)
        [NutshellNative]::SendMsg($list, $script:WM_MOUSEMOVE, $mk,
            (ConvertTo-NutshellPointLParam -X $x -Y $y)) | Out-Null
    }
    for ($i = 0; $i -lt $HoldMoves; $i++) {
        [NutshellNative]::SendMsg($list, $script:WM_MOUSEMOVE, $mk,
            (ConvertTo-NutshellPointLParam -X $ToX -Y $ToY)) | Out-Null
    }
    [NutshellNative]::SendMsg($list, $script:WM_LBUTTONUP, [IntPtr]::Zero,
        (ConvertTo-NutshellPointLParam -X $ToX -Y $ToY)) | Out-Null
    Start-Sleep -Milliseconds 200
}

function Set-NutshellFocus {
    <# Give keyboard focus to one of the app's windows with no click and no
       foreground: attach this thread's input to the app's UI thread (as
       Send-NutshellChord does), so SetFocus may name a window of that
       thread, then detach. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [IntPtr] $Hwnd)
    $ownerPid = [uint32]0
    $uiThread = [NutshellNative]::GetWindowThreadProcessId($Session.Main, [ref]$ownerPid)
    if ($uiThread -eq 0) { throw "could not find the UI thread of window $($Session.Main)" }
    $mine = [NutshellNative]::GetCurrentThreadId()
    if (-not [NutshellNative]::AttachThreadInput($mine, $uiThread, $true)) {
        throw "AttachThreadInput to Nutshell's UI thread ($uiThread) failed"
    }
    try {
        [NutshellSelNative]::SetFocus($Hwnd) | Out-Null
    } finally {
        [NutshellNative]::AttachThreadInput($mine, $uiThread, $false) | Out-Null
    }
    Start-Sleep -Milliseconds 200
}
```
In `Send-NutshellChord`, replace the parameter line

```powershell
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Key,
          [switch] $Ctrl, [switch] $Shift, [switch] $Alt, [int] $SettleMs = 500)
```
with

```powershell
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Key,
          [switch] $Ctrl, [switch] $Shift, [switch] $Alt, [int] $SettleMs = 500,
          [IntPtr] $Hwnd = ([IntPtr]::Zero))
    # -Hwnd: post the key to that window of the app (the AI panel's message
    # list, say) instead of the main window. Same UI thread, so the attached
    # keyboard state below holds for it too.
    $target = if ($Hwnd -ne [IntPtr]::Zero) { $Hwnd } else { $Session.Main }
```
and in its body replace

```powershell
            [NutshellNative]::PostMessage($Session.Main, $msgDown, [IntPtr]$vk, $down) | Out-Null
```
with

```powershell
            [NutshellNative]::PostMessage($target, $msgDown, [IntPtr]$vk, $down) | Out-Null
```
and

```powershell
            [NutshellNative]::PostMessage($Session.Main, $msgUp, [IntPtr]$vk, $up) | Out-Null
```
with

```powershell
            [NutshellNative]::PostMessage($target, $msgUp, [IntPtr]$vk, $up) | Out-Null
```
(the `WM_NULL` round trip stays on `$Session.Main`: same thread). Append the new functions to `Export-ModuleMember`: replace its last line

```powershell
    Get-NutshellRegionHash
```
with

```powershell
    Get-NutshellRegionHash, Get-NutshellChatList, Get-NutshellChatWordRect, Send-NutshellChatDrag, `
    Set-NutshellFocus
```
- [ ] **Step 13.6: Check the module parses**

Where PowerShell exists (the Windows box; `pwsh` on Linux if installed):

```bash
pwsh -NoProfile -Command "Import-Module ./tests/integration/NutshellIT.psm1 -Force; Get-Command Get-NutshellChatWordRect, Send-NutshellChatDrag, Set-NutshellFocus | Select-Object -ExpandProperty Name"
```
Expected: the three names. (On Windows PowerShell 5.1 use `powershell -NoProfile -Command ...`.) Without PowerShell, read the diff against the existing helpers' conventions; Task 14's run is the real check.

- [ ] **Step 13.7: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/ui/chat_listview.h src/ui/chat_listview.c src/ui/ai_chat.c tests/test_chat_select_win.c tests/win_runner.c tests/integration/NutshellIT.psm1
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
test(harness): ask the chat list where it painted a word

Under --ui-demo the list answers CLV_MSG_QUERY_WORD with the rect of
the first painted run that begins with a word, named by a global atom,
so the harness drives selection with no hardcoded coordinates. The
module gains Get-NutshellChatWordRect, Send-NutshellChatDrag (sent
mouse messages, no cursor), Set-NutshellFocus and a -Hwnd target for
Send-NutshellChord.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 14: Four gate-tier harness cases

Spec section 7's harness cases, run with `--ui-demo=chat` (no SSH host, no AI key): an exact single-line copy with nothing copied on release; a drag from the user bubble across the Thinking body and an `[EXEC]` command into the reply; a drag past the bottom edge that auto-scrolls to the reply's last line; and Edit > Copy with the list focused versus the terminal focused. Gate tier, like every case that needs no key; posted/sent input only, so they run on a locked or RDP-disconnected desktop. A physical mouse moved over the window during a drag would add a drag point: run them with nobody at the mouse, like the other posted-input cases.

**Files:**
- Create: `tests/integration/cases/95-ai-select.ps1`
- Modify: `tests/integration/README.md` -- Layout table (after the `cases\90-keys.ps1` row, ~46), Cases table (after `backspace_sends_bs_over_ssh`, ~216), helper table (after `Get-NutshellRegionHash`, ~255)

**Interfaces:**
- Consumes: `Get-NutshellChatList`, `Get-NutshellChatWordRect`, `Send-NutshellChatDrag`, `Set-NutshellFocus`, `Send-NutshellChord -Hwnd` (Task 13); `New-NutshellTestEnv`, `Start-Nutshell`, `Stop-Nutshell`, `Set-NutshellWindowSize`, `Send-NutshellCommand`, `Save-NutshellScreenshot`; the driver's `$ActiveTiers`, `$Only`, `$Artifacts`, `$results`, `Assert-True`; `CLV_MSG_QUERY_WORD` (Task 13); the demo reply and selection (Task 11); `IDM_EDIT_COPY` = 2010 (`src/ui/resource.h`).
- Produces: cases `ai_panel_select_copy`, `ai_panel_select_across_items`, `ai_panel_select_scrolls`, `ai_panel_edit_copy_follows_focus`.

- [ ] **Step 14.1: Write the cases**

Create `tests/integration/cases/95-ai-select.ps1`:

```powershell
# 95-ai-select.ps1 -- character selection in the AI Assist panel (spec
# docs/superpowers/specs/2026-10-18-ai-panel-text-selection-design.md
# section 7). Driven by --ui-demo=chat, so no SSH host or AI key is needed;
# the list says where it painted each word through CLV_MSG_QUERY_WORD
# (Get-NutshellChatWordRect), so no coordinate is hardcoded, and the drag is
# sent mouse messages (Send-NutshellChatDrag), so the cases run on a locked
# desktop like every other posted-input case. Dot-sourced by
# Run-Integration.ps1; depends on its $ActiveTiers/$Only/$Artifacts/$results/
# $HostName/$User/$KeyPath/$Exe and Assert-True.
#
# The --ui-demo=chat thread is two items: 0 = the user turn ("Can you check
# disk usage on this box and tell me what's eating space?"), 1 = the reply
# (src/core/ui_demo.c CHAT_ASSISTANT_MSG), its Thinking body expanded.

# The Thinking text of --ui-demo=chat (src/core/ui_demo.c CHAT_THINKING).
$script:DemoThinking = "The user wants a disk usage summary. I should run " +
    "df -h for the mount table and du on /var/log since that's usually the " +
    "biggest growth area on a box like this. I'll summarize with a table " +
    "and flag the largest files, then offer a concrete next step rather " +
    "than just dumping raw output."

# One --ui-demo case: start, size the window, find the list, run the body
# (param $s, $list; returns the detail string), record the result.
function Invoke-AiSelectCase {
    param([string] $Name, [int] $Height = 900, [scriptblock] $Body)
    if (-not (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains $Name))) { return }
    Write-Host ("[RUN ] " + $Name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=chat")
        Set-NutshellWindowSize -Session $session -Width 1400 -Height $Height | Out-Null
        Start-Sleep -Seconds 2
        $list = Get-NutshellChatList -Session $session
        if ($list -eq [IntPtr]::Zero) { throw "AI panel message list not found in --ui-demo=chat" }
        $detail = & $Body $session $list
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($session) { try { Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$Name-FAIL.png") | Out-Null } catch {} }
    } finally {
        if ($session) { Stop-Nutshell -Session $session }
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $Name) } else { Write-Host ("[FAIL] " + $Name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $Name; Passed = $ok; Detail = $detail })
}

# Select "mounts first:" in the reply by dragging from the left edge of
# "mounts" to the last character of "first:". Returns the two rects.
function Select-DemoMountsFirst {
    param($s)
    $a = Get-NutshellChatWordRect -Session $s -Word "mounts" -Item 1
    $b = Get-NutshellChatWordRect -Session $s -Word "first:" -Item 1
    Assert-True ($null -ne $a -and $null -ne $b) "'mounts first:' is not painted in the reply"
    Send-NutshellChatDrag -Session $s -FromX ($a.X + 1) -FromY ($a.Y + [int]($a.H / 2)) `
        -ToX ($b.X + $b.W - 1) -ToY ($b.Y + [int]($b.H / 2))
}

Invoke-AiSelectCase "ai_panel_select_copy" -Body {
    param($s, $list)
    Set-Clipboard -Value "SENTINEL_SELECT_1"
    Select-DemoMountsFirst $s
    $after = Get-Clipboard -Raw
    Assert-True ($after -ceq "SENTINEL_SELECT_1") "the button-up copied ('$after'): nothing may be copied on release"
    Send-NutshellChord -Session $s -Key C -Ctrl -Hwnd $list
    $got = Get-Clipboard -Raw
    Assert-True ($got -ceq "mounts first:") "clipboard holds '$got', want 'mounts first:'"
    "drag + Ctrl+C copied exactly 'mounts first:'; the release copied nothing"
}

Invoke-AiSelectCase "ai_panel_select_across_items" -Body {
    param($s, $list)
    $a = Get-NutshellChatWordRect -Session $s -Word "disk" -Item 0
    $b = Get-NutshellChatWordRect -Session $s -Word "found" -Item 1
    Assert-True ($null -ne $a) "'disk' is not painted in the user bubble"
    Assert-True ($null -ne $b) "'found' is not painted in the reply"
    Set-Clipboard -Value "SENTINEL_SELECT_2"
    Send-NutshellChatDrag -Session $s -FromX ($a.X + 1) -FromY ($a.Y + [int]($a.H / 2)) `
        -ToX ($b.X + $b.W - 1) -ToY ($b.Y + [int]($b.H / 2)) -Steps 8
    Send-NutshellChord -Session $s -Key C -Ctrl -Hwnd $list
    # Kept identical to tests/test_ui_demo.c
    # test_ui_demo_chat_drag_across_items_golden.
    $want = "disk usage on this box and tell me what's eating space?`n`n" +
            $script:DemoThinking + "`n`n" +
            "Disk Usage Summary`n`nI checked the mounts first:`ndf -h /`n`n" +
            "Here's what I found"
    $got = Get-Clipboard -Raw
    Assert-True ($got -ceq $want) "clipboard holds:`n$got`nwant:`n$want"
    "user bubble -> Thinking -> heading -> [EXEC] command -> reply joined exactly"
}

Invoke-AiSelectCase "ai_panel_select_scrolls" -Height 480 -Body {
    param($s, $list)
    $c = [NutshellNative]::ClientSize($list)
    $a = Get-NutshellChatWordRect -Session $s -Word "disk" -Item 0
    $w = Get-NutshellChatWordRect -Session $s -Word "Want" -Item 1
    Assert-True ($null -ne $a) "'disk' is not painted in the user bubble"
    Assert-True ($null -ne $w -and $w.Y -ge $c.B) `
        "precondition: the reply's last line must start below the visible list (list $($c.B) px tall)"
    Set-Clipboard -Value "SENTINEL_SELECT_3"
    Send-NutshellChatDrag -Session $s -FromX ($a.X + 1) -FromY ($a.Y + [int]($a.H / 2)) `
        -ToX ([int]($c.R / 2)) -ToY ($c.B + 40) -Steps 4 -HoldMoves 80
    Send-NutshellChord -Session $s -Key C -Ctrl -Hwnd $list
    $got = Get-Clipboard -Raw
    Assert-True ($got.StartsWith("disk usage on this box")) "clipboard does not start at the anchor: '$got'"
    Assert-True ($got.EndsWith("Want me to rotate and compress the old logs?")) `
        "the drag past the bottom edge did not reach the end of the reply: '$got'"
    $w2 = Get-NutshellChatWordRect -Session $s -Word "Want" -Item 1
    Assert-True ($null -eq $w2 -or $w2.Y -lt $w.Y) "the list did not scroll"
    "dragging past the bottom edge auto-scrolled and the selection reached the reply's last line"
}

Invoke-AiSelectCase "ai_panel_edit_copy_follows_focus" -Body {
    param($s, $list)
    Select-DemoMountsFirst $s       # the button-down also focuses the list
    Set-Clipboard -Value "SENTINEL_SELECT_4A"
    Send-NutshellCommand -Session $s -Id 2010                  # IDM_EDIT_COPY
    $got = Get-Clipboard -Raw
    Assert-True ($got -ceq "mounts first:") "Edit > Copy with the panel focused gave '$got'"
    Set-NutshellFocus -Session $s -Hwnd $s.Main                # the terminal
    Set-Clipboard -Value "SENTINEL_SELECT_4B"
    Send-NutshellCommand -Session $s -Id 2010
    $got2 = Get-Clipboard -Raw
    Assert-True ($got2 -ceq "SENTINEL_SELECT_4B") "Edit > Copy with the terminal focused copied the panel's selection ('$got2')"
    "Edit > Copy took the panel's selection only while its list had focus"
}
```
The expected string of `ai_panel_select_across_items` is the golden of `test_ui_demo_chat_drag_across_items_golden` (Task 11) and `$script:DemoThinking` is `CHAT_THINKING` in `src/core/ui_demo.c`; change all three together.

- [ ] **Step 14.2: Document them**

In `tests/integration/README.md`, after the Layout row for `cases\90-keys.ps1` add:

```markdown
| `cases\95-ai-select.ps1` | character selection in the AI panel under `--ui-demo=chat`: exact copy, across items, auto-scroll, Edit > Copy and focus |
```
after the Cases row for `backspace_sends_bs_over_ssh` add:

```markdown
| `ai_panel_select_copy` | `--ui-demo=chat`: drag over "mounts first:" in the reply (rects from `Get-NutshellChatWordRect`, no hardcoded coordinates), nothing copied on release, Ctrl+C posted to the list copies exactly `mounts first:`; no SSH host or AI key needed |
| `ai_panel_select_across_items` | `--ui-demo=chat`: drag from "disk" in the user bubble to "found" in the reply, across the open Thinking body and an `[EXEC]` command; the clipboard equals the golden of `tests/test_ui_demo.c` exactly |
| `ai_panel_select_scrolls` | `--ui-demo=chat` in a short window: the reply's last line starts below the list; a drag held past the bottom edge auto-scrolls and the copy runs from "disk" to the reply's last line |
| `ai_panel_edit_copy_follows_focus` | `--ui-demo=chat`: Edit > Copy copies the panel selection while the list has focus, and leaves the clipboard alone once focus is on the terminal |
```
and after the helper row for `Get-NutshellRegionHash -Path -Region` add:

```markdown
| `Get-NutshellChatList -Session` / `Get-NutshellChatWordRect -Session -Word -Item [-Thinking]` | the AI panel's message list / where it painted a word (`CLV_MSG_QUERY_WORD`, `--ui-demo` only): `@{X;Y;W;H}` in the list's client pixels, W ending at the word's last character |
| `Send-NutshellChatDrag -Session -FromX -FromY -ToX -ToY [-Steps] [-HoldMoves]` | a drag in the message list with sent `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` (no cursor, no foreground); `-HoldMoves` repeats the last point, each past an edge auto-scrolling once |
| `Set-NutshellFocus -Session -Hwnd` | keyboard focus to one of the app's windows via `AttachThreadInput` + `SetFocus`; `Send-NutshellChord -Hwnd` posts a chord to a window other than the main one |
```
- [ ] **Step 14.3: Run them (Windows, with the exe of Task 16, or a local build of this branch)**

```powershell
.\tests\integration\Run-Integration.ps1 -HostName tompi -User thomas -KeyPath $HOME\.ssh\thomas `
    -Only ai_panel_select_copy,ai_panel_select_across_items,ai_panel_select_scrolls,ai_panel_edit_copy_follows_focus
```
Expected: four `[PASS]` lines. A failure saves `artifacts\<case>-FAIL.png` and prints what the clipboard held. These cases cannot run on Linux; until the maintainer runs them, `make test` (unchanged) is this task's check, and the PR records the run.

- [ ] **Step 14.4: Commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add tests/integration/cases/95-ai-select.ps1 tests/integration/README.md
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
test(harness): four gate cases for selection in the AI panel

Under --ui-demo=chat, with words found through the list rather than
coordinates: an exact copy of a two-word drag with nothing copied on
release, a drag from the user bubble across the Thinking body and a
command into the reply, a drag past the bottom edge that scrolls to
the last line, and Edit > Copy following keyboard focus.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 15: Documentation

**Files:**
- Modify: `README.md` -- the AI Assist section (after "#### Sending Messages", ~238-245), the source tree (~470-523)
- Modify: `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` -- the Done list (before "Open, in priority order:", ~214), the Open list (after its first item, ~223)

**Interfaces:** none.

- [ ] **Step 15.1: README -- how to select and copy**

In `README.md`, insert after the "#### Sending Messages" list (after the line `- Click **Send** (or press Enter) to submit; while a reply is streaming the button becomes **Stop**`):

```markdown

#### Selecting and Copying

- **Click and drag** anywhere in the thread to select characters -- your messages, replies, the open Thinking text, commands, status lines -- across messages if you like; dragging past the top or bottom edge scrolls
- **Double-click** selects a word (in a code block, the command token under the pointer); **triple-click** selects the whole line
- **Ctrl+C**, **Edit > Copy** or right-click **Copy** puts the selection on the clipboard as it reads on screen: no markdown markers, list items with their bullet or number when the selection includes it, table cells separated by tabs, commands without their `[EXEC]` markers. Nothing is copied just by letting go of the mouse
- **Ctrl+A** or right-click **Select all** selects the whole conversation; **Esc** clears the selection
- The copy icons on code blocks and command rows still copy their raw text in one click
```
In the source tree, after the `chat_approval.c/.h` line add

```text
│   │   ├── chat_copy.c/.h             #   Visible text of an AI-panel selection (what Ctrl+C copies)
│   │   ├── chat_exec.c/.h             #   [EXEC] segment split of an AI reply, shared by painter and copier
```
after the `log_format.c/.h` line add

```text
│   │   ├── markdown.h                 #   Header-only markdown line classifier and inline parser
```
after the `term_extract.c/.h` line add

```text
│   │   ├── text_lines.c/.h            #   LF <-> CR LF for the message box
│   │   ├── text_runs.c/.h             #   Painted word geometry and the hit-test behind chat selection
```
and replace

```text
│       ├── md_render.c/.h, markdown.h #   Markdown rendering for AI responses
```
with

```text
│       ├── md_render.c/.h             #   Markdown and plain-text layout for the AI panel (records runs, draws the selection)
```
(If `text_lines.c/.h` is already listed, keep the existing line and add only `text_runs.c/.h`.)

- [ ] **Step 15.2: The notes document**

In `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`, insert before the line `Open, in priority order:`:

```markdown
- Character selection in the AI Assist panel, v1.2.37
  (`2026-10-18-ai-panel-text-selection-design.md`, plan
  `docs/superpowers/plans/2026-10-18-ai-panel-text-selection.md`): drag,
  double- and triple-click select characters across messages; Ctrl+C, Edit >
  Copy and the menu copy the visible text; every item is laid out by
  md_render, which records runs and draws the highlight. Two spec rules
  changed on the prototype's evidence: a list marker copies only when the
  range covers its bytes, and the user bubble's highlight uses its text
  colour (the focus tint is invisible on the accent bubble).

```
and after the first Open item (the one that ends `two smaller follow-ups: the classifier's reason string as a "why this level" line in the popup, and a Unicode input box.`) add:

```markdown
- [ ] AI panel selection follow-ups (spec `2026-10-18-ai-panel-text-selection-design.md`
      section 8 and the plan's notes): an I-beam cursor over text; Shift+arrow keyboard
      selection; grapheme-safe boundaries (a combining mark or an emoji sequence can
      split); dragging past the Thinking box's edge does not scroll the box; the message
      box is still an ANSI EDIT, so text outside the code page cannot be typed (the
      "Unicode input box" item above); the highlight leaves 2-6 px gaps between lines
      (line spacing), which the four-theme review may want closed.
```
- [ ] **Step 15.3: Check and commit**

```bash
cd /home/thomas/nutshell-wt/text-selection
grep -n "Selecting and Copying\|text_runs.c/.h\|chat_copy.c/.h" README.md
make test 2>&1 | tail -1
```
Expected: the three README lines found; `make test` unchanged.

```bash
cd /home/thomas/nutshell-wt/text-selection
git add README.md docs/superpowers/specs/2026-09-06-ui-redesign-notes.md
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
docs: selecting and copying in the AI panel

README gains the selection and copy keys for the AI panel and the new
core files; the notes record the feature, the two spec rules the
prototype changed, and the follow-ups.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
```

---

### Task 16: Version bump, build, commit the exe

The one build of the branch (CLAUDE.md: bump before every build; `Version bump` gate: `APP_VERSION`, `APP_VERSION_BINARY`, README and the committed exe's FileVersion agree and exceed `main`'s).

**Files:**
- Modify: `src/ui/resource.h:4-5`, `README.md:5`
- Rebuild: `build/win/nutshell.exe`

**Interfaces:** none.

- [ ] **Step 16.1: Bump**

Check `main`'s version first (`git fetch origin main && git show origin/main:src/ui/resource.h | sed -n 4,5p`); bump past it if it moved. From 1.2.36, in `src/ui/resource.h` replace

```c
#define APP_VERSION        "1.2.36"
#define APP_VERSION_BINARY  1,2,36,0
```
with

```c
#define APP_VERSION        "1.2.37"
#define APP_VERSION_BINARY  1,2,37,0
```
and in `README.md` replace `**Version**: v1.2.36 \` with `**Version**: v1.2.37 \` (the line ends in a backslash; keep it).

- [ ] **Step 16.2: Build and test**

```bash
cd /home/thomas/nutshell-wt/text-selection
make clean && make release
make test 2>&1 | tail -1
wineserver -k; xvfb-run -a make wintest 2>&1 | grep -E "^\[FAIL|^Tests Run"
```
Expected: the release build links and UPX-packs `build/win/nutshell.exe`; `Tests Run: 3007, Failed: 0`; `make wintest` `Tests Run: 13, Failed: 0`; on the Windows box: `mingw32-make clean && mingw32-make release`, `mingw32-make test`, `mingw32-make wintest`.

- [ ] **Step 16.3: Look at it**

Run `build\win\nutshell.exe --ui-demo=chat --theme "<theme>"` for the four themes (or let `ui_gallery` capture them) and check: the bubble from "disk" on, the open Thinking body, the heading, `df -h /`, the list, the table and the code block are highlighted, text stays readable on every surface, and a drag, a double-click and Ctrl+C behave as README says. The maintainer reviews these screenshots before the PR leaves draft (spec section 6).

- [ ] **Step 16.4: Commit and push**

```bash
cd /home/thomas/nutshell-wt/text-selection
git add src/ui/resource.h README.md build/win/nutshell.exe
git status --short
git -c user.name=umbongo commit -F - <<'EOF'
build: character selection in the AI panel (v1.2.37)

Bump to 1.2.37 and rebuild the exe from this branch: drag, double- and
triple-click selection in the AI Assist panel, Ctrl+C, Edit > Copy and
the menu copying the visible text.

Models: claude-opus-5-5 (plan); claude-sonnet-5 (implementation)

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
git fetch origin main
.github/scripts/check-version.sh "$(git merge-base origin/main HEAD)"
git push
```
Expected: `check-version.sh` reports the version raised, README current and the exe's FileVersion equal to `APP_VERSION`, and exits 0 -- the check the `Version bump` gate runs. Then open the pull request as a draft (`gh pr create --draft`), record in its body the `make test` count, the `make wintest` run and the four harness cases' results, and stop: the maintainer reviews the four-theme screenshots, runs `mingw32-make wintest` and the four cases on Windows, and takes it out of draft. A cloud session stops at the pushed branch.

---

## Self-Review

**Spec coverage** (sections 2-7 -> tasks):

| Requirement | Task |
|---|---|
| s2 `markdown.h` to core; `chat_exec_split()` shared by painter and copier | 1, 2 |
| s2 `chat_copy_visible()`: segments with state reset, every line kind, markers never copied, list numbering, table cells via `md_table_split_row`, rule and empty line keep their break, `markdown == 0` passes bytes, cut characters widened | 5 (tests per kind and the golden) |
| s2 join across items with `\n\n`, heap buffer with no bound, `\n` line ends, `CF_UNICODETEXT`, `chat_strip_exec_markers` removed | 5, 9 |
| s2 user bubble text UTF-8 at the source, CR LF to LF | 10 |
| s3 `TextRun`/`TextRuns`, one `GetTextExtentExPointW` per run, UTF-16 to UTF-8 boundaries, surrogate pairs once | 3, 7 |
| s3 recorded by paint with base offsets at every level; one native test per level with a fake painter | 6, 7, 8 |
| s3 code lines on `TextOutW` (`ExtTextOutW` clipped), one run per line, no prefix eating `&&`; double-click takes a token | 3, 7, 9 |
| s3 every item type on the layouter in `MD_PLAIN` with the two plain-only rules; twins at the same width; Thinking measured at its paint width | 7, 8 |
| s3 Thinking runs only inside the clip; layout cached per length/width/collapsed | 8 |
| s3 runs on `ChatMsgItem`, reset at every paint; `UpdateWindow` before a hit after scroll/wheel; button-up re-hits | 4, 8, 9 |
| s3 pinned bubble records and resolves its own runs | 8, 9 |
| s4 every hit rule (line by `y`, above/below, left/right/between, nearest boundary, synthetic, Thinking part) | 3 |
| s5 state by id/part/offset, cleared when an id goes; button-down/drag/up, 4 px threshold, no copy on release; double-click with nothing over glyphs and no forwarding; triple-click before the normal path; Ctrl+C/Ctrl+A/Escape; context menu; cards never an endpoint; streaming clamp | 5, 9 |
| s5 Edit > Copy via `ai_chat_copy_selection()` with the focus check, before `do_copy()`'s no-session return | 10 |
| s6 highlight before each run's text, after its background, over the colour it sits on; partial runs from `cum[]`; full lines to the right edge; no relayout | 7, 8 |
| s6 `--ui-demo=chat` selection across bubble, list, code, table, `[EXEC]`, Thinking; four-theme review; gallery | 11, 16 |
| s7 native `test_chat_copy.c`, `test_text_runs.c`, base offsets, painted-equals-copied | 3, 5, 6 |
| s7 wintest: md_render/md_table/ns_draw/theme in `WIN_TEST_SRCS`, hidden list view, two widths, two DPIs, centre hits, measured == painted, `cum[]` monotonic | 7, 12 |
| s7 harness: `--ui-demo` query message, the three named cases plus Edit > Copy | 13, 14 |

Gaps found while mapping, now tasks or steps: no way into a hidden window's paint (`WM_PRINTCLIENT`, Task 8); a markdown toggle never remeasured (Task 8); hit-tests computed the Thinking box one `side_pad` wider than paint (Task 8); `Send-NutshellChord` can only post to the main window, whose Ctrl+C sends SIGINT (`-Hwnd`, Task 13); no posted-input way to move focus for the Edit > Copy case (`Set-NutshellFocus`, Task 13); the five Review Focus items.

**Spec rules this plan changes** (each on the prototype's evidence, each named in the task that does it): the list marker copies when the range covers the marker's bytes, not any byte of the line (Task 5); whitespace the painter drops at the start of a line's content is not copied (Task 5); the table column count is the painter's, not `MD_TABLE_MAX_COLS` alone (Task 5); fence lines and the alignment row drop their line break (Task 5); `chat_copy_visible()`'s fifth argument is a flags word so a markdown-off reply still splits at `[EXEC]` (Task 5); `TextRuns` carries `cum_byte[]` and `TextRun` a `flags` word (Task 3); the bubble's highlight hue is its text colour (Task 8); the input box stays an ANSI control, read through `GetWindowTextW` (Task 10).

**Placeholder scan:** no "TBD", "add error handling", "similar to Task N" or step without its code. The one stated default is the implementer's model id in the `Models:`/`Co-Authored-By:` lines (Global Constraints says to put the real one there).

**Type and signature consistency:** `chat_exec_split(const char *, int, int *, ChatSeg *)` (Task 2) is called the same way in Tasks 5, 6, 8; `text_runs_hit(const TextRuns *, int, int, int *)`, `text_runs_run_at`, `text_runs_word`, `text_line_at`, `text_runs_span_px`, `text_runs_cum_from_utf16`, `text_runs_utf8_prefix_for_utf16`, `text_next_line` (Task 3) match their uses in Tasks 6, 7, 9, 12, 13; `ChatSelPos {msg_id, part, off}` and `chat_sel_cmp/chat_sel_part_range/chat_copy_selection/chat_sel_all` (Task 5) match Tasks 8, 9, 11, 12; `md_render_ex(..., int mode, int paint, COLORREF plain_clr, MdBlockOut *, MdRunCtx *)` (Task 7) matches Tasks 8 and 12; `paint_user_item(..., int pinned)` changes its forward declaration and both calls in Task 8; `draw_ai_text_with_exec` goes from `(lv, hdc, const char *text, RECT *rc, int item_id)` (Task 2) to `(lv, hdc, ChatMsgItem *item, const RECT *rc)` (Task 8) with its one call; `chat_listview_set_selection` (Task 11) precedes `chat_listview_enable_query` (Task 13), which is inserted before it; `TR_PART_THINKING 0 < TR_PART_MAIN 1` gives reading order everywhere it is compared. Test counts: 2,957 + 8 (T2) + 15 (T3) + 2 (T4) + 20 (T5) + 4 (T6) + 2 (T8) - 4 (T9) + 1 (T10) + 2 (T11) = 3,007; wintest 9 + 1 (T7) + 2 (T12) + 1 (T13) = 13.

**Review Focus coverage:** item 1 -> Task 5 (`test_chat_copy_bullet_marker_follows_its_bytes`, the ordered-list test) and Task 12 (double/triple-click on a list item); item 2 -> Task 8 (`test_sel_highlight_visible_on_every_surface`); item 3 -> Task 8 fix, Task 12 (`run_config(420, 96, 0)` after the toggle); item 4 -> Task 5 (`test_chat_copy_leading_whitespace_is_not_copied`) and Task 6 (fixture 4); item 5 -> Task 5 (`test_chat_copy_table_columns_follow_the_alignment_row`).
