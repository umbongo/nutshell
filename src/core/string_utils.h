#ifndef NUTSHELL_CORE_STRING_UTILS_H
#define NUTSHELL_CORE_STRING_UTILS_H

#include <stddef.h>

char *str_dup(const char *s);
void str_cat(char *dst, size_t dst_size, const char *src);
void str_trim(char *s);
int str_starts_with(const char *s, const char *prefix);
int str_ends_with(const char *s, const char *suffix);

/* Strip ANSI/VT escape sequences from src (length src_len) and write the
 * plain-text result into dst (capacity dst_size).  Always null-terminates dst.
 * Returns the number of bytes written (excluding the null terminator). */
size_t ansi_strip(char *dst, size_t dst_size, const char *src, size_t src_len);

#endif