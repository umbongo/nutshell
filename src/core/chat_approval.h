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
    CmdSafetyLevel safety;
    ApprovalStatus status;
} ApprovalEntry;

typedef struct {
    ApprovalEntry entries[APPROVAL_MAX_CMDS];
    int count;
    int auto_approve;           /* 1 = session-level auto-approve active */
    int auto_approve_confirming; /* 1 = waiting for double-click confirm */
    float confirm_start_time;    /* when "are you sure?" was shown */
} ApprovalQueue;

/* Initialize approval queue. */
void chat_approval_init(ApprovalQueue *q);

/* Add a command to the approval queue. Classifies it against the given platform.
 * If permit_write is 0 and the command is write/critical, it's auto-blocked.
 * If auto_approve is active, it's auto-approved.
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

/* Get next approved command that hasn't started executing.
 * Returns entry index, or -1 if none. */
int chat_approval_next_approved(const ApprovalQueue *q);

/* Mark a command as executing. */
void chat_approval_set_executing(ApprovalQueue *q, int index);

/* Mark a command as completed. */
void chat_approval_set_completed(ApprovalQueue *q, int index);

/* Reset the queue (e.g., for new AI response). */
void chat_approval_reset(ApprovalQueue *q);

#endif /* NUTSHELL_CHAT_APPROVAL_H */
