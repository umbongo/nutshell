# AI Assist: command details popup, copy icons, transcript clipboard

**Date**: 2026-10-13
**Status**: DRAFT, revised after one Opus critique (dispositions at the end).
**Branch**: `feat/cmd-detail-popup`
**Prompted by**: the maintainer, 2026-10-13. Three asks, in their words: (1) the approval
card truncates commands, so clicking a command must open a popup with a short description of
what it intends to do at the top and the full command below; (2) text cannot be copied or
pasted from the AI Assist window; (3) an icon on each command that copies it to the clipboard.
**Builds on**: `2026-09-23-command-dispatch-states-design.md` (the approval card and its
`ns_layout.c` geometry), `2026-09-07-design-system-foundation-design.md` (tokens, fonts,
icons), `2026-09-25-ai-command-origin-marker-design.md`.

---

## Problem

**Truncation.** An approval row is one line with an end ellipsis (`src/core/ns_layout.c:157`,
`src/ui/chat_listview.c:1593`). The panel is narrow, so a command like
`echo "alias ll='ls -al'" >> ~/.bashrc` shows as `echo "alias ll='ls -al…`. The only way to
read it is to widen the panel. Clicking the row text does nothing except start a transcript
drag-selection (`chat_listview.c:3153`, `:3229`).

**No description.** The system prompt already tells the model to "explain what each command
does before its marker" (`src/core/ai_prompt.c:259-282`), but the live path cuts the reply
text at the first `[EXEC]` (`src/ui/ai_chat.c:4239-4245`) and the per-command explanations are
never shown. Nothing on `ChatMsgItem.u.cmd` (`src/core/chat_msg.h:40-55`) or `ApprovalEntry`
(`src/core/chat_approval.h:19-30`) holds one.

**Clipboard.** The transcript has a band selection that copies whole items on mouse-up,
Ctrl+C and Ctrl+A (`chat_listview.c:3229-3329`, `:3570-3576`), but it copies `CF_TEXT` with
the raw UTF-8 bytes (`sel_copy_to_clipboard`, `:432-446`), so anything non-ASCII (the `—` and
`·` the models like) is garbled, there is no visible feedback, and right-click silently
pastes into the input box (`:3333`, `ai_chat.c:3879`). From the user's seat: "I can't copy".
Fenced code blocks in a reply (`src/ui/md_render.c:791-838`) have no geometry, no hit id and no
button, so a command the model wrote as a code block instead of an `[EXEC]` marker (the
screenshot: eight `echo … >> ~/.bashrc` lines) can only be band-selected together with the
prose around it.

---

## Design

### 1. Per-command description, extracted from the reply

**One walk, not two.** `ai_extract_commands_ex()` (`src/core/ai_prompt.c:1224`) gets a
sibling that shares its walker, so a note can never drift from its command:

```c
#define CMD_NOTE_MAX 256

/* Same acceptance rules and the same return as ai_extract_commands_ex();
 * additionally fills notes[i] for every ACCEPTED block i, so index i
 * matches cmds[i] by construction. notes may be NULL (then this is
 * ai_extract_commands_ex). The note is the LAST paragraph (text after the
 * final blank line) of the prose between the end of the previous [EXEC]
 * block -- accepted or dropped -- and this block's [EXEC]. It is trimmed,
 * markdown fence lines removed, whitespace runs collapsed to one space,
 * C0/C1 controls and Unicode bidi controls removed, and cut at the last
 * UTF-8 boundary that fits CMD_NOTE_MAX-1 bytes. A note containing no
 * letter or digit (a bare "2." or "-") is stored as empty. */
int ai_extract_commands_notes(const char *response, char cmds[][1024],
                              char notes[][CMD_NOTE_MAX], int max_cmds,
                              int *rejected, int *rejected_long);
```

