/* src/core/chat_msg.c */
#include "chat_msg.h"
#include "secure_zero.h"
#include "tool_decision.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMD_MAX_LEN 1023

void chat_msg_list_init(ChatMsgList *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->next_id = 1;
}

ChatMsgItem *chat_msg_append(ChatMsgList *list, ChatItemType type, const char *text)
{
    ChatMsgItem *item = calloc(1, sizeof(*item));
    if (!item) return NULL;

    item->type = type;
    item->id = list->next_id++;
    item->dirty = 1;
    item->measured_height = 0;

    /* Copy text (treat NULL as empty) */
    const char *src = text ? text : "";
    size_t len = strlen(src);
    item->text = malloc(len + 1);
    if (!item->text) { free(item); return NULL; }
    memcpy(item->text, src, len + 1);
    item->text_len = len;
    text_runs_init(&item->runs);
    text_runs_init(&item->pinned_runs);
    item->painted_height = 0;

    /* Type-specific defaults */
    if (type == CHAT_ITEM_AI_TEXT) {
        item->u.ai.thinking_text = NULL;
        item->u.ai.thinking_collapsed = 1;
        item->u.ai.thinking_elapsed = 0.0f;
        item->u.ai.thinking_complete = 0;
        item->u.ai.thinking_scroll_y = 0;
        item->u.ai.thinking_autoscroll = 1;
        item->u.ai.think_cache_len = -1;
    } else if (type == CHAT_ITEM_COMMAND) {
        item->u.cmd.command = NULL;
        item->u.cmd.note = NULL;
        item->u.cmd.reason[0] = '\0';
        item->u.cmd.safety = CMD_READ;
        item->u.cmd.approved = -1;
        item->u.cmd.blocked = 0;
        item->u.cmd.settled = 0;
        item->u.cmd.batch = 0;
        item->u.cmd.run = CHAT_RUN_NONE;
        item->u.cmd.container_scroll = 0;
        item->u.cmd.tool = 0;
        item->u.cmd.tool_outcome = (int)TOOL_DECISION_NONE;
        item->u.cmd.fold = CHAT_FOLD_NONE;
        item->u.cmd.fold_hidden = 0;
        item->u.cmd.fold_open = 0;
    }

    /* Link into list */
    item->prev = list->tail;
    item->next = NULL;
    if (list->tail)
        list->tail->next = item;
    else
        list->head = item;
    list->tail = item;
    list->count++;

    return item;
}

static void free_item(ChatMsgItem *item)
{
    if (!item) return;
    text_runs_free(&item->runs);
    text_runs_free(&item->pinned_runs);
    /* Zero sensitive content before free (may contain API reasoning, passwords) */
    if (item->text) {
        secure_zero(item->text, item->text_len);
        free(item->text);
    }
    if (item->type == CHAT_ITEM_AI_TEXT && item->u.ai.thinking_text) {
        size_t tlen = strlen(item->u.ai.thinking_text);
        secure_zero(item->u.ai.thinking_text, tlen);
        free(item->u.ai.thinking_text);
    }
    if (item->type == CHAT_ITEM_COMMAND && item->u.cmd.command) {
        size_t clen = strlen(item->u.cmd.command);
        secure_zero(item->u.cmd.command, clen);
        free(item->u.cmd.command);
    }
    if (item->type == CHAT_ITEM_COMMAND && item->u.cmd.note) {
        size_t nlen = strlen(item->u.cmd.note);
        secure_zero(item->u.cmd.note, nlen);
        free(item->u.cmd.note);
    }
    free(item);
}

void chat_msg_remove(ChatMsgList *list, ChatMsgItem *item)
{
    if (!list || !item) return;
    if (item->prev)
        item->prev->next = item->next;
    else
        list->head = item->next;
    if (item->next)
        item->next->prev = item->prev;
    else
        list->tail = item->prev;
    list->count--;
    free_item(item);
}

