#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#include <windows.h>
#include <io.h>
#include <sddl.h>       /* ConvertSidToStringSidA */
#else
#include <unistd.h>
#include <fcntl.h>
#endif

#include "knownhosts.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
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

/* Shared by knownhosts_entry_name() (lowercase = 1) and the H-6 backward-
 * compat retry in knownhosts_lookup() (lowercase = 0, the host exactly as
 * given). */
static int build_entry_name(const char *host, int port, char *out,
                             size_t out_size, int lowercase)
{
    if (!host || !host[0] || !out || out_size == 0u) return -1;
    if (port <= 0) port = 22;

    char namebuf[300];
    size_t hlen = strlen(host);
    if (hlen >= sizeof(namebuf)) return -1;
    for (size_t i = 0; i < hlen; i++) {
        namebuf[i] = lowercase ? (char)tolower((unsigned char)host[i]) : host[i];
    }
    namebuf[hlen] = '\0';

    int written;
    if (port == 22) {
        written = snprintf(out, out_size, "%s", namebuf);
    } else {
        written = snprintf(out, out_size, "[%s]:%d", namebuf, port);
    }
    return (written > 0 && (size_t)written < out_size) ? 0 : -1;
}

int knownhosts_entry_name(const char *host, int port, char *out, size_t out_size)
{
    return build_entry_name(host, port, out, out_size, 1);
}

/* Case-insensitive ASCII compare (host names are ASCII; avoids depending on
 * a POSIX/MSVC-specific strcasecmp/_stricmp declaration). */
static int ascii_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (char)tolower((unsigned char)*a);
        char cb = (char)tolower((unsigned char)*b);
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
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

/* Probe whether 'path' is safely readable as a known_hosts store.
 * Returns 1 if a regular file exists there, 0 if it legitimately does not
 * exist yet (ENOENT -- fine, first run), or -1 for anything else that must
 * fail closed (a directory, permission denied, ...).
 *
 * A directory is checked explicitly: on Windows fopen() refuses a
 * directory, but on POSIX it succeeds and the read then fails later (or is
 * silently empty), which would otherwise pass as "no known hosts". */
static int probe_regular_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && !S_ISREG(st.st_mode)) {
        return -1;
    }
    FILE *probe = fopen(path, "rb");
    if (!probe) {
        return (errno == ENOENT) ? 0 : -1;
    }
    fclose(probe);
    return 1;
}

/* Allocate a fresh known-hosts store and load 'path' into it, applying the
 * same fail-closed rules as knownhosts_init(): a missing file is a
 * legitimate empty store; a directory, permission error, or garbled/
 * unparseable content is KNOWNHOSTS_ERROR and *out_store is left NULL.
 * Used by knownhosts_init() and by the H-7 read-merge-write in
 * knownhosts_add(), so both start from the same on-disk truth. */
