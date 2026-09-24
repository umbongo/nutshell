#include "test_framework.h"
#include "crypto.h"
#include "crypto_dpapi.h"
#include "fake_dpapi.h"
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

int test_crypto_decrypt_empty_payload(void)
{
    TEST_BEGIN();
    char dec[256];
    /* Prefix with empty or minimal base64 — too short for nonce+tag */
    int rc = crypto_decrypt_with_key(TEST_KEY, CRYPTO_ENC_PREFIX, dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);
    rc = crypto_decrypt_with_key(TEST_KEY, CRYPTO_ENC_PREFIX "AA==", dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);
    TEST_END();
}

int test_crypto_decrypt_invalid_base64(void)
{
    TEST_BEGIN();
    char dec[256];
    /* Prefix with invalid base64 characters */
    int rc = crypto_decrypt_with_key(TEST_KEY, CRYPTO_ENC_PREFIX "!!!not-base64!!!", dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);
    TEST_END();
}

/* ============================================================
 * DPAPI-backed secrets (crypto_encrypt_dpapi / crypto_decrypt_dpapi)
 *
 * All of these run against the fake backend (tests/fake_dpapi.h) that
 * tests/runner.c installs at startup -- real DPAPI is per-machine state a
 * test binary cannot rely on, and does not exist at all on the Linux host
 * that runs `make test` in CI. Tests that change the fake identity restore
 * it afterward so later tests keep seeing the default identity.
 * ============================================================ */

int test_crypto_dpapi_roundtrip_basic(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF], dec[256];

    ASSERT_EQ(crypto_encrypt_dpapi("hunter2", enc, sizeof(enc)), CRYPTO_OK);
    ASSERT_TRUE(crypto_is_dpapi(enc));
    ASSERT_TRUE(strstr(enc, "hunter2") == NULL); /* not plaintext in output */

    ASSERT_EQ(crypto_decrypt_dpapi(enc, dec, sizeof(dec)), CRYPTO_OK);
    ASSERT_STR_EQ(dec, "hunter2");
    TEST_END();
}

int test_crypto_dpapi_roundtrip_empty(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF], dec[256];

    ASSERT_EQ(crypto_encrypt_dpapi("", enc, sizeof(enc)), CRYPTO_OK);
    ASSERT_TRUE(enc[0] != '\0');

    ASSERT_EQ(crypto_decrypt_dpapi(enc, dec, sizeof(dec)), CRYPTO_OK);
    ASSERT_TRUE(dec[0] == '\0');
    TEST_END();
}

int test_crypto_dpapi_roundtrip_long(void)
{
    TEST_BEGIN();
    char pt[256];
    memset(pt, 'Z', sizeof(pt) - 1u);
    pt[sizeof(pt) - 1u] = '\0';

    char enc[ENC_BUF], dec[256];
    ASSERT_EQ(crypto_encrypt_dpapi(pt, enc, sizeof(enc)), CRYPTO_OK);
    ASSERT_EQ(crypto_decrypt_dpapi(enc, dec, sizeof(dec)), CRYPTO_OK);
    ASSERT_STR_EQ(dec, pt);
    TEST_END();
}

int test_crypto_dpapi_prefix_differs_from_legacy(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF];
    ASSERT_EQ(crypto_encrypt_dpapi("pw", enc, sizeof(enc)), CRYPTO_OK);
    ASSERT_TRUE(strncmp(enc, CRYPTO_DPAPI_PREFIX, strlen(CRYPTO_DPAPI_PREFIX)) == 0);
    ASSERT_TRUE(!crypto_is_encrypted(enc));   /* not the legacy prefix */
    ASSERT_TRUE(crypto_is_any_encrypted(enc));
    TEST_END();
}

int test_crypto_is_dpapi_yes_no(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF];
    ASSERT_EQ(crypto_encrypt_dpapi("pw", enc, sizeof(enc)), CRYPTO_OK);
    ASSERT_TRUE(crypto_is_dpapi(enc));
    ASSERT_TRUE(!crypto_is_dpapi("plaintext"));
    ASSERT_TRUE(!crypto_is_dpapi(""));
    ASSERT_TRUE(!crypto_is_dpapi(NULL));
    ASSERT_TRUE(!crypto_is_dpapi(CRYPTO_ENC_PREFIX "abc")); /* legacy, not dpapi */
    TEST_END();
}

