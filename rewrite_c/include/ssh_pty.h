#ifndef SSH_PTY_H
#define SSH_PTY_H

#include "ssh_channel.h"

/* Request a PTY for the channel.
   term_type: e.g. "xterm", "vanilla" (defaults to "vanilla" if NULL)
   width, height: characters
*/
int ssh_pty_request(SSHChannel *channel, const char *term_type, int width, int height);

/* Resize an existing PTY */
int ssh_pty_resize(SSHChannel *channel, int width, int height);

#endif /* SSH_PTY_H */