# SSH Keepalives, User-Idle Timeout, and Settings Tooltips Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the false-positive 90 s SSH idle disconnect by tracking libssh2 *socket* recv activity (not channel data) plus OS TCP keepalives, and add a separate user-idle disconnect (default 0 = never) configurable from a Settings dialog where every field gains a hover tooltip.

**Architecture:**
- Pure deadline-math lives in `src/core/ssh_timeout.{h,c}` (testable on native Linux).
- Liveness signal moves from "channel byte arrived" to "libssh2 recv() returned bytes," instrumented via `libssh2_session_callback_set(LIBSSH2_CALLBACK_RECV, …)` plus OS-level `SO_KEEPALIVE` / `SIO_KEEPALIVE_VALS` as a backstop.
- User-idle tick is bumped at four sites (keystroke-to-channel, mouse-wheel, tab activate, AI input keystroke) and checked once per WM_TIMER cycle.
- Settings dialog gets a refactored `add_tooltip` helper called once per existing and new control.

**Tech Stack:** C11, MinGW (`x86_64-w64-mingw32-gcc`), Win32, libssh2, custom test framework (`tests/test_framework.h`). Native `gcc` for tests.

---

## Spec Reference

Spec: [docs/superpowers/specs/2026-04-27-ssh-keepalives-and-idle-timeout-design.md](../specs/2026-04-27-ssh-keepalives-and-idle-timeout-design.md)

Read it once before starting. Constants used in this plan:

| Constant | Value | Where |
|---|---|---|
| `NETWORK_FAILURE_TIMEOUT_MS` | `90000` | `src/core/ssh_timeout.h` |
| TCP keepalive idle | 30 s | hardcoded in `set_tcp_keepalive` |
| TCP keepalive interval | 10 s | hardcoded in `set_tcp_keepalive` |
| libssh2 keepalive interval | 30 s | already in `ssh_session.c:156` |
| Manual `libssh2_keepalive_send` cadence | ~1 Hz | already in `window.c:1970` |
| Default `ssh_user_idle_timeout_mins` | `0` (never) | `loader.c` |
| Validation clamp | `[0, 10080]` (7 days) | `loader.c::settings_validate` |

---

## Build & test commands

Per `nutshell/CLAUDE.md`:

- **Tests (native Linux):** `cd /home/thomas/nutshell && make test` (builds `build/test_runner` with `gcc`, then runs it).
- **Windows release:** `cd /home/thomas/nutshell && make clean && make release` (cross-compiles with `x86_64-w64-mingw32-gcc`, then UPX).
- **Version bump is MANDATORY** before every Windows build. Update both:
  1. `src/ui/resource.h` — `APP_VERSION` string and `APP_VERSION_BINARY` macro.
  2. `README.md` — `**Version**:` line.

Both compilers must accept the code with `-Werror -Wpedantic -Wshadow -Wconversion -Wformat=2`.

---

## Task list overview

| # | Task | Touches |
|---|---|---|
| 1 | Add new field to `Settings` and bump default | `src/config/config.h`, `src/config/loader.c` |
| 2 | Create pure timeout helpers in `src/core/` | `src/core/ssh_timeout.{h,c}` (new) |
| 3 | Unit-test the pure helpers | `tests/test_ssh_timeout.c` (new), `tests/runner.c` |
| 4 | Test new field defaults, validation, round-trip | `tests/test_config.c`, `tests/runner.c` |
| 5 | Add `bytes_read_total` and recv callback to `SshSession` | `src/term/ssh_session.{h,c}` |
| 6 | Add `set_tcp_keepalive` and call it from connect | `src/term/ssh_session.c` |
| 7 | Add new tick fields to `Session` and replace WM_TIMER timeout logic | `src/ui/window.c` |
| 8 | Bump idle tick on keystroke / mouse wheel / tab activate | `src/ui/window.c` |
| 9 | Export `session_mark_user_active`; bump on AI input | `src/ui/window.c`, `src/ui/ai_chat.c`, header (`src/ui/ui.h` or new) |
| 10 | Add SSH section to Settings dialog + tooltip helper + tooltip every field | `src/ui/settings.c` |
| 11 | Version bump, full build, manual verification | `src/ui/resource.h`, `README.md` |

---

## Task 1: Add `ssh_user_idle_timeout_mins` to `Settings`

**Files:**
- Modify: `src/config/config.h:17-40`
- Modify: `src/config/loader.c:91-107` (validation), `:109-130` (defaults), `:194-280` (load), `:348-415` (save)

- [ ] **Step 1: Add field to the Settings struct**

