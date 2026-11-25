/* tests/test_voice_live_win.c -- the live transcription session against a
 * loopback server scripted as a fake provider. Built only by `make wintest`.
 * Wine cannot wake a blocked WebSocket receive or abort a blocked handshake
 * from the client side (measured in test_voice_ws_win.c); a test that needs
 * that takes a defined SKIP and ends the session from the server side. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "wav.h"
#include "voice_live_win.h"
#include "voice_live.h"
#include "voice_ledger.h"
#include "voice_gate.h"
#include "ai_gate.h"
#include "ws_loopback_win.h"

static void gates(int on)
{
    ai_gate_set_enabled(on);
    voice_gate_set_enabled(on);
}

static LRESULT CALLBACK vl_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static HWND vl_window(void)
{
    static int registered;
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = vl_wndproc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "NsVoiceLiveTest";
        RegisterClassA(&wc);
        registered = 1;
    }
    return CreateWindowExA(0, "NsVoiceLiveTest", "", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
}

static int running_under_wine(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    return nt && GetProcAddress(nt, "wine_get_version") != NULL;
}

/* ---- what the UI sees --------------------------------------------------- */

#define LOG_MAX 64
typedef struct {
    int kinds[LOG_MAX];
    unsigned tokens[LOG_MAX];
    int has_text[LOG_MAX];
    int status[LOG_MAX];
    ULONGLONG tick[LOG_MAX];
    int n;
    int wakes;                  /* NULL-payload exit wake-ups seen */
} UiLog;

static int log_count(const UiLog *g, int kind)
{
    int c = 0;
    for (int i = 0; i < g->n; i++) if (g->kinds[i] == kind) c++;
    return c;
}

static int log_index(const UiLog *g, int kind)
{
    for (int i = 0; i < g->n; i++) if (g->kinds[i] == kind) return i;
    return -1;
}

/* Pump until a final (VLE_DONE) or VLE_CLOSED arrives, or `ms` pass. */
static void pump(UiLog *g, DWORD ms, int stop_on_end)
{
    ULONGLONG end = GetTickCount64() + ms;
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message != WM_VOICE_LIVE) continue;
            VoiceLiveMsg *m = (VoiceLiveMsg *)msg.lParam;
            if (!m) { g->wakes++; continue; }       /* a thread's exit wake-up */
            if (g->n < LOG_MAX) {
                g->kinds[g->n] = (int)m->ev.kind;
                g->tokens[g->n] = m->token;
                g->has_text[g->n] = m->ev.text != NULL;
                g->status[g->n] = m->http_status;
                g->tick[g->n] = GetTickCount64();
                g->n++;
            }
            int endk = m->ev.kind == VLE_DONE || m->ev.kind == VLE_CLOSED;
            voice_live_msg_free(m);
            if (endk && stop_on_end) return;
        }
        Sleep(10);
    }
}

/* ---- the fake provider -------------------------------------------------- */

typedef struct {
    int mistral;
    int drop_after_updated;
    int manual_ready;            /* the test sends session.updated itself, later */
    int delay_completed_n;       /* the completion of this commit is held back ... */
    int delay_ms;                /* ... this long */
    int dup_first;               /* the first completion is sent twice */
    int error_after_ready;       /* an error event right after session.updated (not fatal) */
    volatile ULONGLONG commit_tick[8];
    volatile LONG session_update, bad_session, ready_sent;
    volatile LONG appended_before_ready, appends;
    volatile LONG append_bytes, commits, flushes, ends;
} Script;

static size_t b64_decoded(const char *msg)
{
    const char *p = strstr(msg, "\"audio\":\"");
    if (!p) return 0;
    p += 9;
    const char *q = strchr(p, '"');
    if (!q) return 0;
    size_t n = (size_t)(q - p);
    size_t pad = 0;
    if (n >= 1 && q[-1] == '=') pad++;
    if (n >= 2 && q[-2] == '=') pad++;
    return n / 4 * 3 - pad;
}

