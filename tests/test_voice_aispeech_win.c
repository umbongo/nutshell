/* tests/test_voice_aispeech_win.c -- the AI speech engine (Task 7a): contexts, render handling,
 * the resource ledger, the done guard and the dead flag, driven by a fake fetch against the null
 * render sink. Built only by `make wintest`. The pump below is the window procedure's job in the
 * product: it routes WM_VOICE_AUDIO, WM_VOICE_AIS and WM_TIMER to the engine and records every
 * WM_VOICE_TTS the engine posts. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "voice_aispeech_win.h"
#include "voice_audio_win.h"
#include "voice_tts_win.h"
#include "voice_gate.h"
#include "ai_gate.h"
#include "voice_ledger.h"
#include "voice_speak.h"
#include "voice_net_win.h"
#include "voice_wavstream.h"
#include "voice_aispeech.h"
#include "base64.h"
#include "sse_line.h"
#include "http_loopback_win.h"

#define VA_TOKEN 0x00321u
#define VA_TOKEN_B 0x00777u

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
        wc.lpszClassName = "NsVoiceAisTest";
        RegisterClassA(&wc);
        registered = 1;
    }
    return CreateWindowExA(0, "NsVoiceAisTest", "", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
}

/* ---- the fake fetch ---- */

static struct {
    size_t frames;             /* per chunk */
    size_t block;
    DWORD sleep_ms;
    long ret;
    int amp;
    int arm_fail_push;         /* after the data, make the worker's render_end fail */
    int hold_after_first;      /* push one block, then wait for `release` */
    HANDLE release;
    volatile LONG calls, running, first_pushed, refused;
} g_fk;

static void fk_reset(void)
{
    if (g_fk.release) CloseHandle(g_fk.release);
    memset(&g_fk, 0, sizeof(g_fk));
    g_fk.frames = 12000;
    g_fk.block = 2400;
    g_fk.amp = 8000;
    g_fk.release = CreateEventW(NULL, TRUE, FALSE, NULL);
}

static long fake_fetch(void *job, const VoiceAisTarget *t, const char *key, const char *text,
                       size_t len, VoiceAisPush push)
{
    (void)t; (void)key; (void)text; (void)len;
    InterlockedIncrement(&g_fk.calls);
    InterlockedIncrement(&g_fk.running);
    size_t sent = 0;
    long ret = g_fk.ret;
    int16_t *buf = (int16_t *)malloc(g_fk.block * sizeof(int16_t));
    if (buf == NULL) { InterlockedDecrement(&g_fk.running); return VOICE_AIS_HR_NET; }
    for (size_t i = 0; i < g_fk.block; i++) buf[i] = (int16_t)((i & 1) ? -g_fk.amp : g_fk.amp);
    while (sent < g_fk.frames) {
        size_t n = g_fk.frames - sent < g_fk.block ? g_fk.frames - sent : g_fk.block;
        if (!push(job, buf, n)) { InterlockedIncrement(&g_fk.refused); ret = 0; break; }
        sent += n;
        if (g_fk.hold_after_first && sent == n) {
            InterlockedExchange(&g_fk.first_pushed, 1);
            WaitForSingleObject(g_fk.release, 10000);
        }
        if (g_fk.sleep_ms) Sleep(g_fk.sleep_ms);
    }
    if (g_fk.arm_fail_push && sent == g_fk.frames) voice_audio_test_fail_push(1);
    free(buf);
    InterlockedDecrement(&g_fk.running);
    return ret;
}

/* ---- the pump and the log ---- */

typedef struct { VoiceTtsKind kind; unsigned token, reply; size_t offset; long hr; ULONGLONG tick; } VaLog;
#define VA_LOG_MAX 512
static VaLog g_log[VA_LOG_MAX];
static int g_nlog;
static int g_played;
static ULONGLONG g_progress_tick;
static unsigned g_last_chunk;
static uint64_t g_last_frames;
static int g_have_prog;
static int g_ais_seen[8];
static int g_drop_chunk_done;     /* the pump loses CHUNK_DONE messages, as OOM could */

static void va_free_audio(const MSG *m)
{
    if (m->wParam == VAM_DEVICES) voice_audio_free_devices((VoiceDeviceList *)m->lParam);
    else if (m->wParam == VAM_CAPTURED || m->wParam == VAM_CONV_TURN)
        voice_audio_free_capture((VoiceCapture *)m->lParam);
}

static void va_pump(DWORD ms)
{
    ULONGLONG end = GetTickCount64() + ms;
    MSG msg;
    do {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_VOICE_AUDIO) {
                if (msg.wParam == VAM_PLAYED) {
                    const VoiceRenderMsg *r = (const VoiceRenderMsg *)msg.lParam;
                    g_played++;
                    if (!g_have_prog || r->chunk != g_last_chunk || r->frames_in_chunk != g_last_frames) {
                        g_progress_tick = GetTickCount64();
                        g_last_chunk = r->chunk;
                        g_last_frames = r->frames_in_chunk;
                        g_have_prog = 1;
                    }
                }
                va_free_audio(&msg);
                voice_ais_on_audio((UINT)msg.wParam, msg.lParam);
            } else if (msg.message == WM_VOICE_AIS) {
                VoiceAisMsg *m = (VoiceAisMsg *)msg.lParam;
                if ((int)m->kind >= 0 && (int)m->kind < 8) g_ais_seen[m->kind]++;
                if (!(g_drop_chunk_done && m->kind == VAI_CHUNK_DONE)) voice_ais_on_msg(m);
                free(m);
            } else if (msg.message == WM_VOICE_TTS) {
                VoiceTtsMsg *m = (VoiceTtsMsg *)msg.lParam;
                if (g_nlog < VA_LOG_MAX) {
                    g_log[g_nlog].kind = m->kind; g_log[g_nlog].token = m->token;
                    g_log[g_nlog].reply = m->reply_id; g_log[g_nlog].offset = m->offset;
                    g_log[g_nlog].hr = m->hr; g_log[g_nlog].tick = GetTickCount64();
                    g_nlog++;
                }
                free(m);
            } else if (msg.message == WM_TIMER) {
                voice_ais_on_timer(msg.wParam);
            }
        }
        Sleep(5);
    } while (GetTickCount64() < end);
}

static int va_count(VoiceTtsKind k, unsigned reply)
{
    int n = 0;
    for (int i = 0; i < g_nlog; i++) if (g_log[i].kind == k && g_log[i].reply == reply) n++;
    return n;
}

static int va_find(VoiceTtsKind k, unsigned reply)
{
    for (int i = 0; i < g_nlog; i++) if (g_log[i].kind == k && g_log[i].reply == reply) return i;
    return -1;
}

