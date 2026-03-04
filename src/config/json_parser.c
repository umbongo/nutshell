#include "json_parser.h"
#include "json_tokenizer.h"
#include "xmalloc.h"
#include <stdlib.h>
#include <string.h>

/* ---- Internal parser state ------------------------------------------------ */

typedef struct {
    Tokenizer tok;
    Token     cur;
    int       error;  /* 1 once a parse error is encountered */
} Parser;

static void advance(Parser *p)
{
    p->cur = tok_next(&p->tok);
}

/* Forward declaration: parse_array and parse_object call parse_value. */
static JsonNode *parse_value(Parser *p);

/* ---- json_free (defined early so parse_* can use it for cleanup) ---------- */

void json_free(JsonNode *node)
{
    if (!node) {
        return;
    }
    switch (node->type) {
        case JSON_STRING:
            free(node->as.str_val);
            break;
        case JSON_ARRAY: {
            size_t n = vec_size(&node->as.arr);
            for (size_t i = 0u; i < n; i++) {
                json_free((JsonNode *)vec_get(&node->as.arr, i));
            }
            vec_free(&node->as.arr);
            break;
        }
        case JSON_OBJECT: {
            size_t n = vec_size(&node->as.obj.keys);
            for (size_t i = 0u; i < n; i++) {
                free(vec_get(&node->as.obj.keys, i));
                json_free((JsonNode *)vec_get(&node->as.obj.vals, i));
            }
            vec_free(&node->as.obj.keys);
            vec_free(&node->as.obj.vals);
            break;
        }
        default:
            break;
    }
    free(node);
}

/* ---- Array parser --------------------------------------------------------- */

static JsonNode *parse_array(Parser *p)
{
    /* Current token is TOK_LBRACKET — consume it. */
    advance(p);

    JsonNode *node = xcalloc(1u, sizeof(JsonNode));
    node->type = JSON_ARRAY;
    vec_init(&node->as.arr);

    if (p->cur.type == TOK_RBRACKET) {
        advance(p);
        return node;
    }

    for (;;) {
        if (p->error) {
            break;
        }
        JsonNode *val = parse_value(p);
        if (!val) {
            p->error = 1;
            break;
        }
        vec_push(&node->as.arr, val);

        if (p->cur.type == TOK_COMMA) {
            advance(p);
        } else if (p->cur.type == TOK_RBRACKET) {
            advance(p);
            break;
        } else {
            p->error = 1;
            break;
        }
    }

    if (p->error) {
        json_free(node);
        return NULL;
    }
    return node;
}

/* ---- Object parser -------------------------------------------------------- */

static JsonNode *parse_object(Parser *p)
{
    /* Current token is TOK_LBRACE — consume it. */
    advance(p);

    JsonNode *node = xcalloc(1u, sizeof(JsonNode));
    node->type = JSON_OBJECT;
    vec_init(&node->as.obj.keys);
    vec_init(&node->as.obj.vals);

    if (p->cur.type == TOK_RBRACE) {
        advance(p);
        return node;
    }

    for (;;) {
        if (p->error) {
            break;
        }

        /* Expect a string key. */
        if (p->cur.type != TOK_STRING) {
            p->error = 1;
            break;
        }
        char *key = xstrdup(p->cur.value);
        advance(p);

        /* Expect colon. */
        if (p->cur.type != TOK_COLON) {
            free(key);
            p->error = 1;
            break;
        }
        advance(p);

        /* Parse value. */
        JsonNode *val = parse_value(p);
        if (!val) {
            free(key);
            p->error = 1;
            break;
        }

        vec_push(&node->as.obj.keys, key);
        vec_push(&node->as.obj.vals, val);

        if (p->cur.type == TOK_COMMA) {
            advance(p);
        } else if (p->cur.type == TOK_RBRACE) {
            advance(p);
            break;
        } else {
            p->error = 1;
            break;
        }
    }

    if (p->error) {
        json_free(node);
        return NULL;
    }
    return node;
}

/* ---- Value parser --------------------------------------------------------- */

static JsonNode *parse_value(Parser *p)
{
    if (p->error) {
        return NULL;
    }

    JsonNode *node;

    switch (p->cur.type) {
        case TOK_NULL:
            node = xcalloc(1u, sizeof(JsonNode));
            node->type = JSON_NULL;
            advance(p);
            return node;

        case TOK_TRUE:
            node = xcalloc(1u, sizeof(JsonNode));
            node->type = JSON_BOOL;
            node->as.bool_val = 1;
            advance(p);
            return node;

        case TOK_FALSE:
            node = xcalloc(1u, sizeof(JsonNode));
            node->type = JSON_BOOL;
            node->as.bool_val = 0;
            advance(p);
            return node;

        case TOK_STRING:
            node = xcalloc(1u, sizeof(JsonNode));
            node->type = JSON_STRING;
            node->as.str_val = xstrdup(p->cur.value);
            advance(p);
            return node;

        case TOK_NUMBER:
            node = xcalloc(1u, sizeof(JsonNode));
            node->type = JSON_NUMBER;
            node->as.num_val = strtod(p->cur.value, NULL);
            advance(p);
            return node;

        case TOK_LBRACE:
            return parse_object(p);

        case TOK_LBRACKET:
            return parse_array(p);

        default:
            p->error = 1;
            return NULL;
    }
}

/* ---- Public API ----------------------------------------------------------- */

JsonNode *json_parse(const char *src)
{
    Parser p;
    tok_init(&p.tok, src ? src : "");
    p.error = 0;
    advance(&p);   /* load first token */

    JsonNode *root = parse_value(&p);
    if (!root || p.error) {
        json_free(root);
        return NULL;
    }
    return root;
}

JsonNode *json_obj_get(const JsonNode *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT || !key) {
        return NULL;
    }
    /* Cast away const-on-struct to get non-const Vector * for vec_get;
     * the stored pointers are not const, so the return value is safe. */
    const Vector *keys = &obj->as.obj.keys;
    const Vector *vals = &obj->as.obj.vals;
    size_t n = vec_size(keys);
    for (size_t i = 0u; i < n; i++) {
        const char *k = (const char *)vec_get(keys, i);
        if (k && strcmp(k, key) == 0) {
            return (JsonNode *)vec_get(vals, i);
        }
    }
    return NULL;
}

const char *json_obj_str(const JsonNode *obj, const char *key)
{
    JsonNode *n = json_obj_get(obj, key);
    if (!n || n->type != JSON_STRING) {
        return NULL;
    }
    return n->as.str_val;
}

double json_obj_num(const JsonNode *obj, const char *key, double fallback)
{
    JsonNode *n = json_obj_get(obj, key);
    if (!n || n->type != JSON_NUMBER) {
        return fallback;
    }
    return n->as.num_val;
}

int json_obj_bool(const JsonNode *obj, const char *key, int fallback)
{
    JsonNode *n = json_obj_get(obj, key);
    if (!n || n->type != JSON_BOOL) {
        return fallback;
    }
    return n->as.bool_val;
}
