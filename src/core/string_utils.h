#ifndef NUTSHELL_CORE_STRING_UTILS_H
#define NUTSHELL_CORE_STRING_UTILS_H

#include <stddef.h>
#include <stdint.h>

char *str_dup(const char *s);
void str_cat(char *dst, size_t dst_size, const char *src);
void str_trim(char *s);
int str_starts_with(const char *s, const char *prefix);
int str_ends_with(const char *s, const char *suffix);

/* Append a formatted string to buf at offset *len, capped at cap bytes
 * (including the NUL terminator). On success, advances *len past the
 * appended text and returns 1; buf is always NUL-terminated at (the new,
 * or on failure the original) *len.
 *
 * On failure -- the formatted text would not fit in the remaining space,
 * or a bad argument was passed -- buf and *len are left exactly as they
 * were before the call (buf is re-terminated at the original *len, in
 * case vsnprintf wrote a truncated prefix past that point). This is the
 * `bp += snprintf(...)` accumulation pattern from C3/L3, but safe: the
 * caller never needs to check the return of snprintf itself, just stop
 * appending once this returns 0. */
int str_append_fmt(char *buf, size_t cap, size_t *len, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

/* Encode a Unicode codepoint as UTF-8 into buf (must have room for 4 bytes).
 * Returns the number of bytes written, or 0 for invalid codepoints. */
int utf8_encode(uint32_t cp, char *buf);

/* Strip ANSI/VT escape sequences from src (length src_len) and write the
 * plain-text result into dst (capacity dst_size).  Always null-terminates dst.
 * Returns the number of bytes written (excluding the null terminator). */
size_t ansi_strip(char *dst, size_t dst_size, const char *src, size_t src_len);

#endif