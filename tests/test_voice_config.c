#include "test_framework.h"
#include "config.h"
#include "voice_config.h"
#include "voice_lang.h"
#include "voice_gate.h"
#include "ai_gate.h"
#include "opt_feature.h"
#include "notice.h"
#include "crypto.h"
#include "crypto_dpapi.h"
#include "fake_dpapi.h"
#include "json_parser.h"
#include "settings_help.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define VCFG TEST_TMP_DIR "/nutshell_voice_test.config"

static const char *const k_voice_keys[15] = {
    "voice_enabled", "voice_stt_use_ai", "voice_stt_provider", "voice_stt_url",
    "voice_stt_model", "voice_stt_live_model", "voice_stt_language", "stt_api_key", "voice_input_device",
    "voice_input_device_name", "voice_mic_gain_db", "voice_ptt_key", "voice_ptt_mode",
    "voice_mic_delay_ms", "voice_auto_send",
};

static int vcfg_write(const char *text)
{
    FILE *f = test_fopen_private(VCFG);
    if (!f) return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

static char *vcfg_read(void)
{
    FILE *f = fopen(VCFG, "r");
    if (!f) return NULL;
    char *buf = (char *)malloc(65536);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, 65535, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int test_voice_config_defaults(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(s.voice_enabled, 0);
    ASSERT_EQ(s.voice_stt_use_ai, 1);
    ASSERT_STR_EQ(s.voice_stt_provider, "openai");
    ASSERT_STR_EQ(s.voice_stt_url, "");
    ASSERT_STR_EQ(s.voice_stt_model, "");
    ASSERT_STR_EQ(s.voice_stt_live_model, "");
    ASSERT_STR_EQ(s.voice_stt_language, "en-GB");
    ASSERT_STR_EQ(s.stt_api_key, "");
    ASSERT_STR_EQ(s.stt_api_key_enc_preserved, "");
    ASSERT_STR_EQ(s.voice_input_device, "");
    ASSERT_STR_EQ(s.voice_input_device_name, "");
    ASSERT_EQ(s.voice_mic_delay_ms, 150);
    ASSERT_EQ(s.voice_ptt_key, (int)VOICE_PTT_RIGHT_SHIFT);
    ASSERT_EQ(s.voice_ptt_mode, (int)VOICE_MODE_TOGGLE);
    ASSERT_EQ(s.voice_auto_send, 0);
    /* the defaults are the first entries, so a zeroed field is the default too */
    ASSERT_EQ((int)VOICE_PTT_RIGHT_SHIFT, 0);
    ASSERT_EQ((int)VOICE_MODE_TOGGLE, 0);
    TEST_END();
}

int test_voice_config_json_names(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_ptt_key_json(VOICE_PTT_RIGHT_SHIFT), "right_shift");
    ASSERT_STR_EQ(voice_ptt_key_json(VOICE_PTT_RIGHT_CTRL), "right_ctrl");
    ASSERT_STR_EQ(voice_ptt_key_json(VOICE_PTT_F8), "f8");
    ASSERT_STR_EQ(voice_ptt_key_json(VOICE_PTT_F9), "f9");
    ASSERT_STR_EQ(voice_ptt_key_json(-1), "right_shift");
    ASSERT_STR_EQ(voice_ptt_key_json(99), "right_shift");
    ASSERT_EQ(voice_ptt_key_from_json("right_shift"), (int)VOICE_PTT_RIGHT_SHIFT);
    ASSERT_EQ(voice_ptt_key_from_json("right_ctrl"), (int)VOICE_PTT_RIGHT_CTRL);
    ASSERT_EQ(voice_ptt_key_from_json("f8"), (int)VOICE_PTT_F8);
    ASSERT_EQ(voice_ptt_key_from_json("f9"), (int)VOICE_PTT_F9);
    ASSERT_EQ(voice_ptt_key_from_json(NULL), (int)VOICE_PTT_RIGHT_SHIFT);
    ASSERT_EQ(voice_ptt_key_from_json(""), (int)VOICE_PTT_RIGHT_SHIFT);
    ASSERT_EQ(voice_ptt_key_from_json("bogus"), (int)VOICE_PTT_RIGHT_SHIFT);
    ASSERT_STR_EQ(voice_ptt_key_label(VOICE_PTT_RIGHT_SHIFT), "Right Shift");
    ASSERT_STR_EQ(voice_ptt_key_label(VOICE_PTT_RIGHT_CTRL), "Right Ctrl");
    ASSERT_STR_EQ(voice_ptt_key_label(VOICE_PTT_F8), "F8");
    ASSERT_STR_EQ(voice_ptt_key_label(VOICE_PTT_F9), "F9");
    ASSERT_STR_EQ(voice_ptt_key_label(-5), "Right Shift");
    ASSERT_STR_EQ(voice_ptt_key_label(4), "Right Shift");
    ASSERT_STR_EQ(voice_ptt_mode_json(VOICE_MODE_HOLD), "hold");
    ASSERT_STR_EQ(voice_ptt_mode_json(VOICE_MODE_TOGGLE), "toggle");
    ASSERT_STR_EQ(voice_ptt_mode_json(-1), "toggle");
    ASSERT_STR_EQ(voice_ptt_mode_json(2), "toggle");
    ASSERT_EQ(voice_ptt_mode_from_json("hold"), (int)VOICE_MODE_HOLD);
    ASSERT_EQ(voice_ptt_mode_from_json("toggle"), (int)VOICE_MODE_TOGGLE);
    ASSERT_EQ(voice_ptt_mode_from_json(NULL), (int)VOICE_MODE_TOGGLE);
    ASSERT_EQ(voice_ptt_mode_from_json(""), (int)VOICE_MODE_TOGGLE);
    ASSERT_EQ(voice_ptt_mode_from_json("bogus"), (int)VOICE_MODE_TOGGLE);
    ASSERT_STR_EQ(voice_ptt_mode_label(VOICE_MODE_TOGGLE), "Toggle");
    ASSERT_STR_EQ(voice_ptt_mode_label(VOICE_MODE_HOLD), "Hold to talk");
    ASSERT_STR_EQ(voice_ptt_mode_label(-1), "Toggle");
    ASSERT_STR_EQ(voice_ptt_mode_label(2), "Toggle");
    TEST_END();
}

int test_voice_config_names_round_trip_and_modifier(void)
{
    TEST_BEGIN();
    for (int k = 0; k < (int)VOICE_PTT_COUNT; k++)
        ASSERT_EQ(voice_ptt_key_from_json(voice_ptt_key_json(k)), k);
    for (int m = 0; m < (int)VOICE_MODE_COUNT; m++)
        ASSERT_EQ(voice_ptt_mode_from_json(voice_ptt_mode_json(m)), m);
    ASSERT_EQ(voice_ptt_key_is_modifier(VOICE_PTT_RIGHT_SHIFT), 1);
    ASSERT_EQ(voice_ptt_key_is_modifier(VOICE_PTT_RIGHT_CTRL), 1);
    ASSERT_EQ(voice_ptt_key_is_modifier(VOICE_PTT_F8), 0);
    ASSERT_EQ(voice_ptt_key_is_modifier(VOICE_PTT_F9), 0);
    ASSERT_EQ(voice_ptt_key_is_modifier(-1), 0);
    TEST_END();
}

/* A config with no voice keys loads as Right Shift / Toggle; saved choices stay. */
int test_voice_config_load_defaults_and_saved_choices(void)
{
    TEST_BEGIN();
    remove(VCFG);
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": []}"));
    Config *cfg = config_load(VCFG);
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_EQ(cfg->settings.voice_ptt_key, (int)VOICE_PTT_RIGHT_SHIFT);
        ASSERT_EQ(cfg->settings.voice_ptt_mode, (int)VOICE_MODE_TOGGLE);
        config_free(cfg);
    }
    /* with the marker, right_ctrl + hold is a deliberate choice (without it, the
     * untouched-1.2.92 pair migrates: see test_voice_config_migrates_untouched_defaults) */
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"voice_defaults_rev\": 2, "
                           "\"voice_ptt_key\": \"right_ctrl\", "
                           "\"voice_ptt_mode\": \"hold\"}, \"profiles\": []}"));
    cfg = config_load(VCFG);
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_EQ(cfg->settings.voice_ptt_key, (int)VOICE_PTT_RIGHT_CTRL);
        ASSERT_EQ(cfg->settings.voice_ptt_mode, (int)VOICE_MODE_HOLD);
        ASSERT_EQ(config_save(cfg, VCFG), 0);
        config_free(cfg);
    }
    cfg = config_load(VCFG);
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_EQ(cfg->settings.voice_ptt_key, (int)VOICE_PTT_RIGHT_CTRL);
        ASSERT_EQ(cfg->settings.voice_ptt_mode, (int)VOICE_MODE_HOLD);
        config_free(cfg);
    }
    /* every key and mode name survives a save and load */
    for (int k = 0; k < (int)VOICE_PTT_COUNT; k++) {
        Config *c = config_new_default();
        ASSERT_NOT_NULL(c);
        if (!c) break;
        c->settings.voice_ptt_key = k;
        c->settings.voice_ptt_mode = k % (int)VOICE_MODE_COUNT;
        ASSERT_EQ(config_save(c, VCFG), 0);
        config_free(c);
        c = config_load(VCFG);
        ASSERT_NOT_NULL(c);
        if (c) {
            ASSERT_EQ(c->settings.voice_ptt_key, k);
            ASSERT_EQ(c->settings.voice_ptt_mode, k % (int)VOICE_MODE_COUNT);
            config_free(c);
        }
    }
    remove(VCFG);
    TEST_END();
}

