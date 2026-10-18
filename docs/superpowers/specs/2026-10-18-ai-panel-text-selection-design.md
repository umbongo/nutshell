# Character selection in the AI Assist panel

**Date**: 2026-10-18
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
`2026-09-07-design-system-foundation-design.md` (tokens, `ns_hover`, `ns_scale`),
`src/core/markdown.h` (`MdSpan.start/end` are byte offsets into the source line with the
markers excluded).

---

## 1. The change in one paragraph

Selection in the chat panel today is a vertical band with no x coordinate: any item the
band overlaps is inverted whole, and its raw markdown is copied the moment the mouse is
released. The renderer wraps text at paint time and keeps nothing, so a pixel cannot be
mapped to a character. This change makes the renderer retain a per-message **text layout**
(lines of runs, each run a rect plus a byte range into the message text), moves every
piece of text the panel draws onto that one layouter, replaces the band with an anchor and
focus of (item, byte offset), highlights the selected runs, and copies the visible text
with Ctrl+C, Edit > Copy or the context menu.

---

## 2. The retained layout

```c
/* src/core/text_layout.h (new, portable) */
typedef struct {
    int   x, y, w, h;        /* rect in item-local pixels */
    int   start, end;        /* byte range into the item's source text, end exclusive */
    int   font;              /* TlFont: body, bold, italic, mono, heading */
    int   flags;             /* TL_RUN_SYNTHETIC: text not in the source (bullet, number) */
    const char *synthetic;   /* the visible text when TL_RUN_SYNTHETIC, else NULL */
} TlRun;

typedef struct {
    int y, h;                /* line box */
    int first_run, run_count;
} TlLine;

typedef struct {
    TlLine *lines; int line_count, line_cap;
    TlRun  *runs;  int run_count,  run_cap;
    int width, dpi;          /* the inputs it was built for */
} TextLayout;
```

- **Who builds it.** `md_render_core()` (`src/ui/md_render.c`) already walks words with
  their x, y, width and source offsets in `render_inline_spans()`; it gains an optional
  `TextLayout *out` and appends a run per word (or per word fragment when a span boundary
  falls inside a word), one line per wrapped line, the same opt-in pattern as `MdBlockOut`.
  Code lines, bullets, list numbers and table cells, which use `DrawTextW` today, append
  their runs too; bullets and numbers are `TL_RUN_SYNTHETIC`. Headings, blockquotes and
  horizontal rules follow the same rule: every visible glyph belongs to a run.
- **Pure core.** `src/core/text_layout.c` holds the growable containers, the hit-test
  (`tl_hit(layout, x, y)` → byte offset, with the rules of section 3), range extraction
  (`tl_extract(layout, text, a, b, buf)` → visible text) and `tl_offset_rect()` for the
  caret-less highlight. It has no GDI; the width of a string is measured through a
  callback `int (*measure)(void *ctx, const char *s, int len, int font)` so the native
  tests use a fixed-advance fake. The per-character x inside a run comes from the same
  callback, called on prefixes, with a cache in `TlRun` of the prefix widths at every
  byte boundary when the run is hit-tested the first time. (`GetTextExtentExPointW` in
  the GDI measurer gives all of them in one call.)
- **One layouter for all text.** The user bubble, status and tool items, the Thinking
  body and the plain path when markdown is off go through `md_render` with a
  `MD_PLAIN` flag that skips the parser, so their wrapping and runs are the same as
  prose. The `DrawTextW` + `DT_WORDBREAK` calls in `chat_listview.c` (user bubble ~1259,
  Thinking ~1647, status ~2177, EXEC text ~1367/1446/1474) are removed. Command rows and
  the approval card are not text layouts: their text is copied through their glyph, and
  a click on them keeps opening the details popup.
- **When it is built.** In `measure_item()`, which already runs the renderer with
  `paint = 0`, so items above and below the viewport have a layout. It is cached on
  `ChatMsgItem` and rebuilt when the item is dirty (streaming appends), the panel width
  changes or the DPI changes; the `width`/`dpi` fields are the cache key. Paint does not
  rebuild it; paint uses it only to draw the highlight. The `MD_*` spacing constants in
  `md_render.c` (lines 40-49) are passed through `ns_scale` so measure and paint agree
  at every DPI; this is the `md_render.c` DPI item in the notes' Open list, done here
  because the hit-test cannot be right without it.
- **Memory.** One run per word; a 32 KB message is about 6,000 runs of 40 bytes, 240 KB,
  per item. Layouts of items more than 200 messages away from the viewport are freed and
  rebuilt on demand; `measure_item` already runs on scroll.

## 3. Selection

- **State** (`ChatListView`): `sel_anchor` and `sel_focus`, each `{int item; int off;}`
  with `off` a byte offset into that item's source text (`-1` on an item with no layout,
  such as a command card: the offset then means "the whole item is excluded"). `sel_valid`
  and `sel_active` keep their meaning; `sel_start_y`/`sel_end_y` go.
