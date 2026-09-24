#ifdef _WIN32

/* Runtime-resolved DwmSetWindowAttribute. See dwm_util.h. */

#include "dwm_util.h"

typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);

/* Same GetProcAddress -> typed-pointer trick as local_pty.c's ConPTY entry
 * points: a union member, not a cast, satisfies both -Wpedantic (no
 * function-pointer <-> void * cast) and -Wcast-function-type. */
typedef union {
    FARPROC                 raw;
    DwmSetWindowAttributeFn fn;
} DwmProcAddr;

static DwmSetWindowAttributeFn g_dwm_set_attr;
static int                     g_dwm_probed;

void ns_dwm_set_dark_mode(HWND hwnd, BOOL dark)
{
    if (!hwnd) return;

    if (!g_dwm_probed) {
        g_dwm_probed = 1;
        HMODULE h = LoadLibraryExW(L"dwmapi.dll", NULL,
                                   LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (h) {
            DwmProcAddr a;
            a.raw = GetProcAddress(h, "DwmSetWindowAttribute");
            g_dwm_set_attr = a.fn;
        }
    }
    if (!g_dwm_set_attr) return;

    g_dwm_set_attr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

#endif /* _WIN32 */
