# Terminal selection and copy, PuTTY style

**Date**: 2026-10-20 (revised the same day after critique round 1)
**Status**: DRAFT, for the maintainer's review
**Branch**: `feat/putty-selection`
**Decided by the maintainer**: "Have it behave like PuTTY"; include double-click
word and triple-click line selection; not Shift+click extend or Alt+drag blocks.
**Proposed after critique, awaiting the maintainer's word** (section 6):
Ctrl+C always interrupts; the scrollback-size setting is a separate change.

## 0. The problem

The maintainer's report (2026-10-18 to 20): "I can't properly copy and paste
from the terminal"; "when I select and drag down in the nutshell terminal, it
only copies that which is on the window, not that which has been selected in
the buffer."

A survey of `main` at fbfe5cd (v1.2.43) found the cause and further defects,
all present since the original selection commit e5b3aaf (2026-03-08);
nothing regressed in v1.2.36.

| # | Defect | Where |
|---|---|---|
| 1 | The selection is stored in screen rows ("0-based from top of visible area"), so a drag cannot reach past the window: the row is clamped to `rows - 1` and nothing scrolls the view to follow the pointer. | `src/core/selection.h:10`, `selection.c:12-13`; no auto-scroll anywhere in `window.c` |
| 2 | The extractor maps screen row to buffer row as `lines_count - rows + row` and never reads `scrollback_offset`; the renderer does. Scrolled back, the highlight shows history and the clipboard gets the live rows at the same screen positions. | `selection.c:50-56`; `renderer.c:94-117` |
| 3 | Wheel, scrollbar and PgUp/PgDn change the offset and leave the selection alone, so the highlight stays on screen cells whose content changed. | `window.c:5116-5139`, `:5284`, `:3437` |
| 4 | Mouse-up sets `valid = true` even when start equals end, so a plain click on a character copies that character over the clipboard. | `window.c:5253` |
| 5 | Mouse-up copies and then sets `valid = false`, so Ctrl+Shift+C and Edit > Copy find no selection after a drag and do nothing. | `window.c:5263`, `:3397-3402`, `:2731-2751` |
| 6 | The mouse handlers read `LOWORD`/`HIWORD`; a negative coordinate while captured wraps to about 65535, so a drag above the window selects the bottom row and a drag left of it the last column. | `window.c:5187`, `:5223`, `:5247` |
| 7 | Both copy paths use an 8 KB stack buffer; longer text is cut silently. | `window.c:2739`, `:5255`; `selection.c:88,95` |
| 8 | `TermRow.wrapped` is ignored: a soft-wrapped line copies as several lines. Line endings are LF. | `selection.c:94-96` |
| 9 | One global `g_selection` is shared by every tab. | `window.c:602` |
| 10 | No `WM_CAPTURECHANGED` handling: a modal dialog or Alt+Tab mid-drag loses the button-up. | `window.c:5218` is the only guard |
| 11 | `term_resize` never clamps `scrollback_offset`; after a shrinking reflow it can exceed `lines_count - rows`. | `buffer.c:256-300` |
| 12 | Settings offers "scrollback lines" (default 10,000, clamped 100..50,000, saved and loaded) but nothing reads it: every session is created with 3,000 lines of history. | `config/loader.c:452-487`, `settings.c:1262,1937`; `window.c:612`. Not fixed here, see section 6 |

Paste (`do_paste`, `window.c:2332-2459`) is sound: one function behind all
five paste paths, bracketed paste, control filtering, CR for local shells,
unbounded read. It is not changed here.

Existing tests: 19 native tests in `tests/test_selection.c` on the extractor;
none covers scrollback, wrapped rows, the size limit or out-of-window
coordinates. The harness (`tests/integration/cases/10-terminal.ps1`) has paste
cases and `ctrl_c_without_selection_interrupts`; no case drags in the
terminal or reads the clipboard back.

## 1. Behaviour (PuTTY's, as far as this change goes)

