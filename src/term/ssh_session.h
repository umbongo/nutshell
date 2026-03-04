#ifndef NUTSHELL_SSH_SESSION_H
#define NUTSHELL_SSH_SESSION_H

#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#endif

#include <libssh2.h>

typedef struct {
    LIBSSH2_SESSION *session;
    SOCKET socket;
    bool connected;
    char last_error[256];
    char cached_passphrase[256]; /* zeroed on free; never written to disk */
} SshSession;

SshSession *ssh_session_new(void);
void ssh_session_free(SshSession *s);
int ssh_connect(SshSession *s, const char *host, int port);
int ssh_auth_password(SshSession *s, const char *username, const char *password);
int ssh_auth_key(SshSession *s, const char *username, const char *key_path, const char *passphrase);
void ssh_session_set_blocking(SshSession *s, bool blocking);

#endif