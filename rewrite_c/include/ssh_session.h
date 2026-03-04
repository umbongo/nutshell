#ifndef SSH_SESSION_H
#define SSH_SESSION_H

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#define SOCKET int
#define INVALID_SOCKET -1
#endif
#include <libssh2.h>
#include <stdbool.h>

typedef struct SSHSession {
    SOCKET sock;
    LIBSSH2_SESSION *session;
    char *hostname;
    int port;
    bool connected;
    bool authenticated;
    char last_error[256];
} SSHSession;

/* Lifecycle */
SSHSession *ssh_session_new(void);
void ssh_session_free(SSHSession *session);

/* Connection & Auth */
int ssh_connect(SSHSession *session, const char *hostname, int port);
int ssh_auth_password(SSHSession *session, const char *username, const char *password);
int ssh_auth_key(SSHSession *session, const char *username, const char *privkey_path, const char *passphrase);
void ssh_disconnect(SSHSession *session);
void ssh_session_set_blocking(SSHSession *session, bool blocking);

#endif /* SSH_SESSION_H */