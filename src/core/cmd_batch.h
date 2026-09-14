/* src/core/cmd_batch.h */
#ifndef NUTSHELL_CMD_BATCH_H
#define NUTSHELL_CMD_BATCH_H

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

/* Oldest batch (list order) whose queue still needs the user, per
 * chat_approval_needs_user(). NULL if none. */
CmdBatch *cmd_batch_first_pending(CmdBatchSet *set);

/* Oldest batch (list order) with an APPROVE_APPROVED entry not yet sent,
 * per chat_approval_next_approved(). NULL if none. */
CmdBatch *cmd_batch_next_runnable(CmdBatchSet *set);

#endif /* NUTSHELL_CMD_BATCH_H */