In [src/config/config.h:17-40](../../src/config/config.h#L17-L40), append the new field at the end of the struct (just before the closing `} Settings;`):

```c
    int  ai_web_fetch_enabled;       /* 0 = disabled (default), 1 = enabled */
    int  ssh_user_idle_timeout_mins; /* 0 = never; default 0 */
} Settings;
```

- [ ] **Step 2: Set the default in `config_default_settings`**

In [src/config/loader.c:109-130](../../src/config/loader.c#L109-L130), append at the end of the function (after `s->ai_web_fetch_enabled = 0;`):

```c
    s->ssh_user_idle_timeout_mins = 0;
```

- [ ] **Step 3: Add validation clamp**

In [src/config/loader.c:91-107](../../src/config/loader.c#L91-L107), at the end of `settings_validate` (after `ai_max_search_results` clamp):

```c
    if (s->ssh_user_idle_timeout_mins < 0)     s->ssh_user_idle_timeout_mins = 0;
    if (s->ssh_user_idle_timeout_mins > 10080) s->ssh_user_idle_timeout_mins = 10080;
```

- [ ] **Step 4: Read the field in `config_load`**

Find the load block where `paste_delay_ms` is read in `loader.c` (~line 208). Add a parallel block at the end of the Settings load section, just before `settings_validate(s);` is called:

```c
        s->ssh_user_idle_timeout_mins =
            (int)json_obj_num(jset, "ssh_user_idle_timeout_mins",
                              (double)s->ssh_user_idle_timeout_mins);
```

- [ ] **Step 5: Write the field in `config_save`**

In `loader.c` around line 413, change the trailing `ai_web_fetch_enabled` line so the JSON object continues, and add the new key as the new last entry:

Before:
```c
    fprintf(f, "    \"ai_web_fetch_enabled\": %s\n",
            s->ai_web_fetch_enabled ? "true" : "false");
```

After:
```c
    fprintf(f, "    \"ai_web_fetch_enabled\": %s,\n",
            s->ai_web_fetch_enabled ? "true" : "false");
    fprintf(f, "    \"ssh_user_idle_timeout_mins\": %d\n",
            s->ssh_user_idle_timeout_mins);
```

(Trailing comma added to the previous line; the new line has no comma because it's now the last key in the settings object.)

- [ ] **Step 6: Verify it still compiles for tests**

Run: `cd /home/thomas/nutshell && make test`
Expected: build succeeds, all existing tests still pass. (We'll add new tests in tasks 3 and 4.)

- [ ] **Step 7: Commit**

```bash
cd /home/thomas/nutshell
git add src/config/config.h src/config/loader.c
git commit -m "$(cat <<'EOF'
feat(config): add ssh_user_idle_timeout_mins setting

New Settings field defaulting to 0 (never), clamped to [0, 10080] in
settings_validate, round-tripped through config_load/config_save.
Plumbing only — no UI or runtime use yet.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Create pure timeout helpers

**Files:**
- Create: `src/core/ssh_timeout.h`
- Create: `src/core/ssh_timeout.c`

- [ ] **Step 1: Create the header**

`src/core/ssh_timeout.h`:

```c
#ifndef NUTSHELL_SSH_TIMEOUT_H
#define NUTSHELL_SSH_TIMEOUT_H

#include <stdbool.h>
#include <stdint.h>

/* Network-failure rail: declares the link dead when no socket bytes have
 * arrived for `threshold_ms`.  Both timestamps are 32-bit tick counts
 * (e.g. GetTickCount); subtraction wraps modulo 2^32, which is the
 * intended behaviour. */
bool ssh_network_should_timeout(uint32_t now,
                                uint32_t last_socket_tick,
                                uint32_t threshold_ms);

/* User-idle rail: declares the session idle when the user has not
 * interacted for `timeout_mins` minutes.  Returns false when
 * `timeout_mins <= 0` (disabled). */
bool ssh_idle_should_timeout(uint32_t now,
                             uint32_t last_input_tick,
                             int timeout_mins);

#define NETWORK_FAILURE_TIMEOUT_MS ((uint32_t)90000u)

#endif
```

- [ ] **Step 2: Create the implementation**

`src/core/ssh_timeout.c`:

```c
#include "ssh_timeout.h"

bool ssh_network_should_timeout(uint32_t now,
                                uint32_t last_socket_tick,
                                uint32_t threshold_ms)
{
    /* Unsigned subtraction wraps correctly across a 32-bit tick rollover. */
    return (uint32_t)(now - last_socket_tick) > threshold_ms;
}

bool ssh_idle_should_timeout(uint32_t now,
                             uint32_t last_input_tick,
                             int timeout_mins)
{
    if (timeout_mins <= 0) return false;
    /* Cast to uint64_t in the multiplication so 35791-minute (~24-day)
     * thresholds don't overflow a 32-bit ms value.  Compare in 32-bit
     * tick space using unsigned wrap. */
    uint64_t threshold_ms = (uint64_t)timeout_mins * 60000ull;
    if (threshold_ms > 0xFFFFFFFFull) threshold_ms = 0xFFFFFFFFull;
    return (uint32_t)(now - last_input_tick) > (uint32_t)threshold_ms;
}
```

- [ ] **Step 3: Verify it builds**

Run: `cd /home/thomas/nutshell && make test`
Expected: builds (the new file is auto-picked up by `SRC_DIRS = src src/core …` in the Makefile). All existing tests still pass.

- [ ] **Step 4: Commit**

```bash
cd /home/thomas/nutshell
git add src/core/ssh_timeout.h src/core/ssh_timeout.c
git commit -m "$(cat <<'EOF'
feat(core): pure ssh_timeout helpers

Two pure functions for the SSH timeout rails:
- ssh_network_should_timeout: socket-level liveness check
- ssh_idle_should_timeout: user-idle check with disable sentinel (0)

Both handle 32-bit GetTickCount wraparound via unsigned subtraction.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Unit-test the pure helpers

**Files:**
- Create: `tests/test_ssh_timeout.c`
- Modify: `tests/runner.c`

- [ ] **Step 1: Write the failing tests**

`tests/test_ssh_timeout.c`:

```c
#include "test_framework.h"
#include "ssh_timeout.h"

/* ---- Network-failure rail ---- */

int test_ssh_network_just_connected(void)
{
    TEST_BEGIN();
    /* now == last → 0 ms elapsed, well under threshold */
    ASSERT_FALSE(ssh_network_should_timeout(1000u, 1000u, 90000u));
    TEST_END();
}

int test_ssh_network_under_threshold(void)
{
    TEST_BEGIN();
    /* 89 s elapsed, 90 s threshold */
    ASSERT_FALSE(ssh_network_should_timeout(89000u, 0u, 90000u));
    TEST_END();
}

int test_ssh_network_over_threshold(void)
{
    TEST_BEGIN();
    /* 91 s elapsed, 90 s threshold */
    ASSERT_TRUE(ssh_network_should_timeout(91000u, 0u, 90000u));
    TEST_END();
}

int test_ssh_network_wraparound_under(void)
{
    TEST_BEGIN();
    /* last just before rollover, now just after → ~5 s elapsed */
    uint32_t last = 0xFFFFFF00u;
    uint32_t now  = 0x00001000u; /* (uint32_t)(now - last) == 4352 ms */
    ASSERT_FALSE(ssh_network_should_timeout(now, last, 90000u));
    TEST_END();
}

int test_ssh_network_wraparound_over(void)
{
    TEST_BEGIN();
    /* last well before rollover, now well after → ~120 s elapsed */
    uint32_t last = 0xFFFE0000u;
    uint32_t now  = 0x00020000u; /* delta ≈ 0x00040000 = 262 144 ms */
    ASSERT_TRUE(ssh_network_should_timeout(now, last, 90000u));
    TEST_END();
}

/* ---- User-idle rail ---- */

int test_ssh_idle_disabled_zero(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(ssh_idle_should_timeout(0xFFFFFFFFu, 0u, 0));
    TEST_END();
}

int test_ssh_idle_disabled_negative(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(ssh_idle_should_timeout(0xFFFFFFFFu, 0u, -5));
    TEST_END();
}

int test_ssh_idle_under_threshold(void)
{
    TEST_BEGIN();
    /* 179 minutes elapsed, threshold 180 */
    uint32_t now  = 179u * 60u * 1000u;
    uint32_t last = 0u;
    ASSERT_FALSE(ssh_idle_should_timeout(now, last, 180));
    TEST_END();
}

int test_ssh_idle_over_threshold(void)
{
    TEST_BEGIN();
    /* 181 minutes elapsed, threshold 180 */
    uint32_t now  = 181u * 60u * 1000u;
    uint32_t last = 0u;
    ASSERT_TRUE(ssh_idle_should_timeout(now, last, 180));
    TEST_END();
}

int test_ssh_idle_one_minute_under(void)
{
    TEST_BEGIN();
    /* 30 s elapsed, threshold 1 minute */
    ASSERT_FALSE(ssh_idle_should_timeout(30000u, 0u, 1));
    TEST_END();
}

int test_ssh_idle_one_minute_over(void)
{
    TEST_BEGIN();
    /* 70 s elapsed, threshold 1 minute */
    ASSERT_TRUE(ssh_idle_should_timeout(70000u, 0u, 1));
    TEST_END();
}
```

- [ ] **Step 2: Register the tests in `tests/runner.c`**

Add forward declarations alongside the other `int test_…(void);` lines (e.g. just before the `/* test_config.c */` block at line 110):

```c
/* test_ssh_timeout.c */
int test_ssh_network_just_connected(void);
int test_ssh_network_under_threshold(void);
int test_ssh_network_over_threshold(void);
int test_ssh_network_wraparound_under(void);
int test_ssh_network_wraparound_over(void);
int test_ssh_idle_disabled_zero(void);
int test_ssh_idle_disabled_negative(void);
int test_ssh_idle_under_threshold(void);
int test_ssh_idle_over_threshold(void);
int test_ssh_idle_one_minute_under(void);
int test_ssh_idle_one_minute_over(void);
```

Add invocations in `main()` immediately before the `/* Config */` invocation block (~line 1638):

```c
    /* SSH timeout helpers */
    failed += test_ssh_network_just_connected();
    failed += test_ssh_network_under_threshold();
    failed += test_ssh_network_over_threshold();
    failed += test_ssh_network_wraparound_under();
    failed += test_ssh_network_wraparound_over();
    failed += test_ssh_idle_disabled_zero();
    failed += test_ssh_idle_disabled_negative();
    failed += test_ssh_idle_under_threshold();
    failed += test_ssh_idle_over_threshold();
    failed += test_ssh_idle_one_minute_under();
    failed += test_ssh_idle_one_minute_over();
```

- [ ] **Step 3: Run the tests**

Run: `cd /home/thomas/nutshell && make test`
Expected: all 11 new tests pass. Output shows `[PASS] test_ssh_network_just_connected` etc.

- [ ] **Step 4: Commit**

```bash
cd /home/thomas/nutshell
git add tests/test_ssh_timeout.c tests/runner.c
git commit -m "$(cat <<'EOF'
test(core): unit tests for ssh_timeout helpers

11 cases covering both rails: under/over threshold, 32-bit tick
wraparound, disable sentinel (0 / negative), and 1-minute boundary.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Test the new Settings field

**Files:**
- Modify: `tests/test_config.c`
- Modify: `tests/runner.c`

- [ ] **Step 1: Add default + validation tests**

Append to `tests/test_config.c`:

```c
/* ============================================================
 * SSH user-idle timeout setting
 * ============================================================ */

int test_config_default_ssh_user_idle(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 0);
    TEST_END();
}

int test_config_validate_ssh_user_idle_clamp(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);

    s.ssh_user_idle_timeout_mins = -5;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 0);

    s.ssh_user_idle_timeout_mins = 99999;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 10080);

    s.ssh_user_idle_timeout_mins = 180;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 180);

    s.ssh_user_idle_timeout_mins = 0;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 0);
    TEST_END();
}

