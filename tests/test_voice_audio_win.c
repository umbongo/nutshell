/* tests/test_voice_audio_win.c -- the audio thread of voice input.
 * Built only by `make wintest`. Wine on a build host usually has no capture
 * endpoint, so each test accepts the defined "no device" outcome; what none
 * may do is hang or post nothing. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
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
        ASSERT_EQ(voice_audio_start(w), 0);
        ASSERT_EQ(voice_audio_wait_ready(10), 0);          /* no thread was made */
        ASSERT_FALSE(dll_record_was_requested("mmdevapi.dll"));
        /* commands with no thread are dropped, never queued for a later one */
        voice_audio_capture_begin("", 1, 0, 0);
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
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    ASSERT_EQ(voice_audio_start(w), 1);       /* the same running state */
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    va_end_test(w);
    TEST_END();
}

int test_voice_audio_restart(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
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
    ASSERT_NOT_NULL(w);
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
        printf("    [voice_audio] enumerate error %d\n", (int)VOICE_ERR_CODE(lp));
        ASSERT_TRUE(VOICE_ERR_CODE(lp) == VAE_COM_FAILED || VOICE_ERR_CODE(lp) == VAE_NO_DEVICE);
        ASSERT_EQ((int)VOICE_ERR_TOKEN(lp), 0);   /* not a recording's error */
    }
    va_end_test(w);
    TEST_END();
}

int test_voice_audio_capture_no_device_or_data(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_capture_begin("", 7, 0, 0);
    Sleep(300);
    voice_audio_capture_end(1);
    WPARAM wp = 0; LPARAM lp = 0;
    int got = va_wait(BIT(VAM_CAPTURED) | BIT(VAM_ERROR), 5000, &wp, &lp);
    ASSERT_TRUE(got);
    if (got && wp == VAM_ERROR) {
        printf("    [voice_audio] capture error %d (no usable endpoint)\n", (int)VOICE_ERR_CODE(lp));
        ASSERT_TRUE(VOICE_ERR_CODE(lp) == VAE_NO_DEVICE || VOICE_ERR_CODE(lp) == VAE_OPEN_FAILED
                    || VOICE_ERR_CODE(lp) == VAE_DEVICE_LOST || VOICE_ERR_CODE(lp) == VAE_ACCESS_DENIED);
        ASSERT_EQ((int)VOICE_ERR_TOKEN(lp), 7);   /* the error names its recording */
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
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_capture_begin("{bogus}", 3, 0, 0);
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
        printf("    [voice_audio] bogus id, error %d\n", (int)VOICE_ERR_CODE(lp));
        ASSERT_EQ((int)VOICE_ERR_CODE(lp), (int)VAE_NO_DEVICE);
        ASSERT_EQ((int)VOICE_ERR_TOKEN(lp), 3);
    }
    va_end_test(w);
    TEST_END();
}

/* The command queue never drops begin/end: after a burst of enumerations,
 * a begin and an end both reach the thread, so a message carrying the
 * recording's token comes back (an error under Wine's no endpoint, or the
 * capture where there is one). */
/* Waits for the recording with `token` to end (VAM_CAPTURED or VAM_ERROR
 * carrying it). Messages that are not its own (an enumeration's VAM_ERROR
 * with token 0, VAM_DEVICES, levels, other tokens) are skipped; `seen`, when
 * given, records the tokens that ended (index = token, 0 < token < n). */
static int va_wait_token(unsigned token, DWORD ms, unsigned char *seen, unsigned n)
{
    ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        WPARAM wp = 0; LPARAM lp = 0;
        DWORD left = (DWORD)(end - GetTickCount64());
        if (!va_wait(BIT(VAM_CAPTURED) | BIT(VAM_ERROR), left < 200 ? left : 200, &wp, &lp))
            continue;
        unsigned t;
        if (wp == VAM_ERROR) {
            t = VOICE_ERR_TOKEN(lp);
        } else {
            VoiceCapture *c = (VoiceCapture *)lp;
            t = c->token;
            voice_audio_free_capture(c);
        }
        if (t == 0) continue;
        if (seen && t < n) seen[t] = 1;
        if (t == token) return 1;
    }
    return 0;
}

int test_voice_audio_queue_keeps_begin_and_end(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    for (int i = 0; i < 40; i++) voice_audio_enumerate();   /* coalesced to one */
    voice_audio_capture_begin("", 7, 0, 0);
    voice_audio_capture_end(1);
    /* An enumeration error (token 0) may arrive first: it is not ours. */
    ASSERT_TRUE(va_wait_token(7, 5000, NULL, 0));
    va_end_test(w);
    TEST_END();
}

