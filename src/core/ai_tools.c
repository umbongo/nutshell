#include "ai_tools.h"
#include "json_parser.h"
#include "json_validate.h"
#include "dlog.h"
#include "dlog_query.h"
#include "net_guard.h"
#include "paste_filter.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- JSON node to string helper ------------------------------------------ */

/* Forward declaration for recursive use */
static size_t json_node_to_string(const JsonNode *node, char *buf, size_t max);

static size_t json_object_to_string(const JsonNode *node, char *buf, size_t max)
{
    if (!node || node->type != JSON_OBJECT) return 0;

    size_t pos = 0;
    if (pos + 1 >= max) return 0;
    buf[pos++] = '{';

    size_t n = vec_size(&node->as.obj.keys);
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            if (pos + 1 >= max) return 0;
            buf[pos++] = ',';
        }

        /* write key as quoted string */
        const char *key = (const char *)vec_get(&node->as.obj.keys, i);
        if (!key) return 0;

        if (pos + 1 >= max) return 0;
        buf[pos++] = '"';
        size_t key_len = strlen(key);
        size_t new_pos = json_escape_string(key, key_len, buf, max, pos, 0);
        if (new_pos == 0 && key_len > 0) return 0;
        pos = new_pos;
        if (pos + 1 >= max) return 0;
        buf[pos++] = '"';

        if (pos + 1 >= max) return 0;
        buf[pos++] = ':';

        /* write value */
        const JsonNode *val = (const JsonNode *)vec_get(&node->as.obj.vals, i);
        size_t written = json_node_to_string(val, buf + pos, max - pos);
        if (written == 0 && val != NULL) return 0;
        pos += written;
    }

    if (pos + 1 >= max) return 0;
    buf[pos++] = '}';
    buf[pos] = '\0';
    return pos;
}

static size_t json_array_to_string(const JsonNode *node, char *buf, size_t max)
{
    if (!node || node->type != JSON_ARRAY) return 0;

    size_t pos = 0;
    if (pos + 1 >= max) return 0;
    buf[pos++] = '[';

    size_t n = vec_size(&node->as.arr);
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            if (pos + 1 >= max) return 0;
            buf[pos++] = ',';
        }
        const JsonNode *elem = (const JsonNode *)vec_get(&node->as.arr, i);
        size_t written = json_node_to_string(elem, buf + pos, max - pos);
        if (written == 0 && elem != NULL) return 0;
        pos += written;
    }

    if (pos + 1 >= max) return 0;
    buf[pos++] = ']';
    buf[pos] = '\0';
    return pos;
}

/* Serialize a JsonNode back to JSON string.
 * Returns bytes written (0 for null node, or on overflow). */
static size_t json_node_to_string(const JsonNode *node, char *buf, size_t max)
{
    if (!node || max == 0) return 0;

    int n;

    switch (node->type) {
        case JSON_NULL:
            if (max < 5) return 0;
            memcpy(buf, "null", 5);
            return 4;

        case JSON_BOOL:
            if (node->as.bool_val) {
                if (max < 5) return 0;
                memcpy(buf, "true", 5);
                return 4;
            } else {
                if (max < 6) return 0;
                memcpy(buf, "false", 6);
                return 5;
            }

        case JSON_NUMBER: {
            /* Use %g to avoid trailing zeros; fall back to %f for large ints */
            n = snprintf(buf, max, "%.17g", node->as.num_val);
            if (n < 0 || (size_t)n >= max) return 0;
            return (size_t)n;
        }

        case JSON_STRING: {
            if (!node->as.str_val) {
                if (max < 3) return 0;
                memcpy(buf, "\"\"", 3);
                return 2;
            }
            size_t pos = 0;
            if (pos + 1 >= max) return 0;
            buf[pos++] = '"';
            size_t slen = strlen(node->as.str_val);
            size_t new_pos = json_escape_string(node->as.str_val, slen,
                                                buf, max, pos, 0);
            if (new_pos == 0 && slen > 0) return 0;
            pos = new_pos;
            if (pos + 1 >= max) return 0;
            buf[pos++] = '"';
            buf[pos] = '\0';
            return pos;
        }

        case JSON_OBJECT:
            return json_object_to_string(node, buf, max);

        case JSON_ARRAY:
            return json_array_to_string(node, buf, max);
    }

    return 0;
}

/* ---- Registry ------------------------------------------------------------ */

void ai_tools_init(AiToolRegistry *reg)
{
    if (!reg) return;
    memset(reg, 0, sizeof(*reg));
}

int ai_tools_register(AiToolRegistry *reg, const AiToolDef *tool)
{
    if (!reg || !tool) return -1;
    if (reg->count >= AI_TOOL_MAX) return -1;

    reg->tools[reg->count] = *tool;
    reg->count++;
    return 0;
}

