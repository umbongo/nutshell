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
#define closesocket close
#define WSAStartup(a,b) (void)0
#define WSACleanup() (void)0
typedef int WSADATA;
#define MAKEWORD(a,b) 0
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    /* Zero sensitive data before releasing memory */
    memset(s->cached_passphrase, 0, sizeof(s->cached_passphrase));
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
    
    s->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (s->socket == INVALID_SOCKET) {
        snprintf(s->last_error, sizeof(s->last_error), "Failed to create socket");
        return -1;
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = inet_addr(host);
    
    if (sin.sin_addr.s_addr == INADDR_NONE) {
        const struct hostent *he = gethostbyname(host);
        if (!he) {
            snprintf(s->last_error, sizeof(s->last_error), "Host not found");
            return -1;
        }
        memcpy(&sin.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }

    if (connect(s->socket, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0) {
        snprintf(s->last_error, sizeof(s->last_error), "Connection failed");
        return -1;
    }

    if (libssh2_session_handshake(s->session, (libssh2_socket_t)s->socket)) {
        snprintf(s->last_error, sizeof(s->last_error), "SSH handshake failed");
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
    // Assuming public key is key_path + ".pub" or NULL (libssh2 can try to derive it)
    return libssh2_userauth_publickey_fromfile(s->session, username, NULL, key_path, passphrase);
}

void ssh_session_set_blocking(SshSession *s, bool blocking) {
    if (s && s->session) {
        libssh2_session_set_blocking(s->session, blocking ? 1 : 0);
    }
}