/* Begin and end are never dropped when the queue is full: with the audio
 * thread held (it pops nothing), push three times the initial capacity of
 * begin/end pairs, so the queue must have grown (asserted), then release the
 * hold and every token must come back as a capture or an error. A dropped
 * command loses a token. */
int test_voice_audio_queue_grows_past_initial_capacity(void)
{
    TEST_BEGIN();
    enum { PAIRS = 3 * VOICE_AUDIO_QUEUE_MIN };
    unsigned char seen[PAIRS + 1];
    memset(seen, 0, sizeof(seen));
    va_begin_test();
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_test_hold(1);
    for (unsigned t = 1; t <= PAIRS; t++) {
        voice_audio_capture_begin("", t, 0, 0);
        voice_audio_capture_end(1);
    }
    int cap = voice_audio_queue_cap();
    voice_audio_test_hold(0);            /* released before any assertion can return */
    ASSERT_TRUE(cap >= 2 * VOICE_AUDIO_QUEUE_MIN);
    int ok = va_wait_token(PAIRS, 30000, seen, PAIRS + 1);
    ASSERT_TRUE(ok);
    /* The thread handles commands in order: once the last token is back,
     * every earlier one has been answered. */
    for (unsigned t = 1; t <= PAIRS; t++) {
        if (!seen[t]) { ASSERT_EQ((int)t, 0); }
    }
    va_end_test(w);
    TEST_END();
}

/* The ingest path (gain, level, wipe) runs on a private state through the
 * seam, so these assertions are real under Wine whatever endpoints exist. */
#define VA_FEED 3200   /* ten 20 ms windows */
static void va_fixture(int16_t *s, int16_t v)
{
    for (int i = 0; i < VA_FEED; i++) s[i] = (i & 1) ? (int16_t)-v : v;
}

/* A Test capture is open-ended: four seconds of audio (past the old 3 s end
 * and the 3 s push-to-talk cap's worth) still leave it running, and still
 * report each feed's level. The old code ended it by time, which a feed
 * cannot show; what a feed can show is that no length ever fills it. */
int test_voice_audio_test_capture_has_no_time_limit(void)
{
    TEST_BEGIN();
    static int16_t s[VA_FEED];
    va_fixture(s, 1000);
    int peak[2]; size_t len = 99; int verdict = -1;
    for (int round = 0; round < 20; round++) {   /* 20 x 2 x 0.2 s = 8 s */
        voice_audio_test_ingest(1, 0, 0, s, VA_FEED, peak, &len, &verdict);
        ASSERT_EQ(peak[0], 1000);
        ASSERT_EQ(peak[1], 1000);
    }
    /* The product thread. NOTE: without a capture endpoint (Wine here) this
     * half passes vacuously, as begin only posts an error; with one it shows
     * a test-only begin never posts VAM_CAPTURED. The ingest half above is
     * what always runs. */
    va_begin_test();
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_capture_begin("", 0, 1, 0);
    voice_audio_capture_end(1);          /* keep is ignored for a test */
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_EQ(va_wait(BIT(VAM_CAPTURED), 1500, &wp, &lp), 0);
    va_end_test(w);
    TEST_END();
}

/* A Test keeps no samples: the pcm length is 0 after feeding, while the same
 * feed in a push-to-talk capture is kept (the fixture tells them apart). */
int test_voice_audio_test_capture_keeps_no_samples(void)
{
    TEST_BEGIN();
    static int16_t s[VA_FEED];
    va_fixture(s, 1000);
    int peak[2]; size_t len = 99; int verdict = -1;
    voice_audio_test_ingest(1, 0, 0, s, VA_FEED, peak, &len, &verdict);
    ASSERT_EQ((int)len, 0);
    ASSERT_EQ(verdict, (int)PCM_EMPTY);
    ASSERT_EQ(peak[0], 1000);
    voice_audio_test_ingest(0, 0, 0, s, VA_FEED, peak, &len, &verdict);
    ASSERT_EQ((int)len, 2 * VA_FEED);
    ASSERT_EQ(verdict, (int)PCM_SPEECH);
    TEST_END();
}

/* Raw +-1 noise at -12 dB rounds to 0 after the gain: the push-to-talk verdict
 * must not be ALL_ZERO (the privacy-switch text) while the mic is delivering. */
