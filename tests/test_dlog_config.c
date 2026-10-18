/* tests/test_dlog_config.c -- the debug_* settings: defaults, load, save,
 * clamps, unknown names, the debug_terminal migration, and old config files. */
#include "test_framework.h"
#include "config.h"
#include "dlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFG_PATH TEST_TMP_DIR "/nutshell_test_dlogcfg.json"

static Config *load_text(const char *json)
{
    FILE *f = test_fopen_private(CFG_PATH);
    if (!f) return NULL;
    fputs(json, f);
    fclose(f);
    return config_load(CFG_PATH);
}

static char *read_back(void)
{
    FILE *f = fopen(CFG_PATH, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)n + 1);
    if (b) {
        size_t got = fread(b, 1, (size_t)n, f);
        b[got] = '\0';
    }
    fclose(f);
    return b;
}

static void assert_defaults(const Settings *s, int *failed)
{
    if (s->debug_enabled != 0) *failed = 1;
    for (int i = 0; i < (int)DLOG_CAT_COUNT; i++)
        if (s->debug_levels[i] != (unsigned char)DLOG_LVL_INFO) *failed = 1;
    if (s->debug_include_content != 0) *failed = 1;
    if (s->debug_screenshot != 0) *failed = 1;
    if (s->debug_recorder != 1) *failed = 1;
    if (s->debug_recorder_level != (unsigned char)DLOG_LVL_TRACE) *failed = 1;
    if (strcmp(s->debug_log_dir, "logs") != 0) *failed = 1;
    if (s->debug_max_file_mb != 10) *failed = 1;
    if (s->debug_max_total_mb != 50) *failed = 1;
}

int test_dlog_cfg_defaults(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    int bad = 0;
    assert_defaults(&s, &bad);
    ASSERT_EQ(bad, 0);
    Config *c = config_new_default();
    ASSERT_NOT_NULL(c);
    if (c) {
        bad = 0;
        assert_defaults(&c->settings, &bad);
        ASSERT_EQ(bad, 0);
        config_free(c);
    }
    TEST_END();
}

int test_dlog_cfg_old_config_without_the_keys_loads_unchanged(void)
{
    TEST_BEGIN();
    Config *c = load_text("{\"settings\": {\"font_size\": 14, \"scrollback_lines\": 5000, \"logging_enabled\": true}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.scrollback_lines, 5000);
        ASSERT_EQ(c->settings.logging_enabled, 1);
        int bad = 0;
        assert_defaults(&c->settings, &bad);
        ASSERT_EQ(bad, 0);
        config_free(c);
    }
    remove(CFG_PATH);
    TEST_END();
}

int test_dlog_cfg_round_trip(void)
{
    TEST_BEGIN();
    Config *c = config_new_default();
    ASSERT_NOT_NULL(c);
    if (c) {
        Settings *s = &c->settings;
        s->debug_enabled = 1;
        s->debug_levels[DLOG_RENDER] = DLOG_LVL_TRACE;
        s->debug_levels[DLOG_TERM] = DLOG_LVL_DEBUG;
        s->debug_levels[DLOG_AI] = DLOG_LVL_ERRORS;
        s->debug_levels[DLOG_SSH] = DLOG_LVL_OFF;
        s->debug_levels[DLOG_SHELL] = DLOG_LVL_INFO;
        s->debug_levels[DLOG_GENERAL] = DLOG_LVL_TRACE;
        s->debug_include_content = 1;
        s->debug_screenshot = 1;
        s->debug_recorder = 0;
        s->debug_recorder_level = DLOG_LVL_DEBUG;
        (void)snprintf(s->debug_log_dir, sizeof(s->debug_log_dir), "%s", "D:\\diag logs\\nutshell");
        s->debug_max_file_mb = 25;
        s->debug_max_total_mb = 300;
        ASSERT_EQ(config_save(c, CFG_PATH), 0);
        config_free(c);
    }
    c = config_load(CFG_PATH);
    ASSERT_NOT_NULL(c);
    if (c) {
        Settings *s = &c->settings;
        ASSERT_EQ(s->debug_enabled, 1);
        ASSERT_EQ(s->debug_levels[DLOG_RENDER], (unsigned char)DLOG_LVL_TRACE);
        ASSERT_EQ(s->debug_levels[DLOG_TERM], (unsigned char)DLOG_LVL_DEBUG);
        ASSERT_EQ(s->debug_levels[DLOG_AI], (unsigned char)DLOG_LVL_ERRORS);
        ASSERT_EQ(s->debug_levels[DLOG_SSH], (unsigned char)DLOG_LVL_OFF);
        ASSERT_EQ(s->debug_levels[DLOG_SHELL], (unsigned char)DLOG_LVL_INFO);
        ASSERT_EQ(s->debug_levels[DLOG_GENERAL], (unsigned char)DLOG_LVL_TRACE);
        ASSERT_EQ(s->debug_include_content, 1);
        ASSERT_EQ(s->debug_screenshot, 1);
        ASSERT_EQ(s->debug_recorder, 0);
        ASSERT_EQ(s->debug_recorder_level, (unsigned char)DLOG_LVL_DEBUG);
        ASSERT_STR_EQ(s->debug_log_dir, "D:\\diag logs\\nutshell");
        ASSERT_EQ(s->debug_max_file_mb, 25);
        ASSERT_EQ(s->debug_max_total_mb, 300);
        config_free(c);
    }
    remove(CFG_PATH);
    TEST_END();
}

