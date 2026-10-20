# Keyboard shortcuts cheat sheet

**Date**: 2026-10-20 (revised the same day after critique round 1)
**Status**: DRAFT, for the maintainer's review
**Branch**: `feat/putty-selection` (ships with the selection change, same
pull request; only the Mouse rows depend on it)
**Asked for by the maintainer**: "a 1 page keyboard cheat sheet in the help
menu that lists all the keyboard shortcuts available, categorise the
shortcuts into sections to make finding them easier when scanning with
one's eyes."

## 0. What exists

A survey of `main` at fbfe5cd found:

- There is no accelerator table. Every shortcut is hand-coded in four
  functions in `src/ui/window.c` (`key_on_keydown` :3322, `key_on_syskeydown`
  :3491, `key_on_syschar` :3565, `key_on_char` :3584), in the AI panel's
  input subclass (`src/ui/ai_chat.c:2670`) and in the chat list
  (`src/ui/chat_listview.c:4243-4300`). Main-window chords only fire when
  the main window has focus: with the AI panel focused, Ctrl+Shift+Space,
  F11, Ctrl+Shift+T/W/M do nothing.
- Shortcut lists already exist in four places and disagree: the User
  Guide's "KEYBOARD SHORTCUTS" section (`src/ui/help_guide.c:349-375`,
  inside the larger text part 6), the README table (`README.md:349-372`)
  plus its Text Selection, Keyboard and Zoom paragraphs (`:118-157`,
  `:246-251`), and the menu accelerator strings (`window.c:2656-2709`).
  Two specs cite chords that do not exist (Ctrl+0, Ctrl+Tab).
- Menu labels carry accelerator text for six chords; eight handlers have no
  menu text (Ctrl+Shift+W has no menu item at all).
- The closest dialog patterns: About (`window.c:2763-2977`, custom-painted,
  layout in pure `src/core/about_layout.c`, Esc or Enter closes, never
  clamps to the work area) and the User Guide (`help_guide.c`, a read-only
  EDIT with the themed custom scrollbar `csb_create`). `cmd_detail_dlg.c:
  296-299` is the precedent for `WM_DPICHANGED` (re-fetch fonts, no flush).

## 1. Behaviour

- **Help > Keyboard Shortcuts...** opens a modal, themed, custom-painted
  window that shows every shortcut at once in two columns: a page to scan,
  not a document to read. Esc, Enter or its Close button closes it. It is
  Help-menu only: a chord of its own would be dead while the AI panel has
  focus and would name a different key on German and French layouts (section
  5).
- **Sections**, in this order, left column 1 to 4, right column 5 to 7:
  1. Sessions and tabs
  2. Copy, paste and selection
  3. Scrolling and zoom
  4. Keys the terminal sends to the shell
  5. AI Assist panel
  6. Window and help
  7. Mouse
- **A row** is the chord on the left as a keycap chip (body font on a
  tinted chip, one chip per key, "+" between), and a short action on the
  right. A condition, where one exists, follows the action in the dim text
  colour after a middle dot. A row that names a menu item instead of a
  chord shows "menu" in the chip column. Chords that change meaning with
  focus appear once per section where they apply, each with its condition.
  An action wider than its column is cut with an ellipsis (design-system
  spec, section 3); the table is written so none is at 96 DPI.
- **One source of truth.** The rows live in a table in portable code
  (section 2.1). The sheet draws from it; a native test checks the README
  table against it; a source-scan test requires a row for every accelerator
  string in the menu builder and every chord in the main key handler; the
  User Guide's section is replaced by one line pointing at the sheet, so
  that list no longer exists.