int test_voice_config_roundtrip(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *cfg = config_new_default();
    Settings *s = &cfg->settings;
    s->voice_enabled = 1;
    s->voice_stt_use_ai = 0;
    snprintf(s->voice_stt_provider, sizeof(s->voice_stt_provider), "groq");
    snprintf(s->voice_stt_url, sizeof(s->voice_stt_url), "https://h/v1");
    snprintf(s->voice_stt_model, sizeof(s->voice_stt_model), "m");
    snprintf(s->voice_stt_live_model, sizeof(s->voice_stt_live_model), "lm");
    snprintf(s->voice_stt_language, sizeof(s->voice_stt_language), "en");
    snprintf(s->stt_api_key, sizeof(s->stt_api_key), "sk-stt");
    snprintf(s->voice_input_device, sizeof(s->voice_input_device),
             "{0.0.1.00000000}.{abc}");
    snprintf(s->voice_input_device_name, sizeof(s->voice_input_device_name),
             "Headset Microphone (USB Audio)");
    s->voice_ptt_key = VOICE_PTT_F9;
    s->voice_ptt_mode = VOICE_MODE_TOGGLE;
    s->voice_mic_delay_ms = 400;
    s->voice_mic_gain_db = 12;
    s->voice_auto_send = 1;
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);

    char *txt = vcfg_read();
    ASSERT_NOT_NULL(txt);
    if (txt) {
        ASSERT_TRUE(strstr(txt, "sk-stt") == NULL);
        ASSERT_TRUE(strstr(txt, "\"stt_api_key\": \"$dpapi$v1$") != NULL);
        free(txt);
    }

    Config *l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        const Settings *t = &l->settings;
        ASSERT_EQ(t->voice_enabled, 1);
        ASSERT_EQ(t->voice_stt_use_ai, 0);
        ASSERT_STR_EQ(t->voice_stt_provider, "groq");
        ASSERT_STR_EQ(t->voice_stt_url, "https://h/v1");
        ASSERT_STR_EQ(t->voice_stt_model, "m");
        ASSERT_STR_EQ(t->voice_stt_live_model, "lm");
        ASSERT_STR_EQ(t->voice_stt_language, "en");
        ASSERT_STR_EQ(t->stt_api_key, "sk-stt");
        ASSERT_STR_EQ(t->voice_input_device, "{0.0.1.00000000}.{abc}");
        ASSERT_STR_EQ(t->voice_input_device_name, "Headset Microphone (USB Audio)");
        ASSERT_EQ(t->voice_ptt_key, (int)VOICE_PTT_F9);
        ASSERT_EQ(t->voice_ptt_mode, (int)VOICE_MODE_TOGGLE);
        ASSERT_EQ(t->voice_mic_delay_ms, 400);
        ASSERT_EQ(t->voice_mic_gain_db, 12);
        ASSERT_EQ(t->voice_auto_send, 1);
        config_free(l);
    }
    remove(VCFG);
    TEST_END();
}

int test_voice_config_first_load_voice_off(void)
{
    TEST_BEGIN();
    fake_dpapi_set_identity(0x42);
    char enc[256];
    ASSERT_EQ(crypto_encrypt_dpapi("ai-key", enc, sizeof(enc)), CRYPTO_OK);
    char json[1024];
    snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\", \"ai_enabled\": true, "
             "\"ai_api_key\": \"%s\"}, \"profiles\": []}", enc);
    ASSERT_TRUE(vcfg_write(json));
    ConfigLoadReport rep;
    Config *cfg = config_load_report(VCFG, &rep);
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_EQ(cfg->settings.ai_enabled, 1);
        ASSERT_EQ(cfg->settings.voice_enabled, 0);
        config_free(cfg);
    }
    size_t nk = 0;
    const ConfigSettingKey *keys = config_setting_keys(&nk);
    size_t found = 0;
    for (size_t i = 0; i < rep.missing_count; i++) {
        if (found < 15u && strcmp(keys[rep.missing[i]].key, k_voice_keys[found]) == 0)
            found++;
    }
    ASSERT_EQ(found, (size_t)15);
    remove(VCFG);
    TEST_END();
}

int test_voice_config_stt_key_foreign_blob_preserved(void)
{
    TEST_BEGIN();
    fake_dpapi_set_identity(0x11);
    char enc[256];
    ASSERT_EQ(crypto_encrypt_dpapi("my-stt-key", enc, sizeof(enc)), CRYPTO_OK);
    char json[1024];
    snprintf(json, sizeof(json),
             "{\"settings\": {\"font\": \"Consolas\", \"stt_api_key\": \"%s\"}, "
             "\"profiles\": []}", enc);
    ASSERT_TRUE(vcfg_write(json));

    fake_dpapi_set_identity(0x22);
    Config *cfg = config_load(VCFG);
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_STR_EQ(cfg->settings.stt_api_key, "");
        ASSERT_STR_EQ(cfg->settings.stt_api_key_enc_preserved, enc);
        ASSERT_EQ(config_save(cfg, VCFG), 0);
        config_free(cfg);
    }
    char *raw = vcfg_read();
    ASSERT_NOT_NULL(raw);
    if (raw) {
        ASSERT_TRUE(strstr(raw, enc) != NULL);
        free(raw);
    }
    fake_dpapi_set_identity(0x11);
    Config *c2 = config_load(VCFG);
    ASSERT_NOT_NULL(c2);
    if (c2) {
        ASSERT_STR_EQ(c2->settings.stt_api_key, "my-stt-key");
        config_free(c2);
    }
    fake_dpapi_set_identity(0x42);
    remove(VCFG);
    TEST_END();
}

int test_voice_config_save_aborts_when_stt_encrypt_fails(void)
{
    TEST_BEGIN();
    remove(VCFG);
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"font\": \"Original\"}, \"profiles\": []}"));
    char *before = vcfg_read();
    ASSERT_NOT_NULL(before);

    Config *cfg = config_new_default();
    snprintf(cfg->settings.stt_api_key, sizeof(cfg->settings.stt_api_key), "sk-stt");
    fake_dpapi_set_fail_protect(1);
    int rc = config_save(cfg, VCFG);
    fake_dpapi_set_fail_protect(0);
    ASSERT_EQ(rc, -1);
    config_free(cfg);

    char *after = vcfg_read();
    ASSERT_NOT_NULL(after);
    if (before && after) ASSERT_STR_EQ(before, after);
    free(before);
    free(after);
    remove(VCFG);
    TEST_END();
}

int test_voice_config_validate_clamps(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.voice_mic_delay_ms = -1;
    settings_validate(&s);
    ASSERT_EQ(s.voice_mic_delay_ms, 0);
    s.voice_mic_delay_ms = 401;
    settings_validate(&s);
    ASSERT_EQ(s.voice_mic_delay_ms, 400);
    s.voice_mic_delay_ms = 150;
    settings_validate(&s);
    ASSERT_EQ(s.voice_mic_delay_ms, 150);
    s.voice_ptt_key = 7;
    s.voice_ptt_mode = 9;
    settings_validate(&s);
    ASSERT_EQ(s.voice_ptt_key, (int)VOICE_PTT_RIGHT_SHIFT);
    ASSERT_EQ(s.voice_ptt_mode, (int)VOICE_MODE_TOGGLE);
    snprintf(s.voice_stt_provider, sizeof(s.voice_stt_provider), "elevenlabs");
    settings_validate(&s);
    ASSERT_STR_EQ(s.voice_stt_provider, "openai");
    snprintf(s.voice_stt_provider, sizeof(s.voice_stt_provider), "custom");
    settings_validate(&s);
    ASSERT_STR_EQ(s.voice_stt_provider, "custom");
    s.voice_auto_send = 7;
    settings_validate(&s);
    ASSERT_EQ(s.voice_auto_send, 1);
    s.voice_auto_send = -3;
    settings_validate(&s);
    ASSERT_EQ(s.voice_auto_send, 1);
    s.voice_auto_send = 0;
    settings_validate(&s);
    ASSERT_EQ(s.voice_auto_send, 0);
    TEST_END();
}

int test_opt_feature_voice_rows(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const OptFeatureInfo *t = opt_feature_table(&n);
    ASSERT_EQ(n, (size_t)2);
    ASSERT_EQ((int)OPT_VOICE, 1);
    if (t && n == 2u) {
        ASSERT_EQ((size_t)t[0].id, (size_t)0);
        ASSERT_EQ((size_t)t[1].id, (size_t)1);
        ASSERT_EQ(t[1].id, OPT_VOICE);
        ASSERT_STR_EQ(t[1].name, "Voice");
        ASSERT_STR_EQ(t[1].json_key, "voice_enabled");
        ASSERT_EQ(t[1].requires, (int)OPT_AI);
    }
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(opt_feature_blocked_reason(&s, OPT_AI), "Needs the AI assistant");
    s.ai_enabled = 0; s.voice_enabled = 1;
    ASSERT_EQ(opt_feature_active(&s, OPT_VOICE), 0);
    s.ai_enabled = 1; s.voice_enabled = 0;
    ASSERT_EQ(opt_feature_active(&s, OPT_VOICE), 0);
    s.ai_enabled = 1; s.voice_enabled = 1;
    ASSERT_EQ(opt_feature_active(&s, OPT_VOICE), 1);
    ASSERT_EQ(opt_feature_active(&s, OPT_AI), 1);
    ASSERT_EQ(opt_feature_active(NULL, OPT_VOICE), 0);
    TEST_END();
}

int test_voice_gate(void)
{
    TEST_BEGIN();
    voice_gate_set_enabled(0);
    voice_gate_set_demo_override(0);
    ai_gate_set_enabled(0);
    ai_gate_set_demo_override(0);
    ASSERT_EQ(voice_gate_on(), 0);
    voice_gate_set_enabled(1);
    ASSERT_EQ(voice_gate_on(), 0);          /* AI gate off */
    ai_gate_set_enabled(1);
    ASSERT_EQ(voice_gate_on(), 1);
    voice_gate_set_enabled(0);
    ASSERT_EQ(voice_gate_on(), 0);
    voice_gate_set_demo_override(1);
    ASSERT_EQ(voice_gate_on(), 1);
    ai_gate_set_enabled(0);
    ASSERT_EQ(voice_gate_on(), 0);
    voice_gate_set_enabled(0);
    voice_gate_set_demo_override(0);
    ai_gate_set_enabled(0);
    ai_gate_set_demo_override(0);
    TEST_END();
}

static unsigned char vkey_index(const char *key)
{
    size_t n = 0;
    const ConfigSettingKey *k = config_setting_keys(&n);
    for (size_t i = 0; i < n; i++)
        if (strcmp(k[i].key, key) == 0) return (unsigned char)i;
    return 255;
}

