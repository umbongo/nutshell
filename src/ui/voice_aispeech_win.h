#ifndef NUTSHELL_VOICE_AISPEECH_WIN_H
#define NUTSHELL_VOICE_AISPEECH_WIN_H

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "voice_aispeech.h"
#include "ai_netcancel.h"

/*
 * voice_aispeech_win -- the AI speech engine: spoken replies from a speech provider, played
 * through the render client of voice_audio_win. At most two contexts, the conversation's and
 * the Play sample's. Each speak runs one worker thread (VRES_SPEECH_REQ) that cuts the reply
 * into voice_speak_chunks, fetches each chunk through the installed fetch and pushes the
 * decoded 24 kHz mono PCM16 into the render queue. The engine posts the same WM_VOICE_TTS
 * messages as the SAPI engine (VTT_WORD, VTT_DONE, VTT_ERROR, with a byte offset and, for an
 * error, a VOICE_AIS_HR_* or HTTP status in hr), so the conversation glue treats both alike.
 * It posts no VTT_READY and no VTT_EXITED: the worker's end is its own WM_VOICE_AIS message.
 *
 * Threads. The UI thread owns the contexts, the ledger and every Win32 object; a worker only
 * touches its job, under the engine's one lock. Every worker has a dead flag (R36) and a
 * cancel flag, both read under that lock before every render push and every PostMessage; a
 * quit sets dead first. VAI_EXITED is the worker's last act and is the one message that does
 * not look at the flags.
 *
 * Ledger (UI thread): VRES_RENDER while a context holds the render client (acquired when it
 * pushes render_open, released with render_close or on a pre-emption or render error);
 * VRES_SPEECH_REQ from a worker's creation until its thread has ended (its VAI_EXITED, or the
 * join in quit).
 */

#define WM_VOICE_AIS (WM_APP + 0x49)      /* lParam = VoiceAisMsg*, receiver frees */
typedef enum { VAI_CHUNK_DONE = 1, VAI_FAILED, VAI_EXITED } VoiceAisKind;
/* CHUNK_DONE: chunk and its total frames. FAILED: chunk, hr, and the frames it did push.
 * EXITED: the worker has finished. `serial` is the posting job's; the UI drops a message whose
 * serial is not its context's current one (a cancelled earlier speak). `reply_id` is the
 * engine's internal render id of that speak, not the caller's reply id. */
typedef struct { unsigned token, reply_id, chunk; VoiceAisKind kind;
                 uint64_t frames; long hr; unsigned serial; } VoiceAisMsg;

#define VOICE_AIS_SAMPLE_TOKEN 0x7FFFFFF2u
#define VOICE_AIS_TIMER_BASE   0x56F4      /* + slot 0 or 1: the done guard of each context */
#define VOICE_AIS_TIMER_COUNT  2           /* slots: the conversation's and the Play sample's */
#define VOICE_AIS_DONE_GUARD_MS 2000

/* One chunk's fetch, run on the worker thread: push decoded 24 kHz mono PCM16 through push()
 * (0 = stop: dead or cancelled), return 0 ok or a VOICE_AIS_HR_* / HTTP status. */
typedef int  (*VoiceAisPush)(void *job, const int16_t *s, size_t n);
typedef long (*VoiceAisFetch)(void *job, const VoiceAisTarget *t, const char *key,
                              const char *text, size_t len, VoiceAisPush push);

/* The one context constructor: 0 unless voice_gate_on() and the audio thread is up; 1 when
 * started or already running under this token. Posts nothing, opens nothing. */
int  voice_ais_start(HWND notify, unsigned token);
/* Cancels what the context is saying, flushes its queued audio, opens the render client when
 * the context does not hold it, then speaks utf8[from..]. An empty t->url posts
 * VTT_ERROR(VOICE_AIS_HR_RESOLVE) at `from` at once. */
void voice_ais_speak(unsigned token, unsigned reply_id, const char *utf8, size_t from,
                     const VoiceAisTarget *t, const char *key);
void voice_ais_stop(unsigned token);                 /* nothing is posted for what is dropped */
void voice_ais_volume(unsigned token, int percent);  /* kept across opens */
void voice_ais_quit(unsigned token, unsigned wait_ms);
void voice_ais_quit_all(unsigned wait_ms);           /* before voice_audio_stop (R36) */
/* Play sample: start the sample context and speak "Hello. This is how replies will sound.";
 * it quits itself after its VTT_DONE or VTT_ERROR. 0 when it cannot start. */
int  voice_ais_sample(HWND notify, const VoiceAisTarget *t, const char *key);
int  voice_ais_running(void);                        /* contexts held */
int  voice_ais_owns(unsigned token);                 /* 1 when an AI context holds this token */

/* The window procedure's hooks. on_audio takes ownership of (frees) the VoiceRenderMsg of
 * VAM_PLAYED and VAM_PLAY_DONE and ignores every other kind. on_msg does not free m. */
void voice_ais_on_audio(UINT kind, LPARAM lp);
void voice_ais_on_msg(VoiceAisMsg *m);
void voice_ais_on_timer(UINT_PTR id);

/* voice_ais_start installs the real fetch (Mistral and Custom over WinHTTP) when none is set;
 * tests install fakes; NULL with no context started = none (every speak fails NET). */
void voice_ais_set_fetch(VoiceAisFetch f);
/* The job's cancel token, fired by every cancel and quit: the real fetch hands it to its request
 * so a stop closes a blocked WinHTTP call at once. `job` is the fetch's first argument. */
AiNetCancel *voice_ais_job_nc(void *job);

/* TEST-ONLY seam, never product code: the cap on chunks per reply (0 restores the default). */
void voice_ais_test_max_chunks(size_t n);

#endif /* NUTSHELL_VOICE_AISPEECH_WIN_H */
