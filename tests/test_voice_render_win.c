/* tests/test_voice_render_win.c -- the AI speech render client on the audio thread (Task 6).
 * Built only by `make wintest`. Most tests use the null sink (voice_audio_test_render_null): a
 * 10 ms timer and a simulated 100 ms buffer draining in real time stand in for WASAPI, so they
 * are deterministic under headless Wine. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "voice_audio_win.h"
#include "voice_gate.h"
#include "ai_gate.h"
#include "voice_listen.h"

static LRESULT CALLBACK vr_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static HWND vr_window(void)
{
    static int registered;
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = vr_wndproc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "NsVoiceRenderTest";
        RegisterClassA(&wc);
        registered = 1;
    }
    return CreateWindowExA(0, "NsVoiceRenderTest", "", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
}

#define VRBIT(m) (1u << (unsigned)(m))

static void vr_gates(int on)
{
    ai_gate_set_enabled(on);
    voice_gate_set_enabled(on);
}

/* Starts from no thread, gates on, the audio thread ready, the null sink on (or off). */
static HWND vr_begin(int null_sink)
{
    voice_audio_stop();
    vr_gates(1);
    HWND w = vr_window();
    if (!w || voice_audio_start(w) != 1 || voice_audio_wait_ready(5000) != 1) return NULL;
    voice_audio_test_render_null(null_sink);
    return w;
}

static void vr_end(HWND w)
{
    voice_audio_stop();
    voice_audio_test_render_null(0);
    vr_gates(0);
    if (w) DestroyWindow(w);
}

/* Frees what a message owns. */
static void vr_free_msg(const MSG *m)
{
    if (m->wParam == VAM_DEVICES) voice_audio_free_devices((VoiceDeviceList *)m->lParam);
    else if (m->wParam == VAM_CAPTURED || m->wParam == VAM_CONV_TURN)
        voice_audio_free_capture((VoiceCapture *)m->lParam);
    else if (m->wParam == VAM_PLAYED || m->wParam == VAM_PLAY_DONE) free((void *)m->lParam);
}

/* Waits for a message in `want` (the others are freed); the payload stays the caller's. */
static int vr_wait(unsigned want, DWORD ms, WPARAM *wp, LPARAM *lp)
{
    ULONGLONG end = GetTickCount64() + ms;
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message != WM_VOICE_AUDIO) continue;
            if (VRBIT(msg.wParam) & want) { *wp = msg.wParam; *lp = msg.lParam; return 1; }
            vr_free_msg(&msg);
        }
        Sleep(5);
    }
    return 0;
}

static int16_t *vr_samples(size_t n, int16_t v)
{
    int16_t *s = (int16_t *)malloc(n * sizeof(int16_t));
    if (s) for (size_t i = 0; i < n; i++) s[i] = (i & 1) ? (int16_t)-v : v;
    return s;
}

#define VR_TOKEN 0x00123u

int test_voice_render_null_plays_and_done(void)
{
    TEST_BEGIN();
    HWND w = vr_begin(1);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN), 1);
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED) | VRBIT(VAM_RENDER_ERROR), 3000, &wp, &lp), 1);
    ASSERT_EQ((int)wp, (int)VAM_RENDER_OPENED);
    ASSERT_EQ((int)VOICE_ERR_TOKEN(lp), (int)VR_TOKEN);

    int16_t *s = vr_samples(12000, 8000);               /* 0.5 s */
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 1, 0, s, 12000), 1);
    voice_audio_render_end(VR_TOKEN, 1);

    int played = 0, done = 0, rising = 1;
    uint64_t last = 0, first = 0;
    ULONGLONG end = GetTickCount64() + 5000;
    while (!done && GetTickCount64() < end) {
        if (!vr_wait(VRBIT(VAM_PLAYED) | VRBIT(VAM_PLAY_DONE) | VRBIT(VAM_RENDER_ERROR), 200, &wp, &lp))
            continue;
        ASSERT_TRUE(wp != VAM_RENDER_ERROR);
        VoiceRenderMsg *m = (VoiceRenderMsg *)lp;
        ASSERT_EQ((int)m->token, (int)VR_TOKEN);
        ASSERT_EQ((int)m->reply_id, 1);
        if (wp == VAM_PLAYED) {
            if (played == 0) first = m->frames_in_chunk;
            else if (m->frames_in_chunk < last) rising = 0;
            last = m->frames_in_chunk;
            played++;
        } else {
            done++;
        }
        free(m);
    }
    ASSERT_EQ(done, 1);
    ASSERT_TRUE(played >= 2);
    ASSERT_EQ(rising, 1);
    ASSERT_TRUE(last > first);
    uint64_t f = voice_audio_test_render_frames(1);
    ASSERT_TRUE(f >= 11999 && f <= 12001);
    /* never a second DONE for the same reply */
    ASSERT_EQ(vr_wait(VRBIT(VAM_PLAY_DONE), 400, &wp, &lp), 0);
    ASSERT_EQ(voice_audio_render_queued_ms() == 0, 1);
    voice_audio_render_close(VR_TOKEN);
    vr_end(w);
    TEST_END();
}

