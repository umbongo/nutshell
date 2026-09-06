# Integration tests

End-to-end tests that drive the real `build\win\nutshell.exe` against a live SSH
host. They complement the unit tests in `tests/*.c`, which never touch Win32 or a
network. Everything here is Windows-only and needs a desktop session.

## How it works

`NutshellIT.psm1` creates a scratch directory with a copy of the exe and a generated
`nutshell.config` containing one key-auth profile. Each case launches
`nutshell.exe -sn it`, turns on session logging through the File menu command, types
into the terminal with `SendKeys`, and asserts on the ANSI-stripped session log. No
OCR and no screen scraping: the log is the oracle. Screenshots are still saved to
`artifacts\` as evidence for anything that is only visible on screen (scrolling).

## Prerequisites

- `build\win\nutshell.exe` built from the tree under test (`mingw32-make clean && mingw32-make release`).
- A host reachable by SSH with a **passphrase-free** key authorised for the user.
  The dev box uses the Raspberry Pi `tompi` with `~/.ssh/thomas`.
- Keyboard focus free while the run is in progress (keystrokes are sent to the
  foreground window). Do not type during a run.

## Running

```powershell
.\tests\integration\Run-Integration.ps1 -HostName tompi -User thomas -KeyPath $HOME\.ssh\thomas
```

Run a subset with `-Only connect_shows_prompt,pty_resizes_with_window`. Exit code is
non-zero when any case fails. Per-case logs and screenshots are written to
`tests\integration\artifacts\` (git-ignored).

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
| `resize_applies_to_inactive_tab` | a tab resized while in the background reports the new `tput lines` when activated |
| `ai_panel_docks_with_key` | View › AI Assist docks the panel without a dialog |
| `ai_runs_safe_command_with_auto_approve` | a prompted `echo` runs in the terminal via `[EXEC]` + Auto Approve |
| `ai_write_command_blocked_without_permit_write` | a prompted `touch` is held back while Permit Write is off |

## AI Assist cases

The three `ai_*` cases make real API calls and are **skipped** unless a key is
present in the environment variable `NUTSHELL_IT_AI_KEY` or in the git-ignored
file `tests\integration\.ai_key` (create it yourself; never commit it). Provider
and model default to Moonshot / `kimi-k3` and can be changed with `-AiProvider`
and `-AiModel`. Two requests per run, 30 lines of terminal context, no web tools.

## Adding a case

Copy an `Invoke-Case` block in `Run-Integration.ps1`. The second argument is a
hashtable of settings overrides for that case's generated config; the script block
receives the session object and should throw (via `Assert-True`) on failure and
return a short string on success.
