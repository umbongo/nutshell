#include "build_info.h"

#include <openssl/opensslv.h>
#include <libssh2.h>
#include <stdio.h>

/* zlib is optional at build time: name it only when its header is there. */
#if defined(__has_include)
#  if __has_include(<zlib.h>)
#    include <zlib.h>
#    define BUILD_INFO_ZLIB_VERSION ZLIB_VERSION
#  endif
#endif
#ifndef BUILD_INFO_ZLIB_VERSION
#  define BUILD_INFO_ZLIB_VERSION "?"
#endif

size_t build_info_deps(char *out, size_t cap)
{
    /* The libssh2 part is formatted from the numeric macros: LIBSSH2_VERSION
     * is "1.11.1_DEV" in vcpkg's build. */
    int n = snprintf(out, cap, "OpenSSL %s, libssh2 %d.%d.%d, zlib %s",
                     OPENSSL_VERSION_STR,
                     LIBSSH2_VERSION_MAJOR, LIBSSH2_VERSION_MINOR,
                     LIBSSH2_VERSION_PATCH,
                     BUILD_INFO_ZLIB_VERSION);
    return n < 0 ? 0 : (size_t)n;
}
