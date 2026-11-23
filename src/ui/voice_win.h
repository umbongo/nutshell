#ifndef NUTSHELL_VOICE_WIN_H
#define NUTSHELL_VOICE_WIN_H

#include <windows.h>
#include "../config/config.h"

/*
 * voice_win -- the voice feature's controller. In this revision it only sets
 * the voice gate; later revisions start and stop the audio thread, the
 * push-to-talk hook and the mic button to match.
 */

/* The live settings, owned by window.c; NULL before the config is loaded. */
const Settings *ui_current_settings(void);

/* Bring the voice feature in line with the settings: sets the gate to
 * opt_feature_active(settings, OPT_VOICE). Called after every Settings save,
 * from ai_feature_on/off and at startup, after the AI decisions. */
void voice_feature_apply(HWND main);

/* 1 while the feature is running (audio thread / hook live), else 0. */
int voice_feature_running(void);

#endif /* NUTSHELL_VOICE_WIN_H */