void chat_msg_list_clear(ChatMsgList *list)
{
    ChatMsgItem *cur = list->head;
    while (cur) {
        ChatMsgItem *next = cur->next;
        free_item(cur);
        cur = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

int chat_msg_set_text(ChatMsgItem *item, const char *text)
{
    if (!item) return -1;
    const char *src = text ? text : "";
    size_t len = strlen(src);
    char *new_text = malloc(len + 1);
    if (!new_text) return -1;
    memcpy(new_text, src, len + 1);
    if (item->text) {
        memset(item->text, 0, item->text_len);
        free(item->text);
    }
    item->text = new_text;
    item->text_len = len;
    item->dirty = 1;
    return 0;
}

int chat_msg_set_command(ChatMsgItem *item, const char *command,
                         CmdSafetyLevel safety, int blocked)
{
    if (!item || item->type != CHAT_ITEM_COMMAND) return -1;
    if (!command) return -1;
    size_t len = strlen(command);
    if (len > CMD_MAX_LEN) return -1; /* reject, don't truncate */

    char *new_cmd = malloc(len + 1);
    if (!new_cmd) return -1;
    memcpy(new_cmd, command, len + 1);

    if (item->u.cmd.command) {
        size_t old_len = strlen(item->u.cmd.command);
        memset(item->u.cmd.command, 0, old_len);
        free(item->u.cmd.command);
    }
    item->u.cmd.command = new_cmd;
    item->u.cmd.reason[0] = '\0';    /* a reused item keeps no stale reason */
    item->u.cmd.safety = safety;
    item->u.cmd.approved = -1;
    item->u.cmd.blocked = blocked;
    item->u.cmd.run = CHAT_RUN_NONE;
    /* Approval card v2: pending, non-held rows start checked (the user
     * unchecks the ones they don't want before "Run N selected"); a held
     * (blocked) row's checkbox paints disabled regardless, so leave it
     * unchecked. */
    item->u.cmd.selected = blocked ? 0 : 1;
    item->dirty = 1;
    return 0;
}

int chat_msg_set_note(ChatMsgItem *item, const char *note)
{
    if (!item || item->type != CHAT_ITEM_COMMAND) return -1;
    char *copy = NULL;
    if (note) {
        size_t len = strlen(note);
        copy = malloc(len + 1);
        if (!copy) return -1;
        memcpy(copy, note, len + 1);
    }
    if (item->u.cmd.note) {
        secure_zero(item->u.cmd.note, strlen(item->u.cmd.note));
        free(item->u.cmd.note);
    }
    item->u.cmd.note = copy;
    return 0;
}

void chat_msg_set_reason(ChatMsgItem *item, const char *reason)
{
    if (!item || item->type != CHAT_ITEM_COMMAND) return;
    if (!reason) reason = "";
    size_t len = strlen(reason);
    size_t max = sizeof(item->u.cmd.reason) - 1;
    if (len > max) {
        /* reason[max] is the first byte that does not fit: back up while it
         * is a UTF-8 continuation byte so no character is split. */
        len = max;
        while (len > 0 && ((unsigned char)reason[len] & 0xC0) == 0x80) len--;
    }
    memcpy(item->u.cmd.reason, reason, len);
    item->u.cmd.reason[len] = '\0';
}

void chat_msg_set_batch(ChatMsgItem *item, int batch)
{
    if (!item || item->type != CHAT_ITEM_COMMAND) return;
    item->u.cmd.batch = batch;
}

void chat_msg_set_quoted(ChatMsgItem *item, int quoted)
{
    if (!item || item->type != CHAT_ITEM_COMMAND) return;
    item->u.cmd.quoted = quoted ? 1 : 0;
}

void chat_msg_set_tool(ChatMsgItem *item, int tool)
{
    if (!item || item->type != CHAT_ITEM_COMMAND) return;
    item->u.cmd.tool = tool ? 1 : 0;
    if (tool) item->u.cmd.tool_outcome = (int)TOOL_DECISION_PENDING;
}

void chat_msg_set_tool_outcome(ChatMsgItem *item, int outcome)
{
    if (!item || item->type != CHAT_ITEM_COMMAND) return;
    item->u.cmd.tool_outcome = outcome;
}

void chat_msg_apply_ceiling(ChatMsgItem *it, int prev_allowed, int next_allowed)
{
    if (!it || it->type != CHAT_ITEM_COMMAND || it->u.cmd.tool) return;
    if (next_allowed > prev_allowed) {
        if (it->u.cmd.blocked && !it->u.cmd.quoted &&
            (int)it->u.cmd.safety <= next_allowed) {
            it->u.cmd.blocked = 0;
            it->u.cmd.approved = -1;
            it->u.cmd.selected = 1;  /* no longer held -- starts checked */
        }
    } else if (next_allowed < prev_allowed) {
        if (!it->u.cmd.settled && it->u.cmd.approved == -1 &&
            (int)it->u.cmd.safety > next_allowed) {
            it->u.cmd.blocked = 1;
        }
    }
}

int chat_msg_set_thinking(ChatMsgItem *item, const char *thinking_text)
{
    if (!item || item->type != CHAT_ITEM_AI_TEXT) return -1;
    const char *src = thinking_text ? thinking_text : "";
    size_t len = strlen(src);
    char *new_text = malloc(len + 1);
    if (!new_text) return -1;
    memcpy(new_text, src, len + 1);

    if (item->u.ai.thinking_text) {
        size_t old_len = strlen(item->u.ai.thinking_text);
        memset(item->u.ai.thinking_text, 0, old_len);
        free(item->u.ai.thinking_text);
    }
    item->u.ai.thinking_text = new_text;
    item->dirty = 1;
    return 0;
}

int chat_msg_count(const ChatMsgList *list)
{
    return list ? list->count : 0;
}

ChatMsgItem *chat_msg_find(const ChatMsgList *list, int id)
{
    for (ChatMsgItem *it = list ? list->head : NULL; it; it = it->next)
        if (it->id == id) return it;
    return NULL;
}

int chat_msg_batch_index(const ChatMsgList *list, const ChatMsgItem *item)
{
    if (!list || !item) return -1;
    if (item->type != CHAT_ITEM_COMMAND || item->u.cmd.settled) return -1;

    int idx = 0;
    for (ChatMsgItem *it = list->head; it; it = it->next) {
        if (it->type != CHAT_ITEM_COMMAND || it->u.cmd.settled) continue;
        if (it->u.cmd.batch != item->u.cmd.batch) continue;
        if (it == item) return idx;
        idx++;
    }
    return -1;
}

ChatMsgItem *chat_msg_batch_first(const ChatMsgList *list, int batch)
{
    if (!list) return NULL;
    for (ChatMsgItem *it = list->head; it; it = it->next) {
        if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled &&
            it->u.cmd.batch == batch)
            return it;
    }
    return NULL;
}

int chat_msg_batch_settle(ChatMsgList *list, int batch)
{
    if (!list) return 0;
    int n = 0;
    for (ChatMsgItem *it = list->head; it; it = it->next) {
        if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled &&
            it->u.cmd.batch == batch) {
            it->u.cmd.settled = 1;
            it->dirty = 1;
            n++;
        }
    }
    return n;
}

