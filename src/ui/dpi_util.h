/* dpi_util.h — Per-monitor DPI helper shared across UI components.
 *
 * Dynamically loads GetDpiForWindow (Win10 1607+) and falls back to the
 * system-wide LOGPIXELSY value on older builds.
 */

#ifndef NUTSHELL_DPI_UTIL_H
#define NUTSHELL_DPI_UTIL_H

#include <windows.h>

static inline int get_window_dpi(HWND hwnd)
{
    typedef UINT (WINAPI *GetDpiForWindow_fn)(HWND);
    static GetDpiForWindow_fn pfn = NULL;
    static int resolved = 0;
    if (!resolved) {
        HMODULE h = GetModuleHandleA("user32.dll");
        if (h) pfn = (GetDpiForWindow_fn)(void (*)(void))GetProcAddress(h, "GetDpiForWindow");
        resolved = 1;
    }
    if (pfn && hwnd) return (int)pfn(hwnd);
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    return dpi;
}

#endif /* NUTSHELL_DPI_UTIL_H */
