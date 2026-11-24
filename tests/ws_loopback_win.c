/* tests/ws_loopback_win.c -- see ws_loopback_win.h. Built only by `make wintest`. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ws_loopback_win.h"
#include "base64.h"

#define WSL_MAX_FRAME (4u * 1024u * 1024u)

struct WsLoop {
    int              mode;
    SOCKET           ls;
    volatile SOCKET  cs;         /* the accepted connection, or INVALID_SOCKET */
    int              port;
    HANDLE           thread;
    volatile LONG    stop;
    volatile LONG    upgraded;
    volatile LONG    close_seen;
    CRITICAL_SECTION send_lock;
    char             request[4096];
    int (*on_msg)(WsLoop *, const char *, size_t, void *);
    void            *user;
};

static int send_all(SOCKET s, const void *p, size_t n)
{
    const char *c = (const char *)p;
    while (n) {
        int k = send(s, c, (int)(n > 65536 ? 65536 : n), 0);
        if (k <= 0) return -1;
        c += k;
        n -= (size_t)k;
    }
    return 0;
}

/* One unmasked server frame. */
static int send_frame(WsLoop *w, int opcode, int fin, const void *p, size_t n)
{
    unsigned char hdr[10];
    size_t hl = 0;
    hdr[hl++] = (unsigned char)((fin ? 0x80 : 0) | (opcode & 0x0F));
    if (n < 126) {
        hdr[hl++] = (unsigned char)n;
    } else if (n <= 0xFFFF) {
        hdr[hl++] = 126;
        hdr[hl++] = (unsigned char)(n >> 8);
        hdr[hl++] = (unsigned char)n;
    } else {
        hdr[hl++] = 127;
        for (int i = 7; i >= 0; i--) hdr[hl++] = (unsigned char)((unsigned long long)n >> (8 * i));
    }
    EnterCriticalSection(&w->send_lock);
    SOCKET s = w->cs;
    int rc = s == INVALID_SOCKET ? -1 : send_all(s, hdr, hl);
    if (rc == 0 && n) rc = send_all(s, p, n);
    LeaveCriticalSection(&w->send_lock);
    return rc;
}

/* Wait for the socket to be readable; 1 ready, 0 timeout/stop, -1 error. */
static int wait_readable(WsLoop *w, SOCKET s)
{
    while (!w->stop) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(s, &rs);
        struct timeval tv = { 0, 50000 };
        int r = select(0, &rs, NULL, NULL, &tv);
        if (r < 0) return -1;
        if (r > 0) return 1;
    }
    return 0;
}

static int recv_exact(WsLoop *w, SOCKET s, void *p, size_t n)
{
    char *c = (char *)p;
    while (n) {
        if (wait_readable(w, s) != 1) return -1;
        int k = recv(s, c, (int)n, 0);
        if (k <= 0) return -1;
        c += k;
        n -= (size_t)k;
    }
    return 0;
}

static int read_head(WsLoop *w, SOCKET s)
{
    size_t n = 0;
    while (n + 1 < sizeof(w->request)) {
        if (wait_readable(w, s) != 1) return -1;
        int k = recv(s, w->request + n, 1, 0);
        if (k <= 0) return -1;
        n += (size_t)k;
        w->request[n] = '\0';
        if (n >= 4 && memcmp(w->request + n - 4, "\r\n\r\n", 4) == 0) return 0;
    }
    return -1;
}

static int do_upgrade(WsLoop *w, SOCKET s)
{
    const char *p = strstr(w->request, "Sec-WebSocket-Key:");
    if (!p) p = strstr(w->request, "sec-websocket-key:");
    if (!p) return -1;
    p += 18;
    while (*p == ' ') p++;
    char key[128];
    size_t kn = 0;
    while (*p && *p != '\r' && kn + 1 < sizeof(key)) key[kn++] = *p++;
    key[kn] = '\0';
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char cat[256];
    snprintf(cat, sizeof(cat), "%s%s", key, guid);
    unsigned char dg[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char *)cat, strlen(cat), dg);
    char acc[64];
    if (base64_encode(dg, sizeof(dg), acc, sizeof(acc)) == 0) return -1;
    char resp[256];
    int rn = snprintf(resp, sizeof(resp),
                      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                      "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
    if (send_all(s, resp, (size_t)rn) != 0) return -1;
    InterlockedExchange(&w->upgraded, 1);
    return 0;
}

