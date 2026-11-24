#include "test_framework.h"
#include "json_scan.h"
#include <stdlib.h>
#include <string.h>

/* Scan `key` at depth 1 of a literal; the result must equal want (NULL: must fail). */
static int scan_is(const char *json, const char *key, const char *want)
{
    char *o = (char *)1;
    size_t n = 99;
    int rc = json_scan_string(json, strlen(json), 1, key, 1000000, &o, &n);
    int ok;
    if (!want) ok = rc == 0 && o == NULL && n == 0;
    else ok = rc == 1 && o && strlen(want) == n && memcmp(o, want, n + 1) == 0;
    if (!ok) printf("  scan of %s: rc %d, got '%s'\n", json, rc, o ? o : "(null)");
    if (rc == 1) free(o);
    return ok;
}

int test_json_scan_basic(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(scan_is("{\"a\":\"x\",\"text\":\"hello\"}", "text", "hello"));
    ASSERT_TRUE(scan_is("{ \"a\" : \"x\" , \"text\" :\n \"hello\" }", "text", "hello"));
    ASSERT_TRUE(scan_is("{\"a\":\"x\",\"text\":\"hello\"}", "missing", NULL));
    ASSERT_TRUE(scan_is("{\"note\":\"\\\"text\\\":\\\"no\\\"\",\"text\":\"yes\"}", "text", "yes"));
    ASSERT_TRUE(scan_is("{\"inner\":{\"text\":\"no\"},\"text\":\"yes\"}", "text", "yes"));
    ASSERT_TRUE(scan_is("{\"arr\":[{\"text\":\"no\"},\"text\"],\"text\":\"yes\"}", "text", "yes"));
    ASSERT_TRUE(scan_is("{\"text\":5}", "text", NULL));
    ASSERT_TRUE(scan_is("{\"text\":null}", "text", NULL));
    ASSERT_TRUE(scan_is("{\"inner\":{\"text\":\"no\"}}", "text", NULL));
    ASSERT_TRUE(scan_is("[\"text\"]", "text", NULL));
    ASSERT_TRUE(scan_is("", "text", NULL));
    ASSERT_TRUE(scan_is("{\"text\":\"\"}", "text", ""));
    {   /* depth other than 1 is unsupported */
        char *o = NULL; size_t n = 0;
        const char *j = "{\"text\":\"x\"}";
        ASSERT_EQ(json_scan_string(j, strlen(j), 2, "text", 100, &o, &n), 0);
        ASSERT_NULL(o);
    }
    TEST_END();
}

int test_json_scan_escapes(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(scan_is("{\"text\":\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"}", "text", "\"\\/\b\f\n\r\t"));
    ASSERT_TRUE(scan_is("{\"text\":\"caf\\u00e9\"}", "text", "caf\xC3\xA9"));
    ASSERT_TRUE(scan_is("{\"text\":\"\\ud83d\\ude00\"}", "text", "\xF0\x9F\x98\x80"));
    ASSERT_TRUE(scan_is("{\"text\":\"a\\ud83db\"}", "text", "a\xEF\xBF\xBD" "b"));
    ASSERT_TRUE(scan_is("{\"text\":\"\\ude00\"}", "text", "\xEF\xBF\xBD"));
    ASSERT_TRUE(scan_is("{\"text\":\"a\\u0000b\"}", "text", "a b"));
    ASSERT_TRUE(scan_is("{\"text\":\"a\\qb\"}", "text", NULL));
    ASSERT_TRUE(scan_is("{\"text\":\"abc", "text", NULL));
    ASSERT_TRUE(scan_is("{\"text\":\"abc\\", "text", NULL));
    ASSERT_TRUE(scan_is("{\"text\":\"ab\\u12", "text", NULL));
    ASSERT_TRUE(scan_is("{\"text\":\"ab\\u12\"}", "text", NULL));
    ASSERT_TRUE(scan_is("{\"text\":\"ab\\u12zz\"}", "text", NULL));
    TEST_END();
}

int test_json_scan_large(void)
{
    TEST_BEGIN();
    enum { N = 70000 };
    char *j = (char *)malloc((size_t)N + 64);
    ASSERT_NOT_NULL(j);
    if (j) {
        size_t off = (size_t)sprintf(j, "{\"x\":\"1\",\"text\":\"");
        memset(j + off, 'a', N);
        off += N;
        memcpy(j + off, "\"}", 3);
        off += 2;
        char *o = NULL; size_t n = 0;
        ASSERT_EQ(json_scan_string(j, off, 1, "text", 1000000, &o, &n), 1);
        ASSERT_EQ((int)n, N);
        if (o) { ASSERT_EQ((int)strlen(o), N); free(o); }
        o = (char *)1; n = 5;
        ASSERT_EQ(json_scan_string(j, off, 1, "text", 1000, &o, &n), 0);
        ASSERT_NULL(o);
        ASSERT_EQ((int)n, 0);
        ASSERT_EQ(json_scan_string(j, off, 1, "text", N, &o, &n), 1);   /* exactly the limit */
        free(o);
        ASSERT_EQ(json_scan_string(j, off, 1, "text", N - 1, &o, &n), 0);
        free(j);
    }
    TEST_END();
}

int test_json_scan_path(void)
{
    TEST_BEGIN();
    const char *j = "{\"choices\":[{\"logprobs\":{\"content\":\"no\"},\"message\":{\"content\":\"yes\"}}]}";
    const char *const p[] = {"choices", "[0]", "message", "content", NULL};
    char *o = NULL; size_t n = 0;
    ASSERT_EQ(json_scan_path_string(j, strlen(j), p, 100, &o, &n), 1);
    if (o) { ASSERT_STR_EQ(o, "yes"); ASSERT_EQ((int)n, 3); free(o); }
    /* the first array element only */
    const char *j2 = "{\"choices\":[{\"message\":{\"role\":\"a\"}},{\"message\":{\"content\":\"second\"}}]}";
    ASSERT_EQ(json_scan_path_string(j2, strlen(j2), p, 100, &o, &n), 0);
    ASSERT_NULL(o);
    /* a path through the wrong container type */
    const char *j3 = "{\"choices\":{\"message\":{\"content\":\"x\"}}}";
    ASSERT_EQ(json_scan_path_string(j3, strlen(j3), p, 100, &o, &n), 0);
    const char *j4 = "{\"choices\":[]}";
    ASSERT_EQ(json_scan_path_string(j4, strlen(j4), p, 100, &o, &n), 0);
    /* an empty path is the root value itself: a bare string */
    const char *const none[] = {NULL};
    ASSERT_EQ(json_scan_path_string("\"hi\"", 4, none, 100, &o, &n), 1);
    if (o) { ASSERT_STR_EQ(o, "hi"); free(o); }
    TEST_END();
}
