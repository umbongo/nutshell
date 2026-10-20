# Terminal Selection and Copy, PuTTY Style: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make terminal selection behave like PuTTY: a buffer-absolute, per-session selection that auto-scrolls past an edge, copies on release with no size limit, supports double-click words and triple-click lines, stays until something invalidates it, and leaves Ctrl+C as the interrupt. Confirm Paste also gains Enter and Tab.

**Architecture:** The terminal (`src/term`) gains a view mapping (`term_view_to_logical`), a scroll clamp (`term_max_scroll_offset`), absolute line numbers (`lines_recycled`), an `epoch` and per-row write stamps (`TermRow.gen` / `write_gen`). Stamping happens in the parser's `get_screen_row()` and in buffer.c's scroll functions. All selection logic is pure and lives in `src/core/selection.c`, tested natively. `src/ui/window.c` keeps only the message handlers, the mouse capture and timer 1's auto-scroll and staleness tick. `src/ui/renderer.c` uses the same view mapping and asks the selection which cells are highlighted.

**Tech Stack:** C11. The Windows build uses MinGW (`x86_64-w64-mingw32-gcc`, `-Werror -Wconversion -Wshadow -Wpedantic`) and Win32. Native tests use plain gcc and the repo's `tests/test_framework.h`. The integration harness is PowerShell 5.1 (`tests/integration`).

**Spec:** `docs/superpowers/specs/2026-10-20-putty-selection-design.md` (APPROVED 2026-10-20). Section 6's two defaults are approved: Ctrl+C always interrupts, and the scrollback setting is not wired.

## Global Constraints

These are copied from the spec and CLAUDE.md. Every task must meet them.

- "lines end in CRLF; the last line has no ending."
- "trailing empty rows are dropped (CLAUDE.md, "Terminal Buffer")"
- "trailing blanks are trimmed per logical line after joining, never per row"
- "A soft-wrapped line is one line"
- "Cells go through `cell_text_codepoint()`."
- "**Ctrl+C always sends the interrupt** (section 6.1)." Ctrl+C never consults the selection. Only Ctrl+Shift+C and Edit > Copy copy.
- "The scrollback-size setting is a separate change" (section 6.2): do not wire it. Every session keeps `term_init(rows, cols, 3000)`.
- "Heap, real size, no limit." Use no fixed buffer anywhere on the copy path; the 8 KB stack buffers in `window.c` go away.
- "One `Selection` per `Session`, explicitly zeroed in `create_session`". There is no global selection; `g_drag_session` is the only global.
- "**Up: finish and copy first, then release capture**, because `ReleaseCapture` sends `WM_CAPTURECHANGED` synchronously."
- "Wherever a drag is ended (up, capture change, a clear, tab close), capture is released only if `GetCapture() == hwnd`."
- "a move without `MK_LBUTTON` is ignored"
- Stamps: "Every parser write goes through `get_screen_row()` ... which stamps `row->gen = ++t->write_gen`"; "`term_scroll_up`/`term_scroll_down` in `buffer.c` stamp the rows they move or clear; the recycle branch stamps the refilled row after `lines_recycled` is incremented". The parser has no other stamp site. Cursor motion never stamps.
- "The stored last position is used, never the live cursor." "The view never scrolls past the oldest or the newest line."
- Timer 1's tick (10 ms) runs **outside** the per-session `io.ctx`/`CONN_IDLE` gate. The design-system rule applies: no second `WM_TIMER` loop.
- Mouse handlers read coordinates with `GET_X_LPARAM`/`GET_Y_LPARAM` (`<windowsx.h>`), never `LOWORD`/`HIWORD`.
- The debug events are `sel.begin` (line, col, mode), `sel.scroll` (offset), `sel.copy` (lines, bytes) and `sel.clear` (reason: click, stale, epoch, recycled, close), all at `DLOG_DEBUG` on `DLOG_GENERAL`.
- Compiler flags: code compiles clean under `-std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion`. Cast `size_t`/`int`/`uint64_t` explicitly. Do not shadow parameters (WndProc's parameter is `UINT msg`). Include `<stdio.h>`, `<string.h>` and `<stdlib.h>` wherever they are used.
- Portable logic goes in `src/core` or `src/term`. `src/ui` is excluded from the native tests (`NON_TEST_SRCS`). Every test function is declared and called in `tests/runner.c`.
- Design-system rules for `src/ui`: no `RGB(`, no `MulDiv(x, dpi, 96)`, no `CreateFont`, no new timer loop. `tests/test_ui_tokens.c` enforces them.
- Commits stage files by path (read every `??` line of `git status --short`). End each commit message with the repo's `Models:` line followed by the `Co-Authored-By:` trailer; the orchestrator fills in the model ids. Every commit step below says "commit with the repo's Models line and trailer".
- Do not run `make release` before Task 11. The UI compile check below uses `-fsyntax-only`, which produces no binary and so needs no version bump.

### Conventions used by every task

- **Native suite:** `make test 2>&1 | tail -3` ends with `Tests Run: N, Failed: 0`. To see failures: `make test 2>&1 | grep -E "^\[FAIL\]|Assertion failed|error:|undefined reference" | head -40`.
- **Product-flags compile check (Linux host):** the command is spelled out in each task. It is `x86_64-w64-mingw32-gcc` with the Makefile's `CFLAGS` warning set and include path, plus `-fsyntax-only`. On the Windows (MSYS2) host, use `gcc` and drop the `-I$HOME/vcpkg/...` include.
- **Ordering note:** Task 4 removes the old selection API, which `src/ui/window.c` and `src/ui/renderer.c` still use until Task 7. `make test` stays green the whole time because `src/ui` is not in the native build. Before Task 7, run the product-flags check only on `src/core` and `src/term` files.

## Review Focus

These are input classes the spec does not test. Each is pinned to a test in the task that owns the code.

1. **Double-click on a word with accented Latin letters** (`café`, `naïve`). A person expects the whole word, not `caf`. Pinned in Task 5 by `test_sel_word_accented_latin` and the `0xE9`/`0x0436` rows of `test_sel_char_class`.
2. **Confirm Paste: Tab to Cancel, then Enter.** A person expects Enter to press the focused button (Cancel), not to paste anyway. Pinned in Task 8 by `test_paste_key_enter_on_cancel_cancels` and in Task 10 by the harness case `paste_with_confirmation_tab_enter_cancels`.
3. **A selection held while the 32-bit write counter wraps** (a long build log or `yes` streaming in another part of the screen). The selection must not vanish without a write into it, and must not survive one. Pinned in Task 6 by `test_sel_stamps_survive_counter_wrap`.
4. **A drag or double-click over blank cells only** (the empty area right of a prompt). The copy is empty and the clipboard keeps what it had. Pinned in Task 4 by `test_sel_copy_blank_only_is_empty`, in Task 5 by `test_sel_word_run_of_spaces`, and in Task 7 by the `n > 0` guard in `copy_terminal_selection`.
5. **Zero or negative cell size** (before the font exists, or a minimised window). Selection must not divide by zero or scroll without limit. Pinned in Task 4 by `test_sel_pixel_to_cell_zero_cell_size` and `test_sel_autoscroll_step_zero_char_height`.

---

### Task 1: Terminal view mapping and scroll clamps

**Files:**
- Modify: `src/term/term.h` (declarations after `term_screen_to_phys`)
- Modify: `src/term/buffer.c` (`term_screen_to_phys` neighbourhood, `term_scroll`, `term_resize`)
- Test: `tests/test_term.c`, `tests/runner.c`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `int term_view_to_logical(const Terminal *t, int screen_row);` returns the logical index shown at `screen_row` with `scrollback_offset` applied and the top clamped to 0. It returns -1 only when `screen_row` is outside `[0, rows)`.
  - `int term_max_scroll_offset(const Terminal *t);` returns `min(lines_count - rows, max_scrollback)`, never below 0. This is the history actually held.
  - `term_resize()` clamps `scrollback_offset` to `term_max_scroll_offset()`.

- [ ] **Step 1: Write the failing tests** (append to `tests/test_term.c`; add `#include <string.h>` and `#include "edit_scroll.h"` to its includes)

```c
/* ---- View mapping and scroll clamps (spec 2026-10-20-putty-selection-design.md 2.1) ---- */

static int phys_of_logical(const Terminal *t, int logical)
{
    return (t->lines_start + logical) % t->lines_capacity;
}

static void feed_x_lines(Terminal *t, int n)
{
    for (int i = 0; i < n; i++) term_process(t, "x\r\n", 3);
}

int test_term_view_to_logical_offset_zero_matches_live(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);          /* capacity 25 */
    feed_x_lines(t, 30);                         /* 4 cursor moves, 20 appends, 6 recycles */
    ASSERT_TRUE(t->lines_start != 0);            /* the ring has wrapped */
    for (int r = 0; r < t->rows; r++)
        ASSERT_EQ(phys_of_logical(t, term_view_to_logical(t, r)), term_screen_to_phys(t, r));
    ASSERT_EQ(term_view_to_logical(t, -1), -1);
    ASSERT_EQ(term_view_to_logical(t, t->rows), -1);
    term_free(t);
    TEST_END();
}

int test_term_view_to_logical_scrolled_back(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    feed_x_lines(t, 30);
    int live0 = term_view_to_logical(t, 0);
    t->scrollback_offset = 7;
    ASSERT_EQ(term_view_to_logical(t, 0), live0 - 7);
    ASSERT_EQ(term_view_to_logical(t, 4), live0 - 7 + 4);
    term_free(t);
    TEST_END();
}

int test_term_view_to_logical_fresh_tab_clamps_top(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    t->scrollback_offset = t->rows - 1;          /* stale: no history exists */
    ASSERT_EQ(term_view_to_logical(t, 0), 0);    /* not -1 */
    ASSERT_EQ(term_view_to_logical(t, 4), 4);
    term_free(t);
    TEST_END();
}

int test_term_resize_clamps_scrollback_offset(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    feed_x_lines(t, 8);                          /* lines_count 9: 4 lines of history */
    t->scrollback_offset = 50;                   /* more than the history held */
    term_resize(t, 6, 10);
    ASSERT_EQ(t->scrollback_offset, term_max_scroll_offset(t));
    ASSERT_EQ(t->scrollback_offset, 3);          /* 9 lines - 6 rows */
    term_free(t);
    TEST_END();
}

int test_term_max_scroll_offset_is_history_held(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    ASSERT_EQ(term_max_scroll_offset(t), 0);
    feed_x_lines(t, 6);                          /* 2 lines of history */
    ASSERT_EQ(term_max_scroll_offset(t), 2);
    /* Page Up clamps to the history held, not to max_scrollback (20). */
    ASSERT_EQ(scroll_page_up(0, t->rows, term_max_scroll_offset(t)), 2);
    feed_x_lines(t, 100);
    ASSERT_EQ(term_max_scroll_offset(t), 20);    /* capped by max_scrollback */
    term_free(t);
    TEST_END();
}

int test_term_parser_writes_live_screen_while_scrolled_back(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    feed_x_lines(t, 12);
    t->scrollback_offset = 3;
    term_process(t, "\x1b[1;1HZ", 7);
    ASSERT_EQ(t->lines[term_screen_to_phys(t, 0)]->cells[0].codepoint, 'Z');
    int view0 = term_view_to_logical(t, 0);
    ASSERT_EQ(t->lines[phys_of_logical(t, view0)]->cells[0].codepoint, 'x');
    term_free(t);
    TEST_END();
}
```

Register the tests in `tests/runner.c`. Add the declarations after `int test_term_write_seq_unchanged_on_zero_len(void);`:

```c
int test_term_view_to_logical_offset_zero_matches_live(void);
int test_term_view_to_logical_scrolled_back(void);
int test_term_view_to_logical_fresh_tab_clamps_top(void);
int test_term_resize_clamps_scrollback_offset(void);
int test_term_max_scroll_offset_is_history_held(void);
int test_term_parser_writes_live_screen_while_scrolled_back(void);
```

Add the calls after `failed += test_term_write_seq_unchanged_on_zero_len();`:

```c
    failed += test_term_view_to_logical_offset_zero_matches_live();
    failed += test_term_view_to_logical_scrolled_back();
    failed += test_term_view_to_logical_fresh_tab_clamps_top();
    failed += test_term_resize_clamps_scrollback_offset();
    failed += test_term_max_scroll_offset_is_history_held();
    failed += test_term_parser_writes_live_screen_while_scrolled_back();
```

- [ ] **Step 2: Run to verify they fail**

Run: `make test 2>&1 | grep -E "error:|undefined reference|warning: implicit" | head`
Expected: the build fails on `term_view_to_logical` and `term_max_scroll_offset`, with an implicit declaration or an undefined reference.

- [ ] **Step 3: Implement**

In `src/term/term.h`, add these declarations after the `term_screen_to_phys` declaration:

```c
/* Logical index of the row shown at screen_row in the view as scrolled
 * (scrollback_offset applied, top clamped to 0 exactly as the renderer's
 * get_visible_row did). Never -1 for 0 <= screen_row < rows; -1 outside.
 * The view mapping: the renderer and the selection use it. The parser keeps
 * term_screen_to_phys(), the live mapping -- writes always land on the live
 * screen, scrolled back or not (spec 2026-10-20-putty-selection-design.md 2.1). */
int term_view_to_logical(const Terminal *t, int screen_row);

/* The furthest the view can scroll back: the history actually held,
 * min(lines_count - rows, max_scrollback), never below 0. The wheel, the
 * scrollbar, Page Up, smart scrolling and term_resize() all clamp to it. */
int term_max_scroll_offset(const Terminal *t);
```

In `src/term/buffer.c`, add these functions directly after `term_screen_to_phys()`:

```c
int term_view_to_logical(const Terminal *t, int screen_row)
{
    if (!t || screen_row < 0 || screen_row >= t->rows) return -1;
    int off = t->scrollback_offset > 0 ? t->scrollback_offset : 0;
    int top = (t->lines_count >= t->rows) ? (t->lines_count - t->rows - off) : -off;
    if (top < 0) top = 0;
    return top + screen_row;
}

int term_max_scroll_offset(const Terminal *t)
{
    if (!t) return 0;
    int max_off = t->lines_count - t->rows;
    if (max_off > t->max_scrollback) max_off = t->max_scrollback;
    return max_off > 0 ? max_off : 0;
}
```

In `term_scroll()`, replace the smart-scrolling clamp

```c
    if (term->scrollback_offset > 0) {
        int max_off = term->lines_count - term->rows;
        if (max_off > term->max_scrollback) max_off = term->max_scrollback;
        if (max_off < 0) max_off = 0;
        term->scrollback_offset++;
        if (term->scrollback_offset > max_off) term->scrollback_offset = max_off;
    }
```

with

```c
    if (term->scrollback_offset > 0) {
        int max_off = term_max_scroll_offset(term);
        term->scrollback_offset++;
        if (term->scrollback_offset > max_off) term->scrollback_offset = max_off;
    }
```

In `term_resize()`, directly after `term->scroll_bot = rows - 1;`, add:

```c
    /* A view scrolled back further than the reflowed history holds would
     * show rows that do not exist (spec 2026-10-20 section 2.1). */
    {
        int max_off = term_max_scroll_offset(term);
        if (term->scrollback_offset > max_off) term->scrollback_offset = max_off;
        if (term->scrollback_offset < 0) term->scrollback_offset = 0;
    }
```

- [ ] **Step 4: Run the tests and the product-flags check**

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`.

Run: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/term/buffer.c`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add src/term/term.h src/term/buffer.c tests/test_term.c tests/runner.c
git commit -m "term: view mapping and scroll-offset clamp for selection"
```
Commit with the repo's Models line and trailer.

---

### Task 2: Absolute line numbers, epoch and write stamps

**Files:**
- Modify: `src/term/term.h` (`TermRow`, `Terminal`, the inline `term_row_stamp`)
- Modify: `src/term/buffer.c` (`term_row_alloc`, `term_init`, `term_resize`, `term_scroll`, `term_scroll_up`, `term_scroll_down`, `term_alt_screen_enter`, `term_alt_screen_exit`)
- Modify: `src/term/parser.c:37-50` (`get_screen_row`)
- Test: `tests/test_term.c`, `tests/runner.c`

**Interfaces:**
- Consumes: Task 1's `term_view_to_logical`.
- Produces:
  - `TermRow.gen` (`uint32_t`): `write_gen` at the row's last write.
  - `Terminal.lines_recycled` (`uint64_t`): rows dropped from the front of this ring, counted per ring.
  - `Terminal.primary_lines_recycled` (`uint64_t`): the primary ring's count, parked while the alternate screen is active.
  - `Terminal.epoch` (`uint32_t`): bumped by reflow (`term_resize` with a new size) and by each alt-screen enter or exit, and by nothing else.
  - `Terminal.write_gen` (`uint32_t`): bumped by every row write.
  - `static inline void term_row_stamp(Terminal *term, TermRow *row)`: sets `row->gen = ++term->write_gen`.
  - The absolute line number of logical index `i` is `lines_recycled + i`.

- [ ] **Step 1: Write the failing tests** (append to `tests/test_term.c`)

```c
/* ---- Line numbers, epoch and write stamps (spec 2026-10-20 2.2) ---- */

static TermRow *logical_row(Terminal *t, int logical)
{
    return t->lines[(t->lines_start + logical) % t->lines_capacity];
}

/* Bit r set when screen row r's stamp changed while `seq` was processed. */
static unsigned stamp_mask(Terminal *t, const char *seq)
{
    uint32_t before[16];
    for (int r = 0; r < t->rows; r++) before[r] = t->lines[term_screen_to_phys(t, r)]->gen;
    term_process(t, seq, strlen(seq));
    unsigned mask = 0;
    for (int r = 0; r < t->rows; r++)
        if (t->lines[term_screen_to_phys(t, r)]->gen != before[r]) mask |= 1u << r;
    return mask;
}

int test_term_lines_recycled_counts_only_recycles(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 76);         /* capacity 100, 24 lines held */
    for (int i = 0; i < 76; i++) term_scroll(t); /* appends only */
    ASSERT_EQ(t->lines_recycled, (uint64_t)0);
    term_scroll(t);                              /* the first recycle */
    ASSERT_EQ(t->lines_recycled, (uint64_t)1);
    term_free(t);
    TEST_END();
}

int test_term_lines_recycled_keeps_line_numbers_stable(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 90);         /* capacity 100 */
    term_process(t, "\x1b[10;1HM", 8);           /* a marker on the bottom row */
    uint64_t marker = t->lines_recycled + (uint64_t)term_view_to_logical(t, 9);
    for (int i = 0; i < 50; i++) term_scroll(t);
    int logical = (int)(marker - t->lines_recycled);
    ASSERT_EQ(logical_row(t, logical)->cells[0].codepoint, 'M');
    for (int i = 0; i < 10000; i++) term_scroll(t);
    /* 10,050 scrolls: 90 appends filled the ring, the rest recycled. */
    ASSERT_EQ(t->lines_recycled, (uint64_t)(10050 - 90));
    ASSERT_TRUE(marker < t->lines_recycled);     /* the marked line is gone */
    term_free(t);
    TEST_END();
}

int test_term_alt_ring_has_its_own_recycle_count(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 2);           /* capacity 7 */
    for (int i = 0; i < 10; i++) term_scroll(t); /* 2 appends, 8 recycles */
    ASSERT_EQ(t->lines_recycled, (uint64_t)8);
    term_alt_screen_enter(t);
    ASSERT_EQ(t->lines_recycled, (uint64_t)0);
    for (int i = 0; i < 3; i++) term_scroll(t);  /* the alt ring is always full */
    ASSERT_EQ(t->lines_recycled, (uint64_t)3);
    term_alt_screen_exit(t);
    ASSERT_EQ(t->lines_recycled, (uint64_t)8);
    term_free(t);
    TEST_END();
}

int test_term_epoch_bumps_on_reflow_and_alt_switch_only(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    uint32_t e0 = t->epoch;
    term_process(t, "hello\r\nworld\x1b[2J\x1b[1;1H", 22);
    term_scroll(t);
    t->scrollback_offset = 1;
    term_resize(t, 5, 10);                       /* same size: no reflow */
    ASSERT_EQ(t->epoch, e0);
    term_resize(t, 6, 10);
    ASSERT_EQ(t->epoch, e0 + 1);
    term_alt_screen_enter(t);
    ASSERT_EQ(t->epoch, e0 + 2);
    term_resize(t, 6, 12);                       /* one resize on the alt screen: one bump */
    ASSERT_EQ(t->epoch, e0 + 3);
    term_alt_screen_exit(t);
    ASSERT_EQ(t->epoch, e0 + 4);
    term_free(t);
    TEST_END();
}

