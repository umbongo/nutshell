#include "telnet_io.h"
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "secure_zero.h"

#define TN_RECV_CHUNK 4096
#define TN_RECV_MAX_PER_POLL 10

typedef struct {
    SOCKET     sock;
    TnSession *sess;
    unsigned   serial;
    int        closed;      /* peer closed or a hard socket error */
} TelnetIo;

static volatile LONG g_serial;

/* ---- connect (worker thread) -------------------------------------------- */

static TnConnOutcome classify_err(int err)
{
    if (err == WSAECONNREFUSED) return TN_CONN_REFUSED;
    if (err == WSAETIMEDOUT)    return TN_CONN_TIMEOUT;
    return TN_CONN_OTHER;
}

/* One address. On OK *out is the connected socket, back in blocking mode. */
static TnConnOutcome try_address(const struct addrinfo *ai, unsigned timeout_ms,
                                 SOCKET *out)
{
    SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET) return TN_CONN_OTHER;

    u_long nb = 1;
    ioctlsocket(s, (long)FIONBIO, &nb);

    TnConnOutcome oc = TN_CONN_OK;
    if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) != 0) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            oc = classify_err(err);
        } else {
            fd_set wfds, efds;
            FD_ZERO(&wfds); FD_SET(s, &wfds);
            FD_ZERO(&efds); FD_SET(s, &efds);
            struct timeval tv;
            tv.tv_sec  = (long)(timeout_ms / 1000u);
            tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
            int sel = select(0, NULL, &wfds, &efds, &tv);
            if (sel == 0) {
                oc = TN_CONN_TIMEOUT;
            } else if (sel < 0) {
                oc = TN_CONN_OTHER;
            } else {
                int soerr = 0;
                int len = (int)sizeof(soerr);
                if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &len) != 0)
                    oc = TN_CONN_OTHER;
                else if (soerr != 0)
                    oc = classify_err(soerr);
                else if (FD_ISSET(s, &efds) && !FD_ISSET(s, &wfds))
                    oc = TN_CONN_OTHER;
            }
        }
    }
    if (oc != TN_CONN_OK) { closesocket(s); return oc; }

    nb = 0;
    ioctlsocket(s, (long)FIONBIO, &nb);
    *out = s;
    return TN_CONN_OK;
}

TnConnOutcome telnet_io_connect(const char *host, int port, unsigned timeout_ms,
                                SOCKET *out)
{
    if (!out) return TN_CONN_OTHER;
    *out = INVALID_SOCKET;
    if (!host || port < 1 || port > 65535) return TN_CONN_OTHER;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        if (res) freeaddrinfo(res);
        return TN_CONN_RESOLVE;
    }

    TnConnOutcome acc = TN_CONN_RESOLVE;  /* worst; any attempt improves it */
    for (const struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        SOCKET s = INVALID_SOCKET;
        TnConnOutcome oc = try_address(ai, timeout_ms, &s);
        acc = tn_conn_merge(acc, oc);
        if (oc == TN_CONN_OK) { *out = s; break; }
    }
    freeaddrinfo(res);
    return acc;
}

/* ---- the SessionIo ------------------------------------------------------ */

/* Send what the queue holds. WSAEWOULDBLOCK leaves the rest queued. */
static void flush_out(TelnetIo *t)
{
    for (;;) {
        size_t n = 0;
        const unsigned char *q = tn_session_outq(t->sess, &n);
        if (!q || n == 0u) return;
        int chunk = n > 65536u ? 65536 : (int)n;
        int sent = send(t->sock, (const char *)q, chunk, 0);
        if (sent > 0) {
            tn_session_outq_consume(t->sess, (size_t)sent);
        } else {
            if (sent < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return;
            t->closed = 1;
            return;
        }
    }
}

static int telnet_io_poll(void *ctx, Terminal *term, FILE *log_file)
{
    TelnetIo *t = (TelnetIo *)ctx;
    if (!t) return -1;
    int got_bytes = 0;

    if (!t->closed) flush_out(t);
    tn_session_flush_echo(t->sess, term, log_file);

    for (int i = 0; i < TN_RECV_MAX_PER_POLL && !t->closed; i++) {
        unsigned char buf[TN_RECV_CHUNK];
        int n = recv(t->sock, (char *)buf, (int)sizeof(buf), 0);
        if (n > 0) {
            got_bytes = 1;
            tn_session_rx(t->sess, term, log_file, buf, (size_t)n, GetTickCount());
        } else if (n == 0) {
            t->closed = 1;
        } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
            t->closed = 1;
        } else {
            break;
        }
    }

    tn_session_tick(t->sess, term, log_file, GetTickCount());
    if (!t->closed) flush_out(t);

    if (t->closed) return -2;
    return got_bytes ? 1 : 0;
}

static int telnet_io_write(void *ctx, const char *data, size_t len)
{
    TelnetIo *t = (TelnetIo *)ctx;
    if (!t || t->closed) return -1;
    int rc = tn_session_user_write(t->sess, data, len);
    if (rc < 0) return -1;
    flush_out(t);
    return rc;
}

static int telnet_io_resize(void *ctx, int cols, int rows)
{
    TelnetIo *t = (TelnetIo *)ctx;
    if (!t || t->closed) return 0;
    tn_session_resize(t->sess, cols, rows);
    flush_out(t);
    return 0;
}

static void telnet_io_close(void *ctx)
{
    TelnetIo *t = (TelnetIo *)ctx;
    if (!t) return;
    if (t->sock != INVALID_SOCKET) closesocket(t->sock);
    tn_session_free(t->sess);
    secure_zero(t, sizeof(*t));
    free(t);
}

SessionIo session_io_telnet(SOCKET sock, const TnSessionOpts *opts)
{
    SessionIo io;
    memset(&io, 0, sizeof(io));
    io.kind = SESSION_TELNET;

    TelnetIo *t = (TelnetIo *)calloc(1, sizeof(*t));
    TnSession *s = (t && opts) ? tn_session_new(opts) : NULL;
    if (!t || !s) {
        free(t);
        if (sock != INVALID_SOCKET) closesocket(sock);
        return io;
    }

    u_long nb = 1;
    if (ioctlsocket(sock, (long)FIONBIO, &nb) != 0) {
        tn_session_free(s);
        free(t);
        closesocket(sock);
        return io;
    }
    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, (int)sizeof(one));

    t->sock = sock;
    t->sess = s;
    unsigned serial = (unsigned)InterlockedIncrement(&g_serial);
    if (serial == 0u) serial = (unsigned)InterlockedIncrement(&g_serial);
    t->serial = serial;

    io.poll   = telnet_io_poll;
    io.write  = telnet_io_write;
    io.resize = telnet_io_resize;
    io.close  = telnet_io_close;
    io.ctx    = t;
    return io;
}

int telnet_io_take_password_request(void *ctx)
{
    TelnetIo *t = (TelnetIo *)ctx;
    return t ? tn_session_take_password_request(t->sess) : 0;
}

int telnet_io_answer_password(void *ctx, Terminal *term, const char *password)
{
    TelnetIo *t = (TelnetIo *)ctx;
    if (!t) return 0;
    int sent = tn_session_answer_password(t->sess, term, password);
    if (!t->closed) flush_out(t);
    return sent;
}

unsigned telnet_io_serial(const void *ctx)
{
    const TelnetIo *t = (const TelnetIo *)ctx;
    return t ? t->serial : 0u;
}
