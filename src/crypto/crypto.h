#ifndef CONGA_CRYPTO_H
#define CONGA_CRYPTO_H

#include <stddef.h>
#include <stdbool.h>

/* Prefix that identifies an encrypted password blob in the JSON config. */
#define CRYPTO_ENC_PREFIX "$aes256gcm$v1$"

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
 * crypto_is_encrypted — returns true if s starts with CRYPTO_ENC_PREFIX.
 */
bool crypto_is_encrypted(const char *s);

#endif /* CONGA_CRYPTO_H */