int test_voice_render_flush_then_old_data(void)
{
    TEST_BEGIN();
    HWND w = vr_begin(1);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN), 1);
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED), 3000, &wp, &lp), 1);
    int16_t *s = vr_samples(48000, 5000);               /* 2 s: still playing at the flush */
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 1, 0, s, 48000), 1);
    Sleep(150);                                         /* some of it has been written */
    ASSERT_TRUE(voice_audio_test_render_frames(1) > 0);
    voice_audio_render_flush(VR_TOKEN, 2);
    Sleep(150);
    uint64_t after = voice_audio_test_render_frames(1);
    ASSERT_TRUE(after < 48000);
    int16_t *s2 = vr_samples(4800, 5000);
    ASSERT_NOT_NULL(s2);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 1, 1, s2, 4800), 1);   /* old reply: dropped */
    Sleep(300);
    ASSERT_EQ((int)voice_audio_test_render_frames(1), (int)after);
    ASSERT_EQ((int)voice_audio_render_queued_ms(), 0);
    /* the next reply plays */
    int16_t *s3 = vr_samples(2400, 5000);
    ASSERT_NOT_NULL(s3);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 2, 0, s3, 2400), 1);
    Sleep(300);
    ASSERT_EQ((int)voice_audio_test_render_frames(2), 2400);
    voice_audio_render_close(VR_TOKEN);
    vr_end(w);
    TEST_END();
}

int test_voice_render_gain(void)
{
    TEST_BEGIN();
    HWND w = vr_begin(1);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN), 1);
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED), 3000, &wp, &lp), 1);
    (void)voice_audio_test_render_peak();
    voice_audio_render_gain(VR_TOKEN, 18);
    int16_t *s = vr_samples(2400, 20000);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 1, 0, s, 2400), 1);
    Sleep(300);
    int pk = voice_audio_test_render_peak();
    ASSERT_TRUE(pk >= 3400 && pk <= 3800);              /* 18 % +- 1 of 20000 */
    /* a stale token's gain changes nothing */
    voice_audio_render_gain(VR_TOKEN + 1, 100);
    int16_t *s2 = vr_samples(2400, 20000);
    ASSERT_NOT_NULL(s2);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 2, 0, s2, 2400), 1);
    Sleep(300);
    pk = voice_audio_test_render_peak();
    ASSERT_TRUE(pk >= 3400 && pk <= 3800);        /* the stale gain(100) did not undo the duck */
    /* gain belongs to the owner: the next owner starts at 100 %, a non-owner's gain is ignored */
    voice_audio_render_close(VR_TOKEN);
    voice_audio_render_gain(VR_TOKEN, 18);        /* closed owner: ignored, must not be remembered */
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN + 2), 1);
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED), 3000, &wp, &lp), 1);
    (void)voice_audio_test_render_peak();
    int16_t *s3 = vr_samples(2400, 20000);
    ASSERT_NOT_NULL(s3);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN + 2, 3, 0, s3, 2400), 1);
    Sleep(300);
    pk = voice_audio_test_render_peak();
    ASSERT_TRUE(pk >= 19900);                     /* full scale, no leaked duck */
    voice_audio_render_close(VR_TOKEN + 2);
    vr_end(w);
    TEST_END();
}

