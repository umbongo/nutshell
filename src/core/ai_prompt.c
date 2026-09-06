#include "ai_prompt.h"
#include "ai_tools.h"
#include "cmd_classify.h"
#include "json_parser.h"
#include "json_validate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int ai_context_clamp_lines(int lines)
{
    if (lines < 1) return 1;
    if (lines > AI_CONTEXT_LINES_MAX) return AI_CONTEXT_LINES_MAX;
    return lines;
}

size_t ai_context_buf_size(int lines, int cols)
{
    int clamped_lines = ai_context_clamp_lines(lines);
    int use_cols = (cols < 1) ? 80 : cols;

    size_t l = (size_t)clamped_lines;
    /* 4 bytes/cell worst-case UTF-8 encoding, +1 for the newline */
    size_t line_width = (size_t)use_cols * 4u + 1u;

    /* Guard against overflow before multiplying: l * line_width + 1 */
    if (l > (SIZE_MAX - 1u) / line_width) {
        return AI_CONTEXT_BYTES_MAX;
    }
    size_t total = l * line_width + 1u;

    if (total > AI_CONTEXT_BYTES_MAX) {
        return AI_CONTEXT_BYTES_MAX;
    }
    return total;
}

void ai_attachment_free(AiAttachment **att)
{
    if (!att || !*att) return;
    free((*att)->base64_url);
    free(*att);
    *att = NULL;
}

AiAttachment *ai_attachment_dup(const AiAttachment *att)
{
    if (!att) return NULL;
    AiAttachment *dup = calloc(1, sizeof(*dup));
    if (!dup) return NULL;
    if (att->base64_url) {
        size_t len = strlen(att->base64_url);
        dup->base64_url = malloc(len + 1);
        if (!dup->base64_url) { free(dup); return NULL; }
        memcpy(dup->base64_url, att->base64_url, len + 1);
    }
    dup->width = att->width;
    dup->height = att->height;
    return dup;
}

const char *ai_msg_content(const AiMessage *msg)
{
    if (!msg) return "";
    if (msg->content_overflow) return msg->content_overflow;
    return msg->content;
}

int ai_msg_set_content(AiMessage *msg, const char *data, size_t len)
{
    if (!msg) return -1;
    /* Free any prior overflow */
    if (msg->content_overflow) {
        free(msg->content_overflow);
        msg->content_overflow = NULL;
    }
    if (len < AI_MSG_MAX) {
        memcpy(msg->content, data, len);
        msg->content[len] = '\0';
        msg->content_len = len;
    } else {
        msg->content_overflow = malloc(len + 1);
        if (!msg->content_overflow) return -1;
        memcpy(msg->content_overflow, data, len);
        msg->content_overflow[len] = '\0';
        msg->content_len = len;
        msg->content[0] = '\0';  /* clear inline buffer */
    }
    return 0;
}

void ai_msg_free(AiMessage *msg)
{
    if (!msg) return;
    if (msg->content_overflow) {
        free(msg->content_overflow);
        msg->content_overflow = NULL;
    }
    if (msg->tool_calls) {
        free(msg->tool_calls);
        msg->tool_calls = NULL;
    }
    msg->n_tool_calls = 0;
    if (msg->attachment) {
        ai_attachment_free(&msg->attachment);
    }
}

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
    for (int i = 0; i < conv->msg_count; i++)
        ai_msg_free(&conv->messages[i]);
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
    m->content_overflow = NULL;
    if (ai_msg_set_content(m, content, strlen(content)) != 0) return -1;
    m->attachment = NULL;
    m->tool_call_id[0] = '\0';
    m->tool_name[0] = '\0';
    m->is_tool_error = 0;
    m->tool_calls = NULL;
    m->n_tool_calls = 0;
    conv->msg_count++;
    return 0;
}

int ai_conv_set_system(AiConversation *conv, const char *content)
{
    if (!conv || conv->msg_count <= 0 || !content) return -1;
    if (conv->messages[0].role != AI_ROLE_SYSTEM) return -1;
    return ai_msg_set_content(&conv->messages[0], content, strlen(content));
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
        total_chars += (int)strlen(ai_msg_content(&conv->messages[i]));
    return total_chars / 4;
}

