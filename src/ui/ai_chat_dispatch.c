/* The command dispatcher: batch resolution, and the prompt-gated tick that
 * sends approved commands to the active session one at a time, watches for
 * their completion and hands the output back to the model. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"
#include "ai_conv_guard.h"

#ifdef _WIN32
#include <stdio.h>

/* Resolve which batch a card-action WM_COMMAND targets: lParam is the
 * batch id the card posted (chat_listview.c tags every card action with
 * item->u.cmd.batch), or 0 to mean "the oldest batch still needing the
 * user" -- used by handlers that can't supply a specific id (the
 * integration harness; see docs/superpowers/specs/
 * 2026-09-09-pending-command-batches.md). NULL if there's no active
 * session or no such batch. */
CmdBatch *ai_chat_resolve_batch(AiChatData *d, LPARAM lParam)
{
    if (!d || !d->active_state) return NULL;
    int id = (int)lParam;
    return id ? cmd_batch_find(&d->active_state->batches, id)
              : cmd_batch_first_pending(&d->active_state->batches);
}

AiConversation *ai_chat_owner_conv(AiChatData *d, AiSessionState *owner)
{
    return (owner == d->active_state) ? &d->conv : &owner->conv;
}

int ai_chat_owner_add_note(AiChatData *d, AiSessionState *owner,
                           const char *text, int keep_free)
{
    if (!d || !text) return -1;
    AiConversation *conv = owner ? ai_chat_owner_conv(d, owner) : &d->conv;
    AiNoteQueue *q = owner ? &owner->notes : NULL;
    const int defer = owner &&
        ai_stream_table_find_owner(&d->streams, owner) != NULL;
    const int rc = ai_conv_add_user_note(conv, q, defer, keep_free, text);
    if (rc < 0)   /* the note's text is never logged */
        DLOG(DLOG_AI, DLOG_WARN, 0, "note.door_failed",
             "rc=%d keep_free=%d defer=%d displayed=%d", rc, keep_free,
             defer, (!owner || owner == d->active_state) ? 1 : 0);
    if (rc == 2 && (!owner || owner == d->active_state)) {
        /* Nothing was stored or queued: say so, so no note is lost silently
         * (spec decisions 19 and 24). */
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
            keep_free > 0
                ? "[conversation full: the command results were not sent; "
                  "start a New Chat]"
                : "[conversation full: a Nutshell note was not stored; "
                  "start a New Chat]");
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
    }
    return rc;
}

/* Record a command batch's rows in the owner's run log. A tool batch, an
 * empty queue or an allocation failure records nothing (the log refuses). */
static void batch_record(AiChatData *d, AiSessionState *owner,
                         const CmdBatch *batch, int batch_ending)
{
    if (!d || !owner || !batch || cmd_batch_is_tool(batch)) return;
    /* The hash is the one the batch took from its reply when it was created
     * (CmdBatch.reply_hash), not read from a conversation now: a tab switch
     * has already moved the working copy away, and Retry may have replaced
     * the reply. */
    (void)cmd_run_log_record(&owner->run_log, batch, batch->reply_hash,
                             batch_ending);
}

void ai_chat_batch_remove_recorded(AiChatData *d, AiSessionState *owner,
                                   CmdBatch *batch, int batch_ending)
{
    if (!d || !owner || !batch) return;
    const int id = batch->id;   /* batch is freed by the remove below */
    batch_record(d, owner, batch, batch_ending);
    cmd_batch_remove(&owner->batches, id);
}

void ai_chat_batch_record_oldest_if_full(AiChatData *d, AiSessionState *owner)
{
    if (!d || !owner || owner->batches.count < CMD_BATCH_MAX) return;
    /* cmd_batch_add() evicts b[0] and frees it. */
    CmdBatch *oldest = owner->batches.b[0];
    batch_record(d, owner, oldest, 1);
    /* The displayed owner's live rows end as the record says: an approved
     * row never sent reads "not run", not "queued", and a running one
     * "unfinished". The caller settles the rows after the add. */
    if (owner == d->active_state && !cmd_batch_is_tool(oldest))
        chat_msg_batch_sync_run(&d->msg_list, oldest->id, &oldest->q, 1);
}

void ai_chat_batch_settle_rows(AiChatData *d, CmdBatch *batch)
{
    if (!d || !batch) return;
    batch->settled = 1;
    chat_msg_batch_settle(&d->msg_list, batch->id);
}

/* Once every entry in a batch has been decided (chat_approval_all_decided
 * -- PENDING is the only thing that blocks this; BLOCKED counts as
 * decided), render its command items inline (chat_msg_batch_settle) and,
 * if nothing in it is left to run, remove the batch itself. A batch with
 * an approved-but-not-yet-sent entry stays in the set -- ai_chat_dispatch_tick()
 * removes it once execution actually finishes, since its ApprovalQueue is
 * still needed to drive that. Always follows up with a re-layout
 * (chat_listview_invalidate), since WM_PAINT does not recalc_layout on
 * its own. */
void ai_chat_settle_batch_if_done(AiChatData *d, CmdBatch *batch)
{
    if (!d || !batch) return;
    if (!chat_approval_all_decided(&batch->q)) return;

    /* Still runnable: a command approved but not sent, or sent and still
     * running (EXECUTING) -- denying the last pending row while another row
     * runs must not remove the batch under the dispatcher. */
    int has_runnable = !cmd_batch_finished(batch);

    /* A tool batch is never removed with its call unanswered (spec decision
     * 20: every removal answers the slot). Run and Deny have already
     * answered by now, so this is a read-back that changes nothing; it only
     * acts for a caller that forgot. chat_approval_next_approved() skips a
     * tool entry, so has_runnable is 0 and no dispatch or continue follows. */
    if (cmd_batch_is_tool(batch))
        (void)ai_chat_tool_slot_answer(d, batch, TOOL_DECISION_NOT_APPROVED);

    /* A run_command batch with nothing left to run ends through the one
     * finish (it settles the rows itself); a Deny all starts no request. */
    if (!has_runnable && d->active_state && batch->runcmd) {
        ai_chat_runcmd_finish(d, d->active_state, batch,
                              runcmd_path_plan(RUNCMD_PATH_DECIDED).cause);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
        return;
    }

    ai_chat_batch_settle_rows(d, batch);
    if (!has_runnable && d->active_state)
        ai_chat_batch_remove_recorded(d, d->active_state, batch, 1);
    if (d->hChatList) chat_listview_invalidate(d->hChatList);
}

/* ---- run_command: answering a reply that has no batch (spec decisions 5, 7, 25) ---- */

/* A status row where the owner is displayed; a background owner has no list
 * to draw into: the row is in its run log (runcmd_store_results), and the
 * rebuild draws it. */
static void owner_status_row(AiChatData *d, const AiSessionState *owner,
                             const char *text)
{
    if (!d || owner != d->active_state) return;
    chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, text);
    if (d->hChatList) chat_listview_invalidate(d->hChatList);
}

/* Fit `res` (n results, heap) and store them against the owner's open tool
 * turn; age, flush the owner's notes (`keep_free` slots left free: 0 where
 * nothing relaunches, runcmd_cause_keep_free otherwise) and say what happened: the
 * row for the store's return and `cause_row` (RUNCMD_ROW_NONE: none) when the
 * results are stored. Each row is shown when the owner is displayed and, with
 * the turn kept, recorded in the owner's run log keyed to that reply (R64), so
 * a rebuild or a background owner's later display draws it under its reply.
 * The caller frees `res` and each content. Returns the store's return (0, 1,
 * -1, -2). */
