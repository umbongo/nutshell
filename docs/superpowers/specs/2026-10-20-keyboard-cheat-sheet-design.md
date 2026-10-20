# Keyboard shortcuts cheat sheet

**Date**: 2026-10-20 (revised twice the same day after critique rounds 1 and 2)
**Status**: DRAFT, for the maintainer's review
**Branch**: `feat/putty-selection` (ships with the selection change, same
pull request; only the † rows depend on it)
**Asked for by the maintainer**: "a 1 page keyboard cheat sheet in the help
menu that lists all the keyboard shortcuts available, categorise the
shortcuts into sections to make finding them easier when scanning with
one's eyes."
**Proposed after critique, the maintainer's defaults unless overruled**
(section 4): the full list, in one or two columns as the screen allows,
with a scrollbar only when the page is still too tall; the Mouse section
stays.

## 0. What exists

A survey of `main` at fbfe5cd found:

- There is no accelerator table. Every shortcut is hand-coded in four
  functions in `src/ui/window.c` (`key_on_keydown` :3322, `key_on_syskeydown`
  :3491, `key_on_syschar` :3565, `key_on_char` :3584), in the AI panel's
  input subclass (`src/ui/ai_chat.c:2670`) and in the chat list
  (`src/ui/chat_listview.c:4243-4300`). Main-window chords only fire when
  the main window has focus.
- Shortcut lists exist in four places and disagree: the User Guide's
  "KEYBOARD SHORTCUTS" section (`src/ui/help_guide.c:349-375`, inside text
  part 6) and its SPECIAL KEYS block (`:128-150`), the README table
  (`README.md:349-372`) plus its paragraphs at `:118-157` and `:246-251`,
  and the menu accelerator strings (`window.c:2656-2709`). Two specs cite
  chords that do not exist (Ctrl+0, Ctrl+Tab).
- Patterns: About (`window.c:2763-2977`, custom-painted, layout in pure
  `src/core/about_layout.c`; its loop swallows `WM_QUIT` and re-enables the
  owner unconditionally); the User Guide (`help_guide.c`, read-only EDIT,
  themed scrollbar `csb_create`); the passphrase prompt's `was_disabled`
  pattern (`window.c:940-986`) for modality done right;
  `cmd_detail_dlg.c:296-299` for `WM_DPICHANGED`.
- The type ramp (`src/core/ns_type.c:7-11`): `FONT_BODY` is 13 px at 96 DPI
  with line height 1.4; twelve `CreateFont(` calls already exist in
  `src/ui` (the font cache, the renderer, the AI panel).

## 1. Behaviour

- **Help > Keyboard Shortcuts...** opens a modal, themed, custom-painted
  window listing every shortcut in sections, in two columns where the work
  area is wide enough and one column otherwise, with the themed scrollbar
  only when the page is still taller than the work area. Esc, Enter or its
  Close button closes it. Help-menu only: a chord of its own would be dead
  while the AI panel has focus and would name a different key on German and
  French layouts (section 4.2).
- **Fit, honestly.** The full list is about 46 rows and 7 headings, about
  850 px tall in two columns at 96 DPI. It is one page at 96 and 120 DPI on
  a 1080p screen; at 150 percent and above it scrolls. Section 4.1 offers
  the alternative of cutting the list to fit everywhere.
- **Sections**, in this order; in two columns, 1 to 4 left and 5 to 7 right:
  1. Sessions and tabs
  2. Copy, paste and selection
  3. Scrolling and zoom
  4. Keys the terminal sends to the shell
  5. AI Assist panel
  6. Window and help
  7. Mouse
- **A row** is one chord in the body font in `text_main` on the left, the
  action in the body font on the right, and the condition, where one
  exists, on a second line under the action in the caption font and
  `text_dim`. One chord per row: alternatives are separate rows (`Ctrl+V`
  and `Ctrl+Shift+V` are two rows with the same action). A row that names a
  menu item shows the menu path in the chord column. A chord that is a US
  layout name (`Ctrl+=`) carries "US layout" in its condition. No chip, no
  ellipsis: the columns are sized to the widest texts, and the table is
  written so an action fits `ns_scale(360, dpi)`; the layout test measures
  that.
