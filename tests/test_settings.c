#include "test_framework.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TMP_VAL "/tmp/nutshell_test_validate.json"

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
    FILE *f = fopen(TMP_VAL, "w");
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
    const char *path = "/tmp/nutshell_test_md_off.config";
    FILE *f = fopen(path, "w");
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
    const char *path = "/tmp/nutshell_test_md_on.config";
    FILE *f = fopen(path, "w");
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
    const char *path = "/tmp/nutshell_test_md_missing.config";
    FILE *f = fopen(path, "w");
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
