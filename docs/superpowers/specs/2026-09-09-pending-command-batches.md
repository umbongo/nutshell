# Pending command batches: ask on while a card waits

**Status:** approved by Thomas 2026-09-09. Implemented in v1.1.7 (batch
model + list containers) and v1.1.8 (panel wiring, demo, integration).

Thomas: "when AI gives me commands to approve I have the option to deny all
or run the commands. This is good, however I can't ask another question
while that approval window is active. If I ask a question just before the
approval process I need the bot to respond. Once the bot has responded to
the most recent prompt I should be able to scroll back and then approve the
commands to run; however there could be instances where I don't approve or
deny the command, I just let the approval scroll upwards and move on to
something else in the CLI."

## Behaviour

1. **A pending card never blocks the input.** Sending is refused only while
   a reply is streaming or the dispatcher is sending commands (as today).
2. **A new reply does not settle earlier cards.** Each AI reply that yields
   commands creates its own *batch* with its own card: header, rows, Deny
   all, Run N selected. Any number of cards may be pending at once; they
   scroll with the transcript and stay interactive until acted on.
3. **Acting on a card** (Run selected / Deny all / single approve) settles
   that batch only. Run sends that batch's approved commands through the
   dispatcher (one at a time, at a prompt). If another card's Run is clicked
   while the dispatcher is busy, that batch is queued and runs next.
4. **The continue message** after a batch ran says which batch it was when
   newer exchanges have happened since: "The commands from my earlier
   request (`<first command>` …) have now been executed. Look at the updated
   terminal output and continue with that request." Otherwise unchanged.
5. **Unacted cards** stay pending for the life of the session. To bound
   memory, at most `CMD_BATCH_MAX` = 8 batches are pending per session; when
   a ninth arrives the oldest is settled as skipped with the status line
   `[earlier command batch skipped]`.
6. **Session switch** keeps every pending batch of the session it belongs
   to and re-creates its cards on switch back (replacing today's single
   `pending_cmds` snapshot). Blocked/held rows, auto-approve and Permit
   write behave per batch exactly as they do for a single card today.
7. `--ui-demo=batches` shows two pending cards (one held row in the second)
   so the gallery covers the layout; `ui_demo_states()` grows to ten.

## Model

- `ChatMsgItem.u.cmd.batch` (int): the batch id of a command item; 0 for
  demo/legacy rows. Ids come from the session's `CmdBatchSet.next_id`.
- `src/core/cmd_batch.{c,h}`: `CmdBatch { int id; ApprovalQueue q; }`
  allocated on the heap; `CmdBatchSet { CmdBatch *b[CMD_BATCH_MAX]; int
  count; int next_id; }` with `cmd_batch_set_init/free`, `cmd_batch_add(set,
  int *evicted_id)` (returns the new batch, evicting the oldest when full and
  reporting its id so the UI can settle its rows), `cmd_batch_find(set,
  id)`, `cmd_batch_remove(set, id)`, `cmd_batch_first_pending(set)` (oldest
  batch whose queue still needs the user), `cmd_batch_next_runnable(set)`
  (oldest batch with an APPROVED entry not yet sent). Pure, tested.
- Index of a command item inside its batch = its position among the
  unsettled command items with the same batch id, in list order; the batch
  queue is built in the same order, so `q.entries[i]` ↔ the i-th such item.
- `ai_build_continue_text(int newer_exchanges, const char *first_cmd, char
  *out, size_t cap)` in `src/core/ai_prompt.c`, tested.

## List view

- Pass 2 of `recalc_layout` builds one container per run of consecutive
  unsettled command items sharing a batch id: the first item absorbs the
  container height, the rest measure 0. Per-container scroll offset lives on
  the first item (`u.cmd.container_scroll`); `lv->cmd_scroll_y/cmd_count/
  cmd_total_h/cmd_visible_h` go away in favour of a small per-container
  geometry computed on demand (`cmd_container_at(lv, item)`).
- Paint, hit-test, hover and wheel resolve the container under the point.
  Card actions post `WM_COMMAND` with `wParam = MAKEWPARAM(id, 0)` and
  `lParam = batch id`; checkbox toggles stay local.
- `chat_listview_reset_cmd_expand(hwnd)` resets the newest container.

## Panel (ai_chat.c)

- `d->pending_approval` and `d->queued_cmds/queued_count/queued_next` are
  removed. The per-session `CmdBatchSet` replaces `d->approval_q` for
  decisions; `auto_approve`/`auto_approve_level`/`permit_write` still live
  where they do and are passed into `chat_approval_add` per batch.
- Reply completion: create a batch, add every command, create items tagged
  with the batch id, mark blocked/auto-approved as today; if the batch needs
  no user (auto-approved) start the dispatcher for it; otherwise leave the
  card pending. No `settle_all_commands` at reply start.
- Handlers take the batch id from `lParam`: APPROVE_SEL, CANCEL_ALL,
  APPROVE_ALL, APPROVE_BASE+idx (idx within the batch). `lParam == 0` means
  the oldest pending batch (used by the integration harness).
- Dispatcher: `dispatch_batch_id`; on finishing a batch, settle it, send
  the continue message (rule 4), then pick `cmd_batch_next_runnable`.
- Stop cancels the running batch only (remaining entries denied, settled).

## Tests

Core: cmd_batch set (add/find/remove/evict order/next_id), item↔batch index
helpers in chat_msg, continue text, ui_demo state count and content.
Integration: `ai_prompt_while_approval_pending` — auto-approve off; prompt
1 asks for `echo BATCH_A_<n>` (card pending); prompt 2 "Reply with exactly
the word PONG and no commands"; wait for the reply to finish (Send button
back to normal); post APPROVE_SEL with lParam 0; expect `BATCH_A_<n>` in the
terminal log. Existing AI cases keep passing.
