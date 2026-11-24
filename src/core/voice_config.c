/* src/core/voice_config.c -- see voice_config.h */
#include "voice_config.h"
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

void voice_settings_default(Settings *s)
{
    if (!s) return;
    s->voice_enabled = 0;
    s->voice_stt_use_ai = 1;
    (void)snprintf(s->voice_stt_provider, sizeof(s->voice_stt_provider), "openai");
    s->voice_stt_url[0] = '\0';
    s->voice_stt_model[0] = '\0';
    s->voice_stt_language[0] = '\0';
    s->stt_api_key[0] = '\0';
    s->stt_api_key_enc_preserved[0] = '\0';
    s->voice_input_device[0] = '\0';
    s->voice_input_device_name[0] = '\0';
    s->voice_ptt_key = (int)VOICE_PTT_RIGHT_SHIFT;
    s->voice_ptt_mode = (int)VOICE_MODE_TOGGLE;
    s->voice_mic_delay_ms = VOICE_MIC_DELAY_DEFAULT;
    s->voice_auto_send = 0;
}

void voice_settings_validate(Settings *s)
{
    if (!s) return;
    s->voice_enabled = s->voice_enabled ? 1 : 0;
    s->voice_stt_use_ai = s->voice_stt_use_ai ? 1 : 0;
    s->voice_auto_send = s->voice_auto_send ? 1 : 0;
    if (s->voice_mic_delay_ms < VOICE_MIC_DELAY_MIN) s->voice_mic_delay_ms = VOICE_MIC_DELAY_MIN;
    if (s->voice_mic_delay_ms > VOICE_MIC_DELAY_MAX) s->voice_mic_delay_ms = VOICE_MIC_DELAY_MAX;
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
}
