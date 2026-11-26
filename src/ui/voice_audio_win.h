#ifndef NUTSHELL_VOICE_AUDIO_WIN_H
#define NUTSHELL_VOICE_AUDIO_WIN_H

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "wav.h"
#include "voice_ptt.h"
#include "voice_listen.h"

/*
 * voice_audio_win -- every WASAPI object lives on ONE audio thread
 * (CoInitializeEx MTA). The UI thread only posts commands to it through a
 * small locked queue and receives WM_VOICE_AUDIO messages back; it never
 * touches an audio COM object. The thread exists only while voice is on:
 * the device enumerator is created (and mmdevapi.dll noted in the DLL
 * record) at thread start, never before.
 */

/* Messages posted to the notify HWND given at start. */
#define WM_VOICE_AUDIO (WM_APP + 0x40)   /* wParam = VoiceAudioMsg, lParam = payload */

/* Initial command-queue slots; the queue grows for begin/end, never drops them
 * (exposed so the wintest can push past it). */
#define VOICE_AUDIO_QUEUE_MIN 16

typedef enum {
    VAM_READY = 1,        /* lParam 0: thread up, enumerator created (or failed: VAM_ERROR first) */
    VAM_DEVICES,          /* lParam = VoiceDeviceList* (receiver frees with voice_audio_free_devices) */
    VAM_LEVEL,            /* lParam = raw peak 0..32768 of the last ~50 ms, after gain */
    VAM_CAPTURED,         /* lParam = VoiceCapture* (receiver frees with voice_audio_free_capture) */
    VAM_ERROR,            /* lParam = VOICE_ERR_PACK(err, token): nothing to free */
    VAM_DEVICES_CHANGED,  /* lParam 0: from IMMNotificationClient; re-enumerate */
    /* Conversation capture: lParam packs (value << 24) | (token & VOICE_TOKEN_MASK) unless noted. */
    VAM_CONV_OPENED,      /* value = echo cancellation: 0 none, 1 on, 2 turned on by us */
    VAM_CONV_EVENT,       /* value = VoiceListenEvent */
    VAM_CONV_TURN,        /* lParam = VoiceCapture* of the finished turn (TURN_END/CAP); receiver frees */
    /* AI speech render client (one owner token, R7) */
    VAM_RENDER_OPENED,    /* lParam = VOICE_ERR_PACK(0, token) */
    VAM_PLAYED,           /* lParam = VoiceRenderMsg* (receiver frees with free), every 100 ms while playing */
    VAM_PLAY_DONE,        /* lParam = VoiceRenderMsg*: reply_id's end marker played out (padding 0) */
    VAM_RENDER_ERROR      /* lParam = VOICE_ERR_PACK(err, token): VAE_DEVICE_LOST, VAE_OPEN_FAILED,
                             VAE_NO_DEVICE, VAE_NO_MEMORY, VAE_RENDER_PREEMPTED */
    /* VAM_LEVEL is posted every 50 ms during a conversation capture too (the mic pulse) */
} VoiceAudioMsg;
#define VOICE_CONV_PACK(value, token) \
    ((LPARAM)((((uintptr_t)(value) & 0xFFu) << 24) | ((uintptr_t)(token) & VOICE_TOKEN_MASK)))

typedef enum { VAE_NONE = 0, VAE_NO_DEVICE, VAE_DEVICE_LOST, VAE_OPEN_FAILED,
               VAE_COM_FAILED, VAE_NO_MEMORY, VAE_ACCESS_DENIED,
               VAE_CONV_ENDED, VAE_RENDER_PREEMPTED } VoiceAudioErr;
/* VAE_CONV_ENDED, under a conversation token: the conversation capture is gone because a
 * non-conversation command (a dictation or Test begin or end, or another conversation begin)
 * pre-empted it; its device is closed and its turn wiped. Fatal for that capture. By contrast
 * VAE_NO_MEMORY under a conversation token is per-turn: that one turn was lost, the capture
 * keeps running. A device error (VAE_DEVICE_LOST, ...) is fatal too. */
/* An error and the recording token it belongs to (0: not a push-to-talk
 * recording: a Settings Test, an enumeration or the thread itself) packed
 * into one lParam: the token's low 24 bits (VOICE_TOKEN_MASK) above the
 * error code in the low byte. Tokens wrap, so every comparison is masked. */
#define VOICE_ERR_PACK(err, token) \
    ((LPARAM)((((uintptr_t)((token) & VOICE_TOKEN_MASK)) << 8) | ((uintptr_t)(err) & 0xFFu)))
#define VOICE_ERR_CODE(lp)  ((VoiceAudioErr)((uintptr_t)(lp) & 0xFFu))
#define VOICE_ERR_TOKEN(lp) ((unsigned)(((uintptr_t)(lp) >> 8) & VOICE_TOKEN_MASK))

typedef struct { int n; struct { wchar_t id[256]; char name[256]; } dev[32]; } VoiceDeviceList;
typedef struct { PcmBuf pcm; PcmVerdict verdict; int used_default; unsigned token; } VoiceCapture;

int  voice_audio_start(HWND notify);          /* creates the MTA thread; 0 on failure or while voice is off (voice_gate_on); 1 if already running */
void voice_audio_stop(void);                  /* stop event, join (bounded 3 s), release all */
int  voice_audio_wait_ready(DWORD ms);        /* 1 once VAM_READY was reached */
void voice_audio_enumerate(void);             /* -> VAM_DEVICES */
/* cap_ms: this capture's own length cap (<= 0: VOICE_CAP_MS); live: 1 feeds every kept
 * sample to voice_live_feed(token, ...) and ends it with voice_live_feed_end at the
 * finish (ignored for a test_only capture). */
