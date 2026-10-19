# Character selection in the AI Assist panel

**Date**: 2026-10-18 (revised the same day after two critique rounds)
**Thomas**: *"make sure i can copy and paste text out of any part of the ai assist window.
at the moment i can only copy and paste that which has a copy icon next to it. but when i
try to select text outside of the string that has been provisioned for copy, i can only
select (and copy) the entire paragraph instead of only a string in the paragraph."*
Confirmed 2026-10-18: the clipboard gets the visible text, not the raw markdown; the
automatic copy on mouse-up goes, Ctrl+C and the menu copy.
**Replaces**: the vertical band selection of `src/ui/chat_listview.c` (`sel_start_y` /
`sel_end_y`, whole-item `InvertRect`, raw-markdown copy on mouse-up).
**Builds on**: `2026-10-13-command-detail-and-copy-design.md` (the copy glyphs, whose
raw-source behaviour is unchanged; its §4 left character selection out of scope),
`2026-09-07-design-system-foundation-design.md` (tokens, `rgb_alpha`), the header-only
parser `src/ui/markdown.h` (`md_classify_line`, `md_parse_inline`, `md_next_word`;
`MdSpan.start/end` are byte offsets into the line with the markers excluded).

---

## 1. The change in one paragraph

Selection in the chat panel today is a vertical band with no x coordinate: any item the
band overlaps is inverted whole, and its raw markdown is copied the moment the mouse is
released. The renderer wraps text at paint time and keeps nothing, so a pixel cannot be
mapped to a character. This change splits the problem in two. **Text** is handled with no
geometry: the renderer treats every newline in the source as a hard break and never joins
lines, so the visible text of any byte range is a pure function of the source, computed
in `src/core/` by walking the item exactly as the painter does (same `[EXEC]` segment
split, same line classifier, same inline parser, same list numbering) and tested
natively. **Geometry** is recorded by the paint pass, for the items on screen only: each
word the renderer draws is appended as a run with its rect, its byte range into the
item's text and its cumulative character widths, and the mouse is hit-tested against
those runs by lookup. Selection becomes an anchor and a focus of (message id, part, byte
offset), the selected runs are highlighted, and Ctrl+C, Edit > Copy and the context menu
copy the visible text.

---

## 2. Text: visible-text extraction, no geometry

`src/core/chat_copy.c` (new). `src/ui/markdown.h` moves to `src/core/markdown.h` because
it is header-only and portable; `md_render.c` and `tests/test_markdown_render.c` take
the new path. The painter's `[EXEC]` segment splitter in `chat_listview.c`
(`draw_ai_text_with_exec`) moves to core as `chat_exec_split()` so the copier and the
painter cut the item at the same bytes.

`int chat_copy_visible(const char *text, int len, int start, int end, int markdown,
char **out)` walks the segments of `[start, end)` and, inside each, the source lines,
with the markdown state (fence open, ordered-list counter) reset per segment exactly as
`md_render_core` is run once per segment today. It writes:

- **`[EXEC]` command segment**: the command text, as the painter draws it.
- **prose line**: the line's spans in order (`md_parse_inline`), each span's bytes
  intersected with the range; markers (`**`, `_`, backticks) are never in a span so they
  are never copied. Whitespace inside a span is copied as it is in the source; nothing is
  trimmed, collapsed or inserted.
- **heading**: the content after `content_offset`, span-parsed like the painter, so
  `## **Note**` copies `Note`.
- **list item**: an unordered marker copies as `- `; an ordered marker copies as the
  **displayed** number (`olist_num`, so `1. a\n1. b` copies `1. a\n2. b`), then the
  content, span-parsed. The marker is copied when the range covers any byte of the
  line.
- **blockquote**: the content after the `> ` marker, span-parsed.
- **fenced code**: the body lines verbatim; the fence lines copy as nothing.
- **table row**: cells from `md_table_split_row()` (so `\|` is handled as the painter
  handles it and columns past `MD_TABLE_MAX_COLS` are dropped as they are not drawn),
  span-parsed, joined with a tab; the alignment row copies as nothing.
