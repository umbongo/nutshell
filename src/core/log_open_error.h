#ifndef NUTSHELL_CORE_LOG_OPEN_ERROR_H
#define NUTSHELL_CORE_LOG_OPEN_ERROR_H

#include <stddef.h>

/*
 * The plain-words text of the notice shown when Start Logging cannot open
 * its log file: the path, the reason the system gave, and what to do about
 * it. `err` is the errno value fopen() left (0 or unknown gives a generic
 * reason). Always NUL-terminated, truncated at cap. The path comes first,
 * then the reason, then the advice, so a path too long for cap cuts the
 * reason and the advice off, never the path. The fixed wording is 171 bytes,
 * so the notice's 768-byte buffer holds the whole text for a path of up to
 * 512 bytes (the caller's path buffer) and a reason of up to 84 bytes. Returns
 * the length written, NUL excluded; 0 with out untouched when out is NULL or
 * cap is 0.
 */
size_t log_open_error_text(const char *path, int err, char *out, size_t cap);

#endif
