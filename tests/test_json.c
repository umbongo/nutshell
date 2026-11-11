#include "test_framework.h"
#include "json_tokenizer.h"
#include "json_parser.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Tokenizer tests
 * ============================================================ */

int test_tok_empty(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_EOF);
    TEST_END();
}

int test_tok_null_src(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, NULL);
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_EOF);
    TEST_END();
}

int test_tok_structural_chars(void)
{
    TEST_BEGIN();
    const char *src = "{}[]:,";
    Tokenizer t;
    tok_init(&t, src);
    ASSERT_EQ(tok_next(&t).type, TOK_LBRACE);
    ASSERT_EQ(tok_next(&t).type, TOK_RBRACE);
    ASSERT_EQ(tok_next(&t).type, TOK_LBRACKET);
    ASSERT_EQ(tok_next(&t).type, TOK_RBRACKET);
    ASSERT_EQ(tok_next(&t).type, TOK_COLON);
    ASSERT_EQ(tok_next(&t).type, TOK_COMMA);
    ASSERT_EQ(tok_next(&t).type, TOK_EOF);
    TEST_END();
}

int test_tok_string_basic(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "\"hello\"");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_STRING);
    ASSERT_STR_EQ(tok.value, "hello");
    TEST_END();
}

int test_tok_string_escapes(void)
{
    TEST_BEGIN();
    Tokenizer t;
    /* Input: "a\"b\\c\nd" → a"b\c<LF>d */
    tok_init(&t, "\"a\\\"b\\\\c\\nd\"");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_STRING);
    ASSERT_STR_EQ(tok.value, "a\"b\\c\nd");
    TEST_END();
}

int test_tok_string_tab_escape(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "\"a\\tb\"");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_STRING);
    ASSERT_STR_EQ(tok.value, "a\tb");
    TEST_END();
}

int test_tok_number_integer(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "42");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_NUMBER);
    ASSERT_STR_EQ(tok.value, "42");
    TEST_END();
}

int test_tok_number_float(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "3.14");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_NUMBER);
    ASSERT_STR_EQ(tok.value, "3.14");
    TEST_END();
}

int test_tok_number_negative(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "-5");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_NUMBER);
    ASSERT_STR_EQ(tok.value, "-5");
    TEST_END();
}

int test_tok_true(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "true");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_TRUE);
    TEST_END();
}

int test_tok_false(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "false");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_FALSE);
    TEST_END();
}

int test_tok_null(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "null");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_NULL);
    TEST_END();
}

int test_tok_whitespace_skipped(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "  \t\n42");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_NUMBER);
    ASSERT_STR_EQ(tok.value, "42");
    TEST_END();
}

int test_tok_invalid_char(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "!");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_ERROR);
    TEST_END();
}

int test_tok_truncated_keyword(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "tru");  /* incomplete "true" */
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_ERROR);
    TEST_END();
}

/* ============================================================
 * Parser tests
 * ============================================================ */

int test_parse_null(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("null");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_NULL);
    json_free(n);
    TEST_END();
}

int test_parse_true(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("true");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_BOOL);
    ASSERT_TRUE(n->as.bool_val == 1);
    json_free(n);
    TEST_END();
}

int test_parse_false(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("false");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_BOOL);
    ASSERT_TRUE(n->as.bool_val == 0);
    json_free(n);
    TEST_END();
}

int test_parse_number(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("42");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_NUMBER);
    ASSERT_TRUE(n->as.num_val == 42.0);
    json_free(n);
    TEST_END();
}

int test_parse_number_float(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("1.5");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_NUMBER);
    ASSERT_TRUE(n->as.num_val == 1.5);
    json_free(n);
    TEST_END();
}

int test_parse_string(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("\"hello\"");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_STRING);
    ASSERT_NOT_NULL(n->as.str_val);
    ASSERT_STR_EQ(n->as.str_val, "hello");
    json_free(n);
    TEST_END();
}

