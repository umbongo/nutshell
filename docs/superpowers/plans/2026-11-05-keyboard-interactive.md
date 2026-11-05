# Keyboard-Interactive Authentication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ask the SSH server which methods it offers, try publickey, password and keyboard-interactive in the spec's fixed order, show every server prompt to the user, and never hand a stored secret to the wrong prompt.

**Architecture:** Every decision is a pure function in `src/core/` with native tests: the method-list parser and the step plan (`ssh_auth_plan`), the password-like matcher and the sanitiser, the keyboard-interactive round with injected ask/clock/abort/set-timeout hooks (`ssh_kbd`), the re-arm arithmetic (`ssh_timeout`), the prompt kinds and the save rule (`conn_prompt`), the dialog layout (`pw_prompt_layout`). `src/term/ssh_session.c` is a thin libssh2 adapter; `src/ui/window.c` drives the plan; `src/ui/pw_prompt.c` shows the prompt.

**Tech Stack:** C11, libssh2 1.11.x (1.11.0 in hosted CI, 1.11.1 from vcpkg in the MinGW build), MinGW-w64 cross-compile, Win32, native gcc test runner (`tests/test_framework.h`), Python 3 + paramiko for the manual server.

**Spec:** `docs/superpowers/specs/2026-11-05-keyboard-interactive-design.md`. Read it with this plan; where a step says "spec §N" the wording and values there are binding.

## Global Constraints

- Worktree `~/nutshell-wt/kbdint`, branch `feat/keyboard-interactive`, based on `main` **after the trust-boundary PRs (A and B) have merged**. Host `automaton` only (`hostname`). All rules in `CLAUDE.md` apply. Line numbers below are at `1f85a3a`; find each site by name.
- TDD: tests first for everything in `src/core/`. Each new test function is declared and called in `tests/runner.c`.
- **Line endings, per file.** Check `git ls-files --eol <file>` before editing each file and keep what it shows. At `1f85a3a`: CRLF `Makefile` (not edited) and `tests/test_key_auth.c`; LF every other touched file. New files are LF. Before each commit, `git diff --cached --stat` and `git diff --cached --stat --ignore-cr-at-eol` print the same numbers.
- `-Werror` under both compilers (`-Wconversion`, `-Wshadow`, `-Wpedantic`): `long` is 32-bit on Windows; cast `size_t` explicitly.
- **Build check.** `src/term` and `src/ui` compile only in hosted CI and in `make release`. Every task that touches `src/term/`, `src/ui/` or `src/core/ssh_kbd.*` ends with: `T=$(mktemp -d); make clean && make release 2>&1 | tee "$T/build.log"; rc=${PIPESTATUS[0]}; git restore build/win/nutshell.exe; echo rc=$rc; grep -cE 'warning:|error:' "$T/build.log"`. Expected `rc=0` and `0`. The exe is restored every time; it is committed only in Task 11. Tasks 1, 2 and 4's core files reach MinGW in the next build check.
- **One version bump**, at the start of Task 3 (the first build check): `git fetch origin`; `V` = patch + 1 over the greater of `git show origin/main:src/ui/resource.h` and the working tree's `APP_VERSION`; after `.99` comes the next minor. Write `V` into `APP_VERSION`, `APP_VERSION_BINARY` (`X,Y,Z,0`) and README's `**Version**: vX.Y.Z`. Task 11 re-checks it against `origin/main` and bumps again only if `main` has moved past it. Never write a literal version in advance.
- Secrets: every buffer that held a password, passphrase or typed answer is wiped with `secure_zero()`. No answer is logged; prompt texts only through `DLOG_CONTENT` at DEBUG. `tests/test_dlog_gate.c` and `tests/test_ui_tokens.c` pass unchanged.
- `#include <winsock2.h>` before `<windows.h>` in any file needing both. Tests that write files use `test_fopen_private()` and `TEST_TMP_DIR` (none here needs one).
- **Commits.** One per task. Stage new files by path and read every `??` line of `git status --short`; never `git add -u` alone. Every commit message ends with exactly:

```
Models: claude-opus-5-5 (orchestration, review); claude-sonnet-5-5 (implementation, task review)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg
```

## Review Focus

