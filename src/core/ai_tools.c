#include "ai_tools.h"
#include "json_parser.h"
#include "json_validate.h"
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
        char msg[AI_TOOL_NAME_MAX + 32];
        snprintf(msg, sizeof(msg), "Unknown tool: %s", call->name);
        size_t msg_len = strlen(msg);
        result->content = (char *)malloc(msg_len + 1);
        if (!result->content) return -1;
        memcpy(result->content, msg, msg_len + 1);
        result->content_len = msg_len;
        result->is_error = 1;
        result->was_truncated = 0;
        return -1;
    }

    int rc = tool->execute(call->input_json, tool->tool_data, cancel_flag,
                           &result->content, &result->content_len,
                           &result->was_truncated);
    result->is_error = (rc != 0) ? 1 : 0;
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

    /* Check finish_reason */
    const char *finish_reason = json_obj_str(choice, "finish_reason");
    if (finish_reason) {
        if (strcmp(finish_reason, "tool_calls") == 0) {
            json_free(root);
            return 1;
        }
        if (strcmp(finish_reason, "stop") == 0) {
            json_free(root);
            return 2;
        }
    }

    /* Process delta.tool_calls if present */
    JsonNode *delta = json_obj_get(choice, "delta");
    if (!delta || delta->type != JSON_OBJECT) {
        json_free(root);
        return 0;
    }

    JsonNode *tool_calls = json_obj_get(delta, "tool_calls");
    if (!tool_calls || tool_calls->type != JSON_ARRAY) {
        json_free(root);
        return 0;
    }

    size_t n = vec_size(&tool_calls->as.arr);
    for (size_t i = 0; i < n; i++) {
        JsonNode *tc_node = (JsonNode *)vec_get(&tool_calls->as.arr, i);
        if (!tc_node || tc_node->type != JSON_OBJECT) continue;

        /* Get slot index */
        double idx_d = json_obj_num(tc_node, "index", -1.0);
        if (idx_d < 0.0) continue;
        int slot = (int)idx_d;

        /* Grow pending_tool_calls array if needed to accommodate this slot */
        while (slot >= state->pending_tool_cap) {
            int new_cap = (state->pending_tool_cap == 0) ? 4 : state->pending_tool_cap * 2;
            AiToolCall *grown = (AiToolCall *)realloc(state->pending_tool_calls,
                                                       (size_t)new_cap * sizeof(AiToolCall));
            if (!grown) {
                json_free(root);
                return -1;
            }
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
                if (stream_append_input(&state->pending_tool_calls[slot], arguments) < 0) {
                    json_free(root);
                    return -1;
                }
            }
        }
    }

    json_free(root);
    return 0;
}
