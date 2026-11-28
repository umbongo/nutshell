/* src/core/voice_config.c -- see voice_config.h */
#include "voice_config.h"
#include "voice_lang.h"
#include <stdio.h>
#include <string.h>

static const char *const k_key_json[VOICE_PTT_COUNT]  = { "right_shift", "right_ctrl", "f8", "f9" };
static const char *const k_key_label[VOICE_PTT_COUNT] = { "Right Shift", "Right Ctrl", "F8", "F9" };
static const char *const k_mode_json[VOICE_MODE_COUNT] = { "toggle", "hold" };
static const char *const k_mode_label[VOICE_MODE_COUNT] = { "Toggle", "Hold to talk" };

const char *voice_ptt_key_json(int key)
{
    return (key >= 0 && key < (int)VOICE_PTT_COUNT) ? k_key_json[key] : k_key_json[0];
}

int voice_ptt_key_from_json(const char *s)
{
    if (s) {
        for (int i = 0; i < (int)VOICE_PTT_COUNT; i++)
            if (strcmp(s, k_key_json[i]) == 0) return i;
    }
    return (int)VOICE_PTT_RIGHT_SHIFT;
}

const char *voice_ptt_key_label(int key)
{
    return (key >= 0 && key < (int)VOICE_PTT_COUNT) ? k_key_label[key] : k_key_label[0];
}

int voice_ptt_key_is_modifier(int key)
{
    return key == (int)VOICE_PTT_RIGHT_SHIFT || key == (int)VOICE_PTT_RIGHT_CTRL;
}

const char *voice_ptt_mode_json(int mode)
{
    return (mode >= 0 && mode < (int)VOICE_MODE_COUNT) ? k_mode_json[mode] : k_mode_json[0];
}

const char *voice_ptt_mode_label(int mode)
{
    return (mode >= 0 && mode < (int)VOICE_MODE_COUNT) ? k_mode_label[mode] : k_mode_label[0];
}

int voice_ptt_mode_from_json(const char *s)
{
    if (s) {
        for (int i = 0; i < (int)VOICE_MODE_COUNT; i++)
            if (strcmp(s, k_mode_json[i]) == 0) return i;
    }
    return (int)VOICE_MODE_TOGGLE;
}

const char *voice_mode_json(int mode)
{
    return mode == VOICE_MODE_CONVERSATION ? "conversation" : "dictation";
}

int voice_mode_from_json(const char *s)
{
    return (s && strcmp(s, "conversation") == 0) ? VOICE_MODE_CONVERSATION : VOICE_MODE_DICTATION;
}

const char *voice_speech_json(int e)
{
    return e == VOICE_SPEECH_AI ? "ai" : "windows";
}

int voice_speech_from_json(const char *s)
{
    return (s && strcmp(s, "ai") == 0) ? VOICE_SPEECH_AI : VOICE_SPEECH_WINDOWS;
}

int voice_pause_clamp(int ms)
{
    if (ms < VOICE_PAUSE_MIN_MS) return VOICE_PAUSE_MIN_MS;
    if (ms > VOICE_PAUSE_MAX_MS) return VOICE_PAUSE_MAX_MS;
    return ms;
}

int voice_pause_round(int ms)
{
    return (voice_pause_clamp(ms) + 50) / 100 * 100;
}

int voice_pause_parse(const char *text, int keep_ms)
{
    if (!text) return keep_ms;
    while (*text == ' ' || *text == '\t') text++;
    long whole = 0;
    int digits = 0;
    while (*text >= '0' && *text <= '9') {
        if (whole < 100000) whole = whole * 10 + (*text - '0');
        text++;
        digits++;
    }
    long frac = 0;   /* hundredths of a second, from the first two decimals */
    if (*text == '.' || *text == ',') {
        text++;
        int places = 0;
        while (*text >= '0' && *text <= '9') {
            if (places < 2) frac = frac * 10 + (*text - '0');
            places++;
            digits++;
            text++;
        }
        if (places == 1) frac *= 10;
    }
    while (*text == ' ' || *text == '\t') text++;
    if (digits == 0 || *text != '\0') return keep_ms;
    long ms = whole * 1000 + frac * 10;
    if (ms > VOICE_PAUSE_MAX_MS) ms = VOICE_PAUSE_MAX_MS;
    return voice_pause_round((int)ms);
}

void voice_pause_format(int ms, char *out, size_t cap)
{
    ms = voice_pause_round(ms);
    (void)snprintf(out, cap, "%d.%d", ms / 1000, ms % 1000 / 100);
}

