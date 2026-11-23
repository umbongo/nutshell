/* tests/test_voice_stt_win.c -- the STT upload worker of voice input.
 * Built only by `make wintest`. No real provider is contacted: the one test
 * that sends points at the local discard port, where the connect fails or
 * the cancel lands first. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "voice_stt_win.h"
#include "voice_gate.h"
#include "ai_gate.h"
#include "ai_http.h"

static LRESULT CALLBACK vs_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static HWND vs_window(void)
{
    static int registered;
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = vs_wndproc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "NsVoiceSttTest";
        RegisterClassA(&wc);
        registered = 1;
    }
    return CreateWindowExA(0, "NsVoiceSttTest", "", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
}

/* Wait up to ms for a WM_VOICE_STT; returns the payload or NULL. */
static VoiceSttResult *vs_wait(DWORD ms)
{
    ULONGLONG end = GetTickCount64() + ms;
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            if (msg.message == WM_VOICE_STT) return (VoiceSttResult *)msg.lParam;
        Sleep(10);
    }
    return NULL;
}

static unsigned char *vs_wav(size_t *n)
{
    *n = 44 + 3200;
    unsigned char *w = (unsigned char *)calloc(*n, 1);
    if (w) memcpy(w, "RIFF", 4);
    return w;
}

static void vs_settings(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->voice_stt_use_ai = 0;
    snprintf(s->voice_stt_provider, sizeof(s->voice_stt_provider), "custom");
    snprintf(s->voice_stt_url, sizeof(s->voice_stt_url), "http://127.0.0.1:9/v1");
    snprintf(s->stt_api_key, sizeof(s->stt_api_key), "sk-test-not-real");
}

int test_voice_stt_refused_when_gate_off(void)
{
    TEST_BEGIN();
    HWND w = vs_window();
    ASSERT_NOT_NULL(w);
    ai_gate_set_enabled(0);
    voice_gate_set_enabled(0);
    Settings s;
    vs_settings(&s);
    size_t n;
    unsigned char *wav = vs_wav(&n);
    ASSERT_NOT_NULL(wav);
    char err[200] = "";
    ASSERT_EQ(voice_stt_submit(w, &s, wav, n, 1u, err, sizeof(err)), 0);
    ASSERT_TRUE(err[0] != '\0');
    ASSERT_TRUE(vs_wait(500) == NULL);
    DestroyWindow(w);
    TEST_END();
}

int test_voice_stt_unresolvable_target(void)
{
    TEST_BEGIN();
    HWND w = vs_window();
    ASSERT_NOT_NULL(w);
    ai_gate_set_enabled(1);
    voice_gate_set_enabled(1);
    Settings s;
    vs_settings(&s);
    s.voice_stt_url[0] = '\0';
    size_t n;
    unsigned char *wav = vs_wav(&n);
    ASSERT_NOT_NULL(wav);
    char err[200] = "";
    ASSERT_EQ(voice_stt_submit(w, &s, wav, n, 2u, err, sizeof(err)), 0);
    ASSERT_TRUE(strstr(err, "Enter a base URL") != NULL);
    ASSERT_TRUE(vs_wait(300) == NULL);
    voice_gate_set_enabled(0);
    ai_gate_set_enabled(0);
    DestroyWindow(w);
    TEST_END();
}

int test_voice_stt_cancel_unreachable(void)
{
    TEST_BEGIN();
    HWND w = vs_window();
    ASSERT_NOT_NULL(w);
    ai_gate_set_enabled(1);
    voice_gate_set_enabled(1);
    Settings s;
    vs_settings(&s);
    size_t n;
    unsigned char *wav = vs_wav(&n);
    ASSERT_NOT_NULL(wav);
    char err[200] = "";
    ASSERT_EQ(voice_stt_submit(w, &s, wav, n, 77u, err, sizeof(err)), 1);
    ULONGLONG t0 = GetTickCount64();
    voice_stt_cancel_all();
    VoiceSttResult *r = vs_wait(3000);
    ASSERT_NOT_NULL(r);
    if (r) {
        ASSERT_EQ((int)r->token, 77);
        ASSERT_EQ(r->ok, 0);
        ASSERT_TRUE(r->err[0] != '\0');
        ASSERT_TRUE(r->text[0] == '\0');
        free(r);
    }
    ASSERT_TRUE(GetTickCount64() - t0 < 3500);
    voice_gate_set_enabled(0);
    ai_gate_set_enabled(0);
    DestroyWindow(w);
    TEST_END();
}

