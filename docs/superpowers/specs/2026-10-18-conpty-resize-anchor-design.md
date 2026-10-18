# Local shell: top-anchored reflow for ConPTY sessions, and one resize per DPI change

**Date**: 2026-10-18
**Status**: DRAFT, not yet critiqued.
**Branch**: `fix/conpty-resize-anchor` (cut from `main` at `c04945d`, v1.2.36)
**Prompted by**: issue mark 3 of `nutshell-20261018-130024-p15116` (the maintainer's
`issue3.log`, 12,868 lines). In a local PowerShell tab, after the window moved to a 96 DPI
monitor and the grid grew from 48 to 87 rows, the prompt sat on row 35 of 87 and the history
Nutshell had pulled down onto the screen was erased.
**Builds on**: `2026-09-22-local-shell-design.md` (ConPTY transport, section 3 "Resize"),
`2026-10-17-debug-logging-design.md` (event conventions, `term` category, issue snapshot).

---

## Problem

Two defects meet in one move between monitors.

**The model is bottom-anchored; ConPTY is top-anchored.** `term_reflow_buffer()`
(`src/term/buffer.c:110-223`) lays the whole ring (scrollback and screen) out linearly at the
new width, strips blank rows below the cursor (`:173-179`), and takes the screen as the last
`new_rows` rows (`:209-211`). On a grow with history above, that pulls history down so the
cursor stays on the last row; on a grow without enough history it pads the top with blanks
(`:186-198`); on a shrink it pushes rows into scrollback until the cursor is on the last row,
even when the rows below the cursor were blank. ConPTY's buffer is exactly its viewport: on a
resize it keeps its top line fixed, has nothing to pull down, and after the resize it repaints
its whole viewport. Every row Nutshell pulled out of its scrollback onto the screen is then
overwritten by that repaint and is gone from both.

The log (line numbers in `issue3.log`):

| Line | Time | Event |
|---|---|---|
| 11614-15 | 17:48:02.695 | `term.resize rows_before=48 rows=38 cols_before=124 cols=103 lines_before=1042 lines=1536`, matching `shell.resize` |
| 11617-18 | 17:48:02.830 | `term.resize rows_before=38 rows=85 cols_before=103 cols=292 lines_before=1536 lines=1002`, matching `shell.resize` |
| 11620-21 | 17:48:03.059 | `term.resize rows_before=85 rows=87`, matching `shell.resize`; `render.draw char_h=15` follows |
| 11626-28 | 17:48:03.258 | ConPTY's repaint in three reads (4096, 4096, 1098 bytes): `\e[H`, 35 content lines each ending `\e[K\r\n`, ~52 bare `\e[K` rows, `\e[35;31H\e[?25h` |

Both earlier resizes also changed the width, so the width reflow is part of the bug, not a side
case. Row counts never disagreed between `term.resize` and `shell.resize`; only the vertical
anchoring did. The log has no cursor field (section 3 adds one), but by the code above the
cursor was on row 86 before the repaint, with every row above it filled from scrollback;
ConPTY's repaint put 35 rows of its own content on rows 0-34, blanked rows 35-86 (the pulled
history), and moved the cursor to row 34. Every later ConPTY repaint (AI panel drag, zoom)
re-asserts `\e[35;31H`.

**The DPI handler resizes twice.** `WM_DPICHANGED` (`src/ui/window.c:4798-4840`) calls
`SetWindowPos` with the suggested rectangle (`:4812-4816`) before it rebuilds the renderer's
fonts (`:4819-4821`). `SetWindowPos` sends `WM_SIZE` synchronously, and `WM_SIZE` fits the grid
with `g_renderer.charWidth`/`charHeight` (`:4777-4781`), still the 192 DPI font's 32 px, to the
new, smaller client area: the 48→38 shrink at 02.695. The explicit `WM_SIZE` after the font
rebuild (`:4834-4838`) then grows to 85 rows. ConPTY has no scrollback, so the transient shrink
discarded the rows that fell off its top for good. `apply_zoom()` already does it in the right
order: renderer first, `WM_SIZE` second (`:2516-2541`).

