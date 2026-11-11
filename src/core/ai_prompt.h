#ifndef NUTSHELL_AI_PROMPT_H
#define NUTSHELL_AI_PROMPT_H

#include <stddef.h>
#include "ai_tools.h"
#include "cmd_batch.h"
#include "cmd_run_log.h"
#include "chat_approval.h" /* ApprovalQueue for ai_build_continue_text() */
#include "session_io.h"   /* SessionKind for the system-prompt opening */

/* Default AI provider — must match the first entry in the provider list
 * shown in the Settings dialog (settings.c k_ai_providers[]). */
#define AI_DEFAULT_PROVIDER "anthropic"

#define AI_MSG_MAX     16384
#define AI_BODY_MAX    524288   /* 512KB — accommodates base64 images */
#define AI_MAX_MESSAGES 64

#define AI_CONTEXT_LINES_DEFAULT 1000
#define AI_CONTEXT_LINES_MAX     50000
#define AI_CONTEXT_BYTES_MAX     ((size_t)8 * 1024 * 1024)  /* allocation ceiling */

/* Clamp a configured line count into [1, AI_CONTEXT_LINES_MAX]. */
int ai_context_clamp_lines(int lines);

/* Bytes to allocate to hold `lines` rows of `cols` columns (plus newlines and
 * a NUL terminator), clamped to AI_CONTEXT_BYTES_MAX. Never returns 0.
 * Sizes at 4 bytes/cell (worst-case UTF-8 encoding of a single codepoint)
 * so box-drawing/accented terminal output isn't truncated. */
size_t ai_context_buf_size(int lines, int cols);

typedef enum {
    AI_ROLE_SYSTEM,
    AI_ROLE_USER,
    AI_ROLE_ASSISTANT,
    AI_ROLE_TOOL          /* tool result message */
} AiRole;

typedef struct {
    char *base64_url;    /* "data:image/png;base64,..." (heap) */
    int width, height;   /* original pixel dimensions */
} AiAttachment;

typedef struct {
    AiRole role;
    char content[AI_MSG_MAX];       /* inline buffer — fits most messages */
    char *content_overflow;         /* heap-allocated when content > AI_MSG_MAX, else NULL */
    size_t content_len;             /* actual content length (regardless of which buffer) */
    AiAttachment *attachment;       /* NULL for text-only messages */

    /* Tool fields — zero/empty when unused */
    char tool_call_id[64];    /* set on ROLE_TOOL: the id this result is for */
    char tool_name[64];       /* set on ROLE_TOOL: tool name (for display) */
    int  is_tool_error;       /* set on ROLE_TOOL: 1 if this is an error result */
    char tool_note[AI_TOOL_NOTE_MAX]; /* ROLE_TOOL: Nutshell's own text, outside the data block */

    /* For ROLE_ASSISTANT messages that contain tool_use blocks */
    AiToolCall *tool_calls;   /* heap-allocated array, NULL when unused */
    int n_tool_calls;         /* 0 means normal text message */

    /* The reasoning the model streamed alongside this assistant turn's tool
     * calls, kept so a provider that demands it back (Moonshot's thinking
     * models: a 400 without it) gets it in the follow-up request. Heap, at
     * most AI_REASONING_MAX - 1 bytes, NULL when the turn had none. Only the
     * OpenAI-format builder reads it, and only for provider "moonshot"; the
     * Anthropic path and every other provider ignore it. Owned by the
     * message: ai_msg_free() frees it, ai_conv_copy() duplicates it, and
     * ai_conv_take() moves it with the rest. */
    char *reasoning;
} AiMessage;

/* Cap on a stored reasoning text, terminating NUL included: the same 16 KiB
 * the stream keeps for the panel's Thinking disclosure (AI_MSG_MAX). */
#define AI_REASONING_MAX AI_MSG_MAX

typedef struct {
    char model[64];
    AiMessage messages[AI_MAX_MESSAGES];
    int msg_count;
} AiConversation;

/* Initialize a conversation (zeroes and sets model). */
void ai_conv_init(AiConversation *conv, const char *model);

/* Reset a conversation: clear all messages but keep the model. */
void ai_conv_reset(AiConversation *conv);

/* Move a conversation: free dst's messages, give dst every message of src
 * together with the heap buffers they own (overflow content, attachments,
 * tool calls), and leave src empty with its model kept. dst keeps its own
 * model (the panel's setting is not reverted by a stale copy); only a dst
 * with an empty model takes src's. Afterwards exactly one conversation owns
 * each buffer, so both can be reset or taken again safely. dst == src, or
 * either NULL, is a no-op. */