int test_config_roundtrip_ssh_user_idle(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);
    orig->settings.ssh_user_idle_timeout_mins = 240;

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(loaded->settings.ssh_user_idle_timeout_mins, 240);

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}
```

- [ ] **Step 2: Register the tests in `tests/runner.c`**

Add forward declarations near the other `test_config_*` declarations (after `test_config_validate_empty_ai_font` at ~line 142):

```c
int test_config_default_ssh_user_idle(void);
int test_config_validate_ssh_user_idle_clamp(void);
int test_config_roundtrip_ssh_user_idle(void);
```

Add invocations in `main()` after the existing `test_config_*` block (after `test_config_validate_empty_ai_font();` at ~line 1670):

```c
    failed += test_config_default_ssh_user_idle();
    failed += test_config_validate_ssh_user_idle_clamp();
    failed += test_config_roundtrip_ssh_user_idle();
```

- [ ] **Step 3: Run the tests**

Run: `cd /home/thomas/nutshell && make test`
Expected: all three new tests pass.

- [ ] **Step 4: Commit**

```bash
cd /home/thomas/nutshell
git add tests/test_config.c tests/runner.c
git commit -m "$(cat <<'EOF'
test(config): default, validation, round-trip for ssh_user_idle_timeout_mins

Verifies default of 0, clamping to [0, 10080], and JSON round-trip.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Add `bytes_read_total` and recv callback to `SshSession`

**Files:**
- Modify: `src/term/ssh_session.h`
- Modify: `src/term/ssh_session.c`

- [ ] **Step 1: Add the counter field to `SshSession`**

In [src/term/ssh_session.h](../../src/term/ssh_session.h), add `<stdint.h>` and the counter:

```c
#include <stdbool.h>
#include <stdint.h>
```

```c
typedef struct {
    LIBSSH2_SESSION *session;
    SOCKET socket;
    bool connected;
    char last_error[256];
    char cached_passphrase[256]; /* zeroed on free; never written to disk */
    uint64_t bytes_read_total;   /* incremented by libssh2 RECV callback */
} SshSession;
```

- [ ] **Step 2: Implement the recv callback in `ssh_session.c`**

Just below the includes near the top of `src/term/ssh_session.c`, add:

