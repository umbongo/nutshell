#include "test_framework.h"
#include "tooltip.h"
#include <string.h>

/* ---- tooltip_format_duration -------------------------------------------- */

int test_tooltip_format_3661(void)
{
    TEST_BEGIN();
    char buf[64];
    tooltip_format_duration(3661, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1h 1m 1s");
    TEST_END();
}

int test_tooltip_format_59(void)
{
    TEST_BEGIN();
    char buf[64];
    tooltip_format_duration(59, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "59s");
    TEST_END();
}

int test_tooltip_format_zero(void)
{
    TEST_BEGIN();
    char buf[64];
    tooltip_format_duration(0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0s");
    TEST_END();
}

int test_tooltip_format_90(void)
{
    TEST_BEGIN();
    char buf[64];
    tooltip_format_duration(90, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1m 30s");
    TEST_END();
}

/* ---- tooltip_build_text — connected ------------------------------------- */

int test_tooltip_connected(void)
{
    TEST_BEGIN();
    char buf[256];
    tooltip_build_text(TAB_CONNECTED, "dev.example.com", "tom",
                       90, NULL, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "tom") != NULL);
    ASSERT_TRUE(strstr(buf, "dev.example.com") != NULL);
    ASSERT_TRUE(strstr(buf, "1m 30s") != NULL);
    TEST_END();
}

/* ---- tooltip_build_text — disconnected ---------------------------------- */

int test_tooltip_disconnected(void)
{
    TEST_BEGIN();
    char buf[256];
    tooltip_build_text(TAB_DISCONNECTED, "host", "user",
                       0, NULL, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Disconnected") != NULL);
    TEST_END();
}

/* ---- tooltip_build_text — with log path --------------------------------- */

int test_tooltip_with_log(void)
{
    TEST_BEGIN();
    char buf[256];
    tooltip_build_text(TAB_CONNECTED, "host.com", "alice",
                       60, "C:\\logs\\session.log", buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "alice") != NULL);
    ASSERT_TRUE(strstr(buf, "host.com") != NULL);
    ASSERT_TRUE(strstr(buf, "session.log") != NULL);
    TEST_END();
}

/* ---- invalid index / NULL safety ---------------------------------------- */

int test_tooltip_null_buf(void)
{
    TEST_BEGIN();
    /* Must not crash */
    tooltip_build_text(TAB_CONNECTED, "h", "u", 0, NULL, NULL, 0);
    ASSERT_TRUE(1);
    TEST_END();
}

/* ---- long hostname truncated without overflow --------------------------- */

int test_tooltip_long_hostname(void)
{
    TEST_BEGIN();
    char hostname[256];
    memset(hostname, 'a', sizeof(hostname) - 1);
    hostname[255] = '\0';
    char buf[64];
    tooltip_build_text(TAB_CONNECTED, hostname, "u", 5, NULL, buf, sizeof(buf));
    /* Must not overflow buf — content will be truncated but no crash */
    ASSERT_TRUE(buf[sizeof(buf) - 1] == '\0');
    TEST_END();
}
