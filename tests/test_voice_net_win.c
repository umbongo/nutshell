/* tests/test_voice_net_win.c -- the one voice network opener. Built only by
 * `make wintest`. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <string.h>
#include "test_framework.h"
#include "voice_net_win.h"
#include "voice_gate.h"
#include "ai_gate.h"
#include "ai_http.h"
#include "ai_netcancel.h"
#include "http_loopback_win.h"
#include <stdio.h>
#include <stdlib.h>

int test_voice_net_counts(void)
{
    TEST_BEGIN();
    voice_net_test_reset_count();
    /* gate off: no session, no count */
    ai_gate_set_enabled(0);
    voice_gate_set_enabled(0);
    SetLastError(0);
    HINTERNET h = voice_net_open(VNK_STT_UPLOAD);
    ASSERT_TRUE(h == NULL);
    ASSERT_EQ((int)GetLastError(), (int)ERROR_ACCESS_DENIED);
    ASSERT_TRUE(voice_winhttp_open() == NULL);
    ASSERT_EQ((int)voice_net_open_count(), 0);
    /* voice on but the assistant off: still closed (voice needs the AI gate) */
    voice_gate_set_enabled(1);
    ASSERT_TRUE(voice_net_open(VNK_LIVE_WS) == NULL);
    ASSERT_EQ((int)voice_net_open_count(), 0);
    /* both on: two opens, two counts */
    ai_gate_set_enabled(1);
    HINTERNET a = voice_net_open(VNK_STT_UPLOAD);
    HINTERNET b = voice_net_open(VNK_LIVE_WS);
    ASSERT_TRUE(a != NULL);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ((int)voice_net_open_count(), 2);
    if (a) WinHttpCloseHandle(a);
    if (b) WinHttpCloseHandle(b);
    /* the chat path never counts as a voice open */
    AiHttpResponse resp;
    (void)ai_http_post("http://127.0.0.1:1/", NULL, "{}", 2, &resp, NULL);
    ai_http_response_free(&resp);
    ASSERT_EQ((int)voice_net_open_count(), 2);
    voice_net_test_reset_count();
    ASSERT_EQ((int)voice_net_open_count(), 0);
    voice_gate_set_enabled(0);
    ai_gate_set_enabled(0);
    TEST_END();
}

/* ---- ai_http_post_stream_ex: the streaming POST on a voice session ---- */

typedef struct { unsigned char *p; size_t n, cap; int calls; } Sink;

static int sink_cb(const char *d, size_t n, void *u)
{
    Sink *s = (Sink *)u;
    if (s->n + n > s->cap) {
        s->cap = (s->n + n) * 2;
        s->p = (unsigned char *)realloc(s->p, s->cap);
    }
    memcpy(s->p + s->n, d, n);
    s->n += n;
    s->calls++;
    return 0;
}

static void sink_free(Sink *s) { free(s->p); memset(s, 0, sizeof(*s)); }

static int running_under_wine_tts(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    return nt && GetProcAddress(nt, "wine_get_version") != NULL;
}

static void tts_gates(int on)
{
    ai_gate_set_enabled(on);
    voice_gate_set_enabled(on);
}

/* One request through voice_net_open(VNK_TTS) against `port`. Returns the call's rc. */
static int tts_post(int port, Sink *sink, int *status, char *err, size_t errn, AiNetCancel *nc)
{
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/v1/audio/speech", port);
    static const char *hdrs[] = { "Authorization: Bearer test-key-0000", NULL };
    static const char body[] = "{\"input\":\"hi\"}";
    HINTERNET s = voice_net_open(VNK_TTS);
    if (!s) { snprintf(err, errn, "no session"); return -2; }
    int rc = ai_http_post_stream_ex(s, url, hdrs, body, sizeof(body) - 1, 5000, sink_cb, sink,
                                    status, err, errn, nc);
    WinHttpCloseHandle(s);
    return rc;
}

