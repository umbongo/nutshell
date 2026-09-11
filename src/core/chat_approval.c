/* src/core/chat_approval.c */
#include "chat_approval.h"
#include <string.h>
#include <ctype.h>

void chat_approval_init(ApprovalQueue *q)
{
    memset(q, 0, sizeof(*q));
    q->policy = cmd_policy_default();
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

/* Defense in depth against C1: a command containing a raw control byte
 * (< 0x20 or 0x7F) is refused here too, even though ai_extract_commands()
 * already drops such payloads -- any other caller that reaches this queue
 * gets the same guarantee. */
static int has_control_char(const char *s)
{
    if (!s) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p == 0x7F) return 1;
    }
    return 0;
}

int chat_approval_add(ApprovalQueue *q, const char *command,
                      CmdPlatform platform)
{
    if (!command || is_whitespace_only(command)) return -1;
    if (has_control_char(command)) return -1;
    if (q->count >= APPROVAL_MAX_CMDS) return -1;

    int idx = q->count;
    ApprovalEntry *e = &q->entries[idx];
    size_t len = strlen(command);
    if (len >= sizeof(e->command)) len = sizeof(e->command) - 1;
    memcpy(e->command, command, len);
    e->command[len] = '\0';
    e->safety = cmd_classify(command, platform);
    e->safety_mask = cmd_classify_mask(command, platform);

    if (cmd_policy_blocks(q->policy, e->safety)) {
        e->status = APPROVE_BLOCKED;
    } else if (cmd_policy_runs_unattended(q->policy, e->safety_mask)) {
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

int chat_approval_all_decided(const ApprovalQueue *q)
{
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_PENDING)
            return 0;
    }
    return 1;
}

int chat_approval_needs_user(const ApprovalQueue *q)
{
    int any_blocked = 0, any_approved = 0;
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_PENDING)
            return 1;
        if (q->entries[i].status == APPROVE_BLOCKED)
            any_blocked = 1;
        if (q->entries[i].status == APPROVE_APPROVED)
            any_approved = 1;
    }
    if (any_blocked && !any_approved) return 1;
    return 0;
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
        if (q->entries[i].status == APPROVE_BLOCKED &&
            !cmd_policy_blocks(q->policy, q->entries[i].safety)) {
            q->entries[i].status = APPROVE_PENDING;
            n++;
        }
    }
    return n;
}

int chat_approval_block_disallowed(ApprovalQueue *q)
{
    int n = 0;
    for (int i = 0; i < q->count; i++) {
        if (q->entries[i].status == APPROVE_PENDING &&
            cmd_policy_blocks(q->policy, q->entries[i].safety)) {
            q->entries[i].status = APPROVE_BLOCKED;
            n++;
        }
    }
    return n;
}

void chat_approval_reset(ApprovalQueue *q)
{
    CmdPolicy saved = q->policy;
    memset(q->entries, 0, sizeof(q->entries));
    q->count = 0;
    q->policy = saved;
}