int test_term_stamp_cell_write_and_not_cursor_motion(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(8, 10, 20);
    ASSERT_EQ(stamp_mask(t, "\x1b[3;1HA"), 1u << 2);
    ASSERT_EQ(stamp_mask(t, "\x1b[5;5H\n\x1b[A\r"), 0u);   /* CUP, LF, CUU, CR */
    term_free(t);
    TEST_END();
}

int test_term_stamp_line_edits(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(8, 10, 20);
    ASSERT_EQ(stamp_mask(t, "\x1b[4h\x1b[3;1HB\x1b[4l"), 1u << 2); /* insert-mode shift */
    ASSERT_EQ(stamp_mask(t, "\x1b[3;1H\x1b[2@"), 1u << 2);          /* ICH */
    ASSERT_EQ(stamp_mask(t, "\x1b[3;1H\x1b[P"), 1u << 2);           /* DCH */
    ASSERT_EQ(stamp_mask(t, "\x1b[3;4H\x1b[K"), 1u << 2);           /* EL */
    ASSERT_EQ(stamp_mask(t, "\x1b[3;1H\x1b[3X"), 1u << 2);          /* ECH */
    term_free(t);
    TEST_END();
}

int test_term_stamp_erase_display(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(8, 10, 20);
    ASSERT_EQ(stamp_mask(t, "\x1b[6;1H\x1b[J"), 0xE0u);   /* ED 0: rows 5-7 */
    ASSERT_EQ(stamp_mask(t, "\x1b[3;2H\x1b[1J"), 0x07u);  /* ED 1: rows 0-2 */
    ASSERT_EQ(stamp_mask(t, "\x1b[2J"), 0xFFu);           /* ED 2: every row */
    term_free(t);
    TEST_END();
}

int test_term_stamp_region_scrolls(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(8, 10, 20);
    ASSERT_EQ(stamp_mask(t, "\x1b[6;1H\x1b[L"), 0xE0u);   /* IL at row 5: rows 5-7 */
    ASSERT_EQ(stamp_mask(t, "\x1b[6;1H\x1b[M"), 0xE0u);   /* DL at row 5 */
    ASSERT_EQ(stamp_mask(t, "\x1b[T"), 0xFFu);            /* SD: whole region */
    ASSERT_EQ(stamp_mask(t, "\x1b[3;6r\x1b[6;1H\n"), 0x3Cu); /* DECSTBM 2-5, LF at its bottom */
    term_free(t);
    TEST_END();
}

int test_term_stamp_full_screen_scroll_touches_only_the_new_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(8, 10, 20);
    term_process(t, "A\r\nB\r\nC", 7);
    int n = t->lines_count;
    uint32_t before[8];
    for (int i = 0; i < n; i++) before[i] = logical_row(t, i)->gen;
    term_process(t, "\x1b[S", 3);                /* SU on the primary: term_scroll() */
    ASSERT_EQ(t->lines_count, n + 1);
    for (int i = 0; i < n; i++) ASSERT_EQ(logical_row(t, i)->gen, before[i]);
    ASSERT_EQ(logical_row(t, n)->gen, t->write_gen);
    term_free(t);
    TEST_END();
}

int test_term_stamp_recycled_row_carries_its_new_number(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 2);           /* capacity 6 */
    term_scroll(t);
    term_scroll(t);                              /* full */
    TermRow *oldest = t->lines[t->lines_start];
    term_scroll(t);                              /* recycles it */
    ASSERT_EQ(t->lines_recycled, (uint64_t)1);
    int last = t->lines_count - 1;
    ASSERT_TRUE(logical_row(t, last) == oldest);
    ASSERT_EQ(oldest->gen, t->write_gen);
    ASSERT_EQ(t->lines_recycled + (uint64_t)last, (uint64_t)6); /* its new number */
    term_free(t);
    TEST_END();
}
```

Register the tests in `tests/runner.c`. Add the declarations after the Task 1 declarations:

```c
int test_term_lines_recycled_counts_only_recycles(void);
int test_term_lines_recycled_keeps_line_numbers_stable(void);
int test_term_alt_ring_has_its_own_recycle_count(void);
int test_term_epoch_bumps_on_reflow_and_alt_switch_only(void);
int test_term_stamp_cell_write_and_not_cursor_motion(void);
int test_term_stamp_line_edits(void);
int test_term_stamp_erase_display(void);
int test_term_stamp_region_scrolls(void);
int test_term_stamp_full_screen_scroll_touches_only_the_new_row(void);
int test_term_stamp_recycled_row_carries_its_new_number(void);
```

Add the calls after the Task 1 calls:

```c
    failed += test_term_lines_recycled_counts_only_recycles();
    failed += test_term_lines_recycled_keeps_line_numbers_stable();
    failed += test_term_alt_ring_has_its_own_recycle_count();
    failed += test_term_epoch_bumps_on_reflow_and_alt_switch_only();
    failed += test_term_stamp_cell_write_and_not_cursor_motion();
    failed += test_term_stamp_line_edits();
    failed += test_term_stamp_erase_display();
    failed += test_term_stamp_region_scrolls();
    failed += test_term_stamp_full_screen_scroll_touches_only_the_new_row();
    failed += test_term_stamp_recycled_row_carries_its_new_number();
```

- [ ] **Step 2: Run to verify they fail**

Run: `make test 2>&1 | grep -E "error:" | head`
Expected: `'Terminal' has no member named 'lines_recycled'` and `'TermRow' has no member named 'gen'`.

- [ ] **Step 3: Implement**

In `src/term/term.h`, add a field to `TermRow` after `bool wrapped;`:

```c
    uint32_t gen;      /* Terminal.write_gen at this row's last write (term_row_stamp) */
```

Add these fields to `Terminal` after `unsigned long write_seq;` and its comment:

```c
    /* Selection support (spec docs/superpowers/specs/2026-10-20-putty-selection-design.md
     * section 2.2). lines_recycled: rows dropped from the front of this ring,
     * ever -- per ring: the alternate screen counts its own while the
     * primary's waits in primary_lines_recycled. The absolute line number of
     * logical index i is lines_recycled + i, stable while epoch is. epoch:
     * bumped by reflow and by alt-screen enter/leave, the events that renumber
     * or replace the rows. write_gen: bumped by every row write; the row
     * written takes the new value (term_row_stamp()). It wraps after 2^32
     * writes, so compare stamps wrap-safely. */
    uint64_t lines_recycled;
    uint64_t primary_lines_recycled;
    uint32_t epoch;
    uint32_t write_gen;
```

Then add this after the `term_logical_rows()` inline function:

```c
/* Stamp a row as written now. The parser calls it only from get_screen_row(),
 * the one place every parser write passes through; buffer.c calls it for the
 * rows a scroll moves, clears or recycles. Cursor motion never stamps. */
static inline void term_row_stamp(Terminal *term, TermRow *row) {
    row->gen = ++term->write_gen;
}
```

In `src/term/buffer.c`:

- `term_row_alloc()`: after `row->wrapped = false;`, add `row->gen = 0;`.
- `term_init()`: after `term->log_id = 0;`, add:

```c
    term->lines_recycled         = 0;
    term->primary_lines_recycled = 0;
    term->epoch                  = 0;
    term->write_gen              = 0;
```

- `term_resize()`: directly after the `if (term->alt_screen_active) { term_reflow_buffer(...primary...); }` block, add:

```c
    /* Reflow renumbered every row: a selection's line numbers no longer apply. */
    term->epoch++;
```

- `term_scroll()`: replace the append/recycle `if`/`else` with:

```c
    if (term->lines_count < term->lines_capacity) {
        /* Buffer not full: append new line */
        int new_idx = (term->lines_start + term->lines_count) % term->lines_capacity;
        term_row_fill(term->lines[new_idx], term->cols, term->current_attr);
        term->lines_count++;
        term_row_stamp(term, term->lines[new_idx]);
    } else {
        /* Buffer full: recycle oldest line. Count it first, so the stamp
         * belongs to the row's new number, not the one it held. */
        int recycle_idx = term->lines_start;
        term_row_fill(term->lines[recycle_idx], term->cols, term->current_attr);
        term->lines_start = (term->lines_start + 1) % term->lines_capacity;
        term->lines_recycled++;
        term_row_stamp(term, term->lines[recycle_idx]);
    }
```

- In both `term_scroll_up()` and `term_scroll_down()`, replace the final loop

```c
    /* Mark all rows in the region dirty */
    for (int i = top; i <= bot; i++)
        term->lines[term_screen_to_phys(term, i)]->dirty = true;
```

with

```c
    /* Every row in the region moved or was cleared: dirty and stamped. */
    for (int i = top; i <= bot; i++) {
        TermRow *moved = term->lines[term_screen_to_phys(term, i)];
        moved->dirty = true;
        term_row_stamp(term, moved);
    }
```

- `term_alt_screen_enter()`: after `term->primary_cursor = term->cursor;`, add:

```c
    term->primary_lines_recycled  = term->lines_recycled;
    term->lines_recycled          = 0;
    term->epoch++;
```

- `term_alt_screen_exit()`: after `term->cursor = term->primary_cursor;`, add:

```c
    term->lines_recycled  = term->primary_lines_recycled;
    term->epoch++;
```

In `src/term/parser.c`, replace `get_screen_row()` with:

```c
/* Helper: the row at a screen row (0-based from the top of the live screen)
 * for writing. term_screen_to_phys() is the live mapping (term.h). Every
 * parser write reaches a row through here -- cell writes, insert-mode shifts,
 * ICH/DCH, EL/ED/ECH, the auto-wrap flag -- so this is the one place the
 * parser stamps a row (spec 2026-10-20 section 2.2). Cursor motion never
 * calls it. */
static TermRow *get_screen_row(Terminal *term, int screen_row) {
    if (screen_row < 0 || screen_row >= term->rows) return NULL;
    TermRow *row = term->lines[term_screen_to_phys(term, screen_row)];
    if (row) term_row_stamp(term, row);
    return row;
}
```

- [ ] **Step 4: Run the tests and the product-flags check**

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`.

Run: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/term/buffer.c src/term/parser.c`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add src/term/term.h src/term/buffer.c src/term/parser.c tests/test_term.c tests/runner.c
git commit -m "term: absolute line numbers, epoch and per-row write stamps"
```
Commit with the repo's Models line and trailer.

---

### Task 3: Clear `wrapped` when a row's content is erased

**Files:**
- Modify: `src/term/parser.c` (`handle_csi`: cases `'J'` and `'K'`)
- Test: `tests/test_term.c`, `tests/runner.c`

**Interfaces:**
- Consumes: nothing new.
- Produces: `TermRow.wrapped` is false after EL 2, after EL 0 from column 0, and after the whole-row loops of ED 0 and ED 1. LF, CUU and CUP never change it. Task 5's line selection and Task 4's copy rely on this.

- [ ] **Step 1: Write the failing tests** (append to `tests/test_term.c`)

```c
/* ---- wrapped on erase (spec 2026-10-20 2.4) ---- */

static TermRow *trow(Terminal *t, int screen_row)
{
    return t->lines[term_screen_to_phys(t, screen_row)];
}

static void tfeed(Terminal *t, const char *s)
{
    term_process(t, s, strlen(s));
}

int test_term_wrapped_cleared_by_el2(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    tfeed(t, "0123456789AB");                    /* row 1 continues row 0 */
    ASSERT_TRUE(trow(t, 1)->wrapped);
    tfeed(t, "\x1b[2;1H\x1b[2K");
    ASSERT_FALSE(trow(t, 1)->wrapped);
    term_free(t);
    TEST_END();
}

int test_term_wrapped_cleared_by_el_from_column_0_only(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    tfeed(t, "0123456789AB");
    tfeed(t, "\x1b[2;4H\x1b[K");                 /* EL 0 from column 3: partial */
    ASSERT_TRUE(trow(t, 1)->wrapped);
    tfeed(t, "\x1b[2;1H\x1b[K");                 /* EL 0 from column 0: whole row */
    ASSERT_FALSE(trow(t, 1)->wrapped);
    term_free(t);
    TEST_END();
}

int test_term_wrapped_cleared_by_ed_whole_rows(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    tfeed(t, "0123456789AB");
    tfeed(t, "\x1b[1;6H\x1b[J");                 /* ED 0 from row 0: row 1 is a whole row */
    ASSERT_FALSE(trow(t, 1)->wrapped);
    term_free(t);

    t = term_init(5, 10, 20);
    tfeed(t, "0123456789AB\r\nx");
    tfeed(t, "\x1b[3;1H\x1b[1J");                /* ED 1 from row 2: row 1 is a whole row */
    ASSERT_FALSE(trow(t, 1)->wrapped);
    term_free(t);
    TEST_END();
}

int test_term_wrapped_kept_by_readline_home_end(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    tfeed(t, "$ echo abcdefg");                  /* "$ echo abc" + "defg", cursor row 1 col 4 */
    ASSERT_TRUE(trow(t, 1)->wrapped);
    tfeed(t, "\x1b[A\r\x1b[2C");                 /* Home: CUU, CR, CUF */
    tfeed(t, "\n\x1b[5G");                       /* End: cud1 (a bare LF), CHA */
    tfeed(t, "\x1b[2;3H");                       /* CUP onto the row */
    ASSERT_TRUE(trow(t, 1)->wrapped);
    ASSERT_EQ(trow(t, 1)->cells[0].codepoint, 'd');
    term_free(t);
    TEST_END();
}

int test_term_wrapped_ctrl_u_leaves_next_output_unwrapped(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    tfeed(t, "$ echo abcdefg");
    /* Readline's Ctrl+U redisplay: back to the prompt, clear the rest of
     * row 0, step down and clear the old continuation from column 0, back up. */
    tfeed(t, "\r\x1b[A\x1b[2C\x1b[K\n\r\x1b[K\x1b[A\r\x1b[2C");
    tfeed(t, "ls\r\na.txt\r\n");
    ASSERT_EQ(trow(t, 1)->cells[0].codepoint, 'a');
    ASSERT_FALSE(trow(t, 1)->wrapped);           /* "a.txt" is its own line */
    term_free(t);
    TEST_END();
}
```

Register the tests in `tests/runner.c` after the Task 2 entries. Declarations:

```c
int test_term_wrapped_cleared_by_el2(void);
int test_term_wrapped_cleared_by_el_from_column_0_only(void);
int test_term_wrapped_cleared_by_ed_whole_rows(void);
int test_term_wrapped_kept_by_readline_home_end(void);
int test_term_wrapped_ctrl_u_leaves_next_output_unwrapped(void);
```

Calls:

```c
    failed += test_term_wrapped_cleared_by_el2();
    failed += test_term_wrapped_cleared_by_el_from_column_0_only();
    failed += test_term_wrapped_cleared_by_ed_whole_rows();
    failed += test_term_wrapped_kept_by_readline_home_end();
    failed += test_term_wrapped_ctrl_u_leaves_next_output_unwrapped();
```

- [ ] **Step 2: Run to verify they fail**

Run: `make test 2>&1 | grep -E "^\[FAIL\] test_term_wrapped" `
Expected: these four FAIL: `test_term_wrapped_cleared_by_el2`, `test_term_wrapped_cleared_by_el_from_column_0_only`, `test_term_wrapped_cleared_by_ed_whole_rows` and `test_term_wrapped_ctrl_u_leaves_next_output_unwrapped`. `test_term_wrapped_kept_by_readline_home_end` passes already; it guards against an over-eager fix.

- [ ] **Step 3: Implement** (in `src/term/parser.c`, `handle_csi`)

In `case 'J'`, `n == 0` branch, the loop over `r = term->cursor.row + 1 ...`: after `rr->len = 0;`, add `rr->wrapped = false;`.

In `case 'J'`, `n == 1` branch, the loop over `r = 0; r < term->cursor.row`: after `rr->len = 0;`, add `rr->wrapped = false;`.

In `case 'K'`, after the erase loop (`for (int c = start; c < end; c++) {...}`) and before `row->dirty = true;`, add:

```c
                /* A whole-row erase ends the soft wrap onto this row (spec
                 * 2026-10-20 section 2.4): EL 2, or EL 0 from column 0.
                 * Cursor motion never touches the flag -- readline moves
                 * onto a continuation row with a bare LF and back with CUU. */
                if (n == 2 || (n == 0 && cc == 0)) row->wrapped = false;
```

- [ ] **Step 4: Run the tests and the product-flags check**

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`.

Run: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/term/parser.c`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add src/term/parser.c tests/test_term.c tests/runner.c
git commit -m "term: clear the soft-wrap flag when a row's content is erased"
```
Commit with the repo's Models line and trailer.

---

### Task 4: Selection core: buffer-absolute points, drag, copy, Select All

**Files:**
- Rewrite: `src/core/selection.h`, `src/core/selection.c`
- Rewrite: `tests/test_selection.c`
- Modify: `tests/runner.c` (the `test_sel_*` declaration block after `/* test_selection.c */` near line 1886, and the calls after `printf("\n--- Selection ---\n");` near line 5287)

**Interfaces:**
- Consumes: `term_view_to_logical`, `term_max_scroll_offset` (Task 1); `lines_recycled`, `epoch`, `write_gen`, `TermRow.gen` (Task 2); `wrapped` semantics (Task 3); `cell_text_codepoint`, `utf8_encode` (`src/core/string_utils.h`); `xrealloc` (`src/core/xmalloc.h`).
- Produces (later tasks use these exact names):
  - `typedef struct { uint64_t line; int col; } SelPoint;`
  - `typedef enum { SEL_STREAM = 0, SEL_WORD = 1, SEL_LINE = 2 } SelMode;`
  - `Selection` with the fields `anchor, anchor_end, start, end` (`SelPoint`), `mode` (`SelMode`), `epoch, gen` (`uint32_t`), `active, valid` (`bool`), `press_x, press_y, last_x, last_y` (`int`).
  - `void selection_pixel_to_cell(int px, int py, int char_w, int char_h, int y_offset, int rows, int cols, int *out_row, int *out_col);`
  - `int selection_autoscroll_step(int py, int top_px, int bottom_px, int char_h);` returns negative above `top_px`, positive at or below `bottom_px` (exclusive), magnitude `1 + distance / char_h`.
  - `void selection_begin(Selection *s, const Terminal *t, int screen_row, int col, int click_count, int px, int py);`
  - `void selection_extend(Selection *s, const Terminal *t, int screen_row, int col, int px, int py, int drag_cx, int drag_cy);`
  - `bool selection_finish(Selection *s, const Terminal *t);`
  - `void selection_clear(Selection *s);`
  - `void selection_select_all(Selection *s, const Terminal *t);`
  - `bool selection_contains(const Selection *s, const Terminal *t, int screen_row, int col);`
  - `bool selection_in_view(const Selection *s, const Terminal *t);`
  - `char *selection_copy_text(const Selection *s, const Terminal *t, size_t *out_len);` always returns a heap string the caller frees (`""` at least).
  - `selection_normalise()` and `selection_extract_text()` are removed.

In this task `selection_begin` treats every click count as `SEL_STREAM`; Task 5 adds words and lines, and Task 6 adds staleness and `selection_sync`.

- [ ] **Step 1: Write the failing tests.** Replace `tests/test_selection.c` entirely with:

```c
#include "test_framework.h"
#include "selection.h"
#include "term.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Terminal selection, PuTTY style (spec
 * docs/superpowers/specs/2026-10-20-putty-selection-design.md section 3.1). */

/* ---- helpers ------------------------------------------------------------ */

static void feed(Terminal *t, const char *text)
{
    term_process(t, text, strlen(text));
}

/* "L00", "\r\nL01", ...: line i of the batch reads "L%02d" of first + i. */
static void feed_lines(Terminal *t, int first, int count)
{
    char buf[32];
    for (int i = 0; i < count; i++) {
        int n = snprintf(buf, sizeof buf, "%sL%02d", i ? "\r\n" : "", first + i);
        term_process(t, buf, (size_t)n);
    }
}

/* A drag from screen cell (r0,c0) to (r1,c1), well past the drag threshold,
 * then released. Returns what selection_finish() returns. */
