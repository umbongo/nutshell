/* src/core/chat_approval.h */
#ifndef NUTSHELL_CHAT_APPROVAL_H
#define NUTSHELL_CHAT_APPROVAL_H

#include "cmd_classify.h"
#include "cmd_policy.h"

#define APPROVAL_MAX_CMDS 16

typedef enum {
    APPROVE_PENDING,
    APPROVE_APPROVED,
    APPROVE_DENIED,
    APPROVE_BLOCKED,
    APPROVE_EXECUTING,
    APPROVE_COMPLETED
} ApprovalStatus;

typedef struct {
    char command[1024];
    CmdSafetyLevel safety;      /* the worst category across the command's segments --
                                  * what the approval card's chip shows */
    unsigned safety_mask;       /* the SET of categories across the command's segments
                                  * (CMD_MASK_OF(level) per segment) -- what the
                                  * unattended gate tests. Not the same information
                                  * as `safety`: a {CMD_UNKNOWN, CMD_WRITE} pipeline has
                                  * safety == CMD_WRITE, so a gate that only looked at
                                  * `safety` could not tell it from a plain write. */
    ApprovalStatus status;
} ApprovalEntry;

typedef struct {
    ApprovalEntry entries[APPROVAL_MAX_CMDS];
    int count;
    CmdPolicy policy;   /* the session's one policy: the `allowed` ceiling and
                          * the `unattended` marker (src/core/cmd_policy.h). */
} ApprovalQueue;

/* Initialize approval queue. */
void chat_approval_init(ApprovalQueue *q);

/* Add a command to the approval queue. Classifies it against the given
 * platform, filling both `safety` (the worst category) and `safety_mask`
 * (the set of categories across its segments), then decides against the
 * queue's own policy, in this order:
 *
 *   worst category above policy.allowed            -> APPROVE_BLOCKED
 *   every category at or below policy.unattended   -> APPROVE_APPROVED
 *   otherwise                                      -> APPROVE_PENDING
 *
 * The second test is a set test over safety_mask, not a threshold over
 * `safety` -- see cmd_policy_runs_unattended().
 * Returns the entry index, or -1 if queue is full. */
int chat_approval_add(ApprovalQueue *q, const char *command,
                      CmdPlatform platform);

/* Approve a specific command by index. Returns 0 on success. */
int chat_approval_approve(ApprovalQueue *q, int index);

/* Deny a specific command by index. Returns 0 on success. */
int chat_approval_deny(ApprovalQueue *q, int index);

/* Approve all pending commands. Returns number approved. */
int chat_approval_approve_all(ApprovalQueue *q);

/* Check if all commands have been decided (no PENDING). */
int chat_approval_all_decided(const ApprovalQueue *q);

/* Does the approval card need to stay up for the user to act on?
 * Returns 1 when any entry is still PENDING, or when at least one entry
 * is BLOCKED and nothing in the queue is APPROVED (so nothing would run
 * -- the card must stay up so the user can raise the policy ceiling
 * and run, or Deny all). Returns 0 for an empty queue, or once every entry
 * is settled into APPROVED/DENIED (with at least one APPROVED alongside
 * any BLOCKED entries -- the auto-approve case, where something runs). */
int chat_approval_needs_user(const ApprovalQueue *q);

/* Get next approved command that hasn't started executing.
 * Returns entry index, or -1 if none. */
int chat_approval_next_approved(const ApprovalQueue *q);

/* Mark a command as executing. */
void chat_approval_set_executing(ApprovalQueue *q, int index);

/* Mark a command as completed. */
void chat_approval_set_completed(ApprovalQueue *q, int index);

/* Unblock every blocked command the policy now allows (the ceiling was
 * raised). Changes BLOCKED -> PENDING for entries whose worst category is
 * at or below policy.allowed, and LEAVES the rest blocked -- raising the
 * ceiling from Read to Write must not release a CRITICAL row. Returns the
 * number unblocked. The exact inverse of chat_approval_block_disallowed(). */
int chat_approval_unblock_all(ApprovalQueue *q);

/* Block every pending command the policy no longer allows (the ceiling was
 * lowered). Changes PENDING -> BLOCKED for entries whose worst category is
 * above policy.allowed. Returns number blocked. */
int chat_approval_block_disallowed(ApprovalQueue *q);

/* Reset the queue (e.g., for new AI response). Preserves the policy. */
void chat_approval_reset(ApprovalQueue *q);

#endif /* NUTSHELL_CHAT_APPROVAL_H */
