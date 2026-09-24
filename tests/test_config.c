#include "test_framework.h"
#include "ai_prompt.h"
#include "config.h"
#include "cmd_classify.h"
#include "crypto.h"
#include "crypto_dpapi.h"
#include "fake_dpapi.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

#define TMP_CFG TEST_TMP_DIR "/nutshell_test.config"

/* Any config_load() of an unparseable TMP_CFG renames it to
 * "nutshell_test.config.bad-<timestamp>" beside it (config_backup_unparseable_file()
 * in loader.c). Several tests below trigger that as a side effect without
 * caring about it -- these two helpers find/remove those backups so they
 * don't accumulate in TEST_TMP_DIR across runs. */
static int test_config_find_latest_bad_backup(char *out, size_t out_cap)
{
    if (out && out_cap > 0) out[0] = '\0';
    DIR *d = opendir(TEST_TMP_DIR);
    if (!d) return 0;
    char latest_name[300] = "";
    int found = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strstr(de->d_name, "nutshell_test.config.bad-") != de->d_name) continue;
        found = 1;
        if (strcmp(de->d_name, latest_name) > 0) {
            (void)snprintf(latest_name, sizeof(latest_name), "%s", de->d_name);
        }
    }
    closedir(d);
    if (found && out) {
        (void)snprintf(out, out_cap, "%s/%s", TEST_TMP_DIR, latest_name);
    }
    return found;
}

static void test_config_remove_all_bad_backups(void)
{
    DIR *d = opendir(TEST_TMP_DIR);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strstr(de->d_name, "nutshell_test.config.bad-") != de->d_name) continue;
        char full[600];
        (void)snprintf(full, sizeof(full), "%s/%s", TEST_TMP_DIR, de->d_name);
        (void)remove(full);
    }
    closedir(d);
}

/* ============================================================
 * Settings defaults
 * ============================================================ */

int test_config_default_settings(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(s.font, "Consolas");
    ASSERT_EQ(s.font_size, 10);
    ASSERT_EQ(s.scrollback_lines, 10000);
    ASSERT_EQ(s.paste_delay_ms, 350);
    ASSERT_EQ(s.logging_enabled, 0);
    ASSERT_STR_EQ(s.foreground_colour, "#E0E0E0");
    ASSERT_STR_EQ(s.background_colour, "#121212");
    ASSERT_STR_EQ(s.colour_scheme, "Onyx Synapse");
    ASSERT_STR_EQ(s.host_key_verification, "tofu");
    ASSERT_EQ(s.ai_max_context_lines, 1000);
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
    Config *cfg = config_load("/tmp/does_not_exist_nutshell_xyz.json");
    ASSERT_NULL(cfg);
    TEST_END();
}

int test_config_load_invalid_json(void)
{
    TEST_BEGIN();
    /* Write invalid JSON to tmp file. */
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("not valid json", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NULL(cfg);
    remove(TMP_CFG);
    /* config_load() renames the unparseable file to a ".bad-<timestamp>"
     * backup rather than just dropping it (see the backup tests further
     * down) -- clean that up here too, so it doesn't linger. */
    test_config_remove_all_bad_backups();
    TEST_END();
}

int test_config_load_empty_object(void)
{
    TEST_BEGIN();
    /* Valid JSON but no "settings" or "profiles" keys — should return
     * a Config with defaults and an empty profile list. */
    FILE *f = test_fopen_private(TMP_CFG);
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
                   "%s", "Consolas");
    orig->settings.font_size        = 16;
    orig->settings.scrollback_lines = 5000;
    orig->settings.logging_enabled  = 1;
    (void)snprintf(orig->settings.foreground_colour,
                   sizeof(orig->settings.foreground_colour), "%s", "#FFFFFF");
    orig->settings.ai_max_context_lines = 2500;

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);

    ASSERT_STR_EQ(loaded->settings.font, "Consolas");
    ASSERT_EQ(loaded->settings.font_size, 16);
    ASSERT_EQ(loaded->settings.scrollback_lines, 5000);
    ASSERT_EQ(loaded->settings.logging_enabled, 1);
    ASSERT_STR_EQ(loaded->settings.foreground_colour, "#FFFFFF");
    ASSERT_EQ(loaded->settings.ai_max_context_lines, 2500);

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
    ASSERT_STR_EQ(s.ai_provider, AI_DEFAULT_PROVIDER);
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
    /* Should contain the DPAPI encryption prefix instead -- every new
     * write uses DPAPI; the legacy AES-256-GCM prefix is load-only, for
     * migrating an older config. */
    ASSERT_TRUE(strstr(raw, "$dpapi$v1$") != NULL);
    ASSERT_TRUE(strstr(raw, "$aes256gcm$v1$") == NULL);

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
    ASSERT_STR_EQ(s.font, "Consolas");
    TEST_END();
}

/* ============================================================
 * Load realistic config (reproduces user's exact file)
 * ============================================================ */

