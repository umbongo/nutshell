#include "fake_dpapi.h"
#include "crypto.h"
#include <string.h>

static unsigned char g_fake_dpapi_identity = 0x42;
static int g_fake_dpapi_fail_protect = 0;

void fake_dpapi_set_identity(unsigned char identity)
{
    g_fake_dpapi_identity = identity;
}

void fake_dpapi_set_fail_protect(int fail)
{
    g_fake_dpapi_fail_protect = fail;
}

/* Simple additive checksum over the plaintext, folded into one byte --
 * enough for tests to detect ciphertext tampering (real DPAPI has its own
 * cryptographic integrity check; this fake only needs to be as sensitive
 * to corruption for the "tampered blob fails" tests to mean something). */
static unsigned char checksum(const unsigned char *p, size_t len)
{
    unsigned char c = 0x5A;
    for (size_t i = 0; i < len; i++) c = (unsigned char)(c + p[i] + (unsigned char)i);
    return c;
}

/* Blob layout: [identity(1)][marker(1)][checksum(1)][ciphertext(in_len)]. */
static int fake_dpapi_protect(const unsigned char *in, size_t in_len,
                               unsigned char *out, size_t out_cap,
                               size_t *out_len)
{
    if (!in || !out || !out_len) return CRYPTO_ERR_ARGS;
    if (g_fake_dpapi_fail_protect) return CRYPTO_ERR_ENCRYPT;
    if (out_cap < in_len + 3u) return CRYPTO_ERR_BUFSIZE;
    out[0] = g_fake_dpapi_identity;
    out[1] = 0xA5; /* fixed marker so truncated/garbage blobs are detectable */
    out[2] = checksum(in, in_len);
    for (size_t i = 0; i < in_len; i++) {
        out[3u + i] = (unsigned char)(in[i] ^ 0x5Au ^ (unsigned char)i);
    }
    *out_len = in_len + 3u;
    return CRYPTO_OK;
}

static int fake_dpapi_unprotect(const unsigned char *in, size_t in_len,
                                 unsigned char *out, size_t out_cap,
                                 size_t *out_len)
{
    if (!in || !out || !out_len) return CRYPTO_ERR_ARGS;
    if (in_len < 3u) return CRYPTO_ERR_DECRYPT;
    if (in[0] != g_fake_dpapi_identity) return CRYPTO_ERR_DECRYPT; /* foreign user/PC */
    if (in[1] != 0xA5) return CRYPTO_ERR_DECRYPT;                  /* corrupt */
    size_t pt_len = in_len - 3u;
    if (out_cap < pt_len) return CRYPTO_ERR_BUFSIZE;
    for (size_t i = 0; i < pt_len; i++) {
        out[i] = (unsigned char)(in[3u + i] ^ 0x5Au ^ (unsigned char)i);
    }
    if (in[2] != checksum(out, pt_len)) return CRYPTO_ERR_DECRYPT; /* tampered */
    *out_len = pt_len;
    return CRYPTO_OK;
}

const CryptoDpapiBackend k_fake_dpapi_backend = {
    fake_dpapi_protect, fake_dpapi_unprotect
};
