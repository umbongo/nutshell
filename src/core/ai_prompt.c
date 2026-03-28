#include "ai_prompt.h"
#include "cmd_classify.h"
#include "json_parser.h"
#include <stdio.h>
#include <string.h>

void ai_conv_init(AiConversation *conv, const char *model)
{
    if (!conv) return;
    memset(conv, 0, sizeof(*conv));
    if (model)
        snprintf(conv->model, sizeof(conv->model), "%s", model);
}

void ai_conv_reset(AiConversation *conv)
{
    if (!conv) return;
    char model[64];
    memcpy(model, conv->model, sizeof(model));
    memset(conv, 0, sizeof(*conv));
    memcpy(conv->model, model, sizeof(conv->model));
}

int ai_conv_add(AiConversation *conv, AiRole role, const char *content)
{
    if (!conv || !content) return -1;
    if (conv->msg_count >= AI_MAX_MESSAGES) return -1;

    AiMessage *m = &conv->messages[conv->msg_count];
    m->role = role;
    snprintf(m->content, sizeof(m->content), "%s", content);
    conv->msg_count++;
    return 0;
}

void ai_build_system_prompt(char *buf, size_t buf_size,
                            const char *terminal_text,
                            const char *session_notes,
                            const char *system_notes)
{
    if (!buf || buf_size == 0) return;

    const char *base =
        "You are an AI assistant for an SSH terminal session. "
        "You can see the terminal output and help the user with tasks.\n"
        "When you want to execute a command on the remote server, "
        "wrap it in [EXEC] and [/EXEC] markers like this:\n"
        "[EXEC]ls -la[/EXEC]\n\n"
        "IMPORTANT: Always include ALL commands needed in a SINGLE response. "
        "Do NOT split commands across multiple responses. "
        "Each command must have its own [EXEC]...[/EXEC] markers.\n"
        "Example with multiple commands:\n"
        "1. First, check disk usage\n[EXEC]df -h[/EXEC]\n"
        "2. Then check memory\n[EXEC]free -m[/EXEC]\n"
        "3. Finally check uptime\n[EXEC]uptime[/EXEC]\n\n"
        "Always explain what each command does before its marker. "
        "Never say 'let's start with' or do only the first step. "
        "Include every command the user needs in one response.";

    size_t pos = 0;
    int n = snprintf(buf, buf_size, "%s", base);
    if (n < 0) return;
    pos = (size_t)n;

    if (system_notes && system_notes[0] && pos < buf_size) {
        n = snprintf(buf + pos, buf_size - pos,
                     "\n\nUser's system-wide instructions:\n%s", system_notes);
        if (n > 0) pos += (size_t)n;
    }
    if (session_notes && session_notes[0] && pos < buf_size) {
        n = snprintf(buf + pos, buf_size - pos,
                     "\n\nAbout this server:\n%s", session_notes);
        if (n > 0) pos += (size_t)n;
    }
    if (terminal_text && terminal_text[0] && pos < buf_size) {
        snprintf(buf + pos, buf_size - pos,
                 "\n\nCurrent terminal output:\n```\n%s\n```", terminal_text);
    }
}

int ai_word_count(const char *text)
{
    if (!text) return 0;
    int count = 0;
    int in_word = 0;
    for (const char *p = text; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            count++;
        }
    }
    return count;
}

int ai_model_context_limit(const char *model)
{
    if (!model) return 0;
    static const struct { const char *name; int tokens; } limits[] = {
        {"deepseek-chat",              64000},
        {"deepseek-coder",            128000},
        {"deepseek-reasoner",          64000},
        {"gpt-4o",                    128000},
        {"gpt-4o-mini",               128000},
        {"gpt-4-turbo",               128000},
        {"o3-mini",                   200000},
        {"gpt-3.5-turbo",             16000},
        {"claude-sonnet-4-6",         200000},
        {"claude-haiku-4-5-20251001", 200000},
        {"claude-opus-4-6",           200000},
        {"kimi-k2",                   128000},
        {"moonshot-v1-8k",              8000},
        {"moonshot-v1-32k",            32000},
        {"moonshot-v1-128k",          128000},
        {NULL, 0}
    };
    for (int i = 0; limits[i].name; i++) {
        if (strcmp(model, limits[i].name) == 0)
            return limits[i].tokens;
    }
    return 0;
}

