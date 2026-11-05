# Trust Boundary B (Tool Gate) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship PR B, "tool gate": a tool call that is not SAFE (today `web_fetch`) runs without a click only while Auto-approve is on at Unknown or above and the tool-round cap is not reached; otherwise it becomes a one-row card the stream thread waits on, released by Run, Deny, Stop, Retry, tab close, panel close or a 10-minute expiry.

**Architecture:** The decision rule, the decision slot (one CAS per call), the round counter, the card text and every queue skip are pure functions in `src/core/` with native tests. The stream thread publishes, posts `WM_AI_TOOL_DECIDE` and polls the slot every 250 ms; the UI thread never waits, answers the slot before it settles or removes anything, and sweeps tool batches at every removal site.

**Tech Stack:** C11, MinGW-w64 cross-compile, GCC `__atomic` builtins, WinHTTP, native gcc test runner (`tests/test_framework.h`).

**Spec:** `docs/superpowers/specs/2026-11-05-trust-boundary-design.md` (decisions 13-17 and 20-23; "Delivery", PR B). Where a step says "spec §N" the wording and values there are binding.

## Global Constraints

- **Base:** `main` after PR A ("trust boundary: prompt and classifier") has merged; it supplies `tool_note`, tool-result framing, `ai_data_block`, `CMD_MASK_SENSITIVE` and the post-split UI files (`src/ui/ai_chat_send.c`, `ai_chat_stream.c`, `ai_chat_dispatch.c`, `ai_chat_status.c`, `ai_chat_view.c`, `ai_chat.c`, `ai_chat_internal.h`). Line numbers are from the split branch at 04d6e8c; re-find them by name.
- All rules in `CLAUDE.md` apply. TDD for `src/core/`; every new test function is declared and called in `tests/runner.c` (wintest ones in `tests/win_runner.c`).
- Baseline before Task 1: `make test 2>&1 | tee build/t0.log`, note `Tests Run`; warnings 0 and stay 0.
- **`-Werror` under both compilers.** Cross-compile check (on `automaton`, every task touching a file in the exe; never `make clean` before Task 11): `find src -name '*.o' -delete && make -j"$(nproc)" build/win/nutshell.exe && git restore build/win/nutshell.exe`.
- **Line endings.** CRLF and staying CRLF: `src/core/ai_prompt.c`, `src/core/ai_prompt.h`, `src/crypto/crypto.c`, `src/crypto/crypto.h`, `src/ui/ai_chat.h`, `Makefile` (none of the last four expected to change). Every other file named here is LF; new files LF; any other: `git ls-files --eol` first. Before each commit `git diff --cached --stat` equals `--stat --ignore-cr-at-eol`.
- **Design system (`src/ui`):** no literal `RGB(`, no `MulDiv(..., 96)`, no `CreateFont`, no local `#define S(`; `ns_tokens()`, `ns_type.h`, `ns_scale`, `ns_font`. `tests/test_ui_tokens.c` unchanged.
- **Version:** no bump before Task 11; then patch + 1 over the greater of `main`'s and the branch's `APP_VERSION` at build time (after `.99`, `X.(Y+1).0`).
- Tests that write files use `test_fopen_private()` and `TEST_TMP_DIR` (none needed here).
- No new code logs or displays a URL's userinfo; the row never shows a query string.
- Commit after each task, staged by path after reading every `??` line; never `git add -u` alone. Every commit message ends with:

```
Models: claude-opus-5-5 (orchestration, review); claude-sonnet-5-5 (implementation, task review)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg
```

## Review Focus

1. **Run clicked after the call already ended.** The thread expired or cancelled the call; a late Run must not run it, and the row must say "expired, not run" or "cancelled, not run", not "approved". Pinned by Task 2's `test_tool_decision_late_answer_reports_winner` and `test_tool_decision_row_labels`.
2. **A tool card at the front of the queue.** With a tool batch oldest and a command card behind it, `resolve_batch(0)` (lParam 0), `cmd_batch_first_pending` and `cmd_batch_next_runnable` reach the command card, and the harness's Run never answers a tool. Pinned by Task 3's `test_cmd_batch_skips_tool_batches`.
3. **The ceiling or Auto-approve changed while a fetch card is up.** Raising Auto-approve to Unknown does not run the waiting call (the decision was made when the request arrived); lowering the ceiling neither holds nor releases the tool row. Pinned by Task 3's `test_chat_approval_policy_walks_skip_tool` and `test_chat_msg_apply_ceiling_skips_tool`.
4. **A new stream whose first round has the same `loop_iter` as the last one.** Stream 7 round 0 and stream 8 round 0 are two rounds; two calls in stream 8 round 0 count once. Pinned by Task 5's `test_tool_rounds_key_includes_stream`.
5. **A redirect to the approved host spelled differently.** `Example.COM` after approving `example.com` is followed; `example.com.` (trailing dot) and `a.example.com` are refused, the safe direction. Pinned by Task 6's `test_web_fetch_pinned_host_spellings`.

