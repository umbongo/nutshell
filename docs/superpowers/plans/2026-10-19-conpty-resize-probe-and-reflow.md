# ConPTY Resize: Console Probe and Reflow Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the console probe that tells the maintainer what conhost does to its own buffer on a resize (with and without `PSEUDOCONSOLE_RESIZE_QUIRK`), record the four scenario fixtures, and fix the two reflow faults every session has (a blank wrapped row at exact width; a cursor past the content pulled back onto it).

**Architecture:** Slice 1 runs this test harness again as a console program under a real pseudo-console (`--console-probe <scenario> <dumpfile>`), writes a known scenario into conhost, resizes, and reads conhost's buffer and cursor back through `ReadConsoleOutputCharacterW`. Section 1 (the top-anchored mode) is **not** built by this plan, so the parity case does not compare conhost with `term_resize()` in top mode: it compares conhost's dump with the spec's section 1.3 rule computed from the scenario text by a small pure helper (`tests/conpty_expect.c`, tested natively), and separately with today's bottom-anchored model snapshotted before the post-resize bytes are drained. Those comparisons are printed, not asserted (like wintest cases 4-7, they are facts about a Windows build); what is asserted is the harness itself and that the model's resize kept every scenario line exactly once. The maintainer's Windows run fills `docs/superpowers/specs/2026-10-19-conpty-resize-probe-report.md` and records the fixtures. Slice 2 changes the shared loop in `term_reflow_buffer()` (`src/term/buffer.c`) for every session, SSH included.

**Tech Stack:** C11; native `gcc` for `make test`; `x86_64-w64-mingw32-gcc` for `make wintest` and `make release`; the repo's `tests/test_framework.h`; Win32 console and ConPTY APIs; Wine 9 + `xvfb-run` on this Linux host for the wintest run.

**Spec:** `docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md` (section 0, section 2, Tests "Probe", "Fixtures" and "Reflow fixes", "Maintainer decisions"). Implementation-order items 1 and 2 only; sections 1, 3, 4 and 5 are later plans.

**Plan notes** (where the spec's wording and this plan part, and what was chosen; every code block below was compiled and run on this host while the plan was written):

- **Parity target.** The spec drains the scenario "into a top-mode terminal". Top mode does not exist until section 1 is built, so the expectation is the section 1.3 rule applied to the scenario text (`cpx_expected_after()`), not `term_resize()`. The report says whether conhost agreed; section 1 is then built (or amended per 1.8) in the next plan.
- **Wine runs the ConPTY cases.** Wine 9 on this host implements `CreatePseudoConsole` (cases 1-7 pass under it), so the new cases run under Wine too. Wine's `ResizePseudoConsole` fails (`local_pty_resize` returns -1) and its conhost never changes size, so under Wine the parity case prints "nothing to compare" for every run and passes; only a real Windows run is the report. The case detects Wine (`wine_get_version` in ntdll) and never records fixtures there.
- **Section 2's clamp changes an existing test.** `test_term_resize_cursor_edge` (`tests/test_term.c:261-283`) pins a cursor in the deferred-wrap column (`col == cols`) moving to row 2, col 0 when "0123456789" is narrowed to 5 columns. The spec's rule ("keeps its distance, clamped to `new_cols - 1`") puts it on row 1, col 4. Task 6 rewrites that assertion; this is visible on SSH and is the spec's choice, flagged here.
- **Empty continuation rows.** Section 2's formula alone would move a cursor that sits on a `wrapped` row with no cells (a stale flag ED 0 leaves) up onto the end of an exactly full row above it. The spec's own wording ("the wrap to the next output row happens when the next cell of the same logical line is copied") and its identity property both say the cursor keeps its row, so Task 6 opens the row for it. Pinned by a test.
- **Identity tests in bottom mode.** The spec states the round-trip identity for top mode. These tests use states bottom mode can also reverse (no top padding, nothing past the scrollback capacity); the top-mode versions join the same file with section 1.
- **Test file.** The spec's Files table names a new `tests/test_reflow_fixes.c`; the plan uses it rather than growing `tests/test_term.c`.
- **Raw tap.** `local_pty_poll()`'s `log_file` is ANSI-stripped and the debug log cuts `term.rx` lines at 4096 bytes, so neither can record a fixture. Task 2 adds `local_pty_set_raw_tap()` (not in the spec's Files table).
- **`pcon_flags` type.** The spec says `DWORD`; the header declares `unsigned long` (what `DWORD` is under MinGW) so `local_pty.h` need not include `windows.h` (`window.c` must include `winsock2.h` first).
- **Fixtures.** Four, one per scenario, from the `flags0` runs (what `local_pty_open()` opens with today), written only when `NSPROBE_RECORD=1` on Windows. The burst and throttled recordings are slice 4's, and so is the spec's "lines longer than the final width" requirement, which is about them; of these four, only `width` has such lines.
- **Report location.** A separate file, `docs/superpowers/specs/2026-10-19-conpty-resize-probe-report.md`, with a one-line pointer added to the spec's section 0.
- **Cursor-up.** The probe child places the cursor with `SetConsoleCursorPosition` rather than a VT cursor-up: a console program's VT output needs `ENABLE_VIRTUAL_TERMINAL_PROCESSING`, which would change what is being probed.

## Global Constraints

- **Always run `make test` after changes** to verify nothing is broken. Baseline on this branch: `Tests Run: 2957, Failed: 0`.
- **Two compilers**: `x86_64-w64-mingw32-gcc` for the Windows cross-compile (`make`, `make wintest`), native `gcc` for tests (`make test`). Code must compile clean under both with `-Werror`.
- Tests that write files must build paths from `TEST_TMP_DIR` (in `test_framework.h`), never a literal `/tmp/`. (The wintest code here follows the harness's existing `GetTempPathA()` convention, which is also never a literal.)
- CRLF files stay CRLF: `tests/runner.c`, `Makefile` and `src/term/buffer.c` are CRLF; edit them only with the byte-level scripts given in the tasks, and check afterwards that `grep -c $'\r$' FILE` equals `wc -l < FILE`.
- **MANDATORY: Version Bump Before Every Build** of the product: increment the patch version in `src/ui/resource.h` (`APP_VERSION` and `APP_VERSION_BINARY`) and `README.md` (`**Version**:`). This plan does it **once, in Task 8**, with `make clean && make release`, and commits `build/win/nutshell.exe`; the `Version bump` CI check requires it for any `src/` change. Until Task 8 never run `make`, `make all` or `make release`: `make test` and `make wintest` build test runners only, and Task 2 compiles single objects (`make src/ui/window.o`) and deletes them.
- `-Wshadow` is strict and `-Wconversion` catches `size_t`/`int` mismatches: cast explicitly.
- `ASSERT_EQ`/`ASSERT_TRUE` evaluate their operands a second time when they fail (to print them): never put a call with side effects inside one; store the result first.
- Inside the wintest's re-executed pty child, `ASSERT_*` print to a hidden console: print the reason with `RPT()` before asserting.
- **Git commits**: stage new files by path and read every `??` line of `git status --short` before committing (`git add -u` skips new files). Write the subject and body as if the maintainer authored them. End every commit with a `Models:` line, then a blank line, then exactly these trailers:
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM`
  The `Models:` line in the templates below reads `Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)`; if you are not `claude-sonnet-5`, put your own full model id there.
- This plan ends at local commits on `fix/conpty-resize-anchor` in `/home/thomas/nutshell-wt/conpty-resize`. Pushing and the pull request follow the `feature-workflow` skill; never push to `main`.
- Wintest runs on this host: `wineserver -k; xvfb-run -a make wintest`. It takes about 70 s. If the output stops right after `[pty] re-executing with a console of our own` and make reports `Error 1` with no case lines, run the same command once more (a Wine start-up flake, seen once while this plan was written).

## Review Focus

1. **A cursor on a continuation row with no cells, after a row that ends exactly at the width** (a stale `wrapped` flag left by ED 0, which ConPTY's repaints produce): a reasonable person expects the cursor to stay on its own row, not jump to the end of the row above. Pinned in Task 6 by `test_reflow_cursor_on_empty_continuation_keeps_row`.
2. **A cursor in the deferred-wrap column** (`cursor.col == cols` after a full row, which any shell line ending at the right edge leaves): expected to stay on its row at `new_cols - 1`, with no blank wrapped row opened for it. Pinned in Task 6 by `test_reflow_deferred_wrap_cursor_is_clamped` and the rewritten `test_term_resize_cursor_edge`.
3. **A probe scenario line whose length is an exact multiple of the probe's width**: a legacy console wraps at once, so the following `\r\n` leaves a blank row the prediction does not have, and the report would blame conhost for the harness's mistake. Expected: no scenario can be written that way. Pinned in Task 1 by `test_cpx_scenarios_are_listed_and_writable`.
4. **conhost's dump requested before the resize has reached conhost** (`ResizePseudoConsole` and the `D` key travel separate pipes), **or a host that never applies it** (Wine): expected to be retried until the dump shows the new size, otherwise reported by name, and failed on Windows only. Pinned in Task 4 by the parity case's retry loop and its Wine run ("never showed ... nothing to compare", then PASS).
5. **The fixtures' in-band marker `\e]9999;resize=COLSxROWS\a` reaching the screen, the cursor or the title on replay**: expected to be inert. Pinned in Task 4 by `test_term_conpty_resize_marker_is_inert`.

---

## File Structure

| File | Task | Responsibility |
|---|---|---|
| `tests/conpty_expect.h`, `tests/conpty_expect.c` (new) | 1 | The four probe scenarios and the section 1.3 prediction; pure, portable, built into both `make test` and `make wintest` |
| `tests/test_conpty_expect.c` (new) | 1 | Native tests of the prediction, worked by hand |
| `Makefile` (CRLF) | 1 | `tests/conpty_expect.c` joins `WIN_TEST_SRCS` (line 184); it stays out of `WIN_ONLY_TESTS` (line 142) so the native build compiles it too |
| `src/ui/local_pty.h`, `src/ui/local_pty.c` | 2 | `pcon_flags` on `local_pty_open_ex()`, `PSEUDOCONSOLE_RESIZE_QUIRK`, `local_pty_set_raw_tap()` |
| `src/ui/window.c` | 2 | The one product caller passes 0 |
| `tests/test_local_pty.c` | 2, 3, 4 | Case 8 (flags and tap), the probe child and dump reader, case 9 (probe smoke), case 10 (resize parity and fixture recorder) |
| `tests/win_runner.c` | 2, 3, 4 | Registration, `--console-probe` dispatch, the child's time budget |
| `tests/test_term_conpty.c` | 4 | The resize marker is inert |
| `docs/superpowers/specs/2026-10-19-conpty-resize-probe-report.md` (new) | 5 | The report the maintainer fills |
| `src/term/buffer.c` (CRLF) | 6, 7 | The two reflow fixes in `term_reflow_buffer()` |
| `tests/test_reflow_fixes.c` (new), `tests/test_term.c` | 6, 7 | Reflow-fix and identity tests; one existing assertion rewritten |
| `tests/runner.c` (CRLF) | 1, 4, 6, 7 | Declares and calls every new native test |
| `src/ui/resource.h`, `README.md`, `build/win/nutshell.exe` | 8 | The branch's one version bump and the rebuilt exe |

---

### Task 1: Probe scenarios and the section 1.3 prediction

**Files:**
- Create: `tests/conpty_expect.h`, `tests/conpty_expect.c`, `tests/test_conpty_expect.c`
- Modify: `tests/runner.c` (CRLF; after `int test_term_conpty_opening_sequence_is_consumed(void);` at line 3065 and its call at line 6255)
- Modify: `Makefile:184` (CRLF)

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces (used by Tasks 3 and 4): `CpxScenario`, `CpxView`, `CPX_MAX_LINES` (32), `CPX_MAX_ROWS` (48), `CPX_MAX_COLS` (80); `int cpx_scenario_count(void)`; `const CpxScenario *cpx_scenario_at(int index)`; `const CpxScenario *cpx_scenario_find(const char *name)`; `void cpx_place(int len, int offset, int cols, int *row_in_line, int *col)`; `int cpx_initial_view(const CpxScenario *s, CpxView *out)`; `int cpx_expected_after(const CpxScenario *s, CpxView *out)`; `int cpx_view_diff(const CpxView *a, const CpxView *b)`; `size_t cpx_tag(const char *line, char *out, size_t out_size)`. Scenario names, in order: `grow`, `shrink_bottom`, `shrink_below`, `width`.

The scenarios, all opened at 40 columns by 12 rows:

| Name | Written | Resize to | Section 1.3 predicts |
|---|---|---|---|
| `grow` | `<01> alpha` .. `<20> alpha`, then `P> ` with the cursor after it | 40x20 | rows 0-11 unchanged (`<10>` .. `<20>`, `P>`), rows 12-19 blank, cursor 11,3 |
| `shrink_bottom` | the same | 40x8 | `<14>` .. `<20>`, `P>`; cursor 7,3 |
| `shrink_below` | `<01>` .. `<06>`, `P> `, three empty lines, `<B> below`; cursor put back after `P> ` | 40x8 | segment ends at `<B>` (row 10): 11 rows, 3 leave the top: `<04>` `<05>` `<06>` `P>` blank blank blank `<B> below`; cursor 3,3 |
| `width` | six 34-cell lines `<W1>`..`<W6>` around one 52-cell `<WL>`, then `P> ` | 25x12 | 16 rows at 25 columns, 4 leave the top: `<W3>` (2 rows), `<WL>` (3 rows), `<W4>` .. `<W6>` (2 rows each), `P>`; cursor 11,3 |

- [ ] **Step 1: Write the failing test**

Create `tests/test_conpty_expect.c`:

```c
/* tests/test_conpty_expect.c -- the console probe's expectation helper
 * (tests/conpty_expect.c). The parity case in tests/test_local_pty.c
 * compares conhost's buffer with these predictions on a real Windows host;
 * these cases pin the predictions themselves, worked by hand from
 * docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md
 * section 1.3 and section 2. Native, `make test`. */

#include "test_framework.h"
#include "conpty_expect.h"
#include <string.h>

static void blank_view(CpxView *v, int rows, int cols)
{
    memset(v, 0, sizeof(*v));
    v->rows = rows;
    v->cols = cols;
}

int test_cpx_place_inside_and_past_content(void)
{
    TEST_BEGIN();
    int r = -1, c = -1;
    cpx_place(10, 3, 40, &r, &c);   /* inside                         */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 3);
    cpx_place(3, 3, 40, &r, &c);    /* one past "P> "                 */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 3);
    cpx_place(52, 51, 25, &r, &c);  /* last cell of a 3-row line      */
    ASSERT_EQ(r, 2); ASSERT_EQ(c, 1);
    cpx_place(7, 8, 8, &r, &c);     /* PS C:\> + 1, narrowed to 8     */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 7);
    cpx_place(7, 8, 6, &r, &c);     /* rewrapped: ">" then 1 past     */
    ASSERT_EQ(r, 1); ASSERT_EQ(c, 2);
    cpx_place(20, 20, 20, &r, &c);  /* exact width, clamped           */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 19);
    cpx_place(0, 0, 10, &r, &c);    /* empty line                     */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 0);
    cpx_place(0, 12, 10, &r, &c);   /* empty line, far past, clamped  */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 9);
    TEST_END();
}

/* Four scenarios in a fixed order, each findable by name, none with a line
 * whose length is a multiple of its old width (see conpty_expect.c). */