```c
/* libssh2 RECV callback signature:
 *   ssize_t recv_cb(libssh2_socket_t sock, void *buffer, size_t length,
 *                   int flags, void **abstract);
 *
 * `*abstract` is the per-session opaque pointer set via
 * libssh2_session_abstract().  We store the SshSession* there so we can
 * bump bytes_read_total without a global. */
static ssize_t nutshell_recv_cb(libssh2_socket_t sock, void *buffer,
                                size_t length, int flags, void **abstract)
{
#ifdef _WIN32
    int n = recv(sock, (char *)buffer, (int)length, flags);
#else
    ssize_t n = recv(sock, buffer, length, flags);
#endif
    if (n > 0 && abstract && *abstract) {
        SshSession *s = (SshSession *)*abstract;
        s->bytes_read_total += (uint64_t)n;
    }
#ifdef _WIN32
    /* Map Winsock semantics to what libssh2 expects (negative on error,
     * with errno-equivalent set via WSAGetLastError → libssh2 already
     * handles this internally; we just forward the value). */
    return (ssize_t)n;
#else
    return n;
#endif
}
```

- [ ] **Step 3: Wire the callback and the abstract pointer in `ssh_connect`**

In `ssh_session.c`, find `ssh_connect()` (it sets up the session and calls `libssh2_session_handshake`). Before the handshake call (~line 145), install the callback and the abstract pointer:

```c
    /* Install our recv hook so we can track socket-level liveness.
     * Must run before handshake so KEX bytes are counted. */
    void **abstract = libssh2_session_abstract(s->session);
    if (abstract) *abstract = s;
    libssh2_session_callback_set(s->session,
                                 LIBSSH2_CALLBACK_RECV,
                                 (void *)nutshell_recv_cb);

    if (libssh2_session_handshake(s->session, (libssh2_socket_t)s->socket)) {
```

- [ ] **Step 4: Initialise the counter in `ssh_session_new`**

In `ssh_session_new()`, after allocating, ensure the counter starts at 0. If `ssh_session_new` uses `calloc` it's already zero — verify by reading the function. If it uses `malloc + manual init`, add:

```c
    s->bytes_read_total = 0;
```

- [ ] **Step 5: Build & test**

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build with no warnings under `-Werror`. (Native test target also still builds: `make test`.)

If the native test build fails because `libssh2.h` lacks `libssh2_session_abstract` (the local stub), add a stub for it in `src/term/libssh2.h`:

```c
static inline void **libssh2_session_abstract(LIBSSH2_SESSION *s) { (void)s; return NULL; }
```

Re-run `make test`.

- [ ] **Step 6: Commit**

```bash
cd /home/thomas/nutshell
git add src/term/ssh_session.h src/term/ssh_session.c src/term/libssh2.h
git commit -m "$(cat <<'EOF'
feat(term): track libssh2 socket-recv bytes via RECV callback

Adds SshSession.bytes_read_total bumped from a libssh2 RECV callback.
Lets the WM_TIMER timeout logic detect liveness from keepalive replies
that libssh2 consumes silently — the channel-bytes signal misses these
on idle sessions and false-fires the 90s rail.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Add OS-level TCP keepalive

**Files:**
- Modify: `src/term/ssh_session.c`

- [ ] **Step 1: Add the helper**

In `ssh_session.c`, just below `nutshell_recv_cb`:

```c
#ifdef _WIN32
#include <mstcpip.h>  /* tcp_keepalive struct, SIO_KEEPALIVE_VALS */
#endif

/* Enable OS-level TCP keepalive on the socket as a backstop for the
 * libssh2-recv liveness check.  Idle 30s, interval 10s.  Quiet on
 * failure — the libssh2-layer detector compensates. */
static void set_tcp_keepalive(SOCKET sock)
{
#ifdef _WIN32
    BOOL enable = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
               (const char *)&enable, sizeof(enable));

    struct tcp_keepalive ka;
    ka.onoff             = 1;
    ka.keepalivetime     = 30000u;  /* idle before first probe (ms) */
    ka.keepaliveinterval = 10000u;  /* interval between probes (ms) */
    DWORD bytes_returned = 0;
    WSAIoctl(sock, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
             NULL, 0, &bytes_returned, NULL, NULL);
#else
    int enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
    /* Linux tunables are tested in CI but not on the user's runtime
     * target (Windows); keep parity but ignore failures. */
#ifdef TCP_KEEPIDLE
    int idle = 30;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
    int intvl = 10;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#endif
}
```

- [ ] **Step 2: Call it after connect succeeds**

In `ssh_connect()`, immediately after the `connected` socket is assigned to `s->socket = sock;` (~line 143), before the handshake call:

```c
    s->socket = sock;
    set_tcp_keepalive(sock);
```

- [ ] **Step 3: Build & test**

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build, no warnings.

Run: `cd /home/thomas/nutshell && make test`
Expected: all tests still pass.

- [ ] **Step 4: Commit**

```bash
cd /home/thomas/nutshell
git add src/term/ssh_session.c
git commit -m "$(cat <<'EOF'
feat(term): set OS TCP keepalive on the SSH socket

SO_KEEPALIVE + SIO_KEEPALIVE_VALS (idle 30s, interval 10s) on Windows;
parallel sockopts under Linux for parity. Backstop for the libssh2
recv-callback liveness check.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Replace WM_TIMER timeout logic in window.c

**Files:**
- Modify: `src/ui/window.c` (struct around :77-79, static const around :42-45, WM_CONN_DONE around :2074-2079, WM_TIMER block around :1960-1996)

- [ ] **Step 1: Update the `Session` struct**

