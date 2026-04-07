/* src/core/base64.h */
#ifndef NUTSHELL_BASE64_H
#define NUTSHELL_BASE64_H

#include <stddef.h>

/* Base64-encode src_len bytes from src into dst.
 * dst_size must be at least ((src_len + 2) / 3) * 4 + 1.
 * Returns number of chars written (excluding NUL), or 0 on error. */
size_t base64_encode(const unsigned char *src, size_t src_len,
                     char *dst, size_t dst_size);

#endif /* NUTSHELL_BASE64_H */