void ai_conv_take(AiConversation *dst, AiConversation *src);

/* Deep copy: free dst's messages, then copy src into dst with its own
 * copies of every heap buffer. Returns 0; -1 on a NULL argument (dst
 * untouched) or allocation failure (dst left empty with src's model, never
 * half-shared). dst == src is a no-op returning 0. */
int ai_conv_copy(AiConversation *dst, const AiConversation *src);

/* Add a message. Returns 0 on success, -1 if full. */
int ai_conv_add(AiConversation *conv, AiRole role, const char *content);

/* Replace the content of the system message (messages[0]) in place, without
 * truncating to AI_MSG_MAX — used to refresh the system prompt with fresh
 * terminal context on every turn. Requires conv to be non-NULL, non-empty,
 * and messages[0].role == AI_ROLE_SYSTEM; content must be non-NULL.
 * Returns 0 on success, -1 on invalid arguments or allocation failure. */
int ai_conv_set_system(AiConversation *conv, const char *content);

/* Build the system prompt: the session's settings and rules, no terminal text.
 * session_notes, system_notes may be NULL.
 * kind selects the opening sentences: SESSION_SSH produces exactly the text
 * it always has, and SESSION_TELNET the same text naming a telnet session;
 * SESSION_LOCAL says the shell is a local one on a Windows PC
 * (spec 2026-09-22-local-shell-design.md section 6). shell_name names that
 * shell ("PowerShell", "Git bash", ...) and may be NULL, in which case the
 * prompt lists the possibilities. Ignored for SESSION_SSH and SESSION_TELNET. */
void ai_build_system_prompt(char *buf, size_t buf_size,
                            const char *session_notes,
                            const char *system_notes,
                            SessionKind kind,
                            const char *shell_name);

/* The same, plus (debug_log_tool != 0) the note that the read_debug_log tool
 * exists (ai_debug_tool_prompt_note(), ai_tools.h). Pass it only while the
 * tool is registered for the reply. `tools` is the registry whose tools the
 * reply carries, or NULL (provider without tool support, or none registered):
 * NULL or a registry with count 0 adds no tools paragraph. With
 * debug_log_tool == 0 and tools == NULL it is byte for byte the function
 * above.
 *
 * The system prompt holds settings only: no terminal text, no command results
 * (those travel in a delimited data block in the last user turn). */
void ai_build_system_prompt_ex(char *buf, size_t buf_size,
                               const char *session_notes,
                               const char *system_notes,
                               SessionKind kind,
                               const char *shell_name,
                               int debug_log_tool,
                               const AiToolRegistry *tools);

/* Build the JSON request body from the conversation.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_request_body(const AiConversation *conv,
                             char *buf, size_t buf_size);

/* Free an attachment and NULL the pointer. */
void ai_attachment_free(AiAttachment **att);

/* Deep-copy an attachment. Returns NULL on failure or if att is NULL. */
AiAttachment *ai_attachment_dup(const AiAttachment *att);

/* Build the JSON request body with explicit stream flag.
 * stream=1 adds "stream":true for SSE streaming responses.
 * last_user_attachment: if non-NULL, emits multimodal content array for the
 * last user message.
 * provider: e.g. "anthropic", "openai", NULL for generic OpenAI-compat format.
 *   For "anthropic": emits system message as top-level "system" key (required
 *   by Anthropic Messages API, array form with a cache_control marker) and
 *   adds required "max_tokens":8096.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_request_body_ex(const AiConversation *conv,
                                const AiAttachment *last_user_attachment,
                                char *buf, size_t buf_size, int stream,
                                const char *provider);

/* What a request body adds around the conversation (spec 2026-11-05
 * trust-boundary decisions 2 and 5). */
typedef struct {
    const char *snapshot;  /* terminal text for the last user turn; NULL or "": no block */
    const char *delim;     /* this request's delimiter; NULL: no block, tool output unframed */
} AiBodyFrame;

/* The full builder behind ai_build_request_body_ex and _tools.
 * tools_json: serialized tool definitions, or NULL/"" for none.
 * frame NULL, or frame->delim NULL: no terminal block and tool results are
 * sent unframed (content, then "\n" + tool_note when a note exists).
 * With a delimiter: the snapshot (if non-empty) goes in a data block
 * ("<lead>\n<delim>\n<escaped>\n<delim>") before the content of the LAST
 * AI_ROLE_USER message, in its text part when it has an attachment, and every
 * AI_ROLE_TOOL message is sent as a data block labelled with its tool name,
 * its tool_note after the closing delimiter (an empty result with a note sends
 * the note alone). conv is never modified. Anthropic's "system" is sent as an
 * array whose block carries cache_control ephemeral whenever a system message
 * is present, frame or not.
 * Returns bytes written (excluding NUL), or 0 on error (including a buffer
 * too small and an allocation failure). */
