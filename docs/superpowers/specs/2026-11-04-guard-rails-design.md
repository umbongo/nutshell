# PR 1: guard rails — design

Date: 2026-11-04. Status: scope approved by the maintainer on 2026-11-04;
the decisions below are the author's unless marked "maintainer's decision".

Eleven fixes from the external code review of 2026-11-04, each checked
against the code before it was accepted. They ship in one pull request with
one version bump and one Windows build at the end. Branch
`fix/code-review-2026-11-04`.

## Why

The review found the places where Nutshell trusts something it should not:
the model's reply, the address a fetched URL resolves to, a server that never
answers, a custom endpoint without TLS. It also found smaller faults: a test
that reads freed memory, a JSON parser with no depth limit, lint errors, and
README claims that are no longer true. None of these is exploited today. Each
one becomes a real problem once the assistant runs unattended or a hostile
host or page gets the model's attention.

## Goals

1. An assistant on Auto-approve runs at most three rounds after a user
   message. A fourth round waits for the user's click.
2. `web_fetch` never reaches a loopback, private or link-local address, by
   the URL it was given or by any redirect. It finishes within 30 s, stops
   when the user cancels, and shows every address it fetches in the chat.
3. An `[EXEC]` marker inside a code fence or an inline code span is never
   run. The approval card shows it as "quoted, not run".
4. An SSH server that accepts the TCP connection but never finishes the
   handshake or authentication cannot hang a tab: the attempt fails after
   15 s with a message in the tab that says so.
5. Every WinHTTP session uses the system's automatic proxy settings and
   TLS 1.2 or newer, set up in one place.
6. A custom AI base URL that sends the key in clear text across the network
   shows a warning in Settings.
7. The settings copy says plainly what leaves the machine: debug-log text
   the assistant reads goes to the provider, and web fetch can send session
   text to any address.
8. A deeply nested JSON document fails to parse instead of overflowing the
   stack.
9. `make test` builds with no warnings, and the suite runs clean under ASan
   and LeakSanitizer for the two known faults.
10. `make lint` reports no errors and no warnings. Dead code found by the
    review is removed.
11. README states the real test counts and the real lint status, the full
    Linux build dependencies, and a threat model.

## Non-goals

- Treating terminal output as untrusted data in the system prompt (review
  item AI-1, beyond the one prompt line in item 3), prompt caching, and
  per-tool approval gates (`ToolSafety` stays unread). Later pull requests.
- SSH keyboard-interactive or agent authentication, algorithm preferences
  (`ssh-dss`), and the `sshclient` known-hosts path name.
- A telnet pre-connect warning.
- Running `make lint` in CI.

## Anti-goals (would be a failure even if it technically works)

- **A guard that fails open.** If `web_fetch` cannot resolve a name, cannot
  read the address, or cannot parse a redirect, it refuses. It never fetches
  first and checks afterwards.
