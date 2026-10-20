# Terminal selection and copy, PuTTY style

**Date**: 2026-10-20
**Status**: DRAFT, for the maintainer's review
**Branch**: `feat/putty-selection`
**Decided by the maintainer**: "Have it behave like PuTTY"; include double-click
word and triple-click line selection; not Shift+click extend or Alt+drag blocks.

## 0. The problem

The maintainer's report (2026-10-18 to 20): "I can't properly copy and paste
from the terminal"; "when I select and drag down in the nutshell terminal, it
only copies that which is on the window, not that which has been selected in
the buffer."

A survey of `main` at fbfe5cd (v1.2.43) found the cause and five further
defects, all present since the original selection commit e5b3aaf
(2026-03-08); nothing regressed in v1.2.36.

| # | Defect | Where |
|---|---|---|
| 1 | The selection is stored in screen rows ("0-based from top of visible area"), so a drag cannot reach past the window: the row is clamped to `rows - 1` and nothing scrolls the view to follow the pointer. | `src/core/selection.h:10`, `selection.c:12-13`; no auto-scroll anywhere in `window.c` |
| 2 | The extractor maps screen row to buffer row as `lines_count - rows + row` and never reads `scrollback_offset`; the renderer does. Scrolled back, the highlight shows history and the clipboard gets the live rows at the same screen positions. | `selection.c:50-56`; `renderer.c:94-117` |
| 3 | Wheel, scrollbar and PgUp/PgDn change the offset and leave the selection alone, so the highlight stays on screen cells whose content changed. | `window.c:5116-5139`, `:5284`, `:3437` |
| 4 | Mouse-up sets `valid = true` even when start equals end, so a plain click on a character copies that character over the clipboard. | `window.c:5253` |
| 5 | Mouse-up copies and then sets `valid = false`, so Ctrl+Shift+C, Ctrl+C-with-selection and Edit > Copy find no selection after a drag and do nothing; plain Ctrl+C falls through as an interrupt. | `window.c:5263`, `:3397-3402`, `:2731-2751` |
| 6 | The mouse handlers read `LOWORD`/`HIWORD`; a negative coordinate while captured wraps to about 65535, so a drag above the window selects the bottom row and a drag left of it the last column. | `window.c:5187`, `:5223`, `:5247` |
| 7 | Both copy paths use an 8 KB stack buffer; longer text is cut silently. | `window.c:2739`, `:5255`; `selection.c:88,95` |
| 8 | `TermRow.wrapped` is ignored: a soft-wrapped line copies as several lines. Line endings are LF. | `selection.c:94-96` |
| 9 | One global `g_selection` is shared by every tab. | `window.c:602` |
| 10 | Settings offers "scrollback lines" (default 10,000, clamped 100..50,000, saved and loaded) but nothing reads it: every session is created with 3,000 lines of history. | `config/loader.c:452-487`, `settings.c:1262,1937`; `window.c:612` |

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
  without movement clears any selection and copies nothing.
- **Dragging past an edge scrolls.** While the button is held and the pointer
  is above the top edge or below the bottom edge of the terminal area, the
  view scrolls towards the pointer one line per 50 ms tick, plus one line per
  full character height of distance beyond the edge, and the selection end
  follows. The wheel during a drag scrolls as usual and the end follows the
  pointer on the next move. The view never scrolls past the oldest or the
  newest line.
- **Double-click selects a word, triple-click a line.** A word is a maximal
  run of cells in one class; the classes are: blank (space, empty cell,
  control codepoint); "word" (letters, digits, `_`, `-`, `.`, `/`, `~`, `:`,
  `@`, so that paths, hostnames and addresses select whole); and every other
  codepoint on its own. A line is the whole logical line: the row and every
  following row whose `wrapped` flag is set. Dragging after a double or
  triple click extends by whole words or whole lines in either direction.
  The clicks count by `GetDoubleClickTime()` and `WM_LBUTTONDBLCLK`
  (the window class has `CS_DBLCLKS`; a third press within the double-click
  time after a double-click is the triple).