static int load_store_from_disk(LIBSSH2_SESSION *session, const char *path,
                                 LIBSSH2_KNOWNHOSTS **out_store)
{
    *out_store = NULL;

    int probe = probe_regular_file(path);
    if (probe < 0) return KNOWNHOSTS_ERROR;

    LIBSSH2_KNOWNHOSTS *store = libssh2_knownhost_init(session);
    if (!store) return KNOWNHOSTS_ERROR;

    if (probe == 1) {
        /* File exists and we could read it a moment ago -- any parse
         * failure now (garbled/unparseable content) means the file cannot
         * be trusted. */
        int rc = libssh2_knownhost_readfile(store, path,
                                             LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        if (rc < 0) {
            libssh2_knownhost_free(store);
            return KNOWNHOSTS_ERROR;
        }
    }

    *out_store = store;
    return KNOWNHOSTS_OK;
}

int knownhosts_init(KnownHosts *kh, LIBSSH2_SESSION *session, const char *path)
{
    /* H-8b: memset first, before ANY early return, so a struct this call
     * bails out of early is still all-zero (kh->store == NULL) and safe to
     * pass to knownhosts_free() -- including twice. Only a NULL kh itself
     * skips it, and knownhosts_free(NULL) is already a no-op. */
    if (!kh) return KNOWNHOSTS_ERROR;
    memset(kh, 0, sizeof(*kh));

    if (!session || !path) return KNOWNHOSTS_ERROR;

    kh->session = session;
    snprintf(kh->path, sizeof(kh->path), "%s", path);

    return load_store_from_disk(session, path, &kh->store);
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

    if (result == LIBSSH2_KNOWNHOST_CHECK_NOTFOUND) {
        /* H-6: an entry written by an older Nutshell (or by hand) may use
         * the host exactly as the caller typed it, not lowercased. Retry
         * once with that exact casing before reporting NEW, so an old
         * mixed-case entry still verifies OK for the same key, or still
         * shows MISMATCH (with the stored fingerprint) for a changed one,
         * instead of looking unseen. */
        char exact_name[320];
        if (build_entry_name(host, port, exact_name, sizeof(exact_name), 0) == 0 &&
            strcmp(exact_name, name) != 0) {
            struct libssh2_knownhost *exact_found = NULL;
            int exact_result = libssh2_knownhost_checkp(
                kh->store, exact_name, -1, key, key_len,
                LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW,
                &exact_found);
            if (exact_result == LIBSSH2_KNOWNHOST_CHECK_MATCH ||
                exact_result == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) {
                result = exact_result;
                found = exact_found;
            }
        }
    }

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

/* Write 'store' out to 'path' atomically: build the new content in a temp
 * file in the same directory, flush it to disk, then rename it over the
 * target. On any failure the temp file is removed and the original file at
 * 'path' is left untouched. Takes the store explicitly (rather than a
 * KnownHosts *) so knownhosts_add()'s H-7 read-merge-write can write a
 * freshly-merged store before deciding whether to adopt it. */
static int knownhosts_write_atomic(const char *path, LIBSSH2_KNOWNHOSTS *store)
{
    char tmp_path[sizeof(((KnownHosts *)0)->path) + 64];
#ifdef _WIN32
    snprintf(tmp_path, sizeof(tmp_path), "%s.%lu.%lu.tmp", path,
             (unsigned long)GetCurrentProcessId(),
             (unsigned long)GetCurrentThreadId());
#else
    static int unique_counter = 0;
    snprintf(tmp_path, sizeof(tmp_path), "%s.%ld.%d.tmp", path,
             (long)getpid(), unique_counter++);
#endif

#ifdef _WIN32
    FILE *f = fopen(tmp_path, "wb");
#else
    /* Owner-only permissions, and O_EXCL so a file or symlink already at
     * this predictable name is never opened or followed. */
    FILE *f = NULL;
    {
        int fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            f = fdopen(fd, "wb");
            if (!f) {
                close(fd);
                (void)unlink(tmp_path);
            }
        }
    }
#endif
    if (!f) return KNOWNHOSTS_ERROR;

    int fault = 0;
    char linebuf[8192];
    struct libssh2_knownhost *entry = NULL;
    struct libssh2_knownhost *prev = NULL;

    for (;;) {
        int grc = libssh2_knownhost_get(store, &entry, prev);
        if (grc == 1) break;          /* end of list */
        if (grc != 0) { fault = 1; break; }

        size_t outlen = 0;
        int wrc = libssh2_knownhost_writeline(store, entry, linebuf,
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
    if (!MoveFileExA(tmp_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove_temp_file(tmp_path);
        return KNOWNHOSTS_ERROR;
    }
#else
    if (rename(tmp_path, path) != 0) {
        remove_temp_file(tmp_path);
        return KNOWNHOSTS_ERROR;
    }
#endif

    return KNOWNHOSTS_OK;
}

/* ---- H-7: cross-process lock ------------------------------------------------ */

/* Remove every entry in 'store' whose plain-text name matches 'name'
 * case-insensitively, regardless of its key. Used before adding a new
 * entry so an old, differently-cased entry (an earlier Nutshell version, a
 * hand-edited file, or OpenSSH itself) is replaced rather than left
 * alongside the new lowercase one (H-6), and so re-adding the same host
 * never leaves a duplicate line.
 *
 * Matches are collected in a first pass and deleted in a second: libssh2's
 * knownhost list is only safe to walk with libssh2_knownhost_get() while
 * unmodified, since 'prev' is itself a pointer into the list. */
#define KH_MAX_CI_MATCHES 64

static void remove_entries_ci(LIBSSH2_KNOWNHOSTS *store, const char *name)
{
    struct libssh2_knownhost *to_delete[KH_MAX_CI_MATCHES];
    int n_to_delete = 0;

    struct libssh2_knownhost *entry = NULL;
    struct libssh2_knownhost *prev = NULL;
    for (;;) {
        int grc = libssh2_knownhost_get(store, &entry, prev);
        if (grc != 0) break; /* 1 = end of list; <0 = error -- stop either way */

        if (entry->name && ascii_strcasecmp(entry->name, name) == 0 &&
            n_to_delete < KH_MAX_CI_MATCHES) {
            to_delete[n_to_delete++] = entry;
        }
        prev = entry;
    }

    for (int i = 0; i < n_to_delete; i++) {
        libssh2_knownhost_del(store, to_delete[i]);
    }
}

/* A cross-process, cross-instance exclusive lock on a known_hosts path, so
 * two tabs (two KnownHosts on the same file, same process or two) can never
 * both read-merge-write at once and lose each other's accept, or write back
 * a stale in-memory key over a key the other one just rotated. On Windows
 * (the product) a named mutex excludes threads and processes alike. The
 * POSIX branch, used by the native test build, is an fcntl record lock,
 * which excludes other processes only -- fcntl locks belong to the process,
 * so two threads of one process are not serialised by it. */
typedef struct {
#ifdef _WIN32
    HANDLE mutex;
#else
    int fd;
#endif
} KhLock;

#ifdef _WIN32
/* Named-mutex identity must depend only on the file, not on casing or on
 * anything per-process, so two instances addressing "the same" path always
 * contend for the same mutex. Hash the lowercased path rather than using it
 * directly: mutex names are limited in length and may not contain '\\'. */
static void hash_path_hex(const char *path, char *out_hex, size_t out_size)
{
    char lower[4096];
    size_t n = strlen(path);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1u;
    for (size_t i = 0; i < n; i++) {
        lower[i] = (char)tolower((unsigned char)path[i]);
    }
    lower[n] = '\0';

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)lower, n, digest);

    static const char hexch[] = "0123456789abcdef";
    size_t max_bytes = (out_size > 0u) ? (out_size - 1u) / 2u : 0u;
    size_t bytes = sizeof(digest) < max_bytes ? sizeof(digest) : max_bytes;
    for (size_t i = 0; i < bytes; i++) {
        out_hex[i * 2u]      = hexch[(digest[i] >> 4) & 0xFu];
        out_hex[i * 2u + 1u] = hexch[digest[i] & 0xFu];
    }
    out_hex[bytes * 2u] = '\0';
}

/* The current process's user SID as a string ("S-1-5-21-...-1001"), used to
 * make the known-hosts lock mutex identify the *user*, not the logon
 * session: "Local\" names are already scoped to the calling session by
 * Windows itself, so without this, the same user's two logon sessions (an
 * RDP session and a console session, say) contend for two different mutex
 * objects even though both may write the same known_hosts file in that
 * user's profile. Returns 0 and fills out_sid on success; on any failure
 * returns -1 and the caller falls back to a name without a SID (the
 * previous behaviour). */
static int current_user_sid_string(char *out_sid, size_t out_size)
{
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return -1;

    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &needed);
    if (needed == 0) { CloseHandle(token); return -1; }

    int ok = -1;
    BYTE *buf = (BYTE *)malloc(needed);
    if (buf) {
        if (GetTokenInformation(token, TokenUser, buf, needed, &needed)) {
            TOKEN_USER *tu = (TOKEN_USER *)buf;
            char *sid_str = NULL;
            if (ConvertSidToStringSidA(tu->User.Sid, &sid_str) && sid_str) {
                int written = snprintf(out_sid, out_size, "%s", sid_str);
                LocalFree(sid_str);
                if (written > 0 && (size_t)written < out_size) ok = 0;
            }
        }
        free(buf);
    }
    CloseHandle(token);
    return ok;
}

static int kh_lock_acquire(const char *path, KhLock *lock)
{
    char hex[33];
    hash_path_hex(path, hex, sizeof(hex));
    char sid[192];      /* documented max SID string length is ~184 chars */
    char name[256];     /* well under the 260-char named-object limit */
    if (current_user_sid_string(sid, sizeof(sid)) == 0) {
        snprintf(name, sizeof(name), "Local\\nutshell-known-hosts-%s-%s", sid, hex);
    } else {
        /* SID unavailable: fall back to the path-only name (today's
         * behaviour) rather than fail the lock outright. */
        snprintf(name, sizeof(name), "Local\\nutshell-known-hosts-%s", hex);
    }

    HANDLE h = CreateMutexA(NULL, FALSE, name);
    if (!h) return -1;

    /* Bounded wait: a wedged other instance must not hang this one forever. */
    DWORD wait = WaitForSingleObject(h, 5000 /* ms */);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
        /* WAIT_ABANDONED means the previous owner exited while holding it
         * (e.g. crashed mid-write) -- the mutex itself is fine to take; the
         * atomic writer is what actually protects the file's contents. */
        lock->mutex = h;
        return 0;
    }
    CloseHandle(h);
    lock->mutex = NULL;
    return -1;
}