int test_dlog_cfg_saved_file_is_readable_and_has_no_debug_terminal(void)
{
    TEST_BEGIN();
    Config *c = config_new_default();
    ASSERT_NOT_NULL(c);
    if (c) {
        c->settings.debug_terminal = 1;                       /* legacy mirror: never written */
        c->settings.debug_levels[DLOG_RENDER] = DLOG_LVL_TRACE;
        ASSERT_EQ(config_save(c, CFG_PATH), 0);
        config_free(c);
    }
    char *t = read_back();
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_TRUE(strstr(t, "\"debug_enabled\": false") != NULL);
        ASSERT_TRUE(strstr(t, "\"debug_levels\": {\"render\": \"trace\", \"term\": \"info\", \"ai\": \"info\", "
                              "\"ssh\": \"info\", \"shell\": \"info\", \"general\": \"info\"}") != NULL);
        ASSERT_TRUE(strstr(t, "\"debug_include_content\": false") != NULL);
        ASSERT_TRUE(strstr(t, "\"debug_screenshot\": false") != NULL);
        ASSERT_TRUE(strstr(t, "\"debug_recorder\": true") != NULL);
        ASSERT_TRUE(strstr(t, "\"debug_recorder_level\": \"trace\"") != NULL);
        ASSERT_TRUE(strstr(t, "\"debug_log_dir\": \"logs\"") != NULL);
        ASSERT_TRUE(strstr(t, "\"debug_max_file_mb\": 10") != NULL);
        ASSERT_TRUE(strstr(t, "\"debug_max_total_mb\": 50") != NULL);
        ASSERT_TRUE(strstr(t, "debug_terminal") == NULL);
        free(t);
    }
    remove(CFG_PATH);
    TEST_END();
}

int test_dlog_cfg_unknown_level_names_and_keys_keep_the_default(void)
{
    TEST_BEGIN();
    Config *c = load_text("{\"settings\": {\"debug_enabled\": true, \"debug_levels\": {"
                          "\"render\": \"loud\", \"term\": \"trace\", \"nope\": \"trace\", \"ai\": 7, \"ssh\": \"Debug\"}}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_levels[DLOG_RENDER], (unsigned char)DLOG_LVL_INFO);     /* unknown name */
        ASSERT_EQ(c->settings.debug_levels[DLOG_TERM], (unsigned char)DLOG_LVL_TRACE);
        ASSERT_EQ(c->settings.debug_levels[DLOG_AI], (unsigned char)DLOG_LVL_INFO);         /* not a string */
        ASSERT_EQ(c->settings.debug_levels[DLOG_SSH], (unsigned char)DLOG_LVL_DEBUG);       /* case-insensitive */
        ASSERT_EQ(c->settings.debug_levels[DLOG_SHELL], (unsigned char)DLOG_LVL_INFO);
        config_free(c);
    }
    c = load_text("{\"settings\": {\"debug_levels\": \"trace\", \"debug_recorder_level\": \"loud\"}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        int bad = 0;
        assert_defaults(&c->settings, &bad);               /* a wrong shape changes nothing */
        ASSERT_EQ(bad, 0);
        config_free(c);
    }
    remove(CFG_PATH);
    TEST_END();
}

