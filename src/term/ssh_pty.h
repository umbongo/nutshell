#ifndef CONGA_SSH_PTY_H
#define CONGA_SSH_PTY_H

#include "ssh_channel.h"

int ssh_pty_request(SSHChannel *channel, const char *term_type, int cols, int rows);
int ssh_pty_shell(SSHChannel *channel);
int ssh_pty_resize(SSHChannel *channel, int cols, int rows);

#endif