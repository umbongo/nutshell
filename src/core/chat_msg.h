/* src/core/chat_msg.h */
#ifndef NUTSHELL_CHAT_MSG_H
#define NUTSHELL_CHAT_MSG_H

#include <stddef.h>
#include "cmd_classify.h"
#include "chat_approval.h"
#include "cmd_batch.h"
#include "text_runs.h"
#include "thinking_mode.h"

/* Execution state of a command card, synced from the batch's ApprovalQueue
 * -- never set by hand (see chat_msg_batch_sync_run()). */
enum { CHAT_RUN_NONE = 0, CHAT_RUN_RUNNING, CHAT_RUN_DONE, CHAT_RUN_ABORTED,
       CHAT_RUN_UNFINISHED /* sent, and the panel stopped waiting for it */ };

/* How a finished command card shows (chat_msg_fold_refresh()). */
enum { CHAT_FOLD_NONE = 0,  /* in full, as it always was */
       CHAT_FOLD_CLOSED,    /* the group's head: one "N commands" row */
       CHAT_FOLD_OPEN };    /* the group's head: that row, then the rows */

typedef enum {
    CHAT_ITEM_USER,
    CHAT_ITEM_AI_TEXT,
    CHAT_ITEM_COMMAND,
    CHAT_ITEM_STATUS,
    CHAT_ITEM_TOOL_CALL,    /* "using tool: <name>" status-style row */
    CHAT_ITEM_TOOL_RESULT   /* tool result (brief, collapsible) */
} ChatItemType;

typedef struct ChatMsgItem {
    ChatItemType type;
    int id;
    int measured_height;
    int dirty;
    char *text;
    size_t text_len;

    /* Character selection (spec 2026-10-18 section 3): what the list view's
     * last paint drew of this item, reset for every item at the start of
     * each paint, so an item that was not painted has none. `runs` holds the
     * main text and the Thinking body (TextRun.part); `pinned_runs` the copy
     * of a user bubble pinned at the top of the list. painted_height is the
     * height that paint actually used (0 when not painted). */
    TextRuns runs;
    TextRuns pinned_runs;
    int painted_height;

    union {
        struct {
            char *thinking_text;
            int thinking_mode;       /* a ThinkMode (thinking_mode.h): how the row shows
                                       * its text now.  The list view recomputes it from
                                       * the saved pref at every layout; default collapsed. */
            float thinking_elapsed;
            int thinking_complete;
            int thinking_scroll_y;   /* internal scroll offset for expanded thinking */
            int thinking_autoscroll; /* 1 = auto-scroll to bottom during streaming */
            /* The Thinking body's wrapped height, cached by the list view
             * per (text length, box width, line height) so a hover move
             * costs no layout; think_cache_len = -1 means nothing cached.
             * think_cache_text_w is the width the text was laid out at
             * (the box less its inset and, when it overflows, the
             * scrollbar), shared by measure and paint. */
            int think_cache_len;
            int think_cache_w;
            int think_cache_line_h;
            int think_cache_full_h;
            int think_cache_text_w;
        } ai;
        struct {
            char *command;
            char *note;         /* the assistant's explanation of the command
                                  * (heap, may be NULL); secure_zero'd and freed
                                  * with the item like `command` */
            char reason[CMD_REASON_MAX]; /* why a sensitive command always
                                  * asks ("reads credentials"), "" when it
                                  * is not sensitive; copied from the
                                  * ApprovalEntry by chat_msg_set_reason() */
            CmdSafetyLevel safety;
            int approved;       /* -1=pending, 0=denied, 1=approved */
            int blocked;
            int quoted;         /* 1 = the marker sat inside code: never runs,
                                  * the chip says "quoted, not run" (see
                                  * chat_approval_add_quoted()) */
            int selected;       /* 1=tickbox checked for batch approve */
            int settled;        /* 1=finalized, rendered inline, not in active container */
            int batch;          /* CmdBatch id this command belongs to; 0 for
                                  * demo/legacy rows (see src/core/cmd_batch.h) */
            int run;            /* CHAT_RUN_* -- synced from the batch's
                                  * ApprovalQueue by chat_msg_batch_sync_run(),
                                  * never set by hand */
            int container_scroll; /* per-container scroll offset, px -- only
                                    * meaningful on a container's first item
                                    * (see chat_listview.c's cmd_container_measure) */
            int tool;           /* 1 = a tool call's card row, not a command
                                  * (chat_msg_set_tool()): no ceiling change
                                  * touches it (chat_msg_apply_ceiling()) */
            int tool_outcome;   /* a ToolDecision (tool_decision.h) as int:
                                  * PENDING once chat_msg_set_tool(item, 1)
                                  * ran, NONE (0) on a command row */
            int fold;           /* CHAT_FOLD_*, on a finished group's first
                                  * row; derived by chat_msg_fold_refresh() */
            int fold_hidden;    /* 1 = a row of a closed group: not shown */
            int fold_changed;   /* 1 = this row's fold/fold_hidden changed in
                                  * the last chat_msg_fold_refresh() call */
            int fold_open;      /* the user's choice on the group's first
                                  * row: 1 = keep it expanded (default 0) */
        } cmd;
    } u;

    struct ChatMsgItem *next;
    struct ChatMsgItem *prev;
} ChatMsgItem;

