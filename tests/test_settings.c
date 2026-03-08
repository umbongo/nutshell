#include "test_framework.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

#define TMP_VAL "/tmp/nutshell_test_validate.json"

/* ---- settings_validate: valid defaults pass through unchanged ------------ */

int test_settings_validate_defaults(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    settings_validate(&s);
    ASSERT_STR_EQ(s.font, "Cascadia Code");
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
    ASSERT_STR_EQ(s.font, "Cascadia Code");
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