- **Contents.** The table is authoritative; this is what the survey found,
  corrected by the critique, and what the implementer starts from. "Mouse"
  rows marked † exist only once the selection spec
  (`2026-10-20-putty-selection-design.md`) has landed.

  **Sessions and tabs**: Ctrl+Shift+T new session (opens the Session
  Manager); Ctrl+Shift+W close the active tab; Alt+F4 exit; Alt (alone)
  open the menu bar · only with no live session, see section 4; Alt+Space
  system menu.
  **Copy, paste and selection**: Ctrl+Shift+C copy the selection; Ctrl+C
  interrupt (^C) · copies nothing; Ctrl+V / Ctrl+Shift+V paste · a
  confirmation first when Settings says so; Shift+Insert paste; menu:
  Edit > Select All select everything held · Ctrl+A goes to the shell.
  **Scrolling and zoom**: Shift+PgUp / Shift+PgDn scroll history a page;
  PgUp / PgDn scroll history · on the alternate screen (vim, less): sent to
  the program; Ctrl+PgUp / Ctrl+PgDn always sent to the program; Ctrl+= /
  Ctrl+- zoom the terminal font.
  **Keys the terminal sends to the shell**: Ctrl+Space NUL; Shift+Tab
  back-tab; Ctrl+Arrows, Ctrl+Home/End word and line moves with their
  modifiers; Alt+key ESC then the key · with no live session: the menu;
  Ctrl+Alt+letter ESC then the control byte · unless the layout makes it a
  character; F10 / Shift+F10 sent, not the menu · with no live session:
  the menu; Backspace BS over SSH, DEL in a local shell; menu: Edit > Send
  Key > Send Next Key Raw / with Alt one-shots.
  **AI Assist panel**: Ctrl+Shift+Space show or hide the panel · from the
  terminal, not from inside the panel; Enter send · while a reply streams
  or commands run: stop them; Shift+Enter new line; Ctrl+V paste text, or
  attach an image from the clipboard; Ctrl+= / Ctrl+- zoom the chat;
  Ctrl+wheel zoom the chat. With the conversation list focused: Ctrl+C
  copy the selected messages; Ctrl+A select all messages; Esc clear the
  selection; PgUp / PgDn, Home / End, Up / Down scroll the conversation.
  **Window and help**: F11 fullscreen; Ctrl+Shift+M mark an issue in the
  debug log; Esc / Enter close a dialog · most dialogs; menu: Help >
  Keyboard Shortcuts this sheet.
  **Mouse**: drag select and copy †; double-click select a word †;
  triple-click select a line †; drag past the edge scroll while selecting
  †; right-click paste; wheel scroll three lines; Ctrl+wheel zoom the
  terminal font; in the AI panel: double-click a message, right-click
  Copy / Select all.

## 2. Design

### 2.1 The table, `src/core/shortcuts.c` / `.h`

```c
typedef struct {
    const char *section;      /* "Sessions and tabs", ... */
    const char *chord;        /* "Ctrl+Shift+T"; "" for a menu row */
    const char *action;       /* "New session" */
    const char *condition;    /* "" or "on the alternate screen: sent to the program" */
    unsigned    flags;        /* SC_MENU_ROW; SC_NEEDS_SELECTION_SPEC for the dagger rows */
} Shortcut;

const Shortcut *shortcuts_table(size_t *count);   /* display order; sections contiguous */
size_t shortcuts_sections(const char **out, size_t cap);
/* Split "Ctrl+Shift+T" into keys for the chips: returns count, fills out[]. */
size_t shortcuts_split_chord(const char *chord, const char **out, size_t cap, char *scratch, size_t scratch_cap);
```

Plain ASCII in the table; the sheet draws the dot and the chips itself.

### 2.2 The layout, `src/core/cheatsheet_layout.c` / `.h` (pure, tested)

Input: the DPI, the work-area size, for each section its row count, and
the measured widths of the widest chip run and the widest action per
column (the UI measures text; the layout takes widths). Output: the
client size and, for every heading and row, its column and rectangle.
Rules:

- Two columns: sections 1 to 4 left, 5 to 7 right. A fixed split keeps the
  reading order; with today's rows it is also within one section of
  balanced (23 and 23 rows plus headings).
- Column width = chip column + gutter + action column, where the action
  column is at most `ns_scale(320, dpi)` and anything wider is cut with an
  ellipsis by the painter. Chip column = the widest chip run, capped at
  `ns_scale(150, dpi)`.
