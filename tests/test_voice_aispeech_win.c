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

#endif /* _WIN32 */
