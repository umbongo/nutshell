#ifndef NUTSHELL_VOICE_WIN_H
#define NUTSHELL_VOICE_WIN_H

#include <windows.h>
#include "../config/config.h"
#include "ui_theme.h"

/*
 * voice_win -- the voice feature's controller. It sets the voice gate and
 * starts and stops the audio thread to match, runs push-to-talk from the
 * message pump (pump_dispatch calls g_voice_filter), starts and ends a
 * capture, hands the recording to the STT worker and delivers the transcript
 * into the AI panel.
 */

/* A main-window timer id window.c does not use: the one-shot that ends the
 * Mic start delay or the 60 s cap. */
#define VOICE_TIMER_ID 0x56F1
/* The conversation controller's one-shot timer is VOICE_CONV_TIMER_ID (0x56F2), in
 * voice_conv_win.h. */

/* The AI panel (docked or not), or NULL; owned by window.c. */
HWND ui_ai_panel(void);

/* pump_dispatch's hook: NULL while the feature is stopped (so voice off
 * installs nothing). Returns 1 when it consumed the message. */
extern int (*g_voice_filter)(const MSG *m);
int  voice_pump_filter(const MSG *m);

/* WM_ACTIVATEAPP(FALSE) or WM_ENABLE(FALSE): end a recording (VE_DEACTIVATE). */
void voice_on_deactivate(void);

/* WM_WTSSESSION_CHANGE from window.c's WndProc: a lock ends the dictation recording
 * softly (and, from Task 13, the conversation). Any other change is ignored. */
void voice_on_session_change(WPARAM wp);

/* WM_TIMER with VOICE_TIMER_ID. */
void voice_on_timer(void);

/* The mic button was clicked. */
void voice_mic_clicked(void);

/* The live settings, owned by window.c; NULL before the config is loaded. */
const Settings *ui_current_settings(void);

/* The current theme (for the notice dialog); owned by window.c. */
const ThemeColors *ui_current_theme(void);

/* Bring the voice feature in line with the settings: sets the gate to
 * opt_feature_active(settings, OPT_VOICE), then starts (gate on) or stops
 * (gate off) the audio thread. Called after every Settings save,
 * from ai_feature_on/off and at startup, after the AI decisions. */
void voice_feature_apply(HWND main);

/* 1 while the feature is running (audio thread / hook live), else 0. */
int voice_feature_running(void);

/* Window teardown: stops the audio thread (joined, notification client
 * unregistered) whatever the settings say. Safe to call twice. */
void voice_feature_shutdown(void);

/* WM_VOICE_AUDIO from window.c's WndProc; frees any payload nobody took. */
void voice_on_audio_msg(WPARAM wp, LPARAM lp);

/* WM_VOICE_STT from window.c's WndProc: lParam is a VoiceSttResult the
 * receiver frees. A result for the current recording is delivered into the
 * panel (or dropped when the chat changed); any other is only freed. */
void voice_on_stt(LPARAM lp);

/* WM_VOICE_LIVE from window.c's WndProc: lParam is a VoiceLiveMsg the receiver
 * frees (voice_live_msg_free). A message for the current recording's live session
 * goes into the box; any other is only freed. */
void voice_on_live(LPARAM lp);

/* Posted by ai_chat when the conversation or session changes (never re-entered
 * synchronously); window.c routes WM_VOICE_CHAT to voice_on_chat_changed_msg, which
 * ends a live recording whose binding no longer matches. */
#define WM_VOICE_CHAT (WM_APP + 0x43)
void voice_on_chat_changed(void);
void voice_on_chat_changed_msg(void);

/* The IDC_CHAT_SEND handler's plain-send branch (and so Enter) asks this first: 1 when
 * a dictation recording is running or finishing and voice took the Send (it ends the
 * recording and sends once the words are in); the handler then returns without sending. */
int voice_send_pressed(void);

/* A user launch of a chat turn that did not go through the conversation (Retry, a suggestion chip,
 * a typed send while Working): a running conversation takes the reply baseline again and moves to
 * Working (voice_conv_external_send). Call it after the launch, once the chat is busy. */
void voice_on_external_send(void);

/* Posted to the main window by ai_chat when the chat is idle (nothing streaming, no
 * dispatch, no card waiting for the user), and when a card needing the user appears
 * (wParam = VoiceCardKind). The controller re-checks and ignores both when no
 * conversation runs. window.c routes them (Task 13). */
#define WM_VOICE_TURN_IDLE (WM_APP + 0x45)
#define WM_VOICE_CARD      (WM_APP + 0x46)
/* Posted by the conversation glue when the first spoken reply of a launch needs the
 * "no British voice" notice; the handler shows it (the dialog pauses the conversation). */
#define WM_VOICE_NOTICE    (WM_APP + 0x47)
/* Posted when the window is enabled again (a modal dialog closed): the conversation's
 * resume runs after the code that follows the dialog (Settings: voice_feature_apply),
 * so a voice setting saved off or changed never reopens the microphone first. */
#define WM_VOICE_ACTIVATION (WM_APP + 0x48)

/* The Send/Stop handler's Stop branch ran: a conversation ends its turn. */
void voice_on_chat_stop(void);

/* Conversation mode: window.c and the panel route these (each is a no-op with no
 * conversation running). */
void voice_on_activation(void);          /* WM_ACTIVATEAPP / WM_ENABLE, both directions */
void voice_on_activateapp(int active);   /* WM_ACTIVATEAPP's wParam, just before voice_on_activation */
void voice_on_box_changed(void);         /* EN_CHANGE of the AI input box: the conversation's draft cue */
void voice_on_tts(LPARAM lp);            /* WM_VOICE_TTS: lParam is a VoiceTtsMsg the receiver frees */
void voice_on_turn_idle(void);           /* WM_VOICE_TURN_IDLE */
void voice_on_card(WPARAM kind);         /* WM_VOICE_CARD */
void voice_on_notice(void);              /* WM_VOICE_NOTICE */
void voice_on_conv_timer(void);          /* WM_TIMER with VOICE_CONV_TIMER_ID */
void voice_on_panel_closed(void);        /* the AI panel was hidden or destroyed */

#endif /* NUTSHELL_VOICE_WIN_H */
