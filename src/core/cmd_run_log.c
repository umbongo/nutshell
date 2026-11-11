/* src/core/cmd_run_log.c */
#include "cmd_run_log.h"
#include "chat_msg.h"
#include <stdlib.h>
#include <string.h>

/* Records and command copies come from here, not xmalloc (which aborts), so
 * an allocation failure is observable and records nothing. */
static void *(*g_alloc)(size_t) = malloc;

void cmd_run_log_set_alloc_for_test(void *(*alloc)(size_t))
{
    g_alloc = alloc ? alloc : malloc;
}

void cmd_run_log_init(CmdRunLog *log)
{
    if (!log) return;
    memset(log, 0, sizeof(*log));
}

uint32_t cmd_run_log_turn_hash(const char *text, size_t len)
{
    uint32_t h = 2166136261u;
    if (!text) return h;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)text[i];
        h *= 16777619u;
    }
    return h;
}

void cmd_run_row_from_entry(const ApprovalEntry *e, int batch_ending,
                            CmdRunRow *out)
{
    if (!e || !out) return;
    memset(out, 0, sizeof(*out));
    out->safety = e->safety;
    out->quoted = e->quoted;
    memcpy(out->reason, e->reason, sizeof(out->reason));
    out->reason[sizeof(out->reason) - 1] = '\0';
    memcpy(out->note, e->note, sizeof(out->note));
    out->note[sizeof(out->note) - 1] = '\0';
    out->run = CHAT_RUN_NONE;

    switch (e->status) {
    case APPROVE_COMPLETED:
        out->approved = 1;
        out->run = CHAT_RUN_DONE;
        break;
    case APPROVE_EXECUTING:
        out->approved = 1;
        out->run = batch_ending ? CHAT_RUN_UNFINISHED : CHAT_RUN_RUNNING;
        break;
    case APPROVE_APPROVED:
        out->approved = 1;
        out->run = batch_ending ? CHAT_RUN_ABORTED : CHAT_RUN_NONE;
        break;
    case APPROVE_DENIED:
        out->approved = 0;
        break;
    case APPROVE_BLOCKED:
        /* Held stays held: the live row carries blocked = 1, which
         * chat_cmd_label() ranks first. */
        out->approved = -1;
        out->blocked = 1;
        break;
    case APPROVE_PENDING:
    default:
        out->approved = -1;
        break;
    }
}

static void free_record(CmdRunRecord *r)
{
    if (!r) return;
    for (int i = 0; i < r->n; i++) free(r->rows[i].command);
    free(r);
}

int cmd_run_log_record(CmdRunLog *log, const CmdBatch *batch, uint32_t turn_hash,
                       int batch_ending)
{
    if (!log || !batch || cmd_batch_is_tool(batch) || batch->q.count <= 0)
        return -1;

    /* Build the whole record first; the log is touched only once it is
     * complete, so a failure part-way loses nothing. */
    CmdRunRecord *r = g_alloc(sizeof(*r));
    if (!r) return -1;
    memset(r, 0, sizeof(*r));
    r->reply_msg = batch->reply_msg;
    r->turn_hash = turn_hash;

    int n = batch->q.count < APPROVAL_MAX_CMDS ? batch->q.count
                                               : APPROVAL_MAX_CMDS;
    for (int i = 0; i < n; i++) {
        const ApprovalEntry *e = &batch->q.entries[i];
        const char *nul = memchr(e->command, '\0', sizeof(e->command));
        size_t len = nul ? (size_t)(nul - e->command) : sizeof(e->command) - 1;
        char *copy = g_alloc(len + 1);
        if (!copy) {
            free_record(r);
            return -1;
        }
        memcpy(copy, e->command, len);
        copy[len] = '\0';
        cmd_run_row_from_entry(e, batch_ending, &r->rows[i]);
        r->rows[i].command = copy;
        r->n = i + 1;
    }

    if (log->count >= CMD_RUN_LOG_MAX) {
        free_record(log->rec[0]);
        for (int i = 1; i < log->count; i++) log->rec[i - 1] = log->rec[i];
        log->count--;
        log->rec[log->count] = NULL;
    }
    log->rec[log->count++] = r;
    return 0;
}

int cmd_run_record_matches(const CmdRunRecord *rec, int msg_index,
                           int is_assistant, const char *content, size_t len)
{
    if (!rec || !is_assistant || rec->reply_msg != msg_index) return 0;
    return cmd_run_log_turn_hash(content, len) == rec->turn_hash;
}

int cmd_run_reply_index(int stored, int msg_count, int last_is_assistant)
{
    if (!stored || msg_count <= 0 || !last_is_assistant) return -1;
    return msg_count - 1;
}

int cmd_run_log_forget(CmdRunLog *log, int reply_msg)
{
    if (!log) return 0;
    int kept = 0;
    for (int i = 0; i < log->count; i++) {
        if (log->rec[i]->reply_msg == reply_msg) {
            free_record(log->rec[i]);
        } else {
            log->rec[kept++] = log->rec[i];
        }
    }
    const int dropped = log->count - kept;
    for (int i = kept; i < log->count; i++) log->rec[i] = NULL;
    log->count = kept;
    return dropped;
}

int cmd_run_batches_unkey(CmdBatchSet *set, int reply_msg)
{
    if (!set || reply_msg < 0) return 0;
    int n = 0;
    for (int i = 0; i < set->count; i++) {
        CmdBatch *b = set->b[i];
        if (b->reply_msg != reply_msg) continue;
        b->reply_msg = -1;
        b->reply_hash = 0;
        n++;
    }
    return n;
}

void cmd_run_log_free(CmdRunLog *log)
{
    if (!log) return;
    for (int i = 0; i < log->count; i++) free_record(log->rec[i]);
    cmd_run_log_init(log);
}
