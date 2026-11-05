#include "test_framework.h"
#include "build_info.h"
#include <openssl/opensslv.h>
#include <string.h>

/* Match [0-9]+(\.[0-9A-Za-z-]+)* at s; return the char after it, or NULL. */
static const char *ver(const char *s)
{
    if (!s || *s < '0' || *s > '9') return NULL;
    while (*s >= '0' && *s <= '9') s++;
    while (*s == '.') {
        s++;
        const char *start = s;
        while ((*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'Z') ||
               (*s >= 'a' && *s <= 'z') || *s == '-')
            s++;
        if (s == start) return NULL;
    }
    return s;
}

static const char *lit(const char *s, const char *prefix)
{
    if (!s) return NULL;
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0 ? s + n : NULL;
}

int test_build_info_deps_whole_format(void)
{
    TEST_BEGIN();
    char buf[BUILD_INFO_DEPS_MAX];
    size_t n = build_info_deps(buf, sizeof buf);
    ASSERT_EQ((int)n, (int)strlen(buf));

    const char *p = lit(buf, "OpenSSL ");
    p = ver(p);
    p = lit(p, ", libssh2 ");
    p = ver(p);
    p = lit(p, ", zlib ");
    ASSERT_NOT_NULL(p);
    if (p && *p == '?') p++;
    else p = ver(p);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ((int)*p, 0);

    for (const char *c = buf; *c; c++)
        ASSERT_TRUE((unsigned char)*c >= 0x20 && (unsigned char)*c <= 0x7E);
    TEST_END();
}

int test_build_info_deps_openssl_part_matches_header(void)
{
    TEST_BEGIN();
    char buf[BUILD_INFO_DEPS_MAX];
    (void)build_info_deps(buf, sizeof buf);
    const char *start = lit(buf, "OpenSSL ");
    ASSERT_NOT_NULL(start);
    const char *comma = start ? strchr(start, ',') : NULL;
    ASSERT_NOT_NULL(comma);
    if (start && comma) {
        size_t len = (size_t)(comma - start);
        ASSERT_EQ((int)len, (int)strlen(OPENSSL_VERSION_STR));
        ASSERT_EQ(strncmp(start, OPENSSL_VERSION_STR, len), 0);
    }
    TEST_END();
}

int test_build_info_deps_truncates(void)
{
    TEST_BEGIN();
    char full[BUILD_INFO_DEPS_MAX];
    size_t n = build_info_deps(full, sizeof full);
    ASSERT_TRUE(n > 8);
    ASSERT_TRUE(n < BUILD_INFO_DEPS_MAX);

    char small[8];
    memset(small, 'x', sizeof small);
    ASSERT_EQ((int)build_info_deps(small, sizeof small), (int)n);
    ASSERT_EQ((int)strlen(small), 7);
    ASSERT_EQ(strncmp(small, full, 7), 0);

    char one[1] = { 'x' };
    ASSERT_EQ((int)build_info_deps(one, 1), (int)n);
    ASSERT_EQ((int)one[0], 0);

    ASSERT_EQ((int)build_info_deps(NULL, 0), (int)n);
    TEST_END();
}
