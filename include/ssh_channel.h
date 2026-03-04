#ifndef SSH_CHANNEL_H
#define SSH_CHANNEL_H

#include "ssh_session.h"
#include <libssh2.h>

typedef struct SSHChannel {
    LIBSSH2_CHANNEL *channel;
    SSHSession *session; /* Parent session reference */
} SSHChannel;

/* Lifecycle */
SSHChannel *ssh_channel_open(SSHSession *session);
void ssh_channel_free(SSHChannel *channel);

/* I/O */
int ssh_channel_read(SSHChannel *channel, char *buf, size_t len);
int ssh_channel_write(SSHChannel *channel, const char *buf, size_t len);
int ssh_channel_send_eof(SSHChannel *channel);
int ssh_channel_wait_eof(SSHChannel *channel);

#endif /* SSH_CHANNEL_H */