- **A quoted marker that disappears without a trace.** The user and the
  model are both told that it was not run (maintainer's decision).
- **A round cap that strands work.** Commands the user has already approved
  still run. A paused round keeps everything on screen and continues with
  one click.
- **A timeout that cuts off a working session.** The 15 s limit covers only
  connection setup and goes back to 0 (no limit) before the first keystroke.
- **Changing what the gate or CI checks** to make any of this pass.

## Constraints

- Every rule in `CLAUDE.md` applies. In particular:
  - TDD: tests come first, and every piece of logic in `src/core/` gets
    native tests (positive, negative and corner cases). `src/ui/` cannot be
    tested natively, so each decision below puts the logic that can be
    tested in `src/core/` and leaves only Win32 glue in `src/ui/`.
  - Code compiles clean with `-Werror` under both MinGW and native gcc.
  - `winsock2.h` comes before `windows.h` in any file that needs both
    (`getaddrinfo` in `ai_tool_web_fetch.c`).
- Design-system rules apply to every new UI text and control: no `RGB(`,
  colours from `ns_tokens()` (the warning intent for item 6), sizes from
  `ns_type.h`, `ns_scale`, `ns_font`, `ns_hover`. `tests/test_ui_tokens.c`
  must still pass unchanged.
- Any buffer that held a secret is wiped with `secure_zero()`. No new code
  logs a key, a password or a URL's query string.
- **Line endings are preserved.** These touched files are CRLF and stay
  CRLF: `src/core/ai_prompt.c`, `src/core/ai_prompt.h`,
  `src/config/json_parser.c`, `src/ui/ai_http_win.c`,
  `src/crypto/crypto.c`, `src/crypto/crypto.h`, `tests/test_crypto.c`,
  `tests/test_string_utils.c`, `Makefile`. Check
  `git diff --stat --ignore-cr-at-eol` before review. New files are LF.
- **Version:** one above the higher of `main`'s version and the branch's
  last build, worked out at build time (both `resource.h` macros and
  README's `**Version**:` line). Never a number fixed in this spec. `main`
  is at v1.2.50 on 2026-11-04.
- CodeQL must stay clean. A test that creates a file opens it with
  `test_fopen_private()` and builds the path from `TEST_TMP_DIR`.

## What has been decided

### 1. The round cap

**What exists.** A batch that finishes running sends a continue message
(`send_continue_message()`, `ai_chat.c` ~1911, which calls
`launch_stream_thread(d, "continue")`). It is called from
`dispatch_cancel()` (~2105) and from the dispatcher's finish path in
`dispatch_tick()` (~2566). With Auto-approve on, the reply's commands are
approved as they arrive and dispatched at once (~4782), so
reply → run → continue → reply loops with no limit. `AI_TOOL_LOOP_MAX`
bounds only the tool calls within a single stream. With Auto-approve off,
every batch already waits for a click, so only Auto-approve rounds are
unattended.

**Decision: pause Auto-approve rather than hold the continue message.**
After three unattended rounds, the next reply's batch is built with
Auto-approve forced off, using `cmd_policy_attended()`. That is the same
call the background-tab path already uses (~4508). The batch's rows arrive
PENDING on an ordinary approval card. Nothing runs and no further continue
message goes out until the user clicks Run, so the card is the "asks before
continuing" step and no new card type is needed. A status row directly
above the card says why, e.g. `[Auto-approve paused after 3 automatic
rounds — Run the card to go on]`. The session's policy itself does not
change: the Auto-approve button stays on, and only this one batch is
attended. *Cost, accepted:* the fourth model call still happens (the reply
that proposes the paused commands). The alternative is to hold the continue
text itself behind a new Continue/Stop row. That needs a new chat item
type, new hover ids, and held state that survives tab switches. It is noted
here in case the maintainer prefers it.

**The counter.**
- It lives in `AiSessionState` (`src/core/ai_prompt.h`), so it is per
  session and survives tab switches: `int auto_rounds`. The cap is
  `#define AI_AUTO_ROUNDS_MAX 3` beside it.
- Pure helpers in `src/core/` (e.g. in `ai_prompt.c`), tested natively:
  - `ai_rounds_user_message(AiSessionState *)` sets the counter to 0.
  - `ai_rounds_continue(AiSessionState *, int unattended)` adds 1 for an
    unattended batch and resets to 0 for an attended one.
  - `ai_rounds_batch_policy(const AiSessionState *, CmdPolicy p)` returns
    `p`, or `cmd_policy_attended(p)` once the counter has reached the cap.
- **Reset** in `send_user_message()` (the user typed a message), and
  whenever the user starts or stops a batch by hand.
- **Count** in `send_continue_message()`, only when the batch that just
  finished ran without a user action. A `CmdBatch` gains `int user_acted`
  (`src/core/cmd_batch.h`). It is set by the Run/Approve click paths
  (`dispatch_start()` callers at ~4023, ~4083, ~4125) and by a
  user-initiated `dispatch_cancel()`. It is read in the two finish paths and
  passed to `send_continue_message()`. A batch the user clicked Run on is an
  attended round, so it resets the counter rather than counting towards the
  cap.
- `retry` (`IDC_ACTIVITY_RETRY`) neither counts nor resets.

### 2. `web_fetch`: address check, manual redirects, deadline, rows

**What exists** (`src/core/ai_tool_web_fetch.c`). There is a scheme prefix
check only. WinHTTP follows redirects automatically. Each phase has a 10 s
timeout (`ai_chat.c` ~1334) but there is no overall deadline. `cancel_flag`
is ignored (`(void)cancel_flag`, ~145). Two bugs found during the survey:
the URL's port is ignored (`uc.nPort` is never used, so the connect always
goes to 80/443), and the `User-Agent` header is added empty (`w_ua` is built
but never passed in). Both get fixed in the rewrite, because a redirect to a
non-default port needs the first.

**Address policy: a new pure module `src/core/net_guard.c/.h`** with no
`windows.h`, tested natively in a new `tests/test_net_guard.c`:
- `int net_ipv4_is_internal(const unsigned char a[4])` refuses
  `127.0.0.0/8`, `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`,
  `169.254.0.0/16`, and also `0.0.0.0/8` (many stacks route 0.0.0.0 to the
  local machine).
- `int net_ipv6_is_internal(const unsigned char a[16])` refuses `::1`,
  `::` (unspecified), `fc00::/7`, `fe80::/10`, and IPv4-mapped
  `::ffff:0:0/96` and IPv4-compatible `::/96` addresses whose embedded IPv4
  address is internal (delegated to the v4 test).
- Not refused: `100.64.0.0/10` (CGNAT), multicast, and the NAT64
  `64:ff9b::/96` prefix. They are outside the approved list; adding them
  later is one line each.
- Addresses are passed as bytes, not `sockaddr`, so the tests need no
  socket headers.

**Where the check runs.** Before each request (each hop), the fetch
resolves the host it is about to contact with `getaddrinfo` (ws2_32 is
already linked). It refuses if **any** returned address is internal, and it
refuses if resolution fails (fail closed). *Residual risk, accepted and
written in a code comment:* WinHTTP resolves the name again itself, so a DNS
answer that changes between the two lookups (rebinding) is not caught. A
configured proxy also does its own resolving. Pinning the checked address
would break TLS name checking.

**Shape: a portable fetch loop over an injectable backend**, following the
`CryptoDpapiBackend` pattern. The hop loop, redirect handling, deadline and
cancel checks are the security-critical part, so they sit in portable code
(`ai_tool_web_fetch.c`, outside `#ifdef _WIN32`) and call a small backend
struct:
- `resolve(host, out_addrs)`
- `get(url, timeout_ms, out_status, out_location, out_body)`, which makes
  one request and never follows redirects
- `now_ms()`

The WinHTTP backend sits behind `#ifdef _WIN32`. Tests inject a fake (in
the style of `tests/fake_dpapi.c`) and cover refusal at the first hop and at
a later hop, the redirect cap, the deadline, cancel, an https→http
downgrade, and relative `Location` values. The native build without a fake
keeps today's "not available" result.

**Redirects.**
- Automatic redirects are disabled on the request
  (`WINHTTP_OPTION_DISABLE_FEATURE` with `WINHTTP_DISABLE_REDIRECTS`).
- On 301, 302, 303, 307 or 308 the fetch reads `Location`
  (`WINHTTP_QUERY_LOCATION`) and resolves it against the current URL with a
  pure `net_url_resolve(base, location, out, out_sz)` in `net_guard.c`. It
  accepts absolute http(s), scheme-relative `//host/p`, absolute-path and
  relative references, and refuses any other scheme and over-long results.
- Then the address check runs again for the new host.
- **At most 5 redirects are followed** (up to 6 requests). The next one
  fails with "too many redirects".
- An https→http redirect is refused, matching WinHTTP's own default policy.

**Deadline and cancel.**
- `WEB_FETCH_TOTAL_MS 30000` and `WEB_FETCH_MAX_REDIRECTS 5` go in
  `ai_tool_web_fetch.h`.
- Each hop's WinHTTP timeouts are the smaller of 10 s and the time left
  before the deadline.
- The cancel flag (the stream's abort flag, already passed in) is checked
  before each hop, after each blocking call, and between body reads. On
  cancel or deadline the fetch returns an error result without a body.
- `getaddrinfo` has no timeout of its own. The time it takes counts against
  the deadline.

**Rows in the chat.**
- `WebFetchContext` gains a progress callback and a user pointer
  (`void (*on_hop)(void *user, const char *url, int refused)`). Each stream
  has its own copy of the context (`ai_stream_copy_tools()`), so the stream
  thread sets the callback on `s->fetch` to a function in `ai_chat.c` that
  calls `post_tool_msg(s, CHAT_ITEM_TOOL_CALL, …)`.
- One row per hop: `fetching: <url>`, or `fetch refused: <url> (local or
  private address)`. The row is non-blocking: it informs and asks for
  nothing.
- The displayed URL drops its query string and fragment, has control and
  bidi characters removed, and is cut to a readable length.
- These rows have the same lifetime as today's tool rows: live in the
  displayed session, dropped for a background session (the
  `WM_AI_TOOL_MSG` handler, ~4261), and not rebuilt on a tab switch. The
  rebuilt `using tool: web_fetch` row (`chat_rebuild_display()`, ~1236)
  gains the requested URL from the call's input, so a rebuilt thread still
  shows what was asked for.

**Settings copy.** The `IDC_AI_WEB_FETCH` tooltip (`settings.c` ~465)
becomes: "Allow the AI to fetch web pages as a tool call. The assistant can
then send text from this session to any web address." The checkbox label
"Permit Web Fetch" (~1498) stays.

### 3. Quoted `[EXEC]` markers

**The rule:** a marker is quoted when its opening `[EXEC]` lies inside a
fenced code block body or inside an inline code span. It matches what the
renderer draws as code, so what the user sees as code is never run:
- **Fences:** exactly the renderer's fences, found with
  `md_code_blocks()` (`src/core/md_blocks.c`, which uses
  `md_classify_line`): a line that starts with three backticks at column 0.
  An indented fence is not a fence (same as the renderer). **An
  unterminated fence runs to the end of the reply**, so every marker after
  it is quoted.
- **Inline spans:** a backtick opens a span that closes at the next
  backtick on the same line. A backtick with no partner on its line opens
  nothing, as in `md_parse_inline` (`src/core/markdown.h` ~163). Spans never
  cross a line.
- A marker whose `[EXEC]` is outside any span is live even if its command
  contains backticks: `[EXEC]echo `date`[/EXEC]` runs as before.

**Interfaces** (`src/core/ai_prompt.c/.h`):
- A shared static scanner yields each marker with a `quoted` flag. Three
  places use it: `ai_extract_commands_notes()` (~1381),
  `ai_extract_command()` (~1235) and `ai_response_split()` (~1482). The
  split must not cut the shown text at a quoted marker.
- New `ai_extract_commands_full(response, cmds, notes, quoted[],
  max_cmds, rejected, rejected_long)` returns live and quoted markers in
  reply order, with `quoted[i]` set for the quoted ones. Quoted markers go
  through the same unsafe-character and 1024-byte checks as live ones.
- `ai_extract_commands_notes()` keeps its signature and becomes a wrapper
  that passes `quoted = NULL`, which skips quoted markers. So
  `ai_extract_commands()`, `_ex()`, `_notes()` and all 26 existing test
  calls never see a quoted marker as a command. That makes the safe
  behaviour the default for any caller that does not ask for quoted
  markers.
- Both reply paths in `ai_chat.c` (the background-session one at ~4501 and
  the active one at ~4605) switch to `_full`.

**On the card.**
- A quoted marker becomes a row that cannot run: it is added with status
  `APPROVE_BLOCKED` plus a new `int quoted` on `ApprovalEntry`
  (`chat_approval.h`). A new `chat_approval_add_quoted()` skips the policy
  decision.
- Blocked rows already have a disabled checkbox and dimmed text, are never
  dispatched, and are excluded from "Run N selected".
- `ChatMsgItem.u.cmd` gains `quoted`. `chat_cmd_label()` (`chat_msg.h`
  ~185) gains a `quoted` input and returns **"quoted, not run"** for such a
  row instead of "held" (maintainer's decision for the wording).
- The header's held count excludes quoted rows.
- A reply whose markers are all quoted still gets a card with only these
  rows, the same as an all-held card today.
- `ai_build_continue_text()` reports a quoted entry as "quoted in a code
  block, not run".

**Telling the model.** Rejected blocks already get a note (~4656). In the
same way, when a reply had quoted markers, a user-role note is added: "NOTE:
N [EXEC] marker(s) were inside a code block or inline code and were NOT run.
Put each marker on its own line, outside any code fence."

**Prompt.** The command instructions in `ai_build_system_prompt_ex()`
(`ai_prompt.c` ~253-305) gain one sentence in both `rest` and
`rest_powershell`: "Put each [EXEC]...[/EXEC] marker on its own line, never
inside a code block or inline code: a marker inside code is shown but not
run." The tests that pin prompt text are updated in the same commit.

### 4. SSH handshake and authentication timeout

**What exists.** `ssh_connect()` (`src/term/ssh_session.c` ~130-266) puts
the socket back in blocking mode after a 4 s non-blocking connect, then
calls `libssh2_session_handshake()` (~251) with no timeout. Authentication
(`ssh_auth_password()` / `ssh_auth_key()`, ~269-277), channel open, PTY
request and shell request then run blocking in `connection_thread()`
(`src/ui/window.c` ~1547-1807) until `ssh_session_set_blocking(j->ssh,
false)` at ~1807.

**Decision.**
- `#define SSH_SETUP_TIMEOUT_MS 15000` goes in `ssh_session.h`.
- `ssh_connect()` calls `libssh2_session_set_timeout(session,
  SSH_SETUP_TIMEOUT_MS)` right before the handshake. The limit then covers
  the handshake, authentication, channel open, PTY request and shell
  request: everything up to "interactive".
- `ssh_session.h` gains two functions:
  - `ssh_session_set_timeout(SshSession *, long ms)`, which `window.c`
    calls with `0` immediately before `ssh_session_set_blocking(j->ssh,
    false)`;
  - `int ssh_session_timed_out(const SshSession *)`, which reads
    `libssh2_session_last_errno() == LIBSSH2_ERROR_TIMEOUT`, so `window.c`
    never includes `libssh2.h`.
- The timeout does not run while Nutshell is waiting for the user (host-key
  dialog, passphrase or password box), because no libssh2 call is in flight
  then.
- **How the error reaches the user.** Use the existing SSH failure path:
  `j->error` → `WM_CONN_DONE` → a line in the tab
  (`term_process`, ~5079) plus the "Connection Error" message box
  (~5082). Only the text is new. It names the stage that stopped:
  - "Timed out after 15 s waiting for host:port during the SSH handshake."
  - "… during authentication."
  - "… while opening the session."

  `ssh_connect()` writes the handshake text into `last_error`. The auth and
  channel failures in `connection_thread()` check `ssh_session_timed_out()`
  before choosing between that text and today's "Authentication failed" or
  "Could not open SSH channel". The tab goes to `TAB_DISCONNECTED` as
  today.
- `ssh_session.c` is compiled only by the real-libssh2 test build (hosted
  `Native tests`; it is in `NON_TEST_SRCS` under the stub), so no stub
  change is needed. `libssh2_session_set_timeout` exists in every libssh2
  the builds use.

### 5. One WinHTTP session helper

**Decision:** a new `src/core/winhttp_open.c/.h` (the name is the
builder's) with `HINTERNET ns_winhttp_open(void)`, declared and defined only
under `#ifdef _WIN32`. The native build compiles the file to a translation
unit that only holds a typedef, because `-Wpedantic` rejects an empty one.
It lives in `src/core/` because two of its callers are there behind
`#ifdef _WIN32` (`ai_tool_web_fetch.c` ~227; `ai_tool_web_search.c` ~408 and
~534), and `src/ui/ai_http_win.c` (~77, ~231, ~392) can include a core
header but core cannot include a ui one. It does three things:
- `WinHttpOpen(L"Nutshell/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, …)`,
  falling back to `WINHTTP_ACCESS_TYPE_DEFAULT_PROXY` when that fails (the
  automatic type needs Windows 8.1 or later, and the tree targets
  `0x0601`).
- Sets `WINHTTP_OPTION_SECURE_PROTOCOLS` to TLS 1.2 | TLS 1.3, falling back
  to TLS 1.2 alone when the option is refused (Windows 10 before 1903
  rejects the 1.3 flag). This happens unconditionally: the option affects
  only https connections.
- Leaves timeouts to the caller, because each caller's differ.

All six sites call it. The three `use_ssl`-guarded `SecureProtocols` blocks
in `ai_http_win.c` (~88-93, ~241-246, ~403-408) go. `web_fetch`'s
redirect-disable option is set per request in its own code, not in the
helper.

### 6. Clear-text custom base URL warning

**The test function** goes in `net_guard.c`:
`int net_url_cleartext_remote(const char *url)` returns 1 when the scheme
is `http://` and the host is not loopback. Loopback means `localhost`
(case-insensitive, with or without a trailing dot), any dotted-quad
`127.x.x.x`, or `::1` written as `[::1]`. It parses the host itself,
including bracketed IPv6 and `user@` userinfo. It returns 0 for `https://`,
for an empty string, and for anything that is not http(s). It checks text
only and never does a DNS lookup. A name that resolves to loopback (say
`myhost`) still warns, and that is the safe direction.

**The UI.**
- A one-line static sits directly under the Base URL field on the AI
  Provider page (`settings.c` ~1391-1396). Its text is in the warning
  intent's colour: "Not encrypted: your API key and terminal text cross the
  network in plain text."
- It is visible only when the provider is `custom` and
  `net_url_cleartext_remote()` is true. The test reruns on `EN_CHANGE` of
  `IDC_AI_CUSTOM_URL` and on the provider combo's `CBN_SELCHANGE`, and the
  row shows and hides like `ctrl_ai_base_url`.
- Its colour comes from `ns_tokens()->warning.base` in the page's
  `WM_CTLCOLORSTATIC` (~793), next to the dim-static check.
- No message box. Saving is not blocked.

The URL is still stored as typed (~1977-1979).

### 7. Debug-log copy

- The `IDC_DBG_ENABLE` tooltip (`settings.c` ~420) gains: "When AI Assist
  reads the log, the text it reads is sent to your AI provider."
- **The hint at ~1034 is left as it is.** The survey found that the page's
  `SDBG_NOTE` (~1116-1118) already says "AI Assist can read it too; what it
  reads goes to your AI provider". It is shown under the same condition as
  the hint (debugging on), and lengthening the hint would also change its
  `DBG_SLOTS` row count (`settings_layout.c` ~212) and that row count's test
  in `test_settings_debug.c`. The tooltip covers the moment before the box
  is ticked. If the maintainer still wants the hint changed, it becomes two
  rows and that row count and its test change with it.

### 8. JSON nesting cap

`Parser` (`src/config/json_parser.c` ~9-13) gains `int depth`, and
`json_parser.h` gains `#define JSON_MAX_DEPTH 256`. `parse_array()` and
`parse_object()` increment the depth on entry and decrement it on every
exit. Going past 256 sets `p->error`, the partial node is freed as on any
other parse error, and `json_parse()` returns NULL. The `Token` stays a
by-value member of `Parser`, so recursion frames stay small. The recursive
`json_free()` is bounded by the same cap, since only parsed trees exist.
Tests go in `tests/test_json.c`: depth 256 for arrays, objects and mixed
nesting parses; depth 257 fails cleanly; and a deep failure leaks nothing
(the builder runs it under ASan locally).

### 9. Test hygiene

- `tests/test_cmd_batch.c` ~93-95: save `b->id` in a local before
  `cmd_batch_remove()` and use the local afterwards.
- `tests/test_ui_demo.c`: every test that calls `ui_demo_build()` calls
  `ai_conv_reset()` on each conversation it filled, before reusing it and
  before returning (only ~553 and ~588 do today). There is no product-side
  `ui_demo_free`, because production frees through `free_session()`.
- The seven warnings:
  - `-Wcomment` ×3: `test_ui_tokens.c` ~451, ~467 and ~676 contain
    `src/ui/*.c` inside block comments. Reword the comments, for example to
    `src/ui/<file>.c`.
  - `-Wformat-truncation`: `test_session_manager.c` ~67 and
    `test_string_utils.c` ~396. The truncation is what those tests check.
    Keep the behaviour and silence the warning with a checked return
    (`int n = snprintf(…); ASSERT_TRUE(n >= (int)sizeof …)`), not with a
    pragma.
  - `-Wunused-parameter`: `test_cmd_collapse.c` ~42.
  - The `_POSIX_C_SOURCE` redefinition: `crypto.c:2` becomes
    `#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)`. The test build
    passes `-D_POSIX_C_SOURCE=200809L` (`Makefile` ~118).

### 10. Lint and dead code

- **`dlog.c`'s empty `va_list`.** Change `emit_v()` (~356) to take
  `va_list *ap`, where NULL means "no arguments". The two variadic callers
  (`dlog_emit` ~429, `emit_forced` ~445) pass `&ap`; their `va_list` is a
  local variable, so taking its address is portable. The three no-argument
  sites (`dlog_emit_fields` ~436, `dlog_emit_content` ~515,
  `dlog_issue_mark` ~1430) pass NULL and drop their `memset`. The format
  branch reads `*ap`.
- **The `dlog.c:289` false positive** gets
  `// cppcheck-suppress arrayIndexOutOfBoundsCond` with a one-line reason.
  **The `lint` target must gain `--inline-suppr`** (`Makefile` ~246);
  without it cppcheck ignores inline suppressions.
- **`window.c` ~2744:** the `if (bpm)` inside the branch that already
  handles `bpm` is always false. It goes, together with its body.
- **`crypto.c`:** `crypto_derive_key()` and `PBKDF2_SALT` ("CongaSSH-v1")
  have no caller anywhere, tests included. They go, along with the
  declaration and comment in `crypto.h`.
  **`crypto_encrypt_with_key()` / `crypto_decrypt_with_key()` stay**,
  because 16 tests in `tests/test_crypto.c` use them, which the scope's
  "if nothing including tests needs it" rule excludes. The maintainer may
  prefer porting those tests to `crypto_encrypt()`/`crypto_decrypt()` (the
  live legacy-migration path, already called natively by `test_config.c`)
  and then deleting the pair.
- After these changes the builder runs `make lint` and records the counts.
  The review measured cppcheck 2.13.0 at 6 errors, 1 warning and 78 style
  findings. The expected result is 0 errors, 0 warnings and about 77 style
  findings, which decides item 11's wording.

### 11. README

- **Counts:** "N unit tests across M test files" at L46 and in the
  directory tree at L675, with N from `make test`'s "Tests Run" and M from
  `ls tests/test_*.c | wc -l` **at build time** (3,306 / 126 on
  2026-11-04). **No test pins them.** Almost every pull request changes the
  count, so a pin like `test_shortcuts.c` would force a README edit in each
  one and fail CI whenever someone forgot, for numbers nobody relies on.
  The retrospective refreshes them instead.
- **Build Date:** drop the line (L6). The `**Version**:` line keeps its
  trailing ` \` because `**Author**` follows it. Nothing reads the line:
  `check-version.sh` reads only `**Version**:`.
- **Lint claim** (L46 "zero lint warnings", L748 "Zero warnings
  required."): if `make lint` still reports style findings after item 10,
  reword both to what is true, for example "cppcheck reports no errors or
  warnings" and "Run `make lint` before committing; it must report no errors
  or warnings."
- **Linux build** (L705-712): add `libssl-dev` (OpenSSL headers for the
  native `make test`; CI installs it, `checks.yml` ~96).
- **Security section** (L329): a threat-model paragraph covering these
  points:
  - the remote host and everything it prints are untrusted;
  - the AI provider receives the last N terminal lines (the context-lines
    setting) and anything the assistant reads (debug log, fetched pages);
  - web fetch lets the model contact any public web address and send it
    text from the session;
  - a local shell runs with your Windows privileges;
  - `nutshell.config` in a shared folder exposes its non-secret fields
    (hosts, usernames, settings). The secrets stay DPAPI-locked.
- **Command Execution** (L240-250): one sentence. "The dispatcher treats a
  line ending in `$`, `#`, `%` or `>` as a shell prompt, so a REPL or a
  running program whose line ends that way can receive the next approved
  command."

## Left to the builder

- File and function names (`net_guard`, `winhttp_open`, the
  `ai_rounds_*` helpers, the fetch backend struct), provided the split
  between pure code in `src/core/` and Win32 code is kept.
- The exact wording of status rows, error texts and the card's header when
  it has quoted rows, within the meaning given here.
- How the SSH timeout is tested. One option is a test in `tests/test_ssh.c`
  (real-libssh2 build only) against a local listener that accepts and stays
  silent, with the limit lowered through a setter so the test does not take
  15 s. Otherwise the manual check below.
- Whether `web_fetch` also checks the connected address with
  `WINHTTP_OPTION_CONNECTION_INFO` when no proxy is in use. It narrows the
  rebinding gap, but only after the request has been sent.
- The order of commits inside the pull request.

## Verification before the gate

- `make test` passes with 0 warnings (`grep -c 'warning:'` is 0), the
  count is noted, and an ASan/LeakSanitizer run of at least
  `test_cmd_batch` and `test_ui_demo` is clean.
- `make lint`: 0 errors and 0 warnings.
- On `automaton`: `make clean && make release` with the version rule
  above, then `xvfb-run -a make wintest`.
- On Windows, by hand:
  - `web_fetch` refuses `http://127.0.0.1/`, `http://[::1]/`,
    `http://169.254.169.254/` and a public URL that redirects to one of
    them, shows a row for each hop, and follows a normal redirect chain;
  - SSH to a port that accepts but never speaks (e.g. `nc -l` on a test
    box) fails after about 15 s with the handshake message;
  - Settings shows the warning for `http://example.com/v1` and not for
    `http://localhost:11434/v1` or any `https://` URL;
  - `--ui-demo=all` on the four themes shows nothing out of place.
- The integration harness's `-Tier gate` cases that touch the AI panel and
  the connect path.
