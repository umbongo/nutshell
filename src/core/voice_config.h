#ifndef NUTSHELL_VOICE_CONFIG_H
#define NUTSHELL_VOICE_CONFIG_H

#include "../config/config.h"

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

#define VOICE_MIC_DELAY_DEFAULT 150
#define VOICE_MIC_DELAY_MIN 0
#define VOICE_MIC_DELAY_MAX 400
#define VOICE_CAP_MS 60000
#define VOICE_SHORT_TAP_MS 300

const char *voice_ptt_key_json(int key);        /* "right_shift" "right_ctrl" "f8" "f9"; default for out of range */
int voice_ptt_key_from_json(const char *s);     /* unknown/NULL -> VOICE_PTT_RIGHT_SHIFT */
const char *voice_ptt_key_label(int key);       /* "Right Shift" "Right Ctrl" "F8" "F9" */
int voice_ptt_key_is_modifier(int key);         /* Right Shift, Right Ctrl: a press is never consumed */
const char *voice_ptt_mode_json(int mode);      /* "toggle" "hold"; default for out of range */
int voice_ptt_mode_from_json(const char *s);    /* unknown/NULL -> VOICE_MODE_TOGGLE */
const char *voice_ptt_mode_label(int mode);     /* "Toggle" "Hold to talk"; default for out of range */
void voice_settings_default(Settings *s);       /* called by config_default_settings */
void voice_settings_validate(Settings *s);      /* called by settings_validate */

#endif /* NUTSHELL_VOICE_CONFIG_H */