const char *chat_cmd_label(int approved, int blocked, int quoted, int run,
                           ChatCmdIntent *intent)
{
    ChatCmdIntent i;
    const char *label;

    if (quoted) {
        label = "quoted, not run";
        i = CHAT_CMD_INTENT_DIM;
    } else if (blocked) {
        label = "held";
        i = CHAT_CMD_INTENT_WARNING;
    } else if (approved == 0) {
        label = "denied";
        i = CHAT_CMD_INTENT_DIM;
    } else if (approved == -1) {
        label = "skipped";
        i = CHAT_CMD_INTENT_DIM;
    } else if (run == CHAT_RUN_RUNNING) {
        label = "running";
        i = CHAT_CMD_INTENT_INFO;
    } else if (run == CHAT_RUN_DONE) {
        label = "ran";
        i = CHAT_CMD_INTENT_SUCCESS;
    } else if (run == CHAT_RUN_ABORTED) {
        label = "not run";
        i = CHAT_CMD_INTENT_WARNING;
    } else if (run == CHAT_RUN_UNFINISHED) {
        label = "unfinished";
        i = CHAT_CMD_INTENT_WARNING;
    } else {
        /* approved == 1, CHAT_RUN_NONE, or any unexpected run value */
        label = "queued";
        i = CHAT_CMD_INTENT_DIM;
    }

    if (intent) *intent = i;
    return label;
}

