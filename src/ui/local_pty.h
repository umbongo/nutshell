#ifndef NUTSHELL_LOCAL_PTY_H
#define NUTSHELL_LOCAL_PTY_H

#ifdef _WIN32

/* The ConPTY transport behind a local shell session.
 *
 * Win32 only, and therefore in src/ui, which the Makefile's NON_TEST_SRCS
 * rule drops from the native test build. Everything portable about a local
 * session (which shell, which command line, which environment) is in
 * src/core/local_shell.c and tested there; this file is the Win32 half.
 *
 * See docs/superpowers/specs/2026-09-22-local-shell-design.md section 3.
 */

#include "local_shell.h"
#include "session_io.h"
#include <stddef.h>
#include <stdio.h>

typedef struct LocalPty LocalPty;

/* How many of the bytes ConPTY emits before we write anything are kept for
 * the Windows test harness to print (does it open with a DSR/DA query?). */
#define LOCAL_PTY_FIRST_MAX ((size_t)512)

/* CreatePseudoConsole's flag for a terminal that reflows its own buffer on a
 * resize (docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md
 * section 0). The MinGW headers this tree builds against do not define it;
 * the value is the one Windows Terminal passes. local_pty_open() does not
 * pass it: the console probe (tests/test_local_pty.c) decides. */
#ifndef PSEUDOCONSOLE_RESIZE_QUIRK
#define PSEUDOCONSOLE_RESIZE_QUIRK 0x2
#endif

/* Open a pseudo-console running spec->command at cols x rows and start the
 * reader thread.
 *
 * Returns NULL on failure, with a human-readable reason in err (when err is
 * non-NULL): ConPTY missing (Windows older than 10 version 1809), spec->kind
 * == SHELL_NONE, or CreateProcess failing. The caller prints that text into
 * the terminal and leaves the tab disconnected. */
LocalPty *local_pty_open(const LocalShellSpec *spec, int cols, int rows,
                         char *err, size_t err_size);

/* The same, with the session's debug-log id (the "s3" in a log line; 0 for
 * none) so the shell.* events and the reader thread's lines carry it, and
 * the flags CreatePseudoConsole gets (a DWORD: 0, or
 * PSEUDOCONSOLE_RESIZE_QUIRK; local_pty_open() passes 0). */
LocalPty *local_pty_open_ex(const LocalShellSpec *spec, int cols, int rows,
                            unsigned log_id, unsigned long pcon_flags,
                            char *err, size_t err_size);

/* The vtable. kind is SESSION_LOCAL and ctx is the LocalPty; the SessionIo
 * is dead once close() has run, exactly as for session_io_ssh(). Returns a
 * zeroed SessionIo (ctx NULL) when pty is NULL. */
SessionIo session_io_local(LocalPty *pty);

/* The four vtable entries, also exported so the Windows harness
 * (tests/test_local_pty.c) can drive them without a SessionIo.
 * poll() follows the ssh_io_poll contract: >0 read, 0 nothing, -1 error,
 * -2 EOF. */
int  local_pty_poll(void *ctx, Terminal *term, FILE *log_file);
int  local_pty_write(void *ctx, const char *data, size_t len);
int  local_pty_resize(void *ctx, int cols, int rows);
void local_pty_close(void *ctx);

/* Non-zero once the child process has exited (the harness waits on this). */
int local_pty_child_exited(const LocalPty *pty);

/* The child's exit code once it has exited (-1 while it is still running or
 * when it cannot be read). For the shell.exit log event. */
long local_pty_exit_code(const LocalPty *pty);

/* Every byte poll() feeds the emulator is also written, unchanged, to tap
 * (NULL stops it) and flushed: the Windows harness's fixture recorder.
 * The caller owns the FILE and closes it after local_pty_close(). */
void local_pty_set_raw_tap(LocalPty *pty, FILE *tap);

/* Copy out the first bytes ConPTY emitted on open (up to
 * LOCAL_PTY_FIRST_MAX, captured before anything was written to the shell).
 * Returns the number of bytes copied. Diagnostics only. */
size_t local_pty_first_bytes(const LocalPty *pty, char *out, size_t out_size);

#endif /* _WIN32 */
#endif /* NUTSHELL_LOCAL_PTY_H */
