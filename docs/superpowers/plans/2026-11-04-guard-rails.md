# Guard Rails Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the eleven fixes from the 2026-11-04 code review (round cap, guarded `web_fetch`, quoted `[EXEC]`, SSH setup timeout, one WinHTTP session helper, clear-text URL warning, settings copy, JSON depth cap, test hygiene, lint, README) in one pull request with one version bump.

**Architecture:** Every decision that can be tested is a pure function in `src/core/` (or `src/config/`) with native tests: the address and URL policy (`net_guard`), the `web_fetch` hop loop over an injectable backend, the fence-aware `[EXEC]` scanner, the round counter, the JSON depth cap, the SSH timeout wording. `src/ui/` and the `#ifdef _WIN32` backends only wire those helpers into Win32.

**Tech Stack:** C11, MinGW-w64 cross-compile, WinHTTP, libssh2, native gcc test runner (`tests/test_framework.h`).

**Spec:** `docs/superpowers/specs/2026-11-04-guard-rails-design.md`. Read it with this plan; where a step says "spec §N" the wording and values there are binding.

## Global Constraints

- Worktree `/home/thomas/nutshell-wt/review`, branch `fix/code-review-2026-11-04`. All rules in `CLAUDE.md` apply.
- TDD: tests first for everything in `src/core/`, `src/config/`, `src/term/ssh_session.c`. Each new test function is declared and called in `tests/runner.c`.
- Baseline on 2026-11-04: `make test` runs 3,306 tests, 0 failed, with 7 compiler warnings (`make test 2>&1 | grep -c 'warning:'`). Tasks 1-15 must not raise the warning count; Task 16 brings it to 0.
- **Cross-compile check** (every task that touches a file compiled into the exe; never `make clean` before Task 19): `find src -name '*.o' -delete && make -j"$(nproc)" build/win/nutshell.exe && git restore build/win/nutshell.exe`. The Makefile has no header dependencies, so stale objects would hide a broken caller; deleting the objects forces every file to compile with `-Werror -Wpedantic -Wconversion -Wshadow` and links (catching a WinHTTP symbol missing from `build-support/delayload/winhttp.def`). The exe changes only in Task 19.
- **No version bump before Task 19.** Task 19 sets the version to one above the higher of `main`'s version and the branch's last build, worked out then. Never write a literal version anywhere else.
- **Line endings.** CRLF in the index, and must stay CRLF: `src/core/ai_prompt.c`, `src/core/ai_prompt.h`, `src/config/json_parser.c`, `src/ui/ai_http_win.c`, `src/crypto/crypto.c`, `src/crypto/crypto.h`, `tests/test_crypto.c`, `tests/test_string_utils.c`, `tests/test_json.c` (missing from the spec's list; it is CRLF), `Makefile`. Every other touched file is LF; new files are LF. Before each commit: `git diff --cached --stat` and `git diff --cached --stat --ignore-cr-at-eol` must show the same line counts per file.
- Design system (`src/ui`): no `RGB(`, colours from `ns_tokens()`, sizes from `ns_type.h`, `ns_scale`, `ns_font`, `ns_hover`. `tests/test_ui_tokens.c` passes unchanged in its rules (Task 16 only rewords three comments).
- Secrets: buffers that held a secret are wiped with `secure_zero()`. No new code logs or displays a key, a password, URL userinfo or a URL's query string.
- Test files that create files open them with `test_fopen_private()` and build paths from `TEST_TMP_DIR`. (No task here needs a file; the rule stands for any that does.)
- `#include <winsock2.h>` (then `<ws2tcpip.h>`) before `<windows.h>` in any file needing both.
- **Commits.** One commit per task unless a step says otherwise. Stage new files by path and read every `??` line of `git status --short` first; never `git add -u` alone. Every commit message ends with this block (add `claude-opus-5-5 (review)` or another model to the `Models:` line when one touched the task):

```
Models: claude-opus-5-5 (plan); claude-sonnet-5-5 (implementation)
Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg
```

## Review Focus

1. **URL parser differential in `web_fetch`.** `http://127.0.0.1\@evil.test/`, `http://evil.test@127.0.0.1/`, `http://127.0.0.1#@evil.test/`: the host the guard checks must be the host the request goes to. Pinned by Task 1's parse tests (backslash refused, userinfo and `#`/`?` split) and by Task 10 connecting with the parsed parts, never re-cracking the URL.
2. **Userinfo and query strings in chat rows.** `https://user:pw@host/p?key=secret` must show as `https://host/p` in every `fetching:`/`fetch refused:` row and the rebuilt `using tool:` row. Pinned by Task 2's `net_url_display` tests and Task 3's on_hop test.
3. **A raised policy ceiling releasing a quoted row.** Raising Allowed to Critical must not turn a "quoted, not run" row into a runnable one, in the queue or on the card. Pinned by Task 6 (`chat_approval_unblock_all` skips quoted) and Task 12 (the unblock walk skips quoted items).
4. **A marker on the fence line itself** (` ```[EXEC]rm -rf ~[/EXEC] `) and CRLF fences. The renderer treats the line as a fence, so the marker is quoted. Pinned by Task 5's tests.
5. **A resolver answer the guard cannot see in full**: an over-long URL silently truncated (today's 2048-byte `url_copy`), or more addresses than the buffer holds. Both refuse (fail closed). Pinned by Task 3's tests.

---

### Task 1: `net_guard` address policy, URL parse and clear-text test

**Files:**
- Create: `src/core/net_guard.h`, `src/core/net_guard.c` (LF, no `windows.h`)
- Create: `tests/test_net_guard.c`
- Modify: `tests/runner.c`

**Interfaces:**
- Produces:
  - `typedef struct { int v6; unsigned char b[16]; } NetAddr;` (IPv4 in `b[0..3]` when `v6 == 0`)
  - `int net_ipv4_is_internal(const unsigned char a[4]);`
  - `int net_ipv6_is_internal(const unsigned char a[16]);`
  - `int net_addr_is_internal(const NetAddr *a);` NULL returns 1 (fail closed)
  - `#define NET_URL_MAX 2048`, `#define NET_HOST_MAX 256`
  - `typedef struct { int https; char host[NET_HOST_MAX]; unsigned short port; char path[NET_URL_MAX]; } NetUrlParts;` (`path` is path plus query, never the fragment, always starting with `/`)
  - `int net_url_parse(const char *url, NetUrlParts *out);` 0 or -1
  - `int net_url_cleartext_remote(const char *url);`

- [ ] **Step 1: Write the failing tests** in `tests/test_net_guard.c`, declared and called in `tests/runner.c`. Helper `static unsigned char *v4(a,b,c,d)` into a static buffer is fine.

```c
int test_net_ipv4_internal_ranges(void);   /* each: ASSERT_TRUE */
/* 127.0.0.1, 127.255.255.255, 10.0.0.0, 10.255.255.255, 172.16.0.0, 172.31.255.255,
   192.168.0.1, 169.254.169.254, 0.0.0.0, 0.255.255.255 */
int test_net_ipv4_public_edges(void);      /* each: ASSERT_FALSE */
/* 126.255.255.255, 128.0.0.0, 9.255.255.255, 11.0.0.0, 172.15.255.255, 172.32.0.0,
   192.167.255.255, 192.169.0.0, 169.253.255.255, 169.255.0.0, 1.0.0.0, 8.8.8.8,
   100.64.0.1 (CGNAT: not refused), 224.0.0.1 (multicast: not refused) */
int test_net_ipv6_internal(void);          /* ASSERT_TRUE */
/* ::1, ::, fc00::, fdff:ffff::1, fe80::1, febf::1, ::ffff:127.0.0.1, ::ffff:10.1.2.3,
   ::127.0.0.1 (IPv4-compatible), ::ffff:169.254.169.254 */
int test_net_ipv6_public(void);            /* ASSERT_FALSE */
/* 2001:db8::1, 2606:4700::1111, fec0::1, fe7f::1, fbff::1, ::ffff:8.8.8.8, ::8.8.8.8,
   64:ff9b::7f00:1 (NAT64: not refused), ff02::1 (multicast: not refused) */
int test_net_addr_is_internal_dispatch(void);
/* v6=0 uses b[0..3]; v6=1 uses all 16; NULL -> 1 */
int test_net_url_parse_basic(void);
/* "http://example.com" -> https 0, host "example.com", port 80, path "/"
   "https://example.com:8443/a?b#c" -> https 1, port 8443, path "/a?b"
   "HTTP://Example.com/x" -> accepted, port 80
   "http://example.com?q=1" -> path "/?q=1"
   "http://example.com:/x" -> port 80 (empty port = default) */
int test_net_url_parse_userinfo_and_delims(void);
/* "http://user:pw@10.0.0.1/" -> host "10.0.0.1"
   "http://evil.test@127.0.0.1/" -> host "127.0.0.1"
   "http://127.0.0.1#@evil.test/" -> host "127.0.0.1", path "/"
   "http://127.0.0.1?@evil.test/" -> host "127.0.0.1", path "/?@evil.test/" */
int test_net_url_parse_ipv6_literal(void);
/* "http://[::1]:8080/x" -> host "::1", port 8080; "https://[fe80::1]/" -> port 443 */
int test_net_url_parse_refuses(void);      /* each returns -1 */
/* NULL, "", "ftp://x/", "file:///etc/passwd", "http://", "http:///x", "http://:80/",
   "http://host:0/", "http://host:65536/", "http://host:8a/", "http://[::1/",
   "http://a\\@b/" (any backslash), "http://a b/", "http://a\tb/", "http://a/\x7f",
   a URL of NET_URL_MAX bytes or more, a host of NET_HOST_MAX bytes or more */
int test_net_url_cleartext_remote(void);
/* 1: "http://example.com/v1", "http://myhost/", "http://10.0.0.5/", "http://127.1/",
      "http://256.0.0.1/", "http://127.0.0.1.evil.test/", "http://localhost.evil.test/",
      "http://localhost@evil.test/", "http://" (unparseable http warns: safe direction)
   0: "http://localhost:11434/v1", "http://LOCALHOST./v1", "http://127.0.0.1:8080",
      "http://127.5.6.7/", "http://[::1]:8080/", "http://user@localhost/",
      "https://example.com", "", NULL, "ftp://example.com" */
```

- [ ] **Step 2: Run `make test`.** Expected: link failure, `net_ipv4_is_internal` undefined.

- [ ] **Step 3: Implement `src/core/net_guard.c/.h`.** Ranges exactly as spec §2 "Address policy". IPv6: `::1` and `::` are internal outright; `::ffff:0:0/96` and `::/96` delegate their last four bytes to the v4 test. `net_url_parse`: scheme `http://`/`https://` case-insensitive; refuse any byte `< 0x21`, `0x7F` or `\` anywhere in the URL; authority ends at the first `/`, `?` or `#`; userinfo is everything up to the last `@` inside the authority and is dropped; bracketed host is stored without brackets; port 1-65535 decimal or empty. `net_url_cleartext_remote`: 1 for an `http://` URL unless it parses and the host is `localhost`/`localhost.` (case-insensitive), a dotted quad of four decimal 0-255 parts starting `127`, or `::1` written bracketed; 0 otherwise. Text only, no DNS.

- [ ] **Step 4: Run `make test`.** Expected: all pass, `Tests Run` up by 10, warnings still 7.

- [ ] **Step 5: Commit** (`feat(core): net_guard address and URL policy`) with the trailer block from Global Constraints, e.g.

```bash
git add src/core/net_guard.h src/core/net_guard.c tests/test_net_guard.c tests/runner.c
git commit -F - <<'EOF'
feat(core): net_guard address and URL policy

Pure checks for web_fetch and the Settings warning: internal IPv4/IPv6
ranges, a strict http(s) URL parser and the clear-text remote test.

Models: claude-opus-5-5 (plan); claude-sonnet-5-5 (implementation)
Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg
EOF
```

### Task 2: `net_guard` redirect resolution and display form

**Files:**
- Modify: `src/core/net_guard.h`, `src/core/net_guard.c`, `tests/test_net_guard.c`, `tests/runner.c`

**Interfaces:**
- Consumes: `net_url_parse` (Task 1); `paste_filter_controls()` (`src/core/paste_filter.h`) for control and bidi removal.
- Produces:
  - `int net_url_resolve(const char *base, const char *location, char *out, size_t out_sz);` 0 or -1
  - `#define NET_URL_DISPLAY_MAX 80`
  - `size_t net_url_display(const char *url, char *out, size_t cap);` returns the length written

- [ ] **Step 1: Write the failing tests.**

```c
int test_net_url_resolve_absolute(void);
/* ("http://a.test/x", "https://b.test/y") -> "https://b.test/y"
   ("http://a.test/x", "HTTPS://b.test/") -> accepted */
int test_net_url_resolve_scheme_relative(void);
/* ("https://a.test/x", "//b.test/p") -> "https://b.test/p" */
int test_net_url_resolve_absolute_path(void);
/* ("http://a.test:8080/x/y?q", "/z") -> "http://a.test:8080/z" */
int test_net_url_resolve_relative(void);
/* ("http://a.test/x/y", "z?k=1") -> "http://a.test/x/z?k=1"
   ("http://a.test", "z") -> "http://a.test/z"
   ("http://a.test/x/y", "../z") -> "http://a.test/x/../z" (dot segments kept as written)
   ("http://a.test/x?old", "?new") -> "http://a.test/x?new" */
int test_net_url_resolve_refuses(void);    /* each -1 */
/* location "ftp://x/", "file:///etc/passwd", "javascript:alert(1)", "data:text/html,x",
   "mailto:a@b", "", NULL, "/a b", "/a\r\nSet-Cookie: x", "/a\\b";
   base "ftp://a/"; a result that does not fit out_sz (out_sz 16 for a 30-byte result) */
int test_net_url_display(void);
/* "https://example.com/p?key=secret#frag" -> "https://example.com/p"
   "http://user:pw@example.com/p" -> "http://example.com/p"
   "http://ex\xE2\x80\xAEample.com/" -> "http://example.com/"   (bidi removed)
   "http://ex\x01ample.com/" -> "http://example.com/"            (C0 removed)
   a 200-byte URL -> ends with "...", strlen <= NET_URL_DISPLAY_MAX + 3
   a URL whose cut falls inside a 3-byte UTF-8 character -> no partial sequence
   cap 8 -> NUL-terminated, strlen < 8; NULL url -> "" */
```

- [ ] **Step 2: Run `make test`.** Expected: link failure on `net_url_resolve`.

- [ ] **Step 3: Implement.** `net_url_resolve`: refuse a `base` that is not http(s) and any location byte `< 0x21`, `0x7F` or `\`; a location with a `:` before its first `/`, `?` or `#` is a scheme and only `http`/`https` (case-insensitive) are accepted; `//` takes the base's scheme; `/` takes the base's scheme and authority; `?` replaces the base's query; anything else replaces the base path after its last `/`. No dot-segment removal. The result must also pass `net_url_parse` or the call returns -1. `net_url_display`: drop userinfo and everything from the first `?` or `#`, filter with `paste_filter_controls`, cut at `NET_URL_DISPLAY_MAX` bytes on a UTF-8 boundary and append `...` when cut.

- [ ] **Step 4: Run `make test`.** Expected: all pass, warnings 7.

- [ ] **Step 5: Commit** (`feat(core): resolve redirects and sanitise URLs for display`) with the trailer block.

### Task 3: portable `web_fetch` hop loop over an injectable backend

**Files:**
- Modify: `src/core/ai_tool_web_fetch.h`, `src/core/ai_tool_web_fetch.c` (the old inline `#ifdef _WIN32` WinHTTP block ~197-342 is deleted here; until Task 10 adds the WinHTTP backend, `web_fetch_default_backend()` returns NULL on every platform, so an intermediate Windows build answers "not available")
- Create: `tests/test_web_fetch_guard.c` (the fake backend lives here, `static`, in the style of `tests/fake_dpapi.c`)
- Modify: `tests/test_tool_web_fetch.c` (the native "not available" case), `tests/runner.c`

**Interfaces:**
- Consumes: Task 1 and 2 (`NetAddr`, `NetUrlParts`, `net_url_parse`, `net_url_resolve`, `net_addr_is_internal`, `net_url_display`), `web_fetch_html_to_text`.
- Produces (in `ai_tool_web_fetch.h`):

```c
#define WEB_FETCH_TOTAL_MS      30000
#define WEB_FETCH_MAX_REDIRECTS 5
#define WEB_FETCH_MAX_ADDRS     32

typedef struct WebFetchBackend {
    void *ctx;
    /* Every address `host` resolves to: writes up to `max` into out and returns
     * the total found (may exceed max), or -1 when resolution fails. */
    int (*resolve)(void *ctx, const char *host, NetAddr *out, int max);
    /* One GET that never follows redirects. Returns 0 when a response arrived
     * (any status), -1 on failure or cancel. *location is the Location header
     * or "". *body is malloc'd (caller frees), at most AI_TOOL_RESULT_MAX * 4. */
    int (*get)(void *ctx, const NetUrlParts *url, int timeout_ms,
               volatile int *cancel, int *status,
               char *location, size_t location_cap,
               char **body, size_t *body_len);
    unsigned long long (*now_ms)(void *ctx);
} WebFetchBackend;

typedef struct {
    int timeout_ms;                    /* per-hop cap (default 10000) */
    void (*on_hop)(void *user, const char *display_url, int refused);
    void *user;
    const WebFetchBackend *backend;    /* NULL: web_fetch_default_backend() */
} WebFetchContext;

/* WinHTTP on _WIN32 from Task 10 on; NULL elsewhere (and on _WIN32 until then). */
const WebFetchBackend *web_fetch_default_backend(void);

/* The "using tool:" suffix: the requested URL in display form. Returns 1, or
 * 0 (out = "") when the input has no usable url. */
int web_fetch_describe(const char *input_json, char *out, size_t cap);
```

  `on_hop` receives the `net_url_display` form, never the raw URL. `refused` is 1 only for a host refused as local or private.

- [ ] **Step 1: Write the failing tests** in `tests/test_web_fetch_guard.c`. The fake holds a host-to-addresses table, a scripted list of responses (status, Location, body) consumed one per `get`, a clock that each `get` advances by a configurable step, and records: resolve calls, get calls, each get's `NetUrlParts` and `timeout_ms`, and each `on_hop` (display URL, refused). Every test calls `tool_web_fetch_execute("{\"url\":\"...\"}", &ctx, &cancel, &buf, &len, &trunc)` with `ctx.backend = &fake`.

```c
int test_web_fetch_success_converts_html(void);
/* pub.test -> 93.184.216.34; 200 "<p>Hi</p>" -> rc 0, buf "Hi", gets 1,
   one on_hop ("http://pub.test/", 0) */
int test_web_fetch_refuses_loopback_first_hop(void);
/* host -> 127.0.0.1 -> rc -1, strstr(buf,"local or private"), gets 0,
   one on_hop with refused 1 */
int test_web_fetch_refuses_if_any_address_internal(void);
/* host -> {8.8.8.8, 10.0.0.1} -> rc -1, gets 0 */
int test_web_fetch_refuses_when_resolve_fails(void);
/* resolve -1 -> rc -1, strstr(buf,"resolve"), gets 0, no on_hop */
int test_web_fetch_refuses_too_many_addresses(void);
/* resolve returns WEB_FETCH_MAX_ADDRS + 1 -> rc -1, gets 0 */
int test_web_fetch_refuses_internal_at_later_hop(void);
/* pub.test 302 Location "http://intra.test/" (-> 192.168.1.1) -> rc -1, gets 1,
   on_hop rows: ("http://pub.test/",0), ("http://intra.test/",1) */
int test_web_fetch_follows_five_redirects(void);
/* 5 x 302 then 200 -> rc 0, gets 6 */
int test_web_fetch_sixth_redirect_fails(void);
/* 6 x 302 -> rc -1, strstr(buf,"too many redirects"), gets 6 */
int test_web_fetch_redirect_statuses(void);
/* each of 301, 302, 303, 307, 308 is followed; 300 and 304 are returned as final */
int test_web_fetch_deadline(void);
/* clock step 11000, every get 302 -> timeouts recorded {10000, 10000, 8000},
   gets 3, rc -1, strstr(buf,"timed out"), no body text in buf */
int test_web_fetch_cancel_before_first_hop(void);
/* cancel = 1 on entry -> rc -1, strstr(buf,"cancelled"), resolves 0, gets 0 */
int test_web_fetch_cancel_during_get(void);
/* fake get sets *cancel = 1 and returns 200 "<p>secret</p>" -> rc -1,
   strstr(buf,"cancelled"), strstr(buf,"secret") == NULL */
int test_web_fetch_https_to_http_refused(void);
/* https://pub.test/ 301 "http://pub.test/" -> rc -1, strstr(buf,"https to http"), gets 1 */
int test_web_fetch_relative_locations(void);
/* "http://pub.test/a/b" 302 "c" -> 2nd get host "pub.test" path "/a/c";
   302 "//other.test/p" -> resolve called with "other.test", path "/p" */
int test_web_fetch_bad_location_refused(void);
/* 302 with "" and with "file:///etc/passwd" -> rc -1, strstr(buf,"redirect") */
int test_web_fetch_passes_port(void);
/* "http://pub.test:8080/x" -> get sees port 8080, path "/x" */
int test_web_fetch_on_hop_hides_query_and_userinfo(void);
/* "https://u:pw@pub.test/p?key=secret" -> on_hop url "https://pub.test/p" */
int test_web_fetch_url_too_long_refused(void);
/* a 3000-byte http URL -> rc -1, resolves 0 (no silent truncation) */
int test_web_fetch_describe(void);
/* "{\"url\":\"https://a.test/p?k=v\"}" -> 1, "https://a.test/p"; "{}" -> 0, "" */
```

  In `tests/test_tool_web_fetch.c` add `test_web_fetch_no_backend_not_available` under `#ifndef _WIN32`: `tool_web_fetch_execute("{\"url\":\"http://a.test/\"}", NULL, NULL, ...)` returns -1 and `buf` contains `"not available"`.

- [ ] **Step 2: Run `make test`.** Expected: compile failure (`WebFetchBackend` unknown).

- [ ] **Step 3: Implement the loop in `tool_web_fetch_execute`**, outside `#ifdef _WIN32`. Keep the existing JSON and scheme checks; replace the 2048-byte silent truncation with a refusal. Backend: `ctx->backend` if set, else `web_fetch_default_backend()`; NULL keeps today's text `"Web fetch not available (non-Windows build)"`. Order per hop, written so it cannot fetch before checking:

```
deadline = now + WEB_FETCH_TOTAL_MS
for hop = 0 .. WEB_FETCH_MAX_REDIRECTS:
    cancelled? -> "Fetch cancelled";  now >= deadline? -> "Fetch timed out after 30 s"
    net_url_parse(url) else refuse
    n = resolve(host);  n < 0 or n > WEB_FETCH_MAX_ADDRS -> "Refused: could not resolve <host>"
    any net_addr_is_internal -> on_hop(display, 1); "Refused: ... local or private address"
    on_hop(display, 0)
    get(parts, min(per_hop, deadline - now), cancel, ...)
    cancelled? / past deadline? -> same errors, body freed, no body text returned
    get failed -> "HTTP request failed"
    status in {301,302,303,307,308}:
        hop == WEB_FETCH_MAX_REDIRECTS -> "Refused: too many redirects (more than 5)"
        net_url_resolve(url, location) fails -> "Refused: redirect to an unsupported or invalid location"
        https -> http -> "Refused: redirect from https to http"
        url = next; free body; continue
    otherwise: web_fetch_html_to_text(body) and return its result
```

  `per_hop` is `ctx->timeout_ms` when positive, else 10000. Add the residual-risk comment from spec §2 ("Where the check runs") above the resolve step: WinHTTP resolves again, a rebinding answer between the two lookups is not caught, a proxy resolves on its own, and pinning the address would break TLS name checks. `WINHTTP_OPTION_CONNECTION_INFO` is not used (builder's choice settled: it only narrows the gap after the request is sent). Implement `web_fetch_describe` with `json_parse` + `net_url_display`.

- [ ] **Step 4: Run `make test`.** Expected: all pass, warnings 7. The existing `test_web_fetch_url_validation` still passes.

- [ ] **Step 5: Cross-compile check** (Global Constraints).

- [ ] **Step 6: Commit** (`feat(web_fetch): portable hop loop with address guard, redirect cap, deadline and cancel`).

### Task 4: JSON nesting cap

**Files:**
- Modify: `src/config/json_parser.c` (CRLF), `src/config/json_parser.h` (LF), `tests/test_json.c` (CRLF), `tests/runner.c`

**Interfaces:**
- Produces: `#define JSON_MAX_DEPTH 256` in `json_parser.h`.

- [ ] **Step 1: Write the failing tests** in `tests/test_json.c`, building inputs with `malloc` (not on the stack):

```c
int test_json_depth_256_arrays_parse(void);   /* "[" x256 + "]" x256 -> non-NULL, json_free */
int test_json_depth_256_objects_parse(void);  /* {"a":{"a":...{}...}} 256 levels -> non-NULL */
int test_json_depth_256_mixed_parse(void);    /* alternating [ and {"k": , 256 levels -> non-NULL */
int test_json_depth_257_fails(void);          /* 257 arrays -> NULL; 257 objects -> NULL */
int test_json_depth_100000_fails(void);       /* 100000 "[" with no closers -> NULL, no crash */
int test_json_depth_resets_between_siblings(void); /* "[" + 200-deep + "," + 200-deep + "]" -> non-NULL */
```

- [ ] **Step 2: Run `make test`.** Expected: `test_json_depth_257_fails` fails (the deep one may crash: that is the bug).

- [ ] **Step 3: Implement.** `Parser` gains `int depth`; `parse_array()`/`parse_object()` increment on entry, decrement on every return path, and set `p->error = 1` past `JSON_MAX_DEPTH`, freeing the partial node as the other error paths do. Keep CRLF.

- [ ] **Step 4: Run `make test`.** Expected: all pass, warnings 7. Leak check of the deep failure: run the ASan recipe from Task 16 Step 4 and confirm no report names `json_parser.c`.

- [ ] **Step 5: Commit** (`fix(json): cap nesting depth at 256`).

### Task 5: fence-aware `[EXEC]` extraction and the prompt sentence

**Files:**
- Modify: `src/core/ai_prompt.c` (CRLF), `src/core/ai_prompt.h` (CRLF)
- Create: `tests/test_ai_exec_quoted.c`
- Modify: `tests/test_ai_prompt.c` (prompt pins), `tests/runner.c`

**Interfaces:**
- Consumes: `md_code_blocks()` (`src/core/md_blocks.h`); `md_classify_line` semantics.
- Produces:

```c
int ai_extract_commands_full(const char *response, char cmds[][1024],
                             char notes[][CMD_NOTE_MAX], int quoted[],
                             int max_cmds, int *rejected, int *rejected_long);
```

  Live and quoted markers in reply order; `quoted[i]` is 1 for a quoted one. `quoted == NULL` skips quoted markers entirely (they take no slot and are not counted in `rejected`/`rejected_long`). `notes` may be NULL. `ai_extract_commands_notes()` keeps its signature and calls `_full` with `quoted = NULL`. `ai_extract_command()` returns the first live marker. `ai_response_split()` counts and cuts only at live markers.

- [ ] **Step 1: Write the failing tests** in `tests/test_ai_exec_quoted.c`:

```c
int test_exec_quoted_fence_skipped_by_notes(void);
/* "Run:\n```\n[EXEC]rm -rf /tmp/x[/EXEC]\n```\n[EXEC]ls[/EXEC]" -> _notes returns 1, cmds[0] "ls" */
int test_exec_quoted_full_reports_both_in_order(void);
/* same text -> _full 2; cmds[0] "rm -rf /tmp/x" quoted[0] 1; cmds[1] "ls" quoted[1] 0 */
int test_exec_quoted_unterminated_fence_runs_to_end(void);
/* "```\n[EXEC]a[/EXEC]\nmore\n[EXEC]b[/EXEC]" -> _notes 0; _full 2, both quoted */
int test_exec_quoted_indented_fence_is_not_a_fence(void);
/* "  ```\n[EXEC]ls[/EXEC]\n  ```" -> _notes 1 */
int test_exec_quoted_marker_on_fence_line(void);
/* "```[EXEC]rm x[/EXEC]\n```\n" -> _notes 0; _full 1 quoted */
int test_exec_quoted_crlf_fences(void);
/* "```\r\n[EXEC]ls[/EXEC]\r\n```\r\n[EXEC]pwd[/EXEC]" -> _notes 1, "pwd" */
int test_exec_quoted_inline_span(void);
/* "Try `[EXEC]ls[/EXEC]` here" -> _notes 0; _full 1 quoted */
int test_exec_quoted_unpaired_backtick_opens_nothing(void);
/* "a ` b [EXEC]ls[/EXEC]" -> _notes 1 */
int test_exec_quoted_span_does_not_cross_lines(void);
/* "a `b\n[EXEC]ls[/EXEC] c`" -> _notes 1 */
int test_exec_live_command_with_backticks(void);
/* "[EXEC]echo `date`[/EXEC]" -> _notes 1, cmds[0] "echo `date`" */
int test_exec_quoted_same_checks_apply(void);
/* "`[EXEC]a\x1b[/EXEC]`" -> _full 0, rejected 1; a quoted 1024-byte payload -> rejected_long 1;
   with quoted == NULL both counts stay 0 */
int test_exec_quoted_notes_align(void);
/* "Explain\n`[EXEC]x[/EXEC]`\nThen\n[EXEC]ls[/EXEC]" -> notes[1] "Then" */
int test_exec_quoted_takes_slots(void);
/* 3 quoted + 1 live, max_cmds 2 -> _full returns 2 */
int test_extract_command_skips_quoted(void);
/* "`[EXEC]rm x[/EXEC]` then [EXEC]ls[/EXEC]" -> 1, "ls"; "`[EXEC]rm x[/EXEC]`" -> 0 */
int test_response_split_ignores_quoted(void);
/* "Intro\n```\n[EXEC]a[/EXEC]\n```\nThen\n[EXEC]ls[/EXEC]\nAfter" -> returns 1,
   strstr(pre,"[EXEC]a[/EXEC]") and strstr(pre,"Then"), strstr(post,"After");
   an all-quoted reply returns 0 and pre equals the whole reply */
int test_system_prompt_quoted_marker_sentence(void);
/* SSH, local bash and local PowerShell prompts each contain
   "Put each [EXEC]...[/EXEC] marker on its own line, never inside a code block or
    inline code: a marker inside code is shown but not run." */
```

  Update `expected_ssh` in `tests/test_ai_prompt.c` (~910-928) to end with `"Include every command the user needs in one response. "` followed by the new sentence.

- [ ] **Step 2: Run `make test`.** Expected: link failure on `ai_extract_commands_full`.

- [ ] **Step 3: Implement** a static scanner in `ai_prompt.c` that yields each `[EXEC]`...`[/EXEC]` with a `quoted` flag, used by `_full`, `ai_extract_command` and `ai_response_split`. Quoted means the opening `[EXEC]` lies inside a fenced block as `md_code_blocks()` reports it, widened to include its opening and closing fence lines, or inside an inline span (a backtick pairs with the next backtick on the same line; an unpaired one opens nothing; spans are computed per line from its start). With `quoted == NULL` a quoted marker is skipped before any check; otherwise it goes through the same trim, 1024-byte check and `text_has_unsafe_command_char()` as a live one and counts in `rejected`/`rejected_long` the same way. Append the sentence in spec §3 "Prompt" to both `rest` and `rest_powershell` (~282-306), after the last sentence, separated by one space. Keep CRLF.

- [ ] **Step 4: Run `make test`.** Expected: all pass (the 26 existing extraction calls included), warnings 7.

- [ ] **Step 5: Commit** (`feat(ai): never run an [EXEC] marker quoted in code`).

### Task 6: quoted rows in the approval queue, the card model and the continue text

**Files:**
- Modify: `src/core/chat_approval.h`, `src/core/chat_approval.c`, `src/core/chat_msg.h`, `src/core/chat_msg.c`, `src/core/ai_prompt.c` (CRLF; `ai_continue_outcome_word` ~1651 and its caller), `src/core/ai_prompt.h` (CRLF; doc comment of `ai_build_continue_text`)
- Modify: `src/ui/chat_listview.c` (~1937 call site only, to keep the build green)
- Test: `tests/test_chat_approval.c`, `tests/test_chat_msg.c`, `tests/test_ai_prompt.c`, `tests/runner.c`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `ApprovalEntry` gains `int quoted;` (zeroed by every add)
  - `int chat_approval_add_quoted(ApprovalQueue *q, const char *command, CmdPlatform platform, int contradicted);` classifies like `chat_approval_add_session` (for the chip), sets `APPROVE_BLOCKED` and `quoted = 1`, ignores the policy; same -1 cases
  - `ChatMsgItem.u.cmd` gains `int quoted;`; `void chat_msg_set_quoted(ChatMsgItem *item, int quoted);`
  - `const char *chat_cmd_label(int approved, int blocked, int quoted, int run, ChatCmdIntent *intent);` quoted outranks everything: `"quoted, not run"`, `CHAT_CMD_INTENT_DIM`
  - `int chat_card_header_text(int n, int held, int quoted, char *buf, size_t cap);` returns the length or 0 when it does not fit

- [ ] **Step 1: Write the failing tests.**

```c
/* test_chat_approval.c */
int test_chat_approval_add_quoted_blocked_even_with_auto(void);
/* policy auto-on at CMD_CRITICAL; add_quoted("ls") -> status APPROVE_BLOCKED, quoted 1, safety CMD_READ */
int test_chat_approval_add_quoted_rejects_like_add(void);
/* "", "   ", "a\nb", 1024-byte command, 17th entry -> -1 */
int test_chat_approval_add_session_quoted_zero(void);
/* add_quoted, reset, add_session -> entries[0].quoted 0 */
int test_chat_approval_unblock_all_skips_quoted(void);
/* policy allowed CMD_READ; add_session("rm x") (BLOCKED) + add_quoted("ls");
   raise allowed to CMD_CRITICAL; unblock_all returns 1; quoted entry still BLOCKED */
int test_chat_approval_approve_quoted_fails(void);   /* approve(q, idx) == -1 */
int test_chat_approval_needs_user_all_quoted(void);  /* only quoted rows -> 1 (same as all-held) */
/* test_chat_msg.c */
int test_chat_cmd_label_quoted(void);
/* (-1,1,1,CHAT_RUN_NONE) and (1,1,1,CHAT_RUN_DONE) -> "quoted, not run", DIM */
int test_chat_card_header_text(void);
/* (3,0,0) "3 commands"; (1,0,0) "1 command"; (3,1,0) "3 commands \xC2\xB7 1 held";
   (3,0,2) "3 commands \xC2\xB7 2 quoted"; (4,1,2) "4 commands \xC2\xB7 1 held \xC2\xB7 2 quoted";
   cap 8 -> returns 0, buf NUL-terminated */
int test_chat_msg_set_quoted(void);   /* sets u.cmd.quoted; NULL item is safe */
/* test_ai_prompt.c */
int test_continue_text_quoted_entry(void);
/* queue with add_quoted("ls") -> strstr(buf, "1. quoted in a code block, not run: ls") */
```

  Update the eight existing `chat_cmd_label(...)` calls in `tests/test_chat_msg.c` (~451-506) to pass `quoted = 0`.

- [ ] **Step 2: Run `make test`.** Expected: compile failure (new fields and functions).

- [ ] **Step 3: Implement.** `chat_approval_unblock_all` skips `quoted` entries. `ai_continue_outcome_word` takes the entry (or its `quoted` flag) and returns `"quoted in a code block, not run"` for a quoted one. In `chat_listview.c` ~1937 pass `item->u.cmd.quoted`. Update the doc comments of `chat_cmd_label` and `ai_build_continue_text`.

- [ ] **Step 4: Run `make test`.** Expected: all pass, warnings 7.

- [ ] **Step 5: Cross-compile check.**

- [ ] **Step 6: Commit** (`feat(chat): quoted rows that never run and say so`).

### Task 7: round counter helpers and `CmdBatch.user_acted`

**Files:**
- Modify: `src/core/ai_prompt.h` (CRLF), `src/core/ai_prompt.c` (CRLF), `src/core/cmd_batch.h`
- Create: `tests/test_ai_rounds.c`
- Modify: `tests/test_cmd_batch.c`, `tests/runner.c`

**Interfaces:**
- Produces:
  - `AiSessionState` gains `int auto_rounds;` with `#define AI_AUTO_ROUNDS_MAX 3` beside it
  - `void ai_rounds_user_message(AiSessionState *s);` sets 0
  - `void ai_rounds_continue(AiSessionState *s, int unattended);` +1 when unattended, else 0
  - `CmdPolicy ai_rounds_batch_policy(const AiSessionState *s, CmdPolicy p);` `p`, or `cmd_policy_attended(p)` once `auto_rounds >= AI_AUTO_ROUNDS_MAX`
  - `int ai_rounds_paused(const AiSessionState *s, CmdPolicy p);` 1 when the cap is reached and `cmd_policy_auto_on(p)`
  - `CmdBatch` gains `int user_acted;` (0 from `cmd_batch_add`'s calloc)
  - All four helpers are NULL-safe (`batch_policy(NULL, p)` returns `p`, `paused(NULL, p)` returns 0).

- [ ] **Step 1: Write the failing tests.**

```c
int test_ai_rounds_user_message_resets(void);      /* auto_rounds 5 -> 0 */
int test_ai_rounds_continue_unattended_counts(void); /* 0 -> 1 -> 2 */
int test_ai_rounds_continue_attended_resets(void); /* 2, continue(0) -> 0 */
int test_ai_rounds_three_rounds_then_paused(void);
/* user_message; 2 x continue(1): batch_policy(auto p) == p (allowed and unattended equal), paused 0;
   3rd continue(1): batch_policy(p).unattended == POLICY_NONE, .allowed == p.allowed, paused 1 */
int test_ai_rounds_auto_off_never_paused(void);    /* cap reached, auto off -> returns p, paused 0 */
int test_ai_rounds_session_policy_untouched(void); /* s->policy identical before and after batch_policy */
int test_ai_rounds_null_safe(void);
int test_cmd_batch_add_user_acted_zero(void);      /* in tests/test_cmd_batch.c */
```

- [ ] **Step 2: Run `make test`.** Expected: compile failure.

- [ ] **Step 3: Implement** in `ai_prompt.c` (CRLF kept).

- [ ] **Step 4: Run `make test`.** Expected: all pass, warnings 7.

- [ ] **Step 5: Commit** (`feat(ai): count unattended rounds per session`).

### Task 8: SSH setup timeout in `ssh_session` and its wording

**Files:**
- Modify: `src/core/ssh_timeout.h`, `src/core/ssh_timeout.c`, `tests/test_ssh_timeout.c`
- Modify: `src/term/ssh_session.h`, `src/term/ssh_session.c` (~247-256), `tests/test_ssh.c`
- Modify: `tests/runner.c`

**Interfaces:**
- Produces:
  - `typedef enum { SSH_STAGE_HANDSHAKE, SSH_STAGE_AUTH, SSH_STAGE_SESSION } SshSetupStage;` (`ssh_timeout.h`)
  - `size_t ssh_setup_timeout_text(SshSetupStage stage, const char *host, int port, long timeout_ms, char *buf, size_t cap);` returns the length, 0 when it does not fit
  - `#define SSH_SETUP_TIMEOUT_MS 15000` (`ssh_session.h`)
  - `void ssh_session_set_timeout(SshSession *s, long ms);` (NULL-safe)
  - `int ssh_session_timed_out(const SshSession *s);` 1 when `libssh2_session_last_errno() == LIBSSH2_ERROR_TIMEOUT`; 0 for NULL or no session
  - `void ssh_setup_timeout_override(long ms);` test hook; 0 restores `SSH_SETUP_TIMEOUT_MS`

- [ ] **Step 1: Write the failing tests.**

```c
/* tests/test_ssh_timeout.c (runs in every native build) */
int test_ssh_setup_timeout_text_stages(void);
/* (HANDSHAKE,"example.com",22,15000) -> exactly
   "Timed out after 15 s waiting for example.com:22 during the SSH handshake."
   AUTH -> "... during authentication."; SESSION -> "... while opening the session." */
int test_ssh_setup_timeout_text_rounds_up(void);  /* 300 ms -> "after 1 s" */
int test_ssh_setup_timeout_text_ipv6_host(void);  /* "::1", 2222 -> "[::1]:2222" */
int test_ssh_setup_timeout_text_small_buf(void);  /* cap 10 -> 0, buf NUL-terminated */
/* tests/test_ssh.c (real-libssh2 builds only) */
int test_ssh_timeout_helpers_null_safe(void);
/* set_timeout(NULL,0) no crash; timed_out(NULL) 0; timed_out(new session) 0 */
int test_ssh_handshake_timeout_silent_server(void);
/* #ifndef _WIN32 (body is an empty TEST_BEGIN/TEST_END pass on Windows):
   socket + bind 127.0.0.1:0 + listen(1), never accept (the kernel completes the TCP
   handshake, the server never speaks); getsockname for the port;
   ssh_setup_timeout_override(300); rc = ssh_connect(s, "127.0.0.1", port);
   rc -1, elapsed >= 250 ms and < 5000 ms, ssh_session_timed_out(s) 1,
   strstr(s->last_error, "during the SSH handshake"); override(0); close the listener */
```

- [ ] **Step 2: Run `make test`.** Expected: link failure on `ssh_setup_timeout_text`. (On `automaton` the stub build excludes `tests/test_ssh.c`; those two tests run in the hosted `Native tests` job.)

- [ ] **Step 3: Implement.** `ssh_connect()` calls `libssh2_session_set_timeout(session, limit)` right before `libssh2_session_handshake()`; on handshake failure it writes `ssh_setup_timeout_text(SSH_STAGE_HANDSHAKE, host, port, limit, ...)` into `last_error` when `ssh_session_timed_out()`, else keeps `"SSH handshake failed"`. `limit` is the override when set, else `SSH_SETUP_TIMEOUT_MS`.

- [ ] **Step 4: Run `make test`.** Expected: all pass, warnings 7.

- [ ] **Step 5: Cross-compile check.**

- [ ] **Step 6: Commit** (`feat(ssh): 15 s limit on handshake and setup`).

### Task 9: one WinHTTP session helper

**Files:**
- Create: `src/core/winhttp_open.h`, `src/core/winhttp_open.c`
- Modify: `src/ui/ai_http_win.c` (CRLF; ~77, ~231, ~392 and the three `use_ssl` `SecureProtocols` blocks ~88-93, ~241-246, ~403-408), `src/core/ai_tool_web_search.c` (~408, ~534). The sixth site, `web_fetch`, calls the helper from its Task 10 backend.

**Interfaces:**
- Produces: `HINTERNET ns_winhttp_open(const wchar_t *agent);` declared and defined only under `#ifdef _WIN32`; `agent == NULL` means `L"Nutshell/1.0"`. (Settled: the agent is a parameter so `web_search` keeps its current `L"Nutshell/1.0 (Windows NT 10.0; Win64; x64)"` agent.) Outside `_WIN32` the `.c` holds only `typedef int ns_winhttp_open_unused;` (`-Wpedantic` rejects an empty unit).

- [ ] **Step 1: Implement** per spec §5: `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`, falling back to `WINHTTP_ACCESS_TYPE_DEFAULT_PROXY` when `WinHttpOpen` returns NULL; then `WINHTTP_OPTION_SECURE_PROTOCOLS` = TLS 1.2 | TLS 1.3, falling back to TLS 1.2 alone when `WinHttpSetOption` fails; timeouts left to callers. Replace the five `WinHttpOpen` calls; delete the three `use_ssl` blocks (keep `use_ssl` for `WINHTTP_FLAG_SECURE`). Keep `ai_http_win.c` CRLF.

- [ ] **Step 2: Run `make test`.** Expected: all pass, warnings 7 (native build compiles the typedef-only unit).

- [ ] **Step 3: Cross-compile check.** Expected: no new symbol needed in `winhttp.def` (`WinHttpOpen`, `WinHttpSetOption` are listed).

- [ ] **Step 4: Commit** (`refactor(net): one WinHTTP session helper with automatic proxy and TLS 1.2+`).

### Task 10: WinHTTP backend for the `web_fetch` loop

**Files:**
- Modify: `src/core/ai_tool_web_fetch.c` (`#ifdef _WIN32` part only)

**Interfaces:**
- Consumes: `WebFetchBackend`, `NetUrlParts`, `NetAddr` (Task 3, 1), `ns_winhttp_open` (Task 9).
- Produces: `web_fetch_default_backend()` returns a static WinHTTP backend on `_WIN32`.

- [ ] **Step 1: Implement** behind `#ifdef _WIN32`:
  - includes in order `<winsock2.h>`, `<ws2tcpip.h>`, `<windows.h>`, `<winhttp.h>`;
  - `resolve`: `getaddrinfo(host, NULL, {AF_UNSPEC, SOCK_STREAM})`, copying `sin_addr`/`sin6_addr` bytes into `NetAddr`, returning the total count; failure returns -1;
  - `get`: `ns_winhttp_open()` with `AI_TOOL_USER_AGENT` converted to UTF-16 (this replaces the empty `User-Agent:` header, spec §2 bug 2); `WinHttpConnect` with `url->host` and `url->port` (bug 1); `WinHttpOpenRequest` on `url->path`, `WINHTTP_FLAG_SECURE` when `url->https`; `WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, WINHTTP_DISABLE_REDIRECTS)`; `WinHttpSetTimeouts(req, t, t, t, t)`; status via `WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER`; `WINHTTP_QUERY_LOCATION` converted to UTF-8 (absent means `""`); body read loop as today, capped at `AI_TOOL_RESULT_MAX * 4`, checking `*cancel` after send, after receive and between reads, returning -1 on cancel; every handle closed on every path;
  - `now_ms`: `GetTickCount64()`.
  The backend never parses a URL string itself; it uses the parts the portable loop checked.

- [ ] **Step 2: Run `make test`.** Expected: all pass, warnings 7.

- [ ] **Step 3: Cross-compile check.** Expected: links (`getaddrinfo` from `-lws2_32`; `WinHttpQueryHeaders` already in `winhttp.def`).

- [ ] **Step 4: Commit** (`fix(web_fetch): WinHTTP backend with manual redirects, real port and User-Agent`).

### Task 11: round cap in the AI panel

**Files:**
- Modify: `src/ui/ai_chat.c`

**Interfaces:**
- Consumes: `ai_rounds_*`, `AI_AUTO_ROUNDS_MAX`, `CmdBatch.user_acted` (Task 7).

- [ ] **Step 1: Wire the counter.**
  - `send_user_message()` (~1718): `ai_rounds_user_message(d->active_state)` after the guard.
  - `send_continue_message()` (~1911) gains `int unattended` and calls `ai_rounds_continue(d->active_state, unattended)` after its guards.
  - `dispatch_cancel()` (~2041) and the finish path in `dispatch_tick()` (~2540-2566): read `!batch->user_acted` into a local before `cmd_batch_remove()` and pass it.
  - Set `batch->user_acted = 1` before `dispatch_start()` in the `IDC_CMD_APPROVE_BASE` (~4023), approve-all (~4083) and `IDC_CMD_APPROVE_SEL` (~4125) paths, and on the running batch before `dispatch_cancel(d, "[command queue stopped]", 1)` in `IDC_CHAT_SEND` (~3810). `IDC_ACTIVITY_RETRY` (~4164) is left alone.
- [ ] **Step 2: Pause the fourth round.** In the active reply path (~4687), seed the new batch from a local `ApprovalQueue` (as the background path does at ~4510) whose policy is `ai_rounds_batch_policy(d->active_state, d->approval_q.policy)`. When `ai_rounds_paused()` is true, append a `CHAT_ITEM_STATUS` row before the command items: `"[Auto-approve paused after %d automatic rounds \xE2\x80\x94 Run the card to go on]"` with `AI_AUTO_ROUNDS_MAX`. The session's policy and the Auto-approve button do not change.
- [ ] **Step 3: Run `make test`** (expected: pass, warnings 7) **and the cross-compile check.**
- [ ] **Step 4: Commit** (`feat(ai): pause Auto-approve after three unattended rounds`).

### Task 12: quoted markers in the AI panel

**Files:**
- Modify: `src/ui/ai_chat.c`, `src/ui/chat_listview.c`

**Interfaces:**
- Consumes: `ai_extract_commands_full` (Task 5); `chat_approval_add_quoted`, `chat_msg_set_quoted`, `chat_card_header_text`, `ApprovalEntry.quoted` (Task 6).

- [ ] **Step 1: Extraction.** Both reply paths (background ~4501, active ~4605) call `ai_extract_commands_full(..., quoted, 16, ...)`. Quoted entries go in with `chat_approval_add_quoted()`; live ones as today. Active path: each quoted item gets `chat_msg_set_quoted(cmd_item, 1)`. The policy-blocked note (~4738) counts only entries with `!quoted`.
- [ ] **Step 2: Tell the model.** When the reply had N quoted markers, add a user-role note in both paths, next to the rejected notes (~4656, ~4545): `"NOTE: %d [EXEC] marker(s) were inside a code block or inline code and were NOT run. Put each marker on its own line, outside any code fence."`
- [ ] **Step 3: Keep quoted rows quoted.** `append_batch_command_items()` (~1172) sets `quoted` from the entry; the unblock walk (~1026-1033) skips items with `u.cmd.quoted`.
- [ ] **Step 4: Card.** In `chat_listview.c` (~2097-2104) count `quoted_count` separately and keep quoted rows out of `held_count` (they stay in `held_arr` for the disabled look); the header (~2176-2210) uses `chat_card_header_text()` into a 64-byte buffer, and the " — above the allowed ceiling" reason still shows only when `held_count > 0`.
- [ ] **Step 5: Run `make test`** (expected: pass, warnings 7) **and the cross-compile check.**
- [ ] **Step 6: Commit** (`feat(ai): show quoted [EXEC] markers as "quoted, not run"`).

### Task 13: `web_fetch` rows in the chat

**Files:**
- Modify: `src/ui/ai_chat.c`

**Interfaces:**
- Consumes: `WebFetchContext.on_hop`/`user`, `web_fetch_describe` (Task 3).

- [ ] **Step 1: Implement** `static void fetch_on_hop(void *user, const char *display_url, int refused)` (`user` is the `AiStream *`), posting `post_tool_msg(s, CHAT_ITEM_TOOL_CALL, ...)` with `"fetching: %s"` or `"fetch refused: %s (local or private address)"`. After `ai_stream_copy_tools()` in `launch_stream_thread()` (~1417) set `s->fetch.on_hop = fetch_on_hop; s->fetch.user = s;`. In `chat_rebuild_display()` (~1236) and the live row (~808-821), a `web_fetch` call shows `"using tool: web_fetch - <url>"` from `web_fetch_describe(call->input_json, ...)`, as `read_debug_log` does with `ai_debug_tool_describe`. Lifetimes stay as spec §2 "Rows in the chat" describes (dropped for a background session by the existing `WM_AI_TOOL_MSG` handler).
- [ ] **Step 2: Run `make test`** (expected: pass, warnings 7) **and the cross-compile check.**
- [ ] **Step 3: Commit** (`feat(ai): one chat row per web_fetch hop`).

### Task 14: SSH timeout messages in the connect thread

**Files:**
- Modify: `src/ui/window.c` (`connection_thread()` ~1547-1807)

**Interfaces:**
- Consumes: `ssh_session_timed_out`, `ssh_session_set_timeout`, `ssh_setup_timeout_text`, `SSH_SETUP_TIMEOUT_MS` (Task 8).

- [ ] **Step 1: Implement.** Auth failure (~1789): when `ssh_session_timed_out(j->ssh)`, `j->error` gets `ssh_setup_timeout_text(SSH_STAGE_AUTH, ...)`, else today's `"Authentication failed for %s@%s."`. Channel failure (~1800): same with `SSH_STAGE_SESSION`, else `"Could not open SSH channel to %s."`. After `ssh_pty_request()`/`ssh_pty_shell()` (~1805), whose returns are ignored today, fail with the `SSH_STAGE_SESSION` text and `CONN_FAIL(3)` when `ssh_session_timed_out()`. Immediately before `ssh_session_set_blocking(j->ssh, false)` (~1807) call `ssh_session_set_timeout(j->ssh, 0)`. The handshake text already arrives through `last_error` in the existing `"Cannot connect to %s:%d\n\n%s"` message. Pass `SSH_SETUP_TIMEOUT_MS` as `timeout_ms`.
- [ ] **Step 2: Run `make test`** (expected: pass, warnings 7) **and the cross-compile check.**
- [ ] **Step 3: Commit** (`feat(ssh): say which setup stage timed out`).

### Task 15: Settings warning and copy

**Files:**
- Modify: `src/ui/settings.c`

**Interfaces:**
- Consumes: `net_url_cleartext_remote` (Task 1); `ns_tokens()->warning.base`.

- [ ] **Step 1: Copy.** `IDC_AI_WEB_FETCH` tooltip (~464): `"Allow the AI to fetch web pages as a tool call. The assistant can then send text from this session to any web address."` `IDC_DBG_ENABLE` tooltip (~419) appends `" When AI Assist reads the log, the text it reads is sent to your AI provider."` The checkbox label and the debug hint stay.
- [ ] **Step 2: Warning row.** A one-line static directly under the Base URL row (~1391-1396), added with `add_ctrl(nd, NULL, h, NULL, SETTINGS_PAGE_AI_PROVIDER, ...)` like `hCtxHint`, text `"Not encrypted: your API key and terminal text cross the network in plain text."`, stored in a new `HWND hUrlWarn` and `SettingsCtrl *ctrl_ai_url_warn`. A `static void ai_url_warn_update(SettingsDlgData *d)` sets `ctrl_ai_url_warn->visible` to "provider is `custom` and `net_url_cleartext_remote(<edit text>)`", then relayouts. Call it after page creation, on `IDC_AI_PROVIDER_COMBO` `CBN_SELCHANGE` (~1777) and on a new `IDC_AI_CUSTOM_URL` `EN_CHANGE` case. `WM_CTLCOLORSTATIC` (~793) gives `hUrlWarn` `theme_cr(ns_tokens()->warning.base)`. No message box; saving is not blocked; the URL is stored as typed.
- [ ] **Step 3: Run `make test`** (expected: pass, warnings 7; `test_ui_tokens` still green) **and the cross-compile check.**
- [ ] **Step 4: Commit** (`feat(settings): warn on a clear-text custom endpoint; say what leaves the machine`).

### Task 16: test hygiene and zero warnings

**Files:**
- Modify: `tests/test_cmd_batch.c` (~93-95), `tests/test_ui_demo.c`, `tests/test_ui_tokens.c` (~451, ~467, ~676 comments), `tests/test_session_manager.c` (~67), `tests/test_string_utils.c` (CRLF, ~396), `tests/test_cmd_collapse.c` (~42), `src/crypto/crypto.c` (CRLF, line 2)

- [ ] **Step 1: Fix** per spec §9: save `b->id` in a local before `cmd_batch_remove()`; every `tests/test_ui_demo.c` test that calls `ui_demo_build()` calls `ai_conv_reset()` on each conversation it filled before reusing it and before returning; reword `src/ui/*.c` in the three comments to `src/ui/<file>.c`; replace the two truncating `snprintf` calls with `int n = snprintf(...); ASSERT_TRUE(n >= (int)sizeof <buf>);` keeping each test's other assertions; `(void)list;` or drop the name for the unused parameter; `crypto.c:2` becomes `#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)`.
- [ ] **Step 2: Run `make test 2>&1 | tee build/t.log`.** Expected: `Tests Run` unchanged from Task 15, `Failed: 0`, `grep -c 'warning:' build/t.log` prints `0`.
- [ ] **Step 3: Cross-compile check** (crypto.c is in the exe).
- [ ] **Step 4: ASan/LeakSanitizer run.** `make test TEST_CC="gcc -fsanitize=address,undefined -fno-omit-frame-pointer" 2>&1 | tee build/asan.log`. Expected: `Failed: 0`, and no ASan or LeakSanitizer report whose stack names `test_cmd_batch.c`, `test_ui_demo.c` or `json_parser.c`. Record any other report in the PR body, unfixed (out of scope).
- [ ] **Step 5: Commit** (`test: fix a use-after-free, demo leaks and seven warnings`).

### Task 17: lint and dead code

**Files:**
- Modify: `src/core/dlog.c` (~289, ~356, ~429, ~436, ~445, ~515, ~1430), `Makefile` (CRLF, lint target ~246), `src/ui/window.c` (~2744), `src/crypto/crypto.c` (CRLF, `PBKDF2_SALT` ~27, `crypto_derive_key` ~110), `src/crypto/crypto.h` (CRLF, ~25-31)

- [ ] **Step 1: Implement** per spec §10: `emit_v()` takes `va_list *ap` (NULL = no arguments); the two variadic callers pass `&ap`; the three no-argument callers pass NULL and drop their `memset`; `// cppcheck-suppress arrayIndexOutOfBoundsCond` with a one-line reason above `dlog.c:289`; the lint line gains `--inline-suppr`; remove the always-false `if (bpm)` and its body in the `else` branch at `window.c` ~2744; delete `crypto_derive_key()`, `PBKDF2_SALT` and their declaration and comment. `crypto_encrypt_with_key()`/`crypto_decrypt_with_key()` stay.
- [ ] **Step 2: Run `make test`.** Expected: pass, 0 warnings.
- [ ] **Step 3: Run `make lint 2>&1 | tee build/lint.log`.** Expected: `grep -c ': error:'` 0 and `grep -c ': warning:'` 0; note the style count (about 77) for Task 18.
- [ ] **Step 4: Cross-compile check.**
- [ ] **Step 5: Commit** (`chore: clean lint errors and remove dead code`).

### Task 18: README

**Files:**
- Modify: `README.md` (LF)

- [ ] **Step 1: Edit** per spec §11: remove the `**Build Date**:` line (L6), keeping the trailing ` \` on `**Version**:`; L46 and the tree line L675 become "N unit tests across M test files" with N from the latest `make test` "Tests Run" and M from `ls tests/test_*.c | wc -l`; if Task 17 left style findings, L46 says "cppcheck reports no errors or warnings" and L748 says "Run `make lint` before committing; it must report no errors or warnings."; add `libssl-dev` to the Linux host list (L705-712); the threat-model paragraph in Security (L329) with the five points of spec §11; the shell-prompt sentence in Command Execution (L240-250) with the spec's exact text; the Command Execution header bullet mentions "· K quoted" rows ("quoted, not run").
- [ ] **Step 2: Run `make test`.** Expected: pass (`test_shortcuts` still matches its README block), 0 warnings.
- [ ] **Step 3: Commit** (`docs(readme): real counts, lint status, Linux deps and a threat model`).

### Task 19: version bump, Windows build and gate checks

**Files:**
- Modify: `src/ui/resource.h` (`APP_VERSION`, `APP_VERSION_BINARY`), `README.md` (`**Version**:`, and refresh the counts from Step 3 if they moved), `build/win/nutshell.exe`

- [ ] **Step 1: Work out the version.** `git fetch origin main`; take the higher of `git show origin/main:src/ui/resource.h | grep APP_VERSION` and this branch's `APP_VERSION`; add one patch (after `.99` comes `X.(Y+1).0`). Write it in both `resource.h` macros and README's `**Version**:` line. Confirm `hostname` prints `automaton`.
- [ ] **Step 2: Build.** `make clean && make release`. Expected: success, UPX output. If it fails, `git restore build/win/nutshell.exe` before anything else.
- [ ] **Step 3: Tests.** `make test 2>&1 | tee build/t.log`: `Failed: 0`, `grep -c 'warning:' build/t.log` 0; note the count. Then `xvfb-run -a make wintest` (if Wine reports no driver: `pgrep -a wine` first, then `wineserver -k` only if no other session's run shows).
- [ ] **Step 4: Gate script.** `bash .github/scripts/check-version.sh origin/main`. Expected: passes (version bumped, three strings agree, exe in the diff with the same FileVersion).
- [ ] **Step 5: Checklist before committing.**
  - `git diff origin/main --stat` and `git diff origin/main --stat --ignore-cr-at-eol` show the same counts for every file; every CRLF file in Global Constraints is still CRLF (`git ls-files --eol <file>` shows `i/crlf`).
  - `make lint` still 0 errors, 0 warnings.
  - `git status --short`: read every `??` line; nothing stray.
  - `grep -rn 'RGB(' src/ui --include=*.c | grep -v ns_draw.c` unchanged from `main`.
- [ ] **Step 6: Commit** `build: vX.Y.Z` staging `src/ui/resource.h README.md build/win/nutshell.exe` by path, with the trailer block. Do not push, mark ready or merge; the maintainer runs the Windows manual checks in spec "Verification before the gate" (web_fetch refusals and redirect rows, `nc -l` SSH timeout, Settings warning, `--ui-demo=all` on four themes, the harness `-Tier gate` cases).