typedef struct {
    ChatMsgItem *head;
    ChatMsgItem *tail;
    int count;
    int next_id;
} ChatMsgList;

/* Initialize a message list. */
void chat_msg_list_init(ChatMsgList *list);

/* Create and append an item. Returns the new item, or NULL on alloc failure.
 * text is copied (heap-allocated). */
ChatMsgItem *chat_msg_append(ChatMsgList *list, ChatItemType type, const char *text);

/* Remove an item from the list and free it. */
void chat_msg_remove(ChatMsgList *list, ChatMsgItem *item);

/* Free all items in the list. */
void chat_msg_list_clear(ChatMsgList *list);

/* Update item text (re-allocates). Marks item dirty. Returns 0 on success. */
int chat_msg_set_text(ChatMsgItem *item, const char *text);

/* Set command fields on a CHAT_ITEM_COMMAND item. command string is copied.
 * Does not touch u.cmd.batch -- callers tag the item's batch separately
 * with chat_msg_set_batch(), typically right after creating it. */
int chat_msg_set_command(ChatMsgItem *item, const char *command,
                         CmdSafetyLevel safety, int blocked);

/* Set the reason a sensitive command row asks (u.cmd.reason), copied and cut
 * to CMD_REASON_MAX - 1 bytes on a UTF-8 boundary. NULL or "" clears it.
 * NULL-safe; no-op on a NULL or non-command item. Does not mark the item
 * dirty: the row's height does not depend on it. chat_msg_set_command()
 * clears it, so call this after that. */
void chat_msg_set_reason(ChatMsgItem *item, const char *reason);

/* Set (copy) or clear (NULL) the note on a CHAT_ITEM_COMMAND item. Returns 0
 * on success, -1 for a NULL or non-command item or allocation failure. Does
 * not mark the item dirty: a note is shown only in the details popup. */
int chat_msg_set_note(ChatMsgItem *item, const char *note);

/* Mark a CHAT_ITEM_COMMAND item as a tool call's row (u.cmd.tool). Setting
 * it (non-zero) also sets u.cmd.tool_outcome to TOOL_DECISION_PENDING;
 * clearing it leaves the outcome alone. NULL-safe; no-op on a non-command
 * item. */
void chat_msg_set_tool(ChatMsgItem *item, int tool);

/* Set u.cmd.tool_outcome (a ToolDecision as int). NULL-safe; no-op on a
 * non-command item. Does not mark the item dirty: the caller does when the
 * chip's text changed. */
void chat_msg_set_tool_outcome(ChatMsgItem *item, int outcome);