- **One source of truth.** The rows live in a table in portable code
  (section 2.1). The sheet draws from it; the menu builder takes its
  accelerator strings from it, so a menu label cannot say something the
  sheet does not; the README's table is a generated block the native test
  compares verbatim; the User Guide's two shortcut lists are replaced by
  one line pointing at the sheet.
- **Contents.** The table is authoritative; this is the list as corrected
  by two critiques, the implementer's starting point. † marks rows that are
  true only once the selection spec (`2026-10-20-putty-selection-design.md`)
  has landed with its section 6 defaults.

  **Sessions and tabs**: Ctrl+Shift+T, new session (opens the Session
  Manager) · from the terminal, not the AI panel; Ctrl+Shift+W, close the
  active tab · from the terminal; Alt+F4, exit; Alt, open the menu bar;
  Alt+Space, system menu.
  **Copy, paste and selection**: Ctrl+Shift+C, copy the selection †;
  Ctrl+C, interrupt (^C), never copies †; Ctrl+V, paste · a confirmation
  first when Settings says so, Enter confirms †; Ctrl+Shift+V, paste;
  Shift+Insert, paste; menu Edit > Select All, select everything held and
  copy it † · Ctrl+A goes to the shell.
  **Scrolling and zoom**: Shift+PgUp, history up a page; Shift+PgDn,
  history down a page; PgUp, history up a page · on the alternate screen
  (vim, less): sent to the program; PgDn, likewise; Ctrl+PgUp, Ctrl+PgDn,
  always sent to the program; Ctrl+=, zoom the terminal font in · US
  layout; Ctrl+-, zoom out · US layout.
  **Keys the terminal sends to the shell**: Ctrl+Space, NUL; Shift+Tab,
  back-tab; Ctrl+Arrows, Ctrl+Home, Ctrl+End, sent with their modifiers;
  Alt+key, ESC then the key · with no live session: the menu; Ctrl+Alt+
  letter, ESC then the control byte · unless the layout makes it a
  character; F10, Shift+F10, sent, not the menu · with no live session:
  the menu; Backspace, BS over SSH, DEL in a local shell; menu Edit > Send
  Key, send the next key raw, or with Alt.
  **AI Assist panel**: Ctrl+Shift+Space, show or hide the panel · from the
  terminal, not from inside the panel; Enter, send · while a reply streams
  or commands run: stop them; Shift+Enter, new line; Ctrl+V, paste text, or
  attach an image from the clipboard; Ctrl+=, Ctrl+-, zoom the chat;
  Ctrl+C, copy the selected messages · conversation list focused; Ctrl+A,
  select all messages · list focused; Esc, clear the selection · list
  focused; PgUp, PgDn, Home, End, Up, Down, scroll the conversation · list
  focused.
  **Window and help**: F11, fullscreen · from the terminal; Ctrl+Shift+M,
  mark an issue in the debug log · from the terminal; Esc, Enter, close a
  dialog · most dialogs; menu Help > Keyboard Shortcuts, this sheet.
  **Mouse**: drag, select and copy †; double-click, select a word †;
  triple-click, select a line †; drag past the edge, scroll while
  selecting †; right-click, paste; wheel, scroll three lines; Ctrl+wheel,
  zoom the terminal font; drag in the AI panel, select messages · nothing
  copied until Ctrl+C; double-click in the AI panel, select a word;
  triple-click in the AI panel, select a line; right-click in the AI
  panel, Copy, Select all, Paste into message.

## 2. Design

### 2.1 The table, `src/core/shortcuts.c` / `.h`