int test_parse_empty_object(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{}");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_OBJECT);
    ASSERT_EQ((int)vec_size(&n->as.obj.keys), 0);
    json_free(n);
    TEST_END();
}

int test_parse_simple_object(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"key\": \"value\"}");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_OBJECT);
    ASSERT_EQ((int)vec_size(&n->as.obj.keys), 1);

    const char *s = json_obj_str(n, "key");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "value");
    json_free(n);
    TEST_END();
}

int test_parse_object_number(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"port\": 22}");
    ASSERT_NOT_NULL(n);
    double v = json_obj_num(n, "port", -1.0);
    ASSERT_TRUE(v == 22.0);
    json_free(n);
    TEST_END();
}

int test_parse_object_bool(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"enabled\": true, \"disabled\": false}");
    ASSERT_NOT_NULL(n);
    ASSERT_TRUE(json_obj_bool(n, "enabled",  0) == 1);
    ASSERT_TRUE(json_obj_bool(n, "disabled", 1) == 0);
    json_free(n);
    TEST_END();
}

int test_parse_nested_object(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"a\": {\"b\": 99}}");
    ASSERT_NOT_NULL(n);
    JsonNode *inner = json_obj_get(n, "a");
    ASSERT_NOT_NULL(inner);
    ASSERT_EQ(inner->type, JSON_OBJECT);
    double v = json_obj_num(inner, "b", -1.0);
    ASSERT_TRUE(v == 99.0);
    json_free(n);
    TEST_END();
}

int test_parse_empty_array(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("[]");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_ARRAY);
    ASSERT_EQ((int)vec_size(&n->as.arr), 0);
    json_free(n);
    TEST_END();
}

int test_parse_array(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("[1, 2, 3]");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_ARRAY);
    ASSERT_EQ((int)vec_size(&n->as.arr), 3);

    JsonNode *e0 = (JsonNode *)vec_get(&n->as.arr, 0u);
    JsonNode *e1 = (JsonNode *)vec_get(&n->as.arr, 1u);
    JsonNode *e2 = (JsonNode *)vec_get(&n->as.arr, 2u);
    ASSERT_NOT_NULL(e0);
    ASSERT_NOT_NULL(e1);
    ASSERT_NOT_NULL(e2);
    ASSERT_TRUE(e0->as.num_val == 1.0);
    ASSERT_TRUE(e1->as.num_val == 2.0);
    ASSERT_TRUE(e2->as.num_val == 3.0);
    json_free(n);
    TEST_END();
}

int test_parse_array_of_objects(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("[{\"x\": 1}, {\"x\": 2}]");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_ARRAY);
    ASSERT_EQ((int)vec_size(&n->as.arr), 2);

    JsonNode *a = (JsonNode *)vec_get(&n->as.arr, 0u);
    JsonNode *b = (JsonNode *)vec_get(&n->as.arr, 1u);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_TRUE(json_obj_num(a, "x", -1.0) == 1.0);
    ASSERT_TRUE(json_obj_num(b, "x", -1.0) == 2.0);
    json_free(n);
    TEST_END();
}

int test_parse_null_src(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse(NULL);
    ASSERT_NULL(n);
    TEST_END();
}

int test_parse_invalid_json(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{bad}");
    ASSERT_NULL(n);
    TEST_END();
}

int test_parse_missing_colon(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"key\" \"value\"}");
    ASSERT_NULL(n);
    TEST_END();
}

int test_parse_obj_get_missing_key(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"a\": 1}");
    ASSERT_NOT_NULL(n);
    JsonNode *v = json_obj_get(n, "missing");
    ASSERT_NULL(v);
    json_free(n);
    TEST_END();
}

int test_parse_obj_str_type_mismatch(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"n\": 42}");
    ASSERT_NOT_NULL(n);
    /* "n" is a number, not a string — should return NULL */
    const char *s = json_obj_str(n, "n");
    ASSERT_NULL(s);
    json_free(n);
    TEST_END();
}