int test_cpx_scenarios_are_listed_and_writable(void)
{
    TEST_BEGIN();
    static const char *NAMES[] = { "grow", "shrink_bottom", "shrink_below",
                                   "width" };
    ASSERT_EQ(cpx_scenario_count(), 4);
    for (int i = 0; i < 4; i++) {
        const CpxScenario *s = cpx_scenario_at(i);
        ASSERT_NOT_NULL(s);
        if (!s) continue;
        ASSERT_STR_EQ(s->name, NAMES[i]);
        ASSERT_TRUE(cpx_scenario_find(NAMES[i]) == s);
        for (int l = 0; l < s->line_count; l++) {
            size_t len = strlen(s->lines[l]);
            ASSERT_TRUE(len == 0u || len % (size_t)s->old_cols != 0u);
        }
        /* tags are unique */
        for (int a = 0; a < s->line_count; a++) {
            char ta[16], tb[16];
            if (cpx_tag(s->lines[a], ta, sizeof(ta)) == 0u) continue;
            for (int b = a + 1; b < s->line_count; b++) {
                if (cpx_tag(s->lines[b], tb, sizeof(tb)) == 0u) continue;
                ASSERT_TRUE(strcmp(ta, tb) != 0);
            }
        }
    }
    ASSERT_NULL(cpx_scenario_at(4));
    ASSERT_NULL(cpx_scenario_at(-1));
    ASSERT_NULL(cpx_scenario_find("nope"));
    ASSERT_NULL(cpx_scenario_find(NULL));
    TEST_END();
}

/* grow, 40x12 -> 40x20: <10>..<20> and the prompt stay on rows 0-11, the
 * cursor stays on row 11, rows 12-19 are blank -- nothing pulled down. */
int test_cpx_grow_keeps_the_top_line(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("grow");
    CpxView v;
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cpx_initial_view(s, &v), 0);
    ASSERT_STR_EQ(v.text[0], "<10> alpha");
    ASSERT_STR_EQ(v.text[10], "<20> alpha");
    ASSERT_STR_EQ(v.text[11], "P>");
    ASSERT_EQ(v.cursor_row, 11);
    ASSERT_EQ(v.cursor_col, 3);

    ASSERT_EQ(cpx_expected_after(s, &v), 0);
    ASSERT_EQ(v.rows, 20);
    ASSERT_EQ(v.cols, 40);
    ASSERT_STR_EQ(v.text[0], "<10> alpha");
    ASSERT_STR_EQ(v.text[11], "P>");
    for (int r = 12; r < 20; r++) ASSERT_STR_EQ(v.text[r], "");
    ASSERT_EQ(v.cursor_row, 11);
    ASSERT_EQ(v.cursor_col, 3);
    TEST_END();
}

/* shrink_bottom, 40x12 -> 40x8: four rows leave the top. */
int test_cpx_shrink_with_cursor_at_bottom_drops_from_the_top(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("shrink_bottom");
    CpxView v;
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cpx_expected_after(s, &v), 0);
    ASSERT_EQ(v.rows, 8);
    ASSERT_STR_EQ(v.text[0], "<14> alpha");
    ASSERT_STR_EQ(v.text[6], "<20> alpha");
    ASSERT_STR_EQ(v.text[7], "P>");
    ASSERT_EQ(v.cursor_row, 7);
    ASSERT_EQ(v.cursor_col, 3);
    TEST_END();
}

/* shrink_below, 40x12 -> 40x8: the segment runs to <B> on row 10, eleven
 * rows, so three leave the top and the cursor follows its row to 3. */
int test_cpx_shrink_with_content_below_counts_it(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("shrink_below");
    CpxView v;
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cpx_initial_view(s, &v), 0);
    ASSERT_STR_EQ(v.text[6], "P>");
    ASSERT_STR_EQ(v.text[10], "<B> below");
    ASSERT_EQ(v.cursor_row, 6);

    ASSERT_EQ(cpx_expected_after(s, &v), 0);
    ASSERT_STR_EQ(v.text[0], "<04> alpha");
    ASSERT_STR_EQ(v.text[2], "<06> alpha");
    ASSERT_STR_EQ(v.text[3], "P>");
    ASSERT_STR_EQ(v.text[4], "");
    ASSERT_STR_EQ(v.text[6], "");
    ASSERT_STR_EQ(v.text[7], "<B> below");
    ASSERT_EQ(v.cursor_row, 3);
    ASSERT_EQ(v.cursor_col, 3);
    TEST_END();
}

/* width, 40x12 -> 25x12: every line rewraps, sixteen rows, four leave the
 * top (<W1> and <W2>, two rows each). */
int test_cpx_width_change_rewraps_and_overflows(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("width");
    CpxView v;
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(cpx_initial_view(s, &v), 0);
    ASSERT_STR_EQ(v.text[3], "<WL> abcdefghijklmnopqrstuvwxyzABCDEFGHI");
    ASSERT_STR_EQ(v.text[4], "JKLMNOPQRSTU");
    ASSERT_STR_EQ(v.text[8], "P>");
    ASSERT_EQ(v.cursor_row, 8);

    ASSERT_EQ(cpx_expected_after(s, &v), 0);
    ASSERT_EQ(v.cols, 25);
    ASSERT_STR_EQ(v.text[0], "<W3> abcdefghijklmnopqrst");
    ASSERT_STR_EQ(v.text[1], "uvwxyz012");
    ASSERT_STR_EQ(v.text[2], "<WL> abcdefghijklmnopqrst");
    ASSERT_STR_EQ(v.text[3], "uvwxyzABCDEFGHIJKLMNOPQRS");
    ASSERT_STR_EQ(v.text[4], "TU");
    ASSERT_STR_EQ(v.text[10], "uvwxyz012");
    ASSERT_STR_EQ(v.text[11], "P>");
    ASSERT_EQ(v.cursor_row, 11);
    ASSERT_EQ(v.cursor_col, 3);
    TEST_END();
}

/* A line straddling conhost's top cannot be predicted (its head is gone):
 * -1, not a guess. Bad sizes are -1 too. */
int test_cpx_rejects_unpredictable_scenarios(void)
{
    TEST_BEGIN();
    CpxScenario s;
    CpxView v;
    memset(&s, 0, sizeof(s));
    s.name = "straddle";
    s.old_cols = 10; s.old_rows = 3; s.new_cols = 10; s.new_rows = 5;
    s.line_count = 3;
    s.lines[0] = "<1> aaaaaaaaaaaa";   /* 16 cells: two rows at 10 */
    s.lines[1] = "<2>";
    s.lines[2] = "<3>";
    s.cursor_line = 2; s.cursor_offset = 3;
    ASSERT_EQ(cpx_initial_view(&s, &v), -1);
    ASSERT_EQ(cpx_expected_after(&s, &v), -1);

    s.lines[0] = "<1>";
    ASSERT_EQ(cpx_initial_view(&s, &v), 0);
    s.new_cols = CPX_MAX_COLS + 1;
    ASSERT_EQ(cpx_expected_after(&s, &v), -1);
    ASSERT_EQ(cpx_initial_view(NULL, &v), -1);
    TEST_END();
}

int test_cpx_view_diff_counts_rows_cursor_and_size(void)
{
    TEST_BEGIN();
    CpxView a, b;
    blank_view(&a, 3, 10);
    blank_view(&b, 3, 10);
    ASSERT_EQ(cpx_view_diff(&a, &b), 0);
    (void)strcpy(b.text[1], "x");
    ASSERT_EQ(cpx_view_diff(&a, &b), 1);
    b.cursor_col = 2;
    ASSERT_EQ(cpx_view_diff(&a, &b), 2);
    b.rows = 4;
    (void)strcpy(b.text[3], "y");
    ASSERT_EQ(cpx_view_diff(&a, &b), 4);
    char tag[8];
    ASSERT_EQ(cpx_tag("<07> alpha", tag, sizeof(tag)), 4u);
    ASSERT_STR_EQ(tag, "<07>");
    ASSERT_EQ(cpx_tag("", tag, sizeof(tag)), 0u);
    ASSERT_EQ(cpx_tag("P> ", tag, sizeof(tag)), 2u);
    ASSERT_STR_EQ(tag, "P>");
    TEST_END();
}
```

Register the eight tests in `tests/runner.c` (CRLF, so by bytes), from the worktree root:

```bash
cd /home/thomas/nutshell-wt/conpty-resize && python3 - test_term_conpty_opening_sequence_is_consumed test_conpty_expect.c \
  test_cpx_place_inside_and_past_content test_cpx_scenarios_are_listed_and_writable \
  test_cpx_grow_keeps_the_top_line test_cpx_shrink_with_cursor_at_bottom_drops_from_the_top \
  test_cpx_shrink_with_content_below_counts_it test_cpx_width_change_rewraps_and_overflows \
  test_cpx_rejects_unpredictable_scenarios test_cpx_view_diff_counts_rows_cursor_and_size <<'EOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'EOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
EOF
```

Expected: `runner.c: registered 8 tests after test_term_conpty_opening_sequence_is_consumed`

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "conpty_expect|Tests Run"`
Expected: the compile stops with `tests/test_conpty_expect.c:...: fatal error: conpty_expect.h: No such file or directory`.

- [ ] **Step 3: Write the header and the implementation**

Create `tests/conpty_expect.h`:

```c
#ifndef NUTSHELL_CONPTY_EXPECT_H
#define NUTSHELL_CONPTY_EXPECT_H

/* tests/conpty_expect.h -- the console probe's four scenarios, and what
 * docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md predicts
 * conhost shows after each resize: section 1.3 (top-anchored: the old
 * screen top stays, rows below the later of the cursor and the last
 * non-blank row are not carried, an overflow scrolls out from the top and
 * the cursor follows its row) with section 2's cursor rule.
 *
 * Pure, portable C. Built into `make test`, where
 * tests/test_conpty_expect.c checks it, and into `make wintest`, where the
 * parity case in tests/test_local_pty.c compares conhost's own buffer with
 * it. conhost's buffer is its viewport, so a scenario's "old screen" is the
 * last old_rows rows of its text laid out at old_cols. */

#include <stddef.h>

#define CPX_MAX_LINES 32
#define CPX_MAX_ROWS  48
#define CPX_MAX_COLS  80

typedef struct {
    const char *name;            /* grow, shrink_bottom, shrink_below, width */
    int old_cols, old_rows;      /* the size the probe child writes at       */
    int new_cols, new_rows;      /* the one resize the case makes            */
    int line_count;
    const char *lines[CPX_MAX_LINES]; /* written with "\r\n" between them    */
    int cursor_line;             /* index into lines                         */
    int cursor_offset;           /* cells from the start of that line        */
} CpxScenario;

typedef struct {
    int rows, cols;
    char text[CPX_MAX_ROWS][CPX_MAX_COLS + 1]; /* trailing spaces trimmed */
    int cursor_row, cursor_col;
} CpxView;

int cpx_scenario_count(void);
const CpxScenario *cpx_scenario_at(int index);       /* NULL out of range */
const CpxScenario *cpx_scenario_find(const char *name); /* NULL if none   */

/* Where a cursor offset cells into a line of len cells lands at cols:
 * inside the content by division; past it, the content's end plus the
 * distance, clamped to cols - 1 (spec section 2). */
void cpx_place(int len, int offset, int cols, int *row_in_line, int *col);

/* conhost's viewport once the child has written the scenario at
 * old_cols x old_rows. Returns 0, or -1 when the scenario cannot be
 * predicted (bad sizes, or a line straddling the viewport's top, whose
 * head conhost no longer has). */
int cpx_initial_view(const CpxScenario *s, CpxView *out);

/* The viewport section 1.3 predicts after the resize to
 * new_cols x new_rows. Returns 0, or -1 as above. */
int cpx_expected_after(const CpxScenario *s, CpxView *out);

/* Number of differences: one per differing row (rows missing from the
 * shorter view count as ""), plus one for the cursor, plus one for the
 * size. 0 means equal. */
int cpx_view_diff(const CpxView *a, const CpxView *b);

/* The line's tag (its text up to the first space) into out; returns its
 * length, 0 for an empty line. Tags are unique within a scenario. */
size_t cpx_tag(const char *line, char *out, size_t out_size);

#endif /* NUTSHELL_CONPTY_EXPECT_H */
```

Create `tests/conpty_expect.c`:

```c
/* tests/conpty_expect.c -- see conpty_expect.h. */

#include "conpty_expect.h"
#include <string.h>

/* No line's length is a multiple of old_cols: a legacy console moves the
 * cursor to the next row as soon as a row fills, so "\r\n" after an
 * exactly full row would leave a blank row the layout below does not have
 * (tests/test_conpty_expect.c checks this for every scenario). */
static const CpxScenario SCENARIOS[] = {
    { "grow", 40, 12, 40, 20, 21,
      { "<01> alpha", "<02> alpha", "<03> alpha", "<04> alpha", "<05> alpha",
        "<06> alpha", "<07> alpha", "<08> alpha", "<09> alpha", "<10> alpha",
        "<11> alpha", "<12> alpha", "<13> alpha", "<14> alpha", "<15> alpha",
        "<16> alpha", "<17> alpha", "<18> alpha", "<19> alpha", "<20> alpha",
        "P> " },
      20, 3 },
    { "shrink_bottom", 40, 12, 40, 8, 21,
      { "<01> alpha", "<02> alpha", "<03> alpha", "<04> alpha", "<05> alpha",
        "<06> alpha", "<07> alpha", "<08> alpha", "<09> alpha", "<10> alpha",
        "<11> alpha", "<12> alpha", "<13> alpha", "<14> alpha", "<15> alpha",
        "<16> alpha", "<17> alpha", "<18> alpha", "<19> alpha", "<20> alpha",
        "P> " },
      20, 3 },
    { "shrink_below", 40, 12, 40, 8, 11,
      { "<01> alpha", "<02> alpha", "<03> alpha", "<04> alpha", "<05> alpha",
        "<06> alpha", "P> ", "", "", "", "<B> below" },
      6, 3 },
    { "width", 40, 12, 25, 12, 8,
      { "<W1> abcdefghijklmnopqrstuvwxyz012",
        "<W2> abcdefghijklmnopqrstuvwxyz012",
        "<W3> abcdefghijklmnopqrstuvwxyz012",
        "<WL> abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTU",
        "<W4> abcdefghijklmnopqrstuvwxyz012",
        "<W5> abcdefghijklmnopqrstuvwxyz012",
        "<W6> abcdefghijklmnopqrstuvwxyz012",
        "P> " },
      7, 3 },
};

#define CPX_SCENARIO_COUNT ((int)(sizeof(SCENARIOS) / sizeof(SCENARIOS[0])))
#define CPX_LAYOUT_MAX 128

typedef struct {
    int count;
    char text[CPX_LAYOUT_MAX][CPX_MAX_COLS + 1];
    int line_of[CPX_LAYOUT_MAX];   /* the logical line each row belongs to */
    int chunk_of[CPX_LAYOUT_MAX];  /* 0 on a line's first row              */
} CpxLayout;

int cpx_scenario_count(void)
{
    return CPX_SCENARIO_COUNT;
}

const CpxScenario *cpx_scenario_at(int index)
{
    if (index < 0 || index >= CPX_SCENARIO_COUNT) return NULL;
    return &SCENARIOS[index];
}

const CpxScenario *cpx_scenario_find(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < CPX_SCENARIO_COUNT; i++)
        if (strcmp(SCENARIOS[i].name, name) == 0) return &SCENARIOS[i];
    return NULL;
}

void cpx_place(int len, int offset, int cols, int *row_in_line, int *col)
{
    if (cols < 1) cols = 1;
    if (len < 0) len = 0;
    if (offset < 0) offset = 0;
    if (offset < len) {
        *row_in_line = offset / cols;
        *col = offset % cols;
        return;
    }
    int end_row = (len == 0) ? 0 : (len - 1) / cols;
    int end_col = (len == 0) ? 0 : (len - 1) % cols + 1; /* may equal cols */
    int c = end_col + (offset - len);
    if (c > cols - 1) c = cols - 1;
    *row_in_line = end_row;
    *col = c;
}

static void trim_right(char *s)
{
    size_t n = strlen(s);
    while (n > 0u && s[n - 1u] == ' ') n--;
    s[n] = '\0';
}

static int line_len(const CpxScenario *s, int i)
{
    return (int)strlen(s->lines[i]);
}

/* Lines first..last laid out at cols, one row per cols cells, an empty
 * line taking one row. Returns the row count, or -1 when it overflows. */
static int layout(const CpxScenario *s, int first, int last, int cols,
                  CpxLayout *lay)
{
    lay->count = 0;
    for (int i = first; i <= last; i++) {
        int len = line_len(s, i);
        int rows = (len == 0) ? 1 : (len + cols - 1) / cols;
        for (int k = 0; k < rows; k++) {
            if (lay->count >= CPX_LAYOUT_MAX) return -1;
            int start = k * cols;
            int n = len - start;
            if (n > cols) n = cols;
            if (n < 0) n = 0;
            char *dst = lay->text[lay->count];
            if (n > 0) memcpy(dst, s->lines[i] + start, (size_t)n);
            dst[n] = '\0';
            trim_right(dst);
            lay->line_of[lay->count] = i;
            lay->chunk_of[lay->count] = k;
            lay->count++;
        }
    }
    return lay->count;
}

static int first_row_of(const CpxLayout *lay, int line)
{
    for (int r = 0; r < lay->count; r++)
        if (lay->line_of[r] == line) return r;
    return -1;
}

static int sizes_ok(const CpxScenario *s)
{
    return s && s->line_count > 0 && s->line_count <= CPX_MAX_LINES &&
           s->cursor_line >= 0 && s->cursor_line < s->line_count &&
           s->old_cols >= 1 && s->old_cols <= CPX_MAX_COLS &&
           s->new_cols >= 1 && s->new_cols <= CPX_MAX_COLS &&
           s->old_rows >= 1 && s->old_rows <= CPX_MAX_ROWS &&
           s->new_rows >= 1 && s->new_rows <= CPX_MAX_ROWS;
}

/* The old screen: the last old_rows rows at old_cols, and its top row. */
static int old_screen(const CpxScenario *s, CpxLayout *lay, int *top)
{
    if (layout(s, 0, s->line_count - 1, s->old_cols, lay) < 0) return -1;
    *top = (lay->count > s->old_rows) ? lay->count - s->old_rows : 0;
    if (lay->chunk_of[*top] != 0) return -1;
    return 0;
}

int cpx_initial_view(const CpxScenario *s, CpxView *out)
{
    static CpxLayout lay;
    int top = 0;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!sizes_ok(s) || old_screen(s, &lay, &top) < 0) return -1;

    out->rows = s->old_rows;
    out->cols = s->old_cols;
    for (int r = 0; r < s->old_rows; r++)
        if (top + r < lay.count)
            memcpy(out->text[r], lay.text[top + r], sizeof(out->text[r]));

    int rr = 0, cc = 0;
    cpx_place(line_len(s, s->cursor_line), s->cursor_offset, s->old_cols,
              &rr, &cc);
    out->cursor_row = first_row_of(&lay, s->cursor_line) + rr - top;
    out->cursor_col = cc;
    return (out->cursor_row >= 0 && out->cursor_row < s->old_rows) ? 0 : -1;
}

int cpx_expected_after(const CpxScenario *s, CpxView *out)
{
    static CpxLayout old_lay, lay;
    CpxView before;
    int top = 0;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (cpx_initial_view(s, &before) < 0) return -1;
    if (old_screen(s, &old_lay, &top) < 0) return -1;

    /* E: the later of the cursor's row and the last non-blank row of the
     * old screen; rows after it are blank and are not carried. */
    int e_row = before.cursor_row;
    for (int r = s->old_rows - 1; r > e_row; r--) {
        if (before.text[r][0] != '\0') { e_row = r; break; }
    }
    int first_line = old_lay.line_of[top];
    int last_line = old_lay.line_of[top + e_row];

    int seg = layout(s, first_line, last_line, s->new_cols, &lay);
    if (seg < 0) return -1;
    int excess = (seg > s->new_rows) ? seg - s->new_rows : 0;

    out->rows = s->new_rows;
    out->cols = s->new_cols;
    for (int r = 0; r < s->new_rows; r++)
        if (excess + r < seg)
            memcpy(out->text[r], lay.text[excess + r], sizeof(out->text[r]));

    int rr = 0, cc = 0;
    cpx_place(line_len(s, s->cursor_line), s->cursor_offset, s->new_cols,
              &rr, &cc);
    out->cursor_row = first_row_of(&lay, s->cursor_line) + rr - excess;
    if (out->cursor_row < 0) out->cursor_row = 0;   /* its row scrolled out */
    out->cursor_col = cc;
    return 0;
}

int cpx_view_diff(const CpxView *a, const CpxView *b)
{
    int diff = 0;
    if (a->rows != b->rows || a->cols != b->cols) diff++;
    if (a->cursor_row != b->cursor_row || a->cursor_col != b->cursor_col)
        diff++;
    int rows = a->rows > b->rows ? a->rows : b->rows;
    if (rows > CPX_MAX_ROWS) rows = CPX_MAX_ROWS;
    for (int r = 0; r < rows; r++) {
        const char *ta = (r < a->rows) ? a->text[r] : "";
        const char *tb = (r < b->rows) ? b->text[r] : "";
        if (strcmp(ta, tb) != 0) diff++;
    }
    return diff;
}

size_t cpx_tag(const char *line, char *out, size_t out_size)
{
    size_t n = 0u;
    if (!out || out_size == 0u) return 0u;
    while (line && line[n] && line[n] != ' ' && n + 1u < out_size) {
        out[n] = line[n];
        n++;
    }
    out[n] = '\0';
    return n;
}
```

