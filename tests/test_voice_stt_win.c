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
