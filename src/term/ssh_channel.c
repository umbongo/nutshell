#include "ssh_channel.h"
#include "../core/xmalloc.h"
#include <stdlib.h>



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


int ssh_channel_write(SSHChannel *ch, const char *data, size_t len) {
    if (!ch || !ch->channel) return -1;

    /* Temporarily switch to blocking mode so the transport layer fully
     * processes any pending protocol work (e.g. SSH window adjustments
     * after a large read).  In non-blocking mode, libssh2_channel_write
     * can return EAGAIN indefinitely when residual transport-level
     * inbound data hasn't been flushed — the retry loop with waitsocket
     * doesn't clear it because each call only makes partial progress.
     * Blocking mode forces the library to finish all pending work before
     * returning.  For single-byte keystrokes this is effectively instant. */
    if (ch->ssh && ch->ssh->session)
        libssh2_session_set_blocking(ch->ssh->session, 1);

    ssize_t rc = libssh2_channel_write(ch->channel, data, len);

    if (ch->ssh && ch->ssh->session)
        libssh2_session_set_blocking(ch->ssh->session, 0);

    return (int)rc;
}