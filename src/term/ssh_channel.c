#include "ssh_channel.h"
#include "../core/xmalloc.h"
#include <stdlib.h>

SSHChannel *ssh_channel_open(SshSession *s) {
    if (!s || !s->session || !s->connected) return NULL;
    
    LIBSSH2_CHANNEL *c = libssh2_channel_open_session(s->session);
    if (!c) return NULL;
    
    SSHChannel *ch = xmalloc(sizeof(SSHChannel));
    ch->channel = c;
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
    return (int)libssh2_channel_write(ch->channel, data, len);
}