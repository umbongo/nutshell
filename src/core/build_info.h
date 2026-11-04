#ifndef NUTSHELL_BUILD_INFO_H
#define NUTSHELL_BUILD_INFO_H

#include <stddef.h>

/*
 * build_info -- the versions of the libraries built into this binary.
 * Portable (no windows.h): it compiles into the MinGW cross-build and the
 * native test build, where it reports whatever headers that build saw.
 */

/* Buffer size callers use for build_info_deps(); the text is well under it. */
#define BUILD_INFO_DEPS_MAX 96

/*
 * Write "OpenSSL <v>, libssh2 <v>, zlib <v>" (zlib "?" when zlib.h is not
 * available at build time) to `out`. ASCII only, because the About dialog
 * draws with DrawTextA and -v goes through cli_output.
 *
 * snprintf-style: the result is always NUL-terminated when cap > 0 and the
 * return value is the length the full text needs, so a result >= cap means
 * it was truncated. `out` may be NULL only with cap == 0 (a length query).
 */
size_t build_info_deps(char *out, size_t cap);

#endif /* NUTSHELL_BUILD_INFO_H */
