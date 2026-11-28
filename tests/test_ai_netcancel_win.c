/* tests/test_ai_netcancel_win.c -- the Win32 halves of real cancellation:
 * the thread-handle list that bounds a join (src/ui/thread_join.c) and a
 * real blocked WinHTTP call woken by firing the token. Built only by
 * `make wintest`; the WinHTTP case talks to a loopback socket that accepts
 * and never answers. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "thread_join.h"
#include "ai_gate.h"
#include "ai_netcancel.h"
#include "ai_tool_web_fetch.h"
#include "net_guard.h"
#include "http_loopback_win.h"

/* ---- thread_join -------------------------------------------------------- */

static DWORD WINAPI sleep_thread(LPVOID p)
{
    Sleep((DWORD)(UINT_PTR)p);
    return 0;
}

static DWORD WINAPI wait_event_thread(LPVOID p)
{
    WaitForSingleObject((HANDLE)p, 10000);
    return 0;
}

int test_thread_join_all_finish(void)
{
    TEST_BEGIN();
    ThreadJoinList l;
    thread_join_init(&l);
    for (int i = 0; i < 3; i++) {
        HANDLE t = CreateThread(NULL, 0, sleep_thread, (LPVOID)(UINT_PTR)50, 0, NULL);
        ASSERT_EQ(thread_join_add(&l, t), 1);
    }
    ASSERT_EQ(l.n, 3);
    ASSERT_EQ(thread_join_wait(&l, 5000), 0);
    ASSERT_EQ(l.n, 0);
    TEST_END();
}

int test_thread_join_is_bounded(void)
{
    TEST_BEGIN();
    ThreadJoinList l;
    thread_join_init(&l);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE quick = CreateThread(NULL, 0, sleep_thread, (LPVOID)(UINT_PTR)20, 0, NULL);
    HANDLE slow = CreateThread(NULL, 0, wait_event_thread, ev, 0, NULL);
    thread_join_add(&l, quick);
    thread_join_add(&l, slow);

    DWORD t0 = GetTickCount();
    int left = thread_join_wait(&l, 300);
    DWORD took = GetTickCount() - t0;
    ASSERT_EQ(left, 1);               /* the slow one is still running */
    ASSERT_EQ(l.n, 1);                /* and the finished one is closed and gone */
    ASSERT_TRUE(took >= 250 && took < 3000);

    SetEvent(ev);                     /* let it go: now it joins at once */
    ASSERT_EQ(thread_join_wait(&l, 5000), 0);
    CloseHandle(ev);
    TEST_END();
}

int test_thread_join_full_list_closes_handle(void)
{
    TEST_BEGIN();
    ThreadJoinList l;
    thread_join_init(&l);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < THREAD_JOIN_MAX; i++) {
        HANDLE t = CreateThread(NULL, 0, wait_event_thread, ev, 0, NULL);
        ASSERT_EQ(thread_join_add(&l, t), 1);
    }
    HANDLE extra = CreateThread(NULL, 0, wait_event_thread, ev, 0, NULL);
    ASSERT_EQ(thread_join_add(&l, extra), 0);   /* no room: closed, not kept */
    ASSERT_EQ(l.n, THREAD_JOIN_MAX);
    SetEvent(ev);
    ASSERT_EQ(thread_join_wait(&l, 10000), 0);
    CloseHandle(ev);
    TEST_END();
}

/* ---- a real blocked WinHTTP call ---------------------------------------- */

static void wh_close_cb(uintptr_t h)
{
    WinHttpCloseHandle((HINTERNET)h);
}

typedef struct {
    AiNetCancel nc;
    int         port;
    volatile LONG started;     /* the request is about to block */
    BOOL        send_ok, recv_ok;
    int         owns_handle;   /* what disarm said afterwards */
    DWORD       took_ms;
} WhArg;

