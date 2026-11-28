#include "ssh_pty.h"
#include "pty_size.h"
#include <string.h>

int ssh_pty_request(SSHChannel *ch, const char *term_type, int cols, int rows) {
    if (!ch || !ch->channel) return -1;
    int rc = libssh2_channel_request_pty_ex(ch->channel, term_type, (unsigned int)strlen(term_type), NULL, 0, cols, rows, 0, 0);
    if (rc == 0) {
        /* Remember the size the server now has, so adopting the channel at
         * the same size, or a repeated resize, sends nothing. */
        PtySizeState st;
        pty_size_init(&st);
        pty_size_sent(&st, cols, rows);
        ch->last_cols = st.cols;
        ch->last_rows = st.rows;
    }
    return rc;
}

int ssh_pty_shell(SSHChannel *ch) {
    if (!ch || !ch->channel) return -1;
    return libssh2_channel_shell(ch->channel);
}

int ssh_pty_resize(SSHChannel *ch, int cols, int rows) {
    if (!ch || !ch->channel) return -1;
    PtySizeState last = { ch->last_cols, ch->last_rows };
    if (!pty_size_needs_send(&last, cols, rows)) return 0;
    int rc = libssh2_channel_request_pty_size_ex(ch->channel, cols, rows, 0, 0);
    if (rc == 0) {
        ch->last_cols = cols;
        ch->last_rows = rows;
    }
    return rc;
}