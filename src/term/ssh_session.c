#include "ssh_session.h"
#include "../core/xmalloc.h"
#include "../core/secure_zero.h"
#include "../core/ssh_timeout.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>  /* TCP_KEEPIDLE, TCP_KEEPINTVL */
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>   /* fcntl, F_GETFL, F_SETFL, O_NONBLOCK */
#include <sys/select.h>  /* select, fd_set: not pulled in by libssh2.h on Linux */
#include <sys/time.h>    /* struct timeval under _POSIX_C_SOURCE */
#define closesocket close
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#endif
#include <errno.h>   /* EAGAIN/EINTR/EBADF/EIO for the libssh2 recv contract */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* libssh2 RECV callback signature:
 *   ssize_t recv_cb(libssh2_socket_t sock, void *buffer, size_t length,
 *                   int flags, void **abstract);
 *
 * `*abstract` is the per-session opaque pointer set via
 * libssh2_session_abstract().  We store the SshSession* there so we can
 * bump bytes_read_total without a global.
 *
 * Error contract: libssh2's transport layer compares the return value
 * against -EAGAIN literally (see libssh2 transport.c) — anything else
 * negative is treated as a fatal LIBSSH2_ERROR_SOCKET_RECV.  The default
 * _libssh2_recv translates platform error codes to negative POSIX errno
 * values; we must do the same or the handshake (and every non-blocking
 * channel read after it) will fail. */
static ssize_t nutshell_recv_cb(libssh2_socket_t sock, void *buffer,
                                size_t length, int flags, void **abstract)
{
#ifdef _WIN32
    int n = recv(sock, (char *)buffer, (int)length, flags);
    if (n < 0) {
        switch (WSAGetLastError()) {
        case WSAEWOULDBLOCK: return -EAGAIN;
        case WSAEINTR:       return -EINTR;
        case WSAENOTSOCK:    return -EBADF;
        default:             return -EIO;
        }
    }
#else
    ssize_t n = recv(sock, buffer, length, flags);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return -EAGAIN;
        return -errno;
    }
#endif
    if (n > 0 && abstract && *abstract) {
        SshSession *s = (SshSession *)*abstract;
        s->bytes_read_total += (uint64_t)n;
    }
    return (ssize_t)n;
}

#ifdef _WIN32
#include <mstcpip.h>  /* tcp_keepalive struct, SIO_KEEPALIVE_VALS */
#endif

/* Enable OS-level TCP keepalive on the socket as a backstop for the
 * libssh2-recv liveness check.  Idle 30s, interval 10s.  Quiet on
 * failure — the libssh2-layer detector compensates. */
static void set_tcp_keepalive(SOCKET sock)
{
#ifdef _WIN32
    BOOL enable = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
               (const char *)&enable, (int)sizeof(enable));

    struct tcp_keepalive ka;
    ka.onoff             = 1;
    ka.keepalivetime     = 30000u;  /* idle before first probe (ms) */
    ka.keepaliveinterval = 10000u;  /* interval between probes (ms) */
    DWORD bytes_returned = 0;
    WSAIoctl(sock, SIO_KEEPALIVE_VALS, &ka, (DWORD)sizeof(ka),
             NULL, 0, &bytes_returned, NULL, NULL);
#else
    int enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &enable, (socklen_t)sizeof(enable));
    /* Linux tunables for parity (the user's runtime target is Windows). */
#ifdef TCP_KEEPIDLE
    int idle = 30;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, (socklen_t)sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
    int intvl = 10;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, (socklen_t)sizeof(intvl));
#endif
#endif
}

SshSession *ssh_session_new(void) {
    SshSession *s = xcalloc(1, sizeof(SshSession));
    s->socket = INVALID_SOCKET;
    s->session = libssh2_session_init();
    if (!s->session) {
        free(s);
        return NULL;
    }
    return s;
}

