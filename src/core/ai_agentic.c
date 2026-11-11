#include "ai_agentic.h"
#include "ai_conv_guard.h"
#include "json_validate.h"
#include "json_parser.h"
#include "run_command.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void agentic_state_reset(AgenticState *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

int agentic_can_continue(const AgenticState *state)
{
    if (!state) return 0;
    return state->loop_iter < AI_TOOL_LOOP_MAX;
}

int agentic_check_rate_limit(AgenticState *state, const char *tool_name,
                              char *err_buf, size_t err_size)
{
    if (!state || !tool_name) return 1;
    if (strcmp(tool_name, "web_search") == 0) {
        if (state->search_count >= AI_TOOL_SEARCH_MAX) {
            if (err_buf && err_size > 0) {
                snprintf(err_buf, err_size,
                    "Search limit reached (%d per message). "
                    "Please ask the user to send a new message if more searches are needed.",
                    AI_TOOL_SEARCH_MAX);
            }
            return 0;
        }
    }
    return 1;
}

void agentic_record_tool_call(AgenticState *state, const char *tool_name)
{
    if (!state || !tool_name) return;
    if (strcmp(tool_name, "web_search") == 0) {
        state->search_count++;
    }
}

int agentic_add_assistant_tool_msg(AiConversation *conv,
                                    const char *assistant_text,
                                    const AiToolCall *calls, int n_calls)
{
    return agentic_add_assistant_tool_msg_ex(conv, assistant_text, NULL,
                                             NULL, NULL, calls, n_calls);
}

/* 1 when `id` is the id of a call in calls[0..n) or of any call or result
 * stored in the conversation. */
static int id_in_use(const AiConversation *conv, const AiToolCall *calls,
                     int n, const char *id)
{
    for (int i = 0; i < n; i++)
        if (strcmp(calls[i].id, id) == 0) return 1;
    for (int i = 0; i < conv->msg_count; i++) {
        const AiMessage *m = &conv->messages[i];
        for (int k = 0; m->tool_calls && k < m->n_tool_calls; k++)
            if (strcmp(m->tool_calls[k].id, id) == 0) return 1;
        if (m->role != AI_ROLE_TOOL) continue;
        if (!m->parts) {
            if (strcmp(m->tool_call_id, id) == 0) return 1;
            continue;
        }
        for (int k = 0; k < m->n_parts; k++)
            if (strcmp(m->parts[k].tool_call_id, id) == 0) return 1;
    }
    return 0;
}

/* An empty id, or one an earlier call of the turn already has, is replaced by
 * call_<n>, n the smallest number from 1 that no id in the turn or the
 * conversation uses (spec decision 19, review of B1-B11, 2026-11-11). A turn
 * whose ids are unique and non-empty is left as it is. */
static void make_ids_unique(const AiConversation *conv, AiToolCall *calls,
                            int n)
{
    int next = 1;
    for (int i = 0; i < n; i++) {
        int bad = calls[i].id[0] == '\0';
        for (int j = 0; !bad && j < i; j++)
            bad = strcmp(calls[j].id, calls[i].id) == 0;
        if (!bad) continue;
        char cand[AI_TOOL_ID_MAX];
        do {
            snprintf(cand, sizeof(cand), "call_%d", next++);
        } while (id_in_use(conv, calls, n, cand));
        memcpy(calls[i].id, cand, strlen(cand) + 1);
    }
}

int agentic_add_assistant_tool_msg_ex(AiConversation *conv,
                                       const char *assistant_text,
                                       const char *reasoning,
                                       const char *provider, const char *model,
                                       const AiToolCall *calls, int n_calls)
{
    if (!conv) return -1;
    if (conv->msg_count >= AI_MAX_MESSAGES) return -1;

    AiMessage *msg = &conv->messages[conv->msg_count];
    memset(msg, 0, sizeof(*msg));
    msg->role = AI_ROLE_ASSISTANT;

    if (assistant_text) {
        if (ai_msg_set_content(msg, assistant_text, strlen(assistant_text)) != 0)
            return -1;
    }

    if (n_calls > 0 && calls) {
        msg->tool_calls = malloc((size_t)n_calls * sizeof(AiToolCall));
        if (!msg->tool_calls) { ai_msg_free(msg); return -1; }
        memcpy(msg->tool_calls, calls, (size_t)n_calls * sizeof(AiToolCall));
        msg->n_tool_calls = n_calls;
        make_ids_unique(conv, msg->tool_calls, n_calls);
    }

    if (ai_msg_set_reasoning(msg, reasoning) != 0) {
        ai_msg_free(msg);
        return -1;
    }
    snprintf(msg->tool_provider, sizeof(msg->tool_provider), "%s", provider ? provider : "");
    snprintf(msg->tool_model, sizeof(msg->tool_model), "%s", model ? model : "");

    conv->msg_count++;
    return 0;
}

