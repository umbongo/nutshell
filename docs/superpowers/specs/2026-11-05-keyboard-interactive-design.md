# Keyboard-interactive SSH authentication — design

Date: 2026-11-05. Status: draft for the maintainer, revised after critique rounds 1 and 2. The
decisions come from the orchestrator's brief of 2026-11-04. A decision the code forced to
change is marked *Amended*. One the author added is marked *Author's addition*. Written
against `main` at 1f85a3a (v1.2.52) and libssh2 1.11.1 (`~/vcpkg/buildtrees/libssh2`).

## Why

Nutshell tries exactly one SSH method: the profile's password or its key. Many servers want
something else: PAM with a one-time code, a TACACS or RADIUS token behind
keyboard-interactive, or `PasswordAuthentication no` with `KbdInteractiveAuthentication yes`.

Against these servers, `libssh2_userauth_password` fails and the tab says only
"Authentication failed for user@host." The fix: ask the server which methods it offers, try
them in a fixed order, and show each server prompt to the user without ever handing a stored
secret to the wrong prompt.

## Goals

1. A password profile works against a server that offers keyboard-interactive and not
   password. A saved password answers the one prompt that is clearly a password prompt. The
   user answers every other prompt.
2. A server that asks for a one-time code can be logged into, whether the code is the only
   factor or comes after a key or a password.
3. A failed authentication names the methods the server offers.
4. A slow human at a prompt never trips the setup timeout. A server that goes quiet before
   the first prompt still fails in about 15 s, and one that goes quiet after an answer in
   about 60 s, so it cannot hang a tab (guard-rails goal 4).
5. The key passphrase prompt appears only when the key file could not be read. A key the
   server refuses moves on to the next method. A second wrong passphrase stops.

## Non-goals

- Agent and Pageant authentication; GSSAPI, certificate and host-based authentication.
- Any change to how a saved password is stored. The "Save password" tick appears on one more
  prompt, for password profiles only (section 2); nothing else in the save flow changes.
- `SshSession.cached_passphrase`: it is written after a successful passphrase and never read.
  Left alone; out of scope.
- Telnet, which has its own login machine.
- A password-method attempt from a key profile. A key profile's `password` field holds its
  passphrase, so `AuthenticationMethods publickey,password` fails for a key profile, and the
  error names what the server offers.
- Wiping libssh2's own copies. libssh2 frees the response texts and the packet built from
  them without zeroing them, as `libssh2_userauth_password` does today. Accepted.
- UTF-8 answers. RFC 4256 wants UTF-8; answers go out in the ANSI codepage (section 5).

## Anti-goals (would be a failure even if it technically works)

- The saved password answers a prompt that is not password-like, or a truncated prompt.
- The saved password answers twice in one connection.
- A key profile's passphrase is sent to the server as an answer.
- An answer is logged.
- The plan continues after a timeout.
- The setup timeout is left at 0, or at a re-armed value, after authentication.
- Keepalive is on during setup, or off once the session is interactive.
- The gate or CI checks are changed.

## Constraints

- **The CLAUDE.md rules all apply:** TDD with the logic in `src/core/` under native tests;
  `-Werror` under both compilers; `secure_zero()` for every secret; design-system tokens in
  the prompt, with `tests/test_ui_tokens.c` unchanged.
- **Real libssh2 for the auth calls.** `src/term/ssh_session.c` and the auth code in
  `window.c` compile only with real libssh2: hosted CI (Ubuntu, 1.11.0) and the MinGW build
  (vcpkg 1.11.1_DEV). The calls used here are identical in both. This box's `make test`
  excludes them (`Makefile` 139-151, `NO_SSH_LIBS`), so every decision sits behind a
  function-pointer seam in `src/core/`, and the keyboard-interactive callback body lives
  there too (section 4).
- **Line endings are preserved.** CRLF on `main`: `Makefile` and `tests/test_key_auth.c`.
  LF: `window.c`, `pw_prompt.*`, `ssh_session.*`, `conn_prompt.*`, `ssh_timeout.*`,
  `tests/runner.c`, `tests/test_ssh.c`, `tests/test_ssh_timeout.c`,
  `tests/stubs/libssh2.h`, `README.md`, `resource.h` and the notes. New files are LF. Check
  `git ls-files --eol` on every touched file and `git diff --stat --ignore-cr-at-eol` before
  review.

## What has been decided

### 1. Ask the server what it offers

After the host-key check, the thread calls `ssh_auth_list()`, a new NULL-safe wrapper around
`libssh2_userauth_list` that returns NULL when not connected. It is called once before the
first attempt; section 2 says when it is called again.

`ssh_auth_methods_parse()` in the new `src/core/ssh_auth_plan.c/.h` produces a bit set
(`SSH_AM_PUBLICKEY`, `SSH_AM_PASSWORD`, `SSH_AM_KBDINT`, plus `SSH_AM_OTHER` for any unknown
name) and a display string (the names sanitised, deduplicated, joined with ", ", capped at
128 bytes). The parser tolerates spaces, empty items and duplicates.