int chat_card_header_text(const char *noun, int n, int held, int quoted,
                          char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    size_t pos = 0;
    int w = snprintf(buf, cap, "%d %s%s", n, noun ? noun : "command",
                     n == 1 ? "" : "s");
    if (w < 0 || (size_t)w >= cap) { buf[0] = '\0'; return 0; }
    pos = (size_t)w;
    if (held > 0) {
        w = snprintf(buf + pos, cap - pos, " \xC2\xB7 %d held", held);
        if (w < 0 || (size_t)w >= cap - pos) { buf[0] = '\0'; return 0; }
        pos += (size_t)w;
    }
    if (quoted > 0) {
        w = snprintf(buf + pos, cap - pos, " \xC2\xB7 %d quoted", quoted);
        if (w < 0 || (size_t)w >= cap - pos) { buf[0] = '\0'; return 0; }
        pos += (size_t)w;
    }
    return (int)pos;
}

int chat_msg_batch_sync_run(ChatMsgList *list, int batch_id,
                            const ApprovalQueue *q, int batch_ending)
{
    if (!list || !q) return 0;

    int n = 0;
    int idx = 0;
    for (ChatMsgItem *it = list->head; it; it = it->next) {
        if (it->type != CHAT_ITEM_COMMAND || it->u.cmd.batch != batch_id)
            continue;

        if (idx >= q->count) {
            idx++;
            continue;
        }

        const ApprovalEntry *e = &q->entries[idx];
        int run;
        switch (e->status) {
        case APPROVE_EXECUTING:
            run = batch_ending ? CHAT_RUN_UNFINISHED : CHAT_RUN_RUNNING;
            break;
        case APPROVE_COMPLETED:
            run = CHAT_RUN_DONE;
            break;
        case APPROVE_APPROVED:
            run = batch_ending ? CHAT_RUN_ABORTED : CHAT_RUN_NONE;
            break;
        default:
            run = CHAT_RUN_NONE;
            break;
        }

        if (it->u.cmd.run != run) {
            it->u.cmd.run = run;
            it->dirty = 1;
        }
        n++;
        idx++;
    }
    return n;
}

int chat_msg_batch_counts(const ChatMsgList *list, int batch_id,
                          ChatBatchCounts *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!list) return 0;

    for (const ChatMsgItem *it = list->head; it; it = it->next) {
        if (it->type != CHAT_ITEM_COMMAND || it->u.cmd.batch != batch_id)
            continue;
        out->n++;
        if (it->u.cmd.settled) out->decided++;

        /* chat_cmd_label()'s priority order, except that a row still on the
         * card has no run state yet: it awaits, or is held. */
        if (it->u.cmd.quoted)               out->quoted++;
        else if (it->u.cmd.blocked)         out->held++;
        else if (!it->u.cmd.settled)        out->awaiting++;
        else if (it->u.cmd.approved == 0)   out->denied++;
        else if (it->u.cmd.approved == -1)  out->skipped++;
        else if (it->u.cmd.run == CHAT_RUN_RUNNING)    out->running++;
        else if (it->u.cmd.run == CHAT_RUN_DONE)       out->ran++;
        else if (it->u.cmd.run == CHAT_RUN_ABORTED)    out->not_run++;
        else if (it->u.cmd.run == CHAT_RUN_UNFINISHED) out->unfinished++;
        else                                           out->queued++;
    }
    return out->n;
}

/* The non-zero parts as "<count> <word>" joined by ", ", appended to buf at
 * *pos in the fixed order. 0 when it does not fit in cap. */
