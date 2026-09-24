#ifndef NUTSHELL_CRYPTO_H
#define NUTSHELL_CRYPTO_H

#include <stddef.h>
#include <stdbool.h>

/* Prefix that identifies a legacy (MachineGuid-derived key) encrypted blob
 * in the JSON config. Kept for migration only -- new writes never use it. */
#define CRYPTO_ENC_PREFIX "$aes256gcm$v1$"

/* Prefix that identifies a DPAPI-protected blob (current format). */
#define CRYPTO_DPAPI_PREFIX "$dpapi$v1$"

/* Return codes */
#define CRYPTO_OK          0
#define CRYPTO_ERR_ARGS   -1
#define CRYPTO_ERR_RAND   -2
#define CRYPTO_ERR_ENCRYPT -3
#define CRYPTO_ERR_DECRYPT -4  /* includes GCM tag mismatch */
#define CRYPTO_ERR_B64    -5
#define CRYPTO_ERR_BUFSIZE -6
#define CRYPTO_ERR_KEY    -7

/*
 * crypto_derive_key — derive a 32-byte machine-specific key.
 *   Windows: reads HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Cryptography\MachineGuid
 *            and runs PBKDF2-SHA256 with a fixed application salt.
 *   Other:   uses gethostname() as the key material.
 * Returns CRYPTO_OK or a negative error code.
 */
int crypto_derive_key(unsigned char key[32]);

/*
 * crypto_encrypt_with_key — encrypt plaintext with a caller-supplied 32-byte key.
 * Output format (written to *out): CRYPTO_ENC_PREFIX + base64(nonce||ciphertext||tag)
 *   nonce = 12 bytes, tag = 16 bytes.
 * Returns CRYPTO_OK on success, negative on error.
 */
int crypto_encrypt_with_key(const unsigned char key[32], const char *plaintext,
                             char *out, size_t out_size);

/*
 * crypto_decrypt_with_key — decrypt a blob produced by crypto_encrypt_with_key.
 * Returns CRYPTO_OK on success, negative on error (including tag mismatch).
 */
int crypto_decrypt_with_key(const unsigned char key[32], const char *blob,
                             char *out, size_t out_size);

/*
 * High-level wrappers that derive the machine key internally.
 */
int crypto_encrypt(const char *plaintext, char *out, size_t out_size);
int crypto_decrypt(const char *blob, char *out, size_t out_size);

/*
 * crypto_is_encrypted — returns true if s starts with CRYPTO_ENC_PREFIX
 * (the legacy MachineGuid-derived format only).
 */
bool crypto_is_encrypted(const char *s);

/*
 * crypto_is_dpapi — returns true if s starts with CRYPTO_DPAPI_PREFIX.
 */
bool crypto_is_dpapi(const char *s);

/*
 * crypto_is_any_encrypted — true for either the legacy or DPAPI prefix.
 */
bool crypto_is_any_encrypted(const char *s);

/*
 * crypto_encrypt_dpapi / crypto_decrypt_dpapi — encrypt/decrypt a secret
 * for the current Windows user via the active DPAPI backend (see
 * crypto_dpapi.h). Output format: CRYPTO_DPAPI_PREFIX + base64(DPAPI blob).
 *
 * crypto_decrypt_dpapi fails (CRYPTO_ERR_DECRYPT or CRYPTO_ERR_KEY) for a
 * blob written by a different user or machine, or one that is corrupt --
 * by design, since that is exactly what DPAPI enforces. Callers that want
 * to preserve such a blob for round-tripping (rather than losing it) must
 * keep the original string themselves; this function only reports failure.
 */
int crypto_encrypt_dpapi(const char *plaintext, char *out, size_t out_size);
int crypto_decrypt_dpapi(const char *blob, char *out, size_t out_size);

#endif /* NUTSHELL_CRYPTO_H */
