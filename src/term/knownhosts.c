#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "knownhosts.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
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

/* Decode a stored known_hosts key (base64 text, as libssh2 hands back in
 * struct libssh2_knownhost.key) and format its fingerprint. Returns 0 on
 * success, leaving out untouched (caller should clear it first) on failure. */
static int format_stored_fingerprint(const char *b64key, char *out, size_t out_size)
{
    if (!b64key) return -1;
    size_t b64_len = strlen(b64key);
    if (b64_len == 0u || (b64_len % 4u) != 0u) return -1;

    /* Sanity bound -- no real host key's base64 form is anywhere near this
     * long; refuse rather than risk an oversized stack buffer. */
    if (b64_len > 8192u) return -1;

    unsigned char rawbuf[6144]; /* (8192/4)*3 */
    int declen = EVP_DecodeBlock(rawbuf, (const unsigned char *)b64key, (int)b64_len);
    if (declen < 0) return -1;

    int pad = 0;
    if (b64_len >= 1u && b64key[b64_len - 1u] == '=') pad++;
    if (b64_len >= 2u && b64key[b64_len - 2u] == '=') pad++;
    if ((size_t)declen < (size_t)pad) return -1;

    size_t raw_len = (size_t)declen - (size_t)pad;
    if (raw_len == 0u) return -1;

    return format_fingerprint((const char *)rawbuf, raw_len, out, out_size);
}

/* ---- Canonical entry name -------------------------------------------------- */

int knownhosts_entry_name(const char *host, int port, char *out, size_t out_size)
{
    if (!host || !host[0] || !out || out_size == 0u) return -1;
    if (port <= 0) port = 22;

    char lower[300];
    size_t hlen = strlen(host);
    if (hlen >= sizeof(lower)) return -1;
    for (size_t i = 0; i < hlen; i++) {
        lower[i] = (char)tolower((unsigned char)host[i]);
    }
    lower[hlen] = '\0';

    int written;
    if (port == 22) {
        written = snprintf(out, out_size, "%s", lower);
    } else {
        written = snprintf(out, out_size, "[%s]:%d", lower, port);
    }
    return (written > 0 && (size_t)written < out_size) ? 0 : -1;
}

/* ---- Key type names --------------------------------------------------------- */

/* Map libssh2_session_hostkey() type (LIBSSH2_HOSTKEY_TYPE_*) to its OpenSSH name. */
const char *knownhosts_key_type_name(int libssh2_hostkey_type)
{
    switch (libssh2_hostkey_type) {
        case LIBSSH2_HOSTKEY_TYPE_RSA:       return "ssh-rsa";
        case LIBSSH2_HOSTKEY_TYPE_DSS:       return "ssh-dss";
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_256
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return "ecdsa-sha2-nistp256";
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return "ecdsa-sha2-nistp384";
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return "ecdsa-sha2-nistp521";
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
        case LIBSSH2_HOSTKEY_TYPE_ED25519:   return "ssh-ed25519";
#endif
        default:                             return "unknown";
    }
}

/* Map a stored knownhost entry's typemask (LIBSSH2_KNOWNHOST_KEY_* field) to
 * its OpenSSH name -- the reverse of map_key_type() below. */
static const char *knownhost_key_mask_name(int typemask)
{
    switch (typemask & LIBSSH2_KNOWNHOST_KEY_MASK) {
        case LIBSSH2_KNOWNHOST_KEY_SSHRSA:      return "ssh-rsa";
        case LIBSSH2_KNOWNHOST_KEY_SSHDSS:      return "ssh-dss";
#ifdef LIBSSH2_KNOWNHOST_KEY_ECDSA_256
        case LIBSSH2_KNOWNHOST_KEY_ECDSA_256:   return "ecdsa-sha2-nistp256";
        case LIBSSH2_KNOWNHOST_KEY_ECDSA_384:   return "ecdsa-sha2-nistp384";
        case LIBSSH2_KNOWNHOST_KEY_ECDSA_521:   return "ecdsa-sha2-nistp521";
#endif
#ifdef LIBSSH2_KNOWNHOST_KEY_ED25519
        case LIBSSH2_KNOWNHOST_KEY_ED25519:     return "ssh-ed25519";
#endif
        default:                                return "unknown";
    }
}

/* H-4: map libssh2_session_hostkey() type to LIBSSH2_KNOWNHOST_KEY_* flag. */
static int map_key_type(int libssh2_hostkey_type)
{
    switch (libssh2_hostkey_type) {
        case LIBSSH2_HOSTKEY_TYPE_RSA:       return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
        case LIBSSH2_HOSTKEY_TYPE_DSS:       return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_256
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
        case LIBSSH2_HOSTKEY_TYPE_ED25519:   return LIBSSH2_KNOWNHOST_KEY_ED25519;
#endif
        default:                             return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    }
}