Ruled out by the log: the scroll region (reset on every resize, `buffer.c:262-263`); the
renderer skipping rows (every repaint drew every row, `rows_left_dirty=0`,
`render.stuck_dirty` never fired); scrollback offset (0 at onset); the AI dock and zoom (after
onset). PR #66 (v1.2.34) fixed stale rows on screen over a correct model; here the model is
wrong.

---

## Design

### 1. Top-anchored reflow for ConPTY sessions

**1.1 The mode.** `term.h` gains

```c
typedef enum { TERM_ANCHOR_BOTTOM = 0, TERM_ANCHOR_TOP = 1 } TermAnchor;
```

and `Terminal` (`src/term/term.h:60-129`) gains `TermAnchor anchor;`, set to
`TERM_ANCHOR_BOTTOM` in `term_init()` (`buffer.c:34-87`; `term_init` uses `xmalloc`, so the
field is set explicitly next to `log_id` at `:76`). `term_reflow_buffer()` takes it as a new
parameter and `term_resize()` passes `term->anchor` for both the active buffer (`:235-243`)
and the saved primary buffer (`:248-257`). The mode is read at each resize; changing it never
moves content by itself.

**1.2 `TERM_ANCHOR_BOTTOM` is today's code path, unchanged.** The new parameter selects a
separate branch; the bottom branch is the current body of `term_reflow_buffer()` line for line.
SSH sessions never set the mode, so their behaviour, and every existing resize test
(`tests/test_term.c:215-345`, `:943`, `:977`, `:1165-1192`; `tests/test_term_fuzz.c:414-460`;
`test_dirty.c`, `test_scroll_region.c`, `test_selection.c`, `test_color_consistency.c`,
`test_term_extract.c`), stays exactly as it is.

