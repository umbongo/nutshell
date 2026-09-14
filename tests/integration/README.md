# Integration tests

End-to-end tests that drive the real `build\win\nutshell.exe` against a live SSH
host. They complement the unit tests in `tests/*.c`, which never touch Win32 or a
network. Everything here is Windows-only.

No case needs a desktop session: typing and driving dialogs go through
`PostMessage`/`SendMessage` straight to the app's own window queue, not simulated
input, so the suite works with the desktop locked, the RDP session disconnected,
or another app in the foreground (a self-hosted CI runner's normal state). Even
the modifier chords the app reads via `GetKeyState` (Ctrl+C/V, Ctrl+Shift+C/V,
Shift+Insert, Ctrl+= zoom) are posted — `Send-NutshellChord` borrows the app's own
keyboard state with `AttachThreadInput` instead of synthesising real input. See
"Posted input" below.

## How it works

`NutshellIT.psm1` creates a scratch directory with a copy of the exe and a generated
`nutshell.config` containing one key-auth profile. Each case launches
`nutshell.exe -sn it`, turns on session logging through the File menu command, and
asserts on the ANSI-stripped session log. No OCR and no screen scraping: the log is
the oracle. Screenshots are still saved to `artifacts\` as evidence for anything
that is only visible on screen (scrolling, dialogs, theme colours).

### Layout

`Run-Integration.ps1` is the driver only: params, `Invoke-Case`/`Invoke-AiCase`,
`Assert-True`, `Invoke-KnownBugBlock`, the capture helpers more than one case
file needs (`Test-NutshellCaptureNonBlank`, `ConvertTo-NutshellFileToken` — both
defined in the driver), the results table and the exit code. A helper only one
case file uses lives in that file (e.g. `Get-TerminalAreaHash` in
`cases\10-terminal.ps1`). Case bodies live in `tests\integration\cases\*.ps1`, dot-sourced by the
driver in name order (so they share its scope — `Invoke-Case`, `$Artifacts`,
`$results`, etc. are all visible with nothing passed explicitly):

| File | Cases |
|---|---|
| `cases\10-terminal.ps1` | connect, Ctrl+C, log filename, paste x2, PTY resize, Page Up, holds position, inactive-tab resize |
| `cases\20-ai.ps1` | the five key-gated `ai_*` cases + `ai_panel_opens_without_key` |
| `cases\30-ui-demo.ps1` | `ui_gallery`, `approval_card_run_selected_settles`, `helpers_theme_pixel_matches_token` |
| `cases\40-helpers.ps1` | the other `helpers_*` cases |
| `cases\50-window.ps1` | launch/window/shutdown/menu (`LAUNCH-*`, `RESIZE-1`, `WINDOW-*`, `CLOSE-1`, `MENU-1`) |
| `cases\60-tabs-logging.ps1` | tabs (`TABS-*`) and logging (`LOG-*`) |
| `cases\70-cli.ps1` | CLI flags (`CLI-1`'s several small cases) |

Add a batch of cases by adding a new `cases\NN-name.ps1` file (any file matching
`cases\*.ps1` is picked up automatically) rather than growing one giant script.

### Posted input

- **Plain keys and text:** `Send-NutshellText`, `Send-NutshellKey` (named keys:
  Enter, Tab, Escape, Backspace, PgUp, PgDn, Home, End, Up, Down, Left, Right,
  Insert, F1–F12, plus the letters the chords need), `Send-NutshellLine`,
  `Wait-NutshellShell`, `Send-NutshellCommand`, and every dialog helper below.
  These post `WM_CHAR`/`WM_KEYDOWN`+`WM_KEYUP`/`WM_COMMAND`/`BM_CLICK`/etc.
  straight to the target window's message queue, exactly mirroring what
  `src/ui/window.c`'s `WM_CHAR`/`WM_KEYDOWN` handlers (and each dialog's own
  `WM_COMMAND` handler) already do for real input. `Send-NutshellKey -Hwnd`
  aims the pair at another window of the app — used for the paste preview's
  Escape, which `paste_dlg.c`'s own modal loop watches for.
- **Modifier chords:** `Send-NutshellChord -Key <name> [-Ctrl] [-Shift]`.
  `window.c` decides Ctrl+C/V, Ctrl+Shift+C/V, Shift+Insert and Ctrl+=/Ctrl+-
  with `GetKeyState(VK_CONTROL/VK_SHIFT)` rather than from the message, so a
  bare posted `WM_KEYDOWN` takes the wrong branch. `GetKeyState` reads the
  keyboard-state table of the calling thread's input queue, and
  `AttachThreadInput` makes two threads share one input queue — so the helper
  attaches to Nutshell's UI thread, `SetKeyboardState`s the modifier down,
  posts the key, waits for a `WM_NULL` round trip, then restores and detaches.
  The app's message loop reads the same table in `TranslateMessage`, so Ctrl+C
  with no selection — which `window.c` deliberately falls through on — still
  produces the `WM_CHAR` `0x03` that reaches the shell as SIGINT. No real
  input, no input desktop, nothing to lock.

That is the whole of it: `Send-NutshellKeys` (real `SendKeys`, and the
`SetForegroundWindow`/`keybd_event` dance it needed) is gone, and no case is
foreground-dependent. It had to go rather than be tolerated — the dev box's
logon session is normally RDP-disconnected, and on such a desktop
`GetForegroundWindow()` returns 0 for *every* process, so no amount of retrying
can make a window come forward. `Set-NutshellTerminalFocus` still calls
`SetForegroundWindow` as a best effort after its click, but does not depend on
it succeeding.

## Prerequisites

- `build\win\nutshell.exe` built from the tree under test (`mingw32-make clean && mingw32-make release`).
- A host reachable by SSH with a **passphrase-free** key authorised for the user.
  The dev box uses the Raspberry Pi `tompi` with `~/.ssh/thomas`.
- No desktop requirement: every case runs with the workstation locked, the RDP
  session disconnected and other apps in the foreground.

## Running

```powershell
.\tests\integration\Run-Integration.ps1 -HostName tompi -User thomas -KeyPath $HOME\.ssh\thomas
```

Run a subset with `-Only connect_shows_prompt,pty_resizes_with_window`. Exit code is
non-zero when any case fails. Per-case logs and screenshots are written to
`tests\integration\artifacts\` (git-ignored); each case's scratch copy of the exe
and config lives under `artifacts\scratch\` for the duration of the case and is
deleted afterwards, so anything left there is from an interrupted run and can be
removed.

`-Exe` is validated up front: its `FileVersion` (from the binary's embedded
`VERSIONINFO` resource — `nutshell.rc`'s `FILEVERSION`, populated from
`resource.h`'s `APP_VERSION_BINARY`) is read and printed before any case runs, so a
stale `build\win\nutshell.exe` left over from an earlier checkout doesn't silently
run against the wrong code. This deliberately does **not** run `nutshell.exe -v`
and capture its console output — see `Get-NutshellExeVersion`'s doc comment in
`NutshellIT.psm1` for why: that approach works interactively, but
`Start-Process -NoNewWindow` was observed to hang indefinitely (never returning,
even past its own `WaitForExit` timeout guard) when invoked from inside this
harness's actual automation shell, which has no ordinary interactive console —
exactly the kind of environment a self-hosted CI runner has, so it had to be
avoided rather than merely guarded against.

### Tiers

`-Tier gate|ai|nightly|all` (default `all`, i.e. today's behaviour) filters which
cases run:

- **`gate`** — the merge gate: every case that needs no AI key, i.e. all cases in
  the table below except the five key-gated `ai_*` ones. About 8–10 minutes with
  the section 1/4/5/8 additions below (was 3–4 minutes for the original set).
  This is the tier `.github/workflows/integration.yml` runs.
- **`ai`** — the five key-gated `ai_*` cases (see "AI Assist cases" below).
- **`nightly`** — reserved, currently empty; no case in this suite is slow/flaky
  enough yet to warrant it (idle-timeout and host-unreachable scenarios from
  `docs/superpowers/specs/2026-09-09-integration-coverage.md`'s proposed tiers are not
  implemented).

`-Only` and `-Tier` combine (a case must be in both the requested tier(s) and, if
given, `-Only`'s list).

#### When CI runs the `gate` tier

`.github/workflows/integration.yml` runs it **once per pull request, when the
pull request is marked ready for review** — not on every push. It holds the one
self-hosted machine that can drive a real desktop for 10–15 minutes, so the
workflow triggers on `ready_for_review` only: work in a draft pull request and
run `gh pr ready` when the change is done. A push afterwards leaves the
`Integration tests` check missing on the new commit, which blocks the merge;
re-gate with the draft round-trip `gh pr ready --undo N && gh pr ready N`, which
is also how to gate a pull request opened non-draft (Dependabot's). Note that
`gh workflow run integration.yml --ref <branch>` does **not** work for a PR: a
`workflow_dispatch` run goes green but GitHub does not count it toward the PR's
required check, so the PR stays blocked. Only a `pull_request`-triggered run
(the draft round-trip, or a fresh push) satisfies the gate.

## Cases

| Case | Checks |
|---|---|
| `connect_shows_prompt` | key-auth connect via `-sn`, shell output reaches the session log |
| `ctrl_c_without_selection_interrupts` | Ctrl+C with no selection still delivers SIGINT |
| `log_filename_follows_log_format` | File-menu logging names the file `<Log Name Format>_<name>.log` |
| `paste_without_confirmation` | `paste_confirm=false` pastes straight through |
| `paste_with_confirmation_shows_dialog` | `paste_confirm=true` shows the preview window |
| `pty_resizes_with_window` | shrinking the window shrinks `tput lines`/`tput cols` |
| `page_up_scrolls_history` | evidence screenshots before/after Page Up |
| `terminal_holds_position_while_output_arrives` | a view scrolled back with Page Up stays on the same lines while background output arrives; Enter returns to the live view (smart scrolling) |
| `resize_applies_to_inactive_tab` | a tab resized while in the background comes back with the same grid as the tab that was in front, and a bigger one than it had before the resize |
| `ai_panel_docks_with_key` | View › AI Assist docks the panel without a dialog |
| `ai_runs_read_command_unattended` | a prompted `echo` runs in the terminal via `[EXEC]` once the policy control's unattended marker is on Read |
| `ai_commands_run_one_at_a_time` | two `[EXEC]` blocks (`sleep 6 && echo FIRST_DONE`, then `echo SECOND_DONE`) run in order: the second command's echoed text only reaches the log after `FIRST_DONE`'s output, proving commands are gated on the shell prompt rather than burst-sent |
| `ai_write_command_held_then_runs_after_allow` | a prompted `touch` is held back while the policy ceiling is Read, then runs via "Run N selected" once the ceiling is raised to Write |
| `ai_prompt_while_approval_pending` | a second prompt gets a normal reply while the first prompt's command batch sits pending (default policy, nothing unattended); "Run N selected" with `lParam` 0 (the integration-harness "oldest pending batch" convention) then still runs the first batch's command — see docs/superpowers/specs/2026-09-09-pending-command-batches.md |
| `ui_gallery` | contact sheet of every `--ui-demo` state x theme (10 states x 4 themes = 40 captures); no SSH host or AI key needed |
| `approval_card_run_selected_settles` | "Run N selected" on a live approval card (`--ui-demo=approval`) settles without crashing; no SSH host or AI key needed |
| `ai_panel_opens_without_key` | the panel opens straight into the no-key empty state with `ai_api_key` empty, no dialog; no AI key needed despite the `ai_` prefix |
| `helpers_posted_text_reaches_shell` | posted `Send-NutshellLine`/`Wait-NutshellShell` reach the shell with no foreground/focus dependency (proves the posted-input helpers work end to end) |
| `helpers_session_manager_opens_and_lists_profile` | `Open-NutshellSessionManager` + `Wait-NutshellDialog` find the dialog, `Get-NutshellListItems` lists the generated profile, `Close-NutshellDialog -Button Cancel` closes it |
| `helpers_settings_opens_every_page` | `Open-NutshellSettings`/`Select-NutshellSettingsPage` select and capture all nine Settings pages, `Close-NutshellDialog -Button Cancel` closes it |
| `helpers_theme_pixel_matches_token` | `--ui-demo=chat --theme "Onyx Light"`; a sampled background pixel matches `Get-NutshellThemeColor`'s `bg_primary` within tolerance; no SSH host or AI key needed |
| `launch_main_window_no_dialog` | (`LAUNCH-1`) `-nc` launch: `Nutshell_Window` appears, no dialog within 3s, `IDM_FILE_EXIT` → exit code 0 within 5s |
| `launch_without_config_writes_defaults` | (`LAUNCH-2`) no `nutshell.config`: `config_load()` returns `NULL`, so a "Configuration Warning" MessageBox appears (not mentioned in the original plan — documented in the case); dismissed, no config is written until Settings › Save, then it exists with default keys |
| `launch_with_corrupt_config_survives` | (`LAUNCH-3`) same Configuration Warning path for a truncated-JSON config; app keeps running with defaults, no crash |
| `resize_range_paints_cleanly` | (`RESIZE-1`) 640×400 → 1024×768 → 1920×1080 → 1024×768 → 640×400: every capture non-blank, `tput cols` tracks the direction of each resize |
| `minimise_restore_repaints` | (`WINDOW-1`) SW_MINIMIZE then SW_RESTORE: iconic/restored state and the "repaints identically" comparison all asserted normally. Regression guard for the `WM_SIZE` `SIZE_MINIMIZED` bug this case found, fixed in v1.1.12 (see the case comment) |
| `fullscreen_toggle_changes_pty` | (`WINDOW-2`) `IDM_VIEW_FULLSCREEN` twice: `tput cols` grows then returns to its original value |
| `close_with_live_session_exits_cleanly` | (`CLOSE-1`) `WM_CLOSE` with a connected tab: process exits within 5s, exit code 0, no dialog (none exists today) |
| `menus_open_and_list_items` | (`MENU-1`) message-free: `GetMenu`/`GetSubMenu`/`GetMenuItemCount`/`GetMenuItemID` against the 4 top-level menus and every item's real `WM_COMMAND` id (0 = separator), hand-derived from `create_app_menu()` in `src/ui/window.c` — captions are **not** checked: the menu is entirely owner-drawn (`MF_OWNERDRAW`, no `MENU` resource in `resource.rc`) so `GetMenuString` returns empty for every item |
| `tabs_open_switch_close` | (`TABS-1`) open a second tab (fully posted — no click needed), the tab strip capture changes, Ctrl+W closes the active one; the post-close checks (strip hashes back to the one-tab strip, a marker still reaches the surviving tab's log) are asserted normally. Regression guard for the tab-close bug this case found, fixed in v1.1.13 (see the case comment) |
| `tab_status_dot_colours` | (`TABS-2`) three phases (unroutable host / tompi / `kill -9 $$`), each at a fixed 1200×800 so the tab-strip scan band is meaningful: the status dot samples to the theme's `warning`/`success`/`danger` token colour respectively |
| `logging_stop_then_restart_new_file` | (`LOG-1`) `IDM_FILE_LOG_STOP` then a marker is absent from the old file; `IDM_FILE_LOG_START` opens a new file and a second marker lands in it |
| `debug_terminal_log_written` | (`LOG-2`) `debug_terminal=true`: a `<profile>-debug-<timestamp>.log` appears next to the exe (not in `log_dir` — see `open_debug_log()` in `window.c`) containing the sent sequence rendered as the literal text `ESC[1m` followed by `BOLD` |
| `cli_version_prints` | (`CLI-1`) `nutshell.exe -v`: version string captured via the app's shared console (`AttachConsole`/`ReadConsoleTail`, output scoped to this run with a sentinel) on a host that has one, or read out of `cli_output()`'s MessageBox on a console-less host such as the runner job — see `Test-NutshellHostConsole` |
| `cli_list_profiles` | (`CLI-1`) `-l` lists the generated profile's name and host |
| `cli_help` | (`CLI-1`) `-?` prints usage text |
| `cli_unknown_flag_errors` | (`CLI-1`) an unrecognised flag: non-zero exit code, "Unknown option" text |
| `cli_no_connect_opens_idle` | (`CLI-1`) `-nc`: main window, no dialog, and no repaint over 5s (nothing animates a connecting-state tab, since nothing tried to connect) |
| `cli_host_flag_connects` | (`CLI-1`) `-h tompi` resolves the generated profile by host (`config_find_profile_by_host`) and connects, same as `-sn` |

## AI Assist cases

The five `ai_*` cases above the `ui_gallery`/`approval_card_run_selected_settles`
block make real API calls and are **skipped** unless a key is present in the
environment variable `NUTSHELL_IT_AI_KEY` or in the git-ignored file
`tests\integration\.ai_key` (create it yourself; never commit it) —
`ai_panel_opens_without_key` is not one of them; it needs no key. Provider and
model default to Moonshot / `kimi-k3` and can be changed with `-AiProvider` and
`-AiModel`. 30 lines of terminal context, no web tools. A full run of the five
key-gated cases makes about nine model calls: `ai_panel_docks_with_key` sends
none; the other four each send one or two prompts, plus one dispatcher
follow-up call for every command batch that actually finishes running
(`ai_runs_read_command_unattended` 2, `ai_commands_run_one_at_a_time` 2,
`ai_write_command_held_then_runs_after_allow` 2, `ai_prompt_while_approval_pending` 3).

## Dialog helpers

All find a control by its real Win32 control id (`src/ui/resource.h`, or the
`#define` block at the top of the `.c` file that owns the dialog) and drive it
with the message a real interaction sends — no foreground, focus, or unlocked
desktop needed. One line each (see `NutshellIT.psm1` for full doc comments):

| Helper | Does |
|---|---|
| `Wait-NutshellDialog -Session [-Title] [-TimeoutSec]` | HWND of a top-level window in one of Nutshell's own dialog classes (`#32770`, `NutshellPassDlg`, `Nutshell_Settings`, `Nutshell_PastePreview`, `Nutshell_About`), optionally title-filtered; Zero if none appears |
| `Get-NutshellControl -Dialog -Id` | child HWND by control id — `GetDlgItem`, then a recursive `EnumChildWindows`/`GetDlgCtrlID` fallback for a control nested in a sub-page (e.g. a Settings page) |
| `Set-NutshellControlText -Control -Text` / `Get-NutshellControlText -Control` | `WM_SETTEXT` (+ an `EN_CHANGE` notification, though no dialog in this codebase actually reads it — see the function's doc comment) / `WM_GETTEXT` |
| `Invoke-NutshellButton -Control` | `BM_CLICK` — works on every button in this codebase, including owner-drawn ones |
| `Select-NutshellCombo -Control -Item` / `Get-NutshellComboSelection -Control` | index or exact text via `CB_FINDSTRINGEXACT` + `CB_SETCURSEL` + `CBN_SELCHANGE` / `CB_GETCURSEL` |
| `Set-NutshellCheckbox -Control -Checked` / `Get-NutshellCheckbox -Control` | `BM_SETCHECK` + `BN_CLICKED` / `BM_GETCHECK` |
| `Select-NutshellListItem -Control -Index` / `Get-NutshellListItems -Control` | `LB_SETCURSEL` + `LBN_SELCHANGE` / `LB_GETCOUNT`+`LB_GETTEXT` |
| `Close-NutshellDialog -Dialog [-Button OK\|Cancel\|Yes\|No]` | clicks the standard-id button (`IDOK`=1/`IDCANCEL`=2/`IDYES`=6/`IDNO`=7 — every Nutshell dialog and MessageBox uses these), falling back to a caption-matched `Button` child if the id isn't present |
| `Open-NutshellSessionManager -Session` | File › New Session, waits for the dialog |
| `Open-NutshellSettings -Session [-Page]` / `Select-NutshellSettingsPage -Dialog -Page` | Edit › Settings, optionally selecting a nav page by name (`Appearance`, `Terminal`, `Logging`, `SSH`, `Startup`, `Provider`, `Behaviour`, `Web Access`, `About`) |
| `Get-NutshellWindowText -Hwnd` / `Get-NutshellChildWindows -Hwnd` | debugging: a window's text / every descendant as `hwnd`⇥`ctrlId`⇥`class`⇥`text` |
| `Get-NutshellPixel -Path -X -Y` / `Test-NutshellPixelNear -Path -X -Y -Rgb -Tolerance` | read/compare a pixel in a saved capture |
| `Get-NutshellThemeColor -Name -Token` | parses `src/core/ui_theme.c`'s per-theme initialisers for one of `bg_primary`, `bg_secondary`, `accent`, `text_main`, `text_dim`, `border`, `terminal_fg`, `terminal_bg`, `success`, `warning`, `danger`, `info`, `link` |
| `Get-NutshellRegionHash -Path -Region` | MD5 of a proportional region `@{X;Y;W;H}` (each 0..1) of a saved capture — the general form of `cases-terminal.ps1`'s terminal-specific `Get-TerminalAreaHash`; used by `minimise_restore_repaints`, `tabs_open_switch_close` and `cli_no_connect_opens_idle` to compare a specific band (or the whole window) across two captures |

**Window geometry is never assumed.** `Set-NutshellWindowSize` treats its
`-Width`/`-Height` as a *request*: it clamps them to the primary work area
(`Get-NutshellWorkArea`), keeps the window on-screen, and returns what was
actually achieved — `Width`/`Height`/`X`/`Y`, the real `ClientWidth`/
`ClientHeight` from `GetClientRect`, and `Clamped`. Cases must reason from that
return value (or from a relative comparison between two achieved sizes), never
from the numbers they asked for: the runner's logon session is not a fixed
size, and a window bigger than the desktop is not a size anything can reason
about. The same goes for pixel bands — anything scanning or hashing the tab
strip takes its rectangle from `Get-NutshellTabStripRegion`, which reads the
real `Nutshell_Tabs` child window, because the chrome above the strip also
scales with the monitor's DPI (this dev box's session runs at 288 DPI). A case
that hardcodes a size, a row count or a proportional band is a desktop
dependency in disguise and will fail the moment the runner's session changes.

One thing the harness cannot do, documented rather than faked:

- **`Get-NutshellTabCount`** throws — `tabs.c`'s tab strip is a single
  owner-drawn window with no per-tab child HWNDs, no exposed count message, and
  no UI Automation provider, so there's no way to ask it how many tabs exist
  without clicking around and diffing captures.

**Tab switching** (`Select-NutshellTab`, and `Close-NutshellTab`, which selects
before posting Ctrl+W) *is* posted: `tabs.c`'s `WM_LBUTTONDOWN` handler
hit-tests the message's own `lParam` and never reads the cursor, so the helper
posts `WM_LBUTTONDOWN`+`WM_LBUTTONUP` to the `Nutshell_Tabs` child window at the
tab's computed rect — no foreground, no mouse, works desktop-locked. (It used to
click for real with `SetCursorPos`/`mouse_event`, which silently landed in
whatever window was in front when Nutshell was not, and that is exactly how
`resize_applies_to_inactive_tab` failed once.) The x it clicks assumes
minimum-width tabs, which every harness tab is — the generated profile is named
`it`; a long profile name would widen its tab and shift the ones after it.

## Adding a case

Copy an `Invoke-Case` block in the matching `cases\NN-*.ps1` file (see "Layout"
above) — or start a new `cases\NN-name.ps1` file for a new batch, it's picked up
automatically. The second argument is a hashtable of settings overrides for that
case's generated config; the script block receives the session object and should
throw (via `Assert-True`) on failure and return a short string on success.
`Invoke-Case` also takes `-ExtraArgs` (launch with these args instead of
`-sn <profile>`, e.g. `@("-h", "tompi")` or `@("-nc")`) and `-NoConfig` (skip
writing `nutshell.config` — see `New-NutshellTestEnv`'s `-NoConfig` switch, used
by the `LAUNCH-2`/`LAUNCH-3` first-run/corrupt-config cases). A case that needs to
observe a dialog that blocks the main window from becoming visible in the first
place (config-missing/corrupt — `WM_CREATE` shows a modal `MessageBoxA` before
`CreateWindowEx` even returns) can't use `Invoke-Case`'s `Start-Nutshell` at all;
see `Start-NutshellUntilDialogOrWindow` in `cases\50-window.ps1` for that pattern.

### Known product bugs

A check that fails because of a *product* bug already written down — not a
harness bug, and not a regression — goes inside `Invoke-KnownBugBlock`:

```powershell
$known = Invoke-KnownBugBlock -Bug "what is broken, and where" {
    Assert-True ($hb -eq $ha) "terminal content differs after minimise/restore"
}
"...the rest of the detail; $known"
```

The block still runs, and the outcome — `[XFAIL]` (still failing) or `[XPASS]`
(unexpectedly passing: time to unwrap it) — lands in the case's detail column,
but it does not fail the tier, so the `Integration tests` gate keeps saying "nothing
regressed" rather than "the same known bugs are still open". Wrap only the
checks the named bug actually breaks; everything else in the case stays a
normal assertion. Two cases use this today: `minimise_restore_repaints` and
`tabs_open_switch_close`.