int test_dlog_cfg_values_are_clamped_on_load(void)
{
    TEST_BEGIN();
    Config *c = load_text("{\"settings\": {\"debug_max_file_mb\": 0, \"debug_max_total_mb\": 1, \"debug_log_dir\": \"\"}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_max_file_mb, 1);
        ASSERT_EQ(c->settings.debug_max_total_mb, 5);
        ASSERT_STR_EQ(c->settings.debug_log_dir, "logs");
        config_free(c);
    }
    c = load_text("{\"settings\": {\"debug_max_file_mb\": 500, \"debug_max_total_mb\": 99999}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_max_file_mb, 100);
        ASSERT_EQ(c->settings.debug_max_total_mb, 5000);
        config_free(c);
    }
    c = load_text("{\"settings\": {\"debug_max_file_mb\": -4, \"debug_max_total_mb\": -1}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_max_file_mb, 1);
        ASSERT_EQ(c->settings.debug_max_total_mb, 5);
        config_free(c);
    }
    remove(CFG_PATH);
    TEST_END();
}

int test_dlog_cfg_validate_clamps_programmatic_values(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.debug_enabled = 7;
    s.debug_levels[DLOG_AI] = 200;
    s.debug_recorder_level = 99;
    s.debug_include_content = 5;
    s.debug_screenshot = -1;
    s.debug_recorder = 9;
    s.debug_log_dir[0] = '\0';
    s.debug_max_file_mb = 1000;
    s.debug_max_total_mb = 0;
    settings_validate(&s);
    ASSERT_EQ(s.debug_enabled, 1);
    ASSERT_EQ(s.debug_levels[DLOG_AI], (unsigned char)DLOG_LVL_INFO);
    ASSERT_EQ(s.debug_recorder_level, (unsigned char)DLOG_LVL_TRACE);
    ASSERT_EQ(s.debug_include_content, 1);
    ASSERT_EQ(s.debug_screenshot, 1);
    ASSERT_EQ(s.debug_recorder, 1);
    ASSERT_STR_EQ(s.debug_log_dir, "logs");
    ASSERT_EQ(s.debug_max_file_mb, 100);
    ASSERT_EQ(s.debug_max_total_mb, 5);
    TEST_END();
}

int test_dlog_cfg_debug_terminal_true_migrates_once(void)
{
    TEST_BEGIN();
    Config *c = load_text("{\"settings\": {\"debug_terminal\": true}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_enabled, 1);
        ASSERT_EQ(c->settings.debug_levels[DLOG_TERM], (unsigned char)DLOG_LVL_TRACE);
        ASSERT_EQ(c->settings.debug_include_content, 1);
        ASSERT_EQ(c->settings.debug_levels[DLOG_RENDER], (unsigned char)DLOG_LVL_INFO);    /* nothing else moves */
        ASSERT_EQ(c->settings.debug_levels[DLOG_AI], (unsigned char)DLOG_LVL_INFO);
        ASSERT_EQ(c->settings.debug_screenshot, 0);
        ASSERT_EQ(c->settings.debug_recorder, 1);
        ASSERT_EQ(config_save(c, CFG_PATH), 0);
        config_free(c);
    }
    /* after a save the old key is gone and the migrated state is just the new settings */
    char *t = read_back();
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_TRUE(strstr(t, "debug_terminal") == NULL);
        ASSERT_TRUE(strstr(t, "\"debug_enabled\": true") != NULL);
        free(t);
    }
    c = config_load(CFG_PATH);
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_enabled, 1);
        ASSERT_EQ(c->settings.debug_levels[DLOG_TERM], (unsigned char)DLOG_LVL_TRACE);
        ASSERT_EQ(c->settings.debug_include_content, 1);
        c->settings.debug_enabled = 0;                         /* the user switches it off ... */
        ASSERT_EQ(config_save(c, CFG_PATH), 0);
        config_free(c);
    }
    c = config_load(CFG_PATH);                                 /* ... and it stays off: no re-migration */
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_enabled, 0);
        config_free(c);
    }
    remove(CFG_PATH);
    TEST_END();
}