/* Pumps until DONE or ERROR for reply is logged (or ms pass). */
static int va_wait_end(unsigned reply, DWORD ms)
{
    ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        va_pump(20);
        if (va_find(VTT_DONE, reply) >= 0 || va_find(VTT_ERROR, reply) >= 0) return 1;
    }
    return 0;
}

static int va_wait_played(int n, DWORD ms)
{
    ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        va_pump(10);
        if (g_played >= n) return 1;
    }
    return 0;
}

static int va_wait_ledger(VoiceRes r, int want, DWORD ms)
{
    ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        va_pump(10);
        if (voice_ledger_count(r) == want) return 1;
    }
    return voice_ledger_count(r) == want;
}

static void va_gates(int on)
{
    ai_gate_set_enabled(on);
    voice_gate_set_enabled(on);
}

static HWND va_begin(void)
{
    voice_ais_quit_all(1000);
    voice_audio_stop();
    va_gates(1);
    HWND w = va_window();
    if (!w || voice_audio_start(w) != 1 || voice_audio_wait_ready(5000) != 1) return NULL;
    voice_audio_test_render_null(1);
    fk_reset();
    g_nlog = 0; g_played = 0; g_have_prog = 0; g_progress_tick = 0;
    memset(g_ais_seen, 0, sizeof(g_ais_seen));
    g_drop_chunk_done = 0;
    voice_ledger_reset();
    voice_ais_set_fetch(fake_fetch);
    return w;
}

static void va_end(HWND w)
{
    voice_ais_quit_all(2000);
    va_pump(50);
    voice_ais_set_fetch(NULL);
    voice_ais_test_max_chunks(0);
    voice_audio_stop();
    voice_audio_test_render_null(0);
    va_gates(0);
    if (w) DestroyWindow(w);
}

static VoiceAisTarget va_target(void)
{
    VoiceAisTarget t;
    memset(&t, 0, sizeof(t));
    snprintf(t.url, sizeof(t.url), "http://speech.invalid/v1/audio/speech");
    snprintf(t.provider, sizeof(t.provider), "custom");
    return t;
}

/* ---- tests ---- */

int test_voice_ais_gate_off(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    va_gates(0);
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 0);
    ASSERT_EQ(voice_ais_running(), 0);
    ASSERT_EQ(voice_ais_owns(VA_TOKEN), 0);
    VoiceAisTarget t = va_target();
    ASSERT_EQ(voice_ais_sample(w, &t, "k"), 0);
    voice_ais_speak(VA_TOKEN, 1, "Hello.", 0, &t, "k");   /* no context: nothing */
    va_pump(100);
    ASSERT_EQ((int)g_fk.calls, 0);
    ASSERT_EQ(g_nlog, 0);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    /* gate on but no audio thread: still refused */
    va_gates(1);
    voice_audio_stop();
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 0);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

int test_voice_ais_fake_done(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    ASSERT_EQ(voice_ais_owns(VA_TOKEN), 1);

    /* one chunk: "One. Two." */
    voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, "key");
    ASSERT_EQ(voice_ledger_count(VRES_RENDER), 1);
    ASSERT_EQ(va_wait_end(1, 6000), 1);
    ASSERT_EQ(va_count(VTT_DONE, 1), 1);
    ASSERT_EQ(va_count(VTT_ERROR, 1), 0);
    int words = 0, past_two = 0, nondecr = 1;
    size_t last = 0;
    for (int i = 0; i < g_nlog; i++) {
        if (g_log[i].kind != VTT_WORD || g_log[i].reply != 1) continue;
        words++;
        if (g_log[i].offset <= last && words > 1) nondecr = 0;   /* M4: only a changed offset is posted */
        last = g_log[i].offset;
        if (g_log[i].offset >= 5) past_two = 1;   /* "Two" starts at byte 5 */
    }
    ASSERT_TRUE(words >= 1);
    ASSERT_EQ(nondecr, 1);
    ASSERT_EQ(past_two, 1);
    ASSERT_EQ(va_count(VTT_DONE, 1) == 1 && va_find(VTT_DONE, 1) > 0, 1);

    /* two chunks: a first sentence of about 144 bytes, then the same again */
    char text[400], sent[200];
    sent[0] = '\0';
    for (int i = 0; i < 28; i++) strcat(sent, "abcd ");
    strcat(sent, "end.");
    snprintf(text, sizeof(text), "%s %s", sent, sent);
    const size_t second = strlen(sent) + 1;
    g_log[0].kind = 0;
    g_nlog = 0;
    g_fk.calls = 0;
    voice_ais_speak(VA_TOKEN, 2, text, 0, &t, "key");
    ASSERT_EQ(va_wait_end(2, 8000), 1);
    ASSERT_EQ(va_count(VTT_DONE, 2), 1);
    ASSERT_EQ((int)g_fk.calls, 2);
    int reached_second = 0;
    last = 0;
    nondecr = 1;
    for (int i = 0; i < g_nlog; i++) {
        if (g_log[i].kind != VTT_WORD || g_log[i].reply != 2) continue;
        if (g_log[i].offset < last) nondecr = 0;
        last = g_log[i].offset;
        if (g_log[i].offset >= second) reached_second = 1;
    }
    ASSERT_EQ(nondecr, 1);
    ASSERT_EQ(reached_second, 1);
    ASSERT_EQ((int)voice_audio_test_render_frames(2), 24000);

    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    ASSERT_EQ(voice_ledger_underflow(), 0);
    va_end(w);
    TEST_END();
}

int test_voice_ais_empty_chunk_fails(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    g_fk.frames = 0;                      /* the fetch succeeds and produces nothing */
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 3, "One. Two.", 0, &t, "key");
    ASSERT_EQ(va_wait_end(3, 6000), 1);
    const int i = va_find(VTT_ERROR, 3);
    ASSERT_TRUE(i >= 0);
    if (i >= 0) {
        ASSERT_EQ((int)g_log[i].hr, (int)VOICE_AIS_HR_STREAM);
        ASSERT_EQ((int)g_log[i].offset, 0);
    }
    ASSERT_EQ(va_count(VTT_DONE, 3), 0);
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