size_t ai_build_request_body_frame(const AiConversation *conv,
                                   const AiAttachment *last_user_attachment,
                                   const char *tools_json,
                                   const AiBodyFrame *frame,
                                   char *buf, size_t buf_size, int stream,
                                   const char *provider);

/* Parse a chat completion JSON response, extract assistant message content.
 * Returns 0 on success, -1 on error. */
int ai_parse_response(const char *json, char *content_out, size_t content_size);

/* Parse a single SSE streaming chunk (the JSON after "data: ").
 * Extracts delta.content and delta.reasoning_content from the chunk.
 * Also extracts actual token counts when present:
 *   - Anthropic message_start: input_tokens_out set from message.usage.input_tokens
 *   - Anthropic message_delta: output_tokens_out set from usage.output_tokens
 *   - OpenAI with top-level usage: prompt_tokens -> input_tokens_out,
 *                                  completion_tokens -> output_tokens_out
 * Any output pointer may be NULL if not needed.
 * Returns 0 on success, 1 if stream is done ([DONE]), -1 on error. */
int ai_parse_stream_chunk(const char *json,
                          char *content_out, size_t content_size,
                          char *thinking_out, size_t thinking_size,
                          int *input_tokens_out, int *output_tokens_out);

/* Extended stream chunk parser that also detects tool_use blocks.
 * content_out/thinking_out: same as ai_parse_stream_chunk.
 * tool_stream: if non-NULL, tool_use blocks are accumulated into this state.
 * provider: e.g. "anthropic", "openai" — determines SSE format.
 * Returns:
 *   0  — normal chunk processed (text delta, thinking delta, etc.)
 *   1  — stream done (end_turn/stop)
 *   2  — tool_use stop_reason detected (time to execute tools)
 *  -1  — error
 * An OpenAI-format chunk that carries a finish_reason ("stop" -> 1,
 * "tool_calls" -> 2) still fills content_out/thinking_out, and its
 * delta.tool_calls are accumulated: the caller reads the text, then acts on
 * the code. Whether the tool loop runs is ai_tool_round_should_run()'s
 * decision at the end of the round, not this code's. */
int ai_parse_stream_chunk_ex(const char *json,
                             char *content_out, size_t content_size,
                             char *thinking_out, size_t thinking_size,
                             AiToolStreamState *tool_stream,
                             const char *provider);

/* The setting "Commands as tool calls" (Settings.ai_runcmd_mode). Automatic
 * resolves through ai_runcmd_auto_default(); On enables any provider; Off
 * leaves [EXEC] markers wherever they are today. */
enum { AI_RUNCMD_AUTO = 0, AI_RUNCMD_ON = 1, AI_RUNCMD_OFF = 2 };

/* What Automatic means for a provider: 1 for anthropic, openai and moonshot,
 * 0 for deepseek, gemini, custom and anything else (NULL included). The one
 * table; flipping a row is one line. */
int ai_runcmd_auto_default(const char *provider);

/* 1 if the provider gets native tools (web_search, web_fetch, read_debug_log
 * and, with ai_provider_runcmd(), run_command) in a request. anthropic,
 * openai, deepseek and moonshot always; gemini and custom only when mode is
 * AI_RUNCMD_ON (the user's statement that the endpoint accepts them); any
 * other provider and NULL never. */
int ai_provider_tools(const char *provider, int mode);

/* Returns 1 if the provider supports native tool use with the setting left
 * on Automatic, 0 otherwise: ai_provider_tools(provider, AI_RUNCMD_AUTO). */
int ai_provider_supports_tools(const char *provider);

/* 1 when a request to this provider offers the run_command tool: Off never;
 * On wherever ai_provider_tools(provider, AI_RUNCMD_ON) holds; Automatic as
 * ai_runcmd_auto_default() says. */
int ai_provider_runcmd(const char *provider, int mode);

/* Output limit sent as "max_completion_tokens" to provider "moonshot" (and
 * only to it). Without one the server's default is far lower, and a thinking
 * model's reasoning counts against it, so a long [EXEC] reply was cut off
 * before its closing marker. A model whose context window is known to be
 * under AI_MOONSHOT_OUTPUT_BIG_CTX gets half of it (moonshot-v1-8k: 4000,
 * moonshot-v1-32k: 16000); every other model, one that is not in
 * ai_model_context_limit()'s table included, gets AI_MOONSHOT_OUTPUT_MAX. */