static DWORD WINAPI blocked_request(LPVOID p)
{
    WhArg *a = (WhArg *)p;
    HINTERNET s = WinHttpOpen(L"nstest", WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, L"127.0.0.1", (INTERNET_PORT)a->port, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/", NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : NULL;
    if (!r) {
        if (c) WinHttpCloseHandle(c);
        if (s) WinHttpCloseHandle(s);
        return 1;
    }
    if (ai_nc_arm(&a->nc, (uintptr_t)r, wh_close_cb) != 0) {
        WinHttpCloseHandle(r);
    } else {
        DWORD t0 = GetTickCount();
        InterlockedExchange(&a->started, 1);
        a->send_ok = WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        a->recv_ok = a->send_ok && WinHttpReceiveResponse(r, NULL);   /* blocks */
        a->took_ms = GetTickCount() - t0;
        /* Failed call: ask the token before touching r again. */
        a->owns_handle = ai_nc_disarm(&a->nc);
        if (a->owns_handle) WinHttpCloseHandle(r);
    }
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return 0;
}

/* Wine's WinHttpSendRequest on a synchronous handle blocks until the
 * response headers arrive and does not abort when the handle is closed from
 * another thread (measured: a closed request stayed blocked for the 5 s the
 * test waited, and returned only when the server finally answered). Windows
 * aborts the call with ERROR_WINHTTP_OPERATION_CANCELLED, which is the
 * behaviour the app relies on; this case therefore runs for real only on a
 * Windows host (`mingw32-make wintest`) and is skipped under Wine. */
static int running_under_wine(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    return nt && GetProcAddress(nt, "wine_get_version") != NULL;
}

int test_ai_nc_fire_wakes_blocked_winhttp_call(void)
{
    TEST_BEGIN();
    if (running_under_wine()) {
        TEST_SKIP("Wine does not abort a synchronous WinHTTP call on handle close;"
                  " runs on a Windows host only");
    }
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_TRUE(bind(ls, (struct sockaddr *)&sa, sizeof(sa)) == 0);
    ASSERT_TRUE(listen(ls, 4) == 0);
    int salen = (int)sizeof(sa);
    getsockname(ls, (struct sockaddr *)&sa, &salen);

    static WhArg a;           /* the thread outlives nothing: joined below */
    memset(&a, 0, sizeof(a));
    ai_nc_init(&a.nc);
    a.port = ntohs(sa.sin_port);
    HANDLE t = CreateThread(NULL, 0, blocked_request, &a, 0, NULL);
    ASSERT_NOT_NULL(t);

    for (int i = 0; i < 200 && !a.started; i++) Sleep(10);
    ASSERT_EQ((int)a.started, 1);
    Sleep(400);                          /* let it block in ReceiveResponse */
    DWORD f0 = GetTickCount();
    ai_nc_fire(&a.nc);                   /* the UI thread's cancel */
    DWORD w = WaitForSingleObject(t, 5000);
    DWORD joined = GetTickCount() - f0;
    ASSERT_EQ((int)w, (int)WAIT_OBJECT_0);        /* far inside the 30-90 s timeouts */
    ASSERT_TRUE(joined < 3000);
    ASSERT_FALSE(a.recv_ok);
    ASSERT_EQ(a.owns_handle, 0);                  /* fire closed it, the worker did not */
    if (w != WAIT_OBJECT_0) TerminateThread(t, 1);
    CloseHandle(t);
    closesocket(ls);
    TEST_END();
}

/* ---- the web_fetch hop watchdog ------------------------------------------ */

/* One raw hop (the default backend's get, which does no address checks) at a
 * loopback server that trickles its response head, one byte per 100 ms: each
 * byte restarts WinHTTP's receive timeout, so only the watchdog ends the
 * wait. Not run by the author (no Wine on the authoring session). */
typedef struct {
    int  port;
    int  timeout_ms;
    volatile int *cancel;
    int  rc;
    int  status;
    DWORD ms;
    volatile int done;
} TrickleArg;

static DWORD WINAPI trickle_thread(LPVOID p)
{
    TrickleArg *a = (TrickleArg *)p;
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/", a->port);
    NetUrlParts parts;
    char location[256];
    char *body = NULL;
    size_t body_len = 0;
    a->rc = -99;
    if (net_url_parse(url, &parts) == 0) {
        const WebFetchBackend *be = web_fetch_default_backend();
        DWORD t0 = GetTickCount();
        a->rc = be->get(be->ctx, &parts, a->timeout_ms, a->cancel, &a->status,
                        location, sizeof(location), &body, &body_len);
        a->ms = GetTickCount() - t0;
    }
    free(body);
    a->done = 1;
    return 0;
}

int test_web_fetch_trickling_head_ends_at_hop_deadline(void)
{
    TEST_BEGIN();
    /* ns_winhttp_open refuses while the AI gate is off (an earlier test leaves
     * it off), which would fail the hop at once without reaching the watchdog:
     * the lower bound below catches that. */
    ai_gate_set_enabled(1);
    HttpLoop *h = hl_start(HLS_TRICKLE_HEAD, NULL);
    ASSERT_NOT_NULL(h);
    if (h) {
        static TrickleArg a;
        memset(&a, 0, sizeof(a));
        a.port = hl_port(h);
        a.timeout_ms = 700;
        HANDLE t = CreateThread(NULL, 0, trickle_thread, &a, 0, NULL);
        ASSERT_NOT_NULL(t);
        DWORD w = t ? WaitForSingleObject(t, 8000) : WAIT_FAILED;
        ASSERT_EQ((int)w, (int)WAIT_OBJECT_0);        /* far inside the 60 s trickle */
        ASSERT_EQ(a.rc, -1);
        ASSERT_EQ(a.status, 0);
        ASSERT_TRUE(a.ms >= 600);                     /* it waited for the hop's 700 ms */
        ASSERT_TRUE(a.ms < 4000);
        hl_stop(h);
        if (t) {
            if (w != WAIT_OBJECT_0) TerminateThread(t, 1);
            CloseHandle(t);
        }
    }
    ai_gate_set_enabled(0);
    TEST_END();
}

int test_web_fetch_trickling_head_ends_on_cancel_flag(void)
{
    TEST_BEGIN();
    ai_gate_set_enabled(1);                            /* see the test above */
    HttpLoop *h = hl_start(HLS_TRICKLE_HEAD, NULL);
    ASSERT_NOT_NULL(h);
    if (h) {
        static TrickleArg a;
        static volatile int cancel;
        memset(&a, 0, sizeof(a));
        cancel = 0;
        a.port = hl_port(h);
        a.timeout_ms = 30000;                          /* the hop's own limit is far off */
        a.cancel = &cancel;
        HANDLE t = CreateThread(NULL, 0, trickle_thread, &a, 0, NULL);
        ASSERT_NOT_NULL(t);
        Sleep(500);
        cancel = 1;
        DWORD w = t ? WaitForSingleObject(t, 5000) : WAIT_FAILED;
        ASSERT_EQ((int)w, (int)WAIT_OBJECT_0);
        ASSERT_EQ(a.rc, -1);
        ASSERT_TRUE(a.ms >= 400);                     /* it ran until the cancel at 500 ms */
        ASSERT_TRUE(a.ms < 4000);
        hl_stop(h);
        if (t) {
            if (w != WAIT_OBJECT_0) TerminateThread(t, 1);
            CloseHandle(t);
        }
    }
    ai_gate_set_enabled(0);
    TEST_END();
}

#endif /* _WIN32 */
