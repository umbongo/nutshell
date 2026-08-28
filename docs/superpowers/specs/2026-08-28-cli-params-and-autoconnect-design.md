# Command-Line Parameters & Auto-Connect at Startup — Design

**Date:** 2026-08-28
**Status:** Spec — pending implementation plan

## Problem

Nutshell can only be started bare: `WinMain` discards its command line
([main.c:8](../../../src/main.c#L8)), so every session begins with manually
opening the Session Manager and clicking Connect. The user wants a taskbar
icon that clicks straight into their SSH session on `automaton`.

Windows shortcuts carry arguments, so the standard mechanism is command-line
parameters: a pinned shortcut targeting `nutshell.exe --session-name automaton`
gives one-click access. Additionally, an **auto-connect at startup** setting
lets a bare double-click of the exe connect automatically, with an escape-hatch
flag to suppress it in case of bugs.

## Decisions (from brainstorming)

- **Q1:** `-sn <name>` with no matching session → **error MessageBox, then
  open the app with the Session Manager** so the user can pick or create one.
- **Q2:** Help/version/list output: **try `AttachConsole(ATTACH_PARENT_PROCESS)`**
  so output prints in the launching terminal; **fall back to a MessageBox**
  when there is no parent console (Run dialog, shortcut).
- **Q3:** Connect flags are **`-sn`/`--session-name`** (match on profile
  `name`) and **`-h`/`--host`** (match on profile `host`) — not a single `-c`.
- **Q4:** With `-h` taken by `--host`, help is **`-?` and `--help`**.
- **Q5:** Also include **`-v`/`--version`** and **`-l`/`--list`** (list saved
  sessions). Rejected for now (YAGNI): `--config`, ad-hoc `--user`/`--port`
  connections, `--debug`/`--log`.
- **Q6:** Launching while Nutshell is already running opens a **new window**
  (second instance). No single-instance/IPC code.
- **Q7:** New **auto-connect at startup** setting: checkbox + assigned session
  name in the Settings dialog. New param **`-nc`/`--no-connect`** starts the
  app without auto-connecting, as a recovery path.

## CLI Syntax

```
nutshell.exe [option]

  -sn, --session-name <name>   Connect to the saved session with this name
  -h,  --host <host>           Connect to the saved session with this host
  -nc, --no-connect            Start without auto-connecting (overrides the
                               auto-connect setting)
  -l,  --list                  List saved sessions (name and host)
  -v,  --version               Show version
  -?,  --help                  Show this help
```

Rules:

- **All options are mutually exclusive.** More than one → error + usage text,
  exit without starting the UI. Same for an unknown flag or a missing value.
- Matching is **case-insensitive exact**; empty profile fields never match;
  **first match wins** on duplicates.
- `-sn` matches only the `name` field; `-h` matches only the `host` field.

## Startup Precedence

1. Parse the command line. Parse error → error + usage (console/MessageBox),
   exit code 2.
2. `-?` / `-v` / `-l` → print (console or MessageBox), exit 0. `--list` loads
   `nutshell.config` directly from the exe directory — no UI is created.
3. `-sn` / `-h` → start UI and connect to the matching session (overrides the
   auto-connect setting).
4. `-nc` → start UI plainly; the auto-connect setting is ignored.
5. No args + auto-connect enabled with a non-empty session name → start UI and
   connect to it.
6. Otherwise → normal start.

Auto-connect (step 5) resolves **name first, then host** — the Settings
dropdown displays `host` for unnamed sessions, so the stored string may be
either. `-sn`/`-h` stay strict per Q3. Any failed lookup (steps 3 or 5) →
MessageBox `Session "<x>" not found` → Session Manager opens (Q1).

## Architecture

Three layers; all decision logic in testable code.

### 1. `src/core/cli_args.{h,c}` — pure parser (new, unit-tested)

```c
typedef enum {
    CLI_RUN,            /* no args — normal start (check auto-connect) */
    CLI_RUN_NO_CONNECT, /* -nc */
    CLI_CONNECT_NAME,   /* -sn <name> */
    CLI_CONNECT_HOST,   /* -h <host>  */
    CLI_LIST,
    CLI_VERSION,
    CLI_HELP,
    CLI_ERROR
} CliAction;

typedef struct {
    CliAction action;
    char arg[256];        /* session name or host for connect actions */
    char error[256];      /* human-readable message when CLI_ERROR    */
} CliOptions;

void cli_parse(int argc, char **argv, CliOptions *out);
const char *cli_usage_text(void);
```

No Win32 calls, no I/O. `argv[0]` is ignored. The usage text is a single
static string shared by `--help` and error paths.

### 2. `src/config/loader.c` — profile lookup + settings (extended, unit-tested)

```c
Profile *config_find_profile_by_name(const Config *cfg, const char *name);
Profile *config_find_profile_by_host(const Config *cfg, const char *host);
```

Case-insensitive exact match, skip empty fields, first match wins, NULL when
not found or on NULL/empty input.

`Settings` gains ([config.h](../../../src/config/config.h)):

```c
int  auto_connect;                 /* 0 = off (default) */
char auto_connect_session[256];    /* name or host, as picked in Settings */
```

Loader: default `auto_connect = 0`, empty session string; read/write both keys
in `config_load`/`config_save`; validation clamps `auto_connect` to 0/1.

### 3. Win32 glue (thin, not unit-tested)

**`src/main.c`** — `WinMain` converts the command line via
`CommandLineToArgvW` + `WideCharToMultiByte` (UTF-8), calls `cli_parse`, then:

- `CLI_HELP` / `CLI_VERSION` / `CLI_ERROR` → `cli_output(text, title)` helper:
  `AttachConsole(ATTACH_PARENT_PROCESS)`; on success reopen `CONOUT$` and
  print; else `MessageBoxA`. Exit (0, or 2 for errors) without `ui_init`.
- `CLI_LIST` → resolve `<exe_dir>\nutshell.config` (local `GetModuleFileNameA`
  helper), `config_load()`, format one `name — host` line per profile
  (`(unnamed)`/fallback handling matching the Session Manager's display rule),
  output via `cli_output`, `config_free`, exit 0.
- Connect/run actions → `ui_set_startup_action(action, arg)` (new function in
  [ui.h](../../../src/ui/ui.h)) before `ui_init()`, then run normally.

**`src/ui/window.c`** — stores the startup action in statics. At the end of
`WM_CREATE`, after config load, decide the startup connect:

- `CLI_CONNECT_NAME` / `CLI_CONNECT_HOST` → lookup via the strict finder.
- `CLI_RUN` + `settings.auto_connect` + non-empty `auto_connect_session` →
  lookup by name, then host.
- `CLI_RUN_NO_CONNECT` → nothing.

On a match, `PostMessage(hwnd, WM_STARTUP_CONNECT, 0, 0)` (new `WM_USER`
message beside `WM_SHOW_SESSION_MANAGER`,
[window.c:51](../../../src/ui/window.c#L51)) whose handler calls the existing
`on_session_connect()` ([window.c:640](../../../src/ui/window.c#L640)) — the
same path the Session Manager uses. On no match, MessageBox then
`PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0)`. Posting (rather than
calling inside `WM_CREATE`) lets window creation finish first.

**`src/ui/settings.c`** — new "Startup" section:

- Checkbox `IDC_AUTOCONNECT_CHECK` — "Auto-connect at startup".
- Dropdown `IDC_AUTOCONNECT_COMBO` (`CBS_DROPDOWNLIST`) listing every saved
  session by name (falling back to host for unnamed ones, matching Session
  Manager display). Selecting stores the displayed string into
  `auto_connect_session`. If the stored value no longer matches any session,
  it is still shown as the selected text so saving without touching the
  control does not lose it.
- Combo enabled only while the checkbox is ticked. Tooltips on both, via the
  existing `add_tooltip` helper.

## Error Handling & Edge Cases

| Scenario | Handling |
|---|---|
| Unknown flag, missing value, or multiple options | Usage text via console/MessageBox, exit 2, no UI. |
| `-sn`/`-h`/auto-connect name not found | MessageBox `Session "<x>" not found`, then Session Manager opens. App stays usable. |
| Auto-connect enabled but session string empty | Treated as normal start (no error). |
| Auto-connect points at a since-deleted session | Same as not found: MessageBox + Session Manager. `-nc` and the Settings dialog remain available to fix it. |
| `--list` with missing/corrupt config | Output `No saved sessions.` (missing) or the loader's error (corrupt), exit 0/2 respectively. |
| Value looks like a flag (`-sn -v`) | Accepted as the literal value — no flag-sniffing of values. |
| Session name ≥ 256 chars on the command line | Truncated into the fixed buffer (same limit as `Profile.name`), lookup proceeds; a truncated name simply won't match. |
| Launched from console: output after prompt returns | Known `AttachConsole` cosmetic quirk for GUI-subsystem apps; accepted. |
| Second instance while one is running | Independent new window by design (Q6). |
| Duplicate session names | First match in profile order wins (documented). |

## Testing

TDD; all logic lands in `src/core/` / `src/config/`, which the native test
build covers. Win32 glue is manual-verification only.

### `tests/test_cli_args.c` (new, registered in `runner.c`)

| # | Case | Expectation |
|---|---|---|
| 1 | no args | `CLI_RUN` |
| 2 | `-sn prod` / `--session-name prod` | `CLI_CONNECT_NAME`, arg `prod` |
| 3 | `-h box.example.com` / `--host …` | `CLI_CONNECT_HOST`, arg copied |
| 4 | `-nc` / `--no-connect` | `CLI_RUN_NO_CONNECT` |
| 5 | `-l` / `--list`, `-v` / `--version`, `-?` / `--help` | matching actions |
| 6 | `-sn` with no value (end of argv) | `CLI_ERROR`, message mentions the flag |
| 7 | unknown flag `-x` / `--frobnicate` | `CLI_ERROR` |
| 8 | two options (`-sn a -h b`, `-v -l`, `-nc -sn a`) | `CLI_ERROR` (mutual exclusion) |
| 9 | value resembling a flag (`-sn -v`) | `CLI_CONNECT_NAME`, arg `-v` |
| 10 | over-long value (300 chars) | truncated to 255 + NUL, no overflow |
| 11 | `cli_usage_text()` | non-NULL, mentions every flag |
| 12 | bare non-flag argument (`foo`) | `CLI_ERROR` |

### Profile lookup — extend config tests

| # | Case | Expectation |
|---|---|---|
| 13 | find by name, exact | match |
| 14 | find by name, different case | match (case-insensitive) |
| 15 | name absent | NULL |
| 16 | empty search string / NULL cfg | NULL |
| 17 | profile with empty name never matches empty query | NULL |
| 18 | duplicates → first match | first profile returned |
| 19 | find by host, case-insensitive | match |

### Settings round-trip — extend loader tests

| # | Case | Expectation |
|---|---|---|
| 20 | defaults | `auto_connect == 0`, empty session |
| 21 | save with `auto_connect=1`, session `automaton`; reload | both preserved |
| 22 | legacy config without the keys | defaults, no error |
| 23 | validation clamps `auto_connect = 7` | `1` |

### Manual verification (record in commit message)

- `nutshell.exe -?` from PowerShell → usage prints in the terminal.
- Double-click a shortcut with `-?` → MessageBox with usage.
- `-v` → version matches `APP_VERSION`; `-l` → lists sessions correctly.
- Shortcut `nutshell.exe -sn automaton` → connects straight to the session.
- `-sn nosuch` → error box, then Session Manager.
- Enable auto-connect in Settings, restart bare → connects; restart with
  `-nc` → does not connect.
- Launch a second instance while one is running → independent new window.

## Docs

- README: new "Command-Line Options" section.
- In-app User Guide ([help_guide.c](../../../src/ui/help_guide.c)) GETTING
  STARTED text: mention the flags and the auto-connect setting, including the
  pinned-shortcut recipe (`nutshell.exe -sn <name>`).

## Out of Scope (future candidates)

- Taskbar **jump list** of saved sessions (`ICustomDestinationList`) — would
  sit directly on top of `-sn`; natural follow-up.
- A "Create desktop shortcut" button in the Session Manager.
- Single-instance mode / open-as-tab in the running window (Q6 chose new
  window).
- Ad-hoc connections to unsaved hosts (`--user`/`--port`), `--config <path>`,
  `--debug`/`--log`.
- Prefix or fuzzy session matching.
