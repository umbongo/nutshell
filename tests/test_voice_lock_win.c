/* tests/test_voice_lock_win.c -- the session-lock registration. Built only by
 * `make wintest`. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include "test_framework.h"
#include "voice_lock_win.h"
#include "voice_gate.h"
#include "voice_ledger.h"
#include "ai_gate.h"

int test_voice_lock_register_balances(void)
{
    TEST_BEGIN();
    voice_ledger_reset();
    HWND w = CreateWindowExW(0, L"STATIC", L"lock-test", WS_POPUP, 0, 0, 1, 1,
                             NULL, NULL, GetModuleHandleW(NULL), NULL);
    ASSERT_TRUE(w != NULL);

    /* gate off: nothing registers, nothing is counted */
    ai_gate_set_enabled(0);
    voice_gate_set_enabled(0);
    ASSERT_EQ(voice_lock_register(w), 0);
    ASSERT_EQ(voice_lock_registered(), 0);
    ASSERT_EQ(voice_ledger_count(VRES_SESSION_LOCK), 0);

    /* gate on */
    ai_gate_set_enabled(1);
    voice_gate_set_enabled(1);
    const int ok = voice_lock_register(w);
    if (!ok) {
        /* Wine may refuse the registration: a defined outcome, not a failure. */
        printf("  SKIP: WTSRegisterSessionNotification refused; count check skipped\n");
        ASSERT_EQ(voice_ledger_count(VRES_SESSION_LOCK), 0);
        ASSERT_EQ(voice_lock_registered(), 0);
    } else {
        ASSERT_EQ(voice_lock_registered(), 1);
        ASSERT_EQ(voice_ledger_count(VRES_SESSION_LOCK), 1);
        ASSERT_EQ(voice_lock_register(w), 1);              /* idempotent */
        ASSERT_EQ(voice_ledger_count(VRES_SESSION_LOCK), 1);
        voice_lock_unregister(w);
        ASSERT_EQ(voice_lock_registered(), 0);
        ASSERT_EQ(voice_ledger_count(VRES_SESSION_LOCK), 0);
        voice_lock_unregister(w);                          /* idempotent */
        ASSERT_EQ(voice_ledger_count(VRES_SESSION_LOCK), 0);
    }
    ASSERT_EQ(voice_ledger_underflow(), 0);

    voice_gate_set_enabled(0);
    ai_gate_set_enabled(0);
    DestroyWindow(w);
    TEST_END();
}

int test_voice_lock_is_lock_event(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_lock_is_lock_event((WPARAM)7), 1);   /* WTS_SESSION_LOCK */
    ASSERT_EQ(voice_lock_is_lock_event((WPARAM)8), 0);   /* WTS_SESSION_UNLOCK */
    ASSERT_EQ(voice_lock_is_lock_event((WPARAM)2), 1);   /* WTS_CONSOLE_DISCONNECT: fail safe */
    ASSERT_EQ(voice_lock_is_lock_event((WPARAM)4), 1);   /* WTS_REMOTE_DISCONNECT */
    ASSERT_EQ(voice_lock_is_lock_event((WPARAM)3), 0);   /* WTS_REMOTE_CONNECT */
    ASSERT_EQ(voice_lock_is_lock_event((WPARAM)1), 0);   /* WTS_CONSOLE_CONNECT */
    ASSERT_EQ(voice_lock_is_lock_event((WPARAM)0), 0);
    TEST_END();
}

#endif /* _WIN32 */