#define AI_MOONSHOT_OUTPUT_MAX     16384
#define AI_MOONSHOT_OUTPUT_BIG_CTX 65536
int ai_moonshot_output_limit(const char *model);

/* 1 when an OpenAI-format stream chunk (the JSON after "data: ") ends the
 * reply because it hit the provider's output limit: choices[0].finish_reason
 * is "length". 0 for any other chunk, malformed JSON or NULL. */
int ai_stream_chunk_cut_off(const char *json);

/* Extended parse: also extract reasoning/thinking content (e.g. DeepSeek
 * reasoner's "reasoning_content" field).  thinking_out may be NULL.
 * Returns 0 on success, -1 on error. */
int ai_parse_response_ex(const char *json, char *content_out, size_t content_size,
                          char *thinking_out, size_t thinking_size);

/* Extract the first live command from [EXEC]...[/EXEC] markers in an AI
 * response (a marker quoted in code is skipped; see
 * ai_extract_commands_full()). Returns 1 if found, 0 otherwise. */
int ai_extract_command(const char *response, char *cmd_out, size_t cmd_size);

/* Extract up to max_cmds commands from [EXEC]...[/EXEC] markers.
 * Each command is written into cmds[i] (each of cmd_size bytes).
 * Returns the number of commands found (0 if none).
 *
 * A block is trimmed of surrounding whitespace/CR/LF, then dropped (not
 * returned, not sanitised) if what remains still contains a control
 * character (< 0x20 or 0x7F) -- e.g. an embedded newline that would let
 * one [EXEC] block smuggle a second, unclassified command. A block whose
 * trimmed payload is 1024 bytes or longer (too long for the 1023-byte
 * cmds[] slot plus NUL) is also dropped, never truncated. Use
 * ai_extract_commands_ex() to find out how many blocks were dropped, and
 * why. */
int ai_extract_commands(const char *response, char cmds[][1024],
                        int max_cmds);

/* Same as ai_extract_commands(), but also reports how many [EXEC] blocks
 * were dropped, and why:
 *   *rejected      -- blocks whose trimmed payload still contained a
 *                      control character (set to 0 up front, incremented
 *                      per drop).
 *   *rejected_long -- blocks whose trimmed payload was 1024 bytes or
 *                      longer, i.e. would not fit cmds[][1024] (CMD_MAX_LEN
 *                      1023 plus NUL) without truncation. These are
 *                      skipped entirely: no cmds[] slot is filled, no
 *                      count is added. The length is measured from the
 *                      [EXEC]/[/EXEC] span itself (after the same
 *                      whitespace trim as an accepted block), so an
 *                      oversized block never needs to be copied to be
 *                      recognised as oversized. (Also set to 0 up front,
 *                      incremented per drop.)
 * Either out-parameter may be NULL. */
int ai_extract_commands_ex(const char *response, char cmds[][1024],
                           int max_cmds, int *rejected, int *rejected_long);

/* CMD_NOTE_MAX (the longest note, with the NUL) is defined in
 * chat_approval.h, which this header includes. */

/* Same acceptance rules and the same return as ai_extract_commands_ex();
 * additionally fills notes[i] for every ACCEPTED block i, so index i
 * matches cmds[i] by construction. notes may be NULL (then this is
 * ai_extract_commands_ex). The note is the LAST paragraph (text after the
 * final blank line) of the prose between the end of the previous [EXEC]
 * block -- accepted or dropped -- and this block's [EXEC]. It is trimmed,
 * markdown fence lines removed, whitespace runs collapsed to one space,
 * C0/C1 controls and Unicode bidi controls removed, and cut at the last
 * UTF-8 boundary that fits CMD_NOTE_MAX-1 bytes. A note containing no
 * letter (a bare "2." or "-") is stored as empty; a byte >= 0x80 counts as
 * a letter, so non-Latin text is kept. */
int ai_extract_commands_notes(const char *response, char cmds[][1024],
                              char notes[][CMD_NOTE_MAX], int max_cmds,
                              int *rejected, int *rejected_long);