static bool drag(Selection *s, const Terminal *t, int r0, int c0, int r1, int c1)
{
    selection_begin(s, t, r0, c0, 1, 0, 0);
    selection_extend(s, t, r1, c1, 100, 100, 4, 4);
    return selection_finish(s, t);
}

/* selection_copy_text() as a string the caller frees. */
static char *copy_str(const Selection *s, const Terminal *t)
{
    size_t n = 0;
    return selection_copy_text(s, t, &n);
}

/* ---- pixel_to_cell ------------------------------------------------------ */

int test_sel_pixel_to_cell_basic(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(40, 64, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 2);
    ASSERT_EQ(col, 5);
    TEST_END();
}

int test_sel_pixel_to_cell_origin(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(0, 32, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

int test_sel_pixel_to_cell_clamp_negative(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(0, 10, 8, 16, 32, 24, 80, &row, &col);   /* in the tab strip */
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

int test_sel_pixel_to_cell_clamp_max(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(800, 500, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 23);
    ASSERT_EQ(col, 79);
    TEST_END();
}

int test_sel_pixel_to_cell_mid_cell(void)
{
    TEST_BEGIN();
    int row, col;
    selection_pixel_to_cell(12, 44, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 1);
    TEST_END();
}

/* A captured drag reports negative client coordinates above or left of the
 * window (GET_X/Y_LPARAM, signed): they clamp, never wrap. */
int test_sel_pixel_to_cell_negative_x_and_y(void)
{
    TEST_BEGIN();
    int row = -1, col = -1;
    selection_pixel_to_cell(-30, -40, 8, 16, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

/* Review Focus 5: no font metrics yet. */
int test_sel_pixel_to_cell_zero_cell_size(void)
{
    TEST_BEGIN();
    int row = -1, col = -1;
    selection_pixel_to_cell(100, 100, 0, 0, 32, 24, 80, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 0);
    TEST_END();
}

/* ---- autoscroll_step ---------------------------------------------------- */

int test_sel_autoscroll_step_edges(void)
{
    TEST_BEGIN();
    /* Terminal area y in [32, 416): 24 rows of 16 px. */
    ASSERT_EQ(selection_autoscroll_step(32, 32, 416, 16), 0);
    ASSERT_EQ(selection_autoscroll_step(200, 32, 416, 16), 0);
    ASSERT_EQ(selection_autoscroll_step(415, 32, 416, 16), 0);
    ASSERT_EQ(selection_autoscroll_step(31, 32, 416, 16), -1);       /* one pixel above */
    ASSERT_EQ(selection_autoscroll_step(0, 32, 416, 16), -3);        /* two cell heights above */
    ASSERT_EQ(selection_autoscroll_step(416, 32, 416, 16), 1);       /* one pixel below */
    ASSERT_EQ(selection_autoscroll_step(416 + 15, 32, 416, 16), 2);  /* one cell height below */
    TEST_END();
}

/* Review Focus 5: a zero cell height must not divide by zero. */
int test_sel_autoscroll_step_zero_char_height(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(selection_autoscroll_step(10, 32, 416, 0) < 0);
    ASSERT_TRUE(selection_autoscroll_step(500, 32, 416, 0) > 0);
    ASSERT_EQ(selection_autoscroll_step(100, 32, 416, 0), 0);
    TEST_END();
}

/* ---- begin / extend / finish -------------------------------------------- */

int test_sel_click_is_not_a_selection(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    selection_begin(&s, t, 0, 3, 1, 27, 40);
    ASSERT_FALSE(selection_finish(&s, t));
    ASSERT_FALSE(s.valid);
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_move_under_threshold_is_not_a_selection(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    selection_begin(&s, t, 0, 3, 1, 31, 40);
    selection_extend(&s, t, 0, 4, 33, 40, 4, 4);   /* into the next cell, 2 px < 4 */
    ASSERT_FALSE(s.valid);
    ASSERT_FALSE(selection_finish(&s, t));
    term_free(t);
    TEST_END();
}

int test_sel_move_over_threshold_selects(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    selection_begin(&s, t, 0, 3, 1, 31, 40);
    selection_extend(&s, t, 0, 4, 35, 40, 4, 4);   /* 4 px: a drag */
    ASSERT_TRUE(s.valid);
    ASSERT_TRUE(selection_finish(&s, t));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "DE");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_backward_drag_normalises(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 7, 0, 2));
    ASSERT_EQ(s.start.col, 2);
    ASSERT_EQ(s.end.col, 7);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "CDEFGH");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_press_clears_previous_selection(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 0, 5));
    selection_begin(&s, t, 1, 0, 1, 0, 0);
    ASSERT_FALSE(s.valid);
    ASSERT_TRUE(s.active);
    term_free(t);
    TEST_END();
}

int test_sel_extend_after_finish_is_ignored(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "ABCDEFGHIJ");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 1, 0, 3));
    selection_extend(&s, t, 0, 8, 200, 200, 4, 4);
    ASSERT_EQ(s.end.col, 3);
    term_free(t);
    TEST_END();
}

/* Points are absolute line numbers: a drag while scrolled back names the
 * rows the view shows, and the copy reads exactly those (defect 2). */
int test_sel_points_are_buffer_absolute(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    feed_lines(t, 0, 12);                         /* L00..L11 at logical 0..11 */
    t->scrollback_offset = 3;                     /* the view shows L04..L08 */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    ASSERT_EQ(s.start.line, t->lines_recycled + (uint64_t)term_view_to_logical(t, 0));
    ASSERT_EQ(s.start.line, (uint64_t)4);
    ASSERT_EQ(s.end.line, (uint64_t)5);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "L04\r\nL05");
    free(txt);
    term_free(t);
    TEST_END();
}

/* ---- copy_text ---------------------------------------------------------- */

int test_sel_copy_wrapped_rows_are_one_line(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "ABCDEFGHIJKLMNOPQRSTUVWXY");            /* rows 0-2, rows 1-2 wrapped */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 2, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "ABCDEFGHIJKLMNOPQRSTUVWXY");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_keeps_space_at_wrap_boundary(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "123456foo bar");                         /* "123456foo " | "bar" */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 6, 1, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "foo bar");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_crlf_between_lines_none_at_end(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "one\r\ntwo\r\nthree");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 2, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "one\r\ntwo\r\nthree");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_trims_trailing_blanks_per_line(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "ab   \r\ncd");                           /* written spaces, then empty cells */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "ab\r\ncd");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_drops_trailing_empty_rows(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "top\r\n\r\n\r\n");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 3, 9));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "top");
    free(txt);
    term_free(t);
    TEST_END();
}

/* No size limit (defect 7): 3,000 lines of 200 characters, whole. */
int test_sel_copy_3000_rows_whole(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 200, 3000);
    char line[202];
    for (int i = 0; i < 3000; i++) {
        memset(line, 'a' + i % 26, 200);
        line[200] = '\r';
        line[201] = '\n';
        term_process(t, line, sizeof line);
    }
    Selection s;
    selection_select_all(&s, t);
    ASSERT_TRUE(s.valid);
    size_t n = 0;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)(3000 * 200 + 2999 * 2));
    ASSERT_EQ(strlen(txt), n);
    ASSERT_EQ(txt[0], 'a');
    ASSERT_EQ(txt[199], 'a');
    ASSERT_EQ(txt[200], '\r');
    ASSERT_EQ(txt[201], '\n');
    ASSERT_EQ(txt[n - 201], '\n');
    ASSERT_EQ(txt[n - 1], 'a' + 2999 % 26);
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_copy_empty_selection_is_empty_string(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "text");
    Selection s;
    selection_clear(&s);
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    txt = selection_copy_text(NULL, t, &n);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    txt = selection_copy_text(&s, NULL, NULL);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    term_free(t);
    TEST_END();
}

/* Review Focus 4: a selection of blanks copies nothing, so the window leaves
 * the clipboard alone. */
int test_sel_copy_blank_only_is_empty(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "text");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 2, 0, 2, 9));
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    term_free(t);
    TEST_END();
}

/* Hardening (kept from the old extractor's tests): DEL and C1 controls in
 * cells copy as spaces, never as the control byte. */
int test_sel_copy_controls_become_spaces(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "A\x7F" "B");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 0, 2));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "A B");
    free(txt);
    term_free(t);

    t = term_init(4, 10, 0);
    feed(t, "A\xC2\x80" "B");
    ASSERT_TRUE(drag(&s, t, 0, 0, 0, 2));
    txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "A B");
    free(txt);
    term_free(t);
    TEST_END();
}

/* ---- select_all, contains, in_view -------------------------------------- */

int test_sel_select_all_from_oldest_to_last_nonempty(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 3);            /* capacity 8 */
    feed_lines(t, 0, 12);                         /* L00..L03 recycled */
    ASSERT_EQ(t->lines_recycled, (uint64_t)4);
    Selection s;
    selection_select_all(&s, t);
    ASSERT_TRUE(s.valid);
    ASSERT_FALSE(s.active);
    ASSERT_EQ(s.start.line, (uint64_t)4);
    ASSERT_EQ(s.start.col, 0);
    ASSERT_EQ(s.end.line, (uint64_t)11);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "L04\r\nL05\r\nL06\r\nL07\r\nL08\r\nL09\r\nL10\r\nL11");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_select_all_empty_terminal_is_not_valid(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 3);
    Selection s;
    selection_select_all(&s, t);
    ASSERT_FALSE(s.valid);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_contains_and_in_view_follow_the_view(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 20);
    feed_lines(t, 0, 12);                         /* live view L07..L11 */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 2));         /* L07 col 0 .. L08 col 2 */
    ASSERT_TRUE(selection_contains(&s, t, 0, 0));
    ASSERT_TRUE(selection_contains(&s, t, 1, 2));
    ASSERT_FALSE(selection_contains(&s, t, 1, 3));
    ASSERT_FALSE(selection_contains(&s, t, 2, 0));
    t->scrollback_offset = 2;                     /* view L05..L09: L07 on row 2 */
    ASSERT_TRUE(selection_contains(&s, t, 2, 0));
    ASSERT_FALSE(selection_contains(&s, t, 0, 0));
    ASSERT_TRUE(selection_in_view(&s, t));
    t->scrollback_offset = 7;                     /* view L00..L04 */
    ASSERT_FALSE(selection_in_view(&s, t));
    ASSERT_FALSE(selection_contains(&s, t, 0, 0));
    term_free(t);
    TEST_END();
}
```

In `tests/runner.c`, replace the block of 19 declarations from `int test_sel_pixel_to_cell_basic(void);` through `int test_sel_extract_c1_control_becomes_space(void);` with:

```c
int test_sel_pixel_to_cell_basic(void);
int test_sel_pixel_to_cell_origin(void);
int test_sel_pixel_to_cell_clamp_negative(void);
int test_sel_pixel_to_cell_clamp_max(void);
int test_sel_pixel_to_cell_mid_cell(void);
int test_sel_pixel_to_cell_negative_x_and_y(void);
int test_sel_pixel_to_cell_zero_cell_size(void);
int test_sel_autoscroll_step_edges(void);
int test_sel_autoscroll_step_zero_char_height(void);
int test_sel_click_is_not_a_selection(void);
int test_sel_move_under_threshold_is_not_a_selection(void);
int test_sel_move_over_threshold_selects(void);
int test_sel_backward_drag_normalises(void);
int test_sel_press_clears_previous_selection(void);
int test_sel_extend_after_finish_is_ignored(void);
int test_sel_points_are_buffer_absolute(void);
int test_sel_copy_wrapped_rows_are_one_line(void);
int test_sel_copy_keeps_space_at_wrap_boundary(void);
int test_sel_copy_crlf_between_lines_none_at_end(void);
int test_sel_copy_trims_trailing_blanks_per_line(void);
int test_sel_copy_drops_trailing_empty_rows(void);
int test_sel_copy_3000_rows_whole(void);
int test_sel_copy_empty_selection_is_empty_string(void);
int test_sel_copy_blank_only_is_empty(void);
int test_sel_copy_controls_become_spaces(void);
int test_sel_select_all_from_oldest_to_last_nonempty(void);
int test_sel_select_all_empty_terminal_is_not_valid(void);
int test_sel_contains_and_in_view_follow_the_view(void);
```

Then replace the 19 calls from `failed += test_sel_pixel_to_cell_basic();` through `failed += test_sel_extract_c1_control_becomes_space();` with:

```c
    failed += test_sel_pixel_to_cell_basic();
    failed += test_sel_pixel_to_cell_origin();
    failed += test_sel_pixel_to_cell_clamp_negative();
    failed += test_sel_pixel_to_cell_clamp_max();
    failed += test_sel_pixel_to_cell_mid_cell();
    failed += test_sel_pixel_to_cell_negative_x_and_y();
    failed += test_sel_pixel_to_cell_zero_cell_size();
    failed += test_sel_autoscroll_step_edges();
    failed += test_sel_autoscroll_step_zero_char_height();
    failed += test_sel_click_is_not_a_selection();
    failed += test_sel_move_under_threshold_is_not_a_selection();
    failed += test_sel_move_over_threshold_selects();
    failed += test_sel_backward_drag_normalises();
    failed += test_sel_press_clears_previous_selection();
    failed += test_sel_extend_after_finish_is_ignored();
    failed += test_sel_points_are_buffer_absolute();
    failed += test_sel_copy_wrapped_rows_are_one_line();
    failed += test_sel_copy_keeps_space_at_wrap_boundary();
    failed += test_sel_copy_crlf_between_lines_none_at_end();
    failed += test_sel_copy_trims_trailing_blanks_per_line();
    failed += test_sel_copy_drops_trailing_empty_rows();
    failed += test_sel_copy_3000_rows_whole();
    failed += test_sel_copy_empty_selection_is_empty_string();
    failed += test_sel_copy_blank_only_is_empty();
    failed += test_sel_copy_controls_become_spaces();
    failed += test_sel_select_all_from_oldest_to_last_nonempty();
    failed += test_sel_select_all_empty_terminal_is_not_valid();
    failed += test_sel_contains_and_in_view_follow_the_view();
```

- [ ] **Step 2: Run to verify they fail**

Run: `make test 2>&1 | grep -E "error:" | head`
Expected: compile errors in `tests/test_selection.c`: `unknown type name 'SelPoint'` or `'Selection' has no member named 'start'`, and implicit declarations of `selection_begin`.

- [ ] **Step 3: Implement.** Replace `src/core/selection.h` entirely:

```c
#ifndef NUTSHELL_SELECTION_H
#define NUTSHELL_SELECTION_H

#include "term.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Terminal text selection, PuTTY style (spec
 * docs/superpowers/specs/2026-10-20-putty-selection-design.md). Pure: it
 * reads a const Terminal and never draws or touches the clipboard; window.c
 * owns the mouse capture, the timer and the clipboard.
 *
 * Points are buffer-absolute: `line` is the terminal's absolute line number
 * (lines_recycled + logical index, term.h), stable while the terminal's epoch
 * is, so a selection survives scrolling the view and new output scrolling
 * the text. */
typedef struct { uint64_t line; int col; } SelPoint;
typedef enum { SEL_STREAM = 0, SEL_WORD = 1, SEL_LINE = 2 } SelMode;

typedef struct {
    SelPoint anchor, anchor_end; /* the unit under the press (a cell for STREAM) */
    SelPoint start, end;         /* normalised, both inclusive */
    SelMode  mode;
    uint32_t epoch, gen;         /* the terminal's at the last change */
    bool     active;             /* button held */
    bool     valid;              /* a non-empty selection exists */
    int      press_x, press_y;   /* client pixels of the press, drag threshold */
    int      last_x, last_y;     /* last reported pointer, auto-scroll */
} Selection;

/* Client pixel (px, py) to a screen cell. px is relative to the terminal's
 * left edge, py to the client top (y_offset is where the grid starts).
 * Signed: a captured drag above or left of the window gives negative values.
 * Clamped to [0, rows) x [0, cols); a zero cell size gives cell 0. */
void selection_pixel_to_cell(int px, int py, int char_w, int char_h,
                             int y_offset, int rows, int cols,
                             int *out_row, int *out_col);

/* Lines the view should scroll this tick for a drag whose stored pointer is
 * at py: negative (back into history) above top_px, positive at or below
 * bottom_px (exclusive), 0 inside; magnitude 1 + distance / char_h. */
int  selection_autoscroll_step(int py, int top_px, int bottom_px, int char_h);

/* The press: clears s, then starts a selection at the screen cell
 * (click_count 1 a cell, 2 a word, 3 a line) and records the press pixel. */
void selection_begin(Selection *s, const Terminal *t, int screen_row, int col,
                     int click_count, int px, int py);
/* A move while the button is held. A STREAM press becomes a selection only
 * once the pointer has moved drag_cx/drag_cy pixels on either axis. */
void selection_extend(Selection *s, const Terminal *t, int screen_row, int col,
                      int px, int py, int drag_cx, int drag_cy);
/* The release: ends the drag and takes the terminal's stamps, so what is on
 * screen now is what is copied. Returns valid. */
bool selection_finish(Selection *s, const Terminal *t);
void selection_clear(Selection *s);
/* From the oldest line held to the last non-empty line; valid = false on an
 * empty terminal. */
void selection_select_all(Selection *s, const Terminal *t);
/* Is the cell at screen (row, col), in the view as scrolled, selected? */
bool selection_contains(const Selection *s, const Terminal *t, int screen_row, int col);
/* True when a valid selection intersects the rows in view (renderer hint). */
bool selection_in_view(const Selection *s, const Terminal *t);
/* The selected text: a soft-wrapped line is one line, trailing blanks trimmed
 * per logical line, trailing empty rows dropped, CRLF between lines and none
 * after the last. Always a heap string the caller frees ("" at least). */
char *selection_copy_text(const Selection *s, const Terminal *t, size_t *out_len);

#endif /* NUTSHELL_SELECTION_H */
```

Replace `src/core/selection.c` entirely:

```c
#include "selection.h"
#include "string_utils.h"
#include "xmalloc.h"
#include <stdlib.h>
#include <string.h>

/* ---- points and rows ---------------------------------------------------- */

static int sel_cmp(SelPoint a, SelPoint b)
{
    if (a.line != b.line) return (a.line < b.line) ? -1 : 1;
    return (a.col > b.col) - (a.col < b.col);
}

/* The row holding absolute line `line`, or NULL when it has been recycled or
 * lies past the rows the terminal holds. */
static const TermRow *sel_row(const Terminal *t, uint64_t line)
{
    if (line < t->lines_recycled) return NULL;
    uint64_t logical = line - t->lines_recycled;
    if (logical >= (uint64_t)term_logical_rows(t)) return NULL;
    return t->lines[(t->lines_start + (int)logical) % t->lines_capacity];
}

/* The absolute point under a screen cell of the view as scrolled. */
static SelPoint sel_point_at(const Terminal *t, int screen_row, int col)
{
    SelPoint p;
    int logical = term_view_to_logical(t, screen_row);
    if (logical < 0) logical = 0;
    p.line = t->lines_recycled + (uint64_t)logical;
    p.col = (col < 0) ? 0 : (col >= t->cols ? t->cols - 1 : col);
    return p;
}

static bool sel_row_blank(const Terminal *t, const TermRow *r)
{
    for (int c = 0; c < t->cols; c++)
        if (cell_text_codepoint(r->cells[c].codepoint) != (uint32_t)' ') return false;
    return true;
}

/* ---- a growable string -------------------------------------------------- */

typedef struct { char *p; size_t len, cap; } SelBuf;

static void sb_put(SelBuf *b, const char *src, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1) cap *= 2;
        b->p = xrealloc(b->p, cap);
        b->cap = cap;
    }
    memcpy(b->p + b->len, src, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void sb_cut(SelBuf *b, size_t len)
{
    b->len = len;
    b->p[len] = '\0';
}

/* ---- the API ------------------------------------------------------------ */

void selection_pixel_to_cell(int px, int py, int char_w, int char_h,
                             int y_offset, int rows, int cols,
                             int *out_row, int *out_col)
{
    int col = (char_w > 0) ? (px / char_w) : 0;
    int row = (char_h > 0) ? ((py - y_offset) / char_h) : 0;
    if (row >= rows) row = rows - 1;
    if (col >= cols) col = cols - 1;
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (out_row) *out_row = row;
    if (out_col) *out_col = col;
}

int selection_autoscroll_step(int py, int top_px, int bottom_px, int char_h)
{
    int ch = (char_h > 0) ? char_h : 1;
    if (py < top_px) return -(1 + (top_px - py) / ch);
    if (py >= bottom_px) return 1 + (py - bottom_px + 1) / ch;
    return 0;
}

void selection_begin(Selection *s, const Terminal *t, int screen_row, int col,
                     int click_count, int px, int py)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (!t) return;
    (void)click_count;                     /* a cell for every click count, for now */
    s->mode = SEL_STREAM;
    s->anchor = sel_point_at(t, screen_row, col);
    s->anchor_end = s->anchor;
    s->start = s->anchor;
    s->end = s->anchor_end;
    s->valid = false;                      /* a press alone selects nothing */
    s->epoch = t->epoch;
    s->gen = t->write_gen;
    s->active = true;
    s->press_x = s->last_x = px;
    s->press_y = s->last_y = py;
}