int ai_format_context_label(int tokens, int limit, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    if (limit <= 0)
        return snprintf(buf, buf_size, "Context: N/A");

    int pct = (tokens * 100) / limit;
    if (pct > 100) pct = 100;

    char tok_str[16], lim_str[16];
    if (tokens >= 1000)
        snprintf(tok_str, sizeof(tok_str), "%.1fk", tokens / 1000.0);
    else
        snprintf(tok_str, sizeof(tok_str), "%d", tokens);
    if (limit >= 1000)
        snprintf(lim_str, sizeof(lim_str), "%dk", limit / 1000);
    else
        snprintf(lim_str, sizeof(lim_str), "%d", limit);

    return snprintf(buf, buf_size, "Context: %s / %s (%d%%)",
                    tok_str, lim_str, pct);
}

/* Internal helper: format a non-negative int with thousands separators
 * (e.g. 18432 -> "18,432"). Writes into buf of size buf_size and
 * NUL-terminates. Returns bytes written (excluding NUL). */
static int format_int_with_commas(int value, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    if (value < 0) value = 0;

    char tmp[16];
    int tmp_len = snprintf(tmp, sizeof(tmp), "%d", value);
    if (tmp_len < 0) tmp_len = 0;

    /* Walk digits from the right, inserting commas every 3. */
    int out_len = 0;
    int digits_since_comma = 0;
    char rev[24];
    for (int i = tmp_len - 1; i >= 0; i--) {
        if (digits_since_comma == 3) {
            rev[out_len++] = ',';
            digits_since_comma = 0;
        }
        rev[out_len++] = tmp[i];
        digits_since_comma++;
    }

    /* Reverse into buf. */
    int written = 0;
    for (int i = out_len - 1; i >= 0 && (size_t)written < buf_size - 1; i--)
        buf[written++] = rev[i];
    buf[written] = '\0';
    return written;
}

int ai_format_context_tooltip(int actual_in, int actual_out,
                              int estimated_total,
                              int context_limit,
                              const char *model_name,
                              char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    buf[0] = '\0';

    int actual_total = actual_in + actual_out;
    int has_actual   = (actual_total > 0);
    int has_limit    = (context_limit > 0);
    int has_model    = (model_name && model_name[0]);

    char tot_str[24], lim_str[24];
    int  display_total = has_actual ? actual_total : estimated_total;
    if (display_total < 0) display_total = 0;
    format_int_with_commas(display_total, tot_str, sizeof(tot_str));
    if (has_limit)
        format_int_with_commas(context_limit, lim_str, sizeof(lim_str));

    int pct = 0;
    if (has_limit) {
        pct = (display_total * 100) / context_limit;
        if (pct > 100) pct = 100;
    }

    /* Build into a working buffer to keep the snprintf calls simple. */
    char tmp[512];
    int  off = 0;
    int  rem = (int)sizeof(tmp);

    int w = snprintf(tmp + off, (size_t)rem, "Context usage\r\n\r\n");
    if (w < 0) w = 0;
    if (w > rem) w = rem;
    off += w; rem -= w;

    if (has_actual) {
        char in_str[24], out_str[24];
        format_int_with_commas(actual_in,  in_str,  sizeof(in_str));
        format_int_with_commas(actual_out, out_str, sizeof(out_str));
        w = snprintf(tmp + off, (size_t)rem,
            "Input tokens:   %s\r\n"
            "Output tokens:  %s\r\n",
            in_str, out_str);
        if (w < 0) w = 0;
        if (w > rem) w = rem;
        off += w; rem -= w;

        if (has_limit) {
            w = snprintf(tmp + off, (size_t)rem,
                "Total:          %s / %s (%d%%)\r\n",
                tot_str, lim_str, pct);
        } else {
            w = snprintf(tmp + off, (size_t)rem,
                "Total:          %s\r\n"
                "Limit: unknown\r\n",
                tot_str);
        }
    } else {
        if (has_limit) {
            w = snprintf(tmp + off, (size_t)rem,
                "Total (estimated):  ~%s / %s (%d%%)\r\n",
                tot_str, lim_str, pct);
        } else {
            w = snprintf(tmp + off, (size_t)rem,
                "Total (estimated):  ~%s\r\n"
                "Limit: unknown\r\n",
                tot_str);
        }
    }
    if (w < 0) w = 0;
    if (w > rem) w = rem;
    off += w; rem -= w;

    if (has_model) {
        w = snprintf(tmp + off, (size_t)rem,
            "\r\nModel:          %s", model_name);
        if (w < 0) w = 0;
        if (w > rem) w = rem;
        off += w; rem -= w;
    }

    /* Copy into the caller's buffer with truncation. */
    size_t copy_len = (size_t)off;
    if (copy_len > buf_size - 1) copy_len = buf_size - 1;
    memcpy(buf, tmp, copy_len);
    buf[copy_len] = '\0';
    return (int)copy_len;
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

    int old_msg_count = conv->msg_count;

    /* Free heap resources (content_overflow, attachment, tool_calls) owned
     * by the messages being dropped, before their slots are overwritten. */
    for (int i = remove_start; i < remove_end; i++)
        ai_msg_free(&conv->messages[i]);

    memmove(&conv->messages[remove_start],
            &conv->messages[remove_end],
            (size_t)(conv->msg_count - remove_end) * sizeof(AiMessage));
    conv->msg_count -= remove_count;

    /* The tail slots now hold stale duplicates of pointers that were just
     * relocated (or already-freed pointers from the loop above) — clear
     * them so nothing dangling is left behind. */
    memset(&conv->messages[conv->msg_count], 0,
           (size_t)(old_msg_count - conv->msg_count) * sizeof(AiMessage));

    return remove_count;
}