/* The general form of ai_extract_commands_notes(): it can also report the
 * [EXEC] markers the reply quoted in code.
 *
 * A marker is quoted when its opening [EXEC] lies inside a fenced code block
 * (the renderer's fences: three backticks at column 0; an unterminated fence
 * runs to the end of the reply) or inside an inline code span, found with
 * md_parse_inline() exactly as the renderer parses the line (so bold or
 * italic spans take backticks first, and an unpaired backtick opens
 * nothing). The one scanner, src/core/exec_scan.h, also decides what the
 * chat panel draws as a command. A marker outside any span is live even if
 * its command contains backticks.
 *
 * quoted[] (max_cmds entries) non-NULL: live and quoted markers are returned
 * together in reply order, quoted[i] is 1 for a quoted one. A quoted marker
 * takes a slot and goes through the same trim, length and control-character
 * checks as a live one, counting in *rejected / *rejected_long the same way.
 * quoted == NULL: quoted markers are skipped before any check -- no slot, no
 * count -- which is what ai_extract_commands_notes() and everything built on
 * it do, so a caller that does not ask for quoted markers never sees one as
 * a command. notes may be NULL. */
int ai_extract_commands_full(const char *response, char cmds[][1024],
                             char notes[][CMD_NOTE_MAX], int quoted[],
                             int max_cmds, int *rejected, int *rejected_long);

/* Get the API endpoint URL for a provider name.
 * Returns NULL for unknown providers. */
const char *ai_provider_url(const char *provider);

/* Get the default model name for a provider.
 * Returns NULL for unknown providers. */
const char *ai_provider_model(const char *provider);

/* Get a NULL-terminated array of model names for a provider.
 * Returns NULL for unknown/custom providers. */
const char * const *ai_provider_models(const char *provider);

/* Get the models list API endpoint URL for a provider.
 * Returns NULL for unknown/custom providers. */
const char *ai_provider_models_url(const char *provider);

/* Build a NULL-terminated array of auth headers for a provider's chat API.
 * provider:  e.g. "anthropic", "openai", "deepseek"
 * api_key:   the raw API key string
 * hdr0/hdr1: caller-supplied buffers (each at least 300 bytes)
 * out:        filled with up to 2 header pointers + NULL terminator
 *
 * Anthropic → {"x-api-key: <key>", "anthropic-version: 2023-06-01", NULL}
 * All others → {"Authorization: Bearer <key>", NULL}
 */
void ai_build_auth_headers(const char *provider, const char *api_key,
                            char *hdr0, size_t hdr0_size,
                            char *hdr1, size_t hdr1_size,
                            const char *out[3]);

/* Build a confirmation dialog string listing all commands for batch approval.
 * cmds: array of command strings, ncmds: count.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_confirm_text(char cmds[][1024], int ncmds,
                              char *buf, size_t buf_size);

/* Build the "continue" message sent to the AI after a pending command
 * batch finishes running, or is stopped/abandoned (see docs/superpowers/
 * specs/2026-09-09-pending-command-batches.md, rule 4, and
 * docs/superpowers/specs/2026-09-23-command-dispatch-states-design.md
 * section 3). When newer_exchanges is 0 (no other exchange has happened
 * since this batch's card appeared), the opening is the plain "the
 * commands above" text unchanged from before batching existed. When
 * newer_exchanges > 0, the opening names which batch just ran via its
 * first command, since "above" would otherwise be ambiguous once newer
 * exchanges are in between. first_cmd may be NULL/empty only when
 * newer_exchanges == 0.
 *
 * q is the batch's queue and may be NULL, in which case the function
 * behaves exactly as it always has (only the opening text, unchanged).
 * When q is non-NULL and any entry is still APPROVE_APPROVED or
 * APPROVE_EXECUTING, the batch was stopped mid-flight: the opening
 * ignores newer_exchanges/first_cmd entirely and becomes "The command
 * batch was stopped before it finished. Do not re-send the commands that
 * did not run unless I ask for them." -- never the "have been executed"
 * or "include ALL of them" wording, since neither is true of a batch that
 * didn't finish.
 * When q is non-NULL and q->count > 0, the opening is followed by a
 * blank line, one short sentence telling the model these are the actual
 * per-command outcomes and it must not claim a command ran when its line
 * says otherwise, and then one numbered line per queue entry in queue
 * order ("  1. ran: printf ...\n"), the command elided to 60 characters
 * (adding "..." when longer, never splitting a UTF-8 sequence). The
 * outcome word comes from the entry's ApprovalStatus: APPROVE_COMPLETED
 * "ran", APPROVE_EXECUTING "started, stopped before it finished",
 * APPROVE_APPROVED "not run (batch stopped)", APPROVE_DENIED "not run
 * (denied)", APPROVE_BLOCKED "not run (above the command policy
 * ceiling)", APPROVE_PENDING "not run (never decided)". A quoted entry
 * (ApprovalEntry.quoted, an [EXEC] marker found inside code) is listed as
 * "quoted in a code block, not run" whatever its status.
 *
 * Callers should pass a 4096-byte buffer and check the return value:
 * every snprintf inside is checked, and the function returns 0 (writing
 * nothing usable) if anything would overflow buf_size, rather than
 * silently truncating the note the model receives. */