int test_voice_audio_ingest_quiet_mic_not_all_zero(void)
{
    TEST_BEGIN();
    static int16_t s[VA_FEED];
    int peak[2]; size_t len = 0; int verdict = -1;
    va_fixture(s, 1);   /* alternates +1, -1 */
    voice_audio_test_ingest(0, -12, -12, s, VA_FEED, peak, &len, &verdict);
    ASSERT_TRUE(verdict != (int)PCM_ALL_ZERO);
    ASSERT_EQ(verdict, (int)PCM_SILENT);
    /* a truly dead feed (all zero before the gain) still is */
    memset(s, 0, sizeof s);
    voice_audio_test_ingest(0, -12, -12, s, VA_FEED, peak, &len, &verdict);
    ASSERT_EQ(verdict, (int)PCM_ALL_ZERO);
    TEST_END();
}

/* The gain applies to the new samples only: the level and the classifier see
 * the gained signal, and a gain change between feeds takes effect at once. */
int test_voice_audio_ingest_applies_gain_live(void)
{
    TEST_BEGIN();
    static int16_t s[VA_FEED];
    int peak[2]; size_t len = 0; int verdict = -1;
    va_fixture(s, 1000);
    voice_audio_test_ingest(1, 0, 6, s, VA_FEED, peak, &len, &verdict);
    ASSERT_EQ(peak[0], 1000);                        /* 0 dB: untouched */
    ASSERT_TRUE(peak[1] >= 1950 && peak[1] <= 2050); /* +6 dB, about x2 */
    voice_audio_test_ingest(1, 6, 0, s, VA_FEED, peak, &len, &verdict);
    ASSERT_TRUE(peak[0] >= 1950 && peak[0] <= 2050);
    ASSERT_EQ(peak[1], 1000);                        /* live change back */
    /* Saturation: a loud signal at the top gain stays inside int16. */
    va_fixture(s, 30000);   /* alternates +30000, -30000 */
    voice_audio_test_ingest(1, 12, 12, s, VA_FEED, peak, &len, &verdict);
    ASSERT_TRUE(peak[0] >= 32767);
    ASSERT_EQ(peak[1], 32768);   /* -30000 x +12 dB saturates at full scale */
    /* Quiet speech below the speech threshold becomes speech with gain, and
     * a push-to-talk capture classifies the gained samples. */
    va_fixture(s, 100);
    voice_audio_test_ingest(0, 0, 0, s, VA_FEED, peak, &len, &verdict);
    ASSERT_EQ(verdict, (int)PCM_SILENT);
    voice_audio_test_ingest(0, 12, 12, s, VA_FEED, peak, &len, &verdict);
    ASSERT_EQ(verdict, (int)PCM_SPEECH);
    TEST_END();
}

/* The latest gain always wins: SET(a), BEGIN(g), SET(b) must not fold b into
 * the first SET (the begin would then overwrite it). Held, so nothing drains. */
int test_voice_audio_set_gain_after_begin_is_kept(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_test_hold(1);
    voice_audio_set_gain(3);
    voice_audio_capture_begin("", 0, 1, 5);
    voice_audio_set_gain(9);
    voice_audio_set_gain(11);      /* folds into the one after the begin */
    int last = -999;
    int n = voice_audio_test_pending_gain(&last);
    voice_audio_test_hold(0);
    ASSERT_EQ(n, 2);               /* one before the begin, one after */
    ASSERT_EQ(last, 11);
    va_end_test(w);
    TEST_END();
}

/* Gain commands coalesce like the others: held, many pushes leave one queued
 * command carrying the last value. Real under Wine (the thread needs no
 * endpoint). */
int test_voice_audio_set_gain_coalesces(void)
{
    TEST_BEGIN();
    va_begin_test();
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_start(w), 1);
    ASSERT_EQ(voice_audio_wait_ready(5000), 1);
    voice_audio_test_hold(1);
    for (int i = 0; i < 50; i++) voice_audio_set_gain(i - 10);
    voice_audio_set_gain(7);
    int last = -999;
    int n = voice_audio_test_pending_gain(&last);
    voice_audio_test_hold(0);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(last, 7);
    Sleep(100);
    ASSERT_EQ(voice_audio_test_pending_gain(&last), 0);   /* applied and popped */
    va_end_test(w);
    TEST_END();
}

int test_voice_audio_never_started(void)
{
    TEST_BEGIN();
    va_gates(0);
    HWND w = va_window();
    ASSERT_NOT_NULL(w);
    voice_audio_stop();
    voice_audio_enumerate();
    voice_audio_capture_begin("", 1, 0, 0);
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