- **horizontal rule**: nothing but its line break.
- **empty line**: nothing, but the line break is kept, so `p1\n\np2` copies as
  `p1\n\np2`.
- **between lines**: `\n` after every source line but the last in the range. No soft
  wrap exists in the source, so none is ever emitted.
- **`markdown == 0`** (the setting is off, or the item is a user bubble, a status line or
  the Thinking body): the bytes of the range as they are.
- **link syntax**: there is no link span in the parser; links render as `[text](url)`
  and copy the same way. Not changed here.

Across items the caller joins with `\n\n`. The buffer is heap-allocated to the size the
walk needs; items can exceed `AI_MSG_MAX` through `content_overflow`, so there is no
fixed bound and no silent cut, and the stack array in `sel_copy_to_clipboard` goes. Line
ends stay `\n`, as `selection.c` and `clipboard.c` already do for the terminal. Result
goes to `CF_UNICODETEXT` as now. `chat_strip_exec_markers` is removed because the
segment walk replaces it.

**User bubble text is made UTF-8 at the source.** Today the input box's text is taken
with the ANSI `GetWindowText`, so a user item can hold code-page bytes and `\r\n`. The
input is read with `GetWindowTextW` and converted with `WideCharToMultiByte(CP_UTF8)`,
and `\r\n` becomes `\n`, before it becomes an item or goes to the model. Without this
the byte-boundary rules below misread `é` in cp1252 as a three-byte lead.

## 3. Geometry: runs recorded by the paint pass

```c
/* src/core/text_runs.h (new, portable) */
typedef struct {
    int x, y, w, h;      /* list-local pixels, as painted this frame */
    int start, end;      /* byte range into the part's source text, end exclusive */
    int part;            /* TR_PART_MAIN or TR_PART_THINKING */
    int line;            /* paint-time line counter, so runs on one visual line share it */
    int first_cum;       /* index into cum[] of this run's first boundary */
    int n_cum;           /* number of UTF-8 character boundaries in the run + 1 */
    char synthetic[8];   /* "- " or "3. " for a bullet or number, else "" */
} TextRun;

typedef struct {
    TextRun *v; int n, cap;
    int *cum; int cum_n, cum_cap;   /* cumulative pixel width at each boundary */
    int line_count;
} TextRuns;
```

- **Recorded by paint, not measure.** The measure pass skips bullets, lays table cells at
  (0,0) and measures code at full width, so it is not the layout the user sees; the
  paint pass is. `md_render_core` gains an optional `TextRuns *runs` and a `base` byte
  offset; `paint_word`, the code-line, bullet, number, heading, blockquote and table-cell
  draws each append one run with the rect they drew and `base + line offset + span
  offset`, and **one `GetTextExtentExPointW` call per run** fills `cum[]` with the width
  at every UTF-8 character boundary (converted from the UTF-16 result, surrogate pairs
  counted once). That is the whole measuring the feature does: no font index, no
  `HFONT` map, no GDI on mouse events. The callers thread the base down: `chat_exec_
  split()` gives each segment's offset, the per-line `line_buf` copy adds the line's
  offset, `content_offset` is added for headings, lists and quotes, and table cells add
  the cell's offset in the row. One native test per level checks the arithmetic with a
  fake painter.
- **Code lines** are drawn today with one `DrawTextW` per line and no `DT_NOPREFIX`, so
  `cd a && make` paints as `cd a & make`. They move to `TextOutW` per line (no prefix
  handling, no `DT_EXPANDTABS` difference), clipped at the same `text_w`, and record one
  run per line; `cum[]` makes the hit-test on a long line one lookup. Double-click in a
  code run selects the whitespace-delimited token under the pointer, not the line.
