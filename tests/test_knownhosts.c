#ifndef NO_SSH_LIBS

#include "test_framework.h"
#include "knownhosts.h"
#include "ssh_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define UNLINK(p) DeleteFileA(p)
#else
#include <unistd.h>
#define UNLINK(p) unlink(p)
#endif

/* A minimal real RSA host key (SSH wire format, 279 bytes).
 * This is a test key only — not used for any real connection. */
static const unsigned char TEST_KEY_A[] = {
    /* "ssh-rsa" type prefix + minimal RSA key material */
    0x00,0x00,0x00,0x07,'s','s','h','-','r','s','a',
    0x00,0x00,0x00,0x03,0x01,0x00,0x01, /* exponent = 65537 */
    /* 256-byte (2048-bit) modulus filled with a test pattern */
    0x00,0x00,0x01,0x01, /* length = 257 (leading 0x00 sign byte) */
    0x00, /* sign byte */
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,
    0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89
};
static const size_t TEST_KEY_A_LEN = sizeof(TEST_KEY_A);

/* A different key — same structure but different modulus bytes */
static unsigned char TEST_KEY_B[sizeof(TEST_KEY_A)];
static const size_t  TEST_KEY_B_LEN = sizeof(TEST_KEY_B);

static void init_key_b(void)
{
    memcpy(TEST_KEY_B, TEST_KEY_A, sizeof(TEST_KEY_A));
    /* Flip bytes in the modulus section */
    TEST_KEY_B[20] = 0xDE;
    TEST_KEY_B[21] = 0xAD;
    TEST_KEY_B[22] = 0xBE;
    TEST_KEY_B[23] = 0xEF;
}

/* Helper: create a temp file path */
static void tmp_path(char *buf, size_t n, const char *name)
{
#ifdef _WIN32
    char tmp[MAX_PATH];
    GetTempPathA((DWORD)n, tmp);
    snprintf(buf, n, "%s%s", tmp, name);
#else
    snprintf(buf, n, "/tmp/%s", name);
#endif
}

/* Helper: set up a fresh KnownHosts at a temp path using a real ssh session */
static SshSession *g_sess = NULL;

static int setup_kh(KnownHosts *kh, const char *fname)
{
    if (!g_sess) {
        g_sess = ssh_session_new();
        if (!g_sess) return -1;
    }
    char path[256];
    tmp_path(path, sizeof(path), fname);
    UNLINK(path); /* start fresh */
    return knownhosts_init(kh, g_sess->session, path);
}

/* ---- Positive tests ------------------------------------------------------- */

