/* tests/test_voice_audio_win.c -- the audio thread of voice input.
 * Built only by `make wintest`. Wine on a build host usually has no capture
 * endpoint, so each test accepts the defined "no device" outcome; what none
 * may do is hang or post nothing. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>
#include "test_framework.h"
#include "voice_audio_win.h"
#include "voice_gate.h"
#include "ai_gate.h"
#include "dll_record.h"

static LRESULT CALLBACK va_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static HWND va_window(void)
{
    static int registered;
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = va_wndproc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "NsVoiceAudioTest";
        RegisterClassA(&wc);
        registered = 1;
    }
    return CreateWindowExA(0, "NsVoiceAudioTest", "", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
}

/* Pump until a WM_VOICE_AUDIO with a wParam in `want` arrives (others are
 * freed and counted) or `ms` pass. Returns 1 and the message in wp and lp. */
static int va_wait(unsigned want_mask, DWORD ms, WPARAM *wp, LPARAM *lp)
{
    ULONGLONG end = GetTickCount64() + ms;
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_VOICE_AUDIO) {
                unsigned bit = 1u << (unsigned)msg.wParam;
                if (bit & want_mask) {
                    *wp = msg.wParam;
                    *lp = msg.lParam;
                    return 1;
                }
                if (msg.wParam == VAM_DEVICES)
                    voice_audio_free_devices((VoiceDeviceList *)msg.lParam);
                else if (msg.wParam == VAM_CAPTURED)
                    voice_audio_free_capture((VoiceCapture *)msg.lParam);
                else if (msg.wParam == VAM_ERROR)
                    voice_audio_free_error((VoiceAudioErrMsg *)msg.lParam);
            }
        }
        Sleep(10);
    }
    return 0;
}

#define BIT(m) (1u << (unsigned)(m))

/* voice_audio_start() refuses while voice is off: every test that starts the
 * thread turns both gates on first and off again after, and begins from no
 * thread at all, so one failing test cannot leak its thread into the next. */
static void va_gates(int on)
{
    ai_gate_set_enabled(on);
    voice_gate_set_enabled(on);
}

static void va_begin_test(void)
{
    voice_audio_stop();
    va_gates(1);
}

static void va_end_test(HWND w)
{
    voice_audio_stop();
    va_gates(0);
    if (w) DestroyWindow(w);
}

/* Threads of this process (Toolhelp), to prove a start made one thread. */
static int va_thread_count(void)
{
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == GetCurrentProcessId()) n++;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return n;
}

int test_voice_audio_start_stop(void)
{
    TEST_BEGIN();
    va_begin_test();
    dll_record_reset();
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    ASSERT_TRUE(dll_record_was_requested("mmdevapi.dll"));
    ULONGLONG t0 = GetTickCount64();
    voice_audio_stop();
    ASSERT_TRUE(GetTickCount64() - t0 < 3500);
    voice_audio_stop();   /* a second stop is harmless */
    ASSERT_EQ(voice_audio_wait_ready(10), 0);
    va_end_test(w);
    TEST_END();
}

int test_voice_audio_start_refused_when_gate_off(void)
{
    TEST_BEGIN();
    voice_audio_stop();
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
    for (int mode = 0; mode < 3; mode++) {
        /* voice off, AI off; voice on but AI off; AI on but voice off */
        ai_gate_set_enabled(mode == 2);
        voice_gate_set_enabled(mode == 1);
        dll_record_reset();
        int threads = va_thread_count();
        ASSERT_EQ(voice_audio_start(w), 0);
        ASSERT_EQ(voice_audio_wait_ready(10), 0);          /* no thread was made */
        ASSERT_FALSE(dll_record_was_requested("mmdevapi.dll"));
        ASSERT_EQ(va_thread_count(), threads);
        /* commands with no thread are dropped, never queued for a later one */
        voice_audio_capture_begin("", 1, 0);
        voice_audio_enumerate();
    }
    va_gates(0);
    DestroyWindow(w);
    TEST_END();
}

int test_voice_audio_double_start_is_one_thread(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    int t0 = va_thread_count();
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    int t1 = va_thread_count();
    ASSERT_EQ(voice_audio_start(w), 1);       /* the same running state */
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    int t2 = va_thread_count();
    ASSERT_EQ(t2, t1);                        /* no second thread */
    ASSERT_TRUE(t1 > t0);                     /* and the first did start one */
    va_end_test(w);
    TEST_END();
}