size_t ai_build_continue_text(int newer_exchanges, const char *first_cmd,
                              const ApprovalQueue *q,
                              char *buf, size_t buf_size);

/* The corrective note injected into the conversation when the user raises
 * the policy ceiling to `allowed_stop` (a CmdSafetyLevel). Without it the
 * AI still has the earlier "these were BLOCKED" message in its history and
 * keeps telling the user to change a setting they have just changed.
 * Lives here, rather than at the one call site in src/ui/ai_chat_status.c, so the
 * wording the model actually receives is natively testable.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_policy_raised_note(int allowed_stop, char *buf, size_t buf_size);

/* The note listing commands that were NOT run because they sat above the
 * policy ceiling. `list` is the already-formatted "  - <command>\n" block.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_policy_blocked_note(const char *list, char *buf, size_t buf_size);

/* Split an AI response into pre-command and post-command text.
 * pre_cmd receives text before the first [EXEC] marker.
 * post_cmd receives text after the last [/EXEC] marker.
 * Either output buffer may be NULL (with size 0) to skip.
 * Returns the number of live [EXEC]...[/EXEC] command pairs found; a marker
 * quoted in code (ai_extract_commands_full()) is neither counted nor a cut
 * point. */
int ai_response_split(const char *response,
                      char *pre_cmd, size_t pre_size,
                      char *post_cmd, size_t post_size);

/* Returns pointer to message content (inline or overflow buffer) */
const char *ai_msg_content(const AiMessage *msg);

/* Sets message content. Handles inline vs overflow allocation.
 * Returns 0 on success, -1 on allocation failure. */
int ai_msg_set_content(AiMessage *msg, const char *data, size_t len);

/* Store `text` as msg's reasoning (see AiMessage), replacing any earlier one.
 * Kept to AI_REASONING_MAX - 1 bytes, cut on a UTF-8 boundary. NULL or ""
 * clears it. Returns 0, or -1 on allocation failure (reasoning cleared). */
int ai_msg_set_reasoning(AiMessage *msg, const char *text);

/* Frees dynamic resources (content_overflow, tool_calls, reasoning,
 * attachment). */
void ai_msg_free(AiMessage *msg);

/* Build the JSON request body with tool definitions.
 * tools_json: serialized tool definitions array (e.g. from ai_tools_serialize_*), or NULL.
 * If tools_json is NULL, behaves identically to ai_build_request_body_ex.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_request_body_tools(const AiConversation *conv,
                                    const AiAttachment *last_user_attachment,
                                    const char *tools_json,
                                    char *buf, size_t buf_size,
                                    int stream, const char *provider);

/* Count words in text (whitespace-delimited). Returns 0 for NULL/empty. */
int ai_word_count(const char *text);

/* Format a context bar label string.
 * tokens: current estimated token count.
 * limit: model context window size (0 = unknown → "Context: N/A").
 * buf/buf_size: output buffer.
 * Returns bytes written (excluding NUL). */
int ai_format_context_label(int tokens, int limit, char *buf, size_t buf_size);

/* Build a multi-line tooltip string describing context window usage.
 *
 * Behaviour:
 *  - If actual_in + actual_out > 0: shows separate "Input tokens" and
 *    "Output tokens" lines plus a "Total" line.
 *  - Otherwise: shows a single "Total (estimated)" line using
 *    estimated_total, prefixed with '~'.
 *  - If context_limit <= 0: omits the "/ <limit> (<pct>%)" tail and
 *    appends a "Limit: unknown" line.
 *  - If model_name is NULL or empty: the "Model:" line is omitted.
 *
 * Numbers >= 1000 are formatted with thousands separators (commas).
 *
 * Returns the number of bytes written (excluding NUL), or 0 if buf is
 * NULL or buf_size == 0. Output is always NUL-terminated when buf_size
 * >= 1.
 */
int ai_format_context_tooltip(int actual_in, int actual_out,
                              int estimated_total,
                              int context_limit,
                              const char *model_name,
                              char *buf, size_t buf_size);

/* Get the context window token limit for a model name.
 * Returns 0 for unknown models. */
int ai_model_context_limit(const char *model);

/* Estimate total tokens in a conversation (chars/4 heuristic). */
int ai_context_estimate_tokens(const AiConversation *conv);

