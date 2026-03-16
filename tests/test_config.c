#include "test_framework.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TMP_CFG "/tmp/nutshell_test.config"

/* ============================================================
 * Settings defaults
 * ============================================================ */

int test_config_default_settings(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(s.font, "Cascadia Code");
    ASSERT_EQ(s.font_size, 10);
    ASSERT_EQ(s.scrollback_lines, 10000);
    ASSERT_EQ(s.paste_delay_ms, 350);
    ASSERT_EQ(s.logging_enabled, 0);
    ASSERT_STR_EQ(s.foreground_colour, "#E0E0E0");
    ASSERT_STR_EQ(s.background_colour, "#121212");
    ASSERT_STR_EQ(s.colour_scheme, "Onyx Synapse");
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
    ASSERT_STR_EQ(cfg->settings.font, "Cascadia Code");
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
    Config *cfg = config_load("/tmp/does_not_exist_nutshell_xyz.json");
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
    ASSERT_STR_EQ(cfg->settings.font, "Cascadia Code");
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

/* ============================================================
 * AI settings
 * ============================================================ */

int test_config_default_ai_provider(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(s.ai_provider, "deepseek");
    TEST_END();
}

int test_config_default_ai_key_empty(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(s.ai_api_key, "");
    TEST_END();
}

int test_config_roundtrip_ai_settings(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    (void)snprintf(orig->settings.ai_provider,
                   sizeof(orig->settings.ai_provider), "%s", "openai");
    (void)snprintf(orig->settings.ai_api_key,
                   sizeof(orig->settings.ai_api_key), "%s", "sk-test-key-12345");

    ASSERT_EQ(config_save(orig, TMP_CFG), 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(loaded->settings.ai_provider, "openai");
    ASSERT_STR_EQ(loaded->settings.ai_api_key, "sk-test-key-12345");

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_ai_key_encrypted_on_disk(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    (void)snprintf(cfg->settings.ai_api_key,
                   sizeof(cfg->settings.ai_api_key), "%s", "secret-api-key");
    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);

    /* Read raw file and check the key is NOT in plaintext */
    FILE *f = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(f);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, f);
    raw[n] = '\0';
    fclose(f);

    ASSERT_TRUE(strstr(raw, "secret-api-key") == NULL);
    /* Should contain the encryption prefix instead */
    ASSERT_TRUE(strstr(raw, "$aes256gcm$v1$") != NULL);

    /* Verify round-trip: load should decrypt back to original */
    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(loaded->settings.ai_api_key, "secret-api-key");
    config_free(loaded);

    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_custom_provider_defaults(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(s.ai_custom_url, "");
    ASSERT_STR_EQ(s.ai_custom_model, "");
    TEST_END();
}

int test_config_roundtrip_custom_provider(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    (void)snprintf(orig->settings.ai_provider,
                   sizeof(orig->settings.ai_provider), "%s", "custom");
    (void)snprintf(orig->settings.ai_custom_url,
                   sizeof(orig->settings.ai_custom_url), "%s",
                   "http://localhost:11434/v1/chat/completions");
    (void)snprintf(orig->settings.ai_custom_model,
                   sizeof(orig->settings.ai_custom_model), "%s", "llama3");
    (void)snprintf(orig->settings.ai_api_key,
                   sizeof(orig->settings.ai_api_key), "%s", "ollama-key");

    ASSERT_EQ(config_save(orig, TMP_CFG), 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(loaded->settings.ai_provider, "custom");
    ASSERT_STR_EQ(loaded->settings.ai_custom_url,
                  "http://localhost:11434/v1/chat/completions");
    ASSERT_STR_EQ(loaded->settings.ai_custom_model, "llama3");

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Settings validation
 * ============================================================ */

int test_config_validate_null(void)
{
    TEST_BEGIN();
    settings_validate(NULL);  /* must not crash */
    ASSERT_TRUE(1);
    TEST_END();
}

int test_config_validate_font_size_snap(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);

    /* Exact match */
    s.font_size = 12;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 12);

    /* Snaps to nearest: 7 -> 6 (equidistant, picks first match) */
    s.font_size = 7;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 6);

    /* Snaps to nearest: 0 -> 6 */
    s.font_size = 0;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 6);

    /* Snaps to nearest: 100 -> 20 */
    s.font_size = 100;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 20);

    /* Negative -> 6 */
    s.font_size = -5;
    settings_validate(&s);
    ASSERT_EQ(s.font_size, 6);
    TEST_END();
}

