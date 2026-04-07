/* src/core/base64.c */
#include "base64.h"
#include <openssl/evp.h>

size_t base64_encode(const unsigned char *src, size_t src_len,
                     char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0) return 0;
    if (src_len == 0) { dst[0] = '\0'; return 0; }

    size_t needed = (size_t)(((src_len + 2) / 3) * 4 + 1);
    if (dst_size < needed) return 0;

    int n = EVP_EncodeBlock((unsigned char *)dst, src, (int)src_len);
    if (n <= 0) return 0;
    dst[n] = '\0';
    return (size_t)n;
}
