#include "test_framework.h"
#include "log_format.h"
#include <string.h>

/* =========================================================================
 * log_format_filename — positive tests
 * ========================================================================= */

/* Basic name produces <dir>/<safe_name>-YYYYMMDD_HHMMSS.log */
int test_logfmt_basic_name(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = log_format_filename("myserver", "/tmp", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* Must start with dir + / + name + - */
    ASSERT_TRUE(strncmp(buf, "/tmp/myserver-", 14) == 0);
    /* Must end with .log */
    size_t len = strlen(buf);
    ASSERT_TRUE(len >= 4);
    ASSERT_TRUE(strcmp(buf + len - 4, ".log") == 0);
    TEST_END();
}

/* Name with spaces gets underscored. */
int test_logfmt_spaces_to_underscores(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = log_format_filename("my server", ".", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "my_server-") != NULL);
    TEST_END();
}

/* Empty dir defaults to ".". */
int test_logfmt_empty_dir(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = log_format_filename("srv", "", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strncmp(buf, "./srv-", 6) == 0);
    TEST_END();
}

/* NULL dir defaults to ".". */
int test_logfmt_null_dir(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = log_format_filename("srv", NULL, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strncmp(buf, "./srv-", 6) == 0);
    TEST_END();
}

/* Timestamp format: exactly 15 chars (YYYYMMDD_HHMMSS) before .log */
int test_logfmt_timestamp_format(void)
{
    TEST_BEGIN();
    char buf[256];
    log_format_filename("x", ".", buf, sizeof(buf));
    /* buf = "./x-YYYYMMDD_HHMMSS.log" */
    /* Find the dash after name */
    char *dash = strrchr(buf, '-');
    ASSERT_TRUE(dash != NULL);
    char *dot_log = strstr(dash, ".log");
    ASSERT_TRUE(dot_log != NULL);
    /* Timestamp part should be 15 chars: 8 digits + _ + 6 digits */
    ASSERT_EQ((int)(dot_log - dash - 1), 15);
    /* Check underscore at position 9 in timestamp */
    ASSERT_EQ(dash[9], '_');
    TEST_END();
}

/* Long name gets truncated but still produces valid output. */
int test_logfmt_long_name(void)
{
    TEST_BEGIN();
    char longname[256];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[255] = '\0';
    char buf[64]; /* small buffer */
    int n = log_format_filename(longname, ".", buf, sizeof(buf));
    /* Should either produce truncated output or 0 if too small */
    ASSERT_TRUE(n >= 0);
    if (n > 0) {
        ASSERT_TRUE(buf[n] == '\0');
    }
    TEST_END();
}

/* =========================================================================
 * log_format_filename — negative / edge-case tests
 * ========================================================================= */

/* NULL name falls back to "session". */
int test_logfmt_null_name(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = log_format_filename(NULL, "/tmp", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "session-") != NULL);
    TEST_END();
}

/* Empty name falls back to "session". */
int test_logfmt_empty_name(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = log_format_filename("", "/tmp", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "session-") != NULL);
    TEST_END();
}

/* NULL buf returns 0. */
int test_logfmt_null_buf(void)
{
    TEST_BEGIN();
    int n = log_format_filename("srv", "/tmp", NULL, 256);
    ASSERT_EQ(n, 0);
    TEST_END();
}

/* Zero buf_size returns 0. */
int test_logfmt_zero_bufsize(void)
{
    TEST_BEGIN();
    char buf[16];
    int n = log_format_filename("srv", "/tmp", buf, 0);
    ASSERT_EQ(n, 0);
    TEST_END();
}

/* Special characters in name get replaced. */
int test_logfmt_special_chars(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = log_format_filename("my@server!#$", ".", buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* Only alnum, -, . and _ should remain in the name part */
    char *slash = strrchr(buf, '/');
    ASSERT_TRUE(slash != NULL);
    char *dash = strchr(slash + 1, '-');
    ASSERT_TRUE(dash != NULL);
    /* Check sanitized name between slash+1 and dash */
    for (char *p = slash + 1; p < dash; p++) {
        int ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                 (*p >= '0' && *p <= '9') || *p == '-' || *p == '.' || *p == '_';
        ASSERT_TRUE(ok);
    }
    TEST_END();
}
