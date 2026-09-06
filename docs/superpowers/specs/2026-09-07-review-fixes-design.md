# Review Fixes — Design

**Date**: 2026-09-07
**Branch**: `ui-polish`
**Status**: Approved (Thomas's answers to the review questions, 2026-09-07)
**Source**: `2026-09-07-app-and-docs-review.md` §4

Six behaviour changes, three of them new settings. The seventh review item (window
re-sizing itself) was a capture-harness artefact and is withdrawn.

## 1. Ctrl+C: copy when a selection exists, interrupt otherwise

Today `WM_CHAR` intercepts only Ctrl+W and Ctrl+V; Ctrl+C reaches the shell as
`0x03` while the Edit menu and help guide claim it copies.

New behaviour (Windows Terminal / VS Code convention):

| Keys | Action |
|---|---|
| Ctrl+C with an active selection | copy selection to clipboard, clear the selection, do **not** send `0x03` |
| Ctrl+C with no selection | send `0x03` (SIGINT) as today |
| Ctrl+Shift+C | always copy (no-op if no selection); never sent to the shell |
| Ctrl+Shift+V | always paste (same path as Ctrl+V) |
| Ctrl+A | unchanged (goes to the shell); the Edit menu's Select All loses its `Ctrl+A` label |

Implementation: handle in `WM_KEYDOWN` (where Ctrl+V already is) so Shift state is
available; `WM_CHAR` must then swallow `0x03` only when the keydown consumed it (set a
flag, same pattern as Ctrl+V's `0x16`). Menu labels become `Copy\tCtrl+C`,
`Paste\tCtrl+V`, `Select All` (no accelerator). Selection helpers already exist
(`g_selection`, `copy_selection_to_clipboard` or equivalent in `window.c`) — reuse.

## 2. Setting: confirm before paste (default on)

Keep the unconditional confirmation (Thomas pastes code blocks and wants to see them).
Add `int paste_confirm` to `Settings` (default 1, JSON key `"paste_confirm"`), a
checkbox **"Confirm before pasting"** on the Terminal page under Paste Delay, and gate
the `paste_preview_show` call in `do_paste` on it. Remove the dead
`PASTE_CONFIRM_THRESHOLD` define and fix the comment above `do_paste`.

## 3. Page Up / Page Down scroll a page

`VK_PRIOR` / `VK_NEXT` move `scrollback_offset` by `rows - 1` lines (minimum 1),
clamped to `[0, max_scrollback]`, instead of by 1. Put the arithmetic in a pure helper
in `src/core` (e.g. `scroll_page_up(offset, rows, max)` / `scroll_page_down` in
`edit_scroll.c` or a new `term_scroll.c`) with tests for: page from 0, page near the
top clamps to max, page down near 0 clamps to 0, rows=1 moves by 1.

## 5. Setting: open Session Manager at startup (default off)

Add `int open_session_manager_at_start` to `Settings` (default 0, JSON key
`"open_session_manager_at_start"`), a checkbox **"Open Session Manager at startup"** on
the Startup page above the auto-connect checkbox. In `WM_STARTUP_CONNECT`, the
`CLI_RUN`-with-nothing-to-do branch posts `WM_SHOW_SESSION_MANAGER` when the setting
is on. Auto-connect takes precedence: if auto-connect fires, the Session Manager is
not opened. `CLI_RUN_NO_CONNECT` (`-nc`) also suppresses it.

## 6. Session log filenames honour the Log Name Format

Two code paths exist: `on_log_toggle` (File menu / `[L]` badge) calls
`log_format_filename()` which hardcodes `%Y%m%d_%H%M%S`; `open_session_log` (only
reachable via the hidden `logging_enabled` setting) honours `settings.log_format`
with a whitelist. Unify on one core helper:

```c
/* src/core/log_format.h */
int  log_format_validate(const char *fmt);   /* 1 if every %-spec is in "YymdHMSjAaBbpZz%" */
int  log_format_filename(const char *name, const char *dir, const char *fmt,
                         const struct tm *t, char *buf, size_t buf_size);
```

- `fmt` NULL/empty/invalid → `"%Y-%m-%d_%H-%M-%S"` (the documented default).
- `t` is passed in so tests are deterministic; callers pass `localtime(&now)`.
- Output: `<dir>\<strftime(fmt)>_<safe_name>.log`, matching what README documents.
  `safe_name` is the profile name, falling back to host, then `"session"`, sanitised
  as today (`[A-Za-z0-9._-]`, spaces → `_`, rest dropped).
- `on_log_toggle` and `open_session_log` both call it; the whitelist loop in
  `open_session_log` is deleted. Existing tests in `test_log_format.c` are updated
  for the new signature; add tests for validate (valid, `%F` rejected, trailing `%`
  rejected) and for the default-on-invalid fallback.

## 7. Setting: Auto Approve includes write/critical commands (default off)

Today session Auto Approve approves every command that Permit Write lets through.
Add `int ai_auto_approve_all` to `Settings` (default 0, JSON key
`"ai_auto_approve_all"`), a checkbox **"Auto Approve also covers write/critical
commands"** on the AI Assistant › Behaviour page under "Render AI markdown".

Core: add `int auto_approve_all;` to `ApprovalQueue`, set from settings when the AI
panel is created and whenever settings are saved (`ai_chat.c` has the settings
pointer). In `chat_approval_add`:

```
if (safety > CMD_SAFE && !permit_write)          → BLOCKED   (unchanged)
else if (q->auto_approve && (safety == CMD_SAFE || q->auto_approve_all)) → APPROVED
else                                              → PENDING
```

Tests in `test_chat_approval.c`: auto-approve on, all off, write command → PENDING;
all on → APPROVED; safe command → APPROVED either way; permit_write off still BLOCKED.

## Settings persistence (items 2, 5, 7)

`config.h` gains the three ints; `config_default_settings` sets 1/0/0;
`config_load` reads them with those defaults when absent; `config_save` writes them;
`settings_validate` clamps to 0/1. `test_config.c` gains a round-trip test covering
all three, and a test that an old config without the keys loads the defaults.
Settings UI reads/writes them in the page `WM_CREATE` blocks and the `IDOK` handler,
following the existing `IDC_AUTO_CONNECT` / `IDC_AI_MD_RENDER` pattern; new control
IDs go in `resource.h` next to their siblings.

## Documentation

README (Terminal, Settings table, Session Logging, Keyboard Shortcuts, AI Chat
Assistant › Auto Approve) and `help_guide.c` are updated to describe items 1–7 as
built. The Session Logging section describes the single filename scheme.

## Version

Bump `1.0.77` → `1.0.78` in `src/ui/resource.h` and `README.md` before the release
build. `make test` must pass on Windows (`mingw32-make test`).

## Task split

| Task | Files | Order |
|---|---|---|
| A. Core + config + Settings UI: items 2, 5, 7 fields/UI; item 6 core helper; item 7 queue logic; item 3 scroll helper; all tests | `config.h`, `loader.c`, `settings.c`, `resource.h`, `log_format.{c,h}`, `chat_approval.{c,h}`, `edit_scroll.{c,h}` (or new), `tests/*` | first |
| B. Win32 wiring + docs: item 1; item 3 keys; item 2/5/6/7 call sites in `window.c` and `ai_chat.c`; menu labels; README + help_guide; version bump; release build | `window.c`, `ai_chat.c`, `README.md`, `help_guide.c`, `resource.h` (version) | after A |
