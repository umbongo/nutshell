#include "ssh_pty.h"
#include <string.h>

int ssh_pty_request(SSHChannel *ch, const char *term_type, int cols, int rows) {
    if (!ch || !ch->channel) return -1;
    return libssh2_channel_request_pty_ex(ch->channel, term_type, (unsigned int)strlen(term_type), NULL, 0, cols, rows, 0, 0);
}

int ssh_pty_shell(SSHChannel *ch) {
    if (!ch || !ch->channel) return -1;
    return libssh2_channel_shell(ch->channel);
}

int ssh_pty_resize(SSHChannel *ch, int cols, int rows) {
    if (!ch || !ch->channel) return -1;
    if (cols == ch->last_cols && rows == ch->last_rows) return 0;
    int rc = libssh2_channel_request_pty_size_ex(ch->channel, cols, rows, 0, 0);
    if (rc == 0) {
        ch->last_cols = cols;
        ch->last_rows = rows;
    }
    return rc;
}