int ai_context_estimate_tokens(const AiConversation *conv)
{
    if (!conv) return 0;
    int total_chars = 0;
    for (int i = 0; i < conv->msg_count; i++)
        total_chars += (int)strlen(conv->messages[i].content);
    return total_chars / 4;
}

int ai_conv_compact(AiConversation *conv, int keep_recent)
{
    if (!conv || keep_recent <= 0) return 0;
    int keep_msgs = keep_recent * 2;
    if (conv->msg_count <= 1 + keep_msgs) return 0;

    int remove_start = 1;
    int remove_end = conv->msg_count - keep_msgs;
    int remove_count = remove_end - remove_start;
    if (remove_count <= 0) return 0;

    memmove(&conv->messages[remove_start],
            &conv->messages[remove_end],
            (size_t)(conv->msg_count - remove_end) * sizeof(AiMessage));
    conv->msg_count -= remove_count;
    return remove_count;
}

/* Write a JSON-escaped string (with quotes) into buf at position pos.
 * Returns new position, or 0 on overflow. */
static size_t json_escape_str(const char *s, char *buf, size_t buf_size,
                              size_t pos)
{
    if (pos >= buf_size) return 0;
    buf[pos++] = '"';

    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        const char *esc = NULL;
        char u_esc[7];

        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if (c < 0x20) {
                    snprintf(u_esc, sizeof(u_esc), "\\u%04x", (unsigned)c);
                    esc = u_esc;
                }
                break;
        }

        if (esc) {
            size_t len = strlen(esc);
            if (pos + len >= buf_size) return 0;
            memcpy(buf + pos, esc, len);
            pos += len;
        } else {
            if (pos + 1 >= buf_size) return 0;
            buf[pos++] = (char)c;
        }
    }

    if (pos + 1 >= buf_size) return 0;
    buf[pos++] = '"';
    return pos;
}

static const char *role_str(AiRole role)
{
    switch (role) {
        case AI_ROLE_SYSTEM:    return "system";
        case AI_ROLE_USER:      return "user";
        case AI_ROLE_ASSISTANT: return "assistant";
    }
    return "user";
}

size_t ai_build_request_body_ex(const AiConversation *conv,
                                char *buf, size_t buf_size, int stream)
{
    if (!conv || !buf || buf_size == 0 || conv->msg_count == 0) return 0;

    size_t pos = 0;

    /* Opening: {"model":"...","messages":[ */
    int n = snprintf(buf, buf_size, "{\"model\":");
    if (n < 0) return 0;
    pos = (size_t)n;

    pos = json_escape_str(conv->model, buf, buf_size, pos);
    if (pos == 0) return 0;

    const char *mid = stream ? ",\"stream\":true,\"messages\":["
                             : ",\"stream\":false,\"messages\":[";
    size_t mid_len = strlen(mid);
    if (pos + mid_len >= buf_size) return 0;
    memcpy(buf + pos, mid, mid_len);
    pos += mid_len;

    for (int i = 0; i < conv->msg_count; i++) {
        if (i > 0) {
            if (pos + 1 >= buf_size) return 0;
            buf[pos++] = ',';
        }

        const char *role = role_str(conv->messages[i].role);
        int rn = snprintf(buf + pos, buf_size - pos, "{\"role\":\"%s\",\"content\":", role);
        if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
        pos += (size_t)rn;

        pos = json_escape_str(conv->messages[i].content, buf, buf_size, pos);
        if (pos == 0) return 0;

        if (pos + 1 >= buf_size) return 0;
        buf[pos++] = '}';
    }

    /* Close: ]} */
    if (pos + 2 >= buf_size) return 0;
    buf[pos++] = ']';
    buf[pos++] = '}';
    buf[pos] = '\0';

    return pos;
}

size_t ai_build_request_body(const AiConversation *conv,
                             char *buf, size_t buf_size)
{
    return ai_build_request_body_ex(conv, buf, buf_size, 0);
}

