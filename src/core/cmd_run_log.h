/* src/core/cmd_run_log.h */
#ifndef NUTSHELL_CMD_RUN_LOG_H
#define NUTSHELL_CMD_RUN_LOG_H

#include <stddef.h>
#include <stdint.h>
#include "cmd_batch.h"

/*
 * cmd_run_log -- a record of every command batch the AI panel has finished
 * with, so a rebuilt display (tab switch and back, undock) can show again
 * which commands ran and which did not (spec 2026-11-05 run-command tool,
 * decision 21). One log per session, on AiSessionState: up to CMD_RUN_LOG_MAX
 * records, the oldest dropped first.
 *
 * A record is taken from a batch's queue just before the batch is freed. It
 * holds a heap copy of each command and the inputs chat_cmd_label() needs
 * (approved, blocked, quoted, run) as the row ended, so the replay looks like
 * the live rows did. It is tied to the conversation by the index of the
 * assistant reply that produced the batch and a hash of that reply's text: a
 * record whose turn was popped (Retry) or whose index a new reply took does
 * not match and is not shown (cmd_run_record_matches()).
 *
 * Pure, portable, no windows.h -- fully testable on Linux.
 */

#define CMD_RUN_LOG_MAX 16

typedef struct {
    char *command;                       /* heap copy */
    CmdSafetyLevel safety;
    int quoted;
    char reason[CMD_REASON_MAX];
    char note[CMD_NOTE_MAX];             /* the assistant's note for the command
                                          * (the live entry's), so a replayed
                                          * row's details popup is not blank */
    int approved, blocked, run;          /* chat_cmd_label's inputs as the row ended */
} CmdRunRow;

typedef struct {
    int reply_msg;                       /* CmdBatch.reply_msg */
    uint32_t turn_hash;                  /* cmd_run_log_turn_hash() of that reply */
    int n;
    CmdRunRow rows[APPROVAL_MAX_CMDS];
} CmdRunRecord;

typedef struct {
    CmdRunRecord *rec[CMD_RUN_LOG_MAX];
    int count;                           /* rec[0] is the oldest */
} CmdRunLog;

/* Empty the log without freeing (use on a log that holds nothing). NULL-safe. */
void cmd_run_log_init(CmdRunLog *log);

/* FNV-1a 32 of `len` bytes of `text` (a NULL text is the empty string). */
uint32_t cmd_run_log_turn_hash(const char *text, size_t len);

/* Map one queue entry to the row it leaves in a record: the label inputs
 * only, `command` is left NULL (not copied). `batch_ending` is 1 when the
 * batch is being removed with work undone:
 *
 *   COMPLETED -> approved 1, run DONE
 *   EXECUTING -> approved 1, run RUNNING, or UNFINISHED when batch_ending
 *   APPROVED  -> approved 1, run NONE, or ABORTED when batch_ending
 *   DENIED    -> approved 0
 *   PENDING -> approved -1, blocked 0 (chat_cmd_label: "skipped")
 *   BLOCKED -> approved -1, blocked 1 (chat_cmd_label: "held", as live)
 *
 * `safety`, `quoted` and `reason` are copied from the entry; quoted wins in
 * chat_cmd_label(). NULL `e` or `out` does nothing. */
void cmd_run_row_from_entry(const ApprovalEntry *e, int batch_ending,
                            CmdRunRow *out);

/* Record `batch`'s queue as a new record at the end of the log, with the
 * batch's reply_msg and `turn_hash` (the caller, which can see the
 * conversation, hashes the reply at batch->reply_msg). Returns 0 when
 * recorded; -1, with nothing recorded and the log unchanged, for a NULL log
 * or batch, a tool batch (cmd_batch_is_tool), an empty queue, or an
 * allocation failure. A record that would be the 17th frees and drops the
 * oldest, but only once the new one is complete. */
int cmd_run_log_record(CmdRunLog *log, const CmdBatch *batch, uint32_t turn_hash,
                       int batch_ending);

/* The rebuild's rule: 1 when `rec` belongs to message `msg_index`, i.e. its
 * reply_msg == msg_index, that message is an assistant turn (`is_assistant`)
 * and the FNV-1a 32 of its `content` (`len` bytes) equals rec->turn_hash. A
 * NULL `rec` is 0; a NULL `content` is the empty string. Plain arguments so
 * this header need not include ai_prompt.h (which includes this one). */
int cmd_run_record_matches(const CmdRunRecord *rec, int msg_index,
                           int is_assistant, const char *content, size_t len);

/* The index to key a new batch's rows to (CmdBatch.reply_msg): the last
 * message of the conversation, `msg_count - 1`, when it is the reply that was
 * just stored, else -1 (matches no turn). That needs all of: `stored` (the
 * code that produced the reply added it to the conversation: ai_conv_add()
 * returned 0 -- a full conversation refuses the add, and then "the last
 * message" is an older turn), a conversation with a message, and the last
 * message an assistant turn (`last_is_assistant`). The reply's text is not
 * compared: a reply that went through a tool round is posted as every
 * iteration's text joined, while the conversation holds only the last. */
int cmd_run_reply_index(int stored, int msg_count, int last_is_assistant);

/* Drop (free) every record keyed to `reply_msg`, keeping the others in
 * order. Called where a reply is popped (Retry), so a new reply with the
 * same text at that index does not match the old reply's rows as well.
 * Returns the number dropped; NULL log or no match is 0. */
int cmd_run_log_forget(CmdRunLog *log, int reply_msg);

/* Detach every batch of `set` keyed to `reply_msg` (the popped reply's
 * index): reply_msg = -1 and reply_hash = 0, so its rows match no turn when
 * recorded. The other half of Retry's pop beside cmd_run_log_forget(): that
 * drops the records already made, this stops the batches still live from
 * making one for the old reply. A negative `reply_msg` does nothing. Returns
 * the number detached; NULL set is 0. */
int cmd_run_batches_unkey(CmdBatchSet *set, int reply_msg);

/* Free every record, then cmd_run_log_init(). Safe twice and on NULL. */
void cmd_run_log_free(CmdRunLog *log);

/* TEST ONLY. Replace the allocator the log uses for records and command
 * copies (default malloc; NULL restores it), so a test can fail one
 * allocation. Never xmalloc, which aborts: a failure must be observable and
 * leave nothing recorded (spec decision 25). Not for product code. */
void cmd_run_log_set_alloc_for_test(void *(*alloc)(size_t));

#endif /* NUTSHELL_CMD_RUN_LOG_H */
