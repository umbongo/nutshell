#include "ssh_session.h"
#include "../core/xmalloc.h"
#include "../core/secure_zero.h"
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
#define closesocket close
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#endif
#include <errno.h>   /* EAGAIN/EINTR/EBADF/EIO for the libssh2 recv contract */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    libssh2_session_callback_set2(s->session,
                                  LIBSSH2_CALLBACK_RECV,
                                  (libssh2_cb_generic *)nutshell_recv_cb);

    if (libssh2_session_handshake(s->session, (libssh2_socket_t)s->socket)) {
        snprintf(s->last_error, sizeof(s->last_error), "SSH handshake failed");
        closesocket(s->socket);
        s->socket = INVALID_SOCKET;
        return -1;
    }

    /* Configure SSH-level keepalive: 30s interval, want_reply=1 so the
     * server must respond.  The reply travels through our RECV callback
     * and bumps bytes_read_total, which keeps the network-failure rail
     * happy on idle but healthy connections.  libssh2_keepalive_send is
     * driven once per second by the WM_TIMER poll loop. */
    libssh2_keepalive_config(s->session, 1, 30);

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

void ssh_session_set_blocking(SshSession *s, bool blocking) {
    if (s && s->session) {
        libssh2_session_set_blocking(s->session, blocking ? 1 : 0);
    }
}
