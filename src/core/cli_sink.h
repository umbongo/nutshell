#ifndef NUTSHELL_CLI_SINK_H
#define NUTSHELL_CLI_SINK_H

/*
 * cli_sink -- where nutshell.exe's command-line output (--help, --version,
 * --list, usage errors) goes, and how it is written there. The exe is a
 * GUI-subsystem program, so an interactive shell does not wait for it: cmd
 * and PowerShell print their next prompt at once and text written to the
 * parent console through AttachConsole (src/main.c, the 2026-08-28 CLI
 * design) lands wherever the cursor is by then. That race cannot be
 * removed for a GUI exe. But when the parent redirected the stream the
 * process inherits a real handle and the text must go there: a pipe
 * ("| more", a capturing script) is drained by a reader that waits for
 * EOF, so the output comes out in order; a file ("> out.txt") at least
 * receives the text instead of staying empty while the console got it
 * (the behaviour until v1.2.30).
 *
 * Portable, no windows.h. The file-type values mirror Win32's FILE_TYPE_*
 * so main.c passes GetFileType() through unchanged (it static-asserts the
 * match). The write loop takes an injected writer so its partial-write and
 * closed-pipe rules are tested natively; main.c supplies WriteFile.
 */

#include <stddef.h>

typedef enum {
    CLI_FT_UNKNOWN = 0, /* FILE_TYPE_UNKNOWN: no usable handle */
    CLI_FT_DISK = 1,    /* FILE_TYPE_DISK: "> file" */
    CLI_FT_CHAR = 2,    /* FILE_TYPE_CHAR: the console, NUL, COM, LPT */
    CLI_FT_PIPE = 3     /* FILE_TYPE_PIPE: "| more", a capturing parent */
} CliFileType;

typedef enum {
    CLI_SINK_CONSOLE = 0, /* attach to the parent console, else MessageBox */
    CLI_SINK_HANDLE = 1   /* write to the inherited standard handle */
} CliSink;

/* handle_valid: the standard handle is neither NULL nor
 * INVALID_HANDLE_VALUE. file_type: its CliFileType. is_console: for a
 * character device, whether GetConsoleMode() on it succeeds (after
 * AttachConsole, or an inherited console handle may be refused); ignored
 * for other types. A disk file or a pipe is a redirect. A character
 * device that is the console takes the console path, which prints a
 * leading newline to get off the shell's prompt line; one that is not
 * (NUL, a serial port) is written like a file, so "> NUL" is silent and a
 * console-less parent never gets a MessageBox for it. An unknown type is
 * no stream at all. */
CliSink cli_sink_for(int handle_valid, unsigned file_type, int is_console);

/* What one call of the injected writer reports. CLOSED is a pipe whose
 * reader has gone (ERROR_BROKEN_PIPE / ERROR_NO_DATA): the text was piped
 * away on purpose, so nothing else should show it. */
typedef enum {
    CLI_IO_OK = 0,
    CLI_IO_ERROR = 1,
    CLI_IO_CLOSED = 2
} CliIoStatus;

/* Writes up to `len` bytes of `buf`; sets *written to the count actually
 * taken (0 is allowed with CLI_IO_OK and is treated as an error, so a
 * non-blocking pipe cannot spin). */
typedef CliIoStatus (*CliWriteFn)(void *ctx, const char *buf, size_t len,
                                  size_t *written);

typedef enum {
    CLI_WRITE_DONE = 0,    /* everything written */
    CLI_WRITE_NONE = 1,    /* nothing written, ordinary failure: the caller
                            * may fall back to the console */
    CLI_WRITE_STOPPED = 2  /* part written, or the reader is gone: stop
                            * quietly, never show the text twice */
} CliWriteResult;

/* Converts `text` to CRLF line ends (what a text-mode stdout would have
 * produced; existing CRLF is left alone) and writes all of it through
 * `fn`, resuming after partial writes. Empty text is DONE without a
 * call. */
CliWriteResult cli_write_all(CliWriteFn fn, void *ctx, const char *text);

#endif /* NUTSHELL_CLI_SINK_H */