```c
typedef struct {
    const char *section;    /* "Sessions and tabs", ... */
    const char *chord;      /* one chord: "Ctrl+Shift+T"; or a menu path "Edit > Select All" */
    const char *action;     /* "New session" */
    const char *condition;  /* "" or "on the alternate screen: sent to the program" */
    unsigned    flags;      /* SC_MENU_PATH; SC_SELECTION_SPEC (the dagger rows); SC_MENU_ACCEL id */
    int         menu_id;    /* IDM_* whose menu label carries this chord as accelerator, or 0 */
} Shortcut;

const Shortcut *shortcuts_table(size_t *count);          /* display order; sections contiguous */
size_t shortcuts_sections(const char **out, size_t cap);
const char *shortcuts_menu_accel(int menu_id);          /* "" when none */
/* The README block: a GitHub-flavoured markdown table per section, LF
 * line endings, between the markers below. Returns the length needed. */
size_t shortcuts_readme_block(char *buf, size_t cap);
```

The README markers are `<!-- shortcuts:begin -->` and `<!-- shortcuts:end -->`.
A chord row's grammar: keys joined by `+`, written as the key is named on a
US layout (`Ctrl+=`, `Ctrl+-`, `Shift+PgUp`); no alternatives, no
placeholders other than `key`, `letter` and `Arrows`, which are literal
words in the table.

### 2.2 The layout, `src/core/cheatsheet_layout.c` / `.h` (pure, tested)

Input: the DPI, the work-area size, each section's row count and which
rows have a condition, and the measured widths of the widest chord and the
widest action and condition (the UI measures text; natively the test uses
fixed per-character estimates and says so). Output: the column count, the
client size, whether the page scrolls, the scrollbar rectangle, the Close
button rectangle, and for every heading and row its column and rectangle.
Rules:

- Column width = chord column + `SP_LG` gutter + text column; the chord
  column is the widest chord, the text column the widest action or
  condition, no caps (the table is written to fit; the test asserts the
  action width is at most `ns_scale(360, dpi)` with its estimate).
- Two columns, sections 1 to 4 left and 5 to 7 right, when two columns plus
  margins fit the work-area width; else one column in section order.
- Spacing and type from `ns_type.h` as the design-system spec's table
  names them: `SP_LG` page padding, `SP_XL` between sections, `SP_XS`
  between rows, `FONT_HEADING` for section headings with a `border`-colour
  rule under each, `FONT_BODY` for chords and actions, `FONT_CAPTION` for
  conditions on their own line. Every pixel through `ns_scale(px, dpi)`.
- Page height = the taller column plus padding plus the button row. If it
  exceeds the work-area height, the window takes the work-area height, the
  rows scroll inside it with `csb_create`'s themed scrollbar (wheel,
  `WM_VSCROLL`, PgUp/PgDn, Home/End; painting with the scroll offset), and
  the Close button stays outside the scrolled area.
- The window is centred on the owner, then moved to lie within the owner's
  monitor work area.

### 2.3 The window, `src/ui/cheatsheet_dlg.c`

Custom-painted like About: registers `Nutshell_Shortcuts`; themed title
bar and borders as the User Guide draws them; colours from `ns_tokens()`
only (`text_main`, `text_dim`, `border`, `bg_primary` for the page),
converted with `theme_cr()` where a `COLORREF` is needed (tokens are
`0xRRGGBB`, `rgb_alpha` takes `COLORREF`); fonts from `ns_font(role, dpi)`;
one owner-draw Close button by `draw_themed_button`. Modality follows the
passphrase prompt (`window.c:940-986`), not About: `was_disabled` is
recorded for the main window and for a floating AI panel, each is
re-enabled only if this dialog disabled it, the previously focused window
is restored, and `SetForegroundWindow(owner)` is called before the dialog
is destroyed. The loop closes on Esc, Enter and Close, and re-posts a
`WM_QUIT` it sees. `WM_DPICHANGED` re-fetches fonts through `ns_font`
without flushing, re-runs the layout and re-clamps to the new monitor. A
second `IDM_HELP_SHORTCUTS` while open is ignored. Public entry
`cheatsheet_show(HWND parent)`.

