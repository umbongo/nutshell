#include "test_framework.h"
#include "config.h"
#include "voice_config.h"
#include "voice_gate.h"
#include "ai_gate.h"
#include "opt_feature.h"
#include "notice.h"
#include "crypto.h"
#include "crypto_dpapi.h"
#include "fake_dpapi.h"
#include "json_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define VCFG TEST_TMP_DIR "/nutshell_voice_test.config"

static const char *const k_voice_keys[13] = {
    "voice_enabled", "voice_stt_use_ai", "voice_stt_provider", "voice_stt_url",
    "voice_stt_model", "voice_stt_language", "stt_api_key", "voice_input_device",
    "voice_input_device_name", "voice_ptt_key", "voice_ptt_mode",
    "voice_mic_delay_ms", "voice_review",
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
    ASSERT_STR_EQ(s.voice_stt_language, "");
    ASSERT_STR_EQ(s.stt_api_key, "");
    ASSERT_STR_EQ(s.stt_api_key_enc_preserved, "");
    ASSERT_STR_EQ(s.voice_input_device, "");
    ASSERT_STR_EQ(s.voice_input_device_name, "");
    ASSERT_EQ(s.voice_mic_delay_ms, 150);
    ASSERT_EQ(s.voice_ptt_key, (int)VOICE_PTT_RIGHT_CTRL);
    ASSERT_EQ(s.voice_ptt_mode, (int)VOICE_MODE_HOLD);
    ASSERT_EQ(s.voice_review, 0);
    TEST_END();
}

int test_voice_config_json_names(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_ptt_key_json(VOICE_PTT_RIGHT_CTRL), "right_ctrl");
    ASSERT_STR_EQ(voice_ptt_key_json(VOICE_PTT_F8), "f8");
    ASSERT_STR_EQ(voice_ptt_key_json(VOICE_PTT_F9), "f9");
    ASSERT_STR_EQ(voice_ptt_key_json(-1), "right_ctrl");
    ASSERT_STR_EQ(voice_ptt_key_json(99), "right_ctrl");
    ASSERT_EQ(voice_ptt_key_from_json("right_ctrl"), (int)VOICE_PTT_RIGHT_CTRL);
    ASSERT_EQ(voice_ptt_key_from_json("f8"), (int)VOICE_PTT_F8);
    ASSERT_EQ(voice_ptt_key_from_json("f9"), (int)VOICE_PTT_F9);
    ASSERT_EQ(voice_ptt_key_from_json(NULL), (int)VOICE_PTT_RIGHT_CTRL);
    ASSERT_EQ(voice_ptt_key_from_json(""), (int)VOICE_PTT_RIGHT_CTRL);
    ASSERT_EQ(voice_ptt_key_from_json("bogus"), (int)VOICE_PTT_RIGHT_CTRL);
    ASSERT_STR_EQ(voice_ptt_key_label(VOICE_PTT_RIGHT_CTRL), "Right Ctrl");
    ASSERT_STR_EQ(voice_ptt_key_label(VOICE_PTT_F8), "F8");
    ASSERT_STR_EQ(voice_ptt_key_label(VOICE_PTT_F9), "F9");
    ASSERT_STR_EQ(voice_ptt_key_label(-5), "Right Ctrl");
    ASSERT_STR_EQ(voice_ptt_key_label(3), "Right Ctrl");
    ASSERT_STR_EQ(voice_ptt_mode_json(VOICE_MODE_HOLD), "hold");
    ASSERT_STR_EQ(voice_ptt_mode_json(VOICE_MODE_TOGGLE), "toggle");
    ASSERT_STR_EQ(voice_ptt_mode_json(-1), "hold");
    ASSERT_STR_EQ(voice_ptt_mode_json(2), "hold");
    ASSERT_EQ(voice_ptt_mode_from_json("hold"), (int)VOICE_MODE_HOLD);
    ASSERT_EQ(voice_ptt_mode_from_json("toggle"), (int)VOICE_MODE_TOGGLE);
    ASSERT_EQ(voice_ptt_mode_from_json(NULL), (int)VOICE_MODE_HOLD);
    ASSERT_EQ(voice_ptt_mode_from_json(""), (int)VOICE_MODE_HOLD);
    ASSERT_EQ(voice_ptt_mode_from_json("bogus"), (int)VOICE_MODE_HOLD);
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
    snprintf(s->voice_stt_language, sizeof(s->voice_stt_language), "en");
    snprintf(s->stt_api_key, sizeof(s->stt_api_key), "sk-stt");
    snprintf(s->voice_input_device, sizeof(s->voice_input_device),
             "{0.0.1.00000000}.{abc}");
    snprintf(s->voice_input_device_name, sizeof(s->voice_input_device_name),
             "Headset Microphone (USB Audio)");
    s->voice_ptt_key = VOICE_PTT_F9;
    s->voice_ptt_mode = VOICE_MODE_TOGGLE;
    s->voice_mic_delay_ms = 400;
    s->voice_review = 1;
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
        ASSERT_STR_EQ(t->voice_stt_language, "en");
        ASSERT_STR_EQ(t->stt_api_key, "sk-stt");
        ASSERT_STR_EQ(t->voice_input_device, "{0.0.1.00000000}.{abc}");
        ASSERT_STR_EQ(t->voice_input_device_name, "Headset Microphone (USB Audio)");
        ASSERT_EQ(t->voice_ptt_key, (int)VOICE_PTT_F9);
        ASSERT_EQ(t->voice_ptt_mode, (int)VOICE_MODE_TOGGLE);
        ASSERT_EQ(t->voice_mic_delay_ms, 400);
        ASSERT_EQ(t->voice_review, 1);
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
        if (found < 13u && strcmp(keys[rep.missing[i]].key, k_voice_keys[found]) == 0)
            found++;
    }
    ASSERT_EQ(found, (size_t)13);
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
    ASSERT_EQ(s.voice_ptt_key, (int)VOICE_PTT_RIGHT_CTRL);
    ASSERT_EQ(s.voice_ptt_mode, (int)VOICE_MODE_HOLD);
    snprintf(s.voice_stt_provider, sizeof(s.voice_stt_provider), "elevenlabs");
    settings_validate(&s);
    ASSERT_STR_EQ(s.voice_stt_provider, "openai");
    snprintf(s.voice_stt_provider, sizeof(s.voice_stt_provider), "custom");
    settings_validate(&s);
    ASSERT_STR_EQ(s.voice_stt_provider, "custom");
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
    for (int i = 0; i < 13; i++) r.missing[r.missing_count++] = vkey_index(k_voice_keys[i]);
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
    r.missing[r.missing_count++] = vkey_index("voice_review");
    ASSERT_EQ(notice_upgrade(&n, "1.2.90", &r, &s), 1);
    ASSERT_EQ(n.item_count, 1);
    ASSERT_TRUE(strcmp(n.items[0].label, "Voice") != 0);
    TEST_END();
}
