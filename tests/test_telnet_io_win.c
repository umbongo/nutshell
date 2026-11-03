/* tests/test_telnet_io_win.c -- Win32 loopback tests for the telnet socket
 * backend (src/ui/telnet_io.c). Built only by `make wintest`. 127.0.0.1 only;
 * every wait is bounded at 3 s. */

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"
#include "telnet_io.h"
#include "term_extract.h"

#define WAIT_MS 3000

typedef struct { SOCKET lsn; int port; } Lsn;

static int lsn_open(Lsn *l)
{
    l->lsn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l->lsn == INVALID_SOCKET) return 0;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(l->lsn, (struct sockaddr *)&a, sizeof(a)) != 0) return 0;
    if (listen(l->lsn, 4) != 0) return 0;
    int len = (int)sizeof(a);
    if (getsockname(l->lsn, (struct sockaddr *)&a, &len) != 0) return 0;
    l->port = ntohs(a.sin_port);
    u_long nb = 1;
    ioctlsocket(l->lsn, (long)FIONBIO, &nb);
    return 1;
}

/* Accept one connection, bounded. The accepted socket is non-blocking. */
static SOCKET lsn_accept(const Lsn *l)
{
    for (int i = 0; i < WAIT_MS / 20; i++) {
        SOCKET c = accept(l->lsn, NULL, NULL);
        if (c != INVALID_SOCKET) {
            u_long nb = 1;
            ioctlsocket(c, (long)FIONBIO, &nb);
            return c;
        }
        Sleep(20);
    }
    return INVALID_SOCKET;
}

static int has_run(const unsigned char *h, size_t hl, const void *n, size_t nl)
{
    if (nl > hl) return 0;
    for (size_t i = 0; i + nl <= hl; i++)
        if (memcmp(h + i, n, nl) == 0) return 1;
    return 0;
}

typedef struct { unsigned char b[512]; size_t n; } Rx;

static void srv_drain(SOCKET c, Rx *r)
{
    int got = recv(c, (char *)r->b + r->n, (int)(sizeof(r->b) - r->n), 0);
    if (got > 0) r->n += (size_t)got;
}

/* Poll the client and drain the server until pred(rx, term) or the bound. */
typedef int (*Pred)(const Rx *r, const char *text);

static int pump(SessionIo *io, Terminal *T, SOCKET srv, Rx *r, Pred pred)
{
    for (int i = 0; i < WAIT_MS / 20; i++) {
        (void)io->poll(io->ctx, T, NULL);
        srv_drain(srv, r);
        char text[4096];
        (void)term_extract_visible(T, text, sizeof(text));
        if (pred(r, text)) return 1;
        Sleep(20);
    }
    return 0;
}

static int p_login_answers(const Rx *r, const char *text)
{
    static const unsigned char naws[] = {0xFF,0xFB,0x1F, 0xFF,0xFA,0x1F,0x00,0x50,0x00,0x18,0xFF,0xF0};
    static const unsigned char dosga[] = {0xFF,0xFD,0x01};
    (void)text;
    return has_run(r->b, r->n, naws, sizeof(naws)) &&
           has_run(r->b, r->n, dosga, sizeof(dosga)) &&
           has_run(r->b, r->n, "u\r\n", 3);
}
static int p_pass_sent(const Rx *r, const char *text)
{
    (void)text;
    return has_run(r->b, r->n, "p\r\n", 3);
}

int test_telnet_io_win_connect_ok_and_refused(void)
{
    TEST_BEGIN();
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    Lsn l;
    ASSERT_TRUE(lsn_open(&l));
    SOCKET s = INVALID_SOCKET;
    ASSERT_EQ(telnet_io_connect("127.0.0.1", l.port, 3000u, &s), TN_CONN_OK);
    ASSERT_TRUE(s != INVALID_SOCKET);
    if (s != INVALID_SOCKET) closesocket(s);
    closesocket(l.lsn);
    SOCKET s2 = INVALID_SOCKET;
    ASSERT_EQ(telnet_io_connect("127.0.0.1", l.port, 3000u, &s2), TN_CONN_REFUSED);
    if (s2 != INVALID_SOCKET) closesocket(s2);
    ASSERT_EQ(telnet_io_connect("127.0.0.1", 0, 3000u, &s2), TN_CONN_OTHER);
    WSACleanup();
    TEST_END();
}

int test_telnet_io_win_resolve_fails(void)
{
    TEST_BEGIN();
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    SOCKET s = INVALID_SOCKET;
    ASSERT_EQ(telnet_io_connect("no-such-host.invalid", 23, 3000u, &s), TN_CONN_RESOLVE);
    if (s != INVALID_SOCKET) closesocket(s);
    WSACleanup();
    TEST_END();
}

