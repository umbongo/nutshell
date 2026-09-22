# Command cards tell the truth: run states, no silent truncation, stall reporting

**Date**: 2026-09-23
**Status**: DRAFT, revised after one Opus critique (see the Critique section)
**Branch**: `dispatch-states`, version **1.2.2**
**Fixes**: the maintainer's report of 2026-09-23: the assistant proposed three commands, the
panel showed all three as "ran", the terminal showed only the first, truncated mid-argument,
with bash sitting on its continuation prompt.
**Builds on**: `2026-09-07-command-dispatch-and-auto-approve-levels.md`,
`2026-09-09-pending-command-batches.md`.

---

## Problem

Three independent defects lined up:

1. **Commands are cut at 1024 bytes without a word.** `ai_extract_commands_ex()` copies an
   `[EXEC]` block into `char cmds[][1024]` and clamps (`src/core/ai_prompt.c:1177`);
   `chat_approval_add()` clamps again (`src/core/chat_approval.c:42-46`). A long single-line
   `printf` of a whole script reached bash missing its closing quote.
2. **"ran" means "approved".** A batch settles when every entry has a decision
   (`settle_batch_if_done`, `src/ui/ai_chat.c:1378-1390`), before a byte is sent, and the
   settled row paints "ran" from `approved == 1` (`src/ui/chat_listview.c:1601-1689`).
   `ChatMsgItem.u.cmd` has no execution state (`src/core/chat_msg.h:38-50`); the queue's
   `APPROVE_EXECUTING`/`APPROVE_COMPLETED` never reach a card, and `dispatch_cancel`
   force-denies what it never sent (`ai_chat.c:1767-1779`), so a stopped batch says "denied".
3. **A shell waiting for more input looks like a hang.** The dispatcher refuses a bare `>`
   as a prompt (`src/core/shell_prompt.c:18-20`) and waits for ever; nothing tells the user,
   and the model's next turn is told nothing either. For zsh the opposite happens: its
   continuation prompts (`dquote> `, `cmdsubst> `) end in `>` after a word and pass as
   primary prompts, so the next command is typed into the open quote.

---

## 1. Command length: reject, never truncate

- The limit stays **1024** (`ApprovalEntry.command`, `cmds[][1024]`, `CMD_MAX_LEN` 1023).
  Raising it means three public prototypes, about 45 test declarations, and two 64 KB
  arrays on the UI thread's 1 MB stack; the bug is the silence, not the number. A model told
  the limit writes the file in steps, which is what a 5 KB `printf` should have been anyway.
- `ai_extract_commands_ex()` gains an out-parameter `rejected_long` beside `rejected`. An
  `[EXEC]` block longer than 1023 bytes after trimming is **skipped**: no `cmds[]` slot, no
  queue entry, **no card**, so the card-to-queue-entry mapping that the approve buttons and
  the auto-approve walk depend on (`chat_msg_batch_index`, `ai_chat.c:3641-3650`) is
  untouched.
- Both call sites (`ai_chat.c:3615` and `3692`) report it. The status line buffer grows from
  80 to 160 bytes and reads `[1 command not run: over the 1023-byte limit; ask for it in
  smaller steps]`; the note the model receives says the same, including the count and the
  limit, in both the displayed and the background-session branch (`ai_chat.c:3629-3640`,
  which today emits nothing for rejects).
- `chat_approval_add()` rejects (returns -1) instead of clamping, and its two callers treat
  -1 as "not added". Unreachable from the AI path after this change; it exists so no future
  caller can truncate.

---

## 2. Cards show the queue's state

No new state machine: the queue already has `PENDING / APPROVED / DENIED / BLOCKED /
EXECUTING / COMPLETED`, and the dispatcher already maintains it. The card learns it.

- `ChatMsgItem.u.cmd` gains `int run`: `CHAT_RUN_NONE`, `CHAT_RUN_RUNNING`,
  `CHAT_RUN_DONE`, `CHAT_RUN_ABORTED`. It is never set by hand; one function,
  `chat_msg_batch_sync_run(list, batch_id, const ApprovalQueue *q, int batch_ending)`,
  walks the batch's COMMAND items in list order (which is `q.entries[]` order, since section
  1 keeps the two sets identical) and maps each entry's status:
  `EXECUTING` → RUNNING; `COMPLETED` → DONE; `APPROVED` → NONE while the batch is alive,
  ABORTED when `batch_ending`; `EXECUTING` with `batch_ending` → ABORTED (stopped in
  flight); everything else → NONE.
