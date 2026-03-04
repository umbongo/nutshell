#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif
#include "crypto.h"
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* ---- Constants ----------------------------------------------------------- */

#define NONCE_LEN   12
#define TAG_LEN     16
#define KEY_LEN     32

/* PBKDF2 parameters */
static const char PBKDF2_SALT[] = "CongaSSH-v1";
#define PBKDF2_ITER 100000

/* ---- Base64 helpers ------------------------------------------------------- */

/* Returns the number of bytes needed to base64-encode `src_len` bytes
 * (including the null terminator). */
static size_t b64_enc_size(size_t src_len)
{
    return ((src_len + 2u) / 3u) * 4u + 1u;
}

/* Returns the maximum number of decoded bytes for a base64 string of length
 * `b64_len` (may over-estimate by up to 2). */
static size_t b64_dec_max(size_t b64_len)
{
    return (b64_len / 4u) * 3u + 3u;
}

/* Base64-encode src[src_len] into dst (must be >= b64_enc_size(src_len) bytes).
 * dst is null-terminated. Returns number of base64 chars written (excl. NUL). */
static size_t b64_encode(const unsigned char *src, size_t src_len,
                          char *dst, size_t dst_size)
{
    int len = EVP_EncodeBlock((unsigned char *)dst, src, (int)src_len);
    if (len < 0 || (size_t)len >= dst_size) {
        dst[0] = '\0';
        return 0u;
    }
    dst[len] = '\0';
    return (size_t)len;
}

/* Base64-decode src into dst. Returns decoded byte count, or 0 on error.
 * EVP_DecodeBlock may write up to 2 padding bytes; caller checks actual length
 * separately via the prefix structure (nonce_len + payload_len + tag_len). */
static size_t b64_decode(const char *src, size_t src_len,
                          unsigned char *dst, size_t dst_size)
{
    if (dst_size < b64_dec_max(src_len)) return 0u;
    int len = EVP_DecodeBlock(dst, (const unsigned char *)src, (int)src_len);
    if (len < 0) return 0u;
    /* Adjust for padding '=' characters */
    if (src_len >= 2u && src[src_len - 1u] == '=') len--;
    if (src_len >= 2u && src[src_len - 2u] == '=') len--;
    return (size_t)len;
}

/* ---- Key derivation ------------------------------------------------------- */

int crypto_derive_key(unsigned char key[32])
{
    if (!key) return CRYPTO_ERR_ARGS;

    char material[512];
    material[0] = '\0';

#ifdef _WIN32
    /* Read MachineGuid from the registry */
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        DWORD type = REG_SZ;
        DWORD sz = (DWORD)(sizeof(material) - 1u);
        RegQueryValueExA(hKey, "MachineGuid", NULL, &type,
                         (BYTE *)material, &sz);
        RegCloseKey(hKey);
    }
#else
    gethostname(material, sizeof(material) - 1u);
    material[sizeof(material) - 1u] = '\0';
#endif

    if (material[0] == '\0') return CRYPTO_ERR_KEY;

    int rc = PKCS5_PBKDF2_HMAC(material, (int)strlen(material),
                                 (const unsigned char *)PBKDF2_SALT,
                                 (int)(sizeof(PBKDF2_SALT) - 1u),
                                 PBKDF2_ITER, EVP_sha256(),
                                 KEY_LEN, key);
    return (rc == 1) ? CRYPTO_OK : CRYPTO_ERR_KEY;
}

/* ---- Encrypt ------------------------------------------------------------- */

int crypto_encrypt_with_key(const unsigned char key[32], const char *plaintext,
                             char *out, size_t out_size)
{
    if (!key || !plaintext || !out || out_size == 0u) return CRYPTO_ERR_ARGS;

    size_t pt_len = strlen(plaintext);

    /* Verify output buffer is large enough */
    size_t raw_len   = NONCE_LEN + pt_len + TAG_LEN;
    size_t b64_len   = b64_enc_size(raw_len);
    size_t prefix_len = strlen(CRYPTO_ENC_PREFIX);
    if (out_size < prefix_len + b64_len) return CRYPTO_ERR_BUFSIZE;

    /* Generate random nonce */
    unsigned char nonce[NONCE_LEN];
    if (RAND_bytes(nonce, NONCE_LEN) != 1) return CRYPTO_ERR_RAND;

    /* AES-256-GCM encrypt */
    unsigned char *raw = NULL;
    unsigned char *ciphertext = NULL;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int ok = 0;
    if (!ctx) return CRYPTO_ERR_ENCRYPT;

    /* raw = nonce || ciphertext || tag */
    raw = (unsigned char *)malloc(raw_len);
    if (!raw) { EVP_CIPHER_CTX_free(ctx); return CRYPTO_ERR_ENCRYPT; }
    ciphertext = raw + NONCE_LEN;

    memcpy(raw, nonce, NONCE_LEN);

    int ct_len = 0, final_len = 0;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_LEN, NULL) != 1) goto done;
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) goto done;
    if (EVP_EncryptUpdate(ctx, ciphertext, &ct_len,
                          (const unsigned char *)plaintext, (int)pt_len) != 1) goto done;
    if (EVP_EncryptFinal_ex(ctx, ciphertext + ct_len, &final_len) != 1) goto done;
    ct_len += final_len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN,
                             raw + NONCE_LEN + (size_t)ct_len) != 1) goto done;
    ok = 1;