int test_voice_audio_restart(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_start(w), 1);   /* already running: harmless */
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_stop();
    ASSERT_EQ(voice_audio_start(w), 1);   /* a fresh thread with its own state */
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_enumerate();
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_TRUE(va_wait(BIT(VAM_DEVICES) | BIT(VAM_ERROR), 5000, &wp, &lp));
    if (wp == VAM_DEVICES) voice_audio_free_devices((VoiceDeviceList *)lp);
    va_end_test(w);
    voice_audio_stop();
    TEST_END();
}

int test_voice_audio_enumerate_defined(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_enumerate();
    WPARAM wp = 0; LPARAM lp = 0;
    int got = va_wait(BIT(VAM_DEVICES) | BIT(VAM_ERROR), 5000, &wp, &lp);
    ASSERT_TRUE(got);
    if (got && wp == VAM_DEVICES) {
        VoiceDeviceList *l = (VoiceDeviceList *)lp;
        ASSERT_TRUE(l->n >= 0 && l->n <= 32);
        printf("    [voice_audio] %d capture endpoint(s)\n", l->n);
        voice_audio_free_devices(l);
    } else if (got) {
        VoiceAudioErrMsg *e = (VoiceAudioErrMsg *)lp;
        printf("    [voice_audio] enumerate error %d\n", (int)e->err);
        ASSERT_TRUE(e->err == VAE_COM_FAILED || e->err == VAE_NO_DEVICE);
        ASSERT_EQ((int)e->token, 0);          /* not a recording's error */
        voice_audio_free_error(e);
    }
    va_end_test(w);
    TEST_END();
}

int test_voice_audio_capture_no_device_or_data(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_capture_begin("", 7, 0);
    Sleep(300);
    voice_audio_capture_end(1);
    WPARAM wp = 0; LPARAM lp = 0;
    int got = va_wait(BIT(VAM_CAPTURED) | BIT(VAM_ERROR), 5000, &wp, &lp);
    ASSERT_TRUE(got);
    if (got && wp == VAM_ERROR) {
        VoiceAudioErrMsg *e = (VoiceAudioErrMsg *)lp;
        printf("    [voice_audio] capture error %d (no usable endpoint)\n", (int)e->err);
        ASSERT_TRUE(e->err == VAE_NO_DEVICE || e->err == VAE_OPEN_FAILED
                    || e->err == VAE_DEVICE_LOST || e->err == VAE_ACCESS_DENIED);
        ASSERT_EQ((int)e->token, 7);          /* the error names its recording */
        voice_audio_free_error(e);
    } else if (got) {
        VoiceCapture *c = (VoiceCapture *)lp;
        ASSERT_EQ((int)c->token, 7);
        printf("    [voice_audio] captured %u samples\n", (unsigned)c->pcm.n);
        ASSERT_TRUE(c->pcm.n <= c->pcm.max);
        voice_audio_free_capture(c);
    }
    va_end_test(w);
    TEST_END();
}

int test_voice_audio_bad_id_falls_back(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_capture_begin("{bogus}", 3, 0);
    Sleep(300);
    voice_audio_capture_end(1);
    WPARAM wp = 0; LPARAM lp = 0;
    int got = va_wait(BIT(VAM_CAPTURED) | BIT(VAM_ERROR), 5000, &wp, &lp);
    ASSERT_TRUE(got);
    if (got && wp == VAM_CAPTURED) {
        VoiceCapture *c = (VoiceCapture *)lp;
        ASSERT_EQ(c->used_default, 1);
        voice_audio_free_capture(c);
    } else if (got) {
        /* no endpoint at all: the default is missing too, which is exactly
         * "no device" (a present but unopenable default would be OPEN_FAILED,
         * and has to be looked at, not accepted here) */
        VoiceAudioErrMsg *e = (VoiceAudioErrMsg *)lp;
        printf("    [voice_audio] bogus id, error %d\n", (int)e->err);
        ASSERT_EQ((int)e->err, (int)VAE_NO_DEVICE);
        ASSERT_EQ((int)e->token, 3);
        voice_audio_free_error(e);
    }
    va_end_test(w);
    TEST_END();
}

int test_voice_audio_never_started(void)
{
    TEST_BEGIN();
    va_gates(0);
    HWND w = va_window();
    voice_audio_stop();
    voice_audio_enumerate();
    voice_audio_capture_begin("", 1, 0);
    voice_audio_capture_end(1);
    voice_audio_watch_devices(1);
    voice_audio_watch_devices(0);
    ASSERT_EQ(voice_audio_wait_ready(10), 0);
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_EQ(va_wait(0xFFFFFFFFu, 300, &wp, &lp), 0);
    DestroyWindow(w);
    TEST_END();
}

#endif /* _WIN32 */