static void serve(WsLoop *w, SOCKET s)
{
    if (read_head(w, s) != 0) return;
    if (w->mode == WSL_NO_UPGRADE) {
        while (!w->stop) Sleep(20);
        return;
    }
    if (w->mode == WSL_REJECT_403) {
        static const char r[] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n"
                                "Connection: close\r\n\r\n";
        send_all(s, r, sizeof(r) - 1);
        Sleep(100);
        return;
    }
    if (do_upgrade(w, s) != 0) return;

    unsigned char *msg = NULL;
    size_t mlen = 0, mcap = 0;
    for (;;) {
        unsigned char h[2];
        if (recv_exact(w, s, h, 2) != 0) break;
        int fin = (h[0] & 0x80) != 0, op = h[0] & 0x0F, masked = (h[1] & 0x80) != 0;
        unsigned long long len = h[1] & 0x7F;
        if (len == 126) {
            unsigned char e[2];
            if (recv_exact(w, s, e, 2) != 0) break;
            len = ((unsigned long long)e[0] << 8) | e[1];
        } else if (len == 127) {
            unsigned char e[8];
            if (recv_exact(w, s, e, 8) != 0) break;
            len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
        }
        if (len > WSL_MAX_FRAME) break;
        unsigned char mask[4] = { 0, 0, 0, 0 };
        if (masked && recv_exact(w, s, mask, 4) != 0) break;
        if (op >= 8) {                      /* control frame */
            unsigned char cb[125];
            if (len > sizeof(cb) || recv_exact(w, s, cb, (size_t)len) != 0) break;
            if (op == 8) {
                send_frame(w, 8, 1, cb, (size_t)len);
                InterlockedExchange(&w->close_seen, 1);
                break;
            }
            if (op == 9) {
                for (size_t i = 0; i < (size_t)len; i++) cb[i] ^= mask[i & 3];
                send_frame(w, 10, 1, cb, (size_t)len);
            }
            continue;
        }
        if (mlen + (size_t)len > WSL_MAX_FRAME) break;
        if (mlen + (size_t)len + 1 > mcap) {
            size_t nc = (mlen + (size_t)len + 1) * 2;
            unsigned char *nb = (unsigned char *)realloc(msg, nc);
            if (!nb) break;
            msg = nb;
            mcap = nc;
        }
        if (recv_exact(w, s, msg + mlen, (size_t)len) != 0) break;
        for (size_t i = 0; i < (size_t)len; i++) msg[mlen + i] ^= mask[i & 3];
        mlen += (size_t)len;
        if (!fin) continue;
        msg[mlen] = '\0';
        int end = 0;
        if (w->mode == WSL_ECHO && send_frame(w, 1, 1, msg, mlen) != 0) end = 1;
        if (!end && w->on_msg) end = w->on_msg(w, (const char *)msg, mlen, w->user) != 0;
        mlen = 0;
        if (end) break;
    }
    free(msg);
}

static DWORD WINAPI loop_thread(LPVOID arg)
{
    WsLoop *w = (WsLoop *)arg;
    while (!w->stop) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(w->ls, &rs);
        struct timeval tv = { 0, 50000 };
        int r = select(0, &rs, NULL, NULL, &tv);
        if (r <= 0) continue;
        SOCKET c = accept(w->ls, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        EnterCriticalSection(&w->send_lock);
        w->cs = c;
        LeaveCriticalSection(&w->send_lock);
        serve(w, c);
        break;                         /* one connection only */
    }
    return 0;
}

WsLoop *ws_loop_start(int mode)
{
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return NULL;
    WsLoop *w = (WsLoop *)calloc(1, sizeof(*w));
    if (!w) { WSACleanup(); return NULL; }
    w->mode = mode;
    w->cs = INVALID_SOCKET;
    InitializeCriticalSection(&w->send_lock);
    w->ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int salen = (int)sizeof(sa);
    if (w->ls == INVALID_SOCKET || bind(w->ls, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
        listen(w->ls, 4) != 0 || getsockname(w->ls, (struct sockaddr *)&sa, &salen) != 0) {
        if (w->ls != INVALID_SOCKET) closesocket(w->ls);
        DeleteCriticalSection(&w->send_lock);
        free(w);
        WSACleanup();
        return NULL;
    }
    w->port = ntohs(sa.sin_port);
    w->thread = CreateThread(NULL, 0, loop_thread, w, 0, NULL);
    if (!w->thread) {
        closesocket(w->ls);
        DeleteCriticalSection(&w->send_lock);
        free(w);
        WSACleanup();
        return NULL;
    }
    return w;
}

int ws_loop_port(WsLoop *w) { return w ? w->port : 0; }

void ws_loop_stop(WsLoop *w)
{
    if (!w) return;
    InterlockedExchange(&w->stop, 1);
    WaitForSingleObject(w->thread, INFINITE);
    CloseHandle(w->thread);
    if (w->cs != INVALID_SOCKET) closesocket(w->cs);
    closesocket(w->ls);
    DeleteCriticalSection(&w->send_lock);
    free(w);
    WSACleanup();
}

void ws_loop_set_script(WsLoop *w,
                        int (*on_msg)(WsLoop *, const char *, size_t, void *), void *u)
{
    w->on_msg = on_msg;
    w->user = u;
}

static int wait_upgraded(WsLoop *w)
{
    for (int i = 0; i < 300 && !w->upgraded; i++) Sleep(10);
    return w->upgraded ? 0 : -1;
}

int ws_loop_send_text(WsLoop *w, const char *s)
{
    if (wait_upgraded(w) != 0) return -1;
    return send_frame(w, 1, 1, s, strlen(s));
}

int ws_loop_send_fragmented(WsLoop *w, const char *s, size_t n, int parts)
{
    if (parts < 1 || wait_upgraded(w) != 0) return -1;
    size_t each = n / (size_t)parts;
    size_t off = 0;
    for (int i = 0; i < parts; i++) {
        size_t k = i == parts - 1 ? n - off : each;
        if (send_frame(w, i == 0 ? 1 : 0, i == parts - 1, s + off, k) != 0) return -1;
        off += k;
    }
    return 0;
}

const char *ws_loop_request(WsLoop *w) { return w->request; }

int ws_loop_close_seen(WsLoop *w) { return w->close_seen ? 1 : 0; }

#endif /* _WIN32 */