/* Bring one chat item into line with a changed policy ceiling, the per-item
 * body of the walks in apply_policy_change (src/ui/ai_chat_status.c).
 * Raised (next_allowed > prev_allowed): a CHAT_ITEM_COMMAND that is blocked,
 * not quoted and whose safety is within next_allowed becomes unblocked,
 * approved = -1, selected = 1. Lowered: an unsettled command with
 * approved == -1 whose safety is above next_allowed becomes blocked. A tool
 * row (u.cmd.tool), a non-command item and NULL are never touched, either
 * way, and an unchanged ceiling changes nothing. */
void chat_msg_apply_ceiling(ChatMsgItem *it, int prev_allowed, int next_allowed);

/* Set the batch id on a CHAT_ITEM_COMMAND item (see cmd_batch.h). No-op on
 * a non-command item. */
void chat_msg_set_batch(ChatMsgItem *item, int batch);

/* Mark a CHAT_ITEM_COMMAND item as quoted (u.cmd.quoted). NULL-safe; no-op
 * on a non-command item. */
void chat_msg_set_quoted(ChatMsgItem *item, int quoted);

/* Set thinking text on a CHAT_ITEM_AI_TEXT item. Copied to heap. */
int chat_msg_set_thinking(ChatMsgItem *item, const char *thinking_text);

/* Get item count. */
int chat_msg_count(const ChatMsgList *list);

/* The item with this id, or NULL (ids are unique and never reused). */
ChatMsgItem *chat_msg_find(const ChatMsgList *list, int id);

/* Position of `item` among the list's unsettled CHAT_ITEM_COMMAND items
 * that share its batch id, in list order (0-based) -- matches the order
 * the batch's ApprovalQueue entries were added in, so q.entries[i] is the
 * i-th such item. Returns -1 if `item` is NULL, not a command, or already
 * settled. */
int chat_msg_batch_index(const ChatMsgList *list, const ChatMsgItem *item);

/* First unsettled CHAT_ITEM_COMMAND item in the list with u.cmd.batch ==
 * batch, in list order. NULL if none. */
ChatMsgItem *chat_msg_batch_first(const ChatMsgList *list, int batch);

/* Mark every unsettled CHAT_ITEM_COMMAND item with u.cmd.batch == batch as
 * settled (u.cmd.settled = 1) and dirty (for remeasure). Returns the
 * number of items settled. */
int chat_msg_batch_settle(ChatMsgList *list, int batch);

/* Visual weight for a command card's status chip -- one of the five
 * intents ns_tokens() carries (DIM/INFO/SUCCESS/WARNING map onto
 * text_dim/info/success/warning; there is no DANGER row in this table). */
typedef enum {
    CHAT_CMD_INTENT_DIM = 0,
    CHAT_CMD_INTENT_INFO,
    CHAT_CMD_INTENT_SUCCESS,
    CHAT_CMD_INTENT_WARNING
} ChatCmdIntent;

/* Pure label for a command card's status chip, from the item's own
 * approval/blocked/quoted/run state -- no queue lookup, no item pointer.
 * `approved` is the item's u.cmd.approved (-1 pending, 0 denied, 1
 * approved), `blocked` its u.cmd.blocked, `quoted` its u.cmd.quoted, `run`
 * a CHAT_RUN_* value. Tested in this priority order:
 *
 *   quoted (any other state)          -> "quoted, not run", DIM
 *   blocked (any run)                 -> "held",     WARNING
 *   approved == 0 (any run)           -> "denied",   DIM
 *   approved == -1, not blocked       -> "skipped",  DIM
 *   approved == 1, CHAT_RUN_NONE      -> "queued",   DIM
 *   approved == 1, CHAT_RUN_RUNNING   -> "running",  INFO
 *   approved == 1, CHAT_RUN_DONE      -> "ran",      SUCCESS
 *   approved == 1, CHAT_RUN_ABORTED   -> "not run",  WARNING
 *   approved == 1, CHAT_RUN_UNFINISHED -> "unfinished", WARNING
 *
 * "not run" is a command that was never sent; "unfinished" one that was sent
 * and whose outcome the panel did not see (the batch ended while it ran).
 *
 * "held" only outranks the run states because raising the policy ceiling
 * clears u.cmd.blocked on unblock (src/ui/ai_chat_status.c, the unblock walk) --
 * an approved-and-run command is therefore never still blocked, so this
 * priority order never hides a "ran"/"running" row behind "held".
 *
 * An `approved == 1` row with any other `run` value falls back to the
 * "queued"/DIM row. Returns a static string, never NULL. Writes *intent
 * when `intent` is non-NULL; leaves it untouched otherwise. */
