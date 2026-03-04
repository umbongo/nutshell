#include "knownhosts.h"
#include <string.h>
#include <stdio.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

/* ---- Fingerprint helper --------------------------------------------------- */

/* Format raw key bytes as "SHA256:<base64>" (OpenSSH display format).
 * Writes into out[out_size].  Returns 0 on success. */
static int format_fingerprint(const char *key, size_t key_len,
                               char *out, size_t out_size)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)key, key_len, digest);

    /* base64-encode the 32-byte digest (EVP_EncodeBlock, no line-breaks) */
    char b64[64]; /* ceil(32/3)*4 = 44 + NUL */
    int b64_len = EVP_EncodeBlock((unsigned char *)b64, digest, SHA256_DIGEST_LENGTH);
    if (b64_len < 0) return -1;
    b64[b64_len] = '\0';

    /* Strip trailing '=' padding (OpenSSH omits it) */
    while (b64_len > 0 && b64[b64_len - 1] == '=') {
        b64[--b64_len] = '\0';
    }

    int written = snprintf(out, out_size, "SHA256:%s", b64);
    return (written > 0 && (size_t)written < out_size) ? 0 : -1;
}

/* ---- Public API ----------------------------------------------------------- */

int knownhosts_init(KnownHosts *kh, LIBSSH2_SESSION *session, const char *path)
{
    if (!kh || !session || !path) return KNOWNHOSTS_ERROR;
    memset(kh, 0, sizeof(*kh));
    kh->session = session;
    snprintf(kh->path, sizeof(kh->path), "%s", path);

    kh->store = libssh2_knownhost_init(session);
    if (!kh->store) return KNOWNHOSTS_ERROR;

    /* Load existing entries — it's OK if the file does not exist yet */
    int rc = libssh2_knownhost_readfile(kh->store, path,
                                         LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    if (rc < 0 && rc != LIBSSH2_ERROR_FILE) {
        /* File exists but is unreadable/corrupt */
        return KNOWNHOSTS_ERROR;
    }
    return KNOWNHOSTS_OK;
}

int knownhosts_check(KnownHosts *kh,
                     const char *host, int port,
                     const char *key, size_t key_len,
                     char *fingerprint_out, size_t fp_size)
{
    if (!kh || !kh->store || !host || !key || key_len == 0u) return KNOWNHOSTS_ERROR;

    if (fingerprint_out && fp_size > 0u) {
        if (format_fingerprint(key, key_len, fingerprint_out, fp_size) != 0) {
            fingerprint_out[0] = '\0';
        }
    }

    struct libssh2_knownhost *found = NULL;
    int result = libssh2_knownhost_checkp(
        kh->store, host, port, key, key_len,
        LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW,
        &found);

    switch (result) {
        case LIBSSH2_KNOWNHOST_CHECK_MATCH:    return KNOWNHOSTS_OK;
        case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND: return KNOWNHOSTS_NEW;
        case LIBSSH2_KNOWNHOST_CHECK_MISMATCH: return KNOWNHOSTS_MISMATCH;
        default:                               return KNOWNHOSTS_ERROR;
    }
}

int knownhosts_add(KnownHosts *kh,
                   const char *host, int port,
                   const char *key, size_t key_len)
{
    if (!kh || !kh->store || !host || !key || key_len == 0u) return KNOWNHOSTS_ERROR;

    /* Build the host string with port if non-standard */
    char hostport[320];
    if (port == 22) {
        snprintf(hostport, sizeof(hostport), "%s", host);
    } else {
        snprintf(hostport, sizeof(hostport), "[%s]:%d", host, port);
    }

    /* Remove any existing entry for this host first (handles key rotation) */
    struct libssh2_knownhost *existing = NULL;
    int check = libssh2_knownhost_checkp(
        kh->store, host, port, key, key_len,
        LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW,
        &existing);
    if (check == LIBSSH2_KNOWNHOST_CHECK_MISMATCH && existing) {
        libssh2_knownhost_del(kh->store, existing);
    }

    int rc = libssh2_knownhost_addc(
        kh->store,
        hostport, NULL,          /* host, salt (NULL = unhashed) */
        key, key_len,
        NULL, 0,                 /* comment, comment_len */
        LIBSSH2_KNOWNHOST_TYPE_PLAIN |
        LIBSSH2_KNOWNHOST_KEYENC_RAW |
        LIBSSH2_KNOWNHOST_KEY_SSHRSA,
        NULL);                   /* struct libssh2_knownhost ** store_handle */

    if (rc != 0) return KNOWNHOSTS_ERROR;

    rc = libssh2_knownhost_writefile(kh->store, kh->path,
                                      LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    return (rc == 0) ? KNOWNHOSTS_OK : KNOWNHOSTS_ERROR;
}

void knownhosts_free(KnownHosts *kh)
{
    if (!kh) return;
    if (kh->store) {
        libssh2_knownhost_free(kh->store);
        kh->store = NULL;
    }
}
