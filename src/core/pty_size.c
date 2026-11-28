#include "pty_size.h"

#include <stddef.h>

void pty_size_init(PtySizeState *st)
{
    if (!st) return;
    st->cols = 0;
    st->rows = 0;
    st->pending_cols = 0;
    st->pending_rows = 0;
}

void pty_size_sent(PtySizeState *st, int cols, int rows)
{
    if (!st || cols <= 0 || rows <= 0) return;
    st->cols = cols;
    st->rows = rows;
}

int pty_size_needs_send(const PtySizeState *st, int cols, int rows)
{
    if (cols <= 0 || rows <= 0) return 0;
    if (!st) return 1;
    return (cols != st->cols || rows != st->rows) ? 1 : 0;
}

int pty_size_pending(const PtySizeState *st)
{
    if (!st) return 0;
    return (st->pending_cols > 0 && st->pending_rows > 0) ? 1 : 0;
}

int pty_size_apply(PtySizeState *st, int cols, int rows,
                   PtySizeSendFn send, void *ctx, int again_rc)
{
    if (!st || !send) return -1;

    if (pty_size_pending(st)) {
        int rc = send(ctx, st->pending_cols, st->pending_rows);
        if (rc == again_rc) return rc;          /* still in flight */
        if (rc == 0) pty_size_sent(st, st->pending_cols, st->pending_rows);
        st->pending_cols = 0;                   /* finished, or failed for good */
        st->pending_rows = 0;
        if (rc != 0) return rc;
    }

    if (!pty_size_needs_send(st, cols, rows)) return 0;
    int rc = send(ctx, cols, rows);
    if (rc == 0) {
        pty_size_sent(st, cols, rows);
    } else if (rc == again_rc) {
        st->pending_cols = cols;
        st->pending_rows = rows;
    }
    return rc;
}