const char *chat_cmd_label(int approved, int blocked, int quoted, int run,
                           ChatCmdIntent *intent);

/* The approval card's header, e.g. "4 commands \xC2\xB7 1 held \xC2\xB7 2 quoted":
 * `n` of `noun` (NULL means "command"; singular for 1, else an "s" added), then " \xC2\xB7 H held" when held > 0, then
 * " \xC2\xB7 Q quoted" when quoted > 0. Writes a NUL-terminated string into
 * buf and returns its length, or 0 (with buf[0] = NUL, when cap > 0) when it
 * does not fit in cap bytes or buf is NULL. */
int chat_card_header_text(const char *noun, int n, int held, int quoted, char *buf, size_t cap);

/* Sync u.cmd.run on every CHAT_ITEM_COMMAND item in `list` with
 * u.cmd.batch == batch_id, in list order, whether settled or not (a batch
 * settles its rows while the dispatcher is still running them, and a
 * settled row must keep tracking the queue same as a live one). The i-th
 * such item is paired with q->entries[i] -- the two sets stay in the same
 * order the batch was built in (see chat_msg_batch_index()). Items beyond
 * q->count, if any, are left untouched. Mapping, by the paired entry's
 * ApprovalStatus:
 *
 *   APPROVE_EXECUTING -> CHAT_RUN_RUNNING, or CHAT_RUN_UNFINISHED when
 *                        batch_ending (stopped mid-flight)
 *   APPROVE_COMPLETED -> CHAT_RUN_DONE
 *   APPROVE_APPROVED  -> CHAT_RUN_NONE, or CHAT_RUN_ABORTED when
 *                        batch_ending (approved but never sent)
 *   anything else      -> CHAT_RUN_NONE
 *
 * An item whose run value changes is marked dirty (for remeasure); one that
 * already matches is left alone. Returns the number of items synced (paired
 * with an entry), or 0 if `list` or `q` is NULL. Called from every point
 * the queue changes during dispatch (chat_approval_set_executing/
 * _set_completed, batch end, ai_chat_dispatch_cancel before cmd_batch_remove with
 * batch_ending = 1) and from chat_msg_append_batch_items() when a display is
 * rebuilt from the queue, so a rebuilt view shows the same labels as the
 * live panel. */
int chat_msg_batch_sync_run(ChatMsgList *list, int batch_id,
                            const ApprovalQueue *q, int batch_ending);

/* How a batch's rows stand, for the summary line under it (spec 2026-11-05
 * run-command tool, decision 21 (d)). Each row is counted once, by the rule
 * of chat_cmd_label(), except that a row still on the card (not settled)
 * counts `awaiting`, or `held` when blocked. A quoted row counts `quoted`
 * wherever it sits. `decided` counts the rows that have left the card
 * (settled). n is the sum of the ten state counts. */
typedef struct {
    int n, ran, running, unfinished, queued, awaiting, held, not_run, denied,
        skipped, quoted, decided;
} ChatBatchCounts;

/* Fill *out with the counts of the CHAT_ITEM_COMMAND items of `list` whose
 * u.cmd.batch == batch_id (zeroed first). Returns the number of such items,
 * 0 when `list` or `out` is NULL (out is still zeroed when non-NULL). */
int chat_msg_batch_counts(const ChatMsgList *list, int batch_id,
                          ChatBatchCounts *out);

