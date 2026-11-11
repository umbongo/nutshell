/* src/core/chat_rebuild.c -- the rules of the rebuilt AI display. */
#include "chat_rebuild.h"
#include "ai_agentic.h"
#include "ai_tool_web_fetch.h"
#include "run_command.h"
#include <stdio.h>
#include <string.h>

#define CALL_ROW_MAX 1280    /* "using tool: run_command - " + a 1023-byte command */

/* Copy `src` into out (cap bytes, NUL included), cutting on a UTF-8 boundary.
 * Returns the length written. */
static size_t copy_cut(char *out, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) n--;
    }
    memcpy(out, src, n);
    out[n] = '\0';
    return n;
}

size_t chat_rebuild_call_text(const AiToolCall *call, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!call) return 0;

    char desc[1100] = "";
    if (strcmp(call->name, "web_fetch") == 0)
        (void)web_fetch_describe(call->input_json, desc,
                                 CHAT_REBUILD_FETCH_DESC_MAX);
    else if (strcmp(call->name, "run_command") == 0)
        (void)runcmd_describe(call->input_json, desc, sizeof(desc));

    char row[CALL_ROW_MAX];
    if (desc[0])
        snprintf(row, sizeof(row), "using tool: %s - %s", call->name, desc);
    else
        snprintf(row, sizeof(row), "using tool: %s", call->name);
    return copy_cut(out, cap, row);
}

int chat_rebuild_is_cut(const char *note, size_t omitted_bytes)
{
    if (omitted_bytes > 0) return 1;
    return note && strstr(note, AI_TOOL_TRUNC_NOTE) != NULL;
}

void chat_rebuild_part_rows(ChatMsgList *list, const char *tool_name,
                            const char *note, const char *content,
                            size_t content_len, size_t omitted_bytes,
                            int covered)
{
    if (!list || covered) return;
    const char *name = tool_name ? tool_name : "";

    if (note && note[0]) {
        char row[AI_TOOL_PART_NOTE_MAX + 1];
        size_t n = copy_cut(row, sizeof(row), note);
        for (size_t k = 0; k < n; k++)
            if (row[k] == '\n' || row[k] == '\r') row[k] = ' ';
        chat_msg_append(list, CHAT_ITEM_STATUS, row);
    }

    if (chat_rebuild_is_cut(note, omitted_bytes)) {
        char warn[256];
        agentic_truncation_warning(name, omitted_bytes, warn, sizeof(warn));
        chat_msg_append(list, CHAT_ITEM_STATUS, warn);
    }

    if (strcmp(name, "run_command") != 0 && content && content_len > 0) {
        char preview[CHAT_REBUILD_PREVIEW_MAX + 1];
        size_t plen = content_len < CHAT_REBUILD_PREVIEW_MAX
                    ? content_len : CHAT_REBUILD_PREVIEW_MAX;
        if (plen < content_len)
            while (plen > 0 && ((unsigned char)content[plen] & 0xC0u) == 0x80u)
                plen--;
        memcpy(preview, content, plen);
        preview[plen] = '\0';
        chat_msg_append(list, CHAT_ITEM_TOOL_RESULT, preview);
    }
}

/* ---- the replay ---- */

/* One answer of a results turn, from a part or from a parts-less message's own
 * fields. */
typedef struct {
    const char *id, *name, *note, *content;
    size_t len, omitted;
} PartView;

static int part_count(const AiMessage *tm)
{
    return tm->parts ? tm->n_parts : 1;
}

static void part_view(const AiMessage *tm, int pi, PartView *v)
{
    if (tm->parts) {
        const AiToolPart *p = &tm->parts[pi];
        v->id = p->tool_call_id;
        v->name = p->tool_name;
        v->note = p->tool_note;
        v->content = p->content;
        v->len = p->content ? p->content_len : 0;
        v->omitted = p->omitted_bytes;
    } else {
        v->id = tm->tool_call_id;
        v->name = tm->tool_name;
        v->note = tm->tool_note;
        v->content = ai_msg_content(tm);
        v->len = tm->content_len;
        v->omitted = 0;
    }
}

static void emit_part(ChatMsgList *list, const AiMessage *tm, int pi, int covered)
{
    PartView v;
    part_view(tm, pi, &v);
    chat_rebuild_part_rows(list, v.name, v.note, v.content, v.len, v.omitted,
                           covered);
}