const AiToolDef *ai_tools_find(const AiToolRegistry *reg, const char *name)
{
    if (!reg || !name) return NULL;
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->tools[i].name, name) == 0)
            return &reg->tools[i];
    }
    return NULL;
}

/* ---- Serialization helpers ----------------------------------------------- */

/* Append src to buf at *pos with bounds checking.
 * Returns 0 on success, -1 on overflow. */
static int buf_append(char *buf, size_t max, size_t *pos, const char *src)
{
    size_t len = strlen(src);
    if (*pos + len >= max) return -1;
    memcpy(buf + *pos, src, len);
    *pos += len;
    buf[*pos] = '\0';
    return 0;
}

/* ---- Anthropic serialization --------------------------------------------- */

int ai_tools_serialize_anthropic(const AiToolRegistry *reg, char *buf, size_t max)
{
    if (!reg || !buf || max == 0) return -1;

    size_t pos = 0;
    if (buf_append(buf, max, &pos, "[") < 0) return -1;

    for (int i = 0; i < reg->count; i++) {
        const AiToolDef *t = &reg->tools[i];

        if (i > 0) {
            if (buf_append(buf, max, &pos, ",") < 0) return -1;
        }

        if (buf_append(buf, max, &pos, "{\"name\":\"") < 0) return -1;

        /* name is already safe (no special chars expected), but escape it */
        size_t new_pos = json_escape_string(t->name, strlen(t->name),
                                            buf, max, pos, 0);
        if (new_pos == 0 && t->name[0] != '\0') return -1;
        pos = new_pos;

        if (buf_append(buf, max, &pos, "\",\"description\":\"") < 0) return -1;

        new_pos = json_escape_string(t->description, strlen(t->description),
                                     buf, max, pos, 0);
        if (new_pos == 0 && t->description[0] != '\0') return -1;
        pos = new_pos;

        if (buf_append(buf, max, &pos, "\",\"input_schema\":") < 0) return -1;

        /* Embed schema verbatim */
        if (buf_append(buf, max, &pos, t->input_schema_json) < 0) return -1;

        if (buf_append(buf, max, &pos, "}") < 0) return -1;
    }

    if (buf_append(buf, max, &pos, "]") < 0) return -1;

    return (int)pos;
}

/* ---- OpenAI serialization ------------------------------------------------ */

int ai_tools_serialize_openai(const AiToolRegistry *reg, char *buf, size_t max)
{
    if (!reg || !buf || max == 0) return -1;

    size_t pos = 0;
    size_t new_pos;
    if (buf_append(buf, max, &pos, "[") < 0) return -1;

    for (int i = 0; i < reg->count; i++) {
        const AiToolDef *t = &reg->tools[i];

        if (i > 0) {
            if (buf_append(buf, max, &pos, ",") < 0) return -1;
        }

        if (buf_append(buf, max, &pos, "{\"type\":\"function\",\"function\":{\"name\":\"") < 0) return -1;

        new_pos = json_escape_string(t->name, strlen(t->name),
                                     buf, max, pos, 0);
        if (new_pos == 0 && t->name[0] != '\0') return -1;
        pos = new_pos;

        if (buf_append(buf, max, &pos, "\",\"description\":\"") < 0) return -1;

        new_pos = json_escape_string(t->description, strlen(t->description),
                                     buf, max, pos, 0);
        if (new_pos == 0 && t->description[0] != '\0') return -1;
        pos = new_pos;

        if (buf_append(buf, max, &pos, "\",\"parameters\":") < 0) return -1;

        /* Embed schema verbatim as "parameters" */
        if (buf_append(buf, max, &pos, t->input_schema_json) < 0) return -1;

        if (buf_append(buf, max, &pos, "}}") < 0) return -1;
    }

    if (buf_append(buf, max, &pos, "]") < 0) return -1;

    return (int)pos;
}

/* ---- Anthropic response parsing ------------------------------------------ */

int ai_parse_tool_calls_anthropic(const char *response_json,
                                   AiToolCall *calls, int max_calls)
{
    if (!response_json || !calls || max_calls <= 0) return -1;

    JsonNode *root = json_parse(response_json);
    if (!root) return -1;

    JsonNode *content_arr = json_obj_get(root, "content");
    if (!content_arr || content_arr->type != JSON_ARRAY) {
        json_free(root);
        return 0;
    }

    int count = 0;
    size_t n = vec_size(&content_arr->as.arr);

    for (size_t i = 0; i < n && count < max_calls; i++) {
        JsonNode *block = (JsonNode *)vec_get(&content_arr->as.arr, i);
        if (!block || block->type != JSON_OBJECT) continue;

        const char *type = json_obj_str(block, "type");
        if (!type || strcmp(type, "tool_use") != 0) continue;

        const char *id   = json_obj_str(block, "id");
        const char *name = json_obj_str(block, "name");
        JsonNode   *inp  = json_obj_get(block, "input");

        if (!id || !name) continue;

        AiToolCall *tc = &calls[count];
        memset(tc, 0, sizeof(*tc));

        snprintf(tc->id,   sizeof(tc->id),   "%s", id);
        snprintf(tc->name, sizeof(tc->name), "%s", name);

        if (inp) {
            /* Re-serialize the input JsonNode to a JSON string */
            size_t written = json_node_to_string(inp, tc->input_json,
                                                 sizeof(tc->input_json));
            if (written == 0) {
                /* Failed to serialize — use empty object */
                snprintf(tc->input_json, sizeof(tc->input_json), "{}");
            }
        } else {
            snprintf(tc->input_json, sizeof(tc->input_json), "{}");
        }

        count++;
    }

    json_free(root);
    return count;
}

