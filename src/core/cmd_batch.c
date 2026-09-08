/* src/core/cmd_batch.c */
#include "cmd_batch.h"
#include <stdlib.h>
#include <string.h>

void cmd_batch_set_init(CmdBatchSet *set)
{
    if (!set) return;
    memset(set, 0, sizeof(*set));
    set->next_id = 1;
}

void cmd_batch_set_free(CmdBatchSet *set)
{
    if (!set) return;
    for (int i = 0; i < set->count; i++) {
        free(set->b[i]);
        set->b[i] = NULL;
    }
    set->count = 0;
}

CmdBatch *cmd_batch_add(CmdBatchSet *set, const ApprovalQueue *defaults,
                        int *evicted_id)
{
    if (evicted_id) *evicted_id = 0;
    if (!set) return NULL;

    CmdBatch *nb = calloc(1, sizeof(*nb));
    if (!nb) return NULL;

    if (set->count >= CMD_BATCH_MAX) {
        CmdBatch *old = set->b[0];
        if (evicted_id) *evicted_id = old->id;
        free(old);
        for (int i = 1; i < set->count; i++)
            set->b[i - 1] = set->b[i];
        set->count--;
    }

    nb->id = set->next_id++;
    chat_approval_init(&nb->q);
    if (defaults) {
        nb->q.auto_approve = defaults->auto_approve;
        nb->q.auto_approve_level = defaults->auto_approve_level;
    }

    set->b[set->count++] = nb;
    return nb;
}

CmdBatch *cmd_batch_find(CmdBatchSet *set, int id)
{
    if (!set) return NULL;
    for (int i = 0; i < set->count; i++) {
        if (set->b[i] && set->b[i]->id == id)
            return set->b[i];
    }
    return NULL;
}

void cmd_batch_remove(CmdBatchSet *set, int id)
{
    if (!set) return;
    for (int i = 0; i < set->count; i++) {
        if (set->b[i] && set->b[i]->id == id) {
            free(set->b[i]);
            for (int j = i + 1; j < set->count; j++)
                set->b[j - 1] = set->b[j];
            set->count--;
            set->b[set->count] = NULL;
            return;
        }
    }
}

CmdBatch *cmd_batch_first_pending(CmdBatchSet *set)
{
    if (!set) return NULL;
    for (int i = 0; i < set->count; i++) {
        if (set->b[i] && chat_approval_needs_user(&set->b[i]->q))
            return set->b[i];
    }
    return NULL;
}

CmdBatch *cmd_batch_next_runnable(CmdBatchSet *set)
{
    if (!set) return NULL;
    for (int i = 0; i < set->count; i++) {
        if (set->b[i] && chat_approval_next_approved(&set->b[i]->q) >= 0)
            return set->b[i];
    }
    return NULL;
}
