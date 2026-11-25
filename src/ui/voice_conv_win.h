#ifndef NUTSHELL_VOICE_CONV_WIN_H
#define NUTSHELL_VOICE_CONV_WIN_H

#include <stddef.h>
#include <windows.h>
#include "voice_conv.h"
#include "voice_audio_win.h"
#include "voice_live_win.h"
#include "voice_stt_win.h"
#include "voice_tts_win.h"

/*
 * voice_conv_win -- the glue between the pure conversation machine
 * (src/core/voice_conv.c) and the Win32 modules. It feeds events to the machine,
 * carries out each returned action through an ops table, keeps the transcript of
 * the turn being heard and the last reply's speech text, arms one timer, and
 * pauses the conversation while a dialog is open or Nutshell is in the
 * background. UI thread only.
 *
 * The real ops table lives in voice_win.c (it talks to the AI panel and the
 * audio, live, upload and speech modules) and is registered with
 * voice_conv_attach(); the wintest installs a fake one with voice_conv_set_ops().
 * Everything here is idempotent and drops an event that arrives after its moment
 * (a stale token, a message for a turn that is over).
 */

#define VOICE_CONV_TIMER_ID 0x56F2

typedef struct {     /* every effect the glue has; the real table calls the modules above */
    int  (*cap_begin)(unsigned token);  void (*cap_end)(unsigned token);
    void (*cap_mode)(unsigned token, VoiceListenMode m);
    void (*cap_attach_live)(unsigned token, unsigned live_token);
    void (*cap_end_turn)(unsigned token);   /* end the open turn now, as the pause would (Send while hearing) */
    int  (*link_start)(unsigned live_token); void (*link_close)(unsigned live_token);
    int  (*stt_submit)(VoiceCapture *turn, unsigned token); void (*stt_cancel)(unsigned token);
    int  (*tts_start)(unsigned token);  void (*tts_speak)(unsigned token, unsigned id,
                                                          const char *text, size_t from);
    void (*tts_stop)(unsigned token);   void (*tts_volume)(unsigned token, int percent);
    void (*tts_quit)(unsigned token);
    int  (*lock_reg)(void);             void (*lock_unreg)(void);
    /* chat */
    VoiceSendStatus (*send_turn)(int voice_turn);
    unsigned (*msg_count)(void);
    int  (*reply)(unsigned since, char **text, int *is_error);
    int  (*chat_idle)(void);
    void (*cancel_work)(void);
    void (*note)(const char *text);     void (*header)(const char *text);
    void (*live_begin)(void); void (*live_replace)(const char *text);
    void (*drop_words)(void); void (*lock_typing)(int on);
    void (*send_caption)(int stop);     void (*mic_ui)(int on);
    int  (*box_has_text)(void);
    int  (*notice_voice)(const char *voice_name);   /* shows the once-per-launch notice */
    /* Optional (NULL is fine): */
    int  (*notice_due)(char *voice_name, size_t cap);   /* 1 when the notice is due; names the voice */
    void (*level)(double frac);                         /* the mic pulse follows the level, 0..1 */
    int  (*app_active)(void);                           /* 0 when another application has the foreground */
    int  (*dialog_open)(void);                          /* 1 when a dialog owned by the (undocked) AI panel is up */
    int  (*chat_same)(void);                            /* 0 when the panel's chat is no longer the conversation's */
} VoiceConvOps;

/* Registers the real table and the main window (timer, pause checks, WM_VOICE_NOTICE).
 * Called again with the same arguments is fine. */
void voice_conv_attach(HWND main, const VoiceConvOps *real);
/* Installs `ops` and resets the glue to nothing running (tests); NULL restores the real
 * table. */
void voice_conv_set_ops(const VoiceConvOps *ops);

int  voice_conv_running(void);
VoiceConvState voice_conv_state(void);
void voice_conv_start(int live);                    /* from voice_win.c after the busy checks */
void voice_conv_event(VoiceConvEvent ev, int a, int b);  /* feed + carry out, with now = GetTickCount */
void voice_conv_on_audio(UINT kind, LPARAM lp);     /* VAM_CONV_* and VAM_LEVEL/VAM_ERROR for it */
void voice_conv_on_live(const VoiceLiveMsg *m);     /* WM_VOICE_LIVE for the turn's token */
void voice_conv_on_stt(VoiceSttResult *r);          /* WM_VOICE_STT for the turn's token */
void voice_conv_on_tts(const VoiceTtsMsg *m);
void voice_conv_on_timer(void);                     /* VOICE_CONV_TIMER_ID */
void voice_conv_on_activation(void);                /* recompute paused/active */
void voice_conv_note_activateapp(int active);       /* WM_ACTIVATEAPP's wParam: the activation hint */
void voice_conv_on_box_changed(void);               /* the input box's text changed: the draft cue */
/* The user-facing text of a capture error (also what dictation shows). */
const char *voice_conv_device_text(VoiceAudioErr e);
void voice_conv_on_turn_idle(void);                 /* WM_VOICE_TURN_IDLE */
void voice_conv_on_card(int kind);                  /* WM_VOICE_CARD, kind = VoiceCardKind */
void voice_conv_on_notice(void);                    /* WM_VOICE_NOTICE: shows the notice */
int  voice_conv_last_aec(void);                     /* -1 unknown, 0 none, 1 or 2 present */
int  voice_conv_is_typing_locked(void);             /* the brief's voice_conv_typing_locked(): renamed, the core has that name */
unsigned voice_conv_capture_token(void);            /* the open conversation capture's token, 0 none */

/* TEST-ONLY seam: moves the glue's clock forward (the machine's 3-minute and 30-second rules). */
void voice_conv_test_advance(unsigned long ms);
/* TEST-ONLY seam: the bytes of the turn's audio the glue still holds (0 when none). */
size_t voice_conv_test_turn_audio_bytes(void);

#endif /* NUTSHELL_VOICE_CONV_WIN_H */