/* ---- OpenAI response parsing --------------------------------------------- */

int ai_parse_tool_calls_openai(const char *response_json,
                                AiToolCall *calls, int max_calls)
{
    if (!response_json || !calls || max_calls <= 0) return -1;

    JsonNode *root = json_parse(response_json);
    if (!root) return -1;

    JsonNode *choices = json_obj_get(root, "choices");
    if (!choices || choices->type != JSON_ARRAY
            || vec_size(&choices->as.arr) == 0) {
        json_free(root);
        return 0;
    }

    JsonNode *first = (JsonNode *)vec_get(&choices->as.arr, 0);
    if (!first || first->type != JSON_OBJECT) {
        json_free(root);
        return 0;
    }

    JsonNode *message = json_obj_get(first, "message");
    if (!message || message->type != JSON_OBJECT) {
        json_free(root);
        return 0;
    }

    JsonNode *tool_calls = json_obj_get(message, "tool_calls");
    if (!tool_calls || tool_calls->type != JSON_ARRAY) {
        json_free(root);
        return 0;
    }

    int count = 0;
    size_t n = vec_size(&tool_calls->as.arr);

    for (size_t i = 0; i < n && count < max_calls; i++) {
        JsonNode *tc_node = (JsonNode *)vec_get(&tool_calls->as.arr, i);
        if (!tc_node || tc_node->type != JSON_OBJECT) continue;

        const char *id = json_obj_str(tc_node, "id");

        JsonNode *func = json_obj_get(tc_node, "function");
        if (!func || func->type != JSON_OBJECT) continue;

        const char *name      = json_obj_str(func, "name");
        const char *arguments = json_obj_str(func, "arguments");

        if (!id || !name) continue;

        AiToolCall *tc = &calls[count];
        memset(tc, 0, sizeof(*tc));

        snprintf(tc->id,   sizeof(tc->id),   "%s", id);
        snprintf(tc->name, sizeof(tc->name), "%s", name);

        if (arguments) {
            snprintf(tc->input_json, sizeof(tc->input_json), "%s", arguments);
        } else {
            snprintf(tc->input_json, sizeof(tc->input_json), "{}");
        }

        count++;
    }

    json_free(root);
    return count;
}

/* ---- Tool execution ------------------------------------------------------ */

int ai_tool_execute(const AiToolRegistry *reg, const AiToolCall *call,
                    volatile int *cancel_flag, AiToolResult *result)
{
    if (!reg || !call || !result) return -1;

    memset(result, 0, sizeof(*result));
    snprintf(result->tool_use_id, sizeof(result->tool_use_id), "%s", call->id);

    const AiToolDef *tool = ai_tools_find(reg, call->name);
    if (!tool) {
        /* Unknown tool — produce an error result */
        /* Nutshell's own text: it goes in tool_note, not content. */
        snprintf(result->tool_note, sizeof(result->tool_note),
                 "Unknown tool: %s", call->name);
        result->is_error = 1;
        result->was_truncated = 0;
        return -1;
    }

    int rc = tool->execute(call->input_json, tool->tool_data, cancel_flag,
                           &result->content, &result->content_len,
                           &result->was_truncated);
    result->is_error = (rc != 0) ? 1 : 0;

    /* A failure's text is Nutshell's own (a refusal, a limit, "no such host")
     * for every tool but web_search, whose errors may quote the provider's
     * response and so stay with the data, and a tool that says its text
     * quotes remote data (AI_TOOL_FAIL_CONTENT). */
    if (result->is_error && rc != AI_TOOL_FAIL_CONTENT &&
        strcmp(call->name, "web_search") != 0) {
        size_t n = result->content ? result->content_len : 0;
        if (n > AI_TOOL_NOTE_MAX - 1) {
            n = AI_TOOL_NOTE_MAX - 1;
            while (n > 0 && ((unsigned char)result->content[n] & 0xC0) == 0x80)
                n--;
        }
        if (n > 0) memcpy(result->tool_note, result->content, n);
        result->tool_note[n] = '\0';
        free(result->content);
        result->content = NULL;
        result->content_len = 0;
    }
    return rc;
}