- **Every item type.** The user bubble, status and tool items, the Thinking body, the
  `[EXEC]` command text (`draw_text_utf8`) and the plain path when markdown is off are
  drawn today with `DrawTextW` and `DT_WORDBREAK`, which reports no line breaks. They
  move to the renderer's word layouter in a new `MD_PLAIN` mode that skips the parser.
  Two visible differences, **applied to `MD_PLAIN` only** and accepted: leading
  whitespace on a source line is kept (pasted indented code in a user message stays
  indented; wrapped continuation lines are not indented, because the rule applies at a
  source line start, not at `at_line_start` after a soft wrap), and a word wider than
  the line is broken at a character boundary (today it overflows the bubble). Prose
  wrapping is unchanged. The measuring twins change in lockstep: `measure_item`'s user
  and status branches, `build_thinking_layout`, `measure_ai_segment` and
  `measure_ai_text_with_exec` call the same layouter with `paint = 0`, which for
  `MD_PLAIN` is identical to the paint pass by construction, **at the same width**: the
  Thinking measure today uses `body.w` while the paint uses the clip minus `SP_XS` minus
  the scrollbar, and that is corrected so both use the paint width.
- **Thinking.** The body is painted at full height under a clip offset by its own
  scroll; a run is appended only when its rect intersects the clip, so no run lies over
  the header or the main text. `build_thinking_layout` is called on every hover move
  today; its result is cached per (text length, width, collapsed) and rebuilt only when
  one changes, so hover costs no layout.
- **Lifetime.** `TextRuns` lives on `ChatMsgItem`. **Every item's runs are reset at the
  start of `on_paint`**, and the items painted this frame append theirs, so an item
  not painted this frame has `n == 0` and stale rects from an older width cannot
  survive. Before any hit-test that follows a scroll, wheel or resize in the same
  message (edge auto-scroll in `WM_MOUSEMOVE`, the wheel-while-dragging case), the
  list view calls `UpdateWindow` so the runs are current; button-up re-hits once more
  for the same reason. There is no cache key: everything goes through a repaint.
- **The pinned user bubble** at the top records runs from its own paint, in the band's
  coordinates, as a second set on `last_user`; a hit in the band resolves against that
  set, so what the user sees selected is what is copied.

## 4. Hit-test

`int text_runs_hit(const TextRuns *r, int x, int y, int *part_out)` in
`src/core/text_runs.c`, pure, no callbacks:

- The line containing `y` is the run line whose `[y, y+h)` covers it; above the first
  line maps to the first run's `start`, below the last to the last run's `end`.
- On that line: left of the first run maps to its `start`; right of the last run to its
  `end`; between two runs to the end of the left one; inside a run to the nearest
  boundary in `cum[]`, which is always a UTF-8 character boundary. Combining marks and
  emoji sequences may split (the boundaries are code points); recorded as a known limit,
  the same as the terminal's selection.
- A synthetic run (bullet, number) maps to the start of the next real run on its line.
- Thinking runs exist only for the visible part of an expanded box (section 3).
  Dragging beyond the box's edge does not scroll the box (out of scope).

## 5. Selection state and mouse

- **State** (`ChatListView`): `sel_anchor`, `sel_focus`, each `{int msg_id; int part;
  int off;}`; `sel_valid`, `sel_active` keep their meaning; `sel_start_y`/`sel_end_y`
  go. Ids are unique and in list order (`next_id` is never reset by `chat_msg_list_clear`
  and the list is append-only), so an endpoint is resolved by id on every use and the
  selection is cleared when either id no longer resolves (session switch, stream
  cancel). No removal hook: `chat_msg.c` is core and cannot call the view.
- **Button down** (`WM_LBUTTONDOWN`): if `on_lbuttondown` consumed it (a glyph, a
  tickbox, the card buttons, the Details link, the Thinking header), no selection starts
  and the existing selection is cleared, as today. Otherwise the hit sets anchor = focus
  and `SetCapture`. A down with no run under it (item margin, empty space, the gap
  between items) uses the hit rules above, so a drag that starts in the margin works.
- **Drag** (`WM_MOUSEMOVE` with capture): focus follows the hit; edge auto-scroll as
  today, with `UpdateWindow` between the scroll and the hit. Wheel while dragging
  scrolls, updates, and re-hits at the last cursor position. `WM_CAPTURECHANGED` ends
  the drag (`sel_active = 0`).
