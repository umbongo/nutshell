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

/* The same, keeping `reasoning` (the thinking the model streamed with the
 * tool calls; NULL or "" for none) on the message, capped at
 * AI_REASONING_MAX - 1 bytes. Only provider "moonshot" sends it back.
 * provider and model (NULL or "" for unknown) are recorded on the message as
 * AiMessage.tool_provider / tool_model, cut to their fields: the request
 * builder sends the turn's ids and reasoning only to that provider and model,
 * and renders the turn as text to any other (spec decision 16). */
int agentic_add_assistant_tool_msg_ex(AiConversation *conv,
                                      const char *assistant_text,
                                      const char *reasoning,
                                      const char *provider, const char *model,
                                      const AiToolCall *calls, int n_calls);

/* A stored tool result is at most this many bytes once JSON-escaped (the size
 * json_escape_string would write for it). */
#define AI_TOOL_RESULT_ESCAPED_MAX 65536
#define AI_TOOL_TRUNC_NOTE "[truncated by nutshell at 65536 bytes]"

/* Cap one tool result in place: strip every control byte other than \n and
 * \t (so a raw byte escapes to at most 2 bytes), then cut content on a UTF-8
 * boundary at the longest prefix whose JSON-escaped size is at most
 * AI_TOOL_RESULT_ESCAPED_MAX. A cut sets tool_note to AI_TOOL_TRUNC_NOTE, or
 * appends "\n" AI_TOOL_TRUNC_NOTE to a note already there (the old note
 * shortens if both do not fit in AI_TOOL_PART_NOTE_MAX). Returns 1 when it cut,
 * 0 otherwise (also for a NULL result or NULL content). */
int agentic_cap_tool_result(AiToolResult *r);

/* Add one round's results to the conversation as ONE AI_ROLE_TOOL message
 * whose parts are the results, in order (spec decision 11: a round costs one
 * slot, not one per call).
 * Each result's content is capped (agentic_cap_tool_result) on a private copy
 * before it is stored, so the caller's results are never modified and still
 * own their content; except a run_command result, whose executor has already
 * bounded it. A part keeps the result's whole tool_note (AI_TOOL_PART_NOTE_MAX;
 * the truncation line, when the cap added one, ends it) and its omit_at and
 * omitted_bytes. An empty result stores no content.
 * calls: array of tool calls (used to copy tool names), or NULL.
 * results: array of tool results.
 * n_results: number of results; 0 stores nothing and returns 0.
 * Out of memory: the round is stored as a notes-only turn instead, one part
 * per call with no content and the note "not run: Nutshell ran out of
 * memory", so no call is left unanswered (spec decision 25).
 * Returns 0 on success, -1 storing nothing when the conversation is full or
 * when even the notes-only array cannot be allocated (the open assistant turn
 * is then the caller's to drop). */
int agentic_add_tool_results(AiConversation *conv,
                             const AiToolCall *calls,
                             const AiToolResult *results, int n_results);

/* TEST ONLY. Replace the allocator agentic_add_tool_results() uses for the
 * parts array and each part's content (default malloc; NULL restores it), so a
 * test can fail one allocation. Not for product code. */
void agentic_set_alloc_for_test(void *(*alloc)(size_t));

/* Generate a truncation warning message.
 * tool_name: name of the tool whose output was cut.
 * omitted_bytes: how many bytes were left out; 0 when not known, which says
 * what the cap is: "[%s output truncated at 64 KiB]". Otherwise
 * "[%s output cut: %zu bytes omitted]".
 * buf/size: output buffer.
 * Returns bytes written (snprintf's value), 0 for a NULL or empty buffer. */
int agentic_truncation_warning(const char *tool_name, size_t omitted_bytes,
                               char *buf, size_t buf_size);

/* Make every call's stored input something a provider will accept back: an
 * input that overflowed AI_TOOL_INPUT_MAX (a cut or spliced command must
 * never be read as a different one) or is not exactly one JSON object
 * followed by nothing but whitespace (a reply cut off mid-call leaves an
 * unterminated string; trailing bytes or a second value would make every
 * later request body invalid) is replaced with "{}". The overflowed flag is left set. Returns how many were replaced. */
int agentic_sanitize_tool_inputs(AiToolCall *calls, int n);

/* 1 when the conversation has room for one tool round (the assistant turn,
 * the results turn and a closing reply): msg_count + 3 <= AI_MAX_MESSAGES.
 * 0 for a NULL conversation. */
int agentic_room_for_tool_round(const AiConversation *conv);

#endif /* NUTSHELL_AI_AGENTIC_H */