int test_config_load_realistic(void)
{
    TEST_BEGIN();
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(
        "{\n"
        "  \"settings\": {\n"
        "    \"font\": \"Consolas\",\n"
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
        "    \"ai_api_key\": \"sk-test-placeholder-not-a-real-key\"\n"
        "  },\n"
        "  \"profiles\": [\n"
        "  ]\n"
        "}\n", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_STR_EQ(cfg->settings.font, "Consolas");
    ASSERT_EQ(cfg->settings.font_size, 10);
    ASSERT_EQ(cfg->settings.scrollback_lines, 10000);
    ASSERT_EQ(cfg->settings.paste_delay_ms, 350);
    ASSERT_EQ(cfg->settings.logging_enabled, 0);
    ASSERT_STR_EQ(cfg->settings.foreground_colour, "#000000");
    ASSERT_STR_EQ(cfg->settings.background_colour, "#FFFFFF");
    ASSERT_STR_EQ(cfg->settings.ai_provider, "deepseek");
    ASSERT_STR_EQ(cfg->settings.ai_custom_url, "https://api.deepseek.com/v1");
    ASSERT_STR_EQ(cfg->settings.ai_custom_model, "deepseek-chat");
    ASSERT_STR_EQ(cfg->settings.ai_api_key, "sk-test-placeholder-not-a-real-key");
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
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(
        "{\n"
        "  \"settings\": {\n"
        "    \"font\": \"Consolas\",\n"
        "    \"font_size\": 10\n"
        "  }\n"
        "}\n", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    /* AI fields should be defaults */
    ASSERT_STR_EQ(cfg->settings.ai_provider, AI_DEFAULT_PROVIDER);
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
    FILE *f = test_fopen_private(TMP_CFG);
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
    FILE *f = test_fopen_private(TMP_CFG);
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

/* ============================================================
 * AI Assist font settings
 * ============================================================ */

int test_config_default_ai_font(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(s.ai_font, "Consolas");
    TEST_END();
}

int test_config_roundtrip_ai_font(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    (void)snprintf(cfg->settings.ai_font,
                   sizeof(cfg->settings.ai_font),
                   "%s", "Inter");
    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(loaded->settings.ai_font, "Inter");
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_validate_empty_ai_font(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.ai_font[0] = '\0';
    settings_validate(&s);
    ASSERT_STR_EQ(s.ai_font, "Consolas");
    TEST_END();
}

/* ============================================================
 * Profile lookup (config_find_profile_by_name / _by_host)
 * ============================================================ */

static Config *make_lookup_cfg(void)
{
    Config *cfg = config_new_default();
    Profile *a = config_profile_new();
    snprintf(a->name, sizeof(a->name), "Automaton");
    snprintf(a->host, sizeof(a->host), "automaton.local");
    vec_push(&cfg->profiles, a);
    Profile *b = config_profile_new();
    /* unnamed profile — host only */
    snprintf(b->host, sizeof(b->host), "backup.example.com");
    vec_push(&cfg->profiles, b);
    Profile *c = config_profile_new();
    snprintf(c->name, sizeof(c->name), "automaton");  /* duplicate name, different case */
    snprintf(c->host, sizeof(c->host), "other.host");
    vec_push(&cfg->profiles, c);
    return cfg;
}

int test_find_profile_by_name_exact(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    Profile *p = config_find_profile_by_name(cfg, "Automaton");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->host, "automaton.local");
    config_free(cfg);
    TEST_END();
}

int test_find_profile_by_name_case_insensitive(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    Profile *p = config_find_profile_by_name(cfg, "AUTOMATON");
    ASSERT_NOT_NULL(p);
    /* first match wins: profile 'a', not the duplicate 'c' */
    ASSERT_STR_EQ(p->host, "automaton.local");
    config_free(cfg);
    TEST_END();
}

int test_find_profile_by_name_absent(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    ASSERT_NULL(config_find_profile_by_name(cfg, "nosuch"));
    config_free(cfg);
    TEST_END();
}

int test_find_profile_null_and_empty(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    ASSERT_NULL(config_find_profile_by_name(NULL, "x"));
    ASSERT_NULL(config_find_profile_by_name(cfg, NULL));
    ASSERT_NULL(config_find_profile_by_name(cfg, ""));
    ASSERT_NULL(config_find_profile_by_host(NULL, "x"));
    ASSERT_NULL(config_find_profile_by_host(cfg, NULL));
    ASSERT_NULL(config_find_profile_by_host(cfg, ""));
    config_free(cfg);
    TEST_END();
}

int test_find_profile_empty_name_never_matches(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    /* profile 'b' has empty name — empty query must not match it */
    ASSERT_NULL(config_find_profile_by_name(cfg, ""));
    config_free(cfg);
    TEST_END();
}

int test_find_profile_by_host_case_insensitive(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    Profile *p = config_find_profile_by_host(cfg, "BACKUP.example.COM");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->host, "backup.example.com");
    config_free(cfg);
    TEST_END();
}

int test_find_profile_no_partial_match(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    ASSERT_NULL(config_find_profile_by_name(cfg, "Auto"));       /* prefix */
    ASSERT_NULL(config_find_profile_by_host(cfg, "backup"));     /* prefix */
    config_free(cfg);
    TEST_END();
}

/* ============================================================
 * SSH user-idle timeout setting
 * ============================================================ */

int test_config_default_ssh_user_idle(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 0);
    TEST_END();
}

int test_config_validate_ssh_user_idle_clamp(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);

    s.ssh_user_idle_timeout_mins = -5;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 0);

    s.ssh_user_idle_timeout_mins = 99999;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 10080);

    s.ssh_user_idle_timeout_mins = 180;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 180);

    s.ssh_user_idle_timeout_mins = 0;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 0);

    s.ssh_user_idle_timeout_mins = 10080;
    settings_validate(&s);
    ASSERT_EQ(s.ssh_user_idle_timeout_mins, 10080);
    TEST_END();
}

int test_config_roundtrip_ssh_user_idle(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);
    orig->settings.ssh_user_idle_timeout_mins = 240;

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(loaded->settings.ssh_user_idle_timeout_mins, 240);

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Auto-connect settings
 * ============================================================ */

int test_config_default_auto_connect(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(s.auto_connect, 0);
    ASSERT_STR_EQ(s.auto_connect_session, "");
    TEST_END();
}

int test_config_validate_auto_connect_clamp(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.auto_connect = 7;
    settings_validate(&s);
    ASSERT_EQ(s.auto_connect, 1);
    s.auto_connect = -3;
    settings_validate(&s);
    ASSERT_EQ(s.auto_connect, 1);
    s.auto_connect = 0;
    settings_validate(&s);
    ASSERT_EQ(s.auto_connect, 0);
    TEST_END();
}

