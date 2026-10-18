# Character selection in the AI Assist panel

**Date**: 2026-10-18 (revised the same day after one critique round)
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
parser `src/ui/markdown.h` (`md_classify_line`, `md_parse_spans`, `md_next_word`;
`MdSpan.start/end` are byte offsets into the line with the markers excluded).

---

## 1. The change in one paragraph

Selection in the chat panel today is a vertical band with no x coordinate: any item the
band overlaps is inverted whole, and its raw markdown is copied the moment the mouse is
released. The renderer wraps text at paint time and keeps nothing, so a pixel cannot be
mapped to a character. This change splits the problem in two. **Text** is handled with no
geometry at all: the renderer treats every newline in the source as a hard break and never
joins lines, so the visible text of any byte range is the source with the markdown markers
stripped line by line, a pure function in `src/core/` tested natively. **Geometry** is
recorded by the paint pass, for the items on screen only: each word the renderer draws is
appended as a run with its rect and its byte range into the item's text, and the mouse is
hit-tested against those runs. Selection becomes an anchor and a focus of (message id,
part, byte offset), the selected runs are highlighted, and Ctrl+C, Edit > Copy and the
context menu copy the visible text.

---

## 2. Text: visible-text extraction, no geometry

`src/core/chat_copy.c` (new; `src/ui/markdown.h` moves to `src/core/markdown.h` because
it is header-only and portable, and `md_render.c` and `tests/test_markdown_render.c` take
the new path).

`int chat_copy_visible(const char *text, int start, int end, int markdown, char **out)`
walks the source lines that intersect `[start, end)` and writes:

- **prose line**: the line's spans in order (`md_parse_spans`), each span's bytes
  intersected with the range; markers (`**`, `_`, backticks) are never in a span so they
  are never copied. Whitespace inside a span is copied as it is in the source; nothing is
  trimmed, collapsed or inserted. Leading indentation of the source line is copied.
- **heading**: the text after `content_offset` (the `#`s and the space are dropped).
- **list item**: the source marker as written, `- `, `* ` or `3. `, then the content; the
  marker is copied when the range covers any byte of the line.
- **blockquote**: the content after the `> ` marker.
- **fenced code**: the body lines verbatim; the fence lines themselves copy as nothing.
- **table row**: the cells with `|` replaced by a tab, the alignment row dropped.
- **empty line**: nothing, but the line break below is kept, so `p1\n\np2` copies as
  `p1\n\np2`.
- **between lines**: `\n` after every source line but the last in the range. No soft wrap
  exists in the source, so none is ever emitted.
- **`markdown == 0`** (the setting is off, or the item is a user bubble, a status line or
  the Thinking body): the bytes of the range as they are.
- **link syntax**: there is no link span in the parser, links render as `[text](url)`
  and copy the same way. Not changed here.

Across items the caller joins with `\n\n`. The buffer is heap-allocated to the size the
walk needs (`AI_MAX_MESSAGES * AI_MSG_MAX` is 1 MB plus Thinking text, so never a stack
array; the existing `char buf[COPY_CAP+4]` in `sel_copy_to_clipboard` goes). Line ends
stay `\n`, as `selection.c` and `clipboard.c` already do for the terminal; nothing
converts to CRLF today and nothing will. Result goes to `CF_UNICODETEXT` as now.
`chat_strip_exec_markers` is removed with the band copy.

## 3. Geometry: runs recorded by the paint pass

```c
/* src/core/text_runs.h (new, portable) */
typedef struct {
    int x, y, w, h;      /* item-local pixels, as painted */
    int start, end;      /* byte range into the part's source text, end exclusive */
    int part;            /* TR_PART_MAIN or TR_PART_THINKING */
    int font;            /* index the UI side maps to the HFONT it drew with */
    int line;            /* paint-time line counter, so runs on one visual line share it */
    char synthetic[8];   /* "• " or "3. " for a bullet or number, else "" */
} TextRun;

typedef struct { TextRun *v; int n, cap; int line_count; } TextRuns;
```

