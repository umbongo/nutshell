#include "test_framework.h"
#include "xmalloc.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * xmalloc tests
 * ============================================================ */

int test_xmalloc_basic(void)
{
    TEST_BEGIN();
    void *p = xmalloc(128);
    ASSERT_NOT_NULL(p);
    /* should be writable */
    memset(p, 0xAA, 128);
    free(p);
    TEST_END();
}

int test_xmalloc_zero_size(void)
{
    TEST_BEGIN();
    /* xmalloc(0) delegates to malloc(0); result is implementation-defined
     * but must not abort.  Either NULL or a valid pointer is acceptable. */
    void *p = xmalloc(0);
    /* just verify no crash; free is safe on NULL or valid ptr */
    free(p);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_xmalloc_one_byte(void)
{
    TEST_BEGIN();
    char *p = xmalloc(1);
    ASSERT_NOT_NULL(p);
    *p = 'x';
    ASSERT_EQ(*p, 'x');
    free(p);
    TEST_END();
}

int test_xmalloc_large(void)
{
    TEST_BEGIN();
    /* 1 MB allocation */
    size_t sz = 1024 * 1024;
    void *p = xmalloc(sz);
    ASSERT_NOT_NULL(p);
    memset(p, 0, sz);
    free(p);
    TEST_END();
}

int test_xcalloc_basic(void)
{
    TEST_BEGIN();
    int *arr = xcalloc(10, sizeof(int));
    ASSERT_NOT_NULL(arr);
    /* calloc must zero-initialize */
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(arr[i], 0);
    }
    free(arr);
    TEST_END();
}

int test_xcalloc_zero_nmemb(void)
{
    TEST_BEGIN();
    void *p = xcalloc(0, sizeof(int));
    /* must not abort; result may be NULL or valid */
    free(p);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_xcalloc_zero_size(void)
{
    TEST_BEGIN();
    void *p = xcalloc(10, 0);
    /* must not abort */
    free(p);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_xrealloc_grow(void)
{
    TEST_BEGIN();
    char *p = xmalloc(16);
    ASSERT_NOT_NULL(p);
    memcpy(p, "hello", 6);

    p = xrealloc(p, 64);
    ASSERT_NOT_NULL(p);
    /* original data preserved */
    ASSERT_STR_EQ(p, "hello");
    free(p);
    TEST_END();
}

int test_xrealloc_shrink(void)
{
    TEST_BEGIN();
    char *p = xmalloc(64);
    ASSERT_NOT_NULL(p);
    memcpy(p, "abcd", 5);

    p = xrealloc(p, 8);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p, "abcd");
    free(p);
    TEST_END();
}

int test_xrealloc_null_ptr(void)
{
    TEST_BEGIN();
    /* realloc(NULL, size) == malloc(size) */
    void *p = xrealloc(NULL, 32);
    ASSERT_NOT_NULL(p);
    free(p);
    TEST_END();
}

int test_xstrdup_basic(void)
{
    TEST_BEGIN();
    char *s = xstrdup("hello world");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hello world");
    free(s);
    TEST_END();
}

int test_xstrdup_empty(void)
{
    TEST_BEGIN();
    char *s = xstrdup("");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "");
    free(s);
    TEST_END();
}

int test_xstrdup_null(void)
{
    TEST_BEGIN();
    char *s = xstrdup(NULL);
    ASSERT_NULL(s);
    TEST_END();
}

int test_xstrdup_long(void)
{
    TEST_BEGIN();
    /* 1000-char string */
    char buf[1001];
    memset(buf, 'A', 1000);
    buf[1000] = '\0';
    char *s = xstrdup(buf);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(strlen(s), 1000u);
    ASSERT_EQ(s[999], 'A');
    free(s);
    TEST_END();
}
