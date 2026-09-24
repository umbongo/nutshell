#include "crypto_dpapi.h"
#include "crypto.h"
#include <string.h>

#ifdef _WIN32

#include <windows.h>
#include <wincrypt.h>

/*
 * Fixed application entropy mixed into every DPAPI call. This is not a
 * secret -- it is public, compiled into the binary -- it only scopes the
 * protection so a blob written by Nutshell cannot be unprotected by some
 * other program's CryptUnprotectData call running as the same user (and
 * vice versa). The real protection is DPAPI's per-user master key.
 */
static const unsigned char k_entropy_bytes[] = "Nutshell-config-v1";

typedef BOOL (WINAPI *PFN_CryptProtectData)(DATA_BLOB *, LPCWSTR, DATA_BLOB *,
                                             PVOID, CRYPTPROTECT_PROMPTSTRUCT *,
                                             DWORD, DATA_BLOB *);
typedef BOOL (WINAPI *PFN_CryptUnprotectData)(DATA_BLOB *, LPWSTR *, DATA_BLOB *,
                                               PVOID, CRYPTPROTECT_PROMPTSTRUCT *,
                                               DWORD, DATA_BLOB *);

typedef struct {
    HMODULE                module;
    PFN_CryptProtectData   protect;
    PFN_CryptUnprotectData unprotect;
} Crypt32Api;

/* GetProcAddress hands back a FARPROC. Casting a function pointer to void *
 * is not ISO C (-Wpedantic) and casting it straight to an incompatible
 * function type trips -Wcast-function-type (in -Wextra), so the address goes
 * through a union -- type punning a union member is defined in C11 and is
 * the one form both warnings accept (see src/ui/local_pty.c for the same
 * pattern with the ConPTY entry points). */
typedef union {
    FARPROC                 raw;
    PFN_CryptProtectData    protect;
    PFN_CryptUnprotectData  unprotect;
} Crypt32ProcAddr;

/* Loads crypt32.dll from the system directory only (never the app's own
 * directory or CWD -- LOAD_LIBRARY_SEARCH_SYSTEM32 -- so a planted DLL next
 * to nutshell.exe cannot be picked up) and resolves the two DPAPI entry
 * points. Returns 0 and a populated *api on success; on failure *api is
 * zeroed and any partially-loaded module is released. */
static int load_crypt32(Crypt32Api *api)
{
    memset(api, 0, sizeof(*api));
    api->module = LoadLibraryExW(L"crypt32.dll", NULL,
                                  LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!api->module) {
        return -1;
    }

    Crypt32ProcAddr a;
    a.raw = GetProcAddress(api->module, "CryptProtectData");
    api->protect = a.protect;
    a.raw = GetProcAddress(api->module, "CryptUnprotectData");
    api->unprotect = a.unprotect;

    if (!api->protect || !api->unprotect) {
        FreeLibrary(api->module);
        memset(api, 0, sizeof(*api));
        return -1;
    }
    return 0;
}

static int win_protect(const unsigned char *in, size_t in_len,
                        unsigned char *out, size_t out_cap, size_t *out_len)
{
    if (!in || !out || !out_len) return CRYPTO_ERR_ARGS;
    if (in_len == 0u || in_len > 65536u) return CRYPTO_ERR_ARGS;

    Crypt32Api api;
    if (load_crypt32(&api) != 0) return CRYPTO_ERR_KEY;

    DATA_BLOB blob_in;
    blob_in.pbData = (BYTE *)in;
    blob_in.cbData = (DWORD)in_len;

    DATA_BLOB entropy;
    entropy.pbData = (BYTE *)k_entropy_bytes;
    entropy.cbData = (DWORD)(sizeof(k_entropy_bytes) - 1u);

    DATA_BLOB blob_out;
    memset(&blob_out, 0, sizeof(blob_out));

    BOOL ok = api.protect(&blob_in, L"Nutshell secret", &entropy, NULL, NULL,
                           CRYPTPROTECT_UI_FORBIDDEN, &blob_out);
    FreeLibrary(api.module);
    if (!ok) return CRYPTO_ERR_ENCRYPT;

    int rc = CRYPTO_OK;
    if ((size_t)blob_out.cbData > out_cap) {
        rc = CRYPTO_ERR_BUFSIZE;
    } else {
        memcpy(out, blob_out.pbData, blob_out.cbData);
        *out_len = (size_t)blob_out.cbData;
    }
    if (blob_out.pbData) {
        SecureZeroMemory(blob_out.pbData, blob_out.cbData);
        LocalFree(blob_out.pbData);
    }
    return rc;
}

