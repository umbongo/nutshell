# Terminal selection and copy, PuTTY style

**Date**: 2026-10-20 (revised twice the same day after critique rounds 1 and 2)
**Status**: APPROVED by the maintainer on 2026-10-20 ("Decisions look good. Proceed"): Ctrl+C always interrupts; the scrollback-size setting is a separate change.
**Branch**: `feat/putty-selection`
**Decided by the maintainer**: "Have it behave like PuTTY"; include double-click
word and triple-click line selection; not Shift+click extend or Alt+drag blocks
(Shift+click extend is superseded by the amendment of 2026-10-20 after section 1);
Confirm Paste proceeds on Enter.
**Proposed after critique, the maintainer's defaults unless overruled**
(section 6): Ctrl+C always interrupts; the scrollback-size setting is a
separate change.

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
| 1 | The selection is stored in screen rows, so a drag cannot reach past the window: the row is clamped to `rows - 1` and nothing scrolls the view to follow the pointer. | `src/core/selection.h:10`, `selection.c:12-13`; no auto-scroll in `window.c` |
| 2 | The extractor maps screen row to buffer row without `scrollback_offset`; the renderer applies it. Scrolled back, the highlight shows history and the clipboard gets the live rows at the same screen positions. | `selection.c:50-56`; `renderer.c:94-117` |
| 3 | Wheel, scrollbar and PgUp/PgDn change the offset and leave the selection alone. | `window.c:5116-5139`, `:5284`, `:3437` |
| 4 | Mouse-up sets `valid = true` even when start equals end, so a plain click on a character copies it over the clipboard. | `window.c:5253` |
| 5 | Mouse-up copies and then sets `valid = false`, so Ctrl+Shift+C and Edit > Copy find nothing after a drag. | `window.c:5263`, `:3397-3402`, `:2731-2751` |
| 6 | The mouse handlers read `LOWORD`/`HIWORD`; a negative coordinate while captured wraps to about 65535. | `window.c:5187`, `:5223`, `:5247` |
| 7 | Both copy paths use an 8 KB stack buffer; longer text is cut silently. | `window.c:2739`, `:5255`; `selection.c:88,95` |
| 8 | `TermRow.wrapped` is ignored: a soft-wrapped line copies as several lines. Line endings are LF. | `selection.c:94-96` |
| 9 | One global `g_selection` is shared by every tab. | `window.c:602` |
| 10 | No `WM_CAPTURECHANGED` handling; the up-handler can `break` before `ReleaseCapture` when the active session changed mid-drag. | `window.c:5218`, `:5241` |
| 11 | `term_resize` never clamps `scrollback_offset`; `scroll_page_up` clamps only to `max_scrollback` (`edit_scroll.c:64-73`); the renderer hides both by clamping `top_logical` to 0. | `buffer.c:256-300`, `renderer.c:104-106` |
| 12 | `wrapped` is reset only by `term_row_fill` and ED 2, so after a wrapped prompt is erased with Ctrl+U the next output row still says `wrapped`. | `parser.c:366-460` |
| 13 | Settings offers "scrollback lines" (default 10,000, clamped 100..50,000) but nothing reads it: every session gets 3,000 lines. | `config/loader.c:452-487`, `settings.c:1262,1937`; `window.c:612`. Not fixed here, section 6 |
| 14 | Confirm Paste creates Paste as the default push button but never focuses it, and its loop handles only Escape; Enter and Tab do nothing. | `paste_dlg.c:258`, `:521-529` |

Paste itself (`do_paste`, `window.c:2332-2459`) is sound and not changed.

Existing tests: 19 native tests in `tests/test_selection.c` on the extractor;
none covers scrollback, wrapped rows, the size limit or out-of-window
coordinates. The harness has paste cases and
`ctrl_c_without_selection_interrupts`; no case drags in the terminal or
reads the clipboard back.

## 1. Behaviour (PuTTY's, as far as this change goes)

- **Drag selects and copies.** Left button down starts a selection at the
  cell under the pointer; moving extends it; release copies it if it is
  non-empty, and the highlight stays. A press and release without movement,
  or with movement under the system drag threshold (`SM_CXDRAG`/`SM_CYDRAG`),
  clears any selection and copies nothing: a click to focus the window
  never touches the clipboard.