void voice_audio_capture_begin(const char *device_id_utf8, unsigned token, int test_only, int gain_db,
                               int cap_ms, int live);
void voice_audio_set_gain(int gain_db);       /* software gain in dB for the running capture; coalesced, applied on the audio thread */
void voice_audio_capture_end(int keep);       /* keep=1 -> VAM_CAPTURED; 0 -> discard (wiped) */
void voice_audio_watch_devices(int on);       /* register/unregister the notification client */
void voice_audio_test_hold(int on);           /* TEST-ONLY seam, not for product code:  while on, the audio thread pops no commands; off wakes it */
int  voice_audio_queue_cap(void);             /* TEST-ONLY seam, not for product code: current command-queue capacity, 0 with no thread */
int  voice_audio_test_pending_gain(int *last_db); /* TEST-ONLY seam: queued gain commands (<=1) and the last value */
void voice_audio_test_ingest(int test_only, int gain_db, int gain2_db, const int16_t *s, size_t n,
                             int peak[2], size_t *pcm_len, int *verdict); /* TEST-ONLY seam: the ingest path, no device */
/* TEST-ONLY seam: replaces voice_live_feed / voice_live_feed_end (both NULL in product code). */
void voice_audio_test_live_hooks(void (*feed)(unsigned token, const int16_t *s, size_t n),
                                 void (*end)(unsigned token));
/* TEST-ONLY seam: the ingest path of one capture (cap_ms, live, token, gain) on a private state
 * (no device), then its finish. */
void voice_audio_test_ingest_live(int live, unsigned token, int gain_db, int cap_ms,
                                  const int16_t *s, size_t n);
/* Conversation capture (spec 5.3, 7.3): the device is opened in the communications category
 * (echo cancellation when the endpoint offers it) and every captured buffer, after the gain,
 * goes to a VoiceListen on the audio thread. Posts VAM_CONV_OPENED or VAM_ERROR for token.
 * No 60 s / 10 min cap applies; the listener caps each turn. gain_db is rounded and clamped. */
int  voice_audio_conv_begin(const char *device_id_utf8, unsigned token, double gain_db,
                            int pause_ms);
void voice_audio_conv_mode(unsigned token, VoiceListenMode m);   /* HOLD wipes a turn in progress */
void voice_audio_conv_attach_live(unsigned token, unsigned live_token);  /* turn so far is replayed */
void voice_audio_conv_end(unsigned token);    /* close the device, wipe the turn */
/* End the open turn now, as the endpointer's pause would: VAM_CONV_EVENT TURN_END, the live
 * session's end, then VAM_CONV_TURN with the audio kept. No-op with no open turn or a stale
 * token. (Send pressed while hearing.) */
void voice_audio_conv_end_turn(unsigned token);
/* TEST-ONLY seams (no device): open a conversation state on the audio thread, and feed it
 * samples as the capture path would (gain applied, then the listener). */
void voice_audio_test_conv_open(unsigned token, int pause_ms);
void voice_audio_test_conv_ingest(unsigned token, const int16_t *s, size_t n);

/* AI speech render client (spec: 24 kHz mono 16-bit, shared mode, 100 ms buffer) on the audio
 * thread, fed from a VoicePlayQ. Every call is a queued command under the caller's token; a
 * stale token is ignored. The resource ledger is the caller's business (UI thread). */
typedef struct { unsigned token, reply_id, chunk; uint64_t frames_in_chunk; } VoiceRenderMsg;
/* One client, one owner: gain belongs to the current owner only. render_gain from a token that
 * does not own the open client is ignored, and every render_open starts at 100 %, so a caller
 * ducks AFTER its open (Task 7 sends its gain after render_open, never before). */
int  voice_audio_render_open(unsigned token);       /* push; 1 queued, 0 no thread */
int  voice_audio_render_data(unsigned token, unsigned reply_id, unsigned chunk,
                             int16_t *s, size_t n);  /* takes s on 1; 0: caller still owns s */
void voice_audio_render_end(unsigned token, unsigned reply_id);
void voice_audio_render_flush(unsigned token, unsigned min_reply);   /* cannot be lost */
void voice_audio_render_gain(unsigned token, int percent);
void voice_audio_render_close(unsigned token);                       /* cannot be lost */
long voice_audio_render_queued_ms(void);           /* any thread, interlocked; 0 with none */
/* TEST-ONLY seams, never product code: */
void voice_audio_test_render_null(int on);         /* a 10 ms waitable timer and a simulated
                                                      100 ms buffer draining in real time stand
                                                      in for WASAPI (also resets the counters) */
uint64_t voice_audio_test_render_frames(unsigned reply_id);  /* frames written for reply_id */
int  voice_audio_test_render_peak(void);           /* largest |sample| the null sink took since the last call */
void voice_audio_test_fail_push(int n);            /* the next n raw pushes fail as on OOM */

void voice_audio_free_devices(VoiceDeviceList *l);
void voice_audio_free_capture(VoiceCapture *c);  /* pcm_free (secure wipe) + free */

#ifdef NS_VOICE_WAV_SEAM
/* The harness build only (make harness-exe): a 16 kHz mono 16-bit PCM WAV replaces WASAPI for
 * every capture, dictation and conversation. "" clears it. */
void voice_audio_seam_set_wav(const char *path);
/* Appends a conversation header line to "<wav>.header" (the harness reads it; the header is painted,
 * not a window text). A no-op with no WAV set. */
void voice_audio_seam_note_header(const char *text);
#endif

#endif /* NUTSHELL_VOICE_AUDIO_WIN_H */