static int runcmd_store_results(AiChatData *d, AiSessionState *owner,
                                AiToolResult *res, int n,
                                RunCmdStatusRow cause_row, int cause_n,
                                int keep_free)
{
    AiConversation *conv = ai_chat_owner_conv(d, owner);
    const AiMessage *turn = ai_conv_open_tool_turn(conv);
    if (!turn) return -1;
    /* The key of the record, taken now: a negative return frees the turn. */
    const int reply_msg = (int)(turn - conv->messages);
    const uint32_t reply_hash = cmd_run_log_reply_hash(
        ai_msg_content(turn), turn->content_len, turn->tool_calls,
        turn->n_tool_calls);
    /* The budget uses the panel's current model (the one the next request
     * goes to), never a background owner's conv.model (Part A review item 4). */
    const size_t snap = ai_conv_has_snapshot(conv) ? conv->snapshot_len : 0;
    runcmd_fit(res, n,
               runcmd_request_estimate(conv, d->provider, RUNCMD_TOOLS_ESTIMATE, snap),
               runcmd_budget(AI_BODY_MAX, d->context_limit));
    /* The stored turn's calls, in result order: without them no part is
     * recognised as run_command (framed, aged, exempt from the cap). A negative
     * return frees the turn, so nothing below reads it. */
    const int rc = agentic_add_tool_results(conv, turn->tool_calls, res, n);
    if (rc >= 0) runcmd_age_results(conv);
    (void)ai_conv_flush_notes(conv, &owner->notes, keep_free);

    char rows[CMD_RUN_STATUS_ROWS][CMD_RUN_STATUS_MAX];
    const char *texts[CMD_RUN_STATUS_ROWS];
    int nt = 0;
    RunCmdStatusRow kinds[2];
    int ks = 0;
    if (rc == 1) kinds[ks++] = RUNCMD_ROW_REDUCED;
    else if (rc == -1) kinds[ks++] = RUNCMD_ROW_FULL;
    else if (rc < 0) kinds[ks++] = RUNCMD_ROW_STORE_FAILED;
    if (rc >= 0 && cause_row != RUNCMD_ROW_NONE) kinds[ks++] = cause_row;
    for (int k = 0; k < ks; k++) {
        if (runcmd_status_text(kinds[k], kinds[k] == RUNCMD_ROW_FULL ? n : cause_n,
                               rows[nt], sizeof(rows[nt])) == 0)
            continue;
        owner_status_row(d, owner, rows[nt]);
        texts[nt] = rows[nt];
        nt++;
    }
    /* With the turn gone (a negative return) there is no reply to key to. */
    if (rc >= 0 && nt > 0)
        (void)cmd_run_log_record_status(&owner->run_log, reply_msg, reply_hash,
                                        texts, nt);
    return rc;
}

static void free_results(AiToolResult *res, int n)
{
    if (!res) return;
    for (int i = 0; i < n; i++) free(res[i].content);
    free(res);
}

void ai_chat_runcmd_answer_reply(AiChatData *d, AiSessionState *owner,
                                 RunCmdReply *r, RunCmdCause cause)
{
    if (!r) return;
    if (!d || !owner) { runcmd_reply_free(r); return; }
    const int n = r->n_calls;
    /* 16 + 48 results is far too large for the stack: on the heap. */
    AiToolResult *res = (AiToolResult *)calloc((size_t)(n > 0 ? n : 1), sizeof(*res));
    if (!res || runcmd_build_results(r, NULL, cause, res) != n) {
        free_results(res, n);
        runcmd_reply_free(r);
        /* No answer could be built: fall back to the one that needs no plan,
         * else drop the turn so no call stays unanswered (spec decision 25). */
        ai_chat_runcmd_answer_oom(d, owner);
        return;
    }
    RunCmdStatusRow row = RUNCMD_ROW_NONE;
    if (cause == RUNCMD_CAUSE_SESSION_GONE) row = RUNCMD_ROW_SESSION_GONE;
    else if (cause == RUNCMD_CAUSE_SET_FULL) row = RUNCMD_ROW_SET_FULL;
    else if (cause == RUNCMD_CAUSE_NO_SLOT) row = RUNCMD_ROW_NO_SLOT;
    /* "N refused" counts every call of the reply: none of them could run. */
    (void)runcmd_store_results(d, owner, res, n, row, n, 0);
    free_results(res, n);
    runcmd_reply_free(r);
}

void ai_chat_runcmd_answer_oom(AiChatData *d, AiSessionState *owner)
{
    if (!d || !owner) return;
    AiConversation *conv = ai_chat_owner_conv(d, owner);
    const AiMessage *turn = ai_conv_open_tool_turn(conv);
    if (!turn) return;
    const int n = turn->n_tool_calls;
    AiToolResult *res = (AiToolResult *)calloc((size_t)n, sizeof(*res));
    if (!res || runcmd_oom_results(turn->tool_calls, n, res) != n) {
        free(res);
        (void)ai_conv_drop_open_tool_turn(conv);
        char row[CMD_RUN_STATUS_MAX];
        if (runcmd_status_text(RUNCMD_ROW_STORE_FAILED, n, row, sizeof(row)) > 0)
            owner_status_row(d, owner, row);
        return;
    }
    (void)runcmd_store_results(d, owner, res, n, RUNCMD_ROW_PLANNER_OOM, n, 0);
    free_results(res, n);
}

/* ---- run_command: the one end of a batch (spec decisions 8, 9 and 10) ---- */

static const char RUNCMD_NO_ROOM_ROW[] =
    "[conversation full: the command results were not sent; start a New Chat]";

/* Read back the output of the call whose entry is `entry` (once: the capture's
 * own flag makes every later call a no-op). UI thread only: it reads the
 * Terminal. */
static void runcmd_take_entry(AiChatData *d, CmdBatch *batch, int entry)
{
    if (!d || !batch || !batch->runcmd || !d->active_term) return;
    RunCmdCall *call = runcmd_call_for_entry(batch->runcmd, entry);
    if (!call) return;
    (void)runcmd_capture_take(&call->cap, d->active_term, call->command,
                              runcmd_cap_bytes(batch->runcmd->n_slot));
}

void ai_chat_runcmd_finish(AiChatData *d, AiSessionState *owner, CmdBatch *batch,
                           RunCmdCause cause)
{
    if (!d || !owner || !batch) return;
    const int displayed = (owner == d->active_state);
    const int batch_id = batch->id;
    /* Read before the removal below frees the batch. */
    const int user_acted = batch->user_acted;
    const RunCmdReply *r = batch->runcmd;
    int stored = 0;
    int room = 0;

    if (r && runcmd_cause_captures(cause) && displayed) {
        /* The terminal may be gone (SESSION_ENDED, PANEL_CLOSED): no capture
         * then; the call says "ran; output not captured". Only a displayed
         * owner's terminal is d->active_term. */
        for (int i = 0; i < batch->q.count; i++)
            if (batch->q.entries[i].status == APPROVE_EXECUTING)
                runcmd_take_entry(d, batch, i);
    }

    if (r && runcmd_cause_appends(cause)) {
        AiConversation *conv = ai_chat_owner_conv(d, owner);
        /* R45: the owner's queue, read here on the UI thread. Taken before the
         * append, as the budget below is. */
        room = agentic_room_for_tool_round(conv, &owner->notes);
        const int n = r->n_calls;
        /* 64 results are about 40 KB: on the heap. */
        AiToolResult *res = (AiToolResult *)calloc((size_t)(n > 0 ? n : 1),
                                                   sizeof(*res));
        if (res && runcmd_build_results(r, &batch->q, cause, res) == n) {
            const int rc = runcmd_store_results(d, owner, res, n,
                                                RUNCMD_ROW_NONE, 0,
                                                runcmd_cause_keep_free(cause));
            stored = (rc >= 0);
        } else {
            /* No answer could be built: the one that needs no plan, else the
             * turn is dropped (spec decision 25). Nothing relaunches from it. */
            ai_chat_runcmd_answer_oom(d, owner);
        }
        free_results(res, n);
    }

    if (displayed) {
        chat_msg_batch_sync_run(&d->msg_list, batch_id, &batch->q, 1);
        ai_chat_batch_settle_rows(d, batch);
    }
    ai_chat_batch_remove_recorded(d, owner, batch, 1);   /* frees batch */

    if (runcmd_finish_relaunches(cause, displayed, owner->busy, stored, room)) {
        ai_chat_send_tool_results(d, !user_acted);
    } else {
        if (runcmd_cause_relaunches(cause) && displayed && stored && !room)
            chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, RUNCMD_NO_ROOM_ROW);
        /* No request follows: the activity line must not keep reading
         * "Executing" for a command that is no longer running. */
        if (displayed && d->activity.phase == ACTIVITY_EXECUTING) {
            chat_activity_reset(&d->activity);
            ai_chat_update_context_bar(d);
        }
        /* Nothing launched, so the stream that would have ended in
         * ai_chat_maybe_start_next_batch() never runs: an approved batch
         * waiting behind this one must not be stranded. Only a cause that
         * leaves the session in place asks (a teardown cause never does: the
         * session is being left or freed). Its own guards make it a no-op
         * while a dispatch runs or a stream is busy; a background owner is
         * never started here. */
        if (displayed && runcmd_cause_starts_next(cause))
            ai_chat_maybe_start_next_batch(d);
    }
    if (displayed && d->hChatList) chat_listview_invalidate(d->hChatList);
}

