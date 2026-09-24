#ifndef NUTSHELL_TEST_FAKE_DPAPI_H
#define NUTSHELL_TEST_FAKE_DPAPI_H

/*
 * A deterministic, reversible fake CryptoDpapiBackend (see
 * src/crypto/crypto_dpapi.h) for tests. Real DPAPI is per-user/per-machine
 * state that a test binary cannot control or portably rely on (it does not
 * even exist on the Linux host that runs `make test` in CI), so every test
 * that needs a secret to actually round-trip installs this instead via
 * crypto_dpapi_set_backend(&k_fake_dpapi_backend) -- tests/runner.c installs
 * it once for the whole run, and tests that need to simulate a blob from a
 * "different user/PC" call fake_dpapi_set_identity() to change which blobs
 * successfully unprotect.
 *
 * Each blob is tagged with a 1-byte "identity": protect() stamps the blob
 * with the currently active identity, unprotect() only succeeds when the
 * blob's identity matches -- exactly the property real DPAPI gives us
 * (a blob protected under one user/machine fails to unprotect under
 * another), without needing real per-machine secret state.
 *
 * Definitions live in fake_dpapi.c (external linkage) rather than being
 * `static` in this header: every test file that includes this header must
 * see and mutate the SAME identity variable and the SAME backend function
 * pointers as tests/runner.c installed, or fake_dpapi_set_identity() calls
 * made from a test file would silently affect only that translation unit's
 * own copy and never the active backend.
 */

#include "crypto_dpapi.h"

/* Switch the "current user/machine" identity. Blobs protected under a
 * previous identity will no longer unprotect until it is switched back --
 * this is how tests simulate a config file moved to another PC. */
void fake_dpapi_set_identity(unsigned char identity);

/* Force protect() (encryption) to fail with CRYPTO_ERR_ENCRYPT until
 * switched back off. Real DPAPI can fail to encrypt (profile unloaded,
 * out of memory, ...); this is how tests simulate that deterministically
 * to exercise the save-time failure path (M-2) without needing to break
 * the real backend. Does not affect unprotect(). */
void fake_dpapi_set_fail_protect(int fail);

extern const CryptoDpapiBackend k_fake_dpapi_backend;

#endif /* NUTSHELL_TEST_FAKE_DPAPI_H */
