#include "test_framework.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TMP_VAL TEST_TMP_DIR "/nutshell_test_validate.json"

/* ---- settings_validate: valid defaults pass through unchanged ------------ */

int test_settings_validate_defaults(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    settings_validate(&s);
    ASSERT_STR_EQ(s.font, "Consolas");
    ASSERT_EQ(s.font_size, 10);
    ASSERT_EQ(s.scrollback_lines, 10000);
    ASSERT_EQ(s.paste_delay_ms, 350);
    TEST_END();
}

/* ---- font_size clamping -------------------------------------------------- */

int test_settings_validate_font_size_low(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.font_size = 0;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 6);
    TEST_END();
}

int test_settings_validate_font_size_high(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.font_size = 999;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 20);
    TEST_END();
}

/* ---- font_size snaps to nearest allowed size (6,8,10,12,14,16,18,20) ---- */

int test_settings_validate_font_size_snap_7(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.font_size = 7;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 6);  /* distance 1 to 6, distance 1 to 8: first wins */
    TEST_END();
}

int test_settings_validate_font_size_snap_9(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.font_size = 9;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 8);  /* distance 1 to 8, distance 1 to 10: first wins */
    TEST_END();
}

int test_settings_validate_font_size_snap_15(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.font_size = 15;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 14); /* distance 1 to 14, distance 1 to 16: first wins */
    TEST_END();
}

int test_settings_validate_font_size_snap_19(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.font_size = 19;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 18); /* distance 1 to 18, distance 1 to 20: first wins */
    TEST_END();
}

/* ---- scrollback_lines clamping ------------------------------------------ */

int test_settings_validate_scrollback_low(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.scrollback_lines = 0;
    settings_validate(&s);
    ASSERT_EQ(s.scrollback_lines, 100);
    TEST_END();
}

int test_settings_validate_scrollback_high(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.scrollback_lines = 999999;
    settings_validate(&s);
    ASSERT_EQ(s.scrollback_lines, 50000);
    TEST_END();
}

/* ---- ai_max_context_lines clamping --------------------------------------- */

int test_settings_validate_ai_max_context_lines_low(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.ai_max_context_lines = 0;
    settings_validate(&s);
    ASSERT_EQ(s.ai_max_context_lines, 1);

    s.ai_max_context_lines = -500;
    settings_validate(&s);
    ASSERT_EQ(s.ai_max_context_lines, 1);
    TEST_END();
}

int test_settings_validate_ai_max_context_lines_high(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.ai_max_context_lines = 999999;
    settings_validate(&s);
    ASSERT_EQ(s.ai_max_context_lines, 50000);
    TEST_END();
}

/* ---- ai_tool_timeout_sec clamping ---------------------------------------- */

int test_settings_validate_ai_tool_timeout_low(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.ai_tool_timeout_sec = 5;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_timeout_sec, 15);

    s.ai_tool_timeout_sec = -100;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_timeout_sec, 15);

    s.ai_tool_timeout_sec = 15;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_timeout_sec, 15);

    /* 0 is "never expire" and stays; 1..14 take the floor */
    s.ai_tool_timeout_sec = 0;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_timeout_sec, 0);

    s.ai_tool_timeout_sec = 1;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_timeout_sec, 15);

    s.ai_tool_timeout_sec = 14;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_timeout_sec, 15);
    TEST_END();
}

int test_settings_validate_ai_tool_timeout_high(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.ai_tool_timeout_sec = 99999;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_timeout_sec, 3600);

    s.ai_tool_timeout_sec = 3600;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_timeout_sec, 3600);

    s.ai_tool_timeout_sec = 120;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_timeout_sec, 120);
    TEST_END();
}

int test_settings_validate_ai_tool_rounds(void)
{
    TEST_BEGIN();
    Settings s;
    memset(&s, 0, sizeof(s));
    s.ai_tool_auto_rounds = 0;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_auto_rounds, 1);

    s.ai_tool_auto_rounds = -7;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_auto_rounds, 1);

    s.ai_tool_auto_rounds = 1;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_auto_rounds, 1);

    s.ai_tool_auto_rounds = 20;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_auto_rounds, 20);

    s.ai_tool_auto_rounds = 21;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_auto_rounds, 20);

    s.ai_tool_auto_rounds = 4;
    settings_validate(&s);
    ASSERT_EQ(s.ai_tool_auto_rounds, 4);
    TEST_END();
}

/* ---- paste_delay_ms clamping -------------------------------------------- */

int test_settings_validate_paste_delay_neg(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.paste_delay_ms = -1;
    settings_validate(&s);
    ASSERT_EQ(s.paste_delay_ms, 0);
    TEST_END();
}

int test_settings_validate_paste_delay_high(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.paste_delay_ms = 99999;
    settings_validate(&s);
    ASSERT_EQ(s.paste_delay_ms, 5000);
    TEST_END();
}

/* ---- empty font name gets default --------------------------------------- */

int test_settings_validate_empty_font(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.font[0] = '\0';
    settings_validate(&s);
    ASSERT_STR_EQ(s.font, "Consolas");
    TEST_END();
}

/* ---- NULL pointer must not crash ---------------------------------------- */

int test_settings_validate_null(void)
{
    TEST_BEGIN();
    settings_validate(NULL); /* must not crash */
    ASSERT_TRUE(1);
    TEST_END();
}

/* ---- validate is called automatically by config_load -------------------- */