int test_config_roundtrip_auto_connect(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    cfg->settings.auto_connect = 1;
    snprintf(cfg->settings.auto_connect_session,
             sizeof(cfg->settings.auto_connect_session), "automaton");
    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    Config *re = config_load(TMP_CFG);
    ASSERT_NOT_NULL(re);
    ASSERT_EQ(re->settings.auto_connect, 1);
    ASSERT_STR_EQ(re->settings.auto_connect_session, "automaton");
    config_free(re);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_load_legacy_no_auto_connect(void)
{
    TEST_BEGIN();
    /* A config written before these fields existed must default them. */
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": []}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.auto_connect, 0);
    ASSERT_STR_EQ(cfg->settings.auto_connect_session, "");
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Review-fixes settings: paste_confirm, open_session_manager_at_start,
 * ai_policy_default
 * ============================================================ */

int test_config_default_review_fix_settings(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(s.paste_confirm, 1);
    ASSERT_EQ(s.open_session_manager_at_start, 0);
    ASSERT_EQ(s.ai_policy_default.allowed, (int)CMD_READ);
    ASSERT_EQ(s.ai_policy_default.unattended, POLICY_NONE);
    TEST_END();
}

int test_config_validate_review_fix_settings_clamp(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);

    s.paste_confirm = 7;
    s.open_session_manager_at_start = -3;
    s.ai_policy_default.allowed = 42;
    s.ai_policy_default.unattended = 42;
    settings_validate(&s);
    ASSERT_EQ(s.paste_confirm, 1);
    ASSERT_EQ(s.open_session_manager_at_start, 1);
    ASSERT_EQ(s.ai_policy_default.allowed, (int)CMD_CRITICAL);
    ASSERT_EQ(s.ai_policy_default.unattended, (int)CMD_CRITICAL);

    s.paste_confirm = 0;
    s.open_session_manager_at_start = 0;
    s.ai_policy_default.allowed = -1;
    s.ai_policy_default.unattended = -9;
    settings_validate(&s);
    ASSERT_EQ(s.paste_confirm, 0);
    ASSERT_EQ(s.open_session_manager_at_start, 0);
    ASSERT_EQ(s.ai_policy_default.allowed, (int)CMD_READ);
    ASSERT_EQ(s.ai_policy_default.unattended, POLICY_NONE);

    /* The invariant survives validation: an unattended marker above the
     * ceiling comes down to it, and the ceiling is never raised. */
    s.ai_policy_default.allowed = CMD_UNKNOWN;
    s.ai_policy_default.unattended = CMD_CRITICAL;
    settings_validate(&s);
    ASSERT_EQ(s.ai_policy_default.allowed, (int)CMD_UNKNOWN);
    ASSERT_EQ(s.ai_policy_default.unattended, (int)CMD_UNKNOWN);

    /* In-range values pass through untouched. */
    s.ai_policy_default.allowed = CMD_WRITE;
    s.ai_policy_default.unattended = CMD_READ;
    settings_validate(&s);
    ASSERT_EQ(s.ai_policy_default.allowed, (int)CMD_WRITE);
    ASSERT_EQ(s.ai_policy_default.unattended, (int)CMD_READ);
    TEST_END();
}

int test_config_roundtrip_review_fix_settings(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);
    orig->settings.paste_confirm = 0;
    orig->settings.open_session_manager_at_start = 1;
    orig->settings.ai_policy_default.allowed = CMD_WRITE;
    orig->settings.ai_policy_default.unattended = CMD_READ;

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ(loaded->settings.paste_confirm, 0);
    ASSERT_EQ(loaded->settings.open_session_manager_at_start, 1);
    ASSERT_EQ(loaded->settings.ai_policy_default.allowed, (int)CMD_WRITE);
    ASSERT_EQ(loaded->settings.ai_policy_default.unattended, (int)CMD_READ);

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_load_legacy_no_review_fix_settings(void)
{
    TEST_BEGIN();
    /* A config written before these fields existed must default them. */
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": []}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.paste_confirm, 1);
    ASSERT_EQ(cfg->settings.open_session_manager_at_start, 0);
    ASSERT_EQ(cfg->settings.ai_policy_default.allowed, (int)CMD_READ);
    ASSERT_EQ(cfg->settings.ai_policy_default.unattended, POLICY_NONE);
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_load_ignores_old_auto_approve_all_key(void)
{
    TEST_BEGIN();
    /* The oldest boolean key is dropped with no migration: a config file
     * that still has it must fall back to the default policy, not
     * read/coerce the stale key into a permissive one. */
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("{\"settings\": {\"font\": \"Consolas\", "
          "\"ai_auto_approve_all\": true}, \"profiles\": []}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.ai_policy_default.allowed, (int)CMD_READ);
    ASSERT_EQ(cfg->settings.ai_policy_default.unattended, POLICY_NONE);
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * ai_policy_default: the one policy key, and migration from the two
 * settings it replaces
 * (2026-09-11-status-policy-control-design.md section 6)
 * ============================================================ */

int test_config_policy_token_round_trips_every_pair(void)
{
    TEST_BEGIN();
    /* All 14 legal (allowed, unattended) pairs survive save/load. */
    int pairs = 0;
    for (int a = 0; a < POLICY_STOP_COUNT; a++) {
        for (int u = POLICY_NONE; u <= a; u++) {
            Config *orig = config_new_default();
            ASSERT_NOT_NULL(orig);
            orig->settings.ai_policy_default.allowed = a;
            orig->settings.ai_policy_default.unattended = u;

            int rc = config_save(orig, TMP_CFG);
            ASSERT_EQ(rc, 0);

            Config *loaded = config_load(TMP_CFG);
            ASSERT_NOT_NULL(loaded);
            if (loaded->settings.ai_policy_default.allowed != a ||
                loaded->settings.ai_policy_default.unattended != u) {
                printf("  {%d,%d} round-tripped as {%d,%d}\n", a, u,
                       loaded->settings.ai_policy_default.allowed,
                       loaded->settings.ai_policy_default.unattended);
                _tf_local_fail = 1;
            }
            config_free(orig);
            config_free(loaded);
            remove(TMP_CFG);
            pairs++;
        }
    }
    ASSERT_EQ(pairs, 14);
    TEST_END();
}

int test_config_save_writes_policy_key_and_neither_old_key(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);
    orig->settings.ai_policy_default.allowed = CMD_WRITE;
    orig->settings.ai_policy_default.unattended = CMD_READ;
    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    FILE *f = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(f);
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    ASSERT_TRUE(strstr(buf, "\"ai_policy_default\": \"write/read\"") != NULL);
    /* Neither superseded key is written back. */
    ASSERT_NULL(strstr(buf, "ai_auto_approve_mode"));
    ASSERT_NULL(strstr(buf, "ai_auto_approve_default"));

    config_free(orig);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_load_migrates_v1_1_16_auto_approve_mode(void)
{
    TEST_BEGIN();
    /* A v1.1.16 config carries an auto-approve mode and no ceiling of its
     * own, because the old permit-write flag was per session and always
     * started off. So every mode migrates to ceiling `read`, and to
     * unattended `read` unless the mode was "off" -- READ was the only
     * category such a session could ever actually run unattended. Migrating
     * the mode's top category onto the new ceiling would start new sessions
     * MORE permissively than the same config starts them today, which a
     * migration must never do. */
    static const char *const modes[6] = {
        "off", "safe", "safe+unknown", "safe+write", "safe+unknown+write", "all"
    };
    for (int i = 0; i < 6; i++) {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"settings\": {\"font\": \"Consolas\", "
                 "\"ai_auto_approve_mode\": \"%s\"}, \"profiles\": []}", modes[i]);
        FILE *f = test_fopen_private(TMP_CFG);
        ASSERT_NOT_NULL(f);
        fputs(json, f);
        fclose(f);

        Config *cfg = config_load(TMP_CFG);
        ASSERT_NOT_NULL(cfg);
        int want_u = (i == 0) ? POLICY_NONE : (int)CMD_READ;
        if (cfg->settings.ai_policy_default.allowed != (int)CMD_READ ||
            cfg->settings.ai_policy_default.unattended != want_u) {
            printf("  \"%s\" -> {%d,%d}, expected {%d,%d}\n", modes[i],
                   cfg->settings.ai_policy_default.allowed,
                   cfg->settings.ai_policy_default.unattended,
                   (int)CMD_READ, want_u);
            _tf_local_fail = 1;
        }
        config_free(cfg);
        remove(TMP_CFG);
    }
    TEST_END();
}

int test_config_load_migrates_pre_v1_1_16_numeric_auto_approve(void)
{
    TEST_BEGIN();
    /* Older still: the numeric key, 0 = off and 1..3 = on at some level.
     * Same rule -- ceiling `read`, unattended `read` unless it was off.
     * An out-of-range number is treated as off, never as on. */
    static const int legacy[6]  = { 0, 1, 2, 3, 4, -1 };
    static const int want_on[6] = { 0, 1, 1, 1, 0,  0 };
    for (int i = 0; i < 6; i++) {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"settings\": {\"font\": \"Consolas\", "
                 "\"ai_auto_approve_default\": %d}, \"profiles\": []}", legacy[i]);
        FILE *f = test_fopen_private(TMP_CFG);
        ASSERT_NOT_NULL(f);
        fputs(json, f);
        fclose(f);

        Config *cfg = config_load(TMP_CFG);
        ASSERT_NOT_NULL(cfg);
        int want_u = want_on[i] ? (int)CMD_READ : POLICY_NONE;
        if (cfg->settings.ai_policy_default.allowed != (int)CMD_READ ||
            cfg->settings.ai_policy_default.unattended != want_u) {
            printf("  legacy %d -> {%d,%d}, expected {%d,%d}\n", legacy[i],
                   cfg->settings.ai_policy_default.allowed,
                   cfg->settings.ai_policy_default.unattended,
                   (int)CMD_READ, want_u);
            _tf_local_fail = 1;
        }
        config_free(cfg);
        remove(TMP_CFG);
    }
    TEST_END();
}

int test_config_load_policy_key_wins_over_both_old_keys(void)
{
    TEST_BEGIN();
    /* All three present: the new key is authoritative, and neither old key
     * is merged into it. */
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("{\"settings\": {\"font\": \"Consolas\", "
          "\"ai_auto_approve_default\": 1, "
          "\"ai_auto_approve_mode\": \"all\", "
          "\"ai_policy_default\": \"write/unknown\"}, \"profiles\": []}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.ai_policy_default.allowed, (int)CMD_WRITE);
    ASSERT_EQ(cfg->settings.ai_policy_default.unattended, (int)CMD_UNKNOWN);
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_load_garbage_policy_token_is_the_default(void)
{
    TEST_BEGIN();
    /* Garbage must fall back to read-only/nothing-unattended -- never to
     * something more permissive, and never leave the field uninitialised. */
    static const char *const bad[] = {
        "nonsense", "", "critical", "safe+write", "none/none", "critical/"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"settings\": {\"font\": \"Consolas\", "
                 "\"ai_policy_default\": \"%s\"}, \"profiles\": []}", bad[i]);
        FILE *f = test_fopen_private(TMP_CFG);
        ASSERT_NOT_NULL(f);
        fputs(json, f);
        fclose(f);

        Config *cfg = config_load(TMP_CFG);
        ASSERT_NOT_NULL(cfg);
        if (cfg->settings.ai_policy_default.allowed != (int)CMD_READ ||
            cfg->settings.ai_policy_default.unattended != POLICY_NONE) {
            printf("  \"%s\" -> {%d,%d}\n", bad[i],
                   cfg->settings.ai_policy_default.allowed,
                   cfg->settings.ai_policy_default.unattended);
            _tf_local_fail = 1;
        }
        config_free(cfg);
        remove(TMP_CFG);
    }
    TEST_END();
}

int test_config_load_inverted_policy_token_clamps_down(void)
{
    TEST_BEGIN();
    /* A hand-edited config asking to run Critical unattended under a Write
     * ceiling gets the unattended marker clamped DOWN to the ceiling, not
     * the ceiling raised. */
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("{\"settings\": {\"font\": \"Consolas\", "
          "\"ai_policy_default\": \"write/critical\"}, \"profiles\": []}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.ai_policy_default.allowed, (int)CMD_WRITE);
    ASSERT_EQ(cfg->settings.ai_policy_default.unattended, (int)CMD_WRITE);
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Profile platform (device platform setting, audit H2)
 * ============================================================ */

int test_config_profile_platform_missing_defaults_to_auto(void)
{
    TEST_BEGIN();
    /* A profile written before "platform" existed must default to "auto",
     * same as config_profile_new() does for a brand-new profile. */
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("{\"settings\": {}, \"profiles\": ["
          "{\"name\": \"Old Box\", \"host\": \"old.example.com\"}"
          "]}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ((int)vec_size(&cfg->profiles), 1);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0u);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->platform, "auto");
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_profile_platform_roundtrip(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);

    Profile *p = config_profile_new();
    (void)snprintf(p->host,     sizeof(p->host),     "%s", "switch.example.com");
    (void)snprintf(p->platform, sizeof(p->platform), "%s", "cisco-nxos");
    vec_push(&orig->profiles, p);

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ((int)vec_size(&loaded->profiles), 1);

    Profile *lp = (Profile *)vec_get(&loaded->profiles, 0u);
    ASSERT_NOT_NULL(lp);
    ASSERT_STR_EQ(lp->platform, "cisco-nxos");

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_profile_platform_garbage_maps_unknown(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);

    Profile *p = config_profile_new();
    (void)snprintf(p->host,     sizeof(p->host),     "%s", "mystery.example.com");
    (void)snprintf(p->platform, sizeof(p->platform), "%s", "not-a-real-platform");
    vec_push(&orig->profiles, p);

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    Profile *lp = (Profile *)vec_get(&loaded->profiles, 0u);
    ASSERT_NOT_NULL(lp);
    /* Config storage doesn't validate -- the garbage string round-trips
     * as-is... */
    ASSERT_STR_EQ(lp->platform, "not-a-real-platform");
    /* ...but resolving it at connect time maps it safely to UNKNOWN rather
     * than mis-detecting some real platform. */
    ASSERT_EQ((int)cmd_platform_from_name(lp->platform), (int)CMD_PLATFORM_UNKNOWN);

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Profile kind / shell (local shell design, section 5)
 * ============================================================ */

int test_config_profile_new_defaults_kind_ssh_shell_empty(void)
{
    TEST_BEGIN();
    Profile *p = config_profile_new();
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->kind, "ssh");
    ASSERT_STR_EQ(p->shell, "");
    config_profile_free(p);
    TEST_END();
}

int test_config_profile_kind_missing_defaults_to_ssh(void)
{
    TEST_BEGIN();
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("{\"settings\": {}, \"profiles\": ["
          "{\"name\": \"Old Box\", \"host\": \"old.example.com\"}"
          "]}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ((int)vec_size(&cfg->profiles), 1);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0u);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->kind, "ssh");
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_profile_kind_local_allows_empty_host_user_password(void)
{
    TEST_BEGIN();
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("{\"settings\": {}, \"profiles\": ["
          "{\"name\": \"Local shell\", \"kind\": \"local\", "
          "\"host\": \"\", \"username\": \"\", \"password\": \"\"}"
          "]}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ((int)vec_size(&cfg->profiles), 1);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0u);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->kind, "local");
    ASSERT_STR_EQ(p->host, "");
    ASSERT_STR_EQ(p->username, "");
    ASSERT_STR_EQ(p->password, "");
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_profile_shell_roundtrip_with_backslashes_and_spaces(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);

    Profile *p = config_profile_new();
    (void)snprintf(p->name, sizeof(p->name), "%s", "Local shell");
    (void)snprintf(p->kind, sizeof(p->kind), "%s", "local");
    (void)snprintf(p->shell, sizeof(p->shell), "%s",
                   "C:\\Program Files\\Git\\bin\\bash.exe --login -i");
    vec_push(&orig->profiles, p);

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ((int)vec_size(&loaded->profiles), 1);
    Profile *lp = (Profile *)vec_get(&loaded->profiles, 0u);
    ASSERT_NOT_NULL(lp);
    ASSERT_STR_EQ(lp->shell, "C:\\Program Files\\Git\\bin\\bash.exe --login -i");

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_profile_kind_shell_roundtrip_multiple_profiles(void)
{
    TEST_BEGIN();
    Config *orig = config_new_default();
    ASSERT_NOT_NULL(orig);

    Profile *a = config_profile_new();
    (void)snprintf(a->name, sizeof(a->name), "%s", "Server A");
    (void)snprintf(a->host, sizeof(a->host), "%s", "a.example.com");
    vec_push(&orig->profiles, a);

    Profile *b = config_profile_new();
    (void)snprintf(b->name, sizeof(b->name), "%s", "Local shell");
    (void)snprintf(b->kind, sizeof(b->kind), "%s", "local");
    (void)snprintf(b->shell, sizeof(b->shell), "%s", "/bin/zsh -l");
    vec_push(&orig->profiles, b);

    Profile *c = config_profile_new();
    (void)snprintf(c->name, sizeof(c->name), "%s", "Server C");
    (void)snprintf(c->host, sizeof(c->host), "%s", "c.example.com");
    vec_push(&orig->profiles, c);

    int rc = config_save(orig, TMP_CFG);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ((int)vec_size(&loaded->profiles), 3);

    Profile *la = (Profile *)vec_get(&loaded->profiles, 0u);
    Profile *lb = (Profile *)vec_get(&loaded->profiles, 1u);
    Profile *lc = (Profile *)vec_get(&loaded->profiles, 2u);
    ASSERT_NOT_NULL(la);
    ASSERT_NOT_NULL(lb);
    ASSERT_NOT_NULL(lc);
    ASSERT_STR_EQ(la->kind, "ssh");
    ASSERT_STR_EQ(la->shell, "");
    ASSERT_STR_EQ(lb->kind, "local");
    ASSERT_STR_EQ(lb->shell, "/bin/zsh -l");
    ASSERT_STR_EQ(lc->kind, "ssh");
    ASSERT_STR_EQ(lc->shell, "");

    config_free(orig);
    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * config_ensure_local_profile
 * ============================================================ */

int test_config_ensure_local_profile_inserts_when_absent(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    Profile *first = config_profile_new();
    (void)snprintf(first->name, sizeof(first->name), "%s", "Server A");
    (void)snprintf(first->host, sizeof(first->host), "%s", "a.example.com");
    vec_push(&cfg->profiles, first);

    int rc = config_ensure_local_profile(cfg);
    ASSERT_EQ(rc, 1);
    ASSERT_EQ((int)vec_size(&cfg->profiles), 2);

    Profile *p0 = (Profile *)vec_get(&cfg->profiles, 0u);
    ASSERT_NOT_NULL(p0);
    ASSERT_STR_EQ(p0->name, "Local shell");
    ASSERT_STR_EQ(p0->kind, "local");
    ASSERT_STR_EQ(p0->shell, "");

    Profile *p1 = (Profile *)vec_get(&cfg->profiles, 1u);
    ASSERT_EQ(p1, first);
    ASSERT_STR_EQ(p1->name, "Server A");

    config_free(cfg);
    TEST_END();
}

int test_config_ensure_local_profile_second_call_is_noop(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);

    ASSERT_EQ(config_ensure_local_profile(cfg), 1);
    ASSERT_EQ((int)vec_size(&cfg->profiles), 1);
    ASSERT_EQ(config_ensure_local_profile(cfg), 0);
    ASSERT_EQ((int)vec_size(&cfg->profiles), 1);

    config_free(cfg);
    TEST_END();
}

int test_config_ensure_local_profile_noop_when_local_already_first(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    Profile *local = config_profile_new();
    (void)snprintf(local->name, sizeof(local->name), "%s", "Local shell");
    (void)snprintf(local->kind, sizeof(local->kind), "%s", "local");
    vec_push(&cfg->profiles, local);

    int rc = config_ensure_local_profile(cfg);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ((int)vec_size(&cfg->profiles), 1);

    config_free(cfg);
    TEST_END();
}

int test_config_ensure_local_profile_noop_when_local_at_end(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    Profile *a = config_profile_new();
    (void)snprintf(a->name, sizeof(a->name), "%s", "Server A");
    vec_push(&cfg->profiles, a);
    Profile *local = config_profile_new();
    (void)snprintf(local->name, sizeof(local->name), "%s", "My Terminal");
    (void)snprintf(local->kind, sizeof(local->kind), "%s", "local");
    vec_push(&cfg->profiles, local);

    int rc = config_ensure_local_profile(cfg);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ((int)vec_size(&cfg->profiles), 2);

    config_free(cfg);
    TEST_END();
}

int test_config_ensure_local_profile_null(void)
{
    TEST_BEGIN();
    int rc = config_ensure_local_profile(NULL);
    ASSERT_EQ(rc, 0);
    TEST_END();
}

int test_config_ensure_local_profile_survives_save_load_roundtrip(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    Profile *a = config_profile_new();
    (void)snprintf(a->name, sizeof(a->name), "%s", "Server A");
    (void)snprintf(a->host, sizeof(a->host), "%s", "a.example.com");
    vec_push(&cfg->profiles, a);

    ASSERT_EQ(config_ensure_local_profile(cfg), 1);
    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ((int)vec_size(&loaded->profiles), 2);
    Profile *lp0 = (Profile *)vec_get(&loaded->profiles, 0u);
    ASSERT_NOT_NULL(lp0);
    ASSERT_STR_EQ(lp0->name, "Local shell");
    ASSERT_STR_EQ(lp0->kind, "local");

    config_free(loaded);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * Secrets: DPAPI encryption, legacy migration, foreign-blob preservation
 *
 * All of these rely on the deterministic fake DPAPI backend that
 * tests/runner.c installs for the whole run (see tests/fake_dpapi.h) --
 * real DPAPI is per-user/per-machine state a test binary cannot control.
 * ============================================================ */

int test_config_password_dpapi_encrypted_on_disk(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    Profile *p = config_profile_new();
    (void)snprintf(p->name, sizeof(p->name), "%s", "box1");
    (void)snprintf(p->host, sizeof(p->host), "%s", "example.com");
    (void)snprintf(p->password, sizeof(p->password), "%s", "hunter2");
    vec_push(&cfg->profiles, p);

    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);

    FILE *f = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(f);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, f);
    raw[n] = '\0';
    fclose(f);

    ASSERT_TRUE(strstr(raw, "hunter2") == NULL);
    ASSERT_TRUE(strstr(raw, "$dpapi$v1$") != NULL);
    ASSERT_TRUE(strstr(raw, "$aes256gcm$v1$") == NULL);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ((int)vec_size(&loaded->profiles), 1);
    Profile *lp = (Profile *)vec_get(&loaded->profiles, 0);
    ASSERT_NOT_NULL(lp);
    ASSERT_STR_EQ(lp->password, "hunter2");
    ASSERT_STR_EQ(lp->password_enc_preserved, "");
    config_free(loaded);

    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_ai_key_foreign_blob_preserved_through_load_save(void)
{
    TEST_BEGIN();
    fake_dpapi_set_identity(0x11);
    char enc[256];
    ASSERT_EQ(crypto_encrypt_dpapi("my-api-key", enc, sizeof(enc)), CRYPTO_OK);

    char json[1024];
    (void)snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\", \"ai_api_key\": \"%s\"}, "
             "\"profiles\": []}", enc);
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(json, f);
    fclose(f);

    /* Switch identity: simulates the config file arriving on another
     * PC/user, where this blob cannot be decrypted. */
    fake_dpapi_set_identity(0x22);
    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_STR_EQ(cfg->settings.ai_api_key, "");
    ASSERT_STR_EQ(cfg->settings.ai_api_key_enc_preserved, enc);

    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    /* The file on disk must carry the original blob byte-for-byte -- not
     * re-encrypted, not dropped. */
    FILE *rf = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(rf);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, rf);
    raw[n] = '\0';
    fclose(rf);
    ASSERT_TRUE(strstr(raw, enc) != NULL);

    /* Switch back to the original identity: the preserved blob still
     * decrypts -- moving the config back to its original PC/user works. */
    fake_dpapi_set_identity(0x11);
    Config *cfg2 = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg2);
    ASSERT_STR_EQ(cfg2->settings.ai_api_key, "my-api-key");
    config_free(cfg2);

    fake_dpapi_set_identity(0x42); /* restore default identity */
    remove(TMP_CFG);
    TEST_END();
}

int test_config_new_password_replaces_preserved_blob(void)
{
    TEST_BEGIN();
    fake_dpapi_set_identity(0x11);
    char enc[256];
    ASSERT_EQ(crypto_encrypt_dpapi("old-pw", enc, sizeof(enc)), CRYPTO_OK);

    char json[1024];
    (void)snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": ["
             "{\"name\": \"box1\", \"host\": \"example.com\", \"password\": \"%s\"}"
             "]}", enc);
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(json, f);
    fclose(f);

    fake_dpapi_set_identity(0x22); /* foreign: cannot decrypt old-pw */
    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->password, "");
    ASSERT_STR_EQ(p->password_enc_preserved, enc);

    /* The user enters a brand-new password. */
    (void)snprintf(p->password, sizeof(p->password), "%s", "new-pw");
    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    /* Reload (still under the identity foreign to the OLD blob): the new
     * password decrypts, and the old blob is gone -- not written back. */
    Config *reloaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(reloaded);
    Profile *rp = (Profile *)vec_get(&reloaded->profiles, 0);
    ASSERT_NOT_NULL(rp);
    ASSERT_STR_EQ(rp->password, "new-pw");
    ASSERT_STR_EQ(rp->password_enc_preserved, "");
    config_free(reloaded);

    fake_dpapi_set_identity(0x42);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_cleared_password_drops_blob(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    Profile *p = config_profile_new();
    (void)snprintf(p->name, sizeof(p->name), "%s", "box1");
    (void)snprintf(p->host, sizeof(p->host), "%s", "example.com");
    (void)snprintf(p->password, sizeof(p->password), "%s", "hunter2");
    vec_push(&cfg->profiles, p);
    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    Config *loaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(loaded);
    Profile *lp = (Profile *)vec_get(&loaded->profiles, 0);
    ASSERT_NOT_NULL(lp);
    ASSERT_STR_EQ(lp->password, "hunter2");

    /* The user clears the password field (as session_manager.c's form_read
     * would leave pr->password) and saves. */
    lp->password[0] = '\0';
    ASSERT_EQ(config_save(loaded, TMP_CFG), 0);
    config_free(loaded);

    FILE *rf = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(rf);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, rf);
    raw[n] = '\0';
    fclose(rf);
    ASSERT_TRUE(strstr(raw, "$dpapi$v1$") == NULL);
    ASSERT_TRUE(strstr(raw, "\"password\": \"\"") != NULL);

    Config *reloaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(reloaded);
    Profile *rp = (Profile *)vec_get(&reloaded->profiles, 0);
    ASSERT_NOT_NULL(rp);
    ASSERT_STR_EQ(rp->password, "");
    ASSERT_STR_EQ(rp->password_enc_preserved, "");
    config_free(reloaded);

    remove(TMP_CFG);
    TEST_END();
}

int test_config_password_legacy_migrates_to_dpapi_on_save(void)
{
    TEST_BEGIN();
    char legacy[600];
    ASSERT_EQ(crypto_encrypt("legacy-pw", legacy, sizeof(legacy)), CRYPTO_OK);
    ASSERT_TRUE(strncmp(legacy, CRYPTO_ENC_PREFIX, strlen(CRYPTO_ENC_PREFIX)) == 0);

    char json[1024];
    (void)snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": ["
             "{\"name\": \"box1\", \"host\": \"example.com\", \"password\": \"%s\"}"
             "]}", legacy);
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(json, f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ((int)vec_size(&cfg->profiles), 1);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->password, "legacy-pw");
    config_free(cfg);

    /* config_load() must have re-saved once on its own: the file should
     * now carry the DPAPI prefix, and the legacy one must be gone. */
    FILE *rf = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(rf);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, rf);
    raw[n] = '\0';
    fclose(rf);
    ASSERT_TRUE(strstr(raw, "$dpapi$v1$") != NULL);
    ASSERT_TRUE(strstr(raw, "$aes256gcm$v1$") == NULL);

    Config *reloaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(reloaded);
    Profile *rp = (Profile *)vec_get(&reloaded->profiles, 0);
    ASSERT_NOT_NULL(rp);
    ASSERT_STR_EQ(rp->password, "legacy-pw");
    config_free(reloaded);

    remove(TMP_CFG);
    TEST_END();
}

int test_config_garbage_password_blob_does_not_crash_and_is_preserved(void)
{
    TEST_BEGIN();
    const char *garbage = "$dpapi$v1$!!!not-base64-and-also-way-too-short";
    char json[512];
    (void)snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": ["
             "{\"name\": \"box1\", \"host\": \"example.com\", \"password\": \"%s\"}"
             "]}", garbage);
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(json, f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->password, "");
    ASSERT_STR_EQ(p->password_enc_preserved, garbage);

    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    Config *reloaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(reloaded);
    Profile *rp = (Profile *)vec_get(&reloaded->profiles, 0);
    ASSERT_NOT_NULL(rp);
    ASSERT_STR_EQ(rp->password, "");
    ASSERT_STR_EQ(rp->password_enc_preserved, garbage);
    config_free(reloaded);

    remove(TMP_CFG);
    TEST_END();
}

int test_config_empty_password_writes_empty_no_prefix(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    Profile *p = config_profile_new();
    (void)snprintf(p->name, sizeof(p->name), "%s", "box1");
    (void)snprintf(p->host, sizeof(p->host), "%s", "example.com");
    /* password left empty */
    vec_push(&cfg->profiles, p);
    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);

    FILE *rf = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(rf);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, rf);
    raw[n] = '\0';
    fclose(rf);
    ASSERT_TRUE(strstr(raw, "$dpapi$v1$") == NULL);
    ASSERT_TRUE(strstr(raw, "\"password\": \"\"") != NULL);

    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * M-2: a DPAPI encrypt failure at save time must abort the save --
 * never write "" and wipe the secret.
 * ============================================================ */

int test_config_save_aborts_when_dpapi_encrypt_fails(void)
{
    TEST_BEGIN();
    remove(TMP_CFG);

    Config *cfg = config_new_default();
    Profile *p = config_profile_new();
    (void)snprintf(p->name, sizeof(p->name), "%s", "box1");
    (void)snprintf(p->host, sizeof(p->host), "%s", "example.com");
    (void)snprintf(p->password, sizeof(p->password), "%s", "hunter2");
    vec_push(&cfg->profiles, p);

    fake_dpapi_set_fail_protect(1);
    int rc = config_save(cfg, TMP_CFG);
    fake_dpapi_set_fail_protect(0);

    ASSERT_TRUE(rc != 0);
    /* The save must have aborted before ever opening the temp file --
     * neither the real config nor a stray ".tmp" exists. */
    FILE *f = fopen(TMP_CFG, "r");
    ASSERT_NULL(f);
    char tmp_path[300];
    (void)snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", TMP_CFG);
    FILE *tf = fopen(tmp_path, "r");
    ASSERT_NULL(tf);

    config_free(cfg);
    TEST_END();
}

int test_config_ai_key_save_aborts_when_dpapi_encrypt_fails(void)
{
    TEST_BEGIN();
    remove(TMP_CFG);

    Config *cfg = config_new_default();
    (void)snprintf(cfg->settings.ai_api_key, sizeof(cfg->settings.ai_api_key),
                    "%s", "sk-secret");

    fake_dpapi_set_fail_protect(1);
    int rc = config_save(cfg, TMP_CFG);
    fake_dpapi_set_fail_protect(0);

    ASSERT_TRUE(rc != 0);
    FILE *f = fopen(TMP_CFG, "r");
    ASSERT_NULL(f);

    config_free(cfg);
    TEST_END();
}

int test_config_migration_resave_failure_leaves_file_untouched(void)
{
    TEST_BEGIN();
    char legacy[600];
    ASSERT_EQ(crypto_encrypt("legacy-pw", legacy, sizeof(legacy)), CRYPTO_OK);

    char json[1024];
    (void)snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": ["
             "{\"name\": \"box1\", \"host\": \"example.com\", \"password\": \"%s\"}"
             "]}", legacy);
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(json, f);
    fclose(f);

    /* config_load() must try its automatic migration re-save, have it fail,
     * and simply skip it -- not crash, and not corrupt the file it could
     * not rewrite. */
    fake_dpapi_set_fail_protect(1);
    Config *cfg = config_load(TMP_CFG);
    fake_dpapi_set_fail_protect(0);

    ASSERT_NOT_NULL(cfg);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->password, "legacy-pw");
    config_free(cfg);

    /* The file on disk must be exactly as it was: still the legacy blob,
     * not wiped, not partially rewritten. */
    FILE *rf = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(rf);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, rf);
    raw[n] = '\0';
    fclose(rf);
    ASSERT_TRUE(strstr(raw, legacy) != NULL);

    /* And now that DPAPI works again, a normal load does migrate it. */
    Config *reloaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(reloaded);
    FILE *rf2 = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(rf2);
    char raw2[4096];
    size_t n2 = fread(raw2, 1, sizeof(raw2) - 1, rf2);
    raw2[n2] = '\0';
    fclose(rf2);
    ASSERT_TRUE(strstr(raw2, "$aes256gcm$v1$") == NULL);
    ASSERT_TRUE(strstr(raw2, "$dpapi$v1$") != NULL);
    config_free(reloaded);

    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * M-3: a bare (unencrypted) secret must be flagged for migration too,
 * same as a decrypted legacy blob -- not left in the clear.
 * ============================================================ */

