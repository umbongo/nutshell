#include "ssh_session.h"
#include "../core/xmalloc.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>   /* fcntl, F_GETFL, F_SETFL, O_NONBLOCK */
#include <errno.h>
#define closesocket close
#define WSAStartup(a,b) (void)0
#define WSACleanup() (void)0
typedef int WSADATA;
#define MAKEWORD(a,b) 0
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Portable secret-zeroing: volatile prevents the compiler from eliding it. */
static void secure_zero(void *p, size_t n)
{
    volatile unsigned char *vp = (volatile unsigned char *)p;
    while (n--) *vp++ = 0;
}

SshSession *ssh_session_new(void) {
    SshSession *s = xcalloc(1, sizeof(SshSession));
    s->socket = INVALID_SOCKET;

    WSADATA wsadata;
    memset(&wsadata, 0, sizeof(wsadata));
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    (void)wsadata;

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
#ifdef _WIN32
    WSACleanup();
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

    if (libssh2_session_handshake(s->session, (libssh2_socket_t)s->socket)) {
        snprintf(s->last_error, sizeof(s->last_error), "SSH handshake failed");
        closesocket(s->socket);
        s->socket = INVALID_SOCKET;
        return -1;
    }

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
