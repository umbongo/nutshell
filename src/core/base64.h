/* src/core/base64.h */
#ifndef NUTSHELL_BASE64_H
#define NUTSHELL_BASE64_H

#include <stddef.h>

/* Base64-encode src_len bytes from src into dst.
 * dst_size must be at least ((src_len + 2) / 3) * 4 + 1.
 * Returns number of chars written (excluding NUL), or 0 on error. */
size_t base64_encode(const unsigned char *src, size_t src_len,
                     char *dst, size_t dst_size);

/* Decode the standard-alphabet base64 in src[0..len) into dst (cap bytes). Padding
 * is optional at the very end; whitespace, any other byte, a '=' anywhere but the
 * end and a length of 1 mod 4 are errors. Returns the byte count, or (size_t)-1 on
 * a bad byte, a NULL argument or a cap too small. len 0 gives 0. */
size_t base64_decode(const char *src, size_t len, unsigned char *dst, size_t cap);

#endif /* NUTSHELL_BASE64_H */
