#ifndef NUTSHELL_AI_PROMPT_H
#define NUTSHELL_AI_PROMPT_H

#include <stddef.h>

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

/* Add a message. Returns 0 on success, -1 if full. */
int ai_conv_add(AiConversation *conv, AiRole role, const char *content);

/* Build the system prompt with terminal context embedded.
 * terminal_text may be NULL. */
void ai_build_system_prompt(char *buf, size_t buf_size,
                            const char *terminal_text);

/* Build the JSON request body from the conversation.
 * Returns bytes written (excluding NUL), or 0 on error. */
size_t ai_build_request_body(const AiConversation *conv,
                             char *buf, size_t buf_size);

/* Parse a chat completion JSON response, extract assistant message content.
 * Returns 0 on success, -1 on error. */
int ai_parse_response(const char *json, char *content_out, size_t content_size);

/* Extract a command from [EXEC]...[/EXEC] markers in AI response.
 * Returns 1 if found, 0 otherwise. */
int ai_extract_command(const char *response, char *cmd_out, size_t cmd_size);

/* Get the API endpoint URL for a provider name.
 * Returns NULL for unknown providers. */
const char *ai_provider_url(const char *provider);

/* Get the default model name for a provider.
 * Returns NULL for unknown providers. */
const char *ai_provider_model(const char *provider);

#endif /* NUTSHELL_AI_PROMPT_H */
