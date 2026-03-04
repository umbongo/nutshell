#ifndef JSON_PARSER_H
#define JSON_PARSER_H

/*
 * Recursive-descent JSON parser.
 *
 * Usage:
 *   JsonNode *root = json_parse(src);   // returns NULL on error
 *   JsonNode *val  = json_obj_get(root, "key");
 *   const char *s  = json_obj_str(root, "key");   // NULL if missing/wrong type
 *   json_free(root);
 *
 * All heap memory is owned by the JsonNode tree; json_free() releases it all.
 */

#include "vector.h"

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

/* Forward declaration so the struct can reference itself. */
typedef struct JsonNode JsonNode;

typedef struct {
    Vector keys;   /* Vector of char *   (owned, null-terminated) */
    Vector vals;   /* Vector of JsonNode * (owned)                */
} JsonObject;

struct JsonNode {
    JsonType type;
    union {
        int        bool_val;   /* JSON_BOOL   */
        double     num_val;    /* JSON_NUMBER */
        char      *str_val;    /* JSON_STRING — heap-allocated   */
        Vector     arr;        /* JSON_ARRAY  — Vector of JsonNode * (owned) */
        JsonObject obj;        /* JSON_OBJECT */
    } as;
};

/* Parse a null-terminated JSON string.  Returns NULL on error. */
JsonNode  *json_parse(const char *src);

/* Recursively free a JsonNode tree.  Safe to call with NULL. */
void       json_free(JsonNode *node);

/* Object lookup — returns NULL if key not found or node is not JSON_OBJECT. */
JsonNode  *json_obj_get(const JsonNode *obj, const char *key);

/* Convenience getters that return a default if the key is absent or has the
 * wrong type.  json_obj_str() returns NULL (not a default string) on miss. */
const char *json_obj_str(const JsonNode *obj, const char *key);
double      json_obj_num(const JsonNode *obj, const char *key, double fallback);
int         json_obj_bool(const JsonNode *obj, const char *key, int fallback);

#endif /* JSON_PARSER_H */
