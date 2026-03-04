#ifndef CONGA_CORE_LOG_FORMAT_H
#define CONGA_CORE_LOG_FORMAT_H

#include <stddef.h>

/*
 * log_format_filename — build a log file path.
 *
 *   name      session name (NULL/empty → "session")
 *   dir       directory (NULL/empty → ".")
 *   buf       output buffer
 *   buf_size  capacity including NUL
 *
 * Writes "<dir>/<safe_name>-YYYYMMDD_HHMMSS.log" into buf.
 * Sanitises name: only [A-Za-z0-9._-] kept; spaces→underscores; rest dropped.
 * Returns bytes written (excluding NUL), or 0 on error.
 */
int log_format_filename(const char *name, const char *dir,
                        char *buf, size_t buf_size);

#endif
