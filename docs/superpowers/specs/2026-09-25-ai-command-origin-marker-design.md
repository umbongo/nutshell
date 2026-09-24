# AI command origin marker: telling AI-dispatched rows from typed ones

**Date**: 2026-09-25
**Status**: DRAFT, not yet critiqued
**Branch**: not started; takes the next free patch version when it is
**Asks for**: an external reviewer's "clear UI signifiers for AI-suggested commands vs
user-typed commands".
**Builds on**: `2026-09-23-command-dispatch-states-design.md`,
`2026-09-11-status-policy-control-design.md`, `2026-09-07-design-system-foundation-design.md`.

---

## Problem

`execute_command()` (`src/ui/ai_chat.c:1634-1665`) writes Ctrl+E Ctrl+U, the command and
`\r` through `io->write`. From then on nothing records who typed it: the echo arrives from
the shell like any other output, and in the terminal and its scrollback an AI-dispatched
command is indistinguishable from one the user typed. With `unattended` above NONE
(`src/core/cmd_policy.h`) a command runs with no approval card at all, and scrolling back
later to see what the assistant did means cross-reading the chat panel by hand.

Constraints found in the survey:

- The echo is asynchronous: it comes back from the remote shell (or ConPTY) on a later
  `WM_TIMER` poll (`window.c:3582`), may wrap, may be redrawn by readline or PSReadLine, and
  may never appear. Both the dispatcher and `term_process()` run on the UI thread, so a
  stamp made in `execute_command()` is ordered before any echo byte without locking.
- The dispatcher writes only when `term_at_prompt()` holds, the terminal has been quiet for
  `PROMPT_QUIET_MS` (400 ms) and nothing follows the cursor on its row
  (`dispatch_tick`, `ai_chat.c:1899-1901`). At that instant the cursor row **is** the row
  the echo will land on. `execute_command()` is the only place AI text reaches `io->write`.
- `TermRow` (`src/term/term.h:46-52`) is a pointer, two ints and two bools: six bytes of
  padding on x64. `src/term/buffer.c` and `parser.c` are in the native test build;
  `src/ui` is not.

---

## 1. What is marked, and how it attaches: stamp the cursor row at write time

**Decision**: immediately before the three writes, `execute_command()` calls
`term_mark_origin(d->active_term)`, which stamps the **cursor row** with a fresh origin id.
Rows the echo then spills onto by **auto-wrap** inherit the id. Nothing else is marked:
not the command's output, not the next prompt.

Why this and not matching the echoed text later:

- **Correct in the common case.** The dispatcher's own readiness test guarantees the cursor
  is on an empty input line at a primary prompt, so the echo starts on that row. A long
  command that wraps continues through `term_put_char()`'s auto-wrap, which already flags
  the new row `wrapped`; it copies the origin at the same moment. A prompt that ends exactly
  at the right edge (deferred wrap, `cursor.col == cols`) wraps on the first echoed
  character and is covered by the same rule.
- **Text matching is fragile for no gain.** Syntax highlighting (zsh, fish, PSReadLine)
  re-colours and re-emits the line; readline's horizontal-scroll mode shows a slice; `$`
  expansion, autocorrect and PSReadLine's re-render change the bytes; the echo may be
  split over several polls. A matcher needs a timeout, a state machine and a failure path
  for each of those; the stamp needs none.
- **Harmless when wrong.** The failure modes, all of which leave the mark on the prompt row
  of a command the assistant really did send:
  - no echo (`stty -echo`): the row shows the prompt only, still marked. True statement.
  - the shell wraps by explicit CR LF instead of auto-wrap: only the first row is marked;
    the continuation looks typed. Under-marking, never mis-attribution of a user command.
  - the command clears the screen (`clear`, Ctrl+L): the mark goes with the row's content,
    which is correct (section 3).

Output is deliberately not marked in v1: the question the reviewer asked is who *typed*
the line; bracketing the output needs the end-of-command signal (the next prompt), which
the dispatcher has but the terminal does not. See open question 3.

---

## 2. Where the state lives

In the buffer, one field per row, set only through a core API in `src/term/buffer.c`:

```c
/* term.h */
typedef struct { ...; bool dirty; bool wrapped; uint16_t origin; } TermRow;
/* 0 = no origin (typed, remote output, anything else); 1..65535 = the id of one AI dispatch.
 * Fits the existing padding: sizeof(TermRow) does not change. */

/* In Terminal: */
uint16_t origin_next;       /* next id to hand out; never 0 */

/* Stamp the cursor row of the primary screen with a fresh id and mark it dirty.
 * Returns the id, or 0 (nothing stamped) when term is NULL, the alt screen is active,
 * or the cursor is outside the screen. Ids wrap 65535 -> 1. */
uint16_t term_mark_origin(Terminal *term);

/* The origin id of view row `screen_row` as currently displayed, honouring
 * scrollback_offset (the renderer's get_visible_row() mapping, made const and shared).
 * *run_start / *run_end (either may be NULL) are set to 1 when the view row above /
 * below carries a different id (or is off the view), so a painter can cap each run. */
uint16_t term_view_row_origin(const Terminal *term, int screen_row,
                              int *run_start, int *run_end);
```

