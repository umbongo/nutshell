#ifndef NUTSHELL_VOICE_WIN_H
#define NUTSHELL_VOICE_WIN_H

#include <windows.h>
#include "../config/config.h"

/*
 * voice_win -- the voice feature's controller. It sets the voice gate and
 * starts and stops the audio thread to match; later revisions add the
 * push-to-talk hook and the mic button.
 */

/* The live settings, owned by window.c; NULL before the config is loaded. */
const Settings *ui_current_settings(void);

/* Bring the voice feature in line with the settings: sets the gate to
 * opt_feature_active(settings, OPT_VOICE), then starts (gate on) or stops
 * (gate off) the audio thread. Called after every Settings save,
 * from ai_feature_on/off and at startup, after the AI decisions. */
void voice_feature_apply(HWND main);

/* 1 while the feature is running (audio thread / hook live), else 0. */
int voice_feature_running(void);

/* WM_VOICE_AUDIO from window.c's WndProc; frees any payload nobody took. */
void voice_on_audio_msg(WPARAM wp, LPARAM lp);

#endif /* NUTSHELL_VOICE_WIN_H */
