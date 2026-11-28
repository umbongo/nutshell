#include "test_framework.h"
#include "log_open_error.h"

#include <errno.h>
#include <string.h>

int test_log_open_error_names_path_and_reason(void)
{
    TEST_BEGIN();
    char out[768];
    size_t n = log_open_error_text("C:\\logs\\a.log", ENOENT, out, sizeof(out));
    ASSERT_EQ(n, strlen(out));
    ASSERT_TRUE(strstr(out, "C:\\logs\\a.log") != NULL);
    ASSERT_TRUE(strstr(out, strerror(ENOENT)) != NULL);
    ASSERT_TRUE(strstr(out, "Logging was not started") != NULL);
    TEST_END();
}

int test_log_open_error_no_errno_and_no_path(void)
{
    TEST_BEGIN();
    char out[768];
    ASSERT_TRUE(log_open_error_text(NULL, 0, out, sizeof(out)) > 0);
    ASSERT_TRUE(strstr(out, "(no path)") != NULL);
    ASSERT_TRUE(strstr(out, "unknown error") != NULL);
    ASSERT_TRUE(log_open_error_text("", 0, out, sizeof(out)) > 0);
    ASSERT_TRUE(strstr(out, "(no path)") != NULL);
    TEST_END();
}

/* A tiny buffer truncates but stays NUL-terminated; NULL/zero cap are safe. */
int test_log_open_error_truncates_safely(void)
{
    TEST_BEGIN();
    char out[16];
    memset(out, 'x', sizeof(out));
    size_t n = log_open_error_text("C:\\a.log", EACCES, out, sizeof(out));
    ASSERT_EQ(n, strlen(out));
    ASSERT_TRUE(n < sizeof(out));
    ASSERT_EQ(log_open_error_text("p", EACCES, NULL, 10), (size_t)0);
    ASSERT_EQ(log_open_error_text("p", EACCES, out, 0), (size_t)0);
    TEST_END();
}

/* A long path is kept whole when the buffer is the notice's opening size. */
int test_log_open_error_long_path_fits_notice(void)
{
    TEST_BEGIN();
    char path[261];
    memset(path, 'p', 260);
    path[260] = 0;
    char out[768];
    log_open_error_text(path, EACCES, out, sizeof(out));
    ASSERT_TRUE(strstr(out, path) != NULL);
    ASSERT_TRUE(strstr(out, strerror(EACCES)) != NULL);
    TEST_END();
}