- It is called from the four places the queue changes during dispatch:
  `chat_approval_set_executing`, `chat_approval_set_completed` (both via `dispatch_tick`),
  the end of a batch, and `dispatch_cancel` **before** `cmd_batch_remove` with
  `batch_ending = 1`. It is also called by `append_batch_command_items`
  (`ai_chat.c:1032-1060`) when a display is rebuilt from the queue, so a tab switch or the
  `--ui-demo=executing` state shows the same labels as the live panel.
- `dispatch_cancel` stops force-denying: an entry the dispatcher never sent stays APPROVED
  in the queue and becomes ABORTED on the card, which is the honest label.
- **The label is a pure function**, `chat_cmd_label(approved, blocked, run)` in
  `src/core/chat_msg.c`, used by `paint_cmd_settled_row` and tested:

  | approved / blocked | run | label | intent |
  |---|---|---|---|
  | blocked | any | held | warning |
  | denied | any | denied | dim |
  | pending, not blocked | any | skipped | dim |
  | approved | NONE | queued | dim |
  | approved | RUNNING | running | info |
  | approved | DONE | ran | success |
  | approved | ABORTED | not run | warning |

  All four intents exist in `ns_tokens()`; no new colour literal.
- Settling is unchanged. Repaint stays whole-list.

---

## 3. A shell waiting for more input: report, do not guess

- `shell_prompt_is_continuation(text)` in `src/core/shell_prompt.c`: the trimmed line is a
  bare `>`, or one of zsh's secondary-prompt words followed by `>` (`dquote`, `quote`,
  `bquote`, `cmdsubst`, `heredoc`, `pipe`, `cmdand`, `cmdor`, `braceparam`, `math`,
  `cursh`, `then`, `do`, `done`, `for`, `foreach`, `while`, `until`, `repeat`, `if`, `elif`,
  `else`, `fi`, `case`, `esac`, `select`, `function`, `array`, `end`). `shell_prompt_line()`
  returns 0 for anything `shell_prompt_is_continuation()` accepts, so zsh's continuation
  prompt stops passing as a primary one; `router>` and `foo>` still pass, as the existing
  tests require.
- `term_at_continuation_prompt(term)` in `src/term/buffer.c` shares the cursor-row lookup
  with `term_at_prompt` (a static helper extracted from it).
- **No automatic Ctrl+C.** A command that prompts with `> ` itself (`read -p '> '`, a REPL)
  is indistinguishable from an unclosed quote, and killing a working command is worse than
  waiting. Instead `dispatch_tick`, whenever it is waiting (a command in flight **or** the
  first command not yet sent, which covers a quote the user opened by hand), the terminal is
  quiet, and the row is a continuation prompt, appends **one** status line per stall:
  `[the shell is waiting for more input (a ">" prompt). Finish the line in the terminal, or
  press Stop to cancel.]` and keeps waiting. The in-flight card shows "running", the rest
  "queued"; nothing lies.
- **Stop** (`dispatch_cancel` from the button) additionally sends `\x03` through
  `io->write` when the terminal is at a continuation prompt, so bash discards the partial
  line and the user gets a prompt back. Stop at a primary prompt sends nothing, as today.
- **The model is told.** `ai_build_continue_text()` (`src/core/ai_prompt.c:1387`, six
  existing tests in `tests/test_ai_prompt.c:790-860`, which are updated) takes the queue and
  lists every command's outcome, one line each, the command elided to 60 characters:
  `1. ran: printf ...`, `2. not run (batch stopped): chmod +x ...`. Its buffer grows to
  4096 and the caller checks the return value. It is sent as the continue message at batch
  end as today, and **also on Stop and on session end**, which today cancel silently; that
  is one model turn per abort, the same cost as a completed batch.
- **Not a timeout.** A command that takes long keeps the dispatcher waiting; Stop is the way
  out.

---

## 4. Tests

Native, `make test`:

- `tests/test_ai_prompt.c`: a 5,000-byte `[EXEC]` block is skipped with `rejected_long == 1`
  and the commands around it arrive intact and in order; a block of exactly 1023 bytes is
  accepted whole; `ai_build_continue_text()` with a queue of three where the second is
  APPROVED-never-sent and the batch ended names all three outcomes and elides a long command.
- `tests/test_chat_approval.c`: `chat_approval_add()` returns -1 for 1024 bytes and adds
  nothing.
- `tests/test_chat_msg.c`: `chat_cmd_label()` for every row of the table;
  `chat_msg_batch_sync_run()` with a batch of three (COMPLETED, EXECUTING, APPROVED) alive
  and ending, with earlier items settled, and with an unrelated batch interleaved.
- `tests/test_shell_prompt.c`: `shell_prompt_is_continuation()` for `>`, `> `, `dquote> `,
  `cmdsubst>`, and the negatives `foo>`, `router>`, `$`, empty; `shell_prompt_line()` now 0
  for `dquote> `.
- `tests/test_term.c`: `term_at_continuation_prompt()` true on a terminal fed
  `printf 'abc\r\n> `, false on `user@host:~$ `; `term_at_prompt()` unchanged for both and
  false for `dquote> `.
- `tests/test_ui_demo.c`: the `executing` demo state's cards carry RUNNING/DONE after
  rebuild.

Windows, manual, with the local shell (no AI key needed for the terminal side; one cheap
model turn for the batch): a command with an unclosed quote followed by two more, expecting
"running" / "queued" / "queued", the stall line once, then Stop giving "not run" on all
three and a prompt back in the terminal.

---

## 5. Out of scope

Raising the 1024 limit; a timeout for commands that never return; multi-line `[EXEC]`
blocks; a per-row repaint; the 16-command batch limit; moving `dispatch_tick` into
`src/core` so it can be tested natively (worth doing, noted in the notes document's open
list).

---

## Critique

One round, Opus (`claude-opus-5`), 2026-09-23, briefed per the `critique` skill: twelve
findings and a cheaper route. Dispositions:

1. A "too long" card breaks the card-to-queue index invariant. **Accepted**: no card; the
   block is skipped and reported.
2. `chat_cmd_label` could not express "too long". **Accepted**: the row is gone with the card.
3. Stop mid-batch leaves the in-flight card "running". **Accepted**: `EXECUTING` with
   `batch_ending` maps to ABORTED.
4. zsh's `dquote>` passes as a primary prompt. **Accepted**: the zsh vocabulary is in
   `shell_prompt_is_continuation()` and `shell_prompt_line()` defers to it.
5. Rebuilding the display resets the state. **Accepted**: `append_batch_command_items`
   syncs from the queue; the demo state gets a test.
6. `ai_build_continue_text()` exists elsewhere with pinned tests and 1024-byte buffers.
   **Accepted**: location, signature, tests and buffer size are now in section 3.
7. The rejected-command status line is 80 bytes and absent in the background branch.
   **Accepted**: 160 bytes, both branches.
8. Widening to 4096 touches 45 sites and two 64 KB stack arrays. **Accepted**: the limit
   stays at 1024.
9. `chat_approval_add` still clamps and a NULL command paints an empty row. **Accepted**:
   it rejects; no card is created for a reject.
10. Automatic Ctrl+C kills a command that prompts with `>`. **Accepted**: no automatic
    Ctrl+C; report the stall, Stop sends it.
11. A quote opened by hand before the batch is still a silent hang. **Accepted**: the stall
    check runs whether or not a command has been sent.
12. `dispatch_tick` is untestable natively. **Accepted as a limitation**: manual check
    listed; moving the dispatcher into `src/core` is recorded as an open item, not done here.

Cheaper route (keep 1024, no card, derive the label from the queue). **Accepted**, with one
change: the queue is freed on cancel, so the label is synced into the item at each change
and at batch end rather than looked up at paint time; otherwise a stopped batch would paint
"ran" for commands never sent.

Not verified by the critic: whether `\x03` through `io->write` reaches bash as SIGINT on the
SSH backend and under ConPTY. The manual check in section 4 settles it on the local shell;
the integration case `ctrl_c_without_selection_interrupts` already proves it for SSH.