int ai_parse_response_ex(const char *json, char *content_out, size_t content_size,
                          char *thinking_out, size_t thinking_size)
{
    if (!json || !content_out || content_size == 0) return -1;

    content_out[0] = '\0';
    if (thinking_out && thinking_size > 0)
        thinking_out[0] = '\0';

    JsonNode *root = json_parse(json);
    if (!root) return -1;

    /* Navigate: root.choices[0].message.content */
    JsonNode *choices = json_obj_get(root, "choices");
    if (!choices || choices->type != JSON_ARRAY || vec_size(&choices->as.arr) == 0) {
        json_free(root);
        return -1;
    }

    JsonNode *first = (JsonNode *)vec_get(&choices->as.arr, 0);
    if (!first || first->type != JSON_OBJECT) {
        json_free(root);
        return -1;
    }

    JsonNode *message = json_obj_get(first, "message");
    if (!message || message->type != JSON_OBJECT) {
        json_free(root);
        return -1;
    }

    const char *content = json_obj_str(message, "content");
    if (!content) {
        json_free(root);
        return -1;
    }

    snprintf(content_out, content_size, "%s", content);

    /* Extract reasoning/thinking content if present (DeepSeek reasoner format) */
    if (thinking_out && thinking_size > 0) {
        const char *reasoning = json_obj_str(message, "reasoning_content");
        if (reasoning)
            snprintf(thinking_out, thinking_size, "%s", reasoning);
    }

    json_free(root);
    return 0;
}

int ai_parse_response(const char *json, char *content_out, size_t content_size)
{
    return ai_parse_response_ex(json, content_out, content_size, NULL, 0);
}

int ai_parse_stream_chunk(const char *json,
                          char *content_out, size_t content_size,
                          char *thinking_out, size_t thinking_size)
{
    if (!json) return -1;
    if (content_out && content_size > 0) content_out[0] = '\0';
    if (thinking_out && thinking_size > 0) thinking_out[0] = '\0';

    /* Check for stream termination */
    if (strcmp(json, "[DONE]") == 0) return 1;

    JsonNode *root = json_parse(json);
    if (!root) return -1;

    /* Navigate: root.choices[0].delta */
    JsonNode *choices = json_obj_get(root, "choices");
    if (!choices || choices->type != JSON_ARRAY || vec_size(&choices->as.arr) == 0) {
        json_free(root);
        return -1;
    }

    JsonNode *first = (JsonNode *)vec_get(&choices->as.arr, 0);
    if (!first || first->type != JSON_OBJECT) {
        json_free(root);
        return -1;
    }

    JsonNode *delta = json_obj_get(first, "delta");
    if (!delta || delta->type != JSON_OBJECT) {
        /* Some chunks may have empty delta (e.g. role-only) — not an error */
        json_free(root);
        return 0;
    }

    const char *content = json_obj_str(delta, "content");
    if (content && content_out && content_size > 0)
        snprintf(content_out, content_size, "%s", content);

    const char *reasoning = json_obj_str(delta, "reasoning_content");
    if (reasoning && thinking_out && thinking_size > 0)
        snprintf(thinking_out, thinking_size, "%s", reasoning);

    json_free(root);
    return 0;
}

int ai_extract_command(const char *response, char *cmd_out, size_t cmd_size)
{
    if (!response || !cmd_out || cmd_size == 0) return 0;

    cmd_out[0] = '\0';

    const char *start = strstr(response, "[EXEC]");
    if (!start) return 0;
    start += 6; /* skip "[EXEC]" */

    const char *end = strstr(start, "[/EXEC]");
    if (!end) return 0;

    size_t len = (size_t)(end - start);
    if (len == 0) return 0;
    if (len >= cmd_size) len = cmd_size - 1;

    memcpy(cmd_out, start, len);
    cmd_out[len] = '\0';
    return 1;
}

int ai_extract_commands(const char *response, char cmds[][1024],
                        int max_cmds)
{
    if (!response || !cmds || max_cmds <= 0) return 0;

    int count = 0;
    const char *pos = response;

    while (count < max_cmds) {
        const char *start = strstr(pos, "[EXEC]");
        if (!start) break;
        start += 6; /* skip "[EXEC]" */

        const char *end = strstr(start, "[/EXEC]");
        if (!end) break;

        size_t len = (size_t)(end - start);
        if (len == 0) {
            pos = end + 7;
            continue;
        }
        if (len >= 1024) len = 1023;

        memcpy(cmds[count], start, len);
        cmds[count][len] = '\0';
        count++;

        pos = end + 7; /* skip "[/EXEC]" */
    }

    return count;
}