static int append_count_parts(const ChatBatchCounts *c, char *buf, size_t cap,
                              size_t *pos)
{
    const struct { int count; const char *word; } parts[] = {
        { c->ran,        "ran" },
        { c->running,    "running" },
        { c->unfinished, "unfinished" },
        { c->queued,     "queued" },
        { c->awaiting,   "awaiting approval" },
        { c->held,       "held" },
        { c->not_run,    "not run" },
        { c->denied,     "denied" },
        { c->skipped,    "skipped" },
        { c->quoted,     "quoted" },
    };
    int first = 1;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        if (parts[i].count <= 0) continue;
        int w = snprintf(buf + *pos, cap - *pos, "%s%d %s", first ? "" : ", ",
                         parts[i].count, parts[i].word);
        if (w < 0 || (size_t)w >= cap - *pos) return 0;
        *pos += (size_t)w;
        first = 0;
    }
    return 1;
}

int chat_batch_summary_text(const ChatBatchCounts *c, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (!c || c->n < 2 || c->decided < 1) return 0;

    int w = snprintf(buf, cap, "%d commands: ", c->n);
    if (w < 0 || (size_t)w >= cap) { buf[0] = '\0'; return 0; }
    size_t pos = (size_t)w;
    if (!append_count_parts(c, buf, cap, &pos)) { buf[0] = '\0'; return 0; }
    return (int)pos;
}

int chat_batch_is_final(const ChatBatchCounts *c)
{
    if (!c) return 0;
    return c->n >= 1 && c->decided == c->n && c->running == 0 &&
           c->queued == 0 && c->awaiting == 0;
}

int chat_batch_fold_text(const ChatBatchCounts *c, char *label, size_t lcap,
                         char *summary, size_t scap, int *warn)
{
    if (label && lcap > 0) label[0] = '\0';
    if (summary && scap > 0) summary[0] = '\0';
    if (warn) *warn = 0;
    if (!label || lcap == 0 || !summary || scap == 0) return 0;
    if (!chat_batch_is_final(c)) return 0;

    int w = snprintf(label, lcap, c->n == 1 ? "%d command" : "%d commands",
                     c->n);
    if (w < 0 || (size_t)w >= lcap) { label[0] = '\0'; return 0; }
    w = snprintf(summary, scap, "\xC2\xB7 ");
    if (w < 0 || (size_t)w >= scap) {
        label[0] = '\0'; summary[0] = '\0';
        return 0;
    }
    size_t pos = (size_t)w;
    if (!append_count_parts(c, summary, scap, &pos)) {
        label[0] = '\0'; summary[0] = '\0';
        return 0;
    }
    if (warn) *warn = (c->unfinished > 0 || c->not_run > 0 || c->held > 0);
    return 1;
}

/* A row that can be part of a fold group: a settled command row that is not
 * a tool call, of a real batch. */
static int fold_candidate(const ChatMsgItem *it)
{
    return it && it->type == CHAT_ITEM_COMMAND && it->u.cmd.settled &&
           !it->u.cmd.tool && it->u.cmd.batch != 0;
}