static int script_on_msg(WsLoop *w, const char *msg, size_t n, void *u)
{
    Script *s = (Script *)u;
    (void)n;
    if (strstr(msg, "\"session.update\"")) {
        InterlockedIncrement(&s->session_update);
        if (s->mistral) {
            if (!strstr(msg, "\"sample_rate\":16000")) InterlockedIncrement(&s->bad_session);
        } else {
            if (!strstr(msg, "\"rate\":24000") || !strstr(msg, "\"turn_detection\":null"))
                InterlockedIncrement(&s->bad_session);
        }
        if (!s->manual_ready) {
            InterlockedExchange(&s->ready_sent, 1);
            ws_loop_send_text(w, "{\"type\":\"session.updated\"}");
            if (s->error_after_ready)
                ws_loop_send_text(w, "{\"type\":\"error\",\"error\":{\"message\":\"boom\"}}");
        }
        return s->drop_after_updated ? 1 : 0;
    }
    if (strstr(msg, "input_audio_buffer.append") || strstr(msg, "input_audio.append")) {
        if (!s->ready_sent) InterlockedIncrement(&s->appended_before_ready);
        InterlockedIncrement(&s->appends);
        InterlockedExchangeAdd(&s->append_bytes, (LONG)b64_decoded(msg));
        return 0;
    }
    if (strstr(msg, "input_audio_buffer.commit")) {
        LONG nth = InterlockedIncrement(&s->commits);
        char item[32], buf[256];
        snprintf(item, sizeof(item), "item_%ld", (long)nth);
        if (nth < 8) s->commit_tick[nth] = GetTickCount64();
        snprintf(buf, sizeof(buf), "{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"%s\"}", item);
        ws_loop_send_text(w, buf);
        snprintf(buf, sizeof(buf), "{\"type\":\"conversation.item.input_audio_transcription.delta\","
                 "\"item_id\":\"%s\",\"delta\":\"hel\"}", item);
        ws_loop_send_text(w, buf);
        if (s->delay_completed_n == (int)nth) Sleep((DWORD)s->delay_ms);
        snprintf(buf, sizeof(buf), "{\"type\":\"conversation.item.input_audio_transcription.completed\","
                 "\"item_id\":\"%s\",\"transcript\":\"hello\"}", item);
        ws_loop_send_text(w, buf);
        if (s->dup_first && nth == 1) ws_loop_send_text(w, buf);   /* a duplicate completion */
        return 0;
    }
    if (strstr(msg, "input_audio.flush")) {
        InterlockedIncrement(&s->flushes);
        return 0;
    }
    if (strstr(msg, "input_audio.end")) {
        InterlockedIncrement(&s->ends);
        ws_loop_send_text(w, "{\"type\":\"transcription.text.delta\",\"text\":\"hel\"}");
        ws_loop_send_text(w, "{\"type\":\"transcription.done\",\"text\":\"hello\"}");
        return 0;
    }
    return 0;
}

static void make_target(VoiceLiveTarget *t, int mistral, int port)
{
    memset(t, 0, sizeof(*t));
    t->prov = mistral ? VLP_MISTRAL : VLP_OPENAI;
    snprintf(t->host, sizeof(t->host), "127.0.0.1");
    t->port = port;
    t->secure = 0;
    snprintf(t->path, sizeof(t->path), "%s",
             mistral ? "/v1/audio/transcriptions/realtime?model=m" : "/v1/realtime?intent=transcription");
    snprintf(t->model, sizeof(t->model), "%s", mistral ? "voxtral-mini-transcribe-realtime-3" : "gpt-live-transcribe");
    t->rate = mistral ? 16000u : 24000u;
    t->hangover_ms = mistral ? VOICE_LIVE_HANGOVER_MISTRAL_MS : VOICE_LIVE_HANGOVER_OPENAI_MS;
    t->gate_rms = PCM_SPEECH_RMS;
}