/* ---- Result serialization ------------------------------------------------ */

int ai_tool_result_anthropic(const AiToolResult *result, char *buf, size_t max)
{
    if (!result || !buf || max == 0) return -1;

    size_t pos = 0;

    if (buf_append(buf, max, &pos,
                   "{\"type\":\"tool_result\",\"tool_use_id\":\"") < 0) return -1;

    size_t new_pos = json_escape_string(result->tool_use_id,
                                        strlen(result->tool_use_id),
                                        buf, max, pos, 0);
    if (new_pos == 0 && result->tool_use_id[0] != '\0') return -1;
    pos = new_pos;

    if (buf_append(buf, max, &pos, "\",\"content\":\"") < 0) return -1;

    if (result->content && result->content_len > 0) {
        new_pos = json_escape_string(result->content,
                                     result->content_len,
                                     buf, max, pos, 0);
        if (new_pos == 0 && result->content_len > 0) return -1;
        pos = new_pos;
    }

    if (buf_append(buf, max, &pos, "\"") < 0) return -1;

    if (result->is_error) {
        if (buf_append(buf, max, &pos, ",\"is_error\":true") < 0) return -1;
    }

    if (buf_append(buf, max, &pos, "}") < 0) return -1;

    return (int)pos;
}

int ai_tool_result_openai(const AiToolResult *result, char *buf, size_t max)
{
    if (!result || !buf || max == 0) return -1;

    size_t pos = 0;

    if (buf_append(buf, max, &pos,
                   "{\"role\":\"tool\",\"tool_call_id\":\"") < 0) return -1;

    size_t new_pos = json_escape_string(result->tool_use_id,
                                        strlen(result->tool_use_id),
                                        buf, max, pos, 0);
    if (new_pos == 0 && result->tool_use_id[0] != '\0') return -1;
    pos = new_pos;

    if (buf_append(buf, max, &pos, "\",\"content\":\"") < 0) return -1;

    if (result->content && result->content_len > 0) {
        new_pos = json_escape_string(result->content,
                                     result->content_len,
                                     buf, max, pos, 0);
        if (new_pos == 0 && result->content_len > 0) return -1;
        pos = new_pos;
    }

    if (buf_append(buf, max, &pos, "\"}") < 0) return -1;

    return (int)pos;
}

/* ---- Schema validation --------------------------------------------------- */

int ai_tools_validate_schemas(const AiToolRegistry *reg)
{
    if (!reg) return -1;

    for (int i = 0; i < reg->count; i++) {
        JsonNode *node = json_parse(reg->tools[i].input_schema_json);
        if (!node) return -1;
        json_free(node);
    }
    return 0;
}

/* ---- Streaming state management ------------------------------------------ */

void ai_tools_stream_init(AiToolStreamState *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->current_block_type = BLOCK_NONE;
    state->pending_tool_calls = NULL;
}

void ai_tools_stream_reset(AiToolStreamState *state)
{
    if (!state) return;
    if (state->pending_tool_calls) {
        free(state->pending_tool_calls);
    }
    memset(state, 0, sizeof(*state));
    state->current_block_type = BLOCK_NONE;
    state->pending_tool_calls = NULL;
}

/* Append partial_json to tool_call->input_json.
 * Returns 0 on success, -1 on overflow. */
static int stream_append_input(AiToolCall *tool_call, const char *partial)
{
    if (!tool_call || !partial) return 0;

    size_t existing = strlen(tool_call->input_json);
    size_t added    = strlen(partial);

    if (existing + added >= (size_t)AI_TOOL_INPUT_MAX) {
        return -1;  /* overflow */
    }

    memcpy(tool_call->input_json + existing, partial, added);
    tool_call->input_json[existing + added] = '\0';
    return 0;
}

/* Add completed active_tool_call to pending_tool_calls array.
 * Returns 0 on success, -1 on alloc failure. */
static int stream_commit_tool(AiToolStreamState *state)
{
    if (!state) return -1;

    /* Grow array if needed — initial cap 4, double on overflow */
    if (state->pending_tool_count >= state->pending_tool_cap) {
        int new_cap = (state->pending_tool_cap == 0) ? 4 : state->pending_tool_cap * 2;
        AiToolCall *grown = (AiToolCall *)realloc(state->pending_tool_calls,
                                                   (size_t)new_cap * sizeof(AiToolCall));
        if (!grown) return -1;
        state->pending_tool_calls = grown;
        state->pending_tool_cap   = new_cap;
    }

    state->pending_tool_calls[state->pending_tool_count] = state->active_tool_call;
    state->pending_tool_count++;
    return 0;
}

/* ---- Anthropic streaming parser ------------------------------------------ */