`ai_extract_commands_ex()` becomes a call to this with `notes == NULL`. Taking only the last
paragraph means a lead-in such as "Here's the plan:" followed by a blank line and "1. Check
disk usage" yields the second. Prose *after* a marker and before the next is, by the prompt,
the next command's explanation; the system prompt gains one sentence to make that
unambiguous: "Put the explanation on the line directly before its marker and write nothing
between a marker and the next explanation." A note is never synthesised from the command.

The note is model prose shaped by whatever the terminal showed the model, so the popup labels
it as such ("The assistant says:" in `text_dim` above the note) and the risk tag stays the
more prominent element. An empty note shows the fixed line "The assistant gave no
explanation for this command." in `text_dim`.

**Where it lives.** `ApprovalEntry` gains `char note[CMD_NOTE_MAX];` (4 KB per queue at
`APPROVAL_MAX_CMDS` rows; `bg_defaults` on the stack in `ai_chat.c:4145` grows by the same)
and `chat_approval.h` gains `chat_approval_set_note(q, idx, note)` (copies, NUL-terminates,
ignores an out-of-range `idx`). Nothing else in the add API changes, so the existing callers
in tests stay as they are. The three places that add commands to a queue set the note on the
index **returned by** `chat_approval_add_session()`, never the loop index: the live path
(`ai_chat.c:4341`), the background-session path (`:4157`) and `ui_demo.c`.
`ChatMsgItem.u.cmd` gains `char *note;` (heap, `secure_zero`d and freed with the item like
`command`), duplicated from the entry wherever card items are built
(`append_batch_command_items()`, `ai_chat.c:1031`, and the live path at `:4340-4353`). Both
the live and the session-switch rebuild paths therefore get the note from the queue while the
batch is alive. **Known limit:** a finished batch is removed (`cmd_batch_remove`), so after a
session switch its commands appear only as inline `[EXEC]` segments inside the rebuilt AI text,
which have no popup and no copy icon. Fixing that means keeping finished batches or parsing
the rebuilt text into settled rows; it is a follow-up, recorded in the notes document.

`ui_demo.c`'s `approval` state sets a note on each row and adds one long command (about 120
characters, `echo "alias …" >> ~/.bashrc` style) so truncation and the copy glyph can be
eyeballed with `nutshell.exe --ui-demo=approval`. A new state `cmddetail` is `approval` with
the popup opened on the long row, so `ui_gallery` can capture it.

### 2. The details popup

Clicking the row text (`HIT_TEXT`) **or the risk tag** (`HIT_TAG`, so the popup stays
reachable on a panel so narrow that the text rect has gone to zero width) of a pending or
settled command row opens a modal window, class `Nutshell_CmdDetail`, new file
`src/ui/cmd_detail_dlg.c`.

**Lifetime.** The click handler does not open the popup. Like every other card action it
posts a `WM_COMMAND` (`IDC_CMD_DETAIL`, item id in `lParam`) to the panel; the panel's handler
looks the item up by id, and the popup **copies** command, note and safety level into its
own storage when it opens. It then runs its own modal loop with the parent disabled. Any
`WM_AI_STREAM`, batch eviction or `chat_rebuild_display()` that fires while it is open frees
list items the popup does not reference. The loop re-posts `WM_QUIT` if it sees one, calls
`IsDialogMessage` so Tab cycles Copy, Close and the text box, and ends on Esc, Enter (when
the text box does not have focus) or the close box.

**Appearance.** Colours from `ns_tokens()`, fonts from `ns_font()`, sizes from `ns_scale()`
and `ns_type.h`, title bar through `themed_apply_title_bar`. Layout from a pure
`cmd_detail_layout()` in `src/core/cmd_detail_layout.c`, tested in
`tests/test_cmd_detail_layout.c` the way `about_layout` is:

