# Settings help on hover: design

Date: 2026-11-05, revised 2026-11-10. Status: draft for the maintainer's
review, revised after critique rounds 1 and 2 (dispositions at the end) and
the 2026-11-10 addenda and the lazy-scrollback critique round. Source of
truth: `main` at `d73eeec` (v1.2.74), which merged PR #91
(`feat/trust-boundary-tool-gate`; its `settings.c`, `settings_layout.c`
and `src/term` are byte for byte those read at `297a9bc` and `f36d4f7`
during the revision), and so includes `4cd589f`
"feat(ai): automatic tool rounds in Settings" and `9e2f1b3` "fix(ui):
settings notes under the tool timeout and rounds", and so both new AI
Assistant > Behaviour rows, "Tool approval timeout (seconds):" and
"Automatic tool rounds:", each with a dim note under it. Source of truth
for the terminal buffer is the same tree: PR #91 did not change
`src/term`.

The request, verbatim: "add it as a settings param. when i hover over it,
provide a detailed explanation on what it does and how to use it. apply this
concept to all settings params".

## Goal

Every parameter on every Settings page shows a detailed explanation when the
pointer rests on its control or on its label: what it does, how to use it,
its default and allowed values, and what it interacts with. The tip matches
the dialog's theme.

**This PR is hover-only.** The page has no Tab order today, so a keyboard
user cannot reach a setting at all. Keyboard reach is one named follow-up,
"Keyboard navigation and accessible descriptions in Settings": Tab order,
F1 on the focused control showing its help in a tracking tip, and an
accessible description so a screen reader speaks the help when a control
takes focus (`IAccPropServices::SetHwndPropStr` with
`PROPID_ACC_DESCRIPTION`).

## What is there today

