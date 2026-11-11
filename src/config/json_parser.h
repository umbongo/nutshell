#ifndef NUTSHELL_CONFIG_JSON_PARSER_H
#define NUTSHELL_CONFIG_JSON_PARSER_H

#include "../core/vector.h"

/* Deepest array/object nesting json_parse() accepts; deeper input fails. */
#define JSON_MAX_DEPTH 256

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonNode {
    JsonType type;
    /* JSON_STRING: the decoded length in bytes. It exceeds strlen(str_val)
     * when the string held a \u0000 escape, which ends the C string early. */
    size_t str_len;
    union {
        int bool_val;
        double num_val;
        char *str_val;
        Vector arr;
        struct {
            Vector keys;
            Vector vals;
        } obj;
    } as;
} JsonNode;

JsonNode *json_parse(const char *src);
/* Like json_parse, but the value must be the whole input: only JSON
 * whitespace may follow it. Trailing bytes, a second value ("{}{}") or an
 * unterminated tail give NULL. */
JsonNode *json_parse_exact(const char *src);
void json_free(JsonNode *node);
JsonNode *json_obj_get(const JsonNode *obj, const char *key);
const char *json_obj_str(const JsonNode *obj, const char *key);
double json_obj_num(const JsonNode *obj, const char *key, double fallback);
int json_obj_bool(const JsonNode *obj, const char *key, int fallback);

#endif