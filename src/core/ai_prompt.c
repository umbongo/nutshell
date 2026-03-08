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
                            const char *terminal_text)
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

/* ---- Command read-only classification ---- */

/* Check if a word matches a known write/dangerous command. */
static int is_write_command(const char *word, size_t len)
{
    static const char *write_cmds[] = {
        "rm", "rmdir", "mv", "cp", "mkdir", "touch", "chmod", "chown",
        "chgrp", "ln", "install", "truncate", "shred",
        "dd", "mkfs", "mount", "umount",
        "vim", "vi", "nano", "emacs", "ed", "pico",
        "sed", "awk",
        "tee",
        "apt", "apt-get", "dpkg", "yum", "dnf", "rpm", "pacman",
        "snap", "flatpak", "pip", "pip3", "npm", "yarn", "cargo",
        "gem", "go",
        "git", "svn", "hg",
        "kill", "killall", "pkill",
        "reboot", "shutdown", "halt", "poweroff", "init",
        "systemctl", "service",
        "useradd", "userdel", "usermod", "groupadd", "groupdel",
        "passwd", "chpasswd",
        "iptables", "ufw", "firewall-cmd",
        "crontab", "at",
        "sudo", "su", "doas",
        "wget", "curl",
        "tar", "zip", "unzip", "gzip", "gunzip", "bzip2",
        "make", "cmake", "gcc", "g++", "cc",
        "docker", "podman", "kubectl",
        NULL
    };

    for (int i = 0; write_cmds[i]; i++) {
        size_t cl = strlen(write_cmds[i]);
        if (cl == len && memcmp(word, write_cmds[i], len) == 0)
            return 1;
    }
    return 0;
}

int ai_command_is_readonly(const char *cmd)
{
    if (!cmd || !cmd[0]) return 1;

    /* Check for output redirects anywhere in the command.
     * Allow harmless patterns: 2>/dev/null, >/dev/null, 2>&1, &>/dev/null */
    for (const char *p = cmd; *p; p++) {
        if (*p == '\'' || *p == '"') {
            /* Skip quoted strings */
            char q = *p++;
            while (*p && *p != q) p++;
            if (!*p) break;
            continue;
        }
        if (*p == '>' || (*p == '&' && *(p + 1) == '>')) {
            /* Check if this is a harmless redirect */
            const char *r = p;
            /* Handle &> prefix */
            if (*r == '&') r++;
            /* Handle 2> prefix (r already at > or after &) */
            if (r > cmd && *(r - 1) == '2') { /* 2> or 2>> */ }
            /* Skip > or >> */
            r++;
            if (*r == '>') r++;
            /* Skip spaces */
            while (*r == ' ' || *r == '\t') r++;
            /* Allow: /dev/null, &1, &2 */
            if (strncmp(r, "/dev/null", 9) == 0)
                { p = r + 8; continue; }
            if (*r == '&' && (*(r + 1) == '1' || *(r + 1) == '2'))
                { p = r + 1; continue; }
            return 0; /* real file redirect — not readonly */
        }
    }

    /* Check each command in a pipeline */
    const char *seg = cmd;
    while (*seg) {
        /* Skip leading whitespace */
        while (*seg == ' ' || *seg == '\t') seg++;
        if (!*seg) break;

        /* Extract first word of this segment */
        const char *word_start = seg;
        while (*seg && *seg != ' ' && *seg != '\t' && *seg != '|'
               && *seg != ';' && *seg != '&')
            seg++;
        size_t word_len = (size_t)(seg - word_start);

        /* Strip path prefix (e.g., /usr/bin/rm → rm) */
        const char *base = word_start;
        for (const char *s = word_start; s < word_start + word_len; s++) {
            if (*s == '/') base = s + 1;
        }
        size_t base_len = (size_t)((word_start + word_len) - base);

        if (base_len > 0 && is_write_command(base, base_len))
            return 0;

        /* Advance to next pipe/semicolon segment */
        while (*seg && *seg != '|' && *seg != ';' && *seg != '&') seg++;
        if (*seg) seg++; /* skip separator */
    }

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
    if (strcmp(provider, "moonshot") == 0)
        return "https://api.moonshot.ai/v1/chat/completions";

    return NULL;
}

const char *ai_provider_model(const char *provider)
{
    if (!provider) return NULL;

    if (strcmp(provider, "deepseek") == 0) return "deepseek-chat";
    if (strcmp(provider, "openai") == 0)   return "gpt-4o";
    if (strcmp(provider, "anthropic") == 0) return "claude-sonnet-4-6";
    if (strcmp(provider, "moonshot") == 0)  return "kimi-k2";

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

    if (strcmp(provider, "deepseek") == 0)   return deepseek_models;
    if (strcmp(provider, "openai") == 0)     return openai_models;
    if (strcmp(provider, "anthropic") == 0)  return anthropic_models;
    if (strcmp(provider, "moonshot") == 0)   return moonshot_models;

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