/* Speech at RMS 2000: a square wave of +-2000. */
static void feed_speech(unsigned token, size_t samples)
{
    static int16_t buf[1600];
    for (int i = 0; i < 1600; i++) buf[i] = (i & 1) ? (int16_t)-2000 : (int16_t)2000;
    while (samples > 0) {
        size_t n = samples > 1600 ? 1600 : samples;
        voice_live_feed(token, buf, n);
        samples -= n;
    }
}

static void feed_silence(unsigned token, size_t samples)
{
    static int16_t buf[1600];
    memset(buf, 0, sizeof(buf));
    while (samples > 0) {
        size_t n = samples > 1600 ? 1600 : samples;
        voice_live_feed(token, buf, n);
        samples -= n;
    }
}

int test_voice_live_openai_flow(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);
    ASSERT_NOT_NULL(l);
    Script s;
    memset(&s, 0, sizeof(s));
    s.manual_ready = 1;
    ws_loop_set_script(l, script_on_msg, &s);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 5, err, sizeof(err)), 1);
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 1);
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_THREAD), 2);
    ASSERT_EQ(ws_loop_send_text(l, "{\"type\":\"session.created\"}"), 0);
    feed_speech(5, 16000);
    feed_silence(5, 16000);
    voice_live_feed_end(5);
    /* The provider is held in its handshake: session.update is in, session.updated is not.
     * The audio queued meanwhile must not reach it. */
    for (int i = 0; i < 300 && !s.session_update; i++) Sleep(10);
    ASSERT_EQ((int)s.session_update, 1);
    Sleep(400);
    ASSERT_EQ((int)s.appends, 0);
    ASSERT_EQ((int)s.commits, 0);
    InterlockedExchange(&s.ready_sent, 1);
    ASSERT_EQ(ws_loop_send_text(l, "{\"type\":\"session.updated\"}"), 0);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 5000, 1);
    int ir = log_index(&g, VLE_READY), id = log_index(&g, VLE_DELTA);
    int ic = log_index(&g, VLE_COMPLETED), ie = log_index(&g, VLE_DONE);
    ASSERT_TRUE(ir >= 0);
    ASSERT_TRUE(id > ir);
    ASSERT_TRUE(ic > id);
    ASSERT_TRUE(ie > ic);
    if (ie >= 0) {
        ASSERT_EQ(g.has_text[ie], 0);       /* the OpenAI final carries no text */
        ASSERT_EQ(g.tokens[ie], 5u);
    }
    ASSERT_EQ((int)s.session_update, 1);
    ASSERT_EQ((int)s.bad_session, 0);
    ASSERT_EQ((int)s.appended_before_ready, 0);
    ASSERT_TRUE(s.appends > 0);                       /* the held audio arrived after session.updated */
    ASSERT_TRUE(s.append_bytes >= (LONG)(24000 * 2 * 13 / 10));
    ASSERT_TRUE(s.append_bytes % 2 == 0);
    ASSERT_EQ((int)s.commits, 1);
    voice_live_close(5);
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 0);
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    if (!running_under_wine()) ASSERT_EQ(voice_live_test_detached_count(), 0);
    ASSERT_EQ(voice_ledger_underflow(), 0);
    ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

int test_voice_live_mistral_flow(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);
    ASSERT_NOT_NULL(l);
    Script s;
    memset(&s, 0, sizeof(s));
    s.mistral = 1;
    ws_loop_set_script(l, script_on_msg, &s);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 1, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 6, err, sizeof(err)), 1);
    ASSERT_EQ(ws_loop_send_text(l, "{\"type\":\"session.created\"}"), 0);
    feed_speech(6, 16000);
    feed_silence(6, 20000);
    voice_live_feed_end(6);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 5000, 1);
    int ir = log_index(&g, VLE_READY), id = log_index(&g, VLE_DELTA), ie = log_index(&g, VLE_DONE);
    ASSERT_TRUE(ir >= 0);
    ASSERT_TRUE(id > ir);
    ASSERT_TRUE(ie > id);
    if (ie >= 0) ASSERT_EQ(g.has_text[ie], 1);   /* transcription.done carries the transcript */
    ASSERT_EQ((int)s.bad_session, 0);
    ASSERT_EQ((int)s.appended_before_ready, 0);
    ASSERT_TRUE(s.append_bytes >= (LONG)(16000 * 2 * 13 / 10));
    ASSERT_EQ((int)s.flushes, 2);                /* the gate close, then the end */
    ASSERT_EQ((int)s.ends, 1);
    voice_live_close(6);
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    if (!running_under_wine()) ASSERT_EQ(voice_live_test_detached_count(), 0);
    ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

