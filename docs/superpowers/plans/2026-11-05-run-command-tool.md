# run_command Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On every provider on run_command (Anthropic, OpenAI and Moonshot by default; DeepSeek, Gemini and Custom when "Commands as tool calls" is On; spec §1), commands reach the terminal only as `run_command` tool calls: the stream ends at the call, the reply's calls become one ordinary command card, and when the batch goes the panel appends one result per call (the command's own captured output, framed as data) and resumes the stream from them. Every command batch, `[EXEC]` or run_command, shows which commands ran, which runs and which wait, also after a rebuild.

**Architecture:** Every rule that can be tested is a pure function in `src/core/` with native tests: the tool definition and input parser, the planner, the note choke point and the tool-pair invariant, the capture by absolute line, the cut and fit, the result and note texts, the round gate, the Retry plan, the body and Save As rendering. `src/ui/` wires them: the stream thread stores the turn and posts the calls, `WM_AI_RESPONSE` builds the batch, and one finish function in `ai_chat_dispatch.c` ends every run_command batch with a cause.

**Tech Stack:** C11, MinGW-w64 cross-compile, native gcc test runner (`tests/test_framework.h`), `make wintest` under Wine.

**Spec:** `docs/superpowers/specs/2026-11-05-run-command-tool-design.md` (decisions 1-26: re-based on v1.2.75, critiqued twice on 2026-11-11). Read it with this plan; where a step says "spec §N" the wording and values there are binding.

## Global Constraints

