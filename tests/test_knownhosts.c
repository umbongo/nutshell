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
#define MKDIR(p)  CreateDirectoryA((p), NULL)
#define RMDIR(p)  RemoveDirectoryA(p)
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#define UNLINK(p) unlink(p)
#define MKDIR(p)  mkdir((p), 0700)
#define RMDIR(p)  rmdir(p)
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

/* Helper: read a whole file into a freshly malloc'd, NUL-terminated buffer.
 * Returns the byte length (not counting the added NUL) or -1 on failure,
 * leaving *out_buf NULL. Caller frees *out_buf. */
static long read_file_bytes(const char *path, unsigned char **out_buf)
{
    *out_buf = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }

    unsigned char *buf = (unsigned char *)malloc((size_t)len + 1u);
    if (!buf) { fclose(f); return -1; }
    if (len > 0) {
        size_t rd = fread(buf, 1, (size_t)len, f);
        if (rd != (size_t)len) { fclose(f); free(buf); return -1; }
    }
    buf[len] = '\0';
    fclose(f);
    *out_buf = buf;
    return len;
}

/* Helper: count files next to base_path whose name is "<base_path>.<...>.tmp"
 * -- the atomic writer's temp-file pattern. Used to check no leftover temp
 * file survives a failed knownhosts_add(). */
static int count_matching_tmp_files(const char *base_path)
{
    int count = 0;
#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s.*.tmp", base_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { count++; } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    const char *slash = strrchr(base_path, '/');
    char dir[512];
    const char *fname;
    if (slash) {
        size_t dlen = (size_t)(slash - base_path);
        if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1u;
        memcpy(dir, base_path, dlen);
        dir[dlen] = '\0';
        fname = slash + 1;
    } else {
        snprintf(dir, sizeof(dir), ".");
        fname = base_path;
    }
    size_t fname_len = strlen(fname);
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            size_t nlen = strlen(ent->d_name);
            if (nlen > fname_len + 4u &&
                strncmp(ent->d_name, fname, fname_len) == 0 &&
                strcmp(ent->d_name + nlen - 4u, ".tmp") == 0) {
                count++;
            }
        }
        closedir(d);
    }
#endif
    return count;
}