void voice_mode_enables(int voice_on, int mode, int ptt_mode, VoiceModeEnables *out)
{
    const int conv = mode == VOICE_MODE_CONVERSATION;
    out->key_mode = voice_on && !conv;
    out->mic_delay = voice_on && !conv && ptt_mode == VOICE_MODE_HOLD;
    out->auto_send = voice_on && !conv;
    out->conversation = voice_on && conv;
}

void voice_speech_enables(int conversation_enabled, int engine, const char *provider,
                          int key_shared, int uk_voice_installed, VoiceSpeechEnables *out)
{
    memset(out, 0, sizeof(*out));
    if (!conversation_enabled) return;
    const int ai = engine == VOICE_SPEECH_AI;
    const int custom = provider && strcmp(provider, "custom") == 0;
    out->speech = 1;
    out->ai_rows = ai;
    out->url = ai && custom;
    out->key = ai && !key_shared;
    out->style = ai && custom;
    out->uk_line = !ai && !uk_voice_installed;
    out->privacy = ai;
}

int voice_gain_clamp(int db)
{
    if (db < VOICE_GAIN_MIN_DB) return VOICE_GAIN_MIN_DB;
    if (db > VOICE_GAIN_MAX_DB) return VOICE_GAIN_MAX_DB;
    return db;
}

void voice_gain_label(int db, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    (void)snprintf(out, cap, db > 0 ? "+%d dB" : "%d dB", db);
}

void voice_settings_default(Settings *s)
{
    if (!s) return;
    s->voice_enabled = 0;
    s->voice_stt_use_ai = 1;
    (void)snprintf(s->voice_stt_provider, sizeof(s->voice_stt_provider), "openai");
    s->voice_stt_url[0] = '\0';
    s->voice_stt_model[0] = '\0';
    s->voice_stt_live_model[0] = '\0';
    (void)snprintf(s->voice_stt_language, sizeof(s->voice_stt_language), "%s", VOICE_LANG_DEFAULT);
    s->stt_api_key[0] = '\0';
    s->stt_api_key_enc_preserved[0] = '\0';
    s->voice_input_device[0] = '\0';
    s->voice_input_device_name[0] = '\0';
    s->voice_mic_gain_db = VOICE_GAIN_DEFAULT_DB;
    s->voice_ptt_key = (int)VOICE_PTT_RIGHT_SHIFT;
    s->voice_ptt_mode = (int)VOICE_MODE_TOGGLE;
    s->voice_mic_delay_ms = VOICE_MIC_DELAY_DEFAULT;
    s->voice_auto_send = 0;
    s->voice_mode = VOICE_MODE_DICTATION;
    s->voice_pause_ms = VOICE_PAUSE_DEFAULT_MS;
    s->voice_conv_mute_speaking = 0;
    s->voice_windows_voice[0] = '\0';
    s->voice_windows_voice_name[0] = '\0';
    s->voice_speech_engine = VOICE_SPEECH_WINDOWS;
    (void)snprintf(s->voice_tts_provider, sizeof(s->voice_tts_provider), "%s",
                   VOICE_AIS_DEFAULT_PROVIDER);
    s->voice_tts_url[0] = '\0';
    s->tts_api_key[0] = '\0';
    s->tts_api_key_for[0] = '\0';
    s->tts_api_key_enc_preserved[0] = '\0';
    s->voice_ai_voice[0] = '\0';
    s->voice_ai_tts_model[0] = '\0';
    s->voice_ai_style[0] = '\0';
}