int test_voice_ais_stop_then_speak_no_old_audio(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    g_fk.frames = 72000;                  /* 3 s, pushed over about 0.3 s */
    g_fk.sleep_ms = 10;
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, "key");
    ASSERT_EQ(va_wait_played(1, 5000), 1);
    voice_ais_stop(VA_TOKEN);
    ASSERT_TRUE(va_wait_ledger(VRES_SPEECH_REQ, 0, 3000));   /* the cancelled worker ended */
    va_pump(300);
    const uint64_t f0 = voice_audio_test_render_frames(1);
    ASSERT_TRUE(f0 < 72000);
    g_fk.frames = 12000;
    g_fk.sleep_ms = 0;
    voice_ais_speak(VA_TOKEN, 2, "Three.", 0, &t, "key");
    ASSERT_EQ(va_wait_end(2, 6000), 1);
    ASSERT_EQ(va_count(VTT_DONE, 2), 1);
    ASSERT_EQ((int)voice_audio_test_render_frames(1), (int)f0);
    ASSERT_EQ((int)voice_audio_test_render_frames(2), 12000);
    ASSERT_EQ(va_count(VTT_DONE, 1), 0);
    ASSERT_EQ(va_count(VTT_ERROR, 1), 0);
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

int test_voice_ais_gain_survives_open(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    g_fk.amp = 10000;
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_volume(VA_TOKEN, 18);       /* before the first speak: kept, sent after the open */
    (void)voice_audio_test_render_peak();
    voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, "key");
    ASSERT_EQ(va_wait_end(1, 6000), 1);
    const int pk = voice_audio_test_render_peak();
    ASSERT_TRUE(pk >= 1700 && pk <= 1900);   /* 18 % +- 1 of 10000 */
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

int test_voice_ais_reopen_after_preempt(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    g_fk.amp = 10000;
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_volume(VA_TOKEN, 18);
    voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, "key");
    ASSERT_EQ(va_wait_end(1, 6000), 1);
    ASSERT_EQ(voice_ledger_count(VRES_RENDER), 1);
    va_pump(200);                          /* A is silent now */
    const int before = g_nlog;
    ASSERT_EQ(voice_audio_render_open(VA_TOKEN_B), 1);   /* B pre-empts A */
    ASSERT_TRUE(va_wait_ledger(VRES_RENDER, 0, 3000));
    va_pump(200);
    ASSERT_EQ(g_nlog, before);             /* nothing posted for A */
    voice_audio_render_close(VA_TOKEN_B);
    va_pump(100);
    (void)voice_audio_test_render_peak();
    voice_ais_speak(VA_TOKEN, 2, "Three. Four.", 0, &t, "key");
    ASSERT_EQ(voice_ledger_count(VRES_RENDER), 1);       /* reopened */
    ASSERT_EQ(va_wait_end(2, 6000), 1);
    ASSERT_EQ(va_count(VTT_DONE, 2), 1);
    const int pk = voice_audio_test_render_peak();
    ASSERT_TRUE(pk >= 1700 && pk <= 1900);               /* the gain was re-sent */
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

int test_voice_ais_preempt_while_playing(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    g_fk.frames = 72000;
    g_fk.sleep_ms = 10;
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, "key");
    ASSERT_EQ(va_wait_played(2, 5000), 1);
    ASSERT_EQ(voice_audio_render_open(VA_TOKEN_B), 1);
    ASSERT_EQ(va_wait_end(1, 5000), 1);
    const int i = va_find(VTT_ERROR, 1);
    ASSERT_TRUE(i >= 0);
    if (i >= 0) ASSERT_EQ((int)g_log[i].hr, (int)VOICE_AIS_HR_DEVICE);
    ASSERT_EQ(va_count(VTT_DONE, 1), 0);
    ASSERT_EQ(va_count(VTT_ERROR, 1), 1);
    ASSERT_EQ(voice_ledger_count(VRES_RENDER), 0);
    voice_audio_render_close(VA_TOKEN_B);
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

int test_voice_ais_done_guard_waits_for_progress(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    g_fk.frames = 36000;                  /* 1.5 s, all pushed at once */
    g_fk.arm_fail_push = 1;               /* the worker's render_end is lost */
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, "key");
    ASSERT_EQ(va_wait_end(1, 12000), 1);
    const int i = va_find(VTT_DONE, 1);
    ASSERT_TRUE(i >= 0);
    ASSERT_EQ(va_count(VTT_ERROR, 1), 0);
    ASSERT_TRUE(g_played >= 10);
    ASSERT_EQ((int)voice_audio_test_render_frames(1), 36000);
    if (i >= 0) {
        /* DONE came after the audio's last progress, not before, and 2 s after it */
        const ULONGLONG gap = g_log[i].tick - g_progress_tick;
        ASSERT_TRUE(g_log[i].tick > g_progress_tick);
        ASSERT_TRUE(gap >= 1800 && gap <= 6000);
    }
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

int test_voice_ais_dead_worker_never_pushes(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    g_fk.frames = 24000;
    g_fk.hold_after_first = 1;
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 7, "One. Two.", 0, &t, "key");
    ULONGLONG end = GetTickCount64() + 5000;
    while (!g_fk.first_pushed && GetTickCount64() < end) va_pump(10);
    ASSERT_EQ((int)g_fk.first_pushed, 1);
    voice_ais_quit(VA_TOKEN, 50);          /* the fetch is blocked: detached */
    ASSERT_EQ(voice_ais_running(), 0);
    ASSERT_EQ(voice_ledger_count(VRES_SPEECH_REQ), 1);   /* kept until VAI_EXITED */
    ASSERT_EQ(voice_ledger_count(VRES_RENDER), 0);
    va_pump(300);
    const uint64_t f0 = voice_audio_test_render_frames(7);
    SetEvent(g_fk.release);                /* the fetch goes on pushing */
    end = GetTickCount64() + 5000;
    while (g_fk.running && GetTickCount64() < end) va_pump(10);
    ASSERT_EQ((int)g_fk.running, 0);
        va_pump(200);
    /* render_close already dropped the audio, so the frame count alone proves little (M2):
     * the fetch's own refused-push count is the evidence. Its first refused push ends it. */
    ASSERT_EQ((int)g_fk.refused, 1);
    ASSERT_EQ((int)voice_audio_test_render_frames(7), (int)f0);
    ASSERT_TRUE(va_wait_ledger(VRES_SPEECH_REQ, 0, 3000));   /* released by its VAI_EXITED */
    ASSERT_EQ(g_ais_seen[VAI_EXITED], 1);
    ASSERT_EQ(g_ais_seen[VAI_CHUNK_DONE], 0);              /* a dead worker posts nothing else */
    ASSERT_EQ(g_ais_seen[VAI_FAILED], 0);
    ASSERT_EQ(va_count(VTT_DONE, 7) + va_count(VTT_ERROR, 7), 0);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    ASSERT_EQ(voice_ledger_underflow(), 0);
    va_end(w);
    TEST_END();
}