- **The selection persists.** It stays until the next left press in the
  terminal area, a resize, a switch to or from the alternate screen, the
  session closing, or every selected line being recycled out of history.
  Typing, new output and scrolling do not clear it: it stays on the same
  lines of text and scrolls with them, off the screen if need be. It is
  per session, so switching tabs shows each tab's own selection.
- **Copy again from the keyboard and the menu.** Ctrl+Shift+C, Ctrl+C while a
  selection exists, and Edit > Copy copy the current selection; Ctrl+C
  without a selection sends the interrupt as today. Edit > Select All
  selects the whole buffer, history included, and copies it.
- **The text.** Built on the heap at its real size (no limit). A soft-wrapped
  line is one line in the clipboard; trailing blanks are trimmed from each
  line; lines end in CRLF (PuTTY on Windows); the last line has no ending
  unless it was selected past its end onto the next row. Cells are emitted
  as UTF-8 through the existing `cell_text_codepoint()` filter.
- **Right-click pastes**, unchanged. Paste is otherwise untouched.
- **Scrollback size** comes from Settings: a session is created with
  `settings.scrollback_lines` of history; a change applies to tabs opened
  afterwards (the history size is fixed at open, as today).

## 2. Design

### 2.1 Line numbers that survive scrolling

The history is a ring (`Terminal.lines`, `lines_start`, `lines_count`,
`lines_capacity`). Once full, `term_scroll()` recycles the oldest row by
moving `lines_start` forward (`buffer.c:309-313`), so a logical index points
at a different line after every scroll. The terminal gains

```c
uint64_t lines_recycled;   /* rows dropped from the front of the ring, ever */
```

incremented in that branch and in any other code that discards a row from
the front (none found; the implementer checks `term_clear` and the
scrollback-clearing paths and adds the increment where a row is dropped).
The **absolute line number** of logical index `i` is `lines_recycled + i`.
It never changes for a given row of text. A reflow (`term_reflow_buffer`,
on resize) renumbers everything, which is why a resize clears the
selection; the alternate screen has its own ring and its own count, which
is why switching clears it. `term_init()` sets the counter to 0; the primary
and alternate buffers each keep their own (`primary_lines_*` fields gain a
`primary_lines_recycled`).

### 2.2 The selection

```c
typedef struct { uint64_t line; int col; } SelPoint;
typedef enum { SEL_STREAM = 0, SEL_WORD = 1, SEL_LINE = 2 } SelMode;

typedef struct {
    SelPoint anchor;   /* the press; for WORD/LINE, the unit under it is anchor..anchor_end */
    SelPoint anchor_end;
    SelPoint start;    /* normalised: start <= end, both inclusive */
    SelPoint end;
    SelMode  mode;
    bool     active;   /* button held */
    bool     valid;    /* a non-empty selection exists */
} Selection;
```