- **Drag selects and copies.** Left button down starts a selection at the
  cell under the pointer; moving extends it; release copies it to the
  clipboard if it is non-empty, and the highlight stays. A press and release
  without movement, or with movement under the system drag threshold
  (`SM_CXDRAG`/`SM_CYDRAG`, 4 px by default), clears any selection and
  copies nothing: a click to focus the window never touches the clipboard.
- **Dragging past an edge scrolls.** While the button is held and the last
  reported pointer position is above the top edge or below the bottom edge
  of the terminal area, every tick of the window's existing 10 ms timer
  scrolls the view towards the pointer by one line plus one per full
  character height of distance beyond the edge, and the selection end
  follows. The stored last position is used, never the live cursor (the
  harness parks the real cursor in a corner). In a maximized window the
  pointer can get only a few rows past either edge, so a long history takes
  minutes by this route; the wheel during a drag scrolls as usual, the end
  follows the pointer on the next move, and that is the fast way back. The
  view never scrolls past the oldest or the newest line.
- **Double-click selects a word, triple-click a line.** A word is a maximal
  run of cells in one class: blank (space, empty cell, control codepoint);
  "word" (letters, digits, `_`, `-`, `.`, `/`, `~`, `:`, `@`, so that paths,
  hostnames and addresses select whole); and every other codepoint on its
  own (a CJK character therefore selects alone; the emulator has no
  wide-cell model, one codepoint per cell). A line is the whole logical
  line: back to the first row that is not a continuation, forward over every
  following row whose `wrapped` flag is set. Dragging after a double or
  triple click extends by whole words or whole lines in either direction.
  The clicks count by `WM_LBUTTONDBLCLK` (the window class gains
  `CS_DBLCLKS`) and a third press within `GetDoubleClickTime()` and
  `SM_CXDOUBLECLK`/`SM_CYDOUBLECLK` of the double-click is the triple.
- **The selection persists** until: the next left press in the terminal
  area; a write, erase or region scroll that touches a selected line; a
  resize; a switch to or from the alternate screen; the session closing; or
  every selected line being recycled out of history. New output that only
  scrolls the selected text up, scrolling the view, and typing that writes
  elsewhere do not clear it: it stays on the same text and scrolls with it,
  off the screen if need be. It is per session, so switching tabs shows each
  tab's own selection. (The AI panel's selection deliberately does not copy
  on release, `chat_listview.c:3975`; the terminal follows PuTTY, the panel
  follows an editor, and both are documented on the cheat sheet.)
- **Copy again from the keyboard and the menu.** Ctrl+Shift+C and Edit >
  Copy copy the current selection. **Ctrl+C always sends the interrupt**, as
  in PuTTY (section 6, proposed): with the selection persisting through
  typing, a Ctrl+C that copied instead of interrupting would leave `ping`
  running after the user had copied the hostname. Edit > Select All selects
  from the oldest line held to the last non-empty line and copies it.
- **The text.** Built on the heap at its real size (no limit). A soft-wrapped
  line is one line in the clipboard; trailing blanks are trimmed per logical
  line after joining, never per row (a wrap boundary keeps its space);
  trailing empty rows are dropped (CLAUDE.md, "Terminal Buffer"); lines end
  in CRLF (PuTTY on Windows); the last line has no ending. Cells are emitted
  as UTF-8 through the existing `cell_text_codepoint()` filter.
- **Right-click pastes**, unchanged. Paste is otherwise untouched.

## 2. Design

### 2.1 One view mapping in the terminal module

`src/term/term.h` gains

```c
/* Logical index of the row shown at screen_row in the current view,
 * scrollback_offset included; -1 when the view has no row there. The one
 * mapping, used by the renderer, the selection and the parser's screen
 * helpers. */
int term_view_to_logical(const Terminal *t, int screen_row);
```

`renderer.c`'s `get_visible_row()` and the selection code both call it.
`term_resize()` clamps `scrollback_offset` to `max(0, lines_count - rows)`
after reflow (defect 11).

### 2.2 Line numbers and epochs