/* A loopback listener on an ephemeral port that accepts and never replies. */
static SOCKET vs_listen(int *port)
{
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    int len = (int)sizeof(a);
    if (ls == INVALID_SOCKET || bind(ls, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        listen(ls, 4) != 0 || getsockname(ls, (struct sockaddr *)&a, &len) != 0) {
        if (ls != INVALID_SOCKET) closesocket(ls);
        return INVALID_SOCKET;
    }
    *port = (int)ntohs(a.sin_port);
    return ls;
}

/* Accept within ms; INVALID_SOCKET when nobody connected. */
static SOCKET vs_accept(SOCKET ls, DWORD ms)
{
    fd_set fs;
    FD_ZERO(&fs);
    FD_SET(ls, &fs);
    struct timeval tv;
    tv.tv_sec = (long)(ms / 1000);
    tv.tv_usec = (long)((ms % 1000) * 1000);
    if (select(0, &fs, NULL, NULL, &tv) <= 0) return INVALID_SOCKET;
    return accept(ls, NULL, NULL);
}

/* Bytes readable on c within ms (reads and discards); 0 when none. */
static int vs_read_some(SOCKET c, DWORD ms)
{
    fd_set fs;
    FD_ZERO(&fs);
    FD_SET(c, &fs);
    struct timeval tv;
    tv.tv_sec = (long)(ms / 1000);
    tv.tv_usec = (long)((ms % 1000) * 1000);
    if (select(0, &fs, NULL, NULL, &tv) <= 0) return 0;
    char buf[2048];
    int n = recv(c, buf, (int)sizeof(buf), 0);
    return n > 0 ? n : 0;
}

static int vs_under_wine(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    return nt && GetProcAddress(nt, "wine_get_version") != NULL;
}

static void vs_point_at(Settings *s, int port)
{
    snprintf(s->voice_stt_url, sizeof(s->voice_stt_url), "http://127.0.0.1:%d/v1", port);
}

/* A server that accepts, takes the request and never answers: only the
 * cancel can end the call, long before the 60 s receive timeout. */
int test_voice_stt_cancel_closes_a_blocked_request(void)
{
    TEST_BEGIN();
    HWND w = vs_window();
    ASSERT_NOT_NULL(w);
    int port = 0;
    SOCKET ls = vs_listen(&port);
    ASSERT_TRUE(ls != INVALID_SOCKET);
    ai_gate_set_enabled(1);
    voice_gate_set_enabled(1);
    Settings s;
    vs_settings(&s);
    vs_point_at(&s, port);
    size_t n;
    unsigned char *wav = vs_wav(&n);
    ASSERT_NOT_NULL(wav);
    char err[200] = "";
    ASSERT_EQ(voice_stt_submit(w, &s, wav, n, 9u, err, sizeof(err)), 1);
    SOCKET c = vs_accept(ls, 5000);
    ASSERT_TRUE(c != INVALID_SOCKET);
    ASSERT_TRUE(vs_read_some(c, 5000) > 0);        /* the request is on the wire */
    ASSERT_TRUE(vs_wait(300) == NULL);             /* and nothing answers */
    ULONGLONG t0 = GetTickCount64();
    voice_stt_cancel_all();
    int wine = vs_under_wine();
    if (wine) {
        /* Wine does not abort a synchronous WinHTTP call on handle close, so
         * cancel_all gives up after its 3 s and detaches the worker. The
         * server then hangs up, which ends the call; the worker still posts
         * its one result and frees its own record (the detach path). */
        printf("  [NOTE] Wine: the close-aborts-the-call half runs on a Windows host only\n");
        closesocket(c);
        c = INVALID_SOCKET;
    } else {
        ASSERT_TRUE(GetTickCount64() - t0 < 3500);   /* joined, not timed out */
    }
    VoiceSttResult *r = vs_wait(5000);
    ASSERT_NOT_NULL(r);
    if (r) {
        ASSERT_EQ((int)r->token, 9);
        ASSERT_EQ(r->ok, 0);
        if (!wine) ASSERT_STR_EQ(r->err, "Cancelled");
        free(r);
    }
    if (c != INVALID_SOCKET) closesocket(c);
    closesocket(ls);
    voice_gate_set_enabled(0);
    ai_gate_set_enabled(0);
    DestroyWindow(w);
    TEST_END();
}

static void vs_gate_off_hook(void)
{
    voice_gate_set_enabled(0);
}

/* The gate goes off after the body is built and before the send: nothing
 * reaches the server and the result says why. */
int test_voice_stt_gate_off_before_send_sends_nothing(void)
{
    TEST_BEGIN();
    HWND w = vs_window();
    ASSERT_NOT_NULL(w);
    int port = 0;
    SOCKET ls = vs_listen(&port);
    ASSERT_TRUE(ls != INVALID_SOCKET);
    ai_gate_set_enabled(1);
    voice_gate_set_enabled(1);
    Settings s;
    vs_settings(&s);
    vs_point_at(&s, port);
    size_t n;
    unsigned char *wav = vs_wav(&n);
    ASSERT_NOT_NULL(wav);
    voice_stt_test_pre_send_hook = vs_gate_off_hook;
    char err[200] = "";
    ASSERT_EQ(voice_stt_submit(w, &s, wav, n, 11u, err, sizeof(err)), 1);
    VoiceSttResult *r = vs_wait(3000);
    voice_stt_test_pre_send_hook = NULL;
    ASSERT_NOT_NULL(r);
    if (r) {
        ASSERT_EQ(r->ok, 0);
        ASSERT_TRUE(strstr(r->err, "turned off") != NULL);
        free(r);
    }
    SOCKET c = vs_accept(ls, 500);
    ASSERT_TRUE(c == INVALID_SOCKET);              /* no connection at all */
    if (c != INVALID_SOCKET) closesocket(c);
    closesocket(ls);
    voice_stt_cancel_all();
    ai_gate_set_enabled(0);
    DestroyWindow(w);
    TEST_END();
}

/* cancel_all with nothing running returns at once, twice. */
int test_voice_stt_cancel_all_idle(void)
{
    TEST_BEGIN();
    ULONGLONG t0 = GetTickCount64();
    voice_stt_cancel_all();
    voice_stt_cancel_all();
    ASSERT_TRUE(GetTickCount64() - t0 < 500);
    TEST_END();
}

int test_ai_http_post_ct_rejects_bad_content_type(void)
{
    TEST_BEGIN();
    AiHttpResponse resp;
    const char *bad[] = { "text/plain\r\nX-Evil: 1", "a\nb", "" };
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(ai_http_post_ct("http://127.0.0.1:9/x", NULL, bad[i], "x", 1, &resp, NULL), -1);
        ASSERT_STR_EQ(resp.error, "Invalid content type");
    }
    char longct[260];
    memset(longct, 'a', sizeof(longct) - 1);
    longct[sizeof(longct) - 1] = '\0';
    ASSERT_EQ(ai_http_post_ct("http://127.0.0.1:9/x", NULL, longct, "x", 1, &resp, NULL), -1);
    ASSERT_STR_EQ(resp.error, "Invalid content type");
    TEST_END();
}

#endif /* _WIN32 */
