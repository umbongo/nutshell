#include "ai_conv_guard.h"
#include <string.h>

/* ---- parts of a results turn ------------------------------------------------ */

/* The number of answers a tool message carries: its parts, or one for the
 * older parts-less form. */
static int tool_msg_parts(const AiMessage *m)
{
    return m->parts ? m->n_parts : 1;
}

/* How many of the message's parts answer tool call `id`. */
static int tool_msg_count_id(const AiMessage *m, const char *id)
{
    if (!m->parts) return strcmp(m->tool_call_id, id) == 0 ? 1 : 0;
    int n = 0;
    for (int i = 0; i < m->n_parts; i++)
        if (strcmp(m->parts[i].tool_call_id, id) == 0) n++;
    return n;
}

/* How many answers to the calls of assistant turn `a` the tool messages
 * [from, to) hold. Every call is counted once however often it is answered. */
static int calls_answered(const AiMessage *a, const AiMessage *msgs, int from,
                          int to)
{
    int answered = 0;
    for (int k = 0; k < a->n_tool_calls; k++) {
        int n = 0;
        for (int j = from; j < to; j++)
            n += tool_msg_count_id(&msgs[j], a->tool_calls[k].id);
        if (n > 0) answered++;
    }
    return answered;
}

static int is_tool_turn(const AiMessage *m)
{
    return m->role == AI_ROLE_ASSISTANT && m->n_tool_calls > 0;
}

/* ---- open calls ------------------------------------------------------------- */

/* Index of the open assistant tool turn at the end of the conversation, or
 * -1 when none is open. */
static int open_turn_index(const AiConversation *conv)
{
    if (!conv) return -1;
    int i = conv->msg_count - 1;
    while (i >= 0 && conv->messages[i].role == AI_ROLE_TOOL) i--;
    if (i < 0) return -1;
    const AiMessage *a = &conv->messages[i];
    if (!is_tool_turn(a)) return -1;
    if (!a->tool_calls) return i;
    return calls_answered(a, conv->messages, i + 1, conv->msg_count)
               < a->n_tool_calls ? i : -1;
}

int ai_conv_tool_calls_open(const AiConversation *conv)
{
    return open_turn_index(conv) >= 0;
}

int ai_conv_drop_open_tool_turn(AiConversation *conv)
{
    int idx = open_turn_index(conv);
    if (idx < 0) return 0;
    for (int i = idx; i < conv->msg_count; i++) {
        ai_msg_free(&conv->messages[i]);
        memset(&conv->messages[i], 0, sizeof(conv->messages[i]));
    }
    conv->msg_count = idx;
    return 1;
}

/* ---- the invariant ---------------------------------------------------------- */

int ai_conv_tool_pairs_well_formed(const AiConversation *conv)
{
    if (!conv) return 0;
    int i = 0;
    while (i < conv->msg_count) {
        const AiMessage *m = &conv->messages[i];
        if (m->role == AI_ROLE_TOOL) return 0;       /* no tool_use before it */
        if (!is_tool_turn(m)) { i++; continue; }
        if (!m->tool_calls) return 0;

        /* the run of results turns that follows */
        int from = i + 1, to = from, total = 0, with_parts = 0;
        while (to < conv->msg_count && conv->messages[to].role == AI_ROLE_TOOL) {
            total += tool_msg_parts(&conv->messages[to]);
            if (conv->messages[to].parts) with_parts = 1;
            to++;
        }
        if (to == from) return 0;                    /* open or unanswered */
        /* one results turn per round; only a run of the older parts-less
         * messages (one answer each) may be several */
        if (with_parts && to - from != 1) return 0;
        if (total != m->n_tool_calls) return 0;
        /* a repeated id within one turn can never be answered unambiguously
         * (review of B1-B11, 2026-11-11) */
        for (int k = 0; k < m->n_tool_calls; k++)
            for (int l = k + 1; l < m->n_tool_calls; l++)
                if (strcmp(m->tool_calls[k].id, m->tool_calls[l].id) == 0)
                    return 0;
        for (int k = 0; k < m->n_tool_calls; k++) {
            int n = 0;
            for (int j = from; j < to; j++)
                n += tool_msg_count_id(&conv->messages[j], m->tool_calls[k].id);
            if (n != 1) return 0;
        }
        i = to;
    }
    return 1;
}