- **Button up**: re-hits once, ends the drag. A down and up with no movement past the
  4 px threshold clears the selection, unless the down was a triple-click (below).
  **Nothing is copied on mouse-up.**
- **Double-click**: the class has `CS_DBLCLKS` and no handler; `WM_LBUTTONDBLCLK` is
  added. Over a run it selects the word: the run's `[start, end)` trimmed of trailing
  whitespace, or the token under the pointer in a code run. Over a glyph, tickbox or
  button it does **nothing**, which is what the unhandled message does today; it is not
  forwarded to `on_lbuttondown`, so nothing toggles twice.
- **Triple-click**: a `WM_LBUTTONDOWN` within `GetDoubleClickTime()` of a handled
  double-click at the same point is taken before the normal down path: it selects the
  **source line** containing the hit (the paragraph, since every source newline is a
  hard break), sets a flag so the following button-up does not clear it, and takes no
  capture.
- **Keyboard**: Ctrl+C copies the selection when the panel has focus; Ctrl+A selects
  from the first text item's offset 0 to the last text item's end (main parts; Thinking
  text is included only when its box is expanded); Escape clears. **Edit > Copy**
  (`window.c` `do_copy`) first asks `ai_chat_copy_selection()`, which copies and
  returns 1 only when the list view **has keyboard focus** and `sel_valid`; otherwise
  `do_copy` continues to the terminal path, and that call sits before the no-session
  early return so a panel without a session still copies. The context menu keeps Copy
  and Select all, and a right-click does not clear the selection.
- **Command cards and rows** between two text items: a selection spanning them skips
  them in the copy; their text is copied through their glyph. No endpoint can land on a
  card: cards record no runs and a hit in their band resolves to the nearest text
  item's edge.
- **Streaming**: offsets are into the source text, so an append to the item being
  selected keeps the selection valid; the next paint re-records the runs. Offsets past
  the current length (after a replace) are clamped.

## 6. Highlight

Drawn by the paint pass **immediately before each run's `TextOutW`**, after any
background that run's line draws (the code-line `FillRect`, the inline-code background
in `paint_word`, the table-header fill), so it is never painted over. The fill is
`rgb_alpha(tokens->focus, bg, 30%)` where `bg` is the colour the run is actually on,
passed down by the caller: the panel surface for AI prose, the user bubble surface for
user text, the code background for code, the table-header fill for header cells, the
Thinking surface for Thinking. The selected portion of a run comes from `cum[]`; a run
fully inside the range is filled whole, and on a line fully inside the range the fill
extends from the last run to the content's right edge, as in a browser. The item's
`measured_height` is unchanged by selection, so no layout runs.