int test_ai_stream_ex_wav_bytes(void)
{
    TEST_BEGIN();
    tts_gates(1);
    voice_net_test_reset_count();
    HlAudio a = { 1, 0, 0.5, NULL };
    HttpLoop *h = hl_start(HLS_WAV, &a);
    ASSERT_NOT_NULL(h);
    if (h) {
        Sink sk;
        memset(&sk, 0, sizeof(sk));
        int status = 0;
        char err[256];
        int rc = tts_post(hl_port(h), &sk, &status, err, sizeof(err), NULL);
        ASSERT_EQ(rc, 0);
        ASSERT_EQ(status, 200);
        size_t wn = 0;
        unsigned char *w = hl_wav_build(&a, 24000, &wn);
        ASSERT_NOT_NULL(w);
        ASSERT_EQ((int)sk.n, (int)wn);
        ASSERT_TRUE(w && sk.n == wn && memcmp(sk.p, w, wn) == 0);
        ASSERT_TRUE(sk.calls > 1);                      /* it arrived across callbacks */
        ASSERT_TRUE(sk.n > 64 && memcmp(sk.p + 36, "LIST", 4) == 0);
        ASSERT_EQ((int)voice_net_open_count(), 1);
        ASSERT_EQ(hl_requests(h), 1);
        ASSERT_TRUE(strstr(hl_request_head(h), "Authorization: Bearer test-key-0000") != NULL);
        ASSERT_TRUE(strstr(hl_request_head(h), "POST /v1/audio/speech") != NULL);
        size_t bn = 0;
        (void)hl_request_body(h, &bn);
        ASSERT_EQ((int)bn, 14);
        free(w);
        sink_free(&sk);
        hl_stop(h);
    }
    tts_gates(0);
    TEST_END();
}

int test_ai_stream_ex_sse(void)
{
    TEST_BEGIN();
    tts_gates(1);
    voice_net_test_reset_count();
    HttpLoop *h = hl_start(HLS_SSE, NULL);
    ASSERT_NOT_NULL(h);
    if (h) {
        Sink sk;
        memset(&sk, 0, sizeof(sk));
        int status = 0;
        char err[256];
        int rc = tts_post(hl_port(h), &sk, &status, err, sizeof(err), NULL);
        ASSERT_EQ(rc, 0);
        ASSERT_EQ(status, 200);
        FILE *f = fopen("tests/fixtures/voice_tts/provisional_mistral_wav.sse", "rb");
        ASSERT_NOT_NULL(f);
        if (f) {
            fseek(f, 0, SEEK_END);
            long fn = ftell(f);
            fseek(f, 0, SEEK_SET);
            unsigned char *fb = (unsigned char *)malloc((size_t)fn);
            ASSERT_TRUE(fb && fread(fb, 1, (size_t)fn, f) == (size_t)fn);
            ASSERT_EQ((int)sk.n, (int)fn);
            ASSERT_TRUE(fb && sk.n == (size_t)fn && memcmp(sk.p, fb, sk.n) == 0);
            free(fb);
            fclose(f);
        }
        ASSERT_EQ((int)voice_net_open_count(), 1);
        sink_free(&sk);
        hl_stop(h);
    }
    tts_gates(0);
    TEST_END();
}

int test_ai_stream_ex_statuses(void)
{
    TEST_BEGIN();
    tts_gates(1);
    static const struct { int mode, status; const char *needle; } c[4] = {
        { HLS_401, 401, "invalid key" }, { HLS_404, 404, "no such model" },
        { HLS_422, 422, "unknown field" }, { HLS_429, 429, "slow down" } };
    for (int i = 0; i < 4; i++) {
        voice_net_test_reset_count();
        HttpLoop *h = hl_start(c[i].mode, NULL);
        ASSERT_NOT_NULL(h);
        if (!h) continue;
        Sink sk;
        memset(&sk, 0, sizeof(sk));
        int status = 0;
        char err[512];
        int rc = tts_post(hl_port(h), &sk, &status, err, sizeof(err), NULL);
        ASSERT_EQ(rc, -1);
        ASSERT_EQ(status, c[i].status);
        ASSERT_TRUE(strstr(err, c[i].needle) != NULL);
        ASSERT_EQ((int)sk.n, 0);                         /* the body is the error, not audio */
        ASSERT_EQ((int)voice_net_open_count(), 1);
        sink_free(&sk);
        hl_stop(h);
    }
    tts_gates(0);
    TEST_END();
}