done:
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) { free(raw); return CRYPTO_ERR_ENCRYPT; }

    /* Assemble output: prefix + base64(nonce||ciphertext||tag) */
    memcpy(out, CRYPTO_ENC_PREFIX, prefix_len);
    size_t b64_written = b64_encode(raw, NONCE_LEN + (size_t)ct_len + TAG_LEN,
                                     out + prefix_len, out_size - prefix_len);
    free(raw);
    if (b64_written == 0u) return CRYPTO_ERR_B64;
    return CRYPTO_OK;
}

/* ---- Decrypt ------------------------------------------------------------- */

int crypto_decrypt_with_key(const unsigned char key[32], const char *blob,
                             char *out, size_t out_size)
{
    if (!key || !blob || !out || out_size == 0u) return CRYPTO_ERR_ARGS;

    size_t prefix_len = strlen(CRYPTO_ENC_PREFIX);
    if (strncmp(blob, CRYPTO_ENC_PREFIX, prefix_len) != 0) return CRYPTO_ERR_ARGS;

    const char *b64 = blob + prefix_len;
    size_t b64_len  = strlen(b64);
    if (b64_len == 0u) return CRYPTO_ERR_B64;

    size_t raw_max = b64_dec_max(b64_len);
    unsigned char *raw = (unsigned char *)malloc(raw_max);
    if (!raw) return CRYPTO_ERR_DECRYPT;

    size_t raw_len = b64_decode(b64, b64_len, raw, raw_max);
    if (raw_len < (size_t)(NONCE_LEN + TAG_LEN)) { free(raw); return CRYPTO_ERR_B64; }

    size_t ct_len  = raw_len - NONCE_LEN - TAG_LEN;
    unsigned char *nonce      = raw;
    unsigned char *ciphertext = raw + NONCE_LEN;
    unsigned char *tag        = raw + NONCE_LEN + ct_len;

    if (out_size < ct_len + 1u) { free(raw); return CRYPTO_ERR_BUFSIZE; }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int ok = 0;
    int pt_len = 0, final_len = 0;
    if (!ctx) { free(raw); return CRYPTO_ERR_DECRYPT; }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_LEN, NULL) != 1) goto done;
    if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) goto done;
    if (EVP_DecryptUpdate(ctx, (unsigned char *)out, &pt_len,
                          ciphertext, (int)ct_len) != 1) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN, tag) != 1) goto done;
    if (EVP_DecryptFinal_ex(ctx, (unsigned char *)out + pt_len, &final_len) != 1) goto done;
    pt_len += final_len;
    out[pt_len] = '\0';
    ok = 1;

done:
    EVP_CIPHER_CTX_free(ctx);
    free(raw);
    if (!ok) {
        memset(out, 0, out_size);
        return CRYPTO_ERR_DECRYPT;
    }
    return CRYPTO_OK;
}

/* ---- High-level wrappers ------------------------------------------------- */

int crypto_encrypt(const char *plaintext, char *out, size_t out_size)
{
    unsigned char key[KEY_LEN];
    int rc = crypto_derive_key(key);
    if (rc != CRYPTO_OK) return rc;
    rc = crypto_encrypt_with_key(key, plaintext, out, out_size);
    memset(key, 0, sizeof(key));
    return rc;
}

int crypto_decrypt(const char *blob, char *out, size_t out_size)
{
    unsigned char key[KEY_LEN];
    int rc = crypto_derive_key(key);
    if (rc != CRYPTO_OK) return rc;
    rc = crypto_decrypt_with_key(key, blob, out, out_size);
    memset(key, 0, sizeof(key));
    return rc;
}

/* ---- Predicate ----------------------------------------------------------- */

bool crypto_is_encrypted(const char *s)
{
    if (!s) return false;
    return strncmp(s, CRYPTO_ENC_PREFIX, strlen(CRYPTO_ENC_PREFIX)) == 0;
}