/* ---- Tool cards (trust boundary spec decisions 14, 15, 20, 22) ---- */

/* Answer the decision slot of tool call (stream_id, gen). NONE when the
 * stream is gone. */
static ToolDecision tool_slot_answer_ids(AiChatData *d, unsigned stream_id,
                                         unsigned gen, ToolDecision answer)
{
    if (!d || stream_id == 0) return TOOL_DECISION_NONE;
    AiStream *s = ai_stream_table_find(&d->streams, stream_id, NULL);
    if (!s) return TOOL_DECISION_NONE;
    return tool_decision_answer(&s->decision, gen, answer);
}

ToolDecision ai_chat_tool_slot_answer(AiChatData *d, const CmdBatch *batch,
                                      ToolDecision answer)
{
    if (!batch || !cmd_batch_is_tool(batch)) return TOOL_DECISION_NONE;
    return tool_slot_answer_ids(d, batch->tool_stream_id, batch->tool_gen,
                                answer);
}

/* Label every row of displayed batch `batch_id` from the slot's outcome. */
static void tool_rows_set_outcome(AiChatData *d, int batch_id, ToolDecision res)
{
    for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
        if (it->type != CHAT_ITEM_COMMAND || !it->u.cmd.tool ||
            it->u.cmd.batch != batch_id)
            continue;
        chat_msg_set_tool_outcome(it, (int)res);
        it->dirty = 1;
    }
}

/* The displayed session has stopped waiting on a card: the activity line goes
 * back to Processing, but only from Waiting, so a Stop or error that already
 * moved it on is left alone. */
static void tool_wait_over(AiChatData *d)
{
    if (d->activity.phase != ACTIVITY_WAITING) return;
    chat_activity_set_phase(&d->activity, ACTIVITY_PROCESSING,
                            (float)GetTickCount() / 1000.0f);
}

/* Run (APPROVED) or Deny (NOT_APPROVED) on a tool card. Spec decision 20,
 * "Order of operations": answer the slot FIRST and read the CAS result; the
 * rows are labelled from that result (a click after the thread expired or
 * cancelled the call says so, never "approved"); only then is the entry
 * marked and the batch settled and removed. No dispatch and no continue
 * message: chat_approval_next_approved() skips a tool entry. */
void ai_chat_tool_card_answer(AiChatData *d, CmdBatch *batch, ToolDecision answer)
{
    if (!d || !batch || !d->active_state) return;
    const int run = (answer == TOOL_DECISION_APPROVED);
    const int batch_id = batch->id;

    const ToolDecision res = ai_chat_tool_slot_answer(d, batch, answer);
    tool_rows_set_outcome(d, batch_id, res);

    for (int i = 0; i < batch->q.count; i++) {
        if (!batch->q.entries[i].tool) continue;
        if (run) chat_approval_approve(&batch->q, i);
        else     chat_approval_deny(&batch->q, i);
    }
    for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
        if (it->type == CHAT_ITEM_COMMAND && it->u.cmd.tool &&
            !it->u.cmd.settled && it->u.cmd.batch == batch_id)
            it->u.cmd.approved = run ? 1 : 0;
    }

    /* A Run the thread actually took is an attended round. */
    if (run && res == TOOL_DECISION_APPROVED)
        ai_tool_rounds_reset(d->active_state);

    ai_chat_settle_batch_if_done(d, batch);   /* frees batch */

    tool_wait_over(d);
    ai_chat_maybe_start_next_batch(d);        /* no-op while the stream is busy */
}

/* WM_AI_TOOL_DECIDE (spec decision 15). Never waits; frees `m` on every
 * path. */
void ai_chat_on_tool_decide(AiChatData *d, AiToolDecideMsg *m)
{
    if (!m) return;
    m->row[sizeof(m->row) - 1] = '\0';
    m->detail[sizeof(m->detail) - 1] = '\0';

    void *owner_ptr = NULL;
    AiStream *stream = d ? ai_stream_table_find(&d->streams, m->stream_id,
                                                &owner_ptr) : NULL;
    if (!stream) {
        /* Stale: stopped, retried, tab closed or panel rebuilt. The thread
         * sees its own abort flag. */
        free(m);
        return;
    }
    AiSessionState *owner = (AiSessionState *)owner_ptr;
    if (!owner) {
        (void)tool_decision_answer(&stream->decision, m->gen,
                                   TOOL_DECISION_NOT_APPROVED);
        free(m);
        return;
    }
    const int displayed = (owner == d->active_state);

    if (m->kind == AI_TOOL_DECIDE_EXPIRED) {
        /* The thread gave up on the card: its row reads "not run" and the
         * batch goes. */
        CmdBatch *b = NULL;
        for (int bi = 0; bi < owner->batches.count; bi++) {
            CmdBatch *c = owner->batches.b[bi];
            if (c->tool_stream_id == m->stream_id && c->tool_gen == m->gen) {
                b = c;
                break;
            }
        }
        if (b) {
            if (displayed) tool_rows_set_outcome(d, b->id, TOOL_DECISION_EXPIRED);
            for (int i = 0; i < b->q.count; i++)
                if (b->q.entries[i].tool) chat_approval_deny(&b->q, i);
            if (displayed) {
                ai_chat_settle_batch_if_done(d, b);   /* frees b */
            } else {
                cmd_batch_remove(&owner->batches, b->id);
            }
        }
        if (displayed) {
            tool_wait_over(d);
            ai_chat_maybe_start_next_batch(d);
        } else if (owner->activity_phase == (int)ACTIVITY_WAITING) {
            owner->activity_phase = (int)ACTIVITY_PROCESSING;
        }
        free(m);
        return;
    }

    if (m->kind != AI_TOOL_DECIDE_ASK) {
        (void)tool_decision_answer(&stream->decision, m->gen,
                                   TOOL_DECISION_NOT_APPROVED);
        free(m);
        return;
    }

    /* The displayed session's live policy, read now, as WM_AI_RESPONSE does
     * for a new batch; a background session is always attended. */
    const CmdPolicy p = displayed ? d->approval_q.policy : owner->policy;
    /* The round key is recorded for every owner, a background one included
     * (it is attended whatever the count says): a later Run click on that
     * card resets the count, and a second call of the same round must still
     * be judged on the count this round started with (decision 22). */
    const int capped = ai_tool_rounds_attended(owner, m->stream_id,
                                               m->loop_iter,
                                               stream->tool_auto_rounds);
    const int attended = !displayed || capped;

    if (tool_gate_decide(m->safety, p, attended) == TOOL_GATE_RUN) {
        (void)tool_decision_answer(&stream->decision, m->gen,
                                   TOOL_DECISION_RUN);
        /* SAFE tools never count. Only a displayed call can run here (a
         * background owner is always attended); the round was recorded by
         * ai_tool_rounds_attended() above. */
        if (displayed && m->safety != TOOL_SAFE)
            ai_tool_rounds_ran_unattended(owner, m->stream_id, m->loop_iter);
        free(m);
        return;
    }

    /* ASK. When only the round cap forced it, say so once. */
    if (displayed && tool_gate_decide(m->safety, p, 0) == TOOL_GATE_RUN &&
        ai_tool_rounds_take_pause_row(owner)) {
        char pause_row[96];
        ai_tool_pause_row(pause_row, sizeof(pause_row), stream->tool_auto_rounds);
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, pause_row);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
    }

    /* A tool batch never evicts another card. */
    if (cmd_batch_full(&owner->batches)) {
        (void)tool_decision_answer(&stream->decision, m->gen,
                                   TOOL_DECISION_TOO_MANY);
        free(m);
        return;
    }
    CmdBatch *b = cmd_batch_add(&owner->batches, NULL, NULL);
    if (!b) {
        (void)tool_decision_answer(&stream->decision, m->gen,
                                   TOOL_DECISION_NOT_APPROVED);
        free(m);
        return;
    }
    b->tool_stream_id = m->stream_id;
    b->tool_gen = m->gen;
    if (chat_approval_add_tool(&b->q, m->row, m->detail,
                               (CmdSafetyLevel)m->safety) < 0) {
        /* Answer the slot first, then drop the batch (decision 20). */
        (void)tool_decision_answer(&stream->decision, m->gen,
                                   TOOL_DECISION_NOT_APPROVED);
        cmd_batch_remove(&owner->batches, b->id);
        free(m);
        return;
    }

    if (displayed) {
        ai_chat_append_batch_command_items(&d->msg_list, b);
        chat_activity_set_phase(&d->activity, ACTIVITY_WAITING,
                                (float)GetTickCount() / 1000.0f);
        if (d->hChatList) chat_listview_reset_cmd_expand(d->hChatList);
        ai_chat_relayout(d);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
    } else {
        /* Restored by the session switch (activity_phase). */
        owner->activity_phase = (int)ACTIVITY_WAITING;
    }
    free(m);
}

