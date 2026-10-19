# Local shell: top-anchored reflow and throttled resizes for ConPTY sessions

**Date**: 2026-10-18
**Status**: DRAFT, revised after two Opus critiques (dispositions at the end); for a draft PR
with the questions in "Open for the maintainer".
**Branch**: `fix/conpty-resize-anchor` (cut from `main` at `c04945d`, v1.2.36)
**Prompted by**: issue mark 3 of `nutshell-20261018-130024-p15116` (the maintainer's
`issue3.log`, 12,868 lines). In a local PowerShell tab, after the window moved to a 96 DPI
monitor and the grid grew from 48 to 87 rows, the prompt sat on row 35 of 87 and the history
Nutshell had pulled down onto the screen was erased.
**Builds on**: `2026-09-22-local-shell-design.md` (ConPTY transport, section 3 "Resize"),
`2026-10-17-debug-logging-design.md` (event conventions, `term` category, issue snapshot).

---

## Problem

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
did. The log has no cursor field (section 5 adds one), but by the code above the cursor was on
row 86 before the repaint with every row above it filled from scrollback; the repaint put 35
rows of ConPTY's content on rows 0-34, blanked rows 35-86 (the pulled history) and moved the
cursor to row 34. Every later ConPTY repaint (AI panel drag, zoom) re-asserts `\e[35;31H`.

**The reflow is not an identity, for any session.** Two faults in the shared loop:

- A row whose content ends exactly at the new width gains a blank row. Copying its last cell
  makes `cur_new_col` reach `new_cols`, so the loop starts a new row and marks it `wrapped`
  (`buffer.c:157-161`); the next unwrapped source row then starts another (`:140-143`). The
  blank `wrapped` row stays in the buffer.
- A cursor past the row's content is pulled back to the content's end (`:164-167`). Every
  ConPTY prompt sits one past its text: the repaint at line 11628 writes
  `PS C:\Users\thoma\lightcraft>` (29 cells) and then `\e[35;31H`, column 30.

**Every intermediate size of a drag reaches ConPTY, and its frames arrive late.** `WM_SIZE`
calls `io.resize` on every grid change (`window.c:4783-4787`), as does `sync_session_grid()`
(`:550-553`); a window drag, an AI-dock slide (the animation tick calls `relayout_main()` every
16 ms, `:4433`) and a splitter drag (`:5193`) each produce a run of them. At
15:55:59.091-.489 (lines 679-746) twelve width-only resizes (158 → 121 columns) went to ConPTY
24-50 ms apart; no `term.rx` line appears between .091 and .603 although paints ran, and at
15:55:59.603 (lines 763-769) seven reads delivered twelve full frames (each `\e[H` ..
`\e[36;31H`), all applied at 121 columns. The same happened at 15:56:01.406 and 15:56:02.245.
A frame rendered at a wider intermediate width wraps every row longer than the final width and
the extra rows scroll the model, so screen rows enter scrollback a second time. These rows were
short enough not to wrap; the mechanism does not depend on that. `local_pty_poll()` feeds each
read straight into `term_process()` (`local_pty.c:564-578`).

**The DPI handler resizes twice.** `WM_DPICHANGED` (`window.c:4798-4840`) calls
`SetWindowPos` with the suggested rectangle (`:4812-4816`) before it rebuilds the renderer's
fonts (`:4819-4821`). `SetWindowPos` sends `WM_SIZE` synchronously, and `WM_SIZE` fits the grid
with `g_renderer.charWidth`/`charHeight` (`:4777-4781`), still the 192 DPI font's, to the new,
smaller client area: the 48→38 shrink at 02.695. The explicit `WM_SIZE` after the rebuild
(`:4834-4838`) then grows to 85. ConPTY has no scrollback, so the transient shrink discarded
the rows that fell off its top.

**`TermRow.len` and `wrapped` cannot be trusted after a ConPTY repaint** (a pre-existing fact,
not changed here). EL never touches `len` or `wrapped` (`src/term/parser.c:437-458`); ED 0 and
ED 1 leave the cursor row's `len` (`:368-375`, `:400-414`) and the cleared rows' `wrapped`
(`:376-386`, `:388-399`); only ED 2 maintains both (`:415-427`); `wrapped` is only ever set,
on auto-wrap (`:59-72`), and a line feed onto a row (`:641-647`) leaves it. ConPTY repaints
with exactly `\e[H` + `text\e[K\r\n`, so rows keep an old, longer `len` and stale `wrapped`
flags. Changing the parser would change SSH rendering; section 1 works around it instead.

Ruled out by the log: the scroll region (reset on every resize, `buffer.c:262-263`); the
renderer skipping rows (`rows_left_dirty=0` throughout, `render.stuck_dirty` never fired);
scrollback offset (0 at onset); the AI dock and zoom (after onset). PR #66 (v1.2.34) fixed
stale rows over a correct model; here the model is wrong. The log cannot be replayed
byte-exact: `term.rx` lines are cut at 4096 bytes of line (`truncated=1`), two of the three
reads at 17:48:03.258 and six of the seven at 15:55:59.603.

