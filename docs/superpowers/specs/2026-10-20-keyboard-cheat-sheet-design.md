# Keyboard shortcuts cheat sheet

**Date**: 2026-10-20
**Status**: DRAFT, for the maintainer's review
**Branch**: `feat/putty-selection` (ships with the selection change, same
pull request)
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
- Two shortcut lists already exist and are both stale: the User Guide's
  "KEYBOARD SHORTCUTS" section (`src/ui/help_guide.c:349-375`) and the README
  table (`README.md:349-372`). Both miss Ctrl+Shift+M and the panel's list
  keys; neither says Enter stops a busy reply. Two specs cite chords that
  do not exist (Ctrl+0, Ctrl+Tab).
- Menu labels carry accelerator text for six chords; eight handlers have no
  menu text (Ctrl+Shift+W has no menu item at all).
- The closest dialog patterns: the User Guide (`help_guide.c`, a read-only
  EDIT in a themed modal window with a custom scrollbar) and About
  (`window.c:2763-2977`, custom-painted, layout in pure `src/core/
  about_layout.c`, Esc or Enter closes).

## 1. Behaviour

- **Help > Keyboard Shortcuts...** opens a modal, themed, custom-painted
  window sized to show every shortcut at once, two columns, no scrolling: a
  page to scan, not a document to read. Esc, Enter or its Close button
  closes it. It opens from the Help menu and with **Ctrl+Shift+/** (the
  `?` key with Ctrl, the chord GitHub and Slack use), which is listed on the
  sheet itself.
- **Sections**, in this order, each a heading followed by its rows:
  1. Sessions and tabs
  2. Copy, paste and selection
  3. Scrolling and zoom
  4. Keys the terminal sends to the shell
  5. AI Assist panel
  6. Window and help
  7. Mouse
- **A row** is the chord on the left in the mono font and a short
  description on the right. A condition, where one exists, follows the
  description in the dim text colour, for example "Ctrl+C — Copy the
  selection · without a selection: interrupt (^C)". Chords that change
  meaning with focus appear once per section where they apply.
- **One source of truth.** The rows live in a table in portable code
  (section 2.1). The sheet draws from it; the User Guide's shortcut section
  is generated from it at runtime; a native test checks the README table
  against it, so the three cannot drift apart again.
- Contents at the time of writing (the table is authoritative; this list is
  what the survey found and is what the implementer starts from):

  **Sessions and tabs**: Ctrl+Shift+T new session (opens the Session
  Manager); Ctrl+Shift+W close the active tab; Alt+F4 exit.
  **Copy, paste and selection**: Ctrl+C copy the selection · without a
  selection: interrupt; Ctrl+Shift+C copy; Ctrl+V / Ctrl+Shift+V paste;
  Shift+Insert paste; Edit > Select All (no chord: Ctrl+A goes to the
  shell).
  **Scrolling and zoom**: Shift+PgUp / Shift+PgDn scroll history a page;
  PgUp / PgDn scroll history · on the alternate screen (vim, less): sent to
  the program; Ctrl+= / Ctrl+- zoom the terminal font; Ctrl+wheel zoom.
  **Keys the terminal sends to the shell**: Ctrl+Space NUL; Shift+Tab
  back-tab; Alt+key ESC then the key; Ctrl+Alt+letter ESC then the control
  byte; F10 / Shift+F10 sent, not the menu; Edit > Send Key > Send Next Key
  Raw / with Alt one-shots; Backspace DEL (local: BS).
  **AI Assist panel** (focus inside the panel): Ctrl+Shift+Space show or
  hide the panel · from the terminal; Enter send · while a reply is
  streaming: stop it; Shift+Enter new line; Ctrl+V paste text, or attach
  an image from the clipboard; Ctrl+= / Ctrl+- zoom the chat; Ctrl+C copy
  the selected messages; Ctrl+A select all messages; Esc clear the
  selection; PgUp / PgDn, Home / End, Up / Down scroll the conversation.
  **Window and help**: F11 fullscreen; Ctrl+Shift+/ this sheet;
  Ctrl+Shift+M mark an issue in the debug log; Esc / Enter close a dialog.
  **Mouse**: drag select and copy; double-click select a word;
  triple-click select a line; drag past the edge scroll while selecting;
  right-click paste; Ctrl+wheel zoom; wheel scroll three lines.

## 2. Design

### 2.1 The table, `src/core/shortcuts.c` / `.h`

```c
typedef struct {
    const char *section;      /* "Sessions and tabs", ... */
    const char *chord;        /* "Ctrl+Shift+T" */
    const char *action;       /* "New session" */
    const char *condition;    /* "" or "without a selection: interrupt (^C)" */
} Shortcut;

/* Rows in display order; sections contiguous. */
const Shortcut *shortcuts_table(size_t *count);
/* Distinct section names in display order. */
size_t shortcuts_sections(const char **out, size_t cap);
/* The User Guide's shortcut section, "\r\n"-separated, written into buf;
 * returns the length it needs. */
