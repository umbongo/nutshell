#ifndef NUTSHELL_SSH_IO_H
#define NUTSHELL_SSH_IO_H
#include "ssh_channel.h"
#include "../term/term.h"
#include <stdio.h>
/* Returns >0 if data read, 0 if nothing, -1 on error, -2 on EOF.
 * log_file may be NULL; if non-NULL, ANSI-stripped output is written there.
 * debug_log may be NULL; if non-NULL, raw bytes are written in readable form
 * (ESC shown as "ESC", printable ASCII as-is, others as \xHH). */
int ssh_io_poll(SSHChannel *channel, Terminal *term, FILE *log_file,
                FILE *debug_log);
#endif