/* ---- the note door ---------------------------------------------------------- */

#define NOTE_CUT_MARKER " [\xE2\x80\xA6 cut]"

/* Copy `text` into dst (AI_NOTE_TEXT_MAX bytes), cut on a UTF-8 boundary with
 * the marker at the end when it is AI_NOTE_TEXT_MAX bytes or more. */
static void note_copy(char *dst, const char *text)
{
    size_t len = strlen(text);
    if (len < (size_t)AI_NOTE_TEXT_MAX) {
        memcpy(dst, text, len + 1);
        return;
    }
    size_t marker = strlen(NOTE_CUT_MARKER);
    size_t keep = (size_t)AI_NOTE_TEXT_MAX - 1 - marker;
    while (keep > 0 && ((unsigned char)text[keep] & 0xC0) == 0x80) keep--;
    memcpy(dst, text, keep);
    memcpy(dst + keep, NOTE_CUT_MARKER, marker + 1);
}

/* Append `text` as a USER turn marked as Nutshell's own: the one place the
 * mark is set (spec decision 22). */
static int add_note_turn(AiConversation *conv, const char *text)
{
    if (ai_conv_add(conv, AI_ROLE_USER, text) != 0) return -1;
    conv->messages[conv->msg_count - 1].nutshell_note = 1;
    return 0;
}

int ai_conv_flush_notes(AiConversation *conv, AiNoteQueue *q, int keep_free)
{
    if (!conv || !q || q->count <= 0) return 0;
    if (ai_conv_tool_calls_open(conv)) return 0;
    if (keep_free < 0) keep_free = 0;
    /* In order, up to the first note that cannot be stored or would take one
     * of the keep_free slots; that one and every later one stay queued
     * (review of B1-B11 and of the fix wave, 2026-11-11). */
    int appended = 0;
    while (appended < q->count
           && conv->msg_count + 1 + keep_free <= AI_MAX_MESSAGES
           && add_note_turn(conv, q->text[appended]) == 0)
        appended++;
    if (appended == 0) return 0;
    int left = q->count - appended;
    memmove(q->text[0], q->text[appended], (size_t)left * sizeof(q->text[0]));
    memset(q->text[left], 0, (size_t)appended * sizeof(q->text[0]));
    q->count = left;
    return appended;
}

int ai_conv_add_user_note(AiConversation *conv, AiNoteQueue *q, int defer,
                          int keep_free, const char *text)
{
    if (!conv || !text || !text[0]) return -1;

    if (defer || ai_conv_tool_calls_open(conv)) {
        if (!q || q->count >= AI_NOTE_QUEUE_MAX) return -1;
        note_copy(q->text[q->count], text);
        q->count++;
        return 1;
    }

    if (keep_free < 0) keep_free = 0;
    /* Earlier notes waiting go in first, so the order is kept, and each leaves
     * the text's own slot and keep_free more (the reply's, when the text
     * launches a request): the flush is the door's, not the caller's. A note
     * that cannot be stored under that rule stays queued with every later one
     * (C0, R54). */
    ai_conv_flush_notes(conv, q, keep_free + 1);
    if (q && q->count > 0) return 2;                  /* never ahead of a queued note */
    if (conv->msg_count + 1 + keep_free > AI_MAX_MESSAGES) return 2;

    char cut[AI_NOTE_TEXT_MAX];
    note_copy(cut, text);
    return add_note_turn(conv, cut) == 0 ? 0 : -1;
}

int ai_conv_room_for_send(const AiConversation *conv, int open_batch,
                          const AiNoteQueue *q)
{
    if (!conv) return 0;
    int notes = q && q->count > 0 ? q->count : 0;
    return conv->msg_count + 2 + (open_batch ? 1 : 0) + notes <= AI_MAX_MESSAGES;
}