/* Helper: create a temp file path */
static void tmp_path(char *buf, size_t n, const char *name)
{
#ifdef _WIN32
    char tmp[MAX_PATH];
    GetTempPathA((DWORD)n, tmp);
    snprintf(buf, n, "%s%s", tmp, name);
#else
    snprintf(buf, n, TEST_TMP_DIR "/%s", name);
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
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA),
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
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA),
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
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA),
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
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_add(&kh, "host2.example.com", 22,
                              (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA), KNOWNHOSTS_OK);

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
                                (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA)
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
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_add(&kh, "rotate.example.com", 22,
                              (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA), KNOWNHOSTS_OK);

    /* Only key B should now match */
    ASSERT_EQ(knownhosts_check(&kh, "rotate.example.com", 22,
                                (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    knownhosts_free(&kh);
    TEST_END();
}

/* ---- Fail-closed init ------------------------------------------------------ */

int test_kh_missing_file_ok_and_new(void)
{
    TEST_BEGIN();
    char path[256];
    tmp_path(path, sizeof(path), "nutshell_kh_test_missing.txt");
    UNLINK(path);

    KnownHosts kh;
    ASSERT_EQ(knownhosts_init(&kh, g_sess->session, path), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_check(&kh, "missing.example.com", 22,
                                (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                                NULL, 0), KNOWNHOSTS_NEW);
    knownhosts_free(&kh);
    TEST_END();
}

int test_kh_directory_path_init_error(void)
{
    TEST_BEGIN();
    char path[256];
    tmp_path(path, sizeof(path), "nutshell_kh_test_dir");
    UNLINK(path);
    RMDIR(path);
    MKDIR(path);

    KnownHosts kh;
    ASSERT_EQ(knownhosts_init(&kh, g_sess->session, path), KNOWNHOSTS_ERROR);
    knownhosts_free(&kh);

    RMDIR(path);
    TEST_END();
}

int test_kh_garbled_file_init_error(void)
{
    TEST_BEGIN();
    char path[256];
    tmp_path(path, sizeof(path), "nutshell_kh_test_garbled.txt");
    UNLINK(path);

    /* libssh2_knownhost_readfile() tolerates a line with an unparseable
     * base64 key field (it just skips it) -- what reliably makes it report
     * a parse error is a line with too few whitespace-separated fields
     * (no key field at all), which is what "host ssh-rsa" is below. */
    FILE *f = test_fopen_private(path);
    ASSERT_NOT_NULL(f);
    if (f) {
        fputs("this is not a known hosts line\n", f);
        fputs("host ssh-rsa\n", f);
        fclose(f);
    }

    KnownHosts kh;
    ASSERT_EQ(knownhosts_init(&kh, g_sess->session, path), KNOWNHOSTS_ERROR);
    knownhosts_free(&kh);

    UNLINK(path);
    TEST_END();
}

/* ---- Atomic writer --------------------------------------------------------- */

int test_kh_add_atomic_faults(void)
{
    TEST_BEGIN();
    char path[256];
    tmp_path(path, sizeof(path), "nutshell_kh_test_atomic.txt");
    UNLINK(path);

    KnownHosts kh;
    ASSERT_EQ(knownhosts_init(&kh, g_sess->session, path), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_add(&kh, "atomic.example.com", 22,
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA), KNOWNHOSTS_OK);

    unsigned char *snapshot = NULL;
    long snap_len = read_file_bytes(path, &snapshot);
    ASSERT_TRUE(snap_len > 0);

    for (int step = 1; step <= 3; step++) {
        char host[64];
        snprintf(host, sizeof(host), "fault%d.example.com", step);

        knownhosts_test_inject_write_fault(step);
        int rc = knownhosts_add(&kh, host, 22,
                                (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                                LIBSSH2_HOSTKEY_TYPE_RSA);
        ASSERT_EQ(rc, KNOWNHOSTS_ERROR);

        unsigned char *after = NULL;
        long after_len = read_file_bytes(path, &after);
        ASSERT_EQ(after_len, snap_len);
        if (after && snapshot && after_len == snap_len) {
            ASSERT_TRUE(memcmp(after, snapshot, (size_t)snap_len) == 0);
        }
        free(after);

        ASSERT_EQ(count_matching_tmp_files(path), 0);
    }

    free(snapshot);
    knownhosts_free(&kh);
    TEST_END();
}

int test_kh_add_missing_dir_error(void)
{
    TEST_BEGIN();
    char base[256];
    tmp_path(base, sizeof(base), "nutshell_kh_nosuchdir");
    RMDIR(base); /* make sure it really doesn't exist */

    char path[300];
    snprintf(path, sizeof(path), "%s/known_hosts", base);

    KnownHosts kh;
    ASSERT_EQ(knownhosts_init(&kh, g_sess->session, path), KNOWNHOSTS_OK);
    int rc = knownhosts_add(&kh, "nodir.example.com", 22,
                            (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                            LIBSSH2_HOSTKEY_TYPE_RSA);
    ASSERT_EQ(rc, KNOWNHOSTS_ERROR);

    FILE *f = fopen(path, "rb");
    ASSERT_NULL(f);
    if (f) fclose(f);

    knownhosts_free(&kh);
    TEST_END();
}

/* ---- Port-exact entries ----------------------------------------------------- */

int test_kh_port_specific_entries(void)
{
    TEST_BEGIN();
    init_key_b();
    char path[256];
    tmp_path(path, sizeof(path), "nutshell_kh_test_port.txt");
    UNLINK(path);

    KnownHosts kh;
    ASSERT_EQ(knownhosts_init(&kh, g_sess->session, path), KNOWNHOSTS_OK);

    ASSERT_EQ(knownhosts_add(&kh, "h.example", 22,
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA), KNOWNHOSTS_OK);

    /* Port 2222 must be NEW before anything is ever stored for it, even
     * though port 22 already has an entry for the same host. */
    ASSERT_EQ(knownhosts_check(&kh, "h.example", 2222,
                                (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                                NULL, 0), KNOWNHOSTS_NEW);

    ASSERT_EQ(knownhosts_add(&kh, "h.example", 2222,
                              (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA), KNOWNHOSTS_OK);

    ASSERT_EQ(knownhosts_check(&kh, "h.example", 22,
                                (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_check(&kh, "h.example", 2222,
                                (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    knownhosts_free(&kh);

    /* Reload from disk with a fresh store -- same answers. */
    KnownHosts kh2;
    ASSERT_EQ(knownhosts_init(&kh2, g_sess->session, path), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_check(&kh2, "h.example", 22,
                                (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_check(&kh2, "h.example", 2222,
                                (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    knownhosts_free(&kh2);
    TEST_END();
}

int test_kh_case_insensitive_lookup(void)
{
    TEST_BEGIN();
    char path[256];
    tmp_path(path, sizeof(path), "nutshell_kh_test_case.txt");
    UNLINK(path);

    KnownHosts kh;
    ASSERT_EQ(knownhosts_init(&kh, g_sess->session, path), KNOWNHOSTS_OK);

    ASSERT_EQ(knownhosts_add(&kh, "MyHost.Example", 22,
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA), KNOWNHOSTS_OK);

    ASSERT_EQ(knownhosts_check(&kh, "myhost.example", 22,
                                (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    ASSERT_EQ(knownhosts_check(&kh, "MYHOST.EXAMPLE", 22,
                                (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                                NULL, 0), KNOWNHOSTS_OK);
    knownhosts_free(&kh);

    unsigned char *buf = NULL;
    long len = read_file_bytes(path, &buf);
    ASSERT_TRUE(len > 0);
    if (buf) {
        ASSERT_TRUE(strstr((char *)buf, "myhost.example") != NULL);
        ASSERT_TRUE(strstr((char *)buf, "MyHost.Example") == NULL);
        free(buf);
    }
    TEST_END();
}

/* ---- knownhosts_entry_name -------------------------------------------------- */

int test_kh_entry_name_table(void)
{
    TEST_BEGIN();
    char out[64];

    ASSERT_EQ(knownhosts_entry_name("Host", 22, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "host");

    ASSERT_EQ(knownhosts_entry_name("Host", 2222, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "[host]:2222");

    ASSERT_EQ(knownhosts_entry_name("h", 0, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "h");

    ASSERT_EQ(knownhosts_entry_name("::1", 2222, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "[::1]:2222");

    ASSERT_EQ(knownhosts_entry_name(NULL, 22, out, sizeof(out)), -1);
    ASSERT_EQ(knownhosts_entry_name("", 22, out, sizeof(out)), -1);

    char tiny[2];
    ASSERT_EQ(knownhosts_entry_name("host", 2222, tiny, sizeof(tiny)), -1);

    TEST_END();
}

/* ---- knownhosts_lookup / key type names ------------------------------------- */

int test_kh_mismatch_stored_fingerprint(void)
{
    TEST_BEGIN();
    init_key_b();
    KnownHosts kh;
    ASSERT_EQ(setup_kh(&kh, "nutshell_kh_test_mismatch_fp.txt"), KNOWNHOSTS_OK);

    ASSERT_EQ(knownhosts_add(&kh, "mmfp.example.com", 22,
                              (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                              LIBSSH2_HOSTKEY_TYPE_RSA), KNOWNHOSTS_OK);

    KnownHostsResult res_a;
    ASSERT_EQ(knownhosts_lookup(&kh, "mmfp.example.com", 22,
                                (const char *)TEST_KEY_A, TEST_KEY_A_LEN,
                                LIBSSH2_HOSTKEY_TYPE_RSA, &res_a), KNOWNHOSTS_OK);

    KnownHostsResult res_mismatch;
    ASSERT_EQ(knownhosts_lookup(&kh, "mmfp.example.com", 22,
                                (const char *)TEST_KEY_B, TEST_KEY_B_LEN,
                                LIBSSH2_HOSTKEY_TYPE_RSA, &res_mismatch),
              KNOWNHOSTS_MISMATCH);

    ASSERT_TRUE(res_mismatch.fingerprint[0] != '\0');
    ASSERT_TRUE(res_mismatch.stored_fingerprint[0] != '\0');
    ASSERT_STR_EQ(res_mismatch.stored_fingerprint, res_a.fingerprint);
    ASSERT_TRUE(strcmp(res_mismatch.stored_fingerprint, res_mismatch.fingerprint) != 0);
    ASSERT_STR_EQ(res_mismatch.stored_key_type, "ssh-rsa");

    knownhosts_free(&kh);
    TEST_END();
}

int test_kh_key_type_name_table(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(knownhosts_key_type_name(LIBSSH2_HOSTKEY_TYPE_RSA), "ssh-rsa");
    ASSERT_STR_EQ(knownhosts_key_type_name(LIBSSH2_HOSTKEY_TYPE_DSS), "ssh-dss");
    ASSERT_STR_EQ(knownhosts_key_type_name(LIBSSH2_HOSTKEY_TYPE_ED25519), "ssh-ed25519");
    ASSERT_STR_EQ(knownhosts_key_type_name(LIBSSH2_HOSTKEY_TYPE_ECDSA_256), "ecdsa-sha2-nistp256");
    ASSERT_STR_EQ(knownhosts_key_type_name(LIBSSH2_HOSTKEY_TYPE_ECDSA_384), "ecdsa-sha2-nistp384");
    ASSERT_STR_EQ(knownhosts_key_type_name(LIBSSH2_HOSTKEY_TYPE_ECDSA_521), "ecdsa-sha2-nistp521");
    ASSERT_STR_EQ(knownhosts_key_type_name(999), "unknown");
    TEST_END();
}

#endif /* NO_SSH_LIBS */