void ssh_session_free(SshSession *s) {
    if (!s) return;
    /* L-1: use secure_zero so the compiler cannot elide the wipe */
    secure_zero(s->cached_passphrase, sizeof(s->cached_passphrase));
    if (s->session) {
        libssh2_session_disconnect(s->session, "Bye");
        libssh2_session_free(s->session);
    }
    if (s->socket != INVALID_SOCKET) {
        closesocket(s->socket);
    }
    free(s);
}

/* Test hook behind ssh_setup_timeout_override(); 0 means the default. */
static long g_setup_timeout_override_ms = 0;

void ssh_setup_timeout_override(long ms)
{
    g_setup_timeout_override_ms = ms > 0 ? ms : 0;
}

long ssh_setup_timeout_ms(void)
{
    return g_setup_timeout_override_ms > 0 ? g_setup_timeout_override_ms
                                           : SSH_SETUP_TIMEOUT_MS;
}

void ssh_session_set_timeout(SshSession *s, long ms)
{
    if (s && s->session) libssh2_session_set_timeout(s->session, ms);
}

int ssh_session_timed_out(const SshSession *s)
{
    if (!s || !s->session) return 0;
    return libssh2_session_last_errno(s->session) == LIBSSH2_ERROR_TIMEOUT ? 1 : 0;
}

int ssh_session_peer_closed(const SshSession *s)
{
    if (!s) return 0;
    if (s->socket == INVALID_SOCKET) return 1;
    char b;
#ifdef _WIN32
    /* Windows has no per-call non-blocking flag: ask select() whether a read
     * would return at once, and only then peek. */
    fd_set rfds;
    struct timeval tv = {0, 0};
    FD_ZERO(&rfds);
    FD_SET(s->socket, &rfds);
    int sel = select(0, &rfds, NULL, NULL, &tv);
    if (sel == SOCKET_ERROR) return 1;
    if (sel == 0) return 0;                     /* nothing to read: alive */
    int n = recv(s->socket, &b, 1, MSG_PEEK);
    if (n > 0) return 0;
    if (n == 0) return 1;
    switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK:
    case WSAEINTR:
        return 0;
    default:
        return 1;
    }
#else
    ssize_t n = recv(s->socket, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n > 0) return 0;
    if (n == 0) return 1;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : 1;
#endif
}

int ssh_connect(SshSession *s, const char *host, int port) {
    if (!s) return -1;

    /* M-7: validate port range */
    if (port < 1 || port > 65535) {
        snprintf(s->last_error, sizeof(s->last_error), "Invalid port number");
        return -1;
    }

    /* H-2: use getaddrinfo — thread-safe and supports IPv6 */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;   /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;

    int gai_rc = getaddrinfo(host, port_str, &hints, &res);
    if (gai_rc != 0 || !res) {
        snprintf(s->last_error, sizeof(s->last_error),
                 "Host not found (getaddrinfo error %d)", gai_rc);
        if (res) freeaddrinfo(res);
        return -1;
    }

    /* Try each resolved address; apply 4-second connect timeout (I-1 / todo). */
    SOCKET sock = INVALID_SOCKET;
    struct addrinfo *ai;
    for (ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == INVALID_SOCKET) continue;

        /* Switch to non-blocking so we can timeout the connect. */
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(sock, (long)FIONBIO, &nb);
#else
        int fl = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, fl | O_NONBLOCK);
#endif

        int cr = connect(sock, ai->ai_addr, (int)ai->ai_addrlen);
        int needs_wait = 0;
#ifdef _WIN32
        needs_wait = (cr != 0 && WSAGetLastError() == WSAEWOULDBLOCK);
#else
        needs_wait = (cr != 0 && errno == EINPROGRESS);