- **Recorded by paint, not measure.** The measure pass skips bullets, lays table cells at
  (0,0) and measures code at full width (`md_render_core` with `paint = 0`), so it is not
  the layout the user sees. The paint pass is. `md_render_core` gains an optional
  `TextRuns *runs` and a `base` byte offset; `paint_word`, the code-line, bullet, number,
  heading, blockquote and table-cell draws each append one run with the rect they drew
  and `base + line offset + span offset`. The callers thread the base down: `draw_ai_
  text_with_exec` passes each segment's offset in the item text (it strips a trailing
  newline from its malloc'd copy, which does not move the offsets of what remains), the
  per-line `line_buf` copy adds the line's offset, `content_offset` is added for headings,
  lists and quotes, and table cells add the cell's offset in the row. One native test
  per level checks the arithmetic with a fake painter.
- **Every item type.** The user bubble, status and tool items, the Thinking body and the
  plain path when markdown is off are drawn today with `DrawTextW` and `DT_WORDBREAK`,
  which reports no line breaks. They move to the renderer's word layouter in a new
  `MD_PLAIN` mode that skips the parser. Two visible differences, both accepted and
  both also applied to prose so the panel wraps one way: leading whitespace on a line is
  **kept** (today `render_inline_spans` drops it at `at_line_start`; pasted indented code
  in a user message stays indented), and a word wider than the line is **broken at a
  character boundary** (today it overflows the bubble; a long URL will now wrap). The
  measuring twins change in lockstep: `measure_item`'s user and status branches,
  `build_thinking_layout`'s `DT_CALCRECT`, `measure_ai_segment` and
  `measure_ai_text_with_exec` all call the same layouter with `paint = 0`, which for
  `MD_PLAIN` is identical to the paint pass by construction. `hSmallFont` for status items
  is one of the font indices.
- **Lifetime.** `TextRuns` lives on `ChatMsgItem`, is reset at the start of every paint of
  that item and holds only what that paint drew. An item not painted this frame has
  `n == 0`. There is no cache key: the markdown toggle, Thinking collapse, width, DPI,
  glyph reserve and streaming all go through a repaint and the runs follow. Memory is one
  run per drawn word of the on-screen items, freed with the item.
- **Fonts.** The index enumerates what the renderer really uses: body, bold, italic,
  bold-italic, mono, h1, h2, h3 (bold or italic inside a heading uses the heading font,
  as today), small. The UI side keeps the `HFONT` for each index for the hit-test's prefix
  measurement.

## 4. Hit-test

`int text_runs_hit(const TextRuns *r, int x, int y, TrMeasure measure, void *ctx, int
*part_out)` in `src/core/text_runs.c`, pure; `measure(ctx, font, text, nbytes)` returns
the pixel width of a UTF-8 prefix, implemented on the UI side with
`GetTextExtentPoint32W` on the converted prefix (one call per candidate boundary; a run
is at most one word so the cost per `WM_MOUSEMOVE` is a handful of calls, measured in
`make wintest` and recorded in the PR).

- The line containing `y` is the run line whose `[y, y+h)` covers it; above the first
  line maps to the first run's `start`, below the last to the last run's `end`.
- On that line: left of the first run maps to its `start`; right of the last run to its
  `end`; between two runs to the end of the left one; inside a run to the nearest
  **UTF-8 character boundary** by prefix width, never inside a multi-byte sequence.
  Combining marks and emoji sequences may split (the measurer works per UTF-16 unit);
  recorded as a known limit, same as the terminal's selection.
- A synthetic run (bullet, number) maps to the start of the next real run on its line.
- A run of the Thinking part is hit only while the Thinking box is expanded; its rects
  are recorded after the box's own scroll offset is applied, so no adjustment is needed.
  Dragging beyond the box's edge does not scroll the box (out of scope).
- The sticky pinned user message is painted into a band at the top; hits in that band
  resolve against the item at that content Y, not the pinned copy, which records no runs.

## 5. Selection state and mouse

- **State** (`ChatListView`): `sel_anchor`, `sel_focus`, each `{int msg_id; int part;
  int off;}`; `sel_valid`, `sel_active` keep their meaning; `sel_start_y`/`sel_end_y`
  go. Anchoring by message id, not index or pointer: `chat_msg_list_clear` (session
  switch via `chat_rebuild_display`), `chat_msg_remove` (stream cancel) and every other
  removal call `sel_on_item_removed(id)`, which clears the selection if either end names
  that id. An id that no longer resolves clears on the next use as well.
- **Button down** (`WM_LBUTTONDOWN`): if `on_lbuttondown` consumed it (a glyph, a
  tickbox, the card buttons, the Details link, the Thinking header), no selection starts
  and the existing selection is cleared, as today. Otherwise the hit sets anchor = focus
  and `SetCapture`. A down with no run under it (item margin, empty space) uses the hit
  rules above, so a drag that starts in the margin works.