int test_voice_live_close_mid_handshake(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_NO_UPGRADE);
    ASSERT_NOT_NULL(l);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 7, err, sizeof(err)), 1);
    Sleep(100);
    ULONGLONG t0 = GetTickCount64();
    voice_live_close(7);
    ASSERT_TRUE(GetTickCount64() - t0 < 200);       /* close never blocks the UI thread */
    voice_live_reap(3000);
    if (running_under_wine() && GetTickCount64() - t0 >= 2900)
        printf("  [SKIP] Wine does not abort a blocked WinHTTP handshake on handle close;"
               " the session was detached; strict on a Windows host only\n");
    else
        ASSERT_TRUE(GetTickCount64() - t0 < 2900);
    ASSERT_TRUE(voice_ledger_all_zero());
    ws_loop_stop(l);                                 /* ends a detached handshake */
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 400, 0);
    ASSERT_EQ(g.n, 0);                               /* nothing after the close */
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

int test_voice_live_server_drops(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);
    ASSERT_NOT_NULL(l);
    Script s;
    memset(&s, 0, sizeof(s));
    s.drop_after_updated = 1;
    ws_loop_set_script(l, script_on_msg, &s);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 8, err, sizeof(err)), 1);
    ASSERT_EQ(ws_loop_send_text(l, "{\"type\":\"session.created\"}"), 0);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 600, 0);                                /* VLE_READY */
    ws_loop_stop(l);                                 /* the server side goes away (closes the socket) */
    l = NULL;
    pump(&g, 5000, 1);
    int ir = log_index(&g, VLE_READY), ic = log_index(&g, VLE_CLOSED);
    ASSERT_TRUE(ir >= 0);
    ASSERT_TRUE(ic > ir);
    ASSERT_EQ(log_index(&g, VLE_DONE), -1);          /* no final: the link ended first */
    voice_live_close(8);
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    if (!running_under_wine()) ASSERT_EQ(voice_live_test_detached_count(), 0);
    if (l) ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

int test_voice_live_stale_token_feed(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);          /* never answers session.created */
    ASSERT_NOT_NULL(l);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 9, err, sizeof(err)), 1);
    ASSERT_EQ((int)voice_live_test_queued(), 0);
    feed_speech(10, 16000);                          /* another recording's token */
    voice_live_feed_end(10);
    ASSERT_EQ((int)voice_live_test_queued(), 0);
    feed_speech(9, 16000);
    ASSERT_TRUE(voice_live_test_queued() > 0);
    voice_live_close(9);
    feed_speech(9, 16000);                           /* closed: ignored too */
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    if (!running_under_wine()) ASSERT_EQ(voice_live_test_detached_count(), 0);
    ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

/* Two utterances, two commits: the second completion is held back, and the first is sent
 * twice. The final must come once, after the second completion (not after the duplicate). */
