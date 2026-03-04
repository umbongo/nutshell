#include "test_framework.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TMP_CFG "/tmp/conga_test_config.json"

/* ============================================================
 * Settings defaults
 * ============================================================ */

int test_config_default_settings(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(s.font, "Consolas");
    ASSERT_EQ(s.font_size, 12);
    ASSERT_EQ(s.scrollback_lines, 10000);
    ASSERT_EQ(s.paste_delay_ms, 350);
    ASSERT_EQ(s.logging_enabled, 0);
    ASSERT_STR_EQ(s.foreground_colour, "#0C0C0C");
    ASSERT_STR_EQ(s.background_colour, "#F2F2F2");
    ASSERT_STR_EQ(s.host_key_verification, "tofu");
    TEST_END();
}

/* ============================================================
 * Profile lifecycle
 * ============================================================ */

int test_config_profile_new_free(void)
{
    TEST_BEGIN();
    Profile *p = config_profile_new();
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->port, 22);
    ASSERT_EQ((int)p->auth_type, (int)AUTH_PASSWORD);
    config_profile_free(p);
    config_profile_free(NULL);  /* must not crash */
    ASSERT_TRUE(1);
    TEST_END();
}

/* ============================================================
 * Config new / free
 * ============================================================ */

int test_config_new_default_free(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    ASSERT_STR_EQ(cfg->settings.font, "Consolas");
    ASSERT_EQ((int)vec_size(&cfg->profiles), 0);
    config_free(cfg);
    config_free(NULL);  /* must not crash */
    ASSERT_TRUE(1);
    TEST_END();
}

/* ============================================================
 * config_load error cases
 * ============================================================ */

int test_config_load_null_path(void)
{
    TEST_BEGIN();
    Config *cfg = config_load(NULL);
    ASSERT_NULL(cfg);
    TEST_END();
}

int test_config_load_nonexistent(void)
{
    TEST_BEGIN();
    Config *cfg = config_load("/tmp/does_not_exist_conga_xyz.json");
    ASSERT_NULL(cfg);
    TEST_END();
}

int test_config_load_invalid_json(void)
{
    TEST_BEGIN();
    /* Write invalid JSON to tmp file. */
    FILE *f = fopen(TMP_CFG, "w");
    ASSERT_NOT_NULL(f);
    fputs("not valid json", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NULL(cfg);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_load_empty_object(void)
{
    TEST_BEGIN();
    /* Valid JSON but no "settings" or "profiles" keys — should return
     * a Config with defaults and an empty profile list. */
    FILE *f = fopen(TMP_CFG, "w");
    ASSERT_NOT_NULL(f);
    fputs("{}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    /* Defaults should be intact. */
    ASSERT_STR_EQ(cfg->settings.font, "Consolas");
    ASSERT_EQ((int)vec_size(&cfg->profiles), 0);
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Round-trip: save → load → compare
 * ============================================================ */

int test_config_roundtrip_settings(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);

    /* Modify a few settings. */
    (void)snprintf(orig->settings.font, sizeof(orig->settings.font),
                   "%s", "Cascadia Code");
    orig->settings.font_size        = 16;
    orig->settings.scrollback_lines = 5000;
    orig->settings.logging_enabled  = 1;
    (void)snprintf(orig->settings.foreground_colour,
                   sizeof(orig->settings.foreground_colour), "%s", "#FFFFFF");

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);

    ASSERT_STR_EQ(loaded->settings.font, "Cascadia Code");
    ASSERT_EQ(loaded->settings.font_size, 16);
    ASSERT_EQ(loaded->settings.scrollback_lines, 5000);
    ASSERT_EQ(loaded->settings.logging_enabled, 1);
    ASSERT_STR_EQ(loaded->settings.foreground_colour, "#FFFFFF");

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_roundtrip_profile(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);

    Profile *p = config_profile_new();
    (void)snprintf(p->name,     sizeof(p->name),     "%s", "My Server");
    (void)snprintf(p->host,     sizeof(p->host),     "%s", "example.com");
    p->port = 2222;
    (void)snprintf(p->username, sizeof(p->username), "%s", "admin");
    p->auth_type = AUTH_KEY;
    (void)snprintf(p->key_path, sizeof(p->key_path), "%s", "/home/user/.ssh/id_rsa");
    vec_push(&orig->profiles, p);

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ((int)vec_size(&loaded->profiles), 1);

    Profile *lp = (Profile *)vec_get(&loaded->profiles, 0u);
    ASSERT_NOT_NULL(lp);
    ASSERT_STR_EQ(lp->name,     "My Server");
    ASSERT_STR_EQ(lp->host,     "example.com");
    ASSERT_EQ(lp->port, 2222);
    ASSERT_STR_EQ(lp->username, "admin");
    ASSERT_EQ((int)lp->auth_type, (int)AUTH_KEY);
    ASSERT_STR_EQ(lp->key_path, "/home/user/.ssh/id_rsa");

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_roundtrip_multiple_profiles(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);

    for (int i = 0; i < 3; i++) {
        Profile *p = config_profile_new();
        char buf[CFG_STR_MAX];
        (void)snprintf(buf, sizeof(buf), "Server%d", i);
        (void)snprintf(p->name, sizeof(p->name), "%s", buf);
        (void)snprintf(buf, sizeof(buf), "host%d.example.com", i);
        (void)snprintf(p->host, sizeof(p->host), "%s", buf);
        p->port = 22 + i;
        vec_push(&orig->profiles, p);
    }

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ((int)vec_size(&loaded->profiles), 3);

    for (int i = 0; i < 3; i++) {
        Profile *lp = (Profile *)vec_get(&loaded->profiles, (size_t)i);
        ASSERT_NOT_NULL(lp);
        char expected_name[CFG_STR_MAX];
        (void)snprintf(expected_name, sizeof(expected_name), "Server%d", i);
        ASSERT_STR_EQ(lp->name, expected_name);
        ASSERT_EQ(lp->port, 22 + i);
    }

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_save_null(void)
{
    TEST_BEGIN();
    int rc = config_save(NULL, TMP_CFG);
    ASSERT_EQ(rc, -1);
    Config *cfg = config_new_default();
    rc = config_save(cfg, NULL);
    ASSERT_EQ(rc, -1);
    config_free(cfg);
    TEST_END();
}