static int win_unprotect(const unsigned char *in, size_t in_len,
                          unsigned char *out, size_t out_cap, size_t *out_len)
{
    if (!in || !out || !out_len) return CRYPTO_ERR_ARGS;
    if (in_len == 0u) return CRYPTO_ERR_ARGS;

    Crypt32Api api;
    if (load_crypt32(&api) != 0) return CRYPTO_ERR_KEY;

    DATA_BLOB blob_in;
    blob_in.pbData = (BYTE *)in;
    blob_in.cbData = (DWORD)in_len;

    DATA_BLOB entropy;
    entropy.pbData = (BYTE *)k_entropy_bytes;
    entropy.cbData = (DWORD)(sizeof(k_entropy_bytes) - 1u);

    DATA_BLOB blob_out;
    memset(&blob_out, 0, sizeof(blob_out));

    /* CRYPTPROTECT_UI_FORBIDDEN: never pop a credential UI -- a blob that
     * needs one (or that belongs to another user/machine) simply fails. */
    BOOL ok = api.unprotect(&blob_in, NULL, &entropy, NULL, NULL,
                             CRYPTPROTECT_UI_FORBIDDEN, &blob_out);
    FreeLibrary(api.module);
    if (!ok) return CRYPTO_ERR_DECRYPT;

    int rc = CRYPTO_OK;
    if ((size_t)blob_out.cbData > out_cap) {
        rc = CRYPTO_ERR_BUFSIZE;
    } else {
        memcpy(out, blob_out.pbData, blob_out.cbData);
        *out_len = (size_t)blob_out.cbData;
    }
    if (blob_out.pbData) {
        SecureZeroMemory(blob_out.pbData, blob_out.cbData);
        LocalFree(blob_out.pbData);
    }
    return rc;
}

static const CryptoDpapiBackend k_win_backend = { win_protect, win_unprotect };

const CryptoDpapiBackend *crypto_dpapi_default_backend(void)
{
    return &k_win_backend;
}

#else /* !_WIN32 -- native test builds only; the shipped app is Windows-only */

static int stub_protect(const unsigned char *in, size_t in_len,
                         unsigned char *out, size_t out_cap, size_t *out_len)
{
    (void)in; (void)in_len; (void)out; (void)out_cap; (void)out_len;
    return CRYPTO_ERR_KEY; /* DPAPI is unavailable off Windows */
}

static int stub_unprotect(const unsigned char *in, size_t in_len,
                           unsigned char *out, size_t out_cap, size_t *out_len)
{
    (void)in; (void)in_len; (void)out; (void)out_cap; (void)out_len;
    return CRYPTO_ERR_KEY;
}

static const CryptoDpapiBackend k_stub_backend = { stub_protect, stub_unprotect };

const CryptoDpapiBackend *crypto_dpapi_default_backend(void)
{
    return &k_stub_backend;
}

#endif /* _WIN32 */

static const CryptoDpapiBackend *g_active_backend = NULL;

const CryptoDpapiBackend *crypto_dpapi_backend(void)
{
    return g_active_backend ? g_active_backend : crypto_dpapi_default_backend();
}

void crypto_dpapi_set_backend(const CryptoDpapiBackend *backend)
{
    g_active_backend = backend;
}
