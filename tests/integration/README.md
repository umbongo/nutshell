# Integration tests

End-to-end tests that drive the real `build\win\nutshell.exe` against a live SSH
host. They complement the unit tests in `tests/*.c`, which never touch Win32 or a
network. Everything here is Windows-only.

Most of the suite needs no desktop session at all: typing and driving dialogs go
through `PostMessage`/`SendMessage` straight to the app's own window queue, not
simulated input, so it works with the desktop locked or another app in the
foreground (a self-hosted CI runner's normal state). Only the handful of cases
that exercise a true modifier chord the app reads via `GetKeyState` (Ctrl+C/V,
Ctrl+Shift+C/V, Shift+Insert, Ctrl+= zoom) still need `SendKeys`, and therefore an
unlocked, interactive desktop with keyboard focus free — see "Posted vs. real
input" below for exactly which cases those are.

## How it works

`NutshellIT.psm1` creates a scratch directory with a copy of the exe and a generated
`nutshell.config` containing one key-auth profile. Each case launches
`nutshell.exe -sn it`, turns on session logging through the File menu command, and
asserts on the ANSI-stripped session log. No OCR and no screen scraping: the log is
the oracle. Screenshots are still saved to `artifacts\` as evidence for anything
that is only visible on screen (scrolling, dialogs, theme colours).

### Posted vs. real input

- **Posted (no foreground/focus/unlocked desktop needed):** `Send-NutshellText`,
  `Send-NutshellKey` (named keys: Enter, Tab, Escape, Backspace, PgUp, PgDn, Home,
  End, Up, Down, Left, Right, Insert, F1–F12), `Send-NutshellLine`,
  `Wait-NutshellShell`, `Send-NutshellCommand`, and every dialog helper below.
  These post `WM_CHAR`/`WM_KEYDOWN`+`WM_KEYUP`/`WM_COMMAND`/`BM_CLICK`/etc.
  straight to the target window's message queue, exactly mirroring what
  `src/ui/window.c`'s `WM_CHAR`/`WM_KEYDOWN` handlers (and each dialog's own
  `WM_COMMAND` handler) already do for real input.
- **Real (`SendKeys`, needs the foreground and an unlocked desktop):**
  `Send-NutshellKeys` — kept only for the modifier chords `window.c` reads via
  `GetKeyState` at the moment the message is processed (Ctrl+C/V, Ctrl+Shift+C/V,
  Shift+Insert, Ctrl+=/Ctrl+- zoom), since a posted `WM_KEYDOWN` alone doesn't
  change what `GetKeyState` reports. Cases still using it:
  `ctrl_c_without_selection_interrupts`, `paste_without_confirmation`,
  `paste_with_confirmation_shows_dialog` (all via their Ctrl+C/Ctrl+V step), plus
  the AI cases' `Set-NutshellTerminalFocus`. These need keyboard focus free and
  the desktop unlocked; everything else in the table below does not.

## Prerequisites

- `build\win\nutshell.exe` built from the tree under test (`mingw32-make clean && mingw32-make release`).
- A host reachable by SSH with a **passphrase-free** key authorised for the user.
  The dev box uses the Raspberry Pi `tompi` with `~/.ssh/thomas`.
- For the cases listed under "Real" above only: keyboard focus free and the
  desktop unlocked while the run is in progress. Every other case runs fine with
  the desktop locked.

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

`-Tier bvt|ai|nightly|all` (default `all`, i.e. today's behaviour) filters which
cases run:

- **`bvt`** — every case that needs no AI key: all cases in the table below down
  through `helpers_theme_pixel_matches_token`. About 3–4 minutes.
- **`ai`** — the five key-gated `ai_*` cases (see "AI Assist cases" below).
- **`nightly`** — reserved, currently empty; no case in this suite is slow/flaky
  enough yet to warrant it (idle-timeout and host-unreachable scenarios from
  `docs/superpowers/specs/2026-09-09-bvt-coverage.md`'s proposed tiers are not
  implemented).

`-Only` and `-Tier` combine (a case must be in both the requested tier(s) and, if
given, `-Only`'s list).

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
| `resize_applies_to_inactive_tab` | a tab resized while in the background reports the new `tput lines` when activated |
| `ai_panel_docks_with_key` | View › AI Assist docks the panel without a dialog |
| `ai_runs_safe_command_with_auto_approve` | a prompted `echo` runs in the terminal via `[EXEC]` + Auto Approve |
| `ai_commands_run_one_at_a_time` | two `[EXEC]` blocks (`sleep 6 && echo FIRST_DONE`, then `echo SECOND_DONE`) run in order: the second command's echoed text only reaches the log after `FIRST_DONE`'s output, proving commands are gated on the shell prompt rather than burst-sent |
| `ai_write_command_held_then_runs_after_permit` | a prompted `touch` is held back while Permit Write is off, then runs via "Run N selected" once Permit Write is switched on |
| `ai_prompt_while_approval_pending` | a second prompt gets a normal reply while the first prompt's command batch sits pending (auto-approve off); "Run N selected" with `lParam` 0 (the integration-harness "oldest pending batch" convention) then still runs the first batch's command — see docs/superpowers/specs/2026-09-09-pending-command-batches.md |
| `ui_gallery` | contact sheet of every `--ui-demo` state x theme (10 states x 4 themes = 40 captures); no SSH host or AI key needed |
| `approval_card_run_selected_settles` | "Run N selected" on a live approval card (`--ui-demo=approval`) settles without crashing; no SSH host or AI key needed |
| `ai_panel_opens_without_key` | the panel opens straight into the no-key empty state with `ai_api_key` empty, no dialog; no AI key needed despite the `ai_` prefix |
| `helpers_posted_text_reaches_shell` | posted `Send-NutshellLine`/`Wait-NutshellShell` reach the shell with no foreground/focus dependency (proves the posted-input helpers work end to end) |
| `helpers_session_manager_opens_and_lists_profile` | `Open-NutshellSessionManager` + `Wait-NutshellDialog` find the dialog, `Get-NutshellListItems` lists the generated profile, `Close-NutshellDialog -Button Cancel` closes it |
| `helpers_settings_opens_every_page` | `Open-NutshellSettings`/`Select-NutshellSettingsPage` select and capture all nine Settings pages, `Close-NutshellDialog -Button Cancel` closes it |
| `helpers_theme_pixel_matches_token` | `--ui-demo=chat --theme "Onyx Light"`; a sampled background pixel matches `Get-NutshellThemeColor`'s `bg_primary` within tolerance; no SSH host or AI key needed |

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
(`ai_runs_safe_command_with_auto_approve` 2, `ai_commands_run_one_at_a_time` 2,
`ai_write_command_held_then_runs_after_permit` 2, `ai_prompt_while_approval_pending` 3).

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

Two things the harness cannot do, documented rather than faked:

- **`Get-NutshellTabCount`** throws — `tabs.c`'s tab strip is a single
  owner-drawn window with no per-tab child HWNDs, no exposed count message, and
  no UI Automation provider, so there's no way to ask it how many tabs exist
  without clicking around and diffing captures.
- **Tab switching** (`Select-NutshellTab`, and therefore `Close-NutshellTab`,
  which selects before posting Ctrl+W) still needs a real click
  (`ClickClient`/`mouse_event`) since the tab strip only responds to
  `WM_LBUTTONDOWN` hit-testing — no message-based way to activate a tab exists.
  That means both need the foreground and an unlocked desktop, same as
  `Send-NutshellKeys`.

## Adding a case

Copy an `Invoke-Case` block in `Run-Integration.ps1`. The second argument is a
hashtable of settings overrides for that case's generated config; the script block
receives the session object and should throw (via `Assert-True`) on failure and
return a short string on success.