int test_voice_live_openai_final_waits_for_every_commit(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);
    ASSERT_NOT_NULL(l);
    Script s;
    memset(&s, 0, sizeof(s));
    s.delay_completed_n = 2;
    s.delay_ms = 500;
    s.dup_first = 1;
    ws_loop_set_script(l, script_on_msg, &s);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 11, err, sizeof(err)), 1);
    ASSERT_EQ(ws_loop_send_text(l, "{\"type\":\"session.created\"}"), 0);
    feed_speech(11, 16000);
    feed_silence(11, 16000);        /* the hangover closes the gate: commit 1 */
    feed_speech(11, 16000);
    feed_silence(11, 16000);        /* commit 2 */
    voice_live_feed_end(11);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 5000, 1);
    pump(&g, 400, 0);               /* a second final would show here */
    ASSERT_EQ((int)s.commits, 2);
    ASSERT_EQ(log_count(&g, VLE_DONE), 1);
    ASSERT_EQ(log_count(&g, VLE_COMPLETED), 3);        /* item_1 twice, item_2 once */
    int ie = log_index(&g, VLE_DONE);
    int last_completed = -1;
    for (int i = 0; i < g.n; i++) if (g.kinds[i] == VLE_COMPLETED) last_completed = i;
    ASSERT_TRUE(ie > last_completed);                  /* after the held completion */
    if (ie >= 0 && s.commit_tick[2])
        ASSERT_TRUE(g.tick[ie] >= s.commit_tick[2] + 450);
    voice_live_close(11);
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    if (!running_under_wine()) ASSERT_EQ(voice_live_test_detached_count(), 0);
    ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

/* Silence only: nothing is committed, and the final arrives at once. */
int test_voice_live_openai_zero_commits_final_at_once(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);
    ASSERT_NOT_NULL(l);
    Script s;
    memset(&s, 0, sizeof(s));
    ws_loop_set_script(l, script_on_msg, &s);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 12, err, sizeof(err)), 1);
    ASSERT_EQ(ws_loop_send_text(l, "{\"type\":\"session.created\"}"), 0);
    feed_silence(12, 16000);
    voice_live_feed_end(12);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 3000, 1);
    ASSERT_EQ(log_count(&g, VLE_DONE), 1);
    ASSERT_EQ((int)s.commits, 0);
    ASSERT_EQ((int)s.appends, 0);
    voice_live_close(12);
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    if (!running_under_wine()) ASSERT_EQ(voice_live_test_detached_count(), 0);
    ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

/* A refused upgrade: the closed message carries the HTTP status. */
int test_voice_live_connect_refused_reports_status(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_REJECT_403);
    ASSERT_NOT_NULL(l);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 1, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 13, err, sizeof(err)), 1);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 5000, 1);
    int ic = log_index(&g, VLE_CLOSED);
    ASSERT_TRUE(ic >= 0);
    if (ic >= 0) ASSERT_EQ(g.status[ic], 403);
    voice_live_close(13);
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    if (!running_under_wine()) ASSERT_EQ(voice_live_test_detached_count(), 0);
    ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

/* M11: nothing starts while voice is off: no thread, no ledger count */
int test_voice_live_start_refused_while_voice_off(void)
{
    TEST_BEGIN();
    gates(1);
    voice_gate_set_enabled(0);
    voice_ledger_reset();
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, 1);
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 20, err, sizeof(err)), 0);
    ASSERT_STR_EQ(err, "Voice input is turned off.");
    ASSERT_TRUE(voice_ledger_all_zero());
    ASSERT_EQ(voice_ledger_underflow(), 0);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

/* C1b: the provider upgrades but never sends session.created: VLE_CLOSED after about 10 s */
int test_voice_live_ready_timeout(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);
    ASSERT_NOT_NULL(l);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ULONGLONG t0 = GetTickCount64();
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 21, err, sizeof(err)), 1);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 14000, 1);
    int ic = log_index(&g, VLE_CLOSED);
    ASSERT_TRUE(ic >= 0);
    if (ic >= 0) {
        ASSERT_EQ(g.status[ic], 0);
        ASSERT_TRUE(g.tick[ic] - t0 >= 9500);
        ASSERT_TRUE(g.tick[ic] - t0 <= 12000);
    }
    ASSERT_EQ(log_index(&g, VLE_READY), -1);
    voice_live_close(21);
    ws_loop_stop(l);                       /* ends the receiver's blocked read on any host */
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    if (!running_under_wine()) ASSERT_EQ(voice_live_test_detached_count(), 0);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

