#include "voice_lock_win.h"

#include <wtsapi32.h>
#include "voice_gate.h"
#include "voice_ledger.h"

#ifndef WTS_SESSION_LOCK
#define WTS_SESSION_LOCK 0x7
#endif

static HWND g_lock_hwnd;   /* the window registered, or NULL (UI thread only) */

int voice_lock_register(HWND main)
{
    if (!voice_gate_on() || main == NULL) return 0;
    if (g_lock_hwnd != NULL) return 1;
    if (!WTSRegisterSessionNotification(main, NOTIFY_FOR_THIS_SESSION)) return 0;
    g_lock_hwnd = main;
    voice_ledger_acquire(VRES_SESSION_LOCK);
    return 1;
}

void voice_lock_unregister(HWND main)
{
    (void)main;
    if (g_lock_hwnd == NULL) return;
    (void)WTSUnRegisterSessionNotification(g_lock_hwnd);
    g_lock_hwnd = NULL;
    voice_ledger_release(VRES_SESSION_LOCK);
}

int voice_lock_registered(void)
{
    return g_lock_hwnd != NULL;
}

int voice_lock_is_lock_event(WPARAM wp)
{
    return wp == (WPARAM)WTS_SESSION_LOCK;
}