/* ---- Public API ----------------------------------------------------------- */

int knownhosts_init(KnownHosts *kh, LIBSSH2_SESSION *session, const char *path)
{
    if (!kh || !session || !path) return KNOWNHOSTS_ERROR;
    memset(kh, 0, sizeof(*kh));
    kh->session = session;
    snprintf(kh->path, sizeof(kh->path), "%s", path);

    /* Probe the file ourselves first: a missing file (ENOENT) is a
     * legitimate "no known_hosts yet" (first run). Anything else -- the
     * path is a directory, permission denied, ... -- means we cannot trust
     * what libssh2_knownhost_readfile() would silently do with it, so fail
     * closed instead of starting from an empty (and therefore
     * everything-is-NEW) store. */
    FILE *probe = fopen(path, "rb");
    if (!probe && errno != ENOENT) {
        return KNOWNHOSTS_ERROR;
    }
    int file_exists = (probe != NULL);
    if (probe) fclose(probe);

    kh->store = libssh2_knownhost_init(session);
    if (!kh->store) return KNOWNHOSTS_ERROR;

    if (file_exists) {
        /* File exists and we could read it a moment ago -- any parse
         * failure now (garbled/unparseable content) means the file cannot
         * be trusted. */
        int rc = libssh2_knownhost_readfile(kh->store, path,
                                             LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        if (rc < 0) return KNOWNHOSTS_ERROR;
    }
    return KNOWNHOSTS_OK;
}

int knownhosts_lookup(KnownHosts *kh, const char *host, int port,
                      const char *key, size_t key_len, int key_type,
                      KnownHostsResult *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!kh || !kh->store || !host || !key || key_len == 0u) return KNOWNHOSTS_ERROR;

    char name[320];
    if (knownhosts_entry_name(host, port, name, sizeof(name)) != 0) {
        return KNOWNHOSTS_ERROR;
    }

    if (out) {
        snprintf(out->key_type, sizeof(out->key_type), "%s",
                 knownhosts_key_type_name(key_type));
        if (format_fingerprint(key, key_len, out->fingerprint, sizeof(out->fingerprint)) != 0) {
            out->fingerprint[0] = '\0';
        }
    }

    /* Port -1 with the pre-bracketed/lowercased name is what stops libssh2
     * from falling back to a plain "host" entry when the name encodes a
     * non-default port -- with port >= 0 it tries "[host]:port" and then
     * falls back to plain "host", which would let a port-2222 lookup match
     * a stored port-22 entry. */
    struct libssh2_knownhost *found = NULL;
    int result = libssh2_knownhost_checkp(
        kh->store, name, -1, key, key_len,
        LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW,
        &found);

    if (result == LIBSSH2_KNOWNHOST_CHECK_MISMATCH && found && out) {
        snprintf(out->stored_key_type, sizeof(out->stored_key_type), "%s",
                 knownhost_key_mask_name(found->typemask));
        if (format_stored_fingerprint(found->key, out->stored_fingerprint,
                                       sizeof(out->stored_fingerprint)) != 0) {
            out->stored_fingerprint[0] = '\0';
        }
    }

    switch (result) {
        case LIBSSH2_KNOWNHOST_CHECK_MATCH:    return KNOWNHOSTS_OK;
        case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND: return KNOWNHOSTS_NEW;
        case LIBSSH2_KNOWNHOST_CHECK_MISMATCH: return KNOWNHOSTS_MISMATCH;
        default:                               return KNOWNHOSTS_ERROR;
    }
}

int knownhosts_check(KnownHosts *kh,
                     const char *host, int port,
                     const char *key, size_t key_len,
                     char *fingerprint_out, size_t fp_size)
{
    KnownHostsResult res;
    int rc = knownhosts_lookup(kh, host, port, key, key_len,
                               LIBSSH2_HOSTKEY_TYPE_UNKNOWN, &res);
    if (fingerprint_out && fp_size > 0u) {
        snprintf(fingerprint_out, fp_size, "%s", res.fingerprint);
    }
    return rc;
}

/* ---- Atomic writer ---------------------------------------------------------- */

/* Test-only fault injection: 0 = off; 1 = fail the first entry write;
 * 2 = fail the flush/close; 3 = fail the final replace. Fires once. */
static int g_write_fault_step = 0;

void knownhosts_test_inject_write_fault(int step)
{
    g_write_fault_step = step;
}

/* Returns 1 (and consumes the injected fault) if 'step' is the one armed;
 * the caller treats that as if the real call at this point had failed. */
static int inject_fault(int step)
{
    if (g_write_fault_step == step) {
        g_write_fault_step = 0;
        return 1;
    }
    return 0;
}