int test_crypto_is_any_encrypted(void)
{
    TEST_BEGIN();
    char legacy_enc[ENC_BUF], dpapi_enc[ENC_BUF];
    ASSERT_EQ(crypto_encrypt_with_key(TEST_KEY, "pw", legacy_enc, sizeof(legacy_enc)), CRYPTO_OK);
    ASSERT_EQ(crypto_encrypt_dpapi("pw", dpapi_enc, sizeof(dpapi_enc)), CRYPTO_OK);

    ASSERT_TRUE(crypto_is_any_encrypted(legacy_enc));
    ASSERT_TRUE(crypto_is_any_encrypted(dpapi_enc));
    ASSERT_TRUE(!crypto_is_any_encrypted("plaintext"));
    ASSERT_TRUE(!crypto_is_any_encrypted(""));
    ASSERT_TRUE(!crypto_is_any_encrypted(NULL));
    TEST_END();
}

int test_crypto_dpapi_foreign_blob_fails_and_stays_empty(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF];
    char dec[256];

    fake_dpapi_set_identity(0x11);
    ASSERT_EQ(crypto_encrypt_dpapi("hunter2", enc, sizeof(enc)), CRYPTO_OK);

    /* Simulate the config file moving to another PC/user: the identity the
     * blob was protected under is no longer the active one. */
    fake_dpapi_set_identity(0x22);
    int rc = crypto_decrypt_dpapi(enc, dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);

    fake_dpapi_set_identity(0x42); /* restore default for later tests */
    TEST_END();
}

int test_crypto_dpapi_corrupt_blob_fails(void)
{
    TEST_BEGIN();
    char enc[ENC_BUF], dec[256];
    ASSERT_EQ(crypto_encrypt_dpapi("hunter2", enc, sizeof(enc)), CRYPTO_OK);

    /* Flip a byte in the middle of the base64 payload. */
    size_t prefix_len = strlen(CRYPTO_DPAPI_PREFIX);
    size_t payload_pos = prefix_len + (strlen(enc) - prefix_len) / 2u;
    enc[payload_pos] ^= 0x01;

    int rc = crypto_decrypt_dpapi(enc, dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);
    TEST_END();
}

int test_crypto_dpapi_truncated_blob_does_not_crash(void)
{
    TEST_BEGIN();
    char dec[256];
    int rc = crypto_decrypt_dpapi(CRYPTO_DPAPI_PREFIX, dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);
    rc = crypto_decrypt_dpapi(CRYPTO_DPAPI_PREFIX "!!!not-base64!!!", dec, sizeof(dec));
    ASSERT_TRUE(rc != CRYPTO_OK);
    TEST_END();
}

int test_crypto_dpapi_null_inputs(void)
{
    TEST_BEGIN();
    char buf[ENC_BUF];
    ASSERT_TRUE(crypto_encrypt_dpapi(NULL, buf, sizeof(buf)) != CRYPTO_OK);
    ASSERT_TRUE(crypto_encrypt_dpapi("pw", NULL, sizeof(buf)) != CRYPTO_OK);
    ASSERT_TRUE(crypto_decrypt_dpapi(NULL, buf, sizeof(buf)) != CRYPTO_OK);
    ASSERT_TRUE(crypto_decrypt_dpapi("$dpapi$v1$abc", NULL, sizeof(buf)) != CRYPTO_OK);
    TEST_END();
}

int test_crypto_dpapi_output_too_small(void)
{
    TEST_BEGIN();
    char tiny[4];
    int rc = crypto_encrypt_dpapi("password", tiny, sizeof(tiny));
    ASSERT_TRUE(rc == CRYPTO_ERR_BUFSIZE);
    TEST_END();
}

int test_crypto_dpapi_backend_get_set(void)
{
    TEST_BEGIN();
    const CryptoDpapiBackend *saved = crypto_dpapi_backend();
    ASSERT_NOT_NULL(saved);

    crypto_dpapi_set_backend(NULL);
    ASSERT_TRUE(crypto_dpapi_backend() == crypto_dpapi_default_backend());

    crypto_dpapi_set_backend(&k_fake_dpapi_backend);
    ASSERT_TRUE(crypto_dpapi_backend() == &k_fake_dpapi_backend);
    TEST_END();
}
