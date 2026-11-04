#ifndef NUTSHELL_SSH_SESSION_H
#define NUTSHELL_SSH_SESSION_H

#include <stdbool.h>
#include <stdint.h>

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
    uint64_t bytes_read_total;   /* incremented by libssh2 RECV callback;
                                   accessed only after WM_CONN_DONE on the
                                   UI thread, so no atomicity needed */
} SshSession;

/* Limit, in ms, on the connection-setup stages: handshake, authentication,
 * channel open, PTY and shell requests.  It never applies to an
 * established session: window.c clears it with ssh_session_set_timeout(s, 0)
 * just before the session goes non-blocking. */
#define SSH_SETUP_TIMEOUT_MS 15000

SshSession *ssh_session_new(void);
void ssh_session_free(SshSession *s);
int ssh_connect(SshSession *s, const char *host, int port);
int ssh_auth_password(SshSession *s, const char *username, const char *password);
int ssh_auth_key(SshSession *s, const char *username, const char *key_path, const char *passphrase);
void ssh_session_set_blocking(SshSession *s, bool blocking);

/* Sets libssh2's API timeout for blocking calls on this session; 0 means no
 * limit.  NULL-safe. */
void ssh_session_set_timeout(SshSession *s, long ms);

/* 1 when the last libssh2 call on this session failed by hitting that
 * timeout (libssh2_session_last_errno() == LIBSSH2_ERROR_TIMEOUT); 0 for
 * NULL or a session without a libssh2 handle.  Lets callers ask without
 * including libssh2.h. */
int ssh_session_timed_out(const SshSession *s);

/* Test hook: ssh_connect() uses `ms` instead of SSH_SETUP_TIMEOUT_MS while
 * it is positive; 0 restores the default. */
void ssh_setup_timeout_override(long ms);

#endif