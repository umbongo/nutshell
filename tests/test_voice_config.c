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

static const char *const k_voice_keys[14] = {
    "voice_enabled", "voice_stt_use_ai", "voice_stt_provider", "voice_stt_url",
    "voice_stt_model", "voice_stt_language", "stt_api_key", "voice_input_device",
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
    ASSERT_STR_EQ(s.voice_stt_language, "");
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
        if (found < 14u && strcmp(keys[rep.missing[i]].key, k_voice_keys[found]) == 0)
            found++;
    }
    ASSERT_EQ(found, (size_t)14);
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
    for (int i = 0; i < 14; i++) r.missing[r.missing_count++] = vkey_index(k_voice_keys[i]);
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
        ASSERT_TRUE(strstr(t, VMARK ": 2") != NULL);
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