void selection_extend(Selection *s, const Terminal *t, int screen_row, int col,
                      int px, int py, int drag_cx, int drag_cy)
{
    if (!s || !t || !s->active) return;
    s->last_x = px;
    s->last_y = py;
    if (s->epoch != t->epoch) return;              /* the window ends the drag */
    if (s->mode == SEL_STREAM && !s->valid &&
        abs(px - s->press_x) < drag_cx && abs(py - s->press_y) < drag_cy)
        return;                                    /* still a click */
    SelPoint p = sel_point_at(t, screen_row, col);
    s->start = (sel_cmp(p, s->anchor) < 0) ? p : s->anchor;
    s->end = (sel_cmp(p, s->anchor_end) > 0) ? p : s->anchor_end;
    s->valid = true;
    s->gen = t->write_gen;
}

bool selection_finish(Selection *s, const Terminal *t)
{
    if (!s) return false;
    if (!s->active) return s->valid;
    s->active = false;
    if (!t || s->epoch != t->epoch) {
        selection_clear(s);
        return false;
    }
    s->gen = t->write_gen;
    return s->valid;
}

void selection_clear(Selection *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

void selection_select_all(Selection *s, const Terminal *t)
{
    if (!s) return;
    selection_clear(s);
    if (!t) return;
    int last = term_logical_rows(t) - 1;
    while (last >= 0 &&
           sel_row_blank(t, t->lines[(t->lines_start + last) % t->lines_capacity]))
        last--;
    if (last < 0) return;                          /* an empty terminal: nothing */
    s->mode = SEL_STREAM;
    s->start.line = t->lines_recycled;
    s->start.col = 0;
    s->end.line = t->lines_recycled + (uint64_t)last;
    s->end.col = t->cols - 1;
    s->anchor = s->start;
    s->anchor_end = s->end;
    s->epoch = t->epoch;
    s->gen = t->write_gen;
    s->valid = true;
}

bool selection_contains(const Selection *s, const Terminal *t, int screen_row, int col)
{
    if (!s || !t || !s->valid || s->epoch != t->epoch) return false;
    int logical = term_view_to_logical(t, screen_row);
    if (logical < 0) return false;
    uint64_t line = t->lines_recycled + (uint64_t)logical;
    if (line < s->start.line || line > s->end.line) return false;
    if (line == s->start.line && col < s->start.col) return false;
    if (line == s->end.line && col > s->end.col) return false;
    return true;
}

bool selection_in_view(const Selection *s, const Terminal *t)
{
    if (!s || !t || !s->valid || s->epoch != t->epoch || t->rows <= 0) return false;
    uint64_t top = t->lines_recycled + (uint64_t)term_view_to_logical(t, 0);
    uint64_t bottom = top + (uint64_t)(t->rows - 1);
    return s->start.line <= bottom && s->end.line >= top;
}

char *selection_copy_text(const Selection *s, const Terminal *t, size_t *out_len)
{
    SelBuf b = { NULL, 0, 0 };
    sb_put(&b, "", 0);                             /* always a string */
    if (out_len) *out_len = 0;
    if (!s || !t || !s->valid || s->epoch != t->epoch) return b.p;

    uint64_t first = s->start.line;
    int first_col = s->start.col;
    if (first < t->lines_recycled) {               /* its head has been recycled */
        first = t->lines_recycled;
        first_col = 0;
    }
    size_t keep = 0;   /* end of the current logical line's last non-blank cell */
    for (uint64_t ln = first; ln <= s->end.line; ln++) {
        const TermRow *r = sel_row(t, ln);
        if (!r) break;
        if (ln != first && !r->wrapped) {          /* a new logical line */
            sb_cut(&b, keep);                      /* trim the one just finished */
            sb_put(&b, "\r\n", 2);
            keep = b.len;
        }
        int c0 = (ln == first) ? first_col : 0;
        int c1 = (ln == s->end.line) ? s->end.col : t->cols - 1;
        if (c1 > t->cols - 1) c1 = t->cols - 1;
        for (int c = c0; c <= c1; c++) {
            uint32_t cp = cell_text_codepoint(r->cells[c].codepoint);
            char u8[4];
            int n = utf8_encode(cp, u8);
            if (n <= 0) continue;
            sb_put(&b, u8, (size_t)n);
            if (cp != (uint32_t)' ') keep = b.len;
        }
    }
    sb_cut(&b, keep);
    /* Trailing empty rows are dropped: no CRLF after the last line. */
    while (b.len >= 2 && b.p[b.len - 2] == '\r' && b.p[b.len - 1] == '\n')
        sb_cut(&b, b.len - 2);
    if (out_len) *out_len = b.len;
    return b.p;
}
```

- [ ] **Step 4: Run the tests and the product-flags check**

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`. The new `test_sel_*` tests show `[PASS]`; check with `make test 2>&1 | grep -c "^\[PASS\] test_sel_"`.

Run: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/core/selection.c`
Expected: no output. Do not compile `src/ui` yet (see the Ordering note).

- [ ] **Step 5: Commit**

```bash
git add src/core/selection.h src/core/selection.c tests/test_selection.c tests/runner.c
git commit -m "selection: buffer-absolute points, drag threshold, unbounded CRLF copy"
```
Commit with the repo's Models line and trailer.

---

### Task 5: Double-click words and triple-click lines

**Files:**
- Modify: `src/core/selection.h` (declare `selection_char_class`)
- Modify: `src/core/selection.c` (add word and line helpers after `sel_point_at`; replace `selection_begin` and `selection_extend`; add `selection_char_class`)
- Test: `tests/test_selection.c`, `tests/runner.c`

**Interfaces:**
- Consumes: Task 4's `Selection`, `SelPoint`, `sel_row`, `sel_point_at`, `sel_cmp` (all in `selection.c`).
- Produces:
  - `int selection_char_class(uint32_t cp);` returns 0 for blanks (space, empty cell, control) and 1 for word characters (ASCII letters and digits, `_ - . / ~ : @`, and the letters of Latin-1 Supplement and Latin Extended-A/B except `×` and `÷`, IPA, Greek and Cyrillic). Any other codepoint returns `2 + cp`.
  - `selection_begin` with `click_count == 2` selects the word under the press, and with `click_count >= 3` the logical line. Both are `valid` at once.
  - `selection_extend` in WORD and LINE modes extends by whole units in either direction.

- [ ] **Step 1: Write the failing tests** (append to `tests/test_selection.c`)

```c
/* ---- words and lines (spec section 1) ----------------------------------- */

/* A double- or triple-click at a screen cell, released where it was pressed. */
static bool click_n(Selection *s, const Terminal *t, int row, int col, int clicks)
{
    selection_begin(s, t, row, col, clicks, 0, 0);
    selection_extend(s, t, row, col, 0, 0, 4, 4);
    return selection_finish(s, t);
}

int test_sel_char_class(void)
{
    TEST_BEGIN();
    ASSERT_EQ(selection_char_class(0), 0);
    ASSERT_EQ(selection_char_class(' '), 0);
    ASSERT_EQ(selection_char_class(0x07), 0);                 /* a control codepoint */
    const char *word = "azAZ09_-./~:@";
    for (const char *p = word; *p; p++)
        ASSERT_EQ(selection_char_class((uint32_t)(unsigned char)*p), 1);
    ASSERT_EQ(selection_char_class(0xE9), 1);                 /* é: Review Focus 1 */
    ASSERT_EQ(selection_char_class(0x0436), 1);               /* ж */
    ASSERT_EQ(selection_char_class(','), 2 + ',');
    ASSERT_EQ(selection_char_class(0xD7), 2 + 0xD7);          /* × */
    ASSERT_EQ(selection_char_class(0x65E5), 2 + 0x65E5);      /* 日 */
    TEST_END();
}

int test_sel_word_path(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 40, 10);
    feed(t, "ls /usr/local/bin now");
    Selection s;
    selection_begin(&s, t, 0, 7, 2, 0, 0);
    ASSERT_TRUE(s.valid);                                     /* highlighted on the press */
    ASSERT_TRUE(selection_finish(&s, t));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "/usr/local/bin");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_stops_at_comma(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "foo,bar");
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 1, 2));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "foo");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_run_of_spaces(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "a    b");
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 2, 2));
    ASSERT_EQ(s.start.col, 1);
    ASSERT_EQ(s.end.col, 4);
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);                                  /* Review Focus 4 */
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_cjk_cell(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");            /* 日本語, one cell each */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 1, 2));
    ASSERT_EQ(s.start.col, 1);
    ASSERT_EQ(s.end.col, 1);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "\xE6\x9C\xAC");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_follows_wrap(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "go http://ex.com/a/b ok");                       /* the URL wraps onto row 1 */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 1, 2, 2));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "http://ex.com/a/b");
    free(txt);
    term_free(t);
    TEST_END();
}

/* Review Focus 1: an accented word is one word. */
int test_sel_word_accented_latin(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "un caf\xC3\xA9 noir");
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 5, 2));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "caf\xC3\xA9");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_line_middle_of_three_rows(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "$ abcdefghijklmnopqrstuvwxyz\r\nnext");             /* 28 chars on rows 0-2 */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 1, 4, 3));
    ASSERT_EQ(s.start.line, (uint64_t)0);
    ASSERT_EQ(s.start.col, 0);
    ASSERT_EQ(s.end.line, (uint64_t)2);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "$ abcdefghijklmnopqrstuvwxyz");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_line_head_recycled_stops_at_oldest(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(3, 10, 1);                        /* capacity 4 */
    /* One 45-character line over five rows: the fifth recycles the head. */
    feed(t, "0123456789abcdefghijABCDEFGHIJklmnopqrstKLMNO");
    ASSERT_EQ(t->lines_recycled, (uint64_t)1);
    t->scrollback_offset = 1;                                 /* row 0 shows logical 0 */
    Selection s;
    ASSERT_TRUE(click_n(&s, t, 0, 3, 3));
    ASSERT_EQ(s.start.line, t->lines_recycled);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "abcdefghijABCDEFGHIJklmnopqrstKLMNO");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_word_drag_backwards_whole_words(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    feed(t, "alpha beta gamma");
    Selection s;
    selection_begin(&s, t, 0, 13, 2, 0, 0);                   /* in "gamma" */
    selection_extend(&s, t, 0, 7, 0, 0, 4, 4);                /* into "beta" */
    ASSERT_TRUE(selection_finish(&s, t));
    ASSERT_EQ(s.start.col, 6);
    ASSERT_EQ(s.end.col, 15);
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "beta gamma");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_line_drag_extends_by_lines(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 10);
    feed(t, "one\r\ntwo\r\nthree");
    Selection s;
    selection_begin(&s, t, 2, 1, 3, 0, 0);
    selection_extend(&s, t, 0, 2, 0, 0, 4, 4);
    ASSERT_TRUE(selection_finish(&s, t));
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "one\r\ntwo\r\nthree");
    free(txt);
    term_free(t);
    TEST_END();
}
```

Register the tests in `tests/runner.c`. Add the declarations after `int test_sel_contains_and_in_view_follow_the_view(void);`:

```c
int test_sel_char_class(void);
int test_sel_word_path(void);
int test_sel_word_stops_at_comma(void);
int test_sel_word_run_of_spaces(void);
int test_sel_word_cjk_cell(void);
int test_sel_word_follows_wrap(void);
int test_sel_word_accented_latin(void);
int test_sel_line_middle_of_three_rows(void);
int test_sel_line_head_recycled_stops_at_oldest(void);
int test_sel_word_drag_backwards_whole_words(void);
int test_sel_line_drag_extends_by_lines(void);
```

Add the calls after `failed += test_sel_contains_and_in_view_follow_the_view();`:

```c
    failed += test_sel_char_class();
    failed += test_sel_word_path();
    failed += test_sel_word_stops_at_comma();
    failed += test_sel_word_run_of_spaces();
    failed += test_sel_word_cjk_cell();
    failed += test_sel_word_follows_wrap();
    failed += test_sel_word_accented_latin();
    failed += test_sel_line_middle_of_three_rows();
    failed += test_sel_line_head_recycled_stops_at_oldest();
    failed += test_sel_word_drag_backwards_whole_words();
    failed += test_sel_line_drag_extends_by_lines();
```

- [ ] **Step 2: Run to verify they fail**

Run: `make test 2>&1 | grep -E "error:|undefined reference|^\[FAIL\] test_sel_" | head -20`
Expected: the build fails with an undefined reference to `selection_char_class`. (If you add the function first, the word and line tests FAIL instead, because every click count still selects a single cell.)

- [ ] **Step 3: Implement**

In `src/core/selection.h`, add before `#endif`:

```c
/* Word classes for a double-click: 0 blank (space, empty cell, control
 * codepoint); 1 word (letters, digits, _ - . / ~ : @); 2 + cp otherwise, so
 * every other codepoint is a class of its own. "Letters" are ASCII plus
 * Latin-1 Supplement and Latin Extended-A/B (not U+00D7 or U+00F7), IPA,
 * Greek and Cyrillic; CJK and the rest stand alone (one codepoint per cell,
 * no wide-cell model). */
int  selection_char_class(uint32_t cp);
```

In `src/core/selection.c`, insert directly after `sel_point_at()`:

```c
/* ---- words and lines ---------------------------------------------------- */

static uint32_t sel_cp(const Terminal *t, SelPoint p)
{
    const TermRow *r = sel_row(t, p.line);
    if (!r || p.col < 0 || p.col >= t->cols) return 0;
    return r->cells[p.col].codepoint;
}

/* Step to the previous cell of the same logical line, across a soft wrap. */
static bool sel_prev_cell(const Terminal *t, SelPoint *p)
{
    if (p->col > 0) { p->col--; return true; }
    const TermRow *r = sel_row(t, p->line);
    if (!r || !r->wrapped || p->line == 0 || !sel_row(t, p->line - 1)) return false;
    p->line--;
    p->col = t->cols - 1;
    return true;
}

/* Step to the next cell of the same logical line, across a soft wrap. */
static bool sel_next_cell(const Terminal *t, SelPoint *p)
{
    if (p->col < t->cols - 1) { p->col++; return true; }
    const TermRow *n = sel_row(t, p->line + 1);
    if (!n || !n->wrapped) return false;
    p->line++;
    p->col = 0;
    return true;
}

/* The maximal run of cells of p's class, following soft wraps. */
static void sel_word_bounds(const Terminal *t, SelPoint p, SelPoint *first, SelPoint *last)
{
    int cls = selection_char_class(sel_cp(t, p));
    SelPoint a = p, b = p, q;
    for (;;) {
        q = a;
        if (!sel_prev_cell(t, &q) || selection_char_class(sel_cp(t, q)) != cls) break;
        a = q;
    }
    for (;;) {
        q = b;
        if (!sel_next_cell(t, &q) || selection_char_class(sel_cp(t, q)) != cls) break;
        b = q;
    }
    *first = a;
    *last = b;
}

/* The whole logical line through `line`: back to the first row that is not a
 * continuation (bounded by the oldest row held), forward over every row whose
 * wrapped flag is set. */
static void sel_line_bounds(const Terminal *t, uint64_t line, SelPoint *first, SelPoint *last)
{
    uint64_t a = line, b = line;
    for (;;) {
        const TermRow *r = sel_row(t, a);
        if (!r || !r->wrapped || a <= t->lines_recycled) break;
        a--;
    }
    for (;;) {
        const TermRow *n = sel_row(t, b + 1);
        if (!n || !n->wrapped) break;
        b++;
    }
    first->line = a;
    first->col = 0;
    last->line = b;
    last->col = t->cols - 1;
}

/* The unit under p for the selection's mode: a cell, a word or a line. */
static void sel_unit(const Selection *s, const Terminal *t, SelPoint p,
                     SelPoint *first, SelPoint *last)
{
    if (s->mode == SEL_WORD) sel_word_bounds(t, p, first, last);
    else if (s->mode == SEL_LINE) sel_line_bounds(t, p.line, first, last);
    else { *first = p; *last = p; }
}
```

Replace `selection_begin()` and `selection_extend()` with:

```c
void selection_begin(Selection *s, const Terminal *t, int screen_row, int col,
                     int click_count, int px, int py)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (!t) return;
    s->mode = (click_count >= 3) ? SEL_LINE : (click_count == 2 ? SEL_WORD : SEL_STREAM);
    sel_unit(s, t, sel_point_at(t, screen_row, col), &s->anchor, &s->anchor_end);
    s->start = s->anchor;
    s->end = s->anchor_end;
    /* A press alone selects nothing; a double or triple click selects its
     * word or line at once (PuTTY highlights it on the press). */
    s->valid = (s->mode != SEL_STREAM);
    s->epoch = t->epoch;
    s->gen = t->write_gen;
    s->active = true;
    s->press_x = s->last_x = px;
    s->press_y = s->last_y = py;
}

void selection_extend(Selection *s, const Terminal *t, int screen_row, int col,
                      int px, int py, int drag_cx, int drag_cy)
{
    if (!s || !t || !s->active) return;
    s->last_x = px;
    s->last_y = py;
    if (s->epoch != t->epoch) return;              /* the window ends the drag */
    if (s->mode == SEL_STREAM && !s->valid &&
        abs(px - s->press_x) < drag_cx && abs(py - s->press_y) < drag_cy)
        return;                                    /* still a click */
    SelPoint first, last;
    sel_unit(s, t, sel_point_at(t, screen_row, col), &first, &last);
    /* Whole units in either direction: from the earlier of the two units'
     * starts to the later of their ends. */
    s->start = (sel_cmp(first, s->anchor) < 0) ? first : s->anchor;
    s->end = (sel_cmp(last, s->anchor_end) > 0) ? last : s->anchor_end;
    s->valid = true;
    s->gen = t->write_gen;
}
```

Add at the end of `selection.c`:

```c
int selection_char_class(uint32_t cp)
{
    if (cell_text_codepoint(cp) == (uint32_t)' ') return 0;
    if ((cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
        cp == '_' || cp == '-' || cp == '.' || cp == '/' || cp == '~' || cp == ':' || cp == '@')
        return 1;
    if ((cp >= 0xC0u && cp <= 0x2AFu && cp != 0xD7u && cp != 0xF7u) ||
        (cp >= 0x370u && cp <= 0x52Fu))
        return 1;
    return 2 + (int)cp;
}
```

- [ ] **Step 4: Run the tests and the product-flags check**

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`.

Run: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/core/selection.c`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add src/core/selection.h src/core/selection.c tests/test_selection.c tests/runner.c
git commit -m "selection: double-click words and triple-click lines across soft wraps"
```
Commit with the repo's Models line and trailer.

---

### Task 6: Staleness, recycling and `selection_sync`

**Files:**
- Modify: `src/core/selection.h` (declare `selection_sync`)
- Modify: `src/core/selection.c` (add stamp helpers after `sel_unit`; replace `selection_extend`, `selection_contains`, `selection_copy_text`; add `selection_sync`)
- Test: `tests/test_selection.c`, `tests/runner.c`

**Interfaces:**
- Consumes: Task 2's `TermRow.gen`, `Terminal.write_gen`, `epoch`, `lines_recycled`; Task 5's `sel_unit`.
- Produces:
  - `bool selection_sync(Selection *s, const Terminal *t);` trims the four points to `lines_recycled`. It clears the selection, returning true, when the epoch changed (this also ends an active drag), when every selected line has been recycled, or, after the release, when a selected row's stamp is newer than the selection's. When nothing in the selection was written, it moves the selection's stamp up to `write_gen`, so the next call is two compares. It returns true on any change.
  - `selection_contains` returns false, after the release, for a row written since.
  - `selection_copy_text` returns `""` for a stale selection. The const API cannot clear it, so the window calls `selection_sync` before every copy (Task 7).
  - `selection_extend` trims first, so a recycled anchor cannot come back.

- [ ] **Step 1: Write the failing tests** (append to `tests/test_selection.c`)

```c
/* ---- staleness and recycling (spec section 2.3) ------------------------- */