int test_ai_stream_ex_no_redirect(void)
{
    TEST_BEGIN();
    tts_gates(1);
    voice_net_test_reset_count();
    HttpLoop *h = hl_start(HLS_302, NULL);
    ASSERT_NOT_NULL(h);
    if (h) {
        Sink sk;
        memset(&sk, 0, sizeof(sk));
        int status = 0;
        char err[256];
        int rc = tts_post(hl_port(h), &sk, &status, err, sizeof(err), NULL);
        ASSERT_EQ(rc, -1);
        ASSERT_EQ(status, 302);
        Sleep(100);                                      /* a follow-up request would be in by now */
        ASSERT_EQ(hl_requests(h), 1);
        ASSERT_EQ((int)voice_net_open_count(), 1);
        sink_free(&sk);
        hl_stop(h);
    }
    tts_gates(0);
    TEST_END();
}

typedef struct {
    int port, rc, status;
    char err[256];
    Sink sk;
    AiNetCancel nc;
    volatile LONG started, done;
} HangArg;

static DWORD WINAPI hang_thread(LPVOID p)
{
    HangArg *a = (HangArg *)p;
    InterlockedExchange(&a->started, 1);
    a->rc = tts_post(a->port, &a->sk, &a->status, a->err, sizeof(a->err), &a->nc);
    InterlockedExchange(&a->done, 1);
    return 0;
}

int test_ai_stream_ex_cancel_hang(void)
{
    TEST_BEGIN();
    tts_gates(1);
    HttpLoop *h = hl_start(HLS_HANG, NULL);
    ASSERT_NOT_NULL(h);
    static HangArg a;
    memset(&a, 0, sizeof(a));
    ai_nc_init(&a.nc);
    int skipped = 0;
    if (h) {
        a.port = hl_port(h);
        HANDLE t = CreateThread(NULL, 0, hang_thread, &a, 0, NULL);
        ASSERT_NOT_NULL(t);
        for (int i = 0; i < 200 && !a.started; i++) Sleep(10);
        for (int i = 0; i < 300 && hl_requests(h) == 0 && !a.done; i++) Sleep(10);  /* request in: it blocks */
        Sleep(200);
        DWORD f0 = GetTickCount();
        ai_nc_fire(&a.nc);
        while (!a.done && GetTickCount() - f0 < 2000) Sleep(10);
        if (!a.done && running_under_wine_tts()) {
            skipped = 1;
        } else {
            ASSERT_EQ((int)a.done, 1);
            ASSERT_EQ(a.rc, -1);
            ASSERT_STR_EQ(a.err, "cancelled");
        }
        hl_stop(h);                  /* closes the server side, which frees a Wine-blocked call */
        if (t) {
            DWORD w = WaitForSingleObject(t, 10000);
            ASSERT_EQ((int)w, (int)WAIT_OBJECT_0);
            if (w != WAIT_OBJECT_0) TerminateThread(t, 1);
            CloseHandle(t);
        }
    }
    sink_free(&a.sk);
    tts_gates(0);
    if (skipped) TEST_SKIP("Wine does not abort a synchronous WinHTTP call");
    TEST_END();
}

int test_ai_stream_ex_null_session(void)
{
    TEST_BEGIN();
    Sink sk;
    memset(&sk, 0, sizeof(sk));
    int status = 99;
    char err[128];
    int rc = ai_http_post_stream_ex(NULL, "http://127.0.0.1:1/", NULL, "{}", 2, 1000,
                                    sink_cb, &sk, &status, err, sizeof(err), NULL);
    ASSERT_EQ(rc, -1);
    ASSERT_STR_EQ(err, "No session");
    ASSERT_EQ((int)sk.n, 0);
    sink_free(&sk);
    TEST_END();
}

#endif /* _WIN32 */