---

## Design

### 0. Probe first

The top-anchored rule in section 1 is inferred from one log and conhost's design. The first
implementation slice is the console probe (Tests, "Probe") and a short written report of what
the maintainer's conhost actually does on a grow, a shrink with the cursor at the bottom, a
shrink with content below the cursor, and a width change. Section 1 is committed to code only
after that report agrees with it, or is amended as 1.8 says.

The probe runs every scenario twice: once with `CreatePseudoConsole` flags 0, as `local_pty.c`
opens today, and once with `PSEUDOCONSOLE_RESIZE_QUIRK` (0x2). The report says what the flag
changes in the post-resize bytes and in conhost's own buffer and cursor. If the flag stops the
full repaint while conhost's cursor still tracks the model, `local_pty_open()` passes it for
every local session (maintainer's decision, 2026-10-19). Section 1 stays either way: conhost's
buffer and cursor row are top-anchored whether or not it repaints, and the model has to agree
with them for the next output to land on the right row. What the flag removes is the repaint
itself, and with it section 3's two residuals.

The report is `2026-10-19-conpty-resize-probe-report.md`, filled from one Windows run of
`test_local_pty_resize_parity`.

### 1. Top-anchored reflow for ConPTY sessions

**1.1 The mode.** `term.h` gains

```c
typedef enum { TERM_ANCHOR_BOTTOM = 0, TERM_ANCHOR_TOP = 1 } TermAnchor;
```

and `Terminal` (`src/term/term.h:60-129`) gains `TermAnchor anchor`, set in `term_init()`
(`buffer.c:34-87`, which uses `xmalloc`, so it is set explicitly beside `log_id` at `:76`).
`term_reflow_buffer()` takes the mode as a parameter; `term_resize()` passes `term->anchor`
for the main screen and `TERM_ANCHOR_BOTTOM` for the alternate screen (1.6). The mode is read at
each resize; changing it never moves content.

**1.2 `TERM_ANCHOR_BOTTOM` keeps today's logic.** The bottom branch is the current body, with
only the two shared fixes of section 2. SSH sessions never set the mode. `parser.c` is not
changed.

**1.3 `TERM_ANCHOR_TOP` semantics, main screen** (what conhost does with a buffer that is its
viewport). Because `len` is untrusted (Problem), the top branch measures every source row by
scanning: its *extent* is one past the last cell whose codepoint is not 0, within
`min(len, old_cols)`; a row is *blank* when its extent is 0. Rows are reflowed by extent.

1. **One pass.** The whole buffer is reflowed at the new width in a single pass, as today
   (`:135-168`, with section 2's fixes), recording for each source row the output row it
   starts on.
2. **The boundary.** `T = old_count - old_rows` is the old screen top (`:132`). If row `T`
   continues a logical line that began in scrollback (`wrapped` set on `T`, and on the rows
   back to the line's first row), the whole logical line stays on the screen side: the
   segment starts at that line's first output row. Scrollback can end a row short of where it
   did; it never holds a row twice. `S` is the number of output rows before the segment.
3. **The segment** runs to `E`, the later of the cursor's row and the last non-blank row of
   the old screen; rows after `E` are blank and are not carried. `L` is the number of output
   rows from the segment start to the end of `E`.
4. **Fit** (`L <= new_rows`): the screen top is row `S`; the rows after the segment are fresh
   blanks. No top padding, no history pulled down.
5. **Overflow** (`L > new_rows`): `excess = L - new_rows`; the first `excess` segment rows
   become scrollback, the screen top is `S + excess`, nothing below is dropped. The cursor
   follows its row: its new row is its output row minus the screen top, clamped to 0 when
   content below it pushed its own row out (a test pins this).
6. **Capacity.** The rows kept are `S + max(L, new_rows)`, counted before any row is copied;
   when that exceeds `new_capacity` the oldest scrollback rows are dropped, never segment rows.
7. **Scroll offset** (a view scrolled back keeps its place, as `term_scroll()` does,
   `:289-295`). On a height-only change the same history row stays at the top of the view:
   `scrollback_offset` grows by `excess`, clamped to `[0, lines_count - rows]`. On a width
   change the logical line at the top of the view stays at the top, from its first row; if the
   view began mid-line it moves by up to that line's other rows.

`lines_count >= rows` (`:200-207`) holds by construction; the cursor column is clamped as today
(`:212-215`). A width change is the same algorithm with real rewrapping, which is what conhost
does (the log's 124→103→292 sequence ends with 35 rows of content at the top, consistent with
that). Stale `wrapped` flags (Problem) can still join two rows ConPTY painted as separate lines
on a width change; ConPTY's next repaint corrects the screen, and history keeps the joined line.

**1.4 The trade-off on shrink.** conhost drops the rows that leave its top; Nutshell keeps
them in scrollback. The model still equals ConPTY's next repaint, which addresses only the
viewport; what diverges is that Nutshell's scrollback holds rows ConPTY no longer has, which is
what scrollback is for. The converse rule keeps them in step: **no scrollback row returns to
the screen.** After a grow, history stays above the screen (reachable by scrolling) instead of
filling the new rows; a 1×1 round trip leaves only the cursor row on screen (the minimised case
never resizes, `window.c:4732-4735`).

**1.5 Where the mode is set.** `start_local_shell()` (`window.c:1106`), immediately after a
successful `local_pty_open_ex()` (`:1149`) where `s->io` is published (`:1170-1180`):
`s->term->anchor = TERM_ANCHOR_TOP`. Reconnect calls it again (`:2123`). A failed spawn stays
bottom; after the shell exits the mode stays top. The SSH connect path (`:4646`) never touches
it. The mode is per session, so it can later become a profile option (section 6).

**1.6 The alternate screen keeps today's handling.** It has no scrollback and full-screen
programs repaint it on resize; the alternate buffer is reflowed bottom-anchored as now
(`buffer.c:235-243` with `max_scrollback` 0). The saved primary buffer reflowed while the
alternate screen is up (`:248-257`) is the main screen and uses the session's mode.

**1.7 Reconnect keeps the old screen.** A new ConPTY opens with `\e[2J` .. `\e[H`
(`tests/test_term_conpty.c:144-152`), and ED 2 clears the screen rows in place
(`parser.c:415-427`), so today a reconnect erases what the dead shell left on screen. The
reconnect path keeps its `\r\n` (`window.c:2122`) and then calls a new
`term_push_screen_to_scrollback(term)` (`buffer.c`): `term_scroll()` once per row up to the
later of the cursor row and the last non-blank screen row, then the cursor to (0,0); then
`start_local_shell()` (`:2123`). The clear hits blank rows.

**1.8 If the probe disagrees.** Should conhost keep the cursor row and drop rows from the
*bottom* on a shrink (content below the cursor first), step 5 changes: `excess` is taken first
from the segment rows below the cursor's row, which are dropped (conhost no longer has them
and they cannot go to scrollback), and only the rest from the top. Should conhost not reflow on
a width change, the top branch copies rows truncated or padded instead of rewrapping. Either
change is local to the top branch.

### 2. Two reflow fixes for every session

Both change SSH-visible behaviour, and are general bug fixes with their own regression tests:

- **Exact width.** In `:149-162`, a row that ends exactly at `new_cols` does not start a new
  `wrapped` row: the wrap to the next output row happens when the next cell of the same
  logical line is copied, not after the last one.
- **Cursor past the content.** In `:164-167`, a cursor at `cur_col >= cell_limit` keeps its
  distance past the content: `new_cursor_col = cur_new_col + (cur_col - cell_limit)`, clamped
  to `new_cols - 1`.

With these, a sequence of resizes that ends at its starting size, without an overflow in
between, leaves a top-mode model unchanged.

### 3. Throttled shell resizes

**3.1 Throttle.** The model resizes at once (the window needs the grid); `ResizePseudoConsole`
is sent at most once every `PTY_RESIZE_INTERVAL_MS` = **100 ms**, leading and trailing edge:
a request sends at once when the last send was at least 100 ms ago, otherwise it becomes the
pending size, and the poll tick sends the pending size once 100 ms have passed. The final size
is always sent. The decision is pure, in `src/core/pty_resize_throttle.{c,h}`:

```c
#define PTY_RESIZE_INTERVAL_MS 100
typedef struct { int sent_once; unsigned last_send_ms; int pending; int cols, rows; } PtyResizeThrottle;
void prt_init(PtyResizeThrottle *t);
int  prt_request(PtyResizeThrottle *t, int cols, int rows, unsigned now_ms); /* 1 = send now */
int  prt_take(PtyResizeThrottle *t, unsigned now_ms, int *cols, int *rows);  /* 1 = send pending */
```

`Session` (`window.c:106-149`) gains a `PtyResizeThrottle`, initialised with `prt_init()` in
`create_session()` (`:610-656`, which uses `xmalloc` and sets each field by hand). For a
`SESSION_LOCAL` session the two `io.resize` calls (`:552`, `:4786-4787`) go through
`prt_request(..., GetTickCount())`; SSH sessions keep the immediate call. The 10 ms poll timer
(`SetTimer(hwnd, 1, 10)`, `:3880`, handled at `:4161`) calls `prt_take()` for each local
session before polling it. `local_pty_resize()` already skips the size ConPTY has
(`local_pty.c:646`). On the 15:55:59 drag, with poll ticks on every 10 ms, it sends 157 at
.091, 144 at .200, 132 at .300, 126 at .400 and 121 at .500: five resizes instead of twelve.

**3.2 Bounded staleness.** During a drag, ConPTY's size lags the model's by at most 100 ms
plus one poll tick, and output the shell writes meanwhile keeps arriving (a debounce would
have held ConPTY at the old size for the whole drag). What arrives is rendered for ConPTY's
size and applied at the model's: rows wider than the grid auto-wrap, a line feed on the last
row scrolls, absolute moves are clamped (`clamp_cursor`). Every ConPTY resize is followed by a
full repaint from `\e[H`, so the screen converges; what can remain is rows a stale frame
scrolled into scrollback (duplicated history). A shrink that overflows and a grow back within
one interval reach ConPTY as nothing (same size, no call, no repaint) while the model has moved
rows into scrollback; the screen then disagrees until the next full repaint. Both need a resize
during output or a sub-100 ms shrink-and-back; they are accepted rather than carrying state to
undo them.

**3.3 The starved-timer hypothesis.** No `term.rx` was logged between 15:55:59.091 and .603
while paints ran, which suggests the poll timer's `WM_TIMER` did not run inside the modal
size loop (it is a low-priority message) and the reads piled up. If so, the trailing edge
also waits until the loop ends. The implementation must check this before relying on it: log
the throttle's sends and the poll tick count during a drag (a `shell.resize` line carries its
time; add a Trace `poll.tick` count per paint if needed). If the timer is starved, the trailing
edge also runs from `WM_SIZE` itself (send the pending size there once 100 ms have passed).

### 4. DPI change: renderer before `SetWindowPos`

Churn reduction, not the fix: it removes a model reflow, a wrong-grid paint and a ConPTY
resize. `WM_DPICHANGED` (`window.c:4798`) becomes:

1. `g_dpi`, `g_tab_height`, `g_left_margin` (`:4799-4803`) and the AI panel width rescale
   (`:4805-4809`): unchanged.
2. **Moved up** from `:4818-4832`: `renderer_free`/`renderer_init` at `newDpi`,
   `apply_config_colors`, `renderer_apply_theme`, `ns_font_flush`, `g_hMenuFont`,
   `tabs_set_font`, `ai_chat_refresh_fonts`.
3. `SetWindowPos` (`:4812-4816`), tagged `why=dpi` (section 5); its synchronous `WM_SIZE` fits
   the final grid.
4. The explicit `WM_SIZE` (`:4834-4838`) stays, also `dpi`: after step 3 the grid is unchanged
   (`:4782`) and it only re-lays out the children; it is the only resize when the suggested
   rectangle equals the current size and the font alone changes the grid.

Today the only stale read between the move and the rebuild is the char size in `WM_SIZE`
(`:4777-4781`); the AI panel's and tab strip's own `WM_SIZE` (`:4747-4760`) now see the new
fonts too. `renderer_init()` sizes the font from its `dpi` argument (`renderer.c:21`) and
measures on the screen DC (`:55-66`); no message is pumped between steps 2 and 3. A 192→96 move
becomes one 48×124 → 87×292 resize. The reverse move (96→192) today grows to a transient grid
of roughly twice the final rows (the old small font in the doubled client area) and then
shrinks; under top mode the transient grow adds blank rows that the shrink removes, so it lost
nothing even before the reorder, and with it the transient is gone. The 85→87 resize at
17:48:03.059 (no `grid.sync`, so `WM_SIZE`) is unexplained; one guess, unverified, is the
window's menu bar (`SetMenu`, `:1709`) changing its non-client height after the DPI change.
`why=` will name the source.

### 5. Logging

`src/core/dlog_types.c` holds only the category and level names (`:6`, `:10`); there is no
event catalogue in code. The debug-logging spec's section 2 tables cover `render` events only
and name `resize` among the `term` sites in prose (`2026-10-17-debug-logging-design.md:150-151`);
the implementation adds a short `term` table there with `term.resize` and its fields.

**`term.resize`** (Debug, `buffer.c:265-268`) appends `cursor_before=r,c cursor=r,c
anchor=top|bottom why=…` after its current fields, so the existing order is unchanged; rows are
0-based like `issue.term`'s `cursor=`. `why` values: `size` (a `WM_SIZE` from the system),
`dpi`, `zoom` (`apply_zoom`, `:2538`), `ai_dock` (`relayout_main`, `:1789-1794`, whose four
callers are the AI dock's open, mode switch, slide tick and splitter drag), `settings`
(`:1766`), `sync` (`sync_session_grid`, `:526-554`), `other` (default). `term_resize(t, r, c)`
becomes a wrapper for `term_resize_why(t, r, c, "other")`, so the tests' ~45 calls are
unchanged. `window.c` sets a file-static `const char *g_size_why` (saved and restored) around
each synthetic `SendMessage(WM_SIZE)` and the `WM_DPICHANGED` `SetWindowPos`; `WM_SIZE`
passes `g_size_why ? g_size_why : "size"`; `sync_session_grid()` passes `"sync"`.

**`shell.resize`** (`local_pty.c:657`) is unchanged; its count against `term.resize` shows the
throttle at work.

**Snapshot**: `DlogSnapTerm` (`src/core/dlog_snapshot.h:33-49`) gains `const char *anchor`,
formatted through the NULL-safe `val()` (`dlog_snapshot.c:45-48`, which prints `-` for NULL)
as `anchor=…` at the end of `issue.term` (`:73-84`), filled in `window.c:3183-3200`.

### 6. Out of scope

- **SSH, including SSH to Windows.** Only section 2's two fixes reach SSH sessions. A session
  to Win32-OpenSSH runs the remote shell under a server-side ConPTY and shows the same repaint
  pattern; the mode is per session and can later be a profile option. In the notes' Open list.
- **The parser.** `len` and `wrapped` upkeep in EL, ED and line feed is a pre-existing quirk,
  left alone because fixing it changes SSH rendering; one line in the notes' Open list.
- **The bottom branch's capacity truncation** (`buffer.c:144`, `:160` drop the newest rows,
  the cursor's among them, when a narrowing reflow overflows a full ring).
- **The scrolled-back repaint storm** (`issue3.log` 11761-12797) and **wide characters**: in
  the notes' Open list.
- Any `CreatePseudoConsole` flag other than `PSEUDOCONSOLE_RESIZE_QUIRK` (section 0), and
  paint-path changes.

---

## Implementation order

1. **Probe and report** (section 0): `--console-probe`, the parity case, the fixture
   recorder; a conhost-behaviour report in the PR. Section 1 is amended if needed (1.8).
2. **Reflow fixes** (section 2) with their tests, in both modes.
3. **Top mode** (section 1) and the reconnect push, with the native tests and fixtures.
4. **Throttle** (section 3), including the starved-timer check.
5. **DPI reorder and logging** (sections 4, 5).
6. **Integration case**, then the manual DPI check; version bump and exe last.

---

## Tests

Native, `make test`; new files `tests/test_term_anchor.c`, `tests/test_reflow_fixes.c` and
`tests/test_pty_resize_throttle.c`, every function declared and called in `tests/runner.c`.
"Screen text" is the rows read through `term_screen_to_phys()` (`buffer.c:319-323`); "history"
is `term_extract_last_n()` (`src/core/term_extract.h:25`) over every logical line. Native
tests call `term_resize()` directly: no throttle is involved unless a test says so.

**Reflow fixes**, each in bottom and top mode: a 20-character line on a 20-column terminal
resized to 20 columns wide and 10 rows taller gains no row and no `wrapped` flag; the same line
narrowed to 10 and widened back to 20 is one row again; a prompt `PS C:\>` (7 cells) with the
cursor at column 8 keeps column 8 across a height change and is clamped to `new_cols - 1` when
narrowed below it. Negative: a 21-character line on 20 columns still wraps to two rows.

**Top mode** (`test_term_anchor.c`):

- **Defaults**: `term_init()` gives bottom mode.
- **Grow, cursor at the bottom**: 10×20, 30 numbered lines, cursor row 9; resize to 20×20: row
  0 unchanged, cursor row 9, rows 10-19 blank, `lines_count - rows` unchanged.
- **Grow, cursor mid-screen**: 24 rows, 5 lines, cursor row 5; grow to 40: cursor row 5, no
  pull-down, no top padding.
- **Shrink, cursor at the bottom**: 24 full rows, cursor 23; shrink to 14: scrollback +10, row
  0 is old row 10, cursor row 13.
- **Shrink, blank below the cursor**: cursor row 5 of 24; shrink to 14: nothing moves.
- **Shrink, content below the cursor**: cursor row 5, text on row 20; shrink to 14: `excess` 7,
  row 0 is old row 7, cursor clamped to row 0 (pinned; the probe report decides 1.8).
- **Stale `len`**: a row written 60 wide, then `\r` + `ab\e[K` (EL leaves `len` 60), has extent
  2: a narrowing to 30 columns gives it one row, not two; a row erased to all zero cells is
  blank.
- **Boundary**: a 30-character line on 20 columns whose second row is screen row 0; after a
  narrowing to 10 columns the whole line is on the screen side, scrollback ends before it, and
  history holds it once.
- **Width**: a 30-character line on 20 columns wraps to two rows at the top and unwraps on 40.
- **Capacity**: a narrowing that overflows `max_scrollback` drops the oldest scrollback rows
  and keeps every screen row and the cursor.
- **Scroll offset**: scrolled back 12, a height-only shrink with `excess` 10 leaves offset 22
  and the same top visible row; a width change keeps the top visible logical line's first row
  at the top.
- **Round trip**: 24 rows with the cursor on row 5, grow to 40 and back to 24: the model equals
  the original (screen, cursor, `lines_count`, flags).
- **Alternate screen**: a resize with `?1049h` active gives the same alternate buffer as a
  bottom-mode terminal; on `?1049l` the primary is top-anchored at the new size.
- **Reconnect**: `\r\n`, `term_push_screen_to_scrollback()`, then the recorded ConPTY opening
  (`test_term_conpty.c:152`) leave the old screen's text in history once and a blank screen.
- **Bottom-mode witness**: the log's sequence by direct `term_resize()` calls (48×124 with 120
  numbered lines and a prompt; 38×103; 87×292) leaves the cursor on row 86 in bottom mode and
  on row 37 in top mode, before any repaint. Pins today's bottom behaviour.
- **Logging** (fake dlog, `term` at Debug): `term.resize` carries `cursor_before=`, `cursor=`,
  `anchor=top`, `why=other` from `term_resize()` and `why=dpi` from `term_resize_why()`;
  `issue.term` ends `anchor=top`, and `anchor=-` for NULL (`tests/test_dlog_snapshot.c:148`
  updated for the field).

**Throttle** (`test_pty_resize_throttle.c`): the twelve request times of the 15:55:59 drag
(.091, .135, .171, .202, .234, .284, .323, .354, .378, .416, .457, .489 with 157 .. 121
columns) and `prt_take()` every 10 ms from .090 give exactly five sends: 157 at .091 (leading),
144 at .200, 132 at .300, 126 at .400, 121 at .500 (trailing); the last send is always the last
request; a lone request sends at once; no take without a pending size; `prt_init()` state
sends the first request at once; `now_ms` across the 2^32 wrap.

**Probe** (`make wintest`, Windows, real conhost; `tests/test_local_pty.c`, `Makefile:142`,
registered in `tests/win_runner.c`). A child mode `--console-probe <scenario> <dumpfile>`
beside `--dump-keys` (`win_runner.c:68`, `:159`), run as the pseudo-console's own process the
way `test_local_pty_records_key_mapping()` runs `--dump-keys` (`test_local_pty.c:410-430`). It
writes the scenario to its console (numbered lines, lines longer than the width so they wrap,
content left below the cursor with a cursor-up), then reads keys: on `D` it writes every
buffer row and the cursor from `GetConsoleScreenBufferInfo` and `ReadConsoleOutputCharacterW`
to the dump file, on `Q` it exits. Nothing is typed as a command. `local_pty_open_ex()`
(`local_pty.h:39`) gains a `DWORD pcon_flags` argument that it passes to `CreatePseudoConsole`
(`local_pty.c:395`, 0 today) so the case can open the second run with
`PSEUDOCONSOLE_RESIZE_QUIRK`; `local_pty_open()` keeps its signature and passes 0 until the
report says otherwise (section 0). The parity case: open, drain
the scenario into a top-mode terminal, then for each step (grow; shrink with the cursor at the
bottom; shrink with content below the cursor; a width change) call `term_resize()` and
`local_pty_resize()`, snapshot the model **before** draining the post-resize bytes, assert that
scrollback plus screen hold each scenario line exactly once, send `D`, and compare the dump's
viewport and cursor with the snapshot row by row. Then drain and keep the raw repaint bytes:
whether a soft-wrapped row is sent without `\r\n` answers the soft-wrap question from them.
Trailing spaces cannot be answered here (`ReadConsoleOutputCharacterW` returns a space for a
written space and for an empty cell alike), so that question stays open. The report of slice 1
is this case's output, `print_escaped()` included.

**Fixtures** (`tests/fixtures/conpty_resize_*.bin`, replayed like `test_term_conpty.c:57-147`;
recorded by the probe case on the maintainer's box because the log's lines are truncated). Each
recording interleaves the bytes with in-band markers (`\e]9999;resize=COLSxROWS\a`, an OSC the
parser ignores) at the point each `ResizePseudoConsole` was made, so the replay knows what the
model's size was. The scenario uses lines longer than the final width. `..._burst.bin`: twelve
resizes 30 ms apart from 158 to 121 columns, drained only at the end, as the 15:55:59 drag was;
replayed with every marker applied as it occurred to a top-mode terminal, it must show
duplicated rows in history (the hazard, asserted). `..._throttled.bin`: the same drag through
the throttle; the replay must show none.

**Integration, gate tier** (`tests/integration/cases/80-local-shell.ps1`):
`local_resize_keeps_history`, with `debug_enabled` and `debug_level_term = "debug"` as
`debug_terminal_log_written` does (`60-tabs-logging.ps1:243`). A local `cmd` tab prints 120
numbered lines and then `KEEP_MARKER`. Five direct `SetWindowPos` calls about 20 ms apart shrink
the window step by step; the log must then show five `term.resize` lines and at most two
`shell.resize` lines (the leading and the trailing edge), the last with the final size. The
window then grows back the same way. The harness has no text query, and Edit > Select All
covers screen rows only (`window.c:3982-3989`), so the case scrolls back first with a posted
`WM_MOUSEWHEEL` (new helper, posted like the others; the handler is `window.c:5098`), then Edit
> Select All and Edit > Copy (`Send-NutshellCommand` 2012 and 2010, `resource.h:21-23`) and
`Get-Clipboard`: numbered lines from before the resize are present, each once; at the bottom,
`KEEP_MARKER` and the prompt are on screen. Whether the copy reads the scrolled view is checked
first (the scrolled copy must differ from the bottom one). The log checks stay: every
`term.resize` has `anchor=top`, the grow's `lines - rows` is not below its `lines_before -
rows_before`, and the shrink's is above it by the rows that left the screen.

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
| term | `src/term/term.h:60-133` | `TermAnchor`, `anchor`, `term_resize_why()`, `term_push_screen_to_scrollback()` |
| term | `src/term/buffer.c:34-87`, `:110-310` | init; section 2 fixes; top branch (extent scan, boundary, overflow, capacity, offset); `term.resize` fields; push-to-scrollback |
| core | `src/core/pty_resize_throttle.{c,h}` | new: the throttle decision |
| core | `src/core/dlog_snapshot.{c,h}` | `anchor` in `issue.term` |
| ui | `src/ui/window.c:106-149`, `:526-554`, `:610-656`, `:1170-1180`, `:1766`, `:1789-1794`, `:2122-2123`, `:2538`, `:3183-3200`, `:4161`, `:4723-4840` | throttle state, init and take; `g_size_why`; top mode; reconnect push; DPI reorder; snapshot |
| ui | `src/ui/resource.h` | `APP_VERSION`/`APP_VERSION_BINARY` bump |
| build | `README.md` `**Version**:` line, `build/win/nutshell.exe` | version and rebuilt exe for the gate |
| tests | `tests/test_term_anchor.c`, `tests/test_reflow_fixes.c`, `tests/test_pty_resize_throttle.c`, `tests/runner.c`, `tests/test_dlog_snapshot.c`, `tests/test_term_conpty.c`, `tests/fixtures/conpty_resize_*.bin` | new and updated |
| tests | `tests/test_local_pty.c`, `tests/win_runner.c` | `--console-probe`, parity case, fixture recorder |
| tests | `tests/integration/NutshellIT.psm1`, `tests/integration/cases/80-local-shell.ps1` | wheel helper; `local_resize_keeps_history` |
| docs | `docs/superpowers/specs/2026-10-17-debug-logging-design.md` section 2 | a `term` table with `term.resize` |

`src/term/parser.c` is not in the list: it is not changed.

---

## Open for the maintainer

Two critique rounds are the ceiling; these remain and go to the draft PR as they stand.

1. **conhost's resize behaviour on your build** (grow keeps the top line; shrink drops from the
   top only as far as the cursor needs; a width change rewraps; the buffer is the viewport). The
   spec is written for that; the probe report (slice 1) confirms or triggers 1.8.
2. **Content below the cursor on a shrink** (PSReadLine's list-view predictions): the spec
   scrolls out from the top and clamps the cursor to row 0 if its own row goes; conhost may
   drop from the bottom instead (1.8). Answered by the probe.
3. **Soft wraps in ConPTY's repaint**: whether a soft-wrapped row is sent without `\r\n`.
   Answered by the probe's raw repaint bytes.
4. **Trailing spaces**: whether conhost treats a row of written spaces as blank. The probe
   cannot tell (`ReadConsoleOutputCharacterW` returns spaces for both); the extent scan treats
   written spaces as content.
5. **`PSEUDOCONSOLE_RESIZE_QUIRK` (0x2)**: believed to make conhost skip its full repaint on
   resize for a terminal that reflows itself; not confirmed, not proposed.
6. **The starved poll timer during a drag** (3.3): to be measured in slice 4; if starved, the
   trailing edge also runs from `WM_SIZE`.
7. **The 100 ms throttle interval**, chosen from one log's drag cadence; and acceptance of the
   residuals in 3.2 (duplicated rows from output during a resize; a sub-100 ms shrink-and-back).
8. **The 85→87 resize at 17:48:03.059**: unexplained; menu bar non-client height is a guess;
   `why=` will name it.
9. **`WM_SIZE` when `SetWindowPos` does not change the size**: assumed none is sent (hence
   step 4 of section 4 stays); unverified.
10. **The ConPTY opening clear** (`\e[2J` .. `\e[H`): recorded on one build; whether every
    supported build sends it, or ever `\e[3J`, is unverified. 1.7 is harmless either way.
11. **Section 2 changes SSH sessions** (no blank row at exact width, cursor kept past the
    content): bug fixes, but visible; confirm they belong in this change.

### Maintainer decisions (2026-10-19)

- Item 11: section 2 belongs in this change.
- Item 7: the 100 ms interval and the two residuals of 3.2 are accepted.
- Item 5: proceed with `PSEUDOCONSOLE_RESIZE_QUIRK`: the probe tries it (section 0) and the
  product adopts it if the report says it stops the repaint without losing the cursor.
- Items 1-4 and 8-10 are answered by the probe and by the implementation's own logging.

---

## Critique (round 1, 2026-10-18)

One Opus critique, thirteen findings; dispositions by the maintainer's session. Items 1, 2 and 3
were revised again in round 2.

1. Stale repaints for intermediate sizes applied at a later size. **Accepted**: Problem
   paragraph with the 15:55:59 evidence; a settle step for shell resizes; the residual race
   stated. (Round 2: the debounce became a throttle and `anchor_held` was dropped.)
2. `len` not shrunk by EL/ED 0/ED 1, `wrapped` stale. **Accepted**: the primitives were fixed
   in the parser. (Round 2: reverted; the top branch scans cells instead.)
3. Circular native parity test, weak wintest. **Accepted**: recorded fixtures and a console
   dump. (Round 2: the probe runs as the pty's own process and the model is snapshotted before
   the repaint.)
4. Alternate-screen contradiction. **Accepted**: top mode is main screen only (1.1, 1.6).
5. Overflow put the cursor at row 0 when content lay below it. **Accepted**: overflow is
   defined by `L` over the later of the cursor and the last non-blank row; the cursor follows
   its row; the test pins it; the probe compares with conhost.
6. SSH to a Windows host. **Accepted as scope**: section 6; notes' Open list.
7. The `cursor_jump` detector. **Accepted the cheaper option**: dropped; `term.resize` keeps
   `cursor_before`, `cursor`, `anchor`, `why`; `issue.term` keeps `anchor`.
8. Reconnect erases the screen. **Accepted**: 1.7.
9. Forced hard break at the screen top. **Accepted**: removed. (Round 2: the straddling line
   stays whole on the screen side.)
10. `scrollback_offset` on a top-mode shrink. **Accepted**: 1.3 step 7.
11. DPI reorder is churn, not correctness. **Accepted**: section 4.
12. Wide characters. **Rejected for this spec**: pre-existing; notes' Open list.
13. Smaller gaps. **Accepted**: Files gains `resource.h`, README's version line and the exe;
    the integration case asserts history; the capacity check counts the rows kept.

## Critique (round 2, 2026-10-18)

Second and final Opus critique, thirteen findings; dispositions by the orchestrating session.

1. The parser fixes change SSH behaviour. **Accepted, cheaper option**: section 3 of round 1
   is gone and `parser.c` is untouched; the top branch decides extent and blankness by scanning
   cells (1.3), bottom mode keeps its `len` logic; the quirk is a line in the notes' Open list.
2. The reflow is not an identity. **Accepted**: section 2 fixes the exact-width blank row
   (`buffer.c:157-161` with `:140-143`) and the cursor pulled back to `len` (`:164-167`) for
   every session, says they are SSH-visible, and pins both at exact width in both modes; a round
   trip now leaves the model unchanged.
3. The probe was circular. **Accepted**: the model is snapshotted before the post-resize bytes
   are drained and must hold each line once; the probe is the pty's own process, driven by
   keys; soft wraps come from the raw repaint bytes; trailing spaces stay open because
   `ReadConsoleOutputCharacterW` cannot tell them apart.
4. Probe first. **Accepted**: section 0 and "Implementation order" make the probe and its report
   slice 1; 1.8 states what changes if conhost drops from the bottom.
5. The fixtures could not fail. **Accepted**: recorded with lines longer than the final width
   and in-band resize markers; the burst must duplicate rows and the throttled recording must
   not.
6. Settle state uninitialised. **Accepted**: `prt_init()` in `create_session()`
   (`window.c:610-656`), in the Files table.
7. Debounce versus output during a drag. **Accepted**: a 100 ms leading-and-trailing-edge
   throttle with the final size always sent (3.1), bounded staleness (3.2), and the
   starved-`WM_TIMER` hypothesis as a check for the implementation (3.3).
8. The boundary at `T - H`. **Accepted**: one pass over the whole buffer; a line straddling the
   old screen top stays whole on the screen side; `S` and `L` are defined after the pass; a test
   pins it.
9. Covered by 1: the parser is untouched, so its changes no longer need review.
10. Integration case. **Accepted**: direct `SetWindowPos` calls about 20 ms apart; the assertion
    is at most two `shell.resize` lines (leading and trailing edge), not one, because the
    throttle sends the first request at once; history is read after scrolling back, since
    Select All covers screen rows only (`window.c:3982-3989`); the log checks stay.
11. Witness and episode tests did not say where the throttle fires. **Accepted**: native tests
    call `term_resize()` directly with no throttle; the throttle test lists the five exact send
    times for the 15:55:59 drag; the episode test went with `anchor_held`.
12. Scroll offset on a width change. **Accepted**: same top visible row guaranteed for
    height-only changes; width changes keep the top logical line, with the mid-line residual
    stated (1.3 step 7).
13. Smaller contradictions. **Accepted**: the 85→87 note names the menu-bar guess as
    unverified; push-to-scrollback goes to the later of the cursor row and the last non-blank
    row with the `\r\n` kept; history is `term_extract_last_n()`; the debug-logging spec's
    section 2 wording is corrected (no `term` table exists there yet); the same-size check is
    `local_pty.c:646` (re-verified; not `:658`, which is the `shell.resize` log line); the
    snapshot field goes through `val()`.

Cheaper option. **Accepted**: `anchor_held` is dropped; with section 2 and the throttle's
trailing edge, the rare residual is duplicated rows, accepted in 3.2.