/* ---- Response splitting ---- */

int ai_response_split(const char *response,
                      char *pre_cmd, size_t pre_size,
                      char *post_cmd, size_t post_size)
{
    if (pre_cmd && pre_size > 0) pre_cmd[0] = '\0';
    if (post_cmd && post_size > 0) post_cmd[0] = '\0';
    if (!response || !*response) return 0;

    /* Find first [EXEC] and count command pairs */
    const char *first_exec = NULL;
    const char *last_end = NULL;
    int count = 0;
    const char *pos = response;

    while ((pos = strstr(pos, "[EXEC]")) != NULL) {
        if (!first_exec) first_exec = pos;
        pos += 6;
        const char *end = strstr(pos, "[/EXEC]");
        if (!end) break; /* unclosed — don't count */
        count++;
        last_end = end + 7; /* past "[/EXEC]" */
        pos = last_end;
    }

    if (count == 0) {
        /* No valid command pairs — full text goes to pre_cmd */
        if (pre_cmd && pre_size > 0) {
            size_t len = strlen(response);
            if (len >= pre_size) len = pre_size - 1;
            memcpy(pre_cmd, response, len);
            pre_cmd[len] = '\0';
        }
        return 0;
    }

    /* Pre-command: text before first [EXEC] */
    if (pre_cmd && pre_size > 0) {
        size_t len = (size_t)(first_exec - response);
        if (len >= pre_size) len = pre_size - 1;
        memcpy(pre_cmd, response, len);
        pre_cmd[len] = '\0';
    }

    /* Post-command: text after last [/EXEC] */
    if (post_cmd && post_size > 0 && last_end) {
        size_t len = strlen(last_end);
        if (len >= post_size) len = post_size - 1;
        memcpy(post_cmd, last_end, len);
        post_cmd[len] = '\0';
    }

    return count;
}

/* ---- Command read-only classification ---- */

int ai_command_is_readonly(const char *cmd)
{
    return cmd_classify(cmd, CMD_PLATFORM_LINUX) == CMD_SAFE;
}

const char *ai_provider_url(const char *provider)
{
    if (!provider) return NULL;

    if (strcmp(provider, "deepseek") == 0)
        return "https://api.deepseek.com/chat/completions";
    if (strcmp(provider, "openai") == 0)
        return "https://api.openai.com/v1/chat/completions";
    if (strcmp(provider, "anthropic") == 0)
        return "https://api.anthropic.com/v1/messages";
    if (strcmp(provider, "moonshot") == 0)
        return "https://api.moonshot.ai/v1/chat/completions";
    if (strcmp(provider, "gemini") == 0)
        return "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions";

    return NULL;
}

const char *ai_provider_model(const char *provider)
{
    if (!provider) return NULL;

    if (strcmp(provider, "deepseek") == 0) return "deepseek-chat";
    if (strcmp(provider, "openai") == 0)   return "gpt-4o";
    if (strcmp(provider, "anthropic") == 0) return "claude-sonnet-4-6";
    if (strcmp(provider, "moonshot") == 0)  return "kimi-k2";
    if (strcmp(provider, "gemini") == 0)   return "gemini-2.5-flash";

    return NULL;
}

/* Returns a NULL-terminated array of model names for a given provider. */
const char * const *ai_provider_models(const char *provider)
{
    if (!provider) return NULL;

    static const char * const deepseek_models[] = {
        "deepseek-chat", "deepseek-coder", "deepseek-reasoner", NULL
    };
    static const char * const openai_models[] = {
        "gpt-4o", "gpt-4o-mini", "gpt-4-turbo", "o3-mini", "gpt-3.5-turbo", NULL
    };
    static const char * const anthropic_models[] = {
        "claude-sonnet-4-6", "claude-haiku-4-5-20251001", "claude-opus-4-6", NULL
    };
    static const char * const moonshot_models[] = {
        "kimi-k2", "moonshot-v1-8k", "moonshot-v1-32k", "moonshot-v1-128k", NULL
    };
    static const char * const gemini_models[] = {
        "gemini-2.5-flash", "gemini-2.5-pro", "gemini-2.0-flash", NULL
    };

    if (strcmp(provider, "deepseek") == 0)   return deepseek_models;
    if (strcmp(provider, "openai") == 0)     return openai_models;
    if (strcmp(provider, "anthropic") == 0)  return anthropic_models;
    if (strcmp(provider, "moonshot") == 0)   return moonshot_models;
    if (strcmp(provider, "gemini") == 0)     return gemini_models;

    return NULL;
}