```
┌ Command details ──────────────────────────────── ✕ ┐
│ The assistant says:                                 │  <- FONT_CAPTION, text_dim
│ Appends the colour and alias settings to ~/.bashrc  │  <- note, FONT_BODY, text_main,
│ so that ls, grep and diff colour their output.      │     DT_WORDBREAK, at most 8 lines,
│                                                     │     DT_END_ELLIPSIS on the last
│ [ Write ]                                           │  <- the row's risk tag, drawn as on
│                                                     │     the card
│ ┌─────────────────────────────────────────────────┐ │
│ │ echo "alias ls='ls --color=auto'" >> ~/.bashrc  │ │  <- read-only EDIT (Unicode:
│ │                                                 │ │     CreateWindowExW/SetWindowTextW),
│ │                                                 │ │     FONT_MONO, ES_MULTILINE|
│ └─────────────────────────────────────────────────┘ │     ES_READONLY, wrapped, selectable
│                                    [ Copy ] [ Close ]│
└─────────────────────────────────────────────────────┘
```

- Window `ns_scale(560, dpi)` by `ns_scale(360, dpi)`, resizable, centred on the AI panel's
  top-level window. The layout function takes the measured note height (from
  `DrawText(DT_CALCRECT)`, capped at eight lines) as an input so it stays pure; the command
  box takes the rest.
- A read-only EDIT asks for its colours with `WM_CTLCOLORSTATIC`, not `WM_CTLCOLOREDIT`; the
  popup handles both and returns the `cmd_bg` brush.
- `WM_DPICHANGED`: re-fetch fonts with `ns_font(role, new_dpi)` and relayout. Never
  `ns_font_flush()` from the popup: that deletes the panel's cached fonts.
- Copy copies the command (section 4) and relabels itself "Copied" for 1.5 s via an
  `ns_motion` timer, then reverts. No toast system exists and none is added.
- Informational only: no Approve or Deny. Those stay on the card so no decision is taken from
  a window showing one command out of context.
- `chatlv_hit_is_actionable()` adds `HIT_TEXT` and `HIT_TAG`; the row text gets an `ns_hover`
  state (there is none today) that draws it in `text_main` instead of the row colour and shows
  a hand cursor. Clicking the row text no longer starts a band selection; the margin and the
  prose still do.

### 3. Copy icons

Two places get the existing `NS_ICON_COPY` glyph (`src/ui/icons.h:38`), drawn with
`ns_icon_draw()` at `SZ_ICON`, `text_dim` at rest and `text_main` on hover, hand cursor.
Neither copy includes a trailing newline (pasting into a terminal with paste confirmation off
must not run the command).

**Approval rows.** `ApprovalRowLayout` (`ns_layout.h:42-46`) gains `NsRect copy;` between
the text and the risk tag with `SP_XS` either side; `approval_card_layout()` shrinks `text.w`
accordingly and `approval_card_hit()` returns a new `HIT_COPY`. `settled_row_layout()` gives
settled rows the same rect. Click copies `u.cmd.command`. Drop order on a narrow panel: the
copy rect goes to zero size (and is never returned by the hit test) before the text does, and
the tag is the last to survive. `tests/test_ns_layout.c` extends the round-trip and overlap
cases to the new rect and adds the drop-order case.

**Hit ids.** Settled rows have no ids today and `CLV_THINK_HOVER_ID` is `4353 + item id` with
item ids unbounded (`chat_listview.c:122-141`), so no range "above" it is safe. Two new ranges
are placed far above everything, with the bound stated in the macro comment:
`CLV_SETTLED_HIT_BASE = 0x20000000` encoding `item id * 8 + HIT_*` for settled rows, and
`CLV_CODE_COPY_BASE = 0x40000000` encoding `item id * 16 + block` for code blocks. The
Thinking range would collide only past item id `0x1FFFFFFF - 4353`, which a session cannot
reach. Both are wired into `chatlv_hover_hit`, `chatlv_hover_rect_for_id`,
`chatlv_hit_is_actionable` and `on_lbuttondown` the way `chatlv_thinking_row_hit` (`:2780`) is.

**Fenced code blocks in replies.** A pure splitter in `src/core/md_blocks.c` that reuses the
renderer's own line classifier (`md_classify_line`, header-only `src/ui/markdown.h`) so it
recognises exactly the fences the renderer draws (three backticks at column 0):

