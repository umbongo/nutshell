#ifndef NUTSHELL_VOICE_LOCK_WIN_H
#define NUTSHELL_VOICE_LOCK_WIN_H

#include <windows.h>

/*
 * voice_lock_win -- session-lock notifications for a recording. wtsapi32.dll is
 * delay-loaded, so it is only asked for when a recording starts. A lock ends the
 * recording (the window receives WM_WTSSESSION_CHANGE).
 */

/* 0 unless voice_gate_on(); WTSRegisterSessionNotification(NOTIFY_FOR_THIS_SESSION);
 * acquires VRES_SESSION_LOCK on success and returns 1; idempotent (1 when already
 * registered). */
int  voice_lock_register(HWND main);

/* Releases it; idempotent. */
void voice_lock_unregister(HWND main);

int  voice_lock_registered(void);

/* WTS_SESSION_LOCK (7). */
int  voice_lock_is_lock_event(WPARAM wp);

#endif /* NUTSHELL_VOICE_LOCK_WIN_H */