A NULL reply is handled in this order:

- If `ssh_session_timed_out()` holds, the auth timeout text is used. `userauth_list` runs
  under `BLOCK_ADJUST_ERRNO`, which keeps `LIBSSH2_ERROR_TIMEOUT` as the last error.
- With `libssh2_userauth_authenticated()` true, "none" auth succeeded, and the thread
  proceeds.
- Otherwise the error is "Could not get the authentication methods from host."

There is no blind fallback to the old single attempt. The list call sends a "none" request.
OpenSSH counts every "none" request but an initial one toward `MaxAuthTries` (default 6),
so a re-query costs one of the server's tries.

### 2. The plan

`ssh_auth_next()` is a pure function over a small plan state (profile type, whether a saved
password exists, the offered set, the steps tried, whether the saved password has been sent,
the attempt count). It returns the first step below whose condition holds, or "done". Each
step runs at most once.

| Step | Profile | Condition |
|---|---|---|
| `PUBLICKEY` | key | publickey offered |
| `KBDINT` | key | keyboard-interactive offered; every prompt goes to the user |
| `PASSWORD_SAVED` | password | a saved password exists and password is offered |
| `KBDINT` | password | keyboard-interactive offered; the saved password, if not yet sent, answers one password-like prompt (section 4) |
| `KBDINT_RETRY` | password | `KBDINT` failed after the saved password answered one of its prompts, and password is not offered; every prompt goes to the user |
| `PASSWORD_ASK` | password | password offered; today's `CONN_UI_PASSWORD` box with its save tick |

- **Order rule (round 2).** When a saved password exists and both password and
  keyboard-interactive are offered, the password method is tried first, so silent login
  holds exactly as today. Otherwise keyboard-interactive comes first (OpenSSH and PuTTY
  order); the saved password still answers a "Password:" prompt silently.