**1.3 `TERM_ANCHOR_TOP` semantics** (what conhost does with a buffer that is its viewport).
With `T = old_count - old_rows` the logical index of the old screen's top row
(`buffer.c:132`), `C` the cursor's logical row (`:133`), and `E = max(C, last row in [T,
old_count) with len > 0)`:

1. **Scrollback** `[0, T)` is reflowed at the new width exactly as today (`:135-168`), giving
   `S` rows.
2. **Hard break at the screen top.** Row `T` always starts a new output row with
   `wrapped = false`, even when it continues a line in scrollback. ConPTY has no row above its
   top line, so a line that straddles the boundary is two lines to it; joining them in
   Nutshell would rewrap a different fragment onto the screen than ConPTY repaints.
3. **Screen segment** `[T, E]` is reflowed at the new width into `L` rows starting at `S`.
   Rows after `E` are blank and are not carried (no stripping is needed, and none is done).
4. **Grow or fit** (`L <= new_rows`): the screen top is row `S`. No top padding, no history
   pulled down. Rows `[S+L, S+new_rows)` are fresh blanks; `new_count = S + new_rows`.
5. **Overflow** (`L > new_rows`): `excess = min(L - new_rows, cursor's row within the
   segment)`; the first `excess` segment rows stay in Nutshell's scrollback (above the screen),
   the screen top is `S + excess`, and segment rows below the new last row (only possible when
   content sits below the cursor) are dropped. `new_count = S + excess + new_rows`. The cursor
   is never pushed off the screen.
6. **Capacity.** When `S + max(L, new_rows)` exceeds `new_capacity`, the oldest scrollback rows
   are dropped, never segment rows. (The bottom branch's `break` at `:144`/`:160` drops the
   newest rows instead; see section 5.)
7. **Cursor**: its reflowed segment row minus `excess`, clamped as today (`:212-215`).
   `lines_count >= rows` (`:200-207`) holds by construction.

A height-only change is the same algorithm with an identity reflow: a grow keeps the top line
and the cursor row; a shrink moves rows into scrollback only when the cursor (or content below
it) would fall below the new last row, and only as many as that needs.

**1.4 Width changes get the same treatment.** conhost reflows wrapped lines on a width change
(the log's 124→103→292 sequence ends with 35 rows of content at the top, consistent with that)
and Nutshell already rewraps; what differs is only where the result is anchored, and steps 2-5
decide that. The bottom branch's width handling is untouched.

**1.5 The trade-off on shrink.** conhost drops the rows that leave its top; Nutshell keeps them
in its own scrollback. That keeps the model equal to ConPTY's next repaint, because the repaint
addresses only the viewport (`\e[H` .. `\e[r;cH`) and the screen rows are the same in both;
what diverges is only that Nutshell's scrollback holds rows ConPTY no longer knows about, which
is the point of having scrollback. Dropping them would lose history for no gain. The rule that
keeps the two in step is the converse: **in top mode no scrollback row ever moves back onto the
screen**, on any resize. The user-visible cost: after a grow, history stays above the screen
(reachable by scrolling) instead of filling the new rows, as in Windows Terminal; and a 1×1
round trip leaves only the cursor row on screen. The minimised case never resizes
(`window.c:4732-4735`).

**1.6 Alternate screen.** The same mode applies to the alternate buffer (no scrollback, so
`T = 0`, `S = 0`) and to the saved primary buffer reflowed while the alternate screen is up.
Full-screen programs repaint on resize, and ConPTY repaints on alternate-screen exit, so this
only has to be no worse than bottom mode; it is chosen for consistency, not evidence.

**1.7 Where the mode is set.** `start_local_shell()` (`window.c:1106`), immediately after a
successful `local_pty_open_ex()` (`:1149`) where `s->io` is published (`:1170-1180`):
`s->term->anchor = TERM_ANCHOR_TOP;`. The reconnect path calls `start_local_shell()` again
(`:2116-2124`), which sets it again (idempotent). A failed spawn leaves the session in bottom
mode (there is no ConPTY to agree with). After the shell exits the mode stays top: switching
would itself move content. `create_session()` (`:610-614`) is unchanged; the SSH connect path
(`:4646`) never touches the field.

### 2. One resize per DPI change

`WM_DPICHANGED` (`window.c:4798`) becomes, in this order:

1. `g_dpi`, `g_tab_height`, `g_left_margin` (`:4799-4803`) and the AI panel width rescale
   (`:4805-4809`): unchanged, already before the move.
2. **Moved up** from `:4818-4832`: `renderer_free`/`renderer_init` at `newDpi`,
   `apply_config_colors`, `renderer_apply_theme`, `ns_font_flush`, `g_hMenuFont`,
   `tabs_set_font`, `ai_chat_refresh_fonts`.
3. `SetWindowPos` with the suggested rectangle (`:4812-4816`), with the resize reason set to
   `dpi` (section 3). Its synchronous `WM_SIZE` now fits the grid with the new font: one
   `term_resize()` and one `io.resize()` to the final grid.
4. The explicit `WM_SIZE` (`:4834-4838`) stays, also tagged `dpi`. After step 3 it finds the
   grid unchanged (`:4782`) and only re-lays out the children and invalidates; it is the only
   resize when the suggested rectangle equals the current size (no `WM_SIZE` from step 3) and
   the font change alone changes the grid.

What read the old metrics between the move and the rebuild today: `WM_SIZE`'s
`ai_dock_terminal_grid()` call reads `g_renderer.charWidth`/`charHeight` (`:4777-4781`), which
is the bug; the AI panel's and the tab strip's own `WM_SIZE` (`:4747-4760`) ran with the old
fonts and now see the new ones. `g_tab_height`, `g_left_margin` and `g_ai_panel_width` were
already new. Nothing else reads DPI state there. Rebuilding the renderer before the move is
safe: `renderer_init()` sizes the font from the `dpi` argument (`renderer.c:21`) and measures
on the screen DC (`:55-66`), not the window's; no message is pumped between steps 2 and 3, so
no paint sees the new font against the old grid. The sequence for a 192→96 DPI move is then
`WM_DPICHANGED` → (`SetWindowPos` → `WM_SIZE`: 48×124 → 87×292, `shell.resize` once) →
`WM_SIZE` (no grid change), instead of 48→38→85.

### 3. Logging

There is no event catalogue in code: `src/core/dlog_types.c` holds only the category and level
names (`:6`, `:10`). The list lives in the debug-logging spec's section 2 tables; the
implementation appends these rows there, under a new `term` table.

| Event | Level | Fields | Site |
|---|---|---|---|
| `term.resize` | Debug | existing fields (`buffer.c:265-268`) plus `cursor_before=r,c cursor=r,c anchor=top\|bottom why=…` | `term_resize_why()` |
| `term.cursor_jump` | Debug | `reflow_row`, `cup_row`, `ms_since_resize`, `rows`, `anchor` | end of `term_process()` |

New fields are appended, so the existing order is unchanged for anyone parsing it. Rows are
0-based, as in `issue.term`'s `cursor=`. `sid` is the line's session column (`term->log_id`),
as for every `term` event.

**`why=`** values: `size` (a `WM_SIZE` the system sent: drag, maximise, restore), `dpi`,
`zoom` (`apply_zoom`, `:2538`), `ai_dock` (`relayout_main`, `:1789-1794`, whose four callers
are all the AI dock: open, mode switch, slide animation, splitter drag), `settings` (the
post-Settings relayout, `:1766`), `sync` (`sync_session_grid`, `:526-554`: tab switch and
connect), `other` (the default). `term_resize(t, r, c)` becomes a wrapper for
`term_resize_why(t, r, c, "other")`, so the tests' ~45 calls are unchanged. In `window.c` a
file-static `const char *g_size_why` is set (saved and restored) around each synthetic
`SendMessage(WM_SIZE)` and around the `WM_DPICHANGED` `SetWindowPos`; `WM_SIZE` passes
`g_size_why ? g_size_why : "size"`. `sync_session_grid()` passes `"sync"` directly. Seven
literals, no per-call cost beyond a pointer.

**`term.cursor_jump`**: the model was told one cursor row by the reflow and the shell's next
absolute move says another. The decision is pure, in `src/core/cursor_jump.{c,h}` (beside
`render_stuck`):

```c
#define CURSOR_JUMP_WINDOW_MS 500   /* how long after a resize a move is judged */
#define CURSOR_JUMP_MIN_ROWS  2     /* fire when |cup_row - reflow_row| >  this */
typedef enum { CURSOR_JUMP_WAIT, CURSOR_JUMP_FIRE, CURSOR_JUMP_DISARM } CursorJumpVerdict;
CursorJumpVerdict cursor_jump_judge(unsigned ms_since_resize, int reflow_row, int cup_row);
```

`cup_row < 0` (no qualifying move) within the window is `WAIT`; past the window is `DISARM`;
a move within `MIN_ROWS` is `DISARM` (agreement, judged once); further is `FIRE`. `Terminal`
gains `jump_armed`, `jump_reflow_row`, `jump_resize_ms`, `jump_abs_row`. `term_resize_why()`
arms it with the post-reflow cursor row only when the `term` category captures Debug
(`dlog_capture_level`, `dlog.h:142`) and the alternate screen is not active (full-screen
programs move the cursor absolutely as a matter of course). The parser records the target row
of CUP/HVP (`parser.c:356-363`) and VPA (`:488-492`) in `jump_abs_row`, **skipping a move to
row 0, column 0**: ConPTY opens every repaint with `\e[H`, and the repaint arrives in 4096-byte
reads (`local_pty.c:90`; lines 11626-28), so the home move and the final `\e[r;cH` land in
different `term_process()` calls. `term_process()` (`parser.c:605`) resets `jump_abs_row` to
-1 on entry and, if armed, judges the last recorded move on exit; `FIRE` writes one line and
disarms. The clock is a new `unsigned dlog_mono_ms(void)` in `dlog.h`/`dlog.c` returning the
platform's `mono_ms` (`dlog.h:59`; 0 when not started), so `src/term` gets no new seam and the
tests drive it with `fk_advance()` (`tests/fake_dlog.c:121`). The detector is mode-independent
and changes no behaviour; on the 2026-10-18 sequence it would have written
`reflow_row=86 cup_row=34`.

**Snapshot.** `DlogSnapTerm` (`src/core/dlog_snapshot.h:33-49`) gains `const char *anchor`,
printed as `anchor=top|bottom` at the end of `issue.term` (`dlog_snapshot.c:73-84`) and filled
in `window.c:3183-3200`. The last resize's `why` and the cursor fields are already in the
recorder's `term.resize` lines, so nothing else is added.

### 4. Tests

Native, `make test`; new files `tests/test_term_anchor.c` and `tests/test_cursor_jump.c`,
every function declared and called in `tests/runner.c`. "Screen text" means the rows read
through `term_screen_to_phys()` (`buffer.c:319-323`); "history" means the logical text from
the first scrollback row to the cursor, as `term_extract` gives it.

- **Defaults**: `term_init()` gives `TERM_ANCHOR_BOTTOM`; a bottom-mode resize after
  `anchor` was set and cleared matches one that never set it (same `lines_count`, cursor,
  screen text).
- **Top, grow, cursor at the bottom**: 10×20 with 30 numbered lines, cursor on row 9; resize to
  20×20: row 0's text unchanged, cursor still row 9, rows 10-19 blank, `lines_count - rows`
  (scrollback) unchanged.
- **Top, grow, cursor mid-screen**: 24 rows, 5 lines and the cursor on row 5; grow to 40: cursor
  row 5, nothing pulled down, no top padding.
- **Top, shrink, cursor at the bottom**: 24 full rows, cursor on 23; shrink to 14: scrollback
  +10, row 0 is old row 10, cursor row 13.
- **Top, shrink, cursor mid-screen, blank below**: cursor on row 5 of 24; shrink to 14: nothing
  moves, scrollback unchanged, cursor row 5 (bottom mode moves it to row 13; asserted in the
  bottom-mode witness below).
- **Top, shrink, content below the cursor**: cursor row 5, text on row 20; shrink to 14:
  cursor stays on screen, rows below the new last row dropped (open question 3).
- **Top, width narrower / wider**: a 30-character line on 20 columns wraps to two rows at the
  top, unwraps on 40; the cursor follows its character.
- **Top, hard break at the screen top**: a line that starts in scrollback and continues on
  screen row 0; narrowing rewraps the screen part from row 0 on its own, and the scrollback
  part keeps its rows.
- **Top, capacity**: a narrowing that overflows `max_scrollback` drops the oldest scrollback
  rows and keeps every screen row and the cursor.
- **Top, alternate screen**: resize with `?1049h` active; on `?1049l` the primary screen is
  top-anchored at the new size.
- **ConPTY repaint parity (the regression test)**: 48×124, lines `L001`..`L120` then a
  `PS C:\> ` prompt, so `L074`..`L120` fill rows 0-46 and the cursor is on (47,8). Top mode;
  resize to 38×103 then 87×292 (the log's two steps). Expected ConPTY viewport, built by hand
  in the test: `L084`..`L120` and the prompt on rows 0-37, blanks below, cursor (37,8).
  Assert the model's screen equals it **before** any repaint; then feed the repaint ConPTY
  sends (`\e[?25l\e[H`, the 38 rows and then 49 blank rows, each ending `\e[K` and separated by
  `\r\n` with none after the last, then `\e[38;9H\e[?25h`),
  split across three `term_process()` calls at arbitrary byte offsets: screen text and cursor
  are unchanged, and history is `L001`..`L120` plus the prompt, in order, no gaps, no
  duplicates. A second case adds 150-character lines so both steps rewrap.
- **Bottom-mode witness**: the same sequence in bottom mode loses history rows (a gap in
  `L0xx`) and puts the cursor on row 86 before the repaint. This pins today's behaviour for SSH
  and documents why the local shell cannot use it.
- **Logging** (fake dlog, `term` at Debug): `term.resize` carries
  `cursor_before=47,8 cursor=37,8 anchor=top why=other` from `term_resize()` and `why=dpi` from
  `term_resize_why()`; `issue.term` ends `anchor=top` (`tests/test_dlog_snapshot.c:148` is
  updated for the new field; it is a formatter test, not reflow behaviour).
- **`cursor_jump_judge()`** table: within the window and 3 rows away fires; exactly
  `MIN_ROWS` away disarms; at exactly `WINDOW_MS` waits, one past disarms; `cup_row = -1`
  within the window waits; `ms` across the 2^32 wrap.
- **`term.cursor_jump` wiring**: bottom mode, resize, `fk_advance(100)`, `\e[H` alone (no line),
  `\e[35;31H` (one line, `reflow_row=… cup_row=34 ms_since_resize=100`), a second far move (no
  second line); after 600 ms no line; `term` at Info: never armed, no line, clock not read;
  alternate screen active: never armed; top mode with the parity repaint: no line.

`make wintest` (Windows only, real conhost): `test_local_pty_resize_matches_conpty`
in `tests/test_local_pty.c` (`Makefile:142`), registered in `tests/win_runner.c`. Open
`cmd.exe` at 24×80 through `local_pty_open()`, print 60 numbered lines, wait for the prompt
with the file's `wait_for_screen()`, set top mode, then `term_resize()` and
`local_pty_resize()` together to 14×60 and then 40×120, draining after each. Assert the
screen text and cursor just before the drained repaint equal those after it, and that history
holds lines 1-60 in order. Print the repaint escaped (`print_escaped()`) so a failure shows
what this Windows build's conhost actually did; this is the test that answers open questions
1, 3 and 4 on the maintainer's box.

Integration, gate tier, `tests/integration/cases/80-local-shell.ps1`:
`local_resize_keeps_history`, config `debug_enabled`, `debug_level_term = "debug"` (as
`debug_terminal_log_written` does, `60-tabs-logging.ps1:243`). Local cmd tab, 120 numbered
lines, `Set-NutshellWindowSize` (`NutshellIT.psm1:919`) smaller then larger; assert every
`term.resize` line for the session says `anchor=top` and no `term.cursor_jump` line exists.

Not testable natively or by the harness: the `WM_DPICHANGED` order (`src/ui`, and the harness
cannot post `WM_DPICHANGED` across processes because its `lParam` is a pointer into the
receiver). **Manual**, on two monitors at 200% and 100% with Terminal at Debug: drag a local
PowerShell tab holding a few hundred lines each way. Expect one `term.resize why=dpi` per
`WM_DPICHANGED` (no 38-row shrink), `anchor=top`, no `term.cursor_jump`, the prompt on the row
ConPTY's `\e[r;cH` names, and the first lines still reachable in scrollback. Repeat with an SSH
tab: one `why=dpi` resize, bottom-anchored as before.

### 5. Out of scope

- **The scrolled-back repaint storm.** `issue3.log` lines 11761-12797 (17:49:56.627 to
  17:50:01.078): with `scrollback_offset` between 12 and 6, 186 paints had `dirty_rows` equal
  to `sb`, each driven by `poll.invalidate poll_rc=0` (no new bytes) about every 16 ms, every
  `render.draw` reported `rows_left_dirty=0`, and `render.stuck_dirty` never fired. Likely
  cause: the poll counts dirty rows of the live screen while the renderer paints and clears
  the scrolled-back view, so the bottom `sb` live rows are never cleared. Recorded in the notes
  document's Open list; its own fix.
- **Anything about SSH**: bottom mode is unchanged, including its capacity truncation
  (`buffer.c:144`, `:160` stop at `new_capacity` and drop the newest rows, the cursor's among
  them, when a narrowing reflow overflows a full ring). Noted, not fixed here.
- Adopting `PSEUDOCONSOLE_RESIZE_QUIRK` or any other `CreatePseudoConsole` flag
  (`local_pty.c:395` passes 0).
- Paint-path changes; the existing paint follow-ups in the notes document.

---

## Files

| Area | File | Change |
|---|---|---|
| term | `src/term/term.h:60-133` | `TermAnchor`, `anchor`, `jump_*` fields, `term_resize_why()` |
| term | `src/term/buffer.c:34-87`, `:110-269` | init the fields; top branch in `term_reflow_buffer()`; `term_resize_why()`; `term.resize` fields; arm the detector |
| term | `src/term/parser.c:356-363`, `:488-492`, `:605` | record absolute moves; judge at the end of `term_process()` |
| core | `src/core/cursor_jump.{c,h}` | new: `cursor_jump_judge()` and its constants |
| core | `src/core/dlog.{c,h}` | `dlog_mono_ms()` |
| core | `src/core/dlog_snapshot.{c,h}` | `anchor` in `issue.term` |
| ui | `src/ui/window.c:526-554`, `:1170-1180`, `:1766`, `:1789-1794`, `:2538`, `:3183-3200`, `:4723-4840` | `g_size_why`; set top mode; DPI reorder; snapshot field |
| tests | `tests/test_term_anchor.c`, `tests/test_cursor_jump.c`, `tests/runner.c`, `tests/test_dlog_snapshot.c` | new and updated |
| tests | `tests/test_local_pty.c`, `tests/win_runner.c` | wintest parity case |
| tests | `tests/integration/cases/80-local-shell.ps1` | `local_resize_keeps_history` |
| docs | `docs/superpowers/specs/2026-10-17-debug-logging-design.md` section 2 | the two `term` rows |

---

## Open questions (not verified from the code)

1. **conhost's exact resize behaviour on the maintainer's Windows build.** The top-anchored
   rules (copy up to the cursor or the last non-blank row, drop from the top on overflow,
   reflow on a width change, buffer equal to the viewport) are inferred from one log and from
   conhost's design, not read from the inbox conhost. Windows 10 and 11 inbox builds may
   differ. The wintest parity case is the check.
2. **`PSEUDOCONSOLE_RESIZE_QUIRK` (0x2)**: believed to tell conhost that the terminal reflows
   its own buffer, so conhost skips its full-viewport repaint on resize (Windows Terminal
   passes it). Not confirmed; Nutshell passes 0. Whether it would hide or remove the mismatch
   is not proposed here.
3. **Content below the cursor on a shrink** (PSReadLine's list-view predictions draw there):
   the spec keeps the cursor on screen and drops rows below the new last row. conhost's choice
   is unverified.
4. **Soft wraps in ConPTY's repaint**: whether ConPTY emits a soft-wrapped row without
   `\r\n` (so Nutshell marks it `wrapped`). If it ends every row with `\r\n`, as the 2026-10-18
   repaint shows for short lines, Nutshell's screen has hard breaks where conhost has soft
   ones, and the next width change rewraps differently until the following repaint.
5. **Trailing blanks**: `row->len` counts written spaces; conhost may treat a row of spaces as
   blank. That only changes `E` (section 1.3) when nothing but spaces sits below the cursor.
6. **The 85→87 resize at 17:48:03.059** has no `grid.sync` line, so it came through `WM_SIZE`;
   its source is unknown. The `why=` field will name it next time.
7. **Detector thresholds**: 500 ms and more than 2 rows are guesses; skipping a move to the
   home cell means a real jump to (0,0) is never reported.
8. **Alternate buffer**: whether conhost reflows its alternate buffer on resize; section 1.6
   applies top mode there for consistency only.