int agentic_cap_tool_result(AiToolResult *r)
{
    if (!r || !r->content) return 0;

    /* Strip control bytes other than \n and \t (0x7F included). */
    size_t w = 0;
    for (size_t i = 0; i < r->content_len; i++) {
        unsigned char c = (unsigned char)r->content[i];
        if ((c < 0x20 && c != '\n' && c != '\t') || c == 0x7F) continue;
        r->content[w++] = (char)c;
    }
    r->content_len = w;
    r->content[w] = '\0';

    /* Longest prefix whose escaped size fits. */
    size_t esc = 0, cut = w;
    for (size_t i = 0; i < w; i++) {
        esc += json_escaped_byte_len((unsigned char)r->content[i]);
        if (esc > AI_TOOL_RESULT_ESCAPED_MAX) { cut = i; break; }
    }
    if (cut == w) return 0;

    while (cut > 0 && ((unsigned char)r->content[cut] & 0xC0) == 0x80)
        cut--;
    r->content_len = cut;
    r->content[cut] = '\0';

    size_t note_len = strlen(r->tool_note);
    size_t marker = strlen(AI_TOOL_TRUNC_NOTE);
    if (note_len == 0) {
        memcpy(r->tool_note, AI_TOOL_TRUNC_NOTE, marker + 1);
    } else {
        size_t room = sizeof(r->tool_note) - 1 - 1 - marker;  /* "\n" + marker */
        if (note_len > room) {
            note_len = room;
            while (note_len > 0 &&
                   ((unsigned char)r->tool_note[note_len] & 0xC0) == 0x80)
                note_len--;
        }
        r->tool_note[note_len] = '\n';
        memcpy(r->tool_note + note_len + 1, AI_TOOL_TRUNC_NOTE, marker + 1);
    }
    return 1;
}

/* Parts and their content come from here, not malloc directly, so a test can
 * fail one allocation (agentic_set_alloc_for_test). */
static void *(*g_alloc)(size_t) = malloc;

void agentic_set_alloc_for_test(void *(*alloc)(size_t))
{
    g_alloc = alloc ? alloc : malloc;
}

/* Free a parts array and the content of the first n parts. */
static void parts_free(AiToolPart *parts, int n)
{
    for (int i = 0; parts && i < n; i++)
        free(parts[i].content);
    free(parts);
}

/* Which stored call result i answers, or -1 for none (ruling R50, review of
 * the fix wave, 2026-11-11):
 *  1. by id: its tool_use_id is non-empty, equals the id of a stored call, and
 *     no earlier result carries the same id (stored ids are unique, so this is
 *     the one call it names);
 *  2. by position, only for an id the store made up: i is a stored call whose
 *     stored id differs from the caller's own id for that position
 *     (calls[i].id, or this result's tool_use_id when there is no calls array)
 *     -- an empty or repeated id became call_<n> when the turn was stored, so
 *     the result cannot name it;
 *  3. otherwise none: the part keeps the result's own id, and a results array
 *     whose ids are wrong or out of place fails
 *     ai_conv_tool_pairs_well_formed instead of being paired by luck. */
static int pair_result(const AiToolCall *calls, const AiToolResult *results,
                       int i, const AiToolCall *stored, int n_stored)
{
    const char *rid = results[i].tool_use_id;
    if (rid[0]) {
        int dup = 0;
        for (int k = 0; k < i && !dup; k++)
            dup = strcmp(results[k].tool_use_id, rid) == 0;
        if (!dup)
            for (int j = 0; j < n_stored; j++)
                if (strcmp(stored[j].id, rid) == 0) return j;
    }
    if (i < n_stored) {
        const char *caller_id = calls ? calls[i].id : rid;
        if (strcmp(stored[i].id, caller_id) != 0) return i;
    }
    return -1;
}

