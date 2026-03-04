#include "test_framework.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

#define TEST_LOG_PATH "/tmp/nutshell_test_logger.txt"

static void cleanup(void)
{
    log_close();
    remove(TEST_LOG_PATH);
}

int test_logger_init_stderr_only(void)
{
    TEST_BEGIN();
    /* NULL file path — logs to stderr only, should not crash */
    log_init(NULL, LOG_LEVEL_INFO);
    log_write(LOG_LEVEL_INFO, "test_logger_init_stderr_only");
    log_close();
    TEST_END();
}

int test_logger_creates_file(void)
{
    TEST_BEGIN();
    remove(TEST_LOG_PATH);

    log_init(TEST_LOG_PATH, LOG_LEVEL_DEBUG);
    log_write(LOG_LEVEL_INFO, "file creation test");
    log_close();

    FILE *f = fopen(TEST_LOG_PATH, "r");
    ASSERT_NOT_NULL(f);
    if (f) {
        fclose(f);
    }
    remove(TEST_LOG_PATH);
    TEST_END();
}

int test_logger_file_contains_message(void)
{
    TEST_BEGIN();
    remove(TEST_LOG_PATH);

    log_init(TEST_LOG_PATH, LOG_LEVEL_DEBUG);
    log_write(LOG_LEVEL_INFO, "sentinel_string_xyz");
    log_close();

    FILE *f = fopen(TEST_LOG_PATH, "r");
    ASSERT_NOT_NULL(f);

    int found = 0;
    if (f) {
        char line[512];
        while (fgets(line, (int)sizeof(line), f)) {
            if (strstr(line, "sentinel_string_xyz")) {
                found = 1;
                break;
            }
        }
        fclose(f);
    }

    ASSERT_TRUE(found);
    remove(TEST_LOG_PATH);
    TEST_END();
}

int test_logger_min_level_filters(void)
{
    TEST_BEGIN();
    remove(TEST_LOG_PATH);

    /* Set min level to WARN — DEBUG and INFO should be suppressed */
    log_init(TEST_LOG_PATH, LOG_LEVEL_WARN);
    log_write(LOG_LEVEL_DEBUG, "should_not_appear_debug");
    log_write(LOG_LEVEL_INFO,  "should_not_appear_info");
    log_write(LOG_LEVEL_WARN,  "should_appear_warn");
    log_close();

    FILE *f = fopen(TEST_LOG_PATH, "r");
    ASSERT_NOT_NULL(f);

    int found_debug = 0, found_info = 0, found_warn = 0;
    if (f) {
        char line[512];
        while (fgets(line, (int)sizeof(line), f)) {
            if (strstr(line, "should_not_appear_debug")) found_debug = 1;
            if (strstr(line, "should_not_appear_info"))  found_info  = 1;
            if (strstr(line, "should_appear_warn"))      found_warn  = 1;
        }
        fclose(f);
    }

    ASSERT_FALSE(found_debug);
    ASSERT_FALSE(found_info);
    ASSERT_TRUE(found_warn);

    cleanup();
    TEST_END();
}

int test_logger_all_levels_no_crash(void)
{
    TEST_BEGIN();
    log_init(NULL, LOG_LEVEL_DEBUG);
    LOG_DEBUG("debug message");
    LOG_INFO("info message");
    LOG_WARN("warn message");
    LOG_ERROR("error message");
    log_close();
    TEST_END();
}

int test_logger_close_without_file(void)
{
    TEST_BEGIN();
    /* log_close() on a logger with no file should not crash */
    log_init(NULL, LOG_LEVEL_INFO);
    log_close();
    log_close();  /* double close should also be safe */
    TEST_END();
}