1. **The saved password reaching a prompt that only looks like a password prompt.** "Password (new):", "Passcode or password:", "Enter PIN password:", a Duo menu whose last line is "Passcode or option (1-3):". Each must go to the user. Pinned by Task 2's `test_ssh_kbd_matcher_lookalikes`.
2. **The saved password sent twice in one connection**: after `PASSWORD_SAVED` was refused, then in `KBDINT`; or in `KBDINT_RETRY`. Pinned by Task 1's `test_ssh_auth_saved_for` and Task 4's `test_ssh_kbd_saved_once_across_rounds`.
3. **The password box, or a re-query, on a dead session**: a -18 whose last errno is a socket error, a -7/-43 return, a failed re-query. Pinned by Task 1's `test_ssh_auth_may_continue` and `test_ssh_auth_requery_rule`.
4. **The timeout left re-armed, or 60 s granted with no human in the loop**: a server that loops on zero-prompt rounds, a round whose prompts were all emptied, the test override (an auto-answered round re-arms to 60 s since the maintainer's ruling of 2026-11-05: the saved password answers at most once per connection). Pinned by Task 4's `test_ssh_kbd_rearm_limits` and `test_ssh_kbd_attempt_restores_override`.
5. **Hostile or oversized server text**: ESC and OSC sequences, TABs, `&`, bare LF in the EDIT, a 5,000-byte instruction, a 2,049-byte prompt whose label is its last line. Pinned by Task 2's sanitiser tests, Task 4's `test_ssh_kbd_prompt_boundaries` and Task 5's `test_pw_text_crlf`.

---

### Task 1: method-list parser and the step plan

**Files:**
- Create: `src/core/ssh_auth_plan.h`, `src/core/ssh_auth_plan.c` (no `windows.h`, no libssh2)
- Create: `tests/test_ssh_auth_plan.c`; Modify: `tests/runner.c`

**Interfaces:**
- Produces:

```c
#define SSH_AM_PUBLICKEY 0x1u
#define SSH_AM_PASSWORD  0x2u
#define SSH_AM_KBDINT    0x4u
#define SSH_AM_OTHER     0x8u
#define SSH_AUTH_DISPLAY_MAX  128
#define SSH_AUTH_MAX_ATTEMPTS 4
#define SSH_RC_AUTH_FAILED    (-18)   /* LIBSSH2_ERROR_AUTHENTICATION_FAILED */
#define SSH_RC_PK_UNVERIFIED  (-19)   /* LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED */
unsigned ssh_auth_methods_parse(const char *list, char *display, size_t cap);
typedef enum { SSH_STEP_DONE = 0, SSH_STEP_PUBLICKEY, SSH_STEP_KBDINT,
               SSH_STEP_PASSWORD_SAVED, SSH_STEP_KBDINT_RETRY, SSH_STEP_PASSWORD_ASK } SshAuthStep;
typedef struct {
    int is_key, has_saved;     /* has_saved: password profile with a non-empty saved password */
    unsigned offered;          /* SSH_AM_* */
    unsigned tried;            /* 1u << step for each step run */
    int saved_used;            /* the saved password has been sent */
    int kbd_sent_saved;        /* the KBDINT attempt sent it */
    int attempts, requeried;
} SshAuthPlan;
void ssh_auth_plan_init(SshAuthPlan *p, int is_key, const char *saved, unsigned offered);
SshAuthStep ssh_auth_next(const SshAuthPlan *p);
void ssh_auth_plan_record(SshAuthPlan *p, SshAuthStep step, int saved_sent);
void ssh_auth_plan_count_retry(SshAuthPlan *p);           /* the passphrase retry */
const char *ssh_auth_saved_for(const SshAuthPlan *p, SshAuthStep step, const char *saved);
int ssh_auth_transport_dead(int last_errno);              /* -7, -13, -43 */
int ssh_auth_may_continue(int rc, int timed_out, int last_errno);
int ssh_auth_should_requery(const SshAuthPlan *p, int rc, int timed_out, int last_errno);
void ssh_auth_plan_requeried(SshAuthPlan *p, unsigned offered);
size_t ssh_auth_fail_text(const char *user, const char *host, int is_key, unsigned offered,
                          const char *display, const char *extra, char *buf, size_t cap);
```

- [ ] **Step 1: Write the failing tests** in `tests/test_ssh_auth_plan.c`. Helper `static void walk(SshAuthPlan *p, SshAuthStep *out, int max)` calls `ssh_auth_next`, records each step with `saved_sent = (step == PASSWORD_SAVED || (step == KBDINT && <row's flag>))`, as if every attempt returned -18, until `DONE`.

```c
int test_ssh_auth_parse(void);
/* NULL -> 0, display ""; "" -> 0; "publickey" -> PK, "publickey";
   "publickey,password,keyboard-interactive" -> PK|PW|KBD, "publickey, password, keyboard-interactive";
   " password , ,password,," -> PW, "password"; "hostbased,gssapi-with-mic" -> OTHER, "hostbased, gssapi-with-mic";
   "pass\x1bword" -> OTHER, display has no 0x1b; a 300-byte list -> strlen(display) <= 127, cap 8 -> NUL-terminated */
int test_ssh_auth_sequence_table(void);
/* every row walks to DONE; cell = step sequence:
   key  {pk}        PUBLICKEY
   key  {pk,kbd}    PUBLICKEY, KBDINT
   key  {pk,pw,kbd} PUBLICKEY, KBDINT
   key  {kbd}       KBDINT
   key  {pw}        (none)
   pw+saved {pw,kbd}    PASSWORD_SAVED, KBDINT, PASSWORD_ASK      (order rule: password first)
   pw+saved {pk,pw,kbd} PASSWORD_SAVED, KBDINT, PASSWORD_ASK
   pw+saved {pw}        PASSWORD_SAVED, PASSWORD_ASK              (asked once more)
   pw+saved {kbd}, KBDINT sent saved      KBDINT, KBDINT_RETRY
   pw+saved {kbd}, KBDINT did not send it KBDINT
   pw none  {pw,kbd}    KBDINT, PASSWORD_ASK                      (keyboard-interactive first)
   pw none  {pw}        PASSWORD_ASK
   pw none  {kbd}       KBDINT
   pw ""    {pw}        PASSWORD_ASK                              (empty saved = none)
   any {}, {other}, pw {pk}   (none) */
int test_ssh_auth_attempt_cap(void);
/* p.attempts = 4 -> DONE for every profile; key {pk,kbd}: record PUBLICKEY, count_retry,
   attempts 2, next KBDINT, record -> attempts 3 */
int test_ssh_auth_saved_for(void);
/* pw+saved {pw,kbd}: PASSWORD_SAVED -> saved; after recording it, KBDINT -> NULL;
   pw+saved {kbd}: KBDINT -> saved, KBDINT_RETRY -> NULL, PASSWORD_ASK -> NULL;
   key profile, any step -> NULL; saved "" -> NULL */
int test_ssh_auth_transport_dead(void);   /* -7,-13,-43 -> 1; 0,-9,-16,-18,-19,-48 -> 0 */
int test_ssh_auth_may_continue(void);
/* (-18,0,-18) 1; (-19,0,-19) 1; (-18,1,-9) 0; (-18,0,-43) 0; (-18,0,-7) 0;
   (-16,0,-16) 0; (-48,..) 0; (-7,..) 0; (-1,..) 0; (0,..) 0 */
int test_ssh_auth_requery_rule(void);
/* key {pk}: record PUBLICKEY; should_requery(p,-19,0,-19) 1; requeried(p, KBD) -> next KBDINT;
   record; should_requery(p,-18,0,-18) 0 (once only);
   key {pk,kbd} after PUBLICKEY: should_requery 0 (KBDINT still left);
   timed out, dead transport, rc -16, attempts 4 -> 0.
   Amended (maintainer, 2026-11-05), a refused saved password re-queries at once:
   pw+saved {pw}: PASSWORD_SAVED -18 -> should_requery 1; requeried({pw}) -> PASSWORD_ASK;
   requeried({kbd}) -> KBDINT with saved_for NULL (partial success);
   pw+saved {pw,kbd}: PASSWORD_SAVED -18 -> should_requery 1; requeried({pw,kbd}) -> KBDINT,
   then PASSWORD_ASK, and a later DONE does not re-query again;
   timed out, dead transport, attempts 4 after PASSWORD_SAVED -> 0 */
int test_ssh_auth_saved_not_after_used(void);
/* Amended (maintainer, 2026-11-05): KBDINT sent the saved password, requeried({pw}) or
   ({pw,kbd}) -> next is PASSWORD_ASK, never PASSWORD_SAVED; KBDINT that sent nothing,
   requeried({pw}) -> PASSWORD_SAVED */
int test_ssh_auth_fail_text(void);
/* ("alice","host",0,PK|KBD,"publickey, keyboard-interactive",NULL) ->
   "Authentication failed for alice@host. The server offers: publickey, keyboard-interactive."
   key profile, offered PW, "password" -> "... The server does not offer key authentication; it offers: password."
   extra "Too many authentication prompts." -> appended after one space; display "" -> no "offers" clause;
   cap 16 -> 0 and buf "" */
```

- [ ] **Step 2: Run `make test`.** Expected: link failure, `ssh_auth_methods_parse` undefined.
- [ ] **Step 3: Implement** in `src/core/ssh_auth_plan.c`. Parse: split on `,`, trim spaces, skip empty and duplicate items; known names `publickey`, `password`, `keyboard-interactive`; anything else sets `SSH_AM_OTHER`; display names are printable ASCII only (other bytes dropped), joined with ", ", cut on an item boundary at `cap - 1`. `ssh_auth_next` evaluates spec §2's table top to bottom for the profile type, skipping steps in `tried`, and returns `DONE` once `attempts >= SSH_AUTH_MAX_ATTEMPTS`. `record` sets the step's bit, `attempts++`, and `saved_used`/`kbd_sent_saved` from `saved_sent`. `may_continue` is 1 only for rc -18/-19 with `!timed_out && !transport_dead(last_errno)`. `should_requery` is `may_continue && !requeried && attempts < MAX && (next == DONE || last == PASSWORD_SAVED)` (amended, maintainer 2026-11-05: `record` also stores `last`, and the PASSWORD_SAVED condition gains `&& !saved_used`). `requeried` replaces `offered` and sets `requeried = 1`.
- [ ] **Step 4: Run `make test`.** Expected: all pass, `Failed: 0`, no new warning.
- [ ] **Step 5: Commit** (`feat(core): SSH auth method parser and step plan`).

### Task 2: the password-like matcher and the sanitiser

**Files:**
- Modify: `src/core/ssh_auth_plan.h`, `src/core/ssh_auth_plan.c`, `tests/test_ssh_auth_plan.c`, `tests/runner.c`

**Interfaces:**
- Produces:

```c
int ssh_kbd_prompt_is_password(const char *text);
size_t ssh_kbd_sanitize(const char *in, size_t len, char *out, size_t cap, int single_line);
```

`in` need not be NUL-terminated; NULL with `len == 0` gives `""`. Returns the length written; `out` is always NUL-terminated when `cap > 0`.

- [ ] **Step 1: Write the failing tests.**

```c
int test_ssh_kbd_matcher_positive(void);
/* "Password:", "password", "  PASSWORD:  ", "user@host's password:",
   "alice@NEWCASTLE.AC.UK's Password:", "SPINNAKER password:", "Password for alice:",
   "LDAP Password:", "Password for pin:" -> 1 */
int test_ssh_kbd_matcher_negative(void);
/* "PASSCODE:", "One-time password:", "New password:", "Retype new password:",
   "Verification code:", "OTP password:", "Token password:", "PIN password:",
   "Password expired, enter code:", "Username:", "", NULL, "passwords:", "xpassword:" -> 0 */
int test_ssh_kbd_matcher_lookalikes(void);
/* "Password (new):", "Passcode or password:", "Enter PIN password:",
   "Passcode or option (1-3):", "Password for new account:" -> 0 */
int test_ssh_kbd_sanitize_escapes(void);
/* "a\x1b[31mb\x1b[0m" -> "ab"; "a\x1b]0;title\x07" "b" -> "ab"; "a\x1b]0;t\x1b\\b" -> "ab";
   "a\x1b" "Zb" -> "ab"; lone trailing ESC dropped; "\x01a\x7f" -> "a" */
int test_ssh_kbd_sanitize_lines(void);
/* "a\r\nb\rc\n" -> "a\nb\nc"; "\n\n a  \n" -> " a"; "a\tb" -> "a b";
   single_line: "a\nb" -> "a b" */
int test_ssh_kbd_sanitize_utf8_and_cap(void);
/* "\xC3\xA9" kept; "\xC3(" -> "?("; "\xFF" -> "?";
   5000 x 'a', cap 2048 -> length 2047, ends "..."; a cut that falls inside "\xE2\x82\xAC"
   leaves no partial sequence before the "..." */
```

- [ ] **Step 2: Run `make test`.** Expected: link failure on `ssh_kbd_prompt_is_password`.
- [ ] **Step 3: Implement.** The matcher follows spec §4 exactly: ASCII case-fold, trim, drop trailing `:` and spaces; strip a leading `<token>@<token>'s ` and a trailing ` for <token>` (token = run of non-space bytes); the rest must end with `password` preceded by its start or a non-alphanumeric byte; none of `passcode`, `one-time`, `otp`, `new`, `retype`, `verification`, `token`, `pin`, `code` may occur in the rest bounded on both sides by an edge or a non-alphanumeric byte (`-` counts as non-alphanumeric for the boundary, so `one-time` is matched literally). The sanitiser follows spec §5; the marker is ASCII `...`.
- [ ] **Step 4: Run `make test`.** Expected: all pass, no new warning.
- [ ] **Step 5: Commit** (`feat(core): password-prompt matcher and server-text sanitiser`).

### Task 3: prompt kinds and the save rule in `conn_prompt.h`; the version bump

**Files:**
- Modify: `src/core/conn_prompt.h`, `src/core/conn_prompt.c`, `tests/test_conn_prompt.c`, `tests/runner.c`
- Modify: `src/ui/window.c` (delete the `ConnUiKind` typedef at ~309 only; `conn_prompt.h` is already included)
- Modify: `src/ui/resource.h`, `README.md` (`**Version**:` only)

**Interfaces:**
- Produces:

```c
typedef enum { CONN_UI_HOSTKEY, CONN_UI_PASSPHRASE, CONN_UI_PASSWORD, CONN_UI_KBDINT } ConnUiKind;
int conn_ui_kind_has_secret(ConnUiKind kind);   /* PASSPHRASE, PASSWORD, KBDINT -> 1 */
#define CONN_SAVE_MAX 256
typedef struct { char pw[CONN_SAVE_MAX]; int ticked; } ConnSaveCandidate;
/* The latest typed password-like answer wins (spec §2): wipes what was kept, then keeps
 * `typed` only when `ticked`. NULL-safe. */
void conn_save_note(ConnSaveCandidate *c, const char *typed, int ticked);
void conn_save_clear(ConnSaveCandidate *c);
```

- [ ] **Step 1: Bump the version** per Global Constraints.
- [ ] **Step 2: Write the failing tests** in `tests/test_conn_prompt.c`:

```c
int test_conn_ui_kind_has_secret(void);   /* HOSTKEY 0; PASSPHRASE, PASSWORD, KBDINT 1 */
int test_conn_save_note_latest_wins(void);
/* note("one",1) -> pw "one", ticked 1; note("two",0) -> pw all zero bytes, ticked 0;
   note("three",1) -> "three"; a 300-byte typed -> 255 bytes kept, NUL at [255];
   clear -> every byte 0; NULL c and NULL typed safe */
```

- [ ] **Step 3: Run `make test`.** Expected: compile failure (`CONN_UI_KBDINT` unknown).
- [ ] **Step 4: Implement**, and delete the enum from `window.c` (the three existing `kind ==` tests there stay as they are until Task 7).
- [ ] **Step 5: Run `make test`** (all pass) **and the build check** (`rc=0`, `0`).
- [ ] **Step 6: Commit** (`feat(core): prompt kinds and save rule in conn_prompt; bump to vX.Y.Z`), staging `resource.h` and `README.md` with the rest.

### Task 4: the keyboard-interactive round, the attempt wrapper and the re-arm

**Files:**
- Create: `src/core/ssh_kbd.h`, `src/core/ssh_kbd.c`, `tests/test_ssh_kbd.c`
- Modify: `src/core/ssh_timeout.h`, `src/core/ssh_timeout.c`, `tests/test_ssh_timeout.c`, `tests/stubs/libssh2.h`, `tests/runner.c`

**Interfaces:**
- Consumes: `ssh_kbd_prompt_is_password`, `ssh_kbd_sanitize` (Task 2); `ConnSaveCandidate`, `conn_save_note` (Task 3); `dlog_secret_add/remove`, `DLOG`, `DLOG_CONTENT` (`dlog.h`).
- Produces (`ssh_timeout.h`): `#define SSH_KBD_ANSWER_WAIT_MS 60000L`; `long ssh_kbd_rearm_ms(long long entry_s, long long now_s, long limit_ms);` = `(now − entry) * 1000 + limit_ms + 1000`, elapsed clamped at 0 when the clock goes backwards, saturating at `LONG_MAX`.
- Produces (stub): `LIBSSH2_USERAUTH_KBDINT_PROMPT`, `LIBSSH2_USERAUTH_KBDINT_RESPONSE` and `LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC(name_)` copied from libssh2 1.11.1 `include/libssh2.h` 285-296 and 314-318 (`unsigned char *text; size_t length; unsigned char echo;` / `char *text; unsigned int length;`).
- Produces (`ssh_kbd.h`, which includes `<libssh2.h>` and `conn_prompt.h`):

```c
typedef LIBSSH2_USERAUTH_KBDINT_PROMPT   SshKbdPromptIn;
typedef LIBSSH2_USERAUTH_KBDINT_RESPONSE SshKbdResponseOut;
#define SSH_KBD_MAX_PROMPTS 8
#define SSH_KBD_MAX_ROUNDS  16
#define SSH_KBD_NAME_MAX    96
#define SSH_KBD_TEXT_MAX    2048     /* instruction and text area */
#define SSH_KBD_PROMPT_MAX  2048     /* one prompt; tail kept on overflow */
#define SSH_KBD_ANSWER_MAX  256      /* ConnUiRequest.passphrase */
typedef struct {
    const char *name;    /* sanitised, single line, "" when none */
    const char *text;    /* zero-prompt instruction + instruction + prompt lines but the last; "" when none */
    const char *label;   /* the prompt's last line, single line */
    int echo, offer_save;
} SshKbdAsk;
typedef struct SshKbdCtx {
    /* set by window.c */
    int  (*ask)(void *ui_user, const SshKbdAsk *q, char *answer, size_t cap, int *save);
    void *ui_user;
    const char *saved;        /* ssh_auth_saved_for(); NULL = no auto-answer */
    int  is_key;
    long setup_limit_ms;      /* ssh_setup_timeout_ms() */
    ConnSaveCandidate *save;  /* may be NULL */
    unsigned log_id;
    /* set by ssh_auth_kbdint() */
    void (*set_timeout)(void *io_user, long ms);
    void (*abort_io)(void *io_user);
    long long (*now)(void *io_user);   /* seconds, like time() */
    void *io_user;
    /* per attempt; reset by ssh_kbd_attempt() */
    long long entry;
    long limit_in_force_ms;   /* SSH_KBD_ANSWER_WAIT_MS after an answered round, else setup_limit_ms */
    int  rounds, prompts, auto_answered, user_answered, saved_sent, cancelled, too_many, aborted;
    char zero_instruction[SSH_KBD_TEXT_MAX];   /* last zero-prompt round's text not yet shown */
    char answer[SSH_KBD_ANSWER_MAX];           /* the buffer handed to ask; zero when a round returns */
} SshKbdCtx;
void ssh_kbd_round(SshKbdCtx *ctx, const char *name, int name_len, const char *instruction,
                   int instruction_len, int num_prompts, const SshKbdPromptIn *prompts,
                   SshKbdResponseOut *responses);
int  ssh_kbd_attempt(void **slot, SshKbdCtx *ctx, int (*call)(void *arg), void *arg);
```

- [ ] **Step 1: Write the failing tests.** In `tests/test_ssh_kbd.c` a fake records every `ask` (copies of name, text, label, echo, offer_save), every `set_timeout` value, the `abort_io` count, and serves scripted answers, ticks and a cancel; the clock returns a settable value. Prompts are built from `unsigned char` arrays with no NUL and trailing garbage beyond `length`.

```c
/* tests/test_ssh_timeout.c */
int test_ssh_kbd_rearm_ms(void);
/* (100,100,60000) 61000; (100,130,60000) 91000; (100,90,15000) 16000 (backwards clock);
   (0, 1LL<<40, 60000) LONG_MAX */
/* tests/test_ssh_kbd.c */
int test_ssh_kbd_copies_by_length(void);
/* prompt "Password:XXXX" length 9 -> label "Password:"; name/instruction likewise;
   NULL name, instruction and prompt text with length 0 -> "" and no crash;
   responses[i].text malloc'd, .length == strlen(answer), test frees each */
int test_ssh_kbd_prompt_boundaries(void);
/* 2047-byte prompt ending "\nPassword:" -> not truncated, label "Password:", auto-answered;
   2048 and 2049 bytes -> truncated, text starts "...", label still "Password:", NOT auto-answered
   (asked, offer_save 0) */
int test_ssh_kbd_saved_answers_one_prompt(void);
/* saved "s3cret": round {"Password:" echo 0} -> response "s3cret", asks 0, saved_sent 1;
   round {"Password:", "Password:"} -> first auto, second asked */
int test_ssh_kbd_saved_once_across_rounds(void);
/* round 1 {"Password:"} auto; round 2 {"Password:"} asked; ctx.saved NULL -> always asked */
int test_ssh_kbd_never_auto(void);
/* echo 1 "Password:", "PASSCODE:", "Verification code:", is_key 1 with saved NULL -> all asked */
int test_ssh_kbd_offer_save(void);
/* password profile, "Password:" asked -> offer_save 1; echo prompt, "PASSCODE:", truncated
   password prompt, is_key 1 -> offer_save 0; answers "a" ticked then "b" unticked ->
   save->ticked 0, pw zeroed; "c" ticked last -> "c" */
int test_ssh_kbd_zero_prompt_round(void);
/* round(0 prompts, instruction "Use Duo") -> asks 0, zero_instruction "Use Duo";
   next round's ask text starts "Use Duo"; zero_instruction "" afterwards */
int test_ssh_kbd_too_many_prompts(void);
/* 100 prompts -> 8 asked, responses 8..99 length 0 (each non-NULL malloc), too_many 1 */
int test_ssh_kbd_round_cap(void);
/* 16 rounds normal; round 17 -> asks 0, all answers empty, aborted 1, abort_io called once */
int test_ssh_kbd_cancel(void);
/* 3 prompts, ask returns 0 at the 2nd -> 3rd not asked, responses 2 and 3 empty,
   cancelled 1, abort_io once; a later round asks nothing */
int test_ssh_kbd_rearm_limits(void);
/* entry 100, now 130: answered round -> set_timeout(91000), limit_in_force 60000;
   zero-prompt round -> set_timeout(30000 + 15000 + 1000), limit_in_force 15000;
   all-auto-answered round -> (now-entry)*1000 + 61000, limit_in_force 60000 (amended,
   maintainer 2026-11-05: any answered round, typed or from the saved password, re-arms to
   60 s); a round after a cancel (nothing answered) -> the setup limit;
   setup_limit_ms 300 -> 31300 */
int test_ssh_kbd_attempt_restores_override(void);
/* slot NULL before; call fake checks *slot == ctx; after: *slot NULL, last set_timeout ==
   setup_limit_ms (15000, then 300 for an override), entry == now at entry, counters reset,
   return value is call's; NULL slot or ctx -> -1, call not made */
int test_ssh_kbd_wipes_answers(void);
/* the fake answers "SENTINEL" (also with the saved password "SENTINEL2" auto-answering a
   prompt); after the round every byte of ctx.answer is 0, responses still carry the answers,
   and a cancelled round leaves ctx.answer zero too */
int test_ssh_kbd_adapter_signature(void);
/* static LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC(adapter) { ssh_kbd_round(*(SshKbdCtx **)abstract, ...); }
   called with one prompt -> one ask */
```

- [ ] **Step 2: Run `make test`.** Expected: compile failure (`ssh_kbd.h` missing).
- [ ] **Step 3: Implement** per spec §4 and §6. Per prompt, in order: past `SSH_KBD_MAX_PROMPTS`, after a cancel, or in a capped round → empty answer, no ask; auto-answer when `saved && saved[0] && !saved_sent && !echo && !truncated && ssh_kbd_prompt_is_password(label)`; otherwise `ask` with `offer_save = !is_key && !echo && !truncated && matcher(label)`, and on OK with `offer_save`, `conn_save_note(save, answer, tick)`. A non-echo answer of 3+ bytes is registered with `dlog_secret_add` and removed before the round returns. Every response is a fresh `malloc(len + 1)` (never NULL). The prompt text is logged with `DLOG_CONTENT(DLOG_SSH, DLOG_DEBUG, log_id, "connect.auth.prompt", "text", ...)`. End: `set_timeout(ssh_kbd_rearm_ms(entry, now(), user_answered_this_round ? SSH_KBD_ANSWER_WAIT_MS : setup_limit_ms))`, recording `limit_in_force_ms`; the round-17 path calls `abort_io` instead. `ask` writes into `ctx->answer`; wipe it, and any other local copy, with `secure_zero()` after each prompt and before the round returns.
- [ ] **Step 4: Run `make test`.** Expected: all pass, no new warning.
- [ ] **Step 5: Build check.**
- [ ] **Step 6: Commit** (`feat(core): keyboard-interactive round with injected hooks and re-arm`).

### Task 5: dialog layout in core

**Files:**
- Create: `src/core/pw_prompt_layout.h`, `src/core/pw_prompt_layout.c`, `tests/test_pw_prompt_layout.c`; Modify: `tests/runner.c`
- Modify: `src/ui/pw_prompt.c` (delete `PwLayout` and `pw_layout()` ~40-66; include the new header; both calls pass `has_text = 0`)

**Interfaces:**
- Produces:

```c
typedef struct {
    int pad, gap, ctlh, fw, bw, texth;
    int y_text, y_label, y_detail, y_edit, y_save, y_btn;
    int client_h;
} PwLayout;
/* Rows top to bottom: [text area], label, [detail], field, [save], buttons. */
void pw_layout(int dpi, int has_detail, int has_text, int has_save, PwLayout *L);
/* LF -> CRLF for the multi-line EDIT (an existing CRLF is not doubled). Returns the length
 * written; stops before a pair that would not fit; NUL-terminated when cap > 0. */
size_t pw_text_crlf(const char *in, char *out, size_t cap);
```

- [ ] **Step 1: Write the failing tests.**

```c
int test_pw_layout_today_unchanged(void);
/* has_text 0, at 96 and 192 DPI, each of (detail, save) in {0,1}^2: every field equals the
   value today's formula gives (copy the formula from pw_prompt.c ~48-66 into the test as
   the expected values: pad=ns_scale(SP_MD), gap=ns_scale(SP_SM), ctlh=ns_scale(SZ_CTRL_H),
   fw=ns_scale(264), bw=ns_scale(SZ_BTN_MIN_W)); at 96 DPI, no detail, no save: client_h 128 */
int test_pw_layout_text_row(void);
/* has_text 1: texth == 4 * ctlh, y_text == pad, y_label == pad + texth + gap,
   client_h == the has_text 0 value + texth + gap; at 192 DPI too */
int test_pw_text_crlf(void);
/* "a\nb" -> "a\r\nb"; "a\r\nb" -> "a\r\nb"; "\n\n" -> "\r\n\r\n"; cap 4 with "a\nb" -> "a\r\n"
   or shorter, never a lone "\r"; NULL in -> "" */
```

- [ ] **Step 2: Run `make test`.** Expected: link failure on `pw_layout`.
- [ ] **Step 3: Implement**; move the existing formula unchanged and add the text row (`texth = 4 * ctlh`).
- [ ] **Step 4: Run `make test`** (all pass) **and the build check.**
- [ ] **Step 5: Commit** (`refactor(pw_prompt): move the layout to core and test it`).

### Task 6: libssh2 adapter in `ssh_session.c`

**Files:**
- Modify: `src/term/ssh_session.h`, `src/term/ssh_session.c` (LF)
- Modify: `tests/test_key_auth.c` (CRLF), `tests/runner.c`

**Interfaces:**
- Consumes: `SshKbdCtx`, `ssh_kbd_round`, `ssh_kbd_attempt` (Task 4).
- Produces:
  - `SshSession` gains `void *kbd_ctx;` (NULL outside an attempt)
  - `const char *ssh_auth_list(SshSession *s, const char *user);` NULL when `!s || !s->connected` or libssh2 returns NULL
  - `int ssh_session_authenticated(const SshSession *s);` 0 for NULL
  - `int ssh_session_last_errno(const SshSession *s);` 0 for NULL or no session
  - `int ssh_auth_kbdint(SshSession *s, const char *user, SshKbdCtx *ctx);` -1 when `!s || !s->connected || !ctx`
  - `void ssh_session_set_keepalive(SshSession *s, int interval_s);` NULL-safe; `libssh2_keepalive_config(s->session, 1, interval)`
  - `long ssh_setup_timeout_ms(void);` the override when set, else `SSH_SETUP_TIMEOUT_MS`

- [ ] **Step 1: Write the failing tests** in `tests/test_key_auth.c` (real-libssh2 builds only; runs in hosted `Native tests`), keeping CRLF:

```c
int test_auth_list_null_and_not_connected(void);   /* NULL s -> NULL; new session -> NULL */
int test_auth_kbdint_null_and_not_connected(void); /* NULL s, NULL ctx, new session -> -1; kbd_ctx stays NULL */
int test_session_helpers_null_safe(void);
/* authenticated(NULL) 0; last_errno(NULL) 0; set_keepalive(NULL, 30) no crash;
   ssh_setup_timeout_ms() 15000; override(300) -> 300; override(0) -> 15000 */
```

- [ ] **Step 2: Implement.** `ssh_connect()`: replace `libssh2_keepalive_config(s->session, 1, 30)` with `(1, 0)` (off during setup) and update its comment (spec §6); compute `limit` with `ssh_setup_timeout_ms()`. `ssh_auth_kbdint` sets `ctx->set_timeout` (`libssh2_session_set_timeout`), `ctx->abort_io` (`shutdown(s->socket, SD_BOTH)` on `_WIN32`, `SHUT_RDWR` elsewhere), `ctx->now` (`(long long)time(NULL)`), `ctx->io_user = s`, then returns `ssh_kbd_attempt(&s->kbd_ctx, ctx, call, &args)` where `call` runs `libssh2_userauth_keyboard_interactive_ex(s->session, user, (unsigned)strlen(user), kbd_cb)`. `kbd_cb` is `static LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC(kbd_cb)`: `SshSession *ss = *abstract;` then `ssh_kbd_round((SshKbdCtx *)ss->kbd_ctx, ...)` with every argument passed through (a NULL `kbd_ctx` answers nothing and returns). Leave `cached_passphrase` alone (spec Non-goals).
- [ ] **Step 3: Run `make test`** (the stub build skips `test_key_auth.c`; all pass) **and the build check** (this compiles `ssh_session.c` against vcpkg libssh2 1.11.1).
- [ ] **Step 4: Commit** (`feat(ssh): auth list, keyboard-interactive adapter, keepalive off during setup`).

### Task 7: drive the plan from the connect thread

**Files:**
- Modify: `src/ui/window.c` (`conn_ui_request_cancel` ~356, `conn_ui_ask` ~372-424, `ConnUiRequest` ~311, `conn_setup_timeout_error` ~1558, the auth stage ~1755-1817, the interactive point ~1841)

**Interfaces:**
- Consumes: Tasks 1, 3, 4, 6.
- Produces: `ConnUiRequest` gains `const char *kb_name, *kb_text, *kb_prompt; int kb_echo, kb_offer_save;`; `static int conn_setup_timeout_error(ConnJob *j, SshSetupStage stage, long limit_ms)`; `static int conn_authenticate(ConnJob *j)` returning 0 or a `CONN_FAIL` code with `j->error` set.

- [ ] **Step 1: Prompt plumbing.** Factor `conn_ui_ask()`'s publish/post/wait/copy-out into one static function taking a filled `ConnUiRequest`; `conn_ui_ask()` keeps its signature on top of it, and a new `static int conn_kbd_ask(void *ui_user, const SshKbdAsk *q, char *answer, size_t cap, int *save)` fills `kind = CONN_UI_KBDINT`, the `kb_*` fields and `message` (the same "profile — user@host" detail line the password box gets, built once per connection) and calls it. The three `CONN_UI_PASSPHRASE || CONN_UI_PASSWORD` tests (cancel wipe, `conn_ui_ask` copy-out, `WM_CONN_PROMPT` copy-out ~5009) become `conn_ui_kind_has_secret(kind)`; the save line ~5011 becomes `(req->kind == CONN_UI_PASSWORD || req->kind == CONN_UI_KBDINT) && ans`.
- [ ] **Step 2: Timeout text.** `conn_setup_timeout_error` takes `limit_ms`; the channel, PTY and shell callers pass `ssh_setup_timeout_ms()`.
- [ ] **Step 3: `conn_authenticate()`** replaces the auth block from `int auth_rc = -1;` through the `auth_rc != 0` failure. The loop (an algorithm the interfaces do not fix):

```
list = ssh_auth_list(ssh, user)
if !list: timed_out -> AUTH timeout text (setup limit), 2;  authenticated -> 0;
          else "Could not get the authentication methods from host.", 2
offered = parse(list, display); DLOG connect.auth.methods (bit names only)
plan_init(&p, is_key, info->password, offered); kctx = {ask=conn_kbd_ask, ui_user=j, is_key,
  setup_limit_ms=ssh_setup_timeout_ms(), save=&cand, log_id}; rc = -1; extra = ""
loop:
  (amended, maintainer 2026-11-05) last_errno = peer_closed(j->ssh) ? -13 : last_errno(j->ssh)
  if should_requery(&p, rc, timed_out, last_errno):     (nothing left, or PASSWORD_SAVED just ran)
      list = ssh_auth_list(...); if !list: timed_out -> timeout text, 2; authenticated -> success; else break
      plan_requeried(&p, parse(list, display)); continue
  step = next(&p); if step == DONE break
  extra = ""                                            (only the last attempt's text is kept)
  if CONN_CANCELLED(): "Cancelled.", 2
  PUBLICKEY:      rc = ssh_auth_key(info->password); record(step, 0)
                  rc in {-16,-48}: CONN_UI_PASSPHRASE box; cancel -> "Passphrase entry cancelled.", 4
                     rc = ssh_auth_key(pp); count_retry; rc in {-16,-48} -> "Wrong passphrase for <key_path>.", 2
  PASSWORD_SAVED: rc = ssh_auth_password(saved_for(...)); record(step, 1)
  PASSWORD_ASK:   today's box (detail line as today); cancel -> "Password entry cancelled.", 4
                  rc = ssh_auth_password(pw); conn_save_note(&cand, pw, want_save); record(step, 0)
  KBDINT, KBDINT_RETRY: kctx.saved = saved_for(&p, step, info->password); rc = ssh_auth_kbdint
                  record(step, kctx.saved_sent); keep kctx.zero_instruction / too_many in extra
                  cancelled -> "Authentication cancelled.", 4;  aborted -> "Too many authentication prompts.", 2
  rc == 0: cand.ticked -> copy to j->save_password, save_requested = 1; return 0
  timed_out -> AUTH timeout text with (last step was KBDINT* ? kctx.limit_in_force_ms : setup limit), 2
  !may_continue(rc, timed_out, last_errno): break      /* dead transport: never another prompt */
fail: j->error = ssh_auth_fail_text(user, host, is_key, p.offered, display, extra), 2
```

  `timed_out` is `ssh_session_timed_out(j->ssh)`, `last_errno` is `ssh_session_last_errno(j->ssh)`. After each attempt log `connect.auth.attempt` with `method= result= rounds= prompts= auto_answered= cancelled= aborted=`. Every exit wipes `cand`, `pw` and `pp` with `secure_zero()` and removes any `dlog_secret_add` it made.
- [ ] **Step 4: Interactive point.** Next to `ssh_session_set_timeout(j->ssh, 0)` (~1841) call `ssh_session_set_keepalive(j->ssh, 30)`, with a comment that keepalive was off during setup (spec §6).
- [ ] **Step 5: Run `make test`** (all pass) **and the build check.**
- [ ] **Step 6: Commit** (`feat(ssh): try the offered methods in order, keyboard-interactive included`).

### Task 8: the keyboard-interactive dialog

**Files:**
- Modify: `src/ui/pw_prompt.h`, `src/ui/pw_prompt.c`, `src/ui/window.c` (`WM_CONN_PROMPT` ~4983-5001)

**Interfaces:**
- Consumes: `pw_layout`, `pw_text_crlf` (Task 5); `ConnUiRequest.kb_*` (Task 7).
- Produces: `PwPromptOpts` gains `const char *text;` (NULL = no text area) and `int echo;` (1 = visible field).

- [ ] **Step 1: Dialog.** `pw_layout(dpi, detail != NULL, text != NULL, offer_save, &L)`. The label STATIC gains `SS_NOPREFIX`. With `text`, a read-only EDIT at `y_text`, height `texth`, style `WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY` (no `ES_AUTOHSCROLL`, no `SS_NOPREFIX`), filled with `pw_text_crlf()` output from a heap buffer of `2 * SSH_KBD_TEXT_MAX + 1` bytes; its `WM_CTLCOLORSTATIC` uses the detail line's tokens. With `echo`, the field has no `ES_PASSWORD` and `pw_reveal_attach` is not called. Existing callers pass zeroes and look exactly as before.
- [ ] **Step 2: `WM_CONN_PROMPT` branch** before the final `else`: `CONN_UI_KBDINT` builds the caption `"SSH Authentication"`, or `"SSH Authentication - <kb_name>"` when `kb_name[0]`, in a local buffer; `label = kb_prompt`, `detail = req->message` (the worker sets today's "profile — user@host" line), `text = kb_text[0] ? kb_text : NULL`, `echo = kb_echo`, `offer_save = req->kb_offer_save && g_config && config_find_profile_match(g_config, &s->conn_profile)`; `passbuf` and `&save` as for the password box.
- [ ] **Step 3: Run `make test`** (all pass, `test_ui_tokens` unchanged) **and the build check.**
- [ ] **Step 4: Visual check** (best effort): `xvfb-run -a wine build/win/nutshell.exe --ui-demo=all` still opens; the kbd dialog itself is a manual-check item (Task 11).
- [ ] **Step 5: Commit** (`feat(ui): keyboard-interactive prompt with server text and echo`).

### Task 9: manual keyboard-interactive server

**Files:**
- Create: `tests/manual/kbdint_server.py`, `tests/manual/README.md` (both LF)

- [ ] **Step 1: Write the server.** Python 3, paramiko, standard library otherwise; listens on `127.0.0.1` (`--port`, default 2222; `--bind` to expose it to the Windows box on the LAN, with a warning line); generates an ephemeral host key at start; user `alice`. `--scenario` selects one of:
  - `password`: one round, `"Password:"` echo off; answer `secret`;
  - `password-otp`: `"Password:"`, then a second round `"Verification code:"` (`123456`);
  - `zero-rounds`: two zero-prompt rounds carrying an instruction, then `"Password:"`;
  - `wrong-disconnect`: `"Password:"`; any answer but `secret` closes the transport;
  - `duo-menu`: a 300-byte instruction listing three options, prompt `"Passcode or option (1-3):"` echo on; `1` succeeds after a 5 s sleep (push approval);
  - `kbd-and-password`: offers `password,keyboard-interactive`.
  Each successful login opens a shell channel that prints `kbdint-ok <scenario>` and closes. `--self-test` runs every scenario against itself with a paramiko client (the right answers, then one wrong answer) and prints `PASS <scenario>` / `FAIL <scenario>`, exit 0 only when all pass.
- [ ] **Step 2: Write `tests/manual/README.md`**: install (`python3 -m pip install --user paramiko`), run a scenario, point a Nutshell password profile (user `alice`, port 2222) at it from Windows, what each scenario should show (spec "Verification before the gate"), and that nothing here runs in `make test` or CI.
- [ ] **Step 3: Run it if possible.** `python3 -c "import paramiko"`; on success run `python3 tests/manual/kbdint_server.py --self-test` and record the output; on failure record `skipped: paramiko not installed` (the state on `automaton` on 2026-11-04). Do not install packages. Either result goes in the PR body.
- [ ] **Step 4: Commit** (`test(manual): paramiko keyboard-interactive server for hand checks`).

### Task 10: README and notes

**Files:**
- Modify: `README.md` (LF; the auth bullet ~37 and "The password box" ~549-560), `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` (LF)

- [ ] **Step 1: README** per spec §11: the bullet names keyboard-interactive (OTP, PAM and TACACS-style prompts, one at a time); "The password box" gains a paragraph: the order rule; a saved password answers only a plain password prompt ("Password:", "user@host's password:"), never a passcode, code or new-password prompt, and only once; the save tick appears on such a prompt for password profiles; after a refused saved password the user is asked once more; a push approval must come within a minute of the answer; agent and Pageant are not supported. Update the tree listing for `ssh_auth_plan`, `ssh_kbd`, `pw_prompt_layout` and `tests/manual/` in the existing column layout.
- [ ] **Step 2: Notes**: a Done line for this change and an Open item "SSH agent / Pageant authentication".
- [ ] **Step 3: Run `make test`.** Expected: pass (`test_shortcuts` still matches its README block).
- [ ] **Step 4: Commit** (`docs: keyboard-interactive authentication`).

### Task 11: build, verify, commit the exe

**Files:**
- Modify: `build/win/nutshell.exe`; `src/ui/resource.h` and `README.md` only if Step 1 re-bumps

- [ ] **Step 1: Version.** `git fetch origin`; if `origin/main`'s `APP_VERSION` is now `>=` the branch's, bump again per Global Constraints. Confirm `hostname` prints `automaton`.
- [ ] **Step 2: Build.** `make clean && make release`: exit 0, no `warning:`. Do **not** restore the exe. On failure, `git restore build/win/nutshell.exe` first.
- [ ] **Step 3: Tests.** `make test 2>&1 | tee "$T/t.log"`: `Failed: 0`, `grep -c 'warning:'` 0, count noted. `xvfb-run -a make wintest`: 45 run, 0 failed (Wine driver error: `pgrep -a wine`, then `wineserver -k` only if no other session's run shows). `make lint`: 0 errors, 0 warnings.
- [ ] **Step 4: Gate script.** `bash .github/scripts/check-version.sh origin/main`: PASS.
- [ ] **Step 5: Checklist.** `git diff origin/main --stat` equals `--stat --ignore-cr-at-eol`; `git ls-files --eol tests/test_key_auth.c` still `i/crlf`; `grep -rn 'RGB(' src/ui --include=*.c | grep -v ns_draw.c` unchanged from `main`; read every `??` line.
- [ ] **Step 6: Commit** `build: vX.Y.Z` staging the exe (and `resource.h`/`README.md` if re-bumped) by path. Do not push, mark ready or merge. The PR body lists, as **not run before this merge (maintainer's instruction of 2026-11-04)**, the Windows hand-tests in spec "Verification before the gate" and `Run-Integration.ps1 -Tier gate`; the manual-check items (keepalive off in setup and on at the interactive point, the save line accepting `CONN_UI_KBDINT`, the `WM_CONN_PROMPT` KBDINT branch and its save condition); Task 9's self-test result; and the known limit that a password profile under `AuthenticationMethods password,publickey` has no publickey step to continue with (a password profile never tries a key), so it ends with the failure text. The old limit, a second password box after a partially accepted saved password, no longer applies: a refused saved password now re-queries at once (maintainer's ruling of 2026-11-05).
