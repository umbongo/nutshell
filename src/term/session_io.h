#ifndef NUTSHELL_SESSION_IO_H
#define NUTSHELL_SESSION_IO_H

/* The seam between the terminal and its byte source.
 *
 * Everything above the byte stream (the main window, the AI panel, the
 * system prompt) talks to a session through this vtable and never names a
 * libssh2 type.  See docs/superpowers/specs/2026-09-22-local-shell-design.md
 * section 2.
 *
 * Header only, and deliberately free of <windows.h> and <libssh2.h>: it is
 * included from src/core, which is compiled by the native test build on a
 * host that may have neither.  The SSH implementation lives in
 * src/config/ssh_io.c (session_io_ssh()), which the Makefile already drops
 * from the test build when libssh2 is absent.
 */

#include "term.h"
#include <stddef.h>
#include <stdio.h>

typedef enum { SESSION_SSH = 0, SESSION_LOCAL = 1 } SessionKind;

typedef struct SessionIo {
    /* Same contract as ssh_io_poll: >0 read, 0 nothing, -1 error, -2 EOF. */
    int  (*poll)(void *ctx, Terminal *term, FILE *log_file, FILE *debug_log);
    int  (*write)(void *ctx, const char *data, size_t len);
    int  (*resize)(void *ctx, int cols, int rows);
    void (*close)(void *ctx);        /* releases the transport; the SessionIo is dead after */
    void *ctx;
    SessionKind kind;
    /* Pointer to a monotonic tick count (GetTickCount() on Windows,
     * `unsigned long` rather than DWORD so this header stays Windows-free)
     * of the last keystroke or paste actually written to this session's
     * terminal, owned by whoever holds the SessionIo (window.c's
     * Session.last_term_input_tick -- deliberately not the broader
     * Session.last_user_input_tick, which also counts AI-panel typing and
     * mouse-wheel scrolling; neither can glue text onto a dispatched
     * command, so neither may hold one back) and kept live for as long as
     * this SessionIo is. NULL when the owner does not track one --
     * callers must treat that as "no recent keystroke", not as
     * "keystroke at time zero". Set by the owner right after assigning
     * session_io_local()/session_io_ssh() (neither constructor can fill
     * it in itself: both are given only a transport handle, not the
     * enclosing session). See dispatch_keystroke_too_recent() in
     * src/core/dispatch_line_clear.h, which is the reason this field
     * exists: a no-prefix AI dispatch must not write a command onto the
     * input line while a keystroke the user just made has not been
     * echoed into the terminal buffer yet. */
    const unsigned long *last_input_tick;
} SessionIo;

#endif /* NUTSHELL_SESSION_IO_H */