One `Selection` per `Session` (`window.c`'s `Session` struct), replacing the
global. Columns are inclusive cell indexes; a point past a row's content is
legal (the extractor trims).

### 2.3 The portable API, `src/core/selection.c`

Everything below is pure, takes `const Terminal *`, and is tested natively.
`window.c` keeps the message handlers, the capture, and the timer.

```c
/* Screen row (0..rows-1) of the view as currently scrolled, to an absolute
 * line, including scrollback_offset -- the one mapping, used by the renderer
 * and the extractor. */
uint64_t selection_screen_to_line(const Terminal *t, int screen_row);
/* Absolute line to screen row in the current view, or -1 when off screen. */
int      selection_line_to_screen(const Terminal *t, uint64_t line);
/* Logical index for an absolute line, or -1 when recycled or beyond count. */
int      selection_line_to_logical(const Terminal *t, uint64_t line);

/* Pixel to cell, signed, clamped to 0..rows-1 / 0..cols-1. Replaces the
 * current signature's behaviour for negative input. */
void selection_pixel_to_cell(int px, int py, int char_w, int char_h,
                             int y_offset, int rows, int cols,
                             int *out_row, int *out_col);

/* Lines to scroll per tick while dragging at py: negative = up (towards
 * history), 0 inside [top_px, bottom_px), positive = down; magnitude
 * 1 + (distance beyond the edge) / char_h. */
int  selection_autoscroll_step(int py, int top_px, int bottom_px, int char_h);

/* Button down with click_count 1, 2 or 3: STREAM at the cell; WORD or LINE
 * covering the unit under it (valid at once for 2 and 3). */
void selection_begin(Selection *s, const Terminal *t, uint64_t line, int col,
                     int click_count);
/* Pointer moved while active: STREAM sets end; WORD/LINE extend by units
 * from the anchor unit in the pointer's direction. */
void selection_extend(Selection *s, const Terminal *t, uint64_t line, int col);
/* Button up: active = false; valid = (start != end) for STREAM, already
 * true for WORD/LINE. Returns valid. */
bool selection_finish(Selection *s);
void selection_clear(Selection *s);
/* Whole buffer: line lines_recycled, col 0 .. last logical line, last col. */
void selection_select_all(Selection *s, const Terminal *t);
/* After output: drop any part below lines_recycled; clear when nothing is
 * left. Called by the window after each poll that produced output. */
void selection_trim(Selection *s, const Terminal *t);

bool selection_contains(const Selection *s, uint64_t line, int col);
/* Word class for the double-click rule: 0 blank, 1 word, 2 + codepoint for
 * everything else (each such character is its own class). */
int  selection_char_class(uint32_t cp);
/* The text: heap, NUL-terminated, *out_len bytes, caller frees; NULL only on
 * allocation failure. Joins wrapped rows, trims per line, CRLF. */
char *selection_copy_text(const Selection *s, const Terminal *t, size_t *out_len);
```

`selection_normalise()` and `selection_extract_text()` go; their callers
move to the above.

### 2.4 The window, `src/ui/window.c`

- `WM_LBUTTONDOWN`, `WM_LBUTTONDBLCLK`, `WM_MOUSEMOVE`, `WM_LBUTTONUP` read
  `GET_X_LPARAM`/`GET_Y_LPARAM`. Down clears the session's selection, starts
  capture and calls `selection_begin` with the click count (1; 2 on
  `WM_LBUTTONDBLCLK`; 3 when a `WM_LBUTTONDOWN` arrives within
  `GetDoubleClickTime()` of the double-click, tracked by a tick stamp). Move
  calls `selection_extend` with the cell under the pointer, and starts or
  stops the auto-scroll timer (`SEL_SCROLL_TIMER_ID`, 50 ms) according to
  `selection_autoscroll_step`. Each tick scrolls the view by the step
  (clamped like the wheel handler), re-extends to the pointer's cell at the
  edge, and marks the rows dirty. Up releases capture, stops the timer,
  calls `selection_finish`, and copies via `selection_copy_text` +
  `clip_set_utf8` when it returns true.
- `do_copy` copies the active session's selection when `valid`; it still
  asks the AI panel first when that has focus. Ctrl+C, Ctrl+Shift+C and
  Edit > Copy are unchanged otherwise. Select All calls
  `selection_select_all` then copies.
- After every poll that fed the emulator, `selection_trim`. On `term_resize`
  and on alt-screen enter/leave, `selection_clear`. A full repaint follows a
  change, as today (`force_full_terminal_repaint`).
- `term_init(rows, cols, cfg->settings.scrollback_lines)` at session
  creation.

### 2.5 The renderer, `src/ui/renderer.c`

`cell_in_selection(sel, screen_row, col)` becomes
`selection_contains(sel, selection_screen_to_line(term, screen_row), col)`.
Nothing else changes; the existing "any selection disables the clean-row
skip" rule stays.

### 2.6 Debug log

`sel.begin` (line, col, mode), `sel.copy` (lines, bytes), `sel.clear`
(reason) at `DLOG_DEBUG` on the UI channel, so a report like this one can be
read from a log next time.

## 3. Tests

### 3.1 Native, `tests/test_selection.c` (rewritten) and `tests/test_term.c`

- `lines_recycled` increments only when the ring recycles; absolute numbers
  are stable across 10,000 scrolls on a 100-line ring; `line_to_logical`
  returns -1 for a recycled line.
- `screen_to_line` at offset 0 equals the parser's screen rows; at offset k
  it is k lines earlier; the sparse state after a resize (`lines_count <
  rows`) maps every screen row.
- `pixel_to_cell` with negative x and y gives row 0, col 0; beyond the
  bottom and right gives the last row and column.
- `autoscroll_step`: 0 inside, -1 one pixel above, -3 two character heights
  above, +1 one pixel below.
- `begin`/`extend`/`finish`: a press without movement is not valid; a
  backward drag normalises; WORD on `/usr/local/bin` selects the whole
  path, on `foo,bar` selects `foo`, on a run of spaces selects the run;
  LINE on a line wrapped over three rows selects all three; WORD drag
  backwards across two words selects both whole.
- `trim`: a selection whose first line is recycled is cut to the rest; one
  wholly recycled is cleared.
- `copy_text`: three wrapped rows come out as one line; CRLF between lines;
  trailing blanks trimmed; a selection of 3,000 rows of 200 characters is
  returned whole (over 600 KB); a copy while scrolled back equals the rows
  the renderer highlights (both through `screen_to_line`); empty selection
  gives "".
- `select_all` covers from `lines_recycled` to the last written line.
- `term_init` is called with the configured scrollback: a test on
  `config` defaults that `scrollback_lines` is 10,000 already exists;
  the wiring is covered by the harness case below.

### 3.2 Harness, gate tier, `tests/integration/cases/10-terminal.ps1`

A new helper `Send-NutshellDrag -Session -From @(x,y) -To @(x,y) [-Clicks n]`
in `NutshellIT.psm1` posts `WM_LBUTTONDOWN`, a series of `WM_MOUSEMOVE` and
`WM_LBUTTONUP` with client coordinates (the AI-panel selection cases already
drive `WM_LBUTTON*` this way; reuse their pattern). Clipboard is read with
`Get-Clipboard -Raw`. Cell geometry comes from the `render.draw` debug event
(`char_w cols x y`), as `helpers_ansi_contrast_floor` does.

- `select_drag_past_bottom_copies_history`: run `seq 1 300`, scroll back
  100 lines with the wheel, press on the first visible row and drag to 40 px
  below the terminal's bottom edge, wait until the view reaches the last
  line, release; the clipboard holds every line from the pressed one to
  `300`, more lines than the window has rows.
- `select_while_scrolled_back_copies_highlight`: `seq 1 300`, wheel back
  100 lines, drag three rows; the clipboard holds the three numbers that
  were on those rows (computed from the offset), not the live rows.
- `click_does_not_touch_clipboard`: set the clipboard to a marker, click
  on a character, the clipboard still holds the marker.
- `ctrl_shift_c_copies_after_drag`: clear the clipboard after the drag's
  auto-copy, press Ctrl+Shift+C, the clipboard holds the text again;
  Edit > Copy likewise.
- `double_click_selects_word`, `triple_click_selects_line`: on `echo
  /usr/local/bin and more`, the clipboard holds `/usr/local/bin` and the
  whole line respectively.
- `scrollback_setting_is_applied`: with `scrollback_lines` 500 in the test
  config, `seq 1 2000`, Select All, the clipboard holds at most 500 plus
  `rows` lines and ends with `2000`.

The existing `ctrl_c_without_selection_interrupts` stays.

## 4. Out of scope, recorded on the notes open list

Shift+click to extend; Alt+drag block selection; middle-button paste; mouse
reporting to applications (modes 1000/1006); the per-byte paste writes over
SSH (`ssh_channel_write` sets socket options per call), which make a large
paste slow; a scrollback-size change applying to open sessions.

## 5. Risks

- `window.c` is over 5,000 lines; the handlers get shorter, not longer, by
  moving the logic to `selection.c`.
- Capture and the auto-scroll timer interact with the existing paste timer
  and animation timer only through distinct ids.
- The triple-click detection by tick stamp is the one piece that cannot be
  tested natively; the harness case covers it.