---

### Task 1: `TOOL_UNKNOWN`, the registrations and the decision rule

**Files:**
- Modify: `src/core/ai_tools.h` (`ToolSafety`), `src/ui/ai_chat_send.c` (`ai_chat_register_tools` ~20: `web_fetch` gets `TOOL_UNKNOWN`)
- Create: `src/core/tool_decision.h`, `src/core/tool_decision.c`, `tests/test_tool_decision.c`
- Modify: `tests/runner.c`

**Interfaces:**
- Produces:
  - `typedef enum { TOOL_SAFE, TOOL_UNKNOWN, TOOL_WRITE, TOOL_CRITICAL } ToolSafety;` (`CmdSafetyLevel` order; `read_debug_log` stays `TOOL_SAFE` in `ai_tools.c`, `web_search` `TOOL_SAFE`)
  - In `tool_decision.h` (includes `ai_tools.h`, `cmd_policy.h`; no `windows.h`):
    `typedef enum { TOOL_GATE_RUN, TOOL_GATE_ASK } ToolGate;`
    `ToolGate tool_gate_decide(ToolSafety safety, CmdPolicy p, int attended);` SAFE: RUN always; UNKNOWN: RUN only when `!attended && cmd_policy_runs_unattended(p, CMD_MASK_OF(CMD_UNKNOWN))`; WRITE, CRITICAL and any out-of-range value: ASK.

- [ ] **Step 1: Write the failing test.**

```c
int test_tool_gate_matrix(void);
/* policies: default {allowed READ, unattended POLICY_NONE}; Auto on at READ, UNKNOWN, WRITE;
   ceiling CRITICAL with Auto off; POLICY_NONE for both. attended 0 and 1. Expect:
   TOOL_SAFE -> RUN in every cell; TOOL_UNKNOWN -> RUN only for (Auto on at UNKNOWN or WRITE,
   attended 0), else ASK; TOOL_WRITE, TOOL_CRITICAL, (ToolSafety)9 -> ASK in every cell.
   One ASSERT per cell, message naming safety, policy and attended. */
int test_tool_registrations(void);
/* after ai_debug_tool_register(): read_debug_log safety TOOL_SAFE (registry from tests) */
```

- [ ] **Step 2: Run `make test`.** Expected: link failure on `tool_gate_decide`.
- [ ] **Step 3: Implement.** Check every `TOOL_WRITE`/`TOOL_CRITICAL` use still compiles (`grep -rn 'TOOL_\(SAFE\|WRITE\|CRITICAL\)' src tests`).
- [ ] **Step 4: Run `make test`.** Expected: all pass.
- [ ] **Step 5: Cross-compile check.**
- [ ] **Step 6: Commit** (`feat(ai): tool safety levels and the tool gate rule`).

### Task 2: the decision slot

**Files:**
- Modify: `src/core/tool_decision.h`, `src/core/tool_decision.c`, `src/core/ai_stream.h`, `src/core/ai_stream.c` (`ai_stream_new` inits the slot)
- Test: `tests/test_tool_decision.c`, `tests/test_ai_stream.c`, `tests/runner.c`

**Interfaces:**
- Produces:

```c
#define AI_TOOL_DECIDE_TIMEOUT_MS 600000u
#define AI_TOOL_DECIDE_POLL_MS    250u
typedef enum {
    TOOL_DECISION_NONE = 0,      /* nothing published, or a stale generation */
    TOOL_DECISION_PENDING,
    TOOL_DECISION_RUN,           /* the gate said RUN: no card */
    TOOL_DECISION_APPROVED,      /* Run on the card: redirects pinned (spec §23) */
    TOOL_DECISION_NOT_APPROVED,  /* Deny, refused card, failed post */
    TOOL_DECISION_TOO_MANY,      /* the owner's set was full (spec §14) */
    TOOL_DECISION_EXPIRED,
    TOOL_DECISION_CANCELLED
} ToolDecision;
typedef struct { unsigned volatile word; } ToolDecisionSlot; /* gen << 3 | state */
void         tool_decision_init(ToolDecisionSlot *s);
unsigned     tool_decision_publish(ToolDecisionSlot *s);   /* thread: PENDING, new gen, never 0 */
/* CAS PENDING -> a for generation gen. a must be RUN, APPROVED, NOT_APPROVED, TOO_MANY or
 * CANCELLED. Returns the state that holds for gen afterwards: a when this call won, the
 * earlier outcome when the call was already settled, NONE for a stale gen or an invalid a. */
ToolDecision tool_decision_answer(ToolDecisionSlot *s, unsigned gen, ToolDecision a);
/* thread: aborted -> CAS CANCELLED; waited_ms >= AI_TOOL_DECIDE_TIMEOUT_MS -> CAS EXPIRED;
 * returns the current state for gen (PENDING while undecided). */
ToolDecision tool_decision_poll(ToolDecisionSlot *s, unsigned gen, int aborted, unsigned long long waited_ms);
const char  *tool_decision_note(ToolDecision d);   /* NULL for RUN and APPROVED */
const char  *tool_decision_row_label(ToolDecision d);
```

  `tool_decision_note`: TOO_MANY `"The user did not approve this tool call (too many pending cards)."`; every other non-run outcome `"The user did not approve this tool call."` (spec §16). `tool_decision_row_label`: PENDING `"waiting"`, APPROVED `"approved"`, NOT_APPROVED and TOO_MANY `"not approved"`, EXPIRED `"expired, not run"`, CANCELLED `"cancelled, not run"`, NONE and RUN `"not run"`. Packing gen and state in one word means a stale generation can never win the CAS (`__atomic_compare_exchange_n`, seq-cst). `AiStream` gains `ToolDecisionSlot decision;`.

