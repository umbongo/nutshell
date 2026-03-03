#ifndef CONGA_SSH_IO_H
#define CONGA_SSH_IO_H
#include "ssh_channel.h"
#include "../term/term.h"
#include <stdio.h>
/* Returns >0 if data read, 0 if nothing, -1 on error, -2 on EOF.
 * log_file may be NULL; if non-NULL, ANSI-stripped output is written there. */
int ssh_io_poll(SSHChannel *channel, Terminal *term, FILE *log_file);
#endif