#endif

        if (cr == 0 || needs_wait) {
            if (needs_wait) {
                /* Wait up to 4 seconds for the connect to complete. */
                fd_set wfds, efds;
                FD_ZERO(&wfds); FD_SET(sock, &wfds);
                FD_ZERO(&efds); FD_SET(sock, &efds);
                struct timeval tv = {4, 0};
                int sel = select((int)sock + 1, NULL, &wfds, &efds, &tv);
                if (sel <= 0 || FD_ISSET(sock, &efds)) {
                    closesocket(sock);
                    sock = INVALID_SOCKET;
                    continue;
                }
                /* Verify connect succeeded. */
                int err = 0;
                socklen_t errlen = sizeof(err);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &errlen);
                if (err != 0) {
                    closesocket(sock);
                    sock = INVALID_SOCKET;
                    continue;
                }
            }
            /* Restore blocking mode. */
#ifdef _WIN32
            u_long nb2 = 0;
            ioctlsocket(sock, (long)FIONBIO, &nb2);
#else
            fcntl(sock, F_SETFL, fl & ~O_NONBLOCK);
#endif
            break; /* connected */
        }

        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    freeaddrinfo(res);

    if (sock == INVALID_SOCKET) {
        snprintf(s->last_error, sizeof(s->last_error), "Connection failed");
        return -1;
    }

    s->socket = sock;
    set_tcp_keepalive(sock);

    /* Install our recv hook so libssh2 reads route through us; this lets
     * us track post-handshake socket-level liveness via bytes_read_total.
     * The counter's baseline is reset at WM_CONN_DONE, so KEX/auth bytes
     * are never observed — but the hook must already honour libssh2's
     * -EAGAIN/-errno contract because the handshake itself reads through it. */
    void **abstract = libssh2_session_abstract(s->session);
    if (abstract) *abstract = s;
#if defined(LIBSSH2_VERSION_NUM) && LIBSSH2_VERSION_NUM >= 0x010b01
    libssh2_session_callback_set2(s->session,
                                  LIBSSH2_CALLBACK_RECV,
                                  (libssh2_cb_generic *)nutshell_recv_cb);
#else
    /* libssh2 before 1.11.1 (Ubuntu 24.04 ships 1.11.0) has neither
     * libssh2_cb_generic nor callback_set2; the older setter takes a
     * void *. Go through a union so no function-to-object pointer cast
     * trips -Wpedantic. */
    union {
        void *obj;
        ssize_t (*fn)(libssh2_socket_t, void *, size_t, int, void **);
    } cb;
    cb.fn = nutshell_recv_cb;
    libssh2_session_callback_set(s->session, LIBSSH2_CALLBACK_RECV, cb.obj);
#endif

    /* A server that accepts TCP and then never speaks must not hang the
     * tab: bound the handshake.  The limit stays in force for the stages
     * that follow; window.c clears it before the session goes interactive. */
    long limit = ssh_setup_timeout_ms();
    ssh_session_set_timeout(s, limit);

    if (libssh2_session_handshake(s->session, (libssh2_socket_t)s->socket)) {
        if (ssh_session_timed_out(s)) {
            if (ssh_setup_timeout_text(SSH_STAGE_HANDSHAKE, host, port, limit,
                                       s->last_error,
                                       sizeof(s->last_error)) == 0)
                snprintf(s->last_error, sizeof(s->last_error),
                         "SSH handshake timed out");
        } else {
            snprintf(s->last_error, sizeof(s->last_error), "SSH handshake failed");
        }
        closesocket(s->socket);
        s->socket = INVALID_SOCKET;
        return -1;
    }

    /* SSH-level keepalive stays OFF during setup.  libssh2's wait loop
     * shortens each poll to the time left before the next keepalive is due,
     * and a poll that runs out returns LIBSSH2_ERROR_TIMEOUT, so with a
     * 30 s interval a wait inside a re-armed keyboard-interactive call could
     * stop at the keepalive deadline and fail the attempt.
     * ssh_session_set_keepalive() switches it on (30 s, want_reply 1) once
     * the session is interactive; the reply travels through our RECV
     * callback and bumps bytes_read_total, which keeps the network-failure
     * rail happy on idle but healthy connections.  libssh2_keepalive_send is
     * driven once per second by the WM_TIMER poll loop, which runs only
     * after that point. */
    libssh2_keepalive_config(s->session, 1, 0);

    s->connected = true;
    return 0;
}