/* The sweep (spec decision 20: every batch removal answers). Answer every tool
 * card of `state` CANCELLED, oldest first, then remove it: the slot FIRST, so
 * the stream thread's wait ends before the card it was waiting on is gone.
 * The displayed session's rows are labelled from the CAS result (a card the
 * thread had already expired or taken says so; a stream that is already gone
 * reads "cancelled") and settled; a background session has no rows. A command
 * batch is never touched. The batch array shifts down on every remove, so the
 * index only advances past a batch that stays. */
void ai_chat_cancel_tool_batches(AiChatData *d, AiSessionState *state)
{
    if (!d || !state) return;
    const int displayed = (state == d->active_state);
    int swept = 0;

    int i = 0;
    while (i < state->batches.count) {
        CmdBatch *b = state->batches.b[i];
        if (!cmd_batch_is_tool(b)) { i++; continue; }

        const int batch_id = b->id;
        ToolDecision res = ai_chat_tool_slot_answer(d, b, TOOL_DECISION_CANCELLED);
        if (res == TOOL_DECISION_NONE) res = TOOL_DECISION_CANCELLED;
        if (displayed) {
            tool_rows_set_outcome(d, batch_id, res);
            ai_chat_batch_settle_rows(d, b);
        }
        cmd_batch_remove(&state->batches, batch_id);   /* frees b */
        swept++;
    }
    if (swept == 0) return;

    /* No card is left to wait on. A caller that goes on to stop or restart
     * the stream sets the phase itself (idle or processing); this keeps the
     * line from reading "Waiting for your decision" for one that does not. */
    if (displayed) {
        tool_wait_over(d);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
    } else if (state->activity_phase == (int)ACTIVITY_WAITING) {
        state->activity_phase = (int)ACTIVITY_PROCESSING;
    }
}

/* line_clear_mode is the caller's (ai_chat_dispatch_tick's) already-decided
 * DispatchLineClearMode -- computed once per tick from the same
 * term_cursor_row_is_windows_prompt() reading ai_chat_dispatch_tick used for its
 * own no-prefix safety gate, and handed in rather than recomputed here,
 * so the prefix actually written can never disagree with the check that
 * decided it was safe to send at all (dispatch_line_clear.h). */
static void execute_command(AiChatData *d, const char *cmd,
                             DispatchLineClearMode line_clear_mode,
                             RunCmdCall *call)
{
    if (!d || !cmd || !cmd[0]) return;
    SessionIo *io = d->active_io;
    if (!io || !io->write) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                        "[error: no active session]");
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
        /* A run_command call is answered as not run, not "ran". */
        if (call) call->unsent = RUNCMD_UNSENT_NO_SESSION;
        return;
    }
    /* Defense in depth: even though ai_extract_commands() and
     * chat_approval_add() already refuse a command containing a raw
     * control byte, a UTF-8-encoded C1 control, or a bidi override/isolate
     * character, refuse to write one to the channel here too -- this is
     * the last point before the bytes reach the remote shell. Shared with
     * paste_filter_controls() -- see text_has_unsafe_command_char() in
     * src/core/paste_filter.h. */
    if (text_has_unsafe_command_char(cmd)) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
            "[command rejected: control characters inside an EXEC block]");
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
        if (call) call->unsent = RUNCMD_UNSENT_UNSAFE;
        return;
    }

    /* A run_command call records where its output starts, just before this
     * command's first write (the prefix below, or the command itself). It
     * writes nothing to the terminal. */
    if (call) runcmd_capture_start(&call->cap, d->active_term);

    /* Clear any existing text on the line before pasting -- but only for a
     * shell known to treat Ctrl+E (end of line) + Ctrl+U (kill to start of
     * line) as the readline line-editing idiom it is. PSReadLine
     * (PowerShell) and cmd.exe both take those two bytes as literal input
     * instead, which used to land "^E^U" in front of every dispatched
     * command (see dispatch_line_clear.h). The caller (ai_chat_dispatch_tick) has
     * already confirmed the line is otherwise empty and quiet since the
     * user's last keystroke when no prefix is about to be sent -- see
     * term_at_unambiguous_prompt() and dispatch_keystroke_too_recent(). */
    if (line_clear_mode == DISPATCH_LINE_CLEAR_READLINE) {
        io->write(io->ctx, "\x05\x15", 2);
    }

    /* Send command + CR to the session (CR = Enter key, same as WM_CHAR) */
    io->write(io->ctx, cmd, (size_t)strlen(cmd));
    io->write(io->ctx, "\r", 1);

}

/* Start the command dispatcher on one specific batch: sends every command
 * in its queue with status APPROVE_APPROVED, in order, one at a time,
 * only once active_term is sitting at a shell prompt (see
 * term_at_prompt()). Does not send anything itself -- the TIMER_CMD_QUEUE
 * tick (ai_chat_dispatch_tick(), driven from AiChatWndProc's WM_TIMER) does the
 * actual sending. Safe to call repeatedly (e.g. once per single-command
 * Approve click, or from a different batch's Run while this one is
 * already running): a no-op while any dispatch is active or the batch
 * has nothing approved yet -- another card's Run while the dispatcher is
 * busy just leaves its commands APPROVED, and cmd_batch_next_runnable()
 * picks the batch up once the current one finishes (see
 * ai_chat_maybe_start_next_batch()). */