int test_notice_upgrade_folds_voice_keys(void)
{
    TEST_BEGIN();
    Notice n;
    ConfigLoadReport r;
    Settings s;
    config_default_settings(&s);
    memset(&r, 0, sizeof(r));
    r.status = CONFIG_LOAD_OK;
    for (int i = 0; i < 15; i++) r.missing[r.missing_count++] = vkey_index(k_voice_keys[i]);
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "Voice");
    ASSERT_STR_EQ(n.items[0].value, "off until you choose to turn it on");
    TEST_END();
}

int test_notice_upgrade_lists_voice_subkey_when_switch_present(void)
{
    TEST_BEGIN();
    Notice n;
    ConfigLoadReport r;
    Settings s;
    config_default_settings(&s);
    memset(&r, 0, sizeof(r));
    r.status = CONFIG_LOAD_OK;
    r.missing[r.missing_count++] = vkey_index("voice_auto_send");
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "Voice: send automatically");
    ASSERT_STR_EQ(n.items[0].value, "off, so your words wait in the box for you to review");
    char txt[1024];
    notice_plain_text(&n, txt, sizeof txt);
    ASSERT_TRUE(strstr(txt, "- Voice: send automatically: off, so your words wait in the box for you to review") != NULL);
    /* With the voice switch missing too, the sub-key folds into "Voice". */
    r.missing[r.missing_count++] = vkey_index("voice_enabled");
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "Voice");
    TEST_END();
}

/* ---- voice_auto_send replaces voice_review: an old key is ignored ---- */

/* Saves a default config with voice on, then swaps the voice_auto_send line for
 * the old voice_review one, so every other key is present. */
static int vcfg_write_with_old_review(int old_value)
{
    Config *c = config_new_default();
    if (!c) return 0;
    c->settings.voice_enabled = 1;
    int rc = config_save(c, VCFG);
    config_free(c);
    if (rc != 0) return 0;
    char *t = vcfg_read();
    if (!t) return 0;
    char *m = strstr(t, "\"voice_auto_send\": false");
    if (!m) { free(t); return 0; }
    size_t len = strlen("\"voice_auto_send\": false");
    char *tail = strdup(m + len);
    if (!tail) { free(t); return 0; }
    snprintf(m, 65536u - (size_t)(m - t), "\"voice_review\": %s%s", old_value ? "true" : "false", tail);
    free(tail);
    int ok = vcfg_write(t);
    free(t);
    return ok;
}

static int old_review_case(int old_value)
{
    int bad = 0;
    if (!vcfg_write_with_old_review(old_value)) return 1;
    ConfigLoadReport rep;
    Config *cfg = config_load_report(VCFG, &rep);
    if (!cfg) return 1;
    if (cfg->settings.voice_auto_send != 0) bad++;
    if (cfg->settings.voice_enabled != 1) bad++;
    unsigned char want = vkey_index("voice_auto_send");
    int listed = 0;
    for (size_t i = 0; i < rep.missing_count; i++)
        if (rep.missing[i] == want) listed = 1;
    if (!listed || rep.missing_count != 1) bad++;
    if (config_save(cfg, VCFG) != 0) bad++;
    config_free(cfg);
    char *raw = vcfg_read();
    if (!raw) return bad + 1;
    if (!strstr(raw, "\"voice_auto_send\": false")) bad++;
    if (strstr(raw, "voice_review")) bad++;
    free(raw);
    remove(VCFG);
    return bad;
}

int test_voice_config_old_voice_review_ignored(void)
{
    TEST_BEGIN();
    ASSERT_EQ(old_review_case(1), 0);
    ASSERT_EQ(old_review_case(0), 0);
    TEST_END();
}

int test_voice_config_auto_send_true_loads(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"voice_enabled\": true, \"voice_auto_send\": true}, "
                           "\"profiles\": []}"));
    Config *c = config_load(VCFG);
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.voice_auto_send, 1);
        config_free(c);
    }
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"voice_auto_send\": false}, \"profiles\": []}"));
    c = config_load(VCFG);
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.voice_auto_send, 0);
        config_free(c);
    }
    remove(VCFG);
    TEST_END();
}

/* ---- Migration of untouched 1.2.92 voice defaults ---- */

#define VMARK "\"voice_defaults_rev\""

static int vcfg_has_marker(void)
{
    char *t = vcfg_read();
    int has = t && strstr(t, VMARK) != NULL;
    free(t);
    return has;
}

/* Writes a config as the loader saves it for these settings, then removes the
 * marker line, as a file saved by 1.2.92 lacks it. */
static int vcfg_write_old(int enabled, int key, int mode)
{
    Config *c = config_new_default();
    if (!c) return 0;
    c->settings.voice_enabled = enabled;
    c->settings.voice_ptt_key = key;
    c->settings.voice_ptt_mode = mode;
    int rc = config_save(c, VCFG);
    config_free(c);
    if (rc != 0) return 0;
    char *t = vcfg_read();
    if (!t) return 0;
    /* the marker is the last member: drop it and the comma before it */
    char *m = strstr(t, ",\n    " VMARK);
    if (!m) { free(t); return 0; }
    char *eol = strchr(m + 2, '\n');
    if (!eol) { free(t); return 0; }
    memmove(m, eol, strlen(eol) + 1);
    int ok = vcfg_write(t);
    free(t);
    return ok;
}

static void vcfg_expect(int key, int mode, int *failed_out)
{
    Config *c = config_load(VCFG);
    if (!c) { (*failed_out)++; return; }
    if (c->settings.voice_ptt_key != key || c->settings.voice_ptt_mode != mode) (*failed_out)++;
    config_free(c);
}

int test_voice_config_migrates_untouched_defaults(void)
{
    TEST_BEGIN();
    remove(VCFG);
    ASSERT_TRUE(vcfg_write_old(0, VOICE_PTT_RIGHT_CTRL, VOICE_MODE_HOLD));
    ASSERT_EQ(vcfg_has_marker(), 0);
    char *before = vcfg_read();
    ASSERT_NOT_NULL(before);
    ConfigLoadReport rep;
    Config *c = config_load_report(VCFG, &rep);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(rep.voice_migrated, 1);
    ASSERT_EQ(rep.missing_count, 0);
    if (c) {
        ASSERT_EQ(c->settings.voice_ptt_key, (int)VOICE_PTT_RIGHT_SHIFT);
        ASSERT_EQ(c->settings.voice_ptt_mode, (int)VOICE_MODE_TOGGLE);
        /* the loader saves nothing (--ui-demo and read-only callers load the
         * real config): the file is byte-for-byte as it was */
        char *after = vcfg_read();
        ASSERT_NOT_NULL(after);
        if (before && after) ASSERT_STR_EQ(after, before);
        free(after);
        ASSERT_EQ(vcfg_has_marker(), 0);
        /* it applies again on every load until a save writes the marker */
        Config *c2 = config_load(VCFG);
        ASSERT_NOT_NULL(c2);
        if (c2) {
            ASSERT_EQ(c2->settings.voice_ptt_key, (int)VOICE_PTT_RIGHT_SHIFT);
            config_free(c2);
        }
        /* the launch path asks for a save, except in demo mode */
        ASSERT_EQ(notice_launch_save_wanted(0, rep.voice_migrated, 0), 1);
        ASSERT_EQ(notice_launch_save_wanted(1, rep.voice_migrated, 0), 0);
        ASSERT_EQ(config_save(c, VCFG), 0);
        config_free(c);
    }
    free(before);
    ASSERT_EQ(vcfg_has_marker(), 1);
    Config *d = config_load_report(VCFG, &rep);
    ASSERT_NOT_NULL(d);
    ASSERT_EQ(rep.voice_migrated, 0);
    if (d) {
        ASSERT_EQ(d->settings.voice_ptt_key, (int)VOICE_PTT_RIGHT_SHIFT);
        ASSERT_EQ(d->settings.voice_ptt_mode, (int)VOICE_MODE_TOGGLE);
        config_free(d);
    }
    remove(VCFG);
    TEST_END();
}

int test_voice_config_migration_leaves_other_choices(void)
{
    TEST_BEGIN();
    int bad = 0;
    remove(VCFG);
    /* voice on */
    ASSERT_TRUE(vcfg_write_old(1, VOICE_PTT_RIGHT_CTRL, VOICE_MODE_HOLD));
    vcfg_expect(VOICE_PTT_RIGHT_CTRL, VOICE_MODE_HOLD, &bad);
    /* a non-default key or mode */
    ASSERT_TRUE(vcfg_write_old(0, VOICE_PTT_F8, VOICE_MODE_HOLD));
    vcfg_expect(VOICE_PTT_F8, VOICE_MODE_HOLD, &bad);
    ASSERT_TRUE(vcfg_write_old(0, VOICE_PTT_RIGHT_CTRL, VOICE_MODE_TOGGLE));
    vcfg_expect(VOICE_PTT_RIGHT_CTRL, VOICE_MODE_TOGGLE, &bad);
    ASSERT_TRUE(vcfg_write_old(0, VOICE_PTT_RIGHT_SHIFT, VOICE_MODE_HOLD));
    vcfg_expect(VOICE_PTT_RIGHT_SHIFT, VOICE_MODE_HOLD, &bad);
    ASSERT_EQ(bad, 0);
    /* the marker present: right_ctrl + hold is a deliberate choice */
    Config *c = config_new_default();
    ASSERT_NOT_NULL(c);
    if (c) {
        c->settings.voice_ptt_key = VOICE_PTT_RIGHT_CTRL;
        c->settings.voice_ptt_mode = VOICE_MODE_HOLD;
        ASSERT_EQ(config_save(c, VCFG), 0);
        config_free(c);
    }
    ASSERT_EQ(vcfg_has_marker(), 1);
    vcfg_expect(VOICE_PTT_RIGHT_CTRL, VOICE_MODE_HOLD, &bad);
    vcfg_expect(VOICE_PTT_RIGHT_CTRL, VOICE_MODE_HOLD, &bad);   /* and after a reload */
    ASSERT_EQ(bad, 0);
    /* a file with no voice keys at all is not the old pair */
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": []}"));
    vcfg_expect(VOICE_PTT_RIGHT_SHIFT, VOICE_MODE_TOGGLE, &bad);
    ASSERT_EQ(bad, 0);
    remove(VCFG);
    TEST_END();
}