- **Dragging past an edge scrolls.** While the button is held and the last
  reported pointer position is above the top edge or below the bottom edge
  of the terminal area, every tick of the window's existing 10 ms timer
  scrolls the view towards the pointer by one line plus one per full
  character height of distance beyond the edge, and the selection end
  follows. The stored last position is used, never the live cursor. In a
  maximized window the pointer gets only a few rows past either edge, so a
  long history takes minutes by this route; the wheel during a drag scrolls
  as usual and the end follows on the next move. The view never scrolls past
  the oldest or the newest line.
- **Double-click selects a word, triple-click a line.** A word is a maximal
  run of cells in one class, following a soft wrap across rows (a URL wrapped
  over two rows double-clicks whole, as in PuTTY). The classes: blank (space,
  empty cell, control codepoint); "word" (letters, digits, `_`, `-`, `.`,
  `/`, `~`, `:`, `@`); every other codepoint on its own (the emulator has one
  codepoint per cell and no wide-cell model). A line is the whole logical
  line: back to the first row that is not a continuation, bounded by the
  oldest row held, forward over every following row whose `wrapped` flag is
  set. Dragging after a double or triple click extends by whole words or
  lines in either direction. The window class gains `CS_DBLCLKS`; a third
  press within `GetDoubleClickTime()` and `SM_CXDOUBLECLK`/`SM_CYDOUBLECLK`
  of the double-click is the triple.
- **The selection persists** until: the next left press in the terminal
  area; a write into a selected row after the release (section 2.3); a
  resize; a switch to or from the alternate screen; the session closing; or
  every selected line being recycled. While the button is held, writes do
  not clear it: dragging over streaming output selects what is there at
  release. New output that only scrolls the selected text, scrolling the
  view, and typing elsewhere do not clear it. It is per session. (The AI
  panel's selection deliberately does not copy on release,
  `chat_listview.c:3975`; both behaviours are on the cheat sheet.)
- **Copy again from the keyboard and the menu.** Ctrl+Shift+C and Edit >
  Copy copy the current selection. **Ctrl+C always sends the interrupt**
  (section 6.1). Edit > Select All selects from the oldest line held to the
  last non-empty line and copies it; on an empty terminal it selects nothing.
- **The text.** Heap, real size, no limit. A soft-wrapped line is one line;
  trailing blanks are trimmed per logical line after joining, never per row;
  trailing empty rows are dropped (CLAUDE.md, "Terminal Buffer"); lines end
  in CRLF; the last line has no ending. Cells go through
  `cell_text_codepoint()`.
- **Right-click pastes**, unchanged.
- **Confirm Paste** (section 3.5): Paste has focus when the dialog opens,
  Enter pastes, Tab moves between Paste and Cancel, Escape cancels.

### Amendment, 2026-10-20 (the selection follow-ups, branch `feat/selection-extend`)

The maintainer then asked for more of PuTTY. This amendment supersedes the
original decision above not to extend with Shift+click, and the "Shift+click
to extend" and "middle-button paste" lines of section 4 where they say the
middle button does not extend:

- **Shift+click and middle-click extend the selection** (PuTTY's which-end
  rule): a click before the start moves the start, after the end moves the
  end, inside moves the nearer end (nearer by line, then by column; a tie
  moves the end). The selection keeps its mode, so a word or line selection
  extends by whole words or lines, and the drag is live again. With no
  selection it is an ordinary press.
- **The click cycle is 1, 2, 3, 1, ...** by time and rectangle, whichever
  message Windows delivers the press as: a fourth rapid click selects a cell
  again.
- **A combining mark joins the cell before it** for word selection, so a
  decomposed accented word is one word.

## 2. Design

### 2.1 Two mappings in the terminal module, live and view

`term_screen_to_phys()` (`term.h:196-208`) stays the parser's live mapping:
writes always land on the live screen, scrolled back or not. `term.h` gains
the view mapping, which the renderer and the selection use:

```c
/* Logical index of the row shown at screen_row in the view as scrolled
 * (scrollback_offset applied, top clamped to 0 exactly as the renderer's
 * get_visible_row does today). Never -1 for 0 <= screen_row < rows. */
int term_view_to_logical(const Terminal *t, int screen_row);
```

`term_resize()` clamps `scrollback_offset` to `max(0, lines_count - rows)`
after reflow, and `scroll_page_up` (`edit_scroll.c`) clamps to the history
actually held, so the clamp inside the mapping is a guard, not the rule.

### 2.2 Line numbers, epochs and write stamps