int test_dlog_cfg_debug_terminal_false_or_an_explicit_new_switch_does_not_migrate(void)
{
    TEST_BEGIN();
    Config *c = load_text("{\"settings\": {\"debug_terminal\": false}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        int bad = 0;
        assert_defaults(&c->settings, &bad);
        ASSERT_EQ(bad, 0);
        config_free(c);
    }
    c = load_text("{\"settings\": {\"debug_terminal\": true, \"debug_enabled\": false}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_enabled, 0);               /* the user's explicit choice wins */
        ASSERT_EQ(c->settings.debug_include_content, 0);
        ASSERT_EQ(c->settings.debug_levels[DLOG_TERM], (unsigned char)DLOG_LVL_INFO);
        config_free(c);
    }
    remove(CFG_PATH);
    TEST_END();
}

int test_dlog_cfg_migrated_levels_do_not_override_explicit_ones(void)
{
    TEST_BEGIN();
    /* the migration only fills in what the old switch implied; term is Trace, the rest as saved */
    Config *c = load_text("{\"settings\": {\"debug_terminal\": true, \"debug_levels\": {\"render\": \"debug\"}}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_levels[DLOG_RENDER], (unsigned char)DLOG_LVL_DEBUG);
        ASSERT_EQ(c->settings.debug_levels[DLOG_TERM], (unsigned char)DLOG_LVL_TRACE);
        config_free(c);
    }
    remove(CFG_PATH);
    TEST_END();
}

int test_dlog_cfg_flat_spelling_is_read_too(void)
{
    TEST_BEGIN();
    Config *c = load_text("{\"settings\": {\"debug_enabled\": true, \"debug_level_render\": \"trace\", "
                          "\"debug_level_shell\": \"off\", \"debug_level_nope\": \"trace\", \"debug_issue_screenshot\": true}, \"profiles\": []}");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.debug_levels[DLOG_RENDER], (unsigned char)DLOG_LVL_TRACE);
        ASSERT_EQ(c->settings.debug_levels[DLOG_SHELL], (unsigned char)DLOG_LVL_OFF);
        ASSERT_EQ(c->settings.debug_levels[DLOG_TERM], (unsigned char)DLOG_LVL_INFO);
        ASSERT_EQ(c->settings.debug_screenshot, 1);
        config_free(c);
    }
    remove(CFG_PATH);
    TEST_END();
}

int test_dlog_cfg_dlog_config_from_settings_carries_every_field(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.debug_enabled = 1;
    s.debug_recorder_level = DLOG_LVL_DEBUG;
    s.debug_recorder = 0;
    s.debug_levels[DLOG_SHELL] = DLOG_LVL_OFF;
    DlogConfig d;
    dlog_config_from_settings(&s, &d);
    ASSERT_EQ(d.enabled, 1);
    ASSERT_EQ(d.recorder, 0);
    ASSERT_EQ(d.recorder_level, (unsigned char)DLOG_LVL_DEBUG);
    ASSERT_EQ(d.levels[DLOG_SHELL], (unsigned char)DLOG_LVL_OFF);
    ASSERT_STR_EQ(d.dir, "logs");
    s.debug_log_dir[0] = '\0';
    dlog_config_from_settings(&s, &d);
    ASSERT_STR_EQ(d.dir, "logs");                              /* an empty folder means the default */
    s.debug_recorder_level = 55;
    dlog_config_from_settings(&s, &d);
    ASSERT_EQ(d.recorder_level, (unsigned char)DLOG_LVL_TRACE);
    TEST_END();
}
