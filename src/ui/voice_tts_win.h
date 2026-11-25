#ifndef NUTSHELL_VOICE_TTS_WIN_H
#define NUTSHELL_VOICE_TTS_WIN_H

#include <windows.h>
#include <stddef.h>

/*
 * voice_tts_win -- spoken replies through SAPI (ISpVoice), on one speech thread
 * per conversation (a Play sample is its own short-lived one). sapi.dll is
 * never imported: the SpVoice object is created at run time by
 * CoCreateInstance on the speech thread, and only from voice_tts_start, which
 * is the single constructor and refuses unless voice_gate_on(). The thread is
 * MTA. Commands go through a locked queue and an event; the thread waits on
 * that event and on SAPI's notify event.
 *
 * Messages (WM_VOICE_TTS, lParam = VoiceTtsMsg* from malloc; the receiver
 * frees it) carry the token of the start that produced them, so a stale
 * message from a finished speaker is recognisable. Ledger (UI thread):
 * VRES_SPVOICE from a successful voice_tts_start until voice_tts_quit has
 * joined (or detached) the thread.
 *
 * The GUIDs (CLSID_SpVoice, CLSID_SpObjectToken, IID_ISpVoice,
 * IID_ISpObjectToken) come from the MinGW sapi.h (sapi53.h), defined through
 * <initguid.h> in voice_tts_win.c.
 */

#define WM_VOICE_TTS (WM_APP + 0x44)    /* lParam = VoiceTtsMsg*, receiver frees */

typedef enum { VTT_READY = 1, VTT_WORD, VTT_DONE, VTT_ERROR, VTT_EXITED } VoiceTtsKind;

typedef struct {
    unsigned token;
    VoiceTtsKind kind;
    unsigned reply_id;
    size_t offset;   /* VTT_WORD: UTF-8 byte offset into the text given to speak */
    long hr;         /* VTT_ERROR: the failing HRESULT. VTT_READY: 0, or the failing
                      * HRESULT of selecting the requested voice (the default
                      * voice is then kept). */
} VoiceTtsMsg;

#define VOICE_TTS_SAMPLE_TOKEN   0x7FFFFFF0u
#define VOICE_TTS_SELFTEST_TOKEN 0x7FFFFFF1u
#define VOICE_TTS_DUCK_PERCENT   18   /* about -15 dB */

/* The one SpVoice constructor: 0 unless voice_gate_on() (or on failure to
 * start the thread); 1 when started or already running under this token.
 * Posts VTT_READY or VTT_ERROR (then VTT_EXITED). An empty or unknown
 * voice_id selects the system default voice. */
int  voice_tts_start(HWND notify, unsigned token, const char *voice_id);

/* Purges what is playing, then speaks utf8[from..] in voice_speak_chunks order.
 * VTT_WORD offsets are absolute into utf8; VTT_DONE (same reply_id) follows the
 * last chunk. A purged reply never posts VTT_DONE. */
void voice_tts_speak(unsigned token, unsigned reply_id, const char *utf8, size_t from);

/* Purge everything queued and playing; nothing is posted for it. */
void voice_tts_stop(unsigned token);

/* ISpVoice::SetVolume, 0..100. */
void voice_tts_volume(unsigned token, int percent);

/* Stop, release SpVoice, join (bounded by wait_ms; the thread then detaches
 * and frees itself). Releases VRES_SPVOICE. Idempotent per token. */
void voice_tts_quit(unsigned token, unsigned wait_ms);

int  voice_tts_running(void);   /* 1 while any speech thread is held */

/* Play sample: start (VOICE_TTS_SAMPLE_TOKEN), speak "Hello. This is how replies
 * will sound.", and the thread quits itself after VTT_DONE or VTT_ERROR; the UI
 * reaps it with voice_tts_quit on VTT_EXITED. */
int  voice_tts_sample(HWND notify, const char *voice_id);

/* --self-report control: create SpVoice and keep it (1), or 0 when voice is off
 * or SAPI is unavailable. Release with voice_tts_selftest_release. */
int  voice_tts_selftest_hold(void);
void voice_tts_selftest_release(void);

/* UTF-8 to UTF-16 with a position map, for word-boundary offsets: writes up to
 * `wcap` wide chars (no terminator counted) and map[i] = the UTF-8 byte offset
 * of wide char i (map has wcap+1 slots; map[n] = len). An invalid byte becomes
 * U+FFFD (one byte). Returns the wide length n (<= wcap). */
size_t voice_tts_widen(const char *s, size_t len, wchar_t *w, size_t *map, size_t wcap);

#endif /* NUTSHELL_VOICE_TTS_WIN_H */