static void kh_lock_release(KhLock *lock)
{
    if (!lock->mutex) return;
    ReleaseMutex(lock->mutex);
    CloseHandle(lock->mutex);
    lock->mutex = NULL;
}
#else
static int kh_lock_acquire(const char *path, KhLock *lock)
{
    char lock_path[sizeof(((KnownHosts *)0)->path) + 8];
    int n = snprintf(lock_path, sizeof(lock_path), "%s.lock", path);
    if (n < 0 || (size_t)n >= sizeof(lock_path)) return -1;

    int fd = open(lock_path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    /* F_SETLKW: block until the lock is available. Only the test build's
     * -std=c11 -D_POSIX_C_SOURCE=200809L POSIX APIs are used here (open,
     * fcntl, close) -- no flock(), which is BSD/Linux-specific and not
     * declared under _POSIX_C_SOURCE. */
    if (fcntl(fd, F_SETLKW, &fl) != 0) {
        close(fd);
        return -1;
    }

    lock->fd = fd;
    return 0;
}

static void kh_lock_release(KhLock *lock)
{
    if (lock->fd < 0) return;
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fcntl(lock->fd, F_SETLK, &fl);
    close(lock->fd);
    lock->fd = -1;
}
#endif

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

    /* H-7: serialise the read-merge-write against any other KnownHosts
     * instance writing the same file -- same process (two tabs) or another.
     * Everything from here to the matching release works on a store freshly
     * read from disk, never this instance's own (possibly stale) kh->store,
     * so another writer's already-persisted accept or key rotation is never
     * lost or overwritten with what this instance had in memory. */
    KhLock lock;
#ifdef _WIN32
    lock.mutex = NULL;
#else
    lock.fd = -1;
#endif
    if (kh_lock_acquire(kh->path, &lock) != 0) return KNOWNHOSTS_ERROR;

    LIBSSH2_KNOWNHOSTS *merged = NULL;
    if (load_store_from_disk(kh->session, kh->path, &merged) != KNOWNHOSTS_OK) {
        kh_lock_release(&lock);
        return KNOWNHOSTS_ERROR;
    }

    remove_entries_ci(merged, name);

    int arc = libssh2_knownhost_addc(
        merged,
        name, NULL,              /* host, salt (NULL = unhashed) */
        key, key_len,
        NULL, 0,                 /* comment, comment_len */
        LIBSSH2_KNOWNHOST_TYPE_PLAIN |
        LIBSSH2_KNOWNHOST_KEYENC_RAW |
        map_key_type(key_type),  /* H-4: use actual key type */
        NULL);                   /* struct libssh2_knownhost ** store_handle */

    if (arc != 0) {
        libssh2_knownhost_free(merged);
        kh_lock_release(&lock);
        return KNOWNHOSTS_ERROR;
    }

    if (knownhosts_write_atomic(kh->path, merged) != KNOWNHOSTS_OK) {
        libssh2_knownhost_free(merged);
        kh_lock_release(&lock);
        return KNOWNHOSTS_ERROR;
    }

    /* Adopt the freshly merged, disk-authoritative store so later lookups
     * on this instance see whatever any other writer added too. */
    libssh2_knownhost_free(kh->store);
    kh->store = merged;

    kh_lock_release(&lock);
    return KNOWNHOSTS_OK;
}

void knownhosts_free(KnownHosts *kh)
{
    if (!kh) return;
    if (kh->store) {
        libssh2_knownhost_free(kh->store);
        kh->store = NULL;
    }
}