int ssh_auth_password(SshSession *s, const char *username, const char *password) {
    if (!s || !s->connected) return -1;
    return libssh2_userauth_password(s->session, username, password);
}

int ssh_auth_key(SshSession *s, const char *username, const char *key_path, const char *passphrase) {
    if (!s || !s->connected) return -1;
    return libssh2_userauth_publickey_fromfile(s->session, username, NULL, key_path, passphrase);
}

const char *ssh_auth_list(SshSession *s, const char *user) {
    if (!s || !s->connected || !user) return NULL;
    return libssh2_userauth_list(s->session, user, (unsigned int)strlen(user));
}

int ssh_session_authenticated(const SshSession *s) {
    if (!s || !s->session) return 0;
    return libssh2_userauth_authenticated(s->session) ? 1 : 0;
}

int ssh_session_last_errno(const SshSession *s) {
    if (!s || !s->session) return 0;
    return libssh2_session_last_errno(s->session);
}

void ssh_session_set_keepalive(SshSession *s, int interval_s) {
    if (!s || !s->session) return;
    libssh2_keepalive_config(s->session, 1,
                             interval_s > 0 ? (unsigned int)interval_s : 0u);
}

/* ---- keyboard-interactive adapter ----------------------------------------
 * Nothing here decides anything: the hooks below wrap one libssh2 or socket
 * call each, and kbd_cb passes every argument to ssh_kbd_round() (src/core). */

/* libssh2 response callback.  *abstract is the SshSession * that
 * ssh_connect() stored with libssh2_session_abstract().  A NULL kbd_ctx
 * (no attempt running) answers nothing. */
static LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC(kbd_cb)
{
    SshSession *ss = (abstract && *abstract) ? (SshSession *)*abstract : NULL;
    if (!ss || !ss->kbd_ctx) return;
    ssh_kbd_round((SshKbdCtx *)ss->kbd_ctx, name, name_len, instruction,
                  instruction_len, num_prompts, prompts, responses);
}

static void kbd_set_timeout(void *io_user, long ms)
{
    ssh_session_set_timeout((SshSession *)io_user, ms);
}

static void kbd_abort_io(void *io_user)
{
    SshSession *ss = (SshSession *)io_user;
    if (!ss || ss->socket == INVALID_SOCKET) return;
#ifdef _WIN32
    shutdown(ss->socket, SD_BOTH);
#else
    shutdown(ss->socket, SHUT_RDWR);
#endif
}

static long long kbd_now(void *io_user)
{
    (void)io_user;
    return (long long)time(NULL);
}

typedef struct {
    SshSession *s;
    const char *user;
} KbdCallArgs;

static int kbd_call(void *arg)
{
    KbdCallArgs *a = (KbdCallArgs *)arg;
    return libssh2_userauth_keyboard_interactive_ex(
        a->s->session, a->user, (unsigned int)strlen(a->user), kbd_cb);
}

int ssh_auth_kbdint(SshSession *s, const char *user, SshKbdCtx *ctx) {
    if (!s || !s->connected || !ctx || !user) return -1;
    ctx->set_timeout = kbd_set_timeout;
    ctx->abort_io    = kbd_abort_io;
    ctx->now         = kbd_now;
    ctx->io_user     = s;
    KbdCallArgs args = { s, user };
    return ssh_kbd_attempt(&s->kbd_ctx, ctx, kbd_call, &args);
}

void ssh_session_set_blocking(SshSession *s, bool blocking) {
    if (s && s->session) {
        libssh2_session_set_blocking(s->session, blocking ? 1 : 0);
    }
}