int ai_parse_tool_stream_anthropic(const char *chunk_json, AiToolStreamState *state)
{
    if (!chunk_json || !state) return -1;

    JsonNode *root = json_parse(chunk_json);
    if (!root) return -1;

    const char *type = json_obj_str(root, "type");
    if (!type) {
        json_free(root);
        return -1;
    }

    int ret = 0;

    if (strcmp(type, "content_block_start") == 0) {
        JsonNode *cb = json_obj_get(root, "content_block");
        if (cb) {
            const char *cb_type = json_obj_str(cb, "type");
            if (cb_type) {
                if (strcmp(cb_type, "text") == 0) {
                    state->current_block_type = BLOCK_TEXT;
                } else if (strcmp(cb_type, "tool_use") == 0) {
                    state->current_block_type = BLOCK_TOOL_USE;
                    memset(&state->active_tool_call, 0, sizeof(state->active_tool_call));

                    const char *id   = json_obj_str(cb, "id");
                    const char *name = json_obj_str(cb, "name");

                    if (id)   snprintf(state->active_tool_call.id,
                                       sizeof(state->active_tool_call.id),   "%s", id);
                    if (name) snprintf(state->active_tool_call.name,
                                       sizeof(state->active_tool_call.name), "%s", name);

                    state->tool_call_active = 1;
                }
            }
        }

    } else if (strcmp(type, "content_block_delta") == 0) {
        JsonNode *delta = json_obj_get(root, "delta");
        if (delta) {
            const char *delta_type = json_obj_str(delta, "type");
            if (delta_type) {
                if (strcmp(delta_type, "text_delta") == 0
                        && state->current_block_type == BLOCK_TEXT) {
                    /* Caller handles text display — just return 0 */
                } else if (strcmp(delta_type, "input_json_delta") == 0
                           && state->current_block_type == BLOCK_TOOL_USE) {
                    const char *partial = json_obj_str(delta, "partial_json");
                    if (partial) {
                        if (stream_append_input(&state->active_tool_call, partial) < 0) {
                            json_free(root);
                            return -1;
                        }
                    }
                }
            }
        }

    } else if (strcmp(type, "content_block_stop") == 0) {
        if (state->current_block_type == BLOCK_TOOL_USE) {
            if (stream_commit_tool(state) < 0) {
                json_free(root);
                return -1;
            }
            state->tool_call_active = 0;
        }
        state->current_block_type = BLOCK_NONE;

    } else if (strcmp(type, "message_delta") == 0) {
        JsonNode *delta = json_obj_get(root, "delta");
        if (delta) {
            const char *stop_reason = json_obj_str(delta, "stop_reason");
            if (stop_reason) {
                snprintf(state->finish, sizeof(state->finish), "%s", stop_reason);
                if (strcmp(stop_reason, "tool_use") == 0) {
                    ret = 1;
                } else if (strcmp(stop_reason, "end_turn") == 0
                           || strcmp(stop_reason, "stop") == 0) {
                    ret = 2;
                }
            }
        }

    }
    /* message_start, ping: return 0 (ignore) */

    json_free(root);
    return ret;
}

/* ---- OpenAI streaming parser --------------------------------------------- */

/* Accumulate one delta.tool_calls array into the state. -1 on out of memory. */
static int openai_accumulate_tool_calls(AiToolStreamState *state,
                                        JsonNode *tool_calls)
{
    size_t n = vec_size(&tool_calls->as.arr);
    for (size_t i = 0; i < n; i++) {
        JsonNode *tc_node = (JsonNode *)vec_get(&tool_calls->as.arr, i);
        if (!tc_node || tc_node->type != JSON_OBJECT) continue;

        /* Get slot index. Some servers leave it out: that is index 0 (one
         * call), counted so the caller can log it once. */
        double idx_d = json_obj_num(tc_node, "index", -1.0);
        if (idx_d < 0.0) {
            idx_d = 0.0;
            state->index_defaulted++;
        }
        int slot = (int)idx_d;

        /* Grow pending_tool_calls array if needed to accommodate this slot */
        while (slot >= state->pending_tool_cap) {
            int new_cap = (state->pending_tool_cap == 0) ? 4 : state->pending_tool_cap * 2;
            AiToolCall *grown = (AiToolCall *)realloc(state->pending_tool_calls,
                                                       (size_t)new_cap * sizeof(AiToolCall));
            if (!grown) return -1;
            /* Zero-initialize the newly allocated slots */
            memset(grown + state->pending_tool_cap, 0,
                   (size_t)(new_cap - state->pending_tool_cap) * sizeof(AiToolCall));
            state->pending_tool_calls = grown;
            state->pending_tool_cap   = new_cap;
        }

        /* Extend count if this slot is new */
        if (slot >= state->pending_tool_count) {
            state->pending_tool_count = slot + 1;
        }

        /* First chunk: id and function.name */
        const char *id = json_obj_str(tc_node, "id");
        if (id) {
            snprintf(state->pending_tool_calls[slot].id,
                     sizeof(state->pending_tool_calls[slot].id), "%s", id);
        }

        JsonNode *func = json_obj_get(tc_node, "function");
        if (func && func->type == JSON_OBJECT) {
            const char *name = json_obj_str(func, "name");
            if (name) {
                snprintf(state->pending_tool_calls[slot].name,
                         sizeof(state->pending_tool_calls[slot].name), "%s", name);
            }

            const char *arguments = json_obj_str(func, "arguments");
            if (arguments && arguments[0] != '\0') {
                if (stream_append_input(&state->pending_tool_calls[slot], arguments) < 0)
                    return -1;
            }
        }
    }
    return 0;
}