int test_sel_write_after_finish_makes_row_stale(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "one\r\ntwo\r\nthree");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    feed(t, "\x1b[2;1HX");                        /* write into "two" */
    ASSERT_TRUE(selection_contains(&s, t, 0, 0)); /* untouched row: still highlighted */
    ASSERT_FALSE(selection_contains(&s, t, 1, 1));/* written row: not */
    size_t n = 99;
    char *txt = selection_copy_text(&s, t, &n);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(txt, "");
    free(txt);
    ASSERT_TRUE(selection_sync(&s, t));           /* the clear */
    ASSERT_FALSE(s.valid);
    term_free(t);
    TEST_END();
}

int test_sel_write_during_drag_does_not_clear(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "one\r\ntwo\r\nthree");
    Selection s;
    selection_begin(&s, t, 0, 0, 1, 0, 0);
    selection_extend(&s, t, 1, 9, 100, 100, 4, 4);
    feed(t, "\x1b[2;1HX");                        /* streaming output under the drag */
    ASSERT_FALSE(selection_sync(&s, t));
    ASSERT_TRUE(s.valid);
    ASSERT_TRUE(selection_contains(&s, t, 1, 0));
    ASSERT_TRUE(selection_finish(&s, t));         /* takes the stamps */
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "one\r\nXwo");             /* what is on screen at release */
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_epoch_change_clears_and_ends_drag(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "one\r\ntwo");
    Selection s;
    selection_begin(&s, t, 0, 0, 1, 0, 0);
    selection_extend(&s, t, 1, 2, 100, 100, 4, 4);
    term_resize(t, 6, 10);                        /* reflow: a new epoch */
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_FALSE(s.active);
    ASSERT_FALSE(s.valid);

    ASSERT_TRUE(drag(&s, t, 0, 0, 0, 2));
    term_alt_screen_enter(t);                     /* vim, less: a new epoch */
    ASSERT_FALSE(selection_contains(&s, t, 0, 0));
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_FALSE(s.valid);
    term_free(t);
    TEST_END();
}

/* The row at the recycle point is trimmed away, not treated as touched: the
 * recycled TermRow is stamped, but under its new, newest line number. */
int test_sel_recycle_trims_start_without_touching(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 2);            /* capacity 6 */
    feed_lines(t, 0, 6);                          /* full: L00..L05 */
    t->scrollback_offset = 2;                     /* view L00..L03 */
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 2, 9));         /* L00..L02 */
    feed(t, "\r\nL06");                           /* recycles L00 */
    ASSERT_EQ(t->lines_recycled, (uint64_t)1);
    ASSERT_TRUE(selection_sync(&s, t));           /* trimmed */
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.start.line, (uint64_t)1);
    ASSERT_EQ(s.start.col, 0);
    ASSERT_TRUE(s.anchor.line >= t->lines_recycled);
    ASSERT_TRUE(selection_contains(&s, t, 0, 0)); /* view row 0 is L01 now */
    char *txt = copy_str(&s, t);
    ASSERT_STR_EQ(txt, "L01\r\nL02");
    free(txt);
    term_free(t);
    TEST_END();
}

int test_sel_recycled_anchor_cannot_return(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 2);
    feed_lines(t, 0, 6);
    t->scrollback_offset = 2;
    Selection s;
    selection_begin(&s, t, 0, 0, 1, 0, 0);        /* anchor on L00 */
    selection_extend(&s, t, 2, 9, 100, 100, 4, 4);
    feed(t, "\r\nL06");                           /* L00 recycled mid-drag */
    selection_extend(&s, t, 1, 0, 100, 120, 4, 4);/* no sync in between */
    ASSERT_TRUE(s.anchor.line >= t->lines_recycled);
    ASSERT_TRUE(s.start.line >= t->lines_recycled);
    term_free(t);
    TEST_END();
}

int test_sel_wholly_recycled_clears(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 2);
    feed_lines(t, 0, 6);
    t->scrollback_offset = 2;
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));         /* L00..L01 */
    feed(t, "\r\nL06\r\nL07");                    /* both recycled */
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_FALSE(s.valid);
    term_free(t);
    TEST_END();
}

int test_sel_sync_ignores_writes_elsewhere(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    feed(t, "one\r\ntwo");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    ASSERT_FALSE(selection_sync(&s, t));          /* nothing written */
    feed(t, "\x1b[4;1HZ");                        /* a row outside the selection */
    ASSERT_FALSE(selection_sync(&s, t));
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.gen, t->write_gen);               /* stamp moved up: next sync is cheap */
    term_free(t);
    TEST_END();
}

/* Review Focus 3: write_gen is 32 bits and wraps; the stamp compare must
 * not mistake an old row for a new one across the wrap, or the reverse. */
int test_sel_stamps_survive_counter_wrap(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 10);
    t->write_gen = 0xFFFFFFF0u;
    feed(t, "one\r\ntwo");
    Selection s;
    ASSERT_TRUE(drag(&s, t, 0, 0, 1, 9));
    for (int i = 0; i < 40; i++) feed(t, "\x1b[4;1HZ");  /* elsewhere; the counter wraps */
    ASSERT_TRUE(t->write_gen < 0x100u);
    ASSERT_FALSE(selection_sync(&s, t));
    ASSERT_TRUE(s.valid);
    ASSERT_TRUE(selection_contains(&s, t, 0, 0));
    feed(t, "\x1b[1;1HX");                        /* into the selection */
    ASSERT_FALSE(selection_contains(&s, t, 0, 0));
    ASSERT_TRUE(selection_sync(&s, t));
    ASSERT_FALSE(s.valid);
    term_free(t);
    TEST_END();
}
```

Register the tests in `tests/runner.c`. Add the declarations after `int test_sel_line_drag_extends_by_lines(void);`:

```c
int test_sel_write_after_finish_makes_row_stale(void);
int test_sel_write_during_drag_does_not_clear(void);
int test_sel_epoch_change_clears_and_ends_drag(void);
int test_sel_recycle_trims_start_without_touching(void);
int test_sel_recycled_anchor_cannot_return(void);
int test_sel_wholly_recycled_clears(void);
int test_sel_sync_ignores_writes_elsewhere(void);
int test_sel_stamps_survive_counter_wrap(void);
```

Add the calls after `failed += test_sel_line_drag_extends_by_lines();`:

```c
    failed += test_sel_write_after_finish_makes_row_stale();
    failed += test_sel_write_during_drag_does_not_clear();
    failed += test_sel_epoch_change_clears_and_ends_drag();
    failed += test_sel_recycle_trims_start_without_touching();
    failed += test_sel_recycled_anchor_cannot_return();
    failed += test_sel_wholly_recycled_clears();
    failed += test_sel_sync_ignores_writes_elsewhere();
    failed += test_sel_stamps_survive_counter_wrap();
```

- [ ] **Step 2: Run to verify they fail**

Run: `make test 2>&1 | grep -E "undefined reference|^\[FAIL\] test_sel_" | head -20`
Expected: the build fails with an undefined reference to `selection_sync` (gcc 13 first warns about an implicit declaration).

- [ ] **Step 3: Implement**

In `src/core/selection.h`, add after the `selection_select_all` declaration:

```c
/* Bring a selection up to date with the terminal (spec section 2.3). Trims
 * the four points below lines_recycled; clears the selection when the epoch
 * changed (ending an active drag), when every selected line is recycled, or
 * -- after the release only -- when a selected row was written since. The
 * window calls it every timer tick and before every copy. Returns true on
 * any change. */
bool selection_sync(Selection *s, const Terminal *t);
```

In `src/core/selection.c`, insert after `sel_unit()`:

```c
/* ---- write stamps (spec section 2.3) ------------------------------------ */

/* Wrap-safe: write_gen is a 32-bit counter that wraps after 2^32 writes. */
static bool sel_gen_newer(uint32_t row_gen, uint32_t sel_gen)
{
    return (int32_t)(row_gen - sel_gen) > 0;
}

/* Was any selected row written after the selection's stamp? */
static bool sel_rows_newer(const Selection *s, const Terminal *t)
{
    if (t->write_gen == s->gen) return false;                 /* nothing written since */
    uint64_t ln = (s->start.line < t->lines_recycled) ? t->lines_recycled : s->start.line;
    for (; ln <= s->end.line; ln++) {
        const TermRow *r = sel_row(t, ln);
        if (!r) break;
        if (sel_gen_newer(r->gen, s->gen)) return true;
    }
    return false;
}

/* Cut the four points to the oldest line held; true when any moved. */
static bool sel_trim(Selection *s, const Terminal *t)
{
    SelPoint *pts[4] = { &s->anchor, &s->anchor_end, &s->start, &s->end };
    bool moved = false;
    for (int i = 0; i < 4; i++) {
        if (pts[i]->line < t->lines_recycled) {
            pts[i]->line = t->lines_recycled;
            pts[i]->col = 0;
            moved = true;
        }
    }
    return moved;
}
```

Replace `selection_extend()` with the final version:

```c
void selection_extend(Selection *s, const Terminal *t, int screen_row, int col,
                      int px, int py, int drag_cx, int drag_cy)
{
    if (!s || !t || !s->active) return;
    s->last_x = px;
    s->last_y = py;
    if (s->epoch != t->epoch) return;              /* selection_sync() ends the drag */
    (void)sel_trim(s, t);                          /* a recycled anchor cannot come back */
    if (s->mode == SEL_STREAM && !s->valid &&
        abs(px - s->press_x) < drag_cx && abs(py - s->press_y) < drag_cy)
        return;                                    /* still a click */
    SelPoint first, last;
    sel_unit(s, t, sel_point_at(t, screen_row, col), &first, &last);
    s->start = (sel_cmp(first, s->anchor) < 0) ? first : s->anchor;
    s->end = (sel_cmp(last, s->anchor_end) > 0) ? last : s->anchor_end;
    s->valid = true;
    s->gen = t->write_gen;
}
```

Replace `selection_contains()` with:

```c
bool selection_contains(const Selection *s, const Terminal *t, int screen_row, int col)
{
    if (!s || !t || !s->valid || s->epoch != t->epoch) return false;
    int logical = term_view_to_logical(t, screen_row);
    if (logical < 0) return false;
    uint64_t line = t->lines_recycled + (uint64_t)logical;
    if (line < s->start.line || line > s->end.line) return false;
    if (line == s->start.line && col < s->start.col) return false;
    if (line == s->end.line && col > s->end.col) return false;
    if (!s->active) {
        /* Written since the release: no longer what was selected, so the
         * highlight goes row by row as the output overwrites it. */
        const TermRow *r = sel_row(t, line);
        if (!r || sel_gen_newer(r->gen, s->gen)) return false;
    }
    return true;
}
```

In `selection_copy_text()`, directly after the line
`    if (!s || !t || !s->valid || s->epoch != t->epoch) return b.p;`
add:

```c
    /* Stale: copy nothing. The const API cannot clear it; selection_sync(),
     * which the window calls before every copy, does. */
    if (!s->active && sel_rows_newer(s, t)) return b.p;
```

Add after `selection_select_all()`:

```c
bool selection_sync(Selection *s, const Terminal *t)
{
    if (!s || !t || (!s->valid && !s->active)) return false;
    if (s->epoch != t->epoch || s->end.line < t->lines_recycled) {
        selection_clear(s);              /* renumbered or replaced, or all recycled */
        return true;
    }
    bool changed = sel_trim(s, t);
    if (!s->active) {
        if (sel_rows_newer(s, t)) {
            selection_clear(s);
            return true;
        }
        /* No selected row is newer than the stamp, so none is newer than
         * now: compare later writes against now, and the next call with
         * nothing written is two compares. */
        s->gen = t->write_gen;
    }
    return changed;
}
```

- [ ] **Step 4: Run the tests and the product-flags check**

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`.

Run: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/core/selection.c`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add src/core/selection.h src/core/selection.c tests/test_selection.c tests/runner.c
git commit -m "selection: write-stamp staleness, recycle trimming and selection_sync"
```
Commit with the repo's Models line and trailer.

---

### Task 7: Renderer and window wiring, and the Ctrl+C documentation

`src/ui` is not in the native build (`NON_TEST_SRCS`), so this task has no native test. Task 10's harness cases test it. The task's checks are the product-flags compile of `src/ui` and a green `make test`. Spec section 2.9 requires the Ctrl+C documentation to change in the same commit as the handler, so it is part of this task.

**Files:**
- Modify: `src/ui/renderer.c:89-150` (`get_visible_row`, `cell_in_selection`, `resolve_cell`) and `:177` (`has_sel`)
- Modify: `src/ui/window.c`: includes; the `Session` struct (`:106-149`); globals and helpers (`:601-608`); `create_session` (`:610`); `free_session` (`:697`); `on_tab_select` (`:730`); `do_copy` and `copy_selection_and_clear` (`:2730-2760`); Ctrl+C (`:3394-3403`); Page Up (`:3437`); `IDM_EDIT_SELECT_ALL` (`:4000`); `WM_TIMER` (`:4402`); `WM_PAINT` (`:4889`); `WM_MOUSEWHEEL` (`:5129`); the mouse handlers (`:5167-5279`); `WM_VSCROLL` (`:5287`); the window class (`:5379`); the `WM_CHAR` swallow comment (`:3593`)
- Modify: `README.md:121`, `README.md:355`, `src/ui/help_guide.c:99-102`, `src/ui/help_guide.c:354`

**Interfaces:**
- Consumes: everything from Tasks 1-6: `term_view_to_logical`, `term_max_scroll_offset`, `Selection`, `selection_pixel_to_cell`, `selection_autoscroll_step`, `selection_begin`, `selection_extend`, `selection_finish`, `selection_select_all`, `selection_sync`, `selection_contains`, `selection_in_view`, `selection_copy_text`; `clip_copy_utf8(HWND, const char *, size_t)` (`src/ui/clipboard.h`).
- Produces: `Session.sel`; `static Session *g_drag_session`; the debug events `sel.begin`, `sel.scroll`, `sel.copy` and `sel.clear` that Task 10 waits on; the window class with `CS_DBLCLKS`.

- [ ] **Step 1: Renderer.** In `src/ui/renderer.c`, replace `get_visible_row()` (its comment and body) and delete `cell_in_selection()` entirely:

```c
/* The TermRow shown at screen_row in the view as scrolled. It uses
 * term_view_to_logical() (term.h), the mapping the selection uses too, so
 * the highlight and the copy name the same rows (spec 2026-10-20 section
 * 2.8). */
static TermRow *get_visible_row(Terminal *term, int screen_row) {
    int logical_idx = term_view_to_logical(term, screen_row);
    if (logical_idx < 0 || logical_idx >= term->lines_capacity) return NULL;
    return term->lines[(term->lines_start + logical_idx) % term->lines_capacity];
}
```

In `resolve_cell()`, replace

```c
    if (cell_in_selection(sel, row_idx, col_idx))
        draw_flags |= TERM_DRAW_SELECTED;
```

with

```c
    if (sel && selection_contains(sel, term, row_idx, col_idx))
        draw_flags |= TERM_DRAW_SELECTED;
```

In `renderer_draw()`, replace `    bool has_sel = sel && sel->valid;` with:

```c
    /* A selection in view disables the clean-row skip below, so its
     * highlight is painted on rows nothing else dirtied. */
    bool has_sel = sel && selection_in_view(sel, term);
```

- [ ] **Step 2: Window state and helpers.** In `src/ui/window.c`, add `#include <stdlib.h>` after `#include <string.h>`. Then, in `typedef struct Session`, add before `struct Session *next;`:

```c
    /* The terminal selection, PuTTY style (spec
     * docs/superpowers/specs/2026-10-20-putty-selection-design.md): per
     * session, buffer-absolute, zeroed in create_session(). */
    Selection sel;
```

Replace

```c
static PasteState g_paste = {0};
static Selection g_selection = {0};

/* Set in WM_KEYDOWN when Ctrl+C (or Ctrl+Shift+C) copies a selection, so the
 * WM_CHAR that TranslateMessage still queues for 0x03 can be swallowed
 * instead of being sent to the shell as SIGINT. Same pattern as Ctrl+V's
 * 0x16, which WM_CHAR always swallows because WM_KEYDOWN always consumes it. */
static bool g_ctrlc_swallow_char = false;
```

with

```c
static PasteState g_paste = {0};

/* The session whose terminal drag holds the mouse capture, or NULL. A tab
 * switch or close mid-drag ends that drag and releases the capture through
 * it (spec section 2.5). */
static Session *g_drag_session = NULL;

/* The last WM_LBUTTONDBLCLK in the terminal, so that the press after it can
 * be the triple-click (spec section 1). */
static DWORD g_dblclk_tick = 0;
static int   g_dblclk_x = 0, g_dblclk_y = 0;
static bool  g_dblclk_armed = false;

/* Set in WM_KEYDOWN when Ctrl+Shift+C copies, so the WM_CHAR that
 * TranslateMessage still queues for 0x03 is swallowed instead of being sent
 * to the shell as SIGINT. Plain Ctrl+C never sets it: it always interrupts
 * (spec section 6.1). Same pattern as Ctrl+V's 0x16. */
static bool g_ctrlc_swallow_char = false;

/* ---- Terminal selection (spec 2026-10-20-putty-selection-design.md 2.7) ---- */

/* End the drag that holds the capture. Forget it first, so the
 * WM_CAPTURECHANGED that ReleaseCapture() sends synchronously finds no drag;
 * then release only if this window still holds the capture. */
static void end_terminal_drag(HWND hwnd)
{
    g_drag_session = NULL;
    if (hwnd && GetCapture() == hwnd) ReleaseCapture();
}

/* selection_sync() for one session, plus the window's side of it: say why a
 * selection was cleared, end a drag the terminal ended (an epoch change),
 * and repaint when a highlight went away. Returns true when it cleared. */
static bool sync_selection(HWND hwnd, Session *s)
{
    if (!s || !s->term || (!s->sel.valid && !s->sel.active)) return false;
    const Terminal *t = s->term;
    bool epoch_changed = s->sel.epoch != t->epoch;
    bool recycled = !epoch_changed && s->sel.end.line < t->lines_recycled;
    bool was_active = s->sel.active;
    if (!selection_sync(&s->sel, t)) return false;
    if (s->sel.valid || s->sel.active) return false;      /* only trimmed */
    DLOG(DLOG_GENERAL, DLOG_DEBUG, s->log_id, "sel.clear", "reason=%s",
         epoch_changed ? "epoch" : (recycled ? "recycled" : "stale"));
    if (was_active && g_drag_session == s) end_terminal_drag(hwnd);
    if (s == g_active_session) force_full_terminal_repaint(hwnd, s->term);
    return true;
}

/* Copy a session's terminal selection to the clipboard. It never touches
 * the clipboard for an empty copy (a click, or a selection of blanks), and
 * never copies a selection the output has since written over. */
static bool copy_terminal_selection(HWND hwnd, Session *s)
{
    if (!s || !s->term) return false;
    (void)sync_selection(hwnd, s);
    if (!s->sel.valid) return false;
    size_t n = 0;
    char *text = selection_copy_text(&s->sel, s->term, &n);
    bool ok = n > 0 && clip_copy_utf8(hwnd, text, n) != 0;
    if (ok) {
        int lines = 1;
        for (size_t i = 0; i + 1 < n; i++)
            if (text[i] == '\r' && text[i + 1] == '\n') lines++;
        DLOG(DLOG_GENERAL, DLOG_DEBUG, s->log_id, "sel.copy", "lines=%d bytes=%zu",
             lines, n);
    }
    free(text);
    return ok;
}

static bool sel_point_same(SelPoint a, SelPoint b)
{
    return a.line == b.line && a.col == b.col;
}

/* The press in the terminal: clear what was selected, take the capture and
 * start a selection of `clicks` (1 cell, 2 word, 3 line) at client pixel
 * (px, py). px has the left margin already taken off. */
static void start_terminal_drag(HWND hwnd, Session *s, int px, int py, int clicks)
{
    if (s->sel.valid || s->sel.active)
        DLOG(DLOG_GENERAL, DLOG_DEBUG, s->log_id, "sel.clear", "reason=%s", "click");
    if (g_drag_session && g_drag_session != s && g_drag_session->term)
        (void)selection_finish(&g_drag_session->sel, g_drag_session->term);
    g_drag_session = NULL;          /* a WM_CAPTURECHANGED from SetCapture finds no drag */
    SetCapture(hwnd);
    g_drag_session = s;
    int row = 0, col = 0;
    selection_pixel_to_cell(px, py, g_renderer.charWidth, g_renderer.charHeight,
                            g_tab_height, s->term->rows, s->term->cols, &row, &col);
    selection_begin(&s->sel, s->term, row, col, clicks, px, py);
    DLOG(DLOG_GENERAL, DLOG_DEBUG, s->log_id, "sel.begin", "line=%llu col=%d mode=%d",
         (unsigned long long)s->sel.anchor.line, s->sel.anchor.col, (int)s->sel.mode);
    force_full_terminal_repaint(hwnd, s->term);
}

/* Extend the drag to client pixel (px, py), px less the left margin, and
 * repaint when the selection changed. */
static void extend_terminal_drag(HWND hwnd, Session *s, int px, int py)
{
    int row = 0, col = 0;
    selection_pixel_to_cell(px, py, g_renderer.charWidth, g_renderer.charHeight,
                            g_tab_height, s->term->rows, s->term->cols, &row, &col);
    bool was_valid = s->sel.valid;
    SelPoint old_start = s->sel.start, old_end = s->sel.end;
    selection_extend(&s->sel, s->term, row, col, px, py,
                     GetSystemMetrics(SM_CXDRAG), GetSystemMetrics(SM_CYDRAG));
    if (s->sel.valid != was_valid || !sel_point_same(old_start, s->sel.start) ||
        !sel_point_same(old_end, s->sel.end))
        force_full_terminal_repaint(hwnd, s->term);
}

/* 1 for a press; 3 for the press that follows a double-click within the
 * double-click time and rectangle. GetTickCount() rather than
 * GetMessageTime(): the harness sends its clicks, and a sent message has no
 * message time of its own (spec section 5). */
static int terminal_press_clicks(int x, int y)
{
    int clicks = 1;
    if (g_dblclk_armed &&
        GetTickCount() - g_dblclk_tick <= (DWORD)GetDoubleClickTime() &&
        abs(x - g_dblclk_x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
        abs(y - g_dblclk_y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2)
        clicks = 3;
    g_dblclk_armed = false;
    return clicks;
}

/* Timer 1's selection work: every tick, for every session, outside the poll
 * loop's io.ctx/CONN_IDLE gate, so a closed session's history still selects.
 * It scrolls the view towards a drag whose stored pointer is past an edge
 * and follows with the selection end. It also clears any selection the
 * terminal has made stale. */
static void selection_tick(HWND hwnd)
{
    Session *d = g_drag_session;
    if (d && d->term && d->sel.active && d == g_active_session) {
        Terminal *t = d->term;
        int top = g_tab_height;
        int bottom = g_tab_height + t->rows * g_renderer.charHeight;
        int step = selection_autoscroll_step(d->sel.last_y, top, bottom,
                                             g_renderer.charHeight);
        if (step != 0) {
            int off = t->scrollback_offset - step;   /* above the top: back into history */
            int max_off = term_max_scroll_offset(t);
            if (off > max_off) off = max_off;
            if (off < 0) off = 0;
            if (off != t->scrollback_offset) {
                t->scrollback_offset = off;
                DLOG(DLOG_GENERAL, DLOG_DEBUG, d->log_id, "sel.scroll", "offset=%d", off);
                update_scrollbar(hwnd);
                force_full_terminal_repaint(hwnd, t);
            }
            extend_terminal_drag(hwnd, d, d->sel.last_x, d->sel.last_y);
        }
    }
    for (Session *s = g_session_list; s; s = s->next)
        (void)sync_selection(hwnd, s);
}
```

