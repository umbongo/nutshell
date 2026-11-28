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
        ch->pending_cols = 0;
        ch->pending_rows = 0;
    }
    return rc;
}

int ssh_pty_shell(SSHChannel *ch) {
    if (!ch || !ch->channel) return -1;
    return libssh2_channel_shell(ch->channel);
}

static int pty_size_send(void *ctx, int cols, int rows) {
    return libssh2_channel_request_pty_size_ex((LIBSSH2_CHANNEL *)ctx, cols, rows, 0, 0);
}

int ssh_pty_resize(SSHChannel *ch, int cols, int rows) {
    if (!ch || !ch->channel) return -1;
    PtySizeState st = { ch->last_cols, ch->last_rows,
                        ch->pending_cols, ch->pending_rows };
    int rc = pty_size_apply(&st, cols, rows, pty_size_send, ch->channel,
                            LIBSSH2_ERROR_EAGAIN);
    ch->last_cols = st.cols;
    ch->last_rows = st.rows;
    ch->pending_cols = st.pending_cols;
    ch->pending_rows = st.pending_rows;
    return rc;
}

int ssh_pty_retry_pending(SSHChannel *ch) {
    if (!ch || !ch->channel) return -1;
    PtySizeState st = { ch->last_cols, ch->last_rows,
                        ch->pending_cols, ch->pending_rows };
    if (!pty_size_pending(&st)) return 0;
    return ssh_pty_resize(ch, ch->pending_cols, ch->pending_rows);
}
