#ifndef NUTSHELL_SSH_IO_H
#define NUTSHELL_SSH_IO_H
#include "ssh_channel.h"
#include "../term/term.h"
#include "../term/session_io.h"
#include <stdio.h>
/* Returns >0 if data read, 0 if nothing, -1 on error, -2 on EOF.
 * log_file may be NULL; if non-NULL, ANSI-stripped output is written there.
 * debug_log may be NULL; if non-NULL, raw bytes are written in readable form
 * (ESC shown as "ESC", printable ASCII as-is, others as \xHH). */
int ssh_io_poll(SSHChannel *channel, Terminal *term, FILE *log_file,
                FILE *debug_log);

/* Fill a SessionIo vtable for an open SSH channel (kind SESSION_SSH).
 * ssh must be the session the channel was opened on; channel may be NULL,
 * in which case the returned vtable has ctx == NULL ("no transport") and
 * must not be used.
 *
 * close() releases the whole SSH transport -- the channel AND the session
 * it was opened on -- so a caller keeping its own SshSession/SSHChannel
 * pointers must clear both after calling it. */
SessionIo session_io_ssh(SshSession *ssh, SSHChannel *channel);
#endif