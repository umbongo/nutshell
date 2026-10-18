# Local shell: top-anchored reflow, one settled resize per change for ConPTY sessions

**Date**: 2026-10-18
**Status**: DRAFT, revised after one Opus critique (dispositions at the end).
**Branch**: `fix/conpty-resize-anchor` (cut from `main` at `c04945d`, v1.2.36)
**Prompted by**: issue mark 3 of `nutshell-20261018-130024-p15116` (the maintainer's
`issue3.log`, 12,868 lines). In a local PowerShell tab, after the window moved to a 96 DPI
monitor and the grid grew from 48 to 87 rows, the prompt sat on row 35 of 87 and the history
Nutshell had pulled down onto the screen was erased.
**Builds on**: `2026-09-22-local-shell-design.md` (ConPTY transport, section 3 "Resize"),
`2026-10-17-debug-logging-design.md` (event conventions, `term` category, issue snapshot).

---

## Problem

Three defects meet when a local tab is resized.

**The model is bottom-anchored; ConPTY is top-anchored.** `term_reflow_buffer()`
(`src/term/buffer.c:110-223`) lays the whole ring (scrollback and screen) out linearly at the
new width, strips blank rows below the cursor (`:173-179`), and takes the screen as the last
`new_rows` rows (`:209-211`). On a grow with history above, that pulls history down so the
cursor stays on the last row; on a grow without enough history it pads the top with blanks
(`:186-198`); on a shrink it pushes rows into scrollback until the cursor is on the last row,
even when the rows below the cursor were blank. ConPTY's buffer is exactly its viewport: on a
resize it keeps its top line fixed, has nothing to pull down, and then repaints its whole
viewport. Every row Nutshell pulled out of its scrollback onto the screen is overwritten by
that repaint and is gone from both.

| `issue3.log` line | Time | Event |
|---|---|---|
| 11614-15 | 17:48:02.695 | `term.resize rows_before=48 rows=38 cols_before=124 cols=103 lines_before=1042 lines=1536`, matching `shell.resize` |
| 11617-18 | 17:48:02.830 | `term.resize rows_before=38 rows=85 cols_before=103 cols=292 lines_before=1536 lines=1002`, matching `shell.resize` |
| 11620-21 | 17:48:03.059 | `term.resize rows_before=85 rows=87`, matching `shell.resize`; `render.draw char_h=15` follows |
| 11626-28 | 17:48:03.258 | ConPTY's repaint in three reads (4096, 4096, 1098 bytes): `\e[H`, 35 content rows each ending `\e[K\r\n`, ~52 bare `\e[K` rows, `\e[35;31H\e[?25h` |

Both earlier resizes also changed the width, so the width reflow is part of the bug. Row
counts never disagreed between `term.resize` and `shell.resize`; only the vertical anchoring
did. The log has no cursor field (section 4 adds one), but by the code above the cursor was on
row 86 before the repaint with every row above it filled from scrollback; the repaint put 35
rows of ConPTY's content on rows 0-34, blanked rows 35-86 (the pulled history) and moved the
cursor to row 34. Every later ConPTY repaint (AI panel drag, zoom) re-asserts `\e[35;31H`.

**Every intermediate size of a drag reaches ConPTY, and its frames arrive late.**
`WM_SIZE` calls `io.resize` on every grid change (`window.c:4783-4787`), as does
`sync_session_grid()` (`:550-553`); a window drag, an AI-dock slide (the animation tick calls
`relayout_main()` every 16 ms, `:4433`) and a splitter drag (`:5193`) each produce a run of
them. At 15:55:59.091-.489 (lines 679-746) twelve width-only resizes (158 → 121 columns)
went to ConPTY 24-50 ms apart; ConPTY's frames for them arrived together at 15:55:59.603
(lines 763-769, seven reads holding twelve full frames, each `\e[H` .. `\e[36;31H`) and were
applied at 121 columns. The same happened at 15:56:01.406 and 15:56:02.245. A frame rendered
at a wider intermediate width and applied at the final one wraps every row longer than the
final width, and the extra rows scroll the model: rows of the screen go into scrollback a
second time. In this burst the rows were short enough not to wrap; the mechanism does not
depend on that. `local_pty_poll()` feeds each 4096-byte read straight into `term_process()`
(`local_pty.c:564-578`), so nothing tells a stale frame from a current one.