Add it to the wintest sources (the `Makefile` is CRLF; this edits one line mid-line, so the line's `\r` stays; the pattern only matches the indented `WIN_TEST_SRCS` continuation, not `WIN_ONLY_TESTS` at line 142, which must not list it):

```bash
cd /home/thomas/nutshell-wt/conpty-resize && \
sed -i '/^ \+tests\/test_icons.c/s|tests/win_runner.c|tests/conpty_expect.c tests/win_runner.c|' Makefile && \
grep -n "conpty_expect" Makefile | tr -d '\r' && echo "CR lines: $(grep -c $'\r$' Makefile) of $(wc -l < Makefile)"
```

Expected:
```
184:                   tests/test_icons.c tests/test_local_pty.c tests/conpty_expect.c tests/win_runner.c
CR lines: 229 of 229
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "^\[(PASS|FAIL)\] test_cpx|^\[FAIL\]|^Tests Run"`
Expected: eight `[PASS] test_cpx_...` lines and `Tests Run: 2965, Failed: 0`.

Then check both compilers with the strict flags:

```bash
cd /home/thomas/nutshell-wt/conpty-resize && \
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -D_POSIX_C_SOURCE=200809L -Itests -c tests/conpty_expect.c -o /dev/null && \
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -D_POSIX_C_SOURCE=200809L -Itests -c tests/test_conpty_expect.c -o /dev/null && \
wineserver -k; xvfb-run -a make wintest > build/wintest.log 2>&1; grep -E "^\[FAIL\]|Tests Run" build/wintest.log
```

Expected: no compiler output, then `[pty child] Tests Run: 7, Failed: 0` and `Tests Run: 9, Failed: 0` (the wintest build now compiles `tests/conpty_expect.c` with `-Werror -Wconversion`).

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && git status --short && \
git add tests/conpty_expect.h tests/conpty_expect.c tests/test_conpty_expect.c tests/runner.c Makefile && \
git commit -F - <<'EOF'
test(conpty-resize): probe scenarios and the section 1.3 prediction

Four scenarios for the console probe (grow; shrink with the cursor at
the bottom; shrink with content below the cursor; a width change), and
a pure helper that lays each out the way conhost shows it and predicts
the viewport and cursor the spec's top-anchored rule gives after the
resize. The parity case compares conhost's own buffer with this, since
section 1 is not built yet. Native tests pin every prediction by hand.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 2: `pcon_flags` and a raw tap on the ConPTY transport

**Files:**
- Modify: `src/ui/local_pty.h:23-25` (after `LOCAL_PTY_FIRST_MAX`), `:37-40` (`local_pty_open_ex`), `:63` (before `local_pty_first_bytes`)
- Modify: `src/ui/local_pty.c:119` (struct), `:321-329` (`local_pty_open`/`local_pty_open_ex`), `:395` (`CreatePseudoConsole` call), `:571-572` (poll), `:767` (new function before `local_pty_first_bytes`)
- Modify: `src/ui/window.c:1149`
- Test: `tests/test_local_pty.c` (append above the final `#endif /* _WIN32 */`, line 749), `tests/win_runner.c:28-29`, `:69`, `:87-89`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces (used by Tasks 3 and 4): `PSEUDOCONSOLE_RESIZE_QUIRK` (0x2, in `local_pty.h`); `LocalPty *local_pty_open_ex(const LocalShellSpec *spec, int cols, int rows, unsigned log_id, unsigned long pcon_flags, char *err, size_t err_size)`; `void local_pty_set_raw_tap(LocalPty *pty, FILE *tap)`. `local_pty_open()` keeps its signature and passes 0.

- [ ] **Step 1: Write the failing test**

In `tests/test_local_pty.c`, replace the file's last line `#endif /* _WIN32 */` with:

```c

/* ---- 8. CreatePseudoConsole flags and the raw tap ---------------------
 *
 * local_pty_open_ex() hands pcon_flags to CreatePseudoConsole, and the raw
 * tap sees every byte poll() feeds the emulator. A host whose ConPTY
 * refuses PSEUDOCONSOLE_RESIZE_QUIRK fails the open; that is reported and
 * passes, as a missing ConPTY does in case 1. */
int test_local_pty_open_ex_passes_pcon_flags(void)
{
    TEST_BEGIN();

    char tmp_dir[MAX_PATH];
    DWORD tn = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    ASSERT_TRUE(tn > 0u && tn < sizeof(tmp_dir));
    char tap_path[MAX_PATH * 2];
    (void)snprintf(tap_path, sizeof(tap_path), "%snsprobe_tap_check.bin",
                   tmp_dir);

    static const unsigned long FLAGS[2] = { 0ul, PSEUDOCONSOLE_RESIZE_QUIRK };
    for (int f = 0; f < 2; f++) {
        LocalShellSpec spec;
        spec_for(&spec, "cmd.exe /c echo nutshell-flags");
        char err[512];
        err[0] = '\0';
        LocalPty *pty = local_pty_open_ex(&spec, 80, 24, 0u, FLAGS[f],
                                          err, sizeof(err));
        if (!pty) {
            RPT("  [skip] flags 0x%lx: local_pty_open_ex failed: %s\n",
                FLAGS[f], err);
            ASSERT_TRUE(err[0] != '\0');
            continue;
        }
        FILE *tap = fopen(tap_path, "w+b");
        ASSERT_NOT_NULL(tap);
        local_pty_set_raw_tap(pty, tap);

        Terminal *term = term_init(24, 80, 200);
        ASSERT_NOT_NULL(term);
        int seen = wait_for_screen(pty, term, "nutshell-flags", 5000);
        local_pty_close(pty);

        static char raw[65536];
        size_t got = 0u;
        if (tap) {
            (void)fseek(tap, 0L, SEEK_SET);
            got = fread(raw, 1u, sizeof(raw) - 1u, tap);
            raw[got] = '\0';
            fclose(tap);
        }
        (void)DeleteFileA(tap_path);
        RPT("  [flags 0x%lx] echo %s; tap holds %u bytes\n", FLAGS[f],
            seen ? "seen" : "NOT seen", (unsigned)got);
        ASSERT_TRUE(seen);
        ASSERT_TRUE(contains(raw, got, "nutshell-flags"));
        term_free(term);
    }
    TEST_END();
}

#endif /* _WIN32 */
```

In `tests/win_runner.c`, three edits.

Replace:
```c
int test_key_layout_records_ctrl_space(void);
int nspty_key_dump_main(void);
```
with:
```c
int test_key_layout_records_ctrl_space(void);
int test_local_pty_open_ex_passes_pcon_flags(void);
int nspty_key_dump_main(void);
```

Replace `#define PTY_CASE_COUNT 7` with `#define PTY_CASE_COUNT 8`.

Replace:
```c
        { "test_key_layout_records_ctrl_space",
          test_key_layout_records_ctrl_space },
    };
```
with:
```c
        { "test_key_layout_records_ctrl_space",
          test_key_layout_records_ctrl_space },
        { "test_local_pty_open_ex_passes_pcon_flags",
          test_local_pty_open_ex_passes_pcon_flags },
    };
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make wintest 2>&1 | grep -E "error:" | head -8`
Expected: the build fails in `tests/test_local_pty.c` with, among others, `'PSEUDOCONSOLE_RESIZE_QUIRK' undeclared`, `too many arguments to function 'local_pty_open_ex'` and `implicit declaration of function 'local_pty_set_raw_tap'`.

- [ ] **Step 3: Write the implementation**

`src/ui/local_pty.h`: replace

```c
#define LOCAL_PTY_FIRST_MAX ((size_t)512)
```
with:
```c
#define LOCAL_PTY_FIRST_MAX ((size_t)512)

/* CreatePseudoConsole's flag for a terminal that reflows its own buffer on a
 * resize (docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md
 * section 0). The MinGW headers this tree builds against do not define it;
 * the value is the one Windows Terminal passes. local_pty_open() does not
 * pass it: the console probe (tests/test_local_pty.c) decides. */
#ifndef PSEUDOCONSOLE_RESIZE_QUIRK
#define PSEUDOCONSOLE_RESIZE_QUIRK 0x2
#endif
```

replace

```c
/* The same, with the session's debug-log id (the "s3" in a log line; 0 for
 * none) so the shell.* events and the reader thread's lines carry it. */
LocalPty *local_pty_open_ex(const LocalShellSpec *spec, int cols, int rows,
                            unsigned log_id, char *err, size_t err_size);
```
with:
```c
/* The same, with the session's debug-log id (the "s3" in a log line; 0 for
 * none) so the shell.* events and the reader thread's lines carry it, and
 * the flags CreatePseudoConsole gets (a DWORD: 0, or
 * PSEUDOCONSOLE_RESIZE_QUIRK; local_pty_open() passes 0). */
LocalPty *local_pty_open_ex(const LocalShellSpec *spec, int cols, int rows,
                            unsigned log_id, unsigned long pcon_flags,
                            char *err, size_t err_size);
```

and replace

```c
/* Copy out the first bytes ConPTY emitted on open (up to
```
with:
```c
/* Every byte poll() feeds the emulator is also written, unchanged, to tap
 * (NULL stops it) and flushed: the Windows harness's fixture recorder.
 * The caller owns the FILE and closes it after local_pty_close(). */
void local_pty_set_raw_tap(LocalPty *pty, FILE *tap);

/* Copy out the first bytes ConPTY emitted on open (up to
```

`src/ui/local_pty.c`: replace

```c
    unsigned log_id;       /* the session's debug-log id; 0 = none            */
};
```
with:
```c
    unsigned log_id;       /* the session's debug-log id; 0 = none            */
    FILE   *raw_tap;       /* local_pty_set_raw_tap(); NULL = none            */
};
```

replace

```c
    return local_pty_open_ex(spec, cols, rows, 0u, err, err_size);
}

LocalPty *local_pty_open_ex(const LocalShellSpec *spec, int cols, int rows,
                            unsigned log_id, char *err, size_t err_size)
{
```
with:
```c
    return local_pty_open_ex(spec, cols, rows, 0u, 0ul, err, err_size);
}

LocalPty *local_pty_open_ex(const LocalShellSpec *spec, int cols, int rows,
                            unsigned log_id, unsigned long pcon_flags,
                            char *err, size_t err_size)
{
```

replace

```c
        HRESULT hr = g_create_pcon(size, in_read, out_write, 0, &p->hpc);
```
with:
```c
        HRESULT hr = g_create_pcon(size, in_read, out_write,
                                   (DWORD)pcon_flags, &p->hpc);
```

replace

```c
        if (got > 0u) {
            term_process(term, buf, got);
```
with:
```c
        if (got > 0u) {
            if (p->raw_tap) {
                (void)fwrite(buf, 1u, got, p->raw_tap);
                (void)fflush(p->raw_tap);
            }
            term_process(term, buf, got);
```

and replace

```c
size_t local_pty_first_bytes(const LocalPty *pty, char *out, size_t out_size)
```
with:
```c
void local_pty_set_raw_tap(LocalPty *pty, FILE *tap)
{
    if (pty) pty->raw_tap = tap;
}

size_t local_pty_first_bytes(const LocalPty *pty, char *out, size_t out_size)
```

(`p` is `calloc`ed, so `raw_tap` starts NULL.)

`src/ui/window.c:1149`: replace

```c
            pty = local_pty_open_ex(spec, cols, rows, s->log_id, err, sizeof(err));
```
with:
```c
            pty = local_pty_open_ex(spec, cols, rows, s->log_id, 0ul,
                                    err, sizeof(err));
```

- [ ] **Step 4: Run the tests to verify they pass**

Compile the two product objects under the release flags (no binary, so no version bump), then delete them:

```bash
cd /home/thomas/nutshell-wt/conpty-resize && make src/ui/window.o src/ui/local_pty.o 2>&1 | grep -E "error|warning"; rm -f src/ui/window.o src/ui/local_pty.o; echo compiled
```
Expected: only `compiled`.

```bash
cd /home/thomas/nutshell-wt/conpty-resize && wineserver -k; xvfb-run -a make wintest > build/wintest.log 2>&1; grep -E "^\[FAIL\]|Tests Run|\[flags" build/wintest.log
```
Expected:
```
  [flags 0x0] echo seen; tap holds 38 bytes
  [flags 0x2] echo seen; tap holds 38 bytes
[pty child] Tests Run: 8, Failed: 0
Tests Run: 10, Failed: 0
```
(The byte count is Wine's; on Windows it differs. A host that refuses 0x2 prints `[skip] flags 0x2: local_pty_open_ex failed: ...` and still passes.)

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `Tests Run: 2965, Failed: 0`

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && git status --short && \
git add src/ui/local_pty.h src/ui/local_pty.c src/ui/window.c tests/test_local_pty.c tests/win_runner.c && \
git commit -F - <<'EOF'
feat(local-pty): CreatePseudoConsole flags and a raw byte tap

local_pty_open_ex() takes the flags it hands to CreatePseudoConsole, so
the console probe can open a second run with PSEUDOCONSOLE_RESIZE_QUIRK;
local_pty_open() and the app keep passing 0 until the probe's report
says otherwise. A raw tap writes every byte poll() feeds the emulator to
a file, unstripped and uncut, which is what the resize fixtures need.
A new wintest case opens cmd with each flag and checks the echo reaches
both the screen and the tap.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 3: The `--console-probe` child and its dump

**Files:**
- Modify: `tests/test_local_pty.c:34-36` (includes) and append above the final `#endif /* _WIN32 */`
- Modify: `tests/win_runner.c` (declarations, `KEY_DUMP_FLAG` block at line 68-69, the case table, `main()` at lines 159-160)

**Interfaces:**
- Consumes: Task 1's `CpxScenario`, `CpxView`, `cpx_scenario_find()`, `cpx_initial_view()`, `cpx_view_diff()`; Task 2's `local_pty_open_ex(..., unsigned long pcon_flags, ...)`.
- Produces (used by Task 4), all `static` in `tests/test_local_pty.c` unless marked: `int nsprobe_console_main(const char *scenario, const char *dump_path)` (extern, called by `win_runner.c`); `static int probe_read_dump(const char *path, int n, CpxView *view)`; `static int probe_wait_dump(const char *path, int n, CpxView *view, int ms)`; `static void probe_command(char *out, size_t out_size, const char *scenario, const char *dump_path)`; `static void model_view(const Terminal *term, CpxView *v)`; `static int report_views(const char *who, const char *label, const CpxView *want, const CpxView *got)`. The flag string is `--console-probe` (`CONSOLE_PROBE_FLAG` in `win_runner.c`).

- [ ] **Step 1: Write the failing test**

In `tests/test_local_pty.c`, replace

```c
#include "dlog.h"
#include <stdio.h>
#include <string.h>
```
with:
```c
#include "dlog.h"
#include "conpty_expect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
```

and replace the file's last line `#endif /* _WIN32 */` with the block below: the probe child, the dump reader and the comparison helpers (test code, `static`, so they come before the case that uses them), then case 9:

```c

/* ---- 9-10. the console probe (resize spec section 0) -------------------
 *
 * docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md, section
 * 0 and Tests, "Probe" and "Fixtures". `test_runner.exe --console-probe
 * <scenario> <dumpfile>` (tests/win_runner.c) runs as the pseudo-console's
 * own process: it writes a scenario from tests/conpty_expect.c to its
 * console, puts the cursor where the scenario says, appends dump 0 to the
 * dump file, and then on each 'D' appends the next dump -- every buffer
 * row (GetConsoleScreenBufferInfo, ReadConsoleOutputCharacterW) and the
 * cursor -- and on 'Q' exits. Nothing is typed as a command.
 *
 * Dump format, one block per dump, rows trimmed of trailing spaces:
 *   DUMP <n>
 *   size=<X>x<Y> window=<left>,<top>,<right>,<bottom> cursor=<row>,<col>
 *   R<row, 4 digits>|<text>|        (one per buffer row)
 *   END <n>
 */

static void probe_write(HANDLE out, const char *text)
{
    DWORD wrote = 0;
    (void)WriteConsoleA(out, text, (DWORD)strlen(text), &wrote, NULL);
}

static void probe_dump(HANDLE out, const char *path, int n)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if (!GetConsoleScreenBufferInfo(out, &bi)) return;
    FILE *f = fopen(path, "ab");
    if (!f) return;
    fprintf(f, "DUMP %d\n", n);
    fprintf(f, "size=%dx%d window=%d,%d,%d,%d cursor=%d,%d\n",
            (int)bi.dwSize.X, (int)bi.dwSize.Y,
            (int)bi.srWindow.Left, (int)bi.srWindow.Top,
            (int)bi.srWindow.Right, (int)bi.srWindow.Bottom,
            (int)bi.dwCursorPosition.Y, (int)bi.dwCursorPosition.X);
    /* Every buffer row while the buffer is small (under ConPTY it is the
     * viewport); only the window's rows if it is not. */
    int first = 0;
    int last = (int)bi.dwSize.Y - 1;
    if (bi.dwSize.Y > 200) {
        first = (int)bi.srWindow.Top;
        last = (int)bi.srWindow.Bottom;
    }
    int width = (int)bi.dwSize.X;
    if (width > 512) width = 512;
    for (int y = first; y <= last; y++) {
        WCHAR wrow[512];
        char row[513];
        DWORD got = 0;
        COORD at;
        at.X = 0;
        at.Y = (SHORT)y;
        if (!ReadConsoleOutputCharacterW(out, wrow, (DWORD)width, at, &got))
            got = 0;
        int k = 0;
        for (DWORD i = 0; i < got && k < 512; i++) {
            WCHAR w = wrow[i];
            row[k++] = (w >= 0x20 && w < 0x7F) ? (char)w : (w == 0 ? ' ' : '?');
        }
        while (k > 0 && row[k - 1] == ' ') k--;
        row[k] = '\0';
        fprintf(f, "R%04d|%s|\n", y, row);
    }
    fprintf(f, "END %d\n", n);
    fclose(f);
}

/* The probe child: `test_runner.exe --console-probe <scenario> <dumpfile>`
 * lands here (tests/win_runner.c). Returns 0 on 'Q', 2 for an unknown
 * scenario, 3 when no 'Q' came within a minute. */
int nsprobe_console_main(const char *scenario, const char *dump_path)
{
    const CpxScenario *s = cpx_scenario_find(scenario);
    if (!s || !dump_path) return 2;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE in  = GetStdHandle(STD_INPUT_HANDLE);
    /* No processed, line or echo input: 'D' and 'Q' are plain keys. */
    (void)SetConsoleMode(in, ENABLE_EXTENDED_FLAGS);

    for (int i = 0; i < s->line_count; i++) {
        if (i > 0) probe_write(out, "\r\n");
        probe_write(out, s->lines[i]);
    }
    /* The cursor where the scenario puts it -- for shrink_below, back up
     * above the content written below it. */
    CpxView v;
    if (cpx_initial_view(s, &v) == 0) {
        CONSOLE_SCREEN_BUFFER_INFO bi;
        if (GetConsoleScreenBufferInfo(out, &bi)) {
            COORD at;
            at.X = (SHORT)v.cursor_col;
            at.Y = (SHORT)(bi.srWindow.Top + v.cursor_row);
            (void)SetConsoleCursorPosition(out, at);
        }
    }

    int dumps = 0;
    probe_dump(out, dump_path, dumps++);   /* dump 0: the ready signal */

    ULONGLONG deadline = GetTickCount64() + 60000u;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(in, 200u) != WAIT_OBJECT_0) continue;
        INPUT_RECORD rec[16];
        DWORD n = 0;
        if (!ReadConsoleInputW(in, rec, 16u, &n)) break;
        for (DWORD i = 0; i < n; i++) {
            if (rec[i].EventType != KEY_EVENT) continue;
            if (!rec[i].Event.KeyEvent.bKeyDown) continue;
            WCHAR ch = rec[i].Event.KeyEvent.uChar.UnicodeChar;
            if (ch == L'D') probe_dump(out, dump_path, dumps++);
            else if (ch == L'Q') return 0;
        }
    }
    return 3;
}

/* Dump n from the probe's file: the window's rows into view, the cursor
 * relative to the window's top. Returns 1 once that dump is complete. */
static int probe_read_dump(const char *path, int n, CpxView *view)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char head[32];
    (void)snprintf(head, sizeof(head), "DUMP %d", n);
    char line[600];
    int in_dump = 0, done = 0;
    int bx = 0, by = 0, wl = 0, wt = 0, wr = 0, wb = 0, cy = 0, cx = 0;
    memset(view, 0, sizeof(*view));
    while (fgets(line, (int)sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!in_dump) {
            if (strcmp(line, head) == 0) in_dump = 1;
            continue;
        }
        if (strncmp(line, "size=", 5) == 0) {
            if (sscanf(line, "size=%dx%d window=%d,%d,%d,%d cursor=%d,%d",
                       &bx, &by, &wl, &wt, &wr, &wb, &cy, &cx) != 8) break;
            view->rows = wb - wt + 1;
            view->cols = wr - wl + 1;
            view->cursor_row = cy - wt;
            view->cursor_col = cx;
            continue;
        }
        if (line[0] == 'R') {
            char *bar = strchr(line, '|');
            char *end = strrchr(line, '|');
            if (!bar || end == bar) continue;
            *end = '\0';
            int vr = atoi(line + 1) - wt;
            if (vr >= 0 && vr < view->rows && vr < CPX_MAX_ROWS)
                (void)snprintf(view->text[vr], sizeof(view->text[vr]),
                               "%.80s", bar + 1);
            continue;
        }
        if (strncmp(line, "END ", 4) == 0) {
            done = (atoi(line + 4) == n);
            break;
        }
    }
    fclose(f);
    (void)bx;
    (void)by;
    return done;
}

static int probe_wait_dump(const char *path, int n, CpxView *view, int ms)
{
    for (int waited = 0; waited <= ms; waited += 50) {
        if (probe_read_dump(path, n, view)) return 1;
        Sleep(50);
    }
    return 0;
}

/* The probe child's command line, for spec_for(). */
static void probe_command(char *out, size_t out_size, const char *scenario,
                          const char *dump_path)
{
    char self[MAX_PATH];
    (void)GetModuleFileNameA(NULL, self, (DWORD)sizeof(self));
    (void)snprintf(out, out_size, "\"%s\" --console-probe %s \"%s\"", self,
                   scenario, dump_path);
}

/* The model's screen in the same shape as a dump, so the two compare. */
static void model_view(const Terminal *term, CpxView *v)
{
    memset(v, 0, sizeof(*v));
    v->rows = term->rows < CPX_MAX_ROWS ? term->rows : CPX_MAX_ROWS;
    v->cols = term->cols;
    v->cursor_row = term->cursor.row;
    v->cursor_col = term->cursor.col;
    for (int r = 0; r < v->rows; r++) {
        const TermRow *row = term->lines[term_screen_to_phys(term, r)];
        int n = row->len < term->cols ? row->len : term->cols;
        if (n > CPX_MAX_COLS) n = CPX_MAX_COLS;
        int k = 0;
        for (int c = 0; c < n; c++) {
            uint32_t cp = row->cells[c].codepoint;
            v->text[r][k++] = (cp >= 0x20u && cp < 0x7Fu) ? (char)cp
                            : (cp == 0u ? ' ' : '?');
        }
        while (k > 0 && v->text[r][k - 1] == ' ') k--;
        v->text[r][k] = '\0';
    }
}

/* Print two views side by side; returns cpx_view_diff(). */
static int report_views(const char *who, const char *label,
                        const CpxView *want, const CpxView *got)
{
    int diff = cpx_view_diff(want, got);
    RPT("  [%s] %s: %s (%d difference%s)\n", who, label,
        diff ? "DISAGREES" : "AGREES", diff, diff == 1 ? "" : "s");
    int rows = want->rows > got->rows ? want->rows : got->rows;
    if (rows > CPX_MAX_ROWS) rows = CPX_MAX_ROWS;
    for (int r = 0; r < rows; r++) {
        const char *a = (r < want->rows) ? want->text[r] : "";
        const char *b = (r < got->rows) ? got->text[r] : "";
        RPT("    %c %2d want |%s| got |%s|\n", strcmp(a, b) ? '*' : ' ', r,
            a, b);
    }
    RPT("    %c cursor want %d,%d got %d,%d; size want %dx%d got %dx%d\n",
        (want->cursor_row != got->cursor_row ||
         want->cursor_col != got->cursor_col) ? '*' : ' ',
        want->cursor_row, want->cursor_col, got->cursor_row, got->cursor_col,
        want->cols, want->rows, got->cols, got->rows);
    return diff;
}

/* 9. The probe child runs under a pseudo-console, writes its scenario, and
 * its dump 0 reads back at the size it was opened at, with the text the
 * scenario wrote. */
int test_local_pty_console_probe_dumps(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("grow");
    ASSERT_NOT_NULL(s);
    if (!s) TEST_END();

    char tmp_dir[MAX_PATH];
    DWORD tn = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    ASSERT_TRUE(tn > 0u && tn < sizeof(tmp_dir));
    char dump_path[MAX_PATH * 2];
    (void)snprintf(dump_path, sizeof(dump_path), "%snsprobe_smoke.txt",
                   tmp_dir);
    (void)DeleteFileA(dump_path);

    char cmd[MAX_PATH * 6];
    probe_command(cmd, sizeof(cmd), s->name, dump_path);
    LocalShellSpec spec;
    spec_for(&spec, cmd);
    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open_ex(&spec, s->old_cols, s->old_rows, 0u,
                                      0ul, err, sizeof(err));
    if (!pty) {
        RPT("  [skip] local_pty_open_ex failed: %s\n", err);
        ASSERT_TRUE(err[0] != '\0');
        TEST_END();
    }
    Terminal *term = term_init(s->old_rows, s->old_cols, 500);
    ASSERT_NOT_NULL(term);

    CpxView got, want, model;
    int ok = probe_wait_dump(dump_path, 0, &got, 10000);
    ASSERT_TRUE(ok);
    drain(pty, term, 800);
    ASSERT_EQ(cpx_initial_view(s, &want), 0);
    if (ok) {
        ASSERT_EQ(got.rows, s->old_rows);
        ASSERT_EQ(got.cols, s->old_cols);
        int found = 0;
        for (int r = 0; r < got.rows; r++)
            if (strcmp(got.text[r], "<20> alpha") == 0) found = 1;
        ASSERT_TRUE(found);
        (void)report_views("smoke", "dump 0: conhost vs scenario layout",
                           &want, &got);
        model_view(term, &model);
        (void)report_views("smoke", "dump 0: conhost vs model", &model, &got);
    }

    (void)local_pty_write(pty, "Q", 1);
    drain(pty, term, 300);
    local_pty_close(pty);
    term_free(term);
    (void)DeleteFileA(dump_path);
    TEST_END();
}

#endif /* _WIN32 */
```

In `tests/win_runner.c`, four edits.

Replace:
```c
int test_local_pty_open_ex_passes_pcon_flags(void);
int nspty_key_dump_main(void);
```
with:
```c
int test_local_pty_open_ex_passes_pcon_flags(void);
int test_local_pty_console_probe_dumps(void);
int nspty_key_dump_main(void);
int nsprobe_console_main(const char *scenario, const char *dump_path);
```

Replace:
```c
#define KEY_DUMP_FLAG  "--dump-keys"
#define PTY_CASE_COUNT 8
```
with:
```c
#define KEY_DUMP_FLAG  "--dump-keys"
/* test_local_pty_console_probe_dumps (and the resize parity case) run this
 * executable again as the console program whose buffer they read back. */
#define CONSOLE_PROBE_FLAG "--console-probe"
#define PTY_CASE_COUNT 9
```

Replace:
```c
        { "test_local_pty_open_ex_passes_pcon_flags",
          test_local_pty_open_ex_passes_pcon_flags },
    };
```
with:
```c
        { "test_local_pty_open_ex_passes_pcon_flags",
          test_local_pty_open_ex_passes_pcon_flags },
        { "test_local_pty_console_probe_dumps",
          test_local_pty_console_probe_dumps },
    };
```

Replace:
```c
        return nspty_key_dump_main();
```
with:
```c
        return nspty_key_dump_main();
    if (argc >= 4 && strcmp(argv[1], CONSOLE_PROBE_FLAG) == 0)
        return nsprobe_console_main(argv[2], argv[3]);
```

- [ ] **Step 2: Run it to verify the build and the case**

The child, the reader and the case land together, so the first run is the real one; the check that it can fail is the dump assertion, which an unknown scenario name or a wrong dump path trips (see Step 4's negative check).

Run: `cd /home/thomas/nutshell-wt/conpty-resize && wineserver -k; xvfb-run -a make wintest > build/wintest.log 2>&1; grep -E "error:|^\[FAIL\]|Tests Run|\[smoke\] dump 0" build/wintest.log`
Expected:
```
  [smoke] dump 0: conhost vs scenario layout: AGREES (0 differences)
  [smoke] dump 0: conhost vs model: AGREES (0 differences)
[pty child] Tests Run: 9, Failed: 0
Tests Run: 11, Failed: 0
```
Any `error:` line is a compiler error to fix before going on.

- [ ] **Step 3: Negative check that the case can fail**

Temporarily change `cpx_scenario_find("grow")` in `test_local_pty_console_probe_dumps` to `cpx_scenario_find("growx")`, rerun the Step 2 command, and confirm `[FAIL] test_local_pty_console_probe_dumps` and `[pty child] Tests Run: 9, Failed: 1`. Restore `"grow"`.

- [ ] **Step 4: Run everything to verify it passes**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && wineserver -k; xvfb-run -a make wintest > build/wintest.log 2>&1; grep -E "^\[FAIL\]|Tests Run" build/wintest.log`
Expected: `[pty child] Tests Run: 9, Failed: 0` and `Tests Run: 11, Failed: 0`.

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `Tests Run: 2965, Failed: 0`

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && git status --short && \
git add tests/test_local_pty.c tests/win_runner.c && \
git commit -F - <<'EOF'
test(local-pty): a console probe that dumps conhost's own buffer

test_runner.exe --console-probe <scenario> <dumpfile> runs as the
pseudo-console's own process: it writes one of the probe scenarios,
puts the cursor where the scenario says, and on each 'D' appends every
buffer row and the cursor to the dump file, read through
ReadConsoleOutputCharacterW. A smoke case opens it under ConPTY and
checks dump 0 has the size and text the scenario wrote.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 4: The resize parity case and the fixture recorder

**Files:**
- Modify: `tests/test_local_pty.c` (append above the final `#endif /* _WIN32 */`)
- Modify: `tests/win_runner.c` (declaration, `PTY_CASE_COUNT`, case table, the child's wait at line 134)
- Modify: `tests/test_term_conpty.c` (append at the end, after line 184)
- Modify: `tests/runner.c` (CRLF)

**Interfaces:**
- Consumes: Task 1's whole `cpx_*` API; Task 2's `local_pty_open_ex()`, `PSEUDOCONSOLE_RESIZE_QUIRK`, `local_pty_set_raw_tap()`; Task 3's `probe_wait_dump()`, `probe_command()`, `model_view()`, `report_views()` and the existing `spec_for()`, `drain()`, `print_escaped()`, `RPT()`.
- Produces: the wintest case `int test_local_pty_resize_parity(void)`; the native test `int test_term_conpty_resize_marker_is_inert(void)`; on a Windows run with `NSPROBE_RECORD=1`, the files `tests/fixtures/conpty_resize_grow.bin`, `..._shrink_bottom.bin`, `..._shrink_below.bin`, `..._width.bin`, each the raw byte stream from open to the end of the post-resize drain with `\x1b]9999;resize=<new_cols>x<new_rows>\x07` at the point `ResizePseudoConsole` was called (slice 3's replay reads these).

- [ ] **Step 1: Write the native marker test**

Append to `tests/test_term_conpty.c`:

```c
/* The resize fixtures (tests/fixtures/conpty_resize_*.bin, recorded by the
 * wintest case test_local_pty_resize_parity) carry an in-band marker,
 * ESC ] 9999 ; resize=COLSxROWS BEL, where each ResizePseudoConsole was
 * made, so a replay knows the model's size at that point. The emulator must
 * ignore it: no text, no cursor move, no title. */
int test_term_conpty_resize_marker_is_inert(void)
{
    TEST_BEGIN();
    static const char MARK[] = "\x1b]9999;resize=25x12\x07";
    Terminal *plain  = term_init(10, 40, 100);
    Terminal *marked = term_init(10, 40, 100);
    ASSERT_NOT_NULL(plain);
    ASSERT_NOT_NULL(marked);

    term_process(plain, "abc\r\ndef", 8);
    term_process(marked, "abc\r\ndef", 8);
    term_process(marked, MARK, sizeof(MARK) - 1u);
    term_process(plain, "ghi", 3);
    term_process(marked, "ghi", 3);

    char a[1024], b[1024];
    (void)term_extract_last_n(plain, 10, a, sizeof(a));
    (void)term_extract_last_n(marked, 10, b, sizeof(b));
    ASSERT_STR_EQ(a, b);
    ASSERT_TRUE(strstr(b, "9999") == NULL);
    ASSERT_TRUE(strstr(b, "resize") == NULL);
    ASSERT_EQ(marked->cursor.row, plain->cursor.row);
    ASSERT_EQ(marked->cursor.col, plain->cursor.col);
    ASSERT_STR_EQ(marked->title, plain->title);

    term_free(plain);
    term_free(marked);
    TEST_END();
}
```

Register it (CRLF, by bytes):

```bash
cd /home/thomas/nutshell-wt/conpty-resize && python3 - test_term_conpty_opening_sequence_is_consumed "" \
  test_term_conpty_resize_marker_is_inert <<'EOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'EOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
EOF
```

Expected: `runner.c: registered 1 tests after test_term_conpty_opening_sequence_is_consumed`

- [ ] **Step 2: Run it**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "marker_is_inert|^\[FAIL\]|^Tests Run"`
Expected: `[PASS] test_term_conpty_resize_marker_is_inert` and `Tests Run: 2966, Failed: 0`. It passes at once: it pins what the fixture format relies on (`process_osc()` in `src/term/parser.c:294-319` acts on codes 0 and 2 only). Check that it can fail: change `MARK` to `"\x1b]2;resize=25x12\x07"` (a title OSC), rerun, see `[FAIL] test_term_conpty_resize_marker_is_inert`, restore `9999`.

- [ ] **Step 3: Write the parity case**

In `tests/test_local_pty.c`, replace the file's last line `#endif /* _WIN32 */` with:

```c

/* ---- 10. resize parity: conhost's own buffer against the spec's rule ----
 *
 * For each scenario, opened once with flags 0 (what local_pty_open() passes
 * today) and once with PSEUDOCONSOLE_RESIZE_QUIRK:
 *   - drain the scenario into a Terminal at the old size;
 *   - term_resize() and local_pty_resize() to the new size;
 *   - snapshot the model BEFORE draining ConPTY's post-resize bytes, and
 *     check the resize kept every scenario line in scrollback plus screen
 *     as often as before (exactly once, on Windows);
 *   - send 'D' and compare conhost's dump with the spec's section 1.3
 *     prediction (tests/conpty_expect.c) and with the model snapshot;
 *   - drain and print the raw post-resize bytes.
 * Section 1 (top mode) is not built yet, so the model is today's
 * bottom-anchored one: what it is compared against is the rule, computed
 * from the scenario text. The comparisons are the report, printed and not
 * asserted -- like cases 4-7, they are facts about this Windows build.
 *
 * NSPROBE_RECORD=1 in the environment (on Windows, never under Wine) also
 * writes tests/fixtures/conpty_resize_<scenario>.bin from the flags-0 run:
 * every byte poll() drained, with \e]9999;resize=COLSxROWS\a where the
 * resize was made. */

/* Wine's ntdll exports wine_get_version; Windows's does not. Under Wine
 * the probe's conhost results are Wine's, so they are printed, never taken
 * as the report, and no fixture is recorded from them. */
static int running_under_wine(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    if (!nt) return 0;
    FARPROC fn = GetProcAddress(nt, "wine_get_version");
    return fn ? 1 : 0;
}

static int count_in(const char *hay, const char *needle)
{
    int count = 0;
    size_t nl = strlen(needle);
    if (nl == 0u) return 0;
    for (const char *p = strstr(hay, needle); p; p = strstr(p + nl, needle))
        count++;
    return count;
}

/* Every logical line the model holds, scrollback and screen, one per text
 * line (wrapped rows joined). */
static void model_history(const Terminal *term, char *out, size_t out_size)
{
    size_t n = 0u;
    for (int i = 0; i < term->lines_count; i++) {
        const TermRow *row =
            term->lines[(term->lines_start + i) % term->lines_capacity];
        if (i > 0 && !row->wrapped && n + 1u < out_size) out[n++] = '\n';
        int len = row->len < term->cols ? row->len : term->cols;
        for (int c = 0; c < len && n + 1u < out_size; c++) {
            uint32_t cp = row->cells[c].codepoint;
            out[n++] = (cp >= 0x20u && cp < 0x7Fu) ? (char)cp : ' ';
        }
    }
    out[n] = '\0';
}

static void count_tags(const CpxScenario *s, const Terminal *term,
                       int counts[CPX_MAX_LINES])
{
    static char hist[65536];
    model_history(term, hist, sizeof(hist));
    for (int i = 0; i < s->line_count; i++) {
        char tag[16];
        counts[i] = (cpx_tag(s->lines[i], tag, sizeof(tag)) > 0u)
                  ? count_in(hist, tag) : -1;
    }
}

/* Copy the whole tap file to path (the fixture). Returns 1 on success. */
static int copy_tap(FILE *tap, const char *path)
{
    static char buf[262144];
    if (fseek(tap, 0L, SEEK_SET) != 0) return 0;
    size_t got = fread(buf, 1u, sizeof(buf), tap);
    FILE *out = fopen(path, "wb");
    if (!out) return 0;
    size_t put = fwrite(buf, 1u, got, out);
    fclose(out);
    return put == got;
}

/* One scenario under one flag set. Returns 0, or 1 when a check failed. */
static int probe_scenario(const CpxScenario *s, unsigned long flags,
                          int on_wine, int record)
{
    int _tf_local_fail = 0;
    char who[64];
    (void)snprintf(who, sizeof(who), "%s/%s", s->name,
                   flags ? "quirk" : "flags0");

    char tmp_dir[MAX_PATH];
    DWORD tn = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    if (tn == 0u || tn >= sizeof(tmp_dir)) {
        RPT("  [%s] no temp directory\n", who);
        return 1;
    }
    char dump_path[MAX_PATH * 2], tap_path[MAX_PATH * 2];
    (void)snprintf(dump_path, sizeof(dump_path), "%snsprobe_%s_%lu.txt",
                   tmp_dir, s->name, flags);
    (void)snprintf(tap_path, sizeof(tap_path), "%snsprobe_%s_%lu.bin",
                   tmp_dir, s->name, flags);
    (void)DeleteFileA(dump_path);

    char cmd[MAX_PATH * 6];
    probe_command(cmd, sizeof(cmd), s->name, dump_path);
    LocalShellSpec spec;
    spec_for(&spec, cmd);
    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open_ex(&spec, s->old_cols, s->old_rows, 0u,
                                      flags, err, sizeof(err));
    if (!pty) {
        RPT("  [%s] [skip] local_pty_open_ex failed: %s\n", who, err);
        ASSERT_TRUE(err[0] != '\0');
        return _tf_local_fail;
    }
    FILE *tap = fopen(tap_path, "w+b");
    if (tap) local_pty_set_raw_tap(pty, tap);
    Terminal *term = term_init(s->old_rows, s->old_cols, 500);
    ASSERT_NOT_NULL(term);
    if (!term) {
        local_pty_close(pty);
        if (tap) fclose(tap);
        return 1;
    }

    /* The scenario, drained at the old size. */
    static CpxView want0, got0, want1, got1, model1, model2;
    int ok0 = probe_wait_dump(dump_path, 0, &got0, 10000);
    ASSERT_TRUE(ok0);
    drain(pty, term, 800);
    if (ok0 && cpx_initial_view(s, &want0) == 0)
        (void)report_views(who, "before the resize: conhost vs scenario layout",
                           &want0, &got0);
    int before[CPX_MAX_LINES], after[CPX_MAX_LINES];
    count_tags(s, term, before);

    /* The resize, with the in-band marker where it was made. */
    long repaint_at = -1L;
    if (tap) {
        char mark[64];
        int mn = snprintf(mark, sizeof(mark), "\x1b]9999;resize=%dx%d\x07",
                          s->new_cols, s->new_rows);
        if (mn > 0) (void)fwrite(mark, 1u, (size_t)mn, tap);
        (void)fflush(tap);
        repaint_at = ftell(tap);
    }
    term_resize(term, s->new_rows, s->new_cols);
    int resized = local_pty_resize(pty, s->new_cols, s->new_rows);
    if (resized != 0)
        RPT("  [%s] local_pty_resize returned %d%s\n", who, resized,
            on_wine ? " (Wine's ResizePseudoConsole fails)" : "");
    if (!on_wine) ASSERT_EQ(resized, 0);

    /* The model before ConPTY's post-resize bytes reach it. */
    model_view(term, &model1);
    count_tags(s, term, after);
    for (int i = 0; i < s->line_count; i++) {
        if (before[i] < 0) continue;
        if (after[i] != before[i] || (!on_wine && after[i] != 1))
            RPT("  [%s] line %d (%s): %d before the resize, %d after\n", who,
                i, s->lines[i], before[i], after[i]);
        ASSERT_EQ(after[i], before[i]);
        if (!on_wine) ASSERT_EQ(after[i], 1);
    }

    /* conhost's buffer after the resize: retried until the dump shows the
     * new size, since ResizePseudoConsole and the 'D' key travel different
     * pipes. */
    int ok1 = 0, n = 1;
    for (int attempt = 0; attempt < 5 && !ok1; attempt++) {
        Sleep(300);
        (void)local_pty_write(pty, "D", 1);
        if (!probe_wait_dump(dump_path, n, &got1, 3000)) break;
        n++;
        ok1 = (got1.rows == s->new_rows && got1.cols == s->new_cols);
    }
    if (!ok1) {
        RPT("  [%s] conhost's dump never showed %dx%d (last read: %dx%d)%s\n",
            who, s->new_cols, s->new_rows, got1.cols, got1.rows,
            on_wine ? " -- Wine does not resize its conhost; nothing to "
                      "compare" : "");
        if (!on_wine) ASSERT_TRUE(ok1);
    } else {
        int rule = -1;
        if (cpx_expected_after(s, &want1) == 0)
            rule = report_views(who,
                "after the resize: conhost vs spec section 1.3 (top-anchored)",
                &want1, &got1);
        int model = report_views(who,
            "after the resize: conhost vs today's model before its repaint",
            &model1, &got1);
        RPT("  [%s] VERDICT rule=%s model=%s\n", who,
            rule == 0 ? "AGREES" : (rule < 0 ? "N/A" : "DISAGREES"),
            model == 0 ? "AGREES" : "DISAGREES");
    }

    /* ConPTY's post-resize bytes: is there a full repaint, and are
     * soft-wrapped rows sent without \r\n? */
    drain(pty, term, 1000);
    if (tap && repaint_at >= 0L) {
        static char raw[65536];
        (void)fflush(tap);
        long end = ftell(tap);
        size_t want_len = (size_t)(end - repaint_at);
        if (want_len > sizeof(raw)) want_len = sizeof(raw);
        size_t got = 0u;
        if (fseek(tap, repaint_at, SEEK_SET) == 0)
            got = fread(raw, 1u, want_len, tap);
        (void)fseek(tap, 0L, SEEK_END);
        char label[96];
        (void)snprintf(label, sizeof(label), "%s post-resize bytes", who);
        print_escaped(label, raw, got);
    }
    if (ok1) {
        model_view(term, &model2);
        (void)report_views(who, "after the repaint: conhost vs the model",
                           &model2, &got1);
    }

    (void)local_pty_write(pty, "Q", 1);
    drain(pty, term, 300);
    local_pty_close(pty);

    if (tap) {
        if (record && flags == 0ul) {
            char fixture[128];
            (void)snprintf(fixture, sizeof(fixture),
                           "tests/fixtures/conpty_resize_%s.bin", s->name);
            int wrote = copy_tap(tap, fixture);
            RPT("  [%s] fixture %s: %s\n", who, fixture,
                wrote ? "written" : "NOT written");
            ASSERT_TRUE(wrote);
        }
        fclose(tap);
        (void)DeleteFileA(tap_path);
    }
    (void)DeleteFileA(dump_path);
    term_free(term);
    return _tf_local_fail;
}

int test_local_pty_resize_parity(void)
{
    TEST_BEGIN();
    int on_wine = running_under_wine();
    char rec[8];
    DWORD rn = GetEnvironmentVariableA("NSPROBE_RECORD", rec,
                                       (DWORD)sizeof(rec));
    int record = (rn > 0u && rn < sizeof(rec) && rec[0] == '1') ? 1 : 0;
    if (on_wine) {
        RPT("  [host] Wine: the conhost results below are Wine's, not "
            "Windows's -- not the report; no fixture is recorded\n");
        record = 0;
    }
    static const unsigned long FLAGS[2] = { 0ul, PSEUDOCONSOLE_RESIZE_QUIRK };
    for (int i = 0; i < cpx_scenario_count(); i++) {
        for (int f = 0; f < 2; f++) {
            /* One call: ASSERT_EQ evaluates its operands again to print. */
            int rc = probe_scenario(cpx_scenario_at(i), FLAGS[f], on_wine,
                                    record);
            ASSERT_EQ(rc, 0);
        }
    }
    TEST_END();
}

#endif /* _WIN32 */
```

In `tests/win_runner.c`, four edits.

Replace:
```c
int test_local_pty_console_probe_dumps(void);
```
with:
```c
int test_local_pty_console_probe_dumps(void);
int test_local_pty_resize_parity(void);
```

Replace `#define PTY_CASE_COUNT 9` with `#define PTY_CASE_COUNT 10`.

Replace:
```c
        { "test_local_pty_console_probe_dumps",
          test_local_pty_console_probe_dumps },
    };
```
with:
```c
        { "test_local_pty_console_probe_dumps",
          test_local_pty_console_probe_dumps },
        { "test_local_pty_resize_parity", test_local_pty_resize_parity },
    };
```

Replace:
```c
    DWORD wait_rc = WaitForSingleObject(pi.hProcess, 120000u);
```
with:
```c
    /* 300 s: the resize parity case alone opens eight pseudo-consoles. */
    DWORD wait_rc = WaitForSingleObject(pi.hProcess, 300000u);
```

- [ ] **Step 4: Run it under Wine**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && wineserver -k; xvfb-run -a make wintest > build/wintest.log 2>&1; grep -E "^\[FAIL\]|Tests Run|\[host\]|never showed|returned -1|before the resize" build/wintest.log`
Expected (Wine's `ResizePseudoConsole` fails, so every run stops short of a comparison and says so):
```
  [host] Wine: the conhost results below are Wine's, not Windows's -- not the report; no fixture is recorded
  [grow/flags0] before the resize: conhost vs scenario layout: AGREES (0 differences)
  [grow/flags0] local_pty_resize returned -1 (Wine's ResizePseudoConsole fails)
  [grow/flags0] conhost's dump never showed 40x20 (last read: 40x12) -- Wine does not resize its conhost; nothing to compare
```
the same three lines for `grow/quirk`, `shrink_bottom/...`, `shrink_below/...` and `width/...` (every `before the resize` line says AGREES), then `[pty child] Tests Run: 10, Failed: 0` and `Tests Run: 12, Failed: 0`. Also check nothing was recorded: `ls tests/fixtures` lists only `conpty_ls_gitbash.bin`.

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `Tests Run: 2966, Failed: 0`

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && git status --short && \
git add tests/test_local_pty.c tests/win_runner.c tests/test_term_conpty.c tests/runner.c && \
git commit -F - <<'EOF'
test(local-pty): resize parity against conhost, and the fixture recorder

For each probe scenario, with and without PSEUDOCONSOLE_RESIZE_QUIRK, the
case resizes the model and the pseudo-console, snapshots the model before
any post-resize byte reaches it (every scenario line still there exactly
once), asks the probe for conhost's buffer, and prints it against the
spec's top-anchored rule and against today's model, then prints the raw
post-resize bytes. The comparisons are the report and are not asserted.
With NSPROBE_RECORD=1 on Windows it also writes the four
tests/fixtures/conpty_resize_*.bin recordings with an in-band
ESC]9999;resize=COLSxROWS BEL marker where the resize was made; a native
test pins that the emulator ignores the marker.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 5: The conhost-behaviour report, and the Windows run

**Files:**
- Create: `docs/superpowers/specs/2026-10-19-conpty-resize-probe-report.md`
- Modify: `docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md:108` (one pointer line after section 0's second paragraph)
- Maintainer, on Windows: fills the report; creates `tests/fixtures/conpty_resize_{grow,shrink_bottom,shrink_below,width}.bin`

**Interfaces:**
- Consumes: Task 4's case output and fixture recorder.
- Produces: the report the next plan (section 1) argues from. Tasks 6-8 do not wait for the Windows run.

- [ ] **Step 1: Write the report template**

Create `docs/superpowers/specs/2026-10-19-conpty-resize-probe-report.md`; its empty table cells and `Answer:` lines are the fields the maintainer fills in Step 4, by design:

```markdown
# ConPTY resize probe: what conhost does on a resize

**Date**: 2026-10-19 (template). The Windows run that fills it: _date, Windows build from `winver`_.
**Status**: DRAFT until the maintainer has filled every section from one Windows run.
**Spec**: `2026-10-18-conpty-resize-anchor-design.md`, section 0 ("Probe first") and 1.8.
**Produced by**: `test_local_pty_resize_parity` in `tests/test_local_pty.c`, run by
`mingw32-make wintest` on Windows (MSYS2 MINGW64). The case runs under Wine on Linux as well,
but Wine's `ResizePseudoConsole` fails, so a Wine run says nothing about conhost and is not this
report.

## How to read the case's output

Each scenario runs twice, `flags0` (what `local_pty_open()` passes today) and `quirk`
(`PSEUDOCONSOLE_RESIZE_QUIRK`, 0x2). Per run the case prints:

- `before the resize: conhost vs scenario layout`: conhost's buffer once the probe child has
  written the scenario. It should say AGREES. If it does not, the scenario itself did not
  render as `tests/conpty_expect.c` assumes, and the run's later lines cannot be read against
  the rule.
- `after the resize: conhost vs spec section 1.3 (top-anchored)`: conhost's buffer and cursor
  after `ResizePseudoConsole`, against the rule computed from the scenario text. AGREES means
  section 1 describes this conhost for that scenario.
- `after the resize: conhost vs today's model before its repaint`: the same buffer against
  Nutshell's bottom-anchored model, snapshotted before any post-resize byte reached it.
- `VERDICT rule=... model=...`: the two comparisons in one line.
- `post-resize bytes`: everything ConPTY sent after the resize, with ESC, `\r` and `\n` shown.
- `after the repaint: conhost vs the model`: whether the repaint brought the model into line.

## Results

| Scenario | Flags | Rule verdict | Model verdict | Post-resize bytes | Starts with a full repaint from `ESC[H`? |
|---|---|---|---|---|---|
| grow (40x12 to 40x20) | flags0 | | | | |
| grow | quirk | | | | |
| shrink_bottom (40x12 to 40x8) | flags0 | | | | |
| shrink_bottom | quirk | | | | |
| shrink_below (40x12 to 40x8) | flags0 | | | | |
| shrink_below | quirk | | | | |
| width (40x12 to 25x12) | flags0 | | | | |
| width | quirk | | | | |

## Answers to the spec's "Open for the maintainer"

1. **conhost's resize behaviour** (grow keeps the top line; a shrink drops from the top only as
   far as the cursor needs; a width change rewraps; the buffer is the viewport). Answered by the
   rule verdicts for grow, shrink_bottom and width, and by the dump's `size=` line (a buffer
   taller than the window would mean the buffer is not the viewport). Answer:
2. **Content below the cursor on a shrink.** Answered by shrink_below. AGREES means conhost
   scrolls out from the top and section 1.3 step 5 stands. If conhost instead shows rows
   `<01>` to `<06>`, `P>` and one blank row with the cursor on row 6, it dropped from the bottom
   and section 1.8's first change applies. Answer:
3. **Soft wraps in ConPTY's repaint.** Answered by the width run's post-resize bytes: is a
   rewrapped row (for example `<W3> abcdefghijklmnopqrst` followed by `uvwxyz012`) sent as two
   rows each ending `ESC[K\r\n`, or as one run of text with no `\r\n` between? Answer:
4. **Trailing spaces.** Not answerable by this probe: `ReadConsoleOutputCharacterW` returns a
   space for a written space and for an empty cell alike. Stays open.
5. **`PSEUDOCONSOLE_RESIZE_QUIRK`.** Compare each scenario's `flags0` and `quirk` rows. The
   maintainer's rule (2026-10-19): the product adopts the flag if, with it, the post-resize
   bytes no longer start with a full repaint from `ESC[H` **and** conhost's cursor after the
   resize still equals the rule's cursor. Answer:

## Decisions

- Section 1 as written, or amended per 1.8 (say which change):
- `PSEUDOCONSOLE_RESIZE_QUIRK` adopted for every local session (yes or no, and why):

## Fixtures recorded

`NSPROBE_RECORD=1` writes these from the `flags0` runs (`tests/fixtures/*.bin` is `binary` in
`.gitattributes`, so git stores the bytes unchanged):

| File | Bytes |
|---|---|
| `tests/fixtures/conpty_resize_grow.bin` | |
| `tests/fixtures/conpty_resize_shrink_bottom.bin` | |
| `tests/fixtures/conpty_resize_shrink_below.bin` | |
| `tests/fixtures/conpty_resize_width.bin` | |

## Appendix: the case's output

Paste everything from `[RUN ] test_local_pty_resize_parity` to its `[PASS]` or `[FAIL]` line.
```

In the spec, replace

```markdown
itself, and with it section 3's two residuals.
```
with:
```markdown
itself, and with it section 3's two residuals.

The report is `2026-10-19-conpty-resize-probe-report.md`, filled from one Windows run of
`test_local_pty_resize_parity`.
```

- [ ] **Step 2: Commit the template**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && git status --short && \
git add docs/superpowers/specs/2026-10-19-conpty-resize-probe-report.md docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md && \
git commit -F - <<'EOF'
docs(conpty-resize): the probe report, to be filled from a Windows run

What each line of the parity case's output means, a results table for
the four scenarios with and without the resize quirk flag, the five open
questions the probe answers (or cannot), the adoption rule for
PSEUDOCONSOLE_RESIZE_QUIRK, and the fixtures. The spec's section 0 points
to it.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

- [ ] **Step 3 (maintainer runs on Windows): run the probe and record the fixtures**

In an MSYS2 MINGW64 shell (PATH with `C:\msys64\mingw64\bin` first), at the worktree's repo root on branch `fix/conpty-resize-anchor`, after pulling Tasks 1-5:

```bash
NSPROBE_RECORD=1 mingw32-make wintest 2>&1 | tee build/probe-run.log
ls -l tests/fixtures/conpty_resize_*.bin
```

Expected: `[PASS] test_local_pty_resize_parity`, `Tests Run: 12, Failed: 0`, no `[host] Wine` line, four `[<scenario>/flags0] fixture tests/fixtures/conpty_resize_<scenario>.bin: written` lines, and `ls` listing the four files. A `[FAIL]` here is a harness failure on Windows (a dump that never showed the new size, `local_pty_resize` failing, or a scenario line counted other than once after the model's resize); its `RPT` line names it.

- [ ] **Step 4 (maintainer): fill the report and commit it with the fixtures**

Fill every table cell and `Answer:` line of the report from `build/probe-run.log`, paste the case's output into the appendix, and change **Status** to `REVIEWED`. Then:

```bash
git status --short
git add docs/superpowers/specs/2026-10-19-conpty-resize-probe-report.md \
        tests/fixtures/conpty_resize_grow.bin tests/fixtures/conpty_resize_shrink_bottom.bin \
        tests/fixtures/conpty_resize_shrink_below.bin tests/fixtures/conpty_resize_width.bin
git commit -m "docs(conpty-resize): fill the probe report from a Windows run"
```

If a Claude session makes this commit, it carries the `Models:` line and the two trailers from Global Constraints. `build/probe-run.log` is not committed (`*.log` is ignored). If the report's decision amends section 1 (spec 1.8) or adopts the quirk flag, that change is the next plan's, not this one's.

---

### Task 6: Reflow fix 1, exact width

**Files:**
- Modify: `src/term/buffer.c:146-163` (CRLF; the inner loop of `term_reflow_buffer()`)
- Create: `tests/test_reflow_fixes.c`
- Modify: `tests/test_term.c:274-277` (`test_term_resize_cursor_edge`, its third step)
- Modify: `tests/runner.c` (CRLF; after `test_term_resize_degenerate_round_trip_keeps_scroll_anchor`, lines 1121 and 4305)

**Interfaces:**
- Consumes: `term_init()`, `term_process()`, `term_resize()`, `term_free()`, `term_screen_to_phys()` (`src/term/term.h:206`).
- Produces (used by Task 7): `tests/test_reflow_fixes.c` with the static helpers `screen_row(const Terminal *t, int r)`, `row_text(const TermRow *row, int cols, char *out, size_t out_size)`, `screen_text_at(const Terminal *t, int r, char *out, size_t out_size)`, `feed(Terminal *t, const char *s)`; the registered test `test_reflow_deferred_wrap_cursor_is_clamped` that Task 7's registration anchors on. Behaviour: a row that ends exactly at `new_cols` opens no blank `wrapped` row; the wrap happens when the next cell of the same logical line is copied, or when the cursor sits on an empty continuation row.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_reflow_fixes.c`:

```c
/* tests/test_reflow_fixes.c -- the two reflow fixes every session gets
 * (docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md
 * section 2), and the identity property they buy: a resize sequence that
 * ends at its starting size, with no overflow in between, leaves the model
 * unchanged.
 *
 * The spec states the identity for top mode; top mode is not built yet
 * (it is the next plan), so these cases use states bottom mode can also
 * reverse: no top padding (no grow while the screen has blank rows below
 * the cursor) and nothing pushed past the scrollback capacity. The top-mode
 * versions join this file with section 1.
 *
 * Native, `make test`; term_resize() is called directly. */

#include "test_framework.h"
#include "term.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ---- helpers ------------------------------------------------------------ */

static const TermRow *screen_row(const Terminal *t, int r)
{
    return t->lines[term_screen_to_phys(t, r)];
}

/* A row's written cells as text (NUL cells as spaces), up to row->len. */
static void row_text(const TermRow *row, int cols, char *out, size_t out_size)
{
    size_t n = 0;
    int len = row->len < cols ? row->len : cols;
    for (int c = 0; c < len && n + 1 < out_size; c++) {
        uint32_t cp = row->cells[c].codepoint;
        out[n++] = (cp >= 0x20u && cp < 0x7Fu) ? (char)cp : ' ';
    }
    out[n] = '\0';
}

static void screen_text_at(const Terminal *t, int r, char *out, size_t out_size)
{
    row_text(screen_row(t, r), t->cols, out, out_size);
}

static void feed(Terminal *t, const char *s)
{
    term_process(t, s, strlen(s));
}

/* ---- exact width ---------------------------------------------------------- */

/* A 20-character line on a 20-column terminal, made 10 rows taller: the
 * line keeps one row and no row gains a wrapped flag. */
int test_reflow_exact_width_height_grow_adds_no_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "ABCDEFGHIJKLMNOPQRST\r\n$ ");
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 2);

    term_resize(t, 20, 20);

    char text[64];
    ASSERT_EQ(t->cursor.row, 19);
    ASSERT_EQ(t->cursor.col, 2);
    screen_text_at(t, 18, text, sizeof(text));
    ASSERT_STR_EQ(text, "ABCDEFGHIJKLMNOPQRST");
    screen_text_at(t, 19, text, sizeof(text));
    ASSERT_STR_EQ(text, "$ ");
    int wrapped_rows = 0;
    for (int r = 0; r < t->rows; r++)
        if (screen_row(t, r)->wrapped) wrapped_rows++;
    ASSERT_EQ(wrapped_rows, 0);

    term_free(t);
    TEST_END();
}

/* The same line narrowed to 10 columns is two rows, and widened back to 20
 * it is one row again, with the prompt directly below it. */
int test_reflow_exact_width_narrow_and_back_is_one_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "ABCDEFGHIJKLMNOPQRST\r\n$ ");

    char text[64];
    term_resize(t, 10, 10);
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "ABCDEFGHIJ");
    screen_text_at(t, 1, text, sizeof(text));
    ASSERT_STR_EQ(text, "KLMNOPQRST");
    ASSERT_TRUE(screen_row(t, 1)->wrapped);
    screen_text_at(t, 2, text, sizeof(text));
    ASSERT_STR_EQ(text, "$ ");
    ASSERT_FALSE(screen_row(t, 2)->wrapped);
    ASSERT_EQ(t->cursor.row, 2);
    ASSERT_EQ(t->cursor.col, 2);

    term_resize(t, 10, 20);
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "ABCDEFGHIJKLMNOPQRST");
    ASSERT_FALSE(screen_row(t, 0)->wrapped);
    screen_text_at(t, 1, text, sizeof(text));
    ASSERT_STR_EQ(text, "$ ");
    ASSERT_FALSE(screen_row(t, 1)->wrapped);
    ASSERT_EQ(screen_row(t, 2)->len, 0);
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 2);

    term_free(t);
    TEST_END();
}

/* Negative: one character more than the width still wraps to two rows. */
int test_reflow_line_one_past_width_still_wraps(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "ABCDEFGHIJKLMNOPQRSTU\r\n$ ");

    term_resize(t, 12, 20);

    char text[64];
    screen_text_at(t, 9, text, sizeof(text));
    ASSERT_STR_EQ(text, "ABCDEFGHIJKLMNOPQRST");
    ASSERT_FALSE(screen_row(t, 9)->wrapped);
    screen_text_at(t, 10, text, sizeof(text));
    ASSERT_STR_EQ(text, "U");
    ASSERT_TRUE(screen_row(t, 10)->wrapped);
    screen_text_at(t, 11, text, sizeof(text));
    ASSERT_STR_EQ(text, "$ ");
    ASSERT_EQ(t->cursor.row, 11);
    ASSERT_EQ(t->cursor.col, 2);

    term_free(t);
    TEST_END();
}

/* A cursor on a continuation row that has no cells of its own (a stale
 * wrapped flag left by ED 0), after a row that ends exactly at the width,
 * keeps its row: the exact-width fix must not pull it up onto the end of
 * the row above. */
int test_reflow_cursor_on_empty_continuation_keeps_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "AAAAAAAAAAAAAAAAAAAAB");     /* 20 A, then B auto-wraps   */
    ASSERT_TRUE(screen_row(t, 1)->wrapped);
    feed(t, "\x1b[1;20H\x1b[J");          /* ED 0: row 1 keeps wrapped */
    feed(t, "A");                          /* row 0 full again          */
    feed(t, "\x1b[2;1H");                  /* cursor to row 1, col 0    */
    ASSERT_TRUE(screen_row(t, 1)->wrapped);
    ASSERT_EQ(screen_row(t, 1)->len, 0);

    term_resize(t, 8, 20);

    char text[64];
    screen_text_at(t, 0, text, sizeof(text));
    ASSERT_STR_EQ(text, "AAAAAAAAAAAAAAAAAAAA");
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 0);

    term_free(t);
    TEST_END();
}

/* ---- the deferred-wrap column --------------------------------------------- */

/* The deferred-wrap column (cursor.col == cols after a full row) is past
 * the content by 0: the spec clamps it to new_cols - 1 on the same row
 * instead of opening a blank wrapped row for it. */
int test_reflow_deferred_wrap_cursor_is_clamped(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 10, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "0123456789");
    ASSERT_EQ(t->cursor.col, 10);

    term_resize(t, 8, 10);

    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 9);
    ASSERT_EQ(screen_row(t, 1)->len, 0);
    ASSERT_FALSE(screen_row(t, 1)->wrapped);

    term_free(t);
    TEST_END();
}

```

In `tests/test_term.c`, replace

```c
    // Resize to 5 cols. Should wrap to 3 lines. Cursor at row 2, col 0.
    term_resize(t, 10, 5);
    ASSERT_EQ(t->cursor.row, 2);
    ASSERT_EQ(t->cursor.col, 0);
```
with:
```c
    // Resize to 5 cols: "01234" / "56789", and no blank third row. The
    // cursor, one past the content, keeps its distance from the content's
    // end and is clamped to the last column (resize spec section 2).
    term_resize(t, 10, 5);
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 4);
```

Register the five new tests (CRLF, by bytes):

```bash
cd /home/thomas/nutshell-wt/conpty-resize && python3 - test_term_resize_degenerate_round_trip_keeps_scroll_anchor test_reflow_fixes.c \
  test_reflow_exact_width_height_grow_adds_no_row test_reflow_exact_width_narrow_and_back_is_one_row \
  test_reflow_line_one_past_width_still_wraps test_reflow_cursor_on_empty_continuation_keeps_row \
  test_reflow_deferred_wrap_cursor_is_clamped <<'EOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'EOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
EOF
```

Expected: `runner.c: registered 5 tests after test_term_resize_degenerate_round_trip_keeps_scroll_anchor`

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected:
```
[FAIL] test_term_resize_cursor_edge
[FAIL] test_reflow_exact_width_height_grow_adds_no_row
[FAIL] test_reflow_exact_width_narrow_and_back_is_one_row
[FAIL] test_reflow_deferred_wrap_cursor_is_clamped
Tests Run: 2971, Failed: 4
```
(`test_reflow_line_one_past_width_still_wraps` and `test_reflow_cursor_on_empty_continuation_keeps_row` pass already: they guard what the fix must not break.)

- [ ] **Step 3: Write the fix**

`src/term/buffer.c` is CRLF; apply the change by bytes:

```bash
cd /home/thomas/nutshell-wt/conpty-resize && python3 - <<'EOF'
# Task 6: exact width (spec section 2). src/term/buffer.c is CRLF: edit bytes.
p = 'src/term/buffer.c'
b = open(p, 'rb').read()
old = (b"        /* Only reflow up to the written content width */\r\n"
       b"        int cell_limit = old_row->len < old_cols ? old_row->len : old_cols;\r\n"
       b"\r\n"
       b"        for (int c = 0; c < cell_limit; c++) {\r\n"
       b"            if (i == cursor_logical && c == cur_col) {\r\n"
       b"                new_cursor_row = cur_new_row;\r\n"
       b"                new_cursor_col = cur_new_col;\r\n"
       b"            }\r\n"
       b"            new_lines[cur_new_row]->cells[cur_new_col] = old_row->cells[c];\r\n"
       b"            new_lines[cur_new_row]->len = cur_new_col + 1;\r\n"
       b"            cur_new_col++;\r\n"
       b"            if (cur_new_col >= new_cols) {\r\n"
       b"                cur_new_row++;\r\n"
       b"                cur_new_col = 0;\r\n"
       b"                if (cur_new_row >= new_capacity) break;\r\n"
       b"                new_lines[cur_new_row]->wrapped = true;\r\n"
       b"            }\r\n"
       b"        }\r\n")
new = (b"        /* Only reflow up to the written content width */\r\n"
       b"        int cell_limit = old_row->len < old_cols ? old_row->len : old_cols;\r\n"
       b"\r\n"
       b"        /* A continuation row that holds the cursor but no cells still gets\r\n"
       b"         * its own output row when the row before it ended exactly at\r\n"
       b"         * new_cols, so the cursor keeps its row (spec section 2). */\r\n"
       b"        if (old_row->wrapped && i > 0 && i == cursor_logical &&\r\n"
       b"            cell_limit == 0 && cur_new_col >= new_cols) {\r\n"
       b"            cur_new_row++;\r\n"
       b"            cur_new_col = 0;\r\n"
       b"            if (cur_new_row >= new_capacity) break;\r\n"
       b"            new_lines[cur_new_row]->wrapped = true;\r\n"
       b"        }\r\n"
       b"\r\n"
       b"        for (int c = 0; c < cell_limit; c++) {\r\n"
       b"            /* Exact width (spec section 2): wrap when the next cell of the\r\n"
       b"             * same logical line arrives, never after the last one, so a row\r\n"
       b"             * that ends exactly at new_cols gains no blank wrapped row. */\r\n"
       b"            if (cur_new_col >= new_cols) {\r\n"
       b"                cur_new_row++;\r\n"
       b"                cur_new_col = 0;\r\n"
       b"                if (cur_new_row >= new_capacity) break;\r\n"
       b"                new_lines[cur_new_row]->wrapped = true;\r\n"
       b"            }\r\n"
       b"            if (i == cursor_logical && c == cur_col) {\r\n"
       b"                new_cursor_row = cur_new_row;\r\n"
       b"                new_cursor_col = cur_new_col;\r\n"
       b"            }\r\n"
       b"            new_lines[cur_new_row]->cells[cur_new_col] = old_row->cells[c];\r\n"
       b"            new_lines[cur_new_row]->len = cur_new_col + 1;\r\n"
       b"            cur_new_col++;\r\n"
       b"        }\r\n")
assert b.count(old) == 1, 'anchor not found exactly once'
open(p, 'wb').write(b.replace(old, new))
print('buffer.c: exact-width fix applied')
EOF
echo "CR lines: $(grep -c $'\r$' src/term/buffer.c) of $(wc -l < src/term/buffer.c)"
```

Expected: `buffer.c: exact-width fix applied` and the two counts equal.

The loop now reads (for review; the script wrote exactly this):

```c
        /* A continuation row that holds the cursor but no cells still gets
         * its own output row when the row before it ended exactly at
         * new_cols, so the cursor keeps its row (spec section 2). */
        if (old_row->wrapped && i > 0 && i == cursor_logical &&
            cell_limit == 0 && cur_new_col >= new_cols) {
            cur_new_row++;
            cur_new_col = 0;
            if (cur_new_row >= new_capacity) break;
            new_lines[cur_new_row]->wrapped = true;
        }

        for (int c = 0; c < cell_limit; c++) {
            /* Exact width (spec section 2): wrap when the next cell of the
             * same logical line arrives, never after the last one, so a row
             * that ends exactly at new_cols gains no blank wrapped row. */
            if (cur_new_col >= new_cols) {
                cur_new_row++;
                cur_new_col = 0;
                if (cur_new_row >= new_capacity) break;
                new_lines[cur_new_row]->wrapped = true;
            }
            if (i == cursor_logical && c == cur_col) {
                new_cursor_row = cur_new_row;
                new_cursor_col = cur_new_col;
            }
            new_lines[cur_new_row]->cells[cur_new_col] = old_row->cells[c];
            new_lines[cur_new_row]->len = cur_new_col + 1;
            cur_new_col++;
        }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `Tests Run: 2971, Failed: 0`

Strict compile of the new test file, and the wintest build (it compiles `buffer.c` with `-Werror -Wconversion` under MinGW):

```bash
cd /home/thomas/nutshell-wt/conpty-resize && \
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -D_POSIX_C_SOURCE=200809L -Isrc -Isrc/term -Isrc/core -c tests/test_reflow_fixes.c -o /dev/null && \
wineserver -k; xvfb-run -a make wintest > build/wintest.log 2>&1; grep -E "^\[FAIL\]|Tests Run" build/wintest.log
```
Expected: no compiler output, `[pty child] Tests Run: 10, Failed: 0`, `Tests Run: 12, Failed: 0`.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && git status --short && \
git add src/term/buffer.c tests/test_reflow_fixes.c tests/test_term.c tests/runner.c && \
git commit -F - <<'EOF'
fix(term): a row that fills the width exactly no longer gains a blank row on resize

The reflow started a new wrapped row as soon as it copied a row's last
cell, so every row ending exactly at the new width left a blank wrapped
row behind, and a plain height change grew the buffer. The wrap now
happens when the next cell of the same logical line arrives (or when the
cursor sits on an empty continuation row, which keeps its row). This
reaches SSH sessions too. A cursor in the deferred-wrap column now stays
on its row at the last column instead of moving to a new blank row;
test_term_resize_cursor_edge's narrowing step says so.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 7: Reflow fix 2, the cursor past the content, and the identity property

**Files:**
- Modify: `src/term/buffer.c` (CRLF; the `cur_col >= cell_limit` block right after Task 6's inner loop, originally lines 164-167)
- Modify: `tests/test_reflow_fixes.c` (append)
- Modify: `tests/runner.c` (CRLF; after `test_reflow_deferred_wrap_cursor_is_clamped`)

**Interfaces:**
- Consumes: Task 6's helpers in `tests/test_reflow_fixes.c` (`screen_row`, `row_text`, `screen_text_at`, `feed`) and its registration of `test_reflow_deferred_wrap_cursor_is_clamped`.
- Produces: `new_cursor_col = cur_new_col + (cur_col - cell_limit)` for a cursor at or past its row's content, clamped to `new_cols - 1` by the existing clamp; with Task 6, a resize sequence that ends at its starting size, with no top padding and nothing past the scrollback capacity in between, leaves the model unchanged.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_reflow_fixes.c`:

```c
/* Every logical row the model holds -- scrollback and screen -- with its
 * text, len and wrapped flag, plus the geometry and the cursor. */
#define SNAP_MAX_ROWS 256
#define SNAP_MAX_COLS 64

typedef struct {
    int rows, cols, lines_count;
    int cursor_row, cursor_col, scrollback_offset;
    int len[SNAP_MAX_ROWS];
    bool wrapped[SNAP_MAX_ROWS];
    char text[SNAP_MAX_ROWS][SNAP_MAX_COLS + 1];
} ModelSnap;

static void snap(const Terminal *t, ModelSnap *s)
{
    memset(s, 0, sizeof(*s));
    s->rows = t->rows;
    s->cols = t->cols;
    s->lines_count = t->lines_count;
    s->cursor_row = t->cursor.row;
    s->cursor_col = t->cursor.col;
    s->scrollback_offset = t->scrollback_offset;
    for (int i = 0; i < t->lines_count && i < SNAP_MAX_ROWS; i++) {
        const TermRow *row = t->lines[(t->lines_start + i) % t->lines_capacity];
        s->len[i] = row->len;
        s->wrapped[i] = row->wrapped;
        row_text(row, t->cols < SNAP_MAX_COLS ? t->cols : SNAP_MAX_COLS,
                 s->text[i], sizeof(s->text[i]));
    }
}

/* 1 when equal; otherwise prints the first difference and returns 0. */
static int snap_equal(const ModelSnap *a, const ModelSnap *b)
{
    if (a->rows != b->rows || a->cols != b->cols ||
        a->lines_count != b->lines_count) {
        printf("  geometry %dx%d lines=%d vs %dx%d lines=%d\n",
               a->rows, a->cols, a->lines_count,
               b->rows, b->cols, b->lines_count);
        return 0;
    }
    if (a->cursor_row != b->cursor_row || a->cursor_col != b->cursor_col) {
        printf("  cursor %d,%d vs %d,%d\n", a->cursor_row, a->cursor_col,
               b->cursor_row, b->cursor_col);
        return 0;
    }
    if (a->scrollback_offset != b->scrollback_offset) {
        printf("  scrollback_offset %d vs %d\n", a->scrollback_offset,
               b->scrollback_offset);
        return 0;
    }
    for (int i = 0; i < a->lines_count && i < SNAP_MAX_ROWS; i++) {
        if (a->len[i] != b->len[i] || a->wrapped[i] != b->wrapped[i] ||
            strcmp(a->text[i], b->text[i]) != 0) {
            printf("  line %d: len=%d wrapped=%d \"%s\" vs len=%d wrapped=%d \"%s\"\n",
                   i, a->len[i], (int)a->wrapped[i], a->text[i],
                   b->len[i], (int)b->wrapped[i], b->text[i]);
            return 0;
        }
    }
    return 1;
}

/* 29 numbered lines, every third exactly 20 wide, then a PowerShell-style
 * prompt with the cursor one cell past it on the last row of a 10x20
 * screen: 20 rows of scrollback above. */
static void fill_history_with_prompt(Terminal *t)
{
    char line[32];
    for (int i = 1; i <= 29; i++) {
        if (i % 3 == 0) {
            (void)snprintf(line, sizeof(line), "L%02d=================\r\n", i);
        } else {
            (void)snprintf(line, sizeof(line), "L%02d\r\n", i);
        }
        feed(t, line);
    }
    feed(t, "PS C:\\>");
    feed(t, "\x1b[10;9H");
}

/* ---- cursor past the content ---------------------------------------------- */

/* PS C:\> is 7 cells; the cursor one cell further, at column 8, keeps
 * column 8 across height changes either way. */
int test_reflow_cursor_past_content_keeps_column(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "PS C:\\>\x1b[1;9H");
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 8);

    term_resize(t, 8, 20);
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 8);

    term_resize(t, 12, 20);
    ASSERT_EQ(t->cursor.col, 8);
    char text[64];
    screen_text_at(t, t->cursor.row, text, sizeof(text));
    ASSERT_STR_EQ(text, "PS C:\\>");

    term_free(t);
    TEST_END();
}

/* Narrowed so the prompt wraps, the cursor keeps its distance on the
 * continuation row; narrowed so it no longer fits, it is clamped to the
 * last column. */
int test_reflow_cursor_past_content_rewraps_and_clamps(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "PS C:\\>\x1b[1;9H");

    term_resize(t, 10, 6);               /* "PS C:\" + ">" wrapped     */
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(t->cursor.col, 2);

    term_free(t);

    t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "PS C:\\>\x1b[1;9H");
    term_resize(t, 10, 8);               /* column 8 does not exist    */
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 7);

    term_free(t);
    TEST_END();
}

/* ---- identity --------------------------------------------------------------- */

/* Narrow and widen back: exact-width, wrapping and short lines and a
 * prompt with the cursor past it come back exactly as they were. */
int test_reflow_width_round_trip_is_identity(void)
{
    TEST_BEGIN();
    static ModelSnap before, after;
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    feed(t, "ABCDEFGHIJKLMNOPQRST\r\nhello\r\n"
            "123456789012345678901234567\r\nPS C:\\>\x1b[5;9H");
    ASSERT_EQ(t->cursor.row, 4);
    ASSERT_EQ(t->cursor.col, 8);
    snap(t, &before);

    term_resize(t, 10, 12);
    term_resize(t, 10, 20);

    snap(t, &after);
    ASSERT_TRUE(snap_equal(&before, &after));

    term_free(t);
    TEST_END();
}

/* Taller and back with history above and the cursor on the last row. */
int test_reflow_height_round_trip_with_history_is_identity(void)
{
    TEST_BEGIN();
    static ModelSnap before, after;
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    fill_history_with_prompt(t);
    ASSERT_EQ(t->lines_count, 30);
    ASSERT_EQ(t->cursor.row, 9);
    ASSERT_EQ(t->cursor.col, 8);
    snap(t, &before);

    term_resize(t, 20, 20);
    ASSERT_EQ(t->cursor.row, 19);
    ASSERT_EQ(t->cursor.col, 8);
    term_resize(t, 10, 20);

    snap(t, &after);
    ASSERT_TRUE(snap_equal(&before, &after));

    term_free(t);
    TEST_END();
}

/* A drag's worth of sizes, height and width both, ending where it began. */
int test_reflow_mixed_sequence_round_trip_is_identity(void)
{
    TEST_BEGIN();
    static ModelSnap before, after;
    Terminal *t = term_init(10, 20, 100);
    ASSERT_NOT_NULL(t);
    fill_history_with_prompt(t);
    snap(t, &before);

    term_resize(t, 15, 20);
    term_resize(t, 15, 12);
    term_resize(t, 10, 12);
    term_resize(t, 10, 20);

    snap(t, &after);
    ASSERT_TRUE(snap_equal(&before, &after));

    term_free(t);
    TEST_END();
}
```

Register them (CRLF, by bytes):

```bash
cd /home/thomas/nutshell-wt/conpty-resize && python3 - test_reflow_deferred_wrap_cursor_is_clamped "" \
  test_reflow_cursor_past_content_keeps_column test_reflow_cursor_past_content_rewraps_and_clamps \
  test_reflow_width_round_trip_is_identity test_reflow_height_round_trip_with_history_is_identity \
  test_reflow_mixed_sequence_round_trip_is_identity <<'EOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'EOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
EOF
```

Expected: `runner.c: registered 5 tests after test_reflow_deferred_wrap_cursor_is_clamped`

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run|  cursor "`
Expected:
```
[FAIL] test_reflow_cursor_past_content_keeps_column
[FAIL] test_reflow_cursor_past_content_rewraps_and_clamps
  cursor 4,8 vs 4,7
[FAIL] test_reflow_width_round_trip_is_identity
  cursor 9,8 vs 9,7
[FAIL] test_reflow_height_round_trip_with_history_is_identity
  cursor 9,8 vs 9,7
[FAIL] test_reflow_mixed_sequence_round_trip_is_identity
Tests Run: 2976, Failed: 5
```

- [ ] **Step 3: Write the fix**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && python3 - <<'EOF'
# Task 7: cursor past the content (spec section 2). CRLF file: edit bytes.
p = 'src/term/buffer.c'
b = open(p, 'rb').read()
old = (b"        if (i == cursor_logical && cur_col >= cell_limit) {\r\n"
       b"            new_cursor_row = cur_new_row;\r\n"
       b"            new_cursor_col = cur_new_col;\r\n"
       b"        }\r\n")
new = (b"        /* A cursor past the content keeps its distance from it (spec\r\n"
       b"         * section 2); the clamp to new_cols - 1 below bounds it. */\r\n"
       b"        if (i == cursor_logical && cur_col >= cell_limit) {\r\n"
       b"            new_cursor_row = cur_new_row;\r\n"
       b"            new_cursor_col = cur_new_col + (cur_col - cell_limit);\r\n"
       b"        }\r\n")
assert b.count(old) == 1, 'anchor not found exactly once'
open(p, 'wb').write(b.replace(old, new))
print('buffer.c: cursor-past-content fix applied')
EOF
echo "CR lines: $(grep -c $'\r$' src/term/buffer.c) of $(wc -l < src/term/buffer.c)"
```

Expected: `buffer.c: cursor-past-content fix applied` and the two counts equal. The block now reads:

```c
        /* A cursor past the content keeps its distance from it (spec
         * section 2); the clamp to new_cols - 1 below bounds it. */
        if (i == cursor_logical && cur_col >= cell_limit) {
            new_cursor_row = cur_new_row;
            new_cursor_col = cur_new_col + (cur_col - cell_limit);
        }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `Tests Run: 2976, Failed: 0`

```bash
cd /home/thomas/nutshell-wt/conpty-resize && \
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -D_POSIX_C_SOURCE=200809L -Isrc -Isrc/term -Isrc/core -c tests/test_reflow_fixes.c -o /dev/null && \
wineserver -k; xvfb-run -a make wintest > build/wintest.log 2>&1; grep -E "^\[FAIL\]|Tests Run" build/wintest.log
```
Expected: no compiler output, `[pty child] Tests Run: 10, Failed: 0`, `Tests Run: 12, Failed: 0`.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && git status --short && \
git add src/term/buffer.c tests/test_reflow_fixes.c tests/runner.c && \
git commit -F - <<'EOF'
fix(term): a cursor past the text keeps its column across a resize

The reflow pulled a cursor that sat beyond its row's text back to the
text's end, so every PowerShell prompt (cursor one past "PS C:\>") moved
one cell left on any resize. It now keeps its distance from the text,
clamped to the last column. With the exact-width fix, a resize sequence
that ends where it began leaves the model unchanged; three round-trip
tests pin that for width, height with history, and a mixed drag. This
reaches SSH sessions too.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 8: Merge main, version bump, release build, exe

**Files:**
- Modify: `src/ui/resource.h:4-5`
- Modify: `README.md:5`
- Modify: `build/win/nutshell.exe` (rebuilt)

**Interfaces:**
- Consumes: Tasks 1-7 committed.
- Produces: one version everywhere the `Version bump` check reads it (`APP_VERSION`, `APP_VERSION_BINARY`, README's `**Version**:` line, the exe's FileVersion), one patch above `origin/main`'s at the time of this task. The version is **not** fixed in this plan: this branch was cut from `c04945d` (v1.2.36) and `main` has moved since (v1.2.37 on 2026-10-19, with PR #71 to follow it); the bump is computed from `main` in Step 1.

- [ ] **Step 1: Merge `main` and read its version**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && git fetch origin main && git merge origin/main
```

Resolve any conflict by keeping both sides' intent (the files this plan touches are `src/term/buffer.c`, `src/ui/local_pty.{c,h}`, `src/ui/window.c`, `tests/runner.c`, `tests/win_runner.c`, `tests/test_local_pty.c`, `tests/test_term.c`, `tests/test_term_conpty.c` and the Makefile; a conflict in `tests/runner.c` or the Makefile is the union of both sides' registrations, CRLF preserved). After the merge, `make test` must pass before going on.

Run: `git show origin/main:src/ui/resource.h | grep -E 'APP_VERSION(_BINARY)? '`
Expected: two lines, e.g. `#define APP_VERSION        "1.2.41"` and `#define APP_VERSION_BINARY  1,2,41,0`. Call that version M. The new version N is M with the patch raised by one (after 1.2.99 comes 1.3.0, `1,3,0,0`).

- [ ] **Step 2: Bump the version in both files to N**

`src/ui/resource.h`: `APP_VERSION` becomes `"N"` and `APP_VERSION_BINARY` the comma form of N (e.g. `"1.2.42"` and `1,2,42,0`). `README.md` line 5: `**Version**: vN \`. No other commit on this branch bumps the version; if one has, raise from that value instead.

- [ ] **Step 3: Build the release**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make clean && make release 2>&1 | tail -5`
Expected: the link of `build/win/nutshell.exe` with no warning or error, then UPX's table ending in a line for `nutshell.exe` and `Packed 1 file.`

Run: `strings -el build/win/nutshell.exe | grep -m1 -F "N"` (with N spelled out)
Expected: the version string N.

- [ ] **Step 4: Run every test**

Run: `cd /home/thomas/nutshell-wt/conpty-resize && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `Failed: 0`; the count is the merged tree's baseline plus this plan's 19 new tests.

Run: `cd /home/thomas/nutshell-wt/conpty-resize && wineserver -k; xvfb-run -a make wintest > build/wintest.log 2>&1; grep -E "^\[FAIL\]|Tests Run" build/wintest.log`
Expected: `Failed: 0` in both the pty child and the main run (rerun once if the pty section prints nothing: Wine flakes).

- [ ] **Step 5: Commit and check the gate locally**

```bash
cd /home/thomas/nutshell-wt/conpty-resize && git status --short && \
git add src/ui/resource.h README.md build/win/nutshell.exe && \
git commit -F - <<'EOF'
build: vN for the console probe and the two reflow fixes

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
bash .github/scripts/check-version.sh origin/main
```

(with N spelled out in the subject.) Expected: the four `check-version: ... = N` lines, the list of changed build inputs, `check-version: base (...) APP_VERSION = M`, and `check-version: PASS (M -> N)`.

`git status --short` must show nothing left over (`build/wintest.log` is ignored as `*.log`; the objects `make release` leaves are ignored as `*.o`).