int test_settings_validate_via_load(void)
{
    TEST_BEGIN();
    FILE *f = test_fopen_private(TMP_VAL);
    ASSERT_NOT_NULL(f);
    fputs("{ \"settings\": {"
          " \"font_size\": 1,"
          " \"scrollback_lines\": 10,"
          " \"paste_delay_ms\": -5"
          " } }", f);
    fclose(f);

    Config *cfg = config_load(TMP_VAL);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.font_size,        6);
    ASSERT_EQ(cfg->settings.scrollback_lines, 100);
    ASSERT_EQ(cfg->settings.paste_delay_ms,   0);

    config_free(cfg);
    remove(TMP_VAL);
    TEST_END();
}

/* ---- markdown_render_enabled default + JSON roundtrip ---- */

int test_settings_markdown_render_default(void) {
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    /* Default: markdown rendering is ON. */
    ASSERT_EQ(s.markdown_render_enabled, 1);
    TEST_END();
}

int test_settings_markdown_render_roundtrip_off(void) {
    TEST_BEGIN();
    /* Write a config with the flag explicitly false; verify it loads as 0. */
    const char *path = TEST_TMP_DIR "/nutshell_test_md_off.config";
    FILE *f = test_fopen_private(path);
    ASSERT_NOT_NULL(f);
    fputs("{ \"settings\": { \"markdown_render_enabled\": false } }", f);
    fclose(f);
    Config *cfg = config_load(path);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.markdown_render_enabled, 0);
    config_free(cfg);
    remove(path);
    TEST_END();
}

int test_settings_markdown_render_roundtrip_on(void) {
    TEST_BEGIN();
    /* Explicitly true → loads as 1. */
    const char *path = TEST_TMP_DIR "/nutshell_test_md_on.config";
    FILE *f = test_fopen_private(path);
    ASSERT_NOT_NULL(f);
    fputs("{ \"settings\": { \"markdown_render_enabled\": true } }", f);
    fclose(f);
    Config *cfg = config_load(path);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.markdown_render_enabled, 1);
    config_free(cfg);
    remove(path);
    TEST_END();
}

int test_settings_markdown_render_missing_field(void) {
    TEST_BEGIN();
    /* Missing field → falls back to default (1). */
    const char *path = TEST_TMP_DIR "/nutshell_test_md_missing.config";
    FILE *f = test_fopen_private(path);
    ASSERT_NOT_NULL(f);
    fputs("{ \"settings\": {} }", f);
    fclose(f);
    Config *cfg = config_load(path);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.markdown_render_enabled, 1);
    config_free(cfg);
    remove(path);
    TEST_END();
}

/* ---- config_save creates its file owner-only ------------------------------ */

#ifndef _WIN32
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

int test_config_save_creates_an_owner_only_file(void)
{
    TEST_BEGIN();
#ifndef _WIN32
    const char *path = TEST_TMP_DIR "/nutshell_test_save_mode.config";
    remove(path);
    remove(TEST_TMP_DIR "/nutshell_test_save_mode.config.tmp");

    /* A permissive umask must not widen the file: the mode is requested at
     * creation, not left to the umask. */
    mode_t old_mask = umask(0);
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        int rc = config_save(cfg, path);
        umask(old_mask);
        ASSERT_EQ(rc, 0);
        if (rc == 0) {
            struct stat st;
            ASSERT_EQ(stat(path, &st), 0);
            ASSERT_EQ((int)(st.st_mode & 0077), 0);
        }
        config_free(cfg);
    } else {
        umask(old_mask);
    }
    remove(path);
#endif
    TEST_END();
}

/* A stale, world-readable <path>.tmp must not leak its mode to the config. */
int test_config_save_ignores_stale_wide_tmp(void)
{
    TEST_BEGIN();
#ifndef _WIN32
    const char *path = TEST_TMP_DIR "/nutshell_test_stale_tmp.config";
    const char *tmp  = TEST_TMP_DIR "/nutshell_test_stale_tmp.config.tmp";
    remove(path);
    remove(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT, 0644);
    ASSERT_TRUE(fd >= 0);
    if (fd >= 0) {
        fchmod(fd, 0644);
        close(fd);
    }
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        int rc = config_save(cfg, path);
        ASSERT_EQ(rc, 0);
        if (rc == 0) {
            struct stat st;
            ASSERT_EQ(stat(path, &st), 0);
            ASSERT_EQ((int)(st.st_mode & 0077), 0);
        }
        config_free(cfg);
    }
    remove(path);
    remove(tmp);
#endif
    TEST_END();
}

/* Portable: saving twice to the same path works and the file reloads. */
int test_config_save_twice_then_reload(void)
{
    TEST_BEGIN();
    const char *path = TEST_TMP_DIR "/nutshell_test_save_twice.config";
    const char *tmp  = TEST_TMP_DIR "/nutshell_test_save_twice.config.tmp";
    remove(path);
    remove(tmp);
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        cfg->settings.scrollback_lines = 4321;
        ASSERT_EQ(config_save(cfg, path), 0);
        ASSERT_EQ(config_save(cfg, path), 0);
        Config *back = config_load(path);
        ASSERT_NOT_NULL(back);
        if (back) {
            ASSERT_EQ(back->settings.scrollback_lines, 4321);
            config_free(back);
        }
        config_free(cfg);
    }
    remove(path);
    remove(tmp);
    TEST_END();
}