- **Drag** (`WM_MOUSEMOVE` with capture): focus follows the hit; edge auto-scroll as
  today. **Wheel while dragging** recomputes the focus from the last cursor position
  after the scroll. `WM_CAPTURECHANGED` ends the drag (`sel_active = 0`) so capture lost
  to a dialog cannot leave it stuck.
- **Button up**: ends the drag. A down and up with no movement past the 4 px threshold
  clears the selection. **Nothing is copied on mouse-up.**
- **Double-click**: the class has `CS_DBLCLKS` and no handler; `WM_LBUTTONDBLCLK` is
  added. Over a run it selects the word: the run's `[start, end)` trimmed of trailing
  whitespace. Over a glyph or button it behaves as a second click would today.
- **Triple-click**: a `WM_LBUTTONDOWN` within `GetDoubleClickTime()` of a handled
  double-click at the same point selects the **source line** containing the hit (the
  paragraph, since every source newline is a hard break). Win32 has no triple-click
  message; the list view keeps the time and point of the last double-click.
- **Keyboard**: Ctrl+C copies the selection when the panel has focus; Ctrl+A selects
  from the first text item's offset 0 to the last text item's end (main parts; Thinking
  text is included only when its box is expanded); Escape clears. **Edit > Copy**
  (`window.c` `do_copy`) asks the panel through a new `ai_chat_copy_selection()` and
  falls back to the terminal selection when it returns 0; `window.c` has no handle to
  the list view, so the call goes through `ai_chat`. The context menu keeps Copy and
  Select all.
- **Command cards and rows** between two text items: a selection spanning them skips
  them in the copy; their text is copied through their glyph. No endpoint can land on
  a card, because cards record no runs and a hit in their band resolves to the nearest
  text item's edge.
- **Streaming**: offsets are into the source text, so an append to the item being
  selected keeps the selection valid; the next paint re-records the runs. Offsets past
  the current length (after a replace) are clamped.

## 6. Highlight

Drawn by the paint pass **immediately before each run's `TextOutW`/`DrawTextW`**, after
any background that run's line draws (the code-line `FillRect`, the inline-code
background in `paint_word`), so it is never painted over. The fill is
`rgb_alpha(tokens->focus, bg, 30%)` where `bg` is the colour the run is actually on,
passed down by the caller: the panel surface for AI prose, the user bubble surface for
user text, the code background for code, the Thinking surface for Thinking. The
selected portion of a run is computed from its byte range and the prefix widths; a run
fully inside the range is filled whole, and on a line fully inside the range the fill
extends from the last run to the content's right edge, as in a browser. The item's
`measured_height` is unchanged by selection, so no layout runs.