**The DPI handler resizes twice.** `WM_DPICHANGED` (`window.c:4798-4840`) calls
`SetWindowPos` with the suggested rectangle (`:4812-4816`) before it rebuilds the renderer's
fonts (`:4819-4821`). `SetWindowPos` sends `WM_SIZE` synchronously, and `WM_SIZE` fits the grid
with `g_renderer.charWidth`/`charHeight` (`:4777-4781`), still the 192 DPI font's, to the new,
smaller client area: the 48→38 shrink at 02.695. The explicit `WM_SIZE` after the rebuild
(`:4834-4838`) then grows to 85. ConPTY has no scrollback, so the transient shrink discarded
the rows that fell off its top. This one is churn rather than the root cause: with sections 1
and 2 in place, the transient shrink no longer reaches ConPTY or loses anything, but it still
reflows the model twice and paints a wrong grid for a frame.

**The primitives under the reflow are not trustworthy.** Top-anchored reflow decides how much
of the screen to carry by the last non-blank row, so `TermRow.len` must be the written width
(the CLAUDE.md invariant) and `wrapped` must mean "continues the row above". Neither holds
after ConPTY's repaints, which are made of exactly the sequences that break them: EL never
touches `len` or `wrapped` (`src/term/parser.c:437-458`); ED 0 leaves the cursor row's `len`
(`:368-375`) and the cleared rows' `wrapped` (`:376-386`); ED 1 leaves the cursor row's `len`
(`:400-414`) and the cleared rows' `wrapped` (`:388-399`); only ED 2 maintains both
(`:415-427`). And `wrapped` is only ever set, on auto-wrap (`:59-72`); a line feed onto a row
that was once a continuation (`:641-647`) leaves it marked. After `\e[H` + `text\e[K\r\n` over
rows that used to hold longer text, `len` keeps the old width and stale `wrapped` flags join
rows that ConPTY painted as separate lines.

Ruled out by the log: the scroll region (reset on every resize, `buffer.c:262-263`); the
renderer skipping rows (`rows_left_dirty=0` throughout, `render.stuck_dirty` never fired);
scrollback offset (0 at onset); the AI dock and zoom (after onset). PR #66 (v1.2.34) fixed
stale rows over a correct model; here the model is wrong. The log cannot be replayed
byte-exact: `term.rx` lines are cut at 4096 bytes of line (`truncated=1`), two of the three
reads at 17:48:03.258 and six of the seven at 15:55:59.603.

---

## Design

### 1. Top-anchored reflow for ConPTY sessions

**1.1 The mode.** `term.h` gains

```c
typedef enum { TERM_ANCHOR_BOTTOM = 0, TERM_ANCHOR_TOP = 1 } TermAnchor;
```

and `Terminal` (`src/term/term.h:60-129`) gains `TermAnchor anchor` and `int anchor_held`
(section 2), both set in `term_init()` (`buffer.c:34-87`, which uses `xmalloc`, so they are
set explicitly beside `log_id` at `:76`). `term_reflow_buffer()` takes the mode as a parameter;
`term_resize()` passes `term->anchor` for the main screen and `TERM_ANCHOR_BOTTOM` for the
alternate screen (1.6). The mode is read at each resize; changing it never moves content.

**1.2 `TERM_ANCHOR_BOTTOM` is today's code path, unchanged.** The mode selects a separate
branch; the bottom branch is the current body line for line. SSH sessions never set the mode,
so their behaviour and every existing resize test (`tests/test_term.c:215-345`, `:943`, `:977`,
`:1165-1192`; `tests/test_term_fuzz.c:414-460`; `test_dirty.c`, `test_scroll_region.c`,
`test_selection.c`, `test_color_consistency.c`, `test_term_extract.c`) stay as they are. (The
parser fixes in section 3 apply to every session; they must leave every existing assertion
as it is, and any test they would change is a finding for review, not an edit to the test.)