static void remove_temp_file(const char *tmp_path)
{
#ifdef _WIN32
    DeleteFileA(tmp_path);
#else
    unlink(tmp_path);
#endif
}

/* Write kh->store out to kh->path atomically: build the new content in a
 * temp file in the same directory, flush it to disk, then rename it over
 * the target. On any failure the temp file is removed and the original
 * file at kh->path is left untouched. */
static int knownhosts_write_atomic(KnownHosts *kh)
{
    char tmp_path[sizeof(((KnownHosts *)0)->path) + 64];
#ifdef _WIN32
    snprintf(tmp_path, sizeof(tmp_path), "%s.%lu.%lu.tmp", kh->path,
             (unsigned long)GetCurrentProcessId(),
             (unsigned long)GetCurrentThreadId());
#else
    static int unique_counter = 0;
    snprintf(tmp_path, sizeof(tmp_path), "%s.%ld.%d.tmp", kh->path,
             (long)getpid(), unique_counter++);
#endif

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return KNOWNHOSTS_ERROR;

    int fault = 0;
    char linebuf[8192];
    struct libssh2_knownhost *entry = NULL;
    struct libssh2_knownhost *prev = NULL;

    for (;;) {
        int grc = libssh2_knownhost_get(kh->store, &entry, prev);
        if (grc == 1) break;          /* end of list */
        if (grc != 0) { fault = 1; break; }

        size_t outlen = 0;
        int wrc = libssh2_knownhost_writeline(kh->store, entry, linebuf,
                                              sizeof(linebuf), &outlen,
                                              LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        if (wrc != 0) { fault = 1; break; }

        size_t written = inject_fault(1) ? 0 : fwrite(linebuf, 1, outlen, f);
        if (written != outlen) { fault = 1; break; }

        prev = entry;
    }

    if (!fault && (inject_fault(2) || fflush(f) != 0)) {
        fault = 1;
    }
    if (!fault) {
#ifdef _WIN32
        if (inject_fault(2) || _commit(_fileno(f)) != 0) fault = 1;
#else
        if (inject_fault(2) || fsync(fileno(f)) != 0) fault = 1;
#endif
    }

    if (fclose(f) != 0) fault = 1;

    if (fault) {
        remove_temp_file(tmp_path);
        return KNOWNHOSTS_ERROR;
    }

    if (inject_fault(3)) {
        remove_temp_file(tmp_path);
        return KNOWNHOSTS_ERROR;
    }

#ifdef _WIN32
    if (!MoveFileExA(tmp_path, kh->path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove_temp_file(tmp_path);
        return KNOWNHOSTS_ERROR;
    }
#else
    if (rename(tmp_path, kh->path) != 0) {
        remove_temp_file(tmp_path);
        return KNOWNHOSTS_ERROR;
    }
#endif

    return KNOWNHOSTS_OK;
}

int knownhosts_add(KnownHosts *kh,
                   const char *host, int port,
                   const char *key, size_t key_len,
                   int key_type)
{
    if (!kh || !kh->store || !host || !key || key_len == 0u) return KNOWNHOSTS_ERROR;

    /* Canonical name for both lookup and storage, so a port-2222 accept
     * never touches a plain-"host" port-22 entry (or vice versa). */
    char name[320];
    if (knownhosts_entry_name(host, port, name, sizeof(name)) != 0) {
        return KNOWNHOSTS_ERROR;
    }

    /* Remove every existing entry for this exact name whose key differs
     * (key rotation). Bounded iteration count as a sanity guard against a
     * pathological store that never resolves to NOTFOUND/MATCH. */
    for (int i = 0; i < 64; i++) {
        struct libssh2_knownhost *existing = NULL;
        int check = libssh2_knownhost_checkp(
            kh->store, name, -1, key, key_len,
            LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW,
            &existing);
        if (check != LIBSSH2_KNOWNHOST_CHECK_MISMATCH || !existing) break;
        libssh2_knownhost_del(kh->store, existing);
    }

    int rc = libssh2_knownhost_addc(
        kh->store,
        name, NULL,              /* host, salt (NULL = unhashed) */
        key, key_len,
        NULL, 0,                 /* comment, comment_len */
        LIBSSH2_KNOWNHOST_TYPE_PLAIN |
        LIBSSH2_KNOWNHOST_KEYENC_RAW |
        map_key_type(key_type),  /* H-4: use actual key type */
        NULL);                   /* struct libssh2_knownhost ** store_handle */

    if (rc != 0) return KNOWNHOSTS_ERROR;

    return knownhosts_write_atomic(kh);
}

void knownhosts_free(KnownHosts *kh)
{
    if (!kh) return;
    if (kh->store) {
        libssh2_knownhost_free(kh->store);
        kh->store = NULL;
    }
}
