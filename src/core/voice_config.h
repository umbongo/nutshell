#ifndef NUTSHELL_VOICE_CONFIG_H
#define NUTSHELL_VOICE_CONFIG_H

#include "../config/config.h"
#include <stdint.h>

/*
 * voice_config -- the push-to-talk key and mode enums, their JSON names, and
 * the defaults and validation of the voice fields in Settings. Portable, no
 * Win32. Called from config_default_settings() and settings_validate().
 */

typedef enum {
    VOICE_PTT_RIGHT_SHIFT = 0,   /* the default; the combo lists keys in this order */
    VOICE_PTT_RIGHT_CTRL = 1,
    VOICE_PTT_F8 = 2,
    VOICE_PTT_F9 = 3,
    VOICE_PTT_COUNT
} VoicePttKey;

typedef enum {
    VOICE_MODE_TOGGLE = 0,       /* the default */
    VOICE_MODE_HOLD = 1,
    VOICE_MODE_COUNT
} VoicePttMode;

/* Settings.voice_mode: where a transcript goes. Distinct from VoicePttMode
 * (VOICE_MODE_TOGGLE/HOLD), which is the key's behaviour. */
#define VOICE_MODE_DICTATION 0
#define VOICE_MODE_CONVERSATION 1

#define VOICE_PAUSE_MIN_MS 500
#define VOICE_PAUSE_MAX_MS 3000
#define VOICE_PAUSE_DEFAULT_MS 1200

#define VOICE_MIC_DELAY_DEFAULT 150
#define VOICE_MIC_DELAY_MIN 0
#define VOICE_MIC_DELAY_MAX 400
#define VOICE_GAIN_MIN_DB (-12)
#define VOICE_GAIN_MAX_DB 30
#define VOICE_GAIN_DEFAULT_DB 0
#define VOICE_CAP_MS 60000
#define VOICE_LIVE_CAP_MS 600000
#define VOICE_SHORT_TAP_MS 300

const char *voice_ptt_key_json(int key);        /* "right_shift" "right_ctrl" "f8" "f9"; default for out of range */
int voice_ptt_key_from_json(const char *s);     /* unknown/NULL -> VOICE_PTT_RIGHT_SHIFT */
const char *voice_ptt_key_label(int key);       /* "Right Shift" "Right Ctrl" "F8" "F9" */
int voice_ptt_key_is_modifier(int key);         /* Right Shift, Right Ctrl: a press is never consumed */
const char *voice_ptt_mode_json(int mode);      /* "toggle" "hold"; default for out of range */
int voice_ptt_mode_from_json(const char *s);    /* unknown/NULL -> VOICE_MODE_TOGGLE */
const char *voice_ptt_mode_label(int mode);     /* "Toggle" "Hold to talk"; default for out of range */
const char *voice_mode_json(int mode);          /* "dictation" "conversation"; Dictation for out of range */
int voice_mode_from_json(const char *s);        /* unknown/NULL -> VOICE_MODE_DICTATION */

/* Settings.voice_speech_engine: who speaks the replies. */
#define VOICE_SPEECH_WINDOWS 0
#define VOICE_SPEECH_AI 1
#define VOICE_AIS_DEFAULT_PROVIDER "mistral"
const char *voice_speech_json(int e);           /* "windows" "ai"; Windows for out of range */
int voice_speech_from_json(const char *s);      /* unknown/NULL -> VOICE_SPEECH_WINDOWS */
int voice_pause_clamp(int ms);                  /* into VOICE_PAUSE_MIN_MS..VOICE_PAUSE_MAX_MS */
int voice_pause_round(int ms);                  /* clamped, then to the nearest 100 (halves up) */
int voice_pause_parse(const char *text, int keep_ms); /* Settings entry in seconds ("1.2", "1,5") -> ms, rounded to 100 and clamped; keep_ms when it is not a plain non-negative number */
void voice_pause_format(int ms, char *out, size_t cap); /* "1.2": seconds with one decimal, ms clamped and rounded */

/* What the Voice page leaves enabled (1) for a voice_on / Mode / Key mode state. */
typedef struct { int key_mode; int mic_delay; int auto_send; int conversation; } VoiceModeEnables;
void voice_mode_enables(int voice_on, int mode, int ptt_mode, VoiceModeEnables *out);
int voice_gain_clamp(int db);                   /* into VOICE_GAIN_MIN_DB..VOICE_GAIN_MAX_DB */
void voice_gain_label(int db, char *out, size_t cap); /* "+12 dB" "0 dB" "-12 dB" */
void voice_settings_default(Settings *s);       /* called by config_default_settings */
void voice_settings_validate(Settings *s);      /* called by settings_validate */
/* FNV-1a 64 over every voice_* field, stt_api_key and the AI provider, key and
 * URL ("Use current AI assistant" reads them): equal digests mean no voice
 * setting changed between two Settings values. The key is hashed in memory and
 * never logged. */
uint64_t voice_settings_digest(const Settings *s);

#endif /* NUTSHELL_VOICE_CONFIG_H */
