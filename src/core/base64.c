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

static int b64_val(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t base64_decode(const char *src, size_t len, unsigned char *dst, size_t cap)
{
    const size_t bad = (size_t)-1;
    if (len == 0) return 0;
    if (!src || (!dst && cap)) return bad;
    size_t pad = 0;
    while (pad < len && src[len - 1 - pad] == '=') pad++;
    if (pad > 0) {
        if (pad > 2 || len % 4 != 0) return bad;   /* padding ends a whole group */
        len -= pad;
    }
    if (len % 4 == 1) return bad;
    size_t out = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        int v = b64_val((unsigned char)src[i]);
        if (v < 0) return bad;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out >= cap) return bad;
            dst[out++] = (unsigned char)((acc >> bits) & 0xFFu);
            acc &= (1u << bits) - 1u;
        }
    }
    return out;
}