/* The summary line for a batch: "<n> commands: " then the non-zero parts as
 * "<count> <word>" joined by ", ", in the order ran, running, unfinished,
 * queued, awaiting approval, held, not run, denied, skipped, quoted (the
 * words are states and take no plural). Writes it into buf and returns its
 * length. A line exists only for a batch of two or more rows of which at
 * least one has left the card (n >= 2 && decided >= 1); otherwise, for NULL
 * counts and when it does not fit in cap bytes (the NUL included), buf[0] is
 * set to NUL (when buf is non-NULL and cap > 0) and 0 is returned. */
int chat_batch_summary_text(const ChatBatchCounts *c, char *buf, size_t cap);

/* True when every row of the batch has reached a final state: n >= 1, all
 * decided (settled), and none running, queued or awaiting approval. Counts
 * cover every row of the batch anywhere in the list. CmdBatch.settled is not
 * the test: it is set while rows are still running. NULL is not final. */
int chat_batch_is_final(const ChatBatchCounts *c);

/* The fold row of a finished card: label "N commands" ("1 command") and
 * summary "\xC2\xB7 " then the non-zero parts as chat_batch_summary_text()
 * words them ("\xC2\xB7 1 ran, 1 unfinished, 2 not run"). *warn (when not
 * NULL) is 1 when anything is unfinished, not run or held -- denied and
 * skipped alone are not a warning. Returns 1 when the batch is final and
 * both strings fit; otherwise 0 with both set to "". */
int chat_batch_fold_text(const ChatBatchCounts *c, char *label, size_t lcap,
                         char *summary, size_t scap, int *warn);

/* Derive every command row's fold state from the list: a group is a
 * maximal run of consecutive settled, non-tool command rows of one non-zero
 * batch, folded when the whole batch is final, is that one run, and holds no
 * tool row. Sets u.cmd.fold (CLOSED or, with fold_open, OPEN) on the
 * group's first row and u.cmd.fold_hidden on the rest of a closed group;
 * every other row gets NONE / 0. Returns how many rows changed (0 on a
 * second call). Does not mark anything dirty. */
int chat_msg_fold_refresh(ChatMsgList *list);

/* Flip fold_open on the head of a folded group. Returns the new value; does
 * nothing (returns 0) when head is NULL or its fold is CHAT_FOLD_NONE. */
int chat_msg_fold_toggle(ChatMsgItem *head);

/* Append one CHAT_ITEM_COMMAND per entry in a batch's queue, in order,
 * tagged with the batch's id (chat_msg_set_batch) -- the inverse of what
 * WM_AI_RESPONSE builds live, used to rebuild a pending card wherever the
 * command items themselves are not carried across a rebuild: a session
 * switch (the rebuild, chat_rebuild_replay()) and --ui-demo's "batches" etc.
 * states (which build real CmdBatch entries from the canned ApprovalQueue and
 * let this same replay draw them).
 * Status maps onto the fields the list view reads: approved follows the click
 * (APPROVED/EXECUTING/COMPLETED 1, DENIED 0, PENDING/BLOCKED -1, which
 * chat_msg_set_command already defaults to). Whether the rows are settled
 * (inline, off the card) is the batch's, not each row's, and not derived from
 * the queue: the live view settles a batch's rows together
 * (ai_chat_batch_settle_rows(), which sets CmdBatch.settled), and a ceiling
 * change moves entries between PENDING and BLOCKED without settling or
 * unsettling a row, so the queue alone cannot say which the live view shows.
 * A batch not yet settled comes back as one whole card, its decided rows on
 * it, and its summary line (needs a settled row) as absent as it was live.
 * Run state is then synced from the queue (chat_msg_batch_sync_run with
 * batch_ending 0: a rebuild only replays batches still in the set). NULL list
 * or batch does nothing. */
void chat_msg_append_batch_items(ChatMsgList *list, const CmdBatch *batch);

/* If the NUL-terminated string in buf is longer than max-1 bytes, truncate
 * it at the last UTF-8 character boundary that fits in max-1 bytes and
 * NUL-terminate. Returns the resulting length. Writes nothing and returns 0
 * when buf is NULL or max is 0. */
size_t utf8_cut_at(char *buf, size_t max);

#endif /* NUTSHELL_CHAT_MSG_H */
