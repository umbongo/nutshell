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
    int pending_cols;   /* a request that returned "try again": 0 when none */
    int pending_rows;
} PtySizeState;

/* Nothing sent yet. NULL is tolerated. */
void pty_size_init(PtySizeState *st);

/* Record that cols x rows reached the server. Ignored for NULL or a
 * non-positive size. */
void pty_size_sent(PtySizeState *st, int cols, int rows);

/* 1 when cols x rows is a valid size that differs from the last one sent
 * (or none was sent yet), else 0. NULL state counts as nothing sent. */
int pty_size_needs_send(const PtySizeState *st, int cols, int rows);

/* 1 when a size request returned "try again" and is not yet finished, else 0.
 * The one rule for "a retry is owed"; NULL counts as none. */
int pty_size_pending(const PtySizeState *st);

/* Sends one size request; returns the transport's code: 0 when the request
 * went through, the caller's "try again" code when it is still in flight, any
 * other value for a hard error. */
typedef int (*PtySizeSendFn)(void *ctx, int cols, int rows);

/*
 * Brings the remote to cols x rows through send(). libssh2 builds a size
 * request's packet only when idle: after it returns "try again" it keeps
 * that packet, and the next call, whatever size it names, sends the OLD one
 * and returns 0. So a request that returns again_rc is remembered as pending
 * and recorded as sent only when a later call returns 0; the next call here
 * repeats the pending size first, then applies the normal rule for the size
 * now wanted (which may send a second request). A hard error leaves the
 * recorded size alone and drops the pending one (libssh2 is idle again).
 * Returns 0 when the remote has the wanted size or none was needed, else the
 * code of the request that did not finish. NULL state or send returns -1.
 */
int pty_size_apply(PtySizeState *st, int cols, int rows,
                   PtySizeSendFn send, void *ctx, int again_rc);

#endif
