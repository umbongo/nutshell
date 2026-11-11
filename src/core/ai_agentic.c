#include "ai_agentic.h"
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
                                             calls, n_calls);
}

int agentic_add_assistant_tool_msg_ex(AiConversation *conv,
                                       const char *assistant_text,
                                       const char *reasoning,
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
    }

    if (ai_msg_set_reasoning(msg, reasoning) != 0) {
        ai_msg_free(msg);
        return -1;
    }

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

/* Copy an AiToolResult note (up to AI_TOOL_PART_NOTE_MAX) into a stored
 * message's smaller buffer. A note that fits is copied whole. A longer one is
 * cut on a UTF-8 boundary; when it ends with the cap's "\n" truncation line,
 * that line is kept and the text before it shortened instead. */
static void store_tool_note(char *dst, size_t dst_size, const char *note)
{
    size_t len = strlen(note);
    if (len < dst_size) {
        memcpy(dst, note, len + 1);
        return;
    }
    size_t marker = strlen(AI_TOOL_TRUNC_NOTE);
    size_t head = dst_size - 1;
    size_t tail = 0;
    if (len > marker + 1 && note[len - marker - 1] == '\n' &&
        strcmp(note + len - marker, AI_TOOL_TRUNC_NOTE) == 0 &&
        marker + 1 < dst_size) {
        tail = marker + 1;           /* "\n" + marker */
        head = dst_size - 1 - tail;
    }
    while (head > 0 && ((unsigned char)note[head] & 0xC0) == 0x80) head--;
    memcpy(dst, note, head);
    if (tail) memcpy(dst + head, note + len - tail, tail);
    dst[head + tail] = '\0';
}

int agentic_add_tool_results(AiConversation *conv,
                              const AiToolCall *calls,
                              const AiToolResult *results, int n_results)
{
    if (!conv || !results) return -1;
    if (conv->msg_count + n_results > AI_MAX_MESSAGES) return -1;

    for (int i = 0; i < n_results; i++) {
        AiMessage *msg = &conv->messages[conv->msg_count];
        memset(msg, 0, sizeof(*msg));
        msg->role = AI_ROLE_TOOL;

        /* Cap a private copy: the caller keeps ownership of results[i]. */
        AiToolResult capped = results[i];
        capped.content = NULL;
        if (results[i].content) {
            capped.content = (char *)malloc(results[i].content_len + 1);
            if (!capped.content) return -1;
            memcpy(capped.content, results[i].content, results[i].content_len);
            capped.content[results[i].content_len] = '\0';
        }
        /* run_command's output is bounded by its own executor (64 KiB
         * escaped, with its own cut note): no second cap. */
        if (!(calls && strcmp(calls[i].name, RUNCMD_TOOL_NAME) == 0))
            (void)agentic_cap_tool_result(&capped);

        const char *content = capped.content ? capped.content : "";
        size_t content_len = capped.content ? capped.content_len : 0;
        int set_rc = ai_msg_set_content(msg, content, content_len);
        free(capped.content);
        if (set_rc != 0) return -1;

        store_tool_note(msg->tool_note, sizeof(msg->tool_note),
                        capped.tool_note);

        strncpy(msg->tool_call_id, results[i].tool_use_id,
                sizeof(msg->tool_call_id) - 1);
        msg->tool_call_id[sizeof(msg->tool_call_id) - 1] = '\0';

        if (calls) {
            strncpy(msg->tool_name, calls[i].name,
                    sizeof(msg->tool_name) - 1);
            msg->tool_name[sizeof(msg->tool_name) - 1] = '\0';
        }

        msg->is_tool_error = results[i].is_error;

        conv->msg_count++;
    }
    return 0;
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
            JsonNode *root = json_parse(calls[i].input_json);
            bad = !root || root->type != JSON_OBJECT;
            if (root) json_free(root);
        }
        if (bad) {
            memcpy(calls[i].input_json, "{}", 3);
            fixed++;
        }
    }
    return fixed;
}

int agentic_room_for_tool_round(const AiConversation *conv)
{
    if (!conv) return 0;
    return conv->msg_count + 3 <= AI_MAX_MESSAGES;
}
