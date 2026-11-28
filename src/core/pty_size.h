#ifndef NUTSHELL_CORE_PTY_SIZE_H
#define NUTSHELL_CORE_PTY_SIZE_H

/*
 * The last terminal size sent to a remote pty, and the one decision made
 * from it: does this size need to be sent? Pure logic, so the SSH channel
 * code (src/term/ssh_pty.c) and the connect-done handler share one rule and
 * a native test can cover it without libssh2.
 */

typedef struct {
    int cols;   /* 0 until a size has been sent */
    int rows;
} PtySizeState;

/* Nothing sent yet. NULL is tolerated. */
void pty_size_init(PtySizeState *st);

/* Record that cols x rows reached the server. Ignored for NULL or a
 * non-positive size. */
void pty_size_sent(PtySizeState *st, int cols, int rows);

/* 1 when cols x rows is a valid size that differs from the last one sent
 * (or none was sent yet), else 0. NULL state counts as nothing sent. */
int pty_size_needs_send(const PtySizeState *st, int cols, int rows);

#endif