int test_kh_new_host_returns_new(void)
{
    TEST_BEGIN();
    KnownHosts kh;
    ASSERT_EQ(setup_kh(&kh, "nutshell_kh_test1.txt"), KNOWNHOSTS_OK);

    int rc = knownhosts_check(&kh, "newhost.example.com", 22,
                               (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                               NULL, 0);
    ASSERT_EQ(rc, KNOWNHOSTS_NEW);
    knownhosts_free(&kh);
    TEST_END();
}

int test_kh_add_then_check_ok(void)
{
    TEST_BEGIN();
    KnownHosts kh;
    ASSERT_EQ(setup_kh(&kh, "nutshell_kh_test2.txt"), KNOWNHOSTS_OK);

    ASSERT_EQ(knownhosts_add(&kh, "myhost.example.com", 22,
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN),
              KNOWNHOSTS_OK);
    int rc = knownhosts_check(&kh, "myhost.example.com", 22,
                               (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                               NULL, 0);
    ASSERT_EQ(rc, KNOWNHOSTS_OK);
    knownhosts_free(&kh);
    TEST_END();
}

int test_kh_mismatch(void)
{
    TEST_BEGIN();
    init_key_b();
    KnownHosts kh;
    ASSERT_EQ(setup_kh(&kh, "nutshell_kh_test3.txt"), KNOWNHOSTS_OK);

    ASSERT_EQ(knownhosts_add(&kh, "mismatch.example.com", 22,
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN),
              KNOWNHOSTS_OK);
    int rc = knownhosts_check(&kh, "mismatch.example.com", 22,
                               (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                               NULL, 0);
    ASSERT_EQ(rc, KNOWNHOSTS_MISMATCH);
    knownhosts_free(&kh);
    TEST_END();
}

int test_kh_fingerprint_populated(void)
{
    TEST_BEGIN();
    KnownHosts kh;
    ASSERT_EQ(setup_kh(&kh, "nutshell_kh_test4.txt"), KNOWNHOSTS_OK);

    char fp[128];
    fp[0] = '\0';
    knownhosts_check(&kh, "fp.example.com", 22,
                     (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                     fp, sizeof(fp));
    ASSERT_TRUE(strncmp(fp, "SHA256:", 7) == 0);
    ASSERT_TRUE(strlen(fp) > 7u);
    knownhosts_free(&kh);
    TEST_END();
}

int test_kh_file_created_on_add(void)
{
    TEST_BEGIN();
    char path[256];
    tmp_path(path, sizeof(path), "nutshell_kh_test5.txt");
    UNLINK(path);

    KnownHosts kh;
    ASSERT_EQ(knownhosts_init(&kh, g_sess->session, path), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_add(&kh, "filetest.example.com", 22,
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN),
              KNOWNHOSTS_OK);

    FILE *f = fopen(path, "r");
    ASSERT_TRUE(f != NULL);
    if (f) fclose(f);
    knownhosts_free(&kh);
    TEST_END();
}

int test_kh_multiple_hosts(void)
{
    TEST_BEGIN();
    init_key_b();
    KnownHosts kh;
    ASSERT_EQ(setup_kh(&kh, "nutshell_kh_test6.txt"), KNOWNHOSTS_OK);

    ASSERT_EQ(knownhosts_add(&kh, "host1.example.com", 22,
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_add(&kh, "host2.example.com", 22,
                              (const char *)TEST_KEY_B, TEST_KEY_B_LEN), KNOWNHOSTS_OK);

    ASSERT_EQ(knownhosts_check(&kh, "host1.example.com", 22,
                                (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_check(&kh, "host2.example.com", 22,
                                (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    /* Cross-check: host1's key should mismatch for host2 */
    int rc = knownhosts_check(&kh, "host1.example.com", 22,
                               (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                               NULL, 0);
    ASSERT_EQ(rc, KNOWNHOSTS_MISMATCH);
    knownhosts_free(&kh);
    TEST_END();
}

/* ---- Negative tests ------------------------------------------------------- */

int test_kh_null_inputs(void)
{
    TEST_BEGIN();
    KnownHosts kh;
    memset(&kh, 0, sizeof(kh));

    ASSERT_TRUE(knownhosts_init(NULL, g_sess->session, "/tmp/x") == KNOWNHOSTS_ERROR);
    ASSERT_TRUE(knownhosts_init(&kh, NULL, "/tmp/x") == KNOWNHOSTS_ERROR);
    ASSERT_TRUE(knownhosts_init(&kh, g_sess->session, NULL) == KNOWNHOSTS_ERROR);

    /* check/add with NULL store should not crash */
    ASSERT_TRUE(knownhosts_check(NULL, "h", 22,
                                  (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                                  NULL, 0) == KNOWNHOSTS_ERROR);
    ASSERT_TRUE(knownhosts_add(NULL, "h", 22,
                                (const char *)TEST_KEY_A, TEST_KEY_A_LEN)
                == KNOWNHOSTS_ERROR);

    knownhosts_free(NULL); /* must not crash */
    TEST_END();
}

int test_kh_key_rotation(void)
{
    TEST_BEGIN();
    init_key_b();
    KnownHosts kh;
    ASSERT_EQ(setup_kh(&kh, "nutshell_kh_test7.txt"), KNOWNHOSTS_OK);

    /* Add key A, then update to key B */
    ASSERT_EQ(knownhosts_add(&kh, "rotate.example.com", 22,
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_add(&kh, "rotate.example.com", 22,
                              (const char *)TEST_KEY_B, TEST_KEY_B_LEN), KNOWNHOSTS_OK);

    /* Only key B should now match */
    ASSERT_EQ(knownhosts_check(&kh, "rotate.example.com", 22,
                                (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    knownhosts_free(&kh);
    TEST_END();
}

#endif /* NO_SSH_LIBS */
