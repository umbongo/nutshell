#include "pty_size.h"

#include <stddef.h>

void pty_size_init(PtySizeState *st)
{
    if (!st) return;
    st->cols = 0;
    st->rows = 0;
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
