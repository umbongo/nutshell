# Telnet sessions and hold-to-reveal passwords — design

Date: 2026-11-03. Status: approved by the maintainer on 2026-11-03.

Two features the maintainer asked for together. They share one new piece,
a password prompt box, and are otherwise independent.

## Why

**Telnet.** The maintainer needs to log into devices that only speak
telnet: first their home router, then network equipment at work. About
90 % of use will be device logins; about 10 % is the classic diagnostic of
connecting to `host:port` to see whether the port answers. Today they would
need a second program for either.

**Hold-to-reveal.** When typing a password into Nutshell there is no way to
check what was typed. A button that shows the password only while it is
held down lets them check it without leaving it on screen.

## Goals

1. A telnet session type, chosen in the existing profile editor's Session
   type dropdown alongside SSH and Local shell, with host, port (default
   23), username and password. The password is stored exactly like an SSH
   profile password (DPAPI, `$dpapi$v1$`, preserved verbatim on another
   PC/user).
2. Auto-login: when the device shows a login prompt (`login:`,
   `Username:`) and then a password prompt (`Password:`), Nutshell answers
   them with the profile's saved username and password.
3. When the profile has a username but no saved password, Nutshell asks
   for the password in a password prompt box when the device's password
   prompt appears.
4. Port checks via a profile: a telnet profile with **neither a username
   nor a password** does no auto-login (maintainer's decision). It connects and shows whatever the far end sends. If the
   connection fails, the tab says so plainly with host and port, and tells
   "refused" (nothing listening) apart from "timed out" (filtered or host
   down), so open-but-quiet is distinguishable from closed at a glance.
5. Once connected, a telnet tab behaves like an SSH tab: copy/paste,
   selection, logging, Mark Issue, reconnect, and **the AI panel runs
   commands on it exactly as it does on SSH** (maintainer's decision),
   under the platform ruleset taken from the profile's `platform` field or
   auto-detected from the banner/prompt (`cmd_detect.c`), with the same
   sticky "only ever stricter" rule.
6. Hold-to-reveal: an eye button at the right edge of every password field
   shows the password in plain text only while the mouse button is held
   down on it.
7. SSH profiles using password auth with no saved password get the same
   password prompt box instead of failing with "Authentication failed"
   (today `window.c` calls `ssh_auth_password` with an empty password).

## Non-goals

- A quick-connect box. Port checks go through a profile (maintainer's
  decision).
- Telnet over TLS, Kerberos/NTLM telnet authentication, serial/COM ports,
  rlogin, raw-TCP as a separate session type.
- Login scripts or expect-style automation beyond the two prompts.
- Reveal that stays on (a toggle), or reveal by keyboard shortcut alone.

## Anti-goals (would be a failure even if it technically works)

- **Sending the password to anything but a password prompt.** Auto-login
  must never type the password blind, on a timer, or into a prompt it does
  not recognise. When in doubt it does nothing and the user logs in by
  hand.
- **A password in any log, recorder capture, issue log or AI context.**
  The auto-sent username is ordinary text; the auto-sent password is never
  logged, recorded, or visible to the AI panel's terminal-context reads
  (the device does not echo it, but Nutshell's own write path must not
  record it either).
- **Breaking or slowing SSH or local shell tabs.** Telnet is a third
  backend behind the existing `SessionIo` interface; SSH and ConPTY paths
  do not change behaviour.
- **A revealed password left on screen** after the button is released,
  the pointer leaves, the dialog loses focus, or capture is lost.

## Constraints

- All rules in `CLAUDE.md` apply: TDD, `-Werror` under MinGW and native
  gcc, portable logic in `src/core/` with native tests, Win32 code in
  `src/ui/`, design-system rules (no `RGB(`, `ns_scale`, `ns_font`,
  `ns_type.h` sizes, `ns_hover`, `ns_motion`), version bump before every
  build, `secure_zero()` for every buffer that held a password.
- `winsock2.h` before `windows.h` in any file that needs both.
- The terminal reads its byte source on the UI thread through
  `SessionIo.poll` (`src/term/session_io.h`), and keystrokes go out through
  `SessionIo.write` on the UI thread. Telnet's `poll` and `write` must
  therefore never block: non-blocking socket, connect on a worker thread
  as SSH's `connection_thread` does.
- `nutshell.config` stays portable; only secret fields are DPAPI-locked.

## What has been decided

### Telnet: shape in the code

- `Profile.kind` gains the value `"telnet"` (today `"ssh"` or `"local"`);
  the loader reads and writes it like the others. The profile editor's
  `IDC_COMBO_KIND` gains "Telnet"; for telnet it shows host, port,
  username, password and platform, and hides key path, auth type and
  shell. Choosing Telnet sets the port to 23 when the port field still
  holds SSH's 22 (and back).
- `SessionKind` gains `SESSION_TELNET`. Every site that switches on kind
  (about 50 sites in 12 files, mostly `window.c`, found on 2026-11-03 by
  searching for `SESSION_LOCAL`, `SESSION_SSH`, `profile_is_local` and
  `"local"` string compares: `window.c`, `ui_demo.c`, `ai_prompt.c/.h`,
  `dispatch_line_clear.c/.h`, `ai_chat.c`, `session_manager.c`,
  `loader.c`, `config.h`, `profile.h`, `dlog_snapshot.h`,
  `local_pty.c/.h`; rebuild the list by the same search) is reviewed:
  telnet takes the **remote-host** path wherever SSH and local differ for
  the AI panel and dispatch (e.g. `ai_prompt.c` ~219-260, `ai_chat.c`
  ~424), and is excluded from SSH-only machinery (libssh2 keepalive, host
  key checks, SFTP, `io.kind == SESSION_SSH` gates).
- The telnet protocol (IAC parsing and negotiation, outgoing IAC
  escaping, NAWS encoding) and the login-prompt matcher live in
  `src/core/` (e.g. `telnet_proto.c/.h`, `telnet_login.c/.h`) with native
  tests written first. The socket glue (`session_io_telnet()`, connect
  thread) lives in `src/ui/` or `src/config/` beside the SSH backend.

### Telnet: protocol behaviour

- Option negotiation follows RFC 854/855 with loop protection (RFC 1143
  style: never answer a request that would not change state).
- **Passive: Nutshell never starts a negotiation, it only answers** (the
  author's call). Connecting to a non-telnet service (SMTP, HTTP, a TLS
  port) for a port check therefore sends no `IAC` bytes the service would
  read as junk.
- Answers: server `WILL ECHO` → `DO ECHO`; server `WILL
  SUPPRESS-GO-AHEAD` → `DO SUPPRESS-GO-AHEAD`; server `DO
  SUPPRESS-GO-AHEAD` → `WILL SUPPRESS-GO-AHEAD`. `DO NAWS` → `WILL NAWS`,
  then send the window size, and send it again on every terminal resize.
  `DO TERMINAL-TYPE` → `WILL TERMINAL-TYPE` and answer `SEND` with
  `XTERM`. Refuse every other option (`WONT`/`DONT`).
- **Local echo (the author's call, PuTTY's "Auto" behaviour):** until the
  server has agreed to echo (`WILL ECHO`), Nutshell echoes what the user
  types into the terminal itself, so a port check that types `HELO` or
  `GET / HTTP/1.0` shows the typing. Once the server echoes, local echo
  stops; if the server withdraws (`WONT ECHO`), it resumes. Local echo is
  **suppressed while a password prompt is on screen** (the matcher's
  password-prompt test on the cursor line), so a password typed by hand is
  never drawn into the terminal, the session log or the AI panel's
  context. The auto-sent password and the password box's value bypass
  local echo always. Keys are sent as they are typed either way (no line
  buffering).
- Outgoing `0xFF` bytes are doubled (`IAC IAC`). The keyboard and AI
  dispatch send a bare `\r` for Enter today; the telnet backend's `write`
  turns a lone CR into CR LF (builder may choose CR NUL if testing shows a
  device needs it, and must say so).
- Incoming `IAC` sequences are stripped before bytes reach the terminal
  parser; a sequence split across two `recv` calls is handled.

### Telnet: auto-login

- Armed when the profile has a username **or** a saved password
  (maintainer's decision); a profile with neither is a port check. The
  matcher looks at the text at
  the cursor's line once output pauses (no new bytes for a short settle
  period), case-insensitively, ignoring trailing spaces:
  - login prompt: the line ends in `login:`, `username:`, `user name:`
    or `user:`;
  - password prompt: the line ends in `password:` (also `passcode:`).
- Sequence: send the username + Enter once at the first login prompt; send
  the password + Enter once at the next password prompt. Devices that ask
  for the password only (a home router, a Cisco line password) get the
  password at the first password prompt; this works with or without a
  username in the profile. A login prompt with no username in the profile
  switches auto-login off.
- Auto-login switches itself off, for the rest of the connection, when any
  of these happens (maintainer's decision on the first two):
  - **the user types anything** in the tab (a key press that reaches the
    session; AI-dispatched keystrokes count too);
  - **a device prompt appears**: the cursor line ends in `>`, `#` or `$`
    (ignoring trailing spaces), e.g. `Router>`, `switch#`. This stops a
    device that needs no login from having the login password sent to a
    later `enable` → `Password:` prompt;
  - the password has been sent;
  - a second login prompt appears after the password was sent (wrong
    credentials: the user sees the device's own failure message and types
    by hand);
  - no prompt is recognised within a timeout after connect (e.g. 20 s).

  It never retries. The cost, accepted: a user who starts typing the
  username by hand finishes the login by hand; no password box appears.
- Username but no saved password: at the password prompt (auto-login
  still armed), the password prompt box (below) asks for it; Cancel leaves
  the user at the device's prompt to type by hand. The typed password is
  not saved unless the user ticks "Save password to this profile" in the
  box, and it is not kept for reconnects: a reconnect asks again.

### Telnet: status lines

Nutshell writes its own status lines into the terminal, styled like the
existing SSH connect/disconnect messages:
`Connecting to host:port…`, `Connected to host:port (telnet — not
encrypted)`, `Connection refused by host:port`, `Connection to host:port
timed out`, `Could not resolve host`, `Connection closed by host:port`.
The connect timeout matches SSH's. A failed telnet connect is reported in
the tab only; there is no "Connection Error" message box as SSH shows
(the author's call: a refused port is an expected answer in a port check,
not an error to dismiss). SSH-only timers (network-silence timeout, idle
disconnect, keepalive) do not apply to telnet tabs.

### Password prompt box

A small modal box: the profile name and host, one password field (with
the reveal button), "Save password to this profile", OK and Cancel. Used
by telnet (no saved password) and by SSH password-auth profiles with no
saved password. SSH asks from its connect thread, so it goes through the
`ConnUiKind` / `conn_prompt` (`src/core/conn_prompt.h`) machinery; telnet
asks mid-session on the UI thread, so it shows the box directly. Both
follow the existing passphrase window's pattern (`pass_wnd_proc`,
`window.c` ~1087). The buffer is wiped with `secure_zero()` after use.

### Hold-to-reveal

- Fields: the profile editor's password (`IDC_EDIT_PASS`), the key
  passphrase prompt (`window.c` ~1100), the AI API key in Settings
  (`settings.c` ~1369), and the new password prompt box.
- An eye icon button sits inside the right edge of the field (or directly
  beside it where the dialog layout makes inside impossible). A new
  `NS_ICON_EYE` is added to the icon set (`src/ui/icons.h/.c`), drawn with
  design-system tokens and `ns_scale`.
- Press and hold the left mouse button on it: the field shows plain text
  (`EM_SETPASSWORDCHAR` 0, then redraw). Release anywhere, capture lost,
  the window deactivated or the field emptied/destroyed: the mask comes
  back immediately. The button captures the mouse on press so a release
  outside it is still seen.
- Revealing never copies or selects text and does not change the field's
  contents or caret.
- Hover state via `ns_hover`; a tooltip "Hold to show password".

## Left to the builder

- File and function names; whether telnet's socket glue sits in
  `src/ui/` or `src/config/`.
- The exact settle period and timeouts for auto-login (state them).
- Whether the eye button sits inside or beside each field, per dialog.
- How to test telnet end to end: a small scripted telnet responder for
  the integration harness, a local `telnetd`, or the maintainer's router.
  Native tests must cover the protocol parser (split sequences, IAC IAC,
  negotiation loops, NAWS on resize) and the login matcher (each prompt
  form, password-only devices, failure re-prompt, no-username profiles,
  timeout).
- The tab marker for "not encrypted" beyond the status line (a small
  lock-off glyph on telnet tabs is the author's suggestion, not the
  maintainer's request).

## Order of work (author's call)

1. Hold-to-reveal on the three existing fields: small, self-contained, its
   own pull request.
2. Telnet with auto-login, the password prompt box (with reveal), and the
   SSH no-saved-password prompt: one pull request, or two if the telnet
   protocol core lands first behind no UI.

Each pull request bumps the version per `CLAUDE.md` and is built on
`automaton` or the Windows box.