int test_parse_obj_num_fallback(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"s\": \"hello\"}");
    ASSERT_NOT_NULL(n);
    /* "s" is a string, not a number — should return fallback */
    double v = json_obj_num(n, "s", -99.0);
    ASSERT_TRUE(v == -99.0);
    json_free(n);
    TEST_END();
}

/* json_parse_exact: one value and nothing after it but whitespace. */
int test_json_parse_exact(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse_exact("{\"command\":\"ls\"}");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_OBJECT);
    json_free(n);
    n = json_parse_exact("  {\"a\":1} \n\t\r\n");
    ASSERT_NOT_NULL(n);
    json_free(n);
    n = json_parse_exact("42");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_NUMBER);
    json_free(n);
    /* trailing bytes, a second value, an unterminated tail */
    ASSERT_NULL(json_parse_exact("{\"command\":\"ls\"}x"));
    ASSERT_NULL(json_parse_exact("{\"command\":\"ls\"}garbage"));
    ASSERT_NULL(json_parse_exact("{}{}"));
    ASSERT_NULL(json_parse_exact("{\"a\":1}{\"a\":1"));
    ASSERT_NULL(json_parse_exact("[1] 2"));
    ASSERT_NULL(json_parse_exact("{} ,"));
    /* what json_parse alone rejects stays rejected */
    ASSERT_NULL(json_parse_exact(""));
    ASSERT_NULL(json_parse_exact(NULL));
    ASSERT_NULL(json_parse_exact("{\"a\":"));
    TEST_END();
}

int test_json_free_null(void)
{
    TEST_BEGIN();
    json_free(NULL);  /* must not crash */
    ASSERT_TRUE(1);
    TEST_END();
}

/* ============================================================
 * Unicode escape tests
 * ============================================================ */

int test_tok_unicode_escape_ascii(void)
{
    TEST_BEGIN();
    Tokenizer t;
    /* \u003c = '<', \u003e = '>' */
    tok_init(&t, "\"\\u003cpackage\\u003e\"");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_STRING);
    ASSERT_STR_EQ(tok.value, "<package>");
    TEST_END();
}

int test_tok_unicode_escape_apostrophe(void)
{
    TEST_BEGIN();
    Tokenizer t;
    tok_init(&t, "\"it\\u0027s\"");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_STRING);
    ASSERT_STR_EQ(tok.value, "it's");
    TEST_END();
}

int test_tok_unicode_escape_multibyte(void)
{
    TEST_BEGIN();
    Tokenizer t;
    /* \u00e9 = 'é' (UTF-8: 0xC3 0xA9) */
    tok_init(&t, "\"caf\\u00e9\"");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_STRING);
    ASSERT_STR_EQ(tok.value, "caf\xc3\xa9");
    TEST_END();
}

int test_tok_unicode_escape_3byte(void)
{
    TEST_BEGIN();
    Tokenizer t;
    /* \u2603 = snowman (UTF-8: 0xE2 0x98 0x83) */
    tok_init(&t, "\"\\u2603\"");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_STRING);
    ASSERT_STR_EQ(tok.value, "\xe2\x98\x83");
    TEST_END();
}

int test_tok_unicode_surrogate_pair(void)
{
    TEST_BEGIN();
    Tokenizer t;
    /* \uD83D\uDE00 = U+1F600 grinning face (UTF-8: 0xF0 0x9F 0x98 0x80) */
    tok_init(&t, "\"\\uD83D\\uDE00\"");
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_STRING);
    ASSERT_STR_EQ(tok.value, "\xf0\x9f\x98\x80");
    TEST_END();
}

int test_parse_string_with_unicode(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"msg\": \"use \\u003ctag\\u003e here\"}");
    ASSERT_NOT_NULL(n);
    const char *s = json_obj_str(n, "msg");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "use <tag> here");
    json_free(n);
    TEST_END();
}

