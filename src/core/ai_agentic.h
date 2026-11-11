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
 * Returns 0 on success, -1 if conversation is full.
 * An empty call id, or one repeated within the turn, is stored as call_<n>,
 * n the smallest number from 1 that no id in the turn or the conversation
 * uses; a turn whose ids are unique and non-empty is stored byte for byte.
 * The caller's `calls` are never changed. */
int agentic_add_assistant_tool_msg(AiConversation *conv,
                                   const char *assistant_text,
                                   const AiToolCall *calls, int n_calls);

/* The same, keeping `reasoning` (the thinking the model streamed with the
 * tool calls; NULL or "" for none) on the message, capped at
 * AI_REASONING_MAX - 1 bytes (a cut sets AiMessage.reasoning_cut). Provider
 * "moonshot" sends it back on a tool-call turn, "deepseek" on every assistant
 * turn of a request with tools.
 * provider and model (NULL or "" for unknown) are recorded on the message as
 * AiMessage.tool_provider / tool_model, cut to their fields: the request
 * builder sends the turn's ids and reasoning only to that provider and model,
 * and renders the turn as text to any other (spec decision 16).
 * thought_sig (NULL or "" for none) is Gemini's thought signature for the
 * turn's tool calls, stored on the message as AiMessage.thought_sig (no cap);
 * the builder writes it back on the turn's first tool call for provider
 * "gemini" only. */
int agentic_add_assistant_tool_msg_ex(AiConversation *conv,
                                      const char *assistant_text,
                                      const char *reasoning,
                                      const char *provider, const char *model,
                                      const AiToolCall *calls, int n_calls,
                                      const char *thought_sig);

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
 * per call with no content and the result's own note (and is_error), plus
 * "output dropped: Nutshell ran out of memory" for a result that had output
 * ("; " after a note, alone after none; a note too long for it is cut on a
 * UTF-8 boundary), so no call is left unanswered and a command that ran is
 * never reported as not run (spec decision 25). A result with no output
 * keeps its note unchanged.
 * Each part's tool_call_id and tool_name come from the open stored assistant
 * turn's call the result answers (ruling R50, review of the fix wave,
 * 2026-11-11): the call whose stored id equals the result's tool_use_id (when
 * no earlier result carries that id too); else the call at the result's own
 * position when the store made that call's id up (an empty or repeated id
 * became call_<n>, so it differs from the caller's calls[i].id; with `calls`
 * NULL, from the result's own tool_use_id); else none, and the part keeps the
 * result's own tool_use_id. A results array whose ids are wrong or out of
 * place therefore fails ai_conv_tool_pairs_well_formed instead of being
 * paired by position.
 * Returns 0 when the results are stored; 1 when an allocation failure left only
 * the notes-only answer stored; -1 when the conversation is full; -2 when even
 * the notes-only array could not be had. On -1 and -2 nothing is stored and the
 * open assistant tool turn is dropped (ai_conv_drop_open_tool_turn), so after
 * any return the conversation passes ai_conv_tool_pairs_well_formed. */
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
 * later request body invalid), or that json_validate rejects (a raw newline in
 * a string, an unknown escape, nesting past its limit), is replaced with "{}". The overflowed flag is left set. Returns how many were replaced. */
int agentic_sanitize_tool_inputs(AiToolCall *calls, int n);

/* 1 when the conversation has room for one tool round (the assistant turn,
 * the results turn and a closing reply) and for the notes queued in `q` (NULL
 * counts 0), which the settle flushes after the results:
 * msg_count + 3 + q->count <= AI_MAX_MESSAGES. 0 for a NULL conversation. The
 * stream thread passes NULL (it never reads the owner's queue); the reply's
 * slot is then kept by the flush itself (ai_conv_flush_notes' keep_free). */
int agentic_room_for_tool_round(const AiConversation *conv,
                                const AiNoteQueue *q);

/* What the stream thread does after the conversation took (or refused) a tool
 * turn or a round's results (Task B13; spec decision 25, R37). */
typedef enum {
    AGENTIC_STEP_GO,     /* stored: carry on */
    AGENTIC_STEP_NOTE,   /* stored as notes only: carry on, say AGENTIC_REDUCED_ROW */
    AGENTIC_STEP_FULL,   /* conversation full: end with agentic_full_row() */
    AGENTIC_STEP_FAIL    /* could not store (out of memory): end with an error */
} AgenticStep;

/* agentic_add_tool_results' return value: 0 GO, 1 NOTE, negative (-1 full,
 * -2 not even the notes-only array) FULL. The turn is already dropped then. */
AgenticStep agentic_results_step(int rc);

/* agentic_add_assistant_tool_msg_ex' return value: 0 GO, -1 FULL (the
 * conversation filled meanwhile), anything else FAIL. */
AgenticStep agentic_turn_step(int rc);

#define AGENTIC_REDUCED_ROW \
    "[some results were reduced to notes: Nutshell ran out of memory]"

/* The status row for a round that is not stored for want of room:
 * "[conversation full: N tool call(s) not run; start a New Chat]". Returns
 * snprintf's value; 0 and nothing written for a NULL or empty buffer. */
int agentic_full_row(int n_calls, char *buf, size_t cap);

#endif /* NUTSHELL_AI_AGENTIC_H */