/* The same estimate plus the terminal snapshot the next request carries in
 * its last user turn: ai_context_estimate_tokens(conv) + snapshot_len / 4.
 * The snapshot is not in conv (it is added when the body is built), so the
 * meter has to add it. */
int ai_context_estimate_tokens_ex(const AiConversation *conv, size_t snapshot_len);

/* Compact a conversation: keep system prompt (msg[0]) and last
 * keep_recent*2 messages (user+assistant pairs). Removes older messages.
 * Returns number of messages removed, or 0 if nothing to compact. */
int ai_conv_compact(AiConversation *conv, int keep_recent);

/* Format a command execution progress string.
 * current: 1-based index of current command.
 * total: total number of commands.
 * buf/buf_size: output buffer.
 * Returns bytes written (excluding NUL). */
int ai_cmd_progress_text(int current, int total, char *buf, size_t buf_size);

/* Format a "waiting for output" status string.
 * buf/buf_size: output buffer.
 * Returns bytes written (excluding NUL). */
int ai_cmd_waiting_text(char *buf, size_t buf_size);

/* Build a plain-text save of a conversation for "Save As".
 * Skips the system prompt (msg[0]).  If show_thinking is non-zero and
 * thinking[i] is non-NULL for an assistant message at index i, a
 * "--- Thinking ---" block is included before the AI response.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_save_text(const AiConversation *conv,
                           char *const *thinking, int show_thinking,
                           char *buf, size_t buf_size);

/* Key action for AI chat input field. */
typedef enum {
    AI_INPUT_SEND,       /* Enter without Shift: send message */
    AI_INPUT_NEWLINE,    /* Shift+Enter: insert newline */
    AI_INPUT_PASSTHROUGH /* Not Enter: pass to default handler */
} AiInputAction;

/* Decide what to do when a key is pressed in the AI chat input.
 * is_enter: 1 if VK_RETURN/Enter, 0 otherwise.
 * shift_held: 1 if Shift is down, 0 otherwise. */
AiInputAction ai_input_key_action(int is_enter, int shift_held);

/* Per-session AI state: wraps a conversation with a validity flag.
 * Sessions embed this so the AI chat window can save/restore conversations
 * when the user switches tabs. */
typedef struct {
    AiConversation conv;
    int valid;  /* 0 = never used, 1 = has saved conversation */
    /* Pending command approval batches (see src/core/cmd_batch.h and
     * docs/superpowers/specs/2026-09-09-pending-command-batches.md): any
     * number of cards may be pending at once, each with its own
     * ApprovalQueue. Replaces the old single pending_approval/pending_cmds
     * snapshot -- cards are rebuilt straight from this set on session
     * switch instead. Initialized with cmd_batch_set_init() when the
     * session is created, freed with cmd_batch_set_free() when it's
     * destroyed. */
    CmdBatchSet batches;
    /* The commands of finished or cancelled batches, kept so a rebuilt display
     * can show what ran again (src/core/cmd_run_log.h, spec 2026-11-05
     * run-command tool, decision 21). cmd_run_log_init() beside each
     * cmd_batch_set_init(), cmd_run_log_free() beside each
     * cmd_batch_set_free(). */
    CmdRunLog run_log;
    /* Per-session streaming state (supports concurrent AI requests) */
    volatile int busy;           /* 1 while a stream thread is running */
    char *stream_content;        /* heap-allocated accumulator, NULL when idle */
    size_t stream_content_len;
    char *stream_thinking;       /* heap-allocated accumulator, NULL when idle */
    size_t stream_thinking_len;
    int stream_phase;            /* 0=not started, 1=thinking, 2=content */
    int platform;                /* CmdPlatform for this session */
    int platform_contradicted;   /* sticky: host output disagreed with
                                  * `platform` after it resolved (cmd_detect.h).
                                  * Set, the session's commands are classified
                                  * under the worse of `platform` and
                                  * CMD_PLATFORM_UNKNOWN (cmd_classify_session());
                                  * never cleared for the session's life. */
    CmdPolicy policy;            /* This session's command policy: the `allowed`
                                   * ceiling and the `unattended` marker
                                   * (src/core/cmd_policy.h) */
    int policy_seeded;           /* 1 once `policy` has been set for this session
                                   * (from the configured default, or by the
                                   * user) -- guards against re-seeding an
                                   * explicit choice on a later session switch */
    int activity_phase;          /* ActivityPhase saved on session switch */
    int show_thinking;           /* 1 = user opened a Thinking disclosure this
                                   * session -- suppresses auto-collapse when a
                                   * reply's text starts (see chat_listview.c) */
    int auto_rounds;             /* consecutive unattended continue rounds
                                   * since the user last spoke or acted
                                   * (ai_rounds_*() below); 0 at creation */
    /* Unattended tool rounds (trust-boundary spec decision 22), separate from
     * auto_rounds; see ai_tool_rounds_*() below. Zero at creation. */
    int auto_tool_rounds;        /* rounds in which a non-SAFE tool ran
                                  * unattended since the user last spoke or
                                  * clicked Run on a tool row */
    unsigned tool_round_stream;  /* round key: stream id of the last round
                                  * seen (never 0 in the product) ... */
    int tool_round_iter;         /* ... and its loop_iter */
    int tool_round_base;         /* auto_tool_rounds before that round began */
    int tool_round_counted;      /* 1 once that round has added its 1 */
    int tool_pause_shown;        /* 1 once the pause row was posted since the
                                  * last reset */
} AiSessionState;