int test_voice_ais_resolve_error_at_once(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    t.url[0] = '\0';
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 5, "Hello there.", 3, &t, "key");
    ASSERT_EQ(voice_ledger_count(VRES_SPEECH_REQ), 0);
    ASSERT_EQ(voice_ledger_count(VRES_RENDER), 0);
    ASSERT_EQ(va_wait_end(5, 2000), 1);
    const int i = va_find(VTT_ERROR, 5);
    ASSERT_TRUE(i >= 0);
    if (i >= 0) {
        ASSERT_EQ((int)g_log[i].hr, (int)VOICE_AIS_HR_RESOLVE);
        ASSERT_EQ((int)g_log[i].offset, 3);
    }
    va_pump(100);
    ASSERT_EQ((int)g_fk.calls, 0);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    voice_ais_quit(VA_TOKEN, 3000);
    va_end(w);
    TEST_END();
}

int test_voice_ais_sample_self_quits(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    ASSERT_EQ(voice_ais_sample(w, &t, "key"), 1);
    ASSERT_EQ(voice_ais_owns(VOICE_AIS_SAMPLE_TOKEN), 1);
    ASSERT_EQ(va_wait_end(1, 6000), 1);
    const int i = va_find(VTT_DONE, 1);
    ASSERT_TRUE(i >= 0);
    if (i >= 0) ASSERT_EQ((int)g_log[i].token, (int)VOICE_AIS_SAMPLE_TOKEN);
    va_pump(300);
    ASSERT_EQ(voice_ais_running(), 0);
    ASSERT_EQ(voice_ais_owns(VOICE_AIS_SAMPLE_TOKEN), 0);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    ASSERT_EQ(voice_ledger_underflow(), 0);
    va_end(w);
    TEST_END();
}

/* ---- fix round 1 ---- */

static void va_two_sentences(char *text, size_t cap, size_t *second)
{
    char sent[200];
    sent[0] = '\0';
    for (int i = 0; i < 28; i++) strcat(sent, "abcd ");
    strcat(sent, "end.");
    snprintf(text, cap, "%s %s", sent, sent);
    if (second) *second = strlen(sent) + 1;
}

/* I1: a lost CHUNK_DONE must still end the reply with a terminal message. */
int test_voice_ais_lost_chunk_done_still_ends(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    g_drop_chunk_done = 1;
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, "key");
    ASSERT_EQ(va_wait_end(1, 6000), 1);
    ASSERT_EQ(va_count(VTT_DONE, 1), 0);
    const int i = va_find(VTT_ERROR, 1);
    ASSERT_TRUE(i >= 0);
    if (i >= 0) ASSERT_EQ((int)g_log[i].hr, (int)VOICE_AIS_HR_STREAM);
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

/* I2: a chunk that failed after part of it was pushed resumes inside it, not at its end. */
int test_voice_ais_partial_chunk_failure_offset(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    char text[400];
    va_two_sentences(text, sizeof(text), NULL);
    text[144] = '\0';                      /* one chunk of 144 bytes */
    g_fk.frames = 24000;                   /* 1 s: about 15 bytes of it at the estimate */
    g_fk.ret = VOICE_AIS_HR_NET;
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 1, text, 0, &t, "key");
    ASSERT_EQ(va_wait_end(1, 8000), 1);
    const int i = va_find(VTT_ERROR, 1);
    ASSERT_TRUE(i >= 0);
    if (i >= 0) {
        ASSERT_EQ((int)g_log[i].hr, (int)VOICE_AIS_HR_NET);
        ASSERT_TRUE(g_log[i].offset > 0 && g_log[i].offset < 72);
    }
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

/* I3: a re-speak under the same reply id is not confused with the first speak's messages. */
int test_voice_ais_same_reply_id_respeak(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    char text[400];
    va_two_sentences(text, sizeof(text), NULL);
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 5, "One. Two.", 0, &t, "key");   /* one chunk */
    ULONGLONG end = GetTickCount64() + 5000;
    while ((g_fk.calls < 1 || g_fk.running) && GetTickCount64() < end) Sleep(5);
    Sleep(150);                            /* its CHUNK_DONE is posted, not yet pumped */
    ASSERT_EQ((int)g_fk.calls, 1);
    voice_ais_speak(VA_TOKEN, 5, text, 2, &t, "key");          /* two chunks, same id */
    ASSERT_EQ(va_wait_end(5, 8000), 1);
    va_pump(300);
    ASSERT_EQ(va_count(VTT_DONE, 5), 1);
    ASSERT_EQ(va_count(VTT_ERROR, 5), 0);
    ASSERT_EQ((int)g_fk.calls, 3);
    size_t last = 0;
    int ok = 1;
    for (int i = 0; i < g_nlog; i++) {
        if (g_log[i].kind != VTT_WORD) continue;
        if (g_log[i].offset < last) ok = 0;
        if (g_log[i].offset > strlen(text)) ok = 0;
        last = g_log[i].offset;
    }
    ASSERT_EQ(ok, 1);
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

/* M3: text beyond the chunk cap is never reported as read. */
int test_voice_ais_chunk_cap_is_an_error(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    char text[1900], sent[601];
    memset(sent, 'a', 599);
    for (int i = 4; i < 599; i += 5) sent[i] = ' ';
    sent[599] = '.'; sent[600] = '\0';
    snprintf(text, sizeof(text), "%s %s %s", sent, sent, sent);
    size_t starts[8], lens[8];
    const size_t all = voice_speak_chunks(text, 0, starts, lens, 8);
    ASSERT_TRUE(all > 2);
    voice_ais_test_max_chunks(2);
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 1, text, 0, &t, "key");
    ASSERT_EQ(va_wait_end(1, 8000), 1);
    ASSERT_EQ(va_count(VTT_DONE, 1), 0);
    const int i = va_find(VTT_ERROR, 1);
    ASSERT_TRUE(i >= 0);
    if (i >= 0) {
        ASSERT_EQ((int)g_log[i].hr, (int)VOICE_AIS_HR_STREAM);
        ASSERT_EQ((int)g_log[i].offset, (int)(starts[1] + lens[1]));
    }
    voice_ais_quit(VA_TOKEN, 3000);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

/* M5: quit_all with the conversation and the sample both alive and speaking. */
int test_voice_ais_quit_all_two_contexts(void)
{
    TEST_BEGIN();
    HWND w = va_begin();
    ASSERT_NOT_NULL(w);
    VoiceAisTarget t = va_target();
    g_fk.frames = 72000;
    g_fk.sleep_ms = 10;
    ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
    voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, "key");
    ASSERT_EQ(voice_ais_sample(w, &t, "key"), 1);
    ASSERT_EQ(voice_ais_running(), 2);
    va_wait_played(2, 5000);
    voice_ais_quit_all(3000);
    ASSERT_EQ(voice_ais_running(), 0);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    ASSERT_EQ(voice_ledger_underflow(), 0);
    va_pump(300);
    ASSERT_EQ(voice_ledger_all_zero(), 1);
    va_end(w);
    TEST_END();
}

