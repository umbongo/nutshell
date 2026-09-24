#ifndef NUTSHELL_CRYPTO_DPAPI_H
#define NUTSHELL_CRYPTO_DPAPI_H

#include <stddef.h>

/*
 * Function-pointer backend for per-user secret protection (DPAPI on
 * Windows). Both members behave like CryptProtectData / CryptUnprotectData:
 * they encrypt/decrypt an opaque byte blob, bound to the current Windows
 * user and machine, so a blob copied to another user or PC -- or one that
 * is simply corrupt -- fails to decrypt rather than producing garbage.
 *
 * Returns CRYPTO_OK (0) on success, a negative CRYPTO_ERR_* code (see
 * crypto.h) on failure. `*out_len` is only meaningful when the call
 * returns CRYPTO_OK. Tests inject a fake backend (see tests/fake_dpapi.h)
 * to exercise the "foreign blob" and "corrupt blob" paths deterministically,
 * without depending on real per-machine DPAPI state.
 */
typedef struct {
    int (*protect)(const unsigned char *in, size_t in_len,
                    unsigned char *out, size_t out_cap, size_t *out_len);
    int (*unprotect)(const unsigned char *in, size_t in_len,
                      unsigned char *out, size_t out_cap, size_t *out_len);
} CryptoDpapiBackend;

/*
 * The platform default backend: real DPAPI (crypt32.dll, loaded at runtime
 * via LoadLibraryExW rather than a static import) on Windows; an
 * "unsupported" stub that always fails on any other platform (native test
 * builds only -- the shipped app is Windows-only).
 */
const CryptoDpapiBackend *crypto_dpapi_default_backend(void);

/*
 * The backend actually in use: whatever crypto_dpapi_set_backend() last
 * installed, or crypto_dpapi_default_backend() when none was installed.
 */
const CryptoDpapiBackend *crypto_dpapi_backend(void);

/*
 * Install a backend for testing. Pass NULL to restore the platform default.
 * The pointer is stored, not copied -- it must outlive its use as the
 * active backend (a `static const` in the caller is the usual case).
 */
void crypto_dpapi_set_backend(const CryptoDpapiBackend *backend);

#endif /* NUTSHELL_CRYPTO_DPAPI_H */