int test_voice_config_new_config_writes_marker(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *c = config_new_default();
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(c->settings.voice_ptt_key, (int)VOICE_PTT_RIGHT_SHIFT);
        ASSERT_EQ(c->settings.voice_ptt_mode, (int)VOICE_MODE_TOGGLE);
        ASSERT_EQ(config_save(c, VCFG), 0);
        config_free(c);
    }
    ASSERT_EQ(vcfg_has_marker(), 1);
    char *t = vcfg_read();
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_TRUE(strstr(t, VMARK ": 3") != NULL);
        free(t);
    }
    remove(VCFG);
    TEST_END();
}

/* The marker is internal: a config complete except for it reports nothing
 * missing, builds no upgrade notice, and the marker is in no notice label. */
int test_voice_config_marker_is_not_a_notice_key(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const ConfigSettingKey *keys = config_setting_keys(&n);
    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(strcmp(keys[i].key, "voice_defaults_rev") != 0);
        ASSERT_TRUE(!keys[i].label || strstr(keys[i].label, "defaults_rev") == NULL);
    }
    remove(VCFG);
    ASSERT_TRUE(vcfg_write_old(0, VOICE_PTT_RIGHT_CTRL, VOICE_MODE_HOLD));
    ConfigLoadReport rep;
    Config *c = config_load_report(VCFG, &rep);
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(rep.missing_count, 0);
        Notice nt;
        ASSERT_EQ(notice_upgrade(&nt, "1.2.93", &rep, &c->settings), 0);
        config_free(c);
    }
    remove(VCFG);
    TEST_END();
}

int test_voice_gain_default_and_clamp(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(s.voice_mic_gain_db, 0);
    ASSERT_EQ(voice_gain_clamp(99), 30);
    ASSERT_EQ(voice_gain_clamp(-50), -12);
    ASSERT_EQ(voice_gain_clamp(7), 7);
    s.voice_mic_gain_db = 99;
    voice_settings_validate(&s);
    ASSERT_EQ(s.voice_mic_gain_db, 30);
    s.voice_mic_gain_db = -50;
    voice_settings_validate(&s);
    ASSERT_EQ(s.voice_mic_gain_db, -12);
    TEST_END();
}

int test_voice_gain_roundtrip(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *cfg = config_new_default();
    cfg->settings.voice_mic_gain_db = 12;
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);
    Config *l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        ASSERT_EQ(l->settings.voice_mic_gain_db, 12);
        config_free(l);
    }
    remove(VCFG);
    TEST_END();
}

int test_voice_gain_label(void)
{
    TEST_BEGIN();
    char b[16];
    voice_gain_label(12, b, sizeof b);  ASSERT_STR_EQ(b, "+12 dB");
    voice_gain_label(0, b, sizeof b);   ASSERT_STR_EQ(b, "0 dB");
    voice_gain_label(-12, b, sizeof b); ASSERT_STR_EQ(b, "-12 dB");
    voice_gain_label(30, b, sizeof b);  ASSERT_STR_EQ(b, "+30 dB");
    TEST_END();
}

/* ---- Language: custom code round trip and the one-time English (UK) migration ---- */

int test_voice_config_custom_language_roundtrip(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *c = config_new_default();
    ASSERT_NOT_NULL(c);
    if (!c) return 1;
    ASSERT_STR_EQ(c->settings.voice_stt_language, VOICE_LANG_DEFAULT);
    snprintf(c->settings.voice_stt_language, sizeof(c->settings.voice_stt_language), "cy");
    ASSERT_EQ(config_save(c, VCFG), 0);
    config_free(c);
    char *t = vcfg_read();
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_TRUE(strstr(t, VMARK ": 3") != NULL);
        free(t);
    }
    ConfigLoadReport rep;
    Config *l = config_load_report(VCFG, &rep);
    ASSERT_NOT_NULL(l);
    if (l) {
        ASSERT_STR_EQ(l->settings.voice_stt_language, "cy");
        ASSERT_EQ(rep.voice_migrated, 0);
        config_free(l);
    }
    remove(VCFG);
    TEST_END();
}

/* Loads {"settings": {rev?, language, key, mode}}; returns the report's
 * voice_migrated and copies the loaded language. */
static int lang_fixture(const char *rev, const char *lang, const char *key,
                        const char *mode, char *lang_out, size_t cap, int *key_out)
{
    char json[512];
    snprintf(json, sizeof json,
             "{\"settings\": {%s\"voice_stt_language\": \"%s\", "
             "\"voice_ptt_key\": \"%s\", \"voice_ptt_mode\": \"%s\"}, \"profiles\": []}",
             rev, lang, key, mode);
    if (!vcfg_write(json)) return -1;
    ConfigLoadReport rep;
    Config *c = config_load_report(VCFG, &rep);
    if (!c) return -1;
    snprintf(lang_out, cap, "%s", c->settings.voice_stt_language);
    if (key_out) *key_out = c->settings.voice_ptt_key;
    config_free(c);
    return rep.voice_migrated;
}

int test_voice_config_language_migration(void)
{
    TEST_BEGIN();
    char lang[32];
    /* marker absent, empty: becomes English (UK) and is reported */
    ASSERT_EQ(lang_fixture("", "", "right_shift", "toggle", lang, sizeof lang, NULL), 1);
    ASSERT_STR_EQ(lang, "en-GB");
    /* rev 2, empty: the same (the Right Shift migration does not apply) */
    ASSERT_EQ(lang_fixture("\"voice_defaults_rev\": 2, ", "", "right_shift", "toggle",
                           lang, sizeof lang, NULL), 1);
    ASSERT_STR_EQ(lang, "en-GB");
    /* rev 3, empty: a deliberate Auto-detect, kept */
    ASSERT_EQ(lang_fixture("\"voice_defaults_rev\": 3, ", "", "right_shift", "toggle",
                           lang, sizeof lang, NULL), 0);
    ASSERT_STR_EQ(lang, "");
    /* a chosen language is never touched */
    ASSERT_EQ(lang_fixture("\"voice_defaults_rev\": 2, ", "fr", "right_shift", "toggle",
                           lang, sizeof lang, NULL), 0);
    ASSERT_STR_EQ(lang, "fr");
    ASSERT_EQ(lang_fixture("\"voice_defaults_rev\": 2, ", "en", "right_shift", "toggle",
                           lang, sizeof lang, NULL), 0);
    ASSERT_STR_EQ(lang, "en");
    ASSERT_EQ(lang_fixture("", "fr", "right_shift", "toggle", lang, sizeof lang, NULL), 0);
    ASSERT_STR_EQ(lang, "fr");
    /* a migrated config saved once is not migrated again */
    Config *c = config_load(VCFG);
    ASSERT_NOT_NULL(c);
    if (c) { ASSERT_EQ(config_save(c, VCFG), 0); config_free(c); }
    remove(VCFG);
    TEST_END();
}

int test_voice_config_rshift_migration_rev2_skips(void)
{
    TEST_BEGIN();
    char lang[32];
    int key = -1;
    /* rev 2: Right Ctrl + Hold is a deliberate choice and stays */
    lang_fixture("\"voice_defaults_rev\": 2, ", "fr", "right_ctrl", "hold", lang, sizeof lang, &key);
    ASSERT_EQ(key, (int)VOICE_PTT_RIGHT_CTRL);
    /* rev 3 too */
    lang_fixture("\"voice_defaults_rev\": 3, ", "fr", "right_ctrl", "hold", lang, sizeof lang, &key);
    ASSERT_EQ(key, (int)VOICE_PTT_RIGHT_CTRL);
    /* absent: the old pair still migrates to Right Shift */
    lang_fixture("", "fr", "right_ctrl", "hold", lang, sizeof lang, &key);
    ASSERT_EQ(key, (int)VOICE_PTT_RIGHT_SHIFT);
    remove(VCFG);
    TEST_END();
}

/* A marker that is not a small integer must not be cast to int (UB for NaN or
 * 1e30): huge counts as current (no migration), negative as before rev 2. */
int test_voice_config_marker_extremes(void)
{
    TEST_BEGIN();
    char lang[32];
    int key = -1;
    ASSERT_EQ(lang_fixture("\"voice_defaults_rev\": 1e30, ", "", "right_ctrl", "hold",
                           lang, sizeof lang, &key), 0);
    ASSERT_STR_EQ(lang, "");
    ASSERT_EQ(key, (int)VOICE_PTT_RIGHT_CTRL);
    ASSERT_EQ(lang_fixture("\"voice_defaults_rev\": -5, ", "", "right_ctrl", "hold",
                           lang, sizeof lang, &key), 1);
    ASSERT_STR_EQ(lang, "en-GB");
    ASSERT_EQ(key, (int)VOICE_PTT_RIGHT_SHIFT);
    remove(VCFG);
    TEST_END();
}

/* ---- the English (UK) migration is told to the user, in plain words ---- */

#define LANG_LABEL "Voice: language"
#define LANG_VALUE "English (UK) (choose Auto-detect in Settings > Voice if you speak another language)"