**1.3 `TERM_ANCHOR_TOP` semantics, main screen** (what conhost does with a buffer that is its
viewport). With `T = old_count - old_rows` the old screen's top row (`buffer.c:132`), `H =
anchor_held` (section 2; 0 outside a resize episode), the cursor's logical row `C` (`:133`),
and `E = max(C, last row in [T, old_count) with len > 0)`:

1. **Scrollback** `[0, T - H)` is reflowed at the new width exactly as today (`:135-168`),
   giving `S` rows. Wrapped flags are honoured across the screen boundary as everywhere else:
   no forced break at row `T`. That costs nothing on the screen (ConPTY's repaint rewrites
   every screen row and its flags) and keeps history as logical lines.
2. **Screen segment** `[T - H, E]` is reflowed at the new width into `L` rows. Rows after `E`
   are blank and are not carried.
3. **Fit** (`L <= new_rows`): the screen top is row `S`; rows `[S+L, S+new_rows)` are fresh
   blanks. No top padding, no history pulled down.
4. **Overflow** (`L > new_rows`): `excess = L - new_rows`. The first `excess` segment rows
   become scrollback, the screen top is `S + excess`, and nothing below is dropped. The cursor
   follows its row: its new row is its segment row minus `excess`, clamped to 0 when content
   below it was long enough to push its own row out (the test pins this; open question 3).
5. **Capacity.** The rows kept are `S + max(L, new_rows)`, counted before any row is copied;
   when that exceeds `new_capacity` the oldest scrollback rows are dropped, never segment rows.
   (The bottom branch's `break` at `:144`/`:160` drops the newest rows instead; section 6.)
6. **Scroll offset.** A view scrolled back keeps showing the same history, as `term_scroll()`
   does (`:289-295`): `scrollback_offset` grows by the rows that entered scrollback
   (`excess - H`, when positive) and shrinks by the rows that left it (`H - excess`, when
   positive), clamped to `[0, lines_count - rows]`.
7. `anchor_held = excess` afterwards (section 2). `lines_count >= rows` (`:200-207`) holds by
   construction; the cursor column is clamped as today (`:212-215`).

A height-only change is the same algorithm with an identity reflow. A width change is the
same algorithm with real rewrapping, which is what conhost does on a width change (the log's
124→103→292 sequence ends with 35 rows of content at the top, consistent with that).

**1.4 The trade-off on shrink.** conhost drops the rows that leave its top; Nutshell keeps
them in scrollback. The model still equals ConPTY's next repaint, which addresses only the
viewport; what diverges is that Nutshell's scrollback holds rows ConPTY no longer has, which is
what scrollback is for. The converse rule keeps the two in step: **no scrollback row returns to
the screen**, except the rows the current resize episode itself pushed out, which ConPTY never
lost (section 2). After a grow, history stays above the screen (reachable by scrolling) instead
of filling the new rows; a 1×1 round trip leaves only the cursor row on screen (the minimised
case never resizes, `window.c:4732-4735`).

**1.5 Where the mode is set.** `start_local_shell()` (`window.c:1106`), immediately after a
successful `local_pty_open_ex()` (`:1149`) where `s->io` is published (`:1170-1180`):
`s->term->anchor = TERM_ANCHOR_TOP`. Reconnect calls it again (`:2123`). A failed spawn stays
bottom; after the shell exits the mode stays top. The SSH connect path (`:4646`) never touches
it. The mode is per session, so it can later become a profile option (section 6).

**1.6 The alternate screen keeps today's handling.** It has no scrollback and full-screen
programs repaint it on resize; the alternate buffer is reflowed bottom-anchored exactly as now
(`buffer.c:235-243` with `max_scrollback` 0). The saved primary buffer reflowed while the
alternate screen is up (`:248-257`) is the main screen and uses the session's mode.

**1.7 Reconnect keeps the old screen.** A new ConPTY opens with `\e[2J` .. `\e[H`
(`tests/test_term_conpty.c:144-152`), and ED 2 clears the screen rows in place
(`parser.c:415-427`), so today a reconnect erases whatever the dead shell left on screen.
The reconnect path (`window.c:2122`, which writes `\r\n` before `start_local_shell()`) instead
calls a new `term_push_screen_to_scrollback(term)` (`buffer.c`): `term_scroll()` once per row
up to the last non-blank screen row, then the cursor to (0,0). The clear then hits blank rows.

### 2. One settled ConPTY resize per change

**2.1 Coalescing.** The model resizes at once (the window needs the grid); the pseudo-console
resize is deferred until the size has been stable for `PTY_RESIZE_SETTLE_MS` = **75 ms**. The
drag in the log stepped every 24-50 ms, so a drag, an AI-dock slide (16 ms animation ticks) or
a splitter drag produces one `ResizePseudoConsole` at its end; a pause longer than 75 ms
mid-drag produces one more. The decision is pure, in `src/core/pty_resize_settle.{c,h}`:

```c
#define PTY_RESIZE_SETTLE_MS 75
typedef struct { int pending; int cols, rows; unsigned last_ms; } PtyResizeSettle;
void prs_request(PtyResizeSettle *st, int cols, int rows, unsigned now_ms); /* restarts the wait */
int  prs_take(PtyResizeSettle *st, unsigned now_ms, int *cols, int *rows);  /* 1 once, when settled */
```

`Session` (`window.c:106-149`) gains a `PtyResizeSettle`. For a `SESSION_LOCAL` session, the
two `io.resize` calls (`:552`, `:4786-4787`) become `prs_request(..., GetTickCount())`; SSH
sessions keep the immediate call. The existing 10 ms poll timer (`SetTimer(hwnd, 1, 10)`,
`:3880`, handled at `:4161`) calls `prs_take()` for each local session before polling it; on 1
it calls `io.resize` and then `term_anchor_settled(term)`, which zeroes `anchor_held`.
`local_pty_resize()` already skips a call to the size ConPTY has (`local_pty.c:646`).

**2.2 Path independence within an episode.** ConPTY sees one resize, from the size it had to
the final one; the model saw every step. Top-mode reflow is path-independent except where a
step overflowed and pushed screen rows into scrollback that the final size would have kept (a
shrink-then-grow inside one drag, a narrow-then-wide). `anchor_held` records the rows the last
reflow pushed out since ConPTY was last told a size; the next reflow takes them back into the
screen segment (1.3, `[T - H, E]`), so the model after the episode equals one reflow from the
start size to the end size, which is what conhost performs. It also covers a drag that ends
at the starting size: `local_pty_resize()` then sends nothing, ConPTY never repaints, and the
model is back where ConPTY is.

**2.3 The residual race, stated.** Between the model's resize and the settled ConPTY resize,
and while ConPTY's frame for a settled size is still in its pipe, output arrives rendered for
a different size than the model's. The model applies it as it is: rows wider than the grid
auto-wrap, a line feed on the last row scrolls, absolute moves are clamped (`clamp_cursor`).
`term_scroll()` zeroes `anchor_held` (an output scroll ends path independence for that
episode). The full repaint that follows every ConPTY resize rewrites each screen row from
`\e[H`, so the screen converges; what can remain is rows a stale frame scrolled into scrollback
(duplicated history) and, after an output scroll, an episode whose pushed-out rows stay in
scrollback. It needs output during a resize; an idle prompt (both incidents in the log) is
fully covered.

### 3. The primitives: `len` and `wrapped`

In `parser.c`, every erase leaves `len` equal to one past the last cell with a non-zero
codepoint, and clears `wrapped` on any row it clears from column 0:

- **EL** (`:437-458`): after clearing `[start, end)`, if `end >= len` then `len = start`, then
  trailing zero cells are trimmed. EL 1 and EL 2 clear `wrapped` (the row's start is gone).
- **ED 0**: the cursor row as EL 0 (`:368-375`); the rows below also clear `wrapped`
  (`:376-386`, `len = 0` is already there).
- **ED 1**: the rows above also clear `wrapped` (`:388-399`); the cursor row as EL 1
  (`:400-414`).
- **ED 2** (`:415-427`) already does both.
- **Line feed** (`:641-647`): the row the cursor lands on, by moving down or by scrolling,
  gets `wrapped = false`: the row above ended in a hard break. A scrolled-in row is already
  clean (`term_row_fill`, `buffer.c:24-32`).

"Blank" in 1.3 is then `len == 0`. A cell holding a space keeps `len` (it was written); open
question 5 asks whether conhost agrees.

### 4. DPI change: renderer before `SetWindowPos`

This is churn reduction, not the fix: it removes a model reflow, a wrong-grid paint and, before
section 2 coalesces it, a ConPTY resize and its frame. `WM_DPICHANGED` (`window.c:4798`)
becomes, in this order:

1. `g_dpi`, `g_tab_height`, `g_left_margin` (`:4799-4803`) and the AI panel width rescale
   (`:4805-4809`): unchanged.
2. **Moved up** from `:4818-4832`: `renderer_free`/`renderer_init` at `newDpi`,
   `apply_config_colors`, `renderer_apply_theme`, `ns_font_flush`, `g_hMenuFont`,
   `tabs_set_font`, `ai_chat_refresh_fonts`.
3. `SetWindowPos` (`:4812-4816`), tagged `why=dpi` (section 5). Its synchronous `WM_SIZE` fits
   the final grid.
4. The explicit `WM_SIZE` (`:4834-4838`) stays, also `dpi`: after step 3 the grid is unchanged
   (`:4782`) and it only re-lays out the children; it is the only resize when the suggested
   rectangle equals the current size and the font alone changes the grid.

Today the only stale read between the move and the rebuild is the char size in `WM_SIZE`
(`:4777-4781`); the AI panel's and tab strip's own `WM_SIZE` (`:4747-4760`) now see the new
fonts too. `renderer_init()` sizes the font from its `dpi` argument (`renderer.c:21`) and
measures on the screen DC (`:55-66`), so rebuilding before the move gives the same metrics; no
message is pumped between steps 2 and 3. A 192→96 move becomes one 48×124 → 87×292 resize
instead of 48→38→85. The reverse move (96→192) today grows to a transient grid of roughly
twice the final rows (the old 15 px font in the doubled client area) and then shrinks to the
final grid; under top mode that transient grow adds blank rows below and the shrink removes
them, so it lost nothing even before the reorder, and with it the transient disappears. The
85→87 resize at 17:48:03.059 is still unexplained (no `grid.sync` line, so it came through
`WM_SIZE`); `why=` will name it.

### 5. Logging

There is no event catalogue in code (`src/core/dlog_types.c` holds only the category and level
names, `:6`, `:10`); the list is the debug-logging spec's section 2, where the implementation
adds a `term` row.

**`term.resize`** (Debug, `buffer.c:265-268`) appends `cursor_before=r,c cursor=r,c
anchor=top|bottom why=…` after its current fields, so the existing order is unchanged; rows are
0-based like `issue.term`'s `cursor=`. `why` values: `size` (a `WM_SIZE` from the system:
drag, maximise, restore), `dpi`, `zoom` (`apply_zoom`, `:2538`), `ai_dock` (`relayout_main`,
`:1789-1794`, whose four callers are the AI dock's open, mode switch, slide tick and splitter
drag), `settings` (`:1766`), `sync` (`sync_session_grid`, `:526-554`), `other` (default).
`term_resize(t, r, c)` becomes a wrapper for `term_resize_why(t, r, c, "other")`, so the
tests' ~45 calls are unchanged. `window.c` sets a file-static `const char *g_size_why` (saved
and restored) around each synthetic `SendMessage(WM_SIZE)` and the `WM_DPICHANGED`
`SetWindowPos`; `WM_SIZE` passes `g_size_why ? g_size_why : "size"`; `sync_session_grid()`
passes `"sync"`.

**`shell.resize`** (`local_pty.c:657`) is unchanged; with section 2 its count against
`term.resize` shows the coalescing directly.

**Snapshot**: `DlogSnapTerm` (`src/core/dlog_snapshot.h:33-49`) gains `const char *anchor`,
printed as `anchor=top|bottom` at the end of `issue.term` (`dlog_snapshot.c:73-84`), filled in
`window.c:3183-3200`.

### 6. Out of scope

- **SSH, including SSH to Windows.** Bottom mode is unchanged. A session to Win32-OpenSSH runs
  the remote shell under a server-side ConPTY and shows the same repaint pattern; because the
  mode is per session it can later be a profile option (or follow platform detection). Recorded
  in the notes' Open list. Also unchanged: the bottom branch's capacity truncation
  (`buffer.c:144`, `:160` drop the newest rows, the cursor's among them, when a narrowing
  reflow overflows a full ring).
- **The scrolled-back repaint storm** (`issue3.log` 11761-12797, 17:49:56.627-17:50:01.078:
  186 paints with `dirty_rows` equal to `sb`, driven by `poll.invalidate poll_rc=0`,
  `rows_left_dirty=0`, no `render.stuck_dirty`). In the notes' Open list.
- **Wide characters**: the parser gives every codepoint one cell; ConPTY gives East Asian wide
  characters and most emoji two. Pre-existing and independent of anchoring. In the notes' Open
  list.
- `PSEUDOCONSOLE_RESIZE_QUIRK` or any other `CreatePseudoConsole` flag (`local_pty.c:395`
  passes 0), and paint-path changes.

---

## Tests

Native, `make test`; new files `tests/test_term_anchor.c`, `tests/test_parser_erase.c` and
`tests/test_pty_resize_settle.c`, every function declared and called in `tests/runner.c`.
"Screen text" is the rows read through `term_screen_to_phys()` (`buffer.c:319-323`); "history"
is the logical text from the first scrollback row to the cursor, as `term_extract` gives it.

**Primitives** (`test_parser_erase.c`), each with a negative case: EL 0 at column 5 of a
20-wide row leaves `len` 5; EL 1 at column 3 of a 10-long row keeps `len` 10 (negative case);
EL 2 leaves `len` 0; EL 1 and EL 2 clear `wrapped`, EL 0 does not; ED 0 trims the cursor row and clears `wrapped` below; ED 1 clears `wrapped` above;
a line feed onto a row marked `wrapped` clears it; an auto-wrap still sets it; `\e[H` +
`ab\e[K\r\n` over two longer, wrapped rows leaves `len` 2 and no `wrapped` on row 1.

**Top mode** (`test_term_anchor.c`):

- **Defaults**: `term_init()` gives bottom mode and `anchor_held` 0.
- **Grow, cursor at the bottom**: 10×20, 30 numbered lines, cursor row 9; resize to 20×20: row
  0 unchanged, cursor row 9, rows 10-19 blank, scrollback (`lines_count - rows`) unchanged.
- **Grow, cursor mid-screen**: 24 rows, 5 lines, cursor row 5; grow to 40: cursor row 5, no
  pull-down, no top padding.
- **Shrink, cursor at the bottom**: 24 full rows, cursor 23; shrink to 14: scrollback +10, row
  0 is old row 10, cursor row 13.
- **Shrink, blank below the cursor**: cursor row 5 of 24; shrink to 14: nothing moves.
- **Shrink, content below the cursor**: cursor row 5, text on row 20; shrink to 14: `excess` 7,
  row 0 is old row 7, the cursor is clamped to row 0 (pinned; the wintest dump compares it with
  conhost).
- **Width**: a 30-character line on 20 columns wraps to two rows at the top and unwraps on 40;
  a line that starts in scrollback and continues on screen row 0 rejoins in history after a
  narrowing (no forced break).
- **Capacity**: a narrowing that overflows `max_scrollback` drops the oldest scrollback rows
  and keeps every screen row and the cursor.
- **Scroll offset**: scrolled back 12 rows, a shrink with `excess` 10 leaves offset 22 and the
  same top visible line.
- **Episode**: 24 full rows, shrink to 14 then grow to 24 without `term_anchor_settled()`: the
  screen equals the original (the 10 rows came back); with `term_anchor_settled()` between the
  two, they stay in scrollback; an output line between the two (a scroll) zeroes
  `anchor_held`.
- **Alternate screen**: a resize with `?1049h` active reflows the alternate buffer exactly as
  bottom mode does (same result as a bottom-mode terminal), and on `?1049l` the primary is
  top-anchored at the new size.
- **Reconnect**: `term_push_screen_to_scrollback()` then the recorded ConPTY opening
  (`test_term_conpty.c:152`) leaves the old screen's text in history and a blank screen.
- **Bottom-mode witness**: the log's sequence (48×124 with 120 numbered lines and a prompt;
  38×103; 87×292) puts the cursor on row 86 in bottom mode and on row 37 in top mode, before
  any repaint. Pins today's behaviour for SSH.
- **Logging** (fake dlog, `term` at Debug): `term.resize` carries `cursor_before=`, `cursor=`,
  `anchor=top`, `why=other` from `term_resize()` and `why=dpi` from `term_resize_why()`;
  `issue.term` ends `anchor=top` (`tests/test_dlog_snapshot.c:148` updated for the field).

**Settling** (`test_pty_resize_settle.c`): the twelve request times of the 15:55:59 burst
(.091, .135, .171, .202, .234, .284, .323, .354, .378, .416, .457, .489) produce no take until
.564 and exactly one, with 121 columns; every request restarts the wait, including one for the
size already pending (the drag is still moving); a request after a take starts a new episode;
no take without a request; `now_ms` across the 2^32 wrap.

**Recorded fixtures** (`tests/fixtures/*.bin`, replayed like `test_term_conpty.c:57-147`):
the log's `term.rx` lines are truncated, so the fixtures are recorded on the maintainer's box
by a wintest helper (below) and committed. `conpty_resize_burst_pwsh.bin`: PowerShell after
`dir`, twelve `ResizePseudoConsole` calls 30 ms apart from 158 to 121 columns, then a drain,
as the 15:55:59 drag did. The native test feeds it to a 48×121 top-mode terminal: with lines
shorter than 121 the screen equals the burst's last frame and scrollback gains no rows. A
second recording, `conpty_resize_settled_pwsh.bin`, makes the same change with one call; the
native test asserts the same result from one frame, which is what section 2 delivers.

**`make wintest`** (Windows, real conhost; `tests/test_local_pty.c`, `Makefile:142`,
registered in `tests/win_runner.c`). A new child mode `--console-probe` beside `--dump-keys`
(`win_runner.c:68`, `:159`; the precedent is `test_local_pty.c:410-447`): `--console-probe
emit <scenario>` writes a scenario's bytes to its console (long lines that wrap, content left
below the cursor with a cursor-up); `--console-probe dump <file>` reads its own console with
`GetConsoleScreenBufferInfo` and `ReadConsoleOutputCharacterW` and writes every buffer row and
the cursor to a file, touching nothing on screen. The case runs both under `cmd.exe` and under
PowerShell (`powershell.exe`, and `pwsh.exe` when installed) inside `local_pty_open()`: emit,
drain, resize the model and the pseudo-console through the settling path (shrink, then grow,
then a width change), drain, run the dump, drain, and compare the model's screen and cursor
with the dump row by row. The same helper records the two fixtures above. It settles open
questions 1, 3, 4 and 5 for the build it runs on, and prints the repaint bytes escaped
(`print_escaped()`) on a failure.

**Integration, gate tier** (`tests/integration/cases/80-local-shell.ps1`):
`local_resize_keeps_history`, with `debug_enabled` and `debug_level_term = "debug"` as
`debug_terminal_log_written` does (`60-tabs-logging.ps1:243`). A local `cmd` tab prints 120
numbered lines and then `KEEP_MARKER`; `Set-NutshellWindowSize` (`NutshellIT.psm1:919`) shrinks
then grows the window. There is no text query in the harness today, so the case reads the
screen with Edit > Select All and Edit > Copy (`Send-NutshellCommand` with 2012 and 2010,
`resource.h:21-23`) and `Get-Clipboard`: `KEEP_MARKER` and the prompt are on screen after each
step, and after the grow the top line is the one that was there after the shrink. History is
read from the debug log: every `term.resize` has `anchor=top`, the grow's `lines - rows` is
not below its `lines_before - rows_before` (nothing pulled down), and the shrink's is above it
by the rows that left the screen; one `shell.resize` per step.

**Manual**, on two monitors at 200% and 100%, Terminal at Debug: drag a local PowerShell tab
holding a few hundred lines each way. Expect one `term.resize why=dpi` per `WM_DPICHANGED`, one
`shell.resize`, `anchor=top`, the prompt on the row ConPTY's `\e[r;cH` names, and the first
lines reachable in scrollback. Repeat with an SSH tab: one `why=dpi` resize, bottom-anchored.
The harness cannot post `WM_DPICHANGED` across processes (its `lParam` points into the
receiver).

---

## Files

| Area | File | Change |
|---|---|---|
| term | `src/term/term.h:60-133` | `TermAnchor`, `anchor`, `anchor_held`, `term_resize_why()`, `term_anchor_settled()`, `term_push_screen_to_scrollback()` |
| term | `src/term/buffer.c:34-87`, `:110-310` | init; top branch; `anchor_held` and offset upkeep; `term_scroll()` zeroes `anchor_held`; `term.resize` fields; push-to-scrollback |
| term | `src/term/parser.c:364-458`, `:641-647` | `len` and `wrapped` upkeep in EL, ED and line feed |
| core | `src/core/pty_resize_settle.{c,h}` | new: the settle decision |
| core | `src/core/dlog_snapshot.{c,h}` | `anchor` in `issue.term` |
| ui | `src/ui/window.c:106-149`, `:526-554`, `:1170-1180`, `:1766`, `:1789-1794`, `:2122`, `:2538`, `:3183-3200`, `:4161`, `:4723-4840` | settle state and take; `g_size_why`; top mode; reconnect push; DPI reorder; snapshot |
| ui | `src/ui/resource.h` | `APP_VERSION`/`APP_VERSION_BINARY` bump |
| build | `README.md` `**Version**:` line, `build/win/nutshell.exe` | version and rebuilt exe for the gate |
| tests | `tests/test_term_anchor.c`, `tests/test_parser_erase.c`, `tests/test_pty_resize_settle.c`, `tests/runner.c`, `tests/test_dlog_snapshot.c`, `tests/test_term_conpty.c`, `tests/fixtures/conpty_resize_*.bin` | new and updated |
| tests | `tests/test_local_pty.c`, `tests/win_runner.c` | `--console-probe`, parity case, fixture recorder |
| tests | `tests/integration/cases/80-local-shell.ps1` | `local_resize_keeps_history` |
| docs | `docs/superpowers/specs/2026-10-17-debug-logging-design.md` section 2 | the `term.resize` row |

---

## Open questions (not verified from the code)

1. **conhost's resize behaviour on the maintainer's build**: copy up to the cursor or the last
   non-blank row, drop from the top on overflow, reflow on a width change, a buffer equal to
   the viewport. Inferred from one log and conhost's design; Windows 10 and 11 inbox builds may
   differ. Settled per build by the wintest dump.
2. **`PSEUDOCONSOLE_RESIZE_QUIRK` (0x2)**: believed to tell conhost that the terminal reflows
   itself so conhost skips its full repaint on resize (Windows Terminal passes it); not
   confirmed. Nutshell passes 0; not proposed.
3. **Content below the cursor on a shrink** (PSReadLine's list-view predictions): the spec
   scrolls out what `L` needs and clamps the cursor to row 0 if its own row goes; conhost's
   choice is unverified. Settled by the wintest dump.
4. **Soft wraps in ConPTY's repaint**: whether ConPTY emits a soft-wrapped row without `\r\n`
   so the model marks it `wrapped`; if not, history rejoins differently from conhost's lines.
   Settled by the wintest dump.
5. **Trailing spaces**: `len` counts written spaces; conhost may treat a row of spaces as
   blank, which changes `E` only when nothing but spaces sits below the cursor. Settled by the
   wintest dump.
6. **The 85→87 resize at 17:48:03.059**: no `grid.sync`, so `WM_SIZE`; source unknown until
   `why=` names it.
7. **`WM_SIZE` when `SetWindowPos` does not change the size**: assumed that `DefWindowProc`
   sends no `WM_SIZE` from `WM_WINDOWPOSCHANGED` then (hence step 4 of section 4 stays);
   unverified.
8. **The ConPTY opening clear** (`\e[2J` .. `\e[H`): recorded on one build
   (`test_term_conpty.c:144`); whether every supported build sends it on open, and whether one
   ever sends `\e[3J`, is unverified. Section 1.7 is harmless either way.
9. **The settle time**: 75 ms is chosen from one log's drag cadence; a slower machine's drag
   could step more slowly and send more resizes (correct, only more frames).
10. **Replaying the incidents**: the 17:48 and 15:55 bursts cannot be rebuilt from the log
    (`truncated=1`); the fixtures are fresh recordings of the same actions, not the incident's
    bytes.

---

## Critique (round 1, 2026-10-18)

One Opus critique, thirteen findings; dispositions by the maintainer's session.

1. Stale repaints for intermediate sizes applied at a later size. **Accepted**: section 2
   (75 ms settle via the poll timer, `anchor_held` for path independence, the residual race
   stated), Problem paragraph with the 15:55:59 evidence, the settle test and the burst fixture.
2. `len` not shrunk by EL/ED 0/ED 1, `wrapped` stale. **Accepted**: section 3 fixes the
   primitives, "blank" is `len == 0`, `test_parser_erase.c`.
3. Circular native parity test, weak wintest. **Accepted**: the hand-built viewport is gone;
   recorded fixtures plus the `--console-probe` dump compared with the model under cmd and
   PowerShell, with wrapping lines and content below the cursor; it settles questions 1, 3-5.
4. Alternate-screen contradiction. **Accepted**: top mode is main screen only; the alternate
   buffer keeps bottom handling (1.1, 1.6).
5. Overflow put the cursor at row 0 when content lay below it. **Accepted**: overflow is
   defined by `L` over `max(cursor, last non-blank)`, the cursor follows its row, the test pins
   the result, the comparison with conhost is left to the dump.
6. SSH to a Windows host. **Accepted as scope**: section 6 says Win32-OpenSSH shows the same
   pattern and the mode could become a profile option; added to the notes' Open list.
7. The `cursor_jump` detector. **Accepted the cheaper option**: dropped with `dlog_mono_ms`,
   `cursor_jump.{c,h}` and their tests; `term.resize` keeps `cursor_before`, `cursor`,
   `anchor`, `why`; `issue.term` keeps `anchor`; the integration case no longer asserts it.
8. Reconnect erases the screen. **Accepted**: 1.7, `term_push_screen_to_scrollback()` at
   `window.c:2122`, with a test.
9. Forced hard break at the screen top. **Accepted**: removed; row `T` keeps its flag (1.3
   step 1); nothing on screen changes and history keeps logical lines.
10. `scrollback_offset` on a top-mode shrink. **Accepted**: 1.3 step 6, as `term_scroll()`
    does, with a test.
11. DPI reorder is churn, not correctness. **Accepted**: section 4 says so, works the 96→192
    move, and keeps 85→87 as unexplained for `why=` to name.
12. Wide characters. **Rejected for this spec**: pre-existing and independent of anchoring;
    section 6 and one line in the notes' Open list.
13. Smaller gaps. **Accepted**: Files gains `resource.h`, README's version line and the exe;
    the integration case asserts history (screen copy plus the log's scrollback counts); the
    capacity check counts the rows actually kept; the fixture precedent is used.