The history is a ring; once full, `term_scroll()` recycles the oldest row
(`buffer.c:309-313`). No other path drops rows from the front (ED 3 is not
implemented); reflow renumbers everything; the alternate screen is a
separate ring (`buffer.c:505-513`) whose LF at the bottom also recycles.
The terminal gains

```c
uint64_t lines_recycled;   /* rows dropped from the front of this ring, ever (per ring) */
uint32_t epoch;            /* bumped by reflow and by alt-screen enter/leave */
uint32_t write_gen;        /* bumped by every row write; TermRow gains uint32_t gen */
```

The **absolute line number** of logical index `i` is `lines_recycled + i`,
stable while the epoch is. Every parser write goes through
`get_screen_row()` (`parser.c:37`), which stamps `row->gen = ++t->write_gen`
beside `row->dirty = true`; `term_scroll_up`/`term_scroll_down` in
`buffer.c` stamp the rows they move or clear; the recycle branch stamps the
refilled row after `lines_recycled` is incremented, so its new number is
what the stamp belongs to. That is the single choke point: cell writes,
insert-mode shifts, ICH/DCH, EL/ED/ECH, IL/DL/SU/SD, DECSTBM scrolling and
`term_scroll` are all covered without a list. Cost: an increment and a
store per write, four bytes per row.

### 2.3 Staleness, checked lazily

The selection records `epoch` and `write_gen` at its last change. A
selection is **stale** when the terminal's epoch differs, or when any
selected row's `gen` is newer than the selection's, or when all its lines
are recycled. `selection_contains` checks the row it is asked about (so the
highlight disappears row by row as rows are written, and a stale selection
paints nothing); `selection_copy_text` and `selection_sync` check every
selected row and clear a stale selection. While `active`, the stamp is
refreshed on every `extend`, and `finish` takes the current `write_gen`:
what is on screen at release is what is copied. No watched range, no flag,
no hook at any `term_process` call site; a session that is not polled (a
reconnecting one writing its own banner) is covered because the check is at
paint and copy time.

### 2.4 The `wrapped` flag

Its meaning is right (set on the continuation row by auto-wrap,
`parser.c:69-71`; reflow agrees), but it is reset only when a row is
refilled or by ED 2. Readline moves the cursor onto a continuation row with
a bare LF (`cud1`) and back up with CUU, so cursor motion must not touch
the flag. The parser clears `wrapped` where a row's content is erased: EL
from column 0, EL 2, and the whole-row loops of ED 0 and ED 1 (today these
reset only `len`). Native tests pin both the Ctrl+U case and the readline
Home/End case.

### 2.5 The selection

```c
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
```

One `Selection` per `Session`, explicitly zeroed in `create_session`
(`Session` comes from `xmalloc`). The window keeps one global
`g_drag_session` (the session whose drag holds the capture), so a tab close
or switch mid-drag still releases capture.

### 2.6 The portable API, `src/core/selection.c`

Pure, takes `const Terminal *`, tested natively; `window.c` keeps the
message handlers, the capture and the timer tick.

```c
void selection_pixel_to_cell(int px, int py, int char_w, int char_h,
                             int y_offset, int rows, int cols,
                             int *out_row, int *out_col);   /* signed, clamped */
int  selection_autoscroll_step(int py, int top_px, int bottom_px, int char_h);

void selection_begin(Selection *s, const Terminal *t, int screen_row, int col,
                     int click_count, int px, int py);
void selection_extend(Selection *s, const Terminal *t, int screen_row, int col,
                      int px, int py, int drag_cx, int drag_cy);
bool selection_finish(Selection *s, const Terminal *t);   /* takes the stamps; returns valid */
void selection_clear(Selection *s);
void selection_select_all(Selection *s, const Terminal *t); /* valid = false when empty */
/* Trim the four points below lines_recycled, clear when stale (2.3) or
 * empty; end an active drag on an epoch change. Returns true on change. */
bool selection_sync(Selection *s, const Terminal *t);
bool selection_contains(const Selection *s, const Terminal *t, int screen_row, int col);
/* True when a valid selection intersects the rows in view (renderer hint). */
bool selection_in_view(const Selection *s, const Terminal *t);
int  selection_char_class(uint32_t cp);  /* 0 blank, 1 word, 2+cp otherwise */
char *selection_copy_text(const Selection *s, const Terminal *t, size_t *out_len);
```

`selection_normalise()` and `selection_extract_text()` go.

### 2.7 The window, `src/ui/window.c`

- Window class: add `CS_DBLCLKS`. `WM_LBUTTONDBLCLK` repeats the dock
  splitter hit test (`:5167-5182`) first.