/* The fixed fields of part i: ids and note, never content. */
static void part_fill_head(AiToolPart *p, const AiToolCall *calls,
                           const AiToolResult *results, int i,
                           const AiToolCall *stored, int n_stored)
{
    int j = pair_result(calls, results, i, stored, n_stored);
    snprintf(p->tool_call_id, sizeof(p->tool_call_id), "%s",
             j >= 0 ? stored[j].id : results[i].tool_use_id);
    if (j >= 0)
        snprintf(p->tool_name, sizeof(p->tool_name), "%s", stored[j].name);
    else if (calls)
        snprintf(p->tool_name, sizeof(p->tool_name), "%s", calls[i].name);
}

/* One part per result, content capped on a private copy (the caller keeps its
 * results). NULL on an allocation failure, with everything freed. */
static AiToolPart *parts_from_results(const AiToolCall *calls,
                                      const AiToolResult *results, int n,
                                      const AiToolCall *stored, int n_stored)
{
    AiToolPart *parts = (AiToolPart *)g_alloc((size_t)n * sizeof(AiToolPart));
    if (!parts) return NULL;
    memset(parts, 0, (size_t)n * sizeof(AiToolPart));

    for (int i = 0; i < n; i++) {
        AiToolPart *p = &parts[i];
        part_fill_head(p, calls, results, i, stored, n_stored);
        p->is_error = results[i].is_error;
        memcpy(p->tool_note, results[i].tool_note, sizeof(p->tool_note));
        p->tool_note[sizeof(p->tool_note) - 1] = '\0';

        p->omit_at = results[i].omit_at;
        p->omitted_bytes = results[i].omitted_bytes;
        if (!results[i].content) continue;
        AiToolResult capped = results[i];
        capped.content = (char *)g_alloc(results[i].content_len + 1);
        if (!capped.content) { parts_free(parts, i); return NULL; }
        memcpy(capped.content, results[i].content, results[i].content_len);
        capped.content[results[i].content_len] = '\0';

        /* run_command's output is bounded by its own executor (64 KiB
         * escaped, with its own cut note): no second cap. The tool is the
         * one of the call this result was paired with (by id, R50), which
         * part_fill_head wrote into the part: a results array in another
         * order than the calls must not cap the wrong one (C0). */
        int runcmd = strcmp(p->tool_name, RUNCMD_TOOL_NAME) == 0;
        if (!runcmd) {
            (void)agentic_cap_tool_result(&capped);
            memcpy(p->tool_note, capped.tool_note, sizeof(p->tool_note));
        }
        if (capped.content_len == 0) {
            free(capped.content);            /* an empty result: no content */
        } else {
            p->content = capped.content;
            p->content_len = capped.content_len;
        }
    }
    return parts;
}

#define AGENTIC_OUTPUT_DROPPED "output dropped: Nutshell ran out of memory"

/* The result's own note with, when the result had output, the phrase that
 * says it was dropped ("; " after a note, alone after none), in note[cap]. A
 * note too long to take the phrase is cut back on a UTF-8 boundary first, so
 * it is the end of the note that goes, never a part of the phrase. A stale
 * "output cut: N bytes omitted" phrase goes first. A result
 * with no output keeps its note as it is. */
static void notes_only_note(char *note, size_t cap, const AiToolResult *r)
{
    snprintf(note, cap, "%s", r->tool_note);
    if (!r->content || r->content_len == 0) return;
    /* the output is dropped, so a count of bytes omitted from it is stale */
    runcmd_note_drop_cut(note, cap);
    size_t phrase = strlen(AGENTIC_OUTPUT_DROPPED);
    size_t len = strlen(note);
    if (len == 0) {
        memcpy(note, AGENTIC_OUTPUT_DROPPED, phrase + 1);
        return;
    }
    if (len + 2 + phrase > cap - 1) {
        len = cap - 1 - 2 - phrase;
        while (len > 0 && ((unsigned char)note[len] & 0xC0) == 0x80) len--;
    }
    memcpy(note + len, "; ", 2);
    memcpy(note + len + 2, AGENTIC_OUTPUT_DROPPED, phrase + 1);
}