int test_voice_config_language_migration_notice(void)
{
    TEST_BEGIN();
    char lang[32];
    ConfigLoadReport rep;
    /* fires: report flag set, one notice item in plain words */
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"voice_stt_language\": \"\"}, \"profiles\": []}"));
    Config *c = config_load_report(VCFG, &rep);
    ASSERT_NOT_NULL(c);
    if (c) config_free(c);
    ASSERT_EQ(rep.voice_lang_migrated, 1);
    Settings st;
    config_default_settings(&st);
    Notice n;
    ConfigLoadReport only;
    memset(&only, 0, sizeof only);
    only.status = CONFIG_LOAD_OK;
    only.voice_lang_migrated = 1;
    ASSERT_EQ(only.missing_count, 0);
    ASSERT_EQ(notice_upgrade(&n, "1.2.94", &only, &st), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, LANG_LABEL);
    ASSERT_STR_EQ(n.items[0].value, LANG_VALUE);
    char txt[1024];
    notice_plain_text(&n, txt, sizeof txt);
    ASSERT_TRUE(strstr(txt, "- " LANG_LABEL ": " LANG_VALUE) != NULL);
    ASSERT_TRUE(strstr(txt, "defaults_rev") == NULL);
    /* alongside another missing key: both lines */
    only.missing[only.missing_count++] = vkey_index("voice_mic_gain_db");
    ASSERT_EQ(notice_upgrade(&n, "1.2.94", &only, &st), 1);
    ASSERT_EQ(n.item_count, 2);
    /* a launch that saved queues the notice although no key is missing */
    memset(&only, 0, sizeof only);
    only.status = CONFIG_LOAD_OK;
    only.voice_lang_migrated = 1;
    NoticeQueue q;
    notice_queue_init(&q);
    NoticeLaunch nl;
    memset(&nl, 0, sizeof nl);
    nl.status = CONFIG_LOAD_OK;
    nl.rep = &only;
    nl.saved = 1;
    nl.save_ok = 1;
    nl.version = "1.2.94";
    nl.s = &st;
    notice_launch_plan(&q, &nl);
    ASSERT_EQ(q.count, 1);
    /* with the voice switch missing (a pre-voice config) it folds into "Voice" */
    only.missing[only.missing_count++] = vkey_index("voice_enabled");
    ASSERT_EQ(notice_upgrade(&n, "1.2.94", &only, &st), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "Voice");

    /* does not fire: rev 3, or a chosen language (the helper reports the flag
     * through voice_migrated, which also covers the Right Shift move: use
     * keys that rule that one out) */
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"voice_defaults_rev\": 3, \"voice_stt_language\": \"\"}, \"profiles\": []}"));
    c = config_load_report(VCFG, &rep);
    if (c) config_free(c);
    ASSERT_EQ(rep.voice_lang_migrated, 0);
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"voice_defaults_rev\": 2, \"voice_stt_language\": \"fr\"}, \"profiles\": []}"));
    c = config_load_report(VCFG, &rep);
    if (c) config_free(c);
    ASSERT_EQ(rep.voice_lang_migrated, 0);
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": []}"));
    c = config_load_report(VCFG, &rep);   /* no language key: the default is already en-GB */
    if (c) config_free(c);
    ASSERT_EQ(rep.voice_lang_migrated, 0);
    (void)lang;
    remove(VCFG);
    TEST_END();
}

/* ---- live dictation: the live model setting and the settings digest ---- */

int test_voice_live_model_default_and_roundtrip(void)
{
    TEST_BEGIN();
    Settings d;
    config_default_settings(&d);
    ASSERT_STR_EQ(d.voice_stt_live_model, "");

    remove(VCFG);
    Config *cfg = config_new_default();
    snprintf(cfg->settings.voice_stt_live_model, sizeof(cfg->settings.voice_stt_live_model),
             "gpt-live-transcribe");
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);
    Config *l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        ASSERT_STR_EQ(l->settings.voice_stt_live_model, "gpt-live-transcribe");
        config_free(l);
    }
    remove(VCFG);

    /* a name with a line break is not a model id: validation empties it */
    Settings s;
    config_default_settings(&s);
    snprintf(s.voice_stt_live_model, sizeof(s.voice_stt_live_model), "gpt\r\nx");
    settings_validate(&s);
    ASSERT_STR_EQ(s.voice_stt_live_model, "");
    snprintf(s.voice_stt_live_model, sizeof(s.voice_stt_live_model), "gpt\nx");
    settings_validate(&s);
    ASSERT_STR_EQ(s.voice_stt_live_model, "");
    snprintf(s.voice_stt_live_model, sizeof(s.voice_stt_live_model), "voxtral-mini-transcribe-realtime-3");
    settings_validate(&s);
    ASSERT_STR_EQ(s.voice_stt_live_model, "voxtral-mini-transcribe-realtime-3");
    TEST_END();
}

int test_voice_live_model_missing_key_reported(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *cfg = config_new_default();
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);
    char *txt = vcfg_read();
    ASSERT_NOT_NULL(txt);
    if (!txt) { TEST_END(); }
    char *p = strstr(txt, "    \"voice_stt_live_model\":");
    ASSERT_NOT_NULL(p);   /* the save writes the key */
    if (p) {
        char *e = strchr(p, '\n');
        memmove(p, e + 1, strlen(e + 1) + 1);
    }
    ASSERT_TRUE(vcfg_write(txt));
    free(txt);

    ConfigLoadReport rep;
    Config *l = config_load_report(VCFG, &rep);
    ASSERT_NOT_NULL(l);
    if (l) config_free(l);
    ASSERT_EQ(rep.missing_count, (size_t)1);
    unsigned char idx = vkey_index("voice_stt_live_model");
    ASSERT_TRUE(idx != 255);
    ASSERT_EQ(rep.missing[0], idx);

    Notice n;
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(notice_upgrade(&n, "1.2.95", &rep, &s), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "Voice: live speech-to-text model");
    ASSERT_STR_EQ(n.items[0].value, "the provider's default");

    /* with the voice switch missing too it folds into "Voice" */
    rep.missing[rep.missing_count++] = vkey_index("voice_enabled");
    ASSERT_EQ(notice_upgrade(&n, "1.2.95", &rep, &s), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "Voice");
    remove(VCFG);
    TEST_END();
}

int test_voice_settings_digest(void)
{
    TEST_BEGIN();
    Settings a, b;
    config_default_settings(&a);
    config_default_settings(&b);
    ASSERT_TRUE(voice_settings_digest(&a) == voice_settings_digest(&b));
    ASSERT_TRUE(voice_settings_digest(&a) != 0);

    const uint64_t base = voice_settings_digest(&a);
    snprintf(b.voice_stt_provider, sizeof(b.voice_stt_provider), "mistral");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.voice_stt_live_model, sizeof(b.voice_stt_live_model), "x");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    b.voice_ptt_key = (int)VOICE_PTT_F9;
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.voice_input_device, sizeof(b.voice_input_device), "{dev}");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.stt_api_key, sizeof(b.stt_api_key), "k");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    b.voice_stt_use_ai = 0;
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.ai_provider, sizeof(b.ai_provider), "zz-other");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.ai_api_key, sizeof(b.ai_api_key), "ak");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.ai_custom_url, sizeof(b.ai_custom_url), "https://h/v1");
    ASSERT_TRUE(voice_settings_digest(&b) != base);

    /* a string boundary is part of the hash: ("ab","c") is not ("a","bc") */
    config_default_settings(&a);
    config_default_settings(&b);
    snprintf(a.voice_stt_url, sizeof(a.voice_stt_url), "ab");
    snprintf(a.voice_stt_model, sizeof(a.voice_stt_model), "c");
    snprintf(b.voice_stt_url, sizeof(b.voice_stt_url), "a");
    snprintf(b.voice_stt_model, sizeof(b.voice_stt_model), "bc");
    ASSERT_TRUE(voice_settings_digest(&a) != voice_settings_digest(&b));

    /* a non-voice field does not move it */
    config_default_settings(&b);
    b.font_size = b.font_size + 3;
    ASSERT_TRUE(voice_settings_digest(&b) == base);
    TEST_END();
}

/* ---- PR 2: conversation mode settings ---------------------------------- */

static void conv_drop_line(char *txt, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "    \"%s\":", key);
    char *p = strstr(txt, pat);
    if (p) {
        char *e = strchr(p, '\n');
        memmove(p, e + 1, strlen(e + 1) + 1);
    }
}

int test_voice_conv_settings_defaults(void)
{
    TEST_BEGIN();
    Settings d;
    config_default_settings(&d);
    ASSERT_EQ(d.voice_mode, VOICE_MODE_DICTATION);
    ASSERT_EQ(d.voice_pause_ms, 1200);
    ASSERT_STR_EQ(d.voice_windows_voice, "");
    ASSERT_STR_EQ(d.voice_windows_voice_name, "");
    /* a file without the four keys loads with the same values */
    remove(VCFG);
    Config *cfg = config_new_default();
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);
    char *txt = vcfg_read();
    ASSERT_NOT_NULL(txt);
    if (!txt) { TEST_END(); }
    conv_drop_line(txt, "voice_mode");
    conv_drop_line(txt, "voice_pause_ms");
    conv_drop_line(txt, "voice_windows_voice");
    conv_drop_line(txt, "voice_windows_voice_name");
    ASSERT_TRUE(strstr(txt, "voice_pause_ms") == NULL);
    ASSERT_TRUE(vcfg_write(txt));
    free(txt);
    Config *l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        ASSERT_EQ(l->settings.voice_mode, VOICE_MODE_DICTATION);
        ASSERT_EQ(l->settings.voice_pause_ms, 1200);
        ASSERT_STR_EQ(l->settings.voice_windows_voice, "");
        ASSERT_STR_EQ(l->settings.voice_windows_voice_name, "");
        config_free(l);
    }
    remove(VCFG);
    TEST_END();
}

int test_voice_conv_settings_roundtrip(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *cfg = config_new_default();
    cfg->settings.voice_mode = VOICE_MODE_CONVERSATION;
    cfg->settings.voice_pause_ms = 2300;
    snprintf(cfg->settings.voice_windows_voice, sizeof cfg->settings.voice_windows_voice,
             "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\TTS_MS_EN-GB_HAZEL_11.0");
    snprintf(cfg->settings.voice_windows_voice_name, sizeof cfg->settings.voice_windows_voice_name,
             "Microsoft Hazel Desktop - English (Great Britain)");
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);
    char *txt = vcfg_read();
    ASSERT_NOT_NULL(txt);
    if (txt) {
        ASSERT_TRUE(strstr(txt, "\"voice_mode\": \"conversation\"") != NULL);
        free(txt);
    }
    Config *l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        ASSERT_EQ(l->settings.voice_mode, VOICE_MODE_CONVERSATION);
        ASSERT_EQ(l->settings.voice_pause_ms, 2300);
        ASSERT_TRUE(strstr(l->settings.voice_windows_voice, "TTS_MS_EN-GB_HAZEL_11.0") != NULL);
        ASSERT_STR_EQ(l->settings.voice_windows_voice_name,
                      "Microsoft Hazel Desktop - English (Great Britain)");
        config_free(l);
    }
    remove(VCFG);
    TEST_END();
}

