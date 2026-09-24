#ifndef NUTSHELL_KNOWNHOSTS_H
#define NUTSHELL_KNOWNHOSTS_H

#include <stddef.h>
#include <libssh2.h>
#include <libssh2_publickey.h>

/* Return codes */
#define KNOWNHOSTS_OK        0  /* host key matches stored entry */
#define KNOWNHOSTS_NEW       1  /* host not in file — first connection */
#define KNOWNHOSTS_MISMATCH  2  /* host known but key has changed */
#define KNOWNHOSTS_ERROR    -1  /* I/O or API failure */

typedef struct {
    LIBSSH2_KNOWNHOSTS *store;
    LIBSSH2_SESSION    *session;
    char                path[4096];
} KnownHosts;

/*
 * knownhosts_init — open the known_hosts file at path.
 * The file is NOT created here: a missing file (first run) is fine and
 * leaves an empty in-memory store, but any other reason the file cannot be
 * read (the path is a directory, permission denied, a garbled/unparseable
 * file) fails closed with KNOWNHOSTS_ERROR rather than silently starting
 * from an empty store. The file is created on disk the first time
 * knownhosts_add() persists an entry.
 * Returns KNOWNHOSTS_OK or KNOWNHOSTS_ERROR.
 * Call knownhosts_free() when done even on error (to release partial state).
 */
int knownhosts_init(KnownHosts *kh, LIBSSH2_SESSION *session, const char *path);

/*
 * knownhosts_check — look up host:port with the given raw key bytes.
 * On KNOWNHOSTS_NEW or KNOWNHOSTS_MISMATCH, fingerprint_out receives a
 * null-terminated "SHA256:base64..." string (OpenSSH display format).
 * fingerprint_out may be NULL if the caller does not need the fingerprint.
 * Thin wrapper over knownhosts_lookup(); prefer that when the caller also
 * wants the key type or the stored key's fingerprint on a mismatch.
 */
int knownhosts_check(KnownHosts *kh,
                     const char *host, int port,
                     const char *key, size_t key_len,
                     char *fingerprint_out, size_t fp_size);

/*
 * knownhosts_add — store host:port → key and persist to disk.
 * key_type is the value returned by libssh2_session_hostkey() (LIBSSH2_HOSTKEY_TYPE_*).
 * Existing entry for the same host:port is replaced (handles key rotation).
 * The file is written atomically: a temp file in the same directory is
 * written, flushed to disk and then renamed over the target, so a crash or
 * power loss mid-write never leaves a truncated or half-written
 * known_hosts file — the original stays intact until the replace succeeds.
 * Returns KNOWNHOSTS_OK or KNOWNHOSTS_ERROR.
 */
int knownhosts_add(KnownHosts *kh,
                   const char *host, int port,
                   const char *key, size_t key_len,
                   int key_type);

/*
 * knownhosts_free — release all resources held by kh.
 * Safe to call even if knownhosts_init returned an error.
 */
void knownhosts_free(KnownHosts *kh);

/*
 * knownhosts_entry_name — the canonical known_hosts entry name for
 * host:port, exactly as OpenSSH writes it: host lowercased, and
 * "[host]:port" for any port other than 22 (port <= 0 is treated as 22).
 * Writes a NUL-terminated string into out[out_size].
 * Returns 0, or -1 if host is NULL/empty, out is NULL/zero-sized, or the
 * name does not fit in out_size.
 */
int knownhosts_entry_name(const char *host, int port, char *out, size_t out_size);

/*
 * knownhosts_key_type_name — the OpenSSH name of a libssh2_session_hostkey()
 * key type (LIBSSH2_HOSTKEY_TYPE_*): "ssh-rsa", "ssh-dss",
 * "ecdsa-sha2-nistp256"/"384"/"521", "ssh-ed25519". Returns "unknown" for
 * any other value.
 */
const char *knownhosts_key_type_name(int libssh2_hostkey_type);

typedef struct {
    char key_type[32];            /* presented key's type name */
    char fingerprint[128];        /* presented key, "SHA256:..." */
    char stored_key_type[32];     /* MISMATCH only: stored entry's type name, else "" */
    char stored_fingerprint[128]; /* MISMATCH only: stored key's "SHA256:...", else "" */
} KnownHostsResult;

/*
 * knownhosts_lookup — look up the exact entry for host:port (see
 * knownhosts_entry_name) — never a plain "host" entry for a non-22 port.
 * Fills *out with the presented key's type/fingerprint always, and (on
 * KNOWNHOSTS_MISMATCH only) the stored entry's type/fingerprint. out may
 * be NULL if the caller does not need any of this.
 * Returns KNOWNHOSTS_OK / KNOWNHOSTS_NEW / KNOWNHOSTS_MISMATCH / KNOWNHOSTS_ERROR.
 */
int knownhosts_lookup(KnownHosts *kh, const char *host, int port,
                      const char *key, size_t key_len, int key_type,
                      KnownHostsResult *out);

/*
 * knownhosts_test_inject_write_fault — test-only fault injection for the
 * atomic writer used by knownhosts_add(). 0 = off (default); 1 = fail the
 * first write of an entry; 2 = fail the flush/close; 3 = fail the final
 * replace. Resets to 0 after firing once, so a single call injects exactly
 * one failure into the next knownhosts_add().
 */
void knownhosts_test_inject_write_fault(int step);

#endif /* NUTSHELL_KNOWNHOSTS_H */