/* C1c: a queue that fills (3 minutes unsent) fails the link once, never silently */
int test_voice_live_queue_overflow_fails_link(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);      /* never answers: nothing drains the queue */
    ASSERT_NOT_NULL(l);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 22, err, sizeof(err)), 1);
    feed_speech(22, 16000u * 185u);              /* 185 s of audio > the 180 s queue bound (24 kHz output) */
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 3000, 1);
    ASSERT_EQ(log_count(&g, VLE_CLOSED), 1);
    pump(&g, 300, 0);
    ASSERT_EQ(log_count(&g, VLE_CLOSED), 1);     /* once */
    voice_live_close(22);
    ws_loop_stop(l);
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

/* M2: a provider error event is posted but does not end the session */
int test_voice_live_error_event_not_fatal(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);
    ASSERT_NOT_NULL(l);
    Script s;
    memset(&s, 0, sizeof(s));
    s.error_after_ready = 1;
    ws_loop_set_script(l, script_on_msg, &s);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 23, err, sizeof(err)), 1);
    ASSERT_EQ(ws_loop_send_text(l, "{\"type\":\"session.created\"}"), 0);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 800, 0);
    ASSERT_EQ(log_count(&g, VLE_ERROR), 1);
    ASSERT_EQ(log_count(&g, VLE_CLOSED), 0);
    feed_speech(23, 16000);
    feed_silence(23, 16000);
    voice_live_feed_end(23);
    pump(&g, 5000, 1);
    ASSERT_EQ(log_count(&g, VLE_CLOSED), 0);
    ASSERT_EQ(log_count(&g, VLE_DONE), 1);       /* the session went on to its final */
    ASSERT_EQ((int)s.commits, 1);
    voice_live_close(23);
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

/* M1: under 100 ms appended: no commit is sent or counted, and the final still comes */
int test_voice_live_short_utterance_not_committed(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_SILENT);
    ASSERT_NOT_NULL(l);
    Script s;
    memset(&s, 0, sizeof(s));
    ws_loop_set_script(l, script_on_msg, &s);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 0, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 24, err, sizeof(err)), 1);
    ASSERT_EQ(ws_loop_send_text(l, "{\"type\":\"session.created\"}"), 0);
    feed_speech(24, 640);               /* two 20 ms windows: the gate opens, 40 ms of audio */
    voice_live_feed_end(24);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 4000, 1);
    ASSERT_TRUE(s.appends > 0);
    ASSERT_EQ((int)s.commits, 0);
    ASSERT_EQ(log_count(&g, VLE_DONE), 1);
    voice_live_close(24);
    voice_live_reap(3000);
    ASSERT_TRUE(voice_ledger_all_zero());
    ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

/* I3: both threads post a NULL wake-up as they exit, so the UI reaps at once */
int test_voice_live_exit_wakes_ui(void)
{
    TEST_BEGIN();
    gates(1);
    voice_ledger_reset();
    WsLoop *l = ws_loop_start(WSL_REJECT_403);
    ASSERT_NOT_NULL(l);
    HWND w = vl_window();
    ASSERT_NOT_NULL(w);
    VoiceLiveTarget t;
    make_target(&t, 1, ws_loop_port(l));
    char err[128] = "";
    ASSERT_EQ(voice_live_start(w, &t, "test-key", 25, err, sizeof(err)), 1);
    UiLog g;
    memset(&g, 0, sizeof(g));
    pump(&g, 5000, 1);
    ASSERT_TRUE(log_index(&g, VLE_CLOSED) >= 0);
    voice_live_close(25);
    pump(&g, 1500, 0);                  /* the exit wake-ups arrive without any further call */
    ASSERT_EQ(g.wakes, 2);
    voice_live_reap(0);                 /* what the UI does on the wake-up */
    ASSERT_TRUE(voice_ledger_all_zero());
    ASSERT_EQ(voice_live_test_detached_count(), 0);
    ws_loop_stop(l);
    DestroyWindow(w);
    gates(0);
    TEST_END();
}

#endif /* _WIN32 */
