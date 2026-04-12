#ifndef NUTSHELL_JSON_VALIDATE_H
#define NUTSHELL_JSON_VALIDATE_H

#include <stddef.h>

/* Write a JSON-escaped string into buf at position pos.
 * If add_quotes is non-zero, wraps the output in double quotes.
 * s_len is the byte length of s (does not rely on NUL terminator).
 * Returns new position, or 0 on overflow. */
size_t json_escape_string(const char *s, size_t s_len, char *buf,
                          size_t buf_size, size_t pos, int add_quotes);

/* Validate a JSON buffer for syntax correctness and proper string escaping.
 * Returns 1 if valid, 0 if invalid.
 * On failure, writes a human-readable error to error (includes byte offset). */
int json_validate(const char *buf, size_t len, char *error, size_t error_size);

#endif
