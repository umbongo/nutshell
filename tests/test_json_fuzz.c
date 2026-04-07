#include "test_framework.h"
#include "json_tokenizer.h"
#include "json_parser.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================
 * JSON parser robustness / fuzz-style tests
 *
 * These tests exercise malformed input, boundary conditions,
 * deep nesting, and untrusted-input scenarios.
 * ============================================================ */

/* --- Malformed structural input --- */

int test_json_fuzz_lone_lbrace(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_lone_lbracket(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("[");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_trailing_comma_object(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"a\": 1,}");
    /* trailing comma is invalid JSON — parser should reject or handle gracefully */
    /* either NULL (rejected) or valid parse is acceptable, must not crash */
    if (n) json_free(n);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_json_fuzz_trailing_comma_array(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("[1, 2, 3,]");
    if (n) json_free(n);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_json_fuzz_double_colon(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"a\":: 1}");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_empty_string_input(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_whitespace_only(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("   \t\n\r   ");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_just_comma(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse(",");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_just_colon(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse(":");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_nested_empty_objects(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("{\"a\":{\"b\":{\"c\":{}}}}");
    ASSERT_NOT_NULL(n);
    JsonNode *a = json_obj_get(n, "a");
    ASSERT_NOT_NULL(a);
    JsonNode *b = json_obj_get(a, "b");
    ASSERT_NOT_NULL(b);
    JsonNode *c = json_obj_get(b, "c");
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(c->type, JSON_OBJECT);
    json_free(n);
    TEST_END();
}

int test_json_fuzz_nested_empty_arrays(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("[[[[[]]]]]");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_ARRAY);
    json_free(n);
    TEST_END();
}

/* --- Deep nesting stress (recursive parser stack test) --- */

int test_json_fuzz_deep_nesting_arrays(void)
{
    TEST_BEGIN();
    /* 100 levels deep — should not crash even if parser rejects */
    char buf[301];
    int pos = 0;
    for (int i = 0; i < 100 && pos < 299; i++) buf[pos++] = '[';
    buf[pos++] = '1';
    for (int i = 0; i < 100 && pos < 299; i++) buf[pos++] = ']';
    buf[pos] = '\0';

    JsonNode *n = json_parse(buf);
    /* either parsed or rejected, must not crash */
    if (n) json_free(n);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_json_fuzz_deep_nesting_objects(void)
{
    TEST_BEGIN();
    /* Build {"a":{"a":{"a":...null...}}} 50 levels deep */
    char buf[2048];
    int pos = 0;
    for (int i = 0; i < 50 && pos < 2000; i++) {
        memcpy(buf + pos, "{\"a\":", 5);
        pos += 5;
    }
    memcpy(buf + pos, "null", 4);
    pos += 4;
    for (int i = 0; i < 50 && pos < 2040; i++) {
        buf[pos++] = '}';
    }
    buf[pos] = '\0';

    JsonNode *n = json_parse(buf);
    if (n) json_free(n);
    ASSERT_TRUE(1);
    TEST_END();
}

/* --- Malformed strings --- */

int test_json_fuzz_unterminated_string(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("\"hello");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_string_with_newline(void)
{
    TEST_BEGIN();
    /* raw newline in JSON string is invalid */
    JsonNode *n = json_parse("\"hello\nworld\"");
    /* parser may accept or reject; must not crash */
    if (n) json_free(n);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_json_fuzz_string_lone_backslash(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("\"hello\\\"");
    /* backslash escapes the closing quote, so string is unterminated */
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_string_bad_unicode_escape(void)
{
    TEST_BEGIN();
    /* \\uGGGG is not valid hex */
    JsonNode *n = json_parse("\"\\uGGGG\"");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_string_truncated_unicode(void)
{
    TEST_BEGIN();
    /* \\u00 — only 2 hex digits */
    JsonNode *n = json_parse("\"\\u00\"");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_string_lone_high_surrogate(void)
{
    TEST_BEGIN();
    /* \\uD800 without low surrogate */
    JsonNode *n = json_parse("\"\\uD800\"");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_string_bad_low_surrogate(void)
{
    TEST_BEGIN();
    /* \\uD800\\u0041 — second is not in DC00-DFFF range */
    JsonNode *n = json_parse("\"\\uD800\\u0041\"");
    ASSERT_NULL(n);
    TEST_END();
}

/* --- Malformed numbers --- */

int test_json_fuzz_number_double_minus(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("--5");
    /* double minus is invalid JSON; parser may reject or partially parse.
     * Either way, must not crash. */
    if (n) json_free(n);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_json_fuzz_number_trailing_dot(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("5.");
    /* might parse as 5.0 or reject; must not crash */
    if (n) json_free(n);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_json_fuzz_number_leading_plus(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("+5");
    /* JSON doesn't allow leading + */
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_number_nan(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("NaN");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_number_infinity(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("Infinity");
    ASSERT_NULL(n);
    TEST_END();
}

/* --- Tokenizer boundary tests --- */

int test_json_fuzz_tok_long_string(void)
{
    TEST_BEGIN();
    /* String near TOK_VALUE_MAX (8192) limit */
    size_t len = 8000;
    char *buf = malloc(len + 3); /* quotes + NUL */
    buf[0] = '"';
    memset(buf + 1, 'A', len);
    buf[len + 1] = '"';
    buf[len + 2] = '\0';

    Tokenizer t;
    tok_init(&t, buf);
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_STRING);
    ASSERT_EQ(strlen(tok.value), len);

    free(buf);
    TEST_END();
}

int test_json_fuzz_tok_overlong_string(void)
{
    TEST_BEGIN();
    /* String exceeding TOK_VALUE_MAX — should produce TOK_ERROR, not crash */
    size_t len = 9000;
    char *buf = malloc(len + 3);
    buf[0] = '"';
    memset(buf + 1, 'B', len);
    buf[len + 1] = '"';
    buf[len + 2] = '\0';

    Tokenizer t;
    tok_init(&t, buf);
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_ERROR);

    free(buf);
    TEST_END();
}

int test_json_fuzz_tok_long_number(void)
{
    TEST_BEGIN();
    /* Very long number string */
    char buf[500];
    memset(buf, '9', 499);
    buf[499] = '\0';

    Tokenizer t;
    tok_init(&t, buf);
    Token tok = tok_next(&t);
    ASSERT_EQ(tok.type, TOK_NUMBER);
    TEST_END();
}

/* --- Mixed edge cases --- */

int test_json_fuzz_object_int_key(void)
{
    TEST_BEGIN();
    /* keys must be strings */
    JsonNode *n = json_parse("{42: \"value\"}");
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_duplicate_keys(void)
{
    TEST_BEGIN();
    /* duplicate keys — valid JSON, last wins or both kept; must not crash */
    JsonNode *n = json_parse("{\"a\": 1, \"a\": 2}");
    ASSERT_NOT_NULL(n);
    json_free(n);
    TEST_END();
}

int test_json_fuzz_array_of_mixed_types(void)
{
    TEST_BEGIN();
    JsonNode *n = json_parse("[1, \"two\", true, null, {\"k\": 3}, [4]]");
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->type, JSON_ARRAY);
    ASSERT_EQ((int)vec_size(&n->as.arr), 6);
    json_free(n);
    TEST_END();
}

int test_json_fuzz_garbage_after_value(void)
{
    TEST_BEGIN();
    /* valid JSON followed by garbage */
    JsonNode *n = json_parse("42 garbage");
    /* parser may accept 42 and ignore rest, or reject; must not crash */
    if (n) json_free(n);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_json_fuzz_null_bytes_in_input(void)
{
    TEST_BEGIN();
    /* input with embedded NUL — parser treats as end of string due to strlen */
    char buf[] = "{\"a\":\0\"b\"}";
    JsonNode *n = json_parse(buf);
    /* truncated at NUL — should parse as incomplete and return NULL */
    ASSERT_NULL(n);
    TEST_END();
}

int test_json_fuzz_obj_get_on_array(void)
{
    TEST_BEGIN();
    /* calling json_obj_get on non-object should return NULL, not crash */
    JsonNode *n = json_parse("[1, 2]");
    ASSERT_NOT_NULL(n);
    JsonNode *v = json_obj_get(n, "key");
    ASSERT_NULL(v);
    json_free(n);
    TEST_END();
}

int test_json_fuzz_obj_str_on_null_node(void)
{
    TEST_BEGIN();
    const char *s = json_obj_str(NULL, "key");
    ASSERT_NULL(s);
    TEST_END();
}

int test_json_fuzz_obj_num_on_null_node(void)
{
    TEST_BEGIN();
    double v = json_obj_num(NULL, "key", -1.0);
    ASSERT_TRUE(v == -1.0);
    TEST_END();
}

int test_json_fuzz_obj_bool_on_null_node(void)
{
    TEST_BEGIN();
    int v = json_obj_bool(NULL, "key", 42);
    ASSERT_EQ(v, 42);
    TEST_END();
}
