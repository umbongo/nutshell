#ifndef NUTSHELL_AI_PROMPT_H
#define NUTSHELL_AI_PROMPT_H

#include <stddef.h>

/* Default AI provider — must match the first entry in the provider list
 * shown in the Settings dialog (settings.c k_ai_providers[]). */
#define AI_DEFAULT_PROVIDER "anthropic"

#define AI_MSG_MAX     8192
#define AI_BODY_MAX    65536
#define AI_MAX_MESSAGES 64

typedef enum {
    AI_ROLE_SYSTEM,
    AI_ROLE_USER,
    AI_ROLE_ASSISTANT
} AiRole;

typedef struct {
    AiRole role;
    char content[AI_MSG_MAX];
} AiMessage;

typedef struct {
    char model[64];
    AiMessage messages[AI_MAX_MESSAGES];
    int msg_count;
} AiConversation;

/* Initialize a conversation (zeroes and sets model). */
void ai_conv_init(AiConversation *conv, const char *model);

/* Reset a conversation: clear all messages but keep the model. */
void ai_conv_reset(AiConversation *conv);

/* Add a message. Returns 0 on success, -1 if full. */
int ai_conv_add(AiConversation *conv, AiRole role, const char *content);

/* Build the system prompt with terminal context embedded.
 * terminal_text, session_notes, system_notes may be NULL. */
void ai_build_system_prompt(char *buf, size_t buf_size,
                            const char *terminal_text,
                            const char *session_notes,
                            const char *system_notes);

/* Build the JSON request body from the conversation.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_request_body(const AiConversation *conv,
                             char *buf, size_t buf_size);

/* Build the JSON request body with explicit stream flag.
 * stream=1 adds "stream":true for SSE streaming responses.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_request_body_ex(const AiConversation *conv,
                                char *buf, size_t buf_size, int stream);

/* Parse a chat completion JSON response, extract assistant message content.
 * Returns 0 on success, -1 on error. */
int ai_parse_response(const char *json, char *content_out, size_t content_size);

/* Parse a single SSE streaming chunk (the JSON after "data: ").
 * Extracts delta.content and delta.reasoning_content from the chunk.
 * Either output may be NULL if not needed.
 * Returns 0 on success, 1 if stream is done ([DONE]), -1 on error. */
int ai_parse_stream_chunk(const char *json,
                          char *content_out, size_t content_size,
                          char *thinking_out, size_t thinking_size);

/* Extended parse: also extract reasoning/thinking content (e.g. DeepSeek
 * reasoner's "reasoning_content" field).  thinking_out may be NULL.
 * Returns 0 on success, -1 on error. */
int ai_parse_response_ex(const char *json, char *content_out, size_t content_size,
                          char *thinking_out, size_t thinking_size);

/* Extract a command from [EXEC]...[/EXEC] markers in AI response.
 * Returns 1 if found, 0 otherwise. */
int ai_extract_command(const char *response, char *cmd_out, size_t cmd_size);

/* Extract up to max_cmds commands from [EXEC]...[/EXEC] markers.
 * Each command is written into cmds[i] (each of cmd_size bytes).
 * Returns the number of commands found (0 if none). */
int ai_extract_commands(const char *response, char cmds[][1024],
                        int max_cmds);

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

/* Build a confirmation dialog string listing all commands for batch approval.
 * cmds: array of command strings, ncmds: count.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_confirm_text(char cmds[][1024], int ncmds,
                              char *buf, size_t buf_size);

/* Check if a shell command is read-only (does not modify files or system state).
 * Returns 1 if read-only, 0 if the command may write/modify. */
int ai_command_is_readonly(const char *cmd);

/* Split an AI response into pre-command and post-command text.
 * pre_cmd receives text before the first [EXEC] marker.
 * post_cmd receives text after the last [/EXEC] marker.
 * Either output buffer may be NULL (with size 0) to skip.
 * Returns the number of [EXEC]...[/EXEC] command pairs found. */
int ai_response_split(const char *response,
                      char *pre_cmd, size_t pre_size,
                      char *post_cmd, size_t post_size);

/* Count words in text (whitespace-delimited). Returns 0 for NULL/empty. */
int ai_word_count(const char *text);

/* Format a context bar label string.
 * tokens: current estimated token count.
 * limit: model context window size (0 = unknown → "Context: N/A").
 * buf/buf_size: output buffer.
 * Returns bytes written (excluding NUL). */
int ai_format_context_label(int tokens, int limit, char *buf, size_t buf_size);

/* Get the context window token limit for a model name.
 * Returns 0 for unknown models. */
int ai_model_context_limit(const char *model);

/* Estimate total tokens in a conversation (chars/4 heuristic). */
int ai_context_estimate_tokens(const AiConversation *conv);

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
    /* Pending command approval (heap-allocated to keep struct small) */
    int pending_approval;
    int pending_cmd_count;
    char (*pending_cmds)[1024];  /* heap array, NULL when not pending */
    /* Per-session streaming state (supports concurrent AI requests) */
    volatile int busy;           /* 1 while a stream thread is running */
    char *stream_content;        /* heap-allocated accumulator, NULL when idle */
    size_t stream_content_len;
    char *stream_thinking;       /* heap-allocated accumulator, NULL when idle */
    size_t stream_thinking_len;
    int stream_phase;            /* 0=not started, 1=thinking, 2=content */
    int platform;                /* CmdPlatform for this session */
    int auto_approve;            /* Session-level auto-approve flag */
    int activity_phase;          /* ActivityPhase saved on session switch */
} AiSessionState;

#endif /* NUTSHELL_AI_PROMPT_H */
