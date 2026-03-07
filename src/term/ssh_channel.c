#include "ssh_channel.h"
#include "../core/xmalloc.h"
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

SSHChannel *ssh_channel_open(SshSession *s) {
    if (!s || !s->session || !s->connected) return NULL;

    LIBSSH2_CHANNEL *c = libssh2_channel_open_session(s->session);
    if (!c) return NULL;

    SSHChannel *ch = xmalloc(sizeof(SSHChannel));
    ch->channel = c;
    ch->ssh = s;
    ch->last_cols = 0;
    ch->last_rows = 0;
    return ch;
}

void ssh_channel_free(SSHChannel *ch) {
    if (ch) {
        if (ch->channel) {
            libssh2_channel_close(ch->channel);
            libssh2_channel_free(ch->channel);
        }
        free(ch);
    }
}

/* Wait for the underlying socket to be ready in the direction(s) libssh2
 * needs.  Returns 0 on success, -1 on timeout/error. */
static int waitsocket(SSHChannel *ch)
{
    if (!ch->ssh) return -1;

    SOCKET sock = ch->ssh->socket;
    int dir = libssh2_session_block_directions(ch->ssh->session);

    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
        FD_SET(sock, &rfds);
    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
        FD_SET(sock, &wfds);

    struct timeval tv = {0, 50000}; /* 50 ms */
    return select((int)sock + 1, &rfds, &wfds, NULL, &tv);
}

int ssh_channel_write(SSHChannel *ch, const char *data, size_t len) {
    if (!ch || !ch->channel) return -1;

    ssize_t rc;
    int retries = 0;
    do {
        rc = libssh2_channel_write(ch->channel, data, len);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            waitsocket(ch);
        }
    } while (rc == LIBSSH2_ERROR_EAGAIN && ++retries < 20);

    return (int)rc;
}