- [ ] **Step 3: Session lifecycle.** In `create_session()`, after `memset(&s->io, 0, sizeof(s->io));`, add:

```c
    memset(&s->sel, 0, sizeof(s->sel));   /* xmalloc() does not zero */
```

In `free_session()`, directly after `key_oneshot_clear();`, add:

```c
        /* A drag in this session ends with it, and gives the capture back. */
        if (g_drag_session == s) {
            g_drag_session = NULL;
            if (g_hwndTabs) end_terminal_drag(GetParent(g_hwndTabs));
        }
        if (s->sel.valid || s->sel.active)
            DLOG(DLOG_GENERAL, DLOG_DEBUG, s->log_id, "sel.clear", "reason=%s", "close");
```

In `on_tab_select()`, directly after `key_oneshot_clear();`, add:

```c
    /* A drag belongs to the tab it started in: switching away ends it where
     * it stands, uncopied, and releases the capture. */
    if (g_drag_session && g_drag_session != (Session *)user_data) {
        if (g_drag_session->term)
            (void)selection_finish(&g_drag_session->sel, g_drag_session->term);
        end_terminal_drag(GetParent(g_hwndTabs));
    }
```

- [ ] **Step 4: Copy paths, Ctrl+C, Page Up, Select All.** Replace `do_copy()` and `copy_selection_and_clear()` (from `/* Copy current selection to clipboard */` to the closing brace of `copy_selection_and_clear`) with:

```c
/* Copy the current selection to the clipboard: the AI panel's when its
 * message list has focus and a selection, else the active terminal's
 * (Edit > Copy, Ctrl+Shift+C). The terminal highlight stays. */
static void do_copy(HWND hwnd)
{
    if (g_hwndAiChat && ai_chat_copy_selection(g_hwndAiChat)) return;
    if (!g_active_session) return;
    (void)copy_terminal_selection(hwnd, g_active_session);
}
```

Replace the Ctrl+C block

```c
    /* Ctrl+C -- copy selection if one exists, otherwise fall through so
     * WM_CHAR sends 0x03 (SIGINT) as before. Ctrl+Shift+C always copies
     * (no-op if nothing selected) and never reaches the shell. */
    if (ctrl_only && vk == (WPARAM)'C') {
        if (shift || g_selection.valid) {
            copy_selection_and_clear(hwnd);
            g_ctrlc_swallow_char = true;
            return true;
        }
    }
```

with

```c
    /* Ctrl+Shift+C copies the selection (nothing if none) and never reaches
     * the shell. Plain Ctrl+C always falls through, so WM_CHAR sends 0x03
     * (SIGINT), selection or not, as in PuTTY (spec section 6.1). */
    if (ctrl_only && shift && vk == (WPARAM)'C') {
        do_copy(hwnd);
        g_ctrlc_swallow_char = true;
        return true;
    }
```

In `WM_CHAR`, change the swallow comment `/* Ctrl+C copied a selection in WM_KEYDOWN -- don't send SIGINT */` to `/* Ctrl+Shift+C copied in WM_KEYDOWN -- don't send SIGINT */`.

In the Page Up branch, replace

```c
                t->scrollback_offset = scroll_page_up(t->scrollback_offset,
                                                      t->rows,
                                                      t->max_scrollback);
```

with

```c
                t->scrollback_offset = scroll_page_up(t->scrollback_offset,
                                                      t->rows,
                                                      term_max_scroll_offset(t));
```

Replace the `IDM_EDIT_SELECT_ALL` case body with:

```c
                case IDM_EDIT_SELECT_ALL:
                    if (g_active_session && g_active_session->term) {
                        Session *as = g_active_session;
                        if (g_drag_session == as) end_terminal_drag(hwnd);
                        if (as->sel.valid || as->sel.active)
                            DLOG(DLOG_GENERAL, DLOG_DEBUG, as->log_id, "sel.clear",
                                 "reason=%s", "click");
                        selection_select_all(&as->sel, as->term);
                        force_full_terminal_repaint(hwnd, as->term);
                        (void)copy_terminal_selection(hwnd, as);
                    }
                    return 0;
```

- [ ] **Step 5: Timer, paint, wheel, scrollbar, class.** In `WM_TIMER`, replace

```c
                    s = s->next;
                }
            } else if (wParam == 2) {
```

with

```c
                    s = s->next;
                }
                /* Outside the per-session io.ctx/CONN_IDLE gate above. */
                selection_tick(hwnd);
            } else if (wParam == 2) {
```

In `WM_PAINT`, replace `&ps.rcPaint, &g_selection);` with `&ps.rcPaint, &g_active_session->sel);`.

In `WM_MOUSEWHEEL`, replace

```c
                int max_off = (t->lines_count > t->rows)
                                  ? (t->lines_count - t->rows) : 0;
                if (max_off > t->max_scrollback) max_off = t->max_scrollback;
```

with `                int max_off = term_max_scroll_offset(t);`

In `WM_VSCROLL`, replace

```c
            int max_off = (t->lines_count > t->rows) ? (t->lines_count - t->rows) : 0;
            if (max_off > t->max_scrollback) max_off = t->max_scrollback;
```

with `            int max_off = term_max_scroll_offset(t);`

In `ui_init()`, replace `    wc.style = CS_HREDRAW | CS_VREDRAW;` with `    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;`.

- [ ] **Step 6: Mouse handlers.** Replace everything from `        case WM_LBUTTONDOWN: {` through the closing `        }` of `case WM_LBUTTONUP:` (the block that ends with the dirty-row loop and `invalidate_terminal(hwnd); return 0; }`) with:

```c
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
            /* Splitter drag first. CS_DBLCLKS turns a quick second press
             * into WM_LBUTTONDBLCLK, so the double-click repeats the test. */
            if (g_ai_docked && g_ai_panel_width > 0 && g_hwndAiChat) {
                RECT crc;
                GetClientRect(hwnd, &crc);
                int splitter_x = crc.right - g_ai_panel_width;
                if (ai_dock_splitter_hit(mx, splitter_x,
                                          AI_DOCK_SPLITTER_W,
                                          AI_DOCK_SPLITTER_PAD,
                                          my, g_tab_height)) {
                    g_ai_splitter_dragging = 1;
                    SetCapture(hwnd);
                    return 0;
                }
            }
            if (!g_active_session || !g_active_session->term) break;
            SetFocus(hwnd);  /* reclaim keyboard focus from AI panel */
            int clicks;
            if (msg == WM_LBUTTONDBLCLK) {
                g_dblclk_tick = GetTickCount();
                g_dblclk_x = mx;
                g_dblclk_y = my;
                g_dblclk_armed = true;
                clicks = 2;
            } else {
                clicks = terminal_press_clicks(mx, my);
            }
            start_terminal_drag(hwnd, g_active_session, mx - g_left_margin, my, clicks);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_ai_splitter_dragging) {
                RECT crc;
                GetClientRect(hwnd, &crc);
                int mx = GET_X_LPARAM(lParam);
                int new_w = crc.right - mx;
                new_w = ai_dock_clamp_width(new_w, crc.right,
                                             AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
                g_ai_panel_width = new_w;
                g_ai_last_width = new_w;
                relayout_main(hwnd);
                /* Force synchronous repaint so no ghosting between frames */
                RedrawWindow(hwnd, NULL, NULL,
                             RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                return 0;
            }
            Session *ds = g_drag_session;
            if (!ds || !ds->term || !ds->sel.active) break;
            /* A move without the button (the one Windows synthesises when
             * capture changes, or a stray) is not a drag point. */
            if (!(wParam & MK_LBUTTON)) break;
            extend_terminal_drag(hwnd, ds, GET_X_LPARAM(lParam) - g_left_margin,
                                 GET_Y_LPARAM(lParam));
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_ai_splitter_dragging) {
                g_ai_splitter_dragging = 0;
                ReleaseCapture();
                return 0;
            }
            Session *ds = g_drag_session;
            if (!ds) break;
            if (ds->term && ds->sel.active) {
                /* The release point is the end: WM_MOUSEMOVE may not have
                 * reported it. */
                extend_terminal_drag(hwnd, ds, GET_X_LPARAM(lParam) - g_left_margin,
                                     GET_Y_LPARAM(lParam));
                if (selection_finish(&ds->sel, ds->term))
                    (void)copy_terminal_selection(hwnd, ds);
                force_full_terminal_repaint(hwnd, ds->term);
            }
            /* Copy first, then release: ReleaseCapture() sends
             * WM_CAPTURECHANGED synchronously (spec section 2.7). */
            end_terminal_drag(hwnd);
            return 0;
        }

        case WM_CAPTURECHANGED: {
            /* Capture taken away mid-drag (a dialog, Alt+Tab, another
             * SetCapture): end the drag where it stands, without copying.
             * With no drag (our own end_terminal_drag(), the splitter) it is
             * ignored. */
            Session *ds = g_drag_session;
            if (ds) {
                g_drag_session = NULL;
                if (ds->term) {
                    (void)selection_finish(&ds->sel, ds->term);
                    force_full_terminal_repaint(hwnd, ds->term);
                }
            }
            break;
        }
```

- [ ] **Step 7: The Ctrl+C documentation (spec 2.9, same commit).** In `README.md`, replace the line

```
- **Ctrl+C** copies the current selection to the clipboard and clears it; with no selection, Ctrl+C sends SIGINT to the remote shell as usual. **Ctrl+Shift+C** always copies (never sent to the shell)
```

with

```
- **Releasing the mouse button** copies the selection to the clipboard, and the highlight stays; **Ctrl+Shift+C** or **Edit > Copy** copies it again (never sent to the shell). **Ctrl+C** always sends SIGINT to the shell, with or without a selection, as in PuTTY
```

and the table row

```
| **Ctrl+C** | Copy selection to clipboard and clear it; sends SIGINT if there is no selection |
```

with

```
| **Ctrl+C** | Send SIGINT (interrupt), with or without a selection |
```

In `src/ui/help_guide.c`, replace

```c
"  - Select text: Click and drag in the terminal\r\n"
"  - Copy:        Ctrl+C copies the selection and clears it;\r\n"
"                 with no selection, Ctrl+C sends SIGINT as usual\r\n"
"  - Copy always: Ctrl+Shift+C (never sent to the shell)\r\n"
```

with

```c
"  - Select text: Click and drag in the terminal; releasing the\r\n"
"                 button copies the selection\r\n"
"  - Copy again:  Ctrl+Shift+C or Edit > Copy\r\n"
"  - Ctrl+C:      always sends SIGINT, selection or not\r\n"
```

and

```c
"  Ctrl+C           Copy selection (clears it); SIGINT if none\r\n"
```

with

```c
"  Ctrl+C           Interrupt (SIGINT), even with a selection\r\n"
```

- [ ] **Step 8: Verify**

Run: `grep -n "g_selection\|selection_extract_text\|selection_normalise\|cell_in_selection\|copy_selection_and_clear" src/ui/*.c`
Expected: no output.

Run: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/ui/window.c src/ui/renderer.c src/ui/help_guide.c src/core/selection.c src/term/buffer.c src/term/parser.c`
Expected: no output.

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`. This includes `tests/test_ui_tokens.c`, the design-system gate over `src/ui`.

- [ ] **Step 9: Commit**

```bash
git add src/ui/window.c src/ui/renderer.c src/ui/help_guide.c README.md
git commit -m "ui: PuTTY-style terminal selection: per session, auto-scroll, copy on release; Ctrl+C always interrupts"
```
Commit with the repo's Models line and trailer.

---

### Task 8: Confirm Paste takes Enter and Tab

**Files:**
- Modify: `src/core/paste_preview.h`, `src/core/paste_preview.c` (add `paste_dlg_key_action`)
- Modify: `src/ui/paste_dlg.c` (a focus helper before the dialog proc; the modal loop near `:521`)
- Test: `tests/test_paste_preview.c`, `tests/runner.c`

**Interfaces:**
- Consumes: `PasteDlgData` (`hBtnPaste`, `hBtnCancel`, `result`) in `paste_dlg.c`.
- Produces: `PasteKeyAction paste_dlg_key_action(unsigned vk, bool cancel_has_focus);` with `PASTE_KEY_NONE`, `PASTE_KEY_PASTE`, `PASTE_KEY_CANCEL`, `PASTE_KEY_NEXT_BUTTON`. Paste has the focus when the dialog opens; Enter presses the focused button.

- [ ] **Step 1: Write the failing tests** (append to `tests/test_paste_preview.c`)

```c
/* ---- paste_dlg_key_action (spec 2026-10-20-putty-selection-design.md 3.5) ---- */

int test_paste_key_enter_pastes_by_default(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paste_dlg_key_action(0x0D, false), PASTE_KEY_PASTE);
    TEST_END();
}

/* Review Focus 2: after Tab to Cancel, Enter presses Cancel. */
int test_paste_key_enter_on_cancel_cancels(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paste_dlg_key_action(0x0D, true), PASTE_KEY_CANCEL);
    TEST_END();
}

int test_paste_key_escape_cancels_wherever_focus_is(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paste_dlg_key_action(0x1B, false), PASTE_KEY_CANCEL);
    ASSERT_EQ(paste_dlg_key_action(0x1B, true), PASTE_KEY_CANCEL);
    TEST_END();
}

int test_paste_key_tab_moves_focus(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paste_dlg_key_action(0x09, false), PASTE_KEY_NEXT_BUTTON);
    ASSERT_EQ(paste_dlg_key_action(0x09, true), PASTE_KEY_NEXT_BUTTON);
    TEST_END();
}

int test_paste_key_other_keys_are_not_the_dialogs(void)
{
    TEST_BEGIN();
    ASSERT_EQ(paste_dlg_key_action('A', false), PASTE_KEY_NONE);
    ASSERT_EQ(paste_dlg_key_action(0x20, false), PASTE_KEY_NONE);   /* Space: the button's own */
    ASSERT_EQ(paste_dlg_key_action(0x26, true), PASTE_KEY_NONE);    /* Up */
    TEST_END();
}
```

Register the tests in `tests/runner.c`. Add the declarations after `int test_paste_clamp_too_wide(void);`:

```c
int test_paste_key_enter_pastes_by_default(void);
int test_paste_key_enter_on_cancel_cancels(void);
int test_paste_key_escape_cancels_wherever_focus_is(void);
int test_paste_key_tab_moves_focus(void);
int test_paste_key_other_keys_are_not_the_dialogs(void);
```

Add the calls after `failed += test_paste_clamp_too_wide();`:

```c
    failed += test_paste_key_enter_pastes_by_default();
    failed += test_paste_key_enter_on_cancel_cancels();
    failed += test_paste_key_escape_cancels_wherever_focus_is();
    failed += test_paste_key_tab_moves_focus();
    failed += test_paste_key_other_keys_are_not_the_dialogs();
```

- [ ] **Step 2: Run to verify they fail**

Run: `make test 2>&1 | grep -E "error:|undefined reference" | head`
Expected: `'PASTE_KEY_PASTE' undeclared` and `paste_dlg_key_action` undefined.

- [ ] **Step 3: Implement**

In `src/core/paste_preview.h`, add `#include <stdbool.h>` after `#include <stddef.h>`, and add before `#endif`:

```c
/* What a key pressed in the Confirm Paste dialog does (spec
 * docs/superpowers/specs/2026-10-20-putty-selection-design.md section 3.5).
 * `vk` is the Win32 virtual-key code: Enter 0x0D, Escape 0x1B, Tab 0x09.
 * Enter presses the focused button: Paste, which has the focus when the
 * dialog opens, or Cancel after a Tab. Escape always cancels. Tab moves the
 * focus between the two buttons. Every other key belongs to the control
 * that has the focus. */
typedef enum {
    PASTE_KEY_NONE = 0,
    PASTE_KEY_PASTE,
    PASTE_KEY_CANCEL,
    PASTE_KEY_NEXT_BUTTON
} PasteKeyAction;

PasteKeyAction paste_dlg_key_action(unsigned vk, bool cancel_has_focus);
```

In `src/core/paste_preview.c`, add at the end:

```c
PasteKeyAction paste_dlg_key_action(unsigned vk, bool cancel_has_focus)
{
    switch (vk) {
    case 0x0Du: return cancel_has_focus ? PASTE_KEY_CANCEL : PASTE_KEY_PASTE; /* VK_RETURN */
    case 0x1Bu: return PASTE_KEY_CANCEL;                                      /* VK_ESCAPE */
    case 0x09u: return PASTE_KEY_NEXT_BUTTON;                                 /* VK_TAB */
    default:    return PASTE_KEY_NONE;
    }
}
```

In `src/ui/paste_dlg.c`, add this helper directly before the dialog's window procedure (`PasteDlgProc`):

