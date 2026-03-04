#ifndef LIBSSH2_H
#define LIBSSH2_H

#include <stddef.h>
#include <sys/types.h>

#define LIBSSH2_ERROR_EAGAIN -37

typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;
typedef struct _LIBSSH2_CHANNEL LIBSSH2_CHANNEL;

LIBSSH2_SESSION *libssh2_session_init(void);
int libssh2_session_free(LIBSSH2_SESSION *session);
int libssh2_session_disconnect(LIBSSH2_SESSION *session, const char *description);
int libssh2_session_handshake(LIBSSH2_SESSION *session, int socket);
void libssh2_session_set_blocking(LIBSSH2_SESSION *session, int blocking);

int libssh2_userauth_password(LIBSSH2_SESSION *session, const char *username, const char *password);
int libssh2_userauth_publickey_fromfile(LIBSSH2_SESSION *session, const char *username, const char *publickey, const char *privatekey, const char *passphrase);

LIBSSH2_CHANNEL *libssh2_channel_open_session(LIBSSH2_SESSION *session);
int libssh2_channel_free(LIBSSH2_CHANNEL *channel);
int libssh2_channel_close(LIBSSH2_CHANNEL *channel);

ssize_t libssh2_channel_read_ex(LIBSSH2_CHANNEL *channel, int stream_id, char *buf, size_t buflen);
#define libssh2_channel_read(channel, buf, buflen) libssh2_channel_read_ex((channel), 0, (buf), (buflen))
#define libssh2_channel_read_stderr(channel, buf, buflen) libssh2_channel_read_ex((channel), 1, (buf), (buflen))

ssize_t libssh2_channel_write(LIBSSH2_CHANNEL *channel, const char *buf, size_t buflen);
int libssh2_channel_request_pty_ex(LIBSSH2_CHANNEL *channel, const char *term, unsigned int term_len, const char *modes, unsigned int modes_len, int width, int height, int width_px, int height_px);
int libssh2_channel_request_pty_size_ex(LIBSSH2_CHANNEL *channel, int width, int height, int width_px, int height_px);

#endif