void ai_chat_dispatch_start(AiChatData *d, int batch_id)
{
    /* Never while the session's stream is busy (spec decision 21): no batch
     * finishes during a stream, so no continue message is ever produced
     * while busy. An approved row stays APPROVED, and
     * ai_chat_maybe_start_next_batch() picks it up when the stream ends. */
    if (!d || !d->active_state || d->dispatch_active || ACTIVE_BUSY(d)) return;

    CmdBatch *batch = cmd_batch_find(&d->active_state->batches, batch_id);
    if (!batch || chat_approval_next_approved(&batch->q) < 0) return;

    DWORD now = GetTickCount();

    d->dispatch_active = 1;
    d->dispatch_batch_id = batch->id;
    d->dispatch_seq = d->active_term ? d->active_term->write_seq : 0;
    d->dispatch_await_echo = 0;
    d->dispatch_last_change_tick = now;
    d->dispatch_last_idx = -1;
    d->dispatch_sent_count = 0;
    d->dispatch_stall_reported = 0;
    d->dispatch_ambiguous_since_tick = 0;
    d->dispatch_ambiguous_reported = 0;
    d->dispatch_start_tick = now;
    d->dispatch_last_progress_tick = now;
    d->dispatch_last_diag_log_tick = 0;

    {
        int windows_prompt = term_cursor_row_is_windows_prompt(d->active_term);
        char line[176];
        snprintf(line, sizeof(line),
            "dispatch_start: batch=%d kind=%s shell=%s mode=%s",
            batch->id,
            session_kind_name(active_session_kind(d)),
            active_shell_name(d) ? active_shell_name(d) : "(none)",
            dispatch_line_clear_mode(active_session_kind(d), active_shell_name(d),
                                      windows_prompt)
                == DISPATCH_LINE_CLEAR_READLINE ? "READLINE" : "NONE");
        DLOG(DLOG_AI, DLOG_INFO, 0, "dispatch.start", "msg=%s", line);
    }

    SetTimer(d->hwnd, TIMER_CMD_QUEUE, CMD_QUEUE_POLL_MS, NULL);

    /* Send button shows Stop while the dispatcher is running, same as
     * while an AI stream is in flight. */
    if (d->hSendBtn) {
        SetWindowTextW(d->hSendBtn, L"\x25A0");
        InvalidateRect(d->hSendBtn, NULL, TRUE);
    }
}

/* If nothing is currently dispatching or streaming, start the oldest
 * batch that has an approved-but-unsent command (rule 3: a batch whose
 * Run was clicked while another was dispatching "is queued and runs
 * next"). Called once a stream finishes (the reply itself, or a batch's
 * continue message) so a queued batch picks up right away instead of
 * waiting for another user action. */
void ai_chat_maybe_start_next_batch(AiChatData *d)
{
    if (!d || !d->active_state || d->dispatch_active || ACTIVE_BUSY(d)) return;
    CmdBatch *nb = cmd_batch_next_runnable(&d->active_state->batches);
    if (nb) ai_chat_dispatch_start(d, nb->id);
}

/* Stop the dispatcher: kill the timer, mark the running batch's cards with
 * what actually happened to them, settle and remove just that batch's card,
 * and restore the Send button. Other pending batches are untouched -- "Stop
 * cancels the running batch only" (rule 3). When status_msg is non-NULL it
 * is appended as a status line (the Stop button path); a session switch or
 * panel close cancels silently (NULL). A no-op when the dispatcher isn't
 * running.
 *
 * Nothing is force-denied any more (2026-09-23 dispatch-states spec,
 * section 2): a command the dispatcher never sent stays APPROVE_APPROVED in
 * the queue and chat_msg_batch_sync_run(..., batch_ending = 1) paints it
 * "not run", which is the honest label -- the user denied nothing. The
 * batch is removed from the set on the next line, so a later
 * ai_chat_dispatch_start() cannot resurrect it either way.
 *
 * `notify` marks the two paths the user can see the end of -- the Stop
 * button and the session ending mid-batch. Those additionally (a) send
 * Ctrl+C when the terminal is sitting at a continuation prompt, so the
 * shell discards the half-finished line and gives the user a prompt back,
 * and (b) tell the model what ran and what did not, instead of leaving it
 * to assume every command it proposed was executed (section 3).
 *
 * A run_command batch never reaches the [EXEC] continue text: it ends through
 * ai_chat_runcmd_finish() with `cause` (the caller's runcmd_path_plan cause),
 * after the status row and the Send button, so a relaunch (the timeout's) sees
 * the panel at rest. The finish answers the model with the results, not with
 * notify's text; notify still sends the Ctrl+C. */
void ai_chat_dispatch_cancel(AiChatData *d, const char *status_msg, int notify,
                             RunCmdCause cause)
{
    if (!d || !d->dispatch_active) return;

    KillTimer(d->hwnd, TIMER_CMD_QUEUE);
    d->dispatch_active = 0;
    d->dispatch_stall_reported = 0;
    d->dispatch_ambiguous_since_tick = 0;
    d->dispatch_ambiguous_reported = 0;

    char continue_text[4096] = "";
    int have_continue = 0;
    int unattended = 1;
    CmdBatch *runcmd_batch = NULL;   /* ended by the finish, below */

    CmdBatch *batch = d->active_state
        ? cmd_batch_find(&d->active_state->batches, d->dispatch_batch_id)
        : NULL;
    if (batch && batch->runcmd) {
        runcmd_batch = batch;
    } else if (batch) {
        /* Read before ai_chat_batch_remove_recorded() below frees the batch. */
        unattended = !batch->user_acted;
        chat_msg_batch_sync_run(&d->msg_list, batch->id, &batch->q, 1);

        if (notify) {
            int newer_exchanges = (d->conv.msg_count > batch->conv_mark) ? 1 : 0;
            char first_cmd[1024] = "";
            if (batch->q.count > 0)
                snprintf(first_cmd, sizeof(first_cmd), "%s",
                         batch->q.entries[0].command);
            have_continue = ai_build_continue_text(newer_exchanges, first_cmd,
                                                   &batch->q, continue_text,
                                                   sizeof(continue_text)) > 0;
            /* The per-command outcomes list can overflow the buffer on a
             * batch with many/long commands -- fall back to the opening
             * sentence alone so a continue message is still sent and
             * ai_chat_maybe_start_next_batch() still gets its turn once it
             * completes, rather than silently dropping the continue. */
            if (!have_continue)
                have_continue = ai_build_continue_text(newer_exchanges,
                                                       first_cmd, NULL,
                                                       continue_text,
                                                       sizeof(continue_text)) > 0;
        }

        ai_chat_batch_settle_rows(d, batch);
        ai_chat_batch_remove_recorded(d, d->active_state, batch, 1);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
    }
    d->dispatch_batch_id = 0;

    /* Stop at a continuation prompt: the shell is holding an unfinished
     * line (an unclosed quote, a half-typed here-doc). Ctrl+C throws it
     * away so the next thing the user types isn't swallowed by it. At a
     * primary prompt nothing is sent, as before. */
    if (notify && d->active_io && d->active_io->write && d->active_term &&
        term_at_continuation_prompt(d->active_term))
        d->active_io->write(d->active_io->ctx, "\x03", 1);

    if (status_msg) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, status_msg);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
    }

    if (d->hSendBtn) {
        SetWindowText(d->hSendBtn, ">");
        InvalidateRect(d->hSendBtn, NULL, TRUE);
    }

    if (runcmd_batch)
        ai_chat_runcmd_finish(d, d->active_state, runcmd_batch, cause);
    else if (have_continue)
        ai_chat_send_continue_message(d, continue_text, unattended);
}