/* Saves a default config, replaces the first occurrence of `from` with `to`,
 * loads the result. */
static Config *conv_load_edited(const char *from, const char *to)
{
    remove(VCFG);
    Config *cfg = config_new_default();
    config_save(cfg, VCFG);
    config_free(cfg);
    char *txt = vcfg_read();
    if (!txt) return NULL;
    char *p = strstr(txt, from);
    char *out = (char *)malloc(70000);
    if (!p || !out) { free(txt); free(out); return NULL; }
    snprintf(out, 70000, "%.*s%s%s", (int)(p - txt), txt, to, p + strlen(from));
    int ok = vcfg_write(out);
    free(txt);
    free(out);
    return ok ? config_load(VCFG) : NULL;
}

int test_voice_conv_settings_clamp(void)
{
    TEST_BEGIN();
    Config *l = conv_load_edited("\"voice_pause_ms\": 1200", "\"voice_pause_ms\": 100");
    ASSERT_NOT_NULL(l);
    if (l) { ASSERT_EQ(l->settings.voice_pause_ms, 500); config_free(l); }
    l = conv_load_edited("\"voice_pause_ms\": 1200", "\"voice_pause_ms\": 9000");
    ASSERT_NOT_NULL(l);
    if (l) { ASSERT_EQ(l->settings.voice_pause_ms, 3000); config_free(l); }
    l = conv_load_edited("\"voice_mode\": \"dictation\"", "\"voice_mode\": \"shouting\"");
    ASSERT_NOT_NULL(l);
    if (l) { ASSERT_EQ(l->settings.voice_mode, VOICE_MODE_DICTATION); config_free(l); }
    l = conv_load_edited("\"voice_mode\": \"dictation\"", "\"voice_mode\": \"conversation\"");
    ASSERT_NOT_NULL(l);
    if (l) { ASSERT_EQ(l->settings.voice_mode, VOICE_MODE_CONVERSATION); config_free(l); }

    /* saved rounded to the nearest 100: 1249 -> 1200, 1250 -> 1300 */
    static const int in[] = { 1249, 1250, 500, 3000 };
    static const int want[] = { 1200, 1300, 500, 3000 };
    for (int i = 0; i < 4; i++) {
        remove(VCFG);
        Config *cfg = config_new_default();
        cfg->settings.voice_pause_ms = in[i];
        ASSERT_EQ(config_save(cfg, VCFG), 0);
        config_free(cfg);
        char *txt = vcfg_read();
        ASSERT_NOT_NULL(txt);
        if (txt) {
            char pat[48];
            snprintf(pat, sizeof pat, "\"voice_pause_ms\": %d,", want[i]);
            ASSERT_TRUE(strstr(txt, pat) != NULL);
            free(txt);
        }
    }
    /* validate clamps an in-memory value too */
    Settings s;
    config_default_settings(&s);
    s.voice_pause_ms = 7;
    s.voice_mode = 9;
    settings_validate(&s);
    ASSERT_EQ(s.voice_pause_ms, VOICE_PAUSE_MIN_MS);
    ASSERT_EQ(s.voice_mode, VOICE_MODE_DICTATION);
    remove(VCFG);
    TEST_END();
}

int test_voice_conv_settings_notice_lines(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *cfg = config_new_default();
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);
    char *txt = vcfg_read();
    ASSERT_NOT_NULL(txt);
    if (!txt) { TEST_END(); }
    conv_drop_line(txt, "voice_mode");
    conv_drop_line(txt, "voice_pause_ms");
    conv_drop_line(txt, "voice_windows_voice");
    conv_drop_line(txt, "voice_windows_voice_name");
    ASSERT_TRUE(vcfg_write(txt));
    free(txt);
    ConfigLoadReport rep;
    Config *l = config_load_report(VCFG, &rep);
    ASSERT_NOT_NULL(l);
    if (l) config_free(l);
    ASSERT_EQ(rep.missing_count, (size_t)4);
    Notice n;
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(notice_upgrade(&n, "1.2.96", &rep, &s), 1);
    ASSERT_EQ(n.item_count, 4);
    if (n.item_count == 4) {
        ASSERT_STR_EQ(n.items[0].label, "Voice: mode");
        ASSERT_STR_EQ(n.items[0].value,
                      "Dictation, so your words wait in the box for you to press Send");
        ASSERT_STR_EQ(n.items[1].label, "Voice: pause before sending");
        ASSERT_STR_EQ(n.items[1].value, "1.2 seconds");
        ASSERT_STR_EQ(n.items[2].label, "Voice: Windows voice");
        ASSERT_STR_EQ(n.items[2].value,
                      "Automatic (an English (UK) woman's voice if one is installed)");
        ASSERT_STR_EQ(n.items[3].label, "Voice: Windows voice name");
        ASSERT_STR_EQ(n.items[3].value, "none, as Automatic is used");
    }
    /* with the voice switch missing too they fold into the one "Voice" line */
    rep.missing[rep.missing_count++] = vkey_index("voice_enabled");
    ASSERT_EQ(notice_upgrade(&n, "1.2.96", &rep, &s), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "Voice");
    remove(VCFG);
    TEST_END();
}

int test_voice_conv_settings_digest(void)
{
    TEST_BEGIN();
    Settings a, b;
    config_default_settings(&a);
    const uint64_t base = voice_settings_digest(&a);
    config_default_settings(&b);
    b.voice_mode = VOICE_MODE_CONVERSATION;
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    b.voice_pause_ms = 1300;
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.voice_windows_voice, sizeof b.voice_windows_voice, "tok");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.voice_windows_voice_name, sizeof b.voice_windows_voice_name, "nm");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    TEST_END();
}

int test_voice_conv_auto_send_unchanged(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *cfg = config_new_default();
    cfg->settings.voice_auto_send = 1;
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);
    char *txt = vcfg_read();
    ASSERT_NOT_NULL(txt);
    if (txt) {
        ASSERT_TRUE(strstr(txt, "\"voice_auto_send\": true,") != NULL);
        ASSERT_TRUE(strstr(txt, "\"voice_defaults_rev\": 3\n") != NULL);
        free(txt);
    }
    Config *l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) { ASSERT_EQ(l->settings.voice_auto_send, 1); config_free(l); }
    /* the missing-key notice for voice_auto_send is still its own wording */
    Config *c2 = config_new_default();
    ASSERT_EQ(config_save(c2, VCFG), 0);
    config_free(c2);
    txt = vcfg_read();
    ASSERT_NOT_NULL(txt);
    if (!txt) { TEST_END(); }
    conv_drop_line(txt, "voice_auto_send");
    ASSERT_TRUE(vcfg_write(txt));
    free(txt);
    ConfigLoadReport rep;
    Config *l2 = config_load_report(VCFG, &rep);
    if (l2) config_free(l2);
    ASSERT_EQ(rep.missing_count, (size_t)1);
    Notice n;
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(notice_upgrade(&n, "1.2.96", &rep, &s), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "Voice: send automatically");
    ASSERT_STR_EQ(n.items[0].value, "off, so your words wait in the box for you to review");
    remove(VCFG);
    TEST_END();
}

/* Task 14: the Pause before sending entry and the Mode greying. */
int test_voice_pause_parse(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_pause_parse("1.2", 1700), 1200);
    ASSERT_EQ(voice_pause_parse("0.3", 1700), 500);      /* clamped up */
    ASSERT_EQ(voice_pause_parse("9", 1700), 3000);       /* clamped down */
    ASSERT_EQ(voice_pause_parse("3.0", 1700), 3000);
    ASSERT_EQ(voice_pause_parse("0.5", 1700), 500);
    ASSERT_EQ(voice_pause_parse("2", 1700), 2000);       /* no decimals */
    ASSERT_EQ(voice_pause_parse(" 1.5 ", 1700), 1500);   /* spaces */
    ASSERT_EQ(voice_pause_parse("1,5", 1700), 1500);     /* decimal comma */
    ASSERT_EQ(voice_pause_parse("1.25", 1700), 1300);    /* nearest 100, halves up */
    ASSERT_EQ(voice_pause_parse("abc", 1700), 1700);     /* keep */
    ASSERT_EQ(voice_pause_parse("", 1700), 1700);
    ASSERT_EQ(voice_pause_parse(NULL, 1700), 1700);
    ASSERT_EQ(voice_pause_parse("1.2x", 1700), 1700);
    ASSERT_EQ(voice_pause_parse("-1", 1700), 1700);
    ASSERT_EQ(voice_pause_parse(".", 1700), 1700);
    ASSERT_EQ(voice_pause_parse("99999999999999999999", 1700), 3000);
    TEST_END();
}

int test_voice_pause_format(void)
{
    TEST_BEGIN();
    char b[16];
    voice_pause_format(1200, b, sizeof(b));
    ASSERT_STR_EQ(b, "1.2");
    voice_pause_format(3000, b, sizeof(b));
    ASSERT_STR_EQ(b, "3.0");
    voice_pause_format(500, b, sizeof(b));
    ASSERT_STR_EQ(b, "0.5");
    voice_pause_format(50, b, sizeof(b));    /* clamped */
    ASSERT_STR_EQ(b, "0.5");
    voice_pause_format(1250, b, sizeof(b));  /* rounded */
    ASSERT_STR_EQ(b, "1.3");
    TEST_END();
}

int test_voice_mode_enables(void)
{
    TEST_BEGIN();
    VoiceModeEnables e;
    /* Dictation, Toggle key */
    voice_mode_enables(1, VOICE_MODE_DICTATION, VOICE_MODE_TOGGLE, &e);
    ASSERT_EQ(e.key_mode, 1);
    ASSERT_EQ(e.mic_delay, 0);
    ASSERT_EQ(e.auto_send, 1);
    ASSERT_EQ(e.conversation, 0);
    /* Dictation, Hold key */
    voice_mode_enables(1, VOICE_MODE_DICTATION, VOICE_MODE_HOLD, &e);
    ASSERT_EQ(e.mic_delay, 1);
    /* Conversation: key mode, delay and auto-send off, the section on */
    voice_mode_enables(1, VOICE_MODE_CONVERSATION, VOICE_MODE_HOLD, &e);
    ASSERT_EQ(e.key_mode, 0);
    ASSERT_EQ(e.mic_delay, 0);
    ASSERT_EQ(e.auto_send, 0);
    ASSERT_EQ(e.conversation, 1);
    /* Voice off: everything off */
    voice_mode_enables(0, VOICE_MODE_CONVERSATION, VOICE_MODE_TOGGLE, &e);
    ASSERT_EQ(e.key_mode + e.mic_delay + e.auto_send + e.conversation, 0);
    voice_mode_enables(0, VOICE_MODE_DICTATION, VOICE_MODE_HOLD, &e);
    ASSERT_EQ(e.key_mode + e.mic_delay + e.auto_send + e.conversation, 0);
    TEST_END();
}