- **Mouse.** Button down over a text run sets anchor = focus = hit; down in an item's
  margin maps to the nearest line end; down over a glyph or button keeps its current
  behaviour and starts no selection. Drag moves the focus with the existing edge
  auto-scroll. A drag below the last item selects to the end of the last text item; above
  the first, to its start. Double-click selects the word under the pointer (run bounds,
  then trimmed at Unicode space/punctuation); triple-click selects the paragraph (the
  runs between two line boxes whose gap exceeds the body line height). A click without
  a drag clears the selection. `SetCapture` as now.
- **Hit-test rules** (`tl_hit`): a point left of a line's first run maps to that run's
  start; right of its last run to its end; between runs to the end of the left run;
  inside a run to the nearest character boundary by prefix width; above the first line
  to offset 0; below the last line to the end. Synthetic runs (bullets) are not hit;
  a point on one maps to the start of the next real run on the line.
- **Highlight.** Paint fills, per selected line, the union of the selected runs' rects
  (from `tl_offset_rect`) with `rgb_alpha(tokens->focus, surface.base, 30%)` before the
  text is drawn, through the renderer's `paint` pass (a `TlRange` is passed in, the
  renderer fills the rect behind each run inside it). A full-line selection extends to
  the line's right edge, as in a browser. The Thinking body's own scroll offset is
  applied to its runs; a collapsed Thinking box is not selectable and its text is not
  copied. The sticky pinned user message at the top is painted but never hit-tested:
  hits in its band go to the item at that content Y.
- **Keyboard.** Ctrl+C copies the selection when the panel has focus; Ctrl+A selects
  from the first text item's offset 0 to the last's end; Escape clears. Edit > Copy
  (`window.c` `do_copy`) copies the panel selection when the panel has focus and
  `sel_valid`, otherwise the terminal selection as now. The context menu keeps Copy and
  Select all. Nothing is copied on mouse-up.
- **Streaming.** Offsets are into the source text, so an append to the item being
  selected keeps the selection valid; the rebuilt layout re-derives the rects. If the
  item's text is replaced rather than appended (`chat_msg_update` on a redraw after a
  session switch), offsets past the new length are clamped to it.

## 4. What goes on the clipboard

`tl_extract` walks the lines of each item in the range and writes:

- real runs: the source bytes `[start, end)` of the run, intersected with the range;
- synthetic runs: their visible text (`"• "`, `"3. "`) when the range covers any part of
  the line they start;
- between runs on a line: one space where the layouter broke at a space, nothing where it
  broke inside a word (a code line that overflowed);
- between lines: `\n` when the break is a hard break in the source or a block boundary,
  a space when it is a soft wrap of the same paragraph, so a wrapped paragraph pastes as
  one line as it does from a browser;
- between items: `\n\n`;
- fenced code: the body verbatim, one line per source line, no fences.

Markdown markers (`**`, `_`, backticks, `#`, `>`, link syntax) are never copied because
they are never inside a run: `MdSpan` already excludes them. Link runs copy their text,
not their URL. Tables copy as tab-separated cells, one row per line. The result is
UTF-8 to `CF_UNICODETEXT` with CRLF line ends, as the terminal copy does; the 32 KB cap
of `sel_copy_to_clipboard` becomes 256 KB and still cuts on a UTF-8 boundary. The
copy glyphs on code blocks and command rows keep copying raw source (PR #64 behaviour).

## 5. Tests

Native (`make test`), with the fixed-advance measurer:
- `text_layout`: wrapping of prose at a width, bullets and numbers as synthetic runs,
  code lines unwrapped, a span boundary inside a word, a table row; hit-test at every
  rule of section 3 including the synthetic-run skip and the margins; extraction of a
  range inside one run, across a soft wrap, across a hard break, across a bullet, across
  a code block, across two items; the prefix-width cache gives the same answer as a cold
  hit; a layout rebuilt at another width keeps a (item, offset) selection meaningful.
- `md_render` core moves what it can into `src/core/`: the word-break loop that decides
  line breaks is portable once the measurer is a callback, so it moves to
  `text_layout.c` and `md_render.c` keeps only the GDI calls. This is what makes the
  wrapping testable; it is the one refactor the change needs.
- Clipboard text: a markdown fixture with headings, bold, a list, a fenced block and a
  table, selected whole, matches a golden visible-text string.

Windows: `--ui-demo=ai-selection` shows a selection spanning a user bubble, a markdown
reply with a list and a code block, and a Thinking body, in all four themes through the
gallery; harness gate-tier case `ai_panel_select_copy` posts a button-down, mouse-moves
and button-up over a known reply, sends Ctrl+C through `Send-NutshellChord`, and checks
the clipboard against the expected substring; a second case checks that Edit > Copy with
the panel focused copies the same text and with the terminal focused copies the
terminal's.

## 6. Out of scope

A caret and keyboard-driven selection (Shift+arrows); drag-and-drop of selected text;
copying as rich text or HTML; selecting inside command rows and the approval card;
right-to-left text; search in the panel.