Trade-off, stated: custom-painted text is invisible to a screen reader and
cannot be selected; the README carries the same table as text.

### 2.4 Wiring

- `resource.h`: `IDM_HELP_SHORTCUTS 2033`; `IDM_FILE_CLOSE_TAB 2008` (the
  free id in the File block).
- `create_app_menu()`: Help > "Keyboard Shortcuts..." first, above User
  Guide; File > "Close Tab" after New Session. Every item's accelerator
  text comes from `shortcuts_menu_accel(id)` instead of a literal after
  `\t`, so the six existing labels and the new one cannot disagree with the
  table. `WM_COMMAND` cases for both ids (`IDM_FILE_CLOSE_TAB` calls
  `close_active_tab`).
- `src/core/ui_demo.c`: a `shortcuts` state that opens the main window and
  the sheet, so `--ui-demo=all` and the visual check in CLAUDE.md include
  it; `tests/test_ui_demo.c`'s state count and the comment at `ui_demo.c:12`
  updated; `30-ui-demo.ps1`'s `$galleryStates` not updated (the gallery
  captures only the main window; the sheet has its own case, 3.2).
- `help_guide.c`: the KEYBOARD SHORTCUTS section inside part 6 and the
  SPECIAL KEYS block in part 2 are each replaced by one line, "See Help >
  Keyboard Shortcuts for the full list."; part 2's "see KEYBOARD SHORTCUTS
  below" goes with it; the "AES-256-GCM" text in parts 5 and 6 becomes the
  DPAPI description the README gives.
- `README.md`: the table at `:349-372` becomes the generated block between
  the markers; the paragraphs at `:118-157` and `:246-251` are cut to prose
  pointing at it; `:121` and `:355` lose "Ctrl+C copies".
- `.github/workflows/checks.yml:71`: `README.md` added to the paths that run
  the native suite; the workflow's comments at `:13-16` and `:55-60`, the
  gate section of CLAUDE.md and the gate section of
  `.claude/memory/MEMORY.md` say so, with a dated `.claude/CHANGELOG.md`
  entry. This is a change to what the gate runs and gets its own line in
  the pull request body.
- `docs/superpowers/specs/2026-09-09-integration-coverage.md` lines citing
  Ctrl+0 and Ctrl+Tab get a note that neither exists; the notes document's
  "never shown in the UI" line is corrected.
- `tests/integration/cases/50-window.ps1:466-470`: the Help id list gains
  2033 at the front and the File list 2008 after New Session; the detail
  strings "Help 3" and "File 11 items" are corrected;
  `tests/integration/README.md:40`'s case table gains the new cases.

Not changed: which chords exist (no Ctrl+Tab, no Ctrl+0, no approve or
deny keys), and the focus rule that main-window chords need the main
window focused. Both go on the notes open list.

## 3. Tests

### 3.1 Native

- `tests/test_shortcuts.c`: every row has a non-empty section, chord and
  action; sections are contiguous and in the fixed order; no row repeats
  another's chord and condition within a section; no chord contains ` / `
  or `,`; every `menu_id` is non-zero at most once per chord;
  `shortcuts_menu_accel` returns the table's chord for each of the seven
  menu ids and "" for `IDM_FILE_EXIT`. The README check reads `README.md`
  from the working directory, tolerates CRLF, and asserts the text between
  the markers equals `shortcuts_readme_block()` byte for byte (after CRLF
  folding). When the file is absent the test fails if the `CI` environment
  variable is set and skips with a printed note otherwise (`checks.yml:106`
  sets `CI: true`).
- `tests/test_cheatsheet_layout.c`: with the real table and the fixed
  estimates, sections 1 to 4 are in column 0 and 5 to 7 in column 1 at a
  1920 by 1040 work area and 96 DPI, with no scrollbar; at 1366 by 718 and
  120 DPI the page scrolls and the Close button and scrollbar rectangles
  lie inside the window; at 1920 by 984 and 192 DPI the layout is one
  column and scrolls; no rectangle overlaps another or leaves the client
  area; sizes at 96 and 192 DPI are within rounding of a factor of two;
  every action's estimated width is at most `ns_scale(360, dpi)`; the test
  prints the first DPI at which two columns stop fitting a 1080p work area.
