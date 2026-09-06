#include "test_framework.h"
#include "log_format.h"
#include <string.h>

/* Fixed, deterministic broken-down time used by every filename test:
 * 2026-09-07 14:30:05. */
static void make_test_tm(struct tm *t)
{
    memset(t, 0, sizeof(*t));
    t->tm_year = 126; /* 2026 - 1900 */
    t->tm_mon  = 8;   /* September (0-indexed) */
    t->tm_mday = 7;
    t->tm_hour = 14;
    t->tm_min  = 30;
    t->tm_sec  = 5;
}

#define DEFAULT_TS "2026-09-07_14-30-05"

/* =========================================================================
 * log_format_filename — positive tests
 * ========================================================================= */

/* Basic name, default format -> <dir>\<timestamp>_<safe_name>.log */
int test_logfmt_basic_name(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("myserver", "/tmp", NULL, &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, "/tmp\\" DEFAULT_TS "_myserver.log");
    TEST_END();
}

/* Name with spaces gets underscored. */
int test_logfmt_spaces_to_underscores(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("my server", ".", NULL, &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\" DEFAULT_TS "_my_server.log");
    TEST_END();
}

/* Empty dir defaults to ".". */
int test_logfmt_empty_dir(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("srv", "", NULL, &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\" DEFAULT_TS "_srv.log");
    TEST_END();
}

/* NULL dir defaults to ".". */
int test_logfmt_null_dir(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("srv", NULL, NULL, &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\" DEFAULT_TS "_srv.log");
    TEST_END();
}

/* Custom (valid) format is honoured. */
int test_logfmt_custom_format(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("srv", ".", "%Y%m%d_%H%M%S", &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\20260907_143005_srv.log");
    TEST_END();
}

/* Long name gets truncated but still produces valid output. */
int test_logfmt_long_name(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char longname[256];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[255] = '\0';
    char buf[64]; /* small buffer */
    int n = log_format_filename(longname, ".", NULL, &t, buf, sizeof(buf));
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
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename(NULL, "/tmp", NULL, &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, "/tmp\\" DEFAULT_TS "_session.log");
    TEST_END();
}

/* Empty name falls back to "session". */
int test_logfmt_empty_name(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("", "/tmp", NULL, &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, "/tmp\\" DEFAULT_TS "_session.log");
    TEST_END();
}

/* NULL buf returns 0. */
int test_logfmt_null_buf(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    int n = log_format_filename("srv", "/tmp", NULL, &t, NULL, 256);
    ASSERT_EQ(n, 0);
    TEST_END();
}

/* Zero buf_size returns 0. */
int test_logfmt_zero_bufsize(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[16];
    int n = log_format_filename("srv", "/tmp", NULL, &t, buf, 0);
    ASSERT_EQ(n, 0);
    TEST_END();
}

/* Special characters in name get replaced. */
int test_logfmt_special_chars(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("my@server!#$", ".", NULL, &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\" DEFAULT_TS "_myserver.log");
    TEST_END();
}

/* NULL fmt uses the documented default. */
int test_logfmt_null_fmt_uses_default(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("srv", ".", NULL, &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\" DEFAULT_TS "_srv.log");
    TEST_END();
}

/* Empty fmt uses the documented default. */
int test_logfmt_empty_fmt_uses_default(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("srv", ".", "", &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\" DEFAULT_TS "_srv.log");
    TEST_END();
}

/* Invalid fmt (unsupported conversion) falls back to the default. */
int test_logfmt_invalid_fmt_uses_default(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("srv", ".", "%F", &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\" DEFAULT_TS "_srv.log");
    TEST_END();
}

/* Invalid fmt (trailing bare '%') falls back to the default. */
int test_logfmt_trailing_percent_fmt_uses_default(void)
{
    TEST_BEGIN();
    struct tm t;
    make_test_tm(&t);
    char buf[256];
    int n = log_format_filename("srv", ".", "session_%Y%m%d_%", &t, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\" DEFAULT_TS "_srv.log");
    TEST_END();
}

/* NULL t produces an empty timestamp portion without crashing. */
int test_logfmt_null_t(void)
{
    TEST_BEGIN();
    char buf[256];
    int n = log_format_filename("srv", ".", NULL, NULL, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, ".\\_srv.log");
    TEST_END();
}

/* =========================================================================
 * log_format_validate
 * ========================================================================= */

int test_logfmt_validate_default_valid(void)
{
    TEST_BEGIN();
    ASSERT_EQ(log_format_validate("%Y-%m-%d_%H-%M-%S"), 1);
    TEST_END();
}

int test_logfmt_validate_null_invalid(void)
{
    TEST_BEGIN();
    ASSERT_EQ(log_format_validate(NULL), 0);
    TEST_END();
}

int test_logfmt_validate_empty_invalid(void)
{
    TEST_BEGIN();
    ASSERT_EQ(log_format_validate(""), 0);
    TEST_END();
}

int test_logfmt_validate_percent_f_rejected(void)
{
    TEST_BEGIN();
    ASSERT_EQ(log_format_validate("%F"), 0);
    TEST_END();
}

int test_logfmt_validate_trailing_percent_rejected(void)
{
    TEST_BEGIN();
    ASSERT_EQ(log_format_validate("session_%Y%m%d_%"), 0);
    TEST_END();
}

int test_logfmt_validate_escaped_percent_ok(void)
{
    TEST_BEGIN();
    ASSERT_EQ(log_format_validate("100%%_%Y"), 1);
    TEST_END();
}

int test_logfmt_validate_plain_text_ok(void)
{
    TEST_BEGIN();
    ASSERT_EQ(log_format_validate("session"), 1);
    TEST_END();
}

int test_logfmt_validate_all_allowed_specs_ok(void)
{
    TEST_BEGIN();
    ASSERT_EQ(log_format_validate(
        "%Y%y%m%d%H%M%S%j%A%a%B%b%p%Z%z%%"), 1);
    TEST_END();
}
