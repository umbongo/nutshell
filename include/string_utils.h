#ifndef STRING_UTILS_H
#define STRING_UTILS_H

/*
 * Safe string utilities.
 *
 * Never use strcat/strcpy/sprintf directly — use these wrappers.
 * All functions that write into a buffer always null-terminate it.
 */

#include <stddef.h>

/* Return a newly allocated duplicate of s (caller must free).
 * Returns NULL if s is NULL. Aborts on OOM. */
char *str_dup(const char *s);

/* Append src to dst, guaranteed to not overflow dst_size bytes
 * (including the null terminator). Always null-terminates dst. */
void str_cat(char *dst, size_t dst_size, const char *src);

/* Trim leading and trailing ASCII whitespace in-place. */
void str_trim(char *s);

/* Return 1 if s starts with prefix, 0 otherwise. */
int str_starts_with(const char *s, const char *prefix);

/* Return 1 if s ends with suffix, 0 otherwise. */
int str_ends_with(const char *s, const char *suffix);

#endif /* STRING_UTILS_H */