int test_voice_render_preempt(void)
{
    TEST_BEGIN();
    HWND w = vr_begin(1);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN), 1);
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED), 3000, &wp, &lp), 1);
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN + 1), 1);
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_ERROR), 3000, &wp, &lp), 1);
    ASSERT_EQ((int)VOICE_ERR_CODE(lp), (int)VAE_RENDER_PREEMPTED);
    ASSERT_EQ((int)VOICE_ERR_TOKEN(lp), (int)VR_TOKEN);
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED), 3000, &wp, &lp), 1);
    ASSERT_EQ((int)VOICE_ERR_TOKEN(lp), (int)(VR_TOKEN + 1));
    /* the old owner is gone: its data is not played, the new owner's is */
    int16_t *s = vr_samples(2400, 5000);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 1, 0, s, 2400), 1);
    int16_t *s2 = vr_samples(1200, 5000);
    ASSERT_NOT_NULL(s2);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN + 1, 2, 0, s2, 1200), 1);
    Sleep(300);
    ASSERT_EQ((int)voice_audio_test_render_frames(1), 0);
    ASSERT_EQ((int)voice_audio_test_render_frames(2), 1200);
    voice_audio_render_close(VR_TOKEN + 1);
    vr_end(w);
    TEST_END();
}

/* The real WASAPI path under headless Wine may open a null driver or have no endpoint: either
 * outcome is defined. Never a hang or a leaked thread. */
int test_voice_render_real_wine(void)
{
    TEST_BEGIN();
    HWND w = vr_begin(0);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN), 1);
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED) | VRBIT(VAM_RENDER_ERROR), 3000, &wp, &lp), 1);
    if (wp == VAM_RENDER_OPENED) {
        printf("[real_wine] OPENED on the real WASAPI render path\n");
        int16_t *s = vr_samples(4800, 3000);            /* 0.2 s */
        ASSERT_NOT_NULL(s);
        ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 1, 0, s, 4800), 1);
        voice_audio_render_end(VR_TOKEN, 1);
        ASSERT_EQ(vr_wait(VRBIT(VAM_PLAY_DONE) | VRBIT(VAM_RENDER_ERROR), 3000, &wp, &lp), 1);
        if (wp == VAM_PLAY_DONE) { printf("[real_wine] PLAY_DONE\n"); free((void *)lp); }
        else {
            printf("[real_wine] after OPENED: VAM_RENDER_ERROR code %d\n", (int)VOICE_ERR_CODE(lp));
            ASSERT_EQ((int)VOICE_ERR_TOKEN(lp), (int)VR_TOKEN);
        }
    } else {
        printf("[real_wine] VAM_RENDER_ERROR code %d (1 NO_DEVICE, 3 OPEN_FAILED, 4 COM_FAILED)\n",
               (int)VOICE_ERR_CODE(lp));
        ASSERT_EQ((int)VOICE_ERR_TOKEN(lp), (int)VR_TOKEN);
        ASSERT_TRUE(VOICE_ERR_CODE(lp) == VAE_NO_DEVICE || VOICE_ERR_CODE(lp) == VAE_OPEN_FAILED ||
                    VOICE_ERR_CODE(lp) == VAE_COM_FAILED);
    }
    voice_audio_render_close(VR_TOKEN);
    ULONGLONG t0 = GetTickCount64();
    voice_audio_stop();                                 /* joins within its bound */
    ASSERT_TRUE(GetTickCount64() - t0 < 3500);
    vr_end(w);
    TEST_END();
}

/* ---- notes item (r): a close and a conversation end cannot be lost ---- */

#define VR_WIN VVAD_WIN
static void vr_quiet(int16_t *s, size_t windows)
{
    for (size_t i = 0; i < windows * VR_WIN; i++) s[i] = (i & 1) ? (int16_t)-20 : (int16_t)20;
}
static void vr_speech(int16_t *s, size_t windows)
{
    for (size_t k = 0; k < windows; k++) {
        int16_t a = (int16_t)(6000 + (int)(k % 5) * 1200);
        for (size_t i = 0; i < VR_WIN; i++) s[k * VR_WIN + i] = (i & 1) ? (int16_t)-a : a;
    }
}
#define VR_LEAD 20
#define VR_SPEECH 30
#define VR_TAIL 45
#define VR_TOTAL ((VR_LEAD + VR_SPEECH + VR_TAIL) * VR_WIN)
#define VR_PAUSE_MS 500

/* Counts the conversation events that arrive for `ms`. */
static int vr_conv_events(DWORD ms)
{
    int n = 0;
    ULONGLONG end = GetTickCount64() + ms;
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message != WM_VOICE_AUDIO) continue;
            if (msg.wParam == VAM_CONV_EVENT) n++;
            vr_free_msg(&msg);
        }
        Sleep(5);
    }
    return n;
}

