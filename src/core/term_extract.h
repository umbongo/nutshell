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
 * N counts back from the last row that has content; blank rows below the
 * cursor (e.g. after a `clear`) are ignored rather than counted toward N.
 * If buf_size is too small to hold every requested row, the OLDEST rows
 * are dropped first so the most recent terminal output is preserved
 * (only when even the single newest row doesn't fit is it truncated).
 * Returns the number of bytes written (excluding NUL terminator).
 */
size_t term_extract_last_n(const Terminal *term, int n, char *buf, size_t buf_size);

/*
 * term_extract_last_n() into a heap buffer sized exactly for the content, so
 * nothing is ever truncated however wide the rows are -- what a detector
 * that judges a prompt by its first and last characters needs (a fixed
 * buffer keeps the newest rows, but cuts a single row wider than itself,
 * and on wide output a prompt row can be). Returns the NUL-terminated text
 * (caller frees) with its length in *len_out, or NULL with *len_out = 0 when
 * term is NULL, n <= 0, every row is blank, or allocation fails. len_out may
 * be NULL.
 */
char *term_extract_last_n_dup(const Terminal *term, int n, size_t *len_out);

#endif /* NUTSHELL_TERM_EXTRACT_H */