`--ui-demo=chat` gains a selected range across a user bubble, a markdown reply with a
list, a code block and a table, an `[EXEC]` segment, and an expanded Thinking body; the
maintainer reviews that screenshot in the four themes (the notes' "mockups before
choosing" rule for UI work) before the PR leaves draft, and the gallery captures it.

## 7. Tests

Native (`make test`), new `tests/test_chat_copy.c` and `tests/test_text_runs.c`:
- `chat_copy_visible`: every line kind of section 2 with a range inside one line, across
  a line break, across an empty line, across a bullet (marker included and excluded by
  the range), across a fence, a table row with an escaped pipe and more than
  `MD_TABLE_MAX_COLS` cells, a renumbered ordered list, a heading with inline markers, a
  horizontal rule, an `[EXEC]` segment inside the range and the parser state reset
  across the segment boundary (`` ```bash\n[EXEC]ls[/EXEC]\n```\nDone. ``), and the
  whole of a fixture against a golden string; `markdown == 0` passes bytes through; a
  range starting inside a multi-byte sequence is widened to the character; the join
  across items.
- `text_runs_hit`: every rule of section 4 over hand-built runs with `cum[]` tables
  that include multi-byte characters; left/right of a line, between runs, synthetic
  skip, Thinking part gating, above/below.
- Base-offset arithmetic: a fake painter appends runs for a message with an `[EXEC]`
  segment, a heading, a list and a table, and every run's bytes equal the word it was
  told to draw.
- **Painted words equal copied text**: the same fake-painter walk concatenates the
  bytes of the runs it recorded per source line and compares them with
  `chat_copy_visible` over that line's range; the two walkers must agree on every
  fixture. This is the test that keeps section 2 and section 3 from diverging.

What the native suite cannot see, and what covers it:
- The real GDI renderer: a **`make wintest` case** (`md_render.c`, `md_table.c`,
  `ns_draw.c` and the theme files join `WIN_TEST_SRCS`) creates a hidden chat list
  view window, loads the fixture, paints to a memory DC at two widths and two DPIs,
  then checks that every recorded run's bytes are drawn where `cum[]` says (hit-testing
  each run's centre returns an offset inside it), that the measured height of every
  item equals its painted height (user, status, Thinking and AI items alike, through
  the window so the static functions are exercised), and that `cum[]` is monotonic.
  `make wintest` is manual, not in CI; the PR records its run.
- The highlight on the five surfaces: the demo screenshot of section 6.
- The harness, gate tier, three cases, run on a locked desktop like every other posted-
  input case (physical mouse movement during a captured drag would rewrite the focus):
  `ai_panel_select_copy` asks the app for the run rects through a `--ui-demo` query
  message (no hardcoded coordinates), posts down, moves and up over a known word, sends
  Ctrl+C through `Send-NutshellChord`, and compares the clipboard **exactly**;
  `ai_panel_select_across_items` drags from the user bubble into the reply across an
  `[EXEC]` segment and checks the joined text; `ai_panel_select_scrolls` drags past the
  bottom edge so the view auto-scrolls and checks the selection reached the next item.
  A fourth checks Edit > Copy with the panel focused versus the terminal focused.

## 8. Out of scope

A caret and keyboard-driven selection (Shift+arrows); drag-and-drop; rich text or HTML
on the clipboard; selecting inside command rows and the approval card; right-to-left
text; grapheme-cluster-safe splitting of combining sequences; scrolling the Thinking
box by dragging past its edge; a link span in the parser; changes to how prose wraps;
DPI-scaling the renderer's `MD_*` spacing constants (the `md_render.c` DPI item in the
notes, parked in sub-project 3, not needed here because hit-testing uses recorded rects).

## Critique (2026-10-18, two rounds, Opus)

**Round 1**, fifteen findings:

1. Offsets need a base at every level; Thinking is a second string. **Accepted**.
2. Cheaper design: every newline is a hard break, so extraction needs no layout, and
   geometry is only needed on screen. **Accepted**: the spec is rebuilt on it.
3. The measure pass is not the paint pass. **Accepted**: runs recorded by paint.
4. A highlight drawn before the text is painted over; `surface.base` undefined.
   **Accepted**: per run, on its real background.
5. The plain items change look on the layouter; twins unlisted. **Accepted**.
6. DPI-scaling `MD_*` is scope creep. **Accepted**: removed.
7. Selection does not survive items being freed. **Accepted**: by message id.
8. Font model too coarse. **Accepted**; superseded by round 2, item 6.
9. `synthetic` pointed at stack memory. **Accepted**: owned.
10. Extraction rules conflicted with the tokenizer. **Accepted**: source-line based.
11. CRLF claim false; 256 KB on the stack; silent cut. **Accepted**.
12. Mouse gaps. **Accepted** except Thinking-edge scrolling, out of scope.
13. Cache key incomplete. **Accepted**: no cache.
14. Tests miss UTF-16 mapping, measure/paint, fonts, hidden highlight, coordinates.
    **Accepted**.
15. Minor (header path, no link span, `window.c` handle, dead helper, no mockup, card
    endpoints). **Accepted**.

**Round 2**, thirteen findings on the revised spec:

1. `[EXEC]` unhandled: markers copied verbatim, command text records no runs, parser
   state reset per segment not matched. **Accepted**: `chat_exec_split()` in core, the
   copier walks segments with the state reset, command text on the layouter.
2. Runs not current after an auto-scroll, wheel or resize; off-screen items keep stale
   runs. **Accepted**: all runs reset at `on_paint` start, `UpdateWindow` before a
   hit that follows a scroll, button-up re-hits.
3. Thinking runs overlap other content; its measure and paint widths differ.
   **Accepted**: runs only inside the clip; one width.
4. Code lines are one `DrawTextW` with prefix handling; cost and double-click wrong.
   **Accepted**: `TextOutW` per line, `cum[]` lookup, token double-click.
5. Copy rules diverge from paint (headings and cells not span-parsed, escaped pipes,
   column cap, list renumbering, rules, `md_parse_spans` name, no cross-check).
   **Accepted**: rules aligned with the painter, native painted-equals-copied test.
6. Font list incomplete; cheaper to record cumulative widths at paint. **Accepted**:
   `cum[]` per run, no font index, no measurer callback.
7. User bubble text is ANSI with `\r\n`. **Accepted**: UTF-8 at the source.
8. Triple-click down path clears its own selection; double-click forwarding would
   toggle twice. **Accepted**: taken before the normal path; no forwarding.
9. Pinned bubble hits go to hidden text. **Accepted**: resolve to the pinned item's
   own runs.
10. Edit > Copy ignores focus; `do_copy` early return. **Accepted**: focus checked in
    the API, call before the return.
11. Unrequested prose layout changes; Thinking layout on every hover. **Accepted**:
    `MD_PLAIN` only; Thinking layout cached.
12. The removal hook cannot live in core and is unnecessary; the 1 MB bound is wrong.
    **Accepted**: resolve by id on use; no bound.
13. `wintest` manual and incomplete; statics unreachable; harness misses cross-item,
    `[EXEC]`, scrolling; physical mouse flakiness. **Accepted**: list-view window in
    `wintest`, three harness cases, locked-desktop note.

Not verified by the critics and settled here: `DrawText` without `DT_NOCLIP` clips at
the rect, which `TextOutW` with a clip region reproduces; `Send-NutshellChord` posts to
the window that has focus after the harness's posted click, as the terminal cases
already rely on; the table-header fill is listed as a highlight surface; the user bubble
measure and paint widths come from the same layout call at every panel width.

## Implementation notes (2026-10-18)

What the plan and the reviews changed from the text above, one line each. The
sections above are left as written; where they disagree, this list wins.

- A list marker (bullet or number) is copied only when the selected range covers
  its bytes; a range that starts inside the item's text does not gain a marker.
- The user-bubble highlight hue is the bubble's text colour, through
  `ui_theme_sel_hue()`; the focus tint is invisible on the accent bubble.
- Leading whitespace at the start of a markdown line's content is not copied;
  plain mode (no markdown) keeps it.
- A table's column count follows what the painter draws, through
  `md_table_drawn_cols()`, so the copy and the paint cannot disagree on a ragged
  row.
- `chat_copy_visible` takes a flags word (`CHAT_COPY_MD`, `CHAT_COPY_EXEC`), not a
  markdown boolean.
- `TextRuns` gained a `cum_byte[]` array (byte offset at each recorded boundary)
  and `TextRun` a `flags` field.
- Fence lines and a table's alignment row copy as nothing, including their line
  break.
- Every newline byte in range copies as `\n`.
- The Thinking layout cache key is (text length, width, line height).
- Table cells are not filled to the right edge of the table.
- The harness gained `-Hwnd` on `Send-NutshellChord` and `Set-NutshellFocus`.
- The input box is still an ANSI EDIT: `GetWindowTextW` fixes the cp1252 bytes but
  cannot admit characters outside the code page.
- The old 2,047-byte cut on the input text is gone; the only cap is the EDIT's own.
- The `[EXEC]` split lives in `src/core/chat_exec.c`, shared by the painter and the
  copier.
- The glue that resolves a hit across items stays in `src/ui`, not in core.