The history is a ring; once full, `term_scroll()` recycles the oldest row by
moving `lines_start` (`buffer.c:309-313`). ED 3 is not implemented and no
other path drops rows from the front; reflow rebuilds and renumbers
everything, and the alternate screen is a separate ring (`buffer.c:505-513`)
whose LF at the bottom also reaches the recycle branch. The terminal gains

```c
uint64_t lines_recycled;   /* rows dropped from the front of this ring, ever */
uint32_t epoch;            /* bumped by reflow and by alt-screen enter/leave */
```

both per ring (the `primary_*` save set gains `primary_lines_recycled`;
`epoch` is one counter, bumped on every swap). The **absolute line number**
of logical index `i` is `lines_recycled + i`, stable while the epoch is.

### 2.3 Writes that touch the selection

The selection must not survive its text changing (`apt`'s scroll region,
a progress bar's `\r`, ED). The terminal keeps a watched range the window
sets while a selection exists:

```c
uint64_t sel_watch_lo, sel_watch_hi;  /* absolute lines, inclusive; hi < lo = none */
bool     sel_touched;                 /* set by any write into the range */
```

Every cell write, erase (EL, ED, ECH), region scroll (`term_scroll_region`
and its callers: DECSTBM scrolling, IL/DL, SU/SD) and `term_scroll` into a
watched line sets `sel_touched`; the check is one compare of the written
row's absolute number against the range, on the write path only when a
range is set. After each poll the window clears the selection if
`sel_touched` is set or the epoch changed, and `term_resize`/alt-screen
switches are covered by the epoch rather than by hooks at their call sites
(`sync_session_grid` on tab switch, `WM_SIZE`, and the nine `term_process`
call sites need no edit). A clear during an active drag also ends the drag:
`active = false`, release capture, stop auto-scroll.

### 2.4 The `wrapped` flag

Its meaning is right (set on the continuation row by auto-wrap,
`parser.c:69-71`; reflow agrees) but it is only ever reset by
`term_row_fill` and ED 2, so after a wrapped prompt line is erased with
Ctrl+U the next command's first output row still says `wrapped`. The parser
clears `wrapped` on the row that LF, IND, NEL, CUP, CUU/CUD, RI and
`term_scroll` land the cursor on; only auto-wrap sets it. Native tests in
`tests/test_term.c` pin both.

### 2.5 The selection

```c
typedef struct { uint64_t line; int col; } SelPoint;
typedef enum { SEL_STREAM = 0, SEL_WORD = 1, SEL_LINE = 2 } SelMode;

typedef struct {
    SelPoint anchor, anchor_end; /* the unit under the press (a cell for STREAM) */
    SelPoint start, end;         /* normalised, both inclusive */
    SelMode  mode;
    uint32_t epoch;              /* the terminal's epoch when begun */
    bool     active;             /* button held */
    bool     valid;              /* a non-empty selection exists */
    int      press_x, press_y;   /* client pixels of the press, for the drag threshold */
    int      last_x, last_y;     /* last reported pointer, for auto-scroll */
} Selection;
```

One `Selection` per `Session`, explicitly zeroed in `create_session`
(`Session` comes from `xmalloc`, which does not zero), replacing the global.
Columns are inclusive cell indexes; a point past a row's content is legal
(the extractor trims).

### 2.6 The portable API, `src/core/selection.c`

Pure, takes `const Terminal *`, tested natively; `window.c` keeps the
message handlers, the capture and the timer tick.

```c
/* Pixel to cell, signed, clamped to 0..rows-1 / 0..cols-1. */
void selection_pixel_to_cell(int px, int py, int char_w, int char_h,
                             int y_offset, int rows, int cols,
                             int *out_row, int *out_col);
/* Lines to scroll this tick for a pointer at py: negative = up, 0 inside
 * [top_px, bottom_px), magnitude 1 + (distance beyond the edge) / char_h. */
int  selection_autoscroll_step(int py, int top_px, int bottom_px, int char_h);

/* Button down with click_count 1, 2 or 3 at (screen_row, col): STREAM at
 * the cell; WORD or LINE covering the unit under it (valid at once). */
void selection_begin(Selection *s, const Terminal *t, int screen_row, int col,
                     int click_count, int px, int py);
/* Pointer moved while active. STREAM: end = the cell, valid once the pointer
 * is past the drag threshold from the press. WORD/LINE: extend by units from
 * the anchor unit in the pointer's direction. */
void selection_extend(Selection *s, const Terminal *t, int screen_row, int col,
                      int px, int py, int drag_cx, int drag_cy);
/* Button up: active = false. Returns valid. */
bool selection_finish(Selection *s);
void selection_clear(Selection *s);
/* Oldest line held, col 0 .. last non-empty line, its last cell. */
void selection_select_all(Selection *s, const Terminal *t);
/* After output: epoch mismatch or sel_touched clears; otherwise drop any of
 * the four points below lines_recycled (anchor included) and clear when
 * nothing is left. Returns true when something changed. */
bool selection_sync(Selection *s, const Terminal *t);
/* The watched range for the terminal (section 2.3); hi < lo when invalid. */
void selection_watch_range(const Selection *s, uint64_t *lo, uint64_t *hi);

bool selection_contains(const Selection *s, const Terminal *t, int screen_row, int col);
int  selection_char_class(uint32_t cp);  /* 0 blank, 1 word, 2+cp otherwise */
/* The text: heap, NUL-terminated, *out_len bytes, caller frees; NULL only on
 * allocation failure. Joins wrapped rows, trims per logical line, drops
 * trailing empty rows, CRLF. */
char *selection_copy_text(const Selection *s, const Terminal *t, size_t *out_len);
```

`selection_normalise()` and `selection_extract_text()` go.

### 2.7 The window, `src/ui/window.c`

- Window class: add `CS_DBLCLKS`. `WM_LBUTTONDBLCLK` repeats the dock
  splitter hit test (`:5167-5182`) before treating the press as a
  double-click in the terminal.
- `WM_LBUTTONDOWN`, `WM_LBUTTONDBLCLK`, `WM_MOUSEMOVE`, `WM_LBUTTONUP`,
  `WM_CAPTURECHANGED` read `GET_X_LPARAM`/`GET_Y_LPARAM`. Down clears the
  session's selection, sets capture, calls `selection_begin` with the click
  count (1; 2 on DBLCLK; 3 by the tick stamp and distance rule). Move while
  active calls `selection_extend` and stores the pointer; a move without
  `MK_LBUTTON` is ignored, not treated as the end of the drag (the AI panel's
  rule, `chat_listview.c:3895-3903`). `WM_CAPTURECHANGED` ends the drag
  without copying. Up ends it, and when `selection_finish` returns true
  copies via `selection_copy_text` and `clip_copy_utf8` (`clipboard.c:31`,
  which builds the data before emptying the clipboard).
- Timer 1's tick (10 ms, `window.c:3898`), when a drag is active and
  `selection_autoscroll_step` for the stored pointer is non-zero, scrolls
  the view by the step, clamped like the wheel handler, re-extends to the
  stored pointer's cell, marks the rows dirty, and logs `sel.scroll`.
- After every poll that fed the emulator: `selection_sync`, then
  `selection_watch_range` into the terminal, repaint on change.
- `do_copy` copies the active session's selection when `valid`; it still
  asks the AI panel first when that has focus. Ctrl+C no longer consults
  the selection. Select All calls `selection_select_all` then copies.
- `force_full_terminal_repaint` after every selection change, as today.

### 2.8 The renderer, `src/ui/renderer.c`

`get_visible_row` uses `term_view_to_logical`; `cell_in_selection` becomes
`selection_contains(sel, term, screen_row, col)`. The "any selection
disables the clean-row skip" rule stays.

### 2.9 Debug log

`sel.begin` (line, col, mode), `sel.scroll` (offset), `sel.copy` (lines,
bytes), `sel.clear` (reason: click, touched, epoch, recycled, close) at
`DLOG_DEBUG` on the UI channel.

## 3. Tests

### 3.1 Native

`tests/test_term.c`:
- `term_view_to_logical` at offset 0 equals the parser's screen rows; at
  offset k it is k lines earlier; -1 beyond the rows held; `term_resize`
  clamps a stale offset.
- `lines_recycled` increments only when the ring recycles; stable across
  10,000 scrolls on a 100-line ring; the alt ring has its own count; the
  epoch bumps on reflow and on each alt switch and nowhere else.
- `sel_touched`: set by a cell write, EL, ED, ECH, IL/DL, SU/SD and a
  DECSTBM scroll into a watched line; not set by writes outside it or when
  no range is set.
- `wrapped` is cleared by LF, CUP and friends onto a row and set only by
  auto-wrap: the Ctrl+U-at-a-wrapped-prompt sequence leaves the next output
  row unwrapped.

`tests/test_selection.c` (rewritten):
- `pixel_to_cell` with negative x and y gives row 0, col 0; beyond the
  bottom and right gives the last row and column.
- `autoscroll_step`: 0 inside, -1 one pixel above, -3 two character heights
  above, +1 one pixel below.
- `begin`/`extend`/`finish`: a press and release is not valid; a move of 3
  px across a cell boundary is not valid, 5 px is; a backward drag
  normalises; WORD on `/usr/local/bin` selects the whole path, on `foo,bar`
  selects `foo`, on a run of spaces the run, on a CJK character that one
  cell; LINE on a line wrapped over three rows selects all three from a
  press on the middle row; WORD drag backwards across two words selects
  both whole.
- `sync`: epoch mismatch clears; `sel_touched` clears; a recycled start is
  cut; a recycled anchor is cut so a later `extend` cannot resurrect it; a
  wholly recycled selection clears; `sync` during an active drag ends it.
- `copy_text`: three wrapped rows come out as one line; `foo ` | `bar` at a
  wrap boundary keeps its space; CRLF between lines and none at the end;
  trailing blanks trimmed; trailing empty rows dropped (a drag to the
  bottom of a mostly empty screen copies no blank lines); a selection of
  3,000 rows of 200 characters is returned whole; a copy while scrolled
  back equals the cells the renderer would highlight (both through
  `term_view_to_logical`); an empty selection gives "".
- `select_all` covers from `lines_recycled` to the last non-empty line.

### 3.2 Harness, gate tier, `tests/integration/cases/10-terminal.ps1`

A new helper `Send-NutshellDrag -Session -From @(x,y) -To @(x,y) [-Clicks n]`
in `NutshellIT.psm1` **sends** (not posts, as `Send-NutshellChatDrag` does
and for the same reason) `WM_LBUTTONDOWN`, a series of `WM_MOUSEMOVE` and
`WM_LBUTTONUP` with client coordinates, and for `-Clicks 2` sends
`WM_LBUTTONDBLCLK` itself, since sent input gets no double-click synthesis;
`-Clicks 3` sends a further `WM_LBUTTONDOWN` at once. Clipboard is read
with `Get-Clipboard -Raw` and compared with CRLF intact (not through
`Get-AiSelClipboard`, which folds CRLF). Cell geometry comes from the
`render.draw` debug event, as `helpers_ansi_contrast_floor` does; the
`sel.scroll` event is what the auto-scroll case waits on.

- `select_drag_past_bottom_copies_history`: run `seq 1 300`, wheel back
  100 lines, press on the first visible row and drag to 40 px below the
  terminal's bottom edge, wait on `sel.scroll` until the offset is 0,
  release; the clipboard holds every line from the pressed one to `300`
  and then the prompt line, more lines than the window has rows, CRLF
  separated.
- `select_while_scrolled_back_copies_highlight`: `seq 1 300`, wheel back
  100 lines, drag three rows; the clipboard holds the three numbers that
  were on those rows (computed from the offset), not the live rows.
- `click_does_not_touch_clipboard`: set the clipboard to a marker, click
  on a character and release 2 px away, the clipboard still holds the
  marker.
- `ctrl_shift_c_copies_after_drag`: clear the clipboard after the drag's
  auto-copy, press Ctrl+Shift+C, the clipboard holds the text again; Edit >
  Copy likewise.
- `ctrl_c_with_selection_interrupts`: drag, then `sleep 30`, Ctrl+C: the
  prompt returns within two seconds and the clipboard still holds the
  dragged text (the existing `ctrl_c_without_selection_interrupts` stays).
- `double_click_selects_word`, `triple_click_selects_line`: on `echo
  /usr/local/bin and more`, the clipboard holds `/usr/local/bin` and the
  whole line respectively.
- `output_into_selection_clears_it`: select the line a running
  `for i in 1 2 3; do printf '\r%d' $i; sleep 1; done` is writing; after
  the next write the highlight is gone (`sel.clear reason=touched` in the
  log) and Ctrl+Shift+C copies nothing new.

## 4. Out of scope, recorded on the notes open list

Shift+click to extend; Alt+drag block selection; middle-button paste; mouse
reporting to applications (modes 1000/1006); the per-byte paste writes over
SSH (`ssh_channel_write` sets socket options per call), which make a large
paste slow; the scrollback-size setting (section 6); a narrowing reflow of
a full ring drops the newest rows rather than the oldest (`buffer.c:142,
165`, pre-existing).

## 5. Risks

- `window.c` is over 5,000 lines; the handlers get shorter, not longer, by
  moving the logic to `selection.c`.
- The auto-scroll rides on timer 1 rather than a new `WM_TIMER` loop
  (CLAUDE.md's one-timer rule is about animation; this is a poll-tick
  check).
- The triple-click tick stamp is the one piece that cannot be tested
  natively; the harness case covers it.
- A persistent selection disables the renderer's clean-row skip while it
  exists; the cell-level shadow still bounds the GDI work. Not measured.

## 6. Open for the maintainer

1. **Ctrl+C always interrupts** (proposed). Today Ctrl+C copies when a
   selection exists; with a persistent selection that would stop it
   interrupting after any drag. PuTTY never copies on Ctrl+C.
2. **The scrollback-size setting is a separate change** (proposed). Wiring
   it here means `term_init` pre-allocates `capacity × cols × 20 B` and
   `term_reflow_buffer` allocates the whole capacity again on every
   `WM_SIZE` during a live resize (there is no `WM_EXITSIZEMOVE`). At the
   10,000 default that is about 40 MB per tab at 200 columns, 200 MB at the
   50,000 cap. It needs lazy allocation in reflow first. Until then a
   selection can reach 3,000 lines of history plus the screen.

## 7. Critique

Round 1 (2026-10-20, Opus, 18 findings). Accepted and folded in above: 1
(Ctrl+C, as a proposal in section 6), 2 (`CS_DBLCLKS`, splitter, harness
sends DBLCLK, triple-click distance), 3 (`WM_CAPTURECHANGED`, ignore
buttonless moves), 4 (writes touching the selection, section 2.3), 5
(scrollback setting split out, section 6), 6 (epoch, and a clear ends an
active drag), 7 (stale `wrapped`, triple-click walks back, trim per logical
line), 8 (trailing empty rows, harness expectation), 9 (drag threshold),
10 (one mapping in `src/term`, offset clamp on resize), 11 (anchor trimmed),
12 (timer 1 tick, stored pointer, wheel as the fast path), 13 (helper sends,
`sel.scroll` event, raw clipboard), 14 (explicit init), 15 (corrections:
no `term_clear`, no ED 3, alt ring recycles, sparse state unreachable), 16
(`clip_copy_utf8`), 18 (one line on the AI panel's choice). Rejected: none.
No change: 17 (wide characters: nothing to handle). Not verified by the
critic and still open: PuTTY's exact deselect rule (this spec clears on any
touching write, which is at least as strict); the memory figures in
section 6 are computed, not measured.