In [src/ui/window.c:77-79](../../src/ui/window.c#L77-L79), replace:

```c
    DWORD           last_data_tick;       /* GetTickCount() of last byte received */
    DWORD           last_keepalive_tick;  /* GetTickCount() of last keepalive_send() */
    struct Session *next;
```

with:

```c
    DWORD           last_socket_data_tick;  /* GetTickCount() of last libssh2 recv() */
    DWORD           last_keepalive_tick;    /* GetTickCount() of last keepalive_send() */
    DWORD           last_user_input_tick;   /* GetTickCount() of last user activity */
    uint64_t        prev_bytes_read;        /* SshSession.bytes_read_total snapshot */
    struct Session *next;
```

Add `#include <stdint.h>` near the top of `window.c` if not already present (search for existing `#include <stdio.h>` and add nearby).

- [ ] **Step 2: Add the include for `ssh_timeout.h`**

Near the other project includes at the top of `window.c` (e.g. next to `#include "ssh_session.h"`):

```c
#include "ssh_timeout.h"
```

- [ ] **Step 3: Delete the static `SSH_IDLE_TIMEOUT_MS`**

Remove [src/ui/window.c:42-45](../../src/ui/window.c#L42-L45):

```c
/* Read-inactivity timeout: declare connection dead if no bytes
 * received for this long. Driven by libssh2 keepalive replies on
 * a healthy connection. */
static const DWORD SSH_IDLE_TIMEOUT_MS = 90000u;
```

- [ ] **Step 4: Update `WM_CONN_DONE` to initialise the new ticks**

In [src/ui/window.c:2074-2079](../../src/ui/window.c#L2074-L2079), replace:

```c
        case WM_CONN_DONE: {
            Session *s = (Session *)lParam;
            s->conn_state = CONN_IDLE;
            s->last_data_tick      = GetTickCount();
            s->last_keepalive_tick = s->last_data_tick;
            CloseHandle(s->conn_thread);
```

with:

```c
        case WM_CONN_DONE: {
            Session *s = (Session *)lParam;
            s->conn_state = CONN_IDLE;
            DWORD now = GetTickCount();
            s->last_socket_data_tick = now;
            s->last_keepalive_tick   = now;
            s->last_user_input_tick  = now;
            s->prev_bytes_read       = s->ssh ? s->ssh->bytes_read_total : 0;
            CloseHandle(s->conn_thread);
```

- [ ] **Step 5: Replace the WM_TIMER timeout block**

In [src/ui/window.c:1960-1996](../../src/ui/window.c#L1960-L1996), replace the entire block beginning with `DWORD now_tick = GetTickCount();` and ending with the close of the inactivity-deadline `if`. Replace with:

```c
                        DWORD now_tick = GetTickCount();

                        /* Track socket-level liveness: libssh2's RECV
                         * callback bumps s->ssh->bytes_read_total whenever
                         * any byte (including silently-consumed keepalive
                         * replies) arrives.  Any change since last tick
                         * means the link is alive. */
                        if (s->ssh) {
                            uint64_t curr = s->ssh->bytes_read_total;
                            if (curr != s->prev_bytes_read) {
                                s->last_socket_data_tick = now_tick;
                                s->prev_bytes_read = curr;
                            }
                        }

                        /* Drive libssh2 keepalive ~once per second.  The
                         * library handles the 30s send cadence internally;
                         * we just need to give it CPU time. */
                        if (now_tick - s->last_keepalive_tick >= 1000u) {
                            int next_secs = 0;
                            if (s->ssh && s->ssh->session)
                                libssh2_keepalive_send(s->ssh->session,
                                                       &next_secs);
                            s->last_keepalive_tick = now_tick;
                        }

                        /* Network-failure rail: no socket bytes at all
                         * (not even keepalive replies) for the threshold. */
                        if (poll_rc != -2 && s->channel &&
                            ssh_network_should_timeout(now_tick,
                                                       s->last_socket_data_tick,
                                                       NETWORK_FAILURE_TIMEOUT_MS)) {
                            dispbuf_invalidate(&g_renderer.dispbuf);
                            if (g_paste.channel == s->channel)
                                paste_cancel();
                            term_process(s->term,
                                         "\r\n[Connection timed out]\r\n", 26);
                            ssh_channel_free(s->channel);
                            s->channel = NULL;
                            int tidx = tabs_find(g_hwndTabs, s);
                            if (tidx >= 0)
                                tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
                            if (s == g_active_session) hide_ai_panel(hwnd);
                        }

                        /* User-idle rail: configurable, 0 = disabled. */
                        if (poll_rc != -2 && s->channel &&
                            ssh_idle_should_timeout(
                                now_tick,
                                s->last_user_input_tick,
                                g_config->settings.ssh_user_idle_timeout_mins)) {
                            char banner[64];
                            int n = snprintf(banner, sizeof(banner),
                                             "\r\n[Disconnected after %d min idle]\r\n",
                                             g_config->settings.ssh_user_idle_timeout_mins);
                            if (n < 0) n = 0;
                            if (n > (int)sizeof(banner)) n = (int)sizeof(banner) - 1;
                            dispbuf_invalidate(&g_renderer.dispbuf);
                            if (g_paste.channel == s->channel)
                                paste_cancel();
                            term_process(s->term, banner, (size_t)n);
                            ssh_channel_free(s->channel);
                            s->channel = NULL;
                            int tidx = tabs_find(g_hwndTabs, s);
                            if (tidx >= 0)
                                tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
                            if (s == g_active_session) hide_ai_panel(hwnd);
                        }
```

(Keep the `if (poll_rc == -2)` EOF block immediately following — unchanged.)

Note: this **removes** the old logic that bumped `last_data_tick` on `poll_rc > 0`. The `poll_rc > 0` branch still exists for `update_scrollbar(hwnd)`; keep that line intact at its original position.

- [ ] **Step 6: Build (cross-compile)**

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build with no warnings.

If a `-Wconversion` warning fires on the `snprintf(..., %d, ...)` line (because `g_config->settings.ssh_user_idle_timeout_mins` is `int`), no cast needed — `%d` matches `int`.

- [ ] **Step 7: Commit**

```bash
cd /home/thomas/nutshell
git add src/ui/window.c
git commit -m "$(cat <<'EOF'
fix(ui): replace channel-byte timeout with socket-recv liveness

Idle SSH sessions no longer false-disconnect at 90s. The new logic:
- Network-failure rail (90s) tracks SshSession.bytes_read_total which
  the libssh2 RECV callback bumps on every recv, including silently-
  consumed keepalive replies — so an idle but healthy connection
  registers as alive.
- New user-idle rail uses ssh_user_idle_timeout_mins (0 = never;
  default 0) and tears the session down with a
  "[Disconnected after N min idle]" banner.

The bump points for the user-idle tick come in the next commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Bump idle tick on keystroke / mouse wheel / tab activate

**Files:**
- Modify: `src/ui/window.c`

- [ ] **Step 1: Bump on tab activation**

In [src/ui/window.c:213-232](../../src/ui/window.c#L213-L232) (`on_tab_select`), after `g_active_session = (Session *)user_data;`:

```c
static void on_tab_select(int index, void *user_data) {
    (void)index;
    g_active_session = (Session *)user_data;
    if (g_active_session)
        g_active_session->last_user_input_tick = GetTickCount();
    HWND hParent = GetParent(g_hwndTabs);
```

- [ ] **Step 2: Bump on mouse wheel over the terminal**

In [src/ui/window.c:2629-2652](../../src/ui/window.c#L2629-L2652), inside the `WM_MOUSEWHEEL` handler, in the plain-scroll branch (the `if (g_active_session && g_active_session->term)` block), add at the top of the block:

```c
        case WM_MOUSEWHEEL: {
            /* Ctrl+Scroll zooms the font */
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                apply_zoom(hwnd, delta > 0 ? 1 : -1);
                return 0;
            }
            /* Plain scroll: scroll 3 lines per notch */
            if (g_active_session && g_active_session->term) {
                g_active_session->last_user_input_tick = GetTickCount();
                Terminal *t = g_active_session->term;
```

- [ ] **Step 3: Bump on keystrokes forwarded to the channel**

In `WM_KEYDOWN` and `WM_CHAR`, every existing call to `ssh_channel_write(g_active_session->channel, …)` should be preceded by a tick bump. The existing call sites in `window.c` are at lines 1167, 1175, 1180, 1195, 2533, 2616 (per earlier grep).

The cleanest fix is one helper at the top of the file (just below the `static` forwards near line 108):

```c
static void mark_active_user_active(void) {
    if (g_active_session) {
        g_active_session->last_user_input_tick = GetTickCount();
    }
}
```

Then before each of the `ssh_channel_write(g_active_session->channel, …)` calls **inside the WM_KEYDOWN / WM_CHAR handlers** (lines 2533, 2616 are inside these handlers), call `mark_active_user_active();`.

The other `ssh_channel_write` sites (do_paste at 1167–1195, paste_timer_tick at 1072/1097) are paste flows — bumping on those is fine and consistent (paste IS user activity), so call `mark_active_user_active();` once at the entry of `do_paste()` and once at the entry of `paste_timer_tick()` rather than peppering every line.

Locate `do_paste()` (look for `static void do_paste(HWND hwnd)` around line 1114) and add at the top:

```c
static void do_paste(HWND hwnd) {
    if (!g_active_session || !g_active_session->channel) return;
    mark_active_user_active();
    /* ... existing body ... */
```

Locate `paste_timer_tick()` (look for the function definition) and add at its top:

```c
static void paste_timer_tick(void) {
    /* ... if there is a guard for `g_paste.channel`, keep it first */
    mark_active_user_active();
    /* ... existing body ... */
```

For `WM_KEYDOWN` and `WM_CHAR`, find the case labels (around lines 2510 and 2545 per earlier grep) and add at the top of each handler body (before any returns):

```c
        case WM_CHAR: {
            mark_active_user_active();
            /* ... existing body ... */
        }

        case WM_KEYDOWN: {
            mark_active_user_active();
            /* ... existing body ... */
        }
```

(This bumps on every keystroke even ones not forwarded to the channel — harmless: any key typed in the Nutshell window is "the user is using it.")

- [ ] **Step 4: Build (cross-compile)**

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build, no warnings.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell
git add src/ui/window.c
git commit -m "$(cat <<'EOF'
feat(ui): bump user-idle tick on key/wheel/tab/paste

Adds mark_active_user_active() helper called from WM_KEYDOWN, WM_CHAR,
WM_MOUSEWHEEL (plain scroll), on_tab_select, do_paste, and
paste_timer_tick — i.e. any user-driven activity in the Nutshell window
that targets the active SSH session resets the idle countdown.

Mouse movement, focus changes, and incoming SSH/AI output do NOT bump.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Export `session_mark_user_active`; bump on AI input

**Files:**
- Modify: `src/ui/window.c`
- Modify: `src/ui/ai_chat.c`
- Modify: `src/ui/ai_chat.h` *(if `session_mark_user_active` is declared there; else create a tiny header)*

- [ ] **Step 1: Export the helper from `window.c`**

In `window.c`, change `mark_active_user_active` from `static` to a non-static function with an external name, and add a forward decl in a header. Easiest path: add it to `src/ui/ai_chat.h` since that's where the AI side will call it.

In `window.c` (the helper added in Task 8 step 3), rename and de-static:

```c
void session_mark_user_active(void) {
    if (g_active_session) {
        g_active_session->last_user_input_tick = GetTickCount();
    }
}
```

Update all internal call sites in `window.c` from `mark_active_user_active();` to `session_mark_user_active();`.

- [ ] **Step 2: Declare it in a header**

Pick the smallest existing header that both `window.c` and `ai_chat.c` include. Check: `grep -n '#include "ai_chat.h"' src/ui/window.c` — if it's included, add the declaration there. Otherwise, declare in `src/ui/ui.h` (search for an existing project-wide UI header).

Add to whichever header is appropriate, in a clearly-marked section:

```c
/* Bump the active session's user-idle tick.  Called from any UI
 * surface (chat input, etc.) that should count as user activity. */
void session_mark_user_active(void);
```

- [ ] **Step 3: Call it from the AI chat input subclass**

In [src/ui/ai_chat.c:1437](../../src/ui/ai_chat.c#L1437) (`InputSubclassProc`), add at the very top of the function, before any other handling:

```c
static LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData)
{
    if (msg == WM_KEYDOWN || msg == WM_CHAR) {
        session_mark_user_active();
    }
    /* ... existing body ... */
```

If `ai_chat.c` doesn't already include the chosen header, add the `#include` near the top.

- [ ] **Step 4: Build (cross-compile)**

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build, no warnings.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell
git add src/ui/window.c src/ui/ai_chat.c src/ui/ai_chat.h src/ui/ui.h
git commit -m "$(cat <<'EOF'
feat(ai): bump SSH user-idle tick on AI chat keystrokes

Exports session_mark_user_active() from window.c and calls it from
the AI chat input subclass on WM_KEYDOWN/WM_CHAR — typing in the AI
panel keeps the linked SSH session alive.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

(Adjust `git add` to drop `ui.h` or `ai_chat.h` if you didn't touch them.)

---

## Task 10: Settings dialog — SSH section + tooltip-every-field

**Files:**
- Modify: `src/ui/settings.c` (control IDs around :19-36, dialog construction around :370-450 and the AI block following it, OK handler near the end)

- [ ] **Step 1: Add the new control ID**

In `src/ui/settings.c` near [line 19-36](../../src/ui/settings.c#L19-L36), add:

```c
#define IDC_SSH_IDLE_EDIT       1030
```

(Keep existing IDs; pick the next free integer — verify nothing else in `resource.h` collides.)

- [ ] **Step 2: Refactor the existing tooltip block into a helper**

Above the dialog's `WM_CREATE` handler (where the tooltip is currently created), add:

```c
/* Add a tooltip to a single child control inside the dialog.
 * Reuses one shared tooltip window per dialog (TTM_ADDTOOL). */
static void add_tooltip(HWND tooltip_host, HWND tool, const char *text)
{
    if (!tooltip_host || !tool || !text) return;
    TOOLINFO ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize   = sizeof(TOOLINFO);
    ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd     = GetParent(tool);
    ti.uId      = (UINT_PTR)tool;
    ti.lpszText = (LPSTR)text;
    SendMessage(tooltip_host, TTM_ADDTOOL, 0, (LPARAM)&ti);
}
```

- [ ] **Step 3: Create the shared tooltip window once, near the start of dialog creation**

Find where `nd->hTooltip` is currently created at [settings.c:390](../../src/ui/settings.c#L390) (deep inside the log-format row). Move that creation block to immediately after the dialog's child controls begin to be created — e.g. right after `nd->cfg = ...` is assigned and the first label is built, but before any control. The window needs to exist before `add_tooltip` is called.

After moving:

```c
        nd->hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | TTS_BALLOON,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        if (nd->hTooltip)
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)300);
```

Delete the original block at lines 389-409 except the `lpszText` content, which moves into the tooltip table (next step).

- [ ] **Step 4: Add the tooltip-text table**

Near the top of `settings.c` (just below the `IDC_*` defines, before the static helpers):

```c
typedef struct { int id; const char *text; } TooltipEntry;

static const TooltipEntry k_tooltips[] = {
    { IDC_FONT_COMBO,
      "Monospaced font used by the terminal display." },
    { IDC_AI_FONT_COMBO,
      "Font used by the AI chat panel (does not affect the terminal)." },
    { IDC_FONTSIZE_COMBO,
      "Terminal font size in points." },
    { IDC_SCROLLBACK_EDIT,
      "Number of lines kept in the scrollback buffer (100 - 50 000)." },
    { IDC_PASTEDELAY_EDIT,
      "Pause in ms between characters when pasting into the terminal "
      "(0 - 5 000). Higher values help slow remote shells keep up." },
    { IDC_SCHEME_COMBO,
      "Predefined colour scheme for the terminal. Foreground and "
      "background overrides apply on top of the scheme." },
    { IDC_LOG_DIR_EDIT,
      "Directory where session logs are written when logging is enabled." },
    { IDC_LOG_FMT_EDIT,
      "%Y  4-digit year (e.g. 2026)\r\n"
      "%m  month (01-12)\r\n"
      "%d  day   (01-31)\r\n"
      "%H  hour  (00-23)\r\n"
      "%M  minute (00-59)\r\n"
      "%S  second (00-59)\r\n"
      "Example: session-%Y%m%d_%H%M%S" },
    { IDC_DEBUG_TERMINAL,
      "Write raw terminal byte stream to a debug log "
      "(useful for debugging escape-sequence handling)." },
    { IDC_AI_KEY_EDIT,
      "API key for the chosen AI provider. Stored encrypted in "
      "nutshell.config." },
    { IDC_AI_PROVIDER_COMBO,
      "AI provider used by the chat panel." },
    { IDC_AI_CUSTOM_URL,
      "Custom AI API endpoint URL. Only used when provider is 'custom'." },
    { IDC_AI_CUSTOM_MODEL,
      "Custom model identifier. Only used when provider is 'custom'." },
    { IDC_AI_SYSTEM_NOTES,
      "Default system instructions for the AI. Profile-specific notes, "
      "if set, take precedence." },
    { IDC_AI_SEARCH_PROVIDER_COMBO,
      "Search backend used by AI tool calls. 'None' disables web search." },
    { IDC_AI_SEARCH_URL_EDIT,
      "Custom search endpoint. Only used when search provider is 'custom'." },
    { IDC_AI_MAX_RESULTS_EDIT,
      "Maximum number of search results returned to the AI per query "
      "(1 - 20)." },
    { IDC_AI_WEB_FETCH_CHECK,
      "Allow the AI to fetch arbitrary URLs as a tool call." },
    { IDC_SSH_IDLE_EDIT,
      "Disconnect SSH sessions after this many minutes of no user "
      "activity. 0 = never disconnect on idle. Keystrokes, mouse-wheel "
      "scrolling, tab switches, and AI chat input all count as activity." },
};
#define NUM_TOOLTIPS ((int)(sizeof(k_tooltips) / sizeof(k_tooltips[0])))
```

If any of the IDs above don't exist in this codebase (e.g. `IDC_AI_SEARCH_PROVIDER_COMBO`, `IDC_AI_SEARCH_URL_EDIT`, `IDC_AI_MAX_RESULTS_EDIT`, `IDC_AI_WEB_FETCH_CHECK`), drop those entries from the table — `grep -n 'IDC_AI_SEARCH\|IDC_AI_MAX\|IDC_AI_WEB' src/ui/settings.c` to verify what exists. Only include entries whose IDs the file already defines, **plus** the new `IDC_SSH_IDLE_EDIT`.

- [ ] **Step 5: Apply the tooltip table after all controls are created**

At the very end of dialog creation (just before the `WM_CREATE` handler returns 0 / falls through), iterate the table:

```c
        if (nd->hTooltip) {
            for (int i = 0; i < NUM_TOOLTIPS; i++) {
                HWND tool = GetDlgItem(hwnd, k_tooltips[i].id);
                if (tool)
                    add_tooltip(nd->hTooltip, tool, k_tooltips[i].text);
            }
        }
```

- [ ] **Step 6: Add the SSH section to the dialog**

Find a sensible insertion point — between the existing terminal section and the AI section. A good anchor is just after the Debug Terminal checkbox (around line 423 `y += rh;`) and before "Row 8: AI API Key" at line 425:

```c
        y += rh;

        /* SSH section heading + user-idle timeout (mins, 0 = never) */
        {
            char buf[32];
            int ssh_h = MulDiv(20, nd->dpi, 96);
            HWND hSshLabel = CreateWindow("STATIC", "SSH",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                lx, y + ssh_h / 6, lw, ssh_h, hwnd, NULL, NULL, NULL);
            (void)hSshLabel;
        }
        y += rh;

        make_label(hwnd, "User Idle Timeout (mins, 0=never):",
                   lx, y, lw, nd->dpi);
        {
            char buf[32];
            (void)snprintf(buf, sizeof(buf), "%d",
                           nd->cfg->settings.ssh_user_idle_timeout_mins);
            make_edit(hwnd, buf, ex, y, ew,
                      (HMENU)IDC_SSH_IDLE_EDIT, nd->dpi);
        }
        y += rh + S(5);  /* extra gap before the AI section */
```

Verify the layout still fits the dialog client height — if the dialog is fixed-size, you may need to bump it. Search for the dialog window creation `CreateWindow("Nutshell_Settings", …)` and find the height constant; bump it by `2 * rh` if needed.

- [ ] **Step 7: Read the value back on OK**

Find the OK handler in `settings.c` (search for `IDOK` or the spot that calls `settings_validate(...)` after pulling values from controls — it parses fields like `paste_delay_ms`). Add a parallel parse for the new edit:

```c
            char buf[32];
            GetDlgItemText(hwnd, IDC_SSH_IDLE_EDIT, buf, sizeof(buf));
            char *endp = NULL;
            long v = strtol(buf, &endp, 10);
            if (endp != buf && *endp == '\0' && v >= 0 && v <= 10080) {
                nd->cfg->settings.ssh_user_idle_timeout_mins = (int)v;
            }
            /* on parse failure or out-of-range: retain previous value */
```

- [ ] **Step 8: Build (cross-compile)**

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build, no warnings.

- [ ] **Step 9: Commit**

```bash
cd /home/thomas/nutshell
git add src/ui/settings.c
git commit -m "$(cat <<'EOF'
feat(ui): SSH section in Settings + hover tooltips on every field

- New SSH section with User Idle Timeout edit (0 = never).
- add_tooltip() helper applied via a static k_tooltips table covering
  every Settings control. The shared tooltip window is created once
  per dialog instead of inline at the log-format row.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Version bump, build, manual verification

**Files:**
- Modify: `src/ui/resource.h`
- Modify: `README.md`

- [ ] **Step 1: Read the current version**

Run: `grep -n 'APP_VERSION' /home/thomas/nutshell/src/ui/resource.h`

Note the current version (string and binary). Increment patch version by one (e.g. `1.0.30` → `1.0.31`, `1,0,30,0` → `1,0,31,0`).

- [ ] **Step 2: Update `src/ui/resource.h`**

Edit both `APP_VERSION` macros to the new patch version. The `nutshell.rc` file picks them up via `#include`.

- [ ] **Step 3: Update `README.md`**

Edit the line beginning `**Version**:` to match the new version.

- [ ] **Step 4: Full clean build**

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build with no warnings; `build/win/nutshell.exe` produced; UPX compresses successfully.

- [ ] **Step 5: Run all tests**

Run: `cd /home/thomas/nutshell && make test`
Expected: every test passes including the new ones from Tasks 3 and 4.

- [ ] **Step 6: Manual verification on Windows**

Run `nutshell.exe` and verify each scenario. Note the result in the commit message.

| # | Scenario | Expected |
|---|---|---|
| 1 | Open SSH session, leave untouched ≥ 5 min | Stays connected (no `[Connection timed out]`) |
| 2 | Drop SSH server's IP via `iptables -j DROP` (or pull cable) | Drops within ~90 s with `[Connection timed out]` banner |
| 3 | Set `User Idle Timeout = 1`, type one key, wait 70 s without input | Drops with `[Disconnected after 1 min idle]` banner |
| 4 | Set `User Idle Timeout = 1`, type a key every 30 s for 5 min | Stays connected |
| 5 | Same as #4 but bumping via mouse-wheel scroll on terminal | Stays connected |
| 6 | Same as #4 but bumping via tab-switch (alt-tab between two sessions) | Stays connected |
| 7 | Same as #4 but bumping via typing in the AI chat input | Stays connected |
| 8 | Set `User Idle Timeout = 0`, leave 30 min untouched | Stays connected (idle disabled) |
| 9 | Hover every Settings control | Tooltip appears with sensible text |
| 10 | Set `User Idle Timeout` to non-numeric or `99999`, click OK | Previous value retained or clamped to 10080 |

- [ ] **Step 7: Commit version bump + verification record**

```bash
cd /home/thomas/nutshell
git add src/ui/resource.h README.md
git commit -m "$(cat <<'EOF'
chore: bump version for SSH idle/timeout feature

Manual verification on Windows:
- 5+ min idle session stays alive (was: died at 90s).
- iptables-DROP causes [Connection timed out] in ~90s.
- User Idle Timeout = 1 + 70s no-input → [Disconnected after 1 min idle].
- Bumps confirmed: keystrokes, mouse-wheel, tab-switch, AI chat input.
- User Idle Timeout = 0 stays connected indefinitely.
- Settings tooltips visible on every field.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review Notes

**Coverage vs. spec:**
- Spec §"Network-failure detection" → Tasks 5, 6, 7.
- Spec §"User-idle disconnect" → Tasks 1, 7, 8, 9.
- Spec §"Settings UI" → Tasks 1, 10.
- Spec §"Testing" rows 1-11 → Task 3.
- Spec §"Testing" rows 12-16 → Task 4 (note: Task 4 includes default=0, clamp, round-trip; rows 14, 15 are absorbed into the clamp test).
- Spec §"Testing" row 17 → Task 4 round-trip test.
- Spec §"Testing" row 18 (legacy config no key → 0) → covered implicitly by `json_obj_num(jset, "ssh_user_idle_timeout_mins", default)` falling back to `s->ssh_user_idle_timeout_mins` which `config_default_settings` set to 0. Add as an extra test case if extra paranoia desired; not on the critical path.
- Spec §"Manual verification" → Task 11 step 6.

**Type & name consistency:** `session_mark_user_active`, `bytes_read_total`, `last_socket_data_tick`, `last_user_input_tick`, `prev_bytes_read`, `nutshell_recv_cb`, `set_tcp_keepalive`, `add_tooltip`, `k_tooltips`, `IDC_SSH_IDLE_EDIT`, `NETWORK_FAILURE_TIMEOUT_MS` used identically across tasks.

**No placeholders.** Every step has the actual content needed.