/* Diagnostic only, for dispatch_log_stall_if_due() below -- never used by
 * term_at_prompt()/term_at_unambiguous_prompt() etc. themselves. Mirrors
 * term_cursor_row_text()'s guards (src/term/buffer.c, private to that
 * file) in the same order, so buf holding text vs. a reason lines up
 * exactly with whether that function would have produced a row -- but
 * explains a failure instead of returning 0 silently, and represents the
 * row's raw content instead of that function's lossy ASCII-only copy
 * (every non-ASCII codepoint becomes '?' there, and a real space cell is
 * indistinguishable from a blank one). buf always ends up NUL-terminated
 * within buf_size: on success, the row's text up to the cursor with
 * control bytes and codepoints above ASCII written as \xNN and a blank
 * cell as a literal space (so trailing spaces stay visible); on failure,
 * the reason, including -- for the one case worth pinpointing -- the
 * column and codepoint of the non-blank cell found after the cursor.
 * Returns 1/0 matching term_cursor_row_text()'s own return. */
static int describe_cursor_row(const Terminal *term, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    buf[0] = '\0';

    if (!term) { snprintf(buf, buf_size, "no terminal"); return 0; }
    if (term->alt_screen_active) {
        snprintf(buf, buf_size, "alt screen active");
        return 0;
    }
    if (term->cursor.row < 0 || term->cursor.row >= term->rows) {
        snprintf(buf, buf_size, "cursor row %d out of range [0,%d)",
                 term->cursor.row, term->rows);
        return 0;
    }
    if (term->cursor.col < 0) {
        snprintf(buf, buf_size, "cursor col %d < 0", term->cursor.col);
        return 0;
    }

    /* term_screen_to_phys() (term.h) -- the exact mapping
     * term_cursor_row_text() itself uses -- not a hand-rolled copy. This
     * diagnostic's own former copy added a `logical < lines_count` check
     * that production's term_cursor_row_text() no longer has (see
     * term_screen_to_phys()'s comment): that extra check is what made
     * this very function log "logical row 7 out of range [0,1)" for the
     * regression report that led here -- the diagnostic was reproducing
     * the bug it was added to diagnose, not just describing it. */
    int physical = term_screen_to_phys(term, term->cursor.row);
    if (physical < 0 || physical >= term->lines_capacity) {
        snprintf(buf, buf_size, "physical row %d out of range [0,%d)",
                 physical, term->lines_capacity);
        return 0;
    }
    TermRow *row = term->lines[physical];
    if (!row) { snprintf(buf, buf_size, "row slot empty"); return 0; }

    int col = term->cursor.col;
    if (col > term->cols) col = term->cols;

    for (int c = col; c < row->len && c < term->cols; c++) {
        uint32_t cp = row->cells[c].codepoint;
        if (cp != 0 && cp != ' ') {
            snprintf(buf, buf_size,
                     "non-blank cell after cursor: column %d, U+%04X",
                     c, (unsigned)cp);
            return 0;
        }
    }

    size_t pos = 0;
    for (int i = 0; i < col && pos + 5 < buf_size; i++) {
        uint32_t cp = row->cells[i].codepoint;
        if (cp == 0) cp = ' ';
        if (cp >= 0x20 && cp < 0x7F) {
            buf[pos++] = (char)cp;
        } else {
            int n = snprintf(buf + pos, buf_size - pos, "\\x%02X",
                              cp <= 0xFFu ? (unsigned)cp : 0xFFu);
            if (n < 0) break;
            pos += (size_t)n;
        }
    }
    buf[pos < buf_size ? pos : buf_size - 1] = '\0';
    return 1;
}

/* Diagnostic only, added for a 2026-09-25 regression report: a maintainer's
 * single-command PowerShell batch ran to completion in the terminal but
 * the AI panel never settled -- card stuck "running" forever, no status
 * line, so nothing in the product's own UI said why. A live reproduction
 * against a real ConPTY-hosted PowerShell could not reproduce it (see the
 * wintest case below), which points at something particular to the
 * reporter's own machine/session -- this exists to capture that, without
 * asking for a debugger attach. Logs at ai WARN (the debug log folder,
 * always on -- see init_app_log(), src/main.c) every field the settle
 * gate (`ready = !dispatch_await_echo && quiet && term_at_prompt(term)`,
 * ai_chat_dispatch_tick() below) and the no-prefix safety checks actually read,
 * but only once a batch has gone >3 s without sending a command or
 * settling (d->dispatch_last_progress_tick), and at most once every 2 s
 * after that (d->dispatch_last_diag_log_tick) -- cheap in the case that
 * matters (nothing happening) and silent otherwise. Never logs command
 * text beyond the cursor row (which may hold whatever the user typed
 * there -- acceptable for a local diagnostic log -- but never the AI
 * conversation). */
static void dispatch_log_stall_if_due(AiChatData *d, CmdBatch *batch)
{
    if (!d || !batch) return;

    DWORD now = GetTickCount();
    if (now - d->dispatch_last_progress_tick <= 3000u) return;
    if (d->dispatch_last_diag_log_tick != 0 &&
        now - d->dispatch_last_diag_log_tick < 2000u) return;
    d->dispatch_last_diag_log_tick = now;

    Terminal *term = d->active_term;

    int next_idx = chat_approval_next_approved(&batch->q);
    unsigned long seq = term ? term->write_seq : 0;
    int quiet = (int)((now - d->dispatch_last_change_tick) >= PROMPT_QUIET_MS);
    int at_prompt = term_at_prompt(term);
    int unambiguous = term_at_unambiguous_prompt(term);
    int continuation = term_at_continuation_prompt(term);
    int windows_prompt = term_cursor_row_is_windows_prompt(term);

    DispatchLineClearMode mode = dispatch_line_clear_mode(
        active_session_kind(d), active_shell_name(d), windows_prompt);

    char keystroke_desc[24] = "none";
    if (d->active_io && d->active_io->last_input_tick)
        snprintf(keystroke_desc, sizeof(keystroke_desc), "%lu",
                 (unsigned long)(now - *d->active_io->last_input_tick));

    char row_desc[220];
    int row_ok = describe_cursor_row(term, row_desc, sizeof(row_desc));

    char idx_desc[16];
    if (next_idx >= 0)
        snprintf(idx_desc, sizeof(idx_desc), "%d", next_idx);
    else
        snprintf(idx_desc, sizeof(idx_desc), "settle");

    /* Plain state as fields; the cursor-row text (what the user may have
     * typed there) goes through DLOG_CONTENT, so it is written only when
     * "Include terminal content" is on. row_ok says whether row_desc is the
     * row or the reason there is none. */
    DLOG(DLOG_AI, DLOG_WARN, 0, "dispatch.stall",
         "batch=%d next=%s sent=%d await_echo=%d quiet=%d quiet_ms=%lu "
         "write_seq=%lu dispatch_seq=%lu at_prompt=%d unambiguous=%d "
         "continuation=%d windows_prompt=%d mode=%s keystroke_ms=%s "
         "alt_screen=%d row=%d col=%d rows=%d cols=%d row_ok=%d",
         batch->id, idx_desc, d->dispatch_sent_count, d->dispatch_await_echo,
         quiet, (unsigned long)(now - d->dispatch_last_change_tick),
         seq, d->dispatch_seq, at_prompt, unambiguous, continuation,
         windows_prompt,
         mode == DISPATCH_LINE_CLEAR_READLINE ? "READLINE" : "NONE",
         keystroke_desc,
         term ? (int)term->alt_screen_active : -1,
         term ? term->cursor.row : -1, term ? term->cursor.col : -1,
         term ? term->rows : -1, term ? term->cols : -1,
         row_ok);
    DLOG_CONTENT(DLOG_AI, DLOG_WARN, 0, "dispatch.stall.row", "text",
                 row_desc, strlen(row_desc));
}