int ai_parse_tool_stream_openai(const char *chunk_json, AiToolStreamState *state)
{
    if (!chunk_json || !state) return -1;

    JsonNode *root = json_parse(chunk_json);
    if (!root) return -1;

    /* Check choices array */
    JsonNode *choices = json_obj_get(root, "choices");
    if (!choices || choices->type != JSON_ARRAY || vec_size(&choices->as.arr) == 0) {
        json_free(root);
        return 0;
    }

    JsonNode *choice = (JsonNode *)vec_get(&choices->as.arr, 0);
    if (!choice || choice->type != JSON_OBJECT) {
        json_free(root);
        return 0;
    }

    /* The delta first: a call (or its last fragment) can ride in the same
     * chunk as the finish_reason, and acting on the finish first lost it. */
    JsonNode *delta = json_obj_get(choice, "delta");
    if (delta && delta->type == JSON_OBJECT) {
        JsonNode *tool_calls = json_obj_get(delta, "tool_calls");
        if (tool_calls && tool_calls->type == JSON_ARRAY &&
            openai_accumulate_tool_calls(state, tool_calls) < 0) {
            json_free(root);
            return -1;
        }
    }

    /* Then the finish_reason. */
    int ret = 0;
    const char *finish_reason = json_obj_str(choice, "finish_reason");
    if (finish_reason) {
        snprintf(state->finish, sizeof(state->finish), "%s", finish_reason);
        if (strcmp(finish_reason, "tool_calls") == 0)
            ret = 1;
        else if (strcmp(finish_reason, "stop") == 0)
            ret = 2;
    }

    json_free(root);
    return ret;
}

int ai_tools_stream_compact(AiToolStreamState *state)
{
    if (!state || !state->pending_tool_calls) return 0;
    int kept = 0;
    for (int i = 0; i < state->pending_tool_count; i++) {
        AiToolCall *c = &state->pending_tool_calls[i];
        if (c->name[0] == '\0' && c->id[0] == '\0') continue;
        if (kept != i) {
            memcpy(&state->pending_tool_calls[kept], c, sizeof(*c));
        }
        kept++;
    }
    int dropped = state->pending_tool_count - kept;
    for (int i = kept; i < state->pending_tool_count; i++)
        memset(&state->pending_tool_calls[i], 0, sizeof(AiToolCall));
    state->pending_tool_count = kept;
    return dropped;
}

int ai_tool_round_should_run(const char *provider, int tool_stop,
                             int pending, int cut_off)
{
    if (pending <= 0) return 0;
    if (provider && strcmp(provider, "anthropic") == 0)
        return tool_stop ? 1 : 0;
    return cut_off ? 0 : 1;
}

/* Append the sanitised form of src to out (cap bytes with the terminator,
 * limit bytes of content at most). */
static void stream_error_sanitise(const char *src, char *out, size_t cap,
                                  size_t limit)
{
    if (cap == 0) return;
    if (limit > cap - 1) limit = cap - 1;
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)src;
    while (*p) {
        size_t clen = 1;
        int drop = 0;
        int space = 0;          /* a multi-byte separator written as one ' ' */
        unsigned char c = *p;
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        } else if (c < 0x20 || c == 0x7F) {
            drop = 1;
        } else if (c >= 0x80) {
            /* Length of the UTF-8 sequence, or drop an invalid byte. */
            if (c >= 0xC2 && c <= 0xDF) clen = 2;
            else if (c >= 0xE0 && c <= 0xEF) clen = 3;
            else if (c >= 0xF0 && c <= 0xF4) clen = 4;
            else drop = 1;
            if (!drop) {
                for (size_t k = 1; k < clen; k++) {
                    if ((p[k] & 0xC0) != 0x80) { drop = 1; clen = 1; break; }
                }
            }
            if (!drop && clen == 2 && c == 0xC2 && p[1] >= 0x80 && p[1] <= 0x9F)
                drop = 1;                                   /* C1 controls */
            if (!drop && clen == 2 && c == 0xD8 && p[1] == 0x9C)
                drop = 1;                                   /* U+061C ALM */
            if (!drop && clen == 3 && c == 0xE2) {
                if (p[1] == 0x80 && (p[2] == 0xA8 || p[2] == 0xA9)) {
                    c = ' ';                   /* U+2028/2029 line/paragraph */
                    space = 1;
                }
                if (p[1] == 0x80 && (p[2] == 0x8E || p[2] == 0x8F ||
                                     (p[2] >= 0xAA && p[2] <= 0xAE)))
                    drop = 1;                  /* LRM/RLM, LRE..RLO */
                if (p[1] == 0x81 && p[2] >= 0xA6 && p[2] <= 0xA9)
                    drop = 1;                  /* LRI..PDI */
            }
        }
        if (!drop) {
            if (o + (space ? 1 : clen) > limit) break;   /* never split */
            if (space || clen == 1) out[o++] = (char)c;
            else { memcpy(out + o, p, clen); o += clen; }
        }
        p += clen;
    }
    out[o] = '\0';
}

