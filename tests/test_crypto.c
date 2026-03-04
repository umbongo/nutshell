#include "test_framework.h"
#include "crypto.h"
#include <string.h>
#include <stdlib.h>

/* Fixed 32-byte test key — never use derive_key in unit tests so results are
 * deterministic and machine-independent. */
static const unsigned char TEST_KEY[32] = {
    0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
    0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
    0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00
};

/* Enough room for any encrypted password: prefix(14) + base64(12+255+16) ≈ 420 */
#define ENC_BUF 512

/* ---- Positive tests ------------------------------------------------------- */

int test_crypto_roundtrip_basic(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF];
    char dec[256];

    int rc = crypto_encrypt_with_key(TEST_KEY, "hunter2", enc, sizeof(enc));
    ASSERT_EQ(rc, CRYPTO_OK);
    ASSERT_TRUE(crypto_is_encrypted(enc));
    ASSERT_TRUE(strstr(enc, "hunter2") == NULL); /* not plaintext in output */

    rc = crypto_decrypt_with_key(TEST_KEY, enc, dec, sizeof(dec));
    ASSERT_EQ(rc, CRYPTO_OK);
    ASSERT_TRUE(strcmp(dec, "hunter2") == 0);
    TEST_END();
}

int test_crypto_roundtrip_empty(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF];
    char dec[256];

    int rc = crypto_encrypt_with_key(TEST_KEY, "", enc, sizeof(enc));
    ASSERT_EQ(rc, CRYPTO_OK);
    ASSERT_TRUE(enc[0] != '\0'); /* non-empty even for empty plaintext */

    rc = crypto_decrypt_with_key(TEST_KEY, enc, dec, sizeof(dec));
    ASSERT_EQ(rc, CRYPTO_OK);
    ASSERT_TRUE(dec[0] == '\0');
    TEST_END();
}

int test_crypto_roundtrip_single_char(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF], dec[256];
    ASSERT_EQ(crypto_encrypt_with_key(TEST_KEY, "x", enc, sizeof(enc)), CRYPTO_OK);
    ASSERT_EQ(crypto_decrypt_with_key(TEST_KEY, enc, dec, sizeof(dec)), CRYPTO_OK);
    ASSERT_TRUE(strcmp(dec, "x") == 0);
    TEST_END();
}

int test_crypto_roundtrip_long(void)
{
    TEST_BEGIN();
    /* 128-char password */
    char pt[129];
    memset(pt, 'A', 128);
    pt[128] = '\0';

    char enc[ENC_BUF], dec[256];
    ASSERT_EQ(crypto_encrypt_with_key(TEST_KEY, pt, enc, sizeof(enc)), CRYPTO_OK);
    ASSERT_EQ(crypto_decrypt_with_key(TEST_KEY, enc, dec, sizeof(dec)), CRYPTO_OK);
    ASSERT_TRUE(strcmp(dec, pt) == 0);
    TEST_END();
}

int test_crypto_roundtrip_special_chars(void)
{
    TEST_BEGIN();
    const char *pt = "p@$$w0rd!#%^&*()_+-=[]{}|;':\",./<>?";
    char enc[ENC_BUF], dec[256];
    ASSERT_EQ(crypto_encrypt_with_key(TEST_KEY, pt, enc, sizeof(enc)), CRYPTO_OK);
    ASSERT_EQ(crypto_decrypt_with_key(TEST_KEY, enc, dec, sizeof(dec)), CRYPTO_OK);
    ASSERT_TRUE(strcmp(dec, pt) == 0);
    TEST_END();
}

int test_crypto_nonce_unique(void)
{
    TEST_BEGIN();
    /* Encrypt same plaintext 20 times — blobs must all differ (unique nonces) */
    char enc[20][ENC_BUF];
    for (int i = 0; i < 20; i++) {
        ASSERT_EQ(crypto_encrypt_with_key(TEST_KEY, "password", enc[i], ENC_BUF), CRYPTO_OK);
    }
    int all_unique = 1;
    for (int i = 0; i < 20 && all_unique; i++) {
        for (int j = i + 1; j < 20 && all_unique; j++) {
            if (strcmp(enc[i], enc[j]) == 0) all_unique = 0;
        }
    }
    ASSERT_TRUE(all_unique);
    TEST_END();
}

