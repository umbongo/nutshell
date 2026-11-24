#ifndef NUTSHELL_VOICE_STT_WIN_H
#define NUTSHELL_VOICE_STT_WIN_H

#include <windows.h>
#include <stddef.h>
#include "../config/config.h"
#include "voice_stt.h"

/*
 * voice_stt_win -- the speech-to-text upload worker. One thread per
 * recording; one attempt, no retry. UI thread only (submit, cancel_all).
 */

#define WM_VOICE_STT (WM_APP + 0x41)   /* lParam = VoiceSttResult*, receiver frees */

typedef struct {
    unsigned token;     /* the caller's id for this recording */
    int      ok;        /* 1: text holds the transcript; 0: err says why */
    char    *text;      /* heap, NUL-terminated, text_cap bytes (>= VOICE_STT_TEXT_MIN) */
    size_t   text_cap;
    char     err[300];
} VoiceSttResult;

/* Wipes the transcript and the record, then frees both. NULL is fine. The
 * receiver of WM_VOICE_STT calls this. */
void voice_stt_result_free(VoiceSttResult *r);

/* Takes ownership of wav (wiped and freed by the worker, or here on a
 * failure). Resolves the target with voice_stt_resolve(s, ...) on the
 * caller's thread and copies the key; returns 0 with err filled when
 * resolution fails or voice_gate_on() is 0 (nothing is sent). */
int  voice_stt_submit(HWND notify, const Settings *s, unsigned char *wav, size_t wav_len,
                      unsigned token, char *err, size_t errcap);

/* Fire every running upload's cancel token and join the threads (bounded
 * 3 s). Each cancelled upload still posts its failed result. */
void voice_stt_cancel_all(void);

/* Test hook: called on the worker right before its last gate check. NULL
 * outside tests. */
extern void (*voice_stt_test_pre_send_hook)(void);

#endif /* NUTSHELL_VOICE_STT_WIN_H */
