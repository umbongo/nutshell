/* voice_ws_win.c -- WinHTTP WebSocket client for live dictation. See the header. */

#include "voice_ws_win.h"

#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dlog.h"
#include "secure_zero.h"
#include "voice_net_win.h"
#include "winhttp_open.h"

#ifndef WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET
#define WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET 114
#endif
#ifndef WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT
#define WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT 115
#endif

/* The documented values; MinGW's winhttp.h may lack them. */
#ifndef WINHTTP_OPTION_REDIRECT_POLICY
#define WINHTTP_OPTION_REDIRECT_POLICY 88
#endif
#ifndef WINHTTP_OPTION_REDIRECT_POLICY_NEVER
#define WINHTTP_OPTION_REDIRECT_POLICY_NEVER 0
#endif

#define VWS_MAX_MSG   (1024u * 1024u)
#define VWS_CLOSE_MS  2000u

struct VoiceWs {
    HINTERNET     session;
    HINTERNET     connect;
    HINTERNET     ws;          /* the WebSocket handle; armed on *nc */
    AiNetCancel  *nc;          /* the caller's token, or &own */
    AiNetCancel   own;
    volatile LONG shut;        /* shutdown or abort was requested: recv fails */
    volatile LONG closed;      /* WinHttpWebSocketClose succeeded */
};

static void ws_close_cb(uintptr_t h)
{
    WinHttpCloseHandle((HINTERNET)h);
}

static void set_err(char *err, size_t cap, const char *what, DWORD code)
{
    if (err && cap) snprintf(err, cap, "%s: %lu", what, (unsigned long)code);
}

/* "Name: value" lines to one wide block, each ended by CRLF. Heap; the caller
 * wipes and frees it (it holds the bearer key). */
static wchar_t *headers_wide(const char *const *headers, size_t *wchars)
{
    size_t total = 1;
    for (int i = 0; headers && headers[i]; i++) total += strlen(headers[i]) + 2;
    wchar_t *w = (wchar_t *)calloc(total, sizeof(wchar_t));
    if (!w) return NULL;
    size_t pos = 0;
    for (int i = 0; headers && headers[i]; i++) {
        int n = MultiByteToWideChar(CP_UTF8, 0, headers[i], -1, w + pos, (int)(total - pos - 2));
        if (n <= 0) continue;
        pos += (size_t)(n - 1);
        w[pos++] = L'\r';
        w[pos++] = L'\n';
    }
    w[pos] = L'\0';
    *wchars = pos;
    return w;
}

static void wide_free(wchar_t *w, size_t wchars)
{
    if (!w) return;
    secure_zero(w, (wchars + 1) * sizeof(wchar_t));
    free(w);
}

static void ws_destroy(VoiceWs *ws)
{
    if (ws->ws && ai_nc_disarm(ws->nc)) WinHttpCloseHandle(ws->ws);
    ws->ws = NULL;
    if (ws->connect) WinHttpCloseHandle(ws->connect);
    if (ws->session) WinHttpCloseHandle(ws->session);
    free(ws);
}

VoiceWs *voice_ws_connect(const char *host, int port, int secure, const char *path,
                          const char *const *headers, AiNetCancel *nc,
                          int *http_status, char *err, size_t errcap)
{
    if (http_status) *http_status = 0;
    if (err && errcap) err[0] = '\0';
    if (!host || !*host || !path || port <= 0 || port > 65535) {
        set_err(err, errcap, "bad websocket target", 0);
        return NULL;
    }

    VoiceWs *ws = (VoiceWs *)calloc(1, sizeof(*ws));
    if (!ws) {
        set_err(err, errcap, "out of memory", 0);
        return NULL;
    }
    ai_nc_init(&ws->own);
    ws->nc = nc ? nc : &ws->own;

    ws->session = voice_net_open(VNK_LIVE_WS);
    if (!ws->session) {
        DWORD le = GetLastError();
        if (le == NS_ERR_TLS_UNAVAILABLE)
            ns_winhttp_failure_text((unsigned long)le, err, errcap);   /* plain words */
        else
            set_err(err, errcap, "websocket session unavailable", le);
        ws_destroy(ws);
        return NULL;
    }
    WinHttpSetTimeouts(ws->session, 10000, 10000, 10000, 15000);

    wchar_t whost[256], wpath[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024) <= 0) {
        set_err(err, errcap, "websocket target too long", 0);
        ws_destroy(ws);
        return NULL;
    }
    ws->connect = WinHttpConnect(ws->session, whost, (INTERNET_PORT)port, 0);
    if (!ws->connect) {
        set_err(err, errcap, "WinHttpConnect failed", GetLastError());
        ws_destroy(ws);
        return NULL;
    }

    HINTERNET req = WinHttpOpenRequest(ws->connect, L"GET", wpath, NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        set_err(err, errcap, "WinHttpOpenRequest failed", GetLastError());
        ws_destroy(ws);
        return NULL;
    }
    if (ai_nc_arm(ws->nc, (uintptr_t)req, ws_close_cb) != 0) {
        WinHttpCloseHandle(req);
        set_err(err, errcap, "cancelled", 0);
        ws_destroy(ws);
        return NULL;
    }

    HINTERNET wsh = NULL;
    DWORD code = 0;
    const char *what = NULL;
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
        what = "WebSocket upgrade option refused";
        code = GetLastError();
    } else if (!WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) {
        /* Never follow a redirect: the bearer header must not be re-sent. */
        what = "WinHTTP redirect policy refused";
        code = GetLastError();
    } else {
        size_t wn = 0;
        wchar_t *wh = headers_wide(headers, &wn);
        BOOL sent = wh && WinHttpSendRequest(req, wh, (DWORD)wn, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        DWORD serr = sent ? 0 : GetLastError();
        wide_free(wh, wn);                         /* the bearer key is wiped */
        if (!sent) {
            what = wh ? "WinHttpSendRequest failed" : "websocket headers unavailable";
            code = wh ? serr : 0;
        } else if (!WinHttpReceiveResponse(req, NULL)) {
            what = "WinHttpReceiveResponse failed";
            code = GetLastError();
        } else {
            DWORD status = 0, sz = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                                WINHTTP_NO_HEADER_INDEX);
            if (http_status) *http_status = (int)status;
            if (status != 101) {
                what = "websocket upgrade refused, HTTP status";
                code = status;
            } else {
                wsh = WinHttpWebSocketCompleteUpgrade(req, 0);
                if (!wsh) {
                    what = "WinHttpWebSocketCompleteUpgrade failed";
                    code = GetLastError();
                }
            }
        }
    }

    /* The request handle is finished with either way; it is ours to close
     * unless a fire already took it. */
    int cancelled = ai_nc_disarm(ws->nc) == 0;
    if (!cancelled) WinHttpCloseHandle(req);

    if (!wsh) {
        set_err(err, errcap, cancelled ? "cancelled" : what, cancelled ? 0 : code);
        ws_destroy(ws);
        return NULL;
    }
    /* Arm the WebSocket handle on the same token; refused when it fired. */
    if (cancelled || ai_nc_arm(ws->nc, (uintptr_t)wsh, ws_close_cb) != 0) {
        WinHttpCloseHandle(wsh);
        set_err(err, errcap, "cancelled", 0);
        ws_destroy(ws);
        return NULL;
    }
    ws->ws = wsh;

    DWORD zero = 0, close_ms = VWS_CLOSE_MS;
    (void)WinHttpSetOption(wsh, WINHTTP_OPTION_RECEIVE_TIMEOUT, &zero, sizeof(zero));
    (void)WinHttpSetOption(wsh, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &close_ms,
                           sizeof(close_ms));
    DLOG(DLOG_AI, DLOG_INFO, 0, "voice.ws_connected", "port=%d", port);
    return ws;
}