An **id rather than a bool** costs nothing (padding) and buys two things: two back-to-back
AI commands whose echoes sit on adjacent rows paint as two bars, not one; and the id is the
key a later link-back to the chat card would use (section 5). `term_row_alloc()` uses
`xmalloc`, so it and `term_row_fill()` both set `origin = 0`.

---

## 3. What keeps a mark and what clears it

| Event | Effect on `origin` | Where |
|---|---|---|
| Auto-wrap onto a new row | new row takes the previous row's id | `term_put_char()` |
| LF / scroll into scrollback | kept: the row pointer moves, the field goes with it | nothing to do |
| Row evicted from the ring, any recycle | cleared | `term_row_fill()` |
| ED 2, and ED 0 / ED 1 on every row they erase *whole* | cleared | `parser.c` `'J'` |
| ED 0 / ED 1 on the cursor row (partial) | kept | |
| EL 0 / EL 1 (partial line erase) | kept: readline's Ctrl+U and redraw use these on the stamped row before the echo arrives | |
| EL 2 (whole line) | cleared | `parser.c` `'K'` |
| ECH, ICH, DCH (cell edits) | kept | |
| IL / DL, region scroll | moved rows keep theirs; rows recycled blank are cleared | `term_scroll_up/down()` via `term_row_fill()` |
| Resize reflow | every new row a logical line produces takes the id of that line's head row, set when the row is started (so a marked row with `len == 0` keeps it) | `term_reflow_buffer()` |
| Alt screen enter / exit | primary rows untouched (saved by pointer); alt rows start at 0; stamping refused while alt is active; reflow of the saved primary buffer carries ids as above | `buffer.c` |
| Selection, copy, `term_extract_*` (AI context, logs) | unaffected: the mark is row metadata, never a cell | none |

Known false positive, accepted: a program that repaints the **primary** screen with cursor
addressing and no whole-row erase (rare; full-screen tools use the alt screen) can leave a
stale bar beside a row it overwrote. See open question 2.

---

## 4. The visual: a bar in the existing left margin

- **Where**: the terminal already has a left margin, `ns_scale(TERM_LEFT_MARGIN, dpi)`
  (6 px at 96 DPI, `window.c:68`), filled with `defaultBg` after `renderer_draw()`
  (`window.c:4181-4184`). The bar is painted there, right after that fill, by a new
  `renderer_draw_origin_gutter(r, hdc, term, gutter_w, y, paintRect, colour, dpi)`. No
  column is taken from the grid, so the terminal's `cols` and the remote PTY size do not
  change, and nothing interacts with the cell-level display-buffer shadow.
- **Shape**: a solid vertical bar `ns_scale(SP_XS, dpi)` wide (4 of the 6 px, leaving a
  2 px gap before the text), flush with the window's left edge, the full row height. A run
  of rows with one id is one continuous bar; `run_start` insets its top by
  `STROKE_HAIRLINE` so adjacent runs read as separate commands.
- **Colour**: `ns_tokens()->info.base`, the intent the command cards already use for
  "running" and the old `[EXEC]` purple, so the terminal and the panel share one visual
  language. No `RGB(` literal; `window.c` passes the token in, `renderer.c` stays
  token-free. Contrast against `terminal_bg` (a non-text graphic, WCAG 1.4.11 wants 3:1):
  Onyx Synapse 4.15, Onyx Light 5.53, Sage & Sand 3.10, Moss & Mist 4.93.
- **Not hue alone**: the signal is presence, position and shape (a bar at the left edge of
  exactly the command's rows), which reads the same in greyscale and to every form of
  colour blindness; the 3:1 floor keeps it visible without the hue. No tint behind the
  text: tinting cells would fight the program's own colours and the selection inversion.
- **Scrolled back**: painted from `term_view_row_origin()`, so it scrolls with its row.
- **Tooltip (in v1)**: hovering the bar shows "Sent by AI Assist". An unexplained stripe is
  not a clear signifier. The hit-test is the margin strip; `WM_MOUSEMOVE` feeds
  `ns_hover` with the view row as id, so only the old and new row's margin are invalidated.

---

## 5. Link-back to the chat card: out of scope for v1

Click-to-scroll the AI panel to the originating card needs an id to (batch, entry, chat
item) map that outlives `cmd_batch_remove()` (the queue is freed when a batch ends), a
conversation clear and a tab switch, plus a card that may no longer exist. That is a design
of its own. v1 keeps the id so the map can be added later without touching the buffer.

---

## 6. Local ConPTY sessions

Same code path: `execute_command()` writes through the session's `io`, whichever backend,
and `term_mark_origin()` works on the `Terminal`. What differs is the bytes coming back.
ConPTY re-renders the child's screen: it opens with `ESC[2J ESC[H` (before any command,
so harmless), positions with CUP and erases with EL 0 / ECH, all of which the table in
section 3 keeps (ECH, like EL 0, is a partial erase). Whether ConPTY lets a long echo
auto-wrap or breaks it with CUP is not verified; if it breaks it, only the first row is
marked, which is the under-marking case of section 1. The larger risk is a full
ConPTY repaint after a resize or a buffer switch using ED 2: that would drop the on-screen
marks (scrollback keeps its own). Open question 1; the test in section 7 replays the
existing recorded ConPTY fixture with a stamp to pin the common case.

