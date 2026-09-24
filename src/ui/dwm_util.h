#ifndef NUTSHELL_DWM_UTIL_H
#define NUTSHELL_DWM_UTIL_H

#ifdef _WIN32
#include <windows.h>

/* DWMWA_USE_IMMERSIVE_DARK_MODE is attribute 20 (Windows 10 2004+). Defined
 * here rather than pulled from <dwmapi.h> so nothing in this header needs
 * that include -- see ns_dwm_set_dark_mode() below for why. */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* Sets DWMWA_USE_IMMERSIVE_DARK_MODE on hwnd. dwmapi.dll is not a KnownDLL
 * (unlike gdi32, user32, shell32, ...), so a static `-ldwmapi` import would
 * be resolved by the loader from the exe's own directory before System32 --
 * DLL search-order hardening resolves DwmSetWindowAttribute at runtime
 * instead, via LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32), the first
 * time it's needed, and caches the pointer. A no-op, silently, if the DLL or
 * the entry point isn't there (older than Windows 10 2004): the title bar
 * just keeps the system default colour, exactly as before this attribute
 * existed. */
void ns_dwm_set_dark_mode(HWND hwnd, BOOL dark);

#endif /* _WIN32 */
#endif /* NUTSHELL_DWM_UTIL_H */