- Spacing and type from `ns_type.h`: `SP_XL` page margins, `SP_LG` between
  sections, `SP_XS` between rows, `FONT_HEADING` for section headings,
  `FONT_BODY` for chips and actions, `FONT_CAPTION` for conditions when
  they wrap to a second line (they never do at 96 DPI by construction of
  the table). Every pixel through `ns_scale(px, dpi)`.
- Page height = the taller column plus margins plus the button row. If
  that exceeds the work area, the window is the work area's height and the
  page scrolls inside it with the User Guide's themed scrollbar
  (`csb_create`); the Close button stays outside the scrolled area. The
  design goal is no scrollbar at 1920 by 1080 and 150 percent; the test
  pins that figure and reports the first DPI at which the page no longer
  fits.
- The window is centred on the owner and then moved to lie within the
  owner's monitor work area (About does not do this; the sheet must).

### 2.3 The window, `src/ui/cheatsheet_dlg.c`

Follows About: registers `Nutshell_Shortcuts`, custom-painted, themed
title bar and borders as the User Guide draws them, colours from
`ns_tokens()` only (`text_main` for actions, `text_dim` for conditions and
the dot, `border` for the rule under a heading and the chip outline, a
chip background of `rgb_alpha(surface.base, text_main, ...)` from
`ns_draw.h`, surface `.base` for the page), fonts from `ns_font(role,
dpi)`, one owner-draw Close button by `draw_themed_button`. Modal loop
with `IsDialogMessage`, closes on Esc, Enter and Close; the loop re-posts
`WM_QUIT` if it sees one (the About loop swallows it). While open, both
the main window and a floating AI panel are disabled (`EnableWindow` on
each owned top-level), so the panel's gear cannot open Settings over the
sheet. `WM_DPICHANGED`: re-fetch fonts through `ns_font` without flushing
the cache (`cmd_detail_dlg.c:296-299`), re-run the layout, re-clamp to the
new monitor's work area. A second `IDM_HELP_SHORTCUTS` while the sheet is
open is ignored (a static "open" flag). Public entry
`cheatsheet_show(HWND parent)`.

Trade-off, stated: custom-painted text is invisible to a screen reader and
cannot be selected; the README carries the same table as text.

### 2.4 Wiring

- `resource.h`: `IDM_HELP_SHORTCUTS 2033`, `IDM_FILE_CLOSE_TAB 2034`.
- `create_app_menu()`: Help > "Keyboard Shortcuts..." as the first item,
  above User Guide; File > "Close Tab\tCtrl+Shift+W" after New Session.
  `WM_COMMAND` cases for both (`IDM_FILE_CLOSE_TAB` calls
  `close_active_tab`). The menu pool (`MAX_MENU_ITEMS 64`, about 53 used)
  has room.
- `help_guide.c`: the KEYBOARD SHORTCUTS section inside part 6 is replaced
  by one line, "See Help > Keyboard Shortcuts for the full list." While in
  that part, the SECURITY paragraph's "AES-256-GCM" becomes the DPAPI
  description the README gives.
- `README.md`: the table at `:349-372` is rewritten from the same rows in
  the same section order; the paragraphs at `:118-157` and `:246-251` are
  cut to prose that points at the table.
- `.github/workflows/checks.yml:71`: `README.md` is added to the paths that
  run the native suite, so a docs-only pull request cannot break the
  README check for the next code one.
- `docs/superpowers/specs/2026-09-09-integration-coverage.md` lines citing
  Ctrl+0 and Ctrl+Tab get a note that neither exists; the notes document's
  "never shown in the UI" line is corrected.
- `tests/integration/cases/50-window.ps1:466-470`: the Help id list gains
  2033 at the front and the File list 2034 after New Session; the "Help 3"
  and "File 11 items" detail strings are corrected.

Not changed: which chords exist (no Ctrl+Tab, no Ctrl+0, no approve or
deny keys, by the earlier decisions), and the focus rule that main-window
chords need the main window focused. Both go on the notes open list.

## 3. Tests

### 3.1 Native

