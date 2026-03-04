#ifndef CONGA_KNOWNHOSTS_H
#define CONGA_KNOWNHOSTS_H

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
 * knownhosts_init — open or create the known_hosts file at path.
 * Existing entries are loaded; the file is created if it does not exist.
 * Returns KNOWNHOSTS_OK or KNOWNHOSTS_ERROR.
 * Call knownhosts_free() when done even on error (to release partial state).
 */
int knownhosts_init(KnownHosts *kh, LIBSSH2_SESSION *session, const char *path);

/*
 * knownhosts_check — look up host:port with the given raw key bytes.
 * On KNOWNHOSTS_NEW or KNOWNHOSTS_MISMATCH, fingerprint_out receives a
 * null-terminated "SHA256:base64..." string (OpenSSH display format).
 * fingerprint_out may be NULL if the caller does not need the fingerprint.
 */
int knownhosts_check(KnownHosts *kh,
                     const char *host, int port,
                     const char *key, size_t key_len,
                     char *fingerprint_out, size_t fp_size);

/*
 * knownhosts_add — store host:port → key and persist to disk.
 * Existing entry for the same host:port is replaced (handles key rotation).
 * Returns KNOWNHOSTS_OK or KNOWNHOSTS_ERROR.
 */
int knownhosts_add(KnownHosts *kh,
                   const char *host, int port,
                   const char *key, size_t key_len);

/*
 * knownhosts_free — release all resources held by kh.
 * Safe to call even if knownhosts_init returned an error.
 */
void knownhosts_free(KnownHosts *kh);

#endif /* CONGA_KNOWNHOSTS_H */