- `WM_LBUTTONDOWN`, `WM_LBUTTONDBLCLK`, `WM_MOUSEMOVE`, `WM_LBUTTONUP`,
  `WM_CAPTURECHANGED` use `GET_X_LPARAM`/`GET_Y_LPARAM`. Down clears the
  session's selection, sets `g_drag_session`, sets capture, calls
  `selection_begin` with the click count. Move while active calls
  `selection_extend` and stores the pointer; a move without `MK_LBUTTON` is
  ignored (`chat_listview.c:3895-3903`). **Up: finish and copy first, then
  release capture**, because `ReleaseCapture` sends `WM_CAPTURECHANGED`
  synchronously. `WM_CAPTURECHANGED` with `g_drag_session` set ends that
  drag without copying and clears the pointer; without it, it is ignored.
  Wherever a drag is ended (up, capture change, a clear, tab close), capture
  is released only if `GetCapture() == hwnd`.
- Copy: `selection_copy_text` then `clip_copy_utf8` (`clipboard.c:31`, which
  builds the data before emptying the clipboard and leaves it alone on an
  empty length).
- Timer 1's tick (10 ms, `window.c:3898`), **outside** the per-session
  `io.ctx`/`CONN_IDLE` gate so it works on a closed session's history: when
  `g_drag_session` is set and `selection_autoscroll_step` for the stored
  pointer is non-zero, scroll the view by the step (clamped like the wheel
  handler), re-extend to the stored pointer's cell, mark rows dirty, log
  `sel.scroll`. Also each tick, for every session: `selection_sync`, repaint
  on change (two compares when nothing changed).
- `do_copy` copies the active session's selection when valid and not stale;
  it still asks the AI panel first when that has focus. Ctrl+C no longer
  consults the selection. Select All calls `selection_select_all` then
  copies.
- `force_full_terminal_repaint` after every selection change, as today.

### 2.8 The renderer, `src/ui/renderer.c`

`get_visible_row` uses `term_view_to_logical`; `cell_in_selection` becomes
`selection_contains(sel, term, screen_row, col)`; the "any selection
disables the clean-row skip" rule uses `selection_in_view`.

### 2.9 Documentation of the Ctrl+C change

`README.md:121` and `:355`, `src/ui/help_guide.c:100-101` and `:354` say
Ctrl+C copies when a selection exists; they change in the same commit as
the handler, and the cheat sheet's row says "interrupt".

### 2.10 Debug log

`sel.begin` (line, col, mode), `sel.scroll` (offset), `sel.copy` (lines,
bytes), `sel.clear` (reason: click, stale, epoch, recycled, close) at
`DLOG_DEBUG` on the UI channel.

## 3. Tests

### 3.1 Native

`tests/test_term.c`:
- `term_view_to_logical` at offset 0 equals `term_screen_to_phys` through
  the ring; at offset k it is k lines earlier; a fresh tab with the offset
  forced to `rows - 1` maps row 0 to logical 0, not -1; `term_resize` clamps
  a stale offset; the parser writes to the live screen while scrolled back.
- `lines_recycled` increments only when the ring recycles; stable across
  10,000 scrolls on a 100-line ring; the alt ring has its own count; the
  epoch bumps on reflow and on each alt switch and nowhere else.
- `write_gen`: a cell write, insert-mode shift, ICH, DCH, EL, ED, ECH,
  IL/DL, SU/SD, a DECSTBM scroll and `term_scroll` each stamp exactly the
  rows they touch; the recycled row carries its new number.
- `wrapped`: cleared by EL 2, EL from column 0, ED 0/1 whole rows; kept by
  LF, CUU, CUP onto the row (the readline Home/End sequence on a wrapped
  command keeps the continuation wrapped); the Ctrl+U sequence leaves the
  next output row unwrapped.

`tests/test_selection.c` (rewritten):
- `pixel_to_cell` negative x and y give row 0, col 0; beyond the bottom and
  right give the last row and column.
- `autoscroll_step`: 0 inside, -1 one pixel above, -3 two character heights
  above, +1 one pixel below.
- `begin`/`extend`/`finish`: press and release is not valid; a move under
  the threshold across a cell boundary is not valid, over it is; a backward
  drag normalises; WORD on `/usr/local/bin` selects the path, on `foo,bar`
  selects `foo`, on a run of spaces the run, on a CJK character that cell,
  on a URL wrapped over two rows the whole URL; LINE from a press on the
  middle row of a three-row wrapped line selects all three, and from a
  continuation row whose head is recycled stops at logical 0; WORD drag
  backwards across two words selects both whole.
