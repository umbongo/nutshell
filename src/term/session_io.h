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
} SessionIo;

#endif /* NUTSHELL_SESSION_IO_H */