int test_voice_speech_enables(void)
{
    TEST_BEGIN();
    VoiceSpeechEnables e;
    /* Dictation (the Conversation section off): everything off */
    voice_speech_enables(0, VOICE_SPEECH_AI, "custom", 0, 0, &e);
    ASSERT_EQ(e.speech + e.ai_rows + e.url + e.key + e.style + e.uk_line + e.privacy, 0);
    /* Windows speech, no British woman's voice installed: the line, nothing of AI */
    voice_speech_enables(1, VOICE_SPEECH_WINDOWS, "mistral", 0, 0, &e);
    ASSERT_EQ(e.speech, 1);
    ASSERT_EQ(e.uk_line, 1);
    ASSERT_EQ(e.ai_rows + e.url + e.key + e.style + e.privacy, 0);
    /* Windows speech with such a voice installed: no line */
    voice_speech_enables(1, VOICE_SPEECH_WINDOWS, "mistral", 0, 1, &e);
    ASSERT_EQ(e.uk_line, 0);
    ASSERT_EQ(e.speech, 1);
    /* AI speech with Mistral, key shared: no key, url or style; privacy on; no UK line */
    voice_speech_enables(1, VOICE_SPEECH_AI, "mistral", 1, 0, &e);
    ASSERT_EQ(e.ai_rows, 1);
    ASSERT_EQ(e.privacy, 1);
    ASSERT_EQ(e.key + e.url + e.style + e.uk_line, 0);
    /* Mistral with its own key: the key row, still no url or style */
    voice_speech_enables(1, VOICE_SPEECH_AI, "mistral", 0, 0, &e);
    ASSERT_EQ(e.key, 1);
    ASSERT_EQ(e.url + e.style, 0);
    /* Custom: url, key and style */
    voice_speech_enables(1, VOICE_SPEECH_AI, "custom", 0, 0, &e);
    ASSERT_EQ(e.url, 1);
    ASSERT_EQ(e.key, 1);
    ASSERT_EQ(e.style, 1);
    ASSERT_EQ(e.privacy, 1);
    /* An unknown provider: the AI rows stay on, the url is off */
    voice_speech_enables(1, VOICE_SPEECH_AI, "openai", 0, 0, &e);
    ASSERT_EQ(e.ai_rows, 1);
    ASSERT_EQ(e.privacy, 1);
    ASSERT_EQ(e.url, 0);
    ASSERT_EQ(e.style, 0);
    /* NULL provider is unknown, not a crash */
    voice_speech_enables(1, VOICE_SPEECH_AI, NULL, 0, 0, &e);
    ASSERT_EQ(e.ai_rows, 1);
    ASSERT_EQ(e.url, 0);
    TEST_END();
}

/* ---- PR 3: AI speech settings ------------------------------------------- */

static const char *const k_ais_keys[7] = {
    "voice_speech_engine", "voice_tts_provider", "voice_tts_url", "tts_api_key",
    "voice_ai_voice", "voice_ai_tts_model", "voice_ai_style",
};

int test_voice_ais_settings_defaults(void)
{
    TEST_BEGIN();
    remove(VCFG);
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": []}"));
    Config *c = config_load(VCFG);
    ASSERT_NOT_NULL(c);
    if (c) {
        const Settings *s = &c->settings;
        ASSERT_EQ(s->voice_speech_engine, VOICE_SPEECH_WINDOWS);
        ASSERT_STR_EQ(s->voice_tts_provider, VOICE_AIS_DEFAULT_PROVIDER);
        ASSERT_STR_EQ(s->voice_tts_provider, "mistral");
        ASSERT_STR_EQ(s->voice_tts_url, "");
        ASSERT_STR_EQ(s->tts_api_key, "");
        ASSERT_STR_EQ(s->tts_api_key_for, "");
        ASSERT_STR_EQ(s->tts_api_key_enc_preserved, "");
        ASSERT_STR_EQ(s->voice_ai_voice, "");
        ASSERT_STR_EQ(s->voice_ai_tts_model, "");
        ASSERT_STR_EQ(s->voice_ai_style, "");
        config_free(c);
    }
    ASSERT_STR_EQ(voice_speech_json(VOICE_SPEECH_WINDOWS), "windows");
    ASSERT_STR_EQ(voice_speech_json(VOICE_SPEECH_AI), "ai");
    ASSERT_EQ(voice_speech_from_json("ai"), VOICE_SPEECH_AI);
    ASSERT_EQ(voice_speech_from_json("windows"), VOICE_SPEECH_WINDOWS);
    ASSERT_EQ(voice_speech_from_json("loud"), VOICE_SPEECH_WINDOWS);
    ASSERT_EQ(voice_speech_from_json(NULL), VOICE_SPEECH_WINDOWS);
    remove(VCFG);
    TEST_END();
}

int test_voice_ais_settings_roundtrip(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *cfg = config_new_default();
    Settings *s = &cfg->settings;
    s->voice_speech_engine = VOICE_SPEECH_AI;
    snprintf(s->voice_tts_provider, sizeof s->voice_tts_provider, "custom");
    snprintf(s->voice_tts_url, sizeof s->voice_tts_url, "http://10.0.0.5:8880/v1");
    snprintf(s->voice_ai_voice, sizeof s->voice_ai_voice, "bf_emma");
    snprintf(s->voice_ai_tts_model, sizeof s->voice_ai_tts_model, "kokoro");
    snprintf(s->voice_ai_style, sizeof s->voice_ai_style, "say \"hi\"\nslowly");
    snprintf(s->tts_api_key, sizeof s->tts_api_key, "sk-tts-secret");
    snprintf(s->tts_api_key_for, sizeof s->tts_api_key_for, "custom http://10.0.0.5:8880/v1");
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);

    char *txt = vcfg_read();
    ASSERT_NOT_NULL(txt);
    if (txt) {
        ASSERT_TRUE(strstr(txt, "\"voice_speech_engine\": \"ai\"") != NULL);
        ASSERT_TRUE(strstr(txt, "\"tts_api_key\": \"$dpapi$v1$") != NULL);
        ASSERT_TRUE(strstr(txt, "sk-tts-secret") == NULL);
        /* the binding is inside the blob too, never a JSON key of its own */
        ASSERT_TRUE(strstr(txt, "tts_api_key_for") == NULL);
        ASSERT_TRUE(strstr(txt, "\"custom http://10.0.0.5:8880/v1\"") == NULL);
        free(txt);
    }
    Config *l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        const Settings *t = &l->settings;
        ASSERT_EQ(t->voice_speech_engine, VOICE_SPEECH_AI);
        ASSERT_STR_EQ(t->voice_tts_provider, "custom");
        ASSERT_STR_EQ(t->voice_tts_url, "http://10.0.0.5:8880/v1");
        ASSERT_STR_EQ(t->voice_ai_voice, "bf_emma");
        ASSERT_STR_EQ(t->voice_ai_tts_model, "kokoro");
        ASSERT_STR_EQ(t->voice_ai_style, "say \"hi\"\nslowly");
        ASSERT_STR_EQ(t->tts_api_key, "sk-tts-secret");
        ASSERT_STR_EQ(t->tts_api_key_for, "custom http://10.0.0.5:8880/v1");
        config_free(l);
    }
    remove(VCFG);
    TEST_END();
}

int test_voice_ais_key_binding_roundtrip(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *cfg = config_new_default();
    snprintf(cfg->settings.tts_api_key, sizeof cfg->settings.tts_api_key, "k1");
    snprintf(cfg->settings.tts_api_key_for, sizeof cfg->settings.tts_api_key_for, "mistral");
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);
    Config *l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        ASSERT_STR_EQ(l->settings.tts_api_key, "k1");
        ASSERT_STR_EQ(l->settings.tts_api_key_for, "mistral");
        config_free(l);
    }
    /* a key with no binding: the key is the whole plaintext, the binding empty */
    char enc[256];
    ASSERT_EQ(crypto_encrypt_dpapi("bare-key", enc, sizeof enc), CRYPTO_OK);
    char json[1024];
    snprintf(json, sizeof json,
             "{\"settings\": {\"font\": \"Consolas\", \"tts_api_key\": \"%s\"}, \"profiles\": []}", enc);
    ASSERT_TRUE(vcfg_write(json));
    l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        ASSERT_STR_EQ(l->settings.tts_api_key, "bare-key");
        ASSERT_STR_EQ(l->settings.tts_api_key_for, "");
        config_free(l);
    }
    /* only the first newline splits: the key may not hold one but must not lose text */
    ASSERT_EQ(crypto_encrypt_dpapi("custom http://h/v1\nk\nz", enc, sizeof enc), CRYPTO_OK);
    snprintf(json, sizeof json,
             "{\"settings\": {\"font\": \"Consolas\", \"tts_api_key\": \"%s\"}, \"profiles\": []}", enc);
    ASSERT_TRUE(vcfg_write(json));
    l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        ASSERT_STR_EQ(l->settings.tts_api_key_for, "custom http://h/v1");
        ASSERT_STR_EQ(l->settings.tts_api_key, "k\nz");
        config_free(l);
    }
    remove(VCFG);
    TEST_END();
}

