#ifndef NUTSHELL_SSH_PTY_H
#define NUTSHELL_SSH_PTY_H

#include "ssh_channel.h"

int ssh_pty_request(SSHChannel *channel, const char *term_type, int cols, int rows);
int ssh_pty_shell(SSHChannel *channel);
int ssh_pty_resize(SSHChannel *channel, int cols, int rows);
/* Finishes a size request that returned EAGAIN earlier (see pty_size.h):
 * a no-op, one integer test, when none is pending. Returns 0 when nothing
 * was pending or it completed, else the transport's code. */
int ssh_pty_retry_pending(SSHChannel *channel);

#endif