int test_config_validate_scrollback_clamp(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);

    s.scrollback_lines = 50;
    settings_validate(&s);
    ASSERT_EQ(s.scrollback_lines, 100);

    s.scrollback_lines = 99999;
    settings_validate(&s);
    ASSERT_EQ(s.scrollback_lines, 50000);

    s.scrollback_lines = 5000;
    settings_validate(&s);
    ASSERT_EQ(s.scrollback_lines, 5000);
    TEST_END();
}

int test_config_validate_paste_delay_clamp(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);

    s.paste_delay_ms = -10;
    settings_validate(&s);
    ASSERT_EQ(s.paste_delay_ms, 0);

    s.paste_delay_ms = 9999;
    settings_validate(&s);
    ASSERT_EQ(s.paste_delay_ms, 5000);

    s.paste_delay_ms = 200;
    settings_validate(&s);
    ASSERT_EQ(s.paste_delay_ms, 200);
    TEST_END();
}

int test_config_validate_empty_font(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.font[0] = '\0';
    settings_validate(&s);
    ASSERT_STR_EQ(s.font, "Cascadia Code");
    TEST_END();
}

/* ============================================================
 * Load realistic config (reproduces user's exact file)
 * ============================================================ */

int test_config_load_realistic(void)
{
    TEST_BEGIN();
    FILE *f = fopen(TMP_CFG, "w");
    ASSERT_NOT_NULL(f);
    fputs(
        "{\n"
        "  \"settings\": {\n"
        "    \"font\": \"Cascadia Code\",\n"
        "    \"font_size\": 10,\n"
        "    \"scrollback_lines\": 10000,\n"
        "    \"paste_delay_ms\": 350,\n"
        "    \"logging_enabled\": false,\n"
        "    \"log_format\": \"%Y-%m-%d_%H-%M-%S\",\n"
        "    \"log_dir\": \"C:\\\\Users\\\\user\\\\Desktop\",\n"
        "    \"host_key_verification\": \"tofu\",\n"
        "    \"foreground_colour\": \"#000000\",\n"
        "    \"background_colour\": \"#FFFFFF\",\n"
        "    \"ai_provider\": \"deepseek\",\n"
        "    \"ai_custom_url\": \"https://api.deepseek.com/v1\",\n"
        "    \"ai_custom_model\": \"deepseek-chat\",\n"
        "    \"ai_api_key\": \"sk-3b016e9b5afc4000b28120f57bd6c7d9\"\n"
        "  },\n"
        "  \"profiles\": [\n"
        "  ]\n"
        "}\n", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_STR_EQ(cfg->settings.font, "Cascadia Code");
    ASSERT_EQ(cfg->settings.font_size, 10);
    ASSERT_EQ(cfg->settings.scrollback_lines, 10000);
    ASSERT_EQ(cfg->settings.paste_delay_ms, 350);
    ASSERT_EQ(cfg->settings.logging_enabled, 0);
    ASSERT_STR_EQ(cfg->settings.foreground_colour, "#000000");
    ASSERT_STR_EQ(cfg->settings.background_colour, "#FFFFFF");
    ASSERT_STR_EQ(cfg->settings.ai_provider, "deepseek");
    ASSERT_STR_EQ(cfg->settings.ai_custom_url, "https://api.deepseek.com/v1");
    ASSERT_STR_EQ(cfg->settings.ai_custom_model, "deepseek-chat");
    ASSERT_STR_EQ(cfg->settings.ai_api_key, "sk-3b016e9b5afc4000b28120f57bd6c7d9");
    /* Legacy config has no colour_scheme — migration fills in default */
    ASSERT_STR_EQ(cfg->settings.colour_scheme, "Onyx Synapse");
    ASSERT_EQ((int)vec_size(&cfg->profiles), 0);

    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Load config with missing optional fields (defaults)
 * ============================================================ */

int test_config_load_missing_ai_fields(void)
{
    TEST_BEGIN();
    FILE *f = fopen(TMP_CFG, "w");
    ASSERT_NOT_NULL(f);
    fputs(
        "{\n"
        "  \"settings\": {\n"
        "    \"font\": \"Cascadia Code\",\n"
        "    \"font_size\": 10\n"
        "  }\n"
        "}\n", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    /* AI fields should be defaults */
    ASSERT_STR_EQ(cfg->settings.ai_provider, "deepseek");
    ASSERT_STR_EQ(cfg->settings.ai_api_key, "");
    ASSERT_STR_EQ(cfg->settings.ai_custom_url, "");
    ASSERT_STR_EQ(cfg->settings.ai_custom_model, "");

    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Load config with unknown/extra fields (should be ignored)
 * ============================================================ */

int test_config_load_unknown_fields(void)
{
    TEST_BEGIN();
    FILE *f = fopen(TMP_CFG, "w");
    ASSERT_NOT_NULL(f);
    fputs(
        "{\n"
        "  \"settings\": {\n"
        "    \"font\": \"Hack\",\n"
        "    \"font_size\": 14,\n"
        "    \"unknown_field\": \"should be ignored\",\n"
        "    \"another_bogus\": 42\n"
        "  }\n"
        "}\n", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_STR_EQ(cfg->settings.font, "Hack");
    ASSERT_EQ(cfg->settings.font_size, 14);

    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Load config with out-of-range values (validate clamps them)
 * ============================================================ */

int test_config_load_out_of_range(void)
{
    TEST_BEGIN();
    FILE *f = fopen(TMP_CFG, "w");
    ASSERT_NOT_NULL(f);
    fputs(
        "{\n"
        "  \"settings\": {\n"
        "    \"font_size\": 999,\n"
        "    \"scrollback_lines\": -50,\n"
        "    \"paste_delay_ms\": 99999\n"
        "  }\n"
        "}\n", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    /* settings_validate should have clamped these */
    ASSERT_EQ(cfg->settings.font_size, 20);       /* nearest to 999 */
    ASSERT_EQ(cfg->settings.scrollback_lines, 100); /* min clamp */
    ASSERT_EQ(cfg->settings.paste_delay_ms, 5000);  /* max clamp */

    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Roundtrip: all fields including custom provider
 * ============================================================ */

int test_config_roundtrip_all_fields(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    Settings *s = &orig->settings;

    (void)snprintf(s->font, sizeof(s->font), "%s", "JetBrains Mono");
    s->font_size = 14;
    s->scrollback_lines = 20000;
    s->paste_delay_ms = 500;
    s->logging_enabled = 1;
    (void)snprintf(s->log_format, sizeof(s->log_format), "%s", "sess_%Y%m%d");
    (void)snprintf(s->log_dir, sizeof(s->log_dir), "%s", "/var/log/nutshell");
    (void)snprintf(s->foreground_colour, sizeof(s->foreground_colour), "%s", "#839496");
    (void)snprintf(s->background_colour, sizeof(s->background_colour), "%s", "#002B36");
    (void)snprintf(s->ai_provider, sizeof(s->ai_provider), "%s", "custom");
    (void)snprintf(s->ai_api_key, sizeof(s->ai_api_key), "%s", "my-secret-key");
    (void)snprintf(s->ai_custom_url, sizeof(s->ai_custom_url), "%s",
                   "http://localhost:11434/v1/chat/completions");
    (void)snprintf(s->ai_custom_model, sizeof(s->ai_custom_model), "%s", "llama3:70b");

    ASSERT_EQ(config_save(orig, TMP_CFG), 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    const Settings *ls = &loaded->settings;

    ASSERT_STR_EQ(ls->font, "JetBrains Mono");
    ASSERT_EQ(ls->font_size, 14);
    ASSERT_EQ(ls->scrollback_lines, 20000);
    ASSERT_EQ(ls->paste_delay_ms, 500);
    ASSERT_EQ(ls->logging_enabled, 1);
    ASSERT_STR_EQ(ls->log_format, "sess_%Y%m%d");
    ASSERT_STR_EQ(ls->log_dir, "/var/log/nutshell");
    ASSERT_STR_EQ(ls->foreground_colour, "#839496");
    ASSERT_STR_EQ(ls->background_colour, "#002B36");
    ASSERT_STR_EQ(ls->ai_provider, "custom");
    ASSERT_STR_EQ(ls->ai_custom_url,
                  "http://localhost:11434/v1/chat/completions");
    ASSERT_STR_EQ(ls->ai_custom_model, "llama3:70b");

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

int test_config_default_colour_scheme(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(s.colour_scheme, "Onyx Synapse");
    TEST_END();
}

int test_config_roundtrip_colour_scheme(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    (void)snprintf(cfg->settings.colour_scheme,
                   sizeof(cfg->settings.colour_scheme),
                   "%s", "Moss & Mist");
    int rc = config_save(cfg, TMP_CFG);
    ASSERT_EQ(rc, 0);
    config_free(cfg);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(loaded->settings.colour_scheme, "Moss & Mist");
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