```c
typedef struct { size_t start, end; } MdCodeSpan;   /* body span, fences excluded */
/* Report each fenced code block's body span in `text`, in order. An
 * unterminated fence runs to the end. Returns the count (may exceed max;
 * only max are written). */
int md_code_blocks(const char *text, size_t len, MdCodeSpan *out, int max);
```

tested in `tests/test_md_blocks.c`: no block; one; two; a language tag; unterminated; an
**empty body** (the live path leaves ```` ```bash\n ```` when the first `[EXEC]` follows a fence
opener); CRLF; an indented fence that must **not** count, because the renderer does not draw
it. The renderer walks per `[EXEC]`-split **segment** (`draw_ai_text_with_exec`,
`chat_listview.c:1244-1330`), so the splitter is applied to each segment's text exactly as
passed to `md_render_core`, and `md_render_core` fills an optional out array of block rects
from the same line walk. `chat_listview.c` keeps rects and spans per item; if their counts
differ for a segment, that segment draws no glyphs (never a mismatched copy). A block with an
empty body, or narrower than two icon widths, gets no glyph. When glyphs are enabled the
renderer reserves `SZ_ICON + SP_XS` on the right of every code line in the block so the glyph
never covers text (`MD_CODE_PAD_H` is 4 px, far too small to hide it).

Nothing is added to the input box, the user bubbles or the Thinking row.

### 4. Transcript clipboard, fixed rather than redesigned

- `set_clipboard_utf8()` moves out of `window.c` (`:2644-2657`) into `src/ui/clipboard.c` /
  `clipboard.h` as `clip_set_utf8(const char *utf8, size_t len)` (`CF_UNICODETEXT`).
  `window.c` and `chat_listview.c` (`sel_copy_to_clipboard`) call it. That fixes the garbled
  copy. **The chat paste path is left alone**: the input box is an ANSI `EDIT`
  (`ai_chat.c:3176`) fed by `EM_REPLACESEL` with `CF_TEXT`, and feeding it UTF-8 would garble
  it the other way. Making the input box Unicode is a separate change.
- Before copying, band-selection text passes through a pure
  `chat_strip_exec_markers(char *s)` in `src/core/chat_msg.c` (removes `[EXEC]` and `[/EXEC]`,
  keeps the command text) and the 32 KB cap is applied at a UTF-8 boundary by a pure
  `utf8_cut_at(buf, max)`; both tested. Markdown stays as written; turning rendered markdown
  back into plain text is out of scope.
- Right-click in the transcript opens a context menu with **Copy** (enabled when a selection
  exists), **Select all** and **Paste into message** (the current silent paste, now a named
  item). Nothing pastes on a bare right-click any more. It is a native `TrackPopupMenu`, the
  first in `src/ui`, and follows the system menu theme like the native menu bar does
  (`create_app_menu`), not the app theme. Accepted: a themed owner-drawn menu is not worth
  building for three items.
- Character-level selection inside an item is **out of scope**; the band selection stays. The
  copy icons cover wanting one command, and the popup's EDIT gives character selection for the
  command text.

### 5. Resizable composer, and line ends on paste (added 2026-10-14)

The maintainer's fourth ask, from a screenshot of a pasted multi-line reply crammed onto one
line of the input box: "make the ai textbox resizable".

**The box is already multi-line** (`ES_MULTILINE | ES_WANTRETURN`, `ai_chat.c:3178`) but
fixed at `ns_scale(46, dpi)`, about two lines (`:871`, `:2631`, `:3145`), and the panel's
own `AiPanelLayout` already takes the composer height as an input. Two changes:

- **A drag sash** between the status line and the composer. `ai_panel_layout()` gains a
  `sash` rect in `AiPanelLayout`: `SP_SM` tall, full panel width, directly above the
  composer, taken out of the thread's height (the status line keeps its place above it; the
  tiling tests' exact-sum invariant extends to the new rect). A pure
  `ai_composer_clamp_h(int want_h, int panel_h, int line_h, int dpi)` clamps a requested
  height to `[2 * line_h + padding, panel_h / 2]`, never below the minimum even when the panel
  is tiny; tested at 96 and 192 DPI. In `ai_chat.c` the panel handles `WM_SETCURSOR`
  (`IDC_SIZENS` over the sash), `WM_LBUTTONDOWN` (capture, remember the start), `WM_MOUSEMOVE`
  (new height from the cursor, clamped, relayout) and `WM_LBUTTONUP`/`WM_CAPTURECHANGED`
  (release), the way `window.c`'s panel splitter does (`g_ai_splitter_dragging`, `:4836-4894`).
  The chosen height lives in `AiChatData` as a 96-DPI base value and is rescaled on
  `WM_DPICHANGED`; it is not persisted, matching the docked panel width, which is not either.
  The sash is painted as a `border`-coloured hairline (`STROKE_HAIRLINE` or the nearest token)
  so it is discoverable; no grip glyph.
- **LF to CRLF on paste into the message box.** The transcript copies lines joined with
  `\n`; a multiline `EDIT` only breaks on `\r\n`, which is why the pasted reply in the
  screenshot ran on as one line. The `IDC_CHATLIST_PASTE` handler (`ai_chat.c:3879`) and the
  input's `WM_PASTE` (which falls through to the control today) normalise bare `\n` to
  `\r\n` before `EM_REPLACESEL`, through a pure, tested `text_lf_to_crlf()` in
  `src/core` (returns the needed size, never splits an existing `\r\n`). The box stays ANSI
  (section 4).

### 6. Out of scope, said once

- A description for code blocks that are not `[EXEC]` commands (section 3 copies them; the
  popup is for approval rows only).
- Approve/Deny from inside the popup.
- The classifier's reason string (`cmd_classify_ex`) as a second line in the popup: it
  explains the risk level, not the intent, and production has no `_session` variant returning
  it. Worth a follow-up if the maintainer wants "why is this Write?".
- Popups and copy icons for the inline `[EXEC]` segments of rebuilt AI text (section 1, known
  limit). A Unicode input box.

---

## Files

| Area | File | Change |
|---|---|---|
| core | `src/core/ai_prompt.h/.c` | `CMD_NOTE_MAX`, `ai_extract_commands_notes()`, `_ex` delegates; one prompt sentence |
| core | `src/core/chat_approval.h/.c` | `ApprovalEntry.note`, `chat_approval_set_note()` |
| core | `src/core/chat_msg.h/.c` | `u.cmd.note`, wiped and freed with the item; `chat_strip_exec_markers()`; `utf8_cut_at()` |
| core | `src/core/ns_layout.h/.c` | `ApprovalRowLayout.copy`, `HIT_COPY`, narrow-panel drop order |
| core | `src/core/cmd_detail_layout.h/.c` | pure popup layout |
| core | `src/core/md_blocks.h/.c` | `md_code_blocks()` over `md_classify_line` |
| core | `src/core/ui_demo.c` | notes and a long command in `approval`; `cmddetail` state |
| ui | `src/ui/cmd_detail_dlg.h/.c` | the popup |
| ui | `src/ui/clipboard.h/.c` | `clip_set_utf8` |
| ui | `src/ui/chat_listview.c` | `HIT_TEXT`/`HIT_TAG` click posts `IDC_CMD_DETAIL`; `HIT_COPY`; settled and code-block id ranges; hover on row text; context menu; marker stripping and cap |
| ui | `src/ui/md_render.c` | block rect out array; reserved right margin |
| ui | `src/ui/ai_chat.c` | set notes on the queue (returned index, both paths); copy note to items; `IDC_CMD_DETAIL` handler; demo state; composer sash drag; CRLF on paste |
| ui | `src/ui/window.c` | call `clip_set_utf8` |
| core | `src/core/ai_panel_layout.h/.c` | `AiPanelLayout.sash`, `ai_composer_clamp_h()` |
| core | `src/core/text_lines.h/.c` | `text_lf_to_crlf()` |
| tests | `tests/test_ai_panel.c`, `tests/test_text_lines.c` | sash tiling, clamp, LF to CRLF |
| ui | `src/ui/resource.h` | `IDC_CMD_DETAIL`; version |
| tests | `tests/test_ai_prompt.c` | notes aligned with drops; last paragraph; empty and digit-only notes; UTF-8 cut; fences; CRLF; controls stripped |
| tests | `tests/test_chat_approval.c` | `set_note` bounds; note survives approve/deny |
| tests | `tests/test_chat_msg.c` | note freed; `chat_strip_exec_markers`; `utf8_cut_at` |
| tests | `tests/test_ns_layout.c` | copy rect placement, round trip, overlap, drop order |
| tests | `tests/test_cmd_detail_layout.c` | new |
| tests | `tests/test_md_blocks.c` | new |
| tests | `tests/runner.c` | declarations and calls |
| build | `Makefile` | nothing: `SRCS` and `NON_TEST_SRCS` are wildcards |

Version: bumped once per build as CLAUDE.md requires (`1.2.29` → `1.2.30` for the first, and
again for every rebuild), the final exe committed. The cross-compile on `automaton` (the
maintainer's Linux box, not a cloud session) produced the v1.2.29 exe that passed the gate on
2026-10-13; the `feature-workflow` skill's "only on the Windows box" wording predates that and
is corrected in this change's `.claude/CHANGELOG.md` entry.

What `make test` cannot catch, by construction (all in `src/ui`): hit-id decoding, the
popup's lifetime, ANSI/Unicode window creation, span-to-rect alignment, glyph overlap. Those
are checked by the Opus diff review and by eyeballing `--ui-demo=approval` and
`--ui-demo=cmddetail` on Windows before the PR is marked ready.

---

## Critique

One round, Opus, 2026-10-13. Dispositions:

1. Hit-id range collision: **accepted**, dedicated high ranges with a stated bound (section 3).
2. Popup lifetime during streaming: **accepted**, `WM_COMMAND` to the panel and copied data (section 2).
3. ANSI input box and the popup EDIT: **accepted**, paste path untouched, popup created Unicode (sections 2, 4).
4. Wrong prose pinned on a destructive command: **accepted** in part: last paragraph only, a prompt sentence, digit-only notes count as empty, "The assistant says:" label, risk tag prominent. A structured field from the model was rejected: it changes the reply protocol every provider is tested against for a label.
5. Two walks drift: **accepted**, one walker with an optional notes array; returned index; background path named.
6. Splitter and renderer disagree: **accepted**, splitter reuses `md_classify_line`, applied per segment, counts must match.
7. Zero-line block: **accepted**, no glyph, tested.
8. Glyph covers code: **accepted**, reserved right margin.
9. False claims: **accepted**, all four corrected (hover added, `TrackPopupMenu` acknowledged as new and system-themed, `SP_XS`, settled rows get both hits).
10. Finished batches after a session switch: **accepted as a stated limit** with a follow-up; keeping finished batches alive is a dispatcher change outside this feature.
11. Trailing newline: **accepted**, neither copy has one.
12. Cap and stripping untested: **accepted**, both pure and tested.
13. DPI, fonts, `IsDialogMessage`, `WM_QUIT`, `WM_CTLCOLORSTATIC`: **accepted**, all five.
14. Minor: tag opens the popup (accepted); long notes get an ellipsis (accepted); controls stripped (accepted); `secure_zero` (accepted); band selection from row text (accepted, intended); entry size (accepted, 256 bytes); version wording (accepted); Linux build vs skill text (accepted, skill corrected); demo state for the gallery (accepted).

Not-verified items: `ui_demo.c`'s approval state does go through `append_batch_command_items()` (survey, `ai_chat.c:5294`); the cross-compile on automaton did produce the gate-passing v1.2.29 exe. The rest are line references already used above.