void voice_settings_validate(Settings *s)
{
    if (!s) return;
    s->voice_enabled = s->voice_enabled ? 1 : 0;
    s->voice_stt_use_ai = s->voice_stt_use_ai ? 1 : 0;
    s->voice_auto_send = s->voice_auto_send ? 1 : 0;
    if (s->voice_mic_delay_ms < VOICE_MIC_DELAY_MIN) s->voice_mic_delay_ms = VOICE_MIC_DELAY_MIN;
    if (s->voice_mic_delay_ms > VOICE_MIC_DELAY_MAX) s->voice_mic_delay_ms = VOICE_MIC_DELAY_MAX;
    s->voice_mic_gain_db = voice_gain_clamp(s->voice_mic_gain_db);
    if (s->voice_speech_engine != VOICE_SPEECH_AI) s->voice_speech_engine = VOICE_SPEECH_WINDOWS;
    if (s->voice_mode != VOICE_MODE_CONVERSATION) s->voice_mode = VOICE_MODE_DICTATION;
    s->voice_pause_ms = voice_pause_clamp(s->voice_pause_ms);
    s->voice_conv_mute_speaking = s->voice_conv_mute_speaking ? 1 : 0;
    if (s->voice_ptt_key < 0 || s->voice_ptt_key >= (int)VOICE_PTT_COUNT)
        s->voice_ptt_key = (int)VOICE_PTT_RIGHT_SHIFT;
    if (s->voice_ptt_mode < 0 || s->voice_ptt_mode >= (int)VOICE_MODE_COUNT)
        s->voice_ptt_mode = (int)VOICE_MODE_TOGGLE;
    static const char *const k_providers[] = { "openai", "groq", "mistral", "gemini", "custom" };
    int known = 0;
    for (size_t i = 0; i < sizeof(k_providers) / sizeof(k_providers[0]); i++)
        if (strcmp(s->voice_stt_provider, k_providers[i]) == 0) { known = 1; break; }
    if (!known)
        (void)snprintf(s->voice_stt_provider, sizeof(s->voice_stt_provider), "openai");
    /* a model id never holds a line break: it goes into a URL and a JSON body */
    if (strchr(s->voice_stt_live_model, '\r') || strchr(s->voice_stt_live_model, '\n'))
        s->voice_stt_live_model[0] = '\0';
    /* the voice token id and name are shown and handed to SAPI as text */
    if (strchr(s->voice_windows_voice, '\r') || strchr(s->voice_windows_voice, '\n')) {
        s->voice_windows_voice[0] = '\0';
        s->voice_windows_voice_name[0] = '\0';
    }
    if (strchr(s->voice_windows_voice_name, '\r') || strchr(s->voice_windows_voice_name, '\n'))
        s->voice_windows_voice_name[0] = '\0';
}

#define FNV64_OFFSET 0xcbf29ce484222325ULL
#define FNV64_PRIME  0x100000001b3ULL

static uint64_t fnv_bytes(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= FNV64_PRIME; }
    return h;
}

/* The string's bytes, then a 0xFF separator (never in UTF-8), so adjacent
 * fields cannot trade characters unnoticed. */
static uint64_t fnv_str(uint64_t h, const char *s)
{
    h = fnv_bytes(h, s, strlen(s));
    return fnv_bytes(h, "\xFF", 1);
}

static uint64_t fnv_int(uint64_t h, int v)
{
    return fnv_bytes(h, &v, sizeof v);
}

uint64_t voice_settings_digest(const Settings *s)
{
    if (!s) return 0;
    uint64_t h = FNV64_OFFSET;
    h = fnv_int(h, s->voice_enabled);
    h = fnv_int(h, s->voice_stt_use_ai);
    h = fnv_str(h, s->voice_stt_provider);
    h = fnv_str(h, s->voice_stt_url);
    h = fnv_str(h, s->voice_stt_model);
    h = fnv_str(h, s->voice_stt_live_model);
    h = fnv_str(h, s->voice_stt_language);
    h = fnv_str(h, s->stt_api_key);
    h = fnv_str(h, s->voice_input_device);
    h = fnv_str(h, s->voice_input_device_name);
    h = fnv_int(h, s->voice_mic_gain_db);
    h = fnv_int(h, s->voice_ptt_key);
    h = fnv_int(h, s->voice_ptt_mode);
    h = fnv_int(h, s->voice_mic_delay_ms);
    h = fnv_int(h, s->voice_auto_send);
    h = fnv_int(h, s->voice_mode);
    h = fnv_int(h, s->voice_pause_ms);
    h = fnv_int(h, s->voice_conv_mute_speaking);
    h = fnv_str(h, s->voice_windows_voice);
    h = fnv_str(h, s->voice_windows_voice_name);
    h = fnv_int(h, s->voice_speech_engine);
    h = fnv_str(h, s->voice_tts_provider);
    h = fnv_str(h, s->voice_tts_url);
    h = fnv_str(h, s->tts_api_key);
    h = fnv_str(h, s->tts_api_key_for);
    h = fnv_str(h, s->voice_ai_voice);
    h = fnv_str(h, s->voice_ai_tts_model);
    h = fnv_str(h, s->voice_ai_style);
    h = fnv_str(h, s->ai_provider);
    h = fnv_str(h, s->ai_api_key);
    h = fnv_str(h, s->ai_custom_url);
    return h;
}
