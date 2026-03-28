/* src/core/chat_approval.c */
#include "chat_approval.h"
#include <string.h>
#include <ctype.h>

void chat_approval_init(ApprovalQueue *q)
{
    memset(q, 0, sizeof(*q));
}

static int is_whitespace_only(const char *s)
{
    if (!s) return 1;
    while (*s) {
        if (!isspace((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

int chat_approval_add(ApprovalQueue *q, const char *command,
                      CmdPlatform platform, int permit_write)
{
    if (!command || is_whitespace_only(command)) return -1;
    if (q->count >= APPROVAL_MAX_CMDS) return -1;

    int idx = q->count;
    ApprovalEntry *e = &q->entries[idx];
    size_t len = strlen(command);
    if (len >= sizeof(e->command)) len = sizeof(e->command) - 1;
    memcpy(e->command, command, len);
    e->command[len] = '\0';
    e->safety = cmd_classify(command, platform);

    if (e->safety > CMD_SAFE && !permit_write) {
        e->status = APPROVE_BLOCKED;
    } else if (q->auto_approve) {
        e->status = APPROVE_APPROVED;
    } else {
        e->status = APPROVE_PENDING;
    }

    q->count++;
    return idx;
}

int chat_approval_approve(ApprovalQueue *q, int index)
{
    if (index < 0 || index >= q->count) return -1;
    if (q->entries[index].status != APPROVE_PENDING) return -1;
    q->entries[index].status = APPROVE_APPROVED;
    return 0;
}

int chat_approval_deny(ApprovalQueue *q, int index)
{
    if (index < 0 || index >= q->count) return -1;
    if (q->entries[index].status != APPROVE_PENDING) return -1;
    q->entries[index].status = APPROVE_DENIED;
    return 0;
}

int chat_approval_approve_all(ApprovalQueue *q)
{
    int n = 0;
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_PENDING) {
            q->entries[i].status = APPROVE_APPROVED;
            n++;
        }
    }
    return n;
}

int chat_approval_auto_approve_click(ApprovalQueue *q, float current_time,
                                      float confirm_timeout)
{
    if (!q->auto_approve_confirming) {
        q->auto_approve_confirming = 1;
        q->confirm_start_time = current_time;
        return 0;
    }

    float elapsed = current_time - q->confirm_start_time;
    if (elapsed > confirm_timeout) {
        q->auto_approve_confirming = 0;
        return -1;
    }

    q->auto_approve = 1;
    q->auto_approve_confirming = 0;
    return 1;
}

void chat_approval_revoke_auto(ApprovalQueue *q)
{
    q->auto_approve = 0;
    q->auto_approve_confirming = 0;
}

int chat_approval_all_decided(const ApprovalQueue *q)
{
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_PENDING)
            return 0;
    }
    return 1;
}

int chat_approval_next_approved(const ApprovalQueue *q)
{
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_APPROVED)
            return i;
    }
    return -1;
}

void chat_approval_set_executing(ApprovalQueue *q, int index)
{
    if (index >= 0 && index < q->count)
        q->entries[index].status = APPROVE_EXECUTING;
}

void chat_approval_set_completed(ApprovalQueue *q, int index)
{
    if (index >= 0 && index < q->count)
        q->entries[index].status = APPROVE_COMPLETED;
}

int chat_approval_unblock_all(ApprovalQueue *q)
{
    int n = 0;
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_BLOCKED) {
            q->entries[i].status = APPROVE_PENDING;
            n++;
        }
    }
    return n;
}

void chat_approval_reset(ApprovalQueue *q)
{
    int saved_auto = q->auto_approve;
    memset(q->entries, 0, sizeof(q->entries));
    q->count = 0;
    q->auto_approve = saved_auto;
    q->auto_approve_confirming = 0;
}