int test_config_bare_plaintext_password_migrates_on_load(void)
{
    TEST_BEGIN();
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(
        "{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": ["
        "{\"name\": \"box1\", \"host\": \"example.com\", \"password\": \"plain-pw\"}"
        "]}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->password, "plain-pw");
    config_free(cfg);

    /* config_load() must have re-saved once on its own: the bare value is
     * gone from disk, replaced with a DPAPI-encrypted blob. */
    FILE *rf = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(rf);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, rf);
    raw[n] = '\0';
    fclose(rf);
    ASSERT_TRUE(strstr(raw, "plain-pw") == NULL);
    ASSERT_TRUE(strstr(raw, "$dpapi$v1$") != NULL);

    remove(TMP_CFG);
    TEST_END();
}

int test_config_bare_plaintext_ai_key_migrates_on_load(void)
{
    TEST_BEGIN();
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(
        "{\"settings\": {\"font\": \"Consolas\", \"ai_api_key\": \"plain-key\"}, "
        "\"profiles\": []}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_STR_EQ(cfg->settings.ai_api_key, "plain-key");
    config_free(cfg);

    FILE *rf = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(rf);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, rf);
    raw[n] = '\0';
    fclose(rf);
    ASSERT_TRUE(strstr(raw, "plain-key") == NULL);
    ASSERT_TRUE(strstr(raw, "$dpapi$v1$") != NULL);

    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * M-4: entering a new secret value must drop any preserved foreign/
 * corrupt blob in memory, so a later clear-and-save doesn't resurrect it.
 * ============================================================ */

int test_config_secret_drop_stale_preserved_core(void)
{
    TEST_BEGIN();
    char preserved[CFG_BLOB_MAX];

    /* A non-empty new value drops the stale preserved blob. */
    (void)snprintf(preserved, sizeof(preserved), "%s", "$dpapi$v1$stale-blob");
    config_secret_drop_stale_preserved("new-value", preserved, sizeof(preserved));
    ASSERT_STR_EQ(preserved, "");

    /* An empty "new" value (nothing supplied) is a no-op. */
    (void)snprintf(preserved, sizeof(preserved), "%s", "$dpapi$v1$stale-blob");
    config_secret_drop_stale_preserved("", preserved, sizeof(preserved));
    ASSERT_STR_EQ(preserved, "$dpapi$v1$stale-blob");

    /* NULL plain is also a no-op, not a crash. */
    (void)snprintf(preserved, sizeof(preserved), "%s", "$dpapi$v1$stale-blob");
    config_secret_drop_stale_preserved(NULL, preserved, sizeof(preserved));
    ASSERT_STR_EQ(preserved, "$dpapi$v1$stale-blob");

    TEST_END();
}

/* Simulates session_manager.c's IDC_BTN_SAVE flow: an edited profile with a
 * preserved foreign blob gets a new password typed in, then (in the same
 * run, no reload) has that password cleared again -- the old blob must
 * NOT come back. */
int test_config_new_password_then_cleared_same_session_drops_blob(void)
{
    TEST_BEGIN();
    fake_dpapi_set_identity(0x11);
    char enc[256];
    ASSERT_EQ(crypto_encrypt_dpapi("old-pw", enc, sizeof(enc)), CRYPTO_OK);

    char json[1024];
    (void)snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": ["
             "{\"name\": \"box1\", \"host\": \"example.com\", \"password\": \"%s\"}"
             "]}", enc);
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(json, f);
    fclose(f);

    fake_dpapi_set_identity(0x22); /* foreign: cannot decrypt old-pw */
    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->password_enc_preserved, enc);

    /* Session 1: user types a new password. The session_manager.c/
     * settings.c fix drops the stale preserved blob at this point --
     * simulate that here at the core level. */
    (void)snprintf(p->password, sizeof(p->password), "%s", "new-pw");
    config_secret_drop_stale_preserved(p->password, p->password_enc_preserved,
                                        sizeof(p->password_enc_preserved));
    ASSERT_STR_EQ(p->password_enc_preserved, "");

    /* Session 2 (same run, no reload): user clears the password again. */
    p->password[0] = '\0';
    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    /* The old foreign blob must not have resurfaced. */
    Config *reloaded = config_load(TMP_CFG);
    ASSERT_NOT_NULL(reloaded);
    Profile *rp = (Profile *)vec_get(&reloaded->profiles, 0);
    ASSERT_NOT_NULL(rp);
    ASSERT_STR_EQ(rp->password, "");
    ASSERT_STR_EQ(rp->password_enc_preserved, "");
    config_free(reloaded);

    fake_dpapi_set_identity(0x42);
    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * A preserved blob that doesn't fit the field must be dropped, not
 * truncated (which would silently corrupt it and write it back that way).
 * ============================================================ */

int test_config_oversized_preserved_blob_is_dropped_not_truncated(void)
{
    TEST_BEGIN();
    char oversized[1200];
    memcpy(oversized, "$dpapi$v1$", 10);
    memset(oversized + 10, 'A', sizeof(oversized) - 11u);
    oversized[sizeof(oversized) - 1u] = '\0';

    char json[2048];
    (void)snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": ["
             "{\"name\": \"box1\", \"host\": \"example.com\", \"password\": \"%s\"}"
             "]}", oversized);
    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs(json, f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    Profile *p = (Profile *)vec_get(&cfg->profiles, 0);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->password, "");
    /* Dropped, not a truncated (corrupt) copy of the oversized blob. */
    ASSERT_STR_EQ(p->password_enc_preserved, "");

    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    FILE *rf = fopen(TMP_CFG, "r");
    ASSERT_NOT_NULL(rf);
    char raw[4096];
    size_t n = fread(raw, 1, sizeof(raw) - 1, rf);
    raw[n] = '\0';
    fclose(rf);
    ASSERT_TRUE(strstr(raw, "\"password\": \"\"") != NULL);

    remove(TMP_CFG);
    TEST_END();
}