- [ ] **Step 1: Write the failing tests.**

```c
int test_tool_decision_publish_gen_nonzero_and_fresh(void);   /* 2 publishes -> gens differ, both != 0, state PENDING */
int test_tool_decision_each_outcome(void);
/* answer(gen, X) == X and poll(gen,0,0) == X for X in RUN, APPROVED, NOT_APPROVED, TOO_MANY, CANCELLED */
int test_tool_decision_second_answer_loses(void);   /* APPROVED then NOT_APPROVED -> 2nd returns APPROVED */
int test_tool_decision_stale_gen(void);   /* publish g1, publish g2; answer(g1, APPROVED) == NONE; g2 still PENDING */
int test_tool_decision_invalid_answer(void);  /* answer(gen, PENDING) and (gen, EXPIRED) -> NONE, still PENDING */
int test_tool_decision_aborted_poll(void);
/* published then poll(gen, aborted 1, 0) -> CANCELLED; a later answer(gen, APPROVED) -> CANCELLED */
int test_tool_decision_abort_before_publish(void);
/* aborted already true when the thread publishes: its first poll returns CANCELLED (publish-before-check) */
int test_tool_decision_expiry(void);
/* poll(gen, 0, 599999) PENDING; poll(gen, 0, 600000) EXPIRED; answer(gen, APPROVED) then -> EXPIRED */
int test_tool_decision_late_answer_reports_winner(void);
/* EXPIRED or CANCELLED first; answer(APPROVED) returns that state, whose row label is not "approved" */
int test_tool_decision_answer_before_remove_order(void);
/* models the sweep: answer(gen, CANCELLED) returns CANCELLED, then a removal-time answer(gen,
   NOT_APPROVED) returns CANCELLED (the first answer stands; removal never overrides it) */
int test_tool_decision_notes(void);      /* both note strings byte for byte; RUN/APPROVED NULL */
int test_tool_decision_row_labels(void); /* every value above */
int test_ai_stream_new_slot_idle(void);  /* tests/test_ai_stream.c: new stream poll(0,0,0) == NONE */
```