int test_telnet_io_win_negotiates_and_logs_in(void)
{
    TEST_BEGIN();
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    Lsn l;
    ASSERT_TRUE(lsn_open(&l));
    SOCKET cs = INVALID_SOCKET;
    ASSERT_EQ(telnet_io_connect("127.0.0.1", l.port, 3000u, &cs), TN_CONN_OK);
    SOCKET srv = lsn_accept(&l);
    ASSERT_TRUE(srv != INVALID_SOCKET);
    TnSessionOpts o;
    memset(&o, 0, sizeof(o));
    o.username = "u"; o.password = "p"; o.cols = 80; o.rows = 24;
    o.log_id = 0; o.now_ms = GetTickCount();
    SessionIo io = session_io_telnet(cs, &o);
    ASSERT_TRUE(io.ctx != NULL);
    ASSERT_EQ(io.kind, SESSION_TELNET);
    Terminal *T = term_init(24, 80, 100);
    ASSERT_NOT_NULL(T);
    if (io.ctx && T && srv != INVALID_SOCKET) {
        static const unsigned char hello[] = {0xFF,0xFD,0x1F,0xFF,0xFB,0x01};
        send(srv, (const char *)hello, (int)sizeof(hello), 0);
        send(srv, "login: ", 7, 0);
        Rx r; memset(&r, 0, sizeof(r));
        ASSERT_TRUE(pump(&io, T, srv, &r, p_login_answers));
        send(srv, "Password: ", 10, 0);
        ASSERT_TRUE(pump(&io, T, srv, &r, p_pass_sent));
        char text[4096];
        (void)term_extract_visible(T, text, sizeof(text));
        ASSERT_TRUE(strstr(text, "login:") != NULL);
        ASSERT_TRUE(strstr(text, "p\r") == NULL);
    }
    if (io.ctx) io.close(io.ctx);
    if (T) term_free(T);
    if (srv != INVALID_SOCKET) closesocket(srv);
    closesocket(l.lsn);
    WSACleanup();
    TEST_END();
}

int test_telnet_io_win_eof_reports_closed(void)
{
    TEST_BEGIN();
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    Lsn l;
    ASSERT_TRUE(lsn_open(&l));
    SOCKET cs = INVALID_SOCKET;
    ASSERT_EQ(telnet_io_connect("127.0.0.1", l.port, 3000u, &cs), TN_CONN_OK);
    SOCKET srv = lsn_accept(&l);
    ASSERT_TRUE(srv != INVALID_SOCKET);
    TnSessionOpts o;
    memset(&o, 0, sizeof(o));
    o.username = ""; o.password = ""; o.cols = 80; o.rows = 24;
    o.now_ms = GetTickCount();
    SessionIo io = session_io_telnet(cs, &o);
    ASSERT_TRUE(io.ctx != NULL);
    Terminal *T = term_init(24, 80, 100);
    ASSERT_NOT_NULL(T);
    if (srv != INVALID_SOCKET) closesocket(srv);
    int rc = 0;
    if (io.ctx && T) {
        for (int i = 0; i < WAIT_MS / 20; i++) {
            rc = io.poll(io.ctx, T, NULL);
            if (rc == -2 || rc == -1) break;
            Sleep(20);
        }
        ASSERT_EQ(rc, -2);
        io.close(io.ctx);
    }
    if (T) term_free(T);
    closesocket(l.lsn);
    WSACleanup();
    TEST_END();
}

int test_telnet_io_win_serial_unique(void)
{
    TEST_BEGIN();
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    Lsn l;
    ASSERT_TRUE(lsn_open(&l));
    SOCKET a = INVALID_SOCKET, b = INVALID_SOCKET;
    ASSERT_EQ(telnet_io_connect("127.0.0.1", l.port, 3000u, &a), TN_CONN_OK);
    ASSERT_EQ(telnet_io_connect("127.0.0.1", l.port, 3000u, &b), TN_CONN_OK);
    TnSessionOpts o;
    memset(&o, 0, sizeof(o));
    o.username = ""; o.password = ""; o.cols = 80; o.rows = 24;
    SessionIo ia = session_io_telnet(a, &o);
    SessionIo ib = session_io_telnet(b, &o);
    ASSERT_TRUE(ia.ctx != NULL && ib.ctx != NULL);
    if (ia.ctx && ib.ctx) {
        unsigned sa = telnet_io_serial(ia.ctx), sb = telnet_io_serial(ib.ctx);
        ASSERT_TRUE(sa != 0u);
        ASSERT_TRUE(sb != 0u);
        ASSERT_TRUE(sa != sb);
    }
    if (ia.ctx) ia.close(ia.ctx);
    if (ib.ctx) ib.close(ib.ctx);
    closesocket(l.lsn);
    WSACleanup();
    TEST_END();
}

#endif /* _WIN32 */