/* ============================================================
 * Nesting depth cap (JSON_MAX_DEPTH)
 * ============================================================ */

/* Builds a heap string nested `depth` levels deep, innermost level empty.
 * mode 0: arrays, 1: objects ({"k": ...}), 2: alternating array/object. */
static char *build_nested(int depth, int mode)
{
    size_t cap = (size_t)depth * 8u + 8u;
    char *buf = malloc(cap);
    if (!buf) {
        return NULL;
    }
    size_t pos = 0;
    for (int i = 0; i < depth; i++) {
        int is_arr = (mode == 0) || (mode == 2 && (i % 2) == 0);
        if (i == depth - 1) {
            memcpy(buf + pos, is_arr ? "[]" : "{}", 2);
            pos += 2;
        } else if (is_arr) {
            buf[pos++] = '[';
        } else {
            memcpy(buf + pos, "{\"k\":", 5);
            pos += 5;
        }
    }
    for (int i = depth - 2; i >= 0; i--) {
        int is_arr = (mode == 0) || (mode == 2 && (i % 2) == 0);
        buf[pos++] = is_arr ? ']' : '}';
    }
    buf[pos] = '\0';
    return buf;
}

int test_json_depth_256_arrays_parse(void)
{
    TEST_BEGIN();
    char *s = build_nested(256, 0);
    ASSERT_NOT_NULL(s);
    JsonNode *n = json_parse(s);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_ARRAY);
    json_free(n);
    free(s);
    TEST_END();
}

int test_json_depth_256_objects_parse(void)
{
    TEST_BEGIN();
    char *s = build_nested(256, 1);
    ASSERT_NOT_NULL(s);
    JsonNode *n = json_parse(s);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_OBJECT);
    json_free(n);
    free(s);
    TEST_END();
}

int test_json_depth_256_mixed_parse(void)
{
    TEST_BEGIN();
    char *s = build_nested(256, 2);
    ASSERT_NOT_NULL(s);
    JsonNode *n = json_parse(s);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_ARRAY);
    json_free(n);
    free(s);
    TEST_END();
}

int test_json_depth_257_fails(void)
{
    TEST_BEGIN();
    char *arrays = build_nested(257, 0);
    char *objects = build_nested(257, 1);
    char *mixed = build_nested(257, 2);
    ASSERT_NOT_NULL(arrays);
    ASSERT_NOT_NULL(objects);
    ASSERT_NOT_NULL(mixed);
    ASSERT_NULL(json_parse(arrays));
    ASSERT_NULL(json_parse(objects));
    ASSERT_NULL(json_parse(mixed));
    free(arrays);
    free(objects);
    free(mixed);
    TEST_END();
}

int test_json_depth_100000_fails(void)
{
    TEST_BEGIN();
    size_t n = 100000u;
    char *s = malloc(n + 1u);
    ASSERT_NOT_NULL(s);
    memset(s, '[', n);
    s[n] = '\0';
    /* No closers: the cap must stop the recursion at depth 257. */
    ASSERT_NULL(json_parse(s));
    free(s);
    TEST_END();
}

int test_json_depth_resets_between_siblings(void)
{
    TEST_BEGIN();
    char *deep = build_nested(200, 0);
    ASSERT_NOT_NULL(deep);
    size_t len = strlen(deep);
    char *s = malloc(2u * len + 4u);
    ASSERT_NOT_NULL(s);
    size_t pos = 0;
    s[pos++] = '[';
    memcpy(s + pos, deep, len);
    pos += len;
    s[pos++] = ',';
    memcpy(s + pos, deep, len);
    pos += len;
    s[pos++] = ']';
    s[pos] = '\0';
    /* Each sibling is 200 deep under a 1-deep root; summed depth would exceed
     * the cap if a returning parse_array() did not decrement. */
    JsonNode *n = json_parse(s);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_ARRAY);
    ASSERT_EQ(n->as.arr.size, 2u);
    json_free(n);
    free(s);
    free(deep);
    TEST_END();
}

