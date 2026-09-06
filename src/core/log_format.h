#ifndef NUTSHELL_CORE_LOG_FORMAT_H
#define NUTSHELL_CORE_LOG_FORMAT_H

#include <stddef.h>
#include <time.h>

/*
 * log_format_validate — check a strftime-style log-name format string.
 *
 * Returns 1 if every '%' conversion in fmt is one of "YymdHMSjAaBbpZz%"
 * (a literal '%%' counts as one conversion), 0 otherwise — including a
 * NULL/empty fmt or a trailing bare '%'.
 */
int log_format_validate(const char *fmt);

/*
 * log_format_filename — build a full session log path.
 *
 *   name      session name (NULL/empty -> "session")
 *   dir       directory (NULL/empty -> ".")
 *   fmt       strftime format for the timestamp portion. NULL, empty, or
 *             invalid (see log_format_validate) falls back to the
 *             documented default "%Y-%m-%d_%H-%M-%S".
 *   t         broken-down time (caller passes localtime(&now)); passed in
 *             explicitly so callers/tests are deterministic. NULL yields
 *             an empty timestamp portion.
 *   buf       output buffer
 *   buf_size  capacity including NUL
 *
 * Writes "<dir>\<strftime(fmt)>_<safe_name>.log" into buf.
 * Sanitises name: only [A-Za-z0-9._-] kept; spaces->underscores; rest
 * dropped.
 * Returns bytes written (excluding NUL), or 0 on error.
 */
int log_format_filename(const char *name, const char *dir, const char *fmt,
                        const struct tm *t, char *buf, size_t buf_size);

#endif
