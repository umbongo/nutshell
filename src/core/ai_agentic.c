#include "ai_agentic.h"
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
        if (!msg->tool_calls) return -1;
        memcpy(msg->tool_calls, calls, (size_t)n_calls * sizeof(AiToolCall));
        msg->n_tool_calls = n_calls;
    }

    conv->msg_count++;
    return 0;
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

        const char *content = results[i].content ? results[i].content : "";
        size_t content_len = results[i].content ? results[i].content_len : 0;
        if (ai_msg_set_content(msg, content, content_len) != 0)
            return -1;

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

int agentic_truncation_warning(const char *tool_name, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    return snprintf(buf, buf_size,
                    "Tool result from '%s' was truncated to 1MB.",
                    tool_name ? tool_name : "");
}
