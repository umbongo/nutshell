#include "test_framework.h"
#include "json_tokenizer.h"
#include "json_parser.h"
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