int ai_stream_chunk_error(const char *provider, const char *json,
                          char *msg_out, size_t msg_size)
{
    if (!json || !json[0]) return 0;
    if (provider && strcmp(provider, "anthropic") == 0) return 0;
    JsonNode *root = json_parse(json);
    if (!root) return 0;
    if (root->type != JSON_OBJECT) { json_free(root); return 0; }

    JsonNode *err = json_obj_get(root, "error");
    int is_error = 0;
    const char *text = NULL;
    if (err && err->type == JSON_OBJECT) {
        is_error = 1;
        text = json_obj_str(err, "message");
        if (!text || !text[0]) text = json_obj_str(err, "type");
    } else if (err && err->type == JSON_STRING) {
        is_error = 1;
        text = err->as.str_val;
    }
    if (!is_error) { json_free(root); return 0; }

    if (msg_out && msg_size > 0) {
        stream_error_sanitise(text ? text : "", msg_out, msg_size,
                              AI_STREAM_ERROR_MSG_MAX);
        if (msg_out[0] == '\0')
            snprintf(msg_out, msg_size, "%s", "unknown error");
    }
    json_free(root);
    return 1;
}

/* ---- read_debug_log ------------------------------------------------------ */

void ai_debug_tool_ctx_init(DebugLogToolContext *ctx)
{
    if (ctx) ctx->calls = 0;
}

int ai_debug_tool_register(AiToolRegistry *reg, DebugLogToolContext *ctx)
{
    if (!reg || !ctx) return -1;
    AiToolDef def;
    memset(&def, 0, sizeof(def));
    snprintf(def.name, sizeof(def.name), "%s", DLOG_TOOL_NAME);
    snprintf(def.description, sizeof(def.description), "%s", dlog_tool_description());
    snprintf(def.input_schema_json, sizeof(def.input_schema_json), "%s", dlog_tool_schema());
    def.safety = TOOL_SAFE;            /* read-only: no approval */
    def.execute = ai_debug_tool_execute;
    def.tool_data = ctx;
    return ai_tools_register(reg, &def);
}

int ai_debug_tool_register_if_enabled(AiToolRegistry *reg, DebugLogToolContext *ctx)
{
    if (!dlog_enabled()) return 0;
    return ai_debug_tool_register(reg, ctx) == 0 ? 1 : 0;
}

/* Hand the caller a heap copy of a short text. Always returns non-zero (the
 * tool's error code), so callers can `return debug_tool_text(...)`. */
static int debug_tool_text(const char *msg, char **result_buf, size_t *result_len)
{
    size_t n = strlen(msg);
    char *p = (char *)malloc(n + 1);
    if (p) {
        memcpy(p, msg, n + 1);
        *result_buf = p;
        *result_len = n;
    }
    return -1;
}