int test_voice_audio_close_cannot_be_lost(void)
{
    TEST_BEGIN();
    static int16_t s[VR_TOTAL];
    vr_quiet(s, VR_LEAD);
    vr_speech(s + VR_LEAD * VR_WIN, VR_SPEECH);
    vr_quiet(s + (VR_LEAD + VR_SPEECH) * VR_WIN, VR_TAIL);
    HWND w = vr_begin(1);
    ASSERT_NOT_NULL(w);

    /* control: with a live conversation state the same samples do post events */
    voice_audio_test_conv_open(0x111, VR_PAUSE_MS);
    voice_audio_test_conv_ingest(0x111, s, VR_TOTAL);
    ASSERT_TRUE(vr_conv_events(800) > 0);
    voice_audio_conv_end(0x111);

    /* the end's push fails as on OOM: it is still applied, and what follows is not run */
    voice_audio_test_conv_open(0x222, VR_PAUSE_MS);
    Sleep(50);
    voice_audio_test_fail_push(1);
    voice_audio_conv_end(0x222);
    voice_audio_test_conv_ingest(0x222, s, VR_TOTAL);
    ASSERT_EQ(vr_conv_events(800), 0);

    /* the same for a render close */
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN), 1);
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED), 3000, &wp, &lp), 1);
    voice_audio_test_fail_push(1);
    voice_audio_render_close(VR_TOKEN);
    int16_t *d = vr_samples(2400, 5000);
    ASSERT_NOT_NULL(d);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 1, 0, d, 2400), 1);
    Sleep(300);
    ASSERT_EQ((int)voice_audio_test_render_frames(1), 0);
    /* and the render client really is closed: a fresh data for the token is not played either */
    int16_t *d2 = vr_samples(2400, 5000);
    ASSERT_NOT_NULL(d2);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 2, 0, d2, 2400), 1);
    Sleep(300);
    ASSERT_EQ((int)voice_audio_test_render_frames(2), 0);
    ASSERT_EQ((int)voice_audio_render_queued_ms(), 0);   /* the pending close was applied */
    /* a flush that fails to push is applied too: reply 1 is dropped after it */
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN + 1), 1);
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED) | VRBIT(VAM_RENDER_ERROR), 3000, &wp, &lp), 1);
    ASSERT_EQ((int)wp, (int)VAM_RENDER_OPENED);          /* no PREEMPTED for the closed token */
    voice_audio_test_fail_push(1);
    voice_audio_render_flush(VR_TOKEN + 1, 5);
    int16_t *d3 = vr_samples(2400, 5000);
    ASSERT_NOT_NULL(d3);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN + 1, 4, 0, d3, 2400), 1);
    Sleep(300);
    ASSERT_EQ((int)voice_audio_test_render_frames(4), 0);
    int16_t *d4 = vr_samples(2400, 5000);
    ASSERT_NOT_NULL(d4);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN + 1, 5, 0, d4, 2400), 1);
    Sleep(300);
    ASSERT_EQ((int)voice_audio_test_render_frames(5), 2400);
    voice_audio_render_close(VR_TOKEN + 1);
    vr_end(w);
    TEST_END();
}

/* The reply id of the next VAM_PLAY_DONE within ms (the message freed), -1 for none, -2 for an error first. */
static int vr_wait_done(DWORD ms)
{
    WPARAM wp = 0; LPARAM lp = 0;
    if (!vr_wait(VRBIT(VAM_PLAY_DONE) | VRBIT(VAM_RENDER_ERROR), ms, &wp, &lp)) return -1;
    if (wp != VAM_PLAY_DONE) return -2;
    int id = (int)((VoiceRenderMsg *)lp)->reply_id;
    free((void *)lp);
    return id;
}

/* I1: a reply whose end is reached while the client is stopped still gets its DONE. */
int test_voice_render_end_without_data_done(void)
{
    TEST_BEGIN();
    HWND w = vr_begin(1);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN), 1);
    WPARAM wp = 0; LPARAM lp = 0;
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED), 3000, &wp, &lp), 1);
    voice_audio_render_end(VR_TOKEN, 1);                 /* no frames at all */
    ASSERT_EQ(vr_wait_done(500), 1);
    /* a reply played out (R6 stop), then an empty one */
    int16_t *s = vr_samples(2400, 4000);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 2, 0, s, 2400), 1);
    voice_audio_render_end(VR_TOKEN, 2);
    ASSERT_EQ(vr_wait_done(3000), 2);
    Sleep(100);
    voice_audio_render_end(VR_TOKEN, 3);
    ASSERT_EQ(vr_wait_done(500), 3);
    voice_audio_render_close(VR_TOKEN);
    vr_end(w);
    TEST_END();
}