/* ---- Task 7b: the real fetch against the loopback server ---- */

#define VR_FIX_WAV "tests/fixtures/voice_tts/provisional_mistral_wav.sse"
#define VR_CAP_WAV "tests/fixtures/voice_tts/mistral_stream_wav.sse"
#define VR_KEY "test-key-0000"

static HWND vr_begin(void)
{
    HWND w = va_begin();
    if (w) {
        voice_ais_set_fetch(NULL);          /* voice_ais_start installs the real fetch */
        voice_net_test_reset_count();
    }
    return w;
}

static VoiceAisTarget vr_target(int port, VoiceAisShape shape)
{
    VoiceAisTarget t;
    memset(&t, 0, sizeof(t));
    snprintf(t.url, sizeof(t.url), "http://127.0.0.1:%d/v1/audio/speech", port);
    snprintf(t.model, sizeof(t.model), "tts-model");
    snprintf(t.voice, sizeof(t.voice), "alloy");
    snprintf(t.provider, sizeof(t.provider), shape == VAIS_SHAPE_MISTRAL ? "mistral" : "custom");
    t.shape = shape;
    return t;
}

static int vr_under_wine(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    return nt != NULL && GetProcAddress(nt, "wine_get_version") != NULL;
}

/* Speaks `text` as reply 1 and waits for its end; 1 when an end arrived. */
static int vr_speak(HWND w, const VoiceAisTarget *t, const char *key, const char *text)
{
    if (voice_ais_start(w, VA_TOKEN) != 1) return 0;
    voice_ais_speak(VA_TOKEN, 1, text, 0, t, key);
    return va_wait_end(1, 10000);
}

static long vr_error_hr(void)
{
    const int i = va_find(VTT_ERROR, 1);
    return i >= 0 ? g_log[i].hr : 12345;
}

static char *vr_read(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) { b[n] = '\0'; if (len) *len = (size_t)n; }
    return b;
}

static int vr_write(const char *path, const char *a, size_t an, const char *b, size_t bn)
{
    FILE *f = test_fopen_private(path);
    if (!f) return 0;
    if (an) fwrite(a, 1, an, f);
    if (bn) fwrite(b, 1, bn, f);
    fclose(f);
    return 1;
}

/* Runs one Mistral reply against an SSE file; 1 when an end arrived. */
static int vr_mistral_file(HWND w, HttpLoop **hp, const char *path, const char *key)
{
    HlAudio a;
    memset(&a, 0, sizeof(a));
    a.sse_file = path;
    *hp = hl_start(HLS_SSE, &a);
    if (!*hp) return 0;
    VoiceAisTarget t = vr_target(hl_port(*hp), VAIS_SHAPE_MISTRAL);
    return vr_speak(w, &t, key, "One. Two.");
}

int test_voice_ais_custom_wav_done(void)
{
    TEST_BEGIN();
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    HlAudio a = { 1, 0, 0.25, NULL };
    HttpLoop *h = hl_start(HLS_WAV, &a);
    ASSERT_NOT_NULL(h);
    if (w && h) {
        VoiceAisTarget t = vr_target(hl_port(h), VAIS_SHAPE_OPENAI);
        ASSERT_EQ(vr_speak(w, &t, VR_KEY, "One. Two."), 1);
        va_pump(100);
        ASSERT_EQ(va_count(VTT_DONE, 1), 1);
        ASSERT_EQ(va_count(VTT_ERROR, 1), 0);
        ASSERT_TRUE(strstr(hl_request_head(h), "Authorization: Bearer " VR_KEY) != NULL);
        ASSERT_TRUE(strstr(hl_request_head(h), "POST /v1/audio/speech") != NULL);
        size_t bn = 0;
        const char *body = hl_request_body(h, &bn);
        ASSERT_TRUE(body != NULL && strstr(body, "\"input\":\"One. Two.\"") != NULL);
        ASSERT_TRUE(body != NULL && strstr(body, "\"response_format\":\"wav\"") != NULL);
        ASSERT_EQ((int)voice_net_open_count(), 1);
        ASSERT_EQ(hl_requests(h), 1);
        const long fr = (long)voice_audio_test_render_frames(1);
        ASSERT_TRUE(fr >= 5999 && fr <= 6001);
        voice_ais_quit(VA_TOKEN, 3000);
        ASSERT_EQ(voice_ledger_all_zero(), 1);
    }
    if (h) hl_stop(h);
    va_end(w);
    TEST_END();
}

int test_voice_ais_custom_no_key(void)
{
    TEST_BEGIN();
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    HttpLoop *h = hl_start(HLS_WAV, NULL);
    ASSERT_NOT_NULL(h);
    if (w && h) {
        VoiceAisTarget t = vr_target(hl_port(h), VAIS_SHAPE_OPENAI);
        ASSERT_EQ(vr_speak(w, &t, "", "One. Two."), 1);
        ASSERT_EQ(va_count(VTT_DONE, 1), 1);
        ASSERT_TRUE(strstr(hl_request_head(h), "Authorization") == NULL);
        ASSERT_TRUE(strstr(hl_request_head(h), "POST /v1/audio/speech") != NULL);
        voice_ais_quit(VA_TOKEN, 3000);
    }
    if (h) hl_stop(h);
    va_end(w);
    TEST_END();
}

/* The frames a clean decode of an SSE file gives (the expectation, built apart from the fetch). */
typedef struct { VoiceAisSse st; VoiceWavStream ws; } VrCount;

static int vr_count_line(const char *line, size_t len, void *ctx)
{
    VrCount *x = (VrCount *)ctx;
    VoiceAisEv ev;
    int over = voice_ais_sse_line(&x->st, line, len, &ev);
    if (ev.kind == VAIS_EV_AUDIO) {
        const size_t cap = voice_wavs_out_max(ev.audio_len);
        int16_t *o = (int16_t *)malloc(cap * sizeof(int16_t));
        if (o) { (void)voice_wavs_feed(&x->ws, ev.audio, ev.audio_len, o, cap); free(o); }
        free(ev.audio);
    }
    return over;
}

static uint64_t vr_fixture_frames(const char *path)
{
    size_t n = 0;
    char *raw = vr_read(path, &n);
    if (!raw) return 0;
    SseLine sl;
    sse_line_init(&sl);
    static VrCount c;
    memset(&c, 0, sizeof(c));
    voice_ais_sse_init(&c.st);
    voice_wavs_init(&c.ws, VWS_RAW_NONE);
    sse_line_feed(&sl, raw, n, vr_count_line, &c);
    sse_line_free(&sl);
    free(raw);
    return voice_wavs_frames(&c.ws);
}