/* ============================================================
 * A config file that fails to parse must be preserved under a
 * ".bad-<timestamp>" name, never silently destroyed by the caller's
 * fallback-to-defaults save.
 * ============================================================ */

int test_config_load_backs_up_unparseable_file(void)
{
    TEST_BEGIN();
    /* Start from a clean slate: other tests intentionally feed config_load()
     * unparseable content too and don't care about the resulting backup
     * (see test_config_load_invalid_json), which would otherwise make
     * "the latest backup" ambiguous here. */
    test_config_remove_all_bad_backups();

    FILE *f = test_fopen_private(TMP_CFG);
    ASSERT_NOT_NULL(f);
    fputs("this is not valid json at all {{{", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NULL(cfg);

    /* The original path no longer holds the bad content. */
    FILE *gone = fopen(TMP_CFG, "r");
    ASSERT_NULL(gone);

    /* Find the ".bad-<timestamp>" backup beside it, and check the original
     * content survived under the new name. */
    char backup_path[600];
    ASSERT_TRUE(test_config_find_latest_bad_backup(backup_path, sizeof(backup_path)));

    FILE *bf = fopen(backup_path, "r");
    ASSERT_NOT_NULL(bf);
    char raw[256];
    size_t n = fread(raw, 1, sizeof(raw) - 1, bf);
    raw[n] = '\0';
    fclose(bf);
    ASSERT_TRUE(strstr(raw, "this is not valid json") != NULL);

    test_config_remove_all_bad_backups();
    TEST_END();
}

/* ============================================================
 * config_fallback_path(): pure string helper for the absolute per-user
 * fallback location, used when the exe's own directory can't be found.
 * ============================================================ */

int test_config_fallback_path_core(void)
{
    TEST_BEGIN();
    char out[300];

    ASSERT_EQ(config_fallback_path("C:\\Users\\alice\\AppData\\Local", out, sizeof(out)), 1);
    ASSERT_STR_EQ(out, "C:\\Users\\alice\\AppData\\Local\\Nutshell\\nutshell.config");

    /* Empty or NULL local_appdata: refuse rather than build a bogus path
     * (the caller must fall back to refusing to save, never the CWD). */
    ASSERT_EQ(config_fallback_path("", out, sizeof(out)), 0);
    ASSERT_EQ(config_fallback_path(NULL, out, sizeof(out)), 0);

    /* Doesn't fit the output buffer: refuse rather than truncate into a
     * bogus (and possibly wrong-directory) path. */
    char tiny[8];
    ASSERT_EQ(config_fallback_path("C:\\Users\\alice\\AppData\\Local", tiny, sizeof(tiny)), 0);

    TEST_END();
}

