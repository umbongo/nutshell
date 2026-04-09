#ifndef NUTSHELL_AI_AGENTIC_H
#define NUTSHELL_AI_AGENTIC_H

#include "ai_tools.h"
#include "ai_prompt.h"

/* Agentic loop state — tracks iteration and rate limits per user message */
typedef struct {
    int loop_iter;        /* current iteration (0-based), reset per user message */
    int search_count;     /* web_search invocations this user message */
} AgenticState;

/* Initialize/reset agentic state (call at start of each user message) */
void agentic_state_reset(AgenticState *state);

/* Check if we should continue the agentic loop.
 * Returns 1 if we can continue, 0 if max iterations reached. */
int agentic_can_continue(const AgenticState *state);

/* Check rate limit for a specific tool.
 * Returns 1 if allowed, 0 if rate-limited.
 * If rate-limited, writes an error message to err_buf. */
int agentic_check_rate_limit(AgenticState *state, const char *tool_name,
                             char *err_buf, size_t err_size);

/* Record that a tool call was made (for rate limiting). */
void agentic_record_tool_call(AgenticState *state, const char *tool_name);

/* Add an assistant message with tool_use blocks to the conversation.
 * assistant_text: the text content from the assistant response (may be empty).
 * calls: array of tool calls from the response.
 * n_calls: number of tool calls.
 * Returns 0 on success, -1 if conversation is full. */
int agentic_add_assistant_tool_msg(AiConversation *conv,
                                   const char *assistant_text,
                                   const AiToolCall *calls, int n_calls);

/* Add tool result messages to the conversation.
 * calls: array of tool calls (used to copy tool names).
 * results: array of tool results.
 * n_results: number of results.
 * Returns 0 on success, -1 if conversation is full. */
int agentic_add_tool_results(AiConversation *conv,
                             const AiToolCall *calls,
                             const AiToolResult *results, int n_results);

/* Generate a truncation warning message.
 * tool_name: name of the tool that was truncated.
 * buf/size: output buffer.
 * Returns bytes written. */
int agentic_truncation_warning(const char *tool_name, char *buf, size_t buf_size);

#endif /* NUTSHELL_AI_AGENTIC_H */
