#ifndef NUTSHELL_SSH_CHANNEL_H
#define NUTSHELL_SSH_CHANNEL_H

#include "ssh_session.h"
#include <libssh2.h>

typedef struct {
    LIBSSH2_CHANNEL *channel;
    int last_cols;
    int last_rows;
} SSHChannel;

SSHChannel *ssh_channel_open(SshSession *session);
void ssh_channel_free(SSHChannel *channel);
int ssh_channel_write(SSHChannel *channel, const char *data, size_t len);

#endif