size_t shortcuts_guide_text(char *buf, size_t cap);
```

Plain ASCII in the table; the sheet draws the separator between action and
condition itself.

### 2.2 The layout, `src/core/cheatsheet_layout.c` / `.h` (pure, tested)

Given the DPI, the row count per section and the measured widest chord and
widest description (the UI measures text; the layout only takes widths),
`cheatsheet_layout()` returns the window's client size and, for every
section heading and row, its column (0 or 1) and rectangle. Rules: two
columns of equal width; sections are placed in order, never split across
columns, into whichever column is shorter when the section is placed
(first fit, which gives the balanced two-column page); margins, gutters
and line heights from `ns_type.h` (`SP_*`, `FONT_HEADING` for the section
headings, `FONT_MONO` for the chord column, `FONT_BODY` for the rest). All
pixel sizes go through `ns_scale(px, dpi)`. The page's height is the
taller column plus margins; if that exceeds the work area at the current
DPI, the window is clamped to the work area and the Close button stays
visible (a scrollbar is out of scope; at 96 DPI the page is about 900 by
640 for the rows above, 192 DPI doubles it, within a 1080p work area in
both cases; the test pins this).

### 2.3 The window, `src/ui/cheatsheet_dlg.c`

Follows About: registers `Nutshell_Shortcuts`, custom-painted, themed
title bar and borders as the User Guide draws them, colours from
`ns_tokens()` only (`text_main` for actions, `text_dim` for conditions,
`border` for the rule under a heading, surface `.base` for the page), fonts
from `ns_font(role, dpi)`, one owner-draw Close button drawn by the
existing `draw_themed_button`. Modal loop with `IsDialogMessage`, closes
on Esc, Enter and Close. Re-measures on `WM_DPICHANGED`. Public entry
`cheatsheet_show(HWND parent)`.

### 2.4 Wiring

- `resource.h`: `IDM_HELP_SHORTCUTS 2033`.
- `create_app_menu()`: Help > "Keyboard Shortcuts...\tCtrl+Shift+/" as the
  first item, above User Guide.
- `key_on_keydown`: Ctrl+Shift+`VK_OEM_2` posts `IDM_HELP_SHORTCUTS`. The
  one-shot "Send Next Key Raw" bypasses it like every other chord.
- `help_guide.c`: the static KEYBOARD SHORTCUTS part is replaced by
  `shortcuts_guide_text()` at build time of the guide text (the parts are
  joined at `:487-508`; the generated part joins there).
- `README.md`: the table at `:349-372` is rewritten from the same rows, in
  the same section order.
- `tests/integration/cases/50-window.ps1:466-470`: the Help id list gains
  2033 at the front; the "Help 3" detail string is corrected.

### 2.5 Also fixed on the way, because the survey found them and the sheet
would otherwise list a chord with no menu item or a menu item that lies

- File gains "Close Tab\tCtrl+Shift+W" after New Session.
- Edit > "Copy\tCtrl+C" keeps its label; Ctrl+C's condition is on the
  sheet. Edit > Select All stays without a chord.
- `docs/superpowers/specs/2026-09-09-integration-coverage.md` lines citing
  Ctrl+0 and Ctrl+Tab get a note that neither exists; the notes document's
  "never shown in the UI" line is corrected.

Not changed: which chords exist (no Ctrl+Tab, no Ctrl+0, no approve or
deny keys, by the earlier decisions), and the focus rule that main-window
chords need the main window focused. Both go on the notes open list as
follow-ups.

## 3. Tests

### 3.1 Native

- `tests/test_shortcuts.c`: every row has a non-empty section, chord and
  action; sections are contiguous and in the fixed order; no chord appears
  twice within a section; `shortcuts_guide_text()` contains every chord
  and every section heading, and fits the guide's part limit; the README
  check: read `README.md` from the working directory (the ConPTY tests
  already read `tests/fixtures/...` relative to it; skip with a printed
  note when the file is not found, as they do), find the shortcuts table,
  and assert that every chord in the code table appears in it and every
  `Ctrl+`/`Shift+`/`Alt+`/`F11` chord in that README section appears in the
  code table.
- `tests/test_cheatsheet_layout.c`: with the real table's section sizes,
  every section is whole in one column; the columns differ by at most one
  section's height; no rectangle overlaps another or leaves the client
  area; sizes at 96 and 192 DPI scale by two; the 96-DPI page is under
  1000 by 700; the clamp to a 1366 by 768 work area keeps the Close button
  inside.
- `tests/test_ui_tokens.c`'s gate covers the new `src/ui` file for `RGB(`,
  `CreateFont` and `MulDiv` by construction.

### 3.2 Harness, gate tier, `tests/integration/cases/50-window.ps1`

- `help_shortcuts_sheet_opens_and_closes`: `IDM_HELP_SHORTCUTS` by
  `WM_COMMAND`, wait for the `Nutshell_Shortcuts` window, screenshot it
  (non-blank, saved to artifacts for the eye), Esc, the window is gone.
  Repeated with the chord Ctrl+Shift+/ through `Send-NutshellChord`.
- `help_shortcuts_sheet_fits_the_screen`: the sheet's rectangle lies
  within the monitor work area at the runner's DPI (192 on the
  maintainer's box).
- `ui_gallery` gains a `shortcuts` state so the sheet is captured on all
  four themes: `--ui-demo=shortcuts` opens the main window and the sheet.
  `src/core/ui_demo.c`'s STATE_NAMES and `30-ui-demo.ps1`'s hand-kept
  `$galleryStates` both gain it.

## 4. Open for the maintainer

1. Ctrl+Shift+/ as the sheet's own chord: keep, or Help menu only? It takes
   nothing from the shell on a US layout (Ctrl+Shift+/ has no control
   byte), but on layouts where `?` is elsewhere `VK_OEM_2` is a different
   key.
2. The Mouse section: keep on a "keyboard" sheet, or drop?