- **Base:** `main` at `6432ca0` (v1.2.75), both trust-boundary PRs (#88, #91) and #87, #89, #90, #92, #93 merged. Worktree `~/nutshell-wt/run-command`, branch `feat/run-command-tool`; the spec and this plan are already committed there. UI names: `ai_chat_send.c` (`ai_chat_launch_stream_thread`, `ai_chat_send_user_message`, `ai_chat_send_continue_message`, `ai_chat_drain_stream_messages`, `ai_chat_abort_session_stream`, `ai_chat_cancel_active_stream`), `ai_chat_stream.c` (`ai_chat_stream_thread_proc`, `post_response`), `ai_chat_dispatch.c` (`execute_command`, `ai_chat_dispatch_tick`, `ai_chat_dispatch_cancel`, `ai_chat_settle_batch_if_done`, `ai_chat_cancel_tool_batches`), `ai_chat_view.c` (`ai_chat_rebuild_display`), `ai_chat_status.c` (`apply_policy_change`), `ai_chat.c` (`WM_AI_RESPONSE`, `do_session_switch`, `IDC_ACTIVITY_RETRY`, `IDC_CHAT_NEWCHAT`, `IDC_CHAT_SEND`, `WM_DESTROY`, `ai_chat_notify_session_closed`). **Line numbers are from `6432ca0`: find every site by name** (`grep -n`). Trust provides, use and duplicate none: `ai_build_request_body_frame`/`AiBodyFrame`, `ai_data_block_dup`, `ai_data_lead_tool`, `ai_data_escape_dup` (`ai_data_block.h`), `agentic_cap_tool_result`, `agentic_add_assistant_tool_msg_ex`, `AI_TOOL_NOTE_MAX` (`ai_tools.h`), `TOOL_UNKNOWN`, `tool_gate_decide`, `tool_dispatch_rule`/`tool_fetch_off_note`/`tool_fetch_off_row` (`tool_decision.h`), `cmd_batch_full`, `cmd_batch_is_tool`, `chat_card_header_text(noun, ...)`, `ai_chat_cancel_tool_batches`. A run_command batch never sets `tool_stream_id`.
- **Providers (spec §1):** the setting "Commands as tool calls" resolves Automatic through one table (`ai_runcmd_auto_default`: anthropic, openai, moonshot On; deepseek, gemini, custom Off); On enables any provider; `[EXEC]` stays wherever it is off. Moonshot's request body does not change by a byte for a conversation `[EXEC]` would also produce. Governing principle (spec Constraints): no model request without a user action that would start one today, nothing unattended that would not run today.
- All rules in `CLAUDE.md` apply. TDD for everything in `src/core/` and `src/term/`; each new test function is declared and called in `tests/runner.c`.
- **Verification commands, each run alone** (never chained, never in parallel with another build in this worktree): `make -j test`, `make test SAN=1`, `make lint-gate`, `xvfb-run -a make wintest`, `bash .github/scripts/check-version.sh origin/main`. Baselines on `6432ca0`: 3695 native tests, 0 failed, 0 `warning:` lines (measured 2026-11-11 with `make -j test`, libssh2 stub); 51 wintest tests. Every task ends with `make -j test` 0 failed and no new warning; Tasks A1, A3, B2, B3, B5, B6, B8, B19 and B20 also run `make test SAN=1`; Task B6 also runs `make fuzz && make fuzz-run` (clang-16 is on `automaton`); the last task runs all five.
- **Mid-plan Windows compile check** (every task touching a file in the exe): `make -B` on the touched `.c` files' Windows `.o` targets, which sit beside their sources (`OBJS = $(SRCS:.c=.o)`, `CC = x86_64-w64-mingw32-gcc`), e.g. `make -B src/ui/ai_chat.o src/core/run_command.o`, then `rm -f` those `.o` files. Never `make clean`, never `make release`, before the final task.
- **Version:** no bump before the final task. There it is one above both `main`'s version and the branch's last build, at build time (`git fetch origin main` first); the patch never passes 99 (after `X.Y.99` comes `X.(Y+1).0`); both `resource.h` macros and README's `**Version**:` line. No literal version anywhere else.
- **Git:** no `git stash`, no force-push, no rebase, no amend, no push. One commit per task; stage by path, read every `??` line of `git status --short`, never `git add -u` alone. Never commit a fuzz crash input (`build/fuzz/crashes/`); a hand-written seed under `tests/fuzz/corpus/` is fine.
- **Line endings.** CRLF and staying CRLF, touched by this plan: `src/core/ai_prompt.c`, `src/core/ai_prompt.h`, `src/core/term_extract.c`, `src/core/term_extract.h`, `tests/test_term_extract.c`, `src/term/buffer.c`, `src/config/loader.c`, `src/ui/ai_chat.h`, `Makefile` (Task B1's `FUZZ_TARGETS`). LF (checked on `6432ca0`): `src/config/config.h`, `src/core/settings_help.h`, `src/ui/settings.c`, `src/ui/resource.h`, `src/ui/chat_listview.c`, `tests/test_config.c`, `tests/test_settings_help.c`, `src/term/term.h`, `src/core/cmd_batch.*`, `src/core/ai_agentic.*`, `src/core/ai_tools.*`, `src/core/ai_stream.*`, `src/core/chat_approval.*`, `src/core/settings_help.c`, `src/core/chat_msg.*`, `src/core/ui_demo.*`, `src/ui/window.c`, `tests/test_chat_msg.c`, `tests/test_ui_demo.c`, `tests/integration/cases/30-ui-demo.ps1`, every `src/ui/ai_chat_*.c`, `src/ui/ai_chat_internal.h`, `tests/runner.c`, `tests/test_term_scrollback.c`, `tests/fuzz/fuzz_term.c`, `README.md`. Any other file: `git ls-files --eol <file>` first. New files LF. Before each commit `git diff --cached --stat` equals `git diff --cached --stat --ignore-cr-at-eol`.
- **Design system (`src/ui`):** no literal `RGB(`, no `MulDiv(..., 96)`, no `CreateFont`, no local scale macro, no second `WM_TIMER` animation loop; colours from `ns_tokens()`, sizes from `ns_type.h`, fonts from `ns_font()`, hover via `ns_hover`. `tests/test_ui_tokens.c` passes unchanged.
- Tests that write files open them with `test_fopen_private()` and build paths from `TEST_TMP_DIR` (no task here needs one).
- No new code logs a command or its output except through `DLOG_CONTENT`.
- **Stop points.** After Task A4, and again after Task B22, the orchestrator runs one Opus review of that part's diff (`git diff` over that part's commits, not a summary), stops and returns the findings. It does not fix them and does not run Task V until told.
- **Commits** (CLAUDE.md, "Git commits"). Every commit message ends with this block: the `Models:` line lists the full id of every model that touched the commit and what it did; `Co-Authored-By` names the model that did this task's work, never a fixed one:

```
Models: <every model id and what it did, e.g. claude-opus-5-5 (orchestration, review); claude-sonnet-5-5 (implementation)>

Co-Authored-By: Claude <name and version of the model that did this task's work> <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg
```

## Review Focus

1. **A USER turn between a `tool_use` and its results.** A BLOCKED call waits on its card; the user raises the ceiling (the "policy raised" note) and clicks Run. The note must land after the results, never between, and a conversation that is still malformed must give an error row, not a 400. Pinned by Task B4's `test_conv_note_queued_while_open` and `test_pairs_note_between_fails`.
2. **A command too big for the stream, or spliced.** A 5 KB heredoc in `command`, or a 4000-byte `note` followed by a short `command`, overflows `AI_TOOL_INPUT_MAX`; the call must be INVALID with stored input `{}`, never a spliced command, and the rest of the chunk must still be read. Pinned by Task A1's `test_tool_stream_overflow_splice_refused`, `test_tool_stream_overflow_keeps_rest_of_chunk`, Task B2's `test_agentic_sanitize_cut_heredoc` and Task B11's `test_body_after_cut_input_validates`.
3. **A provider switch mid-conversation.** Tool turns recorded under one provider and sent to another must render as text (ids, reasoning and signatures go back only to their issuer) and still build a valid body. Pinned by Task B11's `test_body_foreign_provider_turn_as_text`, `test_body_runcmd_turn_as_text_custom` and `test_body_runcmd_turn_structured_moonshot`.
4. **A long command and Retry or Stop.** `sleep 120` then Retry: the result says it ran and may still be running, the `tool_use` turn is never popped, and Stop says "do not run it again". Pinned by Task B8's `test_runcmd_results_executing_each_cause` and Task B9's `test_runcmd_retry_never_pops_tool_turn`.
5. **A new unattended re-ask.** A reply of INVALID calls only, a Deny all, Stop, and showing a background tab must each start no model request (spec Constraints, governing principle). Pinned by Task B8's `test_runcmd_no_slot_does_not_relaunch` and Task B15's handler order.
6. **A recorded batch shown under the wrong reply.** Retry pops a reply whose batch was recorded; the next reply lands at the same index. Its rows must not appear under the new reply. Pinned by Task A4's `turn_hash` check and `test_ui_demo_runlist_record_matches_turn`.
---

## Part A: which commands ran, which wait (standalone on today's `[EXEC]` path)

Part A depends on no run_command task and ends with the tree shippable (Task A5). It also carries two defects that stand alone today.

### Task A1: sticky tool-input overflow (spec §2; a defect today on the shipped `web_fetch` path)

**Files:**
- Modify: `src/core/ai_tools.h` (`AiToolCall.overflowed`), `src/core/ai_tools.c` (`stream_append_input` ~559, its callers ~649 and ~759), `tests/test_ai_tools.c`, `tests/runner.c`

**Interfaces:**
- `AiToolCall` gains `int overflowed;`. The first fragment `stream_append_input` refuses sets it and every later fragment for that call is ignored; the accumulators no longer return -1 for an overflow, so the chunk's other calls and its `finish_reason` are processed. The stream thread, finding a call `overflowed`, stores its input as `{}` (today's tool path then answers it with the executor's own error; Part B answers run_command calls "needs a command string").

- [ ] **Step 1: Write the failing tests** (`tests/test_ai_tools.c`).

```c
int test_tool_stream_overflow_splice_refused(void);
/* OpenAI deltas for one call: "{\"note\":\"" + 4000 x 'a' (refused: past AI_TOOL_INPUT_MAX), then
   "\",\"command\":\"rm -rf /home/user/build-cache\"}" (would fit) -> overflowed 1, input_json holds no "rm -rf" */
int test_tool_stream_overflow_keeps_rest_of_chunk(void);
/* one chunk: call 0's overflowing fragment, call 1's complete arguments, finish_reason "tool_calls"
   -> return 1, call 1's input intact, finish "tool_calls", call 0 overflowed */
int test_tool_stream_anthropic_overflow_sticky(void); /* the same splice through input_json_delta */
int test_tool_stream_web_fetch_url_not_spliced(void); /* a web_fetch "url" spliced the same way -> overflowed, no URL kept */
```

- [ ] **Step 2: Run `make -j test`.** Expected: failures in the new tests.
- [ ] **Step 3: Implement**, and in `ai_chat_stream.c`'s tool branch replace an `overflowed` call's input with `"{}"` before it is stored.
- [ ] **Step 4: Run `make -j test`**, **`make test SAN=1`** and **the Windows compile check.**
- [ ] **Step 5: Commit** (`fix(ai): an overflowing tool input can no longer splice into a different call`).

### Task A2: a take keeps the destination's model (spec §16; a defect today)

**Files:**
- Modify: `src/core/ai_prompt.c` (CRLF: `ai_conv_take`, ~165-176), `tests/test_ai_stream.c`, `tests/runner.c`

**Interfaces:** `ai_conv_take(dst, src)` keeps `dst->model` when it is non-empty, else takes `src`'s. Fixes today's revert of a model chosen in Settings on a tab switch (`ai_chat.c` ~2299) and on the background branch (~1162).

- [ ] **Step 1: Write the failing tests:** `test_ai_conv_take_keeps_destination_model` (dst "b", src "a" -> "b"; dst "" -> "a"); `test_ai_conv_take_background_path_model` (a session conversation with model "old", the panel's model "new": the switch's take into `d->conv` leaves "new").
- [ ] **Step 2: Run `make -j test`.** Expected: failures.
- [ ] **Step 3: Implement.** Keep `ai_prompt.c` CRLF.
- [ ] **Step 4: Run `make -j test`** and **the Windows compile check.**
- [ ] **Step 5: Commit** (`fix(ai): switching tabs no longer reverts the model chosen in Settings`).

### Task A3: the command list's core: `unfinished`, the run log, the summary text, the demo state (spec §21)

**Files:**
- Create: `src/core/cmd_run_log.h`, `src/core/cmd_run_log.c`, `tests/test_cmd_run_log.c`
- Modify: `src/core/chat_msg.h`, `src/core/chat_msg.c`, `src/core/cmd_batch.h` (`CmdBatch.reply_msg`), `src/core/ai_prompt.h` (CRLF: `AiSessionState.run_log`), `src/core/ui_demo.h`, `src/core/ui_demo.c`, `tests/test_chat_msg.c`, `tests/test_ui_demo.c`, `tests/runner.c`

**Interfaces:** the `chat_msg.h`, `cmd_run_log.h`, row-mapping, `CmdBatch.reply_msg`, summary-line and `ui_demo` items below (the UI items are Task A4's).

- `chat_msg.h`: `CHAT_RUN_UNFINISHED` appended to the `CHAT_RUN_*` enum; `chat_cmd_label` gives `"unfinished"`, `CHAT_CMD_INTENT_WARNING`, for `approved == 1` and that run; `chat_msg_batch_sync_run` maps `APPROVE_EXECUTING` with `batch_ending` to it (APPROVED with `batch_ending` stays `CHAT_RUN_ABORTED`, "not run").
- `cmd_run_log.h` (includes `cmd_batch.h`, no `windows.h`):

```c
#define CMD_RUN_LOG_MAX 16
typedef struct {
    char *command;                       /* heap copy */
    CmdSafetyLevel safety;
    int quoted;
    char reason[CMD_REASON_MAX];
    int approved, blocked, run;          /* chat_cmd_label's inputs as the row ended */
} CmdRunRow;
typedef struct { int reply_msg; uint32_t turn_hash; int n; CmdRunRow rows[APPROVAL_MAX_CMDS]; } CmdRunRecord;
typedef struct { CmdRunRecord *rec[CMD_RUN_LOG_MAX]; int count; } CmdRunLog;   /* oldest first */
void     cmd_run_log_init(CmdRunLog *log);
uint32_t cmd_run_log_turn_hash(const char *text, size_t len);   /* FNV-1a 32 */
void     cmd_run_row_from_entry(const ApprovalEntry *e, int batch_ending, CmdRunRow *out); /* command not copied */
int      cmd_run_log_record(CmdRunLog *log, const CmdBatch *batch, uint32_t turn_hash, int batch_ending);
         /* 0 recorded; -1 for NULL, a tool batch (cmd_batch_is_tool), an empty queue or OOM (nothing recorded);
            a 17th record frees and drops the oldest */
void     cmd_run_log_free(CmdRunLog *log);                      /* frees every record, then init */
```
- Row mapping (`cmd_run_row_from_entry`): COMPLETED -> approved 1, run DONE; EXECUTING -> approved 1, run RUNNING, or UNFINISHED when `batch_ending`; APPROVED -> approved 1, run NONE, or ABORTED when `batch_ending`; DENIED -> approved 0; PENDING and BLOCKED -> approved -1, blocked 0 ("skipped"); `quoted` copied (it wins in `chat_cmd_label`).
- `AiSessionState` gains `CmdRunLog run_log;`: `cmd_run_log_init` beside each `cmd_batch_set_init` (`window.c` ~1032, `ai_chat.c` ~525, ~2528), `cmd_run_log_free` beside each `cmd_batch_set_free` (`window.c` ~1139, New Chat ~524, the demo reset ~2527).
- `CmdBatch` gains `int reply_msg;` (the producing assistant reply's index, set beside `conv_mark` at `ai_chat.c` ~1218 and ~1437 and, in Part B, Task B15's batch builder; spec §21: the displayed branch adds NOTEs before `conv_mark`, so `conv_mark - 1` is not the reply). `CmdRunRecord` holds `reply_msg` in place of `conv_mark`.
- UI (`ai_chat_internal.h`): `void ai_chat_batch_remove_recorded(AiChatData *d, AiSessionState *owner, CmdBatch *batch, int batch_ending);` records with the hash of the assistant turn at `batch->reply_msg` of `ai_chat_owner_conv(d, owner)` (0 when that is not an assistant turn), then `cmd_batch_remove`. It replaces the plain `cmd_batch_remove` of a command batch in `ai_chat_settle_batch_if_done`, the tick's end, `ai_chat_dispatch_cancel` and (in Part B) Task B16's `ai_chat_runcmd_finish`; before `cmd_batch_add` on a full set (`CMD_BATCH_MAX`), the oldest command batch is recorded (`batch_ending` 1) before the add evicts it. Tool batches keep trust's paths.
- Rebuild (`ai_chat_rebuild_display`): after appending message `i`, for each record with `reply_msg == i`, message `i` an assistant turn and `turn_hash` equal to its content's hash, append its rows as settled `CHAT_ITEM_COMMAND` items (`chat_msg_set_command`, `_set_quoted`, `_set_reason`, `approved`, `run`, `settled = 1`, batch id `-(record index + 1)` so no live sync touches them) and, for that reply, show only its pre-command text (`ai_response_split`, as live) and, once Part B exists, none of Task B18's "using tool: run_command" rows: each command appears once. A record that does not match (Retry popped its turn, a new reply took the index) is not shown. Live batches are appended at the end as today.
- Summary line (spec §21 (d)), `chat_msg.h`: `typedef struct { int n, ran, running, unfinished, queued, awaiting, held, not_run, denied, skipped, quoted, decided; } ChatBatchCounts;` `int chat_msg_batch_counts(const ChatMsgList *list, int batch_id, ChatBatchCounts *out);` (each row by `chat_cmd_label`'s rule; a row still on the card counts `awaiting`, or `held` when blocked; `decided` counts rows off the card); `int chat_batch_summary_text(const ChatBatchCounts *c, char *buf, size_t cap);` returns 0 and `""` unless `n >= 2 && decided >= 1`, else writes `"<n> commands: "` (n is at least 2) and the non-zero parts in the order ran, running, unfinished, queued, awaiting approval, held, not run, denied, skipped, quoted, as `"<count> <word>"` joined by `", "`, and returns the length (0 and `""` when it does not fit). The painter (`chat_listview.c`) draws it directly under a batch's last item in list order (its last settled row, or the card's button row when that item is a row still on the card; spec §21 (d)) with `ns_font(FONT_CAPTION, dpi)`, `ns_tokens()->text_dim`, `SP_XS` above, at the settled row's text inset; the row's measured height includes that caption line. Replayed records use batch id `-(record index + 1)`, so each gets its own line and no live sync matches it (live ids start at 1).
- `ui_demo`: state `"runlist"` (valid, listed by `ui_demo_states`, not part of `"all"`): a conversation of two replies; the live batch holds ran, running, queued, queued (line `4 commands: 1 ran, 1 running, 2 queued`), and the recorded batch, built with `batch_ending`, holds unfinished, not run, denied (line `3 commands: 1 unfinished, 1 not run, 1 denied`); `void ui_demo_run_log(const char *state, CmdRunLog *log, const AiConversation *conv);` fills one record under the first reply for `"runlist"` and nothing for any other state.

- [ ] **Step 1: Write the failing tests.**


```c
/* tests/test_chat_msg.c */
int test_chat_cmd_label_unfinished(void);           /* approved 1, CHAT_RUN_UNFINISHED -> "unfinished", WARNING */
int test_chat_msg_batch_sync_run_executing_ending(void); /* EXECUTING + ending -> UNFINISHED; APPROVED + ending -> ABORTED */
int test_chat_batch_summary_order(void);    /* one of every state -> "10 commands: 1 ran, 1 running, 1 unfinished, 1 queued,
                                               1 awaiting approval, 1 held, 1 not run, 1 denied, 1 skipped, 1 quoted" */
int test_chat_batch_summary_zero_omitted(void); /* ran 2, queued 1 -> "3 commands: 2 ran, 1 queued" */
int test_chat_batch_summary_no_line(void);  /* n 1 (decided) -> 0 ""; n 3 all on the card -> 0 ""; n 0 -> 0 ""; NULL -> 0 */
int test_chat_batch_summary_head(void);     /* n 2 -> starts "2 commands: "; n 1 never gives a line, so "1 command" never appears */
int test_chat_batch_summary_cap(void);      /* cap 10 -> 0 and ""; exact-fit cap -> full text */
int test_chat_msg_batch_counts_rows(void);  /* a list of 5 rows in batch 3 and 2 in batch 4 -> counts per batch only; card rows awaiting/held */
/* tests/test_cmd_run_log.c */
int test_cmd_run_row_every_state(void);   /* each ApprovalStatus x batch_ending 0/1 -> the mapping above; chat_cmd_label of each
                                             gives ran, running, unfinished, queued, not run, denied, skipped, quoted, not run */
int test_cmd_run_log_record_copies(void); /* 3-entry batch -> n 3, commands copied (source changed after: copy unchanged), reply_msg kept */
int test_cmd_run_log_refusals(void);      /* NULL log or batch, tool batch, empty queue -> -1, count unchanged */
int test_cmd_run_log_drops_oldest(void);  /* 17 records -> count 16, rec[0] is the 2nd; no leak under SAN=1 */
int test_cmd_run_log_free(void);          /* free -> count 0; free twice safe */
int test_cmd_run_log_turn_hash(void);     /* "" and "abc" fixed FNV-1a values; one byte changed -> different */
/* tests/test_ui_demo.c */
int test_ui_demo_runlist_rows(void);      /* labels of the live and recorded rows, in order, and both summary lines verbatim */
int test_ui_demo_runlist_record_matches_turn(void); /* the record's reply_msg is an assistant turn whose hash matches */
int test_cmd_run_log_exec_note_before_mark(void); /* an [EXEC] reply with a rejected marker: NOTE added, then conv_mark taken;
                                                     reply_msg is the reply, not conv_mark - 1; the replay lands under it */
```

- [ ] **Step 2: Run `make -j test`.** Expected: compile failure.
- [ ] **Step 3: Implement** (`chat_msg.c`, `cmd_run_log.c`, `ui_demo.c`; `ai_prompt.h` stays CRLF). A copy that fails to allocate records nothing (spec §25): `test_cmd_run_log_oom_records_nothing` with `xmalloc`'s test hook.
- [ ] **Step 4: Run `make -j test`** and **`make test SAN=1`**: all pass.
- [ ] **Step 5: Commit** (`feat(core): the command list's run log and summary text`).

### Task A4: the command list in the panel (spec §21; `src/ui` only, native coverage from A3's `ui_demo` tests)

**Files:**
- Modify: `src/ui/ai_chat_internal.h` (`ai_chat_owner_conv`, `ai_chat_batch_remove_recorded`), `src/ui/ai_chat_dispatch.c`, `src/ui/ai_chat_view.c`, `src/ui/ai_chat.c` (`reply_msg` beside `conv_mark` ~1218, ~1437; New Chat; the demo; eviction), `src/ui/chat_listview.c` (the summary caption), `src/ui/window.c` (session init ~1032 and free ~1139), `tests/integration/cases/30-ui-demo.ps1` (`$galleryStates` gains `"runlist"`)

- [ ] **Step 1: Wire it** as A3's UI items say: `AiConversation *ai_chat_owner_conv(AiChatData *d, AiSessionState *owner);` (`&d->conv` when displayed, else `&owner->conv`); every removal of a command batch goes through `ai_chat_batch_remove_recorded` (`ai_chat_settle_batch_if_done`, the tick's end, `ai_chat_dispatch_cancel`, and before `cmd_batch_add` evicts the oldest command batch of a full set); the rebuild replays matching records after their reply (batch id `-(record index + 1)`, the `[EXEC]` reply's pre-command text only); the painter draws the summary under the batch's last item. `grep -n 'cmd_batch_remove(' src/ui/*.c`: every hit on a command batch is inside the helper; the rest are trust's tool-batch paths. No new token, size, font, timer or hover id.
- [ ] **Step 2: Run `make -j test`** (`test_ui_tokens` green) **and the Windows compile check.** `--ui-demo=runlist` and `ui_gallery` run on the Windows box (no `pwsh` on `automaton`).
- [ ] **Step 3: Commit** (`feat(ai): list which commands ran and which wait, with a summary per batch, kept after a rebuild`).

### Task A5: Part A's verification (a cut point)

- [ ] **Step 1:** Each alone: `make -j test` (count against 3695 plus Part A's tests), `make test SAN=1`, `make lint-gate`, `make fuzz && make fuzz-run`. The tree is shippable here: if the maintainer ships Part A alone, run Task V now; otherwise go on to Part B.

## Part B: the `run_command` tool

### Carried from Part A (built and reviewed; head `cec497a`, 3727 native tests, 0 failed)

Part A ended with these interfaces, some beyond what Tasks A1 to A4 name. Part B uses them and duplicates none:

- `CmdBatch.reply_msg`, `CmdBatch.reply_hash` (the reply's hash, taken when the batch is created) and `CmdBatch.settled` (set only by `ai_chat_batch_settle_rows(d, batch)`, the one place the live view settles a batch's rows; the rebuild reads the flag).
- `int cmd_run_reply_index(int stored, int msg_count, int last_is_assistant)`; `AiResponseMsg.reply_stored` (the stream thread's own `ai_conv_add` result, passed through `post_response`).
- `cmd_run_log_record(log, batch, turn_hash, batch_ending)`, `cmd_run_record_matches(rec, idx, is_assistant, content, len)`, `cmd_run_log_forget(log, reply_msg)`, `cmd_run_batches_unkey(set, reply_msg)`, `cmd_batch_finished(b)`, `chat_msg_batch_counts` with `chat_batch_summary_text`, `ai_chat_batch_remove_recorded`, `ai_chat_batch_record_oldest_if_full`, `ai_chat_owner_conv`. `CmdRunRow` carries the command's note.

Four things Part A's reviews found that Part B must do, each in the task named:

1. **Task B15:** the batch builder sets `reply_hash` as well as `reply_msg`, through `cmd_run_reply_index`. A run_command reply that is only a tool call has empty content, so its hash mixes in the calls' ids (in order) as well as the content; the rebuild computes the same hash for an assistant tool turn. Without this every empty assistant turn at one index hashes alike (Review Focus 6). Put the hash in one core function used by both sides, with native tests: same content and different ids differ; same ids match; a plain `[EXEC]` reply hashes exactly as it does at `cec497a`.
2. **Task B18, first step:** move `ai_chat_append_batch_command_items` (`src/ui/ai_chat_view.c`; it touches no Win32) into `src/core` beside `chat_msg`, and give it the regression tests Part A could not have: a settled batch stays settled after a ceiling raise turns its BLOCKED rows PENDING; a card stays a card after a ceiling drop leaves every row BLOCKED; a half-decided card rebuilds with its decided rows on the card.
3. **Task B18:** the rebuild appends every "using tool" row before it knows whether a run-log record matches (`ai_chat_view.c`, the block before `has_record`). Reorder it, or make the tool row conditional on the match, so a command is never shown twice. This also covers a hidden-tab reply whose `[EXEC]` text sits in a non-final tool round: that turn's text is drawn split at the first marker, as the live view draws it.
4. **Every Part B task that builds a request:** never build one from `ai_chat_owner_conv()` of a background owner and trust its `model`. After Task A2 a hidden session's `conv.model` is set once and not refreshed; take the model from the settings at launch, as `ai_chat_launch_stream_thread` does.

Also for Task B16 (`WM_DESTROY`): undocking the panel while a command runs sets `dispatch_active = 0` but neither cancels nor records the batch, so after a redock its row may read "running" for good. This predates the branch; the finish function ends that batch with its cause ("while dispatching", spec §13) for `[EXEC]` batches too, and the final review checks it.

### Carried from fix round 3 (2026-11-11)

- **H1, DeepSeek (review item I1):** spec decision 23's `reasoning_content` and its refusal of a cut reasoning apply only to a `deepseek` request whose tools array offers `run_command`; any other DeepSeek request is byte-identical to `6432ca0` (four pinned bodies). The reject hint and the single-delta `thinking_cut` false positive are unchanged and wait for the DeepSeek settling test (spec §23).

- **H3, rebuild parity (review M3, B18 minor):** a rebuilt reply shows the rows its answer showed live. `CmdRunRecord` (`cmd_run_log.h`) gains `no_card` (set by `cmd_run_log_record_status`'s new last argument, 1 for a reply answered without a card, 0 for a card's results), `quoted` (the markers shown as "quoted, not run" that landed on no card, from the new `cmd_run_log_record_quoted`) and `after_rows` (the status rows of a card's OVER_CAP or queue-refused calls, taken from the batch's `runcmd` by `cmd_run_log_record`). The row texts are `runcmd_unqueued_row` and `runcmd_quoted_row` (`run_command.c`), used by the live view (`ai_chat.c`) and `chat_rebuild_replay`, which also draws a live batch's not-run rows after its card. Rebuild order: the reply's text up to its first marker, the record's command rows each followed by its `after_rows`, the status rows, then the quoted rows (the live order of an answer without a card); a `no_card` reply draws none of its run_command calls (live drew none). A record made before this change has none of the fields and rebuilds as it did (`test_rebuild_replay_record_without_the_new_fields_rebuilds_as_before`). An allocation failure of the larger record stores nothing (as R64).

### Carried from the review of B1 to B11 (built, reviewed and fixed; code head `1802305`, then the FW6 docs commits)

Tasks B1 to B11 and five fix waves (FW1 to FW5) are built. The review found what B12 to B22 will trip over; the items below are in the task that owns each (rulings R12 to R53 are in the ledger, `progress.md`; the ones a later task acts on are below, and a ruling marked ★ there was decided in the second fix round: R46, R47, R48 and R50 each change a step, R49, R51, R52 and R53 refine one). Step texts of B2, B3, B4, B7, B8, B11 and B12 to B18 below are corrected to match; a correction is marked "(corrected by the review of B1-B11)". Signatures are at HEAD; read the headers, not this list, when they differ.

**Signatures that changed or are new** (every caller in `src/ui` is B12 to B17's):
- *(fix round 3, H4, 2026-11-11)* `AiStream.snapshot` (it was named in the Global Constraints above as a Trust provision; struck from that list, 2026-11-11) is gone: nothing set or read it after R55 (the frozen text lives in `conv.snapshot`), and `test_ai_stream_snapshot_freed` went with it.
- `ai_conv_room_for_send(conv, open_batch, const AiNoteQueue *q)` and `agentic_room_for_tool_round(conv, const AiNoteQueue *q)` count `q->count` (NULL counts 0); `ai_conv_flush_notes(conv, q, keep_free)` leaves `keep_free` slots free (R47: a flush that precedes a relaunch leaves 1 for the reply, the typed message's finish 2, any other flush 0) and keeps a note that does not fit, or would take one of them, in order, at the front of `q` for the next flush, and returns the number appended; `ai_conv_add_user_note(conv, q, defer, keep_free, text)` (R54) flushes with `keep_free + 1` itself and returns 0 stored, 1 queued (`defer` or an open call), 2 conversation full (the text neither stored nor queued, never stored ahead of a queued note), -1 bad arguments or a full queue.
- *(fix round 3, 2026-11-11)* `agentic_room_for_finish(conv, q)` (`ai_agentic.h`) is the run_command finish's room check, with the assistant tool turn already stored: `msg_count + 2 + q->count <= AI_MAX_MESSAGES`; B16's finish uses it in place of `agentic_room_for_tool_round` (which stays the stream thread's check before the store, `+ 3`). `ai_chat_send_tool_results(d, unattended)` returns 1 when a stream is in flight, 0 when nothing launched; the finish then takes its no-relaunch tail (next batch, activity reset). The error branch of `WM_AI_RESPONSE` flushes the owner's queued notes with `keep_free` 0 (Stop, the reaper and Retry's abort already flushed through `ai_chat_abort_session_stream`).
- `agentic_add_tool_results` returns 0 stored, 1 notes-only stored (an allocation failed), -1 full, -2 even the notes-only array failed; on -1 and -2 nothing is stored and the open assistant turn is dropped (R37), so the conversation passes `ai_conv_tool_pairs_well_formed` after every return. Each part's `tool_call_id` and `tool_name` come from the stored call its result's `tool_use_id` names (when no earlier result has that id too); only for an id the store made up (`call_<n>`) is a result paired by position; any other result keeps its own id, so a results array that is wrong or out of place fails the pair check (R50, replacing R39's positional pairing).
- `agentic_add_assistant_tool_msg_ex` replaces an empty or repeated call id with `call_<n>` (smallest unused in the turn and conversation) and stores it, so an id-less call is sent as `call_<n>`, not `"id":""` (the one intended byte change to a request body, spec §1); read ids from the stored turn, never from `pending_tool_calls`. The pair check rejects a repeated id.
- `AiMessage.nutshell_note` (R40; set only by the note door); `ai_conv_has_snapshot(conv)`; `ai_conv_snapshot_trim(conv)`; `ai_conv_freeze_snapshot(conv, text, len, offers_runcmd)` returns 0 (frozen; also 0 with nothing kept when `text` is NULL or `len` is 0, the older snapshot being freed), 1 nothing changed (`conv` NULL, last message not USER, or a note while `offers_runcmd` is 1), -1 allocation failed (no snapshot kept) *(review of the fix wave, 2026-11-11, R48: the fourth parameter is new)*.
- `runcmd_capture_start(RunCmdCapture *, const Terminal *)` (const again: it writes nothing to the terminal, R46; the capture must be zero-initialised before its first start); `RunCmdCapture` gains `start_erase_seq`, `prompt[RUNCMD_PROMPT_MAX]`, `prompt_len`, `prompt_long`; `Terminal.erase_seq` (ED 2, and ED 0 straight after a CUP/HVP to the home position, primary screen only; no cursor move counts, and an ED 0 reached at home any other way does not: C0); `term_extract_cursor_prefix(term, out, cap)` (`term_extract.h`); `runcmd_note_drop_cut(note, cap)` (`run_command.h`).
- `JsonNode.str_len` and `Token.len` (decoded string length); `AI_TOOL_STREAM_CALLS_MAX` 64 (`ai_tools.h`).
- Capture reads: `RUNCMD_CAP_SCREEN` also covers an erase (ED 2, or ED 0 straight after a CUP/HVP to the home position, primary screen only) (R46, *review of the fix wave, 2026-11-11*; a cursor move above the start row is not a trigger); `RUNCMD_CAP_REDRAWN` goes with it. The cursor row's text follows R49 (as replaced by C0): ends with the recorded prompt, strip it and keep the rest; equals it, nothing; otherwise the whole text, a prompt that changed since the command started included as the output's last line. There is no `shell_prompt_line()` fallback and no PROMPT flag. A COMPLETED or EXECUTING call whose capture never ran says "ran; output not captured".

**Rulings the later tasks act on (one line each; R12 to R14, R16, R18, R19, R23, R25, R26, R28, R34 and R36 changed no later step):**
- R15 (B3's OOM fallback, B4; amended by R41): one heap array of n parts, no content, each part carrying its result's own note plus "output dropped: Nutshell ran out of memory" where that result had output (the phrase alone when the note was empty; a result with no output keeps its note unchanged; no static note); it is the notes-only answer, return 1. If the array cannot be had, nothing is stored (-2) and the open turn is dropped. B13/B16 treat a negative return as in R37.
- R17 (B4): `agentic_add_tool_results` drops the open turn on its last fallback; with R37 it is now every negative return.
- R20/R30 (B6/B7): `region_scroll_seq` moves only for primary-screen scrolls that shift rows in place (`term_scroll_up`'s in-place path and every `term_scroll_down`); the alternate screen never bumps it. B16 does nothing with it, but a capture across `less` reads the screen rows, flagged SCREEN.
- R21 (B8): `runcmd_budget` saturates at 0 for a small context (limit 8000); B16 passes it as is and a 0 budget gives notes-only results.
- R22 (B8): aging overwrites a part's heap content in place with the stub (no allocation; a shorter part stays); B16 calls `runcmd_age_results` right after the append, because the estimate already assumes it.
- R24 (B9): `runcmd_path_plan` is the only table for cause, append, relaunch, capture (1 except NEW_CHAT and SESSION_CLOSED) and `reset_rounds` (Stop and a typed message); `capture` 1 means the terminal may still exist, so guard `active_term`; B16/B17 read it, never a condition in `src/ui`, and never switch on `RunCmdPath` (the `RUNCMD_PATH_COUNT` sentinel trips `-Wswitch`).
- R27 (B13): a stream call index at or above 64 is skipped by the accumulator (OpenAI index and Anthropic block index); a reply may carry more than 16 calls, 64 at most, so OVER_CAP stays reachable. A second call on an explicit index with a different id marks that slot overflowed (M3).
- R29 (B7/B8): a capture whose excerpt was lost to an allocation failure has no head, no tail and no EMPTY; `omitted_bytes` = len when the failure was inside `runcmd_cut`, 0 when it was the read or join in `runcmd_capture_take` (nothing was measured); B8 need not tell the two apart and notes both "output dropped: Nutshell ran out of memory" (R31). No stored capture holds a control byte.
- R31 (B8): that wording is the capture's; "not run: Nutshell ran out of memory" is reserved for a call that truly did not run, which only B15's planner-failure path writes (R41).
- R32/R33 (B8): `ai_context_estimate_tokens` counts calls (id, name, input) and a per-part framing overhead and tracks the body within 10 % in every round; `runcmd_request_estimate` counts parts the coming append will age at the stub's size, so B16 passes the estimate before the append and the stub's size is already in it.
- R35 (B11): `turn_as_text` sends a stored tool turn as text in exactly three cases: (a) it holds a `run_command` call and the request's tools lack `run_command`; (b) the request has no tools array and the turn records a provider; (c) its recorded provider or model differs from the request's. A turn with an empty `tool_provider` and no `run_command` call stays structured. Every adder in `src/ui` passes a provider and model (`agentic_add_assistant_tool_msg_ex`); a NULL provider means a turn is judged by the tools array alone.
- R37 (B13/B16): the four return values above; the caller shows a row for 1, -1 and -2 and never drops the turn itself.
- R46 ★ (B7/B16; replaces R38): no cursor low-water mark. The screen fallback triggers on `Terminal.erase_seq` only (ED 2, and ED 0 straight after a CUP/HVP to the home position, primary screen; C0: not an ED 0 that reaches home by `\r ESC[1A ESC[J`, and a `clear` in any other form is not detected, spec §10); a program that homes and repaints without erasing is read from the start row down with no note (spec §10 limit). `runcmd_capture_start` takes a `const Terminal *` again, so captures of several calls can be armed in turn; start each just before its command's first write. R49 (B7/B8; replaced in C0): the cursor row's text left of the cursor is judged by three rules in order: it ends with the recorded prompt -> strip it and keep the rest; it equals it -> add nothing; otherwise add it whole. A prompt that changed since the command started (`ssh web1` leaving `deploy@web1:~$`, `python3` leaving `>>>`, `cd /tmp && printf foo` leaving `foou@h:/tmp$`, a status prompt `[1] $`) is the output's last line, because dropping it lost output with no trailing newline and hid a change of host or a REPL from the model; a progress line (`progress 50%`) is kept. A prompt recorded as empty never matches "ends with". R53 withdrawn (C0): no PROMPT flag; `RUNCMD_CAP_PROMPT` (32) does not exist and an empty final text is EMPTY.
- R39 (B13/B15/B16/B17): pass the owner's `&notes` to the room checks; made-up ids are in core, and results are paired by id there (R50), by position only for a made-up id.
- R40 (B12/B14/B17): for a request that offers run_command, freeze only on a typed USER turn (an `[EXEC]` request freezes a fresh snapshot on its last USER turn, note or not: R48); key on `ai_conv_has_snapshot`, never on `snapshot_msg` (a zero-filled conversation has 0); every Nutshell USER turn goes through the door so it is marked, the continue text that launches its own request included (R48: an `[EXEC]` request ignores the mark); call `ai_conv_snapshot_trim(conv)` after every pop.
- R41 (B15/B16): the notes-only answer keeps each result's note and adds "output dropped: Nutshell ran out of memory" only where there was output (return 1).
- R42 (all body builders): `json_validate`'s number scan follows the JSON number grammar, so a body with `1.` or `01` is refused where it was sent before (the PR body notes it).
- R43 (B1's parser, every `json_parse`): decoded string lengths are kept so `runcmd_parse_input` refuses an embedded `\u0000`; an object key holding `\u0000` no longer fails the parse (R52): the member is kept and `json_obj_get` never matches it.
- R48 ★ (B12/B14; replaces R44's narrower wording): a request that does not offer run_command is built exactly as today in every case: a fresh snapshot on the last USER turn, marked note or not. The keep-on-question rule and the freeze's refusal on a note apply only to a request that offers run_command. The builder decides from the request's tools (the same test `turn_as_text` uses); B12 passes `ai_chat_provider_runcmd(d)` to `ai_conv_freeze_snapshot` as its `offers_runcmd`, and B14 routes every Nutshell USER turn through the door, the launching continue text included (an `[EXEC]` request ignores the mark). Pinned by the two extra Moonshot bodies (a conversation ending on a NOTE, a plain `[EXEC]` exchange).
- R45 (B13/B16): the stream thread's tool-round room check cannot see the owner's note queue and passes NULL (it never reads the queue across threads); the UI-side checks pass `&owner->notes`, and the reply's slot is kept at the flush (R47).
- R47 ★ (B13/B14/B16/B17): `ai_conv_flush_notes(conv, q, keep_free)` leaves `keep_free` slots free: a flush that precedes a relaunch leaves 1 (the reply), the typed message's finish leaves 2 (the typed message and its reply), a flush that precedes nothing (the abort's) 0. Notes that do not fit stay queued, in order, for the next flush. **The launching `[EXEC]` continue text does not flush on its own** (C0, R54; this replaces "R47 (amended by G2): launching continue text flushes with keep_free 2"): it goes through the door `ai_conv_add_user_note(conv, q, defer, 1, text)`, whose flush leaves the text's slot and the reply's; on 2 nothing launches and the caller shows `"[conversation full: ...; start a New Chat]"` like a typed message (spec decision 24); every other note passes `keep_free` 0.
- R50 ★ (B13/B16): results are paired with calls by `tool_use_id` (exactly one stored id, not claimed by an earlier result); by position only where the store made the id up (`call_<n>`); otherwise the result keeps its own id and the pair check fails. So a results array in a different order from the calls, with matching ids, pairs by id and passes; wrong, repeated or out-of-place ids fail the pair check. Everything that depends on which call a result answers (the part's name, the run_command "no second cap" decision in `parts_from_results`) reads the paired stored entry, never `calls[i]` (C0).
- R51 (B1 accumulator): with a defaulted (absent) stream index, a non-empty id different from the open slot's opens the next slot; only an explicit index reused with a different id overflows.
- R52 (B1 parser, narrows R43): a key containing `\u0000` is kept with its decoded length and never matched by `json_obj_get` (the serialiser writes it back whole); it no longer fails the parse. `\u0000` in the run_command command still gives NO_COMMAND.

**B12**
- `frame.snapshot_at` is 0 in `ai_chat_send.c:146` and `ai_chat_stream.c:732` today. Set it to `snapshot_msg + 1` from the frozen copy, with `frame.snapshot = NULL` and `snapshot_at` 0 when `!ai_conv_has_snapshot(&conv)` (a resume with nothing frozen): a fresh snapshot with `snapshot_at` 0 lands in the question of a request that offers run_command, which spec §22 forbids there (a request without run_command is built as today: a fresh snapshot on its last USER turn). Both sites, so the in-stream rebuild agrees with the launch.
- Take a fresh snapshot only when the last message is a USER turn (R48): `ai_conv_freeze_snapshot(&d->conv, ..., ai_chat_provider_runcmd(d))` returns 1 for a note only when the launch offers run_command, so a launch after a note keeps the old snapshot on its question there, and a launch without run_command (`[EXEC]`) freezes a fresh one onto its last USER turn, as today. `ai_conv_freeze_snapshot` returns -1 when an allocation fails (its header says so): launch without a snapshot rather than fail.
- A snapshot an `[EXEC]` launch froze onto a note does not carry over: the freeze refuses a note only when `offers_runcmd` is 1, so after an `[EXEC]` launch (or a continue launch) has frozen a snapshot onto a Nutshell note, a later launch that offers run_command with no typed turn since gets 1 from the freeze, and the builder (which keeps a snapshot only on a typed turn) would drop the snapshot sitting on the note. B12 must handle it: when the freeze returns 1 and the frozen snapshot sits on a note, call `ai_conv_snapshot_trim` and launch without a snapshot (nothing typed since the launch that froze it); freezing onto an earlier typed turn would need a new API and would put newer screen state before the results, so it is not done; a test pins this. B12's test also asserts that the freeze's `offers_runcmd` and the built tools array agree: a launch whose `ai_chat_provider_runcmd(d)` is 1 has `run_command` in its tools array, one whose is 0 does not, so the freeze and the builder never disagree about the request.
- The body builder returns 0 for three causes (pairing, overflow, allocation) and the in-stream rebuild says only "JSON rebuild failed": call `ai_conv_tool_pairs_well_formed` first and pick the row from it; the launch's row for the other two is the overflow or "out of memory" text, never "JSON build failed".
- A continue launch freezes a fresh snapshot on its continue text, as today: it passes `offers_runcmd` 0 whenever its request does not offer run_command, and the continue text's mark changes nothing there (R48).

**B13**
- The plan's `agentic_room_for_tool_round(&s->conv, K)` is wrong: the second argument is the note queue (`const AiNoteQueue *q`), not the call count K. The stream thread must not read the owner's queue (the UI appends to it while the stream runs, and it is 16 KB), so the thread passes `NULL` (notes that arrive later stay queued: `ai_conv_flush_notes` keeps what does not fit, and the reply's slot is kept at the flush: a flush that precedes a relaunch leaves 1 free, R47); the UI-side checks in B15, B16 and B17 pass `&owner->notes`.
- **Step 1 is required before any build.** Its last part (the stream thread acting on `agentic_add_tool_results`' return value: -1 or -2 ends the loop with the "conversation full" row, 1 posts the status row) must be in place first: today `ai_chat_stream.c` ~700 ignores the return, and on "full" (-1) nothing is stored and the open turn is dropped, so the web loop would repeat the same tool up to `AI_TOOL_LOOP_MAX` (5) times, each round running the tool again and failing to store it.
- Replace the old overflow loop at `ai_chat_stream.c` ~486-524 with the sanitise call, keeping its DLOG.
- `agentic_add_assistant_tool_msg_ex` makes up ids (I4); read each call's id back from the stored turn (`conv.messages[last].tool_calls`), never from `pending_tool_calls`, when posting rows or heap-copying the calls into the response.
- Pass the provider and model to every adder. The stream accumulator now bounds calls at 64 (R27).
- `agentic_add_tool_results` for web tools: a negative return ends the loop with the "conversation full" row; the turn is already dropped (R37), so do not call `ai_conv_drop_open_tool_turn` as well. A return of 1 shows a status row ("[some results were reduced to notes: Nutshell ran out of memory]").

**B14**
- No `src/ui` site calls `ai_conv_add_user_note` yet: the policy-raised note (`ai_chat_status.c` ~67), the NOTEs in `ai_chat.c` (~1279-1297, ~1415-1446), the policy-blocked note (~1565) and the `[EXEC]` continue text (`ai_chat_send.c` ~578) still use `ai_conv_add`, so none is marked and the snapshot fix (I6) is inert until each goes through the door. Clear `AiSessionState.notes` at New Chat and session reset.
- Route the `[EXEC]` continue text that launches its own request through the door like every other Nutshell USER turn (R48; it is marked, and an `[EXEC]` request ignores the mark, so its body and fresh snapshot are as today); one the door queues behind an open run_command turn rides the relaunch.
- A note the door queues (`defer`, or a run_command turn open: it returns 1) is never dropped and the caller shows nothing extra. A return of 2 (R54, C0) means the conversation is full and nothing was stored or queued: for the launching continue text (passed `keep_free` 1) the caller launches nothing and shows the conversation-full notice of spec decision 24; any other note (`keep_free` 0) is simply not stored, and the next typed message gets the same notice from `ai_conv_room_for_send`.

**B15**
- `runcmd_plan` returns NULL both for out of memory and for no calls; guard it with `runcmd_reply_has_call(calls, n)` first, or a call-free reply is answered "may call no other tool". On a NULL plan with calls, and when `cmd_batch_add` returns NULL, the taken tool turn stays open: answer every call (planner failure: each call "not run: Nutshell ran out of memory", R41) with `agentic_add_tool_results` from a heap array of `n_calls` results, or, when even that fails, `ai_conv_drop_open_tool_turn`. The core has no non-allocating helper.
- `runcmd_cap_bytes` takes the number of SLOT calls (`r->n_slot`), not `n_calls`.
- **Read call ids from the stored assistant turn** (`conv.messages[last].tool_calls`), never from the provider's original calls (`rmsg` / `pending_tool_calls`), which may carry an empty or repeated id the store replaced with `call_<n>` (I4, R50). The batch builder, the card and `runcmd_plan` use the stored ids. B15 or B16 gains a native test of an **id-less `run_command` call** (empty id): the turn stores (`call_1`), the plan pairs, the results store with `tool_call_id` `call_1`, `ai_conv_tool_pairs_well_formed` is 1, and a request body builds from the result (C0).

**B16**
- Every `runcmd_capture_*` and `term_extract_*` call stays on the UI thread (`term_process` runs only there, `window.c` ~4867-4875 and ~3740; no lock guards a `Terminal`); a worker gets copies.
- **Read call ids from the stored assistant turn**, never from the provider's original calls, which may be empty (C0; see B15): the finish builds results, pairs and ages with the stored ids, so an id-less `run_command` call stores, pairs and builds a request. If B15 does not carry the native id-less test, B16 does.
- `runcmd_age_results` has no caller yet: call it right after the append (R22). Pass the stored assistant turn's calls in the same order as the results, or no part is recognised as run_command (not framed, not aged, not exempt from the cap).
- `out[64]` of `AiToolResult` is about 40 KB: keep it on the heap, and free each `out[i].content` after the store.
- Set `user_acted` for Retry and Stop as well as a typed message and New Chat. Guard `d->active_term == NULL` on SESSION_ENDED and PANEL_CLOSED (no capture then) and use the owner's conversation, never `d->conv`.
- "Never captured" is now "ran; output not captured" in core; the finish needs nothing for it. Pass the owner's `&owner->notes` to the room checks (I3, done in core).
- Handle `agentic_add_tool_results` as R37: 1, -1 and -2 each show a status row; the batch is still removed and rows settled.
- Check room with the owner's queue before any relaunch (R45: the stream's tool-round check cannot see the queue), and flush leaving 1 free before it (R47); a flush that precedes a relaunch leaves 1, notes that do not fit stay queued.

**B17**
- `ai_conv_room_for_send(&d->conv, batch != NULL, &owner->notes)`: the typed message counts the queued notes the finish flushes; the finish's flush leaves 2 free (R47).
- Every pop (Retry's `ai_msg_free(&messages[--msg_count])` and any other) is followed by `ai_conv_snapshot_trim(&d->conv)`.
- Retry and New Chat: `runcmd_path_plan` decides, never `switch (RunCmdPath)`.

**B18**
- The rebuild ignores a parts-less tool message and shows no part notes: show both. `agentic_truncation_warning(name, 0, ...)` says "truncated at 64 KiB", so the rebuild passes the part's real `omitted_bytes`, and for a part with no count (0) shows the row only when its note holds `AI_TOOL_TRUNC_NOTE`.

**B19, B20, B21, B22**
- B20/B21: turning on Gemini or Custom exposed I4 at once; core now makes up ids, so no UI step. B20's accumulator change keeps the 64-call bound (R27) and `agentic_add_assistant_tool_msg_ex` keeps the made-up-id rule when it gains `thought_sig`.
- B19: `runcmd_request_estimate` is the one place reasoning is counted; the aging stub and `runcmd_note_drop_cut` do not touch it.
- B22: the README states Part B's behaviour only; the notes-only wording and the "output not captured" note are not user-visible docs.

### Task B1: the tool, its parser and the provider predicates

**Files:**
- Create: `src/core/run_command.h`, `src/core/run_command.c`, `tests/test_run_command.c`
- Modify: `src/core/ai_prompt.h`, `src/core/ai_prompt.c` (CRLF), `tests/test_ai_tools.c`, `tests/runner.c`

**Interfaces:**
- Produces (`run_command.h`, includes `ai_tools.h`, no `windows.h`):
  - `#define RUNCMD_TOOL_NAME "run_command"`, `#define RUNCMD_COMMAND_MAX 1023`, `#define RUNCMD_NOTE_MAX 256`
  - `void runcmd_tool_def(AiToolDef *out);` name, description and schema exactly as spec §1; `safety = TOOL_CRITICAL`; `execute` a stub that fails with `"run_command is handled by the panel"`.
  - `int runcmd_register(AiToolRegistry *reg);` 0 or -1 (`ai_tools_register`).
  - `typedef enum { RUNCMD_PARSE_OK, RUNCMD_PARSE_NO_COMMAND, RUNCMD_PARSE_TOO_LONG, RUNCMD_PARSE_UNSAFE } RunCmdParse;`
  - `RunCmdParse runcmd_parse_input(const char *input_json, char command[RUNCMD_COMMAND_MAX + 1], char note[RUNCMD_NOTE_MAX]);`
  - `const char *runcmd_parse_error(RunCmdParse p);` the three spec §2 texts; `""` for OK.
  - `int runcmd_describe(const char *input_json, char *out, size_t cap);` 1 and the command, or 0 and `""`.
  - In `ai_prompt.h` beside `ai_provider_supports_tools` (`ai_prompt.c` ~1445): `enum { AI_RUNCMD_AUTO = 0, AI_RUNCMD_ON = 1, AI_RUNCMD_OFF = 2 };` `int ai_runcmd_auto_default(const char *provider);` the one table (anthropic, openai, moonshot 1; deepseek, gemini, custom and anything else 0). `int ai_provider_tools(const char *provider, int mode);` anthropic, openai, deepseek, moonshot 1; gemini and custom 1 only when `mode == AI_RUNCMD_ON`; `ai_provider_supports_tools(p)` becomes `ai_provider_tools(p, AI_RUNCMD_AUTO)`. `int ai_provider_runcmd(const char *provider, int mode);` OFF 0; ON `ai_provider_tools(provider, AI_RUNCMD_ON)`; AUTO `ai_runcmd_auto_default(provider)`.
  - `int runcmd_reject_hint_applies(int http_status, const char *error_text);` 1 only for 400 or 422 whose text contains, case-insensitively, `tool`, `function` or `reasoning_content`; `const char *runcmd_reject_hint(void);` the spec §1 row.
  - Fuzz: `tests/fuzz/fuzz_runcmd.c` (`FUZZ_TARGETS` in the Makefile, CRLF, gains `runcmd`) feeds `runcmd_parse_input` and the OpenAI and Anthropic tool-stream accumulators (A1's sticky overflow), checking no input is ever longer than `AI_TOOL_INPUT_MAX` and an overflowed call stays overflowed; hand-written seeds under `tests/fuzz/corpus/runcmd/` only.
- Schema (exact): `{"type":"object","properties":{"command":{"type":"string","description":"one single-line command, at most 1023 bytes"},"note":{"type":"string","description":"one line for the user: what the command does and why"}},"required":["command"]}`

- [ ] **Step 1: Write the failing tests** (`tests/test_run_command.c`, `tests/test_ai_tools.c`).

```c
int test_runcmd_parse_ok(void);        /* {"command":"ls -la","note":"list"} -> OK, "ls -la", "list"; no note -> note "" */
int test_runcmd_parse_no_command(void);/* NULL, "", "{}", {"command":""}, {"command":"   "}, {"command":5}, "[1]", "{\"command\":\"ls" -> NO_COMMAND */
int test_runcmd_parse_length(void);    /* 1023-byte command -> OK; 1024 -> TOO_LONG */
int test_runcmd_parse_unsafe(void);    /* "ls\nrm x", "a\tb", "a\x1b[0m", "a\xE2\x80\xAEb" -> UNSAFE */
int test_runcmd_parse_note_cut(void);  /* 300 x "é" note -> strlen <= 255, valid UTF-8, no partial sequence;
                                          note "a\nb" -> note "" and still OK */
int test_runcmd_parse_error_texts(void); /* the three spec §2 strings verbatim */
int test_runcmd_describe(void);        /* {"command":"uptime"} -> 1 "uptime"; "{}" -> 0 "" */
int test_runcmd_auto_default_table(void); /* the six rows exactly; one line flips one */
int test_provider_runcmd_modes(void);  /* OFF: every provider 0 while ai_provider_tools keeps its value; ON: all six 1 (gemini from Task B20);
                                          AUTO: the table; ai_provider_supports_tools unchanged for each; NULL and "other" 0 */
int test_runcmd_reject_hint(void);     /* 400 "tools is not supported" 1; 422 "Function calling" 1; 400 "reasoning_content" 1;
                                          401, 402, 429 0; 400 "maximum context length" 0; status 0 (in-stream "overloaded") 0 */
/* test_ai_tools.c */
int test_runcmd_serialized_both_formats(void);
/* registry with runcmd_register: ai_tools_validate_schemas == 0; anthropic output contains
   "\"name\":\"run_command\"" and "\"input_schema\""; openai output contains
   "\"type\":\"function\"" and "\"required\":[\"command\"]" */
int test_runcmd_stub_is_error(void);   /* ai_tool_execute on run_command -> is_error 1, content or tool_note names "handled by the panel" */
```

- [ ] **Step 2: Run `make -j test`.** Expected: link failure on `runcmd_parse_input`.
- [ ] **Step 3: Implement.** Parser: `json_parse` + `json_obj_str`; trim nothing; blank means only spaces and tabs; unsafe via `text_has_unsafe_command_char()` (`paste_filter.h`); note cut on a UTF-8 boundary, dropped when unsafe. Keep `ai_prompt.c` CRLF.
- [ ] **Step 4: Run `make -j test`.** Expected: all pass, warnings 0.
- [ ] **Step 5: Commit** (`feat(core): run_command tool definition, input parser and provider predicates`).

### Task B2: the Anthropic cut-off, input sanitising, room for a tool round (spec §2, §4)

**Files:**
- Modify: `src/core/ai_prompt.c` (CRLF: `ai_stream_chunk_cut_off` ~1427), `src/ui/ai_chat_stream.c` (~158's prefilter), `src/core/ai_agentic.h`, `src/core/ai_agentic.c`, `src/core/ai_tools.h` (`AI_TOOL_PART_NOTE_MAX` 512 for `AiToolResult.tool_note`; `AiMessage.tool_note` stays 256), `tests/test_ai_tools.c`, `tests/test_agentic_loop.c`, `tests/runner.c`

**Interfaces:**
- `ai_stream_chunk_cut_off` also returns 1 for an Anthropic `message_delta` whose `delta.stop_reason` is `"max_tokens"`; the prefilter looks for `"max_tokens"` as well as `"length"`.
- `int agentic_sanitize_tool_inputs(AiToolCall *calls, int n);` replaces with `"{}"` every input that is `overflowed` (A1) or does not parse as a JSON object; returns the count.
- `int agentic_room_for_tool_round(const AiConversation *conv, const AiNoteQueue *q);` 1 when `msg_count + 3 + q->count <= AI_MAX_MESSAGES` (NULL `q` counts 0) *(corrected by the review of B1-B11: it was one argument)*.
- `agentic_add_tool_results` skips `agentic_cap_tool_result` for a run_command result.
- `int agentic_truncation_warning(const char *tool_name, size_t omitted_bytes, char *buf, size_t buf_size);` omitted 0: `"[%s output truncated at 64 KiB]"`; else `"[%s output cut: %zu bytes omitted]"` (today "Tool result from '%s' was truncated to 1MB.", `ai_agentic.c` ~184). Callers `ai_chat_stream.c` ~653, `ai_chat_view.c` ~234 pass 0; Task B18 passes the real value.

- [ ] **Step 1: Write the failing tests:** `test_stream_cut_off_anthropic_max_tokens` (`"max_tokens"` -> 1, `"end_turn"` -> 0); `test_agentic_sanitize_cut_heredoc` (a 4000-byte unterminated heredoc command -> `"{}"`, then `runcmd_parse_input` NO_COMMAND); `test_agentic_sanitize_keeps_valid` (`{"command":"ls"}` kept; `""`, `"[1]"`, `"null"` -> `"{}"`, returns 3); `test_agentic_room_for_tool_round` (61 -> 1, 62 -> 0, NULL -> 0); `test_agentic_runcmd_result_not_capped`; `test_agentic_truncation_warning_text`.
- [ ] **Step 2: Run `make -j test`.** Expected: compile failure.
- [ ] **Step 3: Implement.** Keep `ai_prompt.c` CRLF.
- [ ] **Step 4: Run `make -j test`**, **`make test SAN=1`** and **the Windows compile check.**
- [ ] **Step 5: Commit** (`fix(ai): an Anthropic reply cut off mid-call says so; sanitise tool input; room for a round`).

### Task B3: one results turn per round, the estimator, token counts on the tool path (spec §11, §26)

**Files:**
- Modify: `src/core/ai_prompt.h` (CRLF: `AiToolPart`, `AiMessage.parts`), `src/core/ai_prompt.c` (CRLF: `ai_msg_free`, `ai_conv_copy`, `ai_conv_take`, the `AI_ROLE_TOOL` writers in `build_body_core`, Save As, `ai_context_estimate_tokens` ~464, `ai_parse_stream_chunk_ex` ~1461-1486), `src/core/ai_agentic.c`, `src/core/ui_demo.c`, `src/ui/ai_chat_view.c` (read parts), `tests/test_agentic_loop.c`, `tests/test_ai_prompt.c`, `tests/runner.c`

**Interfaces:**
- `typedef struct { char tool_call_id[64]; char tool_name[64]; int is_error; char tool_note[AI_TOOL_PART_NOTE_MAX]; char *content; size_t content_len, omit_at, omitted_bytes; } AiToolPart;` `AiMessage` gains `AiToolPart *parts; int n_parts;` (heap; freed, copied, moved with the message). `agentic_add_tool_results` stores all results as one `AI_ROLE_TOOL` message. On OOM it stores a notes-only turn instead, one heap part per call with `content` NULL, each keeping its result's own note and adding "output dropped: Nutshell ran out of memory" where there was output (spec §25; never "not run", which is for a call that did not run). *(corrected by the review of B1-B11: it was a static parts array of `AI_TOOL_MAX` entries, "not run: Nutshell ran out of memory" and "-1 storing nothing when full".)* Returns 0 stored, 1 notes-only stored, -1 full, -2 notes-only array not had; on -1 and -2 nothing is stored and the open turn is dropped (`ai_conv_drop_open_tool_turn`, Task B4; R37). `AiToolResult` gains `omit_at, omitted_bytes`.
- The builder expands a parts message: Anthropic one user turn with one `tool_result` block per part; OpenAI form one `tool` message per part. The view and Save As walk the parts.
- `ai_context_estimate_tokens` counts parts. `ai_parse_stream_chunk_ex` fills the token counts as `ai_parse_stream_chunk` does.

- [ ] **Step 1: Write the failing tests:** `test_agentic_results_one_turn` (5 results -> msg_count + 1, n_parts 5, ids in order, cut fields and notes kept; copy, take and free under SAN=1); `test_agentic_results_oom_notes_only` (allocation-failure hook: a notes-only turn, well-formed); `test_body_parts_anthropic`; `test_body_parts_openai`; `test_context_estimate_counts_parts`; `test_stream_chunk_ex_reports_tokens` (an OpenAI usage chunk and an Anthropic `message_delta` through the tool path report their counts). Update every existing test that counts one message per result (`grep -n 'AI_ROLE_TOOL' tests/*.c`).
- [ ] **Step 2: Run `make -j test`.** Expected: compile failure.
- [ ] **Step 3: Implement.** Keep `ai_prompt.*` CRLF.
- [ ] **Step 4: Run `make -j test`**, **`make test SAN=1`** and **the Windows compile check.**
- [ ] **Step 5: Commit** (`feat(ai): one stored turn per tool round; real token counts when tools ran`).

### Task B4: one door for USER notes; the tool-pair invariant

**Files:**
- Create: `src/core/ai_conv_guard.h`, `src/core/ai_conv_guard.c`, `tests/test_ai_conv_guard.c`
- Modify: `src/core/ai_prompt.h` (CRLF: `AiNoteQueue`, `AiSessionState.notes`), `src/core/ai_prompt.c` (CRLF: body builders call the check), `tests/runner.c`

**Interfaces:**
- Produces:
  - `ai_prompt.h`: `#define AI_NOTE_QUEUE_MAX 4`, `#define AI_NOTE_TEXT_MAX 4096` (the continue text's buffer, `ai_chat_dispatch.c` ~474, ~964), `typedef struct { char text[AI_NOTE_QUEUE_MAX][AI_NOTE_TEXT_MAX]; int count; } AiNoteQueue;`; `AiSessionState` gains `AiNoteQueue notes;`.
  - `ai_conv_guard.h` (includes `ai_prompt.h`):
    - `int ai_conv_tool_calls_open(const AiConversation *conv);` 1 when the last message is an assistant turn with `n_tool_calls > 0`, or an assistant turn with tool calls is followed by fewer `AI_ROLE_TOOL` turns than calls up to the end.
    - `int ai_conv_add_user_note(AiConversation *conv, AiNoteQueue *q, int defer, int keep_free, const char *text);` (R54, C0: `keep_free` is the number of slots the caller needs left free after the text, 1 when it launches a request) appends an `AI_ROLE_USER` turn and returns 0; queues a copy and returns 1 when `defer` or `ai_conv_tool_calls_open(conv)`; returns 2, storing and queueing nothing, when the queued notes and the text cannot all be stored with `keep_free` left free (the door flushes the queue with `keep_free + 1` first, in order; the rest stay queued; the text is never stored ahead of a queued note); a text of `AI_NOTE_TEXT_MAX` bytes or more is cut on a UTF-8 boundary and ends `" [\xE2\x80\xA6 cut]"`; -1 only for NULL/empty text, a full queue (the caller shows a row) or a failed allocation.
    - `int ai_conv_room_for_send(const AiConversation *conv, int open_batch, const AiNoteQueue *q);` 1 when `msg_count + 2 + (open_batch ? 1 : 0) + q->count <= AI_MAX_MESSAGES` (spec §24; NULL `q` counts 0) *(corrected by the review of B1-B11)*.
    - `int ai_conv_flush_notes(AiConversation *conv, AiNoteQueue *q, int keep_free);` while not open: appends the queued notes in order, leaving `keep_free` slots of the conversation free (a flush that precedes a relaunch leaves 1 for the reply; notes that do not fit stay queued), and returns the number appended; a note that does not fit stays queued with every later one, nothing is dropped *(corrected by the review of B1-B11: it dropped them; `keep_free` added by the review of the fix wave, R47)*; while open returns 0 and leaves `q`.
    - `int ai_conv_drop_open_tool_turn(AiConversation *conv);` frees a trailing open assistant turn (and any partial results after it); returns 1 when it dropped one.
    - `int ai_conv_tool_pairs_well_formed(const AiConversation *conv);` spec §19.
  - Every body builder returns 0 when `!ai_conv_tool_pairs_well_formed(conv)`: the check goes once in `build_body_core` (`ai_prompt.c` ~759), which `ai_build_request_body`, `_ex`, `_tools` and `_frame` all reach.

- [ ] **Step 1: Write the failing tests** (`tests/test_ai_conv_guard.c`). Helper: build conversations with `ai_conv_add` and `agentic_add_assistant_tool_msg`/`agentic_add_tool_results`.

```c
int test_pairs_plain_ok(void);              /* sys, user, assistant -> 1 */
int test_pairs_tool_round_ok(void);         /* sys, user, asst(2 calls), one tool turn with parts a, b, user note -> 1 */
int test_pairs_note_between_fails(void);    /* asst(1 call), USER "policy raised", tool -> 0 */
int test_pairs_exec_continue_between_fails(void); /* asst(1 call), USER "The commands above have been executed...", tool -> 0 */
int test_pairs_missing_results_fails(void); /* asst(2 calls) then assistant text -> 0 */
int test_pairs_partial_results_fails(void); /* asst(2 calls), a tool turn with one part, user -> 0; two tool turns -> 0 */
int test_pairs_wrong_id_fails(void);        /* asst(call "a"), tool for "b" -> 0 */
int test_pairs_orphan_result_fails(void);   /* user, tool "a" with no tool_use -> 0 */
int test_pairs_open_at_end_fails(void);     /* ..., asst(1 call) last -> 0; ai_conv_tool_calls_open -> 1 */
int test_conv_note_queued_while_open(void);
/* open conv: add_user_note -> 1, msg_count unchanged; agentic_add_tool_results; flush -> 1;
   last message is the note, well_formed 1 */
int test_conv_note_deferred_while_busy(void); /* defer 1 on a closed conv -> 1 (queued); flush -> appended */
int test_conv_note_refusals(void);          /* NULL/"" -> -1; 5th queued -> -1 */
int test_conv_note_long_text_cut(void);     /* a 4095-byte continue text queues whole; a 5000-byte one ends with the cut marker, valid UTF-8 */
int test_conv_room_for_send(void);          /* msg_count 62 -> 1; 63 -> 0; 62 with open_batch -> 0;
                                               "stop, don't do that" into a 63-message conversation: refused, msg_count unchanged */
int test_conv_note_door_keeps_the_reply_slot(void);   /* 61 messages, two queued notes, the launching text (keep_free 1): 2, msg_count 62, one note still queued, a reply fits; keep_free 0: both notes then the text, 64 (C0, R54) */
int test_conv_note_door_keep_free_values(void);       /* the text alone at 62/63/64 messages, negative keep_free, defer on a full conversation still 1, bad arguments -1 (C0, R54) */
int test_conv_note_door_out_of_memory_is_not_full(void);   /* a failed append (test hook `ai_conv_guard_set_add_for_test`) is -1, notes stay queued; no room anyway is still 2 (C0 review) */
int test_conv_note_door_keep_free_huge(void);   /* keep_free INT_MAX or AI_MAX_MESSAGES is clamped: 2, nothing stored or queued (C0 review) */
int test_conv_drop_open_tool_turn(void);    /* open -> 1, then well_formed 1; closed -> 0 */
int test_body_refuses_malformed(void);
/* ai_build_request_body_frame on test_pairs_note_between_fails' conv returns 0 for "anthropic" and "openai" */
```

- [ ] **Step 2: Run `make -j test`.** Expected: link failure on `ai_conv_tool_pairs_well_formed`.
- [ ] **Step 3: Implement.** Pairing: one pass; the assistant turn's K calls against the following tool turn's parts by `tool_call_id`, any order, each once. Keep `ai_prompt.*` CRLF.
- [ ] **Step 4: Run `make -j test`.** Expected: all pass (existing body tests included), warnings 0.
- [ ] **Step 5: Commit** (`feat(core): one door for USER notes and a tool-pair check in every body builder`).

### Task B5: the planner, `RunCmdReply` and batch storage

**Files:**
- Modify: `src/core/run_command.h`, `src/core/run_command.c`, `src/core/cmd_batch.h`, `src/core/cmd_batch.c`, `tests/test_run_command.c`, `tests/test_cmd_batch.c`, `tests/runner.c`

**Interfaces:**
- Consumes: Task B1's parser.
- Produces (`run_command.h`):

```c
typedef enum { RUNCMD_KIND_SLOT, RUNCMD_KIND_OVER_CAP, RUNCMD_KIND_INVALID, RUNCMD_KIND_OTHER_TOOL } RunCmdKind;
#define RUNCMD_CAP_SCREEN       1u   /* screen rows: alt screen, reflow, region scroll, an erase (ED 2, or ED 0 straight after a CUP/HVP to the home position; primary screen only), or empty/inverted range */
#define RUNCMD_CAP_SCROLLED_OUT 4u
#define RUNCMD_CAP_EMPTY        8u   /* nothing readable at all */
#define RUNCMD_CAP_REDRAWN     16u   /* screen rows: an erase or an empty or inverted range, screen has content */
#define RUNCMD_PROMPT_MAX     256   /* buffer size: a prompt is recorded up to 255 bytes */
/* 32 was RUNCMD_CAP_PROMPT (fix wave, R49/R53); withdrawn in C0: a changed prompt is output, no flag */
typedef struct {
    int      started, captured;      /* captured: spec §10's once-only flag */
    uint64_t start_line;  uint32_t start_reflow_seq, start_region_seq, start_erase_seq;  int start_alt;
    char    *head; size_t head_len;  /* heap; NULL when no excerpt */
    char    *tail; size_t tail_len;  /* heap; NULL when uncut */
    size_t   omitted_bytes;
    unsigned flags;                  /* RUNCMD_CAP_* */
    char     prompt[RUNCMD_PROMPT_MAX];   /* the start row's text left of the cursor */
    size_t   prompt_len;             /* bytes in prompt; 0 with prompt_long */
    int      prompt_long;            /* the start row's text did not fit: not recorded */
} RunCmdCapture;
typedef struct {
    char tool_use_id[AI_TOOL_ID_MAX], tool_name[AI_TOOL_NAME_MAX];
    RunCmdKind kind;  RunCmdParse parse;
    char command[RUNCMD_COMMAND_MAX + 1], note[RUNCMD_NOTE_MAX];
    int  entry;                      /* ApprovalEntry index; -1 unless SLOT and queued */
    int  fetch_off;                  /* OTHER_TOOL web_fetch the stream's registry lacked (spec §4) */
    RunCmdCapture cap;
} RunCmdCall;
typedef struct RunCmdReply { RunCmdCall *calls; int n_calls; int n_slot; } RunCmdReply;
#define RUNCMD_SLOT_MAX APPROVAL_MAX_CMDS
RunCmdReply *runcmd_plan(const AiToolCall *calls, int n_calls,
                         int fetch_registered);                   /* NULL on n_calls <= 0 or OOM; fetch_off set on
                                                                     web_fetch calls when !fetch_registered */
void runcmd_reply_free(RunCmdReply *r);                           /* frees every capture; NULL-safe */
RunCmdCall *runcmd_call_for_entry(RunCmdReply *r, int entry);     /* NULL when none */
int runcmd_reply_has_call(const AiToolCall *calls, int n_calls);  /* any call named run_command */
```

- `cmd_batch.h`: `struct RunCmdReply;` forward declaration; `CmdBatch` gains `struct RunCmdReply *runcmd;` (NULL for every other batch); `void cmd_batch_release(CmdBatch *b);` (frees `runcmd` through `runcmd_reply_free`, then the batch; NULL-safe); `CmdBatch *cmd_batch_runcmd(CmdBatchSet *set);` (the set's run_command batch, or NULL). `cmd_batch_remove`, `cmd_batch_set_free` and `cmd_batch_add`'s eviction all call `cmd_batch_release`. `cmd_batch.c` includes `run_command.h`; `cmd_batch.h` does not.

- [ ] **Step 1: Write the failing tests.**

```c
int test_runcmd_plan_order(void);
/* calls: run_command ls; web_fetch; run_command {}; run_command "a\nb" -> kinds SLOT, OTHER_TOOL,
   INVALID (NO_COMMAND), INVALID (UNSAFE); n_slot 1; ids copied; every entry -1;
   fetch_registered 1 -> fetch_off 0 everywhere; fetch_registered 0 -> fetch_off 1 on the web_fetch call only */
int test_runcmd_plan_over_cap(void);   /* 17 valid calls -> 16 SLOT, the 17th OVER_CAP; n_slot 16 */
int test_runcmd_plan_mixed_cap(void);  /* 3 INVALID + 16 valid -> 16 SLOT, no OVER_CAP (INVALID take no slot) */
int test_runcmd_call_for_entry(void);  /* entry set on call 2 -> found; -1 and unknown -> NULL */
int test_runcmd_reply_has_call(void);  /* web_search only -> 0; plus run_command -> 1 */
/* tests/test_cmd_batch.c */
int test_cmd_batch_release_frees_runcmd(void);
/* a batch with runcmd (heads and tails malloc'd) removed; another freed by set_free; a third evicted by
   the 9th cmd_batch_add -> no leak under the sanitizer run, evicted_id reported */
int test_cmd_batch_runcmd_finds(void); /* none -> NULL; the one with runcmd -> it */
```

- [ ] **Step 2: Run `make -j test`.** Expected: compile failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run `make -j test`** (all pass, warnings 0), **then `make test SAN=1`**: no report naming `cmd_batch.c` or `run_command.c`. **Windows compile check.**
- [ ] **Step 5: Commit** (`feat(core): run_command planner and per-batch call state, released on eviction`).

### Task B6: absolute-line capture helpers and `reflow_seq`

**Files:**
- Modify: `src/term/term.h` (LF), `src/term/buffer.c` (CRLF), `src/core/term_extract.h`, `src/core/term_extract.c` (CRLF), `tests/test_term_extract.c` (CRLF), `tests/test_term_scrollback.c`, `tests/fuzz/fuzz_term.c`, `tests/runner.c`

The buffer as rewritten in #93: the ring is `lines`/`lines_capacity`/`lines_count`/`lines_start` (`term.h` ~70-73) and only its live part, `term_logical_rows(t)` rows from `lines_start`, holds rows (the rest are NULL); there is no row accessor, so read `t->lines[(t->lines_start + i) % t->lines_capacity]` for `i < term_logical_rows(t)` and NULL-check, as `term_extract.c` ~11-14 does. Nothing here allocates. `lines_recycled` (`uint64_t`, ~154) and `epoch` (~156) exist. `TermRow.wrapped` is set on the continuation row (parser.c ~57-68).

**Interfaces:**
- Produces:
  - `Terminal` gains `uint32_t reflow_seq, region_scroll_seq;` (after `epoch`); `region_scroll_seq` is bumped by `term_scroll_up`'s region path (`buffer.c` ~597-602, a scroll that is not full-screen on the primary screen) and, like `reflow_seq`, set to 0 in `term_init` beside the other explicit zeroings (`buffer.c` ~114-121; the struct comes from `xmalloc`, not zeroed) and bumped only beside `term_resize`'s `epoch++` (~460), after the commit: a `term_resize` that returns false leaves it unchanged. Alternate-screen enter and exit (`term_alt_screen_enter`/`_exit`, ~740-838, which save and restore the primary's `lines_recycled`) do not touch it.
  - `#define TERM_RANGE_START_RECYCLED 1u`
  - `uint64_t term_abs_cursor_line(const Terminal *t);` `lines_recycled` + `top + cursor.row`, `top = lines_count >= rows ? lines_count - rows : 0` as `term_screen_to_phys` computes it (`buffer.c` ~568-570): `scrollback_offset` never changes it. 0 for NULL.
  - `char *term_extract_abs_range_dup(const Terminal *t, uint64_t first, uint64_t end, const char *echo, size_t *len_out, unsigned *flags);` absolute lines `[first, end)` formatted as `term_extract_last_n` formats rows, trailing blank rows trimmed. When `echo` is non-NULL and the range's first row starts a logical line (it and the following rows whose `wrapped` is set) that contains `echo`, that whole logical line is dropped. `first < lines_recycled` starts at `lines_recycled` and sets `TERM_RANGE_START_RECYCLED`. Returns malloc'd text (`""` when nothing is left), NULL on OOM or NULL `t`.

- [ ] **Step 1: Write the failing tests** (`term_init(5, 20, 50)` + `term_process`).

```c
int test_term_abs_cursor_line(void);       /* after 12 lines on a 5-row screen: equals lines_recycled + logical index;
                                              unchanged with scrollback_offset = 3 */
int test_term_abs_range_across_scrollback(void); /* lines 2..9 of 12 come back in order, one '\n' per row */
int test_term_abs_range_echo_dropped(void);/* "$ ls\r\nout1\r\nout2\r\n" from the prompt line, echo "ls" -> "out1\nout2" */
int test_term_abs_range_echo_wrapped(void);/* a 30-byte command on a 20-col screen wraps; both rows dropped */
int test_term_abs_range_echo_kept(void);   /* first row lacks the command -> kept */
int test_term_abs_range_trailing_blank(void);/* trailing empty rows trimmed */
int test_term_abs_range_start_recycled(void);/* scrollback 50, 200 lines; first 0 -> flag set, text from lines_recycled */
int test_term_abs_range_empty(void);       /* first == end -> "" , len 0 */
int test_term_reflow_seq(void);            /* term_init -> 0; term_resize(t, 5, 30) bumps reflow_seq by 1; same size -> unchanged;
                                              "\x1b[?1049h" then "\x1b[?1049l" leaves it and keeps absolute line numbers of old rows */
int test_term_region_scroll_seq(void);     /* "\x1b[2;4r" then newlines at row 4 bump it; a full-screen scroll on the primary does not;
                                              scrolling on the alternate screen does */
/* tests/test_term_scrollback.c: extend test_term_sb_resize_alloc_failure_unchanged and
   test_term_sb_resize_alt_alloc_failure_unchanged with ASSERT_EQ on reflow_seq beside lines_recycled and epoch */
```
- Fuzz (`tests/fuzz/fuzz_term.c`, after the `term_resize` call): read `reflow_seq` before, then trap unless it rose by 1 when the call returned true and the size changed, else by 0. Also call `term_abs_cursor_line` and `term_extract_abs_range_dup(t, 0, term_abs_cursor_line(t) + 1, NULL, ...)` after each run and free the result (exercises the reader under injected failures).

- [ ] **Step 2: Run `make -j test`.** Expected: compile failure.
- [ ] **Step 3: Implement.** Keep `buffer.c`, `term_extract.*` and `test_term_extract.c` CRLF.
- [ ] **Step 4: Run `make -j test`** (all pass, warnings 0), **`make test SAN=1`, `make fuzz && make fuzz-run`** (every committed seed, 0 failures; any crash input stays under `build/fuzz/crashes/`, never committed) **and the Windows compile check.**
- [ ] **Step 5: Commit** (`feat(term): capture terminal rows by absolute line; count reflows`).

### Task B7: the cut, the per-command cap and capture start/take

**Files:**
- Modify: `src/core/run_command.h`, `src/core/run_command.c`, `tests/test_run_command.c`, `tests/runner.c`

**Interfaces:**
- Consumes: Task B5's `RunCmdCapture`, Task B6's helpers.
- Produces:
  - `size_t runcmd_cap_bytes(int n_slot);` `min(16384, 65536 / n)`; `n <= 0` gives 16384.
  - `void runcmd_cut(const char *text, size_t len, size_t cap, RunCmdCapture *c);` replaces any excerpt in `c`: `len <= cap` keeps all as `head`, `tail` NULL, `omitted_bytes` 0; else `head` the first ¾·cap bytes and `tail` the last ¼·cap bytes, each cut back to a UTF-8 boundary, `omitted_bytes` the rest. The marker is never stored.
  - `void runcmd_capture_start(RunCmdCapture *c, const Terminal *t);` *(corrected by the review of B1-B11; const again after the fix wave, R46)* does nothing for NULL `c` or `t`; otherwise it resets a capture armed before (frees any excerpt, `flags` 0, `captured` 0), records `start_line` (`term_abs_cursor_line`), `start_reflow_seq`, `start_region_seq`, `start_erase_seq`, `start_alt`, and the prompt (the start row's text left of the cursor, untrimmed, via `term_extract_cursor_prefix`: up to `RUNCMD_PROMPT_MAX - 1` = 255 bytes in `prompt` with `prompt_len`; a longer one is not recorded: `prompt_long` 1, `prompt_len` 0), then `started = 1`. It writes nothing to the terminal; `c` must be zero-initialised before its first start.
  - `int runcmd_capture_take(RunCmdCapture *c, const Terminal *t, const char *command, size_t cap);` returns 0 unless `started && !captured && t`; then `captured = 1` and (spec §10): alternate screen on at start or take, `reflow_seq` or `region_scroll_seq` moved -> the screen rows (`term_extract_last_n_dup(t, t->rows, ...)`), `RUNCMD_CAP_SCREEN`; else `erase_seq` moved (ED 2, or ED 0 straight after a CUP/HVP to the home position, primary screen only; *review of the fix wave*: no cursor-position trigger) or the range `[start_line, term_abs_cursor_line(t))` empty or inverted -> the screen rows, `SCREEN | REDRAWN` (just `SCREEN` when the screen is blank); else the primary range with `echo = command` (`TERM_RANGE_START_RECYCLED` -> `RUNCMD_CAP_SCROLLED_OUT`), then the cursor row's text left of the cursor (`prompt_long` leaves it out), by R49 (C0) in this order: ends with the recorded prompt -> the part before it; equals it -> nothing; else all of it (a changed prompt included); trailing blanks trimmed, joined after a `\n` (alone when the range is empty). Nothing readable -> `RUNCMD_CAP_EMPTY`. An allocation failure in the read or join leaves no excerpt, no EMPTY and `omitted_bytes` 0 (R29). Text goes through `runcmd_cut(..., cap)`. Returns 1.

- [ ] **Step 1: Write the failing tests.**

```c
int test_runcmd_cap_bytes(void);       /* n 1, 4, 8, 16 -> 16384, 16384, 8192, 4096; 0 -> 16384 */
int test_runcmd_cut_uncut(void);       /* 100 bytes, cap 4096 -> head 100, tail NULL, omitted 0 */
int test_runcmd_cut_head_tail(void);   /* 20000 x 'a', cap 4096 -> head 3072, tail 1024, omitted 15904 */
int test_runcmd_cut_multibyte(void);   /* 3-byte chars straddling both cut points -> valid UTF-8, head+tail+omitted == len */
int test_runcmd_capture_normal(void);  /* start at "$ ls" line, output 2 lines, new prompt -> head "out1\nout2" */
int test_runcmd_capture_once(void);    /* take -> 1; more output, cursor moves; take -> 0, excerpt unchanged */
int test_runcmd_capture_reflowed(void);/* term_resize between start and take -> SCREEN, head is the screen rows */
int test_runcmd_capture_alt_round_trip(void); /* start primary, ?1049h then ?1049l, take -> the primary range, no flag */
int test_runcmd_capture_inside_alt(void);  /* ?1049h before start (tmux), output, take -> SCREEN, the visible rows */
int test_runcmd_capture_region_scroll(void); /* DECSTBM "\x1b[1;3r" on the primary, output scrolls the region -> SCREEN */
int test_runcmd_capture_cursor_up(void);   /* "\x1b[2J\x1b[H" then "out" (clear; ls): end < start -> SCREEN, head contains "out" */
int test_runcmd_capture_scrolled_out(void); /* 200 lines of output, scrollback 50 -> SCROLLED_OUT and an excerpt */
int test_runcmd_capture_empty(void);   /* no output and a blank screen -> EMPTY */
int test_runcmd_capture_scrolled_view(void); /* scrollback_offset 3 at start and take -> same text as offset 0 */
int test_runcmd_capture_not_started(void);   /* take before start -> 0 */
int test_runcmd_capture_home_erase_below(void);   /* "\x1b[H\x1b[J" is an erase: SCREEN | REDRAWN (fix wave, R46) */
int test_runcmd_capture_transient_prompt_not_clear(void);   /* `\r ESC[1A ESC[J` at home, 40 lines: flags 0, all 40 (C0) */
int test_term_erase_seq_home_cup_only(void);   /* tests/test_term_extract.c: ED 0 counts only straight after a CUP/HVP home (C0) */
int test_runcmd_capture_prompt_decorations(void); /* transient prompt, clock via DECSC/CUP/DECRC, DECSC/CSI r/DECRC: flags 0, the range */
int test_runcmd_capture_changing_prompt(void);    /* `false` under "[0] $ " then "[1] $ ": head "[1] $", flags 0; `#1 >` and `progress 50%` kept (C0, R49) */
int test_runcmd_capture_no_prompt_fallback(void);   /* ssh web1, python3, cd + printf foo, printf foo, false, progress 50%: literal final texts (C0) */
```

- [ ] **Step 2: Run `make -j test`.** Expected: link failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run `make -j test`.** Expected: all pass, warnings 0.
- [ ] **Step 5: Commit** (`feat(core): capture a command's output once, cut head and tail`).

### Task B8: results, notes, fit and framing

**Files:**
- Modify: `src/core/run_command.h`, `src/core/run_command.c`, `src/core/ai_prompt.c` (CRLF: `AI_ROLE_TOOL` framing), `tests/test_run_command.c`, `tests/test_ai_body_frame.c`, `tests/runner.c`

**Interfaces:**
- Consumes: Tasks B5 and B7; trust's `ai_data_lead_tool`, `ai_data_escape_dup`; `json_escape_string`.
- Produces:

```c
typedef enum {
    RUNCMD_CAUSE_SETTLED, RUNCMD_CAUSE_DECIDED, RUNCMD_CAUSE_TIMEOUT, RUNCMD_CAUSE_RETRY, RUNCMD_CAUSE_STOP,
    RUNCMD_CAUSE_TAB_SWITCH, RUNCMD_CAUSE_SESSION_ENDED, RUNCMD_CAUSE_TYPED,
    RUNCMD_CAUSE_NEW_CHAT, RUNCMD_CAUSE_SESSION_CLOSED, RUNCMD_CAUSE_PANEL_CLOSED,
    RUNCMD_CAUSE_EVICTED,
    /* whole-reply answers, built with q == NULL */
    RUNCMD_CAUSE_NO_SLOT, RUNCMD_CAUSE_SESSION_GONE, RUNCMD_CAUSE_SET_FULL
} RunCmdCause;
int runcmd_cause_relaunches(RunCmdCause c); /* 1: SETTLED (the tick's end), TIMEOUT, RETRY; else 0 (DECIDED: a Deny/Deny all that left
                                               nothing runnable; NO_SLOT; STOP; spec §7's table and the governing principle) */
int runcmd_cause_appends(RunCmdCause c);    /* 0: NEW_CHAT, SESSION_CLOSED; else 1 */
#define RUNCMD_OMIT_MARKER_FMT "[\xE2\x80\xA6 %zu bytes omitted \xE2\x80\xA6]"
int runcmd_build_results(const RunCmdReply *r, const ApprovalQueue *q, RunCmdCause cause,
                         AiToolResult *out);  /* out[r->n_calls], call order; content malloc'd head+tail
                                                 or NULL, omit_at = head_len; returns n_calls or -1 */
size_t runcmd_request_estimate(const AiConversation *conv, const char *provider, size_t tools_json_len, size_t snapshot_len);
#define RUNCMD_BODY_HEADROOM 32768
#define RUNCMD_TOOLS_ESTIMATE 8192
size_t runcmd_budget(size_t body_max, int context_limit_tokens); /* min(body_max, 3 * limit when limit > 0) - RUNCMD_BODY_HEADROOM */
int runcmd_fit(AiToolResult *res, int n, size_t estimate, size_t budget);  /* returns halvings done */
char *runcmd_frame_result(const AiToolPart *part, const char *delim, size_t *out_len);
#define RUNCMD_AGE_ROUNDS 2
#define RUNCMD_AGED_STUB "[output dropped to save room; run the command again if you need it]"
int runcmd_age_results(AiConversation *conv);  /* run_command parts in command rounds older than the newest 2: the stub is written over the
                                                  content in place (no allocation), "output cut" dropped from the note, call kept;
                                                  returns parts aged. A command round is a results turn holding a run_command part. */
```

- Note texts: spec §13 table verbatim (round 2 rows included), capture notes spec §10 verbatim, the cut note `"output cut: %zu bytes omitted from the middle"`, the fit note `"output dropped: no room left in this conversation"`; parts joined with `"; "` into `tool_note` (512). `is_error` 1 only for OTHER_TOOL and INVALID (OTHER_TOOL text spec §4, INVALID `runcmd_parse_error`); an OTHER_TOOL call with `fetch_off` gets `tool_fetch_off_note()` (`tool_decision.h`), `is_error` 0. `<reason>` per cause: STOP `"stopped by the user; do not run it again unless the user asks"` *(corrected by the review of B1-B11: a semicolon, spec §13's table is the authority; a COMPLETED or EXECUTING call whose capture never ran ends its note "output not captured")*, TAB_SWITCH `"the user switched tabs"`, SESSION_ENDED `"the session ended"`, PANEL_CLOSED `"the AI panel was closed"`, TIMEOUT `"the terminal did not show a prompt in time"`. With `q == NULL`, SLOT calls get the cause's whole-reply note (SET_FULL, SESSION_GONE); OVER_CAP, INVALID and OTHER_TOOL keep their own rows under every cause. A SLOT call whose `entry` is -1 in a batch gets `"not run (refused by the approval queue)"`.
- `runcmd_request_estimate`: the summed `json_escape_string` length of every stored message's content plus `tools_json_len` plus `2 * snapshot_len`. It takes the provider and adds every stored assistant turn's escaped `reasoning` for `moonshot` (and, from Task B19, `deepseek` when tools are sent); the fit never cuts reasoning. `runcmd_fit`: while `estimate + Σ escaped(content + note) > body_max - RUNCMD_BODY_HEADROOM`, halve every head (keep its start) and tail (keep its end) on UTF-8 boundaries, growing `omitted_bytes`; when nothing is left, `content` NULL and the fit note appended.
- `runcmd_frame_result`: uncut, trust's one-block layout with `ai_data_lead_tool("run_command")`; cut, spec §12's layout byte for byte (head block, marker between delimiters as Nutshell text, tail block, note); no excerpt, the note alone. The body builder uses it for every part whose `tool_name` is `run_command`, both wire forms; trust's 64 KiB path never sees one.

- [ ] **Step 1: Write the failing tests.**

```c
int test_runcmd_results_every_row(void);
/* one queue with COMPLETED (excerpt), DENIED, BLOCKED, APPROVED-unsent; plus OVER_CAP, INVALID and
   OTHER_TOOL calls; cause SETTLED -> n_calls results in call order, ids matching, each note as spec §13,
   is_error only on INVALID and OTHER_TOOL; PENDING under SETTLED -> "not run (the batch finished without it)";
   a SLOT call with entry -1 -> "not run (refused by the approval queue)" */
int test_runcmd_results_executing_each_cause(void);
/* EXECUTING under STOP, TAB_SWITCH, SESSION_ENDED, PANEL_CLOSED -> "the panel stopped waiting; the command
   may still be running (<reason>)" with the rows so far; under RETRY -> starts "ran; the panel stopped waiting";
   PENDING under RETRY -> "not run (the user retried)"; STOP note contains "do not run it again" */
int test_runcmd_results_typed_new_chat(void); /* PENDING/BLOCKED under TYPED -> "...sent a new message instead"; NEW_CHAT, EVICTED -> "not run (never decided)" */
int test_runcmd_results_whole_reply(void);  /* q NULL: SET_FULL, SESSION_GONE notes on every SLOT call; NO_SLOT: each call its own row */
int test_runcmd_no_slot_does_not_relaunch(void); /* runcmd_cause_relaunches(NO_SLOT), (DECIDED), (STOP) == 0 */
int test_runcmd_results_fetch_off(void);    /* OTHER_TOOL web_fetch with fetch_off -> tool_fetch_off_note(), is_error 0; without -> spec §4 text, is_error 1 */
int test_runcmd_results_capture_notes(void);/* REFLOWED, ALT_SCREEN, SCROLLED_OUT+cut, EMPTY appended after "ran; " */
int test_runcmd_note_fits_512(void);        /* longest composition (EXECUTING, STOP reason, scrolled out, cut) < 512 */
int test_runcmd_cause_tables(void);         /* relaunches/appends for every cause; STOP's APPROVED-unsent note per spec §13 */
int test_runcmd_fit_to_notes(void);         /* estimate = body_max - 40000, 4 results of 16 KiB -> halved until
                                               within budget; estimate = body_max -> content NULL, fit note */
int test_runcmd_fit_multibyte(void);        /* 3-byte text halved stays valid UTF-8 */
int test_runcmd_budget_context_limit(void); /* limit 8000 tokens -> 24000 - headroom; 0 -> body_max - headroom */
int test_runcmd_age_results(void);         /* 5 rounds: rounds 1-3 stubbed, 4-5 untouched, notes kept; web_search parts untouched */
int test_runcmd_body_bounded_over_rounds(void); /* 20 rounds of 16 KiB outputs: every body under the budget; ai_context_estimate_tokens
                                               tracks the body within 10 % */
int test_runcmd_estimate_counts_reasoning(void); /* "moonshot": a turn with 10 KB reasoning raises the estimate by its escaped size; "openai": not */
/* tests/test_ai_body_frame.c (fixed delimiter) */
int test_body_runcmd_result_uncut(void);    /* one block, note after the closing delimiter */
int test_body_runcmd_result_cut(void);      /* spec §12 layout exactly, marker "[… 41213 bytes omitted …]" outside both blocks */
int test_body_runcmd_note_only(void);       /* content NULL -> note alone, no delimiter */
```

- [ ] **Step 2: Run `make -j test`.** Expected: link failure.
- [ ] **Step 3: Implement.** Keep `ai_prompt.c` CRLF.
- [ ] **Step 4: Run `make -j test`** (all pass, warnings 0), **`make test SAN=1` and the Windows compile check.**
- [ ] **Step 5: Commit** (`feat(core): run_command results, notes, fit and framing`).

### Task B9: the Retry plan and the removal table

**Files:**
- Modify: `src/core/run_command.h`, `src/core/run_command.c`, `tests/test_run_command.c`, `tests/test_ai_rounds.c`, `tests/runner.c`

**Interfaces:**
- Produces: `typedef enum { RUNCMD_RETRY_CANCEL_BATCH, RUNCMD_RETRY_POP_ASSISTANT, RUNCMD_RETRY_RELAUNCH } RunCmdRetry;` `RunCmdRetry runcmd_retry_plan(const AiConversation *conv, int has_runcmd_batch);` batch: CANCEL_BATCH; else POP only when the last message is an assistant turn with `n_tool_calls == 0`; else RELAUNCH.
- The removal table (spec §8) as data: `typedef enum { RUNCMD_PATH_TICK_END, RUNCMD_PATH_DECIDED, RUNCMD_PATH_TIMEOUT, RUNCMD_PATH_STOP, RUNCMD_PATH_SESSION_ENDED, RUNCMD_PATH_TAB_SWITCH, RUNCMD_PATH_RETRY, RUNCMD_PATH_TYPED, RUNCMD_PATH_NEW_CHAT, RUNCMD_PATH_SESSION_CLOSED, RUNCMD_PATH_PANEL_CLOSED } RunCmdPath;` `typedef struct { RunCmdCause cause; int capture, append, relaunch, reset_rounds; } RunCmdPathPlan;` `RunCmdPathPlan runcmd_path_plan(RunCmdPath p);` one row per path, from spec §8 (relaunch only TICK_END, TIMEOUT, RETRY; append all but NEW_CHAT and SESSION_CLOSED; capture where `active_term` may still exist). `src/ui` asks it and writes no such condition of its own.

- [ ] **Step 1: Write the failing tests.**

```c
int test_runcmd_retry_plan(void);  /* batch -> CANCEL_BATCH; plain assistant last -> POP; tool results last -> RELAUNCH;
                                      user last -> RELAUNCH */
int test_runcmd_path_plan_table(void); /* every row: cause, capture, append, relaunch exactly as spec §8 */
int test_runcmd_retry_never_pops_tool_turn(void);
/* last message an assistant turn with tool calls, no batch -> RELAUNCH (not POP); with batch -> CANCEL_BATCH */
/* tests/test_ai_rounds.c */
int test_ai_rounds_runcmd_fourth_attended(void); /* three ai_rounds_continue(s, 1) -> ai_rounds_batch_policy attended */
```

- [ ] **Step 2: Run `make -j test`.** Expected: link failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run `make -j test`.** Expected: all pass, warnings 0.
- [ ] **Step 5: Commit** (`feat(core): run_command Retry plan`).

### Task B10: the tool-variant prompt and the marker NOTE

**Files:**
- Modify: `src/core/ai_prompt.h`, `src/core/ai_prompt.c` (CRLF), `tests/test_ai_prompt.c`, `tests/runner.c`

**Interfaces:**
- Consumes: `RUNCMD_TOOL_NAME`; trust's `ai_build_system_prompt_ex(..., int debug_log_tool, const AiToolRegistry *tools)`.
- Produces: `_ex` builds the tool variant when `tools` holds `run_command` (`ai_tools_find`); otherwise every byte is as before. `int ai_build_runcmd_marker_note(int n, char *buf, size_t cap);` writes spec §3's NOTE with `n`; returns the length, 0 when it does not fit or `n <= 0`.
- Tool variant (spec §3 verbatim): in each opening (SSH, telnet, local) the `[EXEC]` instruction sentence becomes the `run_command` sentence; `rest` and `rest_powershell` are replaced whole by the spec's text (the PowerShell one adds its sentence); the tools paragraph's closing `[EXEC]` sentence becomes "Run terminal commands only through the run_command tool."

- [ ] **Step 1: Write the failing tests.**

```c
int test_prompt_tool_variant_ssh(void);     /* registry with run_command: contains "call the run_command tool with the
   command in `command`" and spec §3's `rest` IMPORTANT sentence in full; no "[EXEC]ls -la[/EXEC]", no "wrap it in [EXEC]" */
int test_prompt_tool_variant_telnet(void);  /* same, telnet opening */
int test_prompt_tool_variant_local_powershell(void); /* plus "Write PowerShell syntax, not bash." */
int test_prompt_tools_paragraph_both_ways(void); /* with run_command: "Run terminal commands only through the run_command tool.";
   web_search only: the old "Continue to use [EXEC]" sentence */
int test_prompt_exec_variant_unchanged(void);    /* registry without run_command and NULL: identical bytes to the
   existing pins (run them unchanged) */
int test_runcmd_marker_note(void); /* n 2 -> exact spec text with "2"; cap 10 -> 0 */
```

- [ ] **Step 2: Run `make -j test`.** Expected: failures in the new tests.
- [ ] **Step 3: Implement.** Keep CRLF.
- [ ] **Step 4: Run `make -j test`.** Expected: all pass, warnings 0.
- [ ] **Step 5: Commit** (`feat(ai): system prompt variant for the run_command tool`).

### Task B11: Save As, text-form tool turns, and the frozen snapshot

**Files:**
- Modify: `src/core/ai_prompt.h` (CRLF: `AiMessage.tool_provider`, `AiConversation.snapshot`), `src/core/ai_prompt.c` (CRLF: `ai_build_save_text`, the body builder, `ai_conv_copy`/`_take`/`_reset`), `src/core/ai_agentic.h`, `src/core/ai_agentic.c`, `src/ui/ai_chat_stream.c` (pass the provider), `tests/test_ai_prompt.c`, `tests/test_ai_body_frame.c`, `tests/test_ai_stream.c`, `tests/runner.c`

**Interfaces:**
- Consumes: `runcmd_describe`, `runcmd_frame_result`, `RUNCMD_OMIT_MARKER_FMT`.
- Frozen snapshot (spec §22): `AiConversation` gains `char *snapshot; size_t snapshot_len; int snapshot_msg;` (`snapshot_msg` -1 when none; `ai_conv_init` and `ai_conv_reset` set -1 and free; `ai_conv_copy` duplicates; `ai_conv_take` moves). `int ai_conv_freeze_snapshot(AiConversation *conv, const char *text, size_t len, int offers_runcmd);` frees any older one and freezes `text` to the last message when it is a USER turn (returns 0), else does nothing (returns 1; -1 when an allocation fails) *(corrected by the review of B1-B11: a Nutshell note, `AiMessage.nutshell_note`, is refused; corrected by the review of the fix wave, 2026-11-11, R48: refused only when `offers_runcmd` is 1, and the builder applies the keep-on-question rule only to a request whose tools offer run_command)*. `AiBodyFrame` keeps `snapshot` and gains `int snapshot_at;` (0, the zero value: the last `AI_ROLE_USER` message as today; k > 0: message k − 1, attached only while it is a typed `AI_ROLE_USER` message and every `AI_ROLE_USER` message after it is a Nutshell note; *corrected by the review of B1-B11, was "while that is the last `AI_ROLE_USER` message"*). Trust's ten tests in `tests/test_ai_body_frame.c` pass unchanged. Task B12 fills both from the frozen copy.
- Produces:
  - Save As: after an assistant turn's text, one line per call: `$ <command>` for run_command (from `runcmd_describe`), `[tool: <name>]` otherwise; each `AI_ROLE_TOOL` turn as `"--- Output (<tool_name>) ---\n"`, head, the marker line when `omitted_bytes > 0`, tail, then the `tool_note` line, then a blank line.
  - `AiMessage` gains `char tool_provider[16]; char tool_model[64];`, set by `agentic_add_assistant_tool_msg_ex` from new `provider` and `model` arguments (the stream's `s->provider` and `s->conv.model`; every caller updated), copied with the message.
  - Body (spec §16): an assistant turn with tool calls renders as text when the request carries no tools array (`tools_json` NULL or `""`), when it holds a run_command call and `tools_json` lacks `"run_command"`, or when `tool_provider`/`tool_model` is set and differs from the request's provider or `conv->model`: its text, then `"\nran: <command>"` per run_command call and `"\ncalled <tool>"` per other call; the `AI_ROLE_TOOL` turns that answer it become one user message `"output:\n"` + each framed result (`runcmd_frame_result` or trust's block). Never role `tool`, `tool_calls` or `tool_result` for such a turn. Other turns are unchanged.

- [ ] **Step 1: Write the failing tests.**

```c
int test_save_text_tool_turns(void);   /* asst "Checking." + calls ls, web_fetch; results (one cut) -> contains
   "$ ls\n", "[tool: web_fetch]", "--- Output (run_command) ---", the marker, the note */
int test_body_runcmd_turn_as_text_custom(void);   /* "custom", tools_json NULL: no "\"role\":\"tool\"", no "tool_calls", contains "ran: ls" and "output:"; json_validate ok */
int test_body_runcmd_turn_as_text_no_runcmd_tool(void); /* "deepseek", tools_json with web_search only: run_command turn as text;
   a later web_search turn keeps "\"role\":\"tool\"" and "tool_calls"; json_validate ok */
int test_body_runcmd_turn_structured_anthropic(void); /* "anthropic": tool_use/tool_result as today */
int test_body_runcmd_turn_structured_moonshot(void); /* "moonshot", tools with run_command: "tool_calls" and "reasoning_content" on the
   run_command turn, results "\"role\":\"tool\"" framed; the same conversation with tools_json NULL: text, no "reasoning_content" */
int test_snapshot_frozen_question_bytes_same(void); /* user Q, freeze "screen1"; body round 1; add asst(run_command) + results;
   freeze "screen2" refused (last is TOOL); body round 2: the bytes of Q's message, block included, equal round 1's */
int test_snapshot_new_user_turn_moves_it(void);    /* a later USER turn frozen with "screen3": Q carries no block, the new turn does */
int test_snapshot_copy_take_free(void);            /* copy duplicates, take moves, reset frees (SAN=1 clean) */
int test_body_foreign_provider_turn_as_text(void); /* a turn recorded under "moonshot" with id "functions.run_command:0", sent to
   "anthropic": text form, no "tool_use", json_validate ok; the same turn sent to "moonshot": structured;
   sent to "moonshot" under another model: text */
int test_body_moonshot_pinned(void); /* an [EXEC]-only and a web_fetch conversation to "moonshot": byte-identical to a body
   captured from 6432ca0's builder and committed as a fixture string in the test */
int test_body_after_cut_input_validates(void);  /* a call sanitised to "{}" (Task B2) stored and answered: anthropic and
   openai bodies pass json_validate */
```

- [ ] **Step 2: Run `make -j test`.** Expected: failures in the new tests.
- [ ] **Step 3: Implement.** Keep CRLF.
- [ ] **Step 4: Run `make -j test`.** Expected: all pass, warnings 0.
- [ ] **Step 5: Commit** (`feat(ai): keep commands in Save As; tool turns as text across providers; freeze the snapshot to its question`).

### Task B12: launch: predicates, registration, one prompt site, the frozen snapshot

**Files:**
- Modify: `src/ui/ai_chat_send.c` (`ai_chat_launch_stream_thread`, `ai_chat_send_user_message`, `ai_chat_send_continue_message`), `src/ui/ai_chat_internal.h` (`AiChatData.runcmd_mode`), `src/core/ai_stream.h` (`AiStream.run_command`)

- [ ] **Step 1: Launch.** Two static helpers in `ai_chat_send.c`: `ai_chat_provider_tools(d)` returns `ai_provider_tools(d->provider, d->runcmd_mode)` and `ai_chat_provider_runcmd(d)` returns `ai_provider_runcmd(d->provider, d->runcmd_mode)` (`AiChatData.runcmd_mode`, 0 = Automatic until Task B21 sets it); the five `ai_provider_supports_tools(d->provider)` calls in `ai_chat_send.c` (~16, ~152, ~245, ~515, ~572) use the first. `has_tools` (~151) becomes `d->tool_registry.count > 0 || dlog_enabled() || ai_chat_provider_runcmd(d)`. The snapshot (spec §22): take a fresh one only when the conversation's last message is a USER turn (R48) and freeze it with `ai_conv_freeze_snapshot(&d->conv, ..., ai_chat_provider_runcmd(d))` before the copy (**this step sets the switch**: the fourth argument says whether the launch offers run_command, and only then does it refuse a note, returning 1; it returns -1 on an allocation failure, then launch without one); otherwise take none; the launch sets `frame.snapshot` and `frame.snapshot_at = snapshot_msg + 1` from the frozen copy when `ai_conv_has_snapshot(&d->conv)` (never test `snapshot_msg`), else `frame.snapshot = NULL` and `snapshot_at` 0, at **both** body builds (`ai_chat_send.c` ~146 and the in-stream rebuild `ai_chat_stream.c` ~732, which still pass 0), so a request that offers run_command carries it on its question, and one that does not carries a fresh snapshot on its last USER turn, as today (R48). *(corrected by the review of B1-B11: it said "`AI_ROLE_USER`" and named one site.)* In the tools branch, after `ai_debug_tool_register_if_enabled(launch_reg, ...)` (~171) and before `ai_stream_copy_tools`: when `ai_chat_provider_runcmd(d)`, `runcmd_register(launch_reg)` and `s->run_command = 1`. Build the system prompt here, once, in both branches (registry `NULL` in the no-tools one), with `ai_build_system_prompt_ex(..., debug_tool_active(d), launch_reg)` (`launch_reg` only when `ai_chat_provider_tools(d)`) and `ai_conv_set_system(&d->conv, ...)` before `ai_conv_copy(&s->conv, &d->conv)` (~120, so move the copy after it); send (~511) and continue (~568) stop building it and keep only making sure `messages[0]` is a system turn (spec §3: send, continue and retry agree). Before building the body, `!ai_conv_tool_pairs_well_formed(&d->conv)` calls `launch_failed(d, "[request not sent: tool calls and results do not pair up; start a New Chat]")`, and the builder's failure then names its cause (the builder returns 0 for pairing, overflow and allocation alike, so check pairing first; the in-stream rebuild's bare "JSON rebuild failed" gets the same reasons).
- [ ] **Step 2: Run `make -j test`** (expected: pass) **and the Windows compile check.**
- [ ] **Step 3: Commit** (`feat(ai): register run_command at launch; one prompt site; freeze the snapshot`).

### Task B13: the stream thread ends at run_command

**Files:**
- Modify: `src/core/ai_stream.h` (`AiStream.run_command`), `src/ui/ai_chat_internal.h` (`AiResponseMsg`), `src/ui/ai_chat_send.c` (`ai_chat_launch_stream_thread`, `ai_chat_drain_stream_messages`, send and continue prompt calls), `src/ui/ai_chat_stream.c` (`ai_chat_stream_thread_proc`, `post_response`), `src/ui/ai_chat.c` (`WM_AI_RESPONSE` stale and error branches: free only)

**Interfaces:**
- Consumes: Tasks A1, B1, B2, B3, B4, B10, B12.
- Produces: `AiStream` gains `int run_command;`. `AiResponseMsg` (`ai_chat_internal.h` ~444) gains `AiToolCall *calls; int n_calls; int exec_inert; int fetch_registered;` (`exec_inert = s->run_command` on every done message; `fetch_registered` = the stream's registry holds `web_fetch`). A helper `void ai_response_msg_free(AiResponseMsg *m);` (internal) frees content, thinking, calls and the struct; every free site uses it.

- [ ] **Step 1: The stop.** Inside `if (run_tools)` (~486; `ai_tool_round_should_run` already keeps a cut-off reply out), in this order, ahead of today's `agentic_add_assistant_tool_msg_ex` (~507): `agentic_sanitize_tool_inputs(pending, K)`; unless `agentic_room_for_tool_round(&s->conv, NULL)` *(corrected by the review of B1-B11: the second argument is the note queue, which the thread cannot read; the plan passed the call count K)*, add the text as a plain assistant turn, post `CHAT_ITEM_STATUS` `"[conversation full: %d tool call(s) not run; start a New Chat]"` and end with `post_response(s, 2, ...)` and no calls; then today's `agentic_add_assistant_tool_msg_ex(&s->conv, ctx.full_content, ctx.full_thinking, provider, model, ...)` (keep the reasoning: Moonshot needs it back; always pass the provider and model; it makes up an empty or repeated call id as `call_<n>`, so read ids from the stored turn, not from `pending`; a -1 means the conversation filled meanwhile: the same "conversation full" row); then, when `s->run_command && runcmd_reply_has_call(pending, K)`: post `tool_fetch_off_row()` once per `web_fetch` call for which `tool_dispatch_rule(...) == TOOL_DISPATCH_FETCH_OFF`, heap-copy all K calls into the response with `fetch_registered`, post no "using tool" row, ask no tool card, run nothing, end the loop. Otherwise today's loop (trust's gate and fetch-off rule included); `agentic_add_tool_results` returning -1 or -2 ends it with the same "conversation full" row (the open turn is already dropped, R37); 1 posts a status row that results were reduced to notes. In the rebuild, a conversation failing `ai_conv_tool_pairs_well_formed` posts the same text as an error response instead of sending.
- [ ] **Step 2: Rejections and full conversations.** When a stream with `s->run_command` ends on an HTTP error and `runcmd_reject_hint_applies(status, error_text)` (Task B1), post the status row `runcmd_reject_hint()` after the error row. In-stream error objects never get it. A body the builder refuses (pairing, DeepSeek's cut reasoning) is shown with its reason (`launch_failed(d, reason)`), never "JSON build failed". The normal end's `ai_conv_add(&s->conv, AI_ROLE_ASSISTANT, ...)` (~717): on -1 post `"[reply not stored: conversation full; start a New Chat]"` (spec §24).
- [ ] **Step 3: Free paths.** `post_response` (~269) frees the calls when `PostMessage` fails; `ai_chat_drain_stream_messages` (`ai_chat_send.c` ~328-331), the stale branch (`ai_chat.c` ~1145-1151), the background branch's end (~1272) and the error branch of `WM_AI_RESPONSE` call `ai_response_msg_free`. Until Task B15 the done branch frees `calls` unread (run_command calls are not acted on yet; no build ships in between).
- [ ] **Step 4: Run `make -j test`** (expected: pass; Task B1 holds the hint's test) **and the Windows compile check.** `grep -n 'free(rmsg\|free(r->content' src/ui/*.c` shows only `ai_response_msg_free`.
- [ ] **Step 5: Commit** (`feat(ai): end the stream at run_command calls`).

### Task B14: notes through one door; the take for run_command replies

**Files:**
- Modify: `src/ui/ai_chat.c` (`WM_AI_RESPONSE`), `src/ui/ai_chat_status.c` (`apply_policy_change`), `src/ui/ai_chat_send.c` (`ai_chat_send_continue_message`), `src/ui/ai_chat_internal.h`

**Interfaces:** (`ai_chat_owner_conv` is Task A4's)
  - `int ai_chat_owner_add_note(AiChatData *d, AiSessionState *owner, const char *text);` `ai_conv_add_user_note(ai_chat_owner_conv(d, owner), &owner->notes, defer, keep_free, text)` (`keep_free` 0 unless the text launches, R54) with `defer` = a stream owned by `owner` is still in `d->streams` (walk `ai_stream_table_owner_at`), not `busy`: inside `WM_AI_RESPONSE`, after `ai_stream_table_finish`, `busy` is still 1 (Step 7) but nothing defers.

- [ ] **Step 1: Notes through one door.** Every `ai_conv_add(..., AI_ROLE_USER, ...)` in `src/ui` except the typed message in `ai_chat_send_user_message` goes through `ai_chat_owner_add_note`: `apply_policy_change`'s note, the policy-blocked note, the rejected, long and quoted NOTEs (both branches), and `ai_chat_send_continue_message`'s text **only when the door would queue it** (a run_command turn is open: the call returns 1, nothing launches, the text rides the relaunch). The launching path (nothing open) goes through the door too, with `keep_free` 1 and **no separate `ai_conv_flush_notes` first** (C0, R54: the door's own flush leaves the text's slot and the reply's; this replaces the earlier `ai_conv_flush_notes(..., 2)` step): on a return of 2 nothing launches and the caller shows `"[conversation full: ...; start a New Chat]"` like a typed message (spec decision 24); every other note passes `keep_free` 0. The door marks it, and since an `[EXEC]` request ignores the mark (R48: B12's launch passes `ai_chat_provider_runcmd(d)` to the freeze, the builder decides from the request's tools) it takes a fresh snapshot and its body is what it is today. `grep -n 'ai_conv_add([^)]*AI_ROLE_USER' src/ui/*.c` then shows one hit: the typed message in `ai_chat_send_user_message`. Only the door sets `AiMessage.nutshell_note`, so until each site is routed the snapshot fix (I6) does not apply; a note the door cannot store stays queued, in order (R39). `AiSessionState.notes` is cleared at New Chat and session reset. *(corrected by the review of B1-B11: the marker and the queue clearing were not named.)*
- [ ] **Step 2: The take** (spec §5, §16): only when `rmsg->n_calls > 0`, wParam 2, both branches: take the thread's conversation into `ai_chat_owner_conv` before `ai_stream_table_finish`; then `ai_conv_flush_notes(conv, &q, 1)` (a flush that precedes a relaunch leaves 1 free; notes that do not fit stay queued); index thinking on the last assistant message after the take. Every other reply, and every error, is stored exactly as today.
- [ ] **Step 3: Run `make -j test`** (expected: pass) **and the Windows compile check.**
- [ ] **Step 4: Commit** (`feat(ai): one door for Nutshell's notes; keep a run_command reply's turns`).

### Task B15: run_command calls become one command card

**Files:**
- Modify: `src/ui/ai_chat.c` (`WM_AI_RESPONSE`), `src/ui/ai_chat_internal.h`

- [ ] **Step 1: One batch builder.** Factor today's displayed and background batch code into `static CmdBatch *reply_batch_build(...)` used by both (displayed: `ai_rounds_batch_policy` and the pause row; background: `cmd_policy_attended(owner->policy)`). At `cmd_batch_add`'s eviction add one comment: a run_command batch is never evicted, since every path that starts a new stream removes it first (spec §8).
- [ ] **Step 2: Calls.** With `rmsg->n_calls > 0` and `runcmd_reply_has_call(calls, n)` (a call-free reply is not a run_command reply; unguarded it is answered "may call no other tool"): `r = runcmd_plan(...)`; NULL (out of memory) or a NULL from `cmd_batch_add` leaves the taken tool turn open, so answer every call "not run: Nutshell ran out of memory" through `agentic_add_tool_results` from a heap results array, or with `ai_conv_drop_open_tool_turn` when that fails too (`runcmd_plan` returns NULL for no calls and for out of memory alike; the core has no non-allocating answer helper) *(corrected by the review of B1-B11: the plan had no guard and no NULL path)*. Then in order (spec §5), none relaunching: displayed and `!d->active_term || !d->active_io` -> `SESSION_GONE`; `cmd_batch_full(&owner->batches)` -> `SET_FULL`; `r->n_slot == 0` -> `NO_SLOT` with the row `"[no command in this reply could run: %d refused; type a message to carry on]"`; else one batch (`batch->reply_msg` = the reply's index, set beside `conv_mark`): SLOT calls through `chat_approval_add_session` (a refusal leaves `entry` -1, answered "not run (refused by the approval queue)"), note, `CHAT_ITEM_COMMAND` row (displayed), `batch->runcmd = r`, `conv_mark`; then the reply's quoted rows in the entries left; no `ai_build_policy_blocked_note`. `!chat_approval_needs_user` settles and dispatches as today. Anywhere a cap is chosen, `runcmd_cap_bytes` takes the number of SLOT calls (`r->n_slot`), not `n_calls`. *(C0)* The calls given to `runcmd_plan`, the batch and the card are the stored turn's (`conv.messages[last].tool_calls`, ids made up where the provider's were empty), never `rmsg`'s own; B15 or B16 carries a native test with an id-less `run_command` call that stores, pairs and builds a request.
- [ ] **Step 3: Markers.** With `exec_inert`, extracted markers go in only through `chat_approval_add_quoted` (in the batch, as above); markers that land in no batch become `CHAT_ITEM_STATUS` rows `"quoted, not run: <command>"` (displayed only). A reply with no calls stores `ai_build_runcmd_marker_note` through `ai_chat_owner_add_note` instead of the three notes.
- [ ] **Step 4: No relaunch here.** The handler starts no request of its own; after `src->busy = 0` and the Send button restore, `ai_chat_maybe_start_next_batch(d)` as today (a batch that dispatches at once).
- [ ] **Step 5: Run `make -j test`** (expected: pass) **and the Windows compile check.**
- [ ] **Step 6: Commit** (`feat(ai): run_command calls become one command card`).

### Task B16: the finish function and the causes

**Files:**
- Modify: `src/ui/ai_chat_dispatch.c`, `src/ui/ai_chat_send.c` (`ai_chat_send_tool_results`), `src/ui/ai_chat_internal.h`

**Interfaces:**
  - `void ai_chat_send_tool_results(AiChatData *d, int unattended);` begins with `ai_chat_send_continue_message`'s guard unchanged (`!d || !d->active_state || d->active_state->busy` returns); then `ai_rounds_continue`, `ai_conv_flush_notes(..., 1)` (the reply's slot stays free; notes that do not fit stay queued), indicator `"continuing"`, `ai_chat_launch_stream_thread(d, "continue")`. No USER turn.
  - `void ai_chat_runcmd_finish(AiChatData *d, AiSessionState *owner, CmdBatch *batch, RunCmdCause cause);`
  - `void ai_chat_runcmd_answer_reply(AiChatData *d, AiSessionState *owner, RunCmdReply *r, RunCmdCause cause);` whole-reply answer with no batch: `runcmd_build_results(r, NULL, cause, ...)`, fit, append, free, flush notes (`keep_free` 0: this function never relaunches; notes that do not fit stay queued), `runcmd_reply_free(r)`.

- Consumes: Tasks B7, B8, B9, A4.
- Produces: `void ai_chat_dispatch_cancel(AiChatData *d, const char *status_msg, int notify, RunCmdCause cause);` (all six callers updated: Stop `ai_chat.c` ~449, New Chat ~512, `do_session_switch` ~2283, `ai_chat_notify_session_closed` ~2458, the tick's session-ended branch `ai_chat_dispatch.c` ~715, the ambiguous-prompt timeout ~880).

- [ ] **Step 1: `ai_chat_runcmd_finish`**, in this order (everything here runs on the UI thread; `d->active_term` is NULL on SESSION_ENDED and PANEL_CLOSED, so guard it, and use the owner's conversation, never `d->conv`; before any relaunch check `agentic_room_for_tool_round(conv, &owner->notes)`, R45): when the batch's EXECUTING entry has a call, `d->active_term` exists and the owner is displayed, `runcmd_capture_take`; `runcmd_build_results`; `runcmd_fit(res, n, runcmd_request_estimate(conv, d->provider, RUNCMD_TOOLS_ESTIMATE, d->last_snapshot_len), runcmd_budget(AI_BODY_MAX, ai_model_context_limit(conv->model)))`, with `res` on the heap (`out[64]` is about 40 KB: not on the stack; free each `res[i].content` after the store); when the path's plan appends, `agentic_add_tool_results` into `ai_chat_owner_conv` with the stored assistant turn's calls in the same order as the results (else no part is recognised as run_command: not framed, not aged, not exempt from the cap) and its return handled as R37 (1, -1, -2 each show a status row; the turn is already dropped on a negative one), then `runcmd_age_results` on it right after (the estimate already assumes it), and `ai_conv_flush_notes(..., &owner->notes, keep_free)` (1 when the path relaunches, 2 for `TYPED`, else 0: a flush that precedes a relaunch leaves the reply's slot free and notes that do not fit stay queued, R47); free the results; settle the rows when displayed; read `user_acted` (set for Stop and Retry as well as a typed message and New Chat), then `ai_chat_batch_remove_recorded` (Task A4); then when `runcmd_cause_relaunches(cause)` and the owner is displayed and not busy -> `ai_chat_send_tool_results(d, !user_acted)`; otherwise nothing (a background owner never relaunches). `ai_chat_runcmd_answer_reply` is the same without capture, settle or remove, never relaunches, and appends the status row of spec §5 (displayed) or stores it for the rebuild (background). *(C0)* Call ids come from the stored assistant turn, never the provider's original; an id-less `run_command` call stores as `call_<n>`, its result pairs with that id and a request builds from it (native test in B15 or here).
- [ ] **Step 2: Decisions from the table.** Every "does this path append, relaunch, capture" question is answered by Task B9's `runcmd_path_plan`, never by a condition written in `src/ui`.
- [ ] **Step 3: Capture.** `execute_command` calls `runcmd_capture_start(&call->cap, d->active_term)` (a `const Terminal *` since R46: it writes nothing to the terminal) on `runcmd_call_for_entry(batch->runcmd, idx)` just before that command's first write. Both `chat_approval_set_completed` sites in `ai_chat_dispatch_tick` call `runcmd_capture_take(&call->cap, d->active_term, call->command, runcmd_cap_bytes(r->n_slot))`; the `captured` flag makes every later ready tick a no-op.
- [ ] **Step 4: Causes.** A batch with `runcmd` never reaches either continue-text builder; instead: the tick's settle branch -> `SETTLED`; `ai_chat_settle_batch_if_done` (nothing runnable) -> `DECIDED`; the ambiguous-prompt timeout -> `TIMEOUT`; Stop -> `STOP`, plus `ai_rounds_user_message`; the session-ended branch -> `SESSION_ENDED`; `do_session_switch` -> `TAB_SWITCH`, moved before `ai_conv_take`; `ai_chat_notify_session_closed` -> `SESSION_CLOSED`; `WM_DESTROY` while dispatching -> `PANEL_CLOSED` for the active session's run_command batch, before the save-back take (~1893; `WM_DESTROY` calls no `ai_chat_dispatch_cancel` today, it only clears `dispatch_active`). `cmd_batch_set_free` at session free needs no call (Task B5's release frees the state). `ai_chat_abort_session_stream` flushes the owner's queued notes (`keep_free` 0: nothing relaunches) after the abort (a stopped stream's reply never arrives to flush them).
- [ ] **Step 5: Run `make -j test`** (expected: pass) **and the Windows compile check.**
- [ ] **Step 6: Commit** (`feat(ai): every run_command batch ends with its results and a cause`).

### Task B17: typed message, New Chat, Retry and tab show

**Files:**
- Modify: `src/ui/ai_chat.c` (`IDC_CHAT_NEWCHAT`, `IDC_ACTIVITY_RETRY`, `do_session_switch`), `src/ui/ai_chat_send.c` (`ai_chat_send_user_message`)

- [ ] **Step 1: Typed message, New Chat, Retry.** `ai_chat_send_user_message`: when `cmd_batch_runcmd(&state->batches)`, first `ai_conv_room_for_send(&d->conv, batch != NULL, &state->notes)` *(corrected by the review of B1-B11: the queue is the third argument)*: when 0, keep the text in the input box, append `"[conversation full: your message was not sent; start a New Chat]"` and return (spec §24); then finish the batch `TYPED` (its flush leaves 2 slots free, the typed message's and its reply's; notes that do not fit stay queued, R47), then the user's turn. New Chat: finish `NEW_CHAT` before `cmd_batch_set_free`. Retry, before today's abort and pop: `runcmd_retry_plan(&d->conv, cmd_batch_runcmd(...) != NULL)`; CANCEL_BATCH -> `ai_chat_dispatch_cancel(d, NULL, 0, RUNCMD_CAUSE_RETRY)` (or finish a batch not dispatching) and return, the finish relaunches; POP_ASSISTANT -> today's pop, then `ai_conv_snapshot_trim(&d->conv)` (every pop of messages off the end, here and wherever else one is made), and relaunch; RELAUNCH -> relaunch without popping.
- [ ] **Step 2: Tab show.** `do_session_switch` starts no request for a run_command conversation: the rebuilt view shows the stored rows and any waiting card; nothing else changes there.
- [ ] **Step 3: Run `make -j test`** (expected: pass) **and the Windows compile check.** `grep -n 'cmd_batch_remove\|cmd_batch_set_free\|ai_build_continue_text' src/ui/*.c`: each hit is the finish, a text-batch path guarded by `!batch->runcmd`, or preceded by a finish.
- [ ] **Step 4: Commit** (`feat(ai): a typed message, New Chat and Retry finish a run_command batch`).

### Task B18: the rebuilt view and the truncation row

**Files:**
- Modify: `src/ui/ai_chat_view.c` (`ai_chat_rebuild_display`), `src/ui/ai_chat_stream.c` (the live warning)

- [ ] **Step 1: Implement.** Rebuild (spec §16 Display): an assistant turn's run_command calls show `"using tool: run_command - <command>"` (`runcmd_describe`) followed by that call's result note, and no preview row of its output; Task A4 replaces this with the replayed command row when a run-log record matches, so a command is shown once. Other tools' parts show their `tool_note` (when set) and the first 200 bytes of `content`, cut on a UTF-8 boundary. The truncation row appears only when `omitted_bytes > 0` or `tool_note` contains `AI_TOOL_TRUNC_NOTE`, never for content longer than the preview; it and the live warning call `agentic_truncation_warning(name, omitted_bytes, ...)` (with 0 it says "truncated at 64 KiB", so a part whose cut is a note only is not passed 0 unless its note holds `AI_TOOL_TRUNC_NOTE`). The rebuild also walks a parts-less `AI_ROLE_TOOL` message (one part with its `tool_call_id`, as the builder counts it) and shows each part's note. *(corrected by the review of B1-B11.)*
- [ ] **Step 2: Run `make -j test`** (expected: pass, `test_ui_tokens` green) **and the Windows compile check.**
- [ ] **Step 3: Commit** (`fix(ai): rebuilt tool rows show the command, the note and only real cuts`).

### Task B19: DeepSeek's reasoning, whole or not at all (spec §1, §23; Moonshot unchanged)

**Files:**
- Modify: `src/core/ai_prompt.h` (CRLF: `AiMessage.reasoning_cut`), `src/core/ai_prompt.c` (CRLF: `build_body_core` gains `int with_tools`, `ai_build_request_body_frame` passes `tools_json && tools_json[0]`, the other wrappers 0; `ai_msg_set_reasoning`), `src/core/ai_agentic.c`, `src/ui/ai_chat_stream.c` (the accumulator ~214 marks a drop; the normal end ~717 stores reasoning), `tests/test_agentic_loop.c`, `tests/test_run_command.c`, `tests/runner.c`

**Interfaces:**
- `AI_REASONING_MAX` and the stream's 16 KiB accumulator stay. `AiMessage` gains `int reasoning_cut;`: set when the accumulator dropped reasoning, when `AI_REASONING_MAX` cut it, or when its copy failed to allocate.
- The thread's normal end stores `ctx.full_thinking` on the turn it adds (`ai_msg_set_reasoning`); only the DeepSeek rule reads plain turns.
- `deepseek` with `with_tools`: every assistant turn carries `"reasoning_content":` its stored reasoning or `""`; a conversation with any `reasoning_cut` turn makes the builder refuse with the reason shown as spec §23's row. Without tools: none. *(Corrected 2026-11-11, fix round 3 H1, maintainer's "off until tested": "with tools" means a tools array that offers `run_command` (`tools_offer_runcmd`, `deepseek_reasoning_applies`); a DeepSeek request with web tools only is built as at `6432ca0`, pinned by `test_body_deepseek_pinned_no_runcmd`.)*
- `moonshot`: exactly as shipped (tool-call turns only). Anthropic, OpenAI, Gemini, Custom: none.
- `runcmd_request_estimate` counts reasoning for `moonshot` tool-call turns and for every `deepseek` turn when `tools_json_len > 0`.
- **If DeepSeek's settling test fails** on a model (a 400 saying `reasoning_content` is not accepted): `deepseek` reverts to none sent, `ai_runcmd_auto_default("deepseek")` stays 0, README says so; one commit.

- [ ] **Step 1: Write the failing tests** (`tests/test_agentic_loop.c`; change `test_ai_reasoning_other_providers_none` (~320) to drop `deepseek`, keeping openai, anthropic, gemini, custom).

```c
int test_ai_reasoning_deepseek_tools_every_turn(void);
/* "deepseek", tools_json set: user; asst(tool call, reasoning "r1"); tool; asst plain (reasoning "r2"); asst plain (none); user
   -> body has "\"reasoning_content\":\"r1\"", "...\"r2\"" and one "\"reasoning_content\":\"\""; json_validate ok */
int test_ai_reasoning_deepseek_no_tools_none(void); /* same conversation, tools_json NULL -> no "reasoning_content" */
int test_ai_reasoning_deepseek_cut_not_sent(void);  /* a turn with reasoning_cut 1, tools set -> builder refuses with the reason */
int test_ai_reasoning_oom_sets_cut(void);           /* the allocation-failure hook on the reasoning copy -> reasoning_cut 1 */
int test_ai_reasoning_moonshot_unchanged(void);     /* the same conversation to "moonshot": reasoning_content on the tool-call turn only,
                                                       and Task B11's test_body_moonshot_pinned still byte-identical */
/* tests/test_run_command.c */
int test_runcmd_estimate_reasoning_rules(void);     /* moonshot tool turns counted; deepseek all turns only when tools_json_len > 0; openai not */
```

- [ ] **Step 2: Run `make -j test`.** Expected: failures in the new tests.
- [ ] **Step 3: Implement.** Keep `ai_prompt.*` CRLF.
- [ ] **Step 4: Run `make -j test`**, **`make test SAN=1`** and **the Windows compile check.**
- [ ] **Step 5: Commit** (`fix(ai): send DeepSeek its reasoning back whole on a tool conversation, or not at all`). The body names the settling test and its fail branch.

### Task B20: Gemini on tools, with thought signatures (spec §1)

**Files:**
- Modify: `src/core/ai_tools.h` (`AiToolStreamState.sig`, `sig_len`, `sig_cap`), `src/core/ai_tools.c` (`openai_accumulate_tool_calls` ~692-765, `ai_tools_stream_reset` frees the buffer), `src/core/ai_prompt.h` (CRLF: `AiMessage.thought_sig`), `src/core/ai_prompt.c` (CRLF: `ai_provider_tools`, the `tool_calls` writer ~1052, `ai_msg_free`/`ai_conv_copy`/`_take`), `src/core/ai_agentic.c` (`agentic_add_assistant_tool_msg_ex` gains `const char *thought_sig`), `src/ui/ai_chat_stream.c` (pass `tool_stream_ptr->sig`), `tests/test_ai_tools.c`, `tests/test_agentic_loop.c`, `tests/test_run_command.c`, `tests/runner.c`

**Interfaces:**
- The accumulator copies the first `tool_calls[i].extra_content.google.thought_signature` of a response into `state->sig`, a heap buffer grown as needed (no cap: Google's page states no size), and ignores any later one in the same response (only the first call of a parallel set carries one).
- `AiMessage` gains `char *thought_sig;` (heap, NULL when none), owned, copied and moved with the message.
- The builder writes `,"extra_content":{"google":{"thought_signature":"<escaped>"}}` on the turn's **first** tool call when the provider is `gemini` and `thought_sig` is set; nothing otherwise, and never the placeholder `skip_thought_signature_validator` (a turn recorded under another provider is text, Task B11).
- `ai_provider_tools("gemini", mode)` returns 1 (the web tools come with it).

- [ ] **Step 1: Write the failing tests.**

```c
/* tests/test_ai_tools.c */
int test_ai_stream_gemini_signature_captured(void); /* a delta with tool_calls[0] {id, function, extra_content.google.thought_signature
                                                       "c2ln"} and no "index" -> one pending call, sig "c2ln" */
int test_ai_stream_gemini_parallel_first_only(void); /* two index-less calls, the first with a signature -> sig is the first's */
int test_ai_stream_gemini_long_signature_kept(void); /* a 20 KB signature -> kept whole */
/* tests/test_agentic_loop.c */
int test_body_gemini_echoes_signature_first_call(void); /* "gemini", 2 calls: the block on the first call only; json_validate ok */
int test_body_gemini_no_signature(void);          /* thought_sig NULL -> no "extra_content" */
int test_body_openai_never_signature(void);       /* the same turn to "openai", "moonshot", "deepseek" -> no "extra_content" */
/* tests/test_run_command.c */
int test_provider_runcmd_gemini(void);            /* gemini AUTO 1, OFF 0, ai_provider_tools 1 */
```

- [ ] **Step 2: Run `make -j test`.** Expected: compile failure.
- [ ] **Step 3: Implement.** Keep `ai_prompt.c` CRLF.
- [ ] **Step 4: Run `make -j test`** (all pass, warnings 0), **`make test SAN=1` and the Windows compile check.**
- [ ] **Step 5: Commit** (`feat(ai): tools for Gemini, thought signatures passed back`).

### Task B21: the setting "Commands as tool calls" (spec §1)

One task, so it ends green: the help id, its table tests and its control in `settings.c` land together (`test_settings_help_every_id_in_settings_c` reads `settings.c`).

**Files:**
- Modify: `src/config/config.h` (LF: `Settings.ai_runcmd_mode`), `src/config/loader.c` (CRLF: default 0 beside `ai_web_fetch_enabled` ~548, load ~727, save ~1061), `src/core/settings_help.h` (`SH_AI_RUNCMD_MODE` after `SH_AI_BASE_URL`), `src/core/settings_help.c` (its entry; Base URL's text ~225 and Provider's ~180-186 name it), `src/ui/settings.c` (a three-item combo, `IDC_AI_RUNCMD_MODE`, on its own row after Base URL and shown for every provider, unlike Base URL's custom-only row ~1391), `src/ui/resource.h`, `src/ui/ai_chat.h` (CRLF: `void ai_chat_set_runcmd_mode(HWND hwnd, int mode);`), `src/ui/ai_chat.c`, `src/ui/ai_chat_internal.h` (`AiChatData.runcmd_mode`), `src/ui/ai_chat_send.c` (the one-time note ~245-249), `src/ui/window.c` (pass it beside `ai_web_fetch_enabled` ~2401, ~2472), `tests/test_config.c`, `tests/test_settings_help.c`, `tests/runner.c`

**Interfaces:**
- `int ai_runcmd_mode; /* AI_RUNCMD_AUTO 0 (default), AI_RUNCMD_ON 1, AI_RUNCMD_OFF 2 */`, JSON key `"ai_runcmd_mode"` as a string `"automatic"`, `"on"`, `"off"` (an unknown value loads as automatic).
- Help entry: `SETTINGS_PAGE_AI_PROVIDER`, `SH_KIND_SETTING`, label `"Commands as tool calls:"`, default `"Automatic"`, text exactly as spec §1 gives it.
- The combo is drawn and sized like the page's other combos, its tooltip from `settings_help`; no new size or colour.
- The one-time note: `custom` not On keeps `"[Note: a Custom endpoint gets tools only with Settings > Provider > Commands as tool calls set to On]"`; every other provider that is not offered web tools while a tool registry exists (gemini on Automatic or Off, an unknown provider) gets main's general note, reworded for what is true now: `"[Note: web search/fetch tools are offered to Anthropic, OpenAI, DeepSeek and Moonshot; Gemini gets them with Settings > Provider > Commands as tool calls set to On]"`. *(fix round 3, H4, 2026-11-11)* The decision is the core predicate `ai_provider_tools_note(provider, mode, tools_configured)` (none / general / custom; table test `test_provider_tools_note_table`), the texts `ai_provider_tools_note_text`; `ai_chat_launch_stream_thread` only shows the result, once per panel life (`tool_support_notified`).

- [ ] **Step 1: Write the failing tests, and update the existing ones in the same step:** `test_settings_help.c`: `SH_COUNT` 46 -> 47 (~105), the new row in `k_names[]` (~81) in enum order, `render_default` (~214) gains `case SH_AI_RUNCMD_MODE` rendering `"Automatic"`/`"On"`/`"Off"`; `test_settings_help_every_id_in_settings_c` then needs the id in `settings.c` (Step 3). `test_config.c`: default automatic; a round trip of each value (beside `ai_tool_auto_rounds` ~193-208); a file without the key and one with `"bogus"` load automatic.
- [ ] **Step 2: Run `make -j test`.** Expected: failures in exactly those tests.
- [ ] **Step 3: Implement** config, loader (CRLF), help entry, the combo, the panel setter and its plumbing.
- [ ] **Step 4: Run `make -j test`** (all pass, `test_ui_tokens` and every `test_settings_help_*` green) **and the Windows compile check.**
- [ ] **Step 5: Commit** (`feat(ai): Commands as tool calls: Automatic, On or Off per provider`).

### Task B22: README

**Files:**
- Modify: `README.md` (LF). (Every `settings_help.c` text is Task B21's.)

- [ ] **Step 1: Edit.** In the AI Assist and Command Execution sections (the tools paragraph ~256 says "Tools work with Anthropic, OpenAI, DeepSeek and Moonshot (Kimi)"; Gemini and Custom "get no tools"): with "Commands as tool calls" On, the assistant runs commands through a `run_command` tool, each call an ordinary card under the same policy, ceiling, sensitive-read and Auto-approve rules, never expiring (the Tool approval timeout is for tool cards); Automatic is On for Anthropic, OpenAI and Moonshot and Off for DeepSeek, Gemini and Custom, whose tool calls are written from their documentation and not yet verified against the live services (On enables them); Off keeps `[EXEC]` markers, and in a run_command chat a marker is shown "quoted, not run"; the output of each command (up to 16 KiB, shared when more than four run, older rounds' output dropped to save room) goes back as data; Stop, Deny all and a reply with nothing runnable start no new request; Save As keeps each `$ command` and its output. Refresh the test counts from `make -j test` and `ls tests/test_*.c | wc -l` where README states them.
- [ ] **Step 2: Run `make -j test`.** Expected: pass (`test_shortcuts` still matches its block).
- [ ] **Step 3: Commit** (`docs(readme): the run_command tool`).

## The cut point

### Task V: version bump, Windows build and gate checks (at a cut point, only when told)

**Files:**
- Modify: `src/ui/resource.h` (`APP_VERSION`, `APP_VERSION_BINARY`), `README.md` (`**Version**:`), `build/win/nutshell.exe`

- [ ] **Step 1: Version.** Confirm `hostname` prints `automaton`. `git fetch origin main`; one above both `git show origin/main:src/ui/resource.h | grep APP_VERSION` and the branch's last build, at build time (after `X.Y.99` comes `X.(Y+1).0`), in both macros and README.
- [ ] **Step 2: Build.** `make clean && make release`. On failure `git restore build/win/nutshell.exe` before anything else.
- [ ] **Step 3: Tests**, each run alone: `make -j test 2>&1 | tee build/t.log` (`Failed: 0`, `grep -c 'warning:' build/t.log` 0, count noted against 3695); `make test SAN=1` (0 failed, no sanitizer report); `make lint-gate` (exit 0); `make fuzz && make fuzz-run`; `xvfb-run -a make wintest` (0 failed, count noted against 51; on "no driver", `pgrep -a wine` first, `wineserver -k` only if no other session's run shows).
- [ ] **Step 4: Gate.** `bash .github/scripts/check-version.sh origin/main` passes.
- [ ] **Step 5: Checklist.** `git diff origin/main --stat` equals `--ignore-cr-at-eol`; `git ls-files --eol` shows `i/crlf` for every CRLF file in Global Constraints; `git status --short` has no stray `??`; `grep -rn 'RGB(' src/ui --include=*.c | grep -v ns_draw.c` unchanged from `main`.
- [ ] **Step 6: Commit** `build: vX.Y.Z` staging `src/ui/resource.h README.md build/win/nutshell.exe` by path. Do not push, mark ready or merge. At the Part A cut, the maintainer checks on Windows: a five-command `[EXEC]` batch read while it runs (ran, running, queued, summary line), then after a tab switch and back (rows still under their reply); an `[EXEC]` reply with a quoted marker plus commands, same check; Stop during `sleep 120` ("unfinished"); `--ui-demo=runlist` on four themes and the `ui_gallery` case; a model changed in Settings survives a tab switch. At the Part B cut, add spec "Verification before the gate": Anthropic, OpenAI and Moonshot `kimi-k3.1-pro` on Automatic; DeepSeek's settling test on its three models and Gemini with On; Custom on Automatic (`[EXEC]`, no tools) and On; two calls, one card, Deny one, Run one, and the resumed reply knows both; `web_fetch` beside a call with Permit Web Fetch off shows the fetch-off row; Stop mid-batch starts no reply; Retry during `sleep 120; echo done` does not re-ask the command; a typed message over a card; a reply of invalid calls only ends with its row and no new request; a background tab's reply waits for the user on its tab; raise the ceiling over a BLOCKED card and Run (no 400); Auto-approve at Read: `ls` three rounds, the fourth carded; `cat ~/.ssh/id_rsa` carded; `less /etc/hosts`; a resize and a `tmux` pane mid-command (screen-rows note); a 5 KB heredoc ("needs a command string", chat continues); Save As shows `$ command` and output; a switch from Anthropic to Moonshot mid-chat (text-form turns); a full conversation refuses a typed message and keeps it in the box; `--ui-demo=all` on four themes.
