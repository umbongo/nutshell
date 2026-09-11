/* src/core/chat_approval.h */
#ifndef NUTSHELL_CHAT_APPROVAL_H
#define NUTSHELL_CHAT_APPROVAL_H

#include "cmd_classify.h"

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
                                  * auto-approve gate tests. Not the same information
                                  * as `safety`: a {CMD_UNKNOWN, CMD_WRITE} pipeline has
                                  * safety == CMD_WRITE, but must not auto-approve under
                                  * a mode that permits write and not unknown. */
    ApprovalStatus status;
} ApprovalEntry;

/* How far session-level Auto Approve reaches. The five modes are NOT nested:
 * "safe and write" deliberately excludes unknown, so a level is a set of
 * permitted categories, not a threshold. auto_approve_mask() gives that set;
 * a command auto-approves when every category in its safety_mask is in it:
 *   (entry->safety_mask & ~auto_approve_mask(level)) == 0
 */
typedef enum {
    AUTO_APPROVE_SAFE               = 0,  /* safe only */
    AUTO_APPROVE_SAFE_UNKNOWN       = 1,  /* safe + unknown */
    AUTO_APPROVE_SAFE_WRITE         = 2,  /* safe + write */
    AUTO_APPROVE_SAFE_UNKNOWN_WRITE = 3,  /* safe + unknown + write */
    AUTO_APPROVE_ALL                = 4   /* safe + unknown + write + critical */
} AutoApproveLevel;

/* The set of CmdSafetyLevel categories permitted to auto-approve at `level`,
 * as a bitmask of CMD_MASK_OF(level) bits. CMD_SAFE is in every mode. An
 * out-of-range level is treated as AUTO_APPROVE_SAFE. */
unsigned auto_approve_mask(AutoApproveLevel level);

/* Config-token / UI-label mappings for the auto-approve mode, over the range
 * 0..5 (0 = off, 1..5 = AutoApproveLevel 0..4, i.e. "on" at that level) --
 * the convention ai_chat.c already uses for its "level0to5" seed value.
 * Mappings live here (rather than in the UI or config layer) so they are
 * natively testable. */

/* Token -> mode. Recognises "off", "safe", "safe+unknown", "safe+write",
 * "safe+unknown+write", "all". Returns 0 (off) for NULL or an unrecognised
 * token. */
int         auto_approve_mode_from_name(const char *name);

/* Mode -> stable config token (the inverse of auto_approve_mode_from_name).
 * An out-of-range mode clamps to 0 ("off"). */
const char *auto_approve_mode_name(int mode0to5);

/* Mode -> UI label ("Off", "Safe only", "Safe + unknown", "Safe + write",
 * "Safe + unknown + write", "All"). An out-of-range mode clamps to 0. */
const char *auto_approve_mode_label(int mode0to5);

typedef struct {
    ApprovalEntry entries[APPROVAL_MAX_CMDS];
    int count;
    int auto_approve;           /* 1 = session-level auto-approve active */
    int auto_approve_confirming; /* 1 = waiting for double-click confirm */
    float confirm_start_time;    /* when "are you sure?" was shown */
    int auto_approve_level;     /* AutoApproveLevel: how far auto_approve reaches */
} ApprovalQueue;

/* Initialize approval queue. */
void chat_approval_init(ApprovalQueue *q);

/* Add a command to the approval queue. Classifies it against the given platform,
 * filling both `safety` (the worst category) and `safety_mask` (the set of
 * categories across its segments).
 * If permit_write is 0 and the command is not provably read-only (safety >
 * CMD_SAFE -- this includes CMD_UNKNOWN), it's auto-blocked.
 * If auto_approve is active, it's auto-approved when every category in the
 * command's safety_mask is permitted at auto_approve_level:
 *   (safety_mask & ~auto_approve_mask(auto_approve_level)) == 0
 * This is a set test, not a threshold -- a {CMD_UNKNOWN, CMD_WRITE} pipeline
 * does not auto-approve under AUTO_APPROVE_SAFE_WRITE even though its
 * `safety` maximum is CMD_WRITE, because CMD_UNKNOWN is not in that mode's set.
 * Returns the entry index, or -1 if queue is full. */
int chat_approval_add(ApprovalQueue *q, const char *command,
                      CmdPlatform platform, int permit_write);

/* Approve a specific command by index. Returns 0 on success. */
int chat_approval_approve(ApprovalQueue *q, int index);

/* Deny a specific command by index. Returns 0 on success. */
int chat_approval_deny(ApprovalQueue *q, int index);

/* Approve all pending commands. Returns number approved. */
int chat_approval_approve_all(ApprovalQueue *q);

/* Start the "allow all session" flow. First call shows confirm prompt.
 * Second call within timeout activates auto-approve.
 * current_time: monotonic seconds. confirm_timeout: seconds (3.0).
 * Returns: 0 = confirming (show "are you sure?"), 1 = activated, -1 = timed out (reset). */
int chat_approval_auto_approve_click(ApprovalQueue *q, float current_time,
                                      float confirm_timeout);

/* Revoke session auto-approve. */
void chat_approval_revoke_auto(ApprovalQueue *q);

/* Check if all commands have been decided (no PENDING). */
int chat_approval_all_decided(const ApprovalQueue *q);

/* Does the approval card need to stay up for the user to act on?
 * Returns 1 when any entry is still PENDING, or when at least one entry
 * is BLOCKED and nothing in the queue is APPROVED (so nothing would run
 * -- the card must stay up so the user can switch to Read + write and
 * run, or Deny all). Returns 0 for an empty queue, or once every entry
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

/* Unblock all blocked commands (permit_write was enabled).
 * Changes BLOCKED → PENDING. Returns number unblocked. */
int chat_approval_unblock_all(ApprovalQueue *q);

/* Block all pending not-provably-safe commands (permit_write was disabled).
 * Changes PENDING → BLOCKED for entries with safety > CMD_SAFE -- write,
 * critical, and unknown alike.
 * Returns number blocked. */
int chat_approval_block_pending_writes(ApprovalQueue *q);

/* Reset the queue (e.g., for new AI response).
 * Preserves auto_approve and auto_approve_level. */
void chat_approval_reset(ApprovalQueue *q);

#endif /* NUTSHELL_CHAT_APPROVAL_H */
