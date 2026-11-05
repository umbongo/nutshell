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

#include "ssh_kbd.h"

typedef struct {
    LIBSSH2_SESSION *session;
    SOCKET socket;
    bool connected;
    char last_error[256];
    char cached_passphrase[256]; /* zeroed on free; never written to disk */
    uint64_t bytes_read_total;   /* incremented by libssh2 RECV callback;
                                   accessed only after WM_CONN_DONE on the
                                   UI thread, so no atomicity needed */
    void *kbd_ctx;               /* SshKbdCtx * while a keyboard-interactive
                                   call is running, else NULL; set and
                                   cleared by ssh_kbd_attempt() */
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

/* The authentication methods the server offers for `user`, from
 * libssh2_userauth_list(): a comma-separated list owned by the session
 * ("publickey,password,keyboard-interactive"), valid until the next libssh2
 * call.  NULL when s is NULL or not connected, or when libssh2 returns NULL
 * (the server accepted "none", or the call failed; ssh_session_authenticated()
 * and ssh_session_last_errno() tell which). */
const char *ssh_auth_list(SshSession *s, const char *user);

/* 1 when the session is authenticated (libssh2_userauth_authenticated());
 * 0 for NULL or a session without a libssh2 handle. */
int ssh_session_authenticated(const SshSession *s);

/* libssh2_session_last_errno(); 0 for NULL or a session without a libssh2
 * handle. */
int ssh_session_last_errno(const SshSession *s);

/* 1 when the peer has closed (or reset) the TCP connection, 0 when it is
 * still up.  libssh2 1.11 overwrites socket errors with
 * LIBSSH2_ERROR_AUTHENTICATION_FAILED / PUBLICKEY_UNVERIFIED inside the
 * keyboard-interactive and publickey calls, so ssh_session_last_errno() never
 * shows a dead transport there; the caller asks this instead.  A one-byte
 * non-blocking recv(MSG_PEEK): 0 bytes is a close, data or "would block" or
 * "interrupted" is alive, any other error is closed.  NULL s gives 0; a
 * session whose socket is invalid (never connected, or already closed) gives
 * 1.  Never consumes data. */
int ssh_session_peer_closed(const SshSession *s);

/* Keyboard-interactive authentication.  A thin adapter: it fills the io hooks
 * of ctx (set_timeout, abort_io, now, io_user) and runs
 * libssh2_userauth_keyboard_interactive_ex() through ssh_kbd_attempt(), whose
 * rounds are answered by ssh_kbd_round() in src/core/ssh_kbd.c.  Returns
 * libssh2's result (0 on success), or -1 when s is NULL or not connected or
 * ctx is NULL.  s->kbd_ctx is NULL again on return. */
int ssh_auth_kbdint(SshSession *s, const char *user, SshKbdCtx *ctx);

/* Sets the SSH-level keepalive: want_reply 1, `interval_s` seconds, 0 turns it
 * off.  ssh_connect() leaves it off; window.c turns it on once the session is
 * interactive.  NULL-safe. */
void ssh_session_set_keepalive(SshSession *s, int interval_s);

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

/* The setup limit in force, in ms: the override while it is positive, else
 * SSH_SETUP_TIMEOUT_MS. */
long ssh_setup_timeout_ms(void);

#endif