static int vr_mistral_checks(HWND w, const char *path)   /* 0 when every check held */
{
    int _tf_local_fail = 0;
    HttpLoop *h = NULL;
    const uint64_t want = vr_fixture_frames(path);
    int got_end = vr_mistral_file(w, &h, path, VR_KEY);
    ASSERT_EQ(got_end, 1);
    ASSERT_TRUE(want > 1000);
    va_pump(100);
    ASSERT_EQ(va_count(VTT_DONE, 1), 1);
    ASSERT_EQ(va_count(VTT_ERROR, 1), 0);
    const long fr = (long)voice_audio_test_render_frames(1);
    ASSERT_TRUE(fr >= (long)want - 1 && fr <= (long)want + 1);
    if (h) {
        size_t bn = 0;
        const char *body = hl_request_body(h, &bn);
        ASSERT_TRUE(body != NULL && strstr(body, "\"voice_id\"") != NULL);
        ASSERT_TRUE(body != NULL && strstr(body, "\"stream\":true") != NULL);
        ASSERT_TRUE(strstr(hl_request_head(h), "Accept: text/event-stream") != NULL);
        ASSERT_TRUE(strstr(hl_request_head(h), "Authorization: Bearer " VR_KEY) != NULL);
        hl_stop(h);
    }
    voice_ais_quit(VA_TOKEN, 3000);
    return _tf_local_fail;
}

int test_voice_ais_mistral_sse(void)
{
    TEST_BEGIN();
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    if (w) ASSERT_EQ(vr_mistral_checks(w, VR_FIX_WAV), 0);
    va_end(w);
    TEST_END();
}

int test_voice_ais_mistral_capture(void)
{
    TEST_BEGIN();
    FILE *f = fopen(VR_CAP_WAV, "rb");
    if (!f) TEST_SKIP("no capture: " VR_CAP_WAV " (Task 0)");
    fclose(f);
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    if (w) ASSERT_EQ(vr_mistral_checks(w, VR_CAP_WAV), 0);
    va_end(w);
    TEST_END();
}

/* One event of the stream ends in an error, or only the done event arrives: each fails. */
static int vr_expect_stream_fail_hr(int mode, long want_hr)   /* 0 when every check held */
{
    int _tf_local_fail = 0;
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    HttpLoop *h = hl_start(mode, NULL);
    ASSERT_NOT_NULL(h);
    if (w && h) {
        VoiceAisTarget t = vr_target(hl_port(h), VAIS_SHAPE_MISTRAL);
        ASSERT_EQ(vr_speak(w, &t, VR_KEY, "One. Two."), 1);
        va_pump(100);
        ASSERT_EQ(va_count(VTT_DONE, 1), 0);
        ASSERT_EQ(va_count(VTT_ERROR, 1), 1);
        ASSERT_EQ((int)vr_error_hr(), (int)want_hr);
        voice_ais_quit(VA_TOKEN, 3000);
    }
    if (h) hl_stop(h);
    va_end(w);
    return _tf_local_fail;
}

static int vr_expect_stream_fail(int mode) { return vr_expect_stream_fail_hr(mode, VOICE_AIS_HR_STREAM); }

/* A stream of valid WAV audio, then an error event, then the done event: the error must end it
 * as a passing stream failure. The done event after it is the discriminator: with the error
 * event ignored the stream would end in done and play to the end, so a stream that merely stops
 * (no done) proves nothing. */
int test_voice_ais_sse_wav_then_error_event(void)
{
    TEST_BEGIN();
    static const char *const path = TEST_TMP_DIR "/nutshell_vais_wavthenerr.sse";
    static const char err_ev[] = "event: error\ndata: {\"error\": {\"message\": \"interrupted\", "
                                 "\"type\": \"server_error\", \"code\": 500}}\n\n";
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    size_t n = 0;
    char *fix = vr_read(VR_FIX_WAV, &n);
    ASSERT_NOT_NULL(fix);
    const char *first_end = fix ? strstr(fix, "\n\n") : NULL;
    ASSERT_NOT_NULL(first_end);
    ASSERT_TRUE(first_end == NULL || strstr(first_end + 2, "speech.audio.done") != NULL);
    char *mix = NULL;
    size_t total = 0;
    if (first_end) {
        const size_t head = (size_t)(first_end - fix) + 2, rest = n - head;
        mix = (char *)malloc(head + sizeof(err_ev) - 1 + rest + 1);
        ASSERT_NOT_NULL(mix);
        if (mix) {                          /* delta, the error event, then the fixture's done */
            memcpy(mix, fix, head);
            memcpy(mix + head, err_ev, sizeof(err_ev) - 1);
            memcpy(mix + head + sizeof(err_ev) - 1, fix + head, rest);
            total = head + sizeof(err_ev) - 1 + rest;
        }
    }
    if (w && first_end && mix) {
        ASSERT_EQ(vr_write(path, mix, total, NULL, 0), 1);
        HttpLoop *h = NULL;
        ASSERT_EQ(vr_mistral_file(w, &h, path, VR_KEY), 1);
        va_pump(100);
        ASSERT_EQ(va_count(VTT_DONE, 1), 0);
        ASSERT_EQ((int)vr_error_hr(), (int)VOICE_AIS_HR_STREAM);
        if (h) hl_stop(h);
        voice_ais_quit(VA_TOKEN, 3000);
    }
    free(mix);
    free(fix);
    remove(path);
    va_end(w);
    TEST_END();
}

/* The canned error fixture's first delta is raw PCM, not WAV, so (F5) it fails as an unsupported
 * format rather than as a stream failure. */
int test_voice_ais_sse_error_fixture_is_format(void)
{
    TEST_BEGIN();
    ASSERT_EQ(vr_expect_stream_fail_hr(HLS_SSE_ERROR, VOICE_AIS_HR_FORMAT), 0);
    TEST_END();
}
int test_voice_ais_sse_done_only(void) { TEST_BEGIN(); ASSERT_EQ(vr_expect_stream_fail(HLS_SSE_DONE_ONLY), 0); TEST_END(); }

