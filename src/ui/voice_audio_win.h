#ifndef NUTSHELL_VOICE_AUDIO_WIN_H
#define NUTSHELL_VOICE_AUDIO_WIN_H

#include <windows.h>
#include "wav.h"
#include "voice_ptt.h"

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

typedef enum {
    VAM_READY = 1,        /* lParam 0: thread up, enumerator created (or failed: VAM_ERROR first) */
    VAM_DEVICES,          /* lParam = VoiceDeviceList* (receiver frees with voice_audio_free_devices) */
    VAM_LEVEL,            /* lParam = level 0..1000 (peak of the last ~50 ms) */
    VAM_CAPTURED,         /* lParam = VoiceCapture* (receiver frees with voice_audio_free_capture) */
    VAM_ERROR,            /* lParam = VOICE_ERR_PACK(err, token): nothing to free */
    VAM_DEVICES_CHANGED   /* lParam 0: from IMMNotificationClient; re-enumerate */
} VoiceAudioMsg;

typedef enum { VAE_NONE = 0, VAE_NO_DEVICE, VAE_DEVICE_LOST, VAE_OPEN_FAILED,
               VAE_COM_FAILED, VAE_NO_MEMORY, VAE_ACCESS_DENIED } VoiceAudioErr;
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
void voice_audio_capture_begin(const char *device_id_utf8, unsigned token, int test_only);
void voice_audio_capture_end(int keep);       /* keep=1 -> VAM_CAPTURED; 0 -> discard (wiped) */
void voice_audio_watch_devices(int on);       /* register/unregister the notification client */
void voice_audio_free_devices(VoiceDeviceList *l);
void voice_audio_free_capture(VoiceCapture *c);  /* pcm_free (secure wipe) + free */

#endif /* NUTSHELL_VOICE_AUDIO_WIN_H */