int ai_debug_tool_execute(const char *input_json, void *tool_data,
                          volatile int *cancel_flag,
                          char **result_buf, size_t *result_len,
                          int *was_truncated)
{
    if (!result_buf || !result_len || !was_truncated) return -1;
    *result_buf = NULL;
    *result_len = 0;
    *was_truncated = 0;

    DebugLogToolContext *ctx = (DebugLogToolContext *)tool_data;
    if (!ctx) return debug_tool_text("debug log tool is not available", result_buf, result_len);
    if (cancel_flag && *cancel_flag)
        return debug_tool_text("cancelled", result_buf, result_len);

    /* The per-message cap. A refused call is not counted again, and a call
     * with bad arguments counts: the model must not loop on them either. */
    if (ctx->calls >= DLOG_TOOL_MAX_CALLS_PER_MSG)
        return debug_tool_text(AI_DEBUG_TOOL_LIMIT_MSG, result_buf, result_len);
    ctx->calls++;

    DlogQuery q;
    char err[160] = "";
    if (!dlog_query_from_json(input_json, &q, err, sizeof(err))) {
        char msg[220];
        snprintf(msg, sizeof(msg), "invalid arguments: %s", err[0] ? err : "not understood");
        DLOG(DLOG_AI, DLOG_INFO, 0, "ai.tool.read_debug_log", "n=%d ok=%d", ctx->calls, 0);
        return debug_tool_text(msg, result_buf, result_len);
    }

    char *out = (char *)malloc(DLOG_QUERY_OUT_MAX);
    if (!out) return debug_tool_text("out of memory", result_buf, result_len);
    dlog_flush();
    size_t n = dlog_read_log(&q, out, DLOG_QUERY_OUT_MAX);
    if (n >= DLOG_QUERY_OUT_MAX) n = DLOG_QUERY_OUT_MAX - 1;
    out[n] = '\0';

    /* The use is logged, never the output: it is the log itself. */
    DLOG(DLOG_AI, DLOG_INFO, 0, "ai.tool.read_debug_log",
         "n=%d ok=%d cat=%d since=%d min_level=%d summary=%d around=%d bytes=%zu",
         ctx->calls, 1, q.category, q.since_minutes, q.min_level,
         q.summary, q.around_issue, n);

    *result_buf = out;
    *result_len = n;
    return 0;
}

int ai_debug_tool_describe(const char *input_json, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    DlogQuery q;
    char err[160] = "";
    if (!dlog_query_from_json(input_json, &q, err, sizeof(err))) {
        snprintf(out, cap, "arguments refused: %s", err[0] ? err : "not understood");
        return 0;
    }
    dlog_query_describe(&q, out, cap);
    return 1;
}

void ai_debug_tool_summary_line(const char *result, size_t len, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!result) return;
    size_t n = 0;
    while (n < len && result[n] != '\0' && result[n] != '\n' && result[n] != '\r') n++;
    if (n > cap - 1) n = cap - 1;
    memcpy(out, result, n);
    out[n] = '\0';
}

const char *ai_debug_tool_prompt_note(void)
{
    return "Debugging is on, so you also have the read_debug_log tool, which reads "
           "Nutshell's own debug log. Use it when the user reports a glitch or a hang, "
           "or says they just marked an issue (start with summary=true, then narrow with "
           "around_issue or category), and call it at most a few times per turn.";
}

/* ---- ai_tool_card_text ----------------------------------------------------- */

/* Copy src into out (cap bytes) as a one-line label: controls, bidi, TAB, LF
 * and CR removed, cut on a UTF-8 boundary, NUL-terminated. */
static void card_label(const char *src, char *out, size_t cap)
{
    char tmp[AI_TOOL_NAME_MAX];
    size_t n, w, k;

    if (!out || cap == 0) return;
    n = strnlen(src, sizeof tmp - 1);
    memcpy(tmp, src, n);
    n = paste_filter_controls(tmp, n, tmp, NULL);
    for (k = 0, w = 0; k < n; k++)
        if (tmp[k] != '\t' && tmp[k] != '\n' && tmp[k] != '\r') tmp[w++] = tmp[k];
    tmp[w] = '\0';
    if (w > cap - 1) {
        w = cap - 1;
        while (w > 0 && ((unsigned char)tmp[w] & 0xC0) == 0x80) w--;
        tmp[w] = '\0';
    }
    memcpy(out, tmp, w + 1);
}

int ai_tool_card_text(const AiToolCall *call, char *row, size_t row_cap,
                      char *detail, size_t detail_cap)
{
    static const char prefix[] = "web_fetch: ";
    const size_t plen = sizeof prefix - 1;

    if (row && row_cap > 0) row[0] = '\0';
    if (detail && detail_cap > 0) detail[0] = '\0';
    if (!call) return -1;

    if (strncmp(call->name, "web_fetch", AI_TOOL_NAME_MAX) == 0) {
        JsonNode *input = json_parse(call->input_json);
        const char *url = input ? json_obj_str(input, "url") : NULL;
        int rc = 0;

        if (!url || url[0] == '\0') {
            if (row && row_cap > 0) snprintf(row, row_cap, "%s(no url)", prefix);
            rc = -1;
        } else {
            if (row && row_cap > 0) {
                size_t head = plen < row_cap - 1 ? plen : row_cap - 1;
                memcpy(row, prefix, head);
                row[head] = '\0';
                if (head == plen)
                    net_url_display_marked(url, row + plen, row_cap - plen);
            }
            if (detail && detail_cap > 0) net_url_detail(url, detail, detail_cap);
        }
        if (input) json_free(input);
        return rc;
    }

    if (row && row_cap > 0) card_label(call->name, row, row_cap);
    if (detail && detail_cap > 0)
        net_text_escape(call->input_json, strnlen(call->input_json, AI_TOOL_INPUT_MAX),
                        detail, detail_cap);
    return 0;
}