/* Each custom-server failure of the decoder or the stream, by mode and by the hr it must give. */
static int vr_expect_custom(int mode, long want_hr)   /* 0 when every check held */
{
    int _tf_local_fail = 0;
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    HttpLoop *h = hl_start(mode, NULL);
    ASSERT_NOT_NULL(h);
    if (w && h) {
        VoiceAisTarget t = vr_target(hl_port(h), VAIS_SHAPE_OPENAI);
        ASSERT_EQ(vr_speak(w, &t, VR_KEY, "One. Two."), 1);
        va_pump(100);
        ASSERT_EQ(va_count(VTT_DONE, 1), 0);
        ASSERT_EQ(va_count(VTT_ERROR, 1), 1);
        ASSERT_EQ((int)vr_error_hr(), (int)want_hr);
        voice_ais_quit(VA_TOKEN, 3000);
        ASSERT_EQ(voice_ledger_all_zero(), 1);
    }
    if (h) hl_stop(h);
    va_end(w);
    return _tf_local_fail;
}

int test_voice_ais_badrate_is_format(void) { TEST_BEGIN(); ASSERT_EQ(vr_expect_custom(HLS_WAV_BADRATE, VOICE_AIS_HR_FORMAT), 0); TEST_END(); }
/* F5: a 200 whose body is not WAV (an MP3 tag here) cannot work: unsupported audio format. A stream
 * that began as valid WAV and broke later (second_header_fails below) stays a stream failure. */
int test_voice_ais_not_wav_is_format(void)
{
    TEST_BEGIN();
    ASSERT_EQ(vr_expect_custom(HLS_NOTWAV, VOICE_AIS_HR_FORMAT), 0);
    ASSERT_EQ(voice_ais_failure_fatal(VOICE_AIS_HR_FORMAT), 1);
    TEST_END();
}
int test_voice_ais_second_header_fails(void) { TEST_BEGIN(); ASSERT_EQ(vr_expect_custom(HLS_WAV_TWICE, VOICE_AIS_HR_STREAM), 0); TEST_END(); }
int test_voice_ais_empty_body_fails(void) { TEST_BEGIN(); ASSERT_EQ(vr_expect_custom(HLS_EMPTY, VOICE_AIS_HR_STREAM), 0); TEST_END(); }
int test_voice_ais_http_401(void) { TEST_BEGIN(); ASSERT_EQ(vr_expect_custom(HLS_401, 401), 0); TEST_END(); }
int test_voice_ais_http_429(void) { TEST_BEGIN(); ASSERT_EQ(vr_expect_custom(HLS_429, 429), 0); TEST_END(); }
/* F3: a redirect is not followed and not retried: the status is the failure, and it is fatal. */
int test_voice_ais_http_302_redirect(void)
{
    TEST_BEGIN();
    ASSERT_EQ(vr_expect_custom(HLS_302, 302), 0);
    ASSERT_EQ(vr_expect_custom(HLS_307, 307), 0);
    ASSERT_EQ(voice_ais_failure_fatal(302), 1);
    TEST_END();
}
int test_voice_ais_http_404_and_422(void)
{
    TEST_BEGIN();
    ASSERT_EQ(vr_expect_custom(HLS_404, 404), 0);
    ASSERT_EQ(vr_expect_custom(HLS_422, 422), 0);
    TEST_END();
}

/* An SSE line over SSE_LINE_MAX is discarded and counted; the rest of the stream is otherwise
 * a clean reply, so only the skipped-line check can fail it. */
int test_voice_ais_oversized_line_fails(void)
{
    TEST_BEGIN();
    static const char *const path = TEST_TMP_DIR "/nutshell_vais_oversize.sse";
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    size_t n = 0;
    char *fix = vr_read(VR_FIX_WAV, &n);
    ASSERT_NOT_NULL(fix);
    if (w && fix) {
        const size_t big = SSE_LINE_MAX + 1000;
        char *line = (char *)malloc(big + 3);
        ASSERT_NOT_NULL(line);
        if (line) {
            line[0] = ':'; line[1] = ' ';
            memset(line + 2, 'x', big);
            line[big + 2] = '\n';
            ASSERT_EQ(vr_write(path, line, big + 3, fix, n), 1);
            free(line);
            HttpLoop *h = NULL;
            ASSERT_EQ(vr_mistral_file(w, &h, path, VR_KEY), 1);
            va_pump(100);
            ASSERT_EQ(va_count(VTT_DONE, 1), 0);
            ASSERT_EQ((int)vr_error_hr(), (int)VOICE_AIS_HR_STREAM);
            if (h) hl_stop(h);
            voice_ais_quit(VA_TOKEN, 3000);
        }
    }
    free(fix);
    remove(path);
    va_end(w);
    TEST_END();
}

/* Audio but no speech.audio.done: the stream was cut, so it is a failure (R32). */
int test_voice_ais_mistral_no_done_fails(void)
{
    TEST_BEGIN();
    static const char *const path = TEST_TMP_DIR "/nutshell_vais_nodone.sse";
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    size_t n = 0;
    char *fix = vr_read(VR_FIX_WAV, &n);
    ASSERT_NOT_NULL(fix);
    if (w && fix) {
        const char *done = strstr(fix, "event: speech.audio.done");
        ASSERT_NOT_NULL(done);
        if (done) {
            ASSERT_EQ(vr_write(path, fix, (size_t)(done - fix), NULL, 0), 1);
            HttpLoop *h = NULL;
            ASSERT_EQ(vr_mistral_file(w, &h, path, VR_KEY), 1);
            va_pump(100);
            ASSERT_EQ(va_count(VTT_DONE, 1), 0);
            ASSERT_EQ((int)vr_error_hr(), (int)VOICE_AIS_HR_STREAM);
            if (h) hl_stop(h);
            voice_ais_quit(VA_TOKEN, 3000);
        }
    }
    free(fix);
    remove(path);
    va_end(w);
    TEST_END();
}

/* A stream that stops mid-frame, then says done: the decoder is in DATA with a partial frame
 * and frames > 0, so only voice_wavs_finish_ok can fail it. */
int test_voice_ais_partial_frame_fails(void)
{
    TEST_BEGIN();
    static const char *const path = TEST_TMP_DIR "/nutshell_vais_partial.sse";
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    unsigned char wav[44 + 5];
    memcpy(wav, "RIFF", 4); memset(wav + 4, 0xFF, 4); memcpy(wav + 8, "WAVEfmt ", 8);
    const unsigned char fmt[] = { 16,0,0,0, 1,0, 1,0, 0xC0,0x5D,0,0, 0x80,0xBB,0,0, 2,0, 16,0 };
    memcpy(wav + 16, fmt, sizeof(fmt));
    memcpy(wav + 36, "data", 4); memset(wav + 40, 0xFF, 4);
    const unsigned char pcm[5] = { 1, 2, 3, 4, 5 };   /* two frames and half of a third */
    memcpy(wav + 44, pcm, 5);
    char b64[128];
    const size_t bl = base64_encode(wav, sizeof(wav), b64, sizeof(b64));
    ASSERT_TRUE(bl > 0 && bl < sizeof(b64));
    if (w && bl > 0 && bl < sizeof(b64)) {
        b64[bl] = '\0';
        char text[400];
        snprintf(text, sizeof(text),
                 "event: speech.audio.delta\ndata: {\"type\": \"speech.audio.delta\", \"audio_data\": \"%s\"}\n\n"
                 "event: speech.audio.done\ndata: {\"type\": \"speech.audio.done\"}\n\n", b64);
        ASSERT_EQ(vr_write(path, text, strlen(text), NULL, 0), 1);
        HttpLoop *h = NULL;
        ASSERT_EQ(vr_mistral_file(w, &h, path, VR_KEY), 1);
        va_pump(100);
        ASSERT_EQ(va_count(VTT_DONE, 1), 0);
        ASSERT_EQ((int)vr_error_hr(), (int)VOICE_AIS_HR_STREAM);
        if (h) hl_stop(h);
        voice_ais_quit(VA_TOKEN, 3000);
    }
    remove(path);
    va_end(w);
    TEST_END();
}