- `tests/test_shortcuts.c`: every row has a non-empty section and action,
  and a chord unless flagged as a menu row; sections are contiguous and in
  the fixed order; no chord appears twice within a section;
  `shortcuts_split_chord` splits `Ctrl+Shift+=` into three and `Ctrl+-`
  into two. The README check reads `README.md` from the working directory
  (skip with a printed note when absent, as the ConPTY fixture tests do),
  tolerates CRLF, finds the shortcuts table, and asserts that every chord
  in the code table appears in it and every `Ctrl+`/`Shift+`/`Alt+`/`F11`
  chord in that section appears in the code table. The source-scan check
  reads `src/ui/window.c` the same way: every `\t<accel>` in
  `create_app_menu` and every `vk == (WPARAM)'X'` in the Ctrl+Shift block
  of `key_on_keydown` must have a table row with that chord; a chord added
  to the handler without a row fails `make test`.
- `tests/test_cheatsheet_layout.c`: with the real table's section sizes,
  sections 1 to 4 are in column 0 and 5 to 7 in column 1; no rectangle
  overlaps another or leaves the client area; sizes at 96 and 192 DPI are
  within rounding of a factor of two (`ns_type_font_px` rounds: 13 and 27
  for body); the page fits a 1920 by 1080 work area at 144 DPI without a
  scrollbar and the test prints the first DPI at which it does not; at
  1366 by 718 and 120 DPI the window is clamped and the Close button's
  rectangle is inside it.
- `tests/test_ui_tokens.c`: the gate gains a `CreateFont(` scan of
  `src/ui/*.c` with an allow-list of zero (today no such test exists; the
  rule in CLAUDE.md was enforced by reading).

### 3.2 Harness, gate tier, `tests/integration/cases/50-window.ps1`

`NutshellIT.psm1`: `Nutshell_Shortcuts` joins `$script:DIALOG_CLASSES`
(`:505`) so `Wait-NutshellDialog` finds it.

- `help_shortcuts_sheet_opens_and_closes`: `IDM_HELP_SHORTCUTS` by
  `WM_COMMAND`, wait for the sheet, screenshot it by its own handle
  (`Save-NutshellScreenshot -Hwnd`; the default captures only the main
  window), non-blank, saved to artifacts; a second `IDM_HELP_SHORTCUTS`
  opens nothing more; Esc posted to the sheet's handle closes it.
- `help_shortcuts_sheet_fits_the_screen`: the sheet's rectangle lies within
  the monitor work area at the runner's DPI.
- `help_shortcuts_sheet_gallery`: the sheet captured on all four themes,
  into `artifacts\gallery\` beside the `ui_gallery` images, for the eye.

## 4. Open for the maintainer

1. The Mouse section: keep on a "keyboard" sheet, or drop?
2. A chord for the sheet: none (proposed). If wanted, it needs a runtime
   key label from `GetKeyNameText` and forwarding from the AI panel.

## 5. Critique

Round 1 (2026-10-20, Opus, 14 findings). Accepted and folded in: 1 (fit and
order: fixed split, scrollbar fallback, honest test), 2 (width rule, chips
in the body font, ellipsis), 3 (row corrections: Backspace, panel focus,
missing conditions and rows, † for the selection-dependent rows, "most
dialogs", wrong precedent), 4 (source-scan test; other hand-kept lists cut
to pointers), 5 (CI path filter, CRLF), 6 (chord dropped), 7 (capture the
sheet's own handle, DIALOG_CLASSES, Esc to the sheet; gallery as its own
case, no demo state), 8 (Close Tab id and command case, harness lists),
9 (guide section replaced by a pointer; AES text fixed), 10 (DPI change
without flush, re-clamp), 11 (floating panel disabled), 12 (rounding
tolerance), 13 (CreateFont scan added to the gate), 14 (trade-offs stated;
WM_QUIT re-posted). Rejected: none; "ship separately" from 14 is not taken
because the maintainer asked for it in this change and only the † rows
depend on the selection code. Not verified by the critic and still open:
the real text widths at each DPI (the layout test uses measured widths at
run time on Windows and fixed estimates natively); what Enter does in the
Paste preview and Settings dialogs.