int test_crypto_is_encrypted_yes(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF];
    ASSERT_EQ(crypto_encrypt_with_key(TEST_KEY, "pw", enc, sizeof(enc)), CRYPTO_OK);
    ASSERT_TRUE(crypto_is_encrypted(enc));
    TEST_END();
}

int test_crypto_is_encrypted_no(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(!crypto_is_encrypted("plaintext_password"));
    ASSERT_TRUE(!crypto_is_encrypted(""));
    ASSERT_TRUE(!crypto_is_encrypted(NULL));
    TEST_END();
}

/* ---- Negative tests ------------------------------------------------------- */

int test_crypto_wrong_key(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF], dec[256];
    ASSERT_EQ(crypto_encrypt_with_key(TEST_KEY, "secret", enc, sizeof(enc)), CRYPTO_OK);

    unsigned char bad_key[32];
    memset(bad_key, 0, sizeof(bad_key));
    int rc = crypto_decrypt_with_key(bad_key, enc, dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK); /* GCM tag mismatch */
    TEST_END();
}

int test_crypto_tampered_ciphertext(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF], dec[256];
    ASSERT_EQ(crypto_encrypt_with_key(TEST_KEY, "secret", enc, sizeof(enc)), CRYPTO_OK);

    /* Flip a byte in the middle of the base64 payload */
    size_t prefix_len = strlen(CRYPTO_ENC_PREFIX);
    size_t payload_pos = prefix_len + (strlen(enc) - prefix_len) / 2u;
    enc[payload_pos] ^= 0x01;

    int rc = crypto_decrypt_with_key(TEST_KEY, enc, dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);
    TEST_END();
}

int test_crypto_truncated_blob(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF], dec[256];
    ASSERT_EQ(crypto_encrypt_with_key(TEST_KEY, "secret", enc, sizeof(enc)), CRYPTO_OK);

    /* Strip last 4 bytes of the base64 */
    size_t len = strlen(enc);
    if (len > 4u) enc[len - 4u] = '\0';

    int rc = crypto_decrypt_with_key(TEST_KEY, enc, dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);
    TEST_END();
}

int test_crypto_null_inputs(void)
{
    TEST_BEGIN();
    char buf[ENC_BUF];
    ASSERT_TRUE(crypto_encrypt_with_key(NULL, "pw", buf, sizeof(buf)) != CRYPTO_OK);
    ASSERT_TRUE(crypto_encrypt_with_key(TEST_KEY, NULL, buf, sizeof(buf)) != CRYPTO_OK);
    ASSERT_TRUE(crypto_encrypt_with_key(TEST_KEY, "pw", NULL, sizeof(buf)) != CRYPTO_OK);
    ASSERT_TRUE(crypto_decrypt_with_key(NULL, "$aes256gcm$v1$abc", buf, sizeof(buf)) != CRYPTO_OK);
    ASSERT_TRUE(crypto_decrypt_with_key(TEST_KEY, NULL, buf, sizeof(buf)) != CRYPTO_OK);
    ASSERT_TRUE(crypto_decrypt_with_key(TEST_KEY, "$aes256gcm$v1$abc", NULL, sizeof(buf)) != CRYPTO_OK);
    TEST_END();
}

int test_crypto_output_too_small(void)
{
    TEST_BEGIN();
    char tiny[4];
    int rc = crypto_encrypt_with_key(TEST_KEY, "password", tiny, sizeof(tiny));
    ASSERT_TRUE(rc == CRYPTO_ERR_BUFSIZE);
    TEST_END();
}

int test_crypto_decrypt_not_encrypted(void)
{
    TEST_BEGIN();
    char dec[256];
    /* Blob without the prefix should fail */
    int rc = crypto_decrypt_with_key(TEST_KEY, "plaintext", dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);
    TEST_END();
}