/* F1: a Custom server that never stops sending audio. The worker fails the chunk as a stream
 * failure once the cap's worth of audio has been taken, closes the request (the server's sends
 * then fail) and queues no more than the cap. Wine moves this body at about 70 KB/s, so the
 * test lowers the cap (the seam) to 10 s of audio; the real cap's arithmetic is a native test. */
int test_voice_ais_endless_audio_capped(void)
{
    TEST_BEGIN();
    const uint64_t cap_frames = 24000ull * 10;
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    HttpLoop *h = hl_start(HLS_WAV_ENDLESS, NULL);
    ASSERT_NOT_NULL(h);
    if (w && h) {
        voice_ais_test_max_frames(cap_frames);
        VoiceAisTarget t = vr_target(hl_port(h), VAIS_SHAPE_OPENAI);
        ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
        const ULONGLONG t0 = GetTickCount64();
        voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, VR_KEY);
        ASSERT_EQ(va_wait_end(1, 30000), 1);
        ASSERT_TRUE(GetTickCount64() - t0 < 30000);
        va_pump(100);
        ASSERT_EQ(va_count(VTT_DONE, 1), 0);
        ASSERT_EQ(va_count(VTT_ERROR, 1), 1);
        ASSERT_EQ((int)vr_error_hr(), (int)VOICE_AIS_HR_STREAM);
        /* the server wrote at least the cap's bytes (2 per frame) and not an unbounded amount */
        const unsigned long long cap_bytes = cap_frames * 2ull;
        ASSERT_TRUE(hl_bytes_sent(h) >= cap_bytes);
        ASSERT_TRUE(hl_bytes_sent(h) < cap_bytes + 32ull * 1024 * 1024);
        /* the play queue holds no more than the cap's 10 s */
        ASSERT_TRUE(voice_audio_render_queued_ms() <= 10000 + 100);
        voice_ais_quit(VA_TOKEN, 3000);
        ASSERT_EQ(voice_ledger_all_zero(), 1);
        voice_ais_test_max_frames(0);
    }
    if (h) hl_stop(h);
    va_end(w);
    TEST_END();
}

/* The wall-clock cap: a server that keeps sending past the request's time budget is closed and
 * the chunk fails as a stream, whatever the frame count (the frame cap stays at its default, far
 * out of reach). The seam lowers the 6 minutes to 1.5 s. */
int test_voice_ais_request_time_capped(void)
{
    TEST_BEGIN();
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    HttpLoop *h = hl_start(HLS_WAV_ENDLESS, NULL);
    ASSERT_NOT_NULL(h);
    if (w && h) {
        voice_ais_test_max_request_ms(1500);
        VoiceAisTarget t = vr_target(hl_port(h), VAIS_SHAPE_OPENAI);
        ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
        const ULONGLONG t0 = GetTickCount64();
        voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, VR_KEY);
        ASSERT_EQ(va_wait_end(1, 30000), 1);
        ASSERT_TRUE(GetTickCount64() - t0 < 30000);
        va_pump(100);
        ASSERT_EQ(va_count(VTT_DONE, 1), 0);
        ASSERT_EQ(va_count(VTT_ERROR, 1), 1);
        ASSERT_EQ((int)vr_error_hr(), (int)VOICE_AIS_HR_STREAM);
        voice_ais_quit(VA_TOKEN, 3000);
        ASSERT_EQ(voice_ledger_all_zero(), 1);
        voice_ais_test_max_request_ms(0);
    }
    if (h) hl_stop(h);
    va_end(w);
    TEST_END();
}

/* F1: an error status with a body that never ends: the body read stops at 64 KB, the request is
 * closed and the status still decides (500). */
int test_voice_ais_endless_error_body_capped(void)
{
    TEST_BEGIN();
    ASSERT_EQ(vr_expect_custom(HLS_500_ENDLESS, 500), 0);
    TEST_END();
}

/* A server that never answers: quit fires the request's token and returns well inside 4 s. */
int test_voice_ais_quit_while_server_hangs(void)
{
    TEST_BEGIN();
    int skipped = 0;
    HWND w = vr_begin();
    ASSERT_NOT_NULL(w);
    HttpLoop *h = hl_start(HLS_HANG, NULL);
    ASSERT_NOT_NULL(h);
    if (w && h) {
        VoiceAisTarget t = vr_target(hl_port(h), VAIS_SHAPE_OPENAI);
        ASSERT_EQ(voice_ais_start(w, VA_TOKEN), 1);
        voice_ais_speak(VA_TOKEN, 1, "One. Two.", 0, &t, VR_KEY);
        ULONGLONG end = GetTickCount64() + 6000;
        while (hl_requests(h) < 1 && GetTickCount64() < end) va_pump(10);
        ASSERT_EQ(hl_requests(h), 1);
        const ULONGLONG t0 = GetTickCount64();
        voice_ais_quit(VA_TOKEN, 3000);
        ASSERT_TRUE(GetTickCount64() - t0 < 4000);
        if (voice_ledger_count(VRES_SPEECH_REQ) != 0 && vr_under_wine()) {
            skipped = 1;                    /* Wine does not wake a blocked WinHTTP call */
        } else {
            ASSERT_EQ(voice_ledger_count(VRES_SPEECH_REQ), 0);
            ASSERT_EQ(voice_ledger_all_zero(), 1);
        }
    }
    if (h) hl_stop(h);                      /* closes the server side, which frees a Wine-blocked call */
    if (skipped) (void)va_wait_ledger(VRES_SPEECH_REQ, 0, 4000);
    va_end(w);
    if (skipped) TEST_SKIP("Wine does not abort a synchronous WinHTTP call");
    TEST_END();
}

#endif /* _WIN32 */
