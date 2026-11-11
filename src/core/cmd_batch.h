/* src/core/cmd_batch.h */
#ifndef NUTSHELL_CMD_BATCH_H
#define NUTSHELL_CMD_BATCH_H

#include <stdint.h>
#include "chat_approval.h"

/*
 * cmd_batch -- the set of pending command approval batches for one AI
 * chat session. Each AI reply that yields commands gets its own CmdBatch
 * (id + ApprovalQueue), so any number can be pending -- interactive, in
 * list order -- at once. See
 * docs/superpowers/specs/2026-09-09-pending-command-batches.md.
 *
 * Pure, portable, no windows.h -- fully testable on Linux.
 */

#define CMD_BATCH_MAX 8

typedef struct {
    int id;
    ApprovalQueue q;
    int conv_mark;  /* AiConversation.msg_count right after this batch's
                      * reply was added -- the caller (ai_chat.c) sets this
                      * right after cmd_batch_add(); comparing it to the
                      * live msg_count when the batch finishes running says
                      * whether "newer exchanges" have happened since (see
                      * ai_build_continue_text() in ai_prompt.h). Defaults
                      * to 0 from cmd_batch_add()'s calloc. */
    int reply_msg;  /* index in the conversation of the assistant reply that
                      * produced this batch -- the caller (ai_chat.c) sets it
                      * beside conv_mark. Not conv_mark - 1: the displayed
                      * [EXEC] branch adds its NOTE turns before conv_mark is
                      * taken, so that can be a USER turn. The run log keeps it
                      * (cmd_run_log.h) to replay the rows under the reply.
                      * -1 from cmd_batch_add(): not set, matches no turn. */
    uint32_t reply_hash; /* cmd_run_log_turn_hash() of that reply's content,
                      * taken where the batch is created (beside reply_msg),
                      * so the record made at removal keys to the reply that
                      * produced the batch even when the conversation has been
                      * moved or the reply replaced by then (a tab switch
                      * empties the working copy; Retry swaps the turn).
                      * 0 from cmd_batch_add(): not set. */
    int settled;    /* 1 once the live view has settled this batch's rows
                      * inline (the card is gone: ai_chat_batch_settle_rows()
                      * in the dispatcher, every place that settles). A rebuild
                      * (tab switch, undock) reads this, not the queue: the
                      * queue alone cannot say whether the card is up, since a
                      * ceiling change moves entries between PENDING and
                      * BLOCKED without settling or unsettling any row. 0 from
                      * cmd_batch_add()'s calloc: a new batch is on its card. */
    int user_acted; /* 1 once the user acted on this batch by hand (a
                      * Run/Approve click, or a user-initiated cancel).
                      * Read when the batch finishes: a batch the user
                      * acted on is an attended round and resets the
                      * session's auto-round counter instead of counting
                      * towards AI_AUTO_ROUNDS_MAX (ai_rounds_continue()
                      * in ai_prompt.h). 0 from cmd_batch_add()'s calloc. */
    unsigned tool_stream_id; /* non-zero: a tool call's one-row card, waiting on
                      * the AiStream with this id (spec 2026-11-05 decision
                      * 14). 0 = a command batch. */
    unsigned tool_gen;       /* the decision slot's generation for that call
                      * (decision 20); 0 for a command batch. */
} CmdBatch;

typedef struct {
    CmdBatch *b[CMD_BATCH_MAX];
    int count;
    int next_id;   /* ids start at 1 and never repeat within this set */
} CmdBatchSet;

/* Initialize an empty set. */
void cmd_batch_set_init(CmdBatchSet *set);

/* Free every batch still in the set. */
void cmd_batch_set_free(CmdBatchSet *set);

/* Allocate a new batch, chat_approval_init() its queue, and copy the
 * CmdPolicy from `defaults` (may be NULL, leaving the queue's own default)
 * so the session's policy carries over to the new batch. Appends it
 * to the set (oldest-first order). If the set is already at CMD_BATCH_MAX,
 * evicts the oldest batch first (freeing it) and, when evicted_id is
 * non-NULL, writes its id there so the caller can settle its rows in the
 * list view; evicted_id is set to 0 when no eviction occurs. Returns the
 * new batch, or NULL on allocation failure (set is left unchanged and
 * *evicted_id, if requested, is 0). */
CmdBatch *cmd_batch_add(CmdBatchSet *set, const ApprovalQueue *defaults,
                        int *evicted_id);

/* Find a batch by id. NULL if not present. */
CmdBatch *cmd_batch_find(CmdBatchSet *set, int id);

/* Remove and free a batch by id. No-op if not present. */
void cmd_batch_remove(CmdBatchSet *set, int id);

/* 1 when `b` is a tool call's card (tool_stream_id != 0), else 0 (also for
 * NULL). */
int cmd_batch_is_tool(const CmdBatch *b);

/* 1 when the set holds CMD_BATCH_MAX batches (a tool batch is then refused,
 * never allowed to evict) or `set` is NULL, else 0. */
int cmd_batch_full(const CmdBatchSet *set);

/* 1 when nothing in `b` is left to ask or to run: no entry PENDING, and no
 * command entry APPROVED (not yet sent) or EXECUTING (sent, output still
 * coming). A tool entry (tool = 1) is decided by its click and never
 * dispatched, so its status does not count (chat_approval_next_approved
 * skips it too). An empty queue is finished; NULL is 0. The settle path
 * removes a batch only when this holds: a batch with a command still running
 * stays until the dispatcher's tick sees it finish. */
int cmd_batch_finished(const CmdBatch *b);

/* Oldest command batch (list order) whose queue still needs the user, per
 * chat_approval_needs_user(). Tool batches are skipped. NULL if none. */
CmdBatch *cmd_batch_first_pending(CmdBatchSet *set);

/* Oldest command batch (list order) with an APPROVE_APPROVED entry not yet
 * sent, per chat_approval_next_approved(). Tool batches are skipped. NULL if
 * none. */
CmdBatch *cmd_batch_next_runnable(CmdBatchSet *set);

#endif /* NUTSHELL_CMD_BATCH_H */
