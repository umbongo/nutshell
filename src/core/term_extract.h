#ifndef NUTSHELL_TERM_EXTRACT_H
#define NUTSHELL_TERM_EXTRACT_H

#include "term.h"
#include <stddef.h>

/*
 * Extract visible terminal rows as a UTF-8 string.
 * Each row is separated by '\n'. Trailing spaces per row are trimmed.
 * Returns the number of bytes written (excluding NUL terminator).
 * Returns 0 if term is NULL, buf is NULL, or buf_size is 0.
 */
size_t term_extract_visible(const Terminal *term, char *buf, size_t buf_size);

/*
 * Extract the last N rows (including scrollback) as a UTF-8 string.
 * Useful for providing AI with recent terminal context.
 * Returns the number of bytes written (excluding NUL terminator).
 */
size_t term_extract_last_n(const Terminal *term, int n, char *buf, size_t buf_size);

#endif /* NUTSHELL_TERM_EXTRACT_H */