int chat_msg_fold_refresh(ChatMsgList *list)
{
    if (!list) return 0;
    int changed = 0;
    ChatMsgItem *it = list->head;
    while (it) {
        if (it->type != CHAT_ITEM_COMMAND) { it = it->next; continue; }
        it->u.cmd.fold_changed = 0;

        int want_fold = CHAT_FOLD_NONE;
        int want_hidden = 0;
        ChatMsgItem *end = it->next;   /* one past the run, or past it */
        if (fold_candidate(it)) {
            const int batch = it->u.cmd.batch;
            int run = 1;
            while (end && fold_candidate(end) && end->u.cmd.batch == batch) {
                run++;
                end = end->next;
            }
            ChatBatchCounts c;
            chat_msg_batch_counts(list, batch, &c);
            /* The whole batch must be this one run: otherwise rows of it sit
             * elsewhere (apart, or as a tool row) and the counts would not
             * describe the group. */
            if (chat_batch_is_final(&c) && c.n == run) {
                want_fold = it->u.cmd.fold_open ? CHAT_FOLD_OPEN
                                                : CHAT_FOLD_CLOSED;
                want_hidden = want_fold == CHAT_FOLD_CLOSED;
            }
            int group_changed = 0;
            for (ChatMsgItem *m = it; m != end; m = m->next) {
                m->u.cmd.fold_changed = 0;
                int f = (m == it) ? want_fold : CHAT_FOLD_NONE;
                int h = (m == it) ? 0 : want_hidden;
                if (m->u.cmd.fold != f || m->u.cmd.fold_hidden != h) {
                    m->u.cmd.fold = f;
                    m->u.cmd.fold_hidden = h;
                    m->u.cmd.fold_changed = 1;
                    changed++;
                    group_changed = 1;
                }
            }
            /* A change anywhere in the group changes its rows' heights (head
             * and hidden rows) and where the summary line sits (the last
             * row), so the layout remeasures all of them -- and only them. */
            if (group_changed)
                for (ChatMsgItem *m = it; m != end; m = m->next)
                    m->dirty = 1;
            it = end;
            continue;
        }
        if (it->u.cmd.fold != CHAT_FOLD_NONE || it->u.cmd.fold_hidden) {
            it->u.cmd.fold = CHAT_FOLD_NONE;
            it->u.cmd.fold_hidden = 0;
            it->u.cmd.fold_changed = 1;
            it->dirty = 1;
            changed++;
        }
        it = it->next;
    }
    return changed;
}

int chat_msg_fold_toggle(ChatMsgItem *head)
{
    if (!head || head->type != CHAT_ITEM_COMMAND ||
        head->u.cmd.fold == CHAT_FOLD_NONE)
        return 0;
    head->u.cmd.fold_open = !head->u.cmd.fold_open;
    return head->u.cmd.fold_open;
}

size_t utf8_cut_at(char *buf, size_t max)
{
    if (!buf || max == 0) return 0;
    size_t len = strlen(buf);
    if (len <= max - 1) return len;
    size_t cut = max - 1;
    /* buf[cut] is the first byte that does not fit; while it is a UTF-8
     * continuation byte the character it belongs to straddles the limit. */
    while (cut > 0 && ((unsigned char)buf[cut] & 0xC0) == 0x80) cut--;
    buf[cut] = '\0';
    return cut;
}

void chat_msg_append_batch_items(ChatMsgList *list, const CmdBatch *batch)
{
    if (!list || !batch) return;
    const int settled = batch->settled;
    for (int i = 0; i < batch->q.count; i++) {
        const ApprovalEntry *e = &batch->q.entries[i];
        ChatMsgItem *item = chat_msg_append(list, CHAT_ITEM_COMMAND, "");
        if (!item) continue;
        chat_msg_set_command(item, e->command, e->safety,
                             e->status == APPROVE_BLOCKED);
        chat_msg_set_quoted(item, e->quoted);
        chat_msg_set_reason(item, e->reason);
        chat_msg_set_batch(item, batch->id);
        chat_msg_set_note(item, e->note);
        /* A tool call's row (outcome PENDING until its slot is answered). */
        if (e->tool) chat_msg_set_tool(item, 1);
        /* Approved/denied as the click left them (APPROVED, EXECUTING and
         * COMPLETED read approved, DENIED denied), whether or not the card
         * is still up. */
        switch (e->status) {
        case APPROVE_APPROVED:
        case APPROVE_EXECUTING:
        case APPROVE_COMPLETED:
            item->u.cmd.approved = 1;
            break;
        case APPROVE_DENIED:
            item->u.cmd.approved = 0;
            break;
        case APPROVE_PENDING:
        case APPROVE_BLOCKED:
        default:
            break;
        }
        item->u.cmd.settled = settled;
    }

    /* Run state comes from the same queue, so a rebuilt display (session
     * switch, --ui-demo=executing) shows "running"/"ran" exactly where the
     * live panel does instead of falling back to "queued". The batch is
     * alive here by definition -- a rebuild only ever replays batches still
     * in the set -- hence batch_ending = 0. */
    chat_msg_batch_sync_run(list, batch->id, &batch->q, 0);
}