`src/ui/settings.c` already has one shared tooltip (`hTooltip`,
`TTS_BALLOON`, `TTM_SETMAXTIPWIDTH` 300 px unscaled, no delay setting,
created before the dialog's fonts and theme) and a `k_tooltips[]` table of
one-line texts keyed by control id, attached with
`TTF_IDISHWND | TTF_SUBCLASS` to the control only. Gaps found in the code:

- Labels have no tip: they are `SS_RIGHT` statics without `SS_NOTIFY`, so
  they answer `HTTRANSPARENT` and a subclassed tool never sees the mouse.
- No tip on the six Debugging level combos, the recorder level combo,
  Browse, Open, the folder-usage text or Current log file.
- Two controls can be disabled and then get no mouse messages, so their tip
  never shows: Session (`IDC_AUTOCONNECT_COMBO`, disabled while
  Auto-connect at startup is unticked) and the recorder level combo
  (`IDC_DBG_RECORDER_LEVEL`, disabled while "Keep recent detail ..." is
  unticked). The refresh button is also disabled, briefly, while it
  fetches.
- Model is `CBS_DROPDOWN`: the pointer over its text part is over the
  combo's child edit, which the combo's tool does not see.
- The tip is Windows-styled (light) on all four themes; its font is the
  system tooltip font, not `ns_font`; its width ignores DPI; it pops down
  after the default 5 s, too short for a paragraph.
- Some texts are wrong or have drifted from README: Paste Delay says
  "between characters" (it is between lines, `window.c`
  `paste_send_next_line`); Colour Scheme mentions foreground/background
  overrides that have no UI; README's group table puts Debugging under
  General (the nav shows it as its own top-level entry,
  `settings_layout.c`); README gives no default for Paste Delay (350).
- The search-provider label/value table (`k_search_providers[]`) lives in
  `settings.c`, out of reach of the native tests.
- The out-of-range rule for typed numbers is inline in the Save handler,
  four times: Idle Timeout, Max Results, Tool approval timeout and
  Automatic tool rounds keep the stored value; the rest are clamped by
  `settings_validate()`.
- The page has no keyboard navigation: `settings_dlg_run()`'s loop has no
  `IsDialogMessage`, page controls have no `WS_TABSTOP` and the page host
  has no `WS_EX_CONTROLPARENT`.
- `settings_dlg_run()` has no re-entry guard. Its owner is disabled while
  it runs, but a posted `IDM_EDIT_SETTINGS` (the AI panel's Open Settings
  button posts one; so does the harness) still reaches the main window and
  opens a second dialog over the first.
- The dialog has no `WM_DPICHANGED` handler; it lays out at the DPI it was
  created on.
- **Scrollback Lines is not applied.** `window.c` creates every terminal
  with `term_init(rows, cols, 3000)`; `settings.scrollback_lines` is read by
  nothing but the loader and the dialog (the memory file's open item "the
  scrollback-size setting"). `term_init()` also allocates every row of the
  buffer, screen and scrollback, when the tab opens. This PR wires the
  setting and allocates scrollback rows only as lines are kept (Layout and
  behaviour changes C).
- **Start Logging needs an existing folder.** File > Start Logging
  (`on_log_toggle`) opens `<dir>\<time>_<name>.log` with `fopen` and does
  nothing visible when that fails: no folder is created on that path (only
  the config-only `logging_enabled` path, `open_session_log`, calls
  `CreateDirectoryA`). The same happens when Log Name Format puts a `\` or
  `/` in the name (a subfolder that does not exist) or a character Windows
  refuses.
- **Refresh models, custom provider:** the models address is derived from
  the typed Base URL: cut at `/v1/` plus `/v1/models` when it contains
  `/v1/`, else everything before the last `/` plus `/models`. A Base URL
  with no path (`https://host`) therefore becomes `https://models`. Noted
  for the maintainer; the help text describes the rule, not the bug.

## Decisions

### 1. Mechanism: the stock tooltip, classic-rendered in the theme's colours

**Chosen:** keep comctl32's `TOOLTIPS_CLASS`, render it classic and give it
the design-system colours and font, through a small reusable helper
`src/ui/ns_tip.{c,h}`:

- `ns_tip_create(owner, dpi, hfont, tokens)` creates the tip with
  `WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP` (no prefix processing, so `&`
  in "Sage & Sand" shows; always tip, so it shows whatever window is
  active), `WS_EX_TOPMOST`, plain rectangle, not `TTS_BALLOON`: a balloon's
  tail and rounded body waste room around a paragraph and sit over the
  control being read about; the plain tip opens below the pointer, as the
  AI panel's, the tab strip's and the eye button's tips do.
- `ns_control_classic(tip)` (`SetWindowTheme(tip, L"", L"")`, in
  `ns_draw.c`) so the tip honours `TTM_SETTIPBKCOLOR` /
  `TTM_SETTIPTEXTCOLOR`, which visual styles ignore. Background
  `raised.base`, text `text_main`, both through `theme_cr()`: no `RGB(`
  literal, and `text_main` on `raised` is already in the contrast gate.
  That the colours are honoured is a Windows check (checklist item 1).
- `WM_SETFONT` with `ns_font(FONT_BODY, dpi)`, the dialog's own font.
- `TTM_SETMAXTIPWIDTH ns_scale(360, dpi)` (about 60 characters a line),
  `TTM_SETMARGIN` `ns_scale(SP_XS, dpi)` all round.
- `TTM_SETDELAYTIME`: `TTDT_INITIAL` 500 ms, `TTDT_RESHOW` 100 ms,
  `TTDT_AUTOPOP` 32767 ms (the largest the message takes). The longest text
  is about 110 words, roughly 25 s of reading.
- **No `TTM_SETTITLE`.** A title is per tooltip window, not per tool, so it
  would have to be swapped on every `TTN_SHOW`. Instead the tip text's
  first line is a heading: the table's `title` when set, else the
  control's label; then a blank line, the explanation and the default
  line. `title` exists for a label that is only a fragment on screen: the
  recorder tickbox reads "Keep recent detail for issue marks, at" (its
  level combo finishes the sentence), so its title is "Keep recent detail
  for issue marks", and the 4 MB size is in the body.

**Why not a custom popup.** A popup drawn with the tokens (`R_CARD`
corners, `border` token, a shadow) would match the cards exactly, but it
costs a window class, its own hover timing (`ns_hover` + an `ns_motion`
timer), placement on the right monitor's work area, dismissal on every
input path, and roughly 300-400 lines plus tests, and it would lose the
tooltip role that screen readers know. The classic-rendered stock tip gets
the theme colours and font for about 15 lines. Its visual compromise is
the classic 1 px frame in the system window-frame colour (black); if the
Windows check finds it ugly, `NM_CUSTOMDRAW` can paint the edge in `border`
later.

**What it costs to keep the Windows look instead:** nothing to build, but
the tip stays light on the two dark themes, the one unthemed thing in the
dialog. Rejected.

**Theme coupling, and where the tip is created.** The dialog paints with
`d->theme` (the `ThemeColors` named by `settings.colour_scheme` when the
dialog opened); `ns_tokens()` is the main window's resolved set. They agree
today, because the theme changes only after Save closes the modal dialog,
but nothing enforces it. The tip therefore takes its colours from
`d->theme`: `settings.c` calls `ui_theme_resolve(d->theme, &tokens)` into a
local `ThemeTokens` and passes that to `ns_tip_create`. The tip is created
in `WM_CREATE` **after the theme block** (and after the fonts), not where
`hTooltip` is created today, before both.

**Where it attaches: one help id per control.** Every page control carries
its own `SettingsHelpId`, recorded when it is created (section 2), not one
per row. The tool set:

- each control and its label get a `TTF_IDISHWND | TTF_SUBCLASS` tool with
  the setting's text;
- label statics are created with `SS_NOTIFY` by `page_ctrl()`, so they
  answer `HTCLIENT` and the subclassed tool sees the mouse; `ns_tip_add()`
  changes no window's style;
- Model's child edit (`GetComboBoxInfo` `hwndItem`) gets a second tool with
  Model's text (`ns_tip_add_combo()`);
- a row's companion controls carry their own ids: Model's refresh button
  (`SH_AI_REFRESH`), Log folder's Browse (`SH_DBG_BROWSE`) and Open
  (`SH_DBG_OPEN`), Keep at most's usage text (`SH_DBG_USAGE`); the
  recorder level combo carries `SH_DBG_RECORDER` and the number boxes of
  the two Behaviour rows whose label is the row's `hCtrl` carry their
  setting's id;
- **disabled controls**: Session and the recorder level combo also get a
  rect tool on the page host (`ns_tip_add_rect()`): `uFlags =
  TTF_SUBCLASS` (no `TTF_IDISHWND`), `hwnd = d->hPage`, `uId` = the
  control id, `rect` = the control's rect in page-client coordinates.
  Mouse input over a disabled child goes to its parent, so the page host's
  subclass sees it. `relayout_page()` sends `TTM_NEWTOOLRECT`
  (`ns_tip_move_rect()`) for both after it positions their row, with an
  empty rect while the row is hidden (another page, or Debugging
  unticked). While the control is enabled it takes the mouse itself and its
  own tool shows; the rect tool only fires while it is disabled. The
  refresh button's brief disabled spell gets no rect tool.

The dim hint statics (SSH idle hint, AI context hint, the two Behaviour
notes under Tool approval timeout and Automatic tool rounds, the Debugging
hints and note), the clear-text warning and the page title get no tip.

Expected tool count: **82** (Appearance 8, Terminal 5, Logging 4, SSH 2,
Startup 5, Provider 10, Behaviour 12, Web Access 7, Debugging 29), counting
the two rect tools and Model's child edit. The harness checks it.

**Keyboard.** Not in this PR; follow-up 1.

**Re-entry guard.** `settings_dlg_run()` keeps one `static HWND` for the
open dialog. A second call while it is open (`IsWindow`) brings that
dialog to the front (`SetForegroundWindow`) and returns 0 without creating
anything; `settings_dlg_show()` passes the result on, and
`on_settings_clicked_page()` in `window.c` returns at once on 0, so the
post-Save refresh (theme, renderer, fonts, AI panel) does not run for a
dialog that was never shown. The static is cleared when the dialog's loop
ends.

### 2. Where the texts live: `src/core/settings_help.{c,h}`

A table in `src/core` (portable, so the native suite tests it), keyed by a
stable id enum that `settings.c` uses when it creates each control:

```c
typedef enum {
    SH_NONE = -1,
    SH_COLOUR_SCHEME, SH_TERMINAL_FONT, SH_FONT_SIZE, SH_AI_FONT,      /* Appearance */
    SH_SCROLLBACK, SH_PASTE_DELAY, SH_PASTE_CONFIRM,                   /* Terminal */
    SH_LOG_DIR, SH_LOG_FORMAT,                                         /* Logging */
    SH_SSH_IDLE,                                                       /* SSH */
    SH_SESSION_MGR_STARTUP, SH_AUTOCONNECT, SH_AUTOCONNECT_SESSION,    /* Startup */
    SH_AI_PROVIDER, SH_AI_KEY, SH_AI_MODEL, SH_AI_REFRESH, SH_AI_BASE_URL, /* Provider */
    SH_AI_CONTEXT_LINES, SH_AI_SYSTEM_NOTES, SH_AI_MARKDOWN,           /* Behaviour */
    SH_AI_POLICY_ALLOWED, SH_AI_POLICY_AUTO, SH_AI_TOOL_TIMEOUT, SH_AI_TOOL_ROUNDS,
    SH_AI_WEB_FETCH, SH_AI_SEARCH, SH_AI_SEARCH_URL, SH_AI_MAX_RESULTS, /* Web Access */
    SH_DBG_ENABLE, SH_DBG_LEVEL_RENDER, SH_DBG_LEVEL_TERM, SH_DBG_LEVEL_AI, /* Debugging */
    SH_DBG_LEVEL_SSH, SH_DBG_LEVEL_SHELL, SH_DBG_LEVEL_GENERAL,
    SH_DBG_RECORDER, SH_DBG_SCREENSHOT, SH_DBG_CONTENT, SH_DBG_FOLDER,
    SH_DBG_BROWSE, SH_DBG_OPEN, SH_DBG_MAX_FILE, SH_DBG_MAX_TOTAL, SH_DBG_USAGE,
    SH_DBG_CURRENT,
    SH_COUNT
} SettingsHelpId;

typedef enum { SH_KIND_SETTING, SH_KIND_ACTION, SH_KIND_INFO } SettingsHelpKind;

typedef struct {
    SettingsHelpId   id;
    const char      *name;          /* "SH_COLOUR_SCHEME", via #id in the table macro */
    int              page;          /* SETTINGS_PAGE_* */
    SettingsHelpKind kind;
    const char      *label;         /* exactly as the dialog shows it */
    const char      *title;         /* tip heading when the label is a fragment, else NULL */
    const char      *text;          /* heading "\n\n" body ["\n" "Default: ..."] */
    const char      *default_text;  /* what follows "Default: ", NULL for actions/info */
    int              min, max;      /* numeric range, or 0, 0 */
    unsigned         flags;         /* SH_F_ZERO_MEANS_NEVER, SH_F_THOUSANDS, SH_F_KEEP_OUT_OF_RANGE */
} SettingsHelp;

#define SETTINGS_HELP_TEXT_MAX 700  /* bytes of `text`, terminator excluded */

const SettingsHelp *settings_help_get(int id);          /* NULL out of range */
const SettingsHelp *settings_help_table(size_t *count); /* dialog order */
```

- **Labels come from the table.** Label statics and tickboxes take their
  text from `settings_help_get(id)->label`, so the tip's heading and the
  label on screen are one string (or the label's completed form, `title`).
- **Every control is created through a helper that requires an id.**
  `page_ctrl(d, cls, text, style, ctrl_id, SettingsHelpId help)` creates a
  child of `d->hPage`, adds `SS_NOTIFY` when `cls` is `"STATIC"`, and
  records `(hwnd, help)` in `d->tips[]`; `make_label2`, `make_edit2`,
  `make_combo2`, `make_check2` become thin wrappers that take `help`, and
  the raw `CreateWindow` sites (the System Instructions edit, Browse, Open,
  the usage static, the Behaviour rows' label statics) call `page_ctrl`
  too. The refresh button is created today with `CreateWindowW(L"BUTTON",
  L"\x21BB", ...)` (`settings.c`, the Model row), a wide glyph the ANSI
  helper cannot carry, so it goes through `page_ctrl_w()`, the same helper
  with wide class and text; both record the id through one internal
  `page_track()`. The one path without an id is `make_hint(d, text)`, for
  the dim hints, the two Behaviour notes, the warning line and the About
  statics. One attach loop over `d->tips[]` replaces `k_tooltips[]` and
  `add_tooltip()`. The Debugging level rows use a
  `static const SettingsHelpId k_dbg_level_help[DLOG_CAT_COUNT]` naming
  each id, not `SH_DBG_LEVEL_RENDER + cat`, so every enumerator appears in
  the source (the scan relies on it).
- **The search providers move to `src/core`.** `k_search_providers[]`
  (label and config value: None/`none`, DuckDuckGo (API)/`duckduckgo-api`,
  DuckDuckGo (HTML)/`duckduckgo-html`, Custom/`custom`) moves to
  `src/core/search_providers.{c,h}` with `search_provider_count()`,
  `search_provider_label(i)`, `search_provider_value(i)` and
  `search_provider_index(value)`; `settings.c` fills the combo and saves
  through them. Tests 3 and 5 read the same table.
- **The keep-the-old-value rule is one `src/core` helper.**
  `int settings_entry_or_keep(int parsed_ok, unsigned v, int lo, int hi,
  int zero_ok, int current)` in `src/core/settings_entry.{c,h}` returns `v`
  when it parsed and lies in `lo..hi` (or is 0 with `zero_ok`), else
  `current`. The Save handler uses it for Idle Timeout (0..10080), Max
  Results (1..20), Tool approval timeout (15..3600, zero allowed) and
  Automatic tool rounds (1..20), replacing the four inline tests; those
  four table entries carry `SH_F_KEEP_OUT_OF_RANGE`, and their texts say
  that an out-of-range number is not saved.
- **ASCII only.** `settings.c` uses the ANSI `TOOLINFOA`; a non-ASCII byte
  would be read in the system code page. The refresh button's on-screen
  glyph (`page_ctrl_w()`, above) is not a label; its table label is
  "Refresh models". A table label is the string the dialog shows: the
  timeout row reads "Tool approval timeout (seconds):" since `9e2f1b3`
  moved what 0 means into the note under it.
- **README: no generated block in this PR** (follow-up 6). This PR corrects
  README's Settings section by hand for the four drifts found: Debugging as
  its own nav entry in the group table, Paste Delay's default (350 ms),
  Scrollback Lines as the tip now states it (applies to tabs opened after
  Save, memory used only as lines are kept), and when the AI Assist
  font takes effect. Its existing links stay.

### 3. DPI and themes

- Width `ns_scale(360, d->dpi)`, margins `ns_scale(SP_XS, d->dpi)`, font
  `ns_font(FONT_BODY, d->dpi)`: the tip uses the DPI the dialog laid out
  at, so it always matches the dialog's text.
- The dialog has no `WM_DPICHANGED` handler and this PR adds none; the
  dialog and its tip keep their creation scale together on a second
  monitor, which is consistent. Follow-up 2 adds the handler and
  `ns_tip_set_dpi()` with its test.
- The font is a cached `ns_font` handle; the tip never deletes it.
- Colours: from `d->theme`, resolved once when the dialog opens ("Theme
  coupling"). Re-colouring a live tip (`ns_tip_set_theme()`) is follow-up
  4, with its test.
- No `RGB(`, `MulDiv(x, dpi, 96)` or `CreateFont` in `ns_tip.c`, so
  `tests/test_ui_tokens.c` needs no allow-list change.

### 4. Tests

**Native (`make test`):**

1. Coverage (`tests/test_settings_help.c`): `settings_help_table()` has
   `SH_COUNT` (46) entries, entry *i* has id *i* and `name` equal to its
   enumerator, every label is non-empty and unique within its page, every
   page but About has an entry, and the entries' pages never go back in
   nav order (`settings_nav_at()`). Order within a page is not checked.
2. Shape: `text`'s first line equals `title` when it is set, else
   `label`, and is followed by an empty line; the whole text is at most
   `SETTINGS_HELP_TEXT_MAX` bytes and the body at least 100; ASCII,
   printable apart from `\n`, no tab, no double space, no trailing space on
   a line, ends with `.`; a setting's body has 2 to 6 sentences (count of
   ". " plus the last).
3. Defaults are true: a setting's text ends with
   `"\nDefault: " default_text "."`, and `default_text` begins with the
   default rendered from `config_default_settings()` by a per-id formatter
   in the test (a parenthesis may follow, as in "empty (none chosen)"):
   numbers with thousands separators where `SH_F_THOUSANDS`, "on"/"off" for
   flags, theme and level names, "empty" for an empty string,
   `cmd_policy_stop_label()` for Allowed, and for Search Engine
   `search_provider_label(search_provider_index(default value))`. Model's
   text contains `ai_provider_model("anthropic")`. A default changed in
   `loader.c` without the help fails here.
4. Ranges are true (`tests/test_settings_entry.c` and the help test):
   each numeric entry's body contains `"<min> to <max>"` as formatted.
   For the four `SH_F_KEEP_OUT_OF_RANGE` entries, `settings_entry_or_keep()`
   with the entry's `min`/`max` returns `min`, `max` and a value between
   them unchanged, returns `current` for `min - 1`, `max + 1` and a parse
   failure, and for Tool approval timeout returns 0 for 0 and `current` for
   1. Font Size's text lists every `k_app_font_sizes[]` value; System
   Instructions' names `AI_NOTES_MAX - 1` characters.
5. Labels match: the `k`-th Debugging level label is
   `settings_debug_cat_label(k)` plus `":"`; the Allowed text names every
   `cmd_policy_stop_label()` from Read to Critical; the Search Engine text
   names every `search_provider_label(i)`; the Debugging level texts name
   every `settings_debug_level_label()`.
6. Source scan: `src/ui/settings.c`, read as `test_shortcuts.c` reads
   README.md (path relative to the repository root; skipped when not
   found, **failing under `CI`** when unreadable), contains the name of
   every enumerator from `SH_COLOUR_SCHEME` to `SH_DBG_CURRENT`.
7. Initial placement (`tests/test_settings_layout.c`): see Layout and
   behaviour changes A.
8. Scrollback for a new tab (`tests/test_settings_entry.c`): see Layout
   and behaviour changes C.
9. Lazy scrollback rows (`tests/test_term_scrollback.c`, new): see Layout
   and behaviour changes C, "Tests".

**wintest (`tests/test_ns_tip_win.c`, `xvfb-run -a make wintest`):** the
tip tests run under an activation context built from
`src/ui/nutshell.manifest` (`CreateActCtx` and `ActivateActCtx` in
`win_runner.c` around them, `DeactivateActCtx` and `ReleaseActCtx` after),
so they get comctl32 v6 as the exe does. When `CreateActCtx` fails
(`INVALID_HANDLE_VALUE`: the manifest is missing or unreadable from the
working directory), `win_runner.c` prints one `[FAIL]` line naming the
manifest, counts one failure and skips the tip tests, rather than running
them under comctl32 v5 or activating an invalid handle. That matters: `TTM_GETTEXT` taking
the buffer size in `wParam`, and acceptance of the V3 `TOOLINFO` size (with
`lpReserved`) that a v6 build passes, are v6 behaviour; under comctl32 v5
`TTM_ADDTOOL` with that size fails and the read-back is unbounded. Fixture:
a window holding a static created with `SS_NOTIFY`, an edit, a button, a
disabled combo and a `CBS_DROPDOWN` combo, with short texts ("a", "bb",
...).

- `ns_tip_create()`: the tip's style has `TTS_NOPREFIX` and `TTS_ALWAYSTIP`
  and not `TTS_BALLOON`; `TTM_GETMAXTIPWIDTH` is `ns_scale(360, 192)` at
  DPI 192; `TTM_GETDELAYTIME(TTDT_AUTOPOP)` is 32767.
- `WM_NCHITTEST` on the fixture static returns `HTCLIENT`.
- `ns_tip_add()` leaves every fixture window's style as it was (static,
  edit, button, combos).
- `TTM_GETTOOLCOUNT` matches the tools added; each tool's text read back
  with `TTM_GETTEXT` (size in `wParam`) is the text given;
  `ns_tip_add_combo()` adds the child edit as a tool.
- `ns_tip_add_rect()` on the fixture window for the disabled combo, then
  `ns_tip_move_rect()`: `TTM_GETTOOLINFO` returns the new rect.

Nothing in the wintest checks colours or pixels: Wine renders comctl32 its
own way, and whether the classic tip honours the colours is a Windows check.

**Harness (`tests/integration/cases/50-window.ps1`, tier `gate`):** case
`settings_help_tool_count`, started with `-nc`: `Open-NutshellSettings`,
then every `tooltips_class32` window of the process whose `GetParent` is
the Settings dialog (for a popup, `GetParent` returns its owner),
`TTM_GETTOOLCOUNT` on each. The tips are hidden until hovered and
`Get-NutshellWindows` lists visible windows only, so the case walks the
top-level windows itself: `[NutshellNative]::FindWindowEx([IntPtr]::Zero,
$after, "tooltips_class32", [IntPtr]::Zero)` in a loop until it returns
zero, keeping each handle whose `GetWindowThreadProcessId` is the scratch
process. `FindWindowEx`, `GetParent`, `GetWindowThreadProcessId`, `SendMsg`
and `IsWindow` are all already imported by `NutshellIT.psm1`. Expected: two, one
with 82 tools and the API key eye's tip with 1 (the eye's tip is created
with the edit as parent, so its owner is the dialog). A mismatch means a
control gained or lost a tool. The case also posts `IDM_EDIT_SETTINGS`
once more and asserts there is still exactly one Settings window (the
re-entry guard).

**Windows checklist (the maintainer, on the Windows box, all four themes):**

1. Hover each control and each label on every page: the tip appears after
   about half a second, in the theme's raised colour with body text, its
   heading on the first line, wraps at about 60 characters, stays up about
   30 s.
2. Hover Session with Auto-connect unticked, and the recorder level combo
   with its tickbox unticked: the tips show although the controls are
   disabled; tick them and the tips still show.
3. Hover Model's text area: its tip shows. Hover the API key's eye: "Hold
   to show password" still shows, not the key's help.
4. With Settings open, press the AI panel's Open Settings button: the open
   dialog comes forward and no second one opens.
5. Narrator on: hovering a control reads its help.
6. 100 %, 150 % and 200 % scaling: tip font matches the dialog's labels.
7. The classic 1 px frame looks acceptable on the dark themes (else open
   follow-up 5).
8. `--ui-demo=settings-debug` shows tips on the Debugging page.
9. At 100 % on a 1080p screen, Settings opens with a client area of about
   760 x 744 (the window about 776 x 783), centred on Nutshell's monitor
   and fully inside the work area. AI Assistant > Behaviour shows no page
   scrollbar, with System Instructions at its 6-row height. Debugging,
   ticked, fits too (658 of 688 px), so no page scrolls at this size, and
   making the window taller grows System Instructions as today. Write the
   measured client height in the PR body.
10. AI Assistant > Web Access reads Permit Web Fetch, Search Engine
    (Search URL under it for Custom), Max Results.
11. Set Scrollback Lines to 500 and Save, then open a new tab and print
    2,000 numbered lines (`seq 1 2000`). The buffer holds the window's
    rows plus 500 lines, and the newest is the prompt after `2000`, so
    scrolling back stops at about line 1,502 minus the window's height in
    rows: about line 1,478 in a 24-row window, 1,452 in a 50-row one
    (a prompt that takes two lines moves it by one). In a tab opened
    before the change (so at the earlier setting, 10,000 by default), the
    same output scrolls back to line 1.
12. Memory, by comparison (Task Manager, Details, the Memory column for
    nutshell.exe; it shows the whole process, not one tab, so read the
    change, not the figure). Note the figure, set Scrollback Lines to 100,
    Save, open a new tab and note the rise; set it to 50,000, Save, open
    another tab of the same size and note the rise. The two rises are
    about the same (under 1 MB apart: only the slot array differs), where
    with every row allocated at open the second would be about 200 MB
    larger on a 200-column tab. Then, in the 50,000 tab, print `seq 1 60000`: the
    figure grows as the buffer fills, by roughly columns x 20 bytes per
    line (about 200 MB for a 200-column tab), and further output adds
    nothing more.

### 5. Non-goals

- Any keyboard path to the help: F1, the tracking tip, Tab order,
  auto-show on Tab focus, accessible descriptions (follow-up 1).
- A `WM_DPICHANGED` handler for the Settings dialog, and re-colouring a
  live tip.
- Fixing Start Logging's silent failure or the Base URL derivation bug
  (reported above; separate fixes).
- Moving the other tooltips in the app onto `ns_tip`.
- Help for the Session Manager's profile editor (a recorded follow-up,
  settled 2026-11-10; see the scope note below).
- Config keys with no control in the dialog (`host_key_verification`,
  `logging_enabled`, `foreground_colour`, `background_colour`, the legacy
  `debug_terminal`): there is nothing to hover, so they get no tip.
- A generated README block (follow-up 6). Localisation.
- A clear-scrollback command, a lazy slot array, and any promise that
  freed rows shrink the working set Task Manager shows (Layout and
  behaviour changes C, "Chosen not to do").

### 6. Follow-ups

1. **Keyboard navigation and accessible descriptions in Settings.** Tab
   order (`IsDialogMessage`, `WS_TABSTOP`, `WS_EX_CONTROLPARENT` on the page
   host, `ES_WANTRETURN` on System Instructions so Enter there does not
   press Save); `PROPID_ACC_DESCRIPTION` set from the table for every
   control; and F1, designed in round 1 and kept here: a second tip with
   one `TTF_TRACK` tool (no `TTF_ABSOLUTE`) anchored below the focused
   control; F1 consumed in `settings_dlg_run()`'s loop before translation;
   the hover tip deactivated (`TTM_ACTIVATE`) while it shows; hidden on F1
   again, Escape (dispatched instead when a combo's list is open, per
   `CB_GETDROPPEDSTATE`, so the list closes first), a click, a focus change
   (`GetFocus()` before and after dispatch), a page switch, a scroll, the
   dialog's `WM_MOVE`/`WM_SIZE`/`WM_ENTERSIZEMOVE`, deactivation and
   `WM_DESTROY`; shown on a focus change that followed Tab. Its wintest
   and checklist items come with it.
2. `WM_DPICHANGED` for the Settings dialog: rescale fonts, relayout, and
   `ns_tip_set_dpi()` (width, margin, font) with a wintest reading
   `TTM_GETMAXTIPWIDTH` before and after.
3. *(Removed 2026-11-10: Scrollback Lines is wired in this PR, Layout and
   behaviour changes C. The number is kept so the other follow-ups keep
   theirs.)*
4. Move the AI panel, tab strip and eye-button tips onto `ns_tip`, adding
   `ns_tip_set_theme()` (re-colour a live tip) with a wintest reading the
   colours back.
5. `NM_CUSTOMDRAW` frame in the `border` token, if checklist item 7 fails.
6. A generated "Settings reference" in README from the table, compared byte
   for byte as the shortcuts block is. The generator must escape for
   Markdown: wrap `%` codes, paths and file names in backticks
   (`%Y-%m-%d_%H-%M-%S` would otherwise italicise between underscores),
   keep `<time>_<session>.log` out of raw HTML (backticks or `&lt;`),
   double or backtick backslashes (`%LOCALAPPDATA%\Nutshell\logs`), keep
   square brackets (`[Disconnected after N min idle]`) from forming links,
   and escape `|` if the output is ever a table.
7. Start Logging: create the folder or say why logging did not start.

(Follow-up 8, lazy scrollback rows, was removed on 2026-11-10: the
maintainer chose to do it in this PR, Layout and behaviour changes C.)

## Layout and behaviour changes

Two requests from the maintainer's Windows test on 2026-11-10 (A and B),
and the Scrollback decisions of the same day (C), all in the same PR. They also confirmed the scope of the help: every control in the
Settings dialog gets hover help. They called out the two new Behaviour
rows as lacking it today. That matches the code: "Tool approval timeout"
has a `k_tooltips[]` entry only on its number box, because its label is a
static without `SS_NOTIFY`, and "Automatic tool rounds" has no entry at
all. Both are covered by section 1.

### A. The window opens tall enough for the Behaviour page

**Today.** `settings_dlg_run()` opens at a fixed client size of
`ns_scale(760, dpi)` x `ns_scale(560, dpi)` at `CW_USEDEFAULT`. A page is
the title row plus its rows (`SettingsMetrics`: `pad` 14, `row_h` 30,
button bar 56, at 96 DPI). The content pane is the client height minus the
button bar. A page taller than the content pane scrolls. System Instructions
is not a fixed large height: it has a 6-row minimum (`rows` 6) and
`stretch_v`, so `relayout_page()` already gives it every leftover row. What
makes Behaviour scroll is the window height, not the box.

**Numbers at 100 % (96 DPI)**, every conditional row shown:

| Page | Rows (title included) | Page height | Client height it needs |
|---|---|---|---|
| AI Assistant > Behaviour | 22 (title, Max Terminal Lines, its 4-row hint, System Instructions 6, Render AI markdown, Allowed, Auto-approve, Tool approval timeout and its 2-row note, Automatic tool rounds and its 3-row note) | 28 + 22 x 30 = 688 | 688 + 56 = 744 |
| Debugging, ticked | 21 (`settings_debug_layout(1, ...)`) | 658 | 714 |
| Provider, custom with the warning | 7 | 238 | 294 |
| any other page | 5 or fewer | 178 or less | 234 or less |

Today's client is 560, so the content pane is 504: Behaviour scrolls by
184 px and Debugging (ticked) by 154 px. Only Behaviour must open
unscrolled; the other rows are for reference. The row counts are those of
`settings.c` at `297a9bc`: the `rows` argument of each `add_ctrl()` call on
the page, plus the title row (the two notes added by `9e2f1b3` take 2 and
3 rows), and `settings_debug_layout(1, ...)` for Debugging.

**The rule (maintainer's correction, 2026-11-10).** The window opens just
large enough that AI Assistant > Behaviour needs no scrolling: its natural
height with System Instructions at its 6-row minimum, plus the button bar.
That height is capped by the work area less the frame and 24 px margins,
and never goes below today's minimum. The window is **not** sized to the
tallest page. Every page keeps its scrolling wherever its height exceeds
the page area.

```
target_h  = settings_page_height(behaviour_rows, m) + m.btnbar_h
avail_h   = work_h - nc_h - 2 * ns_scale(SP_XL, dpi)
client_h  = max(m.min_h, min(target_h, avail_h))
client_w  = max(m.min_w, min(ns_scale(760, dpi), work_w - nc_w - 2 * ns_scale(SP_XL, dpi)))
```

- `behaviour_rows` is Behaviour's row count with System Instructions at its
  minimum: 22 at `297a9bc`. `settings.c` sums it from the `rows` of
  `d->ctrls[]` entries on `SETTINGS_PAGE_AI_BEHAVIOUR` after the build,
  plus the title row, so a row added to Behaviour later is counted.
- `work_*` is the work area of the owner's monitor
  (`MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST)`, `rcWork`).
  `nc_w`/`nc_h` come from `AdjustWindowRect` on the dialog's style, as
  today. The window is centred on that work area and moved to lie inside
  it, with top-left winning when it cannot fit. This replaces
  `CW_USEDEFAULT`, whose cascade could push the taller window past the
  bottom of the screen.
- `m.min_h` is today's floor: 420 at 96 DPI, or more if the nav list
  needs it.
- System Instructions keeps its current behaviour: a 6-row minimum plus
  `stretch_v`, so it grows into any spare rows when the user makes the
  window taller. At the initial size there are none on Behaviour.
- Scrolling is unchanged on every page.

**Result at 100 % on a 1080p screen.** The work area is 1032 high with
Windows 11's 48 px taskbar (1040 with Windows 10's 40). The non-client
height is about 39.
- `avail_h` = 1032 - 39 - 48 = 945, so `client_h` = **744**. The window is
  about 776 x 783.
- Behaviour's page area is 688, exactly its height, so it does not scroll.
- Debugging, ticked, is 658 high, so it fits too, with 30 px to spare.
- Every other page fits, so at this size no page scrolls.

Not required, but stated so nobody is surprised:
- At 150 % on the same screen (work area 1008 high, non-client about 57),
  `avail_h` caps the client at 879. Behaviour needs 1,116 (page 1,032 plus
  the 84 px button bar) and scrolls by about 237 px; Debugging, ticked,
  by about 192 px.
- On a 1366 x 768 laptop at 100 % (work area 728 high), the client is
  capped at 641 and Behaviour scrolls by about 103 px.

**A pure function in `src/core`.** `settings_layout.{c,h}` gains:

```c
/* Initial client size and window position for the Settings dialog: the
 * rule above. target_rows is the row count (title included) of the page
 * that must open without scrolling (Behaviour); work_* the monitor work
 * area; nc_* the non-client size from AdjustWindowRect. Writes the client
 * size and the window's top-left. */
void settings_initial_placement(const SettingsMetrics *m, int target_rows,
                                int work_x, int work_y, int work_w, int work_h,
                                int nc_w, int nc_h,
                                int *client_w, int *client_h,
                                int *win_x, int *win_y);
```

Test 7 goes in `tests/test_settings_layout.c`, which exists. All cases are
at 96 DPI unless stated:
- 22 rows, a 1920 x 1032 work area and non-client 16 x 39 give a client of
  760 x 744 and a window top-left of (572, 124): centred, inside the work
  area.
- With the same inputs, `settings_scroll_max(client_h - btnbar_h,
  settings_page_height(22, m))` is 0 (Behaviour does not scroll), and so
  is `settings_scroll_max(688, settings_page_height(21, m))` (Debugging,
  ticked, fits).
- 17 rows on the same work area give a client height of 594: the height
  follows the row count it is given.
- A 1366 x 728 work area gives a client height of 641 (the cap), and
  `settings_scroll_max(641 - 56, 688)` is 103.
- A 640 x 400 work area gives `m.min_w` x `m.min_h` (620 x 420). The
  window, 636 x 459, fits across but not down: x stays centred at 2 and y
  is pinned to the work area's top, 0.
- A 700 px wide work area clamps the client width to 700 - 16 - 48 = 636.
- At 144 DPI on a 1920 x 1008 work area with non-client 24 x 57, the
  client height is capped at 1008 - 57 - 72 = 879, and the window lies
  inside the work area.
- A work area with a negative origin (-1920, -200, 1920 x 1032) places
  the window inside it, not at (0, 0).

### B. Web Access: Permit Web Fetch first

The page reads, top to bottom: **Permit Web Fetch**, **Search Engine**,
**Search URL** (still shown only for Custom, directly under Search
Engine), **Max Results**. In `settings.c` the `IDC_AI_WEB_FETCH` block
moves above the `IDC_AI_SEARCH_COMBO` block, since `relayout_page()`
places rows in `d->ctrls[]` order. The enum's Web Access group, the help
texts below and README's "AI Assistant > Web Access" list follow the same
order. No text changes: each tip already stands alone. Test 1 checks page
order only, so it is unaffected.

### C. Scrollback Lines takes effect, with rows allocated as lines are kept (maintainer's decisions, 2026-11-10)

`create_session()` in `window.c` calls `term_init(rows, cols, 3000)`.
After this change it calls `term_init(rows, cols,
settings_scrollback_for_new_tab(g_config ? &g_config->settings : NULL))`. A tab therefore
gets the configured Scrollback Lines when it is opened. Tabs already open
keep the size they were created with. The default stays 10,000.

The maintainer's second decision of the same day: **use lazy
allocation**, in this PR, so that a tab's memory follows the lines it
actually keeps rather than the setting.

**The pure lookup.** `src/core/settings_entry.{c,h}` gains
`SETTINGS_SCROLLBACK_MIN` (100), `SETTINGS_SCROLLBACK_MAX` (50000),
`SETTINGS_SCROLLBACK_DEFAULT` (10000) and
`int settings_scrollback_for_new_tab(const Settings *s)`:
- `NULL` gives the default;
- otherwise `scrollback_lines` is clamped to 100..50000, the same clamp
  `settings_validate()` applies.

`loader.c`'s default and clamp use the three constants instead of their
literals.

**Memory today, checked against `src/term/term.h` and
`src/term/buffer.c`.** Sizes measured with gcc and MinGW x64 on
2026-11-10:
- `TermCell` is **20 bytes**: a `uint32_t` code point plus a `TermAttr`
  (two `uint32_t` colours and five `uint8_t` fields, padded to 16).
- `TermRow` is **32 bytes**: the `cells` pointer, `width`, `len`,
  `dirty` and `wrapped` (padded), and the 64-bit `gen`. Its cells are a
  second heap block of `cols` x 20 bytes.
- A ring slot is one `TermRow *`, **8 bytes**. `lines` holds
  `lines_capacity` = rows + max_scrollback of them.
- `term_init()` allocates and fills a row for every slot up front
  ("Pre-allocate all rows to avoid allocation during runtime"), and a
  resize reflows into a second complete buffer (`term_reflow_buffer()`
  allocates `new_rows + max_scrollback` rows) before it frees the old
  one. So today a tab costs its whole buffer from the moment it opens,
  and twice that for the moment of a resize.

**Lazy allocation: the design.** The ring itself does not change. `lines`
is still an array of `lines_capacity` slots indexed modulo
`lines_capacity`, and every mapping that uses it (`term_screen_to_phys()`,
`term_view_to_logical()`, `term_max_scroll_offset()`, `lines_recycled` and
the selection's absolute line numbers) is unchanged. What changes is which
slots hold a row.

1. **The invariant.** Call the live part of the ring the
   `term_logical_rows()` slots from `lines_start`, modulo `lines_capacity`:
   that is `lines_count` slots, or `rows` slots in the sparse state where
   `lines_count < rows`. Every slot in the live part holds a row,
   `term->cols` cells wide; every slot outside it is NULL. The product
   never enters the sparse state (`term_init()` sets `lines_count` to
   `rows`, `term_scroll()` only raises it, and `term_reflow_buffer()`
   restores `lines_count >= rows`), so in the running app the live part is
   the `lines_count` slots, the visible screen lies inside it, and the
   number of rows allocated equals `lines_count`. Seven existing tests
   force the sparse state by lowering `lines_count` by hand
   (`test_dirty.c:258, 287, 312`, `test_term.c:965, 996`,
   `test_selection.c:345`, `test_term_extract.c:155`). After such a write
   the slot just past `lines_count` can hold a live screen row, and a test
   that lowers `lines_count` after a resize can leave rows outside the
   live part too. The code therefore never assumes a slot outside the live
   part is NULL: growth allocates only into a NULL slot (step 3), and
   `term_resize()` and `term_free()` free every non-NULL slot of the whole
   array, not only the live part. `TermRow.len` stays the content
   boundary; nothing here changes how a row's cells are read.
2. **At tab open.** `term_init()` allocates the slot array (zeroed, so
   every slot starts NULL) and the `rows` rows of the screen, nothing
   else. These allocations stay on `xmalloc`/`xcalloc`, which end the
   process when memory runs out, as every other allocation made when a
   tab opens does today. Making `term_init()` fallible is out of scope.
3. **The point a scrollback row gets its cells.** The one place is
   `term_scroll()`, in the branch where the ring is not yet full
   (`lines_count < lines_capacity`). If the slot just past the live part,
   `(lines_start + lines_count) % lines_capacity`, is NULL, a new row is
   allocated there (the `TermRow`, then its `cols` cells); if it already
   holds a row (only in a sparse state a test forced), that row is reused
   as today. Either way the row is filled with `current_attr` exactly as
   `term_row_fill()` fills a row today, before `lines_count` is raised.
   `term_scroll()` has exactly two callers: `term_put_char()`'s auto-wrap
   past the bottom row (`parser.c`, which calls it directly, whatever the
   scroll region), and `term_scroll_up()` when its region is the whole
   screen on the primary screen (`buffer.c`). The parser reaches that
   whole-screen `term_scroll_up()` from a line feed, IND or NEL on the
   bottom margin, from `CSI S`, and from `CSI M` (delete lines) with the
   cursor on row 0, each while the scroll region is the whole screen. So
   one row is allocated for each line pushed into history while the
   buffer holds fewer lines than its limit. Once the ring is full, the
   existing branch recycles the oldest row in place: from then on there
   is no allocation and no free.
4. **Readers.** The invariant is the primary defence: every reader stays
   inside the live part, so none of them meets a NULL slot. The parser
   writes through `get_screen_row()` (screen rows). `term_scroll_up()`,
   `term_scroll_down()`, the dirty-row helpers and `term_cursor_row_text()`
   use screen rows through `term_screen_to_phys()`. The renderer's
   `get_visible_row()` uses `term_view_to_logical()`, whose result lies in
   the live part. The selection and `term_extract.c` stop at
   `term_logical_rows()`. The scrollbar reads only `lines_count`, and
   `ai_chat_dispatch.c`'s stall diagnostic reads the cursor's screen row.
   As a second line of defence, most of them already treat a NULL row as
   absent: the renderer paints the row's background, `sel_row()`'s
   callers and `term_extract.c` skip it, `get_screen_row()` returns NULL
   and its callers test it, and `term_clear_dirty()`,
   `term_has_dirty_rows()`, `term_dirty_row_flags()`,
   `term_mark_all_dirty()`, `term_cursor_row_text()`, the parser's
   previous-cursor-row repaint and the stall diagnostic all test the slot.
   Three sites do not, and this PR adds a guard to each, so that a NULL
   row reads as blank or is skipped:
   - `selection_select_all()`, which passes each row to `sel_row_blank()`
     (`selection.c:176-180`, called at `:338-339`): `sel_row_blank()`
     treats a NULL row as blank;
   - `term_scroll()`'s loop that marks the visible rows dirty
     (`buffer.c:411-414`): a NULL slot is skipped;
   - the save, refill and stamp loops of `term_scroll_up()` and
     `term_scroll_down()` (`buffer.c:472-541`): a NULL row is moved like
     any other pointer, but not refilled, marked or stamped.
   None of the three guards can be reached through the public API or the
   fake allocator while the invariant holds. Their tests reach them from
   a constructed state (a test sets one screen slot to NULL by hand and
   puts the row back before `term_free()`), and the guards stay even so.
   `local_pty.c` (the ConPTY path) and the SSH and telnet I/O never touch
   rows: they get the size `window.c` passes after a resize.
5. **Allocation failure while the buffer grows.** The allocation in step
   3 can fail without ending the process: it goes through a fallible
   allocator (step 8), and when a row's cells cannot be allocated its
   `TermRow` is freed first. On failure `term_scroll()` does what it does
   on a full ring, plus one pointer move: the oldest row (at
   `lines_start`) is cleared and moved into the slot past the segment, its
   old slot is set to NULL, `lines_start` advances, `lines_recycled` rises
   by one and `lines_count` stays as it is. The tab keeps working with the
   history it already holds, and the oldest line is dropped as it would be
   in a full buffer. A slot is NULL only past a live part of at least
   `rows` rows, so there is always a row to move. `Terminal` gains `bool grow_failing`: it is set on a failure,
   and when it goes from false to true a `DLOG` warning
   `term.scrollback.alloc_failed` records `lines_count` and
   `lines_capacity`. The next line tries again, and the first success
   clears the flag, so the buffer grows again once memory is available.
6. **Resize and reflow.** `term_reflow_buffer()`'s counting pass already
   knows how many output rows the text needs (`total`, less the `dropped`
   oldest rows). The writing pass now allocates `max(needed, new_rows)`
   rows, not `new_rows + max_scrollback`, then writes them. The
   trailing-row strip treats a NULL slot as empty. After the strip and the
   padding on growth, the spare blank rows outside the new segment are
   moved into the padding slots and any left over are freed, so the
   invariant holds on the new ring. Allocation here is fallible and all or
   nothing: the slot array and every row the reflow needs are allocated
   before anything is changed, and on a failure what was obtained is freed
   and the old buffer is left untouched. `term_reflow_buffer()` therefore
   no longer frees the old buffer itself; `term_resize()` does, once every
   ring involved has been reflowed. On the alternate screen that means
   both the alternate ring and the saved primary are reflowed before
   either is committed. `term_resize()` returns `bool`: false when it
   could not allocate, with the `Terminal` unchanged (rows, cols, both
   rings, the cursors, `epoch`, `lines_recycled`) and a `DLOG` warning
   `term.resize.alloc_failed`; true otherwise, including the existing
   no-op cases. The warning is written once per failing streak:
   `Terminal` gains `bool resize_failing`, set by a failed resize and
   cleared by the next one that succeeds, and the warning is written only
   when it goes from false to true, so a window drag that fails on every
   `WM_SIZE` logs one line. The old buffer, once replaced, is freed slot by
   slot over the whole array. Its two callers in `window.c` (the grid sync and
   `WM_SIZE`) then skip `term_mark_all_dirty()`, `dispbuf_resize()` and
   `io.resize()`, so the terminal, the display buffer and the remote end
   keep the old size together, and the next `WM_SIZE` or grid sync tries
   again. There is no retry timer. The limitation, stated plainly: after a
   failed shrink the terminal keeps more rows or columns than the client
   area shows, so its bottom rows (the cursor's among them) are clipped
   until a resize succeeds; after a failed grow it fills less of the
   window. Making a shrink allocation-free is not attempted in this PR.
   The peak during a resize is the old rows plus the new: about
   twice the lines actually kept, where today it is twice the whole
   buffer.
7. **The alternate screen.** Entering it allocates `rows` rows, as today:
   a small, fixed cost. This allocation becomes fallible too. On failure
   the terminal stays on the primary screen (`alt_screen_active` false, a
   `DLOG` warning `term.alt_screen.alloc_failed`): the full-screen program
   draws on the primary screen, its output reaches the scrollback, and the
   matching `?1049l` does nothing. While the alternate screen is active,
   the saved primary keeps its segment and its NULL slots as they were.
8. **The test hook.** The fallible allocations (steps 5 to 7) go through
   one allocator in `buffer.c`, replaceable from tests the way
   `tests/fake_dpapi.c` replaces the DPAPI backend:
   `typedef void *(*TermCallocFn)(size_t nmemb, size_t size);` and
   `void term_set_calloc(TermCallocFn fn);` in `term.h`, where NULL
   restores `calloc`. Product code never calls it.
9. **When memory is given back.** Rows are freed when the tab closes
   (`term_free()` frees every non-NULL slot of the whole array of both
   rings, as it does today), when the
   alternate screen is left (its rows, as today), and on a resize (the old
   buffer; the new one holds only the rows the text needs). Nothing else
   frees rows: a full ring keeps its rows and reuses them. Nutshell has no
   clear-scrollback command today (the parser ignores `ESC[3J`, ED 3, and
   there is no menu item), so there is nothing to hook; `ESC[2J` clears
   the screen's rows in place and frees nothing. Freed rows go back to the
   C runtime's heap; whether the working set Task Manager shows shrinks
   with them is up to the heap, and the design does not promise it.

**Chosen not to do.**
- The slot array is not made lazy. It costs 8 bytes per line of the
  setting per tab (80 KB at 10,000, 400 KB at 50,000), and growing it
  would change the modulo that every ring mapping uses.
- Rows are not trimmed to their `len`: the renderer reads every cell up to
  `cols`, and a row's background fill lives in the cells past `len`.
- The resize peak is not lowered further by freeing old rows as they are
  consumed.
- No clear-scrollback command is added.
- `term_init()` keeps ending the process when memory runs out, like every
  other allocation made when a tab opens.
- Tabs already open are not resized when the setting changes.

**Memory a user sees, from the sizes above.** A kept line costs
`32 + cols x 20` bytes for its row plus 8 for its slot (the heap's own
header on each of the two blocks is not counted): 4,040 bytes on a tab
200 columns wide, 1,640 on 80 columns.

| Scrollback | 200 x 50, at open | 200 x 50, full | 80 x 24, at open | 80 x 24, full |
|---|---|---|---|---|
| 3,000 (today, eager) | about 12 MB | about 12 MB | about 5 MB | about 5 MB |
| 10,000 (default, lazy) | about 0.3 MB | about 41 MB | about 0.1 MB | about 16 MB |
| 50,000 (maximum, lazy) | about 0.6 MB | about 202 MB | about 0.4 MB | about 82 MB |

At open it is `lines_capacity x 8` for the slots plus the screen's rows
(10,050 x 8 + 50 x 4,032 = 282,000 bytes at the default on 200 x 50).
Full, it is `lines_capacity x 4,040` on 200 columns (10,050 x 4,040 =
40,602,000 bytes). In between, the cost is the lines kept times the
per-line figure: a 200-column tab that has printed 2,000 lines uses about
8 MB at any setting of 2,000 or more. The Scrollback tip quotes the full
figures for 200 columns.

**Tests.** Native test 8 in `tests/test_settings_entry.c` covers:
- `settings_scrollback_for_new_tab()` for `NULL` (10,000), the default
  config (10,000), 99 (100), 100, 50,000, 50,001 (50,000) and a negative
  value (100);
- that the helper and `settings_validate()` agree on 99 and 50,001.

Native test 9 is a new `tests/test_term_scrollback.c`, written before the
change (`src/term` is portable and built natively). Every case checks the
invariant with a local helper `ring_ok(t)`: each slot in the live part is
non-NULL, each other slot NULL, and the same for the saved primary while
the alternate screen is active. The behavioural check of memory is
`rows_held(t)`, the count of non-NULL slots. Allocator calls are counted,
and failures injected, through `term_set_calloc()` with a fake that counts
calls and fails the n-th call or every call; each test restores NULL
before it ends.
- Fresh tab: `term_init(50, 200, 10000)` holds 50 rows and
  `lines_capacity` 10,050; at 50,000, 50 rows and 50,050. The test also
  asserts that the bytes computed from `sizeof` (slots plus 50 rows of 200
  cells) are 282,000 and 602,000: those two asserts document the
  arithmetic behind the spec's figures and test no behaviour.
- Growth one row at a time: `term_init(4, 10, 100)`; after the three line
  feeds that reach the bottom row there are 4 rows, and each further line
  feed adds exactly one row, with `lines_count` following, for ten lines.
- Wrap-around when full: `term_init(4, 10, 6)` (capacity 10), then `L01`
  to `L30`, each followed by `\r\n`, leaves 10 rows, `lines_count` 10,
  `lines_recycled` 21, and the oldest row reading `L22`. Ten more lines
  make no allocator call.
- Resize with a partly filled buffer: `term_init(5, 10, 100)`, twelve
  10-character lines and one of 25 characters, then resized to 5 x 20
  (wider), 5 x 4 (narrower), 8 x 10 (taller, the padding path) and 3 x 10
  (shorter). After each, `ring_ok`, the rows allocated equal
  `lines_count`, and the select-all text (wrapped rows joined) is the text
  from before the first resize. A 3-row terminal with one line of
  scrollback, holding two lines and resized to 6 rows, covers padding
  where the padding slots overlap the spare rows; its text is the old text
  under the four blank rows the padding prepends (growing pads above the
  content), so the test expects exactly that. The same on the
  alternate screen: enter, resize, leave, and the primary's text and
  `ring_ok` hold.
- Allocation failure while growing: with every call failing, filling the
  screen of `term_init(4, 10, 100)` and ten more lines does not crash;
  `lines_count` stays 4, 4 rows are allocated, `lines_recycled` is 10,
  `grow_failing` is true, and the screen shows the last four lines. A
  failure of the cells block alone (the second call) leaks nothing under
  `make test SAN=1`. With the allocator restored, the next line feed adds
  a row and clears `grow_failing`.
- Allocation failure during a resize: the n-th allocator call of a reflow
  fails, for the first, the second, one in the middle and the last
  (counted on an identical terminal first). `term_resize()` returns false,
  and rows, cols, `lines_count`, `lines_start`, `lines_recycled`, `epoch`,
  the cursor and the text are unchanged, with nothing leaked;
  `resize_failing` is true after a failure, stays true through a second
  failing resize, and is false after the next successful one. On the
  alternate screen, a failure in the primary's reflow leaves both rings
  unchanged and the terminal still on the alternate screen.
- Allocation failure entering the alternate screen: `ESC[?1049h` leaves
  `alt_screen_active` false, the next text lands on the primary screen,
  and `ESC[?1049l` changes nothing.
- Growth through delete-lines at the top row: `term_init(4, 10, 100)`,
  `A` to `D` on the four rows, then `ESC[H` and `ESC[2M` (`CSI M` on row
  0 with the whole screen as the region): `lines_count` and `rows_held`
  are 6, and the two oldest rows read `A` and `B`.
- A live part that wraps past the end of the array, with NULL slots in
  the gap: `term_init(5, 10, 100)` (capacity 105), `A`, `B` and `C` on
  three rows, then a resize to 10 x 10. The reflow needs 3 rows, pads 7
  at the end of the new 110-slot array, and leaves `lines_start` 103,
  `lines_count` 10, slots 103 to 109 and 0 to 2 live and slots 3 to 102
  NULL. Twenty line feeds then grow the live part into slots 3 to 22
  (`lines_count` 30), and a resize to 5 x 4 reflows the wrapped ring into
  slots 0 to 29 of a 105-slot array with `lines_start` 0. After each
  step `ring_ok` holds; at the end logical rows 7, 8 and 9 read `A`, `B`
  and `C` and the cursor is on screen row 4.
- A grow failure with history, then a resize: `term_init(4, 10, 100)`,
  `L0` to `L9` (`lines_count` 10), then five lines that fail to allocate:
  `lines_start` 5, slots 0 to 4 NULL, `lines_recycled` 5, the live part
  `L5` to `L9` and `M0` to `M4`. With the allocator restored, a resize to
  4 x 20 gives `lines_start` 0, `lines_count` 10, the same ten lines in
  order, `lines_recycled` still 5, and `ring_ok`.
- A grow failure while the view is scrolled back: the same `L0` to `L9`,
  `scrollback_offset` 3 (the view's top row is `L3`), then two lines that
  fail to allocate: the offset is 5 and the view's top row still reads
  `L3`, as it would on a full ring.
- A grow failure while a selection spans the line being dropped: two
  terminals hold `L0` to `L9`, one with room to grow (`term_init(4, 10,
  100)`, then every allocation failing) and one already full
  (`term_init(4, 10, 6)`, capacity 10). Both select all, then take one
  more line, which drops `L0` from each. `selection_sync()`,
  `selection_copy_text()` and `selection_contains()` for every screen cell
  give the same results on both: a failed growth treats a selection
  exactly as a full ring does.
- The three new NULL guards (Readers, above), each from a constructed
  state that puts the row back before `term_free()`: with screen row 3's
  slot set to NULL, `selection_select_all()` ends its selection on the
  last non-blank row before it; with screen row 1's slot set to NULL, a
  line feed on the bottom row grows the ring without touching the NULL
  slot; with screen row 1's slot set to NULL and the scroll region set to
  rows 2 and 3 (`ESC[2;3r`), `ESC[1S` and `ESC[1T` move the NULL pointer
  without refilling, marking or stamping it. None of these states is
  reachable through the public API or the fake allocator.
- Clear: on a partly filled buffer, `ESC[2J` and `ESC[3J` change neither
  `lines_count` nor the rows allocated, and make no allocator call.
- The limits: `term_init(24, 80, 100)` and 300 lines give 124 rows, the
  capacity, and `term_max_scroll_offset()` 100. `term_init(24, 2, 50000)`
  (2 columns keep the full buffer near 4 MB, quick under the sanitizers)
  holds 24 rows at open; 50,100 lines give 50,024 rows, the capacity, and
  a maximum offset of 50,000; 100 more lines make no allocator call.
- Tab close: `term_free()` on a fresh, a partly filled, a full and an
  alternate-screen terminal (with a partly filled primary) leaks nothing
  under `make test SAN=1`.

The existing terminal suites (`test_term.c`, `test_reflow_fixes.c`,
`test_term_fuzz.c`, `test_dirty.c`, `test_vt_sequences.c`,
`test_selection.c`, `test_term_extract.c`, `test_term_conpty.c`) must pass
unchanged: they read only rows inside the live part, including the seven
that force the sparse state.

**The fuzz target.** `tests/fuzz/fuzz_term.c` today runs `term_init(24,
80, 100)` and never resizes, and its largest seed has 60 line feeds, so
it never reaches a full ring, a grow failure or a reflow. It changes to:
- `term_init(4, 10, 6)`: a capacity of 10, so seven line feeds past the
  bottom row fill the ring and short inputs reach the recycling branch;
  4 rows, so scroll regions and `CSI M` on row 0 are a few bytes away;
  10 columns, so auto-wrap and reflow happen with short lines.
- Two command bytes, taken out of the data stream. `0xFF r c` resizes:
  `term_resize(t, 1 + r % 8, 1 + c % 16)`, so 1 to 8 rows and 1 to 16
  columns, reaching one-row and one-column terminals and twice the
  starting size. `0xFE k` makes allocation number `1 + k % 4` from now on
  fail once, through `term_set_calloc()` and a counting allocator that
  disarms itself after that failure, so the grow, reflow and
  alternate-screen failure paths are reachable. Every other byte, in the
  runs between commands, goes to `term_process()`; a command byte with
  too few bytes after it is passed to `term_process()` as data. `0xFE`
  and `0xFF` are never valid in UTF-8, so the parser loses little: it
  still sees the other invalid bytes (`0xC0`, `0xC1`, `0xF5` to `0xFD`).
- After every `term_process()` run and every resize, the target checks
  the invariant on the active ring and, while `alt_screen_active`, on the
  saved primary, and calls `__builtin_trap()` on a violation. It restores
  `term_set_calloc(NULL)` before it returns.
- Three hand-written seeds join `tests/fuzz/corpus/term/` (seeds are
  hand-written inputs, never crash outputs): `27-ring-wrap` (`L01` to
  `L30`, each followed by CR LF, three times past the capacity),
  `28-resize` (three lines, a resize to 8 x 10 that pads, twenty line
  feeds into the wrapped ring, a resize to 5 x 4, then a resize to 2 x 16
  on the alternate screen and its exit) and `29-alloc-fail` (a filled
  ring, then a failing grow, a failing resize and a failing
  alternate-screen entry).

What the pull-request job proves: `quality.yml`'s `Fuzz` job runs `make
fuzz && make fuzz-run`, which replays the committed seeds once with no
mutation. It therefore proves the invariant on those seeds only, which is
why the three new seeds are written to reach the full ring, the wrapped
ring, the resizes and the three failure paths. Arbitrary input is
explored only by the weekly mutating run (`fuzz-weekly.yml`, `make
fuzz-long`), which is not a pull-request check.

`window.c` is not built natively, so its three changes (the `term_init()`
argument and the two resize callers) are checked by the Windows checklist
(items 11 and 12) and by review.

## Files

| File | Change | Responsibility |
|---|---|---|
| `src/core/settings_help.h`, `.c` | create | Id enum, the help table (names, labels, titles, texts, defaults, ranges, flags), accessors. No Win32. |
| `src/core/search_providers.h`, `.c` | create | The search-provider label/value table and its accessors, moved out of `settings.c`. |
| `src/core/settings_entry.h`, `.c` | create | `settings_entry_or_keep()`, the Save rule for the four keep-the-old-value numbers; the scrollback constants and `settings_scrollback_for_new_tab()` (C). |
| `src/config/loader.c` | modify | Scrollback default and clamp from the `SETTINGS_SCROLLBACK_*` constants. **CRLF file**: keep its line endings. |
| `src/term/buffer.c` | modify | Lazy scrollback rows (C): `term_init()` allocates the screen rows only; `term_scroll()` allocates a row into a NULL slot as the ring grows and recycles the oldest on failure; NULL guards in `term_scroll()`'s dirty loop and in `term_scroll_up()`/`term_scroll_down()`; `resize_failing`; `term_reflow_buffer()` allocates only the rows it needs, all or nothing, and no longer frees the old buffer; `term_resize()` commits or leaves the terminal unchanged; a fallible `term_alt_screen_enter()`; the replaceable allocator. **CRLF file**: keep its line endings. |
| `src/term/term.h` | modify | `bool grow_failing` and `bool resize_failing` in `Terminal`; `term_resize()` returns `bool`; `TermCallocFn` and `term_set_calloc()`; the comments on `term_logical_rows()` and `term_screen_to_phys()` that say every slot is pre-allocated are corrected. LF file. |
| `tests/test_term_scrollback.c` | create | Native test 9 (C). |
| `tests/fuzz/fuzz_term.c`, `tests/fuzz/corpus/term/27-ring-wrap`, `28-resize`, `29-alloc-fail` | modify, create | The fuzz target on a 4 x 10 terminal with 6 lines of scrollback, resize and allocation-failure command bytes, and the invariant check on both rings; three hand-written seeds (C). |
| `src/core/selection.c` | modify | `sel_row_blank()` treats a NULL row as blank (C, Readers). LF file. |
| `src/ui/ns_tip.h`, `.c` | create | Themed tooltip helper: create (`TTS_NOPREFIX \| TTS_ALWAYSTIP`, classic render, token colours, font, width, delays), add a window tool, add a `CBS_DROPDOWN` combo's edit, add and move a rect tool. Changes no window style. Takes `HFONT` and `const ThemeTokens *` so the wintest links it without `window.c`. |
| `src/core/settings_layout.h`, `.c` | modify | `settings_initial_placement()`, the initial size and position rule (Layout and behaviour changes A: just tall enough for Behaviour). |
| `src/ui/settings.c` | modify | Initial size and position through `settings_initial_placement()` with Behaviour's row count and the owner monitor's work area, replacing `CW_USEDEFAULT` and the fixed 760 x 560; Permit Web Fetch moved to the top of Web Access; `page_ctrl` (adds `SS_NOTIFY` to statics), `page_ctrl_w` (the refresh glyph) and `make_hint`; labels from the table; `d->tips[]`; one attach loop replaces `k_tooltips[]`/`add_tooltip()`; tip created after the theme block with tokens resolved from `d->theme`; rect tools for the two disableable combos moved in `relayout_page`; search providers and the Save rule through the new `src/core` modules; the re-entry guard in `settings_dlg_run()`; the tip destroyed in `WM_DESTROY`. LF file. |
| `src/ui/settings_dlg.h`, `src/ui/window.c` | modify | `create_session()` passes `settings_scrollback_for_new_tab()` to `term_init()` (C); the grid sync and `WM_SIZE` skip the display-buffer and remote resize when `term_resize()` returns false (C); `settings_dlg_show()` returns whether it ran a dialog; `on_settings_clicked_page()` returns at once when it did not. |
| `tests/test_settings_help.c`, `tests/test_settings_entry.c` | create | Native tests 1-6 and 8 above. |
| `tests/test_settings_layout.c`, `tests/runner.c` | modify | Native test 7 (Layout and behaviour changes A). |
| `tests/test_ns_tip_win.c` | create | wintest above. |
| `tests/runner.c`, `tests/win_runner.c` | modify | Declare and call the new tests; `win_runner.c` builds and activates the manifest's activation context around the tip tests, and counts one failure and skips them when it cannot be created. |
| `Makefile` | modify | Add `tests/test_ns_tip_win.c` to `WIN_ONLY_TESTS`; add it and `src/ui/ns_tip.c` to the wintest sources. The new `src/core` files are picked up as every `src/core` file is. **The Makefile is CRLF**: keep its line endings and check `git diff --stat --ignore-cr-at-eol` before review. |
| `tests/integration/cases/50-window.ps1`, `tests/integration/README.md` | modify | The `settings_help_tool_count` case (tool count and the re-entry check); the case listed in the README. No module change. |
| `README.md` | modify | Hand corrections to the Settings section (four drifts); the Web Access list reordered (Permit web fetch first); the Terminal section's Scrollback lines entry says what the tip says (applies to tabs opened after Save, memory used only as lines are kept); `**Version**:`. |
| `src/ui/resource.h`, `build/win/nutshell.exe` | modify | Version bump and build. |

## Delivery

One pull request, branch `feat/settings-help`, based on `main` at
`d73eeec` (v1.2.74) or later, which has PR #91 and so the Behaviour
page's two new rows. TDD order: the native
tests and the three `src/core` modules first (every task before the
version bump checks its Windows compile with `make -B` on the objects it
touched, never with `make release`), with `settings_initial_placement()`
and test 7, then `ns_tip` with its wintest, then `settings.c` (help, initial
size and position, Web Access order) and `window.c`, then the lazy rows in
`src/term` with test 9, before the setting is wired (so the default's
10,000 lines never cost a full buffer at open), then the harness case and
README. Version
bump (patch + 1 over `main`'s) at the build on `automaton`: `make clean &&
make release`, `make test`, `xvfb-run -a make wintest`. The maintainer runs
`tests/integration/Run-Integration.ps1 -Tier gate` on the Windows box (the
new case is in the gate tier), then the Windows checklist (items 9 and 10
at 100 % on a 1080p screen, item 12 with Task Manager), which goes in the
PR body.

---

## The help texts

**Scope note (settled 2026-11-10):** this spec covers the Settings dialog
only. The same help for the Session Manager's profile editor is a recorded
follow-up, outside this pull request.

Grouped by page in the order the dialog shows them. The heading is the
control's exact label; the tip text is its heading (the label, or the
`title` where one is given), a blank line, then the quoted text below
(body, then the default line for a setting). The line
under each heading is the table row's metadata, not part of the text.
Defaults and ranges are from `config_default_settings()` and
`settings_validate()` in `src/config/loader.c`, `tool_decision.h`,
`ai_prompt.h`, `settings_debug.h`, `app_font.{c,h}` and `cmd_policy.c`.
An out-of-range number typed into the dialog is clamped on Save, except
Idle Timeout, Max Results, Tool approval timeout and Automatic tool rounds,
whose out-of-range entry is not saved (the stored value stays) through
`settings_entry_or_keep()`; those four texts say so.

### General > Appearance (4 settings)

#### Colour Scheme:
`SH_COLOUR_SCHEME` · setting · `colour_scheme` · default Onyx Synapse

> The colours of the whole window: terminal, tab strip, menus, AI panel and dialogs. Onyx Synapse and Sage & Sand are dark; Onyx Light and Moss & Mist are light. Pick one and press Save; every open tab changes at once.
> Default: Onyx Synapse.

#### Terminal Font:
`SH_TERMINAL_FONT` · setting · `font` · default Consolas

> The font the terminal draws text in. Only the fonts on Nutshell's list that are installed on this PC are offered; install another from the list (Cascadia Code, Fira Code, JetBrains Mono, Hack and others) and it appears here. Every open tab changes when you press Save, and the shell is told the new column and row count. The AI panel's code font is set separately, below.
> Default: Consolas.

#### Font Size:
`SH_FONT_SIZE` · setting · `font_size` · default 10 · values 6, 8, 10, 12, 14, 16, 18, 20

> The terminal font size in points: 6, 8, 10, 12, 14, 16, 18 or 20. A larger size fits fewer columns and rows in the window, and the shell is told the new size at once. Zooming the terminal (Ctrl+= and Ctrl+-, or Ctrl and the mouse wheel) steps through the same sizes, and this box shows where zoom left it.
> Default: 10.

#### AI Assist Font:
`SH_AI_FONT` · setting · `ai_font` · default Consolas

> The font the AI panel uses for code blocks and commands; replies and your messages stay in the interface font, and the terminal is not affected. Only installed fonts from Nutshell's list are offered. The panel reads it when it is created: if it has been opened since Nutshell started, the change shows after Undock or Dock, or after a restart.
> Default: Consolas.

### General > Terminal (3 settings)

#### Scrollback Lines:
`SH_SCROLLBACK` · setting · `scrollback_lines` · default 10,000 · range 100 to 50,000

> Lines that scroll off the top of the screen are kept here so you can scroll back to them; once a tab holds this many, the oldest are dropped. Set from 100 to 50,000. Memory is used only as lines are kept, about 20 bytes per character cell: a new tab costs little, and a full buffer on a 200-column tab takes about 40 MB at 10,000 lines and 200 MB at 50,000, and briefly about as much again while the tab is resized. A change applies to tabs opened after Save; tabs already open keep their size.
> Default: 10,000.

#### Paste Delay (ms):
`SH_PASTE_DELAY` · setting · `paste_delay_ms` · default 350 · range 0 to 5,000

> A pause between lines when you paste several lines into the terminal, from 0 to 5,000 milliseconds. Raise it for a device or slow shell that drops input sent too fast, such as a router or switch console; 0 sends the whole paste at once. It is skipped when the program on the other end has turned on bracketed paste, as most modern shells and editors do, because that program takes the paste as one block.
> Default: 350.

#### Confirm before pasting
`SH_PASTE_CONFIRM` · setting · `paste_confirm` · default on

> Shows what you are about to paste in a preview before it reaches the terminal, so a clipboard holding something unexpected (a password, a long script, a line that would run at once) is caught first. Enter pastes, Tab moves to Cancel and Escape cancels. Control characters are stripped from every paste whether this is ticked or not.
> Default: on.

### General > Logging (2 settings)

#### Log Directory:
`SH_LOG_DIR` · setting · `log_dir` · default empty

> The folder session logs go to; a log is started and stopped per tab with File > Start Logging and is plain text with the terminal's colour codes removed. Leave this empty to use the folder nutshell.exe is in. The folder must already exist: if it does not, Start Logging does nothing and no log is written.
> Default: empty (the folder nutshell.exe is in).

#### Log Name Format:
`SH_LOG_FORMAT` · setting · `log_format` · default %Y-%m-%d_%H-%M-%S

> The date and time part of each log file's name; the file is called <time>_<session>.log. Use %Y year, %m month, %d day, %H hour, %M minute and %S second; %y, %j, %a, %A, %b, %B, %p, %Z, %z and %% work too. A format with any other % code is ignored and the default is used. A \ or / makes a subfolder, which must already exist, and : * ? " < > | are refused in a file name; either way Start Logging then does nothing.
> Default: %Y-%m-%d_%H-%M-%S.

### Connection > SSH (1 setting)

#### Idle Timeout (mins):
`SH_SSH_IDLE` · setting · `ssh_user_idle_timeout_mins` · default 0 · range 0 to 10,080

> Disconnects an SSH tab after this many minutes with no activity from you, from 0 to 10,080 (one week); 0 never disconnects. Keystrokes, mouse-wheel scrolling, switching tabs and typing in the AI panel count as activity; output from the server does not. The tab shows "[Disconnected after N min idle]". Local shell and telnet tabs are never disconnected by this, and a number outside 0 to 10,080 is not saved.
> Default: 0.

### Connection > Startup (3 settings)

#### Open Session Manager at startup
`SH_SESSION_MGR_STARTUP` · setting · `open_session_manager_at_start` · default off

> Opens the Session Manager as soon as Nutshell starts, so you can pick a session straight away. It does not open when Auto-connect at startup connects a session, when a session is named on the command line, or when Nutshell is started with -nc or --local.
> Default: off.

#### Auto-connect at startup
`SH_AUTOCONNECT` · setting · `auto_connect` · default off

> Connects the session chosen in Session, below, as soon as Nutshell starts. A session named on the command line is opened instead, and starting Nutshell with -nc skips auto-connect once. If the chosen session no longer exists, Nutshell says so and opens the Session Manager.
> Default: off.

#### Session:
`SH_AUTOCONNECT_SESSION` · setting · `auto_connect_session` · default empty · disabled while Auto-connect is unticked (rect tool)

> The saved session Auto-connect at startup opens, listed by name, or by host for a session without a name. A session that has since been deleted stays in the list so that saving does not lose it; pick another to replace it. It can be changed only while Auto-connect at startup is ticked.
> Default: empty (none chosen).

### AI Assistant > Provider (4 settings, 1 action)

#### Provider:
`SH_AI_PROVIDER` · setting · `ai_provider` · default anthropic

> The AI service the assistant uses: anthropic, openai, gemini, moonshot, deepseek, or custom for any OpenAI-compatible endpoint given in Base URL. Changing it clears Model; pick or type one again, or leave Model empty for the provider's default. Tool calls (web search, web fetch, the debug log) work with anthropic, openai, deepseek and moonshot; gemini and custom get none.
> Default: anthropic.

#### API Key:
`SH_AI_KEY` · setting · `ai_api_key` · default empty

> Your key from the provider's developer console. There is one key field, so switching provider means entering that provider's key. It is stored in nutshell.config encrypted with Windows DPAPI: only your Windows account on this PC can read it, and on another PC or account you enter it again. Hold the eye button to see what you typed. The AI button in the tab strip turns green once a key is set.
> Default: empty.

#### Model:
`SH_AI_MODEL` · setting · `ai_custom_model` · default empty · tools on the combo, its child edit and its label

> The model the assistant uses, typed in or picked from the list that Refresh models, the button beside it, fills. Left empty, each provider uses its built-in default, for anthropic claude-sonnet-4-6; custom has none, so type the name your server expects, such as llama3.1. A typed name must match the provider's model id exactly.
> Default: empty (the provider's default model).

#### Refresh models
`SH_AI_REFRESH` · action · the button beside Model (on screen it shows only a refresh glyph)

> Fetches the provider's model list with the API key above (it asks for one if the box is empty) and fills the Model list; the button is disabled while it fetches, and an error opens a message box. For custom, the list comes from the Base URL cut at /v1/ followed by /v1/models, or, without /v1/, the Base URL with its last path part replaced by /models. Nothing is saved until you press Save.

#### Base URL:
`SH_AI_BASE_URL` · setting · `ai_custom_url` · default empty · shown only for custom

> The full endpoint address for the custom provider, for example https://host/v1/chat/completions; shown only when Provider is custom. Use https. With plain http to anything but this PC, your API key and terminal text cross the network unencrypted, and a warning under the box says so. Refresh models derives the model list address from this.
> Default: empty.

### AI Assistant > Behaviour (7 settings)

#### Max Terminal Lines:
`SH_AI_CONTEXT_LINES` · setting · `ai_max_context_lines` · default 1,000 · range 1 to 50,000

> How many of the latest terminal lines the assistant reads with every message you send, from 1 to 50,000. More lines let it see more of your session, but every line is sent each time, so cost and reply time grow with it. Lower it if replies get slow or the context meter on the AI panel fills up; raise it when the assistant misses output that scrolled past.
> Default: 1,000.

#### System Instructions:
`SH_AI_SYSTEM_NOTES` · setting · `ai_system_notes` · default empty · up to 2,559 characters

> Instructions sent with every conversation in every tab, such as how you like answers written or rules the assistant must follow. Up to 2,559 characters, about 400 words. A session's own AI Notes, set in the Session Manager, are sent after these, so they can add detail for one server. Leave it empty for none.
> Default: empty.

#### Render AI markdown
`SH_AI_MARKDOWN` · setting · `markdown_render_enabled` · default on

> Shows the assistant's replies formatted: headings, lists, tables, bold text and code blocks, in the theme's colours. Untick it to see the raw text with its markdown symbols, for example to check exactly what the model sent. The open AI panel changes when you press Save.
> Default: on.

#### Allowed:
`SH_AI_POLICY_ALLOWED` · setting · `ai_policy_default.allowed` · default Read · values Read, Unknown, Write, Critical

> The highest level of command a new AI session may run: Read, Unknown, Write or Critical. Commands above it are blocked and shown as held; commands at or below it ask you first, unless Auto-approve is on. This is only where a new session starts: the AI panel's status line changes it for one session. It never holds a tool call; web_fetch is governed by Auto-approve.
> Default: Read.

#### Auto-approve by default
`SH_AI_POLICY_AUTO` · setting · `ai_policy_default.unattended` · default off

> Starts new AI sessions with Auto-approve on, so commands up to the Allowed level run without asking. With Allowed at Critical, Save asks you to confirm. Even when it is on, a background tab always asks, and after 3 automatic rounds in a row the next batch waits for you. With Allowed at Unknown or above it also lets web_fetch run without a card, up to the Automatic tool rounds limit.
> Default: off.

*(The command-round cap is the compiled `AI_AUTO_ROUNDS_MAX` = 3; Automatic tool rounds does not change it.)*

#### Tool approval timeout (seconds):
`SH_AI_TOOL_TIMEOUT` · setting · `ai_tool_timeout_sec` · default 60 · range 15 to 3,600, or 0 · tools on the label static and the number box

> How long a tool approval card waits for Run or Deny before it expires and the call is not run: 15 to 3,600 seconds, or 0 to wait until you answer, press Stop or close the tab. A card appears when Auto-approve does not cover the call, when it comes from a background tab, or after the Automatic tool rounds limit. A new value applies to replies started after Save, and a number outside these is not saved. Command approvals are not affected.
> Default: 60.

#### Automatic tool rounds:
`SH_AI_TOOL_ROUNDS` · setting · `ai_tool_auto_rounds` · default 3 · range 1 to 20 · tools on the label static and the number box

> How many rounds in a row the assistant may run a tool that would otherwise ask (web_fetch today) without asking, while Auto-approve covers it, from 1 to 20. When the count is reached, the next such call waits on a card under "[Auto-approve paused after N automatic tool rounds]", for as long as Tool approval timeout allows. Typing a message or pressing Run on a card starts the count again; web_search and read_debug_log never count. A change applies to replies started after Save; a number outside 1 to 20 is not saved.
> Default: 3.

### AI Assistant > Web Access (4 settings)

#### Permit Web Fetch
`SH_AI_WEB_FETCH` · setting · `ai_web_fetch_enabled` · default off · ranges from `net_guard.h`

> Gives the assistant the web_fetch tool, which downloads an http or https page it names. The address can carry text from your session to any site, so each fetch asks on a card unless Auto-approve is on at Unknown or above; a background tab, and the call after the Automatic tool rounds limit, ask anyway. It refuses 127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 169.254.0.0/16, 0.0.0.0/8, ::1, ::, fc00::/7, fe80::/10 and IPv4 forms of those in IPv6; 100.64.0.0/10 and multicast are not refused.
> Default: off.

#### Search Engine:
`SH_AI_SEARCH` · setting · `ai_search_provider` · default DuckDuckGo (API) · values None, DuckDuckGo (API), DuckDuckGo (HTML), Custom

> The search service behind the assistant's web_search tool: None, DuckDuckGo (API), DuckDuckGo (HTML) or Custom. None removes the tool, so the assistant cannot search. DuckDuckGo (API) falls back to the HTML page when it finds nothing. Searches run without asking and the query goes to the search service. Gemini and custom providers get no tools, so this has no effect with them.
> Default: DuckDuckGo (API).

#### Search URL:
`SH_AI_SEARCH_URL` · setting · `ai_search_url` · default empty · shown only for Custom

> Your own search endpoint, used when Search Engine is Custom and shown only then. Give the address without a query string: Nutshell adds ?q= and the query, and reads the reply as DuckDuckGo API JSON. When it returns no results, the search is retried on DuckDuckGo's HTML page, and left empty with Custom chosen, searches go to DuckDuckGo (API); either way the query can reach DuckDuckGo.
> Default: empty.

#### Max Results:
`SH_AI_MAX_RESULTS` · setting · `ai_max_search_results` · default 7 · range 1 to 20

> How many search results the web_search tool gives the assistant for each query, from 1 to 20. More results give it more to choose from, but every result is added to the conversation and sent with each later message. A number outside 1 to 20 is not saved.
> Default: 7.

### Debugging (13 settings, 2 actions, 2 info)

#### Enable debugging
`SH_DBG_ENABLE` · setting · `debug_enabled` · default off

> Writes a detailed log of what Nutshell does to the log folder, at the levels set below, so you or an AI can find the cause of a problem. Unticked, only errors and warnings are written, to the same folder. The default levels cost very little, so it can stay on. When AI Assist reads the log with its read_debug_log tool, the text it reads is sent to your AI provider.
> Default: off.

The six level rows share one closing sentence, the same in each: "Levels: Off, Errors (errors and warnings), Info, Debug and Trace, each adding to the one before; Off records nothing here, not even for an issue mark."

#### Rendering:
`SH_DBG_LEVEL_RENDER` · setting · `debug_levels[render]` · default Info · values Off, Errors, Info, Debug, Trace

> How much the log records about drawing the window: paints, invalidation, the scrollbar, DPI changes and resizing. Set it to Trace for a display fault such as text that does not redraw. Levels: Off, Errors (errors and warnings), Info, Debug and Trace, each adding to the one before; Off records nothing here, not even for an issue mark.
> Default: Info.

#### Terminal:
`SH_DBG_LEVEL_TERM` · setting · `debug_levels[term]` · default Info

> How much the log records about the terminal emulator: escape-sequence mode changes, the screen buffer, resizes and the alternate screen. Use Trace, with Include terminal content, when output looks wrong; that also records the bytes received. Levels: Off, Errors (errors and warnings), Info, Debug and Trace, each adding to the one before; Off records nothing here, not even for an issue mark.
> Default: Info.

#### AI Assist:
`SH_DBG_LEVEL_AI` · setting · `debug_levels[ai]` · default Info

> How much the log records about the assistant: requests, streamed replies, tool calls, approvals and the command dispatcher. Raise it when a reply stalls, a tool call misbehaves or a command does not run. Levels: Off, Errors (errors and warnings), Info, Debug and Trace, each adding to the one before; Off records nothing here, not even for an issue mark.
> Default: Info.

#### SSH connection:
`SH_DBG_LEVEL_SSH` · setting · `debug_levels[ssh]` · default Info

> How much the log records about SSH: connection stages, the host key, the sign-in method, the channel, keepalives and timeouts. Raise it when a connection fails, hangs or drops. Levels: Off, Errors (errors and warnings), Info, Debug and Trace, each adding to the one before; Off records nothing here, not even for an issue mark.
> Default: Info.

#### Local shell:
`SH_DBG_LEVEL_SHELL` · setting · `debug_levels[shell]` · default Info

> How much the log records about local shell tabs: which shell was chosen, the console starting and closing, and the shell exiting. Raise it when a local tab will not start or closes on its own. Levels: Off, Errors (errors and warnings), Info, Debug and Trace, each adding to the one before; Off records nothing here, not even for an issue mark.
> Default: Info.

#### General:
`SH_DBG_LEVEL_GENERAL` · setting · `debug_levels[general]` · default Info

> How much the log records about everything else: startup, settings, menus, the config file and the logger itself. Raise it when something goes wrong outside a tab. Levels: Off, Errors (errors and warnings), Info, Debug and Trace, each adding to the one before; Off records nothing here, not even for an issue mark.
> Default: Info.

#### Keep recent detail for issue marks, at
`SH_DBG_RECORDER` · setting · `debug_recorder` + `debug_recorder_level` · default on, at Trace · **title** "Keep recent detail for issue marks" · tools on the tickbox, its level combo, and a rect tool for the combo while disabled

> Keeps the last few minutes of detail in a 4 MB memory buffer, at the level chosen beside it, and writes it to its own file when you mark an issue with Ctrl+Shift+M. The log file can then stay at Info while an issue mark still captures Trace detail. A category set to Off is not recorded, and with debugging off only errors and warnings are kept.
> Default: on, at Trace.

#### Save a screenshot with each issue mark
`SH_DBG_SCREENSHOT` · setting · `debug_screenshot` · default off

> Saves a PNG of the Nutshell window in the log folder each time you mark an issue with Ctrl+Shift+M. It shows whatever is in the window at that moment, so look at it before you share the folder.
> Default: off.

#### Include terminal content
`SH_DBG_CONTENT` · setting · `debug_include_content` · default off

> Also records terminal text, AI commands, chat text and host names in the log. The shell's echo of what you type is terminal text, so it is recorded too. Passwords, passphrases and keys, and your keystrokes and pastes themselves, are never recorded. Leave it off unless a problem needs the text itself.
> Default: off.

#### Log folder:
`SH_DBG_FOLDER` · setting · `debug_log_dir` · default logs

> Where debug logs, issue marks and screenshots are written. A relative path is relative to the folder nutshell.exe is in, and %NAME% environment variables are expanded, for example %LOCALAPPDATA%\Nutshell\logs. Browse picks a folder and Open shows it in Explorer. An empty box means the default.
> Default: logs.

#### Browse...
`SH_DBG_BROWSE` · action

> Picks the log folder with the Windows folder picker. A folder inside nutshell.exe's folder is filled in as a relative path, so the setting keeps working if you move Nutshell together with its folder.

#### Open
`SH_DBG_OPEN` · action

> Opens the log folder in Explorer as it is typed in the box, before you Save, creating the folder first if it does not exist yet.

#### Max file size (MB):
`SH_DBG_MAX_FILE` · setting · `debug_max_file_mb` · default 10 · range 1 to 100

> When a log file reaches this size, it is closed and a new one started; from 1 to 100 MB. Smaller files are quicker for an AI or an editor to open; Keep at most, below, decides how many are kept.
> Default: 10.

#### Keep at most (MB):
`SH_DBG_MAX_TOTAL` · setting · `debug_max_total_mb` · default 50 · range 5 to 5,000

> The most the log folder may hold, from 5 to 5,000 MB. When it is over, the oldest log files are deleted. The text beside the box shows how much the folder uses now.
> Default: 50.

#### Log folder usage
`SH_DBG_USAGE` · info · the dim "using N MB in K files" text beside Keep at most (table label "Log folder usage")

> How much the log folder holds, in megabytes and files, measured when Settings opened. When it goes over Keep at most, the oldest log files are deleted to bring it back under.

#### Current log file:
`SH_DBG_CURRENT` · info

> The file Nutshell is writing to now, or why there is none yet: no file is created until the first line is written. It is read-only; click into it to select and copy the path.

### Count

| Page | Settings | Actions / info | Tools on the tip |
|---|---|---|---|
| General > Appearance | 4 | 0 | 8 |
| General > Terminal | 3 | 0 | 5 |
| General > Logging | 2 | 0 | 4 |
| Connection > SSH | 1 | 0 | 2 |
| Connection > Startup | 3 | 0 | 5 |
| AI Assistant > Provider | 4 | 1 (Refresh models) | 10 |
| AI Assistant > Behaviour | 7 | 0 | 12 |
| AI Assistant > Web Access | 4 | 0 | 7 |
| Debugging | 13 | 4 (Browse, Open, Log folder usage, Current log file) | 29 |
| **Total** | **41** | **5** | **82** |

`SH_COUNT` is 46.

---

## Critique

### Round 1

Every finding was ruled by the coordinator; the dispositions and what
changed in this revision:

1. **Per-text corrections. Accepted; each re-checked against the code.**
   - Log Directory: the folder must exist; Start Logging (`on_log_toggle`)
     only `fopen`s and does nothing on failure. The claim that Nutshell
     creates the last folder is gone (that is only `open_session_log`, the
     config-only path). Also listed under "What is there today".
   - Log Name Format: names the characters Windows refuses and what
     happens (Start Logging does nothing); `log_format_validate` checks
     only `%` codes.
   - Model: custom has no default (`ai_provider_model` returns NULL for
     it); the text names anthropic's default and an example for custom;
     test 3 ties the named default to `ai_provider_model("anthropic")`.
   - Refresh models: the real derivation (cut at `/v1/` plus
     `/v1/models`, else last path part replaced by `/models`), the API key
     prompt, disabled while fetching, errors in a message box. The
     `https://host` bug is reported, not described in the tip.
   - Automatic tool rounds: full text from `4cd589f` (`ai_tool_auto_rounds`,
     1 to 20, default 3, applies to replies started after Save, an
     out-of-range entry is not saved); added to the "except" list; test 4
     covers its clamp.
   - AI Assist Font: "monospaced" dropped (the list offers Inter, and the
     named face wins over the `FIXED_PITCH` hint); says when it applies
     (the panel copies `ai_font` at creation and is only hidden, not
     destroyed, so a change shows after Undock/Dock or a restart). README
     gets the same note.
   - Keep recent detail: "errors and warnings" (Errors admits ERROR and
     WARN, `dlog_types.h`).
   - Debugging levels: one shared sentence in all six rows, naming Errors
     as errors and warnings and carrying the Off note.
   - Permit Web Fetch: the exact refused ranges from `net_guard.h`,
     including what is not refused, replacing "never reaches this PC or
     your local network".
   - Colour Scheme: the "this dialog takes the new colours" clause cut.
   - Tool approval timeout: background tab and the rounds limit added to
     when a card appears.
2. **Disabled controls. Accepted.** Session and the recorder level combo
   get a rect tool on `d->hPage`, refreshed with `TTM_NEWTOOLRECT` in
   `relayout_page` (empty rect while hidden); wintest and checklist item 2
   cover it.
3. **Help id per control. Accepted.** `page_ctrl()` requires an id for
   every page control including the raw `CreateWindow` sites; Refresh,
   Browse, Open and the new usage static (`SH_DBG_USAGE`) have their own
   ids; native test 6 scans `settings.c` for every enumerator and fails
   under `CI` when the file is unreadable; harness case
   `settings_help_tool_count` checks 82 tools; Delivery names `-Tier gate`.
4. **README block. Accepted, cheaper path.** No generated block in this
   PR: the API, test and Files entry for it are gone. README is corrected
   by hand for the four drifts with its links kept. The generated block is
   follow-up 6, with the escaping rules.
5. **Goal overclaimed keyboard reach. Accepted.** The Goal now says hover
   now, F1 on a focused control (which needs a mouse click today), and
   names one follow-up for Tab order plus `PROPID_ACC_DESCRIPTION` via
   `IAccPropServices`. The custom-popup paragraph no longer claims
   Narrator support for the stock tip as fact; checklist item 5 checks it.
6. **F1 gaps. Accepted, all five.** Hover tip deactivated while tracking;
   hidden on `WM_MOVE`, `WM_SIZE`, `WM_ENTERSIZEMOVE`; focus change by
   `GetFocus()` before and after dispatch; Escape with an open combo list
   is dispatched so the list closes and the tip stays
   (`CB_GETDROPPEDSTATE`); F1 consumed (not translated or dispatched).
   Checklist item 4 covers them.
7. **Title. Accepted.** No `TTM_SETTITLE`; the label is the first line,
   then a blank line, then the text; test 2 asserts the first line.
8. **`SS_NOTIFY` on any window. Accepted.** Class check (`Static`) before
   setting it; the wintest asserts edit and button styles unchanged.
9. **Tip styles. Accepted.** `TTS_NOPREFIX | TTS_ALWAYSTIP` named in
   `ns_tip_create` and asserted in the wintest.
10. **Wintest overclaims. Accepted.** Colour read-back and tracking
    visibility under Wine are dropped; text read back with `TTM_GETTEXT`
    (size in `wParam`); short fixture texts; colour honouring is checklist
    item 1 only.
11. **Scrollback wiring. Open, maintainer decides.** The honest text stays;
    the wiring and the memory measurement are follow-up 3, marked
    "maintainer to decide".
12. **Profile editor scope. Open, maintainer decides.** A "Scope question"
    line heads the texts section; config-only keys with no control get no
    tip (listed under Non-goals).
13. **Small fixes. Accepted.** The source-of-truth line now names `4cd589f`
    and v1.2.72; the Makefile's CRLF line endings are noted under Files;
    the `d->theme` / `ns_tokens()` coupling is stated and removed by
    resolving the tip's tokens from `d->theme`.

### Round 2 (final)

Ruled by the coordinator; the dispositions and what changed:

1. **Hover-only. Accepted.** F1 and the tracking tip move to follow-up 1
   with Tab order and the accessible description, where the round-1 design
   is kept in brief. Removed from this PR: the loop change, the nine hide
   paths, the Escape case, the second tip, the old checklist item 4 and the
   tracking tip's count. Round 1's finding 6 (F1 gaps) is superseded by
   this. `TTDT_AUTOPOP` stays 32767 ms.
2. **Falls away with 1** (it concerned the F1 path).
3. **Falls away with 1.**
4. **Falls away with 1.**
5. **Search providers in `src/core`. Accepted.** `src/core/search_providers.{c,h}`
   (Files); tests 3 and 5 read it, so they test the table the combo uses.
6. **Wintest activation context. Accepted.** `win_runner.c` builds one from
   `src/ui/nutshell.manifest` (`CreateActCtx`/`ActivateActCtx`, deactivated
   and released after). The spec says why: `TTM_GETTEXT` with the size in
   `wParam` and the V3 `TOOLINFO` size are comctl32 v6 behaviour.
7. **Harness owner lookup. Accepted.** `GetParent` (already imported by
   `NutshellIT.psm1`); no `Add-Type` change, no fresh-window note.
8. **Accepted.** Test 1 checks page order only. The keep-the-old-value rule
   is `settings_entry_or_keep()` in `src/core/settings_entry.{c,h}`, used by
   the Save handler for the four settings; test 4 tests that helper, not
   `settings_validate()`. The wintest asserts `WM_NCHITTEST == HTCLIENT` on
   the fixture static. Labels get `SS_NOTIFY` from `page_ctrl()` at
   creation; nothing is OR'd in afterwards, so round 1's class check
   (finding 8) is dropped, and the wintest's "styles unchanged" assertion
   stays as a guard.
9. **Re-entry guard. Accepted.** One `static HWND` in `settings_dlg_run()`;
   a second `IDM_EDIT_SETTINGS` while open brings the open dialog to the
   front. `settings_dlg_show()` reports that it did not run a dialog so
   `on_settings_clicked_page()` skips the post-Save refresh (`window.c`
   added to Files). The harness case and checklist item 4 cover it.
10. **Accepted.** `ns_tip_set_theme()` and `ns_tip_set_dpi()` leave this PR;
    follow-ups 2 and 4 add them with their tests.
11. **Falls away with 1.**
12. **Accepted.** The source line cites `43ed4d2` / v1.2.73. The tip is
    created after the theme block (and the fonts) in `WM_CREATE`. The table
    gains `title`, used as the tip's first line when the on-screen label is
    a fragment: "Keep recent detail for issue marks", with the 4 MB size in
    the body. Test 2: the first line equals `title` if set, else `label`.

**Text corrections, all six accepted and re-checked against the code:**

- Tool approval timeout: "applies to replies started after Save" (the
  value is copied into each `AiStream` at launch, `tool_timeout_ms`).
- Search URL: left empty with Custom chosen, the search goes to
  DuckDuckGo (API) (`ai_tool_web_search.c` builds the custom URL only when
  `search_url` is non-empty).
- Model: "picked from the list that Refresh models ... fills" (the combo
  starts empty).
- Idle Timeout, Max Results and Tool approval timeout now say that an
  out-of-range number is not saved, as Automatic tool rounds already did.
- Log Name Format: a `\` or `/` makes a subfolder that must already exist,
  while `: * ? " < > |` are refused outright; either way Start Logging does
  nothing.
- Open Session Manager at startup: not opened with `-nc` or `--local`
  (`CLI_CONNECT_LOCAL` takes the local-shell branch first).

The two open maintainer questions (Scrollback wiring, profile editor scope)
are unchanged.

### Addendum 2026-11-10, maintainer's requests, no critique round

The maintainer's Windows test added two requests to this PR. Both are
written up under "Layout and behaviour changes":
- **A.** The window opens tall enough that AI Assistant > Behaviour needs
  no scrolling at 100 % on a 1080p work area. This is a pure placement
  rule in `settings_layout.c` with native test 7. The maintainer corrected
  the first draft, which sized the window to the tallest page: the window
  is sized for Behaviour only, and every other page keeps its scrolling.
- **B.** Web Access puts Permit Web Fetch first.

The maintainer also confirmed that hover help covers every control in the
dialog, including the two new Behaviour rows.

2026-11-10, maintainer: help texts approved as written; Scrollback wiring decision pending.

2026-11-10, maintainer: Scrollback Lines is wired in this PR (Layout and behaviour changes C; follow-up 3 removed). The tip follows the maintainer's wording. The memory figures were corrected against `src/term/buffer.c`: a cell is 20 bytes and all rows are allocated when the tab opens, so a tab costs its full buffer whatever it outputs. Lazy allocation is follow-up 8, maintainer to decide.

### 2026-11-10 addendum (lazy scrollback)

The maintainer decided: "use lazy allocation", in this PR with the
wiring. Follow-up 8 is removed. What changed in this revision:
- **Layout and behaviour changes C** now carries the design: the slot
  array and the screen rows at tab open, one row allocated in
  `term_scroll()` each time the ring grows, the invariant that exactly the
  occupied segment is allocated, the readers (none changes), allocation
  failure while growing (the oldest row is moved and recycled, a flag and
  one warning), all-or-nothing reflow with `term_resize()` returning
  `bool` and `window.c` keeping the old size on failure, a fallible
  alternate-screen entry, a replaceable allocator for tests, when rows are
  freed, what was chosen not to do, and the memory figures from `sizeof`
  (`TermCell` 20, `TermRow` 32, a slot 8 bytes, measured with gcc and
  MinGW x64).
- **Native test 9** (`tests/test_term_scrollback.c`) and the fuzz
  target's invariant check; checklist item 11 corrected (a tab opened
  before the change keeps its earlier setting, not 3,000 lines) and item
  12 added (memory at open and when full).
- **The Scrollback tip** no longer says each tab reserves the whole buffer
  when it opens; it says memory is used only as lines are kept, with the
  full-buffer figures for a 200-column tab. README follows it.

The same revision reconciled the spec with the code at `297a9bc`, as the
plan's author had found it: Behaviour has 22 rows, so the window opens at
a client height of 744 at 100 % on 1080p and Debugging, ticked, no longer
scrolls there (section A, test 7, checklist item 9); the timeout row's
label is "Tool approval timeout (seconds):"; the refresh glyph goes
through `page_ctrl_w()`; the harness case walks `tooltips_class32`
windows with `FindWindowEx`; the wintest guards a failed activation
context; `loader.c` and `buffer.c` are CRLF and stay so.

**This addendum had not been through a critique round when it was
written; the round that followed is recorded below.**

### 2026-11-10, lazy scrollback (one round)

An independent critic read item C and plan tasks 7 and 8 against `main`
at v1.2.74 (`d73eeec`). The design stands. The coordinator's rulings:

1. Fuzz check does not exercise the invariant "on arbitrary input": accepted. The target moves to `term_init(4, 10, 6)` with resize (`0xFF r c`) and allocation-failure (`0xFE k`) command bytes, checks both rings, and gains three hand-written seeds; the spec now says the pull-request job replays seeds only and the weekly run mutates.
2. No test of a live part that wraps past the array end with NULL slots in the gap: accepted. Two tests added (pad to `lines_start` 103, grow, resize to 5 x 4; a grow failure with history, then a resize), with indices checked against `buffer.c`.
3. "Every reader already treats a NULL row as absent" is false: accepted, both halves. Readers rewritten (the invariant is the primary defence; which sites check and which did not), and NULL guards added in `sel_row_blank()`, `term_scroll()`'s dirty loop and `term_scroll_up()`/`term_scroll_down()`, each tested from a constructed state, since none is reachable through the API.
4. Seven tests force `lines_count < rows`: accepted. The invariant is stated over `term_logical_rows()`, growth allocates only into a NULL slot, and `term_resize()` and `term_free()` free every non-NULL slot of the whole array.
5. `make release` without a version bump in tasks 7 and 8: accepted. Every task before the last checks the Windows compile with `make -B` on the touched objects (no link, no `make clean`, no exe); only the last task bumps and runs `make clean && make release`.
6. Checklist item 12 used an absolute Task Manager figure: accepted. It now compares a new tab at 100 with one at 50,000, then fills one, and says Task Manager shows the whole process.
7. Checklist item 11's oldest line ignored the window's rows: accepted. About 1,502 minus the rows: 1,478 at 24 rows, 1,452 at 50.
8. The tip omitted the resize peak: accepted. One clause added ("and briefly about as much again while the tab is resized"); the tip is 530 bytes, 4 sentences.
9. A failed resize logged on every `WM_SIZE`: accepted in part. `resize_failing` logs once per failing streak, and the clipped-terminal limitation after a failed shrink is stated; no retry timer. Rejected: making a shrink allocation-free in this PR.
10. Step 3's list of what pushes a line into history was incomplete: accepted. Step 3 names `term_scroll()` as the one place and lists its two callers and the parser paths into the whole-screen `term_scroll_up()`, `CSI M` on row 0 included; a delete-lines growth test added.
11. Missing tests and behavioural byte counts: accepted in part. Tests added for a grow failure while scrolled back and while a selection spans the dropped line; the byte-count asserts stay, marked as documentation, with `rows_held` the behavioural check. Rejected: making `term_init()` fallible; it keeps `xmalloc` and aborts at tab open as today.