- The scroll path: a pure `cheatsheet_scroll_clamp(offset, page_h, win_h)`
  tested at both ends.

### 3.2 Harness, gate tier, `tests/integration/cases/50-window.ps1`

`NutshellIT.psm1`: `Nutshell_Shortcuts` joins `$script:DIALOG_CLASSES`
(`:505`).

- `help_shortcuts_sheet_opens_and_closes`: `IDM_HELP_SHORTCUTS` by
  `WM_COMMAND`, wait for the sheet, screenshot it by its own handle
  (`Save-NutshellScreenshot -Hwnd`), non-blank, saved to artifacts; a second
  `IDM_HELP_SHORTCUTS` opens nothing more; Esc posted to the sheet's handle
  closes it; the main window is enabled afterwards.
- `help_shortcuts_sheet_fits_the_screen`: the sheet's rectangle lies within
  the monitor work area at the runner's DPI; the case records whether it
  scrolled.
- `help_shortcuts_sheet_gallery`: the sheet captured on all four themes
  into `artifacts\gallery\`, by handle, for the eye.

## 4. Open for the maintainer

1. **Fit** (proposed default: the full list, scrolling when it must). The
   alternative is to cut the list to about 30 rows so it is one page at 200
   percent on a 1080p screen: drop section 4 to three rows and shorten
   every condition.
2. **A chord for the sheet**: none (proposed). If wanted, it needs a runtime
   key label from `GetKeyNameText` and forwarding from the AI panel.
3. **The Mouse section**: keep (proposed).

## 5. Critique

**Round 1** (2026-10-20, Opus, 14 findings). Accepted: 1 (fit and order),
2 (width rule), 3 (row corrections, †, "most dialogs", wrong precedent), 4
(table-driven menu labels), 5 (CI path filter, CRLF), 6 (chord dropped), 7
(capture by handle, `DIALOG_CLASSES`, Esc to the sheet), 8 (Close Tab id and
command, harness lists), 9 (guide section replaced by a pointer; AES text),
10 (DPI change without flush, re-clamp), 11 (floating panel disabled), 12
(rounding tolerance), 13 (CreateFont check: reversed in round 2), 14
(trade-offs stated; `WM_QUIT` re-posted). Rejected: "ship separately",
because the maintainer asked for it in this change.

**Round 2** (2026-10-20, fresh Opus critic, 13 findings). Accepted: 1 (the
CreateFont gate dropped: twelve calls exist), 2 (menu labels from the table
by construction; the regex scan dropped as unreliable), 3 (README block
compared verbatim; one chord per row; grammar stated), 4 (Alt alone; † on
Ctrl+C and Select All; Edit > Copy label from the table; AI panel
double-click is a word, drag added; F11 and Ctrl+Shift+T/W/M conditions;
cross-references), 5 (duplicate rule is chord plus condition; panel rows
reworded), 6 (chips dropped; conditions on their own line; one-column
fallback; honest fit statement and tests; section 4.1), 7 (`was_disabled`
modality, foreground, focus restore), 8 (`theme_cr`, `bg_primary`), 9 (CI
comments, CLAUDE.md, memory, changelog, PR line), 10 (part 5 AES, part 2
pointer and SPECIAL KEYS), 11 (fail when `CI` is set), 12 ("US layout" on
the zoom rows), 13 (SP_XL/SP_LG as the design spec names them; File id
2008; harness README; demo state; scroll clamp test). Rejected: the
read-only EDIT alternative, because the maintainer asked for sections to
scan and two columns; it is recorded as the fallback if the custom window
proves costly. Two rounds is the ceiling; anything beyond goes in the pull
request body.