/* TIMER_CMD_QUEUE tick: advance the dispatcher (on d->dispatch_batch_id)
 * by at most one command. Tracks "quiet" (no terminal writes) and
 * "changed since the last send" (dispatch_await_echo) off
 * active_term->write_seq so the prompt that was on screen before a
 * command runs can never be mistaken for the next prompt. See
 * docs/superpowers/specs/2026-09-07-command-dispatch-and-
 * auto-approve-levels.md, section A. */
void ai_chat_dispatch_tick(AiChatData *d)
{
    if (!d || !d->dispatch_active) return;

    /* The transport can vanish mid-batch (window.c clears active_io before
     * closing it at EOF), and the batch must be abandoned visibly rather
     * than waiting for a prompt that cannot come. */
    if (!d->active_term || !d->active_io) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                        "[session ended -- remaining commands cancelled]");
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
        /* notify: the model is told which commands ran and which did not,
         * rather than being left to assume the whole batch executed. No
         * Ctrl+C -- active_io is already gone, which is why we're here. */
        ai_chat_dispatch_cancel(d, NULL, 1,
                                runcmd_path_plan(RUNCMD_PATH_SESSION_ENDED).cause);
        return;
    }

    CmdBatch *batch = d->active_state
        ? cmd_batch_find(&d->active_state->batches, d->dispatch_batch_id)
        : NULL;
    if (!batch) {
        /* The batch vanished from under the dispatcher (e.g. the session
         * it belonged to was switched away from and its dispatch was
         * cancelled, but this stale tick still fired) -- stop cleanly. */
        KillTimer(d->hwnd, TIMER_CMD_QUEUE);
        d->dispatch_active = 0;
        d->dispatch_batch_id = 0;
        d->dispatch_stall_reported = 0;
        d->dispatch_ambiguous_since_tick = 0;
        d->dispatch_ambiguous_reported = 0;
        if (d->hSendBtn) {
            SetWindowText(d->hSendBtn, ">");
            InvalidateRect(d->hSendBtn, NULL, TRUE);
        }
        return;
    }

    dispatch_log_stall_if_due(d, batch);

    unsigned long seq = d->active_term->write_seq;
    if (seq != d->dispatch_seq) {
        d->dispatch_seq = seq;
        d->dispatch_last_change_tick = GetTickCount();
        d->dispatch_await_echo = 0;
        /* Output means the stall (if there was one) is over: the next one
         * gets its own status line. The ambiguous-prompt wait is *not*
         * reset here, deliberately: a command that updates a line ending
         * in something prompt-shaped (a progress bar's "... 50 %",
         * redirection's "... >") on every burst of output would otherwise
         * re-arm and re-report every cycle without ever timing out --
         * dispatch_ambiguous_since_tick/_reported are cleared only when
         * the check actually passes, or the batch starts, cancels or
         * finishes (see below and ai_chat_dispatch_start()/ai_chat_dispatch_cancel()). */
        d->dispatch_stall_reported = 0;
    }

    int quiet = (GetTickCount() - d->dispatch_last_change_tick) >= PROMPT_QUIET_MS;
    int ready = !d->dispatch_await_echo && quiet &&
                term_at_prompt(d->active_term);
    if (!ready) {
        /* A shell waiting for the rest of an unfinished line looks exactly
         * like a hang from here: no output, and no primary prompt to send
         * the next command to. Say so once per stall and keep waiting --
         * never a Ctrl+C of our own accord, because a command that prompts
         * with "> " itself (read -p, a REPL) is indistinguishable from an
         * unclosed quote and killing it would be worse. The check runs
         * whether or not a command has been sent, so a quote the user
         * opened by hand before the batch is reported too. */
        if (quiet && !d->dispatch_await_echo && !d->dispatch_stall_reported &&
            term_at_continuation_prompt(d->active_term)) {
            d->dispatch_stall_reported = 1;
            chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                "[the shell is waiting for more input (a \">\" or \">>\" prompt). "
                "Finish the line in the terminal, or press Stop to cancel.]");
            if (d->hChatList) chat_listview_invalidate(d->hChatList);
        }
        return;
    }

    int idx = chat_approval_next_approved(&batch->q);
    if (idx >= 0) {
        if (d->dispatch_last_idx >= 0) {
            chat_approval_set_completed(&batch->q, d->dispatch_last_idx);
            /* The finished call's output, read at the prompt (a no-op after
             * the first tick that sees it, the capture's own flag). */
            runcmd_take_entry(d, batch, d->dispatch_last_idx);
            /* Sync the display now, not only right before the next send
             * below: the checks that follow can return without sending
             * anything this tick (a no-prefix wait, possibly for several
             * ticks in a row), and the card for the command that just
             * finished must not sit showing "running" for that whole
             * wait. */
            chat_msg_batch_sync_run(&d->msg_list, batch->id, &batch->q, 0);
            if (d->hChatList) chat_listview_invalidate(d->hChatList);
        }

        /* Computed once per tick and reused for both the safety gate below
         * and the execute_command() call further down, so the prefix
         * actually written can never disagree with the check that decided
         * it was safe to send (see execute_command()'s line_clear_mode
         * parameter doc). term_cursor_row_is_windows_prompt() overrides
         * the session's own kind/shell name to NONE whenever the live
         * cursor row looks like a PowerShell or cmd prompt -- an SSH
         * session to a Windows host, or a pwsh/cmd started as a nested
         * shell inside a local Git bash/MSYS2 session (dispatch_line_clear.h). */
        int windows_prompt = term_cursor_row_is_windows_prompt(d->active_term);
        DispatchLineClearMode line_clear_mode = dispatch_line_clear_mode(
            active_session_kind(d), active_shell_name(d), windows_prompt);

        /* For a local shell that gets no clear-line prefix (PowerShell,
         * cmd, custom, or one not yet resolved -- see execute_command()
         * and dispatch_line_clear.h; SSH normally always gets the prefix,
         * whatever its CmdPlatform, unless windows_prompt overrides it),
         * term_at_prompt()'s loose check above -- the cursor row simply
         * ends in a prompt character -- is not enough to send on: "PS
         * C:\Users\thoma> ls -la >" also ends in '>' (a redirection
         * operator mid-command) and would have the AI's command appended
         * to a line the user is still typing. A readline shell (or any SSH
         * session not currently at a Windows prompt) always gets the
         * Ctrl+E Ctrl+U prefix regardless of what is on the line first, so
         * the loose check already covers it -- this block runs only for
         * the no-prefix path, and only here (inside idx >= 0, after the
         * previous entry is marked completed), so a fully-sent batch
         * settling below is never at risk of it and every card reflects
         * reality before any cancel below. */
        if (line_clear_mode == DISPATCH_LINE_CLEAR_NONE) {
            DWORD now_tick = GetTickCount();

            /* A keystroke landed too recently to trust the cursor row
             * (ConPTY echoes asynchronously, so a character just typed
             * but not yet echoed is invisible to the row text below --
             * see dispatch_keystroke_too_recent()). Delay silently: this
             * is not "an ambiguous prompt" -- it resolves on its own the
             * moment the echo catches up (almost always well under a
             * second), so it must not post the status line below, nor
             * start or advance the 5 s cancel clock -- only a prompt row
             * that is genuinely ambiguous does either of those.
             * last_input_tick (SessionIo.last_input_tick,
             * src/term/session_io.h) points at Session.last_term_input_tick
             * specifically, bumped only where a keystroke or paste is
             * actually written to *this* terminal's session -- not AI-panel
             * typing or mouse-wheel scrolling, neither of which can glue
             * text onto a dispatched command, so typing the AI's next
             * question cannot stall or cancel this batch. NULL when
             * nothing tracks one for this session, in which case there is
             * nothing to guard against. */
            if (d->active_io && d->active_io->last_input_tick) {
                unsigned long since_keystroke = (unsigned long)
                    (now_tick - *d->active_io->last_input_tick);
                if (dispatch_keystroke_too_recent(since_keystroke))
                    return;
            }

            /* The stricter, bare-prompt-only check: an ordinary prompt can
             * legitimately have a space right before its terminator (cmd's
             * default "$P $G" renders as "C:\x >"; a themed prompt like
             * "[main] >"), so this alone must not cancel the batch
             * outright. */
            if (!term_at_unambiguous_prompt(d->active_term)) {
                /* Not something waiting out can necessarily fix -- unlike
                 * a continuation prompt, finishing the command does not
                 * make an oddly-shaped prompt stop looking ambiguous --
                 * but a single failed check must not cancel outright
                 * either: report once and keep waiting, the same pattern
                 * as the continuation-prompt stall above, and cancel only
                 * once the condition has persisted past
                 * DISPATCH_AMBIGUOUS_PROMPT_TIMEOUT_MS (see
                 * dispatch_line_clear.h for why that bound exists and how
                 * it was chosen). The timer and the report-once flag are
                 * cleared only when this check passes below, or the batch
                 * starts, cancels or finishes -- never merely because
                 * output arrived (see the write_seq handling above) --
                 * so a command that keeps refreshing a prompt-shaped line
                 * cannot re-arm this every burst without ever timing out. */
                if (d->dispatch_ambiguous_since_tick == 0)
                    d->dispatch_ambiguous_since_tick = now_tick;

                unsigned long since_ambiguous = (unsigned long)
                    (now_tick - d->dispatch_ambiguous_since_tick);

                if (dispatch_ambiguous_prompt_timed_out(since_ambiguous)) {
                    ai_chat_dispatch_cancel(d,
                        "[not sent: the input line isn't empty, or the "
                        "prompt ends with a space before its last "
                        "character]", 1,
                        runcmd_path_plan(RUNCMD_PATH_TIMEOUT).cause);
                    return;
                }

                if (!d->dispatch_ambiguous_reported) {
                    d->dispatch_ambiguous_reported = 1;
                    chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                        "[not sent yet: the input line isn't empty, or the "
                        "prompt ends with a space before its last "
                        "character. Waiting...]");
                    if (d->hChatList) chat_listview_invalidate(d->hChatList);
                }
                return;
            }

            /* Clear -- reset the wait so a later, unrelated ambiguity
             * (a different command, later in the batch) gets its own
             * timeout window and its own status line. */
            d->dispatch_ambiguous_since_tick = 0;
            d->dispatch_ambiguous_reported = 0;
        }

        /* N = commands already sent plus everything still APPROVED right
         * now (idx included) -- so the label tracks correctly even when
         * more commands get approved after the dispatcher started. */
        int approved_now = 0;
        for (int i = 0; i < batch->q.count; i++)
            if (batch->q.entries[i].status == APPROVE_APPROVED)
                approved_now++;
        int total = d->dispatch_sent_count + approved_now;

        chat_approval_set_executing(&batch->q, idx);
        /* The cards follow the queue: the one just finished turns "ran",
         * this one turns "running", the rest stay "queued". */
        chat_msg_batch_sync_run(&d->msg_list, batch->id, &batch->q, 0);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
        RunCmdCall *rcall = batch->runcmd
            ? runcmd_call_for_entry(batch->runcmd, idx) : NULL;
        execute_command(d, batch->q.entries[idx].command, line_clear_mode, rcall);
        d->dispatch_last_idx = idx;
        d->dispatch_sent_count++;
        d->dispatch_await_echo = 1;
        d->dispatch_last_progress_tick = GetTickCount();

        float now = (float)GetTickCount() / 1000.0f;
        chat_activity_set_phase(&d->activity, ACTIVITY_EXECUTING, now);
        chat_activity_set_exec(&d->activity, d->dispatch_sent_count, total);

        char prog[80];
        snprintf(prog, sizeof(prog), "running %d/%d \xC2\xB7 waiting for prompt",
                 d->dispatch_sent_count, total);
        ai_chat_start_indicator(d, prog);
    } else {
        /* Nothing left to send in this batch -- settle it, remove it, and
         * tell the AI which batch just ran (rule 4) before letting it
         * continue. */
        if (d->dispatch_last_idx >= 0) {
            chat_approval_set_completed(&batch->q, d->dispatch_last_idx);
            runcmd_take_entry(d, batch, d->dispatch_last_idx);
        }

        {
            char line[96];
            snprintf(line, sizeof(line), "settled batch %d after %lu ms",
                     batch->id,
                     (unsigned long)(GetTickCount() - d->dispatch_start_tick));
            DLOG(DLOG_AI, DLOG_INFO, 0, "dispatch.settled", "msg=%s", line);
        }

        if (batch->runcmd) {
            /* The tick's end of a run_command batch: the results answer the
             * model, never the [EXEC] continue text. The finish settles,
             * records, removes, and starts the next request (SETTLED). */
            KillTimer(d->hwnd, TIMER_CMD_QUEUE);
            d->dispatch_active = 0;
            d->dispatch_batch_id = 0;
            d->dispatch_stall_reported = 0;
            d->dispatch_ambiguous_since_tick = 0;
            d->dispatch_ambiguous_reported = 0;
            if (d->hSendBtn) {
                SetWindowText(d->hSendBtn, ">");
                InvalidateRect(d->hSendBtn, NULL, TRUE);
            }
            ai_chat_runcmd_finish(d, d->active_state, batch,
                                  runcmd_path_plan(RUNCMD_PATH_TICK_END).cause);
            return;
        }

        int batch_id = batch->id;
        /* Read before ai_chat_batch_remove_recorded() below frees the batch. */
        int unattended = !batch->user_acted;
        int newer_exchanges = (d->conv.msg_count > batch->conv_mark) ? 1 : 0;
        char first_cmd[1024] = "";
        if (batch->q.count > 0)
            snprintf(first_cmd, sizeof(first_cmd), "%s",
                     batch->q.entries[0].command);

        /* Final card states, and the per-command outcome list for the
         * model -- both read the queue, which cmd_batch_remove() is about
         * to free. batch_ending = 1: nothing should still be APPROVED here
         * (next_approved just said so), but anything that were would be
         * "not run", never "queued". */
        chat_msg_batch_sync_run(&d->msg_list, batch_id, &batch->q, 1);

        char continue_text[4096] = "";
        int have_continue = ai_build_continue_text(newer_exchanges, first_cmd,
                                                   &batch->q, continue_text,
                                                   sizeof(continue_text)) > 0;
        /* The per-command outcomes list can overflow the buffer on a batch
         * with many/long commands -- fall back to the opening sentence
         * alone so a continue message is still sent and
         * ai_chat_maybe_start_next_batch() still gets its turn once it completes,
         * rather than silently dropping the continue. */
        if (!have_continue)
            have_continue = ai_build_continue_text(newer_exchanges, first_cmd,
                                                    NULL, continue_text,
                                                    sizeof(continue_text)) > 0;

        KillTimer(d->hwnd, TIMER_CMD_QUEUE);
        d->dispatch_active = 0;
        d->dispatch_batch_id = 0;
        d->dispatch_stall_reported = 0;
        d->dispatch_ambiguous_since_tick = 0;
        d->dispatch_ambiguous_reported = 0;

        ai_chat_batch_settle_rows(d, batch);
        ai_chat_batch_remove_recorded(d, d->active_state, batch, 1);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);

        if (d->hSendBtn) {
            SetWindowText(d->hSendBtn, ">");
            InvalidateRect(d->hSendBtn, NULL, TRUE);
        }

        if (have_continue) ai_chat_send_continue_message(d, continue_text, unattended);
    }
}

#endif /* _WIN32 */