---

## 7. Tests

Native, `make test`, in `tests/test_term.c` (plus `runner.c` declarations):

- `term_mark_origin()` stamps the cursor row, marks it dirty and returns a non-zero id;
  successive ids differ; `origin_next = 65535` yields 65535 then 1; NULL returns 0.
- Refused on the alt screen (returns 0, no row changes); a primary mark survives
  `?1049h` / `?1049l`.
- Wrap: 20 columns, `"$ "`, stamp, 45 echoed characters: three rows marked; then `"\r\n"`
  and a 50-character output line: its rows unmarked. Prompt ending exactly at column 20:
  both rows marked.
- Scroll: 100 lines after the stamp; the row keeps its id; with `scrollback_offset` set,
  `term_view_row_origin()` finds it on the right view row. Eviction with a small
  `max_scrollback`: the recycled row reads 0.
- Erase: ED 2 clears; ED 0 clears rows below and keeps the cursor row; EL 0 and EL 1 keep;
  EL 2 clears.
- Resize: narrower (one marked logical line becomes more rows, all marked), wider (rejoins
  to one marked row), a marked empty row keeps its id, and a reflow while the alt screen
  is active keeps the primary buffer's ids.
- Runs: two adjacent stamps give `run_end` / `run_start` between them; one wrapped stamp
  gives one run.
- `term_extract_last_n()` output is byte-identical with and without stamps.

`tests/test_term_conpty.c`: the recorded ConPTY stream up to its prompt, a stamp, then the
recorded echo: the echo row is marked.

`tests/test_ui_theme.c`: for each of the four themes,
`theme_contrast(tokens.info.base, tokens.terminal_bg) >= 3.0`, so a palette edit cannot
quietly make the bar invisible.

Visual: a new `--ui-demo=origin` state, included in `all`. `ui_demo.c` returns, beside the
transcript, the byte offsets at which the win32 side calls `term_mark_origin()` while
feeding it (two adjacent AI commands, one wrapped AI command, typed commands between);
`tests/test_ui_demo.c` checks the offsets fall on those lines' prompts. `ui_gallery` then
screenshots it in all four themes (11 states x 4 themes).

Integration: no new case. The marker is not observable through window messages without a
debug query added for the purpose, and the gallery covers the paint. The AI-tier case
`ai_runs_read_command_unattended` saves a screenshot for eyeballing.

---

## 8. Out of scope

The chat-card link-back (section 5); marking command output; a marker in session log files
or in the terminal text sent to the model; distinguishing approved from unattended runs;
Windows high-contrast mode; a setting to turn the marker off.

---

## Open questions

1. **ConPTY repaints**: does ConPTY ever send ED 2 after the first frame (on resize, `cls`),
   and does it let a long echo auto-wrap or break it with CUP? Measure with `make wintest` before implementation; if
   it does, the marks on screen need re-deriving from scrollback or accepting as lost.
2. **Stale marks on primary-screen repaints**: accept the false positive, or also clear a
   row's id when a printable character lands in column 0 *after* its echo has completed
   (the dispatcher knows when that is)? The naive "column 0 clears" rule breaks readline's
   `\r`-and-reprint redraw of the stamped row.
3. **Mark the output too?** A fainter bracket from the command row down to the row before
   the next prompt would answer "what did the AI's command print". Needs the dispatcher to
   close the range (`term_mark_origin_end()`) when it sees the prompt return.
4. **Unattended vs approved**: should a command that ran without a card look different
   (for example a hollow bar)? Adds a second shape to learn; no evidence yet it is needed.
5. **Tell the model**: annotate AI-origin rows in the context `term_extract_last_n()`
   builds, so the assistant can tell its own commands from the user's? Costs tokens on
   every turn and changes the prompt; a separate decision.
6. **Sage & Sand at 3.10:1** is just over the floor. Accept, or give that theme an
   `info` nudge through `ThemeOverrides` (not a literal in `src/ui`)?
7. **Hit target**: a 6 px margin is a small hover target. Is the tooltip enough, or should
   the README's AI Assist section carry a one-line legend as well?