/* 1 when a part of `tm` answers one of the n calls of `turn` (by id). */
static int part_has_call(const PartView *v, const AiToolCall *calls, int n)
{
    if (!v->id[0]) return 0;
    for (int c = 0; c < n; c++)
        if (strcmp(calls[c].id, v->id) == 0) return 1;
    return 0;
}

/* The replayed rows of a record: its command rows as settled rows. */
static void append_record_rows(ChatMsgList *list, const CmdRunRecord *rec,
                               int batch_id)
{
    for (int i = 0; i < rec->n; i++) {
        const CmdRunRow *r = &rec->rows[i];
        ChatMsgItem *item = chat_msg_append(list, CHAT_ITEM_COMMAND, "");
        if (!item) continue;
        chat_msg_set_command(item, r->command ? r->command : "", r->safety,
                             r->blocked);
        chat_msg_set_quoted(item, r->quoted);
        chat_msg_set_reason(item, r->reason);
        chat_msg_set_batch(item, batch_id);
        chat_msg_set_note(item, r->note);
        item->u.cmd.approved = r->approved;
        item->u.cmd.run = r->run;
        item->u.cmd.settled = 1;
    }
}

/* A reply answered without a card (R64): its status rows. */
static void append_record_status(ChatMsgList *list, const CmdRunRecord *rec)
{
    for (int i = 0; i < rec->n_status && i < CMD_RUN_STATUS_ROWS; i++)
        chat_msg_append(list, CHAT_ITEM_STATUS, rec->status[i]);
}

/* A live non-tool batch keyed to message `idx` with this hash and a row. */
static int live_batch_matches(const CmdBatch *b, int idx, uint32_t hash)
{
    return b && !cmd_batch_is_tool(b) && b->q.count > 0 &&
           b->reply_msg == idx && b->reply_hash == hash;
}

/* What carries the commands of assistant turn `i`: *has_record, a record of
 * the run log matches it (status-only ones included); *card_rows, a record
 * with command rows or a live batch matches it; *live_card, a live batch does.
 * Returns the turn's hash. */
static uint32_t turn_cards(const AiConversation *conv, int i,
                           const CmdRunLog *rlog, const CmdBatchSet *batches,
                           int *has_record, int *card_rows, int *live_card)
{
    const AiMessage *msg = &conv->messages[i];
    const AiToolCall *calls = msg->tool_calls;
    const int n_calls = calls ? msg->n_tool_calls : 0;
    /* A tool turn's hash mixes in its calls' ids, the same function the batch
     * builder keyed the record with. */
    const uint32_t hash = cmd_run_log_reply_hash(
        ai_msg_content(msg), msg->content_len, calls, n_calls);
    *has_record = *card_rows = *live_card = 0;
    for (int ri = 0; rlog && ri < rlog->count; ri++) {
        if (!cmd_run_record_matches_hash(rlog->rec[ri], i, 1, hash)) continue;
        *has_record = 1;
        if (rlog->rec[ri]->n > 0) *card_rows = 1;
    }
    for (int bi = 0; batches && bi < batches->count; bi++)
        if (live_batch_matches(batches->b[bi], i, hash)) *live_card = 1;
    *card_rows = *card_rows || *live_card;
    return hash;
}

/* Mark (earlier[k] = 1) the assistant tool-round turns that precede turn `j`
 * in the same reply: walking back from j - 1 over assistant tool turns and
 * results turns, stopping at anything else (the user's turn, a plain reply). */
static void mark_earlier_rounds(const AiConversation *conv, int j,
                                unsigned char *earlier)
{
    for (int k = j - 1; k >= 1; k--) {
        const AiMessage *m = &conv->messages[k];
        if (m->role == AI_ROLE_ASSISTANT && m->tool_calls && m->n_tool_calls > 0)
            earlier[k] = 1;
        else if (m->role != AI_ROLE_TOOL)
            break;
    }
}