```c
/* Move the focus, and the default-button border with it, so that the
 * button Enter will press is the one that looks like it. */
static void paste_dlg_focus_button(PasteDlgData *d, HWND btn)
{
    HWND other = (btn == d->hBtnPaste) ? d->hBtnCancel : d->hBtnPaste;
    SendMessage(other, BM_SETSTYLE, (WPARAM)BS_PUSHBUTTON, TRUE);
    SendMessage(btn, BM_SETSTYLE, (WPARAM)BS_DEFPUSHBUTTON, TRUE);
    SetFocus(btn);
}
```

Replace the modal loop

```c
    if (hwnd) {
        EnableWindow(parent, FALSE);
        MSG msg;
        while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0)) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                d.result = 0;
                DestroyWindow(hwnd);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
```

with

```c
    if (hwnd) {
        EnableWindow(parent, FALSE);
        /* Paste has the focus from the start, so Enter alone pastes. Done
         * after CreateWindowEx: activating the new window gives the focus
         * to the window itself, which would undo a SetFocus made during
         * WM_CREATE. */
        SetFocus(d.hBtnPaste);
        MSG msg;
        while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0)) {
            if (msg.message == WM_KEYDOWN) {
                PasteKeyAction act = paste_dlg_key_action((unsigned)msg.wParam,
                                                          GetFocus() == d.hBtnCancel);
                if (act == PASTE_KEY_CANCEL) {
                    d.result = 0;
                    DestroyWindow(hwnd);
                    continue;
                }
                if (act == PASTE_KEY_PASTE) {
                    SendMessage(hwnd, WM_COMMAND, IDOK, 0);   /* the button's path */
                    continue;
                }
                if (act == PASTE_KEY_NEXT_BUTTON) {
                    paste_dlg_focus_button(&d, GetFocus() == d.hBtnCancel
                                                   ? d.hBtnPaste : d.hBtnCancel);
                    continue;
                }
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
```

- [ ] **Step 4: Run the tests and the product-flags check**

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`.

Run: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/ui/paste_dlg.c src/core/paste_preview.c`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add src/core/paste_preview.h src/core/paste_preview.c src/ui/paste_dlg.c tests/test_paste_preview.c tests/runner.c
git commit -m "paste: Confirm Paste opens on Paste; Enter presses the focused button, Tab switches"
```
Commit with the repo's Models line and trailer.

---

### Task 9: Documentation: README, help guide and the notes Open list

**Files:**
- Modify: `README.md` ("Text Selection and Clipboard", about lines 118-124)
- Modify: `src/ui/help_guide.c` ("COPY & PASTE", about lines 96-110)
- Modify: `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` (the "Open, in priority order" list)

**Interfaces:**
- Consumes: the behaviour Tasks 4-8 built. The Ctrl+C lines already changed in Task 7.
- Produces: user documentation and the notes entry for spec section 4's out-of-scope items.

- [ ] **Step 1: README.** In "Text Selection and Clipboard", replace

```
- **Click and drag** on the terminal to select text
```

with

```
- **Click and drag** on the terminal to select text. Dragging above or below the terminal scrolls the history and the selection follows, so a selection can be longer than the window; the wheel scrolls faster while you drag. The selection is held in the history, not the window: scrolling with the wheel, the scrollbar or Page Up/Down keeps it
- **Double-click** selects a word (a path or URL is one word, even when it wraps onto the next row); **triple-click** selects the whole line. Dragging after either extends by whole words or lines
- The highlight stays until the next click in the terminal, until the program writes over the selected text, or until a resize or a full-screen program (vim, less) replaces the screen. Each tab keeps its own selection
- A soft-wrapped line copies as one line; lines end in CRLF and trailing blanks are trimmed. There is no size limit
```

Replace

```
- **Ctrl+V** or **Shift+Insert** pastes from the clipboard; **Ctrl+Shift+V** always pastes
```

with

```
- **Ctrl+V**, **Shift+Insert** or a **right-click** pastes from the clipboard; **Ctrl+Shift+V** always pastes
- **Edit > Select All** selects everything from the oldest line held to the last line with text, and copies it
```

At the end of the **Confirm before pasting** bullet, after `so you can review what's about to be sent`, append:

```
. The **Paste** button has the focus, so **Enter** pastes; **Tab** moves to **Cancel** (Enter then cancels) and **Escape** cancels
```

- [ ] **Step 2: Help guide.** In `src/ui/help_guide.c`, replace

```c
"  - Select text: Click and drag in the terminal; releasing the\r\n"
"                 button copies the selection\r\n"
```

with

```c
"  - Select text: Click and drag in the terminal; releasing the\r\n"
"                 button copies the selection\r\n"
"  - Past edges:  drag above or below the terminal to scroll the\r\n"
"                 history; the selection follows\r\n"
"  - Word, line:  double-click selects a word, triple-click a line\r\n"
```

Replace

```c
"  - Paste:       Ctrl+V, Ctrl+Shift+V, or Shift+Insert\r\n"
"  - Select All:  Edit menu (no shortcut; Ctrl+A goes to shell)\r\n"
```

with

```c
"  - Paste:       Ctrl+V, Ctrl+Shift+V, Shift+Insert, right-click\r\n"
"  - Select All:  Edit menu, copies all the history (no shortcut;\r\n"
"                 Ctrl+A goes to the shell)\r\n"
```

Replace

```c
"preview before every paste. An inter-line paste delay is also\r\n"
"available, for servers that need time between lines.\r\n"
```

with

```c
"preview before every paste: Enter pastes, Tab moves to Cancel,\r\n"
"Escape cancels. An inter-line paste delay is also available,\r\n"
"for servers that need time between lines.\r\n"
```

- [ ] **Step 3: Notes Open list.** In `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`, replace the whole entry that begins `- [ ] Terminal copy and paste are broken in v1.2.36`, up to and including its last line `buffer, plus whatever the reproduction finds).`, with:

```
- [ ] Terminal selection follow-ups (spec `2026-10-20-putty-selection-design.md` section 4;
      the PuTTY-style selection itself is on `feat/putty-selection`): Shift+click to
      extend; Alt+drag block selection; middle-button paste; mouse reporting to
      applications; the per-byte paste writes over SSH; the scrollback-size setting
      (Settings and the help guide offer 100-50,000 lines but every session gets 3,000;
      `term_init` and `term_reflow_buffer` allocate the whole capacity, so lazy
      allocation in reflow comes first, spec section 6.2); a narrowing reflow of a full
      ring drops the newest rows rather than the oldest (`buffer.c:142, 165`,
      pre-existing).
```

- [ ] **Step 4: Verify**

Run: `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include -Isrc/term -Isrc/ssh -Isrc/ui -fsyntax-only src/ui/help_guide.c`
Expected: no output.

Run: `grep -n "Ctrl+C copies\|clears it" README.md src/ui/help_guide.c`
Expected: no line about the terminal. README's AI-panel bullet about Ctrl+C in the chat list (around line 250) is unrelated and stays.

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`.

- [ ] **Step 5: Commit**

```bash
git add README.md src/ui/help_guide.c docs/superpowers/specs/2026-09-06-ui-redesign-notes.md
git commit -m "docs: terminal selection, Confirm Paste keys, and the selection follow-ups"
```
Commit with the repo's Models line and trailer.

---

### Task 10: Harness: the drag helpers and the selection and Confirm Paste cases

The harness runs only on Windows, against the built exe and a live SSH host. A Linux session writes this task and checks that it parses; the cases are run on the Windows box after a build (Task 11 or the cheat-sheet plan's build). The suite is a manual tool, not a merge gate (CLAUDE.md, "Integration Harness").

**Files:**
- Modify: `tests/integration/NutshellIT.psm1` (the `NutshellSelNative` source, the constants, a cursor-parking helper, new functions after `Send-NutshellChatDrag`, `Export-ModuleMember`)
- Modify: `tests/integration/cases/10-terminal.ps1` (two Confirm Paste cases after `paste_with_confirmation_shows_dialog`; the selection cases appended at the end)
- Modify: `tests/integration/README.md` (the case-file summary at line 38, the cases table, the helpers table)

**Interfaces:**
- Consumes: the debug events `sel.scroll offset=N` and `sel.clear reason=...` (Task 7), the `render.draw` trace fields `rows= ... char_h= char_w= cols= x= y=` (`renderer.c`), `CS_DBLCLKS` (Task 7), `IDM_EDIT_COPY` = 2010 (`resource.h`).
- Produces these helpers:
  - `Send-NutshellDrag -Session -From @(x,y) -To @(x,y) [-Clicks 1|2|3] [-Steps n] [-Hold]`
  - `Send-NutshellDragEnd -Session -At @(x,y)`
  - `Send-NutshellWheel -Session -Lines n`
  - `Get-NutshellTermGeometry -Session`, which returns `Rows, Cols, CharW, CharH, X, Y` in client pixels.
  - `Get-NutshellCellPoint -Geo -Row -Col`, which returns `@(x, y)`.
  - `Get-NutshellDebugLogText -Session` and `Wait-NutshellDebugLog -Session -Pattern [-TimeoutSec]`

- [ ] **Step 1: Module helpers.** In `NutshellIT.psm1`, inside `$nutshellSelNativeSource`, add after the `GetSystemMetrics` line:

```csharp
    [DllImport("user32.dll", EntryPoint="GetClassLongPtrW")] public static extern IntPtr GetClassLongPtr(IntPtr h, int index);
```

(Changing the type's source trips the stale-console guard in an old console, by design: run in a new PowerShell window.)

After `$script:MK_LBUTTON         = 0x0001`, add:

```powershell
$script:WM_LBUTTONDBLCLK   = 0x0203
$script:WM_MOUSEWHEEL      = 0x020A
```

Add this function directly before `function Send-NutshellChatDrag {`:

```powershell
function Set-NutshellCursorParked {
    <# Park the real cursor one pixel inside the bottom-right corner of the
       virtual screen. Windows synthesises a WM_MOUSEMOVE from the real cursor
       (position and button state, wParam 0) when mouse capture changes, and a
       sent button-down takes capture; parked off the window, that move lands
       outside every case's geometry. SetCursorPos fails harmlessly on a locked
       desktop, so a false return is ignored. Not exported. #>
    $vx = [NutshellSelNative]::GetSystemMetrics(76)   # SM_XVIRTUALSCREEN
    $vy = [NutshellSelNative]::GetSystemMetrics(77)   # SM_YVIRTUALSCREEN
    $vw = [NutshellSelNative]::GetSystemMetrics(78)   # SM_CXVIRTUALSCREEN
    $vh = [NutshellSelNative]::GetSystemMetrics(79)   # SM_CYVIRTUALSCREEN
    if ($vw -gt 0 -and $vh -gt 0) {
        [NutshellNative]::SetCursorPos($vx + $vw - 1, $vy + $vh - 1) | Out-Null
    }
}
```

In `Send-NutshellChatDrag`, replace the block from `    # Park the real cursor in the bottom-right corner of the virtual screen,` through the closing `    }` of its `if ($vw -gt 0 -and $vh -gt 0) {` with the single line:

```powershell
    Set-NutshellCursorParked
```

Add these functions directly after `Send-NutshellChatDrag`'s closing brace:

```powershell
# ---- Terminal selection (spec docs/superpowers/specs/2026-10-20-putty-selection-design.md 3.2) ----

function Get-NutshellDebugLogText {
    <# The newest debug log of this case (settings: debug_enabled, debug_log_dir
       = "debuglogs"), read with sharing while the app writes it; "" if none. #>
    param([Parameter(Mandatory)] $Session)
    $dir = Join-Path $Session.Env.Root "debuglogs"
    $log = Get-ChildItem -Path (Join-Path $dir "nutshell-*-p*.log") -ErrorAction SilentlyContinue |
           Where-Object { $_.Name -notmatch "\.issue\d+\.log$" } |
           Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $log) { return "" }
    $fs = [IO.File]::Open($log.FullName, 'Open', 'Read', 'ReadWrite')
    try { $sr = New-Object IO.StreamReader($fs); return $sr.ReadToEnd() } finally { $fs.Dispose() }
}

function Wait-NutshellDebugLog {
    <# Wait until the debug log matches -Pattern (regex). Returns $true/$false. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Pattern, [int] $TimeoutSec = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ((Get-NutshellDebugLogText -Session $Session) -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Get-NutshellTermGeometry {
    <# The terminal grid from the last render.draw event (needs
       debug_level_render = "trace"): Rows, Cols, CharW, CharH, and X, Y, the
       top-left of cell (0,0) in main-window client pixels -- renderer_draw()
       is called with the left margin and the tab height, and mouse messages
       sent to the main window are in the same client coordinates. #>
    param([Parameter(Mandatory)] $Session, [int] $TimeoutSec = 8)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $draws = @((Get-NutshellDebugLogText -Session $Session) -split "`n" | Where-Object { $_ -match ' render\.draw ' })
        if ($draws.Count -gt 0) {
            $m = [regex]::Match($draws[$draws.Count - 1],
                '\brows=(\d+) .*\bchar_h=(\d+) char_w=(\d+) cols=(\d+) x=(-?\d+) y=(-?\d+)')
            if ($m.Success) {
                return [pscustomobject]@{
                    Rows  = [int]$m.Groups[1].Value
                    CharH = [int]$m.Groups[2].Value
                    CharW = [int]$m.Groups[3].Value
                    Cols  = [int]$m.Groups[4].Value
                    X     = [int]$m.Groups[5].Value
                    Y     = [int]$m.Groups[6].Value
                }
            }
        }
        Start-Sleep -Milliseconds 300
    }
    throw "no render.draw event with char_w/cols/x/y in the debug log (debug_level_render must be trace)"
}

function Get-NutshellCellPoint {
    <# Client pixel at the centre of cell (Row, Col) of a Get-NutshellTermGeometry
       grid. Fractional and out-of-grid rows are allowed: -Row -2.5 is two cell
       heights above the terminal area, -Row ($g.Rows + 1.5) two below it. #>
    param([Parameter(Mandatory)] $Geo, [double] $Row, [double] $Col)
    return @([int]($Geo.X + ($Col + 0.5) * $Geo.CharW), [int]($Geo.Y + ($Row + 0.5) * $Geo.CharH))
}

function Send-NutshellDrag {
    <# A left-button drag in the terminal with sent mouse messages (no
       foreground; the real cursor is parked first), from -From to -To, client
       pixels of the main window (Get-NutshellCellPoint). -Clicks 2 sends
       down, up, WM_LBUTTONDBLCLK; -Clicks 3 then up and a further down at
       once (the triple-click). Then -Steps moves with the button held to -To
       and the button-up there -- unless -Hold leaves the button down, so a
       case can wait on sel.scroll and end it with Send-NutshellDragEnd. Sent,
       not posted, so each message is handled before the next goes. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [int[]] $From,
          [Parameter(Mandatory)] [int[]] $To, [ValidateRange(1, 3)] [int] $Clicks = 1,
          [int] $Steps = 4, [switch] $Hold)
    Set-NutshellCursorParked
    $h = $Session.Main
    $mk = [IntPtr]$script:MK_LBUTTON
    $p0 = ConvertTo-NutshellPointLParam -X $From[0] -Y $From[1]
    [NutshellNative]::SendMsg($h, $script:WM_LBUTTONDOWN, $mk, $p0) | Out-Null
    if ($Clicks -ge 2) {
        [NutshellNative]::SendMsg($h, $script:WM_LBUTTONUP, [IntPtr]::Zero, $p0) | Out-Null
        [NutshellNative]::SendMsg($h, $script:WM_LBUTTONDBLCLK, $mk, $p0) | Out-Null
    }
    if ($Clicks -ge 3) {
        [NutshellNative]::SendMsg($h, $script:WM_LBUTTONUP, [IntPtr]::Zero, $p0) | Out-Null
        [NutshellNative]::SendMsg($h, $script:WM_LBUTTONDOWN, $mk, $p0) | Out-Null
    }
    for ($i = 1; $i -le $Steps; $i++) {
        $x = $From[0] + [int](($To[0] - $From[0]) * $i / $Steps)
        $y = $From[1] + [int](($To[1] - $From[1]) * $i / $Steps)
        [NutshellNative]::SendMsg($h, $script:WM_MOUSEMOVE, $mk,
            (ConvertTo-NutshellPointLParam -X $x -Y $y)) | Out-Null
    }
    if (-not $Hold) {
        [NutshellNative]::SendMsg($h, $script:WM_LBUTTONUP, [IntPtr]::Zero,
            (ConvertTo-NutshellPointLParam -X $To[0] -Y $To[1])) | Out-Null
    }
    Start-Sleep -Milliseconds 200
}

function Send-NutshellDragEnd {
    <# Release a Send-NutshellDrag -Hold at -At (client pixels). #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [int[]] $At)
    [NutshellNative]::SendMsg($Session.Main, $script:WM_LBUTTONUP, [IntPtr]::Zero,
        (ConvertTo-NutshellPointLParam -X $At[0] -Y $At[1])) | Out-Null
    Start-Sleep -Milliseconds 200
}

function Send-NutshellWheel {
    <# Scroll the terminal -Lines lines (positive: back into history) with one
       sent WM_MOUSEWHEEL. window.c scrolls 3 lines per WHEEL_DELTA (120), so
       the delta is Lines * 40; |Lines| <= 800 keeps it in the signed 16 bits. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [int] $Lines)
    $delta = $Lines * 40
    $w = [IntPtr]([int64](($delta -band 0xFFFF) -shl 16))
    [NutshellNative]::SendMsg($Session.Main, $script:WM_MOUSEWHEEL, $w, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 200
}
```

In `Export-ModuleMember`, replace its last line `    Set-NutshellFocus` with:

```powershell
    Set-NutshellFocus, Send-NutshellDrag, Send-NutshellDragEnd, Send-NutshellWheel, `
    Get-NutshellTermGeometry, Get-NutshellCellPoint, Get-NutshellDebugLogText, Wait-NutshellDebugLog
```

- [ ] **Step 2: Confirm Paste cases.** In `cases/10-terminal.ps1`, insert directly after the closing `}` of `Invoke-Case "paste_with_confirmation_shows_dialog"`:

```powershell
# Confirm Paste takes Enter (spec 2026-10-20 section 3.5): Paste has the focus
# when the dialog opens. The pasted command computes its marker, so the
# marker reaches the log only if the command ran, not from the echo of the
# paste itself.
Invoke-Case "paste_with_confirmation_enter_pastes" @{ paste_confirm = $true } {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-Clipboard -Value 'echo PASTE_ENTER_$((4*2))'
    Send-NutshellChord -Session $s -Key V -Ctrl -SettleMs 300
    $dlg = Wait-NutshellDialog -Session $s -Title "Confirm Paste" -TimeoutSec 5
    Assert-True ($dlg -ne [IntPtr]::Zero) "no Confirm Paste dialog appeared"
    Send-NutshellKey -Session $s -Key Enter -Hwnd $dlg -SettleMs 600
    Assert-True (-not (Get-NutshellWindows -Session $s | Where-Object { $_ -match "Nutshell_PastePreview" })) "the dialog is still open after Enter"
    Send-NutshellKey -Session $s -Key Enter
    Assert-True (Wait-NutshellLog -Session $s -Pattern "PASTE_ENTER_8" -TimeoutSec 5) "Enter in the dialog did not paste: the command never ran"
    "Enter pasted"
}

# Review Focus 2: Tab moves the focus to Cancel, and Enter then presses Cancel.
Invoke-Case "paste_with_confirmation_tab_enter_cancels" @{ paste_confirm = $true } {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-Clipboard -Value 'echo PASTE_TAB_$((5*2))'
    Send-NutshellChord -Session $s -Key V -Ctrl -SettleMs 300
    $dlg = Wait-NutshellDialog -Session $s -Title "Confirm Paste" -TimeoutSec 5
    Assert-True ($dlg -ne [IntPtr]::Zero) "no Confirm Paste dialog appeared"
    Send-NutshellKey -Session $s -Key Tab -Hwnd $dlg -SettleMs 300
    Send-NutshellKey -Session $s -Key Enter -Hwnd $dlg -SettleMs 600
    Assert-True (-not (Get-NutshellWindows -Session $s | Where-Object { $_ -match "Nutshell_PastePreview" })) "the dialog is still open after Tab, Enter"
    Send-NutshellKey -Session $s -Key Enter -SettleMs 1000
    Assert-True ((Get-NutshellLogText -Session $s) -notmatch "PASTE_TAB_10") "Enter on the focused Cancel button pasted anyway"
    "Tab, Enter cancelled"
}
```