- **After a refused saved password** the user is asked once more: on a password-only server
  through the password box (`PASSWORD_ASK`, so README's sentence holds), on
  `{password, keyboard-interactive}` through the keyboard-interactive prompts, on a
  kbd-only server through `KBDINT_RETRY` (*author's addition, narrowed*: without it a stale
  saved password locks a kbd-only profile out until the profile is edited).
- Nothing else is ever tried. Every libssh2 auth call counts toward the cap of 4 attempts,
  the passphrase retry (section 3) included.
- A password profile with no saved password on a password-only server gets today's box and
  save tick, after the list query, so the user's time falls between libssh2 calls.

**Save tick.** Offered only on a password profile, never on a key profile: on the password box
as today, and on a keyboard-interactive prompt that section 4's matcher accepts, that is not
echoed and not truncated, and that the user types into. The UI thread still adds today's
`config_find_profile_match()` condition. **The latest password-like answer the user typed
wins**, and its own tick decides: a later typed answer, ticked or not, replaces an earlier
one, and an unticked latest answer saves nothing. The kept answer is saved if authentication
succeeds, whichever method succeeds.

**Re-query (rounds 1 and 2, ruled).** *Amended (partial success).* libssh2 1.11 never reads
the partial-success flag (`userauth.c`), and every rc-0 path sets
`LIBSSH2_STATE_AUTHENTICATED` (`userauth.c` password ~390, publickey ~1702 and ~1888,
keyboard-interactive ~2219), so "rc 0 but not authenticated" cannot occur in 1.11.x. A
partial success arrives as `USERAUTH_FAILURE`: publickey reports -19 (-18 at the probe),
keyboard-interactive -18.

- The method list is re-queried at most once, and only when an attempt ended in -18 or -19
  **and `ssh_auth_next()` has nothing left**; the plan then continues with the new set.
  Under `AuthenticationMethods publickey,keyboard-interactive` the initial list is only
  `publickey`; after the key is accepted the re-query returns `keyboard-interactive`, so
  goal 2's "after a key" case holds.
- Never after a timeout (authentication stops, section 6), never after -16, -48, a cancel or
  an abort, and never once the 4 attempts are used.
- **A dead transport stops the plan.** After any -18/-19, and after a re-query, the thread
  asks whether the transport is still alive (`libssh2_session_last_errno()` is not a socket
  error: -7, -13 or -43). A failed re-query, or a dead transport, ends authentication with
  the previous attempt's error (the timeout check runs first, section 6). The password box
  is never shown for a dead session.
- The re-query is the one extra "none" request the OpenSSH note in section 1 refers to.

The final error comes from the pure `ssh_auth_fail_text(user, host, type, offered display,
tried, extra)`. `extra` carries a zero-prompt round's instruction (section 4) or "Too many
authentication prompts." Examples: "Authentication failed for alice@host. The server offers:
publickey, keyboard-interactive."; for a key profile without publickey on offer, "… The
server does not offer key authentication; it offers: password."

### 3. When to ask for the key passphrase (*amended*)

`ssh_auth_key()` passes a NULL public-key path. So libssh2 1.11 derives the public key from
the private file before it contacts the server: `userauth_publickey_fromfile` →
`_libssh2_pub_priv_keyfile`, OpenSSL backend. The return codes then separate the two cases:

| Cause | libssh2 code |
|---|---|
| Missing, unreadable or undecryptable file, or a wrong or missing passphrase | `LIBSSH2_ERROR_FILE` (-16) |
| The in-memory variant of the same failure | `LIBSSH2_ERROR_KEYFILE_AUTH_FAILED` (-48) |
| Server refuses the key at the probe | `AUTHENTICATION_FAILED` (-18) |
| Server refuses the key at the signature | `PUBLICKEY_UNVERIFIED` (-19) |

Only -16 and -48 open the passphrase box (-48 because it is harmless). As today, the box is
shown once and a passphrase that works is cached in `cached_passphrase` (never read; see
Non-goals). The retry with the box's passphrase is a second libssh2 call and counts toward
the 4-attempt cap (section 2).

- If the box's passphrase also fails with -16 or -48, authentication ends with "Wrong
  passphrase for <key file>." It does not move on to keyboard-interactive.
- -18 and -19 move the plan on to the next method.
- An unreadable file is also -16, so it still prompts, as it does today.
- Cancelling the box ends authentication with "Passphrase entry cancelled.", mirroring the
  password box. Today the cancel falls through to the generic failure.

### 4. Keyboard-interactive: a thin adapter over a portable round function

`ssh_auth_kbdint(SshSession *, user, SshKbdCtx *)` in `ssh_session.c` calls
`libssh2_userauth_keyboard_interactive_ex`. libssh2's abstract pointer is already the
`SshSession *`; a new field, `void *SshSession.kbd_ctx`, holds the context during the call.

**The adapter** in `ssh_session.c` is a `LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC`. It only
fetches `s->kbd_ctx` and passes every argument straight to the portable

`ssh_kbd_round(SshKbdCtx *ctx, const char *name, int name_len, const char *instruction,
int instruction_len, int num_prompts, const SshKbdPromptIn *prompts,
SshKbdResponseOut *responses)`

in the new `src/core/ssh_kbd.c/.h`. `SshKbdPromptIn` and `SshKbdResponseOut` are typedefs of
`LIBSSH2_USERAUTH_KBDINT_PROMPT` and `LIBSSH2_USERAUTH_KBDINT_RESPONSE`, so libssh2's arrays
pass straight through. `SshKbdCtx` holds four function pointers: `ask` (one prompt to the
user, through `conn_ui_ask()`), `set_timeout` (`libssh2_session_set_timeout`), `abort_io`
(`shutdown(s->socket, SD_BOTH)`) and `now` (`time()`, injected in tests). It also holds the
setup limit in force (`SSH_SETUP_TIMEOUT_MS`, or the test override), the saved password and
the state: `saved_used`, `rounds`, `cancelled`, `too_many`, `aborted`, the entry time, the
limit in force for the error text, and the zero-prompt instruction.

`ssh_kbd_round()` copies the name, the instruction and each prompt **by its given length**
(none is NUL-terminated; libssh2 passes NULL for a zero-length string) into fixed buffers:
`name[96]`, `instruction[2048]` and prompt `text[2048]`. A name or instruction that does not
fit keeps its head and ends with "...". **A prompt that does not fit keeps its tail**, since
its last line is the label, starts with "...", and is marked `truncated`; a truncated prompt
is never auto-answered. It answers each prompt (below) into a local buffer, hands libssh2 a
`malloc`'d copy with `.length` set (libssh2 frees it with its default allocator), wipes every
local answer with `secure_zero()`, and ends with `set_timeout(ssh_kbd_rearm_ms(...))`
(section 6).

The set-call-clear sequence is the portable `ssh_kbd_attempt(void **slot, SshKbdCtx *ctx,
call, arg)`; core never sees `SshSession`. It stores the context in `*slot` (the session's
`kbd_ctx`), records the entry time, makes the call through the function pointer, restores the
timeout to the setup limit in force (which honours `g_setup_timeout_override_ms`) and sets
`*slot` back to NULL.

Edge cases:

- A round with `num_prompts == 0` (NULL arrays) asks nothing. Its sanitised instruction is
  shown in the next prompt's text area or, if none follows, appended to the final error.
- libssh2 itself refuses more than 100 prompts. Nutshell's `SSH_KBD_MAX_PROMPTS` is 8: in a
  larger round the first 8 are handled and the rest get empty strings, and `too_many` is set
  for the final error.
- **Round cap.** After 16 rounds in one attempt the round answers every prompt with an empty
  string, sets `aborted` and calls `abort_io`. The thread reports "Too many authentication
  prompts."

**The password-like matcher** is a new, narrow `ssh_kbd_prompt_is_password(text)` in
`src/core/ssh_auth_plan.c`; telnet's `tn_line_is_password_prompt()` is not used. On the
trimmed, case-folded text, with trailing `:` and spaces dropped:

1. a leading `<user>@<host>'s ` prefix is stripped, and a trailing ` for <word>` is stripped
   ("Password for alice:" becomes "password");
2. what is left must end with the word `password` (at the start, or after a non-alphanumeric
   byte): "Password:", "user@host's password:", "SPINNAKER password:" match;
3. none of these may appear in what is left **as a whole word** (bounded by the text's edges
   or a non-alphanumeric byte): `passcode`, `one-time`, `otp`, `new`, `retype`,
   `verification`, `token`, `pin`, `code`. So "alice@NEWCASTLE.AC.UK's Password:" and
   "SPINNAKER password:" match, and "New password:" and "PIN password:" do not.

The saved password answers a prompt silently only when all of these hold: the profile is a
password profile (`saved` is NULL for a key profile); the saved password is non-empty;
`saved_used` is 0; the prompt has `echo == 0`; the prompt was not truncated; and
`ssh_kbd_prompt_is_password()` accepts the sanitised text.

The saved password counts as used once the password method or any keyboard-interactive answer
has sent it. Every other prompt goes to `ask`: one-time codes, verification codes, PIN plus
token, a second password prompt, every echo prompt.

**Cancel.** The callback returns `void`, but it can still end the exchange. When `ask`
returns 0 (the user's Cancel, or a window close that cancelled the request),
`cancelled` is set, that prompt and every later one get an empty string without asking, and
the round calls `abort_io`, so libssh2's next send or receive fails at once with a socket
error instead of waiting on the server. The thread reports "Authentication cancelled."
whatever the return code, and tears the connection down.

### 5. The prompt shown to the user

`ConnUiKind` gains `CONN_UI_KBDINT`. `ConnUiRequest` gains `kb_name` and `kb_text` (the
instruction plus every prompt line but the last, both sanitised), `kb_prompt` (the prompt's
last line), `kb_echo` and `kb_offer_save`. The answer comes back in the existing
`passphrase[256]`, the tick in `save`. Each prompt is one `conn_ui_ask()`, made from inside
the round on the connect thread; the generation-numbered `ConnPromptSlot` already supports
prompts in sequence.

`ConnUiKind` and a new `conn_ui_kind_has_secret()` (true for `CONN_UI_PASSPHRASE`,
`CONN_UI_PASSWORD` and `CONN_UI_KBDINT`) move from `window.c` to `src/core/conn_prompt.h`, so
they are tested natively. The edit sites in `window.c`:

- the three tests of `kind == CONN_UI_PASSPHRASE || kind == CONN_UI_PASSWORD` (the cancel
  wipe, the copy-out in `conn_ui_ask()`, the copy-out in `WM_CONN_PROMPT`) switch to
  `conn_ui_kind_has_secret()`;
- the save line in `WM_CONN_PROMPT`, `if (req->kind == CONN_UI_PASSWORD && ans) req->save =
  save;` (about line 5011 at 1f85a3a), also accepts `CONN_UI_KBDINT`;
- `WM_CONN_PROMPT`'s final `else` shows the passphrase box for any other kind: the
  `CONN_UI_KBDINT` branch goes before it. Its save tick is `req->kb_offer_save &&
  config_find_profile_match(g_config, &s->conn_profile)`, evaluated there on the UI thread as
  the password box's is today; the connect thread never touches `g_config`.

**Sanitising.** The pure `ssh_kbd_sanitize(in, len, out, cap)` strips ESC sequences (CSI,
OSC to BEL or ST, two-byte ESC), other C0 controls and DEL; turns CRLF and a lone CR into
LF; turns TAB into a space; trims each line's trailing whitespace and drops empty edge lines;
replaces invalid UTF-8 with `?`; and caps the result at `cap` bytes on a UTF-8 boundary,
ending a truncated text with ASCII `...` (no UTF-8 marker goes through the ANSI APIs). The
text area's content is capped at 2 KiB after sanitising; there is no line cap. The name and
the label are single lines, with LF turned into a space. The output uses bare LF; the UI
layer (`pw_prompt.c`) converts LF to CRLF before it fills the multi-line EDIT, which shows a
bare LF as nothing.

**The dialog.** `PwPromptOpts` gains `text` (NULL, or the text-area content) and `echo`.

- With `echo` on, the field has no `ES_PASSWORD` and no `pw_reveal_attach` eye.
- The text area is a fixed-height read-only EDIT (`ES_MULTILINE | ES_READONLY |
  WS_VSCROLL`) that scrolls, so any amount of server text fits without changing the layout.
- The label (the prompt's last line) is a STATIC with `SS_NOPREFIX`, so a `&` in server text
  is shown, not taken as a mnemonic. *Amended:* the text area does not get `SS_NOPREFIX`. On
  an EDIT that bit (0x80) is `ES_AUTOHSCROLL`, which would turn word wrap off; an EDIT never
  reads `&` as a prefix, so nothing is lost.
- The caption is "SSH Authentication", plus " - name" (ASCII) when the server sent a name.
  The detail line is today's "profile — user@host". The save tick shows when
  `kb_offer_save` is set and the profile matches (above).
- Captions, server texts and answers go through the dialog's existing ANSI window APIs
  (`CreateWindowExA`, `GetWindowTextA`), as the password box does today. Known limit:
  non-ASCII server text shows in the ANSI codepage, as the detail line's em dash does now,
  and a non-ASCII answer is sent in the ANSI codepage, the same as today's password box.
  RFC 4256 asks for UTF-8.

*Amended.* No `tests/test_pw_layout*.c` exists, and `pw_layout()` is `static` in
`src/ui/pw_prompt.c`. It uses only the portable `ns_scale.h` and `ns_type.h`, so it moves to
`src/core/pw_prompt_layout.c/.h` as `pw_layout(dpi, has_detail, has_text, has_save,
PwLayout *)`. `has_text` adds one fixed-height text-area row; there is no line count. A new
`tests/test_pw_prompt_layout.c` checks rows and `client_h` with and without the text area,
the detail line and the save tick, at 96 and 192 DPI, and that today's combinations give
today's numbers.

### 6. The setup timeout and keepalive during authentication (*amended*)

The brief set timeout 0 for the whole call. But nothing could then cancel a network wait
inside it. `on_tab_close()` refuses a `CONN_CONNECTING` tab ("Connection in progress. Please
wait."), and `worker_life_cancelled()` is checked only between calls. A server that went
quiet mid-exchange would hang the tab until Nutshell exited.

**Decision: re-arm.** libssh2 measures elapsed time from the call's entry with `time()`, to
one-second resolution, and re-reads `api_timeout` at every wait (`session.h`
`BLOCK_ADJUST`, `session.c` `_libssh2_wait_socket`). The callback runs inside the call but
never inside a wait.

- `ssh_kbd_attempt()` records the entry time with `ctx->now()`.
- Each round ends, after the user's answers, with `set_timeout(ssh_kbd_rearm_ms(entry, now,
  SSH_KBD_ANSWER_WAIT_MS))`. `ssh_kbd_rearm_ms()`, in `src/core/ssh_timeout.c`, is
  `(now − entry) * 1000 + limit_ms + 1000`, saturating, with both times from `time()` like
  libssh2's. The extra 1000 ms absorbs `time()` granularity. User time never counts.
- `SSH_KBD_ANSWER_WAIT_MS` is 60 000. A Duo-style push approved within a minute of the
  answer therefore succeeds; one approved later fails with the timeout text. That is the
  stated limit. The 15 s `SSH_SETUP_TIMEOUT_MS` applies before the first prompt.
- **Only a round in which the user answered a prompt re-arms to 60 s.** A zero-prompt round,
  or a round whose prompts were all auto-answered or answered empty by the cap or a cancel,
  re-arms with the setup limit in force instead (`SSH_SETUP_TIMEOUT_MS`, or the test
  override), so a server that loops on empty rounds still fails in about 15 s per round.
- After the call, `ssh_kbd_attempt()` restores the setup limit in force (the override
  included). `window.c` 1841 still clears it to 0 before the session goes interactive.
- **The timeout text names the limit actually in force.** `conn_setup_timeout_error()` gains
  a `long limit_ms` argument: 60 s after an answered round, else the setup limit (15 s or the
  override). The context records which limit the last re-arm used; `ssh_session.c` gains
  `long ssh_setup_timeout_ms(void)` for the setup limit.

*Fallback, if the maintainer rejects this:* timeout 0, accepting that a silent server in
mid-exchange hangs the tab until the window closes.

**Keepalive off during setup.** `_libssh2_wait_socket` shortens each poll to the time left
before the next keepalive is due, and a poll that runs out returns `LIBSSH2_ERROR_TIMEOUT`
(`session.c` 609-643, 704-707). With today's `libssh2_keepalive_config(s, 1, 30)` in
`ssh_connect`, a wait inside a re-armed call could stop at the keepalive deadline and fail
the attempt as a timeout. So `ssh_connect` sets `libssh2_keepalive_config(s, 1, 0)` (off)
for setup, and keepalive `(1, 30)` is switched on through a new
`ssh_session_set_keepalive()` next to the `ssh_session_set_timeout(j->ssh, 0)` call at
`window.c` 1841, once the session is interactive. That is the only path that goes
interactive, and the `WM_TIMER` loop that drives `libssh2_keepalive_send` runs only after it.

**Detecting the timeout.** `ssh_session_timed_out()` already works for keyboard-interactive
and publickey: a wait that runs out returns -9 from `_libssh2_wait_socket`, which ends
`BLOCK_ADJUST` with rc -9 and last error `LIBSSH2_ERROR_TIMEOUT`. The overwrite to
`AUTHENTICATION_FAILED` or `PUBLICKEY_UNVERIFIED` (`userauth.c` ~2204, ~1684) fires only on
`_libssh2_packet_requirev`'s own 60 s read timeout, which is checked only when data arrives,
so on a silent server the wait bound fires first.

- After every attempt and every list call, the thread checks `ssh_session_timed_out()`
  first. When it holds, the error is the existing auth timeout text
  (`ssh_setup_timeout_text()`, with the limit in force) and authentication stops. Then it
  checks for a dead transport (section 2) and stops the same way, with the previous
  attempt's error.
- The plan never continues after a timeout. libssh2 leaves `userauth_kybd_state` or
  `userauth_pblc_state` non-idle (the EAGAIN path returns without resetting it), so a next
  call on that session would resume a half-finished exchange.

### 7. Cancel, tab close and window close during a prompt

- **Cancel in the dialog:** `abort_io`, then "Authentication cancelled." (section 4).
- **Tab close:** cannot happen while a prompt is up. The dialog is modal on the main window,
  and `on_tab_close()` refuses a connecting tab.
- **Window close** (End task, or a nested `WM_CLOSE`) runs `free_session()`, which calls
  `worker_life_cancel()` and then `conn_ui_request_cancel()`. `conn_ui_ask()` returns 0, the
  round takes the cancel path (empty answers, `abort_io`), libssh2's send fails at once on
  the shut-down socket, and the thread sees `CONN_CANCELLED()` and fails. The worker ends
  inside `WM_DESTROY`'s bounded wait; the `TerminateProcess` fallback in `ui_run()` is not
  needed for this case.
- **Reconnect:** `start_connection()` on a live job takes the same path.

### 8. Logging

- **INFO:** `connect.auth.methods` logs the parsed bit names, never the raw reply.
  `connect.auth.attempt` logs `method=`, `result=`, `rounds=`, `prompts=`, `auto_answered=`,
  `cancelled=` and `aborted=`.
- **Prompt texts:** sanitised, at DEBUG, through `DLOG_CONTENT`, so only with "Include
  terminal content" on. *Amended:* the brief said debug level only. A prompt can carry a user
  or host name, though, and debug-log spec section 6 puts those behind the content setting.
- **Answers:** never logged. Field names avoid what `tests/test_dlog_gate.c` rejects.
- **Secrets:** the saved password stays registered with `dlog_secret_add`. A typed answer is
  registered for the length of its round only when it is a non-echo answer of 3 bytes or
  more: `dlog_secret_add` ignores shorter strings and its table has 8 slots
  (`DLOG_SECRETS_MAX`), so echo answers are not registered.

**Unchanged behaviour, noted.** The `WM_DPICHANGED` font flush while a prompt is open is
pre-existing and not changed. A whitespace-only saved password and an empty username behave
as they do today.

### 9. Tests (native, run everywhere)

New `tests/test_ssh_auth_plan.c`:

- **Parsing:** NULL, empty, a single name, unknown names, spaces, empty items, duplicates,
  and the 128-byte display cap.
- **`ssh_auth_next()`:** profile type × saved secret × `saved_used` × each offered set ({},
  {pk}, {pw}, {kbd}, {pk,kbd}, {pw,kbd}, {pk,pw,kbd}, {other}); the order rule (saved
  password and both offered: password first; otherwise keyboard-interactive first); the
  password box after a refused saved password on a password-only server; the kbd-only retry;
  the re-query rule (at most once, after -18 or -19 only, only when nothing is left, never
  after a timeout, a dead transport or the last attempt); the passphrase retry counted; the
  four-attempt bound; `ssh_auth_transport_dead()` for -7, -13, -43 and not for -18.
- **`ssh_kbd_prompt_is_password()`:** positive rows "Password:", "password", "user@host's
  password:", "alice@NEWCASTLE.AC.UK's Password:", "SPINNAKER password:", "Password for
  alice:", padded and mixed case; one negative row each for "PASSCODE:", "One-time
  password:", "New password:", "Retype new password:", "Verification code:", plus `otp`,
  `token` and `pin` rows ("PIN password:") and an empty string.
- **`ssh_kbd_sanitize()`:** CSI, OSC, a lone ESC; CRLF, CR, LF; TAB to a space; C0 and DEL;
  invalid UTF-8; the 2 KiB cap with ASCII `...` at a UTF-8 boundary.
- **`ssh_auth_fail_text()`:** each wording, both kinds of `extra`, and truncation.

New `tests/test_ssh_kbd.c`, with fakes for `ask`, `set_timeout`, `abort_io` and the clock
that record their calls:

- prompts copied by length from buffers with no NUL and trailing garbage; NULL `text`,
  `name` and `instruction` with zero length (libssh2's `_libssh2_copy_string`); a prompt of
  2047, 2048 and 2049 bytes (the tail kept and `truncated` set once it does not fit, never
  auto-answered); responses `malloc`'d with `.length` equal to the answer's length (freed by
  the test);
- the saved password answers exactly one password-like prompt; a second one goes to the
  user, in the same round or a later one; echo and OTP prompts, a key profile's prompts and a
  truncated password prompt go to the user; the save tick only on a password-like prompt of a
  password profile that the user types into, never for a key profile; the latest typed
  password-like answer replaces an earlier one, and an unticked latest answer clears it;
- a zero-prompt round asks nothing and keeps the instruction;
- a 100-prompt round has 8 answered and 92 empty, with `too_many` set; round 17 answers empty
  and calls `abort_io` once; a cancel sends empty answers, asks nothing further and calls
  `abort_io` once;
- the re-arm call carries `(now − entry) * 1000 + 61 000` for the injected clock after an
  answered round, and `(now − entry) * 1000 + setup limit + 1000` after a zero-prompt or
  all-auto-answered round; the limit in force is recorded for the error text;
- `ssh_kbd_attempt()` leaves `*slot` NULL and the timeout at the setup limit in force (an
  override value included);
- a function declared with the stub's `LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC` macro calls
  `ssh_kbd_round()` (the adapter signature compiles natively);
- a sentinel in the answers and in the fake's scratch buffer proves the wipe.

Other test files: `tests/test_ssh_timeout.c` gains `ssh_kbd_rearm_ms()` (zero, large and
saturating elapsed time, a clock that goes backwards); `tests/test_pw_prompt_layout.c` is new
(section 5); `tests/test_key_auth.c` (real libssh2 only) gains NULL and not-connected tests
for `ssh_auth_list()` and `ssh_auth_kbdint()`. `tests/test_conn_prompt.c` gains
`conn_ui_kind_has_secret()` for every kind.

**Manual-check items** (no native test can reach them; listed in the PR body): keepalive off
during setup and on after `window.c` 1841; the save line accepting `CONN_UI_KBDINT`; the
`WM_CONN_PROMPT` `CONN_UI_KBDINT` branch with its save condition.

*Amended:* no `userauth_list` timeout test. The silent-listener fixture in `test_ssh.c` never
finishes the handshake, so it cannot reach `userauth_list`.

### 10. The libssh2 stub

`tests/stubs/libssh2.h` gains two typedefs, `LIBSSH2_USERAUTH_KBDINT_PROMPT` (`text`,
`length`, `echo`) and `LIBSSH2_USERAUTH_KBDINT_RESPONSE` (`text`, `length`), with real
libssh2's field types, and the `LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC(name_)` macro copied
from libssh2 1.11.1, so `src/core/ssh_kbd.h` and the adapter-signature test compile against
the stub. Nothing else is added; `ssh_session.c` and the SSH tests stay excluded without real
libssh2.

### 11. README and notes

- **README:** the authentication bullet (line 37) and "The password box" (line 559)
  say:
  - keyboard-interactive is supported, with OTP, PAM and TACACS-style prompts shown one at
    a time; with a saved password and both methods on offer the password method goes
    first (silent login as today), otherwise keyboard-interactive does;
  - a saved password answers only a prompt that is plainly a password prompt ("Password:",
    "user@host's password:"), never a passcode, code or new-password prompt, and only once;
    the save tick also appears on such a prompt;
  - after a refused saved password, the user is asked once more;
  - a push approval must come within a minute; agent and Pageant are not supported.
- *Amended (notes):* `main`'s Open list has no S-5 entry and no agent entry. The notes gain a
  Done line for this change and an Open item, "SSH agent / Pageant authentication".

### 12. Version, build, commit

- **Version:** one above both `main`'s and the branch's `APP_VERSION` at build time, never a
  literal written in advance, in both `resource.h` macros and README's `**Version**:` line.
- **Build:** `make clean && make release` on `automaton`, with the exe committed.
- **Line endings** as in Constraints, and **trailers** per CLAUDE.md with a `Models:` line.

## Left to the builder

- Exact wording of errors, captions and log keys, within the meaning above.
- Struct field sizes, as long as the caps are kept.
- The text area's fixed height, from the `ns_type.h` grid.
- Whether the methods error lists the raw names (sanitised) or the known names plus "others".
- The commit order.

## Verification before the gate

- **Automated:** `make test` (0 failed, 0 warnings, count noted); `make lint` (0 errors, 0
  warnings); `xvfb-run -a make wintest` (45 run, 0 failed); `check-version.sh origin/main`
  (PASS); the line-ending check.
- **Best-effort local check.** A new `tests/manual/kbdint_server.py`: a paramiko
  keyboard-interactive server on loopback with scripted scenarios (a plain "Password:", an
  OTP after a password, zero-prompt rounds, a disconnect after a wrong answer, a 300-byte
  Duo-style menu), and `tests/manual/README.md` on how to run it and point the Windows exe at
  it. The builder runs its self-test only if `python3 -c "import paramiko"` succeeds on
  `automaton`, and reports the result either way (paramiko was not installed there on
  2026-11-04).
- **Not run before this merge** (maintainer's instruction of 2026-11-04): the Windows
  hand-tests below and `Run-Integration.ps1 -Tier gate`. The PR body lists them as not run:
  - an sshd with `KbdInteractiveAuthentication yes` and `PasswordAuthentication no`, with and
    without a saved password, and with the save tick;
  - a stale saved password against `{password, keyboard-interactive}` (asked once more);
  - a PAM OTP prompt, and a `PASSCODE:` prompt (never auto-answered);
  - Cancel at the first and at a second prompt, and closing the window while a prompt is up
    (each ends at once);
  - a long, multi-line instruction (scrolls);
  - `AuthenticationMethods publickey,keyboard-interactive` (key accepted, then the prompts);
  - a key with a wrong passphrase (prompts; a second wrong one stops), and a key the server
    refuses (no prompt, then keyboard-interactive);
  - a server silent before the first prompt (fails in 15–17 s) and after an answer (60–62 s);
  - a prompt left open for over a minute, then answered (succeeds: user time never counts).

## Critique (2026-11-05, round 1)

1. Keepalive shortens libssh2's waits into false timeouts — ACCEPTED: off in `ssh_connect`, on at `window.c` 1841 (section 6).
2. `ssh_wait_exceeded()` was needless — ACCEPTED: dropped; `ssh_session_timed_out()` checked first, stop on timeout (sections 1, 6).
3. The callback could not abort the exchange — ACCEPTED: `abort_io` (`shutdown`) on cancel, window close and after 16 rounds (sections 4, 7).
4. Telnet's matcher accepts `passcode:` — ACCEPTED: narrow `ssh_kbd_prompt_is_password()` with negative test rows (sections 4, 9).
5. Method order differed from OpenSSH and PuTTY — ACCEPTED: keyboard-interactive first for password profiles; save tick on password-like prompts (section 2).
6. Re-querying after every failure wastes server tries — ACCEPTED, then ruled: the rc-0-unauthenticated trigger cannot occur in libssh2 1.11.x, so the list is re-queried exactly once after a -18/-19 refusal (section 2).
7. The 3-line instruction cap hid server text — ACCEPTED: scrolling read-only EDIT, 2 KiB, no line count in `pw_layout` (section 5).
8. `&` prefixes and the wide-API claim — ACCEPTED: `SS_NOPREFIX` on the label only (on the EDIT the bit is `ES_AUTOHSCROLL`); ANSI APIs and codepage stated (section 5).
9. 15 s is too short for a push approval — ACCEPTED: 60 s after an answer, 15 s before the first prompt (section 6).
10. The callback body was untestable — ACCEPTED: `ssh_kbd_round()` in `src/core`, stub typedefs, fourth `WM_CONN_PROMPT` site (sections 4, 5, 9, 10).
11. Re-arm arithmetic must match libssh2's clock — ACCEPTED: `time()` seconds × 1000, injected clock in tests (sections 6, 9).
12. A second wrong passphrase should not fall through — ACCEPTED: "Wrong passphrase for <key file>." (section 3).
13. A truncated prompt could be auto-answered — ACCEPTED: never (section 4).
14. The version was a literal — ACCEPTED: computed at build time (section 12).
15. Missing notes on the DPI flush, secret slots and edge inputs — ACCEPTED (section 8).
16. No change.

## Critique (2026-11-05, round 2)

1. Save tick on key profiles — ACCEPTED: password profiles only (Non-goals, section 2).
2. Line breaks, TABs and the truncation marker through ANSI APIs — ACCEPTED: the UI converts LF to CRLF for the EDIT, TAB becomes a space, ASCII "..." (and an ASCII caption separator) (section 5).
3. 256-byte prompt buffer cut the label off — ACCEPTED: 2048 bytes, tail kept, `truncated` set, never auto-answered (section 4).
4. Substring negatives rejected real prompts; order broke silent login — ACCEPTED: whole-word negatives, `user@host's ` and ` for <user>` stripped; password first when saved and both offered, else keyboard-interactive first; three new test rows (sections 2, 4, 9).
5. Edit sites incomplete — ACCEPTED: the save line also accepts `CONN_UI_KBDINT`; `config_find_profile_match()` stays on the UI thread with `kb_offer_save`; `ConnUiKind` and `conn_ui_kind_has_secret()` move to `conn_prompt.h` (section 5).
6. Which typed answer is saved — ACCEPTED: the latest password-like typed answer wins, its own tick decides (section 2).
7. Timeout text always said 15 s — ACCEPTED: `conn_setup_timeout_error()` takes the limit in force (section 6).
8. README's "asked once more" false on a password-only server — ACCEPTED: `PASSWORD_ASK` after a refused saved password, counted toward the cap (section 2).
9. Re-query fired while steps remained — ACCEPTED: only when `ssh_auth_next()` has nothing left after -18/-19 (section 2).
10. Plan continued on a dead session — ACCEPTED: a failed re-query or a socket-error last errno stops with the previous error; no password box for a dead session (sections 2, 6).
11. Zero-prompt rounds got 60 s — ACCEPTED: only an answered round re-arms to 60 s; others use the setup limit (section 6).
12. Verification claimed hand-tests — ACCEPTED: Windows hand-tests and `-Tier gate` listed as not run (maintainer, 2026-11-04); `tests/manual/kbdint_server.py` added, run only if paramiko imports (Verification).
13. Test gaps — ACCEPTED: NULL zero-length strings, the 2047/2048/2049 boundary, the stub's `LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC`, manual-check items named (sections 9, 10).
14. Core leaked `SshSession`, the override and the cap — ACCEPTED: `ssh_kbd_attempt(void **slot, ...)`, restore honours the override, the passphrase retry counts, `cached_passphrase` noted as never read (Non-goals, sections 3, 4, 6).