int voice_ws_send_text(VoiceWs *ws, const char *s, size_t n)
{
    if (!ws || !ws->ws || (!s && n) || n > VWS_MAX_MSG || ws->shut ||
        ai_nc_cancelled(ws->nc))
        return -1;
    DWORD r = WinHttpWebSocketSend(ws->ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                   (void *)s, (DWORD)n);
    return r == ERROR_SUCCESS ? 0 : -1;
}

/* Grow `buf` to `cap`: the new block gets the old content, the old one is
 * wiped (it may hold a transcript) before it is freed. */
static char *buf_grow(char *buf, size_t used, size_t cap)
{
    char *nb = (char *)malloc(cap);
    if (!nb) return NULL;
    if (buf) {
        memcpy(nb, buf, used);
        secure_zero(buf, used);
        free(buf);
    }
    return nb;
}

int voice_ws_recv(VoiceWs *ws, char **msg, size_t *len, int *is_close)
{
    if (msg) *msg = NULL;
    if (len) *len = 0;
    if (is_close) *is_close = 0;
    if (!ws || !ws->ws || !msg || !len) return -1;

    size_t cap = 4096, used = 0;
    char *buf = buf_grow(NULL, 0, cap);
    if (!buf) return -1;

    for (;;) {
        if (ws->shut || ai_nc_cancelled(ws->nc)) break;
        if (used + 1 >= cap) {                      /* room for data and the NUL */
            if (cap > VWS_MAX_MSG) break;
            size_t ncap = cap * 2;
            char *nb = buf_grow(buf, used, ncap);
            if (!nb) break;
            buf = nb;
            cap = ncap;
        }
        DWORD got = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        DWORD r = WinHttpWebSocketReceive(ws->ws, buf + used, (DWORD)(cap - used - 1), &got,
                                          &type);
        if (r != ERROR_SUCCESS) break;
        if (ws->shut) break;                        /* a cancelled wait is never a message */
        used += got;
        if (used > VWS_MAX_MSG) break;
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            if (is_close) *is_close = 1;
            buf[0] = '\0';
            *msg = buf;
            *len = 0;
            return 0;
        }
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            buf[used] = '\0';
            *msg = buf;
            *len = used;
            return 0;
        }
        /* a fragment: go round again, growing when the buffer is full */
    }
    secure_zero(buf, used);
    free(buf);
    return -1;
}

DWORD voice_ws_shutdown(VoiceWs *ws)
{
    if (!ws || !ws->ws) return ERROR_INVALID_HANDLE;
    InterlockedExchange(&ws->shut, 1);                  /* a later receive fails */
    if (ws->closed) return NO_ERROR;
    if (ai_nc_cancelled(ws->nc)) return NO_ERROR;       /* the handle is already closed */
    DWORD r = WinHttpWebSocketClose(ws->ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    if (r == NO_ERROR) InterlockedExchange(&ws->closed, 1);   /* latched on success only */
    return r;
}

void voice_ws_abort(VoiceWs *ws)
{
    if (!ws) return;
    InterlockedExchange(&ws->shut, 1);
    ai_nc_fire(ws->nc);
}

void voice_ws_free(VoiceWs *ws)
{
    if (!ws) return;
    ws_destroy(ws);
}
