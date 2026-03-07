#include "ai_prompt.h"
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
                            const char *terminal_text)
{
    if (!buf || buf_size == 0) return;

    const char *base =
        "You are an AI assistant for an SSH terminal session. "
        "You can see the terminal output and help the user with tasks.\n"
        "When you want to execute a command on the remote server, "
        "wrap it in [EXEC] and [/EXEC] markers like this:\n"
        "[EXEC]ls -la[/EXEC]\n"
        "Always explain what you will do before executing. "
        "Only suggest one command at a time.";

    if (terminal_text && terminal_text[0]) {
        snprintf(buf, buf_size,
                 "%s\n\nCurrent terminal output:\n```\n%s\n```",
                 base, terminal_text);
    } else {
        snprintf(buf, buf_size, "%s", base);
    }
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

size_t ai_build_request_body(const AiConversation *conv,
                             char *buf, size_t buf_size)
{
    if (!conv || !buf || buf_size == 0 || conv->msg_count == 0) return 0;

    size_t pos = 0;

    /* Opening: {"model":"...","messages":[ */
    int n = snprintf(buf, buf_size, "{\"model\":");
    if (n < 0) return 0;
    pos = (size_t)n;

    pos = json_escape_str(conv->model, buf, buf_size, pos);
    if (pos == 0) return 0;

    const char *mid = ",\"stream\":false,\"messages\":[";
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

int ai_parse_response(const char *json, char *content_out, size_t content_size)
{
    if (!json || !content_out || content_size == 0) return -1;

    content_out[0] = '\0';

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

const char *ai_provider_url(const char *provider)
{
    if (!provider) return NULL;

    if (strcmp(provider, "deepseek") == 0)
        return "https://api.deepseek.com/chat/completions";
    if (strcmp(provider, "openai") == 0)
        return "https://api.openai.com/v1/chat/completions";
    if (strcmp(provider, "anthropic") == 0)
        return "https://api.anthropic.com/v1/messages";

    return NULL;
}

const char *ai_provider_model(const char *provider)
{
    if (!provider) return NULL;

    if (strcmp(provider, "deepseek") == 0) return "deepseek-chat";
    if (strcmp(provider, "openai") == 0)   return "gpt-4o";
    if (strcmp(provider, "anthropic") == 0) return "claude-sonnet-4-6";

    return NULL;
}