int test_voice_ais_key_preserved(void)
{
    TEST_BEGIN();
    fake_dpapi_set_identity(0x11);
    char enc[256];
    ASSERT_EQ(crypto_encrypt_dpapi("mistral\nmy-tts-key", enc, sizeof enc), CRYPTO_OK);
    char json[1024];
    snprintf(json, sizeof json,
             "{\"settings\": {\"font\": \"Consolas\", \"tts_api_key\": \"%s\"}, \"profiles\": []}", enc);
    ASSERT_TRUE(vcfg_write(json));
    fake_dpapi_set_identity(0x22);
    Config *cfg = config_load(VCFG);
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        ASSERT_STR_EQ(cfg->settings.tts_api_key, "");
        ASSERT_STR_EQ(cfg->settings.tts_api_key_for, "");
        ASSERT_STR_EQ(cfg->settings.tts_api_key_enc_preserved, enc);
        ASSERT_EQ(config_save(cfg, VCFG), 0);
        config_free(cfg);
    }
    char *raw = vcfg_read();
    ASSERT_NOT_NULL(raw);
    if (raw) { ASSERT_TRUE(strstr(raw, enc) != NULL); free(raw); }
    fake_dpapi_set_identity(0x11);
    Config *c2 = config_load(VCFG);
    ASSERT_NOT_NULL(c2);
    if (c2) {
        ASSERT_STR_EQ(c2->settings.tts_api_key, "my-tts-key");
        ASSERT_STR_EQ(c2->settings.tts_api_key_for, "mistral");
        config_free(c2);
    }
    /* a new key drops the preserved blob */
    fake_dpapi_set_identity(0x22);
    cfg = config_load(VCFG);
    ASSERT_NOT_NULL(cfg);
    if (cfg) {
        snprintf(cfg->settings.tts_api_key, sizeof cfg->settings.tts_api_key, "fresh");
        snprintf(cfg->settings.tts_api_key_for, sizeof cfg->settings.tts_api_key_for, "mistral");
        config_secret_drop_stale_preserved(cfg->settings.tts_api_key,
            cfg->settings.tts_api_key_enc_preserved,
            sizeof cfg->settings.tts_api_key_enc_preserved);
        ASSERT_STR_EQ(cfg->settings.tts_api_key_enc_preserved, "");
        ASSERT_EQ(config_save(cfg, VCFG), 0);
        config_free(cfg);
    }
    raw = vcfg_read();
    ASSERT_NOT_NULL(raw);
    if (raw) { ASSERT_TRUE(strstr(raw, enc) == NULL); free(raw); }
    c2 = config_load(VCFG);
    ASSERT_NOT_NULL(c2);
    if (c2) { ASSERT_STR_EQ(c2->settings.tts_api_key, "fresh"); config_free(c2); }
    fake_dpapi_set_identity(0x42);
    remove(VCFG);
    TEST_END();
}

int test_voice_ais_style_empty_kept(void)
{
    TEST_BEGIN();
    const char *vals[2] = { "x", "" };
    for (int i = 0; i < 2; i++) {
        char json[256];
        snprintf(json, sizeof json,
                 "{\"settings\": {\"font\": \"Consolas\", \"voice_ai_style\": \"%s\"}, \"profiles\": []}",
                 vals[i]);
        ASSERT_TRUE(vcfg_write(json));
        Config *c = config_load(VCFG);
        ASSERT_NOT_NULL(c);
        if (c) { ASSERT_STR_EQ(c->settings.voice_ai_style, vals[i]); config_free(c); }
    }
    remove(VCFG);
    TEST_END();
}

int test_voice_ais_unknown_provider_kept(void)
{
    TEST_BEGIN();
    const char *ids[3] = { "openai", "gemini", "groq" };
    for (int i = 0; i < 3; i++) {
        char json[256];
        snprintf(json, sizeof json,
                 "{\"settings\": {\"font\": \"Consolas\", \"voice_tts_provider\": \"%s\"}, \"profiles\": []}",
                 ids[i]);
        ASSERT_TRUE(vcfg_write(json));
        Config *c = config_load(VCFG);
        ASSERT_NOT_NULL(c);
        if (c) {
            ASSERT_STR_EQ(c->settings.voice_tts_provider, ids[i]);
            settings_validate(&c->settings);
            ASSERT_STR_EQ(c->settings.voice_tts_provider, ids[i]);
            ASSERT_EQ(config_save(c, VCFG), 0);
            config_free(c);
        }
        Config *l = config_load(VCFG);
        ASSERT_NOT_NULL(l);
        if (l) { ASSERT_STR_EQ(l->settings.voice_tts_provider, ids[i]); config_free(l); }
    }
    ASSERT_TRUE(vcfg_write("{\"settings\": {\"font\": \"Consolas\", \"voice_speech_engine\": \"loud\"}, \"profiles\": []}"));
    Config *c = config_load(VCFG);
    ASSERT_NOT_NULL(c);
    if (c) { ASSERT_EQ(c->settings.voice_speech_engine, VOICE_SPEECH_WINDOWS); config_free(c); }
    remove(VCFG);
    TEST_END();
}

int test_voice_ais_notice_lines(void)
{
    TEST_BEGIN();
    Notice n;
    ConfigLoadReport r;
    Settings s;
    config_default_settings(&s);
    memset(&r, 0, sizeof r);
    r.status = CONFIG_LOAD_OK;
    for (int i = 0; i < 7; i++) r.missing[r.missing_count++] = vkey_index(k_ais_keys[i]);
    for (int i = 0; i < 7; i++) ASSERT_TRUE(r.missing[i] != 255);
    ASSERT_EQ(notice_upgrade(&n, "1.2.100", &r, &s), 1);
    ASSERT_EQ(n.item_count, 7);
    if (n.item_count == 7) {
        ASSERT_STR_EQ(n.items[0].label, "Voice: speech");
        ASSERT_STR_EQ(n.items[0].value, "Windows speech");
        ASSERT_STR_EQ(n.items[1].label, "Voice: speech provider");
        ASSERT_STR_EQ(n.items[1].value, "Mistral");
        ASSERT_STR_EQ(n.items[2].label, "Voice: speech URL");
        ASSERT_STR_EQ(n.items[2].value, "none");
        ASSERT_STR_EQ(n.items[3].label, "Voice: speech API key");
        ASSERT_STR_EQ(n.items[3].value,
                      "none; the speech-to-text key is used when the provider is the same");
        ASSERT_STR_EQ(n.items[4].label, "Voice: AI voice");
        ASSERT_STR_EQ(n.items[4].value,
                      "the provider's default (for Mistral, Mary, British English)");
        ASSERT_STR_EQ(n.items[5].label, "Voice: AI speech model");
        ASSERT_STR_EQ(n.items[5].value, "the provider's default");
        ASSERT_STR_EQ(n.items[6].label, "Voice: accent and style");
        ASSERT_STR_EQ(n.items[6].value, "none");
    }
    /* with the voice switch new too, they fold into the one "Voice" line */
    r.missing[r.missing_count++] = vkey_index("voice_enabled");
    ASSERT_EQ(notice_upgrade(&n, "1.2.100", &r, &s), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_STR_EQ(n.items[0].label, "Voice");
    TEST_END();
}

int test_voice_ais_digest(void)
{
    TEST_BEGIN();
    Settings a, b;
    config_default_settings(&a);
    const uint64_t base = voice_settings_digest(&a);
    config_default_settings(&b);
    b.voice_speech_engine = VOICE_SPEECH_AI;
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.voice_tts_provider, sizeof b.voice_tts_provider, "custom");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.voice_tts_url, sizeof b.voice_tts_url, "http://h/v1");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.tts_api_key, sizeof b.tts_api_key, "k");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.tts_api_key_for, sizeof b.tts_api_key_for, "mistral");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.voice_ai_voice, sizeof b.voice_ai_voice, "v");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.voice_ai_tts_model, sizeof b.voice_ai_tts_model, "m");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    config_default_settings(&b);
    snprintf(b.voice_ai_style, sizeof b.voice_ai_style, "calm");
    ASSERT_TRUE(voice_settings_digest(&b) != base);
    /* the binding differs, the key does not */
    Settings c1, c2;
    config_default_settings(&c1);
    config_default_settings(&c2);
    snprintf(c1.tts_api_key, sizeof c1.tts_api_key, "k");
    snprintf(c2.tts_api_key, sizeof c2.tts_api_key, "k");
    snprintf(c1.tts_api_key_for, sizeof c1.tts_api_key_for, "mistral");
    snprintf(c2.tts_api_key_for, sizeof c2.tts_api_key_for, "custom http://h");
    ASSERT_TRUE(voice_settings_digest(&c1) != voice_settings_digest(&c2));
    TEST_END();
}

int test_voice_ais_long_stt_blob_preserved_not_truncated(void)
{
    TEST_BEGIN();
    fake_dpapi_set_identity(0x11);
    char plain[301];
    memset(plain, 'k', 300);
    plain[300] = '\0';
    char enc[1024];
    ASSERT_EQ(crypto_encrypt_dpapi(plain, enc, sizeof enc), CRYPTO_OK);
    char json[2048];
    snprintf(json, sizeof json,
             "{\"settings\": {\"font\": \"Consolas\", \"stt_api_key\": \"%s\"}, \"profiles\": []}", enc);
    ASSERT_TRUE(vcfg_write(json));
    Config *c = config_load(VCFG);
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_STR_EQ(c->settings.stt_api_key, "");
        ASSERT_STR_EQ(c->settings.stt_api_key_enc_preserved, enc);
        config_free(c);
    }
    remove(VCFG);
    fake_dpapi_set_identity(0x42);
    TEST_END();
}

int test_voice_ais_empty_key_drops_binding(void)
{
    TEST_BEGIN();
    remove(VCFG);
    Config *cfg = config_new_default();
    snprintf(cfg->settings.tts_api_key_for, sizeof cfg->settings.tts_api_key_for, "mistral");
    ASSERT_EQ(config_save(cfg, VCFG), 0);
    config_free(cfg);
    char *txt = vcfg_read();
    ASSERT_NOT_NULL(txt);
    if (txt) { ASSERT_TRUE(strstr(txt, "\"tts_api_key\": \"\"") != NULL); free(txt); }
    Config *l = config_load(VCFG);
    ASSERT_NOT_NULL(l);
    if (l) {
        ASSERT_STR_EQ(l->settings.tts_api_key, "");
        ASSERT_STR_EQ(l->settings.tts_api_key_for, "");
        config_free(l);
    }
    remove(VCFG);
    TEST_END();
}