/* I3: a command whose push failed keeps its place in the queue. Each case queues a burst
 * behind voice_audio_test_hold, with no sleeps between its pushes. */
static int vr_open_wait(unsigned token)
{
    WPARAM wp = 0; LPARAM lp = 0;
    return voice_audio_render_open(token) == 1 &&
           vr_wait(VRBIT(VAM_RENDER_OPENED), 3000, &wp, &lp) == 1;
}

int test_voice_render_pending_keeps_order(void)
{
    TEST_BEGIN();
    HWND w = vr_begin(1);
    ASSERT_NOT_NULL(w);
    WPARAM wp = 0; LPARAM lp = 0;

    /* a. failed flush(T, 5), then reply 5's data and end: reply 5 plays and its DONE arrives */
    ASSERT_EQ(vr_open_wait(VR_TOKEN), 1);
    voice_audio_test_hold(1);
    voice_audio_test_fail_push(1);
    voice_audio_render_flush(VR_TOKEN, 5);
    int16_t *a = vr_samples(2400, 4000);
    ASSERT_NOT_NULL(a);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 5, 0, a, 2400), 1);
    voice_audio_render_end(VR_TOKEN, 5);
    voice_audio_test_hold(0);
    ASSERT_EQ(vr_wait_done(3000), 5);
    ASSERT_EQ((int)voice_audio_test_render_frames(5), 2400);

    /* b. failed close(T), then open(T): OPENED arrives, later data plays, no error for T */
    voice_audio_test_hold(1);
    voice_audio_test_fail_push(1);
    voice_audio_render_close(VR_TOKEN);
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN), 1);
    voice_audio_test_hold(0);
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED) | VRBIT(VAM_RENDER_ERROR), 3000, &wp, &lp), 1);
    ASSERT_EQ((int)wp, (int)VAM_RENDER_OPENED);
    int16_t *b = vr_samples(2400, 4000);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ(voice_audio_render_data(VR_TOKEN, 7, 0, b, 2400), 1);
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_ERROR), 400, &wp, &lp), 0);
    ASSERT_EQ((int)voice_audio_test_render_frames(7), 2400);

    /* c. failed close(A), then open(B): A gets no PREEMPTED (A is a live owner here) */
    ASSERT_EQ(vr_open_wait(VR_TOKEN), 1);
    voice_audio_test_hold(1);
    voice_audio_test_fail_push(1);
    voice_audio_render_close(VR_TOKEN);
    ASSERT_EQ(voice_audio_render_open(VR_TOKEN + 1), 1);
    voice_audio_test_hold(0);
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_OPENED) | VRBIT(VAM_RENDER_ERROR), 3000, &wp, &lp), 1);
    ASSERT_EQ((int)wp, (int)VAM_RENDER_OPENED);
    ASSERT_EQ((int)VOICE_ERR_TOKEN(lp), (int)(VR_TOKEN + 1));
    ASSERT_EQ(vr_wait(VRBIT(VAM_RENDER_ERROR), 300, &wp, &lp), 0);
    voice_audio_render_close(VR_TOKEN + 1);
    vr_end(w);
    TEST_END();
}

int test_voice_audio_conv_end_then_same_token_open(void)
{
    TEST_BEGIN();
    static int16_t s[VR_TOTAL];
    vr_quiet(s, VR_LEAD);
    vr_speech(s + VR_LEAD * VR_WIN, VR_SPEECH);
    vr_quiet(s + (VR_LEAD + VR_SPEECH) * VR_WIN, VR_TAIL);
    HWND w = vr_begin(1);
    ASSERT_NOT_NULL(w);
    voice_audio_test_conv_open(0x333, VR_PAUSE_MS);
    Sleep(50);
    voice_audio_test_hold(1);
    voice_audio_test_fail_push(1);
    voice_audio_conv_end(0x333);
    voice_audio_test_conv_open(0x333, VR_PAUSE_MS);      /* the new conversation, same token */
    voice_audio_test_conv_ingest(0x333, s, VR_TOTAL);
    voice_audio_test_hold(0);
    ASSERT_TRUE(vr_conv_events(1000) > 0);
    voice_audio_conv_end(0x333);
    vr_end(w);
    TEST_END();
}

#endif /* _WIN32 */