`--ui-demo=chat` gains a selected range across a user bubble, a markdown reply with a
list and a code block, and an expanded Thinking body; the maintainer reviews that
screenshot in the four themes (the notes' "mockups before choosing" rule for UI work)
before the PR leaves draft, and the gallery captures it.

## 7. Tests

Native (`make test`), new `tests/test_chat_copy.c` and `tests/test_text_runs.c`:
- `chat_copy_visible`: every line kind of section 2 with a range inside one line, across
  a line break, across an empty line, across a bullet (marker included and excluded by
  the range), across a fence, a table row, and the whole of a fixture with headings,
  bold, a list, a fenced block and a table against a golden string; `markdown == 0`
  passes bytes through; a range starting inside a multi-byte sequence is widened to the
  character; the join across items.
- `text_runs_hit`: every rule of section 4 with a fake measurer whose advance is per
  UTF-8 character (so a hit never lands inside a sequence); left/right of a line,
  between runs, synthetic skip, Thinking part gating, above/below.
- Base-offset arithmetic: a fake painter appends runs for a message with an `[EXEC]`
  segment, a heading, a list and a table, and every run's bytes equal the word it was
  told to draw.

What the native suite cannot see, and what covers it:
- Measure/paint agreement and the wrong-font risk: a **`make wintest` case** renders a
  fixture with the real GDI renderer at two widths and two DPIs, records the runs, then
  checks that hit-testing each run's centre returns an offset inside that run and that
  the item's measured height equals the painted height. This replaces visual-only
  verification for the layouter.
- The highlight under code backgrounds and on the four surfaces: the demo screenshot of
  section 6.
- The harness, gate tier, `ai_panel_select_copy`: it asks the app for the run rects
  through a new `--ui-demo` query message (no hardcoded coordinates; the harness README
  calls those a desktop dependency), posts down, moves and up over a known word, sends
  Ctrl+C through `Send-NutshellChord`, and compares the clipboard **exactly** to the
  expected string including separators. A second case checks Edit > Copy with the panel
  focused versus the terminal focused.

## 8. Out of scope

A caret and keyboard-driven selection (Shift+arrows); drag-and-drop; rich text or HTML
on the clipboard; selecting inside command rows and the approval card; right-to-left
text; grapheme-cluster-safe splitting of combining sequences; scrolling the Thinking
box by dragging past its edge; a link span in the parser; DPI-scaling the renderer's
`MD_*` spacing constants (that is the `md_render.c` DPI item in the notes, parked in
sub-project 3, and it is not needed here because hit-testing uses the recorded rects).

## Critique (2026-10-18, one round, Opus)

Fifteen findings; dispositions:

1. The renderer never sees the item's text, so "offsets into the item" needs a base at
   every level, and Thinking is a second string. **Accepted**: `base` threaded through
   every level (section 3), `part` added to the anchor.
2. Cheaper design: every newline is a hard break, so extraction needs no layout, and
   geometry is only needed on screen. **Accepted**: the spec is rebuilt on it; the
   measure-time cache, memory estimate and eviction rule are gone.
3. The measure pass is not the paint pass; the old spec needed two layouts to agree.
   **Accepted**: runs are recorded by paint; measure/paint agreement is checked by a
   `wintest` case instead of assumed.
4. A highlight drawn before the text is painted over by the code backgrounds, and
   `surface.base` is undefined across four surfaces. **Accepted**: drawn per run just
   before the glyphs, with the run's real background passed down.
5. Routing the plain items through the layouter changes how they look, and the
   measuring twins were not listed. **Accepted**: the two differences are stated and
   applied everywhere; the twins are listed and share one code path.
6. DPI-scaling the `MD_*` constants is scope creep on a false premise. **Accepted**:
   removed, left parked in sub-project 3.
7. The selection does not survive items being freed; `chat_msg_update` does not exist.
   **Accepted**: anchor by message id, cleared on removal.
8. The font model is too coarse. **Accepted**: the real set enumerated; heights are not
   needed in core because rects come from paint.
9. `synthetic` pointed at stack memory. **Accepted**: owned `char[8]`.
10. Extraction rules conflicted with the tokenizer (trailing whitespace, empty lines,
    no hard-break flag, triple-click merging kinds). **Accepted**: extraction works on
    source lines, not runs; empty lines preserved; triple-click is the source line.
11. CRLF claim false; 256 KB on the stack; silent cut. **Accepted**: LF, heap sized to
    need, no cap.
12. Mouse gaps: no `WM_LBUTTONDBLCLK` handler, triple-click timing, wheel mid-drag,
    `WM_CAPTURECHANGED`, Thinking autoscroll. **Accepted** except Thinking-edge
    scrolling, which is out of scope and said so.
13. The cache key was incomplete. **Accepted** by removing the cache: runs are
    re-recorded on every paint.
14. The named tests miss UTF-16 mapping, measure/paint disagreement, wrong fonts, hidden
    highlight, hardcoded harness coordinates, substring checks. **Accepted**: character-
    boundary rule and test, `wintest` round-trip, demo screenshot, a geometry query for
    the harness, exact clipboard comparison.
15. Minor: `markdown.h` path, no link span, `window.c` has no list-view handle, dead
    `chat_strip_exec_markers`, no mockup, card endpoints undefined. **Accepted**: the
    header moves to `src/core/`, links copy as source, `ai_chat_copy_selection()`, the
    helper is removed, the demo screenshot stands in for the mockup, cards record no runs.

Not verified by the critic and settled here: `DT_WORDBREAK` does break an overlong
word mid-word on Windows (`DT_WORDBREAK` without `DT_EDITCONTROL` breaks at the
character that overflows), so breaking overlong words in the layouter keeps today's
behaviour for the plain items and improves prose. GDI font fallback for CJK and emoji
is the same in `TextOutW` and `GetTextExtentPoint32W` for the same `HFONT`. Posted
mouse messages with `SetCapture` are what the harness already uses for the terminal's
selection case. `WM_DPICHANGED` reaches the panel through the parent's `ns_font_flush`
and a full repaint, which re-records the runs.