void chat_rebuild_replay(ChatMsgList *list, const AiConversation *conv,
                         char *const *thinking, const CmdRunLog *rlog,
                         const CmdBatchSet *batches)
{
    if (!list || !conv) return;

    /* A reply that went through tool rounds is stored as a turn per round,
     * and its record or batch is keyed to the last turn: the rounds before a
     * turn that carries commands are split at the marker too. */
    unsigned char earlier[AI_MAX_MESSAGES];
    memset(earlier, 0, sizeof(earlier));
    for (int j = 2; j < conv->msg_count && j < AI_MAX_MESSAGES; j++) {
        if (conv->messages[j].role != AI_ROLE_ASSISTANT) continue;
        int hr, cr, lc;
        (void)turn_cards(conv, j, rlog, batches, &hr, &cr, &lc);
        if (hr || lc) mark_earlier_rounds(conv, j, earlier);
    }

    for (int i = 1; i < conv->msg_count; i++) {
        const AiMessage *msg = &conv->messages[i];

        if (msg->role == AI_ROLE_USER) {
            chat_msg_append(list, CHAT_ITEM_USER, ai_msg_content(msg));
            continue;
        }

        if (msg->role == AI_ROLE_TOOL) {
            /* A results turn no assistant turn above claimed. */
            for (int pi = 0; pi < part_count(msg); pi++)
                emit_part(list, msg, pi, 0);
            continue;
        }

        if (msg->role != AI_ROLE_ASSISTANT) continue;   /* a mid-conversation system message */

        const char *content = ai_msg_content(msg);
        const AiToolCall *calls = msg->tool_calls;
        const int n_calls = calls ? msg->n_tool_calls : 0;
        /* What carries this reply's commands: records of the displayed
         * session's run log, and live batches (a card still up). */
        int has_record, card_rows, live_card;
        const uint32_t turn_hash = turn_cards(conv, i, rlog, batches,
                                              &has_record, &card_rows,
                                              &live_card);

        /* The results turn answering this one, when it is the next message. */
        const AiMessage *tm = (i + 1 < conv->msg_count &&
                               conv->messages[i + 1].role == AI_ROLE_TOOL)
                            ? &conv->messages[i + 1] : NULL;

        /* Each call's row, then the result that answers it. A run_command
         * call with a command is drawn by its card instead (once). */
        for (int ci = 0; ci < n_calls; ci++) {
            const AiToolCall *call = &calls[ci];
            const int covered = card_rows &&
                                strcmp(call->name, "run_command") == 0 &&
                                runcmd_describe(call->input_json, NULL, 0);
            if (!covered) {
                char row[CALL_ROW_MAX];
                chat_rebuild_call_text(call, row, sizeof(row));
                chat_msg_append(list, CHAT_ITEM_TOOL_CALL, row);
            }
            for (int pi = 0; tm && pi < part_count(tm); pi++) {
                PartView v;
                part_view(tm, pi, &v);
                if (v.id[0] && strcmp(v.id, call->id) == 0) {
                    emit_part(list, tm, pi, covered);
                    break;
                }
            }
        }
        /* Answers that match none of the calls. */
        for (int pi = 0; tm && pi < part_count(tm); pi++) {
            PartView v;
            part_view(tm, pi, &v);
            if (!part_has_call(&v, calls, n_calls))
                emit_part(list, tm, pi, 0);
        }

        /* The reply's text. With a card or a record it is only what the model
         * wrote before its first command, as live. */
        const int split_text = has_record || live_card ||
                               (i < AI_MAX_MESSAGES && earlier[i]);
        char pre_text[AI_MSG_MAX];
        const char *shown = content;
        if (split_text && content &&
            ai_response_split(content, pre_text, sizeof(pre_text), NULL, 0) > 0)
            shown = pre_text;
        const int has_thinking = thinking && i < AI_MAX_MESSAGES &&
                                 thinking[i] && thinking[i][0];
        if (shown && (shown[0] != '\0' || (split_text && has_thinking))) {
            ChatMsgItem *item = chat_msg_append(list, CHAT_ITEM_AI_TEXT, shown);
            if (item && has_thinking) {
                chat_msg_set_thinking(item, thinking[i]);
                item->u.ai.thinking_complete = 1;
            }
        }

        /* Recorded command rows, then the status rows, which live come after
         * the results. */
        for (int ri = 0; rlog && ri < rlog->count; ri++)
            if (cmd_run_record_matches_hash(rlog->rec[ri], i, 1, turn_hash))
                append_record_rows(list, rlog->rec[ri], -(ri + 1));
        for (int ri = 0; rlog && ri < rlog->count; ri++)
            if (cmd_run_record_matches_hash(rlog->rec[ri], i, 1, turn_hash))
                append_record_status(list, rlog->rec[ri]);

        if (tm) i++;   /* the results turn is drawn */
    }

    /* Pending command batches: one card per batch still in the set, oldest
     * first -- the interleaving with earlier turns is not reconstructed (see
     * docs/superpowers/specs/2026-09-09-pending-command-batches.md, "Session
     * switch"). */
    for (int bi = 0; batches && bi < batches->count; bi++)
        chat_msg_append_batch_items(list, batches->b[bi]);
}
