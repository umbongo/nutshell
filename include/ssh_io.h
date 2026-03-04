#ifndef SSH_IO_H
#define SSH_IO_H

#include "ssh_channel.h"
#include "term.h"

/* 
 * Poll the SSH channel for new data and write it to the terminal.
 * Intended for use in a non-blocking loop.
 * 
 * Returns:
 * > 0 : Number of bytes processed.
 * 0   : No data available (EAGAIN).
 * -2  : EOF (Channel closed).
 * -1  : Error.
 */
int ssh_io_poll(SSHChannel *channel, Terminal *term);

#endif /* SSH_IO_H */