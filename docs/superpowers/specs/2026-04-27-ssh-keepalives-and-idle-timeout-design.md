# SSH Keepalives, User-Idle Timeout, and Settings Tooltips — Design

**Date:** 2026-04-27
**Status:** Spec — pending implementation plan

## Problem

Today an SSH session in Nutshell is torn down after 90 seconds of "no channel
data received" ([window.c:42-45](../../../src/ui/window.c#L42-L45),
[window.c:1960-1996](../../../src/ui/window.c#L1960-L1996)). On an *idle but
healthy* connection, no channel bytes flow — the libssh2 protocol-level
keepalive replies are consumed inside libssh2 and never reach the channel
layer. The 90s rail therefore false-fires whenever the user steps away. There
is no separate notion of "the user has been idle for X minutes," only the one
broken rail doing both jobs badly.

The user wants:

1. Idle SSH sessions to stay connected until the network actually fails.
2. A separate, configurable user-idle timeout (defaults to **0 = never**) so
   sessions can optionally be torn down after a long period of no user
   activity.
3. Hover tooltips on every field in the Settings dialog.

## Decisions (from brainstorming)

- **Q1 → revised:** "User activity" bumps the idle timer on **keystrokes
  forwarded to the channel, mouse-wheel scroll on the terminal, tab switch,
  and AI chat input.** Mouse movement, window resize, paint, focus change,
  and incoming AI streaming output do **not** count.
- **Q2:** Detect network failure with **both** OS TCP keepalive **and**
  app-level liveness derived from libssh2 recv activity (belt + braces).
- **Q3:** On user-idle timeout, disconnect with a banner
  `\r\n[Disconnected after N min idle]\r\n`, matching existing
  `[Connection timed out]` / `[Connection Closed]` patterns.
- **Q4:** **Global setting** in the Settings dialog. No per-profile override.
- **Q5:** Network-failure detection is hardcoded always-on. **User-idle is
  disable-able** with `0` (the default) meaning "never".
- **Q6:** Hover tooltip on **every** Settings field.

## Architecture

Three independent concerns, each with a clear owner.

### 1. Network-failure detection (replaces the 90s false-positive rail)

Two layers, both always on:

**OS TCP keepalive** — set on the socket immediately after `connect()`
succeeds in `ssh_session_connect()`:

- `setsockopt(SO_KEEPALIVE, 1)`.
- Windows-specific `WSAIoctl(SIO_KEEPALIVE_VALS)` with idle = 30 s,
  interval = 10 s. Windows infers the probe count.
- Failures from either call are non-fatal: log via `s->debug_log` if open and
  continue. The libssh2-layer detector is sufficient on its own.

**App-level liveness from libssh2 recv activity** — instead of tracking
*channel* bytes (which idle out), track *socket* bytes that libssh2 reads:

- Install a recv hook via
  `libssh2_session_callback_set(LIBSSH2_CALLBACK_RECV, our_recv)`.
- Our wrapper does the default `recv()` behaviour, increments
  `s->bytes_read_total` on positive returns, and forwards the return value
  untouched (including `-EAGAIN`).
- A WM_TIMER tick reads `bytes_read_total`; if it changed, bump
  `last_socket_data_tick`. If `now - last_socket_data_tick > 90 000`, declare
  the connection dead and tear it down with `[Connection timed out]`.

This means **libssh2 keepalive replies (which libssh2 consumes silently) now
count as liveness signals**, eliminating the false-positive on idle sessions.

### 2. User-idle disconnect

A new per-`Session` field `last_user_input_tick` (DWORD,
`GetTickCount`-based). Bumped at four points:

1. `WM_KEYDOWN` / `WM_CHAR` handlers in `window.c`, at the existing call
   sites that forward the keystroke to `ssh_channel_write` on the active
   session.
2. `WM_MOUSEWHEEL` on the main window, when there is an active session.
3. `on_tab_activate` callback ([window.c:215](../../../src/ui/window.c#L215))
   — bump the newly-active session.
4. AI chat input, via a new exported helper `session_mark_user_active(void)`
   from `window.c` that `ai_chat.c` calls when its input edit receives a
   keystroke. (Keeps `Session*` out of the AI module.)

Each WM_TIMER tick checks
`ssh_idle_should_timeout(now, last_user_input_tick, timeout_mins)` and tears
the session down with banner
`[Disconnected after N min idle]\r\n` (with `N` = configured value).

### 3. Settings UI

- New field `ssh_user_idle_timeout_mins` (int) in `Settings`.
- New "SSH" section in the Settings dialog with one numeric edit
  (`IDC_SSH_IDLE_EDIT`) labelled `User Idle Timeout (mins, 0 = never):`.
- Tooltips: refactor today's one-off `TOOLTIPS_CLASS` block at
  [settings.c:389-409](../../../src/ui/settings.c#L389-L409) into a small
  helper `add_tooltip(HWND tooltip_host, HWND tool, const char *text)`.
  Call it once per field after each control is created. One shared tooltip
  window per dialog (the existing `nd->hTooltip`).

## File-Level Changes

### `src/config/config.h`

Extend `Settings`:

```c
int ssh_user_idle_timeout_mins;  /* 0 = never; default 0 */
```

### `src/config/loader.c`

- `config_default_settings()` → `s->ssh_user_idle_timeout_mins = 0;`
- `settings_validate()` → clamp to `[0, 10080]` (0 .. 7 days).
- `config_load()` → read `ssh_user_idle_timeout_mins` (default to existing
  field value, which is 0 from `config_default_settings`).
- `config_save()` → write the key.

### `src/term/ssh_session.{h,c}`

- New private helper `set_tcp_keepalive(socket_t)` — `SO_KEEPALIVE` +
  `SIO_KEEPALIVE_VALS`. Called from `ssh_session_connect` after `connect()`
  succeeds. Quiet on failure.
- New public field on `SshSession`:
  `volatile uint64_t bytes_read_total;` (read by window.c each tick).
- New private recv callback installed via
  `libssh2_session_callback_set(LIBSSH2_CALLBACK_RECV, …)` before the
  handshake, that wraps `recv()` and bumps `bytes_read_total`.
- After handshake completes, sanity-check `bytes_read_total > 0` (KEX
  exchanged bytes); log a warning to `debug_log` on failure but continue.

### `src/ui/window.c`

- Add per-`Session` fields:
  `DWORD last_socket_data_tick;`
  `DWORD last_user_input_tick;`
  `uint64_t prev_bytes_read;`
- `WM_CONN_DONE` handler: initialise both ticks to `GetTickCount()` and
  `prev_bytes_read = 0` (mirrors existing init for `last_data_tick`).
- WM_TIMER poll loop ([window.c:1960-1996](../../../src/ui/window.c#L1960-L1996)):
  - Read `s->ssh->bytes_read_total`. If it changed, bump
    `last_socket_data_tick` and update `prev_bytes_read`.
  - Keep the manual `libssh2_keepalive_send` pump (~1 Hz) — libssh2 needs CPU
    to send keepalives. Only the *liveness signal* shifts to the recv side.
  - If `ssh_network_should_timeout(now, last_socket_data_tick, 90 000)` and
    `s->channel != NULL`: teardown with `[Connection timed out]`.
  - If
    `ssh_idle_should_timeout(now, last_user_input_tick,
    g_config->settings.ssh_user_idle_timeout_mins)` and
    `s->channel != NULL`: teardown with `[Disconnected after N min idle]`
    (banner formatted with `snprintf` from the configured value).
- Delete the static `SSH_IDLE_TIMEOUT_MS` and the now-unused
  `last_data_tick` field.
- `WM_KEYDOWN` / `WM_CHAR`: at each existing call to `ssh_channel_write` on
  the active session, also `g_active_session->last_user_input_tick =
  GetTickCount();`.
- `WM_MOUSEWHEEL`: bump `last_user_input_tick` if there is an active
  session.
- `on_tab_activate`: bump `last_user_input_tick` on the newly-active
  session.
- New exported function `void session_mark_user_active(void)` that bumps
  the active session's tick. Called from `ai_chat.c` on AI-input keystrokes.

### `src/ui/ai_chat.c`

- Call `session_mark_user_active()` from the AI-input edit's keystroke
  handler. (Single-line addition.)

### `src/ui/settings.c`

- New "SSH" section between the terminal section and the AI section:
  - Section heading "SSH"
  - Label "User Idle Timeout (mins, 0 = never):"
  - Numeric edit `IDC_SSH_IDLE_EDIT`
- Wire load (`SetWindowTextA`) and save (`GetWindowTextA` → `strtol` →
  range-check → on parse failure or out-of-range, retain previous value).
- Refactor existing tooltip code into:

  ```c
  static void add_tooltip(HWND tooltip_host, HWND tool,
                          const char *text);
  ```

- Add a tooltip-text table (static array of `{control_id, text}`) covering:
  Font, AI Font, Font Size, Scrollback Lines, Paste Delay, Colour Scheme,
  Foreground Colour, Background Colour, Log Directory, Log Name Format
  (existing strftime help retained), Debug Terminal, AI API Key, AI
  Provider, AI Custom URL, AI Custom Model, AI System Notes, AI Search
  Provider, AI Max Search Results, AI Web Fetch Enabled, **SSH User Idle
  Timeout**.
- Call `add_tooltip` once per field after the control exists.
- `TTM_SETMAXTIPWIDTH` → 300 on the shared dialog tooltip.

### `src/core/ssh_timeout.{h,c}` (new, testable)

```c
#include <stdint.h>
#include <stdbool.h>

bool ssh_network_should_timeout(uint32_t now,
                                uint32_t last_socket_tick,
                                uint32_t threshold_ms);

bool ssh_idle_should_timeout(uint32_t now,
                             uint32_t last_input_tick,
                             int timeout_mins);
```

Pure functions. `ssh_idle_should_timeout` returns `false` if
`timeout_mins <= 0`. Both use unsigned subtraction so 32-bit
`GetTickCount` wraparound is handled correctly.

## Data Flow

### Connect path

```
ssh_session_connect()
  → connect() succeeds
  → set_tcp_keepalive(sock)
  → libssh2_session_callback_set(LIBSSH2_CALLBACK_RECV, our_recv)
  → libssh2_session_handshake()
  → libssh2_keepalive_config(s, want_reply=1, 30)
WM_CONN_DONE
  → s->last_socket_data_tick = GetTickCount()
  → s->last_user_input_tick  = GetTickCount()
  → s->prev_bytes_read       = 0
```

### Steady state (WM_TIMER, ~25 ms cadence)

```
curr_bytes = s->ssh->bytes_read_total
if curr_bytes != s->prev_bytes_read:
    s->last_socket_data_tick = now
    s->prev_bytes_read = curr_bytes

if now - s->last_keepalive_tick >= 1000:
    libssh2_keepalive_send(s->ssh->session, &next_secs)
    s->last_keepalive_tick = now

if s->channel && ssh_network_should_timeout(now,
                                            s->last_socket_data_tick,
                                            90000):
    teardown(s, "\r\n[Connection timed out]\r\n")

if s->channel && ssh_idle_should_timeout(
                     now,
                     s->last_user_input_tick,
                     g_config->settings.ssh_user_idle_timeout_mins):
    teardown(s, "\r\n[Disconnected after N min idle]\r\n")
```

### User-activity bump

```
WM_KEYDOWN / WM_CHAR  (forwarded to channel)
WM_MOUSEWHEEL         (over terminal)
on_tab_activate       (newly-active session)
ai_chat input keystroke (via session_mark_user_active)
    → g_active_session->last_user_input_tick = GetTickCount()
```

## Error Handling & Edge Cases

| Scenario | Handling |
|---|---|
| `setsockopt`/`WSAIoctl` keepalive setup fails | Log to `debug_log`, continue. App-level rail compensates. |
| recv callback unsupported by libssh2 build | `bytes_read_total` stays 0; network-failure rail can't fire. Mitigated by OS TCP keepalive (independent path). Post-handshake assertion logs a warning. |
| `GetTickCount` 32-bit wraparound (~49.7 days) | Unsigned subtraction wraps correctly. Covered by tests. |
| Race: idle + network failure + EOF in same tick | Existing `s->channel != NULL` guard plus `s->channel = NULL` after teardown serialises to one path. |
| `ssh_user_idle_timeout_mins == 0` (default) | `ssh_idle_should_timeout` returns false. Idle rail disabled. |
| Negative value loaded from corrupt config | `settings_validate` clamps to 0. Defensive guard in `ssh_idle_should_timeout` also returns false. |
| Settings dialog: invalid number entry | On OK, `strtol` parse + range check; on failure, retain previous value (do **not** silently coerce to 0, which would secretly disable the timer). |
| Tooltip overflow on long descriptions | `TTM_SETMAXTIPWIDTH` = 300 forces wrapping. |
| Reconnect after timeout | Unchanged. Both new teardown paths leave `s` intact with `s->channel == NULL`, identical to existing EOF path. |

## Testing

`src/core/` is the testable layer per `nutshell/CLAUDE.md`. Win32 / UI code
gets manual verification.

### Unit tests — `tests/test_ssh_timeout.c` (new, registered in `runner.c`)

| # | Case | Expectation |
|---|---|---|
| 1 | network: just connected (last == now) | false |
| 2 | network: 89 s elapsed, threshold 90 000 | false |
| 3 | network: 91 s elapsed, threshold 90 000 | true |
| 4 | network: tick wraparound (last = 0xFFFFFF00, now = 0x00001000) → ~5 s elapsed | false |
| 5 | network: wraparound past threshold | true |
| 6 | idle: timeout_mins == 0 | false (disabled) |
| 7 | idle: timeout_mins < 0 | false (defensive) |
| 8 | idle: 179 min elapsed, timeout 180 | false |
| 9 | idle: 181 min elapsed, timeout 180 | true |
| 10 | idle: timeout_mins == 1, 30 s elapsed | false |
| 11 | idle: timeout_mins == 1, 70 s elapsed | true |

### Settings tests — `tests/test_settings.c` (extend or add)

| # | Case | Expectation |
|---|---|---|
| 12 | `settings_validate` clamps `-5 → 0` | pass |
| 13 | clamps `99999 → 10080` | pass |
| 14 | leaves `180` unchanged | pass |
| 15 | leaves `0` unchanged (disable is valid) | pass |
| 16 | `config_default_settings` sets it to `0` | pass |

### Loader tests — `tests/test_loader.c` (extend)

| # | Case | Expectation |
|---|---|---|
| 17 | round-trip: save with `240`, reload, assert `== 240` | pass |
| 18 | load legacy config with no key → field defaults to `0` | pass |

### Manual verification (record in commit message)

- Open SSH session, leave untouched ≥ 5 min — stays alive.
- `iptables -j DROP` the SSH server's IP — drops within ~90 s with
  `[Connection timed out]`.
- Set `ssh_user_idle_timeout_mins = 1`, type one key, wait 70 s — drops with
  `[Disconnected after 1 min idle]`.
- Set `ssh_user_idle_timeout_mins = 1`, type a key every 30 s for 5 min —
  stays connected.
- Repeat last test using only mouse-wheel scroll / tab switch / AI chat
  typing as the bump source — stays connected.
- Set `ssh_user_idle_timeout_mins = 0`, leave for hours — never drops on
  idle.
- Hover every Settings field — tooltip appears with sensible text.

## Out of Scope

- Per-profile idle-timeout override (Q4 chose global only). Could be added
  later as an optional `Profile` field without breaking existing configs.
- Warning dialog before idle disconnect (Q3 chose silent banner).
- Counting mouse movement / window focus / paint as user activity (Q1).
- Configuring the network-failure threshold (90 s, libssh2 keepalive
  interval 30 s, TCP keepalive idle 30 s / interval 10 s) — all hardcoded.
- Configuring the libssh2 keepalive interval (stays at 30 s).