- staleness: a write into a selected row after `finish` makes `contains`
  false for that row and `copy_text` return nothing and clear; a write
  during an active drag does not; an epoch change clears and ends an active
  drag; a recycled start is cut; a recycled anchor is cut so `extend` cannot
  resurrect it; wholly recycled clears; the row at the recycle point is
  trimmed, not treated as touched.
- `copy_text`: three wrapped rows come out as one line; `foo ` | `bar` at a
  wrap boundary keeps its space; CRLF between lines and none at the end;
  trailing blanks trimmed; trailing empty rows dropped; 3,000 rows of 200
  characters returned whole; a copy while scrolled back equals the cells
  the view mapping names; an empty selection gives "".
- `select_all` covers from `lines_recycled` to the last non-empty line and
  is not valid on an empty terminal; `selection_in_view` is false for a
  selection scrolled out of view.

### 3.2 Harness, gate tier, `tests/integration/cases/10-terminal.ps1`

`NutshellIT.psm1` gains `Send-NutshellDrag -Session -From @(x,y) -To @(x,y)
[-Clicks n] [-Hold]` and `Send-NutshellDragEnd -Session -At @(x,y)`: sent,
not posted, as `Send-NutshellChatDrag` is; `-Clicks 2` sends
`WM_LBUTTONDBLCLK` itself, `-Clicks 3` a further `WM_LBUTTONDOWN` at once;
`-Hold` leaves the button down so a case can wait on `sel.scroll` and then
end the drag. Distances are in cells from the `render.draw` geometry
(`char_w cols x y`, as `helpers_ansi_contrast_floor` reads it), never pixel
literals (the dev box runs at 288 DPI). The clipboard is read with
`Get-Clipboard -Raw` and compared with CRLF intact. One case asserts the
window class has `CS_DBLCLKS` (`GetClassLong`), since sent double-clicks do
not need it.

- `select_drag_past_bottom_copies_history`: `seq 1 300`, wheel back 100
  lines, press on the first visible row, drag to two cell heights below the
  bottom edge with `-Hold`, wait on `sel.scroll` until the offset is 0, end
  the drag; the clipboard holds every line from the pressed one to `300`
  and the prompt, more lines than rows, CRLF separated.
- `select_drag_above_top_selects_first_row`: press on row 3, drag to two
  cell heights above the terminal area and release; the copy starts at the
  first visible row's text.
- `select_while_scrolled_back_copies_highlight`: `seq 1 300`, wheel back
  100 lines, drag three rows; the clipboard holds the three numbers that
  were on those rows.
- `click_does_not_touch_clipboard`: clipboard set to a marker, press and
  release half a drag threshold away; the marker remains.
- `ctrl_shift_c_copies_after_drag`: after the drag, set the clipboard to a
  marker, Ctrl+Shift+C; the clipboard holds the text again; Edit > Copy
  likewise.
- `ctrl_c_with_selection_interrupts`: drag, `sleep 30`, Ctrl+C: the prompt
  returns within two seconds and the clipboard still holds the dragged
  text. The existing `ctrl_c_without_selection_interrupts` stays.
- `double_click_selects_word`, `triple_click_selects_line`: on `echo
  /usr/local/bin and more`, `/usr/local/bin` and the whole line.
- `output_into_selection_clears_it`: select the line a
  `for i in 1 2 3; do printf '\r%d' $i; sleep 1; done` is writing; after
  the next write the log has `sel.clear reason=stale`; set the clipboard
  to a marker, Ctrl+Shift+C, the marker remains.

### 3.5 Also in this change: Confirm Paste accepts Enter

Asked for by the maintainer (2026-10-20): "when I paste and the confirmation
window pops up, default the selection to the permit button so that I only
need to press enter to proceed with the paste."

Today `src/ui/paste_dlg.c` creates Paste with `BS_DEFPUSHBUTTON` (`:258`)
but never focuses it, and its modal loop (`:521-529`) handles only
`VK_ESCAPE`. The preview is a read-only edit (`:243`), so Enter has no other
meaning in the dialog.

