#include "test_framework.h"
#include "cli_args.h"
#include <string.h>
#include <stdio.h>

/* ---- no args ---- */

int test_cli_no_args(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell" };
    CliOptions o;
    cli_parse(1, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_RUN);
    ASSERT_STR_EQ(o.arg, "");
    TEST_END();
}

/* ---- connect by name ---- */

int test_cli_session_name_short(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-sn", "prod" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_NAME);
    ASSERT_STR_EQ(o.arg, "prod");
    TEST_END();
}

int test_cli_session_name_long(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "--session-name", "my server" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_NAME);
    ASSERT_STR_EQ(o.arg, "my server");
    TEST_END();
}

/* ---- connect by host ---- */

int test_cli_host_short(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-h", "box.example.com" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_HOST);
    ASSERT_STR_EQ(o.arg, "box.example.com");
    TEST_END();
}

int test_cli_host_long(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "--host", "10.0.0.5" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_HOST);
    ASSERT_STR_EQ(o.arg, "10.0.0.5");
    TEST_END();
}

/* ---- simple actions ---- */

int test_cli_no_connect(void)
{
    TEST_BEGIN();
    char *a1[] = { "nutshell", "-nc" };
    char *a2[] = { "nutshell", "--no-connect" };
    CliOptions o;
    cli_parse(2, a1, &o);
    ASSERT_EQ((int)o.action, (int)CLI_RUN_NO_CONNECT);
    cli_parse(2, a2, &o);
    ASSERT_EQ((int)o.action, (int)CLI_RUN_NO_CONNECT);
    TEST_END();
}

int test_cli_list_version_help(void)
{
    TEST_BEGIN();
    CliOptions o;
    char *l1[] = { "nutshell", "-l" };
    char *l2[] = { "nutshell", "--list" };
    char *v1[] = { "nutshell", "-v" };
    char *v2[] = { "nutshell", "--version" };
    char *h1[] = { "nutshell", "-?" };
    char *h2[] = { "nutshell", "--help" };
    cli_parse(2, l1, &o); ASSERT_EQ((int)o.action, (int)CLI_LIST);
    cli_parse(2, l2, &o); ASSERT_EQ((int)o.action, (int)CLI_LIST);
    cli_parse(2, v1, &o); ASSERT_EQ((int)o.action, (int)CLI_VERSION);
    cli_parse(2, v2, &o); ASSERT_EQ((int)o.action, (int)CLI_VERSION);
    cli_parse(2, h1, &o); ASSERT_EQ((int)o.action, (int)CLI_HELP);
    cli_parse(2, h2, &o); ASSERT_EQ((int)o.action, (int)CLI_HELP);
    TEST_END();
}

/* ---- errors ---- */

int test_cli_missing_value(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-sn" };
    CliOptions o;
    cli_parse(2, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    ASSERT_NOT_NULL(strstr(o.error, "-sn"));
    TEST_END();
}

int test_cli_unknown_flag(void)
{
    TEST_BEGIN();
    CliOptions o;
    char *a1[] = { "nutshell", "-x" };
    char *a2[] = { "nutshell", "--frobnicate" };
    cli_parse(2, a1, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    cli_parse(2, a2, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    ASSERT_NOT_NULL(strstr(o.error, "--frobnicate"));
    TEST_END();
}

int test_cli_bare_argument(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "foo" };
    CliOptions o;
    cli_parse(2, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    TEST_END();
}

int test_cli_mutual_exclusion(void)
{
    TEST_BEGIN();
    CliOptions o;
    char *a1[] = { "nutshell", "-sn", "a", "-h", "b" };
    char *a2[] = { "nutshell", "-v", "-l" };
    char *a3[] = { "nutshell", "-nc", "-sn", "a" };
    cli_parse(5, a1, &o); ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    cli_parse(3, a2, &o); ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    cli_parse(4, a3, &o); ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    TEST_END();
}

int test_cli_trailing_junk(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-sn", "a", "extra" };
    CliOptions o;
    cli_parse(4, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    TEST_END();
}

/* ---- value handling ---- */

int test_cli_value_resembling_flag(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-sn", "-v" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_NAME);
    ASSERT_STR_EQ(o.arg, "-v");
    TEST_END();
}

int test_cli_overlong_value(void)
{
    TEST_BEGIN();
    char big[301];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    char *argv[] = { "nutshell", "-sn", big };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_NAME);
    ASSERT_EQ((int)strlen(o.arg), 255);  /* truncated, NUL-terminated */
    TEST_END();
}

/* ---- usage text ---- */

int test_cli_usage_text_mentions_all_flags(void)
{
    TEST_BEGIN();
    const char *u = cli_usage_text();
    ASSERT_NOT_NULL(u);
    ASSERT_NOT_NULL(strstr(u, "--session-name"));
    ASSERT_NOT_NULL(strstr(u, "--host"));
    ASSERT_NOT_NULL(strstr(u, "--no-connect"));
    ASSERT_NOT_NULL(strstr(u, "--list"));
    ASSERT_NOT_NULL(strstr(u, "--version"));
    ASSERT_NOT_NULL(strstr(u, "--help"));
    ASSERT_NOT_NULL(strstr(u, "-sn"));
    ASSERT_NOT_NULL(strstr(u, "-nc"));
    ASSERT_NOT_NULL(strstr(u, "-?"));
    TEST_END();
}