/* The answer used when the results cannot be copied: one part per call, no
 * content, the result's own note plus "output dropped" where there was
 * output (spec decision 25: a command that ran is never reported as not
 * run). Only the array is allocated. NULL when even that cannot be had. */
static AiToolPart *parts_notes_only(const AiToolCall *calls,
                                    const AiToolResult *results, int n,
                                    const AiToolCall *stored, int n_stored)
{
    AiToolPart *parts = (AiToolPart *)g_alloc((size_t)n * sizeof(AiToolPart));
    if (!parts) return NULL;
    memset(parts, 0, (size_t)n * sizeof(AiToolPart));
    for (int i = 0; i < n; i++) {
        part_fill_head(&parts[i], calls, results, i, stored, n_stored);
        parts[i].is_error = results[i].is_error;
        notes_only_note(parts[i].tool_note, sizeof(parts[i].tool_note), &results[i]);
    }
    return parts;
}

int agentic_add_tool_results(AiConversation *conv,
                              const AiToolCall *calls,
                              const AiToolResult *results, int n_results)
{
    if (!conv || !results) return -1;
    if (n_results <= 0) return 0;
    if (conv->msg_count + 1 > AI_MAX_MESSAGES) {
        /* Full: nothing is stored, and the open assistant turn goes, so the
         * conversation passes ai_conv_tool_pairs_well_formed. */
        ai_conv_drop_open_tool_turn(conv);
        return -1;
    }

    /* The calls this round answers are the stored turn's, in order. */
    const AiToolCall *stored = NULL;
    int n_stored = 0;
    if (conv->msg_count > 0) {
        const AiMessage *last = &conv->messages[conv->msg_count - 1];
        if (last->role == AI_ROLE_ASSISTANT && last->tool_calls) {
            stored = last->tool_calls;
            n_stored = last->n_tool_calls;
        }
    }

    int rc = 0;
    AiToolPart *parts = parts_from_results(calls, results, n_results,
                                           stored, n_stored);
    if (!parts) {
        /* Out of memory: answer every call from its own note instead, so no
         * tool_use is left without its result (1). If even that array cannot
         * be had, the open assistant turn is dropped (spec decision 25) so the
         * conversation stays well-formed, and -2 says nothing was stored. */
        parts = parts_notes_only(calls, results, n_results, stored, n_stored);
        if (!parts) {
            ai_conv_drop_open_tool_turn(conv);
            return -2;
        }
        rc = 1;
    }

    AiMessage *msg = &conv->messages[conv->msg_count];
    memset(msg, 0, sizeof(*msg));
    msg->role = AI_ROLE_TOOL;
    msg->parts = parts;
    msg->n_parts = n_results;
    conv->msg_count++;
    return rc;
}

int agentic_truncation_warning(const char *tool_name, size_t omitted_bytes,
                               char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    const char *name = tool_name ? tool_name : "";
    if (omitted_bytes == 0)
        return snprintf(buf, buf_size, "[%s output truncated at 64 KiB]", name);
    return snprintf(buf, buf_size, "[%s output cut: %zu bytes omitted]", name,
                    omitted_bytes);
}

int agentic_sanitize_tool_inputs(AiToolCall *calls, int n)
{
    if (!calls || n <= 0) return 0;
    int fixed = 0;
    for (int i = 0; i < n; i++) {
        int bad = calls[i].overflowed;
        if (!bad) {
            JsonNode *root = json_parse_exact(calls[i].input_json);
            bad = !root || root->type != JSON_OBJECT;
            if (root) json_free(root);
            /* the parser is more tolerant than the strict check every request
             * body goes through: a raw newline in a string, an unknown escape,
             * \v or \f around the object, nesting past 128, a bare "-" */
            if (!bad) {
                char err[96];
                bad = !json_validate(calls[i].input_json,
                                     strlen(calls[i].input_json), err, sizeof(err));
            }
        }
        if (bad) {
            memcpy(calls[i].input_json, "{}", 3);
            fixed++;
        }
    }
    return fixed;
}

int agentic_room_for_tool_round(const AiConversation *conv,
                                const AiNoteQueue *q)
{
    if (!conv) return 0;
    int notes = q && q->count > 0 ? q->count : 0;
    return conv->msg_count + 3 + notes <= AI_MAX_MESSAGES;
}