- On creation, `SetFocus(hBtnPaste)`.
- In the modal loop, `WM_KEYDOWN` of `VK_RETURN` sends `WM_COMMAND IDOK` to
  the dialog (the button's path, `:377`); `VK_TAB` moves focus between Paste
  and Cancel; Escape is unchanged.
- Harness, gate tier, beside `paste_with_confirmation_shows_dialog`:
  `paste_with_confirmation_enter_pastes` sets the clipboard to `echo
  PASTE_ENTER_8`, Ctrl+V, waits for the dialog, posts Enter to the dialog's
  handle, asserts the dialog is gone and `PASTE_ENTER_8` reached the shell.

## 4. Out of scope, recorded on the notes open list

Shift+click to extend (superseded, see the amendment after section 1);
Alt+drag block selection; middle-button paste (the middle button extends
instead, same amendment); mouse
reporting to applications; the per-byte paste writes over SSH; the
scrollback-size setting (section 6); a narrowing reflow of a full ring
drops the newest rows rather than the oldest (`buffer.c:142, 165`,
pre-existing).

## 5. Risks

- `window.c` is over 5,000 lines; the handlers get shorter by moving the
  logic to `selection.c`.
- The auto-scroll rides on timer 1 rather than a new `WM_TIMER` loop.
- The triple-click tick stamp cannot be tested natively; the harness case
  covers it, with `GetMessageTime()` on sent messages not verified.
- A persistent selection in view disables the renderer's clean-row skip;
  the cell-level shadow bounds the GDI work. Not measured.
- `Get-Clipboard -Raw` keeping CRLF under Windows PowerShell 5.1 is
  inferred from `95-ai-select.ps1`, not verified; the case will tell.

## 6. Open for the maintainer

1. **Ctrl+C always interrupts** (proposed, taken as the default). Today
   Ctrl+C copies when a selection exists; with a persistent selection that
   would stop it interrupting after any drag. PuTTY never copies on Ctrl+C.
2. **The scrollback-size setting is a separate change** (proposed, taken as
   the default). `term_init` pre-allocates `capacity × cols × 20 B` and
   `term_reflow_buffer` allocates the whole capacity again on every
   `WM_SIZE` during a live resize. At the 10,000 default that is about
   40 MB per tab at 200 columns, 200 MB at the 50,000 cap. It needs lazy
   allocation in reflow first. Until then a selection can reach 3,000 lines
   of history plus the screen.

## 7. Critique

**Round 1** (2026-10-20, Opus, 18 findings). Accepted: 1 (Ctrl+C, as 6.1),
2 (`CS_DBLCLKS`, splitter, harness sends DBLCLK, triple-click distance), 3
(`WM_CAPTURECHANGED`, ignore buttonless moves), 4 (writes touching the
selection), 5 (scrollback setting split out, 6.2), 6 (epoch; a clear ends
an active drag), 7 (stale `wrapped`, triple-click walks back, trim per
logical line), 8 (trailing empty rows), 9 (drag threshold), 10 (one view
mapping in `src/term`, offset clamp), 11 (anchor trimmed), 12 (timer 1,
stored pointer, wheel as the fast path), 13 (helper sends, `sel.scroll`,
raw clipboard), 14 (explicit init), 15 (corrections), 16
(`clip_copy_utf8`), 18 (one line on the AI panel). No change: 17 (wide
characters). Rejected: none.

**Round 2** (2026-10-20, fresh Opus critic, 14 findings). Accepted: 1
(clear `wrapped` on erase, not on cursor motion; readline Home/End test),
2 (live and view mappings, parser keeps the live one), 3 (epoch checked at
paint and copy, not only after a poll), 4 and 5 and 6 and 7 (replaced the
watched range by per-row write stamps at the parser's one choke point,
the critic's cheaper design; recycle ordering stated), 8 (clamp inside the
view mapping, `scroll_page_up` clamp), 9 (`g_drag_session`, release when
`GetCapture() == hwnd`), 10 (finish and copy before release;
`WM_CAPTURECHANGED` ignored without a drag), 11 (no staleness while
active; `finish` takes the stamps), 12 (README and guide text for Ctrl+C),
13 (words follow a wrap), 14 (Select All on empty is invalid; walk-back
bound; `selection_in_view`). Test gaps accepted: class-style assertion,
drag-above case, `-Hold`, marker before Ctrl+Shift+C, cell-relative
distances. Rejected: none. Still not verified, carried into section 5:
`GetMessageTime()` on sent messages, `Get-Clipboard -Raw` and CRLF,
`SM_CXDRAG` at 288 DPI, PuTTY's exact deselect rule. Two rounds is the
ceiling; anything the plan's execution finds beyond this goes in the pull
request body.