const char *ai_provider_models_url(const char *provider)
{
    if (!provider) return NULL;

    if (strcmp(provider, "deepseek") == 0)
        return "https://api.deepseek.com/models";
    if (strcmp(provider, "openai") == 0)
        return "https://api.openai.com/v1/models";
    if (strcmp(provider, "anthropic") == 0)
        return "https://api.anthropic.com/v1/models";
    if (strcmp(provider, "moonshot") == 0)
        return "https://api.moonshot.ai/v1/models";
    if (strcmp(provider, "gemini") == 0)
        return "https://generativelanguage.googleapis.com/v1beta/openai/models";

    return NULL;
}

size_t ai_build_confirm_text(char cmds[][1024], int ncmds,
                              char *buf, size_t buf_size)
{
    if (!cmds || ncmds <= 0 || !buf || buf_size == 0) return 0;

    int pos = 0;
    int n;

    if (ncmds == 1) {
        n = snprintf(buf + pos, buf_size - (size_t)pos,
                     "The AI wants to execute 1 command:\n\n");
    } else {
        n = snprintf(buf + pos, buf_size - (size_t)pos,
                     "The AI wants to execute %d commands:\n\n", ncmds);
    }
    if (n < 0 || (size_t)(pos + n) >= buf_size) return 0;
    pos += n;

    for (int i = 0; i < ncmds; i++) {
        n = snprintf(buf + pos, buf_size - (size_t)pos,
                     "  %d. %s\n", i + 1, cmds[i]);
        if (n < 0 || (size_t)(pos + n) >= buf_size) return 0;
        pos += n;
    }

    n = snprintf(buf + pos, buf_size - (size_t)pos, "\nAllow?");
    if (n < 0 || (size_t)(pos + n) >= buf_size) return 0;
    pos += n;

    return (size_t)pos;
}

AiInputAction ai_input_key_action(int is_enter, int shift_held)
{
    if (!is_enter) return AI_INPUT_PASSTHROUGH;
    return shift_held ? AI_INPUT_NEWLINE : AI_INPUT_SEND;
}

int ai_cmd_progress_text(int current, int total, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    return snprintf(buf, buf_size, "(executing %d/%d.)", current, total);
}

int ai_cmd_waiting_text(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    return snprintf(buf, buf_size, "(waiting for output.)");
}

size_t ai_build_save_text(const AiConversation *conv,
                           char *const *thinking, int show_thinking,
                           char *buf, size_t buf_size)
{
    if (!conv || !buf || buf_size == 0) return 0;

    size_t pos = 0;
    int n;

    n = snprintf(buf + pos, buf_size - pos, "AI Assist Conversation\n"
                 "Model: %s\n"
                 "========================================\n\n",
                 conv->model);
    if (n > 0) pos += (size_t)n;

    for (int i = 1; i < conv->msg_count && pos < buf_size; i++) {
        const AiMessage *msg = &conv->messages[i];

        if (msg->role == AI_ROLE_USER) {
            n = snprintf(buf + pos, buf_size - pos,
                         "--- You ---\n%s\n\n", msg->content);
            if (n > 0) pos += (size_t)n;
        } else if (msg->role == AI_ROLE_ASSISTANT) {
            if (show_thinking && thinking && thinking[i] &&
                thinking[i][0]) {
                n = snprintf(buf + pos, buf_size - pos,
                             "--- Thinking ---\n%s\n\n", thinking[i]);
                if (n > 0) pos += (size_t)n;
            }
            n = snprintf(buf + pos, buf_size - pos,
                         "--- AI ---\n%s\n\n", msg->content);
            if (n > 0) pos += (size_t)n;
        }
    }

    if (pos >= buf_size) pos = buf_size - 1;
    buf[pos] = '\0';
    return pos;
}