/* A \u0000 escape decodes to a NUL byte, which ends the C string. The string
 * node keeps its real decoded length so a caller can tell (review of B1-B11,
 * 2026-11-11). A backslash followed by the text u0000 is ordinary text. */
int test_json_string_len_and_nul(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("\"abc\"");
    ASSERT_NOT_NULL(n);
    if (n) {
        ASSERT_EQ(n->str_len, 3u);
        json_free(n);
    }

    n = json_parse("{\"k\":\"a\\u0000b\"}");
    ASSERT_NOT_NULL(n);
    if (n) {
        JsonNode *v = json_obj_get(n, "k");
        ASSERT_NOT_NULL(v);
        if (v) {
            ASSERT_STR_EQ(v->as.str_val, "a");      /* the C string ends at the NUL */
            ASSERT_EQ(v->str_len, 3u);              /* the decoded string does not */
        }
        json_free(n);
    }

    /* an escaped backslash then the text u0000: eight bytes, no NUL */
    n = json_parse("{\"k\":\"a\\\\u0000b\"}");
    ASSERT_NOT_NULL(n);
    if (n) {
        JsonNode *v = json_obj_get(n, "k");
        ASSERT_NOT_NULL(v);
        if (v) {
            ASSERT_STR_EQ(v->as.str_val, "a\\u0000b");
            ASSERT_EQ(v->str_len, 8u);
        }
        json_free(n);
    }

    n = json_parse("{\"k\":1}");
    ASSERT_NOT_NULL(n);
    json_free(n);
    TEST_END();
}

/* A key that decodes to "k" plus a NUL plus "x" does not fail the parse (the
 * config loader and every provider response go through json_parse): the
 * member is kept, json_obj_get never returns it for "k", and the other members
 * read normally (ruling R52, review of the fix wave, 2026-11-11). */
int test_json_nul_key_kept_never_matched(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"a\":1,\"k\\u0000x\":2,\"k\":3,\"b\":\"two\"}");
    ASSERT_NOT_NULL(n);
    if (n) {
        ASSERT_EQ((int)vec_size(&n->as.obj.keys), 4);        /* the member is kept */
        ASSERT_EQ((int)json_obj_num(n, "a", -1.0), 1);
        ASSERT_EQ((int)json_obj_num(n, "k", -1.0), 3);       /* the real "k", not the NUL key */
        ASSERT_STR_EQ(json_obj_str(n, "b"), "two");
        ASSERT_NULL(json_obj_get(n, "kx"));
        ASSERT_NULL(json_obj_get(n, "k\\u0000x"));
        json_free(n);
    }

    /* the NUL key first, the real one after it: the real one still answers */
    n = json_parse("{\"k\\u0000x\":\"evil\",\"k\":\"good\"}");
    ASSERT_NOT_NULL(n);
    if (n) ASSERT_STR_EQ(json_obj_str(n, "k"), "good");
    json_free(n);

    /* a NUL key alone: parses, and "k" is simply absent */
    n = json_parse_exact("{\"k\\u0000x\":1}");
    ASSERT_NOT_NULL(n);
    if (n) {
        ASSERT_EQ((int)vec_size(&n->as.obj.keys), 1);
        ASSERT_NULL(json_obj_get(n, "k"));
        ASSERT_EQ((int)json_obj_num(n, "k", -7.0), -7);
    }
    json_free(n);

    /* a key that is only a NUL never matches the empty name */
    n = json_parse("{\"\\u0000\":1,\"\":2}");
    ASSERT_NOT_NULL(n);
    if (n) ASSERT_EQ((int)json_obj_num(n, "", -1.0), 2);
    json_free(n);
    TEST_END();
}
