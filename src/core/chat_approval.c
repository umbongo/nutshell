/* src/core/chat_approval.c */
#include "chat_approval.h"
#include "paste_filter.h"
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

int chat_approval_add(ApprovalQueue *q, const char *command,
                      CmdPlatform platform)
{
    if (!command || is_whitespace_only(command)) return -1;
    /* Defense in depth: a command containing a raw control byte, a
     * UTF-8-encoded C1 control, or a bidi override/isolate character is
     * refused here too, even though ai_extract_commands() already drops
     * such payloads -- any other caller that reaches this queue gets the
     * same guarantee. Shared with paste_filter_controls() so there is one
     * place that defines "control-ish byte a command must not contain". */
    if (text_has_unsafe_command_char(command)) return -1;
    if (q->count >= APPROVAL_MAX_CMDS) return -1;

    int idx = q->count;
    ApprovalEntry *e = &q->entries[idx];
    size_t len = strlen(command);
    /* Reject outright rather than clamp -- a clamped command would run
     * with its tail silently cut off, which is worse than not running at
     * all (spec: 2026-09-23-command-dispatch-states-design.md section 1).
     * Nothing has been written into *e or q->count yet, so this leaves
     * the queue exactly as it was. */
    if (len >= sizeof(e->command)) return -1;
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