static const char *role_str(AiRole role)
{
    switch (role) {
        case AI_ROLE_SYSTEM:    return "system";
        case AI_ROLE_USER:      return "user";
        case AI_ROLE_ASSISTANT: return "assistant";
        case AI_ROLE_TOOL:      return "tool";
    }
    return "user";
}

size_t ai_build_request_body_ex(const AiConversation *conv,
                                const AiAttachment *last_user_attachment,
                                char *buf, size_t buf_size, int stream,
                                const char *provider)
{
    if (!conv || !buf || buf_size == 0 || conv->msg_count == 0) return 0;

    int is_anthropic = provider && strcmp(provider, "anthropic") == 0;
    size_t pos = 0;
    int n;

    /* {"model":"..." */
    n = snprintf(buf, buf_size, "{\"model\":");
    if (n < 0) return 0;
    pos = (size_t)n;

    pos = json_escape_string(conv->model, strlen(conv->model), buf, buf_size, pos, 1);
    if (pos == 0) return 0;

    /* Anthropic requires max_tokens */
    if (is_anthropic) {
        n = snprintf(buf + pos, buf_size - pos, ",\"max_tokens\":8096");
        if (n < 0 || pos + (size_t)n >= buf_size) return 0;
        pos += (size_t)n;
    }

    /* stream flag */
    const char *stream_str = stream ? ",\"stream\":true" : ",\"stream\":false";
    size_t stream_len = strlen(stream_str);
    if (pos + stream_len >= buf_size) return 0;
    memcpy(buf + pos, stream_str, stream_len);
    pos += stream_len;

    /* Anthropic: system message as top-level "system" key */
    int msg_start = 0;
    if (is_anthropic && conv->msg_count > 0
            && conv->messages[0].role == AI_ROLE_SYSTEM) {
        const char *sys_key = ",\"system\":";
        size_t sys_key_len = strlen(sys_key);
        if (pos + sys_key_len >= buf_size) return 0;
        memcpy(buf + pos, sys_key, sys_key_len);
        pos += sys_key_len;
        pos = json_escape_string(ai_msg_content(&conv->messages[0]), strlen(ai_msg_content(&conv->messages[0])), buf, buf_size, pos, 1);
        if (pos == 0) return 0;
        msg_start = 1;
    }

    /* Open messages array */
    const char *msgs_open = ",\"messages\":[";
    size_t msgs_open_len = strlen(msgs_open);
    if (pos + msgs_open_len >= buf_size) return 0;
    memcpy(buf + pos, msgs_open, msgs_open_len);
    pos += msgs_open_len;

    /* Track whether we emitted at least one message (for comma placement) */
    int emitted = 0;

    for (int i = msg_start; i < conv->msg_count; i++) {
        const AiMessage *msg = &conv->messages[i];

        /* ---- Anthropic: coalesce consecutive AI_ROLE_TOOL messages ---- */
        if (is_anthropic && msg->role == AI_ROLE_TOOL) {
            /* Find the end of this run of tool-result messages */
            int run_end = i;
            while (run_end + 1 < conv->msg_count &&
                   conv->messages[run_end + 1].role == AI_ROLE_TOOL)
                run_end++;

            /* Emit opening comma + user wrapper */
            if (emitted) {
                if (pos + 1 >= buf_size) return 0;
                buf[pos++] = ',';
            }
            emitted = 1;

            const char *tool_user_open = "{\"role\":\"user\",\"content\":[";
            size_t tuo_len = strlen(tool_user_open);
            if (pos + tuo_len >= buf_size) return 0;
            memcpy(buf + pos, tool_user_open, tuo_len);
            pos += tuo_len;

            for (int j = i; j <= run_end; j++) {
                const AiMessage *tm = &conv->messages[j];
                if (j > i) {
                    if (pos + 1 >= buf_size) return 0;
                    buf[pos++] = ',';
                }
                /* {"type":"tool_result","tool_use_id":"...","content":"..."} */
                int rn = snprintf(buf + pos, buf_size - pos,
                                  "{\"type\":\"tool_result\",\"tool_use_id\":");
                if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
                pos += (size_t)rn;

                pos = json_escape_string(tm->tool_call_id, strlen(tm->tool_call_id), buf, buf_size, pos, 1);
                if (pos == 0) return 0;

                const char *content_key = ",\"content\":";
                size_t ck_len = strlen(content_key);
                if (pos + ck_len >= buf_size) return 0;
                memcpy(buf + pos, content_key, ck_len);
                pos += ck_len;

                pos = json_escape_string(ai_msg_content(tm), strlen(ai_msg_content(tm)), buf, buf_size, pos, 1);
                if (pos == 0) return 0;

                if (tm->is_tool_error) {
                    const char *err_flag = ",\"is_error\":true";
                    size_t ef_len = strlen(err_flag);
                    if (pos + ef_len >= buf_size) return 0;
                    memcpy(buf + pos, err_flag, ef_len);
                    pos += ef_len;
                }

                if (pos + 1 >= buf_size) return 0;
                buf[pos++] = '}';
            }

            /* Close content array and user wrapper */
            const char *tool_user_close = "]}";
            size_t tuc_len = strlen(tool_user_close);
            if (pos + tuc_len >= buf_size) return 0;
            memcpy(buf + pos, tool_user_close, tuc_len);
            pos += tuc_len;

            /* Skip the whole run */
            i = run_end;
            continue;
        }

        /* ---- OpenAI: tool result message ---- */
        if (!is_anthropic && msg->role == AI_ROLE_TOOL) {
            if (emitted) {
                if (pos + 1 >= buf_size) return 0;
                buf[pos++] = ',';
            }
            emitted = 1;

            int rn = snprintf(buf + pos, buf_size - pos,
                              "{\"role\":\"tool\",\"tool_call_id\":");
            if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
            pos += (size_t)rn;

            pos = json_escape_string(msg->tool_call_id, strlen(msg->tool_call_id), buf, buf_size, pos, 1);
            if (pos == 0) return 0;

            const char *tc_content = ",\"content\":";
            size_t tcc_len = strlen(tc_content);
            if (pos + tcc_len >= buf_size) return 0;
            memcpy(buf + pos, tc_content, tcc_len);
            pos += tcc_len;

            pos = json_escape_string(ai_msg_content(msg), strlen(ai_msg_content(msg)), buf, buf_size, pos, 1);
            if (pos == 0) return 0;

            if (pos + 1 >= buf_size) return 0;
            buf[pos++] = '}';
            continue;
        }

        /* ---- Assistant message with tool_use blocks (Anthropic) ---- */
        if (is_anthropic && msg->role == AI_ROLE_ASSISTANT && msg->n_tool_calls > 0) {
            if (emitted) {
                if (pos + 1 >= buf_size) return 0;
                buf[pos++] = ',';
            }
            emitted = 1;

            const char *asst_open = "{\"role\":\"assistant\",\"content\":[";
            size_t ao_len = strlen(asst_open);
            if (pos + ao_len >= buf_size) return 0;
            memcpy(buf + pos, asst_open, ao_len);
            pos += ao_len;

            /* Text block — only emit if non-empty (API rejects empty text blocks) */
            const char *asst_text = ai_msg_content(msg);
            int has_text = (asst_text && asst_text[0] != '\0');
            if (has_text) {
                const char *text_open = "{\"type\":\"text\",\"text\":";
                size_t to_len = strlen(text_open);
                if (pos + to_len >= buf_size) return 0;
                memcpy(buf + pos, text_open, to_len);
                pos += to_len;

                pos = json_escape_string(asst_text, strlen(asst_text), buf, buf_size, pos, 1);
                if (pos == 0) return 0;

                if (pos + 1 >= buf_size) return 0;
                buf[pos++] = '}';
            }

            /* Tool use blocks */
            for (int t = 0; t < msg->n_tool_calls; t++) {
                const AiToolCall *tc = &msg->tool_calls[t];
                /* Comma separator: after text block, or between tool blocks */
                if (pos + 1 >= buf_size) return 0;
                if (t > 0 || has_text) buf[pos++] = ',';

                int rn = snprintf(buf + pos, buf_size - pos,
                                  "{\"type\":\"tool_use\",\"id\":");
                if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
                pos += (size_t)rn;

                pos = json_escape_string(tc->id, strlen(tc->id), buf, buf_size, pos, 1);
                if (pos == 0) return 0;

                const char *name_key = ",\"name\":";
                size_t nk_len = strlen(name_key);
                if (pos + nk_len >= buf_size) return 0;
                memcpy(buf + pos, name_key, nk_len);
                pos += nk_len;

                pos = json_escape_string(tc->name, strlen(tc->name), buf, buf_size, pos, 1);
                if (pos == 0) return 0;

                /* input: raw JSON object from input_json */
                const char *input_key = ",\"input\":";
                size_t ik_len = strlen(input_key);
                if (pos + ik_len >= buf_size) return 0;
                memcpy(buf + pos, input_key, ik_len);
                pos += ik_len;

                size_t ij_len = strlen(tc->input_json);
                if (pos + ij_len + 1 >= buf_size) return 0;
                memcpy(buf + pos, tc->input_json, ij_len);
                pos += ij_len;

                if (pos + 1 >= buf_size) return 0;
                buf[pos++] = '}';
            }

            /* Close content array and assistant object */
            const char *asst_close = "]}";
            size_t ac_len = strlen(asst_close);
            if (pos + ac_len >= buf_size) return 0;
            memcpy(buf + pos, asst_close, ac_len);
            pos += ac_len;
            continue;
        }

        /* ---- Assistant message with tool_calls (OpenAI) ---- */
        if (!is_anthropic && msg->role == AI_ROLE_ASSISTANT && msg->n_tool_calls > 0) {
            if (emitted) {
                if (pos + 1 >= buf_size) return 0;
                buf[pos++] = ',';
            }
            emitted = 1;

            int rn = snprintf(buf + pos, buf_size - pos,
                              "{\"role\":\"assistant\",\"content\":");
            if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
            pos += (size_t)rn;

            pos = json_escape_string(ai_msg_content(msg), strlen(ai_msg_content(msg)), buf, buf_size, pos, 1);
            if (pos == 0) return 0;

            const char *tc_open = ",\"tool_calls\":[";
            size_t tco_len = strlen(tc_open);
            if (pos + tco_len >= buf_size) return 0;
            memcpy(buf + pos, tc_open, tco_len);
            pos += tco_len;

            for (int t = 0; t < msg->n_tool_calls; t++) {
                const AiToolCall *tc = &msg->tool_calls[t];
                if (t > 0) {
                    if (pos + 1 >= buf_size) return 0;
                    buf[pos++] = ',';
                }

                int trn = snprintf(buf + pos, buf_size - pos,
                                   "{\"id\":");
                if (trn < 0 || pos + (size_t)trn >= buf_size) return 0;
                pos += (size_t)trn;

                pos = json_escape_string(tc->id, strlen(tc->id), buf, buf_size, pos, 1);
                if (pos == 0) return 0;

                const char *fn_open = ",\"type\":\"function\",\"function\":{\"name\":";
                size_t fno_len = strlen(fn_open);
                if (pos + fno_len >= buf_size) return 0;
                memcpy(buf + pos, fn_open, fno_len);
                pos += fno_len;

                pos = json_escape_string(tc->name, strlen(tc->name), buf, buf_size, pos, 1);
                if (pos == 0) return 0;

                const char *args_key = ",\"arguments\":";
                size_t ak_len = strlen(args_key);
                if (pos + ak_len >= buf_size) return 0;
                memcpy(buf + pos, args_key, ak_len);
                pos += ak_len;

                /* arguments is a JSON string in OpenAI format */
                pos = json_escape_string(tc->input_json, strlen(tc->input_json), buf, buf_size, pos, 1);
                if (pos == 0) return 0;

                /* Close function object and tool_call object */
                const char *tc_close = "}}";
                size_t tcc2_len = strlen(tc_close);
                if (pos + tcc2_len >= buf_size) return 0;
                memcpy(buf + pos, tc_close, tcc2_len);
                pos += tcc2_len;
            }

            /* Close tool_calls array and assistant object */
            const char *tca_close = "]}";
            size_t tcac_len = strlen(tca_close);
            if (pos + tcac_len >= buf_size) return 0;
            memcpy(buf + pos, tca_close, tcac_len);
            pos += tcac_len;
            continue;
        }

        /* ---- Normal message (system/user/assistant without tool_calls) ---- */
        if (emitted) {
            if (pos + 1 >= buf_size) return 0;
            buf[pos++] = ',';
        }
        emitted = 1;

        const char *role = role_str(msg->role);

        /* Check if this is the last user message with a multimodal attachment */
        int is_multimodal = (msg->role == AI_ROLE_USER
                             && i == conv->msg_count - 1
                             && last_user_attachment
                             && last_user_attachment->base64_url);

        if (is_multimodal) {
            /* Emit content as array: [{"type":"text","text":"..."},{"type":"image_url","image_url":{"url":"..."}}] */
            int rn = snprintf(buf + pos, buf_size - pos,
                              "{\"role\":\"%s\",\"content\":[{\"type\":\"text\",\"text\":", role);
            if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
            pos += (size_t)rn;

            pos = json_escape_string(ai_msg_content(msg), strlen(ai_msg_content(msg)), buf, buf_size, pos, 1);
            if (pos == 0) return 0;

            /* image_url part — base64_url is already a complete data URI */
            const char *img_mid = "},{\"type\":\"image_url\",\"image_url\":{\"url\":";
            size_t img_mid_len = strlen(img_mid);
            if (pos + img_mid_len >= buf_size) return 0;
            memcpy(buf + pos, img_mid, img_mid_len);
            pos += img_mid_len;

            pos = json_escape_string(last_user_attachment->base64_url, strlen(last_user_attachment->base64_url), buf, buf_size, pos, 1);
            if (pos == 0) return 0;

            const char *img_close = "}}]}";
            size_t img_close_len = strlen(img_close);
            if (pos + img_close_len >= buf_size) return 0;
            memcpy(buf + pos, img_close, img_close_len);
            pos += img_close_len;
        } else {
            int rn = snprintf(buf + pos, buf_size - pos, "{\"role\":\"%s\",\"content\":", role);
            if (rn < 0 || pos + (size_t)rn >= buf_size) return 0;
            pos += (size_t)rn;

            pos = json_escape_string(ai_msg_content(msg), strlen(ai_msg_content(msg)), buf, buf_size, pos, 1);
            if (pos == 0) return 0;

            if (pos + 1 >= buf_size) return 0;
            buf[pos++] = '}';
        }
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
    return ai_build_request_body_ex(conv, NULL, buf, buf_size, 0, NULL);
}

size_t ai_build_request_body_tools(const AiConversation *conv,
                                    const AiAttachment *last_user_attachment,
                                    const char *tools_json,
                                    char *buf, size_t buf_size,
                                    int stream, const char *provider)
{
    if (!tools_json || tools_json[0] == '\0')
        return ai_build_request_body_ex(conv, last_user_attachment,
                                        buf, buf_size, stream, provider);

    /* Build without tools first to get the base body, then inject tools
     * before the closing '}' of the top-level object. */
    if (!conv || !buf || buf_size == 0) return 0;

    size_t base = ai_build_request_body_ex(conv, last_user_attachment,
                                           buf, buf_size, stream, provider);
    if (base == 0) return 0;

    /* base points past the final '}' — remove it and append tools */
    /* buf[base-1] == '}', buf[base-2] == ']' from the messages close */
    if (base < 2 || buf[base - 1] != '}') return 0;

    /* We need to insert: ,"tools":[...]} */
    size_t tj_len = strlen(tools_json);
    const char *tools_prefix = ",\"tools\":";
    size_t tp_len = strlen(tools_prefix);

    /* Check space: we're replacing the last '}' with ,"tools":<json>} */
    if (base - 1 + tp_len + tj_len + 1 + 1 >= buf_size) return 0;

    size_t pos = base - 1; /* overwrite the closing '}' */
    memcpy(buf + pos, tools_prefix, tp_len);
    pos += tp_len;
    memcpy(buf + pos, tools_json, tj_len);
    pos += tj_len;
    buf[pos++] = '}';
    buf[pos] = '\0';

    return pos;
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

    /* Anthropic Messages API: top-level "content" is an array of blocks */
    JsonNode *content_arr = json_obj_get(root, "content");
    if (content_arr && content_arr->type == JSON_ARRAY) {
        for (size_t i = 0; i < vec_size(&content_arr->as.arr); i++) {
            JsonNode *block = (JsonNode *)vec_get(&content_arr->as.arr, i);
            if (!block || block->type != JSON_OBJECT) continue;
            const char *block_type = json_obj_str(block, "type");
            if (!block_type) continue;
            if (strcmp(block_type, "text") == 0) {
                const char *text = json_obj_str(block, "text");
                if (text) snprintf(content_out, content_size, "%s", text);
            } else if (strcmp(block_type, "thinking") == 0) {
                const char *thinking = json_obj_str(block, "thinking");
                if (thinking && thinking_out && thinking_size > 0)
                    snprintf(thinking_out, thinking_size, "%s", thinking);
            }
        }
        json_free(root);
        return content_out[0] ? 0 : -1;
    }

    /* OpenAI / OpenAI-compatible format: choices[0].message.content */
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
                          char *thinking_out, size_t thinking_size,
                          int *input_tokens_out, int *output_tokens_out)
{
    if (!json) return -1;
    if (content_out && content_size > 0) content_out[0] = '\0';
    if (thinking_out && thinking_size > 0) thinking_out[0] = '\0';
    if (input_tokens_out) *input_tokens_out = 0;
    if (output_tokens_out) *output_tokens_out = 0;

    /* Check for stream termination (OpenAI format) */
    if (strcmp(json, "[DONE]") == 0) return 1;

    JsonNode *root = json_parse(json);
    if (!root) return -1;

    /* Detect format by top-level "type" key (Anthropic) vs "choices" (OpenAI) */
    const char *type = json_obj_str(root, "type");
    if (type) {
        /* Anthropic Messages API SSE format */
        if (strcmp(type, "message_stop") == 0) {
            json_free(root);
            return 1; /* stream done */
        }
        if (strcmp(type, "content_block_delta") == 0) {
            JsonNode *delta = json_obj_get(root, "delta");
            if (delta && delta->type == JSON_OBJECT) {
                const char *delta_type = json_obj_str(delta, "type");
                if (delta_type && strcmp(delta_type, "text_delta") == 0) {
                    const char *text = json_obj_str(delta, "text");
                    if (text && content_out && content_size > 0)
                        snprintf(content_out, content_size, "%s", text);
                } else if (delta_type && strcmp(delta_type, "thinking_delta") == 0) {
                    const char *thinking = json_obj_str(delta, "thinking");
                    if (thinking && thinking_out && thinking_size > 0)
                        snprintf(thinking_out, thinking_size, "%s", thinking);
                }
            }
        } else if (strcmp(type, "message_start") == 0) {
            /* Extract input token count from message.usage.input_tokens */
            if (input_tokens_out) {
                JsonNode *message = json_obj_get(root, "message");
                if (message && message->type == JSON_OBJECT) {
                    JsonNode *usage = json_obj_get(message, "usage");
                    if (usage && usage->type == JSON_OBJECT) {
                        JsonNode *itok = json_obj_get(usage, "input_tokens");
                        if (itok && itok->type == JSON_NUMBER)
                            *input_tokens_out = (int)itok->as.num_val;
                    }
                }
            }
        } else if (strcmp(type, "message_delta") == 0) {
            /* Extract output token count from usage.output_tokens */
            if (output_tokens_out) {
                JsonNode *usage = json_obj_get(root, "usage");
                if (usage && usage->type == JSON_OBJECT) {
                    JsonNode *otok = json_obj_get(usage, "output_tokens");
                    if (otok && otok->type == JSON_NUMBER)
                        *output_tokens_out = (int)otok->as.num_val;
                }
            }
        }
        /* Other Anthropic events (ping, content_block_start,
         * content_block_stop) — not an error, just no content */
        json_free(root);
        return 0;
    }

    /* OpenAI / OpenAI-compatible format: choices[0].delta */
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
    if (delta && delta->type == JSON_OBJECT) {
        const char *content = json_obj_str(delta, "content");
        if (content && content_out && content_size > 0)
            snprintf(content_out, content_size, "%s", content);

        const char *reasoning = json_obj_str(delta, "reasoning_content");
        if (reasoning && thinking_out && thinking_size > 0)
            snprintf(thinking_out, thinking_size, "%s", reasoning);
    }
    /* Some chunks may have empty/missing delta (e.g. role-only) — not an error */

    /* Extract token usage from top-level "usage" field (last OpenAI chunk) */
    JsonNode *usage = json_obj_get(root, "usage");
    if (usage && usage->type == JSON_OBJECT) {
        if (input_tokens_out) {
            JsonNode *ptok = json_obj_get(usage, "prompt_tokens");
            if (ptok && ptok->type == JSON_NUMBER)
                *input_tokens_out = (int)ptok->as.num_val;
        }
        if (output_tokens_out) {
            JsonNode *ctok = json_obj_get(usage, "completion_tokens");
            if (ctok && ctok->type == JSON_NUMBER)
                *output_tokens_out = (int)ctok->as.num_val;
        }
    }

    json_free(root);
    return 0;
}

int ai_provider_supports_tools(const char *provider)
{
    if (!provider) return 0;
    if (strcmp(provider, "anthropic") == 0) return 1;
    if (strcmp(provider, "openai") == 0) return 1;
    if (strcmp(provider, "deepseek") == 0) return 1;
    return 0;  /* unknown providers: safe default */
}

int ai_parse_stream_chunk_ex(const char *json,
                             char *content_out, size_t content_size,
                             char *thinking_out, size_t thinking_size,
                             AiToolStreamState *tool_stream,
                             const char *provider)
{
    if (!tool_stream)
        return ai_parse_stream_chunk(json, content_out, content_size,
                                     thinking_out, thinking_size,
                                     NULL, NULL);

    if (provider && strcmp(provider, "anthropic") == 0) {
        int trc = ai_parse_tool_stream_anthropic(json, tool_stream);
        if (trc == 1) return 2;   /* tool_use stop */
        if (trc == 2) return 1;   /* end_turn / message_stop */
        if (trc == -1) return -1; /* error */
        /* trc == 0: normal chunk — also extract any text/thinking deltas */
        return ai_parse_stream_chunk(json, content_out, content_size,
                                     thinking_out, thinking_size,
                                     NULL, NULL);
    } else {
        /* OpenAI-compatible */
        int trc = ai_parse_tool_stream_openai(json, tool_stream);
        if (trc == 1) return 2;   /* tool_use stop */
        if (trc == 2) return 1;   /* done */
        if (trc == -1) return -1; /* error */
        return ai_parse_stream_chunk(json, content_out, content_size,
                                     thinking_out, thinking_size,
                                     NULL, NULL);
    }
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
                         "--- You ---\n%s\n\n", ai_msg_content(msg));
            if (n > 0) pos += (size_t)n;
        } else if (msg->role == AI_ROLE_ASSISTANT) {
            if (show_thinking && thinking && thinking[i] &&
                thinking[i][0]) {
                n = snprintf(buf + pos, buf_size - pos,
                             "--- Thinking ---\n%s\n\n", thinking[i]);
                if (n > 0) pos += (size_t)n;
            }
            n = snprintf(buf + pos, buf_size - pos,
                         "--- AI ---\n%s\n\n", ai_msg_content(msg));
            if (n > 0) pos += (size_t)n;
        }
    }

    if (pos >= buf_size) pos = buf_size - 1;
    buf[pos] = '\0';
    return pos;
}

void ai_build_auth_headers(const char *provider, const char *api_key,
                            char *hdr0, size_t hdr0_size,
                            char *hdr1, size_t hdr1_size,
                            const char *out[3])
{
    if (!provider || !api_key || !hdr0 || !hdr1 || !out) return;

    if (strcmp(provider, "anthropic") == 0) {
        snprintf(hdr0, hdr0_size, "x-api-key: %s", api_key);
        snprintf(hdr1, hdr1_size, "anthropic-version: 2023-06-01");
        out[0] = hdr0;
        out[1] = hdr1;
        out[2] = NULL;
    } else {
        snprintf(hdr0, hdr0_size, "Authorization: Bearer %s", api_key);
        out[0] = hdr0;
        out[1] = NULL;
        out[2] = NULL;
    }
}