/* Cap on unattended rounds with Auto-approve on: the batch built after
 * AI_AUTO_ROUNDS_MAX consecutive unattended rounds is built attended
 * (guard-rails spec 2026-11-04 section 1, "The counter"). */
#define AI_AUTO_ROUNDS_MAX 3

/* The user typed a message: auto_rounds = 0. NULL-safe. */
void ai_rounds_user_message(AiSessionState *s);

/* A continue message is being sent after a finished batch: auto_rounds += 1
 * when that batch ran `unattended` (non-zero), else auto_rounds = 0.
 * NULL-safe. */
void ai_rounds_continue(AiSessionState *s, int unattended);

/* The policy to build the next batch under: `p`, or cmd_policy_attended(p)
 * once auto_rounds >= AI_AUTO_ROUNDS_MAX. The session's own policy is not
 * changed. NULL `s` returns `p`. */
CmdPolicy ai_rounds_batch_policy(const AiSessionState *s, CmdPolicy p);

/* 1 when the cap is reached and Auto-approve is on in `p` (the batch built
 * under ai_rounds_batch_policy() is paused for the user); else 0.
 * NULL `s` returns 0. */
int ai_rounds_paused(const AiSessionState *s, CmdPolicy p);

/* ---- Unattended tool rounds (trust-boundary spec decision 22) ---- */

/* Cap on unattended tool rounds (Settings > AI Assistant > Behaviour,
 * "Automatic tool rounds"): a non-SAFE tool call in the round after `cap`
 * counted rounds asks on a card. The setting is 1..20, default 3. */
#define AI_TOOL_ROUNDS_DEFAULT 3
#define AI_TOOL_ROUNDS_MIN     1
#define AI_TOOL_ROUNDS_MAX     20

/* `cap` limited to AI_TOOL_ROUNDS_MIN..AI_TOOL_ROUNDS_MAX: 0 and negatives
 * give 1, anything above 20 gives 20. */
int ai_tool_rounds_clamp(int cap);

/* Write the one row posted when the cap first forces an ask,
 * "[Auto-approve paused after N automatic tool rounds]", N = the clamped
 * `cap` ("rounds" stays plural for 1). Always NUL-terminated and truncated
 * to `n`; nothing is written when `buf` is NULL or `n` is 0. */
void ai_tool_pause_row(char *buf, size_t n, int cap);

/* A non-SAFE tool call in round (stream_id, loop_iter) is about to be judged.
 * The first call with a new key records it with tool_round_base =
 * auto_tool_rounds and tool_round_counted = 0; every call in the round is then
 * judged on that pre-round count. Returns 1 (ask on a card) when
 * tool_round_base >= ai_tool_rounds_clamp(cap), else 0. `cap` is the cap in
 * force for that stream (AiStream.tool_auto_rounds). NULL-safe (0). */
int ai_tool_rounds_attended(AiSessionState *s, unsigned stream_id, int loop_iter,
                            int cap);

/* A non-SAFE tool call in that round ran on RUN: auto_tool_rounds += 1, once
 * per key (ignored when the key does not match the stored one or the round
 * was already counted). NULL-safe. */
void ai_tool_rounds_ran_unattended(AiSessionState *s, unsigned stream_id, int loop_iter);

/* A typed message, or a Run click on a tool row: auto_tool_rounds = 0 and
 * tool_pause_shown = 0. The stored key and base are kept, so a call later in
 * the same round is still judged on the pre-round count. NULL-safe. */
void ai_tool_rounds_reset(AiSessionState *s);

/* 1 the first time after a reset (post the ai_tool_pause_row() text), then 0.
 * NULL-safe (0). */
int ai_tool_rounds_take_pause_row(AiSessionState *s);

#endif /* NUTSHELL_AI_PROMPT_H */