- [ ] **Step 2: Run `make test`.** Expected: link failure.
- [ ] **Step 3: Implement** in the style of `src/core/conn_prompt.c` (module doc comment explaining publish-before-check and why there is no sticky cancelled flag: the stream's abort flag is checked by every poll).
- [ ] **Step 4: Run `make test`.** Expected: all pass.
- [ ] **Step 5: Commit** (`feat(core): one decision slot per stream for tool calls`).

### Task 3: tool rows in the queue and the card model

**Files:**
- Modify: `src/core/chat_approval.h`, `src/core/chat_approval.c`, `src/core/cmd_batch.h`, `src/core/cmd_batch.c`, `src/core/chat_msg.h`, `src/core/chat_msg.c`, `src/core/ai_prompt.c` (CRLF; `ai_build_continue_text` ~1718)
- Modify: `src/ui/chat_listview.c` (header call sites only, to keep the build green)
- Test: `tests/test_chat_approval.c`, `tests/test_cmd_batch.c`, `tests/test_chat_msg.c`, `tests/test_ai_prompt.c`, `tests/runner.c`

**Interfaces:**
- Consumes: `ToolDecision` (Task 2).
- Produces:
  - `ApprovalEntry` gains `int tool;`. `int chat_approval_add_tool(ApprovalQueue *q, const char *row_text, const char *detail, CmdSafetyLevel level);` copies `row_text` into `command` and `detail` into `note`, `safety = level`, `safety_mask = CMD_MASK_OF(level)`, `tool = 1`, status PENDING whatever the policy; returns -1 for the same inputs `chat_approval_add` refuses (empty, 1024 bytes or more, control or bidi characters, queue full).
  - `chat_approval_next_approved`, `chat_approval_block_disallowed`, `chat_approval_unblock_all` skip `tool` entries.
  - `CmdBatch` gains `unsigned tool_stream_id; unsigned tool_gen;` (0 = a command batch). `int cmd_batch_is_tool(const CmdBatch *b);` `int cmd_batch_full(const CmdBatchSet *set);` (count >= `CMD_BATCH_MAX`). `cmd_batch_first_pending` and `cmd_batch_next_runnable` skip tool batches.
  - `ai_build_continue_text` skips `tool` entries.
  - `ChatMsgItem.u.cmd` gains `int tool; int tool_outcome;` (a `ToolDecision`, PENDING when created); `void chat_msg_set_tool(ChatMsgItem *item, int tool);` `void chat_msg_set_tool_outcome(ChatMsgItem *item, int outcome);` both NULL-safe.
  - `int chat_card_header_text(const char *noun, int n, int held, int quoted, char *buf, size_t cap);` `noun` NULL means `"command"`; plural adds `s`.
  - `void chat_msg_apply_ceiling(ChatMsgItem *it, int prev_allowed, int next_allowed);` the per-item body of `apply_policy_change`'s two walks (raise: unblock a blocked, unquoted row within the ceiling, `approved = -1`, `selected = 1`; lower: block an unsettled undecided row above it); tool rows and non-command items are untouched in both directions.

- [ ] **Step 1: Write the failing tests.**

```c
int test_chat_approval_add_tool_pending_even_with_auto(void);
/* Auto on at CRITICAL: add_tool("web_fetch: https://a.test/", "https://a.test/?q=1", CMD_UNKNOWN) ->
   PENDING, tool 1, safety CMD_UNKNOWN, note "https://a.test/?q=1" */
int test_chat_approval_add_tool_refuses(void);
/* "", 1024-byte row, "web_fetch: a\x1b", "web_fetch: \xE2\x80\xAE" , 17th entry -> -1, count unchanged */
int test_chat_approval_policy_walks_skip_tool(void);
/* tool row + blocked command row; raise to CRITICAL: unblock_all releases only the command;
   lower to READ: block_disallowed leaves the tool row PENDING; approve(tool) then
   next_approved == -1 */
int test_cmd_batch_skips_tool_batches(void);
/* set: tool batch (tool_stream_id 5, pending tool row) then command batch with a pending row:
   first_pending == command batch; approve the tool row: next_runnable is not the tool batch */
int test_cmd_batch_full(void);   /* 7 batches -> 0; 8 -> 1; NULL -> 1 */
int test_continue_text_skips_tool(void);  /* queue with an approved tool row + "ls" done -> no "web_fetch" in text */
int test_chat_card_header_noun(void);
/* ("tool call",1,0,0) "1 tool call"; ("tool call",2,0,0) "2 tool calls"; (NULL,3,1,0)
   "3 commands \xC2\xB7 1 held"; cap 8 -> 0 */
int test_chat_msg_set_tool_and_outcome(void);   /* sets fields; NULL-safe; non-command no-op */
int test_chat_msg_apply_ceiling_skips_tool(void);
/* tool item (blocked 0, approved -1): raise and lower leave it identical (memcmp);
   command item behaves as the two walks in ai_chat_status.c did; quoted item never unblocked */
```

  Update the existing `chat_card_header_text` tests and callers to pass `NULL` first.

- [ ] **Step 2: Run `make test`.** Expected: compile failure.
- [ ] **Step 3: Implement.** Keep `ai_prompt.c` CRLF.
- [ ] **Step 4: Run `make test`.** Expected: all pass.
- [ ] **Step 5: Cross-compile check.**
- [ ] **Step 6: Commit** (`feat(chat): tool rows that only a click decides`).

### Task 4: the card's row and detail text

**Files:**
- Modify: `src/core/net_guard.h`, `src/core/net_guard.c`, `src/core/ai_tools.h`, `src/core/ai_tools.c`
- Test: `tests/test_net_guard.c`, `tests/test_ai_tools.c`, `tests/runner.c`

**Interfaces:**
- Consumes: `net_url_display`, `net_url_parse` (existing), `paste_filter_controls`.
- Produces:
  - `size_t net_url_display_marked(const char *url, char *out, size_t cap);` as `net_url_display`, but a dropped query adds `"?\xE2\x80\xA6"` and a cut path ends in `"\xE2\x80\xA6"` instead of `...` (`net_url_display` itself is unchanged).
  - `size_t net_url_detail(const char *url, char *out, size_t cap);` the URL with only the userinfo removed; each C0 byte and DEL written as `\xNN`, each bidi control (U+202A-202E, U+2066-2069, U+200E, U+200F) as `\uXXXX`.
  - `int ai_tool_card_text(const AiToolCall *call, char *row, size_t row_cap, char *detail, size_t detail_cap);` `web_fetch`: row `"web_fetch: " + net_url_display_marked(url)`, detail `net_url_detail(url)`; any other tool: row the tool name, detail its `input_json` with controls escaped the same way. Returns 0, or -1 when `call` is NULL or the URL is missing (row then `"web_fetch: (no url)"`).

- [ ] **Step 1: Write the failing tests.**

```c
int test_net_url_display_marked(void);
/* "https://a.test/p?key=secret" -> "https://a.test/p?\xE2\x80\xA6"; "https://a.test/p" unchanged;
   200-byte path -> ends "\xE2\x80\xA6", no "...", no partial UTF-8; userinfo dropped */
int test_net_url_detail(void);
/* "https://u:pw@a.test/p?k=v" -> "https://a.test/p?k=v"; "https://a.test/\x01" -> ".../\\x01";
   "https://a.test/\xE2\x80\xAE" -> ".../\\u202E"; cap 8 -> NUL-terminated */
int test_ai_tool_card_text_web_fetch(void);  /* {"url":"https://u@a.test/x?q=1"} -> row and detail as above */
int test_ai_tool_card_text_other_tool(void); /* name "custom", input "{\"a\":\"\\u0001\"}" -> row "custom" */
```

- [ ] **Step 2: Run `make test`.** Expected: link failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run `make test`.** Expected: all pass.
- [ ] **Step 5: Commit** (`feat(core): tool card row and detail text`).

### Task 5: `auto_tool_rounds`

**Files:**
- Modify: `src/core/ai_prompt.h`, `src/core/ai_prompt.c` (both CRLF; beside `ai_rounds_*` ~1890-1920)
- Test: `tests/test_ai_rounds.c`, `tests/runner.c`

**Interfaces:**
- Produces: `AiSessionState` gains `int auto_tool_rounds; unsigned tool_round_stream; int tool_round_iter; int tool_round_base; int tool_round_counted; int tool_pause_shown;` and `#define AI_AUTO_TOOL_ROUNDS_MAX 3`. All NULL-safe:
  - `int ai_tool_rounds_attended(AiSessionState *s, unsigned stream_id, int loop_iter);` on a new key (stream id, `loop_iter`) records it with `tool_round_base = auto_tool_rounds` and `tool_round_counted = 0`; returns 1 when `tool_round_base >= AI_AUTO_TOOL_ROUNDS_MAX`.
  - `void ai_tool_rounds_ran_unattended(AiSessionState *s, unsigned stream_id, int loop_iter);` +1 once per key (only when the key matches and `!tool_round_counted`).
  - `void ai_tool_rounds_reset(AiSessionState *s);` `auto_tool_rounds = 0`, `tool_pause_shown = 0`; the stored key and base are kept. Called on a typed message and on Run on a tool row.
  - `int ai_tool_rounds_take_pause_row(AiSessionState *s);` 1 the first time after a reset, then 0.
  - `#define AI_TOOL_PAUSE_ROW "[Auto-approve paused after 3 automatic tool rounds]"`.

- [ ] **Step 1: Write the failing tests.**

```c
int test_tool_rounds_three_then_attended(void);
/* stream 1: rounds 0,1,2 each attended()==0 then ran_unattended; round 3 attended()==1 */
int test_tool_rounds_counts_once_per_round(void);  /* two calls in (1,0) -> auto_tool_rounds 1 */
int test_tool_rounds_key_includes_stream(void);
/* (7,0) ran; (8,0) ran -> 2; two calls in (8,1) -> 3 */
int test_tool_rounds_reset_mid_round_still_asks(void);
/* reach base 3 at (1,3): attended 1; reset (Run click); 2nd call in (1,3): attended 1 (pre-round count);
   next round (1,4): attended 0 */
int test_tool_rounds_command_counter_separate(void);  /* ai_rounds_continue(s,1) x3 leaves auto_tool_rounds 0 and vice versa */
int test_tool_rounds_pause_row_once(void);  /* take 1, take 0; after reset take 1 */
int test_tool_rounds_null_safe(void);
```

- [ ] **Step 2: Run `make test`.** Expected: compile failure.
- [ ] **Step 3: Implement** (CRLF kept).
- [ ] **Step 4: Run `make test`.** Expected: all pass.
- [ ] **Step 5: Commit** (`feat(ai): a separate round cap for unattended tool calls`).

### Task 6: redirect host pinning for an approved fetch

**Files:**
- Modify: `src/core/ai_tool_web_fetch.h` (`WebFetchContext`), `src/core/ai_tool_web_fetch.c` (hop loop ~222-310)
- Test: `tests/test_web_fetch_guard.c`, `tests/runner.c`

**Interfaces:**
- Produces: `WebFetchContext` gains `char pinned_host[NET_HOST_MAX];` (`""` = unpinned). When set, a hop after the first whose parsed host differs from it (ASCII case-insensitive, exact otherwise) is refused before any resolve, with the result text `"redirect to another host refused; ask again with the new URL"` (Nutshell's text, so PR A's routing puts it in `tool_note`).

- [ ] **Step 1: Write the failing tests** (the existing fake backend):

```c
int test_web_fetch_pinned_same_host_followed(void);  /* pin "pub.test"; 302 "/next" -> rc 0, gets 2 */
int test_web_fetch_pinned_cross_host_refused(void);
/* pin "pub.test"; 302 "http://other.test/" -> rc -1, strstr(buf, "redirect to another host refused"),
   gets 1, resolve never called with "other.test" */
int test_web_fetch_unpinned_cross_host_followed(void);  /* pin "": same redirect -> rc 0, gets 2 */
int test_web_fetch_pinned_host_spellings(void);
/* pin "example.com": "http://Example.COM/x" followed; "http://example.com./x" and
   "http://a.example.com/x" refused */
```

- [ ] **Step 2: Run `make test`.** Expected: compile failure (`pinned_host`).
- [ ] **Step 3: Implement** in the hop loop, after `net_url_parse` and before resolve, for `hop > 0`.
- [ ] **Step 4: Run `make test`.** Expected: all pass, existing guard tests unchanged.
- [ ] **Step 5: Commit** (`feat(web_fetch): an approved fetch stays on the approved host`).

### Task 7: the stream thread asks and waits

**Files:**
- Modify: `src/ui/ai_chat_internal.h` (`WM_AI_TOOL_DECIDE`, `AiToolDecideMsg`), `src/ui/ai_chat_stream.c` (tool loop ~331-420), `src/ui/ai_chat_send.c` (`ai_chat_drain_stream_messages` ~283)

**Interfaces:**
- Consumes: `tool_decision_*` (Task 2), `ai_tool_card_text` (Task 4), `WebFetchContext.pinned_host` (Task 6), `AiToolDef.safety` (Task 1).
- Produces (`ai_chat_internal.h`):

```c
#define WM_AI_TOOL_DECIDE (WM_USER + 105)  /* lParam: heap AiToolDecideMsg; 104 is WM_AI_GET_POLICY */
enum { AI_TOOL_DECIDE_ASK = 0, AI_TOOL_DECIDE_EXPIRED = 1 };
typedef struct {
    unsigned   stream_id, gen;
    int        loop_iter, kind;
    ToolSafety safety;
    char       row[256], detail[1024];
} AiToolDecideMsg;
```

- [ ] **Step 1: Implement the ask.** For each call whose `tdef->safety != TOOL_SAFE`, after the rate-limit check and the "using tool:" row: `gen = tool_decision_publish(&s->decision)`; build the message (`ai_tool_card_text`, `agentic.loop_iter`); a failed allocation or `PostMessage` answers `tool_decision_answer(&s->decision, gen, TOOL_DECISION_NOT_APPROVED)` itself. Then loop: `d = tool_decision_poll(&s->decision, gen, ai_stream_aborted(s), GetTickCount64() - t0)`; break when not PENDING, else `Sleep(AI_TOOL_DECIDE_POLL_MS)`. EXPIRED posts a second message with `kind = AI_TOOL_DECIDE_EXPIRED` (same stream id and gen). RUN or APPROVED: for APPROVED copy the call's parsed host into `s->fetch.pinned_host`, run `ai_tool_execute`, clear the pin. Any other outcome: `results[ti]` gets `tool_note = tool_decision_note(d)`, `content` NULL, `is_error` 0, and no tool is run. SAFE calls are untouched. After the loop, `ai_stream_aborted(s)` still ends the stream as today.
- [ ] **Step 2: The drain.** `ai_chat_drain_stream_messages` adds a second `PeekMessage(&m, hwnd, WM_AI_TOOL_DECIDE, WM_AI_TOOL_DECIDE, PM_REMOVE)` loop freeing each `AiToolDecideMsg` (the 100-103 range stays; 104 is sent, never posted).
- [ ] **Step 3: Run `make test`** (expected: pass) **and the cross-compile check.**
- [ ] **Step 4: Commit** (`feat(ai): the stream thread asks before a non-SAFE tool and polls for the answer`).

### Task 8: the UI answers: handler, Run/Deny order, activity line

**Files:**
- Modify: `src/core/chat_activity.c` (and `tests/test_chat_activity.c`), `src/ui/ai_chat.c` (`WndProc`: new case; Run/Deny handlers ~640-800), `src/ui/ai_chat_dispatch.c` (`ai_chat_settle_batch_if_done` ~37), `src/ui/ai_chat_view.c` (`append_batch_command_items` ~129), `src/ui/chat_listview.c` (tool row label and header noun), `src/ui/ai_chat_send.c` (`ai_chat_send_user_message`: `ai_tool_rounds_reset`)

**Interfaces:**
- Consumes: Tasks 1-5.
- Produces (UI, declared in `ai_chat_internal.h`): `void ai_chat_tool_card_answer(AiChatData *d, CmdBatch *batch, ToolDecision answer);` and `ToolDecision ai_chat_tool_slot_answer(AiChatData *d, const CmdBatch *batch, ToolDecision answer);` (finds the stream with `ai_stream_table_find(&d->streams, batch->tool_stream_id, NULL)`; no stream: NONE).

- [ ] **Step 1: Activity phase, test first.** `test_chat_activity_waiting_does_not_age` (`tests/test_chat_activity.c`): phase `ACTIVITY_WAITING`, tick at +45 s -> health GREEN; `chat_activity_format` -> exactly `"Waiting for your decision"` (no "(slow)", no "Stalled"). Run `make test` (fails), then change `chat_activity_tick` to keep GREEN in WAITING and the format text; run `make test` (passes). The demo's "error" state still shows "Connection lost" (connection_lost wins).
- [ ] **Step 2: `WM_AI_TOOL_DECIDE` handler.** Never waits. Free the message on every path.
  - `ai_stream_table_find(&d->streams, m->stream_id, &owner)` NULL: ignore (the thread sees its abort flag).
  - `kind == AI_TOOL_DECIDE_EXPIRED`: the owner's tool batch with that stream id and gen gets its row outcome `TOOL_DECISION_EXPIRED`, settles and is removed; displayed: activity back to `ACTIVITY_PROCESSING`.
  - Ask: `p` = `d->approval_q.policy` when `owner == d->active_state`, else `owner->policy`; `attended` = `owner != d->active_state || ai_tool_rounds_attended(owner, m->stream_id, m->loop_iter)`. `tool_gate_decide(m->safety, p, attended)` RUN: `ai_chat_tool_slot_answer(..., TOOL_DECISION_RUN)`, `ai_tool_rounds_ran_unattended(owner, ...)`, done, no row.
  - ASK: when `tool_gate_decide(m->safety, p, 0) == TOOL_GATE_RUN` (the cap forced it) and `ai_tool_rounds_take_pause_row(owner)`, append a `CHAT_ITEM_STATUS` `AI_TOOL_PAUSE_ROW` (displayed only). `cmd_batch_full(&owner->batches)`: answer `TOOL_DECISION_TOO_MANY`, add nothing. Else `cmd_batch_add` (eviction cannot happen: the set is not full), set `tool_stream_id`/`tool_gen`, `chat_approval_add_tool(&b->q, m->row, m->detail, (CmdSafetyLevel)m->safety)`; -1: `cmd_batch_remove`, answer NOT_APPROVED. Displayed: append the card's items (as `append_batch_command_items` does, `chat_msg_set_tool(item, 1)`, outcome PENDING) and set `ACTIVITY_WAITING`; background: `owner->activity_phase = (int)ACTIVITY_WAITING` so the switch restores it.
- [ ] **Step 3: Run and Deny.** In the four handlers (`IDC_CMD_APPROVE_BASE`, `IDC_CMD_DENY_BASE`, `IDC_CMD_APPROVE_ALL`, `IDC_CMD_APPROVE_SEL`), right after `ai_chat_resolve_batch`, a tool batch goes to `ai_chat_tool_card_answer(d, batch, APPROVED or NOT_APPROVED)` and the handler returns. That function, in this order: `res = ai_chat_tool_slot_answer(...)` first; set every row's `tool_outcome = res` (so a late click shows "expired, not run" or "cancelled, not run"); mark the queue entry approved or denied; Run with `res == APPROVED` calls `ai_tool_rounds_reset(d->active_state)`; then `ai_chat_settle_batch_if_done` (which removes it: `next_approved` skips tool rows, so no dispatch and no continue); activity `ACTIVITY_PROCESSING`; `ai_chat_maybe_start_next_batch(d)`.
- [ ] **Step 4: Rows and card.** `append_batch_command_items` sets `tool` from the entry. `chat_listview.c`: a row with `u.cmd.tool` shows `tool_decision_row_label(tool_outcome)` instead of `chat_cmd_label`, its chip shows the class; the card header passes `"tool call"` when the card's rows are tool rows. `ai_chat_send_user_message` calls `ai_tool_rounds_reset(d->active_state)` beside `ai_rounds_user_message`.
- [ ] **Step 5: Run `make test`** (expected: pass, `test_ui_tokens` green) **and the cross-compile check.**
- [ ] **Step 6: Commit** (`feat(ai): tool cards, answered before they are settled`).

### Task 9: sweeps, the policy walk and the dispatch rule

**Files:**
- Modify: `src/ui/ai_chat_send.c` (`ai_chat_abort_session_stream` ~265, `ai_chat_launch_stream_thread` before `ai_stream_table_add` ~188, `ai_chat_cancel_active_stream` ~299), `src/ui/ai_chat_dispatch.c` (`ai_chat_dispatch_start` ~113), `src/ui/ai_chat.c` (New Chat ~505-522, demo reset ~2428, `WM_DESTROY` ~1802), `src/ui/ai_chat_status.c` (`apply_policy_change` ~36-94), `src/ui/ai_chat.h` (CRLF; public sweep), `src/ui/window.c` (~1072)

**Interfaces:**
- Consumes: `ai_chat_tool_slot_answer` (Task 8), `chat_msg_apply_ceiling` (Task 3).
- Produces: `void ai_chat_cancel_tool_batches(AiChatData *d, AiSessionState *state);` (internal) and `void ai_chat_cancel_tool_batches_for(HWND hwnd, AiSessionState *state);` (`ai_chat.h`, no-op when the panel is gone).

- [ ] **Step 1: The sweep.** For each tool batch in `state->batches`, oldest first: `res = ai_chat_tool_slot_answer(d, b, TOOL_DECISION_CANCELLED)` first; when `state == d->active_state`, its rows get `tool_outcome = res` and settle; then `cmd_batch_remove`. Call it: at the top of `ai_chat_abort_session_stream` (before `ai_stream_table_abort_owner`: covers Stop, Retry, tab close, the reaper and the `WM_DESTROY` loop); in `ai_chat_launch_stream_thread` before `ai_stream_table_add`; in New Chat and the demo reset before `cmd_batch_set_free`; in `window.c` via `ai_chat_cancel_tool_batches_for(g_hwndAiChat, &s->ai_state)` before `cmd_batch_set_free`.
- [ ] **Step 2: The policy walk.** `apply_policy_change`'s two `msg_list` loops call `chat_msg_apply_ceiling(it, prev.allowed, next.allowed)`; the per-batch `unblock_all`/`block_disallowed` calls stay (they skip tool entries since Task 3).
- [ ] **Step 3: The dispatch rule** (spec §21). `ai_chat_dispatch_start` returns at once when `ACTIVE_BUSY(d)`; `ai_chat_cancel_active_stream` ends with `ai_chat_maybe_start_next_batch(d)`; Task 8's answer and expiry paths already call it. No `pending_continue` exists; `ai_chat_send_continue_message`'s busy return stays.
- [ ] **Step 4: Run `make test`** (expected: pass) **and the cross-compile check.** `grep -n 'cmd_batch_remove\|cmd_batch_set_free' src/ui/*.c`: every hit is the sweep, settle, `ai_chat_dispatch_cancel`/tick on command batches, or preceded by the sweep.
- [ ] **Step 5: Commit** (`feat(ai): every tool card removal answers its call; dispatch waits for the stream`).

### Task 10: README

**Files:**
- Modify: `README.md` (AI Assist tools section; Security threat model ~333)

- [ ] **Step 1: Edit.** `web_fetch` asks on a card unless Auto-approve is on at Unknown or above; after 3 automatic tool rounds it asks again ("Auto-approve paused" row); a fetch approved on a card does not follow a redirect to another host; Stop, Retry and closing the tab release a waiting call; a card waits at most 10 minutes. The card row hides the query string; the spec's PR-body wording applies to the threat model: the query is one exfiltration channel, the path and a subdomain carry data too, and reading the card is the check. `web_search` and `read_debug_log` run as before. Refresh the test counts README states.
- [ ] **Step 2: Run `make test`.** Expected: pass (`test_shortcuts` still matches its README block).
- [ ] **Step 3: Commit** (`docs(readme): the tool gate`).

### Task 11: version bump, Windows build, gate checks and the Windows list

**Files:**
- Modify: `src/ui/resource.h`, `README.md` (`**Version**:`), `build/win/nutshell.exe`

- [ ] **Step 1: Version.** `git fetch origin main`; patch + 1 over the higher of `main`'s and the branch's `APP_VERSION`; both macros and README. `hostname` prints `automaton`.
- [ ] **Step 2: Build.** `make clean && make release`; on failure `git restore build/win/nutshell.exe` first.
- [ ] **Step 3: Tests.** `make test 2>&1 | tee build/t.log`: `Failed: 0`, 0 warnings, count noted; `xvfb-run -a make wintest`: 0 failed; `make lint`: 0/0.
- [ ] **Step 4: Gate script.** `bash .github/scripts/check-version.sh origin/main`: PASS.
- [ ] **Step 5: Checklist.** `git diff origin/main --stat` equals `--stat --ignore-cr-at-eol`; CRLF files still `i/crlf`; `RGB(` count in `src/ui` unchanged; every `??` line read.
- [ ] **Step 6: Commit** `build: vX.Y.Z` staging the three files by path, with the trailer block. Do not push, mark ready or merge.
- [ ] **Step 7: The PR body's Windows manual checks** (for the maintainer):
  - Auto off: `web_fetch` asks; its row shows `?…` for a URL with a query, Details shows the query; `web_search` and `read_debug_log` run as today.
  - Auto on at Read: `web_fetch` asks. At Unknown: it runs for three automatic tool rounds, then asks under "[Auto-approve paused after 3 automatic tool rounds]".
  - With a fetch card up: Stop, Retry and tab close each release the stream at once (row "cancelled, not run"); lowering the ceiling leaves the row pending and unheld; the activity line reads "Waiting for your decision" and is not "Stalled" after 30 s.
  - A background tab's tool card shows when the tab is opened; a command approved during a tool wait stays queued and runs after the reply, with one continue message.
  - An approved fetch redirected to another host returns the note; an unattended one follows it.
  - Leave a card 10 minutes: the row reads "expired, not run" and the reply goes on.
  - `--ui-demo=all` on four themes; the harness `-Tier gate` AI-panel cases.