- [ ] **Step 3: Selection cases.** Append to the end of `cases/10-terminal.ps1`:

```powershell
# ---- Terminal selection, PuTTY style (spec docs/superpowers/specs/2026-10-20-putty-selection-design.md 3.2) ----
# Gate tier. Debug logging is on: render at trace for the render.draw
# geometry, general at debug for the sel.* events. Every point is a cell of
# that geometry, never a pixel literal (the dev box runs at 288 DPI). The
# clipboard is read raw and compared with CRLF intact. Start-SelCase clears
# the screen, so a typed command sits on row 0 and its output starts on row 1.

$script:SelSettings = @{ debug_enabled = $true; debug_level_render = "trace"; debug_level_general = "debug"; debug_log_dir = "debuglogs" }

function Get-SelClipboard {
    $t = Get-Clipboard -Raw
    if ($null -eq $t) { return "" }
    return [string]$t
}

function Start-SelCase {
    <# Session log on, shell ready, screen cleared; returns the grid geometry. #>
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "clear" -SettleMs 800
    $g = Get-NutshellTermGeometry -Session $s
    Assert-True ($g.Cols -ge 60) "the terminal is too narrow for the selection cases: $($g.Cols) columns"
    return $g
}

function Assert-SelConsecutive {
    <# Every line is an integer one more than the line before; returns the first. #>
    param([string[]] $Lines)
    $first = 0
    Assert-True ([int]::TryParse($Lines[0], [ref]$first)) "the copy does not start with a number: '$($Lines[0])'"
    for ($i = 1; $i -lt $Lines.Count; $i++) {
        Assert-True ($Lines[$i] -eq [string]($first + $i)) "line $i is '$($Lines[$i])', expected $($first + $i)"
    }
    return $first
}

Invoke-Case "select_drag_past_bottom_copies_history" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    Start-Sleep -Milliseconds 500
    Send-NutshellWheel -Session $s -Lines 100
    $from  = Get-NutshellCellPoint -Geo $g -Row 0 -Col 0
    $below = Get-NutshellCellPoint -Geo $g -Row ($g.Rows + 1.5) -Col 0   # two cell heights past the bottom edge
    Send-NutshellDrag -Session $s -From $from -To $below -Hold
    $atBottom = Wait-NutshellDebugLog -Session $s -Pattern 'sel\.scroll .*offset=0\b' -TimeoutSec 10
    Send-NutshellDragEnd -Session $s -At $below
    Assert-True $atBottom "the drag never auto-scrolled back to the bottom (no sel.scroll offset=0)"
    $clip = Get-SelClipboard
    Assert-True ($clip -notmatch "(?<!`r)`n") "a line ending in the copy is not CRLF"
    $lines = @($clip -split "`r`n")
    Assert-True ($lines.Count -gt $g.Rows) "copied $($lines.Count) lines, no more than the $($g.Rows) rows on screen"
    $n = $lines.Count - 1
    $first = Assert-SelConsecutive -Lines $lines[0..($n - 1)]
    Assert-True ($first + $n - 1 -eq 300) "the numbers end at $($first + $n - 1), not 300"
    Assert-True ($lines[$n].Trim().Length -gt 0) "the last copied line is not the prompt"
    "copied $($lines.Count) lines ($first..300 and the prompt) with $($g.Rows) rows on screen"
}

Invoke-Case "select_drag_above_top_selects_first_row" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    Start-Sleep -Milliseconds 500
    $from  = Get-NutshellCellPoint -Geo $g -Row 3 -Col 6
    $above = Get-NutshellCellPoint -Geo $g -Row -2.5 -Col 0               # two cell heights above the terminal area
    Send-NutshellDrag -Session $s -From $from -To $above
    # Timer ticks between the sent messages may auto-scroll a few lines
    # before the release; the last sel.scroll says how many.
    $scrolls = [regex]::Matches((Get-NutshellDebugLogText -Session $s), 'sel\.scroll .*offset=(\d+)')
    $off = if ($scrolls.Count -gt 0) { [int]$scrolls[$scrolls.Count - 1].Groups[1].Value } else { 0 }
    $lines = @((Get-SelClipboard) -split "`r`n")
    Assert-True ($lines.Count -eq 4 + $off) "copied $($lines.Count) lines; expected rows 0-3 plus $off auto-scrolled"
    $first = Assert-SelConsecutive -Lines $lines
    "copied $first..$($first + $lines.Count - 1), auto-scrolled $off"
}

Invoke-Case "select_while_scrolled_back_copies_highlight" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    Start-Sleep -Milliseconds 500
    $a = Get-NutshellCellPoint -Geo $g -Row 0 -Col 0
    $b = Get-NutshellCellPoint -Geo $g -Row 2 -Col 6
    Send-NutshellDrag -Session $s -From $a -To $b
    $live = @((Get-SelClipboard) -split "`r`n")
    Assert-True ($live.Count -eq 3) "the live drag copied $($live.Count) lines"
    $liveFirst = Assert-SelConsecutive -Lines $live
    Send-NutshellWheel -Session $s -Lines 100
    Send-NutshellDrag -Session $s -From $a -To $b
    $back = @((Get-SelClipboard) -split "`r`n")
    Assert-True ($back.Count -eq 3) "the scrolled-back drag copied $($back.Count) lines"
    $backFirst = Assert-SelConsecutive -Lines $back
    Assert-True ($backFirst -eq $liveFirst - 100) "scrolled back 100 lines, the drag copied from $backFirst, not $($liveFirst - 100)"
    "rows 0-2 hold $liveFirst.. live and $backFirst.. scrolled back"
}

Invoke-Case "click_does_not_touch_clipboard" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line 'echo CLICK$((3+4))TEXT'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "CLICK7TEXT" -TimeoutSec 5) "echo output missing"
    # A real selection first, so the click has one to clear.
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 12)
    Assert-True ((Get-SelClipboard) -eq "CLICK7TEXT") "the setup drag did not copy: '$(Get-SelClipboard)'"
    Set-Clipboard -Value "CLICK_MARKER_5"
    # Press on the last pixel of cell (1,2) and release half a drag threshold
    # to the right: across a cell boundary, under the threshold.
    $cx = [NutshellSelNative]::GetSystemMetrics(68)                  # SM_CXDRAG
    $half = [Math]::Max(1, [int][Math]::Floor(($cx - 1) / 2))
    $px = $g.X + 3 * $g.CharW - 1
    $py = [int]($g.Y + 1.5 * $g.CharH)
    Send-NutshellDrag -Session $s -From @($px, $py) -To @(($px + $half), $py) -Steps 1
    Assert-True ((Get-SelClipboard) -eq "CLICK_MARKER_5") "a click under the drag threshold changed the clipboard"
    Send-NutshellChord -Session $s -Key C -Ctrl -Shift -SettleMs 400
    Assert-True ((Get-SelClipboard) -eq "CLICK_MARKER_5") "the click did not clear the selection: Ctrl+Shift+C copied it"
    "SM_CXDRAG $cx, moved $half px: clipboard untouched, selection cleared"
}

Invoke-Case "ctrl_shift_c_copies_after_drag" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line 'echo SEL$((2+3))TEXT'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "SEL5TEXT" -TimeoutSec 5) "echo output missing"
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 12)
    Assert-True ((Get-SelClipboard) -eq "SEL5TEXT") "the release did not copy: '$(Get-SelClipboard)'"
    Set-Clipboard -Value "COPY_MARKER_1"
    Send-NutshellChord -Session $s -Key C -Ctrl -Shift -SettleMs 400
    Assert-True ((Get-SelClipboard) -eq "SEL5TEXT") "Ctrl+Shift+C did not copy the selection again"
    Set-Clipboard -Value "COPY_MARKER_2"
    Send-NutshellCommand -Session $s -Id 2010 -SettleMs 400           # IDM_EDIT_COPY
    Assert-True ((Get-SelClipboard) -eq "SEL5TEXT") "Edit > Copy did not copy the selection"
    "release, Ctrl+Shift+C and Edit > Copy all copied the selection"
}

Invoke-Case "ctrl_c_with_selection_interrupts" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line 'echo SEL$((3+3))TEXT'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "SEL6TEXT" -TimeoutSec 5) "echo output missing"
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 12)
    Assert-True ((Get-SelClipboard) -eq "SEL6TEXT") "the release did not copy"
    Send-NutshellLine -Session $s -Line "sleep 30"
    Start-Sleep -Milliseconds 800
    Send-NutshellChord -Session $s -Key C -Ctrl -SettleMs 300
    Send-NutshellLine -Session $s -Line 'echo AFTER_$((4+4))'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "AFTER_8" -TimeoutSec 2) "the prompt did not return within 2 s: Ctrl+C with a selection was not sent as SIGINT"
    Assert-True ((Get-SelClipboard) -eq "SEL6TEXT") "Ctrl+C changed the clipboard"
    "sleep 30 interrupted with a selection on screen; clipboard kept"
}

Invoke-Case "double_click_selects_word" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    $style = [int64][NutshellSelNative]::GetClassLongPtr($s.Main, -26)   # GCL_STYLE
    Assert-True (($style -band 0x0008) -ne 0) ("the main window class lacks CS_DBLCLKS (style 0x{0:X})" -f $style)
    Send-NutshellLine -Session $s -Line "echo /usr/local/bin and more"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^/usr/local/bin and more" -TimeoutSec 5) "echo output missing"
    Set-Clipboard -Value "WORD_MARKER"
    $p = Get-NutshellCellPoint -Geo $g -Row 1 -Col 6
    Send-NutshellDrag -Session $s -From $p -To $p -Clicks 2 -Steps 1
    Assert-True ((Get-SelClipboard) -eq "/usr/local/bin") "double-click copied '$(Get-SelClipboard)'"
    "double-click copied /usr/local/bin; CS_DBLCLKS set"
}

Invoke-Case "triple_click_selects_line" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "echo /usr/local/bin and more"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^/usr/local/bin and more" -TimeoutSec 5) "echo output missing"
    Set-Clipboard -Value "LINE_MARKER"
    $p = Get-NutshellCellPoint -Geo $g -Row 1 -Col 6
    Send-NutshellDrag -Session $s -From $p -To $p -Clicks 3 -Steps 1
    Assert-True ((Get-SelClipboard) -eq "/usr/local/bin and more") "triple-click copied '$(Get-SelClipboard)'"
    "triple-click copied the line"
}

Invoke-Case "output_into_selection_clears_it" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    # A short command name keeps the command on row 0 and its output on row 1.
    Send-NutshellLine -Session $s -Line 'f(){ for i in 1 2 3 4 5 6; do printf "\r$i"; sleep 1; done; echo; }'
    Send-NutshellLine -Session $s -Line "clear" -SettleMs 800
    Send-NutshellLine -Session $s -Line "f" -SettleMs 1200
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 4)
    Assert-True (Wait-NutshellDebugLog -Session $s -Pattern 'sel\.clear .*reason="?stale' -TimeoutSec 4) "no sel.clear reason=stale after the loop wrote into the selected row"
    Set-Clipboard -Value "STALE_MARKER_9"
    Send-NutshellChord -Session $s -Key C -Ctrl -Shift -SettleMs 400
    Assert-True ((Get-SelClipboard) -eq "STALE_MARKER_9") "Ctrl+Shift+C copied a selection the output had overwritten"
    "the next write cleared the selection; Ctrl+Shift+C copied nothing"
}
```

- [ ] **Step 4: Harness README.** In `tests/integration/README.md`, change the `cases\10-terminal.ps1` summary row (line 38) to:

```
| `cases\10-terminal.ps1` | connect, Ctrl+C, log filename, paste x4 (Enter and Tab in Confirm Paste), PTY resize, Page Up, holds position, inactive-tab resize, terminal selection x9 |
```

In the cases table, add after the `paste_with_confirmation_shows_dialog` row:

```
| `paste_with_confirmation_enter_pastes` | Enter in Confirm Paste pastes (Paste has the focus when it opens) |
| `paste_with_confirmation_tab_enter_cancels` | Tab moves to Cancel, and Enter then cancels |
```

and after the `resize_applies_to_inactive_tab` row:

```
| `select_drag_past_bottom_copies_history` | scrolled back 100 lines, a held drag past the bottom auto-scrolls to the end; the copy holds every line from the press to `300` and the prompt, CRLF separated |
| `select_drag_above_top_selects_first_row` | a drag to above the terminal area selects from the top row (signed coordinates, no wrap to the last row) |
| `select_while_scrolled_back_copies_highlight` | a drag while scrolled back copies the rows the view shows, not the live ones |
| `click_does_not_touch_clipboard` | a press and release under the drag threshold keeps the clipboard and clears the selection |
| `ctrl_shift_c_copies_after_drag` | the selection survives the copy on release; Ctrl+Shift+C and Edit > Copy copy it again |
| `ctrl_c_with_selection_interrupts` | Ctrl+C with a selection on screen still interrupts `sleep 30`; the clipboard is kept |
| `double_click_selects_word` | the class has `CS_DBLCLKS`; a double-click copies `/usr/local/bin` |
| `triple_click_selects_line` | a triple-click copies the whole line |
| `output_into_selection_clears_it` | output written into a selected row logs `sel.clear reason=stale`; Ctrl+Shift+C then copies nothing |
```

In the helpers table, add after the `Send-NutshellChatDrag` row:

```
| `Send-NutshellDrag -Session -From @(x,y) -To @(x,y) [-Clicks 1-3] [-Steps n] [-Hold]` | a terminal drag with sent mouse messages (no foreground; the cursor is parked first); `-Clicks 2` sends `WM_LBUTTONDBLCLK`, `-Clicks 3` a further press; `-Hold` leaves the button down |
| `Send-NutshellDragEnd -Session -At @(x,y)` | the button-up that ends a `-Hold` drag |
| `Send-NutshellWheel -Session -Lines n` | one sent `WM_MOUSEWHEEL` scrolling n lines (positive: back into history) |
| `Get-NutshellTermGeometry -Session` | rows, cols, cell size and grid origin in client pixels, from the last `render.draw` (render at trace) |
| `Get-NutshellCellPoint -Geo -Row -Col` | the client pixel at a cell's centre; fractional and out-of-grid rows allowed |
| `Get-NutshellDebugLogText -Session` / `Wait-NutshellDebugLog -Session -Pattern [-TimeoutSec]` | the case's debug log (`debug_log_dir = "debuglogs"`), and waiting on a regex in it |
```

- [ ] **Step 5: Verify**

From a Linux host there is no PowerShell; check the shape by eye and with `grep -c "Invoke-Case \"select_\|Invoke-Case \"click_\|Invoke-Case \"ctrl_shift_c_\|Invoke-Case \"ctrl_c_with\|Invoke-Case \"double_\|Invoke-Case \"triple_\|Invoke-Case \"output_into\|Invoke-Case \"paste_with_confirmation_" tests/integration/cases/10-terminal.ps1`. Expected: `12`.

On the Windows box, with a build of this branch (Task 11, or the cheat-sheet plan's build):

```powershell
$e = $null; [System.Management.Automation.Language.Parser]::ParseFile("$PWD\tests\integration\cases\10-terminal.ps1", [ref]$null, [ref]$e) | Out-Null; $e
$e = $null; [System.Management.Automation.Language.Parser]::ParseFile("$PWD\tests\integration\NutshellIT.psm1", [ref]$null, [ref]$e) | Out-Null; $e
.\tests\integration\Run-Integration.ps1 -Tier gate -Only paste_with_confirmation_enter_pastes,paste_with_confirmation_tab_enter_cancels,select_drag_past_bottom_copies_history,select_drag_above_top_selects_first_row,select_while_scrolled_back_copies_highlight,click_does_not_touch_clipboard,ctrl_shift_c_copies_after_drag,ctrl_c_with_selection_interrupts,ctrl_c_without_selection_interrupts,double_click_selects_word,triple_click_selects_line,output_into_selection_clears_it
```

Expected: both parse checks print nothing, and every listed case prints `[PASS]`. If `Get-Clipboard -Raw` loses CRLF under Windows PowerShell 5.1 (spec section 5's unverified risk), `select_drag_past_bottom_copies_history` fails on its CRLF assertion. Record that in the pull request body; do not loosen the assertion.

- [ ] **Step 6: Commit**

```bash
git add tests/integration/NutshellIT.psm1 tests/integration/cases/10-terminal.ps1 tests/integration/README.md
git commit -m "test(harness): terminal drag helpers and the selection and Confirm Paste cases"
```
Commit with the repo's Models line and trailer.

---

### Task 11: Bump, build, verify (SKIPPABLE)

**SKIPPABLE.** The orchestrator runs this task only once, after both this plan and the cheat-sheet plan that follows it on `feat/putty-selection`, unless it instructs otherwise. Skip it while working through this plan. It needs the Windows cross-compiler and vcpkg, or the MSYS2 host with `mingw32-make`.

**Files:**
- Modify: `src/ui/resource.h` (`APP_VERSION`, `APP_VERSION_BINARY`), `README.md` (the `**Version**:` line)
- Build: `build/win/nutshell.exe`

**Interfaces:**
- Consumes: every earlier task.
- Produces: one committed, versioned build that the `Version bump` gate accepts.

- [ ] **Step 1: Compute the version.** Run `git fetch origin main` and `git show origin/main:src/ui/resource.h | grep 'define APP_VERSION '`, and also `grep 'define APP_VERSION ' src/ui/resource.h` on the branch. N is one patch above the larger of the two. The patch never passes 99: after `1.2.99` comes `1.3.0`. For example, with both at `1.2.43`, N is `1.2.44`.

- [ ] **Step 2: Bump.** In `src/ui/resource.h`, set `#define APP_VERSION        "N"` and `#define APP_VERSION_BINARY  X,Y,Z,0` (N's three parts). In `README.md`, set the `**Version**:` line to `vN`.

- [ ] **Step 3: Build and test**

Run: `make clean && make release` (on the MSYS2 host: `mingw32-make clean && mingw32-make release`)
Expected: `build/win/nutshell.exe` is rebuilt with no warnings.

Run: `make test 2>&1 | tail -3`
Expected: `Tests Run: N, Failed: 0`.

- [ ] **Step 4: Commit and gate-check**

```bash
git add src/ui/resource.h README.md build/win/nutshell.exe
git commit -m "build: vN, PuTTY-style terminal selection"
bash .github/scripts/check-version.sh "$(git merge-base origin/main HEAD)"
```
Commit with the repo's Models line and trailer. Expected: `check-version.sh` exits 0. Then run Task 10's harness subset on the Windows box against this exe. From a cloud (Linux) session, stop here: CLAUDE.md says do not merge, and warn the maintainer that the pipeline needs a successful `make test` and compiled code.

---

## Spec coverage

| Spec | Task |
|---|---|
| 1 Drag selects, copies on release; click under threshold copies nothing | 4 (threshold), 7 (handlers), 10 (`click_does_not_touch_clipboard`) |
| 1 Auto-scroll past an edge, stored pointer, clamped | 4 (`autoscroll_step`), 7 (`selection_tick`), 10 (drag-past cases) |
| 1 Double-click word, triple-click line, unit-wise extend, `CS_DBLCLKS` | 5, 7, 10 |
| 1 Persistence and clearing rules; per session | 6, 7 |
| 1 Copy again from the keyboard and the menu; Ctrl+C interrupts; Select All | 4 (`select_all`), 7, 10 |
| 1 The text (heap, wrap-joined, trimmed per logical line, CRLF, `cell_text_codepoint`) | 4 |
| 2.1 View mapping, resize and Page Up clamps | 1, 7 (Page Up call site) |
| 2.2 `lines_recycled`, `epoch`, `write_gen`, stamps | 2 |
| 2.3 Lazy staleness | 6 |
| 2.4 `wrapped` cleared on erase only | 3 |
| 2.5-2.6 The struct and the API | 4, 5, 6 |
| 2.7 Window | 7 |
| 2.8 Renderer | 7 |
| 2.9 Ctrl+C docs in the same commit | 7 (rest of docs in 9) |
| 2.10 Debug log | 7 |
| 3.1 Native tests | 1, 2, 3, 4, 5, 6 |
| 3.2 Harness | 10 |
| 3.5 Confirm Paste Enter and Tab | 8, 10 |
| 4 Out of scope, on the notes Open list | 9 |
| 6 Ctrl+C interrupts; scrollback not wired | 7; Global Constraints |
